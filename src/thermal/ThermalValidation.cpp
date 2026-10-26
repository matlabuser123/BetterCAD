#include <bettercad/thermal/ThermalValidation.hpp>

#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/thermal/ThermalAnalysis.hpp>
#include <bettercad/thermal/ThermalMaterial.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>
#include <optional>
#include <tuple>
#include <utility>

namespace bettercad::thermal {
namespace {

// ---------------------------------------------------------------------------
// Findings
// ---------------------------------------------------------------------------

[[nodiscard]] ValidationFinding make(ValidationCode code, std::string message) {
    ValidationFinding finding;
    finding.code = code;
    finding.severity = severityOf(code);
    finding.rule = ruleOf(code);
    finding.message = std::move(message);
    return finding;
}

[[nodiscard]] bool finite(double value) noexcept { return std::isfinite(value); }

/// Whether @p value is on the BAD side of @p bound for a metric whose good direction is @p higherIsBetter. STRICT, in P16's
/// direction: a value exactly on a bound is on the good side. A value that is not a number is never on the good side.
[[nodiscard]] bool pastBound(double value, double bound, bool higherIsBetter) noexcept {
    if (!std::isfinite(value)) {
        return true;
    }
    return higherIsBetter ? value < bound : value > bound;
}

/// The value of a per-element metric of a P16 tetrahedron record. These are P16's own stored values, read -- never recomputed.
[[nodiscard]] std::optional<double> elementValue(const meshing::TetQuality& tet, meshing::QualityMetric metric) noexcept {
    if (!tet.defined) {
        return std::nullopt;
    }
    switch (metric) {
    case meshing::QualityMetric::TetRadiusRatio:
        return tet.radiusRatio;
    case meshing::QualityMetric::TetAspectRatio:
        return tet.aspectRatio;
    case meshing::QualityMetric::TetMinDihedralAngle:
        return tet.minDihedral.si();
    case meshing::QualityMetric::TetMaxDihedralAngle:
        return tet.maxDihedral.si();
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::string stepText(std::size_t step, double time, double stepSize) {
    return std::format("step {} (t = {:.9g} s, dt = {:.9g} s)", step, time, stepSize);
}

// ---------------------------------------------------------------------------
// The policy as text
// ---------------------------------------------------------------------------

[[nodiscard]] std::string number(double value) { return std::format("{}", value); }

constexpr ValidationCode kLastCode = ValidationCode::BalanceAttributionMismatch;

} // namespace

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------

std::string_view toString(ValidationRule rule) noexcept {
    switch (rule) {
    case ValidationRule::AnalysisDefinition:
        return "TV_ANALYSIS_DEFINITION";
    case ValidationRule::GeometryCurrent:
        return "TV_GEOMETRY_CURRENT";
    case ValidationRule::MeshCurrent:
        return "TV_MESH_CURRENT";
    case ValidationRule::MeshStructural:
        return "TV_MESH_STRUCTURAL";
    case ValidationRule::MeshQuality:
        return "TV_MESH_QUALITY";
    case ValidationRule::Material:
        return "TV_MATERIAL";
    case ValidationRule::Loads:
        return "TV_LOADS";
    case ValidationRule::BoundaryConditions:
        return "TV_BCS";
    case ValidationRule::TransientSetup:
        return "TV_TRANSIENT_SETUP";
    case ValidationRule::SourceCompatibility:
        return "TV_SOURCE_COMPAT";
    case ValidationRule::WellPosed:
        return "TV_WELL_POSED";
    case ValidationRule::SolverResidual:
        return "TV_SOLVER_RESIDUAL";
    case ValidationRule::ResultFinite:
        return "TV_RESULT_FINITE";
    case ValidationRule::PostResult:
        return "TV_POST";
    case ValidationRule::HeatBalance:
        return "TV_BALANCE";
    }
    return "TV_UNKNOWN";
}

std::string_view toString(ValidationGroup group) noexcept {
    switch (group) {
    case ValidationGroup::Input:
        return "input";
    case ValidationGroup::System:
        return "system";
    case ValidationGroup::Solve:
        return "solve";
    case ValidationGroup::Result:
        return "result";
    case ValidationGroup::Balance:
        return "balance";
    }
    return "unknown";
}

ValidationGroup groupOf(ValidationRule rule) noexcept {
    switch (rule) {
    case ValidationRule::AnalysisDefinition:
    case ValidationRule::GeometryCurrent:
    case ValidationRule::MeshCurrent:
    case ValidationRule::MeshStructural:
    case ValidationRule::MeshQuality:
    case ValidationRule::Material:
    case ValidationRule::Loads:
    case ValidationRule::BoundaryConditions:
    case ValidationRule::TransientSetup:
        return ValidationGroup::Input;
    case ValidationRule::SourceCompatibility:
    case ValidationRule::WellPosed:
        return ValidationGroup::System;
    case ValidationRule::SolverResidual:
        return ValidationGroup::Solve;
    case ValidationRule::ResultFinite:
    case ValidationRule::PostResult:
        return ValidationGroup::Result;
    case ValidationRule::HeatBalance:
        return ValidationGroup::Balance;
    }
    return ValidationGroup::Input;
}

std::string_view toString(ValidationCode code) noexcept {
    switch (code) {
    case ValidationCode::AnalysisNotFound:
        return "analysis_not_found";
    case ValidationCode::ControlNotFound:
        return "control_not_found";
    case ValidationCode::GeometryIneligible:
        return "geometry_ineligible";
    case ValidationCode::MeshMissing:
        return "mesh_missing";
    case ValidationCode::MeshStale:
        return "mesh_stale";
    case ValidationCode::MeshGenerationFailed:
        return "mesh_generation_failed";
    case ValidationCode::MeshStructurallyInvalid:
        return "mesh_structurally_invalid";
    case ValidationCode::MeshHasInvalidElements:
        return "mesh_has_invalid_elements";
    case ValidationCode::MeshQualityPolicyUnusable:
        return "mesh_quality_policy_unusable";
    case ValidationCode::ElementBelowAccuracyFloor:
        return "element_below_accuracy_floor";
    case ValidationCode::ElementOutsideQualifiedEnvelope:
        return "element_outside_qualified_envelope";
    case ValidationCode::MaterialMissing:
        return "material_missing";
    case ValidationCode::MaterialConductivityUnusable:
        return "material_conductivity_unusable";
    case ValidationCode::MaterialIncompleteForMode:
        return "material_incomplete_for_mode";
    case ValidationCode::LoadsInvalid:
        return "loads_invalid";
    case ValidationCode::NoLoadsConfigured:
        return "no_loads_configured";
    case ValidationCode::BoundaryConditionsInvalid:
        return "boundary_conditions_invalid";
    case ValidationCode::NoBoundaryConditionsConfigured:
        return "no_boundary_conditions_configured";
    case ValidationCode::InitialTemperatureInvalid:
        return "initial_temperature_invalid";
    case ValidationCode::TimeSettingsInvalid:
        return "time_settings_invalid";
    case ValidationCode::SourceStale:
        return "source_stale";
    case ValidationCode::SourceMismatch:
        return "source_mismatch";
    case ValidationCode::ModeMismatch:
        return "mode_mismatch";
    case ValidationCode::SystemMissing:
        return "system_missing";
    case ValidationCode::NoTemperatureDatum:
        return "no_temperature_datum";
    case ValidationCode::UnanchoredRegion:
        return "unanchored_region";
    case ValidationCode::CapacityMissing:
        return "capacity_missing";
    case ValidationCode::CapacityInvalid:
        return "capacity_invalid";
    case ValidationCode::SystemNotFinite:
        return "system_not_finite";
    case ValidationCode::TransientWithoutDatum:
        return "transient_without_datum";
    case ValidationCode::SolveFailed:
        return "solve_failed";
    case ValidationCode::SolutionMissing:
        return "solution_missing";
    case ValidationCode::ResidualAboveTolerance:
        return "residual_above_tolerance";
    case ValidationCode::ResidualNotFinite:
        return "residual_not_finite";
    case ValidationCode::SolverSettingsInvalid:
        return "solver_settings_invalid";
    case ValidationCode::PivotBelowFloor:
        return "pivot_below_floor";
    case ValidationCode::ConstraintNotHeld:
        return "constraint_not_held";
    case ValidationCode::HistoryContractViolated:
        return "history_contract_violated";
    case ValidationCode::InitialConditionOverriddenByPrescription:
        return "initial_condition_overridden_by_prescription";
    case ValidationCode::TemperatureNotFinite:
        return "temperature_not_finite";
    case ValidationCode::ResultSizeMismatch:
        return "result_size_mismatch";
    case ValidationCode::PostMissing:
        return "post_missing";
    case ValidationCode::PostFieldNotFinite:
        return "post_field_not_finite";
    case ValidationCode::PostCardinalityMismatch:
        return "post_cardinality_mismatch";
    case ValidationCode::PostTemperatureMismatch:
        return "post_temperature_mismatch";
    case ValidationCode::BalanceMissing:
        return "balance_missing";
    case ValidationCode::BalanceOutOfTolerance:
        return "balance_out_of_tolerance";
    case ValidationCode::BalanceNotFinite:
        return "balance_not_finite";
    case ValidationCode::BalanceStatesMismatch:
        return "balance_states_mismatch";
    case ValidationCode::BalanceAttributionMismatch:
        return "balance_attribution_mismatch";
    }
    return "unknown";
}

std::string_view toString(ValidationSeverity severity) noexcept {
    switch (severity) {
    case ValidationSeverity::Info:
        return "info";
    case ValidationSeverity::Warning:
        return "warning";
    case ValidationSeverity::Error:
        return "error";
    }
    return "unknown";
}

ValidationSeverity severityOf(ValidationCode code) noexcept {
    switch (code) {
    case ValidationCode::NoLoadsConfigured:
    case ValidationCode::NoBoundaryConditionsConfigured:
    case ValidationCode::TransientWithoutDatum:
    case ValidationCode::InitialConditionOverriddenByPrescription:
        return ValidationSeverity::Info;
    case ValidationCode::MeshQualityPolicyUnusable:
    case ValidationCode::ElementOutsideQualifiedEnvelope:
        return ValidationSeverity::Warning;
    default:
        return ValidationSeverity::Error;
    }
}

ValidationRule ruleOf(ValidationCode code) noexcept {
    switch (code) {
    case ValidationCode::AnalysisNotFound:
        return ValidationRule::AnalysisDefinition;
    case ValidationCode::ControlNotFound:
    case ValidationCode::GeometryIneligible:
        return ValidationRule::GeometryCurrent;
    case ValidationCode::MeshMissing:
    case ValidationCode::MeshStale:
    case ValidationCode::MeshGenerationFailed:
        return ValidationRule::MeshCurrent;
    case ValidationCode::MeshStructurallyInvalid:
    case ValidationCode::MeshHasInvalidElements:
        return ValidationRule::MeshStructural;
    case ValidationCode::MeshQualityPolicyUnusable:
    case ValidationCode::ElementBelowAccuracyFloor:
    case ValidationCode::ElementOutsideQualifiedEnvelope:
        return ValidationRule::MeshQuality;
    case ValidationCode::MaterialMissing:
    case ValidationCode::MaterialConductivityUnusable:
    case ValidationCode::MaterialIncompleteForMode:
        return ValidationRule::Material;
    case ValidationCode::LoadsInvalid:
    case ValidationCode::NoLoadsConfigured:
        return ValidationRule::Loads;
    case ValidationCode::BoundaryConditionsInvalid:
    case ValidationCode::NoBoundaryConditionsConfigured:
        return ValidationRule::BoundaryConditions;
    case ValidationCode::InitialTemperatureInvalid:
    case ValidationCode::TimeSettingsInvalid:
        return ValidationRule::TransientSetup;
    case ValidationCode::SourceStale:
    case ValidationCode::SourceMismatch:
    case ValidationCode::ModeMismatch:
        return ValidationRule::SourceCompatibility;
    case ValidationCode::SystemMissing:
    case ValidationCode::NoTemperatureDatum:
    case ValidationCode::UnanchoredRegion:
    case ValidationCode::CapacityMissing:
    case ValidationCode::CapacityInvalid:
    case ValidationCode::SystemNotFinite:
    case ValidationCode::TransientWithoutDatum:
        return ValidationRule::WellPosed;
    case ValidationCode::SolveFailed:
    case ValidationCode::SolutionMissing:
    case ValidationCode::ResidualAboveTolerance:
    case ValidationCode::ResidualNotFinite:
    case ValidationCode::SolverSettingsInvalid:
    case ValidationCode::PivotBelowFloor:
    case ValidationCode::ConstraintNotHeld:
    case ValidationCode::HistoryContractViolated:
    case ValidationCode::InitialConditionOverriddenByPrescription:
        return ValidationRule::SolverResidual;
    case ValidationCode::TemperatureNotFinite:
    case ValidationCode::ResultSizeMismatch:
        return ValidationRule::ResultFinite;
    case ValidationCode::PostMissing:
    case ValidationCode::PostFieldNotFinite:
    case ValidationCode::PostCardinalityMismatch:
    case ValidationCode::PostTemperatureMismatch:
        return ValidationRule::PostResult;
    case ValidationCode::BalanceMissing:
    case ValidationCode::BalanceOutOfTolerance:
    case ValidationCode::BalanceNotFinite:
    case ValidationCode::BalanceStatesMismatch:
    case ValidationCode::BalanceAttributionMismatch:
        return ValidationRule::HeatBalance;
    }
    return ValidationRule::AnalysisDefinition;
}

std::string_view toString(ValidationSubject subject) noexcept {
    switch (subject) {
    case ValidationSubject::DofMap:
        return "dof_map";
    case ValidationSubject::PreparedLoads:
        return "prepared_loads";
    case ValidationSubject::PreparedBoundary:
        return "prepared_boundary";
    case ValidationSubject::System:
        return "system";
    case ValidationSubject::Solution:
        return "solution";
    case ValidationSubject::PostResult:
        return "post_result";
    case ValidationSubject::BalanceResult:
        return "balance_result";
    }
    return "unknown";
}

std::string_view toString(CheckState state) noexcept {
    switch (state) {
    case CheckState::Evaluated:
        return "evaluated";
    case CheckState::Skipped:
        return "skipped";
    case CheckState::NotApplicable:
        return "not_applicable";
    }
    return "unknown";
}

std::string_view toString(CheckStatus status) noexcept {
    switch (status) {
    case CheckStatus::Pass:
        return "PASS";
    case CheckStatus::Warning:
        return "WARN";
    case CheckStatus::Fail:
        return "FAIL";
    case CheckStatus::Skipped:
        return "SKIPPED";
    case CheckStatus::NotApplicable:
        return "NOT_APPLICABLE";
    }
    return "UNKNOWN";
}

std::string_view toString(ValidationStatus status) noexcept {
    switch (status) {
    case ValidationStatus::Pass:
        return "PASS";
    case ValidationStatus::PassWithWarnings:
        return "PASS_WITH_WARNINGS";
    case ValidationStatus::Fail:
        return "FAIL";
    }
    return "UNKNOWN";
}

// ---------------------------------------------------------------------------
// The policy
// ---------------------------------------------------------------------------

meshing::QualityThresholds thermalQualityThresholds() {
    meshing::QualityThresholds thresholds;
    thresholds.limits[meshing::QualityMetric::TetRadiusRatio] =
        meshing::QualityThreshold{.warning = 3.0e-4, .failure = kThermalRadiusRatioFloor};
    thresholds.limits[meshing::QualityMetric::TetAspectRatio] =
        meshing::QualityThreshold{.warning = 81.0, .failure = kThermalAspectRatioCeiling};
    // The dihedral angles carry a warning at the edge of the qualified envelope and NO failure bound: no law was measured
    // against them, and the measured distributions show they cannot separate a qualified cylinder from a sliver.
    thresholds.limits[meshing::QualityMetric::TetMinDihedralAngle] = meshing::QualityThreshold{.warning = 6.0e-3, .failure = std::nullopt};
    thresholds.limits[meshing::QualityMetric::TetMaxDihedralAngle] = meshing::QualityThreshold{.warning = 3.125, .failure = std::nullopt};
    return thresholds;
}

std::string thermalValidationPolicyText() {
    std::string text;
    text += "policy p18-valid v1\n";
    text += "rules:";
    for (const ValidationRule rule : kValidationRules) {
        text += ' ';
        text += toString(rule);
    }
    text += '\n';
    for (int i = 0; i <= static_cast<int>(kLastCode); ++i) {
        const auto code = static_cast<ValidationCode>(i);
        text += std::format("code {} rule={} severity={}\n", toString(code), toString(ruleOf(code)), toString(severityOf(code)));
    }
    for (const auto& [metric, limit] : thermalQualityThresholds().limits) {
        text += std::format("mesh {} direction={} warning={} failure={}\n", meshing::toString(metric),
                            meshing::direction(metric) == meshing::QualityDirection::HigherIsBetter ? "higher_is_better" : "lower_is_better",
                            limit.warning.has_value() ? number(*limit.warning) : std::string{"none"},
                            limit.failure.has_value() ? number(*limit.failure) : std::string{"none"});
    }
    text += std::format("mesh accuracy_requirement={}\n", number(kThermalGradientAccuracy));
    text += "comparison strict: a value equal to a bound is on the good side; a value that is not a number is never\n";
    text += "authority residual.steady=canonical settings.relativeResidualTolerance via validateSteadySolution (P18-SOLVE-001)\n";
    text += "authority residual.transient=canonical settings.relativeResidualTolerance via validateTransientStep (P18-TRANSIENT-001), every step, actual step size\n";
    text += "authority pivot=canonical settings.pivotFloor\n";
    text += std::format("authority balance=balanceWithinTolerance absolute={} relative={} (P18-BALANCE-001)\n",
                        number(kBalanceAbsoluteToleranceW), number(kBalanceRelativeTolerance));
    text += "steady requires k; transient requires k rho cp, initial temperature, time settings, capacity\n";
    text += "steady datum rule applies to steady only; a transient needs no datum\n";
    text += "no loads and no boundary conditions are not errors\n";
    text += "failure policy: input rules judged independently; system, solve, result, balance blocked by an earlier failing group\n";
    return text;
}

std::string thermalValidationPolicyFingerprint() {
    const std::string text = thermalValidationPolicyText();
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char c : text) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 1099511628211ULL;
    }
    return std::format("p18-valid-v1:{:016x}", hash);
}

// ---------------------------------------------------------------------------
// The mesh
// ---------------------------------------------------------------------------

ThermalMeshVerdict classifyThermalMesh(const meshing::MeshQualityReport& quality) {
    ThermalMeshVerdict verdict;

    if (!quality.structurallyValid) {
        ValidationFinding finding = make(ValidationCode::MeshStructurallyInvalid,
                                         std::format("the mesh violates {} structural invariant(s); an inverted, degenerate or repeated-handle "
                                                     "element is not a tetrahedron",
                                                     quality.structural.issues.size()));
        finding.count = quality.structural.issues.size();
        verdict.structural.push_back(std::move(finding));
    }
    if (quality.invalidElements > 0) {
        ValidationFinding finding =
            make(ValidationCode::MeshHasInvalidElements,
                 std::format("{} element(s) are invalid: structurally refused, or a metric could not be computed as a finite number",
                             quality.invalidElements));
        finding.count = quality.invalidElements;
        verdict.structural.push_back(std::move(finding));
    }
    if (quality.thresholdPolicyError.has_value()) {
        verdict.quality.push_back(make(ValidationCode::MeshQualityPolicyUnusable,
                                       "the quality policy the report was produced under is unusable, so P16 classified nothing; the "
                                       "metrics were still measured and are judged here: " +
                                           quality.thresholdPolicyError->message));
    }

    for (const auto& [metric, limit] : thermalQualityThresholds().limits) {
        const auto found = quality.summaries.find(metric);
        if (found == quality.summaries.end() || found->second.count != quality.tetCount) {
            // A CLASSIFIABLE METRIC THAT WAS NOT MEASURED FOR EVERY ELEMENT IS NOT A PASS. P16 always summarises every metric over every
            // tetrahedron, so this is unreachable today; the validator does not rely on that, because skipping the metric would accept
            // a mesh with an element nobody judged.
            if (quality.tetCount > 0) {
                ValidationFinding finding = make(ValidationCode::MeshHasInvalidElements,
                                                 std::format("the {} was measured for {} of the mesh's {} element(s), so the mesh cannot be judged",
                                                             meshing::toString(metric), found == quality.summaries.end() ? std::size_t{0} : found->second.count,
                                                             quality.tetCount));
                finding.metric = metric;
                finding.count = quality.tetCount;
                verdict.structural.push_back(std::move(finding));
            }
            continue;
        }
        const meshing::MetricSummary& summary = found->second;
        const bool higherIsBetter = meshing::direction(metric) == meshing::QualityDirection::HigherIsBetter;
        const double worst = higherIsBetter ? summary.minimum : summary.maximum;
        verdict.worstByMetric.push_back(ThermalMetricObservation{.metric = metric, .element = summary.worst, .value = worst});

        const auto violators = [&](double bound) {
            std::size_t count = 0;
            for (const meshing::TetQuality& tet : quality.tets) {
                const std::optional<double> value = elementValue(tet, metric);
                if (value.has_value() && pastBound(*value, bound, higherIsBetter)) {
                    ++count;
                }
            }
            return count;
        };

        if (!finite(worst)) {
            ValidationFinding finding =
                make(ValidationCode::MeshHasInvalidElements,
                     std::format("the worst {} is not a finite number, so the element cannot be judged", meshing::toString(metric)));
            finding.metric = metric;
            finding.element = summary.worst;
            verdict.structural.push_back(std::move(finding));
            continue;
        }
        if (limit.failure.has_value() && pastBound(worst, *limit.failure, higherIsBetter)) {
            ValidationFinding finding = make(
                ValidationCode::ElementBelowAccuracyFloor,
                std::format("{} {} of element {} is {} the accuracy bound {}: the gradient recovered on it is not guaranteed to {} relative",
                            meshing::toString(metric), number(worst), summary.worst.value(), higherIsBetter ? "below" : "above",
                            number(*limit.failure), number(kThermalGradientAccuracy)));
            finding.metric = metric;
            finding.element = summary.worst;
            finding.value = worst;
            finding.threshold = *limit.failure;
            finding.unit = meshing::unitOf(metric);
            finding.count = violators(*limit.failure);
            verdict.quality.push_back(std::move(finding));
        } else if (limit.warning.has_value() && pastBound(worst, *limit.warning, higherIsBetter)) {
            ValidationFinding finding = make(
                ValidationCode::ElementOutsideQualifiedEnvelope,
                std::format("{} {} of element {} is outside the range of every qualified P16 reference mesh (bound {})",
                            meshing::toString(metric), number(worst), summary.worst.value(), number(*limit.warning)));
            finding.metric = metric;
            finding.element = summary.worst;
            finding.value = worst;
            finding.threshold = *limit.warning;
            finding.unit = meshing::unitOf(metric);
            finding.count = violators(*limit.warning);
            verdict.quality.push_back(std::move(finding));
        }
    }
    return verdict;
}

// ---------------------------------------------------------------------------
// The result seams
// ---------------------------------------------------------------------------

std::vector<ValidationFinding> checkTemperatureStates(std::span<const std::span<const Temperature>> states,
                                                      std::size_t expectedDofs, std::span<const double> times) {
    std::vector<ValidationFinding> findings;
    for (std::size_t s = 0; s < states.size(); ++s) {
        const std::span<const Temperature> state = states[s];
        const std::optional<double> time = s < times.size() ? std::optional<double>{times[s]} : std::nullopt;
        if (state.size() != expectedDofs) {
            ValidationFinding finding = make(ValidationCode::ResultSizeMismatch,
                                             std::format("state {} holds {} temperatures; the numbering has {} unknowns", s, state.size(), expectedDofs));
            finding.step = s;
            finding.time = time;
            finding.value = static_cast<double>(state.size());
            finding.threshold = static_cast<double>(expectedDofs);
            findings.push_back(std::move(finding));
            continue;
        }
        std::size_t bad = 0;
        std::optional<std::size_t> first;
        bool belowZero = false;
        for (std::size_t i = 0; i < state.size(); ++i) {
            if (!isAbsoluteTemperature(state[i])) {
                ++bad;
                if (!first.has_value()) {
                    first = i;
                    belowZero = std::isfinite(state[i].si());
                }
            }
        }
        if (bad > 0) {
            ValidationFinding finding = make(
                ValidationCode::TemperatureNotFinite,
                std::format("{} temperature(s) of state {} are not finite absolute temperatures; the first is unknown {} = {}", bad, s,
                            *first, state[*first].si()));
            finding.step = s;
            finding.time = time;
            finding.count = bad;
            finding.cause = belowZero ? "below_absolute_zero" : "non_finite";
            finding.unit = "K";
            findings.push_back(std::move(finding));
        }
    }
    return findings;
}

namespace {

[[nodiscard]] ValidationCode residualCodeFor(SteadySolveProblem problem) noexcept {
    switch (problem) {
    case SteadySolveProblem::ResidualTooLarge:
        return ValidationCode::ResidualAboveTolerance;
    case SteadySolveProblem::ConstraintNotPreserved:
        return ValidationCode::ConstraintNotHeld;
    case SteadySolveProblem::InvalidSettings:
        return ValidationCode::SolverSettingsInvalid;
    case SteadySolveProblem::DofMapMismatch:
    case SteadySolveProblem::ConstraintMismatch:
    case SteadySolveProblem::NotSteadySystem:
    case SteadySolveProblem::StaleSystem:
    case SteadySolveProblem::StaleBoundary:
        return ValidationCode::SourceMismatch;
    default:
        return ValidationCode::ResidualNotFinite;
    }
}

[[nodiscard]] ValidationCode residualCodeFor(TransientProblem problem) noexcept {
    switch (problem) {
    case TransientProblem::ResidualTooLarge:
        return ValidationCode::ResidualAboveTolerance;
    case TransientProblem::ConstraintNotPreserved:
        return ValidationCode::ConstraintNotHeld;
    case TransientProblem::InvalidSettings:
        return ValidationCode::SolverSettingsInvalid;
    case TransientProblem::DofMapMismatch:
    case TransientProblem::ConstraintMismatch:
    case TransientProblem::NotTransientSystem:
    case TransientProblem::StaleSystem:
    case TransientProblem::StaleBoundary:
        return ValidationCode::SourceMismatch;
    default:
        return ValidationCode::ResidualNotFinite;
    }
}

} // namespace

ResidualVerdict checkSteadyResidual(const ConductanceMatrix& conductance, const ThermalHeatVector& heat,
                                    const ThermalFreeEquationMap& equations, const ThermalConstraintMap& constraints,
                                    std::span<const Temperature> temperatures, const ThermalSolverSettings& settings,
                                    std::optional<double> pivotRatio) {
    ResidualVerdict verdict;
    const std::expected<SteadyResidual, SteadySolveFailure> judged =
        validateSteadySolution(conductance, heat, equations, constraints, temperatures, settings);
    if (judged.has_value()) {
        verdict.residual = judged->normalized;
    } else {
        const SteadySolveProblem problem = judged.error().problem;
        ValidationFinding finding = make(residualCodeFor(problem), judged.error().error.message);
        finding.cause = toString(problem);
        if (problem == SteadySolveProblem::ResidualTooLarge) {
            // THE NUMBER ONLY. The verdict above is the qualified function's, with the canonical tolerance; this second call reads
            // the residual it refused, with a tolerance that cannot refuse, and its result is never consulted for acceptance.
            ThermalSolverSettings measuring = settings;
            measuring.relativeResidualTolerance = std::numeric_limits<double>::max();
            const std::expected<SteadyResidual, SteadySolveFailure> measured =
                validateSteadySolution(conductance, heat, equations, constraints, temperatures, measuring);
            if (measured.has_value()) {
                verdict.residual = measured->normalized;
                finding.value = measured->normalized;
            }
            finding.threshold = settings.relativeResidualTolerance;
        }
        verdict.findings.push_back(std::move(finding));
    }
    if (pivotRatio.has_value() && (!finite(*pivotRatio) || *pivotRatio < settings.pivotFloor)) {
        ValidationFinding finding = make(ValidationCode::PivotBelowFloor,
                                         std::format("the factorisation's pivot ratio {} is below the canonical floor {}", number(*pivotRatio),
                                                     number(settings.pivotFloor)));
        finding.value = *pivotRatio;
        finding.threshold = settings.pivotFloor;
        verdict.findings.push_back(std::move(finding));
    }
    return verdict;
}

TransientResidualVerdict checkTransientResiduals(const ConductanceMatrix& conductance, const CapacityMatrix& capacity,
                                                 const ThermalHeatVector& heat, const ThermalFreeEquationMap& equations,
                                                 const ThermalConstraintMap& constraints, const ThermalSolverSettings& settings,
                                                 const TimeGrid& grid, std::span<const TransientStepRecord> records,
                                                 std::span<const Temperature> initial,
                                                 std::span<const std::span<const Temperature>> states,
                                                 TransientOutputPolicy output) {
    TransientResidualVerdict verdict;
    const double tolerance = settings.relativeResidualTolerance;
    if (records.size() != grid.stepCount()) {
        verdict.findings.push_back(make(ValidationCode::HistoryContractViolated,
                                        std::format("the history holds {} step record(s); its time grid has {} step(s)", records.size(),
                                                    grid.stepCount())));
        return verdict;
    }

    const auto noteWorst = [&verdict](double residual, std::size_t step, double time, double dt) {
        if (!verdict.worstResidual.has_value() || residual > *verdict.worstResidual) {
            verdict.worstResidual = residual;
            verdict.worstStep = step;
            verdict.worstTime = time;
            verdict.worstStepSize = dt;
        }
    };
    const auto at = [](ValidationFinding finding, std::size_t step, double time, double dt) {
        finding.step = step;
        finding.time = time;
        finding.stepSize = dt;
        return finding;
    };

    for (std::size_t k = 1; k <= grid.stepCount(); ++k) {
        const double dt = grid.stepAt(k - 1);
        const double time = grid.timeAt(k);
        const TransientStepRecord& record = records[k - 1];

        // The recorded metrics: the integrator's own per-step residuals, as it accepted them. Finiteness FIRST, so a NaN never
        // reaches a comparison and falls through it.
        if (!finite(record.physicalResidual) || !finite(record.reducedResidual) ||
            (record.kernelInvoked && !finite(record.pivotRatio))) {
            ValidationFinding finding = make(ValidationCode::ResidualNotFinite,
                                             "a recorded residual or pivot ratio is not finite at " + stepText(k, time, dt));
            finding.cause = "recorded";
            verdict.findings.push_back(at(std::move(finding), k, time, dt));
            continue;
        }
        noteWorst(std::max(record.physicalResidual, record.reducedResidual), k, time, dt);
        if (record.physicalResidual > tolerance) {
            ValidationFinding finding = make(ValidationCode::ResidualAboveTolerance,
                                             std::format("the physical free residual {} exceeds the canonical tolerance {} at {}",
                                                         number(record.physicalResidual), number(tolerance), stepText(k, time, dt)));
            finding.value = record.physicalResidual;
            finding.threshold = tolerance;
            finding.cause = "physical_recorded";
            verdict.findings.push_back(at(std::move(finding), k, time, dt));
        }
        if (record.reducedResidual > tolerance) {
            ValidationFinding finding = make(ValidationCode::ResidualAboveTolerance,
                                             std::format("the reduced free residual {} exceeds the canonical tolerance {} at {}",
                                                         number(record.reducedResidual), number(tolerance), stepText(k, time, dt)));
            finding.value = record.reducedResidual;
            finding.threshold = tolerance;
            finding.cause = "reduced_recorded";
            verdict.findings.push_back(at(std::move(finding), k, time, dt));
        }
        if (record.kernelInvoked && record.pivotRatio < settings.pivotFloor) {
            ValidationFinding finding =
                make(ValidationCode::PivotBelowFloor, std::format("the pivot ratio {} is below the canonical floor {} at {}",
                                                                  number(record.pivotRatio), number(settings.pivotFloor), stepText(k, time, dt)));
            finding.value = record.pivotRatio;
            finding.threshold = settings.pivotFloor;
            verdict.findings.push_back(at(std::move(finding), k, time, dt));
        }

        // The independent re-evaluation, for every step whose two states are stored: the integrator's "success" is not an input.
        std::span<const Temperature> previous;
        std::span<const Temperature> next;
        if (output == TransientOutputPolicy::EveryStep && states.size() == grid.stepCount() + 1) {
            previous = states[k - 1];
            next = states[k];
        } else if (grid.stepCount() == 1 && !states.empty()) {
            previous = initial;
            next = states.back();
        }
        if (previous.empty() || next.empty()) {
            continue;
        }
        const std::expected<TransientStepResidual, TransientFailure> judged =
            validateTransientStep(conductance, capacity, heat, equations, constraints, previous, next, dt, settings);
        if (judged.has_value()) {
            noteWorst(std::max(judged->physical, judged->reduced), k, time, dt);
            continue;
        }
        const TransientProblem problem = judged.error().problem;
        ValidationFinding finding = make(residualCodeFor(problem), judged.error().error.message);
        finding.cause = toString(problem);
        if (problem == TransientProblem::ResidualTooLarge) {
            // THE NUMBER ONLY, as for the steady residual: the verdict is the qualified function's.
            ThermalSolverSettings measuring = settings;
            measuring.relativeResidualTolerance = std::numeric_limits<double>::max();
            const std::expected<TransientStepResidual, TransientFailure> measured =
                validateTransientStep(conductance, capacity, heat, equations, constraints, previous, next, dt, measuring);
            if (measured.has_value()) {
                const double worst = std::max(measured->physical, measured->reduced);
                finding.value = worst;
                noteWorst(worst, k, time, dt);
            }
            finding.threshold = tolerance;
        }
        verdict.findings.push_back(at(std::move(finding), k, time, dt));
    }
    return verdict;
}

std::vector<ValidationFinding> checkPostStates(std::span<const ThermalPostState> states, std::size_t expectedElements,
                                               std::size_t expectedDofs,
                                               std::span<const std::span<const Temperature>> solverStates) {
    std::vector<ValidationFinding> findings;
    if (states.size() != solverStates.size()) {
        findings.push_back(make(ValidationCode::PostCardinalityMismatch,
                                std::format("the recovered result holds {} state(s); the solution holds {}", states.size(), solverStates.size())));
    }
    for (std::size_t s = 0; s < states.size(); ++s) {
        const ThermalPostState& state = states[s];
        const std::optional<double> time = state.time.has_value() ? std::optional<double>{state.time->si()} : std::nullopt;
        const auto at = [&](ValidationFinding finding) {
            finding.step = s;
            finding.time = time;
            return finding;
        };

        // Cardinality FIRST: a missing element is fatal, never skipped, and a finiteness scan of a short array would be a lie.
        const bool cardinalityOk = state.temperatures.size() == expectedDofs && state.fields.gradients.size() == expectedElements &&
                                   state.fields.heatFluxes.size() == expectedElements &&
                                   state.fields.heatFluxMagnitudes.size() == expectedElements;
        if (!cardinalityOk) {
            findings.push_back(at(make(ValidationCode::PostCardinalityMismatch,
                                       std::format("state {} holds {} temperatures, {} gradients, {} fluxes and {} magnitudes; expected {} and {} per "
                                                   "tetrahedron",
                                                   s, state.temperatures.size(), state.fields.gradients.size(), state.fields.heatFluxes.size(),
                                                   state.fields.heatFluxMagnitudes.size(), expectedDofs, expectedElements))));
            continue;
        }

        if (s < solverStates.size()) {
            const std::span<const Temperature> solved = solverStates[s];
            bool same = solved.size() == state.temperatures.size();
            for (std::size_t i = 0; same && i < solved.size(); ++i) {
                same = std::bit_cast<std::uint64_t>(solved[i].si()) == std::bit_cast<std::uint64_t>(state.temperatures[i].si());
            }
            if (!same) {
                findings.push_back(at(make(ValidationCode::PostTemperatureMismatch,
                                           std::format("the recovered temperatures of state {} are not the solver's, bit for bit", s))));
            }
        }

        std::size_t badGradients = 0;
        std::size_t badFluxes = 0;
        std::size_t badMagnitudes = 0;
        std::optional<std::size_t> firstBad;
        for (std::size_t e = 0; e < expectedElements; ++e) {
            const bool g = isFinite(state.fields.gradients[e]);
            const bool q = isFinite(state.fields.heatFluxes[e]);
            const bool m = finite(state.fields.heatFluxMagnitudes[e].si());
            badGradients += g ? 0 : 1;
            badFluxes += q ? 0 : 1;
            badMagnitudes += m ? 0 : 1;
            if (!(g && q && m) && !firstBad.has_value()) {
                firstBad = e;
            }
        }
        if (firstBad.has_value()) {
            ValidationFinding finding = make(ValidationCode::PostFieldNotFinite,
                                             std::format("state {} holds {} non-finite gradient(s), {} flux(es) and {} magnitude(s); the first is "
                                                         "tetrahedron ordinal {}",
                                                         s, badGradients, badFluxes, badMagnitudes, *firstBad));
            finding.count = badGradients + badFluxes + badMagnitudes;
            findings.push_back(at(std::move(finding)));
        }
    }
    return findings;
}

BalanceVerdict checkHeatBalanceStates(std::span<const ThermalHeatBalance> states, std::size_t expectedStates) {
    BalanceVerdict verdict;
    if (states.size() != expectedStates) {
        verdict.findings.push_back(make(ValidationCode::BalanceStatesMismatch,
                                        std::format("the balance holds {} state(s); the history has {} step(s) to account for", states.size(),
                                                    expectedStates)));
    }
    for (std::size_t s = 0; s < states.size(); ++s) {
        const ThermalHeatBalance& state = states[s];
        const std::size_t step = state.step;
        const std::optional<double> time = state.time.has_value() ? std::optional<double>{state.time->si()} : std::nullopt;
        const std::optional<double> dt = state.stepSize.has_value() ? std::optional<double>{state.stepSize->si()} : std::nullopt;
        const auto at = [&](ValidationFinding finding) {
            finding.step = step;
            finding.time = time;
            finding.stepSize = dt;
            return finding;
        };

        // Finiteness of EVERY number the state holds, before any comparison: a NaN must never fall through one.
        bool allFinite = isFinite(state.terms) && finite(state.arithmeticScale.si());
        for (const LoadHeat& load : state.surfaceLoads) {
            allFinite = allFinite && finite(load.heatInto.si());
        }
        for (const LoadHeat& load : state.generationLoads) {
            allFinite = allFinite && finite(load.heatInto.si());
        }
        for (const ConvectionHeat& convection : state.convections) {
            allFinite = allFinite && finite(convection.heatInto.si()) && finite(convection.heatOut.si());
        }
        for (const ReactionHeat& reaction : state.reactions) {
            allFinite = allFinite && finite(reaction.heatInto.si());
        }
        if (state.storedEnergyChange.has_value()) {
            allFinite = allFinite && finite(state.storedEnergyChange->si());
        }
        const double residual = state.residual().si();
        const double gross = state.grossScale().si();
        allFinite = allFinite && finite(residual) && finite(gross);
        if (!allFinite) {
            verdict.findings.push_back(at(make(ValidationCode::BalanceNotFinite,
                                               std::format("a heat rate, a total or the residual of balance state {} is not finite", s))));
            continue;
        }

        const double normalized = state.normalizedResidual();
        if (finite(normalized) && (!verdict.worstResidual.has_value() || normalized > *verdict.worstResidual)) {
            verdict.worstResidual = normalized;
            verdict.worstStep = step;
            verdict.worstTime = time;
        }
        const bool within = balanceWithinTolerance(state.residual(), state.grossScale(), state.arithmeticScale);
        if (!within) {
            const double allowed = kBalanceAbsoluteToleranceW + kBalanceRelativeTolerance * (gross + state.arithmeticScale.si());
            ValidationFinding finding = make(ValidationCode::BalanceOutOfTolerance,
                                             std::format("the heat balance residual {} W of state {} exceeds the policy {} W", number(std::abs(residual)), s,
                                                         number(allowed)));
            finding.value = std::abs(residual);
            finding.threshold = allowed;
            finding.unit = "W";
            finding.cause = "recomputed";
            verdict.findings.push_back(at(std::move(finding)));
        } else if (state.status != BalanceStatus::Balanced) {
            // The numbers are within the policy but the state records otherwise: a result that disagrees with itself is not accepted.
            ValidationFinding finding = make(ValidationCode::BalanceOutOfTolerance,
                                             std::format("balance state {} records itself as out of tolerance", s));
            finding.value = std::abs(residual);
            finding.unit = "W";
            finding.cause = "recorded_status";
            verdict.findings.push_back(at(std::move(finding)));
        }
    }
    return verdict;
}

std::vector<ValidationFinding> checkBalanceAttribution(std::span<const ThermalHeatBalance> states, std::size_t loads,
                                                       std::size_t convections, std::size_t reactions) {
    std::vector<ValidationFinding> findings;
    for (std::size_t s = 0; s < states.size(); ++s) {
        const ThermalHeatBalance& state = states[s];
        const std::size_t haveLoads = state.surfaceLoads.size() + state.generationLoads.size();
        if (haveLoads == loads && state.convections.size() == convections && state.reactions.size() == reactions) {
            continue;
        }
        ValidationFinding finding = make(
            ValidationCode::BalanceAttributionMismatch,
            std::format("balance state {} accounts for {} load(s), {} convection condition(s) and {} reaction(s); the system was made from {}, {} and {}",
                        s, haveLoads, state.convections.size(), state.reactions.size(), loads, convections, reactions));
        finding.step = state.step;
        if (state.time.has_value()) {
            finding.time = state.time->si();
        }
        if (state.stepSize.has_value()) {
            finding.stepSize = state.stepSize->si();
        }
        findings.push_back(std::move(finding));
    }
    return findings;
}
// ---------------------------------------------------------------------------
// The system seams
// ---------------------------------------------------------------------------

std::vector<ValidationFinding> checkSteadyWellPosedness(const ConductanceMatrix& conductance, const ThermalHeatVector& heat,
                                                        const ThermalFreeEquationMap& equations,
                                                        const ThermalConstraintMap& constraints) {
    std::vector<ValidationFinding> findings;
    // Nothing free: every unknown is prescribed, T = Tc is the solution and there is nothing to anchor.
    if (equations.freeCount() == 0) {
        return findings;
    }
    const std::expected<ReducedThermalSystem, SteadySolveFailure> reduced =
        reduceThermalSystem(conductance, heat, equations, constraints);
    if (!reduced.has_value()) {
        ValidationFinding finding = make(residualCodeFor(reduced.error().problem) == ValidationCode::SourceMismatch
                                             ? ValidationCode::SourceMismatch
                                             : ValidationCode::SystemNotFinite,
                                         reduced.error().error.message);
        finding.cause = toString(reduced.error().problem);
        findings.push_back(std::move(finding));
        return findings;
    }
    const AnchorAnalysis analysis = analyseAnchoring(*reduced);
    if (analysis.unanchored == 0) {
        return findings;
    }
    if (analysis.anchored == 0) {
        ValidationFinding finding = make(ValidationCode::NoTemperatureDatum,
                                         std::format("no part of the model is anchored to an absolute temperature: {} independent region(s), none "
                                                     "with a prescribed temperature or a positive convection coefficient",
                                                     analysis.regions));
        finding.count = analysis.regions;
        findings.push_back(std::move(finding));
    } else {
        ValidationFinding finding = make(ValidationCode::UnanchoredRegion,
                                         std::format("{} of {} independent region(s) are not anchored to an absolute temperature", analysis.unanchored,
                                                     analysis.regions));
        finding.count = analysis.unanchored;
        findings.push_back(std::move(finding));
    }
    return findings;
}

std::vector<ValidationFinding> checkTransientWellPosedness(const ConductanceMatrix& conductance,
                                                           const std::optional<CapacityMatrix>& capacity,
                                                           const ThermalConstraintMap& constraints, std::size_t convectionFacets) {
    std::vector<ValidationFinding> findings;
    if (!capacity.has_value()) {
        findings.push_back(make(ValidationCode::CapacityMissing, "a transient analysis needs a heat capacity matrix; the system has none"));
        return findings;
    }
    if (capacity->rows() != conductance.rows()) {
        findings.push_back(make(ValidationCode::SourceMismatch, std::format("the capacity has {} rows; the conductance has {}", capacity->rows(),
                                                                          conductance.rows())));
        return findings;
    }
    std::size_t bad = 0;
    std::optional<std::size_t> first;
    for (std::size_t i = 0; i < capacity->rows(); ++i) {
        const double diagonal = capacity->coeffSi(i, i);
        if (!finite(diagonal) || !(diagonal > 0.0)) {
            ++bad;
            if (!first.has_value()) {
                first = i;
            }
        }
    }
    if (bad > 0) {
        ValidationFinding finding =
            make(ValidationCode::CapacityInvalid,
                 std::format("{} diagonal capacity entr{} not finite and positive; the first is unknown {}", bad, bad == 1 ? "y is" : "ies are",
                             *first));
        finding.count = bad;
        findings.push_back(std::move(finding));
        return findings;
    }
    // NO DATUM IS REQUIRED. The steady constant-temperature null mode is removed by C / dt, so a pure-Neumann body is well posed.
    if (constraints.isEmpty() && convectionFacets == 0) {
        findings.push_back(make(ValidationCode::TransientWithoutDatum,
                                "no prescribed temperature and no convection: the body is insulated, and well posed by its capacity and initial "
                                "temperature"));
    }
    return findings;
}

std::vector<ValidationFinding> checkTransientSetup(const UniformInitialTemperature& initial, const TransientSettings& transient,
                                                   std::size_t nodeCount) {
    std::vector<ValidationFinding> findings;
    if (!isAbsoluteTemperature(initial.value)) {
        ValidationFinding finding = make(ValidationCode::InitialTemperatureInvalid,
                                         std::format("the initial temperature {} K is not a finite absolute temperature", initial.value.si()));
        finding.value = initial.value.si();
        finding.unit = "K";
        findings.push_back(std::move(finding));
    }
    if (const Result<void> valid = validate(transient); !valid.has_value()) {
        findings.push_back(make(ValidationCode::TimeSettingsInvalid, valid.error().message));
        return findings;
    }
    const std::expected<TimeGrid, TransientFailure> grid = TimeGrid::create(transient.timeStep.si(), transient.endTime.si());
    if (!grid.has_value()) {
        ValidationFinding finding = make(ValidationCode::TimeSettingsInvalid, grid.error().error.message);
        finding.cause = toString(grid.error().problem);
        findings.push_back(std::move(finding));
        return findings;
    }
    const std::size_t samples = transient.output == TransientOutputPolicy::EveryStep ? grid->stepCount() + 1 : 1;
    if (nodeCount != 0 && samples > kMaxStoredValues / nodeCount) {
        ValidationFinding finding = make(ValidationCode::TimeSettingsInvalid,
                                         std::format("the history of {} state(s) of {} unknowns would exceed the storage guard of {} values", samples,
                                                     nodeCount, kMaxStoredValues));
        finding.cause = "history_too_large";
        finding.value = static_cast<double>(samples) * static_cast<double>(nodeCount);
        finding.threshold = static_cast<double>(kMaxStoredValues);
        findings.push_back(std::move(finding));
    }
    return findings;
}

// ---------------------------------------------------------------------------
// The entry point
// ---------------------------------------------------------------------------

namespace {

[[nodiscard]] bool ruleFailedOrSkipped(const ThermalValidationReport& report, ValidationRule rule) {
    const CheckStatus status = report.statusOf(rule);
    return status == CheckStatus::Fail || status == CheckStatus::Skipped;
}

/// The first rule in [from, to] that failed or was skipped.
[[nodiscard]] std::optional<ValidationRule> firstBlocker(const ThermalValidationReport& report, ValidationRule from, ValidationRule to) {
    for (std::size_t i = static_cast<std::size_t>(from); i <= static_cast<std::size_t>(to); ++i) {
        const auto rule = static_cast<ValidationRule>(i);
        if (ruleFailedOrSkipped(report, rule)) {
            return rule;
        }
    }
    return std::nullopt;
}

void finishFindings(ThermalValidationReport& report) {
    const auto severityRank = [](ValidationSeverity s) { return s == ValidationSeverity::Error ? 0 : (s == ValidationSeverity::Warning ? 1 : 2); };
    std::ranges::stable_sort(report.findings, [&](const ValidationFinding& a, const ValidationFinding& b) {
        return std::tuple{static_cast<int>(a.rule), severityRank(a.severity), static_cast<int>(a.code),
                          a.element.has_value() ? a.element->value() : 0U, a.step.value_or(0)} <
               std::tuple{static_cast<int>(b.rule), severityRank(b.severity), static_cast<int>(b.code),
                          b.element.has_value() ? b.element->value() : 0U, b.step.value_or(0)};
    });
}

/// The temperature extremes of the final state, INFORMATION only.
void noteTemperatureExtremes(ThermalValidationMetrics& metrics, std::span<const Temperature> state) {
    if (state.empty()) {
        return;
    }
    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    for (const Temperature t : state) {
        if (!finite(t.si())) {
            return;
        }
        lo = std::min(lo, t.si());
        hi = std::max(hi, t.si());
    }
    metrics.minimumTemperature = lo;
    metrics.maximumTemperature = hi;
}

} // namespace

void judgeSolveEvidence(const ThermalSolveEvidence& e, ThermalValidationReport& report) {
    const auto evaluate = [&report](ValidationRule rule) {
        ThermalCheck& check = report.checks[static_cast<std::size_t>(rule)];
        check.state = CheckState::Evaluated;
        check.blockedBy.reset();
    };
    const auto skipFrom = [&report](ValidationRule from, ValidationRule by) {
        for (std::size_t i = static_cast<std::size_t>(from); i < kValidationRuleCount; ++i) {
            ThermalCheck& check = report.checks[i];
            check.state = CheckState::Skipped;
            check.blockedBy = by;
        }
    };
    const auto add = [&report](ValidationFinding finding) { report.findings.push_back(std::move(finding)); };
    const auto addAll = [&add](std::vector<ValidationFinding> findings) {
        for (ValidationFinding& finding : findings) {
            add(std::move(finding));
        }
    };

    // ---- V10: the solve, judged by its residual and not by its status
    evaluate(ValidationRule::SolverResidual);
    report.metrics.dofCount = e.dofCount;
    report.metrics.freeEquations = e.equations->freeCount();
    report.metrics.constrainedCount = e.constraints->size();
    report.metrics.residualTolerance = e.settings.relativeResidualTolerance;
    if (e.lastSolveFailure != nullptr) {
        ValidationFinding finding = make(ValidationCode::SolveFailed, "the last solve attempt failed: " + e.lastSolveFailure->message);
        finding.subject = ValidationSubject::Solution;
        add(std::move(finding));
        skipFrom(ValidationRule::ResultFinite, ValidationRule::SolverResidual);
        return;
    }
    if (e.states.empty()) {
        ValidationFinding finding = make(ValidationCode::SolutionMissing, "no solution was presented");
        finding.subject = ValidationSubject::Solution;
        add(std::move(finding));
        skipFrom(ValidationRule::ResultFinite, ValidationRule::SolverResidual);
        return;
    }

    const bool transient = e.mode == ThermalAnalysisMode::Transient;
    if (!transient) {
        ResidualVerdict verdict =
            checkSteadyResidual(*e.conductance, *e.heat, *e.equations, *e.constraints, e.states.back(), e.settings, e.pivotRatio);
        report.metrics.steadyResidual = verdict.residual;
        addAll(std::move(verdict.findings));
    } else {
        const TimeGrid& grid = *e.grid;
        report.metrics.transientStepCount = grid.stepCount();
        // The history contract of its time grid: how many states, at which times, ending EXACTLY at t_end.
        const bool every = e.output == TransientOutputPolicy::EveryStep;
        const std::size_t expectedSamples = every ? grid.stepCount() + 1 : 1;
        bool contract = e.states.size() == expectedSamples && e.times.size() == e.states.size();
        for (std::size_t i = 0; contract && i < e.states.size(); ++i) {
            const double expectedTime = every ? grid.timeAt(i) : grid.endTime();
            contract = e.times[i] == expectedTime;
        }
        if (!contract) {
            add(make(ValidationCode::HistoryContractViolated,
                     std::format("the history holds {} state(s) at the wrong times for its time grid ({} step(s), output {})", e.states.size(),
                                 grid.stepCount(), toString(e.output))));
        }
        if (e.initialOverrides > 0) {
            ValidationFinding finding = make(ValidationCode::InitialConditionOverriddenByPrescription,
                                             std::format("{} unknown(s) had an initial temperature different from their prescription; the "
                                                         "prescription dominates at t = 0",
                                                         e.initialOverrides));
            finding.count = e.initialOverrides;
            add(std::move(finding));
        }
        TransientResidualVerdict verdict = checkTransientResiduals(*e.conductance, *e.capacity, *e.heat, *e.equations, *e.constraints, e.settings,
                                                                   grid, e.records, e.initial, e.states, e.output);
        report.metrics.worstResidualStep = verdict.worstStep;
        report.metrics.worstResidualTime = verdict.worstTime;
        report.metrics.worstResidualStepSize = verdict.worstStepSize;
        report.metrics.worstTransientResidual = verdict.worstResidual;
        addAll(std::move(verdict.findings));
    }

    // ---- V11: the temperatures are finite -- the primary arrays, before anything derived
    evaluate(ValidationRule::ResultFinite);
    addAll(checkTemperatureStates(e.states, e.dofCount, e.times));
    if (transient && e.states.size() == 1) {
        const std::array<std::span<const Temperature>, 1> initial{e.initial};
        addAll(checkTemperatureStates(initial, e.dofCount));
    }
    if (!ruleFailedOrSkipped(report, ValidationRule::ResultFinite)) {
        noteTemperatureExtremes(report.metrics, e.states.back());
    }

    // ---- V12: the recovered fields
    evaluate(ValidationRule::PostResult);
    if (!e.post.has_value()) {
        ValidationFinding finding = make(ValidationCode::PostMissing, "no recovered temperature gradient and heat flux were presented");
        finding.subject = ValidationSubject::PostResult;
        add(std::move(finding));
    } else {
        addAll(checkPostStates(*e.post, e.tetCount, e.dofCount, e.states));
        // The informational maximum is computed only over fields that passed: no statistic is formed over a number that is not a number.
        if (!ruleFailedOrSkipped(report, ValidationRule::PostResult) && !e.post->empty()) {
            double largest = 0.0;
            bool ok = true;
            for (const HeatFlux q : e.post->back().fields.heatFluxMagnitudes) {
                ok = ok && finite(q.si());
                largest = std::max(largest, q.si());
            }
            if (ok && !e.post->back().fields.heatFluxMagnitudes.empty()) {
                report.metrics.maximumHeatFlux = largest;
            }
        }
    }

    // ---- V13: the heat balance
    evaluate(ValidationRule::HeatBalance);
    if (!e.balance.has_value()) {
        std::string text = "no heat balance was presented";
        if (transient && e.output == TransientOutputPolicy::FinalOnly && e.grid->stepCount() > 1) {
            text += "; a FinalOnly history of several steps stores no intermediate state and cannot be balanced step by step: request EveryStep";
        }
        ValidationFinding finding = make(ValidationCode::BalanceMissing, std::move(text));
        finding.subject = ValidationSubject::BalanceResult;
        add(std::move(finding));
    } else {
        const std::size_t expectedStates = transient ? e.grid->stepCount() : 1;
        BalanceVerdict verdict = checkHeatBalanceStates(*e.balance, expectedStates);
        report.metrics.worstBalanceStep = verdict.worstStep;
        report.metrics.worstBalanceTime = verdict.worstTime;
        report.metrics.worstBalanceResidual = verdict.worstResidual;
        addAll(std::move(verdict.findings));
        // The attribution: one entry per prepared load, per convection condition and per prescribed unknown.
        if (e.balanceCategories.has_value()) {
            addAll(checkBalanceAttribution(*e.balance, (*e.balanceCategories)[0], (*e.balanceCategories)[1], (*e.balanceCategories)[2]));
        }
    }
}
ThermalValidationReport validateThermalAnalysis(const Document& document, const features::Regenerator& regenerator,
                                                const meshing::Mesher& mesher, ThermalAnalysisId analysis,
                                                const ThermalDerivedState& derived) {
    ThermalValidationReport report;
    report.analysis = analysis;
    report.policyFingerprint = thermalValidationPolicyFingerprint();
    for (const ValidationRule rule : kValidationRules) {
        report.checks.push_back(ThermalCheck{.rule = rule, .state = CheckState::Skipped, .blockedBy = std::nullopt});
    }
    const auto check = [&report](ValidationRule rule) -> ThermalCheck& { return report.checks[static_cast<std::size_t>(rule)]; };
    const auto evaluate = [&check](ValidationRule rule) {
        check(rule).state = CheckState::Evaluated;
        check(rule).blockedBy.reset();
    };
    const auto skip = [&check](ValidationRule rule, ValidationRule by) {
        check(rule).state = CheckState::Skipped;
        check(rule).blockedBy = by;
    };
    const auto notApplicable = [&check](ValidationRule rule) {
        check(rule).state = CheckState::NotApplicable;
        check(rule).blockedBy.reset();
    };
    const auto add = [&report](ValidationFinding finding) { report.findings.push_back(std::move(finding)); };
    const auto addAll = [&add](std::vector<ValidationFinding> findings) {
        for (ValidationFinding& finding : findings) {
            add(std::move(finding));
        }
    };
    const auto skipFrom = [&](ValidationRule from, ValidationRule by) {
        for (std::size_t i = static_cast<std::size_t>(from); i < kValidationRuleCount; ++i) {
            skip(static_cast<ValidationRule>(i), by);
        }
    };
    const auto conclude = [&report]() {
        finishFindings(report);
        return std::move(report);
    };

    // ---- V00: the analysis exists
    evaluate(ValidationRule::AnalysisDefinition);
    const ThermalAnalysis* object = findThermalAnalysis(document, analysis);
    if (object == nullptr) {
        add(make(ValidationCode::AnalysisNotFound, std::format("the document has no thermal analysis {}", analysis.value())));
        skipFrom(ValidationRule::GeometryCurrent, ValidationRule::AnalysisDefinition);
        return conclude();
    }
    // ONE COHERENT SNAPSHOT of the intent: read once, judged throughout.
    const ThermalAnalysisDefinition definition = object->definition();
    const ThermalAnalysisMode mode = definition.mode;
    report.mode = mode;
    const std::span<const ThermalLoad> loads{definition.loads};
    const std::span<const ThermalBoundaryCondition> conditions{definition.boundaryConditions};

    // The source this report describes: what a result computed now would carry. Absent when the inputs do not resolve.
    if (const Result<ThermalResultSource> source = currentResultSource(document, regenerator, mesher, analysis); source.has_value()) {
        report.source = *source;
    }

    // ---- V01, V02: geometry and mesh currentness (delegated to P16 and ADR-051)
    Result<ThermalModel> modelResult = requireThermalModel(document, regenerator, mesher, definition.mesh);
    std::optional<InputProblem> problem;
    if (!modelResult.has_value()) {
        problem = thermalInputProblem(document, regenerator, mesher, definition.mesh);
    }
    const bool geometryBad = problem == InputProblem::ControlNotFound || problem == InputProblem::GeometryIneligible;
    const bool meshBad = problem == InputProblem::NoMesh || problem == InputProblem::MeshStale ||
                         problem == InputProblem::MeshGenerationFailed;
    evaluate(ValidationRule::GeometryCurrent);
    if (geometryBad) {
        add(make(*problem == InputProblem::ControlNotFound ? ValidationCode::ControlNotFound : ValidationCode::GeometryIneligible,
                 modelResult.error().message));
        report.findings.back().cause = toString(*problem);
        skip(ValidationRule::MeshCurrent, ValidationRule::GeometryCurrent);
    } else {
        evaluate(ValidationRule::MeshCurrent);
        if (meshBad) {
            const ValidationCode code = *problem == InputProblem::NoMesh
                                            ? ValidationCode::MeshMissing
                                            : (*problem == InputProblem::MeshGenerationFailed ? ValidationCode::MeshGenerationFailed
                                                                                              : ValidationCode::MeshStale);
            add(make(code, modelResult.error().message));
            report.findings.back().cause = toString(*problem);
        }
    }

    // ---- V03, V04: P16's measurements, judged under the thermal policy (needs a current mesh, not a material)
    if (ruleFailedOrSkipped(report, ValidationRule::MeshCurrent)) {
        const ValidationRule by = geometryBad ? ValidationRule::GeometryCurrent : ValidationRule::MeshCurrent;
        skip(ValidationRule::MeshStructural, by);
        skip(ValidationRule::MeshQuality, by);
    } else {
        const meshing::MeshQualityReport* quality = mesher.quality(definition.mesh);
        // Current implies a held report (P16's invariant, checked by requireThermalModel); a null here would be a bug upstream.
        evaluate(ValidationRule::MeshStructural);
        if (quality == nullptr) {
            add(make(ValidationCode::MeshMissing, "the mesh is current but holds no quality report"));
            skip(ValidationRule::MeshQuality, ValidationRule::MeshStructural);
        } else {
            ThermalMeshVerdict verdict = classifyThermalMesh(*quality);
            report.metrics.tetCount = quality->tetCount;
            report.worstByMetric = std::move(verdict.worstByMetric);
            addAll(std::move(verdict.structural));
            if (ruleFailedOrSkipped(report, ValidationRule::MeshStructural)) {
                skip(ValidationRule::MeshQuality, ValidationRule::MeshStructural);
            } else {
                evaluate(ValidationRule::MeshQuality);
                addAll(std::move(verdict.quality));
            }
        }
    }

    // ---- V05: the material, FOR THIS MODE (P15's requirement table, through P18-MAT-001)
    const meshing::MeshControl* control = meshing::findMeshControl(document, definition.mesh);
    if (geometryBad || control == nullptr) {
        skip(ValidationRule::Material, ValidationRule::GeometryCurrent);
    } else {
        evaluate(ValidationRule::Material);
        const ObjectId body = control->definition().body;
        if (const std::optional<MaterialProblem> materialProblem = thermalMaterialProblem(document, body, mode)) {
            const Result<ThermalMaterial> resolved = resolveThermalMaterial(document, body, mode);
            const std::string text = resolved.has_value() ? std::string{} : resolved.error().message;
            if (*materialProblem == MaterialProblem::RequiredPropertyUnavailable) {
                // Is it the conductivity every mode needs, or the density and specific heat only a transient needs?
                const bool conductivityMissing = mode == ThermalAnalysisMode::Steady
                                                     ? true
                                                     : thermalMaterialProblem(document, body, ThermalAnalysisMode::Steady).has_value();
                ValidationFinding finding = make(conductivityMissing ? ValidationCode::MaterialConductivityUnusable
                                                                     : ValidationCode::MaterialIncompleteForMode,
                                                 text);
                finding.cause = toString(*materialProblem);
                add(std::move(finding));
            } else {
                ValidationFinding finding = make(ValidationCode::MaterialMissing, text);
                finding.cause = toString(*materialProblem);
                add(std::move(finding));
            }
        }
    }

    // ---- V06, V07: loads and boundary conditions, on the current model
    std::optional<ThermalDofMap> currentMap;
    std::optional<PreparedThermalLoads> freshLoads;
    if (modelResult.has_value()) {
        if (Result<ThermalDofMap> built = buildThermalDofMap(modelResult->mesh().mesh()); built.has_value()) {
            currentMap = std::move(*built);
        }
    }
    if (!modelResult.has_value() || !currentMap.has_value()) {
        const ValidationRule by = geometryBad ? ValidationRule::GeometryCurrent
                                              : (meshBad ? ValidationRule::MeshCurrent : ValidationRule::Material);
        skip(ValidationRule::Loads, by);
        skip(ValidationRule::BoundaryConditions, by);
    } else {
        const ThermalModel& model = *modelResult;
        report.metrics.dofCount = static_cast<std::size_t>(currentMap->dofCount());

        evaluate(ValidationRule::Loads);
        Result<PreparedThermalLoads> prepared = prepareThermalLoads(model, *currentMap, loads);
        if (prepared.has_value()) {
            freshLoads = std::move(*prepared);
        }
        if (!freshLoads.has_value()) {
            ValidationFinding finding = make(ValidationCode::LoadsInvalid, prepared.error().message);
            if (const std::optional<LoadProblem> why = thermalLoadProblem(model, *currentMap, loads)) {
                finding.cause = toString(*why);
            }
            add(std::move(finding));
        } else if (loads.empty()) {
            add(make(ValidationCode::NoLoadsConfigured, "the analysis applies no heat load"));
        }

        evaluate(ValidationRule::BoundaryConditions);
        if (const std::optional<BoundaryFailure> why = thermalBoundaryFailure(model, *currentMap, conditions)) {
            ValidationFinding finding = make(ValidationCode::BoundaryConditionsInvalid, why->error.message);
            finding.cause = toString(why->problem);
            add(std::move(finding));
        } else if (conditions.empty()) {
            add(make(ValidationCode::NoBoundaryConditionsConfigured, "the analysis has no boundary condition"));
        }
    }

    // ---- TV_TRANSIENT_SETUP
    if (mode == ThermalAnalysisMode::Steady) {
        notApplicable(ValidationRule::TransientSetup);
    } else {
        evaluate(ValidationRule::TransientSetup);
        if (definition.initialCondition.has_value() && definition.transient.has_value()) {
            const std::size_t nodes = currentMap.has_value() ? currentMap->nodeCount() : 0;
            addAll(checkTransientSetup(*definition.initialCondition, *definition.transient, nodes));
        } else {
            add(make(ValidationCode::TimeSettingsInvalid, "a transient analysis carries no initial condition or no time settings"));
        }
    }

    // ---- THE GATE: nothing derived is judged on inputs that are not fit
    if (const std::optional<ValidationRule> blocker =
            firstBlocker(report, ValidationRule::AnalysisDefinition, ValidationRule::TransientSetup)) {
        skipFrom(ValidationRule::SourceCompatibility, *blocker);
        return conclude();
    }
    const ThermalModel& model = *modelResult;
    const ThermalDofMap& map = *currentMap;
    const std::size_t dofCount = static_cast<std::size_t>(map.dofCount());
    const std::size_t tetCount = model.mesh().mesh().tetrahedra().size();
    const bool transientMode = mode == ThermalAnalysisMode::Transient;

    // ---- V08: every derived object describes THIS document and one another (identity, never sizes)
    evaluate(ValidationRule::SourceCompatibility);
    const auto stale = [&](ValidationSubject subject, const Result<void>& current) {
        if (!current.has_value()) {
            ValidationFinding finding = make(ValidationCode::SourceStale, current.error().message);
            finding.subject = subject;
            add(std::move(finding));
        }
    };
    const auto mismatch = [&](ValidationSubject subject, std::string text) {
        ValidationFinding finding = make(ValidationCode::SourceMismatch, std::move(text));
        finding.subject = subject;
        add(std::move(finding));
    };
    const auto wrongMode = [&](ValidationSubject subject, std::string text) {
        ValidationFinding finding = make(ValidationCode::ModeMismatch, std::move(text));
        finding.subject = subject;
        add(std::move(finding));
    };

    if (derived.dofMap != nullptr && !derived.dofMap->describes(model.mesh())) {
        stale(ValidationSubject::DofMap,
              makeError(ErrorCode::FailedPrecondition, "the numbering does not describe the current mesh (another generation, or another count)"));
    }
    if (derived.preparedLoads != nullptr) {
        stale(ValidationSubject::PreparedLoads, requireCurrent(*derived.preparedLoads, model, loads));
    }
    if (derived.preparedBoundary != nullptr) {
        stale(ValidationSubject::PreparedBoundary, requireCurrent(*derived.preparedBoundary, model, conditions));
    }
    if (derived.system != nullptr) {
        stale(ValidationSubject::System, requireCurrent(*derived.system, document, model, mode, loads, conditions));
        if (derived.preparedLoads != nullptr && !(derived.preparedLoads->source().loads == derived.system->source().loads)) {
            mismatch(ValidationSubject::PreparedLoads, "the prepared loads and the assembled system were made from different load lists");
        }
    }
    const SteadyThermalSolution* steady = derived.steady;
    const TransientThermalSolution* transient = derived.transient;
    if (!transientMode && transient != nullptr) {
        wrongMode(ValidationSubject::Solution, "a transient history was presented for a steady analysis");
        transient = nullptr;
    }
    if (transientMode && steady != nullptr) {
        wrongMode(ValidationSubject::Solution, "a steady solution was presented for a transient analysis");
        steady = nullptr;
    }
    std::optional<ThermalPostSource> solutionSource;
    if (steady != nullptr) {
        stale(ValidationSubject::Solution, requireCurrent(*steady, document, model, loads, conditions, definition.solver));
        solutionSource = postSourceOf(steady->source());
        if (derived.system != nullptr && !(steady->source().system == derived.system->source())) {
            mismatch(ValidationSubject::Solution, "the solution was not computed from the assembled system presented with it");
        }
    }
    if (transient != nullptr && definition.initialCondition.has_value() && definition.transient.has_value()) {
        stale(ValidationSubject::Solution, requireCurrent(*transient, document, model, loads, conditions, *definition.initialCondition,
                                                          *definition.transient, definition.solver));
        solutionSource = postSourceOf(transient->source());
        if (derived.system != nullptr && !(transient->source().system == derived.system->source())) {
            mismatch(ValidationSubject::Solution, "the history was not computed from the assembled system presented with it");
        }
    }
    report.resultSource = solutionSource;
    if (derived.post != nullptr) {
        if (derived.post->mode() != mode) {
            wrongMode(ValidationSubject::PostResult, "the recovered fields are of the other analysis mode");
        } else if (!transientMode) {
            stale(ValidationSubject::PostResult, requireCurrent(*derived.post, document, model, loads, conditions, definition.solver));
        } else if (definition.initialCondition.has_value() && definition.transient.has_value()) {
            stale(ValidationSubject::PostResult, requireCurrent(*derived.post, document, model, loads, conditions,
                                                                *definition.initialCondition, *definition.transient, definition.solver));
        }
        if (solutionSource.has_value() && !(derived.post->source() == *solutionSource)) {
            mismatch(ValidationSubject::PostResult, "the recovered fields were not made from the solution presented with them");
        }
    }
    if (derived.balance != nullptr) {
        if (derived.balance->mode() != mode) {
            wrongMode(ValidationSubject::BalanceResult, "the heat balance is of the other analysis mode");
        } else if (!transientMode) {
            stale(ValidationSubject::BalanceResult, requireCurrent(*derived.balance, document, model, loads, conditions, definition.solver));
        } else if (definition.initialCondition.has_value() && definition.transient.has_value()) {
            stale(ValidationSubject::BalanceResult, requireCurrent(*derived.balance, document, model, loads, conditions,
                                                                   *definition.initialCondition, *definition.transient, definition.solver));
        }
        if (solutionSource.has_value() && !(derived.balance->source() == *solutionSource)) {
            mismatch(ValidationSubject::BalanceResult, "the heat balance was not made from the solution presented with it");
        }
    }
    if (ruleFailedOrSkipped(report, ValidationRule::SourceCompatibility)) {
        skipFrom(ValidationRule::WellPosed, ValidationRule::SourceCompatibility);
        return conclude();
    }

    // ---- V09: well posed, by the rule of the MODE
    std::optional<PreparedThermalBoundary> freshBoundary;
    if (derived.preparedBoundary == nullptr) {
        if (Result<PreparedThermalBoundary> prepared = prepareThermalBoundary(model, map, conditions); prepared.has_value()) {
            freshBoundary = std::move(*prepared);
        }
    }
    const PreparedThermalBoundary* boundary = derived.preparedBoundary != nullptr ? derived.preparedBoundary
                                                                                  : (freshBoundary.has_value() ? &*freshBoundary : nullptr);
    const PreparedThermalLoads* loadsForChecks =
        derived.preparedLoads != nullptr ? derived.preparedLoads : (freshLoads.has_value() ? &*freshLoads : nullptr);
    const ThermalSystem* system = derived.system;
    std::optional<ThermalFreeEquationMap> equations;
    if (boundary != nullptr) {
        if (Result<ThermalFreeEquationMap> built = buildThermalFreeEquationMap(map, boundary->constraints()); built.has_value()) {
            equations = std::move(*built);
        }
    }
    evaluate(ValidationRule::WellPosed);
    if (system == nullptr) {
        add(make(ValidationCode::SystemMissing, "no assembled system was presented"));
    } else if (boundary == nullptr || !equations.has_value()) {
        add(make(ValidationCode::SystemMissing, "the constraints and the free numbering could not be formed for the system"));
    } else {
        report.metrics.freeEquations = equations->freeCount();
        report.metrics.constrainedCount = boundary->constraints().size();
        if (!transientMode) {
            if (system->capacity().has_value()) {
                wrongMode(ValidationSubject::System, "a steady analysis was presented a system that carries a capacity");
            } else {
                addAll(checkSteadyWellPosedness(system->conductance(), system->heat(), *equations, boundary->constraints()));
            }
        } else {
            addAll(checkTransientWellPosedness(system->conductance(), system->capacity(), boundary->constraints(),
                                               system->convectionFacetCount()));
        }
    }
    if (ruleFailedOrSkipped(report, ValidationRule::WellPosed)) {
        skipFrom(ValidationRule::SolverResidual, ValidationRule::WellPosed);
        return conclude();
    }

    // ---- V10 .. V13: the solve, the temperatures, the recovered fields and the heat balance, judged from plain evidence
    ThermalSolveEvidence evidence;
    evidence.mode = mode;
    evidence.settings = definition.solver;
    evidence.conductance = &system->conductance();
    evidence.capacity = system->capacity().has_value() ? &*system->capacity() : nullptr;
    evidence.heat = &system->heat();
    evidence.equations = &*equations;
    evidence.constraints = &boundary->constraints();
    evidence.dofCount = dofCount;
    evidence.tetCount = tetCount;
    evidence.lastSolveFailure = derived.lastSolveFailure;
    if (steady != nullptr) {
        evidence.states.push_back(steady->temperatures());
        evidence.pivotRatio = steady->pivotRatio();
    } else if (transient != nullptr) {
        for (std::size_t i = 0; i < transient->sampleCount(); ++i) {
            evidence.states.push_back(transient->temperaturesAt(i));
            evidence.times.push_back(transient->timeOf(i).si());
        }
        evidence.initial = transient->initialTemperatures();
        evidence.initialOverrides = transient->initialOverrides();
        evidence.grid = &transient->grid();
        evidence.records = transient->steps();
        evidence.output = definition.transient->output;
    }
    if (derived.post != nullptr) {
        evidence.post = derived.post->states();
    }
    if (derived.balance != nullptr) {
        evidence.balance = derived.balance->states();
        if (loadsForChecks != nullptr) {
            evidence.balanceCategories = std::array<std::size_t, 3>{loadsForChecks->contributions().size(), boundary->convections().size(),
                                                                    boundary->constraints().size()};
        }
    }
    judgeSolveEvidence(evidence, report);
    return conclude();
}

bool isCurrent(const ThermalValidationReport& report, const Document& document, const features::Regenerator& regenerator,
               const meshing::Mesher& mesher) {
    if (!report.analysis.has_value() || !report.source.has_value()) {
        return false;
    }
    // A report is current only while the policy it was produced under is the policy in force.
    if (report.policyFingerprint != thermalValidationPolicyFingerprint()) {
        return false;
    }
    const Result<ThermalResultSource> current = currentResultSource(document, regenerator, mesher, *report.analysis);
    return current.has_value() && staleReasons(*report.source, *current).empty();
}

} // namespace bettercad::thermal
