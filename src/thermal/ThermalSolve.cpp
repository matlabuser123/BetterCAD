// The steady thermal solve (P18-SOLVE-001).
//
// THE FACTORISATION IS NOT HERE. `linalg::solveSymmetricSparse` is P17's
// qualified kernel moved below layer 45 (ADR-052), and this file is its thermal
// caller: the reduction `Kff Tf = Qf - Kfc Tc`, the question only a conduction
// model can ask (is every region anchored to an absolute temperature?), the
// reconstruction with the prescribed temperatures held exactly, and the
// provenance. No elimination, no Cholesky, no CG is written in this file.
//
// WHAT IS NOT HERE EITHER: no penalty, no epsilon on a diagonal, no pinned
// unknown, no reference temperature, no pseudo-inverse, no fallback to a second
// algorithm, no dense reduced matrix. A singular model fails.

#include <bettercad/thermal/ThermalSolve.hpp>

#include <bettercad/linalg/SymmetricSolve.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <numeric>
#include <utility>

namespace bettercad::thermal {
namespace {

constexpr std::size_t kNone = std::numeric_limits<std::size_t>::max();

[[nodiscard]] SteadySolveFailure failure(SteadySolveProblem problem, ErrorCode code, std::string message) {
    return SteadySolveFailure{.problem = problem, .error = makeError(code, std::move(message)).error()};
}

[[nodiscard]] linalg::SymmetricSolveSettings kernelSettings(const ThermalSolverSettings& settings) {
    return linalg::SymmetricSolveSettings{.relativeResidualTolerance = settings.relativeResidualTolerance,
                                          .pivotFloor = settings.pivotFloor};
}

[[nodiscard]] SteadyResidual residualOf(const linalg::ResidualNorms& norms) {
    SteadyResidual out;
    out.euclideanNorm = Power::fromSi(norms.euclidean);
    out.infinityNorm = Power::fromSi(norms.infinity);
    out.conductanceInfinityNorm = ThermalConductance::fromSi(norms.matrixInfinity);
    out.temperatureNorm = Temperature::fromSi(norms.solution);
    out.heatNorm = Power::fromSi(norms.rightHandSide);
    out.normalized = norms.normalized;
    return out;
}

/// The kernel's classification in the thermal vocabulary. A kernel refusal is
/// translated here and the kernel's structured numbers are used to say it; the
/// lower layer never learns what a temperature is.
[[nodiscard]] SteadySolveFailure thermalFailure(const linalg::SymmetricSolveFailure& refused,
                                                const ThermalSolverSettings& settings) {
    switch (refused.problem) {
    case linalg::SymmetricSolveProblem::InvalidSettings:
        return SteadySolveFailure{.problem = SteadySolveProblem::InvalidSettings, .error = refused.error};
    case linalg::SymmetricSolveProblem::NonFiniteSystem:
        return failure(SteadySolveProblem::NonFiniteInput, ErrorCode::InvalidArgument,
                       std::format("the reduced conduction system holds an entry that is not finite ({}); "
                                   "nothing was factorised",
                                   refused.error.message));
    case linalg::SymmetricSolveProblem::DimensionMismatch:
    case linalg::SymmetricSolveProblem::FactorizationFailure:
        return failure(SteadySolveProblem::FactorizationFailure, ErrorCode::FailedPrecondition,
                       std::format("the linear solve of the {} reduced equations could not be carried out: {}",
                                   refused.rows, refused.error.message));
    case linalg::SymmetricSolveProblem::SingularSystem:
        if (refused.exactlyZeroPivot) {
            return failure(SteadySolveProblem::SingularSystem, ErrorCode::FailedPrecondition,
                           std::format("the reduced conduction system of {} equations is singular: the "
                                       "factorisation found an exactly zero pivot although every region is "
                                       "anchored, so the matrix is degenerate (a collapsed element or an "
                                       "extreme conductance contrast)",
                                       refused.rows));
        }
        return failure(SteadySolveProblem::SingularSystem, ErrorCode::FailedPrecondition,
                       std::format("the reduced conduction system of {} equations is singular or too ill "
                                   "conditioned to solve although every region is anchored: {}. The pivot floor "
                                   "is {}",
                                   refused.rows, refused.error.message, settings.pivotFloor));
    case linalg::SymmetricSolveProblem::NonFiniteSolution:
        return failure(SteadySolveProblem::NonFiniteSolution, ErrorCode::FailedPrecondition,
                       std::format("the solved temperature of free equation {} is not finite although the "
                                   "solver reported success",
                                   refused.index));
    case linalg::SymmetricSolveProblem::NonFiniteResidual:
        return failure(SteadySolveProblem::NonFiniteResidual, ErrorCode::FailedPrecondition,
                       std::format("the residual of free equation {} is not finite", refused.index));
    case linalg::SymmetricSolveProblem::ResidualTooLarge:
        return failure(SteadySolveProblem::ResidualTooLarge, ErrorCode::FailedPrecondition,
                       std::format("the independent residual is too large: ||r||_2 = {} W, ||r||_inf = {} W, "
                                   "normalized = {} against a threshold of {}. The solver reported success; "
                                   "BetterCAD does not",
                                   refused.residual.euclidean, refused.residual.infinity,
                                   refused.residual.normalized, settings.relativeResidualTolerance));
    }
    return failure(SteadySolveProblem::FactorizationFailure, ErrorCode::Internal, "an unclassified solver refusal");
}

/// A row-by-row binary search of a CSR. One definition of what a lookup is here.
[[nodiscard]] double csrAt(const std::vector<ThermalSparseMatrix::Index>& rowStart,
                           const std::vector<ThermalSparseMatrix::Index>& inner, const std::vector<double>& values,
                           std::size_t rows, std::size_t row, std::size_t column) noexcept {
    if (row >= rows) {
        return 0.0;
    }
    const auto first = inner.begin() + static_cast<std::ptrdiff_t>(rowStart[row]);
    const auto last = inner.begin() + static_cast<std::ptrdiff_t>(rowStart[row + 1]);
    const auto found = std::lower_bound(first, last, static_cast<ThermalSparseMatrix::Index>(column));
    if (found == last || *found != static_cast<ThermalSparseMatrix::Index>(column)) {
        return 0.0;
    }
    return values[static_cast<std::size_t>(found - inner.begin())];
}

} // namespace

std::string_view toString(SteadySolveProblem problem) noexcept {
    switch (problem) {
    case SteadySolveProblem::InvalidSettings:
        return "invalid_settings";
    case SteadySolveProblem::NotSteadySystem:
        return "not_steady_system";
    case SteadySolveProblem::DofMapMismatch:
        return "dof_map_mismatch";
    case SteadySolveProblem::ConstraintMismatch:
        return "constraint_mismatch";
    case SteadySolveProblem::StaleSystem:
        return "stale_system";
    case SteadySolveProblem::StaleBoundary:
        return "stale_boundary";
    case SteadySolveProblem::NonFiniteInput:
        return "non_finite_input";
    case SteadySolveProblem::MissingTemperatureDatum:
        return "missing_temperature_datum";
    case SteadySolveProblem::UnanchoredThermalRegion:
        return "unanchored_thermal_region";
    case SteadySolveProblem::SingularSystem:
        return "singular_system";
    case SteadySolveProblem::FactorizationFailure:
        return "factorization_failure";
    case SteadySolveProblem::NonFiniteSolution:
        return "non_finite_solution";
    case SteadySolveProblem::NonFiniteResidual:
        return "non_finite_residual";
    case SteadySolveProblem::ResidualTooLarge:
        return "residual_too_large";
    case SteadySolveProblem::ConstraintNotPreserved:
        return "constraint_not_preserved";
    }
    return "unknown";
}

std::string_view toString(SolutionStaleReason reason) noexcept {
    switch (reason) {
    case SolutionStaleReason::Body:
        return "body";
    case SolutionStaleReason::Mesh:
        return "mesh";
    case SolutionStaleReason::Geometry:
        return "geometry";
    case SolutionStaleReason::Mode:
        return "mode";
    case SolutionStaleReason::Material:
        return "material";
    case SolutionStaleReason::Loads:
        return "loads";
    case SolutionStaleReason::Convections:
        return "convections";
    case SolutionStaleReason::PrescribedTemperatures:
        return "prescribed_temperatures";
    case SolutionStaleReason::Settings:
        return "settings";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// The reduced system
// ---------------------------------------------------------------------------

double ReducedThermalSystem::kffAt(std::size_t row, std::size_t column) const noexcept {
    return csrAt(rowStart_, inner_, values_, rows_, row, column);
}

double ReducedThermalSystem::kfcAt(std::size_t row, std::size_t ordinal) const noexcept {
    return csrAt(couplingRowStart_, couplingInner_, couplingValues_, rows_, row, ordinal);
}

std::expected<ReducedThermalSystem, SteadySolveFailure>
reduceThermalSystem(const ThermalSparseMatrix& conductance, const ThermalHeatVector& heat,
                    const ThermalFreeEquationMap& equations, const ThermalConstraintMap& constraints) {
    const std::size_t n = static_cast<std::size_t>(equations.dofCount());
    if (conductance.rows() != n || conductance.columns() != n || heat.size() != n) {
        return std::unexpected(failure(SteadySolveProblem::DofMapMismatch, ErrorCode::FailedPrecondition,
                                       std::format("the system is {} x {} with {} heat entries but the numbering "
                                                   "has {} unknowns",
                                                   conductance.rows(), conductance.columns(), heat.size(), n)));
    }
    if (!equations.agreesWith(constraints)) {
        return std::unexpected(failure(SteadySolveProblem::ConstraintMismatch, ErrorCode::FailedPrecondition,
                                       "the prescribed temperatures are not the complement of the free "
                                       "numbering, so the reduced rows would name unrelated unknowns"));
    }

    // freeOf[d] is the free equation of unknown d, or kNone when it is prescribed;
    // ordinalOf[d] is the prescribed ordinal, or kNone when it is free. Both are
    // derived from the maps and carry the only conversion between index spaces.
    const std::span<const ThermalDofIndex> freeDofs = equations.freeDofs();
    std::vector<std::size_t> freeOf(n, kNone);
    for (std::size_t equation = 0; equation < freeDofs.size(); ++equation) {
        freeOf[static_cast<std::size_t>(freeDofs[equation].value())] = equation;
    }
    std::vector<std::size_t> ordinalOf(n, kNone);
    const std::span<const ThermalTemperatureConstraint> prescribed = constraints.constrained();
    for (std::size_t ordinal = 0; ordinal < prescribed.size(); ++ordinal) {
        ordinalOf[static_cast<std::size_t>(prescribed[ordinal].dof.value())] = ordinal;
    }

    ReducedThermalSystem out;
    out.rows_ = freeDofs.size();
    out.rowStart_.assign(out.rows_ + 1, 0);
    out.couplingRowStart_.assign(out.rows_ + 1, 0);
    out.qf_.assign(out.rows_, 0.0);
    out.beff_.assign(out.rows_, 0.0);
    out.tc_.reserve(prescribed.size());
    for (const ThermalTemperatureConstraint& constraint : prescribed) {
        out.tc_.push_back(constraint.temperature.si());
    }

    // ONE PASS, IN FreeEquationIndex ORDER. `freeDofs()` is ascending, so the
    // global rows are visited ascending and the inner indices of each reduced row
    // come out ascending without a sort.
    const std::span<const ThermalSparseMatrix::Index> rowStart = conductance.rowStart();
    const std::span<const ThermalSparseMatrix::Index> inner = conductance.innerIndices();
    const std::span<const double> values = conductance.values();
    for (std::size_t equation = 0; equation < out.rows_; ++equation) {
        const auto globalRow = static_cast<std::size_t>(freeDofs[equation].value());
        // Kfc Tc is summed in the row's own column order, so the order is fixed.
        double coupled = 0.0;
        for (ThermalSparseMatrix::Index slot = rowStart[globalRow]; slot < rowStart[globalRow + 1]; ++slot) {
            const auto globalColumn = static_cast<std::size_t>(inner[slot]);
            if (freeOf[globalColumn] != kNone) {
                out.inner_.push_back(static_cast<ReducedThermalSystem::Index>(freeOf[globalColumn]));
                out.values_.push_back(values[slot]);
            } else {
                const std::size_t ordinal = ordinalOf[globalColumn];
                out.couplingInner_.push_back(static_cast<ReducedThermalSystem::Index>(ordinal));
                out.couplingValues_.push_back(values[slot]);
                coupled += values[slot] * out.tc_[ordinal];
            }
        }
        out.rowStart_[equation + 1] = static_cast<ReducedThermalSystem::Index>(out.inner_.size());
        out.couplingRowStart_[equation + 1] = static_cast<ReducedThermalSystem::Index>(out.couplingInner_.size());
        out.qf_[equation] = heat.values()[globalRow];
        // beff = Qf - Kfc Tc. THE TERM THAT P17 DID NOT NEED: Tc is not zero.
        out.beff_[equation] = out.qf_[equation] - coupled;
        if (!std::isfinite(out.beff_[equation])) {
            return std::unexpected(failure(SteadySolveProblem::NonFiniteInput, ErrorCode::InvalidArgument,
                                           std::format("the reduced right-hand side of free equation {} (unknown "
                                                       "{}) is not finite: Qf - Kfc Tc overflowed",
                                                       equation, freeDofs[equation].value())));
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Well-posedness
// ---------------------------------------------------------------------------

AnchorAnalysis analyseAnchoring(const ReducedThermalSystem& reduced) {
    const std::size_t rows = reduced.rows();
    AnchorAnalysis out;
    if (rows == 0) {
        return out;
    }

    // Union-find over the NONZERO couplings. A stored zero is not a coupling: a
    // pattern built from connectivity alone holds exact zeros, and joining two
    // regions through one would hide a region that no numbers connect.
    std::vector<std::size_t> parent(rows);
    std::iota(parent.begin(), parent.end(), std::size_t{0});
    const auto find = [&parent](std::size_t x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    };
    std::vector<bool> anchoredRow(rows, false);
    for (std::size_t row = 0; row < rows; ++row) {
        double sum = 0.0;
        double absolute = 0.0;
        for (ReducedThermalSystem::Index slot = reduced.rowStart()[row]; slot < reduced.rowStart()[row + 1]; ++slot) {
            const double value = reduced.values()[slot];
            sum += value;
            absolute += std::abs(value);
            const auto column = static_cast<std::size_t>(reduced.innerIndices()[slot]);
            if (value != 0.0 && column != row) {
                const std::size_t a = find(row);
                const std::size_t b = find(column);
                if (a != b) {
                    // The smaller representative wins, so the roots do not depend on
                    // the order the edges were met in.
                    parent[std::max(a, b)] = std::min(a, b);
                }
            }
        }
        anchoredRow[row] = std::abs(sum) > kAnchorTolerance * absolute;
    }

    std::vector<bool> anchoredRegion(rows, false);
    for (std::size_t row = 0; row < rows; ++row) {
        if (anchoredRow[row]) {
            anchoredRegion[find(row)] = true;
        }
    }
    for (std::size_t row = 0; row < rows; ++row) {
        if (find(row) != row) {
            continue;
        }
        ++out.regions;
        if (anchoredRegion[row]) {
            ++out.anchored;
        } else {
            ++out.unanchored;
            if (!out.firstUnanchoredEquation.has_value()) {
                out.firstUnanchoredEquation = row;
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// The algebraic solve
// ---------------------------------------------------------------------------

namespace {

/// `K T - Q` over the FULL system, W. Free entries come out ~0; prescribed
/// entries are the heat rates the boundary supplies, correctly non-zero.
[[nodiscard]] std::expected<std::vector<double>, SteadySolveFailure>
fullResidualOf(const ThermalSparseMatrix& conductance, const ThermalHeatVector& heat,
               std::span<const Temperature> temperatures) {
    std::vector<double> out(conductance.rows(), 0.0);
    const std::span<const ThermalSparseMatrix::Index> rowStart = conductance.rowStart();
    const std::span<const ThermalSparseMatrix::Index> inner = conductance.innerIndices();
    const std::span<const double> values = conductance.values();
    for (std::size_t row = 0; row < out.size(); ++row) {
        double product = 0.0;
        for (ThermalSparseMatrix::Index slot = rowStart[row]; slot < rowStart[row + 1]; ++slot) {
            product += values[slot] * temperatures[static_cast<std::size_t>(inner[slot])].si();
        }
        const double entry = product - heat.values()[row];
        if (!std::isfinite(entry)) {
            return std::unexpected(failure(SteadySolveProblem::NonFiniteResidual, ErrorCode::FailedPrecondition,
                                           std::format("full residual component {} (unknown {}) is not finite", row,
                                                       row)));
        }
        out[row] = entry;
    }
    return out;
}

/// Every prescribed unknown holds its prescribed temperature EXACTLY. Equality of
/// doubles, deliberately: the value was assigned, not computed, and a tolerance
/// here would admit a solver that approximated a boundary condition.
[[nodiscard]] std::optional<SteadySolveFailure> constraintsHeld(const ThermalConstraintMap& constraints,
                                                                std::span<const Temperature> temperatures) {
    for (const ThermalTemperatureConstraint& constraint : constraints.constrained()) {
        const double wanted = constraint.temperature.si();
        const double actual = temperatures[static_cast<std::size_t>(constraint.dof.value())].si();
        if (!(actual == wanted)) {
            return failure(SteadySolveProblem::ConstraintNotPreserved, ErrorCode::FailedPrecondition,
                           std::format("unknown {} is prescribed at {} K but holds {} K", constraint.dof.value(),
                                       wanted, actual));
        }
    }
    return std::nullopt;
}

} // namespace

std::expected<ThermalAlgebraicSolution, SteadySolveFailure>
solveThermalAlgebra(const ThermalSparseMatrix& conductance, const ThermalHeatVector& heat,
                    const ThermalFreeEquationMap& equations, const ThermalConstraintMap& constraints,
                    const ThermalSolverSettings& settings) {
    if (Result<void> checked = validate(settings); !checked.has_value()) {
        return std::unexpected(
            SteadySolveFailure{.problem = SteadySolveProblem::InvalidSettings, .error = checked.error()});
    }

    std::expected<ReducedThermalSystem, SteadySolveFailure> reduced =
        reduceThermalSystem(conductance, heat, equations, constraints);
    if (!reduced.has_value()) {
        return std::unexpected(std::move(reduced.error()));
    }

    ThermalAlgebraicSolution out;
    out.freeEquations = reduced->rows();
    out.freeNonZeros = reduced->nonZeros();
    out.anchoring = analyseAnchoring(*reduced);

    std::vector<Temperature> free(reduced->rows());
    if (reduced->rows() > 0) {
        // EVERY INDEPENDENT REGION MUST BE ANCHORED, and this is found in the
        // numbers before the factorisation spends time proving it. An unanchored
        // region has the constant temperature in its null space; no arbitrary level
        // is chosen for it.
        if (out.anchoring.unanchored > 0) {
            const std::size_t equation = *out.anchoring.firstUnanchoredEquation;
            const ThermalDofIndex dof = *equations.dofOf(ThermalFreeEquationIndex::fromValue(equation));
            if (out.anchoring.anchored == 0) {
                return std::unexpected(failure(
                    SteadySolveProblem::MissingTemperatureDatum, ErrorCode::FailedPrecondition,
                    std::format("the steady model has no absolute temperature datum: none of its {} independent "
                                "region(s) is tied to a prescribed temperature or a convection face, so the "
                                "constant temperature is in the null space and the solution is not unique. "
                                "Prescribe a temperature or add a convection condition (unknown {} is in the "
                                "first such region)",
                                out.anchoring.regions, dof.value())));
            }
            return std::unexpected(failure(
                SteadySolveProblem::UnanchoredThermalRegion, ErrorCode::FailedPrecondition,
                std::format("{} of the model's {} independent thermal region(s) are not tied to a prescribed "
                            "temperature or a convection face although {} are, so those regions float at an "
                            "arbitrary temperature and the solution is not unique (unknown {} is in the first "
                            "such region)",
                            out.anchoring.unanchored, out.anchoring.regions, out.anchoring.anchored, dof.value())));
        }

        std::expected<linalg::SymmetricSolution, linalg::SymmetricSolveFailure> solved =
            linalg::solveSymmetricSparse(reduced->rows(), reduced->rowStart(), reduced->innerIndices(),
                                         reduced->values(), reduced->effectiveHeat(), kernelSettings(settings));
        if (!solved.has_value()) {
            return std::unexpected(thermalFailure(solved.error(), settings));
        }
        out.kernelInvoked = true;
        out.pivotRatio = solved->pivotRatio;
        out.residual = residualOf(solved->residual);
        for (std::size_t i = 0; i < free.size(); ++i) {
            free[i] = Temperature::fromSi(solved->x[i]);
        }
    }

    // RECONSTRUCTION, through P18-DOF's own function: free rows take their solved
    // value and prescribed ones take the prescribed temperature, by the maps.
    Result<std::vector<Temperature>> full = reconstructTemperatures(equations, constraints, free);
    if (!full.has_value()) {
        return std::unexpected(
            SteadySolveFailure{.problem = SteadySolveProblem::ConstraintMismatch, .error = full.error()});
    }
    out.temperatures = std::move(*full);

    if (std::optional<SteadySolveFailure> broken = constraintsHeld(constraints, out.temperatures); broken.has_value()) {
        return std::unexpected(std::move(*broken));
    }

    std::expected<std::vector<double>, SteadySolveFailure> residual = fullResidualOf(conductance, heat, out.temperatures);
    if (!residual.has_value()) {
        return std::unexpected(std::move(residual.error()));
    }
    out.fullResidual = std::move(*residual);
    return out;
}

std::expected<SteadyResidual, SteadySolveFailure>
validateSteadySolution(const ThermalSparseMatrix& conductance, const ThermalHeatVector& heat,
                       const ThermalFreeEquationMap& equations, const ThermalConstraintMap& constraints,
                       std::span<const Temperature> temperatures, const ThermalSolverSettings& settings) {
    if (Result<void> checked = validate(settings); !checked.has_value()) {
        return std::unexpected(
            SteadySolveFailure{.problem = SteadySolveProblem::InvalidSettings, .error = checked.error()});
    }
    const std::size_t n = static_cast<std::size_t>(equations.dofCount());
    if (temperatures.size() != n) {
        return std::unexpected(failure(SteadySolveProblem::DofMapMismatch, ErrorCode::InvalidArgument,
                                       std::format("a candidate of {} temperatures cannot describe {} unknowns",
                                                   temperatures.size(), n)));
    }
    for (std::size_t i = 0; i < temperatures.size(); ++i) {
        if (!std::isfinite(temperatures[i].si())) {
            return std::unexpected(failure(SteadySolveProblem::NonFiniteSolution, ErrorCode::FailedPrecondition,
                                           std::format("the temperature of unknown {} is not finite", i)));
        }
    }
    std::expected<ReducedThermalSystem, SteadySolveFailure> reduced =
        reduceThermalSystem(conductance, heat, equations, constraints);
    if (!reduced.has_value()) {
        return std::unexpected(std::move(reduced.error()));
    }
    if (std::optional<SteadySolveFailure> broken = constraintsHeld(constraints, temperatures); broken.has_value()) {
        return std::unexpected(std::move(*broken));
    }

    std::vector<double> freeValues(reduced->rows());
    const std::span<const ThermalDofIndex> freeDofs = equations.freeDofs();
    for (std::size_t equation = 0; equation < freeValues.size(); ++equation) {
        freeValues[equation] = temperatures[static_cast<std::size_t>(freeDofs[equation].value())].si();
    }
    std::expected<linalg::ResidualNorms, linalg::SymmetricSolveFailure> norms =
        linalg::evaluateResidual(reduced->rows(), reduced->rowStart(), reduced->innerIndices(), reduced->values(),
                                 reduced->effectiveHeat(), freeValues, kernelSettings(settings));
    if (!norms.has_value()) {
        return std::unexpected(thermalFailure(norms.error(), settings));
    }
    return residualOf(*norms);
}

// ---------------------------------------------------------------------------
// Provenance
// ---------------------------------------------------------------------------

namespace {

[[nodiscard]] SolutionStaleReason fromSystemReason(SystemStaleReason reason) noexcept {
    switch (reason) {
    case SystemStaleReason::Body:
        return SolutionStaleReason::Body;
    case SystemStaleReason::Mesh:
        return SolutionStaleReason::Mesh;
    case SystemStaleReason::Geometry:
        return SolutionStaleReason::Geometry;
    case SystemStaleReason::Mode:
        return SolutionStaleReason::Mode;
    case SystemStaleReason::Material:
        return SolutionStaleReason::Material;
    case SystemStaleReason::Loads:
        return SolutionStaleReason::Loads;
    case SystemStaleReason::Convections:
        return SolutionStaleReason::Convections;
    }
    return SolutionStaleReason::Mesh;
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

[[nodiscard]] std::string reasonsText(const std::vector<SolutionStaleReason>& reasons) {
    std::string names;
    for (const SolutionStaleReason reason : reasons) {
        if (!names.empty()) {
            names += ", ";
        }
        names += toString(reason);
    }
    return names;
}

} // namespace

std::vector<SolutionStaleReason> staleReasons(const SteadySolutionSource& solved, const SteadySolutionSource& current) {
    std::vector<SolutionStaleReason> reasons;
    for (const SystemStaleReason reason : staleReasons(solved.system, current.system)) {
        reasons.push_back(fromSystemReason(reason));
    }
    if (!(solved.temperatures == current.temperatures)) {
        reasons.push_back(SolutionStaleReason::PrescribedTemperatures);
    }
    if (!(solved.settings == current.settings)) {
        reasons.push_back(SolutionStaleReason::Settings);
    }
    return reasons;
}

Result<SteadySolutionSource> currentSteadySolutionSource(const Document& document, const ThermalModel& model,
                                                         std::span<const ThermalLoad> loads,
                                                         std::span<const ThermalBoundaryCondition> conditions,
                                                         const ThermalSolverSettings& settings) {
    Result<ThermalSystemSource> system =
        currentThermalSystemSource(document, model, ThermalAnalysisMode::Steady, loads, conditions);
    if (!system.has_value()) {
        return std::unexpected(system.error());
    }
    return SteadySolutionSource{.system = std::move(*system),
                                .temperatures = prescribedConditions(conditions),
                                .settings = settings};
}

bool isCurrent(const SteadyThermalSolution& solution, const Document& document, const ThermalModel& model,
               std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
               const ThermalSolverSettings& settings) {
    return requireCurrent(solution, document, model, loads, conditions, settings).has_value();
}

Result<void> requireCurrent(const SteadyThermalSolution& solution, const Document& document, const ThermalModel& model,
                            std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
                            const ThermalSolverSettings& settings) {
    const Result<SteadySolutionSource> current =
        currentSteadySolutionSource(document, model, loads, conditions, settings);
    if (!current.has_value()) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("the solution cannot be compared with the model: {}", current.error().message));
    }
    const std::vector<SolutionStaleReason> reasons = staleReasons(solution.source(), *current);
    if (reasons.empty()) {
        return Result<void>{};
    }
    return makeError(ErrorCode::FailedPrecondition,
                     std::format("the solved temperature field no longer describes the model: {} changed. Solve "
                                 "it again",
                                 reasonsText(reasons)));
}

// ---------------------------------------------------------------------------
// SteadyThermalSolution and the solves
// ---------------------------------------------------------------------------

Result<Temperature> SteadyThermalSolution::temperatureOf(ThermalDofIndex dof) const {
    if (dof.value() >= solution_.temperatures.size()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("unknown {} is outside this solution's {} unknowns", dof.value(), dofCount()));
    }
    return solution_.temperatures[static_cast<std::size_t>(dof.value())];
}

Result<Temperature> SteadyThermalSolution::temperatureOf(const ThermalDofMap& map, meshing::NodeId node) const {
    if (!describes(map)) {
        return makeError(ErrorCode::FailedPrecondition,
                         "this solution belongs to another numbering, so a node cannot be looked up through it");
    }
    const Result<ThermalDofIndex> dof = map.dofOf(node);
    if (!dof.has_value()) {
        return std::unexpected(dof.error());
    }
    return temperatureOf(*dof);
}

std::expected<SteadyThermalSolution, SteadySolveFailure>
solveSteadyThermalSystem(const ThermalSystem& system, const ThermalDofMap& map, const ThermalConstraintMap& constraints,
                         const ThermalSolverSettings& settings,
                         std::vector<ThermalBoundaryCondition> temperatureConditions) {
    if (Result<void> checked = validate(settings); !checked.has_value()) {
        return std::unexpected(
            SteadySolveFailure{.problem = SteadySolveProblem::InvalidSettings, .error = checked.error()});
    }
    if (system.source().mode != ThermalAnalysisMode::Steady) {
        return std::unexpected(failure(SteadySolveProblem::NotSteadySystem, ErrorCode::FailedPrecondition,
                                       "the assembled system was built for a transient analysis; a steady solve "
                                       "needs a system assembled in steady mode"));
    }
    if (!system.describes(map)) {
        return std::unexpected(failure(SteadySolveProblem::DofMapMismatch, ErrorCode::FailedPrecondition,
                                       "the thermal numbering is not the one the system was assembled against, so "
                                       "its unknowns are not this system's rows"));
    }
    Result<ThermalFreeEquationMap> equations = buildThermalFreeEquationMap(map, constraints);
    if (!equations.has_value()) {
        return std::unexpected(
            SteadySolveFailure{.problem = SteadySolveProblem::ConstraintMismatch, .error = equations.error()});
    }

    std::expected<ThermalAlgebraicSolution, SteadySolveFailure> solved =
        solveThermalAlgebra(system.conductance(), system.heat(), *equations, constraints, settings);
    if (!solved.has_value()) {
        return std::unexpected(std::move(solved.error()));
    }
    // Published only here, through the one private constructor: a refused solve
    // cannot leave a partial field behind.
    return SteadyThermalSolution{SteadySolutionSource{.system = system.source(),
                                                      .temperatures = std::move(temperatureConditions),
                                                      .settings = settings},
                                 std::move(*solved)};
}

namespace {

[[nodiscard]] std::expected<SteadyThermalSolution, SteadySolveFailure>
runSteady(const Document& document, const ThermalModel& model, const ThermalDofMap& map, const ThermalSystem& system,
          const PreparedThermalBoundary& preparedBoundary, std::span<const ThermalLoad> loads,
          std::span<const ThermalBoundaryCondition> conditions, const ThermalSolverSettings& settings) {
    if (Result<void> checked = validate(settings); !checked.has_value()) {
        return std::unexpected(
            SteadySolveFailure{.problem = SteadySolveProblem::InvalidSettings, .error = checked.error()});
    }

    // ALL OF THESE BEFORE ANY ARITHMETIC, so a stale input is reported as one and
    // never as whatever numerical symptom it would have caused.
    if (Result<void> current =
            requireCurrent(system, document, model, ThermalAnalysisMode::Steady, loads, conditions);
        !current.has_value()) {
        return std::unexpected(
            SteadySolveFailure{.problem = SteadySolveProblem::StaleSystem, .error = current.error()});
    }
    if (!preparedBoundary.describes(map)) {
        return std::unexpected(failure(SteadySolveProblem::DofMapMismatch, ErrorCode::FailedPrecondition,
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
        return std::unexpected(failure(SteadySolveProblem::StaleBoundary, ErrorCode::FailedPrecondition,
                                       std::format("the prepared boundary no longer describes the model or the "
                                                   "conditions: {} changed",
                                                   names)));
    }
    return solveSteadyThermalSystem(system, map, preparedBoundary.constraints(), settings,
                                    prescribedConditions(conditions));
}

} // namespace

Result<SteadyThermalSolution>
solveSteadyThermal(const Document& document, const ThermalModel& model, const ThermalDofMap& map,
                   const ThermalSystem& system, const PreparedThermalBoundary& preparedBoundary,
                   std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
                   const ThermalSolverSettings& settings) {
    std::expected<SteadyThermalSolution, SteadySolveFailure> solved =
        runSteady(document, model, map, system, preparedBoundary, loads, conditions, settings);
    if (!solved.has_value()) {
        return std::unexpected(std::move(solved.error().error));
    }
    return std::move(*solved);
}

std::optional<SteadySolveFailure>
steadySolveFailure(const Document& document, const ThermalModel& model, const ThermalDofMap& map,
                   const ThermalSystem& system, const PreparedThermalBoundary& preparedBoundary,
                   std::span<const ThermalLoad> loads, std::span<const ThermalBoundaryCondition> conditions,
                   const ThermalSolverSettings& settings) {
    std::expected<SteadyThermalSolution, SteadySolveFailure> solved =
        runSteady(document, model, map, system, preparedBoundary, loads, conditions, settings);
    if (solved.has_value()) {
        return std::nullopt;
    }
    return std::move(solved.error());
}

} // namespace bettercad::thermal
