#pragma once

#include <bettercad/core/Export.hpp>
#include <bettercad/core/Id.hpp>

#include <cstddef>
#include <map>
#include <set>
#include <vector>

namespace bettercad {

class Document;

/// Directed graph over document items. An edge dependency -> dependent means
/// that the dependent's result is computed from the dependency, e.g.
/// width parameter -> Sketch001 -> Extrude001.
class BETTERCAD_CORE_EXPORT DependencyGraph {
public:
    void addNode(ObjectId node);
    /// Records that @p dependent depends on @p dependency (adds missing nodes).
    void addDependency(ObjectId dependent, ObjectId dependency);

    [[nodiscard]] bool contains(ObjectId node) const noexcept { return dependencies_.contains(node); }
    [[nodiscard]] std::size_t nodeCount() const noexcept { return dependencies_.size(); }
    /// All nodes, ascending.
    [[nodiscard]] std::vector<ObjectId> nodes() const;
    /// Direct upstream nodes.
    [[nodiscard]] const std::set<ObjectId>& dependenciesOf(ObjectId node) const;
    /// Direct downstream nodes.
    [[nodiscard]] const std::set<ObjectId>& dependentsOf(ObjectId node) const;

    /// @p nodes and everything downstream of them: what becomes dirty when
    /// @p nodes change.
    [[nodiscard]] std::set<ObjectId> downstreamOf(const std::set<ObjectId>& nodes) const;

    struct Ordering {
        /// Every node not in or behind a cycle, dependencies before
        /// dependents; ties are broken by ascending ID, so the order is
        /// deterministic.
        std::vector<ObjectId> order;
        /// Groups of nodes that depend on each other (each sorted).
        std::vector<std::vector<ObjectId>> cycles;
        /// Nodes downstream of a cycle that are not part of one.
        std::set<ObjectId> blocked;
    };
    [[nodiscard]] Ordering topologicalOrder() const;

    /// Strongly connected components that contain a cycle (including
    /// self-dependencies), each sorted, ordered by their smallest ID.
    [[nodiscard]] std::vector<std::vector<ObjectId>> cycles() const;

private:
    std::map<ObjectId, std::set<ObjectId>> dependencies_;
    std::map<ObjectId, std::set<ObjectId>> dependents_;
};

struct MissingReference {
    ObjectId dependent{};
    ObjectId missing{};

    friend bool operator==(const MissingReference&, const MissingReference&) = default;
};

/// Dependency graph of a document: every parameter and object is a node and
/// edges come from DocumentObject::dependencies(). References to items that
/// do not exist are not edges; they are listed in `missing`.
struct DocumentGraph {
    DependencyGraph graph;
    std::vector<MissingReference> missing;
};

[[nodiscard]] BETTERCAD_CORE_EXPORT DocumentGraph buildDependencyGraph(const Document& document);

} // namespace bettercad
