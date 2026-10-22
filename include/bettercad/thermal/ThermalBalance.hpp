#pragma once

// P18-BALANCE-001 -- the global heat accounting of an accepted thermal solution:
// the heat applied through faces, the heat generated in the volume, the heat the
// convection boundaries exchange, the heat the prescribed temperatures supply, and,
// for a transient step, the rate at which the body stores energy.
//
// ONE SIGN, FROZEN BY P18-DATA-001's `HeatBalance` AND OBEYED HERE:
//
// ```text
//     every scalar heat rate is positive when heat goes INTO the thermal domain
//
//     surface    Q_surface    = sum q'' A            positive q'' heats the body       (P18-LOAD-001)
//     generation Q_generation = sum q''' V           positive generation adds heat     (P18-LOAD-001)
//     convection Q_convection = - sum h (T - Tinf) A  negative when the body is hotter  (P18-BC-001)
//     reaction   Q_reaction   = + r_c                the heat a prescribed temperature supplies
//
//     steady      B = Q_surface + Q_generation + Q_convection + Q_reaction                 ~ 0
//     transient   B = Q_surface + Q_generation + Q_convection + Q_reaction - dE/dt         ~ 0
// ```
//
// THE REACTION'S SIGN IS DERIVED, NOT CHOSEN. The assembled equation of row `i` is
// `(K T)_i = Q_i`; at a prescribed unknown the constraint is an extra heat `R_i` the
// equation did not contain, so `(K T)_i = Q_i + R_i` and
//
// ```text
//     r = K T - Q                         (steady)
//     r = C (T[n+1] - T[n]) / dt + K T[n+1] - Q   (backward Euler step)
//     R_i = + r_i   at a prescribed unknown i
// ```
//
// `K T` is the heat that LEAVES a node through conduction and convection, so a node
// that loses more than the external sources bring must be supplied by its constraint:
// a hot prescribed end of a conducting bar has `r > 0`, a positive reaction INTO the
// body, and the cold end `r < 0`, a negative one. A minus sign here would put heat
// into a cold wall. The two-node oracle in the tests (`g = 2 W/K`, 400 K and 300 K:
// `K T = [+200, -200] W`) is what froze it.
//
// WHERE EACH NUMBER COMES FROM, AND NOTHING IS RECOMPUTED:
//
// ```text
//     q'' A and q''' V per load        P18-LOAD-001   PreparedThermalLoads::contributions()
//     Kconv T - Qconv per facet        P18-BC-001     Tri3Convection::residualFrom
//     K, C, Q                          P18-ASSEMBLY   ThermalSystem
//     T, the grid, the states          P18-SOLVE / P18-TRANSIENT
//     the prescribed unknowns          P18-BC-001     ThermalConstraintMap
//     r, the reactions, 1^T C dT/dt    THIS MILESTONE
// ```
//
// There is no `q'' * area`, no `h A / 12`, no element integral and no Jacobian here.
//
// THE REACTION COMES FROM THE EQUATION AND NOT FROM THE BALANCE. It is the component
// of the full residual at a prescribed unknown, whatever that is. It is never "the
// heat that makes the balance close": a reaction defined that way would make the
// balance a tautology, and a wrong load total, a dropped boundary or a sign error would
// be absorbed into the reaction and reported as a balanced model. The balance residual
// is therefore a CONSEQUENCE (the free rows' residual plus the attribution errors), and
// it is separate from the solver's own acceptance: a balance can cancel large local
// errors, which is why it never replaces the free residual.
//
// CONVECTION IS THE EXCHANGE, NOT THE SOURCE. `Qconv = h Tinf A / 3` is only the ambient
// half of the Robin term; the heat the boundary exchanges is `Kconv T - Qconv`, which
// needs the solved temperature. The global `Q` already contains the loads and `Qconv`,
// so the categories are parallel VIEWS of the same equation and are never added to it.
//
// TRANSIENT STORAGE IS THE FULL CAPACITY. `dE = 1^T C (T[n+1] - T[n])` over EVERY
// unknown, prescribed ones included: `C` is the consistent matrix and its off-diagonal
// terms couple a prescribed node to its free neighbours, so neither `C_ff` nor `diag(C)`
// is the stored energy. The rate uses the ACTUAL step (a shortened last step is not the
// nominal one), and the convection of a step is evaluated at `T[n+1]`, the state the
// backward-Euler equation uses.
//
// A TRANSIENT BALANCE IS PER INTEGRATION STEP. A history that stored only its final state
// (`FinalOnly`) over more than one step does not contain the states a step balance needs,
// and BALANCE does not integrate again to invent them and does not treat an output interval
// as a step: it refuses (`StepDataMissing`). A history with every step (`EveryStep`), or a
// single step, balances.
//
// DERIVED ONLY. Nothing here is canonical, persisted or in a command; a result carries the
// solution's own source and is exactly as current as the solution it was taken from. A
// viewer displays these numbers and never recomputes them.
//
// WHAT THIS IS NOT: no validation policy beyond the numerical self-consistency of the
// accounting (P18-VALID-001 owns acceptance), no field recovery (P18-POST-001), no colour
// (P18-VIZ-001), no command (P18-CMD-001), no persistence (P18-PERSIST-001).

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/units/Dimension.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/thermal/Export.hpp>
#include <bettercad/thermal/ThermalAnalysis.hpp>
#include <bettercad/thermal/ThermalBoundary.hpp>
#include <bettercad/thermal/ThermalData.hpp>
#include <bettercad/thermal/ThermalDof.hpp>
#include <bettercad/thermal/ThermalLoadVector.hpp>
#include <bettercad/thermal/ThermalPost.hpp>
#include <bettercad/thermal/ThermalSolve.hpp>
#include <bettercad/thermal/ThermalSparse.hpp>
#include <bettercad/thermal/ThermalSystem.hpp>
#include <bettercad/thermal/ThermalTransient.hpp>

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
///     K T            (W/K) . K              =  W       a conduction row is a heat rate
///     C dT/dt        (J/K) . (K/s)          =  W       a storage row is a heat rate
///     1^T C dT       (J/K) . K              =  J       the energy a step stores
///     dE / dt        J / s                  =  W
///     Kconv T - Qconv                       =  W       the heat a face loses
/// ```
static_assert(decltype(ThermalConductance{} * Temperature{})::dimension == dimensions::power);
static_assert(decltype(HeatCapacity{} * Temperature{})::dimension == dimensions::energy);
static_assert(decltype(Energy{} / Time{})::dimension == dimensions::power);
static_assert(Energy::dimension != Power::dimension);

// ---------------------------------------------------------------------------
// The numerical self-consistency policy
// ---------------------------------------------------------------------------

/// The ONE tolerance policy of the accounting. It decides whether the books close to
/// the arithmetic's own resolution, nothing more; whether an analysis is ACCEPTED is
/// P18-VALID-001's, which reuses this and adds no second threshold.
///
/// ```text
///     |B|  <=  kBalanceAbsoluteToleranceW + kBalanceRelativeTolerance * (gross + arithmetic)
///
///     gross       sum of the magnitudes of every term (W): the scale the heat flows have,
///                 NOT the net, which can be small because large flows cancel
///     arithmetic  the sum of |K_ij| |T_j| (+ |C_ij| |dT_j| / dt) + |Q_i| over the whole system
///                 (W): the scale the ROUNDING of K T has. A body at 325 K with no heat flowing
///                 has K T = 0 only to that rounding, and a zero-flow balance is judged by it
/// ```
///
/// THE VALUES ARE MEASURED, not chosen: TOLERANCE_MEASUREMENT.md records the worst error of
/// every qualified reference case in every preset and the margin these keep over them.
inline constexpr double kBalanceAbsoluteToleranceW = 1.0e-12;
inline constexpr double kBalanceRelativeTolerance = 1.0e-13;
/// The smallest gross scale a normalised residual divides by (W), so an all-zero balance
/// reports a residual of zero rather than dividing by it.
inline constexpr double kBalanceScaleFloorW = 1.0e-12;

/// Whether a residual of @p residual W, on a gross scale of @p gross W and an arithmetic
/// scale of @p arithmetic W, is within the policy. NaN and infinity are never within it.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT bool balanceWithinTolerance(Power residual, Power gross,
                                                                   Power arithmetic) noexcept;

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

/// Why a balance could not be computed. STABLE NAMES. A balance that was computed and
/// does not close is NOT a failure: it is a result whose `status` is `OutOfTolerance`, so
/// the terms that disagree can be inspected.
enum class BalanceProblem : std::uint8_t {
    /// The solution, the system, the prepared loads or the prepared boundary do not
    /// describe one another, or no longer describe the model (a remesh with the same
    /// counts and ids included).
    SourceMismatch,
    /// The numbering, a matrix, a vector or a constraint map has the wrong size, or does
    /// not belong to this mesh.
    NumberingMismatch,
    /// A steady balance of a transient system, or the reverse.
    ModeMismatch,
    /// The attributed totals do not account for the heat input: a load or a convection
    /// condition is missing from, or extra to, the system. Never inferred from `Q`.
    AttributionMissing,
    /// The transient history does not hold the states a step balance needs.
    StepDataMissing,
    /// A time step is not finite and positive.
    InvalidTimeStep,
    /// A component of the full residual is not finite.
    NonFiniteResidual,
    /// A reaction heat is not finite.
    NonFiniteReaction,
    /// A category total (surface, generation, convection) is not finite.
    NonFiniteCategory,
    /// A stored energy or a storage rate is not finite.
    NonFiniteStorage,
    /// The balance residual or a metric of it is not finite.
    NonFiniteBalance,
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(BalanceProblem problem) noexcept;

/// A refusal with its classification and the step it was found at.
struct BalanceFailure {
    BalanceProblem problem = BalanceProblem::SourceMismatch;
    Error error{};
    /// The step (1-based) of a transient history that failed; 0 for a steady balance or a
    /// refusal before any step.
    std::size_t step = 0;
};

// ---------------------------------------------------------------------------
// The terms of one state
// ---------------------------------------------------------------------------

/// What one load contributed, into the domain. The id is the canonical load's.
struct LoadHeat {
    ThermalLoadId load{};
    Power heatInto{};

    friend bool operator==(const LoadHeat&, const LoadHeat&) = default;
};

/// What one convection condition exchanged. `heatOut = sum (Kconv T - Qconv)` over its facets
/// is positive when the body is hotter than the ambient; `heatInto = -heatOut`.
struct ConvectionHeat {
    ThermalBoundaryConditionId condition{};
    Area area{};
    Power heatOut{};
    Power heatInto{};

    friend bool operator==(const ConvectionHeat&, const ConvectionHeat&) = default;
};

/// The heat the constraint of one prescribed unknown supplies INTO the domain (negative when
/// it removes heat). One per UNIQUE prescribed unknown: two identical canonical conditions
/// that P18-BC-001 collapsed to one constraint are one reaction.
struct ReactionHeat {
    ThermalDofIndex dof = ThermalDofIndex::fromValue(0);
    Power heatInto{};

    friend bool operator==(const ReactionHeat&, const ReactionHeat&) = default;
};

enum class BalanceStatus : std::uint8_t {
    /// The residual is within `balanceWithinTolerance`.
    Balanced,
    /// Computed, finite, and outside the tolerance.
    OutOfTolerance,
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(BalanceStatus status) noexcept;

/// The accounting of ONE state (steady) or ONE backward-Euler step (transient).
///
/// THE TOTALS ARE DERIVED FROM THE DETAILS and the residual from the totals, by
/// `HeatBalance::netImbalance()` and nowhere else, so no second formula can diverge: the
/// per-load totals sum to the category, the per-condition totals to the convection, the
/// per-unknown reactions to the reaction, in the orders listed.
///
/// EXPORTED, because `grossScale` and `normalizedResidual` are defined in the library: a static build links them
/// without an annotation and a shared build cannot find them without one (the same defect P18-BC-001's first shared
/// run found in `PreparedConvectionBoundary`).
struct BETTERCAD_THERMAL_EXPORT ThermalHeatBalance {
    /// The five terms, P18-DATA-001's type and sign.
    HeatBalance terms{};

    /// Surface loads, then generation loads, each in the prepared loads' order.
    std::vector<LoadHeat> surfaceLoads{};
    std::vector<LoadHeat> generationLoads{};
    /// Convection conditions in the prepared boundary's order. Overlapping conditions are
    /// separate entries and ADD.
    std::vector<ConvectionHeat> convections{};
    /// One per prescribed unknown, ascending by `ThermalDofIndex`.
    std::vector<ReactionHeat> reactions{};

    /// `dE = 1^T C (T[n+1] - T[n])` of the step, J; absent for a steady state.
    std::optional<Energy> storedEnergyChange{};
    /// The 1-based step, the time at its end and its ACTUAL size; zero / absent for steady.
    std::size_t step = 0;
    std::optional<Time> time{};
    std::optional<Time> stepSize{};

    /// The rounding scale of the arithmetic (see the policy), W.
    Power arithmeticScale{};
    BalanceStatus status = BalanceStatus::Balanced;

    /// `B`, W: the one residual.
    [[nodiscard]] Power residual() const noexcept { return terms.netImbalance(); }
    /// The gross scale, W: the sum of the MAGNITUDES of every individual heat rate (each load, each convection
    /// condition, each reaction, the storage rate), not of the category totals. Two loads of +1e6 W and -1e6 W are 2e6 W of
    /// heat flow even though their category total is small, and the arithmetic that sums them has that scale.
    [[nodiscard]] Power grossScale() const noexcept;
    /// `|B| / max(gross, floor)`: finite, and zero for an all-zero balance.
    [[nodiscard]] double normalizedResidual() const noexcept;

    friend bool operator==(const ThermalHeatBalance&, const ThermalHeatBalance&) = default;
};

// ---------------------------------------------------------------------------
// The algebraic seam
// ---------------------------------------------------------------------------

/// Everything a balance reads, as non-owning views: nothing is copied and nothing is written.
///
/// THE SEAM THE TESTS DRIVE WITH HAND SYSTEMS: a conductance network of two nodes needs no
/// mesh and no document. The model-level functions run the SAME code.
struct BalanceAlgebra {
    const ConductanceMatrix& conductance;
    /// Engaged for a transient balance, null for a steady one.
    const CapacityMatrix* capacity;
    const ThermalHeatVector& heat;
    const ThermalConstraintMap& constraints;
    /// What each load put into `heat`, in the prepared loads' order.
    std::span<const ThermalLoadContribution> loads;
    /// The prepared convection conditions the system was assembled with.
    std::span<const PreparedConvectionBoundary> convections;
};

/// The full steady residual `r = K T - Q`, W, over every unknown. Zero (to rounding) at a free
/// unknown of an accepted solution; at a prescribed unknown, the heat its constraint supplies.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::expected<std::vector<double>, BalanceFailure>
steadyFullResidual(const ConductanceMatrix& conductance, const ThermalHeatVector& heat,
                   std::span<const Temperature> temperatures);

/// The full residual of one backward-Euler step `r = C (T[n+1] - T[n]) / dt + K T[n+1] - Q`, W.
/// THE TRANSIENT REACTION IS NOT THE STEADY ONE: the storage term is part of it, and with a
/// consistent `C` it couples a prescribed node to its free neighbours.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::expected<std::vector<double>, BalanceFailure>
transientFullResidual(const ConductanceMatrix& conductance, const CapacityMatrix& capacity,
                      const ThermalHeatVector& heat, std::span<const Temperature> previous,
                      std::span<const Temperature> next, double timeStep);

/// `1^T C (T[next] - T[previous])`, J, over EVERY unknown, summed in row order.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::expected<Energy, BalanceFailure>
storedEnergyChange(const CapacityMatrix& capacity, std::span<const Temperature> previous,
                   std::span<const Temperature> next);

/// The heat one convection condition exchanges at the nodal temperatures @p temperatures:
/// `sum (Kconv T - Qconv)` over its facets in order (outward positive), from the prepared terms,
/// never re-integrated.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::expected<ConvectionHeat, BalanceFailure>
convectionHeatOf(const PreparedConvectionBoundary& condition, std::span<const Temperature> temperatures);

/// The reaction at each prescribed unknown of @p constraints from a full residual: `+r_c`, in
/// the constraint map's order. Free rows are not reactions.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::expected<std::vector<ReactionHeat>, BalanceFailure>
reactionHeats(std::span<const double> fullResidual, const ThermalConstraintMap& constraints);

/// The balance of a STEADY state.
///
/// ```text
/// 1  sizes agree; every temperature finite
/// 2  the attribution accounts for the heat input:  sum Q = sum loads + sum Qconv
/// 3  r = K T - Q; the reactions are its prescribed components
/// 4  surface and generation from the load contributions; convection from the facets at T
/// 5  every total finite; B from the five terms; status from the policy
/// ```
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::expected<ThermalHeatBalance, BalanceFailure>
steadyHeatBalance(const BalanceAlgebra& input, std::span<const Temperature> temperatures);

/// The balance of ONE backward-Euler step of size @p timeStep (the ACTUAL size) from
/// @p previous to @p next, numbered @p step (1-based) for the record. The convection is evaluated
/// at @p next.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::expected<ThermalHeatBalance, BalanceFailure>
transientStepHeatBalance(const BalanceAlgebra& input, std::span<const Temperature> previous,
                         std::span<const Temperature> next, double timeStep, std::size_t step);

// ---------------------------------------------------------------------------
// The result
// ---------------------------------------------------------------------------

/// What a transient history integrated over its actual steps.
///
/// ```text
///     external   sum_j dt_j (Q_surface + Q_generation + Q_convection + Q_reaction)_j      J
///     stored     sum_j dE_j                                                                J
///     endpoints  1^T C (T[N] - T[0])        an independent evaluation of the same energy   J
///     residual   external - stored                                                         J
///     allowed    sum_j dt_j (the policy's tolerance of step j)                             J
/// ```
struct IntegratedEnergy {
    Time span{};
    Energy external{};
    Energy stored{};
    Energy endpoints{};
    Energy residual{};
    Energy allowed{};

    friend bool operator==(const IntegratedEnergy&, const IntegratedEnergy&) = default;
};

/// A balance's source is exactly the solution's own: mode, system, prescribed conditions, solver
/// settings and, transient, the initial condition and the time settings. There is NO balance
/// setting, so a display choice cannot stale it.
using ThermalBalanceSource = ThermalPostSource;

/// The balance of one accepted solution, bound to what it was computed from.
///
/// POSSESSION IS THE EVIDENCE that the solution, the system, the loads and the boundary agreed,
/// that every term of every state is finite, and (transient) that every step of the history was
/// balanced from its own states. No public constructor; the two solution seams are the friends.
/// IMMUTABLE. A `status` of `OutOfTolerance` is a result, not a failure.
class BETTERCAD_THERMAL_EXPORT ThermalBalanceResult {
public:
    [[nodiscard]] const ThermalBalanceSource& source() const noexcept { return source_; }
    [[nodiscard]] ThermalAnalysisMode mode() const noexcept { return source_.mode; }

    /// One state for a steady result; one per integration step, in order, for a transient one.
    [[nodiscard]] std::span<const ThermalHeatBalance> states() const noexcept { return states_; }
    [[nodiscard]] std::size_t stateCount() const noexcept { return states_.size(); }
    [[nodiscard]] const ThermalHeatBalance& finalState() const noexcept { return states_.back(); }

    /// The whole-run energy accounting; engaged for a transient result.
    [[nodiscard]] const std::optional<IntegratedEnergy>& integrated() const noexcept { return integrated_; }

    /// Every state within the policy. (The integrated energy needs no clause of its own: the sum of per-step residuals is within the sum of the per-step tolerances whenever every step is.)
    [[nodiscard]] bool balanced() const noexcept;
    /// The largest `normalizedResidual()` of any state.
    [[nodiscard]] double worstNormalizedResidual() const noexcept;

    /// The numbering this result's reactions are indexed by.
    [[nodiscard]] bool describes(const ThermalDofMap& map) const noexcept {
        return map.stamp() == source_.system.mesh && map.nodeCount() == nodeCount_;
    }

    friend bool operator==(const ThermalBalanceResult&, const ThermalBalanceResult&) = default;

private:
    ThermalBalanceResult(ThermalBalanceSource source, std::size_t nodeCount, std::vector<ThermalHeatBalance> states,
                         std::optional<IntegratedEnergy> integrated)
        : source_(std::move(source))
        , nodeCount_(nodeCount)
        , states_(std::move(states))
        , integrated_(integrated) {}

    friend BETTERCAD_THERMAL_EXPORT std::expected<ThermalBalanceResult, BalanceFailure>
    balanceSteadySolution(const ThermalSystem& system, const ThermalDofMap& map, const ThermalConstraintMap& constraints,
                          std::span<const ThermalLoadContribution> loads,
                          std::span<const PreparedConvectionBoundary> convections, const SteadyThermalSolution& solution);
    friend BETTERCAD_THERMAL_EXPORT std::expected<ThermalBalanceResult, BalanceFailure>
    balanceTransientSolution(const ThermalSystem& system, const ThermalDofMap& map,
                             const ThermalConstraintMap& constraints, std::span<const ThermalLoadContribution> loads,
                             std::span<const PreparedConvectionBoundary> convections,
                             const TransientThermalSolution& solution);

    ThermalBalanceSource source_{};
    std::size_t nodeCount_ = 0;
    std::vector<ThermalHeatBalance> states_{};
    std::optional<IntegratedEnergy> integrated_{};
};

// ---------------------------------------------------------------------------
// The solution seams
// ---------------------------------------------------------------------------

/// The balance of an accepted STEADY @p solution of @p system. No document and no model: the seam
/// the tests drive with a hand mesh. It checks that the system is steady and is the solution's own
/// (by value), that the numbering and the constraints belong to it, that every prescribed unknown
/// holds its value, and that the convection conditions account for the system's facets.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::expected<ThermalBalanceResult, BalanceFailure>
balanceSteadySolution(const ThermalSystem& system, const ThermalDofMap& map, const ThermalConstraintMap& constraints,
                      std::span<const ThermalLoadContribution> loads,
                      std::span<const PreparedConvectionBoundary> convections, const SteadyThermalSolution& solution);

/// The balance of an accepted TRANSIENT @p solution, step by step. The states a step needs are the
/// stored samples of an `EveryStep` history, or the initial and final states of a single step; any
/// other history is refused (`StepDataMissing`). The step size is the grid's own `stepAt`.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::expected<ThermalBalanceResult, BalanceFailure>
balanceTransientSolution(const ThermalSystem& system, const ThermalDofMap& map, const ThermalConstraintMap& constraints,
                         std::span<const ThermalLoadContribution> loads,
                         std::span<const PreparedConvectionBoundary> convections,
                         const TransientThermalSolution& solution);

// ---------------------------------------------------------------------------
// The model-level entry points
// ---------------------------------------------------------------------------

/// The balance of @p solution for @p model, or why not.
///
/// THE CHECKS, IN THIS ORDER, ALL BEFORE ANY ARITHMETIC:
///
/// ```text
/// 1  the solution is current for the document, model, loads, conditions and settings
/// 2  the assembled system is current for the same, in the solution's mode
/// 3  the prepared loads and the prepared boundary are current, and describe the numbering
/// 4  the prepared loads are the system's loads, by value (the prepared boundary is current for the same
///    conditions as the solution, so its convection conditions are the system's)
/// 5  the seam
/// ```
///
/// A stale or mismatched input is refused as such (`SourceMismatch`), never balanced and never
/// "repaired": a remesh that kept every count and id, a load, condition, material or settings edit.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<ThermalBalanceResult>
recoverSteadyBalance(const Document& document, const ThermalModel& model, const ThermalDofMap& map,
                     const ThermalSystem& system, const SteadyThermalSolution& solution,
                     const PreparedThermalLoads& preparedLoads, const PreparedThermalBoundary& preparedBoundary,
                     std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
                     const ThermalSolverSettings& settings);

[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<ThermalBalanceResult>
recoverTransientBalance(const Document& document, const ThermalModel& model, const ThermalDofMap& map,
                        const ThermalSystem& system, const TransientThermalSolution& solution,
                        const PreparedThermalLoads& preparedLoads, const PreparedThermalBoundary& preparedBoundary,
                        std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
                        const UniformInitialTemperature& initial, const TransientSettings& transient,
                        const ThermalSolverSettings& settings);

/// The full refusal the matching function would report, or none. The same checks in the same order.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::optional<BalanceFailure>
steadyBalanceFailure(const Document& document, const ThermalModel& model, const ThermalDofMap& map,
                     const ThermalSystem& system, const SteadyThermalSolution& solution,
                     const PreparedThermalLoads& preparedLoads, const PreparedThermalBoundary& preparedBoundary,
                     std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
                     const ThermalSolverSettings& settings);

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::optional<BalanceFailure>
transientBalanceFailure(const Document& document, const ThermalModel& model, const ThermalDofMap& map,
                        const ThermalSystem& system, const TransientThermalSolution& solution,
                        const PreparedThermalLoads& preparedLoads, const PreparedThermalBoundary& preparedBoundary,
                        std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
                        const UniformInitialTemperature& initial, const TransientSettings& transient,
                        const ThermalSolverSettings& settings);

// ---------------------------------------------------------------------------
// Currentness of a held balance
// ---------------------------------------------------------------------------

/// Whether @p result still describes the model: exactly as current as the solution it was taken
/// from, by the same comparison. Identity only; no array is read.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT bool
isCurrent(const ThermalBalanceResult& result, const Document& document, const ThermalModel& model,
          std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
          const ThermalSolverSettings& settings);
[[nodiscard]] BETTERCAD_THERMAL_EXPORT bool
isCurrent(const ThermalBalanceResult& result, const Document& document, const ThermalModel& model,
          std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
          const UniformInitialTemperature& initial, const TransientSettings& transient,
          const ThermalSolverSettings& settings);

/// `Ok` if current, otherwise a `FailedPrecondition` naming each reason that moved.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<void>
requireCurrent(const ThermalBalanceResult& result, const Document& document, const ThermalModel& model,
               std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
               const ThermalSolverSettings& settings);
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<void>
requireCurrent(const ThermalBalanceResult& result, const Document& document, const ThermalModel& model,
               std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
               const UniformInitialTemperature& initial, const TransientSettings& transient,
               const ThermalSolverSettings& settings);

} // namespace bettercad::thermal
