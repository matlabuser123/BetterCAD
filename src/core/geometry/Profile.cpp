#include "core/geometry/ProfileExtent.hpp"

#include <bettercad/core/geometry/Profile.hpp>
#include <bettercad/core/math/BSpline.hpp>
#include <bettercad/core/units/Format.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>

namespace bettercad::geometry {

namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;

using detail::arcSweep;

/// An ellipse in plain numbers: centre, unit first axis (ux, uy), semi-axes
/// a (first) and b (second), and the sense of travel (+1 counter-clockwise).
struct EllipseFrame {
    double cx = 0.0;
    double cy = 0.0;
    double ux = 1.0;
    double uy = 0.0;
    double a = 0.0;
    double b = 0.0;
    double sense = 1.0;

    /// The point at angle @p theta from the first axis, counter-clockwise.
    [[nodiscard]] Point2D at(double theta) const {
        const double c = a * std::cos(theta);
        const double s = b * std::sin(theta);
        return {Length::fromSi(cx + c * ux - s * uy), Length::fromSi(cy + c * uy + s * ux)};
    }
};

EllipseFrame frameOf(const EllipseSegment2D& ellipse) {
    const double dx = (ellipse.xVertex.x - ellipse.center.x).si();
    const double dy = (ellipse.xVertex.y - ellipse.center.y).si();
    const double a = std::hypot(dx, dy);
    return {ellipse.center.x.si(),
            ellipse.center.y.si(),
            a > 0.0 ? dx / a : 1.0,
            a > 0.0 ? dy / a : 0.0,
            a,
            ellipse.radiusY.si(),
            ellipse.counterClockwise ? 1.0 : -1.0};
}

/// The spline's curve. Precondition: validate(spline) succeeded.
UniformBSpline curveOf(const SplineSegment2D& spline) {
    return *UniformBSpline::create(spline.poles, spline.degree, spline.periodic);
}

/// Green's integrands over a spline: 1/2 (x y' - y x'), and the first-moment
/// terms x^2/2 y' and -y^2/2 x' about (ox, oy), each integrated over every
/// polynomial span with a Gauss rule exact for its degree.
std::array<double, 3> splineIntegrals(const SplineSegment2D& spline, double ox, double oy) {
    const UniformBSpline curve = curveOf(spline);
    const GaussLegendreRule& rule = gaussLegendreRule();
    double area = 0.0;
    double mx = 0.0;
    double my = 0.0;
    for (double span = curve.first(); span < curve.last(); span += 1.0) {
        for (std::size_t i = 0; i < GaussLegendreRule::kPoints; ++i) {
            const UniformBSpline::Sample p = curve.evaluate(span + rule.nodes[i]);
            const double w = rule.weights[i];
            area += w * 0.5 * (p.x * p.dy - p.y * p.dx);
            const double x = p.x - ox;
            const double y = p.y - oy;
            mx += w * 0.5 * x * x * p.dy;
            my -= w * 0.5 * y * y * p.dx;
        }
    }
    return {area, mx, my};
}

// Contribution 1/2 * integral(x dy - y dx) of one segment, in m^2.
double greenTerm(const ProfileSegment& segment) {
    if (const auto* line = std::get_if<LineSegment2D>(&segment)) {
        return 0.5 * (line->start.x.si() * line->end.y.si() - line->end.x.si() * line->start.y.si());
    }
    if (const auto* arc = std::get_if<ArcSegment2D>(&segment)) {
        // For x = cx + r cos t, y = cy + r sin t:
        // 1/2 [cx (y1 - y0) - cy (x1 - x0) + r^2 * sweep]
        const double r = distance(arc->center, arc->start).si();
        const double cx = arc->center.x.si();
        const double cy = arc->center.y.si();
        return 0.5 * (cx * (arc->end.y - arc->start.y).si() - cy * (arc->end.x - arc->start.x).si() +
                      r * r * arcSweep(*arc));
    }
    if (const auto* circle = std::get_if<CircleSegment2D>(&segment)) {
        const double r = circle->radius.si();
        return (circle->counterClockwise ? 0.5 : -0.5) * r * r * kTwoPi;
    }
    if (const auto* ellipse = std::get_if<EllipseSegment2D>(&segment)) {
        const EllipseFrame f = frameOf(*ellipse);
        return f.sense * std::numbers::pi * f.a * f.b;
    }
    return splineIntegrals(std::get<SplineSegment2D>(segment), 0.0, 0.0)[0];
}

/// First moments (integral of x dA, integral of y dA) enclosed by one
/// segment, by Green's theorem: integral(x^2/2 dy) and -integral(y^2/2 dx),
/// in m^3, with coordinates taken relative to (ox, oy).
std::array<double, 2> firstMomentTerm(const ProfileSegment& segment, double ox, double oy) {
    if (const auto* line = std::get_if<LineSegment2D>(&segment)) {
        const double x0 = line->start.x.si() - ox;
        const double y0 = line->start.y.si() - oy;
        const double x1 = line->end.x.si() - ox;
        const double y1 = line->end.y.si() - oy;
        return {(y1 - y0) * (x0 * x0 + x0 * x1 + x1 * x1) / 6.0, -(x1 - x0) * (y0 * y0 + y0 * y1 + y1 * y1) / 6.0};
    }
    if (const auto* ellipse = std::get_if<EllipseSegment2D>(&segment)) {
        // A full ellipse is symmetric about its centre: its moments are its
        // signed area times the centre's position.
        const EllipseFrame f = frameOf(*ellipse);
        const double area = f.sense * std::numbers::pi * f.a * f.b;
        return {area * (f.cx - ox), area * (f.cy - oy)};
    }
    if (const auto* spline = std::get_if<SplineSegment2D>(&segment)) {
        const auto integrals = splineIntegrals(*spline, ox, oy);
        return {integrals[1], integrals[2]};
    }
    double cx = 0.0;
    double cy = 0.0;
    double r = 0.0;
    double t0 = 0.0;
    double sweep = 0.0;
    if (const auto* arc = std::get_if<ArcSegment2D>(&segment)) {
        cx = arc->center.x.si() - ox;
        cy = arc->center.y.si() - oy;
        r = distance(arc->center, arc->start).si();
        t0 = std::atan2((arc->start.y - arc->center.y).si(), (arc->start.x - arc->center.x).si());
        sweep = arcSweep(*arc);
    } else {
        const auto& circle = std::get<CircleSegment2D>(segment);
        cx = circle.center.x.si() - ox;
        cy = circle.center.y.si() - oy;
        r = circle.radius.si();
        sweep = circle.counterClockwise ? kTwoPi : -kTwoPi;
    }
    // For x = cx + r cos t, y = cy + r sin t:
    //   x^2/2 dy = r/2 (cx^2 cos t + 2 cx r cos^2 t + r^2 cos^3 t) dt
    //   -y^2/2 dx = r/2 (cy^2 sin t + 2 cy r sin^2 t + r^2 sin^3 t) dt
    const auto fx = [&](double t) {
        const double s = std::sin(t);
        return 0.5 * r *
               (cx * cx * s + 2.0 * cx * r * (t / 2.0 + std::sin(2.0 * t) / 4.0) + r * r * (s - s * s * s / 3.0));
    };
    const auto fy = [&](double t) {
        const double c = std::cos(t);
        return 0.5 * r *
               (-cy * cy * c + 2.0 * cy * r * (t / 2.0 - std::sin(2.0 * t) / 4.0) + r * r * (-c + c * c * c / 3.0));
    };
    const double t1 = t0 + sweep;
    return {fx(t1) - fx(t0), fy(t1) - fy(t0)};
}

ProfileSegment reversedSegment(const ProfileSegment& segment) {
    if (const auto* line = std::get_if<LineSegment2D>(&segment)) {
        return LineSegment2D{line->end, line->start};
    }
    if (const auto* arc = std::get_if<ArcSegment2D>(&segment)) {
        return ArcSegment2D{arc->center, arc->end, arc->start, !arc->counterClockwise};
    }
    if (const auto* circle = std::get_if<CircleSegment2D>(&segment)) {
        return CircleSegment2D{circle->center, circle->radius, !circle->counterClockwise};
    }
    if (const auto* ellipse = std::get_if<EllipseSegment2D>(&segment)) {
        return EllipseSegment2D{ellipse->center, ellipse->xVertex, ellipse->radiusY, !ellipse->counterClockwise};
    }
    SplineSegment2D spline = std::get<SplineSegment2D>(segment);
    std::ranges::reverse(spline.poles);
    return spline;
}

} // namespace

namespace detail {

double arcSweep(const ArcSegment2D& arc) {
    const double a0 = std::atan2((arc.start.y - arc.center.y).si(), (arc.start.x - arc.center.x).si());
    const double a1 = std::atan2((arc.end.y - arc.center.y).si(), (arc.end.x - arc.center.x).si());
    double ccw = std::fmod(a1 - a0, kTwoPi);
    if (ccw <= 0.0) {
        ccw += kTwoPi;
    }
    return arc.counterClockwise ? ccw : ccw - kTwoPi;
}

bool onCounterClockwiseArc(double angle, double start, double end) {
    const auto wrap = [](double a) {
        a = std::fmod(a, kTwoPi);
        return a < 0.0 ? a + kTwoPi : a;
    };
    return wrap(angle - start) <= wrap(end - start);
}

void extendSideRange(const ProfileLoop& loop, const PlaneAxis& axis, double& low, double& high) {
    const auto include = [&](double s) {
        low = std::min(low, s);
        high = std::max(high, s);
    };
    const double towards = axis.normalAngle();
    for (const ProfileSegment& segment : loop.segments) {
        if (const auto* line = std::get_if<LineSegment2D>(&segment)) {
            include(axis.side(line->start));
            include(axis.side(line->end));
        } else if (const auto* arc = std::get_if<ArcSegment2D>(&segment)) {
            include(axis.side(arc->start));
            include(axis.side(arc->end));
            const double radius = distance(arc->center, arc->start).si();
            const double centerSide = axis.side(arc->center);
            // A clockwise arc from start to end covers the counter-clockwise arc from end to start.
            const Point2D& first = arc->counterClockwise ? arc->start : arc->end;
            const Point2D& last = arc->counterClockwise ? arc->end : arc->start;
            const double a0 = std::atan2((first.y - arc->center.y).si(), (first.x - arc->center.x).si());
            const double a1 = std::atan2((last.y - arc->center.y).si(), (last.x - arc->center.x).si());
            if (onCounterClockwiseArc(towards, a0, a1)) {
                include(centerSide + radius);
            }
            if (onCounterClockwiseArc(towards + std::numbers::pi, a0, a1)) {
                include(centerSide - radius);
            }
        } else if (const auto* circle = std::get_if<CircleSegment2D>(&segment)) {
            include(axis.side(circle->center) + circle->radius.si());
            include(axis.side(circle->center) - circle->radius.si());
        } else if (const auto* ellipse = std::get_if<EllipseSegment2D>(&segment)) {
            // The ellipse reaches sqrt((a n.u)^2 + (b n.v)^2) to either side
            // of its centre along the axis normal n.
            const EllipseFrame f = frameOf(*ellipse);
            const double nx = -axis.dy;
            const double ny = axis.dx;
            const double nu = nx * f.ux + ny * f.uy;
            const double nv = -nx * f.uy + ny * f.ux;
            const double reach = std::hypot(f.a * nu, f.b * nv);
            const double centerSide = axis.side(ellipse->center);
            include(centerSide + reach);
            include(centerSide - reach);
        } else {
            // A spline lies in the convex hull of its poles, so their range
            // bounds the curve's (it may be wider than the curve's own).
            for (const Point2D& pole : std::get<SplineSegment2D>(segment).poles) {
                include(axis.side(pole));
            }
        }
    }
}

} // namespace detail

Result<void> validate(const SplineSegment2D& spline) {
    if (auto curve = UniformBSpline::create(spline.poles, spline.degree, spline.periodic); !curve) {
        return std::unexpected(curve.error());
    }
    return {};
}

bool isClosedSegment(const ProfileSegment& segment) noexcept {
    if (const auto* spline = std::get_if<SplineSegment2D>(&segment)) {
        return spline->periodic;
    }
    return std::holds_alternative<CircleSegment2D>(segment) || std::holds_alternative<EllipseSegment2D>(segment);
}

Point2D firstPoint(const ProfileSegment& segment) {
    if (const auto* line = std::get_if<LineSegment2D>(&segment)) {
        return line->start;
    }
    if (const auto* arc = std::get_if<ArcSegment2D>(&segment)) {
        return arc->start;
    }
    if (const auto* circle = std::get_if<CircleSegment2D>(&segment)) {
        return {circle->center.x + circle->radius, circle->center.y};
    }
    if (const auto* ellipse = std::get_if<EllipseSegment2D>(&segment)) {
        return ellipse->xVertex;
    }
    const auto& spline = std::get<SplineSegment2D>(segment);
    if (!spline.periodic) {
        return spline.poles.front();
    }
    return pointAt(segment, 0.0);
}

Point2D lastPoint(const ProfileSegment& segment) {
    if (const auto* line = std::get_if<LineSegment2D>(&segment)) {
        return line->end;
    }
    if (const auto* arc = std::get_if<ArcSegment2D>(&segment)) {
        return arc->end;
    }
    if (const auto* spline = std::get_if<SplineSegment2D>(&segment); spline != nullptr && !spline->periodic) {
        return spline->poles.back();
    }
    return firstPoint(segment);
}

Point2D pointAt(const ProfileSegment& segment, double t) {
    if (const auto* line = std::get_if<LineSegment2D>(&segment)) {
        return {line->start.x + (line->end.x - line->start.x) * t, line->start.y + (line->end.y - line->start.y) * t};
    }
    if (const auto* arc = std::get_if<ArcSegment2D>(&segment)) {
        const double r = distance(arc->center, arc->start).si();
        const double angle =
            std::atan2((arc->start.y - arc->center.y).si(), (arc->start.x - arc->center.x).si()) + t * arcSweep(*arc);
        return {arc->center.x + Length::fromSi(r * std::cos(angle)), arc->center.y + Length::fromSi(r * std::sin(angle))};
    }
    if (const auto* circle = std::get_if<CircleSegment2D>(&segment)) {
        const double angle = (circle->counterClockwise ? kTwoPi : -kTwoPi) * t;
        return {circle->center.x + circle->radius * std::cos(angle),
                circle->center.y + circle->radius * std::sin(angle)};
    }
    if (const auto* ellipse = std::get_if<EllipseSegment2D>(&segment)) {
        const EllipseFrame f = frameOf(*ellipse);
        return f.at(f.sense * kTwoPi * t);
    }
    const UniformBSpline curve = curveOf(std::get<SplineSegment2D>(segment));
    const UniformBSpline::Sample p = curve.evaluate(curve.first() + t * (curve.last() - curve.first()));
    return {Length::fromSi(p.x), Length::fromSi(p.y)};
}

Area signedArea(const ProfileLoop& loop) {
    double area = 0.0;
    for (const ProfileSegment& segment : loop.segments) {
        area += greenTerm(segment);
    }
    return Area::fromSi(area);
}

ProfileLoop reversed(const ProfileLoop& loop) {
    ProfileLoop result;
    result.segments.reserve(loop.segments.size());
    for (auto it = loop.segments.rbegin(); it != loop.segments.rend(); ++it) {
        result.segments.push_back(reversedSegment(*it));
    }
    return result;
}

Area regionArea(const PlanarRegion& region) {
    Area area = abs(signedArea(region.outer));
    for (const ProfileLoop& hole : region.holes) {
        area -= abs(signedArea(hole));
    }
    return area;
}

Point2D regionCentroid(const PlanarRegion& region) {
    // Moments about a point of the region keep the terms small (conditioning).
    Point2D origin{};
    if (!region.outer.segments.empty()) {
        const ProfileSegment& first = region.outer.segments.front();
        if (const auto* circle = std::get_if<CircleSegment2D>(&first)) {
            origin = circle->center;
        } else if (const auto* ellipse = std::get_if<EllipseSegment2D>(&first)) {
            origin = ellipse->center;
        } else {
            origin = firstPoint(first);
        }
    }
    const double ox = origin.x.si();
    const double oy = origin.y.si();
    // Each loop counts as if counter-clockwise: the outer loop adds, holes subtract.
    double area = 0.0;
    double mx = 0.0;
    double my = 0.0;
    const auto accumulate = [&](const ProfileLoop& loop, double sign) {
        double loopArea = 0.0;
        double loopMx = 0.0;
        double loopMy = 0.0;
        for (const ProfileSegment& segment : loop.segments) {
            loopArea += greenTerm(segment);
            const auto moment = firstMomentTerm(segment, ox, oy);
            loopMx += moment[0];
            loopMy += moment[1];
        }
        const double orientation = loopArea < 0.0 ? -1.0 : 1.0;
        area += sign * orientation * loopArea;
        mx += sign * orientation * loopMx;
        my += sign * orientation * loopMy;
    };
    accumulate(region.outer, 1.0);
    for (const ProfileLoop& hole : region.holes) {
        accumulate(hole, -1.0);
    }
    return {Length::fromSi(ox + mx / area), Length::fromSi(oy + my / area)};
}

} // namespace bettercad::geometry
