#pragma once

// Shared fixtures for the P18-VALID-001 tests.
//
// THE ORACLES ARE INDEPENDENT BY CONSTRUCTION. A residual that must be refused is made by perturbing a field the
// test itself solved or typed in; a balance that must be refused is made by editing the plain terms of a balance the test
// obtained from the qualified algebraic seam; a mesh that must be refused is a hand-built tetrahedron with the corners the
// test chose. Nothing here asks the validator what it should say, and nothing weakens a production type to make an invalid
// state constructible: the typed results are immutable, so corruption is shown to the validator as the plain data they hold
// (the seams in ThermalValidation.hpp).

#include "ThermalBalanceTestSupport.hpp"

#include <bettercad/thermal/ThermalValidation.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vltest {

using namespace batest;
using thermal::CheckState;
using thermal::CheckStatus;
using thermal::ThermalCheck;
using thermal::ThermalValidationReport;
using thermal::ValidationCode;
using thermal::ValidationFinding;
using thermal::ValidationRule;
using thermal::ValidationSeverity;
using thermal::ValidationStatus;
using thermal::ValidationSubject;

/// The findings of @p rule, in report order.
[[nodiscard]] inline std::vector<ValidationFinding> findingsOf(const ThermalValidationReport& report, ValidationRule rule) {
    std::vector<ValidationFinding> out;
    for (const ValidationFinding& finding : report.findings) {
        if (finding.rule == rule) {
            out.push_back(finding);
        }
    }
    return out;
}

[[nodiscard]] inline bool hasCode(const std::vector<ValidationFinding>& findings, ValidationCode code) {
    for (const ValidationFinding& finding : findings) {
        if (finding.code == code) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline bool hasCode(const ThermalValidationReport& report, ValidationCode code) { return hasCode(report.findings, code); }

[[nodiscard]] inline std::optional<ValidationFinding> firstWithCode(const std::vector<ValidationFinding>& findings, ValidationCode code) {
    for (const ValidationFinding& finding : findings) {
        if (finding.code == code) {
            return finding;
        }
    }
    return std::nullopt;
}

[[nodiscard]] inline std::size_t errorsIn(const std::vector<ValidationFinding>& findings) {
    std::size_t n = 0;
    for (const ValidationFinding& finding : findings) {
        n += finding.severity == ValidationSeverity::Error ? 1 : 0;
    }
    return n;
}

/// A report with every rule evaluated (the transient setup not applicable) and no finding: the all-pass baseline a test edits.
[[nodiscard]] inline ThermalValidationReport allPassingReport(bool transient = false) {
    ThermalValidationReport report;
    report.mode = transient ? thermal::ThermalAnalysisMode::Transient : thermal::ThermalAnalysisMode::Steady;
    for (const ValidationRule rule : thermal::kValidationRules) {
        const bool notApplicable = !transient && rule == ValidationRule::TransientSetup;
        report.checks.push_back(ThermalCheck{.rule = rule,
                                             .state = notApplicable ? CheckState::NotApplicable : CheckState::Evaluated,
                                             .blockedBy = std::nullopt});
    }
    return report;
}

[[nodiscard]] inline ValidationFinding findingOf(ValidationCode code, std::string message = "test") {
    ValidationFinding finding;
    finding.code = code;
    finding.severity = thermal::severityOf(code);
    finding.rule = thermal::ruleOf(code);
    finding.message = std::move(message);
    return finding;
}

/// A deterministic digest of everything about a report that must not move between runs and presets: the overall status, every
/// rule's status and every finding's classification (rule, code, severity, subject, step, count). NOT the measured numbers,
/// which are within a qualified tolerance across presets and are not classification.
[[nodiscard]] inline std::uint64_t classificationDigest(const ThermalValidationReport& report) {
    std::uint64_t hash = 14695981039346656037ULL;
    const auto mix = [&hash](std::uint64_t value) {
        for (int i = 0; i < 8; ++i) {
            hash ^= (value >> (8 * i)) & 0xFFU;
            hash *= 1099511628211ULL;
        }
    };
    mix(static_cast<std::uint64_t>(report.status()));
    mix(report.checks.size());
    for (const ThermalCheck& check : report.checks) {
        mix(static_cast<std::uint64_t>(check.rule));
        mix(static_cast<std::uint64_t>(report.statusOf(check.rule)));
        mix(check.blockedBy.has_value() ? static_cast<std::uint64_t>(*check.blockedBy) + 1 : 0);
    }
    mix(report.findings.size());
    for (const ValidationFinding& finding : report.findings) {
        mix(static_cast<std::uint64_t>(finding.rule));
        mix(static_cast<std::uint64_t>(finding.code));
        mix(static_cast<std::uint64_t>(finding.severity));
        mix(finding.subject.has_value() ? static_cast<std::uint64_t>(*finding.subject) + 1 : 0);
        mix(finding.step.value_or(0));
        mix(finding.count.value_or(0));
    }
    return hash;
}

/// A hand-built mesh of one tetrahedron from four corners in the given order (the caller chooses the orientation).
using Corners = std::array<std::array<double, 3>, 4>;

[[nodiscard]] inline AsmHand oneTetOf(const Corners& corners) {
    Nodes nodes;
    for (std::size_t i = 0; i < 4; ++i) {
        nodes.emplace_back(static_cast<NodeId::ValueType>(i + 1), at(corners[i][0], corners[i][1], corners[i][2]));
    }
    return makeAsmHand(nodes, {}, {{0, 1, 2, 3}});
}

/// P16's flattening wedge: a 10 mm equilateral base with the apex at @p apexMillimetres.
[[nodiscard]] inline Corners wedgeCorners(double apexMillimetres) {
    const double a = 0.010;
    const double h = a * std::sqrt(3.0) / 2.0;
    return {{{0.0, 0.0, 0.0}, {a, 0.0, 0.0}, {a / 2.0, h, 0.0}, {a / 2.0, h / 3.0, apexMillimetres / 1000.0}}};
}

/// A needle: an equilateral base of @p baseMillimetres, the apex 10 mm away.
[[nodiscard]] inline Corners needleCorners(double baseMillimetres) {
    const double b = baseMillimetres / 1000.0;
    const double h = b * std::sqrt(3.0) / 2.0;
    return {{{0.0, 0.0, 0.0}, {b, 0.0, 0.0}, {b / 2.0, h, 0.0}, {b / 2.0, h / 3.0, 0.010}}};
}

[[nodiscard]] inline meshing::MeshQualityReport qualityOf(const Corners& corners) {
    const AsmHand hand = oneTetOf(corners);
    return meshing::evaluateMeshQuality(hand.mesh, meshing::reportOnlyThresholds());
}

/// A quality report that holds exactly one summary, for the boundary tests: the worst @p metric is @p value.
[[nodiscard]] inline meshing::MeshQualityReport reportWithOneSummary(meshing::QualityMetric metric, double value) {
    meshing::MeshQualityReport report;
    report.structurallyValid = true;
    report.tetCount = 1;
    report.validElements = 1;
    report.summaries[metric] = meshing::MetricSummary{.count = 1, .minimum = value, .maximum = value, .mean = value, .worst = ElementId::fromValue(7)};
    return report;
}

[[nodiscard]] inline std::vector<std::span<const Temperature>> spansOf(const std::vector<std::vector<Temperature>>& states) {
    std::vector<std::span<const Temperature>> out;
    for (const std::vector<Temperature>& state : states) {
        out.emplace_back(state);
    }
    return out;
}

} // namespace vltest
