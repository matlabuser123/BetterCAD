#include "features/SolidSupport.hpp"
#include "features/pattern/PatternSupport.hpp"

#include <bettercad/features/Datums.hpp>
#include <bettercad/features/Regeneration.hpp>

#include <format>
#include <string>

namespace bettercad::features {

namespace {

/// Negative zero as 0, for messages.
double tidy(double value) {
    return value == 0.0 ? 0.0 : value;
}

} // namespace

Result<std::vector<CircularPatternInstance>> resolveCircularPatternInstances(
    const CircularPatternDefinition& definition, const Document& document) {
    const PatternAxis& axis = definition.axis;
    Point3D origin = axis.origin;
    auto direction = Direction3D::fromComponents(axis.direction.x, axis.direction.y, axis.direction.z);
    if (axis.reference) {
        auto resolved = resolveAxis(document, *axis.reference);
        if (!resolved) {
            return std::unexpected(resolved.error());
        }
        origin = resolved->origin;
        direction = resolved->direction;
    }
    if (!direction || !isFinite(origin.x) || !isFinite(origin.y) || !isFinite(origin.z)) {
        return makeError(ErrorCode::InvalidArgument,
                         "the axis needs a finite origin and a finite, non-zero direction");
    }
    auto count = detail::resolvePatternCount(definition.count, definition.countParameter, document, "");
    if (!count) {
        return std::unexpected(count.error());
    }
    Angle angle = definition.angle;
    if (definition.angleParameter && definition.spacing != CircularSpacing::FullCircle) {
        auto value = detail::drivingValue<Angle>(document, *definition.angleParameter, "angle parameter");
        if (!value) {
            return std::unexpected(value.error());
        }
        angle = *value;
    }
    if (auto valid = detail::checkCircularAngle(definition.spacing, *count, angle); !valid) {
        return std::unexpected(valid.error());
    }
    return circularPatternInstances(Axis3D{origin, *direction}, *count,
                                    circularPatternStep(definition.spacing, *count, angle, definition.direction));
}

Result<geometry::Body> regenerateCircularPattern(const CircularPatternFeature& feature, const Document& document,
                                                 const geometry::Body* target) {
    const CircularPatternDefinition& definition = feature.definition();
    const auto build = [&](const geometry::Body& sourceBody) -> Result<geometry::Body> {
        auto instances = resolveCircularPatternInstances(definition, document);
        if (!instances) {
            return std::unexpected(instances.error());
        }
        const DocumentObject* source = document.findObject(ObjectId{definition.source});
        if (source == nullptr) {
            return makeError(ErrorCode::NotFound,
                             std::format("the source {} does not exist", ObjectId{definition.source}));
        }
        auto apply = detail::instanceOperation(*source, document, "circular pattern", "");
        if (!apply) {
            return std::unexpected(apply.error());
        }
        std::vector<detail::PatternPlacement> placements;
        for (const CircularPatternInstance& instance : *instances) {
            if (instance.index != 0) {
                placements.push_back({.motion = instance.motion,
                                      .label = std::format("instance {} at {:.6g} deg", instance.index,
                                                           tidy(instance.angle.in(units::deg)))});
            }
        }
        return detail::buildPattern(sourceBody, *apply, placements);
    };
    const auto pattern = [&](const geometry::Body& sourceBody) -> Result<geometry::Body> {
        auto body = build(sourceBody);
        if (!body) {
            return makeError(body.error().code, std::format("circular pattern: {}", body.error().message));
        }
        return body;
    };
    return detail::applyToTargetBody(feature.name(), "circular pattern", target, pattern, "source");
}

} // namespace bettercad::features
