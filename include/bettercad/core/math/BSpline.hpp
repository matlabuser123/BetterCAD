#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Export.hpp>
#include <bettercad/core/math/Point.hpp>

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace bettercad {

inline constexpr int kMinBSplineDegree = 2;
inline constexpr int kMaxBSplineDegree = 5;

/// A planar, non-rational B-spline with uniform integer knots, of degree
/// kMinBSplineDegree to kMaxBSplineDegree (P12-SKETCH-002). It is the curve
/// of sketch splines and of profile spline segments; the math is here, in
/// core, so that sketches and the geometry kernel share it.
///
/// - Open: clamped knots 0 (degree + 1 times), 1, ..., n - degree
///   (degree + 1 times); the curve starts at the first pole and ends at the
///   last, over the parameter domain [0, n - degree].
/// - Periodic: the first `degree` poles repeat after the last and the knots
///   are 0, 1, ..., n + 2 degree; the curve is closed and smooth, over the
///   domain [degree, n + degree].
///
/// Every integer in the domain starts a polynomial span. The curve lies in the
/// convex hull of its poles.
class BETTERCAD_CORE_EXPORT UniformBSpline {
public:
    /// Fails with InvalidArgument for a degree out of range, fewer than
    /// degree + 1 poles, non-finite poles or a control polygon of zero length
    /// (within 1e-10 m).
    [[nodiscard]] static Result<UniformBSpline> create(std::span<const Point2D> poles, int degree, bool periodic);

    /// The structural part of create()'s checks: the degree range and the
    /// pole count. Fails with InvalidArgument.
    [[nodiscard]] static Result<void> checkStructure(std::size_t poleCount, int degree);

    [[nodiscard]] int degree() const noexcept { return degree_; }
    [[nodiscard]] bool periodic() const noexcept { return periodic_; }
    /// The parameter domain.
    [[nodiscard]] double first() const noexcept { return first_; }
    [[nodiscard]] double last() const noexcept { return last_; }

    /// A point of the curve and its derivative with respect to the
    /// parameter (metres, metres per parameter unit).
    struct Sample {
        double x = 0.0;
        double y = 0.0;
        double dx = 0.0;
        double dy = 0.0;
    };

    /// De Boor's algorithm at @p u, clamped to the domain.
    [[nodiscard]] Sample evaluate(double u) const;

    /// The curve as Bézier pieces, one per knot span of the domain, in order:
    /// degree + 1 control points each (metres, x then y). A piece lies in the
    /// convex hull of its control points. Computed by blossoming, which takes
    /// only convex combinations of the poles.
    [[nodiscard]] std::vector<std::vector<std::array<double, 2>>> bezierPieces() const;

    /// The knots in the form kernels take for the poles as given (a periodic
    /// spline's poles are not repeated): distinct values and multiplicities.
    [[nodiscard]] std::vector<double> knotValues() const;
    [[nodiscard]] std::vector<int> knotMultiplicities() const;

private:
    UniformBSpline() = default;

    int degree_ = 3;
    bool periodic_ = false;
    std::size_t poleCount_ = 0;
    std::vector<double> knots_;
    std::vector<double> poleX_;
    std::vector<double> poleY_;
    double first_ = 0.0;
    double last_ = 1.0;
};

/// Gauss-Legendre nodes and weights on [0, 1] with kPoints points: exact for
/// polynomials of degree up to 2 kPoints - 1 = 15, which covers the
/// Green's-theorem integrands of B-splines of degree up to 5 (degree
/// 3 d - 1 for first moments). The nodes are Newton-refined roots of the
/// Legendre polynomial.
struct GaussLegendreRule {
    static constexpr std::size_t kPoints = 8;
    std::array<double, kPoints> nodes{};
    std::array<double, kPoints> weights{};
};

[[nodiscard]] BETTERCAD_CORE_EXPORT const GaussLegendreRule& gaussLegendreRule();

} // namespace bettercad
