// Build-failure tests for thermal commands (see CMakeLists.txt in this directory). Built once without any
// BETTERCAD_CF_* macro as a control, which must compile -- so every failure below is attributable to its own line.
//
// THE ABSENCES ARE THE MILESTONE'S CENTRAL CLAIM. P18-CMD-001's rule is that the undo history holds canonical thermal
// intent and never a generated solver or result array. A runtime test can show that no stiffness matrix WAS stored;
// these show that none CAN be, which is the stronger statement and the one that survives the next person editing the
// commands.
//
//   - A command is keyed on a `ThermalAnalysisId` and addresses a record by `ThermalLoadId` or
//     `ThermalBoundaryConditionId`. A mesh-local `NodeId` or `ElementId` cannot stand in for any of them, and neither can
//     one thermal id for another.
//   - A load or a condition names a CAD FACE. A mesh node cannot be its target.
//   - A quantity is a typed quantity: a flux, a temperature, a coefficient and a time step are not bare doubles, so no
//     Celsius or millimetre value can be smuggled in as a number.
//   - The analysis definition the commands copy has no temperature array, no matrix, no prepared load, no DOF map, no
//     solution, no heat flux and no report.
//   - THE COMMAND HEADER CANNOT EVEN NAME A DERIVED TYPE. Measured by preprocessing a translation unit that includes only
//     ThermalCommands.hpp: the DOF index, the free-equation index, the DOF map, the assembled system, both solutions, the
//     post result, the balance, the validation report, the prepared loads, the prepared boundary, the result container and
//     the model are NOT DECLARED where a command is declared. (The mesh-local `NodeId` and `ElementId` ARE reachable, as
//     they always were through the analysis object's meshing handles, which is why they are probed as wrong TYPES.)
//   - A command exposes its payload read-only, has no solve, no mesh and no result, and is not copyable.
//
// WHY THE PROBES GO THROUGH CONSTRUCTOR PARAMETERS. P16-CMD-001 recorded the trap: `SomeAggregate{.member = wrongId}` can
// COMPILE, because brace elision initializes the aggregate's FIRST member. What can be guaranteed is the path the commands
// expose, where a parameter of a named type is copy-initialized and no elision applies.
#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/thermal/ThermalCommands.hpp>

#include <memory>

using namespace bettercad;
using namespace bettercad::literals;

int main() {
    Document document{"Part"};
    CommandHistory history;

    // The control: the intended way to say all of this.
    const ThermalAnalysisId analysis = ThermalAnalysisId::fromValue(1U);
    const FaceName face{ObjectId::fromValue(1U), FaceSelector{.role = FaceRole::EndCap}};

    thermal::ThermalAnalysisDefinition definition;
    definition.mesh = MeshControlId::fromValue(1U);

    const thermal::ThermalLoad flux{ThermalLoadId::fromValue(1U),
                                    thermal::SurfaceHeatFluxLoad{.face = face, .flux = HeatFlux::fromSi(1000.0)}};
    const thermal::ThermalBoundaryCondition held{
        ThermalBoundaryConditionId::fromValue(1U),
        thermal::PrescribedTemperature{.face = face, .value = Temperature::fromSi(350.0)}};
    const thermal::UniformInitialTemperature initial{Temperature::fromSi(300.0)};
    const thermal::TransientSettings transient{.timeStep = Time::fromSi(0.3), .endTime = Time::fromSi(1.0)};

    auto create = std::make_unique<thermal::CreateThermalAnalysisCommand>("Study", definition);
    auto addLoad = std::make_unique<thermal::AddThermalLoadCommand>(analysis, flux);
    auto editLoad = std::make_unique<thermal::EditThermalLoadCommand>(analysis, flux);
    auto removeLoad = std::make_unique<thermal::RemoveThermalLoadCommand>(analysis, ThermalLoadId::fromValue(1U));
    auto addCondition = std::make_unique<thermal::AddThermalBoundaryConditionCommand>(analysis, held);
    auto editCondition = std::make_unique<thermal::EditThermalBoundaryConditionCommand>(analysis, held);
    auto removeCondition = std::make_unique<thermal::RemoveThermalBoundaryConditionCommand>(
        analysis, ThermalBoundaryConditionId::fromValue(1U));
    auto setInitial = std::make_unique<thermal::SetInitialTemperatureCommand>(analysis, initial);
    auto setSolver = std::make_unique<thermal::SetThermalSolverSettingsCommand>(analysis, thermal::ThermalSolverSettings{});
    auto setTransient = std::make_unique<thermal::SetTransientSettingsCommand>(analysis, transient);
    const thermal::ThermalAnalysisEditCommand* payload = addLoad.get();
    if (!history.execute(document, std::move(addLoad)).has_value()) {
        // Expected: there is no such analysis in this bare document. The point of the control build is that it COMPILES.
    }
    (void)create;
    (void)editLoad;
    (void)removeLoad;
    (void)addCondition;
    (void)editCondition;
    (void)removeCondition;
    (void)setInitial;
    (void)setSolver;
    (void)setTransient;
    (void)payload;

    // ---- a command is keyed on typed identities -------------------------------------------------------------------
#ifdef BETTERCAD_CF_A_LOAD_COMMAND_IS_NOT_KEYED_ON_A_NODE
    auto probe = std::make_unique<thermal::AddThermalLoadCommand>(meshing::NodeId::fromValue(1U), flux);
    (void)probe;
#endif
#ifdef BETTERCAD_CF_A_LOAD_COMMAND_IS_NOT_KEYED_ON_AN_ELEMENT
    auto probe = std::make_unique<thermal::AddThermalLoadCommand>(meshing::ElementId::fromValue(1U), flux);
    (void)probe;
#endif
#ifdef BETTERCAD_CF_A_LOAD_COMMAND_IS_NOT_KEYED_ON_A_LOAD_ID
    // A load id is not an analysis.
    auto probe = std::make_unique<thermal::AddThermalLoadCommand>(ThermalLoadId::fromValue(1U), flux);
    (void)probe;
#endif
#ifdef BETTERCAD_CF_A_LOAD_COMMAND_IS_NOT_KEYED_ON_A_CONDITION_ID
    auto probe = std::make_unique<thermal::AddThermalLoadCommand>(ThermalBoundaryConditionId::fromValue(1U), flux);
    (void)probe;
#endif
#ifdef BETTERCAD_CF_A_CONDITION_COMMAND_IS_NOT_KEYED_ON_A_NODE
    auto probe = std::make_unique<thermal::AddThermalBoundaryConditionCommand>(meshing::NodeId::fromValue(1U), held);
    (void)probe;
#endif
#ifdef BETTERCAD_CF_A_CONDITION_COMMAND_IS_NOT_KEYED_ON_A_LOAD_ID
    auto probe = std::make_unique<thermal::AddThermalBoundaryConditionCommand>(ThermalLoadId::fromValue(1U), held);
    (void)probe;
#endif
#ifdef BETTERCAD_CF_AN_EDIT_COMMAND_IS_NOT_KEYED_ON_AN_ELEMENT
    auto probe = std::make_unique<thermal::EditThermalLoadCommand>(meshing::ElementId::fromValue(1U), flux);
    (void)probe;
#endif
#ifdef BETTERCAD_CF_REMOVE_LOAD_NEEDS_A_LOAD_ID_NOT_A_NODE
    auto probe = std::make_unique<thermal::RemoveThermalLoadCommand>(analysis, meshing::NodeId::fromValue(1U));
    (void)probe;
#endif
#ifdef BETTERCAD_CF_REMOVE_LOAD_NEEDS_A_LOAD_ID_NOT_A_CONDITION_ID
    auto probe = std::make_unique<thermal::RemoveThermalLoadCommand>(analysis, ThermalBoundaryConditionId::fromValue(1U));
    (void)probe;
#endif
#ifdef BETTERCAD_CF_REMOVE_CONDITION_NEEDS_A_CONDITION_ID_NOT_A_LOAD_ID
    auto probe = std::make_unique<thermal::RemoveThermalBoundaryConditionCommand>(analysis, ThermalLoadId::fromValue(1U));
    (void)probe;
#endif
#ifdef BETTERCAD_CF_REMOVE_CONDITION_NEEDS_A_CONDITION_ID_NOT_AN_ELEMENT
    auto probe = std::make_unique<thermal::RemoveThermalBoundaryConditionCommand>(analysis, meshing::ElementId::fromValue(1U));
    (void)probe;
#endif
#ifdef BETTERCAD_CF_A_SETTINGS_COMMAND_IS_NOT_KEYED_ON_A_NODE
    auto probe = std::make_unique<thermal::SetThermalSolverSettingsCommand>(meshing::NodeId::fromValue(1U),
                                                                           thermal::ThermalSolverSettings{});
    (void)probe;
#endif
#ifdef BETTERCAD_CF_AN_INITIAL_TEMPERATURE_COMMAND_IS_NOT_KEYED_ON_AN_ELEMENT
    auto probe = std::make_unique<thermal::SetInitialTemperatureCommand>(meshing::ElementId::fromValue(1U), initial);
    (void)probe;
#endif
#ifdef BETTERCAD_CF_A_TRANSIENT_COMMAND_IS_NOT_KEYED_ON_A_MESH_CONTROL
    // A mesh-control id names the mesh an analysis reads; it is not the analysis.
    auto probe = std::make_unique<thermal::SetTransientSettingsCommand>(MeshControlId::fromValue(1U), transient);
    (void)probe;
#endif

    // ---- a quantity is a typed quantity ----------------------------------------------------------------------------
#ifdef BETTERCAD_CF_AN_INITIAL_TEMPERATURE_IS_NOT_A_DOUBLE
    // 300.0 could be kelvin or degrees Celsius; the type makes the caller say which by constructing a Temperature.
    auto probe = std::make_unique<thermal::SetInitialTemperatureCommand>(analysis, thermal::UniformInitialTemperature{300.0});
    (void)probe;
#endif
#ifdef BETTERCAD_CF_A_TIME_STEP_IS_NOT_A_DOUBLE
    const thermal::TransientSettings bare{.timeStep = 0.3, .endTime = Time::fromSi(1.0)};
    (void)bare;
#endif
#ifdef BETTERCAD_CF_A_HEAT_FLUX_IS_NOT_A_DOUBLE
    const thermal::SurfaceHeatFluxLoad bare{.face = face, .flux = 1250.0};
    (void)bare;
#endif
#ifdef BETTERCAD_CF_A_CONVECTION_COEFFICIENT_IS_NOT_A_DOUBLE
    const thermal::ConvectionBoundary bare{.face = face, .coefficient = 25.0, .ambient = Temperature::fromSi(300.0)};
    (void)bare;
#endif
#ifdef BETTERCAD_CF_A_FLUX_IS_NOT_A_TEMPERATURE
    const thermal::SurfaceHeatFluxLoad wrong{.face = face, .flux = Temperature::fromSi(300.0)};
    (void)wrong;
#endif

    // ---- a record names a face, never a node -------------------------------------------------------------------------
#ifdef BETTERCAD_CF_A_LOAD_DOES_NOT_TARGET_A_NODE
    // Probed through the CONSTRUCTOR, where no brace elision can silently accept a NodeId as the first member.
    const thermal::ThermalLoad nodal{ThermalLoadId::fromValue(2U), meshing::NodeId::fromValue(3U)};
    (void)nodal;
#endif
#ifdef BETTERCAD_CF_A_CONDITION_DOES_NOT_TARGET_A_NODE
    const thermal::ThermalBoundaryCondition nodal{ThermalBoundaryConditionId::fromValue(2U), meshing::NodeId::fromValue(3U)};
    (void)nodal;
#endif
#ifdef BETTERCAD_CF_A_LOAD_ID_IS_NOT_A_NODE_ID
    const thermal::ThermalLoad wrong{meshing::NodeId::fromValue(2U), thermal::VolumetricHeatSourceLoad{}};
    (void)wrong;
#endif

    // ---- the command header cannot name a derived type ----------------------------------------------------------------
#ifdef BETTERCAD_CF_THE_DOF_INDEX_IS_NOT_REACHABLE
    thermal::ThermalDofIndex index{};
    (void)index;
#endif
#ifdef BETTERCAD_CF_THE_FREE_EQUATION_INDEX_IS_NOT_REACHABLE
    thermal::ThermalFreeEquationIndex index{};
    (void)index;
#endif
#ifdef BETTERCAD_CF_THE_DOF_MAP_IS_NOT_REACHABLE
    const thermal::ThermalDofMap* map = nullptr;
    (void)map;
#endif
#ifdef BETTERCAD_CF_THE_ASSEMBLED_SYSTEM_IS_NOT_REACHABLE
    const thermal::ThermalSystem* system = nullptr;
    (void)system;
#endif
#ifdef BETTERCAD_CF_THE_STEADY_SOLUTION_IS_NOT_REACHABLE
    const thermal::SteadyThermalSolution* solution = nullptr;
    (void)solution;
#endif
#ifdef BETTERCAD_CF_THE_TRANSIENT_SOLUTION_IS_NOT_REACHABLE
    const thermal::TransientThermalSolution* solution = nullptr;
    (void)solution;
#endif
#ifdef BETTERCAD_CF_THE_POST_RESULT_IS_NOT_REACHABLE
    const thermal::ThermalPostResult* post = nullptr;
    (void)post;
#endif
#ifdef BETTERCAD_CF_THE_BALANCE_IS_NOT_REACHABLE
    const thermal::ThermalBalanceResult* balance = nullptr;
    (void)balance;
#endif
#ifdef BETTERCAD_CF_THE_VALIDATION_REPORT_IS_NOT_REACHABLE
    const thermal::ThermalValidationReport* report = nullptr;
    (void)report;
#endif
#ifdef BETTERCAD_CF_THE_PREPARED_LOADS_ARE_NOT_REACHABLE
    const thermal::PreparedThermalLoads* prepared = nullptr;
    (void)prepared;
#endif
#ifdef BETTERCAD_CF_THE_PREPARED_BOUNDARY_IS_NOT_REACHABLE
    const thermal::PreparedThermalBoundary* prepared = nullptr;
    (void)prepared;
#endif
#ifdef BETTERCAD_CF_THE_RESULT_CONTAINER_IS_NOT_REACHABLE
    const thermal::ThermalResult* result = nullptr;
    (void)result;
#endif
#ifdef BETTERCAD_CF_THE_MODEL_IS_NOT_REACHABLE
    const thermal::ThermalModel* model = nullptr;
    (void)model;
#endif

    // ---- the definition and the commands hold no derived member ---------------------------------------------------------
#ifdef BETTERCAD_CF_THE_DEFINITION_HAS_NO_TEMPERATURE_ARRAY
    (void)definition.temperatures;
#endif
#ifdef BETTERCAD_CF_THE_DEFINITION_HAS_NO_MATRIX
    (void)definition.stiffness;
#endif
#ifdef BETTERCAD_CF_THE_DEFINITION_HAS_NO_PREPARED_LOADS
    (void)definition.preparedLoads;
#endif
#ifdef BETTERCAD_CF_THE_DEFINITION_HAS_NO_DOF_MAP
    (void)definition.dofMap;
#endif
#ifdef BETTERCAD_CF_THE_DEFINITION_HAS_NO_SOLUTION
    (void)definition.solution;
#endif
#ifdef BETTERCAD_CF_THE_DEFINITION_HAS_NO_HEAT_FLUX
    (void)definition.heatFlux;
#endif
#ifdef BETTERCAD_CF_THE_DEFINITION_HAS_NO_REPORT
    (void)definition.report;
#endif
#ifdef BETTERCAD_CF_THE_DEFINITION_HAS_NO_TIME_GRID
    (void)definition.timeGrid;
#endif
#ifdef BETTERCAD_CF_A_COMMAND_HAS_NO_RESULT_ACCESSOR
    (void)payload->result();
#endif
#ifdef BETTERCAD_CF_A_COMMAND_HAS_NO_MESH_ACCESSOR
    (void)payload->mesh();
#endif
#ifdef BETTERCAD_CF_A_COMMAND_HAS_NO_SOLVE
    (void)payload->solve(document);
#endif
#ifdef BETTERCAD_CF_A_COMMAND_HAS_NO_PAYLOAD_SETTER
    (void)payload->setBefore(definition);
#endif

    // ---- read-only payload, no copies, no instantiating the base --------------------------------------------------------
#ifdef BETTERCAD_CF_THE_BEFORE_PAYLOAD_IS_READ_ONLY
    payload->before()->loads.clear();
#endif
#ifdef BETTERCAD_CF_THE_AFTER_PAYLOAD_IS_READ_ONLY
    payload->after()->boundaryConditions.clear();
#endif
#ifdef BETTERCAD_CF_AN_EDIT_COMMAND_IS_NOT_COPYABLE
    thermal::AddThermalLoadCommand copy = *std::make_unique<thermal::AddThermalLoadCommand>(analysis, flux);
    (void)copy;
#endif
#ifdef BETTERCAD_CF_A_CREATE_COMMAND_IS_NOT_COPYABLE
    thermal::CreateThermalAnalysisCommand copy = *create;
    (void)copy;
#endif
#ifdef BETTERCAD_CF_THE_EDIT_BASE_CANNOT_BE_INSTANTIATED
    thermal::ThermalAnalysisEditCommand base{analysis};
    (void)base;
#endif

    return 0;
}
