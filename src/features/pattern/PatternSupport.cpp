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
#include <cstdint>
#include <format>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

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
                                                 const std::vector<FaceCopy>& copies) -> Result<geometry::Body> {
        auto moved = geometry::transformed(body, motion);
        if (!moved) {
            return std::unexpected(moved.error());
        }
        const geometry::Body copied = geometry::renameFaces(*moved, appendCopies(copies));
        return cut ? geometry::booleanDifference(target, copied) : geometry::booleanUnion(target, copied);
    }};
}

/// A through-all cut, its tool rebuilt at each instance: the tool must reach
/// through the body as it lies along the moved sketch normal
/// (P12-FEAT-001). The operation is used while @p extrude and @p document
/// are alive (within the pattern's regeneration).
InstanceOperation throughAllOperation(const ExtrudeFeature& extrude, const Document& document) {
    return [&extrude, &document](const geometry::Body& target, const RigidTransform3D& motion,
                                 const std::vector<FaceCopy>& copies) -> Result<geometry::Body> {
        auto tool = extrudeTool(extrude, document, &target, motion);
        if (!tool) {
            return std::unexpected(tool.error());
        }
        auto moved = geometry::transformed(*tool, motion);
        if (!moved) {
            return std::unexpected(moved.error());
        }
        return geometry::booleanDifference(target, geometry::renameFaces(*moved, appendCopies(copies)));
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

/// One instance of a pattern that is itself repeated by another: where it
/// sits relative to its own source, and its own index, which stays its own
/// however the outer pattern repeats it.
struct NestedInstance {
    RigidTransform3D motion{};
    std::uint32_t index = 0;
};

/// The ID of the feature a pattern repeats, and the instances it makes with
/// the suppressed ones left out (they make no geometry wherever the outer
/// pattern puts them). Instance 0 is the source's own geometry, kept first.
struct NestedPattern {
    FeatureId source{};
    std::vector<NestedInstance> instances{};
};

Result<NestedPattern> nestedPattern(const DocumentObject& object, const Document& document) {
    NestedPattern nested;
    if (const auto* linear = dynamic_cast<const LinearPatternFeature*>(&object)) {
        auto instances = resolvePatternInstances(linear->definition(), document);
        if (!instances) {
            return std::unexpected(instances.error());
        }
        nested.source = linear->definition().source;
        for (const PatternInstance& instance : *instances) {
            if (!instance.suppressed) {
                nested.instances.push_back({.motion = RigidTransform3D::translation(instance.offset),
                                            .index = static_cast<std::uint32_t>(instance.index)});
            }
        }
        return nested;
    }
    const auto& circular = dynamic_cast<const CircularPatternFeature&>(object);
    auto instances = resolveCircularPatternInstances(circular.definition(), document);
    if (!instances) {
        return std::unexpected(instances.error());
    }
    nested.source = circular.definition().source;
    for (const CircularPatternInstance& instance : *instances) {
        if (!instance.suppressed) {
            nested.instances.push_back(
                {.motion = instance.motion, .index = static_cast<std::uint32_t>(instance.index)});
        }
    }
    return nested;
}

/// Whether @p object is a pattern, and so repeats instances of its own.
bool isPattern(const DocumentObject& object) {
    return dynamic_cast<const LinearPatternFeature*>(&object) != nullptr ||
           dynamic_cast<const CircularPatternFeature*>(&object) != nullptr;
}

// Patterns nested deeper than this are taken to be a cycle. The regenerator
// reports dependency cycles and does not regenerate their members, so this
// is a second line of defence against unbounded recursion; a chain this
// deep would in any case pass the instance cap only if nearly every pattern
// in it made a single instance.
constexpr std::size_t kMaxPatternNesting = 8;

/// The source a pattern repeats, or nullptr with the reason it cannot be
/// used: missing, or the pattern itself.
Result<const DocumentObject*> patternSource(const DocumentObject& object, const Document& document, FeatureId source) {
    const DocumentObject* found = document.findObject(ObjectId{source});
    if (found == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("the source {} of {} does not exist", ObjectId{source}, object.name()));
    }
    if (found->id() == object.id()) {
        return makeError(ErrorCode::FailedPrecondition, std::format("{} repeats itself", object.name()));
    }
    return found;
}

/// How many instances @p object makes in all: its own, each of which makes
/// every instance of its source again if that source is a pattern too. The
/// suppressed instances at every level are already left out.
Result<std::size_t> effectiveCount(const DocumentObject& object, const Document& document, std::size_t depth) {
    if (!isPattern(object)) {
        return std::size_t{1};
    }
    if (depth >= kMaxPatternNesting) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("{}: patterns nest more than {} deep (a dependency cycle?)", object.name(),
                                     kMaxPatternNesting));
    }
    auto nested = nestedPattern(object, document);
    if (!nested) {
        return std::unexpected(nested.error());
    }
    auto source = patternSource(object, document, nested->source);
    if (!source) {
        return std::unexpected(source.error());
    }
    auto inner = effectiveCount(**source, document, depth + 1);
    if (!inner) {
        return std::unexpected(inner.error());
    }
    return nested->instances.size() * *inner;
}

Result<InstanceOperation> operationAt(const DocumentObject& source, const Document& document,
                                      std::string_view pattern, std::size_t depth);

/// A pattern repeated by another pattern (P12-PATTERN-001): every instance
/// of the inner pattern is made again, moved by the outer instance's motion,
/// which composes with the inner instance's own. The faces each one makes
/// carry the inner pattern's copy step and then the outer's, in the order
/// they were made, so a nested copy names the feature that made the face,
/// the inner pattern and its instance, and the outer pattern and its
/// instance: no instance of either pattern is confused with another.
Result<InstanceOperation> nestedPatternOperation(const DocumentObject& object, const Document& document,
                                                 std::string_view pattern, std::size_t depth) {
    if (depth >= kMaxPatternNesting) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("{}: patterns nest more than {} deep (a dependency cycle?)", object.name(),
                                     kMaxPatternNesting));
    }
    auto nested = nestedPattern(object, document);
    if (!nested) {
        return std::unexpected(nested.error());
    }
    auto source = patternSource(object, document, nested->source);
    if (!source) {
        return std::unexpected(source.error());
    }
    auto inner = operationAt(**source, document, pattern, depth + 1);
    if (!inner) {
        return std::unexpected(inner.error());
    }
    return InstanceOperation{[apply = *inner, instances = std::move(nested->instances), id = object.id(),
                              name = std::string{object.name()}](
                                 const geometry::Body& target, const RigidTransform3D& motion,
                                 const std::vector<FaceCopy>& copies) -> Result<geometry::Body> {
        geometry::Body body = target;
        for (const NestedInstance& instance : instances) {
            // Instance 0 of the inner pattern is its source's own geometry,
            // which the outer pattern copies once: only its outer step names
            // it. Every other instance is a copy of the inner pattern first.
            std::vector<FaceCopy> chain;
            chain.reserve(copies.size() + 1);
            if (instance.index != 0) {
                chain.push_back(FaceCopy{id, instance.index});
            }
            chain.insert(chain.end(), copies.begin(), copies.end());
            auto next = apply(body, motion.after(instance.motion), chain);
            if (!next) {
                return makeError(next.error().code,
                                 std::format("{} instance {}: {}", name, instance.index, next.error().message));
            }
            body = std::move(*next);
        }
        return body;
    }};
}

Result<InstanceOperation> operationAt(const DocumentObject& source, const Document& document,
                                      std::string_view pattern, std::size_t depth) {
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
                                     const std::vector<FaceCopy>& copies) {
            return geometry::cutHole(target, geometry::transformed(request, motion), holeFaceNamer(id, copies));
        }};
    }
    if (const auto* chamfer = dynamic_cast<const ChamferFeature*>(&source)) {
        auto request = resolveChamferRequest(chamfer->definition(), document);
        if (!request) {
            return std::unexpected(request.error());
        }
        return InstanceOperation{[request = *request, id = chamfer->id()](
                                     const geometry::Body& target, const RigidTransform3D& motion,
                                     const std::vector<FaceCopy>& copies) {
            geometry::ChamferRequest moved = request;
            moved.edges = movedEdges(request.edges, motion);
            if (moved.referenceSide) {
                moved.referenceSide = motion.apply(*moved.referenceSide);
            }
            return geometry::chamferEdges(target, moved, chamferFaceNamer(id, copies));
        }};
    }
    if (const auto* fillet = dynamic_cast<const FilletFeature*>(&source)) {
        auto radius = resolveFilletRadius(fillet->definition(), document);
        if (!radius) {
            return std::unexpected(radius.error());
        }
        return InstanceOperation{[edges = fillet->definition().edges, radius = *radius](
                                     const geometry::Body& target, const RigidTransform3D& motion,
                                     const std::vector<FaceCopy>& /*a fillet names no faces*/) {
            return geometry::filletEdges(target, {.edges = movedEdges(edges, motion), .radius = radius});
        }};
    }
    if (dynamic_cast<const VariableFilletFeature*>(&source) != nullptr) {
        // Its stations run along each edge's canonical direction, which a
        // copy's edge may reverse; copies are not supported (P12-FEAT-006).
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("a {} cannot repeat a variable-radius fillet", pattern));
    }
    if (isPattern(source)) {
        return nestedPatternOperation(source, document, pattern, depth);
    }
    return makeError(ErrorCode::FailedPrecondition,
                     std::format("a {} cannot repeat a {}", pattern, source.typeName()));
}

} // namespace

Result<InstanceOperation> instanceOperation(const DocumentObject& source, const Document& document,
                                            std::string_view pattern) {
    return operationAt(source, document, pattern, 0);
}

Result<std::size_t> instanceCountOf(const DocumentObject& source, const Document& document) {
    return effectiveCount(source, document, 0);
}

Result<void> checkNestedCount(const DocumentObject& source, const Document& document, std::size_t instances,
                              std::string_view pattern) {
    auto inner = instanceCountOf(source, document);
    if (!inner) {
        return std::unexpected(inner.error());
    }
    if (*inner <= 1) {
        return {};
    }
    const std::size_t effective = instances * *inner;
    if (effective > kMaxPatternInstances) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("a {} may have at most {} instances, got {} ({} x the {} instances of {})",
                                     pattern, kMaxPatternInstances, effective, instances, *inner, source.name()));
    }
    return {};
}

Result<geometry::Body> buildPattern(const geometry::Body& sourceBody, const InstanceOperation& apply,
                                    const std::vector<PatternPlacement>& placements, ObjectId pattern) {
    // Instance 0 is the source's own result. Every other instance applies the
    // source's operation to the body so far; the first failure fails the
    // whole pattern, which then keeps no body (no partial patterns).
    geometry::Body body = sourceBody;
    for (const PatternPlacement& placement : placements) {
        auto next = apply(body, placement.motion, std::vector<FaceCopy>{FaceCopy{pattern, placement.instance}});
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
