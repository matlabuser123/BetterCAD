// P18-ASSEMBLY-001: the global thermal system, hand-checked.
//
// WHAT THESE TESTS ARE FOR. Assembly is SCATTER, so the claims are about where
// numbers go and how they combine, and they need hand evidence rather than a second
// run of the same code:
//
//   - a single unit tetrahedron assembles to exactly its local matrices;
//   - two tetrahedra sharing a face give, at every shared entry, the SUM of both
//     contributions, and nowhere else do they touch;
//   - an independent dense oracle (nested loops written in the test, no production
//     scatter) agrees with the sparse result on every entry;
//   - convection lands in the right rows and columns, and a node shared by two
//     facets accumulates;
//   - nothing is regularised: a conduction-only K annihilates the constant vector;
//   - a refusal is a refusal and publishes nothing.
//
// The real-mesh, source-currentness and material claims are in
// ThermalAssemblyMappedTests.cpp.
//
// THE HAND LOCAL MATRICES (k = 6 W/(m K), rho cp = 120 J/(m^3 K), both tetrahedra
// positively oriented):
//
//   Tet A, the unit one, V = 1/6, grad N = (-1,-1,-1) (1,0,0) (0,1,0) (0,0,1):
//       Ke_A = k V G^T G = [ 3 -1 -1 -1; -1 1 0 0; -1 0 1 0; -1 0 0 1 ]
//       Ce_A = rho cp V / 20 [2 1 1 1; ...]            = [2 1 1 1; 1 2 1 1; 1 1 2 1; 1 1 1 2]
//   Tet B, nodes (1,0,0) (0,1,0) (0,0,1) (1,1,1), V = 1/3, grad N =
//       (1,-1,-1)/2 (-1,1,-1)/2 (-1,-1,1)/2 (1,1,1)/2, so G^T G = (1/4)[3 -1 -1 -1; ...]
//       Ke_B = k V G^T G = 2 G^T G                      = [ 1.5 -.5 -.5 -.5; ...] (diag 1.5, off -.5)
//       Ce_B = 2 [2 1 1 1; ...]                         = (diag 4, off 2)

#include "ThermalAssemblyTestSupport.hpp"

#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <bit>
#include <chrono>
#include <format>
#include <limits>
#include <numeric>
#include <type_traits>
#include <utility>

using namespace actest;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using thermal::MatrixProblem;
using thermal::ThermalHeatVector;
using thermal::ThermalSparseMatrix;

namespace {

constexpr double kK = 6.0;       // W/(m K)
constexpr double kRho = 120.0;   // kg/m^3
constexpr double kCp = 1.0;      // J/(kg K); rho cp = 120

using Local4 = std::vector<std::vector<double>>;

[[nodiscard]] Local4 keA() { return {{3, -1, -1, -1}, {-1, 1, 0, 0}, {-1, 0, 1, 0}, {-1, 0, 0, 1}}; }
[[nodiscard]] Local4 ceA() { return {{2, 1, 1, 1}, {1, 2, 1, 1}, {1, 1, 2, 1}, {1, 1, 1, 2}}; }
[[nodiscard]] Local4 keB() {
    return {{1.5, -0.5, -0.5, -0.5}, {-0.5, 1.5, -0.5, -0.5}, {-0.5, -0.5, 1.5, -0.5}, {-0.5, -0.5, -0.5, 1.5}};
}
[[nodiscard]] Local4 ceB() { return {{4, 2, 2, 2}, {2, 4, 2, 2}, {2, 2, 4, 2}, {2, 2, 2, 4}}; }

/// `A` of the two-tet oracle: K, C and a constant generation of 24 W/m^3 (nodal
/// 1 W on A's corners and 2 W on B's: q''' V / 4 with V = 1/6 and 1/3).
[[nodiscard]] DenseOracle twoTetOracle() {
    DenseOracle oracle(5);
    oracle.addK({0, 1, 2, 3}, keA());
    oracle.addK({1, 2, 3, 4}, keB());
    oracle.addC({0, 1, 2, 3}, ceA());
    oracle.addC({1, 2, 3, 4}, ceB());
    oracle.addQ({0, 1, 2, 3}, {1, 1, 1, 1});
    oracle.addQ({1, 2, 3, 4}, {2, 2, 2, 2});
    return oracle;
}

constexpr double kTight = 1.0e-14;

} // namespace

// ===========================================================================
// Dimensions and types
// ===========================================================================

TEST_CASE("ThermalAssembly_GlobalDimensions", "[thermal][assembly][basic]") {
    const AsmHand single = makeAsmHand(unitTetNodes(), {}, {{0, 1, 2, 3}});
    const ThermalSystem steady = assemble(single, kK, std::nullopt, noHeat(single));
    CHECK(steady.dofCount() == 4);
    CHECK(steady.conductance().rows() == 4);
    CHECK(steady.conductance().columns() == 4);
    CHECK(steady.heat().size() == 4);
    CHECK_FALSE(steady.capacity().has_value());  // a steady system asks for no C

    const ThermalSystem transient = assemble(single, kK, capacityOf(kRho, kCp), noHeat(single));
    REQUIRE(transient.capacity().has_value());
    CHECK(transient.capacity()->rows() == 4);
    CHECK(transient.capacity()->columns() == 4);
    CHECK(transient.source().mode == thermal::ThermalAnalysisMode::Transient);
    CHECK(steady.source().mode == thermal::ThermalAnalysisMode::Steady);

    // The dimension is the number of unknowns, not a handle or an element count.
    const AsmHand two = makeTwoTet();
    const ThermalSystem system = assemble(two, kK, capacityOf(kRho, kCp), noHeat(two));
    CHECK(system.dofCount() == 5);
    CHECK(system.dofCount() == two.dofs.dofCount());
    CHECK(system.conductance().rows() == 5);
    CHECK(system.capacity()->columns() == 5);
    CHECK(system.describes(two.mesh));
    CHECK(system.describes(two.dofs));
}

TEST_CASE("ThermalAssembly_ConductanceAndCapacityAreDifferentTypes", "[thermal][assembly][basic]") {
    // W/K and J/K, and neither is a bare matrix of doubles: a K cannot be handed
    // where a C is wanted.
    STATIC_CHECK_FALSE(std::is_same_v<ConductanceMatrix, CapacityMatrix>);
    STATIC_CHECK_FALSE(std::is_convertible_v<ConductanceMatrix, CapacityMatrix>);
    STATIC_CHECK_FALSE(std::is_convertible_v<CapacityMatrix, ConductanceMatrix>);
    STATIC_CHECK(std::is_same_v<decltype(std::declval<const ConductanceMatrix&>().coeff(0, 0)), ThermalConductance>);
    STATIC_CHECK(std::is_same_v<decltype(std::declval<const CapacityMatrix&>().coeff(0, 0)), thermal::HeatCapacity>);
    STATIC_CHECK(std::is_same_v<decltype(std::declval<const ThermalHeatVector&>()[0]), Power>);
    // K T is W; C dT/dt is W.
    STATIC_CHECK(decltype(ThermalConductance{} * Temperature{})::dimension == Power::dimension);
    STATIC_CHECK(decltype(thermal::HeatCapacity{} * (Temperature{} / Time{}))::dimension == Power::dimension);
}

// ===========================================================================
// One tetrahedron
// ===========================================================================

TEST_CASE("ThermalAssembly_SingleTetConductivityAnalytical", "[thermal][assembly][singletet]") {
    const AsmHand hand = makeAsmHand(unitTetNodes(), {}, {{0, 1, 2, 3}});
    const ThermalSystem system = assemble(hand, kK, std::nullopt, noHeat(hand));
    // k V = 6 * 1/6 = 1, so K = G^T G exactly as the hand matrix says.
    const Local4 expected = keA();
    double worst = 0.0;
    for (std::size_t r = 0; r < 4; ++r) {
        for (std::size_t c = 0; c < 4; ++c) {
            worst = std::max(worst, std::abs(system.conductance().coeffSi(r, c) - expected[r][c]));
            CHECK_THAT(system.conductance().coeffSi(r, c), WithinAbs(expected[r][c], 1e-15));
        }
    }
    CHECK(system.conductance().nonZeros() == 16);  // the pattern is connectivity: all 16
    WARN(std::format("single unit Tet, k = 6: K = [3 -1 -1 -1; -1 1 0 0; -1 0 1 0; -1 0 0 1] W/K, max entry error {:.2e}",
                     worst));
}

TEST_CASE("ThermalAssembly_SingleTetCapacityAnalytical", "[thermal][assembly][singletet]") {
    const AsmHand hand = makeAsmHand(unitTetNodes(), {}, {{0, 1, 2, 3}});
    const ThermalSystem system = assemble(hand, kK, capacityOf(kRho, kCp), noHeat(hand));
    REQUIRE(system.capacity().has_value());
    // rho cp V / 20 = 120 / 6 / 20 = 1, so C = [2 1 1 1; ...].
    const Local4 expected = ceA();
    double worst = 0.0;
    for (std::size_t r = 0; r < 4; ++r) {
        for (std::size_t c = 0; c < 4; ++c) {
            worst = std::max(worst, std::abs(system.capacity()->coeffSi(r, c) - expected[r][c]));
            CHECK_THAT(system.capacity()->coeffSi(r, c), WithinAbs(expected[r][c], 1e-15));
        }
    }
    WARN(std::format("single unit Tet, rho cp = 120: C = [2 1 1 1; 1 2 1 1; 1 1 2 1; 1 1 1 2] J/K, max entry error {:.2e}",
                     worst));
}

TEST_CASE("ThermalAssembly_SingleTetGenerationAnalytical", "[thermal][assembly][singletet]") {
    const AsmHand hand = makeAsmHand(unitTetNodes(), {}, {{0, 1, 2, 3}});
    const ThermalSystem system = assemble(hand, kK, std::nullopt, generationHeat(hand, 24.0));
    // q''' V / 4 = 24 / 6 / 4 = 1 W at each corner.
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK_THAT(system.heat()[i].si(), WithinAbs(1.0, 1e-15));
    }
    CHECK_THAT(system.heat().total().si(), WithinAbs(4.0, 1e-15));  // q''' V = 24 / 6
}

TEST_CASE("ThermalAssembly_SingleTetCompositeSystemAnalytical", "[thermal][assembly][singletet]") {
    const AsmHand hand = makeAsmHand(unitTetNodes(), {}, {{0, 1, 2, 3}});
    const ThermalSystem system = assemble(hand, kK, capacityOf(kRho, kCp), generationHeat(hand, 24.0));
    DenseOracle oracle(4);
    oracle.addK({0, 1, 2, 3}, keA());
    oracle.addC({0, 1, 2, 3}, ceA());
    oracle.addQ({0, 1, 2, 3}, {1, 1, 1, 1});
    CHECK(maxAbsDifference(toDense(system.conductance()), oracle.k) <= 1e-15);
    CHECK(maxAbsDifference(toDense(*system.capacity()), oracle.c) <= 1e-15);
    CHECK(maxAbsDifference(qDense(system), oracle.q) <= 1e-15);
    // K 1 = 0: conduction alone has the constant mode; nothing was added to remove it.
    const std::vector<double> ones(4, 1.0);
    const auto k1 = system.conductance().multiply(ones);
    REQUIRE(k1.has_value());
    CHECK(maxAbs(*k1) <= 1e-15);
    // 1^T C 1 = rho cp V = 120 / 6 = 20.
    const auto c1 = system.capacity()->multiply(ones);
    REQUIRE(c1.has_value());
    CHECK_THAT(sumOf(*c1), WithinAbs(20.0, 1e-14));
}

// ===========================================================================
// Two tetrahedra
// ===========================================================================

TEST_CASE("ThermalAssembly_TwoTetSharedNodeConductivity", "[thermal][assembly][twotet]") {
    const AsmHand hand = makeTwoTet();
    const ThermalSystem system = assemble(hand, kK, std::nullopt, noHeat(hand));
    // Node 0 belongs to A only, node 4 to B only, nodes 1-3 to both.
    CHECK_THAT(system.conductance().coeffSi(0, 0), WithinAbs(3.0, kTight));       // A only
    CHECK_THAT(system.conductance().coeffSi(4, 4), WithinAbs(1.5, kTight));       // B only
    CHECK_THAT(system.conductance().coeffSi(1, 1), WithinAbs(1.0 + 1.5, kTight)); // A + B
    CHECK_THAT(system.conductance().coeffSi(2, 3), WithinAbs(0.0 - 0.5, kTight)); // A: 0, B: -0.5
    CHECK_THAT(system.conductance().coeffSi(0, 4), WithinAbs(0.0, kTight));       // no element holds both
    // The pattern: A's 16 and B's 16 share the 3 x 3 block of nodes 1-3.
    CHECK(system.conductance().nonZeros() == 16 + 16 - 9);
    WARN("two Tets: K11 = A 1.0 + B 1.5 = 2.5; K23 = A 0 + B -0.5; K04 = 0 (no shared element); nnz 23");
}

TEST_CASE("ThermalAssembly_TwoTetSharedNodeCapacity", "[thermal][assembly][twotet]") {
    const AsmHand hand = makeTwoTet();
    const ThermalSystem system = assemble(hand, kK, capacityOf(kRho, kCp), noHeat(hand));
    REQUIRE(system.capacity().has_value());
    CHECK_THAT(system.capacity()->coeffSi(1, 1), WithinAbs(2.0 + 4.0, kTight));  // A 2 + B 4
    CHECK_THAT(system.capacity()->coeffSi(1, 2), WithinAbs(1.0 + 2.0, kTight));  // A 1 + B 2
    CHECK_THAT(system.capacity()->coeffSi(0, 0), WithinAbs(2.0, kTight));
    CHECK_THAT(system.capacity()->coeffSi(4, 4), WithinAbs(4.0, kTight));
    // 1^T C 1 = rho cp (V_A + V_B) = 120 * (1/6 + 1/3) = 60.
    const auto c1 = system.capacity()->multiply(std::vector<double>(5, 1.0));
    REQUIRE(c1.has_value());
    CHECK_THAT(sumOf(*c1), WithinAbs(60.0, 1e-13));
}

TEST_CASE("ThermalAssembly_TwoTetDuplicateContributionsSum", "[thermal][assembly][twotet]") {
    const AsmHand hand = makeTwoTet();
    const ThermalSystem system = assemble(hand, kK, capacityOf(kRho, kCp), generationHeat(hand, 24.0));
    // THE DUPLICATE POLICY IS SUM. K(1,1): A contributes 1.0, B contributes 1.5.
    const double a = keA()[1][1];
    const double b = keB()[0][0];
    CHECK(a == 1.0);
    CHECK(b == 1.5);
    CHECK_THAT(system.conductance().coeffSi(1, 1), WithinAbs(a + b, kTight));
    CHECK(system.conductance().coeffSi(1, 1) != a);  // not overwritten by A
    CHECK(system.conductance().coeffSi(1, 1) != b);  // not overwritten by B
    // C(1,1): 2 and 4.
    CHECK_THAT(system.capacity()->coeffSi(1, 1), WithinAbs(6.0, kTight));
    // Q(1): A's corner gives 1 W, B's gives 2 W, so 3 W, not 1 or 2.
    CHECK_THAT(system.heat()[1].si(), WithinAbs(3.0, kTight));
    CHECK_THAT(system.heat()[0].si(), WithinAbs(1.0, kTight));
    CHECK_THAT(system.heat()[4].si(), WithinAbs(2.0, kTight));
    WARN("shared K(1,1): contribution A 1.0 + contribution B 1.5 = 2.5 (actual 2.5); shared Q(1): 1 W + 2 W = 3 W");
}

TEST_CASE("ThermalAssembly_TwoTetConstantTemperatureNull", "[thermal][assembly][twotet]") {
    const AsmHand hand = makeTwoTet();
    const ThermalSystem system = assemble(hand, kK, std::nullopt, noHeat(hand));
    const auto k1 = system.conductance().multiply(std::vector<double>(5, 1.0));
    REQUIRE(k1.has_value());
    // Every row of a conduction-only K sums to zero: the constant mode is still in
    // the null space, so no epsilon, penalty or pin was added anywhere.
    CHECK(maxAbs(*k1) <= 1e-14);
    WARN(std::format("two Tets, conduction only: ||K 1||_inf = {:.2e} (constant mode preserved)", maxAbs(*k1)));
}

TEST_CASE("ThermalAssembly_SmallSystemMatchesIndependentDenseOracle", "[thermal][assembly][dense]") {
    // The oracle scatters the hand local matrices with nested loops in the test.
    // It uses no production scatter, no production sparse helper and not the
    // assembler.
    const AsmHand hand = makeTwoTet();
    const ThermalSystem system = assemble(hand, kK, capacityOf(kRho, kCp), generationHeat(hand, 24.0));
    const DenseOracle oracle = twoTetOracle();
    const double kError = maxAbsDifference(toDense(system.conductance()), oracle.k);
    const double cError = maxAbsDifference(toDense(*system.capacity()), oracle.c);
    const double qError = maxAbsDifference(qDense(system), oracle.q);
    CHECK(kError <= 1e-14);
    CHECK(cError <= 1e-14);
    CHECK(qError <= 1e-14);
    WARN(std::format("two Tets vs independent dense oracle: max |K error| {:.2e}, |C error| {:.2e}, |Q error| {:.2e}",
                     kError, cError, qError));

    // And a second system: one Tet with a convection face and a surface load.
    const AsmHand one = makeAsmHand(unitTetNodes(), {{0, 1, 2}}, {{0, 1, 2, 3}});
    std::vector<Power> heat = generationHeat(one, 24.0);
    const std::vector<ElementId> face = one.triangles;
    const auto flux = thermal::integrateSurfaceHeatFlux(one.mesh, one.dofs, face, HeatFlux::fromSi(10.0), heat);
    REQUIRE(flux.has_value());
    const std::vector<thermal::PreparedConvectionBoundary> convections{convectionOver(one, 1, face, 4.0, 300.0)};
    const ThermalSystem composite = assemble(one, kK, capacityOf(kRho, kCp), heat, convections);
    // Face (0,0,0) (1,0,0) (0,1,0): A = 1/2. Kconv = hA/12 [2 1 1; ...] = (1/6)[...];
    // Qconv = h T A / 3 = 200 W each. Flux: q'' A / 3 = 10 * 0.5 / 3 each.
    DenseOracle expected(4);
    expected.addK({0, 1, 2, 3}, keA());
    expected.addC({0, 1, 2, 3}, ceA());
    expected.addQ({0, 1, 2, 3}, {1, 1, 1, 1});
    expected.addK({0, 1, 2}, {{1.0 / 3.0, 1.0 / 6.0, 1.0 / 6.0}, {1.0 / 6.0, 1.0 / 3.0, 1.0 / 6.0}, {1.0 / 6.0, 1.0 / 6.0, 1.0 / 3.0}});
    expected.addQ({0, 1, 2}, {200.0, 200.0, 200.0});
    expected.addQ({0, 1, 2}, {10.0 * 0.5 / 3.0, 10.0 * 0.5 / 3.0, 10.0 * 0.5 / 3.0});
    CHECK(maxAbsDifference(toDense(composite.conductance()), expected.k) <= 1e-14);
    CHECK(maxAbsDifference(toDense(*composite.capacity()), expected.c) <= 1e-14);
    CHECK(maxAbsDifference(qDense(composite), expected.q) <= 1e-13);
}

// ===========================================================================
// Convection scatter
// ===========================================================================

TEST_CASE("ThermalAssembly_ConvectionMatrixScattersToCorrectDofs", "[thermal][assembly][convection]") {
    // A's exterior face (0,1,2): area 1/2, h = 4, T_inf = 300.
    const AsmHand hand = makeAsmHand(twoTetNodes(), {{0, 1, 2}}, {{0, 1, 2, 3}, {1, 2, 3, 4}});
    const auto plain = assemble(hand, kK, std::nullopt, noHeat(hand));
    const std::vector<thermal::PreparedConvectionBoundary> convections{convectionOver(hand, 1, hand.triangles, 4.0, 300.0)};
    const auto withConvection = assemble(hand, kK, std::nullopt, noHeat(hand), convections);
    // The difference is EXACTLY the embedded Kconv: (hA/12) [2 1 1; ...] at dofs
    // 0, 1, 2, and nothing anywhere else.
    const double unit = 4.0 * 0.5 / 12.0;
    for (std::size_t r = 0; r < 5; ++r) {
        for (std::size_t c = 0; c < 5; ++c) {
            const double added = withConvection.conductance().coeffSi(r, c) - plain.conductance().coeffSi(r, c);
            const bool inFace = r < 3 && c < 3;
            const double wanted = inFace ? (r == c ? 2.0 * unit : unit) : 0.0;
            CHECK_THAT(added, WithinAbs(wanted, 1e-15));
        }
    }
    // Total added conductance: h A = 2 W/K.
    CHECK_THAT(sumOf(toDense(withConvection.conductance())) - sumOf(toDense(plain.conductance())), WithinAbs(2.0, 1e-14));
    CHECK(withConvection.convectionFacetCount() == 1);
    CHECK(plain.convectionFacetCount() == 0);
}

TEST_CASE("ThermalAssembly_ConvectionSourceScattersToCorrectDofs", "[thermal][assembly][convection]") {
    const AsmHand hand = makeAsmHand(twoTetNodes(), {{0, 1, 2}}, {{0, 1, 2, 3}, {1, 2, 3, 4}});
    const std::vector<thermal::PreparedConvectionBoundary> convections{convectionOver(hand, 1, hand.triangles, 4.0, 300.0)};
    const auto system = assemble(hand, kK, std::nullopt, noHeat(hand), convections);
    // h T_inf A / 3 = 4 * 300 * 0.5 / 3 = 200 W at dofs 0, 1, 2 and zero elsewhere.
    CHECK_THAT(system.heat()[0].si(), WithinAbs(200.0, 1e-13));
    CHECK_THAT(system.heat()[1].si(), WithinAbs(200.0, 1e-13));
    CHECK_THAT(system.heat()[2].si(), WithinAbs(200.0, 1e-13));
    CHECK(system.heat()[3].si() == 0.0);
    CHECK(system.heat()[4].si() == 0.0);
    CHECK_THAT(system.heat().total().si(), WithinAbs(600.0, 1e-12));  // h T_inf A
}

TEST_CASE("ThermalAssembly_ConvectionSharedNodesAccumulate", "[thermal][assembly][convection]") {
    // Two facets on the two-Tet mesh, sharing the edge of dofs 1 and 2:
    // T1 = (0,1,2) of A, T2 = (1,2,4) of B.
    const AsmHand hand = makeAsmHand(twoTetNodes(), {{0, 1, 2}, {1, 2, 4}}, {{0, 1, 2, 3}, {1, 2, 3, 4}});
    const std::vector<thermal::PreparedConvectionBoundary> convections{convectionOver(hand, 1, hand.triangles, 4.0, 300.0)};
    const auto plain = assemble(hand, kK, std::nullopt, noHeat(hand));
    const auto system = assemble(hand, kK, std::nullopt, noHeat(hand), convections);
    const double a1 = triangleAreaByHand(at(0, 0, 0), at(1, 0, 0), at(0, 1, 0));
    const double a2 = triangleAreaByHand(at(1, 0, 0), at(0, 1, 0), at(1, 1, 1));
    CHECK_THAT(a1, WithinAbs(0.5, 1e-15));
    CHECK_THAT(a2, WithinAbs(std::sqrt(3.0) / 2.0, 1e-15));
    // Independent expectation: add each facet's hand 3 x 3 into a dense array.
    DenseOracle expected(5);
    for (const auto& [dofs, area] : {std::pair<std::vector<std::size_t>, double>{{0, 1, 2}, a1}, {{1, 2, 4}, a2}}) {
        const double unit = 4.0 * area / 12.0;
        expected.addK(dofs, {{2 * unit, unit, unit}, {unit, 2 * unit, unit}, {unit, unit, 2 * unit}});
        const double per = 4.0 * 300.0 * area / 3.0;
        expected.addQ(dofs, {per, per, per});
    }
    const auto added = [&](std::size_t r, std::size_t c) {
        return system.conductance().coeffSi(r, c) - plain.conductance().coeffSi(r, c);
    };
    // The shared nodes 1 and 2 receive BOTH facets: not either one.
    const double u1 = 4.0 * a1 / 12.0;
    const double u2 = 4.0 * a2 / 12.0;
    CHECK_THAT(added(1, 1), WithinAbs(2 * u1 + 2 * u2, 1e-14));
    CHECK_THAT(added(1, 2), WithinAbs(u1 + u2, 1e-14));
    CHECK_THAT(added(0, 0), WithinAbs(2 * u1, 1e-14));
    CHECK_THAT(added(4, 4), WithinAbs(2 * u2, 1e-14));
    CHECK_THAT(system.heat()[1].si(), WithinAbs(4.0 * 300.0 * (a1 + a2) / 3.0, 1e-12));
    // And the whole difference matches the oracle entry by entry.
    for (std::size_t r = 0; r < 5; ++r) {
        for (std::size_t c = 0; c < 5; ++c) {
            CHECK_THAT(added(r, c), WithinAbs(expected.k[r * 5 + c], 1e-14));
        }
        CHECK_THAT(system.heat()[r].si(), WithinAbs(expected.q[r], 1e-12));
    }
}

TEST_CASE("ThermalAssembly_RepeatedConvectionAddsTwice", "[thermal][assembly][convection]") {
    // Two conditions on one face are two exchanges: the system carries both.
    const AsmHand hand = makeAsmHand(unitTetNodes(), {{0, 1, 2}}, {{0, 1, 2, 3}});
    const auto once = assemble(hand, kK, std::nullopt, noHeat(hand), {convectionOver(hand, 1, hand.triangles, 4.0, 300.0)});
    const auto twice = assemble(hand, kK, std::nullopt, noHeat(hand),
                                {convectionOver(hand, 1, hand.triangles, 4.0, 300.0), convectionOver(hand, 2, hand.triangles, 4.0, 300.0)});
    const auto plain = assemble(hand, kK, std::nullopt, noHeat(hand));
    for (std::size_t r = 0; r < 4; ++r) {
        for (std::size_t c = 0; c < 4; ++c) {
            const double add1 = once.conductance().coeffSi(r, c) - plain.conductance().coeffSi(r, c);
            const double add2 = twice.conductance().coeffSi(r, c) - plain.conductance().coeffSi(r, c);
            CHECK_THAT(add2, WithinAbs(2.0 * add1, 1e-14));
        }
    }
    CHECK_THAT(twice.heat()[0].si(), WithinAbs(2.0 * once.heat()[0].si(), 1e-12));
}

TEST_CASE("ThermalAssembly_ConvectionFacetOutsideAnyElementExtendsThePattern", "[thermal][assembly][convection][sparse]") {
    // P16 makes every boundary facet a face of a tetrahedron, but the seam is handed
    // facets as data and does not assume it: a facet whose corner pairs no element
    // holds reserves its own entries, and is added, never written past the pattern.
    const AsmHand hand = makeTwoTet();
    // The same five nodes with a triangle (0,1,4): dofs 0 and 4 share no tetrahedron.
    const AsmHand odd = makeAsmHand(twoTetNodes(), {{0, 1, 4}}, {{0, 1, 2, 3}, {1, 2, 3, 4}});
    REQUIRE(odd.dofs.dofCount() == hand.dofs.dofCount());
    const std::vector<thermal::PreparedConvectionBoundary> convections{convectionOver(odd, 1, odd.triangles, 4.0, 300.0)};
    const auto plain = assemble(hand, kK, std::nullopt, noHeat(hand));
    const auto system = assemble(hand, kK, std::nullopt, noHeat(hand), convections);
    const double area = triangleAreaByHand(at(0, 0, 0), at(1, 0, 0), at(1, 1, 1));
    const double unit = 4.0 * area / 12.0;
    CHECK(plain.conductance().coeffSi(0, 4) == 0.0);
    CHECK_THAT(system.conductance().coeffSi(0, 4), WithinAbs(unit, 1e-14));
    CHECK_THAT(system.conductance().coeffSi(4, 0), WithinAbs(unit, 1e-14));
    CHECK(system.conductance().nonZeros() == plain.conductance().nonZeros() + 2);  // (0,4) and (4,0)
    CHECK(system.conductance().largestSymmetryError() == 0.0);
}

// ===========================================================================
// Heat resultants
// ===========================================================================

TEST_CASE("ThermalAssembly_HeatResultantsPreserved", "[thermal][assembly][resultant]") {
    // Surface flux on face (0,1,2), generation in both tetrahedra, convection on
    // the same face: every category lands in Q and the sum is the sum of the parts.
    const AsmHand hand = makeAsmHand(twoTetNodes(), {{0, 1, 2}}, {{0, 1, 2, 3}, {1, 2, 3, 4}});
    std::vector<Power> heat = generationHeat(hand, 24.0);  // q''' V = 24 / 2 = 12 W
    const double generated = sumOf(std::vector<double>{heat[0].si(), heat[1].si(), heat[2].si(), heat[3].si(), heat[4].si()});
    CHECK_THAT(generated, WithinAbs(12.0, 1e-14));
    const auto flux = thermal::integrateSurfaceHeatFlux(hand.mesh, hand.dofs, hand.triangles, HeatFlux::fromSi(10.0), heat);
    REQUIRE(flux.has_value());
    const double surface = 10.0 * 0.5;  // q'' A
    const std::vector<thermal::PreparedConvectionBoundary> convections{convectionOver(hand, 1, hand.triangles, 4.0, 300.0)};
    const double convectionSource = 4.0 * 300.0 * 0.5;  // h T_inf A
    const auto system = assemble(hand, kK, std::nullopt, heat, convections);
    const double expected = 12.0 + surface + convectionSource;
    CHECK_THAT(system.heat().total().si(), WithinRel(expected, 1e-13));
    WARN(std::format("combined Q: generation 12 W + surface 5 W + convection source 600 W = {} W (actual {:.12g})",
                     expected, system.heat().total().si()));

    // Each category alone.
    const auto onlyGeneration = assemble(hand, kK, std::nullopt, generationHeat(hand, 24.0));
    CHECK_THAT(onlyGeneration.heat().total().si(), WithinAbs(12.0, 1e-13));
    std::vector<Power> onlySurface(5, Power{});
    REQUIRE(thermal::integrateSurfaceHeatFlux(hand.mesh, hand.dofs, hand.triangles, HeatFlux::fromSi(10.0), onlySurface).has_value());
    CHECK_THAT(assemble(hand, kK, std::nullopt, onlySurface).heat().total().si(), WithinAbs(surface, 1e-13));
    CHECK_THAT(assemble(hand, kK, std::nullopt, noHeat(hand), convections).heat().total().si(), WithinRel(convectionSource, 1e-13));

    // Signed loads cancel: the total is the convection source alone.
    std::vector<Power> cancelled = generationHeat(hand, 24.0);
    REQUIRE(thermal::integrateVolumetricHeatSource(hand.mesh, hand.dofs, hand.tets, VolumetricHeatSource::fromSi(-24.0), cancelled).has_value());
    CHECK_THAT(assemble(hand, kK, std::nullopt, cancelled).heat().total().si(), WithinAbs(0.0, 1e-13));
}

TEST_CASE("ThermalAssembly_EmptyLoadVectorIsValid", "[thermal][assembly][resultant]") {
    // A zero Q is valid physics (a Dirichlet-driven problem), not an assembly failure.
    const AsmHand hand = makeTwoTet();
    const ThermalSystem system = assemble(hand, kK, std::nullopt, noHeat(hand));
    CHECK(system.heat().total().si() == 0.0);
    CHECK(maxAbs(qDense(system)) == 0.0);
    CHECK(system.conductance().largestMagnitude() > 0.0);  // but K is real
}

// ===========================================================================
// Symmetry, finiteness, null mode, identities
// ===========================================================================

TEST_CASE("ThermalAssembly_GlobalKSymmetric", "[thermal][assembly][symmetry]") {
    const AsmHand hand = makeAsmHand(twoTetNodes(), {{0, 1, 2}, {1, 2, 4}}, {{0, 1, 2, 3}, {1, 2, 3, 4}});
    const auto system = assemble(hand, kK, std::nullopt, noHeat(hand),
                                 {convectionOver(hand, 1, hand.triangles, 4.0, 300.0)});
    // Bit-exact: both triangles receive the same local numbers in the same order.
    CHECK(system.conductance().largestSymmetryError() == 0.0);
    WARN(std::format("K symmetry: max |Kij - Kji| = {:.2e} against max |K| = {:.4g} W/K",
                     system.conductance().largestSymmetryError(), system.conductance().largestMagnitude()));
}

TEST_CASE("ThermalAssembly_GlobalCSymmetric", "[thermal][assembly][symmetry]") {
    const AsmHand hand = makeTwoTet();
    const auto system = assemble(hand, kK, capacityOf(kRho, kCp), noHeat(hand));
    CHECK(system.capacity()->largestSymmetryError() == 0.0);
    WARN(std::format("C symmetry: max |Cij - Cji| = {:.2e} against max |C| = {:.4g} J/K",
                     system.capacity()->largestSymmetryError(), system.capacity()->largestMagnitude()));
}

TEST_CASE("ThermalAssembly_AllGlobalEntriesFinite", "[thermal][assembly][finite]") {
    const AsmHand hand = makeAsmHand(twoTetNodes(), {{0, 1, 2}}, {{0, 1, 2, 3}, {1, 2, 3, 4}});
    const auto system = assemble(hand, kK, capacityOf(kRho, kCp), generationHeat(hand, 24.0),
                                 {convectionOver(hand, 1, hand.triangles, 4.0, 300.0)});
    for (const double v : system.conductance().values()) {
        CHECK(std::isfinite(v));
    }
    for (const double v : system.capacity()->values()) {
        CHECK(std::isfinite(v));
    }
    for (const double v : system.heat().values()) {
        CHECK(std::isfinite(v));
    }
}

TEST_CASE("ThermalAssembly_PureConductionPreservesConstantNullMode", "[thermal][assembly][nullmode]") {
    // A regularisation (an epsilon on the diagonal, a penalty, a pinned unknown)
    // would move K 1 away from zero. It is zero to rounding.
    const AsmHand hand = makeTwoTet();
    const auto system = assemble(hand, kK, std::nullopt, noHeat(hand));
    const auto k1 = system.conductance().multiply(std::vector<double>(5, 1.0));
    REQUIRE(k1.has_value());
    CHECK(maxAbs(*k1) <= 1e-14 * system.conductance().largestMagnitude());
}

TEST_CASE("ThermalAssembly_ConvectionLiftsTheConstantMode", "[thermal][assembly][nullmode]") {
    const AsmHand hand = makeAsmHand(twoTetNodes(), {{0, 1, 2}}, {{0, 1, 2, 3}, {1, 2, 3, 4}});
    const auto system = assemble(hand, kK, std::nullopt, noHeat(hand), {convectionOver(hand, 1, hand.triangles, 4.0, 300.0)});
    const auto k1 = system.conductance().multiply(std::vector<double>(5, 1.0));
    REQUIRE(k1.has_value());
    CHECK_THAT(sumOf(*k1), WithinAbs(4.0 * 0.5, 1e-14));  // 1^T K 1 = h A
    CHECK(maxAbs(*k1) > 0.1);
}

TEST_CASE("ThermalAssembly_EnergyIdentities", "[thermal][assembly][identity]") {
    const AsmHand hand = makeAsmHand(twoTetNodes(), {{0, 1, 2}, {1, 2, 4}}, {{0, 1, 2, 3}, {1, 2, 3, 4}});
    const auto convections = std::vector<thermal::PreparedConvectionBoundary>{convectionOver(hand, 1, hand.triangles, 4.0, 300.0)};
    const auto system = assemble(hand, kK, capacityOf(kRho, kCp), noHeat(hand), convections);
    const std::vector<double> t{1.3, -0.7, 2.9, 0.4, -1.8};
    const std::vector<double> v{0.5, 1.5, -2.5, 3.5, -0.25};

    // T^T K T by the sparse product ...
    const auto kt = system.conductance().multiply(t);
    REQUIRE(kt.has_value());
    double sparseEnergy = 0.0;
    for (std::size_t i = 0; i < 5; ++i) {
        sparseEnergy += t[i] * (*kt)[i];
    }
    // ... against the sum of local energies, written out in the test.
    const auto local = [](const Local4& m, const std::vector<std::size_t>& g, const std::vector<double>& x) {
        double e = 0.0;
        for (std::size_t i = 0; i < g.size(); ++i) {
            for (std::size_t j = 0; j < g.size(); ++j) {
                e += x[g[i]] * m[i][j] * x[g[j]];
            }
        }
        return e;
    };
    double expected = local(keA(), {0, 1, 2, 3}, t) + local(keB(), {1, 2, 3, 4}, t);
    for (const auto& [dofs, area] : {std::pair<std::vector<std::size_t>, double>{{0, 1, 2}, 0.5}, {{1, 2, 4}, std::sqrt(3.0) / 2.0}}) {
        const double unit = 4.0 * area / 12.0;
        expected += local({{2 * unit, unit, unit}, {unit, 2 * unit, unit}, {unit, unit, 2 * unit}}, dofs, t);
    }
    CHECK_THAT(sparseEnergy, WithinRel(expected, 1e-13));
    WARN(std::format("T^T K T = {:.12g}; sum of local energies = {:.12g}; difference {:.2e}", sparseEnergy, expected,
                     std::abs(sparseEnergy - expected)));

    // v^T C v likewise.
    const auto cv = system.capacity()->multiply(v);
    REQUIRE(cv.has_value());
    double capacityEnergy = 0.0;
    for (std::size_t i = 0; i < 5; ++i) {
        capacityEnergy += v[i] * (*cv)[i];
    }
    const double expectedC = local(ceA(), {0, 1, 2, 3}, v) + local(ceB(), {1, 2, 3, 4}, v);
    CHECK_THAT(capacityEnergy, WithinRel(expectedC, 1e-13));
}

// ===========================================================================
// The sparse container
// ===========================================================================

TEST_CASE("ThermalAssembly_UsesSparseGlobalMatrices", "[thermal][assembly][sparse]") {
    // The matrices are CSR, held as three vectors; there is no dense accessor.
    const AsmHand hand = makeTwoTet();
    const ThermalSystem system = assemble(hand, kK, capacityOf(kRho, kCp), noHeat(hand));
    const ThermalSparseMatrix& k = system.conductance();
    CHECK(k.rowStart().size() == k.rows() + 1);
    CHECK(k.innerIndices().size() == k.nonZeros());
    CHECK(k.values().size() == k.nonZeros());
    CHECK(k.rowStart().front() == 0);
    CHECK(k.rowStart().back() == k.nonZeros());
    CHECK(k.nonZeros() < k.rows() * k.columns());  // 23 of 25 on this tiny coupled mesh
    for (std::size_t row = 0; row < k.rows(); ++row) {
        for (auto slot = k.rowStart()[row] + 1; slot < k.rowStart()[row + 1]; ++slot) {
            CHECK(k.innerIndices()[slot - 1] < k.innerIndices()[slot]);  // strictly ascending
        }
    }
    CHECK(system.capacity()->nonZeros() == k.nonZeros());  // same connectivity, no convection
}

TEST_CASE("ThermalAssembly_SparseMatrixLayoutIsValidated", "[thermal][assembly][sparse]") {
    using Index = ThermalSparseMatrix::Index;
    const auto problem = [](std::size_t rows, std::size_t cols, std::vector<Index> rs, std::vector<Index> in,
                            std::vector<double> v, std::optional<double> sym = std::nullopt) {
        const auto built = ThermalSparseMatrix::fromCsr(rows, cols, std::move(rs), std::move(in), std::move(v), sym);
        return built.has_value() ? std::optional<MatrixProblem>{} : std::optional<MatrixProblem>{built.error().problem};
    };
    // A valid 2 x 2.
    CHECK_FALSE(problem(2, 2, {0, 2, 3}, {0, 1, 1}, {1.0, 2.0, 3.0}).has_value());
    // Dimensions.
    CHECK(problem(2, 2, {0, 2}, {0, 1}, {1.0, 2.0}) == std::optional{MatrixProblem::DimensionsInconsistent});
    CHECK(problem(2, 2, {0, 1, 2}, {0, 1}, {1.0}) == std::optional{MatrixProblem::DimensionsInconsistent});
    // Row starts.
    CHECK(problem(2, 2, {1, 2, 2}, {0, 1}, {1.0, 2.0}) == std::optional{MatrixProblem::RowStartInvalid});
    CHECK(problem(2, 2, {0, 2, 1}, {0, 1}, {1.0, 2.0}) == std::optional{MatrixProblem::RowStartInvalid});
    CHECK(problem(2, 2, {0, 1, 3}, {0, 1}, {1.0, 2.0}) == std::optional{MatrixProblem::RowStartInvalid});
    // Columns.
    CHECK(problem(2, 2, {0, 1, 2}, {0, 2}, {1.0, 2.0}) == std::optional{MatrixProblem::ColumnOutOfRange});
    CHECK(problem(2, 2, {0, 2, 2}, {1, 1}, {1.0, 2.0}) == std::optional{MatrixProblem::ColumnsNotStrictlyAscending});  // a repeat
    CHECK(problem(2, 2, {0, 2, 2}, {1, 0}, {1.0, 2.0}) == std::optional{MatrixProblem::ColumnsNotStrictlyAscending});  // a disorder
    // Values.
    CHECK(problem(2, 2, {0, 1, 2}, {0, 1}, {1.0, std::numeric_limits<double>::quiet_NaN()}) == std::optional{MatrixProblem::NonFiniteEntry});
    CHECK(problem(2, 2, {0, 1, 2}, {0, 1}, {std::numeric_limits<double>::infinity(), 1.0}) == std::optional{MatrixProblem::NonFiniteEntry});
    for (const auto p : {MatrixProblem::DimensionsInconsistent, MatrixProblem::RowStartInvalid, MatrixProblem::ColumnOutOfRange,
                         MatrixProblem::ColumnsNotStrictlyAscending, MatrixProblem::NonFiniteEntry, MatrixProblem::NotSymmetric}) {
        CHECK(thermal::toString(p) != "unknown");
    }
}

TEST_CASE("ThermalAssembly_SparseSymmetryCheckSeesAMissingTranspose", "[thermal][assembly][symmetry]") {
    using Index = ThermalSparseMatrix::Index;
    // [[1 5] [0 1]]: (0,1) is stored and (1,0) is ABSENT. An absent entry is zero,
    // so the asymmetry is 5. A checker that only compared stored pairs would miss it.
    const auto asymmetric = ThermalSparseMatrix::fromCsr(2, 2, std::vector<Index>{0, 2, 3}, std::vector<Index>{0, 1, 1},
                                                          std::vector<double>{1.0, 5.0, 1.0});
    REQUIRE(asymmetric.has_value());
    CHECK(asymmetric->largestSymmetryError() == 5.0);
    CHECK(asymmetric->coeffSi(1, 0) == 0.0);
    // The gate refuses it, and never repairs it.
    const auto refused = ThermalSparseMatrix::fromCsr(2, 2, std::vector<Index>{0, 2, 3}, std::vector<Index>{0, 1, 1},
                                                      std::vector<double>{1.0, 5.0, 1.0}, 1.0e-13);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().problem == MatrixProblem::NotSymmetric);
    // Symmetric: passes. Absent on both sides: passes. All zero: passes.
    CHECK(ThermalSparseMatrix::fromCsr(2, 2, std::vector<Index>{0, 2, 4}, std::vector<Index>{0, 1, 0, 1},
                                        std::vector<double>{1.0, 5.0, 5.0, 1.0}, 1.0e-13).has_value());
    CHECK(ThermalSparseMatrix::fromCsr(3, 3, std::vector<Index>{0, 1, 2, 3}, std::vector<Index>{0, 1, 2},
                                        std::vector<double>{1.0, 2.0, 3.0}, 1.0e-13).has_value());
    CHECK(ThermalSparseMatrix::fromCsr(2, 2, std::vector<Index>{0, 1, 2}, std::vector<Index>{0, 1},
                                        std::vector<double>{0.0, 0.0}, 1.0e-13).has_value());
    // Upper triangle only (the mutation the brief names): rejected as asymmetric.
    const auto upperOnly = ThermalSparseMatrix::fromCsr(3, 3, std::vector<Index>{0, 3, 5, 6}, std::vector<Index>{0, 1, 2, 1, 2, 2},
                                                        std::vector<double>{2.0, -1.0, -1.0, 2.0, -1.0, 2.0}, 1.0e-13);
    REQUIRE_FALSE(upperOnly.has_value());
    CHECK(upperOnly.error().problem == MatrixProblem::NotSymmetric);
    // A non-square matrix has no symmetry.
    const auto wide = ThermalSparseMatrix::fromCsr(1, 2, std::vector<Index>{0, 1}, std::vector<Index>{0}, std::vector<double>{1.0});
    REQUIRE(wide.has_value());
    CHECK(std::isinf(wide->largestSymmetryError()));
    // multiply refuses a vector of the wrong length.
    CHECK_FALSE(wide->multiply(std::vector<double>(3, 1.0)).has_value());
}

TEST_CASE("ThermalAssembly_HeatVectorRefusesNonFinite", "[thermal][assembly][finite]") {
    CHECK(ThermalHeatVector::fromEntries({1.0, -2.0, 0.0}).has_value());
    CHECK_FALSE(ThermalHeatVector::fromEntries({1.0, std::numeric_limits<double>::quiet_NaN()}).has_value());
    CHECK_FALSE(ThermalHeatVector::fromEntries({std::numeric_limits<double>::infinity()}).has_value());
    CHECK(ThermalHeatVector::fromEntries({}).has_value());
}

// ===========================================================================
// Provenance, field by field
// ===========================================================================

TEST_CASE("ThermalAssembly_StaleReasonsNameEachFieldAlone", "[thermal][assembly][currentness]") {
    using thermal::staleReasons;
    using thermal::SystemStaleReason;
    const FaceName face{ObjectId::fromValue(3), FaceSelector{.role = FaceRole::EndCap}};
    const auto convection = [&](double h) {
        return thermal::ThermalBoundaryCondition{
            ThermalBoundaryConditionId::fromValue(1),
            thermal::ConvectionBoundary{.face = face, .coefficient = HeatTransferCoefficient::fromSi(h),
                                        .ambient = Temperature::fromSi(293.15)}};
    };
    const auto generation = [](double q) {
        return thermal::ThermalLoad{ThermalLoadId::fromValue(1),
                                    thermal::VolumetricHeatSourceLoad{.generation = VolumetricHeatSource::fromSi(q)}};
    };
    thermal::ThermalSystemSource base;
    base.body = ObjectId::fromValue(3);
    base.mesh = meshing::MeshStamp{meshing::MeshId::fromValue(7), 1};
    base.nodeCount = 9;
    base.geometry = meshing::GeometryRevision{4};
    base.mode = thermal::ThermalAnalysisMode::Transient;
    base.material = MaterialId::fromValue(5);
    base.materialRevision = 6;
    base.loads = {generation(1.0)};
    base.convections = {convection(25.0)};

    CHECK(staleReasons(base, base).empty());
    const auto only = [&](const auto& edit) {
        thermal::ThermalSystemSource other = base;
        edit(other);
        return staleReasons(base, other);
    };
    using Reasons = std::vector<SystemStaleReason>;
    CHECK(only([](auto& s) { s.body = ObjectId::fromValue(4); }) == Reasons{SystemStaleReason::Body});
    CHECK(only([](auto& s) { s.mesh.generation = 2; }) == Reasons{SystemStaleReason::Mesh});
    CHECK(only([](auto& s) { s.mesh.mesh = meshing::MeshId::fromValue(8); }) == Reasons{SystemStaleReason::Mesh});
    CHECK(only([](auto& s) { s.nodeCount = 10; }) == Reasons{SystemStaleReason::Mesh});
    CHECK(only([](auto& s) { s.geometry = meshing::GeometryRevision{5}; }) == Reasons{SystemStaleReason::Geometry});
    CHECK(only([](auto& s) { s.mode = thermal::ThermalAnalysisMode::Steady; }) == Reasons{SystemStaleReason::Mode});
    CHECK(only([](auto& s) { s.material = MaterialId::fromValue(6); }) == Reasons{SystemStaleReason::Material});
    CHECK(only([](auto& s) { s.materialRevision = 7; }) == Reasons{SystemStaleReason::Material});
    CHECK(only([&](auto& s) { s.loads = {generation(2.0)}; }) == Reasons{SystemStaleReason::Loads});
    CHECK(only([&](auto& s) { s.loads.clear(); }) == Reasons{SystemStaleReason::Loads});
    CHECK(only([&](auto& s) { s.convections = {convection(26.0)}; }) == Reasons{SystemStaleReason::Convections});
    CHECK(only([&](auto& s) { s.convections.clear(); }) == Reasons{SystemStaleReason::Convections});
    // Everything at once, in the enum's order.
    thermal::ThermalSystemSource all = base;
    all.body = ObjectId::fromValue(4);
    all.mesh.generation = 2;
    all.geometry = meshing::GeometryRevision{5};
    all.mode = thermal::ThermalAnalysisMode::Steady;
    all.materialRevision = 7;
    all.loads.clear();
    all.convections.clear();
    CHECK(staleReasons(base, all) ==
          Reasons{SystemStaleReason::Body, SystemStaleReason::Mesh, SystemStaleReason::Geometry, SystemStaleReason::Mode,
                  SystemStaleReason::Material, SystemStaleReason::Loads, SystemStaleReason::Convections});
}

TEST_CASE("ThermalAssembly_SameBuilderLaterSnapshotIsNotTheSameMesh", "[thermal][assembly][currentness]") {
    // A MeshStamp identifies the BUILDER and its generation, and `MeshBuilder::build`
    // is a snapshot: a mesh grown after a snapshot carries the SAME stamp with
    // another node count. The system records the count for exactly this case.
    meshing::MeshBuilder builder;
    std::vector<NodeId> ids;
    for (const auto& [handle, position] : unitTetNodes()) {
        const Result<NodeId> added = builder.addNode(NodeId::fromValue(handle), position);
        REQUIRE(added.has_value());
        ids.push_back(*added);
    }
    REQUIRE(builder.addTetrahedron({ids[0], ids[1], ids[2], ids[3]}, RegionId::fromValue(1)).has_value());
    const meshing::Mesh first = builder.build();
    const Result<ThermalDofMap> firstMap = thermal::buildThermalDofMap(first);
    REQUIRE(firstMap.has_value());
    const std::vector<Power> heat(4, Power{});
    const ThermalAssemblyData data{.mesh = first,
                                   .map = *firstMap,
                                   .conductivity = ThermalConductivity::fromSi(kK),
                                   .capacity = std::nullopt,
                                   .heatInput = heat,
                                   .convections = {}};
    auto built = thermal::assembleThermalSystemFrom(data, ThermalSystemSource{});
    REQUIRE(built.has_value());

    REQUIRE(builder.addNode(NodeId::fromValue(9), at(2, 2, 2)).has_value());
    const meshing::Mesh second = builder.build();
    REQUIRE(second.stamp() == first.stamp());          // the stamp cannot tell them apart
    REQUIRE(second.nodes().size() == first.nodes().size() + 1);
    CHECK(second.owns(built->mesh()));                  // ...which is the point
    CHECK(built->describes(first));
    CHECK_FALSE(built->describes(second));              // the recorded node count can
    CHECK_FALSE(built->describes(*thermal::buildThermalDofMap(second)));
}

// ===========================================================================
// Refusals
// ===========================================================================

TEST_CASE("ThermalAssembly_SeamRefusals", "[thermal][assembly][invalid]") {
    const AsmHand hand = makeTwoTet();
    const auto assembleWith = [&](const AsmHand& h, const ThermalDofMap& map, double k, std::optional<ThermalCapacity> capacity,
                                  const std::vector<Power>& heat,
                                  const std::vector<thermal::PreparedConvectionBoundary>& convections) {
        const ThermalAssemblyData data{.mesh = h.mesh, .map = map, .conductivity = ThermalConductivity::fromSi(k),
                                       .capacity = capacity, .heatInput = heat, .convections = convections};
        return thermal::assembleThermalSystemFrom(data, ThermalSystemSource{});
    };
    const auto problemOf = [](const auto& built) { return built.has_value() ? std::optional<AssemblyProblem>{} : std::optional<AssemblyProblem>{built.error().problem}; };

    SECTION("a numbering built for another mesh") {
        const AsmHand other = makeAsmHand(unitTetNodes(), {}, {{0, 1, 2, 3}});
        CHECK(problemOf(assembleWith(hand, other.dofs, kK, std::nullopt, noHeat(hand), {})) == std::optional{AssemblyProblem::DofMapMismatch});
    }
    SECTION("a mesh with no tetrahedron") {
        const AsmHand flat = makeAsmHand(unitTetNodes(), {{0, 1, 2}}, {});
        CHECK(problemOf(assembleWith(flat, flat.dofs, kK, std::nullopt, noHeat(flat), {})) == std::optional{AssemblyProblem::MeshHasNoElements});
    }
    SECTION("a heat vector of the wrong length") {
        const std::vector<Power> shortVector(3, Power{});
        const auto built = assembleWith(hand, hand.dofs, kK, std::nullopt, shortVector, {});
        CHECK(problemOf(built) == std::optional{AssemblyProblem::LoadVectorMismatch});
        CHECK_THAT(built.error().error.message, ContainsSubstring("5 unknowns"));
    }
    SECTION("a convection facet naming an unknown the numbering does not have") {
        const AsmHand big = makeAsmHand(Nodes{{1, at(0, 0, 0)}, {2, at(1, 0, 0)}, {3, at(0, 1, 0)}, {4, at(0, 0, 1)}, {5, at(1, 1, 1)},
                                              {6, at(2, 0, 0)}, {7, at(0, 2, 0)}},
                                        {{4, 5, 6}}, {{0, 1, 2, 3}});
        const std::vector<thermal::PreparedConvectionBoundary> foreign{convectionOver(big, 1, big.triangles, 4.0, 300.0)};
        // Presented to the 5-unknown two-Tet system: dofs 4, 5, 6 do not all exist.
        CHECK(problemOf(assembleWith(hand, hand.dofs, kK, std::nullopt, noHeat(hand), foreign)) ==
              std::optional{AssemblyProblem::BoundaryDofOutOfRange});
    }
    SECTION("an inverted tetrahedron is refused and named, never skipped") {
        const AsmHand inverted = makeAsmHand(unitTetNodes(), {}, {{0, 2, 1, 3}});
        const auto built = assembleWith(inverted, inverted.dofs, kK, std::nullopt, noHeat(inverted), {});
        CHECK(problemOf(built) == std::optional{AssemblyProblem::ElementRejected});
        CHECK_THAT(built.error().error.message, ContainsSubstring("element"));
    }
    SECTION("a degenerate tetrahedron") {
        const AsmHand flat = makeAsmHand(Nodes{{1, at(0, 0, 0)}, {2, at(1, 0, 0)}, {3, at(0, 1, 0)}, {4, at(1, 1, 0)}}, {}, {{0, 1, 2, 3}});
        CHECK(problemOf(assembleWith(flat, flat.dofs, kK, std::nullopt, noHeat(flat), {})) == std::optional{AssemblyProblem::ElementRejected});
    }
    SECTION("an unusable conductivity and an unusable capacity") {
        CHECK(problemOf(assembleWith(hand, hand.dofs, 0.0, std::nullopt, noHeat(hand), {})) == std::optional{AssemblyProblem::ElementRejected});
        CHECK(problemOf(assembleWith(hand, hand.dofs, std::numeric_limits<double>::quiet_NaN(), std::nullopt, noHeat(hand), {})) ==
              std::optional{AssemblyProblem::ElementRejected});
        CHECK(problemOf(assembleWith(hand, hand.dofs, kK, capacityOf(0.0, 1.0), noHeat(hand), {})) == std::optional{AssemblyProblem::ElementRejected});
    }
    SECTION("a sum that overflows is refused, never accepted") {
        // A tetrahedron of edge 10 m has a face of 50 m^2. With h = 1e307 each
        // convection diagonal is 8.3e307 (the ambient is tiny so the SOURCE stays finite), so two copies are finite and three are
        // not: the SUM overflows although every contribution is representable.
        const Nodes big{{1, at(0, 0, 0)}, {2, at(10, 0, 0)}, {3, at(0, 10, 0)}, {4, at(0, 0, 10)}};
        const AsmHand one = makeAsmHand(big, {{0, 1, 2}}, {{0, 1, 2, 3}});
        std::vector<thermal::PreparedConvectionBoundary> huge;
        for (std::uint64_t id = 1; id <= 3; ++id) {
            huge.push_back(convectionOver(one, id, one.triangles, 1.0e307, 1.0e-3));
        }
        const auto two = assembleWith(one, one.dofs, kK, std::nullopt, noHeat(one), {huge[0], huge[1]});
        INFO((two.has_value() ? std::string{} : two.error().error.message));
        CHECK(two.has_value());
        const auto built = assembleWith(one, one.dofs, kK, std::nullopt, noHeat(one), huge);
        CHECK(problemOf(built) == std::optional{AssemblyProblem::NonFiniteSystem});
    }
    SECTION("every problem has a name") {
        for (const auto p : {AssemblyProblem::DofMapMismatch, AssemblyProblem::LoadSourceMismatch, AssemblyProblem::BoundarySourceMismatch,
                             AssemblyProblem::MaterialUnavailable, AssemblyProblem::MaterialSourceMismatch, AssemblyProblem::MeshHasNoElements,
                             AssemblyProblem::LoadVectorMismatch, AssemblyProblem::BoundaryDofOutOfRange, AssemblyProblem::ElementRejected,
                             AssemblyProblem::NonFiniteSystem, AssemblyProblem::InconsistentSystem}) {
            CHECK(thermal::toString(p) != "unknown");
        }
    }
}

// ===========================================================================
// Local order, sparse ids, disconnected components
// ===========================================================================

TEST_CASE("ThermalAssembly_LocalOrderIsPreservedUnderEvenPermutation", "[thermal][assembly][permutation]") {
    // The same physical tetrahedron listed with its first three corners rotated (an
    // even permutation, so still positively oriented) scatters the SAME global
    // matrix: the local matrix and the local rows are permuted together, and nothing
    // sorts either.
    const AsmHand direct = makeAsmHand(unitTetNodes(), {}, {{0, 1, 2, 3}});
    const AsmHand rotated = makeAsmHand(unitTetNodes(), {}, {{1, 2, 0, 3}});
    const auto a = assemble(direct, kK, capacityOf(kRho, kCp), noHeat(direct));
    const auto b = assemble(rotated, kK, capacityOf(kRho, kCp), noHeat(rotated));
    CHECK(maxAbsDifference(toDense(a.conductance()), toDense(b.conductance())) <= 1e-15);
    CHECK(maxAbsDifference(toDense(*a.capacity()), toDense(*b.capacity())) <= 1e-15);
    // Also for the tetrahedra of the two-Tet mesh.
    const AsmHand two = makeTwoTet();
    const AsmHand twoRotated = makeAsmHand(twoTetNodes(), {{0, 1, 2}}, {{1, 2, 0, 3}, {2, 3, 1, 4}});
    const auto c = assemble(two, kK, std::nullopt, noHeat(two));
    const auto d = assemble(twoRotated, kK, std::nullopt, noHeat(twoRotated));
    CHECK(maxAbsDifference(toDense(c.conductance()), toDense(d.conductance())) <= 1e-14);
}

TEST_CASE("ThermalAssembly_SparseNodeIds", "[thermal][assembly][sparse]") {
    // The same two tetrahedra with handles 7, 42, 104, 9001, 500000. The unknowns
    // are the RANK of the handle, so the matrices are identical to the dense-handle
    // case, and the dimension is five.
    const Nodes sparse{{7, at(0, 0, 0)}, {42, at(1, 0, 0)}, {104, at(0, 1, 0)}, {9001, at(0, 0, 1)}, {500000, at(1, 1, 1)}};
    const AsmHand hand = makeAsmHand(sparse, {{0, 1, 2}}, {{0, 1, 2, 3}, {1, 2, 3, 4}});
    const auto system = assemble(hand, kK, capacityOf(kRho, kCp), generationHeat(hand, 24.0));
    const DenseOracle oracle = twoTetOracle();
    CHECK(system.dofCount() == 5);
    CHECK(maxAbsDifference(toDense(system.conductance()), oracle.k) <= 1e-14);
    CHECK(maxAbsDifference(toDense(*system.capacity()), oracle.c) <= 1e-14);
    CHECK(maxAbsDifference(qDense(system), oracle.q) <= 1e-14);
    // No row is a handle.
    CHECK(system.conductance().rows() < 500000);
    WARN("sparse ids [7 42 104 9001 500000]: matrices 5 x 5 over unknowns [0 1 2 3 4] (not 500001)");
}

TEST_CASE("ThermalAssembly_LargeSparseNodeIdDoesNotControlMatrixSize", "[thermal][assembly][sparse]") {
    constexpr NodeId::ValueType kLarge = 4294967294ULL;
    const Nodes nodes{{1, at(0, 0, 0)}, {2, at(1, 0, 0)}, {3, at(0, 1, 0)}, {kLarge, at(0, 0, 1)}};
    const AsmHand hand = makeAsmHand(nodes, {}, {{0, 1, 2, 3}});
    const auto system = assemble(hand, kK, capacityOf(kRho, kCp), noHeat(hand));
    CHECK(system.dofCount() == 4);
    CHECK(system.conductance().rows() == 4);
    CHECK(system.conductance().nonZeros() == 16);
    DenseOracle oracle(4);
    oracle.addK({0, 1, 2, 3}, keA());
    CHECK(maxAbsDifference(toDense(system.conductance()), oracle.k) <= 1e-15);
    WARN("max NodeId 4 294 967 294, 4 nodes: matrices 4 x 4, 16 entries");
}

TEST_CASE("ThermalAssembly_DisconnectedComponentsRemainBlockDiagonal", "[thermal][assembly][sparse]") {
    // Two unit tetrahedra that share nothing, the second translated by 5.
    const Nodes nodes{{1, at(0, 0, 0)}, {2, at(1, 0, 0)}, {3, at(0, 1, 0)}, {4, at(0, 0, 1)},
                      {5, at(5, 0, 0)}, {6, at(6, 0, 0)}, {7, at(5, 1, 0)}, {8, at(5, 0, 1)}};
    const AsmHand hand = makeAsmHand(nodes, {}, {{0, 1, 2, 3}, {4, 5, 6, 7}});
    const auto system = assemble(hand, kK, capacityOf(kRho, kCp), noHeat(hand));
    CHECK(system.dofCount() == 8);
    // Block diagonal: nothing couples the components, in the VALUES and in the PATTERN.
    for (std::size_t r = 0; r < 4; ++r) {
        for (std::size_t c = 4; c < 8; ++c) {
            CHECK(system.conductance().coeffSi(r, c) == 0.0);
            CHECK(system.conductance().coeffSi(c, r) == 0.0);
            CHECK(system.capacity()->coeffSi(r, c) == 0.0);
        }
    }
    CHECK(system.conductance().nonZeros() == 32);  // 16 + 16, no cross entries stored
    // And each block is the unit tetrahedron's own.
    DenseOracle oracle(8);
    oracle.addK({0, 1, 2, 3}, keA());
    oracle.addK({4, 5, 6, 7}, keA());
    CHECK(maxAbsDifference(toDense(system.conductance()), oracle.k) <= 1e-15);
}

// ===========================================================================
// Large sparse smoke
// ===========================================================================

namespace {

/// A structured tetrahedral grid of `side` nodes per direction, spacing @p h: six
/// tetrahedra per cell (Kuhn), each oriented positively by a test-side determinant.
[[nodiscard]] AsmHand makeGrid(std::size_t side, double h) {
    const auto index = [side](std::size_t i, std::size_t j, std::size_t k) { return (k * side + j) * side + i; };
    Nodes nodes;
    nodes.reserve(side * side * side);
    for (std::size_t k = 0; k < side; ++k) {
        for (std::size_t j = 0; j < side; ++j) {
            for (std::size_t i = 0; i < side; ++i) {
                nodes.emplace_back(static_cast<NodeId::ValueType>(index(i, j, k) + 1),
                                   at(h * static_cast<double>(i), h * static_cast<double>(j), h * static_cast<double>(k)));
            }
        }
    }
    const auto position = [&](std::size_t n) { return nodes[n].second; };
    const auto determinant = [&](std::array<std::size_t, 4> t) {
        const Point3D a = position(t[0]);
        const Point3D b = position(t[1]);
        const Point3D c = position(t[2]);
        const Point3D d = position(t[3]);
        const double e1x = b.x.si() - a.x.si();
        const double e1y = b.y.si() - a.y.si();
        const double e1z = b.z.si() - a.z.si();
        const double e2x = c.x.si() - a.x.si();
        const double e2y = c.y.si() - a.y.si();
        const double e2z = c.z.si() - a.z.si();
        const double e3x = d.x.si() - a.x.si();
        const double e3y = d.y.si() - a.y.si();
        const double e3z = d.z.si() - a.z.si();
        return e1x * (e2y * e3z - e2z * e3y) + e1y * (e2z * e3x - e2x * e3z) + e1z * (e2x * e3y - e2y * e3x);
    };
    std::vector<std::array<std::size_t, 4>> tets;
    tets.reserve(6 * (side - 1) * (side - 1) * (side - 1));
    for (std::size_t k = 0; k + 1 < side; ++k) {
        for (std::size_t j = 0; j + 1 < side; ++j) {
            for (std::size_t i = 0; i + 1 < side; ++i) {
                const std::size_t n000 = index(i, j, k);
                const std::size_t n100 = index(i + 1, j, k);
                const std::size_t n010 = index(i, j + 1, k);
                const std::size_t n110 = index(i + 1, j + 1, k);
                const std::size_t n001 = index(i, j, k + 1);
                const std::size_t n101 = index(i + 1, j, k + 1);
                const std::size_t n011 = index(i, j + 1, k + 1);
                const std::size_t n111 = index(i + 1, j + 1, k + 1);
                const std::array<std::array<std::size_t, 4>, 6> cell{{{n000, n100, n110, n111}, {n000, n100, n101, n111},
                                                                      {n000, n010, n110, n111}, {n000, n010, n011, n111},
                                                                      {n000, n001, n101, n111}, {n000, n001, n011, n111}}};
                for (std::array<std::size_t, 4> t : cell) {
                    if (determinant(t) < 0.0) {
                        std::swap(t[2], t[3]);
                    }
                    tets.push_back(t);
                }
            }
        }
    }
    return makeAsmHand(nodes, {}, tets);
}

} // namespace

TEST_CASE("ThermalAssembly_LargeMeshSparseSmoke", "[thermal][assembly][sparse][smoke]") {
    // 22^3 = 10648 unknowns. A dense K would be 113 million entries (907 MB); the
    // sparse one is a few hundred thousand. Nothing is solved.
    constexpr std::size_t kSide = 22;
    constexpr double kSpacing = 0.001;
    const AsmHand hand = makeGrid(kSide, kSpacing);
    const std::size_t n = kSide * kSide * kSide;
    REQUIRE(hand.dofs.dofCount() == n);
    REQUIRE(hand.tets.size() == 6 * 21 * 21 * 21);

    const auto begin = std::chrono::steady_clock::now();
    const auto system = assemble(hand, 45.0, capacityOf(7850.0, 460.0), generationHeat(hand, 1.0e6));
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();

    CHECK(system.dofCount() == n);
    CHECK(system.conductance().rows() == n);
    CHECK(system.capacity()->rows() == n);
    CHECK(system.conductance().nonZeros() > n);
    const double ratio = static_cast<double>(system.conductance().nonZeros()) / (static_cast<double>(n) * static_cast<double>(n));
    CHECK(ratio < 0.01);  // far below dense; this is a sparse design
    CHECK(system.conductance().largestSymmetryError() == 0.0);
    CHECK(system.capacity()->largestSymmetryError() == 0.0);
    for (const double v : system.conductance().values()) {
        REQUIRE(std::isfinite(v));
    }
    // K 1 = 0, 1^T C 1 = rho cp V, sum Q = q''' V.
    const auto k1 = system.conductance().multiply(std::vector<double>(n, 1.0));
    REQUIRE(k1.has_value());
    CHECK(maxAbs(*k1) <= 1e-12 * system.conductance().largestMagnitude());
    const double volume = std::pow(kSpacing * 21.0, 3);
    const auto c1 = system.capacity()->multiply(std::vector<double>(n, 1.0));
    REQUIRE(c1.has_value());
    CHECK_THAT(sumOf(*c1), WithinRel(7850.0 * 460.0 * volume, 1e-10));
    CHECK_THAT(system.heat().total().si(), WithinRel(1.0e6 * volume, 1e-10));
    WARN(std::format("sparse smoke: grid {}^3, Ndof {}, Ntet {}, nnz(K) {}, nnz(C) {}, Ndof^2 {}, nnz/Ndof^2 {:.3e}, "
                     "assembled in {:.2f} s, all finite, symmetric",
                     kSide, n, hand.tets.size(), system.conductance().nonZeros(), system.capacity()->nonZeros(), n * n,
                     ratio, seconds));
}

// ===========================================================================
// Determinism
// ===========================================================================

TEST_CASE("ThermalAssembly_RepeatedAssemblyDeterministic", "[thermal][assembly][determinism]") {
    const AsmHand hand = makeAsmHand(twoTetNodes(), {{0, 1, 2}, {1, 2, 4}}, {{0, 1, 2, 3}, {1, 2, 3, 4}});
    std::vector<Power> heat = generationHeat(hand, 17.5);
    REQUIRE(thermal::integrateSurfaceHeatFlux(hand.mesh, hand.dofs, hand.triangles, HeatFlux::fromSi(-3.25), heat).has_value());
    const std::vector<thermal::PreparedConvectionBoundary> convections{convectionOver(hand, 1, hand.triangles, 12.5, 288.15)};
    const auto first = assemble(hand, 45.0, capacityOf(7850.0, 460.0), heat, convections);
    int differences = 0;
    for (int i = 0; i < 50; ++i) {
        if (!(assemble(hand, 45.0, capacityOf(7850.0, 460.0), heat, convections) == first)) {
            ++differences;
        }
    }
    CHECK(differences == 0);
    WARN(std::format("determinism: 50 repeated assemblies of a two-Tet system with convection, loads and capacity, "
                     "{} bitwise differences in K, C, Q, the element order or the provenance",
                     differences));
}

TEST_CASE("ThermalAssembly_BitFingerprintForCrossPresetComparison", "[thermal][assembly][determinism]") {
    // A 64-bit FNV-1a hash over the bit patterns of the CSR arrays and values of K
    // and C and the entries of Q, printed so the three presets can be compared
    // exactly. Equality across presets is MEASURED, not promised.
    const auto hashOf = [](const ThermalSystem& s) {
        std::uint64_t hash = 14695981039346656037ULL;
        const auto mixBits = [&hash](std::uint64_t bits) {
            for (int i = 0; i < 8; ++i) {
                hash ^= (bits >> (8 * i)) & 0xFFU;
                hash *= 1099511628211ULL;
            }
        };
        const auto mixMatrix = [&](const thermal::ThermalSparseMatrix& m) {
            for (const auto v : m.rowStart()) {
                mixBits(v);
            }
            for (const auto v : m.innerIndices()) {
                mixBits(v);
            }
            for (const double v : m.values()) {
                mixBits(std::bit_cast<std::uint64_t>(v));
            }
        };
        mixMatrix(s.conductance());
        if (s.capacity().has_value()) {
            mixMatrix(*s.capacity());
        }
        for (const double v : s.heat().values()) {
            mixBits(std::bit_cast<std::uint64_t>(v));
        }
        return hash;
    };
    const AsmHand hand = makeAsmHand(twoTetNodes(), {{0, 1, 2}, {1, 2, 4}}, {{0, 1, 2, 3}, {1, 2, 3, 4}});
    std::vector<Power> heat = generationHeat(hand, 17.5);
    const std::vector<thermal::PreparedConvectionBoundary> convections{convectionOver(hand, 1, hand.triangles, 12.5, 288.15)};
    const auto small = assemble(hand, 45.0, capacityOf(7850.0, 460.0), heat, convections);
    CHECK(hashOf(small) == hashOf(assemble(hand, 45.0, capacityOf(7850.0, 460.0), heat, convections)));
    const AsmHand grid = makeGrid(9, 0.002);
    const auto large = assemble(grid, 45.0, capacityOf(7850.0, 460.0), generationHeat(grid, 2.0e5));
    CHECK(hashOf(large) == hashOf(assemble(grid, 45.0, capacityOf(7850.0, 460.0), generationHeat(grid, 2.0e5))));
    WARN(std::format("bit fingerprint, two-Tet system with convection: {:016x}", hashOf(small)));
    WARN(std::format("bit fingerprint, 9^3 grid ({} unknowns): {:016x}", grid.dofs.dofCount(), hashOf(large)));
}
