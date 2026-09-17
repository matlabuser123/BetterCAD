#include "features/SolidSupport.hpp"
#include "features/pattern/PatternSupport.hpp"

#include <bettercad/core/units/Format.hpp>
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
    const bool total = direction.distribution == PatternDistribution::TotalLength;
    const std::string_view what = total ? "total length" : "spacing";
    PatternStep step{.direction = *unit, .count = *count, .spacing = direction.spacing,
                     .symmetric = direction.symmetric};
    if (direction.spacingParameter) {
        auto value = detail::drivingValue<Length>(document, *direction.spacingParameter,
                                                  total ? "total length parameter" : "spacing parameter");
        if (!value) {
            return makeError(value.error().code, std::format("{}: {}", label, value.error().message));
        }
        step.spacing = *value;
    }
    if (!isFinite(step.spacing) || !(step.spacing > Length{})) {
        return invalid(std::format("the {} must be positive and finite, got {}", what,
                                   toString(step.spacing, units::mm)));
    }
    // The count's own rules, which a driven count only settles here
    // (P12-PATTERN-001).
    if (total && *count < 2) {
        return invalid(std::format("a total length needs at least 2 instances to divide it between, got {}", *count));
    }
    if (direction.symmetric && *count % 2 == 0) {
        return invalid(std::format("a symmetric direction needs an odd count, so the source is its middle instance, "
                                   "got {}",
                                   *count));
    }
    if (total) {
        // The whole row spans the length: the step divides it between the
        // gaps, so no instance's offset depends on the ones before it.
        step.spacing = step.spacing / static_cast<double>(*count - 1);
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
    if (auto valid = checkSuppressedAgainstCount(definition.suppressed, total, "linear pattern"); !valid) {
        return std::unexpected(valid.error());
    }
    return patternInstances(*first, second, definition.suppressed);
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
        const auto active = static_cast<std::size_t>(
            std::ranges::count_if(*instances, [](const PatternInstance& i) { return !i.suppressed; }));
        if (auto valid = detail::checkNestedCount(*source, document, active, "linear pattern"); !valid) {
            return std::unexpected(valid.error());
        }
        auto apply = detail::instanceOperation(*source, document, "linear pattern");
        if (!apply) {
            return std::unexpected(apply.error());
        }
        std::vector<detail::PatternPlacement> placements;
        for (const PatternInstance& instance : *instances) {
            // Instance 0 is the source's own body; a suppressed instance
            // keeps its index and makes no geometry (P12-PATTERN-001).
            if (instance.index != 0 && !instance.suppressed) {
                placements.push_back({.motion = RigidTransform3D::translation(instance.offset),
                                      .label = std::format("instance {} at {}", instance.index,
                                                           formatMm(instance.offset)),
                                      .instance = static_cast<std::uint32_t>(instance.index)});
            }
        }
        return detail::buildPattern(sourceBody, *apply, placements, feature.id());
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
