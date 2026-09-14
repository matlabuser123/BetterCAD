#include "features/SolidSupport.hpp"

#include <bettercad/core/geometry/Chamfer.hpp>
#include <bettercad/features/Regeneration.hpp>

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
    const auto chamfer = [&](const geometry::Body& body) -> Result<geometry::Body> {
        auto distance = resolveChamferDistance(definition, document);
        if (!distance) {
            return std::unexpected(distance.error());
        }
        const geometry::ChamferRequest request{
            .edges = definition.edges,
            .mode = definition.mode,
            .distance = *distance,
            .distance2 = definition.distance2,
            .angle = definition.angle,
            .referenceSide = definition.referenceSide,
        };
        return geometry::chamferEdges(body, request);
    };
    return detail::applyToTargetBody(feature.name(), "chamfer", target, chamfer);
}

} // namespace bettercad::features
