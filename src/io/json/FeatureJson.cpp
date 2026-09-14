#include "io/json/ObjectJson.hpp"

#include <array>
#include <format>
#include <string_view>
#include <utility>

namespace bettercad::io::detail {

namespace {

using features::ExtrudeDirection;
using features::FeatureOperation;

constexpr std::array<std::pair<ExtrudeDirection, std::string_view>, 3> kDirections{{
    {ExtrudeDirection::Normal, "normal"},
    {ExtrudeDirection::Reversed, "reversed"},
    {ExtrudeDirection::Symmetric, "symmetric"},
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

} // namespace bettercad::io::detail
