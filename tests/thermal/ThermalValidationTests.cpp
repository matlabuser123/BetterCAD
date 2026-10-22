// P18-VALID-001: the acceptance policy, the report and every rule's judgement, on hand data.
//
// WHAT THESE TESTS ARE FOR. The validator is a set of judgements about things other milestones produced, so the claims are
// about the judgement and not about the producers: that a mesh is refused for the reason that refuses it and for no
// other; that a corrupted temperature, gradient, flux or balance is refused by the gate that owns it; that a residual is
// judged against the canonical tolerance and nothing else; that the steady datum rule and the transient capacity rule are
// not confused; that a report's status cannot disagree with its findings.
//
// EVERY EXPECTED VALUE IS TYPED IN, comes from a closed form, or from the test's own arithmetic. A corrupted input is made by
// editing plain data the test itself obtained, never by weakening a production type. The real-mesh, currentness and
// end-to-end claims are in ThermalValidationMappedTests.cpp.

#include "ThermalValidationTestSupport.hpp"

#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>

using namespace vltest;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using meshing::QualityMetric;
using thermal::checkBalanceAttribution;
using thermal::checkHeatBalanceStates;
using thermal::checkPostStates;
using thermal::checkSteadyResidual;
using thermal::checkSteadyWellPosedness;
using thermal::checkTemperatureStates;
using thermal::checkTransientResiduals;
using thermal::checkTransientSetup;
using thermal::checkTransientWellPosedness;
using thermal::classifyThermalMesh;

namespace {

constexpr double kNotANumber = std::numeric_limits<double>::quiet_NaN();
constexpr double kInfinite = std::numeric_limits<double>::infinity();

/// A three-node chain, g = 2 between neighbours, 400 K and 300 K held at the ends: the free node settles at 350 K.
[[nodiscard]] Net chainWithEndsHeld() { return makeNet(chainConductance({2.0, 2.0}), {0.0, 0.0, 0.0}, {{0, 400.0}, {2, 300.0}}); }

[[nodiscard]] std::vector<Temperature> solvedChain(const Net& net) { return mustSolve(net.base).temperatures; }

} // namespace

// ===========================================================================
// The policy
// ===========================================================================

TEST_CASE("ThermalValidation_PolicyIsAcceptedByP16sOwnPolicyChecker", "[thermal][validation][policy]") {
    const meshing::QualityThresholds policy = thermal::thermalQualityThresholds();
    CHECK(meshing::validate(policy).has_value());
    // Four metrics are classified and no dihedral angle carries a failure bound.
    REQUIRE(policy.limits.size() == 4);
    CHECK(policy.limits.at(QualityMetric::TetRadiusRatio).failure == std::optional<double>{thermal::kThermalRadiusRatioFloor});
    CHECK(policy.limits.at(QualityMetric::TetAspectRatio).failure == std::optional<double>{thermal::kThermalAspectRatioCeiling});
    CHECK_FALSE(policy.limits.at(QualityMetric::TetMinDihedralAngle).failure.has_value());
    CHECK_FALSE(policy.limits.at(QualityMetric::TetMaxDihedralAngle).failure.has_value());
    // P16's own default is untouched: it still classifies nothing.
    CHECK(meshing::reportOnlyThresholds().limits.empty());
}

TEST_CASE("ThermalValidation_PolicyConstantsArePinned", "[thermal][validation][policy]") {
    // The constants are what the measured laws give (ThermalValidationLawTests.cpp); moving one without re-measuring fails here.
    CHECK(thermal::kThermalRadiusRatioFloor == 7.0e-6);
    CHECK(thermal::kThermalAspectRatioCeiling == 700.0);
    CHECK(thermal::kThermalGradientAccuracy == 1.0e-9);
    const meshing::QualityThresholds policy = thermal::thermalQualityThresholds();
    CHECK(policy.limits.at(QualityMetric::TetRadiusRatio).warning == std::optional<double>{3.0e-4});
    CHECK(policy.limits.at(QualityMetric::TetAspectRatio).warning == std::optional<double>{81.0});
    CHECK(policy.limits.at(QualityMetric::TetMinDihedralAngle).warning == std::optional<double>{6.0e-3});
    CHECK(policy.limits.at(QualityMetric::TetMaxDihedralAngle).warning == std::optional<double>{3.125});
    // A failure bound is always on the bad side of its warning bound, in P16's direction.
    CHECK(thermal::kThermalRadiusRatioFloor < 3.0e-4);
    CHECK(thermal::kThermalAspectRatioCeiling > 81.0);
}

TEST_CASE("ThermalValidation_EveryRuleHasAStableIdAndGroupInTheOrderItRuns", "[thermal][validation][report]") {
    const std::vector<std::string_view> ids{"TV_ANALYSIS_DEFINITION", "TV_GEOMETRY_CURRENT", "TV_MESH_CURRENT",   "TV_MESH_STRUCTURAL",
                                            "TV_MESH_QUALITY",         "TV_MATERIAL",         "TV_LOADS",          "TV_BCS",
                                            "TV_TRANSIENT_SETUP",      "TV_SOURCE_COMPAT",    "TV_WELL_POSED",     "TV_SOLVER_RESIDUAL",
                                            "TV_RESULT_FINITE",        "TV_POST",             "TV_BALANCE"};
    REQUIRE(thermal::kValidationRuleCount == ids.size());
    REQUIRE(thermal::kValidationRules.size() == ids.size());
    for (std::size_t i = 0; i < ids.size(); ++i) {
        CHECK(static_cast<std::size_t>(thermal::kValidationRules[i]) == i);   // the enumeration order IS the execution order
        CHECK(thermal::toString(thermal::kValidationRules[i]) == ids[i]);
    }
    // Groups: input failures are not result failures.
    CHECK(thermal::groupOf(ValidationRule::Material) == thermal::ValidationGroup::Input);
    CHECK(thermal::groupOf(ValidationRule::TransientSetup) == thermal::ValidationGroup::Input);
    CHECK(thermal::groupOf(ValidationRule::SourceCompatibility) == thermal::ValidationGroup::System);
    CHECK(thermal::groupOf(ValidationRule::WellPosed) == thermal::ValidationGroup::System);
    CHECK(thermal::groupOf(ValidationRule::SolverResidual) == thermal::ValidationGroup::Solve);
    CHECK(thermal::groupOf(ValidationRule::ResultFinite) == thermal::ValidationGroup::Result);
    CHECK(thermal::groupOf(ValidationRule::PostResult) == thermal::ValidationGroup::Result);
    CHECK(thermal::groupOf(ValidationRule::HeatBalance) == thermal::ValidationGroup::Balance);
    std::set<std::string_view> groups;
    for (const ValidationRule rule : thermal::kValidationRules) {
        groups.insert(thermal::toString(thermal::groupOf(rule)));
    }
    CHECK(groups.size() == 5);
}

TEST_CASE("ThermalValidation_SeverityIsAFunctionOfTheCodeAndNothingElse", "[thermal][validation][report]") {
    std::set<std::string_view> names;
    std::set<ValidationRule> rulesReached;
    std::vector<ValidationCode> warnings;
    std::vector<ValidationCode> infos;
    constexpr int last = static_cast<int>(ValidationCode::BalanceAttributionMismatch);
    for (int i = 0; i <= last; ++i) {
        const auto code = static_cast<ValidationCode>(i);
        names.insert(thermal::toString(code));
        rulesReached.insert(thermal::ruleOf(code));
        const ValidationFinding finding = findingOf(code);
        CHECK(finding.severity == thermal::severityOf(code));
        CHECK(finding.rule == thermal::ruleOf(code));
        if (thermal::severityOf(code) == ValidationSeverity::Warning) {
            warnings.push_back(code);
        }
        if (thermal::severityOf(code) == ValidationSeverity::Info) {
            infos.push_back(code);
        }
    }
    CHECK(names.size() == static_cast<std::size_t>(last) + 1);   // every code has its own name
    CHECK(names.count("unknown") == 0);
    CHECK(rulesReached.size() == thermal::kValidationRuleCount);   // every rule can be reached by at least one code
    // EXACTLY these are warnings and these are information; everything else refuses. No code is both.
    CHECK(warnings == std::vector<ValidationCode>{ValidationCode::MeshQualityPolicyUnusable, ValidationCode::ElementOutsideQualifiedEnvelope});
    CHECK(infos == std::vector<ValidationCode>{ValidationCode::NoLoadsConfigured, ValidationCode::NoBoundaryConditionsConfigured,
                                               ValidationCode::TransientWithoutDatum,
                                               ValidationCode::InitialConditionOverriddenByPrescription});
    // And the fatal conditions the brief names are fatal at the source, not left for a viewer to promote.
    for (const ValidationCode fatal :
         {ValidationCode::MeshStale, ValidationCode::MeshStructurallyInvalid, ValidationCode::MaterialMissing,
          ValidationCode::LoadsInvalid, ValidationCode::BoundaryConditionsInvalid, ValidationCode::NoTemperatureDatum,
          ValidationCode::ResidualAboveTolerance, ValidationCode::BalanceOutOfTolerance, ValidationCode::TemperatureNotFinite,
          ValidationCode::SourceStale, ValidationCode::SourceMismatch}) {
        CHECK(thermal::severityOf(fatal) == ValidationSeverity::Error);
    }
}

TEST_CASE("ThermalValidation_StatusAndCountsAreDerivedFromTheChecksAndFindings", "[thermal][validation][report]") {
    ThermalValidationReport report = allPassingReport();
    CHECK(report.status() == ValidationStatus::Pass);
    CHECK(report.accepted());
    CHECK(report.errorCount() == 0);
    CHECK(report.warningCount() == 0);
    CHECK(report.statusOf(ValidationRule::TransientSetup) == CheckStatus::NotApplicable);   // honest: not a pass

    // A warning: accepted, with warnings. The warning does not refuse.
    report.findings.push_back(findingOf(ValidationCode::ElementOutsideQualifiedEnvelope));
    CHECK(report.status() == ValidationStatus::PassWithWarnings);
    CHECK(report.accepted());
    CHECK(report.warningCount() == 1);
    CHECK(report.statusOf(ValidationRule::MeshQuality) == CheckStatus::Warning);
    CHECK(report.statusOf(ValidationRule::Material) == CheckStatus::Pass);

    // Information alone changes nothing.
    ThermalValidationReport infoOnly = allPassingReport();
    infoOnly.findings.push_back(findingOf(ValidationCode::NoLoadsConfigured));
    CHECK(infoOnly.status() == ValidationStatus::Pass);
    CHECK(infoOnly.infoCount() == 1);
    CHECK(infoOnly.statusOf(ValidationRule::Loads) == CheckStatus::Pass);

    // One error: the whole analysis is refused, whatever else is said.
    report.findings.push_back(findingOf(ValidationCode::ResidualAboveTolerance));
    CHECK(report.status() == ValidationStatus::Fail);
    CHECK_FALSE(report.accepted());
    CHECK(report.errorCount() == 1);
    CHECK(report.statusOf(ValidationRule::SolverResidual) == CheckStatus::Fail);

    // The status cannot be set: removing the finding is the only way back.
    report.findings.pop_back();
    CHECK(report.status() == ValidationStatus::PassWithWarnings);
}

TEST_CASE("ThermalValidation_ASkippedRuleIsNeverAPass", "[thermal][validation][report]") {
    ThermalValidationReport report = allPassingReport();
    report.checks[static_cast<std::size_t>(ValidationRule::HeatBalance)].state = CheckState::Skipped;
    report.checks[static_cast<std::size_t>(ValidationRule::HeatBalance)].blockedBy = ValidationRule::SolverResidual;
    CHECK(report.statusOf(ValidationRule::HeatBalance) == CheckStatus::Skipped);
    CHECK(report.status() == ValidationStatus::Fail);   // no finding at all, and still not accepted
    CHECK_FALSE(report.accepted());
    CHECK(report.errorCount() == 0);

    // An empty report accepts nothing.
    const ThermalValidationReport empty;
    CHECK(empty.status() == ValidationStatus::Fail);
    CHECK_FALSE(empty.accepted());
    CHECK_FALSE(empty.inputsAccepted());
    CHECK_FALSE(empty.solveAccepted());
}

TEST_CASE("ThermalValidation_SolveAcceptedIsNotFullAcceptance", "[thermal][validation][report]") {
    // The two states are distinct and are not blurred: a solve can be accepted while the recovered fields or the heat balance are not.
    ThermalValidationReport report = allPassingReport();
    report.findings.push_back(findingOf(ValidationCode::BalanceOutOfTolerance));
    CHECK(report.solveAccepted());
    CHECK(report.inputsAccepted());
    CHECK_FALSE(report.accepted());

    ThermalValidationReport post = allPassingReport();
    post.findings.push_back(findingOf(ValidationCode::PostMissing));
    CHECK(post.solveAccepted());
    CHECK_FALSE(post.accepted());

    ThermalValidationReport residual = allPassingReport();
    residual.findings.push_back(findingOf(ValidationCode::ResidualAboveTolerance));
    CHECK(residual.inputsAccepted());
    CHECK_FALSE(residual.solveAccepted());
    CHECK_FALSE(residual.accepted());

    ThermalValidationReport input = allPassingReport();
    input.findings.push_back(findingOf(ValidationCode::MaterialMissing));
    CHECK_FALSE(input.inputsAccepted());
    CHECK_FALSE(input.solveAccepted());

    // A fatal finding in the temperatures also withholds solve acceptance (the finiteness gate is part of the solve).
    ThermalValidationReport finite = allPassingReport();
    finite.findings.push_back(findingOf(ValidationCode::TemperatureNotFinite));
    CHECK_FALSE(finite.solveAccepted());
}

TEST_CASE("ThermalValidation_PolicyFingerprintIsDeterministicAndDescribesTheRules", "[thermal][validation][policy][determinism]") {
    const std::string text = thermal::thermalValidationPolicyText();
    CHECK(text == thermal::thermalValidationPolicyText());
    const std::string fingerprint = thermal::thermalValidationPolicyFingerprint();
    CHECK(fingerprint == thermal::thermalValidationPolicyFingerprint());
    CHECK(fingerprint.starts_with("p18-valid-v1:"));
    CHECK(fingerprint.size() == std::string_view{"p18-valid-v1:"}.size() + 16);
    // The text names every rule in order, every code with its rule and severity, and the thresholds the mesh rules use.
    std::size_t at = 0;
    for (const ValidationRule rule : thermal::kValidationRules) {
        const std::size_t found = text.find(thermal::toString(rule), at);
        REQUIRE(found != std::string::npos);
        at = found;
    }
    CHECK_THAT(text, ContainsSubstring("code mesh_stale rule=TV_MESH_CURRENT severity=error"));
    CHECK_THAT(text, ContainsSubstring("code element_outside_qualified_envelope rule=TV_MESH_QUALITY severity=warning"));
    CHECK_THAT(text, ContainsSubstring("failure=7e-06"));
    CHECK_THAT(text, ContainsSubstring("warning=3e-04"));
    CHECK_THAT(text, ContainsSubstring("failure=700"));
    CHECK_THAT(text, ContainsSubstring("canonical settings.relativeResidualTolerance"));
    CHECK_THAT(text, ContainsSubstring("balanceWithinTolerance absolute=1e-12 relative=1e-13"));
    // No timestamp, address or environment can be in it: it carries no digit run that is not a policy number. (Stability is the test above.)
    CHECK_THAT(text, !ContainsSubstring("0x"));
}

// ===========================================================================
// The mesh
// ===========================================================================

TEST_CASE("ThermalValidation_RejectsAnInvertedTetrahedron", "[thermal][validation][mesh][invalid]") {
    // The regular wedge with two nodes exchanged: the signed volume is negated. NOT repaired, NOT |detJ|, NOT a warning.
    Corners corners = wedgeCorners(5.0);
    std::swap(corners[1], corners[2]);
    const meshing::MeshQualityReport quality = qualityOf(corners);
    REQUIRE_FALSE(quality.structurallyValid);

    const thermal::ThermalMeshVerdict verdict = classifyThermalMesh(quality);
    REQUIRE_FALSE(verdict.structural.empty());
    CHECK(hasCode(verdict.structural, ValidationCode::MeshStructurallyInvalid));
    for (const ValidationFinding& finding : verdict.structural) {
        CHECK(finding.severity == ValidationSeverity::Error);
        CHECK(finding.rule == ValidationRule::MeshStructural);
    }
    const auto invalid = firstWithCode(verdict.structural, ValidationCode::MeshStructurallyInvalid);
    REQUIRE(invalid.has_value());
    CHECK(invalid->count.value_or(0) >= 1);
}

TEST_CASE("ThermalValidation_RejectsADegenerateTetrahedron", "[thermal][validation][mesh][invalid]") {
    // Four coplanar points: zero volume.
    const Corners flat{{{0.0, 0.0, 0.0}, {0.01, 0.0, 0.0}, {0.0, 0.01, 0.0}, {0.01, 0.01, 0.0}}};
    const meshing::MeshQualityReport quality = qualityOf(flat);
    REQUIRE_FALSE(quality.structurallyValid);
    const thermal::ThermalMeshVerdict verdict = classifyThermalMesh(quality);
    CHECK(hasCode(verdict.structural, ValidationCode::MeshStructurallyInvalid));
    CHECK(errorsIn(verdict.structural) >= 1);
}

TEST_CASE("ThermalValidation_ANotANumberMetricIsInvalidAndNeverFallsThroughAComparison", "[thermal][validation][mesh][finite]") {
    for (const QualityMetric metric : {QualityMetric::TetRadiusRatio, QualityMetric::TetAspectRatio, QualityMetric::TetMinDihedralAngle,
                                       QualityMetric::TetMaxDihedralAngle}) {
        for (const double bad : {kNotANumber, kInfinite, -kInfinite}) {
            const thermal::ThermalMeshVerdict verdict = classifyThermalMesh(reportWithOneSummary(metric, bad));
            INFO(meshing::toString(metric) << " = " << bad);
            // Not a pass, not a warning: an error that names the metric.
            CHECK(hasCode(verdict.structural, ValidationCode::MeshHasInvalidElements));
            const auto finding = firstWithCode(verdict.structural, ValidationCode::MeshHasInvalidElements);
            REQUIRE(finding.has_value());
            CHECK(finding->severity == ValidationSeverity::Error);
            CHECK(finding->metric == std::optional{metric});
            CHECK(verdict.quality.empty());
        }
    }
}

TEST_CASE("ThermalValidation_AnInvalidElementCountFromP16IsAnErrorOnItsOwn", "[thermal][validation][mesh][invalid][refusals]") {
    // P16 counts the elements it classified Invalid. A report can say so while every structural check passes and every summary is
    // finite, so the count itself must be judged, not inferred from the other two.
    meshing::MeshQualityReport quality = qualityOf(wedgeCorners(5.0));
    REQUIRE(quality.structurallyValid);
    REQUIRE(quality.invalidElements == 0);
    REQUIRE(classifyThermalMesh(quality).structural.empty());

    quality.invalidElements = 3;
    const thermal::ThermalMeshVerdict verdict = classifyThermalMesh(quality);
    REQUIRE(verdict.structural.size() == 1);
    const ValidationFinding& finding = verdict.structural.front();
    CHECK(finding.code == ValidationCode::MeshHasInvalidElements);
    CHECK(finding.severity == ValidationSeverity::Error);
    CHECK(finding.rule == ValidationRule::MeshStructural);
    CHECK(finding.count == std::optional<std::size_t>{3});
    CHECK(verdict.quality.empty());
}

TEST_CASE("ThermalValidation_AcceptsAWellShapedTetrahedronWithoutAFinding", "[thermal][validation][mesh]") {
    const thermal::ThermalMeshVerdict verdict = classifyThermalMesh(qualityOf(wedgeCorners(5.0)));
    CHECK(verdict.structural.empty());
    CHECK(verdict.quality.empty());
    CHECK(verdict.worstByMetric.size() == 4);
}

TEST_CASE("ThermalValidation_ThermalMeshQualityWarning", "[thermal][validation][mesh][warning]") {
    // A 0.198-degree sliver: structurally valid, past the qualified envelope, and INSIDE the accuracy bound. Accepted, with a warning.
    const meshing::MeshQualityReport quality = qualityOf(wedgeCorners(0.01));
    REQUIRE(quality.structurallyValid);
    const thermal::ThermalMeshVerdict verdict = classifyThermalMesh(quality);
    CHECK(verdict.structural.empty());
    REQUIRE(verdict.quality.size() >= 1);
    CHECK(errorsIn(verdict.quality) == 0);
    const auto warning = firstWithCode(verdict.quality, ValidationCode::ElementOutsideQualifiedEnvelope);
    REQUIRE(warning.has_value());
    CHECK(warning->severity == ValidationSeverity::Warning);
    CHECK(warning->metric == std::optional{QualityMetric::TetRadiusRatio});
    CHECK(warning->threshold == std::optional<double>{3.0e-4});
    CHECK(warning->value.value() < 3.0e-4);
    CHECK(warning->value.value() > thermal::kThermalRadiusRatioFloor);   // between the two bounds
    CHECK(warning->count == std::optional<std::size_t>{1});
    CHECK(warning->element.has_value());
    CHECK(warning->unit == meshing::unitOf(QualityMetric::TetRadiusRatio));
}

TEST_CASE("ThermalValidation_ThermalMeshQualityReject", "[thermal][validation][mesh][reject]") {
    // FLATTENING: 3r/R of this wedge is 9.0e-8, below the 7.0e-6 floor. Structurally valid, so P16 does not refuse it.
    {
        const meshing::MeshQualityReport quality = qualityOf(wedgeCorners(1.0e-3));
        REQUIRE(quality.structurallyValid);
        const thermal::ThermalMeshVerdict verdict = classifyThermalMesh(quality);
        const auto error = firstWithCode(verdict.quality, ValidationCode::ElementBelowAccuracyFloor);
        REQUIRE(error.has_value());
        CHECK(error->severity == ValidationSeverity::Error);
        CHECK(error->metric == std::optional{QualityMetric::TetRadiusRatio});
        CHECK(error->threshold == std::optional<double>{thermal::kThermalRadiusRatioFloor});
        CHECK(error->value.value() < thermal::kThermalRadiusRatioFloor);
        CHECK(error->element.has_value());
        // The aspect ratio saturates and sees nothing of this family: no finding is about it.
        CHECK(std::ranges::none_of(verdict.quality, [](const ValidationFinding& f) { return f.metric == QualityMetric::TetAspectRatio; }));
    }
    // STRETCHING: a needle at aspect ratio 1000. Its radius ratio (1.7e-3) is ABOVE the warning bound, so the radius metric is
    // blind to it -- the reason there are two bounds.
    {
        const meshing::MeshQualityReport quality = qualityOf(needleCorners(0.01));
        REQUIRE(quality.structurallyValid);
        const thermal::ThermalMeshVerdict verdict = classifyThermalMesh(quality);
        const auto error = firstWithCode(verdict.quality, ValidationCode::ElementBelowAccuracyFloor);
        REQUIRE(error.has_value());
        CHECK(error->metric == std::optional{QualityMetric::TetAspectRatio});
        CHECK(error->threshold == std::optional<double>{thermal::kThermalAspectRatioCeiling});
        CHECK(error->value.value() > thermal::kThermalAspectRatioCeiling);
        const auto radius = std::ranges::find_if(verdict.worstByMetric, [](const thermal::ThermalMetricObservation& o) {
            return o.metric == QualityMetric::TetRadiusRatio;
        });
        REQUIRE(radius != verdict.worstByMetric.end());
        CHECK(radius->value > 3.0e-4);   // the radius ratio is fine here
    }
}

TEST_CASE("ThermalValidation_ReportsTheNumberOfElementsPastABoundAndNamesTheWorst", "[thermal][validation][mesh][count]") {
    // Two flattened tetrahedra sharing a base triangle: one above the plane, one below. ONE finding stands for both, and says so.
    const double a = 0.010;
    const double h = a * std::sqrt(3.0) / 2.0;
    const double apex = 1.0e-3 / 1000.0;
    const Nodes nodes{{1, at(0, 0, 0)}, {2, at(a, 0, 0)}, {3, at(a / 2, h, 0)}, {4, at(a / 2, h / 3, apex)}, {5, at(a / 2, h / 3, -apex)}};
    const AsmHand hand = makeAsmHand(nodes, {}, {{0, 1, 2, 3}, {0, 2, 1, 4}});
    const meshing::MeshQualityReport quality = meshing::evaluateMeshQuality(hand.mesh, meshing::reportOnlyThresholds());
    REQUIRE(quality.structurallyValid);
    REQUIRE(quality.tetCount == 2);
    const thermal::ThermalMeshVerdict verdict = classifyThermalMesh(quality);
    const auto error = firstWithCode(verdict.quality, ValidationCode::ElementBelowAccuracyFloor);
    REQUIRE(error.has_value());
    CHECK(error->count == std::optional<std::size_t>{2});
    CHECK(error->element == std::optional{quality.summaries.at(QualityMetric::TetRadiusRatio).worst});
    // Every element was judged; the report is bounded: one finding per metric, not one per element.
    CHECK(std::ranges::count_if(verdict.quality, [](const ValidationFinding& f) { return f.metric == QualityMetric::TetRadiusRatio; }) == 1);
}

TEST_CASE("ThermalValidation_ComparesStrictlySoAValueOnTheBoundIsOnTheGoodSide", "[thermal][validation][mesh][boundary]") {
    const auto verdictFor = [](QualityMetric metric, double value) { return classifyThermalMesh(reportWithOneSummary(metric, value)); };
    const auto errors = [](const thermal::ThermalMeshVerdict& v) { return errorsIn(v.quality) + errorsIn(v.structural); };
    const auto warnings = [](const thermal::ThermalMeshVerdict& v) {
        return std::ranges::count_if(v.quality, [](const ValidationFinding& f) { return f.severity == ValidationSeverity::Warning; });
    };

    // RADIUS RATIO (higher is better). The failure bound, then the warning bound.
    {
        const double floor = thermal::kThermalRadiusRatioFloor;
        CHECK(errors(verdictFor(QualityMetric::TetRadiusRatio, floor)) == 0);                       // ON the bound: good side
        CHECK(errors(verdictFor(QualityMetric::TetRadiusRatio, std::nextafter(floor, 0.0))) == 1);  // one ulp below: refused
        CHECK(errors(verdictFor(QualityMetric::TetRadiusRatio, std::nextafter(floor, 1.0))) == 0);  // one ulp above: fine
        CHECK(warnings(verdictFor(QualityMetric::TetRadiusRatio, floor)) == 1);                     // and between the bounds it warns
        CHECK(warnings(verdictFor(QualityMetric::TetRadiusRatio, 3.0e-4)) == 0);                    // ON the warning bound: good side
        CHECK(warnings(verdictFor(QualityMetric::TetRadiusRatio, std::nextafter(3.0e-4, 0.0))) == 1);
        CHECK(warnings(verdictFor(QualityMetric::TetRadiusRatio, 0.5)) == 0);
    }
    // ASPECT RATIO (lower is better).
    {
        const double ceiling = thermal::kThermalAspectRatioCeiling;
        CHECK(errors(verdictFor(QualityMetric::TetAspectRatio, ceiling)) == 0);
        CHECK(errors(verdictFor(QualityMetric::TetAspectRatio, std::nextafter(ceiling, 1.0e9))) == 1);
        CHECK(errors(verdictFor(QualityMetric::TetAspectRatio, std::nextafter(ceiling, 0.0))) == 0);
        CHECK(warnings(verdictFor(QualityMetric::TetAspectRatio, 81.0)) == 0);
        CHECK(warnings(verdictFor(QualityMetric::TetAspectRatio, std::nextafter(81.0, 1.0e9))) == 1);
        CHECK(warnings(verdictFor(QualityMetric::TetAspectRatio, 1.0)) == 0);
    }
    // DIHEDRAL ANGLES: a warning bound and NO failure bound -- however extreme, never an error.
    {
        CHECK(warnings(verdictFor(QualityMetric::TetMinDihedralAngle, 6.0e-3)) == 0);
        CHECK(warnings(verdictFor(QualityMetric::TetMinDihedralAngle, std::nextafter(6.0e-3, 0.0))) == 1);
        CHECK(errors(verdictFor(QualityMetric::TetMinDihedralAngle, 1.0e-12)) == 0);
        CHECK(warnings(verdictFor(QualityMetric::TetMaxDihedralAngle, 3.125)) == 0);
        CHECK(warnings(verdictFor(QualityMetric::TetMaxDihedralAngle, std::nextafter(3.125, 4.0))) == 1);
        CHECK(errors(verdictFor(QualityMetric::TetMaxDihedralAngle, 3.14159)) == 0);
    }
}

TEST_CASE("ThermalValidation_WarnsAboutAContradictoryReportingPolicyWithoutRefusing", "[thermal][validation][mesh][warning]") {
    // A mesh control carries its own thresholds and the mesher classifies the held report under them. A contradiction there is the
    // user's reporting preference, not a defect of the mesh: P16 still measured every metric. Surfaced, and not a refusal.
    meshing::MeshQualityReport quality = qualityOf(wedgeCorners(5.0));
    quality.thresholdPolicyError = Error{ErrorCode::InvalidArgument, "failure bound exceeds warning bound"};
    const thermal::ThermalMeshVerdict verdict = classifyThermalMesh(quality);
    const auto warning = firstWithCode(verdict.quality, ValidationCode::MeshQualityPolicyUnusable);
    REQUIRE(warning.has_value());
    CHECK(warning->severity == ValidationSeverity::Warning);
    CHECK_THAT(warning->message, ContainsSubstring("failure bound exceeds warning bound"));
    CHECK(errorsIn(verdict.quality) == 0);
    CHECK(verdict.worstByMetric.size() == 4);   // the metrics are still judged
}

TEST_CASE("ThermalValidation_ReusesP16MetricsAndAgreesWithP16sOwnClassificationUnderThePolicy", "[thermal][validation][mesh][p16]") {
    // ONE source of metric truth. P16's own per-element classification under the thermal thresholds must agree with the validator's
    // severity on the same mesh -- the validator reads P16's numbers and compares with P16's direction, it does not measure.
    struct Case {
        Corners corners;
        meshing::QualityClass expected;
    };
    const std::vector<Case> cases{{wedgeCorners(5.0), meshing::QualityClass::Valid},
                                  {wedgeCorners(0.01), meshing::QualityClass::Warning},
                                  {wedgeCorners(1.0e-3), meshing::QualityClass::Failure},
                                  {needleCorners(0.01), meshing::QualityClass::Failure},
                                  {needleCorners(1.0), meshing::QualityClass::Valid}};
    for (const Case& c : cases) {
        const AsmHand hand = oneTetOf(c.corners);
        const meshing::MeshQualityReport measured = meshing::evaluateMeshQuality(hand.mesh, meshing::reportOnlyThresholds());
        const meshing::MeshQualityReport classified = meshing::evaluateMeshQuality(hand.mesh, thermal::thermalQualityThresholds());
        const thermal::ThermalMeshVerdict verdict = classifyThermalMesh(measured);
        // The numbers are identical whatever policy produced the report.
        CHECK(measured.summaries == classified.summaries);
        const bool refuses = errorsIn(verdict.quality) > 0;
        const bool warns = !verdict.quality.empty() && !refuses;
        CHECK(refuses == (classified.failureElements > 0));
        CHECK(warns == (classified.warningElements > 0 && classified.failureElements == 0));
        REQUIRE(classified.tets.size() == 1);
        // What P16 itself would call the element under these thresholds.
        const bool p16Failure = classified.failureElements == 1;
        const bool p16Warning = classified.warningElements == 1;
        CHECK((c.expected == meshing::QualityClass::Failure) == p16Failure);
        CHECK((c.expected == meshing::QualityClass::Warning) == p16Warning);
        // And the worst observations are P16's own summary values, not recomputed ones.
        for (const thermal::ThermalMetricObservation& o : verdict.worstByMetric) {
            const meshing::MetricSummary& s = measured.summaries.at(o.metric);
            const double expected = meshing::direction(o.metric) == meshing::QualityDirection::HigherIsBetter ? s.minimum : s.maximum;
            CHECK(o.value == expected);
            CHECK(o.element == s.worst);
        }
    }
}

TEST_CASE("ThermalValidation_RejectionByThermalPolicyDoesNotFailP16GlobalMeshQuality", "[thermal][validation][mesh][ownership]") {
    // P16 reports, P18-VALID decides. The mesh the thermal policy refuses is, to P16 under its own default, structurally valid and
    // satisfying its (report-only) policy: P16 is not failed by P18's opinion.
    const meshing::MeshQualityReport quality = qualityOf(wedgeCorners(1.0e-3));
    CHECK(quality.structurallyValid);
    CHECK(quality.satisfiesPolicy());
    CHECK(quality.failureElements == 0);
    CHECK(errorsIn(classifyThermalMesh(quality).quality) == 1);
}

// ===========================================================================
// The temperatures
// ===========================================================================

TEST_CASE("ThermalValidation_RejectsNonFiniteTemperature", "[thermal][validation][finite]") {
    const std::vector<Temperature> good = stateOf({300.0, 310.0, 320.0});
    {
        const std::vector<std::span<const Temperature>> states{good};
        CHECK(checkTemperatureStates(states, 3).empty());
    }
    for (const double bad : {kNotANumber, kInfinite, -kInfinite}) {
        std::vector<Temperature> corrupted = good;
        corrupted[1] = Temperature::fromSi(bad);
        const std::vector<std::span<const Temperature>> states{good, corrupted};
        const std::vector<ValidationFinding> findings = checkTemperatureStates(states, 3);
        REQUIRE(findings.size() == 1);
        CHECK(findings[0].code == ValidationCode::TemperatureNotFinite);
        CHECK(findings[0].severity == ValidationSeverity::Error);
        CHECK(findings[0].step == std::optional<std::size_t>{1});
        CHECK(findings[0].count == std::optional<std::size_t>{1});
    }
}

TEST_CASE("ThermalValidation_RejectsATemperatureBelowAbsoluteZero", "[thermal][validation][finite]") {
    const std::vector<Temperature> cold = stateOf({300.0, -0.5, 320.0});
    const std::vector<std::span<const Temperature>> states{cold};
    const std::vector<ValidationFinding> findings = checkTemperatureStates(states, 3);
    REQUIRE(findings.size() == 1);
    CHECK(findings[0].code == ValidationCode::TemperatureNotFinite);
    CHECK(findings[0].cause == "below_absolute_zero");
    // 0 K itself is a temperature (the one policy, isAbsoluteTemperature).
    const std::vector<Temperature> zero = stateOf({0.0, 5.0});
    const std::vector<std::span<const Temperature>> zeroStates{zero};
    CHECK(checkTemperatureStates(zeroStates, 2).empty());
}

TEST_CASE("ThermalValidation_RejectsAStateOfTheWrongSizeBeforeReadingIt", "[thermal][validation][finite][cardinality]") {
    const std::vector<Temperature> shortState = stateOf({300.0, 310.0});
    const std::vector<std::span<const Temperature>> states{shortState};
    const std::vector<ValidationFinding> findings = checkTemperatureStates(states, 3);
    REQUIRE(findings.size() == 1);
    CHECK(findings[0].code == ValidationCode::ResultSizeMismatch);
    CHECK(findings[0].value == std::optional<double>{2.0});
    CHECK(findings[0].threshold == std::optional<double>{3.0});
}

TEST_CASE("ThermalValidation_EveryNonFiniteStateOfAHistoryIsFoundNotOnlyTheLast", "[thermal][validation][finite][transient]") {
    const std::vector<Temperature> good = stateOf({300.0, 310.0});
    std::vector<Temperature> middle = good;
    middle[0] = Temperature::fromSi(kNotANumber);
    std::vector<Temperature> last = good;
    last[1] = Temperature::fromSi(-5.0);
    const std::vector<std::span<const Temperature>> states{good, middle, good, last};
    const std::vector<double> times{0.0, 1.0, 2.0, 3.0};
    const std::vector<ValidationFinding> findings = checkTemperatureStates(states, 2, times);
    REQUIRE(findings.size() == 2);
    CHECK(findings[0].step == std::optional<std::size_t>{1});
    CHECK(findings[0].time == std::optional<double>{1.0});
    CHECK(findings[1].step == std::optional<std::size_t>{3});
    CHECK(findings[1].time == std::optional<double>{3.0});
}

// ===========================================================================
// The steady residual
// ===========================================================================

TEST_CASE("ThermalValidation_AcceptsQualifiedSteadyResidual", "[thermal][validation][residual][steady]") {
    const Net net = chainWithEndsHeld();
    const std::vector<Temperature> t = solvedChain(net);
    CHECK_THAT(t[1].si(), WithinAbs(350.0, 1e-10));
    const thermal::ResidualVerdict verdict = checkSteadyResidual(net.k, net.base.q, net.base.equations, net.base.constraints, t, {}, 1.0);
    CHECK(verdict.findings.empty());
    REQUIRE(verdict.residual.has_value());
    CHECK(*verdict.residual <= 1.0e-9);
}

TEST_CASE("ThermalValidation_RejectsBadSteadyResidual", "[thermal][validation][residual][steady]") {
    const Net net = chainWithEndsHeld();
    std::vector<Temperature> t = solvedChain(net);
    t[1] = Temperature::fromSi(350.5);   // a FINITE, plausible, wrong free temperature; the constraints still hold
    const thermal::ResidualVerdict verdict = checkSteadyResidual(net.k, net.base.q, net.base.equations, net.base.constraints, t, {}, 1.0);
    REQUIRE(verdict.findings.size() == 1);
    const ValidationFinding& finding = verdict.findings[0];
    CHECK(finding.code == ValidationCode::ResidualAboveTolerance);
    CHECK(finding.severity == ValidationSeverity::Error);
    CHECK(finding.threshold == std::optional<double>{thermal::ThermalSolverSettings{}.relativeResidualTolerance});
    REQUIRE(finding.value.has_value());
    CHECK(*finding.value > *finding.threshold);   // the number that was refused is reported
    CHECK(verdict.residual == finding.value);
}

TEST_CASE("ThermalValidation_SolverSuccessDoesNotOverrideResidualFailure", "[thermal][validation][residual][steady]") {
    // The solver would have said "success" for this system with the right field. The judgement here does not take its status as
    // an input at all: the same field that is accepted is refused after one free temperature moves, and the function was never told
    // anything about a backend.
    const Net net = chainWithEndsHeld();
    const std::vector<Temperature> right = solvedChain(net);
    std::vector<Temperature> wrong = right;
    wrong[1] = Temperature::fromSi(right[1].si() + 1.0e-3);
    CHECK(checkSteadyResidual(net.k, net.base.q, net.base.equations, net.base.constraints, right, {}, 1.0).findings.empty());
    CHECK(hasCode(checkSteadyResidual(net.k, net.base.q, net.base.equations, net.base.constraints, wrong, {}, 1.0).findings,
                  ValidationCode::ResidualAboveTolerance));
}

TEST_CASE("ThermalValidation_ResidualIsJudgedAgainstTheCanonicalToleranceAndNoOther", "[thermal][validation][residual][authority]") {
    const Net net = chainWithEndsHeld();
    std::vector<Temperature> t = solvedChain(net);
    t[1] = Temperature::fromSi(350.001);
    // The canonical setting decides. A user who asks for a loose tolerance gets it ...
    thermal::ThermalSolverSettings loose;
    loose.relativeResidualTolerance = 1.0;
    CHECK(checkSteadyResidual(net.k, net.base.q, net.base.equations, net.base.constraints, t, loose, 1.0).findings.empty());
    // ... a strict one refuses what the default accepts: there is no second threshold in the validator.
    thermal::ThermalSolverSettings strict;
    strict.relativeResidualTolerance = 1.0e-30;
    const std::vector<Temperature> right = solvedChain(net);
    std::vector<Temperature> nearlyRight = right;
    nearlyRight[1] = Temperature::fromSi(std::nextafter(right[1].si(), 1.0e9));
    CHECK(hasCode(checkSteadyResidual(net.k, net.base.q, net.base.equations, net.base.constraints, nearlyRight, strict, 1.0).findings,
                  ValidationCode::ResidualAboveTolerance));
}

TEST_CASE("ThermalValidation_ANotANumberResidualMetricIsAnErrorNotAPass", "[thermal][validation][residual][finite]") {
    const Net net = chainWithEndsHeld();
    for (const double bad : {kNotANumber, kInfinite}) {
        std::vector<Temperature> t = solvedChain(net);
        t[1] = Temperature::fromSi(bad);
        const thermal::ResidualVerdict verdict = checkSteadyResidual(net.k, net.base.q, net.base.equations, net.base.constraints, t, {}, 1.0);
        REQUIRE(verdict.findings.size() == 1);
        CHECK(verdict.findings[0].code == ValidationCode::ResidualNotFinite);
        CHECK_FALSE(verdict.residual.has_value());
    }
    // A pivot ratio that is not a number is below any floor.
    const std::vector<Temperature> t = solvedChain(net);
    const thermal::ResidualVerdict pivot = checkSteadyResidual(net.k, net.base.q, net.base.equations, net.base.constraints, t, {}, kNotANumber);
    CHECK(hasCode(pivot.findings, ValidationCode::PivotBelowFloor));
}

TEST_CASE("ThermalValidation_RejectsAPrescribedTemperatureThatIsNotHeld", "[thermal][validation][residual][constraint]") {
    const Net net = chainWithEndsHeld();
    std::vector<Temperature> t = solvedChain(net);
    t[0] = Temperature::fromSi(401.0);   // the prescription is 400 K
    const thermal::ResidualVerdict verdict = checkSteadyResidual(net.k, net.base.q, net.base.equations, net.base.constraints, t, {}, 1.0);
    CHECK(hasCode(verdict.findings, ValidationCode::ConstraintNotHeld));
}

TEST_CASE("ThermalValidation_RejectsAPivotRatioBelowTheCanonicalFloor", "[thermal][validation][residual][pivot]") {
    const Net net = chainWithEndsHeld();
    const std::vector<Temperature> t = solvedChain(net);
    const thermal::ThermalSolverSettings settings;
    CHECK(checkSteadyResidual(net.k, net.base.q, net.base.equations, net.base.constraints, t, settings, settings.pivotFloor).findings.empty());   // ON the floor
    const thermal::ResidualVerdict below =
        checkSteadyResidual(net.k, net.base.q, net.base.equations, net.base.constraints, t, settings, std::nextafter(settings.pivotFloor, 0.0));
    REQUIRE(below.findings.size() == 1);
    CHECK(below.findings[0].code == ValidationCode::PivotBelowFloor);
    CHECK(below.findings[0].threshold == std::optional<double>{settings.pivotFloor});
}

TEST_CASE("ThermalValidation_RejectsInvalidSolverSettingsInsteadOfComparingAgainstThem", "[thermal][validation][residual][settings]") {
    const Net net = chainWithEndsHeld();
    const std::vector<Temperature> t = solvedChain(net);
    thermal::ThermalSolverSettings broken;
    broken.relativeResidualTolerance = kNotANumber;   // `r < NaN` is false: it would otherwise reject or accept for no explicable reason
    const thermal::ResidualVerdict verdict = checkSteadyResidual(net.k, net.base.q, net.base.equations, net.base.constraints, t, broken, 1.0);
    REQUIRE(verdict.findings.size() >= 1);
    CHECK(verdict.findings[0].code == ValidationCode::SolverSettingsInvalid);
}

TEST_CASE("ThermalValidation_FullyConstrainedSteadyResidualIsNotAFailure", "[thermal][validation][residual][nfree]") {
    // Nfree = 0: every unknown is prescribed, T = Tc and there is nothing to solve. Not a singularity, not a residual failure.
    const Net net = makeNet(chainConductance({2.0, 2.0}), {0.0, 0.0, 0.0}, {{0, 400.0}, {1, 350.0}, {2, 300.0}});
    REQUIRE(net.base.equations.freeCount() == 0);
    const std::vector<Temperature> t = stateOf({400.0, 350.0, 300.0});
    const thermal::ResidualVerdict verdict = checkSteadyResidual(net.k, net.base.q, net.base.equations, net.base.constraints, t, {}, 1.0);
    CHECK(verdict.findings.empty());
    CHECK(verdict.residual == std::optional<double>{0.0});
    CHECK(checkSteadyWellPosedness(net.k, net.base.q, net.base.equations, net.base.constraints).empty());
}

TEST_CASE("ThermalValidation_ASteadyFieldOfTheWrongSizeIsASourceMismatchNotAResidual", "[thermal][validation][residual][cardinality]") {
    const Net net = chainWithEndsHeld();
    const std::vector<Temperature> shortField = stateOf({400.0, 350.0});
    const thermal::ResidualVerdict verdict = checkSteadyResidual(net.k, net.base.q, net.base.equations, net.base.constraints, shortField, {}, 1.0);
    REQUIRE(verdict.findings.size() == 1);
    CHECK(verdict.findings[0].code == ValidationCode::SourceMismatch);
}

// ===========================================================================
// The transient residual
// ===========================================================================

namespace {

struct Run {
    TrAlgebra algebra;
    thermal::ThermalTransientAlgebra run;
    std::vector<std::vector<Temperature>> states;
};

/// A three-node chain held at 400 K at one end, started at 300 K, advanced from 0 to @p end in steps of @p dt.
[[nodiscard]] Run makeRun(double dt, double end, TransientOutputPolicy output) {
    TrAlgebra a = makeTr(chainConductance({2.0, 2.0}), chainCapacity(3, 1.0), {0.0, 0.0, 0.0}, {{0, 400.0}});
    const std::vector<Temperature> initial = uniformState(3, 300.0);
    thermal::ThermalTransientAlgebra run = mustRunTr(a, initial, dt, end, output);
    std::vector<std::vector<Temperature>> states;
    for (std::size_t i = 0; i < run.sampleTimes.size(); ++i) {
        states.emplace_back(run.samples.begin() + static_cast<std::ptrdiff_t>(i * run.dofCount),
                            run.samples.begin() + static_cast<std::ptrdiff_t>((i + 1) * run.dofCount));
    }
    return Run{std::move(a), std::move(run), std::move(states)};
}

[[nodiscard]] thermal::TransientResidualVerdict judge(const Run& r, const std::vector<std::vector<Temperature>>& states,
                                                      std::span<const thermal::TransientStepRecord> records,
                                                      TransientOutputPolicy output, const thermal::ThermalSolverSettings& settings = {}) {
    const std::vector<std::span<const Temperature>> spans = spansOf(states);
    return checkTransientResiduals(r.algebra.k, r.algebra.c, r.algebra.base.q, r.algebra.base.equations, r.algebra.base.constraints, settings,
                                   r.run.grid, records, r.run.initial, spans, output);
}

} // namespace

TEST_CASE("ThermalValidation_AcceptsQualifiedTransientResiduals", "[thermal][validation][residual][transient]") {
    // 0.5 s steps to 1.7 s: three full steps and a SHORTENED last one of 0.2 s.
    const Run r = makeRun(0.5, 1.7, TransientOutputPolicy::EveryStep);
    REQUIRE(r.run.grid.stepCount() == 4);
    REQUIRE(r.run.grid.hasPartialFinalStep());
    const thermal::TransientResidualVerdict verdict = judge(r, r.states, r.run.steps, TransientOutputPolicy::EveryStep);
    CHECK(verdict.findings.empty());
    REQUIRE(verdict.worstResidual.has_value());
    CHECK(*verdict.worstResidual <= 1.0e-9);
    REQUIRE(verdict.worstStep.has_value());
    CHECK(*verdict.worstStep >= 1);
    CHECK(*verdict.worstStep <= 4);
    // The worst step is reported WITH its time and its actual size.
    CHECK(verdict.worstStepSize == std::optional<double>{r.run.grid.stepAt(*verdict.worstStep - 1)});
    CHECK(verdict.worstTime == std::optional<double>{r.run.grid.timeAt(*verdict.worstStep)});
}

TEST_CASE("ThermalValidation_RejectsAnyBadTransientStepResidualNotOnlyTheLastOne", "[thermal][validation][residual][transient]") {
    const Run r = makeRun(0.5, 1.7, TransientOutputPolicy::EveryStep);
    std::vector<std::vector<Temperature>> corrupted = r.states;
    corrupted[2][1] = Temperature::fromSi(corrupted[2][1].si() + 0.3);   // state 2 (t = 1.0 s), a FREE node, finite and wrong
    const thermal::TransientResidualVerdict verdict = judge(r, corrupted, r.run.steps, TransientOutputPolicy::EveryStep);
    // State 2 is the END of step 2 and the START of step 3: both are wrong, and the LAST step is fine.
    std::set<std::size_t> steps;
    for (const ValidationFinding& finding : verdict.findings) {
        CHECK(finding.code == ValidationCode::ResidualAboveTolerance);
        REQUIRE(finding.step.has_value());
        steps.insert(*finding.step);
    }
    CHECK(steps == std::set<std::size_t>{2, 3});
    const auto second = firstWithCode(verdict.findings, ValidationCode::ResidualAboveTolerance);
    REQUIRE(second.has_value());
    CHECK(second->step == std::optional<std::size_t>{2});
    CHECK(second->time == std::optional<double>{1.0});
    CHECK(second->stepSize == std::optional<double>{0.5});
    CHECK(second->threshold == std::optional<double>{thermal::ThermalSolverSettings{}.relativeResidualTolerance});
    CHECK(second->value.value() > *second->threshold);
    // The worst step is the one the validator names.
    REQUIRE(verdict.worstStep.has_value());
    CHECK((*verdict.worstStep == 2 || *verdict.worstStep == 3));
}

TEST_CASE("ThermalValidation_TheFinalPartialStepIsJudgedWithItsActualStepSize", "[thermal][validation][residual][transient][partial-step]") {
    const Run r = makeRun(0.5, 1.7, TransientOutputPolicy::EveryStep);
    std::vector<std::vector<Temperature>> corrupted = r.states;
    corrupted[4][2] = Temperature::fromSi(corrupted[4][2].si() - 0.2);
    const thermal::TransientResidualVerdict verdict = judge(r, corrupted, r.run.steps, TransientOutputPolicy::EveryStep);
    REQUIRE(hasCode(verdict.findings, ValidationCode::ResidualAboveTolerance));
    const auto last = firstWithCode(verdict.findings, ValidationCode::ResidualAboveTolerance);
    REQUIRE(last.has_value());
    CHECK(last->step == std::optional<std::size_t>{4});
    CHECK_THAT(last->stepSize.value(), WithinAbs(0.2, 1.0e-12));   // 1.7 - 3 * 0.5, NOT the nominal 0.5
    CHECK(last->time == std::optional<double>{1.7});
    // Judged with the NOMINAL step the same field would be a different (wrong) verdict: the real run, uncorrupted, passes with 0.2.
    CHECK(judge(r, r.states, r.run.steps, TransientOutputPolicy::EveryStep).findings.empty());
}

TEST_CASE("ThermalValidation_FinalOnlyHistoriesAreJudgedByTheirRecordedStepResiduals", "[thermal][validation][residual][transient][records]") {
    // FinalOnly stores only the last state, so the intermediate steps cannot be recomputed; their RECORDED residuals (the qualified
    // per-step metrics, as the integrator accepted them) are the evidence, and every one of them is held to the tolerance.
    const Run r = makeRun(0.5, 1.7, TransientOutputPolicy::FinalOnly);
    REQUIRE(r.states.size() == 1);
    CHECK(judge(r, r.states, r.run.steps, TransientOutputPolicy::FinalOnly).findings.empty());

    std::vector<thermal::TransientStepRecord> records = r.run.steps;
    records[2].physicalResidual = 1.0e-3;   // step 3
    const thermal::TransientResidualVerdict verdict = judge(r, r.states, records, TransientOutputPolicy::FinalOnly);
    REQUIRE(verdict.findings.size() == 1);
    CHECK(verdict.findings[0].code == ValidationCode::ResidualAboveTolerance);
    CHECK(verdict.findings[0].step == std::optional<std::size_t>{3});
    CHECK(verdict.findings[0].cause == "physical_recorded");

    records = r.run.steps;
    records[0].reducedResidual = 2.0e-3;
    const thermal::TransientResidualVerdict reduced = judge(r, r.states, records, TransientOutputPolicy::FinalOnly);
    REQUIRE(reduced.findings.size() == 1);
    CHECK(reduced.findings[0].cause == "reduced_recorded");
    CHECK(reduced.findings[0].step == std::optional<std::size_t>{1});
}

TEST_CASE("ThermalValidation_ASingleStepFinalOnlyRunIsStillRecomputedFromTheInitialState", "[thermal][validation][residual][transient]") {
    const Run r = makeRun(0.5, 0.5, TransientOutputPolicy::FinalOnly);
    REQUIRE(r.run.grid.stepCount() == 1);
    CHECK(judge(r, r.states, r.run.steps, TransientOutputPolicy::FinalOnly).findings.empty());
    std::vector<std::vector<Temperature>> corrupted = r.states;
    corrupted[0][1] = Temperature::fromSi(corrupted[0][1].si() + 0.4);
    const thermal::TransientResidualVerdict verdict = judge(r, corrupted, r.run.steps, TransientOutputPolicy::FinalOnly);
    CHECK(hasCode(verdict.findings, ValidationCode::ResidualAboveTolerance));
}

TEST_CASE("ThermalValidation_ANotANumberTransientRecordIsAnErrorNotAPass", "[thermal][validation][residual][transient][finite]") {
    const Run r = makeRun(0.5, 1.0, TransientOutputPolicy::EveryStep);
    for (const double bad : {kNotANumber, kInfinite}) {
        std::vector<thermal::TransientStepRecord> records = r.run.steps;
        records[1].physicalResidual = bad;
        const thermal::TransientResidualVerdict verdict = judge(r, r.states, records, TransientOutputPolicy::EveryStep);
        REQUIRE(hasCode(verdict.findings, ValidationCode::ResidualNotFinite));
        CHECK(firstWithCode(verdict.findings, ValidationCode::ResidualNotFinite)->step == std::optional<std::size_t>{2});
    }
    // A pivot ratio below the floor at one step is named with that step.
    std::vector<thermal::TransientStepRecord> records = r.run.steps;
    records[0].pivotRatio = 1.0e-14;
    records[0].kernelInvoked = true;
    const thermal::TransientResidualVerdict pivot = judge(r, r.states, records, TransientOutputPolicy::EveryStep);
    REQUIRE(hasCode(pivot.findings, ValidationCode::PivotBelowFloor));
    CHECK(firstWithCode(pivot.findings, ValidationCode::PivotBelowFloor)->step == std::optional<std::size_t>{1});
}

TEST_CASE("ThermalValidation_ATransientHistoryWithTheWrongNumberOfStepRecordsViolatesItsContract", "[thermal][validation][residual][transient][history]") {
    const Run r = makeRun(0.5, 1.0, TransientOutputPolicy::EveryStep);
    const std::span<const thermal::TransientStepRecord> shortRecords{r.run.steps.data(), r.run.steps.size() - 1};
    const thermal::TransientResidualVerdict verdict = judge(r, r.states, shortRecords, TransientOutputPolicy::EveryStep);
    REQUIRE(verdict.findings.size() == 1);
    CHECK(verdict.findings[0].code == ValidationCode::HistoryContractViolated);
}

TEST_CASE("ThermalValidation_TransientResidualsUseTheCanonicalToleranceToo", "[thermal][validation][residual][transient][authority]") {
    const Run r = makeRun(0.5, 1.0, TransientOutputPolicy::EveryStep);
    std::vector<std::vector<Temperature>> corrupted = r.states;
    corrupted[1][1] = Temperature::fromSi(corrupted[1][1].si() + 0.05);
    CHECK_FALSE(judge(r, corrupted, r.run.steps, TransientOutputPolicy::EveryStep).findings.empty());
    thermal::ThermalSolverSettings loose;
    loose.relativeResidualTolerance = 1.0;
    CHECK(judge(r, corrupted, r.run.steps, TransientOutputPolicy::EveryStep, loose).findings.empty());
}

// ===========================================================================
// The heat balance
// ===========================================================================

namespace {

/// A balance obtained from the qualified algebraic seam: two nodes, g = 2, 400 K held and 300 K held, so +-200 W of reaction.
[[nodiscard]] ThermalHeatBalance goodBalance() {
    const Net net = makeNet(Dense2{{2.0, -2.0}, {-2.0, 2.0}}, {0.0, 0.0}, {{0, 400.0}, {1, 300.0}});
    return mustBalance(thermal::steadyHeatBalance(algebraOf(net), kelvin({400.0, 300.0})));
}

} // namespace

TEST_CASE("ThermalValidation_AcceptsQualifiedSteadyBalance", "[thermal][validation][balance][steady]") {
    const ThermalHeatBalance good = goodBalance();
    REQUIRE(good.status == BalanceStatus::Balanced);
    const std::vector<ThermalHeatBalance> states{good};
    const thermal::BalanceVerdict verdict = checkHeatBalanceStates(states, 1);
    CHECK(verdict.findings.empty());
    REQUIRE(verdict.worstResidual.has_value());
    CHECK(*verdict.worstResidual <= 1.0e-12);
    CHECK(verdict.worstStep == std::optional<std::size_t>{0});
}

TEST_CASE("ThermalValidation_RejectsBadSteadyBalance", "[thermal][validation][balance][steady]") {
    ThermalHeatBalance bad = goodBalance();
    bad.terms.convectionHeat = bad.terms.convectionHeat + Power::fromSi(5.0);   // 5 W of heat that goes nowhere
    // The state still RECORDS itself as balanced: the validator does not take that on trust.
    REQUIRE(bad.status == BalanceStatus::Balanced);
    const std::vector<ThermalHeatBalance> states{bad};
    const thermal::BalanceVerdict verdict = checkHeatBalanceStates(states, 1);
    REQUIRE(verdict.findings.size() == 1);
    const ValidationFinding& finding = verdict.findings[0];
    CHECK(finding.code == ValidationCode::BalanceOutOfTolerance);
    CHECK(finding.severity == ValidationSeverity::Error);
    CHECK(finding.cause == "recomputed");
    CHECK_THAT(finding.value.value(), WithinAbs(5.0, 1e-9));
    CHECK(finding.threshold.value() < 1.0e-9);   // the policy is BALANCE's: 1e-12 W + 1e-13 of the scale
    CHECK(finding.unit == "W");
}

TEST_CASE("ThermalValidation_BalanceIsJudgedByBalancesOwnTolerancePolicy", "[thermal][validation][balance][authority]") {
    // Exactly the policy P18-BALANCE-001 froze, consumed through `balanceWithinTolerance`: a residual just inside is accepted and just
    // outside is refused, with the SAME constants and no new one.
    ThermalHeatBalance state = goodBalance();
    const double scale = state.grossScale().si() + state.arithmeticScale.si();
    const double allowed = thermal::kBalanceAbsoluteToleranceW + thermal::kBalanceRelativeTolerance * scale;
    ThermalHeatBalance inside = state;
    inside.terms.convectionHeat = inside.terms.convectionHeat + Power::fromSi(0.5 * allowed);
    ThermalHeatBalance outside = state;
    outside.terms.convectionHeat = outside.terms.convectionHeat + Power::fromSi(4.0 * allowed);
    CHECK(thermal::balanceWithinTolerance(inside.residual(), inside.grossScale(), inside.arithmeticScale));
    CHECK_FALSE(thermal::balanceWithinTolerance(outside.residual(), outside.grossScale(), outside.arithmeticScale));
    CHECK(checkHeatBalanceStates(std::vector<ThermalHeatBalance>{inside}, 1).findings.empty());
    CHECK(hasCode(checkHeatBalanceStates(std::vector<ThermalHeatBalance>{outside}, 1).findings, ValidationCode::BalanceOutOfTolerance));
}

TEST_CASE("ThermalValidation_ABalanceThatRecordsItselfOutOfToleranceIsNotAccepted", "[thermal][validation][balance]") {
    ThermalHeatBalance state = goodBalance();
    state.status = BalanceStatus::OutOfTolerance;   // the numbers are fine; the state disagrees with itself
    const thermal::BalanceVerdict verdict = checkHeatBalanceStates(std::vector<ThermalHeatBalance>{state}, 1);
    REQUIRE(verdict.findings.size() == 1);
    CHECK(verdict.findings[0].code == ValidationCode::BalanceOutOfTolerance);
    CHECK(verdict.findings[0].cause == "recorded_status");
}

TEST_CASE("ThermalValidation_RejectsNonFiniteBalance", "[thermal][validation][balance][finite]") {
    const auto corrupted = [](auto&& edit) {
        ThermalHeatBalance state = goodBalance();
        edit(state);
        return checkHeatBalanceStates(std::vector<ThermalHeatBalance>{state}, 1);
    };
    // Every number a state holds is checked for finiteness BEFORE any comparison: none of these can fall through one.
    const std::vector<thermal::BalanceVerdict> verdicts{
        corrupted([](ThermalHeatBalance& s) { s.terms.appliedSurfaceHeat = Power::fromSi(kNotANumber); }),
        corrupted([](ThermalHeatBalance& s) { s.terms.generatedHeat = Power::fromSi(kInfinite); }),
        corrupted([](ThermalHeatBalance& s) { s.terms.prescribedTemperatureReaction = Power::fromSi(kNotANumber); }),
        corrupted([](ThermalHeatBalance& s) { s.terms.storedEnergyRate = Power::fromSi(kNotANumber); }),
        corrupted([](ThermalHeatBalance& s) { s.arithmeticScale = Power::fromSi(kNotANumber); }),
        corrupted([](ThermalHeatBalance& s) { s.reactions.at(0).heatInto = Power::fromSi(kInfinite); }),
        corrupted([](ThermalHeatBalance& s) { s.surfaceLoads.push_back(LoadHeat{.load = ThermalLoadId::fromValue(1), .heatInto = Power::fromSi(kNotANumber)}); }),
        corrupted([](ThermalHeatBalance& s) { s.storedEnergyChange = Energy::fromSi(kNotANumber); }),
    };
    for (std::size_t i = 0; i < verdicts.size(); ++i) {
        INFO("corruption " << i);
        REQUIRE(verdicts[i].findings.size() == 1);
        CHECK(verdicts[i].findings[0].code == ValidationCode::BalanceNotFinite);
        CHECK(verdicts[i].findings[0].severity == ValidationSeverity::Error);
    }
}

TEST_CASE("ThermalValidation_ABalanceWithTheWrongNumberOfStatesIsRefused", "[thermal][validation][balance][cardinality]") {
    const ThermalHeatBalance good = goodBalance();
    const thermal::BalanceVerdict fewer = checkHeatBalanceStates(std::vector<ThermalHeatBalance>{good}, 3);
    REQUIRE(fewer.findings.size() == 1);
    CHECK(fewer.findings[0].code == ValidationCode::BalanceStatesMismatch);
    CHECK(hasCode(checkHeatBalanceStates({}, 1).findings, ValidationCode::BalanceStatesMismatch));
}

TEST_CASE("ThermalValidation_ABalanceMustAccountForEveryPreparedCategory", "[thermal][validation][balance][attribution]") {
    // One entry per prepared load, per convection condition and per prescribed UNKNOWN. The balance of the two-node chain has two reactions
    // and nothing else.
    const ThermalHeatBalance good = goodBalance();
    REQUIRE(good.reactions.size() == 2);
    const std::vector<ThermalHeatBalance> states{good};
    CHECK(checkBalanceAttribution(states, 0, 0, 2).empty());
    // A load, a convection condition or a reaction the system was made from that the balance does not list; and one it lists twice.
    for (const auto& [loads, convections, reactions] : std::vector<std::array<std::size_t, 3>>{{1, 0, 2}, {0, 1, 2}, {0, 0, 3}, {0, 0, 1}}) {
        const std::vector<ValidationFinding> findings = checkBalanceAttribution(states, loads, convections, reactions);
        INFO(loads << " loads, " << convections << " conditions, " << reactions << " reactions");
        REQUIRE(findings.size() == 1);
        CHECK(findings[0].code == ValidationCode::BalanceAttributionMismatch);
        CHECK(findings[0].severity == ValidationSeverity::Error);
    }
    // A surplus entry in the balance itself.
    ThermalHeatBalance surplus = good;
    surplus.surfaceLoads.push_back(LoadHeat{.load = ThermalLoadId::fromValue(9), .heatInto = Power::fromSi(1.0)});
    CHECK(hasCode(checkBalanceAttribution(std::vector<ThermalHeatBalance>{surplus}, 0, 0, 2), ValidationCode::BalanceAttributionMismatch));
}

TEST_CASE("ThermalValidation_EveryStepOfATransientBalanceIsJudgedAndTheWorstIsReported", "[thermal][validation][balance][transient]") {
    ThermalHeatBalance a = goodBalance();
    a.step = 1;
    a.time = Time::fromSi(0.5);
    a.stepSize = Time::fromSi(0.5);
    ThermalHeatBalance b = a;
    b.step = 2;
    b.time = Time::fromSi(1.0);
    b.terms.convectionHeat = b.terms.convectionHeat + Power::fromSi(2.0);   // step 2 is bad
    ThermalHeatBalance c = a;
    c.step = 3;
    c.time = Time::fromSi(1.2);
    c.stepSize = Time::fromSi(0.2);
    const thermal::BalanceVerdict verdict = checkHeatBalanceStates(std::vector<ThermalHeatBalance>{a, b, c}, 3);
    REQUIRE(verdict.findings.size() == 1);
    CHECK(verdict.findings[0].step == std::optional<std::size_t>{2});
    CHECK(verdict.findings[0].time == std::optional<double>{1.0});
    CHECK(verdict.findings[0].stepSize == std::optional<double>{0.5});
    CHECK(verdict.worstStep == std::optional<std::size_t>{2});
    CHECK(verdict.worstTime == std::optional<double>{1.0});
}

// ===========================================================================
// The independence of the two gates
// ===========================================================================

TEST_CASE("ThermalValidation_BalancePassDoesNotOverrideResidualFailure", "[thermal][validation][residual][balance][independence]") {
    // THE CANCELLATION TRAP. K = I, Q = 0, T = [+5, -5]: the free equations are wrong by 5 W each way, and the global balance is
    // exactly zero because the two errors cancel. The balance of this field is a PASS and the residual is a FAIL, and the residual is
    // the gate that is not allowed to be rescued.
    const Net net = makeNet(Dense2{{1.0, 0.0}, {0.0, 1.0}}, {0.0, 0.0});
    const std::vector<Temperature> wrong = stateOf({5.0, -5.0});
    // The books of the cancelling field: B = sum(K T - Q) = 0.
    const ThermalHeatBalance cancelling = mustBalance(thermal::steadyHeatBalance(algebraOf(net), wrong));
    CHECK_THAT(cancelling.residual().si(), WithinAbs(0.0, 1e-12));
    CHECK(checkHeatBalanceStates(std::vector<ThermalHeatBalance>{cancelling}, 1).findings.empty());   // the balance PASSES
    // ... and the free residual of the same field FAILS.
    const thermal::ResidualVerdict residual =
        checkSteadyResidual(net.k, net.base.q, net.base.equations, net.base.constraints, wrong, {}, 1.0);
    CHECK(hasCode(residual.findings, ValidationCode::ResidualAboveTolerance));
}

TEST_CASE("ThermalValidation_ResidualPassDoesNotOverrideBalanceFailure", "[thermal][validation][residual][balance][independence]") {
    // The converse: a field that solves the algebra (residual PASS) with a corrupted balance attribution (a 7 W category that is
    // nowhere in the system). The balance gate fails; the residual gate has nothing to say about it.
    const Net net = chainWithEndsHeld();
    const std::vector<Temperature> t = solvedChain(net);
    CHECK(checkSteadyResidual(net.k, net.base.q, net.base.equations, net.base.constraints, t, {}, 1.0).findings.empty());
    ThermalHeatBalance balance = mustBalance(thermal::steadyHeatBalance(algebraOf(net), t));
    REQUIRE(balance.status == BalanceStatus::Balanced);
    balance.terms.generatedHeat = balance.terms.generatedHeat + Power::fromSi(7.0);
    CHECK(hasCode(checkHeatBalanceStates(std::vector<ThermalHeatBalance>{balance}, 1).findings, ValidationCode::BalanceOutOfTolerance));
}

// ===========================================================================
// The recovered fields
// ===========================================================================

namespace {

constexpr std::size_t kElements = 2;
constexpr std::size_t kDofs = 4;

[[nodiscard]] thermal::ThermalPostState goodPostState() {
    thermal::ThermalPostState state;
    state.temperatures = stateOf({300.0, 310.0, 320.0, 330.0});
    for (std::size_t e = 0; e < kElements; ++e) {
        state.fields.gradients.push_back(thermal::TemperatureGradient3{TemperatureGradient::fromSi(1.0 + static_cast<double>(e)), TemperatureGradient::fromSi(-2.0),
                                                                       TemperatureGradient::fromSi(3.0)});
        const thermal::HeatFlux3 flux{HeatFlux::fromSi(-45.0 - static_cast<double>(e)), HeatFlux::fromSi(90.0), HeatFlux::fromSi(-135.0)};
        state.fields.heatFluxes.push_back(flux);
        state.fields.heatFluxMagnitudes.push_back(flux.magnitude());
    }
    return state;
}

[[nodiscard]] std::vector<ValidationFinding> judgePost(const std::vector<thermal::ThermalPostState>& states) {
    std::vector<std::vector<Temperature>> solver;
    for (std::size_t i = 0; i < states.size(); ++i) {
        solver.push_back(stateOf({300.0, 310.0, 320.0, 330.0}));
    }
    const std::vector<std::span<const Temperature>> spans = spansOf(solver);
    return checkPostStates(states, kElements, kDofs, spans);
}

} // namespace

TEST_CASE("ThermalValidation_AcceptsAFiniteCompleteRecoveredResult", "[thermal][validation][post][finite]") {
    CHECK(judgePost({goodPostState()}).empty());
    CHECK(judgePost({goodPostState(), goodPostState(), goodPostState()}).empty());
}

TEST_CASE("ThermalValidation_RejectsNonFiniteGradient", "[thermal][validation][post][finite]") {
    for (const double bad : {kNotANumber, kInfinite}) {
        thermal::ThermalPostState state = goodPostState();
        state.fields.gradients[1].y = TemperatureGradient::fromSi(bad);
        const std::vector<ValidationFinding> findings = judgePost({state});
        REQUIRE(findings.size() == 1);
        CHECK(findings[0].code == ValidationCode::PostFieldNotFinite);
        CHECK(findings[0].severity == ValidationSeverity::Error);
        CHECK_THAT(findings[0].message, ContainsSubstring("ordinal 1"));
    }
}

TEST_CASE("ThermalValidation_RejectsNonFiniteHeatFlux", "[thermal][validation][post][finite]") {
    // The temperatures are finite and balanced and the gradients are fine: the flux alone is corrupt, and it is still refused.
    thermal::ThermalPostState state = goodPostState();
    state.fields.heatFluxes[0].z = HeatFlux::fromSi(kNotANumber);
    const std::vector<ValidationFinding> findings = judgePost({state});
    REQUIRE(findings.size() == 1);
    CHECK(findings[0].code == ValidationCode::PostFieldNotFinite);
    state = goodPostState();
    state.fields.heatFluxMagnitudes[1] = HeatFlux::fromSi(kInfinite);
    CHECK(hasCode(judgePost({state}), ValidationCode::PostFieldNotFinite));
}

TEST_CASE("ThermalValidation_RejectsAMissingElementResultInsteadOfSkippingIt", "[thermal][validation][post][cardinality]") {
    thermal::ThermalPostState state = goodPostState();
    state.fields.gradients.pop_back();
    const std::vector<ValidationFinding> findings = judgePost({state});
    REQUIRE(findings.size() == 1);
    CHECK(findings[0].code == ValidationCode::PostCardinalityMismatch);
    state = goodPostState();
    state.fields.heatFluxMagnitudes.pop_back();
    CHECK(hasCode(judgePost({state}), ValidationCode::PostCardinalityMismatch));
    state = goodPostState();
    state.temperatures.pop_back();
    CHECK(hasCode(judgePost({state}), ValidationCode::PostCardinalityMismatch));
}

TEST_CASE("ThermalValidation_RejectsRecoveredTemperaturesThatAreNotTheSolversBitForBit", "[thermal][validation][post][source]") {
    thermal::ThermalPostState state = goodPostState();
    state.temperatures[2] = Temperature::fromSi(std::nextafter(320.0, 400.0));   // one ulp
    const std::vector<ValidationFinding> findings = judgePost({state});
    REQUIRE(findings.size() == 1);
    CHECK(findings[0].code == ValidationCode::PostTemperatureMismatch);
}

TEST_CASE("ThermalValidation_RejectsARecoveredResultWithTheWrongNumberOfStates", "[thermal][validation][post][cardinality]") {
    const thermal::ThermalPostState good = goodPostState();
    const std::vector<std::vector<Temperature>> solver{stateOf({300.0, 310.0, 320.0, 330.0}), stateOf({300.0, 310.0, 320.0, 330.0})};
    const std::vector<std::span<const Temperature>> spans = spansOf(solver);
    const std::vector<thermal::ThermalPostState> one{good};
    const std::vector<ValidationFinding> findings = checkPostStates(one, kElements, kDofs, spans);
    CHECK(hasCode(findings, ValidationCode::PostCardinalityMismatch));
}

// ===========================================================================
// Well-posedness
// ===========================================================================

namespace {

[[nodiscard]] std::vector<ValidationFinding> steadyWellPosed(const Dense2& dk, const std::vector<std::pair<std::size_t, double>>& prescribed) {
    const Algebra a = makeAlgebra(dk, std::vector<double>(dk.size(), 0.0), prescribed);
    const thermal::ConductanceMatrix k{a.k};
    return checkSteadyWellPosedness(k, a.q, a.equations, a.constraints);
}

} // namespace

TEST_CASE("ThermalValidation_SteadyPureNeumannFails", "[thermal][validation][wellposed][steady]") {
    // A chain with no prescribed temperature and no convection: the constant-temperature mode is in the null space.
    const std::vector<ValidationFinding> findings = steadyWellPosed(chainConductance({1.0, 1.0}), {});
    REQUIRE(findings.size() == 1);
    CHECK(findings[0].code == ValidationCode::NoTemperatureDatum);
    CHECK(findings[0].severity == ValidationSeverity::Error);
    CHECK(findings[0].count == std::optional<std::size_t>{1});
}

TEST_CASE("ThermalValidation_SteadyDirichletAnchoredPasses", "[thermal][validation][wellposed][steady]") {
    CHECK(steadyWellPosed(chainConductance({1.0, 1.0}), {{0, 300.0}}).empty());
    CHECK(steadyWellPosed(chainConductance({1.0, 1.0}), {{1, 300.0}}).empty());   // the anchor need not be at an end
}

TEST_CASE("ThermalValidation_SteadyRobinOnlyPasses", "[thermal][validation][wellposed][steady]") {
    // No prescribed temperature at all, a positive convection coefficient on one node: anchored. NOT "Dirichlet count = 0, reject".
    Dense2 k = chainConductance({1.0, 1.0});
    k[2][2] += 0.01;   // h A at the last node
    const std::vector<ValidationFinding> findings = steadyWellPosed(k, {});
    CHECK(findings.empty());
    // A vanishing "convection" is rounding, not an anchor.
    Dense2 noise = chainConductance({1.0, 1.0});
    noise[2][2] += 3.0e-16;
    CHECK(hasCode(steadyWellPosed(noise, {}), ValidationCode::NoTemperatureDatum));
}

TEST_CASE("ThermalValidation_ASystemWhoseReductionOverflowsIsNotFinite", "[thermal][validation][wellposed][steady][finite][refusals]") {
    // Two unknowns coupled by the largest conductance a double holds, one of them held at 300 K: the reduced right-hand side is
    // 300 * 1e308, which is not a number. That is a system nobody can solve, and it is said BEFORE the solver is asked.
    Dense2 k(2, std::vector<double>(2, 0.0));
    k[0][0] = 1.0;
    k[0][1] = -1.0e308;
    k[1][0] = -1.0e308;
    k[1][1] = 1.0;
    const std::vector<ValidationFinding> findings = steadyWellPosed(k, {{0, 300.0}});
    REQUIRE(findings.size() == 1);
    CHECK(findings[0].code == ValidationCode::SystemNotFinite);
    CHECK(findings[0].severity == ValidationSeverity::Error);
    CHECK(findings[0].rule == ValidationRule::WellPosed);
    CHECK(!findings[0].cause.empty());
}

TEST_CASE("ThermalValidation_FullyConstrainedSteadyPasses", "[thermal][validation][wellposed][steady]") {
    CHECK(steadyWellPosed(chainConductance({1.0, 1.0}), {{0, 300.0}, {1, 310.0}, {2, 320.0}}).empty());
}

TEST_CASE("ThermalValidation_DisconnectedUnanchoredSteadyFails", "[thermal][validation][wellposed][steady][disconnected]") {
    // Two independent regions: {0,1} held at 300 K, {2,3} coupled to nothing prescribed. The first is anchored and the second is not.
    Dense2 k(4, std::vector<double>(4, 0.0));
    k[0][0] = 1.0;
    k[0][1] = -1.0;
    k[1][0] = -1.0;
    k[1][1] = 1.0;
    k[2][2] = 1.0;
    k[2][3] = -1.0;
    k[3][2] = -1.0;
    k[3][3] = 1.0;
    const std::vector<ValidationFinding> findings = steadyWellPosed(k, {{0, 300.0}});
    REQUIRE(findings.size() == 1);
    CHECK(findings[0].code == ValidationCode::UnanchoredRegion);   // another region IS anchored: not "no datum at all"
    CHECK(findings[0].count == std::optional<std::size_t>{1});
}

TEST_CASE("ThermalValidation_DisconnectedAllAnchoredSteadyPasses", "[thermal][validation][wellposed][steady][disconnected]") {
    Dense2 k(4, std::vector<double>(4, 0.0));
    k[0][0] = 1.0;
    k[0][1] = -1.0;
    k[1][0] = -1.0;
    k[1][1] = 1.0;
    k[2][2] = 1.0;
    k[2][3] = -1.0;
    k[3][2] = -1.0;
    k[3][3] = 1.0;
    CHECK(steadyWellPosed(k, {{0, 300.0}, {3, 400.0}}).empty());   // each independent region is held
}

TEST_CASE("ThermalValidation_TransientPureNeumannPasses", "[thermal][validation][wellposed][transient]") {
    // The steady datum rule does NOT apply: no prescribed temperature, no convection, a positive capacity -- well posed.
    const TrAlgebra a = makeTr(chainConductance({1.0, 1.0}), chainCapacity(3, 1.0), {0.0, 0.0, 0.0});
    const std::vector<ValidationFinding> findings =
        checkTransientWellPosedness(a.k, std::optional<thermal::CapacityMatrix>{a.c}, a.base.constraints, 0);
    REQUIRE(findings.size() == 1);
    CHECK(findings[0].code == ValidationCode::TransientWithoutDatum);
    CHECK(findings[0].severity == ValidationSeverity::Info);
    // ... and the SAME system fails the steady rule, which is exactly why the two must not be confused.
    CHECK(hasCode(checkSteadyWellPosedness(a.k, a.base.q, a.base.equations, a.base.constraints), ValidationCode::NoTemperatureDatum));
    // Anchored transients say nothing at all.
    const TrAlgebra anchored = makeTr(chainConductance({1.0, 1.0}), chainCapacity(3, 1.0), {0.0, 0.0, 0.0}, {{0, 300.0}});
    CHECK(checkTransientWellPosedness(anchored.k, std::optional<thermal::CapacityMatrix>{anchored.c}, anchored.base.constraints, 0).empty());
    // A convection facet is a datum too, as far as the information is concerned.
    CHECK(checkTransientWellPosedness(a.k, std::optional<thermal::CapacityMatrix>{a.c}, a.base.constraints, 2).empty());
}

TEST_CASE("ThermalValidation_TransientDisconnectedComponentsWithCapacityPass", "[thermal][validation][wellposed][transient][disconnected]") {
    Dense2 k(4, std::vector<double>(4, 0.0));
    k[0][0] = 1.0;
    k[0][1] = -1.0;
    k[1][0] = -1.0;
    k[1][1] = 1.0;
    k[2][2] = 1.0;
    k[2][3] = -1.0;
    k[3][2] = -1.0;
    k[3][3] = 1.0;
    const TrAlgebra a = makeTr(k, chainCapacity(4, 1.0), {0.0, 0.0, 0.0, 0.0});
    const std::vector<ValidationFinding> findings =
        checkTransientWellPosedness(a.k, std::optional<thermal::CapacityMatrix>{a.c}, a.base.constraints, 0);
    CHECK(errorsIn(findings) == 0);   // disconnected is not wrong; no unanchored-region rule in a transient
}

TEST_CASE("ThermalValidation_TransientMissingCapacityFails", "[thermal][validation][wellposed][transient][capacity]") {
    const TrAlgebra a = makeTr(chainConductance({1.0, 1.0}), chainCapacity(3, 1.0), {0.0, 0.0, 0.0});
    const std::vector<ValidationFinding> findings = checkTransientWellPosedness(a.k, std::nullopt, a.base.constraints, 0);
    REQUIRE(findings.size() == 1);
    CHECK(findings[0].code == ValidationCode::CapacityMissing);
    CHECK(findings[0].severity == ValidationSeverity::Error);
}

TEST_CASE("ThermalValidation_TransientCapacityMustBeFiniteAndPositiveOnTheDiagonal", "[thermal][validation][wellposed][transient][capacity]") {
    // No zero-capacity fallback. A zero on the diagonal is a node that stores nothing: refused.
    Dense2 zero = chainCapacity(3, 1.0);
    zero[1][1] = 0.0;
    const TrAlgebra a = makeTr(chainConductance({1.0, 1.0}), zero, {0.0, 0.0, 0.0});
    const std::vector<ValidationFinding> findings =
        checkTransientWellPosedness(a.k, std::optional<thermal::CapacityMatrix>{a.c}, a.base.constraints, 0);
    REQUIRE(findings.size() == 1);
    CHECK(findings[0].code == ValidationCode::CapacityInvalid);
    CHECK(findings[0].count == std::optional<std::size_t>{1});

    Dense2 negative = chainCapacity(3, 1.0);
    negative[2][2] = -0.5;
    const TrAlgebra b = makeTr(chainConductance({1.0, 1.0}), negative, {0.0, 0.0, 0.0});
    CHECK(hasCode(checkTransientWellPosedness(b.k, std::optional<thermal::CapacityMatrix>{b.c}, b.base.constraints, 0),
                  ValidationCode::CapacityInvalid));

    // A capacity of another size is a source mismatch, not a quiet pass.
    const TrAlgebra small = makeTr(chainConductance({1.0}), chainCapacity(2, 1.0), {0.0, 0.0});
    CHECK(hasCode(checkTransientWellPosedness(a.k, std::optional<thermal::CapacityMatrix>{small.c}, a.base.constraints, 0),
                  ValidationCode::SourceMismatch));
}

// ===========================================================================
// The transient setup
// ===========================================================================

TEST_CASE("ThermalValidation_AcceptsValidTransientSetup", "[thermal][validation][transient][setup]") {
    const thermal::UniformInitialTemperature initial{Temperature::fromSi(300.0)};
    const thermal::TransientSettings settings{.timeStep = Time::fromSi(0.5), .endTime = Time::fromSi(10.0), .output = TransientOutputPolicy::EveryStep};
    CHECK(checkTransientSetup(initial, settings, 1000).empty());
    // 0 K is a temperature.
    CHECK(checkTransientSetup(thermal::UniformInitialTemperature{Temperature::fromSi(0.0)}, settings, 1000).empty());
}

TEST_CASE("ThermalValidation_RejectsInvalidInitialTemperature", "[thermal][validation][transient][setup]") {
    const thermal::TransientSettings settings{.timeStep = Time::fromSi(0.5), .endTime = Time::fromSi(10.0), .output = TransientOutputPolicy::FinalOnly};
    for (const double bad : {-1.0, kNotANumber, kInfinite}) {
        const std::vector<ValidationFinding> findings =
            checkTransientSetup(thermal::UniformInitialTemperature{Temperature::fromSi(bad)}, settings, 10);
        REQUIRE(findings.size() == 1);
        CHECK(findings[0].code == ValidationCode::InitialTemperatureInvalid);
        CHECK(findings[0].unit == "K");
    }
}

TEST_CASE("ThermalValidation_RejectsInvalidTimeSettings", "[thermal][validation][transient][setup]") {
    const thermal::UniformInitialTemperature initial{Temperature::fromSi(300.0)};
    const auto with = [&](double dt, double end, TransientOutputPolicy output = TransientOutputPolicy::FinalOnly, std::size_t nodes = 10) {
        return checkTransientSetup(initial, thermal::TransientSettings{.timeStep = Time::fromSi(dt), .endTime = Time::fromSi(end), .output = output}, nodes);
    };
    CHECK(hasCode(with(0.0, 10.0), ValidationCode::TimeSettingsInvalid));       // dt not positive
    CHECK(hasCode(with(-1.0, 10.0), ValidationCode::TimeSettingsInvalid));
    CHECK(hasCode(with(kNotANumber, 10.0), ValidationCode::TimeSettingsInvalid));
    CHECK(hasCode(with(20.0, 10.0), ValidationCode::TimeSettingsInvalid));      // dt longer than the span
    CHECK(hasCode(with(1.0, kInfinite), ValidationCode::TimeSettingsInvalid));
    // A step count past the resource guard is refused, NEVER turned into a larger step.
    const std::vector<ValidationFinding> tooMany = with(1.0e-9, 1.0e3);
    REQUIRE(tooMany.size() == 1);
    CHECK(tooMany[0].code == ValidationCode::TimeSettingsInvalid);
    // A history that would not fit its storage guard.
    const std::vector<ValidationFinding> tooLarge = with(1.0, 1000.0, TransientOutputPolicy::EveryStep, 100000);
    REQUIRE(tooLarge.size() == 1);
    CHECK(tooLarge[0].cause == "history_too_large");
    // FinalOnly stores one state, so the same step count is fine.
    CHECK(with(1.0, 1000.0, TransientOutputPolicy::FinalOnly, 100000).empty());
}
