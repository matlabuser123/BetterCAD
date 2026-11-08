/// \file
/// P19-MOM-001's compile-fail group: what the type system must make
/// impossible, ADR-073.
///
/// THE CLAIMS THIS GROUP CARRIES. Seven, matching the seven sections of the
/// body -- AN EARLIER VERSION SAID "Five things" while the body was sectioned
/// into eight, and an independent review counted eleven of the 28 cases sitting
/// outside any claim the header made. The numbering is the body's now.
///
/// 1. A `CfdMomentumSystem` cannot be DEFAULT-CONSTRUCTED. It holds a
///    `CfdSparseMatrix`, whose default constructor is private because
///    possession is the evidence (ADR-036), so a momentum system that exists
///    holds a matrix that went through `fromCsr`.
///
///    NOT "CANNOT BE CONJURED", WHICH AN EARLIER DRAFT CLAIMED AND THE SAME
///    REVIEW OVERTURNED. `CfdMomentumSystem` is an aggregate with a public
///    `matrix` member, so a caller already holding any validated
///    `CfdSparseMatrix` can fabricate a system around it with an arbitrary
///    source and an arbitrary summary. The narrow claim -- that the MATRIX
///    inside one has been validated -- is the true one and is the one worth
///    making.
/// 2. THE TWO GRADIENT FIELDS CANNOT BE SWAPPED. `pressureGradient` is
///    `PressureGradient3` in Pa/m and one per CELL; `faceGradient` is
///    `VelocityGradient3` in 1/s and one per FACE. Both are three doubles in a
///    span, so only the quantity type separates them -- and swapping them would
///    produce a finite, plausible, dimensionally nonsensical momentum source
///    that every finiteness gate accepts.
/// 3. THE FLUID PROPERTIES AND THE TIME STEP CANNOT BE SWAPPED OR UNTYPED.
///    `Density` and `DynamicViscosity` are different `Quantity`
///    instantiations, so `rho` cannot be handed in as `mu`; nor can the time
///    step take a bare double or a `Length`.
///
///    A SWAP WOULD BE CAUGHT AT RUNTIME TOO, and an earlier draft said no value
///    test would notice it. That is true of an arbitrary fixture and false of
///    this suite, whose
///    `CfdMomentum_TheTransientDiagonalIsDensityTimesVolumeOverTheTimeStep`
///    pins the density in closed form -- an independent review made the
///    distinction. The compile-time guard is still worth having, because it
///    moves the failure from a test run to a build.
/// 4. THE ASSEMBLY'S FAILURE IS TAGGED, NOT A PLAIN `Error`. ADR-073 Decision
///    10 returns `std::expected<CfdMomentumSystem, CfdMomentumFailure>` rather
///    than `Result<CfdMomentumSystem>`, which is what makes
///    `ResultIsNotFinite` observable as a tag instead of grepped out of a
///    sentence -- P19-DIV-001's recorded limitation L10, resolved by
///    P19-CONV-001 and inherited here. Binding the result to a `Result` must
///    fail, or the distinction is cosmetic.
/// 5. THE TRANSIENT COEFFICIENTS ARE TYPED, AND TYPED AS A MASS FLOW RATE.
///    `smallestTransientCoefficient` and `largestTransientCoefficient` are
///    `MassFlowRate` -- the SAME quantity as `phi` and as `mu A / d`, which is
///    the whole of ADR-073 Decision 3's claim that the three matrix terms are
///    commensurate. A bare double there would make the claim unobservable, and
///    a `Force` there would make it false.
/// 6. GRAVITY IS A RECORD, NOT A VECTOR AND NOT A MAGNITUDE. `CfdData.hpp`
///    makes the absence of the record the absence of gravity, so the input
///    holds an `optional<CfdGravity>` and not an `Acceleration3` whose zero
///    would be a second spelling of none. Two cases, which the earlier header
///    did not mention.
/// 7. THE INPUT AND THE SUMMARY HOLD THE MEMBERS THEY HOLD. No pressure field
///    (the operator takes a GRADIENT, and choosing the reconstruction is the
///    analysis's decision), no reference height (ADR-073 Decision 6: reading it
///    here would apply the hydrostatic field twice), and no matrix on the
///    summary. Five cases, which the earlier header did not mention either.
///    Structurally these are the weakest cases in the group -- a misspelling
///    would "pass" one too -- but their content, that nothing may be ADDED
///    under those names, is legitimate and the convection group carries the
///    same.
///
/// THERE IS NO CASE FOR THE PREVIOUS-VERSUS-CURRENT VELOCITY SWAP, AND THAT IS
/// THE MOST IMPORTANT SENTENCE IN THIS FILE. `previousVelocity` and
/// `currentVelocity` are both `std::span<const double>` of length `3 * cells`
/// in the same component-blocked layout, so they are THE SAME TYPE and no
/// compile-fail case could distinguish them. ADR-073 Decision 4 records the
/// consequence -- passing the current field where the previous one belongs
/// converges to the correct steady answer and passes every steady assertion --
/// and the trap is closed by a name, a comment and
/// `CfdMomentum_TheTransientSourceCarriesThePreviousFieldAndNotTheCurrentOne`,
/// not by the compiler. Claiming a compile-fail case for it would be exactly
/// the false claim P19-CONV-001's review found in `CfdConvection.hpp`, where a
/// runtime asymmetry was documented as having a type-level expression it could
/// not have.
///
/// EVERY HELPER IS `[[maybe_unused]]`, AND THAT IS THE WORD THAT EARNS ITS
/// PLACE. Each is called only from inside a `#if defined(BETTERCAD_CF_...)`
/// block, so in the CONTROL configuration -- no macro, the build every preset
/// runs -- they are unused, and this translation unit is compiled with
/// `-Werror`. P19-GEOM-001 recorded what a failing control costs: it broke
/// `cmake --build` for the whole project. The hazard it guards against is a
/// probe that can exit non-zero for a reason UNRELATED to its claim, which
/// proves nothing -- P18-ELEM-001's finding F3, and P19-GRAD-001 hit it on all
/// 24 of its own cases.
///
/// `static` IS REDUNDANT HERE AND IS KEPT FOR CONSISTENCY WITH THE OTHER
/// GROUPS. An earlier version of this paragraph said `static` was what stopped
/// `-Wmissing-declarations` firing; an independent review compiled the
/// counter-example and showed that the unnamed namespace already gives internal
/// linkage, so `namespace { [[maybe_unused]] int f() {...} }` is clean under
/// `-Wall -Wextra -Wmissing-declarations -Wunused -Werror` with no `static` at
/// all. The misattribution is corrected rather than the keyword removed,
/// because every other compile-fail group in this directory spells it the same
/// way and a lone exception would be the more confusing of the two.
///
/// EVERY EXPECTED DIAGNOSTIC IN THE CMAKE GROUP WAS READ OFF THE COMPILER, not
/// predicted. P19-GRAD-001 had three gates fail because its regex did not list
/// `invalid initialization`, which is what GCC emits for a reference bound to
/// the wrong type -- the gates were right and the expectation was incomplete.

#include <bettercad/cfd/CfdBoundary.hpp>
#include <bettercad/cfd/CfdConvection.hpp>
#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdDiffusion.hpp>
#include <bettercad/cfd/CfdGeometry.hpp>
#include <bettercad/cfd/CfdMomentum.hpp>
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

#if defined(BETTERCAD_CF_DEFAULT_CONSTRUCT_A_MOMENTUM_SYSTEM)
[[maybe_unused]] static CfdMomentumSystem conjureASystem() { return CfdMomentumSystem{}; }
#endif

// ---------------------------------------------------------------------------
// 2 -- the two gradient fields cannot be swapped
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_THE_PRESSURE_GRADIENT_TAKES_A_VELOCITY_GRADIENT)
[[maybe_unused]] static void pressureGradientTakesAVelocityGradient(
    CfdMomentumInput& input, std::span<const VelocityGradient3> wrong) {
    // Pa/m against 1/s. A swap is dimensionally nonsensical and numerically
    // plausible.
    input.pressureGradient = wrong;
}
#endif

#if defined(BETTERCAD_CF_THE_FACE_GRADIENT_TAKES_A_PRESSURE_GRADIENT)
[[maybe_unused]] static void faceGradientTakesAPressureGradient(
    CfdMomentumInput& input, std::span<const PressureGradient3> wrong) {
    input.faceGradient = wrong;
}
#endif

#if defined(BETTERCAD_CF_A_PRESSURE_GRADIENT_IS_A_VELOCITY_GRADIENT)
[[maybe_unused]] static VelocityGradient3 pressureGradientIsAVelocityGradient(
    const PressureGradient3& gradient) {
    return gradient;
}
#endif

#if defined(BETTERCAD_CF_A_PRESSURE_GRADIENT_TAKES_A_BARE_DOUBLE)
[[maybe_unused]] static PressureGradient3 pressureGradientTakesABareDouble() {
    return PressureGradient3{1.0, 2.0, 3.0};
}
#endif

// ---------------------------------------------------------------------------
// 3 -- the fluid properties cannot be swapped
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_THE_DENSITY_TAKES_A_VISCOSITY)
[[maybe_unused]] static void densityTakesAViscosity(CfdMomentumInput& input,
                                                    DynamicViscosity viscosity) {
    input.density = viscosity;
}
#endif

#if defined(BETTERCAD_CF_THE_VISCOSITY_TAKES_A_DENSITY)
[[maybe_unused]] static void viscosityTakesADensity(CfdMomentumInput& input, Density density) {
    input.viscosity = density;
}
#endif

#if defined(BETTERCAD_CF_THE_DENSITY_TAKES_A_BARE_DOUBLE)
[[maybe_unused]] static void densityTakesABareDouble(CfdMomentumInput& input) {
    input.density = 998.0;
}
#endif

#if defined(BETTERCAD_CF_THE_TIME_STEP_TAKES_A_BARE_DOUBLE)
[[maybe_unused]] static void timeStepTakesABareDouble(CfdMomentumInput& input) {
    input.timeStep = 0.001;
}
#endif

#if defined(BETTERCAD_CF_THE_TIME_STEP_TAKES_A_LENGTH)
[[maybe_unused]] static void timeStepTakesALength(CfdMomentumInput& input, Length length) {
    input.timeStep = length;
}
#endif

// ---------------------------------------------------------------------------
// 4 -- gravity is a record, not a vector, and not a magnitude
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_GRAVITY_TAKES_AN_ACCELERATION_VECTOR)
[[maybe_unused]] static void gravityTakesAnAccelerationVector(CfdMomentumInput& input,
                                                              const Acceleration3& g) {
    // CfdData.hpp: no gravity is the ABSENCE of the record, so the input holds
    // an optional RECORD and not a vector whose zero would be a second
    // spelling of none.
    input.gravity = g;
}
#endif

#if defined(BETTERCAD_CF_A_GRAVITY_RECORD_IS_AN_ACCELERATION_VECTOR)
[[maybe_unused]] static Acceleration3 gravityRecordIsAVector(const CfdGravity& gravity) {
    return gravity;
}
#endif

// ---------------------------------------------------------------------------
// 5 -- the return type is tagged
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_THE_ASSEMBLY_RETURNS_A_PLAIN_RESULT)
[[maybe_unused]] static Result<CfdMomentumSystem> assemblyReturnsAPlainResult(
    const CfdTopology& topology, const CfdGeometry& geometry, const PreparedCfdBoundary& boundary,
    const CfdMomentumInput& input) {
    return assembleCfdMomentum(topology, geometry, boundary, input);
}
#endif

#if defined(BETTERCAD_CF_A_FAILURE_IS_AN_ERROR)
[[maybe_unused]] static Error failureIsAnError(const CfdMomentumFailure& failure) {
    return failure;
}
#endif

#if defined(BETTERCAD_CF_A_FAILURE_IS_ITS_PROBLEM)
[[maybe_unused]] static CfdMomentumProblem failureIsItsProblem(
    const CfdMomentumFailure& failure) {
    return failure;
}
#endif

// ---------------------------------------------------------------------------
// 6 -- the problem enum is its own type
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_MOMENTUM_PROBLEM_IS_AN_INTEGER)
[[maybe_unused]] static int momentumProblemIsAnInteger(CfdMomentumProblem problem) {
    return problem;
}
#endif

#if defined(BETTERCAD_CF_A_MOMENTUM_PROBLEM_IS_A_CONVECTION_PROBLEM)
[[maybe_unused]] static CfdConvectionProblem momentumProblemIsAConvectionProblem(
    CfdMomentumProblem problem) {
    return problem;
}
#endif

#if defined(BETTERCAD_CF_A_MOMENTUM_PROBLEM_IS_A_DIFFUSION_PROBLEM)
[[maybe_unused]] static CfdDiffusionProblem momentumProblemIsADiffusionProblem(
    CfdMomentumProblem problem) {
    return problem;
}
#endif

#if defined(BETTERCAD_CF_A_CONVECTION_PROBLEM_IS_A_MOMENTUM_PROBLEM)
[[maybe_unused]] static CfdMomentumProblem convectionProblemIsAMomentumProblem(
    CfdConvectionProblem problem) {
    return problem;
}
#endif

// ---------------------------------------------------------------------------
// 7 -- the transient coefficients are a MASS FLOW RATE
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_TRANSIENT_COEFFICIENT_IS_A_BARE_DOUBLE)
[[maybe_unused]] static double transientCoefficientIsABareDouble(
    const CfdMomentumSummary& summary) {
    return summary.largestTransientCoefficient;
}
#endif

#if defined(BETTERCAD_CF_A_TRANSIENT_COEFFICIENT_IS_A_FORCE)
[[maybe_unused]] static Force transientCoefficientIsAForce(const CfdMomentumSummary& summary) {
    // ADR-073 Decision 3: the transient coefficient is kg/s, the same quantity
    // as `phi`. A force would be the coefficient already multiplied by a
    // velocity, which is a source entry and not a matrix entry.
    return summary.largestTransientCoefficient;
}
#endif

#if defined(BETTERCAD_CF_A_TRANSIENT_COEFFICIENT_TAKES_A_BARE_DOUBLE)
[[maybe_unused]] static void transientCoefficientTakesABareDouble(CfdMomentumSummary& summary) {
    summary.largestTransientCoefficient = 1.0;
}
#endif

#if defined(BETTERCAD_CF_A_MASS_FLOW_RATE_IS_A_FORCE)
[[maybe_unused]] static Force massFlowRateIsAForce(MassFlowRate rate) {
    // The identity ADR-073 Decision 3 rests on, inverted: a coefficient is not
    // a source entry, and the units framework says so.
    return rate;
}
#endif

// ---------------------------------------------------------------------------
// 8 -- the input's members are the ones it has
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_MOMENTUM_INPUT_HOLDS_A_REFERENCE_HEIGHT)
[[maybe_unused]] static Length momentumInputHoldsAReferenceHeight(
    const CfdMomentumInput& input) {
    // ADR-073 Decision 6: the reference height belongs to prescribed boundary
    // PRESSURES and is reached through the gravity record, never as an input of
    // its own, because reading it here would apply the hydrostatic field twice.
    return input.referenceHeight;
}
#endif

#if defined(BETTERCAD_CF_A_MOMENTUM_INPUT_HOLDS_A_PRESSURE)
[[maybe_unused]] static Pressure momentumInputHoldsAPressure(const CfdMomentumInput& input) {
    // The operator takes a pressure GRADIENT, never a pressure field: choosing
    // the reconstruction is the analysis's decision and not this operator's.
    return input.pressure;
}
#endif

#if defined(BETTERCAD_CF_A_MOMENTUM_SUMMARY_HOLDS_THE_MATRIX)
[[maybe_unused]] static CfdSparseMatrix summaryHoldsTheMatrix(
    const CfdMomentumSummary& summary) {
    return summary.matrix;
}
#endif

#if defined(BETTERCAD_CF_A_MOMENTUM_INPUT_IS_A_CONVECTION_INPUT)
[[maybe_unused]] static CfdConvectionInput momentumInputIsAConvectionInput(
    const CfdMomentumInput& input) {
    return input;
}
#endif

#if defined(BETTERCAD_CF_A_MOMENTUM_INPUT_IS_A_DIFFUSION_INPUT)
[[maybe_unused]] static CfdDiffusionInput momentumInputIsADiffusionInput(
    const CfdMomentumInput& input) {
    return input;
}
#endif

} // namespace

// THE CONTROL CONFIGURATION. With no `BETTERCAD_CF_...` macro defined this
// translation unit must COMPILE cleanly under `-Werror`, because every preset
// builds it that way. A control that fails breaks `cmake --build` for the whole
// project, which is what P19-GEOM-001 recorded.
//
// NOTHING LINKS, SO THIS `main` IS DEAD WEIGHT, and an earlier version of this
// comment said "compile and link". `bettercad_compile_fail_group` creates an
// OBJECT library and that directory's own preamble says so in as many words; an
// independent review checked. It is kept because every other group in this
// directory carries one and the pattern is established, not because anything
// calls it.
int main() { return 0; }
