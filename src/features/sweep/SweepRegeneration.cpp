#include "features/SolidSupport.hpp"

#include <bettercad/core/geometry/Sweeps.hpp>
#include <bettercad/features/Regeneration.hpp>

#include <algorithm>
#include <format>
#include <optional>
#include <variant>

namespace bettercad::features {

namespace {

using geometry::ArcSegment2D;
using geometry::CircleSegment2D;
using geometry::LineSegment2D;
using geometry::ProfileSegment;

// Path edges meet where their ends coincide within the sketch tolerance.
constexpr Length kTolerance = sketch::Sketch::kLengthTolerance;

Point2D startOf(const ProfileSegment& segment) {
    if (const auto* line = std::get_if<LineSegment2D>(&segment)) {
        return line->start;
    }
    return std::get<ArcSegment2D>(segment).start;
}

Point2D endOf(const ProfileSegment& segment) {
    if (const auto* line = std::get_if<LineSegment2D>(&segment)) {
        return line->end;
    }
    return std::get<ArcSegment2D>(segment).end;
}

ProfileSegment reversed(const ProfileSegment& segment) {
    if (const auto* line = std::get_if<LineSegment2D>(&segment)) {
        return LineSegment2D{line->end, line->start};
    }
    const auto& arc = std::get<ArcSegment2D>(segment);
    return ArcSegment2D{arc.center, arc.end, arc.start, !arc.counterClockwise};
}

bool meets(const Point2D& p, const ProfileSegment& segment) {
    return distance(p, startOf(segment)) <= kTolerance || distance(p, endOf(segment)) <= kTolerance;
}

/// An edge's segment in its natural direction (arcs counter-clockwise).
Result<ProfileSegment> edgeSegment(const sketch::Sketch& sketch, EntityId id, std::size_t edgeCount) {
    const sketch::Entity* entity = sketch.findEntity(id);
    if (entity == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("the path edge {} does not exist in sketch '{}'", id, sketch.name()));
    }
    const auto invalid = [&](std::string_view problem) {
        return makeError(ErrorCode::InvalidArgument, std::format("the path edge {} {}", id, problem));
    };
    switch (entity->type()) {
    case sketch::EntityType::Point:
        return invalid("is a point, not a line, arc or circle");
    case sketch::EntityType::Ellipse:
        return invalid("is an ellipse, not a line, arc or circle");
    case sketch::EntityType::Spline:
        return invalid("is a spline, not a line, arc or circle");
    case sketch::EntityType::Circle: {
        if (edgeCount != 1) {
            return invalid("is a circle, which is a closed path on its own and cannot be joined with other edges");
        }
        const Length radius = sketch.radius(id).value();
        if (!(radius > kTolerance)) {
            return invalid("is a circle of zero radius");
        }
        return CircleSegment2D{sketch.center(id).value(), radius, true};
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
                     std::format("the path is not connected: the edges {} and {} do not meet (their nearest ends are "
                                 "{:.6g} mm apart)",
                                 a, b, gap.in(units::mm)));
}

} // namespace

Result<geometry::PlanarPath> resolveSweepPath(const SweepPath& path, const Document& document) {
    const auto* sketch = document.findObjectAs<sketch::Sketch>(ObjectId{path.sketch});
    if (sketch == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("path {} is not a sketch in this document", path.sketch));
    }
    if (path.edges.empty()) {
        return makeError(ErrorCode::InvalidArgument, "the path has no edges");
    }
    std::vector<ProfileSegment> natural;
    for (const EntityId id : path.edges) {
        auto segment = edgeSegment(*sketch, id, path.edges.size());
        if (!segment) {
            return std::unexpected(segment.error());
        }
        natural.push_back(*segment);
    }
    if (natural.size() == 1) {
        return geometry::PlanarPath{sketch->placement(), std::move(natural)};
    }

    // Head to tail: the first edge is turned so that it ends on the second,
    // and every later edge so that it starts where the one before ends.
    std::vector<ProfileSegment> oriented;
    oriented.reserve(natural.size());
    if (meets(endOf(natural[0]), natural[1])) {
        oriented.push_back(natural[0]);
    } else if (meets(startOf(natural[0]), natural[1])) {
        oriented.push_back(reversed(natural[0]));
    } else {
        const Length gap = std::min({distance(startOf(natural[0]), startOf(natural[1])),
                                     distance(startOf(natural[0]), endOf(natural[1])),
                                     distance(endOf(natural[0]), startOf(natural[1])),
                                     distance(endOf(natural[0]), endOf(natural[1]))});
        return disconnected(path.edges[0], path.edges[1], gap);
    }
    for (std::size_t i = 1; i < natural.size(); ++i) {
        const Point2D at = endOf(oriented.back());
        if (distance(startOf(natural[i]), at) <= kTolerance) {
            oriented.push_back(natural[i]);
        } else if (distance(endOf(natural[i]), at) <= kTolerance) {
            oriented.push_back(reversed(natural[i]));
        } else {
            const Length gap = std::min(distance(startOf(natural[i]), at), distance(endOf(natural[i]), at));
            return disconnected(path.edges[i - 1], path.edges[i], gap);
        }
    }
    return geometry::PlanarPath{sketch->placement(), std::move(oriented)};
}

Result<geometry::Body> sweepTool(const SweepFeature& feature, const Document& document) {
    const SweepDefinition& definition = feature.definition();
    const auto prefixed = [&](const Error& error) {
        return makeError(error.code, std::format("{}: {}", feature.name(), error.message));
    };
    // Profile first, then the path, then the solid (checked by makeSweep).
    auto profile = detail::requireProfileSketch(document, definition.profile, feature.name());
    if (!profile) {
        return std::unexpected(profile.error());
    }
    auto regions = detail::labelledProfileRegions(**profile, feature.name());
    if (!regions) {
        return std::unexpected(regions.error());
    }
    auto path = resolveSweepPath(definition.path, document);
    if (!path) {
        return prefixed(path.error());
    }
    // Path segment i is the path's edge i (resolveSweepPath() keeps the order).
    const std::vector<EntityId>& edges = definition.path.edges;
    const detail::PathEdgeOf along = [&edges](std::size_t segment) -> std::optional<EntityId> {
        return segment < edges.size() ? std::optional<EntityId>{edges[segment]} : std::nullopt;
    };
    // SweepOrientation::FollowPath is makeSweep's frame: the only mode.
    auto solid = detail::uniteRegionSolids(*regions, [&](const LabelledRegion& region) {
        return geometry::makeSweep(region.region, *path,
                                   detail::sweptFaceNamer(feature.id(), region, FaceRole::StartCap,
                                                          FaceRole::EndCap, along));
    });
    if (!solid) {
        return prefixed(solid.error());
    }
    return solid;
}

Result<geometry::Body> regenerateSweep(const SweepFeature& feature, const Document& document,
                                       const geometry::Body* target) {
    auto tool = sweepTool(feature, document);
    if (!tool) {
        return std::unexpected(tool.error());
    }
    return combineWithTarget(feature.definition().operation, *tool, target, feature.name());
}

} // namespace bettercad::features
