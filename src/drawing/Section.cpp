#include <bettercad/drawing/Section.hpp>

#include <bettercad/core/geometry/Booleans.hpp>
#include <bettercad/core/geometry/Edges.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

namespace bettercad::drawing {
namespace {

[[nodiscard]] std::unexpected<Error> wrong(std::string message) {
    return makeError(ErrorCode::InvalidArgument, std::move(message));
}

[[nodiscard]] bool finite(Length value) noexcept { return std::isfinite(value.si()); }

[[nodiscard]] bool finite(const Point3D& p) noexcept {
    return finite(p.x) && finite(p.y) && finite(p.z);
}

} // namespace

std::string_view toString(SectionKind kind) noexcept {
    switch (kind) {
    case SectionKind::Full:
        return "full";
    case SectionKind::Half:
        return "half";
    case SectionKind::Offset:
        return "offset";
    }
    return "unknown";
}

std::optional<SectionKind> sectionKindFromString(std::string_view text) noexcept {
    for (const SectionKind kind : {SectionKind::Full, SectionKind::Half, SectionKind::Offset}) {
        if (toString(kind) == text) {
            return kind;
        }
    }
    return std::nullopt;
}

Result<Frame3D> frameOf(const CuttingPlane& plane) {
    // Frame3D::create projects the reference into the plane and refuses a
    // near-parallel pair, so the orthonormality and the "not parallel" rule
    // are the qualified ones rather than a second copy here.
    return Frame3D::create(plane.origin, plane.normal, plane.reference);
}

Result<void> validate(const CuttingPlane& plane) {
    if (!finite(plane.origin)) {
        return wrong("a cutting plane's origin must be finite");
    }
    if (toString(plane.kind) == "unknown") {
        return wrong("a cutting plane must have a known kind");
    }
    // Builds the frame, which is what checks that the normal and the
    // reference are usable and not parallel.
    if (auto frame = frameOf(plane); !frame) {
        return std::unexpected(frame.error());
    }

    if (plane.kind == SectionKind::Half) {
        if (!finite(plane.splitAt)) {
            return wrong("a half section's split position must be finite");
        }
    } else if (plane.splitAt.si() != 0.0) {
        return wrong(std::format("a {} section takes no split position; only a half section does",
                                 toString(plane.kind)));
    }

    if (plane.kind == SectionKind::Offset) {
        if (plane.legs.empty()) {
            return wrong("an offset section must have at least one leg; a section with none is a "
                         "full section");
        }
        double previous = 0.0;
        bool first = true;
        for (const SectionLeg& leg : plane.legs) {
            if (!finite(leg.at) || !finite(leg.offset)) {
                return wrong("an offset section's legs must be finite");
            }
            // Strictly increasing, so the cutting path never doubles back on
            // itself -- which would make the order of the legs matter to the
            // result in a way nothing could validate.
            if (!first && leg.at.si() <= previous) {
                return wrong("an offset section's legs must step forward: each must be further "
                             "along the plane than the one before");
            }
            previous = leg.at.si();
            first = false;
        }
    } else if (!plane.legs.empty()) {
        return wrong(std::format("a {} section takes no legs; only an offset section does",
                                 toString(plane.kind)));
    }
    return {};
}

Result<void> validate(const HatchSettings& settings) {
    if (!std::isfinite(settings.angle.si())) {
        return wrong("a hatch angle must be finite");
    }
    if (!finite(settings.spacing) || settings.spacing.si() <= 0.0) {
        return wrong("a hatch spacing must be finite and greater than zero");
    }
    if (settings.pattern != "iso-45") {
        return wrong(std::format("unknown hatch pattern '{}'", settings.pattern));
    }
    return {};
}

double twiceSignedArea(const std::vector<Point2D>& loop) noexcept {
    if (loop.size() < 3) {
        return 0.0;
    }
    double total = 0.0;
    for (std::size_t i = 0; i < loop.size(); ++i) {
        const Point2D& a = loop[i];
        const Point2D& b = loop[(i + 1) % loop.size()];
        total += a.x.si() * b.y.si() - b.x.si() * a.y.si();
    }
    return total;
}

bool contains(const std::vector<Point2D>& loop, const Point2D& point) noexcept {
    // Crossing number: count the edges a ray to +x crosses. Odd means inside.
    // The half-open rule on y (one end strictly above, one not) is what stops
    // a vertex exactly at the ray's height being counted twice.
    if (loop.size() < 3) {
        return false;
    }
    const double px = point.x.si();
    const double py = point.y.si();
    bool inside = false;
    for (std::size_t i = 0, j = loop.size() - 1; i < loop.size(); j = i++) {
        const double xi = loop[i].x.si();
        const double yi = loop[i].y.si();
        const double xj = loop[j].x.si();
        const double yj = loop[j].y.si();
        if ((yi > py) != (yj > py)) {
            const double x = xj + (py - yj) / (yi - yj) * (xi - xj);
            if (px < x) {
                inside = !inside;
            }
        }
    }
    return inside;
}

std::vector<double> crossings(const std::vector<Point2D>& loop, const Point2D& from,
                              const Point2D& to) {
    std::vector<double> found;
    if (loop.size() < 3) {
        return found;
    }
    const double dx = (to.x - from.x).si();
    const double dy = (to.y - from.y).si();
    const double lengthSquared = dx * dx + dy * dy;
    if (lengthSquared == 0.0) {
        return found;
    }
    for (std::size_t i = 0, j = loop.size() - 1; i < loop.size(); j = i++) {
        const double ax = loop[j].x.si();
        const double ay = loop[j].y.si();
        const double bx = loop[i].x.si();
        const double by = loop[i].y.si();
        const double ex = bx - ax;
        const double ey = by - ay;
        const double denominator = dx * ey - dy * ex;
        if (denominator == 0.0) {
            continue; // parallel: no single crossing
        }
        const double ox = ax - from.x.si();
        const double oy = ay - from.y.si();
        const double t = (ox * ey - oy * ex) / denominator; // along the line
        const double u = (ox * dy - oy * dx) / denominator; // along the edge
        // Half-open in u, so a crossing exactly at a shared vertex is counted
        // once rather than by both edges that meet there.
        if (u >= 0.0 && u < 1.0) {
            found.push_back(t);
        }
    }
    std::ranges::sort(found);
    return found;
}


namespace {

/// How near the cutting plane an edge must lie to count as on it.
///
/// The kernel places the cut faces exactly on the plane, so this only has to
/// absorb double-precision noise from the intersection. 1e-7 mm is the figure
/// splitBody itself uses to decide whether a body reaches a plane.
constexpr double kOnPlaneSi = 1e-10; // 1e-7 mm, in metres

/// How near two projected points must be to be the same corner when edges are
/// chained into loops. Loose enough for kernel noise, far tighter than any
/// feature a drawing would show.
constexpr double kSameCornerSi = 1e-9;

/// The plane through @p at along @p base's X axis, with its normal along
/// that axis -- the plane a half section stops at, and the one an offset
/// section's legs are bounded by.
[[nodiscard]] Result<Frame3D> frameAtX(const Frame3D& base, Length at);

/// A point @p distance along @p direction from @p from.
[[nodiscard]] Point3D offsetPoint(const Point3D& from, const Direction3D& direction,
                                  Length distance) noexcept {
    return Point3D{from.x + Length::fromSi(direction.x() * distance.si()),
                   from.y + Length::fromSi(direction.y() * distance.si()),
                   from.z + Length::fromSi(direction.z() * distance.si())};
}

/// Where a body sits relative to a plane: entirely on the normal side,
/// entirely on the other, or across it.
enum class Side { Front, Back, Across };

[[nodiscard]] Result<Side> sideOf(const geometry::Body& body, const Frame3D& plane) {
    auto bounds = body.boundingBox();
    if (!bounds) {
        return std::unexpected(bounds.error());
    }
    bool anyFront = false;
    bool anyBack = false;
    for (int corner = 0; corner < 8; ++corner) {
        const Point3D p{(corner & 1) != 0 ? bounds->max.x : bounds->min.x,
                        (corner & 2) != 0 ? bounds->max.y : bounds->min.y,
                        (corner & 4) != 0 ? bounds->max.z : bounds->min.z};
        const double d = plane.signedDistance(p).si();
        if (d > kOnPlaneSi) {
            anyFront = true;
        } else if (d < -kOnPlaneSi) {
            anyBack = true;
        }
    }
    if (anyFront && anyBack) {
        return Side::Across;
    }
    return anyBack ? Side::Back : Side::Front;
}

/// @p body reduced to the part of it on one side of @p plane, or nothing when
/// none of it is there.
///
/// splitBody refuses a plane that does not cross the body, which is right for
/// a split but not for building a region out of half-spaces: there, a plane
/// the body sits wholly on one side of means the body passes through whole or
/// vanishes. The bounds decide which, so splitBody is only asked to do the
/// cuts it is for.
[[nodiscard]] Result<std::optional<geometry::Body>> clipToHalfSpace(const geometry::Body& body,
                                                                    const Frame3D& plane,
                                                                    geometry::SplitKeep keep) {
    auto side = sideOf(body, plane);
    if (!side) {
        return std::unexpected(side.error());
    }
    if (*side != Side::Across) {
        const bool kept = (*side == Side::Front) == (keep == geometry::SplitKeep::Front);
        if (!kept) {
            return std::optional<geometry::Body>{};
        }
        return std::optional<geometry::Body>{body};
    }
    auto part = geometry::splitBody(body, plane, keep);
    if (!part) {
        return std::unexpected(part.error());
    }
    return std::optional<geometry::Body>{std::move(*part)};
}

Result<Frame3D> frameAtX(const Frame3D& base, Length at) {
    // Its own X axis is the cutting plane's normal: any direction in the
    // plane would do, and that one is already there and known not to be
    // parallel to the axis this plane's normal runs along.
    return Frame3D::create(offsetPoint(base.origin(), base.xAxis(), at), base.xAxis(),
                           base.normal());
}

/// Rotates and orients a loop so that the same shape is always written the
/// same way.
///
/// The kernel hands its edges back in an order that carries no meaning, so
/// where a chained loop starts and which way round it runs are accidents of
/// that order. Left alone they would be free to differ between builds while
/// the shape stayed identical. Starting at the lowest corner and running
/// counter-clockwise for material, clockwise for a void, fixes both -- and
/// leaves the loops in the winding a non-zero fill rule already expects.
void canonicalise(std::vector<Point2D>& loop, bool counterClockwise) {
    if (loop.size() < 3) {
        return;
    }
    const auto lowest = std::ranges::min_element(loop, [](const Point2D& a, const Point2D& b) {
        return a.x.si() != b.x.si() ? a.x.si() < b.x.si() : a.y.si() < b.y.si();
    });
    std::ranges::rotate(loop, lowest);
    if ((twiceSignedArea(loop) > 0.0) != counterClockwise) {
        // Reverse everything BUT the first point, so the loop keeps the
        // corner it starts at and only changes direction.
        std::reverse(loop.begin() + 1, loop.end());
    }
}

/// The side of the cutting plane a section removes: the one the viewer is on.
[[nodiscard]] geometry::SplitKeep removedSide(const Frame3D& plane,
                                              const Frame3D& viewBasis) noexcept {
    return keptSide(plane, viewBasis) == geometry::SplitKeep::Front ? geometry::SplitKeep::Back
                                                                    : geometry::SplitKeep::Front;
}

[[nodiscard]] bool sameCorner(const Point2D& a, const Point2D& b) noexcept {
    return std::abs((a.x - b.x).si()) <= kSameCornerSi &&
           std::abs((a.y - b.y).si()) <= kSameCornerSi;
}

/// Drops cut edges that coincide with another, in either direction.
///
/// Where an offset section jogs from one leg to the next, both legs' cut
/// faces end on the jog, and seen along the shared normal those two edges
/// land on each other. An edge with material on both sides of it is not a
/// boundary, so the pair cancels -- which is precisely the rule that
/// "develops" a stepped section into a single plane, and why ISO 128 does not
/// draw a line at the jog.
///
/// A segment appearing an odd number of times keeps one copy, so the rule is
/// stated as parity rather than as "delete both".
[[nodiscard]] std::vector<std::pair<Point2D, Point2D>> cancelCoincident(
    const std::vector<std::pair<Point2D, Point2D>>& segments) {
    std::vector<std::pair<Point2D, Point2D>> kept;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        std::size_t count = 0;
        for (std::size_t j = 0; j < segments.size(); ++j) {
            const bool same =
                (sameCorner(segments[i].first, segments[j].first) &&
                 sameCorner(segments[i].second, segments[j].second)) ||
                (sameCorner(segments[i].first, segments[j].second) &&
                 sameCorner(segments[i].second, segments[j].first));
            if (same && j < i) {
                count = 0; // an earlier copy already decided this one
                break;
            }
            if (same) {
                ++count;
            }
        }
        if (count % 2 == 1) {
            kept.push_back(segments[i]);
        }
    }
    return kept;
}

/// Removes vertices that lie on the straight line between their neighbours.
///
/// Cancelling a jog's edges joins the two legs' cut faces along what was
/// their shared boundary, leaving a vertex in the middle of every edge that
/// crossed it. The vertex says nothing -- the edge is straight through it --
/// and a drawing that carried it would put a corner where the geometry has
/// none.
///
/// The test is the perpendicular distance from the vertex to the line through
/// its neighbours, so it does not depend on how long the two edges are.
[[nodiscard]] std::vector<Point2D> dropCollinear(const std::vector<Point2D>& loop) {
    if (loop.size() < 3) {
        return loop;
    }
    std::vector<Point2D> kept;
    kept.reserve(loop.size());
    for (std::size_t i = 0; i < loop.size(); ++i) {
        const Point2D& previous = loop[(i + loop.size() - 1) % loop.size()];
        const Point2D& here = loop[i];
        const Point2D& next = loop[(i + 1) % loop.size()];
        const double ax = (here.x - previous.x).si();
        const double ay = (here.y - previous.y).si();
        const double bx = (next.x - here.x).si();
        const double by = (next.y - here.y).si();
        const double cross = ax * by - ay * bx;
        const double span = std::hypot(ax + bx, ay + by);
        if (span > 0.0 && std::abs(cross) <= kSameCornerSi * span) {
            continue; // straight through: the vertex is not a corner
        }
        kept.push_back(here);
    }
    return kept.size() >= 3 ? kept : loop;
}

/// Chains segments into closed loops by matching endpoints.
///
/// The kernel hands edges back in its own order, which carries no meaning, so
/// the loops have to be rebuilt rather than read off. A run that does not
/// close is dropped: an open chain is not a boundary, and hatching inside one
/// would be hatching a region with no inside.
[[nodiscard]] std::vector<std::vector<Point2D>> chainLoops(
    std::vector<std::pair<Point2D, Point2D>> segments) {
    std::vector<std::vector<Point2D>> loops;
    std::vector<bool> used(segments.size(), false);
    for (std::size_t start = 0; start < segments.size(); ++start) {
        if (used[start]) {
            continue;
        }
        used[start] = true;
        std::vector<Point2D> loop{segments[start].first, segments[start].second};
        bool closed = false;
        bool grew = true;
        while (grew && !closed) {
            grew = false;
            for (std::size_t i = 0; i < segments.size(); ++i) {
                if (used[i]) {
                    continue;
                }
                if (sameCorner(loop.back(), segments[i].first)) {
                    loop.push_back(segments[i].second);
                } else if (sameCorner(loop.back(), segments[i].second)) {
                    loop.push_back(segments[i].first);
                } else {
                    continue;
                }
                used[i] = true;
                grew = true;
                if (sameCorner(loop.back(), loop.front())) {
                    loop.pop_back(); // the closing point repeats the first
                    closed = true;
                    break;
                }
            }
        }
        if (closed && loop.size() >= 3 && std::abs(twiceSignedArea(loop)) > 0.0) {
            loops.push_back(dropCollinear(loop));
        }
    }
    return loops;
}

} // namespace

Result<std::vector<Frame3D>> legFrames(const CuttingPlane& plane) {
    if (auto valid = validate(plane); !valid) {
        return std::unexpected(valid.error());
    }
    auto base = frameOf(plane);
    if (!base) {
        return std::unexpected(base.error());
    }
    std::vector<Frame3D> frames{*base};
    for (const SectionLeg& leg : plane.legs) {
        auto shifted = Frame3D::fromAxes(offsetPoint(base->origin(), base->normal(), leg.offset),
                                         base->xAxis(), base->yAxis(), base->normal());
        if (!shifted) {
            return std::unexpected(shifted.error());
        }
        frames.push_back(*shifted);
    }
    return frames;
}

Result<Frame3D> splitFrame(const CuttingPlane& plane) {
    auto base = frameOf(plane);
    if (!base) {
        return std::unexpected(base.error());
    }
    // Normal along the cutting plane's X axis, so its front side is where the
    // local x exceeds splitAt.
    return frameAtX(*base, plane.splitAt);
}

geometry::SplitKeep keptSide(const Frame3D& plane, const Frame3D& viewBasis) noexcept {
    // The view's normal points from the model toward the viewer (ADR-013). If
    // the plane's normal points the same way, the viewer stands on the
    // plane's front side, so the front is what is removed and the back kept.
    const Direction3D& n = plane.normal();
    const Direction3D& v = viewBasis.normal();
    const double alignment = n.x() * v.x() + n.y() * v.y() + n.z() * v.z();
    return alignment > 0.0 ? geometry::SplitKeep::Back : geometry::SplitKeep::Front;
}

Result<geometry::Body> cutBody(const geometry::Body& body, const CuttingPlane& plane,
                               const Frame3D& viewBasis) {
    auto frames = legFrames(plane);
    if (!frames) {
        return std::unexpected(frames.error());
    }
    const Frame3D& base = frames->front();
    // A plane at right angles to the direction of sight is seen edge-on: it
    // has no side between it and the viewer, so there is nothing a section
    // could remove and nothing it could show. Said plainly here, because the
    // symptom further down would be "the cut edges did not close into any
    // loop", which is true but explains nothing.
    if (std::abs(base.normal().dot(viewBasis.normal())) < 1e-9) {
        return makeError(ErrorCode::FailedPrecondition,
                         "the cutting plane is edge-on to this view, so it has no side to cut away");
    }
    const geometry::SplitKeep removed = removedSide(base, viewBasis);

    if (plane.kind == SectionKind::Full) {
        // One half-space, so the split is the answer and no boolean is needed.
        return geometry::splitBody(body, base, keptSide(base, viewBasis));
    }

    // Half and offset sections remove a region assembled from half-spaces
    // rather than a single one, so the region is built and then subtracted.
    // Building it from splits of the body itself -- rather than from a box big
    // enough to hold it -- keeps the region bounded without inventing a size.
    std::vector<geometry::Body> pieces;
    const auto addPiece = [&](std::optional<geometry::Body> piece) {
        if (piece) {
            pieces.push_back(std::move(*piece));
        }
    };

    if (plane.kind == SectionKind::Half) {
        auto split = frameAtX(base, plane.splitAt);
        if (!split) {
            return std::unexpected(split.error());
        }
        auto viewerSide = clipToHalfSpace(body, base, removed);
        if (!viewerSide) {
            return std::unexpected(viewerSide.error());
        }
        if (*viewerSide) {
            auto cutHalf = clipToHalfSpace(**viewerSide, *split, geometry::SplitKeep::Front);
            if (!cutHalf) {
                return std::unexpected(cutHalf.error());
            }
            addPiece(std::move(*cutHalf));
        }
    } else {
        // One piece per leg: what lies between the viewer and that leg's
        // plane, within that leg's span along the cutting plane's X axis. The
        // first leg runs from nowhere, the last runs to nowhere, so those
        // bounds are simply not applied.
        for (std::size_t i = 0; i < frames->size(); ++i) {
            auto piece = clipToHalfSpace(body, (*frames)[i], removed);
            if (!piece) {
                return std::unexpected(piece.error());
            }
            if (!*piece) {
                continue;
            }
            // Leg i covers x from legs[i - 1].at (the base leg from nowhere)
            // to legs[i].at (the last leg to nowhere).
            if (i > 0) {
                auto lower = frameAtX(base, plane.legs[i - 1].at);
                if (!lower) {
                    return std::unexpected(lower.error());
                }
                auto clipped = clipToHalfSpace(**piece, *lower, geometry::SplitKeep::Front);
                if (!clipped) {
                    return std::unexpected(clipped.error());
                }
                *piece = std::move(*clipped);
                if (!*piece) {
                    continue;
                }
            }
            if (i < plane.legs.size()) {
                auto upper = frameAtX(base, plane.legs[i].at);
                if (!upper) {
                    return std::unexpected(upper.error());
                }
                auto clipped = clipToHalfSpace(**piece, *upper, geometry::SplitKeep::Back);
                if (!clipped) {
                    return std::unexpected(clipped.error());
                }
                *piece = std::move(*clipped);
            }
            addPiece(std::move(*piece));
        }
    }

    if (pieces.empty()) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("the {} section's cutting path removes nothing from the body",
                                     toString(plane.kind)));
    }
    geometry::Body region = std::move(pieces.front());
    for (std::size_t i = 1; i < pieces.size(); ++i) {
        auto joined = geometry::booleanOperation(geometry::BooleanOperation::Union, region,
                                                 pieces[i]);
        if (!joined) {
            return std::unexpected(joined.error());
        }
        region = std::move(*joined);
    }
    return geometry::booleanOperation(geometry::BooleanOperation::Difference, body, region);
}

Result<SectionGeometry> sectionGeometry(const geometry::Body& body, const CuttingPlane& plane,
                                        const Frame3D& viewBasis, double factor,
                                        const Point2D& placement, const HatchSettings& hatch) {
    if (auto valid = validate(hatch); !valid) {
        return std::unexpected(valid.error());
    }
    auto frames = legFrames(plane);
    if (!frames) {
        return std::unexpected(frames.error());
    }
    auto cut = cutBody(body, plane, viewBasis);
    if (!cut) {
        return std::unexpected(cut.error());
    }
    auto edges = geometry::listEdges(*cut);
    if (!edges) {
        return std::unexpected(edges.error());
    }

    // The outline of the cut is the edges lying ON the plane. A face's
    // boundary loops are not available from the geometry module, so the
    // boundary is rebuilt from the edges that bound it.
    std::vector<std::pair<Point2D, Point2D>> onPlane;
    for (const geometry::EdgeInfo& edge : *edges) {
        // On ANY leg's plane. A full or half section has one; an offset
        // section has one per leg, and its cut faces are spread over them.
        // The midpoint is tested as well as the ends, so an edge that merely
        // touches the plane at a corner is not mistaken for one lying on it.
        const bool onACutPlane = std::ranges::any_of(*frames, [&](const Frame3D& f) {
            return std::abs(f.signedDistance(edge.start).si()) <= kOnPlaneSi &&
                   std::abs(f.signedDistance(edge.end).si()) <= kOnPlaneSi &&
                   std::abs(f.signedDistance(edge.midpoint).si()) <= kOnPlaneSi;
        });
        if (!onACutPlane) {
            continue;
        }
        if (edge.curve != geometry::EdgeCurve::Line) {
            // A curved cut edge needs tessellating before it can bound a
            // hatched region, and that is the curve work P14-HLR-001 owns.
            //
            // Refused, not skipped. Skipping it would let a round hole
            // through a sectioned wall vanish from the outline: the wall's
            // own rectangle would still close into a loop, the hole would not
            // be there to be a void, and the section would come back looking
            // correct with hatch drawn straight across solid material that
            // is not there. A drawing that is confidently wrong is worse
            // than one that is refused.
            return makeError(
                ErrorCode::FailedPrecondition,
                std::format("the cut face is bounded by a {} edge; sections of curved faces need "
                            "the curve handling P14-HLR-001 adds, and are refused until then",
                            geometry::toString(edge.curve)));
        }
        onPlane.emplace_back(viewBasis.toLocal(edge.start), viewBasis.toLocal(edge.end));
    }
    if (onPlane.empty()) {
        return makeError(ErrorCode::FailedPrecondition,
                         "the cutting plane produced no straight cut edges to outline");
    }

    // Centre on the cut's own extent, then scale and place, exactly as a
    // view's projection does, so a section sits where it was put.
    double minX = onPlane.front().first.x.si();
    double maxX = minX;
    double minY = onPlane.front().first.y.si();
    double maxY = minY;
    const auto see = [&](const Point2D& p) {
        minX = std::min(minX, p.x.si());
        maxX = std::max(maxX, p.x.si());
        minY = std::min(minY, p.y.si());
        maxY = std::max(maxY, p.y.si());
    };
    for (const auto& segment : onPlane) {
        see(segment.first);
        see(segment.second);
    }
    const double centreX = 0.5 * (minX + maxX);
    const double centreY = 0.5 * (minY + maxY);
    const auto toSheet = [&](const Point2D& p) {
        return Point2D{placement.x + Length::fromSi((p.x.si() - centreX) * factor),
                       placement.y + Length::fromSi((p.y.si() - centreY) * factor)};
    };
    for (auto& segment : onPlane) {
        segment.first = toSheet(segment.first);
        segment.second = toSheet(segment.second);
    }

    SectionGeometry result;
    // Cancel the jog edges before chaining: they are not boundaries, and
    // leaving them in would give the chain a fork to choose at, which is a
    // determinism hazard as well as a wrong drawing.
    std::vector<std::vector<Point2D>> loops = chainLoops(cancelCoincident(onPlane));
    if (loops.empty()) {
        return makeError(ErrorCode::FailedPrecondition, "the cut edges did not close into any loop");
    }
    // The kernel's edge order carries no meaning, so the loops are sorted into
    // a canonical order. Without this the same section could hand its loops
    // back in a different order on a different build.
    std::ranges::sort(loops, [](const std::vector<Point2D>& a, const std::vector<Point2D>& b) {
        const double areaA = std::abs(twiceSignedArea(a));
        const double areaB = std::abs(twiceSignedArea(b));
        if (areaA != areaB) {
            return areaA > areaB; // the material before its voids
        }
        return a.front().x.si() < b.front().x.si();
    });

    // A loop inside an odd number of others is a void.
    double signedTotal = 0.0;
    for (std::size_t i = 0; i < loops.size(); ++i) {
        bool inside = false;
        for (std::size_t j = 0; j < loops.size(); ++j) {
            if (i != j && contains(loops[j], loops[i].front())) {
                inside = !inside;
            }
        }
        SectionLoop loop;
        loop.points = loops[i];
        loop.outer = !inside;
        canonicalise(loop.points, loop.outer);
        signedTotal += (inside ? -1.0 : 1.0) * std::abs(0.5 * twiceSignedArea(loops[i]));
        result.loops.push_back(std::move(loop));
    }
    result.area = Area::fromSi(signedTotal);

    // Hatch: parallel lines at the given angle and spacing, clipped to the
    // material. Every loop's crossings with a line, taken together and
    // sorted, alternate outside/inside, so alternate spans are material --
    // which is what keeps hatch out of the holes.
    const double angle = hatch.angle.si();
    const double dirX = std::cos(angle);
    const double dirY = std::sin(angle);
    const double spacing = hatch.spacing.si();
    double alongMin = 0.0;
    double alongMax = 0.0;
    double acrossMin = 0.0;
    double acrossMax = 0.0;
    bool first = true;
    for (const SectionLoop& loop : result.loops) {
        for (const Point2D& p : loop.points) {
            const double along = p.x.si() * dirX + p.y.si() * dirY;
            const double across = -p.x.si() * dirY + p.y.si() * dirX;
            if (first) {
                alongMin = alongMax = along;
                acrossMin = acrossMax = across;
                first = false;
                continue;
            }
            alongMin = std::min(alongMin, along);
            alongMax = std::max(alongMax, along);
            acrossMin = std::min(acrossMin, across);
            acrossMax = std::max(acrossMax, across);
        }
    }
    // Start half a spacing in, so a hatch line never lands exactly on the
    // boundary, where contains() is deliberately undefined.
    for (double across = acrossMin + 0.5 * spacing; across < acrossMax; across += spacing) {
        const Point2D from{Length::fromSi(alongMin * dirX - across * dirY),
                           Length::fromSi(alongMin * dirY + across * dirX)};
        const Point2D to{Length::fromSi(alongMax * dirX - across * dirY),
                         Length::fromSi(alongMax * dirY + across * dirX)};
        std::vector<double> all;
        for (const SectionLoop& loop : result.loops) {
            for (const double t : crossings(loop.points, from, to)) {
                all.push_back(t);
            }
        }
        std::ranges::sort(all);
        for (std::size_t i = 0; i + 1 < all.size(); i += 2) {
            const double t0 = all[i];
            const double t1 = all[i + 1];
            if (t1 <= t0) {
                continue;
            }
            const auto at = [&](double t) {
                return Point2D{from.x + Length::fromSi(t * (to.x - from.x).si()),
                               from.y + Length::fromSi(t * (to.y - from.y).si())};
            };
            result.hatch.emplace_back(at(t0), at(t1));
        }
    }
    return result;
}

} // namespace bettercad::drawing
