#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/geometry/Profile.hpp>
#include <bettercad/core/units/Units.hpp>

#include <span>
#include <vector>

// Kernel-independent planning of a loft (see makeLoft()): where the sections
// lie, which point of each section goes to which point of the next, and the
// checks that keep the lofted solid from folding. BetterCAD decides these;
// the kernel only builds.
namespace bettercad::geometry::detail {

struct LoftPlan {
    /// The sections, in order, ready to build: each outer loop is
    /// counter-clockwise about the loft direction and starts at the segment
    /// that matches the previous section's first segment. Every plane has the
    /// loft direction as its normal and the first section's X axis as its X
    /// axis, so circles start (have their seams) at the same angle.
    std::vector<PlanarRegion> sections;
    /// Sum over consecutive sections of h/6 (A0 + 4 Am + A1): the ruled
    /// loft's volume (prismatoid formula; exact because the area of its
    /// cross-section is quadratic in the height).
    Volume expectedVolume{};
};

/// Checks @p sections and plans the loft. Fails with InvalidArgument
/// ("makeLoft: ...") for every preflight rule of makeLoft(). The sections'
/// loops must already be checked.
[[nodiscard]] Result<LoftPlan> planLoft(std::span<const PlanarRegion> sections);

} // namespace bettercad::geometry::detail
