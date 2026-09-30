#include <bettercad/meshing/MeshValidation.hpp>

#include <algorithm>
#include <format>
#include <map>
#include <span>
#include <tuple>

namespace bettercad::meshing {
namespace {

/// Where a node's region was first seen, so a conflict names both regions.
/// std::map and not unordered_map: the report's order must be identical in every
/// preset, and an unordered container's iteration is exactly the thing that
/// would make it not be.
using RegionOfNode = std::map<NodeId, RegionId>;

void addIssue(std::vector<MeshIssue>& issues, MeshIssueKind kind, std::optional<NodeId> node,
              std::optional<ElementId> element, std::optional<RegionId> region, std::string message) {
    issues.push_back(MeshIssue{kind, node, element, region, std::move(message)});
}

/// Checks the nodes an element references, for the two defects that are about
/// connectivity rather than geometry. Reported per element so that one bad
/// element does not hide the next.
void checkElementNodes(const Mesh& mesh, ElementId element, std::span<const NodeId> nodes,
                       std::vector<MeshIssue>& missing, std::vector<MeshIssue>& repeated) {
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        for (std::size_t j = i + 1; j < nodes.size(); ++j) {
            if (nodes[i] == nodes[j]) {
                addIssue(repeated, MeshIssueKind::RepeatedNodeReference, nodes[i], element, std::nullopt,
                         std::format("element {}: {} appears as node {} and node {}", element, nodes[i], i + 1,
                                     j + 1));
            }
        }
        if (mesh.findNode(nodes[i]) == nullptr) {
            addIssue(missing, MeshIssueKind::MissingNodeReference, nodes[i], element, std::nullopt,
                     std::format("element {}: {} names no node of this mesh", element, nodes[i]));
        }
    }
}

/// Records which region owns a node, and complains the first time a second
/// region claims it (ADR-032: a mesh shares no node between regions).
void checkRegionSharing(RegionOfNode& owner, ElementId element, std::span<const NodeId> nodes, RegionId region,
                        std::vector<MeshIssue>& issues) {
    for (const NodeId node : nodes) {
        const auto [it, inserted] = owner.try_emplace(node, region);
        if (!inserted && it->second != region) {
            addIssue(issues, MeshIssueKind::NodeSharedBetweenRegions, node, element, region,
                     std::format("element {}: {} is already used by {}, and a mesh shares no node between "
                                 "regions",
                                 element, node, it->second));
        }
    }
}

/// Orders issues within one kind: by node handle, then element handle. Both are
/// total, so the result does not depend on the order the checks ran in.
[[nodiscard]] bool issueLess(const MeshIssue& a, const MeshIssue& b) noexcept {
    const auto key = [](const MeshIssue& issue) {
        return std::tuple{issue.node.value_or(NodeId{}).value(), issue.element.value_or(ElementId{}).value(),
                          issue.region.value_or(RegionId{}).value()};
    };
    return key(a) < key(b);
}

} // namespace

std::string_view toString(MeshIssueKind kind) noexcept {
    switch (kind) {
    case MeshIssueKind::NonFiniteCoordinate:
        return "non_finite_coordinate";
    case MeshIssueKind::MissingNodeReference:
        return "missing_node_reference";
    case MeshIssueKind::RepeatedNodeReference:
        return "repeated_node_reference";
    case MeshIssueKind::DegenerateTriangle:
        return "degenerate_triangle";
    case MeshIssueKind::DegenerateTetrahedron:
        return "degenerate_tetrahedron";
    case MeshIssueKind::InvertedTetrahedron:
        return "inverted_tetrahedron";
    case MeshIssueKind::NodeSharedBetweenRegions:
        return "node_shared_between_regions";
    case MeshIssueKind::MissingRegion:
        return "missing_region";
    case MeshIssueKind::EmptyMesh:
        return "empty_mesh";
    }
    return "unknown";
}

MeshValidationReport validate(const Mesh& mesh) {
    // One bucket per kind, so the report can be assembled in enumeration order
    // without sorting by an enum value and hoping the comparison is stable.
    std::vector<MeshIssue> nonFinite;
    std::vector<MeshIssue> missing;
    std::vector<MeshIssue> repeated;
    std::vector<MeshIssue> degenerateTriangle;
    std::vector<MeshIssue> degenerateTetrahedron;
    std::vector<MeshIssue> inverted;
    std::vector<MeshIssue> shared;
    std::vector<MeshIssue> missingRegion;
    std::vector<MeshIssue> empty;

    for (const Node& node : mesh.nodes()) {
        if (!isFinite(node.position.x) || !isFinite(node.position.y) || !isFinite(node.position.z)) {
            addIssue(nonFinite, MeshIssueKind::NonFiniteCoordinate, node.id, std::nullopt, std::nullopt,
                     std::format("{}: coordinate is not finite", node.id));
        }
    }

    RegionOfNode owner;

    for (const Triangle& triangle : mesh.triangles()) {
        checkElementNodes(mesh, triangle.id, triangle.nodes, missing, repeated);
        if (!triangle.region.isValid()) {
            addIssue(missingRegion, MeshIssueKind::MissingRegion, std::nullopt, triangle.id, std::nullopt,
                     std::format("element {}: no region", triangle.id));
        } else {
            checkRegionSharing(owner, triangle.id, triangle.nodes, triangle.region, shared);
        }

        const Node* a = mesh.findNode(triangle.nodes[0]);
        const Node* b = mesh.findNode(triangle.nodes[1]);
        const Node* c = mesh.findNode(triangle.nodes[2]);
        if (a == nullptr || b == nullptr || c == nullptr) {
            continue; // already reported as a missing reference; geometry is not computable
        }
        const Area area = triangleArea(a->position, b->position, c->position);
        // Exactly zero, or not finite. Not "small": a thin triangle is a quality
        // finding (P16-QUALITY-001), not a data defect, and a tolerance here
        // would quietly become a quality threshold nobody chose.
        if (!isFinite(area) || area.si() == 0.0) {
            addIssue(degenerateTriangle, MeshIssueKind::DegenerateTriangle, std::nullopt, triangle.id,
                     std::nullopt,
                     std::format("element {}: area is {}, so its three nodes are collinear or coincident",
                                 triangle.id, area.si()));
        }
    }

    for (const Tetrahedron& tetrahedron : mesh.tetrahedra()) {
        checkElementNodes(mesh, tetrahedron.id, tetrahedron.nodes, missing, repeated);
        if (!tetrahedron.region.isValid()) {
            addIssue(missingRegion, MeshIssueKind::MissingRegion, std::nullopt, tetrahedron.id, std::nullopt,
                     std::format("element {}: no region", tetrahedron.id));
        } else {
            checkRegionSharing(owner, tetrahedron.id, tetrahedron.nodes, tetrahedron.region, shared);
        }

        const Node* a = mesh.findNode(tetrahedron.nodes[0]);
        const Node* b = mesh.findNode(tetrahedron.nodes[1]);
        const Node* c = mesh.findNode(tetrahedron.nodes[2]);
        const Node* d = mesh.findNode(tetrahedron.nodes[3]);
        if (a == nullptr || b == nullptr || c == nullptr || d == nullptr) {
            continue;
        }
        const Volume volume = signedVolume(a->position, b->position, c->position, d->position);
        if (!isFinite(volume)) {
            // Finite coordinates can still overflow the triple product. A
            // non-finite determinant is never evidence of a valid element, so it
            // is refused rather than compared against zero -- a comparison an
            // infinity would answer "greater".
            addIssue(degenerateTetrahedron, MeshIssueKind::DegenerateTetrahedron, std::nullopt, tetrahedron.id,
                     std::nullopt,
                     std::format("element {}: signed volume is not finite ({}), so its orientation cannot be "
                                 "established",
                                 tetrahedron.id, volume.si()));
        } else if (volume.si() == 0.0) {
            addIssue(degenerateTetrahedron, MeshIssueKind::DegenerateTetrahedron, std::nullopt, tetrahedron.id,
                     std::nullopt,
                     std::format("element {}: signed volume is 0, so its four nodes are coplanar",
                                 tetrahedron.id));
        } else if (volume.si() < 0.0) {
            addIssue(inverted, MeshIssueKind::InvertedTetrahedron, std::nullopt, tetrahedron.id, std::nullopt,
                     std::format("element {}: signed volume is {}, which is negative; a valid tetrahedron's "
                                 "nodes are ordered so that it is positive",
                                 tetrahedron.id, volume.si()));
        }
    }

    if (mesh.elementCount() == 0) {
        addIssue(empty, MeshIssueKind::EmptyMesh, std::nullopt, std::nullopt, std::nullopt,
                 "mesh: no elements");
    }

    MeshValidationReport report;
    for (std::vector<MeshIssue>* bucket : {&nonFinite, &missing, &repeated, &degenerateTriangle,
                                           &degenerateTetrahedron, &inverted, &shared, &missingRegion, &empty}) {
        std::ranges::stable_sort(*bucket, issueLess);
        report.issues.insert(report.issues.end(), bucket->begin(), bucket->end());
    }
    return report;
}

} // namespace bettercad::meshing
