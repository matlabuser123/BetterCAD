#include "core/geometry/EdgeMatching.hpp"

#include <bettercad/core/geometry/Chamfer.hpp>
#include <bettercad/core/units/Format.hpp>

#include <cmath>
#include <format>
#include <numbers>

namespace bettercad::geometry {

namespace {

struct Vec {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

Vec vec(const Point3D& p) {
    return {p.x.si(), p.y.si(), p.z.si()};
}

Vec vec(const Direction3D& d) {
    return {d.x(), d.y(), d.z()};
}

double dot(const Vec& a, const Vec& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec cross(const Vec& a, const Vec& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

double norm(const Vec& a) {
    return std::sqrt(dot(a, a));
}

Vec minus(const Vec& a, const Vec& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

/// The direction or its reverse: the one whose first component that is
/// clearly non-zero is positive.
Direction3D canonical(const Direction3D& d) {
    for (const double c : {d.x(), d.y(), d.z()}) {
        if (std::abs(c) > detail::kEdgeAngularTolerance) {
            return c > 0.0 ? d : d.reversed();
        }
    }
    return d;
}

bool parallel(const Direction3D& a, const Direction3D& b) {
    return norm(cross(vec(a), vec(b))) <= detail::kEdgeAngularTolerance;
}

bool finite(const Point3D& p) {
    return isFinite(p.x) && isFinite(p.y) && isFinite(p.z);
}

std::string formatMm(const Point3D& p) {
    return std::format("({:.6g}, {:.6g}, {:.6g}) mm", p.x.in(units::mm), p.y.in(units::mm), p.z.in(units::mm));
}

std::string format(const Direction3D& d) {
    return std::format("({:.6g}, {:.6g}, {:.6g})", d.x(), d.y(), d.z());
}

} // namespace

namespace detail {

bool sameCurve(const EdgeSignature& a, const EdgeSignature& b) noexcept {
    if (a.curve != b.curve || !parallel(a.direction, b.direction)) {
        return false;
    }
    if (a.curve == EdgeCurve::Line) {
        // a's point lies on b's line.
        return norm(cross(minus(vec(a.point), vec(b.point)), vec(b.direction))) <= kEdgeLengthTolerance;
    }
    return norm(minus(vec(a.point), vec(b.point))) <= kEdgeLengthTolerance &&
           std::abs((a.radius - b.radius).si()) <= kEdgeLengthTolerance;
}

} // namespace detail

std::string_view toString(EdgeCurve curve) noexcept {
    switch (curve) {
    case EdgeCurve::Line:
        return "line";
    case EdgeCurve::Circle:
        return "circle";
    case EdgeCurve::Other:
        return "other";
    }
    return "unknown";
}

EdgeSignature lineSignature(const Point3D& point, const Direction3D& direction) {
    const Direction3D d = canonical(direction);
    // The point of the line nearest the origin: remove the component along d.
    const Vec p = vec(point);
    const Vec u = vec(d);
    const double t = dot(p, u);
    const Point3D nearest{Length::fromSi(p.x - t * u.x), Length::fromSi(p.y - t * u.y),
                          Length::fromSi(p.z - t * u.z)};
    return {.curve = EdgeCurve::Line, .point = nearest, .direction = d, .radius = {}};
}

Result<EdgeSignature> circleSignature(const Point3D& center, const Direction3D& axis, Length radius) {
    EdgeSignature signature{
        .curve = EdgeCurve::Circle, .point = center, .direction = canonical(axis), .radius = radius};
    if (auto valid = validate(signature); !valid) {
        return std::unexpected(valid.error());
    }
    return signature;
}

Result<void> validate(const EdgeSignature& signature) {
    if (signature.curve == EdgeCurve::Other) {
        return makeError(ErrorCode::InvalidArgument, "only line and circle edges can be referenced");
    }
    if (!finite(signature.point)) {
        return makeError(ErrorCode::InvalidArgument, "an edge reference needs a finite point");
    }
    if (signature.curve == EdgeCurve::Circle &&
        (!isFinite(signature.radius) || signature.radius.si() <= detail::kEdgeLengthTolerance)) {
        return makeError(ErrorCode::InvalidArgument, "a circle edge reference needs a positive radius");
    }
    return {};
}

std::string describe(const EdgeSignature& signature) {
    if (signature.curve == EdgeCurve::Circle) {
        return std::format("circle around {} with axis {} and radius {:.6g} mm", formatMm(signature.point),
                           format(signature.direction), signature.radius.in(units::mm));
    }
    return std::format("{} through {} along {}", toString(signature.curve), formatMm(signature.point),
                       format(signature.direction));
}

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
    if (request.edges.empty()) {
        return makeError(ErrorCode::InvalidArgument, "a chamfer needs at least one edge");
    }
    for (std::size_t i = 0; i < request.edges.size(); ++i) {
        if (auto valid = validate(request.edges[i]); !valid) {
            return makeError(ErrorCode::InvalidArgument, std::format("edge reference {}: {}", i + 1,
                                                                     valid.error().message));
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (detail::sameCurve(request.edges[i], request.edges[j])) {
                return makeError(ErrorCode::InvalidArgument,
                                 std::format("edge references {} and {} refer to the same {}", j + 1, i + 1,
                                             describe(request.edges[i])));
            }
        }
    }
    const auto positive = [](Length value) { return isFinite(value) && value > Length{}; };
    if (!positive(request.distance)) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the chamfer distance must be positive and finite, got {}",
                                     toString(request.distance, units::mm)));
    }
    if (request.mode == ChamferMode::TwoDistance && !positive(request.distance2)) {
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

} // namespace bettercad::geometry
