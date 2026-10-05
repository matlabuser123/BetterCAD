#include "io/json/ObjectJson.hpp"

#include <array>
#include <format>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The persisted form of a meshing control's canonical intent
// (P16-PERSIST-001).
//
// WHAT IS PERSISTED: the body to mesh, the boundary discretisation, the global
// and face-local element sizing, the quality threshold POLICY, and the named
// boundary sets' identities, names and face selections.
//
// WHAT IS NOT, and could not be even deliberately: nodes, tetrahedra, surface
// triangles, boundary facets, the geometry-mesh mapping, a quality REPORT,
// render buffers, backend handles. A `MeshControlDefinition` has no member for
// any of them -- `VolumeMesh` and `GeometryMeshMap` cannot even be
// default-constructed (ADR-030) -- so there is nothing here to exclude. The
// derived-state rule is structural:
//
//     save  ->  meshing intent
//     load  ->  regenerate from current geometry + intent
//
// VALIDATION IS THE CORE'S, NOT THIS FILE'S. Reading ends in
// MeshControl::create, which delegates to validate(MeshSizingControls),
// validate(QualityThresholds) and validate(NamedBoundarySet). So a file
// carrying a zero, negative or non-finite size, two local controls on one
// face, or two boundary sets sharing an identity is refused by the same rules
// the API enforces -- and there is no sizing arithmetic in this file to drift
// from them. That matters particularly for non-finite values: `readNumber`
// checks `is_number()` and would accept an infinity written as 1e999.
//
// FACE REFERENCES GO THROUGH faceNameToJson. BetterCAD's canonical face
// reference already has a qualified serialization, used by datums, drafts,
// shells, dimensions and annotations, which writes every field of the selector
// and validates it on reading. A second encoding here would be a second
// contract.
//
// DETERMINISM IS DELIBERATE. `Json` is `nlohmann::ordered_json`, so the file's
// key order is this file's INSERTION order and its array order is whatever
// order it is given. Every collection is therefore written through
// MeshControl's ordered accessors -- `orderedLocalSizing()` sorts by FaceName,
// `orderedBoundarySets()` by BoundarySetId -- and never by iterating the
// stored vectors. A consequence: the stored order of local controls is not
// preserved across a save. It is not semantic (P16-SIZE-001 declares it
// meaningless, and P16-CMD-001's canonical fingerprint ignores it), and
// writing the canonical order instead is what makes save/load/save converge to
// identical bytes and two documents with the same intent produce the same
// file.
namespace bettercad::io::detail {

namespace {

// The dispatch in DocumentJson.cpp compares against the literal
// "mesh-control", never against kTypeName: binding a reference to a
// dll-imported constexpr static does not link in a shared build, which the
// debug-shared preset found for components and again for materials. This keeps
// the two from drifting.
static_assert(meshing::MeshControl::kTypeName == std::string_view{"mesh-control"});

/// A stable file key for every quality metric.
///
/// STRINGS, NOT INTEGERS, and not the diagnostic strings either. An integer
/// would silently become a different metric the day an enumerator is inserted
/// in the middle; `toString(QualityMetric)` is display text a later milestone
/// may reword or translate, and a file key has to outlive that.
///
/// `metricName` falls back to "unknown" for a value missing from this table,
/// which would serialize silently and then fail to load. A table-size check
/// cannot catch a WRONG entry, so the guard is
/// MeshingPersistence_EveryQualityMetricRoundTrips, which puts a threshold on
/// each of these in turn.
constexpr std::array<std::pair<meshing::QualityMetric, std::string_view>, 18> kQualityMetrics{{
    {meshing::QualityMetric::TetVolume, "tet_volume"},
    {meshing::QualityMetric::TetJacobianDeterminant, "tet_jacobian_determinant"},
    {meshing::QualityMetric::TetMinEdgeLength, "tet_min_edge_length"},
    {meshing::QualityMetric::TetMaxEdgeLength, "tet_max_edge_length"},
    {meshing::QualityMetric::TetMeanEdgeLength, "tet_mean_edge_length"},
    {meshing::QualityMetric::TetAspectRatio, "tet_aspect_ratio"},
    {meshing::QualityMetric::TetRadiusRatio, "tet_radius_ratio"},
    {meshing::QualityMetric::TetInradius, "tet_inradius"},
    {meshing::QualityMetric::TetCircumradius, "tet_circumradius"},
    {meshing::QualityMetric::TetMinDihedralAngle, "tet_min_dihedral_angle"},
    {meshing::QualityMetric::TetMaxDihedralAngle, "tet_max_dihedral_angle"},
    {meshing::QualityMetric::TriangleArea, "triangle_area"},
    {meshing::QualityMetric::TriangleMinEdgeLength, "triangle_min_edge_length"},
    {meshing::QualityMetric::TriangleMaxEdgeLength, "triangle_max_edge_length"},
    {meshing::QualityMetric::TriangleMeanEdgeLength, "triangle_mean_edge_length"},
    {meshing::QualityMetric::TriangleShapeQuality, "triangle_shape_quality"},
    {meshing::QualityMetric::TriangleMinAngle, "triangle_min_angle"},
    {meshing::QualityMetric::TriangleMaxAngle, "triangle_max_angle"},
}};

[[nodiscard]] std::string metricName(meshing::QualityMetric metric) {
    for (const auto& [item, name] : kQualityMetrics) {
        if (item == metric) {
            return std::string{name};
        }
    }
    return "unknown";
}

[[nodiscard]] Result<meshing::QualityMetric> readMetric(const Json& object, std::string_view path) {
    auto text = readString(object, "metric", path);
    if (!text) {
        return std::unexpected(text.error());
    }
    for (const auto& [value, name] : kQualityMetrics) {
        if (name == *text) {
            return value;
        }
    }
    return parseError(childPath(path, "metric"), std::format("unknown quality metric '{}'", *text));
}

/// A number that may be absent. Absent is a state, not a zero.
[[nodiscard]] Result<std::optional<double>> readOptionalNumber(const Json& object,
                                                               std::string_view key,
                                                               std::string_view path) {
    const auto it = object.find(std::string{key});
    if (it == object.end()) {
        return std::optional<double>{};
    }
    if (!it->is_number()) {
        return parseError(childPath(path, key), "expected a number");
    }
    return std::optional<double>{it->get<double>()};
}

[[nodiscard]] Json surfaceToJson(const meshing::SurfaceMeshControls& surface) {
    Json json = Json::object();
    // Always written, both of them. They always have a value -- there is no
    // "absent deflection" -- and an engineering document is better off
    // carrying the number it was meshed with than reconstructing it from
    // whatever this build's default happens to be later.
    json["linear_deflection"] = surface.linearDeflection.si();
    json["angular_deflection"] = surface.angularDeflection.si();
    return json;
}

[[nodiscard]] Result<meshing::SurfaceMeshControls> surfaceFromJson(const Json& value,
                                                                   std::string_view path) {
    if (auto object = requireObject(value, path, {"linear_deflection", "angular_deflection"});
        !object) {
        return std::unexpected(object.error());
    }
    auto linear = readNumber(value, "linear_deflection", path);
    auto angular = readNumber(value, "angular_deflection", path);
    if (!linear || !angular) {
        return std::unexpected(!linear ? linear.error() : angular.error());
    }
    return meshing::SurfaceMeshControls{.linearDeflection = Length::fromSi(*linear),
                                        .angularDeflection = Angle::fromSi(*angular)};
}

/// The sizing section, written from the control's ORDERED view of its local
/// controls.
[[nodiscard]] Json sizingToJson(const meshing::MeshControl& control) {
    const meshing::MeshSizingControls& sizing = control.definition().mesh.sizing;
    Json json = Json::object();
    // ABSENT MEANS "BetterCAD's scale-relative default", which is a canonical
    // state and not a missing number, so there is nothing to write for it.
    if (sizing.globalTargetSize) {
        json["target_size"] = sizing.globalTargetSize->si();
    }
    const std::vector<meshing::LocalMeshSizing> local = control.orderedLocalSizing();
    if (!local.empty()) {
        Json controls = Json::array();
        for (const meshing::LocalMeshSizing& item : local) {
            Json entry = Json::object();
            entry["face"] = faceNameToJson(item.face);
            entry["target_size"] = item.targetSize.si();
            controls.push_back(std::move(entry));
        }
        json["local"] = std::move(controls);
    }
    return json;
}

[[nodiscard]] Result<meshing::MeshSizingControls> sizingFromJson(const Json& value,
                                                                 std::string_view path) {
    if (auto object = requireObject(value, path, {"target_size", "local"}); !object) {
        return std::unexpected(object.error());
    }
    auto target = readOptionalNumber(value, "target_size", path);
    if (!target) {
        return std::unexpected(target.error());
    }
    meshing::MeshSizingControls sizing;
    if (*target) {
        sizing.globalTargetSize = Length::fromSi(**target);
    }
    if (!value.contains("local")) {
        return sizing;
    }
    auto controls = requireArray(value, "local", path);
    if (!controls) {
        return std::unexpected(controls.error());
    }
    const std::string localPath = childPath(path, "local");
    for (std::size_t i = 0; i < (*controls)->size(); ++i) {
        const std::string itemPath = indexPath(localPath, i);
        const Json& entry = (**controls)[i];
        if (auto object = requireObject(entry, itemPath, {"face", "target_size"}); !object) {
            return std::unexpected(object.error());
        }
        auto faceField = requireField(entry, "face", itemPath);
        auto size = readNumber(entry, "target_size", itemPath);
        if (!faceField || !size) {
            return std::unexpected(!faceField ? faceField.error() : size.error());
        }
        auto face = faceNameFromJson(**faceField, childPath(itemPath, "face"));
        if (!face) {
            return std::unexpected(face.error());
        }
        // NOT CHECKED HERE: whether the size is positive and finite, and
        // whether this face already has a control. Both are P16-SIZE-001's
        // rules and both are enforced by MeshControl::create below.
        sizing.local.push_back(
            meshing::LocalMeshSizing{.face = std::move(*face), .targetSize = Length::fromSi(*size)});
    }
    return sizing;
}

[[nodiscard]] Json qualityToJson(const meshing::QualityThresholds& quality) {
    Json json = Json::object();
    if (quality.limits.empty()) {
        return json;
    }
    // An ARRAY, not an object keyed by metric, and the reason is duplicates: a
    // JSON object with the same key twice silently keeps one, which is the
    // kind of quiet loss this format refuses everywhere else. An array makes a
    // repeated metric visible, and readQuality rejects it by name.
    //
    // Ordered because `limits` is a std::map keyed by the metric enum, so this
    // follows the enumeration and not the order thresholds were set in.
    Json limits = Json::array();
    for (const auto& [metric, threshold] : quality.limits) {
        Json entry = Json::object();
        entry["metric"] = metricName(metric);
        if (threshold.warning) {
            entry["warning"] = *threshold.warning;
        }
        if (threshold.failure) {
            entry["failure"] = *threshold.failure;
        }
        limits.push_back(std::move(entry));
    }
    json["limits"] = std::move(limits);
    return json;
}

[[nodiscard]] Result<meshing::QualityThresholds> qualityFromJson(const Json& value,
                                                                 std::string_view path) {
    if (auto object = requireObject(value, path, {"limits"}); !object) {
        return std::unexpected(object.error());
    }
    meshing::QualityThresholds quality;
    if (!value.contains("limits")) {
        return quality;
    }
    auto limits = requireArray(value, "limits", path);
    if (!limits) {
        return std::unexpected(limits.error());
    }
    const std::string limitsPath = childPath(path, "limits");
    for (std::size_t i = 0; i < (*limits)->size(); ++i) {
        const std::string itemPath = indexPath(limitsPath, i);
        const Json& entry = (**limits)[i];
        if (auto object = requireObject(entry, itemPath, {"metric", "warning", "failure"}); !object) {
            return std::unexpected(object.error());
        }
        auto metric = readMetric(entry, itemPath);
        auto warning = readOptionalNumber(entry, "warning", itemPath);
        auto failure = readOptionalNumber(entry, "failure", itemPath);
        if (!metric || !warning || !failure) {
            const Error& error = !metric ? metric.error() : !warning ? warning.error() : failure.error();
            return std::unexpected(error);
        }
        // REFUSED, not merged. Two entries for one metric is a modelling
        // mistake in the file, and choosing one would hide it.
        if (quality.limits.contains(*metric)) {
            return parseError(childPath(itemPath, "metric"),
                              std::format("the metric '{}' is given a threshold twice",
                                          metricName(*metric)));
        }
        quality.limits.emplace(*metric,
                               meshing::QualityThreshold{.warning = *warning, .failure = *failure});
    }
    return quality;
}

/// The boundary sets, written from the control's ORDERED view.
[[nodiscard]] Json boundarySetsToJson(const meshing::MeshControl& control) {
    Json sets = Json::array();
    for (const meshing::NamedBoundarySet& set : control.orderedBoundarySets()) {
        Json entry = Json::object();
        // The IDENTITY, written first and restored as given. A set that came
        // back under a new ID would break every reference to it.
        entry["id"] = set.id.value();
        entry["name"] = set.name;
        Json faces = Json::array();
        // The face selection's own order, which is the user's and is kept:
        // unlike the stored order of local controls, a set's face list is a
        // sequence the user gave.
        for (const FaceName& face : set.faces) {
            faces.push_back(faceNameToJson(face));
        }
        entry["faces"] = std::move(faces);
        sets.push_back(std::move(entry));
    }
    return sets;
}

[[nodiscard]] Result<std::vector<meshing::NamedBoundarySet>> boundarySetsFromJson(
    const Json& value, std::string_view path) {
    std::vector<meshing::NamedBoundarySet> sets;
    if (!value.is_array()) {
        return parseError(path, "expected an array");
    }
    for (std::size_t i = 0; i < value.size(); ++i) {
        const std::string itemPath = indexPath(path, i);
        const Json& entry = value[i];
        if (auto object = requireObject(entry, itemPath, {"id", "name", "faces"}); !object) {
            return std::unexpected(object.error());
        }
        auto id = readId(entry, "id", itemPath);
        auto name = readString(entry, "name", itemPath);
        auto faces = requireArray(entry, "faces", itemPath);
        if (!id || !name || !faces) {
            const Error& error = !id ? id.error() : !name ? name.error() : faces.error();
            return std::unexpected(error);
        }
        meshing::NamedBoundarySet set;
        set.id = BoundarySetId::fromValue(*id);
        set.name = std::move(*name);
        const std::string facesPath = childPath(itemPath, "faces");
        for (std::size_t f = 0; f < (*faces)->size(); ++f) {
            auto face = faceNameFromJson((**faces)[f], indexPath(facesPath, f));
            if (!face) {
                return std::unexpected(face.error());
            }
            set.faces.push_back(std::move(*face));
        }
        // NOT CHECKED HERE: a duplicate identity, an empty name, an empty face
        // list, a face listed twice. All four are validate(NamedBoundarySet)'s
        // and MeshControl::validate's, and both run in create() below.
        sets.push_back(std::move(set));
    }
    return sets;
}

} // namespace

Json meshControlToJson(const meshing::MeshControl& control) {
    const meshing::MeshControlDefinition& d = control.definition();
    Json json = Json::object();
    // Keys inserted in a fixed order, because ordered_json writes insertion
    // order and this file's determinism claim rests on it.
    json["body"] = d.body.value();
    json["surface"] = surfaceToJson(d.mesh.surface);
    json["sizing"] = sizingToJson(control);
    // Omitted when the policy says nothing, which is BetterCAD's default:
    // report-only, with no thresholds (P16-QUALITY-001's deliberate choice,
    // because no solver exists yet to set them).
    if (!d.quality.limits.empty()) {
        json["quality"] = qualityToJson(d.quality);
    }
    if (!d.boundarySets.empty()) {
        json["boundary_sets"] = boundarySetsToJson(control);
    }
    return json;
}

Result<std::unique_ptr<meshing::MeshControl>> meshControlFromJson(const Json& data,
                                                                  std::string name,
                                                                  std::string_view path) {
    if (auto object =
            requireObject(data, path, {"body", "surface", "sizing", "quality", "boundary_sets"});
        !object) {
        return std::unexpected(object.error());
    }
    auto body = readId(data, "body", path);
    auto surfaceField = requireField(data, "surface", path);
    auto sizingField = requireField(data, "sizing", path);
    if (!body || !surfaceField || !sizingField) {
        const Error& error = !body ? body.error() : !surfaceField ? surfaceField.error() : sizingField.error();
        return std::unexpected(error);
    }
    auto surface = surfaceFromJson(**surfaceField, childPath(path, "surface"));
    if (!surface) {
        return std::unexpected(surface.error());
    }
    auto sizing = sizingFromJson(**sizingField, childPath(path, "sizing"));
    if (!sizing) {
        return std::unexpected(sizing.error());
    }

    meshing::MeshControlDefinition definition;
    definition.body = ObjectId::fromValue(*body);
    definition.mesh.surface = *surface;
    definition.mesh.sizing = std::move(*sizing);

    if (data.contains("quality")) {
        auto quality = qualityFromJson(data["quality"], childPath(path, "quality"));
        if (!quality) {
            return std::unexpected(quality.error());
        }
        definition.quality = std::move(*quality);
    }
    if (data.contains("boundary_sets")) {
        auto sets = boundarySetsFromJson(data["boundary_sets"], childPath(path, "boundary_sets"));
        if (!sets) {
            return std::unexpected(sets.error());
        }
        definition.boundarySets = std::move(*sets);
    }

    // THROUGH MeshControl::create, so the file goes through exactly the
    // validation the API does: positive and finite sizes, no two controls on
    // one face, no two sets sharing an identity, every selector well formed.
    // A valid-looking file that violates any of them is refused here rather
    // than becoming a document nobody can mesh.
    auto control = meshing::MeshControl::create(std::move(name), std::move(definition));
    if (!control) {
        return atPath(path, control.error());
    }
    return std::move(*control);
}

} // namespace bettercad::io::detail
