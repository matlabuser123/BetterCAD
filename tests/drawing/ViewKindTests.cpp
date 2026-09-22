#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/drawing/Sheets.hpp>
#include <bettercad/drawing/Views.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using drawing::CuttingPlane;
using drawing::DetailRegion;
using drawing::DrawingScale;
using drawing::HatchSettings;
using drawing::ProjectedDirection;
using drawing::ProjectionConvention;
using drawing::SectionKind;
using drawing::SheetDefinition;
using drawing::StandardView;
using drawing::ViewDefinition;
using drawing::ViewDirection;
using drawing::ViewKind;

// P14-VIEW-002: section, detail and auxiliary views on a sheet.
//
// The part is the same 100 x 60 x 40 box P14-VIEW-001 uses, asymmetric in all
// three axes so a swapped or mirrored orientation changes the answer. Every
// expected number below is arithmetic on those three, done here.
namespace {

constexpr double kMm = 1e-9;
constexpr double kMm2 = 1e6; // SI square metres -> mm^2

struct Fixture {
    Document document{"Drawing"};
    ObjectId part{};
    SheetId sheet{};
    features::Regenerator regenerator;

    drawing::BodyLookup bodies() {
        const features::Regenerator* r = &regenerator;
        return [r](ObjectId object) { return r->body(object); };
    }
    void regenerate() { REQUIRE(regenerator.regenerateAll(document).has_value()); }
};

Fixture makeFixture(ProjectionConvention convention = ProjectionConvention::FirstAngle) {
    Fixture f;
    auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
    addRectangle(*sketch, 0_mm, 0_mm, 100_mm, 60_mm);
    const ObjectId sketchId = require(f.document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Block", {.profile = SketchId::fromValue(sketchId.value()), .depth = 40_mm});
    REQUIRE(extrude.has_value());
    f.part = require(f.document.addObject(std::move(*extrude)));

    SheetDefinition sheet{.format = drawing::SheetFormat::A3,
                          .orientation = drawing::SheetOrientation::Landscape,
                          .margins = drawing::SheetMargins{10_mm, 10_mm, 10_mm, 10_mm},
                          .scale = DrawingScale{1, 1},
                          .convention = convention};
    f.sheet = require(drawing::createSheet(f.document, "Sheet1", sheet));
    f.regenerate();
    return f;
}

/// A right-triangular prism: the triangle (0,0) (40,0) (0,40) in the XY
/// plane, 60 tall. Its slanted face runs from (40,0) to (0,40), so its true
/// width is the hypotenuse 40 * sqrt(2) = 56.5685 mm and its outward normal
/// is (1, 1, 0) / sqrt(2). No standard view faces that squarely, which is
/// what an auxiliary view is for.
Fixture makeWedge() {
    Fixture f;
    auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
    const EntityId a = require(sketch->addPoint(Point2D{0_mm, 0_mm}));
    const EntityId b = require(sketch->addPoint(Point2D{40_mm, 0_mm}));
    const EntityId c = require(sketch->addPoint(Point2D{0_mm, 40_mm}));
    (void)require(sketch->addLine(a, b));
    (void)require(sketch->addLine(b, c));
    (void)require(sketch->addLine(c, a));
    const ObjectId sketchId = require(f.document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Wedge", {.profile = SketchId::fromValue(sketchId.value()), .depth = 60_mm});
    REQUIRE(extrude.has_value());
    f.part = require(f.document.addObject(std::move(*extrude)));

    f.sheet = require(drawing::createSheet(
        f.document, "Sheet1",
        SheetDefinition{.format = drawing::SheetFormat::A3,
                        .orientation = drawing::SheetOrientation::Landscape,
                        .margins = drawing::SheetMargins{10_mm, 10_mm, 10_mm, 10_mm},
                        .scale = DrawingScale{1, 1},
                        .convention = ProjectionConvention::FirstAngle}));
    f.regenerate();
    return f;
}

ViewDefinition baseView(const Fixture& f, StandardView orientation, Point2D placement) {
    return ViewDefinition{.sheet = f.sheet,
                          .source = ObjectReference{f.part},
                          .orientation = orientation,
                          .placement = placement};
}

/// A section cutting across the block's width at x = 50, looked at from +X.
CuttingPlane acrossWidth() {
    CuttingPlane plane;
    plane.origin = Point3D{50_mm, 0_mm, 0_mm};
    plane.normal = Direction3D::unitX();
    plane.reference = Direction3D::unitZ();
    return plane;
}

ViewDefinition sectionView(const Fixture& f, ViewId parent, const CuttingPlane& plane,
                           Length spacing) {
    return ViewDefinition{.kind = ViewKind::Section,
                          .sheet = f.sheet,
                          .parent = parent,
                          .section = plane,
                          .spacing = spacing};
}

double apart(const Point2D& a, const Point2D& b) {
    return std::hypot((a.x - b.x).in(units::mm), (a.y - b.y).in(units::mm));
}

} // namespace

// --- The placement rule ---------------------------------------------------------------

TEST_CASE("SheetDisplacement_AgreesWithPlacementStepOnEveryOrthogonalDirection",
          "[drawing][view][p14]") {
    // A section or auxiliary view looks a way no ProjectedDirection names, so
    // its side of the sheet comes from its own normal rather than a table.
    // Where the two overlap they must give the same answer, or the four
    // standard projections and everything else would follow different rules.
    //
    // The expected normals are read off the Front basis directly: its right
    // is +X and its up is +Z, so a Top view (looking down from +Z) has normal
    // (0, 1) in those axes, a Right view (+X) has (1, 0), and so on.
    const auto front = drawing::basisOf(StandardView::Front);
    REQUIRE(front.has_value());

    for (const ProjectionConvention convention :
         {ProjectionConvention::FirstAngle, ProjectionConvention::ThirdAngle}) {
        for (const auto& [direction, standard] :
             std::vector<std::pair<ProjectedDirection, StandardView>>{
                 {ProjectedDirection::Top, StandardView::Top},
                 {ProjectedDirection::Bottom, StandardView::Bottom},
                 {ProjectedDirection::Left, StandardView::Left},
                 {ProjectedDirection::Right, StandardView::Right}}) {
            INFO(drawing::toString(direction) << ", " << drawing::toString(convention));
            const auto basis = drawing::basisOf(standard);
            REQUIRE(basis.has_value());
            const Direction3D& n = basis->normal();
            const auto general = drawing::sheetDisplacement(
                {front->xAxis().dot(n), front->yAxis().dot(n)}, convention);
            const auto tabulated = drawing::placementStep(direction, convention);
            CHECK_THAT(general.first, WithinAbs(tabulated.first, 1e-12));
            CHECK_THAT(general.second, WithinAbs(tabulated.second, 1e-12));
        }
    }
}

TEST_CASE("SheetDisplacement_HasNoSideWhenTheChildLooksTheParentsWay", "[drawing][view][p14]") {
    // Looking the same way as the parent, or exactly against it, gives a view
    // no side of the sheet to be on. Reported as (0, 0) rather than as an
    // arbitrary direction.
    for (const ProjectionConvention convention :
         {ProjectionConvention::FirstAngle, ProjectionConvention::ThirdAngle}) {
        const auto same = drawing::sheetDisplacement({0.0, 0.0}, convention);
        CHECK(same.first == 0.0);
        CHECK(same.second == 0.0);
    }
}

// --- Section views --------------------------------------------------------------------

TEST_CASE("SectionView_LooksThroughItsOwnCuttingPlane", "[drawing][view][section][p14]") {
    // A section view's basis IS its cutting plane's frame, so the two can
    // never disagree about which way the section is seen.
    Fixture f = makeFixture();
    const ViewId front = require(
        drawing::createView(f.document, "Front", baseView(f, StandardView::Front, {200_mm, 150_mm})));
    const ViewId section = require(
        drawing::createView(f.document, "SectionAA", sectionView(f, front, acrossWidth(), 80_mm)));

    const auto basis = drawing::effectiveBasis(f.document, section);
    REQUIRE(basis.has_value());
    CHECK(basis->normal() == Direction3D::unitX());
    CHECK(basis->xAxis() == Direction3D::unitZ());
}

TEST_CASE("SectionView_IsPlacedOnTheSideItsOwnDirectionPutsIt", "[drawing][view][section][p14]") {
    // The cut is seen from +X, which is the Right view's direction, so in
    // first angle the section lands to the LEFT of its parent -- the same
    // side placementStep gives a Right view, because it is the same rule.
    Fixture f = makeFixture();
    const ViewId front = require(
        drawing::createView(f.document, "Front", baseView(f, StandardView::Front, {200_mm, 150_mm})));
    const ViewId section =
        require(drawing::createView(f.document, "SectionAA", sectionView(f, front, acrossWidth(), 80_mm)));

    const auto placement = drawing::effectivePlacement(f.document, section);
    REQUIRE(placement.has_value());
    CHECK_THAT(placement->x.in(units::mm), WithinAbs(120.0, kMm)); // 200 - 80
    CHECK_THAT(placement->y.in(units::mm), WithinAbs(150.0, kMm)); // aligned

    // Third angle puts it on the other side, and changes nothing else.
    Fixture third = makeFixture(ProjectionConvention::ThirdAngle);
    const ViewId thirdFront = require(drawing::createView(
        third.document, "Front", baseView(third, StandardView::Front, {200_mm, 150_mm})));
    const ViewId thirdSection = require(drawing::createView(
        third.document, "SectionAA", sectionView(third, thirdFront, acrossWidth(), 80_mm)));
    const auto thirdPlacement = drawing::effectivePlacement(third.document, thirdSection);
    REQUIRE(thirdPlacement.has_value());
    CHECK_THAT(thirdPlacement->x.in(units::mm), WithinAbs(280.0, kMm)); // 200 + 80
    CHECK_THAT(thirdPlacement->y.in(units::mm), WithinAbs(150.0, kMm));
}

TEST_CASE("SectionView_CutFaceHasTheAreaTheBlocksCrossSectionHas",
          "[drawing][view][section][p14]") {
    // Cutting across the width exposes the block's 60 x 40 cross-section:
    // 2400 mm^2, computed here from the two extents and not from the routine
    // under test.
    Fixture f = makeFixture();
    const ViewId front = require(
        drawing::createView(f.document, "Front", baseView(f, StandardView::Front, {200_mm, 150_mm})));
    const ViewId section =
        require(drawing::createView(f.document, "SectionAA", sectionView(f, front, acrossWidth(), 80_mm)));

    const auto cut = drawing::sectionOf(f.document, section, f.bodies());
    REQUIRE(cut.has_value());
    REQUIRE(cut->loops.size() == 1);
    CHECK(cut->loops.front().outer);
    CHECK_THAT(cut->area.si() * kMm2, WithinRel(2400.0, 1e-9));
    CHECK_FALSE(cut->hatch.empty());

    // It is drawn where the view was placed.
    const auto placement = drawing::effectivePlacement(f.document, section);
    REQUIRE(placement.has_value());
    double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
    for (const Point2D& p : cut->loops.front().points) {
        minX = std::min(minX, p.x.in(units::mm));
        maxX = std::max(maxX, p.x.in(units::mm));
        minY = std::min(minY, p.y.in(units::mm));
        maxY = std::max(maxY, p.y.in(units::mm));
    }
    CHECK_THAT(0.5 * (minX + maxX), WithinAbs(placement->x.in(units::mm), 1e-6));
    CHECK_THAT(0.5 * (minY + maxY), WithinAbs(placement->y.in(units::mm), 1e-6));
    // Seen from +X the sheet axes are +Z across and -Y up, so the cut face
    // draws 40 wide (the block height) by 60 tall (its depth) -- 2400 mm^2
    // either way round, which is why both extents are asserted and not just
    // the area.
    CHECK_THAT(maxX - minX, WithinAbs(40.0, 1e-6));
    CHECK_THAT(maxY - minY, WithinAbs(60.0, 1e-6));
}

TEST_CASE("SectionView_DrawsTheCutSolidAndNotTheWholeOne", "[drawing][view][section][p14]") {
    // The outline a section view projects must be of what is LEFT after the
    // cut. Half the block's width goes, so the projection is half as deep as
    // a plain Right view of the same block.
    Fixture f = makeFixture();
    const ViewId front = require(
        drawing::createView(f.document, "Front", baseView(f, StandardView::Front, {200_mm, 150_mm})));
    const ViewId section =
        require(drawing::createView(f.document, "SectionAA", sectionView(f, front, acrossWidth(), 80_mm)));

    const auto drawn = drawing::projectedGeometry(f.document, section, f.bodies());
    REQUIRE(drawn.has_value());
    // Seen from +X the sheet's axes are +Z across and -Y up, so the outline
    // is 60 (the depth) by 40 (the height) whether or not it was cut -- what
    // the cut changes is the SOLID, so that is asserted directly.
    const auto solid = drawing::sectionOf(f.document, section, f.bodies());
    REQUIRE(solid.has_value());

    const geometry::Body* whole = f.regenerator.body(f.part);
    REQUIRE(whole != nullptr);
    const auto basis = drawing::effectiveBasis(f.document, section);
    REQUIRE(basis.has_value());
    const auto cutSolid = drawing::cutBody(*whole, acrossWidth(), *basis);
    REQUIRE(cutSolid.has_value());
    const auto bounds = cutSolid->boundingBox();
    REQUIRE(bounds.has_value());
    CHECK_THAT(bounds->min.x.in(units::mm), WithinAbs(0.0, 1e-6));
    CHECK_THAT(bounds->max.x.in(units::mm), WithinAbs(50.0, 1e-6)); // half of 100
    CHECK_FALSE(drawn->segments.empty());
}

TEST_CASE("SectionView_HalfAndOffsetCutsReachTheDocumentUnchanged",
          "[drawing][view][section][p14]") {
    // The cut a section VIEW makes must be the cut the geometry makes, so the
    // two are compared directly rather than each asserted against a picture.
    Fixture f = makeFixture();
    const ViewId front = require(
        drawing::createView(f.document, "Front", baseView(f, StandardView::Front, {200_mm, 150_mm})));

    CuttingPlane half = acrossWidth();
    half.kind = SectionKind::Half;
    half.splitAt = 0_mm; // the plane's X axis is +Z, so this cuts z > 0: all of it

    CuttingPlane offset = acrossWidth();
    offset.kind = SectionKind::Offset;
    offset.legs.push_back({20_mm, 10_mm});

    for (const auto& [name, plane] :
         std::vector<std::pair<std::string, CuttingPlane>>{{"half", half}, {"offset", offset}}) {
        INFO(name << " section");
        const ViewId id =
            require(drawing::createView(f.document, "Section_" + name, sectionView(f, front, plane, 80_mm)));
        const auto throughView = drawing::sectionOf(f.document, id, f.bodies());
        REQUIRE(throughView.has_value());

        const geometry::Body* body = f.regenerator.body(f.part);
        REQUIRE(body != nullptr);
        const auto basis = drawing::effectiveBasis(f.document, id);
        const auto placement = drawing::effectivePlacement(f.document, id);
        const auto scale = drawing::effectiveScale(f.document, id);
        REQUIRE(basis.has_value());
        REQUIRE(placement.has_value());
        REQUIRE(scale.has_value());
        const auto direct = drawing::sectionGeometry(*body, plane, *basis, scale->factor(),
                                                     *placement, HatchSettings{});
        REQUIRE(direct.has_value());
        CHECK(throughView->loops.size() == direct->loops.size());
        CHECK(throughView->area.si() == direct->area.si());
        CHECK(throughView->hatch.size() == direct->hatch.size());
    }
}

TEST_CASE("SectionView_AViewThatCutsNothingHasNoCutFaces", "[drawing][view][section][p14]") {
    Fixture f = makeFixture();
    const ViewId front = require(
        drawing::createView(f.document, "Front", baseView(f, StandardView::Front, {200_mm, 150_mm})));
    const auto cut = drawing::sectionOf(f.document, front, f.bodies());
    REQUIRE_FALSE(cut.has_value());
    CHECK(errorCode(cut) == ErrorCode::InvalidArgument);
    CHECK_THAT(cut.error().message, ContainsSubstring("cuts nothing"));
}

// --- Detail views ---------------------------------------------------------------------

TEST_CASE("DetailView_EnlargesItsRegionByTheRatioOfTheTwoScales",
          "[drawing][view][detail][p14]") {
    // The Front view at 1:1 spans x 150..250 and y 130..170, so its
    // top-right corner is (250, 170). A 2:1 detail of a 20 mm circle there,
    // placed at (300, 100), maps the corner onto the placement and doubles
    // every distance from it -- so the clipped top edge, 20 mm long in the
    // parent, is drawn 40 mm long.
    Fixture f = makeFixture();
    const ViewId front = require(
        drawing::createView(f.document, "Front", baseView(f, StandardView::Front, {200_mm, 150_mm})));

    ViewDefinition d{.kind = ViewKind::Detail,
                     .sheet = f.sheet,
                     .parent = front,
                     .detail = DetailRegion{Point2D{250_mm, 170_mm}, 20_mm, true},
                     .scale = DrawingScale{2, 1},
                     .placement = Point2D{300_mm, 100_mm}};
    const ViewId detail = require(drawing::createView(f.document, "DetailB", d));

    const auto drawn = drawing::projectedGeometry(f.document, detail, f.bodies());
    REQUIRE(drawn.has_value());
    REQUIRE_FALSE(drawn->segments.empty());

    // Nothing is drawn further from the placement than the enlarged radius.
    for (const Point2D& p : drawn->points) {
        INFO("(" << p.x.in(units::mm) << ", " << p.y.in(units::mm) << ")");
        CHECK(apart(p, Point2D{300_mm, 100_mm}) <= 40.0 + 1e-6);
    }
    // And something reaches it, so the crop is at the circle and not inside it.
    double furthest = 0.0;
    for (const Point2D& p : drawn->points) {
        furthest = std::max(furthest, apart(p, Point2D{300_mm, 100_mm}));
    }
    CHECK_THAT(furthest, WithinAbs(40.0, 1e-6));

    // The clipped top edge, exactly: (230,170)..(250,170) becomes
    // (260,100)..(300,100).
    const bool hasTopEdge = std::ranges::any_of(drawn->segments, [](const auto& segment) {
        const auto& [a, b] = segment;
        const Point2D left = a.x.si() <= b.x.si() ? a : b;
        const Point2D right = a.x.si() <= b.x.si() ? b : a;
        return std::abs(left.x.in(units::mm) - 260.0) < 1e-6 &&
               std::abs(left.y.in(units::mm) - 100.0) < 1e-6 &&
               std::abs(right.x.in(units::mm) - 300.0) < 1e-6 &&
               std::abs(right.y.in(units::mm) - 100.0) < 1e-6;
    });
    CHECK(hasTopEdge);
}

TEST_CASE("DetailView_UncroppedKeepsWholeTheSegmentsItsCircleTouches",
          "[drawing][view][detail][p14]") {
    // A partial view: enlarged, but not cut off at the circle. The same top
    // edge is kept at its full 100 mm, drawn 200 mm long at 2:1.
    Fixture f = makeFixture();
    const ViewId front = require(
        drawing::createView(f.document, "Front", baseView(f, StandardView::Front, {200_mm, 150_mm})));

    ViewDefinition d{.kind = ViewKind::Detail,
                     .sheet = f.sheet,
                     .parent = front,
                     .detail = DetailRegion{Point2D{250_mm, 170_mm}, 20_mm, false},
                     .scale = DrawingScale{2, 1},
                     .placement = Point2D{300_mm, 100_mm}};
    const ViewId detail = require(drawing::createView(f.document, "DetailB", d));

    const auto drawn = drawing::projectedGeometry(f.document, detail, f.bodies());
    REQUIRE(drawn.has_value());
    double longest = 0.0;
    for (const auto& [a, b] : drawn->segments) {
        longest = std::max(longest, apart(a, b));
    }
    CHECK_THAT(longest, WithinAbs(200.0, 1e-6));
}

TEST_CASE("DetailView_ARegionThatReachesNothingIsRefused", "[drawing][view][detail][p14]") {
    Fixture f = makeFixture();
    const ViewId front = require(
        drawing::createView(f.document, "Front", baseView(f, StandardView::Front, {200_mm, 150_mm})));

    ViewDefinition d{.kind = ViewKind::Detail,
                     .sheet = f.sheet,
                     .parent = front,
                     .detail = DetailRegion{Point2D{1000_mm, 1000_mm}, 5_mm, true},
                     .placement = Point2D{300_mm, 100_mm}};
    const ViewId detail = require(drawing::createView(f.document, "DetailB", d));

    const auto drawn = drawing::projectedGeometry(f.document, detail, f.bodies());
    REQUIRE_FALSE(drawn.has_value());
    CHECK(errorCode(drawn) == ErrorCode::FailedPrecondition);
    CHECK_THAT(drawn.error().message, ContainsSubstring("nothing reaches"));
}

// --- Auxiliary views ------------------------------------------------------------------

TEST_CASE("AuxiliaryView_ShowsAnInclinedFaceAtTrueLength", "[drawing][view][auxiliary][p14]") {
    // The whole reason auxiliary views exist, as a number.
    //
    // The wedge's slanted face runs from (40, 0) to (0, 40), so its true
    // width is the hypotenuse sqrt(40^2 + 40^2) = 56.5685 mm. A Front view
    // looks along -Y and sees it foreshortened to 40 mm. An auxiliary view
    // along the face's own normal must show all 56.5685 of it.
    Fixture f = makeWedge();
    const double trueLength = 40.0 * std::numbers::sqrt2;

    const ViewId front = require(
        drawing::createView(f.document, "Front", baseView(f, StandardView::Front, {200_mm, 150_mm})));
    const auto frontGeometry = drawing::projectedGeometry(f.document, front, f.bodies());
    REQUIRE(frontGeometry.has_value());
    const double frontWidth =
        (frontGeometry->bounds.max.x - frontGeometry->bounds.min.x).in(units::mm);
    CHECK_THAT(frontWidth, WithinAbs(40.0, 1e-6)); // foreshortened

    const auto normal = Direction3D::fromComponents(1.0, 1.0, 0.0);
    REQUIRE(normal.has_value());
    ViewDefinition d{.kind = ViewKind::Auxiliary,
                     .sheet = f.sheet,
                     .parent = front,
                     .auxiliary = ViewDirection{*normal, Direction3D::unitZ()},
                     .spacing = 90_mm};
    const ViewId auxiliary = require(drawing::createView(f.document, "ViewC", d));

    const auto drawn = drawing::projectedGeometry(f.document, auxiliary, f.bodies());
    REQUIRE(drawn.has_value());
    // The view's up axis is normal x reference = (1,-1,0)/sqrt(2), along
    // which the slanted face spans its full hypotenuse; its right axis is +Z,
    // along which the prism is 60 tall.
    const double across = (drawn->bounds.max.x - drawn->bounds.min.x).in(units::mm);
    const double up = (drawn->bounds.max.y - drawn->bounds.min.y).in(units::mm);
    CHECK_THAT(across, WithinAbs(60.0, 1e-6));
    CHECK_THAT(up, WithinRel(trueLength, 1e-9));
    CHECK(up > frontWidth); // the point of the whole exercise
}

TEST_CASE("AuxiliaryView_IsPlacedAlongItsOwnDirectionOfSight",
          "[drawing][view][auxiliary][p14]") {
    // The auxiliary looks from (1,1,0)/sqrt(2). In the Front view's axes
    // (right +X, up +Z) that is (0.7071, 0), which normalises to (1, 0), so
    // first angle puts it 90 mm to the LEFT of its parent.
    Fixture f = makeWedge();
    const ViewId front = require(
        drawing::createView(f.document, "Front", baseView(f, StandardView::Front, {200_mm, 150_mm})));
    const auto normal = Direction3D::fromComponents(1.0, 1.0, 0.0);
    REQUIRE(normal.has_value());
    ViewDefinition d{.kind = ViewKind::Auxiliary,
                     .sheet = f.sheet,
                     .parent = front,
                     .auxiliary = ViewDirection{*normal, Direction3D::unitZ()},
                     .spacing = 90_mm};
    const ViewId auxiliary = require(drawing::createView(f.document, "ViewC", d));

    const auto placement = drawing::effectivePlacement(f.document, auxiliary);
    REQUIRE(placement.has_value());
    CHECK_THAT(placement->x.in(units::mm), WithinAbs(110.0, kMm)); // 200 - 90
    CHECK_THAT(placement->y.in(units::mm), WithinAbs(150.0, kMm));
}

TEST_CASE("AuxiliaryView_RefusesADirectionItCannotBuildAFrameFrom",
          "[drawing][view][auxiliary][p14]") {
    Fixture f = makeWedge();
    const ViewId front = require(
        drawing::createView(f.document, "Front", baseView(f, StandardView::Front, {200_mm, 150_mm})));
    ViewDefinition d{.kind = ViewKind::Auxiliary,
                     .sheet = f.sheet,
                     .parent = front,
                     .auxiliary = ViewDirection{Direction3D::unitX(), Direction3D::unitX()},
                     .spacing = 90_mm};
    const auto id = drawing::createView(f.document, "ViewC", d);
    REQUIRE_FALSE(id.has_value());
    CHECK_THAT(id.error().message, ContainsSubstring("must not be parallel"));
}

// --- The kinds do not blur into one another -------------------------------------------

TEST_CASE("ViewKind_ADefinitionCarryingAnotherKindsIntentIsRefused", "[drawing][view][p14]") {
    // Two kinds' worth of intent in one definition means whoever built it was
    // confused about which it was making. Refused, rather than one of them
    // silently winning.
    Fixture f = makeFixture();
    const ViewId front = require(
        drawing::createView(f.document, "Front", baseView(f, StandardView::Front, {200_mm, 150_mm})));

    const auto refuse = [&](std::string_view what, ViewDefinition d, std::string_view expected) {
        INFO(what);
        const auto result = drawing::createView(f.document, "Bad", d);
        REQUIRE_FALSE(result.has_value());
        CHECK_THAT(result.error().message, ContainsSubstring(std::string{expected}));
    };

    ViewDefinition section = sectionView(f, front, acrossWidth(), 80_mm);
    section.detail = DetailRegion{Point2D{}, 5_mm, true};
    refuse("a section that is also a detail", section, "takes no detail region");

    ViewDefinition detail{.kind = ViewKind::Detail,
                          .sheet = f.sheet,
                          .parent = front,
                          .detail = DetailRegion{Point2D{250_mm, 170_mm}, 20_mm, true},
                          .placement = Point2D{300_mm, 100_mm}};
    ViewDefinition detailWithPlane = detail;
    detailWithPlane.section = acrossWidth();
    refuse("a detail that is also a section", detailWithPlane, "takes no cutting plane");

    ViewDefinition sectionNoPlane = section;
    sectionNoPlane.detail.reset();
    sectionNoPlane.section.reset();
    refuse("a section with no plane", sectionNoPlane, "must name the plane it cuts on");

    ViewDefinition detailPlaced = detail;
    detailPlaced.spacing = 40_mm;
    refuse("a detail given a spacing", detailPlaced, "takes no spacing");

    ViewDefinition sectionPlaced = sectionView(f, front, acrossWidth(), 80_mm);
    sectionPlaced.placement = Point2D{10_mm, 10_mm};
    refuse("a section given a placement", sectionPlaced, "placement is derived");

    ViewDefinition sectionWithSource = sectionView(f, front, acrossWidth(), 80_mm);
    sectionWithSource.source = ObjectReference{f.part};
    refuse("a section naming its own source", sectionWithSource, "takes its source from its parent");
}

TEST_CASE("ViewKind_EveryKindRoundTripsThroughTheFile", "[drawing][view][p14][persistence]") {
    // Create -> save -> destroy -> load -> compare, for each new kind, so a
    // drawing that was cut, detailed or set at an angle comes back as the
    // same drawing rather than as a plausible one.
    Fixture f = makeFixture();
    const ViewId front = require(
        drawing::createView(f.document, "Front", baseView(f, StandardView::Front, {200_mm, 150_mm})));

    CuttingPlane offset = acrossWidth();
    offset.kind = SectionKind::Offset;
    offset.legs.push_back({20_mm, 10_mm});
    offset.legs.push_back({35_mm, -4_mm});
    ViewDefinition sectionDefinition = sectionView(f, front, offset, 80_mm);
    sectionDefinition.hatch = HatchSettings{Angle::fromSi(0.5), 3_mm, "iso-45"};
    const ViewId section = require(drawing::createView(f.document, "SectionAA", sectionDefinition));

    const auto normal = Direction3D::fromComponents(1.0, 2.0, 3.0);
    REQUIRE(normal.has_value());
    ViewDefinition detailDefinition{.kind = ViewKind::Detail,
                                    .sheet = f.sheet,
                                    .parent = front,
                                    .detail = DetailRegion{Point2D{250_mm, 170_mm}, 20_mm, false},
                                    .scale = DrawingScale{5, 2},
                                    .placement = Point2D{300_mm, 100_mm}};
    const ViewId detail = require(drawing::createView(f.document, "DetailB", detailDefinition));

    ViewDefinition auxiliaryDefinition{.kind = ViewKind::Auxiliary,
                                       .sheet = f.sheet,
                                       .parent = front,
                                       .auxiliary = ViewDirection{*normal, Direction3D::unitZ()},
                                       .spacing = 90_mm};
    const ViewId auxiliary = require(drawing::createView(f.document, "ViewC", auxiliaryDefinition));

    const TempDir directory;
    const std::filesystem::path path = directory.path() / "kinds.bcad";
    REQUIRE(io::saveDocument(f.document, path).has_value());
    const auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());

    for (const auto& [id, expected] :
         std::vector<std::pair<ViewId, ViewDefinition>>{{section, sectionDefinition},
                                                        {detail, detailDefinition},
                                                        {auxiliary, auxiliaryDefinition}}) {
        INFO(drawing::toString(expected.kind) << " view");
        const drawing::View* view = drawing::findView(*loaded, id);
        REQUIRE(view != nullptr);
        CHECK(view->definition() == expected);
    }

    // And the loaded drawing derives the same numbers as the original did.
    for (const ViewId id : {section, detail, auxiliary}) {
        const auto before = drawing::effectivePlacement(f.document, id);
        const auto after = drawing::effectivePlacement(*loaded, id);
        REQUIRE(before.has_value());
        REQUIRE(after.has_value());
        CHECK(before->x.si() == after->x.si());
        CHECK(before->y.si() == after->y.si());
    }
}

TEST_CASE("ViewKind_AFileNamingAnUnknownKindIsRefused", "[drawing][view][p14][persistence]") {
    // The reader dispatches on the kind, so an unknown one must stop the load
    // rather than fall through to whichever branch happened to be last.
    Fixture f = makeFixture();
    (void)require(
        drawing::createView(f.document, "Front", baseView(f, StandardView::Front, {200_mm, 150_mm})));
    const TempDir directory;
    const std::filesystem::path path = directory.path() / "bad.bcad";
    REQUIRE(io::saveDocument(f.document, path).has_value());

    std::string text = readFile(path);
    // The document is written pretty-printed, so the key and its value are
    // separated by a space.
    const std::string from = R"("kind": "base")";
    const auto at = text.find(from);
    REQUIRE(at != std::string::npos);
    text.replace(at, from.size(), R"("kind": "cutaway")");
    writeFile(path, text);

    const auto loaded = io::loadDocument(path);
    REQUIRE_FALSE(loaded.has_value());
    CHECK_THAT(loaded.error().message, ContainsSubstring("unknown view kind 'cutaway'"));
}

// --- Derived, never stored ------------------------------------------------------------

TEST_CASE("SectionView_FollowsTheModelWhenAParameterChanges",
          "[drawing][view][section][p14]") {
    // Drawing geometry is derived on every request and never stored
    // (ADR-011), so the guarantee worth asserting is that nothing can go
    // stale: change the block's height and the cut face must change with it,
    // with no drawing edit in between.
    //
    // The cut across the width exposes the block's depth by its height, so
    // 60 x 40 = 2400 mm^2 becomes 60 x 25 = 1500 mm^2.
    Fixture f = makeFixture();
    const ViewId front = require(
        drawing::createView(f.document, "Front", baseView(f, StandardView::Front, {200_mm, 150_mm})));
    const ViewId section = require(
        drawing::createView(f.document, "SectionAA", sectionView(f, front, acrossWidth(), 80_mm)));

    const auto before = drawing::sectionOf(f.document, section, f.bodies());
    REQUIRE(before.has_value());
    CHECK_THAT(before->area.si() * kMm2, WithinRel(2400.0, 1e-9));

    const auto* extrude = f.document.findObjectAs<features::ExtrudeFeature>(f.part);
    REQUIRE(extrude != nullptr);
    auto definition = extrude->definition();
    definition.depth = 25_mm;
    REQUIRE(f.document
                .modifyObject<features::ExtrudeFeature>(
                    f.part, [&](features::ExtrudeFeature& e) { return e.setDefinition(definition); })
                .has_value());
    f.regenerate();

    const auto after = drawing::sectionOf(f.document, section, f.bodies());
    REQUIRE(after.has_value());
    CHECK_THAT(after->area.si() * kMm2, WithinRel(1500.0, 1e-9));
    CHECK(after->hatch.size() < before->hatch.size()); // a smaller face, less hatch
}

TEST_CASE("SectionView_DrawsTheSameCutAfterASaveAndLoad",
          "[drawing][view][section][p14][persistence]") {
    // The definition round-tripping is not the claim that matters; what
    // matters is that the loaded document DERIVES the same drawing. Compared
    // number for number rather than by eye.
    Fixture f = makeFixture();
    const ViewId front = require(
        drawing::createView(f.document, "Front", baseView(f, StandardView::Front, {200_mm, 150_mm})));

    CuttingPlane offset = acrossWidth();
    offset.kind = SectionKind::Offset;
    offset.legs.push_back({15_mm, 8_mm});
    const ViewId section =
        require(drawing::createView(f.document, "SectionAA", sectionView(f, front, offset, 80_mm)));

    const auto before = drawing::sectionOf(f.document, section, f.bodies());
    REQUIRE(before.has_value());

    const TempDir directory;
    const std::filesystem::path path = directory.path() / "section.bcad";
    REQUIRE(io::saveDocument(f.document, path).has_value());
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());

    features::Regenerator reloaded;
    REQUIRE(reloaded.regenerateAll(*loaded).has_value());
    const features::Regenerator* r = &reloaded;
    const auto after =
        drawing::sectionOf(*loaded, section, [r](ObjectId object) { return r->body(object); });
    REQUIRE(after.has_value());

    REQUIRE(before->loops.size() == after->loops.size());
    for (std::size_t i = 0; i < before->loops.size(); ++i) {
        INFO("loop " << i);
        CHECK(before->loops[i].outer == after->loops[i].outer);
        REQUIRE(before->loops[i].points.size() == after->loops[i].points.size());
        for (std::size_t j = 0; j < before->loops[i].points.size(); ++j) {
            CHECK(before->loops[i].points[j].x.si() == after->loops[i].points[j].x.si());
            CHECK(before->loops[i].points[j].y.si() == after->loops[i].points[j].y.si());
        }
    }
    CHECK(before->area.si() == after->area.si());
    CHECK(before->hatch.size() == after->hatch.size());
}
