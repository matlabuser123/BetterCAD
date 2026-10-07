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

/// A traction in model space: force per unit area, resolved onto X, Y and Z.
///
/// The third sibling, added by P17-LOAD-001 and placed here for the reason
/// `Force3D` records above -- a traction is an engineering quantity, not a
/// structural-analysis concept. Each component is a `Pressure`, so the type
/// system knows that `traction * area` is a `Force` and that a traction cannot
/// be assigned from one.
///
/// GLOBAL COMPONENTS. A traction is expressed on the model's own X, Y and Z,
/// never on a surface-local basis: that is what makes it the load type whose
/// direction does NOT follow a rotated face, as against a pressure, whose
/// direction does. P17-LOAD-001 freezes the distinction and tests both halves.
struct Traction3D {
    Pressure x{};
    Pressure y{};
    Pressure z{};

    /// @p magnitude along @p direction: each component is one product.
    [[nodiscard]] static constexpr Traction3D along(const Direction3D& direction,
                                                    Pressure magnitude) noexcept {
        return {magnitude * direction.x(), magnitude * direction.y(), magnitude * direction.z()};
    }

    friend constexpr bool operator==(const Traction3D&, const Traction3D&) = default;
};

[[nodiscard]] constexpr Traction3D operator+(const Traction3D& a, const Traction3D& b) noexcept {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]] inline bool isFinite(const Traction3D& t) noexcept {
    return isFinite(t.x) && isFinite(t.y) && isFinite(t.z);
}

/// A moment of force about a point, resolved onto X, Y and Z.
///
/// COMPONENTS ARE `Torque`, WHICH IS AN ALIAS OF `Energy`, and saying so is the
/// point. A `Quantity` is keyed on its dimension alone and a newton-metre
/// shares a dimension with a joule, so the two are the same type -- exactly as
/// `Stress` and `ElasticModulus` are the same type as `Pressure`, which
/// `Units.hpp` documents in the same terms. The alias buys readability in a
/// signature, never a second layer of safety, and a reader who assumed
/// otherwise would be wrong in a way that matters.
///
/// Added by P17-LOAD-001, where conservation of the resultant moment is a
/// gate: a nodal load distribution can preserve the total force and still be
/// wrong, and only the first moment catches it.
struct Moment3D {
    Torque x{};
    Torque y{};
    Torque z{};

    friend constexpr bool operator==(const Moment3D&, const Moment3D&) = default;
};

[[nodiscard]] constexpr Moment3D operator+(const Moment3D& a, const Moment3D& b) noexcept {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]] inline bool isFinite(const Moment3D& m) noexcept {
    return isFinite(m.x) && isFinite(m.y) && isFinite(m.z);
}

/// The moment of @p force applied at @p lever, which is a position RELATIVE to
/// the reference point: `M = lever x force`.
///
/// The lever is a `Translation3D` rather than a `Point3D` so that the caller
/// has to form the difference, which is where the choice of reference point
/// belongs. A moment about an unstated origin is not a quantity.
[[nodiscard]] constexpr Moment3D momentOf(const Translation3D& lever,
                                          const Force3D& force) noexcept {
    return {lever.y * force.z - lever.z * force.y, lever.z * force.x - lever.x * force.z,
            lever.x * force.y - lever.y * force.x};
}

} // namespace bettercad
