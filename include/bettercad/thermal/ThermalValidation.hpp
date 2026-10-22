#pragma once

// P18-VALID-001 -- the ONE place a thermal result is accepted or refused.
//
// WHAT THIS FILE OWNS. Every earlier P18 milestone decides one thing about its own output: the assembler that its
// matrices are consistent, the solver that its residual is within the tolerance the user set, the recovery that its fields
// are finite, the balance that its books close. NONE of them decides whether a thermal ANALYSIS may be believed, because a
// solver returning Success is not that: a result can be algebraically perfect and be about a mesh the user has since
// replaced, about a material the user has since edited, or about a model that was never well posed. This milestone is the
// single owner of that decision. The solver, the recovery, the balance, a GUI and a CLI each consume it; none of them keeps a
// second policy.
//
// P16 REPORTS, P18-VALID DECIDES. `meshing::evaluateMeshQuality` measures a mesh and states no opinion
// (`reportOnlyThresholds()` classifies nothing). Whether a measured mesh is good enough for HEAT CONDUCTION is this file's
// to say, and the numbers it says it with are below, with the measurement they came from. No metric is recomputed here:
// `MeshQualityReport::summaries` is read, and a second radius-ratio formula in this module is the defect the module
// boundary exists to prevent.
//
// THE TWO NUMBERS THAT REFUSE A MESH ARE DERIVED, NOT CHOSEN. A tetrahedron degenerates in two independent ways -- toward a
// plane (a wedge with its apex brought down) and toward a line (a needle with its base shrunk) -- and neither shape metric
// sees the other's family. The gradient a Tet4 recovers from nodal temperatures loses relative accuracy as it degrades, and
// the loss was MEASURED through the thermal production path (`tests/thermal/ThermalValidationLawTests.cpp`,
// `docs/verification/P18-VALID-001/MESH_ACCEPTANCE.md`):
//
//     FLATTENING   relative gradient error  <=  C / sqrt(3r/R)    C <= 8.62e-13   over 21 orders of 3r/R
//     STRETCHING   relative gradient error  <=  C * aspect        C <= 4.28e-13   over 7 orders of aspect ratio
//
// at the reference condition that matters for a thermal model and not for a structural one: TEMPERATURES ARE ABSOLUTE. A
// field of 14 K/m on a 307 K level changes by 0.14 K across a 10 mm element, so the nodal temperatures carry the rounding of
// a 307 K number, not of a 0.14 K one, and the SHAPE of the element multiplies that. Inverting each law at the repository's
// documented 1e-9 band for geometric accumulation and keeping a factor of three:
//
//     3r/R   >= 7.0e-6      (crossing 7.4e-7, error at the bound 3.3e-10)
//     aspect <= 700         (crossing 2.3e+3, error at the bound 3.0e-10)
//
// Each is far outside the qualified P16 reference meshes (worst 3r/R 3.22641e-04 and aspect 80.0062), so neither refuses
// anything BetterCAD produces; and the dihedral angles carry no bound at all, because no law was measured against them.
//
// WHAT IT NEVER DOES. Validation observes. It never solves, assembles, remeshes, repairs, takes an absolute value, picks a
// nearest face, supplies a material property, loosens a tolerance or runs a second formula for a quantity another milestone
// owns. Every entry point takes const references and returns a report.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/meshing/MeshQuality.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/thermal/Export.hpp>
#include <bettercad/thermal/ThermalAnalysisObject.hpp>
#include <bettercad/thermal/ThermalBalance.hpp>
#include <bettercad/thermal/ThermalBoundary.hpp>
#include <bettercad/thermal/ThermalData.hpp>
#include <bettercad/thermal/ThermalDof.hpp>
#include <bettercad/thermal/ThermalLoadVector.hpp>
#include <bettercad/thermal/ThermalPost.hpp>
#include <bettercad/thermal/ThermalResult.hpp>
#include <bettercad/thermal/ThermalSolve.hpp>
#include <bettercad/thermal/ThermalSystem.hpp>
#include <bettercad/thermal/ThermalTransient.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bettercad::features {
class Regenerator;
} // namespace bettercad::features

namespace bettercad::thermal {

// ---------------------------------------------------------------------------
// The mesh policy
// ---------------------------------------------------------------------------

/// The radius ratio below which an element is refused. DERIVED, and the derivation is in the file comment and in
/// `docs/verification/P18-VALID-001/MESH_ACCEPTANCE.md`.
///
/// ```text
/// err <= C / sqrt(3r/R)      C <= 8.62e-13 measured at the reference condition
/// err <= 1e-9                the repository's band for geometric accumulation
///   =>  3r/R >= 7.4e-7       the crossing
/// 7.0e-6                     chosen: the error at the bound is C/sqrt(7e-6) = 3.3e-10, a factor of three inside
/// ```
///
/// NO EXPORT MACRO, and it must not have one: a `BETTERCAD_*_EXPORT` on a header-defined `constexpr` expands to `dllimport`
/// and fails in a shared build only.
inline constexpr double kThermalRadiusRatioFloor = 7.0e-6;

/// The aspect ratio above which an element is refused. The second half of the same guarantee, and NOT redundant with the
/// floor: a flattening element's aspect ratio saturates at sqrt(3) and sees nothing, while a stretching element's radius
/// ratio understates the damage.
///
/// ```text
/// err <= C * aspect          C <= 4.28e-13 measured at the reference condition
/// err <= 1e-9                the same band
///   =>  aspect <= 2.3e+3     the crossing
/// 700                        chosen: the error at the bound is C * 700 = 3.0e-10, a factor of three inside
/// ```
inline constexpr double kThermalAspectRatioCeiling = 700.0;

/// The recovered-gradient accuracy the two bounds above guarantee, relative, at the reference condition.
inline constexpr double kThermalGradientAccuracy = 1.0e-9;

/// BetterCAD's thermal mesh policy: TWO hard failures (one per degeneration family) and four envelope WARNINGS.
///
/// ```text
/// metric                 warning        failure      qualified worst
/// TetRadiusRatio         3.0e-4         7.0e-6       0.000322641   RM-MESH-05
/// TetAspectRatio         81.0           700          80.0062       RM-MESH-05
/// TetMinDihedralAngle    6.0e-3 rad     none         0.006330012   RM-MESH-02
/// TetMaxDihedralAngle    3.125 rad      none         3.119060453   RM-MESH-05
/// ```
///
/// Each warning bound is the worst value any qualified P16 reference model exhibits, rounded AWAY from the qualified set, so
/// no qualified mesh warns and the comparison cannot turn on a last-bit difference. A warning says exactly this: the element
/// is outside the range of every mesh BetterCAD has qualified. It is an envelope, not an engineering judgement.
///
/// Comparisons are STRICT in P16's direction (`value == bound` is on the good side), angles are radians.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT meshing::QualityThresholds thermalQualityThresholds();

// ---------------------------------------------------------------------------
// The rules, in the order they run
// ---------------------------------------------------------------------------

/// A validation rule. THE ENUMERATION ORDER IS THE EXECUTION ORDER and the report's order, so it is part of the contract: the
/// stages depend on one another, and an unordered traversal would make the first reported cause depend on a hash.
///
/// ```text
/// brief   rule                     stable id
/// V00     AnalysisDefinition       TV_ANALYSIS_DEFINITION
/// V01     GeometryCurrent          TV_GEOMETRY_CURRENT
/// V02     MeshCurrent              TV_MESH_CURRENT
/// V03     MeshStructural           TV_MESH_STRUCTURAL
/// V04     MeshQuality              TV_MESH_QUALITY
/// V05     Material                 TV_MATERIAL
/// V06     Loads                    TV_LOADS
/// V07     BoundaryConditions       TV_BCS
///         TransientSetup           TV_TRANSIENT_SETUP      (not applicable to a steady analysis)
/// V08     SourceCompatibility      TV_SOURCE_COMPAT
/// V09     WellPosed                TV_WELL_POSED
/// V10     SolverResidual           TV_SOLVER_RESIDUAL
/// V11     ResultFinite             TV_RESULT_FINITE
/// V12     PostResult               TV_POST
/// V13     HeatBalance              TV_BALANCE
/// V14     the final decision       DERIVED: `ThermalValidationReport::status()`, never a stored rule
/// ```
enum class ValidationRule : std::uint8_t {
    AnalysisDefinition,
    GeometryCurrent,
    MeshCurrent,
    MeshStructural,
    MeshQuality,
    Material,
    Loads,
    BoundaryConditions,
    TransientSetup,
    SourceCompatibility,
    WellPosed,
    SolverResidual,
    ResultFinite,
    PostResult,
    HeatBalance,
};

inline constexpr std::size_t kValidationRuleCount = 15;

/// Every rule, in order.
inline constexpr std::array<ValidationRule, kValidationRuleCount> kValidationRules{
    ValidationRule::AnalysisDefinition, ValidationRule::GeometryCurrent,    ValidationRule::MeshCurrent,
    ValidationRule::MeshStructural,     ValidationRule::MeshQuality,        ValidationRule::Material,
    ValidationRule::Loads,              ValidationRule::BoundaryConditions, ValidationRule::TransientSetup,
    ValidationRule::SourceCompatibility, ValidationRule::WellPosed,         ValidationRule::SolverResidual,
    ValidationRule::ResultFinite,       ValidationRule::PostResult,         ValidationRule::HeatBalance};

/// The stable identifier of @p rule, for a CLI to print and a script to compare: `TV_...`.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(ValidationRule rule) noexcept;

/// What a rule is about, so a reader can tell an input failure from a result failure.
enum class ValidationGroup : std::uint8_t {
    /// The analysis, the geometry, the mesh, the material, the loads, the conditions and the time settings.
    Input,
    /// The assembled system and the compatibility of everything derived.
    System,
    /// The solve and its residual.
    Solve,
    /// The temperatures and the recovered fields.
    Result,
    /// The global heat accounting.
    Balance,
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(ValidationGroup group) noexcept;

/// The group @p rule belongs to.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT ValidationGroup groupOf(ValidationRule rule) noexcept;

// ---------------------------------------------------------------------------
// The findings
// ---------------------------------------------------------------------------

/// What a finding is about. EVERY VALUE IS REACHABLE AND TESTED: the list was trimmed to that.
enum class ValidationCode : std::uint8_t {
    // --- TV_ANALYSIS_DEFINITION
    /// The document has no thermal analysis with that id. (A definition that does not validate cannot be in a document:
    /// `ThermalAnalysis::create` and `setDefinition` refuse it, so that check is not repeated here.)
    AnalysisNotFound,
    // --- TV_GEOMETRY_CURRENT
    /// The meshing control the analysis names does not exist.
    ControlNotFound,
    /// The body cannot be analysed: not regenerated, stale, not a solid, empty, invalid, or behind a configuration override.
    GeometryIneligible,
    // --- TV_MESH_CURRENT
    /// Nothing has been meshed for the control.
    MeshMissing,
    /// A mesh is held and does not describe the model: the geometry or the meshing intent moved.
    MeshStale,
    /// The last generation failed; any mesh still held is stale by definition.
    MeshGenerationFailed,
    // --- TV_MESH_STRUCTURAL
    /// The mesh violates a structural invariant: an inverted, degenerate or repeated-handle element is not a tetrahedron.
    MeshStructurallyInvalid,
    /// At least one element is `QualityClass::Invalid`.
    MeshHasInvalidElements,
    // --- TV_MESH_QUALITY
    /// The quality policy the report was produced under is self-contradictory, so P16 classified nothing. A WARNING: P16 still
    /// measured every metric, and refusing a sound solve over an error in the user's reporting preferences would be a false
    /// rejection.
    MeshQualityPolicyUnusable,
    /// An element is past one of the two accuracy bounds. The quality hard failure; the finding names the metric and element.
    ElementBelowAccuracyFloor,
    /// An element is worse than every element of every qualified P16 reference mesh on at least one metric. A WARNING.
    ElementOutsideQualifiedEnvelope,
    // --- TV_MATERIAL
    /// No material is assigned, or the body is gone.
    MaterialMissing,
    /// A material resolves and has no usable thermal conductivity.
    MaterialConductivityUnusable,
    /// The material lacks a property THIS MODE requires (a transient needs a density and a specific heat).
    MaterialIncompleteForMode,
    // --- TV_LOADS
    /// A load does not resolve, is not finite, or is otherwise unusable on the current mesh. `cause` is P18-LOAD's problem.
    LoadsInvalid,
    /// The analysis applies no load at all. INFORMATION: a model with no loads may be perfectly valid.
    NoLoadsConfigured,
    // --- TV_BCS
    /// A boundary condition does not resolve, is not finite, or conflicts with another. `cause` is P18-BC's problem.
    BoundaryConditionsInvalid,
    /// The analysis has no boundary condition. INFORMATION: an insulated transient body is valid.
    NoBoundaryConditionsConfigured,
    // --- TV_TRANSIENT_SETUP
    /// The initial temperature is not a finite absolute temperature.
    InitialTemperatureInvalid,
    /// The time step, the end time or the output policy is unusable, or the history would exceed a resource guard.
    TimeSettingsInvalid,
    // --- TV_SOURCE_COMPAT
    /// A derived object no longer describes the document: a remesh with the same counts and ids, an edit, a changed setting.
    SourceStale,
    /// Two derived objects do not describe one another, whatever the document says.
    SourceMismatch,
    /// A steady object presented for a transient analysis, or the reverse.
    ModeMismatch,
    // --- TV_WELL_POSED
    /// The assembled system is not available.
    SystemMissing,
    /// No part of a steady model is anchored to an absolute temperature.
    NoTemperatureDatum,
    /// Some independent region of a steady model is not anchored although another is.
    UnanchoredRegion,
    /// A transient system has no capacity matrix, or a steady one carries one.
    CapacityMissing,
    /// A transient capacity is not finite and positive on the diagonal.
    CapacityInvalid,
    /// The reduced system holds a value that is not finite.
    SystemNotFinite,
    /// A transient model with no prescribed temperature and no convection: well posed by its capacity, said so.
    TransientWithoutDatum,
    // --- TV_SOLVER_RESIDUAL
    /// The last solve attempt failed. A previous result is stale and cannot rescue the current analysis.
    SolveFailed,
    /// No solution is available.
    SolutionMissing,
    /// The independent normalized free residual is above the canonical tolerance.
    ResidualAboveTolerance,
    /// A residual metric is not finite.
    ResidualNotFinite,
    /// The canonical solver settings are not finite and positive, so no residual can be judged.
    SolverSettingsInvalid,
    /// The factorisation's pivot ratio is below the canonical floor.
    PivotBelowFloor,
    /// A prescribed unknown does not hold its prescribed temperature.
    ConstraintNotHeld,
    /// The stored history does not satisfy the contract of its time grid.
    HistoryContractViolated,
    /// A prescribed temperature overrode the initial temperature at t = 0 (the documented policy). INFORMATION.
    InitialConditionOverriddenByPrescription,
    // --- TV_RESULT_FINITE
    /// A nodal temperature is not finite, or below 0 K.
    TemperatureNotFinite,
    /// A state does not have one temperature per unknown.
    ResultSizeMismatch,
    // --- TV_POST
    /// The recovered fields are not available.
    PostMissing,
    /// A recovered gradient, flux or magnitude is not finite.
    PostFieldNotFinite,
    /// A recovered array does not have one value per tetrahedron, or the state count is wrong.
    PostCardinalityMismatch,
    /// The recovered temperatures are not the solver's, bit for bit.
    PostTemperatureMismatch,
    // --- TV_BALANCE
    /// The heat balance is not available.
    BalanceMissing,
    /// A state's residual is outside the balance milestone's tolerance policy.
    BalanceOutOfTolerance,
    /// A heat rate, a total or a metric of a state is not finite.
    BalanceNotFinite,
    /// The number of balance states does not match the history.
    BalanceStatesMismatch,
    /// A state does not account for every prepared load, every convection condition and every prescribed unknown the system was
    /// made from: a category is missing or surplus.
    BalanceAttributionMismatch,
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(ValidationCode code) noexcept;

/// How serious a finding is.
enum class ValidationSeverity : std::uint8_t {
    /// Context only; never changes the outcome.
    Info,
    /// The result may be accepted and the user should be told.
    Warning,
    /// The result cannot be accepted.
    Error,
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(ValidationSeverity severity) noexcept;

/// The severity @p code always carries.
///
/// A FUNCTION OF THE CODE, not a field a caller chooses, so the same problem cannot be an error in one report and a warning in
/// another, and so a viewer cannot downgrade one: core validation owns severity. The warnings are
/// `MeshQualityPolicyUnusable` and `ElementOutsideQualifiedEnvelope`; the information codes are `NoLoadsConfigured`,
/// `NoBoundaryConditionsConfigured`, `TransientWithoutDatum` and `InitialConditionOverriddenByPrescription`; everything else
/// refuses.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT ValidationSeverity severityOf(ValidationCode code) noexcept;

/// The rule a code belongs to.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT ValidationRule ruleOf(ValidationCode code) noexcept;

/// Which derived object a finding is about.
enum class ValidationSubject : std::uint8_t {
    DofMap,
    PreparedLoads,
    PreparedBoundary,
    System,
    Solution,
    PostResult,
    BalanceResult,
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(ValidationSubject subject) noexcept;

/// One classified observation. The payload is the fields; the message is a fallback for a log, and nothing should parse it.
struct ValidationFinding {
    ValidationCode code = ValidationCode::AnalysisNotFound;
    /// `severityOf(code)`, carried so a reader of the payload does not call it.
    ValidationSeverity severity = ValidationSeverity::Error;
    /// `ruleOf(code)`.
    ValidationRule rule = ValidationRule::AnalysisDefinition;
    std::optional<ValidationSubject> subject{};
    /// Which metric, when the finding is about one. Absent otherwise: naming a metric that was not measured is fabricated data.
    std::optional<meshing::QualityMetric> metric{};
    std::optional<meshing::ElementId> element{};
    /// The 1-based step of a transient history, its end time and its ACTUAL size.
    std::optional<std::size_t> step{};
    std::optional<double> time{};
    std::optional<double> stepSize{};
    std::optional<double> value{};
    std::optional<double> threshold{};
    /// How many elements, states or unknowns the finding stands for. One finding speaks for all of them -- 50 000 elements past
    /// one bound are ONE finding with the worst one named -- but every element was judged.
    std::optional<std::size_t> count{};
    /// The unit of `value` and `threshold`.
    std::string_view unit{};
    /// A stable name for the upstream cause, when an upstream milestone classified it (`target_unresolved`, ...).
    std::string_view cause{};
    std::string message;

    friend bool operator==(const ValidationFinding&, const ValidationFinding&) = default;
};

/// One metric's worst value and the element holding it: a MEASUREMENT, not a finding, recorded for every classifiable metric
/// whether or not it crossed a bound.
struct ThermalMetricObservation {
    meshing::QualityMetric metric{};
    meshing::ElementId element{};
    double value = 0.0;

    friend bool operator==(const ThermalMetricObservation&, const ThermalMetricObservation&) = default;
};

// ---------------------------------------------------------------------------
// The report
// ---------------------------------------------------------------------------

/// Whether a rule was run.
enum class CheckState : std::uint8_t {
    /// The rule ran; its status is derived from its findings.
    Evaluated,
    /// The rule needed something an earlier rule found missing or invalid, so it was not run and says so. NEVER a pass.
    Skipped,
    /// The rule does not apply to this analysis mode.
    NotApplicable,
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(CheckState state) noexcept;

/// The outcome of one rule.
enum class CheckStatus : std::uint8_t { Pass, Warning, Fail, Skipped, NotApplicable };

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(CheckStatus status) noexcept;

/// The overall outcome.
enum class ValidationStatus : std::uint8_t {
    /// Every rule that applies passed, and nothing is worth saying.
    Pass,
    /// Accepted, with warnings the user should see.
    PassWithWarnings,
    /// At least one rule failed: the analysis is not accepted.
    Fail,
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(ValidationStatus status) noexcept;

/// One entry per rule, in the rule order. It records only WHETHER the rule ran: the status is derived from the findings.
struct ThermalCheck {
    ValidationRule rule = ValidationRule::AnalysisDefinition;
    CheckState state = CheckState::Skipped;
    /// The rule whose failure stopped this one, for a skipped check.
    std::optional<ValidationRule> blockedBy{};

    friend bool operator==(const ThermalCheck&, const ThermalCheck&) = default;
};

/// What was measured, for the evidence and for a reader. NONE of these is an acceptance threshold unless a rule says so.
struct ThermalValidationMetrics {
    std::optional<std::size_t> tetCount{};
    std::optional<std::size_t> dofCount{};
    std::optional<std::size_t> freeEquations{};
    std::optional<std::size_t> constrainedCount{};
    /// The steady normalized free residual and the tolerance it was judged by (the canonical setting).
    std::optional<double> steadyResidual{};
    std::optional<double> residualTolerance{};
    /// The worst transient step: its 1-based index, end time, ACTUAL size and physical residual.
    std::optional<std::size_t> worstResidualStep{};
    std::optional<double> worstResidualTime{};
    std::optional<double> worstResidualStepSize{};
    std::optional<double> worstTransientResidual{};
    std::optional<std::size_t> transientStepCount{};
    /// The worst balance state: its 1-based step (0 for steady), time and normalized residual.
    std::optional<std::size_t> worstBalanceStep{};
    std::optional<double> worstBalanceTime{};
    std::optional<double> worstBalanceResidual{};
    /// INFORMATION only: the extremes of the final temperature and of the heat-flux magnitude. Never a gate.
    std::optional<double> minimumTemperature{};
    std::optional<double> maximumTemperature{};
    std::optional<double> maximumHeatFlux{};

    friend bool operator==(const ThermalValidationMetrics&, const ThermalValidationMetrics&) = default;
};

/// Everything validation found.
///
/// THE STATUS IS DERIVED, NEVER STORED. `statusOf(rule)` is computed from the checks and the findings, `status()` from those,
/// and the counts from the findings, so a report carrying an error cannot claim to pass. Defined inline, and not by
/// preference: an out-of-line member of a struct without an export macro is not exported, and that defect has recurred in
/// BetterCAD modules -- caught only by `debug-shared-ext`.
///
/// DETERMINISTIC. `checks` always holds every rule in rule order. `findings` is ordered by rule, then severity (errors first),
/// then code, then element, then step. No wall clock, no pointer identity, no unordered container.
struct ThermalValidationReport {
    ThermalAnalysisMode mode = ThermalAnalysisMode::Steady;
    /// The analysis the report is about, when it exists.
    std::optional<ThermalAnalysisId> analysis{};
    /// What the analysis was computed from at the time of validation. Present when the inputs resolved. The report is current
    /// exactly while this still equals `currentResultSource` (the edit-stales-the-report rule) AND the policy it was produced under
    /// is still the policy in force (`policyFingerprint`).
    std::optional<ThermalResultSource> source{};
    /// The source of the solution the report judged, by value (the solution's own, which is what its recovered fields and its heat
    /// balance carry too). Present when a solution was presented. Provenance: it is recorded, not re-derived.
    std::optional<ThermalPostSource> resultSource{};
    /// The fingerprint of the policy the report was produced under.
    std::string policyFingerprint;

    std::vector<ThermalCheck> checks{};
    std::vector<ValidationFinding> findings{};
    ThermalValidationMetrics metrics{};
    std::vector<ThermalMetricObservation> worstByMetric{};

    [[nodiscard]] CheckStatus statusOf(ValidationRule rule) const noexcept {
        for (const ThermalCheck& check : checks) {
            if (check.rule != rule) {
                continue;
            }
            if (check.state == CheckState::Skipped) {
                return CheckStatus::Skipped;
            }
            if (check.state == CheckState::NotApplicable) {
                return CheckStatus::NotApplicable;
            }
            bool warning = false;
            for (const ValidationFinding& finding : findings) {
                if (finding.rule != rule) {
                    continue;
                }
                if (finding.severity == ValidationSeverity::Error) {
                    return CheckStatus::Fail;
                }
                warning = warning || finding.severity == ValidationSeverity::Warning;
            }
            return warning ? CheckStatus::Warning : CheckStatus::Pass;
        }
        return CheckStatus::Skipped;
    }

    [[nodiscard]] std::size_t errorCount() const noexcept {
        return static_cast<std::size_t>(std::ranges::count_if(
            findings, [](const ValidationFinding& f) { return f.severity == ValidationSeverity::Error; }));
    }
    [[nodiscard]] std::size_t warningCount() const noexcept {
        return static_cast<std::size_t>(std::ranges::count_if(
            findings, [](const ValidationFinding& f) { return f.severity == ValidationSeverity::Warning; }));
    }
    [[nodiscard]] std::size_t infoCount() const noexcept {
        return static_cast<std::size_t>(std::ranges::count_if(
            findings, [](const ValidationFinding& f) { return f.severity == ValidationSeverity::Info; }));
    }

    /// `Fail` if any rule failed OR a mandatory rule was never evaluated (a skipped rule is not a pass), `PassWithWarnings` if
    /// any warning, else `Pass`. A rule that does not apply to the mode is neither.
    [[nodiscard]] ValidationStatus status() const noexcept {
        bool warning = false;
        for (const ThermalCheck& check : checks) {
            const CheckStatus s = statusOf(check.rule);
            if (s == CheckStatus::Fail || s == CheckStatus::Skipped) {
                return ValidationStatus::Fail;
            }
            warning = warning || s == CheckStatus::Warning;
        }
        if (checks.empty()) {
            return ValidationStatus::Fail;
        }
        return warning ? ValidationStatus::PassWithWarnings : ValidationStatus::Pass;
    }

    /// FULL P18 ACCEPTANCE: every mandatory rule for the mode passed, the solve, the recovered fields and the heat balance
    /// included. Warnings do not refuse.
    [[nodiscard]] bool accepted() const noexcept { return status() != ValidationStatus::Fail; }

    /// The inputs are fit to solve: every rule of the Input group passed. Says nothing about a system or a result.
    [[nodiscard]] bool inputsAccepted() const noexcept {
        for (const ThermalCheck& check : checks) {
            if (groupOf(check.rule) != ValidationGroup::Input) {
                continue;
            }
            const CheckStatus s = statusOf(check.rule);
            if (s == CheckStatus::Fail || s == CheckStatus::Skipped) {
                return false;
            }
        }
        return !checks.empty();
    }

    /// The SOLVE is accepted: the inputs, the system, the solver residual and the temperatures. NOT the full analysis: the
    /// recovered fields and the heat balance are separate gates, and a solve accepted without them is reported as exactly that.
    [[nodiscard]] bool solveAccepted() const noexcept {
        for (const ThermalCheck& check : checks) {
            if (check.rule > ValidationRule::ResultFinite) {
                break;
            }
            const CheckStatus s = statusOf(check.rule);
            if (s == CheckStatus::Fail || s == CheckStatus::Skipped) {
                return false;
            }
        }
        return !checks.empty();
    }

    friend bool operator==(const ThermalValidationReport&, const ThermalValidationReport&) = default;
};

// ---------------------------------------------------------------------------
// The policy as data
// ---------------------------------------------------------------------------

/// The policy rendered as deterministic text: the rule order, every code's rule and severity, the mesh thresholds, the strictness
/// of the comparisons and the authority each numeric acceptance policy is taken from. Generated from the enumerations and the
/// constants above, so changing any of them changes it. No timestamp, no address, no environment.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string thermalValidationPolicyText();

/// A 64-bit FNV-1a digest of `thermalValidationPolicyText()`, as `p18-valid-v1:<16 hex digits>`. Recorded in every report.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string thermalValidationPolicyFingerprint();

// ---------------------------------------------------------------------------
// The seams: each rule's judgement of plain data
// ---------------------------------------------------------------------------
//
// PUBLIC DELIBERATELY, for the reason `solveThermalAlgebra` and `steadyHeatBalance` are: the production types cannot be
// constructed in an invalid state (that is what makes them evidence), so a corrupted temperature, a NaN gradient or an
// out-of-tolerance balance can only be shown to the validator as the plain data the typed result holds. Each is the SAME code
// `validateThermalAnalysis` runs, and the model-level function calls nothing else to decide.

/// The verdict on a mesh quality report: the structural findings (TV_MESH_STRUCTURAL), the quality findings (TV_MESH_QUALITY)
/// and the worst value of each classifiable metric.
///
/// Takes a REPORT, not a mesh: the metrics are P16's and are not recomputed. A HELD mesh can never be structurally invalid
/// (`Mesher::generate` refuses one), so the structural findings are reachable only through this entry, which is public because
/// the library does not control what it is given.
struct ThermalMeshVerdict {
    std::vector<ValidationFinding> structural{};
    std::vector<ValidationFinding> quality{};
    std::vector<ThermalMetricObservation> worstByMetric{};

    friend bool operator==(const ThermalMeshVerdict&, const ThermalMeshVerdict&) = default;
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT ThermalMeshVerdict classifyThermalMesh(const meshing::MeshQualityReport& quality);

/// TV_RESULT_FINITE: every state has @p expectedDofs temperatures, each finite and not below 0 K. The primary arrays are judged
/// BEFORE any derived statistic, so nothing downstream operates on a NaN.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::vector<ValidationFinding>
checkTemperatureStates(std::span<const std::span<const Temperature>> states, std::size_t expectedDofs,
                       std::span<const double> times = {});

/// What a steady residual check found.
struct ResidualVerdict {
    std::vector<ValidationFinding> findings{};
    /// The normalized free residual, when it could be computed.
    std::optional<double> residual{};

    friend bool operator==(const ResidualVerdict&, const ResidualVerdict&) = default;
};

/// TV_SOLVER_RESIDUAL for a steady field: the independent free residual of @p temperatures through P18-SOLVE-001's own
/// `validateSteadySolution`, judged against @p settings (the canonical tolerance -- there is no second one), and the pivot ratio
/// against the canonical floor. A solver's "success" is not an input.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT ResidualVerdict
checkSteadyResidual(const ConductanceMatrix& conductance, const ThermalHeatVector& heat,
                    const ThermalFreeEquationMap& equations, const ThermalConstraintMap& constraints,
                    std::span<const Temperature> temperatures, const ThermalSolverSettings& settings,
                    std::optional<double> pivotRatio = std::nullopt);

/// What a transient residual check found, with the worst step.
struct TransientResidualVerdict {
    std::vector<ValidationFinding> findings{};
    std::optional<std::size_t> worstStep{};
    std::optional<double> worstTime{};
    std::optional<double> worstStepSize{};
    std::optional<double> worstResidual{};

    friend bool operator==(const TransientResidualVerdict&, const TransientResidualVerdict&) = default;
};

/// TV_SOLVER_RESIDUAL for a transient history: EVERY step, not the last one.
///
/// Each step's recorded residuals (the qualified per-step metrics, as the integrator accepted them) are held to the canonical
/// tolerance and pivot floor, and -- for every step whose two states are stored -- the step is re-evaluated independently by
/// P18-TRANSIENT-001's own `validateTransientStep` with the ACTUAL step size of the grid. @p states holds the stored samples
/// (`EveryStep`: `t_0 ... t_N`; `FinalOnly`: `{t_N}`, in which case only the recorded residuals can be judged and a single-step
/// run is also re-evaluated against @p initial).
[[nodiscard]] BETTERCAD_THERMAL_EXPORT TransientResidualVerdict
checkTransientResiduals(const ConductanceMatrix& conductance, const CapacityMatrix& capacity, const ThermalHeatVector& heat,
                        const ThermalFreeEquationMap& equations, const ThermalConstraintMap& constraints,
                        const ThermalSolverSettings& settings, const TimeGrid& grid,
                        std::span<const TransientStepRecord> records, std::span<const Temperature> initial,
                        std::span<const std::span<const Temperature>> states, TransientOutputPolicy output);

/// TV_TRANSIENT_SETUP: the initial temperature is a finite absolute temperature, the time settings are valid (`0 < dt <= t_end`,
/// both finite), the time grid can be built within its resource guard, and the stored history for @p nodeCount unknowns fits its
/// guard. Nothing is adjusted: a step that is too large is refused, never turned into a larger one.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::vector<ValidationFinding>
checkTransientSetup(const UniformInitialTemperature& initial, const TransientSettings& transient, std::size_t nodeCount);

/// TV_POST: every state has one temperature per unknown equal to the solver's bit for bit, and one finite gradient, flux and
/// magnitude per tetrahedron. @p solverStates are the solver's own samples, in the same order.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::vector<ValidationFinding>
checkPostStates(std::span<const ThermalPostState> states, std::size_t expectedElements, std::size_t expectedDofs,
                std::span<const std::span<const Temperature>> solverStates);

/// TV_BALANCE: every state is finite and within P18-BALANCE-001's tolerance policy -- `balanceWithinTolerance` on the numbers
/// the state holds AND the state's own recorded status -- and the number of states is @p expectedStates. The tolerance is
/// BALANCE's; this adds none. The worst state is reported through @p worst.
struct BalanceVerdict {
    std::vector<ValidationFinding> findings{};
    std::optional<std::size_t> worstStep{};
    std::optional<double> worstTime{};
    std::optional<double> worstResidual{};

    friend bool operator==(const BalanceVerdict&, const BalanceVerdict&) = default;
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT BalanceVerdict
checkHeatBalanceStates(std::span<const ThermalHeatBalance> states, std::size_t expectedStates);

/// TV_BALANCE, the attribution: every state lists one entry per prepared load, one per convection condition and one per prescribed
/// unknown (a reaction belongs to an UNKNOWN: two conditions that share one share its heat, and no split is invented). A category the
/// system was made from but the balance does not account for, or one it accounts for twice, is a failure.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::vector<ValidationFinding>
checkBalanceAttribution(std::span<const ThermalHeatBalance> states, std::size_t loads, std::size_t convections, std::size_t reactions);

/// TV_WELL_POSED for a STEADY system: every independent region of the reduced conduction network must be anchored to an
/// absolute temperature (a prescribed temperature or a positive convection coefficient -- not specifically a Dirichlet). Fully
/// constrained is well posed. P18-SOLVE-001's own `analyseAnchoring` decides.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::vector<ValidationFinding>
checkSteadyWellPosedness(const ConductanceMatrix& conductance, const ThermalHeatVector& heat,
                         const ThermalFreeEquationMap& equations, const ThermalConstraintMap& constraints);

/// TV_WELL_POSED for a TRANSIENT system: a capacity that is finite and positive on every diagonal entry. NO datum is required --
/// a pure-Neumann transient body with a positive capacity and a valid initial temperature is well posed, and the steady rule
/// must not be applied to it.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::vector<ValidationFinding>
checkTransientWellPosedness(const ConductanceMatrix& conductance, const std::optional<CapacityMatrix>& capacity,
                            const ThermalConstraintMap& constraints, std::size_t convectionFacets);

// ---------------------------------------------------------------------------
// The evidence of a solve, as plain data
// ---------------------------------------------------------------------------

/// Everything the four result rules judge, as VIEWS of plain data.
///
/// `validateThermalAnalysis` builds one from the typed objects it was given and calls `judgeSolveEvidence`, which decides
/// TV_SOLVER_RESIDUAL, TV_RESULT_FINITE, TV_POST and TV_BALANCE and nothing else. It is public for the reason the seams above are: a
/// typed result cannot be corrupted, so "a perturbed free temperature", "a NaN in the heat flux" or "a balance that cancels while the
/// residual does not" can only be shown to the model-level decision as the plain data those objects hold. The SAME function decides in
/// production; the tests change only the data.
///
/// NOTHING IS OWNED. The caller keeps every array alive for the call.
struct ThermalSolveEvidence {
    ThermalAnalysisMode mode = ThermalAnalysisMode::Steady;
    /// The canonical settings: the residual tolerance and the pivot floor. There is no other.
    ThermalSolverSettings settings{};
    /// The system the solution was computed from and the free numbering and constraints of its reduced system.
    const ConductanceMatrix* conductance = nullptr;
    const CapacityMatrix* capacity = nullptr;   // transient only
    const ThermalHeatVector* heat = nullptr;
    const ThermalFreeEquationMap* equations = nullptr;
    const ThermalConstraintMap* constraints = nullptr;
    std::size_t dofCount = 0;
    std::size_t tetCount = 0;
    /// The last solve attempt's failure, when it failed.
    const Error* lastSolveFailure = nullptr;
    /// The solution's states (steady: one; transient: the stored samples) and their times in seconds. Empty when no solution was presented.
    std::vector<std::span<const Temperature>> states{};
    std::vector<double> times{};
    /// Steady: the factorisation's pivot ratio.
    std::optional<double> pivotRatio{};
    /// Transient: the time grid, the integrator's per-step records, the resolved initial state, the output policy.
    const TimeGrid* grid = nullptr;
    std::span<const TransientStepRecord> records{};
    std::span<const Temperature> initial{};
    std::size_t initialOverrides = 0;
    TransientOutputPolicy output = TransientOutputPolicy::FinalOnly;
    /// The recovered fields, when presented.
    std::optional<std::span<const ThermalPostState>> post{};
    /// The heat balance's states, when presented, and the number of prepared loads, convection conditions and prescribed unknowns the system
    /// was made from (absent when the prepared objects were not available to count).
    std::optional<std::span<const ThermalHeatBalance>> balance{};
    std::optional<std::array<std::size_t, 3>> balanceCategories{};
};

/// Decides TV_SOLVER_RESIDUAL, TV_RESULT_FINITE, TV_POST and TV_BALANCE for @p evidence, into @p report (whose checks must already hold every
/// rule, and whose earlier rules are not touched).
///
/// THE ORDER AND THE INDEPENDENCE ARE THE POLICY: a recorded solve failure or a missing solution stops the three rules after it (they would judge
/// nothing); otherwise the finiteness, the recovered fields and the balance are each judged on their own, so a residual that fails does not hide a
/// balance that fails, and a balance that passes does not rescue a residual that fails.
BETTERCAD_THERMAL_EXPORT void judgeSolveEvidence(const ThermalSolveEvidence& evidence, ThermalValidationReport& report);
// ---------------------------------------------------------------------------
// The derived state
// ---------------------------------------------------------------------------

/// What a solver service holds for one analysis, as VIEWS. Every pointer may be null: a missing object is reported by the rule
/// that needs it, never invented, and nothing here is copied or written.
struct ThermalDerivedState {
    const ThermalDofMap* dofMap = nullptr;
    const PreparedThermalLoads* preparedLoads = nullptr;
    const PreparedThermalBoundary* preparedBoundary = nullptr;
    const ThermalSystem* system = nullptr;
    /// The solution of a STEADY analysis, or of a transient one. Presenting the wrong kind for the mode is a finding.
    const SteadyThermalSolution* steady = nullptr;
    const TransientThermalSolution* transient = nullptr;
    const ThermalPostResult* post = nullptr;
    const ThermalBalanceResult* balance = nullptr;
    /// The last solve attempt's failure, when it failed (`ThermalResultSlot::failure()`). A failure is never a current result and
    /// never makes an older one current.
    const Error* lastSolveFailure = nullptr;
};

// ---------------------------------------------------------------------------
// The entry point
// ---------------------------------------------------------------------------

/// Decides whether the thermal analysis @p analysis, with the derived state @p derived, is ACCEPTED.
///
/// ALWAYS RETURNS A REPORT, never an error: "this model is not ready, and here is why" is the answer. It is READ-ONLY in every
/// argument and DOES NOT SOLVE: it never assembles, solves, recovers or balances; it judges what exists. A solver service that
/// wants "solve and validate" calls its solver and then this.
///
/// THE POLICY ON FAILURE, frozen: rules run in the enumeration order, EVERY rule whose own prerequisites exist is evaluated, and
/// a rule whose prerequisite is missing or invalid is SKIPPED with the rule that blocked it -- never passed. Concretely, the Input
/// group is judged rule by rule (a missing material does not hide a stale mesh, and the reverse), the material is judged as soon
/// as the control names a body, the loads and conditions need a current model, and the System, Solve, Result and Balance
/// groups run only when every Input rule passed: a stale mesh makes a solve's residual meaningless, and a report that said
/// otherwise would be a cascade of consequences burying the cause.
///
/// THE MODE decides what is required. A steady analysis needs `k`; a transient one needs `k`, `rho` and `cp`, an initial
/// temperature and valid time settings, and its capacity; the steady datum rule applies to steady only.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT ThermalValidationReport
validateThermalAnalysis(const Document& document, const features::Regenerator& regenerator,
                        const meshing::Mesher& mesher, ThermalAnalysisId analysis,
                        const ThermalDerivedState& derived = {});

/// Whether @p report still describes the document: its source equals the source of a result computed now. Identity only; no
/// array, no count and no value is read. A viewer's colour range, camera or legend is not part of any source, so editing one
/// cannot stale a report; a load, a boundary condition, a setting, a material, a remesh or a geometry edit does.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT bool
isCurrent(const ThermalValidationReport& report, const Document& document, const features::Regenerator& regenerator,
          const meshing::Mesher& mesher);

} // namespace bettercad::thermal
