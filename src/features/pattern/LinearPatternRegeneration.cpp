#include "features/SolidSupport.hpp"

#include <bettercad/core/geometry/Booleans.hpp>
#include <bettercad/core/geometry/Chamfer.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/geometry/Fillet.hpp>
#include <bettercad/core/geometry/Hole.hpp>
#include <bettercad/core/geometry/Transform.hpp>
#include <bettercad/core/units/Format.hpp>
#include <bettercad/features/Regeneration.hpp>

#include <cmath>
#include <format>
#include <functional>
#include <string>

namespace bettercad::features {

namespace {

/// Applies the source's operation, moved by an offset, to a body.
using InstanceOperation = std::function<Result<geometry::Body>(const geometry::Body&, const Translation3D&)>;

/// Negative zero as 0, for messages.
double tidy(double value) {
    return value == 0.0 ? 0.0 : value;
}

std::string formatMm(const Translation3D& t) {
    return std::format("({:.6g}, {:.6g}, {:.6g}) mm", tidy(t.x.in(units::mm)), tidy(t.y.in(units::mm)),
                       tidy(t.z.in(units::mm)));
}

Result<PatternStep> resolveStep(const PatternDirection& direction, const Document& document, std::string_view label) {
    const auto invalid = [&](const std::string& message) {
        return makeError(ErrorCode::InvalidArgument, std::format("{}: {}", label, message));
    };
    const auto unit = Direction3D::fromComponents(direction.direction.x, direction.direction.y, direction.direction.z);
    if (!unit) {
        return invalid("the direction must be a finite, non-zero vector");
    }
    PatternStep step{.direction = *unit, .count = direction.count, .spacing = direction.spacing};
    if (direction.countParameter) {
        auto value = detail::drivingValue<Quantity<dimensions::dimensionless>>(document, *direction.countParameter,
                                                                               "count parameter");
        if (!value) {
            return makeError(value.error().code, std::format("{}: {}", label, value.error().message));
        }
        const double count = value->si();
        if (!(count >= 1.0) || count != std::floor(count) || count > static_cast<double>(kMaxPatternInstances)) {
            return invalid(std::format("the count must be a whole number from 1 to {}, got {:.6g}",
                                       kMaxPatternInstances, count));
        }
        step.count = static_cast<std::size_t>(count);
    } else if (direction.count < 1) {
        return invalid(std::format("the count must be at least 1, got {}", direction.count));
    }
    if (direction.spacingParameter) {
        auto value = detail::drivingValue<Length>(document, *direction.spacingParameter, "spacing parameter");
        if (!value) {
            return makeError(value.error().code, std::format("{}: {}", label, value.error().message));
        }
        step.spacing = *value;
    }
    if (!isFinite(step.spacing) || !(step.spacing > Length{})) {
        return invalid(std::format("the spacing must be positive and finite, got {}", toString(step.spacing, units::mm)));
    }
    return step;
}

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
    return InstanceOperation{[body = *tool, cut](const geometry::Body& target,
                                                 const Translation3D& offset) -> Result<geometry::Body> {
        auto moved = geometry::translated(body, offset);
        if (!moved) {
            return std::unexpected(moved.error());
        }
        return cut ? geometry::booleanDifference(target, *moved) : geometry::booleanUnion(target, *moved);
    }};
}

std::vector<geometry::EdgeSignature> translatedEdges(const std::vector<geometry::EdgeSignature>& edges,
                                                     const Translation3D& offset) {
    std::vector<geometry::EdgeSignature> moved;
    moved.reserve(edges.size());
    for (const geometry::EdgeSignature& edge : edges) {
        moved.push_back(geometry::translated(edge, offset));
    }
    return moved;
}

/// The operation of @p source, resolved once and repeated at each instance.
/// Holes, chamfers and fillets move their references exactly by the offset;
/// each moved reference must resolve on the body as if the feature had been
/// placed there by hand, with the same checks.
Result<InstanceOperation> instanceOperation(const DocumentObject& source, const Document& document) {
    if (const auto* extrude = dynamic_cast<const ExtrudeFeature*>(&source)) {
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
        return InstanceOperation{[request = *request](const geometry::Body& target, const Translation3D& offset) {
            return geometry::cutHole(target, geometry::translated(request, offset));
        }};
    }
    if (const auto* chamfer = dynamic_cast<const ChamferFeature*>(&source)) {
        auto request = resolveChamferRequest(chamfer->definition(), document);
        if (!request) {
            return std::unexpected(request.error());
        }
        return InstanceOperation{[request = *request](const geometry::Body& target, const Translation3D& offset) {
            geometry::ChamferRequest moved = request;
            moved.edges = translatedEdges(request.edges, offset);
            return geometry::chamferEdges(target, moved);
        }};
    }
    if (const auto* fillet = dynamic_cast<const FilletFeature*>(&source)) {
        auto radius = resolveFilletRadius(fillet->definition(), document);
        if (!radius) {
            return std::unexpected(radius.error());
        }
        return InstanceOperation{[edges = fillet->definition().edges, radius = *radius](
                                     const geometry::Body& target, const Translation3D& offset) {
            return geometry::filletEdges(target, {.edges = translatedEdges(edges, offset), .radius = radius});
        }};
    }
    if (dynamic_cast<const LinearPatternFeature*>(&source) != nullptr) {
        return makeError(ErrorCode::FailedPrecondition,
                         "a linear pattern cannot repeat another pattern; use a second direction for a grid");
    }
    return makeError(ErrorCode::FailedPrecondition,
                     std::format("a linear pattern cannot repeat a {}", source.typeName()));
}

} // namespace

Result<std::vector<PatternInstance>> resolvePatternInstances(const LinearPatternDefinition& definition,
                                                             const Document& document) {
    auto first = resolveStep(definition.first, document, "direction 1");
    if (!first) {
        return std::unexpected(first.error());
    }
    std::optional<PatternStep> second;
    if (definition.second) {
        auto step = resolveStep(*definition.second, document, "direction 2");
        if (!step) {
            return std::unexpected(step.error());
        }
        second = *step;
    }
    const std::size_t total = first->count * (second ? second->count : 1);
    if (total > kMaxPatternInstances) {
        return makeError(ErrorCode::InvalidArgument,
                         second ? std::format("a linear pattern may have at most {} instances, got {} ({} x {})",
                                              kMaxPatternInstances, total, first->count, second->count)
                                : std::format("a linear pattern may have at most {} instances, got {}",
                                              kMaxPatternInstances, total));
    }
    return patternInstances(*first, second);
}

Result<geometry::Body> regenerateLinearPattern(const LinearPatternFeature& feature, const Document& document,
                                               const geometry::Body* target) {
    const LinearPatternDefinition& definition = feature.definition();
    const auto build = [&](const geometry::Body& sourceBody) -> Result<geometry::Body> {
        auto instances = resolvePatternInstances(definition, document);
        if (!instances) {
            return std::unexpected(instances.error());
        }
        const DocumentObject* source = document.findObject(ObjectId{definition.source});
        if (source == nullptr) {
            return makeError(ErrorCode::NotFound,
                             std::format("the source {} does not exist", ObjectId{definition.source}));
        }
        auto apply = instanceOperation(*source, document);
        if (!apply) {
            return std::unexpected(apply.error());
        }
        // Instance 0 is the source's own result. Every other instance applies
        // the source's operation to the body so far; the first failure fails
        // the whole pattern, which then keeps no body (no partial patterns).
        geometry::Body body = sourceBody;
        for (const PatternInstance& instance : *instances) {
            if (instance.index == 0) {
                continue;
            }
            auto next = (*apply)(body, instance.offset);
            if (!next) {
                return makeError(next.error().code, std::format("instance {} at {}: {}", instance.index,
                                                                formatMm(instance.offset), next.error().message));
            }
            body = std::move(*next);
        }
        const auto properties = body.massProperties();
        if (body.isEmpty() || !body.isValid() || !properties || !isFinite(properties->volume) ||
            !(properties->volume > Volume{})) {
            return makeError(ErrorCode::Internal, "the pattern produced no valid solid");
        }
        return body;
    };
    const auto pattern = [&](const geometry::Body& sourceBody) -> Result<geometry::Body> {
        auto body = build(sourceBody);
        if (!body) {
            return makeError(body.error().code, std::format("linear pattern: {}", body.error().message));
        }
        return body;
    };
    return detail::applyToTargetBody(feature.name(), "linear pattern", target, pattern, "source");
}

} // namespace bettercad::features
