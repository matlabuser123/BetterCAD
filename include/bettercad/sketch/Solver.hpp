#pragma once

#include <bettercad/core/Id.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/sketch/Export.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// Geometric constraint solver for sketches.
//
// Unknowns x are the coordinates of point entities that are not Fixed and the
// radii of circles. Enabled constraints contribute equations F(x) = 0; every
// arc also contributes |end - centre| = |start - centre|. All residuals are
// lengths (metres), so the Jacobian is well scaled. The system is solved by a
// Gauss-Newton method with minimum-norm steps, so under-constrained geometry
// moves as little as possible.
namespace bettercad::sketch {

class Sketch;

enum class SolveStatus {
    UnderConstrained, ///< Solved; the geometry can still move (degreesOfFreedom > 0).
    FullyConstrained, ///< Solved; no freedom remains.
    OverConstrained,  ///< Satisfiable, but some constraints are redundant.
    Inconsistent,     ///< The constraints cannot all be satisfied together.
    SolverFailure,    ///< No solution was reached and none was proven impossible.
};

/// "UNDER_CONSTRAINED", "FULLY_CONSTRAINED", "OVER_CONSTRAINED",
/// "INCONSISTENT" or "SOLVER_FAILURE".
[[nodiscard]] BETTERCAD_SKETCH_EXPORT std::string_view toString(SolveStatus status) noexcept;

struct SolverOptions {
    /// A constraint is satisfied when all of its residuals are within this.
    Length tolerance = Length::fromSi(1e-10);
    int maxIterations = 100;
};

struct SolveResult {
    SolveStatus status = SolveStatus::SolverFailure;
    std::size_t unknowns = 0;
    std::size_t equations = 0;
    /// unknowns - rank of the constraint Jacobian at the solution.
    std::size_t degreesOfFreedom = 0;
    int iterations = 0;
    /// Largest residual of any equation after solving.
    Length maxResidual{};
    /// Inconsistent: constraints that are not satisfied at the best
    /// (least-squares) compromise.
    std::vector<ConstraintId> conflicting{};
    /// OverConstrained: constraints implied by constraints with lower IDs.
    std::vector<ConstraintId> redundant{};
    /// True if solve() wrote new geometry into the sketch.
    bool geometryChanged = false;
    std::string message{};

    [[nodiscard]] bool solved() const noexcept {
        return status == SolveStatus::UnderConstrained || status == SolveStatus::FullyConstrained;
    }
};

/// Solves the sketch's enabled constraints. The sketch geometry is updated
/// only if the result is UnderConstrained or FullyConstrained; otherwise the
/// sketch is left unchanged, so conflicts never produce arbitrary geometry.
[[nodiscard]] BETTERCAD_SKETCH_EXPORT SolveResult solve(Sketch& sketch,
                                                       const SolverOptions& options = {});

/// Same diagnosis as solve() without modifying the sketch.
[[nodiscard]] BETTERCAD_SKETCH_EXPORT SolveResult analyze(const Sketch& sketch,
                                                         const SolverOptions& options = {});

} // namespace bettercad::sketch
