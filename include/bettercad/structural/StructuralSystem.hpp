#pragma once

// P17-ASSEMBLY-001 -- the global linear system `K u = F`.
//
// WHAT THIS IS. The assembled, UNCONSTRAINED global system of the current
// linear-static Tet4 model: a sparse stiffness matrix in N/m, a dense force
// vector in N, and the input state they were computed from. Derived and
// disposable: nothing here is persisted, undone or treated as canonical.
//
// THE REPRESENTATION IS BETTERCAD'S, AND ADR-038 SAYS WHY. Compressed sparse
// row, owned as three vectors, with no linear algebra library linked into this
// module -- `bettercad_structural` still links none, which keeps the
// structural Eigen admission `P17-SOLVE-001`'s decision as both
// `src/structural/CMakeLists.txt` and `TODO.md` reserve it. The arrays are
// laid out so a solver can wrap them in `Eigen::Map<const
// Eigen::SparseMatrix<double>>`, SuiteSparse or anything else at zero copy.
//
// THE ROW SPACE IS THE FREE-EQUATION NUMBERING OF THE EMPTY CONSTRAINT SET.
// ADR-037 made `FreeEquationIndex` a zero-based POSITION precisely so that
// "the value that comes out must BE the row -- a 1-based handle would put a
// `- 1` at every assembly site, and the one that was forgotten would be an
// off-by-one in the stiffness matrix". With nothing constrained every degree of
// freedom is free, so `freeCount() == dofCount()` and the free numbering IS the
// global one. The consequence is that this module contains **no index
// arithmetic at all**: no `- 1`, no `3 * node`, no row computed from a handle.
//
// WHAT THIS IS NOT.
//
//     no constraint application     no row is zeroed, no diagonal is set to
//                                   one, no penalty stiffness is added.
//                                   `P17-SOLVE-001` applies the ConstraintSet
//     no regularisation             a free body's K is SINGULAR and stays
//                                   singular. No diagonal epsilon, no pinned
//                                   node, no springs. That is correct physics
//                                   and the rigid-body modes are tested
//     no solve                      no factorisation, no iteration, no
//                                   SparseLU, LDLT, CG or BiCGSTAB
//     no element formulation        `Ke` is `P17-ELEM-001`'s. There is no B,
//                                   no D, no detJ and no Lame constant here
//     no load integration           the nodal forces are `P17-LOAD-001`'s.
//                                   There is no traction, pressure, facet area
//                                   or density here
//     no post-processing            no displacement, strain, stress or reaction

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/math/Vector.hpp>
#include <bettercad/meshing/GeometryPreparation.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/structural/Export.hpp>
#include <bettercad/structural/StructuralDof.hpp>
#include <bettercad/structural/Tet4Element.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <string_view>
#include <vector>

namespace bettercad::structural {

class StructuralMaterial;
class StructuralModel;
class PreparedLoads;
class GlobalStructuralSystem;

// ---------------------------------------------------------------------------
// StiffnessMatrix
// ---------------------------------------------------------------------------

/// The assembled global stiffness matrix, in `N/m`.
///
/// COMPRESSED SPARSE ROW, STATED ONCE AND FROZEN HERE.
///
/// ```text
/// rowStart   size rows() + 1, monotone non-decreasing, rowStart[0] == 0,
///            rowStart[rows()] == nonZeros()
/// inner      size nonZeros(), STRICTLY ASCENDING within each row
/// values     size nonZeros(), parallel to inner, SI (N/m)
/// ```
///
/// The invariants are established on construction and asserted again by the
/// tests, because a CSR with a non-monotone outer array or unsorted inner
/// indices is not a slow matrix, it is a different matrix.
///
/// THE INDEX WIDTH IS 64-BIT, deliberately and at no practical cost. A 32-bit
/// inner index is the classic sparse overflow: `nnz` for a Tet4 mesh runs at
/// roughly forty times `Ndof`, so a 32-bit count would cap a model at about 50
/// million degrees of freedom -- plausible for a real analysis. The bound is
/// proved by the `static_assert` below rather than hoped for.
///
/// AN ABSENT ENTRY IS ZERO. `coeff()` returns zero for a pair the pattern does
/// not contain, which is what a sparse matrix means. Stored explicit zeros are
/// permitted and do occur: the assembler emits all 144 local entries and
/// prunes none, so the pattern is a function of CONNECTIVITY ALONE and not of
/// numerical cancellation. That is what makes `nonZeros()` predictable and
/// identical in every preset.
///
/// POSSESSION IS THE EVIDENCE (ADR-036). There is no public constructor and
/// one friend, so a caller cannot hand a solver a matrix whose pattern was
/// never validated, whose dimensions do not match a numbering, or which holds
/// a value that is not finite.
class BETTERCAD_STRUCTURAL_EXPORT StiffnessMatrix {
public:
    using Index = std::uint64_t;

    [[nodiscard]] std::size_t rows() const noexcept { return rows_; }
    [[nodiscard]] std::size_t columns() const noexcept { return columns_; }
    [[nodiscard]] std::size_t nonZeros() const noexcept { return values_.size(); }

    /// The CSR arrays, for a solver that maps rather than copies.
    ///
    /// `values()` is SI. It is `double` and not `Stiffness` so that the buffer
    /// is layout-compatible with every sparse library there is; the TYPED
    /// accessor is `coeff()`, and the dimension is frozen in this header
    /// rather than in a comment on a call site. `Tet4Stiffness` made the same
    /// choice for the same reason.
    [[nodiscard]] std::span<const Index> rowStart() const noexcept { return rowStart_; }
    [[nodiscard]] std::span<const Index> innerIndices() const noexcept { return inner_; }
    [[nodiscard]] std::span<const double> values() const noexcept { return values_; }

    /// `K(row, column)`, or zero if the pattern does not hold that pair.
    ///
    /// A binary search within the row, so O(log k) in that row's entry count
    /// and never a scan of the matrix. Out-of-range indices give zero rather
    /// than a failure: asking about a cell outside the matrix is a question
    /// whose answer is zero, and the DIMENSIONS are checked where they matter,
    /// on construction.
    [[nodiscard]] Stiffness coeff(std::size_t row, std::size_t column) const noexcept;

    /// `max |K(i,j) - K(j,i)|` over the stored pattern, in SI.
    ///
    /// Computed over stored entries and their counterparts, never by forming a
    /// transpose or densifying: a large mesh must not be made dense to be
    /// checked.
    [[nodiscard]] double largestSymmetryError() const noexcept;

    /// The largest `|K(i,j)|`, in SI. Zero for an empty pattern.
    [[nodiscard]] double largestMagnitude() const noexcept;

    friend bool operator==(const StiffnessMatrix&, const StiffnessMatrix&) = default;

private:
    StiffnessMatrix() = default;

    friend BETTERCAD_STRUCTURAL_EXPORT Result<GlobalStructuralSystem>
    assembleStructuralSystem(const StructuralModel& model, const StructuralMaterial& material,
                             const PreparedLoads& loads);

    std::size_t rows_ = 0;
    std::size_t columns_ = 0;
    std::vector<Index> rowStart_{};
    std::vector<Index> inner_{};
    std::vector<double> values_{};
};

/// The bound the 64-bit index relies on, proved rather than asserted in prose.
/// A `NodeId` is 32-bit and the mesh's handles are strictly increasing, so a
/// mesh holds at most 2^32 - 1 nodes and `3N` fits with four orders of
/// magnitude to spare -- so no row index and no entry count derived from one
/// can overflow.
static_assert(static_cast<StiffnessMatrix::Index>(kDofsPerNode) *
                  static_cast<StiffnessMatrix::Index>(
                      std::numeric_limits<meshing::NodeId::ValueType>::max()) <
              std::numeric_limits<StiffnessMatrix::Index>::max() / 2);

// ---------------------------------------------------------------------------
// ForceVector
// ---------------------------------------------------------------------------

/// The assembled global load vector, in `N`.
///
/// Dense, length `Ndof`, and EXPLICITLY ZERO-INITIALISED rather than left to
/// whatever the allocator returned: an uninitialised right-hand side is the
/// kind of defect that produces a plausible answer.
class BETTERCAD_STRUCTURAL_EXPORT ForceVector {
public:
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    /// Entry @p row, or zero outside the vector.
    [[nodiscard]] Force operator[](std::size_t row) const noexcept {
        return row < entries_.size() ? Force::fromSi(entries_[row]) : Force{};
    }

    /// SI, for a solver that maps rather than copies.
    [[nodiscard]] std::span<const double> values() const noexcept { return entries_; }

    /// The sum over the vector, grouped per component by the frozen
    /// interleaving and taken in row order, so the answer is reproducible and
    /// is comparable with `PreparedLoads::resultantForce()`.
    [[nodiscard]] Force3D resultantForce() const noexcept;

    friend bool operator==(const ForceVector&, const ForceVector&) = default;

private:
    ForceVector() = default;

    friend BETTERCAD_STRUCTURAL_EXPORT Result<GlobalStructuralSystem>
    assembleStructuralSystem(const StructuralModel& model, const StructuralMaterial& material,
                             const PreparedLoads& loads);

    std::vector<double> entries_{};
};

// ---------------------------------------------------------------------------
// AssemblySource
// ---------------------------------------------------------------------------

/// Exactly what an assembled system was computed from.
///
/// SIX FIELDS, AND EACH IS PROVED BY AN INPUT rather than passed in: the body,
/// the control and the geometry revision come from the `StructuralModel`, the
/// mesh stamp from the numbering, and the material identity and revision from
/// the `StructuralMaterial`. There is nothing here a caller could get wrong.
///
/// WHY NOT `StructuralResultSource`. That type carries two more fields -- the
/// analysis identity and its revision -- and neither is available to this
/// function or needed by it: `K` is a function of the mesh and the material,
/// and `F` of the mesh and the prepared loads, whose possession already proves
/// they were resolved against this mesh.
///
/// AND THE DISTINCTION MATTERS, because P17-DATA-001 put loads, restraints and
/// solver settings in ONE definition with ONE revision. So
/// `analysisRevision` moves on a RESTRAINT edit, which cannot change an
/// unconstrained `K` or `F` at all. A system stamped with
/// `StructuralResultSource` would be reported stale by an edit that did not
/// touch it. Keeping the analysis fields out is the dependency precision the
/// brief asks for, and it costs nothing: a solve compares
/// `StructuralResultSource` at the P17-DATA boundary as it already does, and
/// the numerical independence is tested -- the same mesh, material and loads
/// with different restraints assemble to an identical `K` and `F`.
struct AssemblySource {
    ObjectId body{};
    MeshControlId control{};
    meshing::GeometryRevision geometry{};
    meshing::MeshStamp mesh{};
    MaterialId material{};
    std::uint64_t materialRevision = 0;

    /// Field-complete and defaulted on purpose: adding a dependency without
    /// adding it to the comparison is then impossible.
    friend bool operator==(const AssemblySource&, const AssemblySource&) = default;
};

// ---------------------------------------------------------------------------
// GlobalStructuralSystem
// ---------------------------------------------------------------------------

/// Why a structural model cannot be assembled into a global system.
enum class AssemblyProblem : std::uint8_t {
    /// The mesh has no nodes, so there is no system. Refused by `P17-DOF`'s
    /// own numbering for the reason it records: a matrix with no rows would
    /// solve trivially and report success for a model that was never there.
    MeshHasNoDegreesOfFreedom,
    /// The mesh carries no tetrahedron, so `K` would be structurally empty. A
    /// body with a boundary and no volume is not a degenerate model, it is a
    /// failed mesh, and P16 does not produce one.
    MeshHasNoElements,
    /// An element names a node the mesh does not have. Only a hand-assembled
    /// mesh can be in this state; it is refused rather than skipped.
    ElementNodeMissing,
    /// `P17-ELEM-001` refused an element: degenerate, inverted, non-finite, or
    /// an unusable material. Propagated with the `ElementId`, never skipped --
    /// an assembly that quietly omitted one tetrahedron would produce a softer
    /// body with nothing reporting it.
    ElementRejected,
    /// The prepared loads were built for a different mesh.
    LoadSourceMismatch,
    /// A prepared load names a node the current numbering does not have, which
    /// means the derived state is inconsistent. It fails rather than dropping
    /// the load.
    LoadNodeMissing,
    /// An assembled entry came out non-finite. Valid inputs cannot cause it
    /// and a pathological fixture can.
    NonFiniteSystem,
};

[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::string_view
toString(AssemblyProblem problem) noexcept;

/// `K u = F` for one current model, with the input state it came from.
///
/// DIMENSIONAL CONSISTENCY, FROZEN HERE rather than left to a call site:
///
/// ```text
/// K   N/m        u   m        K u   N        F   N
/// ```
///
/// so `K u = F` is an equation between forces. There is no internal conversion
/// to `N/mm` for anyone's convenience; SI in, SI out, as everywhere else.
///
/// DERIVED AND DISPOSABLE. `K`, `F` and the CSR arrays are rebuilt from the
/// model; none is persisted, undone or canonical, and nothing in `io` or in
/// any command holds one.
class BETTERCAD_STRUCTURAL_EXPORT GlobalStructuralSystem {
public:
    [[nodiscard]] const StiffnessMatrix& stiffness() const noexcept { return stiffness_; }
    [[nodiscard]] const ForceVector& force() const noexcept { return force_; }

    /// `3 * nodeCount()`, and the dimension of both.
    [[nodiscard]] std::size_t degreesOfFreedom() const noexcept { return force_.size(); }

    /// What this was assembled from.
    [[nodiscard]] const AssemblySource& source() const noexcept { return source_; }

    /// The numbering the rows are in. Carried so a consumer never has to
    /// rebuild it and risk building a different one.
    [[nodiscard]] const MeshDofMap& numbering() const noexcept { return numbering_; }

    /// Which mesh these rows belong to.
    [[nodiscard]] const meshing::MeshStamp& mesh() const noexcept { return numbering_.stamp(); }

    /// Whether this system describes @p mesh: the stamp AND the node count,
    /// for the reason `MeshDofMap::describes` records.
    [[nodiscard]] bool describes(const meshing::Mesh& mesh) const noexcept {
        return numbering_.describes(mesh);
    }

    /// The element order the stiffness was summed in.
    ///
    /// A NUMERICAL CONTRACT AND NOT A DIAGNOSTIC: many elements contribute to
    /// one entry, so changing the order changes the last bits of the sum.
    /// Recorded so the evidence can fingerprint it and so a reader can see
    /// that it is P16's ascending-`ElementId` order and not this module's
    /// invention.
    [[nodiscard]] std::span<const meshing::ElementId> elementOrder() const noexcept {
        return elements_;
    }

    friend bool operator==(const GlobalStructuralSystem&,
                           const GlobalStructuralSystem&) = default;

private:
    /// THE ONLY CONSTRUCTOR, AND THERE IS NO DEFAULT ONE. Not a style choice:
    /// `StiffnessMatrix`, `ForceVector` and `MeshDofMap` each have a private
    /// constructor with one friend, so a `GlobalStructuralSystem` cannot exist
    /// without a matrix, a vector and a numbering that were each already
    /// validated by the function entitled to build them. ADR-036's gate on the
    /// members propagates to the whole type, and a half-built system is
    /// unrepresentable rather than merely discouraged.
    GlobalStructuralSystem(StiffnessMatrix stiffness, ForceVector force, AssemblySource source,
                           MeshDofMap numbering, std::vector<meshing::ElementId> elements)
        : stiffness_(std::move(stiffness)), force_(std::move(force)), source_(std::move(source)),
          numbering_(std::move(numbering)), elements_(std::move(elements)) {}

    friend BETTERCAD_STRUCTURAL_EXPORT Result<GlobalStructuralSystem>
    assembleStructuralSystem(const StructuralModel& model, const StructuralMaterial& material,
                             const PreparedLoads& loads);

    StiffnessMatrix stiffness_;
    ForceVector force_;
    AssemblySource source_;
    MeshDofMap numbering_;
    std::vector<meshing::ElementId> elements_;
};

/// Assembles the unconstrained global system of @p model under @p material and
/// @p loads.
///
/// ATOMIC. Every element and every load is accumulated into working state and
/// validated before anything is published; a failure returns no system rather
/// than a half-filled one. An assembly that published `K` with element 42
/// missing would be a softer body that solved successfully.
///
/// IT TAKES A `StructuralModel`, which is how the stale-geometry and
/// stale-mesh questions are answered without a check here: possession proves
/// the geometry and the mesh are current and that the mapping came from the
/// same lookup (ADR-036). And it takes `PreparedLoads`, whose possession
/// proves every load target resolved -- the pair is checked against each
/// other, so an M1 load vector cannot be assembled with M2 stiffness because
/// some NodeIds happen to overlap.
///
/// SERIAL AND DETERMINISTIC BY DESIGN. There is no thread, no atomic and no
/// parallel scatter: many elements write the same global entry, so a
/// schedule-dependent summation order would make the last bits of `K` depend
/// on the machine. Correctness first; a deterministic parallel merge is a
/// later performance decision with its own evidence.
///
/// BOTH ORDERS ARE FROZEN AND NEITHER IS THIS MODULE'S INVENTION:
///
/// ```text
/// elements   model.mesh().mesh().tetrahedra(), which P16 guarantees is
///            "ascending ElementId [...] Nothing here is an unordered
///            container, so the same construction gives the same enumeration
///            in Debug, Release and Debug-shared"
/// entries    local row 0..11 then local column 0..11
/// loads      PreparedLoads::nodal(), "ascending by handle"
/// ```
///
/// AN EMPTY LOAD SET IS VALID and gives `F = 0`. A model with no restraints is
/// equally valid and gives a SINGULAR `K`; detecting that the model is
/// insufficiently constrained is `P17-SOLVE-001`'s, which is where the
/// eigenstructure is visible, and refusing it here would report physics as an
/// assembly failure.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Result<GlobalStructuralSystem>
assembleStructuralSystem(const StructuralModel& model, const StructuralMaterial& material,
                         const PreparedLoads& loads);

/// The problem `assembleStructuralSystem` would report, or none if it would
/// succeed. The same checks in the same order.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::optional<AssemblyProblem>
structuralAssemblyProblem(const StructuralModel& model, const StructuralMaterial& material,
                          const PreparedLoads& loads);

/// The twelve global degrees of freedom of @p nodes, in the frozen local Tet4
/// order.
///
/// ```text
///  0,  1,  2    node 0   Ux Uy Uz
///  3,  4,  5    node 1   Ux Uy Uz
///  6,  7,  8    node 2   Ux Uy Uz
///  9, 10, 11    node 3   Ux Uy Uz
/// ```
///
/// Exposed because it is the one place the local and global orders meet, so it
/// is the one place a regression test can pin that correspondence directly
/// rather than inferring it from a matrix entry. `localDofIndex` is the same
/// mapping from the other side and the two are asserted to agree.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Result<std::array<DofIndex, kTet4Dofs>>
elementDegreesOfFreedom(const MeshDofMap& numbering,
                        const std::array<meshing::NodeId, kTet4Nodes>& nodes);

} // namespace bettercad::structural
