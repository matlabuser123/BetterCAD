#include "features/SolidSupport.hpp"

#include <bettercad/core/geometry/Sweeps.hpp>
#include <bettercad/core/math/BoundingBox.hpp>
#include <bettercad/core/units/Format.hpp>
#include <bettercad/features/Regeneration.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <limits>
#include <optional>
#include <utility>

namespace bettercad::features {

namespace {

// A through-all tool reaches this far beyond the target body's bounds, so
// that its caps lie clear of the body's faces (P12-FEAT-001).
const Length kThroughAllClearance = Length::fromSi(1e-3);
// A body no further than this from the sketch plane on one side lies on it
// (the kernel's confusion tolerance, 1e-7 mm).
constexpr double kOnPlaneSi = 1e-10;

/// Where a tool starts and ends along its sketch plane's normal.
struct Extent {
    Length from;
    Length to;
};

/// The extent along @p normal, from @p origin, of a tool that reaches
/// through @p box in @p direction (both ways when symmetric), with the
/// clearance beyond it. Fails when the box lies wholly on the side the tool
/// does not go to.
Result<Extent> throughAllExtent(const BoundingBox3D& box, const Point3D& origin, const Direction3D& normal,
                                ExtrudeDirection direction, std::string_view name) {
    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    for (int corner = 0; corner < 8; ++corner) {
        const double x = ((corner & 1) != 0 ? box.max.x : box.min.x).si() - origin.x.si();
        const double y = ((corner & 2) != 0 ? box.max.y : box.min.y).si() - origin.y.si();
        const double z = ((corner & 4) != 0 ? box.max.z : box.min.z).si() - origin.z.si();
        const double along = x * normal.x() + y * normal.y() + z * normal.z();
        lo = std::min(lo, along);
        hi = std::max(hi, along);
    }
    const double clearance = kThroughAllClearance.si();
    switch (direction) {
    case ExtrudeDirection::Normal:
        if (!(hi > kOnPlaneSi)) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("{}: the target lies wholly behind the sketch plane, so cutting through "
                                         "all along its normal removes nothing",
                                         name));
        }
        return Extent{Length{}, Length::fromSi(hi + clearance)};
    case ExtrudeDirection::Reversed:
        if (!(lo < -kOnPlaneSi)) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("{}: the target lies wholly in front of the sketch plane, so cutting "
                                         "through all against its normal removes nothing",
                                         name));
        }
        return Extent{Length::fromSi(lo - clearance), Length{}};
    case ExtrudeDirection::Symmetric:
        return Extent{Length::fromSi(std::min(lo, 0.0) - clearance), Length::fromSi(std::max(hi, 0.0) + clearance)};
    }
    return makeError(ErrorCode::Internal, std::format("{}: unknown extrude direction", name));
}

/// Where the tool of @p feature, sketched on @p plane, starts and ends.
Result<Extent> extrudeExtent(const ExtrudeFeature& feature, const Document& document, const Frame3D& plane,
                             const geometry::Body* target, const RigidTransform3D& placement) {
    const ExtrudeDefinition& definition = feature.definition();
    if (definition.termination == ExtrudeTermination::ThroughAll) {
        if (target == nullptr || target->isEmpty()) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("{}: a through-all extrude needs the body of its target feature",
                                         feature.name()));
        }
        auto box = target->boundingBox();
        if (!box) {
            return makeError(box.error().code, std::format("{}: {}", feature.name(), box.error().message));
        }
        return throughAllExtent(*box, placement.apply(plane.origin()), placement.apply(plane.normal()),
                                definition.direction, feature.name());
    }
    auto depth = resolveDepth(definition, document);
    if (!depth) {
        return std::unexpected(depth.error());
    }
    switch (definition.direction) {
    case ExtrudeDirection::Normal:
        return Extent{Length{}, *depth};
    case ExtrudeDirection::Reversed:
        return Extent{-*depth, Length{}};
    case ExtrudeDirection::Symmetric:
        return Extent{-*depth / 2.0, *depth / 2.0};
    }
    return makeError(ErrorCode::Internal, std::format("{}: unknown extrude direction", feature.name()));
}

} // namespace

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

Result<geometry::Body> extrudeTool(const ExtrudeFeature& feature, const Document& document,
                                   const geometry::Body* target, const RigidTransform3D& placement) {
    const ExtrudeDefinition& definition = feature.definition();
    auto profile = detail::requireProfileSketch(document, definition.profile, feature.name());
    if (!profile) {
        return std::unexpected(profile.error());
    }
    auto extent = extrudeExtent(feature, document, (**profile).placement(), target, placement);
    if (!extent) {
        return std::unexpected(extent.error());
    }
    auto regions = detail::labelledProfileRegions(**profile, feature.name());
    if (!regions) {
        return std::unexpected(regions.error());
    }

    // The start cap lies on the sketch plane (behind it for a symmetric
    // extrude), the end cap at the far end.
    FaceRole first = FaceRole::StartCap;
    FaceRole last = FaceRole::EndCap;
    if (definition.direction == ExtrudeDirection::Reversed) {
        std::swap(first, last);
    }
    return detail::uniteRegionSolids(*regions, [&](const LabelledRegion& region) {
        return geometry::makePrism(region.region, extent->from, extent->to,
                                   detail::sweptFaceNamer(feature.id(), region, first, last));
    });
}

Result<geometry::Body> regenerateExtrude(const ExtrudeFeature& feature, const Document& document,
                                         const geometry::Body* target) {
    auto tool = extrudeTool(feature, document, target);
    if (!tool) {
        return std::unexpected(tool.error());
    }
    return combineWithTarget(feature.definition().operation, *tool, target, feature.name());
}

} // namespace bettercad::features
