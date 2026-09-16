#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

// Independent geometry for validating the reference models. Nothing here
// uses BetterCAD or the kernel: expected properties are computed from exact
// descriptions of the parts (cross-sections made of straight lines and
// circular arcs) with Green's theorem,
//
//   ∫∫ u^m v^n dA = ∮ u^(m+1) v^n / (m + 1) dv,
//
// and Pappus' theorems for solids and surfaces of revolution. Lines are
// integrated exactly (4-point Gauss–Legendre is exact for the polynomials
// involved); arcs, split into pieces of at most 45°, with 16-point
// Gauss–Legendre on the angle, whose error for these trigonometric
// polynomials is far below double rounding. ReferenceModel_AnalyticToolkit
// checks the integrator against closed forms.
namespace bettercad::test::analytic {

inline constexpr double pi = std::numbers::pi;

struct Vec2 {
    double u = 0.0;
    double v = 0.0;
};

/// A piece of a closed boundary: a straight line from `from` to `to`, or an
/// arc of the circle (centre, radius) from angle a0 to a1 in radians (a1 <
/// a0 runs clockwise).
struct Piece {
    bool isArc = false;
    Vec2 from{};
    Vec2 to{};
    Vec2 centre{};
    double radius = 0.0;
    double a0 = 0.0;
    double a1 = 0.0;

    [[nodiscard]] Vec2 start() const {
        return isArc ? Vec2{centre.u + radius * std::cos(a0), centre.v + radius * std::sin(a0)} : from;
    }
    [[nodiscard]] Vec2 end() const {
        return isArc ? Vec2{centre.u + radius * std::cos(a1), centre.v + radius * std::sin(a1)} : to;
    }
};

[[nodiscard]] inline Piece line(Vec2 from, Vec2 to) {
    return {.isArc = false, .from = from, .to = to};
}

/// An arc from @p fromDeg to @p toDeg (degrees; decreasing runs clockwise).
[[nodiscard]] inline Piece arc(Vec2 centre, double radius, double fromDeg, double toDeg) {
    return {.isArc = true, .centre = centre, .radius = radius, .a0 = fromDeg * pi / 180.0, .a1 = toDeg * pi / 180.0};
}

/// A closed loop, piece after piece: counter-clockwise around material,
/// clockwise around a hole.
using Loop = std::vector<Piece>;

/// A loop through the given corners, closed back to the first.
[[nodiscard]] inline Loop polygon(const std::vector<Vec2>& corners) {
    Loop loop;
    for (std::size_t i = 0; i < corners.size(); ++i) {
        loop.push_back(line(corners[i], corners[(i + 1) % corners.size()]));
    }
    return loop;
}

/// A full circle as four quarter arcs, counter-clockwise (or clockwise).
[[nodiscard]] inline Loop circle(Vec2 centre, double radius, bool clockwise = false) {
    Loop loop;
    for (int k = 0; k < 4; ++k) {
        const double a = clockwise ? -90.0 * k : 90.0 * k;
        loop.push_back(arc(centre, radius, a, clockwise ? a - 90.0 : a + 90.0));
    }
    return loop;
}

/// Builds a loop piece by piece from a start point.
class Path {
public:
    explicit Path(Vec2 start) : start_(start), current_(start) {}

    /// A line to @p point.
    Path& to(Vec2 point) {
        loop_.push_back(line(current_, point));
        current_ = point;
        return *this;
    }
    /// An arc about @p centre from @p fromDeg to @p toDeg; it must start at
    /// the current point (largestGap() tells).
    Path& arcTo(Vec2 centre, double radius, double fromDeg, double toDeg) {
        loop_.push_back(arc(centre, radius, fromDeg, toDeg));
        current_ = loop_.back().end();
        return *this;
    }
    /// Closes the loop with a line back to the start.
    [[nodiscard]] Loop close() {
        if (std::hypot(current_.u - start_.u, current_.v - start_.v) > 0.0) {
            loop_.push_back(line(current_, start_));
        }
        return loop_;
    }

private:
    Vec2 start_;
    Vec2 current_;
    Loop loop_;
};

/// The largest gap between the end of a piece and the start of the next.
[[nodiscard]] inline double largestGap(const Loop& loop) {
    double worst = 0.0;
    for (std::size_t i = 0; i < loop.size(); ++i) {
        const Vec2 a = loop[i].end();
        const Vec2 b = loop[(i + 1) % loop.size()].start();
        worst = std::max(worst, std::hypot(a.u - b.u, a.v - b.v));
    }
    return worst;
}

namespace detail {

/// Gauss–Legendre nodes and weights on [-1, 1] (Newton on P_n).
template <std::size_t N>
struct GaussLegendre {
    std::array<double, N> x{};
    std::array<double, N> w{};

    GaussLegendre() {
        for (std::size_t i = 0; i < N; ++i) {
            double z = std::cos(pi * (static_cast<double>(i) + 0.75) / (static_cast<double>(N) + 0.5));
            double derivative = 0.0;
            for (int iteration = 0; iteration < 100; ++iteration) {
                double p0 = 1.0;
                double p1 = z;
                for (std::size_t k = 2; k <= N; ++k) {
                    const double kk = static_cast<double>(k);
                    const double p2 = ((2.0 * kk - 1.0) * z * p1 - (kk - 1.0) * p0) / kk;
                    p0 = p1;
                    p1 = p2;
                }
                derivative = static_cast<double>(N) * (z * p1 - p0) / (z * z - 1.0);
                const double step = p1 / derivative;
                z -= step;
                if (std::abs(step) < 1e-17) {
                    break;
                }
            }
            x[i] = z;
            w[i] = 2.0 / ((1.0 - z * z) * derivative * derivative);
        }
    }
};

inline const GaussLegendre<4>& lineRule() {
    static const GaussLegendre<4> rule;
    return rule;
}

inline const GaussLegendre<16>& arcRule() {
    static const GaussLegendre<16> rule;
    return rule;
}

/// ∫ f(u, v) dv along a piece, for a smooth f.
template <typename F>
double alongDv(const Piece& piece, F f) {
    double sum = 0.0;
    if (!piece.isArc) {
        const auto& rule = lineRule();
        const double du = piece.to.u - piece.from.u;
        const double dv = piece.to.v - piece.from.v;
        for (std::size_t i = 0; i < rule.x.size(); ++i) {
            const double s = 0.5 * (rule.x[i] + 1.0);
            sum += 0.5 * rule.w[i] * f(piece.from.u + s * du, piece.from.v + s * dv) * dv;
        }
        return sum;
    }
    const auto& rule = arcRule();
    const double span = piece.a1 - piece.a0;
    const int parts = static_cast<int>(std::ceil(std::abs(span) / (pi / 4.0) - 1e-12));
    const double h = span / static_cast<double>(std::max(parts, 1));
    for (int p = 0; p < std::max(parts, 1); ++p) {
        const double t0 = piece.a0 + h * static_cast<double>(p);
        for (std::size_t i = 0; i < rule.x.size(); ++i) {
            const double t = t0 + 0.5 * h * (rule.x[i] + 1.0);
            const double u = piece.centre.u + piece.radius * std::cos(t);
            const double v = piece.centre.v + piece.radius * std::sin(t);
            sum += 0.5 * h * rule.w[i] * f(u, v) * piece.radius * std::cos(t);
        }
    }
    return sum;
}

} // namespace detail

/// ∫∫ u^m v^n dA over the region bounded by @p loops.
[[nodiscard]] inline double moment(const std::vector<Loop>& loops, int m, int n) {
    double sum = 0.0;
    for (const Loop& loop : loops) {
        for (const Piece& piece : loop) {
            sum += detail::alongDv(piece, [&](double u, double v) {
                return std::pow(u, m + 1) * std::pow(v, n) / static_cast<double>(m + 1);
            });
        }
    }
    return sum;
}

/// ∮ u ds, exact for lines and arcs.
[[nodiscard]] inline double boundaryMomentU(const std::vector<Loop>& loops) {
    double sum = 0.0;
    for (const Loop& loop : loops) {
        for (const Piece& piece : loop) {
            if (piece.isArc) {
                // ∫ (cu + R cos t) R |dt|, with u >= 0 along the arc.
                sum += piece.radius * std::abs(piece.centre.u * (piece.a1 - piece.a0) +
                                               piece.radius * (std::sin(piece.a1) - std::sin(piece.a0)));
            } else {
                sum += std::hypot(piece.to.u - piece.from.u, piece.to.v - piece.from.v) *
                       0.5 * (piece.from.u + piece.to.u);
            }
        }
    }
    return sum;
}

/// ∫ f(t) dt from @p a to @p b, with 16-point Gauss–Legendre: exact for the
/// polynomials of a lofted rib's cross-section (degree 3 and below).
template <typename F>
[[nodiscard]] double integrate(F f, double a, double b) {
    const auto& rule = detail::arcRule();
    double sum = 0.0;
    for (std::size_t i = 0; i < rule.x.size(); ++i) {
        sum += 0.5 * (b - a) * rule.w[i] * f(0.5 * (a + b) + 0.5 * (b - a) * rule.x[i]);
    }
    return sum;
}

/// The total length of the boundary.
[[nodiscard]] inline double perimeter(const std::vector<Loop>& loops) {
    double sum = 0.0;
    for (const Loop& loop : loops) {
        for (const Piece& piece : loop) {
            sum += piece.isArc ? piece.radius * std::abs(piece.a1 - piece.a0)
                               : std::hypot(piece.to.u - piece.from.u, piece.to.v - piece.from.v);
        }
    }
    return sum;
}

/// Properties of a solid, in millimetres.
struct Solid {
    double volume = 0.0;
    double area = 0.0;
    std::array<double, 3> centroid{};
};

/// The solid swept by a half cross-section turning once about the Z axis:
/// u is the radius (u >= 0) and v the height z. Volume and centroid by
/// Pappus, V = 2π ∫∫ u dA and V z̄ = 2π ∫∫ u v dA; area S = 2π ∮ u ds (edges on
/// the axis contribute nothing).
[[nodiscard]] inline Solid revolved(const std::vector<Loop>& loops) {
    const double v = 2.0 * pi * moment(loops, 1, 0);
    return {.volume = v,
            .area = 2.0 * pi * boundaryMomentU(loops),
            .centroid = {0.0, 0.0, 2.0 * pi * moment(loops, 1, 1) / v}};
}

/// A fillet's cross-section between two faces at right angles: the square
/// r × r in the corner less the quarter disc, area r²(1 − π/4), with its
/// centroid ū = r(10 − 3π)/(3(4 − π)) from each face.
[[nodiscard]] constexpr double filletArea(double r) {
    return r * r * (1.0 - pi / 4.0);
}
[[nodiscard]] constexpr double filletCentroid(double r) {
    return r * (10.0 - 3.0 * pi) / (3.0 * (4.0 - pi));
}

/// Depth of a countersink cone: (D - d) / 2 / tan(angle / 2).
[[nodiscard]] inline double countersinkDepth(double sinkDiameter, double diameter, double angleDeg) {
    return (sinkDiameter - diameter) / 2.0 / std::tan(angleDeg * pi / 360.0);
}

} // namespace bettercad::test::analytic
