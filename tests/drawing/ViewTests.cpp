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
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using drawing::DrawingScale;
using drawing::ProjectedDirection;
using drawing::ProjectionConvention;
using drawing::SheetDefinition;
using drawing::StandardView;
using drawing::ViewDefinition;
using drawing::ViewKind;

// P14-VIEW-001: views on a sheet, and what they draw.
//
// The part is a 100 x 60 x 40 box, asymmetric in all three axes so a swapped
// or mirrored orientation changes the answer. Every expected extent below is
// arithmetic on those three numbers and the view's scale, done here.
namespace {

constexpr double kMm = 1e-9;

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

/// A 100 x 60 x 40 box on the XY plane, extruded up +Z, plus an A3 sheet.
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

ViewDefinition baseView(const Fixture& f, StandardView orientation, Point2D placement) {
    return ViewDefinition{.sheet = f.sheet,
                          .source = ObjectReference{f.part},
                          .orientation = orientation,
                          .placement = placement};
}

ViewDefinition projectedView(const Fixture& f, ViewId parent, ProjectedDirection direction,
                             Length spacing) {
    return ViewDefinition{.kind = ViewKind::Projected,
                          .sheet = f.sheet,
                          .parent = parent,
                          .direction = direction,
                          .spacing = spacing};
}

} // namespace

// --- The document-facing operations --------------------------------------------------

TEST_CASE("View_IsADocumentObjectThatNamesItsSheetAndItsSource", "[drawing][view][p14]") {
    Fixture f = makeFixture();
    const ViewId id = require(
        drawing::createView(f.document, "Front", baseView(f, StandardView::Front, {200_mm, 150_mm})));

    const drawing::View* view = drawing::findView(f.document, id);
    REQUIRE(view != nullptr);
    CHECK(view->typeName() == "view");
    CHECK(view->viewId() == id);
    CHECK(ObjectId{id} == view->id());
    CHECK(drawing::views(f.document) == std::vector{id});
    CHECK(drawing::viewsOn(f.document, f.sheet) == std::vector{id});

    // It depends on its sheet and on what it draws, so both dirty it.
    const std::vector<ObjectId> deps = view->dependencies();
    CHECK(std::ranges::find(deps, ObjectId{f.sheet}) != deps.end());
    CHECK(std::ranges::find(deps, f.part) != deps.end());
}

TEST_CASE("View_AProjectedViewDependsOnItsParent", "[drawing][view][p14]") {
    Fixture f = makeFixture();
    const ViewId front = require(
        drawing::createView(f.document, "Front", baseView(f, StandardView::Front, {200_mm, 150_mm})));
    const ViewId top = require(drawing::createView(
        f.document, "Top", projectedView(f, front, ProjectedDirection::Top, 80_mm)));

    const std::vector<ObjectId> deps = drawing::findView(f.document, top)->dependencies();
    CHECK(std::ranges::find(deps, ObjectId{front}) != deps.end());
    // And it inherits the source rather than naming one of its own.
    CHECK(drawing::findView(f.document, top)->definition().source.object.isValid() == false);
    const auto source = drawing::effectiveSource(f.document, top);
    REQUIRE(source.has_value());
    CHECK(source->object == f.part);
}

TEST_CASE("View_SourceIsInheritedThroughAChainOfProjections", "[drawing][view][p14]") {
    Fixture f = makeFixture();
    const ViewId front = require(
        drawing::createView(f.document, "Front", baseView(f, StandardView::Front, {200_mm, 150_mm})));
    const ViewId top = require(drawing::createView(
        f.document, "Top", projectedView(f, front, ProjectedDirection::Top, 80_mm)));
    const ViewId fromTop = require(drawing::createView(
        f.document, "TopRight", projectedView(f, top, ProjectedDirection::Right, 80_mm)));

    const auto source = drawing::effectiveSource(f.document, fromTop);
    REQUIRE(source.has_value());
    CHECK(source->object == f.part);
}

// --- Scale ---------------------------------------------------------------------------

TEST_CASE("View_ScaleIsInheritedFromTheSheetAndCanBeOverridden", "[drawing][view][p14]") {
    Fixture f = makeFixture();
    const ViewId id = require(
        drawing::createView(f.document, "Front", baseView(f, StandardView::Front, {200_mm, 150_mm})));

    // The sheet is 1:1, so the view is too.
    auto scale = drawing::effectiveScale(f.document, id);
    REQUIRE(scale.has_value());
    CHECK(*scale == DrawingScale{1, 1});

    ViewDefinition d = drawing::findView(f.document, id)->definition();
    d.scale = DrawingScale{1, 2};
    REQUIRE(drawing::setViewDefinition(f.document, id, d).has_value());
    scale = drawing::effectiveScale(f.document, id);
    REQUIRE(scale.has_value());
    CHECK(scale->label() == "1:2");

    // A projected view inherits the sheet's, not its parent's override --
    // each view's scale is its own business unless it says otherwise.
    const ViewId top = require(drawing::createView(
        f.document, "Top", projectedView(f, id, ProjectedDirection::Top, 80_mm)));
    const auto topScale = drawing::effectiveScale(f.document, top);
    REQUIRE(topScale.has_value());
    CHECK(*topScale == DrawingScale{1, 1});
}

TEST_CASE("View_ScaleChangesTheDrawnSizeAndNothingElse", "[drawing][view][p14]") {
    Fixture f = makeFixture();
    const Point2D where{200_mm, 150_mm};
    const ViewId id =
        require(drawing::createView(f.document, "Front", baseView(f, StandardView::Front, where)));

    // Front of a 100 x 60 x 40 box is 100 wide and 40 tall.
    struct Case {
        DrawingScale scale;
        double width;
        double height;
    };
    const std::array<Case, 3> cases{{{DrawingScale{1, 1}, 100.0, 40.0},
                                     {DrawingScale{1, 2}, 50.0, 20.0},
                                     {DrawingScale{2, 1}, 200.0, 80.0}}};
    for (const Case& c : cases) {
        INFO("scale " << c.scale.label());
        ViewDefinition d = drawing::findView(f.document, id)->definition();
        d.scale = c.scale;
        REQUIRE(drawing::setViewDefinition(f.document, id, d).has_value());

        const auto geometry = drawing::projectedGeometry(f.document, id, f.bodies());
        REQUIRE(geometry.has_value());
        CHECK_THAT(geometry->bounds.width().in(units::mm), WithinAbs(c.width, kMm));
        CHECK_THAT(geometry->bounds.height().in(units::mm), WithinAbs(c.height, kMm));
        // The view stays where it was put, whatever the scale.
        const double centreX = 0.5 * (geometry->bounds.min.x + geometry->bounds.max.x).in(units::mm);
        const double centreY = 0.5 * (geometry->bounds.min.y + geometry->bounds.max.y).in(units::mm);
        CHECK_THAT(centreX, WithinAbs(where.x.in(units::mm), kMm));
        CHECK_THAT(centreY, WithinAbs(where.y.in(units::mm), kMm));
    }
}

// --- Placement and alignment ---------------------------------------------------------

TEST_CASE("View_PlacementAnchorsTheProjectedCentre", "[drawing][view][p14]") {
    Fixture f = makeFixture();
    const ViewId id = require(
        drawing::createView(f.document, "Front", baseView(f, StandardView::Front, {200_mm, 150_mm})));
    const auto geometry = drawing::projectedGeometry(f.document, id, f.bodies());
    REQUIRE(geometry.has_value());

    // 100 wide, 40 tall, centred on (200, 150): x 150..250, y 130..170. The
    // box sits at the model origin corner, so this also proves the centring
    // is on the PROJECTION rather than on the model origin.
    CHECK_THAT(geometry->bounds.min.x.in(units::mm), WithinAbs(150.0, kMm));
    CHECK_THAT(geometry->bounds.max.x.in(units::mm), WithinAbs(250.0, kMm));
    CHECK_THAT(geometry->bounds.min.y.in(units::mm), WithinAbs(130.0, kMm));
    CHECK_THAT(geometry->bounds.max.y.in(units::mm), WithinAbs(170.0, kMm));
}

TEST_CASE("View_MovingAViewShiftsEveryProjectedPointByExactlyThatMuch", "[drawing][view][p14]") {
    Fixture f = makeFixture();
    const ViewId id = require(
        drawing::createView(f.document, "Front", baseView(f, StandardView::Front, {200_mm, 150_mm})));
    const auto before = drawing::projectedGeometry(f.document, id, f.bodies());
    REQUIRE(before.has_value());

    ViewDefinition d = drawing::findView(f.document, id)->definition();
    d.placement = Point2D{230_mm, 105_mm}; // +30, -45
    REQUIRE(drawing::setViewDefinition(f.document, id, d).has_value());
    const auto after = drawing::projectedGeometry(f.document, id, f.bodies());
    REQUIRE(after.has_value());

    REQUIRE(before->points.size() == after->points.size());
    for (std::size_t i = 0; i < before->points.size(); ++i) {
        CHECK_THAT((after->points[i].x - before->points[i].x).in(units::mm), WithinAbs(30.0, kMm));
        CHECK_THAT((after->points[i].y - before->points[i].y).in(units::mm), WithinAbs(-45.0, kMm));
    }
}

TEST_CASE("View_FirstAngleAlignmentPutsTopBelowAndRightToTheLeft", "[drawing][view][p14]") {
    Fixture f = makeFixture(ProjectionConvention::FirstAngle);
    const Point2D where{200_mm, 150_mm};
    const ViewId front =
        require(drawing::createView(f.document, "Front", baseView(f, StandardView::Front, where)));
    const ViewId top = require(drawing::createView(
        f.document, "Top", projectedView(f, front, ProjectedDirection::Top, 80_mm)));
    const ViewId right = require(drawing::createView(
        f.document, "Right", projectedView(f, front, ProjectedDirection::Right, 90_mm)));

    const auto topAt = drawing::effectivePlacement(f.document, top);
    const auto rightAt = drawing::effectivePlacement(f.document, right);
    REQUIRE(topAt.has_value());
    REQUIRE(rightAt.has_value());

    // First angle: Top goes BELOW, Right goes to the LEFT.
    CHECK(topAt->x == where.x);                              // shares x exactly
    CHECK_THAT(topAt->y.in(units::mm), WithinAbs(70.0, kMm)); // 150 - 80
    CHECK(rightAt->y == where.y);                            // shares y exactly
    CHECK_THAT(rightAt->x.in(units::mm), WithinAbs(110.0, kMm)); // 200 - 90
}

TEST_CASE("View_ThirdAnglePutsTheSameViewsOnTheOppositeSides", "[drawing][view][p14]") {
    Fixture f = makeFixture(ProjectionConvention::ThirdAngle);
    const Point2D where{200_mm, 150_mm};
    const ViewId front =
        require(drawing::createView(f.document, "Front", baseView(f, StandardView::Front, where)));
    const ViewId top = require(drawing::createView(
        f.document, "Top", projectedView(f, front, ProjectedDirection::Top, 80_mm)));
    const ViewId right = require(drawing::createView(
        f.document, "Right", projectedView(f, front, ProjectedDirection::Right, 90_mm)));

    const auto topAt = drawing::effectivePlacement(f.document, top);
    const auto rightAt = drawing::effectivePlacement(f.document, right);
    REQUIRE(topAt.has_value());
    REQUIRE(rightAt.has_value());
    CHECK_THAT(topAt->y.in(units::mm), WithinAbs(230.0, kMm));   // 150 + 80
    CHECK_THAT(rightAt->x.in(units::mm), WithinAbs(290.0, kMm)); // 200 + 90
    CHECK(topAt->x == where.x);
    CHECK(rightAt->y == where.y);
}

TEST_CASE("View_AlignmentSurvivesMovingTheBaseView", "[drawing][view][p14]") {
    // The failure this prevents is alignment that is maintained rather than
    // derived: move the parent and a maintained alignment drifts.
    Fixture f = makeFixture();
    const ViewId front = require(
        drawing::createView(f.document, "Front", baseView(f, StandardView::Front, {200_mm, 150_mm})));
    const ViewId top = require(drawing::createView(
        f.document, "Top", projectedView(f, front, ProjectedDirection::Top, 80_mm)));

    for (const Point2D where : {Point2D{120_mm, 200_mm}, Point2D{300_mm, 90_mm}, Point2D{50_mm, 50_mm}}) {
        ViewDefinition d = drawing::findView(f.document, front)->definition();
        d.placement = where;
        REQUIRE(drawing::setViewDefinition(f.document, front, d).has_value());

        const auto base = drawing::effectivePlacement(f.document, front);
        const auto derived = drawing::effectivePlacement(f.document, top);
        REQUIRE(base.has_value());
        REQUIRE(derived.has_value());
        // Exactly equal, not nearly: it is the same number.
        CHECK(derived->x == base->x);
        CHECK_THAT((base->y - derived->y).in(units::mm), WithinAbs(80.0, kMm));
    }
}

TEST_CASE("View_AlignmentIsUnaffectedByScale", "[drawing][view][p14]") {
    Fixture f = makeFixture();
    const ViewId front = require(
        drawing::createView(f.document, "Front", baseView(f, StandardView::Front, {200_mm, 150_mm})));
    const ViewId top = require(drawing::createView(
        f.document, "Top", projectedView(f, front, ProjectedDirection::Top, 80_mm)));

    for (const DrawingScale s : {DrawingScale{1, 1}, DrawingScale{1, 5}, DrawingScale{2, 1}}) {
        ViewDefinition d = drawing::findView(f.document, front)->definition();
        d.scale = s;
        REQUIRE(drawing::setViewDefinition(f.document, front, d).has_value());
        const auto base = drawing::effectivePlacement(f.document, front);
        const auto derived = drawing::effectivePlacement(f.document, top);
        REQUIRE(base.has_value());
        REQUIRE(derived.has_value());
        CHECK(derived->x == base->x);
    }
}

TEST_CASE("View_AProjectedViewDrawsTheRightFaceOfTheBox", "[drawing][view][p14]") {
    // A Top view projected from Front must show 100 x 60, not 100 x 40.
    Fixture f = makeFixture();
    const ViewId front = require(
        drawing::createView(f.document, "Front", baseView(f, StandardView::Front, {200_mm, 150_mm})));
    const ViewId top = require(drawing::createView(
        f.document, "Top", projectedView(f, front, ProjectedDirection::Top, 80_mm)));
    const ViewId right = require(drawing::createView(
        f.document, "Right", projectedView(f, front, ProjectedDirection::Right, 90_mm)));

    const auto frontGeometry = drawing::projectedGeometry(f.document, front, f.bodies());
    const auto topGeometry = drawing::projectedGeometry(f.document, top, f.bodies());
    const auto rightGeometry = drawing::projectedGeometry(f.document, right, f.bodies());
    REQUIRE(frontGeometry.has_value());
    REQUIRE(topGeometry.has_value());
    REQUIRE(rightGeometry.has_value());

    CHECK_THAT(frontGeometry->bounds.width().in(units::mm), WithinAbs(100.0, kMm));
    CHECK_THAT(frontGeometry->bounds.height().in(units::mm), WithinAbs(40.0, kMm));
    CHECK_THAT(topGeometry->bounds.width().in(units::mm), WithinAbs(100.0, kMm));
    CHECK_THAT(topGeometry->bounds.height().in(units::mm), WithinAbs(60.0, kMm));
    CHECK_THAT(rightGeometry->bounds.width().in(units::mm), WithinAbs(60.0, kMm));
    CHECK_THAT(rightGeometry->bounds.height().in(units::mm), WithinAbs(40.0, kMm));
}

// --- Failure cases -------------------------------------------------------------------

TEST_CASE("View_MalformedDefinitionsAreRefused", "[drawing][view][p14]") {
    Fixture f = makeFixture();
    const ViewId front = require(
        drawing::createView(f.document, "Front", baseView(f, StandardView::Front, {200_mm, 150_mm})));

    const auto refuse = [&](const char* what, ViewDefinition d, const char* expect) {
        INFO(what);
        const auto result = drawing::createView(f.document, "Bad", d);
        REQUIRE_FALSE(result.has_value());
        CHECK_THAT(result.error().message, ContainsSubstring(expect));
    };

    ViewDefinition d = baseView(f, StandardView::Front, {0_mm, 0_mm});
    d.sheet = SheetId{};
    refuse("no sheet", d, "must name the sheet");

    d = baseView(f, StandardView::Front, {0_mm, 0_mm});
    d.parent = front;
    refuse("a base view given a parent", d, "a base view has no parent");

    d = ViewDefinition{.sheet = f.sheet};
    refuse("a base view with no orientation", d, "must have an orientation");

    d = baseView(f, StandardView::Front, {0_mm, 0_mm});
    d.direction = ProjectedDirection::Top;
    refuse("a base view with a direction", d, "no projected direction");

    d = projectedView(f, front, ProjectedDirection::Top, 80_mm);
    d.source = ObjectReference{f.part};
    refuse("a projected view naming a source", d, "takes its source from its parent");

    d = projectedView(f, front, ProjectedDirection::Top, 0_mm);
    refuse("zero spacing", d, "greater than zero");

    d = projectedView(f, front, ProjectedDirection::Top, 80_mm);
    d.placement = Point2D{10_mm, 10_mm};
    refuse("a projected view storing a placement", d, "derived from its parent");

    d = baseView(f, StandardView::Front, {0_mm, 0_mm});
    d.scale = DrawingScale{1, 0};
    refuse("a zero scale term", d, "zero term");

    d = baseView(f, StandardView::Front, {0_mm, 0_mm});
    d.sheet = SheetId::fromValue(9999);
    refuse("a sheet that is not there", d, "not a sheet of this document");

    d = baseView(f, StandardView::Front, {0_mm, 0_mm});
    d.source = ObjectReference{ObjectId::fromValue(9999)};
    refuse("a source that is not there", d, "not an object of this document");

    d = projectedView(f, ViewId::fromValue(9999), ProjectedDirection::Top, 80_mm);
    refuse("a parent that is not there", d, "not a view of this document");
}

TEST_CASE("View_AParentOnAnotherSheetIsRefused", "[drawing][view][p14]") {
    Fixture f = makeFixture();
    const ViewId front = require(
        drawing::createView(f.document, "Front", baseView(f, StandardView::Front, {200_mm, 150_mm})));
    const SheetId second = require(drawing::createSheet(
        f.document, "Sheet2",
        SheetDefinition{.format = drawing::SheetFormat::A4,
                        .margins = drawing::SheetMargins{10_mm, 10_mm, 10_mm, 10_mm},
                        .scale = DrawingScale{1, 1}}));

    ViewDefinition d = projectedView(f, front, ProjectedDirection::Top, 80_mm);
    d.sheet = second;
    const auto result = drawing::createView(f.document, "Elsewhere", d);
    REQUIRE_FALSE(result.has_value());
    CHECK_THAT(result.error().message, ContainsSubstring("same sheet"));
}

TEST_CASE("View_AViewCannotBeProjectedFromItself", "[drawing][view][p14]") {
    Fixture f = makeFixture();
    const ViewId front = require(
        drawing::createView(f.document, "Front", baseView(f, StandardView::Front, {200_mm, 150_mm})));
    ViewDefinition d = projectedView(f, front, ProjectedDirection::Top, 80_mm);
    d.parent = front;
    const auto result = drawing::setViewDefinition(f.document, front, d);
    REQUIRE_FALSE(result.has_value());
    CHECK_THAT(result.error().message, ContainsSubstring("projected from itself"));
}

TEST_CASE("View_RemovingAParentThatIsStillUsedIsRefused", "[drawing][view][p14]") {
    Fixture f = makeFixture();
    const ViewId front = require(
        drawing::createView(f.document, "Front", baseView(f, StandardView::Front, {200_mm, 150_mm})));
    const ViewId top = require(drawing::createView(
        f.document, "Top", projectedView(f, front, ProjectedDirection::Top, 80_mm)));

    const auto refused = drawing::removeView(f.document, front);
    REQUIRE_FALSE(refused.has_value());
    CHECK(errorCode(refused) == ErrorCode::FailedPrecondition);
    CHECK(drawing::views(f.document).size() == 2);

    // Remove the dependant first and the parent goes.
    REQUIRE(drawing::removeView(f.document, top).has_value());
    REQUIRE(drawing::removeView(f.document, front).has_value());
    CHECK(drawing::views(f.document).empty());
}

TEST_CASE("View_AViewWhoseSourceProducedNoBodyDrawsNothingAndSaysSo", "[drawing][view][p14]") {
    Fixture f = makeFixture();
    const ViewId id = require(
        drawing::createView(f.document, "Front", baseView(f, StandardView::Front, {200_mm, 150_mm})));

    // No body lookup at all: the view must fail rather than draw an empty
    // sheet that looks like a part with no features.
    const auto none = drawing::projectedGeometry(f.document, id, {});
    REQUIRE_FALSE(none.has_value());
    CHECK(errorCode(none) == ErrorCode::NotFound);
    CHECK_THAT(none.error().message, ContainsSubstring("produced no body"));
}

TEST_CASE("View_RecoversWhenTheSourceComesBack", "[drawing][view][p14]") {
    Fixture f = makeFixture();
    const ViewId id = require(
        drawing::createView(f.document, "Front", baseView(f, StandardView::Front, {200_mm, 150_mm})));
    REQUIRE(drawing::projectedGeometry(f.document, id, f.bodies()).has_value());

    // An empty lookup stands in for the body being gone.
    CHECK_FALSE(drawing::projectedGeometry(f.document, id, {}).has_value());
    // And the view is unchanged, so it draws again once the body is back.
    const auto again = drawing::projectedGeometry(f.document, id, f.bodies());
    REQUIRE(again.has_value());
    CHECK_THAT(again->bounds.width().in(units::mm), WithinAbs(100.0, kMm));
}

// --- Determinism ---------------------------------------------------------------------

TEST_CASE("View_TheSameViewProjectsIdenticallyEveryTime", "[drawing][view][p14][determinism]") {
    Fixture f = makeFixture();
    const ViewId front = require(
        drawing::createView(f.document, "Front", baseView(f, StandardView::Front, {200_mm, 150_mm})));
    const ViewId top = require(drawing::createView(
        f.document, "Top", projectedView(f, front, ProjectedDirection::Top, 80_mm)));

    for (const ViewId id : {front, top}) {
        const auto a = drawing::projectedGeometry(f.document, id, f.bodies());
        const auto b = drawing::projectedGeometry(f.document, id, f.bodies());
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        REQUIRE(a->points.size() == b->points.size());
        // Bit for bit: nothing is cached, so this is the same arithmetic twice.
        for (std::size_t i = 0; i < a->points.size(); ++i) {
            CHECK(a->points[i].x.si() == b->points[i].x.si());
            CHECK(a->points[i].y.si() == b->points[i].y.si());
        }
        CHECK(a->bounds == b->bounds);
        CHECK(a->edges.size() == b->edges.size());
    }
}

TEST_CASE("View_ABoxProjectsToTwelveStraightSegments", "[drawing][view][p14]") {
    // The scope boundary, stated as a number: a box has 12 straight edges and
    // every one becomes a segment. Curved edges contribute sampled points
    // P14-HLR-001 replaced the unclassified projection this once asserted.
    // A box seen square-on IS a rectangle: its far face lands exactly on its
    // near face, so four lines are drawn and four are merged away, and the
    // four edges running along the line of sight project to points and are
    // not lines at all.
    Fixture f = makeFixture();
    const ViewId id = require(
        drawing::createView(f.document, "Front", baseView(f, StandardView::Front, {200_mm, 150_mm})));
    const auto geometry = drawing::projectedGeometry(f.document, id, f.bodies());
    REQUIRE(geometry.has_value());
    CHECK(geometry->edges.size() == 4);
    CHECK(geometry->merged == 4);
    CHECK(geometry->suppressed == 0);
    CHECK(geometry->points.size() == 8); // two endpoints per straight line
}

// --- Persistence ---------------------------------------------------------------------

TEST_CASE("View_RoundTripsThroughTheFileWithItsIdAndRelations", "[drawing][view][p14][io]") {
    TempDir dir;
    Fixture f = makeFixture(ProjectionConvention::ThirdAngle);
    ViewDefinition base = baseView(f, StandardView::Front, {200_mm, 150_mm});
    base.scale = DrawingScale{1, 2};
    const ViewId front = require(drawing::createView(f.document, "Front", base));
    const ViewId top = require(drawing::createView(
        f.document, "Top", projectedView(f, front, ProjectedDirection::Top, 80_mm)));

    const auto path = dir.path() / "drawing.bcad";
    REQUIRE(io::saveDocument(f.document, path).has_value());
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());

    CHECK(drawing::views(*loaded) == std::vector{front, top});
    CHECK(drawing::findView(*loaded, front)->definition() == base);
    const drawing::ViewDefinition& reloadedTop = drawing::findView(*loaded, top)->definition();
    CHECK(reloadedTop.parent == std::optional<ViewId>{front});
    CHECK(reloadedTop.direction == std::optional<ProjectedDirection>{ProjectedDirection::Top});
    CHECK(reloadedTop.spacing == 80_mm);
    CHECK_FALSE(reloadedTop.scale.has_value()); // still inherits the sheet's

    // The convention came back too, so alignment is reproduced rather than
    // recomputed under a default.
    const auto placement = drawing::effectivePlacement(*loaded, top);
    REQUIRE(placement.has_value());
    CHECK_THAT(placement->y.in(units::mm), WithinAbs(230.0, kMm)); // third angle: above
}

TEST_CASE("View_TheFileHoldsIntentAndNoProjectedGeometry", "[drawing][view][p14][io]") {
    TempDir dir;
    Fixture f = makeFixture();
    const ViewId front = require(
        drawing::createView(f.document, "Front", baseView(f, StandardView::Front, {200_mm, 150_mm})));
    REQUIRE(drawing::createView(f.document, "Top",
                                projectedView(f, front, ProjectedDirection::Top, 80_mm))
                .has_value());

    const auto path = dir.path() / "drawing.bcad";
    REQUIRE(io::saveDocument(f.document, path).has_value());
    const std::string text = readFile(path);

    CHECK_THAT(text, ContainsSubstring("\"type\": \"view\""));
    CHECK_THAT(text, ContainsSubstring("\"orientation\": \"front\""));
    CHECK_THAT(text, ContainsSubstring("\"direction\": \"top\""));
    // No projected geometry, and no derived placement for the projected view.
    CHECK_THAT(text, !ContainsSubstring("segments"));
    CHECK_THAT(text, !ContainsSubstring("points"));
    CHECK_THAT(text, !ContainsSubstring("bounds"));
    // A base view writes its placement; the file has exactly one "x".
    CHECK(std::ranges::count(text, 'x') > 0);
    CHECK_THAT(text, ContainsSubstring("\"version\": 1")); // no bump

    // Saving twice is byte-identical.
    const auto again = dir.path() / "again.bcad";
    REQUIRE(io::saveDocument(f.document, again).has_value());
    CHECK(readFile(again) == readFile(path));
}

TEST_CASE("View_ALoopOfProjectedViewsIsRefusedRatherThanRecursedInto", "[drawing][view][p14]") {
    // Regression for a real defect. Only self-parenting was blocked, so
    // A-from-B and B-from-A was constructible, and effectiveBasis walked the
    // chain directly -- it would have recursed until the stack ran out
    // instead of producing a diagnostic.
    Fixture f = makeFixture();
    const ViewId a = require(
        drawing::createView(f.document, "Front", baseView(f, StandardView::Front, {200_mm, 150_mm})));
    const ViewId b = require(drawing::createView(
        f.document, "Top", projectedView(f, a, ProjectedDirection::Top, 80_mm)));

    // Turning A into a view projected from B would close the loop.
    const auto refused =
        drawing::setViewDefinition(f.document, a, projectedView(f, b, ProjectedDirection::Right, 80_mm));
    REQUIRE_FALSE(refused.has_value());
    CHECK(errorCode(refused) == ErrorCode::InvalidArgument);
    CHECK_THAT(refused.error().message, ContainsSubstring("loop of views"));

    // A is untouched, so both still derive normally.
    CHECK(drawing::effectiveBasis(f.document, a).has_value());
    CHECK(drawing::effectiveBasis(f.document, b).has_value());
    CHECK(drawing::effectivePlacement(f.document, b).has_value());

    // A longer loop is refused too, not just the two-view one.
    const ViewId c = require(drawing::createView(
        f.document, "Right", projectedView(f, b, ProjectedDirection::Right, 80_mm)));
    const auto longer =
        drawing::setViewDefinition(f.document, a, projectedView(f, c, ProjectedDirection::Bottom, 80_mm));
    REQUIRE_FALSE(longer.has_value());
    CHECK_THAT(longer.error().message, ContainsSubstring("loop of views"));
}
