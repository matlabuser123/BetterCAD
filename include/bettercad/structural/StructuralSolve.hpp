#pragma once

// P17-SOLVE-001 -- the linear static solve of `K u = F` after restraints.
//
// WHAT THIS IS. The exact free-system reduction of the assembled global
// system, a direct sparse factorisation of `Kff`, an INDEPENDENT residual
// validation, and the full global displacement vector reconstructed with the
// constrained degrees of freedom exactly zero. Derived and disposable.
//
// THE PARTITION, STATED ONCE.
//
// ```text
//     [ Kff  Kfc ] [ uf ]   [ Ff ]
//     [ Kcf  Kcc ] [ uc ] = [ Fc ]      with  uc = 0
//
//     so        Kff uf = Ff
//     and       u = uf on the free DOFs, exactly 0 on the constrained ones
// ```
//
// For the current zero-displacement scope there is no `Ff -= Kfc uc`
// correction, because `uc = 0`. The partition is written out anyway so that a
// later prescribed-displacement milestone adds that one term rather than
// discovering the structure.
//
// A LIBRARY SAYING "Success" IS NEVER SUFFICIENT EVIDENCE, and this is the
// milestone's central claim rather than a slogan. `SimplicialLDLT` reports
// `NumericalIssue` only when a pivot is EXACTLY zero
// (`SimplicialCholesky_impl.h`: `if (d == RealScalar(0))`), and a singular
// matrix in floating point almost never produces one -- so a free body
// factorises with `Success` and `solve()` returns an enormous but finite
// displacement. Worse, that garbage lies in the NULL SPACE, so `Kff uf` is
// still close to `Ff` and the residual gate passes too.
//
// Hence THREE INDEPENDENT CONDITIONS, of which the library supplies one:
//
// ```text
//   1  info() == Success, every D(k,k) > 0, and min|D|/max|D| >= pivotFloor
//   2  every component of uf is finite
//   3  the independent residual r = Kff uf - Ff passes the normalized gate
// ```
//
// ADR-039 records the measurements behind that, the solver audit and the
// licence evidence.
//
// THE RESIDUAL IS ON THE FREE SYSTEM, AND THAT IS NOT A SIMPLIFICATION.
// `K u - F` over the FULL system is correctly NON-ZERO at the constrained
// degrees of freedom: those entries are the support REACTIONS. Failing a
// correct solution because they are non-zero would be the classic mistake, so
// the gate is `Kff uf - Ff` and the full residual is retained for
// `P17-REACTION-001` to interpret.
//
// WHAT THIS IS NOT.
//
//     no penalty method             no `Kii += 1e20`. Constraints are applied
//                                   by exact reduction
//     no arbitrary pinning          an under-constrained model FAILS. No node
//                                   is fixed, no weak spring is added
//     no regularisation             no `Kff += eps*I`. A singular system is
//                                   diagnostic information
//     no reactions                  the full residual is retained, not
//                                   aggregated. `P17-REACTION-001` owns that
//     no post-processing            no strain, no stress, no von Mises.
//                                   `P17-POST-001` owns that
//     no persistence                nothing here is saved or undone
//     no GUI, no CLI                no Qt, no formatting, no progress dialog

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/structural/Export.hpp>
#include <bettercad/structural/StructuralDof.hpp>
#include <bettercad/structural/StructuralSystem.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace bettercad::structural {

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

/// Which factorisation to use.
///
/// ONE VALUE, AND IT IS NAMED RATHER THAN DEFAULTED. ADR-039 selected
/// `SimplicialLDLT` and recorded the three alternatives it rejected; the enum
/// exists so the choice is visible in a result's provenance and so a later
/// milestone adds a value rather than changing a hidden default.
enum class SolverAlgorithm : std::uint8_t {
    /// Eigen's `SimplicialLDLT`: a direct `L D L^T` factorisation with an AMD
    /// fill-reducing ordering, for a symmetric positive definite `Kff`.
    SimplicialLdlt,
};

[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::string_view
toString(SolverAlgorithm algorithm) noexcept;

/// The settings that materially affect the numerical result.
///
/// NO LIBRARY DEFAULT IS INHERITED. Everything that changes convergence,
/// failure classification or the published numbers is here with a value and a
/// reason, because a library-version-dependent default would make the
/// engineering semantics move under the project's feet.
///
/// AND THE FIELDS A DIRECT SOLVER DOES NOT HAVE ARE ABSENT RATHER THAN
/// PRESENT AND IGNORED. There is no `maximumIterations`, no
/// `relativeTolerance` and no preconditioner: `SimplicialLDLT` is direct, so
/// those are `N/A`, and a field that existed but did nothing would be a false
/// capability. A later iterative algorithm adds them alongside its enum value.
struct SolverSettings {
    SolverAlgorithm algorithm = SolverAlgorithm::SimplicialLdlt;

    /// The acceptance threshold for the INDEPENDENT normalized residual.
    /// Dimensionless. See `ResidualMetrics::normalized` for the formula.
    ///
    /// 1e-9 CHOSEN FOR A REASON AND NOT FROM OBSERVED VALUES. An LDLT of an
    /// SPD matrix is backward stable, so the normalized residual of a
    /// well-conditioned solve sits at a small multiple of the double epsilon
    /// -- around 1e-16. 1e-9 leaves seven orders of head-room for
    /// conditioning, and a residual one part in a billion of the applied load
    /// is far below any engineering significance. The measured values are
    /// recorded in the evidence so the gap is visible rather than assumed.
    double relativeResidualTolerance = 1.0e-9;

    /// The smallest admissible `min|D| / max|D|` of the factorisation.
    /// Dimensionless.
    ///
    /// THE GATE THE LIBRARY DOES NOT HAVE. `SimplicialLDLT` fails only on an
    /// exactly zero pivot, so a singular system factorises successfully; this
    /// is what refuses it. A RATIO rather than an absolute pivot, because `D`
    /// carries the units of the stiffness diagonal: a soft material with a
    /// small `E` has small pivots and is not singular, and a raw threshold
    /// would confuse the two.
    ///
    /// 1e-12 CHOSEN FOR A REASON. The error of an LDLT solve grows with
    /// `max|D| / min|D|`, so a ratio below 1e-12 means the component of the
    /// solution along that direction has lost twelve of a double's sixteen
    /// digits and is noise. The evidence records the measured ratio for a
    /// well-restrained model and for a free body, which are orders apart, so
    /// this number does not decide the verdict.
    double pivotFloor = 1.0e-12;

    friend bool operator==(const SolverSettings&, const SolverSettings&) = default;
};

/// Checks @p settings on its own terms: a recognised algorithm and two
/// tolerances that are finite and strictly positive.
///
/// A NaN or negative tolerance is refused HERE rather than left to decide
/// something inside the library: `r < NaN` is false, so a NaN threshold would
/// silently reject every solve, and a negative one would reject every solve
/// too but for a reason no diagnostic would explain.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Result<void> validate(const SolverSettings& settings);

// ---------------------------------------------------------------------------
// Residual metrics
// ---------------------------------------------------------------------------

/// The independently computed residual of the FREE system, `r = Kff uf - Ff`.
///
/// COMPUTED BY BETTERCAD, NOT READ OUT OF THE SOLVER. It is a loop over
/// BetterCAD's own CSR copy of `Kff` against BetterCAD's own `Ff`, after the
/// solve has returned, so it uses the ORIGINAL system and not whatever a
/// factorisation did to its internal copy. `solver.error()` is never consulted.
struct ResidualMetrics {
    /// `||r||_2`, in newtons.
    Force euclideanNorm{};
    /// `||r||_inf`, in newtons.
    Force infinityNorm{};
    /// `||Kff||_inf`, in N/m.
    Stiffness stiffnessInfinityNorm{};
    /// `||uf||_2`, in metres.
    Length displacementNorm{};
    /// `||Ff||_2`, in newtons.
    Force forceNorm{};

    /// ```text
    ///                        ||r||_2
    ///     normalized = ---------------------------------
    ///                  ||Kff||_inf ||uf||_2 + ||Ff||_2
    /// ```
    ///
    /// Dimensionless: the denominator is a force, as `||r||_2` is.
    ///
    /// THERE IS NO SCALE FLOOR, AND ITS ABSENCE IS THE POINT. The brief
    /// suggests adding one to avoid dividing by near-zero, but the
    /// denominator vanishes only when `uf` and `Ff` are both exactly zero --
    /// and then `r = Kff 0 - 0` is exactly zero too, so the honest value is
    /// `0` rather than `0 / floor`. That case is handled exactly and is
    /// ASSERTED: if the denominator is zero while `r` is not, the solve fails
    /// rather than reporting a flattering ratio. An invented floor would have
    /// made a zero-load system's residual depend on a number nobody could
    /// justify.
    ///
    /// SCALE-AWARE, which is why the raw norms are carried too: `||r|| = 1e-6`
    /// N means something very different for `F ~ 1` N and for `F ~ 1e9` N, so
    /// the gate is on this ratio and the evidence records both.
    double normalized = 0.0;

    friend bool operator==(const ResidualMetrics&, const ResidualMetrics&) = default;
};

// ---------------------------------------------------------------------------
// The numerical kernel
// ---------------------------------------------------------------------------

/// One solved symmetric sparse system: the solution, the residual and the
/// factorisation's own evidence.
struct SymmetricSolution {
    /// The solution, SI.
    std::vector<double> x{};
    ResidualMetrics residual{};
    /// `min|D| / max|D|` of the factorisation. `1.0` for an empty system,
    /// which has no pivots and is trivially well conditioned.
    double pivotRatio = 0.0;

    friend bool operator==(const SymmetricSolution&, const SymmetricSolution&) = default;
};

/// Why a symmetric sparse system cannot be solved acceptably.
enum class SolveProblem : std::uint8_t {
    /// The settings are malformed on their own terms.
    InvalidSettings,
    /// The system's arrays do not describe a square CSR of the stated size, or
    /// the right-hand side is the wrong length. No implicit resizing.
    DimensionMismatch,
    /// An input entry is not finite. `P17-ASSEMBLY-001` guarantees a finite
    /// `K` and `F`, so this is unreachable from a `GlobalStructuralSystem` --
    /// but the kernel is public and must not let the library become the
    /// validator.
    NonFiniteSystem,
    /// The factorisation reported a failure.
    FactorizationFailure,
    /// The factorisation succeeded numerically and the system is singular or
    /// too ill conditioned to solve: a non-positive pivot, or
    /// `min|D| / max|D|` below the floor. **This is the condition the library
    /// does not report**, and for a structural model it usually means
    /// unremoved rigid-body modes -- but it can also mean a disconnected
    /// unrestrained region, a mechanism, or simply bad conditioning, so the
    /// diagnostic says "may be under-constrained" rather than claiming to know.
    SingularSystem,
    /// A solution component is not finite.
    NonFiniteSolution,
    /// A residual component is not finite.
    NonFiniteResidual,
    /// The independent normalized residual exceeded the threshold. The
    /// diagnostic carries the value, the threshold and the raw norms.
    ResidualTooLarge,
    /// The global system, the constraints and the numbering do not belong to
    /// the same mesh. Refused BEFORE any factorisation.
    SourceMismatch,
};

[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::string_view
toString(SolveProblem problem) noexcept;

/// Solves `A x = b` for a symmetric `A` given in compressed sparse row form.
///
/// THE NUMERICAL KERNEL, AND IT IS PUBLIC DELIBERATELY. A structural fixture
/// cannot pose a system whose solution is known in closed form -- the smallest
/// mesh a `StructuralModel` can be built from has dozens of degrees of freedom
/// -- so the 2x2 and 3x3 validations the gate requires need a way in. This is
/// that way, and it is the SAME code path the structural solve uses rather
/// than a test-only back door: `solveStructuralSystem` reduces, calls this,
/// and reconstructs.
///
/// Applies all three gates: the factorisation and its pivot ratio, finiteness,
/// and the independent residual.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Result<SymmetricSolution>
solveSymmetricSparse(std::size_t rows, std::span<const StiffnessMatrix::Index> rowStart,
                     std::span<const StiffnessMatrix::Index> inner,
                     std::span<const double> values, std::span<const double> rhs,
                     const SolverSettings& settings);

/// The problem `solveSymmetricSparse` would report, or none if it would
/// succeed. The same checks in the same order.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::optional<SolveProblem>
symmetricSolveProblem(std::size_t rows, std::span<const StiffnessMatrix::Index> rowStart,
                      std::span<const StiffnessMatrix::Index> inner,
                      std::span<const double> values, std::span<const double> rhs,
                      const SolverSettings& settings);

// ---------------------------------------------------------------------------
// The reduced free system
// ---------------------------------------------------------------------------

/// `Kff` and `Ff`: the global system restricted to the free degrees of
/// freedom, in `FreeEquationIndex` order.
///
/// EXTRACTED BY COPY, not by a view, and the reason is stated rather than
/// implied: a view would have to carry the global pattern and skip constrained
/// entries on every access, which puts the constraint logic inside every inner
/// loop. The copy is `O(nnz(Kff))`, is built once, and makes `Kff` an ordinary
/// CSR that the residual loop and the factorisation both read directly.
///
/// THE ORDER IS `FreeEquationMap`'S, EXACTLY. Row `i` is the global degree of
/// freedom `dofOf(i)`, and `FreeEquationMap::freeDofs()` is ascending, so the
/// extraction traverses a sorted vector and never an unordered set.
///
/// POSSESSION IS THE EVIDENCE (ADR-036): one friend, no public constructor, so
/// a reduced system cannot exist unless the global system, the constraints and
/// the numbering were checked against each other first.
class BETTERCAD_STRUCTURAL_EXPORT FreeSystem {
public:
    using Index = StiffnessMatrix::Index;

    /// Rows of the reduced system, `= FreeEquationMap::freeCount()`.
    [[nodiscard]] std::size_t rows() const noexcept { return rows_; }
    [[nodiscard]] std::size_t nonZeros() const noexcept { return values_.size(); }

    [[nodiscard]] std::span<const Index> rowStart() const noexcept { return rowStart_; }
    [[nodiscard]] std::span<const Index> innerIndices() const noexcept { return inner_; }
    /// SI, N/m.
    [[nodiscard]] std::span<const double> values() const noexcept { return values_; }
    /// SI, N.
    [[nodiscard]] std::span<const double> force() const noexcept { return force_; }

    /// `Kff(row, column)`, or zero if the pattern does not hold that pair.
    [[nodiscard]] Stiffness coeff(std::size_t row, std::size_t column) const noexcept;
    /// `Ff(row)`, or zero outside the vector.
    [[nodiscard]] Force forceAt(std::size_t row) const noexcept {
        return row < force_.size() ? Force::fromSi(force_[row]) : Force{};
    }

    /// `max |Kff(i,j) - Kff(j,i)|` over the stored pattern, in SI. Carried
    /// because an SPD factorisation reads only one triangle: if the extraction
    /// had broken symmetry, the solver would silently use half of a matrix
    /// that is not the one BetterCAD assembled.
    [[nodiscard]] double largestSymmetryError() const noexcept;

    friend bool operator==(const FreeSystem&, const FreeSystem&) = default;

private:
    FreeSystem() = default;

    friend BETTERCAD_STRUCTURAL_EXPORT Result<FreeSystem>
    extractFreeSystem(const GlobalStructuralSystem& system, const FreeEquationMap& equations);

    std::size_t rows_ = 0;
    std::vector<Index> rowStart_{};
    std::vector<Index> inner_{};
    std::vector<double> values_{};
    std::vector<double> force_{};
};

/// Restricts @p system to the degrees of freedom @p equations leaves free.
///
/// Refuses a pair that does not belong to the same mesh -- the `MeshStamp` and
/// the degree-of-freedom count, so an `M1` stiffness cannot be reduced by `M2`
/// constraints because the dimensions happen to agree.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Result<FreeSystem>
extractFreeSystem(const GlobalStructuralSystem& system, const FreeEquationMap& equations);

// ---------------------------------------------------------------------------
// The solved system
// ---------------------------------------------------------------------------

/// The displacement field of one solved model, with the evidence that it is a
/// solution.
///
/// DERIVED AND DISPOSABLE. Nothing here is persisted, undone or canonical, and
/// no `SolverResultId` is introduced: `P17-DATA-001` owns result identity and
/// a solution is a consequence of the source state, not an identity of its own.
///
/// THE DISPLACEMENT IS IN GLOBAL `DofIndex` ORDER, never in the solver's
/// internal permutation. `SimplicialLDLT` applies an AMD fill-reducing
/// permutation inside the factorisation; that permutation is solver-local and
/// `solve()` returns the vector in the order it was given, which is
/// `FreeEquationIndex` order, which maps back through `FreeEquationMap`. The
/// test asserts it rather than trusting it.
class BETTERCAD_STRUCTURAL_EXPORT SolvedSystem {
public:
    /// What the stiffness and the force were assembled from.
    [[nodiscard]] const AssemblySource& source() const noexcept { return source_; }
    /// The settings the solve used, so a reader never has to guess which
    /// tolerance produced a number.
    [[nodiscard]] const SolverSettings& settings() const noexcept { return settings_; }

    [[nodiscard]] const meshing::MeshStamp& mesh() const noexcept { return mesh_; }
    [[nodiscard]] bool describes(const meshing::Mesh& mesh) const noexcept {
        return mesh.owns(mesh_) && kDofsPerNode * mesh.nodes().size() == displacement_.size();
    }

    /// `3 * nodeCount()`.
    [[nodiscard]] std::size_t degreesOfFreedom() const noexcept { return displacement_.size(); }
    /// Rows of the system that was actually solved.
    [[nodiscard]] std::size_t freeEquations() const noexcept { return freeEquations_; }
    [[nodiscard]] std::size_t constrainedDegreesOfFreedom() const noexcept {
        return displacement_.size() - freeEquations_;
    }

    /// The displacement of one global degree of freedom, in metres. EXACTLY
    /// zero for a constrained one -- it was never an unknown, so no
    /// factorisation noise can reach it.
    [[nodiscard]] Length displacementOf(DofIndex index) const noexcept;

    /// The full field, SI, in global `DofIndex` order (entry `k` is
    /// `DofIndex` `k + 1`, since a `DofIndex` is 1-based).
    [[nodiscard]] std::span<const double> values() const noexcept { return displacement_; }

    /// `K u - F` over the FULL system, SI.
    ///
    /// Free entries are ~0; CONSTRAINED ENTRIES ARE THE SUPPORT REACTIONS and
    /// are correctly non-zero. Retained, not aggregated:
    /// `P17-REACTION-001` owns reaction semantics and equilibrium, and
    /// discarding this now would make it recompute what is already known.
    [[nodiscard]] std::span<const double> fullResidual() const noexcept { return fullResidual_; }

    /// The independent residual of the FREE system.
    [[nodiscard]] const ResidualMetrics& residual() const noexcept { return residual_; }

    /// `min|D| / max|D|` of the factorisation: the measurement that refuses a
    /// singular system the library reported as successful.
    [[nodiscard]] double pivotRatio() const noexcept { return pivotRatio_; }

    [[nodiscard]] std::size_t freeSystemNonZeros() const noexcept { return freeNonZeros_; }

    /// `U = (1/2) u^T K u`, the strain energy. Finite and non-negative for a
    /// solution; equal to `(1/2) u^T F` at equilibrium with zero prescribed
    /// displacement, which the tests check as a supplemental gate.
    [[nodiscard]] Energy strainEnergy() const noexcept { return strainEnergy_; }

    friend bool operator==(const SolvedSystem&, const SolvedSystem&) = default;

private:
    /// THE ONLY CONSTRUCTOR, AND THERE IS NO DEFAULT ONE. A `SolvedSystem`
    /// cannot exist without a displacement field that passed all three gates,
    /// so a half-published solution is unrepresentable rather than merely
    /// discouraged. The same shape `PreparedRestraints` and
    /// `GlobalStructuralSystem` use, for the same reason (ADR-036).
    SolvedSystem(AssemblySource source, SolverSettings settings, meshing::MeshStamp mesh,
                 std::size_t freeEquations, std::size_t freeNonZeros, double pivotRatio,
                 Energy strainEnergy, ResidualMetrics residual,
                 std::vector<double> displacement, std::vector<double> fullResidual)
        : source_(std::move(source)), settings_(settings), mesh_(std::move(mesh)),
          freeEquations_(freeEquations), freeNonZeros_(freeNonZeros), pivotRatio_(pivotRatio),
          strainEnergy_(strainEnergy), residual_(residual),
          displacement_(std::move(displacement)), fullResidual_(std::move(fullResidual)) {}

    friend BETTERCAD_STRUCTURAL_EXPORT Result<SolvedSystem>
    solveStructuralSystem(const GlobalStructuralSystem& system, const ConstraintSet& constraints,
                          const SolverSettings& settings);

    AssemblySource source_;
    SolverSettings settings_;
    meshing::MeshStamp mesh_;
    std::size_t freeEquations_;
    std::size_t freeNonZeros_;
    double pivotRatio_;
    Energy strainEnergy_;
    ResidualMetrics residual_;
    std::vector<double> displacement_;
    std::vector<double> fullResidual_;
};

/// Solves @p system subject to @p constraints.
///
/// THE ORDER OF THE CHECKS IS PART OF THE CONTRACT, because an obvious input
/// error must be reported before a factorisation spends time proving it:
///
/// ```text
/// 1  the settings, on their own terms
/// 2  the constraints and the system against the same mesh
/// 3  the free-equation numbering, from P17-DOF's own buildFreeEquationMap
/// 4  the reduced system, extracted in FreeEquationIndex order
/// 5  the factorisation, its status AND its pivot ratio
/// 6  finiteness of uf
/// 7  the independent residual
/// 8  reconstruction, and only then is anything published
/// ```
///
/// ZERO FREE EQUATIONS IS A SUCCESS, not a special failure. Every degree of
/// freedom constrained to zero means `u = 0` is the solution and there is
/// nothing to factor; returning a failure would report a fully supported model
/// as unsolvable. Nothing is handed to the library, so no 0x0 factorisation is
/// attempted. The full residual is still computed, and its constrained entries
/// are the reactions that balance the applied load -- which is exactly what
/// `P17-REACTION-001` will want from this case.
///
/// AN EMPTY CONSTRAINT SET IS NOT REFUSED EARLY. `P17-BC-001` correctly allows
/// a model with no restraints; it is the FACTORISATION that must refuse it,
/// through the pivot gate, and reporting it earlier would be this milestone
/// deciding a question that belongs to the numbers.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Result<SolvedSystem>
solveStructuralSystem(const GlobalStructuralSystem& system, const ConstraintSet& constraints,
                      const SolverSettings& settings = {});

/// The problem `solveStructuralSystem` would report, or none if it would
/// succeed. The same checks in the same order.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::optional<SolveProblem>
structuralSolveProblem(const GlobalStructuralSystem& system, const ConstraintSet& constraints,
                       const SolverSettings& settings = {});

} // namespace bettercad::structural
