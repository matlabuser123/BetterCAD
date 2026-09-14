#pragma once

#include <bettercad/core/math/Direction.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/units/Units.hpp>

#include <cmath>

namespace bettercad {

/// A dimensionless vector in model space, e.g. a direction as a user gives
/// it, before it is normalized with Direction3D::fromComponents().
struct Vector3D {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    friend constexpr bool operator==(const Vector3D&, const Vector3D&) = default;
};

[[nodiscard]] inline bool isFinite(const Vector3D& v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

/// A translation in model space: how far every point moves along X, Y and Z.
struct Translation3D {
    Length x{};
    Length y{};
    Length z{};

    /// @p distance along @p direction: each component is one product.
    [[nodiscard]] static constexpr Translation3D along(const Direction3D& direction, Length distance) noexcept {
        return {distance * direction.x(), distance * direction.y(), distance * direction.z()};
    }

    friend constexpr bool operator==(const Translation3D&, const Translation3D&) = default;
};

[[nodiscard]] constexpr Translation3D operator+(const Translation3D& a, const Translation3D& b) noexcept {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]] constexpr Point3D operator+(const Point3D& point, const Translation3D& translation) noexcept {
    return {point.x + translation.x, point.y + translation.y, point.z + translation.z};
}

[[nodiscard]] inline bool isFinite(const Translation3D& t) noexcept {
    return isFinite(t.x) && isFinite(t.y) && isFinite(t.z);
}

} // namespace bettercad
