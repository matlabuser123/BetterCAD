#include "features/SolidSupport.hpp"

#include <bettercad/core/geometry/Hole.hpp>
#include <bettercad/features/Regeneration.hpp>

#include <format>

namespace bettercad::features {

geometry::HoleFaceNamer holeFaceNamer(ObjectId hole, std::vector<FaceCopy> copies) {
    return [hole, copies = std::move(copies)](geometry::HoleFace face) -> std::optional<FaceName> {
        FaceRole role = FaceRole::HoleBottom;
        if (face == geometry::HoleFace::CounterboreFloor) {
            role = FaceRole::CounterboreFloor;
        } else if (face == geometry::HoleFace::SpotfaceFloor) {
            role = FaceRole::SpotfaceFloor;
        }
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
        .spotfaceDiameter = definition.spotfaceDiameter,
        .spotfaceDepth = definition.spotfaceDepth,
    };
    // A threaded or standard clearance hole's diameter is its standard's
    // (P12-HOLE-001).
    if (definition.thread) {
        request.diameter = standards::basicDiameters(definition.thread->size).minor;
        request.thread = geometry::CosmeticThread{.majorDiameter = definition.thread->size.diameter(),
                                                  .length = definition.thread->length};
    } else if (definition.clearance) {
        request.diameter = standards::clearanceHoleDiameter(definition.clearance->bolt, definition.clearance->series);
    }
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
    Length threadLength;
    const std::optional<ParameterId> threadLengthParameter =
        definition.thread ? definition.thread->lengthParameter : std::optional<ParameterId>{};
    for (auto result : {drive(definition.diameterParameter, request.diameter, "diameter parameter"),
                        drive(definition.depthParameter, request.depth, "depth parameter"),
                        drive(definition.centerUParameter, request.center.x, "centre u parameter"),
                        drive(definition.centerVParameter, request.center.y, "centre v parameter"),
                        drive(threadLengthParameter, threadLength, "thread length parameter")}) {
        if (!result) {
            return std::unexpected(result.error());
        }
    }
    if (threadLengthParameter) {
        request.thread->length = threadLength;
    }
    // A tolerance class must be defined for a driven diameter, as it is for a
    // literal one at validation.
    if (definition.tolerance && definition.diameterParameter && isFinite(request.diameter) &&
        request.diameter > Length{}) {
        if (auto deviations = standards::limitDeviations(request.diameter, *definition.tolerance); !deviations) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("the tolerance class {}: {}", standards::toString(*definition.tolerance),
                                         deviations.error().message));
        }
    }
    return request;
}

Result<HoleCallout> holeCallout(const HoleDefinition& definition, const Document& document) {
    auto request = resolveHoleRequest(definition, document);
    if (!request) {
        return std::unexpected(request.error());
    }
    HoleCallout callout{.diameter = request->diameter,
                        .extent = request->extent,
                        .depth = request->extent == geometry::HoleExtent::Blind ? request->depth
                                                                                : Length{},
                        .tolerance = definition.tolerance};
    if (definition.tolerance) {
        auto deviations = standards::limitDeviations(request->diameter, *definition.tolerance);
        if (!deviations) {
            return std::unexpected(deviations.error());
        }
        callout.deviations = *deviations;
    }
    if (definition.thread) {
        const HoleThread& thread = *definition.thread;
        auto limits = standards::internalThreadLimits(thread.size, thread.tolerance);
        if (!limits) {
            return std::unexpected(limits.error());
        }
        std::optional<Length> length;
        if (request->thread->length != Length{}) {
            length = request->thread->length;
        } else if (request->extent == geometry::HoleExtent::Blind) {
            length = request->depth;
        }
        callout.thread = HoleThreadCallout{
            .designation = standards::designation(thread.size, thread.tolerance),
            .basic = standards::basicDiameters(thread.size),
            .limits = *limits,
            .length = length,
        };
    }
    return callout;
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
