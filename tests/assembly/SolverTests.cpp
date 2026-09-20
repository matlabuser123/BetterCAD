#include "features/FeatureTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Mates.hpp>
#include <bettercad/assembly/Solver.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/MateReference.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <filesystem>
#include <format>
#include <cmath>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using assembly::MateDefinition;
using assembly::MateType;
using assembly::SolveStatus;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

// P13-SOLVE-001: solving mate intent into component transforms.
//
// Every expected value here is derived by hand from the geometry, never read
// back from the solver. That distinction is the whole point of this file: the
// residuals the solver drives to zero are computed from the same equations it
// differentiates, so "the residual is small" proves the solver agrees with
// itself. Only an independently derived position proves it agrees with the
// world.

namespace {

/// The solver converges below 1e-9 m of residual, and a position is the same
/// order as the residual that places it, so 1e-8 m -- a hundredth of a
/// micrometre -- is tight enough to catch any real error and loose enough not
/// to trip on the last bits of a converged solve.
constexpr double kTolerance = 1e-8;

struct Rig {
    Document document{"Assembly"};
    ObjectId sketch{};
    ObjectId part{};
};

Rig makeRig() {
    Rig rig;
    auto sketch = std::make_unique<sketch::Sketch>("BlockSketch", Frame3D::xy());
    addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
    rig.sketch = require(rig.document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Block", {.profile = SketchId::fromValue(rig.sketch.value()), .depth = 10_mm});
    REQUIRE(extrude.has_value());
    rig.part = require(rig.document.addObject(std::move(*extrude)));
    return rig;
}

ComponentId place(Rig& rig, const std::string& name, const ComponentPlacement& placement = {}) {
    return require(assembly::createComponent(rig.document, name, {.part = rig.part, .placement = placement}));
}

MateTarget plane(ComponentId component, PrincipalPlane which = PrincipalPlane::XY) {
    return planeTarget(component, PlaneReference{.plane = which});
}
MateTarget axis(ComponentId component) { return axisTarget(component, AxisReference{}); }

void add(Rig& rig, const std::string& name, const MateDefinition& definition) {
    auto id = assembly::createMate(rig.document, name, definition);
    if (!id) {
        FAIL(id.error().message);
    }
}

assembly::AssemblySolveResult solved(const Rig& rig) {
    auto result = assembly::solve(rig.document);
    if (!result) {
        FAIL(result.error().message);
    }
    return *result;
}

void checkPosition(const RigidTransform3D& transform, double x, double y, double z) {
    const Point3D origin = transform.apply(Point3D{});
    CHECK_THAT(origin.x.si(), WithinAbs(x, kTolerance));
    CHECK_THAT(origin.y.si(), WithinAbs(y, kTolerance));
    CHECK_THAT(origin.z.si(), WithinAbs(z, kTolerance));
}

void checkDirection(const Direction3D& measured, double x, double y, double z) {
    CHECK_THAT(measured.x(), WithinAbs(x, kTolerance));
    CHECK_THAT(measured.y(), WithinAbs(y, kTolerance));
    CHECK_THAT(measured.z(), WithinAbs(z, kTolerance));
}

} // namespace

TEST_CASE("Solve_AGroundedComponentStaysExactlyWhereItsPlacementPutIt", "[assembly][solve][p13]") {
    // The simplest case there is, and the one that would expose a solver that
    // moves things it was not asked to move.
    Rig rig = makeRig();
    const ComponentId a = place(rig, "Block1", {.translation = {10_mm, 20_mm, 30_mm}});
    add(rig, "Ground", {.type = MateType::Fixed, .component = a});

    const auto result = solved(rig);
    CHECK(result.status == SolveStatus::FullyConstrained);
    CHECK(result.unknowns == 0);
    CHECK(result.equations == 0);
    CHECK(result.degreesOfFreedom == 0);
    checkPosition(result.transforms.at(a), 0.010, 0.020, 0.030);
}

TEST_CASE("Solve_DistanceBetweenPlanesMovesOnlyAlongTheNormal", "[assembly][solve][p13]") {
    // Hand-derived. A is grounded at the origin, so its XY plane is z = 0 with
    // normal +Z. The residual is (Pb - Pa).Da - 25 mm = zb - 25 mm, so the
    // solution is zb = 25 mm, and a minimum-norm step changes nothing else.
    Rig rig = makeRig();
    const ComponentId a = place(rig, "Block1");
    const ComponentId b = place(rig, "Block2", {.translation = {7_mm, -3_mm, 100_mm}});
    add(rig, "Ground", {.type = MateType::Fixed, .component = a});
    add(rig, "Gap", {.type = MateType::Distance, .a = plane(a), .b = plane(b), .distance = 25_mm});

    const auto result = solved(rig);
    REQUIRE(result.solved());
    CHECK(result.unknowns == 6);
    CHECK(result.equations == 1);
    // One equation of rank one leaves five ways to move.
    CHECK(result.degreesOfFreedom == 5);
    CHECK(result.status == SolveStatus::UnderConstrained);
    // x and y are untouched: the constraint does not speak about them and the
    // step is minimum-norm.
    checkPosition(result.transforms.at(b), 0.007, -0.003, 0.025);
    // And the grounded one did not move.
    checkPosition(result.transforms.at(a), 0.0, 0.0, 0.0);
}

TEST_CASE("Solve_DistanceBetweenPlanesIsSignedSoTheOtherSideIsNegative", "[assembly][solve][p13]") {
    // The sign convention P13-MATE-001 wrote down, now exercised: -25 mm puts
    // b on the other side of a's plane.
    Rig rig = makeRig();
    const ComponentId a = place(rig, "Block1");
    const ComponentId b = place(rig, "Block2", {.translation = {0_mm, 0_mm, 100_mm}});
    add(rig, "Ground", {.type = MateType::Fixed, .component = a});
    add(rig, "Gap", {.type = MateType::Distance, .a = plane(a), .b = plane(b), .distance = -25_mm});

    const auto result = solved(rig);
    REQUIRE(result.solved());
    checkPosition(result.transforms.at(b), 0.0, 0.0, -0.025);
}

TEST_CASE("Solve_CoincidentPlanesLeaveThreeDegreesOfFreedom", "[assembly][solve][p13]") {
    // Hand-derived: two coincident planes may still slide in two directions
    // and spin about their shared normal, so exactly three of the six ways to
    // move survive. The DOF count is the claim here, not just the position.
    Rig rig = makeRig();
    const ComponentId a = place(rig, "Block1");
    const ComponentId b = place(rig, "Block2", {.translation = {12_mm, -8_mm, 50_mm}});
    add(rig, "Ground", {.type = MateType::Fixed, .component = a});
    add(rig, "Touch", {.type = MateType::Coincident, .a = plane(a), .b = plane(b)});

    const auto result = solved(rig);
    REQUIRE(result.solved());
    CHECK(result.equations == 3);
    CHECK(result.degreesOfFreedom == 3);
    CHECK(result.status == SolveStatus::UnderConstrained);
    // The planes meet: z goes to zero, and the sliding directions are left
    // alone because nothing asked them to move.
    checkPosition(result.transforms.at(b), 0.012, -0.008, 0.0);
    checkDirection(result.transforms.at(b).apply(Direction3D::unitZ()), 0.0, 0.0, 1.0);
}

TEST_CASE("Solve_ConcentricAxesLeaveTwoDegreesOfFreedom", "[assembly][solve][p13]") {
    // Hand-derived: collinear axes may still slide along themselves and spin
    // about themselves, so two of six survive. The offset perpendicular to
    // the axis goes to zero; the offset along it does not.
    Rig rig = makeRig();
    const ComponentId a = place(rig, "Block1");
    const ComponentId b = place(rig, "Block2", {.translation = {30_mm, 40_mm, 10_mm}});
    add(rig, "Ground", {.type = MateType::Fixed, .component = a});
    add(rig, "Bore", {.type = MateType::Concentric, .a = axis(a), .b = axis(b)});

    const auto result = solved(rig);
    REQUIRE(result.solved());
    CHECK(result.equations == 4);
    CHECK(result.degreesOfFreedom == 2);
    // x and y collapse onto the axis; z is free and so is untouched.
    checkPosition(result.transforms.at(b), 0.0, 0.0, 0.010);
}

TEST_CASE("Solve_ParallelFromANearStartTurnsBackTheWayItCame", "[assembly][solve][p13]") {
    // Hand-derived: b's normal starts 20 degrees from a's. The residual is
    // proportional to sin(theta), whose derivative cos(20 deg) = 0.94 is
    // healthy, so the Newton step -tan(20 deg) lands close to upright and the
    // solve settles on the branch it started nearest.
    Rig rig = makeRig();
    const ComponentId a = place(rig, "Block1");
    const ComponentId b = place(rig, "Block2", {.rotation = {20_deg, 0_deg, 0_deg}});
    add(rig, "Ground", {.type = MateType::Fixed, .component = a});
    add(rig, "Flat", {.type = MateType::Parallel, .a = plane(a), .b = plane(b)});

    const auto result = solved(rig);
    REQUIRE(result.solved());
    CHECK(result.equations == 2);
    CHECK(result.degreesOfFreedom == 4);
    checkDirection(result.transforms.at(b).apply(Direction3D::unitZ()), 0.0, 0.0, 1.0);
}

TEST_CASE("Solve_ParallelAcceptsEitherSenseAndTheBranchIsNotGuaranteed", "[assembly][solve][p13]") {
    // A real property of this constraint, found by a hand-derived test that
    // expected the near branch and did not get it.
    //
    // Parallel is satisfied in either sense, so its residual is proportional
    // to sin(theta), which is equal at theta and 180 - theta: the equation
    // cannot tell "80 degrees from +Z" from "100 degrees from -Z". Near 90
    // degrees the derivative cos(theta) is also small, so the Newton step is
    // proportional to tan(theta) -- about 325 degrees here -- and the line
    // search backtracks into whichever branch it lands in.
    //
    // So the invariant is that the normals end up parallel, not which way b
    // is facing. Asserting the near branch would be asserting something the
    // solver does not promise and the constraint does not mean.
    Rig rig = makeRig();
    const ComponentId a = place(rig, "Block1");
    const ComponentId b = place(rig, "Block2", {.rotation = {80_deg, 0_deg, 0_deg}});
    add(rig, "Ground", {.type = MateType::Fixed, .component = a});
    add(rig, "Flat", {.type = MateType::Parallel, .a = plane(a), .b = plane(b)});

    const auto result = solved(rig);
    REQUIRE(result.solved());
    const Direction3D nb = result.transforms.at(b).apply(Direction3D::unitZ());
    // Parallel to a's +Z normal, in one sense or the other.
    CHECK_THAT(std::abs(nb.z()), WithinAbs(1.0, kTolerance));
    CHECK_THAT(nb.x(), WithinAbs(0.0, kTolerance));
    CHECK_THAT(nb.y(), WithinAbs(0.0, kTolerance));
}

TEST_CASE("Solve_RecoversAKnownSolutionFromPerturbationsOfSeveralSizes", "[assembly][solve][p13]") {
    // Basin test. The known solution is b coincident with a; perturb it and
    // see how far away the solve still returns to it. Recorded rather than
    // claimed: this says where it does work, not that it always does.
    struct Case {
        std::string name;
        Length shift;
        Angle turn;
        bool recovers = true;
    };
    const std::vector<Case> cases{
        {"tiny", 1_mm, 1_deg, true},   {"small", 10_mm, 10_deg, true},
        {"moderate", 50_mm, 30_deg, true}, {"large", 200_mm, 60_deg, true},
    };
    for (const Case& probe : cases) {
        INFO("perturbation: " << probe.name);
        Rig rig = makeRig();
        const ComponentId a = place(rig, "Block1");
        const ComponentId b = place(rig, "Block2", {.translation = {probe.shift, probe.shift, probe.shift},
                                                    .rotation = {probe.turn, probe.turn, probe.turn}});
        add(rig, "Ground", {.type = MateType::Fixed, .component = a});
        add(rig, "Touch", {.type = MateType::Coincident, .a = plane(a), .b = plane(b)});
        add(rig, "AlignX",
            {.type = MateType::Distance, .a = plane(a, PrincipalPlane::YZ),
             .b = plane(b, PrincipalPlane::YZ), .distance = 0_mm});
        add(rig, "AlignY",
            {.type = MateType::Distance, .a = plane(a, PrincipalPlane::XZ),
             .b = plane(b, PrincipalPlane::XZ), .distance = 0_mm});
        add(rig, "Square",
            {.type = MateType::Perpendicular, .a = plane(a, PrincipalPlane::YZ),
             .b = plane(b, PrincipalPlane::XZ)});

        const auto result = solved(rig);
        INFO(result.message);
        REQUIRE(result.solved());
        CHECK(result.degreesOfFreedom == 0);
        checkPosition(result.transforms.at(b), 0.0, 0.0, 0.0);
        checkDirection(result.transforms.at(b).apply(Direction3D::unitZ()), 0.0, 0.0, 1.0);
    }
}

TEST_CASE("Solve_PerpendicularAndAngleReachTheirStatedDotProduct", "[assembly][solve][p13]") {
    // The invariant is hand-derived from the definition: perpendicular means
    // the normals' dot product is zero, and an angle mate means it is the
    // cosine of that angle. Which way round b turns to get there is not
    // determined by the constraint, so the dot product is what is asserted.
    SECTION("perpendicular") {
        Rig rig = makeRig();
        const ComponentId a = place(rig, "Block1");
        const ComponentId b = place(rig, "Block2", {.rotation = {20_deg, 0_deg, 0_deg}});
        add(rig, "Ground", {.type = MateType::Fixed, .component = a});
        add(rig, "Square", {.type = MateType::Perpendicular, .a = plane(a), .b = plane(b)});

        const auto result = solved(rig);
        REQUIRE(result.solved());
        CHECK(result.equations == 1);
        CHECK(result.degreesOfFreedom == 5);
        const Direction3D nb = result.transforms.at(b).apply(Direction3D::unitZ());
        CHECK_THAT(nb.z(), WithinAbs(0.0, kTolerance)); // dot with a's +Z normal
    }
    SECTION("an angle of 35 degrees") {
        Rig rig = makeRig();
        const ComponentId a = place(rig, "Block1");
        const ComponentId b = place(rig, "Block2", {.rotation = {20_deg, 0_deg, 0_deg}});
        add(rig, "Ground", {.type = MateType::Fixed, .component = a});
        add(rig, "Tilt", {.type = MateType::Angle, .a = plane(a), .b = plane(b), .angle = 35_deg});

        const auto result = solved(rig);
        REQUIRE(result.solved());
        const Direction3D nb = result.transforms.at(b).apply(Direction3D::unitZ());
        CHECK_THAT(nb.z(), WithinAbs(std::cos(35.0 * std::numbers::pi / 180.0), kTolerance));
    }
}

TEST_CASE("Solve_DistanceBetweenAxesIsAnUnsignedSeparation", "[assembly][solve][p13]") {
    // Hand-derived: the axes are parallel to Z, so the separation is the
    // distance in the xy plane. b starts at (30, 40) mm, which is 50 mm away
    // by the 3-4-5 triangle; asking for 10 mm pulls it straight in along the
    // same direction, to (6, 8) mm.
    Rig rig = makeRig();
    const ComponentId a = place(rig, "Block1");
    const ComponentId b = place(rig, "Block2", {.translation = {30_mm, 40_mm, 0_mm}});
    add(rig, "Ground", {.type = MateType::Fixed, .component = a});
    add(rig, "Apart", {.type = MateType::Distance, .a = axis(a), .b = axis(b), .distance = 10_mm});

    const auto result = solved(rig);
    REQUIRE(result.solved());
    CHECK(result.equations == 1);
    checkPosition(result.transforms.at(b), 0.006, 0.008, 0.0);
}

TEST_CASE("Solve_AFullyConstrainedAssemblyHasNoFreedomLeft", "[assembly][solve][p13]") {
    // Six independent equations against six unknowns. Getting there needs
    // care: rotation is three degrees of freedom but a Parallel mate supplies
    // constraints in pairs, so two of them always leave one redundant.
    // Perpendicular supplies exactly one, which is what makes a clean
    // DOF-zero case possible.
    //
    //   Coincident(XY, XY)      2 rotation + 1 translation (z)
    //   Distance(YZ, YZ) = 0    1 translation (x)
    //   Distance(XZ, XZ) = 0    1 translation (y)
    //   Perpendicular(YZ, XZ)   1 rotation (about z)
    Rig rig = makeRig();
    const ComponentId a = place(rig, "Block1");
    const ComponentId b = place(rig, "Block2",
                                {.translation = {5_mm, -4_mm, 6_mm}, .rotation = {8_deg, -5_deg, 10_deg}});
    add(rig, "Ground", {.type = MateType::Fixed, .component = a});
    add(rig, "Touch", {.type = MateType::Coincident, .a = plane(a), .b = plane(b)});
    add(rig, "AlignX",
        {.type = MateType::Distance, .a = plane(a, PrincipalPlane::YZ), .b = plane(b, PrincipalPlane::YZ),
         .distance = 0_mm});
    add(rig, "AlignY",
        {.type = MateType::Distance, .a = plane(a, PrincipalPlane::XZ), .b = plane(b, PrincipalPlane::XZ),
         .distance = 0_mm});
    add(rig, "Square",
        {.type = MateType::Perpendicular, .a = plane(a, PrincipalPlane::YZ),
         .b = plane(b, PrincipalPlane::XZ)});

    const auto result = solved(rig);
    INFO(result.message);
    REQUIRE(result.status == SolveStatus::FullyConstrained);
    CHECK(result.unknowns == 6);
    CHECK(result.equations == 6);
    CHECK(result.degreesOfFreedom == 0);
    CHECK(result.redundant.empty());
    // Everything meets everything, so b ends exactly on a.
    checkPosition(result.transforms.at(b), 0.0, 0.0, 0.0);
    checkDirection(result.transforms.at(b).apply(Direction3D::unitZ()), 0.0, 0.0, 1.0);
    checkDirection(result.transforms.at(b).apply(Direction3D::unitX()), 1.0, 0.0, 0.0);
}

TEST_CASE("Solve_AnAssemblyWithNothingGroundedKeepsItsSixRigidBodyModes", "[assembly][solve][p13]") {
    // ADR-005: an assembly with nothing grounded is free to translate and
    // rotate as a whole, and the solver must report that rather than silently
    // pinning something. Hand-derived: 12 unknowns, a coincidence of rank 3,
    // so 9 -- the 3 the mate leaves plus the 6 the whole assembly keeps.
    Rig rig = makeRig();
    const ComponentId a = place(rig, "Block1");
    const ComponentId b = place(rig, "Block2", {.translation = {0_mm, 0_mm, 50_mm}});
    add(rig, "Touch", {.type = MateType::Coincident, .a = plane(a), .b = plane(b)});

    const auto result = solved(rig);
    REQUIRE(result.solved());
    CHECK(result.unknowns == 12);
    CHECK(result.degreesOfFreedom == 9);
    CHECK(result.status == SolveStatus::UnderConstrained);
    CHECK_THAT(result.message, ContainsSubstring("degree(s) of freedom"));
}

TEST_CASE("Solve_AFreeComponentWithNoMatesHasAllSixDegreesOfFreedom", "[assembly][solve][p13]") {
    Rig rig = makeRig();
    const ComponentId a = place(rig, "Block1", {.translation = {3_mm, 4_mm, 5_mm}});

    const auto result = solved(rig);
    REQUIRE(result.solved());
    CHECK(result.unknowns == 6);
    CHECK(result.equations == 0);
    CHECK(result.degreesOfFreedom == 6);
    // Nothing constrained it, so it is exactly where its placement put it.
    checkPosition(result.transforms.at(a), 0.003, 0.004, 0.005);
}

TEST_CASE("Solve_ContradictoryMatesAreInconsistentAtTheirLeastSquaresCompromise",
          "[assembly][solve][p13]") {
    // Hand-derived, and exactly: the two residuals are zb - 10 mm and
    // zb - 20 mm. Least squares minimises their sum of squares at the
    // midpoint, zb = 15 mm, where each is 5 mm out. So the largest residual
    // is 5 mm, not merely "large".
    Rig rig = makeRig();
    const ComponentId a = place(rig, "Block1");
    const ComponentId b = place(rig, "Block2", {.translation = {0_mm, 0_mm, 12_mm}});
    add(rig, "Ground", {.type = MateType::Fixed, .component = a});
    add(rig, "Near", {.type = MateType::Distance, .a = plane(a), .b = plane(b), .distance = 10_mm});
    add(rig, "Far", {.type = MateType::Distance, .a = plane(a), .b = plane(b), .distance = 20_mm});

    const auto result = solved(rig);
    INFO(result.message);
    CHECK(result.status == SolveStatus::Inconsistent);
    CHECK_FALSE(result.solved());
    CHECK_THAT(result.maxResidual.si(), WithinAbs(0.005, 1e-7));
    // Both mates are named, because both are unsatisfied at the compromise.
    CHECK(result.conflicting.size() == 2);
    // And no transforms: a failed solve produces no partial answer.
    CHECK(result.transforms.empty());
}

TEST_CASE("Solve_ADuplicatedMateIsRedundantRatherThanInconsistent", "[assembly][solve][p13]") {
    // The distinction the gate asks for: redundant-but-consistent is not the
    // same as contradictory. Two identical distance mates can both be
    // satisfied, so the system is over-constrained, not inconsistent, and the
    // second is the one reported.
    Rig rig = makeRig();
    const ComponentId a = place(rig, "Block1");
    const ComponentId b = place(rig, "Block2", {.translation = {0_mm, 0_mm, 60_mm}});
    add(rig, "Ground", {.type = MateType::Fixed, .component = a});
    add(rig, "First", {.type = MateType::Distance, .a = plane(a), .b = plane(b), .distance = 25_mm});
    add(rig, "Second", {.type = MateType::Distance, .a = plane(a), .b = plane(b), .distance = 25_mm});

    const auto result = solved(rig);
    INFO(result.message);
    CHECK(result.status == SolveStatus::OverConstrained);
    CHECK(result.redundant.size() == 1);
    CHECK(result.conflicting.empty());
    // The residual is satisfied even so: this is not a failure to solve.
    CHECK(result.maxResidual.si() < 1e-9);
}

TEST_CASE("Solve_LeavesCanonicalPlacementIntentUntouched", "[assembly][solve][p13]") {
    // The architectural gate. A solve produces derived state and returns it;
    // it never writes back into the document, whatever it finds.
    Rig rig = makeRig();
    const ComponentPlacement intent{.translation = {7_mm, -3_mm, 100_mm}};
    const ComponentId a = place(rig, "Block1");
    const ComponentId b = place(rig, "Block2", intent);
    add(rig, "Ground", {.type = MateType::Fixed, .component = a});
    add(rig, "Gap", {.type = MateType::Distance, .a = plane(a), .b = plane(b), .distance = 25_mm});
    const auto revisionBefore = rig.document.revision();

    const auto result = solved(rig);
    REQUIRE(result.solved());
    // The solved position is 25 mm...
    checkPosition(result.transforms.at(b), 0.007, -0.003, 0.025);
    // ...and the intent still says 100 mm.
    CHECK(assembly::findComponent(rig.document, b)->definition().placement == intent);
    CHECK(rig.document.revision() == revisionBefore);
    // Solving again from the unchanged intent gives the same answer, which it
    // could not if the first solve had quietly moved the starting point.
    const auto again = solved(rig);
    checkPosition(again.transforms.at(b), 0.007, -0.003, 0.025);
    CHECK(again.iterations == result.iterations);
}

TEST_CASE("Solve_IsDeterministic", "[assembly][solve][p13][determinism]") {
    const auto build = [] {
        Rig rig = makeRig();
        const ComponentId a = place(rig, "Block1");
        const ComponentId b = place(rig, "Block2", {.translation = {12_mm, -8_mm, 50_mm},
                                                    .rotation = {11_deg, -7_deg, 23_deg}});
        add(rig, "Ground", {.type = MateType::Fixed, .component = a});
        add(rig, "Touch", {.type = MateType::Coincident, .a = plane(a), .b = plane(b)});
        return rig;
    };
    Rig first = build();
    Rig second = build();
    const auto a = solved(first);
    const auto b = solved(second);

    CHECK(a.status == b.status);
    CHECK(a.iterations == b.iterations);
    CHECK(a.degreesOfFreedom == b.degreesOfFreedom);
    CHECK(a.maxResidual == b.maxResidual);
    REQUIRE(a.transforms.size() == b.transforms.size());
    for (const auto& [id, transform] : a.transforms) {
        // Bit-identical: the same inputs through the same arithmetic in the
        // same order, with nothing seeded and nothing cached.
        CHECK(transform == b.transforms.at(id));
    }
}

TEST_CASE("Solve_RefusesToStartWhenAMateDoesNotResolve", "[assembly][solve][p13]") {
    // ADR-004 keeps "this reference does not resolve" distinct from every
    // other failure, and a solve that never started did not diverge or find
    // the system inconsistent. So this is an error, not a status.
    Rig rig = makeRig();
    const ComponentId a = place(rig, "Block1");
    const ComponentId b = place(rig, "Block2");
    add(rig, "Ground", {.type = MateType::Fixed, .component = a});
    add(rig, "Touch", {.type = MateType::Coincident, .a = plane(a), .b = plane(b)});

    REQUIRE(assembly::removeComponent(rig.document, b).has_value());

    auto result = assembly::solve(rig.document);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ErrorCode::NotFound);
}

TEST_CASE("Solve_RefusesInvalidOptions", "[assembly][solve][p13]") {
    Rig rig = makeRig();
    place(rig, "Block1");

    for (const assembly::SolverOptions options :
         {assembly::SolverOptions{.tolerance = Length::fromSi(0.0)},
          assembly::SolverOptions{.tolerance = Length::fromSi(-1e-9)},
          assembly::SolverOptions{.maxIterations = -1}}) {
        auto result = assembly::solve(rig.document, options);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::InvalidArgument);
    }
}

TEST_CASE("Solve_IsNotDisturbedByTheScaleOfTheModel", "[assembly][solve][p13]") {
    // The angular residuals are scaled by the assembly's characteristic size
    // so the Jacobian does not mix metres with radians. If that scaling were
    // wrong, the solve would behave differently on a small part than a large
    // one. Hand-derived: each case should land at a tenth of its own size.
    for (const auto& [name, size] : std::vector<std::pair<std::string, Length>>{
             {"small", 1_mm}, {"medium", 100_mm}, {"large", 10000_mm}}) {
        INFO("scale: " << name);
        Rig rig = makeRig();
        const ComponentId a = place(rig, "Block1");
        const ComponentId b = place(rig, "Block2", {.translation = {Length::fromSi(size.si()), 0_mm,
                                                                    Length::fromSi(size.si())},
                                                    .rotation = {15_deg, 0_deg, 0_deg}});
        add(rig, "Ground", {.type = MateType::Fixed, .component = a});
        add(rig, "Touch", {.type = MateType::Coincident, .a = plane(a), .b = plane(b)});

        const auto result = solved(rig);
        REQUIRE(result.solved());
        CHECK(result.degreesOfFreedom == 3);
        // The plane is met, and the sliding direction is untouched, whatever
        // the size of the model.
        checkPosition(result.transforms.at(b), size.si(), 0.0, 0.0);
        checkDirection(result.transforms.at(b).apply(Direction3D::unitZ()), 0.0, 0.0, 1.0);
    }
}

TEST_CASE("Solve_RecoversAfterAFailedSolveIsRepaired", "[assembly][solve][p13]") {
    // Failure atomicity has a second half: not only must a failed solve leave
    // nothing behind, the document must still be usable afterwards.
    Rig rig = makeRig();
    const ComponentId a = place(rig, "Block1");
    const ComponentId b = place(rig, "Block2", {.translation = {0_mm, 0_mm, 12_mm}});
    add(rig, "Ground", {.type = MateType::Fixed, .component = a});
    add(rig, "Near", {.type = MateType::Distance, .a = plane(a), .b = plane(b), .distance = 10_mm});
    add(rig, "Far", {.type = MateType::Distance, .a = plane(a), .b = plane(b), .distance = 20_mm});

    const auto failed = solved(rig);
    REQUIRE(failed.status == SolveStatus::Inconsistent);
    CHECK(failed.transforms.empty());

    // Repair it by removing the contradiction, and it solves.
    const auto ids = assembly::mates(rig.document);
    REQUIRE(ids.size() == 3);
    REQUIRE(assembly::removeMate(rig.document, ids.back()).has_value());

    const auto repaired = solved(rig);
    REQUIRE(repaired.solved());
    checkPosition(repaired.transforms.at(b), 0.0, 0.0, 0.010);
}

TEST_CASE("Solve_DoesNotDependOnTheOrderTheMatesWereAdded", "[assembly][solve][p13][determinism]") {
    // The least-squares solution does not depend on the order of the
    // equations, so the transforms must match. What does follow ID order is
    // which of two equivalent mates is called redundant -- the same contract
    // the sketch solver states as "constraints implied by constraints with
    // lower IDs" -- and that is asserted rather than glossed over.
    const auto build = [](bool alignFirst) {
        Rig rig = makeRig();
        const ComponentId a = place(rig, "Block1");
        const ComponentId b = place(rig, "Block2", {.translation = {5_mm, -4_mm, 6_mm},
                                                    .rotation = {8_deg, -5_deg, 10_deg}});
        add(rig, "Ground", {.type = MateType::Fixed, .component = a});
        if (alignFirst) {
            add(rig, "AlignX", {.type = MateType::Distance, .a = plane(a, PrincipalPlane::YZ),
                                .b = plane(b, PrincipalPlane::YZ), .distance = 0_mm});
            add(rig, "Touch", {.type = MateType::Coincident, .a = plane(a), .b = plane(b)});
        } else {
            add(rig, "Touch", {.type = MateType::Coincident, .a = plane(a), .b = plane(b)});
            add(rig, "AlignX", {.type = MateType::Distance, .a = plane(a, PrincipalPlane::YZ),
                                .b = plane(b, PrincipalPlane::YZ), .distance = 0_mm});
        }
        return std::pair{std::move(rig), b};
    };
    auto [first, b1] = build(true);
    auto [second, b2] = build(false);
    const auto x = solved(first);
    const auto y = solved(second);

    REQUIRE(x.solved());
    REQUIRE(y.solved());
    CHECK(x.degreesOfFreedom == y.degreesOfFreedom);
    const Point3D px = x.transforms.at(b1).apply(Point3D{});
    const Point3D py = y.transforms.at(b2).apply(Point3D{});
    CHECK_THAT(px.x.si(), WithinAbs(py.x.si(), kTolerance));
    CHECK_THAT(px.y.si(), WithinAbs(py.y.si(), kTolerance));
    CHECK_THAT(px.z.si(), WithinAbs(py.z.si(), kTolerance));
}

TEST_CASE("Solve_NeverProducesANonFiniteTransform", "[assembly][solve][p13]") {
    // Mate values are validated finite when they are set, so a NaN cannot be
    // put in. This checks none is manufactured on the way out either, over
    // every kind of solve including the ones that fail.
    const auto finite = [](const RigidTransform3D& transform) {
        for (const double entry : transform.matrix()) {
            if (!std::isfinite(entry)) {
                return false;
            }
        }
        const Translation3D& t = transform.translationPart();
        return std::isfinite(t.x.si()) && std::isfinite(t.y.si()) && std::isfinite(t.z.si());
    };

    Rig rig = makeRig();
    const ComponentId a = place(rig, "Block1");
    const ComponentId b = place(rig, "Block2", {.translation = {12_mm, -8_mm, 50_mm},
                                                .rotation = {80_deg, 40_deg, -20_deg}});
    add(rig, "Ground", {.type = MateType::Fixed, .component = a});
    add(rig, "Touch", {.type = MateType::Coincident, .a = plane(a), .b = plane(b)});
    add(rig, "Bore", {.type = MateType::Concentric, .a = axis(a), .b = axis(b)});

    const auto result = solved(rig);
    for (const auto& [id, transform] : result.transforms) {
        INFO("component " << id);
        CHECK(finite(transform));
    }
    CHECK(std::isfinite(result.maxResidual.si()));

    // An inconsistent system is where a NaN would plausibly appear: the solve
    // settles on a least-squares compromise it can never drive to zero, so the
    // residual is the one number that keeps being recomputed after the
    // iteration stops making progress.
    Rig contradictory = makeRig();
    const ComponentId c = place(contradictory, "Block1");
    const ComponentId d = place(contradictory, "Block2");
    add(contradictory, "Ground", {.type = MateType::Fixed, .component = c});
    add(contradictory, "Near", {.type = MateType::Distance, .a = plane(c), .b = plane(d), .distance = 10_mm});
    add(contradictory, "Far", {.type = MateType::Distance, .a = plane(c), .b = plane(d), .distance = 20_mm});
    const auto unsatisfiable = solved(contradictory);
    REQUIRE(unsatisfiable.status == SolveStatus::Inconsistent);
    CHECK(std::isfinite(unsatisfiable.maxResidual.si()));
    // A solve that did not succeed returns no transforms at all, so there is
    // nothing partial for a caller to mistake for an answer.
    CHECK(unsatisfiable.transforms.empty());

    // And the other failure: a start sitting exactly on a residual's
    // stationary point, where the gradient is zero and no direction is better
    // than any other. 0/0 lives here if anywhere does.
    Rig stationary = makeRig();
    const ComponentId e = place(stationary, "Block1");
    const ComponentId f = place(stationary, "Block2");
    add(stationary, "Ground", {.type = MateType::Fixed, .component = e});
    add(stationary, "Square", {.type = MateType::Perpendicular, .a = plane(e), .b = plane(f)});
    const auto stuck = solved(stationary);
    CHECK(std::isfinite(stuck.maxResidual.si()));
    CHECK(stuck.transforms.empty());
    // Characterisation, not endorsement. Both normals are +Z, so the residual
    // sits at its maximum with an identically zero gradient: the solver
    // stalls, sees no gradient, and reports Inconsistent -- "the mates cannot
    // all be satisfied". They can: a 90-degree turn satisfies them. The
    // classification is local and cannot tell "no solution" from "no solution
    // reachable from here". The sketch solver's branch is this one line for
    // line, so the same blind spot follows from the same code -- read, not
    // measured, since P12 is qualified and out of scope here. Pinned so that
    // changing it is a deliberate change to both rather than drift in one.
    // See docs/verification/P13-SOLVE-001/ -- known limitations.
    CHECK(stuck.status == SolveStatus::Inconsistent);
}

TEST_CASE("Solve_PerformanceBaselineOverGrowingAssemblies", "[assembly][solve][p13][performance]") {
    // Measured, not optimized. The numbers are recorded in the evidence; what
    // this asserts is only that each size solves and classifies correctly, so
    // the test cannot fail because a machine was busy.
    for (const int count : {2, 5, 10, 20}) {
        Rig rig = makeRig();
        std::vector<ComponentId> ids;
        ids.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            ids.push_back(place(rig, std::format("Block{}", i + 1),
                                {.translation = {Length::fromSi(0.01 * i), Length::fromSi(0.002 * i),
                                                 Length::fromSi(0.05 + 0.01 * i)},
                                 .rotation = {Angle::fromSi(0.05 * i), 0_deg, 0_deg}}));
        }
        add(rig, "Ground", {.type = MateType::Fixed, .component = ids.front()});
        // A chain: each component coincident with the one before it.
        for (std::size_t i = 1; i < ids.size(); ++i) {
            add(rig, std::format("Touch{}", i),
                {.type = MateType::Coincident, .a = plane(ids[i - 1]), .b = plane(ids[i])});
        }

        const auto start = std::chrono::steady_clock::now();
        const auto result = solved(rig);
        const auto elapsed = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - start)
                                 .count();

        INFO("components " << count << ", equations " << result.equations << ", unknowns "
                           << result.unknowns << ", DOF " << result.degreesOfFreedom << ", iterations "
                           << result.iterations << ", " << elapsed << " ms");
        REQUIRE(result.solved());
        // A chain of n coincident planes: 6(n-1) unknowns, 3(n-1) equations,
        // and each link leaves three ways to move.
        CHECK(result.unknowns == 6 * (static_cast<std::size_t>(count) - 1));
        CHECK(result.equations == 3 * (static_cast<std::size_t>(count) - 1));
        CHECK(result.degreesOfFreedom == 3 * (static_cast<std::size_t>(count) - 1));
    }
}

TEST_CASE("Solve_AViolatedMateBetweenTwoGroundedComponentsIsInconsistent", "[assembly][solve][p13]") {
    // Nothing can move, so nothing can satisfy it. The honest answer is that
    // the mates cannot all hold, not a solver that spins to its iteration
    // limit looking for a freedom that does not exist.
    Rig rig = makeRig();
    const ComponentId a = place(rig, "Block1");
    const ComponentId b = place(rig, "Block2", {.translation = {0_mm, 0_mm, 40_mm}});
    add(rig, "GroundA", {.type = MateType::Fixed, .component = a});
    add(rig, "GroundB", {.type = MateType::Fixed, .component = b});
    add(rig, "Gap", {.type = MateType::Distance, .a = plane(a), .b = plane(b), .distance = 10_mm});

    const auto result = solved(rig);
    INFO(result.message);
    CHECK(result.unknowns == 0);
    CHECK(result.status == SolveStatus::Inconsistent);
    // 40 mm apart, asked for 10: the residual is the 30 mm nothing can close.
    CHECK_THAT(result.maxResidual.si(), WithinAbs(0.030, 1e-9));
    CHECK(result.transforms.empty());
    CHECK(result.iterations == 0);
}

TEST_CASE("Solve_IgnoresSuppressedMates", "[assembly][solve][p13]") {
    // Suppression is engineering intent: the mate keeps its identity and its
    // targets, and the solve behaves as though it were not there.
    Rig rig = makeRig();
    const ComponentId a = place(rig, "Block1");
    const ComponentId b = place(rig, "Block2", {.translation = {0_mm, 0_mm, 80_mm}});
    add(rig, "Ground", {.type = MateType::Fixed, .component = a});
    add(rig, "Gap", {.type = MateType::Distance, .a = plane(a), .b = plane(b), .distance = 25_mm,
                     .suppressed = true});

    const auto result = solved(rig);
    REQUIRE(result.solved());
    CHECK(result.equations == 0);
    CHECK(result.degreesOfFreedom == 6);
    // Untouched, at the placement its intent gave it.
    checkPosition(result.transforms.at(b), 0.0, 0.0, 0.080);
    // And the mate is still there, still saying what it said.
    CHECK(assembly::mates(rig.document).size() == 2);
}

TEST_CASE("Solve_ASuppressedFixedMateDoesNotGround", "[assembly][solve][p13]") {
    // The case suppression could quietly get wrong: if a suppressed Fixed
    // mate still grounded its component, the assembly would silently have six
    // fewer ways to move than the model says.
    Rig rig = makeRig();
    const ComponentId a = place(rig, "Block1");
    const ComponentId b = place(rig, "Block2", {.translation = {0_mm, 0_mm, 50_mm}});
    add(rig, "Ground", {.type = MateType::Fixed, .component = a, .suppressed = true});
    add(rig, "Touch", {.type = MateType::Coincident, .a = plane(a), .b = plane(b)});

    const auto result = solved(rig);
    REQUIRE(result.solved());
    // Both components are free, so twelve unknowns rather than six.
    CHECK(result.unknowns == 12);
    CHECK(result.degreesOfFreedom == 9);
}

// ADR-005 states what verifying "placement is intent, transforms are derived"
// looks like, and the three cases below are the ones it names that the rest of
// this file does not already cover: a saved and reloaded assembly solves to
// the same transforms as one built in memory; no transform reaches the .bcad
// file; and a parameter change moves a component, with the transform returning
// when the parameter is restored.

TEST_CASE("Solve_ASavedAndReloadedAssemblySolvesToTheSameTransforms", "[assembly][solve][p13][io]") {
    TempDir dir;
    Rig rig = makeRig();
    const ComponentId a = place(rig, "Block1", {.translation = {5_mm, 0_mm, 0_mm}});
    const ComponentId b = place(rig, "Block2", {.translation = {12_mm, -8_mm, 90_mm},
                                                .rotation = {13_deg, -9_deg, 21_deg}});
    add(rig, "Ground", {.type = MateType::Fixed, .component = a});
    add(rig, "Touch", {.type = MateType::Coincident, .a = plane(a), .b = plane(b)});
    add(rig, "Side", {.type = MateType::Distance, .a = plane(a, PrincipalPlane::YZ),
                      .b = plane(b, PrincipalPlane::YZ), .distance = 30_mm});
    const auto before = solved(rig);
    REQUIRE(before.solved());
    // Hand-derived: a is grounded at x = 5 mm, so its YZ plane is x = 5 mm with
    // normal +X, and the Distance mate puts b's origin 30 mm along it.
    checkPosition(before.transforms.at(b), 0.035, -0.008, 0.0);

    const auto path = dir.path() / "assembly.bcad";
    REQUIRE(io::saveDocument(rig.document, path).has_value());
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());

    auto after = assembly::solve(*loaded);
    REQUIRE(after.has_value());
    CHECK(after->status == before.status);
    CHECK(after->iterations == before.iterations);
    CHECK(after->degreesOfFreedom == before.degreesOfFreedom);
    CHECK(after->unknowns == before.unknowns);
    CHECK(after->equations == before.equations);
    CHECK(after->maxResidual == before.maxResidual);
    REQUIRE(after->transforms.size() == before.transforms.size());
    for (const auto& [id, transform] : before.transforms) {
        INFO("component: " << id.value());
        REQUIRE(after->transforms.contains(id));
        // Bit-identical, not merely close. The file carries intent and the
        // solve starts from intent alone, so a round trip cannot change the
        // arithmetic -- unless something derived had been written into it.
        CHECK(after->transforms.at(id) == transform);
    }
}

TEST_CASE("Solve_WritesNothingDerivedIntoTheSavedFile", "[assembly][solve][p13][io]") {
    TempDir dir;
    Rig rig = makeRig();
    const ComponentPlacement intent{.translation = {7_mm, -3_mm, 100_mm}};
    const ComponentId a = place(rig, "Block1");
    const ComponentId b = place(rig, "Block2", intent);
    add(rig, "Ground", {.type = MateType::Fixed, .component = a});
    add(rig, "Gap", {.type = MateType::Distance, .a = plane(a), .b = plane(b), .distance = 25_mm});

    // Saved before any solve, this is what intent alone looks like on disk.
    const auto first = dir.path() / "intent.bcad";
    REQUIRE(io::saveDocument(rig.document, first).has_value());
    const std::size_t objectsBefore = rig.document.objectCount();

    const auto result = solved(rig);
    REQUIRE(result.solved());
    // The solve moves b by 75 mm, so there is something derived to leak.
    checkPosition(result.transforms.at(b), 0.007, -0.003, 0.025);

    const auto second = dir.path() / "after-solving.bcad";
    REQUIRE(io::saveDocument(rig.document, second).has_value());
    // Byte-identical. A search for a number would prove less: this says the
    // file has not changed at all, in any key, whether or not a solve ran.
    CHECK(readFile(second) == readFile(first));
    CHECK(rig.document.objectCount() == objectsBefore);

    auto reloaded = io::loadDocument(second);
    REQUIRE(reloaded.has_value());
    // And what came back is the 100 mm the engineer asked for, not the 25 mm
    // the solver worked out.
    const assembly::Component* component = assembly::findComponent(*reloaded, b);
    REQUIRE(component != nullptr);
    CHECK(component->definition().placement == intent);
}

TEST_CASE("Solve_FollowsAParameterChangeAndReturnsWhenItIsRestored", "[assembly][solve][p13][param]") {
    Rig rig = makeRig();
    const ParameterId offset = require(rig.document.createParameter("offset", 25_mm, units::mm));
    const ComponentId a = place(rig, "Block1");
    const ComponentId b = place(rig, "Block2",
                                {.translation = {0_mm, 0_mm, 40_mm},
                                 .translationParameters = {offset, std::nullopt, std::nullopt}});
    add(rig, "Ground", {.type = MateType::Fixed, .component = a});
    add(rig, "Touch", {.type = MateType::Coincident, .a = plane(a), .b = plane(b)});

    // Coincident planes leave x, y and the spin about z free, so x is not
    // constrained by the mate and stays where the parameter puts it, while z
    // is driven to 0 by the mate.
    const auto at25 = solved(rig);
    REQUIRE(at25.degreesOfFreedom == 3);
    checkPosition(at25.transforms.at(b), 0.025, 0.0, 0.0);

    REQUIRE(rig.document.setParameterValue(offset, 60_mm).has_value());
    const auto at60 = solved(rig);
    checkPosition(at60.transforms.at(b), 0.060, 0.0, 0.0);

    REQUIRE(rig.document.setParameterValue(offset, 25_mm).has_value());
    const auto restored = solved(rig);
    // Bit-identical to the first solve: nothing was cached, so restoring the
    // parameter restores the transform exactly rather than approximately.
    CHECK(restored.transforms.at(b) == at25.transforms.at(b));
    CHECK(restored.iterations == at25.iterations);
    CHECK(assembly::findComponent(rig.document, b)->definition().placement.translation[0] == 0_mm);
}
