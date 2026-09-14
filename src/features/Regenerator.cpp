#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Regeneration.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/sketch/SketchRegeneration.hpp>

#include <algorithm>
#include <format>
#include <set>
#include <string>

namespace bettercad::features {

namespace {

Result<std::optional<geometry::Body>> regenerateSketchObject(Document& document, ObjectId id,
                                                             const Regenerator& /*regenerator*/) {
    auto changed = document.modifyObject<sketch::Sketch>(id, [&](sketch::Sketch& sketch) -> Result<bool> {
        auto outcome = sketch::regenerateSketch(sketch, document.parameters());
        if (!outcome) {
            return std::unexpected(outcome.error());
        }
        if (!outcome->solved()) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("sketch '{}' is {}: {}", sketch.name(),
                                         sketch::toString(outcome->status), outcome->message));
        }
        return outcome->geometryChanged;
    });
    if (!changed) {
        return std::unexpected(changed.error());
    }
    return std::optional<geometry::Body>{};
}

/// Handler for a solid feature kind F with its evaluation function.
template <typename F, Result<geometry::Body> (*Evaluate)(const F&, const Document&, const geometry::Body*)>
Result<std::optional<geometry::Body>> regenerateSolidFeature(Document& document, ObjectId id,
                                                             const Regenerator& regenerator) {
    const auto* feature = document.findObjectAs<F>(id);
    if (feature == nullptr) {
        return makeError(ErrorCode::Internal, std::format("{} is not a {} feature", id, F::kTypeName));
    }
    const geometry::Body* target = nullptr;
    if (const auto targetId = feature->target()) {
        target = regenerator.body(ObjectId{*targetId});
    }
    auto body = Evaluate(*feature, document, target);
    if (!body) {
        return std::unexpected(body.error());
    }
    return std::optional<geometry::Body>{std::move(*body)};
}

std::string describeCycle(const Document& document, const std::vector<ObjectId>& cycle) {
    std::string text;
    for (const ObjectId id : cycle) {
        if (!text.empty()) {
            text += ", ";
        }
        const auto name = document.nameOf(id);
        text += name ? std::string{*name} : std::format("{}", id);
    }
    return text;
}

} // namespace

std::string_view toString(NodeState state) noexcept {
    switch (state) {
    case NodeState::UpToDate:
        return "up to date";
    case NodeState::Regenerated:
        return "regenerated";
    case NodeState::Failed:
        return "failed";
    case NodeState::Blocked:
        return "blocked";
    }
    return "unknown";
}

Regenerator::Regenerator() {
    registerHandler("sketch", regenerateSketchObject);
    registerHandler(std::string{ExtrudeFeature::kTypeName},
                    regenerateSolidFeature<ExtrudeFeature, &regenerateExtrude>);
    registerHandler(std::string{RevolveFeature::kTypeName},
                    regenerateSolidFeature<RevolveFeature, &regenerateRevolve>);
    registerHandler(std::string{ChamferFeature::kTypeName},
                    regenerateSolidFeature<ChamferFeature, &regenerateChamfer>);
}

void Regenerator::registerHandler(std::string typeName, RegenerationHandler handler) {
    handlers_.insert_or_assign(std::move(typeName), std::move(handler));
}

const geometry::Body* Regenerator::body(ObjectId object) const noexcept {
    const auto it = bodies_.find(object);
    return it == bodies_.end() ? nullptr : &it->second;
}

std::optional<NodeState> Regenerator::state(ObjectId item) const noexcept {
    const auto it = states_.find(item);
    return it == states_.end() ? std::nullopt : std::optional<NodeState>{it->second};
}

const Error* Regenerator::error(ObjectId item) const noexcept {
    const auto it = errors_.find(item);
    return it == errors_.end() ? nullptr : &it->second;
}

Result<RegenerationReport> Regenerator::regenerateAll(Document& document) {
    builtRevisions_.clear();
    states_.clear();
    errors_.clear();
    bodies_.clear();
    return regenerate(document);
}

Result<RegenerationReport> Regenerator::regenerate(Document& document) {
    if (documentId_ && *documentId_ != document.id()) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("this regenerator belongs to {}, not {}", *documentId_, document.id()));
    }
    documentId_ = document.id();

    const DocumentGraph documentGraph = buildDependencyGraph(document);
    const DependencyGraph& graph = documentGraph.graph;
    RegenerationReport report;

    // Forget results of items that no longer exist.
    const auto forgetMissing = [&](auto& map) {
        std::erase_if(map, [&](const auto& entry) { return !graph.contains(entry.first); });
    };
    forgetMissing(builtRevisions_);
    forgetMissing(states_);
    forgetMissing(errors_);
    forgetMissing(bodies_);

    // Sources of dirtiness: new or edited items, items that failed before and
    // items with missing references.
    std::set<ObjectId> changed;
    for (const ObjectId id : graph.nodes()) {
        const auto built = builtRevisions_.find(id);
        const auto previous = states_.find(id);
        const bool unhealthy = previous != states_.end() &&
                               (previous->second == NodeState::Failed || previous->second == NodeState::Blocked);
        if (built == builtRevisions_.end() || built->second != document.revisionOf(id) || unhealthy) {
            changed.insert(id);
        }
    }
    std::map<ObjectId, ObjectId> missing;
    for (const MissingReference& reference : documentGraph.missing) {
        missing.try_emplace(reference.dependent, reference.missing);
        changed.insert(reference.dependent);
    }
    report.changed.assign(changed.begin(), changed.end());
    const std::set<ObjectId> dirty = graph.downstreamOf(changed);

    std::set<ObjectId> broken; // failed or blocked in this pass
    const auto fail = [&](ObjectId id, Error error) {
        states_[id] = NodeState::Failed;
        errors_.insert_or_assign(id, error);
        report.errors.insert_or_assign(id, std::move(error));
        report.failed.push_back(id);
        bodies_.erase(id);
        broken.insert(id);
    };
    const auto block = [&](ObjectId id) {
        states_[id] = NodeState::Blocked;
        errors_.erase(id);
        report.blocked.push_back(id);
        bodies_.erase(id);
        broken.insert(id);
    };

    const DependencyGraph::Ordering ordering = graph.topologicalOrder();
    report.cycles = ordering.cycles;
    for (const auto& cycle : ordering.cycles) {
        for (const ObjectId id : cycle) {
            fail(id, Error{ErrorCode::FailedPrecondition,
                           std::format("dependency cycle: {}", describeCycle(document, cycle))});
        }
    }

    for (const ObjectId id : ordering.order) {
        if (!dirty.contains(id)) {
            states_[id] = NodeState::UpToDate;
            continue;
        }
        const bool upstreamBroken = std::ranges::any_of(
            graph.dependenciesOf(id), [&](ObjectId dependency) { return broken.contains(dependency); });
        if (upstreamBroken) {
            block(id);
            continue;
        }
        if (const auto reference = missing.find(id); reference != missing.end()) {
            fail(id, Error{ErrorCode::NotFound,
                           std::format("{} references {}, which does not exist", id, reference->second)});
            continue;
        }

        const DocumentObject* object = document.findObject(id);
        const auto handler = object == nullptr ? handlers_.end() : handlers_.find(object->typeName());
        if (handler != handlers_.end()) {
            auto result = handler->second(document, id, *this);
            if (!result) {
                fail(id, result.error());
                continue;
            }
            if (*result) {
                bodies_.insert_or_assign(id, std::move(**result));
            } else {
                bodies_.erase(id);
            }
            report.regenerated.push_back(id);
            states_[id] = NodeState::Regenerated;
        } else {
            // Parameters and plain data objects are inputs: nothing to build.
            states_[id] = NodeState::UpToDate;
        }
        errors_.erase(id);
        // The revision after building: a sketch handler bumps it when it solves.
        builtRevisions_.insert_or_assign(id, *document.revisionOf(id));
    }
    for (const ObjectId id : ordering.blocked) {
        block(id);
    }
    return report;
}

} // namespace bettercad::features
