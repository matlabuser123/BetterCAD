// P18-BC-001: canonical thermal boundary conditions, hand-checked.
//
// WHAT THESE TESTS ARE FOR. Two kinds of claim, and they need different
// evidence. The MATHEMATICAL claims -- the consistent Robin matrix
// (h A / 12) [2 1 1; 1 2 1; 1 1 2] and source (h T_inf A / 3) [1 1 1], their
// signs, their scaling, one constraint per unknown, a contradiction refused and
// an agreement collapsed -- are checked against values written out BY HAND in the
// test, through the kernel and the seams with hand-built meshes. The AUTHORITY
// claims -- a facet, node or unknown is never boundary-condition authority, an
// unresolved target is refused rather than bound to a neighbour, a prepared
// boundary dies with its mesh even when every count and every numeric id is
// unchanged -- are checked against REAL P16 meshes in ThermalBoundaryMappedTests.cpp.
//
// THE ROBIN MATRIX IS COMPARED WHOLE. The centroid rule (every entry h A / 9) and
// the lumped rule ((h A / 3) I) both have the correct row sums h A / 3, so a
// row-sum or total test cannot see them. `isConsistentRobin` below compares every
// entry, and the traps are tested by feeding it the wrong matrices.

#include "ThermalBoundaryTestSupport.hpp"

#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <tuple>
#include <type_traits>

using namespace bctest;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using thermal::BoundaryFailure;
using thermal::BoundaryProblem;
using thermal::ConvectionFacet;
using thermal::ConvectionProblem;
using thermal::TemperatureConflict;
using thermal::TemperatureRequest;
using thermal::Tri3Convection;
using thermal::Tri3Temperatures;

namespace {

/// The brief's triangle: area 3 m^2, h = 4 W/(m^2 K), T_inf = 300 K. hA/12 = 1.
[[nodiscard]] Tri3Convection reference() {
    Result<Tri3Convection> term = thermal::computeTri3Convection(
        Area::fromSi(3.0), HeatTransferCoefficient::fromSi(4.0), Temperature::fromSi(300.0));
    INFO((term.has_value() ? std::string{} : term.error().message));
    REQUIRE(term.has_value());
    return std::move(*term);
}

[[nodiscard]] Tri3Convection termFor(double area, double h, double ambient) {
    Result<Tri3Convection> term = thermal::computeTri3Convection(
        Area::fromSi(area), HeatTransferCoefficient::fromSi(h), Temperature::fromSi(ambient));
    INFO((term.has_value() ? std::string{} : term.error().message));
    REQUIRE(term.has_value());
    return std::move(*term);
}

[[nodiscard]] Tri3Temperatures uniform(double kelvin) {
    return Tri3Temperatures{Temperature::fromSi(kelvin), Temperature::fromSi(kelvin), Temperature::fromSi(kelvin)};
}

/// The nine entries of a matrix given as a function, row-major.
using Matrix3 = std::array<double, 9>;

[[nodiscard]] Matrix3 entriesOf(const Tri3Convection& term) {
    Matrix3 m{};
    for (std::size_t r = 0; r < 3; ++r) {
        for (std::size_t c = 0; c < 3; ++c) {
            m[r * 3 + c] = term(r, c).si();
        }
    }
    return m;
}

/// THE WHOLE MATRIX, against the hand-written closed form (h A / 12) [2 1 1; 1 2 1;
/// 1 1 2], to @p relative. A row-sum test is not this: see the file comment.
[[nodiscard]] bool isConsistentRobin(const Matrix3& m, double h, double area, double relative) {
    const double unit = h * area / 12.0;
    const std::array<double, 9> expected{2 * unit, unit, unit, unit, 2 * unit, unit, unit, unit, 2 * unit};
    for (std::size_t i = 0; i < 9; ++i) {
        const double scale = std::max(std::abs(expected[i]), 1e-300);
        if (std::abs(m[i] - expected[i]) > relative * scale) {
            return false;
        }
    }
    return true;
}

} // namespace

// ===========================================================================
// The canonical definition
// ===========================================================================

TEST_CASE("ThermalBC_PrescribedTemperatureDefinition", "[thermal][bc][definition]") {
    const FaceName face{ObjectId::fromValue(3), FaceSelector{.role = FaceRole::EndCap}};
    const thermal::PrescribedTemperature held{.face = face, .value = 350_K};
    // Kelvin, and stored as kelvin: 350 is not converted to 76.85.
    CHECK(held.value.si() == 350.0);
    CHECK((300_K).si() == 300.0);

    const ThermalBoundaryCondition bc{bid(7), held};
    CHECK(bc.id() == bid(7));
    CHECK(bc.kind() == thermal::ThermalBoundaryConditionKind::PrescribedTemperature);
    CHECK(bc.face() == face);
    CHECK(bc.prescribedTemperature() != nullptr);
    CHECK(bc.convection() == nullptr);
    CHECK(thermal::validate(bc).has_value());

    // 0 K is the limit of the scale and is accepted; below it is not a temperature.
    CHECK(thermal::validate(temperatureBc(1, face, 0.0)).has_value());
    CHECK_FALSE(thermal::validate(temperatureBc(1, face, -1.0)).has_value());
    CHECK_FALSE(thermal::validate(temperatureBc(1, face, kNaN)).has_value());
    CHECK_FALSE(thermal::validate(temperatureBc(1, face, kInf)).has_value());
    CHECK_FALSE(thermal::validate(temperatureBc(1, face, -kInf)).has_value());

    // The canonical record holds a face and a temperature and NOTHING ELSE: it is
    // the size of exactly those two.
    struct Mirror {
        FaceName face;
        Temperature value;
    };
    STATIC_CHECK(sizeof(thermal::PrescribedTemperature) == sizeof(Mirror));
    STATIC_CHECK(std::is_same_v<decltype(thermal::PrescribedTemperature::value), Temperature>);
}

TEST_CASE("ThermalBC_ConvectionDefinition", "[thermal][bc][definition]") {
    const FaceName face{ObjectId::fromValue(3), FaceSelector{.role = FaceRole::EndCap}};
    const ThermalBoundaryCondition bc = convectionBc(2, face, 25.0, 293.15);
    CHECK(bc.kind() == thermal::ThermalBoundaryConditionKind::Convection);
    REQUIRE(bc.convection() != nullptr);
    CHECK(bc.convection()->coefficient.si() == 25.0);
    CHECK(bc.convection()->ambient.si() == 293.15);
    CHECK(bc.face() == face);
    CHECK(thermal::validate(bc).has_value());

    struct Mirror {
        FaceName face;
        HeatTransferCoefficient coefficient;
        Temperature ambient;
    };
    STATIC_CHECK(sizeof(thermal::ConvectionBoundary) == sizeof(Mirror));
    // h is a HeatTransferCoefficient and the ambient a Temperature: neither is a
    // double and they are not interchangeable (compile-fail cases pin that).
    STATIC_CHECK(std::is_same_v<decltype(thermal::ConvectionBoundary::coefficient), HeatTransferCoefficient>);
    STATIC_CHECK(std::is_same_v<decltype(thermal::ConvectionBoundary::ambient), Temperature>);
}

TEST_CASE("ThermalBC_CanonicalGeometryReferenceIsAFaceNameAndNothingMeshLocal", "[thermal][bc][definition]") {
    // Both conditions target a FaceName, and the only other members are physical
    // numbers. No NodeId, ElementId, facet or ThermalDofIndex can be a member:
    // the mirror structs fix the size, and the compile-fail group names each
    // absent member.
    STATIC_CHECK(std::is_same_v<decltype(thermal::PrescribedTemperature::face), FaceName>);
    STATIC_CHECK(std::is_same_v<decltype(thermal::ConvectionBoundary::face), FaceName>);
    const ThermalBoundaryCondition bc = temperatureBc(3, FaceName{ObjectId::fromValue(1), FaceSelector{.role = FaceRole::StartCap}}, 300.0);
    STATIC_CHECK(std::is_same_v<decltype(bc.face()), const FaceName&>);
    CHECK(sizeof(ThermalBoundaryCondition) < 256);  // an id, a face and numbers; not a mesh
}

TEST_CASE("ThermalBC_UnitsContract", "[thermal][bc][definition]") {
    // h A is W/K, h T A is W, and (h A) T is W: the compiler checks the algebra.
    STATIC_CHECK(decltype(HeatTransferCoefficient{} * Area{})::dimension == ThermalConductance::dimension);
    STATIC_CHECK(decltype(HeatTransferCoefficient{} * Temperature{} * Area{})::dimension == Power::dimension);
    STATIC_CHECK(decltype(ThermalConductance{} * Temperature{})::dimension == Power::dimension);
    // Neither boundary term is a conductivity-based quantity, and the dimensions differ.
    STATIC_CHECK(HeatTransferCoefficient::dimension != ThermalConductivity::dimension);
    STATIC_CHECK(ThermalConductance::dimension != Power::dimension);

    // Numerically: SI in, SI out. The reference triangle's matrix is in W/K and
    // its source in W, at the values written out by hand.
    const Tri3Convection term = reference();
    CHECK(term(0, 0).si() == 2.0);
    CHECK(term.source(0).si() == 1200.0);
    CHECK(term.area().si() == 3.0);
    CHECK(term.coefficient().si() == 4.0);
    CHECK(term.ambient().si() == 300.0);
}

TEST_CASE("ThermalBC_SignConvention", "[thermal][bc][definition]") {
    // THE LAW: -k grad(T).n = h (T - T_inf), n outward. A face hotter than its
    // ambient LOSES heat; the residual K T - Q is the heat leaving, positive
    // outward. P18-DATA-001 states the same law as q = h (T_ambient - T) positive
    // INTO the body, so heat into the body is the NEGATIVE of the residual.
    const Tri3Convection term = reference();
    const auto hot = term.residualFrom(uniform(350.0));
    const double outward = hot[0].si() + hot[1].si() + hot[2].si();
    CHECK(outward == 600.0);                 // h A (T - T_inf) = 4 * 3 * 50
    const double intoBody = 4.0 * 3.0 * (300.0 - 350.0);  // DATA's q = h (T_amb - T), times A
    CHECK(intoBody == -outward);
    CHECK(intoBody < 0.0);                   // a hot body gains nothing from a cooler ambient

    const auto cold = term.residualFrom(uniform(250.0));
    CHECK(cold[0].si() + cold[1].si() + cold[2].si() == -600.0);  // the ambient heats the body
}

// ===========================================================================
// The Robin matrix and source, by hand
// ===========================================================================

TEST_CASE("ThermalBC_SingleTriangleConvectionMatrixAnalytical", "[thermal][bc][robin]") {
    // A = 3 m^2 (the triangle (0,0,0) (2,0,0) (0,3,0)), h = 4 W/(m^2 K), so h A / 12
    // is exactly 1 and the matrix is [2 1 1; 1 2 1; 1 1 2] W/K, in binary exact.
    const Tri3Convection term = reference();
    const Matrix3 expected{2, 1, 1, 1, 2, 1, 1, 1, 2};
    const Matrix3 actual = entriesOf(term);
    for (std::size_t i = 0; i < 9; ++i) {
        CHECK(actual[i] == expected[i]);
    }
    CHECK(isConsistentRobin(actual, 4.0, 3.0, 0.0));
    WARN("single triangle A = 3 m^2, h = 4 W/(m^2 K): Kconv = [2 1 1; 1 2 1; 1 1 2] W/K, max entry error 0");
}

TEST_CASE("ThermalBC_SingleTriangleConvectionSourceAnalytical", "[thermal][bc][robin]") {
    // h T_inf A / 3 = 4 * 300 * 3 / 3 = 1200 W at each node, exact.
    const Tri3Convection term = reference();
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(term.source(i).si() == 1200.0);
    }
    CHECK(term.source(0).si() + term.source(1).si() + term.source(2).si() == 3600.0);  // h T_inf A
    WARN("single triangle: Qconv = [1200 1200 1200] W (sum 3600 W = h T_inf A), max entry error 0");
}

TEST_CASE("ThermalBC_GeneralTriangleConvectionAnalytical", "[thermal][bc][robin]") {
    // Not axis-aligned, so no coordinate assumption can hide: the area comes from a
    // cross product written in the test.
    const Point3D p1 = at(0.3, -0.2, 0.5);
    const Point3D p2 = at(1.7, 0.4, 0.9);
    const Point3D p3 = at(0.1, 1.3, -0.4);
    const double area = triangleAreaByHand(p1, p2, p3);
    CHECK_THAT(area, WithinRel(1.38025359988663, 1e-13));
    const double h = 17.5;
    const double ambient = 288.15;

    Result<Tri3Convection> made = thermal::computeTri3Convection(
        thermal::Tri3Corners{p1, p2, p3}, HeatTransferCoefficient::fromSi(h), Temperature::fromSi(ambient));
    REQUIRE(made.has_value());
    CHECK_THAT(made->area().si(), WithinRel(area, 1e-14));
    CHECK(isConsistentRobin(entriesOf(*made), h, area, 1e-14));
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK_THAT(made->source(i).si(), WithinRel(h * ambient * area / 3.0, 1e-14));
    }
    // And the constant-ambient equilibrium holds on it too.
    const auto r = made->residualFrom(uniform(ambient));
    const double scale = h * area * ambient / 3.0;
    for (const Power value : r) {
        CHECK_THAT(value.si(), WithinAbs(0.0, 1e-13 * scale));
    }
}

TEST_CASE("ThermalBC_ConvectionMatrixSymmetric", "[thermal][bc][robin]") {
    for (const auto& [area, h, ambient] : {std::tuple{3.0, 4.0, 300.0}, std::tuple{1.38025359988663, 17.5, 288.15},
                                           std::tuple{1e-6, 1e4, 20.0}}) {
        const Tri3Convection term = termFor(area, h, ambient);
        for (std::size_t r = 0; r < 3; ++r) {
            for (std::size_t c = 0; c < 3; ++c) {
                // Bitwise, not to a tolerance: no post-hoc symmetrisation exists.
                CHECK(term(r, c).si() == term(c, r).si());
            }
        }
    }
}

TEST_CASE("ThermalBC_ConvectionMatrixPositiveDefinite", "[thermal][bc][robin]") {
    // The eigenvalues are h A / 3 (once, eigenvector [1 1 1]) and h A / 12 (twice,
    // eigenvectors [1 -1 0] and [1 0 -1]); three independent eigenvectors, all
    // eigenvalues positive for h A > 0. Verified by applying the matrix.
    const double h = 4.0;
    const double area = 3.0;
    const Tri3Convection term = termFor(area, h, 300.0);
    const std::array<std::array<double, 3>, 3> vectors{{{1, 1, 1}, {1, -1, 0}, {1, 0, -1}}};
    const std::array<double, 3> eigenvalues{h * area / 3.0, h * area / 12.0, h * area / 12.0};
    for (std::size_t v = 0; v < 3; ++v) {
        for (std::size_t r = 0; r < 3; ++r) {
            double applied = 0.0;
            for (std::size_t c = 0; c < 3; ++c) {
                applied += term(r, c).si() * vectors[v][c];
            }
            CHECK_THAT(applied, WithinAbs(eigenvalues[v] * vectors[v][r], 1e-15));
        }
        CHECK(eigenvalues[v] > 0.0);
    }
    // And the quadratic form is positive for a vector in no eigenspace.
    const std::array<double, 3> x{0.7, -1.3, 2.9};
    double form = 0.0;
    for (std::size_t r = 0; r < 3; ++r) {
        for (std::size_t c = 0; c < 3; ++c) {
            form += x[r] * term(r, c).si() * x[c];
        }
    }
    CHECK(form > 0.0);
}

TEST_CASE("ThermalBC_ConvectionAmbientEquilibrium", "[thermal][bc][robin]") {
    // T = T_inf everywhere on the facet: Kconv T - Qconv = 0, exactly on the
    // reference triangle. The strongest single Robin test: it ties the matrix and
    // the source to each other.
    const Tri3Convection term = reference();
    const auto r = term.residualFrom(uniform(300.0));
    for (const Power value : r) {
        CHECK(value.si() == 0.0);
    }
    WARN("T = T_inf = 300 K: Kconv T - Qconv = [0 0 0] W (exact)");
}

TEST_CASE("ThermalBC_ConvectionAboveAmbientHeatLoss", "[thermal][bc][robin]") {
    const Tri3Convection term = reference();
    const auto r = term.residualFrom(uniform(350.0));
    for (const Power value : r) {
        CHECK(value.si() == 200.0);  // (2+1+1) * 350 - 1200
    }
    CHECK(r[0].si() + r[1].si() + r[2].si() == 600.0);  // h A (T_b - T_inf) = 4 * 3 * 50
    WARN("T_b = 350 K, T_inf = 300 K: sum(Kconv T - Qconv) = 600 W outward (expected 600)");
}

TEST_CASE("ThermalBC_ConvectionBelowAmbientHeatGain", "[thermal][bc][robin]") {
    const Tri3Convection term = reference();
    const auto r = term.residualFrom(uniform(250.0));
    for (const Power value : r) {
        CHECK(value.si() == -200.0);
    }
    CHECK(r[0].si() + r[1].si() + r[2].si() == -600.0);
    WARN("T_b = 250 K, T_inf = 300 K: sum(Kconv T - Qconv) = -600 W (the ambient heats the body)");
}

TEST_CASE("ThermalBC_NonUniformTemperatureResultant", "[thermal][bc][robin]") {
    // The resultant depends on the MEAN nodal temperature, because the row sums
    // are equal: sum_i (K T)_i = (h A / 3) sum_j T_j. Mean 300 K = T_inf gives zero
    // net exchange although individual nodes lose and gain.
    const Tri3Convection term = reference();
    const auto r = term.residualFrom(Tri3Temperatures{Temperature::fromSi(300.0), Temperature::fromSi(350.0), Temperature::fromSi(250.0)});
    CHECK(r[0].si() == 0.0);
    CHECK(r[1].si() == 50.0);   // 300 + 700 + 250 - 1200
    CHECK(r[2].si() == -50.0);  // 300 + 350 + 500 - 1200
    CHECK(r[0].si() + r[1].si() + r[2].si() == 0.0);
}

// ===========================================================================
// The traps: wrong matrices that have the right row sums
// ===========================================================================

TEST_CASE("ThermalBC_ConvectionRejectsCentroidMatrixMutation", "[thermal][bc][robin][trap]") {
    const double h = 4.0;
    const double area = 3.0;
    // What a one-point centroid rule would produce: every entry h A / 9.
    Matrix3 centroid{};
    centroid.fill(h * area / 9.0);

    // Its row sums are the CORRECT h A / 3 ...
    for (std::size_t r = 0; r < 3; ++r) {
        CHECK_THAT(centroid[r * 3] + centroid[r * 3 + 1] + centroid[r * 3 + 2], WithinRel(h * area / 3.0, 1e-15));
    }
    // ... so a row-sum test would pass it, and the whole-matrix comparison does not.
    CHECK_FALSE(isConsistentRobin(centroid, h, area, 1e-12));
    // The production matrix passes the same comparison.
    CHECK(isConsistentRobin(entriesOf(reference()), h, area, 1e-14));
    // And production's diagonal is hA/6, not hA/9, and its off-diagonal hA/12.
    CHECK(reference()(0, 0).si() == h * area / 6.0);
    CHECK(reference()(0, 1).si() == h * area / 12.0);
    CHECK(reference()(0, 0).si() != h * area / 9.0);
    // The centroid matrix has a null space the true one does not: it annihilates
    // [1 -1 0], which the consistent matrix maps to h A / 12 [1 -1 0].
    CHECK(reference()(0, 0).si() - reference()(0, 1).si() == h * area / 12.0);
    CHECK(centroid[0] - centroid[1] == 0.0);
}

TEST_CASE("ThermalBC_ConvectionIsNotLumped", "[thermal][bc][robin][trap]") {
    const double h = 4.0;
    const double area = 3.0;
    // What hidden diagonal lumping would produce: (h A / 3) I. Same row sums.
    Matrix3 lumped{};
    for (std::size_t r = 0; r < 3; ++r) {
        lumped[r * 3 + r] = h * area / 3.0;
    }
    for (std::size_t r = 0; r < 3; ++r) {
        CHECK_THAT(lumped[r * 3] + lumped[r * 3 + 1] + lumped[r * 3 + 2], WithinRel(h * area / 3.0, 1e-15));
    }
    CHECK_FALSE(isConsistentRobin(lumped, h, area, 1e-12));
    // Production couples the nodes: every off-diagonal entry is positive.
    const Tri3Convection term = reference();
    for (std::size_t r = 0; r < 3; ++r) {
        for (std::size_t c = 0; c < 3; ++c) {
            if (r != c) {
                CHECK(term(r, c).si() > 0.0);
            }
        }
    }
    CHECK(term(0, 0).si() != h * area / 3.0);
}

TEST_CASE("ThermalBC_WrongSignsAndFactorsAreDistinguished", "[thermal][bc][robin][trap]") {
    // The sign mutants and the factor mutants the brief lists, built in the test
    // and shown to be rejected by the same oracle production passes.
    const double h = 4.0;
    const double area = 3.0;
    Matrix3 negated = entriesOf(reference());
    for (double& v : negated) {
        v = -v;
    }
    CHECK_FALSE(isConsistentRobin(negated, h, area, 1e-12));
    Matrix3 wrongFactor = entriesOf(reference());
    for (double& v : wrongFactor) {
        v *= 12.0 / 9.0;  // dividing by 9 instead of 12
    }
    CHECK_FALSE(isConsistentRobin(wrongFactor, h, area, 1e-12));
    Matrix3 withConductivity = entriesOf(reference());
    for (double& v : withConductivity) {
        v *= 45.0;  // multiplied by a conductivity
    }
    CHECK_FALSE(isConsistentRobin(withConductivity, h, area, 1e-12));

    // The source: the sign and the thirds.
    const Tri3Convection term = reference();
    CHECK(term.source(0).si() > 0.0);                   // not negated
    CHECK(term.source(0).si() == h * 300.0 * area / 3.0);
    CHECK(term.source(0).si() != h * 300.0 * area);     // not the whole h T_inf A at every node
    // h and T_inf swapped: 300 * (4 ...) would be the same product for the source
    // but the MATRIX would take T_inf as the coefficient.
    const Tri3Convection swapped = termFor(area, 300.0, 4.0);
    CHECK(swapped(0, 0).si() == 300.0 * area / 6.0);
    CHECK(swapped(0, 0).si() != term(0, 0).si());
}

// ===========================================================================
// Scaling
// ===========================================================================

TEST_CASE("ThermalBC_ConvectionAreaScaling", "[thermal][bc][scaling]") {
    // x' = s x: A' = s^2 A, so Kconv and Qconv scale by s^2 at fixed h and T_inf.
    // A viewer-millimetre leak would give 1e6.
    const double h = 17.5;
    const double ambient = 288.15;
    const Point3D p1 = at(0.3, -0.2, 0.5);
    const Point3D p2 = at(1.7, 0.4, 0.9);
    const Point3D p3 = at(0.1, 1.3, -0.4);
    const auto base = thermal::computeTri3Convection(thermal::Tri3Corners{p1, p2, p3}, HeatTransferCoefficient::fromSi(h),
                                                     Temperature::fromSi(ambient));
    REQUIRE(base.has_value());
    for (const double s : {1.0e-3, 0.1, 2.0, 10.0, 100.0}) {
        const auto scaled = thermal::computeTri3Convection(
            thermal::Tri3Corners{at(s * 0.3, s * -0.2, s * 0.5), at(s * 1.7, s * 0.4, s * 0.9), at(s * 0.1, s * 1.3, s * -0.4)},
            HeatTransferCoefficient::fromSi(h), Temperature::fromSi(ambient));
        REQUIRE(scaled.has_value());
        CHECK_THAT(scaled->area().si() / base->area().si(), WithinRel(s * s, 1e-13));
        for (std::size_t r = 0; r < 3; ++r) {
            CHECK_THAT(scaled->source(r).si() / base->source(r).si(), WithinRel(s * s, 1e-13));
            for (std::size_t c = 0; c < 3; ++c) {
                CHECK_THAT((*scaled)(r, c).si() / (*base)(r, c).si(), WithinRel(s * s, 1e-13));
            }
        }
    }
}

TEST_CASE("ThermalBC_ConvectionCoefficientScaling", "[thermal][bc][scaling]") {
    // h -> c h scales both terms by c.
    const Tri3Convection base = termFor(3.0, 4.0, 300.0);
    for (const double c : {0.25, 2.0, 7.5, 1.0e3}) {
        const Tri3Convection scaled = termFor(3.0, 4.0 * c, 300.0);
        for (std::size_t r = 0; r < 3; ++r) {
            CHECK_THAT(scaled.source(r).si(), WithinRel(c * base.source(r).si(), 1e-15));
            for (std::size_t k = 0; k < 3; ++k) {
                CHECK_THAT(scaled(r, k).si(), WithinRel(c * base(r, k).si(), 1e-15));
            }
        }
    }
}

TEST_CASE("ThermalBC_ConvectionAmbientScaling", "[thermal][bc][scaling]") {
    // At fixed h and geometry the source is proportional to T_inf and the MATRIX
    // IS UNCHANGED, bit for bit: an accidental T_inf coupling into Kconv would move it.
    const Tri3Convection base = termFor(3.0, 4.0, 300.0);
    for (const double t : {0.0, 1.0, 150.0, 600.0, 1.0e4}) {
        const Tri3Convection other = termFor(3.0, 4.0, t);
        CHECK(entriesOf(other) == entriesOf(base));
        for (std::size_t r = 0; r < 3; ++r) {
            CHECK_THAT(other.source(r).si(), WithinRel(base.source(r).si() * (t / 300.0), 1e-14));
        }
    }
    CHECK(termFor(3.0, 4.0, 0.0).source(0).si() == 0.0);  // 0 K ambient: no source, the matrix remains
    CHECK(termFor(3.0, 4.0, 0.0)(0, 0).si() == 2.0);
}

TEST_CASE("ThermalBC_ConvectionDoesNotInvolveConductivity", "[thermal][bc][scaling]") {
    // The kernel takes an area, h and an ambient and nothing else: there is no
    // argument a conductivity could be passed in.
    using Fn = Result<Tri3Convection> (*)(Area, HeatTransferCoefficient, Temperature);
    STATIC_CHECK(std::is_invocable_v<Fn, Area, HeatTransferCoefficient, Temperature>);
    STATIC_CHECK_FALSE(std::is_invocable_v<Fn, Area, ThermalConductivity, Temperature>);
    STATIC_CHECK_FALSE(std::is_invocable_v<Fn, Area, HeatTransferCoefficient, ThermalConductivity>);
    // Dimensionally the result could not take a k: Kconv has the unit of h A.
    STATIC_CHECK(decltype(HeatTransferCoefficient{} * Area{})::dimension != decltype(ThermalConductivity{} * HeatTransferCoefficient{} * Area{})::dimension);
}

// ===========================================================================
// Coefficient and ambient policy
// ===========================================================================

TEST_CASE("ThermalBC_ZeroConvectionCoefficientPolicy", "[thermal][bc][invalid]") {
    // THE KERNEL IS TOTAL OVER h >= 0: a zero coefficient gives all-zero terms,
    // whatever the ambient, with no NaN and no diagnostic.
    for (const double ambient : {300.0, 0.0, 1.0e5}) {
        const Tri3Convection term = termFor(3.0, 0.0, ambient);
        for (std::size_t r = 0; r < 3; ++r) {
            CHECK(term.source(r).si() == 0.0);
            for (std::size_t c = 0; c < 3; ++c) {
                CHECK(term(r, c).si() == 0.0);
            }
        }
        const auto residual = term.residualFrom(uniform(400.0));
        for (const Power value : residual) {
            CHECK(value.si() == 0.0);
        }
    }
    // A zero coefficient with an invalid ambient is STILL refused: a malformed
    // value is malformed whether or not it multiplies nothing.
    CHECK(thermal::convectionProblem(Area::fromSi(3.0), HeatTransferCoefficient::fromSi(0.0), Temperature::fromSi(kNaN)) ==
          std::optional<ConvectionProblem>{ConvectionProblem::InvalidAmbient});
    CHECK(thermal::convectionProblem(Area::fromSi(3.0), HeatTransferCoefficient::fromSi(0.0), Temperature::fromSi(-1.0)) ==
          std::optional<ConvectionProblem>{ConvectionProblem::InvalidAmbient});

    // THE CANONICAL RECORD refuses zero (P18-DATA-001: a face that exchanges
    // nothing is an adiabatic face wearing a convection record). The two layers
    // are different questions and this documents both answers.
    const FaceName face{ObjectId::fromValue(3), FaceSelector{.role = FaceRole::EndCap}};
    CHECK_FALSE(thermal::validate(convectionBc(1, face, 0.0, 300.0)).has_value());
    CHECK(thermal::validate(convectionBc(1, face, 1.0e-9, 300.0)).has_value());
}

TEST_CASE("ThermalBC_NegativeConvectionCoefficientRejected", "[thermal][bc][invalid]") {
    for (const double h : {-1.0, -1.0e-300, -1.0e300, -kInf}) {
        CHECK(thermal::convectionProblem(Area::fromSi(3.0), HeatTransferCoefficient::fromSi(h), Temperature::fromSi(300.0)) ==
              std::optional<ConvectionProblem>{ConvectionProblem::InvalidCoefficient});
        CHECK_FALSE(thermal::computeTri3Convection(Area::fromSi(3.0), HeatTransferCoefficient::fromSi(h), Temperature::fromSi(300.0))
                        .has_value());
    }
    const FaceName face{ObjectId::fromValue(3), FaceSelector{.role = FaceRole::EndCap}};
    CHECK_FALSE(thermal::validate(convectionBc(1, face, -5.0, 300.0)).has_value());
}

TEST_CASE("ThermalBC_NaNConvectionCoefficientRejected", "[thermal][bc][invalid]") {
    // h < 0 is false for a NaN, so a check written that way would pass it.
    CHECK(thermal::convectionProblem(Area::fromSi(3.0), HeatTransferCoefficient::fromSi(kNaN), Temperature::fromSi(300.0)) ==
          std::optional<ConvectionProblem>{ConvectionProblem::InvalidCoefficient});
    CHECK_FALSE(thermal::computeTri3Convection(Area::fromSi(3.0), HeatTransferCoefficient::fromSi(kNaN), Temperature::fromSi(300.0))
                    .has_value());
    const FaceName face{ObjectId::fromValue(3), FaceSelector{.role = FaceRole::EndCap}};
    CHECK_FALSE(thermal::validate(convectionBc(1, face, kNaN, 300.0)).has_value());
}

TEST_CASE("ThermalBC_InfiniteConvectionCoefficientRejected", "[thermal][bc][invalid]") {
    CHECK(thermal::convectionProblem(Area::fromSi(3.0), HeatTransferCoefficient::fromSi(kInf), Temperature::fromSi(300.0)) ==
          std::optional<ConvectionProblem>{ConvectionProblem::InvalidCoefficient});
    const FaceName face{ObjectId::fromValue(3), FaceSelector{.role = FaceRole::EndCap}};
    CHECK_FALSE(thermal::validate(convectionBc(1, face, kInf, 300.0)).has_value());
}

TEST_CASE("ThermalBC_NaNPrescribedTemperatureRejected", "[thermal][bc][invalid]") {
    const FaceName face{ObjectId::fromValue(3), FaceSelector{.role = FaceRole::EndCap}};
    CHECK_FALSE(thermal::validate(temperatureBc(1, face, kNaN)).has_value());
    Hand hand = makeHand(referenceTriangleNodes(), {{0, 1, 2}});
    const std::vector<TemperatureRequest> requests{
        {.condition = bid(1), .dof = dofOfHandle(hand, 1), .value = Temperature::fromSi(kNaN)}};
    const auto resolved = thermal::resolveTemperatureConstraints(hand.dofs, requests);
    REQUIRE_FALSE(resolved.has_value());
    CHECK(resolved.error().problem == BoundaryProblem::TemperatureInvalid);
}

TEST_CASE("ThermalBC_InfinitePrescribedTemperatureRejected", "[thermal][bc][invalid]") {
    Hand hand = makeHand(referenceTriangleNodes(), {{0, 1, 2}});
    for (const double bad : {kInf, -kInf}) {
        const std::vector<TemperatureRequest> requests{
            {.condition = bid(1), .dof = dofOfHandle(hand, 1), .value = Temperature::fromSi(bad)}};
        const auto resolved = thermal::resolveTemperatureConstraints(hand.dofs, requests);
        REQUIRE_FALSE(resolved.has_value());
        CHECK(resolved.error().problem == BoundaryProblem::TemperatureInvalid);
    }
}

TEST_CASE("ThermalBC_BelowZeroKelvinPrescribedTemperatureRejected", "[thermal][bc][invalid]") {
    Hand hand = makeHand(referenceTriangleNodes(), {{0, 1, 2}});
    const std::vector<TemperatureRequest> below{
        {.condition = bid(1), .dof = dofOfHandle(hand, 1), .value = Temperature::fromSi(-0.001)}};
    const auto refused = thermal::resolveTemperatureConstraints(hand.dofs, below);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().problem == BoundaryProblem::TemperatureInvalid);
    // 0 K itself is the limit and is accepted, and is stored as 0 K.
    const std::vector<TemperatureRequest> zero{
        {.condition = bid(1), .dof = dofOfHandle(hand, 1), .value = Temperature::fromSi(0.0)}};
    const auto accepted = thermal::resolveTemperatureConstraints(hand.dofs, zero);
    REQUIRE(accepted.has_value());
    REQUIRE(accepted->constraints.prescribedTemperature(dofOfHandle(hand, 1)).has_value());
    CHECK(accepted->constraints.prescribedTemperature(dofOfHandle(hand, 1))->si() == 0.0);
}

TEST_CASE("ThermalBC_NaNAmbientTemperatureRejected", "[thermal][bc][invalid]") {
    CHECK(thermal::convectionProblem(Area::fromSi(3.0), HeatTransferCoefficient::fromSi(4.0), Temperature::fromSi(kNaN)) ==
          std::optional<ConvectionProblem>{ConvectionProblem::InvalidAmbient});
    const FaceName face{ObjectId::fromValue(3), FaceSelector{.role = FaceRole::EndCap}};
    CHECK_FALSE(thermal::validate(convectionBc(1, face, 4.0, kNaN)).has_value());
}

TEST_CASE("ThermalBC_InfiniteAmbientTemperatureRejected", "[thermal][bc][invalid]") {
    for (const double bad : {kInf, -kInf}) {
        CHECK(thermal::convectionProblem(Area::fromSi(3.0), HeatTransferCoefficient::fromSi(4.0), Temperature::fromSi(bad)) ==
              std::optional<ConvectionProblem>{ConvectionProblem::InvalidAmbient});
        const FaceName face{ObjectId::fromValue(3), FaceSelector{.role = FaceRole::EndCap}};
        CHECK_FALSE(thermal::validate(convectionBc(1, face, 4.0, bad)).has_value());
    }
}

TEST_CASE("ThermalBC_BelowZeroKelvinAmbientRejectedAndZeroKelvinAccepted", "[thermal][bc][invalid]") {
    CHECK(thermal::convectionProblem(Area::fromSi(3.0), HeatTransferCoefficient::fromSi(4.0), Temperature::fromSi(-0.5)) ==
          std::optional<ConvectionProblem>{ConvectionProblem::InvalidAmbient});
    const FaceName face{ObjectId::fromValue(3), FaceSelector{.role = FaceRole::EndCap}};
    CHECK_FALSE(thermal::validate(convectionBc(1, face, 4.0, -0.5)).has_value());
    // 0 K is a valid ambient: Qconv = 0 while Kconv remains. Not a missing value.
    CHECK(thermal::validate(convectionBc(1, face, 4.0, 0.0)).has_value());
    const Tri3Convection zero = termFor(3.0, 4.0, 0.0);
    CHECK(zero.source(1).si() == 0.0);
    CHECK(zero(1, 1).si() == 2.0);
}

TEST_CASE("ThermalBC_InvalidAreaAndOverflowAreRefused", "[thermal][bc][invalid]") {
    for (const double area : {0.0, -1.0, kNaN, kInf}) {
        CHECK(thermal::convectionProblem(Area::fromSi(area), HeatTransferCoefficient::fromSi(4.0), Temperature::fromSi(300.0)) ==
              std::optional<ConvectionProblem>{ConvectionProblem::InvalidArea});
    }
    // The matrix gate: h A / 12 overflows although each input is finite.
    {
        const auto matrix = thermal::computeTri3Convection(Area::fromSi(100.0), HeatTransferCoefficient::fromSi(1.7e308),
                                                           Temperature::fromSi(300.0));
        REQUIRE_FALSE(matrix.has_value());
        CHECK_THAT(matrix.error().message, ContainsSubstring("convection matrix"));
    }
    // The source gate alone: the matrix is finite (h = 1e300), h T_inf A / 3 is not.
    {
        const auto source = thermal::computeTri3Convection(Area::fromSi(1.0), HeatTransferCoefficient::fromSi(1.0e300),
                                                           Temperature::fromSi(1.0e10));
        REQUIRE_FALSE(source.has_value());
        CHECK_THAT(source.error().message, ContainsSubstring("convection source"));
        CHECK_THAT(source.error().message, !ContainsSubstring("convection matrix"));
    }
    // Neither gate fires on large but representable values.
    CHECK(thermal::computeTri3Convection(Area::fromSi(1.0e6), HeatTransferCoefficient::fromSi(1.0e6), Temperature::fromSi(1.0e3))
              .has_value());

    // THE ORDER OF THE DIVISION IS PART OF THE CLAIM. Each entry is formed as
    // h * (A / 12) and (h * (A / 3)) * T_inf, so a product overflows only when the
    // entry does. (h * A) / 12 and (h * T_inf * A) / 3 overflow whenever the
    // product does, although the entry is perfectly representable.
    {
        // h A = 3e308 is not representable; h A / 12 = 2.5e307 is.
        const auto matrix = thermal::computeTri3Convection(Area::fromSi(3.0), HeatTransferCoefficient::fromSi(1.0e308),
                                                           Temperature::fromSi(0.0));
        REQUIRE(matrix.has_value());
        CHECK_THAT((*matrix)(0, 0).si(), WithinRel(5.0e307, 1e-14));
        CHECK_THAT((*matrix)(0, 1).si(), WithinRel(2.5e307, 1e-14));
    }
    {
        // h T_inf A = 3e308 is not representable; h T_inf A / 3 = 1e308 is.
        const auto source = thermal::computeTri3Convection(Area::fromSi(3.0), HeatTransferCoefficient::fromSi(1.0e154),
                                                           Temperature::fromSi(1.0e154));
        REQUIRE(source.has_value());
        CHECK_THAT(source->source(0).si(), WithinRel(1.0e308, 1e-14));
        CHECK_THAT((*source)(0, 0).si(), WithinRel(5.0e153, 1e-14));
    }
}

TEST_CASE("ThermalBC_EveryBoundaryProblemHasAName", "[thermal][bc][invalid]") {
    for (const auto problem :
         {BoundaryProblem::InvalidIdentity, BoundaryProblem::DuplicateConditionId, BoundaryProblem::TemperatureInvalid,
          BoundaryProblem::CoefficientInvalid, BoundaryProblem::AmbientInvalid, BoundaryProblem::TargetInvalid,
          BoundaryProblem::TargetUnresolved, BoundaryProblem::TargetWithoutFacets, BoundaryProblem::DegenerateFacet,
          BoundaryProblem::EntityNotInMesh, BoundaryProblem::EntitiesNotUnique, BoundaryProblem::DofMapMismatch,
          BoundaryProblem::ConflictingTemperature, BoundaryProblem::NonFiniteResult}) {
        CHECK(thermal::toString(problem) != "unknown");
        CHECK(!thermal::toString(problem).empty());
    }
    for (const auto problem : {ConvectionProblem::InvalidArea, ConvectionProblem::InvalidCoefficient,
                               ConvectionProblem::InvalidAmbient, ConvectionProblem::NonFiniteResult}) {
        CHECK(thermal::toString(problem) != "unknown");
    }
    for (const auto reason : {thermal::BoundaryStaleReason::Body, thermal::BoundaryStaleReason::Mesh,
                              thermal::BoundaryStaleReason::Geometry, thermal::BoundaryStaleReason::Conditions}) {
        CHECK(thermal::toString(reason) != "unknown");
    }
}

// ===========================================================================
// Prescribed temperature: the seams
// ===========================================================================

namespace {

/// Sparse handles 7, 11, 42, 900 (the brief's four, in the only order P16's
/// `MeshBuilder` accepts: node handles are STRICTLY INCREASING, so a mesh cannot
/// be built with 7 after 900). The unknowns are therefore 0, 1, 2, 3 by rank, and
/// no unknown equals its handle.
struct Sparse4 {
    Hand hand;
    ThermalDofIndex d11, d42, d900, d7;
};

[[nodiscard]] Sparse4 makeSparse4() {
    // Triangle T0 = (42, 900, 7); T1 = (11, 42, 7). Planar, non-degenerate.
    // Indices into {7, 11, 42, 900}: 42 is 2, 900 is 3, 7 is 0, 11 is 1.
    Hand hand = makeHand({{7, at(0, 1, 0)}, {11, at(0, 0, 0)}, {42, at(1, 0, 0)}, {900, at(1, 1, 0)}},
                         {{2, 3, 0}, {1, 2, 0}});
    Sparse4 s{std::move(hand), ThermalDofIndex::fromValue(0), ThermalDofIndex::fromValue(0),
              ThermalDofIndex::fromValue(0), ThermalDofIndex::fromValue(0)};
    s.d11 = dofOfHandle(s.hand, 11);
    s.d42 = dofOfHandle(s.hand, 42);
    s.d900 = dofOfHandle(s.hand, 900);
    s.d7 = dofOfHandle(s.hand, 7);
    return s;
}

[[nodiscard]] std::vector<std::uint64_t> values(const std::vector<ThermalDofIndex>& dofs) {
    std::vector<std::uint64_t> out;
    for (const ThermalDofIndex dof : dofs) {
        out.push_back(dof.value());
    }
    return out;
}

[[nodiscard]] TemperatureRequest request(std::uint64_t condition, ThermalDofIndex dof, double kelvin) {
    return TemperatureRequest{.condition = bid(condition), .dof = dof, .value = Temperature::fromSi(kelvin)};
}

} // namespace

TEST_CASE("ThermalBC_PrescribedTemperatureMapsBoundaryNodes", "[thermal][bc][dirichlet]") {
    const Sparse4 s = makeSparse4();
    // The numbering is the rank of the handle, not the handle.
    CHECK(s.d7.value() == 0);
    CHECK(s.d11.value() == 1);
    CHECK(s.d42.value() == 2);
    CHECK(s.d900.value() == 3);

    // T0 = (42, 900, 7): the constrained unknowns are 2, 3, 0 -> ascending by UNKNOWN 0, 2, 3.
    const std::vector<ElementId> first{s.hand.triangles[0]};
    const auto dofs = thermal::uniqueFacetDofs(s.hand.mesh, s.hand.dofs, first);
    REQUIRE(dofs.has_value());
    CHECK(values(*dofs) == std::vector<std::uint64_t>{0, 2, 3});

    const auto resolved = thermal::resolveTemperatureConstraints(
        s.hand.dofs, std::vector{request(1, (*dofs)[0], 350.0), request(1, (*dofs)[1], 350.0), request(1, (*dofs)[2], 350.0)});
    REQUIRE(resolved.has_value());
    CHECK(resolved->constraints.size() == 3);
    WARN("Dirichlet fixture: NodeIds {42,900,7} -> unknowns {2,3,0}, ordered {0,2,3}, T = 350 K on each");
}

TEST_CASE("ThermalBC_PrescribedTemperatureUsesThermalDofMap", "[thermal][bc][dirichlet]") {
    const Sparse4 s = makeSparse4();
    const std::vector<ElementId> both{s.hand.triangles[0], s.hand.triangles[1]};
    const auto dofs = thermal::uniqueFacetDofs(s.hand.mesh, s.hand.dofs, both);
    REQUIRE(dofs.has_value());
    // NodeId.value() would put node 900 at index 900 and node 7 at index 7; the
    // numbering puts them at 3 and 0.
    for (const ThermalDofIndex dof : *dofs) {
        CHECK(dof.value() < 4);
    }
    CHECK(s.d900.value() != 900);
    CHECK(s.d42.value() != 42);
    CHECK(s.d11.value() != 11);
    CHECK(s.hand.dofs.dofCount() == 4);
}

TEST_CASE("ThermalBC_PrescribedTemperatureStoresNonzeroKelvin", "[thermal][bc][dirichlet]") {
    const Sparse4 s = makeSparse4();
    const auto resolved = thermal::resolveTemperatureConstraints(
        s.hand.dofs, std::vector{request(1, s.d42, 350.0), request(1, s.d900, 350.0), request(1, s.d7, 350.0)});
    REQUIRE(resolved.has_value());
    for (const ThermalDofIndex dof : {s.d42, s.d900, s.d7}) {
        REQUIRE(resolved->constraints.prescribedTemperature(dof).has_value());
        // 350 K stays 350 K: not zero (P17's restraint), not 76.85 (Celsius).
        CHECK(resolved->constraints.prescribedTemperature(dof)->si() == 350.0);
    }
    // The free unknown has NO prescribed temperature -- not 0 K.
    CHECK_FALSE(resolved->constraints.prescribedTemperature(s.d11).has_value());
    CHECK_FALSE(resolved->constraints.isConstrained(s.d11));
    CHECK(resolved->constraints.stamp() == s.hand.dofs.stamp());
    CHECK(resolved->constraints.dofCount() == s.hand.dofs.dofCount());
}

TEST_CASE("ThermalBC_PrescribedTemperatureDeduplicatesNodes", "[thermal][bc][dirichlet]") {
    const Sparse4 s = makeSparse4();
    // T0 = (42 900 7), T1 = (11 42 7): six corners, four unknowns -- not six entries.
    const std::vector<ElementId> both{s.hand.triangles[0], s.hand.triangles[1]};
    const auto dofs = thermal::uniqueFacetDofs(s.hand.mesh, s.hand.dofs, both);
    REQUIRE(dofs.has_value());
    CHECK(dofs->size() == 4);
    CHECK(values(*dofs) == std::vector<std::uint64_t>{0, 1, 2, 3});
    // The same facet listed through a larger set that shares nodes (42 and 7 are
    // corners of both) still gives each unknown once.
    const std::vector<std::uint64_t> unique = values(*dofs);
    CHECK(std::ranges::adjacent_find(unique) == unique.end());
}

TEST_CASE("ThermalBC_PrescribedTemperatureDeterministicOrder", "[thermal][bc][dirichlet]") {
    const Sparse4 s = makeSparse4();
    std::vector<TemperatureRequest> requests{request(1, s.d11, 300.0), request(1, s.d42, 310.0),
                                             request(2, s.d900, 320.0), request(2, s.d7, 330.0)};
    const auto reference = thermal::resolveTemperatureConstraints(s.hand.dofs, requests);
    REQUIRE(reference.has_value());
    // Ascending by unknown, whatever the request order: every permutation builds
    // an EQUAL map.
    std::vector<std::size_t> order(requests.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    int permutations = 0;
    do {
        std::vector<TemperatureRequest> shuffled;
        for (const std::size_t i : order) {
            shuffled.push_back(requests[i]);
        }
        const auto again = thermal::resolveTemperatureConstraints(s.hand.dofs, shuffled);
        REQUIRE(again.has_value());
        CHECK(again->constraints == reference->constraints);
        ++permutations;
    } while (std::next_permutation(order.begin(), order.end()));
    CHECK(permutations == 24);
    // And the order itself is ascending by unknown, with each unknown's own value.
    const auto constrained = reference->constraints.constrained();
    for (std::size_t i = 1; i < constrained.size(); ++i) {
        CHECK(constrained[i - 1].dof.value() < constrained[i].dof.value());
    }
    CHECK(constrained[0].temperature.si() == 330.0);  // node 7 is unknown 0
    CHECK(constrained[3].temperature.si() == 320.0);  // node 900 is unknown 3
}

// ===========================================================================
// Duplicates and conflicts
// ===========================================================================

TEST_CASE("ThermalBC_DuplicateIdenticalTemperatureAccepted", "[thermal][bc][conflict]") {
    const Sparse4 s = makeSparse4();
    // BC A: nodes 42, 900, 7 at 350 K. BC B: nodes 900, 7 at 350 K.
    const std::vector<TemperatureRequest> requests{
        request(1, s.d42, 350.0), request(1, s.d900, 350.0), request(1, s.d7, 350.0),
        request(2, s.d900, 350.0), request(2, s.d7, 350.0)};
    const auto resolved = thermal::resolveTemperatureConstraints(s.hand.dofs, requests);
    REQUIRE(resolved.has_value());
    // One constraint per unknown: three, not five.
    CHECK(resolved->constraints.size() == 3);
    for (const auto& constraint : resolved->constraints.constrained()) {
        CHECK(constraint.temperature.si() == 350.0);
    }
    // ATTRIBUTION keeps both ids where both asked.
    REQUIRE(resolved->sources.size() == 3);
    CHECK(resolved->sources[0] == std::vector{bid(1), bid(2)});  // unknown 0 (node 7)
    CHECK(resolved->sources[1] == std::vector{bid(1)});          // unknown 2 (node 42)
    CHECK(resolved->sources[2] == std::vector{bid(1), bid(2)});  // unknown 3 (node 900)
}

TEST_CASE("ThermalBC_ConflictingTemperatureRejected", "[thermal][bc][conflict]") {
    const Sparse4 s = makeSparse4();
    // BC A: unknown 2 at 350 K. BC B: unknown 2 at 300 K.
    for (const bool reversed : {false, true}) {
        std::vector<TemperatureRequest> requests{request(1, s.d900, 350.0), request(2, s.d900, 300.0)};
        if (reversed) {
            std::ranges::reverse(requests);
        }
        const auto resolved = thermal::resolveTemperatureConstraints(s.hand.dofs, requests);
        // NOT the first, NOT the last, NOT the mean: nothing is accepted.
        REQUIRE_FALSE(resolved.has_value());
        CHECK(resolved.error().problem == BoundaryProblem::ConflictingTemperature);
        REQUIRE(resolved.error().conflicts.size() == 1);
        const TemperatureConflict& conflict = resolved.error().conflicts[0];
        CHECK(conflict.first == bid(1));
        CHECK(conflict.second == bid(2));
        CHECK(conflict.firstValue.si() == 350.0);
        CHECK(conflict.secondValue.si() == 300.0);
        CHECK(conflict.unknownCount == 1);
        CHECK(conflict.firstDof == s.d900);
        CHECK_THAT(resolved.error().error.message, ContainsSubstring("none chosen over another"));
    }
    WARN("conflict: BC 1 -> 350 K and BC 2 -> 300 K on node 900 (unknown 3); set refused (first/last/mean not chosen)");
}

TEST_CASE("ThermalBC_ConflictUsesExactCanonicalEquality", "[thermal][bc][conflict]") {
    // Two commands are "the same" if they are the same number. 300 K and the next
    // double above it are different commands, and no solver tolerance merges them.
    const Sparse4 s = makeSparse4();
    const double next = std::nextafter(300.0, 400.0);
    CHECK(next != 300.0);
    const auto conflict = thermal::resolveTemperatureConstraints(
        s.hand.dofs, std::vector{request(1, s.d42, 300.0), request(2, s.d42, next)});
    REQUIRE_FALSE(conflict.has_value());
    CHECK(conflict.error().problem == BoundaryProblem::ConflictingTemperature);
    // And 1e-6 K apart (the sort of number a solver tolerance would hide) is a conflict.
    const auto close = thermal::resolveTemperatureConstraints(
        s.hand.dofs, std::vector{request(1, s.d42, 300.0), request(2, s.d42, 300.000001)});
    REQUIRE_FALSE(close.has_value());
    CHECK(close.error().problem == BoundaryProblem::ConflictingTemperature);
    // Exactly equal is one constraint.
    const auto same = thermal::resolveTemperatureConstraints(
        s.hand.dofs, std::vector{request(1, s.d42, 300.0), request(2, s.d42, 300.0)});
    REQUIRE(same.has_value());
    CHECK(same->constraints.size() == 1);
}

TEST_CASE("ThermalBC_ConflictReportIsSummarisedAndOrderIndependent", "[thermal][bc][conflict]") {
    const Sparse4 s = makeSparse4();
    // Three conditions on shared unknowns: 1 and 2 disagree on unknowns 0 and 1,
    // 1 and 3 disagree on unknown 1, and 2 and 3 agree.
    std::vector<TemperatureRequest> requests{
        request(1, s.d11, 300.0), request(1, s.d42, 300.0), request(2, s.d11, 310.0),
        request(2, s.d42, 320.0), request(3, s.d42, 320.0)};
    std::vector<std::size_t> order(requests.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::vector<TemperatureConflict> first;
    bool haveFirst = false;
    int permutations = 0;
    do {
        std::vector<TemperatureRequest> shuffled;
        for (const std::size_t i : order) {
            shuffled.push_back(requests[i]);
        }
        const auto resolved = thermal::resolveTemperatureConstraints(s.hand.dofs, shuffled);
        REQUIRE_FALSE(resolved.has_value());
        const auto& conflicts = resolved.error().conflicts;
        REQUIRE(conflicts.size() == 2);  // pairs (1,2) and (1,3): a summary, not one line per unknown
        CHECK(conflicts[0].first == bid(1));
        CHECK(conflicts[0].second == bid(2));
        CHECK(conflicts[0].unknownCount == 2);   // unknowns 0 and 1
        CHECK(conflicts[0].firstDof == s.d11);
        CHECK(conflicts[1].first == bid(1));
        CHECK(conflicts[1].second == bid(3));
        CHECK(conflicts[1].unknownCount == 1);
        CHECK(conflicts[1].firstDof == s.d42);
        if (!haveFirst) {
            first = conflicts;
            haveFirst = true;
        } else {
            CHECK(conflicts == first);  // independent of the request order
        }
        ++permutations;
    } while (std::next_permutation(order.begin(), order.end()));
    CHECK(permutations == 120);
}

TEST_CASE("ThermalBC_ConflictCountsEachUnknownOncePerPair", "[thermal][bc][conflict]") {
    // Condition 1 asks twice for the same temperature on one unknown (a seam can
    // be handed that), condition 2 asks for another. The pair (1, 2) disagrees on
    // ONE unknown, however many of condition 1's requests meet condition 2's.
    const Sparse4 s = makeSparse4();
    const auto resolved = thermal::resolveTemperatureConstraints(
        s.hand.dofs, std::vector{request(1, s.d42, 300.0), request(1, s.d42, 300.0), request(2, s.d42, 310.0),
                                 request(1, s.d900, 300.0), request(2, s.d900, 310.0)});
    REQUIRE_FALSE(resolved.has_value());
    REQUIRE(resolved.error().conflicts.size() == 1);
    CHECK(resolved.error().conflicts[0].first == bid(1));
    CHECK(resolved.error().conflicts[0].second == bid(2));
    CHECK(resolved.error().conflicts[0].unknownCount == 2);  // unknowns 42 and 900, not three
    CHECK(resolved.error().conflicts[0].firstDof == s.d42);
}
TEST_CASE("ThermalBC_SeamRefusalsOfTheConstraintMap", "[thermal][bc][invalid]") {
    const Sparse4 s = makeSparse4();
    // An unknown the numbering does not have is never clamped and never skipped.
    const auto missing = thermal::resolveTemperatureConstraints(
        s.hand.dofs, std::vector{request(1, ThermalDofIndex::fromValue(99), 300.0)});
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().problem == BoundaryProblem::EntityNotInMesh);

    // The empty set is accepted: no prescribed temperature is a valid boundary here.
    const auto none = thermal::resolveTemperatureConstraints(s.hand.dofs, std::span<const TemperatureRequest>{});
    REQUIRE(none.has_value());
    CHECK(none->constraints.isEmpty());
    CHECK(none->sources.empty());

    // EVERY unknown prescribed (Nfree = 0) is also accepted.
    const auto all = thermal::resolveTemperatureConstraints(
        s.hand.dofs, std::vector{request(1, s.d11, 300.0), request(1, s.d42, 300.0), request(1, s.d900, 300.0),
                                 request(1, s.d7, 300.0)});
    REQUIRE(all.has_value());
    CHECK(all->constraints.size() == s.hand.dofs.dofCount());
    const auto free = thermal::buildThermalFreeEquationMap(s.hand.dofs, all->constraints);
    REQUIRE(free.has_value());
    CHECK(free->freeCount() == 0);
}

// ===========================================================================
// Convection: the hand-mesh seam
// ===========================================================================

TEST_CASE("ThermalBC_ConvectionFacetCarriesTheAnalyticalTermsAndOwnOrder", "[thermal][bc][robin]") {
    // The mesh holds nodes 10, 20, 30 (unknowns 0, 1, 2); the TRIANGLE lists its
    // corners 30, 10, 20, which is not ascending, and the facet must keep that order.
    Hand hand = makeHand({{10, at(0, 0, 0)}, {20, at(2, 0, 0)}, {30, at(0, 3, 0)}}, {{2, 0, 1}});
    const auto facets = convect(hand, 4.0, 300.0);
    REQUIRE(facets.size() == 1);
    // dofs[i] is the unknown of the triangle's corner i AS STORED: nodes (30, 10, 20).
    const meshing::Triangle* triangle = hand.mesh.findTriangle(hand.triangles[0]);
    REQUIRE(triangle != nullptr);
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(facets[0].dofs[i] == dofOfHandle(hand, triangle->nodes[i].value()));
    }
    CHECK(facets[0].dofs[0].value() == 2);  // node 30
    CHECK(facets[0].dofs[1].value() == 0);  // node 10
    CHECK(facets[0].dofs[2].value() == 1);  // node 20: NOT sorted
    CHECK(facets[0].facet == hand.triangles[0]);
    CHECK(isConsistentRobin(entriesOf(facets[0].term), 4.0, 3.0, 0.0));
    CHECK(facets[0].term.source(0).si() == 1200.0);
}

TEST_CASE("ThermalBC_SharedFacetNodeContributionsSum", "[thermal][bc][robin]") {
    // A rectangle 2 x 3 split along 1-3: two triangles of area 3, sharing nodes
    // 1 and 3. h = 4, T_inf = 300, so each triangle contributes [2 1 1; ...] and
    // 1200 per node. The shared nodes receive BOTH.
    Hand hand = makeHand({{1, at(0, 0, 0)}, {2, at(2, 0, 0)}, {3, at(2, 3, 0)}, {4, at(0, 3, 0)}}, {{0, 1, 2}, {0, 2, 3}});
    const auto facets = convect(hand, 4.0, 300.0);
    REQUIRE(facets.size() == 2);
    Dense dense(4);
    for (const ConvectionFacet& facet : facets) {
        dense.add(facet);
    }
    const auto at4 = [&](NodeId::ValueType a, NodeId::ValueType b) {
        return dense.k[static_cast<std::size_t>(dofOfHandle(hand, a).value()) * 4 + static_cast<std::size_t>(dofOfHandle(hand, b).value())];
    };
    const auto q4 = [&](NodeId::ValueType a) { return dense.q[static_cast<std::size_t>(dofOfHandle(hand, a).value())]; };
    // Shared nodes 1 and 3: 2 + 2. Unshared 2 and 4: 2.
    CHECK(at4(1, 1) == 4.0);
    CHECK(at4(3, 3) == 4.0);
    CHECK(at4(2, 2) == 2.0);
    CHECK(at4(4, 4) == 2.0);
    // The shared edge 1-3 couples through both triangles; the others through one.
    CHECK(at4(1, 3) == 2.0);
    CHECK(at4(3, 1) == 2.0);
    CHECK(at4(1, 2) == 1.0);
    CHECK(at4(2, 3) == 1.0);
    CHECK(at4(3, 4) == 1.0);
    CHECK(at4(1, 4) == 1.0);
    CHECK(at4(2, 4) == 0.0);
    CHECK(q4(1) == 2400.0);
    CHECK(q4(3) == 2400.0);
    CHECK(q4(2) == 1200.0);
    CHECK(q4(4) == 1200.0);
    CHECK(dense.sumQ() == 7200.0);  // h T_inf A with A = 6 m^2: 4 * 300 * 6
    // The whole-matrix total: sum of all entries = h A = 24 W/K.
    CHECK(sum(dense.k) == 24.0);
}

TEST_CASE("ThermalBC_TriangleWindingDoesNotChangeTheTerms", "[thermal][bc][robin]") {
    Hand forward = makeHand(referenceTriangleNodes(), {{0, 1, 2}});
    Hand reversed = makeHand(referenceTriangleNodes(), {{0, 2, 1}});
    const auto a = convect(forward, 4.0, 300.0);
    const auto b = convect(reversed, 4.0, 300.0);
    Dense da(3);
    Dense db(3);
    da.add(a[0]);
    db.add(b[0]);
    // The scatter is by unknown, so the reversed triangle gives the SAME global
    // matrix and source bit for bit: both depend on the unsigned area only.
    CHECK(da.k == db.k);
    CHECK(da.q == db.q);
    CHECK(a[0].term.area() == b[0].term.area());
}

TEST_CASE("ThermalBC_ConvectionSeamRefusals", "[thermal][bc][invalid]") {
    Hand hand = makeHand(referenceTriangleNodes(), {{0, 1, 2}});
    const auto h4 = HeatTransferCoefficient::fromSi(4.0);
    const auto t300 = Temperature::fromSi(300.0);

    // An EMPTY target is not a zero convection.
    {
        const auto done = thermal::integrateConvection(hand.mesh, hand.dofs, std::span<const ElementId>{}, h4, t300);
        REQUIRE_FALSE(done.has_value());
        CHECK(done.error().problem == BoundaryProblem::TargetWithoutFacets);
        const auto dofs = thermal::uniqueFacetDofs(hand.mesh, hand.dofs, std::span<const ElementId>{});
        REQUIRE_FALSE(dofs.has_value());
        CHECK(dofs.error().problem == BoundaryProblem::TargetWithoutFacets);
    }
    // A repeated facet would count twice.
    {
        const std::vector<ElementId> twice{hand.triangles[0], hand.triangles[0]};
        const auto done = thermal::integrateConvection(hand.mesh, hand.dofs, twice, h4, t300);
        REQUIRE_FALSE(done.has_value());
        CHECK(done.error().problem == BoundaryProblem::EntitiesNotUnique);
        const auto dofs = thermal::uniqueFacetDofs(hand.mesh, hand.dofs, twice);
        REQUIRE_FALSE(dofs.has_value());
        CHECK(dofs.error().problem == BoundaryProblem::EntitiesNotUnique);
    }
    // A facet the mesh does not have is never resolved to a nearby one.
    {
        const std::vector<ElementId> missing{ElementId::fromValue(9999)};
        const auto done = thermal::integrateConvection(hand.mesh, hand.dofs, missing, h4, t300);
        REQUIRE_FALSE(done.has_value());
        CHECK(done.error().problem == BoundaryProblem::EntityNotInMesh);
        const auto dofs = thermal::uniqueFacetDofs(hand.mesh, hand.dofs, missing);
        REQUIRE_FALSE(dofs.has_value());
        CHECK(dofs.error().problem == BoundaryProblem::EntityNotInMesh);
    }
    // A numbering for another mesh.
    {
        Hand other = makeHand({{1, at(0, 0, 0)}, {2, at(1, 0, 0)}, {3, at(0, 1, 0)}}, {{0, 1, 2}});
        const auto done = thermal::integrateConvection(hand.mesh, other.dofs, hand.triangles, h4, t300);
        REQUIRE_FALSE(done.has_value());
        CHECK(done.error().problem == BoundaryProblem::DofMapMismatch);
        const auto dofs = thermal::uniqueFacetDofs(hand.mesh, other.dofs, hand.triangles);
        REQUIRE_FALSE(dofs.has_value());
        CHECK(dofs.error().problem == BoundaryProblem::DofMapMismatch);
    }
    // Values.
    {
        const auto badH = thermal::integrateConvection(hand.mesh, hand.dofs, hand.triangles, HeatTransferCoefficient::fromSi(-1.0), t300);
        REQUIRE_FALSE(badH.has_value());
        CHECK(badH.error().problem == BoundaryProblem::CoefficientInvalid);
        const auto nanH = thermal::integrateConvection(hand.mesh, hand.dofs, hand.triangles, HeatTransferCoefficient::fromSi(kNaN), t300);
        REQUIRE_FALSE(nanH.has_value());
        CHECK(nanH.error().problem == BoundaryProblem::CoefficientInvalid);
        const auto badT = thermal::integrateConvection(hand.mesh, hand.dofs, hand.triangles, h4, Temperature::fromSi(-3.0));
        REQUIRE_FALSE(badT.has_value());
        CHECK(badT.error().problem == BoundaryProblem::AmbientInvalid);
        // Overflow of an entry is refused, never accepted.
        const auto huge = thermal::integrateConvection(hand.mesh, hand.dofs, hand.triangles, HeatTransferCoefficient::fromSi(1.7e308), t300);
        REQUIRE_FALSE(huge.has_value());
        CHECK(huge.error().problem == BoundaryProblem::NonFiniteResult);
    }
}

TEST_CASE("ThermalBC_EmptyMappedTargetFails", "[thermal][bc][mapping]") {
    // A face that resolved and was attributed NO boundary facet is not an empty
    // condition. Both kinds of condition refuse it, at the seam that sees the
    // facet list, and neither returns a successful empty result.
    Hand hand = makeHand(referenceTriangleNodes(), {{0, 1, 2}});
    const std::span<const ElementId> none{};
    const auto dofs = thermal::uniqueFacetDofs(hand.mesh, hand.dofs, none);
    REQUIRE_FALSE(dofs.has_value());
    CHECK(dofs.error().problem == BoundaryProblem::TargetWithoutFacets);
    CHECK(dofs.error().error.code == ErrorCode::FailedPrecondition);
    const auto terms = thermal::integrateConvection(hand.mesh, hand.dofs, none, HeatTransferCoefficient::fromSi(4.0),
                                                    Temperature::fromSi(300.0));
    REQUIRE_FALSE(terms.has_value());
    CHECK(terms.error().problem == BoundaryProblem::TargetWithoutFacets);
    CHECK(terms.error().error.code == ErrorCode::FailedPrecondition);
    CHECK_THAT(terms.error().error.message, ContainsSubstring("nothing to exchange heat through"));
    CHECK_THAT(dofs.error().error.message, ContainsSubstring("no node to prescribe"));
}
TEST_CASE("ThermalBC_ZeroAreaFacetFails", "[thermal][bc][invalid]") {
    // Three collinear nodes: the builder does no geometry validation, and the area is zero.
    Hand hand = makeHand({{1, at(0, 0, 0)}, {2, at(1, 0, 0)}, {3, at(2, 0, 0)}}, {{0, 1, 2}});
    const auto done = thermal::integrateConvection(hand.mesh, hand.dofs, hand.triangles, HeatTransferCoefficient::fromSi(4.0),
                                                   Temperature::fromSi(300.0));
    REQUIRE_FALSE(done.has_value());
    CHECK(done.error().problem == BoundaryProblem::DegenerateFacet);  // never a zero contribution
    // The Dirichlet seam does not need an area: a degenerate facet still names its nodes.
    CHECK(thermal::uniqueFacetDofs(hand.mesh, hand.dofs, hand.triangles).has_value());
}

TEST_CASE("ThermalBC_ConvectionZeroCoefficientSeamGivesZeros", "[thermal][bc][invalid]") {
    Hand hand = makeHand(referenceTriangleNodes(), {{0, 1, 2}});
    const auto facets = convect(hand, 0.0, 300.0);
    REQUIRE(facets.size() == 1);
    Dense dense(3);
    dense.add(facets[0]);
    CHECK(sum(dense.k) == 0.0);
    CHECK(sum(dense.q) == 0.0);
}

TEST_CASE("ThermalBC_ConvectionSuperposition", "[thermal][bc][superposition]") {
    // Two physical exchanges on one triangle ADD, with their own h and T_inf.
    Hand hand = makeHand(referenceTriangleNodes(), {{0, 1, 2}});
    const double h1 = 4.0;
    const double t1 = 300.0;
    const double h2 = 10.0;
    const double t2 = 350.0;
    Dense both(3);
    for (const ConvectionFacet& f : convect(hand, h1, t1)) {
        both.add(f);
    }
    for (const ConvectionFacet& f : convect(hand, h2, t2)) {
        both.add(f);
    }
    // Independent oracle, typed in: A = 3. K = (h1 + h2) A / 12 [2 1 1; ...],
    // Q = (h1 T1 + h2 T2) A / 3 at each node.
    const double unit = (h1 + h2) * 3.0 / 12.0;
    const double perNode = (h1 * t1 + h2 * t2) * 3.0 / 3.0;
    for (std::size_t r = 0; r < 3; ++r) {
        CHECK_THAT(both.q[r], WithinRel(perNode, 1e-15));
        for (std::size_t c = 0; c < 3; ++c) {
            CHECK_THAT(both.k[r * 3 + c], WithinRel(r == c ? 2 * unit : unit, 1e-15));
        }
    }
    // The equivalent single ambient: h_eff = h1 + h2, T_eff = (h1 T1 + h2 T2)/(h1 + h2).
    const double teff = (h1 * t1 + h2 * t2) / (h1 + h2);
    Dense single(3);
    for (const ConvectionFacet& f : convect(hand, h1 + h2, teff)) {
        single.add(f);
    }
    for (std::size_t i = 0; i < 9; ++i) {
        CHECK_THAT(single.k[i], WithinRel(both.k[i], 1e-14));
    }
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK_THAT(single.q[i], WithinRel(both.q[i], 1e-14));
    }
    // Identical (h, T_inf) twice is DOUBLE the effect, not one: convection is
    // additive physics, not a deduplicated constraint.
    Dense twice(3);
    for (int i = 0; i < 2; ++i) {
        for (const ConvectionFacet& f : convect(hand, h1, t1)) {
            twice.add(f);
        }
    }
    Dense once(3);
    for (const ConvectionFacet& f : convect(hand, h1, t1)) {
        once.add(f);
    }
    for (std::size_t i = 0; i < 9; ++i) {
        CHECK(twice.k[i] == 2.0 * once.k[i]);
    }
    CHECK(twice.q[0] == 2.0 * once.q[0]);
}

TEST_CASE("ThermalBC_ConstantTemperatureHandFaceResultant", "[thermal][bc][robin]") {
    // The scalar analogue of a resultant: for a uniform boundary temperature, the
    // summed residual of a multi-facet face is h A_total (T_b - T_inf).
    Hand hand = makeHand({{1, at(0, 0, 0)}, {2, at(2, 0, 0)}, {3, at(2, 3, 0)}, {4, at(0, 3, 0)}}, {{0, 1, 2}, {0, 2, 3}});
    const double h = 4.0;
    const double ambient = 300.0;
    Dense dense(4);
    for (const ConvectionFacet& f : convect(hand, h, ambient)) {
        dense.add(f);
    }
    for (const double tb : {300.0, 350.0, 250.0, 0.0, 1000.0}) {
        const double total = sum(dense.residual(std::vector<double>(4, tb)));
        const double expected = h * 6.0 * (tb - ambient);
        CHECK_THAT(total, WithinAbs(expected, 1e-12 * std::max(1.0, std::abs(expected))));
    }
}

// ===========================================================================
// Sparse ids
// ===========================================================================

TEST_CASE("ThermalBC_SparseNodeIdsUseThermalDofMap", "[thermal][bc][sparse]") {
    // Handles 7, 104, 9001 (and 500000 for the Dirichlet side): the storage is by
    // unknown and the terms land on the right unknowns.
    Hand hand = makeHand({{7, at(0, 0, 0)}, {104, at(2, 0, 0)}, {9001, at(0, 3, 0)}, {500000, at(2, 3, 0)}},
                         {{0, 1, 2}, {1, 3, 2}});
    const auto facets = convect(hand, 4.0, 300.0);
    Dense dense(4);
    for (const ConvectionFacet& f : facets) {
        dense.add(f);
    }
    // Node 7 belongs to the first triangle only: diag 2, q 1200.
    CHECK(dense.q[static_cast<std::size_t>(dofOfHandle(hand, 7).value())] == 1200.0);
    CHECK(dense.q[static_cast<std::size_t>(dofOfHandle(hand, 500000).value())] == 1200.0);
    // 104 and 9001 are shared: 2400.
    CHECK(dense.q[static_cast<std::size_t>(dofOfHandle(hand, 104).value())] == 2400.0);
    CHECK(dense.q[static_cast<std::size_t>(dofOfHandle(hand, 9001).value())] == 2400.0);
    CHECK(dense.n == 4);
    CHECK(dofOfHandle(hand, 500000).value() == 3);

    const auto dofs = thermal::uniqueFacetDofs(hand.mesh, hand.dofs, hand.triangles);
    REQUIRE(dofs.has_value());
    CHECK(values(*dofs) == std::vector<std::uint64_t>{0, 1, 2, 3});
    WARN("sparse ids [7 104 9001 500000]: constraint/convection storage over unknowns [0 1 2 3] (not 500001)");
}

TEST_CASE("ThermalBC_LargeSparseNodeIdDoesNotSizeConstraintStorage", "[thermal][bc][sparse]") {
    constexpr NodeId::ValueType kLarge = 4294967294ULL;
    Hand hand = makeHand({{1, at(0, 0, 0)}, {2, at(2, 0, 0)}, {kLarge, at(0, 3, 0)}}, {{0, 1, 2}});
    const auto dofs = thermal::uniqueFacetDofs(hand.mesh, hand.dofs, hand.triangles);
    REQUIRE(dofs.has_value());
    CHECK(dofs->size() == 3);
    const auto resolved = thermal::resolveTemperatureConstraints(
        hand.dofs, std::vector{request(1, (*dofs)[0], 400.0), request(1, (*dofs)[1], 400.0), request(1, (*dofs)[2], 400.0)});
    REQUIRE(resolved.has_value());
    // Three entries, 24 bytes of temperatures, not four billion.
    CHECK(resolved->constraints.size() == 3);
    CHECK(resolved->constraints.size() * sizeof(thermal::ThermalTemperatureConstraint) < 1024);
    CHECK(resolved->constraints.dofCount() == 3);
    CHECK(resolved->constraints.prescribedTemperature(dofOfHandle(hand, kLarge))->si() == 400.0);
    WARN("max NodeId 4 294 967 294, 3 nodes: constraint map of 3 entries");
}
