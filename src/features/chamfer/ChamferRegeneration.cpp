#include "features/SolidSupport.hpp"

#include <bettercad/core/geometry/Chamfer.hpp>
#include <bettercad/features/Regeneration.hpp>

namespace bettercad::features {

geometry::ChamferFaceNamer chamferFaceNamer(ObjectId chamfer, std::vector<FaceCopy> copies) {
    return [chamfer, copies = std::move(copies)](std::size_t reference) -> std::optional<FaceName> {
        return FaceName{chamfer, FaceSelector{.role = FaceRole::Chamfer,
                                              .edge = static_cast<std::uint32_t>(reference + 1),
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
        .edges = definition.edges,
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
        return geometry::chamferEdges(body, *request, chamferFaceNamer(feature.id(), {}));
    };
    return detail::applyToTargetBody(feature.name(), "chamfer", target, chamfer);
}

} // namespace bettercad::features
