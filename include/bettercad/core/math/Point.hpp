#pragma once

#include <bettercad/core/units/Units.hpp>

#include <cmath>

namespace bettercad {

/// A position in a plane, e.g. in sketch coordinates.
struct Point2D {
    Length x{};
    Length y{};

    friend constexpr bool operator==(const Point2D&, const Point2D&) = default;
};

/// A position in model space.
struct Point3D {
    Length x{};
    Length y{};
    Length z{};

    friend constexpr bool operator==(const Point3D&, const Point3D&) = default;
};

[[nodiscard]] inline Length distance(const Point2D& a, const Point2D& b) noexcept {
    return Length::fromSi(std::hypot((b.x - a.x).si(), (b.y - a.y).si()));
}

[[nodiscard]] inline Length distance(const Point3D& a, const Point3D& b) noexcept {
    return Length::fromSi(std::hypot((b.x - a.x).si(), (b.y - a.y).si(), (b.z - a.z).si()));
}

[[nodiscard]] constexpr Point2D midpoint(const Point2D& a, const Point2D& b) noexcept {
    return {(a.x + b.x) / 2.0, (a.y + b.y) / 2.0};
}

} // namespace bettercad
