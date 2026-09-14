#include "features/SolidSupport.hpp"

#include <bettercad/core/geometry/Chamfer.hpp>
#include <bettercad/features/Regeneration.hpp>

#include <format>

namespace bettercad::features {

Result<Length> resolveChamferDistance(const ChamferDefinition& definition, const Document& document) {
    if (!definition.distanceParameter) {
        return definition.distance;
    }
    return detail::drivingValue<Length>(document, *definition.distanceParameter, "distance parameter");
}

Result<geometry::Body> regenerateChamfer(const ChamferFeature& feature, const Document& document,
                                         const geometry::Body* target) {
    const ChamferDefinition& definition = feature.definition();
    const auto prefixed = [&](const Error& error) {
        return makeError(error.code, std::format("{}: {}", feature.name(), error.message));
    };
    if (target == nullptr || target->isEmpty()) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("{}: a chamfer needs the body of its target feature", feature.name()));
    }
    auto distance = resolveChamferDistance(definition, document);
    if (!distance) {
        return prefixed(distance.error());
    }
    const geometry::ChamferRequest request{
        .edges = definition.edges,
        .mode = definition.mode,
        .distance = *distance,
        .distance2 = definition.distance2,
        .angle = definition.angle,
        .referenceSide = definition.referenceSide,
    };
    // The target's body is never modified: the chamfer builds a new body,
    // which the regenerator commits only if this succeeds.
    auto body = geometry::chamferEdges(*target, request);
    if (!body) {
        return prefixed(body.error());
    }
    return body;
}

} // namespace bettercad::features
