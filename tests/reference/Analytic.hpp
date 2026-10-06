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

// --- Uniform-density solids, in SI (P15-REFMOD-001) --------------------------
//
// Closed forms, written out from the textbook rather than integrated, for the
// three shapes the material reference models use. Nothing here calls BetterCAD,
// the kernel or the CLI: these ARE the independent expectations, and a milestone
// whose subject is mass properties cannot take its expected masses from the code
// that computes masses.

/// A symmetric second-moment tensor, in kg m^2. Off-diagonals follow the matrix
/// convention BetterCAD's InertiaTensor uses -- the NEGATED products of inertia
/// -- so a comparison needs no sign fixing at the call site.
struct Inertia {
    double xx = 0.0;
    double yy = 0.0;
    double zz = 0.0;
    double xy = 0.0;
    double xz = 0.0;
    double yz = 0.0;
};

/// A uniform solid: what closed form says it weighs and where.
struct UniformSolid {
    double volume = 0.0;              ///< m^3
    double mass = 0.0;                ///< kg
    std::array<double, 3> centroid{}; ///< m, from the document origin
    Inertia centroidal{};             ///< kg m^2, about the centroid
};

/// A cuboid a x b x c (m) of density rho (kg/m^3), with one corner at the
/// origin and its edges along the axes.
///
///   V = abc,  m = rho V,  centroid (a/2, b/2, c/2),
///   Ixx = m(b^2 + c^2)/12,  Iyy = m(a^2 + c^2)/12,  Izz = m(a^2 + b^2)/12,
///   and every product of inertia zero, because each centroidal plane is a
///   plane of symmetry.
[[nodiscard]] inline UniformSolid cuboid(double a, double b, double c, double density) {
    const double volume = a * b * c;
    const double mass = density * volume;
    return {.volume = volume,
            .mass = mass,
            .centroid = {a / 2.0, b / 2.0, c / 2.0},
            .centroidal = {.xx = mass * (b * b + c * c) / 12.0,
                           .yy = mass * (a * a + c * c) / 12.0,
                           .zz = mass * (a * a + b * b) / 12.0}};
}

/// A solid cylinder of radius r and height h (m), axis along Z from z = 0.
///
///   V = pi r^2 h,  I_axis = m r^2 / 2,  I_transverse = m(3r^2 + h^2)/12.
///
/// The axis is Z because that is how the model is built -- an extrude of a
/// circle drawn on the XY plane -- and NOT because Z is a safe guess. The test
/// that uses this checks the model's construction.
[[nodiscard]] inline UniformSolid cylinder(double radius, double height, double density) {
    const double volume = pi * radius * radius * height;
    const double mass = density * volume;
    const double transverse = mass * (3.0 * radius * radius + height * height) / 12.0;
    return {.volume = volume,
            .mass = mass,
            .centroid = {0.0, 0.0, height / 2.0},
            .centroidal = {.xx = transverse, .yy = transverse, .zz = 0.5 * mass * radius * radius}};
}

/// A hollow cylinder, outer radius Ro, inner Ri, height h (m), axis along Z.
///
///   V = pi(Ro^2 - Ri^2)h,  I_axis = m(Ro^2 + Ri^2)/2,
///   I_transverse = m[3(Ro^2 + Ri^2) + h^2]/12.
///
/// The void has to reduce the volume, the mass AND the inertia. A bounding
/// cylinder of radius Ro would give 1.8 times this volume for the tube in the
/// reference suite, which no tolerance could absorb.
[[nodiscard]] inline UniformSolid hollowCylinder(double outer, double inner, double height, double density) {
    const double volume = pi * (outer * outer - inner * inner) * height;
    const double mass = density * volume;
    const double sumSquares = outer * outer + inner * inner;
    const double transverse = mass * (3.0 * sumSquares + height * height) / 12.0;
    return {.volume = volume,
            .mass = mass,
            .centroid = {0.0, 0.0, height / 2.0},
            .centroidal = {.xx = transverse, .yy = transverse, .zz = 0.5 * mass * sumSquares}};
}

/// A 3 x 3 rotation, row major.
using Rotation = std::array<std::array<double, 3>, 3>;

/// A right-handed rotation of @p degrees about the X axis.
[[nodiscard]] inline Rotation rotationAboutX(double degrees) {
    const double a = degrees * pi / 180.0;
    return Rotation{{{1.0, 0.0, 0.0}, {0.0, std::cos(a), -std::sin(a)}, {0.0, std::sin(a), std::cos(a)}}};
}

/// I' = R I R^T. Written out as a matrix triple product rather than as a
/// special case, so a rotation that mixes axes is handled too.
[[nodiscard]] inline Inertia rotated(const Inertia& inertia, const Rotation& r) {
    const double m[3][3] = {{inertia.xx, inertia.xy, inertia.xz},
                            {inertia.xy, inertia.yy, inertia.yz},
                            {inertia.xz, inertia.yz, inertia.zz}};
    double out[3][3]{};
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            double sum = 0.0;
            for (std::size_t k = 0; k < 3; ++k) {
                for (std::size_t l = 0; l < 3; ++l) {
                    sum += r[i][k] * m[k][l] * r[j][l];
                }
            }
            out[i][j] = sum;
        }
    }
    return {.xx = out[0][0], .yy = out[1][1], .zz = out[2][2],
            .xy = out[0][1], .xz = out[0][2], .yz = out[1][2]};
}

/// The parallel-axis theorem, I(P) = I(cm) + m[(d.d)1 - d d^T] with
/// d = cm - P, @p offset being d in metres.
///
/// From the centroid OUTWARDS, which is the direction that only adds and so
/// loses no significance. The reverse shift subtracts two nearly equal numbers.
[[nodiscard]] inline Inertia shifted(const Inertia& centroidal, double mass,
                                     const std::array<double, 3>& offset) {
    const double dd = offset[0] * offset[0] + offset[1] * offset[1] + offset[2] * offset[2];
    return {.xx = centroidal.xx + mass * (dd - offset[0] * offset[0]),
            .yy = centroidal.yy + mass * (dd - offset[1] * offset[1]),
            .zz = centroidal.zz + mass * (dd - offset[2] * offset[2]),
            .xy = centroidal.xy - mass * offset[0] * offset[1],
            .xz = centroidal.xz - mass * offset[0] * offset[2],
            .yz = centroidal.yz - mass * offset[1] * offset[2]};
}

/// A point turned by @p r and then moved by @p translation, all in metres.
[[nodiscard]] inline std::array<double, 3> placed(const std::array<double, 3>& point, const Rotation& r,
                                                  const std::array<double, 3>& translation) {
    std::array<double, 3> out{};
    for (std::size_t i = 0; i < 3; ++i) {
        out[i] = r[i][0] * point[0] + r[i][1] * point[1] + r[i][2] * point[2] + translation[i];
    }
    return out;
}

/// Depth of a countersink cone: (D - d) / 2 / tan(angle / 2).
[[nodiscard]] inline double countersinkDepth(double sinkDiameter, double diameter, double angleDeg) {
    return (sinkDiameter - diameter) / 2.0 / std::tan(angleDeg * pi / 360.0);
}

// --- Volumes and meshing bounds for the meshing suite (P16-REFMOD-001) ------
//
// In MILLIMETRES, because that is how the meshing reference models' dimensions
// are quoted, and converting a closed form is an opportunity to lose a factor
// of a thousand. The three SI closed forms above are not restated: these take
// the model's own dimensions and return a volume in mm^3.
//
// WHY THERE ARE BOUNDS AS WELL AS VOLUMES. A tetrahedral mesh of a CURVED body
// does not have the body's volume, and never will: the boundary is a polyhedron
// whose faces are chords of the true surface. So "the mesh volume equals the
// analytic volume" is the wrong expectation, and a percentage tolerance chosen
// because it passed would be exactly what the brief forbids.
//
// What IS available is a two-sided bound that follows from the declared surface
// deflection alone. A chord of a circle of radius r whose deepest deviation from
// the arc is at most d has its closest approach to the centre at r - d, so an
// inscribed chord polygon
//
//     contains  the disc of radius r - d
//     lies in   the disc of radius r
//
// and therefore has area between pi(r - d)^2 and pi r^2. Nothing is fitted, no
// segment count has to be known, and the direction of the error follows from
// which side of the material the curved surface is on -- which is why a hole's
// bound runs the OTHER WAY from a boss's and a check written for one would fail
// the other.

/// A two-sided bound, in mm^3. The meshed volume must lie inside it.
struct MeshedVolumeBound {
    double lower = 0.0;
    double upper = 0.0;

    [[nodiscard]] bool contains(double volume) const noexcept {
        return volume >= lower && volume <= upper;
    }
    /// The width of the bound, relative to its midpoint: how much the
    /// discretisation is allowed to move the answer at all.
    [[nodiscard]] double relativeWidth() const noexcept {
        return (upper - lower) / (0.5 * (upper + lower));
    }
};

/// A rectangular block: V = abc. Exact, and planar, so a conforming mesh of it
/// tiles the exact solid and must recover this to arithmetic accumulation.
[[nodiscard]] constexpr double blockVolumeMm3(double a, double b, double c) {
    return a * b * c;
}

/// A cylinder: V = pi r^2 h.
[[nodiscard]] inline double cylinderVolumeMm3(double radius, double height) {
    return pi * radius * radius * height;
}

/// A plate with one through-hole: V = L W t - pi r^2 t.
[[nodiscard]] inline double plateWithHoleVolumeMm3(double length, double width, double thickness,
                                                   double holeRadius) {
    return length * width * thickness - pi * holeRadius * holeRadius * thickness;
}

/// A hollow tube: V = pi (Ro^2 - Ri^2) h.
[[nodiscard]] inline double tubeVolumeMm3(double outerRadius, double innerRadius, double height) {
    return pi * (outerRadius * outerRadius - innerRadius * innerRadius) * height;
}

/// A cylinder's meshed volume: the lateral surface is inscribed, so the mesh is
/// SMALLER than the solid.
///
///     pi (r - d)^2 h  <=  V_mesh  <=  pi r^2 h
[[nodiscard]] inline MeshedVolumeBound cylinderMeshBound(double radius, double height,
                                                         double deflection) {
    return {.lower = cylinderVolumeMm3(radius - deflection, height),
            .upper = cylinderVolumeMm3(radius, height)};
}

/// A plate with a hole: the outer boundary is PLANAR and exact, and the hole's
/// wall is inscribed -- so the chord polygon removes LESS material than the true
/// circle and the mesh is LARGER than the solid.
///
///     L W t - pi r^2 t  <=  V_mesh  <=  L W t - pi (r - d)^2 t
///
/// The lower bound is the analytic volume itself, which makes the direction of
/// the error a prediction rather than an allowance: a mesh that came out BELOW
/// the analytic volume would mean the hole had been cut too big, and a mesh that
/// filled the hole would miss the upper bound by the whole hole.
[[nodiscard]] inline MeshedVolumeBound plateWithHoleMeshBound(double length, double width,
                                                              double thickness, double holeRadius,
                                                              double deflection) {
    return {.lower = plateWithHoleVolumeMm3(length, width, thickness, holeRadius),
            .upper = plateWithHoleVolumeMm3(length, width, thickness, holeRadius - deflection)};
}

/// A tube: the outer wall's chords LOSE material and the inner wall's chords
/// GAIN it, so the two errors are in opposite directions and the bound has to
/// take the extreme of each independently.
///
///     pi ((Ro - d)^2 - Ri^2) h  <=  V_mesh  <=  pi (Ro^2 - (Ri - d)^2) h
[[nodiscard]] inline MeshedVolumeBound tubeMeshBound(double outerRadius, double innerRadius,
                                                     double height, double deflection) {
    return {.lower = tubeVolumeMm3(outerRadius - deflection, innerRadius, height),
            .upper = tubeVolumeMm3(outerRadius, innerRadius - deflection, height)};
}

} // namespace bettercad::test::analytic
