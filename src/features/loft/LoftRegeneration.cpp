#include "features/SolidSupport.hpp"

#include <bettercad/core/geometry/Sweeps.hpp>
#include <bettercad/features/Profiles.hpp>
#include <bettercad/features/Regeneration.hpp>

#include <format>
#include <string>
#include <utility>

namespace bettercad::features {

Result<std::vector<geometry::PlanarRegion>> resolveLoftSections(const LoftDefinition& definition,
                                                                const Document& document) {
    std::vector<geometry::PlanarRegion> regions;
    for (std::size_t i = 0; i < definition.sections.size(); ++i) {
        const LoftSection& section = definition.sections[i];
        const auto* sketch = document.findObjectAs<sketch::Sketch>(ObjectId{section.sketch});
        if (sketch == nullptr) {
            return makeError(ErrorCode::NotFound,
                             std::format("section {}: {} is not a sketch in this document", i + 1, section.sketch));
        }
        const std::string where = std::format("section {} (sketch '{}')", i + 1, sketch->name());
        const auto failed = [&](ErrorCode code, std::string_view problem) {
            return makeError(code, std::format("{}: {}", where, problem));
        };
        auto found = extractRegions(*sketch);
        if (!found) {
            return failed(found.error().code, found.error().message);
        }
        if (found->size() != 1) {
            return failed(ErrorCode::FailedPrecondition,
                          std::format("it has {} closed profiles; a loft section must be exactly one", found->size()));
        }
        geometry::PlanarRegion region = std::move(found->front());
        if (!region.holes.empty()) {
            return failed(ErrorCode::FailedPrecondition,
                          "its profile has a hole; loft sections must be single closed profiles");
        }
        Length offset = section.offset;
        if (section.offsetParameter) {
            auto value = detail::drivingValue<Length>(document, *section.offsetParameter, "offset parameter");
            if (!value) {
                return failed(value.error().code, value.error().message);
            }
            offset = *value;
        }
        if (offset != Length{}) {
            const Frame3D& plane = region.plane;
            const Direction3D& n = plane.normal();
            const Point3D& o = plane.origin();
            auto moved = Frame3D::fromAxes(Point3D{o.x + offset * n.x(), o.y + offset * n.y(), o.z + offset * n.z()},
                                           plane.xAxis(), plane.yAxis(), n);
            if (!moved) {
                return failed(moved.error().code, moved.error().message);
            }
            region.plane = *moved;
        }
        regions.push_back(std::move(region));
    }
    return regions;
}

Result<geometry::Body> loftTool(const LoftFeature& feature, const Document& document) {
    const auto prefixed = [&](const Error& error) {
        return makeError(error.code, std::format("{}: {}", feature.name(), error.message));
    };
    auto sections = resolveLoftSections(feature.definition(), document);
    if (!sections) {
        return prefixed(sections.error());
    }
    // LoftInterpolation::Ruled is makeLoft's interpolation: the only mode.
    auto solid = geometry::makeLoft(*sections);
    if (!solid) {
        return prefixed(solid.error());
    }
    return solid;
}

Result<geometry::Body> regenerateLoft(const LoftFeature& feature, const Document& document,
                                      const geometry::Body* target) {
    auto tool = loftTool(feature, document);
    if (!tool) {
        return std::unexpected(tool.error());
    }
    return combineWithTarget(feature.definition().operation, *tool, target, feature.name());
}

} // namespace bettercad::features
