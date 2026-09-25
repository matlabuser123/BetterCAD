#include "features/SolidSupport.hpp"

#include <bettercad/core/geometry/Chamfer.hpp>
#include <bettercad/features/Regeneration.hpp>

namespace bettercad::features {

geometry::ChamferFaceNamer chamferFaceNamer(ObjectId chamfer, std::vector<ChamferEdgeId> ids,
                                            std::vector<FaceCopy> copies) {
    return [chamfer, ids = std::move(ids),
            copies = std::move(copies)](std::size_t reference) -> std::optional<FaceName> {
        // The kernel hands back the INDEX of the request edge it cut this face
        // for. That index is translated into the selection's own id here, and
        // this is the only place the translation happens. An index the ids do
        // not cover would be a request built from something other than the
        // definition, so the face is left unnamed rather than misnamed.
        if (reference >= ids.size()) {
            return std::nullopt;
        }
        return FaceName{chamfer, FaceSelector{.role = FaceRole::Chamfer,
                                              .edge = ids[reference],
                                              .copies = copies}};
    };
}

Result<Length> resolveChamferDistance(const ChamferDefinition& definition, const Document& document) {
    if (!definition.distanceParameter) {
        return definition.distance;
    }
    return detail::drivingValue<Length>(document, *definition.distanceParameter, "distance parameter");
}

Result<geometry::ChamferRequest> resolveChamferRequest(const ChamferDefinition& definition, const Document& document) {
    auto distance = resolveChamferDistance(definition, document);
    if (!distance) {
        return std::unexpected(distance.error());
    }
    return geometry::ChamferRequest{
        .edges = chamferCurves(definition),
        .mode = definition.mode,
        .distance = *distance,
        .distance2 = definition.distance2,
        .angle = definition.angle,
        .referenceSide = definition.referenceSide,
    };
}

Result<geometry::Body> regenerateChamfer(const ChamferFeature& feature, const Document& document,
                                         const geometry::Body* target) {
    const auto chamfer = [&](const geometry::Body& body) -> Result<geometry::Body> {
        auto request = resolveChamferRequest(feature.definition(), document);
        if (!request) {
            return std::unexpected(request.error());
        }
        return geometry::chamferEdges(body, *request,
                                      chamferFaceNamer(feature.id(),
                                                       chamferEdgeIds(feature.definition()), {}));
    };
    return detail::applyToTargetBody(feature.name(), "chamfer", target, chamfer);
}

} // namespace bettercad::features
