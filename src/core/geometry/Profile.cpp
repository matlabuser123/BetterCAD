#include <bettercad/core/geometry/Profile.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace bettercad::geometry {

namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;

/// Signed sweep of an arc: in (0, 2 pi) counter-clockwise, (-2 pi, 0) clockwise.
double arcSweep(const ArcSegment2D& arc) {
    const double a0 = std::atan2((arc.start.y - arc.center.y).si(), (arc.start.x - arc.center.x).si());
    const double a1 = std::atan2((arc.end.y - arc.center.y).si(), (arc.end.x - arc.center.x).si());
    double ccw = std::fmod(a1 - a0, kTwoPi);
    if (ccw <= 0.0) {
        ccw += kTwoPi;
    }
    return arc.counterClockwise ? ccw : ccw - kTwoPi;
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
    const auto& circle = std::get<CircleSegment2D>(segment);
    const double r = circle.radius.si();
    return (circle.counterClockwise ? 0.5 : -0.5) * r * r * kTwoPi;
}

} // namespace

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

} // namespace bettercad::geometry
