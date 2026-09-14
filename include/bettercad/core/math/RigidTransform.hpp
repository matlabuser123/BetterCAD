#pragma once

#include <bettercad/core/math/Direction.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/math/Vector.hpp>
#include <bettercad/core/units/Units.hpp>

#include <array>
#include <cmath>

namespace bettercad {

/// A rigid motion of model space: a rotation followed by a translation,
/// p' = R p + t. It never scales or mirrors. Patterns compute one per
/// instance, from the source, so errors never accumulate.
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
        motion.rotation_ = {c + v * x * x,     v * x * y - s * z, v * x * z + s * y,
                            v * y * x + s * z, c + v * y * y,     v * y * z - s * x,
                            v * z * x - s * y, v * z * y + s * x, c + v * z * z};
        // t = o - R o, so that the axis' origin maps to itself.
        const Point3D turned = motion.rotate(axis.origin);
        motion.translation_ = {axis.origin.x - turned.x, axis.origin.y - turned.y, axis.origin.z - turned.z};
        return motion;
    }

    [[nodiscard]] Point3D apply(const Point3D& point) const noexcept { return rotate(point) + translation_; }

    /// Directions turn with the rotation; the translation does not move them.
    [[nodiscard]] Direction3D apply(const Direction3D& direction) const noexcept {
        const auto& r = rotation_;
        const double x = r[0] * direction.x() + r[1] * direction.y() + r[2] * direction.z();
        const double y = r[3] * direction.x() + r[4] * direction.y() + r[5] * direction.z();
        const double z = r[6] * direction.x() + r[7] * direction.y() + r[8] * direction.z();
        // R is orthonormal to rounding, so the length stays 1 to rounding.
        if (const auto unit = Direction3D::fromUnitComponents(x, y, z)) {
            return *unit;
        }
        return Direction3D::fromComponents(x, y, z).value_or(direction);
    }

    /// Whether the rotation is exactly the identity (a pure translation).
    [[nodiscard]] bool isTranslation() const noexcept { return rotation_ == kIdentity; }

    /// The rotation's matrix, row by row.
    [[nodiscard]] const std::array<double, 9>& rotationMatrix() const noexcept { return rotation_; }
    [[nodiscard]] const Translation3D& translationPart() const noexcept { return translation_; }

    friend bool operator==(const RigidTransform3D&, const RigidTransform3D&) = default;

private:
    static constexpr std::array<double, 9> kIdentity{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};

    [[nodiscard]] Point3D rotate(const Point3D& p) const noexcept {
        const auto& r = rotation_;
        const double x = p.x.si();
        const double y = p.y.si();
        const double z = p.z.si();
        return {Length::fromSi(r[0] * x + r[1] * y + r[2] * z), Length::fromSi(r[3] * x + r[4] * y + r[5] * z),
                Length::fromSi(r[6] * x + r[7] * y + r[8] * z)};
    }

    std::array<double, 9> rotation_ = kIdentity;
    Translation3D translation_{};
};

} // namespace bettercad
