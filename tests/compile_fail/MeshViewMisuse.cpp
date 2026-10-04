// Build-failure tests for the mesh visualisation adapter (P16-VIZ-001).
//
// Built once without any BETTERCAD_CF_* macro as a control, which must
// compile -- so every failure below is attributable to its own line.
//
// WHAT THESE PROVE. The runtime tests show that today's implementation does
// not confuse a render index with an engineering identity, and does not change
// a mesh. These show that NO implementation behind these types could: the
// confusions are not caught by a check, they are unrepresentable.
//
// The sharpest one is MUTATE_A_MESH. A meshing::Mesh has no mutator at all --
// addNode and its siblings are on MeshBuilder -- so a GUI holding a mesh
// cannot change it even by mistake, even with a non-const reference. That is
// the difference between an invariant that is enforced and one that is merely
// observed.

#include <bettercad/core/math/Point.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/renderer/MeshView.hpp>

#include <cstddef>

using namespace bettercad;
using namespace bettercad::renderer;

int main() {
    // The control: the translation that IS allowed, spelled out so a widening
    // of either direction becomes a build failure here.
    using VertexToNode = Result<meshing::NodeId> (MeshView::*)(std::size_t) const;
    using TriangleToElement = Result<meshing::ElementId> (MeshView::*)(std::size_t) const;
    const VertexToNode vertexToNode = &MeshView::nodeOfVertex;
    const TriangleToElement triangleToElement = &MeshView::elementOfTriangle;
    (void)vertexToNode;
    (void)triangleToElement;

    [[maybe_unused]] const meshing::NodeId node = meshing::NodeId::fromValue(1);
    [[maybe_unused]] const meshing::ElementId element = meshing::ElementId::fromValue(1);
    [[maybe_unused]] const std::size_t renderIndex = 0U;

#ifdef BETTERCAD_CF_NODE_ID_FROM_A_RENDER_INDEX
    // A position in a GPU buffer is not a node. If this compiled, every
    // vertex loop would be one keystroke away from inventing identities.
    const meshing::NodeId fabricated = renderIndex;
    (void)fabricated;
#endif

#ifdef BETTERCAD_CF_ELEMENT_ID_FROM_A_RENDER_INDEX
    const meshing::ElementId fabricated = renderIndex;
    (void)fabricated;
#endif

#ifdef BETTERCAD_CF_RENDER_INDEX_FROM_A_NODE_ID
    // And not the other way either: a NodeId is not an offset into anything.
    // The mesh enumerates nodes in ascending NodeId, which makes the mistake
    // look plausible and usually right -- a boundary view's buffer holds only
    // the nodes it draws, so it is wrong exactly when a volume mesh has
    // interior nodes.
    const std::size_t offset = node;
    (void)offset;
#endif

#ifdef BETTERCAD_CF_MUTATE_A_MESH
    // THE INVARIANT, STRUCTURALLY. There is no mutator to call: a finished
    // Mesh is immutable by API, and that is what discharges "rendering does
    // not mutate the mesh" without relying on anyone remembering a const.
    meshing::Mesh mesh;
    (void)mesh.addNode(Point3D{});
#endif

#ifdef BETTERCAD_CF_REACH_A_MESH_THROUGH_A_VIEW
    // A MeshView holds buffers and two lookup tables. It does not hold a mesh,
    // so a caller cannot read engineering data out of the render cache --
    // which is what would let the GUI become a second source of truth.
    const MeshView* view = nullptr;
    (void)view->mesh();
#endif

    return 0;
}
