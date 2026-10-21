#pragma once

// P18-TRANSIENT-001 -- the transient thermal solve of  C dT/dt + K T = Q  by
// backward Euler, with exact prescribed temperatures.
//
// ```text
//     C (T[n+1] - T[n]) / dt  +  K T[n+1]  =  Q
//
//     A  = C / dt + K                     W/K     (C in J/K, dt in s)
//     b  = Q + (C / dt) T[n]              W
//
//     A_ff T_f[n+1]  =  b_f - A_fc T_c    exact elimination of the prescribed unknowns
// ```
//
// THE FORMULATION IS FROZEN BY ADR-049: backward Euler at a constant step from
// t = 0, constant isotropic k, rho and cp, constant loads, constant convection,
// constant prescribed temperatures, a uniform initial temperature. Anything
// time-dependent has no representation in the canonical data (P18-DATA-001), so
// there is nothing to "silently freeze": a load is a number, a prescribed
// temperature is a number, and the compile-fail group proves no member carries a
// time function.
//
// STEADY SINGULARITY IS NOT TRANSIENT SINGULARITY. A pure-Neumann steady model is
// singular: the constant temperature is in the null space of K. A pure-Neumann
// TRANSIENT model with positive capacity is not: `C / dt + K` is positive
// definite because C is, and the absolute level is fixed by the initial
// temperature. So the "missing datum" analysis of P18-SOLVE-001 is deliberately
// NOT applied here, and a disconnected insulated body is not rejected for being
// disconnected: each region evolves from its own initial state. What is refused is
// what is actually wrong -- a region with no capacity at all, which the
// factorisation's pivot gate reports as singular. Nothing is regularised.
//
// THE CAPACITY IS P18-ELEM'S CONSISTENT MATRIX, as assembled. It is never
// row-lumped, replaced or scaled here: a lumped matrix and the consistent one
// agree on a uniform temperature and disagree on every other mode, which is why
// the tests include a non-uniform two-unknown oracle.
//
// EVERY STEP IS INDEPENDENTLY ACCEPTED. A backend's success is not evidence, so
// each step is checked twice from the ORIGINAL arrays and not from the matrix that
// was factorised:
//
// ```text
//   reduced   r_f = A_ff T_f - (b_f - A_fc T_c)       the solve of the system that was built
//   physical  r   = C (T[n+1] - T[n]) / dt + K T[n+1] - Q,  over the free rows
// ```
//
// The second is the one that catches a wrongly BUILT system (a `C * dt + K`, a
// wrong history sign): its reduced residual would be small and its physical
// residual would not. Prescribed unknowns are held exactly at every step; the full
// residual at those unknowns is the heat the boundary supplies and is not a
// failure. The heat balance that interprets it is P18-BALANCE-001's.
//
// THE TIME GRID. t0 = 0. Full steps are exactly `dt`; if `t_end / dt` is not an
// integer (to a relative 1e-9) the LAST step is the shortened remainder, so the
// final time is exactly `t_end`, never overshot and never truncated, and that
// step uses its own `C / dt_last`. Times are formed by multiplication, never by
// repeated addition. There is no list of requested output times in the canonical
// data -- only the policy `FinalOnly | EveryStep` -- so there is no off-grid
// request, no event splitting and no interpolation.
//
// WHAT THIS IS NOT.
//
//     no adaptive stepping       a failed step fails the run; dt is never changed
//     no retry                   no second attempt with a smaller step
//     no regularisation          no `A += eps*I`, no replacement of C
//     no interpolation           the stored states are the integrator's own
//     no gradient, no flux       P18-POST-001
//     no heat balance            P18-BALANCE-001 (only the per-step algebraic residual here)
//     no restart, no persistence every run starts from the canonical initial condition

#include <bettercad/core/Error.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/thermal/Export.hpp>
#include <bettercad/thermal/ThermalBoundary.hpp>
#include <bettercad/thermal/ThermalData.hpp>
#include <bettercad/thermal/ThermalDof.hpp>
#include <bettercad/thermal/ThermalLoadVector.hpp>
#include <bettercad/thermal/ThermalSparse.hpp>
#include <bettercad/thermal/ThermalSystem.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace bettercad::thermal {

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

/// Why a transient run was refused. STABLE NAMES. The first group is checked
/// before any step; the rest classify what a step found.
enum class TransientProblem : std::uint8_t {
    /// The solver settings are not finite and positive.
    InvalidSettings,
    /// The time step is not finite and positive.
    InvalidTimeStep,
    /// The end time is not finite and non-negative (the canonical settings also
    /// require it positive and at least one step long).
    InvalidEndTime,
    /// The output policy is not one this build knows.
    InvalidOutputPolicy,
    /// The initial temperature is not a finite absolute temperature.
    InvalidInitialTemperature,
    /// The definition is not a transient one: no initial condition or no transient
    /// settings.
    NotTransientDefinition,
    /// The system was not assembled for a transient analysis: it has no capacity.
    NotTransientSystem,
    /// The numbering is not the one the system was assembled against.
    DofMapMismatch,
    /// The prescribed temperatures do not belong to the numbering.
    ConstraintMismatch,
    /// The assembled system no longer describes the model, loads, conditions or
    /// material. Refused before any step.
    StaleSystem,
    /// The prepared boundary no longer describes the model or the conditions.
    StaleBoundary,
    /// The number of steps exceeds the resource guard `kMaxTimeSteps`.
    TooManyTimeSteps,
    /// The stored history would exceed the resource guard `kMaxStoredValues`.
    HistoryTooLarge,
    /// An entry of the effective system or its right-hand side is not finite:
    /// `C / dt` or the history term overflowed.
    NonFiniteInput,
    /// The effective matrix is not symmetric, or its layout is inconsistent.
    InconsistentSystem,
    /// The factorisation measured a singular or too ill conditioned effective
    /// system. For a valid transient model this means a region with no capacity.
    SingularSystem,
    /// The library rejected the matrix before factorising.
    FactorizationFailure,
    /// A solved temperature is not finite.
    NonFiniteSolution,
    /// A residual component is not finite.
    NonFiniteResidual,
    /// The reduced or the physical free residual exceeded the threshold.
    ResidualTooLarge,
    /// A prescribed unknown does not hold its prescribed temperature.
    ConstraintNotPreserved,
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(TransientProblem problem) noexcept;

/// A refusal with its classification and where in the run it happened.
struct TransientFailure {
    TransientProblem problem = TransientProblem::SingularSystem;
    Error error{};
    /// The step that failed, counted from 1 (state 0 is the initial state), or 0
    /// when the run was refused before any step.
    std::size_t step = 0;
    /// The time at the END of the failed step, in seconds; 0 for a setup refusal.
    double time = 0.0;
};

// ---------------------------------------------------------------------------
// The time grid
// ---------------------------------------------------------------------------

/// The relative tolerance within which `t_end / dt` is taken to be an integer.
/// The ONE place the time grid is given a tolerance; it is not a solver
/// tolerance and nothing else uses it. `0.3 / 0.1` is `2.9999999999999996` in
/// double precision, and without it the grid would take three steps and a
/// remainder of `5.5e-17` s.
inline constexpr double kTimeGridTolerance = 1.0e-9;

/// The most steps one run may take. A RESOURCE guard against an allocation and a
/// step count that cannot be represented, not an accuracy policy: a larger count
/// is refused, never turned into a larger step. Each step refactorises, so a run
/// anywhere near this size is hours long on any real mesh.
inline constexpr std::size_t kMaxTimeSteps = 1'000'000;

/// The most temperatures one run may store (samples times unknowns).
inline constexpr std::size_t kMaxStoredValues = 50'000'000;

/// The deterministic sequence `0 = t0 < t1 < ... < tN = t_end`, and the size of
/// each step.
///
/// ```text
/// t_k   = k * dt                for k < N          (a multiplication, never a running sum)
/// t_N   = t_end                 EXACTLY
/// step  = dt                    for every full step
/// last  = t_end - n*dt          the shortened remainder, only if t_end / dt is not an integer
/// ```
///
/// `t_end = 0` is a valid grid of one time and no step. `dt > t_end` is one step
/// of `t_end`. The grid is O(1) in memory however many steps it has: times and
/// step sizes are computed from the numbers it holds, so a million-step grid
/// costs nothing until a step is taken. POSSESSION IS THE EVIDENCE that the
/// times are finite, strictly increasing and end exactly at `t_end`.
class BETTERCAD_THERMAL_EXPORT TimeGrid {
public:
    /// Builds the grid, or says why not: `dt` finite and positive, `t_end` finite
    /// and non-negative, and a step count within `kMaxTimeSteps`.
    [[nodiscard]] static std::expected<TimeGrid, TransientFailure> create(double dt, double endTime);

    /// `N`: the number of steps; the number of times is `N + 1`.
    [[nodiscard]] std::size_t stepCount() const noexcept { return steps_; }
    /// `t_k` in seconds for `0 <= k <= N`; zero for `k = 0` and `t_end` exactly
    /// for `k = N`. Out of range is the end time.
    [[nodiscard]] double timeAt(std::size_t index) const noexcept;
    /// The size of step @p index (0-based, `index < N`) in seconds: `dt` for every
    /// full step and the remainder for a shortened last one. Out of range is zero.
    [[nodiscard]] double stepAt(std::size_t index) const noexcept;
    /// All `N + 1` times, for a caller that wants them materialised (O(N)).
    [[nodiscard]] std::vector<double> times() const;
    [[nodiscard]] double endTime() const noexcept { return end_; }
    /// The time the steps actually add up to, in seconds: `N dt` for a whole number
    /// of steps and `(N - 1) dt + remainder` for a shortened last one. It is `t_end`
    /// to rounding in the second case and within `kTimeGridTolerance * t_end` of it
    /// in the first (where `t_end / dt` was an integer only to that tolerance), so
    /// the span integrated is never silently different from the span asked for by
    /// more than the documented tolerance, and a consumer that needs the energy
    /// accumulated reads THIS, not `t_end`.
    [[nodiscard]] double integratedSpan() const noexcept;
    /// The nominal step it was built with.
    [[nodiscard]] double nominalStep() const noexcept { return nominal_; }
    /// Whether the last step is a shortened remainder.
    [[nodiscard]] bool hasPartialFinalStep() const noexcept { return partial_; }

    friend bool operator==(const TimeGrid&, const TimeGrid&) = default;

private:
    TimeGrid() = default;

    double nominal_ = 0.0;
    double end_ = 0.0;
    /// The size of the shortened last step; zero when there is none.
    double remainder_ = 0.0;
    std::size_t steps_ = 0;
    bool partial_ = false;
};

// ---------------------------------------------------------------------------
// The effective system
// ---------------------------------------------------------------------------

/// `A = C / dt + K`, in W/K, as a new sparse matrix over the UNION of the two
/// patterns. Neither input is modified. An overflowing entry is refused
/// (`NonFiniteInput`), not clamped; an asymmetric result is refused
/// (`InconsistentSystem`), not symmetrised.
///
/// THE ARGUMENTS ARE THE TYPED MATRICES. A conductance (W/K) and a capacity (J/K)
/// are different types, so passing `C` where `K` is wanted -- the swap that would
/// produce a plausible wrong answer -- does not compile.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::expected<ThermalSparseMatrix, TransientFailure>
effectiveMatrix(const ConductanceMatrix& conductance, const CapacityMatrix& capacity, double timeStep);

// ---------------------------------------------------------------------------
// One step's evidence
// ---------------------------------------------------------------------------

/// What one accepted step measured. Derived; nothing here is canonical.
struct TransientStepRecord {
    /// 1-based; state 0 is the initial state. The times and the step size are the
    /// grid's, by this index; they are not repeated per record.
    std::size_t index = 0;
    /// Normalized reduced residual `||A_ff T_f - (b_f - A_fc T_c)|| / (||A_ff|| ||T_f|| + ||rhs||)`.
    double reducedResidual = 0.0;
    /// Normalized physical free residual
    /// `||C dT/dt + K T - Q||_free / (||C dT/dt|| + ||K T|| + ||Q||)_free`.
    double physicalResidual = 0.0;
    /// `min|D| / max|D|` of the factorisation; 1 when the kernel was not invoked.
    double pivotRatio = 1.0;
    bool kernelInvoked = false;

    friend bool operator==(const TransientStepRecord&, const TransientStepRecord&) = default;
};

/// The two residual metrics `validateTransientStep` accepted.
struct TransientStepResidual {
    double reduced = 0.0;
    double physical = 0.0;

    friend bool operator==(const TransientStepResidual&, const TransientStepResidual&) = default;
};

/// The normalized PHYSICAL residual of one candidate step, over the free rows, with
/// no tolerance applied:
///
/// ```text
///                          || C (T[n+1] - T[n]) / dt + K T[n+1] - Q ||_free
///   ---------------------------------------------------------------------------------
///   ||C||_inf ||T[n+1] - T[n]||_2 / dt  +  ||K||_inf ||T[n+1]||_2  +  ||Q||_free
/// ```
///
/// PUBLIC BECAUSE IT IS THE DEFENCE AGAINST A WRONGLY BUILT SYSTEM, and a defence
/// that cannot be shown a wrong build has not been tested. It reads only the
/// ORIGINAL `K`, `C` and `Q`, never `C / dt + K`: a candidate that satisfies a system
/// built with `C * dt` (or a wrong history sign) has a small residual against THAT
/// system and a large one here. The same backward-error normalization as the kernel's
/// (`||r|| / (||A|| ||x|| + ||b||)`), so scaling `K`, `C` and `Q` together leaves it
/// unchanged. Refuses a non-finite residual (`NonFiniteResidual`).
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::expected<double, TransientFailure>
physicalStepResidual(const ConductanceMatrix& conductance, const CapacityMatrix& capacity, const ThermalHeatVector& heat,
                     const ThermalFreeEquationMap& equations, std::span<const Temperature> previous,
                     std::span<const Temperature> next, double timeStep);

/// The acceptance of ONE candidate step without a solve: accepts or refuses
/// @p next as the state at the end of a step of @p timeStep from @p previous.
///
/// Every component finite; every prescribed unknown holding its prescribed
/// temperature exactly; the reduced free residual and the physical free residual
/// both computed from the ORIGINAL arrays, finite, and within the tolerance.
/// A backend's "success" is not an input. This is how a deliberately wrong finite
/// step is shown to be refused, and it is the same function every step of a real
/// run is accepted by.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::expected<TransientStepResidual, TransientFailure>
validateTransientStep(const ConductanceMatrix& conductance, const CapacityMatrix& capacity,
                      const ThermalHeatVector& heat, const ThermalFreeEquationMap& equations,
                      const ThermalConstraintMap& constraints, std::span<const Temperature> previous,
                      std::span<const Temperature> next, double timeStep, const ThermalSolverSettings& settings);

// ---------------------------------------------------------------------------
// The algebraic run
// ---------------------------------------------------------------------------

/// The history of one algebraic run: the grid, the stored states and the evidence.
/// A plain value with NO provenance; `TransientThermalSolution` binds it to a source.
struct ThermalTransientAlgebra {
    TimeGrid grid;
    /// The sample times in seconds, ascending. `FinalOnly`: `{t_end}`.
    /// `EveryStep`: `t_0 ... t_N`.
    std::vector<Time> sampleTimes{};
    /// `sampleTimes.size() * dofCount` temperatures, sample-major, each sample in
    /// `ThermalDofIndex` order.
    std::vector<Temperature> samples{};
    std::size_t dofCount = 0;
    /// One record per step, in order.
    std::vector<TransientStepRecord> steps{};
    /// The resolved initial state: the initial temperature with every prescribed
    /// unknown replaced by its prescription.
    std::vector<Temperature> initial{};
    /// How many unknowns had an initial value different from their prescription.
    std::size_t initialOverrides = 0;
    std::size_t freeEquations = 0;
    /// Steps for which the linear kernel was invoked (all, unless nothing is free).
    std::size_t kernelSolves = 0;

    friend bool operator==(const ThermalTransientAlgebra&, const ThermalTransientAlgebra&) = default;
};

/// Advances `C dT/dt + K T = Q` along @p grid from @p initial.
///
/// THE ALGEBRAIC SEAM, PUBLIC DELIBERATELY, for the reason `solveThermalAlgebra`
/// is: a real mesh cannot pose a system whose backward-Euler step is known in
/// closed form, and the one-unknown, two-unknown and three-unknown oracles the
/// milestone requires need a way in. It is the SAME code path the model-level
/// solve runs.
///
/// THE ORDER OF THE CHECKS:
///
/// ```text
/// 1  the settings
/// 2  K, C, Q, the numbering, the constraints and the initial state agree in size
/// 3  the initial state is a finite absolute temperature per unknown; the prescribed
///    unknowns are overwritten by their prescriptions (PRESCRIBED DOMINATES AT t = 0)
/// 4  the history size is within the guard
/// then, for each step with its own dt:
/// 5  A = C / dt + K  and  b = Q + (C / dt) T[n]  (the HISTORY term uses T[n], never T[n+1])
/// 6  the exact reduction, and the factorisation with its pivot gate
/// 7  reconstruction, the constraints held exactly
/// 8  BOTH independent free residuals, finiteness
/// ```
///
/// A step that fails fails the RUN: there is no retry, no smaller step and no
/// partial result. Zero free unknowns is a success: `T = Tc` at every state and no
/// linear solve.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::expected<ThermalTransientAlgebra, TransientFailure>
solveThermalTransientAlgebra(const ConductanceMatrix& conductance, const CapacityMatrix& capacity,
                             const ThermalHeatVector& heat, const ThermalFreeEquationMap& equations,
                             const ThermalConstraintMap& constraints, std::span<const Temperature> initial,
                             const TimeGrid& grid, TransientOutputPolicy output, const ThermalSolverSettings& settings);

// ---------------------------------------------------------------------------
// Provenance
// ---------------------------------------------------------------------------

/// What a transient solution was computed from. A VALUE, not a hash.
///
/// The assembled system's own source (mesh, body, geometry, mode, material, loads,
/// convection) plus the things the system deliberately does not carry: the
/// prescribed-temperature conditions, the initial condition, the transient
/// settings (step, end time, output policy) and the solver settings.
struct TransientSolutionSource {
    ThermalSystemSource system{};
    std::vector<ThermalBoundaryCondition> temperatures{};
    UniformInitialTemperature initial{};
    TransientSettings transient{};
    ThermalSolverSettings settings{};

    friend bool operator==(const TransientSolutionSource&, const TransientSolutionSource&) = default;
};

/// Which part of a transient solution's source no longer matches.
enum class TransientStaleReason : std::uint8_t {
    Body,
    Mesh,
    Geometry,
    Mode,
    Material,
    Loads,
    Convections,
    PrescribedTemperatures,
    InitialCondition,
    TimeStep,
    EndTime,
    OutputPolicy,
    Settings,
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(TransientStaleReason reason) noexcept;

/// Every field of @p solved that differs from @p current, in the enum's order.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::vector<TransientStaleReason>
staleReasons(const TransientSolutionSource& solved, const TransientSolutionSource& current);

// ---------------------------------------------------------------------------
// TransientThermalSolution
// ---------------------------------------------------------------------------

/// The temperature history of one transient analysis, bound to what it was
/// computed from.
///
/// POSSESSION IS THE EVIDENCE that the system and its constraints agreed, the
/// time grid was valid and ended exactly at `t_end`, every step's temperatures are
/// finite, every prescribed unknown held its value at every state and both
/// independent residuals passed at every step. There is no public constructor and
/// one friend: a half-run history is unrepresentable.
///
/// IMMUTABLE. NOT a `ThermalResult` (that type carries a gradient and a flux per
/// element and a heat balance, which are P18-POST-001's and P18-BALANCE-001's);
/// this is the solve's own derived value, as `SteadyThermalSolution` is.
class BETTERCAD_THERMAL_EXPORT TransientThermalSolution {
public:
    [[nodiscard]] const TransientSolutionSource& source() const noexcept { return source_; }
    [[nodiscard]] const TimeGrid& grid() const noexcept { return data_.grid; }

    [[nodiscard]] std::size_t dofCount() const noexcept { return data_.dofCount; }
    [[nodiscard]] std::size_t freeEquations() const noexcept { return data_.freeEquations; }
    [[nodiscard]] std::size_t constrainedCount() const noexcept { return data_.dofCount - data_.freeEquations; }

    /// How many states are stored.
    [[nodiscard]] std::size_t sampleCount() const noexcept { return data_.sampleTimes.size(); }
    /// The time of sample @p index, in seconds; ascending, the last is `t_end`.
    [[nodiscard]] Time timeOf(std::size_t index) const noexcept;
    [[nodiscard]] std::span<const Time> sampleTimes() const noexcept { return data_.sampleTimes; }
    /// `T`, K, in `ThermalDofIndex` order, at sample @p index; empty if out of range.
    [[nodiscard]] std::span<const Temperature> temperaturesAt(std::size_t index) const noexcept;
    /// The state at `t_end`.
    [[nodiscard]] std::span<const Temperature> finalTemperatures() const noexcept;
    /// The resolved initial state (after the prescriptions were applied).
    [[nodiscard]] std::span<const Temperature> initialTemperatures() const noexcept { return data_.initial; }
    [[nodiscard]] std::size_t initialOverrides() const noexcept { return data_.initialOverrides; }

    /// The temperature of unknown @p dof at sample @p index, or an error.
    [[nodiscard]] Result<Temperature> temperatureOf(std::size_t index, ThermalDofIndex dof) const;
    /// The temperature of @p node under @p map at sample @p index, or an error if
    /// @p map is not this solution's numbering.
    [[nodiscard]] Result<Temperature> temperatureOf(std::size_t index, const ThermalDofMap& map, meshing::NodeId node) const;

    /// One record per step.
    [[nodiscard]] std::span<const TransientStepRecord> steps() const noexcept { return data_.steps; }
    [[nodiscard]] std::size_t kernelSolves() const noexcept { return data_.kernelSolves; }
    /// The worst residual over the run.
    [[nodiscard]] double worstReducedResidual() const noexcept;
    [[nodiscard]] double worstPhysicalResidual() const noexcept;

    [[nodiscard]] bool describes(const ThermalDofMap& map) const noexcept {
        return map.stamp() == source_.system.mesh && map.dofCount() == data_.dofCount;
    }

    friend bool operator==(const TransientThermalSolution&, const TransientThermalSolution&) = default;

private:
    TransientThermalSolution(TransientSolutionSource source, ThermalTransientAlgebra data)
        : source_(std::move(source)), data_(std::move(data)) {}

    friend BETTERCAD_THERMAL_EXPORT std::expected<TransientThermalSolution, TransientFailure>
    solveTransientThermalSystem(const ThermalSystem& system, const ThermalDofMap& map,
                                const ThermalConstraintMap& constraints, const UniformInitialTemperature& initial,
                                const TransientSettings& transient, const ThermalSolverSettings& settings,
                                std::vector<ThermalBoundaryCondition> temperatureConditions);

    TransientSolutionSource source_{};
    ThermalTransientAlgebra data_;
};

/// Runs the transient analysis on an assembled TRANSIENT @p system.
///
/// The seam the tests drive with a hand-built mesh. It validates the canonical
/// settings (`0 < dt <= t_end`, both finite: P18-DATA-001's rule), the initial
/// temperature and the solver settings; checks the system carries a capacity and
/// describes @p map and @p constraints belong to it; builds the grid and runs the
/// algebra. @p temperatureConditions are the canonical prescribed-temperature
/// conditions the constraints came from, recorded in the source.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::expected<TransientThermalSolution, TransientFailure>
solveTransientThermalSystem(const ThermalSystem& system, const ThermalDofMap& map, const ThermalConstraintMap& constraints,
                            const UniformInitialTemperature& initial, const TransientSettings& transient,
                            const ThermalSolverSettings& settings,
                            std::vector<ThermalBoundaryCondition> temperatureConditions = {});

/// Runs the transient analysis of @p model.
///
/// THE CHECKS BEFORE ANY STEP, in this order: the settings; the assembled system
/// against the model, loads, conditions and material for TRANSIENT mode
/// (`requireCurrent`, so a remesh or a load, convection, conductivity, density or
/// specific-heat edit is refused even when every dimension agrees); the prepared
/// boundary against the same conditions; then the seam.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<TransientThermalSolution>
solveTransientThermal(const Document& document, const ThermalModel& model, const ThermalDofMap& map,
                      const ThermalSystem& system, const PreparedThermalBoundary& preparedBoundary,
                      std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
                      const UniformInitialTemperature& initial, const TransientSettings& transient,
                      const ThermalSolverSettings& settings);

/// The same, reading the loads, conditions, initial condition, transient settings
/// and solver settings from @p definition, which must be a transient one.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<TransientThermalSolution>
solveTransientThermal(const Document& document, const ThermalModel& model, const ThermalDofMap& map,
                      const ThermalSystem& system, const PreparedThermalBoundary& preparedBoundary,
                      const ThermalAnalysisDefinition& definition);

/// The full refusal `solveTransientThermal` would report, or none.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::optional<TransientFailure>
transientSolveFailure(const Document& document, const ThermalModel& model, const ThermalDofMap& map,
                      const ThermalSystem& system, const PreparedThermalBoundary& preparedBoundary,
                      std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
                      const UniformInitialTemperature& initial, const TransientSettings& transient,
                      const ThermalSolverSettings& settings);

/// The same, for a definition: `NotTransientDefinition` if it is not a transient
/// one (steady mode, or the initial condition or transient settings absent).
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::optional<TransientFailure>
transientSolveFailure(const Document& document, const ThermalModel& model, const ThermalDofMap& map,
                      const ThermalSystem& system, const PreparedThermalBoundary& preparedBoundary,
                      const ThermalAnalysisDefinition& definition);

/// The source a solution would carry if it were computed now (transient mode).
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<TransientSolutionSource>
currentTransientSolutionSource(const Document& document, const ThermalModel& model,
                               std::span<const ThermalLoad> loads,
                               std::span<const ThermalBoundaryCondition> conditions,
                               const UniformInitialTemperature& initial, const TransientSettings& transient,
                               const ThermalSolverSettings& settings);

[[nodiscard]] BETTERCAD_THERMAL_EXPORT bool
isCurrent(const TransientThermalSolution& solution, const Document& document, const ThermalModel& model,
          std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
          const UniformInitialTemperature& initial, const TransientSettings& transient,
          const ThermalSolverSettings& settings);

[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<void>
requireCurrent(const TransientThermalSolution& solution, const Document& document, const ThermalModel& model,
               std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
               const UniformInitialTemperature& initial, const TransientSettings& transient,
               const ThermalSolverSettings& settings);

} // namespace bettercad::thermal
