/// \file
/// P19-CONV-001's compile-fail group: what the type system must make
/// impossible, ADR-072.
///
/// THE CLAIM THIS GROUP CARRIES. Five things, and the last three cannot be
/// reached by any value test:
///
/// 1. A `CfdConvectionSystem` cannot be CONJURED. It holds a `CfdSparseMatrix`,
///    whose default constructor is private because possession is the evidence
///    (ADR-036) -- so a system that exists holds a matrix that went through
///    `fromCsr`, and a default-constructed one does not compile.
/// 2. THE TWO INPUTS CANNOT BE SWAPPED. `CfdConvectionInput` carries a FACE
///    array and a scheme; `CfdMassFluxInput` carries a CELL array and a
///    density. They are different shapes with different lengths, and a runtime
///    tag would turn a `3 * cells` versus `faces` mix-up into something a
///    caller can ignore.
/// 3. THE ASSEMBLY TAKES NO DENSITY. ADR-072's split is load-bearing: `phi`
///    already carries `rho`, so handing the assembly a density is a
///    misunderstanding of what a kg/s face value means. No value test can show
///    it is impossible -- only a compile error can.
/// 4. THE ASSEMBLY'S FAILURE IS TAGGED, NOT A PLAIN `Error`. ADR-072 Decision
///    10 returns `std::expected<CfdConvectionSystem, CfdConvectionFailure>`
///    rather than `Result<CfdConvectionSystem>`, which is what makes
///    `ResultIsNotFinite` observable as a tag instead of grepped out of a
///    sentence -- P19-DIV-001's recorded limitation L10. Binding the result to
///    a `Result` must fail, or the distinction is cosmetic.
/// 5. THE DIMENSIONED SUMMARY MEMBERS ARE TYPED. `inflow`, `outflow` and
///    `prescribedInflow` are `MassFlowRate`, not bare doubles, so a raw number
///    cannot be assigned to one and a `MassFlowRate` cannot be confused with a
///    `VolumetricFlowRate` or a `Density`. ARCHITECTURE.md's rule against a
///    unitless engineering API, enforced by the compiler.
///
/// EVERY HELPER IS `[[maybe_unused]] static`, AND BOTH WORDS EARN THEIR PLACE.
/// `static` gives it internal linkage, so `-Wmissing-declarations` cannot fire
/// -- which matters because a probe that can exit non-zero for a reason
/// UNRELATED to its claim proves nothing, the hazard P18-ELEM-001 recorded as
/// its finding F3 and P19-GRAD-001 hit on all 24 of its own cases.
/// `[[maybe_unused]]` is then needed because each is called only from inside a
/// `#if defined(BETTERCAD_CF_...)` block, so in the CONTROL configuration -- no
/// macro, the build every preset runs -- they are unused, and this translation
/// unit is compiled with `-Werror`. P19-GEOM-001 recorded what a failing
/// control costs: it broke `cmake --build` for the whole project.
///
/// EVERY EXPECTED DIAGNOSTIC IN THE CMAKE GROUP WAS READ OFF THE COMPILER, not
/// predicted. P19-GRAD-001 had three gates fail because its regex did not list
/// `invalid initialization`, which is what GCC emits for a reference bound to
/// the wrong type -- the gates were right and the expectation was incomplete.

#include <bettercad/cfd/CfdBoundary.hpp>
#include <bettercad/cfd/CfdConvection.hpp>
#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdDivergence.hpp>
#include <bettercad/cfd/CfdGeometry.hpp>
#include <bettercad/cfd/CfdSparse.hpp>
#include <bettercad/cfd/CfdTopology.hpp>
#include <bettercad/core/Error.hpp>

#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <vector>

using namespace bettercad;
using namespace bettercad::cfd;

namespace {

// ---------------------------------------------------------------------------
// 1 -- a system cannot be conjured
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_DEFAULT_CONSTRUCT_A_CONVECTION_SYSTEM)
[[maybe_unused]] static CfdConvectionSystem conjureASystem() {
    // The matrix's default constructor is private: possession is the evidence.
    return CfdConvectionSystem{};
}
#endif

#if defined(BETTERCAD_CF_DEFAULT_CONSTRUCT_A_SPARSE_MATRIX)
[[maybe_unused]] static CfdSparseMatrix conjureAMatrix() { return CfdSparseMatrix{}; }
#endif

// ---------------------------------------------------------------------------
// 2 -- the two inputs cannot be swapped
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_CONVECTION_INPUT_IS_A_MASS_FLUX_INPUT)
[[maybe_unused]] static CfdMassFluxInput convectionInputIsAMassFluxInput(
    const CfdConvectionInput& input) {
    return input;
}
#endif

#if defined(BETTERCAD_CF_A_MASS_FLUX_INPUT_IS_A_CONVECTION_INPUT)
[[maybe_unused]] static CfdConvectionInput massFluxInputIsAConvectionInput(
    const CfdMassFluxInput& input) {
    return input;
}
#endif

#if defined(BETTERCAD_CF_THE_ASSEMBLY_TAKES_A_MASS_FLUX_INPUT)
[[maybe_unused]] static auto assemblyTakesAMassFluxInput(const CfdTopology& topology,
                                                         const CfdGeometry& geometry,
                                                         const PreparedCfdBoundary& boundary,
                                                         const CfdMassFluxInput& input) {
    return assembleCfdConvection(topology, geometry, boundary, input);
}
#endif

#if defined(BETTERCAD_CF_THE_PRODUCER_TAKES_A_CONVECTION_INPUT)
[[maybe_unused]] static auto producerTakesAConvectionInput(const CfdTopology& topology,
                                                           const CfdGeometry& geometry,
                                                           const PreparedCfdBoundary& boundary,
                                                           const CfdConvectionInput& input) {
    return cfdMassFlux(topology, geometry, boundary, input);
}
#endif

// ---------------------------------------------------------------------------
// 3 -- the assembly takes no density, and the producer needs one
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_CONVECTION_INPUT_HOLDS_A_DENSITY)
[[maybe_unused]] static Density convectionInputHoldsADensity(const CfdConvectionInput& input) {
    // ADR-072: `phi` already carries `rho`, so there is no density here.
    return input.density;
}
#endif

#if defined(BETTERCAD_CF_A_CONVECTION_INPUT_HOLDS_A_VELOCITY)
[[maybe_unused]] static std::span<const double> convectionInputHoldsAVelocity(
    const CfdConvectionInput& input) {
    return input.currentVelocity;
}
#endif

#if defined(BETTERCAD_CF_A_MASS_FLUX_INPUT_HOLDS_A_SCHEME)
[[maybe_unused]] static CfdConvectionScheme massFluxInputHoldsAScheme(
    const CfdMassFluxInput& input) {
    // The scheme is the ASSEMBLY's choice; the producer interpolates a face
    // velocity and has no upwind direction to pick.
    return input.scheme;
}
#endif

#if defined(BETTERCAD_CF_THE_DENSITY_TAKES_A_BARE_DOUBLE)
[[maybe_unused]] static CfdMassFluxInput densityTakesABareDouble(std::span<const double> cells) {
    return CfdMassFluxInput{cells, 998.0};
}
#endif

// ---------------------------------------------------------------------------
// 4 -- the failure channel is tagged
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_THE_ASSEMBLY_RETURNS_A_PLAIN_RESULT)
[[maybe_unused]] static Result<CfdConvectionSystem> assemblyReturnsAPlainResult(
    const CfdTopology& topology, const CfdGeometry& geometry, const PreparedCfdBoundary& boundary,
    const CfdConvectionInput& input) {
    // ADR-072 Decision 10: the error type is `CfdConvectionFailure`, not
    // `Error`, so the output-judging refusal is observable as a TAG.
    return assembleCfdConvection(topology, geometry, boundary, input);
}
#endif

#if defined(BETTERCAD_CF_THE_PRODUCER_RETURNS_A_PLAIN_RESULT)
[[maybe_unused]] static Result<std::vector<double>> producerReturnsAPlainResult(
    const CfdTopology& topology, const CfdGeometry& geometry, const PreparedCfdBoundary& boundary,
    const CfdMassFluxInput& input) {
    return cfdMassFlux(topology, geometry, boundary, input);
}
#endif

#if defined(BETTERCAD_CF_A_FAILURE_IS_AN_ERROR)
[[maybe_unused]] static Error failureIsAnError(const CfdConvectionFailure& failure) {
    return failure;
}
#endif

#if defined(BETTERCAD_CF_A_FAILURE_IS_ITS_PROBLEM)
[[maybe_unused]] static CfdConvectionProblem failureIsItsProblem(
    const CfdConvectionFailure& failure) {
    return failure;
}
#endif

// ---------------------------------------------------------------------------
// 5 -- the dimensioned members are typed
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_AN_INFLOW_IS_A_BARE_DOUBLE)
[[maybe_unused]] static double inflowIsABareDouble(const CfdConvectionSummary& summary) {
    return summary.inflow;
}
#endif

#if defined(BETTERCAD_CF_AN_INFLOW_TAKES_A_BARE_DOUBLE)
[[maybe_unused]] static void inflowTakesABareDouble(CfdConvectionSummary& summary) {
    summary.inflow = 1.0;
}
#endif

#if defined(BETTERCAD_CF_A_MASS_FLOW_RATE_IS_A_VOLUMETRIC_FLOW_RATE)
[[maybe_unused]] static VolumetricFlowRate massFlowRateIsVolumetric(
    const CfdConvectionSummary& summary) {
    return summary.inflow;
}
#endif

#if defined(BETTERCAD_CF_A_MASS_FLOW_RATE_IS_A_DENSITY)
[[maybe_unused]] static Density massFlowRateIsADensity(const CfdConvectionSummary& summary) {
    return summary.outflow;
}
#endif

#if defined(BETTERCAD_CF_A_MASS_FLOW_RATE_IS_A_MASS)
[[maybe_unused]] static Mass massFlowRateIsAMass(const CfdConvectionSummary& summary) {
    return summary.prescribedInflow;
}
#endif

#if defined(BETTERCAD_CF_A_FLOW_SUM_MIXES_DIMENSIONS)
[[maybe_unused]] static MassFlowRate flowSumMixesDimensions(const CfdConvectionSummary& summary,
                                                            Density density) {
    return summary.inflow + density;
}
#endif

// ---------------------------------------------------------------------------
// The enums are not integers, and not each other
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_CONVECTION_PROBLEM_IS_AN_INTEGER)
[[maybe_unused]] static int problemIsAnInteger(CfdConvectionProblem problem) { return problem; }
#endif

#if defined(BETTERCAD_CF_A_CONVECTION_PROBLEM_IS_A_BOUNDARY_POLICY)
[[maybe_unused]] static CfdBoundaryPolicy problemIsAPolicy(CfdConvectionProblem problem) {
    return problem;
}
#endif

#if defined(BETTERCAD_CF_A_SCHEME_IS_AN_INTEGER)
[[maybe_unused]] static int schemeIsAnInteger(CfdConvectionScheme scheme) { return scheme; }
#endif

#if defined(BETTERCAD_CF_A_SCHEME_IS_A_VECTOR_COMPONENT)
[[maybe_unused]] static VectorComponent schemeIsAComponent(CfdConvectionScheme scheme) {
    // Both are small enums on the same input struct, and confusing them would
    // silently assemble the wrong component with the wrong interpolation.
    return scheme;
}
#endif

#if defined(BETTERCAD_CF_A_VECTOR_COMPONENT_IS_A_SCHEME)
[[maybe_unused]] static CfdConvectionScheme componentIsAScheme(VectorComponent component) {
    return component;
}
#endif

#if defined(BETTERCAD_CF_A_CONVECTION_PROBLEM_IS_A_DIVERGENCE_PROBLEM)
[[maybe_unused]] static CfdDivergenceProblem convectionProblemIsADivergenceProblem(
    CfdConvectionProblem problem) {
    return problem;
}
#endif

// ---------------------------------------------------------------------------
// The face flux is a span of plain SI doubles, not a typed array
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_THE_FACE_FLUX_TAKES_TYPED_RATES)
[[maybe_unused]] static CfdConvectionInput faceFluxTakesTypedRates(
    std::span<const MassFlowRate> typed) {
    return CfdConvectionInput{typed, CfdConvectionScheme::Upwind, VectorComponent::X};
}
#endif

#if defined(BETTERCAD_CF_A_SUMMARY_HOLDS_THE_MATRIX)
[[maybe_unused]] static CfdSparseMatrix summaryHoldsTheMatrix(
    const CfdConvectionSummary& summary) {
    return summary.matrix;
}
#endif

} // namespace

// The CONTROL configuration compiles this translation unit with no macro
// defined, so every block above is excluded and this file must build cleanly.
// That is the build every preset runs, and a control that fails breaks
// `cmake --build` for the whole project.
int main() { return 0; }
