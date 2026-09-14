#include <bettercad/core/geometry/Booleans.hpp>
#include <bettercad/core/geometry/Sweeps.hpp>
#include <bettercad/core/units/Format.hpp>
#include <bettercad/features/Profiles.hpp>
#include <bettercad/features/Regeneration.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <format>

namespace bettercad::features {

Result<Length> resolveDepth(const ExtrudeDefinition& definition, const Document& document) {
    Length depth = definition.depth;
    if (definition.depthParameter) {
        const Parameter* parameter = document.parameters().find(*definition.depthParameter);
        if (parameter == nullptr) {
            return makeError(ErrorCode::NotFound,
                             std::format("depth parameter {} does not exist", *definition.depthParameter));
        }
        auto value = parameter->as<Length>();
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
    const auto* profile = document.findObjectAs<sketch::Sketch>(ObjectId{definition.profile});
    if (profile == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("{}: profile {} is not a sketch in this document", feature.name(),
                                     definition.profile));
    }
    auto depth = resolveDepth(definition, document);
    if (!depth) {
        return std::unexpected(depth.error());
    }
    auto regions = extractRegions(*profile);
    if (!regions) {
        return makeError(regions.error().code,
                         std::format("{}: {}", feature.name(), regions.error().message));
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

    // One prism per region; disjoint regions give a body with several solids.
    geometry::Body solid;
    for (const geometry::PlanarRegion& region : *regions) {
        auto prism = geometry::makePrism(region, from, to);
        if (!prism) {
            return std::unexpected(prism.error());
        }
        if (solid.isEmpty()) {
            solid = *prism;
            continue;
        }
        auto united = geometry::booleanUnion(solid, *prism);
        if (!united) {
            return std::unexpected(united.error());
        }
        solid = *united;
    }

    switch (definition.operation) {
    case FeatureOperation::NewBody:
        return solid;
    case FeatureOperation::Join:
    case FeatureOperation::Cut:
    case FeatureOperation::Intersect:
        break;
    }
    if (target == nullptr || target->isEmpty()) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("{}: a {} extrude needs the body of its target feature", feature.name(),
                                     toString(definition.operation)));
    }
    switch (definition.operation) {
    case FeatureOperation::Join:
        return geometry::booleanUnion(*target, solid);
    case FeatureOperation::Cut:
        return geometry::booleanDifference(*target, solid);
    case FeatureOperation::Intersect:
        return geometry::booleanIntersection(*target, solid);
    case FeatureOperation::NewBody:
        break;
    }
    return solid;
}

} // namespace bettercad::features
