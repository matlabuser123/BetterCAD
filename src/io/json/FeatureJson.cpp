#include "io/json/ObjectJson.hpp"

#include <array>
#include <format>
#include <string_view>
#include <utility>

namespace bettercad::io::detail {

namespace {

using features::ExtrudeDirection;
using features::FeatureOperation;
using features::RevolveAxisKind;
using features::RevolveDirection;

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
    json["depth"] = d.depth.si();
    if (d.depthParameter) {
        json["depth_parameter"] = d.depthParameter->value();
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
    if (auto object = requireObject(data, path,
                                    {"profile", "depth", "depth_parameter", "direction", "operation", "target"});
        !object) {
        return std::unexpected(object.error());
    }
    auto profile = readId(data, "profile", path);
    auto depth = readNumber(data, "depth", path);
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

} // namespace bettercad::io::detail
