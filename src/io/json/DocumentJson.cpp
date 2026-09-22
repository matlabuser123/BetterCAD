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
    if (const auto* view = dynamic_cast<const drawing::View*>(&object)) {
        data = detail::viewToJson(*view);
    } else if (const auto* sheet = dynamic_cast<const drawing::Sheet*>(&object)) {
        data = detail::sheetToJson(*sheet);
    } else if (const auto* component = dynamic_cast<const assembly::Component*>(&object)) {
        data = detail::componentToJson(*component);
    } else if (const auto* mate = dynamic_cast<const assembly::Mate*>(&object)) {
        data = detail::mateToJson(*mate);
    } else if (const auto* sketch = dynamic_cast<const sketch::Sketch*>(&object)) {
        data = detail::sketchToJson(*sketch);
    } else if (const auto* extrude = dynamic_cast<const features::ExtrudeFeature*>(&object)) {
        data = detail::extrudeToJson(*extrude);
    } else if (const auto* revolve = dynamic_cast<const features::RevolveFeature*>(&object)) {
        data = detail::revolveToJson(*revolve);
    } else if (const auto* chamfer = dynamic_cast<const features::ChamferFeature*>(&object)) {
        data = detail::chamferToJson(*chamfer);
    } else if (const auto* fillet = dynamic_cast<const features::FilletFeature*>(&object)) {
        data = detail::filletToJson(*fillet);
    } else if (const auto* variable = dynamic_cast<const features::VariableFilletFeature*>(&object)) {
        data = detail::variableFilletToJson(*variable);
    } else if (const auto* rib = dynamic_cast<const features::RibFeature*>(&object)) {
        data = detail::ribToJson(*rib);
    } else if (const auto* draft = dynamic_cast<const features::DraftFeature*>(&object)) {
        data = detail::draftToJson(*draft);
    } else if (const auto* shell = dynamic_cast<const features::ShellFeature*>(&object)) {
        data = detail::shellToJson(*shell);
    } else if (const auto* hole = dynamic_cast<const features::HoleFeature*>(&object)) {
        data = detail::holeToJson(*hole);
    } else if (const auto* pattern = dynamic_cast<const features::LinearPatternFeature*>(&object)) {
        data = detail::linearPatternToJson(*pattern);
    } else if (const auto* circular = dynamic_cast<const features::CircularPatternFeature*>(&object)) {
        data = detail::circularPatternToJson(*circular);
    } else if (const auto* mirror = dynamic_cast<const features::MirrorFeature*>(&object)) {
        data = detail::mirrorToJson(*mirror);
    } else if (const auto* sweep = dynamic_cast<const features::SweepFeature*>(&object)) {
        data = detail::sweepToJson(*sweep);
    } else if (const auto* loft = dynamic_cast<const features::LoftFeature*>(&object)) {
        data = detail::loftToJson(*loft);
    } else if (const auto* split = dynamic_cast<const features::SplitFeature*>(&object)) {
        data = detail::splitToJson(*split);
    } else if (const auto* combine = dynamic_cast<const features::CombineFeature*>(&object)) {
        data = detail::combineToJson(*combine);
    } else if (const auto* plane = dynamic_cast<const features::DatumPlane*>(&object)) {
        data = detail::datumPlaneToJson(*plane);
    } else if (const auto* axis = dynamic_cast<const features::DatumAxis*>(&object)) {
        data = detail::datumAxisToJson(*axis);
    } else if (const auto* system = dynamic_cast<const features::CoordinateSystem*>(&object)) {
        data = detail::coordinateSystemToJson(*system);
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
    // The literal, not Component::kTypeName: binding a reference to a
    // dll-imported constexpr static does not link in a shared build
    // (found by the debug-shared preset). ComponentJson.cpp carries a
    // static_assert that the two agree, so they cannot drift. The
    // sketch branch below has always compared against its literal.
    if (*type == "view") {
        auto view = detail::viewFromJson(**data, std::move(*name), dataPath);
        if (!view) {
            return std::unexpected(view.error());
        }
        return std::move(*view);
    }
    if (*type == "sheet") {
        auto sheet = detail::sheetFromJson(**data, std::move(*name), dataPath);
        if (!sheet) {
            return std::unexpected(sheet.error());
        }
        return std::move(*sheet);
    }
    if (*type == "mate") {
        auto mate = detail::mateFromJson(**data, std::move(*name), dataPath);
        if (!mate) {
            return std::unexpected(mate.error());
        }
        return std::unique_ptr<DocumentObject>(std::move(*mate));
    }
    if (*type == "component") {
        auto component = detail::componentFromJson(**data, std::move(*name), dataPath);
        if (!component) {
            return std::unexpected(component.error());
        }
        return std::unique_ptr<DocumentObject>(std::move(*component));
    }
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
    if (*type == features::SplitFeature::kTypeName) {
        auto split = detail::splitFromJson(**data, std::move(*name), dataPath);
        if (!split) {
            return std::unexpected(split.error());
        }
        return std::unique_ptr<DocumentObject>(std::move(*split));
    }
    if (*type == features::CombineFeature::kTypeName) {
        auto combine = detail::combineFromJson(**data, std::move(*name), dataPath);
        if (!combine) {
            return std::unexpected(combine.error());
        }
        return std::unique_ptr<DocumentObject>(std::move(*combine));
    }
    if (*type == features::FilletFeature::kTypeName) {
        auto fillet = detail::filletFromJson(**data, std::move(*name), dataPath);
        if (!fillet) {
            return std::unexpected(fillet.error());
        }
        return std::unique_ptr<DocumentObject>(std::move(*fillet));
    }
    if (*type == features::VariableFilletFeature::kTypeName) {
        auto variable = detail::variableFilletFromJson(**data, std::move(*name), dataPath);
        if (!variable) {
            return std::unexpected(variable.error());
        }
        return std::unique_ptr<DocumentObject>(std::move(*variable));
    }
    if (*type == features::RibFeature::kTypeName) {
        auto rib = detail::ribFromJson(**data, std::move(*name), dataPath);
        if (!rib) {
            return std::unexpected(rib.error());
        }
        return std::unique_ptr<DocumentObject>(std::move(*rib));
    }
    if (*type == features::DraftFeature::kTypeName) {
        auto draft = detail::draftFromJson(**data, std::move(*name), dataPath);
        if (!draft) {
            return std::unexpected(draft.error());
        }
        return std::unique_ptr<DocumentObject>(std::move(*draft));
    }
    if (*type == features::ShellFeature::kTypeName) {
        auto shell = detail::shellFromJson(**data, std::move(*name), dataPath);
        if (!shell) {
            return std::unexpected(shell.error());
        }
        return std::unique_ptr<DocumentObject>(std::move(*shell));
    }
    if (*type == features::HoleFeature::kTypeName) {
        auto hole = detail::holeFromJson(**data, std::move(*name), dataPath);
        if (!hole) {
            return std::unexpected(hole.error());
        }
        return std::unique_ptr<DocumentObject>(std::move(*hole));
    }
    if (*type == features::LinearPatternFeature::kTypeName) {
        auto pattern = detail::linearPatternFromJson(**data, std::move(*name), dataPath);
        if (!pattern) {
            return std::unexpected(pattern.error());
        }
        return std::unique_ptr<DocumentObject>(std::move(*pattern));
    }
    if (*type == features::CircularPatternFeature::kTypeName) {
        auto circular = detail::circularPatternFromJson(**data, std::move(*name), dataPath);
        if (!circular) {
            return std::unexpected(circular.error());
        }
        return std::unique_ptr<DocumentObject>(std::move(*circular));
    }
    if (*type == features::MirrorFeature::kTypeName) {
        auto mirror = detail::mirrorFromJson(**data, std::move(*name), dataPath);
        if (!mirror) {
            return std::unexpected(mirror.error());
        }
        return std::unique_ptr<DocumentObject>(std::move(*mirror));
    }
    if (*type == features::SweepFeature::kTypeName) {
        auto sweep = detail::sweepFromJson(**data, std::move(*name), dataPath);
        if (!sweep) {
            return std::unexpected(sweep.error());
        }
        return std::unique_ptr<DocumentObject>(std::move(*sweep));
    }
    if (*type == features::LoftFeature::kTypeName) {
        auto loft = detail::loftFromJson(**data, std::move(*name), dataPath);
        if (!loft) {
            return std::unexpected(loft.error());
        }
        return std::unique_ptr<DocumentObject>(std::move(*loft));
    }
    if (*type == features::DatumPlane::kTypeName) {
        auto plane = detail::datumPlaneFromJson(**data, std::move(*name), dataPath);
        if (!plane) {
            return std::unexpected(plane.error());
        }
        return std::unique_ptr<DocumentObject>(std::move(*plane));
    }
    if (*type == features::DatumAxis::kTypeName) {
        auto axis = detail::datumAxisFromJson(**data, std::move(*name), dataPath);
        if (!axis) {
            return std::unexpected(axis.error());
        }
        return std::unique_ptr<DocumentObject>(std::move(*axis));
    }
    if (*type == features::CoordinateSystem::kTypeName) {
        auto system = detail::coordinateSystemFromJson(**data, std::move(*name), dataPath);
        if (!system) {
            return std::unexpected(system.error());
        }
        return std::unique_ptr<DocumentObject>(std::move(*system));
    }
    return detail::parseError(detail::childPath(path, "type"), std::format("unknown object type '{}'", *type));
}

/// A configuration's overrides are keyed by parameter ID and hold SI values;
/// the dimension and the display unit are the parameter's own, and loading
/// checks the parameter is free and of that dimension (P12-PARAM-002).
Json configurationsToJson(const ConfigurationTable& table) {
    Json json = Json::object();
    if (const Configuration* active = table.activeConfiguration()) {
        json["active"] = active->name();
    }
    Json defined = Json::array();
    for (const Configuration& configuration : table.all()) {
        Json entry = Json::object();
        entry["id"] = configuration.id().value();
        entry["name"] = configuration.name();
        Json overrides = Json::array();
        for (const auto& [parameter, value] : configuration.overrides()) {
            Json item = Json::object();
            item["parameter"] = parameter.value();
            item["si_value"] = value.siValue;
            overrides.push_back(std::move(item));
        }
        entry["overrides"] = std::move(overrides);
        // Suppression overrides (P13-CONF-001). Written only when the
        // configuration has any, so a parameter-only document's file is
        // exactly what it was before this milestone.
        const auto suppressionToJson = [](const auto& overridesById) {
            Json array = Json::array();
            for (const auto& [id, suppressed] : overridesById) {
                Json item = Json::object();
                item["object"] = id.value();
                item["suppressed"] = suppressed;
                array.push_back(std::move(item));
            }
            return array;
        };
        if (!configuration.componentSuppression().empty()) {
            entry["components"] = suppressionToJson(configuration.componentSuppression());
        }
        if (!configuration.mateSuppression().empty()) {
            entry["mates"] = suppressionToJson(configuration.mateSuppression());
        }
        defined.push_back(std::move(entry));
    }
    json["defined"] = std::move(defined);
    return json;
}

/// Reads the configurations into @p document, which must already hold its
/// parameters and its objects -- a configuration overrides both parameter
/// values and the suppression of components and mates, and refuses to name
/// anything that is not there. The active configuration is named, not numbered, so that a
/// file stays readable.
Result<void> readConfigurations(const Json& value, Document& document) {
    if (auto object = detail::requireObject(value, "configurations", {"active", "defined"}); !object) {
        return std::unexpected(object.error());
    }
    auto definedField = detail::requireArray(value, "defined", "configurations");
    if (!definedField) {
        return std::unexpected(definedField.error());
    }
    const Json& defined = **definedField;
    for (std::size_t i = 0; i < defined.size(); ++i) {
        const std::string path = detail::indexPath("configurations.defined", i);
        const Json& entry = defined[i];
        if (auto object = detail::requireObject(entry, path, {"id", "name", "overrides", "components", "mates"}); !object) {
            return std::unexpected(object.error());
        }
        auto id = detail::readId(entry, "id", path);
        auto name = detail::readString(entry, "name", path);
        if (!id || !name) {
            return std::unexpected(!id ? id.error() : name.error());
        }
        if (*id == 0) {
            return detail::parseError(detail::childPath(path, "id"), "a configuration needs a valid ID");
        }
        auto configuration = Configuration::create(ConfigurationId::fromValue(*id), std::move(*name));
        if (!configuration) {
            return detail::atPath(path, configuration.error());
        }
        auto overridesField = detail::requireArray(entry, "overrides", path);
        if (!overridesField) {
            return std::unexpected(overridesField.error());
        }
        const Json& overrides = **overridesField;
        for (std::size_t j = 0; j < overrides.size(); ++j) {
            const std::string itemPath = detail::indexPath(detail::childPath(path, "overrides"), j);
            const Json& item = overrides[j];
            if (auto object = detail::requireObject(item, itemPath, {"parameter", "si_value"}); !object) {
                return std::unexpected(object.error());
            }
            auto parameter = detail::readId(item, "parameter", itemPath);
            auto siValue = detail::readNumber(item, "si_value", itemPath);
            if (!parameter || !siValue) {
                return std::unexpected(!parameter ? parameter.error() : siValue.error());
            }
            const ParameterId target = ParameterId::fromValue(*parameter);
            const Parameter* found = document.parameters().find(target);
            if (found == nullptr) {
                return detail::parseError(detail::childPath(itemPath, "parameter"),
                                          std::format("no parameter with ID {}", *parameter));
            }
            auto set = configuration->setOverride(target, DimensionedValue{found->dimension(), *siValue});
            if (!set) {
                return detail::atPath(itemPath, set.error());
            }
        }
        // Suppression overrides. Absent is the norm, and means a
        // configuration that changes only parameters -- which is every file
        // written before P13-CONF-001.
        for (const std::string_view key : {"components", "mates"}) {
            const auto field = entry.find(key);
            if (field == entry.end()) {
                continue;
            }
            const std::string listPath = detail::childPath(path, key);
            auto listField = detail::requireArray(entry, key, path);
            if (!listField) {
                return std::unexpected(listField.error());
            }
            const Json& list = **listField;
            for (std::size_t j = 0; j < list.size(); ++j) {
                const std::string itemPath = detail::indexPath(listPath, j);
                const Json& item = list[j];
                if (auto object = detail::requireObject(item, itemPath, {"object", "suppressed"}); !object) {
                    return std::unexpected(object.error());
                }
                auto objectId = detail::readId(item, "object", itemPath);
                auto suppressed = detail::readBool(item, "suppressed", itemPath);
                if (!objectId || !suppressed) {
                    return std::unexpected(!objectId ? objectId.error() : suppressed.error());
                }
                // A configuration must never name an object that is gone, so
                // the reader refuses a file in which one does rather than
                // loading an override that can never apply.
                if (document.findObject(ObjectId::fromValue(*objectId)) == nullptr) {
                    return detail::parseError(detail::childPath(itemPath, "object"),
                                              std::format("no object with ID {}", *objectId));
                }
                const Result<bool> set =
                    key == "components"
                        ? configuration->setSuppressed(ComponentId::fromValue(*objectId), *suppressed)
                        : configuration->setSuppressed(MateId::fromValue(*objectId), *suppressed);
                if (!set) {
                    return detail::atPath(itemPath, set.error());
                }
            }
        }
        if (auto inserted = document.insertConfiguration(std::move(*configuration)); !inserted) {
            return detail::atPath(path, inserted.error());
        }
    }

    auto active = detail::readOptionalString(value, "active", "configurations");
    if (!active) {
        return std::unexpected(active.error());
    }
    if (*active) {
        const Configuration* found = document.configurations().findByName(**active);
        if (found == nullptr) {
            return detail::parseError("configurations.active",
                                      std::format("no configuration named '{}'", **active));
        }
        if (auto set = document.setActiveConfiguration(found->id()); !set) {
            return detail::atPath("configurations.active", set.error());
        }
    }
    return {};
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
    // Written only when there are configurations, so a document from before
    // P12-PARAM-002 is written back exactly as it was.
    if (!document.configurations().empty()) {
        root["configurations"] = configurationsToJson(document.configurations());
    }
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
    if (auto object = detail::requireObject(
            root, "", {"format", "version", "units", "document", "parameters", "objects", "configurations"});
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
    // Configurations override parameters, so they come after them; from
    // P13-CONF-001 they also override the suppression of components and
    // mates, so they come after the objects too. Absent is the base
    // configuration, which is what every file written before P12-PARAM-002
    // means.
    if (const auto configurations = root.find("configurations"); configurations != root.end()) {
        if (auto read = readConfigurations(*configurations, document); !read) {
            return std::unexpected(read.error());
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
