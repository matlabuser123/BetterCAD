#include <bettercad/core/units/Format.hpp>
#include <bettercad/features/Profiles.hpp>

#include <cmath>
#include <cstddef>
#include <format>
#include <numbers>
#include <vector>

namespace bettercad::features {

namespace {

using geometry::ArcSegment2D;
using geometry::CircleSegment2D;
using geometry::LineSegment2D;
using geometry::PlanarRegion;
using geometry::ProfileLoop;
using geometry::ProfileSegment;

constexpr double kTwoPi = 2.0 * std::numbers::pi;

/// Angle in [0, 2 pi).
double normalized(double angle) {
    double result = std::fmod(angle, kTwoPi);
    if (result < 0.0) {
        result += kTwoPi;
    }
    return result >= kTwoPi ? 0.0 : result;
}

double angleOf(const Point2D& center, const Point2D& p) {
    return std::atan2((p.y - center.y).si(), (p.x - center.x).si());
}

std::string describe(const Point2D& p) {
    return std::format("({}, {}) mm", p.x.in(units::mm), p.y.in(units::mm));
}

ProfileSegment reverseSegment(const ProfileSegment& segment) {
    if (const auto* line = std::get_if<LineSegment2D>(&segment)) {
        return LineSegment2D{line->end, line->start};
    }
    const auto& arc = std::get<ArcSegment2D>(segment);
    return ArcSegment2D{arc.center, arc.end, arc.start, !arc.counterClockwise};
}

/// Unique end points: points closer than the sketch tolerance are one vertex.
struct Vertices {
    std::vector<Point2D> points;

    std::size_t indexOf(const Point2D& p) {
        for (std::size_t i = 0; i < points.size(); ++i) {
            if (distance(points[i], p) <= sketch::Sketch::kLengthTolerance) {
                return i;
            }
        }
        points.push_back(p);
        return points.size() - 1;
    }
};

struct Edge {
    ProfileSegment segment;
    std::size_t a = 0; ///< vertex at segment start
    std::size_t b = 0; ///< vertex at segment end
};

/// True if the counter-clockwise or clockwise arc passes through angle @p t.
bool arcContainsAngle(const ArcSegment2D& arc, double t) {
    const double a0 = angleOf(arc.center, arc.start);
    const double a1 = angleOf(arc.center, arc.end);
    if (arc.counterClockwise) {
        return normalized(t - a0) <= normalized(a1 - a0);
    }
    return normalized(a0 - t) <= normalized(a0 - a1);
}

/// Even-odd test with a ray from @p p towards +x; arcs are intersected
/// exactly. @p p is chosen away from vertices by the caller.
bool loopContains(const ProfileLoop& loop, const Point2D& p) {
    const double px = p.x.si();
    const double py = p.y.si();
    int crossings = 0;
    const auto circleCrossings = [&](const Point2D& c, double r, auto&& accept) {
        const double dy = py - c.y.si();
        if (std::abs(dy) >= r) {
            return;
        }
        const double dx = std::sqrt(r * r - dy * dy);
        for (const double sx : {dx, -dx}) {
            if (c.x.si() + sx > px && accept(std::atan2(dy, sx))) {
                ++crossings;
            }
        }
    };
    for (const ProfileSegment& segment : loop.segments) {
        if (const auto* line = std::get_if<LineSegment2D>(&segment)) {
            const double ay = line->start.y.si();
            const double by = line->end.y.si();
            if ((ay > py) != (by > py)) {
                const double ax = line->start.x.si();
                const double x = ax + (py - ay) * (line->end.x.si() - ax) / (by - ay);
                if (x > px) {
                    ++crossings;
                }
            }
        } else if (const auto* arc = std::get_if<ArcSegment2D>(&segment)) {
            circleCrossings(arc->center, distance(arc->center, arc->start).si(),
                            [&](double t) { return arcContainsAngle(*arc, t); });
        } else {
            const auto& circle = std::get<CircleSegment2D>(segment);
            circleCrossings(circle.center, circle.radius.si(), [](double) { return true; });
        }
    }
    return crossings % 2 == 1;
}

/// A point on the loop at an irrational fraction of its first segment, which
/// avoids ray tests passing exactly through other loops' vertices.
Point2D samplePoint(const ProfileLoop& loop) {
    constexpr double fraction = 1.0 / std::numbers::pi;
    const ProfileSegment& first = loop.segments.front();
    if (const auto* line = std::get_if<LineSegment2D>(&first)) {
        return {line->start.x + (line->end.x - line->start.x) * fraction,
                line->start.y + (line->end.y - line->start.y) * fraction};
    }
    if (const auto* arc = std::get_if<ArcSegment2D>(&first)) {
        const double a0 = angleOf(arc->center, arc->start);
        const double sweep = arc->counterClockwise ? normalized(angleOf(arc->center, arc->end) - a0)
                                                   : -normalized(a0 - angleOf(arc->center, arc->end));
        const Length r = distance(arc->center, arc->start);
        const double t = a0 + fraction * sweep;
        return {arc->center.x + r * std::cos(t), arc->center.y + r * std::sin(t)};
    }
    const auto& circle = std::get<CircleSegment2D>(first);
    return {circle.center.x + circle.radius * std::cos(1.0), circle.center.y + circle.radius * std::sin(1.0)};
}

} // namespace

Result<std::vector<PlanarRegion>> extractRegions(const sketch::Sketch& sketch) {
    std::vector<ProfileLoop> loops;
    std::vector<Edge> edges;
    Vertices vertices;

    // Collect profile edges; full circles are loops on their own.
    for (const sketch::Entity& entity : sketch.entities()) {
        if (entity.construction) {
            continue;
        }
        switch (entity.type()) {
        case sketch::EntityType::Point:
            break;
        case sketch::EntityType::Circle: {
            loops.push_back(ProfileLoop{{CircleSegment2D{sketch.center(entity.id).value(),
                                                          sketch.radius(entity.id).value(), true}}});
            break;
        }
        case sketch::EntityType::Line:
        case sketch::EntityType::Arc: {
            const sketch::Endpoints ends = sketch.endpoints(entity.id).value();
            const std::size_t a = vertices.indexOf(ends.start);
            const std::size_t b = vertices.indexOf(ends.end);
            if (a == b) {
                return makeError(ErrorCode::InvalidArgument,
                                 std::format("{} has coincident ends and cannot be part of a profile", entity.id));
            }
            // Snap to the shared vertex positions so connected ends are identical.
            const Point2D start = vertices.points[a];
            const Point2D end = vertices.points[b];
            if (entity.type() == sketch::EntityType::Line) {
                edges.push_back({LineSegment2D{start, end}, a, b});
            } else {
                edges.push_back({ArcSegment2D{sketch.center(entity.id).value(), start, end, true}, a, b});
            }
            break;
        }
        }
    }

    // Every vertex must join exactly two edges.
    std::vector<std::vector<std::size_t>> incident(vertices.points.size());
    for (std::size_t i = 0; i < edges.size(); ++i) {
        incident[edges[i].a].push_back(i);
        incident[edges[i].b].push_back(i);
    }
    for (std::size_t v = 0; v < incident.size(); ++v) {
        if (incident[v].size() == 1) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("the profile is open: an edge ends at {} without a neighbour",
                                         describe(vertices.points[v])));
        }
        if (incident[v].size() > 2) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("the profile branches: {} edges meet at {}", incident[v].size(),
                                         describe(vertices.points[v])));
        }
    }

    // Walk each cycle, orienting every edge in the direction of travel.
    std::vector<bool> used(edges.size(), false);
    for (std::size_t first = 0; first < edges.size(); ++first) {
        if (used[first]) {
            continue;
        }
        ProfileLoop loop;
        const std::size_t origin = edges[first].a;
        std::size_t current = first;
        std::size_t at = origin;
        while (true) {
            used[current] = true;
            const Edge& edge = edges[current];
            if (edge.a == at) {
                loop.segments.push_back(edge.segment);
                at = edge.b;
            } else {
                loop.segments.push_back(reverseSegment(edge.segment));
                at = edge.a;
            }
            if (at == origin) {
                break;
            }
            const auto& next = incident[at];
            current = next[0] == current ? next[1] : next[0];
        }
        loops.push_back(std::move(loop));
    }

    if (loops.empty()) {
        return makeError(ErrorCode::FailedPrecondition, "the sketch has no closed profile");
    }

    // Orient all loops counter-clockwise and determine their nesting depth.
    std::vector<ProfileLoop> ccw;
    std::vector<Point2D> samples;
    for (const ProfileLoop& loop : loops) {
        const Area area = geometry::signedArea(loop);
        if (abs(area).si() <= 0.0) {
            return makeError(ErrorCode::InvalidArgument, "a profile loop encloses no area");
        }
        ccw.push_back(area < Area{} ? geometry::reversed(loop) : loop);
        samples.push_back(samplePoint(ccw.back()));
    }
    const std::size_t count = ccw.size();
    std::vector<std::size_t> depth(count, 0);
    std::vector<std::vector<std::size_t>> containers(count);
    for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t j = 0; j < count; ++j) {
            if (i != j && loopContains(ccw[j], samples[i])) {
                ++depth[i];
                containers[i].push_back(j);
            }
        }
    }

    // Even depth: outer boundary; odd depth: hole of its innermost container.
    std::vector<PlanarRegion> regions;
    std::vector<std::size_t> regionOf(count, count);
    for (std::size_t i = 0; i < count; ++i) {
        if (depth[i] % 2 == 0) {
            regionOf[i] = regions.size();
            regions.push_back(PlanarRegion{sketch.placement(), ccw[i], {}});
        }
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (depth[i] % 2 == 1) {
            for (const std::size_t j : containers[i]) {
                if (depth[j] + 1 == depth[i]) {
                    regions[regionOf[j]].holes.push_back(geometry::reversed(ccw[i]));
                    break;
                }
            }
        }
    }
    return regions;
}

} // namespace bettercad::features
