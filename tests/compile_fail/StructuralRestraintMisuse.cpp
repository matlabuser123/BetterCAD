// Build-failure tests for the restraint model (see CMakeLists.txt in this
// directory). Built once without any BETTERCAD_CF_* macro as a control, which
// must compile -- so every failure below is attributable to its own line and
// not to a missing header.
//
// Two claims of P17-BC-001 are enforced here rather than reviewed:
//
//   1. A CANONICAL RESTRAINT CANNOT HOLD A MESH HANDLE. Its identity is the
//      intent; the facets, the nodes and the indices are a consequence of the
//      mesh as it is now. A restraint carrying a NodeId or a DofIndex would
//      survive a remesh naming material that had moved or ceased to exist --
//      which is the whole class of defect the milestone exists to prevent, and
//      a size assertion in the test suite cannot catch an attempt to pass one.
//
//   2. A PREPARED RESTRAINT SET CANNOT BE CONJURED. It has a private
//      constructor and exactly one friend, so possessing one is the evidence
//      that every restraint resolved against a real mesh and was numbered
//      against that mesh's own numbering (ADR-036). A hand-built set would
//      defeat every check in the module -- and so would a default-constructed
//      one, which is why there is not one.
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/structural/StructuralConstraints.hpp>
#include <bettercad/structural/StructuralRestraint.hpp>

using namespace bettercad;
using namespace bettercad::structural;

namespace {

void takesRestraint(const StructuralRestraint& /*unused*/) {}
void takesComponents(RestraintComponents /*unused*/) {}

} // namespace

int main() {
    const RestraintId id = RestraintId::fromValue(1);
    const FaceName face{ObjectId::fromValue(1), FaceSelector{.role = FaceRole::EndCap}};

    // The legitimate spellings, so the control compiles.
    takesRestraint(StructuralRestraint::fixedSupport(id, face));
    takesRestraint(StructuralRestraint{id, face, RestraintComponents::fixed()});
    takesComponents(RestraintComponents::along(DofComponent::Ux));

#if defined(BETTERCAD_CF_RESTRAINT_FROM_NODE_ID)
    // A node handle is not a CAD target. The target is canonical and the node
    // is derived, and the two are not interchangeable in either direction.
    takesRestraint(
        StructuralRestraint{id, meshing::NodeId::fromValue(1), RestraintComponents::fixed()});
#endif

#if defined(BETTERCAD_CF_RESTRAINT_FROM_DOF_INDEX)
    // Nor is a solver index. A restraint names displacement COMPONENTS, which
    // are resolved to indices against a numbering it never sees.
    takesRestraint(StructuralRestraint{id, face, DofIndex::fromValue(1)});
#endif

#if defined(BETTERCAD_CF_COMPONENTS_FROM_INTEGER)
    // The mask is a strong type over DofComponent, not a 1|2|4 bitfield: a
    // reader never has to know that 7 means fixed, and a count, an offset or
    // an index cannot be passed where a mask belongs.
    takesComponents(7);
#endif

#if defined(BETTERCAD_CF_COMPONENTS_AS_INTEGER)
    // And it does not decay the other way either, so a mask cannot be summed,
    // compared against a DOF count or used to index anything.
    const unsigned int bits = RestraintComponents::fixed();
    (void)bits;
#endif

#if defined(BETTERCAD_CF_COMPONENTS_FROM_DISPLACEMENT)
    // Prescribed non-zero displacement is deliberately deferred, so there is
    // nowhere in the schema for a magnitude -- not even by implicit
    // conversion into the mask.
    takesComponents(Length::fromSi(0.001));
#endif

#if defined(BETTERCAD_CF_PREPARED_RESTRAINTS_CONSTRUCTED_DIRECTLY)
    // Possession is the evidence. Only prepareStructuralRestraints may build
    // one, because only it has checked that every target resolved and that the
    // numbering describes this mesh.
    const PreparedRestraints prepared{};
    (void)prepared;
#endif

    return 0;
}
