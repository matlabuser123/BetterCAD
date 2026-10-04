// Build-failure tests for meshing commands (see CMakeLists.txt in this
// directory). Built once without any BETTERCAD_CF_* macro as a control, which
// must compile.
//
// THE ABSENCES ARE THE MILESTONE'S CENTRAL CLAIM. P16-CMD-001's rule is that
// the undo history holds canonical meshing intent and never a generated mesh.
// A runtime test can show that no mesh WAS stored; these show that none CAN
// be, which is the stronger statement and the one that survives the next
// person editing the commands.
//
//   - A generated mesh cannot be default-constructed, so it cannot become a
//     member of the aggregate a command copies. ADR-030 makes the constructors
//     private precisely so that a mesh cannot exist without a generator having
//     produced it.
//   - A `MeshControlDefinition` has no node array, no element array, no
//     quality report and no mapping. Its `mesh` member is `VolumeMeshControls`
//     -- the request, not the result.
//   - Local sizing is keyed on a `FaceName`. A mesh-local `NodeId` or
//     `ElementId` cannot stand in for one, and neither can a bare `ObjectId`:
//     a face is not a feature.
//   - A boundary set holds CAD face references, never the facets they
//     currently resolve to.
//   - Commands are typed on `MeshControlId`. An `ObjectId` cannot stand in for
//     one, because an object is not a meshing control just because it has an
//     ID.
//   - Commands are not copyable, so history cannot come to hold two owners of
//     one command.
#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/meshing/MeshingCommands.hpp>

using namespace bettercad;
using namespace bettercad::literals;

int main() {
    Document document{"Part"};
    CommandHistory history;

    // The control: the intended way to say all of this.
    meshing::MeshControlDefinition definition;
    definition.body = ObjectId::fromValue(1U);
    definition.mesh.sizing.globalTargetSize = 10_mm;
    const FaceName face{ObjectId::fromValue(1U), FaceSelector{.role = FaceRole::EndCap}};
    definition.mesh.sizing.local.push_back(
        meshing::LocalMeshSizing{.face = face, .targetSize = 2_mm});
    definition.boundarySets.push_back(meshing::NamedBoundarySet{
        .id = BoundarySetId::fromValue(1U), .name = "fixed", .faces = {face}});

    const MeshControlId control = MeshControlId::fromValue(1U);
    auto global = std::make_unique<meshing::SetGlobalMeshSizeCommand>(
        control, std::optional<Length>{5_mm});
    auto local = std::make_unique<meshing::AddLocalMeshSizingCommand>(control, face, 1_mm);
    if (!history.execute(document, std::move(global)).has_value()) {
        // Expected: there is no such control in this bare document. The point
        // of the control build is that it COMPILES.
    }
    (void)local;

#ifdef BETTERCAD_CF_MESH_IS_NOT_DEFAULT_CONSTRUCTIBLE
    // A generated mesh cannot be conjured, so it cannot be a member of
    // anything a command copies about.
    meshing::VolumeMesh conjured;
    (void)conjured;
#endif

#ifdef BETTERCAD_CF_DEFINITION_HAS_NO_NODES
    // Canonical intent has no node array.
    definition.nodes.clear();
#endif

#ifdef BETTERCAD_CF_DEFINITION_HAS_NO_ELEMENTS
    // Nor an element array.
    definition.elements.clear();
#endif

#ifdef BETTERCAD_CF_DEFINITION_HAS_NO_QUALITY_REPORT
    // Nor a quality report: `quality` is the threshold POLICY, and a report is
    // measured from a mesh that does not exist yet.
    (void)definition.quality.elements;
#endif

#ifdef BETTERCAD_CF_DEFINITION_HAS_NO_GENERATED_MESH
    // Its `mesh` member is the REQUEST. There is no result in here.
    (void)definition.mesh.tetrahedra();
#endif

#ifdef BETTERCAD_CF_LOCAL_SIZING_IS_NOT_KEYED_ON_A_NODE
    // A mesh-local identity cannot be a sizing target.
    definition.mesh.sizing.local.push_back(
        meshing::LocalMeshSizing{.face = meshing::NodeId::fromValue(4U), .targetSize = 2_mm});
#endif

#ifdef BETTERCAD_CF_LOCAL_SIZING_IS_NOT_KEYED_ON_AN_ELEMENT
    definition.mesh.sizing.local.push_back(
        meshing::LocalMeshSizing{.face = meshing::ElementId::fromValue(4U), .targetSize = 2_mm});
#endif

#ifdef BETTERCAD_CF_A_COMMAND_WILL_NOT_TAKE_A_FEATURE_FOR_A_FACE
    // A FaceName is a feature AND a selector, and a command will not accept the
    // feature alone.
    //
    // PROBED AT THE CALL SITE, DELIBERATELY. The obvious probe --
    // `LocalMeshSizing{.face = someObjectId}` -- COMPILES: `FaceName` is an
    // aggregate, so brace elision initializes its FIRST member and the
    // ObjectId silently becomes `feature`, leaving a default EndCap selector.
    // That is a property of C++ aggregate initialization, not something
    // `FaceName` can refuse without gaining a constructor, and a core change
    // of that kind is outside this milestone. What CAN be guaranteed is the
    // path the commands expose, where a parameter typed `FaceName` is
    // copy-initialized and no elision applies. See ADVERSARIAL_REVIEW.md
    // finding A9.
    auto wrongTarget = std::make_unique<meshing::AddLocalMeshSizingCommand>(
        control, ObjectId::fromValue(1U), 2_mm);
    (void)wrongTarget;
#endif

#ifdef BETTERCAD_CF_BOUNDARY_SET_HAS_NO_FACETS
    // A set holds CAD faces. The facets it resolves to belong to one mesh and
    // are wrong at the next remesh.
    definition.boundarySets.front().facets.clear();
#endif

#ifdef BETTERCAD_CF_COMMAND_IS_KEYED_ON_A_MESH_CONTROL_ID
    // An ObjectId is not a MeshControlId.
    auto wrong = std::make_unique<meshing::SetGlobalMeshSizeCommand>(
        ObjectId::fromValue(1U), std::optional<Length>{5_mm});
    (void)wrong;
#endif

#ifdef BETTERCAD_CF_COMMAND_HAS_NO_MESH_SETTER
    // There is no way to hand a command a mesh after the fact.
    meshing::SetGlobalMeshSizeCommand edit{control, std::optional<Length>{5_mm}};
    edit.setMesh(nullptr);
#endif

#ifdef BETTERCAD_CF_COMMAND_IS_NOT_COPYABLE
    meshing::SetGlobalMeshSizeCommand original{control, std::optional<Length>{5_mm}};
    meshing::SetGlobalMeshSizeCommand copy{original};
    (void)copy;
#endif

#ifdef BETTERCAD_CF_MESHER_DOES_NOT_HAND_OUT_A_MUTABLE_MESH
    // The mesher owns its derived meshes; a caller cannot take one and change
    // it, which is what would make a "mesh" and "the mesh it was generated
    // from" disagree.
    meshing::Mesher mesher;
    meshing::VolumeMesh* taken = mesher.mesh(control);
    (void)taken;
#endif

    return 0;
}
