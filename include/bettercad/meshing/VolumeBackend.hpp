#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/meshing/Export.hpp>
#include <bettercad/meshing/MeshSizing.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// The volume-meshing backend's presence and liveness (INFRA-NETGEN-001).
//
// WHAT THIS IS, AND WHAT IT IS DELIBERATELY NOT:
//
// This is a *toolchain probe*. It answers two questions and no others:
// whether BetterCAD was built against a volume-meshing backend, and whether
// that backend actually links, loads and runs in this process. It converts no
// geometry, meshes nothing, and holds no state.
//
// Volume meshing itself -- feeding P16-SURF-001's validated engineering
// surface to the backend and validating the tetrahedra that come back -- is
// P16-VOL-001's and does not exist yet. Nothing here should grow into it: when
// that milestone is implemented, the adapter is a separate interface and this
// probe stays what it is.
//
// WHY IT IS PERMANENT RATHER THAN A ONE-OFF SCRIPT:
//
// The backend is a third-party native library resolved at configure time,
// loaded from a DLL at run time, and built by a different project with a
// different build system. Every one of those steps can break without a
// BetterCAD source change -- a stale deps prefix, a half-installed
// dependency, a DLL that links but cannot load because its own dependency is
// missing. A probe compiled and run by the ordinary test suite turns all of
// that from a confusing downstream failure into one named test.
namespace bettercad::meshing {

/// Which volume-meshing backend this build was configured with.
struct VolumeBackendInfo
{
    /// True when a backend was found at configure time and linked in.
    bool available = false;
    /// Backend name, or "none" when `available` is false.
    std::string_view name = "none";
    /// Backend version as the backend itself reports it, or "" when absent.
    /// Read from the backend, never hardcoded: a dependency that misreports
    /// its own version is a defect this is meant to expose, not hide.
    std::string_view version = "";
};

/// Reports the configured backend. Does not load or call it.
[[nodiscard]] BETTERCAD_MESHING_EXPORT VolumeBackendInfo volumeBackend() noexcept;

/// Initialises the backend, then shuts it down again.
///
/// Returns false when no backend is configured. When one is, this proves the
/// library links, its DLL and the whole of its runtime closure load, and its
/// entry points can be called -- which a compile-and-link check alone does
/// not. It meshes nothing.
[[nodiscard]] BETTERCAD_MESHING_EXPORT bool volumeBackendResponds() noexcept;

// ---------------------------------------------------------------------------
// The tetrahedralisation seam (P16-VOL-001, ADR-033)
// ---------------------------------------------------------------------------
//
// ADR-033: "No backend type appears in any BetterCAD API. Not in a public
// header, not in a MeshControl, not in a Mesh, not in a diagnostic, not in a
// persisted file." Everything below is BetterCAD and standard-library types
// only, so a second backend is an added translation unit rather than a change
// to anything above this line.
//
// WHY INDICES AND NOT NodeId. A backend knows nothing of mesh identity and
// must not: handles belong to one generation of one mesh (ADR-031), they are
// assigned by MeshBuilder, and letting a third-party library choose them would
// hand it an invariant it has no way to keep. So the seam speaks in dense
// 0-based indices -- which is exactly what a mesher library wants anyway -- and
// the MESHER turns them into handles on the way out. A backend therefore
// cannot produce a handle, valid or otherwise.

/// A closed, oriented, manifold boundary, as a backend needs it.
///
/// This is NOT a Mesh and NOT a viewer tessellation. The mesher builds it from
/// a `EngineeringSurfaceMesh`, which cannot exist unless it closed
/// (P16-SURF-001 refuses otherwise), so the backend receives a boundary that
/// has already been proven watertight, manifold and coherently oriented.
struct VolumeBackendRequest {
    /// Boundary node positions. Index order is the request's own.
    std::span<const Point3D> points;
    /// Oriented triangles as 0-based indices into `points`.
    std::span<const std::array<std::uint32_t, 3>> triangles;
    /// Upper bound on element size everywhere.
    ///
    /// Required rather than optional since P16-SIZE-001: the mesher always
    /// resolves a global bound, from the request or from BetterCAD's own
    /// default, so there is no path where the backend's default decides it.
    /// Nullopt is still accepted and still means "the backend's choice", but
    /// `generateVolumeMesh` never sends it.
    std::optional<Length> maxElementSize{};
    /// Per-point maximum sizes, from P16-SIZE-001's resolved local controls.
    ///
    /// Point3D and Length only, by ADR-033. Each entry restricts the element
    /// size around one point; a backend expressing local sizing another way
    /// would translate the same list differently without this header changing.
    std::span<const SizeRestriction> localSizes{};
    /// Per-region maximum sizes. Translated to the backend's own region
    /// mechanism; see BoxSizeRestriction for why a point is not enough.
    std::span<const BoxSizeRestriction> localRegions{};
};

/// Tetrahedra filling a boundary, in the same index space as the points.
struct VolumeBackendMesh {
    /// Every node of the volume mesh, boundary and interior.
    std::vector<Point3D> points;
    /// Tet4 connectivity as 0-based indices into `points`.
    std::vector<std::array<std::uint32_t, 4>> tetrahedra;
};

/// Why a backend produced no usable tetrahedralisation.
enum class VolumeBackendFailure : std::uint8_t {
    /// BetterCAD was built without a volume-meshing backend.
    NotAvailable,
    /// The request has no points or no triangles.
    EmptyBoundary,
    /// A request index names no point, or a triangle repeats one.
    MalformedRequest,
    /// A requested element size is not positive and finite.
    InvalidElementSize,
    /// The backend rejected the surface it was given.
    SurfaceRejected,
    /// The backend reported a failure generating the volume.
    GenerationFailed,
    /// The backend reported SUCCESS and produced no tetrahedra.
    ///
    /// A DISTINCT failure, not folded into GenerationFailed, because this is
    /// the documented behaviour of the admitted backend rather than a
    /// hypothetical: nglib returns NG_OK with zero elements when it cannot
    /// mesh a surface, having printed its own complaint to stdout. See
    /// docs/verification/INFRA-NETGEN-001/. A backend's return code is never
    /// sufficient evidence of success.
    NoTetrahedra,
    /// The backend returned an element referencing a node it did not return,
    /// or a node count inconsistent with its own report.
    InconsistentOutput,
};

[[nodiscard]] BETTERCAD_MESHING_EXPORT std::string_view toString(VolumeBackendFailure failure) noexcept;

/// Fills @p request's boundary with tetrahedra.
///
/// Checks the request before calling the backend, and checks the backend's
/// output before returning it: a non-empty node list, a non-empty element
/// list, and every index in range. It does NOT validate geometry -- positive
/// volumes, degeneracy, duplication and conformity are the mesher's, over a
/// real `Mesh`, so that one definition of "valid" serves every producer.
[[nodiscard]] BETTERCAD_MESHING_EXPORT Result<VolumeBackendMesh>
generateTetrahedra(const VolumeBackendRequest& request);

}  // namespace bettercad::meshing
