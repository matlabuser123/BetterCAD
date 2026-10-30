#pragma once

// P19-TOPO-001 -- the finite-volume topology: compact cell and face connectivity
// derived from a P16 mesh that has been proved to satisfy the CFD mesh contract.
//
// WHAT THIS ADDS TO ADR-063. The mesh contract proved that a P16 Tet4 mesh CAN
// serve as the finite-volume mesh, and counted its faces. It did not name them.
// Nothing in P16 or in `CfdMeshContract` says "face 7", says which cells sit on
// either side of it, or gives a cell a subscript a field can be indexed by. This
// file does, and it is the last derived structure before the finite-volume
// geometry (P19-GEOM-001) has something to hang a metric on.
//
// WHY A SECOND ADDRESSING SPACE EXISTS AT ALL (ADR-064). A finite-volume method
// assembles arrays: one pressure per cell, one mass flux per face, one matrix
// coefficient per (owner, neighbour) pair. Those want DENSE SUBSCRIPTS. A mesh
// handle is not one: `MeshIds.hpp` is explicit that an ElementId is "allocated by
// the mesh, reused every generation", and nothing in the mesh module promises
// density. Triangles and tetrahedra share ONE handle space, so a mesh built with
// a boundary triangle between two cells already has non-consecutive cell handles,
// and a field indexed by `ElementId::value()` is wrong the moment the allocator
// changes. So the topology assigns its own compact indices and keeps the
// translation, and NO container here is sized by a handle's value -- every one is
// sized by a count.
//
// WHAT IS INHERITED AND NOT RE-DECIDED. The face table (`kTetFaces`), the
// opposite-vertex table, the face key (a sorted node triple), the owner rule
// (the first claimant in ascending ElementId order) and the winding rule (every
// face wound outward of its owner) are ADR-063's, used unchanged. If this file
// chose a different owner, every face area vector P19-GEOM-001 derives would
// point the other way and every flux across those faces would change sign.
//
// WHAT THIS FILE DOES NOT DO. No face area vector, centroid, volume or quality
// metric (P19-GEOM-001); no field storage (P19-FIELD-001); no boundary condition
// (P19-BC-001); no matrix and no solve. It is connectivity and nothing else.

#include <bettercad/cfd/CfdMesh.hpp>
#include <bettercad/cfd/Export.hpp>
#include <bettercad/core/Error.hpp>
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshIds.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bettercad::cfd {

class CfdModel;

// ---------------------------------------------------------------------------
// The four index types
// ---------------------------------------------------------------------------
//
// FOUR CONCRETE TYPES, WRITTEN OUT, and deliberately neither `bettercad::Id<Tag>`
// nor instances of one new generic template (ADR-064, following ADR-031's
// reasoning in MeshIds.hpp).
//
// An `Id<Tag>` promises three things a subscript cannot give: stability across a
// rebuild, persistence in a document, and IdAllocator's never-reuse guarantee. A
// topology index is dense, is recomputed wholesale by every remesh, is never
// written to a file, and is meaningful only against the MeshStamp it was derived
// from. And no generic `Index<Tag>` is introduced, for the reason MeshIds.hpp
// gives for writing its three handles out: so each type's semantics are stated
// where it is declared, and none can be instantiated for a fifth purpose by
// accident.
//
// ZERO IS A VALID INDEX, which inverts the convention of every mesh handle and
// document ID in BetterCAD, where zero is the invalid value and IDs start at 1.
// That inversion is exactly why these are strong types and not integers: a
// subscript whose first legal value is 0 must never be assignable to a handle
// whose first legal value is 1, in either direction. A default-constructed index
// is explicitly INVALID (`kInvalid`), not cell 0.

/// One cell of one topology: a tetrahedron's position in the mesh's own ascending
/// ElementId order, in `[0, cells)`.
///
/// NOT an ElementId and not convertible to or from one. `cellOf` and
/// `cellIndexOf` translate, and they are the only way across.
class CellIndex {
public:
    using ValueType = std::size_t;
    static constexpr ValueType kInvalid = static_cast<ValueType>(-1);

    /// The invalid index. NOT cell 0.
    constexpr CellIndex() noexcept = default;

    [[nodiscard]] static constexpr CellIndex fromValue(ValueType value) noexcept {
        CellIndex index;
        index.value_ = value;
        return index;
    }

    [[nodiscard]] constexpr ValueType value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool isValid() const noexcept { return value_ != kInvalid; }
    constexpr explicit operator bool() const noexcept { return isValid(); }

    friend constexpr bool operator==(const CellIndex&, const CellIndex&) = default;
    friend constexpr auto operator<=>(const CellIndex&, const CellIndex&) = default;

private:
    ValueType value_{kInvalid};
};

/// One face of one topology, in `[0, faces)`: internal faces first, then boundary
/// faces (ADR-064's order). `CfdTopology::isInternal` is a comparison against
/// `internalFaces`, never a stored flag.
class FaceIndex {
public:
    using ValueType = std::size_t;
    static constexpr ValueType kInvalid = static_cast<ValueType>(-1);

    constexpr FaceIndex() noexcept = default;

    [[nodiscard]] static constexpr FaceIndex fromValue(ValueType value) noexcept {
        FaceIndex index;
        index.value_ = value;
        return index;
    }

    [[nodiscard]] constexpr ValueType value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool isValid() const noexcept { return value_ != kInvalid; }
    constexpr explicit operator bool() const noexcept { return isValid(); }

    friend constexpr bool operator==(const FaceIndex&, const FaceIndex&) = default;
    friend constexpr auto operator<=>(const FaceIndex&, const FaceIndex&) = default;

private:
    ValueType value_{kInvalid};
};

/// One boundary face, in `[0, boundaryFaces)`: a face's position WITHIN the
/// boundary range, grouped by patch.
///
/// A SECOND INDEX AND NOT A FaceIndex, because the two count different things and
/// a boundary condition addresses this one. `faceOf` and `boundaryFaceOf`
/// translate; the arithmetic (`face - internalFaces`) is deliberately not the
/// caller's to perform.
class BoundaryFaceIndex {
public:
    using ValueType = std::size_t;
    static constexpr ValueType kInvalid = static_cast<ValueType>(-1);

    constexpr BoundaryFaceIndex() noexcept = default;

    [[nodiscard]] static constexpr BoundaryFaceIndex fromValue(ValueType value) noexcept {
        BoundaryFaceIndex index;
        index.value_ = value;
        return index;
    }

    [[nodiscard]] constexpr ValueType value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool isValid() const noexcept { return value_ != kInvalid; }
    constexpr explicit operator bool() const noexcept { return isValid(); }

    friend constexpr bool operator==(const BoundaryFaceIndex&, const BoundaryFaceIndex&) = default;
    friend constexpr auto operator<=>(const BoundaryFaceIndex&, const BoundaryFaceIndex&) = default;

private:
    ValueType value_{kInvalid};
};

/// One patch, in `[0, patches)`: a CAD face's position in the mapping's
/// `faces()` order (ADR-063: "a patch is one CAD face of the GeometryMeshMap").
///
/// A CORRELATION HANDLE FOR ONE TOPOLOGY, never an identity, exactly as
/// `MappedFace::index` is for one map: it is not persisted and not comparable
/// between two topologies. A patch's canonical reference is the FaceName its
/// MappedFace carries, which is what a GeometryReference resolves (ADR-032).
class PatchIndex {
public:
    using ValueType = std::size_t;
    static constexpr ValueType kInvalid = static_cast<ValueType>(-1);

    constexpr PatchIndex() noexcept = default;

    [[nodiscard]] static constexpr PatchIndex fromValue(ValueType value) noexcept {
        PatchIndex index;
        index.value_ = value;
        return index;
    }

    [[nodiscard]] constexpr ValueType value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool isValid() const noexcept { return value_ != kInvalid; }
    constexpr explicit operator bool() const noexcept { return isValid(); }

    friend constexpr bool operator==(const PatchIndex&, const PatchIndex&) = default;
    friend constexpr auto operator<=>(const PatchIndex&, const PatchIndex&) = default;

private:
    ValueType value_{kInvalid};
};

// ---------------------------------------------------------------------------
// The refusals
// ---------------------------------------------------------------------------

/// Why a mesh and its patches do not yield a finite-volume topology.
///
/// THE TOPOLOGY JOINS TWO SOURCES THAT MUST AGREE: the faces of the cells, and
/// the mesh's boundary triangles, which are what carry patch attribution. These
/// are the ways that join can fail.
///
/// EVERY ONE IS A SUBSET OF WHAT ADR-063's GATE ALREADY REFUSES, so on a mesh
/// that holds a `CfdMeshContract` none of them can fire. They are kept, and each
/// is tested on a mesh built to fail it, for the reason ADR-063 kept
/// `inspectCfdMesh` public: a builder that trusts its precondition silently is a
/// builder whose precondition cannot be shown to matter.
enum class CfdTopologyProblem : std::uint8_t {
    /// A cell's signed volume is zero, negative or not finite.
    ///
    /// REFUSED HERE BECAUSE THE WINDING GUARANTEE DEPENDS ON IT, not only because
    /// a finite-volume balance divides by the volume. `kTetFaces` is wound outward
    /// only of a POSITIVELY oriented tetrahedron (ADR-063), so for an inverted
    /// cell every face this topology stores would point inward while `faceNodes`
    /// promised outward -- and P19-GEOM-001 is told by this file to build its area
    /// vectors on that promise. An adversarial review found the promise being made
    /// unconditionally with nothing checked; it is now enforced.
    NonPositiveCellVolume,
    /// A face is shared by three or more cells, so the owner has no single
    /// neighbour. There is no third side for a flux to cross to.
    NonManifoldFace,
    /// Two cells that share a face traverse it in the SAME direction: both lie on
    /// the same side of it, folded over one another.
    ///
    /// THE OTHER HALF OF THE WINDING GUARANTEE. Two positively oriented cells on
    /// opposite sides of a face list it in opposite windings, and that is what
    /// makes "the normal points from owner to neighbour" true. If they agree, the
    /// owner's winding does not point at the neighbour however positive both
    /// volumes are. Checked combinatorially, exactly, on the node order -- no
    /// tolerance. ADR-063 refuses the same configuration by name.
    InconsistentFaceOrientation,
    /// A face has exactly one cell and no boundary triangle: nothing on its other
    /// side, and no patch through which a boundary condition could reach it. This
    /// is the checklist's "missing neighbour" and its "internal face with one
    /// owner" -- one condition seen from two sides, refused once (ADR-064).
    MissingNeighbour,
    /// A boundary triangle that is a face of no cell: a patch face with no owner
    /// to balance.
    UnownedBoundaryFace,
    /// A boundary triangle lying on a face two or more cells share: the face is
    /// interior, so it cannot also be a boundary face with one owner.
    BoundaryFaceWithTwoOwners,
    /// Two boundary triangles claiming the same face, which would put two entries
    /// in the face table for one face.
    ///
    /// This is the checklist's "duplicate internal faces", satisfied more narrowly
    /// than the wording: the INTERNAL table cannot hold a duplicate, because faces
    /// are keyed by their sorted node triple in an ordered map and a second
    /// arrival is the same entry. The reachable duplicate is a boundary one.
    DuplicateBoundaryFace,
    /// A boundary triangle in no patch, or in more than one. Not on the
    /// checklist and refused anyway: `patchOf` must be total, and giving an
    /// unattributed facet to the nearest CAD face is the silent rebind ADR-032
    /// forbids and ADR-063 refuses by name.
    UnpatchedBoundaryFace,
};

/// The eight, in declaration order: the order findings are reported in.
inline constexpr std::array<CfdTopologyProblem, 8> kCfdTopologyProblems{
    CfdTopologyProblem::NonPositiveCellVolume,     CfdTopologyProblem::NonManifoldFace,
    CfdTopologyProblem::InconsistentFaceOrientation, CfdTopologyProblem::MissingNeighbour,
    CfdTopologyProblem::UnownedBoundaryFace,       CfdTopologyProblem::BoundaryFaceWithTwoOwners,
    CfdTopologyProblem::DuplicateBoundaryFace,     CfdTopologyProblem::UnpatchedBoundaryFace};

[[nodiscard]] BETTERCAD_CFD_EXPORT std::string_view toString(CfdTopologyProblem problem) noexcept;

/// One kind of defect, how many times it occurs, and the first occurrence named
/// by its P16 handle -- a diagnostic, never an identity.
struct CfdTopologyFinding {
    CfdTopologyProblem problem = CfdTopologyProblem::NonManifoldFace;
    std::size_t count = 0;
    std::string message{};

    friend bool operator==(const CfdTopologyFinding&, const CfdTopologyFinding&) = default;
};

/// What the topology counted.
///
/// ```text
/// cells              tetrahedra
/// faces              internalFaces + boundaryFaces
/// internalFaces      faces shared by exactly two cells
/// boundaryFaces      faces with one cell, covered by one boundary triangle
/// patches            CAD faces of the mapping
/// boundaryTriangles  P16's boundary Triangle3 elements
/// ```
///
/// For a mesh that yields a topology, `4 * cells == 2 * internalFaces +
/// boundaryFaces` and `boundaryFaces == boundaryTriangles`. The tests assert both
/// against an independently written face incidence.
struct CfdTopologyCensus {
    std::size_t cells = 0;
    std::size_t faces = 0;
    std::size_t internalFaces = 0;
    std::size_t boundaryFaces = 0;
    std::size_t patches = 0;
    std::size_t boundaryTriangles = 0;

    friend bool operator==(const CfdTopologyCensus&, const CfdTopologyCensus&) = default;
};

/// The result of inspecting a mesh and its patches against the topology's
/// requirements: the census, and every finding in `kCfdTopologyProblems` order.
///
/// Deterministic: faces are keyed by their sorted node handles in an ORDERED map,
/// cells are visited in the mesh's own ascending ElementId order, and nothing
/// depends on an unordered container, an address or a hash.
struct CfdTopologyInspection {
    CfdTopologyCensus census{};
    std::vector<CfdTopologyFinding> findings{};

    [[nodiscard]] bool buildable() const noexcept { return findings.empty(); }
    /// The first finding's problem, in `kCfdTopologyProblems` order, or none.
    [[nodiscard]] std::optional<CfdTopologyProblem> firstProblem() const noexcept {
        if (findings.empty()) {
            return std::nullopt;
        }
        return findings.front().problem;
    }

    friend bool operator==(const CfdTopologyInspection&, const CfdTopologyInspection&) = default;
};

// ---------------------------------------------------------------------------
// The topology
// ---------------------------------------------------------------------------

/// The compact cell and face connectivity of one generation of one mesh.
///
/// A VALUE THAT BORROWS NOTHING, like `CfdMeshContract`: it copies the handles and
/// node triples it needs, so it cannot dangle behind a mesh that has been
/// remeshed. It is DERIVED STATE and is never persisted (ADR-058); it is keyed to
/// the MeshStamp it came from, and `describes` is how a holder checks it is being
/// applied to the same mesh generation.
///
/// FACE ORDER (ADR-064):
///
/// ```text
/// [0, internalFaces)        internal, ascending by (owner, neighbour)
/// [internalFaces, faces)    boundary, ascending by (patch, owner, face key)
/// ```
///
/// The internal range can be swept contiguously to assemble a matrix, and the
/// boundary range is already grouped by patch, so `patchFaces` is a span rather
/// than a filter over every face.
class BETTERCAD_CFD_EXPORT CfdTopology {
public:
    [[nodiscard]] const CfdTopologyCensus& census() const noexcept { return census_; }
    [[nodiscard]] std::size_t cells() const noexcept { return census_.cells; }
    [[nodiscard]] std::size_t faces() const noexcept { return census_.faces; }
    [[nodiscard]] std::size_t internalFaces() const noexcept { return census_.internalFaces; }
    [[nodiscard]] std::size_t boundaryFaces() const noexcept { return census_.boundaryFaces; }
    [[nodiscard]] std::size_t patches() const noexcept { return census_.patches; }

    /// Whether @p face is shared by two cells. A COMPARISON, not a stored flag:
    /// the face order makes it one.
    [[nodiscard]] bool isInternal(FaceIndex face) const noexcept {
        return face.isValid() && face.value() < census_.internalFaces;
    }
    /// Whether @p face has one cell and lies on a patch.
    [[nodiscard]] bool isBoundary(FaceIndex face) const noexcept {
        return face.isValid() && face.value() >= census_.internalFaces && face.value() < census_.faces;
    }

    /// The cell that owns @p face: the one with the LOWER CellIndex, which is
    /// ADR-063's first claimant in ascending ElementId order. Invalid for a face
    /// this topology does not hold.
    [[nodiscard]] CellIndex owner(FaceIndex face) const noexcept;

    /// The cell on the other side of @p face, or none for a boundary face.
    ///
    /// An OPTIONAL and not a sentinel, because a sentinel is the kind of thing a
    /// caller forgets to check. `owner(f) < *neighbour(f)` holds on every internal
    /// face, and a test asserts it on every reference mesh.
    [[nodiscard]] std::optional<CellIndex> neighbour(FaceIndex face) const noexcept;

    /// @p face's three nodes, WOUND OUTWARD OF ITS OWNER (ADR-063): the right-hand
    /// normal points owner -> neighbour for an internal face, and out of the fluid
    /// for a boundary face. That winding IS the face orientation; there is no
    /// separate orientation field and no sign array.
    ///
    /// UNCONDITIONAL, AND ENFORCED RATHER THAN ASSUMED. The guarantee rests on two
    /// properties of the mesh -- every cell positively oriented, and no two cells
    /// folded onto one side of a shared face -- and a topology cannot be built
    /// without both: they are `NonPositiveCellVolume` and
    /// `InconsistentFaceOrientation`. Possession of a `CfdTopology` is therefore
    /// proof that `faceNodes` means what it says, which is what P19-GEOM-001's
    /// area vectors and ADR-063's `S . d > 0` rest on.
    [[nodiscard]] std::array<meshing::NodeId, 3> faceNodes(FaceIndex face) const noexcept;

    /// The four faces of @p cell, ascending by FaceIndex. Empty for a cell this
    /// topology does not hold.
    ///
    /// Whether @p cell is the owner or the neighbour of one of them is asked with
    /// `owner`, not stored twice: a second copy is a second thing to keep true.
    [[nodiscard]] std::span<const FaceIndex> cellFaces(CellIndex cell) const noexcept;

    /// @p face's position within the boundary range, or none if it is internal.
    [[nodiscard]] std::optional<BoundaryFaceIndex> boundaryFaceOf(FaceIndex face) const noexcept;
    /// The face @p boundaryFace is.
    [[nodiscard]] FaceIndex faceOf(BoundaryFaceIndex boundaryFace) const noexcept;

    /// The patch @p boundaryFace belongs to. Total over every boundary face: a
    /// facet in no patch is refused at build time (`UnpatchedBoundaryFace`).
    [[nodiscard]] PatchIndex patchOf(BoundaryFaceIndex boundaryFace) const noexcept;
    /// Every boundary face of @p patch, ascending. A CONTIGUOUS SPAN, because the
    /// boundary range is ordered by patch.
    [[nodiscard]] std::span<const BoundaryFaceIndex> patchFaces(PatchIndex patch) const noexcept;
    /// The CAD face @p patch is: the `MappedFace::index` the mapping gave it,
    /// which is that face's position in `geometry::listFaces(body)` order and is
    /// NOT in general equal to the PatchIndex.
    ///
    /// An optional, for the reason `neighbour` is one: a sentinel is the kind of
    /// thing a caller forgets to check. (This returned `PatchIndex::kInvalid`
    /// first, which was inconsistent with that stated preference in this very
    /// file; the review that found it is recorded in the evidence.)
    [[nodiscard]] std::optional<std::size_t> patchFace(PatchIndex patch) const noexcept;

    /// The mesh element @p cell is. The dense direction.
    [[nodiscard]] meshing::ElementId cellOf(CellIndex cell) const noexcept;
    /// The cell @p element is, or none.
    ///
    /// SPARSE-SAFE: a sorted vector searched by binary search, never a vector
    /// indexed by the handle's value (ADR-064, Decision 5). A mesh may legally
    /// hand out element 900000 for its fourth tetrahedron.
    [[nodiscard]] std::optional<CellIndex> cellIndexOf(meshing::ElementId element) const noexcept;

    /// The boundary triangle @p boundaryFace lies on.
    [[nodiscard]] meshing::ElementId triangleOf(BoundaryFaceIndex boundaryFace) const noexcept;
    /// The boundary face triangle @p element is, or none. Sparse-safe, as above.
    [[nodiscard]] std::optional<BoundaryFaceIndex> boundaryFaceOfTriangle(meshing::ElementId element) const noexcept;

    /// The mesh generation this topology was derived from.
    [[nodiscard]] const meshing::MeshStamp& mesh() const noexcept { return mesh_; }
    /// The geometry that mesh was built from.
    [[nodiscard]] const meshing::GeometryRevision& geometry() const noexcept { return geometry_; }
    /// Whether this topology was derived from @p mesh. STAMPS, NEVER COUNTS: two
    /// generations can agree on every count and share no node.
    [[nodiscard]] bool describes(const meshing::Mesh& mesh) const noexcept { return mesh.owns(mesh_); }

    friend bool operator==(const CfdTopology&, const CfdTopology&) = default;

private:
    CfdTopology() = default;

    /// The assembler, defined in CfdTopology.cpp. It exists because the walk over
    /// the mesh produces a type that cannot appear in this header, and because a
    /// private constructor is what makes possession of a CfdTopology proof that
    /// the eight refusals were checked -- so the assembly must be a named friend
    /// rather than a public constructor.
    friend struct CfdTopologyAssembler;
    friend BETTERCAD_CFD_EXPORT Result<CfdTopology> requireCfdTopology(const CfdModel& model,
                                                                       const CfdMeshContract& contract);

    CfdTopologyCensus census_{};
    meshing::MeshStamp mesh_{};
    meshing::GeometryRevision geometry_{};

    /// Per face, in face order. `neighbour_` holds `CellIndex::kInvalid` for a
    /// boundary face; `neighbour()` turns that into an empty optional at the
    /// boundary rather than letting a sentinel escape.
    std::vector<CellIndex> owner_{};
    std::vector<CellIndex> neighbour_{};
    std::vector<std::array<meshing::NodeId, 3>> faceNodes_{};

    /// 4 * cells, in cell-major order: the faces of cell c are
    /// `[4c, 4c + 4)`. Sized by a COUNT, never by a handle.
    std::vector<FaceIndex> cellFaces_{};

    /// Per cell, in cell order.
    std::vector<meshing::ElementId> cellOf_{};
    /// (ElementId, CellIndex) sorted by ElementId. Already sorted when built,
    /// because CellIndex is assigned in ascending ElementId order; a test asserts
    /// it is sorted rather than trusting that.
    std::vector<std::pair<meshing::ElementId, CellIndex>> cellByElement_{};

    /// Per boundary face, in boundary-face order.
    std::vector<PatchIndex> patchOf_{};
    std::vector<meshing::ElementId> triangleOf_{};
    std::vector<std::pair<meshing::ElementId, BoundaryFaceIndex>> boundaryByTriangle_{};

    /// CSR over the boundary range, which is ordered by patch: patch p owns
    /// `patchFaceList_[patchStart_[p] .. patchStart_[p + 1])`. `patchStart_` has
    /// `patches + 1` entries.
    std::vector<BoundaryFaceIndex> patchFaceList_{};
    std::vector<std::size_t> patchStart_{};
    /// Per patch: the CAD face's position in the mapping's `faces()` order.
    std::vector<std::size_t> patchFace_{};
};

// ---------------------------------------------------------------------------
// Building one
// ---------------------------------------------------------------------------

/// Inspects @p mesh and the @p patches attributed to its boundary triangles.
///
/// A PLAIN MESH AND A PLAIN PATCH LIST ON PURPOSE, for ADR-063's reason: a
/// `VolumeMesh` and a `GeometryMeshMap` cannot be built by hand, so a builder that
/// took only those could never be shown a non-manifold face, an open boundary or
/// an unattributed facet. `std::span<const meshing::MappedFace>` is the plain
/// analogue of `map.faces()`, and `MappedFace` is a plain struct. Read-only.
[[nodiscard]] BETTERCAD_CFD_EXPORT CfdTopologyInspection
inspectCfdTopology(const meshing::Mesh& mesh, std::span<const meshing::MappedFace> patches);

/// Builds the topology of @p mesh with @p patches, or fails naming every finding.
///
/// Takes a plain mesh for the same reason `inspectCfdTopology` does. The staged
/// gate that adds the mesh-contract precondition and the stamp binding is
/// `requireCfdTopology`.
[[nodiscard]] BETTERCAD_CFD_EXPORT Result<CfdTopology> buildCfdTopology(const meshing::Mesh& mesh,
                                                                        std::span<const meshing::MappedFace> patches);

/// The error `buildCfdTopology` returns for a failed @p inspection: FailedPrecondition
/// naming EVERY finding's message, in order, joined by "; ". Exposed for the same
/// reason as the inspection. Precondition: the inspection failed.
[[nodiscard]] BETTERCAD_CFD_EXPORT Error cfdTopologyRefusal(const CfdTopologyInspection& inspection,
                                                            MeshControlId control);

/// Derives the topology of a validated CFD input whose mesh has been PROVED to
/// satisfy the CFD mesh contract.
///
/// THE STAGED GATE (ADR-053's shape, as ADR-063 used it). It requires the
/// @p contract to have been proved for the generation @p model now holds, and
/// refuses otherwise: that check is the whole reason `CfdMeshContract` carries a
/// stamp, and a contract for one generation says nothing about the next however
/// equal their counts. On a mesh that holds a contract none of the six topology
/// refusals can fire, which is deliberate (ADR-064, Decision 7).
[[nodiscard]] BETTERCAD_CFD_EXPORT Result<CfdTopology> requireCfdTopology(const CfdModel& model,
                                                                          const CfdMeshContract& contract);

/// The first problem `buildCfdTopology` would report for @p mesh and @p patches,
/// or none.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::optional<CfdTopologyProblem>
cfdTopologyProblem(const meshing::Mesh& mesh, std::span<const meshing::MappedFace> patches);

} // namespace bettercad::cfd
