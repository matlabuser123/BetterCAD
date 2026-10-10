#pragma once

// P17-BC-001 -- resolving canonical restraints into the current constrained
// DOF set.
//
// THE AUTHORITY CHAIN.
//
//     canonical restraint intent   RestraintId + FaceName + component mask
//              |
//     P16's GeometryMeshMap        structural::resolveFaceTarget
//              |
//     current boundary facets      ElementIds of THIS mesh, derived
//              |
//     unique current NodeIds       meshing::boundaryNodesOf -- ALREADY sorted
//                                  and deduplicated, so P17 writes neither
//              |
//     P17-DOF's MeshDofMap         NodeId + DofComponent -> DofIndex
//              |
//     ConstraintSet                P17-DOF's, with its own duplicate policy
//
// **NOTHING MESH-LOCAL IS CANONICAL.** A facet handle, a node handle and a DOF
// index are all derived and are rebuilt on every preparation. Asserted at
// compile time in `StructuralBC_CanonicalRestraintsCarryNoMeshHandle`.
//
// P17-BC NUMBERS NOTHING. `MeshDofMap` owns `NodeId + component -> DofIndex`
// and `FreeEquationMap` owns the reduced numbering; this module only asks. In
// particular there is no `3 * nodeId` anywhere -- that arithmetic is the defect
// P17-DOF-001 exists to prevent, and a second copy of it here would reintroduce
// it.
//
// AND IT DEDUPLICATES NOTHING EITHER, which is the audit's main finding.
// `meshing::boundaryNodesOf` ends with
//
//     std::ranges::sort(nodes);
//     nodes.erase(std::ranges::unique(nodes).begin(), nodes.end());
//
// so "collect the nodes of every mapped facet, then deduplicate by NodeId,
// then put them in a deterministic order" is one call. Writing it again here
// would be a second definition of the node set.
//
// WHAT THIS FILE DOES NOT DO. It applies no constraint: no stiffness row is
// zeroed, no diagonal is set to one, no penalty stiffness is added and no
// right-hand side is touched. P17-ASSEMBLY-001 and P17-SOLVE-001 own that. It
// also does not decide whether the model is sufficiently constrained -- a free
// Tet4 body has six rigid-body modes and detecting that it still does is
// P17-SOLVE-001's, which is where the eigenstructure is visible.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/structural/Export.hpp>
#include <bettercad/structural/StructuralAnalysis.hpp>
#include <bettercad/structural/StructuralDof.hpp>
#include <bettercad/structural/StructuralRestraint.hpp>
#include <bettercad/structural/StructuralTarget.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace bettercad::structural {

/// Why a set of restraints cannot be resolved into a constrained DOF set.
///
/// EVERY VALUE'S REACHABILITY IS STATED IN THE EVIDENCE, and two the brief
/// asks for are deliberately absent:
///
/// ```text
/// Ambiguous   P16's MappingState has only Resolved and Unresolved, and its
///             header says a FaceName "can be ambiguous ... and that is
///             exactly why this layer does not map through one"
/// Stale       requireStructuralModel already refuses a stale mesh and proves
///             the mapping came from the same lookup (ADR-036), and
///             boundaryNodesOf checks the map against the mesh again. A third
///             gate here would be a branch nothing can take
/// ```
///
/// There is also no `Conflict`. In this scope every prescribed value is zero,
/// so two restraints on one degree of freedom cannot disagree numerically --
/// they are REDUNDANT, which is lawful, and calling that a conflict would be
/// inventing a failure to fill a checkbox. See `CONSTRAINT_VALIDATION.md` on
/// what a real conflict would be once prescribed displacement exists.
enum class RestraintProblem : std::uint8_t {
    /// Two records carry the same `RestraintId`. Refused rather than resolved:
    /// one would be silently ignored, and which is not this layer's to choose.
    DuplicateRestraintId,
    /// A restraint holds no component at zero. Refused, because a restraint
    /// that constrains nothing is a record whose intent cannot be acted on --
    /// not a weaker restraint.
    NoComponents,
    /// The `FaceSelector` is malformed on its own terms.
    TargetInvalid,
    /// The target names no face of the body as it is now. This is where a face
    /// the naming chain never attributed lands, including a drilled hole's
    /// cylindrical wall. NOTHING NEARBY IS REBOUND.
    TargetUnresolved,
    /// The target resolved and the mapping gave it no facet.
    TargetWithoutFacets,
    /// The target resolved to facets and those facets to no node. Refused
    /// rather than published as a restraint constraining nothing.
    TargetWithoutNodes,
    /// The `MeshDofMap` offered is not the numbering of this model's mesh, so
    /// its indices would mean something else. Checked by `MeshStamp` and node
    /// count, for the reason `MeshDofMap::describes` records.
    NumberingIsForADifferentMesh,
};

[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::string_view
toString(RestraintProblem problem) noexcept;

/// What one restraint resolved to, for diagnostics and later visualisation.
///
/// DERIVED, NEVER AUTHORITY. A reader uses it to see which restraint reached
/// how much of the mesh; nothing reads it back as intent.
struct RestraintResolution {
    RestraintId restraint{};
    /// Boundary facets the target resolved to.
    std::size_t facets = 0;
    /// Unique current nodes those facets carry.
    std::size_t nodes = 0;
    RestraintComponents components{};
    /// How many degrees of freedom THIS restraint resolved to, before any
    /// union with other restraints: `constrained.size()`, which equals
    /// `nodes * components.count()` because a restraint's nodes are unique and
    /// its components are a mask.
    std::size_t degreesOfFreedom = 0;
    /// The degrees of freedom THIS restraint resolved to, ascending and unique
    /// within the restraint. `constrained.size() == degreesOfFreedom`.
    ///
    /// ADDED BY P17-REACTION-001 (ADR-041), AND IT IS A VALUE ALREADY
    /// COMPUTED. The loop that builds this already asks
    /// `numbering.indexOf(NodalDof{node, component})` for every degree of
    /// freedom it resolves and verifies the answer; it used to discard the
    /// index and keep only the `NodalDof` in the global union. Keeping it is
    /// what lets a reaction be attributed to the restraint that caused it,
    /// WITHOUT re-resolving the target after the solve -- which would assume
    /// the geometry mapping reproduces the same set and is exactly how a
    /// source mismatch hides.
    ///
    /// DERIVED, NEVER PERSISTED, like the counts beside it. A `DofIndex` is
    /// solver-local: it means a row of one numbering of one mesh, and storing
    /// one as intent is the defect P17-DATA-001 exists to prevent.
    ///
    /// ACROSS restraints these lists may INTERSECT, which is the whole reason
    /// `constraints()` is a union. P17-REACTION-001's attribution policy is
    /// built on that intersection rather than around it.
    std::vector<DofIndex> constrained{};

    friend bool operator==(const RestraintResolution&, const RestraintResolution&) = default;
};

/// The constrained degrees of freedom a set of restraints produces on one
/// current mesh.
///
/// POSSESSION IS THE EVIDENCE that every restraint resolved, every target had
/// nodes, and the whole set was numbered against the mesh named by `mesh()`.
/// There is no public constructor and one friend, so a consumer cannot be
/// handed a partially prepared set -- which is the point of preparing them
/// atomically.
///
/// THE CONSTRAINED SET IS A UNION, NOT A LIST. Two restraints that reach the
/// same degree of freedom contribute it once: overlapping faces share edge
/// nodes, and three single-component restraints on one face are a fixed
/// support. `constraints()` is P17-DOF's `ConstraintSet`, which is ascending
/// and unique by construction.
class BETTERCAD_STRUCTURAL_EXPORT PreparedRestraints {
public:
    /// The mesh these indices belong to.
    [[nodiscard]] const meshing::MeshStamp& mesh() const noexcept { return mesh_; }

    /// Whether this set describes @p mesh. The stamp AND the node count, for
    /// the reason `MeshDofMap::describes` records: a `MeshStamp` identifies the
    /// BUILDER, not the snapshot.
    [[nodiscard]] bool describes(const meshing::Mesh& mesh) const noexcept {
        return mesh.owns(mesh_) && mesh.nodes().size() == nodeCount_;
    }

    /// The constrained degrees of freedom, ascending and unique. P17-DOF's own
    /// type, so `buildFreeEquationMap` takes it directly.
    [[nodiscard]] const ConstraintSet& constraints() const noexcept { return constraints_; }

    /// What each restraint resolved to, in the order the restraints were
    /// given. Sums over these will EXCEED `constraints().size()` wherever
    /// restraints overlap, which is the point of recording both.
    [[nodiscard]] std::span<const RestraintResolution> resolutions() const noexcept {
        return resolutions_;
    }

    /// Every node any restraint reached, ascending and unique.
    [[nodiscard]] std::span<const meshing::NodeId> nodes() const noexcept { return nodes_; }

    friend bool operator==(const PreparedRestraints&, const PreparedRestraints&) = default;

private:
    /// THE ONLY CONSTRUCTOR, AND THERE IS NO DEFAULT ONE. That is not a style
    /// choice: `ConstraintSet`'s own constructor is private to
    /// `buildConstraintSet`, so a `PreparedRestraints` CANNOT EXIST without a
    /// constraint set that was already validated against a numbering. ADR-036's
    /// gate on the member propagates to the whole type, and a half-built value
    /// is unrepresentable rather than merely discouraged.
    PreparedRestraints(meshing::MeshStamp mesh, std::size_t nodeCount, ConstraintSet constraints,
                       std::vector<RestraintResolution> resolutions,
                       std::vector<meshing::NodeId> nodes)
        : mesh_(std::move(mesh)), nodeCount_(nodeCount), constraints_(std::move(constraints)),
          resolutions_(std::move(resolutions)), nodes_(std::move(nodes)) {}

    friend BETTERCAD_STRUCTURAL_EXPORT Result<PreparedRestraints>
    prepareStructuralRestraints(const StructuralModel& model, const MeshDofMap& numbering,
                                std::span<const StructuralRestraint> restraints);

    meshing::MeshStamp mesh_;
    std::size_t nodeCount_;
    ConstraintSet constraints_;
    std::vector<RestraintResolution> resolutions_;
    std::vector<meshing::NodeId> nodes_;
};

/// Resolves @p restraints into the constrained DOF set of @p model's mesh,
/// numbered by @p numbering.
///
/// ATOMIC: every restraint is resolved before anything is published, and one
/// failure refuses the whole set. A partially prepared set is not a weaker
/// restraint case, it is one that claims to be the user's and is not.
///
/// IT TAKES A `StructuralModel`, which is how the stale-input question is
/// answered without a check here: possession proves the geometry and the mesh
/// are current and that the mapping came from the same lookup (ADR-036).
///
/// AND IT TAKES THE `MeshDofMap` RATHER THAN BUILDING ONE, because
/// `P17-ASSEMBLY-001` will need the same numbering for the stiffness and the
/// loads, and two numberings of one mesh would be two answers to the same
/// question. The pair is checked: a numbering for another mesh is refused.
///
/// THE NUMBERING BINDING IS CHECKED FIRST, BEFORE THE RESTRAINTS THEMSELVES,
/// which is why even an empty restraint set is refused against a numbering for
/// another mesh. The published set carries this model's `MeshStamp` and a
/// `ConstraintSet` carrying the numbering's; if the two could differ, an empty
/// result would be a value whose two halves name different meshes.
///
/// AN EMPTY RESTRAINT SET SUCCEEDS, with an empty `ConstraintSet`. A model with
/// no restraints is a real model whose stiffness matrix is singular, and
/// refusing it here would report a physics problem as a resolution failure --
/// `P17-SOLVE-001` is where the rigid-body modes are visible. That separation
/// is deliberate and is tested.
///
/// DETERMINISTIC. Restraints are processed in the order given; facets come
/// from P16's ascending set; nodes come from `boundaryNodesOf`, already sorted
/// and deduplicated; and the constrained indices are accumulated into
/// `ConstraintSet`, which sorts them. Nothing is built from an unordered
/// container, so the same inputs give the same set -- index for index -- in
/// every build configuration, and the insertion order of the restraints cannot
/// change it.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Result<PreparedRestraints>
prepareStructuralRestraints(const StructuralModel& model, const MeshDofMap& numbering,
                            std::span<const StructuralRestraint> restraints);

/// The problem `prepareStructuralRestraints` would report, or none if it would
/// succeed. The same checks in the same order.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::optional<RestraintProblem>
structuralRestraintProblem(const StructuralModel& model, const MeshDofMap& numbering,
                           std::span<const StructuralRestraint> restraints);

} // namespace bettercad::structural
