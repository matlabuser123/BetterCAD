#include "features/SolidSupport.hpp"

#include <bettercad/core/geometry/Booleans.hpp>
#include <bettercad/features/Profiles.hpp>

namespace bettercad::features::detail {

Result<geometry::Body> applyToTargetBody(std::string_view featureName, std::string_view operation,
                                         const geometry::Body* target,
                                         const std::function<Result<geometry::Body>(const geometry::Body&)>& apply,
                                         std::string_view role) {
    if (target == nullptr || target->isEmpty()) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("{}: a {} needs the body of its {} feature", featureName, operation, role));
    }
    auto body = apply(*target);
    if (!body) {
        return makeError(body.error().code, std::format("{}: {}", featureName, body.error().message));
    }
    return body;
}

Result<const sketch::Sketch*> requireProfileSketch(const Document& document, SketchId profile,
                                                   std::string_view featureName) {
    const auto* sketch = document.findObjectAs<sketch::Sketch>(ObjectId{profile});
    if (sketch == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("{}: profile {} is not a sketch in this document", featureName, profile));
    }
    return sketch;
}

Result<std::vector<geometry::PlanarRegion>> profileRegions(const sketch::Sketch& sketch,
                                                           std::string_view featureName) {
    auto regions = extractRegions(sketch);
    if (!regions) {
        return makeError(regions.error().code, std::format("{}: {}", featureName, regions.error().message));
    }
    return regions;
}

Result<std::vector<LabelledRegion>> labelledProfileRegions(const sketch::Sketch& sketch,
                                                           std::string_view featureName) {
    auto regions = extractLabelledRegions(sketch);
    if (!regions) {
        return makeError(regions.error().code, std::format("{}: {}", featureName, regions.error().message));
    }
    return regions;
}

namespace {

template <typename Region>
Result<geometry::Body> unite(const std::vector<Region>& regions,
                             const std::function<Result<geometry::Body>(const Region&)>& build) {
    geometry::Body solid;
    for (const Region& region : regions) {
        auto piece = build(region);
        if (!piece) {
            return std::unexpected(piece.error());
        }
        if (solid.isEmpty()) {
            solid = *piece;
            continue;
        }
        auto united = geometry::booleanUnion(solid, *piece);
        if (!united) {
            return std::unexpected(united.error());
        }
        solid = *united;
    }
    return solid;
}

} // namespace

Result<geometry::Body> uniteRegionSolids(
    const std::vector<geometry::PlanarRegion>& regions,
    const std::function<Result<geometry::Body>(const geometry::PlanarRegion&)>& build) {
    return unite(regions, build);
}

Result<geometry::Body> uniteRegionSolids(const std::vector<LabelledRegion>& regions,
                                         const std::function<Result<geometry::Body>(const LabelledRegion&)>& build) {
    return unite(regions, build);
}

} // namespace bettercad::features::detail
