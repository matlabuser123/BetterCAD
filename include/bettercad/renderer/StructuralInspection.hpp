#pragma once

// P17-VIZ-001 -- result currentness and inspection, both derived from core.
//
// The sibling of `MeshInspection.hpp`, which answers the same two questions
// for a P16 mesh: what is on screen relative to the model, and what does the
// user see when they click something.
//
// THERE IS NO GUI STALE FLAG IN THIS FILE, and that is its central claim.
// `StructuralDisplayState` holds P16's `MeshCurrency` and P17-DATA-001's
// `AnalysisState` and `StaleReason` set, unchanged, because a second opinion
// about currentness is a second opinion that can drift -- and the one that
// drifts toward "current" is the one that shows a wrong number as right. A
// `bool viewerResultIsStale` appears nowhere.
//
// AND IT HOLDS THE TWO STATES SEPARATELY, which the brief is specific about:
// a MATERIAL edit leaves the mesh CURRENT and the result STALE. A UI that
// collapsed them would label the mesh stale, send the user to remesh, and
// leave the real cause untouched.
//
// NOTHING HERE COMPUTES AN ENGINEERING VALUE. Every field of every inspection
// struct is read from `RecoveredFields`, `SupportReactions` or the canonical
// mesh. There is no strain, no stress, no von Mises, no principal value, no
// reaction and no equilibrium arithmetic in this header or its
// implementation.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/math/Vector.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/renderer/Export.hpp>
#include <bettercad/renderer/MeshInspection.hpp>
#include <bettercad/renderer/MeshView.hpp>
#include <bettercad/structural/StructuralConstraints.hpp>
#include <bettercad/structural/StructuralDof.hpp>
#include <bettercad/structural/StructuralLoad.hpp>
#include <bettercad/structural/StructuralLoadVector.hpp>
#include <bettercad/structural/StructuralRestraint.hpp>
#include <bettercad/structural/StructuralPost.hpp>
#include <bettercad/structural/StructuralReaction.hpp>
#include <bettercad/structural/StructuralResult.hpp>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace bettercad::features {
class Regenerator;
} // namespace bettercad::features

namespace bettercad::renderer {

// ---------------------------------------------------------------------------
// What is on screen, relative to the model
// ---------------------------------------------------------------------------

/// The mesh state and the result state, side by side and never merged.
///
/// BOTH AUTHORITIES, CARRIED WHOLE. `mesh` is `meshing::Mesher::currency`,
/// which distinguishes `StaleIntent` from `StaleGeometry`; `analysis` is
/// `structural::analysisState`, which distinguishes `SolvedStale` from
/// `SolveFailed` and from `InputsUnavailable`. Neither is restated as a
/// summary, because restating is where the detail that matters gets lost --
/// P16-MAP-001 made the same point about `Unresolved` being a status and not
/// an empty list.
struct StructuralDisplayState {
    /// P16's verdict on the mesh.
    meshing::MeshCurrency mesh = meshing::MeshCurrency::NoMesh;
    /// P17-DATA's verdict on the result.
    structural::AnalysisState analysis = structural::AnalysisState::NoAnalysis;
    /// WHICH dependencies moved, in the enum's order. Empty when the result is
    /// current or when there is no result to compare.
    ///
    /// A SET, not the first mismatch, because "the geometry and the material
    /// both changed" is more useful than either half -- P17-DATA-001's own
    /// reasoning, inherited rather than re-decided.
    std::vector<structural::StaleReason> staleReasons{};
    /// The core's diagnostic for a failed solve, when there was one.
    std::optional<Error> failure{};

    /// Whether the mesh may be drawn as describing the model now.
    [[nodiscard]] bool meshDescribesModel() const noexcept {
        return meshing::describesTheModel(mesh);
    }

    /// Whether a result may be drawn WITHOUT a stale marker.
    ///
    /// ONLY `SolvedCurrent`. This is the predicate the brief's hardest
    /// requirement rests on: a stale result must never be presented
    /// indistinguishably from a current one, and every other state -- stale,
    /// failed, inputs unavailable, ready, none -- answers false.
    [[nodiscard]] bool resultMayDrawAsCurrent() const noexcept {
        return analysis == structural::AnalysisState::SolvedCurrent;
    }

    /// Whether a result exists at all and can be looked at, stale included.
    ///
    /// Separate from the above, because retaining a historical result and
    /// MARKING it is a legitimate product choice -- what is forbidden is
    /// showing it as current.
    [[nodiscard]] bool resultInspectable() const noexcept {
        return analysis == structural::AnalysisState::SolvedCurrent ||
               analysis == structural::AnalysisState::SolvedStale ||
               analysis == structural::AnalysisState::SolveFailed;
    }

    /// Whether the mesh is current while the result is not.
    ///
    /// THE DISTINCTION THE BRIEF CALLS OUT, as a question a UI can ask
    /// directly: after a material, load or restraint edit this is true, and a
    /// UI that showed "mesh stale" would send the user to the wrong fix.
    [[nodiscard]] bool resultStaleOnACurrentMesh() const noexcept {
        return meshDescribesModel() && analysis == structural::AnalysisState::SolvedStale;
    }

    friend bool operator==(const StructuralDisplayState&,
                           const StructuralDisplayState&) = default;
};

/// The display state of @p analysis, derived from @p document.
///
/// @p lastResult and @p lastFailure are what the application holds for this
/// analysis; both may be null. The shape mirrors `Mesher::mesh()` and
/// `Mesher::lastFailure()`, and it is the shape `structural::analysisState`
/// already takes, so this adds a mesh verdict and the stale reasons and
/// invents nothing.
///
/// DETERMINISTIC: no wall clock, no pointer identity, no unordered iteration.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT StructuralDisplayState structuralDisplayState(
    const Document& document, const features::Regenerator& regenerator,
    const meshing::Mesher& mesher, MeshControlId control, AnalysisId analysis,
    const structural::StructuralResult* lastResult, const Error* lastFailure);

// ---------------------------------------------------------------------------
// Inspection
// ---------------------------------------------------------------------------

/// Everything the UI shows for one selected node.
///
/// EVERY FIELD IS READ. The undeformed position comes from the canonical mesh
/// as a `Point3D`, whose unit is part of its type; the displacement and its
/// magnitude come from `NodalDisplacement`, where the magnitude is stored "so
/// that a consumer cannot compute it a second, different way".
///
/// NO DEFORMED POSITION IS REPORTED HERE. A display-deformed coordinate is a
/// function of a presentation scale, and putting it in the same struct as the
/// engineering position is how it ends up labelled "node coordinate". A UI
/// that wants to show it computes it from the scale it is displaying and says
/// so.
struct NodeResultInspection {
    meshing::NodeId node{};
    /// The canonical, undeformed position. Engineering, unit in the type.
    Point3D undeformedPosition{};
    Translation3D displacement{};
    /// `|u|`, stored by the result.
    Length magnitude{};
    /// Whether this node has any prescribed degree of freedom, and which.
    ///
    /// FROM THE PREPARED RESTRAINTS, never inferred from the displacement. A
    /// node can have exactly zero displacement without being restrained --
    /// a symmetry plane, or simply a stiff region -- and reading restraint
    /// from a zero would be a guess that is usually right and sometimes
    /// badly wrong.
    bool restrained = false;
    structural::RestraintComponents restrainedComponents{};

    friend bool operator==(const NodeResultInspection&, const NodeResultInspection&) = default;
};

/// Everything the UI shows for one selected element.
///
/// A COPY OF ONE `ElementFields`, plus the handle. The strain, the stress, the
/// principal values, the von Mises and the hydrostatic stress are all stored
/// members; this struct exists so the UI has one thing to bind to, not to
/// compute anything.
struct ElementResultInspection {
    meshing::ElementId element{};
    /// Engineering shear, in `TensorComponent` order.
    structural::Strain6 strain{};
    /// In pascals, same order.
    structural::Stress6 stress{};
    /// Descending, as P17-POST produced them. NOT re-sorted.
    structural::PrincipalStresses principalStress{};
    structural::PrincipalStrains principalStrain{};
    Stress vonMises{};
    Stress hydrostatic{};

    friend bool operator==(const ElementResultInspection&, const ElementResultInspection&) = default;
};

/// Everything the UI shows for one support reaction.
struct ReactionInspection {
    meshing::NodeId node{};
    /// The reaction force, in the qualified sign convention: the force the
    /// support exerts ON the structure. NOT negated for presentation.
    Force3D force{};
    /// Which components of this node are prescribed.
    structural::RestraintComponents constrained{};
    /// NO PER-NODE RESTRAINT ATTRIBUTION IS REPORTED HERE, deliberately.
    ///
    /// `SupportReaction` carries the node, the force and the constrained
    /// components, and nothing that says which restraint named this node.
    /// `RestraintResolution` carries a node COUNT and a `DofIndex` list, not
    /// node ids, so answering "which restraints own this node" means walking
    /// the list back through the `MeshDofMap` -- and a viewer that did that
    /// would be re-deriving an attribution P17-REACTION-001 already settled,
    /// including the rule that a SHARED degree of freedom is counted once
    /// globally with per-restraint summaries that are additive by
    /// construction.
    ///
    /// So attribution is asked of the owner instead:
    /// `SupportReactions::of(RestraintId)` gives the per-restraint summary,
    /// and `sharedDegreesOfFreedom()` / `sharedForce()` give the overlap. A UI
    /// shows those; it does not reconstruct them.

    friend bool operator==(const ReactionInspection&, const ReactionInspection&) = default;
};

/// The inspection of @p node.
///
/// Refuses when @p fields does not describe @p mesh, and when @p node is not a
/// node of @p mesh. @p restraints may be null -- a result can be inspected
/// without the prepared restraints in hand, and then `restrained` is false
/// because it is UNKNOWN rather than because it was checked. A UI without them
/// must not draw a support symbol from this.
///
/// @p numbering IS REQUIRED FOR `restrainedComponents`, and null leaves that
/// mask EMPTY -- which is the same "unknown" that a null @p restraints means
/// for `restrained`.
///
/// WHY IT IS NEEDED, because the first version of this function did without
/// it and was WRONG. `PreparedRestraints` holds the `ConstraintSet` but not
/// the degree-of-freedom numbering, so "which components are held at THIS
/// node" cannot be answered from the resolutions: `RestraintResolution`
/// carries a mask and a node COUNT, never a node list. The original code
/// unioned every resolution's mask unconditionally, so with a fixed support
/// and a Uz-only support in the same analysis every restrained node reported
/// FIXED -- and a UI drawing a support symbol from it drew the wrong support.
///
/// With @p numbering the answer is exact and comes from the owners: the DOF
/// index of each component from P17-DOF's map, and whether it is constrained
/// from P17-BC's own `ConstraintSet`. Nothing is unioned and nothing is
/// inferred.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<NodeResultInspection>
inspectNode(const meshing::Mesh& mesh, const structural::RecoveredFields& fields,
            meshing::NodeId node, const structural::PreparedRestraints* restraints,
            const structural::MeshDofMap* numbering = nullptr);

/// The inspection of @p element.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<ElementResultInspection>
inspectElement(const meshing::Mesh& mesh, const structural::RecoveredFields& fields,
               meshing::ElementId element);

/// The inspection of the reaction at @p node.
///
/// Refuses when @p reactions does not describe @p mesh, and when @p node
/// carries no reaction -- an unrestrained node has none, and reporting a zero
/// for it would invent a support that does not exist.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<ReactionInspection>
inspectReaction(const meshing::Mesh& mesh, const structural::SupportReactions& reactions,
                meshing::NodeId node);

/// Every node carrying a reaction, ascending.
///
/// The set a UI draws arrows for. Read from `SupportReactions::nodal()`, which
/// is already ascending and without repeats.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::vector<meshing::NodeId>
reactionNodes(const structural::SupportReactions& reactions);

// ---------------------------------------------------------------------------
// Overlays: loads, restraints, reactions
// ---------------------------------------------------------------------------
//
// EVERY GLYPH IS READ FROM A STORED VALUE, and for the loads that is the
// decisive design point. `PreparedLoads` already carries, per load, the
// `LoadId`, the `LoadKind`, the element count, the area and the RESULTANT
// force, plus the whole nodal force field -- all computed and validated by
// P17-LOAD-001. So this layer needs no surface normal, no integration and no
// knowledge of the pressure convention:
//
//     POSITIVE PRESSURE ACTS INWARD, t = -p n_out
//
// is frozen in P17-LOAD-001 with its own analytical test and its own mutation
// probe, and it is already baked into the prepared nodal forces this layer
// draws. A viewer that recomputed `-p n_out` from a facet normal would be a
// second implementation of that convention, free to disagree with the solver
// about which way a load pushes.
//
// GRAVITY LIKEWISE HAS NO IMPLICIT DIRECTION. P17-LOAD-001 is explicit that
// "there is no implicit -Z", so the glyph's direction comes from the
// contribution's resultant and never from a hardcoded axis.

/// One glyph for one canonical load.
///
/// ONE PER LOAD, not one per facet: a representative arrow that says what the
/// load IS. The per-node field is `nodalLoadArrows`, which is what a user
/// wanting to see the distribution asks for.
struct LoadGlyph {
    LoadId load{};
    structural::LoadKind kind = structural::LoadKind::NodalForce;
    /// The load's resultant force, in newtons, READ from
    /// `LoadContribution::resultant`. This is the arrow's direction and the
    /// engineering magnitude; a display length is a separate, presentation-only
    /// quantity (`displayArrowLength`).
    Force3D resultant{};
    /// The area the load acts over, for a face load. Zero for a nodal force
    /// and for gravity.
    Area area{};
    /// How many mesh entities carried it.
    std::size_t elements = 0;
    /// Whether the resultant has a direction at all.
    ///
    /// A ZERO VECTOR HAS NO DIRECTION AND IS NOT GIVEN ONE. A self-equilibrated
    /// load set, or a zero-magnitude load the user deliberately entered, has a
    /// zero resultant -- and inventing an axis for it would draw an arrow that
    /// means nothing. The inspector still reports the load.
    bool hasDirection = false;

    friend bool operator==(const LoadGlyph&, const LoadGlyph&) = default;
};

/// One arrow at one node, from the prepared nodal force field.
struct NodalLoadArrow {
    meshing::NodeId node{};
    /// In newtons, read from `NodalLoad::force`.
    Force3D force{};
    bool hasDirection = false;

    friend bool operator==(const NodalLoadArrow&, const NodalLoadArrow&) = default;
};

/// One restraint symbol.
///
/// CARRIES THE COMPONENT MASK, so a UI can draw a `Ux`-only roller
/// differently from a fixed support. Rendering every restraint as fixed is a
/// defect the brief names explicitly, and the mask is what prevents it.
struct RestraintGlyph {
    RestraintId restraint{};
    structural::RestraintComponents components{};
    /// How many nodes and facets it resolved to, from the resolution.
    std::size_t nodes = 0;
    std::size_t facets = 0;
    std::size_t degreesOfFreedom = 0;

    friend bool operator==(const RestraintGlyph&, const RestraintGlyph&) = default;
};

/// One reaction arrow at one restrained node.
struct ReactionArrow {
    meshing::NodeId node{};
    /// The force the support applies TO the structure, in the sign convention
    /// P17-REACTION-001 froze against the one-degree-of-freedom case. NOT
    /// reversed for presentation.
    Force3D force{};
    structural::RestraintComponents constrained{};
    bool hasDirection = false;

    friend bool operator==(const ReactionArrow&, const ReactionArrow&) = default;
};

/// A glyph per load, in the order the loads were prepared.
///
/// Reads `PreparedLoads::contributions()`. No integration, no normal, no
/// pressure sign: every number is already there.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::vector<LoadGlyph>
loadGlyphs(const structural::PreparedLoads& loads);

/// An arrow per loaded node, ascending by `NodeId`.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::vector<NodalLoadArrow>
nodalLoadArrows(const structural::PreparedLoads& loads);

/// A symbol per restraint, in the order the restraints were prepared.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::vector<RestraintGlyph>
restraintGlyphs(const structural::PreparedRestraints& restraints);

/// An arrow per restrained node, ascending by `NodeId`.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::vector<ReactionArrow>
reactionArrows(const structural::SupportReactions& reactions);

// --- arrow sizing, which is presentation and nothing else -------------------

/// Whether @p scale is usable as an arrow display scale.
///
/// Finite and strictly positive. Zero is refused here, unlike the deformation
/// scale: a zero deformation scale means "show the undeformed shape", which is
/// meaningful, whereas a zero arrow scale means "draw no arrow", which a
/// visibility toggle expresses better than a magnitude of zero.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT bool isUsableArrowScale(double scale) noexcept;

/// A display length, in METRES of model space, for a vector of @p magnitude.
///
/// PRESENTATION ONLY, AND THE UNITS ARE THE POINT. A force is newtons and a
/// length is metres; there is no physical conversion between them, so this is
/// a deliberate, declared mapping for drawing and nothing more:
///
/// ```text
/// length_m = scale_m_per_unit * (magnitude / reference)
/// ```
///
/// The caller supplies the reference -- typically the largest magnitude in the
/// set -- so the longest arrow is `scale` long and the rest are proportional.
/// Changing @p scale changes only what is drawn: no engineering value anywhere
/// is a function of it, which `ArrowScaleIsPresentationOnly` asserts.
///
/// Returns zero for a zero @p magnitude rather than a minimum glyph size: a
/// floor belongs to the presentation layer that knows its pixel budget, and
/// putting one here would make a zero load look like a small one.
///
/// Refuses a non-finite or non-positive @p scale or @p reference.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<double>
displayArrowLength(double magnitude, double reference, double scale);


// ---------------------------------------------------------------------------
// Boundary highlighting for a load or a restraint
// ---------------------------------------------------------------------------
//
// NO RESOLUTION LOGIC LIVES HERE. P16-MAP-001 and P16-VIZ-001 already resolve
// a canonical reference to render triangles through `GeometryMeshMap`, and
// `highlightFor` carries the mapping's own `MappingState` whole -- so an
// Unresolved reference stays distinguishable from a reference that resolved to
// nothing, which P16-MAP-001 is explicit about: "Unresolved is a status, not
// an empty list".
//
// These two functions exist to make the path a UI must take EXPLICIT, and to
// give it a name a test can hold. There is no geometric search, no
// nearest-face match and no coordinate tolerance in either of them, because
// neither does any work beyond extracting the canonical target and handing it
// to P16.

/// The CAD face a load acts on, when it has one.
///
/// ABSENT FOR A NODAL FORCE AND FOR GRAVITY, and that is not a failure: a
/// nodal force targets a `NodeId` and gravity targets the whole body, so
/// neither has a face to highlight. A UI must show those differently rather
/// than highlighting nothing and implying the target was lost.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::optional<FaceName>
loadTarget(const structural::StructuralLoad& load);

/// The CAD face a restraint acts on. Always present -- P17-BC-001 has exactly
/// one target kind.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT FaceName
restraintTarget(const structural::StructuralRestraint& restraint);

/// The render triangles currently belonging to @p load's canonical target.
///
/// Delegates to P16's `highlightFor`. Refuses with `InvalidArgument` for a
/// load that has no face target, so a caller cannot mistake "this load has no
/// face" for "this face resolved to nothing".
[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<MeshHighlight>
highlightForLoad(const MeshView& view, const meshing::Mesh& mesh,
                 const meshing::GeometryMeshMap& map,
                 const structural::StructuralLoad& load);

/// The render triangles currently belonging to @p restraint's canonical target.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<MeshHighlight>
highlightForRestraint(const MeshView& view, const meshing::Mesh& mesh,
                      const meshing::GeometryMeshMap& map,
                      const structural::StructuralRestraint& restraint);

} // namespace bettercad::renderer
