#include "features/SolidSupport.hpp"

#include <bettercad/core/geometry/Draft.hpp>
#include <bettercad/features/Datums.hpp>
#include <bettercad/features/Regeneration.hpp>

#include <format>

namespace bettercad::features {

Result<Angle> resolveDraftAngle(const DraftDefinition& definition, const Document& document) {
    if (!definition.angleParameter) {
        return definition.angle;
    }
    return detail::drivingValue<Angle>(document, *definition.angleParameter, "angle parameter");
}

Result<geometry::Body> regenerateDraft(const DraftFeature& feature, const Document& document,
                                       const geometry::Body* target, const BodyLookup& bodies) {
    const DraftDefinition& definition = feature.definition();
    const auto draft = [&](const geometry::Body& body) -> Result<geometry::Body> {
        auto angle = resolveDraftAngle(definition, document);
        if (!angle) {
            return std::unexpected(angle.error());
        }
        auto plane = resolvePlane(document, definition.neutralPlane, bodies);
        if (!plane) {
            return makeError(plane.error().code, std::format("the neutral plane: {}", plane.error().message));
        }
        // Each face is named by a feature that names its faces, and is a face
        // of this body: nothing else is taken in its place.
        if (auto named = detail::requireNamedFaces(document, definition.faces, body, definition.target, "face");
            !named) {
            return std::unexpected(named.error());
        }
        return geometry::draftFaces(body, {.faces = definition.faces, .neutralPlane = *plane, .angle = *angle});
    };
    return detail::applyToTargetBody(feature.name(), "draft", target, draft);
}

} // namespace bettercad::features
