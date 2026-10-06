// Build-failure tests for the structural identity model (see CMakeLists.txt in
// this directory). Built once without any BETTERCAD_CF_* macro as a control,
// which must compile -- so every failure below is attributable to its own line
// and not to a missing header.
//
// What these prove is P17-DATA-001's central claim: three identity domains that
// do not convert into one another, enforced by the compiler rather than by a
// reviewer. The cases are chosen for the mistakes that would actually be made:
// a load stored as a node, a restraint stored as an equation, an analysis used
// as a load. Each one, written by hand, is a wrong program that compiles only
// if the model is broken.
#include <bettercad/core/Id.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/structural/StructuralData.hpp>

#include <cstdint>

using namespace bettercad;
using namespace bettercad::structural;

namespace {

void takesAnalysis(AnalysisId /*unused*/) {}
void takesLoad(LoadId /*unused*/) {}
void takesRestraint(RestraintId /*unused*/) {}
void takesObject(ObjectId /*unused*/) {}
void takesDof(DofIndex /*unused*/) {}
void takesNode(meshing::NodeId /*unused*/) {}
void takesCount(std::uint64_t /*unused*/) {}

} // namespace

int main() {
    const AnalysisId analysis = AnalysisId::fromValue(1);
    const LoadId load = LoadId::fromValue(1);
    const RestraintId restraint = RestraintId::fromValue(1);
    const DofIndex dof = DofIndex::fromValue(1);
    const meshing::NodeId node = meshing::NodeId::fromValue(1);

    // Each kind is usable as itself, so the control compiles.
    takesAnalysis(analysis);
    takesLoad(load);
    takesRestraint(restraint);
    takesDof(dof);
    takesNode(node);
    // And a genuine integer is a genuine integer, which also keeps every helper
    // used in the control build -- an unused one is a -Werror failure there,
    // and the control must compile cleanly or no case is attributable.
    takesCount(1);

    // AN ANALYSIS IS A DOCUMENT OBJECT, so its ID widens. This is the one
    // widening the model permits, and it must keep compiling: without it the
    // dependency graph and the command history could not take an analysis.
    takesObject(analysis);

#if defined(BETTERCAD_CF_ANALYSIS_ID_AS_LOAD_ID)
    // Three document identities, three domains. An analysis is not a load.
    takesLoad(analysis);
#elif defined(BETTERCAD_CF_LOAD_ID_AS_RESTRAINT_ID)
    // The one a copy-paste would produce: a load and a restraint are both
    // members of an analysis, both 64-bit, and they mean opposite things.
    takesRestraint(load);
#elif defined(BETTERCAD_CF_RESTRAINT_ID_AS_LOAD_ID)
    takesLoad(restraint);
#elif defined(BETTERCAD_CF_LOAD_ID_AS_OBJECT_ID)
    // A LOAD IS NOT A DOCUMENT OBJECT. It is a member of an analysis's
    // definition, as a boundary set is a member of a control's, so it must not
    // widen. If it did, a load could be passed to findObject, to the dependency
    // graph, or to a command that expects an object -- and would silently
    // resolve to whatever object shares its number.
    takesObject(load);
#elif defined(BETTERCAD_CF_RESTRAINT_ID_AS_OBJECT_ID)
    takesObject(restraint);
#elif defined(BETTERCAD_CF_DOF_INDEX_AS_OBJECT_ID)
    // THE CENTRAL ONE. A DOF index names one equation of one assembled system
    // under one numbering of one mesh. Persisting it as document identity is
    // the defect P17-DATA-001 exists to prevent: a restraint that stored
    // "DOF 1042" would constrain unrelated material after a remesh.
    takesObject(dof);
#elif defined(BETTERCAD_CF_DOF_INDEX_AS_NODE_ID)
    // Nor is a DOF a node: three DOFs share one node, so the two cannot be the
    // same handle even though both are mesh-scoped.
    takesNode(dof);
#elif defined(BETTERCAD_CF_NODE_ID_AS_DOF_INDEX)
    takesDof(node);
#elif defined(BETTERCAD_CF_DOF_INDEX_AS_INTEGER)
    // Not an integer either, so it cannot be passed where a count, a size or a
    // row number is wanted -- which is how a solver-local handle escapes into
    // code that has no idea what it is.
    takesCount(dof);
#elif defined(BETTERCAD_CF_INTEGER_AS_LOAD_ID)
    // And no bare integer becomes a canonical identity by accident.
    takesLoad(1);
#elif defined(BETTERCAD_CF_NODE_ID_AS_LOAD_ID)
    // THE RULE ADR-032 STATES, enforced one layer up: a load's identity is
    // canonical intent naming CAD geometry, never a mesh entity. A load keyed
    // on a node would be rebound to unrelated material by the next remesh.
    takesLoad(node);
#elif defined(BETTERCAD_CF_NODE_ID_AS_RESTRAINT_ID)
    takesRestraint(node);
#endif
    return 0;
}
