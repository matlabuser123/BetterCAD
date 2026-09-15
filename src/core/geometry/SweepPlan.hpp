#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/geometry/Sweeps.hpp>
#include <bettercad/core/units/Units.hpp>

#include <vector>

// Kernel-independent planning of a sweep (see makeSweep()): the path's
// checks, how its segments meet, and the checks that keep the swept solid
// from folding over itself. BetterCAD decides these; the kernel only builds.
namespace bettercad::geometry::detail {

/// How two consecutive path segments meet.
enum class PathJoint {
    Smooth, ///< tangent-continuous
    Corner, ///< two straight segments at an angle: a mitred corner
};

struct SweepPlan {
    /// The last segment ends where the first starts (a full circle is closed).
    bool closed = false;
    /// joints[i] joins segment i to segment i + 1; for a closed path of two
    /// or more segments, the last entry joins the last segment to the first.
    std::vector<PathJoint> joints;
    /// The region's area times the length of the path its centroid travels:
    /// the swept solid's volume if it does not intersect itself (Pappus).
    Volume expectedVolume{};
};

/// Checks @p path and how @p region sits on it, and plans the sweep. Fails
/// with InvalidArgument ("makeSweep: ...") for every preflight rule of
/// makeSweep(). @p region's loops must already be checked.
[[nodiscard]] Result<SweepPlan> planSweep(const PlanarRegion& region, const PlanarPath& path);

} // namespace bettercad::geometry::detail
