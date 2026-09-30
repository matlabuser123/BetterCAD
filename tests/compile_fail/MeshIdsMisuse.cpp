// Build-failure tests for mesh-local identity (see CMakeLists.txt in this
// directory). Built once without any BETTERCAD_CF_* macro as a control, which
// must compile -- so every failure below is attributable to its own line and
// not to a missing header.
//
// What these prove is ADR-031: a mesh handle is NOT a CAD identity, and the
// compiler enforces it rather than a reviewer.
#include <bettercad/core/Id.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshIds.hpp>

#include <cstdint>

using namespace bettercad;
using namespace bettercad::meshing;

namespace {

void takesNode(NodeId /*unused*/) {}
void takesElement(ElementId /*unused*/) {}
void takesRegion(RegionId /*unused*/) {}
void takesObject(ObjectId /*unused*/) {}

} // namespace

int main() {
    const NodeId node = NodeId::fromValue(1);
    const ElementId element = ElementId::fromValue(1);
    const RegionId region = RegionId::fromValue(1);

    // Each kind is usable as itself, so the control compiles.
    takesNode(node);
    takesElement(element);
    takesRegion(region);
    takesObject(ObjectId::fromValue(1));

    // A mesh is readable through a const reference: that is the solver-facing
    // contract, and it must keep compiling.
    MeshBuilder builder;
    [[maybe_unused]] const auto added = builder.addNode(Point3D{});
    const Mesh mesh = builder.build();
    [[maybe_unused]] const std::size_t count = mesh.nodeCount();
    [[maybe_unused]] const auto nodes = mesh.nodes();

#if defined(BETTERCAD_CF_NODE_ID_AS_ELEMENT_ID)
    // A node is not an element, though both are 32-bit handles into one mesh.
    takesElement(node);
#elif defined(BETTERCAD_CF_ELEMENT_ID_AS_NODE_ID)
    takesNode(element);
#elif defined(BETTERCAD_CF_REGION_ID_AS_NODE_ID)
    takesNode(region);
#elif defined(BETTERCAD_CF_NODE_ID_AS_REGION_ID)
    takesRegion(node);
#elif defined(BETTERCAD_CF_NODE_ID_AS_OBJECT_ID)
    // THE CENTRAL ONE. A mesh handle must never widen to a document identity:
    // a node is derived state that a remesh destroys, and ObjectId means the
    // opposite (ADR-031).
    takesObject(node);
#elif defined(BETTERCAD_CF_ELEMENT_ID_AS_OBJECT_ID)
    takesObject(element);
#elif defined(BETTERCAD_CF_REGION_ID_AS_OBJECT_ID)
    takesObject(region);
#elif defined(BETTERCAD_CF_OBJECT_ID_AS_NODE_ID)
    // Nor the other way: a document object is not a node because it has an ID.
    takesNode(ObjectId::fromValue(1));
#elif defined(BETTERCAD_CF_SKETCH_ID_AS_NODE_ID)
    takesNode(SketchId::fromValue(1));
#elif defined(BETTERCAD_CF_INTEGER_TO_NODE_ID)
    // Identity never comes from a bare number, so a loop counter or a backend's
    // own index cannot become a NodeId by accident (ADR-033 keeps backend IDs
    // out; this keeps bare integers out).
    [[maybe_unused]] NodeId fromInteger = 5;
#elif defined(BETTERCAD_CF_NODE_ID_TO_INTEGER)
    [[maybe_unused]] std::uint32_t value = node;
#elif defined(BETTERCAD_CF_COMPARE_NODE_AND_ELEMENT)
    [[maybe_unused]] bool same = node == element;
#elif defined(BETTERCAD_CF_MUTATE_NODE_THROUGH_MESH)
    // The solver-facing view is read-only: P17 can read a mesh and cannot move
    // a node through it.
    mesh.nodes()[0].position = Point3D{};
#elif defined(BETTERCAD_CF_ADD_ELEMENT_THROUGH_MESH)
    // Nor insert into it: there is no mutable container to reach.
    mesh.tetrahedra().push_back(Tetrahedron{});
#elif defined(BETTERCAD_CF_TRIANGLE_WITH_TET_CONNECTIVITY)
    // Arity is part of the type: four handles are not a triangle.
    [[maybe_unused]] const auto bad = builder.addTriangle(
        {NodeId::fromValue(1), NodeId::fromValue(2), NodeId::fromValue(3), NodeId::fromValue(4)},
        region);
    // There is deliberately NO case for addTetrahedron({n1, n2, n3}): a braced
    // list with too FEW elements is valid aggregate initialisation of
    // std::array<NodeId, 4> and zero-fills the fourth, so it cannot be a compile
    // error. The zero handle it produces is the invalid handle, and the runtime
    // test MeshBuilder_RejectsAnElementWithAnUnderfilledConnectivityList pins
    // that it is refused. Too MANY elements is a compile error, and is the case
    // above.
#endif

    return 0;
}
