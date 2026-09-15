#pragma once

#include <bettercad/core/math/Direction.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/math/Vector.hpp>
#include <bettercad/core/units/Units.hpp>

#include <array>
#include <cmath>

namespace bettercad {

/// A rigid transformation of model space (a Euclidean isometry): an
/// orthogonal matrix followed by a translation, p' = A p + t. A is a
/// rotation (det A = +1) for translations and rotations, which keep the
/// handedness of what they move, or a reflection (det A = -1) for mirrors,
/// which reverse it (reversesOrientation()). It never scales. Patterns and
/// mirrors compute one per instance, from the source, so errors never
/// accumulate.
class RigidTransform3D {
public:
    /// No motion.
    constexpr RigidTransform3D() noexcept = default;

    /// Moves every point by @p translation.
    [[nodiscard]] static constexpr RigidTransform3D translation(const Translation3D& translation) noexcept {
        RigidTransform3D motion;
        motion.translation_ = translation;
        return motion;
    }

    /// Turns every point by @p angle about @p axis: counter-clockwise when
    /// looking against the axis' direction (right-hand rule). The matrix is
    /// Rodrigues': R = cos(a) I + sin(a) [k]x + (1 - cos(a)) k k^T; the axis'
    /// points stay put.
    [[nodiscard]] static RigidTransform3D rotation(const Axis3D& axis, Angle angle) noexcept {
        const double c = std::cos(angle.si());
        const double s = std::sin(angle.si());
        const double v = 1.0 - c;
        const double x = axis.direction.x();
        const double y = axis.direction.y();
        const double z = axis.direction.z();
        RigidTransform3D motion;
        motion.matrix_ = {c + v * x * x,     v * x * y - s * z, v * x * z + s * y,
                          v * y * x + s * z, c + v * y * y,     v * y * z - s * x,
                          v * z * x - s * y, v * z * y + s * x, c + v * z * z};
        motion.fixPoint(axis.origin);
        return motion;
    }

    /// Mirrors every point across the plane through @p pointOnPlane with
    /// unit @p normal: p' = p - 2 ((p - p0) . n) n. The matrix is
    /// Householder's, A = I - 2 n n^T (det -1); the plane's points stay put.
    [[nodiscard]] static RigidTransform3D reflection(const Point3D& pointOnPlane, const Direction3D& normal) noexcept {
        const double x = normal.x();
        const double y = normal.y();
        const double z = normal.z();
        RigidTransform3D motion;
        motion.matrix_ = {1.0 - 2.0 * x * x, -2.0 * x * y,      -2.0 * x * z,
                          -2.0 * y * x,      1.0 - 2.0 * y * y, -2.0 * y * z,
                          -2.0 * z * x,      -2.0 * z * y,      1.0 - 2.0 * z * z};
        motion.fixPoint(pointOnPlane);
        return motion;
    }

    [[nodiscard]] Point3D apply(const Point3D& point) const noexcept { return multiply(point) + translation_; }

    /// Directions turn (or mirror) with the matrix; the translation does not
    /// move them.
    [[nodiscard]] Direction3D apply(const Direction3D& direction) const noexcept {
        const auto& r = matrix_;
        const double x = r[0] * direction.x() + r[1] * direction.y() + r[2] * direction.z();
        const double y = r[3] * direction.x() + r[4] * direction.y() + r[5] * direction.z();
        const double z = r[6] * direction.x() + r[7] * direction.y() + r[8] * direction.z();
        // A is orthonormal to rounding, so the length stays 1 to rounding.
        if (const auto unit = Direction3D::fromUnitComponents(x, y, z)) {
            return *unit;
        }
        return Direction3D::fromComponents(x, y, z).value_or(direction);
    }

    /// Whether the matrix is exactly the identity (a pure translation).
    [[nodiscard]] bool isTranslation() const noexcept { return matrix_ == kIdentity; }

    /// Whether the transformation mirrors (det A < 0): a right-handed frame
    /// becomes left-handed, and a solid's faces must be turned inside out to
    /// keep pointing out of its material.
    [[nodiscard]] bool reversesOrientation() const noexcept {
        const auto& r = matrix_;
        const double det = r[0] * (r[4] * r[8] - r[5] * r[7]) - r[1] * (r[3] * r[8] - r[5] * r[6]) +
                           r[2] * (r[3] * r[7] - r[4] * r[6]);
        return det < 0.0;
    }

    /// The matrix A, row by row: a rotation, or a reflection when
    /// reversesOrientation().
    [[nodiscard]] const std::array<double, 9>& matrix() const noexcept { return matrix_; }
    [[nodiscard]] const Translation3D& translationPart() const noexcept { return translation_; }

    friend bool operator==(const RigidTransform3D&, const RigidTransform3D&) = default;

private:
    static constexpr std::array<double, 9> kIdentity{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};

    [[nodiscard]] Point3D multiply(const Point3D& p) const noexcept {
        const auto& r = matrix_;
        const double x = p.x.si();
        const double y = p.y.si();
        const double z = p.z.si();
        return {Length::fromSi(r[0] * x + r[1] * y + r[2] * z), Length::fromSi(r[3] * x + r[4] * y + r[5] * z),
                Length::fromSi(r[6] * x + r[7] * y + r[8] * z)};
    }

    /// t = o - A o, so that @p fixed maps to itself (a point of the axis or
    /// the plane).
    void fixPoint(const Point3D& fixed) noexcept {
        const Point3D moved = multiply(fixed);
        translation_ = {fixed.x - moved.x, fixed.y - moved.y, fixed.z - moved.z};
    }

    std::array<double, 9> matrix_ = kIdentity;
    Translation3D translation_{};
};

} // namespace bettercad
