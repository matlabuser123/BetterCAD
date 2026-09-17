#include "io/json/ObjectJson.hpp"

#include <array>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace bettercad::io::detail {

namespace {

using features::CoordinateSystemKind;
using features::DatumAxisKind;
using features::DatumPlaneKind;

constexpr std::array<std::pair<PrincipalPlane, std::string_view>, 3> kPlanes{{
    {PrincipalPlane::XY, "xy"},
    {PrincipalPlane::YZ, "yz"},
    {PrincipalPlane::XZ, "xz"},
}};
constexpr std::array<std::pair<FaceRole, std::string_view>, 6> kFaceRoles{{
    {FaceRole::StartCap, "start_cap"},
    {FaceRole::EndCap, "end_cap"},
    {FaceRole::Side, "side"},
    {FaceRole::HoleBottom, "hole_bottom"},
    {FaceRole::CounterboreFloor, "counterbore_floor"},
    {FaceRole::Chamfer, "chamfer"},
}};
constexpr std::array<std::pair<PrincipalAxis, std::string_view>, 3> kAxes{{
    {PrincipalAxis::X, "x"},
    {PrincipalAxis::Y, "y"},
    {PrincipalAxis::Z, "z"},
}};
constexpr std::array<std::pair<DatumPlaneKind, std::string_view>, 3> kPlaneKinds{{
    {DatumPlaneKind::Fixed, "fixed"},
    {DatumPlaneKind::Offset, "offset"},
    {DatumPlaneKind::Angled, "angled"},
}};
constexpr std::array<std::pair<DatumAxisKind, std::string_view>, 2> kAxisKinds{{
    {DatumAxisKind::Fixed, "fixed"},
    {DatumAxisKind::Intersection, "intersection"},
}};
constexpr std::array<std::pair<CoordinateSystemKind, std::string_view>, 2> kSystemKinds{{
    {CoordinateSystemKind::Fixed, "fixed"},
    {CoordinateSystemKind::Offset, "offset"},
}};

template <typename Enum, std::size_t N>
std::string_view nameIn(const std::array<std::pair<Enum, std::string_view>, N>& table, Enum value) {
    for (const auto& [item, name] : table) {
        if (item == value) {
            return name;
        }
    }
    return "unknown";
}

template <typename Enum, std::size_t N>
Result<Enum> valueIn(const std::array<std::pair<Enum, std::string_view>, N>& table, const Json& object,
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
    return parseError(childPath(path, key), std::format("unknown value '{}'", *text));
}

std::optional<ParameterId> parameterOf(const std::optional<std::uint64_t>& id) {
    return id ? std::optional<ParameterId>{ParameterId::fromValue(*id)} : std::nullopt;
}

/// Adds "<key>": metres or radians and "<key>_parameter": id when set.
template <QuantityType Q>
void putValue(Json& json, std::string_view key, Q value, const std::optional<ParameterId>& parameter) {
    json[std::string{key}] = value.si();
    if (parameter) {
        json[std::format("{}_parameter", key)] = parameter->value();
    }
}

Json axisToJson(const Axis3D& axis) {
    Json json = Json::object();
    json["origin"] = pointToJson(axis.origin);
    json["direction"] = directionToJson(axis.direction);
    return json;
}

Result<Axis3D> axisFromJson(const Json& object, std::string_view key, std::string_view path) {
    auto field = requireField(object, key, path);
    if (!field) {
        return std::unexpected(field.error());
    }
    const std::string axisPath = childPath(path, key);
    if (auto valid = requireObject(**field, axisPath, {"origin", "direction"}); !valid) {
        return std::unexpected(valid.error());
    }
    auto origin = pointFromJson(**field, "origin", axisPath);
    auto direction = directionFromJson(**field, "direction", axisPath);
    if (!origin || !direction) {
        return std::unexpected(!origin ? origin.error() : direction.error());
    }
    return Axis3D{*origin, *direction};
}

Result<Frame3D> frameField(const Json& object, std::string_view key, std::string_view path) {
    auto field = requireField(object, key, path);
    if (!field) {
        return std::unexpected(field.error());
    }
    return frameFromJson(**field, childPath(path, key));
}

Result<PlaneReference> planeField(const Json& object, std::string_view key, std::string_view path) {
    auto field = requireField(object, key, path);
    if (!field) {
        return std::unexpected(field.error());
    }
    return planeReferenceFromJson(**field, childPath(path, key));
}

Result<AxisReference> axisField(const Json& object, std::string_view key, std::string_view path) {
    auto field = requireField(object, key, path);
    if (!field) {
        return std::unexpected(field.error());
    }
    return axisReferenceFromJson(**field, childPath(path, key));
}


/// Returns the first error among @p results.
const Error* firstError(std::initializer_list<const Error*> errors) {
    for (const Error* error : errors) {
        if (error != nullptr) {
            return error;
        }
    }
    return nullptr;
}

template <typename T>
const Error* errorOf(const Result<T>& result) {
    return result ? nullptr : &result.error();
}

/// An absent key, or a non-negative integer that fits in 32 bits.
Result<std::optional<std::uint32_t>> readOptionalCount(const Json& object, std::string_view key,
                                                       std::string_view path) {
    if (!object.contains(key)) {
        return std::optional<std::uint32_t>{};
    }
    auto value = readUnsigned(object, key, path);
    if (!value) {
        return std::unexpected(value.error());
    }
    if (*value > std::numeric_limits<std::uint32_t>::max()) {
        return parseError(childPath(path, key), "expected an integer below 2^32");
    }
    return std::optional<std::uint32_t>{static_cast<std::uint32_t>(*value)};
}

/// The copy steps of a face: [{"feature": id, "instance": n}, ...].
Result<std::vector<FaceCopy>> faceCopiesFromJson(const Json& face, std::string_view path) {
    std::vector<FaceCopy> copies;
    if (!face.contains("copies")) {
        return copies;
    }
    auto field = requireArray(face, "copies", path);
    if (!field) {
        return std::unexpected(field.error());
    }
    const std::string copiesPath = childPath(path, "copies");
    for (std::size_t i = 0; i < (*field)->size(); ++i) {
        const Json& item = (**field)[i];
        const std::string itemPath = indexPath(copiesPath, i);
        if (auto valid = requireObject(item, itemPath, {"feature", "instance"}); !valid) {
            return std::unexpected(valid.error());
        }
        auto feature = readId(item, "feature", itemPath);
        if (!feature) {
            return std::unexpected(feature.error());
        }
        auto instance = readOptionalCount(item, "instance", itemPath);
        if (!instance) {
            return std::unexpected(instance.error());
        }
        if (!*instance) {
            return parseError(childPath(itemPath, "instance"), "missing required field");
        }
        copies.push_back(FaceCopy{ObjectId::fromValue(*feature), **instance});
    }
    return copies;
}

} // namespace

Json planeReferenceToJson(const PlaneReference& reference) {
    Json json = Json::object();
    if (reference.object) {
        json["object"] = reference.object->value();
    }
    if (reference.face) {
        Json face = Json::object();
        face["role"] = std::string{nameIn(kFaceRoles, reference.face->role)};
        if (reference.face->entity) {
            face["entity"] = reference.face->entity->value();
        }
        if (reference.face->along) {
            face["along"] = reference.face->along->value();
        }
        if (reference.face->edge) {
            face["edge"] = *reference.face->edge;
        }
        if (!reference.face->copies.empty()) {
            Json copies = Json::array();
            for (const FaceCopy& copy : reference.face->copies) {
                Json item = Json::object();
                item["feature"] = copy.feature.value();
                item["instance"] = copy.instance;
                copies.push_back(std::move(item));
            }
            face["copies"] = std::move(copies);
        }
        json["face"] = std::move(face);
    } else {
        json["plane"] = std::string{nameIn(kPlanes, reference.plane)};
    }
    return json;
}

Result<PlaneReference> planeReferenceFromJson(const Json& value, std::string_view path) {
    if (auto valid = requireObject(value, path, {"object", "plane", "face"}); !valid) {
        return std::unexpected(valid.error());
    }
    auto object = readOptionalId(value, "object", path);
    if (!object) {
        return std::unexpected(object.error());
    }
    PlaneReference reference{*object ? std::optional<ObjectId>{ObjectId::fromValue(**object)} : std::nullopt};
    if (!value.contains("face")) {
        auto plane = valueIn(kPlanes, value, "plane", path);
        if (!plane) {
            return std::unexpected(plane.error());
        }
        reference.plane = *plane;
        return reference;
    }
    // A face of a feature (P12-STREF-001, P12-SKETCH-003): {"role": ...,
    // "entity": id, "along": id, "edge": n, "copies": [...]}.
    if (value.contains("plane")) {
        return parseError(childPath(path, "plane"), "a face reference has no plane of its own");
    }
    const std::string facePath = childPath(path, "face");
    const Json& face = value.at("face");
    if (auto valid = requireObject(face, facePath, {"role", "entity", "along", "edge", "copies"}); !valid) {
        return std::unexpected(valid.error());
    }
    auto role = valueIn(kFaceRoles, face, "role", facePath);
    auto entity = readOptionalId(face, "entity", facePath);
    auto along = readOptionalId(face, "along", facePath);
    auto edge = readOptionalCount(face, "edge", facePath);
    auto copies = faceCopiesFromJson(face, facePath);
    if (const Error* error =
            firstError({errorOf(role), errorOf(entity), errorOf(along), errorOf(edge), errorOf(copies)})) {
        return std::unexpected(*error);
    }
    const auto entityOf = [](const std::optional<std::uint64_t>& id) {
        return id ? std::optional<EntityId>{EntityId::fromValue(*id)} : std::nullopt;
    };
    reference.face = FaceSelector{.role = *role,
                                  .entity = entityOf(*entity),
                                  .along = entityOf(*along),
                                  .edge = *edge,
                                  .copies = std::move(*copies)};
    if (auto valid = validate(reference); !valid) {
        return parseError(path, valid.error().message);
    }
    return reference;
}

Json axisReferenceToJson(const AxisReference& reference) {
    Json json = Json::object();
    if (reference.object) {
        json["object"] = reference.object->value();
    }
    json["axis"] = std::string{nameIn(kAxes, reference.axis)};
    return json;
}

Result<AxisReference> axisReferenceFromJson(const Json& value, std::string_view path) {
    if (auto valid = requireObject(value, path, {"object", "axis"}); !valid) {
        return std::unexpected(valid.error());
    }
    auto object = readOptionalId(value, "object", path);
    auto axis = valueIn(kAxes, value, "axis", path);
    if (!object || !axis) {
        return std::unexpected(!object ? object.error() : axis.error());
    }
    return AxisReference{*object ? std::optional<ObjectId>{ObjectId::fromValue(**object)} : std::nullopt, *axis};
}

// --- Datum planes ------------------------------------------------------------------------------------

Json datumPlaneToJson(const features::DatumPlane& datum) {
    const features::DatumPlaneDefinition& d = datum.definition();
    Json json = Json::object();
    json["kind"] = std::string{nameIn(kPlaneKinds, d.kind)};
    switch (d.kind) {
    case DatumPlaneKind::Fixed:
        json["frame"] = frameToJson(d.frame);
        break;
    case DatumPlaneKind::Offset:
        json["base"] = planeReferenceToJson(d.base);
        putValue(json, "offset", d.offset, d.offsetParameter);
        break;
    case DatumPlaneKind::Angled:
        json["base"] = planeReferenceToJson(d.base);
        json["axis"] = axisReferenceToJson(d.axis);
        putValue(json, "angle", d.angle, d.angleParameter);
        break;
    }
    return json;
}

Result<std::unique_ptr<features::DatumPlane>> datumPlaneFromJson(const Json& data, std::string name,
                                                                 std::string_view path) {
    auto kind = valueIn(kPlaneKinds, data, "kind", path);
    if (!kind) {
        return std::unexpected(kind.error());
    }
    features::DatumPlaneDefinition d{.kind = *kind};
    switch (*kind) {
    case DatumPlaneKind::Fixed: {
        if (auto valid = requireObject(data, path, {"kind", "frame"}); !valid) {
            return std::unexpected(valid.error());
        }
        auto frame = frameField(data, "frame", path);
        if (!frame) {
            return std::unexpected(frame.error());
        }
        d.frame = *frame;
        break;
    }
    case DatumPlaneKind::Offset: {
        if (auto valid = requireObject(data, path, {"kind", "base", "offset", "offset_parameter"}); !valid) {
            return std::unexpected(valid.error());
        }
        auto base = planeField(data, "base", path);
        auto offset = readNumber(data, "offset", path);
        auto parameter = readOptionalId(data, "offset_parameter", path);
        if (const Error* error = firstError({errorOf(base), errorOf(offset), errorOf(parameter)})) {
            return std::unexpected(*error);
        }
        d.base = *base;
        d.offset = Length::fromSi(*offset);
        d.offsetParameter = parameterOf(*parameter);
        break;
    }
    case DatumPlaneKind::Angled: {
        if (auto valid = requireObject(data, path, {"kind", "base", "axis", "angle", "angle_parameter"}); !valid) {
            return std::unexpected(valid.error());
        }
        auto base = planeField(data, "base", path);
        auto axis = axisField(data, "axis", path);
        auto angle = readNumber(data, "angle", path);
        auto parameter = readOptionalId(data, "angle_parameter", path);
        if (const Error* error = firstError({errorOf(base), errorOf(axis), errorOf(angle), errorOf(parameter)})) {
            return std::unexpected(*error);
        }
        d.base = *base;
        d.axis = *axis;
        d.angle = Angle::fromSi(*angle);
        d.angleParameter = parameterOf(*parameter);
        break;
    }
    }
    auto datum = features::DatumPlane::create(std::move(name), d);
    if (!datum) {
        return atPath(path, datum.error());
    }
    return std::move(*datum);
}

// --- Datum axes -----------------------------------------------------------------------------------------

Json datumAxisToJson(const features::DatumAxis& datum) {
    const features::DatumAxisDefinition& d = datum.definition();
    Json json = Json::object();
    json["kind"] = std::string{nameIn(kAxisKinds, d.kind)};
    if (d.kind == DatumAxisKind::Fixed) {
        json["axis"] = axisToJson(d.axis);
    } else {
        json["first"] = planeReferenceToJson(d.first);
        json["second"] = planeReferenceToJson(d.second);
    }
    return json;
}

Result<std::unique_ptr<features::DatumAxis>> datumAxisFromJson(const Json& data, std::string name,
                                                               std::string_view path) {
    auto kind = valueIn(kAxisKinds, data, "kind", path);
    if (!kind) {
        return std::unexpected(kind.error());
    }
    features::DatumAxisDefinition d{.kind = *kind};
    if (*kind == DatumAxisKind::Fixed) {
        if (auto valid = requireObject(data, path, {"kind", "axis"}); !valid) {
            return std::unexpected(valid.error());
        }
        auto axis = axisFromJson(data, "axis", path);
        if (!axis) {
            return std::unexpected(axis.error());
        }
        d.axis = *axis;
    } else {
        if (auto valid = requireObject(data, path, {"kind", "first", "second"}); !valid) {
            return std::unexpected(valid.error());
        }
        auto first = planeField(data, "first", path);
        auto second = planeField(data, "second", path);
        if (!first || !second) {
            return std::unexpected(!first ? first.error() : second.error());
        }
        d.first = *first;
        d.second = *second;
    }
    auto datum = features::DatumAxis::create(std::move(name), d);
    if (!datum) {
        return atPath(path, datum.error());
    }
    return std::move(*datum);
}

// --- Coordinate systems -------------------------------------------------------------------------------------

Json coordinateSystemToJson(const features::CoordinateSystem& system) {
    const features::CoordinateSystemDefinition& d = system.definition();
    Json json = Json::object();
    json["kind"] = std::string{nameIn(kSystemKinds, d.kind)};
    if (d.kind == CoordinateSystemKind::Fixed) {
        json["frame"] = frameToJson(d.frame);
        return json;
    }
    if (d.base) {
        json["base"] = d.base->value();
    }
    static constexpr std::array<std::string_view, 3> kTranslations{"x", "y", "z"};
    static constexpr std::array<std::string_view, 3> kRotations{"rx", "ry", "rz"};
    for (std::size_t i = 0; i < 3; ++i) {
        putValue(json, kTranslations[i], d.translation[i], d.translationParameters[i]);
    }
    for (std::size_t i = 0; i < 3; ++i) {
        putValue(json, kRotations[i], d.rotation[i], d.rotationParameters[i]);
    }
    return json;
}

Result<std::unique_ptr<features::CoordinateSystem>> coordinateSystemFromJson(const Json& data, std::string name,
                                                                             std::string_view path) {
    auto kind = valueIn(kSystemKinds, data, "kind", path);
    if (!kind) {
        return std::unexpected(kind.error());
    }
    features::CoordinateSystemDefinition d{.kind = *kind};
    if (*kind == CoordinateSystemKind::Fixed) {
        if (auto valid = requireObject(data, path, {"kind", "frame"}); !valid) {
            return std::unexpected(valid.error());
        }
        auto frame = frameField(data, "frame", path);
        if (!frame) {
            return std::unexpected(frame.error());
        }
        d.frame = *frame;
    } else {
        if (auto valid = requireObject(data, path,
                                       {"kind", "base", "x", "y", "z", "rx", "ry", "rz", "x_parameter", "y_parameter",
                                        "z_parameter", "rx_parameter", "ry_parameter", "rz_parameter"});
            !valid) {
            return std::unexpected(valid.error());
        }
        auto base = readOptionalId(data, "base", path);
        if (!base) {
            return std::unexpected(base.error());
        }
        d.base = *base ? std::optional<ObjectId>{ObjectId::fromValue(**base)} : std::nullopt;
        static constexpr std::array<std::string_view, 3> kTranslations{"x", "y", "z"};
        static constexpr std::array<std::string_view, 3> kRotations{"rx", "ry", "rz"};
        for (std::size_t i = 0; i < 3; ++i) {
            auto t = readNumber(data, kTranslations[i], path);
            auto tp = readOptionalId(data, std::format("{}_parameter", kTranslations[i]), path);
            auto r = readNumber(data, kRotations[i], path);
            auto rp = readOptionalId(data, std::format("{}_parameter", kRotations[i]), path);
            if (const Error* error = firstError({errorOf(t), errorOf(tp), errorOf(r), errorOf(rp)})) {
                return std::unexpected(*error);
            }
            d.translation[i] = Length::fromSi(*t);
            d.translationParameters[i] = parameterOf(*tp);
            d.rotation[i] = Angle::fromSi(*r);
            d.rotationParameters[i] = parameterOf(*rp);
        }
    }
    auto system = features::CoordinateSystem::create(std::move(name), d);
    if (!system) {
        return atPath(path, system.error());
    }
    return std::move(*system);
}

} // namespace bettercad::io::detail
