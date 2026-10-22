// P18-VALID-001 -- the measured law the two thermal mesh REJECTION thresholds are derived from.
//
// THIS FILE IS THE MEASUREMENT, AND IT RAN BEFORE THE POLICY EXISTED. The brief forbids choosing a thermal mesh
// threshold from intuition, from a standard nobody in this repository can check, or by copying the structural number
// because it looks plausible. P17-VALID-001 measured how well a Tet4 reproduces a linear DISPLACEMENT field; this
// measures how well the same element reproduces a linear TEMPERATURE field, through the thermal production path
// (`recoverThermalElementFields`, which calls P18-ELEM's `gradientFrom`), because that is what a heat flux is built from
// and it is the thermal kernel that was qualified.
//
// WHAT IS MEASURED. A linear temperature field `T = a + b x + c y + d z` is imposed on ONE tetrahedron and the gradient
// is recovered. A linear field has a constant gradient, so the expected value is the field's own coefficients, typed in
// here and never obtained from the kernel. The error is RELATIVE, `max |g_rec - g| / |g|`, so it is independent of the
// field's amplitude; two fields three orders of gradient magnitude apart are used as a control on the fit constants.
//
// TWO DEGENERATION FAMILIES, because a tetrahedron degenerates in two independent ways and neither metric sees the
// other's family (the finding P17 recorded and this milestone re-measures rather than inherits):
//
//     FLATTENING, toward a plane -- a wedge with its apex brought down.     err ~ C / sqrt(3r/R)
//     STRETCHING, toward a line  -- a needle with its base shrunk.          err ~ C * aspect
//
// Every row prints a `LAW` line (`ctest` shows it with the warnings), and the fit bands are PINNED, because the
// thresholds in ThermalValidation.hpp rest on them: a kernel whose gradients drifted would leave a band while every
// absolute error still looked small.

#include "ThermalAssemblyTestSupport.hpp"

#include <bettercad/meshing/MeshQuality.hpp>
#include <bettercad/thermal/ThermalPost.hpp>
#include <bettercad/thermal/ThermalSolve.hpp>
#include <bettercad/thermal/ThermalValidation.hpp>

#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <optional>
#include <vector>

using namespace actest;
using Catch::Matchers::WithinAbs;
using meshing::QualityMetric;

namespace {

using Corners = std::array<std::array<double, 3>, 4>;

/// P16's flattening wedge: a 10 mm equilateral base with the apex brought down to @p apexMillimetres.
[[nodiscard]] Corners wedge(double apexMillimetres) {
    const double a = 0.010;
    const double h = a * std::sqrt(3.0) / 2.0;
    return {{{0.0, 0.0, 0.0}, {a, 0.0, 0.0}, {a / 2.0, h, 0.0}, {a / 2.0, h / 3.0, apexMillimetres / 1000.0}}};
}

/// A needle: an equilateral base of @p baseMillimetres with the apex 10 mm away, so shrinking the base drives the element
/// toward a spike rather than a sheet.
[[nodiscard]] Corners needle(double baseMillimetres) {
    const double b = baseMillimetres / 1000.0;
    const double h = b * std::sqrt(3.0) / 2.0;
    return {{{0.0, 0.0, 0.0}, {b, 0.0, 0.0}, {b / 2.0, h, 0.0}, {b / 2.0, h / 3.0, 0.010}}};
}

[[nodiscard]] AsmHand oneTet(const Corners& corners) {
    Nodes nodes;
    for (std::size_t i = 0; i < 4; ++i) {
        nodes.emplace_back(static_cast<NodeId::ValueType>(i + 1), at(corners[i][0], corners[i][1], corners[i][2]));
    }
    return makeAsmHand(nodes, {}, {{0, 1, 2, 3}});
}

/// A field `T = base + g . x`, with its three coefficients typed in by the caller.
struct Field {
    double base = 0.0;
    std::array<double, 3> g{};
};

struct Measurement {
    double radiusRatio = 0.0;
    double aspectRatio = 0.0;
    double relativeError = 0.0;
    bool structurallyValid = false;
    bool recovered = false;
};

[[nodiscard]] Measurement measure(const Corners& corners, const Field& field) {
    Measurement m;
    const AsmHand hand = oneTet(corners);
    const meshing::MeshQualityReport quality = meshing::evaluateMeshQuality(hand.mesh, meshing::reportOnlyThresholds());
    m.structurallyValid = quality.structurallyValid;
    const auto radius = quality.summaries.find(QualityMetric::TetRadiusRatio);
    const auto aspect = quality.summaries.find(QualityMetric::TetAspectRatio);
    REQUIRE(radius != quality.summaries.end());
    REQUIRE(aspect != quality.summaries.end());
    m.radiusRatio = radius->second.minimum;
    m.aspectRatio = aspect->second.maximum;

    std::vector<Temperature> temperatures;
    for (const meshing::Node& node : hand.mesh.nodes()) {
        const double x = node.position.x.si();
        const double y = node.position.y.si();
        const double z = node.position.z.si();
        temperatures.push_back(Temperature::fromSi(field.base + field.g[0] * x + field.g[1] * y + field.g[2] * z));
    }
    const auto recovered = thermal::recoverThermalElementFields(hand.mesh, hand.dofs, ThermalConductivity::fromSi(45.0), temperatures);
    if (!recovered.has_value()) {
        return m;
    }
    m.recovered = true;
    REQUIRE(recovered->gradients.size() == 1);
    const thermal::TemperatureGradient3& got = recovered->gradients[0];
    const std::array<double, 3> have{got.x.si(), got.y.si(), got.z.si()};
    double worst = 0.0;
    for (std::size_t i = 0; i < 3; ++i) {
        worst = std::max(worst, std::abs(have[i] - field.g[i]));
    }
    m.relativeError = worst / std::sqrt(field.g[0] * field.g[0] + field.g[1] * field.g[1] + field.g[2] * field.g[2]);
    return m;
}

// THE ISOLATING FIELDS: no absolute offset, so the only rounding is relative to the field itself and the error that remains is
// what the SHAPE of the element does. This is the measurement the law is fitted on.
constexpr Field kFieldA{.base = 0.0, .g = {7.2, -3.5, 11.4}};              // K/m, all three components nonzero
constexpr Field kFieldB{.base = 0.0, .g = {7200.0, -3500.0, 11400.0}};     // three orders larger
// THE ABSOLUTE FIELDS: the same gradients on a 300 K level. Temperatures are absolute (kelvin) and a rounding of 1e-16 of 300 K is
// 3e-14 K whatever the element looks like, so the relative error of a SMALL gradient is dominated by that and not by the shape.
// Reported beside the law so the two are never confused; the floor it implies is not a mesh property.
// A prescribed temperature must be non-negative, so the patch's isolating field has all-positive coefficients.
constexpr Field kFieldPositive{.base = 0.0, .g = {7.2, 3.5, 11.4}};
constexpr Field kAbsoluteA{.base = 307.3, .g = {7.2, -3.5, 11.4}};
constexpr Field kAbsoluteB{.base = 411.9, .g = {7200.0, -3500.0, 11400.0}};

} // namespace

namespace {

/// One element of a degeneration family, measured under the four fields.
struct Row {
    double parameter = 0.0;       // the apex height or the base, mm
    double radiusRatio = 0.0;
    double aspectRatio = 0.0;
    double isolatingA = 0.0;      // relative gradient error, field with no absolute offset
    double isolatingB = 0.0;
    double absoluteA = 0.0;       // the same gradients on a 307 K / 412 K level
    double absoluteB = 0.0;
};

[[nodiscard]] Row measureRow(double parameter, const Corners& corners) {
    const Measurement a = measure(corners, kFieldA);
    const Measurement b = measure(corners, kFieldB);
    const Measurement absoluteA = measure(corners, kAbsoluteA);
    const Measurement absoluteB = measure(corners, kAbsoluteB);
    REQUIRE(a.structurallyValid);
    REQUIRE((a.recovered && b.recovered && absoluteA.recovered && absoluteB.recovered));
    return Row{.parameter = parameter,
               .radiusRatio = a.radiusRatio,
               .aspectRatio = a.aspectRatio,
               .isolatingA = a.relativeError,
               .isolatingB = b.relativeError,
               .absoluteA = absoluteA.relativeError,
               .absoluteB = absoluteB.relativeError};
}

[[nodiscard]] std::vector<Row> flatteningRows() {
    std::vector<Row> rows;
    for (const double apex : {10.0, 1.0, 0.1, 1.0e-2, 1.0e-4, 3.0e-5, 1.0e-5, 1.0e-6, 1.0e-8, 1.0e-10}) {
        rows.push_back(measureRow(apex, wedge(apex)));
    }
    return rows;
}

[[nodiscard]] std::vector<Row> stretchingRows() {
    std::vector<Row> rows;
    for (const double base : {10.0, 1.0, 0.1, 0.01, 1.0e-3, 1.0e-4, 1.0e-5, 1.0e-6}) {
        rows.push_back(measureRow(base, needle(base)));
    }
    return rows;
}

} // namespace

TEST_CASE("ThermalValidation_MeasuresHowGradientAccuracyDegradesWithElementShape", "[thermal][validation][law][reference]") {
    const std::vector<Row> flattening = flatteningRows();
    const std::vector<Row> stretching = stretchingRows();

    double worstFlatteningAbsolute = 0.0;
    double worstFlatteningIsolating = 0.0;
    for (const Row& r : flattening) {
        const double fitA = r.absoluteA * std::sqrt(r.radiusRatio);
        const double fitB = r.absoluteB * std::sqrt(r.radiusRatio);
        const double isoA = r.isolatingA * std::sqrt(r.radiusRatio);
        WARN(std::format("LAW flattening | apex {:.1e} mm | 3r/R {:.5e} | aspect {:.5f} | abs err A {:.5e} | abs err B {:.5e} | A*sqrt(3r/R) {:.5e} | B*sqrt(3r/R) {:.5e} | iso err A {:.5e} | iso A*sqrt(3r/R) {:.5e}",
                         r.parameter, r.radiusRatio, r.aspectRatio, r.absoluteA, r.absoluteB, fitA, fitB, r.isolatingA, isoA));
        worstFlatteningAbsolute = std::max(worstFlatteningAbsolute, fitA);
        worstFlatteningIsolating = std::max(worstFlatteningIsolating, isoA);
        CHECK(r.aspectRatio < std::sqrt(3.0) + 1.0e-9);   // the aspect ratio SATURATES: it sees nothing of this family
        // THE LAW BAND, PINNED, because the floor in ThermalValidation.hpp rests on it: 1.7x above the largest constant measured.
        CHECK(fitA < 1.5e-12);
        CHECK(fitB < 4.0e-15);
        CHECK(isoA < 5.0e-16);
    }
    double worstStretchingAbsolute = 0.0;
    double worstStretchingIsolating = 0.0;
    for (const Row& r : stretching) {
        const double fitA = r.absoluteA / r.aspectRatio;
        const double fitB = r.absoluteB / r.aspectRatio;
        const double isoA = r.isolatingA / r.aspectRatio;
        WARN(std::format("LAW stretching | base {:.1e} mm | 3r/R {:.5e} | aspect {:.5e} | abs err A {:.5e} | abs err B {:.5e} | A/aspect {:.5e} | B/aspect {:.5e} | iso err A {:.5e} | iso A/aspect {:.5e} | A*sqrt(3r/R) {:.5e}",
                         r.parameter, r.radiusRatio, r.aspectRatio, r.absoluteA, r.absoluteB, fitA, fitB, r.isolatingA, isoA,
                         r.absoluteA * std::sqrt(r.radiusRatio)));
        worstStretchingAbsolute = std::max(worstStretchingAbsolute, fitA);
        worstStretchingIsolating = std::max(worstStretchingIsolating, isoA);
        CHECK(fitA < 1.0e-12);
        CHECK(fitB < 1.0e-14);
    }
    WARN(std::format("LAW constants | flattening C (absolute field A) {:.5e} | flattening C (isolating) {:.5e} | stretching C (absolute field A) {:.5e} | stretching C (isolating) {:.5e}",
                     worstFlatteningAbsolute, worstFlatteningIsolating, worstStretchingAbsolute, worstStretchingIsolating));

    // THE TWO FAMILIES ARE DIFFERENT LAWS, which is why there are two bounds: Law 1 does not describe the needle at all.
    // (The constant Law 1 would need there climbs by orders of magnitude while the stretching constant stays put.)
    const Row& needleEnd = stretching.back();
    CHECK(needleEnd.absoluteA * std::sqrt(needleEnd.radiusRatio) > 1.0e-10);
    CHECK(needleEnd.radiusRatio > 1.0e-7);   // the radius ratio of the needle is "fine" -- and the element is still a needle
}

TEST_CASE("ThermalValidation_TheBoundsGuaranteeTheStatedGradientAccuracy", "[thermal][validation][law][reference]") {
    // THE DERIVATION CHECKED AT THE POINT WHERE IT LANDS, in both directions, for both bounds. An element the policy ACCEPTS must
    // recover the reference gradient to within the stated accuracy; and each bound must refuse something whose error really is
    // outside it (not only conservatively refuse things that were fine).
    const std::vector<Row> flattening = flatteningRows();
    const std::vector<Row> stretching = stretchingRows();

    bool flatteningRefusedGenuinely = false;
    for (const Row& r : flattening) {
        const bool refused = r.radiusRatio < thermal::kThermalRadiusRatioFloor;
        if (!refused) {
            CHECK(r.absoluteA < thermal::kThermalGradientAccuracy);
            // ... and with the margin the derivation claims: a factor of three inside.
            CHECK(r.absoluteA * 3.0 < thermal::kThermalGradientAccuracy);
        } else if (r.absoluteA > thermal::kThermalGradientAccuracy) {
            flatteningRefusedGenuinely = true;
        }
    }
    CHECK(flatteningRefusedGenuinely);

    bool stretchingRefusedGenuinely = false;
    for (const Row& r : stretching) {
        const bool refused = r.aspectRatio > thermal::kThermalAspectRatioCeiling;
        if (!refused) {
            CHECK(r.absoluteA * 3.0 < thermal::kThermalGradientAccuracy);
        } else if (r.absoluteA > thermal::kThermalGradientAccuracy) {
            stretchingRefusedGenuinely = true;
        }
    }
    CHECK(stretchingRefusedGenuinely);

    // The constants are not free numbers: they are what the measured laws give at the stated factor of three.
    //   floor   = (3 C / accuracy)^2 with C = 8.62e-13       (rounded UP, so the error at the bound is inside the three-fold margin)
    //   ceiling = accuracy / (3 C)   with C = 4.28e-13       (rounded DOWN)
    CHECK(thermal::kThermalRadiusRatioFloor >= std::pow(3.0 * 8.62e-13 / thermal::kThermalGradientAccuracy, 2.0));
    CHECK(thermal::kThermalAspectRatioCeiling <= thermal::kThermalGradientAccuracy / (3.0 * 4.28e-13));
    // and they are not wildly more conservative than that: within a factor of two of the three-fold-margin value.
    CHECK(thermal::kThermalRadiusRatioFloor < 2.0 * std::pow(3.0 * 8.62e-13 / thermal::kThermalGradientAccuracy, 2.0));
    CHECK(thermal::kThermalAspectRatioCeiling > 0.5 * thermal::kThermalGradientAccuracy / (3.0 * 4.28e-13));
}

namespace {

/// A patch of FOUR tetrahedra around ONE interior node P, inside the 10 mm tetrahedron A B C D. P sits at
/// `(1 - s) * centroid(A B C D) + s * centroid(A B C)`: s = 0 is the regular split and s -> 1 brings P onto the face A B C, which
/// flattens the sub-tetrahedron A B C P toward a plane while the other three stay moderate. The four outer nodes are PRESCRIBED to an
/// affine temperature field; P alone is free. A linear element reproduces an affine field EXACTLY, so the solved T(P) must be the
/// field's value at P -- whatever it differs by is what the degraded element cost the SOLVE (not the recovery).
struct PatchResult {
    double radiusRatio = 0.0;
    double aspectRatio = 0.0;
    double error = 0.0;          // |T(P) - exact| / (temperature change across the patch)
    double pivotRatio = 0.0;
    double residual = 0.0;
    bool solved = false;
    std::optional<thermal::SteadySolveProblem> problem{};
};

[[nodiscard]] PatchResult patchSolve(double s, const Field& field) {
    const double L = 0.010;
    const std::array<std::array<double, 3>, 4> outer{{{0, 0, 0}, {L, 0, 0}, {0, L, 0}, {0, 0, L}}};
    std::array<double, 3> centroid{}, face{};
    for (std::size_t c = 0; c < 3; ++c) {
        centroid[c] = (outer[0][c] + outer[1][c] + outer[2][c] + outer[3][c]) / 4.0;
        face[c] = (outer[0][c] + outer[1][c] + outer[2][c]) / 3.0;
    }
    std::array<double, 3> p{};
    for (std::size_t c = 0; c < 3; ++c) {
        p[c] = (1.0 - s) * centroid[c] + s * face[c];
    }
    Nodes nodes;
    for (std::size_t i = 0; i < 4; ++i) {
        nodes.emplace_back(static_cast<NodeId::ValueType>(i + 1), at(outer[i][0], outer[i][1], outer[i][2]));
    }
    nodes.emplace_back(5, at(p[0], p[1], p[2]));
    // indices: A=0 B=1 C=2 D=3 P=4; every sub-tetrahedron replaces one corner by P and keeps the orientation.
    const AsmHand hand = makeAsmHand(nodes, {}, {{4, 1, 2, 3}, {0, 4, 2, 3}, {0, 1, 4, 3}, {0, 1, 2, 4}});

    PatchResult out;
    const meshing::MeshQualityReport quality = meshing::evaluateMeshQuality(hand.mesh, meshing::reportOnlyThresholds());
    REQUIRE(quality.structurallyValid);
    out.radiusRatio = quality.summaries.at(QualityMetric::TetRadiusRatio).minimum;
    out.aspectRatio = quality.summaries.at(QualityMetric::TetAspectRatio).maximum;

    const auto exact = [&](const std::array<double, 3>& x) { return field.base + field.g[0] * x[0] + field.g[1] * x[1] + field.g[2] * x[2]; };
    std::vector<thermal::ThermalTemperatureConstraint> prescribed;
    for (std::size_t i = 0; i < 4; ++i) {
        const Result<ThermalDofIndex> dof = hand.dofs.dofOf(NodeId::fromValue(static_cast<NodeId::ValueType>(i + 1)));
        REQUIRE(dof.has_value());
        prescribed.push_back({*dof, Temperature::fromSi(exact(outer[i]))});
    }
    const auto constraints = thermal::buildThermalConstraintMap(hand.dofs, prescribed);
    INFO((constraints.has_value() ? std::string{} : constraints.error().message));
    REQUIRE(constraints.has_value());
    const auto equations = thermal::buildThermalFreeEquationMap(hand.dofs, *constraints);
    REQUIRE(equations.has_value());
    const thermal::ThermalSystem system = assemble(hand, 45.0, std::nullopt, noHeat(hand));
    const auto solved = thermal::solveThermalAlgebra(system.conductance(), system.heat(), *equations, *constraints, thermal::ThermalSolverSettings{});
    if (!solved.has_value()) {
        out.problem = solved.error().problem;
        return out;
    }
    out.solved = true;
    out.pivotRatio = solved->pivotRatio;
    out.residual = solved->residual.normalized;
    const Result<ThermalDofIndex> pDof = hand.dofs.dofOf(NodeId::fromValue(5));
    REQUIRE(pDof.has_value());
    const double got = solved->temperatures[static_cast<std::size_t>(pDof->value())].si();
    const double change = std::abs(field.g[0] * L) + std::abs(field.g[1] * L) + std::abs(field.g[2] * L);
    out.error = std::abs(got - exact(p)) / change;
    return out;
}

} // namespace

TEST_CASE("ThermalValidation_MeasuresHowTheSolveDegradesWithAFlatteningElementInAPatch", "[thermal][validation][law][patch]") {
    // The SOLVE, not the recovery: an affine patch is reproduced exactly by a linear element, so any difference is what the degraded
    // sub-tetrahedron cost. Printed for the absolute-level field (the realistic case) and for the isolating one.
    for (const double s : {0.0, 0.5, 0.9, 0.99, 0.999, 1.0 - 1.0e-4, 1.0 - 1.0e-5, 1.0 - 1.0e-6, 1.0 - 1.0e-8, 1.0 - 1.0e-10}) {
        const PatchResult absolute = patchSolve(s, kAbsoluteA);
        const PatchResult isolating = patchSolve(s, kFieldPositive);
        WARN(std::format("LAW patch | s {:.10f} | worst 3r/R {:.5e} | worst aspect {:.5f} | solved {} | T(P) error at 307 K level {:.5e} | pivot {:.5e} | residual {:.5e} | error on isolating field {:.5e}",
                         s, absolute.radiusRatio, absolute.aspectRatio, absolute.solved ? "yes" : "NO", absolute.error, absolute.pivotRatio,
                         absolute.residual, isolating.error));
        if (!absolute.solved) {
            WARN(std::format("LAW patch refused | s {:.10f} | problem {}", s, absolute.problem.has_value() ? thermal::toString(*absolute.problem) : "none"));
        }
    }
}
