#include "core/geometry/SweepPlan.hpp"

#include "core/geometry/ProfileExtent.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <limits>
#include <numbers>
#include <string>
#include <variant>

namespace bettercad::geometry::detail {

namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;
// Positions agree within the kernel's precision (the sketch tolerance).
constexpr double kLengthTolerance = 1e-10; // metres
// Directions agree when the sine of the angle between them is below this
// (the tolerance of edge and face matching and of revolution axes).
constexpr double kAngularTolerance = 1e-9;

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

Vec2 vec(const Point2D& p) {
    return {p.x.si(), p.y.si()};
}

Vec2 minus(Vec2 a, Vec2 b) {
    return {a.x - b.x, a.y - b.y};
}

double dot(Vec2 a, Vec2 b) {
    return a.x * b.x + a.y * b.y;
}

double cross(Vec2 a, Vec2 b) {
    return a.x * b.y - a.y * b.x;
}

double norm(Vec2 a) {
    return std::hypot(a.x, a.y);
}

Vec2 scaled(Vec2 a, double s) {
    return {a.x * s, a.y * s};
}

/// @p a turned by 90 degrees counter-clockwise.
Vec2 perp(Vec2 a) {
    return {-a.y, a.x};
}

bool finite(const Point2D& p) {
    return isFinite(p.x) && isFinite(p.y);
}

double tidy(double value) {
    return value == 0.0 ? 0.0 : value;
}

// Messages show 6 significant digits: computed values are rounded.
std::string mm(double metres) {
    return std::format("{:.6g} mm", tidy(metres * 1e3));
}

std::string point(Vec2 p) {
    return std::format("({:.6g}, {:.6g}) mm", tidy(p.x * 1e3), tidy(p.y * 1e3));
}

std::string degrees(double radians) {
    return std::format("{:.6g} deg", tidy(Angle::fromSi(radians).in(units::deg)));
}

enum class Kind { Line, Arc, Circle };

std::string_view kindName(Kind kind) {
    switch (kind) {
    case Kind::Line:
        return "a line";
    case Kind::Arc:
        return "an arc";
    case Kind::Circle:
        return "a circle";
    }
    return "a segment";
}

/// A path segment measured in the path plane's coordinates (metres).
struct Segment {
    Kind kind = Kind::Line;
    Vec2 start;
    Vec2 end;
    Vec2 startTangent; ///< unit
    Vec2 endTangent;   ///< unit
    double length = 0.0;
    double radius = 0.0; ///< arcs and circles
    /// Arcs and circles: the signed turn, positive counter-clockwise (to the
    /// left of travel).
    double sweep = 0.0;
};

Result<Segment> measure(const ProfileSegment& segment, std::size_t number) {
    const auto invalid = [&](std::string_view problem) {
        return makeError(ErrorCode::InvalidArgument, std::format("makeSweep: path segment {} {}", number, problem));
    };
    Segment s;
    if (const auto* line = std::get_if<LineSegment2D>(&segment)) {
        if (!finite(line->start) || !finite(line->end)) {
            return invalid("is not finite");
        }
        s.kind = Kind::Line;
        s.start = vec(line->start);
        s.end = vec(line->end);
        s.length = norm(minus(s.end, s.start));
        if (!(s.length > kLengthTolerance)) {
            return invalid("has zero length");
        }
        s.startTangent = scaled(minus(s.end, s.start), 1.0 / s.length);
        s.endTangent = s.startTangent;
        return s;
    }
    if (const auto* arc = std::get_if<ArcSegment2D>(&segment)) {
        if (!finite(arc->center) || !finite(arc->start) || !finite(arc->end)) {
            return invalid("is not finite");
        }
        const Vec2 c = vec(arc->center);
        s.kind = Kind::Arc;
        s.start = vec(arc->start);
        s.end = vec(arc->end);
        s.radius = norm(minus(s.start, c));
        if (!(s.radius > kLengthTolerance)) {
            return invalid("is an arc of zero radius");
        }
        if (std::abs(norm(minus(s.end, c)) - s.radius) > kLengthTolerance) {
            return invalid("is an arc whose ends are not on one circle");
        }
        if (!(norm(minus(s.end, s.start)) > kLengthTolerance)) {
            return invalid("is an arc whose ends coincide (use a circle for a full turn)");
        }
        s.sweep = arcSweep(*arc);
        const double sense = arc->counterClockwise ? 1.0 : -1.0;
        s.startTangent = scaled(perp(minus(s.start, c)), sense / s.radius);
        s.endTangent = scaled(perp(minus(s.end, c)), sense / s.radius);
        s.length = s.radius * std::abs(s.sweep);
        return s;
    }
    const auto& circle = std::get<CircleSegment2D>(segment);
    if (!finite(circle.center) || !isFinite(circle.radius)) {
        return invalid("is not finite");
    }
    s.kind = Kind::Circle;
    s.radius = circle.radius.si();
    if (!(s.radius > kLengthTolerance)) {
        return invalid("is a circle of zero radius");
    }
    // A circle starts on the plane's X axis through its centre.
    const Vec2 c = vec(circle.center);
    s.start = {c.x + s.radius, c.y};
    s.end = s.start;
    s.sweep = circle.counterClockwise ? kTwoPi : -kTwoPi;
    s.startTangent = {0.0, circle.counterClockwise ? 1.0 : -1.0};
    s.endTangent = s.startTangent;
    s.length = s.radius * kTwoPi;
    return s;
}

Direction3D direction(const Frame3D& plane, Vec2 d) {
    const Direction3D& x = plane.xAxis();
    const Direction3D& y = plane.yAxis();
    // Orthonormal axes and a unit d: unit to rounding.
    return *Direction3D::fromComponents(x.x() * d.x + y.x() * d.y, x.y() * d.x + y.y() * d.y,
                                        x.z() * d.x + y.z() * d.y);
}

} // namespace

Result<SweepPlan> planSweep(const PlanarRegion& region, const PlanarPath& path) {
    const auto invalid = [](const std::string& problem) {
        return makeError(ErrorCode::InvalidArgument, std::format("makeSweep: {}", problem));
    };
    if (path.segments.empty()) {
        return invalid("the path is empty");
    }
    const Point3D& pathOrigin = path.plane.origin();
    if (!isFinite(pathOrigin.x) || !isFinite(pathOrigin.y) || !isFinite(pathOrigin.z)) {
        return invalid("the path plane's origin is not finite");
    }
    const std::size_t count = path.segments.size();
    std::vector<Segment> segments;
    segments.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (std::holds_alternative<CircleSegment2D>(path.segments[i]) && count != 1) {
            return invalid(std::format("path segment {} is a full circle, which is a closed path on its own and "
                                       "cannot be joined with other segments",
                                       i + 1));
        }
        auto measured = measure(path.segments[i], i + 1);
        if (!measured) {
            return std::unexpected(measured.error());
        }
        segments.push_back(*measured);
    }
    for (std::size_t i = 0; i + 1 < count; ++i) {
        const double gap = norm(minus(segments[i + 1].start, segments[i].end));
        if (gap > kLengthTolerance) {
            return invalid(std::format("the path is not connected: segment {} ends at {}, but segment {} starts at "
                                       "{}, {} away",
                                       i + 1, point(segments[i].end), i + 2, point(segments[i + 1].start), mm(gap)));
        }
    }

    SweepPlan plan;
    const bool circle = segments.front().kind == Kind::Circle;
    plan.closed = circle || (count >= 2 && norm(minus(segments.front().start, segments.back().end)) <=
                                               kLengthTolerance);
    // Joints: between consecutive segments, and from the last to the first
    // of a closed chain (a lone circle closes smoothly).
    const std::size_t jointCount = circle ? 0 : (plan.closed ? count : count - 1);
    std::vector<double> turns(jointCount, 0.0); // signed, positive to the left
    for (std::size_t j = 0; j < jointCount; ++j) {
        const Segment& a = segments[j];
        const Segment& b = segments[(j + 1) % count];
        const double sine = cross(a.endTangent, b.startTangent);
        const double cosine = dot(a.endTangent, b.startTangent);
        if (std::abs(sine) <= kAngularTolerance && cosine > 0.0) {
            plan.joints.push_back(PathJoint::Smooth);
            continue;
        }
        const std::size_t first = j + 1;
        const std::size_t second = (j + 1) % count + 1;
        if (a.kind != Kind::Line || b.kind != Kind::Line) {
            return invalid(std::format("path segments {} ({}) and {} ({}) meet at an angle of {}; only two straight "
                                       "segments may meet at a corner, and an arc must meet its neighbours "
                                       "tangentially",
                                       first, kindName(a.kind), second, kindName(b.kind),
                                       degrees(std::atan2(std::abs(sine), cosine))));
        }
        if (std::abs(sine) <= kAngularTolerance) {
            return invalid(std::format("the path turns back on itself where segments {} and {} meet", first, second));
        }
        plan.joints.push_back(PathJoint::Corner);
        turns[j] = std::atan2(sine, cosine);
    }

    // The region must sit on the path's start, across it.
    const Point3D start = path.plane.toGlobal(Point2D{Length::fromSi(segments.front().start.x),
                                                      Length::fromSi(segments.front().start.y)});
    const Direction3D tangent = direction(path.plane, segments.front().startTangent);
    const Length offset = region.plane.signedDistance(start);
    if (!(abs(offset).si() <= kLengthTolerance)) {
        return invalid(std::format("the path must start on the profile's plane, but it starts {} from it",
                                   mm(abs(offset).si())));
    }
    const Direction3D& normal = region.plane.normal();
    const double tx = tangent.y() * normal.z() - tangent.z() * normal.y();
    const double ty = tangent.z() * normal.x() - tangent.x() * normal.z();
    const double tz = tangent.x() * normal.y() - tangent.y() * normal.x();
    const double tilt = std::sqrt(tx * tx + ty * ty + tz * tz);
    if (tilt > kAngularTolerance) {
        return invalid(std::format("the path must leave the profile's plane at right angles, but it leaves at {} "
                                   "to the plane's normal",
                                   degrees(std::asin(std::min(1.0, tilt)))));
    }

    // Offsets n of the region's points along N = B x T at the start, where B
    // is the path plane's normal: n is kept all along the path. N lies in
    // the region's plane, so n is the signed distance from a line there.
    const Direction3D& b = path.plane.normal();
    const auto side = b.cross(tangent); // unit: B is perpendicular to T
    if (!side) {
        return invalid("the path's tangent is parallel to the path plane's normal");
    }
    const Point2D origin = region.plane.toLocal(start);
    const double nx = side->dot(region.plane.xAxis());
    const double ny = side->dot(region.plane.yAxis());
    // The line along (ny, -nx) has (nx, ny) as its left normal.
    const PlaneAxis axis{origin.x.si(), origin.y.si(), ny, -nx};
    const Area area = regionArea(region);
    if (!(area.si() > kLengthTolerance * kLengthTolerance)) {
        return invalid("the profile encloses no area");
    }
    double nMin = std::numeric_limits<double>::infinity();
    double nMax = -std::numeric_limits<double>::infinity();
    extendSideRange(region.outer, axis, nMin, nMax);
    const double nCentroid = axis.side(regionCentroid(region));

    // Along each segment a point at offset n travels l(n) = l0 - k n: k is
    // the signed turn of an arc, or the tangents of the half corner angles
    // at the ends of a straight segment (its mitres). l must stay positive
    // for every point of the region, or the solid folds over itself.
    double traced = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const Segment& s = segments[i];
        double k = s.sweep;
        if (s.kind == Kind::Line) {
            k = 0.0;
            const bool hasBefore = i > 0 || plan.closed;
            const std::size_t before = i > 0 ? i - 1 : jointCount - 1;
            if (hasBefore && jointCount > 0 && plan.joints[before] == PathJoint::Corner) {
                k += std::tan(turns[before] / 2.0);
            }
            if (i < jointCount && plan.joints[i] == PathJoint::Corner) {
                k += std::tan(turns[i] / 2.0);
            }
            const double used = std::max(k * nMin, k * nMax);
            if (used >= s.length - kLengthTolerance) {
                const double reach = k * nMin > k * nMax ? std::abs(nMin) : std::abs(nMax);
                return invalid(std::format("path segment {} is too short for the mitred corners at its ends: it is "
                                           "{} long, but the profile reaches {} from the path, which uses {} of it",
                                           i + 1, mm(s.length), mm(reach), mm(used)));
            }
        } else {
            const double reach = s.sweep > 0.0 ? nMax : -nMin;
            if (reach >= s.radius - kLengthTolerance) {
                return invalid(std::format("the profile reaches {} from the path towards the centre of path segment "
                                           "{}, {} of radius {}: the swept solid would fold over itself",
                                           mm(reach), i + 1, kindName(s.kind), mm(s.radius)));
            }
        }
        traced += s.length - k * nCentroid;
    }
    plan.expectedVolume = Volume::fromSi(area.si() * traced);
    return plan;
}

} // namespace bettercad::geometry::detail
