#include <bettercad/core/document/References.hpp>

#include <algorithm>
#include <format>
#include <string>
#include <utility>

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
    case FaceRole::HoleBottom:
        return "hole_bottom";
    case FaceRole::CounterboreFloor:
        return "counterbore_floor";
    case FaceRole::Chamfer:
        return "chamfer";
    case FaceRole::SpotfaceFloor:
        return "spotface_floor";
    }
    return "unknown";
}

namespace {

/// "a start cap", "a hole bottom", ...
std::string_view roleWithArticle(FaceRole role) noexcept {
    switch (role) {
    case FaceRole::StartCap:
        return "a start cap";
    case FaceRole::EndCap:
        return "an end cap";
    case FaceRole::Side:
        return "a side face";
    case FaceRole::HoleBottom:
        return "a hole bottom";
    case FaceRole::CounterboreFloor:
        return "a counterbore floor";
    case FaceRole::Chamfer:
        return "a chamfer face";
    case FaceRole::SpotfaceFloor:
        return "a spotface floor";
    }
    return "a face";
}

} // namespace

Result<void> validate(const FaceSelector& selector) {
    const auto invalid = [](std::string message) { return makeError(ErrorCode::InvalidArgument, std::move(message)); };
    if (selector.role == FaceRole::Side) {
        if (!selector.entity || !selector.entity->isValid()) {
            return invalid("a side face is named by a valid profile entity");
        }
        if (selector.along && !selector.along->isValid()) {
            return invalid("a side face's path edge must be a valid entity");
        }
        if (selector.alongSketch && !selector.alongSketch->isValid()) {
            return invalid("a side face's path sketch must be a valid sketch");
        }
        if (selector.alongSketch && !selector.along) {
            return invalid("a side face's path sketch names no edge without one");
        }
    } else if (selector.entity) {
        return invalid(std::format("{} is not named by an entity", roleWithArticle(selector.role)));
    } else if (selector.along || selector.alongSketch) {
        return invalid(std::format("{} is not named by a path edge", roleWithArticle(selector.role)));
    }
    if (selector.role == FaceRole::Chamfer) {
        if (!selector.edge || *selector.edge == 0) {
            return invalid("a chamfer face is named by its edge reference, from 1");
        }
    } else if (selector.edge) {
        return invalid(std::format("{} is not named by an edge reference", roleWithArticle(selector.role)));
    }
    for (const FaceCopy& copy : selector.copies) {
        if (!copy.feature.isValid()) {
            return invalid("a copy must name a valid feature");
        }
        if (copy.instance == 0) {
            return invalid("a copy is an instance from 1 (instance 0 is the original)");
        }
    }
    return {};
}

std::vector<ObjectId> referencedObjects(const PlaneReference& reference) {
    std::vector<ObjectId> objects;
    const auto push = [&](ObjectId id) {
        if (std::ranges::find(objects, id) == objects.end()) {
            objects.push_back(id);
        }
    };
    if (reference.object) {
        push(*reference.object);
    }
    if (reference.face) {
        for (const FaceCopy& copy : reference.face->copies) {
            push(copy.feature);
        }
    }
    return objects;
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
