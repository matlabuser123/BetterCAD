#include "features/SolidSupport.hpp"

#include <bettercad/core/geometry/VariableFillet.hpp>
#include <bettercad/features/Regeneration.hpp>

#include <format>
#include <string>

namespace bettercad::features {

Result<geometry::VariableFilletRequest> resolveVariableFilletRequest(const VariableFilletDefinition& definition,
                                                                     const Document& document) {
    geometry::VariableFilletRequest request;
    for (std::size_t i = 0; i < definition.edges.size(); ++i) {
        const VariableFilletEdgeDefinition& edge = definition.edges[i];
        geometry::VariableFilletEdge entry{.edge = edge.edge};
        for (std::size_t k = 0; k < edge.stations.size(); ++k) {
            const VariableFilletStation& station = edge.stations[k];
            Length radius = station.radius;
            if (station.radiusParameter) {
                const std::string role = std::format("edge reference {}, station {}: radius parameter", i + 1, k + 1);
                auto driven = detail::drivingValue<Length>(document, *station.radiusParameter, role);
                if (!driven) {
                    return std::unexpected(driven.error());
                }
                radius = *driven;
            }
            entry.stations.push_back({.position = station.position, .radius = radius});
        }
        request.edges.push_back(std::move(entry));
    }
    return request;
}

Result<geometry::Body> regenerateVariableFillet(const VariableFilletFeature& feature, const Document& document,
                                                const geometry::Body* target) {
    const auto fillet = [&](const geometry::Body& body) -> Result<geometry::Body> {
        auto request = resolveVariableFilletRequest(feature.definition(), document);
        if (!request) {
            return std::unexpected(request.error());
        }
        return geometry::variableFilletEdges(body, *request);
    };
    return detail::applyToTargetBody(feature.name(), "variable-radius fillet", target, fillet);
}

} // namespace bettercad::features
