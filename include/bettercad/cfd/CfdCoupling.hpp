#pragma once

/// \file
/// P19-PVCOUPLE-001 — Pressure–Velocity Coupling. ADR-075.
///
/// SIMPLE, for steady incompressible flow: the predictor, the pressure
/// correction, the velocity and face-flux corrections, the under-relaxation,
/// the residuals and the criteria that stop or refuse an outer iteration.
///
/// THREE THINGS THIS DOES NOT CONTAIN, AND EACH IS SOMEONE ELSE'S MILESTONE.
///
/// ```text
/// no linear solver        A solve is SUPPLIED by the caller as a
///                         `CfdLinearSolve`. `src/cfd/CMakeLists.txt` records
///                         that admitting a solve kernel is
///                         `P19-LINSOLVE-001`'s decision, and that milestone's
///                         checklist owns "Pressure-system solver" and
///                         "Momentum-system solver" by name. `BetterCAD::linalg`
///                         is not linked by `cfd` and no `linalg` header is
///                         included here. ADR-075 Decision 1.
/// no momentum assembly    `assembleCfdMomentum` is `P19-MOM-001`'s and is
///                         already qualified. This operator takes the three
///                         ASSEMBLED systems and makes a predictor out of
///                         them: relax, solve, expose the diagonal.
/// no outer loop and no    "Convergence loop", "Initialize fields" and
/// result                  "Atomic result publication" are `P19-STEADY-001`'s,
///                         and `CfdFailureStage` lives in `CfdResult.hpp`
///                         because it belongs to the result. This operator
///                         advances ONE outer iteration and reports; the
///                         criteria that a loop would consult are here as
///                         functions. ADR-075 Decision 9.
/// ```
///
/// THE CENTRAL FACT IS AN IDENTITY, NOT AN APPROXIMATION (ADR-075 Decision 5).
/// The face-flux correction adds `sum_f a_f (p'_P - p'_N)` to each cell's flux
/// sum, which is `(A p')_P`, and the system solved is `A p' = -div(phi*)`.
/// Therefore
///
/// ```text
///     div(phi_corrected)_P  ==  (A p')_P - rhs_P  ==  the SOLVE's own residual
/// ```
///
/// **INCLUDING AT A PINNED CELL, AND THAT IS NOT OBVIOUS.** An adversarial
/// review worked the algebra and concluded the identity must fail there: that
/// row of `A` and its right-hand side are cleared, so `(A p') - b` is zero by
/// construction, while the measured flux sum "ought to be" the unpinned
/// system's compatibility residual. A test built the pinned case and MEASURED
/// both sides, and they agree: `-1.08e-19` against `0`.
///
/// The mechanism is the one `ADR-075` Decision 5 already names. Pinning happens
/// only where NO boundary fixes the pressure, which in this boundary set means
/// every boundary prescribes its flux, and the gate's phase 8 requires those to
/// BALANCE. Global conservation then forces the pinned cell's flux sum to the
/// negative of all the others, and those are each zero to solve accuracy. Q6's
/// figures say the same rather than the opposite: `2.020606e-14` at the
/// reference cell against `1.199041e-14` elsewhere is the same order, not a
/// structural break. The compatibility condition is what makes the pinned cell
/// safe, which is why it is a refusal and not a diagnostic.
///
/// Two consequences a reader should not have to derive: continuity closure
/// after correction is **exactly as good as the pressure solve and no better**,
/// so the closure bound is DERIVED from the system's own residual in the same
/// measure rather than hard-coded; and the identity holds for a WRONG `p'` too,
/// which is why a test asserts the identity instead of merely asserting that
/// continuity came out small.
///
/// NOTHING HERE DECLARES A SETTING, A RESIDUAL OR A BALANCE. `ADR-058` already
/// fixed `CfdIterationSettings`, `CfdConvergenceSettings`, `CfdSteadySettings`,
/// `CfdLinearSolverSettings`, `CfdResiduals`, `CfdMassBalance` and
/// `CfdReferencePressure`; this operator BINDS to them. The continuity closure
/// is `evaluateCfdContinuity`'s `CfdMassBalance` — produced by the operator
/// `P19-CONT-001` qualified, which was written before this one existed, so the
/// central claim is checked against a qualified EXTERNAL measure and not
/// against a restatement of its own formula.

#include <bettercad/cfd/CfdBoundary.hpp>
#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdGeometry.hpp>
#include <bettercad/cfd/CfdMomentum.hpp>
#include <bettercad/cfd/CfdSparse.hpp>
#include <bettercad/cfd/CfdTopology.hpp>
#include <bettercad/cfd/Export.hpp>
#include <bettercad/core/Error.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace bettercad::cfd {

// ---------------------------------------------------------------------------
// The supplied solve
// ---------------------------------------------------------------------------

/// What a supplied linear solve returns: the solution, and the residual the
/// SOLVER measured for it.
///
/// THE RESIDUAL IS CARRIED BECAUSE THIS OPERATOR CANNOT RECOMPUTE IT CHEAPLY
/// AND WILL NOT PRETEND TO. `ADR-052`'s kernel computes `||A x - b||`
/// independently of the factorisation and normalises it, and that number is
/// what decides whether continuity could possibly be closed (ADR-075
/// Decision 5). A port that reports a residual it did not measure is lying to
/// this operator, and no check here can detect that; it is recorded as `L4`.
struct CfdLinearSolveResult {
    /// `x`, one entry per row of the system handed to the port.
    std::vector<double> solution{};
    /// The normalised residual the solver measured, dimensionless and not
    /// negative. Compared against
    /// `CfdLinearSolverSettings::relativeResidualTolerance`.
    double normalizedResidual = 0.0;

    friend bool operator==(const CfdLinearSolveResult&, const CfdLinearSolveResult&) = default;
};

/// A caller-supplied solve of `A x = b`.
///
/// `A` is a `CfdSparseMatrix` this operator assembled, `b` is its right-hand
/// side, and the settings are `ADR-058`'s one ratio. A refusal is surfaced as
/// `CfdCouplingProblem::LinearSolveRefused` with the port's own diagnostic
/// attached rather than replaced.
///
/// THE PRESSURE-CORRECTION MATRIX IS SYMMETRIC BY CONSTRUCTION and, once the
/// pressure level is fixed, positive definite — which is exactly what
/// `linalg::solveSymmetricSparse` requires. `CfdSparseIndex` is `uint64_t`,
/// the same width and signedness as `linalg::SparseIndex`, so a port wrapping
/// that kernel hands these spans over without translation or copy. That a
/// momentum system is NOT symmetric is equally deliberate: convection makes it
/// asymmetric (`ADR-072` Decision 3), so a port must accept both and a port
/// that assumes symmetry everywhere is wrong.
using CfdLinearSolve = std::function<std::expected<CfdLinearSolveResult, Error>(
    const CfdSparseMatrix& matrix, std::span<const double> rightHandSide,
    const CfdLinearSolverSettings& settings)>;

// ---------------------------------------------------------------------------
// Refusals
// ---------------------------------------------------------------------------

/// Why a coupling operation is refused.
///
/// IN THE ORDER THE PHASES CHECK THEM, and the order is asserted by a test: an
/// input with two faults reports the earlier one. The same discipline
/// `P19-CONT-001` used, for the same reason — a refusal order that is not
/// pinned is a refusal order that drifts.
enum class CfdCouplingProblem : std::uint8_t {
    /// The geometry describes a different mesh or geometry revision than the
    /// topology does.
    GeometryDescribesAnotherTopology,
    /// The prepared boundary describes a different topology.
    BoundaryDescribesAnotherTopology,
    /// A face names an owner or a neighbour that is not one of the cells.
    ///
    /// GUARDS WRITES, WHICH IS WHY IT IS NOT OPTIONAL. `CfdTopology::owner` is
    /// invalid for a face the topology does not hold, and `CellIndex::kInvalid`
    /// is `SIZE_MAX`; every loop here then writes through that index, into the
    /// symbolic pattern, the right-hand side and the per-cell face sums.
    /// `geometry.describes` compares the mesh stamp and the counts and NEVER
    /// the owner and neighbour ranges, so nothing upstream establishes this.
    ///
    /// `P19-DIV-001`'s review and then `P19-CONV-001`'s review each found an
    /// unguarded `SIZE_MAX` index reaching writes, and the second was that
    /// correction REGRESSING one milestone later. The first draft of this
    /// operator regressed it a THIRD time, and its own adversarial review
    /// found it.
    TopologyNamesAnUnknownCell,
    /// A supplied array is not the length the topology implies.
    ValueLengthWrong,
    /// A supplied value is NaN or infinite. This is the INPUT gate; a value
    /// this operator COMPUTES and finds non-finite is `ResultIsNotFinite`.
    ValueNotFinite,
    /// `CfdIterationSettings` or `CfdLinearSolverSettings` failed its own
    /// `validate`.
    ///
    /// THOSE TWO AND NOT ALL FOUR, and the difference is recorded rather than
    /// glossed. `CfdConvergenceSettings` and `CfdSteadySettings` reach this
    /// operator only through `cfdCouplingVerdict` and
    /// `cfdCouplingBudgetExhausted`, which are `noexcept` and return a verdict
    /// and a `bool`: neither has a channel to refuse through. They state their
    /// precondition instead, and `L7` records what they do when it is not met.
    SettingsInvalid,
    /// Exactly three momentum systems were not supplied, or they are not the
    /// three components in `VectorComponent` order, or one of them is not
    /// square at the cell count.
    MomentumSystemsWrong,
    /// A momentum diagonal is not strictly positive, so `d_P = V_P / a_P` has
    /// no usable value and SIMPLE has no coefficient to correct with.
    ///
    /// A HARD PROBLEM, NOT A DEFECTIVE ASSEMBLY, and an earlier version of this
    /// comment said the opposite. Its own correctness review SUSPECTED that a
    /// legitimate assembly could produce one -- upwind convection puts a
    /// negative `phi` on the diagonal at a zero-gradient-velocity boundary
    /// (`CfdConvection.hpp`) and diffusion adds nothing there -- and this
    /// milestone's own tests then CONFIRMED it: a chain mesh at 5 mm/s with
    /// water assembles an x-momentum diagonal of `-8.085e-04` kg/s.
    /// `CfdMomentumSummary::rowsNotDiagonallyDominant` measures the same thing
    /// and does not refuse it, because an assembly may legitimately be
    /// non-dominant; this operator cannot proceed from it. Recorded as `L9`.
    MomentumDiagonalNotPositive,
    /// `cfdFaceInterpolationWeight` has no weight for an internal face that must
    /// be interpolated: its orthogonal distance is not positive, or it has no
    /// unit normal.
    ///
    /// REFUSED, NOT DEFAULTED. `P19-DIV-001`'s review found a transcription
    /// that returned `0.5` on a degenerate face, and `P19-CONV-001` carries the
    /// same enumerator for the same reason. The weight is the one
    /// `cfdFaceInterpolationWeight` exports (`ADR-069` Decision 7) and is
    /// CALLED, never recomputed: that function had three private copies once.
    FaceWeightUnavailable,
    /// No boundary fixes the pressure level and the prepared boundary carries
    /// no reference cell, so the pressure-correction system is singular in the
    /// constant mode with nothing to pin it to.
    PressureLevelNotFixed,
    /// The pure-Neumann compatibility condition fails: the prescribed boundary
    /// flux does not balance, so `A p' = rhs` has no solution at all. Measured
    /// rather than inferred from the boundary conditions.
    PrescribedFluxUnbalanced,
    /// The assembled pressure-correction matrix is not symmetric within
    /// `kCfdCouplingSymmetryTolerance`. Symmetric by construction, so this is a
    /// defective scatter rather than slack arithmetic.
    PressureMatrixNotSymmetric,
    /// The supplied solve refused, or returned a residual above the setting.
    LinearSolveRefused,
    /// The supplied solve returned a solution of the wrong length, or one
    /// holding a non-finite value.
    LinearSolutionUnusable,
    /// An output this operator computed is not finite although every input was.
    ResultIsNotFinite,
    /// The operation's own arrays disagree with each other. An internal
    /// inconsistency, never a bad input.
    EvaluationIsInconsistent,
};

/// The sixteen, in the order they are checked.
inline constexpr std::array<CfdCouplingProblem, 16> kCfdCouplingProblems{
    CfdCouplingProblem::GeometryDescribesAnotherTopology,
    CfdCouplingProblem::BoundaryDescribesAnotherTopology,
    CfdCouplingProblem::TopologyNamesAnUnknownCell,
    CfdCouplingProblem::ValueLengthWrong,
    CfdCouplingProblem::ValueNotFinite,
    CfdCouplingProblem::SettingsInvalid,
    CfdCouplingProblem::MomentumSystemsWrong,
    CfdCouplingProblem::MomentumDiagonalNotPositive,
    CfdCouplingProblem::FaceWeightUnavailable,
    CfdCouplingProblem::PressureLevelNotFixed,
    CfdCouplingProblem::PrescribedFluxUnbalanced,
    CfdCouplingProblem::PressureMatrixNotSymmetric,
    CfdCouplingProblem::LinearSolveRefused,
    CfdCouplingProblem::LinearSolutionUnusable,
    CfdCouplingProblem::ResultIsNotFinite,
    CfdCouplingProblem::EvaluationIsInconsistent};

// A SEVENTEENTH ENUMERATOR LEFT OUT OF THE ARRAY WOULD OTHERWISE COMPILE:
// `-Wswitch` guards `toString`, not this array, and a test that builds names
// FROM the array can only ever agree with itself. Every sibling operator
// carries this assert for the same reason; the first draft of this one did not,
// and its own adversarial review found that its array test was a tautology.
static_assert(kCfdCouplingProblems.size() == 16,
              "kCfdCouplingProblems must hold every CfdCouplingProblem exactly once");

[[nodiscard]] BETTERCAD_CFD_EXPORT std::string_view toString(CfdCouplingProblem problem) noexcept;

/// A refusal with its classification and the diagnostic that names the cause,
/// so a caller owning the vocabulary — `P19-STEADY-001` maps these onto
/// `CfdFailureStage` — can say it in its own words without parsing a message.
struct CfdCouplingFailure {
    CfdCouplingProblem problem = CfdCouplingProblem::EvaluationIsInconsistent;
    Error error{};
};

// ---------------------------------------------------------------------------
// The measured bounds
// ---------------------------------------------------------------------------

/// The symmetry a correct pressure-correction assembly has.
///
/// MEASURED AT EXACTLY ZERO (ADR-075 Q3), and this bound is therefore not tight
/// to anything. `a_f` is computed ONCE per face and written to both rows in one
/// visit, so the two entries are the same double. The gate exists so that a
/// defective scatter is a refusal rather than a silent asymmetry — the same
/// reason `kCfdDiffusionSymmetryTolerance` carries the same value.
///
/// WHAT IT GUARDS IS NOT HYPOTHETICAL. `ADR-075` Q1 measured a per-cell
/// assembly, which visits each internal face twice and recomputes the
/// interpolation weight from that side, as bit-identical on only 57.3703% of
/// faces, worst relative `1.241051e-14`.
inline constexpr double kCfdCouplingSymmetryTolerance = 1.0e-13;

/// The bound a test holds `CfdGeometry`'s face normals to, for
/// `sum_i n_i^2 - 1`.
///
/// NOT CHECKED BY THIS OPERATOR, AND AN EARLIER COMMENT CLAIMED IT WAS. The
/// normal-projected diagonal (`ADR-075` Decision 11) reduces to the isotropic
/// case because `sum_i n_i^2 == 1`, and this operator RELIES on that without
/// verifying it per face: a non-unit normal would silently scale every face
/// coefficient. The reliance is on `CfdGeometry`'s invariant, which
/// `faceUnitNormal` owns, and a test asserts it over every face of a real mesh
/// rather than over one fixture. Recorded as `L8`.
inline constexpr double kCfdCouplingNormalTolerance = 1.0e-12;

/// The bound on the pure-Neumann compatibility residual, relative to the
/// boundary throughput.
///
/// `ADR-075` Q2 measured `6.217249e-15` for a balanced prescribed flux and
/// `5.327555e+00` for an unbalanced one — five decades of discrimination either
/// side of any bound in between. One decade above the balanced measurement,
/// rounded up to the next power of ten.
inline constexpr double kCfdCouplingCompatibilityTolerance = 1.0e-13;

/// The allowance added to the continuity closure bound, relative to the
/// continuity scale, for the identity's own floating-point gap.
///
/// THE BOUND IS DERIVED LIKE FOR LIKE, AND AN EARLIER FORM WAS NOT. `ADR-075`
/// Decision 5's identity says the corrected flux's per-cell continuity IS the
/// pressure system's residual, so the bound is that residual in THE SAME
/// MEASURE: `max_P |(A p')_P - b_P|` over the same continuity scale, plus this
/// allowance.
///
/// The first form multiplied the SOLVER's residual by a slack of `1e2`, and its
/// own adversarial review showed the derivation did not transfer: the solver
/// reports `||r||_2 / (||A||_inf ||x||_2 + ||b||_2)` (`ADR-052`) while
/// `maxCellContinuity` is `||r||_inf` over the boundary throughput, and the
/// ratio between those two drifts with mesh size. Comparing a quantity to
/// itself in its own norm has no such drift, and needs no slack factor at all.
///
/// `ADR-075` Q6 measured the identity's gap at `1.686762e-14` of the boundary
/// throughput and the worst closure at `2.020606e-14`; this allowance is
/// `1.0e-13`, which is 4.95x the latter rather than the decade an earlier
/// comment claimed. The number is stated as what it is.
inline constexpr double kCfdCouplingClosureFloor = 1.0e-13;

// ---------------------------------------------------------------------------
// The pressure-correction system
// ---------------------------------------------------------------------------

/// What the pressure-correction assembly measured about itself.
struct CfdPressureCorrectionSummary {
    /// The cell count, which is the system's size.
    std::size_t cells = 0;
    /// Internal faces that coupled two cells. Every internal face does, so this
    /// equals the topology's internal face count; it is reported because a
    /// figure that must equal another is a check.
    std::size_t internalFacesCoupled = 0;
    /// Boundary faces whose pressure is `FixedValue`, so `p' = 0` there and the
    /// face contributes to a diagonal alone.
    std::size_t boundaryFacesFixedPressure = 0;
    /// Boundary faces whose pressure is `ZeroGradient`, so the face contributes
    /// NOTHING: the flux there is prescribed and cannot be corrected.
    std::size_t boundaryFacesZeroGradient = 0;
    /// The cell whose `p'` was pinned to zero, present exactly when no boundary
    /// fixes the pressure level. Taken from
    /// `PreparedCfdBoundary::referenceCell()` and never chosen here (ADR-058).
    std::optional<CellIndex> pinnedCell{};
    /// Whether the REFERENCE CELL'S REGION had its constant mode in the null
    /// space before pinning, so that the pin was needed.
    ///
    /// PER REGION, NOT GLOBALLY. An earlier form read a global "did any
    /// boundary face add to a diagonal", which on a mesh with two disconnected
    /// fluid regions concludes "nothing to pin" as soon as EITHER region has a
    /// pressure boundary — leaving the other region's constant mode free and
    /// the matrix singular, which is exactly the precondition
    /// `linalg::solveSymmetricSparse` needs. Its own adversarial review found
    /// that.
    bool constantModeWasInTheNullSpace = false;
    /// Set when the pin was needed AND some other region fixes its pressure by
    /// a boundary: i.e. the mesh is disconnected. Reported rather than refused,
    /// because the gate has already refused the case that is not solvable — a
    /// region with neither a pressure boundary nor the reference cell.
    bool pressureFixedInAnotherRegion = false;
    /// Whether the pinned diagonal had to fall back to a dimensionless `1.0`
    /// because the reference cell's own diagonal was exactly zero: a cell with
    /// no internal face and no fixed-pressure face. Reported because the rule
    /// "the diagonal keeps its own value" has this one exception.
    bool pinnedDiagonalIsUnitValued = false;
    /// `max |A(i,j) - A(j,i)|`. Exactly zero for a correct assembly.
    double largestSymmetryError = 0.0;
    /// `|sum of the right-hand side|`, relative to the boundary throughput: the
    /// pure-Neumann compatibility residual.
    double compatibilityResidual = 0.0;
    /// The worst spread between the three component momentum diagonals at one
    /// cell, `(max_i - min_i) / max_i`, or zero when there are no cells.
    ///
    /// REPORTED BECAUSE THE NORMAL PROJECTION READS THEM AS ONE NUMBER PER
    /// FACE. At zero spread the three agree and `sum_i n_i^2 d^i = d` exactly;
    /// a large spread does not make the projection wrong, but it tells a caller
    /// the isotropic reading of `d` is being strained.
    double largestDiagonalSpread = 0.0;
    /// `0.5 * sum |phi|` over the boundary faces: the scale every relative
    /// measure here divides by, as `ADR-058` fixed it for the mass balance.
    double boundaryThroughput = 0.0;

    friend bool operator==(const CfdPressureCorrectionSummary&,
                           const CfdPressureCorrectionSummary&) = default;
};

/// The assembled pressure-correction system `A p' = b`.
struct CfdPressureCorrectionSystem {
    /// `A`, square at the cell count. Units are such that `A p'` is a mass flow
    /// rate: `a_f = rho d_f |S_f| / |d_f|` times a pressure is kg/s.
    ///
    /// SYMMETRIC, and `fromCsr` is given `kCfdCouplingSymmetryTolerance` so that
    /// the claim is enforced rather than stated. Positive definite once the
    /// pressure level is fixed, which is what makes the already-qualified
    /// `linalg::solveSymmetricSparse` a legal port for it.
    CfdSparseMatrix matrix;
    /// `b = -div(phi*)`, one entry per cell, in kg/s.
    ///
    /// The sign is the one that makes `A p' = b` correct the flux DOWNWARD where
    /// a cell gains mass, and it is fixed by `phi`'s own convention (`ADR-071`):
    /// `phi` is signed out of the owner, so `sum_f phi_f` over a cell is its net
    /// outflow and a positive net outflow needs a negative source.
    std::vector<double> source;
    CfdPressureCorrectionSummary summary;
};

/// What the pressure-correction assembly needs that is not geometry.
struct CfdPressureCorrectionInput {
    /// `phi*`, the PREDICTED mass flux at every face, in SI (kg/s), one per
    /// face, in `FaceIndex` order, internal faces first as `ADR-064` numbers
    /// them, signed out of the owner.
    ///
    /// Produced by `cfdMassFlux` from the predicted velocity. Never re-derived
    /// here: `ADR-072` gives this module exactly one producer of `phi` and a
    /// second one in this file would be the fifth.
    std::span<const double> predictedFaceFlux{};
    /// The momentum diagonals `a_P`, COMPONENT-BLOCKED as `ADR-066` lays them
    /// out (`component * cells + cell`), three times the cell count, in kg/s.
    ///
    /// ALREADY RELAXED. If the predictor relaxed momentum implicitly the
    /// diagonal is `a_P / alpha_u`, and `d_P = V_P / a_P` must use THAT value:
    /// `ADR-075` Q4 measured the error of using the unrelaxed diagonal as
    /// exactly `1 / alpha_u`, a 42.86% inflation of every coefficient at the
    /// default `0.7`. `cfdCouplingPredictor` returns the relaxed diagonals, so
    /// the consistent value is the one that is already to hand.
    std::span<const double> momentumDiagonal{};
    /// The one density, `rho`. `ADR-058` excludes a variable density from P19 v1
    /// and `ADR-028` forbids this module from holding one.
    Density density{};
};

/// Assembles the pressure-correction system for the SIMPLE correction.
///
/// THE LOOP IS OVER FACES, ONCE EACH. One `a_f` is computed and written as
/// `+a_f` to both diagonals and `-a_f` to both off-diagonals in a single visit,
/// so symmetry holds whatever the interpolation does. `ADR-075` Q1 measured the
/// per-cell alternative at 57.3703% bit-identical — this is not a stylistic
/// choice, it is what makes the matrix acceptable to a symmetric solver at all.
///
/// The boundary condition is `ADR-067`'s treatment table, `pressure` column:
/// `FixedValue` gives `p' = 0` and a diagonal contribution, `ZeroGradient`
/// contributes nothing. That the two are exclusive — a boundary prescribes the
/// flux or the pressure, never both and never neither — is asserted over
/// `kCfdBoundaryConditionKinds` by a test rather than trusted.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::expected<CfdPressureCorrectionSystem, CfdCouplingFailure>
assembleCfdPressureCorrection(const CfdTopology& topology, const CfdGeometry& geometry,
                              const PreparedCfdBoundary& boundary,
                              const CfdPressureCorrectionInput& input);

/// The refusal `assembleCfdPressureCorrection` would give, or none if it would
/// succeed.
///
/// RECORDED FOR WHAT IT IS WORTH: this shares one decision path with the
/// assembly, so their agreement cannot fail — the limitation `P19-DIFF-001`
/// recorded as its `L8` and every cfd operator since has inherited. It cannot
/// see `ResultIsNotFinite` or `EvaluationIsInconsistent`, which judge arrays
/// this function never builds.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::optional<CfdCouplingFailure>
cfdPressureCorrectionFailure(const CfdTopology& topology, const CfdGeometry& geometry,
                             const PreparedCfdBoundary& boundary,
                             const CfdPressureCorrectionInput& input);

/// The problem alone, allocation-free.
///
/// EVERY SIBLING EXPOSES THIS AND THE FIRST DRAFT OF THIS ONE DID NOT, which
/// made the shared gate's `noexcept` a claim with no caller behind it: both of
/// its call sites asked for a message, so every refusal path allocated through
/// `std::format` inside a `noexcept` function. A caller that only needs to know
/// WHETHER an input is usable now has a path that cannot throw.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::optional<CfdCouplingProblem>
cfdPressureCorrectionProblem(const CfdTopology& topology, const CfdGeometry& geometry,
                             const PreparedCfdBoundary& boundary,
                             const CfdPressureCorrectionInput& input) noexcept;

// ---------------------------------------------------------------------------
// The predictor
// ---------------------------------------------------------------------------

/// The predicted velocity and the diagonals the correction needs.
struct CfdCouplingPredictor {
    /// `U*`, COMPONENT-BLOCKED per `ADR-066`, three times the cell count, m/s.
    std::vector<double> velocity{};
    /// The RELAXED diagonals `a_P / alpha_u`, component-blocked, kg/s. Pass
    /// these straight to `CfdPressureCorrectionInput::momentumDiagonal`.
    std::vector<double> diagonal{};
    /// The normalised residual the supplied solve reported for each component,
    /// in `VectorComponent` order.
    std::array<double, 3> solveResidual{};

    friend bool operator==(const CfdCouplingPredictor&, const CfdCouplingPredictor&) = default;
};

/// What the predictor needs.
struct CfdCouplingPredictorInput {
    /// The three ASSEMBLED momentum systems, in `VectorComponent` order.
    ///
    /// ASSEMBLED BY `assembleCfdMomentum`, WHICH IS ALREADY QUALIFIED. This
    /// operator relaxes, solves and exposes the diagonal; it does not assemble,
    /// and it does not reconstruct the gradients that assembly consumes.
    std::span<const CfdMomentumSystem> momentum{};
    /// `U` at the start of the outer iteration, component-blocked, m/s. The
    /// implicit relaxation needs it: the deferred part of the diagonal is
    /// returned to the source against this field.
    std::span<const double> velocity{};
    /// `ADR-058`'s relaxation pair. Only `velocityRelaxation` is used here.
    CfdIterationSettings iteration{};
    /// `ADR-058`'s one ratio, handed to the port unchanged.
    CfdLinearSolverSettings linearSolver{};
};

/// Relaxes and solves the three momentum systems, and reports the diagonals.
///
/// THE RELAXATION IS IMPLICIT, WHICH IS THE CLASSIC FORM AND THE ONE
/// `CfdIterationSettings` DESCRIBES:
///
/// ```text
///     (a_P / alpha_u) u* + sum_N a_N u*_N = b + ((1 - alpha_u) / alpha_u) a_P u_old
/// ```
///
/// so the diagonal is divided by `alpha_u` and the part taken out is returned
/// to the source against the OLD field. At `alpha_u = 1` the matrix and the
/// source are unchanged — bit-for-bit, because the added term is exactly zero
/// and `a_P / 1.0 == a_P`.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::expected<CfdCouplingPredictor, CfdCouplingFailure>
cfdCouplingPredictor(const CfdTopology& topology, const CfdGeometry& geometry,
                     const PreparedCfdBoundary& boundary, const CfdCouplingPredictorInput& input,
                     const CfdLinearSolve& solve);

// ---------------------------------------------------------------------------
// The corrections
// ---------------------------------------------------------------------------

/// The corrected fields of one outer iteration.
struct CfdCouplingCorrection {
    /// `U`, corrected and component-blocked, m/s.
    ///
    /// `u'^i_P = -(1 / a_P^i) sum_f p'_f S_f,i`, a face sum in which the cell
    /// volume CANCELS analytically against `d_P = V_P / a_P` (ADR-075
    /// Decision 12). No gradient operator is involved, and deliberately so:
    /// `reconstructCfdPressureGradient` imposes the PRESCRIBED `p_b` at a
    /// fixed-pressure facet, and the pressure CORRECTION is zero there.
    std::vector<double> velocity{};
    /// `phi`, corrected, one per face, kg/s, signed out of the owner.
    ///
    /// `phi_f = phi*_f - a_f (p'_N - p'_P)` on an internal face,
    /// `phi_f = phi*_f + a_b p'_P` at a fixed-pressure facet (where `p'_b = 0`),
    /// and UNCHANGED at a prescribed-flux facet. This is the field whose
    /// continuity `evaluateCfdContinuity` then measures.
    std::vector<double> faceFlux{};
    /// `p`, updated with under-relaxation: `p = p* + alpha_p p'`, one per cell,
    /// Pa.
    std::vector<double> pressure{};

    friend bool operator==(const CfdCouplingCorrection&, const CfdCouplingCorrection&) = default;
};

/// What the correction needs.
struct CfdCouplingCorrectionInput {
    /// `p'`, the solved pressure correction, one per cell, Pa.
    std::span<const double> pressureCorrection{};
    /// `p*`, the pressure the iteration started from, one per cell, Pa.
    std::span<const double> pressure{};
    /// `U*`, the predicted velocity, component-blocked, m/s.
    std::span<const double> predictedVelocity{};
    /// `phi*`, the predicted flux, one per face, kg/s.
    std::span<const double> predictedFaceFlux{};
    /// The RELAXED momentum diagonals, component-blocked, kg/s.
    std::span<const double> momentumDiagonal{};
    /// The one density, `rho`.
    Density density{};
    /// `ADR-058`'s relaxation pair. Only `pressureRelaxation` is used here: the
    /// velocity relaxation was already applied, implicitly, by the predictor.
    CfdIterationSettings iteration{};
};

/// Applies the velocity, face-flux and pressure corrections.
///
/// THE FACE-FLUX CORRECTION IS THE ONE THAT MATTERS FOR CONSERVATION, and its
/// claim is an identity rather than an approximation: after it,
/// `div(phi)_P == (A p')_P - b_P`, the pressure system's own residual
/// (ADR-075 Decision 5). The velocity correction does not affect conservation
/// at all; it makes the cell velocities consistent with the corrected pressure.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::expected<CfdCouplingCorrection, CfdCouplingFailure>
applyCfdCouplingCorrection(const CfdTopology& topology, const CfdGeometry& geometry,
                           const PreparedCfdBoundary& boundary,
                           const CfdCouplingCorrectionInput& input);

// ---------------------------------------------------------------------------
// One outer iteration
// ---------------------------------------------------------------------------

/// What one outer iteration measured about itself.
struct CfdCouplingIterationSummary {
    std::size_t cells = 0;
    std::size_t faces = 0;
    /// The normalised residual the pressure solve reported.
    double pressureSolveResidual = 0.0;
    /// The normalised residual the momentum solves reported, per component.
    std::array<double, 3> momentumSolveResidual{};
    /// The largest `|p'|` over the cells, Pa. A measure of how far from
    /// converged the iteration was, in the units a reader can interpret.
    double largestPressureCorrection = 0.0;
    /// The pressure system's OWN residual in the continuity measure:
    /// `max_P |(A p')_P - b_P|` over `continuityScale`, excluding a pinned row.
    ///
    /// THE QUANTITY THE IDENTITY SAYS THE CLOSURE EQUALS. It is computed here
    /// rather than taken from the solver, because the solver reports a
    /// different norm over a different scale and the two do not convert
    /// (see `kCfdCouplingClosureFloor`).
    double systemResidual = 0.0;
    // THERE IS NO `closureBound` AND NO `continuityClosed`, AND THEIR ABSENCE
    // IS THE POINT.
    //
    // An earlier version carried both: a bound of `systemResidual + floor` and
    // a flag saying whether `residuals.continuity` met it. A mutation probe
    // then showed that replacing the flag with a literal `true` SURVIVED the
    // whole suite, and the reason is structural rather than a missing test.
    // `ADR-075` Decision 5's identity says the corrected flux's per-cell
    // continuity IS the system's residual, and once the bound is derived from
    // that same residual in that same measure the comparison cannot fail: the
    // flag was true by construction for every input.
    //
    // `CfdBoundary.hpp` states this project's rule for the same shape of
    // problem -- "An enumerator no input can produce is worse than no
    // enumerator" -- and an unfalsifiable flag is that rule's other face. What
    // it claimed to report is reported by `systemResidual`, which is a measured
    // number, and the identity itself is asserted directly by two tests. So
    // both fields are gone rather than documented as always true.
    /// Whether the domain is closed, as the QUALIFIED continuity evaluator
    /// reports it: every boundary face a wall by `ADR-067`'s table.
    bool closedDomain = false;
    /// The scale the continuity residual is relative to, in kg/s: the boundary
    /// throughput where there is one, and otherwise `0.5 * sum |phi|` over the
    /// INTERNAL faces.
    ///
    /// WHY A SECOND SCALE EXISTS. `ADR-058` fixes the mass balance's measures
    /// relative to the BOUNDARY throughput, and leaves them undefined when that
    /// is zero — which is every closed domain, and also any open domain whose
    /// boundary flux happens to be zero. An earlier form of this operator put
    /// the ABSOLUTE `kg/s` residual into the dimensionless `CfdResiduals` in
    /// that case, so the continuity stopping criterion became scale-dependent
    /// and, at millimetre scale, effectively disabled: a wholly
    /// non-conservative field could report `1e-9` and be called converged. Its
    /// own adversarial review found that. The interior flux scale is the
    /// honest denominator there, it is this milestone's own definition and not
    /// `ADR-058`'s, and `CfdMassBalance` is NOT filled from it.
    double continuityScale = 0.0;
    /// Whether `CfdCouplingIteration::balance` holds the evaluator's measures.
    ///
    /// FALSE EXACTLY IN A CLOSED DOMAIN, and `CfdContinuity.hpp` says why: a
    /// closed domain's boundary throughput is zero, so `ADR-058`'s two
    /// RELATIVE measures are UNDEFINED rather than small. `balance` is then
    /// left at its default and the continuity residual is the ABSOLUTE largest
    /// cell residual, which is exactly zero for a conservative closed field.
    /// Reporting a relative zero there would be a measurement nobody made.
    bool massBalanceDefined = false;
    /// The pressure-correction assembly's own summary.
    CfdPressureCorrectionSummary pressure{};

    friend bool operator==(const CfdCouplingIterationSummary&,
                           const CfdCouplingIterationSummary&) = default;
};

/// One advanced outer iteration.
struct CfdCouplingIteration {
    /// The corrected fields.
    CfdCouplingCorrection corrected{};
    /// `p'`, kept so that the identity of ADR-075 Decision 5 is checkable from
    /// this one call rather than by reassembling the system a second time.
    std::vector<double> pressureCorrection{};
    /// The system that produced it, kept for the same reason.
    ///
    /// NOT DEFAULT-INITIALIZED, AND IT CANNOT BE: `CfdSparseMatrix`'s default
    /// constructor is private (`ADR-036`, possession is the evidence), so this
    /// type has no default constructor either and `advanceCfdCoupling`
    /// constructs its result WHOLE rather than filling an empty one. The
    /// compiler enforcing that is the same correction `P19-CONT-001` took on
    /// its report.
    CfdPressureCorrectionSystem system;
    /// `ADR-058`'s residual record: one per momentum component and continuity,
    /// normalised and dimensionless.
    CfdResiduals residuals{};
    /// `ADR-058`'s two mass-balance measures, from `evaluateCfdContinuity` over
    /// the CORRECTED flux. Produced by `P19-CONT-001`'s qualified operator, not
    /// recomputed here.
    CfdMassBalance balance{};
    CfdCouplingIterationSummary summary{};
};

/// What one outer iteration needs.
struct CfdCouplingIterationInput {
    /// The three assembled momentum systems, in `VectorComponent` order.
    std::span<const CfdMomentumSystem> momentum{};
    /// `U` at the start of the iteration, component-blocked, m/s.
    std::span<const double> velocity{};
    /// `p` at the start of the iteration, one per cell, Pa.
    std::span<const double> pressure{};
    /// The one density, `rho`.
    Density density{};
    CfdIterationSettings iteration{};
    CfdLinearSolverSettings linearSolver{};
};

/// Advances one SIMPLE outer iteration.
///
/// ```text
/// 1  predictor          relax the three momentum systems, solve, take a_P
/// 2  predicted flux     cfdMassFlux from U*            (P19-CONV-001)
/// 3  correction system  assembleCfdPressureCorrection
/// 4  solve              the supplied port
/// 5  corrections        velocity, face flux, relaxed pressure
/// 6  continuity         evaluateCfdContinuity over the CORRECTED flux
///                                                      (P19-CONT-001)
/// 7  residuals          ADR-058's CfdResiduals
/// ```
///
/// DETERMINISTIC. Every face loop runs in `FaceIndex` order and every cell loop
/// in `CellIndex` order, with no unordered container on the path: `ADR-075` Q5
/// measured a shuffled face order as bit-identical in 0 of 200 trials, worst
/// relative `1.918002e-12`. Determinism is this operator's; a supplied solve
/// that is itself non-deterministic makes the run non-deterministic and nothing
/// here can detect that (`L4`).
///
/// NO LOOP. One iteration, and the criteria a loop would consult are
/// `cfdCouplingVerdict`. "Convergence loop" is `P19-STEADY-001`'s item.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::expected<CfdCouplingIteration, CfdCouplingFailure>
advanceCfdCoupling(const CfdTopology& topology, const CfdGeometry& geometry,
                   const PreparedCfdBoundary& boundary, const CfdCouplingIterationInput& input,
                   const CfdLinearSolve& solve);

// ---------------------------------------------------------------------------
// The criteria
// ---------------------------------------------------------------------------

/// What the criteria say about an outer iteration's residuals.
enum class CfdCouplingVerdict : std::uint8_t {
    /// Every momentum component and continuity are below their tolerances.
    Converged,
    /// A residual exceeded `divergenceFactor` times its first-iteration value.
    /// Checked BEFORE convergence, because a run that diverged and then
    /// happened to land under a tolerance has still diverged.
    Diverged,
    /// Neither: the iteration should continue if the budget allows.
    Continue,
};

[[nodiscard]] BETTERCAD_CFD_EXPORT std::string_view toString(CfdCouplingVerdict verdict) noexcept;

/// Judges one iteration's residuals against `ADR-058`'s criteria.
///
/// `first` is the residual record of the FIRST iteration of the run, which is
/// what `divergenceFactor` is relative to; pass the current record on the first
/// iteration, where the ratio is then exactly 1 and cannot trip a factor the
/// settings require to be greater than 1.
///
/// A PURE FUNCTION OF ITS ARGUMENTS, so that `P19-STEADY-001`'s loop and this
/// operator's own tests agree by construction rather than by inspection. It
/// reports `Diverged` on a non-finite residual as well: a NaN residual is not
/// below a tolerance, and treating it as "continue" would spend the whole
/// budget on a dead run.
[[nodiscard]] BETTERCAD_CFD_EXPORT CfdCouplingVerdict
cfdCouplingVerdict(const CfdResiduals& current, const CfdResiduals& first,
                   const CfdConvergenceSettings& settings) noexcept;

/// Whether an iteration count has exhausted `ADR-058`'s budget.
///
/// `completed` is the number of iterations already finished. Reaching the budget
/// without converging is a FAILURE and never a result, which `CfdSteadySettings`
/// states and `P19-STEADY-001` reports as
/// `CfdFailureStage::IterationLimitReached`.
[[nodiscard]] BETTERCAD_CFD_EXPORT bool
cfdCouplingBudgetExhausted(std::uint32_t completed, const CfdSteadySettings& settings) noexcept;

} // namespace bettercad::cfd
