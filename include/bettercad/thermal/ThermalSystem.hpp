#pragma once

// P18-ASSEMBLY-001 -- the global thermal system `K T = Q` and `C dT/dt + K T = Q`.
//
// WHAT THIS IS. The assembled, UNCONSTRAINED global system of the current linear
// Tet4 conduction model: a sparse conductance matrix K in W/K, a sparse capacity
// matrix C in J/K (transient only), a dense heat-input vector Q in W, and the exact
// input state they were computed from. Derived and disposable: nothing here is
// persisted, undone or canonical.
//
// ASSEMBLY IS SCATTER, NOT PHYSICS. The formulation lives upstream and is consumed,
// never repeated:
//
// ```text
// Tet conduction Ke, capacity Ce        P18-ELEM-001   computeTet4Conduction / Capacity
// surface q'' and volume q''' to nodal  P18-LOAD-001   PreparedThermalLoads::heatInput
// Robin Kconv and Qconv                 P18-BC-001     PreparedConvectionBoundary
// Dirichlet constraints                 P18-BC-001     (not touched here)
// Dirichlet elimination, the solve      P18-SOLVE-001  (not here)
// ```
//
// so there is no `V k G^T G`, no `rho cp V / 20`, no `q'' A / 3`, no `q''' V / 4`
// and no `h A / 12` in this module. What is here is the composition:
//
// ```text
// K  =  sum over Tet4   Ke        +  sum over convection facets   Kconv
// C  =  sum over Tet4   Ce
// Q  =  the prepared heat input   +  sum over convection facets   Qconv
// ```
//
// and the order, the indices, the validation and the provenance.
//
// EVERY ROW COMES FROM `ThermalDofMap`. `NodeId` is never an index, `ElementId` is
// never a row, and the dimension is `ThermalDofMap::dofCount()`, not the largest
// handle. A sparse matrix of `max(NodeId) + 1` rows would be four billion rows for
// a three-node mesh.
//
// THE FULL SYSTEM, BEFORE ANY ELIMINATION. Prescribed-temperature unknowns stay in
// the matrices: `K_ff T_f = Q_f - K_fc T_c` needs `K_fc`, and a reaction heat needs
// the constrained rows. A Dirichlet value is applied by P18-SOLVE-001, exactly,
// and by nothing here. In particular there is NO penalty (`K_ii += big`), no
// epsilon on a diagonal, no spring, no pinned unknown, no symmetrisation, no
// threshold under which an entry is dropped, and no `C / dt`: a conduction-only
// connected K is SINGULAR with the constant mode in its null space, and that is
// correct and is tested.
//
// WHAT IS OWNED, STATED ONCE:
//   a pure-Neumann K is singular       valid here; the solver detects it
//   a zero Q                           valid physics (a Dirichlet-driven problem)
//   a steady material with no rho/cp   valid; there is then no C at all
//   a transient material with no rho   REFUSED, never a zero C
//
// SERIAL AND DETERMINISTIC. Many elements write the same entry, so a
// schedule-dependent summation order would make the last bits depend on the
// machine. The orders are frozen and none is this module's invention:
//
// ```text
// elements    mesh.tetrahedra(), which P16 guarantees ascending ElementId
// entries     local row 0..3, then local column 0..3, in the element's own node order
// facets      PreparedThermalBoundary::convections() in condition order, then each
//             condition's facets in P16's ascending facet order
// heat input  the prepared vector, in unknown order, then the convection sources
// ```
//
// Nothing is an unordered container, and local degrees of freedom are never sorted:
// the local matrix is in the element's order, so the rows must be as well.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/units/Dimension.hpp>
#include <bettercad/core/units/Quantity.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/meshing/GeometryPreparation.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/thermal/Export.hpp>
#include <bettercad/thermal/ThermalAnalysis.hpp>
#include <bettercad/thermal/ThermalAnalysisObject.hpp>
#include <bettercad/thermal/ThermalBoundary.hpp>
#include <bettercad/thermal/ThermalData.hpp>
#include <bettercad/thermal/ThermalDof.hpp>
#include <bettercad/thermal/ThermalElement.hpp>
#include <bettercad/thermal/ThermalLoadVector.hpp>
#include <bettercad/thermal/ThermalMaterial.hpp>
#include <bettercad/thermal/ThermalSparse.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace bettercad::thermal {

// ---------------------------------------------------------------------------
// Units, checked by the compiler
// ---------------------------------------------------------------------------

/// ```text
///     K T          (W/K) . K              =  W
///     C dT/dt      (J/K) . (K/s)          =  W
///     Q            W
/// ```
/// so both `K T = Q` and `C dT/dt + K T = Q` are equations between powers.
static_assert(decltype(ThermalConductance{} * Temperature{})::dimension == dimensions::power);
static_assert(decltype(HeatCapacity{} * (Temperature{} / Time{}))::dimension == dimensions::power);
static_assert(ThermalConductance::dimension != HeatCapacity::dimension);
static_assert(!std::is_same_v<ConductanceMatrix, CapacityMatrix>);

/// The sparse index is at least as wide as an unknown's, so no row or entry count
/// derived from one can narrow. A `NodeId` is 32-bit and a mesh's handles are
/// strictly increasing, so a model holds at most 2^32 - 1 unknowns and a row index
/// needs 32 bits; the CSR uses 64 for the entry counts, which run at forty times
/// the unknowns.
static_assert(sizeof(ThermalSparseMatrix::Index) >= sizeof(ThermalDofIndex::ValueType));
static_assert(static_cast<ThermalSparseMatrix::Index>(std::numeric_limits<meshing::NodeId::ValueType>::max()) *
                  64 <
              std::numeric_limits<ThermalSparseMatrix::Index>::max() / 2);

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

/// Why the inputs cannot be assembled into a global system.
///
/// The model-level values are checked BEFORE any element is visited, so a stale
/// input is reported as one and never as a numerical symptom; the order is the
/// enum's. EVERY VALUE IS REACHABLE except `InconsistentSystem`, which is a
/// self-check of the assembler's own arrays and fires only if the scatter itself
/// is wrong (a mutation test makes it fire).
enum class AssemblyProblem : std::uint8_t {
    /// The numbering does not describe the model's current mesh: a different
    /// generation, or a different body.
    DofMapMismatch,
    /// The prepared loads were built for a different mesh, body, geometry or load
    /// list than the ones presented. Counts and ids that happen to agree do not
    /// help: the source is a value, not a size.
    LoadSourceMismatch,
    /// The prepared boundary conditions likewise.
    BoundarySourceMismatch,
    /// The material cannot be resolved for this mode: nothing assigned, or a
    /// property the mode requires is missing. A transient analysis with no density
    /// or no specific heat is refused here and never assembled with a zero C.
    MaterialUnavailable,
    /// The material the model was validated against is not the one the document
    /// holds now.
    MaterialSourceMismatch,
    /// The mesh carries no tetrahedron, so K would be structurally empty.
    MeshHasNoElements,
    /// The heat-input vector is not `Ndof` long.
    LoadVectorMismatch,
    /// A convection facet names an unknown outside the numbering.
    BoundaryDofOutOfRange,
    /// P18-ELEM-001 refused an element: degenerate, inverted, non-finite, or an
    /// unusable material. Propagated with the `ElementId`, never skipped: an
    /// assembly that quietly omitted one tetrahedron would produce a body with a
    /// hole in it.
    ElementRejected,
    /// An assembled coefficient or heat input is not finite: a sum overflowed.
    NonFiniteSystem,
    /// The assembled arrays failed their own validation (layout or symmetry).
    InconsistentSystem,
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(AssemblyProblem problem) noexcept;

/// A refusal with its classification.
struct AssemblyFailure {
    AssemblyProblem problem = AssemblyProblem::InconsistentSystem;
    Error error{};
};

// ---------------------------------------------------------------------------
// Provenance
// ---------------------------------------------------------------------------

/// The identity of everything an assembled system was computed from.
///
/// A VALUE, NOT A HASH: no pointer, no address, no timestamp. Equality is
/// field-complete by `= default`, so a dependency added here without being compared
/// is not possible, and `staleReasons` can say WHICH moved.
///
/// ```text
/// body        which solid
/// mesh        WHICH MESH AND WHICH GENERATION: a remesh with the same counts and
///             the same ids has a different stamp
/// nodeCount   a stamp identifies the BUILDER, not the snapshot
/// geometry    the geometry revision the mapping resolved against
/// mode        steady or transient: only the latter has a C
/// material    which material, and its revision: an edit to k (or, for a transient,
///             rho or cp) is a different system
/// loads       the canonical loads it was prepared from, by value
/// convections the canonical CONVECTION conditions, by value
/// ```
///
/// WHAT IS DELIBERATELY ABSENT, because the matrices do not depend on it:
///
/// ```text
/// prescribed temperatures   they never enter K, C or Q here. A T_bar edit changes
///                           the constraint map (P18-BC-001's, with its own source),
///                           not this system. Only the CONVECTION conditions are
///                           listed, in their order
/// solver settings           tolerance and pivot floor do not enter K, C or Q
/// time step, end time       C / dt is not formed here (P18-TRANSIENT-001)
/// initial condition         likewise
/// the analysis revision     it moves on ANY edit, and would stale this system for
///                           a tolerance edit that cannot change it
/// ```
///
/// Material identity is a `MaterialId` and a revision, as P17's `AssemblySource`
/// carries: a whole-record policy, so an edit to any property of the material stales
/// the system, which is conservative and never wrong.
struct ThermalSystemSource {
    ObjectId body{};
    meshing::MeshStamp mesh{};
    std::size_t nodeCount = 0;
    meshing::GeometryRevision geometry{};
    ThermalAnalysisMode mode = ThermalAnalysisMode::Steady;
    MaterialId material{};
    std::uint64_t materialRevision = 0;
    std::vector<ThermalLoad> loads{};
    std::vector<ThermalBoundaryCondition> convections{};

    friend bool operator==(const ThermalSystemSource&, const ThermalSystemSource&) = default;
};

/// Which part of a system's source no longer matches.
enum class SystemStaleReason : std::uint8_t {
    Body,
    Mesh,
    Geometry,
    Mode,
    Material,
    Loads,
    Convections,
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(SystemStaleReason reason) noexcept;

/// Every field of @p assembled that differs from @p current, in the enum's order.
/// Empty when the two are equal.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::vector<SystemStaleReason>
staleReasons(const ThermalSystemSource& assembled, const ThermalSystemSource& current);

/// The source a system for @p mode would be assembled against on @p model, right
/// now. Resolves the material through P18-MAT-001, so it can fail with that
/// milestone's diagnostic.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<ThermalSystemSource>
currentThermalSystemSource(const Document& document, const ThermalModel& model, ThermalAnalysisMode mode,
                           std::span<const ThermalLoad> loads,
                           std::span<const ThermalBoundaryCondition> conditions);

// ---------------------------------------------------------------------------
// ThermalSystem
// ---------------------------------------------------------------------------

struct ThermalAssemblyData;

/// The assembled global system.
///
/// POSSESSION IS THE EVIDENCE that every input was current and mutually compatible,
/// every element was valid, every coefficient is finite, K and C are symmetric, and
/// all of it was assembled against the mesh `source()` names. There is no public
/// constructor and two friends, so a half-built system is unrepresentable.
///
/// DERIVED AND DISPOSABLE. Rebuilt after a remesh or any edit that touches its
/// source; never persisted, never a member of anything canonical.
class BETTERCAD_THERMAL_EXPORT ThermalSystem {
public:
    /// `Ndof`: the dimension of K, of C when present, and of Q.
    [[nodiscard]] std::size_t dofCount() const noexcept { return heat_.size(); }

    /// `K`, in W/K: volume conduction plus the Robin boundary conductance.
    [[nodiscard]] const ConductanceMatrix& conductance() const noexcept { return conductance_; }

    /// `C`, in J/K: volume capacity only. Present exactly when the system was
    /// assembled for a transient analysis.
    [[nodiscard]] const std::optional<CapacityMatrix>& capacity() const noexcept { return capacity_; }

    /// `Q`, in W: the prepared heat input plus the convection ambient source.
    [[nodiscard]] const ThermalHeatVector& heat() const noexcept { return heat_; }

    /// What this was assembled from.
    [[nodiscard]] const ThermalSystemSource& source() const noexcept { return source_; }

    /// The mesh the unknowns belong to.
    [[nodiscard]] const meshing::MeshStamp& mesh() const noexcept { return source_.mesh; }

    /// Whether this system describes @p map: the stamp AND the count.
    [[nodiscard]] bool describes(const ThermalDofMap& map) const noexcept {
        return map.stamp() == source_.mesh && map.dofCount() == heat_.size();
    }
    /// Whether it describes @p mesh: the mesh owns the stamp AND the node count agrees.
    [[nodiscard]] bool describes(const meshing::Mesh& mesh) const noexcept {
        return mesh.owns(source_.mesh) && mesh.nodes().size() == source_.nodeCount;
    }

    /// The element order the matrices were summed in. A NUMERICAL CONTRACT and not a
    /// diagnostic: changing it changes the last bits of a sum.
    [[nodiscard]] std::span<const meshing::ElementId> elementOrder() const noexcept { return elements_; }

    /// How many convection facets were scattered into K and Q.
    [[nodiscard]] std::size_t convectionFacetCount() const noexcept { return convectionFacets_; }

    friend bool operator==(const ThermalSystem&, const ThermalSystem&) = default;

private:
    ThermalSystem(ConductanceMatrix conductance, std::optional<CapacityMatrix> capacity, ThermalHeatVector heat,
                  ThermalSystemSource source, std::vector<meshing::ElementId> elements,
                  std::size_t convectionFacets)
        : conductance_(std::move(conductance))
        , capacity_(std::move(capacity))
        , heat_(std::move(heat))
        , source_(std::move(source))
        , elements_(std::move(elements))
        , convectionFacets_(convectionFacets) {}

    friend BETTERCAD_THERMAL_EXPORT std::expected<ThermalSystem, AssemblyFailure>
    assembleThermalSystemFrom(const ThermalAssemblyData& data, ThermalSystemSource source);

    ConductanceMatrix conductance_;
    std::optional<CapacityMatrix> capacity_;
    ThermalHeatVector heat_;
    ThermalSystemSource source_;
    std::vector<meshing::ElementId> elements_;
    std::size_t convectionFacets_ = 0;
};

// ---------------------------------------------------------------------------
// The seam
// ---------------------------------------------------------------------------

/// Everything the matrices are scattered from, as non-owning views.
///
/// THE SEAM THE TESTS CALL DIRECTLY, with a hand-built mesh: it needs no
/// `ThermalModel` and no document, so a single tetrahedron, two tetrahedra, a
/// sparse `NodeId`, a numbering for another mesh, a heat vector of the wrong length
/// or a convection facet pointing outside the numbering can be presented to it.
/// `assembleThermalSystem` is the same scatter driven by the model-level checks.
struct ThermalAssemblyData {
    const meshing::Mesh& mesh;
    const ThermalDofMap& map;
    ThermalConductivity conductivity;
    /// Engaged for a transient analysis, empty for a steady one. Empty means "no C
    /// is requested", never "C is zero".
    std::optional<ThermalCapacity> capacity;
    /// The prepared per-unknown heat input, `map.dofCount()` long.
    std::span<const Power> heatInput;
    /// The prepared convection conditions, each facet's terms already bound to
    /// unknowns.
    std::span<const PreparedConvectionBoundary> convections;
};

/// Scatters @p data into a system carrying @p source.
///
/// ATOMIC. Everything is accumulated into working state and validated before
/// anything is published; a failure returns no system.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::expected<ThermalSystem, AssemblyFailure>
assembleThermalSystemFrom(const ThermalAssemblyData& data, ThermalSystemSource source);

// ---------------------------------------------------------------------------
// The model-level entry
// ---------------------------------------------------------------------------

/// Assembles the global system of @p model for @p mode.
///
/// THE CHECKS, IN THE ORDER OF `AssemblyProblem`, ALL BEFORE THE FIRST ELEMENT:
/// the numbering describes the model's mesh; the prepared loads are current for
/// @p loads on this model and numbering; the prepared boundary is current for
/// @p conditions likewise; the material resolves for @p mode through P18-MAT-001
/// and is the one the model was validated against.
///
/// IT TAKES A `ThermalModel`, which answers the stale-geometry and stale-mesh
/// questions without a check here: possession proves both current. It takes the
/// document to resolve the material afresh every time, because a material is
/// derived and never cached; a conductivity edit after the model was built is
/// therefore refused, and a density or specific-heat edit is simply read.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<ThermalSystem>
assembleThermalSystem(const Document& document, const ThermalModel& model, ThermalAnalysisMode mode,
                      const ThermalDofMap& map, const PreparedThermalLoads& preparedLoads,
                      std::span<const ThermalLoad> loads, const PreparedThermalBoundary& preparedBoundary,
                      std::span<const ThermalBoundaryCondition> conditions);

/// The loads, conditions and mode of @p definition, assembled. A convenience: it
/// reads `definition.mode`, `definition.loads` and `definition.boundaryConditions`
/// and nothing else, so a solver, initial-condition or time-step edit cannot change
/// the answer.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<ThermalSystem>
assembleThermalSystem(const Document& document, const ThermalModel& model, const ThermalDofMap& map,
                      const PreparedThermalLoads& preparedLoads, const PreparedThermalBoundary& preparedBoundary,
                      const ThermalAnalysisDefinition& definition);

/// The full refusal `assembleThermalSystem` would report, or none if it would
/// succeed. The same checks in the same order.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::optional<AssemblyFailure>
thermalAssemblyFailure(const Document& document, const ThermalModel& model, ThermalAnalysisMode mode,
                       const ThermalDofMap& map, const PreparedThermalLoads& preparedLoads,
                       std::span<const ThermalLoad> loads, const PreparedThermalBoundary& preparedBoundary,
                       std::span<const ThermalBoundaryCondition> conditions);

/// Whether @p system still describes @p model, @p mode, @p loads and
/// @p conditions. False if the material no longer resolves.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT bool
isCurrent(const ThermalSystem& system, const Document& document, const ThermalModel& model,
          ThermalAnalysisMode mode, std::span<const ThermalLoad> loads,
          std::span<const ThermalBoundaryCondition> conditions);

/// `Ok` if @p system is current, otherwise a `FailedPrecondition` naming each
/// reason that moved (or the material diagnostic).
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<void>
requireCurrent(const ThermalSystem& system, const Document& document, const ThermalModel& model,
               ThermalAnalysisMode mode, std::span<const ThermalLoad> loads,
               std::span<const ThermalBoundaryCondition> conditions);

} // namespace bettercad::thermal
