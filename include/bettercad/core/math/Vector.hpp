#pragma once

#include <bettercad/core/math/Direction.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/units/Units.hpp>

#include <span>
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

/// A force in model space, resolved onto X, Y and Z.
///
/// The sibling of Translation3D, and placed beside it for the same reason that
/// one exists: three strongly-typed components say what a bare three doubles
/// cannot, and a Force cannot be assigned from a Length. Added by P17-DATA-001
/// for structural reactions and loads, in `core` rather than in `structural`
/// because a force is an engineering quantity and not a structural-analysis
/// concept -- P18's heat flux will want the same treatment.
struct Force3D {
    Force x{};
    Force y{};
    Force z{};

    /// @p magnitude along @p direction: each component is one product.
    [[nodiscard]] static constexpr Force3D along(const Direction3D& direction, Force magnitude) noexcept {
        return {magnitude * direction.x(), magnitude * direction.y(), magnitude * direction.z()};
    }

    friend constexpr bool operator==(const Force3D&, const Force3D&) = default;
};

[[nodiscard]] constexpr Force3D operator+(const Force3D& a, const Force3D& b) noexcept {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]] constexpr Force3D operator-(const Force3D& a, const Force3D& b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

[[nodiscard]] inline bool isFinite(const Force3D& f) noexcept {
    return isFinite(f.x) && isFinite(f.y) && isFinite(f.z);
}

/// The sum of a set of forces, in the order given.
///
/// ORDER MATTERS AND IS THE CALLER'S, because floating-point addition is not
/// associative: summing reactions in ascending NodeId gives one answer and
/// summing them in hash order gives another, and an equilibrium check that
/// disagreed with itself between runs would be worthless. Every caller in
/// BetterCAD iterates a mesh, whose enumeration is ascending and deterministic.
[[nodiscard]] inline Force3D sum(std::span<const Force3D> forces) noexcept {
    Force3D total{};
    for (const Force3D& f : forces) {
        total = total + f;
    }
    return total;
}

[[nodiscard]] inline bool isFinite(const Translation3D& t) noexcept {
    return isFinite(t.x) && isFinite(t.y) && isFinite(t.z);
}

} // namespace bettercad
