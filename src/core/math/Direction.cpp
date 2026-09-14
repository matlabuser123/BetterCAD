#include <bettercad/core/math/Direction.hpp>

#include <cmath>

namespace bettercad {

std::optional<Direction3D> Direction3D::fromComponents(double x, double y, double z) noexcept {
    const double length = std::hypot(x, y, z);
    if (!std::isfinite(length) || length == 0.0) {
        return std::nullopt;
    }
    return Direction3D{x / length, y / length, z / length};
}

std::optional<Direction3D> Direction3D::fromUnitComponents(double x, double y, double z) noexcept {
    const double length = std::hypot(x, y, z);
    if (!std::isfinite(length) || std::abs(length - 1.0) > 1e-12) {
        return std::nullopt;
    }
    return Direction3D{x, y, z};
}

std::optional<Direction3D> Direction3D::cross(const Direction3D& other) const noexcept {
    const double cx = y_ * other.z_ - z_ * other.y_;
    const double cy = z_ * other.x_ - x_ * other.z_;
    const double cz = x_ * other.y_ - y_ * other.x_;
    // |a x b| = sin(angle); treat nearly parallel directions as parallel.
    constexpr double kParallelSine = 1e-12;
    if (std::hypot(cx, cy, cz) < kParallelSine) {
        return std::nullopt;
    }
    return fromComponents(cx, cy, cz);
}

} // namespace bettercad
