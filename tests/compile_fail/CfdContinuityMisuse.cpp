/// \file
/// P19-CONT-001's compile-fail group: what the type system must make
/// impossible, ADR-074.
///
/// THE CLAIMS THIS GROUP CARRIES. Six, matching the six sections of the body.
///
/// 1. A `CfdContinuityReport` cannot be DEFAULT-CONSTRUCTED, and cannot be
///    constructed from a divergence field either. Its only constructor is
///    private and takes the field by value, so a report that exists holds a
///    divergence that exists -- and `CfdDivergenceField`'s own default
///    constructor is private, so the invariant "a field that exists has been
///    through the gate" propagates here BY CONSTRUCTION rather than by comment.
///
///    THAT WAS THE COMPILER'S CORRECTION, NOT A DESIGN CHOICE MADE IN ADVANCE.
///    The first draft held the field as a default-constructed member and
///    assigned to it, which did not compile. Recorded because the resulting
///    design is stronger than the one intended.
/// 2. THE TWO `globalImbalance` SPELLINGS CANNOT BE ASSIGNED TO EACH OTHER
///    DIRECTLY -- and the guard is PARTIAL, which is the honest claim.
///
///    `CfdDivergenceField::globalImbalance()` returns a `MassFlowRate` and
///    `CfdMassBalance::globalImbalance` is a `double`, so the naive swap does
///    not compile. But `.si()` on the first produces a `double`, and
///    `balance.globalImbalance = field.globalImbalance().si()` COMPILES AND IS
///    CATASTROPHIC: it reports a floating-point artifact of order `1e-16` as
///    the physical mass imbalance, so every mass balance would pass however
///    badly conservation failed.
///
///    NO COMPILE-FAIL CASE CAN CLOSE THAT, because both sides are a `double`
///    once `.si()` is written. The defences are the different NAME
///    (`accumulationResidual`, so the two never look interchangeable), a test
///    asserting the two differ by ten orders of magnitude on a field whose
///    balance genuinely fails, and a MUTATION PROBE that applies the swap and
///    must kill the suite. This group carries the half the type system can
///    carry and says so rather than implying the whole.
/// 3. THE SUMMARY AND THE MASS BALANCE ARE DIFFERENT TYPES carrying different
///    members. `CfdContinuitySummary` holds `accumulationResidual` and no
///    relative measure; `CfdMassBalance` holds the two relative measures and no
///    artifact. Reaching for the wrong member is a build failure, which is what
///    keeps "the measures ADR-058 fixed" from drifting into "whatever this
///    struct happens to hold".
/// 4. THE DIMENSIONED ANSWERS ARE TYPED. `inflow()`, `outflow()`,
///    `netBoundaryFlux()`, `throughput()` and every `CfdPatchMassFlow` total are
///    `MassFlowRate`, not `double`, so a caller cannot mix a mass flow rate with
///    a volumetric one or with a bare number. ARCHITECTURE.md's rule against a
///    unitless engineering API, enforced rather than stated.
/// 5. THE EVALUATION'S FAILURE IS TAGGED, NOT A PLAIN `Error`. ADR-074 Decision
///    10, and the reason is `CfdDivergence.hpp`'s own recorded limitation: its
///    entry points return an untagged `Result`, so its `DivergenceIsNotFinite`
///    is observable only through the diagnostic's wording. A message is not a
///    tag.
/// 6. THE TYPED IDS DO NOT INTERCONVERT. `PatchIndex`, `CellIndex`, `FaceIndex`
///    and `BoundaryFaceIndex` are distinct `Id` instantiations, which matters
///    here more than usual because this evaluator subscripts a per-patch array
///    by `patchOf(...).value()` -- and P19-DIV-001's review, then
///    P19-CONV-001's review, each found an unvalidated index reaching WRITES in
///    a sibling operator.
///
/// WHAT THIS GROUP DOES NOT CLAIM.
///
/// It does not claim the mass balance cannot be fabricated. `CfdMassBalance` is
/// an aggregate of two public doubles declared in `CfdData.hpp`, so anyone can
/// build one with any values. The narrow claim -- that the one this evaluator
/// RETURNS was computed from a gated divergence and a positive throughput -- is
/// the true one, and it is carried by the report's private constructor rather
/// than by the balance's type.
///
/// It does not claim `phi` cannot be the wrong array. `CfdContinuityInput` holds
/// a `std::span<const double>` of one value per face, and so does
/// `CfdMassFluxDivergenceInput`; a caller handing over a per-CELL array of the
/// same length as the face count would compile. The length check is a runtime
/// phase for that reason, and the two INPUT TYPES are still distinct so the
/// structs themselves cannot be swapped.

#include <bettercad/cfd/CfdBoundary.hpp>
#include <bettercad/cfd/CfdContinuity.hpp>
#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdDivergence.hpp>
#include <bettercad/cfd/CfdGeometry.hpp>
#include <bettercad/cfd/CfdTopology.hpp>
#include <bettercad/core/Error.hpp>

#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <vector>

using namespace bettercad;
using namespace bettercad::cfd;

// ---------------------------------------------------------------------------
// 1 -- a report cannot be conjured
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_DEFAULT_CONSTRUCT_A_CONTINUITY_REPORT)
[[maybe_unused]] static CfdContinuityReport conjureAReport() { return CfdContinuityReport{}; }
#endif

#if defined(BETTERCAD_CF_CONSTRUCT_A_REPORT_FROM_A_DIVERGENCE)
[[maybe_unused]] static CfdContinuityReport
reportFromAField(const CfdMassFluxDivergenceField& field) {
    return CfdContinuityReport{field};
}
#endif

#if defined(BETTERCAD_CF_DEFAULT_CONSTRUCT_A_DIVERGENCE_FIELD)
// THE INVARIANT THIS REPORT INHERITS, pinned at its source: the sub-operator's
// own field cannot be conjured either, which is why a report holding one by
// value cannot be default-constructed.
[[maybe_unused]] static CfdMassFluxDivergenceField conjureAField() {
    return CfdMassFluxDivergenceField{};
}
#endif

// ---------------------------------------------------------------------------
// 2 -- the two `globalImbalance` spellings do not interconvert
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_THE_MASS_BALANCE_TAKES_A_DIVERGENCE_IMBALANCE)
// THE HALF THE TYPE SYSTEM CAN CARRY. `globalImbalance()` is a `MassFlowRate`
// and the member is a `double`, so the naive swap is a build failure. The
// `.si()` form is not, and the file header says so.
[[maybe_unused]] static void massBalanceTakesTheArtifact(CfdMassBalance& balance,
                                                         const CfdMassFluxDivergenceField& field) {
    balance.globalImbalance = field.globalImbalance();
}
#endif

#if defined(BETTERCAD_CF_THE_MASS_BALANCE_TAKES_A_NET_BOUNDARY_FLUX)
[[maybe_unused]] static void massBalanceTakesAFlux(CfdMassBalance& balance,
                                                   const CfdMassFluxDivergenceField& field) {
    balance.maxCellContinuity = field.netBoundaryFlux();
}
#endif

// ---------------------------------------------------------------------------
// 3 -- the summary and the mass balance are different types
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_SUMMARY_HOLDS_A_GLOBAL_IMBALANCE)
// THE NAME COLLISION'S OTHER HALF: the summary deliberately does NOT carry a
// member of that name, so code reaching for one on it does not build.
[[maybe_unused]] static double summaryGlobalImbalance(const CfdContinuitySummary& summary) {
    return summary.globalImbalance;
}
#endif

#if defined(BETTERCAD_CF_A_SUMMARY_HOLDS_A_MAX_CELL_CONTINUITY)
[[maybe_unused]] static double summaryMaxCellContinuity(const CfdContinuitySummary& summary) {
    return summary.maxCellContinuity;
}
#endif

#if defined(BETTERCAD_CF_A_MASS_BALANCE_HOLDS_AN_ACCUMULATION_RESIDUAL)
[[maybe_unused]] static double balanceAccumulationResidual(const CfdMassBalance& balance) {
    return balance.accumulationResidual;
}
#endif

#if defined(BETTERCAD_CF_A_MASS_BALANCE_HOLDS_A_CLOSED_DOMAIN)
[[maybe_unused]] static bool balanceClosedDomain(const CfdMassBalance& balance) {
    return balance.closedDomain;
}
#endif

#if defined(BETTERCAD_CF_A_PATCH_HOLDS_A_THROUGHPUT)
[[maybe_unused]] static MassFlowRate patchThroughput(const CfdPatchMassFlow& patch) {
    return patch.throughput;
}
#endif

#if defined(BETTERCAD_CF_A_MASS_BALANCE_IS_A_SUMMARY)
[[maybe_unused]] static CfdContinuitySummary balanceAsSummary(const CfdMassBalance& balance) {
    return balance;
}
#endif

#if defined(BETTERCAD_CF_A_SUMMARY_IS_A_MASS_BALANCE)
[[maybe_unused]] static CfdMassBalance summaryAsBalance(const CfdContinuitySummary& summary) {
    return summary;
}
#endif

// ---------------------------------------------------------------------------
// 4 -- the dimensioned answers are typed
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_THE_INFLOW_IS_A_BARE_DOUBLE)
[[maybe_unused]] static double inflowAsDouble(const CfdContinuityReport& report) {
    return report.inflow();
}
#endif

#if defined(BETTERCAD_CF_THE_OUTFLOW_IS_A_BARE_DOUBLE)
[[maybe_unused]] static double outflowAsDouble(const CfdContinuityReport& report) {
    return report.outflow();
}
#endif

#if defined(BETTERCAD_CF_THE_THROUGHPUT_IS_A_BARE_DOUBLE)
[[maybe_unused]] static double throughputAsDouble(const CfdContinuityReport& report) {
    return report.throughput();
}
#endif

#if defined(BETTERCAD_CF_THE_NET_BOUNDARY_FLUX_IS_A_BARE_DOUBLE)
[[maybe_unused]] static double netAsDouble(const CfdContinuityReport& report) {
    return report.netBoundaryFlux();
}
#endif

#if defined(BETTERCAD_CF_A_PATCH_INFLOW_IS_A_BARE_DOUBLE)
[[maybe_unused]] static double patchInflowAsDouble(const CfdPatchMassFlow& patch) {
    return patch.inflow;
}
#endif

#if defined(BETTERCAD_CF_A_MASS_FLOW_RATE_IS_A_VOLUMETRIC_FLOW_RATE)
// `phi` IS A MASS FLOW RATE AND NOT A VOLUMETRIC ONE, which matters because
// `CfdDivergenceField` is instantiated for both and only the quantity type
// separates a mass-flux divergence from a velocity divergence.
[[maybe_unused]] static VolumetricFlowRate inflowAsVolumetric(const CfdContinuityReport& report) {
    return report.inflow();
}
#endif

#if defined(BETTERCAD_CF_THE_THROUGHPUT_TAKES_A_BARE_DOUBLE)
[[maybe_unused]] static MassFlowRate throughputFromDouble() { return 1.0; }
#endif

// ---------------------------------------------------------------------------
// 5 -- the failure is tagged
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_THE_EVALUATION_RETURNS_A_PLAIN_RESULT)
[[maybe_unused]] static Result<CfdContinuityReport>
evaluationAsPlainResult(const CfdTopology& topology, const CfdGeometry& geometry,
                        const PreparedCfdBoundary& boundary, const CfdContinuityInput& input) {
    return evaluateCfdContinuity(topology, geometry, boundary, input);
}
#endif

#if defined(BETTERCAD_CF_A_FAILURE_IS_AN_ERROR)
[[maybe_unused]] static Error failureAsError(const CfdContinuityFailure& failure) {
    return failure;
}
#endif

#if defined(BETTERCAD_CF_A_FAILURE_IS_ITS_PROBLEM)
[[maybe_unused]] static CfdContinuityProblem
failureAsProblem(const CfdContinuityFailure& failure) {
    return failure;
}
#endif

#if defined(BETTERCAD_CF_THE_MASS_BALANCE_IS_NOT_OPTIONAL)
// THE ABSENT CASE CANNOT BE FORGOTTEN. A closed domain has no throughput, so the
// two relative measures are undefined and `massBalance()` returns an optional --
// ADR-074 Decision 9. A caller that treated it as a bare value would silently
// read a default-constructed balance of two zeros and report perfect
// conservation on a mesh where the measure does not exist.
[[maybe_unused]] static CfdMassBalance balanceWithoutTheOptional(
    const CfdContinuityReport& report) {
    return report.massBalance();
}
#endif

#if defined(BETTERCAD_CF_THE_CELL_RESIDUAL_IS_NOT_OPTIONAL)
[[maybe_unused]] static MassFlowRate residualWithoutTheOptional(
    const CfdContinuityReport& report, CellIndex cell) {
    return report.cellResidualAt(cell);
}
#endif

#if defined(BETTERCAD_CF_THE_PATCH_KIND_IS_NOT_OPTIONAL)
// AN EMPTY OPTIONAL MEANS MIXED OR EMPTY, which a bare kind cannot express: it
// would have to pick one of the six, and every choice would be a condition the
// patch does not uniformly carry.
[[maybe_unused]] static CfdBoundaryConditionKind kindWithoutTheOptional(
    const CfdPatchMassFlow& patch) {
    return patch.kind;
}
#endif

#if defined(BETTERCAD_CF_A_CONTINUITY_PROBLEM_IS_AN_INTEGER)
[[maybe_unused]] static int problemAsInt(CfdContinuityProblem problem) { return problem; }
#endif

#if defined(BETTERCAD_CF_A_CONTINUITY_PROBLEM_IS_A_DIVERGENCE_PROBLEM)
[[maybe_unused]] static CfdDivergenceProblem
continuityProblemAsDivergence(CfdContinuityProblem problem) {
    return problem;
}
#endif

#if defined(BETTERCAD_CF_A_DIVERGENCE_PROBLEM_IS_A_CONTINUITY_PROBLEM)
[[maybe_unused]] static CfdContinuityProblem
divergenceProblemAsContinuity(CfdDivergenceProblem problem) {
    return problem;
}
#endif

#if defined(BETTERCAD_CF_A_CONTINUITY_INPUT_IS_A_DIVERGENCE_INPUT)
[[maybe_unused]] static CfdMassFluxDivergenceInput
continuityInputAsDivergence(const CfdContinuityInput& input) {
    return input;
}
#endif

#if defined(BETTERCAD_CF_A_DIVERGENCE_INPUT_IS_A_CONTINUITY_INPUT)
[[maybe_unused]] static CfdContinuityInput
divergenceInputAsContinuity(const CfdMassFluxDivergenceInput& input) {
    return input;
}
#endif

#if defined(BETTERCAD_CF_A_REPORT_IS_A_DIVERGENCE_FIELD)
[[maybe_unused]] static CfdMassFluxDivergenceField
reportAsField(const CfdContinuityReport& report) {
    return report;
}
#endif

// ---------------------------------------------------------------------------
// 6 -- the typed IDs do not interconvert
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_PATCH_INDEX_IS_A_CELL_INDEX)
[[maybe_unused]] static CellIndex patchAsCell(PatchIndex patch) { return patch; }
#endif

#if defined(BETTERCAD_CF_A_PATCH_INDEX_IS_A_FACE_INDEX)
[[maybe_unused]] static FaceIndex patchAsFace(PatchIndex patch) { return patch; }
#endif

#if defined(BETTERCAD_CF_A_PATCH_INDEX_IS_A_BOUNDARY_FACE_INDEX)
[[maybe_unused]] static BoundaryFaceIndex patchAsBoundaryFace(PatchIndex patch) { return patch; }
#endif

#if defined(BETTERCAD_CF_A_CELL_INDEX_IS_A_PATCH_INDEX)
[[maybe_unused]] static PatchIndex cellAsPatch(CellIndex cell) { return cell; }
#endif

#if defined(BETTERCAD_CF_A_PATCH_INDEX_IS_A_BARE_SIZE)
[[maybe_unused]] static std::size_t patchAsSize(PatchIndex patch) { return patch; }
#endif

#if defined(BETTERCAD_CF_THE_CELL_RESIDUAL_TAKES_A_PATCH_INDEX)
// THE ONE THAT MATTERS MOST HERE, because this evaluator subscripts a per-patch
// array by `patchOf(...).value()` and walks cells separately: handing a patch
// index where a cell index belongs would read the wrong array entirely.
[[maybe_unused]] static std::optional<MassFlowRate>
residualAtAPatch(const CfdContinuityReport& report, PatchIndex patch) {
    return report.cellResidualAt(patch);
}
#endif
