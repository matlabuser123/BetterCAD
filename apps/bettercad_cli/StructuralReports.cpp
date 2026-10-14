#include "Commands.hpp"
#include "Selectors.hpp"
#include "StructuredOutput.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/units/UnitCatalog.hpp>
#include <bettercad/features/FaceReferences.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/structural/StructuralAnalysisObject.hpp>
#include <bettercad/structural/StructuralLoad.hpp>
#include <bettercad/structural/StructuralPost.hpp>
#include <bettercad/structural/StructuralReaction.hpp>
#include <bettercad/structural/StructuralRestraint.hpp>
#include <bettercad/structural/StructuralValidation.hpp>

#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

// The structural reports (P17-CLI-001).
//
// THESE REPORT AND NEVER WRITE, which is P15-CLI-001's rule and P16-CLI-001's
// for one more reason that applies here unchanged: a structural result is
// derived state, and a report that saved one would make the document claim an
// authority P17-PERSIST-001 denies it (ADR-048).
//
// EVERY RESULT COMMAND SOLVES, AND SAYS SO. A result is never persisted, so a
// one-shot process has nothing to load and the only honest way to answer "what
// is the maximum von Mises" is to solve now. That is exactly what the four
// mesh reports do with a mesh -- "Mesh the document and summarise it" -- and
// the alternative, persisting a result so these commands look stateful across
// processes, is the thing the derived-state rule forbids. So in a fresh process
// `structural-stress` solves from canonical intent, and there is no
// "no current result" state for it to report.
//
// NOTHING HERE COMPUTES A STRUCTURAL QUANTITY. No shape function, no B matrix,
// no D matrix, no von Mises formula, no eigenvalue, no `K u - F`, no
// `cross(r, F)`, no restraint count, no residual formula and no threshold. The
// displacements come from `RecoveredFields::displacements()`, the element
// fields from `elements()`, the reactions from `SupportReactions`, the
// acceptance from `StructuralValidationReport::acceptable()`, the equilibrium
// from `ForceBalance` and `MomentBalance`. Each report is an adapter, and a
// grep of this file for a formula finds none.
//
// WHAT IT DOES DO is select and format: the id of the largest value, the
// extrema of a component, the JSON field names. Picking the maximum from values
// the core produced is not computing them, and the tie-break is stated rather
// than left to iteration order.
//
// THE SOLVE ENTRY IS THE PRODUCTION ONE. `solveStructuralAnalysis` is the only
// way these commands obtain a result, so every P17-VALID refusal -- stale
// geometry, a stale mesh, a missing material, an unresolved target, too few
// constrained degrees of freedom, a mesh outside the accuracy floor, a residual
// or equilibrium failure -- reaches the exit status unchanged. There is no
// `--force`, no `--ignore-validation` and no second path.
namespace bettercad::cli {

namespace {

/// Loads, regenerates, and finds the document's one structural analysis.
///
/// A query that reached the solver without regenerating would be asking about
/// geometry nobody had built: a document holds feature definitions, not bodies.
struct Prepared {
    Document document;
    features::Regenerator regenerator{};
    AnalysisId analysis{};
};

/// `<file.bcad> [--json]`, which is the shape of every structural report.
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

[[nodiscard]] std::expected<Prepared, ExitCode> prepare(std::string_view command,
                                                        const std::string& path,
                                                        std::ostream& err) {
    Result<Document> loaded = io::loadDocument(std::filesystem::path{path});
    if (!loaded) {
        failure(command, "document_not_loaded", loaded.error().message, err);
        return std::unexpected(ExitCode::Failure);
    }
    Prepared prepared{.document = std::move(*loaded)};
    // THE REPORT, NOT ONLY THE RESULT. `regenerate()` returns an error only
    // for a document-level fault; a FEATURE that fails to build is a success
    // carrying `failed`, `blocked` or `cycles`. P16-CLI-001's mutation M6
    // found that checking the Result alone let a broken model through, and the
    // CLI then reported "no analysis", which is true and is not the reason.
    const Result<features::RegenerationReport> report =
        prepared.regenerator.regenerate(prepared.document);
    if (!report) {
        failure(command, "regeneration_failed", report.error().message, err);
        return std::unexpected(ExitCode::Failure);
    }
    if (!report->cycles.empty()) {
        failure(command, "dependency_cycle",
                std::format("the model has {} dependency cycle(s) and cannot regenerate",
                            report->cycles.size()),
                err);
        return std::unexpected(ExitCode::Failure);
    }
    if (!report->failed.empty()) {
        const auto at = report->errors.find(report->failed.front());
        const std::string why = at != report->errors.end() ? at->second.message
                                                           : std::string{"no diagnostic recorded"};
        failure(command, "regeneration_failed",
                std::format("{} did not regenerate: {}",
                            label(prepared.document, report->failed.front()), why),
                err);
        return std::unexpected(ExitCode::Failure);
    }
    if (!report->blocked.empty()) {
        failure(command, "regeneration_blocked",
                std::format("{} is blocked by an earlier failure and did not regenerate",
                            label(prepared.document, report->blocked.front())),
                err);
        return std::unexpected(ExitCode::Failure);
    }
    const std::vector<AnalysisId> analyses = structural::structuralAnalysisIds(prepared.document);
    if (analyses.empty()) {
        failure(command, "no_structural_analysis",
                "this document has no structural analysis; structural-analysis-add creates one",
                err);
        return std::unexpected(ExitCode::Failure);
    }
    if (analyses.size() > 1U) {
        // NEVER A FIRST MATCH (the brief's §103). Saying so by name beats
        // analysing whichever came first.
        failure(command, "ambiguous_structural_analysis",
                std::format("this document has {} structural analyses, and choosing between them "
                            "needs a selector this version does not have",
                            analyses.size()),
                err);
        return std::unexpected(ExitCode::Failure);
    }
    prepared.analysis = analyses.front();
    return prepared;
}

/// A validated result, through the production entry and nothing else.
///
/// WRITTEN ONCE AND SHARED BY FOUR COMMANDS (the brief's §197), so solve and
/// the three summaries cannot drift about what "a result" means or about which
/// refusals apply.
///
/// THE MESH IS GENERATED HERE because `solveStructuralAnalysis` does not: its
/// own header states it "never calls `Mesher::generate`", so a solve that
/// silently remeshed would hide both the cost and the intent change behind it.
/// The CLI therefore asks P16 for the mesh explicitly, through the same
/// `Mesher::generate` every other consumer uses, and then asks P17 to solve.
struct Solved {
    structural::StructuralSolveOutcome outcome;
    std::size_t nodeCount = 0;
    /// The MESH total: surface triangles plus tetrahedra, which is what
    /// `Mesh::elementCount()` means and what mesh-info reports.
    std::size_t elementCount = 0;
    /// The TETRAHEDRA, which is what the physics is over.
    ///
    /// BOTH, NAMED SEPARATELY, because conflating them was a real defect:
    /// the stress summary first reported `elementCount` beside a per-element
    /// stress range, and a 12-tetrahedron block reported 24 -- the triangles
    /// counted too. Found by the core-equivalence test, which compared the
    /// printed count against `RecoveredFields::elements().size()`.
    std::size_t tetrahedronCount = 0;
};

[[nodiscard]] std::expected<Solved, ExitCode> solve(std::string_view command, Prepared& prepared,
                                                    meshing::Mesher& mesher,
                                                    std::ostream& err) {
    const structural::StructuralAnalysis* analysis =
        structural::findStructuralAnalysis(prepared.document, prepared.analysis);
    if (analysis == nullptr) {
        failure(command, "no_structural_analysis", "the structural analysis is gone", err);
        return std::unexpected(ExitCode::Failure);
    }
    const Result<const meshing::VolumeMesh*> mesh =
        mesher.generate(prepared.document, prepared.regenerator, analysis->definition().mesh);
    if (!mesh) {
        // P16's diagnostic verbatim: a missing body, an unresolved sizing
        // reference, a backend failure and the NG_OK-with-no-tetrahedra trap
        // all arrive here already distinguished.
        failure(command, "mesh_generation_failed", mesh.error().message, err);
        return std::unexpected(ExitCode::Failure);
    }

    // THE VALIDATION REPORT FIRST, so a refusal is reported as the staged
    // diagnostic P17-VALID produced rather than as a flat solver error -- and
    // so the SOLVER IS NOT INVOKED at all for a refused model, which is the
    // brief's §144 and §145. `solveStructuralAnalysis` runs the same gate
    // itself; this does not replace it, it reads the report the gate produces.
    const structural::StructuralValidationReport report =
        structural::validateStructuralAnalysisForSolve(prepared.document, prepared.regenerator,
                                                       mesher, prepared.analysis);
    if (!report.acceptable()) {
        // `acceptable()` is the core's own accessor for "the solve may
        // proceed", so the CLI does not decide what a warning means.
        const structural::ValidationFinding* first = nullptr;
        for (const structural::ValidationFinding& finding : report.findings) {
            if (finding.severity == structural::ValidationSeverity::Failure) {
                first = &finding;
                break;
            }
        }
        failure(command,
                first != nullptr ? toString(first->code) : std::string_view{"not_ready"},
                first != nullptr ? first->message : std::string{"the analysis is not ready"}, err);
        return std::unexpected(ExitCode::Failure);
    }

    Result<structural::StructuralSolveOutcome> outcome = structural::solveStructuralAnalysis(
        prepared.document, prepared.regenerator, mesher, prepared.analysis);
    if (!outcome) {
        // A refusal the staged report did not predict -- a singular
        // factorisation, a residual or equilibrium failure. The core's message
        // verbatim; flattening these to "solve failed" would discard what
        // P17-SOLVE-001 and P17-REACTION-001 worked out.
        failure(command, "solve_failed", outcome.error().message, err);
        return std::unexpected(ExitCode::Failure);
    }
    return Solved{.outcome = std::move(*outcome),
                  .nodeCount = (*mesh)->mesh().nodeCount(),
                  .elementCount = (*mesh)->mesh().elementCount(),
                  .tetrahedronCount = (*mesh)->mesh().tetrahedra().size()};
}

// ---------------------------------------------------------------------------
// Shared JSON shapes
// ---------------------------------------------------------------------------

/// A face reference written the way the command line writes one, so output can
/// be FED BACK IN. The same helper MeshingReports.cpp has, for the same reason.
[[nodiscard]] std::string faceText(const Document& document, const FaceName& name) {
    const DocumentObject* object = document.findObject(name.feature);
    const std::string feature = object != nullptr && !object->name().empty()
                                    ? object->name()
                                    : std::format("{}", name.feature.value());
    std::string text = std::format("face:{}:{}", feature, toString(name.face.role));
    if (name.face.entity) {
        text += std::format(":{}", name.face.entity->value());
    }
    return text;
}

[[nodiscard]] JsonValue vectorJson(double x, double y, double z, std::string_view unit) {
    JsonValue json = JsonValue::object();
    json.set("x", JsonValue::number(x));
    json.set("y", JsonValue::number(y));
    json.set("z", JsonValue::number(z));
    json.set("unit", JsonValue::text(unit));
    return json;
}

[[nodiscard]] JsonValue validationJson(const structural::StructuralValidationReport& report) {
    JsonValue json = JsonValue::object();
    json.set("status", JsonValue::text(toString(report.status())));
    json.set("acceptable", JsonValue::boolean(report.acceptable()));
    json.set("reachedStage", JsonValue::text(toString(report.reachedStage)));
    JsonValue findings = JsonValue::array();
    // THE CORE'S ORDER, which is the staged order P17-VALID-001 made part of
    // its contract. Nothing here sorts or filters, so a script sees the same
    // sequence the GUI would (the brief's §142).
    for (const structural::ValidationFinding& finding : report.findings) {
        JsonValue entry = JsonValue::object();
        entry.set("code", JsonValue::text(toString(finding.code)));
        entry.set("severity", JsonValue::text(toString(finding.severity)));
        entry.set("message", JsonValue::text(finding.message));
        if (finding.element) {
            entry.set("element", JsonValue::number(
                                     static_cast<std::size_t>(finding.element->value())));
        }
        if (finding.metric) {
            entry.set("metric", JsonValue::text(toString(*finding.metric)));
        }
        if (finding.value) {
            entry.set("value", JsonValue::number(*finding.value));
        }
        if (finding.threshold) {
            entry.set("threshold", JsonValue::number(*finding.threshold));
        }
        findings.push(std::move(entry));
    }
    json.set("findings", std::move(findings));
    return json;
}

/// The counts and the acceptance scalars every result command repeats.
[[nodiscard]] JsonValue solvedJson(const Solved& solved) {
    const structural::StructuralSolveOutcome& o = solved.outcome;
    JsonValue json = JsonValue::object();
    json.set("nodeCount", JsonValue::number(solved.nodeCount));
    json.set("elementCount", JsonValue::number(solved.elementCount));
    json.set("tetrahedronCount", JsonValue::number(solved.tetrahedronCount));
    json.set("freeEquations", JsonValue::number(o.freeEquations));
    json.set("normalizedResidual", JsonValue::number(o.residual.normalized));
    json.set("pivotRatio", JsonValue::number(o.pivotRatio));
    return json;
}

// ---------------------------------------------------------------------------
// Deterministic extrema
// ---------------------------------------------------------------------------

/// The node carrying the largest displacement magnitude.
///
/// SELECTION, NOT COMPUTATION: the magnitudes are
/// `NodalDisplacement::magnitude`, which P17-POST-001 produced. The tie-break
/// is the ASCENDING NodeId, stated here rather than left to iteration order,
/// so two runs on one mesh name the same node (the brief's §85).
[[nodiscard]] const structural::NodalDisplacement* largestDisplacement(
    std::span<const structural::NodalDisplacement> displacements) {
    const structural::NodalDisplacement* best = nullptr;
    for (const structural::NodalDisplacement& d : displacements) {
        if (best == nullptr || d.magnitude.si() > best->magnitude.si() ||
            (d.magnitude.si() == best->magnitude.si() && d.node.value() < best->node.value())) {
            best = &d;
        }
    }
    return best;
}

/// The element carrying the largest von Mises stress, tie-broken by ascending
/// ElementId (the brief's §86).
[[nodiscard]] const structural::ElementFields* largestVonMises(
    std::span<const structural::ElementFields> elements) {
    const structural::ElementFields* best = nullptr;
    for (const structural::ElementFields& e : elements) {
        if (best == nullptr || e.vonMises.si() > best->vonMises.si() ||
            (e.vonMises.si() == best->vonMises.si() &&
             e.element.value() < best->element.value())) {
            best = &e;
        }
    }
    return best;
}

/// The smallest and largest of one scalar over a span, as a pair.
///
/// Seeded from the FIRST element and never from zero: an accumulator compared
/// against 0.0 reports 0.0 when every value is negative, which is how an
/// extrema report comes to claim a compressive-only field reaches zero.
template <typename T, typename Project>
[[nodiscard]] std::optional<std::pair<double, double>> range(std::span<const T> items,
                                                             Project project) {
    if (items.empty()) {
        return std::nullopt;
    }
    double low = project(items.front());
    double high = low;
    for (const T& item : items) {
        const double value = project(item);
        low = value < low ? value : low;
        high = value > high ? value : high;
    }
    return std::pair{low, high};
}

[[nodiscard]] JsonValue rangeJson(const std::optional<std::pair<double, double>>& bounds,
                                  std::string_view unit) {
    JsonValue json = JsonValue::object();
    if (!bounds) {
        json.set("min", JsonValue::null());
        json.set("max", JsonValue::null());
    } else {
        json.set("min", JsonValue::number(bounds->first));
        json.set("max", JsonValue::number(bounds->second));
    }
    json.set("unit", JsonValue::text(unit));
    return json;
}

} // namespace

// ---------------------------------------------------------------------------
// structural-settings
// ---------------------------------------------------------------------------

ExitCode runStructuralSettings(Args args, std::ostream& out, std::ostream& err) {
    bool json = false;
    auto path = onlyPath(args, &json);
    if (!path) {
        return usageError("structural-settings", kStructuralSettingsUsage, path.error().message,
                          err);
    }
    auto prepared = prepare("structural-settings", *path, err);
    if (!prepared) {
        return prepared.error();
    }
    const structural::StructuralAnalysis* analysis =
        structural::findStructuralAnalysis(prepared->document, prepared->analysis);
    if (analysis == nullptr) {
        return failure("structural-settings", "no_structural_analysis",
                       "the structural analysis is gone", err);
    }
    const structural::StructuralAnalysisDefinition& d = analysis->definition();

    // CANONICAL INTENT ONLY, which is the brief's §4: no NodeId, no DofIndex,
    // no K and no F. This command never solves and never meshes, so there is
    // nothing derived for it to print.
    JsonValue structured = JsonValue::object();
    structured.set("analysisId", JsonValue::number(
                                     static_cast<std::size_t>(prepared->analysis.value())));
    structured.set("name", JsonValue::text(analysis->name()));
    structured.set("mode", JsonValue::text(toString(d.mode)));
    structured.set("meshControl",
                   JsonValue::number(static_cast<std::size_t>(d.mesh.value())));

    JsonValue loads = JsonValue::array();
    for (const structural::StructuralLoad& load : d.loads) {
        JsonValue entry = JsonValue::object();
        entry.set("loadId", JsonValue::number(static_cast<std::size_t>(load.id().value())));
        entry.set("type", JsonValue::text(toString(load.kind())));
        if (const std::optional<FaceName> target = load.target()) {
            entry.set("face", JsonValue::text(faceText(prepared->document, *target)));
            // UNRESOLVED IS REPORTED, NOT OMITTED (the brief's §5). A
            // well-formed reference that no current face satisfies is
            // legitimate state -- the user changed the model -- and
            // P17-PERSIST-001 preserves it exactly. Saying so is the whole
            // point of inspection.
            entry.set("faceResolves",
                      JsonValue::boolean(features::checkFaceName(prepared->document, *target).has_value()));
        }
        if (const structural::PressureLoad* pressure = load.pressure()) {
            entry.set("magnitude", JsonValue::quantity(pressure->magnitude.si(), "Pa"));
        } else if (const structural::SurfaceTractionLoad* traction = load.traction()) {
            entry.set("traction", vectorJson(traction->traction.x.si(), traction->traction.y.si(),
                                             traction->traction.z.si(), "Pa"));
        } else if (const structural::GravityLoad* gravity = load.gravity()) {
            entry.set("acceleration", vectorJson(gravity->acceleration.x, gravity->acceleration.y,
                                                 gravity->acceleration.z, "m/s^2"));
        }
        loads.push(std::move(entry));
    }
    structured.set("loads", std::move(loads));

    JsonValue restraints = JsonValue::array();
    for (const structural::StructuralRestraint& restraint : d.restraints) {
        JsonValue entry = JsonValue::object();
        entry.set("restraintId",
                  JsonValue::number(static_cast<std::size_t>(restraint.id().value())));
        entry.set("face", JsonValue::text(faceText(prepared->document, restraint.face())));
        entry.set("faceResolves",
                  JsonValue::boolean(features::checkFaceName(prepared->document, restraint.face()).has_value()));
        entry.set("components", JsonValue::text(toString(restraint.components())));
        restraints.push(std::move(entry));
    }
    structured.set("restraints", std::move(restraints));

    JsonValue solver = JsonValue::object();
    solver.set("algorithm", JsonValue::text(toString(d.solver.algorithm)));
    solver.set("relativeResidualTolerance",
               JsonValue::number(d.solver.relativeResidualTolerance));
    solver.set("pivotFloor", JsonValue::number(d.solver.pivotFloor));
    structured.set("solver", std::move(solver));

    std::string prose = std::format("Structural analysis '{}' (analysis:{})\n", analysis->name(),
                                    prepared->analysis.value());
    prose += std::format("  mode:         {}\n", toString(d.mode));
    prose += std::format("  mesh control: {}\n", d.mesh.value());
    prose += std::format("  solver:       {}, residual <= {}, pivot floor {}\n",
                         toString(d.solver.algorithm), d.solver.relativeResidualTolerance,
                         d.solver.pivotFloor);
    prose += std::format("  {}\n", plural(d.loads.size(), "load", "loads"));
    for (const structural::StructuralLoad& load : d.loads) {
        prose += std::format("    {} {}", load.id().value(), toString(load.kind()));
        if (const std::optional<FaceName> target = load.target()) {
            prose += std::format(" on {}{}", faceText(prepared->document, *target),
                                 features::checkFaceName(prepared->document, *target).has_value() ? ""
                                                                            : " (UNRESOLVED)");
        }
        if (const structural::PressureLoad* pressure = load.pressure()) {
            prose += std::format(", {:.6g} Pa", pressure->magnitude.si());
        } else if (const structural::SurfaceTractionLoad* traction = load.traction()) {
            prose += std::format(", ({:.6g}, {:.6g}, {:.6g}) Pa", traction->traction.x.si(),
                                 traction->traction.y.si(), traction->traction.z.si());
        } else if (const structural::GravityLoad* gravity = load.gravity()) {
            prose += std::format(", ({:.6g}, {:.6g}, {:.6g}) m/s^2", gravity->acceleration.x,
                                 gravity->acceleration.y, gravity->acceleration.z);
        }
        prose += '\n';
    }
    prose += std::format("  {}\n", plural(d.restraints.size(), "restraint", "restraints"));
    for (const structural::StructuralRestraint& restraint : d.restraints) {
        prose += std::format("    {} {} holding {}{}\n", restraint.id().value(),
                             faceText(prepared->document, restraint.face()),
                             toString(restraint.components()),
                             features::checkFaceName(prepared->document, restraint.face()).has_value()
                                 ? ""
                                 : " (UNRESOLVED)");
    }
    emit(out, json, structured, prose);
    // Reporting an analysis is a successful answer whatever it contains --
    // including one whose references do not resolve. Whether it can be SOLVED
    // is structural-validate's question.
    return ExitCode::Success;
}

// ---------------------------------------------------------------------------
// structural-validate
// ---------------------------------------------------------------------------

ExitCode runStructuralValidate(Args args, std::ostream& out, std::ostream& err) {
    bool json = false;
    auto path = onlyPath(args, &json);
    if (!path) {
        return usageError("structural-validate", kStructuralValidateUsage, path.error().message,
                          err);
    }
    auto prepared = prepare("structural-validate", *path, err);
    if (!prepared) {
        return prepared.error();
    }
    const structural::StructuralAnalysis* analysis =
        structural::findStructuralAnalysis(prepared->document, prepared->analysis);
    if (analysis == nullptr) {
        return failure("structural-validate", "no_structural_analysis",
                       "the structural analysis is gone", err);
    }

    // THE MESH IS GENERATED, because a validation that skipped it could not
    // reach the mesh-quality, restraint or load stages at all -- it would stop
    // at "no mesh" and report nothing about the model. A generation failure is
    // reported as itself.
    meshing::Mesher mesher;
    if (const Result<const meshing::VolumeMesh*> mesh = mesher.generate(
            prepared->document, prepared->regenerator, analysis->definition().mesh);
        !mesh) {
        return failure("structural-validate", "mesh_generation_failed", mesh.error().message, err);
    }

    // AND NO SOLVE. This is the pre-solve question, so nothing here
    // factorises; a `validate` that hid a solve would be the brief's §47
    // failure.
    const structural::StructuralValidationReport report =
        structural::validateStructuralAnalysisForSolve(prepared->document, prepared->regenerator,
                                                       mesher, prepared->analysis);

    JsonValue structured = validationJson(report);
    std::string prose = std::format("Validation: {}\n", toString(report.status()));
    prose += std::format("  reached stage: {}\n", toString(report.reachedStage));
    for (const structural::ValidationFinding& finding : report.findings) {
        prose += std::format("  {}: {}: {}\n", toString(finding.severity), toString(finding.code),
                             finding.message);
    }
    emit(out, json, structured, prose);
    // THE REPORT IS THE ANSWER AND THE GATE REACHES THE EXIT STATUS, which is
    // `mesh-validate`'s precedent: "a report that gates is only usable from a
    // script if the gate reaches the exit status." A warning-only analysis is
    // acceptable and exits 0, which is `acceptable()`'s own distinction and
    // not one this file makes.
    return report.acceptable() ? ExitCode::Success : ExitCode::Failure;
}

// ---------------------------------------------------------------------------
// structural-solve
// ---------------------------------------------------------------------------

ExitCode runStructuralSolve(Args args, std::ostream& out, std::ostream& err) {
    bool json = false;
    auto path = onlyPath(args, &json);
    if (!path) {
        return usageError("structural-solve", kStructuralSolveUsage, path.error().message, err);
    }
    auto prepared = prepare("structural-solve", *path, err);
    if (!prepared) {
        return prepared.error();
    }
    meshing::Mesher mesher;
    auto solved = solve("structural-solve", *prepared, mesher, err);
    if (!solved) {
        return solved.error();
    }
    const structural::StructuralSolveOutcome& o = solved->outcome;

    JsonValue structured = solvedJson(*solved);
    structured.set("strainEnergy", JsonValue::quantity(o.strainEnergy.si(), "J"));
    structured.set("largestDisplacement",
                   JsonValue::quantity(o.largestDisplacement.si(), "m"));
    structured.set("largestVonMises", JsonValue::quantity(o.largestVonMises.si(), "Pa"));
    structured.set("forceImbalance",
                   JsonValue::quantity(o.forceBalance.euclideanNorm.si(), "N"));
    structured.set("momentImbalance",
                   JsonValue::quantity(o.momentBalance.euclideanNorm.si(), "N m"));
    structured.set("validation", validationJson(o.validation));

    // A SUMMARY AT ANY SIZE. No displacement array, no stress array and no
    // reaction array: the three summary commands exist for those, and a solve
    // that printed every node would be unusable on a real model (the brief's
    // §24 and §240).
    const std::string prose = std::format(
        "Solved: {} nodes, {} tetrahedra, {} free equations\n"
        "  strain energy:      {:.6g} J\n"
        "  max |u|:            {:.6g} m\n"
        "  max von Mises:      {:.6g} Pa\n"
        "  residual:           {:.3e}\n"
        "  force imbalance:    {:.3e} N\n"
        "  moment imbalance:   {:.3e} N m\n"
        "  validation:         {}\n",
        solved->nodeCount, solved->tetrahedronCount, o.freeEquations, o.strainEnergy.si(),
        o.largestDisplacement.si(), o.largestVonMises.si(), o.residual.normalized,
        o.forceBalance.euclideanNorm.si(), o.momentBalance.euclideanNorm.si(),
        toString(o.validation.status()));
    emit(out, json, structured, prose);
    // THE RESULT IS NOT WRITTEN. It lives for the length of this process, and
    // that is ADR-048's rule rather than an omission.
    return ExitCode::Success;
}

// ---------------------------------------------------------------------------
// structural-displacement
// ---------------------------------------------------------------------------

ExitCode runStructuralDisplacement(Args args, std::ostream& out, std::ostream& err) {
    bool json = false;
    auto path = onlyPath(args, &json);
    if (!path) {
        return usageError("structural-displacement", kStructuralDisplacementUsage,
                          path.error().message, err);
    }
    auto prepared = prepare("structural-displacement", *path, err);
    if (!prepared) {
        return prepared.error();
    }
    meshing::Mesher mesher;
    auto solved = solve("structural-displacement", *prepared, mesher, err);
    if (!solved) {
        return solved.error();
    }
    const std::span<const structural::NodalDisplacement> displacements =
        solved->outcome.fields.displacements();
    const structural::NodalDisplacement* worst = largestDisplacement(displacements);

    JsonValue structured = solvedJson(*solved);
    structured.set("unit", JsonValue::text("m"));
    structured.set("magnitude",
                   rangeJson(range(displacements,
                                   [](const structural::NodalDisplacement& d) {
                                       return d.magnitude.si();
                                   }),
                             "m"));
    structured.set("ux", rangeJson(range(displacements,
                                         [](const structural::NodalDisplacement& d) {
                                             return d.displacement.x.si();
                                         }),
                                   "m"));
    structured.set("uy", rangeJson(range(displacements,
                                         [](const structural::NodalDisplacement& d) {
                                             return d.displacement.y.si();
                                         }),
                                   "m"));
    structured.set("uz", rangeJson(range(displacements,
                                         [](const structural::NodalDisplacement& d) {
                                             return d.displacement.z.si();
                                         }),
                                   "m"));
    if (worst != nullptr) {
        JsonValue at = JsonValue::object();
        // A NodeId FOR INSPECTION, and that is all it is. It names a node of
        // the mesh that was generated in THIS process; it is not canonical
        // intent and cannot be fed to structural-load-add, which accepts only
        // a CAD face (the brief's §38).
        at.set("node", JsonValue::number(static_cast<std::size_t>(worst->node.value())));
        at.set("magnitude", JsonValue::quantity(worst->magnitude.si(), "m"));
        at.set("displacement", vectorJson(worst->displacement.x.si(), worst->displacement.y.si(),
                                          worst->displacement.z.si(), "m"));
        structured.set("largest", std::move(at));
    } else {
        structured.set("largest", JsonValue::null());
    }

    std::string prose = std::format("Displacement over {} nodes\n", displacements.size());
    if (worst != nullptr) {
        prose += std::format("  max |u|:  {:.6g} m at node {}\n", worst->magnitude.si(),
                             worst->node.value());
        prose += std::format("  there:    ({:.6g}, {:.6g}, {:.6g}) m\n",
                             worst->displacement.x.si(), worst->displacement.y.si(),
                             worst->displacement.z.si());
    }
    emit(out, json, structured, prose);
    return ExitCode::Success;
}

// ---------------------------------------------------------------------------
// structural-stress
// ---------------------------------------------------------------------------

ExitCode runStructuralStress(Args args, std::ostream& out, std::ostream& err) {
    bool json = false;
    auto path = onlyPath(args, &json);
    if (!path) {
        return usageError("structural-stress", kStructuralStressUsage, path.error().message, err);
    }
    auto prepared = prepare("structural-stress", *path, err);
    if (!prepared) {
        return prepared.error();
    }
    meshing::Mesher mesher;
    auto solved = solve("structural-stress", *prepared, mesher, err);
    if (!solved) {
        return solved.error();
    }
    const std::span<const structural::ElementFields> elements = solved->outcome.fields.elements();
    const structural::ElementFields* worst = largestVonMises(elements);

    JsonValue structured = solvedJson(*solved);
    // ONE CONSTANT STATE PER TETRAHEDRON, and the field name says so. A Tet4's
    // strain is constant over the element (P17-ELEM-001), so there is no nodal
    // stress here and nothing is averaged to the nodes -- which the brief's
    // §41 forbids and which would invent a smoothing policy the core does not
    // have.
    structured.set("domain", JsonValue::text("element"));
    structured.set("unit", JsonValue::text("Pa"));
    structured.set("vonMises",
                   rangeJson(range(elements,
                                   [](const structural::ElementFields& e) {
                                       return e.vonMises.si();
                                   }),
                             "Pa"));
    const auto componentRange = [&elements](auto project) {
        return rangeJson(range(elements, project), "Pa");
    };
    JsonValue components = JsonValue::object();
    components.set("xx", componentRange([](const structural::ElementFields& e) {
        return e.stress.xx.si();
    }));
    components.set("yy", componentRange([](const structural::ElementFields& e) {
        return e.stress.yy.si();
    }));
    components.set("zz", componentRange([](const structural::ElementFields& e) {
        return e.stress.zz.si();
    }));
    components.set("xy", componentRange([](const structural::ElementFields& e) {
        return e.stress.xy.si();
    }));
    components.set("yz", componentRange([](const structural::ElementFields& e) {
        return e.stress.yz.si();
    }));
    components.set("zx", componentRange([](const structural::ElementFields& e) {
        return e.stress.zx.si();
    }));
    structured.set("components", std::move(components));
    JsonValue principal = JsonValue::object();
    principal.set("sigma1", componentRange([](const structural::ElementFields& e) {
        return e.principalStress.sigma1.si();
    }));
    principal.set("sigma3", componentRange([](const structural::ElementFields& e) {
        return e.principalStress.sigma3.si();
    }));
    structured.set("principal", std::move(principal));
    if (worst != nullptr) {
        JsonValue at = JsonValue::object();
        at.set("element", JsonValue::number(static_cast<std::size_t>(worst->element.value())));
        at.set("vonMises", JsonValue::quantity(worst->vonMises.si(), "Pa"));
        at.set("sigma1", JsonValue::quantity(worst->principalStress.sigma1.si(), "Pa"));
        at.set("sigma3", JsonValue::quantity(worst->principalStress.sigma3.si(), "Pa"));
        structured.set("largest", std::move(at));
    } else {
        structured.set("largest", JsonValue::null());
    }

    std::string prose = std::format("Stress over {} tetrahedra (one constant state each)\n",
                                    elements.size());
    if (worst != nullptr) {
        prose += std::format("  max von Mises:  {:.6g} Pa at element {}\n", worst->vonMises.si(),
                             worst->element.value());
        prose += std::format("  there, s1/s3:   {:.6g} / {:.6g} Pa\n",
                             worst->principalStress.sigma1.si(),
                             worst->principalStress.sigma3.si());
    }
    emit(out, json, structured, prose);
    return ExitCode::Success;
}

// ---------------------------------------------------------------------------
// structural-reactions
// ---------------------------------------------------------------------------

ExitCode runStructuralReactions(Args args, std::ostream& out, std::ostream& err) {
    bool json = false;
    auto path = onlyPath(args, &json);
    if (!path) {
        return usageError("structural-reactions", kStructuralReactionsUsage,
                          path.error().message, err);
    }
    auto prepared = prepare("structural-reactions", *path, err);
    if (!prepared) {
        return prepared.error();
    }
    meshing::Mesher mesher;
    auto solved = solve("structural-reactions", *prepared, mesher, err);
    if (!solved) {
        return solved.error();
    }
    const structural::SupportReactions& reactions = solved->outcome.reactions;
    const structural::ForceBalance& force = reactions.forceBalance();
    const structural::MomentBalance& moment = reactions.momentBalance();

    JsonValue structured = solvedJson(*solved);
    // THE CORE'S SIGN CONVENTION, UNTOUCHED (the brief's §43). A reaction is
    // the support force acting on the structure; nothing here negates one for
    // presentation.
    structured.set("totalForce", vectorJson(reactions.totalForce().x.si(),
                                            reactions.totalForce().y.si(),
                                            reactions.totalForce().z.si(), "N"));
    structured.set("totalMoment", vectorJson(moment.reaction.x.si(), moment.reaction.y.si(),
                                             moment.reaction.z.si(), "N m"));
    structured.set("externalForce",
                   vectorJson(force.external.x.si(), force.external.y.si(),
                              force.external.z.si(), "N"));
    structured.set("forceImbalance", JsonValue::quantity(force.euclideanNorm.si(), "N"));
    structured.set("momentImbalance", JsonValue::quantity(moment.euclideanNorm.si(), "N m"));
    structured.set("momentOrigin", vectorJson(moment.origin.x.si(), moment.origin.y.si(),
                                              moment.origin.z.si(), "m"));
    structured.set("constrainedNodes",
                   JsonValue::number(solved->outcome.result.reactions().size()));

    // PER-RESTRAINT, AND HONEST ABOUT WHAT IT IS. P17-REACTION-001 counts a
    // shared constrained degree of freedom ONCE, in `sharedForce`, rather than
    // attributing it to either restraint -- so the per-restraint entries plus
    // the shared part equal the total, and the owned parts alone do not. The
    // shared term is written out for exactly that reason (the brief's §45).
    JsonValue perRestraint = JsonValue::array();
    for (const structural::RestraintReaction& entry : reactions.restraints()) {
        JsonValue item = JsonValue::object();
        item.set("restraintId",
                 JsonValue::number(static_cast<std::size_t>(entry.restraint.value())));
        item.set("ownedForce", vectorJson(entry.ownedForce.x.si(), entry.ownedForce.y.si(),
                                          entry.ownedForce.z.si(), "N"));
        item.set("ownedMoment", vectorJson(entry.ownedMoment.x.si(), entry.ownedMoment.y.si(),
                                           entry.ownedMoment.z.si(), "N m"));
        perRestraint.push(std::move(item));
    }
    structured.set("restraints", std::move(perRestraint));
    structured.set("sharedForce",
                   vectorJson(reactions.sharedForce().x.si(), reactions.sharedForce().y.si(),
                              reactions.sharedForce().z.si(), "N"));
    structured.set("sharedMoment",
                   vectorJson(reactions.sharedMoment().x.si(), reactions.sharedMoment().y.si(),
                              reactions.sharedMoment().z.si(), "N m"));

    std::string prose = std::format(
        "Reactions over {} constrained nodes, {}\n"
        "  total force:      ({:.6g}, {:.6g}, {:.6g}) N\n"
        "  total moment:     ({:.6g}, {:.6g}, {:.6g}) N m\n"
        "  force imbalance:  {:.3e} N\n"
        "  moment imbalance: {:.3e} N m\n",
        solved->outcome.result.reactions().size(),
        plural(reactions.restraints().size(), "restraint", "restraints"),
        reactions.totalForce().x.si(), reactions.totalForce().y.si(),
        reactions.totalForce().z.si(), moment.reaction.x.si(), moment.reaction.y.si(),
        moment.reaction.z.si(), force.euclideanNorm.si(), moment.euclideanNorm.si());
    for (const structural::RestraintReaction& entry : reactions.restraints()) {
        prose += std::format("    restraint {} owns ({:.6g}, {:.6g}, {:.6g}) N\n",
                             entry.restraint.value(), entry.ownedForce.x.si(),
                             entry.ownedForce.y.si(), entry.ownedForce.z.si());
    }
    prose += std::format("    shared        ({:.6g}, {:.6g}, {:.6g}) N\n",
                         reactions.sharedForce().x.si(), reactions.sharedForce().y.si(),
                         reactions.sharedForce().z.si());
    emit(out, json, structured, prose);
    return ExitCode::Success;
}

} // namespace bettercad::cli
