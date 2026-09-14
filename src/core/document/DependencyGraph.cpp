#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/core/document/Document.hpp>

#include <algorithm>
#include <deque>
#include <utility>

namespace bettercad {

namespace {

const std::set<ObjectId>& emptySet() {
    static const std::set<ObjectId> empty;
    return empty;
}

} // namespace

void DependencyGraph::addNode(ObjectId node) {
    dependencies_.try_emplace(node);
    dependents_.try_emplace(node);
}

void DependencyGraph::addDependency(ObjectId dependent, ObjectId dependency) {
    addNode(dependent);
    addNode(dependency);
    dependencies_[dependent].insert(dependency);
    dependents_[dependency].insert(dependent);
}

std::vector<ObjectId> DependencyGraph::nodes() const {
    std::vector<ObjectId> result;
    result.reserve(dependencies_.size());
    for (const auto& [node, upstream] : dependencies_) {
        result.push_back(node);
    }
    return result;
}

const std::set<ObjectId>& DependencyGraph::dependenciesOf(ObjectId node) const {
    const auto it = dependencies_.find(node);
    return it == dependencies_.end() ? emptySet() : it->second;
}

const std::set<ObjectId>& DependencyGraph::dependentsOf(ObjectId node) const {
    const auto it = dependents_.find(node);
    return it == dependents_.end() ? emptySet() : it->second;
}

std::set<ObjectId> DependencyGraph::downstreamOf(const std::set<ObjectId>& nodes) const {
    std::set<ObjectId> reached;
    std::deque<ObjectId> queue;
    for (const ObjectId node : nodes) {
        if (contains(node) && reached.insert(node).second) {
            queue.push_back(node);
        }
    }
    while (!queue.empty()) {
        const ObjectId node = queue.front();
        queue.pop_front();
        for (const ObjectId next : dependentsOf(node)) {
            if (reached.insert(next).second) {
                queue.push_back(next);
            }
        }
    }
    return reached;
}

std::vector<std::vector<ObjectId>> DependencyGraph::cycles() const {
    // Iterative Tarjan: no recursion, so long dependency chains cannot
    // exhaust the stack.
    struct NodeState {
        int index = -1;
        int lowLink = 0;
        bool onStack = false;
    };
    std::map<ObjectId, NodeState> state;
    std::vector<ObjectId> stack;
    std::vector<std::vector<ObjectId>> components;
    int nextIndex = 0;

    for (const auto& [root, unused] : dependencies_) {
        if (state[root].index >= 0) {
            continue;
        }
        // Frames of (node, position in its successor list).
        std::vector<std::pair<ObjectId, std::set<ObjectId>::const_iterator>> frames;
        const auto enter = [&](ObjectId node) {
            NodeState& s = state[node];
            s.index = s.lowLink = nextIndex++;
            s.onStack = true;
            stack.push_back(node);
            frames.emplace_back(node, dependentsOf(node).begin());
        };
        enter(root);
        while (!frames.empty()) {
            auto& [node, it] = frames.back();
            const std::set<ObjectId>& successors = dependentsOf(node);
            if (it != successors.end()) {
                const ObjectId next = *it++;
                if (state[next].index < 0) {
                    enter(next);
                } else if (state[next].onStack) {
                    state[node].lowLink = std::min(state[node].lowLink, state[next].index);
                }
                continue;
            }
            const ObjectId finished = node;
            frames.pop_back();
            if (!frames.empty()) {
                const ObjectId parent = frames.back().first;
                state[parent].lowLink = std::min(state[parent].lowLink, state[finished].lowLink);
            }
            if (state[finished].lowLink == state[finished].index) {
                std::vector<ObjectId> component;
                ObjectId member;
                do {
                    member = stack.back();
                    stack.pop_back();
                    state[member].onStack = false;
                    component.push_back(member);
                } while (member != finished);
                const bool selfLoop = dependentsOf(finished).contains(finished);
                if (component.size() > 1 || selfLoop) {
                    std::ranges::sort(component);
                    components.push_back(std::move(component));
                }
            }
        }
    }
    std::ranges::sort(components, [](const auto& a, const auto& b) { return a.front() < b.front(); });
    return components;
}

DependencyGraph::Ordering DependencyGraph::topologicalOrder() const {
    Ordering ordering;
    // Kahn's algorithm; the ready set is ordered, so ties go to the smallest ID.
    std::map<ObjectId, std::size_t> pending;
    std::set<ObjectId> ready;
    for (const auto& [node, upstream] : dependencies_) {
        pending[node] = upstream.size();
        if (upstream.empty()) {
            ready.insert(node);
        }
    }
    while (!ready.empty()) {
        const ObjectId node = *ready.begin();
        ready.erase(ready.begin());
        ordering.order.push_back(node);
        for (const ObjectId next : dependentsOf(node)) {
            if (--pending[next] == 0) {
                ready.insert(next);
            }
        }
    }
    ordering.cycles = cycles();
    std::set<ObjectId> inCycle;
    for (const auto& cycle : ordering.cycles) {
        inCycle.insert(cycle.begin(), cycle.end());
    }
    for (const auto& [node, count] : pending) {
        if (count > 0 && !inCycle.contains(node)) {
            ordering.blocked.insert(node);
        }
    }
    return ordering;
}

DocumentGraph buildDependencyGraph(const Document& document) {
    DocumentGraph result;
    for (const ObjectId id : document.itemIds()) {
        result.graph.addNode(id);
    }
    for (const DocumentObject& object : document.objects()) {
        for (const ObjectId dependency : object.dependencies()) {
            if (document.contains(dependency)) {
                result.graph.addDependency(object.id(), dependency);
            } else {
                result.missing.push_back({object.id(), dependency});
            }
        }
    }
    return result;
}

} // namespace bettercad
