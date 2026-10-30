#pragma once

// P19-MESH-001 -- the CFD mesh contract, and the gate that proves a P16 mesh meets it.
//
// THE ANSWER TO THE MILESTONE'S CRITICAL QUESTION (ADR-063). P16's Tet4 mesh
// serves DIRECTLY as the finite-volume mesh: a cell is one of its tetrahedra, a
// boundary face is one of its boundary triangles, a patch is one of the CAD faces
// its GeometryMeshMap attributes them to. There is no second mesh, no second
// geometry and no adapter that rebuilds geometry. What P16 does NOT hold -- the
// interior faces, which cell owns which face, the finite-volume metrics -- is
// DERIVED from it, keyed to its stamp, and never persisted (ADR-058).
//
// BUT P16 DOES NOT GUARANTEE EVERYTHING A FINITE-VOLUME METHOD NEEDS, and the
// audit found exactly where (docs/verification/P19-MESH-001/P16_AUDIT.md):
//
//   a face shared by three or more tetrahedra   tetrahedralBoundary drops it silently
//   two cells folded onto one side of a face    nothing checks the orientation of a shared face
//   cells joined only at a vertex or an edge    nothing checks face-connectivity
//   a boundary facet attributed to no CAD face  the map REPORTS it; nothing refuses it
//   an interior face, owner, neighbour          not held anywhere in P16
//
// So a CFD analysis does not take P16's word for it. `requireCfdMesh` derives a
// face census from the mesh's own connectivity and refuses, by name, every way the
// mesh could fail the contract. Possession of a `CfdMeshContract` is the proof --
// the device of `VolumeMesh`, `CfdModel` and every boundary since -- and
// P19-TOPO-001 builds the compact cell/face topology on a mesh that holds one.
//
// WHAT THIS FILE DOES NOT DO. No CellIndex or FaceIndex (P19-TOPO-001), no face
// area vector, centroid or quality metric beyond what a refusal needs
// (P19-GEOM-001), no acceptance threshold for a metric (P19-VALID-001). The
// definitions all of them must follow are fixed in ADR-063.

#include <bettercad/cfd/Export.hpp>
#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/meshing/GeometryPreparation.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshIds.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bettercad::cfd {

class CfdModel;

/// The four faces of a Tet4 as LOCAL vertex triples, each wound so that its
/// right-hand normal points OUT of a positively oriented tetrahedron
/// (`meshing::signedVolume > 0`).
///
/// THE ONE TABLE CFD USES, and the same table P16 winds its boundary triangles
/// with (pinned by a test against `tetrahedralBoundary`). The gate does not
/// compare a triangle with the derived face node for node -- a rotation of the
/// same triangle is the same oriented face -- it checks the ORIENTATION, and
/// checks it geometrically.
inline constexpr std::array<std::array<std::size_t, 3>, 4> kTetFaces{{{0, 2, 1}, {0, 1, 3}, {1, 2, 3}, {0, 3, 2}}};

/// The local vertex OPPOSITE each face of `kTetFaces`: {0,2,1} omits 3, {0,1,3}
/// omits 2, {1,2,3} omits 0, {0,3,2} omits 1. A TABLE, not a formula: the omitted
/// vertices are 3, 2, 0, 1, which no "3 - k" reproduces -- the gate's first draft
/// assumed one and its tests caught it on every P16 mesh.
inline constexpr std::array<std::size_t, 4> kTetFaceOpposite{3, 2, 0, 1};

/// Why a mesh fails the CFD mesh contract. Every value is reachable, and each is
/// a different defect for the user (or the mesher) to fix.
enum class CfdMeshProblem : std::uint8_t {
    /// No cells.
    EmptyMesh,
    /// A cell's signed volume is zero, negative or not finite. A finite-volume
    /// balance divides by the cell volume and integrates over it; an inverted cell
    /// flips every flux it owns.
    NonPositiveCellVolume,
    /// A face is shared by three or more cells. A face has one owner and at most
    /// one neighbour; there is no third side for a flux to go to. (P16's
    /// `tetrahedralBoundary` drops such a face silently.)
    NonManifoldFace,
    /// Two cells that share a face traverse it in the SAME direction: both lie on
    /// the same side of it, folded over one another. Two positive cells on opposite
    /// sides of a face list it in opposite windings, and that is what makes "the
    /// area vector points from owner to neighbour" (and S . d > 0) true. Checked
    /// combinatorially, exactly, on the node order -- no tolerance.
    InconsistentFaceOrientation,
    /// The cells do not form ONE face-connected set: two pieces that touch at a
    /// vertex, along an edge, or not at all. A flux crosses faces only, so each
    /// piece is a separate flow whose pressure is fixed only up to its own
    /// constant, and the analysis has one pressure datum (ADR-058). A vertex or edge
    /// pinch that leaves the cells face-connected is NOT refused: nothing a
    /// finite-volume method computes crosses it (ADR-063).
    DisconnectedCells,
    /// A boundary triangle that is not a face of exactly one cell: no cell has it,
    /// two cells share it (it is interior), or two triangles claim the same face.
    /// A boundary face has exactly one owner and no neighbour.
    InvalidFaceOwnership,
    /// A boundary triangle wound into its owning cell instead of out of it. Every
    /// boundary area vector points out of the fluid (ADR-063).
    InwardBoundaryFace,
    /// A face owned by exactly one cell that no boundary triangle covers: the
    /// domain is open there, and a flux leaving that face would go nowhere a
    /// boundary condition could name.
    UnclosedDomain,
    /// A boundary triangle attributed to no CAD face, to several, or a CAD face
    /// given no triangle (the map's own report is incomplete). A boundary face a
    /// condition cannot reach by a GeometryReference is refused -- never assigned
    /// to the nearest face.
    UnattributedBoundary,
};

/// The nine, in declaration order: the order findings are reported in.
inline constexpr std::array<CfdMeshProblem, 9> kCfdMeshProblems{
    CfdMeshProblem::EmptyMesh,          CfdMeshProblem::NonPositiveCellVolume,
    CfdMeshProblem::NonManifoldFace,    CfdMeshProblem::InconsistentFaceOrientation,
    CfdMeshProblem::DisconnectedCells,  CfdMeshProblem::InvalidFaceOwnership,
    CfdMeshProblem::InwardBoundaryFace, CfdMeshProblem::UnclosedDomain,
    CfdMeshProblem::UnattributedBoundary};

[[nodiscard]] BETTERCAD_CFD_EXPORT std::string_view toString(CfdMeshProblem problem) noexcept;

/// One kind of defect, how many times it occurs, and the first occurrence named
/// by its P16 handle -- a diagnostic, never an identity.
///
/// `count` is the number of occurrences: cells, faces or triangles, one each.
/// Two problems are not per-entity, and count what they can: DisconnectedCells
/// the pieces BEYOND the first (components - 1), and UnattributedBoundary the
/// defective facets and faces the map's report counts (unmapped + attributed
/// to several + faces without a facet), or 1 for a report that counts a
/// different number of facets than the mesh holds.
struct CfdMeshFinding {
    CfdMeshProblem problem = CfdMeshProblem::EmptyMesh;
    std::size_t count = 0;
    std::string message{};

    friend bool operator==(const CfdMeshFinding&, const CfdMeshFinding&) = default;
};

/// What the face census counted.
///
/// ```text
/// cells              tetrahedra
/// interiorFaces      faces shared by exactly two cells
/// boundaryFaces      faces owned by exactly one cell
/// nonManifoldFaces   faces shared by three or more cells
/// boundaryTriangles  P16's boundary Triangle3 elements
/// components         face-connected sets of cells (1 for a valid mesh, 0 for none)
/// ```
///
/// For a valid mesh `4 * cells == 2 * interiorFaces + boundaryFaces` and
/// `boundaryFaces == boundaryTriangles`; the tests assert both, and an
/// independent census written in the tests must agree with this one.
struct CfdMeshCensus {
    std::size_t cells = 0;
    std::size_t interiorFaces = 0;
    std::size_t boundaryFaces = 0;
    std::size_t nonManifoldFaces = 0;
    std::size_t boundaryTriangles = 0;
    std::size_t components = 0;

    friend bool operator==(const CfdMeshCensus&, const CfdMeshCensus&) = default;
};

/// The result of inspecting a mesh against the contract: the census, and every
/// finding in `kCfdMeshProblems` order. Deterministic: the faces are keyed by
/// their sorted node handles in an ordered map, never by an unordered container
/// or an address.
struct CfdMeshInspection {
    CfdMeshCensus census{};
    std::vector<CfdMeshFinding> findings{};

    [[nodiscard]] bool satisfiesContract() const noexcept { return findings.empty(); }
    /// The first finding's problem, in `kCfdMeshProblems` order, or none.
    [[nodiscard]] std::optional<CfdMeshProblem> firstProblem() const noexcept {
        if (findings.empty()) {
            return std::nullopt;
        }
        return findings.front().problem;
    }

    friend bool operator==(const CfdMeshInspection&, const CfdMeshInspection&) = default;
};

/// Inspects @p mesh and the attribution @p mapping reports for it.
///
/// A PLAIN MESH AND A PLAIN REPORT ON PURPOSE. A `VolumeMesh` cannot be built by
/// hand, so a gate that took only one could never be shown an inverted cell, a
/// non-manifold face or an open boundary. This function is the SAME code
/// `requireCfdMesh` runs, exposed so every refusal can be demonstrated on a mesh
/// built to fail. Read-only; checks every cell and face, reports every problem.
[[nodiscard]] BETTERCAD_CFD_EXPORT CfdMeshInspection inspectCfdMesh(const meshing::Mesh& mesh,
                                                                   const meshing::GeometryMeshMappingReport& mapping);

/// The error `requireCfdMesh` returns for a failed @p inspection of the mesh of
/// meshing control @p control: FailedPrecondition, naming the control and EVERY
/// finding's message, in order, joined by "; ". Exposed for the same reason as
/// `inspectCfdMesh`: a CfdModel always holds a P16-validated mesh, so the gate's
/// refusal could not otherwise be shown. Precondition: the inspection failed.
[[nodiscard]] BETTERCAD_CFD_EXPORT Error cfdMeshRefusal(const CfdMeshInspection& inspection,
                                                        MeshControlId control);

/// The proof that one P16 mesh satisfied the CFD mesh contract.
///
/// No public constructor; only `requireCfdMesh` makes one. It is a VALUE: it
/// borrows nothing, and it names the mesh by its stamp and the geometry by its
/// revision, so that whatever is built on it (P19-TOPO-001) can check it is being
/// applied to the same mesh -- a contract for one generation says nothing about
/// the next, however equal their counts.
class BETTERCAD_CFD_EXPORT CfdMeshContract {
public:
    /// The mesh generation this contract was proved for.
    [[nodiscard]] const meshing::MeshStamp& mesh() const noexcept { return mesh_; }
    /// The geometry that mesh was built from.
    [[nodiscard]] const meshing::GeometryRevision& geometry() const noexcept { return geometry_; }
    [[nodiscard]] const CfdMeshCensus& census() const noexcept { return census_; }
    /// Patches: the CAD faces of the fluid body, one patch per face (ADR-063).
    [[nodiscard]] std::size_t patches() const noexcept { return patches_; }
    /// Patches whose CAD face carries no FaceName (a bored hole's wall, say): mapped,
    /// but no GeometryReference can name them, so P19-BC-001 can give them no
    /// condition and will refuse the analysis if one exists. Counted, not refused,
    /// here: it is a fact about naming, not about the mesh.
    [[nodiscard]] std::size_t unnamedPatches() const noexcept { return unnamedPatches_; }

    /// Whether this contract was proved for @p mesh. Stamps, never counts.
    [[nodiscard]] bool describes(const meshing::Mesh& mesh) const noexcept { return mesh.owns(mesh_); }

    friend bool operator==(const CfdMeshContract&, const CfdMeshContract&) = default;

private:
    CfdMeshContract(meshing::MeshStamp mesh, meshing::GeometryRevision geometry, CfdMeshCensus census,
                    std::size_t patches, std::size_t unnamedPatches) noexcept
        : mesh_(mesh), geometry_(std::move(geometry)), census_(census), patches_(patches),
          unnamedPatches_(unnamedPatches) {}

    friend BETTERCAD_CFD_EXPORT Result<CfdMeshContract> requireCfdMesh(const CfdModel& model);

    meshing::MeshStamp mesh_{};
    meshing::GeometryRevision geometry_{};
    CfdMeshCensus census_{};
    std::size_t patches_ = 0;
    std::size_t unnamedPatches_ = 0;
};

/// Proves that the mesh of a validated CFD input satisfies the contract, or fails
/// naming every finding.
///
/// Takes a `CfdModel`, so the mesh is current, eligible and paired with its map by
/// construction (ADR-060: both read from one held entry): this gate adds the
/// finite-volume conditions on top of those, it does not re-ask them. In
/// particular it does not compare the map's mesh stamp with the mesh -- that
/// pairing is the CfdModel's, and the tests assert it holds. It is a staged
/// gate AFTER the input boundary (ADR-053's shape), not a check inside it.
[[nodiscard]] BETTERCAD_CFD_EXPORT Result<CfdMeshContract> requireCfdMesh(const CfdModel& model);

/// The first problem `requireCfdMesh` would report, or none.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::optional<CfdMeshProblem> cfdMeshProblem(const CfdModel& model);

} // namespace bettercad::cfd
