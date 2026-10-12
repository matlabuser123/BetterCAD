#pragma once

// The drawable geometry of the structural overlays (P17-VIZ-001, ADR-045).
//
// NO QT AND NO OCCT. This turns the overlay DATA -- which carries no position
// at all -- into line segments in SI metres, and that resolution is the part
// most likely to be wrong, so it lives where a test can reach it without a
// GPU.
//
// WHY THERE IS A RESOLUTION STEP. `NodalLoadArrow` and `ReactionArrow` carry a
// `NodeId` and a `Force3D`; `RestraintGlyph` carries a mask and three counts.
// None of them carries a point, because the data layer computes no geometry.
// So a drawing layer must ask the canonical mesh where the node is, and that
// is the only place a position comes from here -- there is no spatial search,
// no centroid fit and no inferred location anywhere in this file.
//
// AND NO ENGINEERING VALUE IS COMPUTED EITHER. A segment is two points. The
// force, the magnitude and the component mask are read from the glyph the
// caller already has.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/renderer/Export.hpp>
#include <bettercad/renderer/StructuralInspection.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace bettercad::renderer {

/// What an overlay depicts. A UI colours by this; the geometry does not
/// differ.
enum class OverlayRole : std::uint8_t {
    /// Applied nodal forces.
    LoadArrows,
    /// Recovered support reactions.
    ReactionArrows,
    /// One bar per constrained component, at each restrained node.
    RestraintSymbols,
};

[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::string_view toString(OverlayRole role) noexcept;

/// One line segment, in **SI metres**, in the mesh's own coordinates.
///
/// METRES, like everything else leaving this layer. The single
/// metres-to-millimetres conversion is `toModelLength` in the presentation
/// adapter, and an overlay passes through it exactly as a mesh buffer does.
struct OverlaySegment {
    Point3D from{};
    Point3D to{};

    friend bool operator==(const OverlaySegment&, const OverlaySegment&) = default;
};

/// An arrow is one shaft and FOUR barbs.
///
/// Four rather than two, in two perpendicular pairs, so the head reads from
/// any viewing direction -- a two-barb head vanishes edge-on, and an overlay
/// that disappears at some camera angles is worse than none.
constexpr std::size_t kSegmentsPerArrow = 5;

/// The barb length, as a fraction of the shaft, and the angle it makes with
/// the shaft. Presentation constants, stated here so a test can predict the
/// geometry rather than accept whatever it is given.
constexpr double kArrowBarbFraction = 0.25;
constexpr double kArrowBarbRadians = 0.4363323129985824; // 25 degrees

/// The segments of one overlay, plus what was left out and why.
///
/// THE CARDINALITY IS PART OF THE ANSWER, as it is for `ScalarField`: an
/// overlay that drew nothing must be distinguishable from one that drew
/// everything and found nothing to skip.
struct StructuralOverlay {
    OverlayRole role = OverlayRole::LoadArrows;
    std::vector<OverlaySegment> segments{};

    /// Glyphs that produced geometry.
    std::size_t glyphs = 0;
    /// Glyphs with no direction, which produce NO arrow.
    ///
    /// A ZERO FORCE IS SKIPPED, NOT DRAWN. `hasDirection` is already false for
    /// one in the data layer, and an arrow pointing some arbitrary way would
    /// be a drawn claim about a direction that does not exist. Counted so a UI
    /// can say "3 loads, 1 of them zero" rather than silently showing two.
    std::size_t skipped = 0;
    /// Glyphs with a direction whose DISPLAY LENGTH came out non-positive.
    ///
    /// A DIFFERENT CAUSE FROM `skipped`, and counted separately because the
    /// remedy differs: this one is the caller's scale, not the model's load.
    /// `displayArrowLength` is `scale * |magnitude| / reference`, which
    /// UNDERFLOWS TO ZERO for a small enough force against a large enough
    /// reference -- and a zero-length shaft puts the tail on the head and
    /// collapses all four barbs onto the same point, so five degenerate
    /// segments get drawn at a dot.
    ///
    /// The accounting invariant: `glyphs + skipped + tooSmall` is the number
    /// of glyphs given.
    std::size_t tooSmall = 0;

    [[nodiscard]] bool empty() const noexcept { return segments.empty(); }
};

/// An arrow per nodal load, at its node.
///
/// THE HEAD IS AT THE NODE and the tail is upstream, so the arrow points AT
/// the loaded node: `tail = position - length * direction`. Both conventions
/// exist in the field; this one says "the force acts HERE", and it makes the
/// head coincide EXACTLY with a canonical node position, which a test can
/// compare bit for bit.
///
/// @p scale and @p reference are the presentation sizing `displayArrowLength`
/// takes, and are refused on the same terms.
///
/// REFUSES a `NodeId` the mesh does not hold, rather than dropping the arrow:
/// an absent node means the arrows and the mesh disagree, and a silently
/// shorter overlay would hide that.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<StructuralOverlay>
loadArrowOverlay(const meshing::Mesh& mesh, std::span<const NodalLoadArrow> arrows, double scale,
                 double reference);

/// An arrow per support reaction, at its node. Same conventions as above.
///
/// THE SIGN IS THE QUALIFIED ONE. A reaction opposes the load it carries, and
/// this draws the `Force3D` the glyph holds without negating it -- P17-REACTION
/// settled that sign and a viewer is not entitled to a second opinion.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<StructuralOverlay>
reactionArrowOverlay(const meshing::Mesh& mesh, std::span<const ReactionArrow> arrows,
                     double scale, double reference);

/// One bar per constrained component, centred on each restrained node and
/// aligned with that axis.
///
/// SO THE BAR COUNT IS THE CONSTRAINED DOF COUNT and the bar directions are
/// which ones. That is directly assertable, and it invents no support-triangle
/// glyph whose meaning a reader would have to be taught.
///
/// @p numbering is REQUIRED, not optional, and the reason is the defect
/// recorded in DEFECT_RESTRAINT_COMPONENTS.md: the per-node mask cannot be
/// derived from the resolutions, because `RestraintResolution` carries a node
/// COUNT and never a node list. Asking the numbering and the `ConstraintSet`
/// one degree of freedom at a time is the only exact answer.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<StructuralOverlay>
restraintSymbolOverlay(const meshing::Mesh& mesh,
                       const structural::PreparedRestraints& restraints,
                       const structural::MeshDofMap& numbering, double barLength);

} // namespace bettercad::renderer
