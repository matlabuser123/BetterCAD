#pragma once

// P18-SOLVE-001 -- the steady thermal solve of `K T = Q` after prescribed
// temperatures.
//
// WHAT THIS IS. The exact free-system reduction of the assembled global system,
// a direct sparse solve of `Kff`, an INDEPENDENT residual validation, and the
// full nodal temperature vector reconstructed with the prescribed temperatures
// held EXACTLY. Derived and disposable: nothing here is persisted, undone or
// canonical.
//
// THE PARTITION, STATED ONCE.
//
// ```text
//     [ Kff  Kfc ] [ Tf ]   [ Qf ]
//     [ Kcf  Kcc ] [ Tc ] = [ Qc ]
//
//     Kff Tf  =  Qf - Kfc Tc   =  beff
// ```
//
// UNLIKE P17, Tc IS NOT ZERO. A structural restraint holds a displacement at
// zero, so P17's right-hand side needs no correction. A prescribed temperature is
// arbitrary -- 350 K stays 350 K -- so `- Kfc Tc` is the term that makes the
// solution depend on the boundary values at all, and omitting it would solve the
// problem with every prescribed node at 0 K. The one-free-node oracle in the
// tests (`Kff=[2] Kfc=[-2] Qf=[100] Tc=[300]` gives `beff=700`, `Tf=350`, where
// the omission gives 50) exists to make that omission impossible to miss.
//
// THE FREE NUMBERING IS P18-DOF-001'S. `ThermalFreeEquationMap` is built by its one
// factory and read here; this module has no second opinion about which unknown is
// row `e`. Free and constrained unknowns are interleaved freely: nothing assumes
// the prescribed ones are first, last or contiguous.
//
// A LIBRARY SAYING "Success" IS NEVER SUFFICIENT EVIDENCE. The kernel is
// `linalg::solveSymmetricSparse` (ADR-052), P17's qualified solver moved below this
// layer, with its three independent conditions: the factorisation and its pivot
// ratio, finiteness, and a residual that BetterCAD computes from the original
// arrays. On top of that this module adds the questions only a thermal model can
// ask:
//
// ```text
//   well posed?     every independent region of the conduction network must be
//                   anchored to an absolute temperature -- a prescribed
//                   temperature or a positive convection coefficient. A pure
//                   Neumann model has the constant-temperature mode in its null
//                   space and is REFUSED, not given an arbitrary level.
//   constraints?    every prescribed unknown holds its prescribed temperature
//                   exactly, bit for bit.
// ```
//
// THE RESIDUAL IS ON THE FREE SYSTEM. `K T - Q` over the FULL system is correctly
// non-zero at the prescribed unknowns: those entries are the heat rates the
// boundary supplies (the reactions), and failing a correct solution because they
// are non-zero would be the classic mistake. The gate is `Kff Tf - beff`; the full
// residual is retained for the reaction and balance milestones to interpret.
//
// WHAT THIS IS NOT.
//
//     no penalty                no `Kii += 1e20`; constraints are applied by exact reduction
//     no pinning                an unanchored model FAILS; no unknown is fixed
//     no regularisation         no `Kff += eps*I`, no weak spring, no pseudo-inverse
//     no hidden datum           no reference temperature is chosen for the user
//     no post-processing        no gradient, no flux (P18-POST-001)
//     no reactions, no balance  the full residual is retained, not aggregated
//     no transient              no C / dt (P18-TRANSIENT-001)
//     no GUI, no CLI            no Qt, no formatting, no progress dialog

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
#include <vector>

namespace bettercad::thermal {

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

/// Why a steady thermal solve was refused.
///
/// STABLE NAMES: a CLI will print them and a script will compare them. The
/// first five are checked before any arithmetic, in this order; the rest classify
/// what the arithmetic found.
enum class SteadySolveProblem : std::uint8_t {
    /// The solver settings are not finite and positive.
    InvalidSettings,
    /// The system was assembled for a transient analysis. A steady solve of `K T
    /// = Q` from a system that also carries `C` is not what that analysis asked
    /// for; assemble in steady mode.
    NotSteadySystem,
    /// The numbering is not the one the system was assembled against.
    DofMapMismatch,
    /// The prescribed temperatures do not belong to this numbering: another
    /// mesh, or another count.
    ConstraintMismatch,
    /// The assembled system no longer describes the model, loads, conditions or
    /// material it is being solved for. Refused before any arithmetic, even when
    /// every dimension agrees.
    StaleSystem,
    /// The prepared boundary no longer describes the model or the conditions.
    StaleBoundary,
    /// A reduced entry is not finite: `Qf - Kfc Tc` overflowed.
    NonFiniteInput,
    /// No independent region of the model is anchored to an absolute
    /// temperature: no prescribed temperature and no positive convection. The
    /// constant-temperature mode is in the null space and the model has no
    /// unique solution.
    MissingTemperatureDatum,
    /// At least one independent region is not anchored although another is: a
    /// disconnected body, or a region cut off from every prescribed temperature
    /// and every convection face.
    UnanchoredThermalRegion,
    /// The factorisation measured a singular or too ill conditioned system
    /// (an anchored model can still be numerically unusable).
    SingularSystem,
    /// The library rejected the matrix before factorising.
    FactorizationFailure,
    /// A solved temperature is not finite.
    NonFiniteSolution,
    /// A residual component is not finite.
    NonFiniteResidual,
    /// The independent normalized residual exceeded the threshold.
    ResidualTooLarge,
    /// A prescribed unknown does not hold its prescribed temperature. Not
    /// reachable through the solve, whose reconstruction assigns the value; it
    /// is the postcondition `validateSteadySolution` applies to any candidate.
    ConstraintNotPreserved,
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(SteadySolveProblem problem) noexcept;

/// A refusal with its classification.
struct SteadySolveFailure {
    SteadySolveProblem problem = SteadySolveProblem::SingularSystem;
    Error error{};
};

// ---------------------------------------------------------------------------
// The reduced system
// ---------------------------------------------------------------------------

/// `Kff`, `Kfc`, `Qf`, `Tc` and `beff = Qf - Kfc Tc`: the global system restricted
/// to the free unknowns, in `ThermalFreeEquationMap` order.
///
/// EXTRACTED BY COPY into ordinary CSR, for the reason P17 gave: the constraint
/// logic stays out of every inner loop, and the residual and the factorisation
/// read the same arrays. `Kff` is SPARSE; nothing here is dense.
///
/// THE ORDERS ARE THE MAPS', EXACTLY. Row `e` is the unknown
/// `ThermalFreeEquationMap::dofOf(e)`; column `j` of `Kfc` is the `j`-th
/// prescribed unknown of `ThermalConstraintMap::constrained()`, ascending. The
/// extraction visits the full matrix row by row in ascending order, so every
/// inner index comes out ascending without a sort and the sum `Kfc Tc` has a
/// fixed order.
///
/// `K`, `Q` and the prescribed temperatures are READ and never written.
///
/// POSSESSION IS THE EVIDENCE (ADR-036): one friend, no public constructor.
class BETTERCAD_THERMAL_EXPORT ReducedThermalSystem {
public:
    using Index = ThermalSparseMatrix::Index;

    /// Rows of the reduced system: `ThermalFreeEquationMap::freeCount()`.
    [[nodiscard]] std::size_t rows() const noexcept { return rows_; }
    /// Prescribed unknowns: the columns of `Kfc`.
    [[nodiscard]] std::size_t constrained() const noexcept { return tc_.size(); }

    /// `Kff` in CSR, `W/K`.
    [[nodiscard]] std::span<const Index> rowStart() const noexcept { return rowStart_; }
    [[nodiscard]] std::span<const Index> innerIndices() const noexcept { return inner_; }
    [[nodiscard]] std::span<const double> values() const noexcept { return values_; }
    [[nodiscard]] std::size_t nonZeros() const noexcept { return values_.size(); }
    /// `Kff(row, column)` in W/K, or zero outside the pattern.
    [[nodiscard]] double kffAt(std::size_t row, std::size_t column) const noexcept;

    /// `Kfc` in CSR (rows are free equations, columns are prescribed ordinals), `W/K`.
    [[nodiscard]] std::span<const Index> couplingRowStart() const noexcept { return couplingRowStart_; }
    [[nodiscard]] std::span<const Index> couplingInner() const noexcept { return couplingInner_; }
    [[nodiscard]] std::span<const double> couplingValues() const noexcept { return couplingValues_; }
    /// `Kfc(row, ordinal)` in W/K, or zero outside the pattern.
    [[nodiscard]] double kfcAt(std::size_t row, std::size_t ordinal) const noexcept;

    /// `Qf`, W.
    [[nodiscard]] std::span<const double> heat() const noexcept { return qf_; }
    /// `Tc`, K, in `ThermalConstraintMap::constrained()` order.
    [[nodiscard]] std::span<const double> prescribed() const noexcept { return tc_; }
    /// `beff = Qf - Kfc Tc`, W.
    [[nodiscard]] std::span<const double> effectiveHeat() const noexcept { return beff_; }

    friend bool operator==(const ReducedThermalSystem&, const ReducedThermalSystem&) = default;

private:
    ReducedThermalSystem() = default;

    friend BETTERCAD_THERMAL_EXPORT std::expected<ReducedThermalSystem, SteadySolveFailure>
    reduceThermalSystem(const ThermalSparseMatrix& conductance, const ThermalHeatVector& heat,
                        const ThermalFreeEquationMap& equations, const ThermalConstraintMap& constraints);

    std::size_t rows_ = 0;
    std::vector<Index> rowStart_{};
    std::vector<Index> inner_{};
    std::vector<double> values_{};
    std::vector<Index> couplingRowStart_{};
    std::vector<Index> couplingInner_{};
    std::vector<double> couplingValues_{};
    std::vector<double> qf_{};
    std::vector<double> tc_{};
    std::vector<double> beff_{};
};

/// Restricts @p conductance and @p heat to the unknowns @p equations leaves
/// free.
///
/// Refuses a numbering that is not the complement of the constraints
/// (`ConstraintMismatch`), a matrix or vector of another size
/// (`DofMapMismatch`), and a reduced right-hand side that overflowed
/// (`NonFiniteInput`).
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::expected<ReducedThermalSystem, SteadySolveFailure>
reduceThermalSystem(const ThermalSparseMatrix& conductance, const ThermalHeatVector& heat,
                    const ThermalFreeEquationMap& equations, const ThermalConstraintMap& constraints);

// ---------------------------------------------------------------------------
// Well-posedness
// ---------------------------------------------------------------------------

/// How the independent regions of the reduced conduction network are anchored.
///
/// A REGION is a connected component of the graph of the NONZERO entries of
/// `Kff`; it is ANCHORED if some row of it does not sum to zero. A conduction
/// matrix sums to zero along every row of a region that is connected to nothing
/// (a constant temperature changes no flux), so a row that does not is coupled to
/// a prescribed unknown (`- Kfc` contributes to the row sum) or to a convection
/// face (`h A` does). This is the thermal meaning of "has a datum", read from the
/// matrix rather than inferred from geometry, so it also covers synthetic systems
/// and a mesh P16 never produced.
struct AnchorAnalysis {
    /// Connected components of the reduced network.
    std::size_t regions = 0;
    /// Components with an anchoring row.
    std::size_t anchored = 0;
    /// Components with none.
    std::size_t unanchored = 0;
    /// The smallest free equation in the first unanchored component, if any.
    std::optional<std::size_t> firstUnanchoredEquation{};

    friend bool operator==(const AnchorAnalysis&, const AnchorAnalysis&) = default;
};

/// The relative size of a row sum, against the row's absolute sum, above which a
/// row anchors its region: `|sum_j Kff(i,j)| > kAnchorTolerance * sum_j |Kff(i,j)|`.
///
/// MEASURED, NOT CHOSEN FOR CONVENIENCE: a conduction row sums to zero to rounding
/// (~1e-16 of its absolute sum, measured at 2.2e-16 on the reference block), and
/// the weakest anchor a model can sensibly carry -- one convection face of
/// `h A / k L ~ 1e-6` -- is nine orders larger. The constant sits between them. It
/// decides only the CLASSIFICATION (datum versus generic singularity) and the
/// early refusal of an unanchored region; the factorisation's pivot gate and the
/// independent residual are separate and are not relaxed by it.
inline constexpr double kAnchorTolerance = 1.0e-10;

[[nodiscard]] BETTERCAD_THERMAL_EXPORT AnchorAnalysis analyseAnchoring(const ReducedThermalSystem& reduced);

// ---------------------------------------------------------------------------
// The solved field
// ---------------------------------------------------------------------------

/// The independently computed residual of the FREE system, `r = Kff Tf - beff`.
///
/// COMPUTED BY BETTERCAD, NOT READ OUT OF THE SOLVER: a loop over the reduced CSR
/// against `beff` after the solve returned.
struct SteadyResidual {
    /// `||r||_2`, W.
    Power euclideanNorm{};
    /// `||r||_inf`, W.
    Power infinityNorm{};
    /// `||Kff||_inf`, W/K.
    ThermalConductance conductanceInfinityNorm{};
    /// `||Tf||_2`, K.
    Temperature temperatureNorm{};
    /// `||beff||_2`, W.
    Power heatNorm{};
    /// `||r||_2 / (||Kff||_inf ||Tf||_2 + ||beff||_2)`, dimensionless and scale
    /// free: scaling `K` and `Q` by `c` leaves it unchanged. Zero when the system
    /// has no free equations.
    double normalized = 0.0;

    friend bool operator==(const SteadyResidual&, const SteadyResidual&) = default;
};

/// The temperature field of one algebraic solve, with the evidence that it is a
/// solution. A plain value: it carries NO provenance. `SteadyThermalSolution` is
/// the one that binds it to a source.
struct ThermalAlgebraicSolution {
    /// `T`, in K, parallel to `ThermalDofIndex`: entry `d` is the temperature of
    /// unknown `d`. Prescribed unknowns hold their prescribed value EXACTLY.
    std::vector<Temperature> temperatures{};
    SteadyResidual residual{};
    /// `min|D|/max|D|` of the factorisation; `1.0` with no free equation.
    double pivotRatio = 1.0;
    /// `K T - Q` over the FULL system, W: ~0 at free unknowns, the boundary heat
    /// rates at prescribed ones. Retained, not interpreted.
    std::vector<double> fullResidual{};
    std::size_t freeEquations = 0;
    std::size_t freeNonZeros = 0;
    /// Whether the linear kernel was invoked: false for a fully prescribed model.
    bool kernelInvoked = false;
    AnchorAnalysis anchoring{};

    friend bool operator==(const ThermalAlgebraicSolution&, const ThermalAlgebraicSolution&) = default;
};

/// Solves `K T = Q` subject to @p constraints, from the matrix and vector alone.
///
/// THE ALGEBRAIC SEAM, PUBLIC DELIBERATELY: a real mesh cannot pose a system whose
/// solution is known in closed form (the smallest assembled model has dozens of
/// unknowns), so the 2x2, 3x3 and one-free-node validations the milestone
/// requires need a way in. This is it, and it is the SAME code path
/// `solveSteadyThermalSystem` runs, not a test-only back door.
///
/// THE ORDER OF THE CHECKS:
///
/// ```text
/// 1  the settings, on their own terms
/// 2  the matrix, the vector, the numbering and the constraints agree
/// 3  the reduced system, and its right-hand side finite
/// 4  NO FREE EQUATIONS: T = Tc, nothing handed to the kernel
/// 5  every region anchored            (MissingTemperatureDatum / UnanchoredThermalRegion)
/// 6  the factorisation, its pivot ratio, finiteness, the independent residual
/// 7  reconstruction, the constraints held exactly, and the full residual
/// ```
///
/// ZERO FREE EQUATIONS IS A SUCCESS: every unknown prescribed means `T = Tc` is the
/// solution and nothing is to be solved. The free residual is empty and accepted
/// trivially; `Q` is irrelevant to `T` and relevant to the reactions, which a
/// later milestone reads from `fullResidual`.
///
/// AN EMPTY CONSTRAINT SET IS NOT REFUSED. A model anchored by convection alone is
/// well posed; what is refused is the absence of ANY anchor, found in the numbers.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::expected<ThermalAlgebraicSolution, SteadySolveFailure>
solveThermalAlgebra(const ThermalSparseMatrix& conductance, const ThermalHeatVector& heat,
                    const ThermalFreeEquationMap& equations, const ThermalConstraintMap& constraints,
                    const ThermalSolverSettings& settings);

/// The acceptance step WITHOUT a solve: accepts or refuses a CANDIDATE full
/// temperature vector @p temperatures.
///
/// Every component finite; every prescribed unknown holding its prescribed
/// temperature exactly; the free residual computed independently from `Kff`, `Tf`
/// and `beff`, finite, and within the tolerance. A solver's "success" is not an
/// input. This is how a deliberately wrong finite vector is shown to be refused.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::expected<SteadyResidual, SteadySolveFailure>
validateSteadySolution(const ThermalSparseMatrix& conductance, const ThermalHeatVector& heat,
                       const ThermalFreeEquationMap& equations, const ThermalConstraintMap& constraints,
                       std::span<const Temperature> temperatures, const ThermalSolverSettings& settings);

// ---------------------------------------------------------------------------
// Provenance
// ---------------------------------------------------------------------------

/// What a steady solution was computed from.
///
/// A VALUE, NOT A HASH. It binds the assembled system's own source (mesh, body,
/// geometry, mode, material, loads, convection) and, beyond it, the two things
/// the system deliberately does not carry:
///
/// ```text
/// temperatures   the canonical PRESCRIBED-TEMPERATURE conditions, by value. A
///                T_bar edit does not stale the assembled system (it never enters
///                K, C or Q) but it changes `Tc`, so it stales the solution.
/// settings       the tolerances, which decide acceptance.
/// ```
struct SteadySolutionSource {
    ThermalSystemSource system{};
    std::vector<ThermalBoundaryCondition> temperatures{};
    ThermalSolverSettings settings{};

    friend bool operator==(const SteadySolutionSource&, const SteadySolutionSource&) = default;
};

/// Which part of a solution's source no longer matches.
enum class SolutionStaleReason : std::uint8_t {
    Body,
    Mesh,
    Geometry,
    Mode,
    Material,
    Loads,
    Convections,
    PrescribedTemperatures,
    Settings,
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(SolutionStaleReason reason) noexcept;

/// Every field of @p solved that differs from @p current, in the enum's order.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::vector<SolutionStaleReason>
staleReasons(const SteadySolutionSource& solved, const SteadySolutionSource& current);

// ---------------------------------------------------------------------------
// SteadyThermalSolution
// ---------------------------------------------------------------------------

/// The steady temperature field of one analysis, bound to what it was computed
/// from.
///
/// POSSESSION IS THE EVIDENCE that the system and its constraints agreed, the
/// model was well posed, the factorisation passed its pivot gate, every
/// temperature is finite, every prescribed unknown holds its value and the
/// independent residual is within tolerance. There is no public constructor and
/// one friend, so a half-published solution is unrepresentable.
///
/// IMMUTABLE: every accessor is const and returns a view or a value.
///
/// NOT a `ThermalResult`. That type (P18-DATA-001) carries a gradient and a flux
/// per element and a heat balance, which are P18-POST-001's and P18-BALANCE-001's;
/// a half-filled one would claim to be a result and not be. This is the solve's
/// own derived value, as P17's `SolvedSystem` was before `StructuralResult`.
class BETTERCAD_THERMAL_EXPORT SteadyThermalSolution {
public:
    [[nodiscard]] const SteadySolutionSource& source() const noexcept { return source_; }

    /// `Ndof`.
    [[nodiscard]] std::size_t dofCount() const noexcept { return solution_.temperatures.size(); }
    [[nodiscard]] std::size_t freeEquations() const noexcept { return solution_.freeEquations; }
    [[nodiscard]] std::size_t constrainedCount() const noexcept { return dofCount() - freeEquations(); }

    /// `T`, K, parallel to `ThermalDofIndex` and therefore to `ThermalDofMap::nodes()`
    /// and `mesh.nodes()`.
    [[nodiscard]] std::span<const Temperature> temperatures() const noexcept { return solution_.temperatures; }
    /// The temperature of unknown @p dof, or an error outside the numbering.
    [[nodiscard]] Result<Temperature> temperatureOf(ThermalDofIndex dof) const;
    /// The temperature of @p node under @p map, or an error if @p map is not the
    /// numbering this solution belongs to or does not hold the node.
    [[nodiscard]] Result<Temperature> temperatureOf(const ThermalDofMap& map, meshing::NodeId node) const;

    [[nodiscard]] const SteadyResidual& residual() const noexcept { return solution_.residual; }
    [[nodiscard]] double pivotRatio() const noexcept { return solution_.pivotRatio; }
    [[nodiscard]] std::size_t freeSystemNonZeros() const noexcept { return solution_.freeNonZeros; }
    [[nodiscard]] bool kernelInvoked() const noexcept { return solution_.kernelInvoked; }
    [[nodiscard]] const AnchorAnalysis& anchoring() const noexcept { return solution_.anchoring; }

    /// `K T - Q` over the FULL system, W: ~0 at free unknowns; at prescribed
    /// unknowns the heat rate the boundary supplies. Retained, not aggregated.
    [[nodiscard]] std::span<const double> fullResidual() const noexcept { return solution_.fullResidual; }

    [[nodiscard]] bool describes(const ThermalDofMap& map) const noexcept {
        return map.stamp() == source_.system.mesh && map.dofCount() == dofCount();
    }

    friend bool operator==(const SteadyThermalSolution&, const SteadyThermalSolution&) = default;

private:
    SteadyThermalSolution(SteadySolutionSource source, ThermalAlgebraicSolution solution)
        : source_(std::move(source)), solution_(std::move(solution)) {}

    friend BETTERCAD_THERMAL_EXPORT std::expected<SteadyThermalSolution, SteadySolveFailure>
    solveSteadyThermalSystem(const ThermalSystem& system, const ThermalDofMap& map,
                             const ThermalConstraintMap& constraints, const ThermalSolverSettings& settings,
                             std::vector<ThermalBoundaryCondition> temperatureConditions);

    SteadySolutionSource source_{};
    ThermalAlgebraicSolution solution_{};
};

/// Solves the assembled @p system subject to @p constraints.
///
/// The seam the tests drive with a hand-built mesh: no `ThermalModel`, no
/// document. It checks that @p system is a STEADY system (the source says so),
/// that it describes @p map, and that @p constraints belong to @p map, then
/// runs the algebraic solve. @p temperatureConditions are the canonical
/// prescribed-temperature conditions the constraints came from, recorded in the
/// solution's source (empty for a hand-built constraint map).
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::expected<SteadyThermalSolution, SteadySolveFailure>
solveSteadyThermalSystem(const ThermalSystem& system, const ThermalDofMap& map,
                         const ThermalConstraintMap& constraints, const ThermalSolverSettings& settings,
                         std::vector<ThermalBoundaryCondition> temperatureConditions = {});

/// Solves the steady thermal problem of @p model.
///
/// THE CHECKS BEFORE ANY ARITHMETIC, in this order: the settings; the assembled
/// system against the model, the loads and the conditions (`requireCurrent`, so a
/// remesh, a load, a convection edit or a material edit is refused even when
/// every dimension agrees); the prepared boundary against the same conditions;
/// then the seam. The prescribed temperatures are the PREPARED BOUNDARY'S
/// constraint map -- the one P18-BC-001 resolved from the canonical conditions --
/// and not a second resolution.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<SteadyThermalSolution>
solveSteadyThermal(const Document& document, const ThermalModel& model, const ThermalDofMap& map,
                   const ThermalSystem& system, const PreparedThermalBoundary& preparedBoundary,
                   std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
                   const ThermalSolverSettings& settings);

/// The full refusal `solveSteadyThermal` would report, or none if it would
/// succeed. The same checks in the same order.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::optional<SteadySolveFailure>
steadySolveFailure(const Document& document, const ThermalModel& model, const ThermalDofMap& map,
                   const ThermalSystem& system, const PreparedThermalBoundary& preparedBoundary,
                   std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
                   const ThermalSolverSettings& settings);

/// The source a solution would carry if it were computed now. Resolves the
/// material through P18-MAT-001.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<SteadySolutionSource>
currentSteadySolutionSource(const Document& document, const ThermalModel& model,
                            std::span<const ThermalLoad> loads,
                            std::span<const ThermalBoundaryCondition> conditions,
                            const ThermalSolverSettings& settings);

/// Whether @p solution still describes @p model, @p loads, @p conditions and
/// @p settings. False if the material no longer resolves.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT bool
isCurrent(const SteadyThermalSolution& solution, const Document& document, const ThermalModel& model,
          std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
          const ThermalSolverSettings& settings);

/// `Ok` if @p solution is current, otherwise a `FailedPrecondition` naming each
/// reason that moved.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<void>
requireCurrent(const SteadyThermalSolution& solution, const Document& document, const ThermalModel& model,
               std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
               const ThermalSolverSettings& settings);

} // namespace bettercad::thermal
