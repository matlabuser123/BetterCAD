#pragma once

#include <bettercad/core/geometry/Profile.hpp>

#include <cmath>

// How far a planar profile reaches to either side of a line in its plane.
// Shared by revolutions (the profile against the axis) and sweeps (the
// profile against the path).
namespace bettercad::geometry::detail {

/// A line in a plane's local coordinates (metres), with unit direction
/// (dx, dy). side(p) is the signed distance of p from the line, positive to
/// the left of the direction.
struct PlaneAxis {
    double ox = 0.0;
    double oy = 0.0;
    double dx = 1.0;
    double dy = 0.0;

    [[nodiscard]] double side(const Point2D& p) const noexcept {
        return dx * (p.y.si() - oy) - dy * (p.x.si() - ox);
    }
    /// Angle of the unit normal pointing to the positive side.
    [[nodiscard]] double normalAngle() const noexcept { return std::atan2(dx, -dy); }
};

/// Whether @p angle lies on the counter-clockwise sweep from @p start to @p end.
[[nodiscard]] bool onCounterClockwiseArc(double angle, double start, double end);

/// Signed sweep of an arc: in (0, 2 pi) counter-clockwise, (-2 pi, 0) clockwise.
[[nodiscard]] double arcSweep(const ArcSegment2D& arc);

/// Extends [low, high] by the signed distances of every point of @p loop
/// from the line: segment ends, plus the points of arcs and circles that are
/// farthest from it on either side.
void extendSideRange(const ProfileLoop& loop, const PlaneAxis& axis, double& low, double& high);

} // namespace bettercad::geometry::detail
