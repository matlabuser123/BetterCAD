// Build-failure tests for mesh quality evaluation (see CMakeLists.txt in this
// directory). Built once without any BETTERCAD_CF_* macro as a control, which
// must compile -- so every failure below is attributable to its own line and
// not to a missing header.
//
// What these prove is P16-QUALITY-001's central restriction: THIS LAYER
// OBSERVES AND CLASSIFIES. It never moves a node, reorders connectivity, drops
// an element or hands back a repaired mesh. "Bad Tet -> report bad Tet", never
// "bad Tet -> optimise -> silently replace the mesh".
//
// A comment cannot enforce that, and a test that merely compares a mesh before
// and after cannot either -- it only shows that TODAY's implementation does not
// mutate. These lines show that an implementation COULD not: the entry points
// take a const Mesh&, so no overload exists to mutate through, and the report
// hands back no route to the mesh it described.
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshQuality.hpp>

using namespace bettercad;
using namespace bettercad::meshing;

namespace {

/// The signatures the API really has. Spelling them out means a change to
/// either one is a build failure here rather than a silent widening.
using ObservingMeshEvaluate = MeshQualityReport (*)(const Mesh&, const QualityThresholds&);
using ObservingTetEvaluate = Result<TetQuality> (*)(const Mesh&, const Tetrahedron&);

} // namespace

int main() {
    MeshBuilder builder;
    [[maybe_unused]] const auto a = builder.addNode(Point3D{});
    const Mesh mesh = builder.build();

    // The control: the observing API, bound through the pointer types above, and
    // a report read for its values.
    const ObservingMeshEvaluate observeMesh = &evaluateMeshQuality;
    const ObservingTetEvaluate observeTet = &evaluateTetQuality;
    const MeshQualityReport report = observeMesh(mesh, reportOnlyThresholds());
    (void)report.satisfiesPolicy();
    (void)report.tets.size();
    (void)observeTet;

#ifdef BETTERCAD_CF_EVALUATE_MESH_THROUGH_A_MUTABLE_REFERENCE
    // If evaluateMeshQuality took a mutable Mesh&, this pointer would bind. It
    // does not, so the mesh cannot be written to from inside.
    using MutatingMeshEvaluate = MeshQualityReport (*)(Mesh&, const QualityThresholds&);
    const MutatingMeshEvaluate mutating = &evaluateMeshQuality;
    (void)mutating;
#endif

#ifdef BETTERCAD_CF_EVALUATE_TET_THROUGH_A_MUTABLE_REFERENCE
    // The same for the per-element entry point, which is the one an optimiser
    // would be written against.
    using MutatingTetEvaluate = Result<TetQuality> (*)(Mesh&, const Tetrahedron&);
    const MutatingTetEvaluate mutating = &evaluateTetQuality;
    (void)mutating;
#endif

#ifdef BETTERCAD_CF_REACH_THE_MESH_THROUGH_THE_REPORT
    // A report is a set of VALUES. It holds no mesh, no reference to one and no
    // handle back to one, so there is nothing for a caller to write through and
    // nothing to dangle once the mesh is gone.
    const Mesh& aliased = report.mesh;
    (void)aliased;
#endif

#ifdef BETTERCAD_CF_REPAIRED_MESH_FROM_A_REPORT
    // And no repaired mesh comes back out of evaluation: the overload that would
    // return one does not exist.
    const Mesh repaired = evaluateMeshQuality(mesh, reportOnlyThresholds());
    (void)repaired;
#endif

    return 0;
}
