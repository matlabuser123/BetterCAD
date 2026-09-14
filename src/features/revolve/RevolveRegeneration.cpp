#include "features/SolidSupport.hpp"

#include <bettercad/core/geometry/Sweeps.hpp>
#include <bettercad/core/units/Format.hpp>
#include <bettercad/features/Regeneration.hpp>

#include <format>
#include <variant>

namespace bettercad::features {

Result<Angle> resolveAngle(const RevolveDefinition& definition, const Document& document) {
    Angle angle = definition.angle;
    if (definition.angleParameter) {
        auto value = detail::drivingValue<Angle>(document, *definition.angleParameter, "angle parameter");
        if (!value) {
            return std::unexpected(value.error());
        }
        angle = *value;
    }
    if (auto valid = validateRevolveAngle(angle); !valid) {
        return std::unexpected(valid.error());
    }
    return angle;
}

Result<Axis3D> resolveAxis(const RevolveAxis& axis, const sketch::Sketch& profile) {
    const Frame3D& placement = profile.placement();
    switch (axis.kind) {
    case RevolveAxisKind::SketchX:
        return Axis3D{placement.origin(), placement.xAxis()};
    case RevolveAxisKind::SketchY:
        return Axis3D{placement.origin(), placement.yAxis()};
    case RevolveAxisKind::Line:
        break;
    }
    const sketch::Entity* entity = profile.findEntity(axis.line);
    if (entity == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("the axis line {} does not exist in sketch '{}'", axis.line, profile.name()));
    }
    if (!std::holds_alternative<sketch::LineEntity>(entity->geometry)) {
        return makeError(ErrorCode::InvalidArgument, std::format("the axis {} is a {}, not a line", axis.line,
                                                                 sketch::toString(entity->type())));
    }
    const auto ends = profile.endpoints(axis.line);
    if (!ends) {
        return std::unexpected(ends.error());
    }
    if (distance(ends->start, ends->end) <= sketch::Sketch::kLengthTolerance) {
        return makeError(ErrorCode::InvalidArgument, std::format("the axis line {} has zero length", axis.line));
    }
    const Point3D start = profile.toGlobal(ends->start);
    const Point3D end = profile.toGlobal(ends->end);
    const auto direction =
        Direction3D::fromComponents((end.x - start.x).si(), (end.y - start.y).si(), (end.z - start.z).si());
    if (!direction) {
        return makeError(ErrorCode::InvalidArgument, std::format("the axis line {} has no direction", axis.line));
    }
    return Axis3D{start, *direction};
}

Result<geometry::Body> revolveTool(const RevolveFeature& feature, const Document& document) {
    const RevolveDefinition& definition = feature.definition();
    const auto prefixed = [&](const Error& error) {
        return makeError(error.code, std::format("{}: {}", feature.name(), error.message));
    };
    auto profile = detail::requireProfileSketch(document, definition.profile, feature.name());
    if (!profile) {
        return std::unexpected(profile.error());
    }
    auto angle = resolveAngle(definition, document);
    if (!angle) {
        return prefixed(angle.error());
    }
    auto axis = resolveAxis(definition.axis, **profile);
    if (!axis) {
        return prefixed(axis.error());
    }
    auto regions = detail::profileRegions(**profile, feature.name());
    if (!regions) {
        return std::unexpected(regions.error());
    }

    Angle from{};
    Angle to = *angle;
    if (definition.direction == RevolveDirection::Negative) {
        from = -*angle;
        to = Angle{};
    } else if (definition.direction == RevolveDirection::Symmetric) {
        from = -*angle / 2.0;
        to = *angle / 2.0;
    }
    auto solid = detail::uniteRegionSolids(*regions, [&](const geometry::PlanarRegion& region) {
        return geometry::makeRevolution(region, *axis, from, to);
    });
    if (!solid) {
        return prefixed(solid.error());
    }
    return solid;
}

Result<geometry::Body> regenerateRevolve(const RevolveFeature& feature, const Document& document,
                                         const geometry::Body* target) {
    auto tool = revolveTool(feature, document);
    if (!tool) {
        return std::unexpected(tool.error());
    }
    return combineWithTarget(feature.definition().operation, *tool, target, feature.name());
}

} // namespace bettercad::features
