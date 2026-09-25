#include "reference/DrawingTestSupport.hpp"

#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Configurations.hpp>
#include <bettercad/assembly/Mates.hpp>
#include <bettercad/assembly/Solver.hpp>
#include <bettercad/core/document/MateReference.hpp>
#include <bettercad/core/math/RigidTransform.hpp>
#include <bettercad/drawing/HiddenLine.hpp>
#include <bettercad/drawing/Sheets.hpp>
#include <bettercad/features/Validation.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace bettercad;
using namespace bettercad::test;
using namespace bettercad::test::drawref;
using Catch::Matchers::WithinAbs;

// RM-DWG-06 -- the assembly drawing: repeated parts, a rotated part, and
// occlusion between components.
namespace {

constexpr double kBodyLengthMm = 90.0;
constexpr double kBodyWidthMm = 50.0;
constexpr double kDeckMm = 15.0;
constexpr double kJawWidthMm = 20.0;
constexpr double kJawHeightMm = 35.0;
constexpr double kPinHeightMm = 40.0;
constexpr double kKeyHeightMm = 8.0;
constexpr double kKeyBaseMm = 55.0;

/// The model's overall height: the key sits on top of the pin, which stands
/// on the deck. 55 + 8 = 63.
constexpr double kOverallHeightMm = kKeyBaseMm + kKeyHeightMm;

} // namespace

TEST_CASE("DrawingReference_ClampSetSolvesWithEveryComponentLocated",
          "[reference][drawing][rm-dwg-06][assembly]") {
    // Five occurrences of four parts, and NOTHING left free. A reference
    // model with a degree of freedom could draw differently between runs.
    Drawn d = drawn(reference::DrawingReferenceModelKind::ClampSet);
    CHECK(features::validateDocument(d.document()).valid());
    CHECK(assembly::components(d.document()).size() == 5);
    CHECK(assembly::activeComponents(d.document()).size() == 5);

    auto solved = assembly::solve(d.document(), {}, d.bodies());
    INFO(why(solved));
    REQUIRE(solved.has_value());
    INFO("status " << assembly::toString(solved->status));
    CHECK(solved->solved());
    CHECK(solved->degreesOfFreedom == 0);
}

TEST_CASE("DrawingReference_ClampSetPlacesTwoInstancesOfOneJawPart",
          "[reference][drawing][rm-dwg-06][assembly]") {
    // TWO OCCURRENCES, ONE PART. The mates put them 46 mm apart along X, and
    // the expected positions are the ones passed to locate() -- world
    // coordinates, derived by hand rather than read back from the solver.
    auto m = reference::buildDrawnClampSetReferenceModel();
    INFO(why(m));
    REQUIRE(m.has_value());
    const ComponentId left = m->jawLeft;
    const ComponentId right = m->jawRight;
    const ObjectId jawPart = m->jawPart;
    Drawn model{std::move(m->document)};
    INFO(model.why());
    REQUIRE(model.succeeded());

    const assembly::Component* a = model.document().findObjectAs<assembly::Component>(left);
    const assembly::Component* b = model.document().findObjectAs<assembly::Component>(right);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(a->definition().part.object == jawPart);
    CHECK(b->definition().part.object == jawPart);
    CHECK(left != right);

    const RigidTransform3D* leftAt = model.regenerator().transform(left);
    const RigidTransform3D* rightAt = model.regenerator().transform(right);
    REQUIRE(leftAt != nullptr);
    REQUIRE(rightAt != nullptr);
    const Point3D origin{};
    const Point3D leftOrigin = leftAt->apply(origin);
    const Point3D rightOrigin = rightAt->apply(origin);
    // 1e-9 mm: the solve's own residual on this model is far below that.
    CHECK_THAT(leftOrigin.x.in(units::mm),
               WithinAbs(reference::DrawnClampSetModel::kJawLeftXMm, 1e-9));
    CHECK_THAT(rightOrigin.x.in(units::mm),
               WithinAbs(reference::DrawnClampSetModel::kJawRightXMm, 1e-9));
    CHECK_THAT(leftOrigin.y.in(units::mm),
               WithinAbs(reference::DrawnClampSetModel::kJawYMm, 1e-9));
    CHECK_THAT(leftOrigin.z.in(units::mm), WithinAbs(kDeckMm, 1e-9));
    CHECK_THAT(rightOrigin.z.in(units::mm), WithinAbs(kDeckMm, 1e-9));
}

TEST_CASE("DrawingReference_ClampSetFrontViewIsTheWholeAssembly",
          "[reference][drawing][rm-dwg-06][assembly]") {
    // The front view looks along -Y with +X right and +Z up, so it is as wide
    // as the body and as tall as the whole stack. Both numbers are computed
    // here from the part sizes and the placements.
    Drawn d = drawn(reference::DrawingReferenceModelKind::ClampSet);
    auto m = reference::buildDrawnClampSetReferenceModel();
    REQUIRE(m.has_value());

    auto geometry = drawing::projectedGeometry(d.document(), m->front, d.bodies(), d.transforms());
    INFO(why(geometry));
    REQUIRE(geometry.has_value());
    const double width =
        (geometry->bounds.max.x - geometry->bounds.min.x).in(units::mm);
    const double height =
        (geometry->bounds.max.y - geometry->bounds.min.y).in(units::mm);
    CHECK_THAT(width, WithinAbs(kBodyLengthMm, 1e-9));
    CHECK_THAT(height, WithinAbs(kOverallHeightMm, 1e-9));

    // And the top view, below it, is the body's footprint.
    auto top = drawing::projectedGeometry(d.document(), m->top, d.bodies(), d.transforms());
    INFO(why(top));
    REQUIRE(top.has_value());
    CHECK_THAT((top->bounds.max.x - top->bounds.min.x).in(units::mm),
               WithinAbs(kBodyLengthMm, 1e-9));
    CHECK_THAT((top->bounds.max.y - top->bounds.min.y).in(units::mm),
               WithinAbs(kBodyWidthMm, 1e-9));
}

TEST_CASE("DrawingReference_ClampSetHidesEdgesBehindOtherComponents",
          "[reference][drawing][rm-dwg-06][hlr]") {
    // OCCLUSION BETWEEN COMPONENTS. The jaws and the pin stand behind the
    // body's front face, so part of each of them is hidden BY THE BODY -- a
    // different solid, not their own material. The evidence from outside is
    // that the sheet carries ISO 128 hidden detail; switching hidden lines
    // off must remove exactly that and leave every visible line alone.
    Drawn d = drawn(reference::DrawingReferenceModelKind::ClampSet);
    const SheetId sheet = drawing::sheets(d.document()).front();
    const drawing::DrawingScene withHidden = sceneOf(d, sheet);
    CHECK(hiddenLineCount(withHidden) > 0);

    auto m = reference::buildDrawnClampSetReferenceModel();
    REQUIRE(m.has_value());
    Drawn off{std::move(m->document)};
    REQUIRE(off.succeeded());
    for (const ViewId view : drawing::viewsOn(off.document(), sheet)) {
        const drawing::View* existing = drawing::findView(off.document(), view);
        REQUIRE(existing != nullptr);
        drawing::ViewDefinition definition = existing->definition();
        definition.hiddenLine.showHidden = false;
        REQUIRE(drawing::setViewDefinition(off.document(), view, definition).has_value());
    }
    off.regenerate();
    INFO(off.why());
    REQUIRE(off.succeeded());

    const drawing::DrawingScene withoutHidden = sceneOf(off, sheet);
    CHECK(hiddenLineCount(withoutHidden) == 0);

    // Every line that was not hidden detail is still there, unchanged.
    const auto visibleOf = [](const drawing::DrawingScene& scene) {
        std::vector<drawing::SceneLine> lines;
        for (const drawing::SceneLine& line : scene.items.lines) {
            if (line.style != drawing::LineStyle::Dashed) {
                lines.push_back(line);
            }
        }
        return lines;
    };
    CHECK(visibleOf(withHidden) == visibleOf(withoutHidden));
}

TEST_CASE("DrawingReference_ClampSetKeyIsDrawnTurnedRatherThanSquare",
          "[reference][drawing][rm-dwg-06][assembly]") {
    // THE ROTATED COMPONENT, and the reason the suite has one. In the top
    // view the key is a 60 x 10 rectangle turned 35 degrees, so its drawn
    // extent along X is 60cos35 + 10sin35 and along Y is 60sin35 + 10cos35 --
    // neither of which is 60 or 10. A component whose rotation had been
    // dropped would draw a square-on rectangle and pass every size check that
    // did not look at the angle.
    Drawn d = drawn(reference::DrawingReferenceModelKind::ClampSet);
    auto m = reference::buildDrawnClampSetReferenceModel();
    REQUIRE(m.has_value());

    const auto key = d.document().findByName("KeyBar");
    REQUIRE(key.has_value());
    const ComponentId component = ComponentId::fromValue(key->value());
    const RigidTransform3D* at = d.regenerator().transform(component);
    REQUIRE(at != nullptr);

    // The part's local +X, turned into assembly space: its direction is the
    // spin. cos and sin of 35 degrees, computed here.
    const Point3D origin = at->apply(Point3D{});
    const Point3D alongX = at->apply(Point3D{Length::fromSi(1.0), Length{}, Length{}});
    const double dx = (alongX.x - origin.x).si();
    const double dy = (alongX.y - origin.y).si();
    const double spin = std::atan2(dy, dx) * 180.0 / std::numbers::pi;
    CHECK_THAT(spin, WithinAbs(reference::DrawnClampSetModel::kPinSpinDeg, 1e-9));
}

TEST_CASE("DrawingReference_ClampSetDimensionFollowsThePartItNames",
          "[reference][drawing][rm-dwg-06][regen]") {
    // A dimension on an assembly view, against a NAMED face of the part the
    // body component places. Growing that part must move the number, and the
    // view with it.
    auto m = reference::buildDrawnClampSetReferenceModel();
    INFO(why(m));
    REQUIRE(m.has_value());
    Drawn model{std::move(m->document)};
    INFO(model.why());
    REQUIRE(model.succeeded());
    CHECK_THAT(measuredMm(model, m->bodyLengthDimension), WithinAbs(kBodyLengthMm, 1e-9));

    constexpr double kGrownMm = 100.0;
    driveMm(model, "body_length", kGrownMm);
    CHECK_THAT(measuredMm(model, m->bodyLengthDimension), WithinAbs(kGrownMm, 1e-9));

    auto grown = drawing::projectedGeometry(model.document(), m->front, model.bodies(),
                                            model.transforms());
    REQUIRE(grown.has_value());
    CHECK_THAT((grown->bounds.max.x - grown->bounds.min.x).in(units::mm),
               WithinAbs(kGrownMm, 1e-9));

    driveMm(model, "body_length", kBodyLengthMm);
    CHECK_THAT(measuredMm(model, m->bodyLengthDimension), WithinAbs(kBodyLengthMm, 1e-9));
}

TEST_CASE("DrawingReference_ClampSetDrawingIsNotCurrentWhenItsAssemblyBreaks",
          "[reference][drawing][rm-dwg-06][recovery]") {
    // VALID -> BROKEN -> REPAIRED, through the assembly. Two mates that ask
    // for different things leave the assembly over-constrained, and P13
    // publishes NO transforms at all rather than a partial set -- so the
    // drawing has nothing to project and must say so rather than showing the
    // last good picture. Removing the offending mate must bring the original
    // drawing back exactly.
    auto m = reference::buildDrawnClampSetReferenceModel();
    INFO(why(m));
    REQUIRE(m.has_value());
    const ComponentId body = m->body;
    const ComponentId jawLeft = m->jawLeft;
    Drawn model{std::move(m->document)};
    INFO(model.why());
    REQUIRE(model.succeeded());
    const SheetId sheet = drawing::sheets(model.document()).front();
    const drawing::DrawingScene before = sceneOf(model, sheet);

    // The left jaw is already located 15 mm up by its mates; a second mate
    // that insists it is 40 mm up contradicts them.
    auto broken = assembly::createMate(
        model.document(), "Contradiction",
        {.type = assembly::MateType::Distance,
         .a = planeTarget(body, PlaneReference{.plane = PrincipalPlane::XY}),
         .b = planeTarget(jawLeft, PlaneReference{.plane = PrincipalPlane::XY}),
         .distance = 40.0 * units::mm});
    INFO(why(broken));
    REQUIRE(broken.has_value());
    model.regenerate();

    auto solved = assembly::solve(model.document(), {}, model.bodies());
    REQUIRE(solved.has_value());
    CHECK_FALSE(solved->solved());
    // No stale picture: the sheet refuses rather than drawing what it had.
    auto stale = model.scene(sheet);
    CHECK_FALSE(stale.has_value());

    REQUIRE(assembly::removeMate(model.document(), *broken).has_value());
    model.regenerate();
    INFO(model.why());
    REQUIRE(model.succeeded());
    const drawing::DrawingScene after = sceneOf(model, sheet);
    CHECK(after.items.lines == before.items.lines);
    CHECK(after.items.arcs == before.items.arcs);
    CHECK(after.items.texts == before.items.texts);
}
