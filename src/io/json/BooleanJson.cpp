#include "io/json/ObjectJson.hpp"

#include <array>
#include <format>
#include <string>
#include <utility>
#include <vector>

// Split and combine features (P12-FEAT-002).
namespace bettercad::io::detail {

namespace {

constexpr std::array<std::pair<geometry::SplitKeep, std::string_view>, 3> kKeeps{{
    {geometry::SplitKeep::Front, "front"},
    {geometry::SplitKeep::Back, "back"},
    {geometry::SplitKeep::Both, "both"},
}};
constexpr std::array<std::pair<features::FeatureOperation, std::string_view>, 3> kCombineOperations{{
    {features::FeatureOperation::Join, "join"},
    {features::FeatureOperation::Cut, "cut"},
    {features::FeatureOperation::Intersect, "intersect"},
}};

template <typename Enum, std::size_t N>
std::string nameIn(const std::array<std::pair<Enum, std::string_view>, N>& table, Enum value) {
    for (const auto& [item, name] : table) {
        if (item == value) {
            return std::string{name};
        }
    }
    return "unknown";
}

template <typename Enum, std::size_t N>
Result<Enum> valueIn(const std::array<std::pair<Enum, std::string_view>, N>& table, const Json& object,
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

Json splitToJson(const features::SplitFeature& feature) {
    const features::SplitDefinition& d = feature.definition();
    Json json = Json::object();
    json["target"] = d.target.value();
    json["plane"] = planeReferenceToJson(d.plane);
    json["keep"] = nameIn(kKeeps, d.keep);
    return json;
}

Result<std::unique_ptr<features::SplitFeature>> splitFromJson(const Json& data, std::string name,
                                                              std::string_view path) {
    if (auto object = requireObject(data, path, {"target", "plane", "keep"}); !object) {
        return std::unexpected(object.error());
    }
    auto target = readId(data, "target", path);
    if (!target) {
        return std::unexpected(target.error());
    }
    auto planeField = requireField(data, "plane", path);
    if (!planeField) {
        return std::unexpected(planeField.error());
    }
    auto plane = planeReferenceFromJson(**planeField, childPath(path, "plane"));
    if (!plane) {
        return std::unexpected(plane.error());
    }
    auto keep = valueIn(kKeeps, data, "keep", path);
    if (!keep) {
        return std::unexpected(keep.error());
    }
    auto feature = features::SplitFeature::create(
        std::move(name), {.target = FeatureId::fromValue(*target), .plane = std::move(*plane), .keep = *keep});
    if (!feature) {
        return atPath(path, feature.error());
    }
    return std::move(*feature);
}

Json combineToJson(const features::CombineFeature& feature) {
    const features::CombineDefinition& d = feature.definition();
    Json json = Json::object();
    json["target"] = d.target.value();
    Json tools = Json::array();
    for (const FeatureId tool : d.tools) {
        tools.push_back(tool.value());
    }
    json["tools"] = std::move(tools);
    json["operation"] = nameIn(kCombineOperations, d.operation);
    return json;
}

Result<std::unique_ptr<features::CombineFeature>> combineFromJson(const Json& data, std::string name,
                                                                  std::string_view path) {
    if (auto object = requireObject(data, path, {"target", "tools", "operation"}); !object) {
        return std::unexpected(object.error());
    }
    auto target = readId(data, "target", path);
    if (!target) {
        return std::unexpected(target.error());
    }
    auto toolsField = requireArray(data, "tools", path);
    if (!toolsField) {
        return std::unexpected(toolsField.error());
    }
    std::vector<FeatureId> tools;
    const std::string toolsPath = childPath(path, "tools");
    for (std::size_t i = 0; i < (*toolsField)->size(); ++i) {
        auto tool = readId((**toolsField)[i], indexPath(toolsPath, i));
        if (!tool) {
            return std::unexpected(tool.error());
        }
        tools.push_back(FeatureId::fromValue(*tool));
    }
    auto operation = valueIn(kCombineOperations, data, "operation", path);
    if (!operation) {
        return std::unexpected(operation.error());
    }
    auto feature = features::CombineFeature::create(
        std::move(name),
        {.target = FeatureId::fromValue(*target), .tools = std::move(tools), .operation = *operation});
    if (!feature) {
        return atPath(path, feature.error());
    }
    return std::move(*feature);
}

} // namespace bettercad::io::detail
