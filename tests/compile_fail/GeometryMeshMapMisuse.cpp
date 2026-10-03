// Build-failure tests for geometry/mesh correspondence (see CMakeLists.txt in
// this directory). Built once without any BETTERCAD_CF_* macro as a control,
// which must compile -- so every failure below is attributable to its own line
// and not to a missing header.
//
// What these prove is the read-only contract of P16-MAP-001. A mapping answers
// questions about a geometry and a mesh; it must not be able to heal CAD,
// regenerate a feature, move a node or re-triangulate anything. A runtime
// comparison shows that today's implementation does not; these show that no
// implementation behind this API could, because there is no mutable overload to
// write through.
//
// They also pin ADR-030's rule for this type: possessing a GeometryMeshMap is
// the evidence that a geometry and a mesh were checked to be the same pair, so
// only the building path may construct one.
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>

using namespace bettercad;
using namespace bettercad::meshing;

namespace {

/// The signatures the API really has. Spelling them out means a widening of
/// any of them is a build failure here rather than a silent change.
using ObservingBuild = Result<GeometryMeshMap> (*)(const MeshableGeometry&, const VolumeMesh&);
using ObservingNodes = Result<std::vector<NodeId>> (*)(const GeometryMeshMap&, const Mesh&,
                                                        std::span<const ElementId>);
using ObservingRegion = Result<MappedRegion> (*)(const GeometryMeshMap&, const Mesh&);

} // namespace

int main() {
    // The control: the observing API, bound through the pointer types above.
    const ObservingBuild build = &buildGeometryMeshMap;
    const ObservingNodes nodes = &boundaryNodesOf;
    const ObservingRegion region = &regionOf;
    (void)build;
    (void)nodes;
    (void)region;

    // And a set is an ordinary value a caller may hold and check.
    const NamedBoundarySet set{BoundarySetId::fromValue(1), "fixed", {}};
    (void)validate(set);

#ifdef BETTERCAD_CF_BUILD_MAP_FROM_A_MUTABLE_GEOMETRY
    // If the builder took a mutable geometry it could regenerate or heal the
    // body it is supposed to be describing.
    using MutatingBuild = Result<GeometryMeshMap> (*)(MeshableGeometry&, const VolumeMesh&);
    const MutatingBuild mutating = &buildGeometryMeshMap;
    (void)mutating;
#endif

#ifdef BETTERCAD_CF_QUERY_THROUGH_A_MUTABLE_MESH
    // If a query took a mutable mesh it could move a node while answering
    // which nodes lie on a face.
    using MutatingNodes = Result<std::vector<NodeId>> (*)(const GeometryMeshMap&, Mesh&,
                                                           std::span<const ElementId>);
    const MutatingNodes mutating = &boundaryNodesOf;
    (void)mutating;
#endif

#ifdef BETTERCAD_CF_FABRICATE_A_MAP
    // A correspondence out of thin air, carrying no evidence that any geometry
    // and any mesh were ever checked against each other.
    const GeometryMeshMap fabricated;
    (void)fabricated.faces();
#endif

#ifdef BETTERCAD_CF_WRITE_THROUGH_A_MAPPED_FACE
    // The faces are a read-only view: a caller cannot edit the attribution it
    // was given and pass it on as the map's answer.
    const GeometryMeshMap* map = nullptr;
    map->faces()[0].facets.clear();
#endif

#ifdef BETTERCAD_CF_REACH_THE_MESH_THROUGH_THE_MAP
    // A map holds no mesh and no reference to one, so there is nothing to
    // write through and nothing to dangle once the mesh is gone.
    const GeometryMeshMap* map = nullptr;
    const Mesh& aliased = map->mesh;
    (void)aliased;
#endif

    return 0;
}
