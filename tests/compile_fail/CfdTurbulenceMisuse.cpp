// P19-TURB-ARCH-001 — the turbulence types that must NOT exist. ADR-079.
//
// THE GUARDS ARE `BETTERCAD_CF_<CASE>`, NOT `<CASE>`. `P19-PVCOUPLE-001` wrote
// them the short way, every translation unit came out EMPTY, every build
// succeeded, and all thirty-seven gates reported "exit code 0, expected 1".
// The convention is kept here.
//
// WHY THIS GROUP IS THE MILESTONE'S CENTRAL PROOF. `ADR-079` decides that the
// turbulence architecture is recorded WITHOUT TYPES: no mode field, no model
// interface, no turbulence variable, no wall-treatment record, no turbulence
// result array, no flow-regime query. Every one of those is a claim about
// ABSENCE, and `ADR-058` prescribed exactly this mechanism for it --
//
//     "The refusal is by ABSENCE where it can be (there is no field, no
//      enumerator and no function for radiation or for a viscosity law) ...
//      each unsupported item stays absent from the types, which the
//      compile-fail groups of those milestones assert."
//
// -- so a build that still succeeds is the only way these claims can fail.
//
// AND THE GROUP IS A TRIPWIRE FOR THE DAY A MODEL IS AUTHORIZED. When
// `P19-KEPS-001` adds `k`, `epsilon` and a turbulent viscosity, these gates stop
// compile-failing and the build goes red. That is the intended outcome: the
// milestone that adds turbulence has to come here and delete the gate that said
// there was none, which makes authorizing a model a visible act rather than an
// accumulation of plausible fields. `architecture.turbulence-ownership` is its
// sibling and guards WHERE turbulence may live; this group guards WHETHER the
// types exist at all.
//
// THE REGEXES LIST EVERY DIAGNOSTIC GCC CAN EMIT FOR THE CASE, not the one a
// reader expects -- the P19-GRAD-001 lesson, which cost six of twenty-eight
// gates on P19-LINSOLVE-001's first run and one of twenty-eight on
// P19-STEADY-001's.

// EVERY PUBLIC cfd HEADER, AND THAT IS NOT TIDINESS. A mutation analysis found the
// first version of this file included five headers whose transitive closure was 11
// of the 23 -- and the 12 it missed included `CfdMomentum.hpp`, the exact seam
// ADR-079 decision 2 names ("composed into momentum the way ADR-073 composes
// convection and diffusion"). A qualified-name lookup only sees declarations
// visible in the translation unit, so `assembleCfdTurbulentViscosity` declared in
// `CfdMomentum.hpp` -- the banned spelling, in the named place -- left six of these
// gates GREEN. A gate that cannot see the file it is about is not a gate.
#include <bettercad/cfd/CfdAnalysis.hpp>
#include <bettercad/cfd/CfdAnalysisObject.hpp>
#include <bettercad/cfd/CfdBoundary.hpp>
#include <bettercad/cfd/CfdContinuity.hpp>
#include <bettercad/cfd/CfdConvection.hpp>
#include <bettercad/cfd/CfdCoupling.hpp>
#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdDiffusion.hpp>
#include <bettercad/cfd/CfdDivergence.hpp>
#include <bettercad/cfd/CfdFields.hpp>
#include <bettercad/cfd/CfdFluid.hpp>
#include <bettercad/cfd/CfdGeometry.hpp>
#include <bettercad/cfd/CfdGradient.hpp>
#include <bettercad/cfd/CfdInitialField.hpp>
#include <bettercad/cfd/CfdLinearSolve.hpp>
#include <bettercad/cfd/CfdMesh.hpp>
#include <bettercad/cfd/CfdMomentum.hpp>
#include <bettercad/cfd/CfdResult.hpp>
#include <bettercad/cfd/CfdSparse.hpp>
#include <bettercad/cfd/CfdSteady.hpp>
#include <bettercad/cfd/CfdTarget.hpp>
#include <bettercad/cfd/CfdTopology.hpp>
#include <bettercad/cfd/CfdTransient.hpp>

namespace cfd = bettercad::cfd;

// ---------------------------------------------------------------------------
// Decision 1: there is no mode, as a TYPE or as a FIELD
// ---------------------------------------------------------------------------
//
// ADR-061 Decision 4: "a selector would be a field with one legal value". While
// only laminar is qualified there is nothing to select between, so the mode is
// not in the definition and the type does not exist. P19-KEPS-001's own first
// checklist item is "Define model variant", and it adds both.

#if defined(BETTERCAD_CF_THERE_IS_NO_TURBULENCE_MODE_TYPE)
[[maybe_unused]] static void misuse() {
    const cfd::CfdTurbulenceMode mode{};
    (void)mode;
}
#endif

#if defined(BETTERCAD_CF_THE_DEFINITION_HAS_NO_TURBULENCE_MODE)
[[maybe_unused]] static auto misuse(const cfd::CfdAnalysisDefinition& definition) {
    return definition.turbulenceMode;
}
#endif

#if defined(BETTERCAD_CF_THE_DEFINITION_HAS_NO_TURBULENCE_SETTINGS)
[[maybe_unused]] static auto misuse(const cfd::CfdAnalysisDefinition& definition) {
    return definition.turbulence;
}
#endif

// ---------------------------------------------------------------------------
// Decision 2: there is no model interface, and no model
// ---------------------------------------------------------------------------
//
// The seam a model must take is decided in ADR-079 by SHAPE -- a free function
// over plain records, composed into momentum the way ADR-073 composes convection
// and diffusion -- and declared nowhere, because a declared interface with no
// implementation is the "derived state with no consumer" ADR-067 rejected. No
// `virtual` enters cfd.

#if defined(BETTERCAD_CF_THERE_IS_NO_TURBULENCE_MODEL_TYPE)
[[maybe_unused]] static void misuse() {
    const cfd::CfdTurbulenceModel model{};
    (void)model;
}
#endif

#if defined(BETTERCAD_CF_THERE_IS_NO_TURBULENT_VISCOSITY_ASSEMBLY)
[[maybe_unused]] static auto misuse() {
    return cfd::assembleCfdTurbulentViscosity();
}
#endif

// ---------------------------------------------------------------------------
// Decision 3: the frozen variable table has no turbulence row
// ---------------------------------------------------------------------------
//
// CfdFields.hpp's own comment names turbulence as precisely the change it
// forbids in v1. The count pin in CfdTurbulenceArchitectureTests.cpp holds the
// table at three; these hold the enumerators that a model would add.

#if defined(BETTERCAD_CF_THERE_IS_NO_TURBULENT_ENERGY_VARIABLE)
[[maybe_unused]] static auto misuse() {
    return cfd::CfdVariable::TurbulentKineticEnergy;
}
#endif

#if defined(BETTERCAD_CF_THERE_IS_NO_TURBULENT_VISCOSITY_VARIABLE)
[[maybe_unused]] static auto misuse() {
    return cfd::CfdVariable::TurbulentViscosity;
}
#endif

// ---------------------------------------------------------------------------
// Decisions 4 and 5: no wall treatment, and no turbulence boundary condition
// ---------------------------------------------------------------------------
//
// ADR-067 rejected a generic per-face Robin form "though a later one may want
// it", because the coefficients ARE a discretization. A wall-treatment record
// is that rejected design under another name, and a wall function is a function
// of the near-wall cell that arrives with the model that needs it.

#if defined(BETTERCAD_CF_THERE_IS_NO_WALL_TREATMENT_TYPE)
[[maybe_unused]] static void misuse() {
    const cfd::CfdWallTreatment treatment{};
    (void)treatment;
}
#endif

#if defined(BETTERCAD_CF_A_TREATMENT_HAS_NO_TURBULENCE_POLICY)
[[maybe_unused]] static auto misuse(const cfd::CfdBoundaryTreatment& treatment) {
    return treatment.turbulence;
}
#endif

#if defined(BETTERCAD_CF_THERE_IS_NO_TURBULENCE_BOUNDARY_KIND)
[[maybe_unused]] static auto misuse() {
    return cfd::CfdBoundaryConditionKind::TurbulenceInlet;
}
#endif

// ---------------------------------------------------------------------------
// Decision 6: a result publishes p, U and phi, and nothing turbulent
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_RESULT_HAS_NO_TURBULENT_VISCOSITY)
[[maybe_unused]] static auto misuse(const cfd::CfdResultFields& fields) {
    return fields.turbulentViscosity();
}
#endif

#if defined(BETTERCAD_CF_RESULT_METADATA_HAS_NO_TURBULENCE_MEASURE)
[[maybe_unused]] static auto misuse(const cfd::CfdResultMetadata& metadata) {
    return metadata.turbulence;
}
#endif

// ---------------------------------------------------------------------------
// Decision 10: no flow-regime query, because its THRESHOLD is not this
// milestone's to fix
// ---------------------------------------------------------------------------
//
// ADR-058 assigns the Reynolds limit to P19-VALID-001, "from a reference". The
// derived-regime shape is recorded in ADR-079 as the one that milestone should
// adopt, and is absent here: a regime function whose only honest answer is "I
// may not say" is derived state with no consumer. This gate is what stops it
// arriving before its number.

#if defined(BETTERCAD_CF_THERE_IS_NO_FLOW_REGIME_QUERY)
[[maybe_unused]] static auto misuse(const cfd::CfdAnalysisDefinition& definition) {
    return cfd::cfdFlowRegime(definition);
}
#endif

#if defined(BETTERCAD_CF_THERE_IS_NO_LAMINAR_REYNOLDS_LIMIT)
[[maybe_unused]] static auto misuse() {
    return cfd::kCfdLaminarReynoldsLimit;
}
#endif
