#include "ObjectJson.hpp"

#include "JsonReader.hpp"

#include <array>
#include <format>
#include <string>
#include <utility>

namespace bettercad::io::detail {
namespace {

Json targetToJson(const drawing::AnnotationTarget& target) {
    Json json = Json::object();
    if (target.plane) {
        json["plane"] = planeReferenceToJson(*target.plane);
    } else if (target.axis) {
        json["axis"] = axisReferenceToJson(*target.axis);
    } else if (target.cylinder) {
        json["cylinder"] = faceNameToJson(*target.cylinder);
    } else if (target.object) {
        json["object"] = target.object->value();
    }
    return json;
}

Result<drawing::AnnotationTarget> targetFromJson(const Json& value, std::string_view path) {
    if (auto object = requireObject(value, path, {"plane", "axis", "cylinder", "object"}); !object) {
        return std::unexpected(object.error());
    }
    drawing::AnnotationTarget target;
    if (const auto plane = value.find("plane"); plane != value.end()) {
        auto reference = planeReferenceFromJson(*plane, childPath(path, "plane"));
        if (!reference) {
            return std::unexpected(reference.error());
        }
        target.plane = *reference;
    }
    if (const auto axis = value.find("axis"); axis != value.end()) {
        auto reference = axisReferenceFromJson(*axis, childPath(path, "axis"));
        if (!reference) {
            return std::unexpected(reference.error());
        }
        target.axis = *reference;
    }
    if (const auto cylinder = value.find("cylinder"); cylinder != value.end()) {
        auto name = faceNameFromJson(*cylinder, childPath(path, "cylinder"));
        if (!name) {
            return std::unexpected(name.error());
        }
        target.cylinder = *name;
    }
    if (value.contains("object")) {
        auto object = readId(value, "object", path);
        if (!object) {
            return std::unexpected(object.error());
        }
        target.object = ObjectId::fromValue(*object);
    }
    return target;
}

Json controlFrameToJson(const drawing::FeatureControlFrame& frame) {
    Json datums = Json::array();
    // An ARRAY, and in order: primary, secondary, tertiary. A|B|C is a
    // different requirement from B|A|C, so the order is the meaning and a
    // set would lose it.
    for (const drawing::DatumReference& datum : frame.datums) {
        datums.push_back(std::string(1, datum.letter));
    }
    return Json{{"characteristic", std::string{drawing::toString(frame.characteristic)}},
                {"zone", std::string{drawing::toString(frame.zone)}},
                {"tolerance", frame.tolerance.si()},
                {"datums", std::move(datums)}};
}

Result<drawing::FeatureControlFrame> controlFrameFromJson(const Json& value,
                                                          std::string_view path) {
    if (auto object = requireObject(value, path,
                                    {"characteristic", "zone", "tolerance", "datums"});
        !object) {
        return std::unexpected(object.error());
    }
    drawing::FeatureControlFrame frame;

    auto characteristicText = readString(value, "characteristic", path);
    if (!characteristicText) {
        return std::unexpected(characteristicText.error());
    }
    const auto characteristic = drawing::geometricCharacteristicFromString(*characteristicText);
    if (!characteristic) {
        return parseError(childPath(path, "characteristic"),
                          std::format("unknown geometric characteristic '{}'",
                                      *characteristicText));
    }
    frame.characteristic = *characteristic;

    auto zoneText = readString(value, "zone", path);
    if (!zoneText) {
        return std::unexpected(zoneText.error());
    }
    const auto zone = drawing::toleranceZoneFromString(*zoneText);
    if (!zone) {
        return parseError(childPath(path, "zone"),
                          std::format("unknown tolerance zone '{}'", *zoneText));
    }
    frame.zone = *zone;

    auto tolerance = readNumber(value, "tolerance", path);
    if (!tolerance) {
        return std::unexpected(tolerance.error());
    }
    frame.tolerance = Length::fromSi(*tolerance);

    const auto datums = value.find("datums");
    if (datums == value.end() || !datums->is_array()) {
        return parseError(childPath(path, "datums"),
                          "a frame lists the datums it cites, in order, even if the list is empty");
    }
    const std::string datumsPath{childPath(path, "datums")};
    for (const Json& datum : *datums) {
        if (!datum.is_string()) {
            return parseError(datumsPath, "a datum is named by its letter");
        }
        const std::string letter = datum.get<std::string>();
        if (letter.size() != 1) {
            return parseError(datumsPath, "a datum is named by a single letter");
        }
        frame.datums.push_back(drawing::DatumReference{letter.front()});
    }
    // validate() is not called here: Annotation::create runs it on the whole
    // definition, so a bad frame is refused once and with one message.
    return frame;
}

} // namespace

// DocumentJson.cpp dispatches on the literal "annotation"; this is what keeps
// that literal and the type name itself from drifting apart. Binding a
// reference to a dll-imported constexpr static does not link in a shared
// build.
static_assert(drawing::Annotation::kTypeName == "annotation",
              "the annotation type name and the name the reader dispatches on must agree");

Json annotationToJson(const drawing::Annotation& annotation) {
    const drawing::AnnotationDefinition& d = annotation.definition();
    Json json = Json::object();
    json["type"] = std::string{drawing::toString(d.type)};
    json["view"] = d.view.value();
    if (!drawing::isEmpty(d.target)) {
        json["target"] = targetToJson(d.target);
    }
    // The words are written only for the kinds whose words are an engineer's
    // choice. A hole callout's text is DERIVED and there is nowhere here to
    // put it, which is what stops a file disagreeing with the hole.
    if (!d.text.empty()) {
        json["text"] = d.text;
    }
    json["height"] = d.style.height.si();
    json["x"] = d.placement.x.si();
    json["y"] = d.placement.y.si();
    if (d.type == drawing::AnnotationType::Centreline) {
        json["extension"] = d.extension.si();
    }
    if (d.type == drawing::AnnotationType::Centremark) {
        json["arm_length"] = d.armLength.si();
    }
    if (d.type == drawing::AnnotationType::BomTable) {
        // SIZES, not contents. What the table says is computed from the
        // assembly every time it is drawn, so there is nowhere here to put a
        // row, a quantity or an item number -- which is what stops a file
        // disagreeing with the assembly it describes (ADR-022).
        json["table"] = Json{{"row_height", d.table.rowHeight.si()},
                             {"item_width", d.table.itemWidth.si()},
                             {"part_width", d.table.partWidth.si()},
                             {"quantity_width", d.table.quantityWidth.si()},
                             {"header", d.table.header}};
    }
    if (d.frame) {
        json["frame"] = controlFrameToJson(*d.frame);
    }
    if (d.finish) {
        json["finish"] = Json{{"roughness", d.finish->roughness.si()},
                              {"removal", std::string{drawing::toString(d.finish->removal)}}};
    }
    return json;
}

Result<std::unique_ptr<drawing::Annotation>> annotationFromJson(const Json& data, std::string name,
                                                                std::string_view path) {
    if (auto object = requireObject(data, path,
                                    {"type", "view", "target", "text", "height", "x", "y",
                                     "extension", "arm_length", "finish", "frame", "table"});
        !object) {
        return std::unexpected(object.error());
    }

    drawing::AnnotationDefinition definition;

    auto typeText = readString(data, "type", path);
    if (!typeText) {
        return std::unexpected(typeText.error());
    }
    const auto type = drawing::annotationTypeFromString(*typeText);
    if (!type) {
        return parseError(childPath(path, "type"),
                          std::format("unknown annotation type '{}'", *typeText));
    }
    definition.type = *type;

    auto view = readId(data, "view", path);
    if (!view) {
        return std::unexpected(view.error());
    }
    definition.view = ViewId::fromValue(*view);

    if (const auto target = data.find("target"); target != data.end()) {
        auto resolved = targetFromJson(*target, childPath(path, "target"));
        if (!resolved) {
            return std::unexpected(resolved.error());
        }
        definition.target = std::move(*resolved);
    }

    if (data.contains("text")) {
        auto text = readString(data, "text", path);
        if (!text) {
            return std::unexpected(text.error());
        }
        definition.text = std::move(*text);
    }

    auto height = readNumber(data, "height", path);
    if (!height) {
        return std::unexpected(height.error());
    }
    definition.style.height = Length::fromSi(*height);

    auto x = readNumber(data, "x", path);
    if (!x) {
        return std::unexpected(x.error());
    }
    auto y = readNumber(data, "y", path);
    if (!y) {
        return std::unexpected(y.error());
    }
    definition.placement = Point2D{Length::fromSi(*x), Length::fromSi(*y)};

    if (data.contains("extension")) {
        auto extension = readNumber(data, "extension", path);
        if (!extension) {
            return std::unexpected(extension.error());
        }
        definition.extension = Length::fromSi(*extension);
    }
    if (data.contains("arm_length")) {
        auto arm = readNumber(data, "arm_length", path);
        if (!arm) {
            return std::unexpected(arm.error());
        }
        definition.armLength = Length::fromSi(*arm);
    }

    if (const auto table = data.find("table"); table != data.end()) {
        const std::string tablePath{childPath(path, "table")};
        if (auto object = requireObject(*table, tablePath,
                                        {"row_height", "item_width", "part_width",
                                         "quantity_width", "header"});
            !object) {
            return std::unexpected(object.error());
        }
        for (const auto& [key, field] : std::array<std::pair<std::string_view, Length*>, 4>{
                 {{"row_height", &definition.table.rowHeight},
                  {"item_width", &definition.table.itemWidth},
                  {"part_width", &definition.table.partWidth},
                  {"quantity_width", &definition.table.quantityWidth}}}) {
            auto value = readNumber(*table, std::string{key}, tablePath);
            if (!value) {
                return std::unexpected(value.error());
            }
            *field = Length::fromSi(*value);
        }
        auto header = readBool(*table, "header", tablePath);
        if (!header) {
            return std::unexpected(header.error());
        }
        definition.table.header = *header;
    }

    if (const auto frame = data.find("frame"); frame != data.end()) {
        auto resolved = controlFrameFromJson(*frame, childPath(path, "frame"));
        if (!resolved) {
            return std::unexpected(resolved.error());
        }
        definition.frame = std::move(*resolved);
    }

    if (const auto finish = data.find("finish"); finish != data.end()) {
        const std::string finishPath{childPath(path, "finish")};
        if (auto object = requireObject(*finish, finishPath, {"roughness", "removal"}); !object) {
            return std::unexpected(object.error());
        }
        auto roughness = readNumber(*finish, "roughness", finishPath);
        if (!roughness) {
            return std::unexpected(roughness.error());
        }
        auto removalText = readString(*finish, "removal", finishPath);
        if (!removalText) {
            return std::unexpected(removalText.error());
        }
        const auto removal = drawing::materialRemovalFromString(*removalText);
        if (!removal) {
            return parseError(childPath(finishPath, "removal"),
                              std::format("unknown material-removal requirement '{}'", *removalText));
        }
        definition.finish = drawing::SurfaceFinish{Length::fromSi(*roughness), *removal};
    }

    // Annotation::create validates, so a file describing a note that points at
    // something, a datum with two letters, or a hole callout carrying its own
    // text is refused on load rather than becoming a drawing that cannot be
    // drawn.
    auto annotation = drawing::Annotation::create(std::move(name), definition);
    if (!annotation) {
        return parseError(path, annotation.error().message);
    }
    return std::move(*annotation);
}

} // namespace bettercad::io::detail
