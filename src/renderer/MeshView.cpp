// The read-only visualisation adapter (P16-VIZ-001).
//
// NO OCCT AND NO QT IN THIS FILE, which is why it lives in src/renderer/ and
// not in src/renderer/occt/. Everything here is arithmetic over the canonical
// mesh and two lookup tables, and all of it is assertable from a headless
// test.

#include <bettercad/renderer/MeshView.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <utility>

namespace bettercad::renderer {
namespace {

/// The four faces of a Tet4, outward for a POSITIVELY oriented tetrahedron.
///
/// P16-DATA-001 fixes the orientation convention: the signed volume
/// V = 1/6 (p2-p1) . ((p3-p1) x (p4-p1)) is positive for a valid element, and
/// nothing in the project takes its absolute value. These four windings are
/// the ones whose right-hand normal points AWAY from the vertex each face
/// omits, under that convention -- which is a property, so it is tested rather
/// than asserted in a comment (MeshView_TetFaceWindingsPointAwayFromTheOppositeVertex).
constexpr std::array<std::array<std::size_t, 3>, 4> kTetFaces{{
    {{0U, 2U, 1U}}, // omits node 4
    {{0U, 1U, 3U}}, // omits node 3
    {{1U, 2U, 3U}}, // omits node 1
    {{0U, 3U, 2U}}, // omits node 2
}};

} // namespace

Result<MeshView> MeshView::build(const meshing::Mesh& mesh, MeshSource source,
                                 std::vector<Facet> facets) {
    if (facets.empty()) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("mesh view: {} has nothing to draw",
                                     toString(source)));
    }

    // VERTICES ARE THE NODES ACTUALLY DRAWN, in ascending NodeId. A volume
    // mesh's interior nodes are not in a boundary view's buffer at all, so
    // vertexCount() means something to a reader instead of being the whole
    // mesh's node count whatever is on screen.
    std::vector<meshing::NodeId> drawn;
    drawn.reserve(facets.size() * 3U);
    for (const Facet& facet : facets) {
        for (const meshing::NodeId node : facet.nodes) {
            drawn.push_back(node);
        }
    }
    std::ranges::sort(drawn, [](meshing::NodeId a, meshing::NodeId b) {
        return a.value() < b.value();
    });
    const auto duplicates = std::ranges::unique(drawn, [](meshing::NodeId a, meshing::NodeId b) {
        return a.value() == b.value();
    });
    drawn.erase(duplicates.begin(), duplicates.end());

    MeshView view;
    view.source_ = source;
    view.stamp_ = mesh.stamp();
    view.nodeOfVertex_ = std::move(drawn);
    view.positions_.reserve(view.nodeOfVertex_.size() * 3U);
    for (const meshing::NodeId node : view.nodeOfVertex_) {
        const meshing::Node* found = mesh.findNode(node);
        if (found == nullptr) {
            // Unreachable through the public factories, which take their nodes
            // from the mesh's own elements. Refused rather than assumed, so a
            // future caller cannot reach a half-built buffer.
            return makeError(ErrorCode::Internal,
                             std::format("mesh view: node {} is referenced by an element but "
                                         "is not in the mesh",
                                         node));
        }
        view.positions_.push_back(found->position.x.si());
        view.positions_.push_back(found->position.y.si());
        view.positions_.push_back(found->position.z.si());
    }

    const auto vertexOf = [&view](meshing::NodeId node) -> std::uint32_t {
        const auto at = std::ranges::lower_bound(view.nodeOfVertex_, node,
                                                 [](meshing::NodeId a, meshing::NodeId b) {
                                                     return a.value() < b.value();
                                                 });
        return static_cast<std::uint32_t>(at - view.nodeOfVertex_.begin());
    };

    view.triangleIndices_.reserve(facets.size() * 3U);
    view.elementOfTriangle_.reserve(facets.size());
    // (min node, max node) -> the edge, so a shared edge is drawn once.
    std::vector<std::pair<meshing::NodeId::ValueType, meshing::NodeId::ValueType>> edges;
    edges.reserve(facets.size() * 3U);
    for (const Facet& facet : facets) {
        for (std::size_t corner = 0U; corner < 3U; ++corner) {
            view.triangleIndices_.push_back(vertexOf(facet.nodes[corner]));
            const meshing::NodeId::ValueType a = facet.nodes[corner].value();
            const meshing::NodeId::ValueType b = facet.nodes[(corner + 1U) % 3U].value();
            edges.emplace_back(std::min(a, b), std::max(a, b));
        }
        view.elementOfTriangle_.push_back(facet.element);
    }

    // EDGE DEDUPLICATION IS A RENDERING CONCERN AND NOTHING ELSE. The key is
    // the unordered node pair, canonical topology is untouched, and the result
    // is sorted so the buffer is identical on every run and in every preset.
    std::ranges::sort(edges);
    const auto repeated = std::ranges::unique(edges);
    edges.erase(repeated.begin(), repeated.end());
    view.edgeIndices_.reserve(edges.size() * 2U);
    for (const auto& [low, high] : edges) {
        view.edgeIndices_.push_back(vertexOf(meshing::NodeId::fromValue(low)));
        view.edgeIndices_.push_back(vertexOf(meshing::NodeId::fromValue(high)));
    }

    return view;
}

std::vector<MeshView::Facet> MeshView::facetsOfTriangles(const meshing::Mesh& mesh) {
    std::vector<Facet> facets;
    facets.reserve(mesh.triangles().size());
    for (const meshing::Triangle& triangle : mesh.triangles()) {
        facets.push_back(Facet{triangle.id, triangle.nodes});
    }
    return facets;
}

std::string_view toString(MeshSource source) noexcept {
    switch (source) {
    case MeshSource::EngineeringSurface:
        return "the engineering surface mesh";
    case MeshSource::VolumeBoundary:
        return "the volume mesh boundary";
    case MeshSource::SelectedTetrahedra:
        return "the selected tetrahedra";
    case MeshSource::VolumeBoundaryByTetrahedron:
        return "the volume mesh boundary, by tetrahedron";
    }
    return "an unknown mesh source";
}

Result<MeshView> MeshView::surfaceOf(const meshing::Mesh& mesh) {
    return build(mesh, MeshSource::EngineeringSurface, facetsOfTriangles(mesh));
}

Result<MeshView> MeshView::volumeBoundaryOf(const meshing::Mesh& mesh) {
    // THE BOUNDARY IS ALREADY A SET OF ELEMENTS. A volume mesh carries its
    // boundary as Triangle3, so there is no face-incidence counting here and
    // no interior tetrahedron face can reach the buffer. One canonical
    // definition of "boundary", and it is P16-VOL-001's.
    return build(mesh, MeshSource::VolumeBoundary, facetsOfTriangles(mesh));
}

Result<std::vector<MeshView::Facet>> MeshView::facetsOfTetrahedronExterior(
    const meshing::Mesh& mesh) {
    // ONE ENTRY PER TETRAHEDRON FACE, keyed on the SORTED node triple.
    //
    // The key is sorted because that is how two tetrahedra are recognised as
    // sharing a face. The EMITTED nodes stay UNSORTED, because the winding
    // from kTetFaces is what makes the normal point out of the tetrahedron --
    // and for an exterior face, out of the solid. Confusing the two would draw
    // a boundary with arbitrary normals.
    struct Keyed {
        std::array<meshing::NodeId, 3> key{};
        meshing::ElementId element{};
        std::array<meshing::NodeId, 3> nodes{};
    };

    std::vector<Keyed> all;
    all.reserve(mesh.tetrahedra().size() * 4U);
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        for (const std::array<std::size_t, 3>& face : kTetFaces) {
            const std::array<meshing::NodeId, 3> nodes{tet.nodes[face[0]], tet.nodes[face[1]],
                                                       tet.nodes[face[2]]};
            std::array<meshing::NodeId, 3> key = nodes;
            std::ranges::sort(key);
            all.push_back(Keyed{key, tet.id, nodes});
        }
    }

    // SORTED, NEVER HASHED. An unordered_map would make the facet order depend
    // on the standard library's bucketing, and this class promises
    // byte-identical buffers in Debug, Release and Debug-shared.
    std::ranges::sort(all, [](const Keyed& a, const Keyed& b) {
        if (a.key != b.key) {
            return a.key < b.key;
        }
        return a.element < b.element;
    });

    std::vector<Facet> facets;
    facets.reserve(all.size());
    std::size_t at = 0U;
    while (at < all.size()) {
        std::size_t run = at + 1U;
        while (run < all.size() && all[run].key == all[at].key) {
            ++run;
        }
        const std::size_t shared = run - at;
        if (shared == 1U) {
            // EXTERIOR: exactly one tetrahedron carries this face.
            facets.push_back(Facet{all[at].element, all[at].nodes});
        } else if (shared > 2U) {
            // NON-MANIFOLD, AND REFUSED BY NAME. Treating it as exterior would
            // draw a plausible surface over a mesh the generator got wrong, and
            // picking one of the claimants would be a silent choice.
            return makeError(
                ErrorCode::FailedPrecondition,
                std::format("mesh view: the face on nodes {}, {} and {} is carried by {} "
                            "tetrahedra, so this mesh is not manifold",
                            all[at].key[0].value(), all[at].key[1].value(),
                            all[at].key[2].value(), shared));
        }
        at = run;
    }

    // ASCENDING ElementId, which is what this class documents its triangles to
    // be. `build` sorts the vertices and the edges but NOT the facets, so the
    // triangle order is the order they arrive in -- and the walk above
    // produced them in key order, not element order.
    std::ranges::sort(facets, [](const Facet& a, const Facet& b) {
        if (a.element != b.element) {
            return a.element < b.element;
        }
        return a.nodes < b.nodes;
    });
    return facets;
}

Result<MeshView> MeshView::volumeBoundaryByTetrahedronOf(const meshing::Mesh& mesh) {
    if (mesh.tetrahedra().empty()) {
        // REFUSED, not answered with an empty view. This factory exists to name
        // tetrahedra, and a mesh with none has nothing to name.
        return makeError(ErrorCode::FailedPrecondition,
                         "mesh view: this mesh has no tetrahedron, so its boundary cannot be "
                         "derived from tetrahedra");
    }
    Result<std::vector<Facet>> facets = facetsOfTetrahedronExterior(mesh);
    if (!facets.has_value()) {
        return makeError(facets.error().code, facets.error().message);
    }
    return build(mesh, MeshSource::VolumeBoundaryByTetrahedron, std::move(*facets));
}

Result<MeshView> MeshView::tetrahedraOf(const meshing::Mesh& mesh,
                                        std::span<const meshing::ElementId> tetrahedra) {
    if (tetrahedra.empty()) {
        return makeError(ErrorCode::InvalidArgument,
                         "mesh view: no tetrahedron was given to inspect");
    }
    std::vector<Facet> facets;
    facets.reserve(tetrahedra.size() * 4U);
    for (const meshing::ElementId element : tetrahedra) {
        const meshing::Tetrahedron* tet = mesh.findTetrahedron(element);
        if (tet == nullptr) {
            // REFUSED, not skipped: a caller that asked for four elements and
            // silently got three has been told something false about the mesh.
            return makeError(ErrorCode::NotFound,
                             std::format("mesh view: {} is not a tetrahedron of this mesh",
                                         element));
        }
        for (const std::array<std::size_t, 3>& face : kTetFaces) {
            facets.push_back(Facet{element,
                                   {tet->nodes[face[0]], tet->nodes[face[1]], tet->nodes[face[2]]}});
        }
    }
    return build(mesh, MeshSource::SelectedTetrahedra, std::move(facets));
}

Result<meshing::NodeId> MeshView::nodeOfVertex(std::size_t vertex) const {
    if (vertex >= nodeOfVertex_.size()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("mesh view: vertex {} of {} drawn vertices", vertex,
                                     nodeOfVertex_.size()));
    }
    return nodeOfVertex_[vertex];
}

Result<meshing::ElementId> MeshView::elementOfTriangle(std::size_t triangle) const {
    if (triangle >= elementOfTriangle_.size()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("mesh view: triangle {} of {} drawn triangles", triangle,
                                     elementOfTriangle_.size()));
    }
    return elementOfTriangle_[triangle];
}

Result<std::size_t> MeshView::vertexOfNode(meshing::NodeId node) const {
    const auto at = std::ranges::lower_bound(nodeOfVertex_, node,
                                             [](meshing::NodeId a, meshing::NodeId b) {
                                                 return a.value() < b.value();
                                             });
    if (at == nodeOfVertex_.end() || at->value() != node.value()) {
        return makeError(ErrorCode::NotFound,
                         std::format("mesh view: node {} is not drawn by {}", node,
                                     toString(source_)));
    }
    return static_cast<std::size_t>(at - nodeOfVertex_.begin());
}

std::vector<std::size_t> MeshView::trianglesOfElement(meshing::ElementId element) const {
    std::vector<std::size_t> found;
    for (std::size_t triangle = 0U; triangle < elementOfTriangle_.size(); ++triangle) {
        if (elementOfTriangle_[triangle] == element) {
            found.push_back(triangle);
        }
    }
    return found;
}

} // namespace bettercad::renderer
