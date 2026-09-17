#include "features/SolidSupport.hpp"

#include <bettercad/core/geometry/Rib.hpp>
#include <bettercad/features/Regeneration.hpp>

#include <algorithm>
#include <format>
#include <optional>
#include <string_view>
#include <variant>

namespace bettercad::features {

namespace {

using geometry::ArcSegment2D;
using geometry::LineSegment2D;
using geometry::ProfileSegment;
using geometry::SplineSegment2D;

// Profile edges meet where their ends coincide within the sketch tolerance.
constexpr Length kTolerance = sketch::Sketch::kLengthTolerance;

ProfileSegment reversed(const ProfileSegment& segment) {
    if (const auto* line = std::get_if<LineSegment2D>(&segment)) {
        return LineSegment2D{line->end, line->start};
    }
    if (const auto* arc = std::get_if<ArcSegment2D>(&segment)) {
        return ArcSegment2D{arc->center, arc->end, arc->start, !arc->counterClockwise};
    }
    SplineSegment2D spline = std::get<SplineSegment2D>(segment);
    std::ranges::reverse(spline.poles);
    return spline;
}

/// @p segment starting exactly at @p start (a shared end, within the
/// tolerance).
ProfileSegment startingAt(ProfileSegment segment, const Point2D& start) {
    if (auto* line = std::get_if<LineSegment2D>(&segment)) {
        line->start = start;
    } else if (auto* arc = std::get_if<ArcSegment2D>(&segment)) {
        arc->start = start;
    } else {
        std::get<SplineSegment2D>(segment).poles.front() = start;
    }
    return segment;
}

/// An edge's segment in its natural direction (arcs counter-clockwise,
/// splines from the first pole).
Result<ProfileSegment> edgeSegment(const sketch::Sketch& sketch, EntityId id) {
    const sketch::Entity* entity = sketch.findEntity(id);
    if (entity == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("the profile edge {} does not exist in sketch '{}'", id, sketch.name()));
    }
    const auto invalid = [&](std::string_view problem) {
        return makeError(ErrorCode::InvalidArgument, std::format("the profile edge {} {}", id, problem));
    };
    if (entity->construction) {
        return invalid("is construction geometry, which makes no rib");
    }
    switch (entity->type()) {
    case sketch::EntityType::Point:
        return invalid("is a point, not a line, arc or open spline");
    case sketch::EntityType::Circle:
        return invalid("is a circle, not a line, arc or open spline");
    case sketch::EntityType::Ellipse:
        return invalid("is an ellipse, not a line, arc or open spline");
    case sketch::EntityType::Spline: {
        const auto& geometry = std::get<sketch::SplineEntity>(entity->geometry);
        if (geometry.periodic) {
            return invalid("is a periodic spline, not a line, arc or open spline");
        }
        SplineSegment2D spline{.poles = {}, .degree = geometry.degree, .periodic = false};
        for (const EntityId pole : geometry.poles) {
            spline.poles.push_back(sketch.position(pole).value());
        }
        return spline;
    }
    case sketch::EntityType::Line:
    case sketch::EntityType::Arc:
        break;
    }
    const sketch::Endpoints ends = sketch.endpoints(id).value();
    if (!(distance(ends.start, ends.end) > kTolerance)) {
        return invalid(entity->type() == sketch::EntityType::Line ? "has zero length"
                                                                   : "is an arc whose ends coincide");
    }
    if (entity->type() == sketch::EntityType::Line) {
        return LineSegment2D{ends.start, ends.end};
    }
    return ArcSegment2D{sketch.center(id).value(), ends.start, ends.end, true};
}

std::unexpected<Error> disconnected(EntityId a, EntityId b, Length gap) {
    return makeError(ErrorCode::InvalidArgument,
                     std::format("the profile is not connected: the edges {} and {} do not meet (their nearest ends "
                                 "are {:.6g} mm apart)",
                                 a, b, gap.in(units::mm)));
}

} // namespace

Result<Length> resolveRibThickness(const RibDefinition& definition, const Document& document) {
    if (!definition.thicknessParameter) {
        return definition.thickness;
    }
    return detail::drivingValue<Length>(document, *definition.thicknessParameter, "thickness parameter");
}

Result<geometry::PlanarPath> resolveRibProfile(const RibDefinition& definition, const Document& document) {
    const auto* sketch = document.findObjectAs<sketch::Sketch>(ObjectId{definition.profile});
    if (sketch == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("profile {} is not a sketch in this document", definition.profile));
    }
    std::vector<ProfileSegment> natural;
    for (const EntityId id : definition.edges) {
        auto segment = edgeSegment(*sketch, id);
        if (!segment) {
            return std::unexpected(segment.error());
        }
        natural.push_back(*segment);
    }
    const auto& edges = definition.edges;
    if (natural.size() == 1) {
        return geometry::PlanarPath{sketch->placement(), std::move(natural)};
    }
    // Head to tail: the first edge turned to end on the second, every later
    // edge to start where the one before ends (exactly there).
    std::vector<ProfileSegment> oriented;
    oriented.reserve(natural.size());
    const auto near = [](const Point2D& a, const Point2D& b) { return distance(a, b) <= kTolerance; };
    const Point2D secondStart = geometry::firstPoint(natural[1]);
    const Point2D secondEnd = geometry::lastPoint(natural[1]);
    const Point2D firstStart = geometry::firstPoint(natural[0]);
    const Point2D firstEnd = geometry::lastPoint(natural[0]);
    if (near(firstEnd, secondStart) || near(firstEnd, secondEnd)) {
        oriented.push_back(natural[0]);
    } else if (near(firstStart, secondStart) || near(firstStart, secondEnd)) {
        oriented.push_back(reversed(natural[0]));
    } else {
        return disconnected(edges[0], edges[1],
                            std::min({distance(firstStart, secondStart), distance(firstStart, secondEnd),
                                      distance(firstEnd, secondStart), distance(firstEnd, secondEnd)}));
    }
    for (std::size_t i = 1; i < natural.size(); ++i) {
        const Point2D at = geometry::lastPoint(oriented.back());
        if (near(geometry::firstPoint(natural[i]), at)) {
            oriented.push_back(startingAt(natural[i], at));
        } else if (near(geometry::lastPoint(natural[i]), at)) {
            oriented.push_back(startingAt(reversed(natural[i]), at));
        } else {
            return disconnected(edges[i - 1], edges[i],
                                std::min(distance(geometry::firstPoint(natural[i]), at),
                                         distance(geometry::lastPoint(natural[i]), at)));
        }
    }
    return geometry::PlanarPath{sketch->placement(), std::move(oriented)};
}

Result<geometry::Body> regenerateRib(const RibFeature& feature, const Document& document,
                                     const geometry::Body* target) {
    const RibDefinition& definition = feature.definition();
    const auto rib = [&](const geometry::Body& body) -> Result<geometry::Body> {
        auto thickness = resolveRibThickness(definition, document);
        if (!thickness) {
            return std::unexpected(thickness.error());
        }
        auto profile = resolveRibProfile(definition, document);
        if (!profile) {
            return std::unexpected(profile.error());
        }
        // Segment i of the profile is the definition's edge i.
        const ObjectId id = feature.id();
        const auto namer = [&](const geometry::SweptFace& face) -> std::optional<FaceName> {
            switch (face.kind) {
            case geometry::SweptFace::Kind::First:
                return FaceName{id, FaceSelector{.role = FaceRole::StartCap}};
            case geometry::SweptFace::Kind::Last:
                return FaceName{id, FaceSelector{.role = FaceRole::EndCap}};
            case geometry::SweptFace::Kind::Side:
                break;
            }
            return FaceName{id, FaceSelector{.role = FaceRole::Side, .entity = definition.edges.at(face.segment)}};
        };
        return geometry::addRib(body,
                                {.profile = std::move(*profile),
                                 .thickness = *thickness,
                                 .placement = definition.placement,
                                 .flipped = definition.flipped},
                                namer);
    };
    return detail::applyToTargetBody(feature.name(), "rib", target, rib);
}

} // namespace bettercad::features
