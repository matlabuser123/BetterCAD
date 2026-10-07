#pragma once

// P17-LOAD-001 -- turning canonical load intent into a current-mesh nodal
// force field.
//
// THE AUTHORITY CHAIN, AND THE ONE RULE THAT MATTERS MOST.
//
//     canonical load intent          LoadId + FaceName + a physical value
//              |
//     P16's GeometryMeshMap          boundaryFacetsOf(map, FaceName)
//              |
//     current boundary facets        ElementIds of THIS mesh, derived
//              |
//     equivalent nodal forces        NodeId -> Force3D, derived
//
// **A FACET HANDLE IS NEVER LOAD AUTHORITY.** A `FaceName` is canonical and
// survives a remesh; a `BoundaryFacet` handle is a mesh-local index that a
// remesh invalidates. So the canonical face loads in `StructuralLoad.hpp`
// store a `FaceName` and nothing else about the mesh, and the facets are
// resolved again on every call to `prepareStructuralLoads`.
//
// AND P17 DOES NOT CLASSIFY GEOMETRY. P16-MAP-001 owns the question of which
// triangles lie on which CAD face, and this file asks it rather than answering
// it again: no distance-to-plane tolerance, no nearest face, no
// normal-similarity threshold, no centroid test. Facet geometry is read only
// AFTER P16 has resolved the mapping, and then only to integrate over it.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/math/Vector.hpp>
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/structural/Export.hpp>
#include <bettercad/structural/StructuralAnalysis.hpp>
#include <bettercad/structural/StructuralLoad.hpp>
#include <bettercad/structural/StructuralMaterial.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace bettercad::structural {

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

/// Why a set of loads cannot be turned into a nodal force field.
///
/// EVERY VALUE IS REACHABLE, which constrained the list sharply, and two the
/// brief names are deliberately absent:
///
/// ```text
/// Ambiguous   P16's MappingState has only Resolved and Unresolved, and its
///             header says why: a FaceName "can be ambiguous, which
///             P12-STREF-001 documents, and that is exactly why this layer
///             does not map through one". There is no ambiguous state to
///             report, so reporting one would be a value nothing returns
/// Stale       requireStructuralModel already refuses a stale mesh and a
///             stale mapping, and possession of a StructuralModel IS that
///             proof (ADR-036). A load preparation that takes one cannot be
///             handed a stale map, so the check belongs where it already is
///             and InputProblem::MeshStale is where it is reported
/// ```
///
/// That is the same discipline P17-ARCH-001 applied when it found three
/// unreachable values in its own first draft and deleted them.
enum class LoadProblem : std::uint8_t {
    /// Two loads carry the same `LoadId`. Refused rather than resolved: one of
    /// them would be silently ignored, and which is not this layer's to
    /// choose.
    DuplicateLoadId,
    /// A load value is not finite -- a NaN or an infinite force component,
    /// traction component, pressure or acceleration. Refused before any
    /// integration, so nothing non-finite can reach a right-hand side.
    NonFiniteValue,
    /// A `FaceName` names no face of the current body. This is also where the
    /// unsupported drilled-hole wall lands: `cutHole` names a hole's flat
    /// faces and not its cylindrical wall, so a reference to that wall
    /// resolves to nothing and is refused. NOTHING NEARBY IS SUBSTITUTED.
    TargetUnresolved,
    /// The `FaceSelector` is malformed on its own terms -- core's
    /// `validate(FaceSelector)`, not a second opinion.
    TargetInvalid,
    /// The target resolved, and the mapping gave it no boundary facet. Refused
    /// rather than treated as zero load: a load the user asked for that
    /// integrates over nothing is not a zero load, it is a model whose
    /// attribution chain lost a face.
    TargetWithoutFacets,
    /// A nodal force names a node the current mesh does not have, or belongs
    /// to a different mesh. Never resolved to the nearest node.
    NodeNotInMesh,
    /// A boundary facet has zero or non-finite area. P16 refuses a degenerate
    /// triangle, so this is a defensive result check rather than an invented
    /// input tolerance.
    DegenerateFacet,
    /// A gravity load is present and the resolved material carries no
    /// density. P15 owns the density and reports it missing; there is no
    /// default, and in particular no 7850.
    DensityMissing,
};

[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::string_view toString(LoadProblem problem) noexcept;

// ---------------------------------------------------------------------------
// Derived nodal load field
// ---------------------------------------------------------------------------

/// One node's total applied force, summed over every load that reaches it.
struct NodalLoad {
    meshing::NodeId node{};
    Force3D force{};

    friend bool operator==(const NodalLoad&, const NodalLoad&) = default;
};

/// What one load contributed, for diagnostics and later visualisation.
///
/// DERIVED, NEVER AUTHORITY. These resultants are computed from the load's
/// canonical value and the current mesh; they do not replace the input, and
/// nothing reads them back as intent.
struct LoadContribution {
    LoadId load{};
    LoadKind kind = LoadKind::NodalForce;
    /// Boundary facets integrated over, or 0 for a nodal force, or the
    /// tetrahedron count for gravity.
    std::size_t elements = 0;
    /// Total area integrated over, for a surface load.
    Area area{};
    Force3D resultant{};

    friend bool operator==(const LoadContribution&, const LoadContribution&) = default;
};

/// The nodal force field a set of loads produces on one current mesh.
///
/// POSSESSION IS THE EVIDENCE that every load resolved, every value was
/// finite, and the whole set was integrated against the mesh named by
/// `mesh()`. There is no public constructor and one friend, so a consumer
/// cannot be handed a partially prepared field -- which is the point of
/// preparing them atomically.
///
/// DERIVED AND DISPOSABLE. It is bound to one `MeshStamp` and must be rebuilt
/// after a remesh or any load edit; it is never persisted, and
/// `P17-PERSIST-001` will store the canonical loads instead.
///
/// ORDERING IS DEFINED: `nodal()` is ascending by `NodeId`, and only nodes
/// that actually receive a force appear. A node with no load is absent rather
/// than present with a zero force, so the field's size says how much of the
/// mesh is loaded.
class BETTERCAD_STRUCTURAL_EXPORT PreparedLoads {
public:
    /// The mesh these nodal forces belong to.
    [[nodiscard]] const meshing::MeshStamp& mesh() const noexcept { return mesh_; }

    /// Whether this field describes @p mesh. Asked through the mesh's own
    /// `owns()`, plus the node count, for the reason `MeshDofMap::describes`
    /// records: a `MeshStamp` identifies the BUILDER, not the snapshot.
    [[nodiscard]] bool describes(const meshing::Mesh& mesh) const noexcept;

    /// The loaded nodes, ascending by handle.
    [[nodiscard]] std::span<const NodalLoad> nodal() const noexcept { return nodal_; }

    /// What each load contributed, in the order the loads were given.
    [[nodiscard]] std::span<const LoadContribution> contributions() const noexcept {
        return contributions_;
    }

    /// The total applied force: the sum over `nodal()`, in its own ascending
    /// order, which is what makes the answer reproducible.
    [[nodiscard]] Force3D resultantForce() const noexcept;

    /// The total applied moment about @p origin.
    ///
    /// THE ORIGIN IS EXPLICIT because a moment about an unstated point is not
    /// a quantity. Needs the mesh for the node positions, and refuses a mesh
    /// this field does not describe.
    [[nodiscard]] Result<Moment3D> resultantMomentAbout(const meshing::Mesh& mesh,
                                                        const Point3D& origin) const;

    friend bool operator==(const PreparedLoads&, const PreparedLoads&) = default;

private:
    PreparedLoads() = default;

    friend BETTERCAD_STRUCTURAL_EXPORT Result<PreparedLoads>
    prepareStructuralLoads(const StructuralModel& model, const StructuralMaterial& material,
                           std::span<const StructuralLoad> loads);

    meshing::MeshStamp mesh_{};
    std::size_t nodeCount_ = 0;
    std::vector<NodalLoad> nodal_{};
    std::vector<LoadContribution> contributions_{};
};

// ---------------------------------------------------------------------------
// Preparation
// ---------------------------------------------------------------------------

/// Turns @p loads into a nodal force field on @p model's current mesh.
///
/// ATOMIC: every load is resolved and integrated before anything is published,
/// and one failure refuses the whole set. A partially prepared field is not a
/// smaller load case, it is a load case that claims to be the user's and is
/// not.
///
/// IT TAKES A `StructuralModel`, which is how the stale-input question is
/// answered without a check here: possession of one proves the geometry is
/// current, the mesh is current, and the mapping and the mesh came from one
/// lookup (ADR-036). A stale mapping therefore cannot be presented, and
/// `InputProblem::MeshStale` is where that refusal lives.
///
/// IT TAKES A `StructuralMaterial` for gravity only. Its `density()` is
/// present exactly when the analysis mode is
/// `LinearStaticWithGravity` -- P17-MAT-001 reads P15's requirement table to
/// decide that -- so a gravity load with a no-gravity material is refused as
/// `DensityMissing`, and a load set without gravity never consults the
/// density at all.
///
/// SUPERPOSITION IS THE SEMANTICS. Two loads on the same face, or on the same
/// node, ADD. Overlapping targets are not an error: linear statics
/// superposes, and refusing them would be deciding the user's model for them.
///
/// DETERMINISTIC. Loads are processed in the order given; facets come from
/// P16's ascending deduplicated set; nodal contributions accumulate into a
/// map keyed on `NodeId` and are emitted ascending. Nothing is built from an
/// unordered container, so the same inputs give the same field -- and the same
/// floating-point sums -- in every build configuration.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Result<PreparedLoads>
prepareStructuralLoads(const StructuralModel& model, const StructuralMaterial& material,
                       std::span<const StructuralLoad> loads);

/// The problem `prepareStructuralLoads` would report, or none if it would
/// succeed. The same checks in the same order, for a caller that wants to
/// present or count the reason rather than parse a message.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::optional<LoadProblem>
structuralLoadProblem(const StructuralModel& model, const StructuralMaterial& material,
                      std::span<const StructuralLoad> loads);

// ---------------------------------------------------------------------------
// Facet integration, exposed for its own sake
// ---------------------------------------------------------------------------

/// The outward area vector of one boundary facet: `(1/2)(p2-p1) x (p3-p1)`.
///
/// Its magnitude is the facet's area and its direction is the outward normal,
/// which is why the two are returned together rather than separately -- a
/// pressure needs exactly this product, and computing an area and a unit
/// normal separately is how a factor of `2A` gets applied twice.
///
/// OUTWARD BECAUSE P16'S BOUNDARY IS. The chain is traceable rather than
/// assumed: `generateVolumeMesh` stores boundary triangles "with the winding
/// the tetrahedra imply", `tetrahedralBoundary` keeps "the outward winding of
/// the first tetrahedron that claimed it", and `kTetFaces`' four windings
/// "give outward normals" for a positively oriented tetrahedron -- which P16
/// refuses a mesh without. A test checks that chain against the owning
/// tetrahedron rather than trusting it.
///
/// Returns a `Vector3D` of SI square metres rather than a dimensioned type:
/// there is no `Area3D` in the tree and inventing one for one call site would
/// be the speculative abstraction the project forbids. The magnitude is
/// available as an `Area` from `facetArea`.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Vector3D facetAreaVector(const Point3D& p1,
                                                                   const Point3D& p2,
                                                                   const Point3D& p3) noexcept;

/// The facet's area: the magnitude of `facetAreaVector`. Unsigned, because a
/// triangle in three dimensions has no sign -- it has a normal.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Area facetArea(const Point3D& p1, const Point3D& p2,
                                                         const Point3D& p3) noexcept;

/// The equivalent nodal force each corner of a facet receives from a constant
/// traction over it: `A t / 3`.
///
/// The consistent load vector for a three-node linear triangle with a constant
/// load is `integral(N^T t dA)`, and each shape function integrates to `A/3`
/// over its own triangle -- so all three corners receive the same third of the
/// total, and they sum to `A t` exactly. Not `A t / 2`, and not the whole
/// force on one node.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Force3D facetNodalForce(Area area,
                                                                  const Traction3D& traction) noexcept;

} // namespace bettercad::structural
