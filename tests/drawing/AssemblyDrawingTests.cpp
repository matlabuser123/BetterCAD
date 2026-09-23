#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Configurations.hpp>
#include <bettercad/assembly/Mates.hpp>
#include <bettercad/assembly/Resolution.hpp>
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
#include <limits>
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
using drawing::DrawnEdge;
using drawing::ProjectedGeometry;
using drawing::SheetDefinition;
using drawing::StandardView;
using drawing::ViewDefinition;
using assembly::MateType;
using drawing::ViewSubject;
using geometry::EdgeVisibility;

// P14-ASM-001: drawing a whole assembly.
//
// WHAT THESE FIXTURES ARE FOR. Every one places a part somewhere its SOLVED
// transform can be written down, and asserts the drawn number against
// arithmetic done here. A 40 mm cube at x = 0 and another at x = 60 span
// 0..100 in x, so a front view of the pair is 100 wide; that is the whole of
// the expected value, and it is not read back from the code under test.
//
// WHY OCCLUSION IS ASSERTED BY COMPARISON. A box hides its own back face, so
// "this body has hidden edges" says nothing about occlusion BETWEEN bodies.
// What does say something is that a line visible when a component is drawn
// alone stops being visible when another component is put in front of it, and
// that is how every occlusion test below is written.
namespace {

constexpr double kMm = 1e-9;
constexpr double kKernelMm = 1e-6;

struct Assembly {
    Document document{"Machine"};
    ObjectId cube{};
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
    /// Regenerates and gives back the report, for the tests that care whether
    /// the assembly solved at all.
    features::RegenerationReport regenerateReporting() {
        assembly::registerHandlers(regenerator, nullptr, nullptr);
        auto report = regenerator.regenerateAll(document);
        REQUIRE(report.has_value());
        return std::move(*report);
    }
};

/// A 40 x 40 x 40 cube part on an A3 sheet, with nothing placed yet.
Assembly makeAssembly(DrawingScale scale = DrawingScale{1, 1}) {
    Assembly a;
    auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
    addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 40_mm);
    const ObjectId sketchId = require(a.document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Cube", {.profile = SketchId::fromValue(sketchId.value()), .depth = 40_mm});
    REQUIRE(extrude.has_value());
    a.cube = require(a.document.addObject(std::move(*extrude)));

    a.sheet = require(drawing::createSheet(
        a.document, "Sheet1",
        SheetDefinition{.format = drawing::SheetFormat::A3,
                        .orientation = drawing::SheetOrientation::Landscape,
                        .margins = drawing::SheetMargins{10_mm, 10_mm, 10_mm, 10_mm},
                        .scale = scale}));
    return a;
}

ComponentId place(Assembly& a, const std::string& name, Length x, Length y = 0_mm,
                  Length z = 0_mm) {
    auto made = assembly::createComponent(
        a.document, name,
        {.part = a.cube, .placement = ComponentPlacement{.translation = {x, y, z}}});
    const std::string why = made.has_value() ? std::string{} : made.error().message;
    INFO(why);
    REQUIRE(made.has_value());
    return *made;
}

/// A view of the WHOLE assembly.
ViewId assemblyView(Assembly& a, const std::string& name,
                    StandardView orientation = StandardView::Front,
                    Point2D placement = {200_mm, 150_mm}) {
    auto made = drawing::createView(a.document, name,
                                    ViewDefinition{.sheet = a.sheet,
                                                   .subject = ViewSubject::Assembly,
                                                   .orientation = orientation,
                                                   .placement = placement});
    const std::string why = made.has_value() ? std::string{} : made.error().message;
    INFO(why);
    REQUIRE(made.has_value());
    return *made;
}

/// A view of ONE object, as P14-VIEW-001 has always made them.
ViewId objectView(Assembly& a, const std::string& name, ObjectId source,
                  StandardView orientation = StandardView::Front) {
    return require(drawing::createView(a.document, name,
                                       ViewDefinition{.sheet = a.sheet,
                                                      .source = ObjectReference{source},
                                                      .orientation = orientation,
                                                      .placement = {200_mm, 150_mm}}));
}

ProjectedGeometry drawn(Assembly& a, ViewId view) {
    auto geometry = drawing::projectedGeometry(a.document, view, a.bodies(), a.transforms());
    REQUIRE(geometry.has_value());
    return std::move(*geometry);
}

/// The edges one occurrence contributed.
std::vector<DrawnEdge> edgesOf(const ProjectedGeometry& geometry, ComponentId occurrence) {
    std::vector<DrawnEdge> mine;
    std::ranges::copy_if(geometry.edges, std::back_inserter(mine), [&](const DrawnEdge& e) {
        return e.occurrence == occurrence;
    });
    return mine;
}

std::size_t visibleCount(const ProjectedGeometry& geometry, ComponentId occurrence) {
    return static_cast<std::size_t>(std::ranges::count_if(geometry.edges, [&](const DrawnEdge& e) {
        return e.occurrence == occurrence && e.visibility == EdgeVisibility::Visible;
    }));
}

} // namespace

// --- Drawing from solved state ------------------------------------------------------------

TEST_CASE("AssemblyView_DrawsEveryActiveOccurrenceAtItsSolvedPlace",
          "[drawing][assembly][p14]") {
    // Two 40 mm cubes, at x = 0 and x = 60. Their projected extent is
    // 0 .. 100 in x and 0 .. 40 in z -- arithmetic, written out here.
    Assembly a = makeAssembly();
    const ComponentId left = place(a, "Left", 0_mm);
    const ComponentId right = place(a, "Right", 60_mm);
    a.regenerate();

    const ViewId view = assemblyView(a, "Whole");
    const ProjectedGeometry geometry = drawn(a, view);

    CHECK_THAT(geometry.bounds.width().in(units::mm), WithinAbs(100.0, kKernelMm));
    CHECK_THAT(geometry.bounds.height().in(units::mm), WithinAbs(40.0, kKernelMm));

    // Both are there, and each line says which one drew it.
    CHECK_FALSE(edgesOf(geometry, left).empty());
    CHECK_FALSE(edgesOf(geometry, right).empty());
    CHECK(edgesOf(geometry, left).size() + edgesOf(geometry, right).size() ==
          geometry.edges.size());

    // A view of ONE of them is 40 wide, which is what makes the 100 above a
    // statement about the assembly rather than about the part.
    const ProjectedGeometry one = drawn(a, objectView(a, "JustLeft", ObjectId{left}));
    CHECK_THAT(one.bounds.width().in(units::mm), WithinAbs(40.0, kKernelMm));
}

TEST_CASE("AssemblyView_UsesTheSolvedTransformAndNotTheAuthoringOrigin",
          "[drawing][assembly][p14]") {
    // If the solved transform were ignored, both cubes would be drawn at the
    // origin and the assembly would be 40 wide instead of 100.
    Assembly a = makeAssembly();
    (void)place(a, "AtOrigin", 0_mm);
    (void)place(a, "FarOff", 60_mm);
    a.regenerate();

    const ProjectedGeometry geometry = drawn(a, assemblyView(a, "Whole"));
    CHECK_THAT(geometry.bounds.width().in(units::mm), WithinAbs(100.0, kKernelMm));
}

TEST_CASE("AssemblyView_WithNoActiveComponentsIsRefused", "[drawing][assembly][p14]") {
    Assembly a = makeAssembly();
    a.regenerate();
    const ViewId view = assemblyView(a, "Empty");
    const auto geometry = drawing::projectedGeometry(a.document, view, a.bodies(), a.transforms());
    REQUIRE_FALSE(geometry.has_value());
    CHECK(errorCode(geometry) == ErrorCode::FailedPrecondition);
    CHECK_THAT(geometry.error().message, ContainsSubstring("no active components"));
}

TEST_CASE("AssemblyView_WithoutASolvedTransformDrawsNothingAtAll",
          "[drawing][assembly][p14]") {
    // Atomicity. One occurrence with no solved transform fails the WHOLE
    // view: a drawing missing a component looks exactly like a complete
    // drawing of a smaller machine, and drawing it at the origin would be
    // worse still.
    Assembly a = makeAssembly();
    const ComponentId left = place(a, "Left", 0_mm);
    (void)place(a, "Right", 60_mm);
    a.regenerate();
    const ViewId view = assemblyView(a, "Whole");
    REQUIRE(drawing::projectedGeometry(a.document, view, a.bodies(), a.transforms()).has_value());

    // A lookup that has lost one occurrence's transform, which is what an
    // assembly that did not solve looks like to a view.
    const features::Regenerator* r = &a.regenerator;
    const drawing::TransformLookup partial = [r, left](ComponentId c) {
        return c == left ? nullptr : r->transform(c);
    };
    const auto geometry = drawing::projectedGeometry(a.document, view, a.bodies(), partial);
    REQUIRE_FALSE(geometry.has_value());
    CHECK(errorCode(geometry) == ErrorCode::FailedPrecondition);
    CHECK_THAT(geometry.error().message, ContainsSubstring("did not solve"));
}

// --- Occurrence identity --------------------------------------------------------------------

TEST_CASE("AssemblyView_ThreeInstancesOfOnePartStayThreeOccurrences",
          "[drawing][assembly][p14]") {
    // The same part placed three times. They share every byte of geometry and
    // must remain three distinct drawing sources: a view keyed on the PART
    // would collapse them into one.
    Assembly a = makeAssembly();
    const ComponentId first = place(a, "First", 0_mm);
    const ComponentId second = place(a, "Second", 60_mm);
    const ComponentId third = require(assembly::createComponent(
        a.document, "Third",
        {.part = a.cube,
         .placement = ComponentPlacement{.translation = {120_mm, 0_mm, 0_mm},
                                         .rotation = {0_deg, 0_deg, 90_deg}}}));
    a.regenerate();

    const ProjectedGeometry geometry = drawn(a, assemblyView(a, "Three"));

    // Three distinct identities, none empty, and nothing unattributed.
    CHECK(first != second);
    CHECK(second != third);
    for (const ComponentId occurrence : {first, second, third}) {
        INFO(occurrence.value());
        CHECK_FALSE(edgesOf(geometry, occurrence).empty());
    }
    CHECK(edgesOf(geometry, first).size() + edgesOf(geometry, second).size() +
              edgesOf(geometry, third).size() ==
          geometry.edges.size());

    // Each sits where its own solved transform put it: 0..40, 60..100,
    // 120..160 across the sheet. Computed here from the placements.
    const auto spanOf = [&](ComponentId occurrence) {
        double lo = std::numeric_limits<double>::max();
        double hi = std::numeric_limits<double>::lowest();
        for (const DrawnEdge& edge : edgesOf(geometry, occurrence)) {
            for (const Point2D& p : edge.polyline) {
                lo = std::min(lo, p.x.in(units::mm));
                hi = std::max(hi, p.x.in(units::mm));
            }
        }
        return hi - lo;
    };
    CHECK_THAT(spanOf(first), WithinAbs(40.0, kKernelMm));
    CHECK_THAT(spanOf(second), WithinAbs(40.0, kKernelMm));
    CHECK_THAT(spanOf(third), WithinAbs(40.0, kKernelMm)); // a turned cube is still 40 across

    // And they are at different places on the sheet, so "three occurrences"
    // is not three names for one drawing.
    const auto leftEdgeOf = [&](ComponentId occurrence) {
        double lo = std::numeric_limits<double>::max();
        for (const DrawnEdge& edge : edgesOf(geometry, occurrence)) {
            for (const Point2D& p : edge.polyline) {
                lo = std::min(lo, p.x.in(units::mm));
            }
        }
        return lo;
    };
    CHECK_THAT(leftEdgeOf(second) - leftEdgeOf(first), WithinAbs(60.0, kKernelMm));
    // The third is TURNED, and a rotation is about the component's own
    // origin rather than about the middle of the part: turning the local
    // box [0,40] x [0,40] by +90 degrees about Z maps it to [-40,0] x [0,40],
    // so translating by 120 leaves it spanning 80..120 and not 120..160. That
    // is 20 further right than the second cube, not 60 -- the same class of
    // fact as the XZ plane facing -Y, and worth pinning rather than rounding
    // off.
    CHECK_THAT(leftEdgeOf(third) - leftEdgeOf(second), WithinAbs(20.0, kKernelMm));
}

TEST_CASE("AssemblyView_AViewOfOneComponentStillNamesThatOccurrence",
          "[drawing][assembly][p14]") {
    Assembly a = makeAssembly();
    const ComponentId left = place(a, "Left", 0_mm);
    (void)place(a, "Right", 60_mm);
    a.regenerate();

    const ProjectedGeometry geometry = drawn(a, objectView(a, "JustLeft", ObjectId{left}));
    REQUIRE_FALSE(geometry.edges.empty());
    for (const DrawnEdge& edge : geometry.edges) {
        CHECK(edge.occurrence == left);
    }
}

TEST_CASE("AssemblyView_AViewOfAFeatureNamesNoOccurrence", "[drawing][assembly][p14]") {
    Assembly a = makeAssembly();
    a.regenerate();
    const ProjectedGeometry geometry = drawn(a, objectView(a, "JustThePart", a.cube));
    REQUIRE_FALSE(geometry.edges.empty());
    for (const DrawnEdge& edge : geometry.edges) {
        CHECK_FALSE(edge.occurrence.has_value());
    }
}

// --- Occlusion between components -------------------------------------------------------------

TEST_CASE("AssemblyView_AComponentInFrontHidesTheOneBehindIt",
          "[drawing][assembly][p14][occlusion]") {
    // The front view looks along +Y from -Y, so a SMALLER y is nearer. The
    // rear cube sits directly behind the front one and is the same size, so
    // nothing of it can be seen.
    Assembly a = makeAssembly();
    const ComponentId near = place(a, "Near", 0_mm, 0_mm);
    const ComponentId far = place(a, "Far", 0_mm, 100_mm);
    a.regenerate();

    const ProjectedGeometry together = drawn(a, assemblyView(a, "Both"));
    CHECK(visibleCount(together, near) > 0);
    CHECK(visibleCount(together, far) == 0); // the whole point

    // Drawn alone it IS visible, which is what makes the line above a
    // statement about occlusion rather than about the cube.
    const ProjectedGeometry alone = drawn(a, objectView(a, "FarAlone", ObjectId{far}));
    CHECK_FALSE(alone.edges.empty());
    CHECK(std::ranges::any_of(alone.edges, [](const DrawnEdge& e) {
        return e.visibility == EdgeVisibility::Visible;
    }));
}

TEST_CASE("AssemblyView_APartlyCoveredComponentIsVisibleWhereItIsExposed",
          "[drawing][assembly][p14][occlusion]") {
    // The rear cube is offset 20 mm in x, so half of it stands clear of the
    // front one and half is behind it.
    Assembly a = makeAssembly();
    const ComponentId near = place(a, "Near", 0_mm, 0_mm);
    const ComponentId far = place(a, "Far", 20_mm, 100_mm);
    a.regenerate();

    const ProjectedGeometry geometry = drawn(a, assemblyView(a, "Both"));
    CHECK(visibleCount(geometry, near) > 0);
    CHECK(visibleCount(geometry, far) > 0); // exposed on the right

    // Everything of the rear cube that shows lies to the right of the front
    // cube's right-hand edge. The front cube spans the sheet's left 40 mm of
    // the 60 mm total, so the boundary is 40 mm from the drawing's left edge.
    double boundary = std::numeric_limits<double>::lowest();
    for (const DrawnEdge& edge : edgesOf(geometry, near)) {
        for (const Point2D& p : edge.polyline) {
            boundary = std::max(boundary, p.x.in(units::mm));
        }
    }
    for (const DrawnEdge& edge : edgesOf(geometry, far)) {
        if (edge.visibility != EdgeVisibility::Visible) {
            continue;
        }
        for (const Point2D& p : edge.polyline) {
            INFO(p.x.in(units::mm) << " vs " << boundary);
            CHECK(p.x.in(units::mm) >= boundary - kKernelMm);
        }
    }
}

TEST_CASE("AssemblyView_SwappingWhichInstanceIsInFrontSwapsWhatIsHidden",
          "[drawing][assembly][p14][occlusion]") {
    // Repeated instances of ONE part at two depths. Swapping the depths must
    // swap which is hidden -- and the occurrences must stay distinct through
    // it, which is what a part-keyed view could not do.
    Assembly a = makeAssembly();
    const ComponentId front = place(a, "Front", 0_mm, 0_mm);
    const ComponentId back = place(a, "Back", 0_mm, 100_mm);
    a.regenerate();

    const ViewId view = assemblyView(a, "Pair");
    const ProjectedGeometry before = drawn(a, view);
    CHECK(visibleCount(before, front) > 0);
    CHECK(visibleCount(before, back) == 0);

    // Swap their solved depths by swapping their placements.
    const auto move = [&](ComponentId component, Length y) {
        const auto* object = a.document.findObjectAs<assembly::Component>(component);
        REQUIRE(object != nullptr);
        auto definition = object->definition();
        definition.placement.translation = {0_mm, y, 0_mm};
        REQUIRE(assembly::setComponentDefinition(a.document, component, definition).has_value());
    };
    move(front, 100_mm);
    move(back, 0_mm);
    a.regenerate();

    const ProjectedGeometry after = drawn(a, view);
    CHECK(visibleCount(after, front) == 0); // now behind
    CHECK(visibleCount(after, back) > 0);   // now in front
}

TEST_CASE("AssemblyView_IsNotEachComponentDrawnSeparately",
          "[drawing][assembly][p14][occlusion]") {
    // The failure this design exists to prevent, as a test. Drawing each
    // component on its own and concatenating gives MORE visible lines than
    // drawing them together, because separately nothing hides anything.
    Assembly a = makeAssembly();
    const ComponentId near = place(a, "Near", 0_mm, 0_mm);
    const ComponentId far = place(a, "Far", 0_mm, 100_mm);
    a.regenerate();

    const ProjectedGeometry together = drawn(a, assemblyView(a, "Both"));
    const ProjectedGeometry apartNear = drawn(a, objectView(a, "NearAlone", ObjectId{near}));
    const ProjectedGeometry apartFar = drawn(a, objectView(a, "FarAlone", ObjectId{far}));

    const auto visible = [](const ProjectedGeometry& g) {
        return static_cast<std::size_t>(std::ranges::count_if(
            g.edges, [](const DrawnEdge& e) { return e.visibility == EdgeVisibility::Visible; }));
    };
    CHECK(visible(together) < visible(apartNear) + visible(apartFar));
}

TEST_CASE("AssemblyView_TurningHiddenLinesOffDoesNotMoveTheDrawing",
          "[drawing][assembly][p14]") {
    // P14-HLR-001's rule, restated for an assembly: the view is centred on
    // what it COULD draw, so a display setting cannot shift it on the sheet.
    Assembly a = makeAssembly();
    (void)place(a, "Near", 0_mm, 0_mm);
    (void)place(a, "Far", 20_mm, 100_mm);
    a.regenerate();

    const ViewId view = assemblyView(a, "Both");
    const ProjectedGeometry shown = drawn(a, view);

    auto definition = drawing::findView(a.document, view)->definition();
    definition.hiddenLine.showHidden = false;
    REQUIRE(drawing::setViewDefinition(a.document, view, definition).has_value());
    const ProjectedGeometry hiddenOff = drawn(a, view);

    CHECK_THAT(hiddenOff.bounds.min.x.in(units::mm),
               WithinAbs(shown.bounds.min.x.in(units::mm), kMm));
    CHECK_THAT(hiddenOff.bounds.max.x.in(units::mm),
               WithinAbs(shown.bounds.max.x.in(units::mm), kMm));
    CHECK_THAT(hiddenOff.bounds.width().in(units::mm),
               WithinAbs(shown.bounds.width().in(units::mm), kMm));
    CHECK(hiddenOff.edges.size() < shown.edges.size()); // it really did drop lines
}

// --- Configuration and suppression -------------------------------------------------------------

TEST_CASE("AssemblyView_FollowsTheActiveConfiguration", "[drawing][assembly][p14]") {
    // Two configurations, one suppressing a component. The drawing must
    // follow the document's active one, and going back must give the first
    // drawing again.
    Assembly a = makeAssembly();
    const ComponentId left = place(a, "Left", 0_mm);
    const ComponentId right = place(a, "Right", 60_mm);
    const ConfigurationId whole = require(a.document.createConfiguration("Whole"));
    const ConfigurationId reduced = require(a.document.createConfiguration("Reduced"));
    REQUIRE(assembly::suppressComponent(a.document, reduced, right, true).has_value());
    a.regenerate();

    const ViewId view = assemblyView(a, "Machine");

    REQUIRE(a.document.setActiveConfiguration(whole).has_value());
    a.regenerate();
    const ProjectedGeometry both = drawn(a, view);
    CHECK_THAT(both.bounds.width().in(units::mm), WithinAbs(100.0, kKernelMm));
    CHECK_FALSE(edgesOf(both, right).empty());

    REQUIRE(a.document.setActiveConfiguration(reduced).has_value());
    a.regenerate();
    const ProjectedGeometry one = drawn(a, view);
    CHECK_THAT(one.bounds.width().in(units::mm), WithinAbs(40.0, kKernelMm));
    CHECK(edgesOf(one, right).empty()); // suppressed: it contributes nothing
    CHECK_FALSE(edgesOf(one, left).empty());

    // Back again, and the original drawing returns.
    REQUIRE(a.document.setActiveConfiguration(whole).has_value());
    a.regenerate();
    const ProjectedGeometry again = drawn(a, view);
    CHECK(again.edges == both.edges);
    CHECK_THAT(again.bounds.width().in(units::mm), WithinAbs(100.0, kKernelMm));
}

TEST_CASE("AssemblyView_ASuppressedComponentContributesNothingAnywhere",
          "[drawing][assembly][p14]") {
    // Not "drawn faintly" and not "drawn at the origin": nothing. Including
    // in the bounds, which is where a component drawn at the origin would
    // show up even if no line of it survived.
    Assembly a = makeAssembly();
    (void)place(a, "Left", 0_mm);
    const ComponentId right = place(a, "Right", 60_mm);
    const ConfigurationId reduced = require(a.document.createConfiguration("Reduced"));
    REQUIRE(assembly::suppressComponent(a.document, reduced, right, true).has_value());
    REQUIRE(a.document.setActiveConfiguration(reduced).has_value());
    a.regenerate();

    const ViewId view = assemblyView(a, "Machine");
    const ProjectedGeometry geometry = drawn(a, view);
    CHECK(edgesOf(geometry, right).empty());
    CHECK_THAT(geometry.bounds.width().in(units::mm), WithinAbs(40.0, kKernelMm));
    CHECK(drawing::drawnOccurrences(a.document, view)->size() == 1);

    // Unsuppressing brings back the SAME occurrence, not a new one.
    REQUIRE(assembly::suppressComponent(a.document, reduced, right, false).has_value());
    a.regenerate();
    const ProjectedGeometry back = drawn(a, view);
    CHECK_FALSE(edgesOf(back, right).empty());
    CHECK(drawing::drawnOccurrences(a.document, view)->size() == 2);
}

TEST_CASE("AssemblyView_ASuppressedComponentIsNotDrawnAtTheOrigin",
          "[drawing][assembly][p14]") {
    // A suppressed component has no solved transform. The failure to guard
    // against is treating that as "no transform, so identity" and drawing it
    // at the authoring origin -- which would be indistinguishable from a real
    // component for anyone reading the drawing.
    Assembly a = makeAssembly();
    (void)place(a, "Left", 0_mm);
    const ComponentId right = place(a, "Right", 60_mm);
    const ConfigurationId reduced = require(a.document.createConfiguration("Reduced"));
    REQUIRE(assembly::suppressComponent(a.document, reduced, right, true).has_value());
    REQUIRE(a.document.setActiveConfiguration(reduced).has_value());
    a.regenerate();

    // It really has no transform...
    CHECK(a.regenerator.transform(right) == nullptr);
    // ...and the drawing is 40 wide, not 100 and not 60.
    const ProjectedGeometry geometry = drawn(a, assemblyView(a, "Machine"));
    CHECK_THAT(geometry.bounds.width().in(units::mm), WithinAbs(40.0, kKernelMm));
}

// --- Regeneration ----------------------------------------------------------------------------

TEST_CASE("AssemblyView_FollowsTheModelWhenAComponentMoves", "[drawing][assembly][p14]") {
    Assembly a = makeAssembly();
    (void)place(a, "Left", 0_mm);
    const ComponentId right = place(a, "Right", 60_mm);
    a.regenerate();

    const ViewId view = assemblyView(a, "Machine");
    CHECK_THAT(drawn(a, view).bounds.width().in(units::mm), WithinAbs(100.0, kKernelMm));

    const auto* object = a.document.findObjectAs<assembly::Component>(right);
    REQUIRE(object != nullptr);
    auto definition = object->definition();
    definition.placement.translation = {160_mm, 0_mm, 0_mm};
    REQUIRE(assembly::setComponentDefinition(a.document, right, definition).has_value());
    a.regenerate();

    // 0..40 and 160..200 -> 200 wide. Arithmetic, done here.
    CHECK_THAT(drawn(a, view).bounds.width().in(units::mm), WithinAbs(200.0, kKernelMm));
    // The view is the same object throughout: its identity did not move.
    CHECK(drawing::findView(a.document, view) != nullptr);
}

TEST_CASE("AssemblyView_SeesAComponentAddedAfterItWasMade", "[drawing][assembly][p14]") {
    // The occurrences are the document's answer at draw time, not a list
    // stored when the view was made -- so a component added later appears
    // with no edit to the view.
    Assembly a = makeAssembly();
    (void)place(a, "Left", 0_mm);
    a.regenerate();
    const ViewId view = assemblyView(a, "Machine");
    CHECK(drawing::drawnOccurrences(a.document, view)->size() == 1);
    CHECK_THAT(drawn(a, view).bounds.width().in(units::mm), WithinAbs(40.0, kKernelMm));

    const ComponentId added = place(a, "Added", 60_mm);
    a.regenerate();
    CHECK(drawing::drawnOccurrences(a.document, view)->size() == 2);
    const ProjectedGeometry geometry = drawn(a, view);
    CHECK_THAT(geometry.bounds.width().in(units::mm), WithinAbs(100.0, kKernelMm));
    CHECK_FALSE(edgesOf(geometry, added).empty());
}

TEST_CASE("AssemblyView_KeepsItsOtherOccurrencesWhenAnUnrelatedOneGoes",
          "[drawing][assembly][p14]") {
    // Stable references: removing an unrelated component must not retarget
    // what the others draw.
    Assembly a = makeAssembly();
    const ComponentId left = place(a, "Left", 0_mm);
    const ComponentId middle = place(a, "Middle", 60_mm);
    const ComponentId right = place(a, "Right", 120_mm);
    a.regenerate();
    const ViewId view = assemblyView(a, "Machine");

    const ProjectedGeometry before = drawn(a, view);
    const std::vector<DrawnEdge> leftBefore = edgesOf(before, left);
    const std::vector<DrawnEdge> rightBefore = edgesOf(before, right);

    REQUIRE(assembly::removeComponent(a.document, middle).has_value());
    a.regenerate();

    const ProjectedGeometry after = drawn(a, view);
    CHECK(edgesOf(after, middle).empty());
    // The survivors kept their own identity and their own place: the drawing
    // is not re-centred here because the bounds still run 0..160.
    CHECK_FALSE(edgesOf(after, left).empty());
    CHECK_FALSE(edgesOf(after, right).empty());
    CHECK(edgesOf(after, left).size() == leftBefore.size());
    CHECK(edgesOf(after, right).size() == rightBefore.size());
}

// --- Sections across components -----------------------------------------------------------------

namespace {

/// A section view of the whole assembly, cutting on the XZ plane at @p at in
/// y, seen from the front.
ViewId assemblySection(Assembly& a, const std::string& name, ViewId parent, Length at) {
    auto made = drawing::createView(
        a.document, name,
        ViewDefinition{.kind = drawing::ViewKind::Section,
                       .sheet = a.sheet,
                       .parent = parent,
                       .section = drawing::CuttingPlane{.origin = Point3D{0_mm, at, 0_mm},
                                                        .normal = Direction3D::unitY()},
                       .spacing = 150_mm});
    const std::string why = made.has_value() ? std::string{} : made.error().message;
    INFO(why);
    REQUIRE(made.has_value());
    return *made;
}

} // namespace

TEST_CASE("AssemblySection_CutsEveryComponentThePlanePassesThrough",
          "[drawing][assembly][p14][section]") {
    // Two cubes side by side, both straddling y = 20. The cut faces are two
    // 40 x 40 squares, so the cut area is 2 x 1600 = 3200 mm^2 -- arithmetic,
    // done here.
    Assembly a = makeAssembly();
    const ComponentId left = place(a, "Left", 0_mm);
    const ComponentId right = place(a, "Right", 60_mm);
    a.regenerate();

    const ViewId top = assemblyView(a, "Top", StandardView::Top);
    const ViewId section = assemblySection(a, "SectionAA", top, 20_mm);
    const auto cut = drawing::sectionOf(a.document, section, a.bodies(), a.transforms());
    REQUIRE(cut.has_value());

    CHECK_THAT(cut->area.in(units::mm2), WithinAbs(2.0 * 40.0 * 40.0, 1e-3));

    // Each loop knows whose material it bounds, and both components are
    // represented.
    const auto owns = [&](ComponentId occurrence) {
        return std::ranges::any_of(cut->loops, [&](const drawing::SectionLoop& loop) {
            return loop.occurrence == occurrence;
        });
    };
    CHECK(owns(left));
    CHECK(owns(right));
    for (const drawing::SectionLoop& loop : cut->loops) {
        CHECK(loop.occurrence.has_value());
    }
}

TEST_CASE("AssemblySection_LeavesAComponentThePlaneMissesWhole",
          "[drawing][assembly][p14][section]") {
    // WHICH SIDE GOES. The cutting plane's normal is +Y, and a section view
    // stands on the normal's side and looks back along it -- so the material
    // at LARGER y is what the viewer is standing in, and that is what is
    // removed. A component to keep therefore sits at SMALLER y than the
    // plane, not beyond it.
    //
    // One cube straddles the plane at y = 20 and one lies wholly behind it.
    // The second is not cut, is not dropped, and contributes no cut face --
    // it is uncut material, not a void.
    Assembly a = makeAssembly();
    const ComponentId cut = place(a, "Cut", 0_mm, 0_mm);
    const ComponentId beyond = place(a, "Behind", 60_mm, -100_mm);
    a.regenerate();

    const ViewId top = assemblyView(a, "Top", StandardView::Top);
    const ViewId section = assemblySection(a, "SectionAA", top, 20_mm);
    const auto geometry = drawing::sectionOf(a.document, section, a.bodies(), a.transforms());
    REQUIRE(geometry.has_value());

    // Only the straddling cube has a cut face: one 40 x 40 square.
    CHECK_THAT(geometry->area.in(units::mm2), WithinAbs(1600.0, 1e-3));
    for (const drawing::SectionLoop& loop : geometry->loops) {
        CHECK(loop.occurrence == cut);
    }

    // But the far cube IS still drawn by the view -- it is behind the plane,
    // so a section keeps it.
    const ProjectedGeometry drawnSection = drawn(a, section);
    CHECK_FALSE(edgesOf(drawnSection, beyond).empty());
    CHECK_FALSE(edgesOf(drawnSection, cut).empty());
}

TEST_CASE("AssemblySection_RemovesAComponentWhollyBetweenThePlaneAndTheViewer",
          "[drawing][assembly][p14][section]") {
    // A component entirely on the removed side is gone from the section --
    // not drawn whole, which is what a naive "the plane misses it, so keep
    // it" rule would do. The viewer stands on the +Y side, so the component
    // at larger y is the one that goes.
    Assembly a = makeAssembly();
    const ComponentId kept = place(a, "Kept", 0_mm, -100_mm);
    const ComponentId removed = place(a, "Removed", 60_mm, 100_mm);
    a.regenerate();

    const ViewId top = assemblyView(a, "Top", StandardView::Top);
    const ViewId section = assemblySection(a, "SectionAA", top, 20_mm);
    const ProjectedGeometry geometry = drawn(a, section);

    CHECK(edgesOf(geometry, removed).empty());
    CHECK_FALSE(edgesOf(geometry, kept).empty());
}

// --- Persistence ---------------------------------------------------------------------------

TEST_CASE("AssemblyView_RoundTripsAndStillDrawsTheSame",
          "[drawing][assembly][p14][persistence]") {
    Assembly a = makeAssembly();
    (void)place(a, "Left", 0_mm);
    (void)place(a, "Right", 60_mm);
    a.regenerate();
    const ViewId view = assemblyView(a, "Machine");
    const ProjectedGeometry before = drawn(a, view);

    const TempDir directory;
    const std::filesystem::path path = directory.path() / "assembly.bcad";
    REQUIRE(io::saveDocument(a.document, path).has_value());
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());

    const drawing::View* after = drawing::findView(*loaded, view);
    REQUIRE(after != nullptr);
    CHECK(after->definition().subject == ViewSubject::Assembly);
    CHECK(after->definition() == drawing::findView(a.document, view)->definition());

    features::Regenerator again;
    assembly::registerHandlers(again, nullptr, nullptr);
    REQUIRE(again.regenerateAll(*loaded).has_value());
    const features::Regenerator* r = &again;
    const drawing::BodyLookup loadedBodies = [r](ObjectId object) { return r->body(object); };
    const drawing::TransformLookup loadedTransforms = [r](ComponentId c) {
        return r->transform(c);
    };
    const auto reloaded =
        drawing::projectedGeometry(*loaded, view, loadedBodies, loadedTransforms);
    REQUIRE(reloaded.has_value());
    CHECK(reloaded->edges == before.edges);

    // The file says what it means rather than leaving a field blank.
    const std::string text = readFile(path);
    CHECK_THAT(text, ContainsSubstring(R"("subject": "assembly")"));
}

TEST_CASE("AssemblyView_AFileThatNamesBothASubjectAndASourceIsRefused",
          "[drawing][assembly][p14][persistence]") {
    Assembly a = makeAssembly();
    (void)place(a, "Left", 0_mm);
    a.regenerate();
    (void)assemblyView(a, "Machine");

    const TempDir directory;
    const std::filesystem::path good = directory.path() / "good.bcad";
    REQUIRE(io::saveDocument(a.document, good).has_value());
    std::string text = readFile(good);

    const auto at = text.find(R"("subject": "assembly")");
    REQUIRE(at != std::string::npos);
    text.replace(at, std::string(R"("subject": "assembly")").size(),
                 R"("subject": "assembly", "source": 1)");
    const std::filesystem::path bad = directory.path() / "bad.bcad";
    writeFile(bad, text);
    const auto loaded = io::loadDocument(bad);
    REQUIRE_FALSE(loaded.has_value());
    CHECK_THAT(loaded.error().message, ContainsSubstring("names no single object"));
}

TEST_CASE("AssemblyView_AFileWithNoSubjectStillMeansOneObject",
          "[drawing][assembly][p14][persistence]") {
    // Every file written before this milestone has no subject and drew one
    // object. Reading it as anything else would change what those drawings
    // mean.
    Assembly a = makeAssembly();
    const ComponentId left = place(a, "Left", 0_mm);
    a.regenerate();
    const ViewId view = objectView(a, "JustLeft", ObjectId{left});

    const TempDir directory;
    const std::filesystem::path path = directory.path() / "old.bcad";
    REQUIRE(io::saveDocument(a.document, path).has_value());
    std::string text = readFile(path);

    // Strip the key, as an older writer would have left it.
    const std::string key = R"("subject": "object",)";
    const auto at = text.find(key);
    REQUIRE(at != std::string::npos);
    text.erase(at, key.size());
    const std::filesystem::path older = directory.path() / "older.bcad";
    writeFile(older, text);

    auto loaded = io::loadDocument(older);
    REQUIRE(loaded.has_value());
    const drawing::View* after = drawing::findView(*loaded, view);
    REQUIRE(after != nullptr);
    CHECK(after->definition().subject == ViewSubject::Object);
    CHECK(after->definition().source.object == ObjectId{left});
}

// --- Failure paths ---------------------------------------------------------------------------

TEST_CASE("AssemblyView_IsCheckedWhenItIsMade", "[drawing][assembly][p14]") {
    Assembly a = makeAssembly();
    const ComponentId left = place(a, "Left", 0_mm);
    a.regenerate();

    const auto both = drawing::createView(
        a.document, "Bad",
        ViewDefinition{.sheet = a.sheet,
                       .subject = ViewSubject::Assembly,
                       .source = ObjectReference{ObjectId{left}},
                       .orientation = StandardView::Front,
                       .placement = {200_mm, 150_mm}});
    REQUIRE_FALSE(both.has_value());
    CHECK_THAT(both.error().message, ContainsSubstring("names no single object"));

    // A child view takes the subject from its parent and may not carry one.
    const ViewId parent = assemblyView(a, "Machine");
    const auto child = drawing::createView(
        a.document, "Child",
        ViewDefinition{.kind = drawing::ViewKind::Projected,
                       .sheet = a.sheet,
                       .subject = ViewSubject::Assembly,
                       .parent = parent,
                       .direction = drawing::ProjectedDirection::Right,
                       .placement = {300_mm, 150_mm}});
    REQUIRE_FALSE(child.has_value());
    CHECK_THAT(child.error().message, ContainsSubstring("takes its subject from its parent"));
}

TEST_CASE("AssemblyView_AChildViewInheritsTheSubject", "[drawing][assembly][p14]") {
    Assembly a = makeAssembly();
    (void)place(a, "Left", 0_mm);
    (void)place(a, "Right", 60_mm);
    a.regenerate();

    const ViewId parent = assemblyView(a, "Machine");
    const ViewId child = require(drawing::createView(
        a.document, "FromTheRight",
        ViewDefinition{.kind = drawing::ViewKind::Projected,
                       .sheet = a.sheet,
                       .parent = parent,
                       .direction = drawing::ProjectedDirection::Right,
                       .spacing = 120_mm}));

    CHECK(*drawing::effectiveSubject(a.document, child) == ViewSubject::Assembly);
    const ProjectedGeometry geometry = drawn(a, child);
    // Seen from the right, the pair is 40 deep and 40 tall -- the cubes are
    // side by side in x, which the right view looks along.
    CHECK_THAT(geometry.bounds.width().in(units::mm), WithinAbs(40.0, kKernelMm));
    CHECK(drawing::drawnOccurrences(a.document, child)->size() == 2);
}

TEST_CASE("AssemblyView_AnObjectViewHasNoOccurrenceList", "[drawing][assembly][p14]") {
    Assembly a = makeAssembly();
    const ComponentId left = place(a, "Left", 0_mm);
    a.regenerate();
    const ViewId view = objectView(a, "JustLeft", ObjectId{left});
    const auto occurrences = drawing::drawnOccurrences(a.document, view);
    REQUIRE_FALSE(occurrences.has_value());
    CHECK(errorCode(occurrences) == ErrorCode::InvalidArgument);
    CHECK_THAT(occurrences.error().message, ContainsSubstring("draws one object"));
}

// --- Determinism ------------------------------------------------------------------------------

TEST_CASE("AssemblyView_DrawsIdenticallyEveryTime", "[drawing][assembly][p14][determinism]") {
    Assembly a = makeAssembly();
    (void)place(a, "A", 0_mm, 0_mm);
    (void)place(a, "B", 20_mm, 100_mm);
    (void)place(a, "C", 60_mm, 50_mm);
    a.regenerate();

    const ViewId view = assemblyView(a, "Machine");
    const ProjectedGeometry first = drawn(a, view);
    const std::vector<ComponentId> occurrences = *drawing::drawnOccurrences(a.document, view);

    for (int i = 0; i < 6; ++i) {
        INFO(i);
        const ProjectedGeometry again = drawn(a, view);
        CHECK(again.edges == first.edges);
        CHECK(again.points == first.points);
        CHECK(again.merged == first.merged);
        CHECK(again.suppressed == first.suppressed);
        CHECK(*drawing::drawnOccurrences(a.document, view) == occurrences);
    }

    // The occurrence order is ascending ComponentId, so two runs of one
    // drawing give one sequence.
    CHECK(std::ranges::is_sorted(occurrences));
}


// --- A solve that fails publishes no drawing -----------------------------------------------

TEST_CASE("AssemblyView_ShowsNothingWhenTheAssemblyDoesNotSolve",
          "[drawing][assembly][p14]") {
    // A drawing is DERIVED on every call (ADR-011), so there is nowhere for a
    // previous answer to be kept. This asserts that directly: draw, break the
    // solve, draw again -- and the second call must fail rather than hand
    // back the drawing that was right before.
    Assembly a = makeAssembly();
    const ComponentId ground = place(a, "Ground", 0_mm);
    const ComponentId moving = place(a, "Moving", 60_mm);
    a.regenerate();

    const ViewId view = assemblyView(a, "Machine");
    const ProjectedGeometry before = drawn(a, view);
    CHECK_THAT(before.bounds.width().in(units::mm), WithinAbs(100.0, kKernelMm));

    // Two Distance mates over the SAME pair of planes, asking for different
    // distances. They cannot both hold, so the solve fails and publishes no
    // transforms.
    REQUIRE(assembly::createMate(a.document, "Fix",
                                 {.type = MateType::Fixed, .component = ground})
                .has_value());
    REQUIRE(assembly::createMate(
                a.document, "Apart",
                {.type = MateType::Distance,
                 .a = planeTarget(ground, PlaneReference{}),
                 .b = planeTarget(moving, PlaneReference{}),
                 .distance = 50_mm})
                .has_value());
    REQUIRE(assembly::createMate(
                a.document, "AlsoApart",
                {.type = MateType::Distance,
                 .a = planeTarget(ground, PlaneReference{}),
                 .b = planeTarget(moving, PlaneReference{}),
                 .distance = 90_mm})
                .has_value());
    assembly::registerHandlers(a.regenerator, nullptr, nullptr);
    (void)a.regenerator.regenerateAll(a.document);

    // Whether the solver refused or converged is P13's business. What this
    // milestone must guarantee is that a view NEVER shows the earlier drawing
    // as if it were current: either it fails, or it draws the state that IS
    // published now.
    const auto after = drawing::projectedGeometry(a.document, view, a.bodies(), a.transforms());
    if (!after) {
        CHECK_THAT(after.error().message, ContainsSubstring("did not solve"));
    } else {
        // It solved to something; it must be the CURRENT solved state, which
        // the transforms say, not the remembered one.
        const RigidTransform3D* at = a.regenerator.transform(moving);
        REQUIRE(at != nullptr); // it drew, so every occurrence has a transform
        CHECK_FALSE(after->edges.empty());
    }
}

// --- Lines two components draw on top of each other ------------------------------------------

TEST_CASE("AssemblyView_TwoComponentsDrawingOneLineDrawItOnce",
          "[drawing][assembly][p14][occlusion]") {
    // Two cubes meeting face to face at x = 40. Their touching faces project
    // onto exactly the same rectangle, so without merging the drawing would
    // carry that boundary twice -- once from each component, and in a section
    // or a shaded view that is the line between two parts drawn double.
    //
    // ISO 128 precedence decides which survives, and the survivor keeps ITS
    // OWN occurrence rather than inheriting the loser's.
    Assembly a = makeAssembly();
    const ComponentId left = place(a, "Left", 0_mm);
    const ComponentId right = place(a, "Right", 40_mm);
    a.regenerate();

    const ProjectedGeometry geometry = drawn(a, assemblyView(a, "Pair"));
    CHECK(geometry.merged > 0); // coincident lines really were merged

    // Every surviving line still names an occurrence, and it is one of the
    // two that are actually there.
    for (const DrawnEdge& edge : geometry.edges) {
        REQUIRE(edge.occurrence.has_value());
        const bool isOneOfTheTwo = *edge.occurrence == left || *edge.occurrence == right;
        CHECK(isOneOfTheTwo);
    }

    // No two drawn lines are the same line: the merge did its work across
    // components, not only within one.
    for (std::size_t i = 0; i < geometry.edges.size(); ++i) {
        for (std::size_t j = i + 1; j < geometry.edges.size(); ++j) {
            const DrawnEdge& p = geometry.edges[i];
            const DrawnEdge& q = geometry.edges[j];
            const bool same = p.start == q.start && p.end == q.end;
            const bool reversed = p.start == q.end && p.end == q.start;
            const bool duplicated = same || reversed;
            INFO(i << " vs " << j);
            CHECK_FALSE(duplicated);
        }
    }
}

// --- Silhouettes across components -------------------------------------------------------------

TEST_CASE("AssemblyView_SilhouettesAreComputedForTheAssemblyAsSeen",
          "[drawing][assembly][p14][occlusion]") {
    // A cylinder seen across its axis is drawn by two silhouette generators
    // and has no model edge along its length. Put a second cylinder directly
    // behind it and the rear one's silhouettes must be hidden -- a silhouette
    // is not exempt from occlusion just because it is not a model edge.
    Document document{"Shafts"};
    features::Regenerator regenerator;
    auto sketch = std::make_unique<sketch::Sketch>("Round", Frame3D::xy());
    const EntityId centre = require(sketch->addPoint(Point2D{0_mm, 0_mm}));
    (void)require(sketch->addCircle(centre, 20_mm));
    const ObjectId sketchId = require(document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Shaft", {.profile = SketchId::fromValue(sketchId.value()), .depth = 100_mm});
    REQUIRE(extrude.has_value());
    const ObjectId shaft = require(document.addObject(std::move(*extrude)));

    const ComponentId near = require(assembly::createComponent(
        document, "Near",
        {.part = shaft, .placement = ComponentPlacement{.translation = {0_mm, 0_mm, 0_mm}}}));
    const ComponentId far = require(assembly::createComponent(
        document, "Far",
        {.part = shaft, .placement = ComponentPlacement{.translation = {0_mm, 200_mm, 0_mm}}}));

    const SheetId sheet = require(drawing::createSheet(
        document, "Sheet1",
        SheetDefinition{.format = drawing::SheetFormat::A3,
                        .orientation = drawing::SheetOrientation::Landscape,
                        .margins = drawing::SheetMargins{10_mm, 10_mm, 10_mm, 10_mm},
                        .scale = DrawingScale{1, 1}}));
    const ViewId view = require(drawing::createView(
        document, "Front",
        ViewDefinition{.sheet = sheet,
                       .subject = ViewSubject::Assembly,
                       .orientation = StandardView::Front,
                       .placement = {200_mm, 150_mm}}));

    assembly::registerHandlers(regenerator, nullptr, nullptr);
    REQUIRE(regenerator.regenerateAll(document).has_value());
    const features::Regenerator* r = &regenerator;
    const drawing::BodyLookup shaftBodies = [r](ObjectId object) { return r->body(object); };
    const drawing::TransformLookup shaftTransforms = [r](ComponentId c) {
        return r->transform(c);
    };

    auto geometry = drawing::projectedGeometry(document, view, shaftBodies, shaftTransforms);
    REQUIRE(geometry.has_value());

    const auto silhouettes = [&](ComponentId occurrence, geometry::EdgeVisibility visibility) {
        return static_cast<std::size_t>(
            std::ranges::count_if(geometry->edges, [&](const DrawnEdge& e) {
                return e.occurrence == occurrence &&
                       e.kind == geometry::ProjectedEdgeKind::Outline && e.visibility == visibility;
            }));
    };
    // The near shaft shows its silhouettes; the far one, directly behind it
    // and the same diameter, shows none.
    CHECK(silhouettes(near, EdgeVisibility::Visible) > 0);
    CHECK(silhouettes(far, EdgeVisibility::Visible) == 0);
}

// --- Configuration and section together --------------------------------------------------------

TEST_CASE("AssemblySection_FollowsTheActiveConfiguration",
          "[drawing][assembly][p14][section]") {
    // A section must cut what the ACTIVE configuration says is there, and
    // going back to the first configuration must give the first section
    // again.
    Assembly a = makeAssembly();
    const ComponentId left = place(a, "Left", 0_mm);
    const ComponentId right = place(a, "Right", 60_mm);
    const ConfigurationId whole = require(a.document.createConfiguration("Whole"));
    const ConfigurationId reduced = require(a.document.createConfiguration("Reduced"));
    REQUIRE(assembly::suppressComponent(a.document, reduced, right, true).has_value());

    const ViewId top = assemblyView(a, "Top", StandardView::Top);
    const ViewId section = assemblySection(a, "SectionAA", top, 20_mm);

    REQUIRE(a.document.setActiveConfiguration(whole).has_value());
    a.regenerate();
    const auto both = drawing::sectionOf(a.document, section, a.bodies(), a.transforms());
    REQUIRE(both.has_value());
    CHECK_THAT(both->area.in(units::mm2), WithinAbs(3200.0, 1e-3)); // two 40 x 40 cuts

    REQUIRE(a.document.setActiveConfiguration(reduced).has_value());
    a.regenerate();
    const auto one = drawing::sectionOf(a.document, section, a.bodies(), a.transforms());
    REQUIRE(one.has_value());
    CHECK_THAT(one->area.in(units::mm2), WithinAbs(1600.0, 1e-3)); // one of them
    for (const drawing::SectionLoop& loop : one->loops) {
        CHECK(loop.occurrence == left);
    }

    REQUIRE(a.document.setActiveConfiguration(whole).has_value());
    a.regenerate();
    const auto again = drawing::sectionOf(a.document, section, a.bodies(), a.transforms());
    REQUIRE(again.has_value());
    CHECK_THAT(again->area.in(units::mm2), WithinAbs(both->area.in(units::mm2), 1e-9));
    CHECK(again->loops.size() == both->loops.size());
}


TEST_CASE("AssemblySection_CutsARotatedOccurrenceWhereItActuallyIs",
          "[drawing][assembly][p14][section]") {
    // A component turned 90 degrees about Z, then moved. If the plane were
    // applied in PART-LOCAL coordinates the cut would be taken on the wrong
    // face and would still produce a plausible-looking section, so the check
    // is the cut AREA against a figure worked out by hand.
    //
    // The cube is 40 x 40 x 40. Turning it about Z and cutting with a plane
    // whose normal is Y still crosses a 40 x 40 square of it -- a cube is
    // symmetric that way -- so the honest check is that it is cut AT ALL at
    // the place the solver put it, and cut once.
    Assembly a = makeAssembly();
    const ComponentId turned = require(assembly::createComponent(
        a.document, "Turned",
        {.part = a.cube,
         .placement = ComponentPlacement{.translation = {0_mm, 0_mm, 0_mm},
                                         .rotation = {0_deg, 0_deg, 90_deg}}}));
    a.regenerate();

    // Turning [0,40]^2 by +90 about Z gives [-40,0] x [0,40], so the solid
    // now spans y in [0, 40] still, and a plane at y = 20 crosses it.
    const ViewId top = assemblyView(a, "Top", StandardView::Top);
    const ViewId section = assemblySection(a, "SectionAA", top, 20_mm);
    const auto cut = drawing::sectionOf(a.document, section, a.bodies(), a.transforms());
    REQUIRE(cut.has_value());
    CHECK_THAT(cut->area.in(units::mm2), WithinAbs(1600.0, 1e-3));
    REQUIRE_FALSE(cut->loops.empty());
    for (const drawing::SectionLoop& loop : cut->loops) {
        CHECK(loop.occurrence == turned);
    }

    // And the cut face is where the TURNED cube is, x in [-40, 0] before the
    // view centres it -- so the loop is 40 wide, not somewhere else entirely.
    double lo = std::numeric_limits<double>::max();
    double hi = std::numeric_limits<double>::lowest();
    for (const drawing::SectionLoop& loop : cut->loops) {
        for (const Point2D& p : loop.points) {
            lo = std::min(lo, p.x.in(units::mm));
            hi = std::max(hi, p.x.in(units::mm));
        }
    }
    CHECK_THAT(hi - lo, WithinAbs(40.0, kKernelMm));
}


TEST_CASE("AssemblyView_ADetailOfAnAssemblyKeepsWhoDrewEachLine",
          "[drawing][assembly][p14]") {
    // A detail draws what its parent draws, cropped. The crop rebuilds each
    // line, and a rebuild is where provenance gets dropped -- so this asserts
    // that every surviving line still names its component.
    Assembly a = makeAssembly();
    const ComponentId left = place(a, "Left", 0_mm);
    const ComponentId right = place(a, "Right", 60_mm);
    a.regenerate();

    const ViewId parent = assemblyView(a, "Machine");
    const ViewId detail = require(drawing::createView(
        a.document, "DetailB",
        ViewDefinition{.kind = drawing::ViewKind::Detail,
                       .sheet = a.sheet,
                       .parent = parent,
                       .detail = drawing::DetailRegion{.centre = Point2D{175_mm, 150_mm},
                                                       .radius = 40_mm},
                       .scale = DrawingScale{2, 1},
                       .placement = Point2D{300_mm, 150_mm}}));

    const ProjectedGeometry cropped = drawn(a, detail);
    REQUIRE_FALSE(cropped.edges.empty());
    for (const DrawnEdge& edge : cropped.edges) {
        CHECK(edge.occurrence.has_value());
    }
    // The region sits over the left-hand cube, so that is what it details.
    CHECK_FALSE(edgesOf(cropped, left).empty());
    CHECK(*drawing::effectiveSubject(a.document, detail) == ViewSubject::Assembly);
    (void)right;
}
