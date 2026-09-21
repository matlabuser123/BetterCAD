#include "Selectors.hpp"

#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Mate.hpp>
#include <bettercad/core/Naming.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/features/Datums.hpp>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace bettercad::cli {

namespace {

/// A selector of digits only. No object name can be one, because
/// validateIdentifier() requires a letter or an underscore first -- which is
/// what makes IDs and names disjoint without a sigil (ADR-009).
bool isIdValue(std::string_view text) noexcept {
    return !text.empty() && std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; });
}

Result<std::uint64_t> parseIdValue(std::string_view text, std::string_view what) {
    std::uint64_t value = 0;
    const auto* const end = text.data() + text.size();
    const auto [stop, code] = std::from_chars(text.data(), end, value);
    if (code != std::errc{} || stop != end) {
        return makeError(ErrorCode::InvalidArgument, std::format("'{}' is not a usable {} ID", text, what));
    }
    if (value == 0) {
        return makeError(ErrorCode::InvalidArgument, std::format("0 is not a valid {} ID", what));
    }
    return value;
}

/// The selector grammar, in the one message that has to teach it.
Error badSelector(std::string_view selector) {
    return Error{ErrorCode::InvalidArgument,
                 std::format("'{}' is neither an ID (digits) nor a name (a letter or _ followed by "
                             "letters, digits or _)",
                             selector)};
}

/// Splits on @p separator, without allocating a string per field.
std::vector<std::string_view> split(std::string_view text, char separator) {
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    while (true) {
        const std::size_t at = text.find(separator, start);
        if (at == std::string_view::npos) {
            fields.push_back(text.substr(start));
            return fields;
        }
        fields.push_back(text.substr(start, at - start));
        start = at + 1;
    }
}

Result<PrincipalPlane> parsePrincipalPlane(std::string_view text) {
    if (text == "xy") {
        return PrincipalPlane::XY;
    }
    if (text == "yz") {
        return PrincipalPlane::YZ;
    }
    if (text == "xz") {
        return PrincipalPlane::XZ;
    }
    return makeError(ErrorCode::InvalidArgument, std::format("'{}' is not a plane; expected xy, yz or xz", text));
}

Result<PrincipalAxis> parsePrincipalAxis(std::string_view text) {
    if (text == "x") {
        return PrincipalAxis::X;
    }
    if (text == "y") {
        return PrincipalAxis::Y;
    }
    if (text == "z") {
        return PrincipalAxis::Z;
    }
    return makeError(ErrorCode::InvalidArgument, std::format("'{}' is not an axis; expected x, y or z", text));
}

bool isAxisWord(std::string_view text) noexcept { return text == "x" || text == "y" || text == "z"; }

Result<FaceRole> parseFaceRole(std::string_view text) {
    if (text == "start_cap") {
        return FaceRole::StartCap;
    }
    if (text == "end_cap") {
        return FaceRole::EndCap;
    }
    if (text == "side") {
        return FaceRole::Side;
    }
    if (text == "hole_bottom") {
        return FaceRole::HoleBottom;
    }
    if (text == "counterbore_floor") {
        return FaceRole::CounterboreFloor;
    }
    if (text == "chamfer") {
        return FaceRole::Chamfer;
    }
    if (text == "spotface_floor") {
        return FaceRole::SpotfaceFloor;
    }
    return makeError(ErrorCode::InvalidArgument,
                     std::format("'{}' is not a face role; expected start_cap, end_cap, side, hole_bottom, "
                                 "counterbore_floor, chamfer or spotface_floor",
                                 text));
}

/// The geometry half of a target, with `component` already resolved.
Result<MateTarget> parseGeometry(const Document& document, ComponentId component,
                                 std::span<const std::string_view> fields, std::string_view whole) {
    const std::string_view kind = fields.front();
    const std::span<const std::string_view> rest = fields.subspan(1);

    if (kind == "origin") {
        if (rest.size() != 1) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("'{}': origin takes one plane or axis, e.g. origin:xy or origin:z", whole));
        }
        if (isAxisWord(rest[0])) {
            auto axis = parsePrincipalAxis(rest[0]);
            if (!axis) {
                return std::unexpected(axis.error());
            }
            return axisTarget(component, AxisReference{.object = std::nullopt, .axis = *axis});
        }
        auto plane = parsePrincipalPlane(rest[0]);
        if (!plane) {
            return std::unexpected(plane.error());
        }
        return planeTarget(component, PlaneReference{.object = std::nullopt, .plane = *plane, .face = std::nullopt});
    }

    if (kind == "datum") {
        if (rest.size() != 1) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("'{}': datum takes one selector, e.g. datum:TopPlane", whole));
        }
        auto object = resolveObject(document, rest[0]);
        if (!object) {
            return std::unexpected(object.error());
        }
        // Which reference kind it is comes from what the object IS, not from a
        // word the engineer has to repeat -- a datum plane cannot be an axis
        // reference, so asking would only create a way to be wrong.
        if (document.findObjectAs<features::DatumPlane>(*object) != nullptr) {
            return planeTarget(component,
                               PlaneReference{.object = *object, .plane = PrincipalPlane::XY, .face = std::nullopt});
        }
        if (document.findObjectAs<features::DatumAxis>(*object) != nullptr) {
            return axisTarget(component, AxisReference{.object = *object, .axis = PrincipalAxis::Z});
        }
        return makeError(
            ErrorCode::FailedPrecondition,
            std::format("{} has type '{}'; a mate target needs a datum plane or a datum axis",
                        label(document, *object), document.findObject(*object)->typeName()));
    }

    if (kind == "csys") {
        if (rest.size() != 2) {
            return makeError(
                ErrorCode::InvalidArgument,
                std::format("'{}': csys takes a selector and a plane or axis, e.g. csys:Station:yz", whole));
        }
        auto object = resolveObject(document, rest[0]);
        if (!object) {
            return std::unexpected(object.error());
        }
        if (document.findObjectAs<features::CoordinateSystem>(*object) == nullptr) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("{} has type '{}'; csys needs a coordinate system", label(document, *object),
                                         document.findObject(*object)->typeName()));
        }
        if (isAxisWord(rest[1])) {
            auto axis = parsePrincipalAxis(rest[1]);
            if (!axis) {
                return std::unexpected(axis.error());
            }
            return axisTarget(component, AxisReference{.object = *object, .axis = *axis});
        }
        auto plane = parsePrincipalPlane(rest[1]);
        if (!plane) {
            return std::unexpected(plane.error());
        }
        return planeTarget(component, PlaneReference{.object = *object, .plane = *plane, .face = std::nullopt});
    }

    if (kind == "face") {
        if (rest.size() < 2 || rest.size() > 3) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("'{}': face takes a feature and a role, and a side face an entity, "
                                         "e.g. face:Block:end_cap or face:Block:side:5",
                                         whole));
        }
        auto feature = resolveObject(document, rest[0]);
        if (!feature) {
            return std::unexpected(feature.error());
        }
        auto role = parseFaceRole(rest[1]);
        if (!role) {
            return std::unexpected(role.error());
        }
        FaceSelector selector{.role = *role};
        if (rest.size() == 3) {
            if (*role != FaceRole::Side) {
                return makeError(ErrorCode::InvalidArgument,
                                 std::format("'{}': only a side face is named by a profile entity", whole));
            }
            auto entity = parseIdValue(rest[2], "entity");
            if (!entity) {
                return std::unexpected(entity.error());
            }
            selector.entity = EntityId::fromValue(*entity);
        }
        return faceTarget(component, FaceName{.feature = *feature, .face = selector});
    }

    return makeError(
        ErrorCode::InvalidArgument,
        std::format("'{}': '{}' is not a geometry kind; expected origin, datum, csys or face", whole, kind));
}

/// A name where the object has one, its ID otherwise. Both are selectors, so
/// the result can always be read back.
std::string selectorFor(const Document& document, ObjectId id) {
    const DocumentObject* found = document.findObject(id);
    return found == nullptr ? std::format("{}", id.value()) : found->name();
}

/// "face:Block:side:5". A copied face has no spelling in this grammar, so it
/// is marked rather than written as the original -- which would parse back to
/// a different face.
std::string formatFaceSelector(const Document& document, ObjectId feature, const FaceSelector& face) {
    std::string text = std::format("face:{}:{}", selectorFor(document, feature), toString(face.role));
    if (face.role == FaceRole::Side && face.entity) {
        text += std::format(":{}", face.entity->value());
    }
    if (!face.copies.empty()) {
        text += ":...";
    }
    return text;
}

} // namespace

Result<ObjectId> resolveObject(const Document& document, std::string_view selector) {
    if (isIdValue(selector)) {
        auto value = parseIdValue(selector, "object");
        if (!value) {
            return std::unexpected(value.error());
        }
        const ObjectId id = ObjectId::fromValue(*value);
        if (document.findObject(id) == nullptr) {
            return makeError(ErrorCode::NotFound, std::format("this document has no {}", id));
        }
        return id;
    }
    if (!validateIdentifier(selector, "object")) {
        return std::unexpected(badSelector(selector));
    }
    const DocumentObject* found = document.findObjectByName(selector);
    if (found == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("this document has nothing named '{}'", selector));
    }
    return found->id();
}

Result<ComponentId> resolveComponent(const Document& document, std::string_view selector) {
    auto object = resolveObject(document, selector);
    if (!object) {
        return std::unexpected(object.error());
    }
    const auto* component = document.findObjectAs<assembly::Component>(*object);
    if (component == nullptr) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("{} has type '{}'; this takes a component", label(document, *object),
                                     document.findObject(*object)->typeName()));
    }
    return component->componentId();
}

Result<MateId> resolveMate(const Document& document, std::string_view selector) {
    auto object = resolveObject(document, selector);
    if (!object) {
        return std::unexpected(object.error());
    }
    const auto* mate = document.findObjectAs<assembly::Mate>(*object);
    if (mate == nullptr) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("{} has type '{}'; this takes a mate", label(document, *object),
                                     document.findObject(*object)->typeName()));
    }
    return mate->mateId();
}

Result<ConfigurationId> resolveConfiguration(const Document& document, std::string_view name) {
    const Configuration* found = document.configurations().findByName(name);
    if (found == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("no configuration named '{}' in this document", name));
    }
    return found->id();
}

Result<MateTarget> parseMateTarget(const Document& document, std::string_view text) {
    const std::vector<std::string_view> fields = split(text, ':');
    if (fields.size() < 2) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("'{}' is not a mate target; expected <component>:<geometry>, e.g. "
                                     "Base:origin:xy or Arm:face:Block:end_cap",
                                     text));
    }
    auto component = resolveComponent(document, fields.front());
    if (!component) {
        return std::unexpected(component.error());
    }
    auto target = parseGeometry(document, *component, std::span{fields}.subspan(1), text);
    if (!target) {
        return std::unexpected(target.error());
    }
    // Shape only: that the geometry exists and belongs to this component's
    // part is assembly::checkMate()'s job, and is not restated here.
    if (auto valid = validate(*target); !valid) {
        return std::unexpected(valid.error());
    }
    return *target;
}

std::string formatMateTarget(const Document& document, const MateTarget& target) {
    const std::string component = selectorFor(document, ObjectId{target.component});
    switch (target.kind) {
    case MateTargetKind::Plane: {
        if (!target.plane) {
            break;
        }
        const PlaneReference& plane = *target.plane;
        if (!plane.object) {
            return std::format("{}:origin:{}", component, toString(plane.plane));
        }
        if (plane.face) {
            return std::format("{}:{}", component, formatFaceSelector(document, *plane.object, *plane.face));
        }
        if (document.findObjectAs<features::CoordinateSystem>(*plane.object) != nullptr) {
            return std::format("{}:csys:{}:{}", component, selectorFor(document, *plane.object),
                               toString(plane.plane));
        }
        return std::format("{}:datum:{}", component, selectorFor(document, *plane.object));
    }
    case MateTargetKind::Axis: {
        if (!target.axis) {
            break;
        }
        const AxisReference& axis = *target.axis;
        if (!axis.object) {
            return std::format("{}:origin:{}", component, toString(axis.axis));
        }
        if (document.findObjectAs<features::CoordinateSystem>(*axis.object) != nullptr) {
            return std::format("{}:csys:{}:{}", component, selectorFor(document, *axis.object), toString(axis.axis));
        }
        return std::format("{}:datum:{}", component, selectorFor(document, *axis.object));
    }
    case MateTargetKind::Face:
        if (!target.face) {
            break;
        }
        return std::format("{}:{}", component, formatFaceSelector(document, target.face->feature, target.face->face));
    }
    // A target whose reference is missing is malformed and validate() refuses
    // it; naming the component is all that can honestly be said of one.
    return std::format("{}:<no {}>", component, toString(target.kind));
}

std::string label(const Document& document, ObjectId id) {
    const DocumentObject* found = document.findObject(id);
    if (found == nullptr) {
        return std::format("{}", id);
    }
    return std::format("{} ({})", found->name(), id);
}

} // namespace bettercad::cli
