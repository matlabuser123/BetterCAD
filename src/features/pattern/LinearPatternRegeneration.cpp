#include "features/SolidSupport.hpp"
#include "features/pattern/PatternSupport.hpp"

#include <bettercad/core/units/Format.hpp>
#include <bettercad/features/Regeneration.hpp>

#include <format>
#include <string>

namespace bettercad::features {

namespace {

/// Negative zero as 0, for messages.
double tidy(double value) {
    return value == 0.0 ? 0.0 : value;
}

std::string formatMm(const Translation3D& t) {
    return std::format("({:.6g}, {:.6g}, {:.6g}) mm", tidy(t.x.in(units::mm)), tidy(t.y.in(units::mm)),
                       tidy(t.z.in(units::mm)));
}

Result<PatternStep> resolveStep(const PatternDirection& direction, const Document& document, std::string_view label) {
    const auto invalid = [&](const std::string& message) {
        return makeError(ErrorCode::InvalidArgument, std::format("{}: {}", label, message));
    };
    const auto unit = Direction3D::fromComponents(direction.direction.x, direction.direction.y, direction.direction.z);
    if (!unit) {
        return invalid("the direction must be a finite, non-zero vector");
    }
    auto count =
        detail::resolvePatternCount(direction.count, direction.countParameter, document, std::format("{}: ", label));
    if (!count) {
        return std::unexpected(count.error());
    }
    PatternStep step{.direction = *unit, .count = *count, .spacing = direction.spacing};
    if (direction.spacingParameter) {
        auto value = detail::drivingValue<Length>(document, *direction.spacingParameter, "spacing parameter");
        if (!value) {
            return makeError(value.error().code, std::format("{}: {}", label, value.error().message));
        }
        step.spacing = *value;
    }
    if (!isFinite(step.spacing) || !(step.spacing > Length{})) {
        return invalid(std::format("the spacing must be positive and finite, got {}", toString(step.spacing, units::mm)));
    }
    return step;
}

} // namespace

Result<std::vector<PatternInstance>> resolvePatternInstances(const LinearPatternDefinition& definition,
                                                             const Document& document) {
    auto first = resolveStep(definition.first, document, "direction 1");
    if (!first) {
        return std::unexpected(first.error());
    }
    std::optional<PatternStep> second;
    if (definition.second) {
        auto step = resolveStep(*definition.second, document, "direction 2");
        if (!step) {
            return std::unexpected(step.error());
        }
        second = *step;
    }
    const std::size_t total = first->count * (second ? second->count : 1);
    if (total > kMaxPatternInstances) {
        return makeError(ErrorCode::InvalidArgument,
                         second ? std::format("a linear pattern may have at most {} instances, got {} ({} x {})",
                                              kMaxPatternInstances, total, first->count, second->count)
                                : std::format("a linear pattern may have at most {} instances, got {}",
                                              kMaxPatternInstances, total));
    }
    return patternInstances(*first, second);
}

Result<geometry::Body> regenerateLinearPattern(const LinearPatternFeature& feature, const Document& document,
                                               const geometry::Body* target) {
    const LinearPatternDefinition& definition = feature.definition();
    const auto build = [&](const geometry::Body& sourceBody) -> Result<geometry::Body> {
        auto instances = resolvePatternInstances(definition, document);
        if (!instances) {
            return std::unexpected(instances.error());
        }
        const DocumentObject* source = document.findObject(ObjectId{definition.source});
        if (source == nullptr) {
            return makeError(ErrorCode::NotFound,
                             std::format("the source {} does not exist", ObjectId{definition.source}));
        }
        auto apply = detail::instanceOperation(*source, document, "linear pattern",
                                               "; use a second direction for a grid");
        if (!apply) {
            return std::unexpected(apply.error());
        }
        std::vector<detail::PatternPlacement> placements;
        for (const PatternInstance& instance : *instances) {
            if (instance.index != 0) {
                placements.push_back({.motion = RigidTransform3D::translation(instance.offset),
                                      .label = std::format("instance {} at {}", instance.index,
                                                           formatMm(instance.offset))});
            }
        }
        return detail::buildPattern(sourceBody, *apply, placements);
    };
    const auto pattern = [&](const geometry::Body& sourceBody) -> Result<geometry::Body> {
        auto body = build(sourceBody);
        if (!body) {
            return makeError(body.error().code, std::format("linear pattern: {}", body.error().message));
        }
        return body;
    };
    return detail::applyToTargetBody(feature.name(), "linear pattern", target, pattern, "source");
}

} // namespace bettercad::features
