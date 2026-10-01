#include <bettercad/meshing/SurfaceMesh.hpp>

#include <bettercad/core/geometry/Mesh.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

namespace bettercad::meshing {
namespace {

/// A node position as an exact key.
///
/// NODE UNIFICATION IS BY EXACT COORDINATE EQUALITY, AND THAT IS A TOPOLOGICAL
/// IDENTITY HERE RATHER THAN AN APPROXIMATION. The kernel discretises a shared
/// edge once and both adjacent faces index that same discretisation, so the two
/// faces' nodes on that edge are the same numbers -- not nearly the same. There
/// is no tolerance in this key and therefore none to tune.
///
/// Why not OCCT's PolygonOnTriangulation correspondence, which would be
/// topological by construction: it is the better rule in principle and is
/// recorded as the fallback, but it needs per-face seam handling (a cylinder's
/// closing edge appears twice on one face, with two polygons) and degenerate-edge
/// handling (a sphere's pole), and each of those is a crack-producing bug if it is
/// wrong. The rule chosen here is simple enough to be obviously right, and -- this
/// is what makes it safe rather than hopeful -- its output is PROVEN to close by
/// validateSurface(), which refuses the mesh if it does not. A welding failure
/// cannot be reported as watertight.
///
/// The ordering is lexicographic on the exact values, so it is total and
/// deterministic, and node numbering depends on the SET of coordinates rather
/// than on the order faces were explored in.
struct NodeKey {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    friend bool operator==(const NodeKey&, const NodeKey&) = default;
    friend auto operator<=>(const NodeKey&, const NodeKey&) = default;
};

[[nodiscard]] NodeKey keyOf(const Point3D& p) noexcept {
    return NodeKey{p.x.si(), p.y.si(), p.z.si()};
}

struct EdgeSi {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

[[nodiscard]] EdgeSi cross(const EdgeSi& a, const EdgeSi& b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

[[nodiscard]] double dot(const EdgeSi& a, const EdgeSi& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

/// The undirected edge between two nodes, for incidence counting ONLY.
///
/// Canonical by ascending handle, so the same pair of nodes gives the same key
/// whichever triangle names them first. The stored connectivity is untouched: the
/// oriented winding is what carries the surface's orientation, and this key
/// exists beside it rather than instead of it.
[[nodiscard]] std::pair<NodeId, NodeId> undirected(NodeId a, NodeId b) noexcept {
    return a < b ? std::pair{a, b} : std::pair{b, a};
}

/// How many times a directed edge appears, so that two triangles traversing a
/// shared edge the same way round can be told from two traversing it oppositely.
using DirectedCount = std::map<std::pair<NodeId, NodeId>, std::size_t>;

[[nodiscard]] bool positiveFinite(Length value) noexcept {
    return isFinite(value) && value.si() > 0.0;
}

[[nodiscard]] bool positiveFinite(Angle value) noexcept {
    return isFinite(value) && value.si() > 0.0;
}

[[nodiscard]] std::string describe(SurfaceMeshFailure failure, const SurfaceValidation& validation) {
    switch (failure) {
    case SurfaceMeshFailure::InvalidControls:
        return "the surface meshing controls must be positive and finite";
    case SurfaceMeshFailure::TriangulationFailed:
        return "the kernel could not triangulate the prepared geometry";
    case SurfaceMeshFailure::EmptySurface:
        return "the prepared solid produced no surface triangles, which is never a successful result "
               "for a body that encloses a volume";
    case SurfaceMeshFailure::NotAValidBoundary:
        return std::format("the generated surface is not a valid closed boundary: {} boundary edge(s), "
                           "{} non-manifold edge(s), {} orientation conflict(s), {} degenerate and {} "
                           "duplicate triangle(s), {} unused node(s)",
                           validation.boundaryEdgeCount, validation.nonManifoldEdgeCount,
                           validation.orientationConflictCount, validation.degenerateTriangleCount,
                           validation.duplicateTriangleCount, validation.unusedNodeCount);
    case SurfaceMeshFailure::InwardOrientation:
        return "the generated surface closes but is oriented inward: the volume it encloses came out "
               "negative, so its triangles face into the material";
    }
    return "the surface could not be generated";
}

} // namespace

std::string_view toString(SurfaceMeshFailure failure) noexcept {
    switch (failure) {
    case SurfaceMeshFailure::InvalidControls:
        return "invalid_controls";
    case SurfaceMeshFailure::TriangulationFailed:
        return "triangulation_failed";
    case SurfaceMeshFailure::EmptySurface:
        return "empty_surface";
    case SurfaceMeshFailure::NotAValidBoundary:
        return "not_a_valid_boundary";
    case SurfaceMeshFailure::InwardOrientation:
        return "inward_orientation";
    }
    return "unknown";
}

Area surfaceArea(const Mesh& mesh) {
    double total = 0.0;
    for (const Triangle& triangle : mesh.triangles()) {
        const Node* a = mesh.findNode(triangle.nodes[0]);
        const Node* b = mesh.findNode(triangle.nodes[1]);
        const Node* c = mesh.findNode(triangle.nodes[2]);
        if (a == nullptr || b == nullptr || c == nullptr) {
            continue;
        }
        total += triangleArea(a->position, b->position, c->position).si();
    }
    return Area::fromSi(total);
}

Volume enclosedVolume(const Mesh& mesh) {
    // 1/6 sum of dot(a, cross(b, c)), with every vertex taken from the same
    // origin. Positive for an outward-oriented closed surface. No absolute value:
    // the sign is the whole point, because it is what distinguishes a boundary
    // that faces out from one that faces in.
    double total = 0.0;
    for (const Triangle& triangle : mesh.triangles()) {
        const Node* a = mesh.findNode(triangle.nodes[0]);
        const Node* b = mesh.findNode(triangle.nodes[1]);
        const Node* c = mesh.findNode(triangle.nodes[2]);
        if (a == nullptr || b == nullptr || c == nullptr) {
            continue;
        }
        const EdgeSi va{a->position.x.si(), a->position.y.si(), a->position.z.si()};
        const EdgeSi vb{b->position.x.si(), b->position.y.si(), b->position.z.si()};
        const EdgeSi vc{c->position.x.si(), c->position.y.si(), c->position.z.si()};
        total += dot(va, cross(vb, vc));
    }
    return Volume::fromSi(total / 6.0);
}

SurfaceValidation validateSurface(const Mesh& mesh) {
    SurfaceValidation report;

    std::map<std::pair<NodeId, NodeId>, std::size_t> undirectedCount;
    DirectedCount directedCount;
    std::map<std::tuple<NodeId::ValueType, NodeId::ValueType, NodeId::ValueType>, std::size_t> byNodeSet;
    std::map<NodeId, std::size_t> usage;

    for (const Node& node : mesh.nodes()) {
        usage.try_emplace(node.id, 0);
    }

    for (const Triangle& triangle : mesh.triangles()) {
        const std::array<NodeId, 3>& n = triangle.nodes;
        for (const NodeId id : n) {
            ++usage[id];
        }

        // Duplicate detection is on the NODE SET after unification, never on
        // coordinates: two triangles are the same triangle when they join the
        // same three nodes, and that stays true for a future contact interface
        // where two regions legitimately occupy the same place.
        std::array<NodeId::ValueType, 3> sorted{n[0].value(), n[1].value(), n[2].value()};
        std::ranges::sort(sorted);
        ++byNodeSet[std::tuple{sorted[0], sorted[1], sorted[2]}];

        for (std::size_t i = 0; i < 3; ++i) {
            const NodeId from = n[i];
            const NodeId to = n[(i + 1) % 3];
            ++undirectedCount[undirected(from, to)];
            ++directedCount[std::pair{from, to}];
        }

        const Node* a = mesh.findNode(n[0]);
        const Node* b = mesh.findNode(n[1]);
        const Node* c = mesh.findNode(n[2]);
        if (a == nullptr || b == nullptr || c == nullptr) {
            continue; // a missing reference is the data model's complaint, not this one's
        }
        const Area area = triangleArea(a->position, b->position, c->position);
        // Exactly zero or not finite. Three distinct nodes can still be
        // collinear, so distinctness is not enough -- and a THIN triangle is not
        // degenerate, it is P16-QUALITY-001's business.
        if (!isFinite(area) || area.si() == 0.0) {
            ++report.degenerateTriangleCount;
        }
    }

    for (const auto& [key, count] : undirectedCount) {
        if (count == 1) {
            ++report.boundaryEdgeCount;
        } else if (count > 2) {
            ++report.nonManifoldEdgeCount;
        }
    }

    // Orientation coherence: two triangles sharing an edge traverse it in
    // OPPOSITE directions. So a directed edge used more than once means two
    // triangles agree where they should disagree -- a patch welded in with its
    // winding flipped, which edge incidence alone cannot see.
    for (const auto& [directed, count] : directedCount) {
        if (count > 1) {
            report.orientationConflictCount += count - 1;
        }
    }

    for (const auto& [nodes, count] : byNodeSet) {
        if (count > 1) {
            report.duplicateTriangleCount += count - 1;
        }
    }

    for (const auto& [id, uses] : usage) {
        if (uses == 0) {
            ++report.unusedNodeCount;
        }
    }
    return report;
}

Result<EngineeringSurfaceMesh> generateSurfaceMesh(const MeshableGeometry& geometry,
                                                   const SurfaceMeshControls& controls) {
    if (!positiveFinite(controls.linearDeflection) || !positiveFinite(controls.angularDeflection)) {
        return makeError(ErrorCode::InvalidArgument,
                         describe(SurfaceMeshFailure::InvalidControls, {}));
    }

    // One kernel call, on a shape the kernel copies with copyMesh=false, so no
    // triangulation cached on the authoritative faces is read and none is left
    // behind. The engineering controls are the ones used; nothing else can have
    // supplied them.
    const Result<geometry::Mesh> raw =
        geometry::triangulate(geometry.body, geometry::MeshOptions{controls.linearDeflection,
                                                                  controls.angularDeflection});
    if (!raw) {
        return makeError(ErrorCode::Internal,
                         std::format("{}: {}", describe(SurfaceMeshFailure::TriangulationFailed, {}),
                                     raw.error().message));
    }
    if (raw->triangles.empty() || raw->vertices.empty()) {
        return makeError(ErrorCode::Internal, describe(SurfaceMeshFailure::EmptySurface, {}));
    }

    // Unify nodes. NodeIds are assigned in ascending coordinate order rather than
    // in the order faces were explored, so the numbering depends on the geometry
    // and not on the kernel's traversal.
    std::map<NodeKey, NodeId> unified;
    for (const Point3D& vertex : raw->vertices) {
        unified.try_emplace(keyOf(vertex), NodeId{});
    }
    MeshBuilder builder;
    for (auto& [key, id] : unified) {
        const Result<NodeId> added =
            builder.addNode(Point3D{Length::fromSi(key.x), Length::fromSi(key.y), Length::fromSi(key.z)});
        if (!added) {
            return std::unexpected(added.error());
        }
        id = *added;
    }

    // Oriented connectivity, in a deterministic order. The SORT KEY is the
    // triangle's node set; the STORED winding is the original oriented tuple. The
    // two must stay separate: sorting the connectivity itself would make the
    // element order deterministic by destroying the orientation it exists to
    // carry.
    struct Pending {
        std::array<NodeId::ValueType, 3> key{};
        std::array<NodeId, 3> oriented{};
    };
    std::vector<Pending> pending;
    pending.reserve(raw->triangles.size());
    for (const std::array<std::uint32_t, 3>& triangle : raw->triangles) {
        Pending entry;
        for (std::size_t i = 0; i < 3; ++i) {
            const auto it = unified.find(keyOf(raw->vertices[triangle[i]]));
            if (it == unified.end()) {
                return makeError(ErrorCode::Internal, "surface mesh: a triangle names an unknown vertex");
            }
            entry.oriented[i] = it->second;
            entry.key[i] = it->second.value();
        }
        std::ranges::sort(entry.key);
        pending.push_back(entry);
    }
    std::ranges::stable_sort(pending, [](const Pending& a, const Pending& b) { return a.key < b.key; });

    // One region: a surface is the boundary of one prepared body. ADR-032 gives a
    // mesh one region per solid, and a multi-solid body's boundary is validated as
    // a whole here -- each component must close on its own, which edge incidence
    // checks without needing to know which component a triangle is in.
    constexpr RegionId kSurface = RegionId::fromValue(1);
    for (const Pending& entry : pending) {
        const Result<ElementId> added = builder.addTriangle(entry.oriented, kSurface);
        if (!added) {
            return std::unexpected(added.error());
        }
    }

    EngineeringSurfaceMesh surface;
    surface.mesh = builder.build();
    surface.validation = validateSurface(surface.mesh);
    surface.revision = geometry.revision;
    surface.controls = controls;
    surface.area = surfaceArea(surface.mesh);
    surface.enclosedVolume = enclosedVolume(surface.mesh);
    surface.faceCount = raw->faces.size();

    if (!surface.validation.valid()) {
        return makeError(ErrorCode::Internal,
                         describe(SurfaceMeshFailure::NotAValidBoundary, surface.validation));
    }
    // Closed and coherent, but which way out? A surface can be perfectly
    // manifold and face inward, and no edge count would notice.
    if (!(surface.enclosedVolume.si() > 0.0) || !isFinite(surface.enclosedVolume)) {
        return makeError(ErrorCode::Internal,
                         describe(SurfaceMeshFailure::InwardOrientation, surface.validation));
    }
    return surface;
}

Result<EngineeringSurfaceMesh> surfaceMeshFor(const Document& document,
                                              const features::Regenerator& regenerator, ObjectId feature,
                                              const SurfaceMeshControls& controls) {
    const Result<MeshableGeometry> prepared = requireMeshableGeometry(document, regenerator, feature);
    if (!prepared) {
        // Returned unchanged: a stale or failed model is refused here with the
        // same diagnostic P16-GEOM-001 gives, because this milestone has no
        // second opinion about what is meshable.
        return std::unexpected(prepared.error());
    }
    return generateSurfaceMesh(*prepared, controls);
}

} // namespace bettercad::meshing
