// P18-POST-001: the recovery of grad(T), q = -k grad(T) and |q|, hand-checked.
//
// WHAT THESE TESTS ARE FOR. The recovery is a GATHER (which temperature belongs to
// which corner), a PRODUCT (grad(T) = G Te, P18-ELEM's) and ONE LINE OF PHYSICS (the
// minus sign and the conductivity), and the claims are about each of those:
//
//   - the gather follows the element's own node order, through the numbering, never
//     through a sorted list or a raw NodeId;
//   - the components are [x, y, z] and the three axis fields prove it, together with
//     an affine field of mixed signs that no swap or transpose survives;
//   - q = -k grad(T): the sign (heat flows toward lower temperature), the conductivity
//     exactly once, the Euclidean magnitude;
//   - only temperature DIFFERENCES matter, and a constant offset does not move a flux --
//     which is why the nodal temperature is checked on its own, in kelvin;
//   - geometry scaling, translation and proper rotation transport the field correctly;
//   - an invalid tetrahedron, a non-finite temperature or an overflow fails the WHOLE
//     recovery, and nothing is skipped.
//
// EVERY EXPECTED VALUE IS TYPED IN FROM A HAND DERIVATION, comes from the known
// coefficients of an affine field, or from the test's own dense 4 x 4 solve
// (ThermalPostTestSupport.hpp). Nothing here asks the production recovery, or
// P18-ELEM's G, for the answer it is judged against.
//
// The real-mesh, currentness and transient claims are in ThermalPostMappedTests.cpp.

#include "ThermalPostTestSupport.hpp"

#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <bit>
#include <format>
#include <limits>
#include <set>

using namespace pstest;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr double kA = 307.3;   // K
constexpr double kB = 7.2;     // K/m
constexpr double kC = -3.5;    // K/m
constexpr double kD = 11.4;    // K/m
constexpr double kK = 5.0;     // W/(m K)

/// The largest difference between every recovered gradient and the single expected one.
[[nodiscard]] double worstGradientError(const ThermalElementFields& fields, const Vec3& expected) {
    double worst = 0.0;
    for (const thermal::TemperatureGradient3& g : fields.gradients) {
        worst = std::max(worst, maxDifference(vec(g), expected));
    }
    return worst;
}
[[nodiscard]] double worstFluxError(const ThermalElementFields& fields, const Vec3& expected) {
    double worst = 0.0;
    for (const thermal::HeatFlux3& q : fields.heatFluxes) {
        worst = std::max(worst, maxDifference(vec(q), expected));
    }
    return worst;
}

/// One tetrahedron with the given corner positions (in this order) and nodal handles, and its numbering.
[[nodiscard]] AsmHand singleTet(const std::array<Vec3, 4>& corners, const std::array<std::uint32_t, 4>& handles) {
    // The builder wants ascending handles; the tetrahedron then names them in the order asked for.
    std::vector<std::pair<std::uint32_t, Vec3>> byHandle;
    for (std::size_t i = 0; i < 4; ++i) {
        byHandle.emplace_back(handles[i], corners[i]);
    }
    std::sort(byHandle.begin(), byHandle.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    Nodes nodes;
    for (const auto& [handle, position] : byHandle) {
        nodes.emplace_back(handle, at(position[0], position[1], position[2]));
    }
    std::array<std::size_t, 4> local{};
    for (std::size_t i = 0; i < 4; ++i) {
        for (std::size_t n = 0; n < byHandle.size(); ++n) {
            if (byHandle[n].first == handles[i]) {
                local[i] = n;
            }
        }
    }
    return makeAsmHand(nodes, {}, {local});
}

[[nodiscard]] AsmHand unitTet(double scale = 1.0) {
    return singleTet({{{0, 0, 0}, {scale, 0, 0}, {0, scale, 0}, {0, 0, scale}}}, {1, 2, 3, 4});
}

} // namespace

// ===========================================================================
// The temperature
// ===========================================================================

TEST_CASE("ThermalPost_ConstantTemperatureZeroGradient", "[thermal][post][constant]") {
    // T = 325.7 K everywhere -- NOT zero, so an absolute-temperature term would show.
    const AsmHand hand = makeGrid(GridSpec{.side = 4, .h = 0.5});
    const std::vector<Temperature> t(hand.mesh.nodes().size(), Temperature::fromSi(325.7));
    const ThermalElementFields fields = mustRecover(hand, kK, t);
    REQUIRE(fields.gradients.size() == hand.mesh.tetrahedra().size());
    double gradient = 0.0;
    double flux = 0.0;
    double magnitude = 0.0;
    for (std::size_t e = 0; e < fields.gradients.size(); ++e) {
        gradient = std::max(gradient, norm(vec(fields.gradients[e])));
        flux = std::max(flux, norm(vec(fields.heatFluxes[e])));
        magnitude = std::max(magnitude, fields.heatFluxMagnitudes[e].si());
    }
    CHECK(gradient <= 1e-11);
    CHECK(flux <= 1e-10);
    CHECK(magnitude <= 1e-10);
    WARN(std::format("constant field T = 325.7 K over {} jittered tetrahedra: max |grad| {:.2e} K/m, max |q| {:.2e} W/m2, max |q| field {:.2e}",
                     fields.gradients.size(), gradient, flux, magnitude));
}

TEST_CASE("ThermalPost_NodalTemperatureRemainsKelvinAndFollowsTheSolver", "[thermal][post][temperature][kelvin]") {
    // A slab solved on a hand mesh: 350 K and 280 K prescribed. The recovered nodal temperature is the solver's value, bit
    // for bit, and it is in KELVIN -- a Celsius conversion would leave every gradient and flux unchanged, so this is checked
    // here on its own and not through a flux.
    const AsmHand hand = makeGrid(GridSpec{.side = 3, .h = 0.5, .jitter = 0.1});
    const ThermalSystem system = assemble(hand, 45.0, std::nullopt, noHeat(hand));
    std::vector<thermal::ThermalTemperatureConstraint> list;
    for (std::size_t n = 0; n < hand.mesh.nodes().size(); ++n) {
        const double z = hand.mesh.nodes()[n].position.z.si();
        if (z == 0.0) {
            list.push_back({dofIx(n), Temperature::fromSi(350.0)});
        } else if (z == 1.0) {
            list.push_back({dofIx(n), Temperature::fromSi(280.0)});
        }
    }
    const auto constraints = thermal::buildThermalConstraintMap(hand.dofs, list);
    REQUIRE(constraints.has_value());
    const auto solved = thermal::solveSteadyThermalSystem(system, hand.dofs, *constraints, ThermalSolverSettings{});
    INFO((solved.has_value() ? std::string{} : solved.error().error.message));
    REQUIRE(solved.has_value());
    const auto post = thermal::recoverSteadyFromSolution(hand.mesh, hand.dofs, ThermalConductivity::fromSi(45.0), *solved);
    INFO((post.has_value() ? std::string{} : post.error().error.message));
    REQUIRE(post.has_value());

    REQUIRE(post->finalState().temperatures.size() == solved->dofCount());
    for (std::size_t d = 0; d < solved->dofCount(); ++d) {
        CHECK(std::bit_cast<std::uint64_t>(post->finalState().temperatures[d].si()) ==
              std::bit_cast<std::uint64_t>(solved->temperatures()[d].si()));
    }
    // 350 K stays 350 K, not 76.85 (Celsius), not 623.15 (an offset the other way).
    bool sawHot = false;
    bool sawCold = false;
    for (const Temperature t : post->finalState().temperatures) {
        sawHot = sawHot || t.si() == 350.0;
        sawCold = sawCold || t.si() == 280.0;
        CHECK(t.si() >= 280.0 - 1e-9);
        CHECK(t.si() <= 350.0 + 1e-9);
    }
    CHECK(sawHot);
    CHECK(sawCold);
    CHECK_FALSE(post->finalState().time.has_value());
    CHECK(post->stateCount() == 1);
    CHECK(post->mode() == thermal::ThermalAnalysisMode::Steady);
    WARN("recovered nodal temperature: source 350 K and 280 K, recovered 350 K and 280 K (not 76.85 / 6.85), bitwise equal to the solver's vector");
}

TEST_CASE("ThermalPost_NodalTemperatureUsesThermalDofOrderForSparseNodeIds", "[thermal][post][temperature][sparse-ids]") {
    // Node handles 104, 7, 9001, 42 with positions at the corners of the unit tetrahedron, the element naming them in the
    // order [104, 7, 9001, 42]. The builder numbers them ascending: unknowns 0..3 are handles 7, 42, 104, 9001.
    //   T(104) = 301 at (0,0,0)   T(7) = 315 at (1,0,0)   T(9001) = 287 at (0,1,0)   T(42) = 349 at (0,0,1)
    // so grad T = (315-301, 287-301, 349-301) = (14, -14, 48) K/m, and with k = 5, q = (-70, 70, -240) W/m2.
    const AsmHand hand = singleTet({{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}, {104, 7, 9001, 42});
    REQUIRE(hand.dofs.dofCount() == 4);
    REQUIRE(hand.dofs.nodes()[0] == NodeId::fromValue(7));
    REQUIRE(hand.dofs.nodes()[1] == NodeId::fromValue(42));
    REQUIRE(hand.dofs.nodes()[2] == NodeId::fromValue(104));
    REQUIRE(hand.dofs.nodes()[3] == NodeId::fromValue(9001));
    const std::vector<Temperature> t{Temperature::fromSi(315.0), Temperature::fromSi(349.0), Temperature::fromSi(301.0),
                                     Temperature::fromSi(287.0)};  // in ThermalDofIndex order
    const ThermalElementFields fields = mustRecover(hand, kK, t);
    REQUIRE(fields.gradients.size() == 1);
    CHECK_THAT(fields.gradients[0].x.si(), WithinAbs(14.0, 1e-12));
    CHECK_THAT(fields.gradients[0].y.si(), WithinAbs(-14.0, 1e-12));
    CHECK_THAT(fields.gradients[0].z.si(), WithinAbs(48.0, 1e-12));
    CHECK_THAT(fields.heatFluxes[0].x.si(), WithinAbs(-70.0, 1e-11));
    CHECK_THAT(fields.heatFluxes[0].y.si(), WithinAbs(70.0, 1e-11));
    CHECK_THAT(fields.heatFluxes[0].z.si(), WithinAbs(-240.0, 1e-11));
    // The array is the size of the node COUNT, never of the largest handle.
    CHECK(t.size() == 4);

    // The same physical tetrahedron named in another (even) local order has the same gradient: the gather follows the
    // element's own node order, whichever it is.
    const AsmHand reordered = singleTet({{{1, 0, 0}, {0, 0, 0}, {0, 0, 1}, {0, 1, 0}}}, {7, 104, 42, 9001});
    const ThermalElementFields again = mustRecover(reordered, kK, t);
    REQUIRE(again.gradients.size() == 1);
    CHECK_THAT(worstGradientError(again, {14.0, -14.0, 48.0}), WithinAbs(0.0, 1e-12));
    // A recovery that SORTED the element's node handles before gathering would pair 301 with the corner of node 7: the
    // gradient it would give is not this one.
    const double sortedWrong = (349.0 - 315.0);  // T(42) - T(7), the x difference if corners were taken in ascending handle order
    CHECK(std::abs(sortedWrong - 14.0) > 1.0);
    WARN("sparse NodeIds 7, 42, 104, 9001 -> ThermalDofIndex 0..3; element order [104, 7, 9001, 42] gives grad (14, -14, 48) K/m; result arrays of size 4, not 9002");
}

// ===========================================================================
// The gradient
// ===========================================================================

TEST_CASE("ThermalPost_XLinearFieldGradient", "[thermal][post][gradient][axis]") {
    // T = x (in SI): grad T = [1, 0, 0] K/m and q = [-k, 0, 0].
    const AsmHand hand = makeGrid(GridSpec{.side = 3, .h = 0.5});
    const ThermalElementFields fields = mustRecover(hand, kK, fieldOn(hand, affineField(0.0, 1.0, 0.0, 0.0)));
    CHECK(worstGradientError(fields, {1.0, 0.0, 0.0}) <= 1e-12);
    CHECK(worstFluxError(fields, {-kK, 0.0, 0.0}) <= 1e-11);
}

TEST_CASE("ThermalPost_YLinearFieldGradient", "[thermal][post][gradient][axis]") {
    const AsmHand hand = makeGrid(GridSpec{.side = 3, .h = 0.5});
    const ThermalElementFields fields = mustRecover(hand, kK, fieldOn(hand, affineField(0.0, 0.0, 1.0, 0.0)));
    CHECK(worstGradientError(fields, {0.0, 1.0, 0.0}) <= 1e-12);
    CHECK(worstFluxError(fields, {0.0, -kK, 0.0}) <= 1e-11);
}

TEST_CASE("ThermalPost_ZLinearFieldGradient", "[thermal][post][gradient][axis]") {
    const AsmHand hand = makeGrid(GridSpec{.side = 3, .h = 0.5});
    const ThermalElementFields fields = mustRecover(hand, kK, fieldOn(hand, affineField(0.0, 0.0, 0.0, 1.0)));
    CHECK(worstGradientError(fields, {0.0, 0.0, 1.0}) <= 1e-12);
    CHECK(worstFluxError(fields, {0.0, 0.0, -kK}) <= 1e-11);
}

TEST_CASE("ThermalPost_AffineFieldGradientAndFourierFlux", "[thermal][post][gradient][flux][patch]") {
    // T = 307.3 + 7.2 x - 3.5 y + 11.4 z  =>  grad T = [7.2, -3.5, 11.4] K/m; k = 5  =>  q = [-36, 17.5, -57] W/m2.
    // Expected values come from the coefficients, NOT from any recovery call.
    const AsmHand hand = makeGrid(GridSpec{.side = 4, .h = 0.5});
    const std::vector<Temperature> t = fieldOn(hand, affineField(kA, kB, kC, kD));
    // The nodal field itself, node by node, against the formula.
    for (std::size_t n = 0; n < t.size(); ++n) {
        const meshing::Node& node = hand.mesh.nodes()[n];
        const double expected = 307.3 + 7.2 * node.position.x.si() - 3.5 * node.position.y.si() + 11.4 * node.position.z.si();
        CHECK_THAT(t[n].si(), WithinAbs(expected, 1e-12));
    }
    const ThermalElementFields fields = mustRecover(hand, kK, t);
    REQUIRE(fields.gradients.size() == 162);
    const Vec3 expectedGradient{7.2, -3.5, 11.4};
    const Vec3 expectedFlux{-36.0, 17.5, -57.0};
    const double expectedMagnitude = std::sqrt(36.0 * 36.0 + 17.5 * 17.5 + 57.0 * 57.0);
    double gradientError = 0.0;
    double fluxError = 0.0;
    double magnitudeError = 0.0;
    double orthogonality = 0.0;
    double sign = -1.0e300;
    for (std::size_t e = 0; e < fields.gradients.size(); ++e) {
        const Vec3 g = vec(fields.gradients[e]);
        const Vec3 q = vec(fields.heatFluxes[e]);
        gradientError = std::max(gradientError, maxDifference(g, expectedGradient));
        fluxError = std::max(fluxError, maxDifference(q, expectedFlux));
        magnitudeError = std::max(magnitudeError, std::abs(fields.heatFluxMagnitudes[e].si() - expectedMagnitude));
        orthogonality = std::max(orthogonality, norm(cross(q, g)));
        // q . grad(T) = -k |grad(T)|^2, independently.
        sign = std::max(sign, std::abs(dot(q, g) - (-kK * dot(g, g))));
    }
    CHECK(gradientError <= 1e-9);
    CHECK(fluxError <= 1e-8);
    CHECK(magnitudeError <= 1e-8);
    CHECK(orthogonality <= 1e-6);   // q x grad(T) = 0: antiparallel
    CHECK(sign <= 1e-6);
    CHECK(dot(vec(fields.heatFluxes[0]), vec(fields.gradients[0])) < 0.0);
    WARN(std::format("affine patch, 162 jittered tetrahedra: max gradient error {:.2e} K/m, max flux error {:.2e} W/m2, "
                     "max |q| error {:.2e}, expected |q| {:.9f}, max |q.grad + k|grad|^2| {:.2e}, max |q x grad| {:.2e}",
                     gradientError, fluxError, magnitudeError, expectedMagnitude, sign, orthogonality));
}

TEST_CASE("ThermalPost_MultiTetAffinePatchUsesGenuinelyDifferentTetrahedra", "[thermal][post][patch]") {
    // The patch is only a patch if its tetrahedra differ: distinct volumes, distinct orientations.
    const AsmHand hand = makeGrid(GridSpec{.side = 4, .h = 0.5});
    std::set<long long> volumes;
    double smallest = 1.0e300;
    double largest = 0.0;
    for (const meshing::Tetrahedron& tet : hand.mesh.tetrahedra()) {
        const Vec3 p0 = positionOf(hand, tet.nodes[0]);
        const Vec3 p1 = positionOf(hand, tet.nodes[1]);
        const Vec3 p2 = positionOf(hand, tet.nodes[2]);
        const Vec3 p3 = positionOf(hand, tet.nodes[3]);
        const Vec3 e1{p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
        const Vec3 e2{p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
        const Vec3 e3{p3[0] - p0[0], p3[1] - p0[1], p3[2] - p0[2]};
        const double det = dot(e1, cross(e2, e3));
        REQUIRE(det > 0.0);
        volumes.insert(std::llround(det * 1.0e9));
        smallest = std::min(smallest, det);
        largest = std::max(largest, det);
    }
    CHECK(volumes.size() > 100);   // of 162
    CHECK(largest > 1.5 * smallest);   // genuinely different sizes and shapes, not one tetrahedron repeated
    // Sharing: 64 nodes carry 162 elements of four corners each.
    CHECK(hand.mesh.nodes().size() == 64);
    const ThermalElementFields fields = mustRecover(hand, kK, fieldOn(hand, affineField(kA, kB, kC, kD)));
    CHECK(worstGradientError(fields, {kB, kC, kD}) <= 1e-9);
    CHECK(worstFluxError(fields, {-kK * kB, -kK * kC, -kK * kD}) <= 1e-8);
}

TEST_CASE("ThermalPost_FourierLawSign", "[thermal][post][flux][sign]") {
    // Temperature increases with x: dT/dx > 0, so heat flows toward -x: qx < 0. And the reverse.
    const AsmHand hand = makeGrid(GridSpec{.side = 3, .h = 0.5});
    const ThermalElementFields up = mustRecover(hand, kK, fieldOn(hand, affineField(300.0, 10.0, 0.0, 0.0)));
    for (const thermal::HeatFlux3& q : up.heatFluxes) {
        CHECK(q.x.si() < 0.0);
        CHECK_THAT(q.x.si(), WithinAbs(-50.0, 1e-9));
    }
    const ThermalElementFields down = mustRecover(hand, kK, fieldOn(hand, affineField(400.0, -10.0, 0.0, 0.0)));
    for (const thermal::HeatFlux3& q : down.heatFluxes) {
        CHECK(q.x.si() > 0.0);
        CHECK_THAT(q.x.si(), WithinAbs(50.0, 1e-9));
    }
    WARN("Fourier sign: dT/dx = +10 K/m, k = 5 -> qx = -50 W/m2 (heat flows toward -x); dT/dx = -10 -> qx = +50");
}

TEST_CASE("ThermalPost_HeatFluxMagnitudeIsEuclideanAndKeepsTheVector", "[thermal][post][flux][magnitude]") {
    // grad T = (-0.6, -0.8, -2.4), k = 5  =>  q = (3, 4, 12), |q| = 13 (Euclidean). The L1 norm would be 19, the vector keeps
    // every sign, and the magnitude is not a stored substitute for it.
    const AsmHand hand = makeGrid(GridSpec{.side = 3, .h = 0.5});
    const ThermalElementFields fields = mustRecover(hand, kK, fieldOn(hand, affineField(300.0, -0.6, -0.8, -2.4)));
    for (std::size_t e = 0; e < fields.heatFluxes.size(); ++e) {
        CHECK_THAT(fields.heatFluxes[e].x.si(), WithinAbs(3.0, 1e-10));
        CHECK_THAT(fields.heatFluxes[e].y.si(), WithinAbs(4.0, 1e-10));
        CHECK_THAT(fields.heatFluxes[e].z.si(), WithinAbs(12.0, 1e-10));
        CHECK_THAT(fields.heatFluxMagnitudes[e].si(), WithinAbs(13.0, 1e-10));
        CHECK(std::abs(fields.heatFluxMagnitudes[e].si() - 19.0) > 5.0);
        // The stored magnitude equals the norm of the stored vector, independently computed.
        CHECK_THAT(fields.heatFluxMagnitudes[e].si(), WithinAbs(norm(vec(fields.heatFluxes[e])), 1e-12));
        CHECK(fields.heatFluxMagnitudes[e].si() >= 0.0);
    }
    // A zero vector has magnitude exactly zero, not NaN, and a zero flux, not a negative zero.
    const std::vector<Temperature> flat(hand.mesh.nodes().size(), Temperature::fromSi(300.0));
    const ThermalElementFields zero = mustRecover(hand, kK, flat);
    for (std::size_t e = 0; e < zero.heatFluxes.size(); ++e) {
        CHECK(zero.heatFluxMagnitudes[e].si() <= 1e-10);
        CHECK_FALSE(std::isnan(zero.heatFluxMagnitudes[e].si()));
    }
    const AsmHand single = unitTet();
    const ThermalElementFields exactZero = mustRecover(single, kK, std::vector<Temperature>(4, Temperature::fromSi(300.0)));
    CHECK(exactZero.heatFluxMagnitudes[0].si() == 0.0);
    CHECK(std::signbit(exactZero.heatFluxes[0].x.si()) == false);
}

TEST_CASE("ThermalPost_ConductivityScalingScalesTheFluxAndNotTheGradient", "[thermal][post][flux][scaling]") {
    const AsmHand hand = makeGrid(GridSpec{.side = 3, .h = 0.5});
    const std::vector<Temperature> t = scatteredField(hand, 99);
    const ThermalElementFields one = mustRecover(hand, 2.5, t);
    const ThermalElementFields four = mustRecover(hand, 10.0, t);
    double gradientDifference = 0.0;
    double fluxRatio = 0.0;
    double magnitudeRatio = 0.0;
    for (std::size_t e = 0; e < one.gradients.size(); ++e) {
        gradientDifference = std::max(gradientDifference, maxDifference(vec(one.gradients[e]), vec(four.gradients[e])));
        fluxRatio = std::max(fluxRatio, maxDifference(vec(four.heatFluxes[e]), scaled(vec(one.heatFluxes[e]), 4.0)));
        magnitudeRatio = std::max(magnitudeRatio, std::abs(four.heatFluxMagnitudes[e].si() - 4.0 * one.heatFluxMagnitudes[e].si()));
    }
    CHECK(gradientDifference == 0.0);   // the gradient does not know k: bit for bit
    CHECK(fluxRatio <= 1e-10);
    CHECK(magnitudeRatio <= 1e-10);
    WARN(std::format("conductivity scaling k1 = 2.5, k2 = 10 (ratio 4): gradient difference {:.1e}, max |q2 - 4 q1| {:.2e}, max ||q2| - 4|q1|| {:.2e}",
                     gradientDifference, fluxRatio, magnitudeRatio));
}

TEST_CASE("ThermalPost_TemperatureOffsetLeavesGradientAndFluxUnchanged", "[thermal][post][offset]") {
    const AsmHand hand = makeGrid(GridSpec{.side = 3, .h = 0.5});
    const std::vector<Temperature> base = scatteredField(hand, 5);
    std::vector<Temperature> shifted = base;
    for (Temperature& t : shifted) {
        t = Temperature::fromSi(t.si() + 123.456);
    }
    const ThermalElementFields a = mustRecover(hand, kK, base);
    const ThermalElementFields b = mustRecover(hand, kK, shifted);
    double worst = 0.0;
    for (std::size_t e = 0; e < a.gradients.size(); ++e) {
        worst = std::max({worst, maxDifference(vec(a.gradients[e]), vec(b.gradients[e])),
                          maxDifference(vec(a.heatFluxes[e]), vec(b.heatFluxes[e])),
                          std::abs(a.heatFluxMagnitudes[e].si() - b.heatFluxMagnitudes[e].si())});
    }
    CHECK(worst <= 1e-10);
}

TEST_CASE("ThermalPost_TemperatureDifferenceScalingIsLinear", "[thermal][post][offset]") {
    // T' = Tref + alpha (T - Tref): the gradient and the flux scale by alpha, where an OFFSET would not move them.
    const AsmHand hand = makeGrid(GridSpec{.side = 3, .h = 0.5});
    const std::vector<Temperature> base = scatteredField(hand, 8);
    const double reference = 300.0;
    const double alpha = 3.0;
    std::vector<Temperature> stretched = base;
    for (Temperature& t : stretched) {
        t = Temperature::fromSi(reference + alpha * (t.si() - reference));
    }
    const ThermalElementFields a = mustRecover(hand, kK, base);
    const ThermalElementFields b = mustRecover(hand, kK, stretched);
    double worst = 0.0;
    for (std::size_t e = 0; e < a.gradients.size(); ++e) {
        worst = std::max({worst, maxDifference(vec(b.gradients[e]), scaled(vec(a.gradients[e]), alpha)),
                          maxDifference(vec(b.heatFluxes[e]), scaled(vec(a.heatFluxes[e]), alpha))});
    }
    CHECK(worst <= 1e-8);
}

// ===========================================================================
// Geometry: translation, rotation, scaling
// ===========================================================================

TEST_CASE("ThermalPost_TranslationInvariant", "[thermal][post][transform][translation]") {
    // The same nodal values on a mesh translated by (3.1, -2.2, 7.7): the same physical field transported, the same gradient.
    const AsmHand base = makeGrid(GridSpec{.side = 3, .h = 0.5});
    const AsmHand moved = makeGrid(GridSpec{.side = 3, .h = 0.5, .translation = {3.1, -2.2, 7.7}});
    const std::vector<Temperature> t = scatteredField(base, 21);
    const ThermalElementFields a = mustRecover(base, kK, t);
    const ThermalElementFields b = mustRecover(moved, kK, t);
    double worst = 0.0;
    for (std::size_t e = 0; e < a.gradients.size(); ++e) {
        worst = std::max({worst, maxDifference(vec(a.gradients[e]), vec(b.gradients[e])),
                          maxDifference(vec(a.heatFluxes[e]), vec(b.heatFluxes[e])),
                          std::abs(a.heatFluxMagnitudes[e].si() - b.heatFluxMagnitudes[e].si())});
    }
    CHECK(worst <= 1e-9);

    // An affine field transported by the translation: T'(x') = T(x' - t) keeps the same gradient [b, c, d].
    const Vec3 shift{3.1, -2.2, 7.7};
    const auto transported = [&](const Vec3& p) { return kA + kB * (p[0] - shift[0]) + kC * (p[1] - shift[1]) + kD * (p[2] - shift[2]); };
    const ThermalElementFields c = mustRecover(moved, kK, fieldOn(moved, transported));
    CHECK(worstGradientError(c, {kB, kC, kD}) <= 1e-9);
}

TEST_CASE("ThermalPost_RotationCovariant", "[thermal][post][transform][rotation]") {
    // x' = R x + t with det R = +1, the scalar field transported rigidly (the same nodal values at the rotated nodes):
    //   grad' = R grad,  q' = R q,  |q'| = |q|.
    const Mat3 rotation = rotationAbout({1.0, 2.0, 3.0}, 0.7);
    REQUIRE_THAT(determinant(rotation), WithinAbs(1.0, 1e-14));
    const AsmHand base = makeGrid(GridSpec{.side = 3, .h = 0.5});
    const AsmHand turned = makeGrid(GridSpec{.side = 3, .h = 0.5, .rotation = rotation, .translation = {1.0, -2.0, 0.5}});
    const std::vector<Temperature> t = scatteredField(base, 33);
    const ThermalElementFields a = mustRecover(base, kK, t);
    const ThermalElementFields b = mustRecover(turned, kK, t);
    REQUIRE(a.gradients.size() == b.gradients.size());
    double gradientError = 0.0;
    double fluxError = 0.0;
    double magnitudeError = 0.0;
    double changed = 0.0;
    for (std::size_t e = 0; e < a.gradients.size(); ++e) {
        const Vec3 expectedGradient = applyMat(rotation, vec(a.gradients[e]));
        const Vec3 expectedFlux = applyMat(rotation, vec(a.heatFluxes[e]));
        gradientError = std::max(gradientError, maxDifference(vec(b.gradients[e]), expectedGradient));
        fluxError = std::max(fluxError, maxDifference(vec(b.heatFluxes[e]), expectedFlux));
        magnitudeError = std::max(magnitudeError, std::abs(b.heatFluxMagnitudes[e].si() - a.heatFluxMagnitudes[e].si()));
        changed = std::max(changed, maxDifference(vec(b.gradients[e]), vec(a.gradients[e])));
    }
    CHECK(gradientError <= 1e-9);
    CHECK(fluxError <= 1e-8);
    CHECK(magnitudeError <= 1e-9);
    CHECK(changed > 1.0);   // the rotation really moved the vectors: this is not a test of nothing

    // An affine field transported by the rotation: T'(x') = T(R^T (x' - t)) has grad' = R [b c d].
    const Vec3 translation{1.0, -2.0, 0.5};
    const Mat3 inverse{{{rotation[0][0], rotation[1][0], rotation[2][0]},
                        {rotation[0][1], rotation[1][1], rotation[2][1]},
                        {rotation[0][2], rotation[1][2], rotation[2][2]}}};
    const auto transported = [&](const Vec3& p) {
        const Vec3 local = applyMat(inverse, {p[0] - translation[0], p[1] - translation[1], p[2] - translation[2]});
        return kA + kB * local[0] + kC * local[1] + kD * local[2];
    };
    const ThermalElementFields c = mustRecover(turned, kK, fieldOn(turned, transported));
    const Vec3 expected = applyMat(rotation, {kB, kC, kD});
    CHECK(worstGradientError(c, expected) <= 1e-9);
    CHECK(worstFluxError(c, scaled(expected, -kK)) <= 1e-8);
    WARN(std::format("rotation about (1,2,3) by 0.7 rad (det {:.15f}): max |grad' - R grad| {:.2e}, max |q' - R q| {:.2e}, max ||q'| - |q|| {:.2e}; "
                     "affine: R [7.2, -3.5, 11.4] = [{:.6f}, {:.6f}, {:.6f}]",
                     determinant(rotation), gradientError, fluxError, magnitudeError, expected[0], expected[1], expected[2]));
}

TEST_CASE("ThermalPost_GeometryScalingDividesTheGradientAndTheFlux", "[thermal][post][transform][scaling]") {
    // The same nodal temperatures on the same mesh scaled by s = 2: the temperature changes over twice the distance, so
    // grad' = grad / s and q' = q / s. (A different statement from transporting an affine field with new coefficients.)
    const AsmHand base = makeGrid(GridSpec{.side = 3, .h = 0.5});
    const AsmHand doubled = makeGrid(GridSpec{.side = 3, .h = 1.0});
    const std::vector<Temperature> t = scatteredField(base, 77);
    const ThermalElementFields a = mustRecover(base, kK, t);
    const ThermalElementFields b = mustRecover(doubled, kK, t);
    double gradientError = 0.0;
    double fluxError = 0.0;
    for (std::size_t e = 0; e < a.gradients.size(); ++e) {
        gradientError = std::max(gradientError, maxDifference(vec(b.gradients[e]), scaled(vec(a.gradients[e]), 0.5)));
        fluxError = std::max(fluxError, maxDifference(vec(b.heatFluxes[e]), scaled(vec(a.heatFluxes[e]), 0.5)));
    }
    CHECK(gradientError <= 1e-9);
    CHECK(fluxError <= 1e-8);
    // A millimetre-denominated mesh is NOT the same as a metre one: the geometry is SI metres, and a unit leak would be 1000x.
    const AsmHand milli = makeGrid(GridSpec{.side = 3, .h = 0.0005});
    const ThermalElementFields c = mustRecover(milli, kK, t);
    double ratio = 0.0;
    for (std::size_t e = 0; e < a.gradients.size(); ++e) {
        ratio = std::max(ratio, maxDifference(vec(c.gradients[e]), scaled(vec(a.gradients[e]), 1000.0)));
    }
    CHECK(ratio <= 1e-5);
    WARN(std::format("geometry scaling s = 2: max |grad' - grad/2| {:.2e}, max |q' - q/2| {:.2e}", gradientError, fluxError));
}

// ===========================================================================
// Arbitrary nodal fields: the gather
// ===========================================================================

TEST_CASE("ThermalPost_GeneralTetIndependentGradientOracle", "[thermal][post][gradient][oracle]") {
    // A scattered nodal field has a different constant gradient in every tetrahedron. Each is compared with the test's own
    // dense solve of [1 x y z] [a b c d]^T = Te for THAT element's corners in THAT element's order.
    const AsmHand hand = makeGrid(GridSpec{.side = 4, .h = 0.5, .jitter = 0.2, .seed = 424242});
    const std::vector<Temperature> t = scatteredField(hand, 2024);
    const ThermalElementFields fields = mustRecover(hand, kK, t);
    REQUIRE(fields.gradients.size() == hand.mesh.tetrahedra().size());
    double worst = 0.0;
    double smallest = 1.0e300;
    for (std::size_t e = 0; e < fields.gradients.size(); ++e) {
        const Vec3 expected = oracleGradient(hand, t, e);
        worst = std::max(worst, maxDifference(vec(fields.gradients[e]), expected));
        smallest = std::min(smallest, norm(expected));
        CHECK(maxDifference(vec(fields.heatFluxes[e]), scaled(expected, -kK)) <= 1e-7);
    }
    CHECK(worst <= 1e-7);
    CHECK(smallest > 0.0);
    WARN(std::format("general-tet oracle over {} tetrahedra of a scattered field: max |grad - dense solve| {:.2e} K/m", fields.gradients.size(), worst));
}

TEST_CASE("ThermalPost_PreservesTetLocalNodeOrderInAMeshWhoseHandlesAreNotSorted", "[thermal][post][gradient][order]") {
    // A mesh in which a tetrahedron names its nodes in DESCENDING handle order: [40, 30, 20, 10]. The temperature of each
    // corner is paired with its coordinates by the element's own order. A gather that sorted the handles would swap them.
    const AsmHand hand = singleTet({{{0, 0, 0}, {2, 0, 0}, {0, 3, 0}, {0, 0, 4}}}, {40, 30, 20, 10});
    // Corners: node 40 at the origin, 30 at (2,0,0), 20 at (0,3,0), 10 at (0,0,4). Unknowns ascend: 10, 20, 30, 40.
    const std::vector<Temperature> t{Temperature::fromSi(318.0),    // node 10 at (0,0,4)
                                     Temperature::fromSi(311.0),    // node 20 at (0,3,0)
                                     Temperature::fromSi(304.0),    // node 30 at (2,0,0)
                                     Temperature::fromSi(300.0)};   // node 40 at (0,0,0)
    // T = 300 + 2 x + (11/3) y + 4.5 z  -> dT/dx = (304-300)/2 = 2, dT/dy = (311-300)/3 = 11/3, dT/dz = (318-300)/4 = 4.5
    const ThermalElementFields fields = mustRecover(hand, kK, t);
    REQUIRE(fields.gradients.size() == 1);
    CHECK_THAT(fields.gradients[0].x.si(), WithinAbs(2.0, 1e-12));
    CHECK_THAT(fields.gradients[0].y.si(), WithinAbs(11.0 / 3.0, 1e-12));
    CHECK_THAT(fields.gradients[0].z.si(), WithinAbs(4.5, 1e-12));
    CHECK_THAT(fields.heatFluxes[0].x.si(), WithinAbs(-10.0, 1e-11));
    CHECK_THAT(fields.heatFluxes[0].y.si(), WithinAbs(-55.0 / 3.0, 1e-11));
    CHECK_THAT(fields.heatFluxes[0].z.si(), WithinAbs(-22.5, 1e-11));
    // The dense oracle agrees, from the same corners and temperatures typed independently.
    const Vec3 oracle = denseTetGradient({{{0, 0, 0}, {2, 0, 0}, {0, 3, 0}, {0, 0, 4}}}, {{300.0, 304.0, 311.0, 318.0}});
    CHECK(maxDifference(vec(fields.gradients[0]), oracle) <= 1e-12);
}

// ===========================================================================
// Refusals
// ===========================================================================

TEST_CASE("ThermalPost_RefusesWhatItCannotRecoverAndPublishesNothing", "[thermal][post][refusals]") {
    const AsmHand hand = unitTet();
    const std::vector<Temperature> good{Temperature::fromSi(300.0), Temperature::fromSi(310.0), Temperature::fromSi(320.0),
                                        Temperature::fromSi(330.0)};
    const auto problemFor = [&](double k, const std::vector<Temperature>& t) {
        return postProblemOf(thermal::recoverThermalElementFields(hand.mesh, hand.dofs, ThermalConductivity::fromSi(k), t));
    };
    REQUIRE_FALSE(problemFor(kK, good).has_value());

    SECTION("a conductivity that is not finite and positive") {
        for (const double k : {0.0, -1.0, kNaN, kInf, -kInf}) {
            CAPTURE(k);
            CHECK(problemFor(k, good) == PostProblem::ThermalConductivityInvalid);
        }
    }
    SECTION("a temperature vector of the wrong size") {
        CHECK(problemFor(kK, std::vector<Temperature>(3, Temperature::fromSi(300.0))) == PostProblem::TemperatureCardinalityMismatch);
        CHECK(problemFor(kK, std::vector<Temperature>(5, Temperature::fromSi(300.0))) == PostProblem::TemperatureCardinalityMismatch);
        CHECK(problemFor(kK, {}) == PostProblem::TemperatureCardinalityMismatch);
    }
    SECTION("a temperature that is not finite, naming the unknown") {
        for (const double bad : {kNaN, kInf, -kInf}) {
            std::vector<Temperature> t = good;
            t[2] = Temperature::fromSi(bad);
            const auto refused = thermal::recoverThermalElementFields(hand.mesh, hand.dofs, ThermalConductivity::fromSi(kK), t);
            REQUIRE_FALSE(refused.has_value());
            CHECK(refused.error().problem == PostProblem::NonFiniteTemperatureResult);
            CHECK_THAT(refused.error().error.message, ContainsSubstring("unknown 2"));
        }
    }
    SECTION("a temperature below absolute zero") {
        std::vector<Temperature> t = good;
        t[1] = Temperature::fromSi(-0.5);
        CHECK(problemFor(kK, t) == PostProblem::TemperatureBelowAbsoluteZero);
        t[1] = Temperature::fromSi(0.0);   // 0 K is an absolute temperature
        CHECK_FALSE(problemFor(kK, t).has_value());
    }
    SECTION("a numbering of another mesh") {
        const AsmHand other = unitTet();
        const auto refused = thermal::recoverThermalElementFields(hand.mesh, other.dofs, ThermalConductivity::fromSi(kK), good);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == PostProblem::NumberingMismatch);
    }
}

TEST_CASE("ThermalPost_OneInvalidTetrahedronFailsTheWholeRecoveryAndIsNeverSkipped", "[thermal][post][refusals][atomic]") {
    // Two tetrahedra on the same four nodes: [1 2 3 4] is valid, [1 3 2 4] is its mirror, inverted. The recovery is refused
    // and names the element; it does not return a one-element field for the valid one.
    const Nodes nodes{{1, at(0, 0, 0)}, {2, at(1, 0, 0)}, {3, at(0, 1, 0)}, {4, at(0, 0, 1)}};
    const AsmHand hand = makeAsmHand(nodes, {}, {{0, 1, 2, 3}, {0, 2, 1, 3}});
    const std::vector<Temperature> t(4, Temperature::fromSi(300.0));
    const auto refused = thermal::recoverThermalElementFields(hand.mesh, hand.dofs, ThermalConductivity::fromSi(kK), t);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().problem == PostProblem::InvalidTetGeometry);
    REQUIRE(refused.error().element.has_value());
    CHECK(*refused.error().element == hand.mesh.tetrahedra()[1].id);
    CHECK(*refused.error().element != hand.mesh.tetrahedra()[0].id);

    // A degenerate (coplanar) tetrahedron likewise.
    const Nodes flat{{1, at(0, 0, 0)}, {2, at(1, 0, 0)}, {3, at(0, 1, 0)}, {4, at(1, 1, 0)}};
    const AsmHand coplanar = makeAsmHand(flat, {}, {{0, 1, 2, 3}});
    CHECK(postProblemOf(thermal::recoverThermalElementFields(coplanar.mesh, coplanar.dofs, ThermalConductivity::fromSi(kK), t)) ==
          PostProblem::InvalidTetGeometry);
}

TEST_CASE("ThermalPost_RefusesNonFiniteDerivedValues", "[thermal][post][refusals][finite]") {
    SECTION("a gradient that overflows although every temperature is finite") {
        // A 1 mm tetrahedron has G ~ 1e3 /m; a 1e308 K difference times 1e3 is infinite.
        const AsmHand hand = unitTet(1.0e-3);
        const std::vector<Temperature> t{Temperature::fromSi(0.0), Temperature::fromSi(1.0e308), Temperature::fromSi(0.0),
                                         Temperature::fromSi(0.0)};
        const auto refused = thermal::recoverThermalElementFields(hand.mesh, hand.dofs, ThermalConductivity::fromSi(kK), t);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == PostProblem::NonFiniteTemperatureGradient);
        CHECK(refused.error().element.has_value());
    }
    SECTION("a heat flux that overflows although the gradient is finite") {
        const AsmHand hand = unitTet();
        const std::vector<Temperature> t{Temperature::fromSi(0.0), Temperature::fromSi(1.0e9), Temperature::fromSi(0.0),
                                         Temperature::fromSi(0.0)};
        const auto refused = thermal::recoverThermalElementFields(hand.mesh, hand.dofs, ThermalConductivity::fromSi(1.0e300), t);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == PostProblem::NonFiniteHeatFlux);
    }
    SECTION("a magnitude that overflows although every component is finite") {
        const AsmHand hand = unitTet();
        const std::vector<Temperature> t{Temperature::fromSi(0.0), Temperature::fromSi(1.5e8), Temperature::fromSi(1.5e8),
                                         Temperature::fromSi(1.5e8)};
        // q = -1e300 * 1.5e8 = -1.5e308 in each component (finite); |q| = 2.6e308 (not).
        const auto refused = thermal::recoverThermalElementFields(hand.mesh, hand.dofs, ThermalConductivity::fromSi(1.0e300), t);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == PostProblem::NonFiniteHeatFluxMagnitude);
    }
}

// ===========================================================================
// The result type
// ===========================================================================

TEST_CASE("ThermalPost_ElementResultsAreKeyedByOrdinalAndNotByRawElementId", "[thermal][post][ordering][sparse-ids]") {
    // makeTwoTet has a triangle FIRST, so its tetrahedra are ElementId 2 and 3, not 0 and 1: an id is not an array index.
    const AsmHand hand = makeTwoTet();
    REQUIRE(hand.tets.size() == 2);
    CHECK(hand.tets[0].value() != 0);
    const ThermalSystem system = assemble(hand, kK, std::nullopt, noHeat(hand));
    std::vector<thermal::ThermalTemperatureConstraint> list{{dofIx(0), Temperature::fromSi(400.0)},
                                                            {dofIx(4), Temperature::fromSi(300.0)}};
    const auto constraints = thermal::buildThermalConstraintMap(hand.dofs, list);
    REQUIRE(constraints.has_value());
    const auto solved = thermal::solveSteadyThermalSystem(system, hand.dofs, *constraints, ThermalSolverSettings{});
    REQUIRE(solved.has_value());
    const auto post = thermal::recoverSteadyFromSolution(hand.mesh, hand.dofs, ThermalConductivity::fromSi(kK), *solved);
    REQUIRE(post.has_value());

    REQUIRE(post->elementCount() == 2);
    CHECK(post->finalState().fields.gradients.size() == 2);   // the number of tetrahedra, not the largest id + 1
    CHECK(post->elementIds()[0] == hand.tets[0]);
    CHECK(post->elementIds()[1] == hand.tets[1]);
    CHECK(post->ordinalOf(hand.tets[0]) == 0);
    CHECK(post->ordinalOf(hand.tets[1]) == 1);
    CHECK_FALSE(post->ordinalOf(ElementId::fromValue(hand.tets[1].value() + 1)).has_value());
    CHECK_FALSE(post->ordinalOf(ElementId::fromValue(99999)).has_value());
    // Lookup by id goes through the ordinal -- for every accessor, for EVERY tetrahedron (an id one past its ordinal would
    // also be right for the last one, so the first is what proves it), and the two tetrahedra differ so a swap shows.
    const auto byId = post->gradientOf(0, hand.tets[1]);
    REQUIRE(byId.has_value());
    CHECK(vec(*byId) == vec(post->finalState().fields.gradients[1]));
    CHECK(vec(post->finalState().fields.gradients[0]) != vec(post->finalState().fields.gradients[1]));
    CHECK(post->finalState().fields.heatFluxMagnitudes[0].si() != post->finalState().fields.heatFluxMagnitudes[1].si());
    for (std::size_t e = 0; e < 2; ++e) {
        const auto g = post->gradientOf(0, hand.tets[e]);
        const auto q = post->heatFluxOf(0, hand.tets[e]);
        const auto m = post->heatFluxMagnitudeOf(0, hand.tets[e]);
        REQUIRE((g.has_value() && q.has_value() && m.has_value()));
        CHECK(vec(*g) == vec(post->finalState().fields.gradients[e]));
        CHECK(vec(*q) == vec(post->finalState().fields.heatFluxes[e]));
        CHECK(m->si() == post->finalState().fields.heatFluxMagnitudes[e].si());
    }
    // The solution seam applies the conductivity rule too -- the kernel's own check is not the only one.
    for (const double badK : {0.0, -1.0, kNaN, kInf}) {
        CAPTURE(badK);
        const auto refusedK = thermal::recoverSteadyFromSolution(hand.mesh, hand.dofs, ThermalConductivity::fromSi(badK), *solved);
        REQUIRE_FALSE(refusedK.has_value());
        CHECK(refusedK.error().problem == PostProblem::ThermalConductivityInvalid);
    }
    CHECK_FALSE(post->gradientOf(0, ElementId::fromValue(99999)).has_value());
    CHECK_FALSE(post->heatFluxOf(0, ElementId::fromValue(99999)).has_value());
    CHECK_FALSE(post->heatFluxMagnitudeOf(0, ElementId::fromValue(99999)).has_value());
    CHECK_FALSE(post->gradientOf(1, hand.tets[0]).has_value());   // a state that does not exist
    const auto flux = post->heatFluxOf(0, hand.tets[0]);
    const auto magnitude = post->heatFluxMagnitudeOf(0, hand.tets[0]);
    REQUIRE((flux.has_value() && magnitude.has_value()));
    CHECK(magnitude->si() == flux->magnitude().si());

    // The nodal lookup goes through the numbering.
    for (const meshing::Node& node : hand.mesh.nodes()) {
        const auto t = post->temperatureOf(0, hand.dofs, node.id);
        REQUIRE(t.has_value());
        CHECK(t->si() == solved->temperatureOf(hand.dofs, node.id)->si());
    }
    CHECK_FALSE(post->temperatureOf(0, hand.dofs, NodeId::fromValue(9999)).has_value());
    CHECK_FALSE(post->temperatureOf(3, hand.dofs, hand.mesh.nodes()[0].id).has_value());
    const AsmHand other = makeTwoTet();
    const auto wrongMap = post->temperatureOf(0, other.dofs, hand.mesh.nodes()[0].id);
    REQUIRE_FALSE(wrongMap.has_value());
    CHECK_THAT(wrongMap.error().message, ContainsSubstring("another numbering"));
    CHECK(post->describes(hand.dofs));
    CHECK_FALSE(post->describes(other.dofs));
}

TEST_CASE("ThermalPost_SteadySlabOnAJitteredMeshRecoversTheFourierLaw", "[thermal][post][slab][fourier]") {
    // 400 K at z = 0 and 300 K at z = L = 1 m, adiabatic sides, k = 45 W/(m K). T(z) = 400 - 100 z/L. The tetrahedra are
    // jittered, but linear elements reproduce an affine solution exactly, so every element must give
    //   grad T = [0, 0, -100/L] = [0, 0, -100] K/m    q = [0, 0, +4500] W/m2    |q| = 4500 W/m2
    // (heat flows toward +z, from the hot face to the cold one) -- solved, then recovered, end to end.
    const double k = 45.0;
    const AsmHand hand = makeGrid(GridSpec{.side = 4, .h = 1.0 / 3.0});
    const ThermalSystem system = assemble(hand, k, std::nullopt, noHeat(hand));
    const auto run = [&](double hot, double cold) {
        std::vector<thermal::ThermalTemperatureConstraint> list;
        for (std::size_t n = 0; n < hand.mesh.nodes().size(); ++n) {
            const double z = hand.mesh.nodes()[n].position.z.si();
            if (z == 0.0) {
                list.push_back({dofIx(n), Temperature::fromSi(hot)});
            } else if (std::abs(z - 1.0) < 1e-12) {
                list.push_back({dofIx(n), Temperature::fromSi(cold)});
            }
        }
        const auto constraints = thermal::buildThermalConstraintMap(hand.dofs, list);
        REQUIRE(constraints.has_value());
        auto solved = thermal::solveSteadyThermalSystem(system, hand.dofs, *constraints, ThermalSolverSettings{});
        REQUIRE(solved.has_value());
        auto post = thermal::recoverSteadyFromSolution(hand.mesh, hand.dofs, ThermalConductivity::fromSi(k), *solved);
        INFO((post.has_value() ? std::string{} : post.error().error.message));
        REQUIRE(post.has_value());
        return std::move(*post);
    };
    const ThermalPostResult forward = run(400.0, 300.0);
    double gradientError = 0.0;
    double fluxError = 0.0;
    double magnitudeError = 0.0;
    for (std::size_t e = 0; e < forward.elementCount(); ++e) {
        gradientError = std::max(gradientError, maxDifference(vec(forward.finalState().fields.gradients[e]), {0.0, 0.0, -100.0}));
        fluxError = std::max(fluxError, maxDifference(vec(forward.finalState().fields.heatFluxes[e]), {0.0, 0.0, 4500.0}));
        magnitudeError = std::max(magnitudeError, std::abs(forward.finalState().fields.heatFluxMagnitudes[e].si() - 4500.0));
    }
    CHECK(gradientError <= 1e-7);
    CHECK(fluxError <= 1e-5);
    CHECK(magnitudeError <= 1e-5);
    CHECK(vec(forward.finalState().fields.heatFluxes[0])[2] > 0.0);   // hot (z = 0) to cold (z = L): +z

    // Reversed: 300 K at z = 0, 400 K at z = L. Heat flows toward -z.
    const ThermalPostResult reversed = run(300.0, 400.0);
    double reversedError = 0.0;
    for (std::size_t e = 0; e < reversed.elementCount(); ++e) {
        reversedError = std::max(reversedError, maxDifference(vec(reversed.finalState().fields.heatFluxes[e]), {0.0, 0.0, -4500.0}));
    }
    CHECK(reversedError <= 1e-5);
    CHECK(vec(reversed.finalState().fields.heatFluxes[0])[2] < 0.0);
    WARN(std::format("steady slab L = 1 m, k = 45, 400 K -> 300 K: expected dT/dz = -100 K/m, qz = +4500 W/m2; max errors {:.2e} K/m, {:.2e} W/m2, "
                     "{:.2e} W/m2 (|q|); reversed: qz = -4500, max error {:.2e}",
                     gradientError, fluxError, magnitudeError, reversedError));
}

TEST_CASE("ThermalPost_TransientHistoryKeepsTheSolutionsOrderAndSeparatesStorageFromFlux", "[thermal][post][transient]") {
    // An insulated body with uniform generation and a uniform initial temperature stays spatially uniform while it heats:
    // dT/dt != 0, grad T = 0, q = 0. Storage is not a flux.
    const AsmHand hand = makeGrid(GridSpec{.side = 3, .h = 0.01});
    const ThermalSystem system = assemble(hand, 45.0, capacityOf(7850.0, 460.0), generationHeat(hand, 2.5e6));
    const std::vector<thermal::ThermalTemperatureConstraint> none;
    const auto constraints = thermal::buildThermalConstraintMap(hand.dofs, none);
    REQUIRE(constraints.has_value());
    const auto solved = thermal::solveTransientThermalSystem(
        system, hand.dofs, *constraints, thermal::UniformInitialTemperature{Temperature::fromSi(300.0)},
        thermal::TransientSettings{.timeStep = Time::fromSi(10.0), .endTime = Time::fromSi(60.0),
                                   .output = thermal::TransientOutputPolicy::EveryStep},
        ThermalSolverSettings{});
    INFO((solved.has_value() ? std::string{} : solved.error().error.message));
    REQUIRE(solved.has_value());
    const auto post = thermal::recoverTransientFromSolution(hand.mesh, hand.dofs, ThermalConductivity::fromSi(45.0), *solved);
    INFO((post.has_value() ? std::string{} : post.error().error.message));
    REQUIRE(post.has_value());

    REQUIRE(post->stateCount() == solved->sampleCount());
    REQUIRE(post->stateCount() == 7);
    CHECK(post->mode() == thermal::ThermalAnalysisMode::Transient);
    double previousMean = 0.0;
    double worstFlux = 0.0;
    double worstGradient = 0.0;
    for (std::size_t i = 0; i < post->stateCount(); ++i) {
        const ThermalPostState& state = post->states()[i];
        // The solution's own order, times and temperatures, bit for bit.
        REQUIRE(state.time.has_value());
        CHECK(state.time->si() == solved->timeOf(i).si());
        REQUIRE(state.temperatures.size() == solved->temperaturesAt(i).size());
        for (std::size_t d = 0; d < state.temperatures.size(); ++d) {
            CHECK(std::bit_cast<std::uint64_t>(state.temperatures[d].si()) ==
                  std::bit_cast<std::uint64_t>(solved->temperaturesAt(i)[d].si()));
        }
        double mean = 0.0;
        for (const Temperature t : state.temperatures) {
            mean += t.si();
        }
        mean /= static_cast<double>(state.temperatures.size());
        if (i > 0) {
            CHECK(mean > previousMean + 1.0);   // it IS heating: dT/dt is not zero
        }
        previousMean = mean;
        for (std::size_t e = 0; e < state.fields.gradients.size(); ++e) {
            worstGradient = std::max(worstGradient, norm(vec(state.fields.gradients[e])));
            worstFlux = std::max(worstFlux, state.fields.heatFluxMagnitudes[e].si());
        }
    }
    CHECK(worstGradient <= 1e-6);
    CHECK(worstFlux <= 1e-4);
    CHECK(post->finalState().time->si() == 60.0);
    WARN(std::format("transient uniform heating, 7 states: mean temperature {:.4f} K at t = 60 s, max |grad T| {:.2e} K/m, max |q| {:.2e} W/m2 "
                     "(conductive flux is zero although dT/dt is not)",
                     previousMean, worstGradient, worstFlux));
}

TEST_CASE("ThermalPost_ALateStateBelowAbsoluteZeroFailsTheWholeHistory", "[thermal][post][transient][refusals][atomic]") {
    // A strong heat SINK in an insulated body cools it past absolute zero: the solver (whose algebra is fine with it) returns
    // the history, and the recovery refuses it as the non-physical field it is -- at the first state below 0 K, naming that
    // state, publishing no earlier state. This is the policy the steady solve left to this milestone.
    const AsmHand hand = makeGrid(GridSpec{.side = 3, .h = 0.01});
    const ThermalSystem system = assemble(hand, 45.0, capacityOf(7850.0, 460.0), generationHeat(hand, -2.5e7));
    const std::vector<thermal::ThermalTemperatureConstraint> none;
    const auto constraints = thermal::buildThermalConstraintMap(hand.dofs, none);
    REQUIRE(constraints.has_value());
    const auto solved = thermal::solveTransientThermalSystem(
        system, hand.dofs, *constraints, thermal::UniformInitialTemperature{Temperature::fromSi(300.0)},
        thermal::TransientSettings{.timeStep = Time::fromSi(10.0), .endTime = Time::fromSi(100.0),
                                   .output = thermal::TransientOutputPolicy::EveryStep},
        ThermalSolverSettings{});
    INFO((solved.has_value() ? std::string{} : solved.error().error.message));
    REQUIRE(solved.has_value());
    // Find, from the solver's own vectors, the first state that has a temperature below 0 K.
    std::size_t first = solved->sampleCount();
    for (std::size_t i = 0; i < solved->sampleCount() && first == solved->sampleCount(); ++i) {
        for (const Temperature t : solved->temperaturesAt(i)) {
            if (t.si() < 0.0) {
                first = i;
                break;
            }
        }
    }
    REQUIRE(first > 0);
    REQUIRE(first < solved->sampleCount());
    const auto refused = thermal::recoverTransientFromSolution(hand.mesh, hand.dofs, ThermalConductivity::fromSi(45.0), *solved);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().problem == PostProblem::TemperatureBelowAbsoluteZero);
    CHECK(refused.error().state == first);
    // The conductivity rule is checked first, at the transient seam too.
    for (const double badK : {0.0, -45.0, kNaN}) {
        CAPTURE(badK);
        const auto refusedK = thermal::recoverTransientFromSolution(hand.mesh, hand.dofs, ThermalConductivity::fromSi(badK), *solved);
        REQUIRE_FALSE(refusedK.has_value());
        CHECK(refusedK.error().problem == PostProblem::ThermalConductivityInvalid);
    }
    WARN(std::format("a heat sink cools the insulated body below 0 K at state {} of {}: the whole history is refused, state {} named", first,
                     solved->sampleCount(), refused.error().state));
}

TEST_CASE("ThermalPost_LargeMeshRecoveryIsExactForAnAffineFieldAndCountsMatch", "[thermal][post][smoke][sparse]") {
    const AsmHand hand = makeGrid(GridSpec{.side = 9, .h = 0.125});
    REQUIRE(hand.mesh.nodes().size() == 729);
    const ThermalElementFields fields = mustRecover(hand, kK, fieldOn(hand, affineField(kA, kB, kC, kD)));
    CHECK(fields.gradients.size() == 3072);
    CHECK(fields.heatFluxes.size() == 3072);
    CHECK(fields.heatFluxMagnitudes.size() == 3072);
    CHECK(worstGradientError(fields, {kB, kC, kD}) <= 1e-8);
    for (const HeatFlux m : fields.heatFluxMagnitudes) {
        CHECK(std::isfinite(m.si()));
    }
}

// ===========================================================================
// Metadata and vocabulary
// ===========================================================================

TEST_CASE("ThermalPost_FieldDescriptorsStateLocationComponentsAndUnits", "[thermal][post][metadata]") {
    using thermal::descriptorOf;
    const ThermalFieldDescriptor t = descriptorOf(ThermalPostField::Temperature);
    CHECK(t.location == FieldLocation::Node);
    CHECK(t.componentCount == 1);
    CHECK(t.dimension == dimensions::temperature);
    CHECK(t.unit == "K");
    const ThermalFieldDescriptor g = descriptorOf(ThermalPostField::TemperatureGradient);
    CHECK(g.location == FieldLocation::Element);
    CHECK(g.componentCount == 3);
    CHECK(g.componentNames[0] == "dT/dx");
    CHECK(g.componentNames[1] == "dT/dy");
    CHECK(g.componentNames[2] == "dT/dz");
    CHECK(g.dimension == dimensions::temperature / dimensions::length);
    CHECK(g.unit == "K/m");
    const ThermalFieldDescriptor q = descriptorOf(ThermalPostField::HeatFlux);
    CHECK(q.location == FieldLocation::Element);
    CHECK(q.componentCount == 3);
    CHECK(q.componentNames[0] == "qx");
    CHECK(q.componentNames[1] == "qy");
    CHECK(q.componentNames[2] == "qz");
    CHECK(q.dimension == dimensions::power / dimensions::area);
    CHECK(q.unit == "W/m^2");
    const ThermalFieldDescriptor m = descriptorOf(ThermalPostField::HeatFluxMagnitude);
    CHECK(m.location == FieldLocation::Element);
    CHECK(m.componentCount == 1);
    CHECK(m.dimension == q.dimension);
    CHECK(m.unit == "W/m^2");
    // The dimensions are the types' own.
    CHECK(g.dimension == TemperatureGradient::dimension);
    CHECK(q.dimension == HeatFlux::dimension);
    // The component order is the same as the vector types' own enumeration.
    CHECK(thermal::kVectorComponents[0] == thermal::VectorComponent::X);
    CHECK(thermal::kVectorComponents[1] == thermal::VectorComponent::Y);
    CHECK(thermal::kVectorComponents[2] == thermal::VectorComponent::Z);
    // Distinct, stable names.
    std::set<std::string_view> names;
    for (const ThermalPostField field : thermal::kThermalPostFields) {
        names.insert(thermal::toString(field));
        CHECK(thermal::toString(field) != "unknown");
    }
    CHECK(names.size() == 4);
    CHECK(thermal::toString(ThermalPostField::HeatFlux) == "heat_flux");
    CHECK(thermal::toString(FieldLocation::Element) == "element");
    CHECK(thermal::toString(FieldLocation::Node) == "node");
    CHECK(thermal::toString(static_cast<ThermalPostField>(99)) == "unknown");
}

TEST_CASE("ThermalPost_DiagnosticNamesAreStableAndDistinct", "[thermal][post][diagnostics]") {
    std::set<std::string_view> names;
    for (std::uint8_t v = 0; v <= static_cast<std::uint8_t>(PostProblem::NonFiniteHeatFluxMagnitude); ++v) {
        const std::string_view name = thermal::toString(static_cast<PostProblem>(v));
        CHECK(name != "unknown");
        CHECK_FALSE(name.empty());
        names.insert(name);
    }
    CHECK(names.size() == 11);
    CHECK(thermal::toString(static_cast<PostProblem>(200)) == "unknown");
    CHECK(thermal::toString(PostProblem::PostSourceMismatch) == "post_source_mismatch");
    CHECK(thermal::toString(PostProblem::TemperatureCardinalityMismatch) == "temperature_cardinality_mismatch");
    CHECK(thermal::toString(PostProblem::InvalidTetGeometry) == "invalid_tet_geometry");
}

// ===========================================================================
// Determinism and purity
// ===========================================================================

TEST_CASE("ThermalPost_RepeatedRecoveryIsBitwiseDeterministic", "[thermal][post][determinism]") {
    const AsmHand hand = makeGrid(GridSpec{.side = 5, .h = 0.5, .seed = 7});
    const std::vector<Temperature> t = scatteredField(hand, 31);
    const std::vector<Temperature> tBefore = t;
    const ThermalElementFields first = mustRecover(hand, kK, t);
    int differences = 0;
    for (int i = 0; i < 25; ++i) {
        if (!(mustRecover(hand, kK, t) == first)) {
            ++differences;
        }
    }
    CHECK(differences == 0);
    CHECK(t == tBefore);   // the temperature input is read, never written

    std::uint64_t hash = 14695981039346656037ULL;
    const auto mix = [&hash](double value) {
        const auto bits = std::bit_cast<std::uint64_t>(value);
        for (int i = 0; i < 8; ++i) {
            hash ^= (bits >> (8 * i)) & 0xFFU;
            hash *= 1099511628211ULL;
        }
    };
    for (std::size_t e = 0; e < first.gradients.size(); ++e) {
        for (const double v : vec(first.gradients[e])) {
            mix(v);
        }
        for (const double v : vec(first.heatFluxes[e])) {
            mix(v);
        }
        mix(first.heatFluxMagnitudes[e].si());
    }
    WARN(std::format("determinism: 25 repeated recoveries of {} tetrahedra, {} bitwise differences", first.gradients.size(), differences));
    WARN(std::format("bit fingerprint, jittered 5^3 grid with a scattered field: {:016x}", hash));
}
