#include "core/geometry/LoftMatching.hpp"

#include "core/geometry/ProfileExtent.hpp"

#include <bettercad/core/math/Frame.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>
#include <variant>

namespace bettercad::geometry::detail {

namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;
// A split may not leave a segment shorter than this: the sketch tolerance,
// which is also the kernel's precision.
constexpr double kLengthTolerance = 1e-10; // metres
// Parameters this close are the same corner, relative to the loop's length.
constexpr double kParameterTolerance = 1e-9;

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

Vec2 vec(const Point2D& p) {
    return {p.x.si(), p.y.si()};
}

Point2D point(Vec2 v) {
    return Point2D{Length::fromSi(v.x), Length::fromSi(v.y)};
}

Vec2 minus(Vec2 a, Vec2 b) {
    return {a.x - b.x, a.y - b.y};
}

Vec2 plus(Vec2 a, Vec2 b) {
    return {a.x + b.x, a.y + b.y};
}

Vec2 scaled(Vec2 a, double s) {
    return {a.x * s, a.y * s};
}

double cross(Vec2 a, Vec2 b) {
    return a.x * b.y - a.y * b.x;
}

double norm(Vec2 a) {
    return std::hypot(a.x, a.y);
}

/// The angle of @p from the centre of an arc, in radians.
double angleOf(Vec2 centre, Vec2 p) {
    return std::atan2(p.y - centre.y, p.x - centre.x);
}

Length segmentLength(const ProfileSegment& segment) {
    if (const auto* line = std::get_if<LineSegment2D>(&segment)) {
        return Length::fromSi(norm(minus(vec(line->end), vec(line->start))));
    }
    if (const auto* arc = std::get_if<ArcSegment2D>(&segment)) {
        const double radius = norm(minus(vec(arc->start), vec(arc->center)));
        return Length::fromSi(radius * std::abs(arcSweep(*arc)));
    }
    const auto& circle = std::get<CircleSegment2D>(segment);
    return Length::fromSi(kTwoPi * circle.radius.si());
}

/// The point at fraction @p f along @p segment, and the segment's two parts
/// either side of it.
struct SplitSegment {
    ProfileSegment before;
    ProfileSegment after;
};

SplitSegment splitSegment(const ProfileSegment& segment, double f) {
    if (const auto* line = std::get_if<LineSegment2D>(&segment)) {
        const Vec2 a = vec(line->start);
        const Vec2 b = vec(line->end);
        const Point2D at = point(plus(a, scaled(minus(b, a), f)));
        return {LineSegment2D{line->start, at}, LineSegment2D{at, line->end}};
    }
    const auto& arc = std::get<ArcSegment2D>(segment);
    const Vec2 centre = vec(arc.center);
    const double radius = norm(minus(vec(arc.start), centre));
    const double sweep = arcSweep(arc);
    const double start = angleOf(centre, vec(arc.start));
    const double angle = start + sweep * f;
    const Point2D at = point({centre.x + radius * std::cos(angle), centre.y + radius * std::sin(angle)});
    return {ArcSegment2D{arc.center, arc.start, at, arc.counterClockwise},
            ArcSegment2D{arc.center, at, arc.end, arc.counterClockwise}};
}

/// A full circle as @p n arcs, the first starting on the circle's own start
/// (centre + radius along +X), so that a circle can be matched segment for
/// segment with anything else.
ProfileLoop circleAsArcs(const CircleSegment2D& circle, const std::vector<double>& parameters) {
    const Vec2 centre = vec(circle.center);
    const double radius = circle.radius.si();
    const double sweep = circle.counterClockwise ? kTwoPi : -kTwoPi;
    const auto at = [&](double f) {
        const double angle = sweep * f;
        return point({centre.x + radius * std::cos(angle), centre.y + radius * std::sin(angle)});
    };
    ProfileLoop loop;
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        const double from = parameters[i];
        const double to = i + 1 < parameters.size() ? parameters[i + 1] : 1.0;
        loop.segments.emplace_back(ArcSegment2D{circle.center, at(from), at(to), circle.counterClockwise});
    }
    return loop;
}

/// The mean of a segment over its own parameter [0, 1].
Vec2 meanOf(const ProfileSegment& segment) {
    if (const auto* line = std::get_if<LineSegment2D>(&segment)) {
        return scaled(plus(vec(line->start), vec(line->end)), 0.5);
    }
    const auto& arc = std::get<ArcSegment2D>(segment);
    const Vec2 centre = vec(arc.center);
    const double radius = norm(minus(vec(arc.start), centre));
    const double sweep = arcSweep(arc);
    const double start = angleOf(centre, vec(arc.start));
    if (std::abs(sweep) < 1e-12) {
        return vec(arc.start);
    }
    // (1/phi) times the integral of (cos, sin) from start to start + phi.
    return {centre.x + radius * (std::sin(start + sweep) - std::sin(start)) / sweep,
            centre.y + radius * (-std::cos(start + sweep) + std::cos(start)) / sweep};
}

Vec2 startOf(const ProfileSegment& segment) {
    if (const auto* line = std::get_if<LineSegment2D>(&segment)) {
        return vec(line->start);
    }
    if (const auto* arc = std::get_if<ArcSegment2D>(&segment)) {
        return vec(arc->start);
    }
    // A circle starts on its plane's X axis through its centre.
    const auto& circle = std::get<CircleSegment2D>(segment);
    return {circle.center.x.si() + circle.radius.si(), circle.center.y.si()};
}

/// Whether @p loop is one full circle, which has no corners of its own.
bool isSingleCircle(const ProfileLoop& loop) {
    return loop.segments.size() == 1 && std::holds_alternative<CircleSegment2D>(loop.segments.front());
}

Vec2 endOf(const ProfileSegment& segment) {
    if (const auto* line = std::get_if<LineSegment2D>(&segment)) {
        return vec(line->end);
    }
    return vec(std::get<ArcSegment2D>(segment).end);
}

/// The integral over [0, 1] of q(s) x p'(s) for one matched pair, in closed
/// form. Summed over the pairs this is the mixed area M.
double mixedTerm(const ProfileSegment& p, const ProfileSegment& q) {
    const Vec2 chord = minus(endOf(p), startOf(p)); // the integral of p'
    if (const auto* line = std::get_if<LineSegment2D>(&p)) {
        // p' is constant: the integral of q x p' is (mean of q) x p'.
        (void)line;
        return cross(meanOf(q), chord);
    }
    const auto& arc = std::get<ArcSegment2D>(p);
    const Vec2 centreP = vec(arc.center);
    const double radiusP = norm(minus(vec(arc.start), centreP));
    const double sweepP = arcSweep(arc);
    const double startP = angleOf(centreP, vec(arc.start));
    if (const auto* other = std::get_if<LineSegment2D>(&q)) {
        // q(s) = q0 + s e. The integral of q x p' is
        // q0 x chord + e x (p(1) - mean of p).
        const Vec2 q0 = vec(other->start);
        const Vec2 e = minus(vec(other->end), q0);
        const Vec2 last = endOf(p);
        return cross(q0, chord) + cross(e, minus(last, meanOf(p)));
    }
    // Both arcs: the integral splits into the centre's part and a cosine.
    const auto& arcQ = std::get<ArcSegment2D>(q);
    const Vec2 centreQ = vec(arcQ.center);
    const double radiusQ = norm(minus(vec(arcQ.start), centreQ));
    const double sweepQ = arcSweep(arcQ);
    const double startQ = angleOf(centreQ, vec(arcQ.start));
    const double centrePart = cross(centreQ, chord);
    const double delta = startQ - startP;
    const double rate = sweepQ - sweepP;
    const double trig = std::abs(rate) < 1e-12
                            ? std::cos(delta)
                            : (std::sin(delta + rate) - std::sin(delta)) / rate;
    return centrePart + radiusP * radiusQ * sweepP * trig;
}

} // namespace

Length loopLength(const ProfileLoop& loop) {
    Length total{};
    for (const ProfileSegment& segment : loop.segments) {
        total = total + segmentLength(segment);
    }
    return total;
}

std::vector<double> cornerParameters(const ProfileLoop& loop) {
    const double total = loopLength(loop).si();
    std::vector<double> breaks;
    if (!(total > 0.0)) {
        return breaks;
    }
    double walked = 0.0;
    for (const ProfileSegment& segment : loop.segments) {
        breaks.push_back(walked / total);
        walked += segmentLength(segment).si();
    }
    return breaks;
}

Result<ProfileLoop> splitAt(const ProfileLoop& loop, const std::vector<double>& parameters) {
    const auto invalid = [](const std::string& problem) {
        return makeError(ErrorCode::InvalidArgument, std::format("makeLoft: {}", problem));
    };
    if (parameters.empty()) {
        return invalid("a section cannot be split at no parameters");
    }
    const double total = loopLength(loop).si();
    if (!(total > 0.0)) {
        return invalid("a section of no length cannot be split");
    }
    std::vector<double> wanted = parameters;
    std::ranges::sort(wanted);
    wanted.erase(std::ranges::unique(wanted, [](double a, double b) {
                     return std::abs(a - b) <= kParameterTolerance;
                 }).begin(),
                 wanted.end());

    // A circle has no corners of its own: it becomes one arc per parameter.
    if (loop.segments.size() == 1 && std::holds_alternative<CircleSegment2D>(loop.segments.front())) {
        if (wanted.size() < 2) {
            return invalid("a circular section must be split at two parameters or more to match another shape");
        }
        return circleAsArcs(std::get<CircleSegment2D>(loop.segments.front()), wanted);
    }

    ProfileLoop result;
    const std::vector<double> corners = cornerParameters(loop);
    std::size_t next = 0;
    for (std::size_t i = 0; i < loop.segments.size(); ++i) {
        const double from = corners[i];
        const double to = i + 1 < corners.size() ? corners[i + 1] : 1.0;
        const double span = to - from;
        // Every wanted parameter strictly inside this segment, in order.
        std::vector<double> inside;
        while (next < wanted.size() && wanted[next] < to - kParameterTolerance) {
            if (wanted[next] > from + kParameterTolerance) {
                inside.push_back((wanted[next] - from) / span);
            }
            ++next;
        }
        ProfileSegment remaining = loop.segments[i];
        double consumed = 0.0;
        for (const double f : inside) {
            // The fraction of what is left of the segment.
            const double local = (f - consumed) / (1.0 - consumed);
            if (!(local > 0.0) || !(local < 1.0)) {
                continue;
            }
            const SplitSegment parts = splitSegment(remaining, local);
            if (segmentLength(parts.before).si() <= kLengthTolerance) {
                return invalid("a section's split would leave a segment of no length");
            }
            result.segments.push_back(parts.before);
            remaining = parts.after;
            consumed = f;
        }
        if (segmentLength(remaining).si() <= kLengthTolerance) {
            return invalid("a section's split would leave a segment of no length");
        }
        result.segments.push_back(remaining);
    }
    return result;
}

std::vector<double> alignmentCandidates(const ProfileLoop& first, const ProfileLoop& second) {
    std::vector<double> offsets;
    const std::vector<double> firstCorners = cornerParameters(first);
    const std::vector<double> secondCorners = cornerParameters(second);
    // Every corner of one against every corner of the other: the offset
    // that makes them correspond. This is the cyclic rotation search
    // P11-FEAT-009 does for equal shapes, written as a parameter shift.
    for (const double p : firstCorners) {
        for (const double q : secondCorners) {
            double offset = p - q;
            offset -= std::floor(offset);
            offsets.push_back(offset);
        }
    }
    // A circle has no corners of its own, so the offsets above cannot place
    // its seam: they only shift it by the other loop's own symmetries.
    // Align by angle about the centroid instead, which is what a circle has.
    const bool firstIsCircle = isSingleCircle(first);
    const bool secondIsCircle = isSingleCircle(second);
    if (secondIsCircle && !firstIsCircle) {
        const Point2D centre = regionCentroid(PlanarRegion{Frame3D::xy(), first, {}});
        for (std::size_t i = 0; i < first.segments.size(); ++i) {
            const double angle = angleOf(vec(centre), startOf(first.segments[i]));
            double turn = angle / kTwoPi;
            turn -= std::floor(turn);
            double offset = firstCorners[i] - turn;
            offset -= std::floor(offset);
            offsets.push_back(offset);
        }
    } else if (firstIsCircle && !secondIsCircle) {
        const Point2D centre = regionCentroid(PlanarRegion{Frame3D::xy(), second, {}});
        for (std::size_t i = 0; i < second.segments.size(); ++i) {
            const double angle = angleOf(vec(centre), startOf(second.segments[i]));
            double turn = angle / kTwoPi;
            turn -= std::floor(turn);
            double offset = turn - secondCorners[i];
            offset -= std::floor(offset);
            offsets.push_back(offset);
        }
    }
    std::ranges::sort(offsets);
    offsets.erase(std::ranges::unique(offsets, [](double a, double b) {
                      return std::abs(a - b) <= kParameterTolerance;
                  }).begin(),
                  offsets.end());
    return offsets;
}

Result<std::vector<ProfileLoop>> matchChainByArcLength(std::span<const ProfileLoop> loops,
                                                      const std::vector<double>& shifts) {
    if (loops.size() < 2 || shifts.size() != loops.size()) {
        return makeError(ErrorCode::Internal, "makeLoft: a chain of sections needs one alignment each");
    }
    // Every corner of every section, in the chain's own parameter.
    std::vector<double> breaks;
    for (std::size_t i = 0; i < loops.size(); ++i) {
        const std::vector<double> corners = cornerParameters(loops[i]);
        if (corners.empty()) {
            return makeError(ErrorCode::InvalidArgument, "makeLoft: a section has no length to match along");
        }
        for (const double c : corners) {
            double t = c - shifts[i];
            t -= std::floor(t);
            breaks.push_back(t);
        }
    }
    std::ranges::sort(breaks);
    breaks.erase(std::ranges::unique(breaks, [](double x, double y) {
                     return std::abs(x - y) <= kParameterTolerance;
                 }).begin(),
                 breaks.end());
    if (breaks.empty() || breaks.front() > kParameterTolerance) {
        breaks.insert(breaks.begin(), 0.0);
    }

    std::vector<ProfileLoop> matched;
    matched.reserve(loops.size());
    for (std::size_t i = 0; i < loops.size(); ++i) {
        // The chain's breaks in this section's own parameter, in the chain's
        // order, so own.front() is where the chain's parameter 0 falls on it.
        std::vector<double> own;
        own.reserve(breaks.size());
        for (const double t : breaks) {
            double p = t + shifts[i];
            p -= std::floor(p);
            own.push_back(p);
        }
        auto split = splitAt(loops[i], own);
        if (!split) {
            return std::unexpected(split.error());
        }
        if (split->segments.size() != breaks.size()) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("makeLoft: the sections cannot be matched: one splits into {} segments "
                                         "where the sections have {} corners between them",
                                         split->segments.size(), breaks.size()));
        }
        // splitAt() sorts its parameters, so the segments start at this
        // section's own parameter 0; rotate them so that segment k of every
        // section spans the same stretch of the chain.
        const double start = own.front();
        std::vector<double> sorted = own;
        std::ranges::sort(sorted);
        const auto found = std::ranges::find_if(
            sorted, [&](double p) { return std::abs(p - start) <= kParameterTolerance; });
        const std::size_t shift = static_cast<std::size_t>(found - sorted.begin());
        ProfileLoop aligned;
        const std::size_t size = split->segments.size();
        aligned.segments.reserve(size);
        for (std::size_t k = 0; k < size; ++k) {
            aligned.segments.push_back(split->segments[(k + shift) % size]);
        }
        matched.push_back(std::move(aligned));
    }
    return matched;
}

Result<MatchedPair> matchByArcLength(const ProfileLoop& first, const ProfileLoop& second, double offset) {
    // A pair is the two-section case of a chain: the first loop anchors the
    // chain's parameter, and the second is shifted back by the offset.
    const std::array<ProfileLoop, 2> pair{first, second};
    auto matched = matchChainByArcLength(pair, {0.0, -offset});
    if (!matched) {
        return std::unexpected(matched.error());
    }
    return MatchedPair{.first = std::move((*matched)[0]), .second = std::move((*matched)[1])};
}

Result<Area> mixedArea(const ProfileLoop& first, const ProfileLoop& second) {
    if (first.segments.size() != second.segments.size()) {
        return makeError(ErrorCode::InvalidArgument, "makeLoft: the mixed area needs matched sections");
    }
    double total = 0.0;
    for (std::size_t i = 0; i < first.segments.size(); ++i) {
        if (std::holds_alternative<CircleSegment2D>(first.segments[i]) ||
            std::holds_alternative<CircleSegment2D>(second.segments[i])) {
            return makeError(ErrorCode::InvalidArgument,
                             "makeLoft: the mixed area needs sections of lines and arcs");
        }
        total += mixedTerm(first.segments[i], second.segments[i]);
    }
    // M is the closed integral of q x dp itself, not half of it: with q = p
    // it must come to twice the loop's area, so that A(t) stays A for a
    // section lofted to itself.
    return Area::fromSi(total);
}

} // namespace bettercad::geometry::detail
