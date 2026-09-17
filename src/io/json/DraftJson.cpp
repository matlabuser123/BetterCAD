#include "io/json/ObjectJson.hpp"

#include <string>
#include <utility>
#include <vector>

// Draft features (P12-FEAT-004).
namespace bettercad::io::detail {

Json draftToJson(const features::DraftFeature& feature) {
    const features::DraftDefinition& d = feature.definition();
    Json json = Json::object();
    json["target"] = d.target.value();
    Json faces = Json::array();
    for (const FaceName& name : d.faces) {
        faces.push_back(faceNameToJson(name));
    }
    json["faces"] = std::move(faces);
    json["neutral_plane"] = planeReferenceToJson(d.neutralPlane);
    json["angle"] = d.angle.si();
    if (d.angleParameter) {
        json["angle_parameter"] = d.angleParameter->value();
    }
    return json;
}

Result<std::unique_ptr<features::DraftFeature>> draftFromJson(const Json& data, std::string name,
                                                              std::string_view path) {
    if (auto object = requireObject(data, path, {"target", "faces", "neutral_plane", "angle", "angle_parameter"});
        !object) {
        return std::unexpected(object.error());
    }
    auto target = readId(data, "target", path);
    auto facesField = requireArray(data, "faces", path);
    auto planeField = requireField(data, "neutral_plane", path);
    auto angle = readNumber(data, "angle", path);
    auto angleParameter = readOptionalId(data, "angle_parameter", path);
    if (!target || !facesField || !planeField || !angle || !angleParameter) {
        const Error& error = !target           ? target.error()
                             : !facesField     ? facesField.error()
                             : !planeField     ? planeField.error()
                             : !angle          ? angle.error()
                                               : angleParameter.error();
        return std::unexpected(error);
    }
    std::vector<FaceName> faces;
    const std::string facesPath = childPath(path, "faces");
    for (std::size_t i = 0; i < (*facesField)->size(); ++i) {
        auto face = faceNameFromJson((**facesField)[i], indexPath(facesPath, i));
        if (!face) {
            return std::unexpected(face.error());
        }
        faces.push_back(std::move(*face));
    }
    auto plane = planeReferenceFromJson(**planeField, childPath(path, "neutral_plane"));
    if (!plane) {
        return std::unexpected(plane.error());
    }
    features::DraftDefinition definition{
        .target = FeatureId::fromValue(*target),
        .faces = std::move(faces),
        .neutralPlane = std::move(*plane),
        .angle = Angle::fromSi(*angle),
        .angleParameter = std::nullopt,
    };
    if (*angleParameter) {
        definition.angleParameter = ParameterId::fromValue(**angleParameter);
    }
    auto feature = features::DraftFeature::create(std::move(name), definition);
    if (!feature) {
        return atPath(path, feature.error());
    }
    return std::move(*feature);
}

} // namespace bettercad::io::detail
