#pragma once

// P18-POST-001 -- recovery of the derived thermal fields from an accepted nodal
// temperature solution: the temperature, the element temperature gradient, the
// conductive heat-flux vector and its magnitude.
//
// ```text
//     grad(T) = G Te            per Tet4, K/m,  [dT/dx, dT/dy, dT/dz]
//     q       = -k grad(T)      per Tet4, W/m^2, [qx, qy, qz]       FOURIER'S LAW
//     |q|     = sqrt(qx^2 + qy^2 + qz^2)          W/m^2, >= 0
// ```
//
// EVERYTHING HERE IS DERIVED. A post result is reproducible from the canonical
// analysis, the mesh and the material; it is never persisted as authority, never
// a member of a command, and never a second source of the temperature it was
// recovered from. What makes that safe is that it carries the identity of the
// solution it was recovered from and answers "I am stale" through that
// solution's own currentness, not through a rule of its own.
//
// OWNERSHIP, STATED ONCE.
//
// ```text
//     Tet4 geometry, G, grad(T) = G Te        P18-ELEM-001  (Tet4Geometry::gradientFrom)
//     the nodal temperature                   P18-SOLVE-001 / P18-TRANSIENT-001
//     the conductivity k                      P15 through P18-MAT-001
//     the Fourier law q = -k grad(T), |q|     THIS MILESTONE
//     the gather of Te through the numbering  THIS MILESTONE, with P18-DOF-001's elementDofs
//     colour, range, legend, component choice P18-VIZ-001 (a view of this, never a recomputation of it)
//     heat balance, reactions, boundary flux  P18-BALANCE-001
// ```
//
// THIS FILE DOES NOT REIMPLEMENT G. The gradient of an element is
// `Tet4Geometry::gradientFrom(Te)`, the "shared gradient-recovery path"
// P18-ELEM-001 published for exactly this use. The only physics written here is
// the one line `q = -k grad(T)`.
//
// THE SIGN IS PART OF THE TYPE'S CONTRACT. `HeatFlux3` points where heat FLOWS,
// toward decreasing temperature, so `q . grad(T) = -k |grad(T)|^2 <= 0`. The
// recovered q is a physical vector field. It is NOT the applied scalar surface
// flux of P18-LOAD-001 (whose positive sign means heat INTO the body through a
// face): the two meet at a boundary through `q . n`, which is P18-BALANCE-001's
// and is not formed here.
//
// WHAT q IS NOT. It is the conductive flux inside the solid, nothing else: no
// convection term `h (T - Tinf)` (a boundary heat transfer), no volumetric
// generation (it changes the solved T, not Fourier's law), and no storage term
// `C dT/dt` (storage is not a spatial flux). A body at a uniform temperature that
// is heating has `grad(T) = 0` and therefore `q = 0` while `dT/dt != 0`.
//
// UNSMOOTHED, ELEMENT-WISE, DELIBERATELY. A linear tetrahedron has a constant
// gradient, so grad(T) and q are constant within each element and one value per
// element is the engineering result. There is no nodal averaging, no
// superconvergent patch recovery and no least-squares nodal flux. A smoothed field
// is a visualisation representation (P18-VIZ-001) and may not replace this.
//
// THE TEMPERATURE STAYS IN KELVIN. The recovered nodal temperature is the solver's
// value, bit for bit, in K. A Celsius offset cancels in every gradient, so a flux
// test cannot see one; the temperature is therefore checked on its own.
// "Temperature magnitude/value" is the scalar value T itself -- there is no vector
// magnitude of a scalar and none is applied.
//
// KEYING. The nodal temperatures are parallel to `ThermalDofIndex`, and a node is
// found through the numbering, never by `NodeId::value()`. The element arrays are
// parallel to `mesh.tetrahedra()` (P16's deterministic order), and an element is
// found through `ordinalOf(ElementId)` -- a binary search of the stored ids, never
// the raw id as an index. Storage is O(nodes + elements) however sparse the ids.
//
// ATOMIC. The whole result is built in temporary storage and published only when
// every element of every state has passed: one invalid element, one non-finite
// value, fails the whole recovery and leaves nothing behind.
//
// WHAT THIS IS NOT.
//
//     no nodal heat-flux smoothing   P18-VIZ-001 may define a display representation
//     no boundary flux, no q . n     P18-BALANCE-001
//     no heat balance, reactions     P18-BALANCE-001
//     no colour, range, clipping     P18-VIZ-001
//     no unit conversion             SI throughout; K, K/m, W/m^2
//     no anisotropy, k(T)            ADR-049's refused scope
//     no persistence                 P18-PERSIST-001 decides what, if anything, is stored
//     no time derivative             storage is not recovered

#include <bettercad/core/Error.hpp>
#include <bettercad/core/units/Dimension.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/thermal/Export.hpp>
#include <bettercad/thermal/ThermalAnalysis.hpp>
#include <bettercad/thermal/ThermalBoundary.hpp>
#include <bettercad/thermal/ThermalData.hpp>
#include <bettercad/thermal/ThermalDof.hpp>
#include <bettercad/thermal/ThermalElement.hpp>
#include <bettercad/thermal/ThermalSolve.hpp>
#include <bettercad/thermal/ThermalSystem.hpp>
#include <bettercad/thermal/ThermalTransient.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
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
///     k grad(T)     [W/(m K)] [K/m]  =  W/m^2
/// ```
/// so Fourier's law is an equation between heat fluxes, and the product is
/// exactly `HeatFlux` (the same type, not a convertible one).
static_assert(std::is_same_v<decltype(ThermalConductivity{} * TemperatureGradient{}), HeatFlux>,
              "k grad(T) must come out in W/m^2");
static_assert(HeatFlux::dimension == dimensions::power / dimensions::area);
static_assert(TemperatureGradient::dimension == dimensions::temperature / dimensions::length);

// ---------------------------------------------------------------------------
// Field identifiers and metadata
// ---------------------------------------------------------------------------

/// The derived fields of a post result. STABLE: a CLI will print them and a
/// viewer will select by them; none is a string to be parsed.
enum class ThermalPostField : std::uint8_t {
    /// The nodal absolute temperature, K. A scalar: its "value" is T itself.
    Temperature,
    /// grad(T) of an element, K/m, `[dT/dx, dT/dy, dT/dz]`.
    TemperatureGradient,
    /// The conductive heat flux of an element, W/m^2, `[qx, qy, qz]`, `q = -k grad(T)`.
    HeatFlux,
    /// `|q|` of an element, W/m^2, `>= 0`.
    HeatFluxMagnitude,
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(ThermalPostField field) noexcept;

/// Where a field's values live.
enum class FieldLocation : std::uint8_t {
    /// One value per node, parallel to `ThermalDofIndex`.
    Node,
    /// One value per tetrahedron, parallel to `mesh.tetrahedra()`.
    Element,
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(FieldLocation location) noexcept;

/// What a consumer needs to read a field without recomputing it or guessing: its
/// location, its components, its dimension and the unit symbol to print.
///
/// The DIMENSION is the authority (it is the type's own, checked below); the symbol
/// is the SI text for a report, and is not parsed anywhere.
struct ThermalFieldDescriptor {
    ThermalPostField field = ThermalPostField::Temperature;
    FieldLocation location = FieldLocation::Node;
    /// 1 for a scalar, 3 for a vector.
    std::size_t componentCount = 1;
    /// The component names in storage order; `componentNames[0]` only for a scalar.
    std::array<std::string_view, 3> componentNames{};
    Dimension dimension{};
    std::string_view unit{};

    friend bool operator==(const ThermalFieldDescriptor&, const ThermalFieldDescriptor&) = default;
};

/// The descriptor of @p field.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT ThermalFieldDescriptor descriptorOf(ThermalPostField field) noexcept;

/// All four fields, in the order the enumeration lists them.
inline constexpr std::array<ThermalPostField, 4> kThermalPostFields{
    ThermalPostField::Temperature, ThermalPostField::TemperatureGradient, ThermalPostField::HeatFlux,
    ThermalPostField::HeatFluxMagnitude};

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

/// Why a recovery was refused. STABLE NAMES. The checks run in this order: the
/// ones about the INPUTS before the element loop, then the ones the loop found.
enum class PostProblem : std::uint8_t {
    /// The conductivity is not finite and positive. No `abs`, no `max(k, eps)`.
    ThermalConductivityInvalid,
    /// The solution no longer describes the model, loads, conditions, material or
    /// mesh it was computed from (`stale`), or belongs to another mesh generation
    /// than the one presented. Counts and ids that happen to agree do not help.
    PostSourceMismatch,
    /// The numbering does not describe the mesh or the solution presented.
    NumberingMismatch,
    /// The material cannot be resolved for the solution's mode, or the model
    /// carries a conductivity that is not the document's now (the model is older
    /// than a material edit).
    MaterialMismatch,
    /// The temperature vector is not `Ndof` long.
    TemperatureCardinalityMismatch,
    /// A nodal temperature is NaN or infinite.
    NonFiniteTemperatureResult,
    /// A nodal temperature is below 0 K. The one absolute-temperature rule
    /// (`isAbsoluteTemperature`): the policy the steady solve left to this milestone.
    TemperatureBelowAbsoluteZero,
    /// P18-ELEM-001 refused an element: degenerate, inverted or non-finite. NEVER
    /// skipped and never reordered: a recovery that omitted one tetrahedron would
    /// publish a field with a hole in it.
    InvalidTetGeometry,
    /// `G Te` is not finite.
    NonFiniteTemperatureGradient,
    /// `-k grad(T)` is not finite (an overflow).
    NonFiniteHeatFlux,
    /// `|q|` is not finite although its components were.
    NonFiniteHeatFluxMagnitude,
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(PostProblem problem) noexcept;

/// A refusal with its classification and where it was found.
struct PostFailure {
    PostProblem problem = PostProblem::PostSourceMismatch;
    Error error{};
    /// The tetrahedron that failed, for the element-wise problems.
    std::optional<meshing::ElementId> element{};
    /// The state (0-based) of a transient history that failed; 0 for a steady one.
    std::size_t state = 0;
};

// ---------------------------------------------------------------------------
// The element kernel
// ---------------------------------------------------------------------------

/// The element fields of ONE temperature state, parallel to `mesh.tetrahedra()`.
///
/// DENSE, ONE VALUE PER TETRAHEDRON, in P16's element order. `gradients[i]` is
/// the gradient of `mesh.tetrahedra()[i]`: an ordinal, not an `ElementId`.
struct ThermalElementFields {
    /// grad(T), K/m: `[dT/dx, dT/dy, dT/dz]`.
    std::vector<TemperatureGradient3> gradients{};
    /// `q = -k grad(T)`, W/m^2: `[qx, qy, qz]`.
    std::vector<HeatFlux3> heatFluxes{};
    /// `|q|`, W/m^2, computed from the components above.
    std::vector<HeatFlux> heatFluxMagnitudes{};

    friend bool operator==(const ThermalElementFields&, const ThermalElementFields&) = default;
};

/// Recovers grad(T), q and |q| of every tetrahedron of @p mesh from the nodal
/// temperatures @p temperatures, which are parallel to @p map.
///
/// THE ALGEBRAIC SEAM, PUBLIC DELIBERATELY: a real mesh cannot pose a field whose
/// every tetrahedron has an independently known gradient, so the patch tests, the
/// axis-aligned fields, the rotation and scaling tests and the arbitrary-field
/// oracle need a way in that has no solver and no document. It is the SAME code the
/// source-bound entry points run.
///
/// ```text
/// 1  k finite and positive
/// 2  the numbering describes the mesh; the vector is Ndof long
/// 3  every temperature finite and >= 0 K                       (before any element)
/// 4  per tetrahedron, in mesh order:
///        Te = [T(n1) T(n2) T(n3) T(n4)]      in the element's OWN node order, via elementDofs
///        geometry = computeTet4Geometry(corners)              refused, never skipped
///        grad = geometry.gradientFrom(Te)                     P18-ELEM's G, not rewritten here
///        q = -k grad ; |q|                                    finite, or the whole recovery fails
/// ```
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::expected<ThermalElementFields, PostFailure>
recoverThermalElementFields(const meshing::Mesh& mesh, const ThermalDofMap& map, ThermalConductivity conductivity,
                            std::span<const Temperature> temperatures);

// ---------------------------------------------------------------------------
// Provenance
// ---------------------------------------------------------------------------

/// What a post result was recovered from: the solution's own source, by value.
///
/// ```text
/// mode          steady or transient
/// system        the assembled system's source: body, mesh stamp, node count, geometry, material AND its
///               revision (so a conductivity edit is a different input), loads, convection conditions
/// temperatures  the canonical prescribed-temperature conditions
/// settings      the solver settings
/// initial       the initial temperature          (transient only)
/// transient     time step, end time, output      (transient only)
/// ```
///
/// There is NO post setting. Which component a viewer shows, a colour map or a legend
/// range is a view of the result and never a field of its source, so editing one
/// cannot stale it.
struct ThermalPostSource {
    ThermalAnalysisMode mode = ThermalAnalysisMode::Steady;
    ThermalSystemSource system{};
    std::vector<ThermalBoundaryCondition> temperatures{};
    ThermalSolverSettings settings{};
    std::optional<UniformInitialTemperature> initial{};
    std::optional<TransientSettings> transient{};

    friend bool operator==(const ThermalPostSource&, const ThermalPostSource&) = default;
};

/// The source of a post result recovered from @p source.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT ThermalPostSource postSourceOf(const SteadySolutionSource& source);
[[nodiscard]] BETTERCAD_THERMAL_EXPORT ThermalPostSource postSourceOf(const TransientSolutionSource& source);

/// Every field of @p recovered that differs from @p current, in the enum's order.
/// Reuses the transient solve's reasons (a superset of the steady ones): the
/// comparison is that solve's own, not a second one.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::vector<TransientStaleReason>
staleReasons(const ThermalPostSource& recovered, const ThermalPostSource& current);

// ---------------------------------------------------------------------------
// ThermalPostState and ThermalPostResult
// ---------------------------------------------------------------------------

/// The derived fields of one time level (or of the one steady state).
///
/// A plain, immutable-by-constness value: a consumer reaches it only through a
/// `const` reference of a `ThermalPostResult`, which owns it.
struct ThermalPostState {
    /// The time of this state in s; nullopt for a steady result.
    std::optional<Time> time{};
    /// `T` in K, parallel to `ThermalDofIndex`. A snapshot of the solver's values,
    /// bit for bit; the solver result remains the source of truth for them.
    std::vector<Temperature> temperatures{};
    /// The element fields, parallel to `mesh.tetrahedra()`.
    ThermalElementFields fields{};

    friend bool operator==(const ThermalPostState&, const ThermalPostState&) = default;
};

/// The recovered fields of one analysis, bound to what they were recovered from.
///
/// POSSESSION IS THE EVIDENCE that the solution was current when this was made,
/// that the numbering described the mesh and the solution, that every temperature
/// was finite and physical, that every tetrahedron had valid geometry, and that
/// every recovered value is finite. There is no public constructor and a small
/// number of friends, so a half-built result is unrepresentable.
///
/// A STEADY RESULT HAS ONE STATE WITHOUT A TIME; A TRANSIENT ONE HAS ONE PER STORED
/// STATE, IN THE SOLUTION'S OWN TIME ORDER. One recovery kernel serves both; the
/// transient path calls it once per stored state and re-sorts nothing.
///
/// IMMUTABLE. There is no setter and no non-const accessor. NOT A `ThermalResult`:
/// that type also carries a heat balance (P18-BALANCE-001), which this milestone
/// does not form. The conversion is a later milestone's, and these arrays are what
/// it will consume.
class BETTERCAD_THERMAL_EXPORT ThermalPostResult {
public:
    /// What this was recovered from.
    [[nodiscard]] const ThermalPostSource& source() const noexcept { return source_; }
    [[nodiscard]] ThermalAnalysisMode mode() const noexcept { return source_.mode; }

    /// The nodes of the numbering (`Ndof`).
    [[nodiscard]] std::size_t nodeCount() const noexcept { return nodeCount_; }
    /// The tetrahedra: the length of every element array of every state.
    [[nodiscard]] std::size_t elementCount() const noexcept { return elementIds_.size(); }

    /// The tetrahedra's ids in element order, ascending, parallel to every element array.
    [[nodiscard]] std::span<const meshing::ElementId> elementIds() const noexcept { return elementIds_; }
    /// The ordinal of @p element in the element arrays, or nullopt if the mesh had no such
    /// tetrahedron. A binary search of the stored ids: an `ElementId` is never an index.
    [[nodiscard]] std::optional<std::size_t> ordinalOf(meshing::ElementId element) const noexcept;

    /// One state for a steady result; one per stored time level for a transient one.
    [[nodiscard]] std::span<const ThermalPostState> states() const noexcept { return states_; }
    [[nodiscard]] std::size_t stateCount() const noexcept { return states_.size(); }
    /// The last state: the only one of a steady result, the one at `t_end` of a transient.
    [[nodiscard]] const ThermalPostState& finalState() const noexcept { return states_.back(); }

    /// The temperature of @p node at state @p state, or an error if @p map is not the
    /// numbering this result belongs to, the node is not a node of it, or @p state is out
    /// of range. Through the numbering, never `NodeId::value()`.
    [[nodiscard]] Result<Temperature> temperatureOf(std::size_t state, const ThermalDofMap& map,
                                                    meshing::NodeId node) const;
    /// grad(T), q and |q| of tetrahedron @p element at state @p state, or an error.
    [[nodiscard]] Result<TemperatureGradient3> gradientOf(std::size_t state, meshing::ElementId element) const;
    [[nodiscard]] Result<HeatFlux3> heatFluxOf(std::size_t state, meshing::ElementId element) const;
    [[nodiscard]] Result<HeatFlux> heatFluxMagnitudeOf(std::size_t state, meshing::ElementId element) const;

    [[nodiscard]] bool describes(const ThermalDofMap& map) const noexcept {
        return map.stamp() == source_.system.mesh && map.nodeCount() == nodeCount_;
    }

    friend bool operator==(const ThermalPostResult&, const ThermalPostResult&) = default;

private:
    ThermalPostResult(ThermalPostSource source, std::size_t nodeCount, std::vector<meshing::ElementId> elementIds,
                      std::vector<ThermalPostState> states)
        : source_(std::move(source))
        , nodeCount_(nodeCount)
        , elementIds_(std::move(elementIds))
        , states_(std::move(states)) {}

    friend BETTERCAD_THERMAL_EXPORT std::expected<ThermalPostResult, PostFailure>
    recoverSteadyFromSolution(const meshing::Mesh& mesh, const ThermalDofMap& map, ThermalConductivity conductivity,
                              const SteadyThermalSolution& solution);
    friend BETTERCAD_THERMAL_EXPORT std::expected<ThermalPostResult, PostFailure>
    recoverTransientFromSolution(const meshing::Mesh& mesh, const ThermalDofMap& map, ThermalConductivity conductivity,
                                 const TransientThermalSolution& solution);

    ThermalPostSource source_{};
    std::size_t nodeCount_ = 0;
    std::vector<meshing::ElementId> elementIds_{};
    std::vector<ThermalPostState> states_{};
};

// ---------------------------------------------------------------------------
// The solution seam
// ---------------------------------------------------------------------------

/// Recovers the fields of an accepted STEADY @p solution on @p mesh with @p conductivity.
///
/// THE SEAM THE TESTS DRIVE WITH A HAND MESH: no document, no `ThermalModel`. It
/// checks, before the element loop and in this order, that the solution belongs to
/// THIS mesh generation (`solution.source().system.mesh == mesh.stamp()` and the node
/// count -- the stamp, not the counts), that the numbering describes both, and that the
/// temperature vector is `Ndof` long; then runs the kernel.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::expected<ThermalPostResult, PostFailure>
recoverSteadyFromSolution(const meshing::Mesh& mesh, const ThermalDofMap& map, ThermalConductivity conductivity,
                          const SteadyThermalSolution& solution);

/// The same for a TRANSIENT history: the kernel is run once per stored state, in the
/// solution's own order, and the result has `solution.sampleCount()` states with the
/// solution's sample times.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::expected<ThermalPostResult, PostFailure>
recoverTransientFromSolution(const meshing::Mesh& mesh, const ThermalDofMap& map, ThermalConductivity conductivity,
                             const TransientThermalSolution& solution);

// ---------------------------------------------------------------------------
// The model-level entry points
// ---------------------------------------------------------------------------

/// Recovers the fields of @p solution for @p model, or says why not.
///
/// THE CHECKS, IN THIS ORDER, ALL BEFORE THE ELEMENT LOOP:
///
/// ```text
/// 1  the solution is CURRENT for the document, the model, the loads, the conditions and the
///    settings (requireCurrent): a remesh, a load, a condition, a conductivity or a settings
///    edit is refused even when every count and id agrees          PostSourceMismatch
/// 2  the material resolves for the solution's mode                 MaterialMismatch
/// 3  the model's conductivity IS the document's now                MaterialMismatch
///    (a ThermalModel older than a material edit would pair an old k with a current solution)
/// 4  the seam's checks and the kernel
/// ```
///
/// The conductivity is read from the document through P18-MAT-001 now; nothing is
/// cached and nothing is stored beside the result.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<ThermalPostResult>
recoverSteadyThermal(const Document& document, const ThermalModel& model, const ThermalDofMap& map,
                     const SteadyThermalSolution& solution, std::span<const ThermalLoad> loads,
                     std::span<const ThermalBoundaryCondition> conditions, const ThermalSolverSettings& settings);

[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<ThermalPostResult>
recoverTransientThermal(const Document& document, const ThermalModel& model, const ThermalDofMap& map,
                        const TransientThermalSolution& solution, std::span<const ThermalLoad> loads,
                        std::span<const ThermalBoundaryCondition> conditions,
                        const UniformInitialTemperature& initial, const TransientSettings& transient,
                        const ThermalSolverSettings& settings);

/// The full refusal the matching function would report, or none. The same checks in the
/// same order.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::optional<PostFailure>
steadyRecoveryFailure(const Document& document, const ThermalModel& model, const ThermalDofMap& map,
                      const SteadyThermalSolution& solution, std::span<const ThermalLoad> loads,
                      std::span<const ThermalBoundaryCondition> conditions, const ThermalSolverSettings& settings);

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::optional<PostFailure>
transientRecoveryFailure(const Document& document, const ThermalModel& model, const ThermalDofMap& map,
                         const TransientThermalSolution& solution, std::span<const ThermalLoad> loads,
                         std::span<const ThermalBoundaryCondition> conditions,
                         const UniformInitialTemperature& initial, const TransientSettings& transient,
                         const ThermalSolverSettings& settings);

// ---------------------------------------------------------------------------
// Currentness of a held post result
// ---------------------------------------------------------------------------

/// Whether @p result still describes the model: exactly as current as the solution it was
/// recovered from, and by the same comparison. Identity only; no array is read.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT bool
isCurrent(const ThermalPostResult& result, const Document& document, const ThermalModel& model,
          std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
          const ThermalSolverSettings& settings);
[[nodiscard]] BETTERCAD_THERMAL_EXPORT bool
isCurrent(const ThermalPostResult& result, const Document& document, const ThermalModel& model,
          std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
          const UniformInitialTemperature& initial, const TransientSettings& transient,
          const ThermalSolverSettings& settings);

/// `Ok` if current, otherwise a `FailedPrecondition` naming each reason that moved.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<void>
requireCurrent(const ThermalPostResult& result, const Document& document, const ThermalModel& model,
               std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
               const ThermalSolverSettings& settings);
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<void>
requireCurrent(const ThermalPostResult& result, const Document& document, const ThermalModel& model,
               std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
               const UniformInitialTemperature& initial, const TransientSettings& transient,
               const ThermalSolverSettings& settings);

} // namespace bettercad::thermal
