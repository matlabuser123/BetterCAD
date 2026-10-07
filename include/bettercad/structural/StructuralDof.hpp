#pragma once

// P17-DOF-001 -- the degree-of-freedom numbering and the constraint model.
//
// THREE TYPES, BECAUSE THERE ARE THREE DIFFERENT FACTS.
//
//   MeshDofMap        which degrees of freedom EXIST. A function of the mesh
//                     alone: three translations for every node, numbered
//                     deterministically, and nothing about physics.
//   ConstraintSet     which of them are PRESCRIBED. A function of the model's
//                     restraints, validated against one MeshDofMap.
//   FreeEquationMap   which of them are UNKNOWN, and which row of the reduced
//                     system each one is. A function of the other two.
//
// One type carrying all three would have to be rebuilt when a restraint
// changed, although the numbering had not; and it would let a caller ask
// "which equation is this" of an object that had not been told what was
// constrained. Splitting them means each answer has exactly one owner, and the
// dependency runs one way.
//
// WHAT THIS FILE DOES NOT DO. There is no stiffness matrix, no element, no
// load, no restraint-from-geometry, no sparse storage and no solve.
// P17-ELEM-001 forms the element, P17-LOAD-001 and P17-BC-001 define the load
// and restraint payloads that will PRODUCE the NodalDofs a ConstraintSet is
// built from, P17-ASSEMBLY-001 fills the matrix and P17-SOLVE-001 solves it.
// This milestone defines the index space they all share.
//
// NOTHING HERE IS PERSISTED, and that is the milestone's central claim. A
// DofIndex and a FreeEquationIndex are solver-local (see StructuralData.hpp on
// why DofIndex is deliberately not in core/Id.hpp); every type below carries
// the MeshStamp it was built against, so a numbering kept across a remesh is
// REFUSED rather than reinterpreted against different geometry.

#include <bettercad/core/Error.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>
#include <bettercad/structural/Export.hpp>
#include <bettercad/structural/StructuralData.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace bettercad::structural {

// ---------------------------------------------------------------------------
// The numbering convention, frozen
// ---------------------------------------------------------------------------

/// How many degrees of freedom one node of a solid mesh has.
///
/// Three translations, and the reason is physical rather than conventional: a
/// node of a Tet4 continuum mesh has no rotational degree of freedom, because
/// the element's displacement field is linear in position and its gradient is
/// the strain. Shell and beam elements would add rotations and are explicitly
/// out of P17's scope (ADR-034).
///
/// Taken from `kDofComponents` rather than written as 3, so the array and the
/// count cannot disagree.
inline constexpr std::size_t kDofsPerNode = kDofComponents.size();

/// The component at @p offset within a node's three degrees of freedom:
/// 0 -> Ux, 1 -> Uy, 2 -> Uz, which is `kDofComponents`' own order.
///
/// A function over the shared array rather than a second table, so a
/// reordering of `kDofComponents` cannot leave the two disagreeing.
///
/// CYCLIC, AND THAT IS WHAT INTERLEAVED NUMBERING MEANS rather than a
/// defensive guard: flat position 3 of an element vector is the SECOND node's
/// Ux. So this is also the lookup `P17-ELEM-001` needs over a twelve-component
/// Tet4 vector `[ux1 uy1 uz1 ux2 ... uz4]`, and
/// `StructuralDof_ComponentLookupIsCyclicOverAFlatElementPosition` fixes all
/// twelve here rather than there. The modulo was unreachable until that test
/// existed -- every caller passed an offset already reduced -- and a mutation
/// removing it survived the suite, which is what a surviving mutation is for.
[[nodiscard]] constexpr DofComponent componentAt(std::size_t offset) noexcept {
    return kDofComponents[offset % kDofsPerNode];
}

/// Where @p component sits within its node's three degrees of freedom, or
/// `kDofsPerNode` if it is not one of them.
[[nodiscard]] constexpr std::size_t offsetOf(DofComponent component) noexcept {
    for (std::size_t i = 0; i < kDofsPerNode; ++i) {
        if (kDofComponents[i] == component) {
            return i;
        }
    }
    return kDofsPerNode;
}

/// Whether @p component is one of the three this module defines.
///
/// A `DofComponent` is a `std::uint8_t` enum, so `static_cast<DofComponent>(7)`
/// is a representable value that names nothing. Validation therefore has to ask
/// rather than assume -- a switch with no default simply falls through such a
/// value, which is how it reaches a lookup and indexes past the end.
[[nodiscard]] constexpr bool isRecognised(DofComponent component) noexcept {
    return offsetOf(component) < kDofsPerNode;
}

// ---------------------------------------------------------------------------
// Solver-local equation identity
// ---------------------------------------------------------------------------

/// One row of the REDUCED system: the unknowns that remain once the prescribed
/// degrees of freedom have been removed.
///
/// A DIFFERENT TYPE FROM `DofIndex`, DELIBERATELY. They count different things.
/// A `DofIndex` names one of the model's 3N degrees of freedom and exists for
/// every node whether it is restrained or not; a `FreeEquationIndex` names a row
/// of the reduced stiffness matrix and exists only for a free one.
/// Interchanging them is the classic way to assemble a stiffness contribution
/// into the wrong row -- silently, and with a plausible-looking answer -- so the
/// compiler refuses the conversion.
///
/// ZERO-BASED, AND WITH NO INVALID VALUE. This is where it departs from every
/// other handle in BetterCAD, and the departure is the point:
///
/// ```text
/// DofIndex             an identity. 1-based, 0 is invalid, so a
///                      default-constructed one names no equation
/// FreeEquationIndex    a POSITION. It is the row and column number an
///                      assembler indexes K and F with, so the value that
///                      comes out must BE the row -- a 1-based handle would
///                      put a "- 1" at every assembly site, and the one that
///                      was forgotten would be an off-by-one in the stiffness
///                      matrix
/// ```
///
/// Absence is therefore not a sentinel but `std::optional<FreeEquationIndex>`,
/// which is strictly stronger: a constrained DOF has no equation, and the
/// compiler makes the caller handle that rather than trusting them to compare
/// against a magic number. There is no default constructor for the same reason
/// -- an index that defaulted to row 0 would be the sentinel defect by another
/// route.
///
/// `StructuralResult` already keys its displacement array by a zero-based
/// ordinal into `mesh.nodes()`, so a zero-based position is not a new idea in
/// this module; it is the same one, named.
class FreeEquationIndex {
public:
    using ValueType = std::uint64_t;

    FreeEquationIndex() = delete;

    [[nodiscard]] static constexpr FreeEquationIndex fromValue(ValueType value) noexcept {
        return FreeEquationIndex{value};
    }

    /// The row and column of the reduced system. Zero-based.
    [[nodiscard]] constexpr ValueType value() const noexcept { return value_; }

    friend constexpr bool operator==(const FreeEquationIndex&,
                                     const FreeEquationIndex&) noexcept = default;
    friend constexpr auto operator<=>(const FreeEquationIndex&,
                                      const FreeEquationIndex&) noexcept = default;

private:
    constexpr explicit FreeEquationIndex(ValueType value) noexcept : value_(value) {}

    ValueType value_;
};

// ---------------------------------------------------------------------------
// MeshDofMap
// ---------------------------------------------------------------------------

/// Why a mesh cannot be given a degree-of-freedom numbering.
enum class DofMapProblem : std::uint8_t {
    /// The mesh has no nodes. A numbering of nothing is not a degenerate
    /// system but an absent one: 3N == 0 would reach an assembler as a matrix
    /// with no rows and would solve trivially, reporting success for a model
    /// that was never there.
    MeshHasNoNodes,
    /// The mesh carries no valid `MeshStamp`, so a numbering built from it
    /// could not be bound to it and a remesh could not be detected. Only a
    /// hand-assembled `Mesh` can be in this state.
    MeshHasNoIdentity,
};

[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::string_view toString(DofMapProblem problem) noexcept;

/// The degrees of freedom of one mesh, numbered.
///
/// THE NUMBERING, STATED ONCE AND FROZEN HERE. For the node at ordinal k in the
/// mesh's own enumeration and the component at offset c in `kDofComponents`:
///
/// ```text
///     DofIndex value  =  kDofsPerNode * k + c + 1
///
///     node ordinal 0 -> 1, 2, 3     (Ux, Uy, Uz)
///     node ordinal 1 -> 4, 5, 6
///     node ordinal k -> 3k+1, 3k+2, 3k+3
/// ```
///
/// One-based because `DofIndex` is: P17-DATA-001 fixed that a
/// default-constructed one is invalid "rather than pointing at the first
/// equation", and this milestone inherits the convention rather than defining a
/// second one.
///
/// INTERLEAVED PER NODE, NOT BLOCKED BY COMPONENT. A node's three degrees of
/// freedom are adjacent, rather than all the Ux first and all the Uy next. Both
/// are valid conventions and the choice is permanent, so it is recorded with
/// its reason: an element's stiffness contribution couples the degrees of
/// freedom of its own four nodes, so interleaving draws the twelve indices it
/// writes from four short runs, whereas component blocking scatters them across
/// three regions N apart. The bandwidth of the assembled matrix follows, and so
/// does the locality of every element loop. It is also the convention a reader
/// of an [ux1 uy1 uz1 ux2 ...] element vector expects, which is how
/// P17-ELEM-001's twelve-component ordering will be written.
///
/// NOT `3 * nodeId.value() + component`, WHICH IS THE DEFECT THIS TYPE EXISTS
/// TO PREVENT. P16 is explicit that node handles may be sparse -- `Mesh`
/// documents its lookup as a binary search, never an index by handle value,
/// because "handles may be sparse (1, 4, 10)" -- so arithmetic on a `NodeId`
/// would number a mesh whose nodes are 3, 1000 and 9000000 as a system with
/// twenty-seven million equations, of which nine would be used. The ordinal
/// comes from the mesh's ENUMERATION, which `Mesh` guarantees is ascending by
/// `NodeId` with no unordered container anywhere, so it is dense, deterministic
/// and the same in every build configuration.
///
/// AND IT IS THE SAME ORDINAL `StructuralResult` ALREADY USES. That is not a
/// coincidence to be maintained by hand: a result's displacement array is
/// documented "parallel to mesh.nodes(). Entry i is the displacement of
/// mesh.nodes()[i]", so a numbering that ordered its nodes any other way would
/// make the solver write node i's answer into node j's slot. The two
/// conventions are one convention, and a test fails if they diverge.
///
/// BOUND TO ITS MESH. The `MeshStamp` is carried, and `describes()` is the
/// question a consumer asks before using an index. Every mesh BetterCAD builds
/// receives a fresh `MeshId` from a process-local atomic counter, so a remesh
/// is detectable even when it produces the same node count and the same numeric
/// handles.
///
/// A VALUE, NOT A VIEW. The node handles are COPIED in rather than borrowed
/// from the mesh: four bytes a node, against a dangling pointer if a numbering
/// outlives the mesher that owns its mesh. `StructuralModel` is move-only for
/// exactly that hazard; this type avoids it instead of guarding it, and is
/// freely copyable as a result.
class BETTERCAD_STRUCTURAL_EXPORT MeshDofMap {
public:
    /// Which mesh and generation these indices belong to.
    [[nodiscard]] const meshing::MeshStamp& stamp() const noexcept { return stamp_; }

    /// Whether this numbering describes @p mesh.
    ///
    /// The refusal ADR-031 requires, asked through the mesh's own `owns()`
    /// rather than by comparing stamps here, so there is one definition of what
    /// a matching stamp is.
    ///
    /// THE NODE COUNT IS CHECKED TOO, AND THE STAMP ALONE IS NOT ENOUGH.
    /// `MeshBuilder` sets its `MeshStamp` in its CONSTRUCTOR and `build()` is
    /// "a snapshot, which is what makes a mesh a value rather than a handle to
    /// a living object" -- so two snapshots of one builder carry the SAME stamp
    /// and may hold different numbers of nodes. A stamp identifies the builder,
    /// not the snapshot.
    ///
    /// The count closes it for every pair that can arise: a builder only grows
    /// and its handles are strictly increasing, so two snapshots whose nodes
    /// differ always differ in count, and two with the same count are the same
    /// nodes. It is O(1), so nothing is paid for it.
    ///
    /// Production cannot reach the ambiguity today -- `generateVolumeMesh`
    /// builds once from a local builder -- but a numbering that could be bound
    /// to the wrong snapshot is exactly the class of defect this milestone
    /// exists to prevent, so it is refused rather than documented.
    [[nodiscard]] bool describes(const meshing::Mesh& mesh) const noexcept {
        return mesh.owns(stamp_) && mesh.nodes().size() == nodes_.size();
    }
    [[nodiscard]] bool describes(const meshing::VolumeMesh& mesh) const noexcept {
        return describes(mesh.mesh());
    }

    [[nodiscard]] std::size_t nodeCount() const noexcept { return nodes_.size(); }

    /// `kDofsPerNode * nodeCount()`. Every node has three, so this is exact and
    /// not an upper bound.
    ///
    /// Computed in `DofIndex::ValueType`, which is 64-bit, so the multiplication
    /// cannot overflow: a `NodeId` is 32-bit and the mesh's handles are strictly
    /// increasing, so a mesh holds at most 2^32 - 1 nodes and 3N + 1 fits with
    /// four orders of magnitude to spare. The `static_assert` beside the
    /// definition of this class states that bound rather than leaving it to a
    /// reader -- and it is a compile-time proof instead of a runtime branch
    /// nothing could ever take.
    [[nodiscard]] DofIndex::ValueType dofCount() const noexcept {
        return static_cast<DofIndex::ValueType>(kDofsPerNode) *
               static_cast<DofIndex::ValueType>(nodes_.size());
    }

    /// The mesh's nodes in numbering order: ascending by `NodeId`, dense,
    /// parallel to `mesh.nodes()`.
    [[nodiscard]] std::span<const meshing::NodeId> nodes() const noexcept { return nodes_; }

    /// Where @p node sits in the numbering, or nullopt if it is not a node of
    /// this mesh.
    ///
    /// A binary search over the ascending handles, which is the access model
    /// `Mesh::findNode` already uses and for the same reason.
    [[nodiscard]] std::optional<std::size_t> nodeOrdinal(meshing::NodeId node) const noexcept;

    /// The node at @p ordinal, or an invalid handle if there is none.
    [[nodiscard]] meshing::NodeId nodeAt(std::size_t ordinal) const noexcept {
        return ordinal < nodes_.size() ? nodes_[ordinal] : meshing::NodeId{};
    }

    /// The index of @p dof, or a failure naming why it is not a degree of
    /// freedom of this mesh.
    ///
    /// Fails with NotFound for a node this mesh does not have -- including an
    /// invalid handle, which no mesh has -- and with InvalidArgument for a
    /// component that is not one of the three.
    [[nodiscard]] Result<DofIndex> indexOf(const NodalDof& dof) const;

    /// The three indices of @p node, in `kDofComponents` order.
    [[nodiscard]] Result<std::array<DofIndex, kDofsPerNode>> indicesOf(meshing::NodeId node) const;

    /// Which degree of freedom @p index names, or a failure if it is outside
    /// 1 ..= `dofCount()`.
    ///
    /// The exact inverse of `indexOf`, and tested as one over every degree of
    /// freedom of several meshes rather than spot-checked: a numbering whose
    /// round trip is not the identity is numbering two things the same.
    [[nodiscard]] Result<NodalDof> dofAt(DofIndex index) const;

    /// Whether @p index is one of this map's 1 ..= `dofCount()`.
    [[nodiscard]] bool contains(DofIndex index) const noexcept {
        return index.isValid() && index.value() <= dofCount();
    }

    friend bool operator==(const MeshDofMap&, const MeshDofMap&) = default;

private:
    MeshDofMap() = default;

    /// The one function permitted to build one. Possession is the evidence that
    /// a numbering is the CANONICAL numbering of a real mesh: there is no public
    /// constructor and no setter, so a caller cannot assemble a map over nodes
    /// the mesh does not have, in an order the mesh does not use, or with a
    /// stamp belonging to a different mesh.
    friend BETTERCAD_STRUCTURAL_EXPORT Result<MeshDofMap>
    buildMeshDofMap(const meshing::Mesh& mesh);

    meshing::MeshStamp stamp_{};
    std::vector<meshing::NodeId> nodes_{};
};

/// The bound `dofCount()` relies on, proved rather than asserted in prose: the
/// largest degree-of-freedom index any representable mesh can have is far
/// inside what a `DofIndex` holds, so the numbering needs no overflow branch.
static_assert(static_cast<DofIndex::ValueType>(kDofsPerNode) *
                      static_cast<DofIndex::ValueType>(
                          std::numeric_limits<meshing::NodeId::ValueType>::max()) +
                  1 <
              std::numeric_limits<DofIndex::ValueType>::max());

/// Numbers the degrees of freedom of @p mesh.
///
/// Read-only, allocating one vector of node handles and nothing else. There is
/// no caching and no shared state: a numbering is cheap to rebuild, and a
/// cached one is a stale one waiting for a remesh.
///
/// IT TAKES A `Mesh` AND NOT A `StructuralModel`, which is a deliberate choice
/// about where the gate belongs. ADR-036 puts the stale-input gate at the
/// boundary a SOLVE is prepared at, and a numbering is not a solve: it is
/// combinatorics over node identity that cannot produce a wrong engineering
/// answer on its own. P17-ASSEMBLY-001 and P17-SOLVE-001 take a
/// `StructuralModel` and a `MeshDofMap` together, and the stamp binding is what
/// proves the two agree -- so the gate stays where it is and numbering a bare
/// mesh, which is what the sparse-handle tests need, stays possible.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Result<MeshDofMap>
buildMeshDofMap(const meshing::Mesh& mesh);

/// The problem `buildMeshDofMap` would report, or none if it would succeed.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::optional<DofMapProblem>
dofMapProblem(const meshing::Mesh& mesh) noexcept;

// ---------------------------------------------------------------------------
// ConstraintSet
// ---------------------------------------------------------------------------

/// Why a set of prescribed degrees of freedom is not usable.
enum class ConstraintProblem : std::uint8_t {
    /// A constraint names a node the mesh does not have, which includes an
    /// invalid handle and a handle from a different mesh. Never resolved to the
    /// nearest node and never skipped: a restraint the user asked for that
    /// silently did not apply leaves a model under-constrained in a way nothing
    /// reports.
    NodeNotInMesh,
    /// A constraint names a `DofComponent` that is not one of the three.
    ComponentNotRecognised,
    /// The same degree of freedom is constrained twice.
    ///
    /// REFUSED RATHER THAN DEDUPLICATED, because this milestone cannot know
    /// whether the repetition is harmless. P17-BC-001 will build these from
    /// restraints, and two restraints that both fix a node's z are harmless
    /// only if they prescribe the same value -- a question about data this type
    /// deliberately does not carry. Accepting the duplicate now would decide it
    /// by silence.
    DuplicateDof,
};

[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::string_view
toString(ConstraintProblem problem) noexcept;

/// The degrees of freedom a model prescribes.
///
/// WHICH, NOT WHAT. A `ConstraintSet` records that a degree of freedom is not
/// an unknown; it carries no prescribed VALUE. Homogeneous and inhomogeneous
/// restraints partition the unknowns identically, so the numbering does not
/// need the value, and inventing a field for it here would fix a representation
/// before P17-BC-001 defines what a restraint is. The name says `Constraint`
/// and not `Restraint` for the same reason: a restraint is the user's modelling
/// intent, and this is its consequence for the index space.
///
/// ORDER-INDEPENDENT BY CONSTRUCTION. The input is sorted into ascending
/// `DofIndex`, so two callers listing the same restraints in different orders
/// build sets that compare equal -- and the free numbering derived from either
/// is identical. That is a property of the representation, not of a sorting
/// step a future caller might skip.
///
/// BOUND TO ONE `MeshDofMap`, by the same `MeshStamp`. A constraint set outlives
/// its usefulness the moment the mesh changes, and `buildFreeEquationMap`
/// refuses a mismatched pair rather than numbering against indices that mean
/// something else.
class BETTERCAD_STRUCTURAL_EXPORT ConstraintSet {
public:
    /// Which mesh these indices belong to. Copied from the map they were
    /// validated against.
    [[nodiscard]] const meshing::MeshStamp& stamp() const noexcept { return stamp_; }

    /// The prescribed degrees of freedom, ascending and without repeats.
    [[nodiscard]] std::span<const DofIndex> constrained() const noexcept { return constrained_; }

    [[nodiscard]] std::size_t size() const noexcept { return constrained_.size(); }
    [[nodiscard]] bool isEmpty() const noexcept { return constrained_.empty(); }

    /// Whether @p index is prescribed. A binary search.
    [[nodiscard]] bool contains(DofIndex index) const noexcept;

    friend bool operator==(const ConstraintSet&, const ConstraintSet&) = default;

private:
    ConstraintSet() = default;

    friend BETTERCAD_STRUCTURAL_EXPORT Result<ConstraintSet>
    buildConstraintSet(const MeshDofMap& map, std::span<const NodalDof> prescribed);

    meshing::MeshStamp stamp_{};
    std::vector<DofIndex> constrained_{};
};

/// Validates @p prescribed against @p map and sorts it into a canonical set.
///
/// An EMPTY set is accepted and is not an error. A model with no restraints is
/// a real model whose stiffness matrix is singular, and refusing to number it
/// here would report "no restraints" as a numbering failure. Detecting an
/// insufficiently constrained model is P17-SOLVE-001's, which is where the
/// rigid-body modes are visible; this milestone's job is to represent what was
/// asked for faithfully, including nothing.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Result<ConstraintSet>
buildConstraintSet(const MeshDofMap& map, std::span<const NodalDof> prescribed);

/// The problem `buildConstraintSet` would report, or none if it would succeed.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::optional<ConstraintProblem>
constraintProblem(const MeshDofMap& map, std::span<const NodalDof> prescribed);

/// All three degrees of freedom of each of @p nodes, in numbering order.
///
/// The common case spelled once: a fully fixed node set, which is what a
/// restraint on a CAD face resolves to once `meshing::boundaryNodesOf` has
/// turned the face's facets into nodes. It does NO validation --
/// `buildConstraintSet` does, and a helper that silently dropped an unknown
/// node would defeat it.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::vector<NodalDof>
fullyFixedDofs(std::span<const meshing::NodeId> nodes);

// ---------------------------------------------------------------------------
// FreeEquationMap
// ---------------------------------------------------------------------------

/// The unknowns of the reduced system, numbered compactly.
///
/// 0 ..= `freeCount()` - 1, assigned to the free degrees of freedom in
/// ascending `DofIndex` order. Compact and gap-free, so the reduced system has
/// exactly `freeCount()` rows and an assembler can size its storage from that
/// one number.
///
/// `isFree` AND `isConstrained` CANNOT DISAGREE, because only one of them is
/// stored. A degree of freedom of this map is free exactly when it has an
/// equation, and constrained exactly when it is in range and does not -- so the
/// partition is a property of the representation rather than an invariant two
/// containers have to maintain between them. The tests check it against the
/// `ConstraintSet`'s own list, which is the version of the question that is not
/// a tautology: two independently built objects are compared.
///
/// STORAGE IS O(freeCount), one `DofIndex` per unknown. The forward direction
/// is a binary search over it and the inverse is an index, so neither direction
/// needs a table over all 3N degrees of freedom and neither needs a sentinel.
class BETTERCAD_STRUCTURAL_EXPORT FreeEquationMap {
public:
    /// Which mesh this numbering belongs to.
    [[nodiscard]] const meshing::MeshStamp& stamp() const noexcept { return stamp_; }

    /// Total degrees of freedom of the model: `freeCount() + constrainedCount()`.
    [[nodiscard]] DofIndex::ValueType dofCount() const noexcept { return dofCount_; }

    /// Rows of the reduced system.
    [[nodiscard]] std::size_t freeCount() const noexcept { return freeDofs_.size(); }

    /// Prescribed degrees of freedom. Derived, never stored separately.
    [[nodiscard]] DofIndex::ValueType constrainedCount() const noexcept {
        return dofCount_ - static_cast<DofIndex::ValueType>(freeDofs_.size());
    }

    /// The free degrees of freedom in equation order: entry e is the degree of
    /// freedom of equation e. Ascending.
    [[nodiscard]] std::span<const DofIndex> freeDofs() const noexcept { return freeDofs_; }

    /// The equation @p index is solved as, or nullopt if it is prescribed or is
    /// not a degree of freedom of this map.
    [[nodiscard]] std::optional<FreeEquationIndex> equationOf(DofIndex index) const noexcept;

    /// The degree of freedom equation @p equation solves for, or an invalid
    /// index if there is no such equation.
    [[nodiscard]] DofIndex dofOf(FreeEquationIndex equation) const noexcept {
        return equation.value() < freeDofs_.size()
                   ? freeDofs_[static_cast<std::size_t>(equation.value())]
                   : DofIndex{};
    }

    [[nodiscard]] bool isFree(DofIndex index) const noexcept {
        return equationOf(index).has_value();
    }

    /// Whether @p index is a degree of freedom of this map that is not free.
    /// False for an index this map does not have -- a degree of freedom of
    /// another mesh is not "constrained" here, it is absent, and the two must
    /// not be confused.
    [[nodiscard]] bool isConstrained(DofIndex index) const noexcept {
        return contains(index) && !isFree(index);
    }

    /// Whether @p index is one of this map's 1 ..= `dofCount()`.
    [[nodiscard]] bool contains(DofIndex index) const noexcept {
        return index.isValid() && index.value() <= dofCount_;
    }

    friend bool operator==(const FreeEquationMap&, const FreeEquationMap&) = default;

private:
    FreeEquationMap() = default;

    friend BETTERCAD_STRUCTURAL_EXPORT Result<FreeEquationMap>
    buildFreeEquationMap(const MeshDofMap& map, const ConstraintSet& constraints);

    meshing::MeshStamp stamp_{};
    DofIndex::ValueType dofCount_ = 0;
    std::vector<DofIndex> freeDofs_{};
};

/// Numbers the unknowns of @p map that @p constraints does not prescribe.
///
/// Fails with FailedPrecondition if the two were not built against the same
/// mesh. That is the check which makes a remesh safe: the pair is the only
/// place the numbering and the restraints meet, so one comparison here is
/// cheaper than a stamp check at every assembly site -- and it cannot be
/// forgotten, because there is no other way to obtain a `FreeEquationMap`.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Result<FreeEquationMap>
buildFreeEquationMap(const MeshDofMap& map, const ConstraintSet& constraints);

} // namespace bettercad::structural
