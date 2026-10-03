#include <bettercad/meshing/Mesh.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <format>

namespace bettercad::meshing {
namespace {

/// Components of b - a, in SI. The arithmetic below is done on plain SI values
/// and wrapped once at the public boundary, which is what distance(Point3D,
/// Point3D) in core already does: a Length-valued cross product would need an
/// area-valued vector type that nothing else in BetterCAD wants yet.
struct EdgeSi {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

[[nodiscard]] EdgeSi edge(const Point3D& a, const Point3D& b) noexcept {
    return {b.x.si() - a.x.si(), b.y.si() - a.y.si(), b.z.si() - a.z.si()};
}

[[nodiscard]] EdgeSi cross(const EdgeSi& a, const EdgeSi& b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

[[nodiscard]] double dot(const EdgeSi& a, const EdgeSi& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

/// Process-local source of MeshIds. Starts at 1 so that a default MeshStamp,
/// which holds MeshId{}, is never a mesh that exists.
[[nodiscard]] MeshId nextMeshId() noexcept {
    static std::atomic<MeshId::ValueType> counter{0};
    return MeshId::fromValue(counter.fetch_add(1, std::memory_order_relaxed) + 1);
}

} // namespace

std::string_view toString(ElementType type) noexcept {
    switch (type) {
    case ElementType::Triangle3:
        return "triangle3";
    case ElementType::Tetrahedron4:
        return "tetrahedron4";
    }
    return "unknown";
}

std::size_t nodeCount(ElementType type) noexcept {
    switch (type) {
    case ElementType::Triangle3:
        return 3;
    case ElementType::Tetrahedron4:
        return 4;
    }
    return 0;
}

Volume signedVolume(const Point3D& p1, const Point3D& p2, const Point3D& p3, const Point3D& p4) noexcept {
    const EdgeSi e1 = edge(p1, p2);
    const EdgeSi e2 = edge(p1, p3);
    const EdgeSi e3 = edge(p1, p4);
    // No std::abs anywhere: the sign IS the orientation, and taking its
    // magnitude would make an inverted element look correct.
    return Volume::fromSi(dot(e1, cross(e2, e3)) / 6.0);
}

Area triangleArea(const Point3D& p1, const Point3D& p2, const Point3D& p3) noexcept {
    const EdgeSi n = cross(edge(p1, p2), edge(p1, p3));
    return Area::fromSi(0.5 * std::sqrt(dot(n, n)));
}

const Node* Mesh::findNode(NodeId id) const noexcept {
    if (!id.isValid()) {
        return nullptr;
    }
    // By identity, over ascending storage. Never nodes_[id.value()]: handles may
    // be sparse, and indexing by value would resolve to a different node.
    const auto it = std::ranges::lower_bound(nodes_, id, {}, &Node::id);
    if (it == nodes_.end() || it->id != id) {
        return nullptr;
    }
    return &*it;
}

std::optional<ElementType> Mesh::elementType(ElementId id) const noexcept {
    if (!id.isValid()) {
        return std::nullopt;
    }
    const auto triangle = std::ranges::lower_bound(triangles_, id, {}, &Triangle::id);
    if (triangle != triangles_.end() && triangle->id == id) {
        return ElementType::Triangle3;
    }
    const auto tetrahedron = std::ranges::lower_bound(tetrahedra_, id, {}, &Tetrahedron::id);
    if (tetrahedron != tetrahedra_.end() && tetrahedron->id == id) {
        return ElementType::Tetrahedron4;
    }
    return std::nullopt;
}

const Triangle* Mesh::findTriangle(ElementId id) const noexcept {
    if (!id.isValid()) {
        return nullptr;
    }
    const auto it = std::ranges::lower_bound(triangles_, id, {}, &Triangle::id);
    if (it == triangles_.end() || it->id != id) {
        return nullptr;
    }
    return &*it;
}

const Tetrahedron* Mesh::findTetrahedron(ElementId id) const noexcept {
    if (!id.isValid()) {
        return nullptr;
    }
    const auto it = std::ranges::lower_bound(tetrahedra_, id, {}, &Tetrahedron::id);
    if (it == tetrahedra_.end() || it->id != id) {
        return nullptr;
    }
    return &*it;
}

std::optional<MeshBounds> Mesh::bounds() const noexcept {
    if (nodes_.empty()) {
        return std::nullopt;
    }
    Point3D low = nodes_.front().position;
    Point3D high = low;
    for (const Node& node : nodes_) {
        low.x = std::min(low.x, node.position.x);
        low.y = std::min(low.y, node.position.y);
        low.z = std::min(low.z, node.position.z);
        high.x = std::max(high.x, node.position.x);
        high.y = std::max(high.y, node.position.y);
        high.z = std::max(high.z, node.position.z);
    }
    return MeshBounds{low, high};
}

MeshBuilder::MeshBuilder(std::uint32_t generation) {
    mesh_.stamp_ = MeshStamp{nextMeshId(), generation};
}

Result<NodeId> MeshBuilder::addNode(const Point3D& position) {
    return addNode(NodeId::fromValue(lastNode_ + 1), position);
}

Result<NodeId> MeshBuilder::addNode(NodeId id, const Point3D& position) {
    if (!id.isValid()) {
        return makeError(ErrorCode::InvalidArgument, "mesh node: the invalid handle cannot name a node");
    }
    if (id.value() <= lastNode_) {
        return makeError(ErrorCode::AlreadyExists,
                         std::format("mesh node: {} is not greater than the last node added (node:{}); "
                                     "node handles are strictly increasing, so this is either a repeat "
                                     "or out of order",
                                     id, lastNode_));
    }
    if (!isFinite(position.x) || !isFinite(position.y) || !isFinite(position.z)) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("mesh node {}: coordinate is not finite", id));
    }
    // Atomic: nothing above this line has changed the builder.
    mesh_.nodes_.push_back(Node{id, position});
    lastNode_ = id.value();
    return id;
}

Result<void> MeshBuilder::checkNodes(std::span<const NodeId> nodes) const {
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (!nodes[i].isValid()) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("mesh element: node {} of {} is the invalid handle", i + 1, nodes.size()));
        }
        for (std::size_t j = i + 1; j < nodes.size(); ++j) {
            if (nodes[i] == nodes[j]) {
                return makeError(ErrorCode::InvalidArgument,
                                 std::format("mesh element: {} appears as node {} and node {}; an element's "
                                             "nodes must be distinct",
                                             nodes[i], i + 1, j + 1));
            }
        }
        if (mesh_.findNode(nodes[i]) == nullptr) {
            return makeError(ErrorCode::NotFound,
                             std::format("mesh element: {} names no node of this mesh", nodes[i]));
        }
    }
    return {};
}

void MeshBuilder::noteRegion(RegionId region) {
    const auto it = std::ranges::lower_bound(mesh_.regions_, region);
    if (it == mesh_.regions_.end() || *it != region) {
        mesh_.regions_.insert(it, region);
    }
}

Result<ElementId> MeshBuilder::addTriangle(const std::array<NodeId, 3>& nodes, RegionId region) {
    if (!region.isValid()) {
        return makeError(ErrorCode::InvalidArgument, "mesh triangle: the invalid handle cannot name a region");
    }
    if (auto checked = checkNodes(nodes); !checked) {
        return std::unexpected(checked.error());
    }
    const ElementId id = ElementId::fromValue(lastElement_ + 1);
    mesh_.triangles_.push_back(Triangle{id, nodes, region});
    lastElement_ = id.value();
    noteRegion(region);
    return id;
}

Result<ElementId> MeshBuilder::addTetrahedron(const std::array<NodeId, 4>& nodes, RegionId region) {
    if (!region.isValid()) {
        return makeError(ErrorCode::InvalidArgument, "mesh tetrahedron: the invalid handle cannot name a region");
    }
    if (auto checked = checkNodes(nodes); !checked) {
        return std::unexpected(checked.error());
    }
    const ElementId id = ElementId::fromValue(lastElement_ + 1);
    mesh_.tetrahedra_.push_back(Tetrahedron{id, nodes, region});
    lastElement_ = id.value();
    noteRegion(region);
    return id;
}

Mesh MeshBuilder::build() const {
    return mesh_;
}

} // namespace bettercad::meshing
