#include "Commands.hpp"
#include "Selectors.hpp"
#include "StructuredOutput.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/units/UnitCatalog.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/meshing/MeshQuality.hpp>
#include <bettercad/meshing/MeshValidation.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/meshing/MeshingCommands.hpp>

#include <filesystem>
#include <format>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

// The meshing reports (P16-CLI-001).
//
// THESE REPORT AND NEVER WRITE, which is P15-CLI-001's recorded rule for the
// same reason: everything they print is either canonical state the document
// already holds or state derived from it, and a report that saved its
// derivation would be caching an answer the next load has to recompute.
//
// For meshing that rule is also P16-PERSIST-001's authority boundary spoken
// headlessly. `mesh-generate` builds a mesh, prints what it is, and exits; the
// mesh is NOT written to the document, because a generated mesh is derived and
// the file holds intent. So in a fresh process `mesh-info` says there is no
// mesh until something asks for one -- which is correct and is tested, not
// worked around.
//
// NOTHING HERE COMPUTES A MESH QUANTITY. No tetrahedron volume, no aspect
// ratio, no dihedral angle, no nearest-face search. The volume comes from
// VolumeMesh::tetrahedralVolume, the quality from MeshQualityReport, the
// structure from meshing::validate, the boundary mapping from
// meshing::resolveBoundarySet. Each report is an adapter, and a grep of this
// file for a formula finds none.
namespace bettercad::cli {

namespace {

/// Loads, and regenerates so that geometry exists to mesh or to resolve
/// against.
///
/// A document holds feature definitions, not bodies; a report that skipped
/// this would be asking about geometry nobody had built.
struct Loaded {
    Document document;
    features::Regenerator regenerator;
};

/// `<file.bcad> [--json]`, which is the shape of every mesh report but one.
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

/// Writes the JSON or the prose, whichever was asked for.
void emit(std::ostream& out, bool json, const JsonValue& structured, std::string_view prose) {
    if (json) {
        out << structured.render();
    } else {
        out << prose;
    }
}

[[nodiscard]] std::string currencyName(meshing::MeshCurrency currency) {
    switch (currency) {
    case meshing::MeshCurrency::NoMesh:
        return "none";
    case meshing::MeshCurrency::Current:
        return "current";
    case meshing::MeshCurrency::StaleIntent:
        return "staleIntent";
    case meshing::MeshCurrency::StaleGeometry:
        return "staleGeometry";
    case meshing::MeshCurrency::GenerationFailed:
        return "generationFailed";
    }
    return "unknown";
}

/// A face reference, written the way the command line writes one, so output
/// can be fed back in.
/// A face reference written the way the command line writes one, so output
/// can be FED BACK IN.
///
/// The feature's own name, not label()'s "Name (object:7)" -- which reads
/// well and is not a selector. `resolveObject` takes a name or an ID, so the
/// name round-trips; an object with no name falls back to the ID, which also
/// round-trips.
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

[[nodiscard]] JsonValue faceJson(const Document& document, const FaceName& name) {
    JsonValue json = JsonValue::object();
    json.set("feature", JsonValue::number(static_cast<std::size_t>(name.feature.value())));
    json.set("role", JsonValue::text(toString(name.face.role)));
    if (name.face.entity) {
        json.set("entity",
                 JsonValue::number(static_cast<std::size_t>(name.face.entity->value())));
    }
    json.set("reference", JsonValue::text(faceText(document, name)));
    return json;
}

/// Loads, regenerates, and finds the document's one meshing control.
///
/// A query that reached the mesher without regenerating would be asking about
/// geometry nobody had built: a document holds feature definitions, not
/// bodies.
struct Prepared {
    Document document;
    features::Regenerator regenerator{};
    MeshControlId control{};
};

[[nodiscard]] std::expected<Prepared, ExitCode> prepare(std::string_view command,
                                                        const std::string& path, std::ostream& err) {
    Result<Document> loaded = io::loadDocument(std::filesystem::path{path});
    if (!loaded) {
        failure(command, "document_not_loaded", loaded.error().message, err);
        return std::unexpected(ExitCode::Failure);
    }
    Prepared prepared{.document = std::move(*loaded)};
    // The core's own refusal, carried through. A failed or blocked
    // regeneration is a meshing precondition (P16-GEOM-001), and the CLI must
    // not reach past it to a stale body.
    // THE REPORT, NOT ONLY THE RESULT -- and that distinction was a defect.
    //
    // `regenerate()` returns an error only for a document-level fault. A
    // FEATURE that fails to build is a success carrying `failed`, `blocked` or
    // `cycles` in its report, so checking the Result alone let a broken model
    // through: the CLI then found no body and said "no meshing control", which
    // is true and is not the reason. Found by mutation M6, which removed this
    // check and survived because no fixture had a model that failed.
    //
    // Each kind keeps its own code rather than collapsing into one, because a
    // script branching on "the model did not build" needs to know whether a
    // feature failed, a feature was blocked by an earlier one, or the
    // dependency graph has a cycle.
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
        // The core's own message for the first failure, named by object, so
        // the diagnostic says WHICH feature and WHY rather than that meshing
        // did not happen.
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
    const std::vector<MeshControlId> controls = meshing::meshControls(prepared.document);
    if (controls.empty()) {
        failure(command, "no_mesh_control",
                "this document has no meshing control; mesh-control-add creates one", err);
        return std::unexpected(ExitCode::Failure);
    }
    if (controls.size() > 1U) {
        failure(command, "ambiguous_mesh_control",
                std::format("this document has {} meshing controls, and choosing between them "
                            "needs a selector this version does not have",
                            controls.size()),
                err);
        return std::unexpected(ExitCode::Failure);
    }
    prepared.control = controls.front();
    return prepared;
}

/// Generates the mesh a query needs, from the intent just loaded.
///
/// EVERY QUERY GENERATES, AND SAYS SO. A generated mesh is never persisted
/// (P16-PERSIST-001), so a one-shot process has nothing to load and the only
/// honest way to answer "how many elements" is to build it now. What would be
/// wrong is persisting a mesh to make these commands look stateful across
/// processes, which is the thing the derived-state rule forbids.
[[nodiscard]] std::expected<const meshing::VolumeMesh*, ExitCode> generate(
    std::string_view command, Prepared& prepared, meshing::Mesher& mesher, std::ostream& err) {
    const Result<const meshing::VolumeMesh*> mesh =
        mesher.generate(prepared.document, prepared.regenerator, prepared.control);
    if (!mesh) {
        // The core's diagnostic verbatim: a missing body, an unresolved sizing
        // reference, a backend failure and the NG_OK-with-no-tetrahedra trap
        // all arrive here already distinguished, and flattening them to
        // "meshing failed" would throw away what P16-VOL-001 worked out.
        failure(command, "mesh_generation_failed", mesh.error().message, err);
        return std::unexpected(ExitCode::Failure);
    }
    return *mesh;
}

/// The counts, the volume and the state, from the core's own accessors.
[[nodiscard]] JsonValue meshJson(const meshing::Mesher& mesher, const Document& document,
                                 MeshControlId control, const meshing::VolumeMesh& mesh) {
    JsonValue json = JsonValue::object();
    json.set("state", JsonValue::text(currencyName(mesher.currency(document, control))));
    // Generated by THIS process from the intent in the file. Stated so that a
    // reader cannot think a mesh was loaded.
    json.set("meshSource", JsonValue::text("generatedNow"));
    json.set("nodeCount", JsonValue::number(mesh.mesh().nodeCount()));
    json.set("elementCount", JsonValue::number(mesh.mesh().tetrahedra().size()));
    json.set("boundaryTriangleCount", JsonValue::number(mesh.mesh().triangles().size()));
    JsonValue types = JsonValue::array();
    if (!mesh.mesh().tetrahedra().empty()) {
        types.push(JsonValue::text("Tet4"));
    }
    if (!mesh.mesh().triangles().empty()) {
        types.push(JsonValue::text("Triangle3"));
    }
    json.set("elementTypes", std::move(types));
    json.set("meshVolume", JsonValue::quantity(mesh.tetrahedralVolume().si(), "m^3"));
    return json;
}

} // namespace

ExitCode runMeshSettings(Args args, std::ostream& out, std::ostream& err) {
    bool json = false;
    auto path = onlyPath(args, &json);
    if (!path) {
        return usageError("mesh-settings", kMeshSettingsUsage, path.error().message, err);
    }
    auto prepared = prepare("mesh-settings", *path, err);
    if (!prepared) {
        return prepared.error();
    }
    const meshing::MeshControl* control =
        meshing::findMeshControl(prepared->document, prepared->control);
    const meshing::MeshControlDefinition& d = control->definition();

    JsonValue structured = JsonValue::object();
    structured.set("control", JsonValue::text(control->name()));
    structured.set("body", JsonValue::text(label(prepared->document, d.body)));
    if (d.mesh.sizing.globalTargetSize) {
        structured.set("globalTargetSize",
                       JsonValue::quantity(d.mesh.sizing.globalTargetSize->si(), "m"));
    } else {
        // ABSENT IS A STATE, not a number: it means BetterCAD's scale-relative
        // default, and printing a number for it would invent one.
        structured.set("globalTargetSize", JsonValue::null());
    }
    structured.set("linearDeflection",
                   JsonValue::quantity(d.mesh.surface.linearDeflection.si(), "m"));
    structured.set("angularDeflection",
                   JsonValue::quantity(d.mesh.surface.angularDeflection.si(), "rad"));

    std::string prose = std::format(
        "Meshing control: {}\nBody: {}\nGlobal target size: {}\n", control->name(),
        label(prepared->document, d.body),
        d.mesh.sizing.globalTargetSize
            ? std::format("{:.4g} mm", d.mesh.sizing.globalTargetSize->in(units::mm))
            : std::string{"BetterCAD default"});

    // THROUGH THE ORDERED ACCESSORS, so the listing is deterministic and does
    // not depend on the order the controls were added (P16-SIZE-001: the
    // stored order carries no meaning).
    JsonValue locals = JsonValue::array();
    const std::vector<meshing::LocalMeshSizing> ordered = control->orderedLocalSizing();
    prose += std::format("Local sizing ({}):\n", ordered.size());
    for (const meshing::LocalMeshSizing& local : ordered) {
        JsonValue entry = JsonValue::object();
        entry.set("face", faceJson(prepared->document, local.face));
        entry.set("targetSize", JsonValue::quantity(local.targetSize.si(), "m"));
        locals.push(std::move(entry));
        prose += std::format("  {}  {:.4g} mm\n", faceText(prepared->document, local.face),
                             local.targetSize.in(units::mm));
    }
    structured.set("localSizing", std::move(locals));

    JsonValue sets = JsonValue::array();
    const std::vector<meshing::NamedBoundarySet> orderedSets = control->orderedBoundarySets();
    prose += std::format("Boundary sets ({}):\n", orderedSets.size());
    for (const meshing::NamedBoundarySet& set : orderedSets) {
        JsonValue entry = JsonValue::object();
        entry.set("id", JsonValue::number(static_cast<std::size_t>(set.id.value())));
        entry.set("name", JsonValue::text(set.name));
        JsonValue faces = JsonValue::array();
        for (const FaceName& face : set.faces) {
            faces.push(faceJson(prepared->document, face));
        }
        entry.set("faces", std::move(faces));
        sets.push(std::move(entry));
        prose += std::format("  {}  {}  {}\n", set.id.value(), set.name,
                             plural(set.faces.size(), "face", "faces"));
    }
    structured.set("boundarySets", std::move(sets));

    JsonValue limits = JsonValue::array();
    for (const auto& [metric, threshold] : d.quality.limits) {
        JsonValue entry = JsonValue::object();
        entry.set("metric", JsonValue::text(toString(metric)));
        entry.set("warning",
                  threshold.warning ? JsonValue::number(*threshold.warning) : JsonValue::null());
        entry.set("failure",
                  threshold.failure ? JsonValue::number(*threshold.failure) : JsonValue::null());
        limits.push(std::move(entry));
    }
    structured.set("qualityThresholds", std::move(limits));
    prose += std::format("Quality thresholds: {}\n", d.quality.limits.size());

    emit(out, json, structured, prose);
    return ExitCode::Success;
}

ExitCode runMeshGenerate(Args args, std::ostream& out, std::ostream& err) {
    bool json = false;
    auto path = onlyPath(args, &json);
    if (!path) {
        return usageError("mesh-generate", kMeshGenerateUsage, path.error().message, err);
    }
    auto prepared = prepare("mesh-generate", *path, err);
    if (!prepared) {
        return prepared.error();
    }
    meshing::Mesher mesher;
    auto mesh = generate("mesh-generate", *prepared, mesher, err);
    if (!mesh) {
        return mesh.error();
    }

    const JsonValue structured = meshJson(mesher, prepared->document, prepared->control, **mesh);
    const std::string prose = std::format(
        "Mesh: generated (not persisted -- the document holds intent)\nNodes: {}\n"
        "Elements: {} Tet4\nBoundary triangles: {}\nVolume: {:.6g} mm^3\n",
        (*mesh)->mesh().nodeCount(), (*mesh)->mesh().tetrahedra().size(),
        (*mesh)->mesh().triangles().size(), (*mesh)->tetrahedralVolume().in(units::mm3));
    emit(out, json, structured, prose);
    return ExitCode::Success;
}

ExitCode runMeshInfo(Args args, std::ostream& out, std::ostream& err) {
    bool json = false;
    auto path = onlyPath(args, &json);
    if (!path) {
        return usageError("mesh-info", kMeshInfoUsage, path.error().message, err);
    }
    auto prepared = prepare("mesh-info", *path, err);
    if (!prepared) {
        return prepared.error();
    }
    meshing::Mesher mesher;
    auto mesh = generate("mesh-info", *prepared, mesher, err);
    if (!mesh) {
        return mesh.error();
    }
    const meshing::MeshControl* control =
        meshing::findMeshControl(prepared->document, prepared->control);

    JsonValue structured = meshJson(mesher, prepared->document, prepared->control, **mesh);
    structured.set("body", JsonValue::text(label(prepared->document, (*mesh)->source())));
    structured.set("localSizingCount",
                   JsonValue::number(control->definition().mesh.sizing.local.size()));
    structured.set("boundarySetCount",
                   JsonValue::number(control->definition().boundarySets.size()));
    // A SUMMARY, AND ONLY A SUMMARY, at any mesh size. A report that printed
    // a hundred thousand handles by default would be useless to a person and
    // ruinous in a log.
    const std::string prose = std::format(
        "Mesh: generated now (a mesh is derived; the document holds intent)\n"
        "Body: {}\nNodes: {}\nElements: {} Tet4\nBoundary triangles: {}\n"
        "Volume: {:.6g} mm^3\nLocal sizing: {}\nBoundary sets: {}\n",
        label(prepared->document, (*mesh)->source()), (*mesh)->mesh().nodeCount(),
        (*mesh)->mesh().tetrahedra().size(), (*mesh)->mesh().triangles().size(),
        (*mesh)->tetrahedralVolume().in(units::mm3),
        control->definition().mesh.sizing.local.size(),
        control->definition().boundarySets.size());
    emit(out, json, structured, prose);
    return ExitCode::Success;
}

ExitCode runMeshQuality(Args args, std::ostream& out, std::ostream& err) {
    bool json = false;
    auto path = onlyPath(args, &json);
    if (!path) {
        return usageError("mesh-quality", kMeshQualityUsage, path.error().message, err);
    }
    auto prepared = prepare("mesh-quality", *path, err);
    if (!prepared) {
        return prepared.error();
    }
    meshing::Mesher mesher;
    auto mesh = generate("mesh-quality", *prepared, mesher, err);
    if (!mesh) {
        return mesh.error();
    }
    // THE CORE'S REPORT, not a recomputation. The classification is
    // P16-QUALITY-001's, under the control's own threshold policy; there is no
    // CLI threshold in this file.
    const meshing::MeshQualityReport* report = mesher.quality(prepared->control);
    if (report == nullptr) {
        failure("mesh-quality", "no_quality_report", "the mesher produced no quality report", err);
        return ExitCode::Failure;
    }

    JsonValue structured = JsonValue::object();
    structured.set("state", JsonValue::text(currencyName(
                                mesher.currency(prepared->document, prepared->control))));
    structured.set("meshSource", JsonValue::text("generatedNow"));
    structured.set("describesCurrentPolicy",
                   JsonValue::boolean(mesher.qualityDescribesCurrentPolicy(prepared->document,
                                                                           prepared->control)));
    structured.set("structurallyValid", JsonValue::boolean(report->structurallyValid));
    structured.set("invalidElements", JsonValue::number(report->invalidElements));
    structured.set("warningElements", JsonValue::number(report->warningElements));
    structured.set("failureElements", JsonValue::number(report->failureElements));
    structured.set("satisfiesPolicy", JsonValue::boolean(report->satisfiesPolicy()));
    structured.set("tetCount", JsonValue::number(report->tetCount));
    structured.set("triangleCount", JsonValue::number(report->triangleCount));
    structured.set("validElements", JsonValue::number(report->validElements));

    JsonValue metrics = JsonValue::array();
    std::string prose =
        std::format("Quality: {} invalid, {} warnings, {} failures\nStructurally valid: {}\n",
                    report->invalidElements, report->warningElements, report->failureElements,
                    report->structurallyValid ? "yes" : "no");
    // IN THE REPORT'S OWN ORDER, which is the metric enumeration's --
    // `summaries` is a std::map keyed by QualityMetric, so iterating it is
    // deterministic and needs no sort. A different order here would be a
    // second externally visible contract.
    for (const auto& [metric, summary] : report->summaries) {
        JsonValue entry = JsonValue::object();
        entry.set("metric", JsonValue::text(toString(metric)));
        entry.set("count", JsonValue::number(summary.count));
        entry.set("minimum", JsonValue::number(summary.minimum));
        entry.set("maximum", JsonValue::number(summary.maximum));
        entry.set("mean", JsonValue::number(summary.mean));
        if (summary.worst.isValid()) {
            // CURRENT-MESH-LOCAL, and labelled so. An ElementId belongs to one
            // generation and is not persistent intent.
            entry.set("worstElement",
                      JsonValue::number(static_cast<std::size_t>(summary.worst.value())));
        }
        metrics.push(std::move(entry));
    }
    structured.set("metrics", std::move(metrics));

    // The findings, in the core's own order (P16-QUALITY-001 orders them).
    JsonValue findings = JsonValue::array();
    for (const meshing::QualityFinding& finding : report->findings) {
        JsonValue entry = JsonValue::object();
        // ABSENT STAYS ABSENT. `metric`, `value` and `threshold` are optional
        // because a structural refusal computed no metric at all, and
        // MeshQuality.hpp says why in as many words: naming a metric with a
        // value of 0 "would be fabricated data in a structured field, and a
        // GUI or a CLI reading the payload instead of the message would
        // believe it". So null, never a default.
        entry.set("metric",
                  finding.metric ? JsonValue::text(toString(*finding.metric)) : JsonValue::null());
        entry.set("element", JsonValue::number(static_cast<std::size_t>(finding.element.value())));
        entry.set("value", finding.value ? JsonValue::number(*finding.value) : JsonValue::null());
        entry.set("threshold",
                  finding.threshold ? JsonValue::number(*finding.threshold) : JsonValue::null());
        entry.set("classification", JsonValue::text(toString(finding.classification)));
        findings.push(std::move(entry));
    }
    structured.set("findings", std::move(findings));
    emit(out, json, structured, prose);
    // A QUALITY WARNING IS NOT A FAILURE OF THIS COMMAND. The report was
    // produced; whether the mesh is good enough is the engineer's policy, and
    // a structurally invalid element is mesh-validate's to gate.
    return ExitCode::Success;
}

ExitCode runMeshValidate(Args args, std::ostream& out, std::ostream& err) {
    bool json = false;
    auto path = onlyPath(args, &json);
    if (!path) {
        return usageError("mesh-validate", kMeshValidateUsage, path.error().message, err);
    }
    auto prepared = prepare("mesh-validate", *path, err);
    if (!prepared) {
        return prepared.error();
    }
    meshing::Mesher mesher;
    auto mesh = generate("mesh-validate", *prepared, mesher, err);
    if (!mesh) {
        return mesh.error();
    }
    // P16-DATA-001's structural check. STRUCTURE, not shape: an inverted or
    // degenerate element is an issue here, a poor aspect ratio is not -- that
    // is mesh-quality's, and conflating them would make a thin element look
    // like a broken one.
    const meshing::MeshValidationReport report = meshing::validate((*mesh)->mesh());

    JsonValue structured = JsonValue::object();
    structured.set("dataValid", JsonValue::boolean(report.dataValid()));
    structured.set("issueCount", JsonValue::number(report.issues.size()));
    JsonValue issues = JsonValue::array();
    for (const meshing::MeshIssue& issue : report.issues) {
        JsonValue entry = JsonValue::object();
        entry.set("kind", JsonValue::text(toString(issue.kind)));
        entry.set("message", JsonValue::text(issue.message));
        issues.push(std::move(entry));
    }
    structured.set("issues", std::move(issues));

    std::string prose = std::format("Validation: {}\n", report.dataValid() ? "PASS" : "FAIL");
    for (const meshing::MeshIssue& issue : report.issues) {
        prose += std::format("  {}: {}\n", toString(issue.kind), issue.message);
    }
    emit(out, json, structured, prose);
    // A STRUCTURALLY INVALID MESH GATES, following `validate`'s own precedent:
    // a report that gates is only usable from a script if the gate reaches the
    // exit status.
    return report.dataValid() ? ExitCode::Success : ExitCode::Failure;
}

ExitCode runMeshBoundaries(Args args, std::ostream& out, std::ostream& err) {
    auto parsed = parseArguments(args, {{"--json", false}});
    if (!parsed) {
        return usageError("mesh-boundaries", kMeshBoundariesUsage, parsed.error().message, err);
    }
    if (parsed->positional().empty() || parsed->positional().size() > 2) {
        return usageError("mesh-boundaries", kMeshBoundariesUsage,
                          "expected a document and optionally one face", err);
    }
    const bool json = parsed->has("--json");
    auto prepared = prepare("mesh-boundaries", std::string{parsed->positional()[0]}, err);
    if (!prepared) {
        return prepared.error();
    }
    meshing::Mesher mesher;
    auto mesh = generate("mesh-boundaries", *prepared, mesher, err);
    if (!mesh) {
        return mesh.error();
    }
    const meshing::GeometryMeshMap* map = mesher.map(prepared->control);
    if (map == nullptr) {
        failure("mesh-boundaries", "no_mapping", "the mesher produced no geometry mapping", err);
        return ExitCode::Failure;
    }
    const meshing::MeshControl* control =
        meshing::findMeshControl(prepared->document, prepared->control);

    JsonValue structured = JsonValue::object();
    structured.set("meshSource", JsonValue::text("generatedNow"));

    if (parsed->positional().size() == 2) {
        // ONE FACE, through P16-MAP's own resolution. No nearest-face search
        // anywhere: a face either has mapped facets in this mesh or it does
        // not, and "does not" is an answer.
        auto face = parseFaceReference(prepared->document, parsed->positional()[1]);
        if (!face) {
            return usageError("mesh-boundaries", kMeshBoundariesUsage, face.error().message, err);
        }
        const meshing::NamedBoundarySet single{
            .id = BoundarySetId::fromValue(1U), .name = "query", .faces = {*face}};
        const Result<meshing::ResolvedBoundarySet> resolved =
            meshing::resolveBoundarySet(single, *map);
        if (!resolved) {
            failure("mesh-boundaries", "boundary_unresolved", resolved.error().message, err);
            return ExitCode::Failure;
        }
        structured.set("face", faceJson(prepared->document, *face));
        structured.set("resolved", JsonValue::boolean(resolved->fullyResolved()));
        structured.set("facetCount", JsonValue::number(resolved->mapping.facets.size()));
        const std::string prose =
            std::format("{}  {}  {}\n", faceText(prepared->document, *face),
                        resolved->fullyResolved() ? "resolved" : "unresolved",
                        plural(resolved->mapping.facets.size(), "facet", "facets"));
        emit(out, json, structured, prose);
        // A QUERY THAT REPORTS "UNRESOLVED" HAS SUCCEEDED. Observation is not
        // operation -- the brief's distinction, and the reason this exits 0.
        return ExitCode::Success;
    }

    JsonValue sets = JsonValue::array();
    const std::vector<meshing::NamedBoundarySet> ordered = control->orderedBoundarySets();
    std::string prose = std::format("Boundary sets ({}):\n", ordered.size());
    for (const meshing::NamedBoundarySet& set : ordered) {
        const Result<meshing::ResolvedBoundarySet> resolved =
            meshing::resolveBoundarySet(set, *map);
        JsonValue entry = JsonValue::object();
        entry.set("id", JsonValue::number(static_cast<std::size_t>(set.id.value())));
        entry.set("name", JsonValue::text(set.name));
        JsonValue faces = JsonValue::array();
        for (const FaceName& face : set.faces) {
            faces.push(faceJson(prepared->document, face));
        }
        entry.set("faces", std::move(faces));
        if (resolved) {
            entry.set("resolved", JsonValue::boolean(resolved->fullyResolved()));
            entry.set("facetCount", JsonValue::number(resolved->mapping.facets.size()));
            prose += std::format("  {}  {}  {}  {}\n", set.id.value(), set.name,
                                 resolved->fullyResolved() ? "resolved" : "unresolved",
                                 plural(resolved->mapping.facets.size(), "facet", "facets"));
        } else {
            entry.set("resolved", JsonValue::boolean(false));
            entry.set("diagnostic", JsonValue::text(resolved.error().message));
            prose += std::format("  {}  {}  unresolved: {}\n", set.id.value(), set.name,
                                 resolved.error().message);
        }
        sets.push(std::move(entry));
    }
    structured.set("boundarySets", std::move(sets));
    emit(out, json, structured, prose);
    return ExitCode::Success;
}

} // namespace bettercad::cli
