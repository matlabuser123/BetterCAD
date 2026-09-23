#include "ObjectJson.hpp"

#include "JsonReader.hpp"

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
                                     "extension", "arm_length", "finish"});
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
