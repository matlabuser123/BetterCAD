#include "TestObjects.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/core/document/Document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <set>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;

namespace {

ObjectId n(std::uint64_t value) {
    return ObjectId::fromValue(value);
}

std::vector<ObjectId> ids(std::initializer_list<std::uint64_t> values) {
    std::vector<ObjectId> result;
    for (const auto v : values) {
        result.push_back(n(v));
    }
    return result;
}

// An object that declares arbitrary dependencies.
class Dependent final : public DocumentObject {
public:
    Dependent(std::string name, std::vector<ObjectId> deps) : DocumentObject(std::move(name)), deps_(std::move(deps)) {}
    [[nodiscard]] std::string_view typeName() const noexcept override { return "test_dependent"; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override { return std::make_unique<Dependent>(*this); }
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override {
        return deps_ == static_cast<const Dependent&>(other).deps_;
    }
    [[nodiscard]] std::vector<ObjectId> dependencies() const override { return deps_; }

private:
    std::vector<ObjectId> deps_;
};

} // namespace

TEST_CASE("Topological order puts dependencies first and breaks ties by ID", "[dependencies]") {
    DependencyGraph graph;
    graph.addDependency(n(3), n(1));
    graph.addDependency(n(2), n(1));
    graph.addDependency(n(4), n(2));
    graph.addDependency(n(4), n(3));
    graph.addNode(n(5));
    graph.addDependency(n(10), n(20)); // a dependency may have a larger ID

    const auto ordering = graph.topologicalOrder();
    CHECK(ordering.order == ids({1, 2, 3, 4, 5, 20, 10}));
    CHECK(ordering.cycles.empty());
    CHECK(ordering.blocked.empty());
    CHECK(graph.dependenciesOf(n(4)) == std::set<ObjectId>{n(2), n(3)});
    CHECK(graph.dependentsOf(n(1)) == std::set<ObjectId>{n(2), n(3)});
    CHECK(graph.dependenciesOf(n(99)).empty());
}

TEST_CASE("Dirty propagation reaches exactly the downstream nodes", "[dependencies]") {
    DependencyGraph graph;
    graph.addDependency(n(3), n(1));
    graph.addDependency(n(2), n(1));
    graph.addDependency(n(4), n(2));
    graph.addNode(n(5));

    CHECK(graph.downstreamOf({n(2)}) == std::set<ObjectId>{n(2), n(4)});
    CHECK(graph.downstreamOf({n(1)}) == std::set<ObjectId>{n(1), n(2), n(3), n(4)});
    CHECK(graph.downstreamOf({n(5)}) == std::set<ObjectId>{n(5)});
    CHECK(graph.downstreamOf({n(99)}).empty());
    CHECK(graph.downstreamOf({}).empty());
}

TEST_CASE("Cycles are detected and block what depends on them", "[dependencies]") {
    DependencyGraph graph;
    graph.addDependency(n(2), n(1));
    graph.addDependency(n(3), n(2));
    graph.addDependency(n(1), n(3)); // 1 -> 2 -> 3 -> 1
    graph.addDependency(n(4), n(3)); // downstream of the cycle
    graph.addNode(n(5));
    graph.addDependency(n(7), n(7)); // self-dependency

    const auto ordering = graph.topologicalOrder();
    REQUIRE(ordering.cycles.size() == 2);
    CHECK(ordering.cycles[0] == ids({1, 2, 3}));
    CHECK(ordering.cycles[1] == ids({7}));
    CHECK(ordering.blocked == std::set<ObjectId>{n(4)});
    CHECK(ordering.order == ids({5}));
}

TEST_CASE("Long dependency chains are handled without recursion", "[dependencies]") {
    DependencyGraph graph;
    constexpr std::uint64_t count = 10000;
    for (std::uint64_t i = 2; i <= count; ++i) {
        graph.addDependency(n(i), n(i - 1));
    }
    const auto ordering = graph.topologicalOrder();
    REQUIRE(ordering.order.size() == count);
    CHECK(ordering.order.front() == n(1));
    CHECK(ordering.order.back() == n(count));
    CHECK(ordering.cycles.empty());

    graph.addDependency(n(1), n(count)); // close the chain into one big cycle
    const auto cyclic = graph.topologicalOrder();
    REQUIRE(cyclic.cycles.size() == 1);
    CHECK(cyclic.cycles.front().size() == count);
}

TEST_CASE("A document's graph comes from the objects' declared dependencies", "[dependencies]") {
    Document doc("Part");
    const auto width = doc.createParameter("width", 100_mm, units::mm);
    REQUIRE(width.has_value());
    const auto first = doc.addObject(std::make_unique<Dependent>("First", std::vector<ObjectId>{*width}));
    REQUIRE(first.has_value());
    const auto second =
        doc.addObject(std::make_unique<Dependent>("Second", std::vector<ObjectId>{*first, n(404)}));
    REQUIRE(second.has_value());

    const DocumentGraph built = buildDependencyGraph(doc);
    CHECK(built.graph.nodeCount() == 3);
    CHECK(built.graph.dependenciesOf(*first) == std::set<ObjectId>{*width});
    CHECK(built.graph.dependenciesOf(*second) == std::set<ObjectId>{*first});
    CHECK(built.missing == std::vector<MissingReference>{{*second, n(404)}});
    CHECK(built.graph.downstreamOf({*width}) == std::set<ObjectId>{*width, *first, *second});
}
