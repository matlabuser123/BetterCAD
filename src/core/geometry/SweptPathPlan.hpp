#pragma once

#include <bettercad/core/Error.hpp>
#include "core/geometry/SweepPlan.hpp"

#include <bettercad/core/geometry/Sweeps.hpp>
#include <bettercad/core/math/Direction.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/math/Vector.hpp>
#include <bettercad/core/units/Units.hpp>

#include <cstddef>
#include <string_view>
#include <vector>

// Kernel-independent planning of a sweep along a path of several runs, with
// a twist or a guide (P12-SWEEP-001). A one-run path with neither is planned
// by planSweep() instead, so that P11-FEAT-008's solids are untouched.
//
// BetterCAD decides everything here; the kernel only builds. The rules are
// P11-FEAT-008's, applied in model space, plus the two a turning section
// adds (see makeSweep(region, SweptPath)).
namespace bettercad::geometry::detail {

/// One path segment in model space.
struct SpatialSegment {
    /// A straight segment; otherwise an arc or a full circle.
    bool straight = true;
    /// A full circle, which is a path on its own.
    bool circle = false;
    Point3D start{};
    Point3D end{};
    Direction3D startTangent = Direction3D::unitX();
    Direction3D endTangent = Direction3D::unitX();
    Length length{};
    /// Arcs and circles: the centre, the axis the section turns about
    /// (right-handed), the radius and the signed turn.
    Point3D center{};
    Direction3D axis = Direction3D::unitZ();
    Length radius{};
    Angle turn{};
    /// Where the segment came from: the run, and its place in that run.
    std::size_t run = 0;
    std::size_t inRun = 0;
};

/// A point of the path and the frame carried there, for the auxiliary spine
/// a twist is built from and for the tests that check where the section
/// points.
struct PathSample {
    /// The normalized path coordinate, 0 at the start and 1 at the end.
    double u = 0.0;
    Point3D point{};
    Direction3D tangent = Direction3D::unitX();
    /// The rotation-minimizing frame's reference direction at this point,
    /// across the tangent. The section's own X axis when the twist is zero.
    Direction3D reference = Direction3D::unitY();
};

struct SweptPlan {
    bool closed = false;
    /// joints[i] joins segment i to segment i + 1; for a closed path, the
    /// last entry joins the last segment to the first.
    std::vector<PathJoint> joints{};
    std::vector<SpatialSegment> segments{};
    /// The region's area times the path's length: the swept solid's volume,
    /// since the region's centroid rides on the path.
    Volume expectedVolume{};
    /// The farthest any point of the region lies from the path.
    Length reach{};
    Length totalLength{};
    SweepFrame frame = SweepFrame::RotationMinimizing;
    /// The unsigned turn at each corner joint, by joint index; 0 where the
    /// joint is smooth. The mitre at a corner cuts reach * tan(turn / 2)
    /// from each of the segments that meet there.
    std::vector<double> cornerTurns{};
};

/// Checks @p path and how @p region sits on it, and plans the sweep. Fails
/// with InvalidArgument ("makeSweep: ...") for every preflight rule.
/// @p region's loops must already be checked.
[[nodiscard]] Result<SweptPlan> planSweptPath(const PlanarRegion& region, const SweptPath& path);

/// The curve checks alone: the runs measured in model space, connected,
/// non-degenerate, finite, and joined smoothly or at a mitrable corner. A
/// guide is checked with this, since nothing sits on it; it carries the
/// section rather than supporting it, so it has no profile and no placement.
/// @p what names it in messages ("the guide").
[[nodiscard]] Result<SweptPlan> planPathCurve(const SweptPath& path, std::string_view what);

/// @p count points along @p path's segments, with the rotation-minimizing
/// frame carried from @p start, which must be across the first tangent. The
/// frame is transported by the double-reflection method, which is exact for
/// a circular arc and second-order accurate elsewhere; along the straight
/// and circular segments BetterCAD supports it is exact.
[[nodiscard]] std::vector<PathSample> samplePath(const std::vector<SpatialSegment>& segments,
                                                 const Direction3D& start, std::size_t count);

} // namespace bettercad::geometry::detail
