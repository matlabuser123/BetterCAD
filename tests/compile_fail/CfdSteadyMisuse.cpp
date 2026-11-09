// P19-STEADY-001 — misuse of the steady solve and of the result's new fields
// that must not compile. ADR-077.
//
// THE GUARDS ARE `BETTERCAD_CF_<CASE>`, NOT `<CASE>`. `P19-PVCOUPLE-001` wrote
// them the short way, every translation unit came out EMPTY, every build
// succeeded, and all thirty-seven gates reported "exit code 0, expected 1" --
// caught only because the gates expect FAILURE. Recorded as that milestone's
// `r22`, and the convention is kept here.
//
// WHAT THIS GROUP IS FOR. `ADR-077` makes two changes a rename or a
// copy-paste could silently undo, and both are numerically or
// architecturally meaningful rather than cosmetic:
//
//   1. `CfdResult` now carries its fields, and `CfdResultFields` has NO default
//      constructor. "Possession is the evidence" (ADR-036) is only true while
//      the compiler refuses the alternatives, so a default-constructed field set
//      or result -- which would be a solution of nothing, claiming to be one --
//      is made a compile error rather than a convention.
//
//   2. The settings come from the DEFINITION and not from a parameter
//      (Decision 7), because `canonicalText` writes them into the fingerprint
//      that `CfdResultSource::definition` carries. A `solveCfdSteady` overload
//      taking settings would publish provenance it does not have, and no test of
//      the numerics would catch it -- so the only defence is that such a call
//      cannot be written.

#include <bettercad/cfd/CfdCoupling.hpp>
#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdFields.hpp>
#include <bettercad/cfd/CfdResult.hpp>
#include <bettercad/cfd/CfdSteady.hpp>

#include <span>
#include <vector>

namespace bc = bettercad;
namespace cfd = bettercad::cfd;

// ---------------------------------------------------------------------------
// Possession is the evidence: neither container may be conjured
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_FIELD_SET_CAN_BE_DEFAULT_CONSTRUCTED)
[[maybe_unused]] static void misuse() {
    cfd::CfdResultFields fields;
    (void)fields;
}
#endif

#if defined(BETTERCAD_CF_A_RESULT_CAN_BE_DEFAULT_CONSTRUCTED)
[[maybe_unused]] static void misuse() {
    cfd::CfdResult result;
    (void)result;
}
#endif

#if defined(BETTERCAD_CF_A_FIELD_SET_CAN_BE_BUILT_FROM_ARRAYS_ALONE)
// Without a layout there is nothing to check the three lengths against, which
// is the whole reason the factory takes one.
[[maybe_unused]] static void misuse(std::vector<double> p, std::vector<double> u, std::vector<double> phi) {
    (void)cfd::CfdResultFields::create(std::move(p), std::move(u), std::move(phi));
}
#endif

// ---------------------------------------------------------------------------
// The fields are READ-ONLY on a published result
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_PUBLISHED_FIELD_IS_WRITABLE)
// `pressure()` is a `span<const double>`: a viewer that could write through it
// would be editing a published solution in place.
[[maybe_unused]] static void misuse(const cfd::CfdResult& result) {
    result.fields().pressure()[0] = 1.0;
}
#endif

#if defined(BETTERCAD_CF_A_RESULTS_FIELDS_ARE_MUTABLE)
[[maybe_unused]] static void misuse(cfd::CfdResult& result) {
    cfd::CfdResultFields& fields = result.fields();
    (void)fields;
}
#endif

// ---------------------------------------------------------------------------
// The old three-argument create is gone, and so is the settings parameter
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_CREATE_STILL_TAKES_ONLY_SOURCE_MESH_AND_METADATA)
[[maybe_unused]] static void misuse(cfd::CfdResultSource source, const bc::meshing::VolumeMesh& mesh,
                                    cfd::CfdResultMetadata metadata) {
    (void)cfd::CfdResult::create(std::move(source), mesh, std::move(metadata));
}
#endif

#if defined(BETTERCAD_CF_THE_STEADY_SOLVE_TAKES_SETTINGS)
// ADR-077 Decision 7: the settings are the definition's, so there is no
// parameter for them and no type to pass.
[[maybe_unused]] static void misuse(const bc::Document& document, const bc::features::Regenerator& regenerator,
                                    const bc::meshing::Mesher& mesher, cfd::CfdAnalysisId analysis,
                                    const cfd::CfdLinearSolve& solve, cfd::CfdResultSlot& slot) {
    (void)cfd::solveCfdSteady(document, regenerator, mesher, analysis, cfd::CfdSteadySettings{}, solve, slot);
}
#endif

#if defined(BETTERCAD_CF_THERE_IS_A_STEADY_SOLVE_INPUT_TYPE)
[[maybe_unused]] static void misuse() {
    cfd::CfdSteadySolveInput input;
    (void)input;
}
#endif

#if defined(BETTERCAD_CF_THE_STEADY_SOLVE_PUBLISHES_WITHOUT_A_SLOT)
// Publication is not optional: "Atomic result publication" is the item, so a
// run that had nowhere to publish would not be one.
[[maybe_unused]] static void misuse(const bc::Document& document, const bc::features::Regenerator& regenerator,
                                    const bc::meshing::Mesher& mesher, cfd::CfdAnalysisId analysis,
                                    const cfd::CfdLinearSolve& solve) {
    (void)cfd::solveCfdSteady(document, regenerator, mesher, analysis, solve);
}
#endif

// ---------------------------------------------------------------------------
// The two vocabularies are separate: sixteen problems, nine stages
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_FAILURE_STAGE_IS_A_COUPLING_PROBLEM)
[[maybe_unused]] static void misuse() {
    (void)cfd::stageOf(cfd::CfdFailureStage::IllPosed);
}
#endif

#if defined(BETTERCAD_CF_A_COUPLING_PROBLEM_IS_A_FAILURE_STAGE)
[[maybe_unused]] static void misuse(cfd::CfdSolveFailure& failure) {
    failure.stage = cfd::CfdCouplingProblem::ValueNotFinite;
}
#endif

// ---------------------------------------------------------------------------
// The report is not the metadata, and does not carry the mass balance
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_THE_REPORT_CARRIES_THE_MASS_BALANCE)
// ADR-077 L7: the two measures live on the RESULT, and the report carries only
// whether they were defined. A reader who found both here would have two places
// to ask the same question.
[[maybe_unused]] static void misuse(const cfd::CfdSteadyReport& report) {
    (void)report.massBalance;
}
#endif

#if defined(BETTERCAD_CF_THE_REPORT_CARRIES_THE_FIELDS)
// The fields are published, not returned: a caller that read them off the
// report could hold a solution the slot never accepted.
[[maybe_unused]] static void misuse(const cfd::CfdSteadyReport& report) {
    (void)report.pressure;
}
#endif

#if defined(BETTERCAD_CF_A_STEADY_REPORT_IS_RESULT_METADATA)
[[maybe_unused]] static void misuse(const cfd::CfdSteadyReport& report) {
    const cfd::CfdResultMetadata metadata = report;
    (void)metadata;
}
#endif
