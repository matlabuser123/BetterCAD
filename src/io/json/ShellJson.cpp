#include "io/json/ObjectJson.hpp"

#include <array>
#include <format>
#include <string>
#include <utility>
#include <vector>

// Shell features (P12-FEAT-003).
namespace bettercad::io::detail {

namespace {

constexpr std::array<std::pair<geometry::ShellSide, std::string_view>, 2> kSides{{
    {geometry::ShellSide::Inward, "inward"},
    {geometry::ShellSide::Outward, "outward"},
}};

std::string sideName(geometry::ShellSide side) {
    for (const auto& [item, name] : kSides) {
        if (item == side) {
            return std::string{name};
        }
    }
    return "unknown";
}

Result<geometry::ShellSide> readSide(const Json& object, std::string_view path) {
    auto text = readString(object, "side", path);
    if (!text) {
        return std::unexpected(text.error());
    }
    for (const auto& [value, name] : kSides) {
        if (name == *text) {
            return value;
        }
    }
    return parseError(childPath(path, "side"), std::format("unknown value '{}'", *text));
}

} // namespace

Json shellToJson(const features::ShellFeature& feature) {
    const features::ShellDefinition& d = feature.definition();
    Json json = Json::object();
    json["target"] = d.target.value();
    Json faces = Json::array();
    for (const FaceName& name : d.openFaces) {
        faces.push_back(faceNameToJson(name));
    }
    json["open_faces"] = std::move(faces);
    json["thickness"] = d.thickness.si();
    if (d.thicknessParameter) {
        json["thickness_parameter"] = d.thicknessParameter->value();
    }
    json["side"] = sideName(d.side);
    return json;
}

Result<std::unique_ptr<features::ShellFeature>> shellFromJson(const Json& data, std::string name,
                                                              std::string_view path) {
    if (auto object = requireObject(data, path, {"target", "open_faces", "thickness", "thickness_parameter", "side"});
        !object) {
        return std::unexpected(object.error());
    }
    auto target = readId(data, "target", path);
    auto facesField = requireArray(data, "open_faces", path);
    auto thickness = readNumber(data, "thickness", path);
    auto thicknessParameter = readOptionalId(data, "thickness_parameter", path);
    auto side = readSide(data, path);
    if (!target || !facesField || !thickness || !thicknessParameter || !side) {
        const Error& error = !target               ? target.error()
                             : !facesField         ? facesField.error()
                             : !thickness          ? thickness.error()
                             : !thicknessParameter ? thicknessParameter.error()
                                                   : side.error();
        return std::unexpected(error);
    }
    std::vector<FaceName> openFaces;
    const std::string facesPath = childPath(path, "open_faces");
    for (std::size_t i = 0; i < (*facesField)->size(); ++i) {
        auto face = faceNameFromJson((**facesField)[i], indexPath(facesPath, i));
        if (!face) {
            return std::unexpected(face.error());
        }
        openFaces.push_back(std::move(*face));
    }
    features::ShellDefinition definition{
        .target = FeatureId::fromValue(*target),
        .openFaces = std::move(openFaces),
        .thickness = Length::fromSi(*thickness),
        .thicknessParameter = std::nullopt,
        .side = *side,
    };
    if (*thicknessParameter) {
        definition.thicknessParameter = ParameterId::fromValue(**thicknessParameter);
    }
    auto feature = features::ShellFeature::create(std::move(name), definition);
    if (!feature) {
        return atPath(path, feature.error());
    }
    return std::move(*feature);
}

} // namespace bettercad::io::detail
