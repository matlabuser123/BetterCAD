// Validation of hole requests: everything that can be checked without a body.
#include <bettercad/core/geometry/Hole.hpp>
#include <bettercad/core/units/Format.hpp>

#include <cmath>
#include <format>
#include <numbers>

namespace bettercad::geometry {

namespace {

bool positiveAndFinite(Length value) {
    return isFinite(value) && value > Length{};
}

std::string mm(Length value) {
    return toString(value, units::mm);
}

/// The checks of a cosmetic thread against its hole (P12-HOLE-001).
Result<void> validateThread(const HoleRequest& request) {
    const CosmeticThread& thread = *request.thread;
    const auto invalid = [](std::string message) { return makeError(ErrorCode::InvalidArgument, std::move(message)); };
    if (!isFinite(thread.majorDiameter) || !(thread.majorDiameter > request.diameter)) {
        return invalid(std::format("the thread's major diameter must be larger than the hole diameter ({}), got {}",
                                   mm(request.diameter), mm(thread.majorDiameter)));
    }
    if (!isFinite(thread.length) || thread.length < Length{}) {
        return invalid(std::format("the thread length must be positive and finite, or zero for the whole hole, got {}",
                                   mm(thread.length)));
    }
    if (request.extent == HoleExtent::Blind && thread.length > request.depth) {
        return invalid(std::format("the thread ({} long) must not be longer than the blind hole ({} deep)",
                                   mm(thread.length), mm(request.depth)));
    }
    if (request.type == HoleType::Simple) {
        return {};
    }
    const std::string_view head = toString(request.type);
    if (!(entryDiameter(request) > thread.majorDiameter)) {
        return invalid(std::format("the {} diameter ({}) must be larger than the thread's major diameter ({})", head,
                                   mm(entryDiameter(request)), mm(thread.majorDiameter)));
    }
    if (thread.length != Length{} && !(thread.length > headDepth(request))) {
        // A countersink's depth is computed, so it is shown rounded.
        return invalid(std::format("the thread ({} long) must be longer than the {} ({:.6g} mm deep)",
                                   mm(thread.length), head, headDepth(request).in(units::mm)));
    }
    return {};
}

} // namespace

std::string_view toString(HoleType type) noexcept {
    switch (type) {
    case HoleType::Simple:
        return "simple";
    case HoleType::Counterbore:
        return "counterbore";
    case HoleType::Countersink:
        return "countersink";
    case HoleType::Spotface:
        return "spotface";
    }
    return "unknown";
}

std::string_view toString(HoleExtent extent) noexcept {
    switch (extent) {
    case HoleExtent::Through:
        return "through";
    case HoleExtent::Blind:
        return "blind";
    }
    return "unknown";
}

Length countersinkDepth(const HoleRequest& request) {
    return (request.countersinkDiameter - request.diameter) / 2.0 / std::tan(request.countersinkAngle.si() / 2.0);
}

Length entryDiameter(const HoleRequest& request) {
    switch (request.type) {
    case HoleType::Counterbore:
        return request.counterboreDiameter;
    case HoleType::Countersink:
        return request.countersinkDiameter;
    case HoleType::Spotface:
        return request.spotfaceDiameter;
    case HoleType::Simple:
        break;
    }
    return request.diameter;
}

Length headDepth(const HoleRequest& request) {
    switch (request.type) {
    case HoleType::Counterbore:
        return request.counterboreDepth;
    case HoleType::Countersink:
        return countersinkDepth(request);
    case HoleType::Spotface:
        return request.spotfaceDepth;
    case HoleType::Simple:
        break;
    }
    return Length{};
}

HoleRequest translated(const HoleRequest& request, const Translation3D& translation) {
    const Point3D centre = facePoint(request.face, request.center) + translation;
    HoleRequest moved = request;
    moved.face = translated(request.face, translation);
    moved.center = faceCoordinates(moved.face, centre);
    return moved;
}

HoleRequest transformed(const HoleRequest& request, const RigidTransform3D& motion) {
    if (motion.isTranslation()) {
        return translated(request, motion.translationPart());
    }
    const Point3D centre = motion.apply(facePoint(request.face, request.center));
    HoleRequest moved = request;
    moved.face = transformed(request.face, motion);
    moved.center = faceCoordinates(moved.face, centre);
    return moved;
}

Result<void> validate(const HoleRequest& request) {
    if (auto face = validate(request.face); !face) {
        return makeError(ErrorCode::InvalidArgument, std::format("placement face: {}", face.error().message));
    }
    if (!isFinite(request.center.x) || !isFinite(request.center.y)) {
        return makeError(ErrorCode::InvalidArgument, "the hole centre must be finite");
    }
    if (!positiveAndFinite(request.diameter)) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the hole diameter must be positive and finite, got {}", mm(request.diameter)));
    }
    const bool blind = request.extent == HoleExtent::Blind;
    if (blind && !positiveAndFinite(request.depth)) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the hole depth must be positive and finite, got {}", mm(request.depth)));
    }
    if (!blind && request.depth != Length{}) {
        return makeError(ErrorCode::InvalidArgument, "a through hole takes no depth; it goes through all material");
    }
    const bool counterbore = request.type == HoleType::Counterbore;
    const bool countersink = request.type == HoleType::Countersink;
    const bool spotface = request.type == HoleType::Spotface;
    if (!counterbore && (request.counterboreDiameter != Length{} || request.counterboreDepth != Length{})) {
        return makeError(ErrorCode::InvalidArgument, "only a counterbore hole takes counterbore dimensions");
    }
    if (!countersink && (request.countersinkDiameter != Length{} || request.countersinkAngle != Angle{})) {
        return makeError(ErrorCode::InvalidArgument, "only a countersink hole takes countersink dimensions");
    }
    if (!spotface && (request.spotfaceDiameter != Length{} || request.spotfaceDepth != Length{})) {
        return makeError(ErrorCode::InvalidArgument, "only a spotface hole takes spotface dimensions");
    }
    // A counterbore and a spotface are both a wider cylinder at the entry.
    const auto checkCylindricalHead = [&](std::string_view head, Length diameter, Length depth) -> Result<void> {
        if (!isFinite(diameter) || !(diameter > request.diameter)) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("the {} diameter must be larger than the hole diameter ({}), got {}", head,
                                         mm(request.diameter), mm(diameter)));
        }
        if (!positiveAndFinite(depth)) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("the {} depth must be positive and finite, got {}", head, mm(depth)));
        }
        if (blind && !(depth < request.depth)) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("the {} ({} deep) must be shallower than the blind hole ({} deep)", head,
                                         mm(depth), mm(request.depth)));
        }
        return {};
    };
    if (counterbore) {
        if (auto head = checkCylindricalHead("counterbore", request.counterboreDiameter, request.counterboreDepth);
            !head) {
            return head;
        }
    }
    if (spotface) {
        if (auto head = checkCylindricalHead("spotface", request.spotfaceDiameter, request.spotfaceDepth); !head) {
            return head;
        }
    }
    if (countersink) {
        if (!isFinite(request.countersinkDiameter) || !(request.countersinkDiameter > request.diameter)) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("the countersink diameter must be larger than the hole diameter ({}), got {}",
                                         mm(request.diameter), mm(request.countersinkDiameter)));
        }
        const double angle = request.countersinkAngle.si();
        if (!std::isfinite(angle) || !(angle > 0.0) || !(angle < std::numbers::pi)) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("the countersink angle must be in (0, 180) deg, got {}",
                                         toString(request.countersinkAngle, units::deg)));
        }
        if (blind && !(countersinkDepth(request) < request.depth)) {
            // The cone's depth is computed, so it is shown rounded.
            return makeError(ErrorCode::InvalidArgument,
                             std::format("the countersink ({:.6g} mm deep) must be shallower than the blind hole ({} "
                                         "deep)",
                                         countersinkDepth(request).in(units::mm), mm(request.depth)));
        }
    }
    if (request.thread) {
        return validateThread(request);
    }
    return {};
}

} // namespace bettercad::geometry
