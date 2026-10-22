// Build-failure tests for the thermal validation and acceptance (see CMakeLists.txt in this
// directory). Built once without any BETTERCAD_CF_* macro as a control, which must compile -- so every
// failure below is attributable to its own line and not to a missing header.
//
// Five claims of P18-VALID-001 are enforced here rather than reviewed:
//
//   1. THE OUTCOME IS NOT A BOOLEAN. An overall status, a per-rule status, a severity and a code are
//      four distinct enumerations; none converts to another, to `bool` or to an integer, so a caller
//      cannot write `if (report.status())` and read "valid", and a warning cannot be passed where a
//      failure is wanted.
//
//   2. THE IDENTITY SPACES DO NOT MIX. An analysis, a meshing control, a load and a condition are
//      different identifiers, and a solver's source is not a report's source.
//
//   3. THE STATUS IS DERIVED, NOT STORED. There is no field that holds "valid", no setter, and no
//      per-finding display severity: the severity is a function of the code, so a viewer cannot
//      downgrade a fatal condition. The thresholds are constants.
//
//   4. THE SCOPE LINE AND THE ONE OWNER. Validation does not solve, repair, remesh, assign a material
//      or persist; there is no policy object a caller can edit, no second function that accepts a
//      thermal result, and no display setting in a report's source. Their absence from the types is
//      the proof that this milestone decides acceptance and does nothing else.
//
//   5. A REPORT IS NEITHER A RESULT NOR A BALANCE. It is a derived verdict about them.
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/thermal/ThermalValidation.hpp>

#include <span>
#include <vector>

using namespace bettercad;
using namespace bettercad::thermal;

int main() {
    // Dead code that must still COMPILE in the control, so every call below is checked against the real
    // signatures without needing a document at run time.
    const Document* document = nullptr;
    const features::Regenerator* regenerator = nullptr;
    const meshing::Mesher* mesher = nullptr;
    const ThermalValidationReport* report = nullptr;
    const meshing::MeshQualityReport* quality = nullptr;
    const ThermalBalanceResult* balance = nullptr;
    const SteadyThermalSolution* steady = nullptr;
    if (document != nullptr && regenerator != nullptr && mesher != nullptr && report != nullptr && quality != nullptr &&
        balance != nullptr && steady != nullptr) {
        const ThermalDerivedState state{};
        const ThermalValidationReport produced =
            validateThermalAnalysis(*document, *regenerator, *mesher, ThermalAnalysisId::fromValue(1), state);
        (void)isCurrent(produced, *document, *regenerator, *mesher);
        (void)classifyThermalMesh(*quality);
        (void)checkHeatBalanceStates(balance->states(), 1);
        (void)report->status();
        (void)report->accepted();
        (void)report->solveAccepted();
        (void)report->inputsAccepted();
        (void)report->statusOf(ValidationRule::Material);
        (void)report->errorCount();
        (void)report->warningCount();
        (void)report->findings;
        (void)report->checks;
        (void)report->metrics;
        (void)report->source;
        (void)report->policyFingerprint;
        (void)severityOf(ValidationCode::MeshStale);
        (void)ruleOf(ValidationCode::MeshStale);
        (void)thermalQualityThresholds();
        (void)thermalValidationPolicyFingerprint();
        (void)kThermalRadiusRatioFloor;
        (void)kThermalAspectRatioCeiling;
    }

#if defined(BETTERCAD_CF_A_STATUS_IS_NOT_A_BOOL)
    // `if (report.status())` would read "valid". It does not compile; `accepted()` is the question.
    if (report != nullptr) {
        [[maybe_unused]] bool valid = report->status();
    }
#endif

#if defined(BETTERCAD_CF_A_CODE_IS_NOT_AN_INTEGER)
    [[maybe_unused]] ValidationCode bad = 3;
#endif

#if defined(BETTERCAD_CF_A_RULE_IS_NOT_A_CODE)
    [[maybe_unused]] ValidationCode bad = ValidationRule::Material;
#endif

#if defined(BETTERCAD_CF_A_SEVERITY_IS_NOT_A_STATUS)
    // A warning is not a verdict.
    [[maybe_unused]] ValidationStatus bad = ValidationSeverity::Warning;
#endif

#if defined(BETTERCAD_CF_A_RULE_STATUS_IS_NOT_AN_OVERALL_STATUS)
    // A rule passing is not the analysis being accepted.
    [[maybe_unused]] ValidationStatus bad = CheckStatus::Pass;
#endif

#if defined(BETTERCAD_CF_A_CHECK_STATE_IS_NOT_A_CHECK_STATUS)
    // "Evaluated" says a rule ran, not that it passed.
    [[maybe_unused]] CheckStatus bad = CheckState::Evaluated;
#endif

#if defined(BETTERCAD_CF_AN_INTEGER_IS_NOT_A_SEVERITY)
    [[maybe_unused]] ValidationSeverity bad = 2;
#endif

#if defined(BETTERCAD_CF_A_MESH_CONTROL_ID_IS_NOT_AN_ANALYSIS_ID)
    if (document != nullptr && regenerator != nullptr && mesher != nullptr) {
        (void)validateThermalAnalysis(*document, *regenerator, *mesher, MeshControlId::fromValue(1));
    }
#endif

#if defined(BETTERCAD_CF_A_SOLVER_SOURCE_IS_NOT_A_REPORT_SOURCE)
    // A report describes the analysis (the result source); a solver's source describes a solve.
    [[maybe_unused]] ThermalResultSource bad = SteadySolutionSource{};
#endif

#if defined(BETTERCAD_CF_A_BALANCE_IS_NOT_A_REPORT)
    if (balance != nullptr) {
        [[maybe_unused]] const ThermalValidationReport& bad = *balance;
    }
#endif

#if defined(BETTERCAD_CF_A_REPORT_IS_NOT_A_THERMAL_RESULT)
    if (report != nullptr) {
        [[maybe_unused]] const ThermalResult& bad = *report;
    }
#endif

#if defined(BETTERCAD_CF_A_SOLUTION_IS_NOT_A_REPORT)
    if (steady != nullptr) {
        [[maybe_unused]] const ThermalValidationReport& bad = *steady;
    }
#endif

#if defined(BETTERCAD_CF_THE_REPORT_HAS_NO_STORED_STATUS)
    // The overall status is computed from the checks every time it is asked.
    if (report != nullptr) {
        (void)report->overallStatus;
    }
#endif

#if defined(BETTERCAD_CF_THE_REPORT_HAS_NO_VALID_FLAG)
    if (report != nullptr) {
        (void)report->valid;
    }
#endif

#if defined(BETTERCAD_CF_THE_REPORT_HAS_NO_STATUS_SETTER)
    ThermalValidationReport mutableReport;
    mutableReport.setStatus(ValidationStatus::Pass);
#endif

#if defined(BETTERCAD_CF_THE_REPORT_HAS_NO_IS_VALIDATED_FLAG)
    // No `analysis.isValidated = true` beside the checks, to drift from them.
    if (report != nullptr) {
        (void)report->isValidated;
    }
#endif

#if defined(BETTERCAD_CF_A_FINDING_HAS_NO_DISPLAY_SEVERITY)
    // Core validation owns severity. A viewer has no field in which to downgrade a fatal condition.
    ValidationFinding finding;
    (void)finding.displaySeverity;
#endif

#if defined(BETTERCAD_CF_A_FINDING_HAS_NO_OVERRIDE)
    ValidationFinding finding;
    (void)finding.overriddenBy;
#endif

#if defined(BETTERCAD_CF_THE_THRESHOLDS_ARE_CONSTANTS)
    kThermalRadiusRatioFloor = 1.0;
#endif

#if defined(BETTERCAD_CF_THE_ASPECT_CEILING_IS_A_CONSTANT)
    kThermalAspectRatioCeiling = 1.0e9;
#endif

#if defined(BETTERCAD_CF_THE_ACCURACY_REQUIREMENT_IS_A_CONSTANT)
    kThermalGradientAccuracy = 1.0;
#endif

#if defined(BETTERCAD_CF_THE_REPORT_IS_READ_ONLY_THROUGH_A_CONST_VIEW)
    if (report != nullptr) {
        report->findings.clear();
    }
#endif

#if defined(BETTERCAD_CF_THE_CHECKS_ARE_READ_ONLY_THROUGH_A_CONST_VIEW)
    if (report != nullptr) {
        report->checks[0].state = CheckState::Evaluated;
    }
#endif

#if defined(BETTERCAD_CF_THE_REPORT_SOURCE_HAS_NO_DISPLAY_SETTING)
    // A colour range, a legend or a camera cannot stale a validation: none is part of its source.
    [[maybe_unused]] auto bad = ThermalResultSource{}.colourMap;
#endif

#if defined(BETTERCAD_CF_THE_REPORT_SOURCE_HAS_NO_TOLERANCE_OVERRIDE)
    [[maybe_unused]] auto bad = ThermalResultSource{}.residualToleranceOverride;
#endif

#if defined(BETTERCAD_CF_THE_MESH_VERDICT_HAS_NO_REPAIRED_MESH)
    // Validation observes. It never moves a node, swaps a tetrahedron's nodes or returns a repaired mesh.
    if (quality != nullptr) {
        (void)classifyThermalMesh(*quality).repairedMesh;
    }
#endif

#if defined(BETTERCAD_CF_THE_DERIVED_STATE_HAS_NO_SOLVER)
    // The validator judges what exists. It does not run a second solve.
    ThermalDerivedState state;
    (void)state.solve;
#endif

#if defined(BETTERCAD_CF_THE_DERIVED_STATE_HAS_NO_REMESH)
    ThermalDerivedState state;
    (void)state.remesh;
#endif

#if defined(BETTERCAD_CF_THERE_IS_NO_POLICY_OBJECT_TO_EDIT)
    // The thresholds are application policy, not canonical user intent and not a settings struct a caller fills in.
    [[maybe_unused]] ThermalValidationPolicy policy;
#endif

#if defined(BETTERCAD_CF_THERE_IS_NO_SECOND_ACCEPTANCE_OWNER)
    // The solver, the recovery, the balance, a GUI and a CLI consume the report; none has a function of its own that accepts a result.
    if (steady != nullptr) {
        (void)acceptThermalResult(*steady);
    }
#endif

#if defined(BETTERCAD_CF_THERE_IS_NO_SOLVE_AND_VALIDATE_IN_THE_VALIDATOR)
    if (document != nullptr && regenerator != nullptr && mesher != nullptr) {
        (void)solveAndValidateThermalAnalysis(*document, *regenerator, *mesher, ThermalAnalysisId::fromValue(1));
    }
#endif

#if defined(BETTERCAD_CF_THERE_IS_NO_VALIDATION_PERSISTENCE)
    if (report != nullptr) {
        (void)saveValidationReport(*report);
    }
#endif

#if defined(BETTERCAD_CF_THERE_IS_NO_SEVERITY_SETTER)
    // The severity is `severityOf(code)`, a function with no state.
    severityOf(ValidationCode::MeshStale) = ValidationSeverity::Info;
#endif

    return 0;
}
