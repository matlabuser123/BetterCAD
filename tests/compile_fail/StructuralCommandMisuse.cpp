// Build-failure tests for structural commands (see CMakeLists.txt in this
// directory). Built once without any BETTERCAD_CF_* macro as a control, which
// must compile.
//
// THE ABSENCES ARE THE MILESTONE'S CENTRAL CLAIM. P17-CMD-001's rule is that
// the undo history holds canonical engineering intent and never a generated
// solver array. A runtime test can show that no stiffness matrix WAS stored;
// these show that none CAN be, which is the stronger statement and the one
// that survives the next person editing the commands.
//
//   - A command is keyed on an `AnalysisId`. A mesh-local `NodeId` or
//     `ElementId` cannot stand in for one, and neither can a `DofIndex`.
//   - A restraint is a `RestraintId`, a `FaceName` and a component mask. A
//     `NodeId` cannot target one, and a component mask is not a `DofIndex`
//     list.
//   - The analysis definition the commands copy has no displacement array, no
//     stress array, no reaction array, no `ConstraintSet` and no prepared load
//     field. Its `loads` and `restraints` are canonical records.
//   - A command cannot be constructed from a result, a solved system or a
//     prepared load set.
//   - Commands are not copyable, so history cannot come to hold two owners of
//     one command.
//
// WHY THE PROBES GO THROUGH CONSTRUCTOR PARAMETERS. P16-CMD-001 recorded the
// trap and it applies here unchanged: the obvious probe --
// `SomeAggregate{.member = wrongId}` -- COMPILES, because brace elision
// initializes the aggregate's FIRST member and the wrong id silently becomes
// something else. That is a property of C++ aggregate initialization, not
// something a struct can refuse without gaining a constructor. What CAN be
// guaranteed is the path the commands expose, where a parameter of a named
// type is copy-initialized and no elision applies.
//
// ONE DECLARED EXCEPTION IS NOT PROBED HERE, AND SAYING SO IS THE POINT. A
// `NodalForceLoad` names a `NodeId`: P17-LOAD-001 made that choice explicitly,
// the payload carries the `MeshStamp` the handle belongs to and is refused
// against any other mesh, and its purpose is fixtures. So a command that adds
// one legitimately carries that NodeId as part of a canonical record. What is
// forbidden, and probed below, is a NodeId or an ElementId as a command's
// TARGET, and every form of derived solver state.
#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/structural/StructuralCommands.hpp>

using namespace bettercad;
using namespace bettercad::literals;

int main() {
    Document document{"Part"};
    CommandHistory history;

    // The control: the intended way to say all of this.
    const AnalysisId analysis = AnalysisId::fromValue(1U);
    const FaceName face{ObjectId::fromValue(1U), FaceSelector{.role = FaceRole::EndCap}};

    structural::StructuralAnalysisDefinition definition;
    definition.mesh = MeshControlId::fromValue(1U);
    definition.solver.relativeResidualTolerance = 1.0e-10;

    const structural::StructuralLoad pressure{
        LoadId::fromValue(1U),
        structural::PressureLoad{.face = face, .magnitude = Pressure::fromSi(1000.0)}};
    const structural::StructuralRestraint fixed =
        structural::StructuralRestraint::fixedSupport(RestraintId::fromValue(1U), face);

    auto addLoad = std::make_unique<structural::AddStructuralLoadCommand>(analysis, pressure);
    auto addRestraint =
        std::make_unique<structural::AddStructuralRestraintCommand>(analysis, fixed);
    auto settings = std::make_unique<structural::SetSolverSettingsCommand>(
        analysis, structural::SolverSettings{});
    if (!history.execute(document, std::move(addLoad)).has_value()) {
        // Expected: there is no such analysis in this bare document. The point
        // of the control build is that it COMPILES.
    }
    (void)addRestraint;
    (void)settings;

#ifdef BETTERCAD_CF_A_LOAD_COMMAND_IS_NOT_KEYED_ON_A_NODE
    // A mesh node cannot name the analysis a command edits.
    auto nodeKeyed =
        std::make_unique<structural::AddStructuralLoadCommand>(meshing::NodeId::fromValue(1U),
                                                              pressure);
    (void)nodeKeyed;
#endif

#ifdef BETTERCAD_CF_A_LOAD_COMMAND_IS_NOT_KEYED_ON_AN_ELEMENT
    auto elementKeyed = std::make_unique<structural::AddStructuralLoadCommand>(
        meshing::ElementId::fromValue(1U), pressure);
    (void)elementKeyed;
#endif

#ifdef BETTERCAD_CF_A_RESTRAINT_COMMAND_IS_NOT_KEYED_ON_A_NODE
    auto nodeRestraint = std::make_unique<structural::AddStructuralRestraintCommand>(
        meshing::NodeId::fromValue(1U), fixed);
    (void)nodeRestraint;
#endif

#ifdef BETTERCAD_CF_A_COMMAND_IS_NOT_KEYED_ON_A_DOF_INDEX
    // A DofIndex is solver-local and is not an analysis.
    auto dofKeyed = std::make_unique<structural::SetSolverSettingsCommand>(
        structural::DofIndex::fromValue(0U), structural::SolverSettings{});
    (void)dofKeyed;
#endif

#ifdef BETTERCAD_CF_A_RESTRAINT_DOES_NOT_TARGET_A_NODE
    // A restraint is a RestraintId, a FaceName and a mask. Probed through the
    // CONSTRUCTOR, where `face` is copy-initialized and brace elision cannot
    // silently accept a NodeId as the first member.
    const structural::StructuralRestraint nodal{RestraintId::fromValue(2U),
                                                meshing::NodeId::fromValue(3U),
                                                structural::RestraintComponents::fixed()};
    (void)nodal;
#endif

#ifdef BETTERCAD_CF_A_RESTRAINT_MASK_IS_NOT_A_DOF_INDEX_LIST
    // The components held at zero are a mask over DofComponent, not a list of
    // solver equation numbers.
    const structural::StructuralRestraint byIndex{
        RestraintId::fromValue(2U), face, std::vector<structural::DofIndex>{}};
    (void)byIndex;
#endif

#ifdef BETTERCAD_CF_THE_DEFINITION_HAS_NO_DISPLACEMENTS
    // Canonical intent has no displacement array.
    definition.displacements.clear();
#endif

#ifdef BETTERCAD_CF_THE_DEFINITION_HAS_NO_STRESSES
    definition.stresses.clear();
#endif

#ifdef BETTERCAD_CF_THE_DEFINITION_HAS_NO_REACTIONS
    definition.reactions.clear();
#endif

#ifdef BETTERCAD_CF_THE_DEFINITION_HAS_NO_CONSTRAINT_SET
    // The constrained DOF set is rebuilt for whatever mesh is current.
    (void)definition.constraints;
#endif

#ifdef BETTERCAD_CF_THE_DEFINITION_HAS_NO_PREPARED_LOADS
    // The equivalent nodal force field is derived, and is not stored here.
    (void)definition.nodalForces;
#endif

#ifdef BETTERCAD_CF_THE_DEFINITION_HAS_NO_STIFFNESS_MATRIX
    (void)definition.stiffness;
#endif

#ifdef BETTERCAD_CF_A_COMMAND_IS_NOT_CONSTRUCTIBLE_FROM_A_RESULT
    // There is no command that takes a generated result, which is what an
    // "undo by restoring the old numbers" design would need.
    //
    // AND THE DIAGNOSTIC IS STRONGER THAN A REFUSED CONVERSION. gcc says
    // `StructuralResult` "is not a member of bettercad::structural", because
    // StructuralResult.hpp is not reachable from the command header at all --
    // so the type a command would have to hold is not even visible here.
    // Measured rather than predicted: the first expected regex asked for a
    // conversion error and this case failed until the claim was corrected.
    auto fromResult = std::make_unique<structural::SetStructuralAnalysisDefinitionCommand>(
        analysis, structural::StructuralResult{});
    (void)fromResult;
#endif

#ifdef BETTERCAD_CF_A_COMMAND_HAS_NO_RESULT_SETTER
    // Nor a way to hand one to a command after the fact.
    structural::SetSolverSettingsCommand edit{analysis, structural::SolverSettings{}};
    edit.setResult(nullptr);
#endif

#ifdef BETTERCAD_CF_A_COMMAND_IS_NOT_COPYABLE
    structural::SetSolverSettingsCommand original{analysis, structural::SolverSettings{}};
    structural::SetSolverSettingsCommand copy{original};
    (void)copy;
#endif

#ifdef BETTERCAD_CF_THE_DEFINITION_DOES_NOT_TAKE_A_MESH
    // A generated mesh is not canonical intent and cannot be assigned into the
    // definition, whose `mesh` member is a CONTROL -- the request, not the
    // result.
    definition.mesh = meshing::MeshStamp{};
#endif

    return 0;
}
