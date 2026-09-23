#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/geometry/HiddenLine.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/drawing/Export.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

// What a view does with the lines hidden-line removal found (P14-HLR-001,
// on ADR-019).
//
// `geometry::hiddenLineDrawing` answers the geometric question and answers it
// faithfully, duplicates and all. This decides what a DRAWING does about it,
// and there are exactly three decisions:
//
//     merge      two model edges that project onto each other are one line
//     hidden     whether this view shows what the solid hides
//     tangent    whether this view shows where a blend runs into its face
//
// The first is not a preference. ISO 128 gives line precedence -- a visible
// line takes precedence over a hidden line -- and without it every box in the
// system draws its outline twice, once solid and once dashed, because a box's
// far face projects exactly onto its near one. The other two are preferences,
// and they are per-view intent that is stored with the view.
namespace bettercad::drawing {

/// Whether a view draws the edges where a blend runs smoothly into the face
/// it blends.
///
/// Both answers are used in practice. ISO 128 does not require tangent edges
/// to be shown, and many houses hide them because they are not features of
/// the part; others show them because they make a fillet legible.
enum class TangentEdgePolicy : std::uint8_t {
    Show,
    Hide,
};

/// "show", "hide".
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::string_view toString(TangentEdgePolicy policy) noexcept;
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::optional<TangentEdgePolicy> tangentEdgePolicyFromString(
    std::string_view text) noexcept;

/// A view's hidden-line intent.
///
/// Intent, so it is stored with the view and round-trips through the file.
/// What it produces -- which lines are drawn -- is derived on every request
/// and is never stored (ADR-011).
///
/// The defaults are the ones a first-angle ISO drawing usually wants: hidden
/// detail shown, tangent edges not.
struct HiddenLineSettings {
    /// Whether the lines the solid hides are drawn at all. Turning this off
    /// removes them from what the view draws; it changes nothing about the
    /// model, the view's identity, its basis, its scale or its placement, and
    /// a test asserts each of those.
    bool showHidden = true;
    TangentEdgePolicy tangentEdges = TangentEdgePolicy::Hide;

    friend bool operator==(const HiddenLineSettings&, const HiddenLineSettings&) = default;
};

/// A known tangent-edge policy. Nothing else can be wrong here.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<void> validate(const HiddenLineSettings& settings);

/// One line a view draws, in sheet coordinates.
struct DrawnEdge {
    /// The curve as a polyline, ready to draw. A straight edge is two points.
    std::vector<Point2D> polyline{};
    /// The exact curve, for the milestones that need a circle to be a circle:
    /// its kind, its ends and its middle, all in sheet coordinates.
    geometry::EdgeCurve curve = geometry::EdgeCurve::Other;
    Point2D start{};
    Point2D end{};
    Point2D midpoint{};
    geometry::EdgeVisibility visibility = geometry::EdgeVisibility::Visible;
    geometry::ProjectedEdgeKind kind = geometry::ProjectedEdgeKind::Sharp;
    /// WHICH OCCURRENCE drew this line (P14-ASM-001, ADR-021).
    ///
    /// Set for every line of an assembly view and of a view of one component;
    /// empty for a view of a feature, which has no occurrence to name. It
    /// survives the coincident-line merge: where a front component's visible
    /// edge and a rear one's hidden edge draw the same line, the line that
    /// wins under ISO 128 keeps ITS occurrence, and the loser's is not
    /// silently inherited.
    ///
    /// This is occurrence identity, not edge identity. There is still no
    /// stable edge name in this codebase (ADR-012), and this does not invent
    /// one -- but it is what lets a later dimension, balloon or BOM row be
    /// attached to the right instance of a repeated part.
    std::optional<ComponentId> occurrence{};

    friend bool operator==(const DrawnEdge&, const DrawnEdge&) = default;
};

/// The outcome of applying a view's hidden-line settings.
struct HiddenLinePolicyResult {
    std::vector<geometry::ProjectedEdge> edges{};
    /// How many edges were dropped because another drew the same line.
    std::size_t merged = 0;
    /// How many were dropped because this view does not show their class.
    std::size_t suppressed = 0;
};

/// Merges coincident edges, then drops the classes @p settings does not show.
///
/// Merging comes first and is unconditional: two model edges that project
/// onto each other are one line however the view is set, and merging after
/// suppression would let a hidden duplicate hide a visible line's existence.
/// Precedence when two edges draw the same line, in order:
///
///     visible beats hidden        ISO 128 line precedence
///     sharp beats outline         a silhouette that falls on a real corner
///                                 IS that corner
///     outline beats smooth        a silhouette is a boundary of the shape;
///                                 a tangent line is not
///     smooth beats sewn           a seam is an artefact of parameterisation
///
/// The tolerance is applied in VIEW-PLANE units, before any scale, so that
/// whether two lines are the same line does not depend on how big the view
/// is drawn.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT HiddenLinePolicyResult applyHiddenLinePolicy(
    const geometry::HiddenLineDrawing& drawing, const HiddenLineSettings& settings);

} // namespace bettercad::drawing
