#include <bettercad/core/document/References.hpp>

#include <format>

namespace bettercad {

std::string_view toString(PrincipalPlane plane) noexcept {
    switch (plane) {
    case PrincipalPlane::XY:
        return "xy";
    case PrincipalPlane::YZ:
        return "yz";
    case PrincipalPlane::XZ:
        return "xz";
    }
    return "unknown";
}

std::string_view toString(FaceRole role) noexcept {
    switch (role) {
    case FaceRole::StartCap:
        return "start_cap";
    case FaceRole::EndCap:
        return "end_cap";
    case FaceRole::Side:
        return "side";
    }
    return "unknown";
}

Result<void> validate(const FaceSelector& selector) {
    if (selector.role == FaceRole::Side) {
        if (!selector.entity || !selector.entity->isValid()) {
            return makeError(ErrorCode::InvalidArgument, "a side face is named by a valid profile entity");
        }
    } else if (selector.entity) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{} is not named by an entity",
                                     selector.role == FaceRole::StartCap ? "a start cap" : "an end cap"));
    }
    return {};
}

Result<void> validate(const PlaneReference& reference) {
    if (reference.object && !reference.object->isValid()) {
        return makeError(ErrorCode::InvalidArgument, "a plane reference must name a valid object");
    }
    if (!reference.face) {
        return {};
    }
    if (!reference.object) {
        return makeError(ErrorCode::InvalidArgument, "a face reference must name the feature that generates the face");
    }
    if (reference.plane != PrincipalPlane::XY) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("a face reference has no principal plane of its own, got {}",
                                     toString(reference.plane)));
    }
    return validate(*reference.face);
}

std::string_view toString(PrincipalAxis axis) noexcept {
    switch (axis) {
    case PrincipalAxis::X:
        return "x";
    case PrincipalAxis::Y:
        return "y";
    case PrincipalAxis::Z:
        return "z";
    }
    return "unknown";
}

} // namespace bettercad
