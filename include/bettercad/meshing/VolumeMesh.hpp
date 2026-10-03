#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/meshing/Export.hpp>
#include <bettercad/meshing/GeometryPreparation.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshSizing.hpp>
#include <bettercad/meshing/SurfaceMesh.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

// The engineering volume mesh: Tet4 from a validated boundary (P16-VOL-001).
//
//     authoritative CAD geometry        requireMeshableGeometry (P16-GEOM-001)
//              |
//     engineering surface mesh          generateSurfaceMesh     (P16-SURF-001)
//              |
//     tetrahedra                        generateTetrahedra      (the backend)
//              |
//     VolumeMesh                        here: translated, validated, refused
//
// WHAT THIS FILE ADDS, given how much already existed. The Tet4 element, the
// signed-volume convention, element validation and mesh identity are
// P16-DATA-001's and are reused unchanged -- a second definition of "is this
// tetrahedron valid" would be the duplicate-rule defect P16-GEOM-001 was
// already bitten by. What is new is the PIPELINE and its refusals: obtaining a
// boundary that is known to close, driving a backend across the ADR-033 seam,
// turning indices into mesh handles, proving the result fills the same space
// the boundary bounds, and refusing anything that does not.
//
// WHY A VolumeMesh CANNOT BE FABRICATED. ADR-030 requires that "a mesh reaches
// a solver only as a ValidatedMesh, which only the validating path can
// construct. The rule is enforced by the type system, not by documentation."
// `VolumeMesh`'s constructor is private and `generateVolumeMesh` is its only
// friend, so possessing one IS the evidence: there is no public way to build
// an unvalidated one and hand it on. ADR-030's general `ValidatedMesh` -- one
// token covering surface meshes too -- is still open, and naming it is P17's
// when it has a second kind of mesh to cover; this discharges the rule for the
// only mesh kind a solver can currently be offered.
namespace bettercad::meshing {

/// What a volume mesh is asked for.
///
/// Deliberately thin, for the reason `SurfaceMeshControls` already states:
/// sizing as a user-facing concept is P16-SIZE-001's, and a second canonical
/// home for it is the competing-state failure. These are the boundary controls
/// (passed through unchanged) and one optional size ceiling.
struct VolumeMeshControls {
    /// How the BOUNDARY is discretised (P16-SURF-001). Deflection and
    /// curvature live here because the boundary is the surface layer's, fixed
    /// before the backend sees it.
    SurfaceMeshControls surface{};
    /// How the VOLUME is sized (P16-SIZE-001): the global target and any
    /// face-local refinements.
    MeshSizingControls sizing{};

    friend bool operator==(const VolumeMeshControls&, const VolumeMeshControls&) = default;
};

/// How the tetrahedralisation compares with the boundary it was built from.
///
/// COUNTED, NOT SCORED. Aspect ratio, dihedral angles and sliver detection are
/// P16-QUALITY-001's; everything here is a structural identity that either
/// holds or does not.
struct VolumeConformity {
    /// Faces of the tetrahedralisation used by exactly one tetrahedron: the
    /// boundary of the volume mesh, computed from the tetrahedra alone and
    /// never taken from the backend's own surface report.
    std::size_t volumeBoundaryFaceCount = 0;
    /// Triangles of the input surface.
    std::size_t surfaceTriangleCount = 0;
    /// Boundary faces whose three node positions match no surface triangle.
    /// A conforming mesh has none.
    std::size_t unmatchedBoundaryFaceCount = 0;
    /// Surface triangles matched by no boundary face. A conforming mesh has
    /// none -- and this is the direction that catches a backend quietly
    /// re-triangulating or swallowing part of the boundary, which counting the
    /// other way round cannot see.
    std::size_t unmatchedSurfaceTriangleCount = 0;

    /// The volume mesh's boundary is exactly the surface it was given.
    [[nodiscard]] bool conforms() const noexcept {
        return unmatchedBoundaryFaceCount == 0 && unmatchedSurfaceTriangleCount == 0 &&
               volumeBoundaryFaceCount == surfaceTriangleCount;
    }

    friend bool operator==(const VolumeConformity&, const VolumeConformity&) = default;
};

/// Why a volume mesh could not be produced.
enum class VolumeMeshFailure : std::uint8_t {
    /// This build has no volume-meshing backend.
    BackendUnavailable,
    /// The boundary offered is not a usable surface: empty, or its own
    /// validation does not pass. `generateSurfaceMesh` cannot return such a
    /// surface, so this catches one assembled by hand.
    SurfaceNotUsable,
    /// The body holds more than one solid. See the note on
    /// `generateVolumeMesh`: supporting it is a later milestone's, and
    /// meshing only the first solid silently would be worse than refusing.
    MultipleSolids,
    /// The backend failed, or reported success without producing tetrahedra.
    BackendFailed,
    /// The translated mesh does not pass `validate()`: a missing node
    /// reference, a repeated one, a degenerate, inverted or duplicate
    /// tetrahedron, or a non-finite coordinate.
    InvalidMesh,
    /// The boundary of the tetrahedralisation is not the surface it was built
    /// from.
    BoundaryNotConforming,
    /// The tetrahedra do not fill the volume the boundary encloses.
    VolumeNotRecovered,
    /// A local sizing control's face does not resolve against the geometry.
    ///
    /// A REFUSAL rather than a warning: meshing with the controls that did
    /// resolve would produce a mesh that is not the one asked for while
    /// reporting success.
    SizingNotResolved,
};

[[nodiscard]] BETTERCAD_MESHING_EXPORT std::string_view toString(VolumeMeshFailure failure) noexcept;

/// A validated engineering volume mesh.
///
/// Immutable, and constructible only by `generateVolumeMesh`. Every field is a
/// record of something that was CHECKED before this object came to exist, not
/// a warning for the caller to act on.
class BETTERCAD_MESHING_EXPORT VolumeMesh {
public:
    /// Nodes, Tet4 elements and the boundary Triangle3 elements, in one
    /// P16-DATA `Mesh`. One mesh model, already qualified.
    [[nodiscard]] const Mesh& mesh() const noexcept { return mesh_; }

    [[nodiscard]] std::size_t nodeCount() const noexcept { return mesh_.nodeCount(); }
    [[nodiscard]] std::size_t tetrahedronCount() const noexcept { return mesh_.tetrahedra().size(); }
    [[nodiscard]] std::size_t boundaryTriangleCount() const noexcept {
        return mesh_.triangles().size();
    }

    /// How the result compares with the boundary it was built from.
    [[nodiscard]] const VolumeConformity& conformity() const noexcept { return conformity_; }

    /// Sum of the signed volumes of the tetrahedra. Positive, because an
    /// inverted or degenerate element would have been refused.
    ///
    /// A DERIVED quantity, never authority: CAD owns the volume.
    [[nodiscard]] Volume tetrahedralVolume() const noexcept { return tetrahedralVolume_; }

    /// Volume enclosed by the input surface. The tetrahedra tile exactly the
    /// polyhedron this bounds, so the two agree to arithmetic accumulation --
    /// which is what makes their comparison a real check rather than a
    /// tolerance to tune.
    [[nodiscard]] Volume boundaryVolume() const noexcept { return boundaryVolume_; }

    /// The volume the kernel integrates for the CAD body. For a curved body
    /// the tetrahedral volume approaches this FROM BELOW as the surface
    /// deflection tightens -- chords cut inside an arc -- so equality is the
    /// wrong expectation and is not asserted anywhere.
    [[nodiscard]] Volume cadVolume() const noexcept { return cadVolume_; }

    /// The feature whose geometry this mesh describes.
    ///
    /// Recorded so that staleness is not a question the CALLER has to get
    /// right. An `isStale(document, feature, mesh)` taking the feature
    /// separately answers confidently against whatever feature it was handed,
    /// so passing the wrong one yields a meaningless verdict with no way to
    /// tell -- a footgun found by this milestone's own adversarial review.
    ///
    /// This is provenance, not a CAD reference inside the mesh: no NODE and no
    /// ELEMENT names an ObjectId, which is what ADR-031 forbids and what
    /// P16-MAP-001 owns.
    [[nodiscard]] ObjectId source() const noexcept { return source_; }

    /// The geometry stamp this mesh was built from (P16-GEOM-001). A mesh
    /// whose revision differs from the current one is stale; see `isStale`.
    [[nodiscard]] const GeometryRevision& revision() const noexcept { return revision_; }

    /// The controls that produced it: what was got, not what was asked for.
    [[nodiscard]] const VolumeMeshControls& controls() const noexcept { return controls_; }

    /// Which CAD face produced each boundary triangle: the face's index in
    /// `geometry::listFaces(body)` order, for the body this mesh was built
    /// from.
    ///
    /// GENERATION PROVENANCE, carried from the engineering surface and keyed by
    /// exact position. It can be carried at all because `generateVolumeMesh`
    /// already REFUSES a result whose boundary is not positionally identical to
    /// the surface it was given, so every boundary triangle here is one of that
    /// surface's triangles and inherits its attribution. No backend tag, no
    /// geometric classification and no tolerance is involved.
    ///
    /// It is a within-mesh correlation handle, NOT a CAD reference and not an
    /// identity: the index is never persisted, every entry is invalidated by a
    /// remesh along with the `ElementId` that keys it, and the canonical name of
    /// a face is its `FaceName`. `GeometryMeshMap` is what joins the two.
    ///
    /// Empty when the surface carried no attribution, which is why a caller asks
    /// `GeometryMeshMap` rather than reading this.
    [[nodiscard]] const std::map<ElementId, std::size_t>& boundarySourceFaces() const noexcept {
        return boundarySourceFaces_;
    }

    /// How the sizing intent resolved against this body: the global bound
    /// actually used, whether it came from BetterCAD's default, the per-point
    /// restrictions sent to the backend, and what became of each local
    /// control.
    ///
    /// A RECORD of what was done, not authority. Canonical intent lives in
    /// `controls().sizing`; this is the derived resolution, and it is kept so
    /// that a caller can see which controls failed to resolve without
    /// re-running the resolution.
    [[nodiscard]] const ResolvedSizing& sizing() const noexcept { return sizing_; }

private:
    VolumeMesh() = default;

    friend BETTERCAD_MESHING_EXPORT Result<VolumeMesh>
    generateVolumeMesh(const MeshableGeometry&, const EngineeringSurfaceMesh&,
                       const VolumeMeshControls&);

    Mesh mesh_{};
    ObjectId source_{};
    ResolvedSizing sizing_{};
    VolumeConformity conformity_{};
    Volume tetrahedralVolume_{};
    Volume boundaryVolume_{};
    Volume cadVolume_{};
    GeometryRevision revision_{};
    VolumeMeshControls controls_{};
    std::map<ElementId, std::size_t> boundarySourceFaces_{};
};

/// Fills @p surface with tetrahedra and validates the result.
///
/// Takes the @p geometry the surface came from rather than a loose volume and
/// feature id: `MeshableGeometry` already carries the source, the kernel's
/// volume, the geometry revision and the solid count, all four of which this
/// needs, and all four of which are then guaranteed to describe the SAME body.
/// Assembling them from separate arguments is how a mesh ends up stamped with
/// another feature's revision.
///
/// THE SURFACE'S OWN VALIDATION REPORT IS NOT TRUSTED. `SurfaceValidation` is
/// a plain struct, so a caller can set every count to zero; this re-runs
/// `validateSurface` over the triangles and uses what it finds. That is the
/// difference between a gate and a label, and it is what makes
/// "a viewer tessellation cannot enter the solver path" structural rather than
/// a matter of which type name was used.
///
/// Refuses rather than returning a mesh that is wrong. Nothing here repairs a
/// tetrahedron, reverses an inverted one, drops a duplicate or relaxes a
/// comparison to make the numbers work.
[[nodiscard]] BETTERCAD_MESHING_EXPORT Result<VolumeMesh>
generateVolumeMesh(const MeshableGeometry& geometry, const EngineeringSurfaceMesh& surface,
                   const VolumeMeshControls& controls = {});

/// Prepares @p feature's geometry, surfaces it, then fills it.
///
/// The ordinary entry point, so a caller cannot skip the preparation step.
/// Every refusal `requireMeshableGeometry` can produce is returned unchanged,
/// which is what makes stale geometry unmeshable rather than merely
/// discouraged -- including the configuration-override refusal that ADR-030
/// requires.
///
/// MULTIPLE SOLIDS ARE REFUSED, and the choice is deliberate. ADR-032 gives a
/// mesh "one region per solid", which is the eventual design, but P16-SURF-001
/// triangulates a whole body into a single region and separating solids is not
/// its job either. Meshing a multi-solid body as though it were one region
/// would produce a mesh whose regions are a lie, and silently meshing only the
/// first solid would be worse. So the request is refused with
/// `MultipleSolids`, and per-solid regions remain a later milestone's work.
[[nodiscard]] BETTERCAD_MESHING_EXPORT Result<VolumeMesh>
volumeMeshFor(const Document& document, const features::Regenerator& regenerator, ObjectId feature,
              const VolumeMeshControls& controls = {});

/// Whether @p mesh describes geometry that has since changed.
///
/// Compares the recorded `GeometryRevision` with the one the mesh's own source
/// feature would carry now. The feature is taken from the MESH, not from the
/// caller, so there is no way to ask the question about the wrong body. True
/// also when the feature has gone, because a mesh of a deleted body is as
/// stale as a mesh can be.
///
/// This is the check that makes the configuration-safety requirement real: a
/// parameter edit, an upstream sketch change or a configuration switch all
/// move the revision, so a mesh held across one is detectably stale rather
/// than quietly reused.
[[nodiscard]] BETTERCAD_MESHING_EXPORT bool isStale(const Document& document,
                                                    const VolumeMesh& mesh);

/// Whether @p mesh is out of date for @p controls as well as for the geometry.
///
/// Geometry staleness AND control staleness, because both invalidate a mesh
/// and a caller asking "may I reuse this?" means both. A sizing edit -- the
/// global target changed, a local control added, removed or retargeted --
/// makes the derived mesh stale exactly as a geometry edit does.
///
/// Comparison is by VALUE over the canonical intent, so 10 mm and 0.01 m are
/// the same request and do not invalidate anything, while 10 mm and 5 mm are
/// different and do. That is why the controls are value-semantic.
[[nodiscard]] BETTERCAD_MESHING_EXPORT bool isStale(const Document& document,
                                                    const VolumeMesh& mesh,
                                                    const VolumeMeshControls& controls);

/// Faces of @p mesh's tetrahedra that exactly one tetrahedron uses.
///
/// The boundary of a tetrahedralisation, derived from its own connectivity.
/// Exposed because it is the independent check: a backend's own account of the
/// surface it produced is not evidence about the tetrahedra it returned.
///
/// Each face is returned as three node handles in the winding that faces OUT
/// of its owning tetrahedron.
[[nodiscard]] BETTERCAD_MESHING_EXPORT std::vector<std::array<NodeId, 3>>
tetrahedralBoundary(const Mesh& mesh);

/// Sum of the signed volumes of @p mesh's tetrahedra.
///
/// Signed, with no absolute value anywhere: an inverted element must drag the
/// total down rather than silently contribute as though it were correct.
[[nodiscard]] BETTERCAD_MESHING_EXPORT Volume tetrahedralVolume(const Mesh& mesh);

} // namespace bettercad::meshing
