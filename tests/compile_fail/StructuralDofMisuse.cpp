// Build-failure tests for the degree-of-freedom index space (see CMakeLists.txt
// in this directory). Built once without any BETTERCAD_CF_* macro as a control,
// which must compile -- so every failure below is attributable to its own line
// and not to a missing header.
//
// Two claims of P17-DOF-001 are enforced here rather than reviewed:
//
//   1. A DofIndex and a FreeEquationIndex are DIFFERENT TYPES. One names a
//      degree of freedom of the model, the other a row of the reduced system,
//      and they do not agree for any model with a restraint in it. Confusing
//      them assembles a stiffness contribution into the wrong row -- silently,
//      with a plausible answer -- which is why the compiler refuses rather than
//      a reviewer noticing.
//
//   2. A numbering, a constraint set and a free-equation map CANNOT BE
//      CONJURED. Each has a private constructor and exactly one friend, so
//      possession is the evidence that it came from a real mesh (ADR-036). A
//      hand-built map over nodes a mesh does not have would defeat every check
//      in the module.
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/structural/StructuralDof.hpp>

#include <cstdint>

using namespace bettercad;
using namespace bettercad::structural;

namespace {

void takesDof(DofIndex /*unused*/) {}
void takesEquation(FreeEquationIndex /*unused*/) {}
void takesNode(meshing::NodeId /*unused*/) {}
void takesCount(std::uint64_t /*unused*/) {}

} // namespace

int main() {
    const DofIndex dof = DofIndex::fromValue(1);
    const FreeEquationIndex equation = FreeEquationIndex::fromValue(0);
    const meshing::NodeId node = meshing::NodeId::fromValue(1);

    // Each kind is usable as itself, so the control compiles.
    takesDof(dof);
    takesEquation(equation);
    takesNode(node);
    // And a genuine integer is a genuine integer, which also keeps every helper
    // used in the control build -- an unused one is a -Werror failure there,
    // and the control must compile cleanly or no case is attributable.
    takesCount(1);

#if defined(BETTERCAD_CF_FREE_EQUATION_AS_DOF_INDEX)
    // The one a solver would actually write: taking the row it is assembling
    // and asking the numbering what degree of freedom it is, without going
    // through dofOf().
    takesDof(equation);
#elif defined(BETTERCAD_CF_DOF_INDEX_AS_FREE_EQUATION)
    // The reverse, and the more dangerous direction: indexing the reduced
    // system by a degree of freedom. For an unrestrained model the two agree
    // apart from the one-based offset, so a test on a free-floating body would
    // not catch it.
    takesEquation(dof);
#elif defined(BETTERCAD_CF_FREE_EQUATION_FROM_INTEGER)
    // A raw row number is not a FreeEquationIndex. fromValue() is the only way
    // in, so an index can be traced to the map that issued it.
    takesEquation(7);
#elif defined(BETTERCAD_CF_FREE_EQUATION_AS_INTEGER)
    // And it does not decay to one, so it cannot be mixed into arithmetic with
    // a count or a degree-of-freedom number.
    takesCount(equation);
#elif defined(BETTERCAD_CF_FREE_EQUATION_AS_NODE_ID)
    // An equation is not a node. Three equations belong to one node when
    // nothing is restrained, and fewer when something is.
    takesNode(equation);
#elif defined(BETTERCAD_CF_FREE_EQUATION_DEFAULT_CONSTRUCTED)
    // NO INVALID VALUE AND NO DEFAULT. An index that defaulted to row 0 would
    // be the sentinel defect this type exists to avoid: absence is
    // std::optional, which the compiler makes the caller handle.
    const FreeEquationIndex uninitialised;
    takesEquation(uninitialised);
#elif defined(BETTERCAD_CF_MESH_DOF_MAP_CONSTRUCTED_DIRECTLY)
    // buildMeshDofMap is the only way to obtain one, so possessing a map is
    // evidence that it is the canonical numbering of a mesh that exists.
    const MeshDofMap fabricated;
    takesCount(fabricated.dofCount());
#elif defined(BETTERCAD_CF_CONSTRAINT_SET_CONSTRUCTED_DIRECTLY)
    // Likewise: a set that had not been through buildConstraintSet could carry
    // a duplicate, an out-of-range index, or indices from another mesh.
    const ConstraintSet fabricated;
    takesCount(fabricated.size());
#elif defined(BETTERCAD_CF_FREE_EQUATION_MAP_CONSTRUCTED_DIRECTLY)
    // And a free-equation map that had not been built from a matching pair
    // could number rows against a mesh its constraints never described.
    const FreeEquationMap fabricated;
    takesCount(fabricated.dofCount());
#endif

    return 0;
}
