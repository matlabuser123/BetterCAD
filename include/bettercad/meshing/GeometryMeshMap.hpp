#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/meshing/Export.hpp>
#include <bettercad/meshing/GeometryPreparation.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Geometry <-> mesh correspondence (P16-MAP-001).
//
// WHY THIS EXISTS. A load or a restraint is an engineering statement about a
// FACE OF A PART -- "this bore is fixed", "this flange carries the pressure" --
// and the mesh it is eventually applied to is derived state that a remesh
// replaces wholesale. So canonical intent must name CAD geometry and the mesh
// entities must be derived from it, every time:
//
//     Load / restraint
//         -> FaceName or NamedBoundarySet        canonical, persistable
//         -> current boundary facets / nodes     DERIVED, recomputed per mesh
//         -> P17 assembly
//
// A solver requirement stored as "node 174" is wrong the moment the mesh
// changes, and nothing would say so.
//
// WHAT P16 GUARANTEES:
//
//     the CURRENT regenerated CAD geometry reference
//         <-> the CURRENT generated mesh entities
//
//   under BetterCAD's existing stable-reference semantics (P12-STREF-001).
//
// WHAT P16 DOES NOT GUARANTEE: that an arbitrary topology-changing edit
// preserves a face's semantic identity. Where P12-STREF's naming carries a
// name through an operation's history, a reference survives; where it does
// not, this layer reports `Unresolved` and binds to nothing. It never picks
// the nearest replacement, and it never infers a face from geometry. Permanent
// semantic topology is P21's problem and is not solved, approximated or
// implied here.
//
// HOW A FACET IS ATTRIBUTED: generation provenance, end to end, with no
// tolerance anywhere.
//
//     CAD face i of listFaces(body)
//         -> geometry::Mesh::faces[i]            the kernel triangulates per face
//         -> EngineeringSurfaceMesh::faceTriangles[i]
//         -> VolumeMesh::boundarySourceFaces     carried by EXACT position
//
// The last step is exact because `generateVolumeMesh` already refuses a result
// whose boundary is not positionally identical to the surface it was given.
// Nothing here classifies a triangle by its coordinates, so two coincident CAD
// faces stay distinct and no tolerance has to be chosen or defended.
//
// EDGES ARE NOT MAPPED, and that is a decision rather than an omission. The
// surface mesh is generated from a PER-FACE triangulation, so a node on a
// shared edge is unified by position and is shared -- but which CAD EDGE it
// lies on is recorded nowhere, because the kernel's per-face triangulation does
// not report edge discretisation through this path. That leaves exactly two
// possible implementations: add edge provenance to the geometry layer, for a
// consumer that does not exist yet; or decide a mesh edge lies on a CAD edge
// because it is NEAR one, which is geometric classification with a tolerance
// and is the thing this file exists to avoid. P17 has stated no edge
// requirement -- a line load, a symmetry edge and edge-local refinement are all
// listed as possible FUTURE uses -- so neither was built. The node sets a face
// selection derives are the mechanism an edge selection would use, and the
// reference type is P12-STREF's to define.
namespace bettercad::meshing {

/// How a geometry reference resolved against the current geometry.
///
/// TWO STATES, and the absence of a third is a finding rather than an
/// omission. `Ambiguous` does not appear because it is not reachable through
/// this layer's reference type: a `FaceName` is matched by VALUE against the
/// names the kernel's history carried, and a name that landed on several faces
/// -- a face split in two carries its name on both parts -- denotes all of
/// them. That is a union with a reported multiplicity, not a choice between
/// candidates. A geometric reference (`geometry::FaceSignature`) genuinely can
/// be ambiguous, which P12-STREF-001 documents, and that is exactly why this
/// layer does not map through one.
///
/// `Unsupported` does not appear either. P16-SIZE-001 needs it because only a
/// planar or cylindrical face can become a sizing region; attribution needs no
/// surface kind at all, so a cone, a sphere, a torus and a B-spline map
/// exactly as a plane does.
enum class MappingState : std::uint8_t {
    /// The reference names at least one face of the current body.
    Resolved,
    /// It names none. The reference is KEPT and reported; nothing is rebound.
    Unresolved,
};

[[nodiscard]] BETTERCAD_MESHING_EXPORT std::string_view toString(MappingState state) noexcept;

/// What a mapping report or a refused query is complaining about.
enum class MappingIssueKind : std::uint8_t {
    /// The `FaceSelector` is malformed on its own terms. Core's own rule
    /// (`validate(FaceSelector)`), not a second opinion.
    GeometryReferenceInvalid,
    /// The reference names no face of the current body.
    GeometryReferenceUnresolved,
    /// The mesh was not built from the geometry that is current now, so no
    /// correspondence it offers is about the present model.
    MappingStale,
    /// An element handle is not a boundary triangle of this mesh: it names
    /// nothing, or it names a tetrahedron.
    MeshFacetInvalid,
    /// A face of a tetrahedron that is not on the boundary, or a boundary
    /// triangle the surface carried no attribution for. There is no CAD
    /// boundary face, and none is invented.
    NoBoundaryCorrespondence,
    /// A CAD face of the meshed body that the mesh gave no facet. Reported
    /// rather than passed over: for an ordinary meshable face it means the
    /// attribution chain lost it.
    FaceWithoutFacets,
    /// A boundary facet attributed to more than one CAD face. Structurally
    /// impossible by construction, and checked anyway -- see
    /// `GeometryMeshMappingReport::facetsWithSeveralFaces`.
    FacetAttributedTwice,
};

[[nodiscard]] BETTERCAD_MESHING_EXPORT std::string_view toString(MappingIssueKind kind) noexcept;

/// One structured complaint. Structured rather than a sentence, so a CLI or a
/// GUI can present it without parsing text; `message` is a fallback for logs.
struct MappingIssue {
    MappingIssueKind kind = MappingIssueKind::MappingStale;
    /// The reference the issue is about, when it is about one.
    std::optional<FaceName> reference{};
    /// The element the issue is about, when it is about one.
    std::optional<ElementId> element{};
    /// The CAD face index the issue is about, when it is about one.
    std::optional<std::size_t> face{};
    std::string message;

    friend bool operator==(const MappingIssue&, const MappingIssue&) = default;
};

/// One CAD boundary face of the meshed body, and the facets it produced.
struct MappedFace {
    /// The face's index in `geometry::listFaces(body)` order.
    ///
    /// A CORRELATION HANDLE FOR ONE MAP, never an identity: it is not
    /// persisted, it is not comparable between two maps, and `listFaces`
    /// documents that the kernel's order carries no meaning. A face's
    /// canonical reference is a `FaceName` from `names`.
    std::size_t index = 0;
    /// What kind of surface it lies on, from the kernel. Reported because it
    /// is what tells a reader why a face has no name or no signature.
    geometry::FaceSurface surface = geometry::FaceSurface::Other;
    /// The persistent names this face carries, sorted (P12-STREF-001).
    ///
    /// MAY BE EMPTY, and that is a real state rather than a defect: a face is
    /// named by the operation that generates it, and some operations name only
    /// some of their faces. `cutHole` names a hole's flat faces and not its
    /// cylindrical wall, so a bored hole's wall has no name -- see
    /// `unnamedFaceCount` and this milestone's known limitations. An unnamed
    /// face is still mapped and is still answerable in reverse; what it cannot
    /// be is the target of a forward query, because there is nothing to ask
    /// with.
    std::vector<FaceName> names{};
    /// For a planar face, its geometric reference. Absent otherwise.
    std::optional<geometry::FaceSignature> signature{};
    /// For a cylindrical face, its axis and radius. Absent otherwise.
    std::optional<geometry::CylindricalFace> cylinder{};
    /// The current mesh's boundary Triangle3 elements this face produced,
    /// ascending. Invalidated by a remesh, together with the handles.
    std::vector<ElementId> facets{};

    friend bool operator==(const MappedFace&, const MappedFace&) = default;
};

/// What a map's own consistency check found.
struct GeometryMeshMappingReport {
    std::size_t cadFaceCount = 0;
    /// Faces carrying at least one `FaceName`, and faces carrying none.
    std::size_t namedFaceCount = 0;
    std::size_t unnamedFaceCount = 0;
    /// Boundary Triangle3 elements of the mesh.
    std::size_t boundaryFacetCount = 0;
    std::size_t mappedFacetCount = 0;
    std::size_t unmappedFacetCount = 0;
    /// Attributed to more than one CAD face. Zero by construction; counted
    /// from the per-face lists so that the construction is checked rather
    /// than asserted in a comment.
    std::size_t facetsWithSeveralFaces = 0;
    /// CAD faces the mesh gave no facet.
    std::size_t facesWithoutFacets = 0;
    std::vector<MappingIssue> issues{};

    /// Every boundary facet attributed exactly once, and every CAD face
    /// represented.
    ///
    /// Note what this does NOT require: that every face be named. An unnamed
    /// face is a limit of the naming infrastructure, not of the
    /// correspondence, and conflating the two would make a complete mapping
    /// look broken.
    [[nodiscard]] bool complete() const noexcept {
        return boundaryFacetCount > 0 && unmappedFacetCount == 0 &&
               facetsWithSeveralFaces == 0 && facesWithoutFacets == 0;
    }

    friend bool operator==(const GeometryMeshMappingReport&, const GeometryMeshMappingReport&) = default;
};

/// The correspondence between one body's current geometry and one mesh.
///
/// IT BELONGS TO ONE GEOMETRY AND ONE MESH, and says which: `source`,
/// `revision` and `meshStamp` are recorded so that a map can never be applied
/// to a different pair. Every query checks them.
///
/// Value-semantic and comparable, with no pointer, no kernel handle and no
/// backend tag, so commands and undo (`P16-CMD-001`) and serialisation
/// (`P16-PERSIST-001`) can be added to the intent it resolves without this
/// type changing.
class BETTERCAD_MESHING_EXPORT GeometryMeshMap {
public:
    /// The feature whose body this describes.
    [[nodiscard]] ObjectId source() const noexcept { return source_; }
    /// The geometry stamp the mesh was built from (P16-GEOM-001).
    [[nodiscard]] const GeometryRevision& revision() const noexcept { return revision_; }
    /// Which mesh generation the derived handles belong to (ADR-031).
    [[nodiscard]] const MeshStamp& meshStamp() const noexcept { return meshStamp_; }

    /// The CAD boundary faces, in `geometry::listFaces(body)` order.
    [[nodiscard]] std::span<const MappedFace> faces() const noexcept { return faces_; }

    [[nodiscard]] const GeometryMeshMappingReport& report() const noexcept { return report_; }

    friend bool operator==(const GeometryMeshMap&, const GeometryMeshMap&) = default;

private:
    /// CONSTRUCTIBLE ONLY BY THE BUILDING PATH, as `VolumeMesh` is (ADR-030):
    /// possessing a map is the evidence that a geometry and a mesh were checked
    /// to be the same pair, and a default-constructed one would be a map of
    /// nothing that a caller could hand on as though it meant something.
    GeometryMeshMap() = default;

    friend BETTERCAD_MESHING_EXPORT Result<GeometryMeshMap>
    buildGeometryMeshMap(const MeshableGeometry&, const VolumeMesh&);

    ObjectId source_{};
    GeometryRevision revision_{};
    MeshStamp meshStamp_{};
    std::vector<MappedFace> faces_{};
    std::map<ElementId, std::size_t> faceOfFacet_{};
    GeometryMeshMappingReport report_{};
};

/// Builds the correspondence between @p geometry and @p mesh.
///
/// Fails with FailedPrecondition when the two are not the same pair -- a
/// different feature, or a mesh built from a different geometry revision --
/// because a correspondence between mismatched inputs is not a weaker answer,
/// it is a wrong one.
///
/// READ-ONLY in both arguments. Nothing here regenerates CAD, heals a shape,
/// moves a node or re-triangulates anything.
[[nodiscard]] BETTERCAD_MESHING_EXPORT Result<GeometryMeshMap>
buildGeometryMeshMap(const MeshableGeometry& geometry, const VolumeMesh& mesh);

/// Prepares @p feature's geometry and maps it against @p mesh.
///
/// The ordinary entry point, so a caller cannot forget the currency check.
/// Every refusal `requireMeshableGeometry` can produce is returned unchanged,
/// so a stale or failed model is unmappable rather than merely discouraged.
[[nodiscard]] BETTERCAD_MESHING_EXPORT Result<GeometryMeshMap>
geometryMeshMapFor(const Document& document, const features::Regenerator& regenerator, ObjectId feature,
                   const VolumeMesh& mesh);

/// How one requested reference resolved.
struct FaceResolution {
    FaceName reference{};
    MappingState state = MappingState::Unresolved;
    /// The CAD faces this name landed on, as indices into
    /// `GeometryMeshMap::faces()`. More than one is legitimate: the kernel's
    /// history carries a name to every face a named face became.
    std::vector<std::size_t> faces{};
    /// Their facets, ascending and without repeats.
    std::vector<ElementId> facets{};

    friend bool operator==(const FaceResolution&, const FaceResolution&) = default;
};

/// The current boundary facets of one or more references.
///
/// UNRESOLVED IS A STATUS, NOT AN EMPTY LIST. A reference that no longer
/// resolves is reported in `requested` with `MappingState::Unresolved`, and
/// `fullyResolved()` is false. Collapsing it to an empty result would make a
/// deleted face indistinguishable from a face that is simply not meshed.
struct BoundaryFacetSet {
    /// One entry per reference asked about, in the order asked.
    std::vector<FaceResolution> requested{};
    /// The union of the resolved facets, ascending and without repeats.
    std::vector<ElementId> facets{};

    [[nodiscard]] bool fullyResolved() const noexcept {
        for (const FaceResolution& resolution : requested) {
            if (resolution.state != MappingState::Resolved) {
                return false;
            }
        }
        return !requested.empty();
    }

    friend bool operator==(const BoundaryFacetSet&, const BoundaryFacetSet&) = default;
};

/// The current boundary facets of @p reference.
///
/// Fails with InvalidArgument for a malformed selector. An unresolvable
/// reference is NOT a failure: it comes back as `MappingState::Unresolved`,
/// because a named set must keep reporting a reference it can no longer bind.
[[nodiscard]] BETTERCAD_MESHING_EXPORT Result<BoundaryFacetSet>
boundaryFacetsOf(const GeometryMeshMap& map, const FaceName& reference);

/// The union of several references' facets, deduplicated and ascending.
[[nodiscard]] BETTERCAD_MESHING_EXPORT Result<BoundaryFacetSet>
boundaryFacetsOf(const GeometryMeshMap& map, std::span<const FaceName> references);

/// Where a boundary facet came from.
struct FacetSource {
    ElementId facet{};
    /// Index into `GeometryMeshMap::faces()`.
    std::size_t face = 0;
    /// That face's names, possibly empty. See `MappedFace::names`.
    std::vector<FaceName> names{};

    friend bool operator==(const FacetSource&, const FacetSource&) = default;
};

/// The CAD face a boundary facet came from.
///
/// Fails with FailedPrecondition (`NoBoundaryCorrespondence`) for any handle
/// this map attributes to no CAD face: a boundary triangle the chain carried no
/// attribution for, a handle that names a tetrahedron, and a handle that names
/// nothing. THIS OVERLOAD CANNOT TELL THOSE APART, and says so rather than
/// claiming a sharper diagnosis than it can make -- it is given no mesh to ask.
/// The overload taking a `Mesh` distinguishes them, and is what an interior
/// face has to be asked about anyway.
///
/// Never guesses. A handle with no attribution gets no face.
[[nodiscard]] BETTERCAD_MESHING_EXPORT Result<FacetSource> sourceFaceOf(const GeometryMeshMap& map,
                                                                        ElementId facet);

/// One face of one tetrahedron: the ordinal is 0..3, in the winding order
/// `tetrahedralBoundary` uses.
struct TetrahedronFace {
    ElementId tetrahedron{};
    std::size_t ordinal = 0;

    friend bool operator==(const TetrahedronFace&, const TetrahedronFace&) = default;
};

/// The CAD face a tetrahedron's face came from, if it is on the boundary.
///
/// THE INTERIOR CASE IS THE POINT. A face shared by two tetrahedra has no CAD
/// boundary face, and this returns `NoBoundaryCorrespondence` for it rather
/// than the nearest exterior face. Membership is decided by the mesh's own
/// connectivity -- the boundary is the set of tetrahedron faces exactly one
/// tetrahedron uses, which is `tetrahedralBoundary`'s definition and not a
/// second one.
[[nodiscard]] BETTERCAD_MESHING_EXPORT Result<FacetSource>
sourceFaceOf(const GeometryMeshMap& map, const Mesh& mesh, const TetrahedronFace& face);

/// The nodes of @p facets, ascending and without repeats.
///
/// Derived from the facets, which are derived from the geometry: there is no
/// independent geometric node selector, so a node set cannot drift from the
/// face it is supposed to describe.
[[nodiscard]] BETTERCAD_MESHING_EXPORT Result<std::vector<NodeId>>
boundaryNodesOf(const GeometryMeshMap& map, const Mesh& mesh, std::span<const ElementId> facets);

/// The tetrahedra owning @p facets, ascending and without repeats.
///
/// Every facet of an external boundary is a face of exactly ONE tetrahedron,
/// and that is checked rather than assumed: a facet with no owner, or with
/// two, is reported instead of being resolved to the first found.
[[nodiscard]] BETTERCAD_MESHING_EXPORT Result<std::vector<ElementId>>
owningTetrahedraOf(const GeometryMeshMap& map, const Mesh& mesh, std::span<const ElementId> facets);

/// The volume region of a mapped body: its CAD identity and its elements.
///
/// ONE REGION, because P16 meshes one solid -- `generateVolumeMesh` refuses a
/// body holding more than one, so there is no second region to name and no
/// multi-region semantics to invent. The region's canonical identity is the
/// feature whose body it is; `region` is the mesh-local handle ADR-032 gives
/// it, and the two are deliberately separate things.
///
/// It carries NO material. P15 assigns materials to a document; combining a
/// material with a region is P17's, and doing it here would make a geometric
/// region and a material region the same object.
struct MappedRegion {
    ObjectId source{};
    RegionId region{};
    std::vector<ElementId> elements{};

    friend bool operator==(const MappedRegion&, const MappedRegion&) = default;
};

/// The mapped body's volume region: every Tet4 of @p mesh.
[[nodiscard]] BETTERCAD_MESHING_EXPORT Result<MappedRegion> regionOf(const GeometryMeshMap& map,
                                                                     const Mesh& mesh);

/// The CAD identity of the region @p element belongs to.
///
/// Fails with InvalidArgument when @p element is not a Tet4 of this mesh. A
/// boundary triangle is not a volume element and does not belong to a volume
/// region.
[[nodiscard]] BETTERCAD_MESHING_EXPORT Result<ObjectId> regionSourceOf(const GeometryMeshMap& map,
                                                                       const Mesh& mesh,
                                                                       ElementId element);

/// A solver-facing boundary set: a name, an identity, and the CAD geometry it
/// means.
///
/// WHAT IS CANONICAL IS THE GEOMETRY SELECTION. The facets are derived and are
/// recomputed for whatever mesh is current, which is the whole point: a
/// restraint on "fixed_end" keeps meaning the same face of the part through
/// every remesh, and nothing stores a facet handle that a remesh would
/// invalidate.
///
/// THE NAME IS NOT THE IDENTITY. `id` is, so two sets may carry the same
/// display name without one silently replacing the other, and a set may be
/// renamed and stay the same set. Two sets may also name the same face: two
/// different physics legitimately refer to one surface, and deduplicating a
/// user's intent because the resolved facets happen to be equal would be
/// deciding for them.
struct NamedBoundarySet {
    BoundarySetId id{};
    std::string name{};
    /// One or more CAD faces. The resolved set is their union.
    std::vector<FaceName> faces{};

    friend bool operator==(const NamedBoundarySet&, const NamedBoundarySet&) = default;
};

/// Checks a set on its own: a valid ID, a non-empty name, at least one face,
/// every selector valid, and no face listed twice. InvalidArgument otherwise.
[[nodiscard]] BETTERCAD_MESHING_EXPORT Result<void> validate(const NamedBoundarySet& set);

/// A named set against one mesh.
struct ResolvedBoundarySet {
    BoundarySetId id{};
    std::string name{};
    BoundaryFacetSet mapping{};

    /// Every face of the set resolved.
    [[nodiscard]] bool fullyResolved() const noexcept { return mapping.fullyResolved(); }

    friend bool operator==(const ResolvedBoundarySet&, const ResolvedBoundarySet&) = default;
};

/// Resolves @p set against @p map.
///
/// A set whose geometry no longer resolves comes back with its id, its name and
/// an `Unresolved` entry for each reference that failed. IT DOES NOT
/// DISAPPEAR, and it is never rebound to a face that is merely nearby or
/// similarly named.
[[nodiscard]] BETTERCAD_MESHING_EXPORT Result<ResolvedBoundarySet>
resolveBoundarySet(const NamedBoundarySet& set, const GeometryMeshMap& map);

} // namespace bettercad::meshing
