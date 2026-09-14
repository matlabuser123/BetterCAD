#include <bettercad/core/math/Frame.hpp>

#include <cmath>

namespace bettercad {

Result<Frame3D> Frame3D::create(const Point3D& origin, const Direction3D& normal,
                                const Direction3D& xDirection) {
    if (!isFinite(origin.x) || !isFinite(origin.y) || !isFinite(origin.z)) {
        return makeError(ErrorCode::InvalidArgument, "frame origin must be finite");
    }
    // Remove the normal component of xDirection (Gram-Schmidt).
    const double along = xDirection.dot(normal);
    const auto x = Direction3D::fromComponents(xDirection.x() - along * normal.x(),
                                               xDirection.y() - along * normal.y(),
                                               xDirection.z() - along * normal.z());
    const auto y = x ? normal.cross(*x) : std::nullopt;
    if (!x || !y || std::abs(along) > 1.0 - 1e-9) {
        return makeError(ErrorCode::InvalidArgument,
                         "frame X direction must not be parallel to the normal");
    }
    return Frame3D{origin, *x, *y, normal};
}

Result<Frame3D> Frame3D::fromAxes(const Point3D& origin, const Direction3D& xAxis,
                                  const Direction3D& yAxis, const Direction3D& normal) {
    if (!isFinite(origin.x) || !isFinite(origin.y) || !isFinite(origin.z)) {
        return makeError(ErrorCode::InvalidArgument, "frame origin must be finite");
    }
    constexpr double kTolerance = 1e-12;
    const auto z = xAxis.cross(yAxis);
    const bool orthonormal = std::abs(xAxis.dot(yAxis)) <= kTolerance && z &&
                             std::abs(z->x() - normal.x()) <= kTolerance &&
                             std::abs(z->y() - normal.y()) <= kTolerance &&
                             std::abs(z->z() - normal.z()) <= kTolerance;
    if (!orthonormal) {
        return makeError(ErrorCode::InvalidArgument,
                         "frame axes must be orthonormal and right-handed (Y = normal x X)");
    }
    return Frame3D{origin, xAxis, yAxis, normal};
}

Point3D Frame3D::toGlobal(const Point2D& local) const noexcept {
    const double u = local.x.si();
    const double v = local.y.si();
    return {origin_.x + Length::fromSi(u * x_.x() + v * y_.x()),
            origin_.y + Length::fromSi(u * x_.y() + v * y_.y()),
            origin_.z + Length::fromSi(u * x_.z() + v * y_.z())};
}

Point2D Frame3D::toLocal(const Point3D& global) const noexcept {
    const double dx = (global.x - origin_.x).si();
    const double dy = (global.y - origin_.y).si();
    const double dz = (global.z - origin_.z).si();
    return {Length::fromSi(dx * x_.x() + dy * x_.y() + dz * x_.z()),
            Length::fromSi(dx * y_.x() + dy * y_.y() + dz * y_.z())};
}

Length Frame3D::signedDistance(const Point3D& global) const noexcept {
    const double dx = (global.x - origin_.x).si();
    const double dy = (global.y - origin_.y).si();
    const double dz = (global.z - origin_.z).si();
    return Length::fromSi(dx * normal_.x() + dy * normal_.y() + dz * normal_.z());
}

} // namespace bettercad
