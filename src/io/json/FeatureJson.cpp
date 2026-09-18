#include "io/json/ObjectJson.hpp"

#include <array>
#include <cstdint>
#include <format>
#include <limits>
#include <string_view>
#include <utility>

namespace bettercad::io::detail {

namespace {

using features::ExtrudeDirection;
using features::FeatureOperation;
using features::RevolveAxisKind;
using features::RevolveDirection;

constexpr std::array<std::pair<features::ExtrudeTermination, std::string_view>, 2> kTerminations{{
    {features::ExtrudeTermination::Blind, "blind"},
    {features::ExtrudeTermination::ThroughAll, "through_all"},
}};
constexpr std::array<std::pair<ExtrudeDirection, std::string_view>, 3> kDirections{{
    {ExtrudeDirection::Normal, "normal"},
    {ExtrudeDirection::Reversed, "reversed"},
    {ExtrudeDirection::Symmetric, "symmetric"},
}};

constexpr std::array<std::pair<RevolveDirection, std::string_view>, 3> kRevolveDirections{{
    {RevolveDirection::Positive, "positive"},
    {RevolveDirection::Negative, "negative"},
    {RevolveDirection::Symmetric, "symmetric"},
}};

constexpr std::array<std::pair<RevolveAxisKind, std::string_view>, 3> kAxisKinds{{
    {RevolveAxisKind::SketchX, "sketch_x"},
    {RevolveAxisKind::SketchY, "sketch_y"},
    {RevolveAxisKind::Line, "line"},
}};

constexpr std::array<std::pair<FeatureOperation, std::string_view>, 4> kOperations{{
    {FeatureOperation::NewBody, "new_body"},
    {FeatureOperation::Join, "join"},
    {FeatureOperation::Cut, "cut"},
    {FeatureOperation::Intersect, "intersect"},
}};

template <typename Enum, std::size_t N>
std::string_view nameOf(const std::array<std::pair<Enum, std::string_view>, N>& table, Enum value) {
    for (const auto& [candidate, name] : table) {
        if (candidate == value) {
            return name;
        }
    }
    return "unknown";
}

template <typename Enum, std::size_t N>
Result<Enum> valueOf(const std::array<std::pair<Enum, std::string_view>, N>& table, const Json& object,
                     std::string_view key, std::string_view path) {
    auto text = readString(object, key, path);
    if (!text) {
        return std::unexpected(text.error());
    }
    for (const auto& [value, name] : table) {
        if (name == *text) {
            return value;
        }
    }
    return parseError(childPath(path, key), std::format("unknown value '{}'", *text));
}

} // namespace

Json extrudeToJson(const features::ExtrudeFeature& feature) {
    const features::ExtrudeDefinition& d = feature.definition();
    Json json = Json::object();
    json["profile"] = d.profile.value();
    if (d.termination == features::ExtrudeTermination::Blind) {
        json["depth"] = d.depth.si();
        if (d.depthParameter) {
            json["depth_parameter"] = d.depthParameter->value();
        }
    } else {
        // A through-all extrude has no depth (P12-FEAT-001).
        json["termination"] = std::string{nameOf(kTerminations, d.termination)};
    }
    json["direction"] = std::string{nameOf(kDirections, d.direction)};
    json["operation"] = std::string{nameOf(kOperations, d.operation)};
    if (d.target) {
        json["target"] = d.target->value();
    }
    return json;
}

Result<std::unique_ptr<features::ExtrudeFeature>> extrudeFromJson(const Json& data, std::string name,
                                                                  std::string_view path) {
    if (auto object = requireObject(
            data, path, {"profile", "termination", "depth", "depth_parameter", "direction", "operation", "target"});
        !object) {
        return std::unexpected(object.error());
    }
    features::ExtrudeTermination termination = features::ExtrudeTermination::Blind;
    if (data.contains("termination")) {
        auto read = valueOf(kTerminations, data, "termination", path);
        if (!read) {
            return std::unexpected(read.error());
        }
        termination = *read;
    }
    if (termination == features::ExtrudeTermination::ThroughAll) {
        for (const std::string_view key : {"depth", "depth_parameter"}) {
            if (data.contains(std::string{key})) {
                return parseError(childPath(path, key), "a through-all extrude has no depth");
            }
        }
    }
    auto profile = readId(data, "profile", path);
    Result<double> depth = 0.0;
    if (termination == features::ExtrudeTermination::Blind) {
        depth = readNumber(data, "depth", path);
    }
    auto depthParameter = readOptionalId(data, "depth_parameter", path);
    auto direction = valueOf(kDirections, data, "direction", path);
    auto operation = valueOf(kOperations, data, "operation", path);
    auto target = readOptionalId(data, "target", path);
    if (!profile || !depth || !depthParameter || !direction || !operation || !target) {
        const Error& error = !profile ? profile.error()
                           : !depth ? depth.error()
                           : !depthParameter ? depthParameter.error()
                           : !direction ? direction.error()
                           : !operation ? operation.error()
                                        : target.error();
        return std::unexpected(error);
    }
    features::ExtrudeDefinition definition{
        .profile = SketchId::fromValue(*profile),
        .depth = Length::fromSi(*depth),
        .depthParameter = std::nullopt,
        .direction = *direction,
        .operation = *operation,
        .target = std::nullopt,
        .termination = termination,
    };
    if (*depthParameter) {
        definition.depthParameter = ParameterId::fromValue(**depthParameter);
    }
    if (*target) {
        definition.target = FeatureId::fromValue(**target);
    }
    auto feature = features::ExtrudeFeature::create(std::move(name), definition);
    if (!feature) {
        return atPath(path, feature.error());
    }
    return std::move(*feature);
}

Json revolveToJson(const features::RevolveFeature& feature) {
    const features::RevolveDefinition& d = feature.definition();
    Json json = Json::object();
    json["profile"] = d.profile.value();
    Json axis = Json::object();
    axis["type"] = std::string{nameOf(kAxisKinds, d.axis.kind)};
    if (d.axis.kind == RevolveAxisKind::Line) {
        axis["line"] = d.axis.line.value();
    }
    json["axis"] = std::move(axis);
    json["angle"] = d.angle.si();
    if (d.angleParameter) {
        json["angle_parameter"] = d.angleParameter->value();
    }
    json["direction"] = std::string{nameOf(kRevolveDirections, d.direction)};
    json["operation"] = std::string{nameOf(kOperations, d.operation)};
    if (d.target) {
        json["target"] = d.target->value();
    }
    return json;
}

namespace {

Result<features::RevolveAxis> revolveAxisFromJson(const Json& data, std::string_view path) {
    auto field = requireField(data, "axis", path);
    if (!field) {
        return std::unexpected(field.error());
    }
    const std::string axisPath = childPath(path, "axis");
    if (auto object = requireObject(**field, axisPath, {"type", "line"}); !object) {
        return std::unexpected(object.error());
    }
    auto kind = valueOf(kAxisKinds, **field, "type", axisPath);
    if (!kind) {
        return std::unexpected(kind.error());
    }
    if (*kind != RevolveAxisKind::Line) {
        if ((*field)->contains("line")) {
            return parseError(childPath(axisPath, "line"), "only a line axis refers to a line");
        }
        return features::RevolveAxis{*kind, {}};
    }
    auto line = readId(**field, "line", axisPath);
    if (!line) {
        return std::unexpected(line.error());
    }
    return features::RevolveAxis::alongLine(EntityId::fromValue(*line));
}

} // namespace

Result<std::unique_ptr<features::RevolveFeature>> revolveFromJson(const Json& data, std::string name,
                                                                  std::string_view path) {
    if (auto object = requireObject(
            data, path, {"profile", "axis", "angle", "angle_parameter", "direction", "operation", "target"});
        !object) {
        return std::unexpected(object.error());
    }
    auto profile = readId(data, "profile", path);
    auto axis = revolveAxisFromJson(data, path);
    auto angle = readNumber(data, "angle", path);
    auto angleParameter = readOptionalId(data, "angle_parameter", path);
    auto direction = valueOf(kRevolveDirections, data, "direction", path);
    auto operation = valueOf(kOperations, data, "operation", path);
    auto target = readOptionalId(data, "target", path);
    if (!profile || !axis || !angle || !angleParameter || !direction || !operation || !target) {
        const Error& error = !profile ? profile.error()
                           : !axis ? axis.error()
                           : !angle ? angle.error()
                           : !angleParameter ? angleParameter.error()
                           : !direction ? direction.error()
                           : !operation ? operation.error()
                                        : target.error();
        return std::unexpected(error);
    }
    features::RevolveDefinition definition{
        .profile = SketchId::fromValue(*profile),
        .axis = *axis,
        .angle = Angle::fromSi(*angle),
        .angleParameter = std::nullopt,
        .direction = *direction,
        .operation = *operation,
        .target = std::nullopt,
    };
    if (*angleParameter) {
        definition.angleParameter = ParameterId::fromValue(**angleParameter);
    }
    if (*target) {
        definition.target = FeatureId::fromValue(**target);
    }
    auto feature = features::RevolveFeature::create(std::move(name), definition);
    if (!feature) {
        return atPath(path, feature.error());
    }
    return std::move(*feature);
}

namespace {

using features::SweepOrientation;

constexpr std::array<std::pair<SweepOrientation, std::string_view>, 1> kSweepOrientations{{
    {SweepOrientation::FollowPath, "follow_path"},
}};

/// An absent key reads as 0; defined with the chamfer readers below.
Result<double> readNumberOrZero(const Json& object, std::string_view key, std::string_view path);

/// The edge IDs of one run.
Result<std::vector<EntityId>> sweepEdgesFromJson(const Json& data, std::string_view path) {
    auto edges = requireArray(data, "edges", path);
    if (!edges) {
        return std::unexpected(edges.error());
    }
    std::vector<EntityId> result;
    const std::string edgesPath = childPath(path, "edges");
    for (std::size_t i = 0; i < (*edges)->size(); ++i) {
        auto edge = readId((**edges)[i], indexPath(edgesPath, i));
        if (!edge) {
            return std::unexpected(edge.error());
        }
        result.push_back(EntityId::fromValue(*edge));
    }
    return result;
}

/// A path: its first run's sketch and edges, and the further runs a spatial
/// path adds (P12-SWEEP-001), which are absent from a path written before
/// that milestone and then read as a path of one run.
Result<features::SweepPath> sweepPathValueFromJson(const Json& data, std::string_view path) {
    if (auto object = requireObject(data, path, {"sketch", "edges", "runs"}); !object) {
        return std::unexpected(object.error());
    }
    auto sketch = readId(data, "sketch", path);
    if (!sketch) {
        return std::unexpected(sketch.error());
    }
    auto edges = sweepEdgesFromJson(data, path);
    if (!edges) {
        return std::unexpected(edges.error());
    }
    features::SweepPath result{.sketch = SketchId::fromValue(*sketch), .edges = std::move(*edges)};
    if (!data.contains("runs")) {
        return result;
    }
    auto runs = requireArray(data, "runs", path);
    if (!runs) {
        return std::unexpected(runs.error());
    }
    const std::string runsPath = childPath(path, "runs");
    for (std::size_t i = 0; i < (*runs)->size(); ++i) {
        const std::string runPath = indexPath(runsPath, i);
        const Json& entry = (**runs)[i];
        if (auto object = requireObject(entry, runPath, {"sketch", "edges"}); !object) {
            return std::unexpected(object.error());
        }
        auto runSketch = readId(entry, "sketch", runPath);
        if (!runSketch) {
            return std::unexpected(runSketch.error());
        }
        auto runEdges = sweepEdgesFromJson(entry, runPath);
        if (!runEdges) {
            return std::unexpected(runEdges.error());
        }
        result.runs.push_back(
            features::SweepPathRun{.sketch = SketchId::fromValue(*runSketch), .edges = std::move(*runEdges)});
    }
    return result;
}

Result<features::SweepPath> sweepPathFromJson(const Json& data, std::string_view path) {
    auto field = requireField(data, "path", path);
    if (!field) {
        return std::unexpected(field.error());
    }
    return sweepPathValueFromJson(**field, childPath(path, "path"));
}

/// A path as JSON. The further runs are written only when there are any, so
/// a path of one run is written exactly as it was before P12-SWEEP-001.
Json sweepPathToJson(const features::SweepPath& value) {
    Json path = Json::object();
    path["sketch"] = value.sketch.value();
    Json edges = Json::array();
    for (const EntityId edge : value.edges) {
        edges.push_back(edge.value());
    }
    path["edges"] = std::move(edges);
    if (!value.runs.empty()) {
        Json runs = Json::array();
        for (const features::SweepPathRun& run : value.runs) {
            Json entry = Json::object();
            entry["sketch"] = run.sketch.value();
            Json runEdges = Json::array();
            for (const EntityId edge : run.edges) {
                runEdges.push_back(edge.value());
            }
            entry["edges"] = std::move(runEdges);
            runs.push_back(std::move(entry));
        }
        path["runs"] = std::move(runs);
    }
    return path;
}

} // namespace

Json sweepToJson(const features::SweepFeature& feature) {
    const features::SweepDefinition& d = feature.definition();
    Json json = Json::object();
    json["profile"] = d.profile.value();
    json["path"] = sweepPathToJson(d.path);
    json["orientation"] = std::string{nameOf(kSweepOrientations, d.orientation)};
    // Written only when they are not the default, so a sweep from before
    // P12-SWEEP-001 is written back exactly as it was read.
    if (d.twist != Angle{}) {
        json["twist"] = d.twist.si();
    }
    if (d.twistParameter) {
        json["twist_parameter"] = d.twistParameter->value();
    }
    if (d.guide) {
        json["guide"] = sweepPathToJson(*d.guide);
    }
    json["operation"] = std::string{nameOf(kOperations, d.operation)};
    if (d.target) {
        json["target"] = d.target->value();
    }
    return json;
}

Result<std::unique_ptr<features::SweepFeature>> sweepFromJson(const Json& data, std::string name,
                                                              std::string_view path) {
    if (auto object = requireObject(data, path,
                                    {"profile", "path", "orientation", "twist", "twist_parameter", "guide",
                                     "operation", "target"});
        !object) {
        return std::unexpected(object.error());
    }
    auto profile = readId(data, "profile", path);
    auto sweepPath = sweepPathFromJson(data, path);
    auto orientation = valueOf(kSweepOrientations, data, "orientation", path);
    auto twist = readNumberOrZero(data, "twist", path);
    auto twistParameter = readOptionalId(data, "twist_parameter", path);
    auto operation = valueOf(kOperations, data, "operation", path);
    auto target = readOptionalId(data, "target", path);
    if (!profile || !sweepPath || !orientation || !twist || !twistParameter || !operation || !target) {
        const Error& error = !profile ? profile.error()
                           : !sweepPath ? sweepPath.error()
                           : !orientation ? orientation.error()
                           : !twist ? twist.error()
                           : !twistParameter ? twistParameter.error()
                           : !operation ? operation.error()
                                        : target.error();
        return std::unexpected(error);
    }
    features::SweepDefinition definition{
        .profile = SketchId::fromValue(*profile),
        .path = std::move(*sweepPath),
        .orientation = *orientation,
        .twist = Angle::fromSi(*twist),
        .twistParameter = std::nullopt,
        .guide = std::nullopt,
        .operation = *operation,
        .target = std::nullopt,
    };
    if (*twistParameter) {
        definition.twistParameter = ParameterId::fromValue(**twistParameter);
    }
    if (data.contains("guide")) {
        auto guide = sweepPathValueFromJson(data["guide"], childPath(path, "guide"));
        if (!guide) {
            return std::unexpected(guide.error());
        }
        definition.guide = std::move(*guide);
    }
    if (*target) {
        definition.target = FeatureId::fromValue(**target);
    }
    auto feature = features::SweepFeature::create(std::move(name), definition);
    if (!feature) {
        return atPath(path, feature.error());
    }
    return std::move(*feature);
}

namespace {

using geometry::ChamferMode;
using geometry::EdgeCurve;

constexpr std::array<std::pair<ChamferMode, std::string_view>, 3> kChamferModes{{
    {ChamferMode::EqualDistance, "equal_distance"},
    {ChamferMode::TwoDistance, "two_distance"},
    {ChamferMode::DistanceAngle, "distance_angle"},
}};

// Only curves that can be referenced; see geometry::validate(EdgeSignature).
constexpr std::array<std::pair<EdgeCurve, std::string_view>, 2> kEdgeCurves{{
    {EdgeCurve::Line, "line"},
    {EdgeCurve::Circle, "circle"},
}};

} // namespace

Json edgeToJson(const geometry::EdgeSignature& edge) {
    Json json = Json::object();
    json["curve"] = std::string{nameOf(kEdgeCurves, edge.curve)};
    if (edge.curve == EdgeCurve::Circle) {
        json["center"] = pointToJson(edge.point);
        json["axis"] = directionToJson(edge.direction);
        json["radius"] = edge.radius.si();
    } else {
        json["point"] = pointToJson(edge.point);
        json["direction"] = directionToJson(edge.direction);
    }
    return json;
}

/// A line reference has a point and a direction; a circle reference a
/// centre, an axis and a radius. The signature is kept as written.
Result<geometry::EdgeSignature> edgeFromJson(const Json& value, std::string_view path) {
    if (auto object = requireObject(value, path, {"curve", "point", "direction", "center", "axis", "radius"});
        !object) {
        return std::unexpected(object.error());
    }
    auto curve = valueOf(kEdgeCurves, value, "curve", path);
    if (!curve) {
        return std::unexpected(curve.error());
    }
    const bool circle = *curve == EdgeCurve::Circle;
    for (const std::string_view key : {"point", "direction", "center", "axis", "radius"}) {
        const bool circleKey = key == "center" || key == "axis" || key == "radius";
        if (circleKey != circle && value.contains(key)) {
            return parseError(childPath(path, key),
                              std::format("a {} reference has no {}", circle ? "circle" : "line", key));
        }
    }
    geometry::EdgeSignature edge{.curve = *curve};
    auto point = pointFromJson(value, circle ? "center" : "point", path);
    auto direction = directionFromJson(value, circle ? "axis" : "direction", path);
    if (!point || !direction) {
        return std::unexpected(!point ? point.error() : direction.error());
    }
    edge.point = *point;
    edge.direction = *direction;
    if (circle) {
        auto radius = readNumber(value, "radius", path);
        if (!radius) {
            return std::unexpected(radius.error());
        }
        edge.radius = Length::fromSi(*radius);
    }
    if (auto valid = geometry::validate(edge); !valid) {
        return atPath(path, valid.error());
    }
    return edge;
}

namespace {

Json edgeListToJson(const std::vector<geometry::EdgeSignature>& edges) {
    Json json = Json::array();
    for (const geometry::EdgeSignature& edge : edges) {
        json.push_back(edgeToJson(edge));
    }
    return json;
}

/// The edge references in @p array (already known to be an array) at @p path.
Result<std::vector<geometry::EdgeSignature>> edgeListFromJson(const Json& array, std::string_view path) {
    std::vector<geometry::EdgeSignature> edges;
    for (std::size_t i = 0; i < array.size(); ++i) {
        auto edge = edgeFromJson(array[i], indexPath(path, i));
        if (!edge) {
            return std::unexpected(edge.error());
        }
        edges.push_back(*edge);
    }
    return edges;
}

/// An absent key reads as 0.
Result<double> readNumberOrZero(const Json& object, std::string_view key, std::string_view path) {
    return object.contains(key) ? readNumber(object, key, path) : Result<double>{0.0};
}

} // namespace

Json chamferToJson(const features::ChamferFeature& feature) {
    const features::ChamferDefinition& d = feature.definition();
    Json json = Json::object();
    json["target"] = d.target.value();
    json["edges"] = edgeListToJson(d.edges);
    json["mode"] = std::string{nameOf(kChamferModes, d.mode)};
    json["distance"] = d.distance.si();
    if (d.distanceParameter) {
        json["distance_parameter"] = d.distanceParameter->value();
    }
    // Fields a mode does not use are zero (the definition's invariant).
    if (d.mode == ChamferMode::TwoDistance) {
        json["distance2"] = d.distance2.si();
    }
    if (d.mode == ChamferMode::DistanceAngle) {
        json["angle"] = d.angle.si();
    }
    if (d.referenceSide) {
        json["reference_side"] = directionToJson(*d.referenceSide);
    }
    return json;
}

Result<std::unique_ptr<features::ChamferFeature>> chamferFromJson(const Json& data, std::string name,
                                                                  std::string_view path) {
    if (auto object = requireObject(data, path,
                                    {"target", "edges", "mode", "distance", "distance_parameter", "distance2",
                                     "angle", "reference_side"});
        !object) {
        return std::unexpected(object.error());
    }
    auto target = readId(data, "target", path);
    auto edgesField = requireArray(data, "edges", path);
    auto mode = valueOf(kChamferModes, data, "mode", path);
    auto distance = readNumber(data, "distance", path);
    auto distanceParameter = readOptionalId(data, "distance_parameter", path);
    auto distance2 = readNumberOrZero(data, "distance2", path);
    auto angle = readNumberOrZero(data, "angle", path);
    if (!target || !edgesField || !mode || !distance || !distanceParameter || !distance2 || !angle) {
        const Error& error = !target ? target.error()
                           : !edgesField ? edgesField.error()
                           : !mode ? mode.error()
                           : !distance ? distance.error()
                           : !distanceParameter ? distanceParameter.error()
                           : !distance2 ? distance2.error()
                                        : angle.error();
        return std::unexpected(error);
    }
    features::ChamferDefinition definition{
        .target = FeatureId::fromValue(*target),
        .edges = {},
        .mode = *mode,
        .distance = Length::fromSi(*distance),
        .distanceParameter = std::nullopt,
        .distance2 = Length::fromSi(*distance2),
        .angle = Angle::fromSi(*angle),
        .referenceSide = std::nullopt,
    };
    auto edges = edgeListFromJson(**edgesField, childPath(path, "edges"));
    if (!edges) {
        return std::unexpected(edges.error());
    }
    definition.edges = std::move(*edges);
    if (*distanceParameter) {
        definition.distanceParameter = ParameterId::fromValue(**distanceParameter);
    }
    if (data.contains("reference_side")) {
        auto side = directionFromJson(data, "reference_side", path);
        if (!side) {
            return std::unexpected(side.error());
        }
        definition.referenceSide = *side;
    }
    auto feature = features::ChamferFeature::create(std::move(name), definition);
    if (!feature) {
        return atPath(path, feature.error());
    }
    return std::move(*feature);
}

namespace {

using geometry::FaceSurface;
using geometry::HoleExtent;
using geometry::HoleType;

// Only surfaces that can be referenced; see geometry::validate(FaceSignature).
constexpr std::array<std::pair<FaceSurface, std::string_view>, 1> kFaceSurfaces{{
    {FaceSurface::Plane, "plane"},
}};

constexpr std::array<std::pair<HoleType, std::string_view>, 4> kHoleTypes{{
    {HoleType::Simple, "simple"},
    {HoleType::Counterbore, "counterbore"},
    {HoleType::Countersink, "countersink"},
    {HoleType::Spotface, "spotface"},
}};

constexpr std::array<std::pair<HoleExtent, std::string_view>, 2> kHoleExtents{{
    {HoleExtent::Through, "through"},
    {HoleExtent::Blind, "blind"},
}};

Json faceToJson(const geometry::FaceSignature& face) {
    Json json = Json::object();
    json["surface"] = std::string{nameOf(kFaceSurfaces, face.surface)};
    json["point"] = pointToJson(face.point);
    json["normal"] = directionToJson(face.normal);
    return json;
}

/// A plane reference: its surface, a point on it and its outward normal,
/// kept as written.
Result<geometry::FaceSignature> faceFromJson(const Json& data, std::string_view key, std::string_view path) {
    auto field = requireField(data, key, path);
    if (!field) {
        return std::unexpected(field.error());
    }
    const std::string facePath = childPath(path, key);
    if (auto object = requireObject(**field, facePath, {"surface", "point", "normal"}); !object) {
        return std::unexpected(object.error());
    }
    auto surface = valueOf(kFaceSurfaces, **field, "surface", facePath);
    auto point = pointFromJson(**field, "point", facePath);
    auto normal = directionFromJson(**field, "normal", facePath);
    if (!surface || !point || !normal) {
        return std::unexpected(!surface ? surface.error() : !point ? point.error() : normal.error());
    }
    const geometry::FaceSignature face{.surface = *surface, .point = *point, .normal = *normal};
    if (auto valid = geometry::validate(face); !valid) {
        return atPath(facePath, valid.error());
    }
    return face;
}

} // namespace

Json holeToJson(const features::HoleFeature& feature) {
    const features::HoleDefinition& d = feature.definition();
    Json json = Json::object();
    json["target"] = d.target.value();
    json["face"] = faceToJson(d.face);
    json["center"] = Json::array({d.center.x.si(), d.center.y.si()});
    if (d.centerUParameter) {
        json["center_u_parameter"] = d.centerUParameter->value();
    }
    if (d.centerVParameter) {
        json["center_v_parameter"] = d.centerVParameter->value();
    }
    json["type"] = std::string{nameOf(kHoleTypes, d.type)};
    json["extent"] = std::string{nameOf(kHoleExtents, d.extent)};
    // A threaded or standard clearance hole's diameter is its standard's
    // (P12-HOLE-001).
    if (!d.thread && !d.clearance) {
        json["diameter"] = d.diameter.si();
    }
    if (d.diameterParameter) {
        json["diameter_parameter"] = d.diameterParameter->value();
    }
    // Fields a type or extent does not use are zero (the definition's invariant).
    if (d.extent == HoleExtent::Blind) {
        json["depth"] = d.depth.si();
        if (d.depthParameter) {
            json["depth_parameter"] = d.depthParameter->value();
        }
    }
    if (d.type == HoleType::Counterbore) {
        json["counterbore_diameter"] = d.counterboreDiameter.si();
        json["counterbore_depth"] = d.counterboreDepth.si();
    }
    if (d.type == HoleType::Countersink) {
        json["countersink_diameter"] = d.countersinkDiameter.si();
        json["countersink_angle"] = d.countersinkAngle.si();
    }
    if (d.type == HoleType::Spotface) {
        json["spotface_diameter"] = d.spotfaceDiameter.si();
        json["spotface_depth"] = d.spotfaceDepth.si();
    }
    if (d.thread) {
        json["thread"] = holeThreadToJson(*d.thread);
    }
    if (d.clearance) {
        json["clearance"] = holeClearanceToJson(*d.clearance);
    }
    if (d.tolerance) {
        json["tolerance"] = standards::toString(*d.tolerance);
    }
    return json;
}

Result<std::unique_ptr<features::HoleFeature>> holeFromJson(const Json& data, std::string name,
                                                            std::string_view path) {
    if (auto object = requireObject(data, path,
                                    {"target", "face", "center", "center_u_parameter", "center_v_parameter", "type",
                                     "extent", "diameter", "diameter_parameter", "depth", "depth_parameter",
                                     "counterbore_diameter", "counterbore_depth", "countersink_diameter",
                                     "countersink_angle", "spotface_diameter", "spotface_depth", "thread",
                                     "clearance", "tolerance"});
        !object) {
        return std::unexpected(object.error());
    }
    auto target = readId(data, "target", path);
    auto face = faceFromJson(data, "face", path);
    auto center = readNumbers(data, "center", path, 2);
    auto centerU = readOptionalId(data, "center_u_parameter", path);
    auto centerV = readOptionalId(data, "center_v_parameter", path);
    auto type = valueOf(kHoleTypes, data, "type", path);
    auto extent = valueOf(kHoleExtents, data, "extent", path);
    // A threaded or standard clearance hole has no diameter (P12-HOLE-001).
    const bool standardDiameter = data.contains("thread") || data.contains("clearance");
    auto diameter = standardDiameter ? readNumberOrZero(data, "diameter", path) : readNumber(data, "diameter", path);
    auto diameterParameter = readOptionalId(data, "diameter_parameter", path);
    auto depth = readNumberOrZero(data, "depth", path);
    auto depthParameter = readOptionalId(data, "depth_parameter", path);
    auto counterboreDiameter = readNumberOrZero(data, "counterbore_diameter", path);
    auto counterboreDepth = readNumberOrZero(data, "counterbore_depth", path);
    auto countersinkDiameter = readNumberOrZero(data, "countersink_diameter", path);
    auto countersinkAngle = readNumberOrZero(data, "countersink_angle", path);
    auto spotfaceDiameter = readNumberOrZero(data, "spotface_diameter", path);
    auto spotfaceDepth = readNumberOrZero(data, "spotface_depth", path);
    for (const Error* error :
         {!target ? &target.error() : nullptr, !face ? &face.error() : nullptr, !center ? &center.error() : nullptr,
          !centerU ? &centerU.error() : nullptr, !centerV ? &centerV.error() : nullptr,
          !type ? &type.error() : nullptr, !extent ? &extent.error() : nullptr,
          !diameter ? &diameter.error() : nullptr, !diameterParameter ? &diameterParameter.error() : nullptr,
          !depth ? &depth.error() : nullptr, !depthParameter ? &depthParameter.error() : nullptr,
          !counterboreDiameter ? &counterboreDiameter.error() : nullptr,
          !counterboreDepth ? &counterboreDepth.error() : nullptr,
          !countersinkDiameter ? &countersinkDiameter.error() : nullptr,
          !countersinkAngle ? &countersinkAngle.error() : nullptr,
          !spotfaceDiameter ? &spotfaceDiameter.error() : nullptr,
          !spotfaceDepth ? &spotfaceDepth.error() : nullptr}) {
        if (error != nullptr) {
            return std::unexpected(*error);
        }
    }
    std::optional<features::HoleThread> thread;
    if (data.contains("thread")) {
        auto read = holeThreadFromJson(data.at("thread"), childPath(path, "thread"));
        if (!read) {
            return std::unexpected(read.error());
        }
        thread = *read;
    }
    std::optional<features::HoleClearance> clearance;
    if (data.contains("clearance")) {
        auto read = holeClearanceFromJson(data.at("clearance"), childPath(path, "clearance"));
        if (!read) {
            return std::unexpected(read.error());
        }
        clearance = *read;
    }
    std::optional<standards::HoleToleranceClass> tolerance;
    if (data.contains("tolerance")) {
        auto read = holeToleranceFromJson(data, "tolerance", path);
        if (!read) {
            return std::unexpected(read.error());
        }
        tolerance = *read;
    }
    const auto optionalParameter = [](const std::optional<std::uint64_t>& id) -> std::optional<ParameterId> {
        return id ? std::optional<ParameterId>{ParameterId::fromValue(*id)} : std::nullopt;
    };
    const features::HoleDefinition definition{
        .target = FeatureId::fromValue(*target),
        .face = *face,
        .center = Point2D{Length::fromSi((*center)[0]), Length::fromSi((*center)[1])},
        .centerUParameter = optionalParameter(*centerU),
        .centerVParameter = optionalParameter(*centerV),
        .type = *type,
        .extent = *extent,
        .diameter = Length::fromSi(*diameter),
        .diameterParameter = optionalParameter(*diameterParameter),
        .depth = Length::fromSi(*depth),
        .depthParameter = optionalParameter(*depthParameter),
        .counterboreDiameter = Length::fromSi(*counterboreDiameter),
        .counterboreDepth = Length::fromSi(*counterboreDepth),
        .countersinkDiameter = Length::fromSi(*countersinkDiameter),
        .countersinkAngle = Angle::fromSi(*countersinkAngle),
        .spotfaceDiameter = Length::fromSi(*spotfaceDiameter),
        .spotfaceDepth = Length::fromSi(*spotfaceDepth),
        .thread = thread,
        .clearance = clearance,
        .tolerance = tolerance,
    };
    auto feature = features::HoleFeature::create(std::move(name), definition);
    if (!feature) {
        return atPath(path, feature.error());
    }
    return std::move(*feature);
}

Json filletToJson(const features::FilletFeature& feature) {
    const features::FilletDefinition& d = feature.definition();
    Json json = Json::object();
    json["target"] = d.target.value();
    json["edges"] = edgeListToJson(d.edges);
    json["radius"] = d.radius.si();
    if (d.radiusParameter) {
        json["radius_parameter"] = d.radiusParameter->value();
    }
    return json;
}

Result<std::unique_ptr<features::FilletFeature>> filletFromJson(const Json& data, std::string name,
                                                                std::string_view path) {
    if (auto object = requireObject(data, path, {"target", "edges", "radius", "radius_parameter"}); !object) {
        return std::unexpected(object.error());
    }
    auto target = readId(data, "target", path);
    auto edgesField = requireArray(data, "edges", path);
    auto radius = readNumber(data, "radius", path);
    auto radiusParameter = readOptionalId(data, "radius_parameter", path);
    if (!target || !edgesField || !radius || !radiusParameter) {
        const Error& error = !target ? target.error()
                           : !edgesField ? edgesField.error()
                           : !radius ? radius.error()
                                     : radiusParameter.error();
        return std::unexpected(error);
    }
    auto edges = edgeListFromJson(**edgesField, childPath(path, "edges"));
    if (!edges) {
        return std::unexpected(edges.error());
    }
    features::FilletDefinition definition{
        .target = FeatureId::fromValue(*target),
        .edges = std::move(*edges),
        .radius = Length::fromSi(*radius),
        .radiusParameter = std::nullopt,
    };
    if (*radiusParameter) {
        definition.radiusParameter = ParameterId::fromValue(**radiusParameter);
    }
    auto feature = features::FilletFeature::create(std::move(name), definition);
    if (!feature) {
        return atPath(path, feature.error());
    }
    return std::move(*feature);
}

namespace {

/// Absent key gives @p fallback; a present key must hold a boolean. The
/// pattern fields added in P12-PATTERN-001 are written only when they are
/// not their default, so a file written before them still reads as one.
Result<bool> readOptionalBool(const Json& object, std::string_view key, std::string_view path, bool fallback) {
    if (!object.contains(key)) {
        return fallback;
    }
    return readBool(object, key, path);
}

/// Absent key gives an empty list; a present key must hold an array of
/// instance indices. The definition checks the indices themselves.
Result<std::vector<std::uint32_t>> readInstanceIndices(const Json& object, std::string_view key,
                                                       std::string_view path) {
    std::vector<std::uint32_t> indices;
    if (!object.contains(key)) {
        return indices;
    }
    auto array = requireArray(object, key, path);
    if (!array) {
        return std::unexpected(array.error());
    }
    const std::string arrayPath = childPath(path, key);
    indices.reserve((*array)->size());
    for (std::size_t i = 0; i < (*array)->size(); ++i) {
        const Json& element = (**array)[i];
        if (!element.is_number_unsigned()) {
            return parseError(indexPath(arrayPath, i), "expected a non-negative integer");
        }
        const std::uint64_t value = element.get<std::uint64_t>();
        if (value > std::numeric_limits<std::uint32_t>::max()) {
            return parseError(indexPath(arrayPath, i),
                              std::format("expected at most {}", std::numeric_limits<std::uint32_t>::max()));
        }
        indices.push_back(static_cast<std::uint32_t>(value));
    }
    return indices;
}

/// How a direction spreads its instances (P12-PATTERN-001).
constexpr std::array<std::pair<features::PatternDistribution, std::string_view>, 2> kPatternDistributions{{
    {features::PatternDistribution::Spacing, "spacing"},
    {features::PatternDistribution::TotalLength, "total_length"},
}};

Json patternDirectionToJson(const features::PatternDirection& d) {
    Json json = Json::object();
    json["direction"] = Json::array({d.direction.x, d.direction.y, d.direction.z});
    json["count"] = d.count;
    if (d.countParameter) {
        json["count_parameter"] = d.countParameter->value();
    }
    json["spacing"] = d.spacing.si();
    if (d.spacingParameter) {
        json["spacing_parameter"] = d.spacingParameter->value();
    }
    // Written only when they are not the default, so that a pattern from
    // before P12-PATTERN-001 is written back exactly as it was read.
    if (d.distribution != features::PatternDistribution::Spacing) {
        json["distribution"] = std::string{nameOf(kPatternDistributions, d.distribution)};
    }
    if (d.symmetric) {
        json["symmetric"] = true;
    }
    return json;
}

/// The vector is kept as written; the definition checks it.
Result<features::PatternDirection> patternDirectionFromJson(const Json& data, std::string_view key,
                                                           std::string_view path) {
    auto field = requireField(data, key, path);
    if (!field) {
        return std::unexpected(field.error());
    }
    const std::string directionPath = childPath(path, key);
    if (auto object = requireObject(**field, directionPath,
                                    {"direction", "count", "count_parameter", "spacing", "spacing_parameter",
                                     "distribution", "symmetric"});
        !object) {
        return std::unexpected(object.error());
    }
    auto vector = readNumbers(**field, "direction", directionPath, 3);
    auto count = readUnsigned(**field, "count", directionPath);
    auto countParameter = readOptionalId(**field, "count_parameter", directionPath);
    auto spacing = readNumber(**field, "spacing", directionPath);
    auto spacingParameter = readOptionalId(**field, "spacing_parameter", directionPath);
    auto symmetric = readOptionalBool(**field, "symmetric", directionPath, false);
    auto distribution = Result<features::PatternDistribution>{features::PatternDistribution::Spacing};
    if ((*field)->contains("distribution")) {
        distribution = valueOf(kPatternDistributions, **field, "distribution", directionPath);
    }
    if (!vector || !count || !countParameter || !spacing || !spacingParameter || !symmetric || !distribution) {
        const Error& error = !vector ? vector.error()
                           : !count ? count.error()
                           : !countParameter ? countParameter.error()
                           : !spacing ? spacing.error()
                           : !spacingParameter ? spacingParameter.error()
                           : !symmetric ? symmetric.error()
                                        : distribution.error();
        return std::unexpected(error);
    }
    if (*count > std::numeric_limits<std::uint32_t>::max()) {
        return parseError(childPath(directionPath, "count"),
                          std::format("expected at most {}", std::numeric_limits<std::uint32_t>::max()));
    }
    features::PatternDirection direction{
        .direction = Vector3D{(*vector)[0], (*vector)[1], (*vector)[2]},
        .count = static_cast<std::uint32_t>(*count),
        .countParameter = std::nullopt,
        .spacing = Length::fromSi(*spacing),
        .spacingParameter = std::nullopt,
        .distribution = *distribution,
        .symmetric = *symmetric,
    };
    if (*countParameter) {
        direction.countParameter = ParameterId::fromValue(**countParameter);
    }
    if (*spacingParameter) {
        direction.spacingParameter = ParameterId::fromValue(**spacingParameter);
    }
    return direction;
}

} // namespace

Json linearPatternToJson(const features::LinearPatternFeature& feature) {
    const features::LinearPatternDefinition& d = feature.definition();
    Json json = Json::object();
    json["source"] = d.source.value();
    json["first"] = patternDirectionToJson(d.first);
    if (d.second) {
        json["second"] = patternDirectionToJson(*d.second);
    }
    if (!d.suppressed.empty()) {
        json["suppressed"] = d.suppressed;
    }
    return json;
}

Result<std::unique_ptr<features::LinearPatternFeature>> linearPatternFromJson(const Json& data, std::string name,
                                                                              std::string_view path) {
    if (auto object = requireObject(data, path, {"source", "first", "second", "suppressed"}); !object) {
        return std::unexpected(object.error());
    }
    auto source = readId(data, "source", path);
    if (!source) {
        return std::unexpected(source.error());
    }
    auto first = patternDirectionFromJson(data, "first", path);
    if (!first) {
        return std::unexpected(first.error());
    }
    auto suppressed = readInstanceIndices(data, "suppressed", path);
    if (!suppressed) {
        return std::unexpected(suppressed.error());
    }
    features::LinearPatternDefinition definition{.source = FeatureId::fromValue(*source),
                                                 .first = *first,
                                                 .second = std::nullopt,
                                                 .suppressed = std::move(*suppressed)};
    if (data.contains("second")) {
        auto second = patternDirectionFromJson(data, "second", path);
        if (!second) {
            return std::unexpected(second.error());
        }
        definition.second = *second;
    }
    auto feature = features::LinearPatternFeature::create(std::move(name), definition);
    if (!feature) {
        return atPath(path, feature.error());
    }
    return std::move(*feature);
}

namespace {

using features::CircularSpacing;
using features::RotationDirection;

constexpr std::array<std::pair<CircularSpacing, std::string_view>, 3> kCircularSpacings{{
    {CircularSpacing::FullCircle, "full_circle"},
    {CircularSpacing::IncludedAngle, "included_angle"},
    {CircularSpacing::AngleStep, "angle_step"},
}};

constexpr std::array<std::pair<RotationDirection, std::string_view>, 2> kRotationDirections{{
    {RotationDirection::Positive, "positive"},
    {RotationDirection::Negative, "negative"},
}};

} // namespace

Json circularPatternToJson(const features::CircularPatternFeature& feature) {
    const features::CircularPatternDefinition& d = feature.definition();
    Json json = Json::object();
    json["source"] = d.source.value();
    Json axis = Json::object();
    if (d.axis.reference) {
        axis["reference"] = axisReferenceToJson(*d.axis.reference);
    } else {
        axis["origin"] = pointToJson(d.axis.origin);
        axis["direction"] = Json::array({d.axis.direction.x, d.axis.direction.y, d.axis.direction.z});
    }
    json["axis"] = std::move(axis);
    json["count"] = d.count;
    if (d.countParameter) {
        json["count_parameter"] = d.countParameter->value();
    }
    json["spacing"] = std::string{nameOf(kCircularSpacings, d.spacing)};
    // A full circle has no angle (the definition's invariant).
    if (d.spacing != CircularSpacing::FullCircle) {
        json["angle"] = d.angle.si();
        if (d.angleParameter) {
            json["angle_parameter"] = d.angleParameter->value();
        }
    }
    json["rotation"] = std::string{nameOf(kRotationDirections, d.direction)};
    // Written only when they are not the default (P12-PATTERN-001).
    if (d.symmetric) {
        json["symmetric"] = true;
    }
    if (!d.suppressed.empty()) {
        json["suppressed"] = d.suppressed;
    }
    return json;
}

Result<std::unique_ptr<features::CircularPatternFeature>> circularPatternFromJson(const Json& data, std::string name,
                                                                                  std::string_view path) {
    if (auto object = requireObject(data, path,
                                    {"source", "axis", "count", "count_parameter", "spacing", "angle",
                                     "angle_parameter", "rotation", "symmetric", "suppressed"});
        !object) {
        return std::unexpected(object.error());
    }
    auto source = readId(data, "source", path);
    auto axisField = requireField(data, "axis", path);
    if (!source || !axisField) {
        return std::unexpected(!source ? source.error() : axisField.error());
    }
    const std::string axisPath = childPath(path, "axis");
    if (auto object = requireObject(**axisField, axisPath, {"origin", "direction", "reference"}); !object) {
        return std::unexpected(object.error());
    }
    // An axis is a reference (P12-DATUM-001) or a line of its own.
    std::optional<AxisReference> reference;
    Result<Point3D> origin = Point3D{};
    Result<std::vector<double>> direction = std::vector<double>{0.0, 0.0, 1.0};
    if ((*axisField)->contains("reference")) {
        if ((*axisField)->contains("origin") || (*axisField)->contains("direction")) {
            return parseError(axisPath, "a pattern axis given by a reference has no origin or direction");
        }
        auto parsed = axisReferenceFromJson((**axisField)["reference"], childPath(axisPath, "reference"));
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        reference = *parsed;
    } else {
        origin = pointFromJson(**axisField, "origin", axisPath);
        direction = readNumbers(**axisField, "direction", axisPath, 3);
    }
    auto count = readUnsigned(data, "count", path);
    auto countParameter = readOptionalId(data, "count_parameter", path);
    auto spacing = valueOf(kCircularSpacings, data, "spacing", path);
    auto angle = readNumberOrZero(data, "angle", path);
    auto angleParameter = readOptionalId(data, "angle_parameter", path);
    auto rotation = valueOf(kRotationDirections, data, "rotation", path);
    auto symmetric = readOptionalBool(data, "symmetric", path, false);
    auto suppressed = readInstanceIndices(data, "suppressed", path);
    for (const Error* error :
         {!origin ? &origin.error() : nullptr, !direction ? &direction.error() : nullptr,
          !count ? &count.error() : nullptr, !countParameter ? &countParameter.error() : nullptr,
          !spacing ? &spacing.error() : nullptr, !angle ? &angle.error() : nullptr,
          !angleParameter ? &angleParameter.error() : nullptr, !rotation ? &rotation.error() : nullptr,
          !symmetric ? &symmetric.error() : nullptr, !suppressed ? &suppressed.error() : nullptr}) {
        if (error != nullptr) {
            return std::unexpected(*error);
        }
    }
    if (*count > std::numeric_limits<std::uint32_t>::max()) {
        return parseError(childPath(path, "count"),
                          std::format("expected at most {}", std::numeric_limits<std::uint32_t>::max()));
    }
    const auto optionalParameter = [](const std::optional<std::uint64_t>& id) -> std::optional<ParameterId> {
        return id ? std::optional<ParameterId>{ParameterId::fromValue(*id)} : std::nullopt;
    };
    const features::CircularPatternDefinition definition{
        .source = FeatureId::fromValue(*source),
        .axis = {.origin = *origin,
                 .direction = Vector3D{(*direction)[0], (*direction)[1], (*direction)[2]},
                 .reference = reference},
        .count = static_cast<std::uint32_t>(*count),
        .countParameter = optionalParameter(*countParameter),
        .spacing = *spacing,
        .angle = Angle::fromSi(*angle),
        .angleParameter = optionalParameter(*angleParameter),
        .direction = *rotation,
        .symmetric = *symmetric,
        .suppressed = std::move(*suppressed),
    };
    auto feature = features::CircularPatternFeature::create(std::move(name), definition);
    if (!feature) {
        return atPath(path, feature.error());
    }
    return std::move(*feature);
}

namespace {

using features::MirrorScope;

constexpr std::array<std::pair<MirrorScope, std::string_view>, 2> kMirrorScopes{{
    {MirrorScope::Feature, "feature"},
    {MirrorScope::Body, "body"},
}};

} // namespace

Json mirrorToJson(const features::MirrorFeature& feature) {
    const features::MirrorDefinition& d = feature.definition();
    Json json = Json::object();
    json["source"] = d.source.value();
    Json plane = Json::object();
    if (d.plane.reference) {
        plane["reference"] = planeReferenceToJson(*d.plane.reference);
    } else {
        plane["origin"] = pointToJson(d.plane.origin);
        plane["normal"] = Json::array({d.plane.normal.x, d.plane.normal.y, d.plane.normal.z});
    }
    plane["offset"] = d.plane.offset.si();
    if (d.plane.offsetParameter) {
        plane["offset_parameter"] = d.plane.offsetParameter->value();
    }
    json["plane"] = std::move(plane);
    json["scope"] = std::string{nameOf(kMirrorScopes, d.scope)};
    json["keep_original"] = d.keepOriginal;
    return json;
}

Result<std::unique_ptr<features::MirrorFeature>> mirrorFromJson(const Json& data, std::string name,
                                                                std::string_view path) {
    if (auto object = requireObject(data, path, {"source", "plane", "scope", "keep_original"}); !object) {
        return std::unexpected(object.error());
    }
    auto source = readId(data, "source", path);
    auto planeField = requireField(data, "plane", path);
    if (!source || !planeField) {
        return std::unexpected(!source ? source.error() : planeField.error());
    }
    const std::string planePath = childPath(path, "plane");
    if (auto object = requireObject(**planeField, planePath,
                                    {"origin", "normal", "offset", "offset_parameter", "reference"});
        !object) {
        return std::unexpected(object.error());
    }
    // A plane is a reference (P12-DATUM-001) or a plane of its own.
    std::optional<PlaneReference> reference;
    Result<Point3D> origin = Point3D{};
    Result<std::vector<double>> normal = std::vector<double>{1.0, 0.0, 0.0};
    if ((*planeField)->contains("reference")) {
        if ((*planeField)->contains("origin") || (*planeField)->contains("normal")) {
            return parseError(planePath, "a mirror plane given by a reference has no origin or normal");
        }
        auto parsed = planeReferenceFromJson((**planeField)["reference"], childPath(planePath, "reference"));
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        reference = *parsed;
    } else {
        origin = pointFromJson(**planeField, "origin", planePath);
        normal = readNumbers(**planeField, "normal", planePath, 3);
    }
    auto offset = readNumber(**planeField, "offset", planePath);
    auto offsetParameter = readOptionalId(**planeField, "offset_parameter", planePath);
    auto scope = valueOf(kMirrorScopes, data, "scope", path);
    auto keepOriginal = readBool(data, "keep_original", path);
    for (const Error* error :
         {!origin ? &origin.error() : nullptr, !normal ? &normal.error() : nullptr, !offset ? &offset.error() : nullptr,
          !offsetParameter ? &offsetParameter.error() : nullptr, !scope ? &scope.error() : nullptr,
          !keepOriginal ? &keepOriginal.error() : nullptr}) {
        if (error != nullptr) {
            return std::unexpected(*error);
        }
    }
    const features::MirrorDefinition definition{
        .source = FeatureId::fromValue(*source),
        .plane = {.origin = *origin,
                  .normal = Vector3D{(*normal)[0], (*normal)[1], (*normal)[2]},
                  .offset = Length::fromSi(*offset),
                  .offsetParameter = *offsetParameter ? std::optional<ParameterId>{ParameterId::fromValue(**offsetParameter)}
                                                      : std::nullopt,
                  .reference = reference},
        .scope = *scope,
        .keepOriginal = *keepOriginal,
    };
    auto feature = features::MirrorFeature::create(std::move(name), definition);
    if (!feature) {
        return atPath(path, feature.error());
    }
    return std::move(*feature);
}

namespace {

using features::LoftInterpolation;

constexpr std::array<std::pair<LoftInterpolation, std::string_view>, 1> kLoftInterpolations{{
    {LoftInterpolation::Ruled, "ruled"},
}};

Result<features::LoftSection> loftSectionFromJson(const Json& value, std::string_view path) {
    if (auto object = requireObject(value, path, {"sketch", "offset", "offset_parameter"}); !object) {
        return std::unexpected(object.error());
    }
    auto sketch = readId(value, "sketch", path);
    auto offset = readNumber(value, "offset", path);
    auto offsetParameter = readOptionalId(value, "offset_parameter", path);
    for (const Error* error : {!sketch ? &sketch.error() : nullptr, !offset ? &offset.error() : nullptr,
                               !offsetParameter ? &offsetParameter.error() : nullptr}) {
        if (error != nullptr) {
            return std::unexpected(*error);
        }
    }
    return features::LoftSection{
        .sketch = SketchId::fromValue(*sketch),
        .offset = Length::fromSi(*offset),
        .offsetParameter = *offsetParameter ? std::optional<ParameterId>{ParameterId::fromValue(**offsetParameter)}
                                            : std::nullopt,
    };
}

} // namespace

Json loftToJson(const features::LoftFeature& feature) {
    const features::LoftDefinition& d = feature.definition();
    Json json = Json::object();
    Json sections = Json::array();
    for (const features::LoftSection& section : d.sections) {
        Json entry = Json::object();
        entry["sketch"] = section.sketch.value();
        entry["offset"] = section.offset.si();
        if (section.offsetParameter) {
            entry["offset_parameter"] = section.offsetParameter->value();
        }
        sections.push_back(std::move(entry));
    }
    json["sections"] = std::move(sections);
    json["interpolation"] = std::string{nameOf(kLoftInterpolations, d.interpolation)};
    json["operation"] = std::string{nameOf(kOperations, d.operation)};
    if (d.target) {
        json["target"] = d.target->value();
    }
    return json;
}

Result<std::unique_ptr<features::LoftFeature>> loftFromJson(const Json& data, std::string name,
                                                            std::string_view path) {
    if (auto object = requireObject(data, path, {"sections", "interpolation", "operation", "target"}); !object) {
        return std::unexpected(object.error());
    }
    auto sectionsField = requireArray(data, "sections", path);
    if (!sectionsField) {
        return std::unexpected(sectionsField.error());
    }
    const std::string sectionsPath = childPath(path, "sections");
    std::vector<features::LoftSection> sections;
    for (std::size_t i = 0; i < (*sectionsField)->size(); ++i) {
        auto section = loftSectionFromJson((**sectionsField)[i], indexPath(sectionsPath, i));
        if (!section) {
            return std::unexpected(section.error());
        }
        sections.push_back(*section);
    }
    auto interpolation = valueOf(kLoftInterpolations, data, "interpolation", path);
    auto operation = valueOf(kOperations, data, "operation", path);
    auto target = readOptionalId(data, "target", path);
    for (const Error* error : {!interpolation ? &interpolation.error() : nullptr,
                               !operation ? &operation.error() : nullptr, !target ? &target.error() : nullptr}) {
        if (error != nullptr) {
            return std::unexpected(*error);
        }
    }
    const features::LoftDefinition definition{
        .sections = std::move(sections),
        .interpolation = *interpolation,
        .operation = *operation,
        .target = *target ? std::optional<FeatureId>{FeatureId::fromValue(**target)} : std::nullopt,
    };
    auto feature = features::LoftFeature::create(std::move(name), definition);
    if (!feature) {
        return atPath(path, feature.error());
    }
    return std::move(*feature);
}

} // namespace bettercad::io::detail
