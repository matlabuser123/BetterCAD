// Validation of chamfer and fillet requests: everything that can be checked
// without a body.
#include "core/geometry/EdgeMatching.hpp"

#include <bettercad/core/geometry/Chamfer.hpp>
#include <bettercad/core/geometry/Fillet.hpp>
#include <bettercad/core/units/Format.hpp>

#include <format>
#include <numbers>

namespace bettercad::geometry {

namespace {

bool positiveAndFinite(Length value) {
    return isFinite(value) && value > Length{};
}

} // namespace

std::string_view toString(ChamferMode mode) noexcept {
    switch (mode) {
    case ChamferMode::EqualDistance:
        return "equal distance";
    case ChamferMode::TwoDistance:
        return "two distances";
    case ChamferMode::DistanceAngle:
        return "distance and angle";
    }
    return "unknown";
}

Result<void> validate(const ChamferRequest& request) {
    if (auto edges = detail::validateEdgeSelection("chamfer", request.edges); !edges) {
        return edges;
    }
    if (!positiveAndFinite(request.distance)) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the chamfer distance must be positive and finite, got {}",
                                     toString(request.distance, units::mm)));
    }
    if (request.mode == ChamferMode::TwoDistance && !positiveAndFinite(request.distance2)) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the second chamfer distance must be positive and finite, got {}",
                                     toString(request.distance2, units::mm)));
    }
    if (request.mode == ChamferMode::DistanceAngle &&
        (!isFinite(request.angle) || request.angle <= Angle{} || request.angle.si() >= std::numbers::pi / 2.0)) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the chamfer angle must be in (0, 90) deg, got {}",
                                     toString(request.angle, units::deg)));
    }
    const bool needsSide = request.mode != ChamferMode::EqualDistance;
    if (needsSide && !request.referenceSide) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("a chamfer by {} needs a reference side", toString(request.mode)));
    }
    if (!needsSide && request.referenceSide) {
        return makeError(ErrorCode::InvalidArgument, "an equal-distance chamfer takes no reference side");
    }
    return {};
}

Result<void> validate(const FilletRequest& request) {
    if (auto edges = detail::validateEdgeSelection("fillet", request.edges); !edges) {
        return edges;
    }
    if (!positiveAndFinite(request.radius)) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the fillet radius must be positive and finite, got {}",
                                     toString(request.radius, units::mm)));
    }
    return {};
}

} // namespace bettercad::geometry
