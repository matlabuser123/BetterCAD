#include "io/json/ObjectJson.hpp"

#include <format>
#include <string>
#include <utility>
#include <vector>

// Variable-radius fillet features (P12-FEAT-006).
namespace bettercad::io::detail {

namespace {

Json stationToJson(const features::VariableFilletStation& station) {
    Json json = Json::object();
    json["position"] = station.position;
    json["radius"] = station.radius.si();
    if (station.radiusParameter) {
        json["radius_parameter"] = station.radiusParameter->value();
    }
    return json;
}

Result<features::VariableFilletStation> stationFromJson(const Json& value, std::string_view path) {
    if (auto object = requireObject(value, path, {"position", "radius", "radius_parameter"}); !object) {
        return std::unexpected(object.error());
    }
    auto position = readNumber(value, "position", path);
    auto radius = readNumber(value, "radius", path);
    auto parameter = readOptionalId(value, "radius_parameter", path);
    if (!position || !radius || !parameter) {
        return std::unexpected(!position ? position.error() : !radius ? radius.error() : parameter.error());
    }
    features::VariableFilletStation station{.position = *position, .radius = Length::fromSi(*radius)};
    if (*parameter) {
        station.radiusParameter = ParameterId::fromValue(**parameter);
    }
    return station;
}

Result<features::VariableFilletEdgeDefinition> edgeEntryFromJson(const Json& value, std::string_view path) {
    if (auto object = requireObject(value, path, {"edge", "stations"}); !object) {
        return std::unexpected(object.error());
    }
    auto edgeField = requireField(value, "edge", path);
    if (!edgeField) {
        return std::unexpected(edgeField.error());
    }
    auto edge = edgeFromJson(**edgeField, childPath(path, "edge"));
    if (!edge) {
        return std::unexpected(edge.error());
    }
    auto stationsField = requireArray(value, "stations", path);
    if (!stationsField) {
        return std::unexpected(stationsField.error());
    }
    features::VariableFilletEdgeDefinition entry{.edge = *edge};
    const std::string stationsPath = childPath(path, "stations");
    for (std::size_t i = 0; i < (*stationsField)->size(); ++i) {
        auto station = stationFromJson((**stationsField)[i], indexPath(stationsPath, i));
        if (!station) {
            return std::unexpected(station.error());
        }
        entry.stations.push_back(*station);
    }
    return entry;
}

} // namespace

Json variableFilletToJson(const features::VariableFilletFeature& feature) {
    const features::VariableFilletDefinition& d = feature.definition();
    Json json = Json::object();
    json["target"] = d.target.value();
    Json edges = Json::array();
    for (const features::VariableFilletEdgeDefinition& entry : d.edges) {
        Json item = Json::object();
        item["edge"] = edgeToJson(entry.edge);
        Json stations = Json::array();
        for (const features::VariableFilletStation& station : entry.stations) {
            stations.push_back(stationToJson(station));
        }
        item["stations"] = std::move(stations);
        edges.push_back(std::move(item));
    }
    json["edges"] = std::move(edges);
    return json;
}

Result<std::unique_ptr<features::VariableFilletFeature>> variableFilletFromJson(const Json& data, std::string name,
                                                                                std::string_view path) {
    if (auto object = requireObject(data, path, {"target", "edges"}); !object) {
        return std::unexpected(object.error());
    }
    auto target = readId(data, "target", path);
    auto edgesField = requireArray(data, "edges", path);
    if (!target || !edgesField) {
        return std::unexpected(!target ? target.error() : edgesField.error());
    }
    features::VariableFilletDefinition definition{.target = FeatureId::fromValue(*target)};
    const std::string edgesPath = childPath(path, "edges");
    for (std::size_t i = 0; i < (*edgesField)->size(); ++i) {
        auto entry = edgeEntryFromJson((**edgesField)[i], indexPath(edgesPath, i));
        if (!entry) {
            return std::unexpected(entry.error());
        }
        definition.edges.push_back(std::move(*entry));
    }
    auto feature = features::VariableFilletFeature::create(std::move(name), definition);
    if (!feature) {
        return atPath(path, feature.error());
    }
    return std::move(*feature);
}

} // namespace bettercad::io::detail
