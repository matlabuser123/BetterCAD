#include "ObjectJson.hpp"

#include "JsonReader.hpp"

#include <bettercad/core/Uuid.hpp>

#include <array>
#include <format>
#include <string_view>
#include <utility>

namespace bettercad::io::detail {
namespace {

// The same six keys, in the same order, that a datum coordinate system's
// offset uses (DatumJson.cpp), because a component placement is the same
// intent in the same convention.
constexpr std::array<std::string_view, 3> kTranslationKeys{"x", "y", "z"};
constexpr std::array<std::string_view, 3> kRotationKeys{"rx", "ry", "rz"};

template <QuantityType Q>
void putValue(Json& json, std::string_view key, Q value, const std::optional<ParameterId>& parameter) {
    json[std::string{key}] = value.si();
    if (parameter) {
        json[std::format("{}_parameter", key)] = parameter->value();
    }
}

[[nodiscard]] std::optional<ParameterId> parameterOf(const std::optional<std::uint64_t>& id) {
    return id ? std::optional<ParameterId>{ParameterId::fromValue(*id)} : std::nullopt;
}

[[nodiscard]] Json referenceToJson(const ObjectReference& reference) {
    // An internal reference is still a bare number, so every component
    // written before P13-REF-001 is written byte for byte as it was, and
    // every file written now that has no external part is readable by the
    // reader that came before.
    if (isInternal(reference)) {
        return reference.object.value();
    }
    Json json = Json::object();
    json["document"] = reference.document->value().toString();
    json["object"] = reference.object.value();
    // The locator is written only when there is one, and is never required
    // to read the reference back: it is a hint, not identity.
    if (!reference.hint.empty()) {
        json["hint"] = reference.hint;
    }
    return json;
}

[[nodiscard]] Result<ObjectReference> referenceFromJson(const Json& value, std::string_view path) {
    if (!value.is_object()) {
        // A number: an object of this document, as it has always been.
        auto object = readId(value, path);
        if (!object) {
            return std::unexpected(object.error());
        }
        return ObjectReference{ObjectId::fromValue(*object)};
    }
    if (auto valid = requireObject(value, path, {"document", "object", "hint"}); !valid) {
        return std::unexpected(valid.error());
    }
    auto text = readString(value, "document", path);
    if (!text) {
        return std::unexpected(text.error());
    }
    const auto uuid = Uuid::parse(*text);
    if (!uuid || uuid->isNil()) {
        return parseError(childPath(path, "document"), "expected a UUID");
    }
    auto object = readId(value, "object", path);
    if (!object) {
        return std::unexpected(object.error());
    }
    std::string hint;
    if (const auto found = value.find("hint"); found != value.end()) {
        if (!found->is_string()) {
            return parseError(childPath(path, "hint"), "must be a string");
        }
        hint = found->get<std::string>();
    }
    return ObjectReference{DocumentId::fromValue(*uuid), ObjectId::fromValue(*object), std::move(hint)};
}

[[nodiscard]] Json placementToJson(const ComponentPlacement& placement) {
    Json json = Json::object();
    for (std::size_t i = 0; i < 3; ++i) {
        putValue(json, kTranslationKeys[i], placement.translation[i], placement.translationParameters[i]);
    }
    for (std::size_t i = 0; i < 3; ++i) {
        putValue(json, kRotationKeys[i], placement.rotation[i], placement.rotationParameters[i]);
    }
    return json;
}

[[nodiscard]] Result<ComponentPlacement> placementFromJson(const Json& value, std::string_view path) {
    if (auto valid = requireObject(value, path,
                                   {"x", "y", "z", "rx", "ry", "rz", "x_parameter", "y_parameter", "z_parameter",
                                    "rx_parameter", "ry_parameter", "rz_parameter"});
        !valid) {
        return std::unexpected(valid.error());
    }
    ComponentPlacement placement;
    for (std::size_t i = 0; i < 3; ++i) {
        auto t = readNumber(value, kTranslationKeys[i], path);
        if (!t) {
            return std::unexpected(t.error());
        }
        auto tp = readOptionalId(value, std::format("{}_parameter", kTranslationKeys[i]), path);
        if (!tp) {
            return std::unexpected(tp.error());
        }
        auto r = readNumber(value, kRotationKeys[i], path);
        if (!r) {
            return std::unexpected(r.error());
        }
        auto rp = readOptionalId(value, std::format("{}_parameter", kRotationKeys[i]), path);
        if (!rp) {
            return std::unexpected(rp.error());
        }
        placement.translation[i] = Length::fromSi(*t);
        placement.translationParameters[i] = parameterOf(*tp);
        placement.rotation[i] = Angle::fromSi(*r);
        placement.rotationParameters[i] = parameterOf(*rp);
    }
    return placement;
}

} // namespace

// DocumentJson.cpp dispatches on the literal "component"; this is what
// keeps that literal and the type name itself from drifting apart.
static_assert(assembly::Component::kTypeName == "component",
              "the component type name and the name the reader dispatches on must agree");

Json componentToJson(const assembly::Component& component) {
    const assembly::ComponentDefinition& d = component.definition();
    Json json = Json::object();
    json["part"] = referenceToJson(d.part);
    // Written only when true, so the common case stays as small as the
    // shape it describes and a document of unsuppressed components has no
    // redundant keys.
    if (d.suppressed) {
        json["suppressed"] = true;
    }
    // Likewise the placement: a component that has not been moved writes no
    // placement at all, so every document written before P13-XFORM-001 is
    // still written byte for byte as it was.
    if (!isIdentity(d.placement)) {
        json["placement"] = placementToJson(d.placement);
    }
    return json;
}

Result<std::unique_ptr<assembly::Component>> componentFromJson(const Json& data, std::string name,
                                                               std::string_view path) {
    if (auto valid = requireObject(data, path, {"part", "suppressed", "placement"}); !valid) {
        return std::unexpected(valid.error());
    }
    auto partField = requireField(data, "part", path);
    if (!partField) {
        return std::unexpected(partField.error());
    }
    auto part = referenceFromJson(**partField, childPath(path, "part"));
    if (!part) {
        return std::unexpected(part.error());
    }
    assembly::ComponentDefinition d{.part = *std::move(part)};
    if (const auto suppressed = data.find("suppressed"); suppressed != data.end()) {
        if (!suppressed->is_boolean()) {
            return parseError(childPath(path, "suppressed"), "must be a boolean");
        }
        d.suppressed = suppressed->get<bool>();
    }
    if (const auto placement = data.find("placement"); placement != data.end()) {
        auto read = placementFromJson(*placement, childPath(path, "placement"));
        if (!read) {
            return std::unexpected(read.error());
        }
        d.placement = *read;
    }
    return assembly::Component::create(std::move(name), d);
}

} // namespace bettercad::io::detail
