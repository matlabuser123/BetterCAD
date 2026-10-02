// Build-failure tests for the validated volume mesh (see CMakeLists.txt in
// this directory). Built once without any BETTERCAD_CF_* macro as a control,
// which must compile -- so every failure below is attributable to its own line
// and not to a missing header.
//
// What these prove is ADR-030: "A mesh reaches a solver only as a
// ValidatedMesh, which only the validating path can construct. The rule is
// enforced by the type system, not by documentation."
//
// `VolumeMesh` is that token for volume meshes. Possessing one is the evidence
// that the mesh passed validation, conformity and volume recovery, because
// `generateVolumeMesh` is its only friend and refuses to return one otherwise.
// If any line below started compiling, that evidence would become a claim
// anybody could make.
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>

using namespace bettercad;
using namespace bettercad::meshing;

namespace {

void takesValidated(const VolumeMesh& /*unused*/) {}

} // namespace

int main() {
    // The control: a VolumeMesh obtained the only way there is may be read and
    // passed on. Nothing here constructs one -- there is no way to -- so the
    // control exercises the READING contract a solver depends on.
    const auto readVolume = [](const VolumeMesh& mesh) {
        takesValidated(mesh);
        return mesh.tetrahedronCount() + mesh.nodeCount() + mesh.boundaryTriangleCount();
    };
    (void)readVolume;

    // An ordinary Mesh is freely constructible, and must stay so: inspection
    // and visualisation (P16-VIZ-001) need to hold a mesh that may be invalid.
    MeshBuilder builder;
    [[maybe_unused]] const auto node = builder.addNode(Point3D{});
    [[maybe_unused]] const Mesh plain = builder.build();

#ifdef BETTERCAD_CF_DEFAULT_CONSTRUCT_VOLUME_MESH
    // A solver-grade mesh out of thin air, carrying no evidence of anything.
    const VolumeMesh fabricated;
    takesValidated(fabricated);
#endif

#ifdef BETTERCAD_CF_VOLUME_MESH_FROM_PLAIN_MESH
    // The dangerous one: an unvalidated Mesh promoted to a validated volume
    // mesh. If this compiled, every guarantee in the type would be decorative.
    const VolumeMesh promoted{plain};
    takesValidated(promoted);
#endif

#ifdef BETTERCAD_CF_MUTATE_VOLUME_MESH
    // A VolumeMesh must expose no way to change the mesh it is evidence about.
    const VolumeMesh* mesh = nullptr;
    mesh->mesh() = plain;
#endif

    return 0;
}
