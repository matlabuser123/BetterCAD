#include "features/SolidSupport.hpp"

#include <bettercad/core/geometry/Hole.hpp>
#include <bettercad/features/Regeneration.hpp>

namespace bettercad::features {

geometry::HoleFaceNamer holeFaceNamer(ObjectId hole, std::vector<FaceCopy> copies) {
    return [hole, copies = std::move(copies)](geometry::HoleFace face) -> std::optional<FaceName> {
        const FaceRole role =
            face == geometry::HoleFace::Bottom ? FaceRole::HoleBottom : FaceRole::CounterboreFloor;
        return FaceName{hole, FaceSelector{.role = role, .copies = copies}};
    };
}

Result<geometry::HoleRequest> resolveHoleRequest(const HoleDefinition& definition, const Document& document) {
    geometry::HoleRequest request{
        .face = definition.face,
        .center = definition.center,
        .type = definition.type,
        .extent = definition.extent,
        .diameter = definition.diameter,
        .depth = definition.depth,
        .counterboreDiameter = definition.counterboreDiameter,
        .counterboreDepth = definition.counterboreDepth,
        .countersinkDiameter = definition.countersinkDiameter,
        .countersinkAngle = definition.countersinkAngle,
    };
    const auto drive = [&](const std::optional<ParameterId>& parameter, Length& value,
                           std::string_view role) -> Result<void> {
        if (!parameter) {
            return {};
        }
        auto driven = detail::drivingValue<Length>(document, *parameter, role);
        if (!driven) {
            return std::unexpected(driven.error());
        }
        value = *driven;
        return {};
    };
    for (auto result : {drive(definition.diameterParameter, request.diameter, "diameter parameter"),
                        drive(definition.depthParameter, request.depth, "depth parameter"),
                        drive(definition.centerUParameter, request.center.x, "centre u parameter"),
                        drive(definition.centerVParameter, request.center.y, "centre v parameter")}) {
        if (!result) {
            return std::unexpected(result.error());
        }
    }
    return request;
}

Result<geometry::Body> regenerateHole(const HoleFeature& feature, const Document& document,
                                      const geometry::Body* target) {
    const auto drill = [&](const geometry::Body& body) -> Result<geometry::Body> {
        auto request = resolveHoleRequest(feature.definition(), document);
        if (!request) {
            return std::unexpected(request.error());
        }
        return geometry::cutHole(body, *request, holeFaceNamer(feature.id(), {}));
    };
    return detail::applyToTargetBody(feature.name(), "hole", target, drill);
}

} // namespace bettercad::features
