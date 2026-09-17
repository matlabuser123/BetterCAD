#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/geometry/Profile.hpp>
#include <bettercad/features/Export.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <vector>

namespace bettercad::features {

/// Closed-profile detection: turns the sketch's non-construction lines, arcs,
/// circles, ellipses and splines into planar regions (outer loops with holes)
/// on the sketch plane.
///
/// Circles, ellipses and periodic splines are loops on their own. Lines, arcs
/// and open splines are edges: they connect where their end points coincide
/// within the sketch tolerance, whether they share a point entity or were
/// made coincident by constraints. Every end must meet exactly one other end;
/// an open spline may end where it starts. Loops nested inside other loops
/// become holes; loops inside holes become separate regions.
///
/// Fails with FailedPrecondition if the sketch has no closed loop, an open
/// end or a branch (three or more edges meeting), and with InvalidArgument
/// for degenerate edges and invalid splines.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<std::vector<geometry::PlanarRegion>>
extractRegions(const sketch::Sketch& sketch);

/// A region with the sketch entity of each of its segments.
struct LabelledRegion {
    geometry::PlanarRegion region;
    /// outer[i] made region.outer.segments[i]; holes[h][i] made
    /// region.holes[h].segments[i].
    std::vector<EntityId> outer;
    std::vector<std::vector<EntityId>> holes;
};

/// extractRegions() with the entity of every segment (P12-STREF-001), so
/// that features can name the faces each entity generates. The regions are
/// the ones extractRegions() returns.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<std::vector<LabelledRegion>>
extractLabelledRegions(const sketch::Sketch& sketch);

} // namespace bettercad::features
