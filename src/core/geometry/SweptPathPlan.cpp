#include "core/geometry/SweptPathPlan.hpp"

#include "core/geometry/ProfileExtent.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>
#include <string>
#include <string_view>
#include <variant>

namespace bettercad::geometry::detail {

namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;
// As for a planar path: positions agree within the kernel's precision, and
// directions when the sine of the angle between them is below the tolerance
// of edge and face matching.
constexpr double kLengthTolerance = 1e-10; // metres
constexpr double kAngularTolerance = 1e-9;

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

Vec3 vec(const Point3D& p) {
    return {p.x.si(), p.y.si(), p.z.si()};
}

Vec3 vec(const Direction3D& d) {
    return {d.x(), d.y(), d.z()};
}

Point3D point(Vec3 v) {
    return Point3D{Length::fromSi(v.x), Length::fromSi(v.y), Length::fromSi(v.z)};
}

Vec3 minus(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 plus(Vec3 a, Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 scaled(Vec3 a, double s) {
    return {a.x * s, a.y * s, a.z * s};
}

double dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

double norm(Vec3 a) {
    return std::sqrt(dot(a, a));
}

std::optional<Direction3D> unit(Vec3 a) {
    return Direction3D::fromComponents(a.x, a.y, a.z);
}

double tidy(double value) {
    return value == 0.0 ? 0.0 : value;
}

std::string mm(double metres) {
    return std::format("{:.6g} mm", tidy(metres * 1e3));
}

std::string at(Vec3 p) {
    return std::format("({:.6g}, {:.6g}, {:.6g}) mm", tidy(p.x * 1e3), tidy(p.y * 1e3), tidy(p.z * 1e3));
}

std::string degrees(double radians) {
    return std::format("{:.6g} deg", tidy(Angle::fromSi(radians).in(units::deg)));
}

bool finite(const Point2D& p) {
    return isFinite(p.x) && isFinite(p.y);
}

bool finite(const Frame3D& plane) {
    const Point3D& o = plane.origin();
    return isFinite(o.x) && isFinite(o.y) && isFinite(o.z);
}

std::string_view kindName(const SpatialSegment& segment) {
    if (segment.circle) {
        return "a circle";
    }
    return segment.straight ? "a line" : "an arc";
}

/// One 2D segment of @p plane, measured in model space. @p number is its
/// place in the whole path, for messages.
Result<SpatialSegment> measure(const ProfileSegment& segment, const Frame3D& plane, std::size_t number,
                              std::string_view what) {
    const auto invalid = [&](std::string_view problem) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("makeSweep: {}segment {} {}", what, number, problem));
    };
    SpatialSegment s;
    if (const auto* line = std::get_if<LineSegment2D>(&segment)) {
        if (!finite(line->start) || !finite(line->end)) {
            return invalid("is not finite");
        }
        s.straight = true;
        s.start = plane.toGlobal(line->start);
        s.end = plane.toGlobal(line->end);
        const auto tangent = unit(minus(vec(s.end), vec(s.start)));
        if (!tangent) {
            return invalid(std::format("is a point at {}: a path segment needs a length", at(vec(s.start))));
        }
        s.startTangent = *tangent;
        s.endTangent = *tangent;
        s.length = Length::fromSi(norm(minus(vec(s.end), vec(s.start))));
        return s;
    }
    if (const auto* arc = std::get_if<ArcSegment2D>(&segment)) {
        if (!finite(arc->center) || !finite(arc->start) || !finite(arc->end)) {
            return invalid("is not finite");
        }
        s.straight = false;
        s.center = plane.toGlobal(arc->center);
        s.start = plane.toGlobal(arc->start);
        s.end = plane.toGlobal(arc->end);
        const Vec3 fromStart = minus(vec(s.start), vec(s.center));
        const Vec3 fromEnd = minus(vec(s.end), vec(s.center));
        const double radius = norm(fromStart);
        if (!(radius > kLengthTolerance)) {
            return invalid("is an arc of no radius");
        }
        if (std::abs(norm(fromEnd) - radius) > kLengthTolerance) {
            return invalid(std::format("is an arc whose ends are {} and {} from its centre", mm(radius),
                                       mm(norm(fromEnd))));
        }
        s.radius = Length::fromSi(radius);
        const auto axis = arc->counterClockwise ? plane.normal() : plane.normal().reversed();
        s.axis = axis;
        // The turn from start to end about the axis, in [0, 2 pi).
        const Vec3 a = vec(axis);
        const double cosine = dot(fromStart, fromEnd) / (radius * radius);
        const double sine = dot(cross(fromStart, fromEnd), a) / (radius * radius);
        double turn = std::atan2(sine, std::clamp(cosine, -1.0, 1.0));
        if (turn < kAngularTolerance) {
            turn += kTwoPi;
        }
        if (turn <= kAngularTolerance || turn >= kTwoPi - kAngularTolerance) {
            return invalid("is an arc whose ends coincide");
        }
        s.turn = Angle::fromSi(turn);
        s.length = Length::fromSi(radius * turn);
        const auto startTangent = unit(cross(a, fromStart));
        const auto endTangent = unit(cross(a, fromEnd));
        if (!startTangent || !endTangent) {
            return invalid("is an arc BetterCAD cannot measure");
        }
        s.startTangent = *startTangent;
        s.endTangent = *endTangent;
        return s;
    }
    if (const auto* full = std::get_if<CircleSegment2D>(&segment)) {
        if (!finite(full->center) || !isFinite(full->radius)) {
            return invalid("is not finite");
        }
        if (!(full->radius.si() > kLengthTolerance)) {
            return invalid("is a circle of no radius");
        }
        s.straight = false;
        s.circle = true;
        s.center = plane.toGlobal(full->center);
        s.radius = full->radius;
        s.axis = full->counterClockwise ? plane.normal() : plane.normal().reversed();
        // A circle starts on the plane's X axis through its centre.
        s.start = s.center + Translation3D::along(plane.xAxis(), full->radius);
        s.end = s.start;
        const auto tangent = unit(cross(vec(s.axis), minus(vec(s.start), vec(s.center))));
        if (!tangent) {
            return invalid("is a circle BetterCAD cannot measure");
        }
        s.startTangent = *tangent;
        s.endTangent = *tangent;
        s.turn = Angle::fromSi(full->counterClockwise ? kTwoPi : -kTwoPi);
        s.length = Length::fromSi(kTwoPi * full->radius.si());
        return s;
    }
    const std::string_view kind = std::holds_alternative<EllipseSegment2D>(segment) ? "an ellipse" : "a spline";
    return makeError(ErrorCode::InvalidArgument,
                     std::format("makeSweep: {}segment {} is {}; a path is made of lines, arcs and circles", what,
                                 number, kind));
}

/// How far the farthest point of @p loop lies from @p centre, in the plane's
/// own coordinates. Curved segments are bounded by their centre and radius,
/// and a spline by its poles, whose hull contains the curve: never an
/// under-estimate, so a fold check made with it never passes geometry that
/// folds.
double reachFromCentroid(const ProfileLoop& loop, const Point2D& centre) {
    const auto away = [&](const Point2D& p) {
        return std::hypot(p.x.si() - centre.x.si(), p.y.si() - centre.y.si());
    };
    double reach = 0.0;
    for (const ProfileSegment& segment : loop.segments) {
        if (const auto* line = std::get_if<LineSegment2D>(&segment)) {
            reach = std::max({reach, away(line->start), away(line->end)});
        } else if (const auto* arc = std::get_if<ArcSegment2D>(&segment)) {
            const double radius = std::hypot(arc->start.x.si() - arc->center.x.si(),
                                             arc->start.y.si() - arc->center.y.si());
            reach = std::max(reach, away(arc->center) + radius);
        } else if (const auto* circle = std::get_if<CircleSegment2D>(&segment)) {
            reach = std::max(reach, away(circle->center) + circle->radius.si());
        } else if (const auto* ellipse = std::get_if<EllipseSegment2D>(&segment)) {
            const double radiusX = std::hypot(ellipse->xVertex.x.si() - ellipse->center.x.si(),
                                              ellipse->xVertex.y.si() - ellipse->center.y.si());
            reach = std::max(reach, away(ellipse->center) + std::max(radiusX, ellipse->radiusY.si()));
        } else if (const auto* spline = std::get_if<SplineSegment2D>(&segment)) {
            for (const Point2D& pole : spline->poles) {
                reach = std::max(reach, away(pole));
            }
        }
    }
    return reach;
}

/// The frame's reference direction carried from @p from at @p fromPoint with
/// tangent @p fromTangent to @p toPoint with tangent @p toTangent, by the
/// double-reflection method: two reflections that take one tangent to the
/// other and carry the reference with them. It adds no rotation about the
/// tangent, which is what a rotation-minimizing frame means.
Direction3D transport(const Direction3D& from, const Point3D& fromPoint, const Direction3D& fromTangent,
                      const Point3D& toPoint, const Direction3D& toTangent) {
    Vec3 r = vec(from);
    const Vec3 step = minus(vec(toPoint), vec(fromPoint));
    const double stepLength = dot(step, step);
    Vec3 tangent = vec(fromTangent);
    if (stepLength > kLengthTolerance * kLengthTolerance) {
        // Reflect in the plane through both points.
        const double c1 = 2.0 / stepLength;
        r = minus(r, scaled(step, c1 * dot(step, r)));
        tangent = minus(tangent, scaled(step, c1 * dot(step, tangent)));
    }
    // Reflect again so that the carried tangent becomes the new one.
    const Vec3 second = minus(vec(toTangent), tangent);
    const double secondLength = dot(second, second);
    if (secondLength > kAngularTolerance * kAngularTolerance) {
        r = minus(r, scaled(second, 2.0 / secondLength * dot(second, r)));
    }
    // Re-orthogonalize against the tangent, so rounding cannot drift.
    const Vec3 t = vec(toTangent);
    r = minus(r, scaled(t, dot(t, r)));
    if (const auto carried = unit(r)) {
        return *carried;
    }
    return from;
}

} // namespace

std::vector<PathSample> samplePath(const std::vector<SpatialSegment>& segments, const Direction3D& start,
                                   std::size_t count) {
    std::vector<PathSample> samples;
    if (segments.empty() || count < 2) {
        return samples;
    }
    double total = 0.0;
    for (const SpatialSegment& segment : segments) {
        total += segment.length.si();
    }
    if (!(total > 0.0)) {
        return samples;
    }
    samples.reserve(count);

    // Walk the path at equal arc length, carrying the reference direction.
    Direction3D reference = start;
    Point3D previousPoint = segments.front().start;
    Direction3D previousTangent = segments.front().startTangent;
    std::size_t index = 0;
    double consumed = 0.0; // length of the segments before `index`
    for (std::size_t i = 0; i < count; ++i) {
        const double u = static_cast<double>(i) / static_cast<double>(count - 1);
        double distance = u * total;
        while (index + 1 < segments.size() && distance > consumed + segments[index].length.si()) {
            consumed += segments[index].length.si();
            ++index;
        }
        const SpatialSegment& segment = segments[index];
        const double along = std::clamp(distance - consumed, 0.0, segment.length.si());
        Point3D here;
        Direction3D tangent = segment.startTangent;
        if (segment.straight) {
            here = segment.start + Translation3D::along(segment.startTangent, Length::fromSi(along));
            tangent = segment.startTangent;
        } else {
            const double angle = segment.radius.si() > 0.0 ? along / segment.radius.si() : 0.0;
            const Vec3 axis = vec(segment.axis);
            const Vec3 radial = minus(vec(segment.start), vec(segment.center));
            // Rodrigues about the axis, which is perpendicular to radial.
            const Vec3 turned = plus(scaled(radial, std::cos(angle)), scaled(cross(axis, radial), std::sin(angle)));
            here = point(plus(vec(segment.center), turned));
            tangent = unit(cross(axis, turned)).value_or(segment.startTangent);
        }
        reference = transport(reference, previousPoint, previousTangent, here, tangent);
        previousPoint = here;
        previousTangent = tangent;
        samples.push_back({.u = u, .point = here, .tangent = tangent, .reference = reference});
    }
    return samples;
}

Result<SweptPlan> planPathCurve(const SweptPath& path, std::string_view what) {
    const auto invalid = [&](const std::string& problem) {
        return makeError(ErrorCode::InvalidArgument, std::format("makeSweep: {}{}", what, problem));
    };
    if (path.runs.empty()) {
        return invalid("is empty");
    }
    SweptPlan plan;
    plan.frame = frameOf(path);

    // Every run's segments, measured in model space and numbered across the
    // whole path, as the messages and the face names number them.
    std::size_t number = 0;
    for (std::size_t r = 0; r < path.runs.size(); ++r) {
        const PlanarPath& run = path.runs[r];
        if (!finite(run.plane)) {
            return invalid(std::format("has a run {} whose plane is not finite", r + 1));
        }
        if (run.segments.empty()) {
            return invalid(std::format("has an empty run {}", r + 1));
        }
        for (std::size_t i = 0; i < run.segments.size(); ++i) {
            auto measured = measure(run.segments[i], run.plane, ++number, what);
            if (!measured) {
                return std::unexpected(measured.error());
            }
            measured->run = r;
            measured->inRun = i;
            plan.segments.push_back(*measured);
        }
    }
    const std::size_t count = plan.segments.size();
    for (std::size_t i = 0; i < count; ++i) {
        if (plan.segments[i].circle && count != 1) {
            return invalid(std::format("has a segment {} that is a full circle, which is a closed path on its own "
                                       "and cannot be joined with other segments",
                                       i + 1));
        }
    }

    // Connected, in model space: a run may end where the next begins even
    // though they lie on different planes.
    for (std::size_t i = 0; i + 1 < count; ++i) {
        const double gap = norm(minus(vec(plan.segments[i + 1].start), vec(plan.segments[i].end)));
        if (gap > kLengthTolerance) {
            return invalid(std::format("is not connected: segment {} ends at {}, but segment {} starts at "
                                       "{}, {} away",
                                       i + 1, at(vec(plan.segments[i].end)), i + 2,
                                       at(vec(plan.segments[i + 1].start)), mm(gap)));
        }
    }

    const bool circle = plan.segments.front().circle;
    plan.closed = circle || (count >= 2 && norm(minus(vec(plan.segments.front().start),
                                                      vec(plan.segments.back().end))) <= kLengthTolerance);
    const std::size_t jointCount = circle ? 0 : (plan.closed ? count : count - 1);
    std::vector<double> turns(jointCount, 0.0);
    for (std::size_t j = 0; j < jointCount; ++j) {
        const SpatialSegment& a = plan.segments[j];
        const SpatialSegment& b = plan.segments[(j + 1) % count];
        const Vec3 ta = vec(a.endTangent);
        const Vec3 tb = vec(b.startTangent);
        const double cosine = dot(ta, tb);
        const double sine = norm(cross(ta, tb));
        if (sine <= kAngularTolerance && cosine > 0.0) {
            plan.joints.push_back(PathJoint::Smooth);
            continue;
        }
        const std::size_t first = j + 1;
        const std::size_t second = (j + 1) % count + 1;
        if (!a.straight || !b.straight) {
            return invalid(std::format("has segments {} ({}) and {} ({}) meeting at an angle of {}; only two "
                                       "straight segments may meet at a corner, and an arc must meet its "
                                       "neighbours tangentially",
                                       first, kindName(a), second, kindName(b), degrees(std::atan2(sine, cosine))));
        }
        if (sine <= kAngularTolerance) {
            return invalid(std::format("turns back on itself where segments {} and {} meet", first, second));
        }
        plan.joints.push_back(PathJoint::Corner);
        turns[j] = std::atan2(sine, cosine); // unsigned: the mitre is symmetric
    }
    plan.cornerTurns = turns;
    Length total{};
    for (const SpatialSegment& s : plan.segments) {
        total = total + s.length;
    }
    plan.totalLength = total;
    return plan;
}

Result<SweptPlan> planSweptPath(const PlanarRegion& region, const SweptPath& path) {
    const auto invalid = [](const std::string& problem) {
        return makeError(ErrorCode::InvalidArgument, std::format("makeSweep: {}", problem));
    };
    auto planned = planPathCurve(path, "the path ");
    if (!planned) {
        return std::unexpected(planned.error());
    }
    SweptPlan plan = std::move(*planned);
    const std::size_t count = plan.segments.size();
    const std::size_t jointCount = plan.joints.size();
    const std::vector<double>& turns = plan.cornerTurns;

    // The region sits on the path's start, across it, as for a planar path.
    const SpatialSegment& first = plan.segments.front();
    const Length offset = region.plane.signedDistance(first.start);
    if (!(abs(offset).si() <= kLengthTolerance)) {
        return invalid(std::format("the path must start on the profile's plane, but it starts {} from it",
                                   mm(abs(offset).si())));
    }
    const double tilt = norm(cross(vec(first.startTangent), vec(region.plane.normal())));
    if (tilt > kAngularTolerance) {
        return invalid(std::format("the path must leave the profile's plane at right angles, but it leaves at {} "
                                   "to the plane's normal",
                                   degrees(std::asin(std::min(1.0, tilt)))));
    }

    const Area area = regionArea(region);
    if (!(area.si() > kLengthTolerance * kLengthTolerance)) {
        return invalid("the profile encloses no area");
    }

    // A turning section has no constant offset from the path, so the
    // centroid must ride on it: the section then travels exactly the path's
    // length, whatever the frame does, and the volume is area x length.
    const Point2D centroid = regionCentroid(region);
    const Point3D centroidAt = region.plane.toGlobal(centroid);
    const double away = norm(minus(vec(centroidAt), vec(first.start)));
    if (away > kLengthTolerance) {
        return invalid(std::format("the profile's centroid must lie on the path, but it lies {} from where the path "
                                   "starts: a spatial or twisted sweep turns its section, which has no fixed offset "
                                   "from the path",
                                   mm(away)));
    }

    // The reach: how far the region's farthest point lies from the path,
    // which the centroid rides on, so it is a distance in the region's own
    // plane. A hole is inside the outer loop, so only the outer loop counts.
    const double reach = reachFromCentroid(region.outer, centroid);
    plan.reach = Length::fromSi(reach);

    // Folding: an arc must be wider than the reach, and a straight segment
    // longer than the mitres its corners cut out of it.
    for (std::size_t i = 0; i < count; ++i) {
        const SpatialSegment& s = plan.segments[i];
        if (!s.straight) {
            if (reach >= s.radius.si() - kLengthTolerance) {
                return invalid(std::format("the profile reaches {} from the path towards the centre of path segment "
                                           "{}, {} of radius {}: the swept solid would fold over itself",
                                           mm(reach), i + 1, kindName(s), mm(s.radius.si())));
            }
            continue;
        }
        double used = 0.0;
        const bool hasBefore = i > 0 || plan.closed;
        const std::size_t before = i > 0 ? i - 1 : jointCount - 1;
        if (hasBefore && jointCount > 0 && plan.joints[before] == PathJoint::Corner) {
            used += std::abs(std::tan(turns[before] / 2.0));
        }
        if (i < jointCount && plan.joints[i] == PathJoint::Corner) {
            used += std::abs(std::tan(turns[i] / 2.0));
        }
        const double cut = used * reach;
        if (cut >= s.length.si() - kLengthTolerance) {
            return invalid(std::format("path segment {} is too short for the mitred corners at its ends: it is "
                                       "{} long, but the profile reaches {} from the path, which uses {} of it",
                                       i + 1, mm(s.length.si()), mm(reach), mm(cut)));
        }
    }

    Length total{};
    for (const SpatialSegment& s : plan.segments) {
        total = total + s.length;
    }
    plan.totalLength = total;
    plan.expectedVolume = Volume::fromSi(area.si() * total.si());

    if (!isFinite(path.twist)) {
        return invalid("the twist must be finite");
    }
    return plan;
}

} // namespace bettercad::geometry::detail

namespace bettercad::geometry {

SweptPath asSweptPath(const PlanarPath& path) {
    return SweptPath{.runs = {path}};
}

std::string_view toString(SweepFrame frame) noexcept {
    switch (frame) {
    case SweepFrame::PlanarBinormal:
        return "planar binormal";
    case SweepFrame::RotationMinimizing:
        return "rotation minimizing";
    case SweepFrame::Guided:
        return "guided";
    }
    return "planar binormal";
}

SweepFrame frameOf(const SweptPath& path) {
    if (path.twist != Angle{} || !path.guide.empty()) {
        return SweepFrame::Guided;
    }
    return path.runs.size() <= 1 ? SweepFrame::PlanarBinormal : SweepFrame::RotationMinimizing;
}

} // namespace bettercad::geometry
