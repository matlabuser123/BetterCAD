#include "features/SolidSupport.hpp"

#include <bettercad/core/geometry/Sweeps.hpp>
#include <bettercad/features/Regeneration.hpp>

#include <algorithm>
#include <format>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

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

Result<geometry::SweptPath> resolveSweptPath(const SweepDefinition& definition, const Document& document) {
    const auto runsOf = [&document](const SweepPath& path,
                                    std::string_view what) -> Result<std::vector<geometry::PlanarPath>> {
        std::vector<geometry::PlanarPath> runs;
        auto firstRun = resolveSweepPath(SweepPath{.sketch = path.sketch, .edges = path.edges}, document);
        if (!firstRun) {
            return std::unexpected(firstRun.error());
        }
        runs.push_back(std::move(*firstRun));
        for (std::size_t i = 0; i < path.runs.size(); ++i) {
            auto run = resolveSweepPath(SweepPath{.sketch = path.runs[i].sketch, .edges = path.runs[i].edges},
                                        document);
            if (!run) {
                return makeError(run.error().code,
                                 std::format("{} run {}: {}", what, i + 2, run.error().message));
            }
            runs.push_back(std::move(*run));
        }
        return runs;
    };

    auto runs = runsOf(definition.path, "the path's");
    if (!runs) {
        return std::unexpected(runs.error());
    }
    geometry::SweptPath path{.runs = std::move(*runs)};
    if (definition.guide) {
        auto guide = runsOf(*definition.guide, "the guide's");
        if (!guide) {
            return std::unexpected(guide.error());
        }
        path.guide = std::move(*guide);
    }
    if (definition.twistParameter) {
        auto value = detail::drivingValue<Angle>(document, *definition.twistParameter, "twist parameter");
        if (!value) {
            return std::unexpected(value.error());
        }
        path.twist = *value;
    } else {
        path.twist = definition.twist;
    }
    if (!isFinite(path.twist)) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the twist must be finite, got {}", toString(path.twist, units::deg)));
    }
    return path;
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
    auto path = resolveSweptPath(definition, document);
    if (!path) {
        return prefixed(path.error());
    }
    // Path segment i is the path's edge i, counted across the runs in the
    // order of travel (resolveSweptPath() keeps that order), so a side face
    // names the edge it actually runs along whichever sketch drew it.
    // A path in one sketch names its edges as it always did; one that runs
    // through several names the sketch too, since entity IDs repeat between
    // sketches (P12-SWEEP-001).
    const bool several = !definition.path.runs.empty();
    std::vector<detail::PathEdge> edges;
    for (const EntityId edge : definition.path.edges) {
        edges.push_back({.sketch = several ? std::optional<SketchId>{definition.path.sketch} : std::nullopt,
                         .edge = edge});
    }
    for (const SweepPathRun& run : definition.path.runs) {
        for (const EntityId edge : run.edges) {
            edges.push_back({.sketch = run.sketch, .edge = edge});
        }
    }
    const detail::PathEdgeOf along = [edges = std::move(edges)](std::size_t segment) -> std::optional<detail::PathEdge> {
        return segment < edges.size() ? std::optional<detail::PathEdge>{edges[segment]} : std::nullopt;
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
