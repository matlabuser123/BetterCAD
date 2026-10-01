#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/meshing/Export.hpp>
#include <bettercad/meshing/GeometryPreparation.hpp>
#include <bettercad/meshing/Mesh.hpp>

#include <cstddef>
#include <string_view>

// The engineering surface mesh (P16-SURF-001).
//
// THE DISTINCTION THIS FILE EXISTS FOR:
//
//     geometry::Mesh            a triangle soup for export and display.
//                               Geometrically watertight, NOT topologically
//                               conforming: a vertex on a shared edge appears
//                               once per face.
//
//     EngineeringSurfaceMesh    identified, shared nodes; oriented triangles;
//                               validated closure. A boundary a solver can use.
//
// The backend machinery is shared -- both come from geometry::triangulate(), and
// duplicating the kernel call would be its own defect. What is NOT shared is
// authority: an engineering surface is generated from geometry that
// P16-GEOM-001 has already cleared, with controls this milestone owns, and is
// refused unless it closes.
//
// WHAT MAKES CONTAMINATION STRUCTURALLY IMPOSSIBLE, rather than merely
// discouraged: geometry::triangulate() meshes a BRepBuilderAPI_Copy of the
// shape with copyMesh=false, so it never reads a triangulation the kernel may
// have cached on the authoritative faces and never writes one there. A display
// path, present or future, cannot hand this one its deflection, and this one
// cannot change what a display path sees. The authoritative shape is not
// touched at all.
namespace bettercad::meshing {

/// What an engineering surface triangulation is asked for.
///
/// Deliberately minimal: P16-SIZE-001 owns sizing controls, and a second
/// canonical place for them would be the competing-state failure. These are the
/// two the kernel needs to produce a testable surface, and they are NOT a
/// display setting -- their defaults are this milestone's, chosen for an
/// analysis boundary rather than for a frame rate.
struct SurfaceMeshControls {
    /// Maximum distance between the triangles and the true surface.
    Length linearDeflection = Length::fromSi(1e-4); // 0.1 mm
    /// Maximum angle between the normals of adjacent triangles on a curved
    /// surface, in addition to the distance limit.
    Angle angularDeflection = Angle::fromSi(0.3490658503988659); // 20 degrees

    friend bool operator==(const SurfaceMeshControls&, const SurfaceMeshControls&) = default;
};

/// What is structurally wrong with a surface, counted rather than described.
///
/// STRUCTURAL ONLY. Aspect ratio, minimum angle and worst-element scoring are
/// P16-QUALITY-001's: a sliver is a valid closed boundary that will solve badly,
/// and mixing the two would let a threshold nobody chose decide whether a
/// surface exists.
struct SurfaceValidation {
    /// Undirected edges used by exactly one triangle. A closed solid's boundary
    /// has none.
    std::size_t boundaryEdgeCount = 0;
    /// Undirected edges used by three or more triangles. A manifold boundary has
    /// none -- and this is why a boundary-edge count of zero is not on its own
    /// evidence of closure.
    std::size_t nonManifoldEdgeCount = 0;
    /// Pairs of triangles that traverse a shared edge the SAME way round. Two
    /// triangles of a coherently oriented surface traverse it in opposite
    /// directions, so this catches a patch welded in with its winding flipped --
    /// which edge counting alone cannot see.
    std::size_t orientationConflictCount = 0;
    /// Triangles of zero area: three distinct nodes can still be collinear.
    std::size_t degenerateTriangleCount = 0;
    /// Triangles on the same three nodes, in either winding. A reversed
    /// duplicate on a closed boundary is the classic doubled-face defect.
    std::size_t duplicateTriangleCount = 0;
    /// Nodes no triangle references.
    std::size_t unusedNodeCount = 0;

    /// A closed, manifold, coherently oriented boundary.
    ///
    /// All three counts, not just the first: a surface can have no boundary edge
    /// and still be non-manifold, and it can be manifold and still have a patch
    /// facing the wrong way.
    [[nodiscard]] bool watertight() const noexcept {
        return boundaryEdgeCount == 0 && nonManifoldEdgeCount == 0 && orientationConflictCount == 0;
    }

    /// Watertight, and free of the defects that would make its triangles unusable.
    [[nodiscard]] bool valid() const noexcept {
        return watertight() && degenerateTriangleCount == 0 && duplicateTriangleCount == 0 &&
               unusedNodeCount == 0;
    }

    friend bool operator==(const SurfaceValidation&, const SurfaceValidation&) = default;
};

/// A validated engineering surface: the boundary of one prepared solid.
///
/// The triangles live in a P16-DATA `Mesh`, not in a representation of this
/// file's own: one triangle model, whose arity, orientation convention,
/// identity rules and immutability are already qualified. This type adds what a
/// surface needs on top -- what it was built from, whether it closes, and the
/// two independent cross-checks.
struct EngineeringSurfaceMesh {
    /// Nodes and Triangle3 elements. Immutable, as every built Mesh is.
    Mesh mesh{};
    /// The structural verdict. `generateSurfaceMesh` refuses rather than
    /// returning a mesh whose `valid()` is false, so this is a record of what
    /// was checked and not a warning to act on.
    SurfaceValidation validation{};
    /// The geometry this was built from (P16-GEOM-001). A later consumer records
    /// it; a surface whose recorded revision differs from the current one is
    /// stale.
    GeometryRevision revision{};
    /// The controls that produced it. Kept so that a caller can see what it got
    /// rather than what it asked for, and so a cached surface can be compared
    /// against a new request.
    SurfaceMeshControls controls{};
    /// Sum of the triangle areas. A DERIVED CHECK, never authority: the CAD
    /// surface area is the kernel's, and for a curved face this is an
    /// approximation that improves with the deflection.
    Area area{};
    /// Volume enclosed by the oriented triangles, 1/6 sum of
    /// dot(a, cross(b, c)). Positive for an outward-oriented closed surface, so
    /// its SIGN detects a globally inward boundary that edge counting would call
    /// perfect. Also a derived check: CAD owns the volume.
    Volume enclosedVolume{};
    /// How many CAD faces contributed.
    std::size_t faceCount = 0;

    friend bool operator==(const EngineeringSurfaceMesh&, const EngineeringSurfaceMesh&) = default;
};

/// Why a surface could not be produced.
enum class SurfaceMeshFailure : std::uint8_t {
    /// The controls are not positive and finite.
    InvalidControls,
    /// The kernel could not triangulate the shape.
    TriangulationFailed,
    /// A valid non-empty solid produced no nodes or no triangles. A successful
    /// empty surface is not a thing.
    EmptySurface,
    /// The surface does not close, is non-manifold, has a patch facing the wrong
    /// way, or carries degenerate or duplicate triangles.
    NotAValidBoundary,
    /// The closed surface is coherent but globally inward: the enclosed volume
    /// came out negative.
    InwardOrientation,
};

[[nodiscard]] BETTERCAD_MESHING_EXPORT std::string_view toString(SurfaceMeshFailure failure) noexcept;

/// The engineering surface of already-prepared geometry.
///
/// Takes a `MeshableGeometry`, which is the point: the only way to obtain one is
/// `requireMeshableGeometry`, so this entry point cannot be handed a stale body,
/// a failed regeneration, a shape with no solid or geometry under a
/// configuration override. P16-SURF performs no body-validity checks of its own
/// and has no second opinion about what is meshable.
///
/// Fails rather than returning a surface that does not close. Nothing here
/// repairs geometry, caps an opening, or drops a triangle to make the counts
/// work.
[[nodiscard]] BETTERCAD_MESHING_EXPORT Result<EngineeringSurfaceMesh>
generateSurfaceMesh(const MeshableGeometry& geometry, const SurfaceMeshControls& controls = {});

/// Prepares @p feature's geometry and then surfaces it.
///
/// The ordinary entry point, so that a caller cannot forget the preparation
/// step. Every refusal `requireMeshableGeometry` can produce is returned
/// unchanged, which is what makes stale geometry unsurfaceable rather than
/// merely discouraged.
[[nodiscard]] BETTERCAD_MESHING_EXPORT Result<EngineeringSurfaceMesh>
surfaceMeshFor(const Document& document, const features::Regenerator& regenerator, ObjectId feature,
               const SurfaceMeshControls& controls = {});

/// Checks @p mesh's triangles as a boundary: edge incidence, orientation
/// coherence, degeneracy, duplication and unused nodes.
///
/// Exposed because a consumer may want to re-check a mesh it was given, and
/// because the counts are more useful than a boolean.
[[nodiscard]] BETTERCAD_MESHING_EXPORT SurfaceValidation validateSurface(const Mesh& mesh);

/// Sum of the areas of @p mesh's triangles. Derived, never authority.
[[nodiscard]] BETTERCAD_MESHING_EXPORT Area surfaceArea(const Mesh& mesh);

/// Volume enclosed by @p mesh's oriented triangles: 1/6 sum of
/// dot(a, cross(b, c)).
///
/// Positive for an outward-oriented closed surface. Translation-invariant for a
/// surface that genuinely closes, which is what makes it a test of closure and
/// orientation together rather than of either alone.
[[nodiscard]] BETTERCAD_MESHING_EXPORT Volume enclosedVolume(const Mesh& mesh);

} // namespace bettercad::meshing
