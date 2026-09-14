#include <bettercad/io/DocumentFile.hpp>

#include "io/json/ObjectJson.hpp"
#include "io/json/ParameterJsonDetail.hpp"

#include <format>
#include <memory>
#include <string>

namespace bettercad::io {

namespace {

using detail::Json;

Result<Json> objectToJson(const DocumentObject& object) {
    Json data;
    if (const auto* sketch = dynamic_cast<const sketch::Sketch*>(&object)) {
        data = detail::sketchToJson(*sketch);
    } else if (const auto* extrude = dynamic_cast<const features::ExtrudeFeature*>(&object)) {
        data = detail::extrudeToJson(*extrude);
    } else if (const auto* revolve = dynamic_cast<const features::RevolveFeature*>(&object)) {
        data = detail::revolveToJson(*revolve);
    } else if (const auto* chamfer = dynamic_cast<const features::ChamferFeature*>(&object)) {
        data = detail::chamferToJson(*chamfer);
    } else if (const auto* fillet = dynamic_cast<const features::FilletFeature*>(&object)) {
        data = detail::filletToJson(*fillet);
    } else {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("objects of type '{}' cannot be saved", object.typeName()));
    }
    Json json = Json::object();
    json["id"] = object.id().value();
    json["type"] = std::string{object.typeName()};
    json["name"] = object.name();
    json["data"] = std::move(data);
    return json;
}

Result<std::unique_ptr<DocumentObject>> objectFromJson(const Json& value, std::string_view path) {
    if (auto object = detail::requireObject(value, path, {"id", "type", "name", "data"}); !object) {
        return std::unexpected(object.error());
    }
    auto type = detail::readString(value, "type", path);
    auto name = detail::readString(value, "name", path);
    auto data = detail::requireField(value, "data", path);
    if (!type || !name || !data) {
        return std::unexpected(!type ? type.error() : !name ? name.error() : data.error());
    }
    const std::string dataPath = detail::childPath(path, "data");
    if (*type == "sketch") {
        auto sketch = detail::sketchFromJson(**data, std::move(*name), dataPath);
        if (!sketch) {
            return std::unexpected(sketch.error());
        }
        return std::unique_ptr<DocumentObject>(std::move(*sketch));
    }
    if (*type == features::ExtrudeFeature::kTypeName) {
        auto extrude = detail::extrudeFromJson(**data, std::move(*name), dataPath);
        if (!extrude) {
            return std::unexpected(extrude.error());
        }
        return std::unique_ptr<DocumentObject>(std::move(*extrude));
    }
    if (*type == features::RevolveFeature::kTypeName) {
        auto revolve = detail::revolveFromJson(**data, std::move(*name), dataPath);
        if (!revolve) {
            return std::unexpected(revolve.error());
        }
        return std::unique_ptr<DocumentObject>(std::move(*revolve));
    }
    if (*type == features::ChamferFeature::kTypeName) {
        auto chamfer = detail::chamferFromJson(**data, std::move(*name), dataPath);
        if (!chamfer) {
            return std::unexpected(chamfer.error());
        }
        return std::unique_ptr<DocumentObject>(std::move(*chamfer));
    }
    if (*type == features::FilletFeature::kTypeName) {
        auto fillet = detail::filletFromJson(**data, std::move(*name), dataPath);
        if (!fillet) {
            return std::unexpected(fillet.error());
        }
        return std::unique_ptr<DocumentObject>(std::move(*fillet));
    }
    return detail::parseError(detail::childPath(path, "type"), std::format("unknown object type '{}'", *type));
}

} // namespace

Result<std::string> documentToJson(const Document& document) {
    Json root = Json::object();
    root["format"] = std::string{kDocumentFormat};
    root["version"] = kDocumentFormatVersion;
    root["units"] = "SI";

    Json header = Json::object();
    header["id"] = document.id().value().toString();
    header["name"] = document.name();
    Json metadata = Json::object();
    metadata["description"] = document.metadata().description;
    metadata["author"] = document.metadata().author;
    Json properties = Json::object();
    for (const auto& [key, value] : document.metadata().properties) {
        properties[key] = value;
    }
    metadata["properties"] = std::move(properties);
    header["metadata"] = std::move(metadata);
    header["last_allocated_id"] = document.lastAllocatedId();
    root["document"] = std::move(header);

    root["parameters"] = detail::parameterTableToJson(document.parameters());
    Json objects = Json::array();
    for (const DocumentObject& object : document.objects()) {
        auto json = objectToJson(object);
        if (!json) {
            return std::unexpected(json.error());
        }
        objects.push_back(std::move(*json));
    }
    root["objects"] = std::move(objects);
    return detail::dumpJson(root);
}

Result<Document> documentFromJson(std::string_view text) {
    auto parsed = detail::parseJson(text);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    const Json& root = *parsed;
    if (auto object = detail::requireObject(root, "",
                                            {"format", "version", "units", "document", "parameters", "objects"});
        !object) {
        return std::unexpected(object.error());
    }
    auto format = detail::readString(root, "format", "");
    auto version = detail::readUnsigned(root, "version", "");
    auto units = detail::readString(root, "units", "");
    if (!format || !version || !units) {
        return std::unexpected(!format ? format.error() : !version ? version.error() : units.error());
    }
    if (*format != kDocumentFormat) {
        return detail::parseError("format", std::format("expected '{}', got '{}'", kDocumentFormat, *format));
    }
    if (*version != static_cast<std::uint64_t>(kDocumentFormatVersion)) {
        return detail::parseError("version", std::format("unsupported version {} (supported: {})", *version,
                                                         kDocumentFormatVersion));
    }
    if (*units != "SI") {
        return detail::parseError("units", std::format("unsupported unit system '{}'", *units));
    }

    // Header.
    auto headerField = detail::requireField(root, "document", "");
    if (!headerField) {
        return std::unexpected(headerField.error());
    }
    const Json& header = **headerField;
    if (auto object = detail::requireObject(header, "document", {"id", "name", "metadata", "last_allocated_id"});
        !object) {
        return std::unexpected(object.error());
    }
    auto idText = detail::readString(header, "id", "document");
    auto name = detail::readString(header, "name", "document");
    auto lastId = detail::readId(header, "last_allocated_id", "document");
    auto metadataField = detail::requireField(header, "metadata", "document");
    if (!idText || !name || !lastId || !metadataField) {
        const Error& error = !idText ? idText.error()
                           : !name ? name.error()
                           : !lastId ? lastId.error()
                                     : metadataField.error();
        return std::unexpected(error);
    }
    const auto uuid = Uuid::parse(*idText);
    if (!uuid || uuid->isNil()) {
        return detail::parseError("document.id", "expected a UUID");
    }
    const Json& metadataJson = **metadataField;
    if (auto object = detail::requireObject(metadataJson, "document.metadata", {"description", "author", "properties"});
        !object) {
        return std::unexpected(object.error());
    }
    auto description = detail::readString(metadataJson, "description", "document.metadata");
    auto author = detail::readString(metadataJson, "author", "document.metadata");
    auto propertiesField = detail::requireField(metadataJson, "properties", "document.metadata");
    if (!description || !author || !propertiesField) {
        return std::unexpected(!description ? description.error()
                               : !author    ? author.error()
                                            : propertiesField.error());
    }
    DocumentMetadata metadata{.description = std::move(*description), .author = std::move(*author), .properties = {}};
    if (!(*propertiesField)->is_object()) {
        return detail::parseError("document.metadata.properties", "expected an object");
    }
    for (const auto& item : (*propertiesField)->items()) {
        if (!item.value().is_string()) {
            return detail::parseError(detail::childPath("document.metadata.properties", item.key()),
                                      "expected a string");
        }
        metadata.properties.emplace(item.key(), item.value().get<std::string>());
    }

    // setName() validates the name; the constructor does not.
    Document document(DocumentId::fromValue(*uuid), "Untitled");
    if (auto set = document.setName(std::move(*name)); !set) {
        return detail::atPath("document.name", set.error());
    }
    if (auto set = document.setMetadata(std::move(metadata)); !set) {
        return detail::atPath("document.metadata", set.error());
    }

    // Parameters, then objects, each keeping its ID.
    auto parametersField = detail::requireField(root, "parameters", "");
    if (!parametersField) {
        return std::unexpected(parametersField.error());
    }
    auto parameters = detail::parameterTableFromJson(**parametersField, "parameters");
    if (!parameters) {
        return std::unexpected(parameters.error());
    }
    std::size_t index = 0;
    for (const Parameter& parameter : parameters->all()) {
        if (auto inserted = document.insertParameter(parameter); !inserted) {
            return detail::atPath(detail::indexPath("parameters", index), inserted.error());
        }
        ++index;
    }
    auto objectsField = detail::requireArray(root, "objects", "");
    if (!objectsField) {
        return std::unexpected(objectsField.error());
    }
    for (std::size_t i = 0; i < (*objectsField)->size(); ++i) {
        const Json& value = (**objectsField)[i];
        const std::string path = detail::indexPath("objects", i);
        auto object = objectFromJson(value, path);
        if (!object) {
            return std::unexpected(object.error());
        }
        auto id = detail::readId(value, "id", path);
        if (!id) {
            return std::unexpected(id.error());
        }
        if (auto restored = document.restoreObject(ObjectId::fromValue(*id), std::move(*object)); !restored) {
            return detail::atPath(path, restored.error());
        }
    }
    // Deleted items keep their IDs reserved, so the counter is never below an
    // ID in use.
    if (*lastId < document.lastAllocatedId()) {
        return detail::parseError("document.last_allocated_id", "less than an ID in use");
    }
    document.reserveIdsThrough(*lastId);
    document.markClean();
    return document;
}

} // namespace bettercad::io
