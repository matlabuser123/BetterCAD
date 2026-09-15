#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/geometry/Export.hpp>
#include <bettercad/core/geometry/Profile.hpp>
#include <bettercad/core/math/Direction.hpp>
#include <bettercad/core/math/Frame.hpp>
#include <bettercad/core/units/Units.hpp>

#include <span>
#include <vector>

namespace bettercad::geometry {

/// The path of a sweep: line, arc and circle segments in the local
/// coordinates of a plane (the path plane), in the order of travel, each
/// starting where the one before ends. The path runs from the first
/// segment's start (a circle starts at centre + radius along the plane's X
/// axis) and is closed when the last segment ends there. A full circle is a
/// path on its own.
struct PlanarPath {
    Frame3D plane = Frame3D::xy();
    std::vector<ProfileSegment> segments{};

    friend bool operator==(const PlanarPath&, const PlanarPath&) = default;
};

/// Solid swept by translating @p region along its plane normal between the
/// offsets @p from and @p to (to > from). For example [0, d] extrudes along
/// the normal, [-d, 0] against it and [-d/2, d/2] symmetrically.
///
/// Loop orientation is normalized (outer counter-clockwise, holes
/// clockwise). Fails with InvalidArgument for malformed loops or extents and
/// with Internal if the kernel cannot build a valid solid (for example
/// self-intersecting loops).
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<Body> makePrism(const PlanarRegion& region,
                                                              Length from, Length to);

/// Solid swept by rotating @p region about @p axis from angle @p from to
/// angle @p to, right-handed about the axis direction, with
/// 0 < to - from <= 360°. For example [0, a] revolves in the positive
/// sense, [-a, 0] in the negative sense and [-a/2, a/2] symmetrically about
/// the profile plane. A sweep of exactly 360° gives a closed solid of
/// revolution.
///
/// The axis must lie in the region's plane and the region must lie on one
/// side of it; touching the axis (e.g. an edge on the axis) is allowed.
/// Fails with InvalidArgument for an axis off the plane, a region that
/// crosses the axis, malformed loops or a sweep outside (0, 360°], and with
/// Internal if the kernel cannot build a valid solid.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<Body> makeRevolution(const PlanarRegion& region,
                                                                   const Axis3D& axis, Angle from, Angle to);

/// Solid swept by moving @p region along @p path.
///
/// Placement: the path must start on the region's plane and leave it at
/// right angles (its start tangent along the plane normal, either way). The
/// region may lie anywhere in its plane; each point keeps its offset from
/// the path.
///
/// Orientation (follow path): the region moves with a frame made of the
/// path's tangent T, the path plane's normal B, which stays fixed, and
/// N = B x T. Every point keeps its coordinates along N and B, so the
/// region turns with the path in the path plane and never twists about the
/// tangent. Along a straight segment it translates; along an arc it turns
/// about the arc's axis, as in a revolution.
///
/// Joints: segments must meet tangentially (to 1e-9 rad), except two
/// straight segments, which may meet at a corner of less than 180 deg. A
/// corner is mitred: both segments end on the plane that bisects it.
///
/// Preflight (InvalidArgument, before the kernel): an empty, non-finite,
/// degenerate or disconnected path; a corner at an arc; a path that turns
/// back on itself; a misplaced region; a region reaching the centre of an
/// arc (the solid would fold over itself); a straight segment too short for
/// the mitres at its ends; malformed loops. A swept solid that intersects
/// itself elsewhere (the path passing too close to itself) is found by the
/// kernel's self-interference check afterwards, also InvalidArgument.
///
/// The result is checked: a valid solid whose volume equals the region's
/// area times the length of the path its centroid travels (the theorem of
/// Pappus, which holds for every sweep that does not intersect itself) to
/// 1e-9 relative. Kernel failures are Internal.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<Body> makeSweep(const PlanarRegion& region, const PlanarPath& path);

/// Solid through @p sections, in the order given: a ruled loft, in which
/// straight lines join matching points of consecutive sections.
///
/// Sections: at least two; each is one closed loop of lines and arcs, or a
/// circle, without holes. Their planes must be parallel (to 1e-9 rad), and
/// the sections must follow one another along them: the loft runs from the
/// first section towards the second, and every section lies strictly beyond
/// the one before (never on the same plane, never back). The list order is
/// kept, never sorted.
///
/// Matching (correspondence): every loop is taken counter-clockwise about
/// the loft direction, whatever its sketch's orientation. Consecutive
/// sections must have the same shape: both circles, or the same lines and
/// arcs in the same cyclic order, matched arcs turning by the same angle
/// (to 1e-9 rad). A section's loop starts where its corners lie nearest to
/// the previous section's corners, relative to each section's centroid (the
/// least twist); starts whose costs tie within 1e-9 of the sections' size go
/// to the loop's first. Circles match angle for angle, from the first
/// section's X axis.
///
/// Preflight (InvalidArgument, before the kernel): fewer than two sections,
/// holes, non-finite coordinates, non-parallel planes, coincident or
/// out-of-order sections, sections of different shapes, and a loft whose
/// cross-section would lose all its area between two sections (it folds).
/// A lofted solid whose sides pass through one another elsewhere is found by
/// the kernel's self-interference check afterwards, also InvalidArgument.
///
/// The result is checked: one valid solid whose volume equals the sum over
/// consecutive sections of h/6 (A0 + 4 Am + A1) (the prismatoid formula, Am
/// the area of the section halfway) to 1e-8 relative. Kernel failures are
/// Internal.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<Body> makeLoft(std::span<const PlanarRegion> sections);

} // namespace bettercad::geometry
