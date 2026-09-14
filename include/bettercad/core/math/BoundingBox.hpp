#pragma once

#include <bettercad/core/math/Point.hpp>

#include <algorithm>

namespace bettercad {

/// Axis-aligned rectangle with min <= max in both coordinates.
struct BoundingBox2D {
    Point2D min{};
    Point2D max{};

    /// Degenerate box containing a single point.
    [[nodiscard]] static constexpr BoundingBox2D around(const Point2D& point) noexcept {
        return {point, point};
    }

    constexpr void include(const Point2D& point) noexcept {
        min = {std::min(min.x, point.x), std::min(min.y, point.y)};
        max = {std::max(max.x, point.x), std::max(max.y, point.y)};
    }
    constexpr void include(const BoundingBox2D& other) noexcept {
        include(other.min);
        include(other.max);
    }

    [[nodiscard]] constexpr Length width() const noexcept { return max.x - min.x; }
    [[nodiscard]] constexpr Length height() const noexcept { return max.y - min.y; }
    [[nodiscard]] constexpr bool contains(const Point2D& point) const noexcept {
        return point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y;
    }

    friend constexpr bool operator==(const BoundingBox2D&, const BoundingBox2D&) = default;
};

/// Axis-aligned box with min <= max in every coordinate.
struct BoundingBox3D {
    Point3D min{};
    Point3D max{};

    [[nodiscard]] constexpr Length sizeX() const noexcept { return max.x - min.x; }
    [[nodiscard]] constexpr Length sizeY() const noexcept { return max.y - min.y; }
    [[nodiscard]] constexpr Length sizeZ() const noexcept { return max.z - min.z; }

    friend constexpr bool operator==(const BoundingBox3D&, const BoundingBox3D&) = default;
};

} // namespace bettercad
