#pragma once

// P18-DOF-001 -- the temperature degree-of-freedom numbering and the
// prescribed-temperature constraint model.
//
// THREE TYPES, BECAUSE THERE ARE THREE DIFFERENT FACTS, the division
// P17-DOF-001 drew and this module adopts:
//
//   ThermalDofMap            which temperature unknowns EXIST. A function of
//                            the mesh alone: ONE scalar per node, numbered
//                            from the mesh's own enumeration, and nothing
//                            about physics or boundary conditions.
//   ThermalConstraintMap     which of them are PRESCRIBED, and AT WHAT
//                            TEMPERATURE. A function of the resolved
//                            boundary conditions, validated against one
//                            ThermalDofMap.
//   ThermalFreeEquationMap   which of them are UNKNOWN, and which row of the
//                            reduced system each one is. A function of the
//                            other two.
//
// ONE SCALAR PER NODE, AND NOTHING STRUCTURAL LEAKS IN. A node of a Tet4
// conduction mesh has exactly one unknown, its temperature. There is no
// component, no interleaving and no "DOFs per node" arithmetic: the
// temperature degree of freedom of a node IS the node's ordinal in the mesh's
// enumeration. That makes `ThermalDofIndex` the index into P18-DATA-001's
// nodal temperature array directly -- `ThermalResult::temperatures()[dof]` is
// the temperature of `nodes()[dof]` -- which is why it is zero-based, unlike
// P17's one-based `DofIndex`: it is a POSITION, and the position must be the
// array index with no "- 1" at any site.
//
// THE ONE P18-SPECIFIC FACT. A structural restraint holds a displacement at
// ZERO, so P17's `ConstraintSet` records WHICH degrees of freedom are
// prescribed and no value. A thermal Dirichlet condition holds a node at an
// arbitrary absolute temperature -- 300 K, 350 K -- so the constraint map here
// records the VALUE as well, in kelvin, and the reduced system a later solve
// forms is `Kff Tf = Qf - Kfc Tc` with a non-zero `Tc`. `uc = 0` is not copied
// from P17, and a mutation that forced the prescribed temperatures to zero is
// one of this milestone's probes.
//
// NOTHING HERE IS PERSISTED, and that is the milestone's central claim. A
// `ThermalDofIndex` and a `ThermalFreeEquationIndex` are solver-local; every
// map below carries the `MeshStamp` it was built against, so a numbering kept
// across a remesh is REFUSED rather than reinterpreted against different
// geometry. No canonical thermal boundary condition stores one of these; a
// boundary condition names a CAD face (P18-DATA-001) and is resolved to nodes
// and then to these indices afresh, by P18-BC-001, for whatever mesh is
// current.
//
// WHAT THIS FILE DOES NOT DO. There is no conductivity or capacity matrix, no
// element, no load, no boundary condition from CAD geometry, no sparse
// storage and no solve. It defines the index space those milestones share.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>
#include <bettercad/thermal/Export.hpp>

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace bettercad::thermal {

// ---------------------------------------------------------------------------
// The numbering convention, frozen
// ---------------------------------------------------------------------------

/// How many temperature unknowns one node has. One: the scalar temperature.
/// Written as a named constant so that a later formulation with more (a
/// coupled field, say) is a visible change and not an implicit assumption.
inline constexpr std::size_t kThermalDofsPerNode = 1;

/// One temperature unknown of one numbering of one mesh.
///
/// ZERO-BASED, A POSITION, AND WITH NO INVALID VALUE. For the node at ordinal
/// k in the mesh's own enumeration:
///
/// ```text
///     ThermalDofIndex value  =  k
///
///     node ordinal 0 -> 0
///     node ordinal k -> k
/// ```
///
/// It is the index of that node's temperature in P18-DATA-001's result array
/// and of its row in the full thermal system, so the value that comes out
/// must BE the index. Absence is `std::optional<ThermalDofIndex>`, never a
/// sentinel; there is no default constructor, because an index that defaulted
/// to row 0 would be the sentinel defect by another route.
///
/// NOT `nodeId.value()`, WHICH IS THE DEFECT THIS TYPE EXISTS TO PREVENT. P16
/// is explicit that node handles may be sparse ("handles may be sparse (1, 4,
/// 10)"; `Mesh::findNode` is a binary search, never an index by handle value),
/// so arithmetic on a `NodeId` would number a mesh whose nodes are 7, 104,
/// 9001 and 500000 as a system with half a million unknowns, four of them
/// used. The ordinal comes from the mesh's ENUMERATION.
///
/// SOLVER-LOCAL, AND NOT IN core/Id.hpp. Remesh and it means nothing;
/// renumber and it means something else. It is deliberately a distinct type
/// from P17's `DofIndex` (one-based, three per node) and from
/// `meshing::NodeId` and `ElementId`: none converts to another, and the
/// compile-fail group proves it.
class ThermalDofIndex {
public:
    using ValueType = std::uint64_t;

    ThermalDofIndex() = delete;

    [[nodiscard]] static constexpr ThermalDofIndex fromValue(ValueType value) noexcept {
        return ThermalDofIndex{value};
    }

    /// The node ordinal, the row of the full system, the index into a nodal
    /// temperature array. Zero-based.
    [[nodiscard]] constexpr ValueType value() const noexcept { return value_; }

    friend constexpr bool operator==(const ThermalDofIndex&, const ThermalDofIndex&) noexcept = default;
    friend constexpr auto operator<=>(const ThermalDofIndex&, const ThermalDofIndex&) noexcept = default;

private:
    constexpr explicit ThermalDofIndex(ValueType value) noexcept : value_(value) {}

    ValueType value_;
};

/// One row of the REDUCED system: the unknowns that remain once the
/// prescribed temperatures have been removed.
///
/// A DIFFERENT TYPE FROM `ThermalDofIndex`, DELIBERATELY. They count different
/// things: a `ThermalDofIndex` exists for every node whether its temperature
/// is prescribed or not; a `ThermalFreeEquationIndex` names a row of the
/// reduced conductivity matrix and exists only for a free one. The two agree
/// only for a model with no prescribed temperature, so a test on such a model
/// would not catch the confusion and the compiler has to. Zero-based, no
/// default, absence is `std::optional`, for the reasons `ThermalDofIndex`
/// gives.
class ThermalFreeEquationIndex {
public:
    using ValueType = std::uint64_t;

    ThermalFreeEquationIndex() = delete;

    [[nodiscard]] static constexpr ThermalFreeEquationIndex fromValue(ValueType value) noexcept {
        return ThermalFreeEquationIndex{value};
    }

    /// The row and column of the reduced system. Zero-based.
    [[nodiscard]] constexpr ValueType value() const noexcept { return value_; }

    friend constexpr bool operator==(const ThermalFreeEquationIndex&,
                                     const ThermalFreeEquationIndex&) noexcept = default;
    friend constexpr auto operator<=>(const ThermalFreeEquationIndex&,
                                      const ThermalFreeEquationIndex&) noexcept = default;

private:
    constexpr explicit ThermalFreeEquationIndex(ValueType value) noexcept : value_(value) {}

    ValueType value_;
};

// ---------------------------------------------------------------------------
// ThermalDofMap
// ---------------------------------------------------------------------------

/// Why a mesh cannot be given a temperature numbering.
enum class DofMapProblem : std::uint8_t {
    /// The mesh has no nodes. A numbering of nothing is not a degenerate
    /// system but an absent one: N == 0 would reach an assembler as a matrix
    /// with no rows and would solve trivially, reporting success for a model
    /// that was never there. The same refusal `ThermalResult::create` makes.
    MeshHasNoNodes,
    /// The mesh carries no valid `MeshStamp`, so a numbering built from it
    /// could not be bound to it and a remesh could not be detected. Only a
    /// default-constructed `Mesh` can be in this state.
    MeshHasNoIdentity,
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(DofMapProblem problem) noexcept;

/// The temperature unknowns of one mesh, numbered.
///
/// BOUND TO ITS MESH. The `MeshStamp` is carried, and `describes()` is the
/// question a consumer asks before using an index. Every mesh BetterCAD
/// builds receives a fresh `MeshId` from a process-local counter, so a remesh
/// is detectable even when it produces the same node count and the same
/// numeric handles -- counts and handles are NOT identity, the stamp is.
///
/// THE NODE COUNT IS CHECKED TOO, AND THE STAMP ALONE IS NOT ENOUGH.
/// `MeshBuilder` sets its stamp in its constructor and `build()` is a
/// snapshot, so two snapshots of one builder share a stamp and may differ in
/// size; the count closes it, O(1). P17-DOF-001 found that gap by reading the
/// builder, and this module inherits the check rather than rediscovering it.
///
/// A VALUE, NOT A VIEW. The node handles are COPIED in, four bytes a node:
/// O(N) in the node COUNT, never in the largest handle, and no pointer into a
/// mesh that a remesh could leave dangling. Freely copyable.
///
/// NO UNORDERED CONTAINER. The forward lookup is a binary search over the
/// ascending handles -- `Mesh`'s own access model -- and the inverse is an
/// index. Equation order comes from the mesh's enumeration and from nothing
/// a hash could reorder.
class BETTERCAD_THERMAL_EXPORT ThermalDofMap {
public:
    /// Which mesh and generation these indices belong to.
    [[nodiscard]] const meshing::MeshStamp& stamp() const noexcept { return stamp_; }

    /// Whether this numbering describes @p mesh: the mesh owns the stamp AND
    /// the node count agrees. Asked through the mesh's own `owns()` so there
    /// is one definition of what a matching stamp is.
    [[nodiscard]] bool describes(const meshing::Mesh& mesh) const noexcept {
        return mesh.owns(stamp_) && mesh.nodes().size() == nodes_.size();
    }
    [[nodiscard]] bool describes(const meshing::VolumeMesh& mesh) const noexcept {
        return describes(mesh.mesh());
    }

    [[nodiscard]] std::size_t nodeCount() const noexcept { return nodes_.size(); }

    /// `kThermalDofsPerNode * nodeCount()`, which is `nodeCount()`. Exact.
    [[nodiscard]] ThermalDofIndex::ValueType dofCount() const noexcept {
        return static_cast<ThermalDofIndex::ValueType>(kThermalDofsPerNode) *
               static_cast<ThermalDofIndex::ValueType>(nodes_.size());
    }

    /// The mesh's nodes in numbering order: ascending by `NodeId`, dense,
    /// parallel to `mesh.nodes()`. Entry d is the node of `ThermalDofIndex` d.
    [[nodiscard]] std::span<const meshing::NodeId> nodes() const noexcept { return nodes_; }

    /// The temperature unknown of @p node, or NotFound if it is not a node of
    /// this mesh -- including the invalid handle, which no mesh has. Never
    /// resolved to the nearest node, never inserted.
    [[nodiscard]] Result<ThermalDofIndex> dofOf(meshing::NodeId node) const;

    /// The node whose temperature @p dof is, or NotFound if it is outside
    /// 0 ..< `dofCount()`. The exact inverse of `dofOf`.
    [[nodiscard]] Result<meshing::NodeId> nodeOf(ThermalDofIndex dof) const;

    /// Whether @p dof is one of this map's 0 ..< `dofCount()`.
    [[nodiscard]] bool contains(ThermalDofIndex dof) const noexcept {
        return dof.value() < dofCount();
    }

    /// The four temperature unknowns of @p element, IN THE ELEMENT'S OWN NODE
    /// ORDER: entry i is the unknown of `element.nodes[i]`, exactly. Not
    /// sorted, not deduplicated -- a valid Tet4 has four distinct nodes, and
    /// the local order is what a later element gradient and Jacobian are
    /// written against. Fails with NotFound, creating nothing, if any node is
    /// not a node of this mesh.
    [[nodiscard]] Result<std::array<ThermalDofIndex, 4>>
    elementDofs(const meshing::Tetrahedron& element) const;

    friend bool operator==(const ThermalDofMap&, const ThermalDofMap&) = default;

private:
    ThermalDofMap() = default;

    /// The one function permitted to build one. Possession is the evidence
    /// that a numbering is THE numbering of a real mesh: there is no public
    /// constructor and no setter, so a caller cannot assemble a map over
    /// nodes the mesh does not have, in an order the mesh does not use, or
    /// with a stamp belonging to a different mesh.
    friend BETTERCAD_THERMAL_EXPORT Result<ThermalDofMap> buildThermalDofMap(const meshing::Mesh& mesh);

    meshing::MeshStamp stamp_{};
    std::vector<meshing::NodeId> nodes_{};
};

/// The bound `dofCount()` relies on, proved rather than asserted in prose: a
/// mesh holds at most 2^32 - 1 nodes (its handles are 32-bit and strictly
/// increasing), so one unknown per node is far inside a 64-bit index.
static_assert(static_cast<ThermalDofIndex::ValueType>(kThermalDofsPerNode) *
                      static_cast<ThermalDofIndex::ValueType>(
                          std::numeric_limits<meshing::NodeId::ValueType>::max()) <
              std::numeric_limits<ThermalDofIndex::ValueType>::max());

/// Numbers the temperature unknowns of @p mesh.
///
/// Read-only, allocating one vector of node handles and nothing else. No
/// caching, no shared state, no global: a numbering is cheap to rebuild, and
/// a cached one is a stale one waiting for a remesh.
///
/// IT TAKES A `Mesh` AND NOT A `ThermalModel`, for the reason P17-DOF-001
/// gave: ADR-051 puts the stale-input gate at the boundary a SOLVE is
/// prepared at, and a numbering is combinatorics over node identity that
/// cannot produce a wrong engineering answer on its own. The later milestones
/// take a `ThermalModel` and a `ThermalDofMap` together, and the stamp
/// binding is what proves the two agree -- so numbering a bare mesh, which is
/// what the sparse-handle tests need, stays possible.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<ThermalDofMap> buildThermalDofMap(const meshing::Mesh& mesh);

/// The problem `buildThermalDofMap` would report, or none if it would succeed.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::optional<DofMapProblem>
thermalDofMapProblem(const meshing::Mesh& mesh) noexcept;

// ---------------------------------------------------------------------------
// ThermalConstraintMap
// ---------------------------------------------------------------------------

/// One prescribed temperature: which unknown, and at what absolute
/// temperature, in kelvin.
///
/// A VALUE, NOT A BIT. This is where the thermal model departs from P17's
/// `ConstraintSet`, which records only WHICH degrees of freedom are
/// prescribed because a structural restraint holds a displacement at zero. A
/// Dirichlet temperature is arbitrary, so the value is part of the
/// constraint, it is a `Temperature` and not a double, and it is in kelvin
/// because every temperature in BetterCAD is (there is no Celsius anywhere in
/// the tree; P18-DATA-001 proved that over the whole unit catalog).
///
/// Named by `ThermalDofIndex`, not by `NodeId`: after resolution the
/// constraint is a fact about the index space. The CAD face it came from and
/// the `NodeId` it passed through are P18-BC-001's attribution, kept there.
struct ThermalTemperatureConstraint {
    ThermalDofIndex dof;
    Temperature temperature;

    friend constexpr bool operator==(const ThermalTemperatureConstraint&,
                                     const ThermalTemperatureConstraint&) noexcept = default;
};

/// Why a set of prescribed temperatures is not usable.
enum class ConstraintProblem : std::uint8_t {
    /// A constraint names an unknown the numbering does not have. Never
    /// clamped and never skipped: a prescribed temperature the user asked for
    /// that silently did not apply leaves a model under-constrained in a way
    /// nothing reports.
    DofNotInMap,
    /// A prescribed value is not a finite absolute temperature: NaN, an
    /// infinity, or below 0 K. The one rule P18-DATA-001 wrote
    /// (`isAbsoluteTemperature`), applied here as well, so a value that could
    /// not enter the canonical boundary condition cannot enter the
    /// mathematical constraint either.
    TemperatureNotAbsolute,
    /// The same unknown is prescribed two DIFFERENT temperatures. Refused:
    /// not the first, not the last, not the mean. Two prescriptions of the
    /// SAME temperature are one constraint and are deduplicated, because the
    /// mathematical fact is the same either way; attributing it to two
    /// boundary conditions is P18-BC-001's business.
    ConflictingTemperature,
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(ConstraintProblem problem) noexcept;

/// The temperatures a model prescribes, by unknown.
///
/// ORDER-INDEPENDENT BY CONSTRUCTION. The input is sorted into ascending
/// `ThermalDofIndex` and exact duplicates are collapsed, so two callers
/// listing the same conditions in different orders build maps that compare
/// equal -- and the free numbering derived from either is identical. That is
/// a property of the representation, not of a sorting step a caller might
/// skip.
///
/// EXACT EQUALITY FOR "THE SAME TEMPERATURE". A prescribed temperature is
/// user intent; two values are the same constraint if they are the same
/// number, and there is no tolerance under which 300 K and 300.000001 K are
/// one prescription. Numerical solution tolerance is a different question
/// that lives with the solver.
///
/// BOUND TO ONE `ThermalDofMap`, by its `MeshStamp` and its `dofCount()`. A
/// constraint map outlives its usefulness the moment the mesh changes, and
/// `buildThermalFreeEquationMap` refuses a mismatched pair rather than
/// numbering against indices that mean something else.
class BETTERCAD_THERMAL_EXPORT ThermalConstraintMap {
public:
    /// Which mesh these indices belong to. Copied from the map they were
    /// validated against.
    [[nodiscard]] const meshing::MeshStamp& stamp() const noexcept { return stamp_; }

    /// How many unknowns the numbering these were validated against has.
    [[nodiscard]] ThermalDofIndex::ValueType dofCount() const noexcept { return dofCount_; }

    /// The prescribed temperatures, ascending by unknown and without repeats.
    [[nodiscard]] std::span<const ThermalTemperatureConstraint> constrained() const noexcept {
        return constrained_;
    }

    /// The number of UNIQUE prescribed unknowns. Two prescriptions of one
    /// unknown at one temperature count once.
    [[nodiscard]] std::size_t size() const noexcept { return constrained_.size(); }
    [[nodiscard]] bool isEmpty() const noexcept { return constrained_.empty(); }

    /// Whether @p dof is prescribed. A binary search.
    [[nodiscard]] bool isConstrained(ThermalDofIndex dof) const noexcept {
        return prescribedTemperature(dof).has_value();
    }

    /// The temperature @p dof is held at, or nullopt if it is free or is not
    /// an unknown of this map. NEVER an implicit zero: a free node has no
    /// prescribed temperature, not a prescribed temperature of 0 K.
    [[nodiscard]] std::optional<Temperature> prescribedTemperature(ThermalDofIndex dof) const noexcept;

    friend bool operator==(const ThermalConstraintMap&, const ThermalConstraintMap&) = default;

private:
    ThermalConstraintMap() = default;

    friend BETTERCAD_THERMAL_EXPORT Result<ThermalConstraintMap>
    buildThermalConstraintMap(const ThermalDofMap& map,
                              std::span<const ThermalTemperatureConstraint> prescribed);

    meshing::MeshStamp stamp_{};
    ThermalDofIndex::ValueType dofCount_ = 0;
    std::vector<ThermalTemperatureConstraint> constrained_{};
};

/// Validates @p prescribed against @p map and sorts it into a canonical map.
///
/// An EMPTY set is accepted and is not an error, and so is a set that
/// prescribes EVERY unknown. A conduction model with no prescribed
/// temperature is a real model whose conductivity matrix has a null mode, and
/// one with every temperature prescribed has nothing to solve; both are
/// representable here, because detecting whether a system is solvable is
/// P18-SOLVE-001's and P18-VALID-001's question, where the null mode is
/// visible. This milestone's job is to represent what was asked for
/// faithfully, including nothing and including everything.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<ThermalConstraintMap>
buildThermalConstraintMap(const ThermalDofMap& map,
                          std::span<const ThermalTemperatureConstraint> prescribed);

/// The problem `buildThermalConstraintMap` would report, or none.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::optional<ConstraintProblem>
thermalConstraintProblem(const ThermalDofMap& map,
                         std::span<const ThermalTemperatureConstraint> prescribed);

// ---------------------------------------------------------------------------
// ThermalFreeEquationMap
// ---------------------------------------------------------------------------

/// The unknowns of the reduced system, numbered compactly.
///
/// 0 ..< `freeCount()`, assigned to the free unknowns in ascending
/// `ThermalDofIndex` order. Compact and gap-free, so the reduced system has
/// exactly `freeCount()` rows and an assembler can size its storage from that
/// one number:
///
/// ```text
/// unknowns 0 1 2 3 4, prescribed 1 and 4:
///     0 -> row 0,   2 -> row 1,   3 -> row 2        freeCount 3
/// ```
///
/// `isFree` AND `isConstrained` CANNOT DISAGREE, because only the free list is
/// stored: an unknown of this map is free exactly when it has a row, and
/// constrained exactly when it is in range and does not.
///
/// BOUND TO THE PAIR IT WAS BUILT FROM. It carries the mesh stamp and the
/// unknown count, and `agreesWith` re-derives whether a given constraint map
/// is exactly the complement of its free list -- so a caller holding an old
/// free map and a new constraint map gets a refusal, not a wrong row.
///
/// WHAT STALES IT. A remesh (the stamp), and a change to WHICH unknowns are
/// prescribed. A change to the prescribed VALUES alone leaves the free
/// topology identical, and `agreesWith` says so; but the reduced right-hand
/// side `Qf - Kfc Tc` changes with `Tc`, so no assembled or solved system may
/// be called current on the strength of a matching free map alone. That
/// warning is P18-ASSEMBLY-001's and P18-SOLVE-001's to honour, and it is
/// recorded here where the map is defined.
class BETTERCAD_THERMAL_EXPORT ThermalFreeEquationMap {
public:
    /// Which mesh this numbering belongs to.
    [[nodiscard]] const meshing::MeshStamp& stamp() const noexcept { return stamp_; }

    /// Total unknowns of the model: `freeCount() + constrainedCount()`.
    [[nodiscard]] ThermalDofIndex::ValueType dofCount() const noexcept { return dofCount_; }

    /// Rows of the reduced system.
    [[nodiscard]] std::size_t freeCount() const noexcept { return freeDofs_.size(); }

    /// Prescribed unknowns. Derived from the two counts, never stored
    /// separately.
    [[nodiscard]] ThermalDofIndex::ValueType constrainedCount() const noexcept {
        return dofCount_ - static_cast<ThermalDofIndex::ValueType>(freeDofs_.size());
    }

    /// The free unknowns in row order: entry e is the unknown of row e.
    /// Ascending.
    [[nodiscard]] std::span<const ThermalDofIndex> freeDofs() const noexcept { return freeDofs_; }

    /// The row @p dof is solved in, or nullopt if it is prescribed or is not
    /// an unknown of this map. A constrained unknown has NO row -- not row 0.
    [[nodiscard]] std::optional<ThermalFreeEquationIndex> equationOf(ThermalDofIndex dof) const noexcept;

    /// The unknown row @p equation solves for, or nullopt if there is no such
    /// row.
    [[nodiscard]] std::optional<ThermalDofIndex> dofOf(ThermalFreeEquationIndex equation) const noexcept;

    [[nodiscard]] bool isFree(ThermalDofIndex dof) const noexcept { return equationOf(dof).has_value(); }

    /// Whether @p dof is an unknown of this map that is not free. False for an
    /// unknown this map does not have -- an unknown of another mesh is not
    /// "constrained" here, it is absent.
    [[nodiscard]] bool isConstrained(ThermalDofIndex dof) const noexcept {
        return contains(dof) && !isFree(dof);
    }

    /// Whether @p dof is one of this map's 0 ..< `dofCount()`.
    [[nodiscard]] bool contains(ThermalDofIndex dof) const noexcept { return dof.value() < dofCount_; }

    /// Whether this free numbering was derived from @p map: same stamp, same
    /// unknown count.
    [[nodiscard]] bool agreesWith(const ThermalDofMap& map) const noexcept;

    /// Whether this free numbering is exactly the complement of
    /// @p constraints: same stamp, same unknown count, and every prescribed
    /// unknown absent from the free list with nothing else absent. The
    /// question a consumer asks before pairing an old free map with a
    /// constraint map that may have changed.
    [[nodiscard]] bool agreesWith(const ThermalConstraintMap& constraints) const noexcept;

    friend bool operator==(const ThermalFreeEquationMap&, const ThermalFreeEquationMap&) = default;

private:
    ThermalFreeEquationMap() = default;

    friend BETTERCAD_THERMAL_EXPORT Result<ThermalFreeEquationMap>
    buildThermalFreeEquationMap(const ThermalDofMap& map, const ThermalConstraintMap& constraints);

    meshing::MeshStamp stamp_{};
    ThermalDofIndex::ValueType dofCount_ = 0;
    std::vector<ThermalDofIndex> freeDofs_{};
};

/// Numbers the unknowns of @p map that @p constraints does not prescribe.
///
/// Fails with FailedPrecondition if the two were not built against the same
/// mesh -- same stamp AND same unknown count, the second because two snapshots
/// of one builder share a stamp. That is the check which makes a remesh safe:
/// the pair is the only place the numbering and the constraints meet, so one
/// comparison here is cheaper than a stamp check at every assembly site, and
/// it cannot be forgotten because there is no other way to obtain a
/// `ThermalFreeEquationMap`.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<ThermalFreeEquationMap>
buildThermalFreeEquationMap(const ThermalDofMap& map, const ThermalConstraintMap& constraints);

/// The full nodal temperature vector from the reduced solution and the
/// prescribed temperatures: entry d is `free[equationOf(d)]` for a free
/// unknown and the prescribed temperature for a constrained one.
///
/// Index bookkeeping only -- no physics -- and in this milestone because the
/// two maps must be shown to encode enough to do it. Fails with
/// FailedPrecondition if @p equations does not agree with @p constraints, and
/// with InvalidArgument if @p free is not exactly `freeCount()` long. The
/// output is parallel to `ThermalDofMap::nodes()` and therefore to
/// `mesh.nodes()`, which is the order `ThermalResult::create` takes.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<std::vector<Temperature>>
reconstructTemperatures(const ThermalFreeEquationMap& equations, const ThermalConstraintMap& constraints,
                        std::span<const Temperature> free);

} // namespace bettercad::thermal
