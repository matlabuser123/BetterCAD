#include "Commands.hpp"
#include "Selectors.hpp"
#include "StructuredOutput.hpp"
#include "ThermalOutput.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/units/UnitCatalog.hpp>
#include <bettercad/features/FaceReferences.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/thermal/ThermalAnalysisObject.hpp>
#include <bettercad/thermal/ThermalAnalysisRun.hpp>
#include <bettercad/thermal/ThermalBalance.hpp>
#include <bettercad/thermal/ThermalData.hpp>
#include <bettercad/thermal/ThermalPost.hpp>
#include <bettercad/thermal/ThermalValidation.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// The thermal reports (P18-CLI-001).
//
// THESE REPORT AND NEVER WRITE, which is P15-CLI-001's rule, P16's and P17's, for the reason that applies here unchanged: a thermal
// result is derived state, and a report that saved one would make the document claim an authority ADR-049 denies it. The file
// that results from "solve" is byte-for-byte the file that went in.
//
// EVERY RESULT COMMAND RUNS THE ANALYSIS, AND SAYS SO. A result is never persisted (P18-PERSIST-001), so a one-shot process has
// nothing to load and the only honest way to answer "what is the hottest node" is to solve now. That is exactly what the mesh
// reports do with a mesh and what the structural reports do with a displacement: in a fresh process `thermal-temperature` solves
// from canonical intent, and there is no "no current result" state for it to report.
//
// NOTHING HERE COMPUTES A THERMAL QUANTITY. No shape function, no conductivity matrix, no capacity matrix, no load integral, no
// Robin term, no `K T = Q`, no backward-Euler step, no `grad(T) = G Te`, no Fourier law, no heat balance, no residual, no
// tolerance and no threshold. Every number printed is read from a qualified object: the temperatures from
// `ThermalPostResult`, the gradient and the flux from the same, the balance from `ThermalBalanceResult`, the acceptance from
// `ThermalValidationReport`. Each report is an adapter, and a grep of this file for a formula finds none.
//
// WHAT IT DOES DO is select and format: the node or element carrying an extremum of an already-qualified array, the norm of a
// stored component triple (the same `hypot` `HeatFlux3::magnitude` is), the JSON field names. Picking the maximum of values the
// core produced is not computing them, and the tie-break is stated (ascending id) rather than left to iteration order.
//
// THE RUN IS THE PRODUCTION ONE. `thermal::runThermalAnalysis` is the only way these commands obtain a result, so every P18-VALID
// refusal -- a stale mesh, a missing material, an unresolved target, conflicting temperatures, a model with no temperature datum,
// a residual or a heat balance out of tolerance -- reaches the exit status unchanged. There is no `--force`, no
// `--ignore-validation` and no second path. SUCCESS IS `run.accepted()`: a solver that returned success is not an input to it.
//
// THE STREAMS. A command that cannot answer prints NOTHING to stdout and a code to stderr (P15-CLI-001), exit 1. Two reports are
// themselves the answer to a question about acceptance -- `thermal-validate` and `thermal-balance` -- so they print their payload
// when the chain got far enough to have one AND exit 1 when the analysis is not accepted, exactly as `structural-validate` does:
// "a report that gates is only usable from a script if the gate reaches the exit status."
namespace bettercad::cli {

namespace {

/// Loads, regenerates, and finds the document's one thermal analysis.
///
/// A query that reached the solver without regenerating would be asking about geometry nobody had built: a document holds feature
/// definitions, not bodies.
struct Prepared {
    Document document;
    features::Regenerator regenerator{};
    ThermalAnalysisId analysis{};
};

/// `<file.bcad> [--json]`, which is the shape of every thermal report.
[[nodiscard]] Result<std::string> onlyPath(Args args, bool* json) {
    auto parsed = parseArguments(args, {{"--json", false}});
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    if (parsed->positional().size() != 1) {
        return makeError(ErrorCode::InvalidArgument, "expected one document");
    }
    *json = parsed->has("--json");
    return std::string{parsed->positional()[0]};
}

void emit(std::ostream& out, bool json, const JsonValue& structured, std::string_view prose) {
    if (json) {
        out << structured.render();
    } else {
        out << prose;
    }
}

[[nodiscard]] std::expected<Prepared, ExitCode> prepare(std::string_view command, const std::string& path,
                                                        std::ostream& err) {
    Result<Document> loaded = io::loadDocument(std::filesystem::path{path});
    if (!loaded) {
        failure(command, "document_not_loaded", loaded.error().message, err);
        return std::unexpected(ExitCode::Failure);
    }
    Prepared prepared{.document = std::move(*loaded)};
    // THE REPORT, NOT ONLY THE RESULT. `regenerate()` returns an error only for a document-level fault; a FEATURE that fails to
    // build is a success carrying `failed`, `blocked` or `cycles`. P16-CLI-001's mutation M6 found that checking the Result alone
    // let a broken model through, and the CLI then reported "no analysis", which is true and is not the reason.
    const Result<features::RegenerationReport> report = prepared.regenerator.regenerate(prepared.document);
    if (!report) {
        failure(command, "regeneration_failed", report.error().message, err);
        return std::unexpected(ExitCode::Failure);
    }
    if (!report->cycles.empty()) {
        failure(command, "dependency_cycle",
                std::format("the model has {} dependency cycle(s) and cannot regenerate", report->cycles.size()), err);
        return std::unexpected(ExitCode::Failure);
    }
    if (!report->failed.empty()) {
        const auto at = report->errors.find(report->failed.front());
        const std::string why = at != report->errors.end() ? at->second.message : std::string{"no diagnostic recorded"};
        failure(command, "regeneration_failed",
                std::format("{} did not regenerate: {}", label(prepared.document, report->failed.front()), why), err);
        return std::unexpected(ExitCode::Failure);
    }
    if (!report->blocked.empty()) {
        failure(command, "regeneration_blocked",
                std::format("{} is blocked by an earlier failure and did not regenerate",
                            label(prepared.document, report->blocked.front())),
                err);
        return std::unexpected(ExitCode::Failure);
    }
    const std::vector<ThermalAnalysisId> analyses = thermal::thermalAnalysisIds(prepared.document);
    if (analyses.empty()) {
        failure(command, "no_thermal_analysis", "this document has no thermal analysis; thermal-analysis-add creates one",
                err);
        return std::unexpected(ExitCode::Failure);
    }
    if (analyses.size() > 1U) {
        // NEVER A FIRST MATCH. Saying so by name beats analysing whichever came first.
        failure(command, "ambiguous_thermal_analysis",
                std::format("this document has {} thermal analyses, and choosing between them needs a selector this version "
                            "does not have",
                            analyses.size()),
                err);
        return std::unexpected(ExitCode::Failure);
    }
    prepared.analysis = analyses.front();
    return prepared;
}

/// One analysis, run through the production entry, with the size of the mesh it was run on.
struct Ran {
    thermal::ThermalAnalysisRun run;
    std::size_t nodeCount = 0;
    /// The TETRAHEDRA, which is what the physics is over -- not the mesh total, which also counts boundary triangles. (P17-CLI-001
    /// printed the mesh total beside a per-tetrahedron range and a 12-tetrahedron block said 24; the lesson is applied here.)
    std::size_t tetrahedronCount = 0;
};

/// The code and the message a refused run is reported under: the validator's first error, in the validator's order, and nothing
/// the CLI chose. If the validator named none (a rule skipped without a finding) the stage that stopped the chain is the code.
struct Refusal {
    std::string code;
    std::string message;
};

[[nodiscard]] Refusal refusalOf(const thermal::ThermalAnalysisRun& run) {
    for (const thermal::ValidationFinding& finding : run.validation.findings) {
        if (finding.severity == thermal::ValidationSeverity::Error) {
            // The CODE is the validator's. The upstream link's own account of why it refused is kept when the finding does not
            // already carry it, so a finding such as `balance_missing` still says WHY the balance could not be formed.
            std::string message = finding.message;
            const std::string_view code = thermal::toString(finding.code);
            if (run.failure.has_value() && code.ends_with("_missing") && message.find(run.failure->message) == std::string::npos) {
                message += std::format(" ({})", run.failure->message);
            }
            return Refusal{std::string{thermal::toString(finding.code)}, std::move(message)};
        }
    }
    if (run.stoppedAt.has_value()) {
        return Refusal{std::format("{}_failed", thermal::toString(*run.stoppedAt)),
                       run.failure.has_value() ? run.failure->message : std::string{"the analysis did not complete"}};
    }
    return Refusal{"not_accepted", "the analysis was not accepted"};
}

/// Meshes (the run never does), then runs. A mesh that cannot be generated is reported as itself, with P16's own message: a
/// missing body, an unresolved sizing reference, a backend failure and the NG_OK-with-no-tetrahedra trap arrive already
/// distinguished.
///
/// @p requireAccepted is true for every command whose payload is a RESULT (it cannot be printed from a refused analysis), and
/// false for the two that report on acceptance.
[[nodiscard]] std::expected<Ran, ExitCode> analyse(std::string_view command, Prepared& prepared,
                                                   meshing::Mesher& mesher, bool requireAccepted,
                                                   std::ostream& err) {
    const thermal::ThermalAnalysis* analysis = thermal::findThermalAnalysis(prepared.document, prepared.analysis);
    if (analysis == nullptr) {
        failure(command, "no_thermal_analysis", "the thermal analysis is gone", err);
        return std::unexpected(ExitCode::Failure);
    }
    const Result<const meshing::VolumeMesh*> mesh =
        mesher.generate(prepared.document, prepared.regenerator, analysis->definition().mesh);
    if (!mesh) {
        failure(command, "mesh_generation_failed", mesh.error().message, err);
        return std::unexpected(ExitCode::Failure);
    }
    Ran ran{.run = thermal::runThermalAnalysis(prepared.document, prepared.regenerator, mesher, prepared.analysis),
            .nodeCount = (*mesh)->mesh().nodeCount(),
            .tetrahedronCount = (*mesh)->mesh().tetrahedra().size()};
    if (requireAccepted && !ran.run.accepted()) {
        const Refusal refusal = refusalOf(ran.run);
        failure(command, refusal.code, refusal.message, err);
        return std::unexpected(ExitCode::Failure);
    }
    return ran;
}

// ---------------------------------------------------------------------------
// Shared JSON shapes
// ---------------------------------------------------------------------------

/// A face reference written the way the command line writes one, so output can be FED BACK IN. The same helper the structural and
/// meshing reports have, for the same reason.
[[nodiscard]] std::string faceText(const Document& document, const FaceName& name) {
    const DocumentObject* object = document.findObject(name.feature);
    const std::string feature = object != nullptr && !object->name().empty() ? object->name()
                                                                              : std::format("{}", name.feature.value());
    std::string text = std::format("face:{}:{}", feature, toString(name.face.role));
    if (name.face.entity) {
        text += std::format(":{}", name.face.entity->value());
    }
    return text;
}


/// The counts every result command repeats.
[[nodiscard]] JsonValue ranJson(const Prepared& prepared, const Ran& ran) {
    const thermal::ThermalAnalysis* analysis = thermal::findThermalAnalysis(prepared.document, prepared.analysis);
    JsonValue json = JsonValue::object();
    json.set("analysisId", idJson(prepared.analysis.value()));
    json.set("name", JsonValue::text(analysis != nullptr ? analysis->name() : std::string{}));
    json.set("mode", JsonValue::text(thermal::toString(ran.run.mode)));
    json.set("nodeCount", count(ran.nodeCount));
    json.set("tetrahedronCount", count(ran.tetrahedronCount));
    return json;
}

// ---------------------------------------------------------------------------
// Deterministic extrema
// ---------------------------------------------------------------------------


/// The norm of a stored component triple -- the same `hypot` `HeatFlux3::magnitude()` is. Presentation: it combines three values
/// the core produced; it recovers nothing.
[[nodiscard]] double normOf(double x, double y, double z) { return std::hypot(x, y, z); }

[[nodiscard]] JsonValue extremumJson(FiniteJson& fin, const char* idKey, const Extremum& e, std::string_view unit) {
    JsonValue json = JsonValue::object();
    json.set(idKey, idJson(e.id));
    json.set("value", fin.number(e.value));
    json.set("unit", JsonValue::text(unit));
    return json;
}

/// The position of the tetrahedron with id @p id in the result's element arrays, found through the result's own stored ids (a binary
/// search): an ElementId is never an index. An id the extremum scan took from those arrays is always found; if it is not, that is a
/// defect and not a result, so it is an exception (the driver reports "internal error") and never element 0.
[[nodiscard]] std::size_t ordinalOrThrow(const thermal::ThermalPostResult& post, std::uint64_t id) {
    const std::optional<std::size_t> ordinal =
        post.ordinalOf(meshing::ElementId::fromValue(static_cast<meshing::ElementId::ValueType>(id)));
    if (!ordinal.has_value()) {
        throw std::logic_error("an element id taken from the result is not in the result");
    }
    return *ordinal;
}

/// The node temperatures of a state as an array of Extrema keyed by the NodeId the NUMBERING says carries each. NEVER `NodeId ==
/// index`: the id comes from `ThermalDofMap::nodeOf`.
[[nodiscard]] std::optional<Extrema> temperatureExtrema(const thermal::ThermalAnalysisRun& run,
                                                        std::span<const Temperature> temperatures) {
    if (!run.dofMap.has_value()) {
        return std::nullopt;
    }
    const thermal::ThermalDofMap& map = *run.dofMap;
    return extremaOf(
        temperatures.size(), [&](std::size_t i) { return temperatures[i].si(); },
        [&](std::size_t i) {
            const Result<meshing::NodeId> node = map.nodeOf(thermal::ThermalDofIndex::fromValue(i));
            if (!node.has_value()) {
                // Every unknown of an accepted run has a node: the numbering is the run's own. This is a defect, not a result.
                throw std::logic_error("the numbering has no node for an unknown of the result");
            }
            return static_cast<std::uint64_t>(node->value());
        });
}

[[nodiscard]] JsonValue temperatureJson(FiniteJson& fin, const std::optional<Extrema>& extrema) {
    JsonValue json = JsonValue::object();
    json.set("unit", JsonValue::text("K"));
    if (!extrema) {
        json.set("count", count(0));
        json.set("min", JsonValue::null());
        json.set("max", JsonValue::null());
        return json;
    }
    json.set("count", count(extrema->count));
    json.set("min", extremumJson(fin, "node", extrema->low, "K"));
    json.set("max", extremumJson(fin, "node", extrema->high, "K"));
    return json;
}

/// A refusal to print a payload that holds a value that is not a number. Only the result reports use this: their values have been
/// through the validator, so reaching it means a path the validator does not judge, and the honest answer is a diagnostic.
[[nodiscard]] ExitCode refuseNonFinite(std::string_view command, std::ostream& err) {
    return failure(command, "nonfinite_value",
                   "a value the report would print is not a finite number, so no payload is written", err);
}

} // namespace

// ---------------------------------------------------------------------------
// thermal-settings
// ---------------------------------------------------------------------------

ExitCode runThermalSettings(Args args, std::ostream& out, std::ostream& err) {
    bool json = false;
    auto path = onlyPath(args, &json);
    if (!path) {
        return usageError("thermal-settings", kThermalSettingsUsage, path.error().message, err);
    }
    auto prepared = prepare("thermal-settings", *path, err);
    if (!prepared) {
        return prepared.error();
    }
    const thermal::ThermalAnalysis* analysis = thermal::findThermalAnalysis(prepared->document, prepared->analysis);
    if (analysis == nullptr) {
        return failure("thermal-settings", "no_thermal_analysis", "the thermal analysis is gone", err);
    }
    const thermal::ThermalAnalysisDefinition& d = analysis->definition();
    FiniteJson fin;

    // CANONICAL INTENT ONLY: no NodeId, no ThermalDofIndex, no K, no Q and no temperature. This command never meshes and never
    // solves, so there is nothing derived for it to print. That a result is not stored is said, not left to be inferred.
    JsonValue structured = JsonValue::object();
    structured.set("analysisId", idJson(prepared->analysis.value()));
    structured.set("name", JsonValue::text(analysis->name()));
    structured.set("mode", JsonValue::text(thermal::toString(d.mode)));
    structured.set("meshControl", idJson(d.mesh.value()));
    structured.set("resultStored", JsonValue::boolean(false));

    JsonValue loads = JsonValue::array();
    for (const thermal::ThermalLoad& load : d.loads) {
        JsonValue entry = JsonValue::object();
        entry.set("loadId", idJson(load.id().value()));
        entry.set("type", JsonValue::text(thermal::toString(load.kind())));
        if (const std::optional<FaceName> target = load.target()) {
            entry.set("face", JsonValue::text(faceText(prepared->document, *target)));
            // UNRESOLVED IS REPORTED, NOT OMITTED. A well-formed reference that no current face satisfies is legitimate state --
            // the user changed the model -- and P18-PERSIST-001 preserves it exactly. Saying so is the whole point of inspection,
            // and no nearest face is substituted.
            entry.set("faceResolves",
                      JsonValue::boolean(features::checkFaceName(prepared->document, *target).has_value()));
        }
        if (const thermal::SurfaceHeatFluxLoad* flux = load.surfaceHeatFlux()) {
            entry.set("flux", fin.quantity(flux->flux.si(), "W/m^2"));
        } else if (const thermal::VolumetricHeatSourceLoad* generation = load.volumetricHeatSource()) {
            entry.set("generation", fin.quantity(generation->generation.si(), "W/m^3"));
        }
        loads.push(std::move(entry));
    }
    structured.set("loads", std::move(loads));

    JsonValue conditions = JsonValue::array();
    for (const thermal::ThermalBoundaryCondition& condition : d.boundaryConditions) {
        JsonValue entry = JsonValue::object();
        entry.set("boundaryConditionId", idJson(condition.id().value()));
        entry.set("type", JsonValue::text(thermal::toString(condition.kind())));
        entry.set("face", JsonValue::text(faceText(prepared->document, condition.face())));
        entry.set("faceResolves",
                  JsonValue::boolean(features::checkFaceName(prepared->document, condition.face()).has_value()));
        if (const thermal::PrescribedTemperature* held = condition.prescribedTemperature()) {
            entry.set("temperature", fin.quantity(held->value.si(), "K"));
        } else if (const thermal::ConvectionBoundary* convection = condition.convection()) {
            entry.set("coefficient", fin.quantity(convection->coefficient.si(), "W/(m^2 K)"));
            entry.set("ambient", fin.quantity(convection->ambient.si(), "K"));
        }
        conditions.push(std::move(entry));
    }
    structured.set("boundaryConditions", std::move(conditions));

    if (d.initialCondition.has_value()) {
        structured.set("initialTemperature", fin.quantity(d.initialCondition->value.si(), "K"));
    } else {
        structured.set("initialTemperature", JsonValue::null());
    }
    JsonValue solver = JsonValue::object();
    solver.set("relativeResidualTolerance", fin.number(d.solver.relativeResidualTolerance));
    solver.set("pivotFloor", fin.number(d.solver.pivotFloor));
    structured.set("solver", std::move(solver));
    if (d.transient.has_value()) {
        JsonValue transient = JsonValue::object();
        transient.set("timeStep", fin.quantity(d.transient->timeStep.si(), "s"));
        transient.set("endTime", fin.quantity(d.transient->endTime.si(), "s"));
        transient.set("output", JsonValue::text(thermal::toString(d.transient->output)));
        structured.set("transient", std::move(transient));
    } else {
        structured.set("transient", JsonValue::null());
    }
    if (fin.sawNonFinite()) {
        return refuseNonFinite("thermal-settings", err);
    }

    std::string prose = std::format("Thermal analysis '{}' (analysis:{})\n", analysis->name(), prepared->analysis.value());
    prose += std::format("  mode:         {}\n", thermal::toString(d.mode));
    prose += std::format("  mesh control: {}\n", d.mesh.value());
    prose += std::format("  solver:       residual <= {}, pivot floor {}\n", d.solver.relativeResidualTolerance,
                         d.solver.pivotFloor);
    if (d.initialCondition.has_value()) {
        prose += std::format("  initial:      {:.6g} K\n", d.initialCondition->value.si());
    }
    if (d.transient.has_value()) {
        prose += std::format("  time:         step {:.6g} s to {:.6g} s, {}\n", d.transient->timeStep.si(),
                             d.transient->endTime.si(), thermal::toString(d.transient->output));
    }
    prose += std::format("  {}\n", plural(d.loads.size(), "load", "loads"));
    for (const thermal::ThermalLoad& load : d.loads) {
        prose += std::format("    {} {}", load.id().value(), thermal::toString(load.kind()));
        if (const std::optional<FaceName> target = load.target()) {
            prose += std::format(" on {}{}", faceText(prepared->document, *target),
                                 features::checkFaceName(prepared->document, *target).has_value() ? "" : " (UNRESOLVED)");
        }
        if (const thermal::SurfaceHeatFluxLoad* flux = load.surfaceHeatFlux()) {
            prose += std::format(", {:.6g} W/m^2", flux->flux.si());
        } else if (const thermal::VolumetricHeatSourceLoad* generation = load.volumetricHeatSource()) {
            prose += std::format(", {:.6g} W/m^3", generation->generation.si());
        }
        prose += '\n';
    }
    prose += std::format("  {}\n", plural(d.boundaryConditions.size(), "boundary condition", "boundary conditions"));
    for (const thermal::ThermalBoundaryCondition& condition : d.boundaryConditions) {
        prose += std::format("    {} {} on {}{}", condition.id().value(), thermal::toString(condition.kind()),
                             faceText(prepared->document, condition.face()),
                             features::checkFaceName(prepared->document, condition.face()).has_value() ? "" : " (UNRESOLVED)");
        if (const thermal::PrescribedTemperature* held = condition.prescribedTemperature()) {
            prose += std::format(", {:.6g} K", held->value.si());
        } else if (const thermal::ConvectionBoundary* convection = condition.convection()) {
            prose += std::format(", {:.6g} W/(m^2 K) to {:.6g} K", convection->coefficient.si(), convection->ambient.si());
        }
        prose += '\n';
    }
    emit(out, json, structured, prose);
    // Reporting an analysis is a successful answer whatever it contains -- including one whose references do not resolve. Whether
    // it can be SOLVED is thermal-validate's question.
    return ExitCode::Success;
}

// ---------------------------------------------------------------------------
// thermal-validate
// ---------------------------------------------------------------------------

ExitCode runThermalValidate(Args args, std::ostream& out, std::ostream& err) {
    bool json = false;
    auto path = onlyPath(args, &json);
    if (!path) {
        return usageError("thermal-validate", kThermalValidateUsage, path.error().message, err);
    }
    auto prepared = prepare("thermal-validate", *path, err);
    if (!prepared) {
        return prepared.error();
    }
    meshing::Mesher mesher;
    // NOT requireAccepted: the report is the answer, and a rejected one is printed. The run still goes through the production
    // entry and nothing else, so the report is the validator's on exactly what the chain built.
    auto ran = analyse("thermal-validate", *prepared, mesher, false, err);
    if (!ran) {
        return ran.error();
    }
    const thermal::ThermalAnalysisRun& run = ran->run;

    FiniteJson fin;
    JsonValue structured = ranJson(*prepared, *ran);
    structured.set("accepted", JsonValue::boolean(run.accepted()));
    structured.set("stoppedAt", run.stoppedAt.has_value() ? JsonValue::text(thermal::toString(*run.stoppedAt))
                                                          : JsonValue::null());
    structured.set("failure", run.failure.has_value() ? JsonValue::text(run.failure->message) : JsonValue::null());
    structured.set("validation", validationJson(fin, run.validation));

    std::string prose = std::format("Validation: {}\n", thermal::toString(run.validation.status()));
    for (const thermal::ThermalCheck& check : run.validation.checks) {
        prose += std::format("  {:<26} {}\n", thermal::toString(check.rule), thermal::toString(run.validation.statusOf(check.rule)));
    }
    for (const thermal::ValidationFinding& finding : run.validation.findings) {
        prose += std::format("  {}: {}: {}\n", thermal::toString(finding.severity), thermal::toString(finding.code),
                             finding.message);
    }
    emit(out, json, structured, prose);
    // THE REPORT IS THE ANSWER AND THE GATE REACHES THE EXIT STATUS. `accepted()` is the production entry's own accessor: a
    // warning-only analysis is accepted and exits 0, and this file never names a ValidationStatus enumerator.
    return run.accepted() ? ExitCode::Success : ExitCode::Failure;
}

// ---------------------------------------------------------------------------
// thermal-solve
// ---------------------------------------------------------------------------

ExitCode runThermalSolve(Args args, std::ostream& out, std::ostream& err) {
    bool json = false;
    auto path = onlyPath(args, &json);
    if (!path) {
        return usageError("thermal-solve", kThermalSolveUsage, path.error().message, err);
    }
    auto prepared = prepare("thermal-solve", *path, err);
    if (!prepared) {
        return prepared.error();
    }
    meshing::Mesher mesher;
    auto ran = analyse("thermal-solve", *prepared, mesher, true, err);
    if (!ran) {
        return ran.error();
    }
    const thermal::ThermalAnalysisRun& run = ran->run;
    FiniteJson fin;

    JsonValue structured = ranJson(*prepared, *ran);
    const std::size_t constrained =
        run.steady.has_value() ? run.steady->constrainedCount() : run.transient->constrainedCount();
    const std::size_t freeEquations =
        run.steady.has_value() ? run.steady->freeEquations() : run.transient->freeEquations();
    structured.set("freeEquations", count(freeEquations));
    structured.set("constrainedCount", count(constrained));

    const thermal::ThermalPostState& finalState = run.post->finalState();
    const std::optional<Extrema> temperatures = temperatureExtrema(run, finalState.temperatures);
    structured.set("temperature", temperatureJson(fin, temperatures));

    std::string prose;
    if (run.steady.has_value()) {
        structured.set("normalizedResidual", fin.number(run.steady->residual().normalized));
        structured.set("pivotRatio", fin.number(run.steady->pivotRatio()));
        prose = std::format("Solved steady: {} nodes, {} tetrahedra, {} free equations\n", ran->nodeCount,
                            ran->tetrahedronCount, freeEquations);
        prose += std::format("  residual:           {:.3e}\n", run.steady->residual().normalized);
    } else {
        const thermal::TransientThermalSolution& transient = *run.transient;
        structured.set("stepCount", count(transient.grid().stepCount()));
        // THE ANALYSIS SPANS THE GRID, WHATEVER WAS STORED. With `final_only` output the only stored state is the last one, so the first
        // STORED time is the end time; the analysis itself starts at the grid's first time. The two are named apart.
        structured.set("storedStateCount", count(transient.sampleCount()));
        structured.set("startTime", fin.quantity(transient.grid().timeAt(0), "s"));
        structured.set("endTime", fin.quantity(transient.grid().endTime(), "s"));
        structured.set("firstStoredTime", fin.quantity(transient.timeOf(0).si(), "s"));
        structured.set("lastStoredTime", fin.quantity(transient.timeOf(transient.sampleCount() - 1U).si(), "s"));
        structured.set("worstPhysicalResidual", fin.number(transient.worstPhysicalResidual()));
        structured.set("worstReducedResidual", fin.number(transient.worstReducedResidual()));
        prose = std::format("Solved transient: {} nodes, {} tetrahedra, {} free equations, {} steps to {:.6g} s\n",
                            ran->nodeCount, ran->tetrahedronCount, freeEquations, transient.grid().stepCount(),
                            transient.grid().endTime());
        prose += std::format("  worst residual:     {:.3e}\n", transient.worstPhysicalResidual());
    }
    JsonValue balance = JsonValue::object();
    balance.set("balanced", JsonValue::boolean(run.balance->balanced()));
    balance.set("worstNormalizedResidual", fin.number(run.balance->worstNormalizedResidual()));
    structured.set("heatBalance", std::move(balance));
    // The validator's report has its OWN writer: a finding about a measured value may legitimately carry one that is not finite
    // (and says so), which must not be mistaken for the summary above being unprintable.
    FiniteJson reportWriter;
    structured.set("validation", validationJson(reportWriter, run.validation));
    if (fin.sawNonFinite()) {
        return refuseNonFinite("thermal-solve", err);
    }

    if (temperatures) {
        prose += std::format("  min T:              {:.6g} K at node {}\n", temperatures->low.value, temperatures->low.id);
        prose += std::format("  max T:              {:.6g} K at node {}\n", temperatures->high.value, temperatures->high.id);
    }
    prose += std::format("  heat balance:       {:.3e} (worst normalized residual)\n",
                         run.balance->worstNormalizedResidual());
    prose += std::format("  validation:         {}\n", thermal::toString(run.validation.status()));
    emit(out, json, structured, prose);
    // THE RESULT IS NOT WRITTEN. It lives for the length of this process, and that is ADR-049's rule rather than an omission.
    return ExitCode::Success;
}

// ---------------------------------------------------------------------------
// thermal-temperature
// ---------------------------------------------------------------------------

ExitCode runThermalTemperature(Args args, std::ostream& out, std::ostream& err) {
    bool json = false;
    auto path = onlyPath(args, &json);
    if (!path) {
        return usageError("thermal-temperature", kThermalTemperatureUsage, path.error().message, err);
    }
    auto prepared = prepare("thermal-temperature", *path, err);
    if (!prepared) {
        return prepared.error();
    }
    meshing::Mesher mesher;
    auto ran = analyse("thermal-temperature", *prepared, mesher, true, err);
    if (!ran) {
        return ran.error();
    }
    const thermal::ThermalAnalysisRun& run = ran->run;
    FiniteJson fin;

    // THE FINAL ACCEPTED STATE, and the payload says which: the only state of a steady analysis, the one at the end time of a
    // transient one. The nodal temperature is the POST result's, which the validator proved equal to the solver's bit for bit.
    const thermal::ThermalPostResult& post = *run.post;
    const std::size_t finalIndex = post.stateCount() - 1U;
    const thermal::ThermalPostState& finalState = post.finalState();
    const std::optional<Extrema> temperatures = temperatureExtrema(run, finalState.temperatures);

    JsonValue structured = ranJson(*prepared, *ran);
    JsonValue state = JsonValue::object();
    state.set("index", count(finalIndex));
    state.set("time", finalState.time.has_value() ? fin.quantity(finalState.time->si(), "s") : JsonValue::null());
    structured.set("state", std::move(state));
    // Merge the range fields flat, so a script reads `count`, `min` and `max` beside `unit`.
    structured.set("temperature", temperatureJson(fin, temperatures));

    // A TRANSIENT HISTORY, bounded by the number of STORED STATES and never by the number of nodes: the range of each, at the time
    // the core stored it. The times are the core's (`ThermalPostState::time`), never `frame * dt`.
    if (run.mode == thermal::ThermalAnalysisMode::Transient) {
        JsonValue history = JsonValue::array();
        for (std::size_t i = 0; i < post.stateCount(); ++i) {
            const thermal::ThermalPostState& s = post.states()[i];
            const std::optional<Extrema> range = temperatureExtrema(run, s.temperatures);
            JsonValue entry = JsonValue::object();
            entry.set("index", count(i));
            entry.set("time", s.time.has_value() ? fin.quantity(s.time->si(), "s") : JsonValue::null());
            entry.set("min", range ? fin.quantity(range->low.value, "K") : JsonValue::null());
            entry.set("max", range ? fin.quantity(range->high.value, "K") : JsonValue::null());
            history.push(std::move(entry));
        }
        structured.set("history", std::move(history));
    }
    if (fin.sawNonFinite()) {
        return refuseNonFinite("thermal-temperature", err);
    }

    std::string prose = std::format("Temperature over {} nodes (state {} of {}{})\n", ran->nodeCount, finalIndex + 1U,
                                    post.stateCount(),
                                    finalState.time.has_value() ? std::format(", t = {:.6g} s", finalState.time->si())
                                                                : std::string{});
    if (temperatures) {
        prose += std::format("  min:  {:.6g} K at node {}\n", temperatures->low.value, temperatures->low.id);
        prose += std::format("  max:  {:.6g} K at node {}\n", temperatures->high.value, temperatures->high.id);
    }
    emit(out, json, structured, prose);
    return ExitCode::Success;
}

// ---------------------------------------------------------------------------
// thermal-gradient
// ---------------------------------------------------------------------------

ExitCode runThermalGradient(Args args, std::ostream& out, std::ostream& err) {
    bool json = false;
    auto path = onlyPath(args, &json);
    if (!path) {
        return usageError("thermal-gradient", kThermalGradientUsage, path.error().message, err);
    }
    auto prepared = prepare("thermal-gradient", *path, err);
    if (!prepared) {
        return prepared.error();
    }
    meshing::Mesher mesher;
    auto ran = analyse("thermal-gradient", *prepared, mesher, true, err);
    if (!ran) {
        return ran.error();
    }
    const thermal::ThermalAnalysisRun& run = ran->run;
    FiniteJson fin;

    const thermal::ThermalPostResult& post = *run.post;
    const std::span<const thermal::TemperatureGradient3> gradients = post.finalState().fields.gradients;
    const std::span<const meshing::ElementId> ids = post.elementIds();
    const auto magnitudeAt = [&](std::size_t i) {
        return normOf(gradients[i].x.si(), gradients[i].y.si(), gradients[i].z.si());
    };
    const std::optional<Extrema> magnitudes = extremaOf(
        gradients.size(), magnitudeAt, [&](std::size_t i) { return static_cast<std::uint64_t>(ids[i].value()); });

    JsonValue structured = ranJson(*prepared, *ran);
    // ONE CONSTANT VALUE PER TETRAHEDRON, and the field says so: a Tet4's gradient is constant (P18-ELEM-001), so there is nothing
    // at a node and nothing is smoothed -- a smoothed field would be a display representation (P18-VIZ-001), not this result.
    structured.set("domain", JsonValue::text("element"));
    structured.set("quantity", JsonValue::text("temperature_gradient"));
    structured.set("unit", JsonValue::text("K/m"));
    structured.set("count", count(gradients.size()));
    if (magnitudes) {
        structured.set("magnitude", [&] {
            JsonValue range = JsonValue::object();
            range.set("min", fin.number(magnitudes->low.value));
            range.set("max", fin.number(magnitudes->high.value));
            range.set("unit", JsonValue::text("K/m"));
            return range;
        }());
        const std::size_t at = ordinalOrThrow(post, magnitudes->high.id);
        JsonValue largest = JsonValue::object();
        largest.set("element", idJson(magnitudes->high.id));
        largest.set("magnitude", fin.quantity(magnitudes->high.value, "K/m"));
        largest.set("gradient", vectorJson(fin, gradients[at].x.si(), gradients[at].y.si(), gradients[at].z.si(), "K/m"));
        structured.set("largest", std::move(largest));
        JsonValue smallest = JsonValue::object();
        smallest.set("element", idJson(magnitudes->low.id));
        smallest.set("magnitude", fin.quantity(magnitudes->low.value, "K/m"));
        structured.set("smallest", std::move(smallest));
    } else {
        structured.set("magnitude", JsonValue::null());
        structured.set("largest", JsonValue::null());
        structured.set("smallest", JsonValue::null());
    }
    if (fin.sawNonFinite()) {
        return refuseNonFinite("thermal-gradient", err);
    }

    std::string prose = std::format("Temperature gradient over {} tetrahedra (one constant value each)\n", gradients.size());
    if (magnitudes) {
        prose += std::format("  max |grad T|:  {:.6g} K/m at element {}\n", magnitudes->high.value, magnitudes->high.id);
        prose += std::format("  min |grad T|:  {:.6g} K/m at element {}\n", magnitudes->low.value, magnitudes->low.id);
    }
    emit(out, json, structured, prose);
    return ExitCode::Success;
}

// ---------------------------------------------------------------------------
// thermal-heat-flux
// ---------------------------------------------------------------------------

ExitCode runThermalHeatFlux(Args args, std::ostream& out, std::ostream& err) {
    bool json = false;
    auto path = onlyPath(args, &json);
    if (!path) {
        return usageError("thermal-heat-flux", kThermalHeatFluxUsage, path.error().message, err);
    }
    auto prepared = prepare("thermal-heat-flux", *path, err);
    if (!prepared) {
        return prepared.error();
    }
    meshing::Mesher mesher;
    auto ran = analyse("thermal-heat-flux", *prepared, mesher, true, err);
    if (!ran) {
        return ran.error();
    }
    const thermal::ThermalAnalysisRun& run = ran->run;
    FiniteJson fin;

    const thermal::ThermalPostResult& post = *run.post;
    const thermal::ThermalElementFields& fields = post.finalState().fields;
    const std::span<const meshing::ElementId> ids = post.elementIds();
    // THE STORED MAGNITUDE, never one recomputed from the components: P18-POST-001 produced `|q|` and the validator checked it.
    const std::optional<Extrema> magnitudes = extremaOf(
        fields.heatFluxMagnitudes.size(), [&](std::size_t i) { return fields.heatFluxMagnitudes[i].si(); },
        [&](std::size_t i) { return static_cast<std::uint64_t>(ids[i].value()); });

    JsonValue structured = ranJson(*prepared, *ran);
    structured.set("domain", JsonValue::text("element"));
    // THE CONDUCTIVE FLUX INSIDE THE SOLID, q = -k grad(T), pointing the way heat FLOWS. It is NOT the applied scalar surface flux
    // of a load (whose positive sign means heat INTO the body through a face), and the field names keep the two apart.
    structured.set("quantity", JsonValue::text("conductive_heat_flux"));
    structured.set("unit", JsonValue::text("W/m^2"));
    structured.set("count", count(fields.heatFluxMagnitudes.size()));
    if (magnitudes) {
        JsonValue range = JsonValue::object();
        range.set("min", fin.number(magnitudes->low.value));
        range.set("max", fin.number(magnitudes->high.value));
        range.set("unit", JsonValue::text("W/m^2"));
        structured.set("magnitude", std::move(range));
        const std::size_t at = ordinalOrThrow(post, magnitudes->high.id);
        JsonValue largest = JsonValue::object();
        largest.set("element", idJson(magnitudes->high.id));
        largest.set("magnitude", fin.quantity(magnitudes->high.value, "W/m^2"));
        largest.set("flux", vectorJson(fin, fields.heatFluxes[at].x.si(), fields.heatFluxes[at].y.si(),
                                       fields.heatFluxes[at].z.si(), "W/m^2"));
        structured.set("largest", std::move(largest));
        JsonValue smallest = JsonValue::object();
        smallest.set("element", idJson(magnitudes->low.id));
        smallest.set("magnitude", fin.quantity(magnitudes->low.value, "W/m^2"));
        structured.set("smallest", std::move(smallest));
    } else {
        structured.set("magnitude", JsonValue::null());
        structured.set("largest", JsonValue::null());
        structured.set("smallest", JsonValue::null());
    }
    if (fin.sawNonFinite()) {
        return refuseNonFinite("thermal-heat-flux", err);
    }

    std::string prose = std::format("Conductive heat flux over {} tetrahedra (one constant value each)\n",
                                    fields.heatFluxMagnitudes.size());
    if (magnitudes) {
        prose += std::format("  max |q|:  {:.6g} W/m^2 at element {}\n", magnitudes->high.value, magnitudes->high.id);
        prose += std::format("  min |q|:  {:.6g} W/m^2 at element {}\n", magnitudes->low.value, magnitudes->low.id);
    }
    emit(out, json, structured, prose);
    return ExitCode::Success;
}

// ---------------------------------------------------------------------------
// thermal-balance
// ---------------------------------------------------------------------------

namespace {

/// The terms of one balance state, as the balance milestone produced them. Every figure is in W (heat INTO the body positive) except
/// where the name says otherwise.
[[nodiscard]] JsonValue stateJson(FiniteJson& fin, const thermal::ThermalHeatBalance& state) {
    JsonValue json = JsonValue::object();
    if (state.step != 0U) {
        json.set("step", count(state.step));
    }
    if (state.time.has_value()) {
        json.set("time", fin.quantity(state.time->si(), "s"));
    }
    if (state.stepSize.has_value()) {
        json.set("stepSize", fin.quantity(state.stepSize->si(), "s"));
    }
    json.set("surfaceHeatInto", fin.quantity(state.terms.appliedSurfaceHeat.si(), "W"));
    json.set("generationInto", fin.quantity(state.terms.generatedHeat.si(), "W"));
    json.set("convectionInto", fin.quantity(state.terms.convectionHeat.si(), "W"));
    json.set("dirichletReactionInto", fin.quantity(state.terms.prescribedTemperatureReaction.si(), "W"));
    if (state.terms.storedEnergyRate.has_value()) {
        json.set("storedEnergyRate", fin.quantity(state.terms.storedEnergyRate->si(), "W"));
    }
    if (state.storedEnergyChange.has_value()) {
        json.set("storedEnergyChange", fin.quantity(state.storedEnergyChange->si(), "J"));
    }
    json.set("residual", fin.quantity(state.residual().si(), "W"));
    json.set("normalizedResidual", fin.number(state.normalizedResidual()));
    json.set("status", JsonValue::text(thermal::toString(state.status)));
    return json;
}

} // namespace

ExitCode runThermalBalance(Args args, std::ostream& out, std::ostream& err) {
    bool json = false;
    auto path = onlyPath(args, &json);
    if (!path) {
        return usageError("thermal-balance", kThermalBalanceUsage, path.error().message, err);
    }
    auto prepared = prepare("thermal-balance", *path, err);
    if (!prepared) {
        return prepared.error();
    }
    meshing::Mesher mesher;
    // NOT requireAccepted: a balance that is out of tolerance is a RESULT (a status, not a failure), and the terms that disagree
    // are exactly what an engineer needs to see. It is printed when the chain reached it, and the exit status still carries the
    // verdict. A chain that stopped before the balance has nothing to print and says why on stderr.
    auto ran = analyse("thermal-balance", *prepared, mesher, false, err);
    if (!ran) {
        return ran.error();
    }
    const thermal::ThermalAnalysisRun& run = ran->run;
    if (!run.balance.has_value()) {
        const Refusal refusal = refusalOf(run);
        return failure("thermal-balance", refusal.code, refusal.message, err);
    }
    const thermal::ThermalBalanceResult& balance = *run.balance;
    FiniteJson fin;

    JsonValue structured = ranJson(*prepared, *ran);
    structured.set("accepted", JsonValue::boolean(run.accepted()));
    structured.set("balanced", JsonValue::boolean(balance.balanced()));
    structured.set("worstNormalizedResidual", fin.number(balance.worstNormalizedResidual()));
    structured.set("stateCount", count(balance.stateCount()));
    // THE FINAL STATE: the only state of a steady balance, the last step of a transient one. Per-step detail follows for a
    // transient analysis, and NO STEP IS OMITTED -- a failing intermediate step is reported where it is.
    const thermal::ThermalHeatBalance& last = balance.finalState();
    structured.set("final", stateJson(fin, last));

    // THE ATTRIBUTION, by canonical id: what each load and each convection condition contributed to the final state. A reaction
    // belongs to an UNKNOWN (two conditions sharing one share its heat, and no split is invented), so it is counted and totalled
    // rather than listed per unknown, which would scale with the mesh.
    JsonValue loads = JsonValue::array();
    for (const thermal::LoadHeat& load : last.surfaceLoads) {
        JsonValue entry = JsonValue::object();
        entry.set("loadId", idJson(load.load.value()));
        entry.set("type", JsonValue::text("surface_heat_flux"));
        entry.set("heatInto", fin.quantity(load.heatInto.si(), "W"));
        loads.push(std::move(entry));
    }
    for (const thermal::LoadHeat& load : last.generationLoads) {
        JsonValue entry = JsonValue::object();
        entry.set("loadId", idJson(load.load.value()));
        entry.set("type", JsonValue::text("volumetric_heat_source"));
        entry.set("heatInto", fin.quantity(load.heatInto.si(), "W"));
        loads.push(std::move(entry));
    }
    structured.set("loads", std::move(loads));
    JsonValue convections = JsonValue::array();
    for (const thermal::ConvectionHeat& convection : last.convections) {
        JsonValue entry = JsonValue::object();
        entry.set("boundaryConditionId", idJson(convection.condition.value()));
        entry.set("area", fin.quantity(convection.area.si(), "m^2"));
        entry.set("heatInto", fin.quantity(convection.heatInto.si(), "W"));
        convections.push(std::move(entry));
    }
    structured.set("convections", std::move(convections));
    structured.set("reactionCount", count(last.reactions.size()));

    if (run.mode == thermal::ThermalAnalysisMode::Transient) {
        JsonValue steps = JsonValue::array();
        for (const thermal::ThermalHeatBalance& step : balance.states()) {
            steps.push(stateJson(fin, step));
        }
        structured.set("steps", std::move(steps));
        if (balance.integrated().has_value()) {
            const thermal::IntegratedEnergy& energy = *balance.integrated();
            JsonValue integrated = JsonValue::object();
            integrated.set("span", fin.quantity(energy.span.si(), "s"));
            integrated.set("external", fin.quantity(energy.external.si(), "J"));
            integrated.set("stored", fin.quantity(energy.stored.si(), "J"));
            integrated.set("endpoints", fin.quantity(energy.endpoints.si(), "J"));
            integrated.set("residual", fin.quantity(energy.residual.si(), "J"));
            integrated.set("allowed", fin.quantity(energy.allowed.si(), "J"));
            structured.set("integrated", std::move(integrated));
        }
    }
    FiniteJson reportWriter;
    structured.set("validation", validationJson(reportWriter, run.validation));
    if (fin.sawNonFinite()) {
        return refuseNonFinite("thermal-balance", err);
    }

    std::string prose = std::format("Heat balance, {} ({}): {}\n", thermal::toString(run.mode),
                                    plural(balance.stateCount(), "state", "states"), balance.balanced() ? "balanced" : "OUT OF TOLERANCE");
    prose += std::format("  surface heat into body:    {:.6g} W\n", last.terms.appliedSurfaceHeat.si());
    prose += std::format("  generation into body:      {:.6g} W\n", last.terms.generatedHeat.si());
    prose += std::format("  convection into body:      {:.6g} W\n", last.terms.convectionHeat.si());
    prose += std::format("  prescribed-T reaction:     {:.6g} W\n", last.terms.prescribedTemperatureReaction.si());
    if (last.terms.storedEnergyRate.has_value()) {
        prose += std::format("  stored energy rate:        {:.6g} W\n", last.terms.storedEnergyRate->si());
    }
    prose += std::format("  residual:                  {:.3e} W (normalized {:.3e})\n", last.residual().si(),
                         last.normalizedResidual());
    emit(out, json, structured, prose);
    // The payload above is the answer; the verdict is the production entry's.
    return run.accepted() ? ExitCode::Success : ExitCode::Failure;
}

} // namespace bettercad::cli
