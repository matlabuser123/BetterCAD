#pragma once

#include <bettercad/core/Export.hpp>
#include <bettercad/core/math/Point.hpp>

#include <optional>

namespace bettercad {

/// A unit-length direction in model space.
class BETTERCAD_CORE_EXPORT Direction3D {
public:
    /// Normalized (x, y, z); std::nullopt for a zero or non-finite vector.
    [[nodiscard]] static std::optional<Direction3D> fromComponents(double x, double y,
                                                                   double z) noexcept;
    /// Components that already form a unit vector (within 1e-12), stored
    /// unchanged. Used to restore saved directions bit for bit.
    [[nodiscard]] static std::optional<Direction3D> fromUnitComponents(double x, double y,
                                                                       double z) noexcept;

    [[nodiscard]] static constexpr Direction3D unitX() noexcept { return {1.0, 0.0, 0.0}; }
    [[nodiscard]] static constexpr Direction3D unitY() noexcept { return {0.0, 1.0, 0.0}; }
    [[nodiscard]] static constexpr Direction3D unitZ() noexcept { return {0.0, 0.0, 1.0}; }

    [[nodiscard]] constexpr double x() const noexcept { return x_; }
    [[nodiscard]] constexpr double y() const noexcept { return y_; }
    [[nodiscard]] constexpr double z() const noexcept { return z_; }

    [[nodiscard]] constexpr Direction3D reversed() const noexcept { return {-x_, -y_, -z_}; }
    [[nodiscard]] constexpr double dot(const Direction3D& other) const noexcept {
        return x_ * other.x_ + y_ * other.y_ + z_ * other.z_;
    }
    /// Normalized cross product; std::nullopt if the directions are parallel.
    [[nodiscard]] std::optional<Direction3D> cross(const Direction3D& other) const noexcept;

    friend constexpr bool operator==(const Direction3D&, const Direction3D&) = default;

private:
    constexpr Direction3D(double x, double y, double z) noexcept : x_(x), y_(y), z_(z) {}

    double x_;
    double y_;
    double z_;
};

/// An axis: an origin and a direction.
struct Axis3D {
    Point3D origin{};
    Direction3D direction = Direction3D::unitZ();

    friend constexpr bool operator==(const Axis3D&, const Axis3D&) = default;
};

} // namespace bettercad
