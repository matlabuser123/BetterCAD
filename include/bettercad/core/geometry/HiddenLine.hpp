#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/geometry/Export.hpp>
#include <bettercad/core/math/Frame.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/units/Units.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

// Hidden-line removal: what a solid looks like from a direction, with the
// lines the solid hides from itself told apart from the ones it does not
// (P14-HLR-001).
//
// This lives in `geometry` rather than in `drawing` for one reason: hidden
// lines are computed by the kernel, and OCCT may only be reached from an
// `occt/` adapter under `src/`. So `geometry` answers the geometric question
// -- which projected curves are there, and which of them the solid hides --
// and `drawing` decides what to do about it. Nothing here knows about
// sheets, scales, views or policy.
//
// WHAT IS DERIVED. All of it. A drawing's hidden lines are recomputed from
// the model and the direction on every request and are never persisted
// (ADR-011). Nothing in this file is engineering intent.
//
// WHAT IS NOT HERE: identity. A projected edge does not say which model edge
// it came from. That is not an omission that could be filled in later by
// trying harder -- ADR-012 records that this codebase has no stable edge
// name, and the kernel's hidden-line output does not carry one back either.
// A dimension therefore references the MODEL and projects it; it does not
// reference a drawn line. See the note on provenance below.
namespace bettercad::geometry {

/// Whether the solid hides a projected edge from the viewer.
///
/// Two values, not three: an edge is either hidden by something or it is not,
/// and an edge that is partly hidden comes back as two edges, because the
/// kernel splits it where the occlusion starts.
enum class EdgeVisibility : std::uint8_t {
    Visible,
    Hidden,
};

/// "visible", "hidden".
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT std::string_view toString(EdgeVisibility visibility) noexcept;

/// What kind of line a projected edge is.
///
/// This is the geometric fact. Whether a drawing shows it, and in what style,
/// is policy and is decided in `drawing`.
enum class ProjectedEdgeKind : std::uint8_t {
    /// A model edge whose two faces meet at an angle. The lines a drawing
    /// always shows.
    Sharp,
    /// A model edge whose faces join smoothly -- a fillet running into the
    /// face it blends. Real topology, but whether a drawing shows it is the
    /// tangent-edge policy, which differs between houses and standards.
    Smooth,
    /// A silhouette. NOT a model edge: the curve where a curved surface turns
    /// away from the viewer, which moves when the viewer moves. A cylinder
    /// seen across its axis is drawn by two of these and has no model edge
    /// along its length at all.
    Outline,
    /// A seam of a closed surface -- the line where a cylinder's surface
    /// joins itself. An artefact of how the surface is parameterised rather
    /// than a feature of the shape.
    Sewn,
};

/// "sharp", "smooth", "outline", "sewn".
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT std::string_view toString(ProjectedEdgeKind kind) noexcept;

/// One curve of a hidden-line drawing, in view-plane coordinates.
///
/// The coordinates are exactly what `Frame3D::toLocal` gives for a point of
/// the model, in model units: x along the basis's X axis, y along its Y axis.
/// Scale and sheet placement belong to `drawing` and are not applied here.
struct ProjectedEdge {
    /// The kind of curve this projects to, which is not always the kind the
    /// model edge was: a circle seen at an angle projects to an ellipse, and
    /// comes back as `Other`.
    EdgeCurve curve = EdgeCurve::Other;
    Point2D start{};
    Point2D end{};
    Point2D midpoint{};
    /// Length in the view plane -- the drawn length, not the model edge's.
    Length length{};
    /// The curve sampled as a polyline, first point to last.
    ///
    /// A straight edge is its two endpoints and nothing more. A curved one is
    /// sampled to a chord deflection of 0.01 mm IN MODEL UNITS, which is
    /// deterministic for a given curve but is not aware of the scale the
    /// drawing will be at (see the milestone's known limitations).
    ///
    /// This exists because `start` and `end` are not enough to draw a curve,
    /// and for a FULL circle they are the same point -- a renderer joining
    /// them with a straight line would draw nothing at all. The exact
    /// description above stays alongside it for the milestones that need a
    /// circle to be a circle rather than a chain of chords.
    std::vector<Point2D> polyline{};
    EdgeVisibility visibility = EdgeVisibility::Visible;
    ProjectedEdgeKind kind = ProjectedEdgeKind::Sharp;
    /// WHICH of the bodies given to hiddenLineDrawing() this came from, as an
    /// index into them. Zero for the single-body call, where there is nothing
    /// to distinguish.
    ///
    /// This is the one piece of identity the kernel's answer can carry
    /// (ADR-021). It is not a stable edge name -- the note above still holds,
    /// and there is none in this codebase -- but it says which OCCURRENCE
    /// drew the line, which is what an assembly drawing needs so that a
    /// dimension, a balloon or a BOM row can be attached to the right one.
    std::size_t source = 0;

    friend bool operator==(const ProjectedEdge&, const ProjectedEdge&) = default;
};

/// What a solid draws as, seen from one direction.
struct HiddenLineDrawing {
    /// Every projected curve, in a canonical order that does not depend on
    /// the kernel's traversal: by kind, then visibility, then the start
    /// point, then the end point. The kernel's own order carries no meaning
    /// and is not stable enough to compare two runs by.
    std::vector<ProjectedEdge> edges{};

    /// How many of `edges` are of each kind and visibility. Counted here so a
    /// test can assert the classification without walking the vector, and so
    /// a caller can tell "nothing was hidden" from "nothing was computed".
    [[nodiscard]] BETTERCAD_GEOMETRY_EXPORT std::size_t count(EdgeVisibility visibility) const noexcept;
    [[nodiscard]] BETTERCAD_GEOMETRY_EXPORT std::size_t count(ProjectedEdgeKind kind) const noexcept;
    [[nodiscard]] BETTERCAD_GEOMETRY_EXPORT std::size_t count(EdgeVisibility visibility,
                                                              ProjectedEdgeKind kind) const noexcept;
};

/// What @p body looks like seen through @p viewBasis.
///
/// The viewer is on the side the basis's normal points to (ADR-013), looking
/// back along it, so the material nearest the viewer is what hides the rest.
/// That convention is asserted rather than assumed: a solid with a feature on
/// the near side and another on the far side must classify the near one
/// Visible and the far one Hidden.
///
/// Fails with FailedPrecondition for an empty body, and with Internal when
/// the kernel's hidden-line algorithm raises -- never by returning a partial
/// drawing, because a drawing missing the lines that failed looks exactly
/// like a drawing of a simpler part.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<HiddenLineDrawing> hiddenLineDrawing(
    const Body& body, const Frame3D& viewBasis);

/// What @p bodies look like TOGETHER, seen through @p viewBasis.
///
/// One hidden-line problem containing all of them, never one problem each
/// (ADR-021). That is the whole difference: run separately, every body is
/// classified against itself alone, so a body standing entirely behind
/// another comes back fully visible and a drawing shows the rear one in solid
/// lines over the front one. Run together, the algorithm decides which body
/// is in front, because that is the same question it already answers about a
/// single body hiding itself.
///
/// Every returned edge carries `source`, the index in @p bodies of the body
/// it came from, so occlusion is computed across the set without losing which
/// member each line belongs to.
///
/// Fails with FailedPrecondition when @p bodies is empty or any member is
/// empty -- a set with a missing member would be a picture of a different
/// assembly -- and with Internal when the kernel raises.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<HiddenLineDrawing> hiddenLineDrawing(
    std::span<const Body> bodies, const Frame3D& viewBasis);

} // namespace bettercad::geometry
