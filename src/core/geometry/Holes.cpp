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

} // namespace

std::string_view toString(HoleType type) noexcept {
    switch (type) {
    case HoleType::Simple:
        return "simple";
    case HoleType::Counterbore:
        return "counterbore";
    case HoleType::Countersink:
        return "countersink";
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

HoleRequest translated(const HoleRequest& request, const Translation3D& translation) {
    const Point3D centre = facePoint(request.face, request.center) + translation;
    HoleRequest moved = request;
    moved.face = translated(request.face, translation);
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
    if (!counterbore && (request.counterboreDiameter != Length{} || request.counterboreDepth != Length{})) {
        return makeError(ErrorCode::InvalidArgument, "only a counterbore hole takes counterbore dimensions");
    }
    if (!countersink && (request.countersinkDiameter != Length{} || request.countersinkAngle != Angle{})) {
        return makeError(ErrorCode::InvalidArgument, "only a countersink hole takes countersink dimensions");
    }
    if (counterbore) {
        if (!isFinite(request.counterboreDiameter) || !(request.counterboreDiameter > request.diameter)) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("the counterbore diameter must be larger than the hole diameter ({}), got {}",
                                         mm(request.diameter), mm(request.counterboreDiameter)));
        }
        if (!positiveAndFinite(request.counterboreDepth)) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("the counterbore depth must be positive and finite, got {}",
                                         mm(request.counterboreDepth)));
        }
        if (blind && !(request.counterboreDepth < request.depth)) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("the counterbore ({} deep) must be shallower than the blind hole ({} deep)",
                                         mm(request.counterboreDepth), mm(request.depth)));
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
    return {};
}

} // namespace bettercad::geometry
