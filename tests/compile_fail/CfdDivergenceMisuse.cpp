/// \file
/// P19-DIV-001's compile-fail group: what the type system must make impossible,
/// ADR-071.
///
/// THE CLAIM THIS GROUP CARRIES. Four things, and the last two cannot be reached
/// by any value test:
///
/// 1. A `CfdDivergenceField` cannot be CONJURED. Possession is the evidence
///    (ADR-036): the only door is a divergence, which runs the whole input gate,
///    so a field that exists has been through it. The default constructor is
///    private and only `CfdDivergenceBuilder` is a friend.
/// 2. A MASS-FLOW FIELD AND A VOLUMETRIC-FLOW FIELD CANNOT BE SWAPPED. ADR-071
///    Decision 4 chose one template with two instantiations precisely so the
///    compiler separates them. `kg/s` and `m^3/s` are different physics and a
///    runtime tag would turn this class of error into something a caller can
///    ignore.
/// 3. THE INTEGRAL AND THE DIVERGENCE ARE DIFFERENT TYPES. ADR-071 Decision 1
///    stores `sum_f F_f . S_f` and divides on read, so `Integral` is `kg/s` and
///    `Divergence` is `kg/(m^3 s)`. Confusing them is a factor of a volume, and
///    it is exactly the error that would make a conservation claim quietly
///    wrong, so the compiler refuses it.
/// 4. THE MASS-FLUX ENTRY POINT TAKES NO BOUNDARY. ADR-071 Decision 5 made that
///    asymmetry load-bearing: `phi` is a face variable the caller owns, and its
///    `Computed` policy at a pressure boundary must NOT be a refusal. Handing
///    that entry point a `PreparedCfdBoundary` is a misunderstanding of the
///    decision, and no value test can show it is impossible -- only a compile
///    error can.
///
/// EVERY HELPER IS `[[maybe_unused]] static`, AND BOTH WORDS EARN THEIR PLACE.
/// `static` gives it internal linkage, so `-Wmissing-declarations` cannot fire
/// -- which matters because a probe that can exit non-zero for a reason
/// UNRELATED to its claim proves nothing, the hazard P18-ELEM-001 recorded as
/// its finding F3 and P19-GRAD-001 hit on all 24 of its own cases.
/// `[[maybe_unused]]` is then needed because each is called only from inside a
/// `#if defined(BETTERCAD_CF_...)` block, so in the CONTROL configuration -- no
/// macro, the build every preset runs -- they are unused, and this translation
/// unit is compiled with `-Werror`. P19-GEOM-001 recorded what a failing control
/// costs: it broke `cmake --build` for the whole project.

#include <bettercad/cfd/CfdBoundary.hpp>
#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdDivergence.hpp>
#include <bettercad/cfd/CfdGeometry.hpp>
#include <bettercad/cfd/CfdTopology.hpp>
#include <bettercad/core/units/Units.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

using namespace bettercad;
using namespace bettercad::cfd;

// ---------------------------------------------------------------------------
// A field cannot be conjured
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_DEFAULT_CONSTRUCT_A_VELOCITY_DIVERGENCE_FIELD)
[[maybe_unused]] static CfdVelocityDivergenceField conjuredVelocity() {
    // The default constructor is private: a divergence is the only door, so a
    // field that was never gated does not exist.
    return CfdVelocityDivergenceField{};
}
#endif

#if defined(BETTERCAD_CF_DEFAULT_CONSTRUCT_A_MASS_FLUX_DIVERGENCE_FIELD)
[[maybe_unused]] static CfdMassFluxDivergenceField conjuredMass() {
    return CfdMassFluxDivergenceField{};
}
#endif

#if defined(BETTERCAD_CF_A_FIELD_EXPOSES_ITS_SI_ARRAY)
[[maybe_unused]] static std::size_t readTheRawArray(const CfdMassFluxDivergenceField& field) {
    // The stored SI array is private. The units live on the accessor, so there
    // is no door to the bare doubles.
    return field.si_.size();
}
#endif

#if defined(BETTERCAD_CF_A_FIELD_EXPOSES_ITS_VOLUME_ARRAY)
[[maybe_unused]] static std::size_t readTheVolumes(const CfdMassFluxDivergenceField& field) {
    // The per-cell volumes are private too: `divergenceAt` is the only way to
    // apply them, which is what stops a caller dividing by a DIFFERENT geometry
    // than the divergence was taken against.
    return field.volumeSi_.size();
}
#endif

#if defined(BETTERCAD_CF_A_FIELD_SUMMARY_IS_WRITABLE)
[[maybe_unused]] static void editTheSummary(const CfdMassFluxDivergenceField& field) {
    // `summary()` returns a const reference: a measurement cannot be edited
    // after the fact.
    field.summary().relativeImbalance = 0.0;
}
#endif

// ---------------------------------------------------------------------------
// The two fields cannot be swapped
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_VELOCITY_DIVERGENCE_FIELD_IS_A_MASS_FLUX_FIELD)
[[maybe_unused]] static void swapFieldsOneWay(const CfdVelocityDivergenceField& volumetric) {
    const CfdMassFluxDivergenceField& wrong = volumetric;
    (void)wrong;
}
#endif

#if defined(BETTERCAD_CF_A_MASS_FLUX_FIELD_IS_A_VELOCITY_DIVERGENCE_FIELD)
[[maybe_unused]] static void swapFieldsOtherWay(const CfdMassFluxDivergenceField& mass) {
    const CfdVelocityDivergenceField& wrong = mass;
    (void)wrong;
}
#endif

// ---------------------------------------------------------------------------
// The quantities cannot be swapped, and are not bare doubles
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_MASS_FLOW_RATE_IS_A_VOLUMETRIC_FLOW_RATE)
[[maybe_unused]] static VolumetricFlowRate massAsVolumetric(MassFlowRate rate) {
    return rate;
}
#endif

#if defined(BETTERCAD_CF_A_VOLUMETRIC_FLOW_RATE_IS_A_MASS_FLOW_RATE)
[[maybe_unused]] static MassFlowRate volumetricAsMass(VolumetricFlowRate rate) {
    return rate;
}
#endif

#if defined(BETTERCAD_CF_A_MASS_FLOW_RATE_IS_A_MASS)
[[maybe_unused]] static Mass rateAsMass(MassFlowRate rate) {
    // kg/s is not kg. A missing `/ time` is a plausible slip and the compiler
    // catches it.
    return rate;
}
#endif

#if defined(BETTERCAD_CF_A_VOLUMETRIC_FLOW_RATE_IS_A_VOLUME)
[[maybe_unused]] static Volume rateAsVolume(VolumetricFlowRate rate) {
    return rate;
}
#endif

#if defined(BETTERCAD_CF_A_MASS_FLOW_RATE_IS_A_BARE_DOUBLE)
[[maybe_unused]] static double rateAsDouble(MassFlowRate rate) {
    // `si()` is the only door out, so a flux cannot be smuggled into unitless
    // arithmetic by assignment.
    return rate;
}
#endif

#if defined(BETTERCAD_CF_A_FLUX_SUM_MIXES_DIMENSIONS)
[[maybe_unused]] static MassFlowRate addUnlikeFluxes(MassFlowRate mass, VolumetricFlowRate volumetric) {
    return mass + volumetric;
}
#endif

// ---------------------------------------------------------------------------
// The integral and the divergence are different types
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_DIVERGENCE_IS_ITS_OWN_INTEGRAL)
[[maybe_unused]] static MassFlowRate divergenceAsIntegral(
    CfdMassFluxDivergenceField::Divergence divergence) {
    // THE FACTOR-OF-A-VOLUME ERROR, refused. ADR-071 Decision 1 stores the
    // integral and divides on read; confusing the two is what would make a
    // conservation claim quietly wrong.
    return divergence;
}
#endif

#if defined(BETTERCAD_CF_AN_INTEGRAL_IS_ITS_OWN_DIVERGENCE)
[[maybe_unused]] static CfdMassFluxDivergenceField::Divergence integralAsDivergence(
    MassFlowRate integral) {
    return integral;
}
#endif

#if defined(BETTERCAD_CF_A_VELOCITY_DIVERGENCE_IS_A_MASS_DIVERGENCE)
[[maybe_unused]] static CfdMassFluxDivergenceField::Divergence swapDivergences(
    CfdVelocityDivergenceField::Divergence velocity) {
    // `1/s` is not `kg/(m^3 s)`.
    return velocity;
}
#endif

#if defined(BETTERCAD_CF_A_MASS_DIVERGENCE_IS_A_DENSITY)
[[maybe_unused]] static Density divergenceAsDensity(
    CfdMassFluxDivergenceField::Divergence divergence) {
    // kg/(m^3 s) is not kg/m^3: a missing `/ time` again, one layer down.
    return divergence;
}
#endif

// ---------------------------------------------------------------------------
// The inputs cannot be swapped
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_MASS_FLUX_INPUT_IS_A_VELOCITY_INPUT)
[[maybe_unused]] static void swapInputsOneWay(const CfdMassFluxDivergenceInput& mass) {
    const CfdVelocityDivergenceInput& wrong = mass;
    (void)wrong;
}
#endif

#if defined(BETTERCAD_CF_A_VELOCITY_INPUT_IS_A_MASS_FLUX_INPUT)
[[maybe_unused]] static void swapInputsOtherWay(const CfdVelocityDivergenceInput& velocity) {
    const CfdMassFluxDivergenceInput& wrong = velocity;
    (void)wrong;
}
#endif

#if defined(BETTERCAD_CF_THE_MASS_ENTRY_POINT_TAKES_A_VELOCITY_INPUT)
[[maybe_unused]] static void massTakesVelocity(const CfdTopology& topology,
                                               const CfdGeometry& geometry,
                                               const CfdVelocityDivergenceInput& input) {
    (void)divergeCfdMassFlux(topology, geometry, input);
}
#endif

#if defined(BETTERCAD_CF_THE_VELOCITY_ENTRY_POINT_TAKES_A_MASS_INPUT)
[[maybe_unused]] static void velocityTakesMass(const CfdTopology& topology,
                                               const CfdGeometry& geometry,
                                               const PreparedCfdBoundary& boundary,
                                               const CfdMassFluxDivergenceInput& input) {
    (void)divergeCfdVelocity(topology, geometry, boundary, input);
}
#endif

#if defined(BETTERCAD_CF_THE_MASS_ENTRY_POINT_TAKES_NO_BOUNDARY)
[[maybe_unused]] static void massTakesABoundary(const CfdTopology& topology,
                                                const CfdGeometry& geometry,
                                                const PreparedCfdBoundary& boundary,
                                                const CfdMassFluxDivergenceInput& input) {
    // ADR-071 DECISION 5, ENFORCED BY THE COMPILER. `phi` is a face variable the
    // caller owns, and its `Computed` policy at a pressure boundary must not be
    // a refusal -- so this entry point has no boundary parameter at all, and
    // passing one is a misunderstanding of the decision rather than a typo.
    (void)divergeCfdMassFlux(topology, geometry, boundary, input);
}
#endif

#if defined(BETTERCAD_CF_THE_FACE_FLUX_TAKES_TYPED_RATES)
[[maybe_unused]] static void typedFaceFlux(std::span<const MassFlowRate> typed) {
    // ADR-068 settled that a bulk field array is a caller-owned span of SI
    // doubles, with the units on the accessor. A typed span is refused here on
    // purpose, so that decision cannot be quietly reversed.
    CfdMassFluxDivergenceInput input;
    input.faceFlux = typed;
}
#endif

#if defined(BETTERCAD_CF_THE_CURRENT_VELOCITY_TAKES_TYPED_VECTORS)
[[maybe_unused]] static void typedVelocity(std::span<const Velocity3> typed) {
    CfdVelocityDivergenceInput input;
    input.currentVelocity = typed;
}
#endif

// ---------------------------------------------------------------------------
// The summary holds counts and dimensionless measures, not fluxes
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_SUMMARY_HOLDS_THE_FLUXES)
[[maybe_unused]] static double summaryHoldsFlux(const CfdDivergenceSummary& summary) {
    // The conservation numbers are DIMENSIONED, so they are typed accessors on
    // the field and not doubles in a struct shared by two instantiations. Asking
    // the summary for one is asking for a flux without its unit.
    return summary.netBoundaryFlux;
}
#endif

#if defined(BETTERCAD_CF_AN_INPUT_HOLDS_A_FIELD)
[[maybe_unused]] static std::size_t inputHoldsField(const CfdMassFluxDivergenceInput& input) {
    return input.cells();
}
#endif

// ---------------------------------------------------------------------------
// The enums are their own types
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_DIVERGENCE_PROBLEM_IS_AN_INTEGER)
[[maybe_unused]] static CfdDivergenceProblem problemFromInteger() {
    return 3;
}
#endif

#if defined(BETTERCAD_CF_A_DIVERGENCE_PROBLEM_IS_A_BOUNDARY_POLICY)
[[maybe_unused]] static CfdDivergenceProblem problemFromPolicy(CfdBoundaryPolicy policy) {
    return policy;
}
#endif

// The CONTROL configuration defines no macro at all, so this translation unit
// must compile clean -- which is what makes every case above a statement about
// its own claim rather than about this file.
