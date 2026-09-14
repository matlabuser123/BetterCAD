#include "features/SolidSupport.hpp"

#include <bettercad/core/geometry/Sweeps.hpp>
#include <bettercad/core/units/Format.hpp>
#include <bettercad/features/Regeneration.hpp>

#include <format>

namespace bettercad::features {

Result<Length> resolveDepth(const ExtrudeDefinition& definition, const Document& document) {
    Length depth = definition.depth;
    if (definition.depthParameter) {
        auto value = detail::drivingValue<Length>(document, *definition.depthParameter, "depth parameter");
        if (!value) {
            return std::unexpected(value.error());
        }
        depth = *value;
    }
    if (!isFinite(depth) || depth <= Length{}) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("extrude depth must be positive, got {}", toString(depth, units::mm)));
    }
    return depth;
}

Result<geometry::Body> regenerateExtrude(const ExtrudeFeature& feature, const Document& document,
                                         const geometry::Body* target) {
    const ExtrudeDefinition& definition = feature.definition();
    auto profile = detail::requireProfileSketch(document, definition.profile, feature.name());
    if (!profile) {
        return std::unexpected(profile.error());
    }
    auto depth = resolveDepth(definition, document);
    if (!depth) {
        return std::unexpected(depth.error());
    }
    auto regions = detail::profileRegions(**profile, feature.name());
    if (!regions) {
        return std::unexpected(regions.error());
    }

    Length from{};
    Length to = *depth;
    if (definition.direction == ExtrudeDirection::Reversed) {
        from = -*depth;
        to = Length{};
    } else if (definition.direction == ExtrudeDirection::Symmetric) {
        from = -*depth / 2.0;
        to = *depth / 2.0;
    }
    auto solid = detail::uniteRegionSolids(
        *regions, [&](const geometry::PlanarRegion& region) { return geometry::makePrism(region, from, to); });
    if (!solid) {
        return std::unexpected(solid.error());
    }
    return combineWithTarget(definition.operation, *solid, target, feature.name());
}

} // namespace bettercad::features
