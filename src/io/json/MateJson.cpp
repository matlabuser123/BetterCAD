#include "ObjectJson.hpp"

#include "JsonReader.hpp"

#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace bettercad::io::detail {
namespace {

using assembly::MateType;

constexpr std::array<std::pair<MateType, std::string_view>, 11> kMateTypes{{
    {MateType::Fixed, "fixed"},
    {MateType::Coincident, "coincident"},
    {MateType::Concentric, "concentric"},
    {MateType::Parallel, "parallel"},
    {MateType::Perpendicular, "perpendicular"},
    {MateType::Distance, "distance"},
    {MateType::Angle, "angle"},
    {MateType::Revolute, "revolute"},
    {MateType::Slider, "slider"},
    {MateType::Cylindrical, "cylindrical"},
    {MateType::Planar, "planar"},
}};

constexpr std::array<std::pair<MateTargetKind, std::string_view>, 3> kTargetKinds{{
    {MateTargetKind::Plane, "plane"},
    {MateTargetKind::Axis, "axis"},
    {MateTargetKind::Face, "face"},
}};

// The same shape DatumJson uses for its own enums. Kept local rather than
// shared, because the alternative is moving a helper out of a module this
// milestone has no other reason to touch.
template <typename Enum, std::size_t N>
[[nodiscard]] std::string_view nameIn(const std::array<std::pair<Enum, std::string_view>, N>& table, Enum value) {
    for (const auto& [item, name] : table) {
        if (item == value) {
            return name;
        }
    }
    return "unknown";
}

template <typename Enum, std::size_t N>
[[nodiscard]] Result<Enum> valueIn(const std::array<std::pair<Enum, std::string_view>, N>& table, const Json& object,
                                   std::string_view key, std::string_view path) {
    auto text = readString(object, key, path);
    if (!text) {
        return std::unexpected(text.error());
    }
    for (const auto& [item, name] : table) {
        if (name == *text) {
            return item;
        }
    }
    return parseError(childPath(path, key), std::format("'{}' is not one of the kinds this key takes", *text));
}

[[nodiscard]] Json targetToJson(const MateTarget& target) {
    Json json = Json::object();
    json["component"] = target.component.value();
    json["kind"] = std::string{nameIn(kTargetKinds, target.kind)};
    switch (target.kind) {
    case MateTargetKind::Plane:
        json["plane"] = planeReferenceToJson(*target.plane);
        break;
    case MateTargetKind::Axis:
        json["axis"] = axisReferenceToJson(*target.axis);
        break;
    case MateTargetKind::Face:
        json["face"] = faceNameToJson(*target.face);
        break;
    }
    return json;
}

[[nodiscard]] Result<MateTarget> targetFromJson(const Json& value, std::string_view path) {
    if (auto valid = requireObject(value, path, {"component", "kind", "plane", "axis", "face"}); !valid) {
        return std::unexpected(valid.error());
    }
    auto component = readId(value, "component", path);
    if (!component) {
        return std::unexpected(component.error());
    }
    auto kind = valueIn(kTargetKinds, value, "kind", path);
    if (!kind) {
        return std::unexpected(kind.error());
    }
    MateTarget target{.component = ComponentId::fromValue(*component), .kind = *kind};
    switch (*kind) {
    case MateTargetKind::Plane: {
        auto field = requireField(value, "plane", path);
        if (!field) {
            return std::unexpected(field.error());
        }
        auto plane = planeReferenceFromJson(**field, childPath(path, "plane"));
        if (!plane) {
            return std::unexpected(plane.error());
        }
        target.plane = *plane;
        break;
    }
    case MateTargetKind::Axis: {
        auto field = requireField(value, "axis", path);
        if (!field) {
            return std::unexpected(field.error());
        }
        auto axis = axisReferenceFromJson(**field, childPath(path, "axis"));
        if (!axis) {
            return std::unexpected(axis.error());
        }
        target.axis = *axis;
        break;
    }
    case MateTargetKind::Face: {
        auto field = requireField(value, "face", path);
        if (!field) {
            return std::unexpected(field.error());
        }
        auto face = faceNameFromJson(**field, childPath(path, "face"));
        if (!face) {
            return std::unexpected(face.error());
        }
        target.face = *face;
        break;
    }
    }
    return target;
}

} // namespace

// DocumentJson.cpp dispatches on the literal "mate"; this keeps that literal
// and the type name from drifting apart, as the component mapping does.
static_assert(assembly::Mate::kTypeName == "mate",
              "the mate type name and the name the reader dispatches on must agree");

Json mateToJson(const assembly::Mate& mate) {
    const assembly::MateDefinition& d = mate.definition();
    Json json = Json::object();
    json["type"] = std::string{nameIn(kMateTypes, d.type)};
    // Exactly the keys the kind calls for: what validation requires is what
    // is written, so a file cannot describe a mate the model would refuse.
    if (d.component.isValid()) {
        json["component"] = d.component.value();
    }
    if (d.a) {
        json["a"] = targetToJson(*d.a);
    }
    if (d.b) {
        json["b"] = targetToJson(*d.b);
    }
    if (d.a2) {
        json["a2"] = targetToJson(*d.a2);
    }
    if (d.b2) {
        json["b2"] = targetToJson(*d.b2);
    }
    if (d.distance) {
        json["distance"] = d.distance->si();
    }
    if (d.angle) {
        json["angle"] = d.angle->si();
    }
    if (d.suppressed) {
        json["suppressed"] = true;
    }
    return json;
}

Result<std::unique_ptr<assembly::Mate>> mateFromJson(const Json& data, std::string name, std::string_view path) {
    if (auto valid = requireObject(
            data, path, {"type", "component", "a", "b", "a2", "b2", "distance", "angle", "suppressed"});
        !valid) {
        return std::unexpected(valid.error());
    }
    auto type = valueIn(kMateTypes, data, "type", path);
    if (!type) {
        return std::unexpected(type.error());
    }
    assembly::MateDefinition d{.type = *type};

    if (const auto found = data.find("component"); found != data.end()) {
        auto component = readId(data, "component", path);
        if (!component) {
            return std::unexpected(component.error());
        }
        d.component = ComponentId::fromValue(*component);
    }
    for (const auto& [key, slot] :
         {std::pair{"a", &d.a}, std::pair{"b", &d.b}, std::pair{"a2", &d.a2}, std::pair{"b2", &d.b2}}) {
        if (const auto found = data.find(key); found != data.end()) {
            auto target = targetFromJson(*found, childPath(path, key));
            if (!target) {
                return std::unexpected(target.error());
            }
            *slot = *target;
        }
    }
    if (const auto found = data.find("distance"); found != data.end()) {
        auto distance = readNumber(data, "distance", path);
        if (!distance) {
            return std::unexpected(distance.error());
        }
        d.distance = Length::fromSi(*distance);
    }
    if (const auto found = data.find("angle"); found != data.end()) {
        auto angle = readNumber(data, "angle", path);
        if (!angle) {
            return std::unexpected(angle.error());
        }
        d.angle = Angle::fromSi(*angle);
    }
    if (const auto found = data.find("suppressed"); found != data.end()) {
        if (!found->is_boolean()) {
            return parseError(childPath(path, "suppressed"), "must be a boolean");
        }
        d.suppressed = found->get<bool>();
    }
    // Mate::create() validates, so a file describing an impossible mate is a
    // parse failure rather than a mate the rest of the system must tolerate.
    auto mate = assembly::Mate::create(std::move(name), d);
    if (!mate) {
        return atPath(path, mate.error());
    }
    return std::move(*mate);
}

} // namespace bettercad::io::detail
