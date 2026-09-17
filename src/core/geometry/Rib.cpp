// Ribs (P12-FEAT-005): the region a rib fills, as a prism, less the body;
// built from the qualified prisms and booleans only.
#include "core/geometry/ProfileExtent.hpp"

#include <bettercad/core/geometry/Booleans.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/geometry/Profile.hpp>
#include <bettercad/core/geometry/Rib.hpp>
#include <bettercad/core/geometry/Split.hpp>
#include <bettercad/core/geometry/Sweeps.hpp>
#include <bettercad/core/units/Format.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace bettercad::geometry {

namespace {

// Segments meet within the kernel's precision (1e-7 mm).
constexpr double kJoinSi = 1e-10;
// The bounds rectangle reaches this far beyond the body and the profile.
constexpr double kMarginSi = 1e-3;
// The joined result's volume must be the body's plus the rib's to this
// (relative); the booleans agree to rounding on these shapes.
constexpr double kVolumeRelative = 1e-9;

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

Vec2 si(const Point2D& p) {
    return {p.x.si(), p.y.si()};
}

Point2D point(const Vec2& p) {
    return {Length::fromSi(p.x), Length::fromSi(p.y)};
}

double gap(const Point2D& a, const Point2D& b) {
    return distance(a, b).si();
}

Vec2 unit(double x, double y) {
    const double n = std::hypot(x, y);
    return {x / n, y / n};
}

std::string_view closedKind(const ProfileSegment& segment) {
    if (std::holds_alternative<CircleSegment2D>(segment)) {
        return "a full circle";
    }
    if (std::holds_alternative<EllipseSegment2D>(segment)) {
        return "a full ellipse";
    }
    return "a periodic spline";
}

bool finite(const Point2D& p) {
    return std::isfinite(p.x.si()) && std::isfinite(p.y.si());
}

Result<void> checkProfile(const PlanarPath& profile) {
    const auto invalid = [](std::string message) { return makeError(ErrorCode::InvalidArgument, std::move(message)); };
    const std::vector<ProfileSegment>& segments = profile.segments;
    if (segments.empty()) {
        return invalid("a rib needs a profile of one or more segments");
    }
    for (std::size_t i = 0; i < segments.size(); ++i) {
        const ProfileSegment& segment = segments[i];
        if (isClosedSegment(segment)) {
            return invalid(std::format("profile segment {} is {}; a rib profile is an open chain of lines, arcs and "
                                       "open splines",
                                       i + 1, closedKind(segment)));
        }
        if (const auto* line = std::get_if<LineSegment2D>(&segment)) {
            if (!finite(line->start) || !finite(line->end)) {
                return invalid(std::format("profile segment {} is not finite", i + 1));
            }
            if (!(gap(line->start, line->end) > kJoinSi)) {
                return invalid(std::format("profile segment {} has zero length", i + 1));
            }
        } else if (const auto* arc = std::get_if<ArcSegment2D>(&segment)) {
            if (!finite(arc->center) || !finite(arc->start) || !finite(arc->end)) {
                return invalid(std::format("profile segment {} is not finite", i + 1));
            }
            const double r0 = gap(arc->center, arc->start);
            const double r1 = gap(arc->center, arc->end);
            if (!(r0 > kJoinSi) || !(std::abs(r1 - r0) <= kJoinSi)) {
                return invalid(std::format("profile segment {} is an arc whose ends are not on one circle", i + 1));
            }
            if (!(gap(arc->start, arc->end) > kJoinSi)) {
                return invalid(std::format("profile segment {} is an arc whose ends coincide", i + 1));
            }
        } else if (auto valid = validate(std::get<SplineSegment2D>(segment)); !valid) {
            return invalid(std::format("profile segment {}: {}", i + 1, valid.error().message));
        }
        if (i + 1 < segments.size() && !(gap(lastPoint(segment), firstPoint(segments[i + 1])) <= kJoinSi)) {
            return invalid(std::format("profile segment {} does not meet the next", i + 1));
        }
    }
    if (gap(firstPoint(segments.front()), lastPoint(segments.back())) <= kJoinSi) {
        return invalid("the profile is closed; a rib profile is open");
    }
    return {};
}

/// The unit direction of travel where @p segment starts.
Vec2 startDirection(const ProfileSegment& segment) {
    if (const auto* line = std::get_if<LineSegment2D>(&segment)) {
        return unit((line->end.x - line->start.x).si(), (line->end.y - line->start.y).si());
    }
    if (const auto* arc = std::get_if<ArcSegment2D>(&segment)) {
        const double rx = (arc->start.x - arc->center.x).si();
        const double ry = (arc->start.y - arc->center.y).si();
        return arc->counterClockwise ? unit(-ry, rx) : unit(ry, -rx);
    }
    // A clamped spline leaves its first pole towards the next distinct one.
    const auto& poles = std::get<SplineSegment2D>(segment).poles;
    for (std::size_t k = 1; k < poles.size(); ++k) {
        if (gap(poles[k], poles.front()) > kJoinSi) {
            return unit((poles[k].x - poles.front().x).si(), (poles[k].y - poles.front().y).si());
        }
    }
    return {1.0, 0.0}; // not reached: validate() refuses a vanishing control polygon
}

/// The unit direction of travel where @p segment ends.
Vec2 endDirection(const ProfileSegment& segment) {
    if (const auto* arc = std::get_if<ArcSegment2D>(&segment)) {
        const double rx = (arc->end.x - arc->center.x).si();
        const double ry = (arc->end.y - arc->center.y).si();
        return arc->counterClockwise ? unit(-ry, rx) : unit(ry, -rx);
    }
    if (const auto* spline = std::get_if<SplineSegment2D>(&segment)) {
        const auto& poles = spline->poles;
        for (std::size_t k = poles.size() - 1; k-- > 0;) {
            if (gap(poles[k], poles.back()) > kJoinSi) {
                return unit((poles.back().x - poles[k].x).si(), (poles.back().y - poles[k].y).si());
            }
        }
        return {1.0, 0.0};
    }
    return startDirection(segment);
}

/// The rectangle [u0, u1] x [v0, v1] (plane coordinates, metres) and the
/// perimeter parameter of its points, counter-clockwise from (u0, v0).
struct Bounds {
    double u0 = 0.0;
    double u1 = 0.0;
    double v0 = 0.0;
    double v1 = 0.0;

    [[nodiscard]] double width() const { return u1 - u0; }
    [[nodiscard]] double height() const { return v1 - v0; }
    [[nodiscard]] std::array<std::pair<double, Vec2>, 4> corners() const {
        return {{{0.0, {u0, v0}},
                 {width(), {u1, v0}},
                 {width() + height(), {u1, v1}},
                 {2.0 * width() + height(), {u0, v1}}}};
    }
};

/// Where the ray from @p p (inside) along @p d leaves the rectangle, and
/// that point's perimeter parameter.
std::pair<Vec2, double> exitPoint(const Bounds& b, const Vec2& p, const Vec2& d) {
    enum class Side { Bottom, Right, Top, Left };
    double t = std::numeric_limits<double>::infinity();
    Side side = Side::Bottom;
    const auto consider = [&](double candidate, Side s) {
        if (candidate < t) {
            t = candidate;
            side = s;
        }
    };
    if (d.x > 0.0) {
        consider((b.u1 - p.x) / d.x, Side::Right);
    } else if (d.x < 0.0) {
        consider((b.u0 - p.x) / d.x, Side::Left);
    }
    if (d.y > 0.0) {
        consider((b.v1 - p.y) / d.y, Side::Top);
    } else if (d.y < 0.0) {
        consider((b.v0 - p.y) / d.y, Side::Bottom);
    }
    Vec2 q{p.x + t * d.x, p.y + t * d.y};
    switch (side) {
    case Side::Bottom:
        q.y = b.v0;
        return {q, q.x - b.u0};
    case Side::Right:
        q.x = b.u1;
        return {q, b.width() + (q.y - b.v0)};
    case Side::Top:
        q.y = b.v1;
        return {q, b.width() + b.height() + (b.u1 - q.x)};
    case Side::Left:
        break;
    }
    q.x = b.u0;
    return {q, 2.0 * b.width() + b.height() + (b.v1 - q.y)};
}

FaceName marker(FaceSelector selector) {
    return FaceName{ObjectId::fromValue(1), std::move(selector)};
}

/// The name that marks loop segment @p index of the region.
FaceName segmentMarker(std::size_t index) {
    return marker({.role = FaceRole::Side, .entity = EntityId::fromValue(index + 1)});
}

} // namespace

std::string_view toString(RibPlacement placement) noexcept {
    switch (placement) {
    case RibPlacement::Symmetric:
        return "symmetric";
    case RibPlacement::AlongNormal:
        return "along normal";
    case RibPlacement::AgainstNormal:
        return "against normal";
    }
    return "unknown";
}

Result<void> validate(const RibRequest& request) {
    if (auto profile = checkProfile(request.profile); !profile) {
        return profile;
    }
    if (!isFinite(request.thickness) || !(request.thickness > Length{})) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the rib thickness must be positive and finite, got {}",
                                     toString(request.thickness, units::mm)));
    }
    if (request.placement != RibPlacement::Symmetric && request.placement != RibPlacement::AlongNormal &&
        request.placement != RibPlacement::AgainstNormal) {
        return makeError(ErrorCode::InvalidArgument, "a rib's thickness lies symmetric, along or against the normal");
    }
    return {};
}

Result<Body> addRib(const Body& body, const RibRequest& request, const SweptFaceNamer& namer) {
    if (body.isEmpty()) {
        return makeError(ErrorCode::FailedPrecondition, "rib: the body is empty");
    }
    if (auto valid = validate(request); !valid) {
        return makeError(ErrorCode::InvalidArgument, std::format("rib: {}", valid.error().message));
    }
    const auto prefixed = [](const Error& error) {
        return makeError(error.code, std::format("rib: {}", error.message));
    };
    const Frame3D& plane = request.profile.plane;
    const std::size_t count = request.profile.segments.size();

    // The rib fills the left of the chain: a flipped rib reverses it. Chain
    // position k (from 0) is profile segment original(k).
    const std::vector<ProfileSegment> chain =
        request.flipped ? reversed(ProfileLoop{request.profile.segments}).segments : request.profile.segments;
    const auto original = [&](std::size_t k) { return request.flipped ? count - 1 - k : k; };

    // The bounds: the profile's extent in the plane and the body's.
    const double inf = std::numeric_limits<double>::infinity();
    double vLow = inf;
    double vHigh = -inf;
    double negULow = inf;
    double negUHigh = -inf;
    const ProfileLoop asLoop{chain};
    detail::extendSideRange(asLoop, detail::PlaneAxis{0.0, 0.0, 1.0, 0.0}, vLow, vHigh);
    detail::extendSideRange(asLoop, detail::PlaneAxis{0.0, 0.0, 0.0, 1.0}, negULow, negUHigh);
    double uLow = -negUHigh;
    double uHigh = -negULow;
    auto box = body.boundingBox();
    if (!box) {
        return prefixed(box.error());
    }
    for (int corner = 0; corner < 8; ++corner) {
        const Point3D p{(corner & 1) != 0 ? box->max.x : box->min.x, (corner & 2) != 0 ? box->max.y : box->min.y,
                        (corner & 4) != 0 ? box->max.z : box->min.z};
        const Point2D local = plane.toLocal(p);
        uLow = std::min(uLow, local.x.si());
        uHigh = std::max(uHigh, local.x.si());
        vLow = std::min(vLow, local.y.si());
        vHigh = std::max(vHigh, local.y.si());
    }
    const Bounds bounds{uLow - kMarginSi, uHigh + kMarginSi, vLow - kMarginSi, vHigh + kMarginSi};

    // The region's loop: the start extension, the chain, the end extension,
    // then counter-clockwise along the bounds back to the start.
    const Vec2 first = si(firstPoint(chain.front()));
    const Vec2 last = si(lastPoint(chain.back()));
    const Vec2 back = startDirection(chain.front());
    const auto [startHit, startS] = exitPoint(bounds, first, Vec2{-back.x, -back.y});
    const auto [endHit, endS] = exitPoint(bounds, last, endDirection(chain.back()));
    const double perimeter = 2.0 * (bounds.width() + bounds.height());
    if (std::abs(startS - endS) <= kJoinSi || perimeter - std::abs(startS - endS) <= kJoinSi) {
        return makeError(ErrorCode::FailedPrecondition,
                         "rib: the profile's two ends, extended along their tangents, reach the bounds at one point");
    }
    ProfileLoop loop;
    loop.segments.emplace_back(LineSegment2D{point(startHit), point(first)});
    for (const ProfileSegment& segment : chain) {
        loop.segments.push_back(segment);
    }
    loop.segments.emplace_back(LineSegment2D{point(last), point(endHit)});
    const std::size_t firstBound = loop.segments.size();
    Vec2 at = endHit;
    const auto walkTo = [&](const Vec2& next) {
        loop.segments.emplace_back(LineSegment2D{point(at), point(next)});
        at = next;
    };
    const auto corners = bounds.corners();
    const auto between = [&](double s, double from, double to) { return s > from + kJoinSi && s < to - kJoinSi; };
    if (startS > endS) {
        for (const auto& [s, c] : corners) {
            if (between(s, endS, startS)) {
                walkTo(c);
            }
        }
    } else {
        for (const auto& [s, c] : corners) {
            if (between(s, endS, perimeter)) {
                walkTo(c);
            }
        }
        for (const auto& [s, c] : corners) {
            if (between(s, -1.0, startS)) {
                walkTo(c);
            }
        }
    }
    walkTo(startHit);

    // The region as a prism of the rib's thickness, its faces marked.
    const Length t = request.thickness;
    const Length from = request.placement == RibPlacement::Symmetric    ? -(t / 2.0)
                        : request.placement == RibPlacement::AlongNormal ? Length{}
                                                                         : -t;
    const Length to = request.placement == RibPlacement::Symmetric    ? t / 2.0
                      : request.placement == RibPlacement::AlongNormal ? t
                                                                       : Length{};
    const PlanarRegion region{.plane = plane, .outer = loop, .holes = {}};
    auto tool = makePrism(region, from, to, [](const SweptFace& face) -> std::optional<FaceName> {
        switch (face.kind) {
        case SweptFace::Kind::First:
            return marker({.role = FaceRole::StartCap});
        case SweptFace::Kind::Last:
            return marker({.role = FaceRole::EndCap});
        case SweptFace::Kind::Side:
            break;
        }
        return segmentMarker(face.segment);
    });
    if (!tool) {
        if (tool.error().code == ErrorCode::Internal) {
            // The loop is checked, so the kernel can only refuse its face.
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("rib: the profile, extended along its end tangents, crosses itself ({})",
                                         tool.error().message));
        }
        return prefixed(tool.error());
    }

    // The region less the body, in pieces; the body's own names stay out of
    // the way of the markers.
    const Body unnamed = renameFaces(body, [](const FaceName&) -> std::optional<FaceName> { return std::nullopt; });
    auto rest = booleanDifference(*tool, unnamed);
    if (!rest) {
        return prefixed(rest.error());
    }
    auto pieces = solidsOf(*rest);
    if (!pieces) {
        return prefixed(pieces.error());
    }
    const auto isChain = [&](const FaceName& name) {
        if (name.face.role != FaceRole::Side || !name.face.entity) {
            return false;
        }
        const std::uint64_t index = name.face.entity->value() - 1;
        return index >= 1 && index <= count;
    };
    const auto isBound = [&](const FaceName& name) {
        return name.face.role == FaceRole::Side && name.face.entity && name.face.entity->value() - 1 >= firstBound;
    };
    std::vector<Body> ribs;
    for (const Body& piece : *pieces) {
        auto faces = listFaces(piece);
        if (!faces) {
            return prefixed(faces.error());
        }
        bool bounded = false;
        bool open = false;
        for (const FaceInfo& face : *faces) {
            bounded = bounded || std::ranges::any_of(face.names, isChain);
            open = open || std::ranges::any_of(face.names, isBound);
        }
        if (!bounded) {
            continue;
        }
        if (open) {
            return makeError(ErrorCode::FailedPrecondition,
                             "rib: the side the rib fills is not closed off by the body: it reaches past the body "
                             "(fill the other side, or turn the profile towards the body)");
        }
        ribs.push_back(piece);
    }
    if (ribs.empty()) {
        return makeError(ErrorCode::FailedPrecondition,
                         "rib: the profile lies inside the body, so there is nothing to fill");
    }

    // The rib's own names in place of the markers, then the rib joined on.
    const FaceRenamer rename = [&](const FaceName& name) -> std::optional<FaceName> {
        if (!namer) {
            return std::nullopt;
        }
        if (name.face.role == FaceRole::StartCap) {
            return namer(SweptFace{.kind = SweptFace::Kind::First});
        }
        if (name.face.role == FaceRole::EndCap) {
            return namer(SweptFace{.kind = SweptFace::Kind::Last});
        }
        if (isChain(name)) {
            const auto k = static_cast<std::size_t>(name.face.entity->value() - 2);
            return namer(SweptFace{.kind = SweptFace::Kind::Side, .segment = original(k)});
        }
        return std::nullopt;
    };
    const auto before = body.massProperties();
    if (!before) {
        return prefixed(before.error());
    }
    Volume expected = before->volume;
    Body result = body;
    for (const Body& rib : ribs) {
        const auto props = rib.massProperties();
        if (!props) {
            return prefixed(props.error());
        }
        expected += props->volume;
        auto joined = booleanUnion(result, renameFaces(rib, rename));
        if (!joined) {
            return prefixed(joined.error());
        }
        result = std::move(*joined);
    }
    const auto after = result.massProperties();
    if (!result.isValid() || result.topology().solids != body.topology().solids || !after ||
        !(abs(after->volume - expected) <= kVolumeRelative * expected)) {
        return makeError(ErrorCode::Internal,
                         "rib: the kernel joined the rib to the body wrongly (the result is invalid, has another "
                         "number of solids, or does not add the rib's volume)");
    }
    return result;
}

} // namespace bettercad::geometry
