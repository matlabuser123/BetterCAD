#include "features/pattern/PatternSupport.hpp"

#include "features/SolidSupport.hpp"

#include <bettercad/core/geometry/Booleans.hpp>
#include <bettercad/core/geometry/Chamfer.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/geometry/Fillet.hpp>
#include <bettercad/core/geometry/Hole.hpp>
#include <bettercad/core/geometry/Transform.hpp>
#include <bettercad/core/units/Format.hpp>
#include <bettercad/features/CircularPatternFeature.hpp>
#include <bettercad/features/LinearPatternFeature.hpp>
#include <bettercad/features/Regeneration.hpp>

#include <cmath>
#include <format>
#include <numbers>

namespace bettercad::features::detail {

namespace {

/// Moves the tool of an extrude or revolve to each instance and combines it
/// with the body: new-body and join sources are united, cut sources
/// subtracted.
Result<InstanceOperation> toolOperation(const Result<geometry::Body>& tool, FeatureOperation operation,
                                        std::string_view kind) {
    if (!tool) {
        return std::unexpected(tool.error());
    }
    if (operation == FeatureOperation::Intersect) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("repeating an intersect {} is not supported: its instances would only "
                                     "intersect each other",
                                     kind));
    }
    const bool cut = operation == FeatureOperation::Cut;
    return InstanceOperation{[body = *tool, cut](const geometry::Body& target, const RigidTransform3D& motion,
                                                 const FaceCopy& copy) -> Result<geometry::Body> {
        auto moved = geometry::transformed(body, motion);
        if (!moved) {
            return std::unexpected(moved.error());
        }
        const geometry::Body copied = geometry::renameFaces(*moved, appendCopy(copy));
        return cut ? geometry::booleanDifference(target, copied) : geometry::booleanUnion(target, copied);
    }};
}

/// A through-all cut, its tool rebuilt at each instance: the tool must reach
/// through the body as it lies along the moved sketch normal
/// (P12-FEAT-001). The operation is used while @p extrude and @p document
/// are alive (within the pattern's regeneration).
InstanceOperation throughAllOperation(const ExtrudeFeature& extrude, const Document& document) {
    return [&extrude, &document](const geometry::Body& target, const RigidTransform3D& motion,
                                 const FaceCopy& copy) -> Result<geometry::Body> {
        auto tool = extrudeTool(extrude, document, &target, motion);
        if (!tool) {
            return std::unexpected(tool.error());
        }
        auto moved = geometry::transformed(*tool, motion);
        if (!moved) {
            return std::unexpected(moved.error());
        }
        return geometry::booleanDifference(target, geometry::renameFaces(*moved, appendCopy(copy)));
    };
}

std::vector<geometry::EdgeSignature> movedEdges(const std::vector<geometry::EdgeSignature>& edges,
                                                const RigidTransform3D& motion) {
    std::vector<geometry::EdgeSignature> moved;
    moved.reserve(edges.size());
    for (const geometry::EdgeSignature& edge : edges) {
        moved.push_back(geometry::transformed(edge, motion));
    }
    return moved;
}

} // namespace

Result<InstanceOperation> instanceOperation(const DocumentObject& source, const Document& document,
                                            std::string_view pattern, std::string_view nestingAdvice) {
    if (const auto* extrude = dynamic_cast<const ExtrudeFeature*>(&source)) {
        if (extrude->definition().termination == ExtrudeTermination::ThroughAll) {
            return throughAllOperation(*extrude, document);
        }
        return toolOperation(extrudeTool(*extrude, document), extrude->definition().operation, "extrude");
    }
    if (const auto* revolve = dynamic_cast<const RevolveFeature*>(&source)) {
        return toolOperation(revolveTool(*revolve, document), revolve->definition().operation, "revolve");
    }
    if (const auto* hole = dynamic_cast<const HoleFeature*>(&source)) {
        auto request = resolveHoleRequest(hole->definition(), document);
        if (!request) {
            return std::unexpected(request.error());
        }
        return InstanceOperation{[request = *request, id = hole->id()](
                                     const geometry::Body& target, const RigidTransform3D& motion,
                                     const FaceCopy& copy) {
            return geometry::cutHole(target, geometry::transformed(request, motion), holeFaceNamer(id, {copy}));
        }};
    }
    if (const auto* chamfer = dynamic_cast<const ChamferFeature*>(&source)) {
        auto request = resolveChamferRequest(chamfer->definition(), document);
        if (!request) {
            return std::unexpected(request.error());
        }
        return InstanceOperation{[request = *request, id = chamfer->id()](
                                     const geometry::Body& target, const RigidTransform3D& motion,
                                     const FaceCopy& copy) {
            geometry::ChamferRequest moved = request;
            moved.edges = movedEdges(request.edges, motion);
            if (moved.referenceSide) {
                moved.referenceSide = motion.apply(*moved.referenceSide);
            }
            return geometry::chamferEdges(target, moved, chamferFaceNamer(id, {copy}));
        }};
    }
    if (const auto* fillet = dynamic_cast<const FilletFeature*>(&source)) {
        auto radius = resolveFilletRadius(fillet->definition(), document);
        if (!radius) {
            return std::unexpected(radius.error());
        }
        return InstanceOperation{[edges = fillet->definition().edges, radius = *radius](
                                     const geometry::Body& target, const RigidTransform3D& motion,
                                     const FaceCopy& /*copy: a fillet names no faces*/) {
            return geometry::filletEdges(target, {.edges = movedEdges(edges, motion), .radius = radius});
        }};
    }
    if (dynamic_cast<const VariableFilletFeature*>(&source) != nullptr) {
        // Its stations run along each edge's canonical direction, which a
        // copy's edge may reverse; copies are not supported (P12-FEAT-006).
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("a {} cannot repeat a variable-radius fillet", pattern));
    }
    if (dynamic_cast<const LinearPatternFeature*>(&source) != nullptr ||
        dynamic_cast<const CircularPatternFeature*>(&source) != nullptr) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("a {} cannot repeat another pattern{}", pattern, nestingAdvice));
    }
    return makeError(ErrorCode::FailedPrecondition,
                     std::format("a {} cannot repeat a {}", pattern, source.typeName()));
}

Result<geometry::Body> buildPattern(const geometry::Body& sourceBody, const InstanceOperation& apply,
                                    const std::vector<PatternPlacement>& placements, ObjectId pattern) {
    // Instance 0 is the source's own result. Every other instance applies the
    // source's operation to the body so far; the first failure fails the
    // whole pattern, which then keeps no body (no partial patterns).
    geometry::Body body = sourceBody;
    for (const PatternPlacement& placement : placements) {
        auto next = apply(body, placement.motion, FaceCopy{pattern, placement.instance});
        if (!next) {
            return makeError(next.error().code, std::format("{}: {}", placement.label, next.error().message));
        }
        body = std::move(*next);
    }
    const auto properties = body.massProperties();
    if (body.isEmpty() || !body.isValid() || !properties || !isFinite(properties->volume) ||
        !(properties->volume > Volume{})) {
        return makeError(ErrorCode::Internal, "the pattern produced no valid solid");
    }
    return body;
}

Result<std::size_t> resolvePatternCount(std::uint32_t literal, const std::optional<ParameterId>& parameter,
                                        const Document& document, std::string_view prefix) {
    if (!parameter) {
        if (literal < 1) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{}the count must be at least 1, got {}", prefix, literal));
        }
        return static_cast<std::size_t>(literal);
    }
    auto value = drivingValue<Quantity<dimensions::dimensionless>>(document, *parameter, "count parameter");
    if (!value) {
        return makeError(value.error().code, std::format("{}{}", prefix, value.error().message));
    }
    const double count = value->si();
    if (!(count >= 1.0) || count != std::floor(count) || count > static_cast<double>(kMaxPatternInstances)) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{}the count must be a whole number from 1 to {}, got {:.6g}", prefix,
                                     kMaxPatternInstances, count));
    }
    return static_cast<std::size_t>(count);
}

Result<void> checkCircularAngle(CircularSpacing spacing, std::optional<std::size_t> count, Angle angle) {
    // Angles this close to a full turn are a full turn (the tolerance of
    // edge and face matching, which could not tell the instances apart).
    constexpr double kFullTurn = 2.0 * std::numbers::pi;
    constexpr double kAngularTolerance = 1e-9;
    const auto invalid = [](const std::string& message) { return makeError(ErrorCode::InvalidArgument, message); };
    const std::string degrees = toString(angle, units::deg);
    switch (spacing) {
    case CircularSpacing::FullCircle:
        return {};
    case CircularSpacing::IncludedAngle:
        if (!isFinite(angle) || !(angle > Angle{})) {
            return invalid(std::format("the included angle must be positive and finite, got {}", degrees));
        }
        if (angle.si() >= kFullTurn - kAngularTolerance) {
            return invalid(std::format("the included angle must be less than 360 deg, got {}: the last instance "
                                       "would land on the source (use a full circle)",
                                       degrees));
        }
        return {};
    case CircularSpacing::AngleStep:
        if (!isFinite(angle) || !(angle > Angle{})) {
            return invalid(std::format("the angle step must be positive and finite, got {}", degrees));
        }
        if (angle.si() >= kFullTurn - kAngularTolerance) {
            return invalid(std::format("the angle step must be less than 360 deg, got {}", degrees));
        }
        if (count && *count > 1 &&
            angle.si() * static_cast<double>(*count - 1) >= kFullTurn - kAngularTolerance) {
            // Rounded as in descriptions: 120 deg is 119.99999999999999 once
            // converted to radians and back.
            return invalid(std::format("the instances would go all the way around: {} instances {:.10g} deg apart "
                                       "span {:.10g} deg, which must stay below 360 deg",
                                       *count, angle.in(units::deg),
                                       angle.in(units::deg) * static_cast<double>(*count - 1)));
        }
        return {};
    }
    return {};
}

} // namespace bettercad::features::detail
