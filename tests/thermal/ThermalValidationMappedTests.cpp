// P18-VALID-001: the acceptance decision against REAL P16 meshes and real documents.
//
// These are the AUTHORITY tests. A thermal analysis is accepted for the document it was computed from and for no other, by
// every rule of the mode it runs in; and it is refused, for the right reason and in the right place, when its mesh is stale or
// replaced, its material lacks what the mode needs, a load or a condition does not resolve, the model is not well posed, or
// anything derived no longer describes what the user has now.
//
// The judgement of corrupted numbers is shown on hand data in ThermalValidationTests.cpp, because a typed result cannot be
// corrupted. Here every expectation is a closed form (a slab's 270 W, a face's h A dT) or an identity between two reports.

#include "ThermalValidationTestSupport.hpp"

#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <format>
#include <functional>
#include <limits>
#include <memory>
#include <set>

using namespace vltest;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using thermal::PreparedThermalBoundary;
using thermal::PreparedThermalLoads;
using thermal::SurfaceHeatFluxLoad;
using thermal::ThermalAnalysisMode;
using thermal::ThermalDerivedState;
using thermal::ThermalLoad;
using thermal::VolumetricHeatSourceLoad;

namespace {

constexpr double kAmbient = 293.15;

[[nodiscard]] ThermalLoad generationLoad(std::uint64_t id, double generation) {
    return ThermalLoad{ThermalLoadId::fromValue(id), VolumetricHeatSourceLoad{.generation = VolumetricHeatSource::fromSi(generation)}};
}

[[nodiscard]] ThermalLoad surfaceLoad(std::uint64_t id, const FaceName& face, double flux) {
    return ThermalLoad{ThermalLoadId::fromValue(id), SurfaceHeatFluxLoad{.face = face, .flux = HeatFlux::fromSi(flux)}};
}

[[nodiscard]] thermal::ThermalAnalysisDefinition steadyDefinition(const BoundaryStudy& study, std::vector<ThermalLoad> loads,
                                                                  std::vector<ThermalBoundaryCondition> conditions,
                                                                  ThermalSolverSettings settings = {}) {
    return thermal::ThermalAnalysisDefinition{.mesh = study.control,
                                              .mode = ThermalAnalysisMode::Steady,
                                              .loads = std::move(loads),
                                              .boundaryConditions = std::move(conditions),
                                              .solver = settings};
}

[[nodiscard]] thermal::ThermalAnalysisDefinition
transientDefinition(const BoundaryStudy& study, std::vector<ThermalLoad> loads, std::vector<ThermalBoundaryCondition> conditions,
                    double initialKelvin, double dt, double end,
                    thermal::TransientOutputPolicy output = thermal::TransientOutputPolicy::EveryStep) {
    return thermal::ThermalAnalysisDefinition{
        .mesh = study.control,
        .mode = ThermalAnalysisMode::Transient,
        .loads = std::move(loads),
        .boundaryConditions = std::move(conditions),
        .initialCondition = thermal::UniformInitialTemperature{Temperature::fromSi(initialKelvin)},
        .transient = thermal::TransientSettings{.timeStep = Time::fromSi(dt), .endTime = Time::fromSi(end), .output = output}};
}

void define(BoundaryStudy& study, const thermal::ThermalAnalysisDefinition& definition) {
    const Result<bool> set = study.setDefinition(definition);
    INFO((set.has_value() ? std::string{} : set.error().message));
    REQUIRE(set.has_value());
}

/// The whole derived chain of one analysis state, as VALUES: no model is kept, because a model is a borrowed snapshot that must not
/// outlive a remesh, and the staleness tests keep this alive across one.
struct Derived {
    ThermalDofMap map;
    std::optional<PreparedThermalLoads> loads;
    std::optional<PreparedThermalBoundary> boundary;
    std::optional<ThermalSystem> system;
    std::optional<SteadyThermalSolution> steady;
    std::optional<TransientThermalSolution> transient;
    std::optional<thermal::ThermalPostResult> post;
    std::optional<ThermalBalanceResult> balance;
    /// Why the chain stopped, when it did: the upstream milestone's own message.
    std::string stoppedBecause;

    [[nodiscard]] ThermalDerivedState view() const {
        ThermalDerivedState state;
        state.dofMap = &map;
        state.preparedLoads = loads.has_value() ? &*loads : nullptr;
        state.preparedBoundary = boundary.has_value() ? &*boundary : nullptr;
        state.system = system.has_value() ? &*system : nullptr;
        state.steady = steady.has_value() ? &*steady : nullptr;
        state.transient = transient.has_value() ? &*transient : nullptr;
        state.post = post.has_value() ? &*post : nullptr;
        state.balance = balance.has_value() ? &*balance : nullptr;
        return state;
    }
};

/// Prepares, assembles, solves, recovers and balances the analysis the DOCUMENT holds, stopping at the first refusal. Every step is
/// the qualified function of its own milestone; nothing is reimplemented here.
[[nodiscard]] std::unique_ptr<Derived> derive(const BoundaryStudy& study) {
    const thermal::ThermalAnalysisDefinition def = study.definition();
    const ThermalModel model = study.model();
    auto out = std::make_unique<Derived>(Derived{.map = dofMapOf(model), .loads = {}, .boundary = {}, .system = {}, .steady = {},
                                                 .transient = {}, .post = {}, .balance = {}, .stoppedBecause = {}});
    const auto stop = [&out](const Error& error) { out->stoppedBecause = error.message; };

    Result<PreparedThermalLoads> loads = thermal::prepareThermalLoads(model, out->map, def);
    if (!loads.has_value()) {
        stop(loads.error());
        return out;
    }
    out->loads = std::move(*loads);
    Result<PreparedThermalBoundary> boundary = thermal::prepareThermalBoundary(model, out->map, def);
    if (!boundary.has_value()) {
        stop(boundary.error());
        return out;
    }
    out->boundary = std::move(*boundary);
    Result<ThermalSystem> system = thermal::assembleThermalSystem(study.document, model, out->map, *out->loads, *out->boundary, def);
    if (!system.has_value()) {
        stop(system.error());
        return out;
    }
    out->system = std::move(*system);

    if (def.mode == ThermalAnalysisMode::Steady) {
        Result<SteadyThermalSolution> solved = thermal::solveSteadyThermal(study.document, model, out->map, *out->system, *out->boundary,
                                                                           def.loads, def.boundaryConditions, def.solver);
        if (!solved.has_value()) {
            stop(solved.error());
            return out;
        }
        out->steady = std::move(*solved);
        Result<thermal::ThermalPostResult> post =
            thermal::recoverSteadyThermal(study.document, model, out->map, *out->steady, def.loads, def.boundaryConditions, def.solver);
        if (!post.has_value()) {
            stop(post.error());
            return out;
        }
        out->post = std::move(*post);
        Result<ThermalBalanceResult> balance = thermal::recoverSteadyBalance(
            study.document, model, out->map, *out->system, *out->steady, *out->loads, *out->boundary, def.loads, def.boundaryConditions, def.solver);
        if (!balance.has_value()) {
            stop(balance.error());
            return out;
        }
        out->balance = std::move(*balance);
    } else {
        Result<TransientThermalSolution> solved =
            thermal::solveTransientThermal(study.document, model, out->map, *out->system, *out->boundary, def);
        if (!solved.has_value()) {
            stop(solved.error());
            return out;
        }
        out->transient = std::move(*solved);
        Result<thermal::ThermalPostResult> post = thermal::recoverTransientThermal(
            study.document, model, out->map, *out->transient, def.loads, def.boundaryConditions, *def.initialCondition, *def.transient, def.solver);
        if (!post.has_value()) {
            stop(post.error());
            return out;
        }
        out->post = std::move(*post);
        Result<ThermalBalanceResult> balance = thermal::recoverTransientBalance(
            study.document, model, out->map, *out->system, *out->transient, *out->loads, *out->boundary, def.loads, def.boundaryConditions,
            *def.initialCondition, *def.transient, def.solver);
        if (!balance.has_value()) {
            stop(balance.error());
            return out;
        }
        out->balance = std::move(*balance);
    }
    return out;
}

/// What is true of EVERY report the validator produces, whatever it found. Checked on each one the mapped tests make.
void requireWellFormed(const ThermalValidationReport& report) {
    REQUIRE(report.checks.size() == thermal::kValidationRuleCount);
    for (std::size_t i = 0; i < report.checks.size(); ++i) {
        const ThermalCheck& check = report.checks[i];
        CHECK(static_cast<std::size_t>(check.rule) == i);   // every rule, in order
        // A skipped check names what blocked it; an evaluated or not-applicable one does not.
        CHECK((check.state == CheckState::Skipped) == check.blockedBy.has_value());
        if (check.blockedBy.has_value()) {
            CHECK(*check.blockedBy < check.rule);   // a rule is only ever blocked by an EARLIER one
        }
    }
    std::size_t errors = 0;
    std::size_t warnings = 0;
    std::size_t infos = 0;
    for (const ValidationFinding& finding : report.findings) {
        // SEVERITY AND RULE ARE FUNCTIONS OF THE CODE: nothing carries a severity a viewer could have changed.
        CHECK(finding.severity == thermal::severityOf(finding.code));
        CHECK(finding.rule == thermal::ruleOf(finding.code));
        // Findings exist only for rules that ran.
        CHECK(report.checks[static_cast<std::size_t>(finding.rule)].state == CheckState::Evaluated);
        errors += finding.severity == ValidationSeverity::Error ? 1 : 0;
        warnings += finding.severity == ValidationSeverity::Warning ? 1 : 0;
        infos += finding.severity == ValidationSeverity::Info ? 1 : 0;
    }
    CHECK(errors == report.errorCount());
    CHECK(warnings == report.warningCount());
    CHECK(infos == report.infoCount());
    CHECK(errors + warnings + infos == report.findings.size());
    CHECK(std::ranges::is_sorted(report.findings, [](const ValidationFinding& a, const ValidationFinding& b) { return a.rule < b.rule; }));
    // The overall status is a function of the per-rule statuses and of nothing else.
    bool anyFail = false;
    bool anyWarn = false;
    for (const ThermalCheck& check : report.checks) {
        const CheckStatus status = report.statusOf(check.rule);
        anyFail = anyFail || status == CheckStatus::Fail || status == CheckStatus::Skipped;
        anyWarn = anyWarn || status == CheckStatus::Warning;
    }
    CHECK(report.status() == (anyFail ? ValidationStatus::Fail : (anyWarn ? ValidationStatus::PassWithWarnings : ValidationStatus::Pass)));
    CHECK(report.accepted() == !anyFail);
    CHECK(report.policyFingerprint == thermal::thermalValidationPolicyFingerprint());
}

[[nodiscard]] ThermalValidationReport validate(const BoundaryStudy& study, const Derived* derived = nullptr,
                                               const Error* failure = nullptr) {
    ThermalDerivedState state = derived != nullptr ? derived->view() : ThermalDerivedState{};
    state.lastSolveFailure = failure;
    ThermalValidationReport report = thermal::validateThermalAnalysis(study.document, study.regenerator, study.mesher, study.analysis, state);
    requireWellFormed(report);
    return report;
}

/// The ordered report as evidence lines (`ctest` shows them with the warnings).
void print(const std::string& name, const ThermalValidationReport& report) {
    std::string line;
    for (const ThermalCheck& check : report.checks) {
        WARN(std::format("REPORT {} | {} | {}{}", name, thermal::toString(check.rule), thermal::toString(report.statusOf(check.rule)),
                         check.blockedBy.has_value() ? std::format(" (blocked by {})", thermal::toString(*check.blockedBy)) : std::string{}));
    }
    for (const ValidationFinding& finding : report.findings) {
        WARN(std::format("FINDING {} | {} | {} | {} | {}", name, thermal::toString(finding.rule), thermal::toString(finding.code),
                         thermal::toString(finding.severity), finding.message));
    }
    const thermal::ThermalValidationMetrics& m = report.metrics;
    const auto opt = [](const auto& value) { return value.has_value() ? std::format("{}", *value) : std::string{"-"}; };
    WARN(std::format("METRICS {} | tets {} | dof {} | free {} | constrained {} | steady residual {} | tolerance {} | worst transient residual {} at step {} t {} dt {} | steps {} | worst balance {} at step {} t {} | T range {} .. {} K | max |q| {}",
                     name, opt(m.tetCount), opt(m.dofCount), opt(m.freeEquations), opt(m.constrainedCount), opt(m.steadyResidual), opt(m.residualTolerance),
                     opt(m.worstTransientResidual), opt(m.worstResidualStep), opt(m.worstResidualTime), opt(m.worstResidualStepSize),
                     opt(m.transientStepCount), opt(m.worstBalanceResidual), opt(m.worstBalanceStep), opt(m.worstBalanceTime), opt(m.minimumTemperature),
                     opt(m.maximumTemperature), opt(m.maximumHeatFlux)));
    WARN(std::format("OVERALL {} | {} | errors {} | warnings {} | info {} | solveAccepted {} | policy {}", name,
                     thermal::toString(report.status()), report.errorCount(), report.warningCount(), report.infoCount(),
                     report.solveAccepted() ? "yes" : "no", report.policyFingerprint));
    WARN(std::format("FP {} {:016x}", name, classificationDigest(report)));
}

[[nodiscard]] std::set<ValidationSubject> staleSubjects(const ThermalValidationReport& report) {
    std::set<ValidationSubject> out;
    for (const ValidationFinding& f : report.findings) {
        if (f.code == ValidationCode::SourceStale && f.subject.has_value()) {
            out.insert(*f.subject);
        }
    }
    return out;
}

/// Whether the report says that the object of @p subject was not made from the object it was presented with (the relation between two
/// derived objects, which a currentness check against the document alone does not state).
[[nodiscard]] bool mismatchOn(const ThermalValidationReport& report, ValidationSubject subject) {
    return std::ranges::any_of(report.findings, [subject](const ValidationFinding& f) {
        return f.code == ValidationCode::SourceMismatch && f.subject == std::optional{subject};
    });
}

void requireAllEvaluatedOrNotApplicable(const ThermalValidationReport& report) {
    for (const ThermalCheck& check : report.checks) {
        INFO(thermal::toString(check.rule));
        CHECK((report.statusOf(check.rule) == CheckStatus::Pass || report.statusOf(check.rule) == CheckStatus::NotApplicable));
    }
}

/// The slab: 400 K and 300 K on the two caps.
[[nodiscard]] std::vector<ThermalBoundaryCondition> slabConditions(const BoundaryStudy& study) {
    return {temperatureBc(1, study.startCap(), 400.0), temperatureBc(2, study.endCap(), 300.0)};
}

} // namespace

// ===========================================================================
// The end to end
// ===========================================================================

TEST_CASE("ThermalValidation_ValidSteadyAnalysisPasses", "[thermal][validation][endtoend][steady]") {
    // Everything at once on the real steel block: two prescribed caps, a convecting side, an applied side flux and generation.
    BoundaryStudy study;
    define(study, steadyDefinition(study, {surfaceLoad(1, study.side(1), 5.0e3), generationLoad(2, 5.0e4)},
                                   {temperatureBc(1, study.startCap(), 400.0), temperatureBc(2, study.endCap(), 330.0),
                                    convectionBc(3, study.side(2), 15.0, kAmbient)}));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->stoppedBecause.empty());
    REQUIRE(derived->balance.has_value());

    const ThermalValidationReport report = validate(study, derived.get());
    print("steady", report);
    CHECK(report.status() == ValidationStatus::Pass);
    CHECK(report.accepted());
    CHECK(report.solveAccepted());
    CHECK(report.inputsAccepted());
    CHECK(report.errorCount() == 0);
    CHECK(report.warningCount() == 0);
    CHECK(report.mode == ThermalAnalysisMode::Steady);
    requireAllEvaluatedOrNotApplicable(report);
    CHECK(report.statusOf(ValidationRule::TransientSetup) == CheckStatus::NotApplicable);   // honest: not a pass, not a fail
    for (const ValidationRule rule : thermal::kValidationRules) {
        if (rule != ValidationRule::TransientSetup) {
            CHECK(report.statusOf(rule) == CheckStatus::Pass);
        }
    }

    // The checks are in the rule order and are all there.
    REQUIRE(report.checks.size() == thermal::kValidationRuleCount);
    for (std::size_t i = 0; i < report.checks.size(); ++i) {
        CHECK(static_cast<std::size_t>(report.checks[i].rule) == i);
    }
    // The metrics are the qualified predecessors' numbers, present because every gate was actually wired.
    REQUIRE(report.metrics.steadyResidual.has_value());
    CHECK(*report.metrics.steadyResidual <= thermal::ThermalSolverSettings{}.relativeResidualTolerance);
    CHECK(report.metrics.residualTolerance == std::optional<double>{thermal::ThermalSolverSettings{}.relativeResidualTolerance});
    REQUIRE(report.metrics.worstBalanceResidual.has_value());
    CHECK(*report.metrics.worstBalanceResidual <= 1.0e-9);
    REQUIRE(report.metrics.tetCount.has_value());
    CHECK(*report.metrics.tetCount > 0);
    CHECK(report.metrics.dofCount == std::optional<std::size_t>{derived->steady->dofCount()});
    CHECK(report.metrics.freeEquations == std::optional<std::size_t>{derived->steady->freeEquations()});
    CHECK(report.metrics.constrainedCount == std::optional<std::size_t>{derived->steady->constrainedCount()});
    REQUIRE(report.metrics.minimumTemperature.has_value());
    CHECK(*report.metrics.minimumTemperature >= 0.0);   // information only
    REQUIRE(report.metrics.maximumHeatFlux.has_value());
    CHECK(*report.metrics.maximumHeatFlux > 0.0);
    CHECK(report.worstByMetric.size() == 4);
    CHECK(report.policyFingerprint == thermal::thermalValidationPolicyFingerprint());
    // The report knows what it describes, and is current for it.
    REQUIRE(report.source.has_value());
    CHECK(thermal::isCurrent(report, study.document, study.regenerator, study.mesher));
    // The report records the solution it judged, by value, and a report produced under another policy is not current.
    REQUIRE(report.resultSource.has_value());
    CHECK(*report.resultSource == thermal::postSourceOf(derived->steady->source()));
    ThermalValidationReport otherPolicy = report;
    otherPolicy.policyFingerprint = "p18-valid-v0:0000000000000000";
    CHECK_FALSE(thermal::isCurrent(otherPolicy, study.document, study.regenerator, study.mesher));
}

TEST_CASE("ThermalValidation_ValidTransientAnalysisPasses", "[thermal][validation][endtoend][transient]") {
    // The block starts at 300 K with its caps held at 400 K and 300 K and 2.4 W generated: 7 s steps to 100 s, a shortened last step.
    BoundaryStudy study;
    define(study, transientDefinition(study, {generationLoad(1, 1.0e5)}, slabConditions(study), 300.0, 7.0, 100.0));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->stoppedBecause.empty());
    REQUIRE(derived->transient.has_value());
    REQUIRE(derived->balance.has_value());

    const ThermalValidationReport report = validate(study, derived.get());
    print("transient", report);
    CHECK(report.status() == ValidationStatus::Pass);
    CHECK(report.accepted());
    CHECK(report.mode == ThermalAnalysisMode::Transient);
    for (const ValidationRule rule : thermal::kValidationRules) {
        CHECK(report.statusOf(rule) == CheckStatus::Pass);   // in a transient the transient setup IS evaluated
    }
    CHECK(report.metrics.transientStepCount == std::optional<std::size_t>{15});
    REQUIRE(report.metrics.worstTransientResidual.has_value());
    CHECK(*report.metrics.worstTransientResidual <= thermal::ThermalSolverSettings{}.relativeResidualTolerance);
    REQUIRE(report.metrics.worstResidualStep.has_value());
    REQUIRE(report.metrics.worstResidualStepSize.has_value());
    CHECK((*report.metrics.worstResidualStepSize == 7.0 || std::abs(*report.metrics.worstResidualStepSize - 2.0) < 1e-12));
    REQUIRE(report.metrics.worstBalanceResidual.has_value());
    REQUIRE(report.metrics.worstBalanceStep.has_value());
    CHECK(*report.metrics.worstBalanceStep >= 1);
    CHECK(*report.metrics.worstBalanceStep <= 15);
    CHECK(thermal::isCurrent(report, study.document, study.regenerator, study.mesher));
    // The caps are held at 400 K and 300 K while the body starts at 300 K: the 400 K cap overrides the initial temperature at t = 0. That
    // is INFORMATION -- the prescription wins, nothing is wrong -- and it belongs to the solve rule that reads the history.
    const auto overridden = firstWithCode(report.findings, ValidationCode::InitialConditionOverriddenByPrescription);
    REQUIRE(overridden.has_value());
    CHECK(overridden->severity == ValidationSeverity::Info);
    CHECK(overridden->rule == ValidationRule::SolverResidual);
    CHECK(overridden->count.value_or(0) >= 1);
}

TEST_CASE("ThermalValidation_ModeSpecificChecksAreNotApplicableCorrectly", "[thermal][validation][report][steady][transient]") {
    BoundaryStudy steadyStudy;
    define(steadyStudy, steadyDefinition(steadyStudy, {}, slabConditions(steadyStudy)));
    const std::unique_ptr<Derived> s = derive(steadyStudy);
    const ThermalValidationReport steady = validate(steadyStudy, s.get());
    CHECK(steady.statusOf(ValidationRule::TransientSetup) == CheckStatus::NotApplicable);
    CHECK(steady.checks[static_cast<std::size_t>(ValidationRule::TransientSetup)].state == CheckState::NotApplicable);

    BoundaryStudy transientStudy;
    define(transientStudy, transientDefinition(transientStudy, {}, slabConditions(transientStudy), 300.0, 50.0, 100.0));
    const std::unique_ptr<Derived> t = derive(transientStudy);
    const ThermalValidationReport transient = validate(transientStudy, t.get());
    CHECK(transient.statusOf(ValidationRule::TransientSetup) == CheckStatus::Pass);
    CHECK(transient.checks[static_cast<std::size_t>(ValidationRule::TransientSetup)].state == CheckState::Evaluated);
    // A steady solution presented for a transient analysis does not satisfy it, and the reverse.
    ThermalDerivedState confused = t->view();
    confused.transient = nullptr;
    confused.steady = s->steady.has_value() ? &*s->steady : nullptr;
    const ThermalValidationReport wrongMode = thermal::validateThermalAnalysis(transientStudy.document, transientStudy.regenerator,
                                                                               transientStudy.mesher, transientStudy.analysis, confused);
    CHECK_FALSE(wrongMode.accepted());
    CHECK(hasCode(wrongMode, ValidationCode::ModeMismatch));
}

namespace {

/// The block thinned to @p depthMillimetres: P16 meshes a thin plate with a handful of slender tetrahedra, which is where the thermal
/// mesh policy has something to say. REAL meshes, from the real mesher: nothing is hand-built.
void thin(BoundaryStudy& study, double depthMillimetres) {
    REQUIRE(study.document
                .modifyObject<features::ExtrudeFeature>(study.feature,
                                                        [depthMillimetres](features::ExtrudeFeature& extrude) -> Result<bool> {
                                                            auto definition = extrude.definition();
                                                            definition.depth = Length::fromSi(depthMillimetres / 1000.0);
                                                            return extrude.setDefinition(definition).has_value();
                                                        })
                .has_value());
    requireReport(study.regenerator, study.document);
    study.remesh();
}

} // namespace

TEST_CASE("ThermalValidation_AWarningOnlyAnalysisIsAcceptedWithWarnings", "[thermal][validation][endtoend][warning]") {
    // A 0.3 mm plate: P16 meshes it with twelve tetrahedra whose worst radius ratio (1.1e-4), aspect ratio (133) and maximum dihedral
    // angle (3.129 rad) are all outside the range of every qualified reference mesh -- and all inside the accuracy bounds. Structurally valid, solves,
    // balances. Accepted, with warnings.
    BoundaryStudy study;
    thin(study, 0.3);
    define(study, steadyDefinition(study, {}, slabConditions(study)));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->stoppedBecause.empty());
    REQUIRE(derived->balance.has_value());

    const ThermalValidationReport report = validate(study, derived.get());
    print("warning", report);
    CHECK(report.status() == ValidationStatus::PassWithWarnings);
    CHECK(report.accepted());
    CHECK(report.errorCount() == 0);
    CHECK(report.warningCount() >= 2);
    CHECK(report.statusOf(ValidationRule::MeshStructural) == CheckStatus::Pass);
    CHECK(report.statusOf(ValidationRule::MeshQuality) == CheckStatus::Warning);
    std::set<meshing::QualityMetric> warned;
    for (const ValidationFinding& finding : findingsOf(report, ValidationRule::MeshQuality)) {
        CHECK(finding.code == ValidationCode::ElementOutsideQualifiedEnvelope);
        CHECK(finding.severity == ValidationSeverity::Warning);
        REQUIRE(finding.metric.has_value());
        warned.insert(*finding.metric);
        CHECK(finding.element.has_value());
        CHECK(finding.value.has_value());
        CHECK(finding.threshold.has_value());
        CHECK(finding.count.value_or(0) >= 1);
    }
    CHECK(warned.contains(meshing::QualityMetric::TetRadiusRatio));
    CHECK(warned.contains(meshing::QualityMetric::TetAspectRatio));
    // A warning does not touch any other gate.
    for (const ThermalCheck& check : report.checks) {
        if (check.rule != ValidationRule::MeshQuality && check.rule != ValidationRule::TransientSetup) {
            CHECK(report.statusOf(check.rule) == CheckStatus::Pass);
        }
    }
}

TEST_CASE("ThermalValidation_ThermalMeshQualityRejectOnARealMesh", "[thermal][validation][endtoend][mesh][reject]") {
    // A 0.07 mm plate: FLATTENING. Its worst radius ratio is 5.9e-6, below the 7.0e-6 floor, while its aspect ratio (571) is inside the ceiling.
    // The WHOLE qualified chain runs to the end on it -- the solve succeeds, the residual passes, the books balance -- and the analysis is NOT
    // accepted, because "the solver said success" is not what accepts a result.
    BoundaryStudy study;
    thin(study, 0.07);
    define(study, steadyDefinition(study, {}, slabConditions(study)));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->stoppedBecause.empty());
    REQUIRE(derived->balance.has_value());
    CHECK(derived->balance->balanced());
    CHECK(derived->steady->residual().normalized <= 1.0e-9);

    const ThermalValidationReport report = validate(study, derived.get());
    print("quality-reject-radius", report);
    CHECK(report.status() == ValidationStatus::Fail);
    CHECK_FALSE(report.accepted());
    CHECK(report.statusOf(ValidationRule::MeshStructural) == CheckStatus::Pass);   // P16 does not refuse it: it is a valid tetrahedron mesh
    CHECK(report.statusOf(ValidationRule::MeshQuality) == CheckStatus::Fail);
    const auto error = firstWithCode(report.findings, ValidationCode::ElementBelowAccuracyFloor);
    REQUIRE(error.has_value());
    CHECK(error->metric == std::optional{meshing::QualityMetric::TetRadiusRatio});
    CHECK(error->value.value() < thermal::kThermalRadiusRatioFloor);
    CHECK(error->threshold == std::optional<double>{thermal::kThermalRadiusRatioFloor});
    CHECK(error->element.has_value());
    // Nothing is judged on the strength of the solver's success.
    CHECK(report.statusOf(ValidationRule::SolverResidual) == CheckStatus::Skipped);
    CHECK(report.checks[static_cast<std::size_t>(ValidationRule::SolverResidual)].blockedBy.has_value());
}

TEST_CASE("ThermalValidation_TheAspectRatioBoundCatchesWhatTheRadiusRatioCannotSee", "[thermal][validation][endtoend][mesh][reject]") {
    // A 0.02 mm plate: STRETCHING toward a line. Its worst radius ratio (1.2e-3) is ABOVE even the warning bound, so a radius-ratio-only policy
    // accepts it; its aspect ratio (2500) is far past the ceiling.
    BoundaryStudy study;
    thin(study, 0.02);
    define(study, steadyDefinition(study, {}, slabConditions(study)));
    const ThermalValidationReport report = validate(study);
    print("quality-reject-aspect", report);
    CHECK(report.status() == ValidationStatus::Fail);
    CHECK(report.statusOf(ValidationRule::MeshQuality) == CheckStatus::Fail);
    const auto error = firstWithCode(report.findings, ValidationCode::ElementBelowAccuracyFloor);
    REQUIRE(error.has_value());
    CHECK(error->metric == std::optional{meshing::QualityMetric::TetAspectRatio});
    CHECK(error->value.value() > thermal::kThermalAspectRatioCeiling);
    // And the radius ratio really is fine: the observation shows it, and no finding is about it.
    const auto radius = std::ranges::find_if(report.worstByMetric, [](const thermal::ThermalMetricObservation& o) {
        return o.metric == meshing::QualityMetric::TetRadiusRatio;
    });
    REQUIRE(radius != report.worstByMetric.end());
    CHECK(radius->value > 3.0e-4);
    CHECK(std::ranges::none_of(findingsOf(report, ValidationRule::MeshQuality),
                               [](const ValidationFinding& f) { return f.metric == meshing::QualityMetric::TetRadiusRatio; }));
}

TEST_CASE("ThermalValidation_QualifiedReferenceMeshesAreAcceptedWithoutAMeshFinding", "[thermal][validation][endtoend][mesh]") {
    // The block P16 qualified: no warning, no refusal, nothing to say about its mesh.
    BoundaryStudy study;
    define(study, steadyDefinition(study, {}, slabConditions(study)));
    const ThermalValidationReport report = validate(study);
    CHECK(report.statusOf(ValidationRule::MeshStructural) == CheckStatus::Pass);
    CHECK(report.statusOf(ValidationRule::MeshQuality) == CheckStatus::Pass);
    CHECK(findingsOf(report, ValidationRule::MeshQuality).empty());
    for (const thermal::ThermalMetricObservation& observation : report.worstByMetric) {
        // The worst of each metric on a qualified mesh sits clear of every failure bound (margins recorded in MESH_ACCEPTANCE.md).
        if (observation.metric == meshing::QualityMetric::TetRadiusRatio) {
            CHECK(observation.value > 100.0 * thermal::kThermalRadiusRatioFloor);
        }
        if (observation.metric == meshing::QualityMetric::TetAspectRatio) {
            CHECK(observation.value < thermal::kThermalAspectRatioCeiling / 10.0);
        }
    }
}
// ===========================================================================
// Geometry and mesh currentness
// ===========================================================================

TEST_CASE("ThermalValidation_RejectsAMissingAnalysis", "[thermal][validation][currentness]") {
    BoundaryStudy study;
    const ThermalValidationReport report = thermal::validateThermalAnalysis(study.document, study.regenerator, study.mesher,
                                                                           ThermalAnalysisId::fromValue(987654));
    CHECK(report.status() == ValidationStatus::Fail);
    CHECK(hasCode(report, ValidationCode::AnalysisNotFound));
    CHECK(report.statusOf(ValidationRule::AnalysisDefinition) == CheckStatus::Fail);
    for (std::size_t i = 1; i < thermal::kValidationRuleCount; ++i) {
        const auto rule = static_cast<ValidationRule>(i);
        CHECK(report.statusOf(rule) == CheckStatus::Skipped);
        CHECK(report.checks[i].blockedBy == std::optional{ValidationRule::AnalysisDefinition});
    }
    CHECK_FALSE(thermal::isCurrent(report, study.document, study.regenerator, study.mesher));
}

TEST_CASE("ThermalValidation_RejectsMissingMesh", "[thermal][validation][currentness][mesh]") {
    BoundaryStudy study;
    // A second control for the same body that was never generated, and an analysis on it.
    auto intent = meshing::MeshControl::create("Unmeshed", meshing::MeshControlDefinition{.body = study.feature});
    REQUIRE(intent.has_value());
    const MeshControlId other = MeshControlId::fromValue(require(study.document.addObject(std::move(*intent))).value());
    auto analysis = thermal::ThermalAnalysis::create("Second", thermal::ThermalAnalysisDefinition{.mesh = other});
    REQUIRE(analysis.has_value());
    const ThermalAnalysisId second = ThermalAnalysisId::fromValue(require(study.document.addObject(std::move(*analysis))).value());
    requireReport(study.regenerator, study.document);

    const ThermalValidationReport report =
        thermal::validateThermalAnalysis(study.document, study.regenerator, study.mesher, second);
    print("missing-mesh", report);
    CHECK(report.status() == ValidationStatus::Fail);
    CHECK(report.statusOf(ValidationRule::GeometryCurrent) == CheckStatus::Pass);
    CHECK(report.statusOf(ValidationRule::MeshCurrent) == CheckStatus::Fail);
    CHECK(hasCode(report, ValidationCode::MeshMissing));
    CHECK(report.statusOf(ValidationRule::MeshStructural) == CheckStatus::Skipped);
    CHECK(report.statusOf(ValidationRule::MeshQuality) == CheckStatus::Skipped);
    // The material does not need a mesh, and is still judged: a missing mesh does not hide anything else.
    CHECK(report.statusOf(ValidationRule::Material) == CheckStatus::Pass);
    // Nothing numerical is judged on a model that does not exist.
    for (const ValidationRule rule : {ValidationRule::Loads, ValidationRule::BoundaryConditions, ValidationRule::SourceCompatibility,
                                      ValidationRule::WellPosed, ValidationRule::SolverResidual, ValidationRule::ResultFinite,
                                      ValidationRule::PostResult, ValidationRule::HeatBalance}) {
        CHECK(report.statusOf(rule) == CheckStatus::Skipped);
    }
}

// ---------------------------------------------------------------------------
// The geometry rule and the system rule each refuse for a reason a real document can reach
// ---------------------------------------------------------------------------

TEST_CASE("ThermalValidation_RejectsAnAnalysisWhoseMeshingControlDoesNotExist", "[thermal][validation][currentness][geometry]") {
    BoundaryStudy study;
    auto orphan = thermal::ThermalAnalysis::create("Orphan", thermal::ThermalAnalysisDefinition{.mesh = MeshControlId::fromValue(987654)});
    INFO((orphan.has_value() ? std::string{} : orphan.error().message));
    REQUIRE(orphan.has_value());
    const ThermalAnalysisId id = ThermalAnalysisId::fromValue(require(study.document.addObject(std::move(*orphan))).value());

    const ThermalValidationReport report = thermal::validateThermalAnalysis(study.document, study.regenerator, study.mesher, id);
    requireWellFormed(report);
    print("missing-control", report);
    CHECK(report.status() == ValidationStatus::Fail);
    CHECK(report.statusOf(ValidationRule::GeometryCurrent) == CheckStatus::Fail);
    const auto finding = firstWithCode(report.findings, ValidationCode::ControlNotFound);
    REQUIRE(finding.has_value());
    CHECK(finding->severity == ValidationSeverity::Error);
    CHECK(finding->cause == "control_not_found");
    // Nothing about a mesh that cannot be named is judged.
    for (const ValidationRule rule : {ValidationRule::MeshCurrent, ValidationRule::MeshStructural, ValidationRule::MeshQuality}) {
        CHECK(report.statusOf(rule) == CheckStatus::Skipped);
        CHECK(report.checks[static_cast<std::size_t>(rule)].blockedBy == std::optional{ValidationRule::GeometryCurrent});
    }
}

TEST_CASE("ThermalValidation_RejectsABodyThatIsNoLongerInTheDocument", "[thermal][validation][currentness][geometry]") {
    // ORDERING IS PART OF THE CONTRACT: a current mesh is still HELD when the body goes, so a validator that asked the mesh first would
    // find nothing wrong with it. The geometry is judged first, and the mesh rule is skipped behind it.
    BoundaryStudy study;
    define(study, steadyDefinition(study, {}, slabConditions(study)));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(validate(study, derived.get()).accepted());

    REQUIRE(study.document.removeObject(study.feature).has_value());
    REQUIRE(study.mesher.mesh(study.control) != nullptr);

    const ThermalValidationReport report = validate(study, derived.get());
    print("body-removed", report);
    CHECK_FALSE(report.accepted());
    CHECK(report.statusOf(ValidationRule::GeometryCurrent) == CheckStatus::Fail);
    const auto finding = firstWithCode(report.findings, ValidationCode::GeometryIneligible);
    REQUIRE(finding.has_value());
    CHECK(finding->cause == "geometry_ineligible");
    CHECK(report.statusOf(ValidationRule::MeshCurrent) == CheckStatus::Skipped);
    CHECK(report.statusOf(ValidationRule::SourceCompatibility) == CheckStatus::Skipped);
    CHECK(report.statusOf(ValidationRule::SolverResidual) == CheckStatus::Skipped);
}

TEST_CASE("ThermalValidation_RejectsAGeometryEditThatWasNotRegenerated", "[thermal][validation][currentness][geometry]") {
    // The user edits the extrusion and nothing regenerates the feature tree: the body the mesh was cut from is not the body the document
    // now describes.
    BoundaryStudy study;
    define(study, steadyDefinition(study, {}, slabConditions(study)));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(validate(study, derived.get()).accepted());

    REQUIRE(study.document
                .modifyObject<features::ExtrudeFeature>(study.feature,
                                                        [](features::ExtrudeFeature& extrude) -> Result<bool> {
                                                            auto definition = extrude.definition();
                                                            definition.depth = Length::fromSi(0.035);
                                                            return extrude.setDefinition(definition).has_value();
                                                        })
                .has_value());

    const ThermalValidationReport report = validate(study, derived.get());
    print("geometry-not-regenerated", report);
    CHECK_FALSE(report.accepted());
    CHECK(report.status() == ValidationStatus::Fail);
    CHECK(hasCode(report, ValidationCode::GeometryIneligible));
    CHECK(report.statusOf(ValidationRule::GeometryCurrent) == CheckStatus::Fail);
    CHECK(report.statusOf(ValidationRule::SolverResidual) == CheckStatus::Skipped);
    CHECK_FALSE(thermal::isCurrent(report, study.document, study.regenerator, study.mesher));
}

TEST_CASE("ThermalValidation_RejectsAFailedGenerationOfEligibleGeometry", "[thermal][validation][currentness][mesh][generation]") {
    // A failed attempt is its own state: an older mesh survives it and would read as merely stale. The geometry stays ELIGIBLE (the
    // failure is an unresolvable local sizing reference), so the geometry rule passes and the mesh rule names the failed generation.
    BoundaryStudy study;
    define(study, steadyDefinition(study, {}, slabConditions(study)));
    REQUIRE(study.document
                .modifyObject<meshing::MeshControl>(
                    ObjectId::fromValue(study.control.value()),
                    [feature = study.feature](meshing::MeshControl& intent) -> Result<bool> {
                        auto definition = intent.definition();
                        definition.mesh.sizing.local.push_back(meshing::LocalMeshSizing{
                            .face = FaceName{feature, FaceSelector{.role = FaceRole::Side, .entity = EntityId::fromValue(987654U)}},
                            .targetSize = Length::fromSi(0.002)});
                        return intent.setDefinition(definition).has_value();
                    })
                .has_value());
    REQUIRE_FALSE(study.mesher.generate(study.document, study.regenerator, study.control).has_value());

    const ThermalValidationReport report = validate(study);
    print("generation-failed", report);
    CHECK_FALSE(report.accepted());
    CHECK(report.statusOf(ValidationRule::GeometryCurrent) == CheckStatus::Pass);
    CHECK(report.statusOf(ValidationRule::MeshCurrent) == CheckStatus::Fail);
    const auto finding = firstWithCode(report.findings, ValidationCode::MeshGenerationFailed);
    REQUIRE(finding.has_value());
    CHECK(finding->cause == "mesh_generation_failed");
    CHECK_FALSE(hasCode(report, ValidationCode::MeshStale));
    CHECK(report.statusOf(ValidationRule::MeshStructural) == CheckStatus::Skipped);
    CHECK(report.statusOf(ValidationRule::MeshQuality) == CheckStatus::Skipped);
}

TEST_CASE("ThermalValidation_RejectsAnAnalysisWithoutAnAssembledSystem", "[thermal][validation][wellposed][system]") {
    BoundaryStudy study;
    define(study, steadyDefinition(study, {}, slabConditions(study)));

    // Nothing derived is presented: the inputs are fit, and nothing was assembled to judge. That is a failure, never a pass.
    const ThermalValidationReport bare = validate(study);
    print("no-system", bare);
    CHECK_FALSE(bare.accepted());
    CHECK(bare.inputsAccepted());
    CHECK(hasCode(bare, ValidationCode::SystemMissing));
    CHECK(bare.statusOf(ValidationRule::WellPosed) == CheckStatus::Fail);
    for (const ValidationRule rule : {ValidationRule::SolverResidual, ValidationRule::ResultFinite, ValidationRule::PostResult, ValidationRule::HeatBalance}) {
        CHECK(bare.statusOf(rule) == CheckStatus::Skipped);
        CHECK(bare.checks[static_cast<std::size_t>(rule)].blockedBy == std::optional{ValidationRule::WellPosed});
    }

    // A solution presented without the system it was solved from is the same refusal.
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->balance.has_value());
    ThermalDerivedState partial = derived->view();
    partial.system = nullptr;
    const ThermalValidationReport report =
        thermal::validateThermalAnalysis(study.document, study.regenerator, study.mesher, study.analysis, partial);
    requireWellFormed(report);
    CHECK(hasCode(report, ValidationCode::SystemMissing));
    CHECK_FALSE(report.accepted());
    CHECK_FALSE(report.solveAccepted());
}

TEST_CASE("ThermalValidation_RejectsStaleMesh", "[thermal][validation][currentness][mesh][stale]") {
    BoundaryStudy study;
    define(study, steadyDefinition(study, {}, slabConditions(study)));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->steady.has_value());
    REQUIRE(validate(study, derived.get()).accepted());

    // The geometry is edited and regenerated; the mesh was built for the old body.
    REQUIRE(study.document
                .modifyObject<features::ExtrudeFeature>(study.feature,
                                                        [](features::ExtrudeFeature& extrude) -> Result<bool> {
                                                            auto definition = extrude.definition();
                                                            definition.depth = Length::fromSi(0.035);
                                                            return extrude.setDefinition(definition).has_value();
                                                        })
                .has_value());
    requireReport(study.regenerator, study.document);
    const ThermalValidationReport stale = validate(study, derived.get());
    print("stale-mesh", stale);
    CHECK(stale.status() == ValidationStatus::Fail);
    CHECK_FALSE(stale.accepted());
    CHECK(stale.statusOf(ValidationRule::GeometryCurrent) == CheckStatus::Pass);
    CHECK(stale.statusOf(ValidationRule::MeshCurrent) == CheckStatus::Fail);
    CHECK(hasCode(stale, ValidationCode::MeshStale));
    // The old solution, system, recovered fields and balance are not judged "ok" -- they are not judged at all.
    for (const ValidationRule rule : {ValidationRule::SourceCompatibility, ValidationRule::WellPosed, ValidationRule::SolverResidual,
                                      ValidationRule::ResultFinite, ValidationRule::PostResult, ValidationRule::HeatBalance}) {
        CHECK(stale.statusOf(rule) == CheckStatus::Skipped);
    }
    CHECK_FALSE(thermal::isCurrent(stale, study.document, study.regenerator, study.mesher));

    // Remeshed: the OLD result is still stale (the new mesh is a new generation), and a fresh derivation is accepted.
    study.remesh();
    const ThermalValidationReport afterRemesh = validate(study, derived.get());
    CHECK_FALSE(afterRemesh.accepted());
    CHECK(afterRemesh.statusOf(ValidationRule::MeshCurrent) == CheckStatus::Pass);
    CHECK(afterRemesh.statusOf(ValidationRule::SourceCompatibility) == CheckStatus::Fail);
    CHECK(validate(study, derive(study).get()).accepted());
}

TEST_CASE("ThermalValidation_RejectsAMeshWhoseSizingIntentMoved", "[thermal][validation][currentness][mesh][stale]") {
    BoundaryStudy study;
    define(study, steadyDefinition(study, {}, slabConditions(study)));
    REQUIRE(study.document
                .modifyObject<meshing::MeshControl>(
                    ObjectId::fromValue(study.control.value()),
                    [](meshing::MeshControl& control) -> Result<bool> {
                        meshing::MeshControlDefinition definition = control.definition();
                        definition.mesh.sizing.globalTargetSize = Length::fromSi(0.005);
                        return control.setDefinition(definition).has_value();
                    })
                .has_value());
    const ThermalValidationReport report = validate(study);
    CHECK(report.statusOf(ValidationRule::MeshCurrent) == CheckStatus::Fail);
    CHECK(hasCode(report, ValidationCode::MeshStale));
}

TEST_CASE("ThermalValidation_RejectsSameCountsNewMeshSource", "[thermal][validation][currentness][source][remesh]") {
    // THE HARD ADVERSARIAL CASE. A remesh of the same body with the same node count, the same tetrahedron count and the same NodeIds is a
    // DIFFERENT mesh. The old result has the right size, the right ids and plausible numbers, and is stale.
    BoundaryStudy study;
    define(study, steadyDefinition(study, {generationLoad(1, 1.0e5)}, {temperatureBc(1, study.startCap(), 300.0), convectionBc(2, study.endCap(), 25.0, kAmbient)}));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->balance.has_value());
    REQUIRE(validate(study, derived.get()).accepted());

    const std::size_t nodesBefore = study.volume().mesh().nodes().size();
    const std::size_t tetsBefore = study.volume().mesh().tetrahedra().size();
    std::vector<NodeId::ValueType> idsBefore;
    for (const meshing::Node& node : study.volume().mesh().nodes()) {
        idsBefore.push_back(node.id.value());
    }
    const meshing::MeshStamp stampBefore = study.volume().mesh().stamp();

    study.remesh();   // nothing else changed
    REQUIRE(study.volume().mesh().nodes().size() == nodesBefore);
    REQUIRE(study.volume().mesh().tetrahedra().size() == tetsBefore);
    std::vector<NodeId::ValueType> idsAfter;
    for (const meshing::Node& node : study.volume().mesh().nodes()) {
        idsAfter.push_back(node.id.value());
    }
    REQUIRE(idsAfter == idsBefore);                                   // the same NodeIds, in the same order
    REQUIRE_FALSE(study.volume().mesh().stamp() == stampBefore);    // and a different generation

    const ThermalValidationReport report = validate(study, derived.get());
    print("same-counts-new-source", report);
    CHECK(report.status() == ValidationStatus::Fail);
    CHECK(report.statusOf(ValidationRule::MeshCurrent) == CheckStatus::Pass);   // the NEW mesh is current
    CHECK(report.statusOf(ValidationRule::SourceCompatibility) == CheckStatus::Fail);
    // Every derived object that carries the old generation is named, with the dependency that moved.
    const std::set<ValidationSubject> expected{ValidationSubject::DofMap,    ValidationSubject::PreparedLoads, ValidationSubject::PreparedBoundary,
                                               ValidationSubject::System,    ValidationSubject::Solution,      ValidationSubject::PostResult,
                                               ValidationSubject::BalanceResult};
    CHECK(staleSubjects(report) == expected);
    for (const ValidationFinding& finding : report.findings) {
        if (finding.code == ValidationCode::SourceStale) {
            CHECK((finding.message.find("mesh") != std::string::npos || finding.message.find("numbering") != std::string::npos));
        }
    }
    // Nothing downstream is "passed" on the strength of matching sizes.
    CHECK(report.statusOf(ValidationRule::WellPosed) == CheckStatus::Skipped);
    CHECK(report.statusOf(ValidationRule::SolverResidual) == CheckStatus::Skipped);
    CHECK(report.statusOf(ValidationRule::HeatBalance) == CheckStatus::Skipped);
}

TEST_CASE("ThermalValidation_RejectsSameIdsNewMeshSource", "[thermal][validation][currentness][source][remesh]") {
    // The same adversary through a different door: the recovered fields and the balance alone are presented with the new generation's
    // model. Their NodeIds and ElementIds all exist in the new mesh. They are still the old mesh's.
    BoundaryStudy study;
    define(study, steadyDefinition(study, {}, slabConditions(study)));
    const std::unique_ptr<Derived> old = derive(study);
    study.remesh();
    const std::unique_ptr<Derived> fresh = derive(study);
    REQUIRE(validate(study, fresh.get()).accepted());

    ThermalDerivedState mixed = fresh->view();
    mixed.post = &*old->post;
    const ThermalValidationReport onlyPost = thermal::validateThermalAnalysis(study.document, study.regenerator, study.mesher, study.analysis, mixed);
    CHECK_FALSE(onlyPost.accepted());
    CHECK(staleSubjects(onlyPost) == std::set<ValidationSubject>{ValidationSubject::PostResult});
    CHECK(mismatchOn(onlyPost, ValidationSubject::PostResult));   // and it was not recovered from the solution presented with it

    mixed = fresh->view();
    mixed.balance = &*old->balance;
    const ThermalValidationReport onlyBalance = thermal::validateThermalAnalysis(study.document, study.regenerator, study.mesher, study.analysis, mixed);
    CHECK_FALSE(onlyBalance.accepted());
    CHECK(staleSubjects(onlyBalance) == std::set<ValidationSubject>{ValidationSubject::BalanceResult});
    CHECK(mismatchOn(onlyBalance, ValidationSubject::BalanceResult));

    mixed = fresh->view();
    mixed.steady = &*old->steady;
    const ThermalValidationReport onlySolution = thermal::validateThermalAnalysis(study.document, study.regenerator, study.mesher, study.analysis, mixed);
    CHECK_FALSE(onlySolution.accepted());
    CHECK(staleSubjects(onlySolution).contains(ValidationSubject::Solution));
}

TEST_CASE("ThermalValidation_RejectsMismatchedAssemblySource", "[thermal][validation][currentness][source]") {
    // Two different load lists, same sizes: the system assembled for the other list is refused even though every dimension agrees.
    BoundaryStudy study;
    define(study, steadyDefinition(study, {generationLoad(1, 1.0e5)}, {temperatureBc(1, study.startCap(), 300.0)}));
    const std::unique_ptr<Derived> a = derive(study);
    define(study, steadyDefinition(study, {generationLoad(1, 2.0e5)}, {temperatureBc(1, study.startCap(), 300.0)}));
    const std::unique_ptr<Derived> b = derive(study);
    REQUIRE(validate(study, b.get()).accepted());

    ThermalDerivedState mixed = b->view();
    mixed.system = &*a->system;
    const ThermalValidationReport report = thermal::validateThermalAnalysis(study.document, study.regenerator, study.mesher, study.analysis, mixed);
    CHECK_FALSE(report.accepted());
    CHECK(report.statusOf(ValidationRule::SourceCompatibility) == CheckStatus::Fail);
    CHECK(staleSubjects(report).contains(ValidationSubject::System));
    // The solution of B was not computed from A's system: a mismatch between the two objects, whatever the document says.
    CHECK(hasCode(report, ValidationCode::SourceMismatch));
    CHECK(mismatchOn(report, ValidationSubject::Solution));   // named for the SOLUTION, not only for the loads that happen to differ
    CHECK(mismatchOn(report, ValidationSubject::PreparedLoads));   // and for the prepared loads, which belong to the other system

    // The same relation for a history: the transient of B was not computed from the system assembled for A.
    const thermal::ThermalAnalysisDefinition historyA =
        transientDefinition(study, {generationLoad(1, 1.0e5)}, {temperatureBc(1, study.startCap(), 300.0)}, 300.0, 25.0, 100.0);
    const thermal::ThermalAnalysisDefinition historyB =
        transientDefinition(study, {generationLoad(1, 2.0e5)}, {temperatureBc(1, study.startCap(), 300.0)}, 300.0, 25.0, 100.0);
    define(study, historyA);
    const std::unique_ptr<Derived> ta = derive(study);
    define(study, historyB);
    const std::unique_ptr<Derived> tb = derive(study);
    REQUIRE(validate(study, tb.get()).accepted());
    ThermalDerivedState mixedHistory = tb->view();
    mixedHistory.system = &*ta->system;
    const ThermalValidationReport historyReport =
        thermal::validateThermalAnalysis(study.document, study.regenerator, study.mesher, study.analysis, mixedHistory);
    CHECK_FALSE(historyReport.accepted());
    CHECK(historyReport.statusOf(ValidationRule::SourceCompatibility) == CheckStatus::Fail);
    CHECK(mismatchOn(historyReport, ValidationSubject::Solution));
}

// ===========================================================================
// The currentness matrix: what each edit stales
// ===========================================================================

namespace {

struct Edit {
    std::string name;
    std::set<ValidationSubject> expected;
    std::function<void(BoundaryStudy&)> apply;
    /// Whether the mesh survives the edit (it does for everything but geometry and meshing intent).
    bool meshStillCurrent = true;
};

} // namespace

TEST_CASE("ThermalValidation_EveryEditStalesExactlyWhatDependsOnIt", "[thermal][validation][currentness][matrix][steady]") {
    using Subjects = std::set<ValidationSubject>;
    const Subjects all{ValidationSubject::System, ValidationSubject::Solution, ValidationSubject::PostResult, ValidationSubject::BalanceResult};
    const std::vector<Edit> edits{
        {"a load edit",
         Subjects{ValidationSubject::PreparedLoads, ValidationSubject::System, ValidationSubject::Solution, ValidationSubject::PostResult,
                  ValidationSubject::BalanceResult},
         [](BoundaryStudy& s) {
             auto d = s.definition();
             d.loads = {generationLoad(1, 2.0e5)};
             define(s, d);
         }},
        {"a prescribed-temperature edit",
         Subjects{ValidationSubject::PreparedBoundary, ValidationSubject::Solution, ValidationSubject::PostResult, ValidationSubject::BalanceResult},
         [](BoundaryStudy& s) {
             auto d = s.definition();
             d.boundaryConditions[0] = temperatureBc(1, s.startCap(), 410.0);
             define(s, d);
         }},
        {"a convection edit",
         Subjects{ValidationSubject::PreparedBoundary, ValidationSubject::System, ValidationSubject::Solution, ValidationSubject::PostResult,
                  ValidationSubject::BalanceResult},
         [](BoundaryStudy& s) {
             auto d = s.definition();
             d.boundaryConditions[1] = convectionBc(2, s.endCap(), 40.0, kAmbient);
             define(s, d);
         }},
        {"a solver-settings edit",
         Subjects{ValidationSubject::Solution, ValidationSubject::PostResult, ValidationSubject::BalanceResult},
         [](BoundaryStudy& s) {
             auto d = s.definition();
             d.solver.relativeResidualTolerance = 1.0e-10;
             define(s, d);
         }},
        {"a conductivity edit",
         all,
         [](BoundaryStudy& s) {
             materials::ThermalProperties thermalProperties;
             thermalProperties.thermalConductivity = materials::MaterialProperty<ThermalConductivity>::known(ThermalConductivity::fromSi(50.0));
             thermalProperties.specificHeatCapacity = materials::MaterialProperty<SpecificHeatCapacity>::known(SpecificHeatCapacity::fromSi(460.0));
             REQUIRE(features::setMaterialThermal(s.document, s.material, thermalProperties).has_value());
         }},
    };
    for (const Edit& edit : edits) {
        SECTION(edit.name) {
            BoundaryStudy study;
            define(study, steadyDefinition(study, {generationLoad(1, 1.0e5)},
                                           {temperatureBc(1, study.startCap(), 400.0), convectionBc(2, study.endCap(), 25.0, kAmbient)}));
            const std::unique_ptr<Derived> derived = derive(study);
            REQUIRE(derived->balance.has_value());
            REQUIRE(validate(study, derived.get()).accepted());
            const ThermalValidationReport before = validate(study, derived.get());

            edit.apply(study);
            // Material, load, condition and setting edits never touch the mesh: nothing is remeshed.
            CHECK(study.mesher.currency(study.document, study.control) == meshing::MeshCurrency::Current);

            const ThermalValidationReport report = validate(study, derived.get());
            WARN(std::format("MATRIX {} | stale: {}", edit.name, [&] {
                std::string names;
                for (const ValidationSubject subject : staleSubjects(report)) {
                    names += std::string{thermal::toString(subject)} + " ";
                }
                return names;
            }()));
            CHECK_FALSE(report.accepted());
            CHECK(staleSubjects(report) == edit.expected);
            // The old report is stale too.
            CHECK_FALSE(thermal::isCurrent(before, study.document, study.regenerator, study.mesher));
        }
    }
}

TEST_CASE("ThermalValidation_EveryTransientEditStalesTheHistory", "[thermal][validation][currentness][matrix][transient]") {
    using Subjects = std::set<ValidationSubject>;
    const Subjects history{ValidationSubject::Solution, ValidationSubject::PostResult, ValidationSubject::BalanceResult};
    const std::vector<Edit> edits{
        {"an initial-temperature edit", history,
         [](BoundaryStudy& s) {
             auto d = s.definition();
             d.initialCondition = thermal::UniformInitialTemperature{Temperature::fromSi(310.0)};
             define(s, d);
         }},
        {"a time-step edit", history,
         [](BoundaryStudy& s) {
             auto d = s.definition();
             d.transient->timeStep = Time::fromSi(10.0);
             define(s, d);
         }},
        {"an end-time edit", history,
         [](BoundaryStudy& s) {
             auto d = s.definition();
             d.transient->endTime = Time::fromSi(90.0);
             define(s, d);
         }},
        {"an output-schedule edit", history,
         [](BoundaryStudy& s) {
             auto d = s.definition();
             d.transient->output = thermal::TransientOutputPolicy::FinalOnly;
             define(s, d);
         }},
        {"a density edit",
         Subjects{ValidationSubject::System, ValidationSubject::Solution, ValidationSubject::PostResult, ValidationSubject::BalanceResult},
         [](BoundaryStudy& s) {
             materials::MechanicalProperties mechanical;
             mechanical.density = materials::MaterialProperty<Density>::known(Density::fromSi(7900.0));
             REQUIRE(features::setMaterialMechanical(s.document, s.material, mechanical).has_value());
         }},
        {"a specific-heat edit",
         Subjects{ValidationSubject::System, ValidationSubject::Solution, ValidationSubject::PostResult, ValidationSubject::BalanceResult},
         [](BoundaryStudy& s) {
             materials::ThermalProperties thermalProperties;
             thermalProperties.thermalConductivity = materials::MaterialProperty<ThermalConductivity>::known(ThermalConductivity::fromSi(45.0));
             thermalProperties.specificHeatCapacity = materials::MaterialProperty<SpecificHeatCapacity>::known(SpecificHeatCapacity::fromSi(490.0));
             REQUIRE(features::setMaterialThermal(s.document, s.material, thermalProperties).has_value());
         }},
    };
    for (const Edit& edit : edits) {
        SECTION(edit.name) {
            BoundaryStudy study;
            define(study, transientDefinition(study, {generationLoad(1, 1.0e5)}, slabConditions(study), 300.0, 20.0, 100.0));
            const std::unique_ptr<Derived> derived = derive(study);
            REQUIRE(derived->balance.has_value());
            REQUIRE(validate(study, derived.get()).accepted());
            edit.apply(study);
            CHECK(study.mesher.currency(study.document, study.control) == meshing::MeshCurrency::Current);
            const ThermalValidationReport report = validate(study, derived.get());
            WARN(std::format("MATRIX transient {} | stale: {}", edit.name, [&] {
                std::string names;
                for (const ValidationSubject subject : staleSubjects(report)) {
                    names += std::string{thermal::toString(subject)} + " ";
                }
                return names;
            }()));
            CHECK_FALSE(report.accepted());
            CHECK(staleSubjects(report) == edit.expected);
        }
    }
}

TEST_CASE("ThermalValidation_InputEditStalesTheReportButAnUnrelatedEditDoesNot", "[thermal][validation][lifecycle][currentness]") {
    BoundaryStudy study;
    define(study, steadyDefinition(study, {}, slabConditions(study)));
    const std::unique_ptr<Derived> derived = derive(study);
    const ThermalValidationReport report = validate(study, derived.get());
    REQUIRE(report.accepted());
    REQUIRE(thermal::isCurrent(report, study.document, study.regenerator, study.mesher));

    // An edit that is not about this analysis -- an unrelated sketch in the document -- changes none of its sources.
    auto sketch = std::make_unique<sketch::Sketch>("Unrelated", Frame3D::xy());
    (void)require(study.document.addObject(std::move(sketch)));
    CHECK(thermal::isCurrent(report, study.document, study.regenerator, study.mesher));
    CHECK(validate(study, derived.get()).accepted());

    // A load edit stales it: the report that said PASS is no longer current, and says nothing about the new state.
    auto edited = study.definition();
    edited.loads = {generationLoad(1, 5.0e4)};
    define(study, edited);
    CHECK_FALSE(thermal::isCurrent(report, study.document, study.regenerator, study.mesher));
    // Editing it BACK does not resurrect the old report: the analysis revision is part of the source and it has moved twice. Currentness
    // is identity, not "the same values again".
    define(study, steadyDefinition(study, {}, slabConditions(study)));
    CHECK_FALSE(thermal::isCurrent(report, study.document, study.regenerator, study.mesher));
}

TEST_CASE("ThermalValidation_FailedResolveDoesNotRevalidateOldResult", "[thermal][validation][lifecycle]") {
    BoundaryStudy study;
    define(study, steadyDefinition(study, {}, slabConditions(study)));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(validate(study, derived.get()).accepted());

    // The attempt to solve AGAIN failed. A previous result exists and is, for this unchanged source, perfectly current -- and it cannot
    // rescue the analysis: a failure is never a current result.
    const Error failure{ErrorCode::FailedPrecondition, "the factorisation measured a singular system"};
    const ThermalValidationReport report = validate(study, derived.get(), &failure);
    print("failed-resolve", report);
    CHECK(report.status() == ValidationStatus::Fail);
    CHECK_FALSE(report.accepted());
    CHECK(hasCode(report, ValidationCode::SolveFailed));
    CHECK(report.statusOf(ValidationRule::SolverResidual) == CheckStatus::Fail);
    CHECK(report.statusOf(ValidationRule::ResultFinite) == CheckStatus::Skipped);
    CHECK(report.statusOf(ValidationRule::PostResult) == CheckStatus::Skipped);
    CHECK(report.statusOf(ValidationRule::HeatBalance) == CheckStatus::Skipped);
    CHECK_THAT(findingsOf(report, ValidationRule::SolverResidual)[0].message, ContainsSubstring("singular"));
    // The same state without a recorded failure is accepted: it is the failure that decides.
    CHECK(validate(study, derived.get()).accepted());
}

TEST_CASE("ThermalValidation_ANewSolveThatFailsAfterAnEditLeavesNothingAccepted", "[thermal][validation][lifecycle][stale]") {
    BoundaryStudy study;
    define(study, steadyDefinition(study, {}, slabConditions(study)));
    const std::unique_ptr<Derived> old = derive(study);
    REQUIRE(validate(study, old.get()).accepted());
    auto edited = study.definition();
    edited.boundaryConditions[0] = temperatureBc(1, study.startCap(), 420.0);
    define(study, edited);
    // The old result is stale; a new solve attempt fails; neither is accepted.
    const Error failure{ErrorCode::FailedPrecondition, "the new solve failed"};
    const ThermalValidationReport report = validate(study, old.get(), &failure);
    CHECK_FALSE(report.accepted());
    CHECK(report.statusOf(ValidationRule::SourceCompatibility) == CheckStatus::Fail);
    // And with no result at all, only the failure: still nothing accepted.
    const ThermalValidationReport bare = validate(study, nullptr, &failure);
    CHECK_FALSE(bare.accepted());
}

// ===========================================================================
// Materials
// ===========================================================================

TEST_CASE("ThermalValidation_SteadyRequiresConductivityOnly", "[thermal][validation][material][steady]") {
    // A material with only a conductivity serves a steady analysis; the density and specific heat are NOT required of it.
    BoundaryStudy study;
    REQUIRE(features::removeMaterialProperty(study.document, study.material, materials::MechanicalPropertyKind::Density).has_value());
    REQUIRE(features::removeMaterialProperty(study.document, study.material, materials::ThermalPropertyKind::SpecificHeatCapacity).has_value());
    define(study, steadyDefinition(study, {}, slabConditions(study)));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->balance.has_value());
    const ThermalValidationReport report = validate(study, derived.get());
    print("steady-k-only", report);
    CHECK(report.statusOf(ValidationRule::Material) == CheckStatus::Pass);
    CHECK(report.status() == ValidationStatus::Pass);
    CHECK(report.accepted());
}

TEST_CASE("ThermalValidation_TransientRequiresConductivityDensitySpecificHeat", "[thermal][validation][material][transient]") {
    SECTION("k only is not enough") {
        BoundaryStudy study;
        REQUIRE(features::removeMaterialProperty(study.document, study.material, materials::MechanicalPropertyKind::Density).has_value());
        REQUIRE(features::removeMaterialProperty(study.document, study.material, materials::ThermalPropertyKind::SpecificHeatCapacity).has_value());
        define(study, transientDefinition(study, {}, slabConditions(study), 300.0, 20.0, 100.0));
        const ThermalValidationReport report = validate(study);
        print("transient-k-only", report);
        CHECK(report.status() == ValidationStatus::Fail);
        CHECK(report.statusOf(ValidationRule::Material) == CheckStatus::Fail);
        CHECK(hasCode(report, ValidationCode::MaterialIncompleteForMode));
        CHECK_FALSE(hasCode(report, ValidationCode::MaterialConductivityUnusable));
        // The inputs that do not depend on the material are still judged: nothing is hidden by the missing density.
        CHECK(report.statusOf(ValidationRule::Loads) == CheckStatus::Pass);
        CHECK(report.statusOf(ValidationRule::BoundaryConditions) == CheckStatus::Pass);
        CHECK(report.statusOf(ValidationRule::TransientSetup) == CheckStatus::Pass);
        // Nothing derived is judged.
        CHECK(report.statusOf(ValidationRule::SourceCompatibility) == CheckStatus::Skipped);
        CHECK(report.checks[static_cast<std::size_t>(ValidationRule::SourceCompatibility)].blockedBy == std::optional{ValidationRule::Material});
        // And the same material still serves the steady analysis.
        define(study, steadyDefinition(study, {}, slabConditions(study)));
        CHECK(validate(study).statusOf(ValidationRule::Material) == CheckStatus::Pass);
    }
    SECTION("a missing specific heat alone") {
        BoundaryStudy study;
        REQUIRE(features::removeMaterialProperty(study.document, study.material, materials::ThermalPropertyKind::SpecificHeatCapacity).has_value());
        define(study, transientDefinition(study, {}, slabConditions(study), 300.0, 20.0, 100.0));
        CHECK(hasCode(validate(study), ValidationCode::MaterialIncompleteForMode));
    }
    SECTION("a missing density alone") {
        BoundaryStudy study;
        REQUIRE(features::removeMaterialProperty(study.document, study.material, materials::MechanicalPropertyKind::Density).has_value());
        define(study, transientDefinition(study, {}, slabConditions(study), 300.0, 20.0, 100.0));
        CHECK(hasCode(validate(study), ValidationCode::MaterialIncompleteForMode));
    }
    SECTION("k, rho and cp together are accepted") {
        BoundaryStudy study;
        define(study, transientDefinition(study, {}, slabConditions(study), 300.0, 20.0, 100.0));
        const std::unique_ptr<Derived> derived = derive(study);
        REQUIRE(derived->balance.has_value());
        const ThermalValidationReport report = validate(study, derived.get());
        CHECK(report.statusOf(ValidationRule::Material) == CheckStatus::Pass);
        CHECK(report.accepted());
    }
}

TEST_CASE("ThermalValidation_RejectsMissingMaterial", "[thermal][validation][material]") {
    BoundaryStudy study;
    define(study, steadyDefinition(study, {}, slabConditions(study)));
    REQUIRE(features::removeMaterialAssignment(study.document).has_value());
    const ThermalValidationReport report = validate(study);
    print("no-material", report);
    CHECK(report.status() == ValidationStatus::Fail);
    CHECK(report.statusOf(ValidationRule::GeometryCurrent) == CheckStatus::Pass);
    CHECK(report.statusOf(ValidationRule::MeshCurrent) == CheckStatus::Pass);
    CHECK(report.statusOf(ValidationRule::MeshStructural) == CheckStatus::Pass);   // the mesh is judged: the material does not hide it
    CHECK(report.statusOf(ValidationRule::MeshQuality) == CheckStatus::Pass);
    CHECK(report.statusOf(ValidationRule::Material) == CheckStatus::Fail);
    CHECK(hasCode(report, ValidationCode::MaterialMissing));
    CHECK(report.statusOf(ValidationRule::Loads) == CheckStatus::Skipped);          // there is no model without a conductivity
    CHECK(report.statusOf(ValidationRule::SolverResidual) == CheckStatus::Skipped);
}

TEST_CASE("ThermalValidation_RejectsAMaterialWithoutAConductivity", "[thermal][validation][material]") {
    BoundaryStudy study;
    define(study, steadyDefinition(study, {}, slabConditions(study)));
    // P15 may refuse to drop the conductivity; if it allows it, the validator must refuse the analysis, in either mode.
    if (features::removeMaterialProperty(study.document, study.material, materials::ThermalPropertyKind::ThermalConductivity).has_value()) {
        const ThermalValidationReport report = validate(study);
        CHECK(report.status() == ValidationStatus::Fail);
        CHECK(report.statusOf(ValidationRule::Material) == CheckStatus::Fail);
        CHECK(hasCode(report, ValidationCode::MaterialConductivityUnusable));
    } else {
        SUCCEED("P15 does not allow a material to lose its conductivity; nothing for the validator to see");
    }
}

TEST_CASE("ThermalValidation_NonFiniteAndNonPositiveThermalPropertiesNeverReachTheDocument", "[thermal][validation][material][upstream]") {
    // The checklist asks that nonfinite and nonpositive required properties are rejected. P15 makes them UNREPRESENTABLE: creation and
    // every edit refuse them, so the document -- and therefore the validator -- never holds one. The refusal is shown, and the document is
    // shown unchanged by it.
    BoundaryStudy study;
    define(study, steadyDefinition(study, {}, slabConditions(study)));
    const std::uint64_t revision = study.document.revision();
    for (const double bad : {0.0, -3.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        materials::ThermalProperties broken;
        broken.thermalConductivity = materials::MaterialProperty<ThermalConductivity>::known(ThermalConductivity::fromSi(bad));
        CHECK_FALSE(features::setMaterialThermal(study.document, study.material, broken).has_value());
        materials::ThermalProperties brokenCp;
        brokenCp.thermalConductivity = materials::MaterialProperty<ThermalConductivity>::known(ThermalConductivity::fromSi(45.0));
        brokenCp.specificHeatCapacity = materials::MaterialProperty<SpecificHeatCapacity>::known(SpecificHeatCapacity::fromSi(bad));
        CHECK_FALSE(features::setMaterialThermal(study.document, study.material, brokenCp).has_value());
        materials::MechanicalProperties brokenRho;
        brokenRho.density = materials::MaterialProperty<Density>::known(Density::fromSi(bad));
        CHECK_FALSE(features::setMaterialMechanical(study.document, study.material, brokenRho).has_value());
    }
    CHECK(study.document.revision() == revision);
    CHECK(validate(study, derive(study).get()).accepted());
}

// ===========================================================================
// Loads and boundary conditions
// ===========================================================================

TEST_CASE("ThermalValidation_RejectsUnresolvedLoad", "[thermal][validation][loads][unresolved]") {
    BoundaryStudy study;
    // A well-formed face name that names no face of the body: nothing nearby is substituted.
    const FaceName nowhere{study.feature, FaceSelector{.role = FaceRole::Side, .entity = EntityId::fromValue(987654)}};
    define(study, steadyDefinition(study, {surfaceLoad(1, nowhere, 1.0e4)}, slabConditions(study)));
    const ThermalValidationReport report = validate(study);
    print("unresolved-load", report);
    CHECK(report.status() == ValidationStatus::Fail);
    CHECK(report.statusOf(ValidationRule::Loads) == CheckStatus::Fail);
    const auto finding = firstWithCode(report.findings, ValidationCode::LoadsInvalid);
    REQUIRE(finding.has_value());
    CHECK(finding->cause == "target_unresolved");
    CHECK(finding->severity == ValidationSeverity::Error);
    // The conditions are judged on their own and are fine; nothing numerical is judged.
    CHECK(report.statusOf(ValidationRule::BoundaryConditions) == CheckStatus::Pass);
    CHECK(report.statusOf(ValidationRule::SourceCompatibility) == CheckStatus::Skipped);
}

TEST_CASE("ThermalValidation_NoLoadsIsNotAnError", "[thermal][validation][loads]") {
    BoundaryStudy study;
    define(study, steadyDefinition(study, {}, slabConditions(study)));
    const ThermalValidationReport report = validate(study);
    CHECK(report.statusOf(ValidationRule::Loads) == CheckStatus::Pass);
    const auto info = firstWithCode(report.findings, ValidationCode::NoLoadsConfigured);
    REQUIRE(info.has_value());
    CHECK(info->severity == ValidationSeverity::Info);
    CHECK(errorsIn(findingsOf(report, ValidationRule::Loads)) == 0);   // (nothing was derived here, so other rules have their own say)
}

TEST_CASE("ThermalValidation_AZeroMagnitudeLoadIsValid", "[thermal][validation][loads][zero]") {
    // A zero flux through a face that resolves is a load of zero, not an unresolved one; the analysis is accepted.
    BoundaryStudy study;
    define(study, steadyDefinition(study, {surfaceLoad(1, study.side(0), 0.0), generationLoad(2, 0.0)}, slabConditions(study)));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->balance.has_value());
    const ThermalValidationReport report = validate(study, derived.get());
    CHECK(report.statusOf(ValidationRule::Loads) == CheckStatus::Pass);
    CHECK_FALSE(hasCode(report, ValidationCode::NoLoadsConfigured));   // two loads ARE configured
    CHECK(report.accepted());
}

TEST_CASE("ThermalValidation_RejectsConflictingTemperatureBC", "[thermal][validation][bcs][conflict]") {
    BoundaryStudy study;
    // The same face prescribed at two different temperatures.
    define(study, steadyDefinition(study, {}, {temperatureBc(1, study.startCap(), 400.0), temperatureBc(2, study.startCap(), 350.0),
                                              temperatureBc(3, study.endCap(), 300.0)}));
    const ThermalValidationReport report = validate(study);
    print("conflicting-bc", report);
    CHECK(report.status() == ValidationStatus::Fail);
    CHECK(report.statusOf(ValidationRule::BoundaryConditions) == CheckStatus::Fail);
    const auto finding = firstWithCode(report.findings, ValidationCode::BoundaryConditionsInvalid);
    REQUIRE(finding.has_value());
    CHECK(finding->cause == "conflicting_temperature");
    CHECK(report.statusOf(ValidationRule::Loads) == CheckStatus::Pass);
    CHECK(report.statusOf(ValidationRule::SolverResidual) == CheckStatus::Skipped);   // no solver acceptance
}

TEST_CASE("ThermalValidation_IdenticalDuplicateTemperatureBCIsNotAConflict", "[thermal][validation][bcs][duplicate]") {
    BoundaryStudy study;
    define(study, steadyDefinition(study, {}, {temperatureBc(1, study.startCap(), 400.0), temperatureBc(2, study.startCap(), 400.0),
                                              temperatureBc(3, study.endCap(), 300.0)}));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->balance.has_value());
    CHECK(validate(study, derived.get()).accepted());
}

TEST_CASE("ThermalValidation_RejectsUnresolvedBoundaryCondition", "[thermal][validation][bcs][unresolved]") {
    BoundaryStudy study;
    const FaceName nowhere{study.feature, FaceSelector{.role = FaceRole::Side, .entity = EntityId::fromValue(987654)}};
    define(study, steadyDefinition(study, {}, {temperatureBc(1, study.startCap(), 400.0), convectionBc(2, nowhere, 25.0, kAmbient)}));
    const ThermalValidationReport report = validate(study);
    CHECK(report.status() == ValidationStatus::Fail);
    const auto finding = firstWithCode(report.findings, ValidationCode::BoundaryConditionsInvalid);
    REQUIRE(finding.has_value());
    CHECK(finding->cause == "target_unresolved");
}

TEST_CASE("ThermalValidation_InvalidConvectionCoefficientNeverReachesTheDocument", "[thermal][validation][bcs][upstream]") {
    // The canonical rule is that a convection coefficient is finite and strictly POSITIVE (h = 0 is an adiabatic face wearing a convection
    // record). The definition refuses it at entry, so no analysis the validator sees can carry one.
    BoundaryStudy study;
    for (const double bad : {0.0, -5.0, std::numeric_limits<double>::quiet_NaN()}) {
        auto d = steadyDefinition(study, {}, {convectionBc(1, study.endCap(), bad, kAmbient)});
        CHECK_FALSE(study.setDefinition(d).has_value());
    }
    auto ambient = steadyDefinition(study, {}, {convectionBc(1, study.endCap(), 25.0, -1.0)});
    CHECK_FALSE(study.setDefinition(ambient).has_value());
    CHECK(study.definition().boundaryConditions.empty());
}

// ===========================================================================
// Well posedness on the real block
// ===========================================================================

TEST_CASE("ThermalValidation_SteadyPureNeumannModelFailsEndToEnd", "[thermal][validation][wellposed][steady][endtoend]") {
    // Generation and nothing else: no prescribed temperature, no convection. The solver refuses it; the validator names why, and nothing
    // after it is judged.
    BoundaryStudy study;
    define(study, steadyDefinition(study, {generationLoad(1, 1.0e5)}, {}));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->system.has_value());
    REQUIRE_FALSE(derived->steady.has_value());   // the qualified solve refused it
    const ThermalValidationReport report = validate(study, derived.get());
    print("steady-pure-neumann", report);
    CHECK(report.status() == ValidationStatus::Fail);
    CHECK(report.inputsAccepted());
    CHECK(report.statusOf(ValidationRule::WellPosed) == CheckStatus::Fail);
    CHECK(hasCode(report, ValidationCode::NoTemperatureDatum));
    CHECK(report.statusOf(ValidationRule::SolverResidual) == CheckStatus::Skipped);
    CHECK(report.checks[static_cast<std::size_t>(ValidationRule::SolverResidual)].blockedBy == std::optional{ValidationRule::WellPosed});
}

TEST_CASE("ThermalValidation_SteadyRobinOnlyModelPassesEndToEnd", "[thermal][validation][wellposed][steady][endtoend]") {
    // No prescribed temperature at all: the only anchor is a positive convection coefficient. Valid.
    BoundaryStudy study;
    define(study, steadyDefinition(study, {generationLoad(1, 1.0e5)}, {convectionBc(1, study.endCap(), 25.0, kAmbient)}));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->balance.has_value());
    REQUIRE(derived->boundary->constraints().isEmpty());
    const ThermalValidationReport report = validate(study, derived.get());
    print("steady-robin-only", report);
    CHECK(report.status() == ValidationStatus::Pass);
    CHECK(report.statusOf(ValidationRule::WellPosed) == CheckStatus::Pass);
    CHECK(report.metrics.constrainedCount == std::optional<std::size_t>{0});
}

TEST_CASE("ThermalValidation_SteadyDirichletOnlyPasses", "[thermal][validation][wellposed][steady][endtoend]") {
    BoundaryStudy study;
    define(study, steadyDefinition(study, {generationLoad(1, 1.0e5)}, {temperatureBc(1, study.endCap(), 300.0)}));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->balance.has_value());
    CHECK(validate(study, derived.get()).accepted());
}

TEST_CASE("ThermalValidation_TransientPureNeumannModelPassesEndToEnd", "[thermal][validation][wellposed][transient][endtoend]") {
    // An insulated body with generation: no boundary condition at all, a positive capacity, a valid initial temperature. The steady datum rule
    // does not apply, the analysis is accepted -- and says why it is well posed.
    BoundaryStudy study;
    define(study, transientDefinition(study, {generationLoad(1, 1.0e5)}, {}, 300.0, 25.0, 100.0));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->balance.has_value());
    const ThermalValidationReport report = validate(study, derived.get());
    print("transient-pure-neumann", report);
    CHECK(report.status() == ValidationStatus::Pass);
    CHECK(report.accepted());
    CHECK(report.statusOf(ValidationRule::WellPosed) == CheckStatus::Pass);
    CHECK(hasCode(report, ValidationCode::TransientWithoutDatum));
    CHECK(hasCode(report, ValidationCode::NoBoundaryConditionsConfigured));
    CHECK(report.errorCount() == 0);
    // The SAME model as a steady analysis is refused: the two rules are not confused.
    define(study, steadyDefinition(study, {generationLoad(1, 1.0e5)}, {}));
    const std::unique_ptr<Derived> steady = derive(study);
    CHECK(hasCode(validate(study, steady.get()), ValidationCode::NoTemperatureDatum));
}

TEST_CASE("ThermalValidation_FullyPrescribedSteadyModelPassesOnARealMesh", "[thermal][validation][wellposed][steady][nfree]") {
    // Every face prescribed. If the mesh has interior nodes they remain free; if every node lies on the boundary nothing is free. Either
    // way the analysis is well posed -- and, when nothing is free, the empty free residual is accepted trivially and is not a failure.
    BoundaryStudy study;
    std::vector<ThermalBoundaryCondition> all{temperatureBc(1, study.startCap(), 400.0), temperatureBc(2, study.endCap(), 300.0)};
    for (std::size_t side = 0; side < 4; ++side) {
        all.push_back(temperatureBc(3 + side, study.side(side), 350.0));
    }
    define(study, steadyDefinition(study, {}, all));
    const std::unique_ptr<Derived> derived = derive(study);
    if (!derived->balance.has_value()) {
        // Overlapping prescriptions at edge nodes (400 K vs 350 K) conflict: the model is refused by P18-BC, which is the right answer.
        const ThermalValidationReport report = validate(study);
        CHECK(report.statusOf(ValidationRule::BoundaryConditions) == CheckStatus::Fail);
        // A consistent total prescription instead: every face at ONE temperature.
        std::vector<ThermalBoundaryCondition> uniform;
        uniform.push_back(temperatureBc(1, study.startCap(), 350.0));
        uniform.push_back(temperatureBc(2, study.endCap(), 350.0));
        for (std::size_t side = 0; side < 4; ++side) {
            uniform.push_back(temperatureBc(3 + side, study.side(side), 350.0));
        }
        define(study, steadyDefinition(study, {}, uniform));
        const std::unique_ptr<Derived> flat = derive(study);
        REQUIRE(flat->balance.has_value());
        const ThermalValidationReport ok = validate(study, flat.get());
        print("steady-all-faces-prescribed", ok);
        CHECK(ok.accepted());
        CHECK(ok.statusOf(ValidationRule::WellPosed) == CheckStatus::Pass);
        CHECK(ok.statusOf(ValidationRule::SolverResidual) == CheckStatus::Pass);
        WARN(std::format("fully prescribed block: {} free of {} unknowns", flat->steady->freeEquations(), flat->steady->dofCount()));
    }
}

// ===========================================================================
// Read-only, deterministic, one owner
// ===========================================================================

TEST_CASE("ThermalValidation_ValidationIsReadOnly", "[thermal][validation][purity]") {
    BoundaryStudy study;
    define(study, steadyDefinition(study, {generationLoad(1, 1.0e5)}, {temperatureBc(1, study.startCap(), 300.0), convectionBc(2, study.endCap(), 25.0, kAmbient)}));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->balance.has_value());

    const thermal::ThermalAnalysisDefinition definitionBefore = study.definition();
    const std::uint64_t documentRevision = study.document.revision();
    const std::uint64_t analysisRevision = thermal::findThermalAnalysis(study.document, study.analysis)->revision();
    const meshing::MeshStamp stampBefore = study.volume().mesh().stamp();
    const std::size_t nodesBefore = study.volume().mesh().nodes().size();
    const ThermalSystem systemBefore = *derived->system;
    const SteadyThermalSolution steadyBefore = *derived->steady;
    const thermal::ThermalPostResult postBefore = *derived->post;
    const ThermalBalanceResult balanceBefore = *derived->balance;
    const PreparedThermalLoads loadsBefore = *derived->loads;
    const PreparedThermalBoundary boundaryBefore = *derived->boundary;

    for (int i = 0; i < 3; ++i) {
        (void)validate(study, derived.get());
    }
    CHECK(study.definition() == definitionBefore);
    CHECK(study.document.revision() == documentRevision);
    CHECK(thermal::findThermalAnalysis(study.document, study.analysis)->revision() == analysisRevision);
    CHECK(study.volume().mesh().stamp() == stampBefore);   // nothing was regenerated or remeshed
    CHECK(study.volume().mesh().nodes().size() == nodesBefore);
    CHECK(*derived->system == systemBefore);
    CHECK(*derived->steady == steadyBefore);
    CHECK(*derived->post == postBefore);
    CHECK(*derived->balance == balanceBefore);
    CHECK(*derived->loads == loadsBefore);
    CHECK(*derived->boundary == boundaryBefore);
}

TEST_CASE("ThermalValidation_ReportOrderingAndContentsAreDeterministic", "[thermal][validation][determinism]") {
    BoundaryStudy study;
    define(study, steadyDefinition(study, {generationLoad(1, 1.0e5)}, {temperatureBc(1, study.startCap(), 300.0), convectionBc(2, study.endCap(), 25.0, kAmbient)}));
    const std::unique_ptr<Derived> derived = derive(study);
    const ThermalValidationReport first = validate(study, derived.get());
    for (int i = 0; i < 25; ++i) {
        const ThermalValidationReport again = validate(study, derived.get());
        CHECK(again == first);   // every status, check, finding, metric and the source, bit for bit
        CHECK(classificationDigest(again) == classificationDigest(first));
    }
    print("determinism-steady", first);
}

TEST_CASE("ThermalValidation_RepeatedReportsOfEveryClassAreStable", "[thermal][validation][determinism]") {
    // The classes the brief asks to repeat: a clean pass, a warning, a stale source, a singular model, a valid transient.
    const auto repeat = [](const std::string& name, const std::function<ThermalValidationReport()>& make, ValidationStatus expected) {
        const ThermalValidationReport first = make();
        CHECK(first.status() == expected);
        for (int i = 0; i < 10; ++i) {
            const ThermalValidationReport again = make();
            CHECK(again == first);
            CHECK(classificationDigest(again) == classificationDigest(first));
        }
        WARN(std::format("FP repeat {} {:016x}", name, classificationDigest(first)));
    };
    SECTION("clean pass") {
        BoundaryStudy study;
        define(study, steadyDefinition(study, {}, slabConditions(study)));
        const std::unique_ptr<Derived> d = derive(study);
        repeat("clean-pass", [&] { return validate(study, d.get()); }, ValidationStatus::Pass);
    }
    SECTION("stale source") {
        BoundaryStudy study;
        define(study, steadyDefinition(study, {}, slabConditions(study)));
        const std::unique_ptr<Derived> d = derive(study);
        study.remesh();
        repeat("stale-source", [&] { return validate(study, d.get()); }, ValidationStatus::Fail);
    }
    SECTION("singular steady") {
        BoundaryStudy study;
        define(study, steadyDefinition(study, {generationLoad(1, 1.0e5)}, {}));
        const std::unique_ptr<Derived> d = derive(study);
        repeat("singular-steady", [&] { return validate(study, d.get()); }, ValidationStatus::Fail);
    }
    SECTION("valid transient") {
        BoundaryStudy study;
        define(study, transientDefinition(study, {generationLoad(1, 1.0e5)}, slabConditions(study), 300.0, 25.0, 100.0));
        const std::unique_ptr<Derived> d = derive(study);
        repeat("valid-transient", [&] { return validate(study, d.get()); }, ValidationStatus::Pass);
    }
}

TEST_CASE("ThermalValidation_PrimaryFailureReasonDoesNotDependOnTheOrderOfTheDocument", "[thermal][validation][report][determinism]") {
    // Several things wrong at once: the report lists them in the RULE order, always, so the first reported cause is a function of the
    // model and not of iteration order.
    BoundaryStudy study;
    REQUIRE(features::removeMaterialProperty(study.document, study.material, materials::MechanicalPropertyKind::Density).has_value());
    const FaceName nowhere{study.feature, FaceSelector{.role = FaceRole::Side, .entity = EntityId::fromValue(987654)}};
    define(study, transientDefinition(study, {surfaceLoad(1, nowhere, 1.0)},
                                      {temperatureBc(1, study.startCap(), 400.0), temperatureBc(2, study.startCap(), 350.0)}, 300.0, 20.0, 100.0));
    const ThermalValidationReport report = validate(study);
    REQUIRE(report.errorCount() >= 3);
    // Material, then loads, then the conditions -- the rule order -- whatever the document holds.
    std::vector<ValidationRule> order;
    for (const ValidationFinding& f : report.findings) {
        if (f.severity == ValidationSeverity::Error && (order.empty() || order.back() != f.rule)) {
            order.push_back(f.rule);
        }
    }
    CHECK(order == std::vector<ValidationRule>{ValidationRule::Material, ValidationRule::Loads, ValidationRule::BoundaryConditions});
    CHECK(std::ranges::is_sorted(report.findings, [](const ValidationFinding& a, const ValidationFinding& b) { return a.rule < b.rule; }));
}


// ===========================================================================
// The transient history contract
// ===========================================================================

TEST_CASE("ThermalValidation_AFinalOnlyHistoryOfSeveralStepsCannotBeFullyAccepted", "[thermal][validation][endtoend][transient][history]") {
    // FinalOnly stores one state, so the intermediate steps cannot be balanced step by step. The SOLVE is accepted -- every recorded step
    // residual is within the tolerance -- and the full analysis is not, and the report says exactly that and why.
    BoundaryStudy study;
    define(study, transientDefinition(study, {generationLoad(1, 1.0e5)}, slabConditions(study), 300.0, 25.0, 100.0,
                                      thermal::TransientOutputPolicy::FinalOnly));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->transient.has_value());
    REQUIRE(derived->transient->sampleCount() == 1);
    REQUIRE_FALSE(derived->balance.has_value());   // BALANCE refuses a history it cannot balance
    const ThermalValidationReport report = validate(study, derived.get());
    print("transient-final-only", report);
    CHECK(report.solveAccepted());
    CHECK_FALSE(report.accepted());
    CHECK(report.statusOf(ValidationRule::SolverResidual) == CheckStatus::Pass);
    CHECK(report.statusOf(ValidationRule::ResultFinite) == CheckStatus::Pass);
    CHECK(report.statusOf(ValidationRule::HeatBalance) == CheckStatus::Fail);
    const auto missing = firstWithCode(report.findings, ValidationCode::BalanceMissing);
    REQUIRE(missing.has_value());
    CHECK_THAT(missing->message, ContainsSubstring("EveryStep"));
}

TEST_CASE("ThermalValidation_ASingleStepFinalOnlyTransientIsFullyAccepted", "[thermal][validation][endtoend][transient][history]") {
    BoundaryStudy study;
    define(study, transientDefinition(study, {generationLoad(1, 1.0e5)}, {}, 300.0, 100.0, 100.0, thermal::TransientOutputPolicy::FinalOnly));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->balance.has_value());
    const ThermalValidationReport report = validate(study, derived.get());
    CHECK(report.accepted());
    CHECK(report.metrics.transientStepCount == std::optional<std::size_t>{1});
}

TEST_CASE("ThermalValidation_RejectsTimeSettingsPastTheResourceGuard", "[thermal][validation][transient][setup][endtoend]") {
    // The definition is valid on its own terms (0 < dt <= t_end, both finite) and asks for 1e8 steps. The grid refuses; the validator says so
    // and nothing downstream is judged. The step is never silently made larger.
    BoundaryStudy study;
    define(study, transientDefinition(study, {}, slabConditions(study), 300.0, 1.0e-6, 100.0));
    const ThermalValidationReport report = validate(study);
    print("transient-too-many-steps", report);
    CHECK(report.status() == ValidationStatus::Fail);
    CHECK(report.statusOf(ValidationRule::TransientSetup) == CheckStatus::Fail);
    CHECK(hasCode(report, ValidationCode::TimeSettingsInvalid));
    CHECK(report.statusOf(ValidationRule::Material) == CheckStatus::Pass);
    CHECK(report.statusOf(ValidationRule::SourceCompatibility) == CheckStatus::Skipped);
}

TEST_CASE("ThermalValidation_TheValidatorAndTheSolverAgreeOnWhatIsSingular", "[thermal][validation][wellposed][authority]") {
    // The validator's verdict on well-posedness is P18-SOLVE-001's own `analyseAnchoring`: where the qualified solve refuses a model for
    // the lack of a datum, the validator names the same cause; where the solve succeeds, the validator passes the rule.
    BoundaryStudy study;
    for (const bool anchored : {false, true}) {
        define(study, steadyDefinition(study, {generationLoad(1, 1.0e5)}, anchored ? std::vector<ThermalBoundaryCondition>{temperatureBc(1, study.endCap(), 300.0)}
                                                                                    : std::vector<ThermalBoundaryCondition>{}));
        const std::unique_ptr<Derived> derived = derive(study);
        const ThermalValidationReport report = validate(study, derived.get());
        CHECK(derived->steady.has_value() == anchored);
        CHECK((report.statusOf(ValidationRule::WellPosed) == CheckStatus::Pass) == anchored);
        if (!anchored) {
            CHECK_THAT(derived->stoppedBecause, ContainsSubstring("temperature"));
        }
    }
}


// ===========================================================================
// The model-level decision on corrupted evidence
// ===========================================================================
//
// A typed result cannot be corrupted -- that is what makes it evidence -- so the decision the validator takes from one is shown here on the
// PLAIN DATA it holds, through the SAME function (`judgeSolveEvidence`) that decides in production. The data are copies the test owns and edits;
// no production type is weakened.

namespace {

struct Owned {
    std::vector<std::vector<Temperature>> states;
    std::vector<double> times;
    std::vector<thermal::TransientStepRecord> records;
    std::vector<Temperature> initial;
    std::vector<thermal::ThermalPostState> post;
    std::vector<ThermalHeatBalance> balance;
    std::optional<ThermalFreeEquationMap> equations;
    std::array<std::size_t, 3> categories{};
    bool transient = false;
    thermal::ThermalSolveEvidence evidence;

    /// Re-points the evidence's views at the (possibly edited) arrays.
    void refresh() {
        evidence.states.clear();
        for (const std::vector<Temperature>& state : states) {
            evidence.states.emplace_back(state);
        }
        evidence.times = times;
        evidence.records = records;
        evidence.initial = initial;
        evidence.post = std::span<const thermal::ThermalPostState>(post);
        evidence.balance = std::span<const ThermalHeatBalance>(balance);
        evidence.balanceCategories = categories;
    }

    [[nodiscard]] ThermalValidationReport judge() const {
        ThermalValidationReport report = allPassingReport(transient);
        thermal::judgeSolveEvidence(evidence, report);
        return report;
    }
};

[[nodiscard]] std::unique_ptr<Owned> own(const BoundaryStudy& study, const Derived& d) {
    auto o = std::make_unique<Owned>();
    const thermal::ThermalAnalysisDefinition def = study.definition();
    o->transient = def.mode == ThermalAnalysisMode::Transient;
    thermal::ThermalSolveEvidence& e = o->evidence;
    e.mode = def.mode;
    e.settings = def.solver;
    e.conductance = &d.system->conductance();
    e.capacity = d.system->capacity().has_value() ? &*d.system->capacity() : nullptr;
    e.heat = &d.system->heat();
    e.constraints = &d.boundary->constraints();
    Result<ThermalFreeEquationMap> equations = thermal::buildThermalFreeEquationMap(d.map, d.boundary->constraints());
    REQUIRE(equations.has_value());
    o->equations = std::move(*equations);
    e.equations = &*o->equations;
    e.dofCount = static_cast<std::size_t>(d.map.dofCount());
    e.tetCount = d.post->elementCount();
    if (!o->transient) {
        o->states.emplace_back(d.steady->temperatures().begin(), d.steady->temperatures().end());
        e.pivotRatio = d.steady->pivotRatio();
    } else {
        for (std::size_t i = 0; i < d.transient->sampleCount(); ++i) {
            o->states.emplace_back(d.transient->temperaturesAt(i).begin(), d.transient->temperaturesAt(i).end());
            o->times.push_back(d.transient->timeOf(i).si());
        }
        o->records.assign(d.transient->steps().begin(), d.transient->steps().end());
        o->initial.assign(d.transient->initialTemperatures().begin(), d.transient->initialTemperatures().end());
        e.grid = &d.transient->grid();
        e.output = def.transient->output;
        e.initialOverrides = d.transient->initialOverrides();
    }
    for (const thermal::ThermalPostState& state : d.post->states()) {
        o->post.push_back(state);
    }
    for (const ThermalHeatBalance& state : d.balance->states()) {
        o->balance.push_back(state);
    }
    o->categories = {d.loads->contributions().size(), d.boundary->convections().size(), d.boundary->constraints().size()};
    o->refresh();
    return o;
}

/// A model with SEVERAL free unknowns, so that a residual means something: the start cap held, a convecting far cap, generation.
void multiFreeDefinition(BoundaryStudy& study) {
    define(study, steadyDefinition(study, {generationLoad(1, 1.0e5)},
                                   {temperatureBc(1, study.startCap(), 300.0), convectionBc(2, study.endCap(), 25.0, kAmbient)}));
}

[[nodiscard]] ThermalDofIndex firstFree(const Owned& o, std::size_t skip = 0) {
    for (std::size_t d = 0; d < o.evidence.dofCount; ++d) {
        if (o.equations->isFree(ThermalDofIndex::fromValue(d)) && skip-- == 0) {
            return ThermalDofIndex::fromValue(d);
        }
    }
    FAIL("no free unknown");
    return ThermalDofIndex::fromValue(0);
}

/// Applies the same edit to the solver's state AND to the recovered temperatures of the same state, so that only the residual gate has a
/// complaint about it and the POST-versus-solver comparison has none.
void perturb(Owned& o, std::size_t state, ThermalDofIndex dof, double delta) {
    const auto at = static_cast<std::size_t>(dof.value());
    o.states[state][at] = Temperature::fromSi(o.states[state][at].si() + delta);
    o.post[state].temperatures[at] = o.states[state][at];
    o.refresh();
}

} // namespace

TEST_CASE("ThermalValidation_EvidenceOfAQualifiedSteadyAnalysisIsAcceptedByTheModelLevelDecision", "[thermal][validation][evidence][steady]") {
    BoundaryStudy study;
    multiFreeDefinition(study);
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->balance.has_value());
    const std::unique_ptr<Owned> o = own(study, *derived);
    REQUIRE(o->equations->freeCount() >= 3);   // a residual that means something
    const ThermalValidationReport report = o->judge();
    CHECK(report.errorCount() == 0);
    for (const ValidationRule rule : {ValidationRule::SolverResidual, ValidationRule::ResultFinite, ValidationRule::PostResult, ValidationRule::HeatBalance}) {
        CHECK(report.statusOf(rule) == CheckStatus::Pass);
    }
    REQUIRE(report.metrics.steadyResidual.has_value());
    CHECK(*report.metrics.steadyResidual <= 1.0e-9);
    REQUIRE(report.metrics.worstBalanceResidual.has_value());
    CHECK(report.metrics.freeEquations == std::optional<std::size_t>{o->equations->freeCount()});
}

TEST_CASE("ThermalValidation_APerturbedFreeTemperatureFailsTheResidualAlone", "[thermal][validation][evidence][steady][residual]") {
    BoundaryStudy study;
    multiFreeDefinition(study);
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->balance.has_value());
    std::unique_ptr<Owned> o = own(study, *derived);
    perturb(*o, 0, firstFree(*o), 0.25);   // finite, plausible, wrong; the constraints still hold; the recovered fields carry the same value
    const ThermalValidationReport report = o->judge();
    print("bad-residual", report);
    CHECK(report.statusOf(ValidationRule::SolverResidual) == CheckStatus::Fail);
    CHECK(hasCode(report, ValidationCode::ResidualAboveTolerance));
    CHECK(report.statusOf(ValidationRule::ResultFinite) == CheckStatus::Pass);    // it is finite
    CHECK(report.statusOf(ValidationRule::PostResult) == CheckStatus::Pass);      // and the recovered temperatures are the solver's
    CHECK(report.statusOf(ValidationRule::HeatBalance) == CheckStatus::Pass);     // the balance was not touched
    CHECK_FALSE(report.solveAccepted());
    CHECK(report.metrics.steadyResidual.value() > report.metrics.residualTolerance.value());
}

TEST_CASE("ThermalValidation_ACorruptedBalanceFailsTheBalanceAlone", "[thermal][validation][evidence][steady][balance]") {
    BoundaryStudy study;
    multiFreeDefinition(study);
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->balance.has_value());
    std::unique_ptr<Owned> o = own(study, *derived);
    o->balance[0].terms.convectionHeat = o->balance[0].terms.convectionHeat + Power::fromSi(0.5);   // 0.5 W of heat that goes nowhere
    o->refresh();
    const ThermalValidationReport report = o->judge();
    print("bad-balance", report);
    CHECK(report.statusOf(ValidationRule::HeatBalance) == CheckStatus::Fail);
    CHECK(hasCode(report, ValidationCode::BalanceOutOfTolerance));
    CHECK(report.statusOf(ValidationRule::SolverResidual) == CheckStatus::Pass);   // the algebra is fine: a small residual, a bad balance
    CHECK(report.statusOf(ValidationRule::ResultFinite) == CheckStatus::Pass);
    CHECK(report.statusOf(ValidationRule::PostResult) == CheckStatus::Pass);
    CHECK(report.solveAccepted());
    CHECK_FALSE(report.accepted());
    CHECK(report.errorCount() == 1);
}

TEST_CASE("ThermalValidation_ANotANumberTemperatureFailsTheFinitenessGateAndTheResidual", "[thermal][validation][evidence][steady][finite]") {
    for (const double bad : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -1.0}) {
        BoundaryStudy study;
        multiFreeDefinition(study);
        const std::unique_ptr<Derived> derived = derive(study);
        std::unique_ptr<Owned> o = own(study, *derived);
        const auto at = static_cast<std::size_t>(firstFree(*o).value());
        o->states[0][at] = Temperature::fromSi(bad);
        o->post[0].temperatures[at] = o->states[0][at];
        o->refresh();
        const ThermalValidationReport report = o->judge();
        INFO("temperature " << bad);
        CHECK(report.statusOf(ValidationRule::ResultFinite) == CheckStatus::Fail);
        CHECK(hasCode(report, ValidationCode::TemperatureNotFinite));
        CHECK(report.statusOf(ValidationRule::SolverResidual) == CheckStatus::Fail);   // never a pass on a number that is not a number
        CHECK_FALSE(report.solveAccepted());
        CHECK_FALSE(report.metrics.minimumTemperature.has_value());   // no statistic is computed on a NaN
        CHECK_FALSE(report.metrics.maximumTemperature.has_value());
    }
}

TEST_CASE("ThermalValidation_ANotANumberHeatFluxFailsThePostGateEvenWhenTheTemperatureAndTheBalanceAreFine", "[thermal][validation][evidence][steady][post]") {
    BoundaryStudy study;
    multiFreeDefinition(study);
    const std::unique_ptr<Derived> derived = derive(study);
    std::unique_ptr<Owned> o = own(study, *derived);
    o->post[0].fields.heatFluxes[0].y = HeatFlux::fromSi(std::numeric_limits<double>::quiet_NaN());
    o->refresh();
    const ThermalValidationReport report = o->judge();
    CHECK(report.statusOf(ValidationRule::PostResult) == CheckStatus::Fail);
    CHECK(hasCode(report, ValidationCode::PostFieldNotFinite));
    CHECK(report.statusOf(ValidationRule::SolverResidual) == CheckStatus::Pass);
    CHECK(report.statusOf(ValidationRule::ResultFinite) == CheckStatus::Pass);
    CHECK(report.statusOf(ValidationRule::HeatBalance) == CheckStatus::Pass);
    CHECK(report.solveAccepted());
    CHECK_FALSE(report.metrics.maximumHeatFlux.has_value());   // the informational maximum is not computed over a NaN
}

TEST_CASE("ThermalValidation_ABalanceThatCancelsDoesNotRescueAFailedResidual", "[thermal][validation][evidence][steady][independence]") {
    // THE CANCELLATION TRAP, on the real model. The free temperatures are moved by +e at one unknown and by -e c_a / c_b at another, where c_j is
    // the sum of column j of K over the free rows: the NET residual sum(K T - Q) over the free rows does not move, so the heat BALANCE of the
    // moved field -- recomputed through the qualified algebraic seam -- closes to rounding, while two free equations are each wrong by an
    // amount a thousand times the tolerance. The residual is the gate that is not allowed to be rescued.
    BoundaryStudy study;
    multiFreeDefinition(study);
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->balance.has_value());
    std::unique_ptr<Owned> o = own(study, *derived);
    const ConductanceMatrix& k = derived->system->conductance();
    const std::size_t n = o->evidence.dofCount;
    std::vector<double> colsum(n, 0.0);
    for (std::size_t j = 0; j < n; ++j) {
        for (std::size_t i = 0; i < n; ++i) {
            if (o->equations->isFree(ThermalDofIndex::fromValue(i))) {
                colsum[j] += k.coeffSi(i, j);
            }
        }
    }
    std::vector<std::size_t> candidates;
    double largest = 0.0;
    for (std::size_t j = 0; j < n; ++j) {
        if (o->equations->isFree(ThermalDofIndex::fromValue(j))) {
            largest = std::max(largest, std::abs(colsum[j]));
        }
    }
    for (std::size_t j = 0; j < n; ++j) {
        if (o->equations->isFree(ThermalDofIndex::fromValue(j)) && std::abs(colsum[j]) > 1.0e-3 * largest) {
            candidates.push_back(j);
        }
    }
    REQUIRE(candidates.size() >= 2);
    const std::size_t a = candidates[0];
    const std::size_t b = candidates[1];
    const double e = 0.5;
    perturb(*o, 0, ThermalDofIndex::fromValue(a), e);
    perturb(*o, 0, ThermalDofIndex::fromValue(b), -e * colsum[a] / colsum[b]);

    // The balance OF THE MOVED FIELD, from the qualified seam.
    const thermal::BalanceAlgebra algebra{.conductance = k, .capacity = nullptr, .heat = derived->system->heat(), .constraints = derived->boundary->constraints(),
                                          .loads = derived->loads->contributions(), .convections = derived->boundary->convections()};
    const auto moved = thermal::steadyHeatBalance(algebra, o->states[0]);
    REQUIRE(moved.has_value());
    o->balance[0] = *moved;
    o->refresh();

    const ThermalValidationReport report = o->judge();
    WARN(std::format("CANCELLATION | |B| {:.3e} W | status {} | steady residual {:.3e} (tolerance {:.1e})", std::abs(moved->residual().si()),
                     thermal::toString(moved->status), report.metrics.steadyResidual.value_or(-1.0), report.metrics.residualTolerance.value_or(-1.0)));
    CHECK(moved->status == BalanceStatus::Balanced);                     // the books close ...
    CHECK(report.statusOf(ValidationRule::HeatBalance) == CheckStatus::Pass);
    CHECK(report.statusOf(ValidationRule::SolverResidual) == CheckStatus::Fail);   // ... and the free equations do not
    CHECK(hasCode(report, ValidationCode::ResidualAboveTolerance));
    CHECK_FALSE(report.solveAccepted());
}

TEST_CASE("ThermalValidation_AResidualThatPassesDoesNotRescueABalanceThatFailsOnTheModel", "[thermal][validation][evidence][steady][independence]") {
    BoundaryStudy study;
    multiFreeDefinition(study);
    const std::unique_ptr<Derived> derived = derive(study);
    std::unique_ptr<Owned> o = own(study, *derived);
    o->balance[0].terms.generatedHeat = o->balance[0].terms.generatedHeat + Power::fromSi(0.2);
    o->refresh();
    const ThermalValidationReport report = o->judge();
    CHECK(report.statusOf(ValidationRule::SolverResidual) == CheckStatus::Pass);
    CHECK(report.statusOf(ValidationRule::HeatBalance) == CheckStatus::Fail);
    CHECK(report.status() == ValidationStatus::Fail);
}

TEST_CASE("ThermalValidation_ABalanceMissingACategoryFailsTheAttributionOnTheModel", "[thermal][validation][evidence][steady][balance]") {
    BoundaryStudy study;
    multiFreeDefinition(study);
    const std::unique_ptr<Derived> derived = derive(study);
    std::unique_ptr<Owned> o = own(study, *derived);
    REQUIRE(o->balance[0].generationLoads.size() == 1);
    o->balance[0].generationLoads.clear();   // the generation is nowhere in the accounting (the totals are left, so the numbers still look fine)
    o->refresh();
    const ThermalValidationReport report = o->judge();
    CHECK(hasCode(report, ValidationCode::BalanceAttributionMismatch));
    CHECK(report.statusOf(ValidationRule::HeatBalance) == CheckStatus::Fail);
}

TEST_CASE("ThermalValidation_ATransientHistoryWithAPerturbedMiddleStateFailsTheResidualAtThatStep", "[thermal][validation][evidence][transient][residual]") {
    BoundaryStudy study;
    define(study, transientDefinition(study, {generationLoad(1, 1.0e5)},
                                      {temperatureBc(1, study.startCap(), 300.0), convectionBc(2, study.endCap(), 25.0, kAmbient)}, 300.0, 7.0, 100.0));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->balance.has_value());
    std::unique_ptr<Owned> o = own(study, *derived);
    REQUIRE(o->evidence.mode == ThermalAnalysisMode::Transient);
    const ThermalValidationReport good = o->judge();
    for (const ValidationRule rule : {ValidationRule::SolverResidual, ValidationRule::ResultFinite, ValidationRule::PostResult, ValidationRule::HeatBalance}) {
        CHECK(good.statusOf(rule) == CheckStatus::Pass);
    }
    perturb(*o, 6, firstFree(*o), 0.5);   // state 6 (t = 42 s)
    const ThermalValidationReport report = o->judge();
    CHECK(report.statusOf(ValidationRule::SolverResidual) == CheckStatus::Fail);
    std::set<std::size_t> steps;
    for (const ValidationFinding& f : findingsOf(report, ValidationRule::SolverResidual)) {
        if (f.code == ValidationCode::ResidualAboveTolerance && f.step.has_value()) {
            steps.insert(*f.step);
        }
    }
    CHECK(steps == std::set<std::size_t>{6, 7});   // the step that ends at the state and the step that starts from it
    REQUIRE(report.metrics.worstResidualStep.has_value());
    CHECK((*report.metrics.worstResidualStep == 6 || *report.metrics.worstResidualStep == 7));
    CHECK(report.metrics.worstResidualStepSize == std::optional<double>{7.0});
    CHECK(report.statusOf(ValidationRule::HeatBalance) == CheckStatus::Pass);   // the balance of the unmoved history is untouched
}

TEST_CASE("ThermalValidation_ATransientHistoryWithAWrongStateCountViolatesItsContractOnTheModel", "[thermal][validation][evidence][transient][history]") {
    BoundaryStudy study;
    define(study, transientDefinition(study, {generationLoad(1, 1.0e5)}, {temperatureBc(1, study.startCap(), 300.0)}, 300.0, 25.0, 100.0));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->balance.has_value());
    std::unique_ptr<Owned> o = own(study, *derived);
    o->states.pop_back();
    o->times.pop_back();
    o->post.pop_back();
    o->refresh();
    const ThermalValidationReport report = o->judge();
    CHECK(hasCode(report, ValidationCode::HistoryContractViolated));
    CHECK(report.statusOf(ValidationRule::SolverResidual) == CheckStatus::Fail);
}

TEST_CASE("ThermalValidation_ARecoveryOrABalanceThatWasNeverPresentedFailsItsOwnRule", "[thermal][validation][evidence][steady][missing][refusals]") {
    BoundaryStudy study;
    multiFreeDefinition(study);
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->balance.has_value());
    std::unique_ptr<Owned> o = own(study, *derived);
    REQUIRE(o->judge().accepted());

    // The recovered gradient and flux are absent: the temperature and the balance are still judged, and the recovery fails alone.
    o->evidence.post.reset();
    ThermalValidationReport report = o->judge();
    CHECK(hasCode(report, ValidationCode::PostMissing));
    CHECK(report.statusOf(ValidationRule::PostResult) == CheckStatus::Fail);
    CHECK(report.statusOf(ValidationRule::SolverResidual) == CheckStatus::Pass);
    CHECK(report.statusOf(ValidationRule::ResultFinite) == CheckStatus::Pass);
    CHECK(report.statusOf(ValidationRule::HeatBalance) == CheckStatus::Pass);
    CHECK_FALSE(report.accepted());

    // The balance is absent: the recovery is judged, and the balance fails alone.
    o->refresh();
    o->evidence.balance.reset();
    report = o->judge();
    CHECK(hasCode(report, ValidationCode::BalanceMissing));
    CHECK(report.statusOf(ValidationRule::HeatBalance) == CheckStatus::Fail);
    CHECK(report.statusOf(ValidationRule::PostResult) == CheckStatus::Pass);
    CHECK_FALSE(report.accepted());
}

TEST_CASE("ThermalValidation_AnAbsentSolutionOrRecordedFailureStopsTheRulesAfterTheSolve", "[thermal][validation][evidence][lifecycle]") {
    BoundaryStudy study;
    multiFreeDefinition(study);
    const std::unique_ptr<Derived> derived = derive(study);
    std::unique_ptr<Owned> o = own(study, *derived);
    o->evidence.states.clear();
    ThermalValidationReport report = o->judge();
    CHECK(hasCode(report, ValidationCode::SolutionMissing));
    for (const ValidationRule rule : {ValidationRule::ResultFinite, ValidationRule::PostResult, ValidationRule::HeatBalance}) {
        CHECK(report.statusOf(rule) == CheckStatus::Skipped);
        CHECK(report.checks[static_cast<std::size_t>(rule)].blockedBy == std::optional{ValidationRule::SolverResidual});
    }
    o->refresh();
    const Error failure{ErrorCode::FailedPrecondition, "the solve failed"};
    o->evidence.lastSolveFailure = &failure;
    report = o->judge();
    CHECK(hasCode(report, ValidationCode::SolveFailed));
    CHECK(report.statusOf(ValidationRule::HeatBalance) == CheckStatus::Skipped);
}
