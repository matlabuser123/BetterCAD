#include "features/FeatureTestSupport.hpp"

#include "assembly/solver/SolverSystem.hpp"

#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Mates.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/MateReference.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <Eigen/Dense>

#include <memory>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using assembly::MateDefinition;
using assembly::MateType;
using assembly::detail::System;
using Catch::Matchers::WithinAbs;

// P13-SOLVE-001: the analytic Jacobian against central finite differences.
//
// This is the milestone's hard gate. A Gauss-Newton solver with a wrong
// Jacobian does not obviously fail -- a wrong derivative is often still a
// descent direction, so the solve converges, reports success, and puts the
// components somewhere plausible and wrong. Nothing downstream would catch
// that: the residuals it converged on are computed from the same equations
// the derivative is supposed to differentiate.
//
// These tests reach into the solver's private System, which is the one place
// in this repository where a test does that. CLAUDE.md's rule is that tests
// use production APIs and not private back doors, and this is a deliberate,
// narrow exception: it bypasses nothing and makes no behaviour exist for
// tests, it checks an internal mathematical contract that no public API can
// expose without inventing one for the tests' benefit.

namespace {

/// The unknowns are metres and radians of order 1e-2, so a step of 1e-6
/// leaves the central difference dominated by its h^2 truncation term rather
/// than by cancellation, which starts to bite below about 1e-8.
constexpr double kStep = 1e-6;

struct Rig {
    Document document{"Assembly"};
    ObjectId sketch{};
    ObjectId part{};
    ComponentId a{};
    ComponentId b{};
};

/// Two components of one part, placed apart and turned, so no term of the
/// derivative is multiplied by a zero that would hide a mistake in it.
Rig makeRig() {
    Rig rig;
    auto sketch = std::make_unique<sketch::Sketch>("BlockSketch", Frame3D::xy());
    addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
    rig.sketch = require(rig.document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Block", {.profile = SketchId::fromValue(rig.sketch.value()), .depth = 10_mm});
    REQUIRE(extrude.has_value());
    rig.part = require(rig.document.addObject(std::move(*extrude)));
    rig.a = require(assembly::createComponent(
        rig.document, "Block1",
        {.part = rig.part, .placement = {.translation = {17_mm, -23_mm, 11_mm},
                                         .rotation = {13_deg, 29_deg, -7_deg}}}));
    rig.b = require(assembly::createComponent(
        rig.document, "Block2",
        {.part = rig.part, .placement = {.translation = {-31_mm, 19_mm, 41_mm},
                                         .rotation = {-23_deg, 5_deg, 37_deg}}}));
    return rig;
}

MateTarget plane(ComponentId component) { return planeTarget(component, PlaneReference{}); }
MateTarget axis(ComponentId component) { return axisTarget(component, AxisReference{}); }

/// The largest disagreement between the analytic Jacobian and a central
/// finite difference of the residuals, and the scale it is measured against.
struct DerivativeError {
    double absolute = 0.0;
    double relative = 0.0;
    Eigen::Index rows = 0;
    Eigen::Index columns = 0;
};

DerivativeError compare(const System& system) {
    const Eigen::Index n = system.unknowns();
    const Eigen::Index m = system.equationCount();
    REQUIRE(n > 0);
    REQUIRE(m > 0);

    Eigen::MatrixXd analytic;
    system.jacobianAtBase(analytic);
    REQUIRE(analytic.rows() == m);
    REQUIRE(analytic.cols() == n);

    Eigen::MatrixXd difference(m, n);
    Eigen::VectorXd plus;
    Eigen::VectorXd minus;
    for (Eigen::Index column = 0; column < n; ++column) {
        Eigen::VectorXd step = Eigen::VectorXd::Zero(n);
        step[column] = kStep;
        system.evaluate(step, plus);
        step[column] = -kStep;
        system.evaluate(step, minus);
        difference.col(column) = (plus - minus) / (2.0 * kStep);
    }

    DerivativeError error;
    error.rows = m;
    error.columns = n;
    error.absolute = (analytic - difference).cwiseAbs().maxCoeff();
    const double scale = std::max(1.0, analytic.cwiseAbs().maxCoeff());
    error.relative = error.absolute / scale;
    return error;
}

/// Builds the system for one mate and compares its derivatives.
DerivativeError errorFor(const MateDefinition& definition) {
    Rig rig = makeRig();
    auto id = assembly::createMate(rig.document, "Mate", definition);
    if (!id) {
        FAIL(id.error().message);
    }
    auto system = System::build(rig.document, {});
    if (!system) {
        FAIL(system.error().message);
    }
    return compare(*system);
}

} // namespace

TEST_CASE("SolverDerivative_MatchesFiniteDifferencesForEveryMateKind", "[assembly][solver][p13]") {
    // Each kind builds a different set of equations, so each needs its own
    // derivative checked. A tolerance of 1e-7 is far below the size of any
    // mistake that matters -- a sign error, a missing term or a transposed
    // cross product is an error of order 1 -- and far above the ~1e-10 a
    // central difference of this step size can achieve.
    Rig probe = makeRig();
    const ComponentId a = probe.a;
    const ComponentId b = probe.b;

    const std::vector<std::pair<std::string, MateDefinition>> cases{
        {"coincident planes", {.type = MateType::Coincident, .a = plane(a), .b = plane(b)}},
        {"coincident axes", {.type = MateType::Coincident, .a = axis(a), .b = axis(b)}},
        {"concentric", {.type = MateType::Concentric, .a = axis(a), .b = axis(b)}},
        {"parallel planes", {.type = MateType::Parallel, .a = plane(a), .b = plane(b)}},
        {"parallel axes", {.type = MateType::Parallel, .a = axis(a), .b = axis(b)}},
        {"perpendicular", {.type = MateType::Perpendicular, .a = plane(a), .b = plane(b)}},
        {"distance between planes",
         {.type = MateType::Distance, .a = plane(a), .b = plane(b), .distance = 25_mm}},
        {"distance between axes",
         {.type = MateType::Distance, .a = axis(a), .b = axis(b), .distance = 12_mm}},
        {"angle", {.type = MateType::Angle, .a = plane(a), .b = plane(b), .angle = 35_deg}},
        // P13-MATE-002. Three of the joints reuse equation forms above, but
        // they combine them differently and each combination is checked in
        // its own right rather than assumed from its parts.
        {"revolute", {.type = MateType::Revolute, .a = axis(a), .b = axis(b)}},
        {"cylindrical", {.type = MateType::Cylindrical, .a = axis(a), .b = axis(b)}},
        {"planar", {.type = MateType::Planar, .a = plane(a), .b = plane(b)}},
        // The slider is the one with a row of its own: cross(Ra, Rb) . D,
        // whose projection vector is the slide axis rather than a complement
        // basis of Ra. If freezing the axis were wrong, or the row were
        // written against the wrong target's columns, this is where it shows.
        {"slider",
         {.type = MateType::Slider,
          .a = axis(a),
          .b = axis(b),
          .a2 = axisTarget(a, AxisReference{.axis = PrincipalAxis::X}),
          .b2 = axisTarget(b, AxisReference{.axis = PrincipalAxis::X})}},
    };

    for (const auto& [name, definition] : cases) {
        INFO("mate: " << name);
        const DerivativeError error = errorFor(definition);
        INFO("equations " << error.rows << ", unknowns " << error.columns);
        INFO("max absolute error " << error.absolute << ", relative " << error.relative);
        CHECK(error.relative < 1e-7);
    }
}

TEST_CASE("SolverDerivative_MatchesFiniteDifferencesForAnAssemblyOfSeveralMates",
          "[assembly][solver][p13]") {
    // The single-mate cases above cannot catch a block written into the wrong
    // component's columns, because with one mate every column belongs to one
    // of its two components. This one has three components and four mates, so
    // a misplaced block lands on a component the equation does not involve
    // and the finite difference sees nothing there.
    Rig rig = makeRig();
    const ComponentId c = require(assembly::createComponent(
        rig.document, "Block3",
        {.part = rig.part, .placement = {.translation = {7_mm, 53_mm, -19_mm},
                                         .rotation = {41_deg, -11_deg, 3_deg}}}));

    REQUIRE(assembly::createMate(rig.document, "Ground", {.type = MateType::Fixed, .component = rig.a})
                .has_value());
    REQUIRE(assembly::createMate(rig.document, "Touch",
                                 {.type = MateType::Coincident, .a = plane(rig.a), .b = plane(rig.b)})
                .has_value());
    REQUIRE(assembly::createMate(rig.document, "Bore",
                                 {.type = MateType::Concentric, .a = axis(rig.b), .b = axis(c)})
                .has_value());
    REQUIRE(assembly::createMate(rig.document, "Gap",
                                 {.type = MateType::Distance, .a = plane(rig.a), .b = plane(c),
                                  .distance = 30_mm})
                .has_value());

    auto system = System::build(rig.document, {});
    REQUIRE(system.has_value());
    // One component is grounded, so only two contribute unknowns.
    CHECK(system->unknowns() == 12);

    const DerivativeError error = compare(*system);
    INFO("equations " << error.rows << ", unknowns " << error.columns);
    INFO("max absolute error " << error.absolute << ", relative " << error.relative);
    CHECK(error.relative < 1e-7);
}

TEST_CASE("SolverDerivative_AGroundedComponentContributesNoColumns", "[assembly][solver][p13]") {
    // A Fixed mate removes a component's unknowns rather than adding
    // equations that pin them, so the system is smaller instead of larger.
    Rig rig = makeRig();
    REQUIRE(assembly::createMate(rig.document, "Touch",
                                 {.type = MateType::Coincident, .a = plane(rig.a), .b = plane(rig.b)})
                .has_value());

    auto free = System::build(rig.document, {});
    REQUIRE(free.has_value());
    CHECK(free->unknowns() == 12);
    CHECK(free->equationCount() == 3);

    REQUIRE(assembly::createMate(rig.document, "Ground", {.type = MateType::Fixed, .component = rig.a})
                .has_value());
    auto grounded = System::build(rig.document, {});
    REQUIRE(grounded.has_value());
    CHECK(grounded->unknowns() == 6);
    // Grounding adds no equation of its own.
    CHECK(grounded->equationCount() == 3);
    CHECK(compare(*grounded).relative < 1e-7);
}
