#include <bettercad/assembly/Resolution.hpp>

#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Configurations.hpp>
#include <bettercad/assembly/Mate.hpp>
#include <bettercad/assembly/Mates.hpp>
#include <bettercad/assembly/Placement.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/features/Feature.hpp>
#include <bettercad/features/Regenerator.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <string>
#include <utility>

namespace bettercad::assembly {
namespace {

/// How a reference reads in a message: the object, and the document when it
/// is somewhere else.
std::string describe(const ObjectReference& reference) {
    if (isInternal(reference)) {
        return std::format("{}", reference.object);
    }
    return std::format("{} of document {}", reference.object, reference.document->value());
}

} // namespace

Result<const features::SolidFeature*> resolvePart(const Document& document, const ObjectReference& reference,
                                                  const ReferenceResolver* resolver) {
    if (auto valid = validate(reference); !valid) {
        return std::unexpected(valid.error());
    }
    const ResolvedReference found = resolve(reference, document, resolver);
    if (!found.resolved()) {
        return makeError(ErrorCode::NotFound,
                         std::format("a component cannot place {}: {}", describe(reference), toString(found.state)));
    }
    const auto* part = dynamic_cast<const features::SolidFeature*>(found.object);
    if (part == nullptr) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("a component cannot place {} ('{}'): a {} produces no body",
                                     describe(reference), found.object->name(), found.object->typeName()));
    }
    return part;
}

Result<const features::SolidFeature*> resolveComponentPart(const Document& document, ComponentId id,
                                                            const ReferenceResolver* resolver) {
    const Component* component = findComponent(document, id);
    if (component == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("there is no component {}", id));
    }
    return resolvePart(document, component->definition().part, resolver);
}

std::vector<UnresolvedComponent> unresolvedComponents(const Document& document, const ReferenceResolver* resolver) {
    std::vector<UnresolvedComponent> found;
    for (const ComponentId id : components(document)) {
        const Component* component = findComponent(document, id);
        if (component == nullptr) {
            continue;
        }
        const ResolvedReference state = resolve(component->definition().part, document, resolver);
        if (!state.resolved()) {
            found.push_back({.component = id, .state = state.state});
        }
    }
    return found;
}

std::string_view toString(SolveTrigger trigger) noexcept {
    switch (trigger) {
    case SolveTrigger::NotNeeded:
        return "not needed";
    case SolveTrigger::First:
        return "first";
    case SolveTrigger::ObjectChanged:
        return "object changed";
    case SolveTrigger::ComponentsInForceChanged:
        return "components in force changed";
    case SolveTrigger::MatesInForceChanged:
        return "mates in force changed";
    case SolveTrigger::ConfigurationChanged:
        return "configuration changed";
    case SolveTrigger::PlacementChanged:
        return "placement changed";
    case SolveTrigger::Broken:
        return "broken";
    }
    return "unknown";
}

namespace {

/// What the last solve consumed. The trigger compares this against what the
/// solve would consume now, so a re-solve happens exactly when one of its own
/// inputs moved (ADR-008) -- rather than when some revision it is downstream
/// of happened to change.
struct SolveInputs {
    bool solved = false;
    std::optional<ConfigurationId> configuration{};
    std::vector<ComponentId> components{};
    std::vector<MateId> mates{};
    std::map<ComponentId, RigidTransform3D> placements{};

    friend bool operator==(const SolveInputs&, const SolveInputs&) = default;
};

/// The inputs as they stand now.
///
/// The placements are the RESOLVED ones, and that is deliberate: a
/// configuration overriding a free parameter changes no object's revision --
/// the base value is untouched and only the value in force differs -- so a
/// revision-based trigger would miss it entirely. placementOf() reads the
/// value in force, so comparing placements catches it.
[[nodiscard]] SolveInputs currentInputs(const Document& document) {
    SolveInputs inputs;
    inputs.solved = true;
    inputs.configuration = document.activeConfiguration();
    inputs.components = activeComponents(document);
    inputs.mates = activeMates(document);
    for (const ComponentId id : inputs.components) {
        if (auto placement = placementOf(document, id)) {
            inputs.placements.emplace(id, *placement);
        }
    }
    return inputs;
}

/// Whether @p id was rebuilt, failed or blocked in this pass.
[[nodiscard]] bool touched(const features::RegenerationReport& report, ObjectId id) {
    const auto in = [&id](const std::vector<ObjectId>& list) {
        return std::ranges::find(list, id) != list.end();
    };
    return in(report.regenerated) || in(report.failed) || in(report.blocked);
}

/// Whether @p id failed or was blocked in this pass.
[[nodiscard]] bool isBroken(const features::RegenerationReport& report, ObjectId id) {
    const auto in = [&id](const std::vector<ObjectId>& list) {
        return std::ranges::find(list, id) != list.end();
    };
    return in(report.failed) || in(report.blocked);
}

/// Why the assembly must re-solve, or NotNeeded.
[[nodiscard]] SolveTrigger triggerFor(const SolveInputs& before, const SolveInputs& now,
                                      const features::RegenerationReport& report) {
    if (!before.solved) {
        return SolveTrigger::First;
    }
    if (before.configuration != now.configuration) {
        return SolveTrigger::ConfigurationChanged;
    }
    if (before.components != now.components) {
        return SolveTrigger::ComponentsInForceChanged;
    }
    if (before.mates != now.mates) {
        return SolveTrigger::MatesInForceChanged;
    }
    for (const ComponentId id : now.components) {
        if (touched(report, ObjectId{id})) {
            return SolveTrigger::ObjectChanged;
        }
    }
    for (const MateId id : now.mates) {
        if (touched(report, ObjectId{id})) {
            return SolveTrigger::ObjectChanged;
        }
    }
    if (before.placements != now.placements) {
        return SolveTrigger::PlacementChanged;
    }
    return SolveTrigger::NotNeeded;
}

} // namespace

void registerHandlers(features::Regenerator& regenerator, const ReferenceResolver* resolver,
                      AssemblyRegeneration* report) {
    regenerator.registerHandler(
        std::string{Component::kTypeName},
        [resolver](Document& document, ObjectId object,
                   const features::Regenerator&) -> Result<std::optional<geometry::Body>> {
            const Component* component = findComponent(document, ComponentId::fromValue(object.value()));
            if (component == nullptr) {
                return makeError(ErrorCode::Internal, std::format("{} is not a component", object));
            }
            // The whole point of the handler: resolving is what turns an
            // unresolvable part into a reported failure instead of silence.
            auto part = resolvePart(document, component->definition().part, resolver);
            if (!part) {
                return std::unexpected(part.error());
            }
            // A component owns no geometry. The part's body is built by the
            // part's own features, exactly once, however many components
            // place it.
            return std::nullopt;
        });

    // Mates had no handler before P13-REGEN-001, which made an unresolvable
    // target silent during regeneration: a face is named through its
    // feature's roles, so the graph reports nothing missing when the role
    // stops existing, and a handler-less mate regenerated as if all were
    // well. With this, a mate whose target does not resolve FAILS.
    regenerator.registerHandler(
        std::string{Mate::kTypeName},
        [](Document& document, ObjectId object,
           const features::Regenerator& current) -> Result<std::optional<geometry::Body>> {
            const MateId id = MateId::fromValue(object.value());
            const Mate* mate = findMate(document, id);
            if (mate == nullptr) {
                return makeError(ErrorCode::Internal, std::format("{} is not a mate", object));
            }
            if (!isMateActive(document, id)) {
                // Not in this build. A suppressed mate, or one on a suppressed
                // component, has nothing to resolve and nothing to say about
                // whether the model is broken (P13-CONF-001).
                return std::nullopt;
            }
            const features::BodyLookup bodies = [&current](ObjectId part) { return current.body(part); };
            const MateDefinition& d = mate->definition();
            for (const std::optional<MateTarget>& target : {d.a, d.b, d.a2, d.b2}) {
                if (target) {
                    if (auto geometry = resolveMateTarget(document, *target, bodies); !geometry) {
                        return std::unexpected(geometry.error());
                    }
                }
            }
            // A mate owns no geometry either. What it contributes is
            // equations, and those are the final pass's business.
            return std::nullopt;
        });

    // The assembly solve: a document-level result, run after the objects
    // because it spans all of them and needs the bodies a face target
    // resolves against (ADR-008).
    regenerator.registerFinalPass(
        "assembly.solve",
        [report, last = SolveInputs{}, cached = std::map<ComponentId, RigidTransform3D>{}](
            Document& document, const features::Regenerator& current,
            features::RegenerationReport& pass) mutable
        -> Result<std::map<ComponentId, RigidTransform3D>> {
            const auto note = [&report](SolveTrigger trigger, std::optional<SolveStatus> status,
                                        std::size_t dof, std::size_t count) {
                if (report != nullptr) {
                    *report = AssemblyRegeneration{
                        .trigger = trigger, .status = status, .degreesOfFreedom = dof, .transforms = count};
                }
            };

            const SolveInputs now = currentInputs(document);

            // Anything in force that is broken means the assembly is not
            // solvable as described. Publish nothing -- not a partial set and
            // not the previous one, because a transform that is one edit out
            // of date still renders, which makes it worse than absent.
            const auto anyBroken = [&pass](const auto& ids) {
                return std::ranges::any_of(ids, [&pass](auto id) { return isBroken(pass, ObjectId{id}); });
            };
            if (anyBroken(now.components) || anyBroken(now.mates)) {
                last = {};
                cached.clear();
                note(SolveTrigger::Broken, std::nullopt, 0, 0);
                return cached;
            }

            const SolveTrigger trigger = triggerFor(last, now, pass);
            if (trigger == SolveTrigger::NotNeeded) {
                // Nothing the solve reads moved, so what it produced last time
                // still stands.
                note(trigger, report == nullptr ? std::nullopt : report->status,
                     report == nullptr ? 0 : report->degreesOfFreedom, cached.size());
                return cached;
            }

            const features::BodyLookup bodies = [&current](ObjectId part) { return current.body(part); };
            auto result = solve(document, {}, bodies);
            if (!result) {
                last = {};
                cached.clear();
                note(SolveTrigger::Broken, std::nullopt, 0, 0);
                return std::unexpected(result.error());
            }
            last = now;
            cached = result->transforms;
            note(trigger, result->status, result->degreesOfFreedom, cached.size());
            return cached;
        });
}

// --- Mate targets (P13-STREF-001) --------------------------------------------------------------

std::string_view toString(MateTargetSide side) noexcept {
    switch (side) {
    case MateTargetSide::A:
        return "a";
    case MateTargetSide::B:
        return "b";
    case MateTargetSide::RollA:
        return "a2";
    case MateTargetSide::RollB:
        return "b2";
    }
    return "unknown";
}

Result<MateTargetGeometry> resolveMateTarget(const Document& document, const MateTarget& target,
                                             const features::BodyLookup& bodies) {
    if (findComponent(document, target.component) == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("{} is not a component of this document", target.component));
    }
    MateTargetGeometry geometry;
    if (target.kind == MateTargetKind::Axis) {
        auto axis = features::resolveAxis(document, *target.axis, bodies);
        if (!axis) {
            return std::unexpected(axis.error());
        }
        geometry.planar = false;
        geometry.origin = axis->origin;
        geometry.direction = axis->direction;
        return geometry;
    }
    // A face is a plane reference naming that face of its feature, which is
    // how P12-STREF-001 already resolves one -- by the role the feature gives
    // it, never by a position in a topology array.
    PlaneReference reference;
    if (target.kind == MateTargetKind::Face) {
        reference.object = target.face->feature;
        reference.face = target.face->face;
    } else {
        reference = *target.plane;
    }
    auto plane = features::resolvePlane(document, reference, bodies);
    if (!plane) {
        return std::unexpected(plane.error());
    }
    geometry.planar = true;
    geometry.origin = plane->origin();
    geometry.direction = plane->normal();
    return geometry;
}

std::vector<UnresolvedMateTarget> unresolvedMateTargets(const Document& document,
                                                        const features::BodyLookup& bodies) {
    std::vector<UnresolvedMateTarget> found;
    for (const MateId id : mates(document)) {
        const Mate* mate = findMate(document, id);
        if (mate == nullptr || !isMateActive(document, id)) {
            // Not in this build, so it says nothing about whether the model
            // is broken. Inactive is not unresolved (P13-CONF-001).
            continue;
        }
        const MateDefinition& d = mate->definition();
        const std::array<std::pair<MateTargetSide, const std::optional<MateTarget>*>, 4> sides{{
            {MateTargetSide::A, &d.a},
            {MateTargetSide::B, &d.b},
            {MateTargetSide::RollA, &d.a2},
            {MateTargetSide::RollB, &d.b2},
        }};
        for (const auto& [side, target] : sides) {
            if (!*target) {
                continue;
            }
            if (auto geometry = resolveMateTarget(document, **target, bodies); !geometry) {
                found.push_back({.mate = id, .side = side, .reason = geometry.error().code});
            }
        }
    }
    return found;
}

} // namespace bettercad::assembly
