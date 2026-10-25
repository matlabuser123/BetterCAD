#pragma once

#include "StructuredOutput.hpp"

#include <bettercad/meshing/MeshQuality.hpp>
#include <bettercad/thermal/ThermalValidation.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

// Finite-checked numbers and the shared JSON shapes of the thermal reports (P18-CLI-001).
//
// WHY THIS EXISTS. `JsonValue::number(double)` writes `std::format("{}", value)`, and for NaN and infinity that is `nan` and `inf` --
// tokens that are not JSON. Every earlier structured report got away with it because what they print has already been through a
// validator that refuses a non-finite value. A thermal report is different in one place: the VALIDATION report prints the measured
// value of a finding, and a finding about a residual that is not finite is precisely the case where the measured value is not
// finite. Emitting `nan` there would make the one payload that explains the failure unparseable.
//
// SO A NUMBER IS WRITTEN ONLY IF IT IS FINITE. Otherwise it is written as `null`, which is JSON, and the writer REMEMBERS that it did
// so. A report that is meant to carry only finite engineering values (a temperature range, a heat balance) turns that memory into a
// refusal -- `nonfinite_value`, exit 1, nothing on stdout -- and the validation report, whose findings may legitimately carry one,
// keeps its payload and marks the finding `"valueFinite": false`.
//
// WHY THE SHARED SHAPES LIVE HERE AND NOT IN ThermalReports.cpp. The validation payload and the deterministic extremum are the two
// pieces of presentation with a rule worth testing on its own: a finding whose value is NaN cannot be produced through a real
// document (the core refuses everything that would overflow first), and a tie between two nodes cannot be told from first-wins
// iteration when the numbering is already ascending. Both are therefore tested on hand-built inputs (`ThermalCli_*` unit tests), which
// needs them callable. They are still presentation: they build `JsonValue`s from values the core produced and select among them.
//
// NOT A SECOND SERIALIZER. It builds `JsonValue`s with `JsonValue`'s own constructors; the only thing it decides is whether a double
// is a number. It touches no engineering quantity and holds no threshold.
namespace bettercad::cli {

class FiniteJson {
public:
    /// The number, or `null` if it is NaN or infinite (and the writer remembers).
    [[nodiscard]] JsonValue number(double value) {
        if (!std::isfinite(value)) {
            sawNonFinite_ = true;
            return JsonValue::null();
        }
        return JsonValue::number(value);
    }

    /// `{"value": v, "unit": u}`, with the same rule. Never a bare number.
    [[nodiscard]] JsonValue quantity(double value, std::string_view unit) {
        JsonValue json = JsonValue::object();
        json.set("value", number(value));
        json.set("unit", JsonValue::text(unit));
        return json;
    }

    /// Whether any value written through this writer was not finite.
    [[nodiscard]] bool sawNonFinite() const noexcept { return sawNonFinite_; }

private:
    bool sawNonFinite_ = false;
};
[[nodiscard]] inline JsonValue count(std::size_t value) { return JsonValue::number(value); }

[[nodiscard]] inline JsonValue idJson(std::uint64_t value) { return JsonValue::number(static_cast<std::size_t>(value)); }

[[nodiscard]] inline JsonValue vectorJson(FiniteJson& fin, double x, double y, double z, std::string_view unit) {
    JsonValue json = JsonValue::object();
    json.set("x", fin.number(x));
    json.set("y", fin.number(y));
    json.set("z", fin.number(z));
    json.set("unit", JsonValue::text(unit));
    return json;
}

/// The validator's report, whole: the status, the checks in the validator's order, and every finding. NOTHING IS SORTED OR FILTERED
/// and nothing is reclassified, so a script sees the sequence the application would (the validator's order is part of its contract).
[[nodiscard]] inline JsonValue validationJson(FiniteJson& fin, const thermal::ThermalValidationReport& report) {
    JsonValue json = JsonValue::object();
    json.set("status", JsonValue::text(thermal::toString(report.status())));
    json.set("accepted", JsonValue::boolean(report.accepted()));
    json.set("inputsAccepted", JsonValue::boolean(report.inputsAccepted()));
    json.set("solveAccepted", JsonValue::boolean(report.solveAccepted()));
    json.set("errorCount", count(report.errorCount()));
    json.set("warningCount", count(report.warningCount()));
    json.set("infoCount", count(report.infoCount()));
    json.set("policy", JsonValue::text(report.policyFingerprint));

    JsonValue checks = JsonValue::array();
    for (const thermal::ThermalCheck& check : report.checks) {
        JsonValue entry = JsonValue::object();
        entry.set("rule", JsonValue::text(thermal::toString(check.rule)));
        entry.set("group", JsonValue::text(thermal::toString(thermal::groupOf(check.rule))));
        entry.set("status", JsonValue::text(thermal::toString(report.statusOf(check.rule))));
        if (check.blockedBy.has_value()) {
            entry.set("blockedBy", JsonValue::text(thermal::toString(*check.blockedBy)));
        }
        checks.push(std::move(entry));
    }
    json.set("checks", std::move(checks));

    JsonValue findings = JsonValue::array();
    for (const thermal::ValidationFinding& finding : report.findings) {
        JsonValue entry = JsonValue::object();
        entry.set("code", JsonValue::text(thermal::toString(finding.code)));
        entry.set("rule", JsonValue::text(thermal::toString(finding.rule)));
        entry.set("severity", JsonValue::text(thermal::toString(finding.severity)));
        entry.set("message", JsonValue::text(finding.message));
        if (finding.subject.has_value()) {
            entry.set("subject", JsonValue::text(thermal::toString(*finding.subject)));
        }
        if (finding.metric.has_value()) {
            entry.set("metric", JsonValue::text(meshing::toString(*finding.metric)));
        }
        if (finding.element.has_value()) {
            entry.set("element", idJson(finding.element->value()));
        }
        if (finding.step.has_value()) {
            entry.set("step", count(*finding.step));
        }
        if (finding.time.has_value()) {
            entry.set("time", fin.quantity(*finding.time, "s"));
        }
        if (finding.stepSize.has_value()) {
            entry.set("stepSize", fin.quantity(*finding.stepSize, "s"));
        }
        if (finding.value.has_value()) {
            // A MEASURED value that is not finite is the very thing a finding about a non-finite residual reports, so it is
            // written as null and SAID to be so, rather than as the token `nan`, which would make the payload unparseable.
            entry.set("value", fin.number(*finding.value));
            entry.set("valueFinite", JsonValue::boolean(std::isfinite(*finding.value)));
        }
        if (finding.threshold.has_value()) {
            entry.set("threshold", fin.number(*finding.threshold));
        }
        if (!finding.unit.empty()) {
            entry.set("unit", JsonValue::text(finding.unit));
        }
        if (finding.count.has_value()) {
            entry.set("count", count(*finding.count));
        }
        if (!finding.cause.empty()) {
            entry.set("cause", JsonValue::text(finding.cause));
        }
        findings.push(std::move(entry));
    }
    json.set("findings", std::move(findings));

    const thermal::ThermalValidationMetrics& m = report.metrics;
    JsonValue metrics = JsonValue::object();
    if (m.tetCount) {
        metrics.set("tetrahedronCount", count(*m.tetCount));
    }
    if (m.dofCount) {
        metrics.set("unknownCount", count(*m.dofCount));
    }
    if (m.freeEquations) {
        metrics.set("freeEquations", count(*m.freeEquations));
    }
    if (m.constrainedCount) {
        metrics.set("constrainedCount", count(*m.constrainedCount));
    }
    if (m.steadyResidual) {
        metrics.set("steadyResidual", fin.number(*m.steadyResidual));
    }
    if (m.residualTolerance) {
        metrics.set("residualTolerance", fin.number(*m.residualTolerance));
    }
    if (m.worstResidualStep) {
        metrics.set("worstResidualStep", count(*m.worstResidualStep));
    }
    if (m.worstResidualTime) {
        metrics.set("worstResidualTime", fin.quantity(*m.worstResidualTime, "s"));
    }
    if (m.worstResidualStepSize) {
        metrics.set("worstResidualStepSize", fin.quantity(*m.worstResidualStepSize, "s"));
    }
    if (m.worstTransientResidual) {
        metrics.set("worstTransientResidual", fin.number(*m.worstTransientResidual));
    }
    if (m.transientStepCount) {
        metrics.set("transientStepCount", count(*m.transientStepCount));
    }
    if (m.worstBalanceStep) {
        metrics.set("worstBalanceStep", count(*m.worstBalanceStep));
    }
    if (m.worstBalanceTime) {
        metrics.set("worstBalanceTime", fin.quantity(*m.worstBalanceTime, "s"));
    }
    if (m.worstBalanceResidual) {
        metrics.set("worstBalanceResidual", fin.number(*m.worstBalanceResidual));
    }
    if (m.minimumTemperature) {
        metrics.set("minimumTemperature", fin.quantity(*m.minimumTemperature, "K"));
    }
    if (m.maximumTemperature) {
        metrics.set("maximumTemperature", fin.quantity(*m.maximumTemperature, "K"));
    }
    if (m.maximumHeatFlux) {
        metrics.set("maximumHeatFlux", fin.quantity(*m.maximumHeatFlux, "W/m^2"));
    }
    json.set("metrics", std::move(metrics));
    return json;
}

// ---------------------------------------------------------------------------
// Deterministic extrema
// ---------------------------------------------------------------------------

/// The extremum of an array of already-qualified values, with the id that carries it.
struct Extremum {
    double value{};
    std::uint64_t id = 0;
};

struct Extrema {
    Extremum low;
    Extremum high;
    std::size_t count = 0;
};

/// Seeded from the FIRST element and never from zero: an accumulator compared against 0.0 reports 0.0 when every value is
/// negative, which is how an extrema report comes to claim a field reaches a value it never does. THE TIE-BREAK IS THE ASCENDING
/// ID, stated here rather than left to iteration order, so two runs on one mesh name the same node or element.
template <typename ValueAt, typename IdAt>
[[nodiscard]] inline std::optional<Extrema> extremaOf(std::size_t n, ValueAt valueAt, IdAt idAt) {
    if (n == 0U) {
        return std::nullopt;
    }
    Extrema e;
    e.count = n;
    e.low = Extremum{valueAt(0), idAt(0)};
    e.high = e.low;
    for (std::size_t i = 1; i < n; ++i) {
        const Extremum candidate{valueAt(i), idAt(i)};
        if (candidate.value < e.low.value || (candidate.value == e.low.value && candidate.id < e.low.id)) {
            e.low = candidate;
        }
        if (candidate.value > e.high.value || (candidate.value == e.high.value && candidate.id < e.high.id)) {
            e.high = candidate;
        }
    }
    return e;
}

} // namespace bettercad::cli
