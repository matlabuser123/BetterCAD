#pragma once

// Shared fixtures for the P18-TRANSIENT-001 tests.
//
// THE ORACLES ARE INDEPENDENT BY CONSTRUCTION, as in the steady solve's. The
// expected states of the small systems are typed in from a hand derivation or come
// from closed forms; the comparison integrator (`denseBackwardEuler`) is a dense
// Gaussian elimination written HERE on `C / dt + K`, and it calls no production
// reduction, no production effective-matrix builder, no `linalg` function and no
// step acceptance. The production path is the thing under test.

#include "ThermalSolveTestSupport.hpp"

#include <bettercad/thermal/ThermalTransient.hpp>

#include <cmath>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace trtest {

using namespace sotest;
using thermal::ThermalTransientAlgebra;
using thermal::TimeGrid;
using thermal::TransientFailure;
using thermal::TransientOutputPolicy;
using thermal::TransientProblem;
using thermal::TransientSettings;
using thermal::TransientStaleReason;
using thermal::TransientThermalSolution;
using thermal::UniformInitialTemperature;

/// A transient system given directly as dense `K`, `C`, a heat vector and the
/// prescribed unknowns, with the numbering of a hand mesh of `n` nodes.
struct TrAlgebra {
    Algebra base;
    thermal::ConductanceMatrix k;
    thermal::CapacityMatrix c;
    Dense2 denseC;
};

[[nodiscard]] inline TrAlgebra makeTr(const Dense2& dk, const Dense2& dc, const std::vector<double>& q,
                                      const std::vector<std::pair<std::size_t, double>>& prescribed = {}) {
    Algebra base = makeAlgebra(dk, q, prescribed);
    thermal::ConductanceMatrix k{base.k};
    return TrAlgebra{std::move(base), std::move(k), thermal::CapacityMatrix{sparseOf(dc)}, dc};
}

[[nodiscard]] inline std::vector<Temperature> stateOf(const std::vector<double>& kelvin) {
    std::vector<Temperature> out;
    for (const double value : kelvin) {
        out.push_back(Temperature::fromSi(value));
    }
    return out;
}

[[nodiscard]] inline std::vector<Temperature> uniformState(std::size_t n, double kelvin) {
    return std::vector<Temperature>(n, Temperature::fromSi(kelvin));
}

[[nodiscard]] inline TimeGrid gridOf(double dt, double end) {
    auto grid = TimeGrid::create(dt, end);
    INFO((grid.has_value() ? std::string{} : grid.error().error.message));
    REQUIRE(grid.has_value());
    return *grid;
}

[[nodiscard]] inline std::expected<ThermalTransientAlgebra, TransientFailure>
runTr(const TrAlgebra& a, std::span<const Temperature> initial, double dt, double end,
      TransientOutputPolicy output = TransientOutputPolicy::EveryStep, const ThermalSolverSettings& settings = {}) {
    return thermal::solveThermalTransientAlgebra(a.k, a.c, a.base.q, a.base.equations, a.base.constraints, initial,
                                                 gridOf(dt, end), output, settings);
}

/// Requires success and returns the run.
[[nodiscard]] inline ThermalTransientAlgebra mustRunTr(const TrAlgebra& a, std::span<const Temperature> initial, double dt,
                                                       double end,
                                                       TransientOutputPolicy output = TransientOutputPolicy::EveryStep,
                                                       const ThermalSolverSettings& settings = {}) {
    auto run = runTr(a, initial, dt, end, output, settings);
    INFO((run.has_value() ? std::string{} : run.error().error.message));
    REQUIRE(run.has_value());
    return std::move(*run);
}

template <typename T>
[[nodiscard]] std::optional<TransientProblem> trProblemOf(const std::expected<T, TransientFailure>& result) {
    if (result.has_value()) {
        return std::nullopt;
    }
    return result.error().problem;
}

/// Sample @p index of a run as plain doubles.
[[nodiscard]] inline std::vector<double> sampleOf(const ThermalTransientAlgebra& run, std::size_t index) {
    std::vector<double> out;
    for (std::size_t i = 0; i < run.dofCount; ++i) {
        out.push_back(run.samples[index * run.dofCount + i].si());
    }
    return out;
}

/// ONE backward-Euler step on dense arrays, with the prescribed unknowns held:
/// `(C/dt + K) T1 = Q + (C/dt) T0`, partitioned and eliminated by the test's own
/// Gaussian elimination. THE TEST'S OWN ORACLE: nothing production is called.
[[nodiscard]] inline std::vector<double>
denseBackwardEuler(const Dense2& dk, const Dense2& dc, const std::vector<double>& q, const std::vector<double>& t0, double dt,
                   const std::vector<std::pair<std::size_t, double>>& prescribed) {
    const std::size_t n = dk.size();
    Dense2 a(n, std::vector<double>(n, 0.0));
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            a[i][j] = dc[i][j] / dt + dk[i][j];
        }
    }
    std::vector<double> b = multiplyDense(dc, t0);
    for (std::size_t i = 0; i < n; ++i) {
        b[i] = q[i] + b[i] / dt;
    }
    return denseExpected(a, b, prescribed);
}

/// The whole history by repeated `denseBackwardEuler` with steps @p steps.
[[nodiscard]] inline std::vector<std::vector<double>>
denseHistory(const Dense2& dk, const Dense2& dc, const std::vector<double>& q, std::vector<double> t0,
             const std::vector<double>& steps, const std::vector<std::pair<std::size_t, double>>& prescribed) {
    for (const auto& [dof, kelvin] : prescribed) {
        t0[dof] = kelvin;
    }
    std::vector<std::vector<double>> out{t0};
    for (const double dt : steps) {
        out.push_back(denseBackwardEuler(dk, dc, q, out.back(), dt, prescribed));
    }
    return out;
}

[[nodiscard]] inline std::vector<double> stepSizesOf(const TimeGrid& grid) {
    std::vector<double> out;
    for (std::size_t i = 0; i < grid.stepCount(); ++i) {
        out.push_back(grid.stepAt(i));
    }
    return out;
}

[[nodiscard]] inline double worstDifference(const std::vector<double>& a, const std::vector<double>& b) {
    REQUIRE(a.size() == b.size());
    double worst = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        worst = std::max(worst, std::abs(a[i] - b[i]));
    }
    return worst;
}

/// `1^T C T`: the heat stored, J, relative to 0 K.
[[nodiscard]] inline double storedEnergy(const Dense2& dc, const std::vector<double>& t) {
    double sum = 0.0;
    const std::vector<double> ct = multiplyDense(dc, t);
    for (const double value : ct) {
        sum += value;
    }
    return sum;
}

/// The 1D conduction matrix of a chain with conductances `g[i]` between neighbours
/// (`g.size() + 1` unknowns), and the matching CONSISTENT capacity: each segment
/// contributes `cell/6 * [2 1; 1 2]`.
[[nodiscard]] inline Dense2 chainConductance(const std::vector<double>& g) {
    const std::size_t n = g.size() + 1;
    Dense2 k(n, std::vector<double>(n, 0.0));
    for (std::size_t i = 0; i + 1 < n; ++i) {
        k[i][i] += g[i];
        k[i + 1][i + 1] += g[i];
        k[i][i + 1] -= g[i];
        k[i + 1][i] -= g[i];
    }
    return k;
}

[[nodiscard]] inline Dense2 chainCapacity(std::size_t n, double cell) {
    Dense2 c(n, std::vector<double>(n, 0.0));
    for (std::size_t i = 0; i + 1 < n; ++i) {
        c[i][i] += 2.0 * cell / 6.0;
        c[i + 1][i + 1] += 2.0 * cell / 6.0;
        c[i][i + 1] += cell / 6.0;
        c[i + 1][i] += cell / 6.0;
    }
    return c;
}

} // namespace trtest
