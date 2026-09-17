#include <bettercad/core/geometry/Booleans.hpp>
#include <bettercad/core/geometry/Split.hpp>
#include <bettercad/features/Datums.hpp>
#include <bettercad/features/Regeneration.hpp>

#include <format>
#include <string>
#include <utility>

namespace bettercad::features {

namespace {

/// "Name (object:7)".
std::string label(const Document& document, FeatureId id) {
    const ObjectId object{id};
    const auto name = document.nameOf(object);
    return name ? std::format("{} ({})", *name, object) : std::format("{}", object);
}

geometry::BooleanOperation booleanOf(FeatureOperation operation) {
    switch (operation) {
    case FeatureOperation::Cut:
        return geometry::BooleanOperation::Difference;
    case FeatureOperation::Intersect:
        return geometry::BooleanOperation::Intersection;
    case FeatureOperation::Join:
    case FeatureOperation::NewBody:
        break;
    }
    return geometry::BooleanOperation::Union;
}

/// "cutting Pin (object:5)", "intersecting with Pin (object:5)".
std::string step(FeatureOperation operation, const std::string& tool) {
    switch (operation) {
    case FeatureOperation::Cut:
        return std::format("cutting {}", tool);
    case FeatureOperation::Intersect:
        return std::format("intersecting with {}", tool);
    case FeatureOperation::Join:
    case FeatureOperation::NewBody:
        break;
    }
    return std::format("joining {}", tool);
}

} // namespace

Result<geometry::Body> regenerateSplit(const SplitFeature& feature, const Document& document,
                                       const geometry::Body* target, const BodyLookup& bodies) {
    const SplitDefinition& definition = feature.definition();
    if (target == nullptr || target->isEmpty()) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("{}: a split needs the body of its target feature", feature.name()));
    }
    auto plane = resolvePlane(document, definition.plane, bodies);
    if (!plane) {
        return makeError(plane.error().code,
                         std::format("{}: the split plane: {}", feature.name(), plane.error().message));
    }
    auto split = geometry::splitBody(*target, *plane, definition.keep);
    if (!split) {
        return makeError(split.error().code, std::format("{}: {}", feature.name(), split.error().message));
    }
    return split;
}

Result<geometry::Body> regenerateCombine(const CombineFeature& feature, const Document& document,
                                         const geometry::Body* target, const BodyLookup& bodies) {
    const CombineDefinition& definition = feature.definition();
    if (target == nullptr || target->isEmpty()) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("{}: a combine needs the body of its target feature", feature.name()));
    }
    geometry::Body result = *target;
    for (std::size_t i = 0; i < definition.tools.size(); ++i) {
        const std::string tool = label(document, definition.tools[i]);
        const geometry::Body* body = bodies ? bodies(ObjectId{definition.tools[i]}) : nullptr;
        if (body == nullptr || body->isEmpty()) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("{}: tool {}, {}, has no body", feature.name(), i + 1, tool));
        }
        auto next = geometry::booleanOperation(booleanOf(definition.operation), result, *body);
        if (!next) {
            return makeError(next.error().code, std::format("{}: {}", feature.name(), next.error().message));
        }
        if (next->isEmpty()) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("{}: nothing is left after {}", feature.name(),
                                         step(definition.operation, tool)));
        }
        result = std::move(*next);
    }
    return result;
}

} // namespace bettercad::features
