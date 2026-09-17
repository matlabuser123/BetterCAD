#include "features/SolidSupport.hpp"
#include "features/pattern/PatternSupport.hpp"

#include <bettercad/features/Datums.hpp>
#include <bettercad/features/Regeneration.hpp>

#include <algorithm>
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
    if (definition.symmetric && *count % 2 == 0) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("a symmetric pattern needs an odd count, so the source is its middle instance, "
                                     "got {}",
                                     *count));
    }
    if (auto valid = checkSuppressedAgainstCount(definition.suppressed, *count, "circular pattern"); !valid) {
        return std::unexpected(valid.error());
    }
    return circularPatternInstances(Axis3D{origin, *direction}, *count,
                                    circularPatternStep(definition.spacing, *count, angle, definition.direction),
                                    definition.symmetric, definition.suppressed);
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
        const auto active = static_cast<std::size_t>(
            std::ranges::count_if(*instances, [](const CircularPatternInstance& i) { return !i.suppressed; }));
        if (auto valid = detail::checkNestedCount(*source, document, active, "circular pattern"); !valid) {
            return std::unexpected(valid.error());
        }
        auto apply = detail::instanceOperation(*source, document, "circular pattern");
        if (!apply) {
            return std::unexpected(apply.error());
        }
        std::vector<detail::PatternPlacement> placements;
        for (const CircularPatternInstance& instance : *instances) {
            // Instance 0 is the source's own body; a suppressed instance
            // keeps its index and makes no geometry (P12-PATTERN-001).
            if (instance.index != 0 && !instance.suppressed) {
                placements.push_back({.motion = instance.motion,
                                      .label = std::format("instance {} at {:.6g} deg", instance.index,
                                                           tidy(instance.angle.in(units::deg))),
                                      .instance = static_cast<std::uint32_t>(instance.index)});
            }
        }
        return detail::buildPattern(sourceBody, *apply, placements, feature.id());
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
