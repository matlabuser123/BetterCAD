#include "io/json/ObjectJson.hpp"

#include <array>
#include <format>
#include <string>
#include <utility>
#include <vector>

// Rib features (P12-FEAT-005).
namespace bettercad::io::detail {

namespace {

constexpr std::array<std::pair<geometry::RibPlacement, std::string_view>, 3> kPlacements{{
    {geometry::RibPlacement::Symmetric, "symmetric"},
    {geometry::RibPlacement::AlongNormal, "along_normal"},
    {geometry::RibPlacement::AgainstNormal, "against_normal"},
}};

std::string placementName(geometry::RibPlacement placement) {
    for (const auto& [item, name] : kPlacements) {
        if (item == placement) {
            return std::string{name};
        }
    }
    return "unknown";
}

Result<geometry::RibPlacement> readPlacement(const Json& object, std::string_view path) {
    auto text = readString(object, "placement", path);
    if (!text) {
        return std::unexpected(text.error());
    }
    for (const auto& [value, name] : kPlacements) {
        if (name == *text) {
            return value;
        }
    }
    return parseError(childPath(path, "placement"), std::format("unknown value '{}'", *text));
}

} // namespace

Json ribToJson(const features::RibFeature& feature) {
    const features::RibDefinition& d = feature.definition();
    Json json = Json::object();
    json["target"] = d.target.value();
    json["profile"] = d.profile.value();
    Json edges = Json::array();
    for (const EntityId edge : d.edges) {
        edges.push_back(edge.value());
    }
    json["edges"] = std::move(edges);
    json["thickness"] = d.thickness.si();
    if (d.thicknessParameter) {
        json["thickness_parameter"] = d.thicknessParameter->value();
    }
    json["placement"] = placementName(d.placement);
    json["flipped"] = d.flipped;
    return json;
}

Result<std::unique_ptr<features::RibFeature>> ribFromJson(const Json& data, std::string name, std::string_view path) {
    if (auto object = requireObject(data, path,
                                    {"target", "profile", "edges", "thickness", "thickness_parameter", "placement",
                                     "flipped"});
        !object) {
        return std::unexpected(object.error());
    }
    auto target = readId(data, "target", path);
    auto profile = readId(data, "profile", path);
    auto edgesField = requireArray(data, "edges", path);
    auto thickness = readNumber(data, "thickness", path);
    auto thicknessParameter = readOptionalId(data, "thickness_parameter", path);
    auto placement = readPlacement(data, path);
    auto flipped = readBool(data, "flipped", path);
    if (!target || !profile || !edgesField || !thickness || !thicknessParameter || !placement || !flipped) {
        const Error& error = !target               ? target.error()
                             : !profile            ? profile.error()
                             : !edgesField         ? edgesField.error()
                             : !thickness          ? thickness.error()
                             : !thicknessParameter ? thicknessParameter.error()
                             : !placement          ? placement.error()
                                                   : flipped.error();
        return std::unexpected(error);
    }
    std::vector<EntityId> edges;
    const std::string edgesPath = childPath(path, "edges");
    for (std::size_t i = 0; i < (*edgesField)->size(); ++i) {
        auto edge = readId((**edgesField)[i], indexPath(edgesPath, i));
        if (!edge) {
            return std::unexpected(edge.error());
        }
        edges.push_back(EntityId::fromValue(*edge));
    }
    features::RibDefinition definition{
        .target = FeatureId::fromValue(*target),
        .profile = SketchId::fromValue(*profile),
        .edges = std::move(edges),
        .thickness = Length::fromSi(*thickness),
        .thicknessParameter = std::nullopt,
        .placement = *placement,
        .flipped = *flipped,
    };
    if (*thicknessParameter) {
        definition.thicknessParameter = ParameterId::fromValue(**thicknessParameter);
    }
    auto feature = features::RibFeature::create(std::move(name), definition);
    if (!feature) {
        return atPath(path, feature.error());
    }
    return std::move(*feature);
}

} // namespace bettercad::io::detail
