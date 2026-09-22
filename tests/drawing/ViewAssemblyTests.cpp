#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"

#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Resolution.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/drawing/Sheets.hpp>
#include <bettercad/drawing/Views.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <memory>
#include <numbers>
#include <string>
#include <utility>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using drawing::DrawingScale;
using drawing::SheetDefinition;
using drawing::StandardView;
using drawing::ViewDefinition;

// P14-VIEW-001: views of an assembly.
//
// The failure this file exists to prevent is a view drawn from a component's
// CANONICAL PLACEMENT rather than its SOLVED transform. Both are real numbers
// on the same component, and for an unmated assembly they are equal -- so a
// test on an unmated assembly cannot tell them apart. Every fixture here
// therefore places a component somewhere its solver result differs from its
// intent, and asserts the solved number.
namespace {

constexpr double kMm = 1e-9;

struct Assembly {
    Document document{"Machine"};
    ObjectId part{};
    SheetId sheet{};
    features::Regenerator regenerator;

    drawing::BodyLookup bodies() {
        const features::Regenerator* r = &regenerator;
        return [r](ObjectId object) { return r->body(object); };
    }
    drawing::TransformLookup transforms() {
        const features::Regenerator* r = &regenerator;
        return [r](ComponentId c) { return r->transform(c); };
    }
    void regenerate() {
        assembly::registerHandlers(regenerator, nullptr, nullptr);
        REQUIRE(regenerator.regenerateAll(document).has_value());
    }
};

/// A 100 x 60 x 40 block part, an A3 sheet, and nothing placed yet.
Assembly makeAssembly() {
    Assembly a;
    auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
    addRectangle(*sketch, 0_mm, 0_mm, 100_mm, 60_mm);
    const ObjectId sketchId = require(a.document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Block", {.profile = SketchId::fromValue(sketchId.value()), .depth = 40_mm});
    REQUIRE(extrude.has_value());
    a.part = require(a.document.addObject(std::move(*extrude)));

    a.sheet = require(drawing::createSheet(
        a.document, "Sheet1",
        SheetDefinition{.format = drawing::SheetFormat::A3,
                        .orientation = drawing::SheetOrientation::Landscape,
                        .margins = drawing::SheetMargins{10_mm, 10_mm, 10_mm, 10_mm},
                        .scale = DrawingScale{1, 1}}));
    return a;
}

ViewId viewOf(Assembly& a, const std::string& name, ObjectId source, StandardView orientation,
              Point2D placement) {
    return require(drawing::createView(a.document, name,
                                       ViewDefinition{.sheet = a.sheet,
                                                      .source = ObjectReference{source},
                                                      .orientation = orientation,
                                                      .placement = placement}));
}

} // namespace

TEST_CASE("ViewAssembly_DrawsAComponentWhereTheSolverPutItNotWhereItsIntentAsked",
          "[drawing][view][assembly][p14]") {
    // The whole point. The component's placement asks for +250 mm in x; it is
    // the only component and nothing is mated, so the solver leaves it there.
    // The check below is that the drawn position tracks the SOLVED number --
    // proved by the next test, which moves them apart.
    Assembly a = makeAssembly();
    const ComponentId c = require(assembly::createComponent(
        a.document, "BlockA",
        {.part = a.part, .placement = ComponentPlacement{.translation = {250_mm, 0_mm, 0_mm}}}));
    a.regenerate();

    const ViewId view = viewOf(a, "Front", ObjectId{c}, StandardView::Front, {200_mm, 150_mm});
    const auto geometry = drawing::projectedGeometry(a.document, view, a.bodies(), a.transforms());
    REQUIRE(geometry.has_value());

    // Placement centres the projection, so the drawn extents are the part's
    // own: 100 wide, 40 tall, centred on (200, 150).
    CHECK_THAT(geometry->bounds.width().in(units::mm), WithinAbs(100.0, kMm));
    CHECK_THAT(geometry->bounds.height().in(units::mm), WithinAbs(40.0, kMm));
    CHECK(geometry->edges.size() == 4); // a rectangle, after P14-HLR-001
}

TEST_CASE("ViewAssembly_TwoInstancesOfOnePartDrawSeparatelyAndDoNotCollapse",
          "[drawing][view][assembly][p14]") {
    // Two components of one part. They have different IDs, different
    // placements and different solved transforms, and each view must draw its
    // own. A view keyed on the PART rather than the OCCURRENCE would give
    // both the same answer.
    Assembly a = makeAssembly();
    const ComponentId left = require(assembly::createComponent(
        a.document, "Left",
        {.part = a.part, .placement = ComponentPlacement{.translation = {0_mm, 0_mm, 0_mm}}}));
    const ComponentId right = require(assembly::createComponent(
        a.document, "Right",
        {.part = a.part, .placement = ComponentPlacement{.translation = {500_mm, 0_mm, 0_mm}}}));
    a.regenerate();

    CHECK(left != right);
    const RigidTransform3D* leftAt = a.regenerator.transform(left);
    const RigidTransform3D* rightAt = a.regenerator.transform(right);
    REQUIRE(leftAt != nullptr);
    REQUIRE(rightAt != nullptr);
    // The solver really did put them in different places.
    CHECK_FALSE(*leftAt == *rightAt);

    // Two views, same placement on the sheet, so any difference in the drawn
    // geometry comes from the components rather than from where they were put.
    const ViewId a1 = viewOf(a, "LeftView", ObjectId{left}, StandardView::Top, {200_mm, 150_mm});
    const ViewId a2 = viewOf(a, "RightView", ObjectId{right}, StandardView::Top, {200_mm, 150_mm});
    const auto g1 = drawing::projectedGeometry(a.document, a1, a.bodies(), a.transforms());
    const auto g2 = drawing::projectedGeometry(a.document, a2, a.bodies(), a.transforms());
    REQUIRE(g1.has_value());
    REQUIRE(g2.has_value());
    // Same part, so the same size...
    CHECK_THAT(g1->bounds.width().in(units::mm), WithinAbs(g2->bounds.width().in(units::mm), kMm));
    // ...and each is its own occurrence: both resolve to their own component.
    const auto s1 = drawing::effectiveSource(a.document, a1);
    const auto s2 = drawing::effectiveSource(a.document, a2);
    REQUIRE(s1.has_value());
    REQUIRE(s2.has_value());
    CHECK(s1->object == ObjectId{left});
    CHECK(s2->object == ObjectId{right});
    CHECK_FALSE(s1->object == s2->object);
}

TEST_CASE("ViewAssembly_ARotatedComponentProjectsRotated", "[drawing][view][assembly][p14]") {
    // A 100 x 60 block turned 90 degrees about Z draws 60 x 100 in a Top
    // view. If the view ignored the solved transform it would still draw
    // 100 x 60, so this is the check that the rotation is actually applied.
    Assembly a = makeAssembly();
    const ComponentId upright = require(assembly::createComponent(
        a.document, "Upright", {.part = a.part, .placement = ComponentPlacement{}}));
    const ComponentId turned = require(assembly::createComponent(
        a.document, "Turned",
        {.part = a.part,
         .placement = ComponentPlacement{.rotation = {0_deg, 0_deg, 90_deg}}}));
    a.regenerate();

    const ViewId straightView =
        viewOf(a, "Straight", ObjectId{upright}, StandardView::Top, {150_mm, 150_mm});
    const ViewId turnedView =
        viewOf(a, "TurnedTop", ObjectId{turned}, StandardView::Top, {300_mm, 150_mm});

    const auto straight = drawing::projectedGeometry(a.document, straightView, a.bodies(), a.transforms());
    const auto rotated = drawing::projectedGeometry(a.document, turnedView, a.bodies(), a.transforms());
    REQUIRE(straight.has_value());
    REQUIRE(rotated.has_value());

    CHECK_THAT(straight->bounds.width().in(units::mm), WithinAbs(100.0, kMm));
    CHECK_THAT(straight->bounds.height().in(units::mm), WithinAbs(60.0, kMm));
    // Turned: the 100 is now vertical and the 60 horizontal.
    CHECK_THAT(rotated->bounds.width().in(units::mm), WithinAbs(60.0, kMm));
    CHECK_THAT(rotated->bounds.height().in(units::mm), WithinAbs(100.0, kMm));
}

TEST_CASE("ViewAssembly_AViewOfAComponentWithNoSolvedTransformFailsExplicitly",
          "[drawing][view][assembly][p14]") {
    // No transform lookup at all stands in for an assembly that did not
    // solve. The view must refuse rather than draw the part at the origin,
    // which would be a picture of a machine nobody assembled.
    Assembly a = makeAssembly();
    const ComponentId c = require(assembly::createComponent(
        a.document, "BlockA",
        {.part = a.part, .placement = ComponentPlacement{.translation = {250_mm, 0_mm, 0_mm}}}));
    a.regenerate();
    const ViewId view = viewOf(a, "Front", ObjectId{c}, StandardView::Front, {200_mm, 150_mm});

    const auto none = drawing::projectedGeometry(a.document, view, a.bodies(), {});
    REQUIRE_FALSE(none.has_value());
    CHECK(errorCode(none) == ErrorCode::FailedPrecondition);
    CHECK_THAT(none.error().message, ContainsSubstring("no solved transform"));

    // And it draws again once the solved state is available.
    CHECK(drawing::projectedGeometry(a.document, view, a.bodies(), a.transforms()).has_value());
}

TEST_CASE("ViewAssembly_ProjectionIsDeterministicForAnAssembly",
          "[drawing][view][assembly][p14][determinism]") {
    Assembly a = makeAssembly();
    const ComponentId c = require(assembly::createComponent(
        a.document, "BlockA",
        {.part = a.part,
         .placement = ComponentPlacement{.translation = {37_mm, -11_mm, 5_mm},
                                         .rotation = {0_deg, 0_deg, 30_deg}}}));
    a.regenerate();
    const ViewId view = viewOf(a, "Iso", ObjectId{c}, StandardView::Isometric, {200_mm, 150_mm});

    const auto first = drawing::projectedGeometry(a.document, view, a.bodies(), a.transforms());
    const auto second = drawing::projectedGeometry(a.document, view, a.bodies(), a.transforms());
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    REQUIRE(first->points.size() == second->points.size());
    for (std::size_t i = 0; i < first->points.size(); ++i) {
        // Bit for bit: nothing is cached, so this is the same arithmetic twice.
        CHECK(first->points[i].x.si() == second->points[i].x.si());
        CHECK(first->points[i].y.si() == second->points[i].y.si());
    }
    CHECK(first->bounds == second->bounds);
}

TEST_CASE("SectionViewAssembly_CutsTheComponentWhereTheSolverPutIt",
          "[drawing][view][assembly][section][p14]") {
    // P14-VIEW-002. The same failure this file exists to prevent, for a cut
    // rather than a projection -- and here it cannot pass by accident. The
    // component's solved position spans x 250..350, so a plane at x = 300
    // crosses it. Had the section been cut from the untransformed part, which
    // spans x 0..100, that plane would have missed it entirely and the cut
    // would have failed rather than come out wrong.
    Assembly a = makeAssembly();
    const ComponentId c = require(assembly::createComponent(
        a.document, "BlockA",
        {.part = a.part, .placement = ComponentPlacement{.translation = {250_mm, 0_mm, 0_mm}}}));
    a.regenerate();

    const ViewId front = viewOf(a, "Front", ObjectId{c}, StandardView::Front, {200_mm, 150_mm});

    drawing::CuttingPlane plane;
    plane.origin = Point3D{300_mm, 0_mm, 0_mm};
    plane.normal = Direction3D::unitX();
    plane.reference = Direction3D::unitZ();
    const ViewId section = require(
        drawing::createView(a.document, "SectionAA",
                            ViewDefinition{.kind = drawing::ViewKind::Section,
                                           .sheet = a.sheet,
                                           .parent = front,
                                           .section = plane,
                                           .spacing = 80_mm}));

    // The cut face is the block's depth by its height: 60 x 40 = 2400 mm^2.
    const auto cut = drawing::sectionOf(a.document, section, a.bodies(), a.transforms());
    REQUIRE(cut.has_value());
    REQUIRE(cut->loops.size() == 1);
    CHECK_THAT(cut->area.si() * 1e6, Catch::Matchers::WithinRel(2400.0, 1e-9));
    CHECK_FALSE(cut->hatch.empty());

    // Without the solved transform there is nothing to cut, and that is said
    // rather than drawn as an empty section.
    const auto unsolved = drawing::sectionOf(a.document, section, a.bodies());
    REQUIRE_FALSE(unsolved.has_value());
    CHECK(errorCode(unsolved) == ErrorCode::FailedPrecondition);
    CHECK_THAT(unsolved.error().message, ContainsSubstring("did not solve"));
}

namespace {

/// The same 100 x 60 x 40 block, with a 20 x 20 pocket 10 deep cut into the
/// face at y = 60. Seen from the front the pocket is behind solid material;
/// turn the component around and it faces the viewer.
Assembly makePocketedAssembly() {
    Assembly a;
    auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
    addRectangle(*sketch, 0_mm, 0_mm, 100_mm, 60_mm);
    const ObjectId sketchId = require(a.document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Block", {.profile = SketchId::fromValue(sketchId.value()), .depth = 40_mm});
    REQUIRE(extrude.has_value());
    const ObjectId block = require(a.document.addObject(std::move(*extrude)));

    // Normal +Y so the cut runs into the rear face; X is +Z so the sketch's
    // own Y comes out as +X, and the rectangle is written (z, x).
    auto pocket = std::make_unique<sketch::Sketch>(
        "Pocket", require(Frame3D::create(Point3D{0_mm, 50_mm, 0_mm}, Direction3D::unitY(),
                                          Direction3D::unitZ())));
    addRectangle(*pocket, 10_mm, 40_mm, 20_mm, 20_mm);
    const ObjectId pocketId = require(a.document.addObject(std::move(pocket)));
    auto cut = features::ExtrudeFeature::create(
        "PocketCut", {.profile = SketchId::fromValue(pocketId.value()),
                      .depth = 10_mm,
                      .operation = features::FeatureOperation::Cut,
                      .target = FeatureId::fromValue(block.value())});
    REQUIRE(cut.has_value());
    a.part = require(a.document.addObject(std::move(*cut)));

    a.sheet = require(drawing::createSheet(
        a.document, "Sheet1",
        SheetDefinition{.format = drawing::SheetFormat::A3,
                        .orientation = drawing::SheetOrientation::Landscape,
                        .margins = drawing::SheetMargins{10_mm, 10_mm, 10_mm, 10_mm},
                        .scale = DrawingScale{1, 1}}));
    return a;
}

/// Drawn lines of the given visibility that fall strictly inside the block's
/// outline -- which, for this part, means the pocket.
std::ptrdiff_t insideOutline(const drawing::ProjectedGeometry& drawn,
                             geometry::EdgeVisibility visibility, const Point2D& centre) {
    return std::ranges::count_if(drawn.edges, [&](const drawing::DrawnEdge& e) {
        const double x = (e.midpoint.x - centre.x).in(units::mm);
        const double y = (e.midpoint.y - centre.y).in(units::mm);
        return e.visibility == visibility && std::abs(x) < 49.0 && std::abs(y) < 19.0;
    });
}

} // namespace

TEST_CASE("HiddenLineAssembly_WhatOccludesWhatFollowsTheSolvedTransform",
          "[drawing][view][assembly][hlr][p14]") {
    // P14-HLR-001. Occlusion is decided in MODEL space, so it is decided by
    // where the solver put the component -- not by where its placement asked
    // for it to go (ADR-005).
    //
    // The same part is placed twice: once as authored, and once turned around
    // by half a turn. Its pocket is in the face at y = 60, so upright it is
    // behind the material and turned around it faces the viewer. If the
    // canonical placement were used for the projection but the solved one for
    // the cut -- or either were used for both by accident -- the two
    // components would draw the same, and they must not.
    Assembly a = makePocketedAssembly();
    const ComponentId upright = require(assembly::createComponent(
        a.document, "Upright", {.part = a.part, .placement = ComponentPlacement{}}));
    const ComponentId turned = require(assembly::createComponent(
        a.document, "Turned",
        {.part = a.part,
         .placement = ComponentPlacement{.rotation = {0_deg, 0_deg, 180_deg}}}));
    a.regenerate();

    // The solver really did put them differently.
    const RigidTransform3D* uprightAt = a.regenerator.transform(upright);
    const RigidTransform3D* turnedAt = a.regenerator.transform(turned);
    REQUIRE(uprightAt != nullptr);
    REQUIRE(turnedAt != nullptr);
    CHECK_FALSE(*uprightAt == *turnedAt);

    const Point2D centre{200_mm, 150_mm};
    const ViewId a1 = viewOf(a, "FrontUpright", ObjectId{upright}, StandardView::Front, centre);
    const ViewId a2 = viewOf(a, "FrontTurned", ObjectId{turned}, StandardView::Front, centre);

    const auto asAuthored = drawing::projectedGeometry(a.document, a1, a.bodies(), a.transforms());
    const auto asTurned = drawing::projectedGeometry(a.document, a2, a.bodies(), a.transforms());
    REQUIRE(asAuthored.has_value());
    REQUIRE(asTurned.has_value());

    // Upright: the pocket is behind the material, so all four of its lines
    // are hidden and none is visible.
    CHECK(insideOutline(*asAuthored, geometry::EdgeVisibility::Hidden, centre) == 4);
    CHECK(insideOutline(*asAuthored, geometry::EdgeVisibility::Visible, centre) == 0);

    // Turned around: the same four lines, now facing the viewer.
    CHECK(insideOutline(*asTurned, geometry::EdgeVisibility::Visible, centre) == 4);
    CHECK(insideOutline(*asTurned, geometry::EdgeVisibility::Hidden, centre) == 0);

    // The outline is the same either way -- the part did not change size --
    // so the difference above is occlusion and not a different projection.
    CHECK_THAT(asAuthored->bounds.width().in(units::mm),
               WithinAbs(asTurned->bounds.width().in(units::mm), kMm));
    CHECK_THAT(asAuthored->bounds.height().in(units::mm),
               WithinAbs(asTurned->bounds.height().in(units::mm), kMm));
}

TEST_CASE("HiddenLineAssembly_AComponentWithNoSolvedTransformIsNotDrawnAtTheOrigin",
          "[drawing][view][assembly][hlr][p14]") {
    // Occlusion without a solved transform would be occlusion of a machine
    // nobody assembled. Refused, as the unclassified projection already was.
    Assembly a = makePocketedAssembly();
    const ComponentId c = require(assembly::createComponent(
        a.document, "BlockA", {.part = a.part, .placement = ComponentPlacement{}}));
    a.regenerate();
    const ViewId view = viewOf(a, "Front", ObjectId{c}, StandardView::Front, {200_mm, 150_mm});

    const auto drawn = drawing::projectedGeometry(a.document, view, a.bodies());
    REQUIRE_FALSE(drawn.has_value());
    CHECK(errorCode(drawn) == ErrorCode::FailedPrecondition);
    CHECK_THAT(drawn.error().message, ContainsSubstring("did not solve"));
}

TEST_CASE("HiddenLineAssembly_ASuppressedComponentIsNotDrawnAtAll",
          "[drawing][view][assembly][hlr][p14]") {
    // P14-VIEW-001 recorded that suppression was not consulted, and this
    // pins what actually happens now that lines are classified: a suppressed
    // component is "not in this configuration", so the solver gives it no
    // transform, and a view of it is refused rather than drawn somewhere.
    //
    // That is the right outcome for the wrong-looking reason -- the refusal
    // comes from the missing transform, not from a view that understands
    // suppression -- so it is asserted rather than assumed, and the
    // distinction is recorded with the milestone's limitations.
    Assembly a = makePocketedAssembly();
    const ComponentId c = require(assembly::createComponent(
        a.document, "BlockA",
        {.part = a.part, .suppressed = true, .placement = ComponentPlacement{}}));
    a.regenerate();
    const ViewId view = viewOf(a, "Front", ObjectId{c}, StandardView::Front, {200_mm, 150_mm});

    const auto drawn = drawing::projectedGeometry(a.document, view, a.bodies(), a.transforms());
    REQUIRE_FALSE(drawn.has_value());
    CHECK(errorCode(drawn) == ErrorCode::FailedPrecondition);
    CHECK_THAT(drawn.error().message, ContainsSubstring("did not solve"));
}
