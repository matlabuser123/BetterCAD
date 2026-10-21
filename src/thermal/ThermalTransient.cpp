// The transient thermal solve (P18-TRANSIENT-001).
//
// THE FACTORISATION IS NOT HERE, as it is not in ThermalSolve.cpp. Every step is
// the steady machinery applied to a different matrix and right-hand side:
//
//     A = C / dt + K        b = Q + (C / dt) T[n]
//
// is handed to the SAME `reduceThermalSystem` (exact Dirichlet elimination, the
// `- Kfc Tc` term included) and the SAME `linalg::solveSymmetricSparse`
// (ADR-052's three-condition kernel). What this file adds is only what a time
// integration adds: the time grid, the effective system, the initial state, the
// history term, the per-step acceptance and the provenance.
//
// WHAT THIS FILE DELIBERATELY DOES NOT CALL: `analyseAnchoring`. A steady model
// without a datum is singular; a transient one is not, because `C / dt` is
// positive definite and the initial temperature fixes the level. The anchoring
// analysis would refuse a valid insulated body, and it is not applied here.
//
// WHAT IS NOT HERE EITHER: no `A += eps*I`, no pinned unknown, no replacement or
// lumping of C, no dense effective matrix, no second attempt with a smaller step.

#include <bettercad/thermal/ThermalTransient.hpp>

#include <bettercad/linalg/SymmetricSolve.hpp>
#include <bettercad/thermal/ThermalSolve.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <utility>

namespace bettercad::thermal {
namespace {

/// The symmetry bound the assembly applied to K and C (ThermalSystem.cpp). The
/// effective matrix is a sum of the two and is held to the same bound.
constexpr double kEffectiveSymmetryTolerance = 1.0e-13;

[[nodiscard]] TransientFailure failure(TransientProblem problem, ErrorCode code, std::string message) {
    return TransientFailure{.problem = problem, .error = makeError(code, std::move(message)).error()};
}

/// Where in the run a refusal happened. A refusal before the first step keeps
/// step 0.
[[nodiscard]] TransientFailure atStep(TransientFailure refused, std::size_t step, double time) {
    refused.step = step;
    refused.time = time;
    return refused;
}

[[nodiscard]] linalg::SymmetricSolveSettings kernelSettings(const ThermalSolverSettings& settings) {
    return linalg::SymmetricSolveSettings{.relativeResidualTolerance = settings.relativeResidualTolerance,
                                          .pivotFloor = settings.pivotFloor};
}

/// The steady reduction's refusals in the transient vocabulary. The reduced system
/// is the same object; only the names differ.
[[nodiscard]] TransientFailure fromReduction(SteadySolveFailure refused) {
    TransientProblem problem = TransientProblem::InconsistentSystem;
    switch (refused.problem) {
    case SteadySolveProblem::DofMapMismatch:
        problem = TransientProblem::DofMapMismatch;
        break;
    case SteadySolveProblem::ConstraintMismatch:
        problem = TransientProblem::ConstraintMismatch;
        break;
    case SteadySolveProblem::NonFiniteInput:
        problem = TransientProblem::NonFiniteInput;
        break;
    default:
        break;
    }
    return TransientFailure{.problem = problem, .error = std::move(refused.error)};
}

/// The kernel's classification in the transient vocabulary.
[[nodiscard]] TransientFailure kernelFailure(const linalg::SymmetricSolveFailure& refused,
                                             const ThermalSolverSettings& settings) {
    switch (refused.problem) {
    case linalg::SymmetricSolveProblem::InvalidSettings:
        return TransientFailure{.problem = TransientProblem::InvalidSettings, .error = refused.error};
    case linalg::SymmetricSolveProblem::NonFiniteSystem:
        return failure(TransientProblem::NonFiniteInput, ErrorCode::InvalidArgument,
                       std::format("the effective transient system holds an entry that is not finite ({}); "
                                   "nothing was factorised",
                                   refused.error.message));
    case linalg::SymmetricSolveProblem::DimensionMismatch:
    case linalg::SymmetricSolveProblem::FactorizationFailure:
        return failure(TransientProblem::FactorizationFailure, ErrorCode::FailedPrecondition,
                       std::format("the linear solve of the {} reduced equations could not be carried out: {}",
                                   refused.rows, refused.error.message));
    case linalg::SymmetricSolveProblem::SingularSystem:
        if (refused.exactlyZeroPivot) {
            return failure(TransientProblem::SingularSystem, ErrorCode::FailedPrecondition,
                           std::format("the effective transient system C/dt + K of {} equations is singular: the "
                                       "factorisation found an exactly zero pivot. A transient system with positive "
                                       "capacity is never singular for want of a datum, so a region has no heat "
                                       "capacity and no conduction path, or the matrix is degenerate",
                                       refused.rows));
        }
        return failure(TransientProblem::SingularSystem, ErrorCode::FailedPrecondition,
                       std::format("the effective transient system C/dt + K of {} equations is singular or too ill "
                                   "conditioned to solve: {}. A region with no heat capacity and no conduction path "
                                   "gives a vanishing pivot. The pivot floor is {}",
                                   refused.rows, refused.error.message, settings.pivotFloor));
    case linalg::SymmetricSolveProblem::NonFiniteSolution:
        return failure(TransientProblem::NonFiniteSolution, ErrorCode::FailedPrecondition,
                       std::format("the solved temperature of free equation {} is not finite although the solver "
                                   "reported success",
                                   refused.index));
    case linalg::SymmetricSolveProblem::NonFiniteResidual:
        return failure(TransientProblem::NonFiniteResidual, ErrorCode::FailedPrecondition,
                       std::format("the residual of free equation {} is not finite", refused.index));
    case linalg::SymmetricSolveProblem::ResidualTooLarge:
        return failure(TransientProblem::ResidualTooLarge, ErrorCode::FailedPrecondition,
                       std::format("the independent reduced residual is too large: ||r||_2 = {} W, normalized = {} "
                                   "against a threshold of {}. The solver reported success; BetterCAD does not",
                                   refused.residual.euclidean, refused.residual.normalized,
                                   settings.relativeResidualTolerance));
    }
    return failure(TransientProblem::FactorizationFailure, ErrorCode::Internal, "an unclassified solver refusal");
}

[[nodiscard]] std::vector<double> siOf(std::span<const Temperature> values) {
    std::vector<double> out(values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        out[i] = values[i].si();
    }
    return out;
}

[[nodiscard]] bool knownPolicy(TransientOutputPolicy policy) noexcept {
    return policy == TransientOutputPolicy::FinalOnly || policy == TransientOutputPolicy::EveryStep;
}

/// `max_i sum_j |A(i,j)|`.
[[nodiscard]] double infinityNorm(const ThermalSparseMatrix& matrix) noexcept {
    double largest = 0.0;
    const std::span<const ThermalSparseMatrix::Index> rowStart = matrix.rowStart();
    const std::span<const double> values = matrix.values();
    for (std::size_t row = 0; row < matrix.rows(); ++row) {
        double sum = 0.0;
        for (ThermalSparseMatrix::Index slot = rowStart[row]; slot < rowStart[row + 1]; ++slot) {
            sum += std::abs(values[slot]);
        }
        largest = std::max(largest, sum);
    }
    return largest;
}

[[nodiscard]] double euclidean(std::span<const double> values) noexcept {
    double sum = 0.0;
    for (const double value : values) {
        sum += value * value;
    }
    return std::sqrt(sum);
}

/// Every prescribed unknown holds its prescribed temperature EXACTLY. Equality of
/// doubles, deliberately: the value was assigned, not computed.
[[nodiscard]] std::optional<TransientFailure> constraintsHeld(const ThermalConstraintMap& constraints,
                                                              std::span<const Temperature> temperatures) {
    for (const ThermalTemperatureConstraint& constraint : constraints.constrained()) {
        const double wanted = constraint.temperature.si();
        const double actual = temperatures[static_cast<std::size_t>(constraint.dof.value())].si();
        if (!(actual == wanted)) {
            return failure(TransientProblem::ConstraintNotPreserved, ErrorCode::FailedPrecondition,
                           std::format("unknown {} is prescribed at {} K but holds {} K", constraint.dof.value(),
                                       wanted, actual));
        }
    }
    return std::nullopt;
}

/// `b = Q + (C T_n) / dt`, W. The history term uses the PREVIOUS state.
[[nodiscard]] std::expected<std::vector<double>, TransientFailure>
historyRightHandSide(const ThermalSparseMatrix& capacity, const ThermalHeatVector& heat,
                     std::span<const Temperature> previous, double timeStep) {
    const std::vector<double> state = siOf(previous);
    Result<std::vector<double>> product = capacity.multiply(state);
    if (!product.has_value()) {
        return std::unexpected(TransientFailure{.problem = TransientProblem::DofMapMismatch, .error = product.error()});
    }
    std::vector<double> out(state.size());
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = heat.values()[i] + (*product)[i] / timeStep;
        if (!std::isfinite(out[i])) {
            return std::unexpected(failure(TransientProblem::NonFiniteInput, ErrorCode::InvalidArgument,
                                           std::format("the right-hand side Q + (C/dt) T[n] of unknown {} is not "
                                                       "finite: it overflowed",
                                                       i)));
        }
    }
    return out;
}

/// The normalized physical residual of the FREE rows:
///
/// ```text
///                          || C (T[n+1] - T[n]) / dt + K T[n+1] - Q ||_free
///   ---------------------------------------------------------------------------------
///   ||C||_inf ||T[n+1] - T[n]||_2 / dt  +  ||K||_inf ||T[n+1]||_2  +  ||Q||_free
/// ```
///
/// The same backward-error normalization the kernel applies to its own residual
/// (`||r|| / (||A|| ||x|| + ||b||)`), so scaling K, C and Q together leaves it
/// unchanged. Computed from the ORIGINAL K, C and Q: a matrix that was built wrong
/// before it was factorised cannot make this residual small.
[[nodiscard]] std::expected<double, TransientFailure>
physicalResidualOf(const ThermalSparseMatrix& conductance, const ThermalSparseMatrix& capacity,
                   const ThermalHeatVector& heat, const ThermalFreeEquationMap& equations,
                   std::span<const Temperature> previous, std::span<const Temperature> next, double timeStep) {
    const std::vector<double> nextSi = siOf(next);
    std::vector<double> change(nextSi.size());
    for (std::size_t i = 0; i < change.size(); ++i) {
        change[i] = nextSi[i] - previous[i].si();
    }
    Result<std::vector<double>> kt = conductance.multiply(nextSi);
    Result<std::vector<double>> cd = capacity.multiply(change);
    if (!kt.has_value() || !cd.has_value()) {
        return std::unexpected(failure(TransientProblem::DofMapMismatch, ErrorCode::FailedPrecondition,
                                       "the physical residual could not be formed: a matrix does not match the "
                                       "state vector"));
    }

    const std::span<const ThermalDofIndex> freeDofs = equations.freeDofs();
    double numerator = 0.0;
    double heatSquared = 0.0;
    for (const ThermalDofIndex dof : freeDofs) {
        const auto row = static_cast<std::size_t>(dof.value());
        const double q = heat.values()[row];
        const double r = (*cd)[row] / timeStep + (*kt)[row] - q;
        if (!std::isfinite(r)) {
            return std::unexpected(failure(TransientProblem::NonFiniteResidual, ErrorCode::FailedPrecondition,
                                           std::format("the physical residual of unknown {} is not finite", row)));
        }
        numerator += r * r;
        heatSquared += q * q;
    }
    numerator = std::sqrt(numerator);

    const double denominator = infinityNorm(capacity) * euclidean(change) / timeStep +
                               infinityNorm(conductance) * euclidean(nextSi) + std::sqrt(heatSquared);
    if (!std::isfinite(numerator) || !std::isfinite(denominator)) {
        return std::unexpected(failure(TransientProblem::NonFiniteResidual, ErrorCode::FailedPrecondition,
                                       "the physical residual norm is not finite"));
    }
    if (denominator == 0.0) {
        // Every term of the denominator is zero, so C dT/dt, K T and Q are each exactly zero over the free rows and r is exactly
        // zero too: nothing is divided by zero and nothing is flattered.
        return 0.0;
    }
    return numerator / denominator;
}

} // namespace

std::string_view toString(TransientProblem problem) noexcept {
    switch (problem) {
    case TransientProblem::InvalidSettings:
        return "invalid_settings";
    case TransientProblem::InvalidTimeStep:
        return "invalid_time_step";
    case TransientProblem::InvalidEndTime:
        return "invalid_end_time";
    case TransientProblem::InvalidOutputPolicy:
        return "invalid_output_policy";
    case TransientProblem::InvalidInitialTemperature:
        return "invalid_initial_temperature";
    case TransientProblem::NotTransientDefinition:
        return "not_transient_definition";
    case TransientProblem::NotTransientSystem:
        return "not_transient_system";
    case TransientProblem::DofMapMismatch:
        return "dof_map_mismatch";
    case TransientProblem::ConstraintMismatch:
        return "constraint_mismatch";
    case TransientProblem::StaleSystem:
        return "stale_system";
    case TransientProblem::StaleBoundary:
        return "stale_boundary";
    case TransientProblem::TooManyTimeSteps:
        return "too_many_time_steps";
    case TransientProblem::HistoryTooLarge:
        return "history_too_large";
    case TransientProblem::NonFiniteInput:
        return "non_finite_input";
    case TransientProblem::InconsistentSystem:
        return "inconsistent_system";
    case TransientProblem::SingularSystem:
        return "singular_system";
    case TransientProblem::FactorizationFailure:
        return "factorization_failure";
    case TransientProblem::NonFiniteSolution:
        return "non_finite_solution";
    case TransientProblem::NonFiniteResidual:
        return "non_finite_residual";
    case TransientProblem::ResidualTooLarge:
        return "residual_too_large";
    case TransientProblem::ConstraintNotPreserved:
        return "constraint_not_preserved";
    }
    return "unknown";
}

std::string_view toString(TransientStaleReason reason) noexcept {
    switch (reason) {
    case TransientStaleReason::Body:
        return "body";
    case TransientStaleReason::Mesh:
        return "mesh";
    case TransientStaleReason::Geometry:
        return "geometry";
    case TransientStaleReason::Mode:
        return "mode";
    case TransientStaleReason::Material:
        return "material";
    case TransientStaleReason::Loads:
        return "loads";
    case TransientStaleReason::Convections:
        return "convections";
    case TransientStaleReason::PrescribedTemperatures:
        return "prescribed_temperatures";
    case TransientStaleReason::InitialCondition:
        return "initial_condition";
    case TransientStaleReason::TimeStep:
        return "time_step";
    case TransientStaleReason::EndTime:
        return "end_time";
    case TransientStaleReason::OutputPolicy:
        return "output_policy";
    case TransientStaleReason::Settings:
        return "settings";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// The time grid
// ---------------------------------------------------------------------------

std::expected<TimeGrid, TransientFailure> TimeGrid::create(double dt, double endTime) {
    if (!std::isfinite(dt) || !(dt > 0.0)) {
        return std::unexpected(failure(TransientProblem::InvalidTimeStep, ErrorCode::InvalidArgument,
                                       std::format("the time step must be finite and positive, got {} s", dt)));
    }
    if (!std::isfinite(endTime) || endTime < 0.0) {
        return std::unexpected(failure(TransientProblem::InvalidEndTime, ErrorCode::InvalidArgument,
                                       std::format("the end time must be finite and not negative, got {} s", endTime)));
    }

    TimeGrid grid;
    grid.nominal_ = dt;
    grid.end_ = endTime;
    if (endTime == 0.0) {
        return grid; // one time, no step
    }

    const double ratio = endTime / dt;
    // `!(ratio <= max)` also refuses an overflowed quotient. The guard is on the
    // count, never turned into a larger step.
    if (!(ratio <= static_cast<double>(kMaxTimeSteps))) {
        return std::unexpected(failure(TransientProblem::TooManyTimeSteps, ErrorCode::InvalidArgument,
                                       std::format("end time {} s over time step {} s needs {} steps, more than the "
                                                   "limit of {}; the step is not enlarged",
                                                   endTime, dt, ratio, kMaxTimeSteps)));
    }

    const double nearest = std::round(ratio);
    if (nearest >= 1.0 && std::abs(ratio - nearest) <= kTimeGridTolerance * nearest) {
        // t_end is a whole number of steps: every step is exactly dt and the final
        // time reported is t_end itself.
        grid.steps_ = static_cast<std::size_t>(nearest);
        return grid;
    }

    const double full = std::floor(ratio);
    grid.remainder_ = endTime - full * dt;
    grid.steps_ = static_cast<std::size_t>(full) + 1;
    grid.partial_ = true;
    return grid;
}

double TimeGrid::timeAt(std::size_t index) const noexcept {
    if (index == 0) {
        return 0.0;
    }
    if (index >= steps_) {
        return end_;
    }
    return static_cast<double>(index) * nominal_;
}

double TimeGrid::stepAt(std::size_t index) const noexcept {
    if (index >= steps_) {
        return 0.0;
    }
    if (partial_ && index + 1 == steps_) {
        return remainder_;
    }
    return nominal_;
}

double TimeGrid::integratedSpan() const noexcept {
    if (steps_ == 0) {
        return 0.0;
    }
    if (partial_) {
        return static_cast<double>(steps_ - 1) * nominal_ + remainder_;
    }
    return static_cast<double>(steps_) * nominal_;
}

std::vector<double> TimeGrid::times() const {
    std::vector<double> out(steps_ + 1);
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = timeAt(i);
    }
    return out;
}

// ---------------------------------------------------------------------------
// The effective system
// ---------------------------------------------------------------------------

std::expected<ThermalSparseMatrix, TransientFailure>
effectiveMatrix(const ConductanceMatrix& conductance, const CapacityMatrix& capacity, double timeStep) {
    if (!std::isfinite(timeStep) || !(timeStep > 0.0)) {
        return std::unexpected(failure(TransientProblem::InvalidTimeStep, ErrorCode::InvalidArgument,
                                       std::format("the time step must be finite and positive, got {} s", timeStep)));
    }
    const std::size_t n = conductance.rows();
    if (conductance.columns() != n || capacity.rows() != n || capacity.columns() != n) {
        return std::unexpected(failure(TransientProblem::DofMapMismatch, ErrorCode::FailedPrecondition,
                                       std::format("K is {} x {} and C is {} x {}; the effective matrix needs two "
                                                   "square matrices of one size",
                                                   conductance.rows(), conductance.columns(), capacity.rows(),
                                                   capacity.columns())));
    }

    // A row-by-row merge of two sorted patterns: the result holds the UNION, in
    // ascending column order, and every sum is one addition in a fixed order.
    const std::span<const ThermalSparseMatrix::Index> kStart = conductance.rowStart();
    const std::span<const ThermalSparseMatrix::Index> kInner = conductance.innerIndices();
    const std::span<const double> kValues = conductance.values();
    const std::span<const ThermalSparseMatrix::Index> cStart = capacity.rowStart();
    const std::span<const ThermalSparseMatrix::Index> cInner = capacity.innerIndices();
    const std::span<const double> cValues = capacity.values();

    std::vector<ThermalSparseMatrix::Index> rowStart(n + 1, 0);
    std::vector<ThermalSparseMatrix::Index> inner;
    std::vector<double> values;
    inner.reserve(kInner.size() + cInner.size());
    values.reserve(kValues.size() + cValues.size());
    for (std::size_t row = 0; row < n; ++row) {
        ThermalSparseMatrix::Index k = kStart[row];
        ThermalSparseMatrix::Index c = cStart[row];
        const ThermalSparseMatrix::Index kEnd = kStart[row + 1];
        const ThermalSparseMatrix::Index cEnd = cStart[row + 1];
        while (k < kEnd || c < cEnd) {
            const bool takeK = k < kEnd && (c >= cEnd || kInner[k] <= cInner[c]);
            const bool takeC = c < cEnd && (k >= kEnd || cInner[c] <= kInner[k]);
            if (takeK && takeC) {
                inner.push_back(kInner[k]);
                values.push_back(cValues[c] / timeStep + kValues[k]);
                ++k;
                ++c;
            } else if (takeK) {
                inner.push_back(kInner[k]);
                values.push_back(kValues[k]);
                ++k;
            } else {
                inner.push_back(cInner[c]);
                values.push_back(cValues[c] / timeStep);
                ++c;
            }
        }
        rowStart[row + 1] = static_cast<ThermalSparseMatrix::Index>(inner.size());
    }

    std::expected<ThermalSparseMatrix, ThermalSparseMatrix::Failure> built =
        ThermalSparseMatrix::fromCsr(n, n, std::move(rowStart), std::move(inner), std::move(values),
                                     kEffectiveSymmetryTolerance);
    if (!built.has_value()) {
        const TransientProblem problem = built.error().problem == MatrixProblem::NonFiniteEntry
                                             ? TransientProblem::NonFiniteInput
                                             : TransientProblem::InconsistentSystem;
        return std::unexpected(TransientFailure{.problem = problem, .error = std::move(built.error().error)});
    }
    return std::move(*built);
}

// ---------------------------------------------------------------------------
// One step's acceptance
// ---------------------------------------------------------------------------

std::expected<double, TransientFailure>
physicalStepResidual(const ConductanceMatrix& conductance, const CapacityMatrix& capacity, const ThermalHeatVector& heat,
                     const ThermalFreeEquationMap& equations, std::span<const Temperature> previous,
                     std::span<const Temperature> next, double timeStep) {
    if (!std::isfinite(timeStep) || !(timeStep > 0.0)) {
        return std::unexpected(failure(TransientProblem::InvalidTimeStep, ErrorCode::InvalidArgument,
                                       std::format("the time step must be finite and positive, got {} s", timeStep)));
    }
    const std::size_t n = static_cast<std::size_t>(equations.dofCount());
    if (conductance.rows() != n || conductance.columns() != n || capacity.rows() != n || capacity.columns() != n ||
        heat.size() != n || previous.size() != n || next.size() != n) {
        return std::unexpected(failure(TransientProblem::DofMapMismatch, ErrorCode::InvalidArgument,
                                       std::format("K, C, Q and the two states must all have the numbering's {} unknowns", n)));
    }
    return physicalResidualOf(conductance, capacity, heat, equations, previous, next, timeStep);
}

std::expected<TransientStepResidual, TransientFailure>
validateTransientStep(const ConductanceMatrix& conductance, const CapacityMatrix& capacity,
                      const ThermalHeatVector& heat, const ThermalFreeEquationMap& equations,
                      const ThermalConstraintMap& constraints, std::span<const Temperature> previous,
                      std::span<const Temperature> next, double timeStep, const ThermalSolverSettings& settings) {
    if (Result<void> checked = validate(settings); !checked.has_value()) {
        return std::unexpected(TransientFailure{.problem = TransientProblem::InvalidSettings, .error = checked.error()});
    }
    if (!std::isfinite(timeStep) || !(timeStep > 0.0)) {
        return std::unexpected(failure(TransientProblem::InvalidTimeStep, ErrorCode::InvalidArgument,
                                       std::format("the time step must be finite and positive, got {} s", timeStep)));
    }
    const std::size_t n = static_cast<std::size_t>(equations.dofCount());
    if (conductance.rows() != n || conductance.columns() != n || capacity.rows() != n || capacity.columns() != n ||
        heat.size() != n || previous.size() != n || next.size() != n) {
        return std::unexpected(failure(TransientProblem::DofMapMismatch, ErrorCode::InvalidArgument,
                                       std::format("K, C, Q and the two states must all have the numbering's {} "
                                                   "unknowns",
                                                   n)));
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (!std::isfinite(previous[i].si())) {
            return std::unexpected(failure(TransientProblem::NonFiniteInput, ErrorCode::InvalidArgument,
                                           std::format("the previous temperature of unknown {} is not finite", i)));
        }
        if (!std::isfinite(next[i].si())) {
            return std::unexpected(failure(TransientProblem::NonFiniteSolution, ErrorCode::FailedPrecondition,
                                           std::format("the temperature of unknown {} is not finite", i)));
        }
    }
    if (!equations.agreesWith(constraints)) {
        return std::unexpected(failure(TransientProblem::ConstraintMismatch, ErrorCode::FailedPrecondition,
                                       "the prescribed temperatures are not the complement of the free numbering"));
    }
    if (std::optional<TransientFailure> broken = constraintsHeld(constraints, next); broken.has_value()) {
        return std::unexpected(std::move(*broken));
    }

    TransientStepResidual out;

    // The reduced residual: rebuilt here from K, C and Q, never read from the
    // matrix a solve factorised.
    std::expected<ThermalSparseMatrix, TransientFailure> effective = effectiveMatrix(conductance, capacity, timeStep);
    if (!effective.has_value()) {
        return std::unexpected(std::move(effective.error()));
    }
    std::expected<std::vector<double>, TransientFailure> rhs = historyRightHandSide(capacity, heat, previous, timeStep);
    if (!rhs.has_value()) {
        return std::unexpected(std::move(rhs.error()));
    }
    Result<ThermalHeatVector> rhsVector = ThermalHeatVector::fromEntries(std::move(*rhs));
    if (!rhsVector.has_value()) {
        return std::unexpected(TransientFailure{.problem = TransientProblem::NonFiniteInput, .error = rhsVector.error()});
    }
    std::expected<ReducedThermalSystem, SteadySolveFailure> reduced =
        reduceThermalSystem(*effective, *rhsVector, equations, constraints);
    if (!reduced.has_value()) {
        return std::unexpected(fromReduction(std::move(reduced.error())));
    }
    if (reduced->rows() > 0) {
        std::vector<double> freeValues(reduced->rows());
        const std::span<const ThermalDofIndex> freeDofs = equations.freeDofs();
        for (std::size_t equation = 0; equation < freeValues.size(); ++equation) {
            freeValues[equation] = next[static_cast<std::size_t>(freeDofs[equation].value())].si();
        }
        std::expected<linalg::ResidualNorms, linalg::SymmetricSolveFailure> norms =
            linalg::evaluateResidual(reduced->rows(), reduced->rowStart(), reduced->innerIndices(), reduced->values(),
                                     reduced->effectiveHeat(), freeValues, kernelSettings(settings));
        if (!norms.has_value()) {
            return std::unexpected(kernelFailure(norms.error(), settings));
        }
        out.reduced = norms->normalized;
    }

    // The physical residual: from the ORIGINAL arrays. It is what notices a system
    // that was built wrongly (C * dt, a wrong history sign) and then solved well.
    std::expected<double, TransientFailure> physical =
        physicalStepResidual(conductance, capacity, heat, equations, previous, next, timeStep);
    if (!physical.has_value()) {
        return std::unexpected(std::move(physical.error()));
    }
    if (!(*physical <= settings.relativeResidualTolerance)) {
        return std::unexpected(failure(TransientProblem::ResidualTooLarge, ErrorCode::FailedPrecondition,
                                       std::format("the physical residual C dT/dt + K T - Q of the free rows is too "
                                                   "large: normalized = {} against a threshold of {}. The step "
                                                   "satisfies the system that was solved but not the heat equation. A "
                                                   "step so small that the temperature change is near the rounding of "
                                                   "the temperature itself does this: the increment is below what the "
                                                   "state can represent",
                                                   *physical, settings.relativeResidualTolerance)));
    }
    out.physical = *physical;
    return out;
}

// ---------------------------------------------------------------------------
// The algebraic run
// ---------------------------------------------------------------------------

std::expected<ThermalTransientAlgebra, TransientFailure>
solveThermalTransientAlgebra(const ConductanceMatrix& conductance, const CapacityMatrix& capacity,
                             const ThermalHeatVector& heat, const ThermalFreeEquationMap& equations,
                             const ThermalConstraintMap& constraints, std::span<const Temperature> initial,
                             const TimeGrid& grid, TransientOutputPolicy output, const ThermalSolverSettings& settings) {
    if (Result<void> checked = validate(settings); !checked.has_value()) {
        return std::unexpected(TransientFailure{.problem = TransientProblem::InvalidSettings, .error = checked.error()});
    }
    const std::size_t n = static_cast<std::size_t>(equations.dofCount());
    if (conductance.rows() != n || conductance.columns() != n || capacity.rows() != n || capacity.columns() != n ||
        heat.size() != n || initial.size() != n) {
        return std::unexpected(failure(TransientProblem::DofMapMismatch, ErrorCode::FailedPrecondition,
                                       std::format("K is {} x {}, C is {} x {}, Q has {} entries and the initial "
                                                   "state {} but the numbering has {} unknowns",
                                                   conductance.rows(), conductance.columns(), capacity.rows(),
                                                   capacity.columns(), heat.size(), initial.size(), n)));
    }
    if (!equations.agreesWith(constraints)) {
        return std::unexpected(failure(TransientProblem::ConstraintMismatch, ErrorCode::FailedPrecondition,
                                       "the prescribed temperatures are not the complement of the free numbering, so "
                                       "the reduced rows would name unrelated unknowns"));
    }
    if (!knownPolicy(output)) {
        return std::unexpected(failure(TransientProblem::InvalidOutputPolicy, ErrorCode::InvalidArgument,
                                       "the transient output policy is not one this build knows"));
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (!isAbsoluteTemperature(initial[i])) {
            return std::unexpected(failure(TransientProblem::InvalidInitialTemperature, ErrorCode::InvalidArgument,
                                           std::format("the initial temperature of unknown {} ({} K) is not a finite "
                                                       "absolute temperature",
                                                       i, initial[i].si())));
        }
    }

    const std::size_t stepCount = grid.stepCount();
    const std::size_t sampleCount = output == TransientOutputPolicy::EveryStep ? stepCount + 1 : 1;
    if (n > 0 && sampleCount > kMaxStoredValues / n) {
        return std::unexpected(failure(TransientProblem::HistoryTooLarge, ErrorCode::InvalidArgument,
                                       std::format("storing {} states of {} unknowns would exceed the limit of {} "
                                                   "values; use the final-only output policy",
                                                   sampleCount, n, kMaxStoredValues)));
    }

    ThermalTransientAlgebra out{.grid = grid};
    out.dofCount = n;
    out.freeEquations = equations.freeCount();

    // PRESCRIBED DOMINATES AT t = 0. The initial temperature is a uniform guess for
    // the unknowns; a Dirichlet unknown is not unknown, and its first state is its
    // prescription. The override is counted, so it is visible and never silent.
    std::vector<Temperature> previous(initial.begin(), initial.end());
    for (const ThermalTemperatureConstraint& constraint : constraints.constrained()) {
        Temperature& slot = previous[static_cast<std::size_t>(constraint.dof.value())];
        if (!(slot.si() == constraint.temperature.si())) {
            ++out.initialOverrides;
        }
        slot = constraint.temperature;
    }
    out.initial = previous;

    out.samples.reserve(sampleCount * n);
    out.sampleTimes.reserve(sampleCount);
    out.steps.reserve(stepCount);
    if (output == TransientOutputPolicy::EveryStep) {
        out.sampleTimes.push_back(Time::fromSi(grid.timeAt(0)));
        out.samples.insert(out.samples.end(), previous.begin(), previous.end());
    }

    std::optional<ThermalSparseMatrix> effective;
    double effectiveStep = 0.0;
    for (std::size_t k = 0; k < stepCount; ++k) {
        const double timeStep = grid.stepAt(k);
        const double timeEnd = grid.timeAt(k + 1);
        const auto fail = [&](TransientFailure refused) {
            return std::unexpected(atStep(std::move(refused), k + 1, timeEnd));
        };

        // A = C / dt + K, rebuilt only when dt changes (the shortened last step).
        if (!effective.has_value() || effectiveStep != timeStep) {
            std::expected<ThermalSparseMatrix, TransientFailure> built = effectiveMatrix(conductance, capacity, timeStep);
            if (!built.has_value()) {
                return fail(std::move(built.error()));
            }
            effective = std::move(*built);
            effectiveStep = timeStep;
        }

        // b = Q + (C / dt) T[n]: the HISTORY term, from the state before this step.
        std::expected<std::vector<double>, TransientFailure> rhs = historyRightHandSide(capacity, heat, previous, timeStep);
        if (!rhs.has_value()) {
            return fail(std::move(rhs.error()));
        }
        Result<ThermalHeatVector> rhsVector = ThermalHeatVector::fromEntries(std::move(*rhs));
        if (!rhsVector.has_value()) {
            return fail(TransientFailure{.problem = TransientProblem::NonFiniteInput, .error = rhsVector.error()});
        }

        // The exact reduction: Aff Tf = bf - Afc Tc, the Tc term included.
        std::expected<ReducedThermalSystem, SteadySolveFailure> reduced =
            reduceThermalSystem(*effective, *rhsVector, equations, constraints);
        if (!reduced.has_value()) {
            return fail(fromReduction(std::move(reduced.error())));
        }

        TransientStepRecord record;
        record.index = k + 1;
        std::vector<Temperature> free(reduced->rows());
        if (reduced->rows() > 0) {
            std::expected<linalg::SymmetricSolution, linalg::SymmetricSolveFailure> solved =
                linalg::solveSymmetricSparse(reduced->rows(), reduced->rowStart(), reduced->innerIndices(),
                                             reduced->values(), reduced->effectiveHeat(), kernelSettings(settings));
            if (!solved.has_value()) {
                return fail(kernelFailure(solved.error(), settings));
            }
            record.kernelInvoked = true;
            record.pivotRatio = solved->pivotRatio;
            ++out.kernelSolves;
            for (std::size_t i = 0; i < free.size(); ++i) {
                free[i] = Temperature::fromSi(solved->x[i]);
            }
        }

        Result<std::vector<Temperature>> next = reconstructTemperatures(equations, constraints, free);
        if (!next.has_value()) {
            return fail(TransientFailure{.problem = TransientProblem::ConstraintMismatch, .error = next.error()});
        }

        // THE STEP'S ACCEPTANCE, independent of everything above.
        std::expected<TransientStepResidual, TransientFailure> accepted =
            validateTransientStep(conductance, capacity, heat, equations, constraints, previous, *next, timeStep, settings);
        if (!accepted.has_value()) {
            return fail(std::move(accepted.error()));
        }
        record.reducedResidual = accepted->reduced;
        record.physicalResidual = accepted->physical;
        out.steps.push_back(record);

        previous = std::move(*next);
        if (output == TransientOutputPolicy::EveryStep) {
            out.sampleTimes.push_back(Time::fromSi(timeEnd));
            out.samples.insert(out.samples.end(), previous.begin(), previous.end());
        }
    }

    if (output == TransientOutputPolicy::FinalOnly) {
        out.sampleTimes.push_back(Time::fromSi(grid.timeAt(stepCount)));
        out.samples.insert(out.samples.end(), previous.begin(), previous.end());
    }
    return out;
}

// ---------------------------------------------------------------------------
// Provenance
// ---------------------------------------------------------------------------

namespace {

[[nodiscard]] TransientStaleReason fromSystemReason(SystemStaleReason reason) noexcept {
    switch (reason) {
    case SystemStaleReason::Body:
        return TransientStaleReason::Body;
    case SystemStaleReason::Mesh:
        return TransientStaleReason::Mesh;
    case SystemStaleReason::Geometry:
        return TransientStaleReason::Geometry;
    case SystemStaleReason::Mode:
        return TransientStaleReason::Mode;
    case SystemStaleReason::Material:
        return TransientStaleReason::Material;
    case SystemStaleReason::Loads:
        return TransientStaleReason::Loads;
    case SystemStaleReason::Convections:
        return TransientStaleReason::Convections;
    }
    return TransientStaleReason::Mesh;
}

[[nodiscard]] std::vector<ThermalBoundaryCondition>
prescribedConditions(std::span<const ThermalBoundaryCondition> conditions) {
    std::vector<ThermalBoundaryCondition> out;
    for (const ThermalBoundaryCondition& condition : conditions) {
        if (condition.prescribedTemperature() != nullptr) {
            out.push_back(condition);
        }
    }
    return out;
}

[[nodiscard]] std::string reasonsText(const std::vector<TransientStaleReason>& reasons) {
    std::string names;
    for (const TransientStaleReason reason : reasons) {
        if (!names.empty()) {
            names += ", ";
        }
        names += toString(reason);
    }
    return names;
}

} // namespace

std::vector<TransientStaleReason> staleReasons(const TransientSolutionSource& solved,
                                               const TransientSolutionSource& current) {
    std::vector<TransientStaleReason> reasons;
    for (const SystemStaleReason reason : staleReasons(solved.system, current.system)) {
        reasons.push_back(fromSystemReason(reason));
    }
    if (!(solved.temperatures == current.temperatures)) {
        reasons.push_back(TransientStaleReason::PrescribedTemperatures);
    }
    if (!(solved.initial == current.initial)) {
        reasons.push_back(TransientStaleReason::InitialCondition);
    }
    if (!(solved.transient.timeStep == current.transient.timeStep)) {
        reasons.push_back(TransientStaleReason::TimeStep);
    }
    if (!(solved.transient.endTime == current.transient.endTime)) {
        reasons.push_back(TransientStaleReason::EndTime);
    }
    if (!(solved.transient.output == current.transient.output)) {
        reasons.push_back(TransientStaleReason::OutputPolicy);
    }
    if (!(solved.settings == current.settings)) {
        reasons.push_back(TransientStaleReason::Settings);
    }
    return reasons;
}

Result<TransientSolutionSource> currentTransientSolutionSource(const Document& document, const ThermalModel& model,
                                                               std::span<const ThermalLoad> loads,
                                                               std::span<const ThermalBoundaryCondition> conditions,
                                                               const UniformInitialTemperature& initial,
                                                               const TransientSettings& transient,
                                                               const ThermalSolverSettings& settings) {
    Result<ThermalSystemSource> system =
        currentThermalSystemSource(document, model, ThermalAnalysisMode::Transient, loads, conditions);
    if (!system.has_value()) {
        return std::unexpected(system.error());
    }
    return TransientSolutionSource{.system = std::move(*system),
                                   .temperatures = prescribedConditions(conditions),
                                   .initial = initial,
                                   .transient = transient,
                                   .settings = settings};
}

bool isCurrent(const TransientThermalSolution& solution, const Document& document, const ThermalModel& model,
               std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
               const UniformInitialTemperature& initial, const TransientSettings& transient,
               const ThermalSolverSettings& settings) {
    return requireCurrent(solution, document, model, loads, conditions, initial, transient, settings).has_value();
}

Result<void> requireCurrent(const TransientThermalSolution& solution, const Document& document,
                            const ThermalModel& model, std::span<const ThermalLoad> loads,
                            std::span<const ThermalBoundaryCondition> conditions,
                            const UniformInitialTemperature& initial, const TransientSettings& transient,
                            const ThermalSolverSettings& settings) {
    const Result<TransientSolutionSource> current =
        currentTransientSolutionSource(document, model, loads, conditions, initial, transient, settings);
    if (!current.has_value()) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("the history cannot be compared with the model: {}", current.error().message));
    }
    const std::vector<TransientStaleReason> reasons = staleReasons(solution.source(), *current);
    if (reasons.empty()) {
        return Result<void>{};
    }
    return makeError(ErrorCode::FailedPrecondition,
                     std::format("the solved temperature history no longer describes the model: {} changed. Solve "
                                 "it again",
                                 reasonsText(reasons)));
}

// ---------------------------------------------------------------------------
// TransientThermalSolution
// ---------------------------------------------------------------------------

Time TransientThermalSolution::timeOf(std::size_t index) const noexcept {
    return index < data_.sampleTimes.size() ? data_.sampleTimes[index] : Time{};
}

std::span<const Temperature> TransientThermalSolution::temperaturesAt(std::size_t index) const noexcept {
    if (index >= data_.sampleTimes.size()) {
        return {};
    }
    return std::span<const Temperature>(data_.samples).subspan(index * data_.dofCount, data_.dofCount);
}

std::span<const Temperature> TransientThermalSolution::finalTemperatures() const noexcept {
    return data_.sampleTimes.empty() ? std::span<const Temperature>{} : temperaturesAt(data_.sampleTimes.size() - 1);
}

Result<Temperature> TransientThermalSolution::temperatureOf(std::size_t index, ThermalDofIndex dof) const {
    if (index >= data_.sampleTimes.size()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("sample {} is outside this history's {} samples", index, sampleCount()));
    }
    if (dof.value() >= data_.dofCount) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("unknown {} is outside this history's {} unknowns", dof.value(), data_.dofCount));
    }
    return temperaturesAt(index)[static_cast<std::size_t>(dof.value())];
}

Result<Temperature> TransientThermalSolution::temperatureOf(std::size_t index, const ThermalDofMap& map,
                                                            meshing::NodeId node) const {
    if (!describes(map)) {
        return makeError(ErrorCode::FailedPrecondition,
                         "this history belongs to another numbering, so a node cannot be looked up through it");
    }
    const Result<ThermalDofIndex> dof = map.dofOf(node);
    if (!dof.has_value()) {
        return std::unexpected(dof.error());
    }
    return temperatureOf(index, *dof);
}

double TransientThermalSolution::worstReducedResidual() const noexcept {
    double worst = 0.0;
    for (const TransientStepRecord& record : data_.steps) {
        worst = std::max(worst, record.reducedResidual);
    }
    return worst;
}

double TransientThermalSolution::worstPhysicalResidual() const noexcept {
    double worst = 0.0;
    for (const TransientStepRecord& record : data_.steps) {
        worst = std::max(worst, record.physicalResidual);
    }
    return worst;
}

// ---------------------------------------------------------------------------
// The solves
// ---------------------------------------------------------------------------

namespace {

/// The settings, on their own terms, classified. The canonical `validate` of the
/// transient settings (`0 < dt <= t_end`, both finite) is applied: the time grid is
/// more permissive (`t_end = 0`, `dt > t_end`) so that its edge cases can be tested,
/// but the canonical data can express neither and a solve of one is refused.
[[nodiscard]] std::optional<TransientFailure> settingsFailure(const UniformInitialTemperature& initial,
                                                              const TransientSettings& transient,
                                                              const ThermalSolverSettings& settings) {
    if (Result<void> checked = validate(settings); !checked.has_value()) {
        return TransientFailure{.problem = TransientProblem::InvalidSettings, .error = checked.error()};
    }
    const double dt = transient.timeStep.si();
    const double end = transient.endTime.si();
    if (!std::isfinite(dt) || !(dt > 0.0)) {
        return failure(TransientProblem::InvalidTimeStep, ErrorCode::InvalidArgument,
                       std::format("the time step must be finite and positive, got {} s", dt));
    }
    if (!std::isfinite(end) || !(end > 0.0) || end < dt) {
        return failure(TransientProblem::InvalidEndTime, ErrorCode::InvalidArgument,
                       std::format("the end time must be finite, positive and at least one time step ({} s), got "
                                   "{} s",
                                   dt, end));
    }
    if (!knownPolicy(transient.output)) {
        return failure(TransientProblem::InvalidOutputPolicy, ErrorCode::InvalidArgument,
                       "the transient output policy is not one this build knows");
    }
    if (Result<void> checked = validate(initial); !checked.has_value()) {
        return TransientFailure{.problem = TransientProblem::InvalidInitialTemperature, .error = checked.error()};
    }
    return std::nullopt;
}

} // namespace

std::expected<TransientThermalSolution, TransientFailure>
solveTransientThermalSystem(const ThermalSystem& system, const ThermalDofMap& map, const ThermalConstraintMap& constraints,
                            const UniformInitialTemperature& initial, const TransientSettings& transient,
                            const ThermalSolverSettings& settings,
                            std::vector<ThermalBoundaryCondition> temperatureConditions) {
    if (std::optional<TransientFailure> refused = settingsFailure(initial, transient, settings); refused.has_value()) {
        return std::unexpected(std::move(*refused));
    }
    // A system's mode is DERIVED from whether it carries a capacity (ThermalSystem.cpp), so the two cannot disagree and one
    // test is the whole check.
    if (!system.capacity().has_value()) {
        return std::unexpected(failure(TransientProblem::NotTransientSystem, ErrorCode::FailedPrecondition,
                                       "the assembled system was built for a steady analysis and carries no heat "
                                       "capacity; a transient solve needs a system assembled in transient mode"));
    }
    if (!system.describes(map)) {
        return std::unexpected(failure(TransientProblem::DofMapMismatch, ErrorCode::FailedPrecondition,
                                       "the thermal numbering is not the one the system was assembled against, so "
                                       "its unknowns are not this system's rows"));
    }
    Result<ThermalFreeEquationMap> equations = buildThermalFreeEquationMap(map, constraints);
    if (!equations.has_value()) {
        return std::unexpected(
            TransientFailure{.problem = TransientProblem::ConstraintMismatch, .error = equations.error()});
    }

    std::expected<TimeGrid, TransientFailure> grid = TimeGrid::create(transient.timeStep.si(), transient.endTime.si());
    if (!grid.has_value()) {
        return std::unexpected(std::move(grid.error()));
    }

    const std::vector<Temperature> start(system.dofCount(), initial.value);
    std::expected<ThermalTransientAlgebra, TransientFailure> run =
        solveThermalTransientAlgebra(system.conductance(), *system.capacity(), system.heat(), *equations, constraints,
                                     start, *grid, transient.output, settings);
    if (!run.has_value()) {
        return std::unexpected(std::move(run.error()));
    }
    // Published only here, through the one private constructor: a refused run
    // cannot leave a partial history behind.
    return TransientThermalSolution{TransientSolutionSource{.system = system.source(),
                                                            .temperatures = std::move(temperatureConditions),
                                                            .initial = initial,
                                                            .transient = transient,
                                                            .settings = settings},
                                    std::move(*run)};
}

namespace {

[[nodiscard]] std::expected<TransientThermalSolution, TransientFailure>
runTransient(const Document& document, const ThermalModel& model, const ThermalDofMap& map, const ThermalSystem& system,
             const PreparedThermalBoundary& preparedBoundary, std::span<const ThermalLoad> loads,
             std::span<const ThermalBoundaryCondition> conditions, const UniformInitialTemperature& initial,
             const TransientSettings& transient, const ThermalSolverSettings& settings) {
    if (std::optional<TransientFailure> refused = settingsFailure(initial, transient, settings); refused.has_value()) {
        return std::unexpected(std::move(*refused));
    }

    // ALL OF THESE BEFORE ANY ARITHMETIC, so a stale input is reported as one and
    // never as whatever numerical symptom it would have caused.
    if (Result<void> current = requireCurrent(system, document, model, ThermalAnalysisMode::Transient, loads, conditions);
        !current.has_value()) {
        return std::unexpected(TransientFailure{.problem = TransientProblem::StaleSystem, .error = current.error()});
    }
    if (!preparedBoundary.describes(map)) {
        return std::unexpected(failure(TransientProblem::DofMapMismatch, ErrorCode::FailedPrecondition,
                                       "the prepared boundary was built for a different numbering than the one "
                                       "presented"));
    }
    if (const std::vector<BoundaryStaleReason> stale =
            staleReasons(preparedBoundary.source(), currentBoundarySource(model, conditions));
        !stale.empty()) {
        std::string names;
        for (const BoundaryStaleReason reason : stale) {
            names += names.empty() ? "" : ", ";
            names += toString(reason);
        }
        return std::unexpected(failure(TransientProblem::StaleBoundary, ErrorCode::FailedPrecondition,
                                       std::format("the prepared boundary no longer describes the model or the "
                                                   "conditions: {} changed",
                                                   names)));
    }
    return solveTransientThermalSystem(system, map, preparedBoundary.constraints(), initial, transient, settings,
                                       prescribedConditions(conditions));
}

} // namespace

Result<TransientThermalSolution>
solveTransientThermal(const Document& document, const ThermalModel& model, const ThermalDofMap& map,
                      const ThermalSystem& system, const PreparedThermalBoundary& preparedBoundary,
                      std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
                      const UniformInitialTemperature& initial, const TransientSettings& transient,
                      const ThermalSolverSettings& settings) {
    std::expected<TransientThermalSolution, TransientFailure> solved =
        runTransient(document, model, map, system, preparedBoundary, loads, conditions, initial, transient, settings);
    if (!solved.has_value()) {
        return std::unexpected(std::move(solved.error().error));
    }
    return std::move(*solved);
}

std::optional<TransientFailure>
transientSolveFailure(const Document& document, const ThermalModel& model, const ThermalDofMap& map,
                      const ThermalSystem& system, const PreparedThermalBoundary& preparedBoundary,
                      std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
                      const UniformInitialTemperature& initial, const TransientSettings& transient,
                      const ThermalSolverSettings& settings) {
    std::expected<TransientThermalSolution, TransientFailure> solved =
        runTransient(document, model, map, system, preparedBoundary, loads, conditions, initial, transient, settings);
    if (solved.has_value()) {
        return std::nullopt;
    }
    return std::move(solved.error());
}

std::optional<TransientFailure>
transientSolveFailure(const Document& document, const ThermalModel& model, const ThermalDofMap& map,
                      const ThermalSystem& system, const PreparedThermalBoundary& preparedBoundary,
                      const ThermalAnalysisDefinition& definition) {
    if (definition.mode != ThermalAnalysisMode::Transient || !definition.initialCondition.has_value() ||
        !definition.transient.has_value()) {
        return failure(TransientProblem::NotTransientDefinition, ErrorCode::FailedPrecondition,
                       "the analysis is not a transient one: it needs the transient mode, an initial condition and "
                       "transient settings");
    }
    return transientSolveFailure(document, model, map, system, preparedBoundary, definition.loads,
                                 definition.boundaryConditions, *definition.initialCondition, *definition.transient,
                                 definition.solver);
}

Result<TransientThermalSolution>
solveTransientThermal(const Document& document, const ThermalModel& model, const ThermalDofMap& map,
                      const ThermalSystem& system, const PreparedThermalBoundary& preparedBoundary,
                      const ThermalAnalysisDefinition& definition) {
    if (definition.mode != ThermalAnalysisMode::Transient || !definition.initialCondition.has_value() ||
        !definition.transient.has_value()) {
        return makeError(ErrorCode::FailedPrecondition,
                         "the analysis is not a transient one: it needs the transient mode, an initial condition and "
                         "transient settings");
    }
    return solveTransientThermal(document, model, map, system, preparedBoundary, definition.loads,
                                 definition.boundaryConditions, *definition.initialCondition, *definition.transient,
                                 definition.solver);
}

} // namespace bettercad::thermal
