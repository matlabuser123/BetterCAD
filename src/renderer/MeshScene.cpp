// One feature's mesh inspection session (P16-VIZ-001).

#include <bettercad/renderer/MeshScene.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace bettercad::renderer {

MeshScene::MeshScene(ObjectId feature, meshing::VolumeMesh volume, meshing::GeometryMeshMap map,
                     MeshQualityView quality)
    : feature_(feature), volume_(std::move(volume)), map_(std::move(map)),
      quality_(std::move(quality)) {}

Result<MeshScene> MeshScene::adopt(ObjectId feature, meshing::VolumeMesh mesh,
                                   meshing::GeometryMeshMap map, MeshQualityView quality) {
    if (!feature.isValid()) {
        return makeError(ErrorCode::InvalidArgument,
                         "mesh scene: the invalid handle cannot name the meshed feature");
    }
    const meshing::MeshStamp& stamp = mesh.mesh().stamp();
    if (!stamp.isValid()) {
        return makeError(ErrorCode::FailedPrecondition,
                         "mesh scene: the mesh carries no generation stamp");
    }
    // THE ONE CHECK THIS CLASS EXISTS FOR, made once and then relied on.
    if (!(map.meshStamp() == stamp)) {
        return makeError(
            ErrorCode::FailedPrecondition,
            std::format("mesh scene: the mapping describes mesh {} generation {}, and the mesh "
                        "is mesh {} generation {}",
                        map.meshStamp().mesh.value(), map.meshStamp().generation,
                        stamp.mesh.value(), stamp.generation));
    }
    if (!quality.describes(mesh.mesh())) {
        return makeError(
            ErrorCode::FailedPrecondition,
            std::format("mesh scene: the quality report describes mesh {} generation {}, and the "
                        "mesh is mesh {} generation {}",
                        quality.stamp().mesh.value(), quality.stamp().generation,
                        stamp.mesh.value(), stamp.generation));
    }
    if (!(mesh.source() == feature)) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("mesh scene: the mesh was built from {} and was offered as "
                                     "the mesh of {}",
                                     mesh.source(), feature));
    }
    return MeshScene{feature, std::move(mesh), std::move(map), std::move(quality)};
}

MeshStatus MeshScene::status(const Document& document,
                            const std::optional<Error>& lastFailure) const {
    MeshHolding holding;
    holding.mesh = volume_;
    holding.lastFailure = lastFailure;
    return statusOf(document, holding);
}

Result<NodeInspection> MeshScene::inspectNode(meshing::NodeId node) const {
    const meshing::Node* found = mesh().findNode(node);
    if (found == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("mesh scene: {} is not a node of this mesh", node));
    }

    NodeInspection inspection;
    inspection.node = node;
    // CANONICAL, WITH ITS UNIT. Not a number out of a render buffer.
    inspection.position = found->position;

    // On the boundary if any boundary facet uses it. Asked of the mesh's own
    // Triangle3 elements, which ARE the boundary -- no second definition.
    for (const meshing::Triangle& facet : mesh().triangles()) {
        if (std::ranges::find(facet.nodes, node) != facet.nodes.end()) {
            inspection.onBoundary = true;
            // And the CAD face behind it, if the map attributes one. The first
            // such facet is enough: a node on an edge between two faces
            // belongs to both, and reporting one of them with its index is
            // honest, where inventing a single owner would not be.
            if (Result<meshing::FacetSource> source = sourceOfFacet(mesh(), map_, facet.id);
                source.has_value()) {
                inspection.source = std::move(*source);
            }
            break;
        }
    }
    return inspection;
}

Result<ElementInspection> MeshScene::inspectElement(meshing::ElementId element) const {
    const std::optional<meshing::ElementType> type = mesh().elementType(element);
    if (!type.has_value()) {
        return makeError(ErrorCode::NotFound,
                         std::format("mesh scene: {} is not an element of this mesh", element));
    }

    ElementInspection inspection;
    inspection.element = element;
    inspection.type = *type;

    if (*type == meshing::ElementType::Tetrahedron4) {
        const meshing::Tetrahedron* tet = mesh().findTetrahedron(element);
        if (tet == nullptr) {
            return makeError(ErrorCode::Internal,
                             std::format("mesh scene: {} is typed as a tetrahedron and is not one",
                                         element));
        }
        inspection.nodes.assign(tet->nodes.begin(), tet->nodes.end());
        // P16-DATA-001's function, not a second implementation of the formula.
        const meshing::Node* p1 = mesh().findNode(tet->nodes[0]);
        const meshing::Node* p2 = mesh().findNode(tet->nodes[1]);
        const meshing::Node* p3 = mesh().findNode(tet->nodes[2]);
        const meshing::Node* p4 = mesh().findNode(tet->nodes[3]);
        if (p1 != nullptr && p2 != nullptr && p3 != nullptr && p4 != nullptr) {
            inspection.signedVolume = meshing::signedVolume(p1->position, p2->position,
                                                            p3->position, p4->position);
        }
        for (const meshing::TetQuality& measured : quality_.report().tets) {
            if (measured.element == element) {
                inspection.tetQuality = measured;
                break;
            }
        }
    } else {
        const meshing::Triangle* facet = mesh().findTriangle(element);
        if (facet == nullptr) {
            return makeError(ErrorCode::Internal,
                             std::format("mesh scene: {} is typed as a triangle and is not one",
                                         element));
        }
        inspection.nodes.assign(facet->nodes.begin(), facet->nodes.end());
        for (const meshing::TriangleQuality& measured : quality_.report().triangles) {
            if (measured.element == element) {
                inspection.triangleQuality = measured;
                break;
            }
        }
        // A boundary facet knows which CAD face it came from.
        if (Result<meshing::FacetSource> source = sourceOfFacet(mesh(), map_, element);
            source.has_value()) {
            inspection.source = std::move(*source);
        }
    }

    // THE REPORT'S CLASSIFICATION. The scene was adopted with a report that
    // describes this mesh, so this cannot fail for an element of it.
    if (Result<meshing::QualityClass> classification = quality_.classOf(mesh(), element);
        classification.has_value()) {
        inspection.classification = *classification;
    }
    return inspection;
}

Result<meshing::ElementId> MeshScene::worstElementFor(meshing::QualityMetric metric) const {
    return quality_.worstElementFor(mesh(), metric);
}

std::vector<MeshScene::FaceEntry> MeshScene::faces() const {
    std::vector<FaceEntry> entries;
    entries.reserve(map_.faces().size());
    for (const meshing::MappedFace& face : map_.faces()) {
        FaceEntry entry;
        entry.index = face.index;
        entry.named = !face.names.empty();
        entry.facetCount = face.facets.size();
        if (entry.named) {
            // Built from the parts that have a spelling: FaceName has no
            // formatter, and inventing one here would be a second naming
            // convention competing with P12-STREF-001's.
            const FaceName& name = face.names.front();
            entry.label = std::format("{} of {}", toString(name.face.role), name.feature);
        } else {
            // AN UNNAMED FACE IS LISTED AND SAID TO BE UNNAMED. P16-MAP-001:
            // cutHole names a hole's flat faces and not its cylindrical wall,
            // so a bored hole's wall has no name -- and it is still mapped and
            // still selectable here, by index. Hiding it, or labelling it with
            // a made-up name, would put the mandatory hole-wall case out of
            // reach.
            entry.label = std::format("{} face {} (unnamed)",
                                      geometry::toString(face.surface), face.index);
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

} // namespace bettercad::renderer
