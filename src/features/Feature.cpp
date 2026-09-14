#include <bettercad/core/geometry/Booleans.hpp>
#include <bettercad/features/Feature.hpp>

#include <format>

namespace bettercad::features {

std::string_view toString(FeatureOperation operation) noexcept {
    switch (operation) {
    case FeatureOperation::NewBody:
        return "new body";
    case FeatureOperation::Join:
        return "join";
    case FeatureOperation::Cut:
        return "cut";
    case FeatureOperation::Intersect:
        return "intersect";
    }
    return "unknown";
}

Result<void> validateOperation(FeatureOperation operation, const std::optional<FeatureId>& target) {
    const bool needsTarget = operation != FeatureOperation::NewBody;
    if (needsTarget && (!target || !target->isValid())) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("a {} feature needs a target feature", toString(operation)));
    }
    if (!needsTarget && target) {
        return makeError(ErrorCode::InvalidArgument, "a new-body feature takes no target feature");
    }
    return {};
}

Result<geometry::Body> combineWithTarget(FeatureOperation operation, const geometry::Body& tool,
                                         const geometry::Body* target, std::string_view featureName) {
    if (operation == FeatureOperation::NewBody) {
        return tool;
    }
    if (target == nullptr || target->isEmpty()) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("{}: a {} feature needs the body of its target feature", featureName,
                                     toString(operation)));
    }
    Result<geometry::Body> combined = tool;
    switch (operation) {
    case FeatureOperation::Join:
        combined = geometry::booleanUnion(*target, tool);
        break;
    case FeatureOperation::Cut:
        combined = geometry::booleanDifference(*target, tool);
        break;
    case FeatureOperation::Intersect:
        combined = geometry::booleanIntersection(*target, tool);
        break;
    case FeatureOperation::NewBody:
        break;
    }
    if (!combined) {
        // Kernel failures name the feature they happened in.
        return makeError(combined.error().code, std::format("{}: {}", featureName, combined.error().message));
    }
    return combined;
}

} // namespace bettercad::features
