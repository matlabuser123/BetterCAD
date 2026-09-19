#include <bettercad/core/document/MateReference.hpp>

#include <algorithm>
#include <format>

namespace bettercad {
namespace {

/// Appends @p id if it is valid and not already there, so a target that
/// names one object twice declares one edge.
void push(std::vector<ObjectId>& into, ObjectId id) {
    if (id.isValid() && std::ranges::find(into, id) == into.end()) {
        into.push_back(id);
    }
}

} // namespace

std::string_view toString(MateTargetKind kind) noexcept {
    switch (kind) {
    case MateTargetKind::Plane:
        return "plane";
    case MateTargetKind::Axis:
        return "axis";
    case MateTargetKind::Face:
        return "face";
    }
    return "unknown";
}

MateTarget planeTarget(ComponentId component, const PlaneReference& reference) {
    return {.component = component, .kind = MateTargetKind::Plane, .plane = reference};
}

MateTarget axisTarget(ComponentId component, const AxisReference& reference) {
    return {.component = component, .kind = MateTargetKind::Axis, .axis = reference};
}

MateTarget faceTarget(ComponentId component, const FaceName& name) {
    return {.component = component, .kind = MateTargetKind::Face, .face = name};
}

Result<void> validate(const MateTarget& target) {
    if (!target.component.isValid()) {
        return makeError(ErrorCode::InvalidArgument, "a mate target must name the component it is on");
    }
    // Exactly one reference, and the one its kind calls for. Checked both
    // ways: the right one present, and the others absent, so a target cannot
    // carry a stale reference of another kind alongside the live one.
    const bool plane = target.plane.has_value();
    const bool axis = target.axis.has_value();
    const bool face = target.face.has_value();
    const int count = static_cast<int>(plane) + static_cast<int>(axis) + static_cast<int>(face);
    if (count != 1) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("a mate target must name exactly one piece of geometry, not {}", count));
    }
    switch (target.kind) {
    case MateTargetKind::Plane:
        if (!plane) {
            return makeError(ErrorCode::InvalidArgument, "a plane target must name a plane");
        }
        return validate(*target.plane);
    case MateTargetKind::Axis:
        if (!axis) {
            return makeError(ErrorCode::InvalidArgument, "an axis target must name an axis");
        }
        // An AxisReference is a principal axis of an optional object; there
        // is nothing about it that can be self-inconsistent.
        return {};
    case MateTargetKind::Face:
        if (!face) {
            return makeError(ErrorCode::InvalidArgument, "a face target must name a face");
        }
        if (!target.face->feature.isValid()) {
            return makeError(ErrorCode::InvalidArgument, "a face target must name the feature that generates it");
        }
        return validate(target.face->face);
    }
    return makeError(ErrorCode::InvalidArgument, "unknown mate target kind");
}

bool hasDirection(const MateTarget& target) noexcept {
    switch (target.kind) {
    case MateTargetKind::Plane:
    case MateTargetKind::Face:
    case MateTargetKind::Axis:
        return true;
    }
    return false;
}

bool isPlanar(const MateTarget& target) noexcept {
    return target.kind == MateTargetKind::Plane || target.kind == MateTargetKind::Face;
}

std::vector<ObjectId> referencedObjects(const MateTarget& target) {
    std::vector<ObjectId> found;
    push(found, ObjectId{target.component});
    switch (target.kind) {
    case MateTargetKind::Plane:
        if (target.plane) {
            for (const ObjectId id : referencedObjects(*target.plane)) {
                push(found, id);
            }
        }
        break;
    case MateTargetKind::Axis:
        if (target.axis && target.axis->object) {
            push(found, *target.axis->object);
        }
        break;
    case MateTargetKind::Face:
        if (target.face) {
            push(found, target.face->feature);
            // The copies, in the order they were made: the last copy's
            // feature is the one that holds the face.
            for (const FaceCopy& copy : target.face->face.copies) {
                push(found, copy.feature);
            }
        }
        break;
    }
    return found;
}

} // namespace bettercad
