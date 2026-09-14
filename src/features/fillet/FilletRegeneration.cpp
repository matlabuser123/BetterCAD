#include "features/SolidSupport.hpp"

#include <bettercad/core/geometry/Fillet.hpp>
#include <bettercad/features/Regeneration.hpp>

namespace bettercad::features {

Result<Length> resolveFilletRadius(const FilletDefinition& definition, const Document& document) {
    if (!definition.radiusParameter) {
        return definition.radius;
    }
    return detail::drivingValue<Length>(document, *definition.radiusParameter, "radius parameter");
}

Result<geometry::Body> regenerateFillet(const FilletFeature& feature, const Document& document,
                                        const geometry::Body* target) {
    const FilletDefinition& definition = feature.definition();
    const auto fillet = [&](const geometry::Body& body) -> Result<geometry::Body> {
        auto radius = resolveFilletRadius(definition, document);
        if (!radius) {
            return std::unexpected(radius.error());
        }
        return geometry::filletEdges(body, {.edges = definition.edges, .radius = *radius});
    };
    return detail::applyToTargetBody(feature.name(), "fillet", target, fillet);
}

} // namespace bettercad::features
