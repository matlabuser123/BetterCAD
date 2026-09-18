#include "core/geometry/LoftPlan.hpp"

#include "core/geometry/LoftMatching.hpp"
#include "core/geometry/ProfileExtent.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <variant>

namespace bettercad::geometry::detail {

namespace {

// Sections closer than this along the loft lie on one plane (the sketch
// tolerance, the kernel's precision).
constexpr double kLengthTolerance = 1e-10; // metres
// Planes are parallel when the sine of the angle between their normals is
// below this; matched arcs turn by the same angle within this (radians).
// The tolerance of edge and face matching and of revolution axes.
constexpr double kAngularTolerance = 1e-9;
// Two ways of matching sections whose costs differ by less than this, relative
// to the sections' size, are a tie: rounding cannot decide between them.
constexpr double kTieTolerance = 1e-9;
// A cross-section keeping less than this fraction of the larger section's
// area has vanished: the loft folds.
constexpr double kFoldTolerance = 1e-9;

double tidy(double value) {
    return value == 0.0 ? 0.0 : value;
}

// Messages show 6 significant digits: computed values are rounded.
std::string mm(double metres) {
    return std::format("{:.6g} mm", tidy(metres * 1e3));
}

std::string degrees(double radians) {
    return std::format("{:.6g} deg", tidy(Angle::fromSi(radians).in(units::deg)));
}

bool finite(const Point2D& p) {
    return isFinite(p.x) && isFinite(p.y);
}

bool finite(const Point3D& p) {
    return isFinite(p.x) && isFinite(p.y) && isFinite(p.z);
}

bool finite(const ProfileLoop& loop) {
    return std::ranges::all_of(loop.segments, [](const ProfileSegment& segment) {
        if (const auto* line = std::get_if<LineSegment2D>(&segment)) {
            return finite(line->start) && finite(line->end);
        }
        if (const auto* arc = std::get_if<ArcSegment2D>(&segment)) {
            return finite(arc->center) && finite(arc->start) && finite(arc->end);
        }
        const auto& circle = std::get<CircleSegment2D>(segment);
        return finite(circle.center) && isFinite(circle.radius);
    });
}

bool isCircle(const ProfileLoop& loop) {
    return loop.segments.size() == 1 && std::holds_alternative<CircleSegment2D>(loop.segments.front());
}

Point2D startPointOf(const ProfileSegment& segment) {
    if (const auto* line = std::get_if<LineSegment2D>(&segment)) {
        return line->start;
    }
    return std::get<ArcSegment2D>(segment).start;
}

/// "a circle", "4 lines", "2 lines and 2 arcs", ...
std::string shapeOf(const ProfileLoop& loop) {
    if (isCircle(loop)) {
        return "a circle";
    }
    const auto lines = static_cast<std::size_t>(std::ranges::count_if(
        loop.segments, [](const ProfileSegment& s) { return std::holds_alternative<LineSegment2D>(s); }));
    const std::size_t arcs = loop.segments.size() - lines;
    const auto count = [](std::size_t n, std::string_view one, std::string_view many) {
        return std::format("{} {}", n, n == 1 ? one : many);
    };
    if (arcs == 0) {
        return count(lines, "line", "lines");
    }
    if (lines == 0) {
        return count(arcs, "arc", "arcs");
    }
    return std::format("{} and {}", count(lines, "line", "lines"), count(arcs, "arc", "arcs"));
}

/// @p loop in the coordinates of the parallel plane @p to: points keep their
/// positions, and the senses of arcs and circles flip if the normals are
/// opposite.
ProfileLoop mapLoop(const ProfileLoop& loop, const Frame3D& from, const Frame3D& to) {
    const bool flip = from.normal().dot(to.normal()) < 0.0;
    const auto map = [&](const Point2D& p) { return to.toLocal(from.toGlobal(p)); };
    ProfileLoop result;
    for (const ProfileSegment& segment : loop.segments) {
        if (const auto* line = std::get_if<LineSegment2D>(&segment)) {
            result.segments.emplace_back(LineSegment2D{map(line->start), map(line->end)});
        } else if (const auto* arc = std::get_if<ArcSegment2D>(&segment)) {
            result.segments.emplace_back(
                ArcSegment2D{map(arc->center), map(arc->start), map(arc->end), arc->counterClockwise != flip});
        } else {
            const auto& circle = std::get<CircleSegment2D>(segment);
            result.segments.emplace_back(
                CircleSegment2D{map(circle.center), circle.radius, circle.counterClockwise != flip});
        }
    }
    return result;
}

/// The loop starting at its segment @p first.
ProfileLoop startingAt(const ProfileLoop& loop, std::size_t first) {
    ProfileLoop result;
    const std::size_t count = loop.segments.size();
    for (std::size_t i = 0; i < count; ++i) {
        result.segments.push_back(loop.segments[(first + i) % count]);
    }
    return result;
}

/// Whether every matched pair of @p a and @p b averages to a segment of
/// its own kind: both lines, both circles, or arcs of the same sweep. Then
/// the halfway section is a loop and its area can be taken directly.
bool averagesToSegments(const ProfileLoop& a, const ProfileLoop& b) {
    if (a.segments.size() != b.segments.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.segments.size(); ++i) {
        if (a.segments[i].index() != b.segments[i].index()) {
            return false;
        }
        if (const auto* arc = std::get_if<ArcSegment2D>(&a.segments[i])) {
            if (std::abs(arcSweep(*arc) - arcSweep(std::get<ArcSegment2D>(b.segments[i]))) > kAngularTolerance) {
                return false;
            }
        }
    }
    return true;
}

/// The cross-section halfway between two matched loops: every point is the
/// average of matching points. Lines average to lines, arcs of equal sweep to
/// arcs (a vanishing one to a point) and circles to circles.
ProfileLoop middle(const ProfileLoop& a, const ProfileLoop& b) {
    ProfileLoop result;
    for (std::size_t i = 0; i < a.segments.size(); ++i) {
        const ProfileSegment& p = a.segments[i];
        const ProfileSegment& q = b.segments[i];
        if (const auto* line = std::get_if<LineSegment2D>(&p)) {
            const auto& other = std::get<LineSegment2D>(q);
            result.segments.emplace_back(
                LineSegment2D{midpoint(line->start, other.start), midpoint(line->end, other.end)});
        } else if (const auto* arc = std::get_if<ArcSegment2D>(&p)) {
            const auto& other = std::get<ArcSegment2D>(q);
            const Point2D center = midpoint(arc->center, other.center);
            const Point2D start = midpoint(arc->start, other.start);
            const Point2D end = midpoint(arc->end, other.end);
            if (distance(center, start).si() <= kLengthTolerance) {
                result.segments.emplace_back(LineSegment2D{start, end});
            } else {
                result.segments.emplace_back(ArcSegment2D{center, start, end, arc->counterClockwise});
            }
        } else {
            const auto& circle = std::get<CircleSegment2D>(p);
            const auto& other = std::get<CircleSegment2D>(q);
            result.segments.emplace_back(CircleSegment2D{midpoint(circle.center, other.center),
                                                         (circle.radius + other.radius) / 2.0,
                                                         circle.counterClockwise});
        }
    }
    return result;
}

/// The area of the cross-section halfway between two matched loops.
///
/// Where every pair averages to a segment of its own kind, the halfway
/// section is built and measured, exactly as P11-FEAT-009 does, so those
/// lofts are unchanged. Where it does not -- a line matched to an arc, or
/// arcs of different sweeps, which matching by arc length produces
/// (P12-LOFT-001) -- the halfway area is taken from the closed form
///
///     A(1/2) = (A_a + M + A_b) / 4,   M the mixed area,
///
/// which is exact for every pair of lines and arcs and needs no sampling.
Result<Area> areaMidway(const ProfileLoop& a, const ProfileLoop& b) {
    if (averagesToSegments(a, b)) {
        return signedArea(middle(a, b));
    }
    auto mixed = mixedArea(a, b);
    if (!mixed) {
        return std::unexpected(mixed.error());
    }
    return Area::fromSi(0.25 * (signedArea(a).si() + mixed->si() + signedArea(b).si()));
}

double squaredDistance(const Point2D& p, const Point2D& p0, const Point2D& q, const Point2D& q0) {
    const double dx = ((p.x - p0.x) - (q.x - q0.x)).si();
    const double dy = ((p.y - p0.y) - (q.y - q0.y)).si();
    return dx * dx + dy * dy;
}

/// The rotations of @p loop that put the same kinds of segment, arcs of
/// equal sweep, against those of @p previous. Empty when the two sections
/// are not the same shape, which is when arc-length matching takes over.
/// Rotating a loop does not change this, so it may be asked of the sections
/// as they were read.
std::vector<std::size_t> directRotations(const ProfileLoop& previous, const ProfileLoop& loop) {
    const std::size_t size = previous.segments.size();
    if (loop.segments.size() != size) {
        return {};
    }
    std::vector<std::size_t> starts;
    for (std::size_t s = 0; s < size; ++s) {
        bool same = true;
        for (std::size_t k = 0; k < size && same; ++k) {
            const ProfileSegment& a = previous.segments[k];
            const ProfileSegment& b = loop.segments[(k + s) % size];
            if (a.index() != b.index()) {
                same = false;
            } else if (const auto* arc = std::get_if<ArcSegment2D>(&a)) {
                same = std::abs(arcSweep(*arc) - arcSweep(std::get<ArcSegment2D>(b))) <= kAngularTolerance;
            }
        }
        if (same) {
            starts.push_back(s);
        }
    }
    return starts;
}

/// Where to align @p loop against @p previous when they are not the same
/// shape (P12-LOFT-001), as a normalized arc length of @p loop: the offset
/// with the least twist, scored exactly as P11-FEAT-009 scores the starts of
/// equal shapes, with ties going to the first candidate so that rounding
/// cannot flip a match.
Result<double> leastTwistOffset(const ProfileLoop& previous, const ProfileLoop& loop, std::size_t index) {
    const std::vector<double> offsets = alignmentCandidates(previous, loop);
    if (offsets.empty()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("makeLoft: sections {} and {} cannot be matched: one of them has no length",
                                     index, index + 1));
    }
    const Point2D c0 = regionCentroid(PlanarRegion{Frame3D::xy(), previous, {}});
    const Point2D c1 = regionCentroid(PlanarRegion{Frame3D::xy(), loop, {}});
    std::optional<double> best;
    double bestCost = 0.0;
    double scale = 0.0;
    std::optional<Error> lastError;
    for (const double offset : offsets) {
        auto matched = matchByArcLength(previous, loop, offset);
        if (!matched) {
            lastError = matched.error();
            continue;
        }
        double cost = 0.0;
        double size = 0.0;
        for (std::size_t k = 0; k < matched->first.segments.size(); ++k) {
            const Point2D a = startPointOf(matched->first.segments[k]);
            const Point2D b = startPointOf(matched->second.segments[k]);
            cost += squaredDistance(b, c1, a, c0);
            size += squaredDistance(a, c0, Point2D{}, Point2D{});
            size += squaredDistance(b, c1, Point2D{}, Point2D{});
        }
        if (!best || cost < bestCost - kTieTolerance * std::max(scale, size)) {
            best = offset;
            bestCost = cost;
            scale = size;
        }
    }
    if (!best) {
        // Every alignment failed: say what the sections are, as the equal-
        // shape path does, and why the last attempt could not be made.
        return makeError(ErrorCode::InvalidArgument,
                         std::format("makeLoft: sections {} and {} cannot be matched: section {} is {} and "
                                     "section {} is {}{}",
                                     index, index + 1, index, shapeOf(previous), index + 1, shapeOf(loop),
                                     lastError ? std::format(" ({})", lastError->message) : std::string{}));
    }
    return *best;
}

/// Every section of a chain matched by arc length (P12-LOFT-001): each
/// consecutive pair's alignment is chosen first, from the sections as they
/// were read, and only then is the whole chain split at the union of their
/// corners. A section in the middle is therefore split once, for both of the
/// pairs it belongs to.
Result<std::vector<ProfileLoop>> matchChain(const std::vector<ProfileLoop>& loops) {
    // shifts[i] is the parameter of section i that the chain's parameter 0
    // falls on; the first section anchors it.
    std::vector<double> shifts(loops.size(), 0.0);
    for (std::size_t i = 1; i < loops.size(); ++i) {
        auto offset = leastTwistOffset(loops[i - 1], loops[i], i);
        if (!offset) {
            return std::unexpected(offset.error());
        }
        double shift = shifts[i - 1] - *offset;
        shift -= std::floor(shift);
        shifts[i] = shift;
    }
    return matchChainByArcLength(loops, shifts);
}

/// A matched plan's expected volume, and the check that it does not fold.
///
/// Between consecutive sections the cross-section's area is quadratic in the
/// height: A(t) = A0 (1 - t)(1 - 2t) + 4 Am t (1 - t) + A1 t (2t - 1). It
/// must stay positive, or the loft folds over itself; its integral is the
/// volume the kernel's result is then checked against.
///
/// Both matchings end here: the sections are matched by then, and how they
/// were matched makes no difference to the arithmetic.
Result<LoftPlan> finish(LoftPlan plan, const std::vector<Area>& areas, const std::vector<double>& heights) {
    const std::size_t count = plan.sections.size();
    double volume = 0.0;
    for (std::size_t i = 0; i + 1 < count; ++i) {
        const double a0 = areas[i].si();
        const double a1 = areas[i + 1].si();
        auto midway = areaMidway(plan.sections[i].outer, plan.sections[i + 1].outer);
        if (!midway) {
            return std::unexpected(midway.error());
        }
        const double am = midway->si();
        const double qa = 2.0 * a0 - 4.0 * am + 2.0 * a1;
        const double qb = -3.0 * a0 + 4.0 * am - a1;
        double least = std::min(a0, a1);
        double at = a0 <= a1 ? 0.0 : 1.0;
        if (qa > 0.0) {
            const double t = -qb / (2.0 * qa);
            if (t > 0.0 && t < 1.0 && a0 - qb * qb / (4.0 * qa) < least) {
                least = a0 - qb * qb / (4.0 * qa);
                at = t;
            }
        }
        if (!(least > kFoldTolerance * std::max(a0, a1))) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("makeLoft: the loft between sections {} and {} folds over itself: its "
                                         "cross-section would lose all its area {:.3g}% of the way from section {} "
                                         "to section {}",
                                         i + 1, i + 2, 100.0 * at, i + 1, i + 2));
        }
        volume += (heights[i + 1] - heights[i]) / 6.0 * (a0 + 4.0 * am + a1);
    }
    plan.expectedVolume = Volume::fromSi(volume);
    return plan;
}

} // namespace

Result<LoftPlan> planLoft(std::span<const PlanarRegion> sections) {
    const auto invalid = [](const std::string& problem) {
        return makeError(ErrorCode::InvalidArgument, std::format("makeLoft: {}", problem));
    };
    const std::size_t count = sections.size();
    if (count < 2) {
        return invalid(std::format("a loft needs at least two sections, got {}", count));
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (!sections[i].holes.empty()) {
            return invalid(std::format("section {} has a hole: loft sections must be single closed loops", i + 1));
        }
        for (const ProfileSegment& segment : sections[i].outer.segments) {
            if (std::holds_alternative<EllipseSegment2D>(segment) || std::holds_alternative<SplineSegment2D>(segment)) {
                return invalid(std::format("section {} has {}; loft sections are made of lines, arcs and circles",
                                           i + 1,
                                           std::holds_alternative<EllipseSegment2D>(segment) ? "an ellipse"
                                                                                             : "a spline"));
            }
        }
        if (!finite(sections[i].plane.origin()) || !finite(sections[i].outer)) {
            return invalid(std::format("section {} is not finite", i + 1));
        }
    }

    // The loft runs along the first section's normal, towards the second.
    const Frame3D& first = sections[0].plane;
    const Direction3D direction = first.signedDistance(sections[1].plane.origin()) < Length{}
                                      ? first.normal().reversed()
                                      : first.normal();
    for (std::size_t i = 1; i < count; ++i) {
        const Direction3D& n = sections[i].plane.normal();
        const double sx = n.y() * direction.z() - n.z() * direction.y();
        const double sy = n.z() * direction.x() - n.x() * direction.z();
        const double sz = n.x() * direction.y() - n.y() * direction.x();
        const double sine = std::sqrt(sx * sx + sy * sy + sz * sz);
        if (sine > kAngularTolerance) {
            return invalid(std::format("section {} is not parallel to section 1: their planes are {} apart; only "
                                       "sections on parallel planes can be lofted",
                                       i + 1, degrees(std::asin(std::min(1.0, sine)))));
        }
    }
    auto common = Frame3D::create(first.origin(), direction, first.xAxis());
    if (!common) {
        return std::unexpected(common.error());
    }

    // Each section's place along the loft; they must follow one another.
    std::vector<double> heights(count, 0.0);
    for (std::size_t i = 1; i < count; ++i) {
        heights[i] = common->signedDistance(sections[i].plane.origin()).si();
        const double gap = heights[i] - heights[i - 1];
        if (std::abs(gap) <= kLengthTolerance) {
            return invalid(std::format("sections {} and {} lie on the same plane: a loft needs its sections apart", i,
                                       i + 1));
        }
        if (gap < 0.0) {
            return invalid(std::format("section {} lies {} behind section {} along the loft: the sections must be "
                                       "listed in the order they follow one another",
                                       i + 1, mm(-gap), i));
        }
    }

    // Every section in a plane with the loft direction as normal and the
    // first section's X axis, counter-clockwise about the loft direction.
    LoftPlan plan;
    std::vector<ProfileLoop> loops;
    std::vector<Area> areas;
    for (std::size_t i = 0; i < count; ++i) {
        const Point3D& o = first.origin();
        const Point3D origin{o.x + Length::fromSi(direction.x() * heights[i]),
                             o.y + Length::fromSi(direction.y() * heights[i]),
                             o.z + Length::fromSi(direction.z() * heights[i])};
        auto frame = Frame3D::create(origin, direction, first.xAxis());
        if (!frame) {
            return std::unexpected(frame.error());
        }
        ProfileLoop loop = mapLoop(sections[i].outer, sections[i].plane, *frame);
        if (signedArea(loop) < Area{}) {
            loop = reversed(loop);
        }
        const Area area = signedArea(loop);
        if (!(area.si() > kLengthTolerance * kLengthTolerance)) {
            return invalid(std::format("section {} encloses no area", i + 1));
        }
        plan.sections.push_back(PlanarRegion{*frame, {}, {}});
        loops.push_back(std::move(loop));
        areas.push_back(area);
    }

    // Match every section to the one before. Sections of the same shape --
    // the same lines and arcs in the same
    // cyclic order, arcs of equal sweep -- are matched by rotation, exactly
    // as P11-FEAT-009 does, so every loft that built before this milestone
    // builds the same solid. Anything else is matched by arc length
    // (P12-LOFT-001).
    //
    // One such pair pulls in the whole chain. A section in the middle
    // belongs to two pairs; splitting it once per pair would leave the two
    // disagreeing about how many segments it has, so the kernel would be
    // handed sections with different numbers of edges and the first pair's
    // correspondence would be lost silently. The chain is therefore matched
    // in one go, or not at all.
    bool byArcLength = false;
    for (std::size_t i = 1; i < count && !byArcLength; ++i) {
        byArcLength = directRotations(loops[i - 1], loops[i]).empty();
    }
    if (byArcLength) {
        auto matched = matchChain(loops);
        if (!matched) {
            return std::unexpected(matched.error());
        }
        for (std::size_t i = 0; i < count; ++i) {
            plan.sections[i].outer = std::move((*matched)[i]);
        }
        return finish(std::move(plan), areas, heights);
    }

    // Every pair the same shape: each section is matched to the one before
    // by rotation, starting where its corners lie nearest the previous
    // section's, relative to each section's centroid.
    plan.sections[0].outer = loops[0];
    for (std::size_t i = 1; i < count; ++i) {
        const ProfileLoop& previous = plan.sections[i - 1].outer;
        const ProfileLoop& loop = loops[i];
        if (isCircle(previous) && isCircle(loop)) {
            plan.sections[i].outer = loop;
            continue;
        }
        const std::size_t size = previous.segments.size();
        // Rotating a section does not change whether two shapes are the
        // same, so this cannot be empty here; the guard is for the day
        // someone changes one of the two tests and not the other.
        const std::vector<std::size_t> starts = directRotations(previous, loop);
        if (starts.empty()) {
            return makeError(ErrorCode::Internal,
                             std::format("makeLoft: sections {} and {} were taken for the same shape but cannot be "
                                         "matched by rotation",
                                         i, i + 1));
        }
        const Point2D c0 = regionCentroid(PlanarRegion{Frame3D::xy(), previous, {}});
        const Point2D c1 = regionCentroid(PlanarRegion{Frame3D::xy(), loop, {}});
        double scale = 0.0;
        for (std::size_t k = 0; k < size; ++k) {
            scale += squaredDistance(startPointOf(previous.segments[k]), c0, Point2D{}, Point2D{});
            scale += squaredDistance(startPointOf(loop.segments[k]), c1, Point2D{}, Point2D{});
        }
        std::vector<double> costs;
        for (const std::size_t s : starts) {
            double cost = 0.0;
            for (std::size_t k = 0; k < size; ++k) {
                const Point2D a = startPointOf(previous.segments[k]);
                const Point2D b = startPointOf(loop.segments[(k + s) % size]);
                cost += squaredDistance(b, c1, a, c0);
            }
            costs.push_back(cost);
        }
        const double best = *std::ranges::min_element(costs);
        std::size_t chosen = starts.front();
        for (std::size_t k = 0; k < starts.size(); ++k) {
            if (costs[k] <= best + kTieTolerance * scale) {
                chosen = starts[k];
                break;
            }
        }
        plan.sections[i].outer = startingAt(loop, chosen);
    }

    return finish(std::move(plan), areas, heights);
}

} // namespace bettercad::geometry::detail
