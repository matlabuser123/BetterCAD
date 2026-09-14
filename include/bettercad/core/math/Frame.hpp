#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Export.hpp>
#include <bettercad/core/math/Direction.hpp>
#include <bettercad/core/math/Point.hpp>

namespace bettercad {

/// A right-handed orthonormal coordinate frame: an origin and X, Y and
/// normal (Z) axes with Y = normal x X.
///
/// A frame is the placement of a sketch plane: local plane coordinates (u, v)
/// map to origin + u * X + v * Y in model space.
class BETTERCAD_CORE_EXPORT Frame3D {
public:
    /// Global XY plane: X = +X, Y = +Y, normal = +Z.
    [[nodiscard]] static constexpr Frame3D xy() noexcept {
        return {Point3D{}, Direction3D::unitX(), Direction3D::unitY(), Direction3D::unitZ()};
    }
    /// Global XZ plane: X = +X, Y = +Z, normal = -Y.
    [[nodiscard]] static constexpr Frame3D xz() noexcept {
        return {Point3D{}, Direction3D::unitX(), Direction3D::unitZ(),
                Direction3D::unitY().reversed()};
    }
    /// Global YZ plane: X = +Y, Y = +Z, normal = +X.
    [[nodiscard]] static constexpr Frame3D yz() noexcept {
        return {Point3D{}, Direction3D::unitY(), Direction3D::unitZ(), Direction3D::unitX()};
    }

    /// Frame at @p origin with the given normal. @p xDirection is projected
    /// into the plane; fails with InvalidArgument if it is (nearly) parallel
    /// to the normal or the origin is not finite.
    [[nodiscard]] static Result<Frame3D> create(const Point3D& origin, const Direction3D& normal,
                                                const Direction3D& xDirection);

    /// Frame from explicit axes, stored unchanged after checking that they are
    /// orthonormal and right-handed within 1e-12. Used to restore saved
    /// frames bit for bit.
    [[nodiscard]] static Result<Frame3D> fromAxes(const Point3D& origin, const Direction3D& xAxis,
                                                  const Direction3D& yAxis, const Direction3D& normal);

    [[nodiscard]] constexpr const Point3D& origin() const noexcept { return origin_; }
    [[nodiscard]] constexpr const Direction3D& xAxis() const noexcept { return x_; }
    [[nodiscard]] constexpr const Direction3D& yAxis() const noexcept { return y_; }
    [[nodiscard]] constexpr const Direction3D& normal() const noexcept { return normal_; }

    /// Model-space position of local plane coordinates.
    [[nodiscard]] Point3D toGlobal(const Point2D& local) const noexcept;
    /// Local coordinates of the point's orthogonal projection onto the plane.
    [[nodiscard]] Point2D toLocal(const Point3D& global) const noexcept;
    /// Signed distance from the plane, positive on the normal side.
    [[nodiscard]] Length signedDistance(const Point3D& global) const noexcept;

    friend constexpr bool operator==(const Frame3D&, const Frame3D&) = default;

private:
    constexpr Frame3D(const Point3D& origin, const Direction3D& x, const Direction3D& y,
                      const Direction3D& normal) noexcept
        : origin_(origin), x_(x), y_(y), normal_(normal) {}

    Point3D origin_;
    Direction3D x_;
    Direction3D y_;
    Direction3D normal_;
};

} // namespace bettercad
