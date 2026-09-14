#include <bettercad/core/units/Format.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/sketch/Solver.hpp>

#include "sketch/solver/SolverSystem.hpp"

#include <Eigen/Dense>

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
#include <set>
#include <vector>

namespace bettercad::sketch {

namespace {

using detail::System;
using Eigen::Index;
using Eigen::MatrixXd;
using Eigen::VectorXd;

// A Jacobian row is linearly dependent on earlier rows when less than this
// fraction of its norm is left after projecting it onto them.
constexpr double kRankTolerance = 1e-9;
// Relative threshold for the minimum-norm Gauss-Newton step.
constexpr double kStepRankThreshold = 1e-12;
// A stationary point: the gradient J^T F is negligible relative to |J| |F|.
constexpr double kStationaryRatio = 1e-8;

double halfSquaredNorm(const System& system, const VectorXd& x) {
    VectorXd residuals;
    system.evaluate(x, residuals, nullptr);
    return 0.5 * residuals.squaredNorm();
}

// Backtracking (Armijo) line search along @p step.
std::optional<VectorXd> lineSearch(const System& system, const VectorXd& x, const VectorXd& step,
                                   double f0, double slope) {
    double alpha = 1.0;
    for (int i = 0; i < 40; ++i) {
        VectorXd candidate = x + alpha * step;
        if (halfSquaredNorm(system, candidate) <= f0 + 1e-4 * alpha * slope) {
            return candidate;
        }
        alpha *= 0.5;
    }
    return std::nullopt;
}

// Levenberg-Marquardt steps with increasing damping, used when the
// Gauss-Newton direction does not reduce the residual.
std::optional<VectorXd> dampedStep(const System& system, const VectorXd& x, const MatrixXd& jacobian,
                                   const VectorXd& gradient, double f0) {
    const MatrixXd normal = jacobian.transpose() * jacobian;
    // Largest diagonal entry, computed explicitly: Eigen's maxCoeff() is
    // undefined for an empty matrix.
    double scale = 1.0;
    for (Index i = 0; i < normal.rows(); ++i) {
        scale = std::max(scale, normal(i, i));
    }
    const MatrixXd identity = MatrixXd::Identity(normal.rows(), normal.cols());
    for (double lambda = 1e-6 * scale; lambda <= 1e6 * scale; lambda *= 100.0) {
        const VectorXd step = (normal + lambda * identity).ldlt().solve(-gradient);
        VectorXd candidate = x + step;
        if (halfSquaredNorm(system, candidate) < f0) {
            return candidate;
        }
    }
    return std::nullopt;
}

struct RankAnalysis {
    Index rank = 0;
    std::vector<Index> dependentRows;
};

// Sequential Gram-Schmidt over the rows in order: a row that adds nothing to
// the span of the rows before it is dependent (redundant).
RankAnalysis analyzeRows(const MatrixXd& jacobian) {
    RankAnalysis analysis;
    std::vector<VectorXd> basis;
    for (Index row = 0; row < jacobian.rows(); ++row) {
        VectorXd v = jacobian.row(row).transpose();
        const double norm = v.norm();
        for (int pass = 0; pass < 2; ++pass) { // re-orthogonalize for stability
            for (const VectorXd& q : basis) {
                v -= q.dot(v) * q;
            }
        }
        const double remaining = v.norm();
        if (norm == 0.0 || remaining <= kRankTolerance * norm) {
            analysis.dependentRows.push_back(row);
        } else {
            basis.push_back(v / remaining);
        }
    }
    analysis.rank = static_cast<Index>(basis.size());
    return analysis;
}

std::vector<ConstraintId> uniqueSources(const System& system, const std::vector<Index>& rows) {
    std::vector<ConstraintId> ids;
    std::set<ConstraintId> seen;
    for (const Index row : rows) {
        const ConstraintId source = system.equations()[static_cast<std::size_t>(row)].source;
        if (source.isValid() && seen.insert(source).second) {
            ids.push_back(source);
        }
    }
    return ids;
}

SolveResult run(const Sketch& sketch, const SolverOptions& options, Sketch* target) {
    SolveResult result;
    if (!(options.tolerance.si() > 0.0) || options.maxIterations < 0) {
        result.message = "invalid solver options";
        return result;
    }
    auto built = System::build(sketch);
    if (!built) {
        result.message = built.error().message;
        return result;
    }
    const System& system = *built;
    const Index n = system.unknowns();
    const Index m = system.equationCount();
    result.unknowns = static_cast<std::size_t>(n);
    result.equations = static_cast<std::size_t>(m);

    const double tolerance = options.tolerance.si();
    const double convergence = tolerance * 1e-3; // iterate well below the tolerance
    VectorXd x = system.initial();
    VectorXd residuals;
    MatrixXd jacobian;
    bool stalled = false;
    int iteration = 0;

    for (; iteration < options.maxIterations; ++iteration) {
        system.evaluate(x, residuals, &jacobian);
        if (m == 0 || residuals.lpNorm<Eigen::Infinity>() <= convergence) {
            break;
        }
        if (n == 0) {
            stalled = true; // nothing can move
            break;
        }
        const VectorXd gradient = jacobian.transpose() * residuals;
        const double f0 = 0.5 * residuals.squaredNorm();

        // Minimum-norm Gauss-Newton step: under-constrained geometry moves least.
        Eigen::CompleteOrthogonalDecomposition<MatrixXd> decomposition(jacobian);
        decomposition.setThreshold(kStepRankThreshold);
        const VectorXd step = decomposition.solve(-residuals);

        std::optional<VectorXd> next = lineSearch(system, x, step, f0, gradient.dot(step));
        if (!next) {
            next = dampedStep(system, x, jacobian, gradient, f0);
        }
        if (!next || (*next - x).lpNorm<Eigen::Infinity>() <= 1e-15 * (1.0 + x.lpNorm<Eigen::Infinity>())) {
            stalled = true;
            break;
        }
        x = *next;
    }

    system.evaluate(x, residuals, &jacobian);
    result.iterations = iteration;
    const double maxResidual = m == 0 ? 0.0 : residuals.lpNorm<Eigen::Infinity>();
    result.maxResidual = Length::fromSi(maxResidual);
    const RankAnalysis ranks = analyzeRows(jacobian);
    result.degreesOfFreedom = static_cast<std::size_t>(n - ranks.rank);

    if (maxResidual > tolerance) {
        const double gradientNorm = (jacobian.transpose() * residuals).norm();
        const double scale = jacobian.norm() * residuals.norm();
        const bool stationary = scale == 0.0 || gradientNorm <= kStationaryRatio * scale;
        if (stalled && stationary) {
            std::vector<Index> unsatisfied;
            for (Index row = 0; row < m; ++row) {
                if (std::abs(residuals[row]) > tolerance) {
                    unsatisfied.push_back(row);
                }
            }
            result.status = SolveStatus::Inconsistent;
            result.conflicting = uniqueSources(system, unsatisfied);
            result.message = std::format(
                "the constraints cannot all be satisfied (largest residual {} at the best compromise)",
                toString(result.maxResidual, units::mm));
        } else {
            result.status = SolveStatus::SolverFailure;
            result.message = std::format("no solution after {} iterations (largest residual {})",
                                         iteration, toString(result.maxResidual, units::mm));
        }
        return result;
    }

    result.redundant = uniqueSources(system, ranks.dependentRows);
    if (!result.redundant.empty()) {
        result.status = SolveStatus::OverConstrained;
        result.message = std::format("{} redundant constraint(s); the first is {}",
                                     result.redundant.size(), result.redundant.front());
        return result;
    }
    if (auto valid = system.checkNonDegenerate(x, tolerance); !valid) {
        result.status = SolveStatus::SolverFailure;
        result.message = valid.error().message;
        return result;
    }

    result.status = result.degreesOfFreedom > 0 ? SolveStatus::UnderConstrained
                                                : SolveStatus::FullyConstrained;
    result.message = std::format("solved in {} iteration(s), {} degree(s) of freedom left",
                                 iteration, result.degreesOfFreedom);
    if (target != nullptr) {
        auto applied = system.apply(x, *target);
        if (!applied) {
            result.status = SolveStatus::SolverFailure;
            result.message = applied.error().message;
            return result;
        }
        result.geometryChanged = *applied;
    }
    return result;
}

} // namespace

std::string_view toString(SolveStatus status) noexcept {
    switch (status) {
    case SolveStatus::UnderConstrained:
        return "UNDER_CONSTRAINED";
    case SolveStatus::FullyConstrained:
        return "FULLY_CONSTRAINED";
    case SolveStatus::OverConstrained:
        return "OVER_CONSTRAINED";
    case SolveStatus::Inconsistent:
        return "INCONSISTENT";
    case SolveStatus::SolverFailure:
        return "SOLVER_FAILURE";
    }
    return "UNKNOWN";
}

SolveResult solve(Sketch& sketch, const SolverOptions& options) {
    return run(sketch, options, &sketch);
}

SolveResult analyze(const Sketch& sketch, const SolverOptions& options) {
    return run(sketch, options, nullptr);
}

} // namespace bettercad::sketch
