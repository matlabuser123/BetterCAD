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
    const ObjectId self = feature.id();
    return detail::uniteRegionSolids(*regions, [&](const LabelledRegion& region) {
        const auto namer = [&](const geometry::PrismFace& face) -> std::optional<FaceName> {
            switch (face.kind) {
            case geometry::PrismFace::Kind::First:
                return FaceName{self, {first, std::nullopt}};
            case geometry::PrismFace::Kind::Last:
                return FaceName{self, {last, std::nullopt}};
            case geometry::PrismFace::Kind::Side:
                break;
            }
            const std::vector<EntityId>* entities =
                face.loop == 0 ? &region.outer
                               : (face.loop <= region.holes.size() ? &region.holes[face.loop - 1] : nullptr);
            if (entities == nullptr || face.segment >= entities->size()) {
                return std::nullopt;
            }
            return FaceName{self, {FaceRole::Side, (*entities)[face.segment]}};
        };
        return geometry::makePrism(region.region, from, to, namer);
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
