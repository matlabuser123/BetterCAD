#pragma once

// Closed forms and an independent dense solver for the thermal reference suite (P18-REFMOD-001).
//
// THIS FILE INCLUDES NO BETTERCAD HEADER AND NAMES NO BETTERCAD TYPE, and that is the whole point of it: a reference model
// whose expected value came from the code under test would compare the code with itself. Everything here is the standard
// library and arithmetic written out, so it CANNOT call a production formula, and `ThermalOracleIndependence` (an
// architecture test) fails if a BetterCAD include or a `thermal::` name ever appears in it.
//
// SI THROUGHOUT: metres, kelvin, watts, seconds, W/(m K), W/m^2, W/m^3, W/(m^2 K), kg/m^3, J/(kg K).
//
// WHAT IS HERE
//   * the affine field and its gradient and flux                                  (RM-THERM-01)
//   * a tetrahedron's shape gradients, volume and stiffness from a 4 x 4 inverse   (RM-THERM-01)
//   * the slab, the composite wall, the convecting slab                            (RM-THERM-02, -03, -05)
//   * the generating slab, the generating tube, the generating cube (Fourier)      (RM-THERM-04, -07)
//   * the lumped body and the insulated body under generation                      (RM-THERM-08)
//   * a dense conduction solver for a tetrahedral mesh given as plain arrays       (the independent solve)
//
// WHAT IT IS NOT: a second implementation of P18. It is small, transparent and slow on purpose, and it solves what a
// textbook solves.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>
#include <utility>
#include <vector>

namespace bettercad::test::analytic::heat {

inline constexpr double pi = std::numbers::pi;

using Vec3 = std::array<double, 3>;
using Mat3 = std::array<Vec3, 3>;

[[nodiscard]] inline double dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
[[nodiscard]] inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
[[nodiscard]] inline Vec3 minus(const Vec3& a, const Vec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
[[nodiscard]] inline Vec3 plus(const Vec3& a, const Vec3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
[[nodiscard]] inline Vec3 scaled(const Vec3& a, double s) { return {a[0] * s, a[1] * s, a[2] * s}; }
[[nodiscard]] inline double norm(const Vec3& a) { return std::sqrt(dot(a, a)); }

/// `R v` for a matrix given by ROWS.
[[nodiscard]] inline Vec3 apply(const Mat3& r, const Vec3& v) { return {dot(r[0], v), dot(r[1], v), dot(r[2], v)}; }

/// The matrix whose COLUMNS are @p x, @p y, @p z -- the rotation that carries the local axes to them.
[[nodiscard]] inline Mat3 fromColumns(const Vec3& x, const Vec3& y, const Vec3& z) {
    return {{{x[0], y[0], z[0]}, {x[1], y[1], z[1]}, {x[2], y[2], z[2]}}};
}
[[nodiscard]] inline double determinant(const Mat3& m) { return dot(m[0], cross(m[1], m[2])); }
/// `max |(R^T R - I)_ij|`: how far @p r is from orthogonal.
[[nodiscard]] inline double orthogonalityError(const Mat3& r) {
    double worst = 0.0;
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            double sum = 0.0;
            for (std::size_t k = 0; k < 3; ++k) {
                sum += r[k][i] * r[k][j];
            }
            worst = std::max(worst, std::abs(sum - (i == j ? 1.0 : 0.0)));
        }
    }
    return worst;
}

// ---------------------------------------------------------------------------
// Dense linear algebra, small and plain
// ---------------------------------------------------------------------------

using Dense = std::vector<std::vector<double>>;

/// Solves @p a x = @p b by Gaussian elimination with partial pivoting. @p a must be square and non-singular.
[[nodiscard]] inline std::vector<double> solveDense(Dense a, std::vector<double> b) {
    const std::size_t n = b.size();
    for (std::size_t col = 0; col < n; ++col) {
        std::size_t pivot = col;
        for (std::size_t r = col + 1; r < n; ++r) {
            if (std::abs(a[r][col]) > std::abs(a[pivot][col])) {
                pivot = r;
            }
        }
        std::swap(a[col], a[pivot]);
        std::swap(b[col], b[pivot]);
        for (std::size_t r = col + 1; r < n; ++r) {
            const double factor = a[r][col] / a[col][col];
            if (factor == 0.0) {
                continue;
            }
            for (std::size_t c = col; c < n; ++c) {
                a[r][c] -= factor * a[col][c];
            }
            b[r] -= factor * b[col];
        }
    }
    std::vector<double> x(n, 0.0);
    for (std::size_t r = n; r-- > 0;) {
        double s = b[r];
        for (std::size_t c = r + 1; c < n; ++c) {
            s -= a[r][c] * x[c];
        }
        x[r] = s / a[r][r];
    }
    return x;
}

// ---------------------------------------------------------------------------
// RM-THERM-01: the affine field and the tetrahedron
// ---------------------------------------------------------------------------

/// T(x, y, z) = a + b x + c y + d z.
struct Affine {
    double a = 0.0;
    double b = 0.0;
    double c = 0.0;
    double d = 0.0;

    [[nodiscard]] double at(const Vec3& p) const { return a + b * p[0] + c * p[1] + d * p[2]; }
    /// grad T = [b, c, d], K/m.
    [[nodiscard]] Vec3 gradient() const { return {b, c, d}; }
    /// q = -k grad T, W/m^2: the Fourier law, written out.
    [[nodiscard]] Vec3 flux(double conductivity) const { return {-conductivity * b, -conductivity * c, -conductivity * d}; }
};

/// A tetrahedron by its four corners.
struct Tet {
    std::array<Vec3, 4> p{};

    /// +1/6 (p2-p1) . ((p3-p1) x (p4-p1)): positive for a positively oriented element, m^3.
    [[nodiscard]] double signedVolume() const {
        return dot(minus(p[1], p[0]), cross(minus(p[2], p[0]), minus(p[3], p[0]))) / 6.0;
    }

    /// The gradient of each of the four linear shape functions, from the INVERSE OF [1 x y z]: shape function j is the
    /// solution of M c = e_j, and its gradient is (c1, c2, c3). Nothing is taken from a production Jacobian.
    [[nodiscard]] std::array<Vec3, 4> shapeGradients() const {
        Dense m(4, std::vector<double>(4));
        for (std::size_t r = 0; r < 4; ++r) {
            m[r] = {1.0, p[r][0], p[r][1], p[r][2]};
        }
        std::array<Vec3, 4> g{};
        for (std::size_t j = 0; j < 4; ++j) {
            std::vector<double> rhs(4, 0.0);
            rhs[j] = 1.0;
            const std::vector<double> c = solveDense(m, rhs);
            g[j] = {c[1], c[2], c[3]};
        }
        return g;
    }

    /// Ke_ij = k V (grad N_i . grad N_j), W/K.
    [[nodiscard]] std::array<std::array<double, 4>, 4> stiffness(double conductivity) const {
        const std::array<Vec3, 4> g = shapeGradients();
        const double v = std::abs(signedVolume());
        std::array<std::array<double, 4>, 4> ke{};
        for (std::size_t i = 0; i < 4; ++i) {
            for (std::size_t j = 0; j < 4; ++j) {
                ke[i][j] = conductivity * v * dot(g[i], g[j]);
            }
        }
        return ke;
    }

    /// The gradient of the linear interpolant of the four nodal values @p t.
    [[nodiscard]] Vec3 gradientOf(const std::array<double, 4>& t) const {
        const std::array<Vec3, 4> g = shapeGradients();
        Vec3 out{};
        for (std::size_t j = 0; j < 4; ++j) {
            out = plus(out, scaled(g[j], t[j]));
        }
        return out;
    }

    /// The barycentric coordinates of @p x (they sum to 1 and are all positive inside).
    [[nodiscard]] std::array<double, 4> barycentric(const Vec3& x) const {
        Dense m(4, std::vector<double>(4));
        for (std::size_t c = 0; c < 4; ++c) {
            m[0][c] = 1.0;
            m[1][c] = p[c][0];
            m[2][c] = p[c][1];
            m[3][c] = p[c][2];
        }
        const std::vector<double> w = solveDense(m, {1.0, x[0], x[1], x[2]});
        return {w[0], w[1], w[2], w[3]};
    }
};

// ---------------------------------------------------------------------------
// RM-THERM-02: the slab
// ---------------------------------------------------------------------------

/// Steady 1D conduction through a slab of thickness L and cross-section A between two held faces (z = 0 hot, z = L cold).
struct Slab {
    double conductivity = 0.0; ///< W/(m K)
    double area = 0.0;         ///< m^2
    double length = 0.0;       ///< m
    double hot = 0.0;          ///< K, at z = 0
    double cold = 0.0;         ///< K, at z = L

    /// Qdot = k A (Th - Tc) / L, W, flowing from the hot face to the cold face (+z).
    [[nodiscard]] double heatRate() const { return conductivity * area * (hot - cold) / length; }
    /// |q| = k (Th - Tc) / L, W/m^2; the z component of q is +flux.
    [[nodiscard]] double flux() const { return conductivity * (hot - cold) / length; }
    /// dT/dz = -(Th - Tc) / L, K/m.
    [[nodiscard]] double gradient() const { return -(hot - cold) / length; }
    [[nodiscard]] double temperature(double z) const { return hot - (hot - cold) * z / length; }
};

// ---------------------------------------------------------------------------
// RM-THERM-03: the composite wall (the closed form P18 v1 cannot be asked to reproduce)
// ---------------------------------------------------------------------------

struct Layer {
    double thickness = 0.0;    ///< m
    double conductivity = 0.0; ///< W/(m K)
};

struct CompositeWall {
    std::vector<double> resistances;            ///< R_i = L_i / (k_i A), K/W
    double totalResistance = 0.0;               ///< sum R_i
    double heatRate = 0.0;                      ///< (Th - Tc) / Rtotal, W
    std::vector<double> interfaceTemperatures;  ///< T at z = 0, L1, L1+L2, ..., Ltotal  (size n + 1)
    std::vector<double> gradients;              ///< dT/dz in each layer, K/m
    double flux = 0.0;                          ///< Qdot / A, W/m^2: the SAME in every layer
};

[[nodiscard]] inline CompositeWall compositeWall(const std::vector<Layer>& layers, double area, double hot, double cold) {
    CompositeWall wall;
    for (const Layer& layer : layers) {
        wall.resistances.push_back(layer.thickness / (layer.conductivity * area));
        wall.totalResistance += wall.resistances.back();
    }
    wall.heatRate = (hot - cold) / wall.totalResistance;
    wall.flux = wall.heatRate / area;
    double t = hot;
    wall.interfaceTemperatures.push_back(t);
    for (std::size_t i = 0; i < layers.size(); ++i) {
        t -= wall.heatRate * wall.resistances[i];
        wall.interfaceTemperatures.push_back(t);
        wall.gradients.push_back(-wall.flux / layers[i].conductivity);
    }
    return wall;
}

// ---------------------------------------------------------------------------
// RM-THERM-04: generation
// ---------------------------------------------------------------------------

/// A slab of thickness 2a, both faces held at Ts, uniform generation q''' (x measured from the midplane).
struct GenerationSlab {
    double conductivity = 0.0;
    double generation = 0.0;    ///< W/m^3
    double surface = 0.0;       ///< K
    double halfThickness = 0.0; ///< a, m

    [[nodiscard]] double temperature(double x) const {
        return surface + generation / (2.0 * conductivity) * (halfThickness * halfThickness - x * x);
    }
    [[nodiscard]] double maximum() const { return temperature(0.0); }
    /// dT/dx, K/m.
    [[nodiscard]] double gradient(double x) const { return -generation / conductivity * x; }
};

/// A tube of inner radius ri, outer radius ro and length L, the OUTER wall held at Ts, the inner wall and the ends
/// insulated, uniform generation q'''.
struct GenerationTube {
    double conductivity = 0.0;
    double generation = 0.0;
    double outerKelvin = 0.0;
    double innerRadius = 0.0;
    double outerRadius = 0.0;
    double length = 0.0;

    /// T(r) = Ts + q'''/(4k) (ro^2 - r^2) - q''' ri^2/(2k) ln(ro/r).
    [[nodiscard]] double temperature(double r) const {
        return outerKelvin + generation / (4.0 * conductivity) * (outerRadius * outerRadius - r * r) -
               generation * innerRadius * innerRadius / (2.0 * conductivity) * std::log(outerRadius / r);
    }
    /// dT/dr = q'''/(2 k r) (ri^2 - r^2), K/m.
    [[nodiscard]] double gradient(double r) const {
        return generation / (2.0 * conductivity * r) * (innerRadius * innerRadius - r * r);
    }
    /// The heat generated in the true annulus, W.
    [[nodiscard]] double generatedHeat() const {
        return generation * pi * (outerRadius * outerRadius - innerRadius * innerRadius) * length;
    }
    [[nodiscard]] double volume() const { return pi * (outerRadius * outerRadius - innerRadius * innerRadius) * length; }
};

/// A cube of side a with uniform generation q''' and ALL SIX faces held at 0 (add the held temperature afterwards):
/// the Poisson problem -k Laplacian T = q''' on [0,a]^3, solved by its triple Fourier sine series,
///
///     T = (64 q''' a^2 / (k pi^5)) sum_{m,n,p odd} sin(m pi x/a) sin(n pi y/a) sin(p pi z/a) / (m n p (m^2 + n^2 + p^2)).
///
/// @p oddLimit is the largest odd index summed. The terms of the temperature fall as 1/index^5, so 61 is converged far below
/// any tolerance the suite uses; the terms of the GRADIENT fall only as 1/index^2 (oscillating), so the gradient's truncation
/// error at the default limit of 31 is a few K/m against gradient errors of hundreds, and the suite says so where it uses it.
/// The sines are tabulated once per call, so a term is three multiplications.
[[nodiscard]] inline double cubeGenerationRise(double side, double generation, double conductivity, const Vec3& x,
                                               int oddLimit = 61) {
    std::vector<double> sx;
    std::vector<double> sy;
    std::vector<double> sz;
    for (int i = 1; i <= oddLimit; i += 2) {
        sx.push_back(std::sin(i * pi * x[0] / side));
        sy.push_back(std::sin(i * pi * x[1] / side));
        sz.push_back(std::sin(i * pi * x[2] / side));
    }
    double sum = 0.0;
    for (std::size_t a = 0; a < sx.size(); ++a) {
        const double m = 2.0 * static_cast<double>(a) + 1.0;
        for (std::size_t b = 0; b < sy.size(); ++b) {
            const double n = 2.0 * static_cast<double>(b) + 1.0;
            for (std::size_t c = 0; c < sz.size(); ++c) {
                const double p = 2.0 * static_cast<double>(c) + 1.0;
                sum += sx[a] * sy[b] * sz[c] / (m * n * p * (m * m + n * n + p * p));
            }
        }
    }
    return 64.0 * generation * side * side / (conductivity * std::pow(pi, 5)) * sum;
}

/// grad T of the same series, termwise: d/dx brings down (m pi / a) and turns sin(m pi x/a) into cos(m pi x/a). K/m.
[[nodiscard]] inline Vec3 cubeGenerationGradient(double side, double generation, double conductivity, const Vec3& x,
                                                 int oddLimit = 31) {
    std::vector<double> sx;
    std::vector<double> sy;
    std::vector<double> sz;
    std::vector<double> cx;
    std::vector<double> cy;
    std::vector<double> cz;
    for (int i = 1; i <= oddLimit; i += 2) {
        sx.push_back(std::sin(i * pi * x[0] / side));
        sy.push_back(std::sin(i * pi * x[1] / side));
        sz.push_back(std::sin(i * pi * x[2] / side));
        cx.push_back(std::cos(i * pi * x[0] / side));
        cy.push_back(std::cos(i * pi * x[1] / side));
        cz.push_back(std::cos(i * pi * x[2] / side));
    }
    Vec3 sum{0.0, 0.0, 0.0};
    for (std::size_t a = 0; a < sx.size(); ++a) {
        const double m = 2.0 * static_cast<double>(a) + 1.0;
        for (std::size_t b = 0; b < sy.size(); ++b) {
            const double n = 2.0 * static_cast<double>(b) + 1.0;
            for (std::size_t c = 0; c < sz.size(); ++c) {
                const double p = 2.0 * static_cast<double>(c) + 1.0;
                const double weight = 1.0 / (m * n * p * (m * m + n * n + p * p));
                sum[0] += m * cx[a] * sy[b] * sz[c] * weight;
                sum[1] += n * sx[a] * cy[b] * sz[c] * weight;
                sum[2] += p * sx[a] * sy[b] * cz[c] * weight;
            }
        }
    }
    const double scale = 64.0 * generation * side * side / (conductivity * std::pow(pi, 5)) * (pi / side);
    return {scale * sum[0], scale * sum[1], scale * sum[2]};
}

// ---------------------------------------------------------------------------
// RM-THERM-05: conduction in series with convection
// ---------------------------------------------------------------------------

struct ConvectionSlab {
    double conductivity = 0.0;
    double area = 0.0;
    double length = 0.0;
    double coefficient = 0.0; ///< h, W/(m^2 K)
    double hot = 0.0;         ///< held at z = 0
    double ambient = 0.0;     ///< the fluid beside z = L

    [[nodiscard]] double conductionResistance() const { return length / (conductivity * area); }
    [[nodiscard]] double convectionResistance() const { return 1.0 / (coefficient * area); }
    /// Qdot = (Th - Tinf) / (Rcond + Rconv), W, from the held face to the ambient (+z).
    [[nodiscard]] double heatRate() const { return (hot - ambient) / (conductionResistance() + convectionResistance()); }
    /// The temperature of the convecting surface: Tinf + Qdot Rconv  (= Th - Qdot Rcond).
    [[nodiscard]] double surfaceTemperature() const { return ambient + heatRate() * convectionResistance(); }
    [[nodiscard]] double surfaceTemperatureFromConduction() const { return hot - heatRate() * conductionResistance(); }
    [[nodiscard]] double flux() const { return heatRate() / area; }
    [[nodiscard]] double temperature(double z) const { return hot - (hot - surfaceTemperature()) * z / length; }
};

// ---------------------------------------------------------------------------
// RM-THERM-08: transient
// ---------------------------------------------------------------------------

/// A body cooling by convection from its whole surface, treated as lumped: C dT/dt = -h A (T - Tinf).
struct Lumped {
    double conductivity = 0.0;
    double density = 0.0;
    double specificHeat = 0.0;
    double volume = 0.0;
    double area = 0.0; ///< the exchange area: the whole surface
    double coefficient = 0.0;
    double initial = 0.0;
    double ambient = 0.0;

    [[nodiscard]] double capacity() const { return density * specificHeat * volume; } ///< rho cp V, J/K
    [[nodiscard]] double characteristicLength() const { return volume / area; }
    [[nodiscard]] double biot() const { return coefficient * characteristicLength() / conductivity; }
    /// tau = rho cp V / (h A), s.
    [[nodiscard]] double timeConstant() const { return capacity() / (coefficient * area); }
    /// The continuous solution T(t) = Tinf + (T0 - Tinf) exp(-t/tau).
    [[nodiscard]] double temperature(double t) const { return ambient + (initial - ambient) * std::exp(-t / timeConstant()); }
    /// The BACKWARD-EULER solution of the same ODE after the given steps (each its own size, so a shortened last step is
    /// honoured): T_{n+1} = (T_n + (dt/tau) Tinf) / (1 + dt/tau).
    [[nodiscard]] double backwardEuler(const std::vector<double>& steps) const {
        double t = initial;
        for (const double dt : steps) {
            const double r = dt / timeConstant();
            t = (t + r * ambient) / (1.0 + r);
        }
        return t;
    }
    /// The same after @p count equal steps of size @p dt.
    [[nodiscard]] double backwardEuler(std::size_t count, double dt) const {
        return backwardEuler(std::vector<double>(count, dt));
    }
};

/// An insulated body under uniform generation: rho cp dT/dt = q'''. Backward Euler reproduces it EXACTLY (a constant
/// source on a uniform field), so the closed form is also the discrete answer.
[[nodiscard]] inline double insulatedGenerationTemperature(double t, double initial, double generation, double density,
                                                           double specificHeat) {
    return initial + generation * t / (density * specificHeat);
}

// ---------------------------------------------------------------------------
// The independent dense conduction solver
// ---------------------------------------------------------------------------

/// A tetrahedral mesh as PLAIN ARRAYS: nothing about it is a BetterCAD type, so the solve below cannot reach a production
/// kernel. The corner order of each tetrahedron is whatever the caller gives; the orientation is not assumed.
struct HeatMesh {
    std::vector<Vec3> nodes;
    std::vector<std::array<std::size_t, 4>> tets;
};

struct HeatSolution {
    std::vector<double> temperature;
    /// For every PRESCRIBED node, the heat the constraint supplies INTO the domain: (K T - Q)_i, W. Zero for a free node.
    std::vector<double> reaction;
    double totalVolume = 0.0;
    /// The heat the source puts in, sum q''' V, W.
    double generated = 0.0;
};

/// Solves  -k Laplacian T = q'''  on @p mesh by linear Galerkin finite elements: Ke = k V G^T G from the 4 x 4 inverse,
/// the source q''' V / 4 per corner, the prescribed nodes eliminated, dense Gaussian elimination. @p prescribed[i] holds the
/// temperature node i is held at, or nothing if it is free.
[[nodiscard]] inline HeatSolution solveConduction(const HeatMesh& mesh, double conductivity, double generation,
                                                  const std::vector<std::optional<double>>& prescribed) {
    const std::size_t n = mesh.nodes.size();
    Dense k(n, std::vector<double>(n, 0.0));
    std::vector<double> q(n, 0.0);
    HeatSolution out;
    for (const std::array<std::size_t, 4>& e : mesh.tets) {
        Tet tet;
        for (std::size_t c = 0; c < 4; ++c) {
            tet.p[c] = mesh.nodes[e[c]];
        }
        const auto ke = tet.stiffness(conductivity);
        const double v = std::abs(tet.signedVolume());
        out.totalVolume += v;
        out.generated += generation * v;
        for (std::size_t a = 0; a < 4; ++a) {
            for (std::size_t b = 0; b < 4; ++b) {
                k[e[a]][e[b]] += ke[a][b];
            }
            q[e[a]] += generation * v / 4.0;
        }
    }
    std::vector<std::size_t> freeIndex;
    for (std::size_t i = 0; i < n; ++i) {
        if (!prescribed[i].has_value()) {
            freeIndex.push_back(i);
        }
    }
    const std::size_t f = freeIndex.size();
    Dense a(f, std::vector<double>(f, 0.0));
    std::vector<double> rhs(f, 0.0);
    for (std::size_t r = 0; r < f; ++r) {
        for (std::size_t c = 0; c < f; ++c) {
            a[r][c] = k[freeIndex[r]][freeIndex[c]];
        }
        double b = q[freeIndex[r]];
        for (std::size_t i = 0; i < n; ++i) {
            if (prescribed[i].has_value()) {
                b -= k[freeIndex[r]][i] * *prescribed[i];
            }
        }
        rhs[r] = b;
    }
    const std::vector<double> x = f > 0 ? solveDense(std::move(a), std::move(rhs)) : std::vector<double>{};
    out.temperature.assign(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        if (prescribed[i].has_value()) {
            out.temperature[i] = *prescribed[i];
        }
    }
    for (std::size_t r = 0; r < f; ++r) {
        out.temperature[freeIndex[r]] = x[r];
    }
    out.reaction.assign(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        if (!prescribed[i].has_value()) {
            continue;
        }
        double row = 0.0;
        for (std::size_t j = 0; j < n; ++j) {
            row += k[i][j] * out.temperature[j];
        }
        // Energy at node i: (K T)_i = Q_i + R_i, so the heat the constraint supplies INTO the domain is R_i = (K T)_i - Q_i:
        // positive at a hot held face, negative where the held face removes heat.
        out.reaction[i] = row - q[i];
    }
    return out;
}

// ---------------------------------------------------------------------------
// The independent dense TRANSIENT integrator (RM-THERM-08, non-uniform field)
// ---------------------------------------------------------------------------

/// A boundary triangle that convects: heat INTO the body is h (Tinf - T) per unit area.
struct RobinFace {
    std::array<std::size_t, 3> nodes{};
    double coefficient = 0.0; ///< h, W/(m^2 K)
    double ambient = 0.0;     ///< Tinf, K
};

/// Backward Euler on  C dT/dt + K T = Q  with the CONSISTENT capacity Ce_ij = rho cp V (1 + delta_ij)/20, the Robin face matrix
/// h A (1 + delta_ij)/12 and load h Tinf A/3, nodes held at @p prescribed, every step of size @p steps[n]. The initial field is
/// @p initial with the held nodes at their held value (a prescription overrides the initial condition). Returns the field at
/// t = 0 and after every step. Nothing here is a production kernel: the shape functions' integrals are the closed forms.
[[nodiscard]] inline std::vector<std::vector<double>>
solveTransient(const HeatMesh& mesh, double conductivity, double density, double specificHeat, double initial,
               const std::vector<std::optional<double>>& prescribed, const std::vector<RobinFace>& robin,
               const std::vector<double>& steps) {
    const std::size_t n = mesh.nodes.size();
    Dense k(n, std::vector<double>(n, 0.0));
    Dense c(n, std::vector<double>(n, 0.0));
    std::vector<double> q(n, 0.0);
    for (const std::array<std::size_t, 4>& e : mesh.tets) {
        Tet tet;
        for (std::size_t m = 0; m < 4; ++m) {
            tet.p[m] = mesh.nodes[e[m]];
        }
        const auto ke = tet.stiffness(conductivity);
        const double v = std::abs(tet.signedVolume());
        for (std::size_t a = 0; a < 4; ++a) {
            for (std::size_t b = 0; b < 4; ++b) {
                k[e[a]][e[b]] += ke[a][b];
                c[e[a]][e[b]] += density * specificHeat * v * (a == b ? 2.0 : 1.0) / 20.0;
            }
        }
    }
    for (const RobinFace& face : robin) {
        const Vec3& p0 = mesh.nodes[face.nodes[0]];
        const Vec3& p1 = mesh.nodes[face.nodes[1]];
        const Vec3& p2 = mesh.nodes[face.nodes[2]];
        const double area = 0.5 * norm(cross(minus(p1, p0), minus(p2, p0)));
        for (std::size_t a = 0; a < 3; ++a) {
            for (std::size_t b = 0; b < 3; ++b) {
                k[face.nodes[a]][face.nodes[b]] += face.coefficient * area * (a == b ? 2.0 : 1.0) / 12.0;
            }
            q[face.nodes[a]] += face.coefficient * face.ambient * area / 3.0;
        }
    }
    std::vector<std::size_t> freeIndex;
    for (std::size_t i = 0; i < n; ++i) {
        if (!prescribed[i].has_value()) {
            freeIndex.push_back(i);
        }
    }
    std::vector<double> t(n, initial);
    for (std::size_t i = 0; i < n; ++i) {
        if (prescribed[i].has_value()) {
            t[i] = *prescribed[i];
        }
    }
    std::vector<std::vector<double>> states{t};
    for (const double dt : steps) {
        const std::size_t f = freeIndex.size();
        Dense a(f, std::vector<double>(f, 0.0));
        std::vector<double> rhs(f, 0.0);
        for (std::size_t r = 0; r < f; ++r) {
            const std::size_t i = freeIndex[r];
            for (std::size_t col = 0; col < f; ++col) {
                const std::size_t j = freeIndex[col];
                a[r][col] = c[i][j] / dt + k[i][j];
            }
            double b = q[i];
            for (std::size_t j = 0; j < n; ++j) {
                b += c[i][j] / dt * t[j];
                if (prescribed[j].has_value()) {
                    b -= (c[i][j] / dt + k[i][j]) * *prescribed[j];
                }
            }
            rhs[r] = b;
        }
        const std::vector<double> x = f > 0 ? solveDense(std::move(a), std::move(rhs)) : std::vector<double>{};
        for (std::size_t r = 0; r < f; ++r) {
            t[freeIndex[r]] = x[r];
        }
        states.push_back(t);
    }
    return states;
}

} // namespace bettercad::test::analytic::heat
