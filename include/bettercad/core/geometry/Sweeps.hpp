#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/geometry/Export.hpp>
#include <bettercad/core/geometry/Profile.hpp>
#include <bettercad/core/math/Direction.hpp>
#include <bettercad/core/math/Frame.hpp>
#include <bettercad/core/units/Units.hpp>

#include <cstddef>
#include <functional>
#include <string_view>
#include <optional>
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

/// The path of a sweep in model space (P12-SWEEP-001): one or more planar
/// runs, joined end to end. One run is a planar path; several runs, on
/// different planes, make a spatial path. The runs' own plane coordinates
/// are local; the joins are in model space.
///
/// Every rule of a planar path holds across the runs: consecutive segments
/// meet (to 1e-10 m), meet tangentially, or -- two straight segments -- meet
/// at a mitred corner of less than 180 deg. A full circle is still a path on
/// its own, and then the only segment of the only run.
///
/// `twist` turns the section about the path's tangent as it travels:
/// theta(u) = u * twist, with u the normalized path coordinate (0 at the
/// start, 1 at the end). `guide` carries the section instead, by its own
/// turning about the path. They are two ways to say the same thing, so at
/// most one may be given.
struct SweptPath {
    std::vector<PlanarPath> runs{};
    Angle twist{};
    /// A guide curve: runs of its own, joined as a path is. Empty for none.
    std::vector<PlanarPath> guide{};

    friend bool operator==(const SweptPath&, const SweptPath&) = default;
};

/// @p path as a one-run spatial path, for the planar sweeps of
/// P11-FEAT-008.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT SweptPath asSweptPath(const PlanarPath& path);

/// How a sweep carries its section along the path (P12-SWEEP-001), for the
/// record and for diagnostics.
enum class SweepFrame {
    /// The path plane's normal, constant: the closed-form rotation-minimizing
    /// frame of a planar path (P11-FEAT-008).
    PlanarBinormal,
    /// The kernel's corrected Frenet frame, which on a planar path gives the
    /// same solid to the last digit (kernel-probe case G) and on a spatial
    /// path is exact where a fixed binormal fails.
    RotationMinimizing,
    /// A guide curve carries the section: either the one the definition
    /// gives, or the one BetterCAD generates from a twist.
    Guided,
};

/// "planar binormal", "rotation minimizing" or "guided".
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT std::string_view toString(SweepFrame frame) noexcept;

/// The frame @p path is swept with, by its own contents: a guide or a twist
/// makes it Guided, a single run PlanarBinormal, and more than one run
/// RotationMinimizing.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT SweepFrame frameOf(const SweptPath& path);

/// Solid swept by translating @p region along its plane normal between the
/// offsets @p from and @p to (to > from). For example [0, d] extrudes along
/// the normal, [-d, 0] against it and [-d/2, d/2] symmetrically.
///
/// Prisms, revolutions and sweeps take regions with any segments (lines,
/// arcs, circles, ellipses, splines); lofts and paths take lines, arcs and
/// circles only.
///
/// Loop orientation is normalized (outer counter-clockwise, holes
/// clockwise). Fails with InvalidArgument for malformed loops or extents and
/// with Internal if the kernel cannot build a valid solid (for example
/// self-intersecting loops).
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<Body> makePrism(const PlanarRegion& region,
                                                              Length from, Length to);

/// Where a face of a swept solid comes from: the region where the sweep
/// starts (First: a prism's `from`, a revolution's `from` angle, the start
/// of a sweep's path, a loft's first section) or ends (Last), or the side
/// one segment of one loop sweeps.
struct SweptFace {
    enum class Kind {
        First,
        Last,
        Side,
    };
    Kind kind = Kind::First;
    /// Side: the loop (0 the outer loop, i > 0 the hole i - 1) and the
    /// segment's index in it, as given in the region.
    std::size_t loop = 0;
    std::size_t segment = 0;
    /// Side of a sweep: the path segment it runs along, as given in the path.
    std::size_t pathSegment = 0;

    friend bool operator==(const SweptFace&, const SweptFace&) = default;
};

/// The name a face of a swept solid gets (P12-STREF-001, P12-SKETCH-003),
/// or none.
using SweptFaceNamer = std::function<std::optional<FaceName>(const SweptFace&)>;

/// makePrism() whose result carries the names @p namer gives its faces (see
/// findNamedFaces()). Each segment sweeps one side face; @p namer is asked
/// once per face.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<Body> makePrism(const PlanarRegion& region, Length from,
                                                              Length to, const SweptFaceNamer& namer);

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

/// makeRevolution() whose result carries the names @p namer gives its faces:
/// the region at `from` and at `to` (a full turn has no such faces) and the
/// side each segment sweeps (a segment on the axis sweeps none).
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<Body> makeRevolution(const PlanarRegion& region,
                                                                   const Axis3D& axis, Angle from, Angle to,
                                                                   const SweptFaceNamer& namer);

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
/// degenerate or disconnected path; an ellipse or spline in the path; a
/// corner at an arc; a path that turns
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

/// makeSweep() whose result carries the names @p namer gives its faces: the
/// outer loop at the start and at the end of an open path, and the side each
/// segment sweeps along each path segment. The kernel lists the faces a
/// profile edge sweeps in path order; a side is named only when it lists
/// one face per path segment.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<Body> makeSweep(const PlanarRegion& region, const PlanarPath& path,
                                                              const SweptFaceNamer& namer);

/// Solid swept by moving @p region along a path of several runs, with a
/// twist or a guide (P12-SWEEP-001). A one-run path without either is
/// exactly makeSweep(region, path): the same solid, bit for bit.
///
/// Placement, joints, preflight and the Pappus check are as for a planar
/// path, applied in model space across the runs, with two rules that a
/// spatial or twisted sweep adds:
/// - the region's centroid must lie on the path (to 1e-10 m). The section
///   then travels exactly the path's length whatever the frame does, so the
///   volume is the region's area times that length however the section
///   turns, and the check does not depend on the frame;
/// - the region's reach -- the farthest any of its points lies from the
///   path -- takes the place of the signed offset used for a planar path,
///   since a turning section has no constant offset. An arc's radius and a
///   mitred corner's legs must both exceed it.
///
/// A twist is built from an auxiliary spine BetterCAD generates from its own
/// rotation-minimizing frame, so theta(u) = u * twist is BetterCAD's law and
/// not the kernel's. That spine is fitted through samples of the path, so a
/// twisted or guided sweep meets Pappus to 1e-5 relative rather than the
/// 1e-9 of an untwisted one (measured: kernel-probe cases E and H).
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<Body> makeSweep(const PlanarRegion& region, const SweptPath& path);

/// makeSweep() whose result carries the names @p namer gives its faces: the
/// outer loop at the start and at the end of an open path, and the side each
/// profile segment sweeps along each path segment, numbered across the runs
/// in the order of travel.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<Body> makeSweep(const PlanarRegion& region, const SweptPath& path,
                                                              const SweptFaceNamer& namer);

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
/// holes, ellipses or splines, non-finite coordinates, non-parallel planes, coincident or
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

/// makeLoft() whose result carries the names @p namer gives its end faces
/// (the first section, First, and the last, Last). Its ruled sides are
/// B-spline surfaces and are not named.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<Body> makeLoft(std::span<const PlanarRegion> sections,
                                                             const SweptFaceNamer& namer);

} // namespace bettercad::geometry
