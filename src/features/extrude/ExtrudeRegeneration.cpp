#include "features/SolidSupport.hpp"

#include <bettercad/core/geometry/Sweeps.hpp>
#include <bettercad/core/units/Format.hpp>
#include <bettercad/features/Regeneration.hpp>

#include <format>
#include <optional>
#include <utility>

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

Result<geometry::Body> extrudeTool(const ExtrudeFeature& feature, const Document& document) {
    const ExtrudeDefinition& definition = feature.definition();
    auto profile = detail::requireProfileSketch(document, definition.profile, feature.name());
    if (!profile) {
        return std::unexpected(profile.error());
    }
    auto depth = resolveDepth(definition, document);
    if (!depth) {
        return std::unexpected(depth.error());
    }
    auto regions = detail::labelledProfileRegions(**profile, feature.name());
    if (!regions) {
        return std::unexpected(regions.error());
    }

    Length from{};
    Length to = *depth;
    // The start cap lies on the sketch plane (behind it for a symmetric
    // extrude), the end cap at the depth.
    FaceRole first = FaceRole::StartCap;
    FaceRole last = FaceRole::EndCap;
    if (definition.direction == ExtrudeDirection::Reversed) {
        from = -*depth;
        to = Length{};
        std::swap(first, last);
    } else if (definition.direction == ExtrudeDirection::Symmetric) {
        from = -*depth / 2.0;
        to = *depth / 2.0;
    }
    return detail::uniteRegionSolids(*regions, [&](const LabelledRegion& region) {
        return geometry::makePrism(region.region, from, to,
                                   detail::sweptFaceNamer(feature.id(), region, first, last));
    });
}

Result<geometry::Body> regenerateExtrude(const ExtrudeFeature& feature, const Document& document,
                                         const geometry::Body* target) {
    auto tool = extrudeTool(feature, document);
    if (!tool) {
        return std::unexpected(tool.error());
    }
    return combineWithTarget(feature.definition().operation, *tool, target, feature.name());
}

} // namespace bettercad::features
