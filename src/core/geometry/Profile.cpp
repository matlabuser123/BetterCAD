#include "core/geometry/ProfileExtent.hpp"

#include <bettercad/core/geometry/Profile.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace bettercad::geometry {

namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;

using detail::arcSweep;

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
    const auto& circle = std::get<CircleSegment2D>(segment);
    const double r = circle.radius.si();
    return (circle.counterClockwise ? 0.5 : -0.5) * r * r * kTwoPi;
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
        } else {
            const auto& circle = std::get<CircleSegment2D>(segment);
            include(axis.side(circle.center) + circle.radius.si());
            include(axis.side(circle.center) - circle.radius.si());
        }
    }
}

} // namespace detail

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
        if (const auto* line = std::get_if<LineSegment2D>(&*it)) {
            result.segments.emplace_back(LineSegment2D{line->end, line->start});
        } else if (const auto* arc = std::get_if<ArcSegment2D>(&*it)) {
            result.segments.emplace_back(ArcSegment2D{arc->center, arc->end, arc->start, !arc->counterClockwise});
        } else {
            const auto& circle = std::get<CircleSegment2D>(*it);
            result.segments.emplace_back(CircleSegment2D{circle.center, circle.radius, !circle.counterClockwise});
        }
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
        if (const auto* line = std::get_if<LineSegment2D>(&first)) {
            origin = line->start;
        } else if (const auto* arc = std::get_if<ArcSegment2D>(&first)) {
            origin = arc->start;
        } else {
            origin = std::get<CircleSegment2D>(first).center;
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
