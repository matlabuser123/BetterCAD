// Build-failure tests for the CFD identity and data model (see CMakeLists.txt in
// this directory). Built once without any BETTERCAD_CF_* macro as a control,
// which must compile -- so every failure below is attributable to its own line
// and not to a missing header.
//
// What these prove is P19-DATA-001's central claim: two CFD identities that do
// not convert into one another, into a mesh handle, into an integer or into the
// structural and thermal identities; canonical records that cannot be given a
// mesh handle or a value of the wrong unit; a definition with no member for any
// derived field or for the fluid; and a result that cannot be written to or
// conjured without its factory.
#include <bettercad/cfd/CfdAnalysisObject.hpp>
#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdResult.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/meshing/MeshIds.hpp>

#include <cstdint>

using namespace bettercad;
using namespace bettercad::cfd;

namespace {

void takesCfdAnalysis(CfdAnalysisId /*unused*/) {}
void takesCondition(CfdBoundaryConditionId /*unused*/) {}
void takesThermalAnalysis(ThermalAnalysisId /*unused*/) {}
void takesObject(ObjectId /*unused*/) {}
void takesNode(meshing::NodeId /*unused*/) {}
void takesCount(std::uint64_t /*unused*/) {}

/// What every consumer receives. Never called: these cases are about what the
/// consumer CANNOT do, which the compiler decides without running it.
[[maybe_unused]] void reads(const CfdResult& result, [[maybe_unused]] const CfdAnalysisDefinition& definition,
                            [[maybe_unused]] CfdResultSlot& slot, [[maybe_unused]] const CfdSolveFailure& failure) {
    [[maybe_unused]] const CfdResultSource& source = result.source();
    [[maybe_unused]] const CfdResultMetadata& metadata = result.metadata();
#if defined(BETTERCAD_CF_A_RESULT_SOURCE_CANNOT_BE_REWRITTEN)
    // THE FAILURE MODE THE DESIGN IS BUILT AGAINST: refreshing a stale result by
    // rewriting what it claims to come from.
    result.source().analysisRevision = 0;
#elif defined(BETTERCAD_CF_A_RESULT_METADATA_IS_READ_ONLY)
    // Nor can a consumer improve what the solve reported about itself.
    result.metadata().residuals.continuity = 0.0;
#elif defined(BETTERCAD_CF_A_RESULT_CANNOT_BE_BUILT_WITHOUT_ITS_FACTORY)
    // The constructor is private: the only way to a CfdResult is `create`, which
    // checks the mesh stamp and the mode.
    [[maybe_unused]] const CfdResult conjured{CfdResultSource{}, CfdResultMetadata{}};
#elif defined(BETTERCAD_CF_A_FAILURE_IS_NOT_A_RESULT)
    // A failed attempt cannot be recorded as a success.
    slot.recordSuccess(failure);
#elif defined(BETTERCAD_CF_THE_DEFINITION_HAS_NO_PRESSURE_FIELD)
    // p, U, phi, cells and results can never become canonical document state:
    // the definition has no member to hold any of them.
    [[maybe_unused]] auto& field = definition.pressure;
#elif defined(BETTERCAD_CF_THE_DEFINITION_HAS_NO_VELOCITY_FIELD)
    [[maybe_unused]] auto& field = definition.velocity;
#elif defined(BETTERCAD_CF_THE_DEFINITION_HAS_NO_MASS_FLUX)
    [[maybe_unused]] auto& field = definition.massFlux;
#elif defined(BETTERCAD_CF_THE_DEFINITION_HAS_NO_CELLS)
    [[maybe_unused]] auto& field = definition.cells;
#elif defined(BETTERCAD_CF_THE_DEFINITION_HAS_NO_RESULT)
    [[maybe_unused]] auto& field = definition.result;
#elif defined(BETTERCAD_CF_THE_DEFINITION_HAS_NO_FLUID)
    // THE FLUID IS THE DOCUMENT'S P15 MATERIAL ASSIGNMENT (ADR-060, ADR-061). A
    // material id in the definition would be a second authority that could name
    // a different material than the one the document assigns.
    [[maybe_unused]] auto& field = definition.fluid;
#endif
}

} // namespace

int main() {
    const CfdAnalysisId analysis = CfdAnalysisId::fromValue(1);
    const CfdBoundaryConditionId condition = CfdBoundaryConditionId::fromValue(1);
    const ThermalAnalysisId thermalAnalysis = ThermalAnalysisId::fromValue(1);
    const ThermalBoundaryConditionId thermalCondition = ThermalBoundaryConditionId::fromValue(1);
    const AnalysisId structuralAnalysis = AnalysisId::fromValue(1);
    const RestraintId restraint = RestraintId::fromValue(1);
    const meshing::NodeId node = meshing::NodeId::fromValue(1);
    const meshing::ElementId element = meshing::ElementId::fromValue(1);

    // Each kind is usable as itself, so the control compiles.
    takesCfdAnalysis(analysis);
    takesCondition(condition);
    takesThermalAnalysis(thermalAnalysis);
    takesNode(node);
    takesCount(1);
    [[maybe_unused]] const auto values = thermalCondition.value() + structuralAnalysis.value() +
                                         restraint.value() + element.value();

    // A CFD ANALYSIS IS A DOCUMENT OBJECT, so its ID widens. The one widening
    // the model permits, and it must keep compiling.
    takesObject(analysis);

    // The canonical records, built the right way, so the control exercises every
    // constructor the cases below misuse.
    [[maybe_unused]] const VelocityInlet inlet{.face = {}, .normalSpeed = Velocity::fromSi(1.0)};
    [[maybe_unused]] const PressureOutlet outlet{.face = {}, .staticPressure = Pressure::fromSi(0.0)};
    [[maybe_unused]] const MovingWall lid{.face = {}, .velocity = Velocity3{.x = Velocity::fromSi(1.0)}};
    [[maybe_unused]] const CfdGravity gravity{.acceleration = Acceleration3{.z = Acceleration::fromSi(-9.81)}};

#if defined(BETTERCAD_CF_CFD_ANALYSIS_ID_AS_BOUNDARY_CONDITION_ID)
    // Two CFD identities, two domains.
    takesCondition(analysis);
#elif defined(BETTERCAD_CF_BOUNDARY_CONDITION_ID_AS_CFD_ANALYSIS_ID)
    takesCfdAnalysis(condition);
#elif defined(BETTERCAD_CF_BOUNDARY_CONDITION_ID_AS_OBJECT_ID)
    // A BOUNDARY CONDITION IS NOT A DOCUMENT OBJECT. It is a member of an
    // analysis's definition; if it widened it could be passed to findObject and
    // resolve to whatever object shares its number.
    takesObject(condition);
#elif defined(BETTERCAD_CF_NODE_ID_AS_CFD_ANALYSIS_ID)
    // A mesh handle is not a document identity.
    takesCfdAnalysis(node);
#elif defined(BETTERCAD_CF_NODE_ID_AS_BOUNDARY_CONDITION_ID)
    // ADR-032 and ADR-058: a boundary condition names CAD geometry, never a mesh
    // entity; one keyed on a node would be rebound by the next remesh.
    takesCondition(node);
#elif defined(BETTERCAD_CF_ELEMENT_ID_AS_BOUNDARY_CONDITION_ID)
    takesCondition(element);
#elif defined(BETTERCAD_CF_THERMAL_ANALYSIS_ID_AS_CFD_ANALYSIS_ID)
    // P18's identities are not P19's, in either direction.
    takesCfdAnalysis(thermalAnalysis);
#elif defined(BETTERCAD_CF_CFD_ANALYSIS_ID_AS_THERMAL_ANALYSIS_ID)
    takesThermalAnalysis(analysis);
#elif defined(BETTERCAD_CF_STRUCTURAL_ANALYSIS_ID_AS_CFD_ANALYSIS_ID)
    takesCfdAnalysis(structuralAnalysis);
#elif defined(BETTERCAD_CF_THERMAL_CONDITION_ID_AS_CFD_CONDITION_ID)
    // A held temperature and a velocity inlet share a face and nothing else.
    takesCondition(thermalCondition);
#elif defined(BETTERCAD_CF_RESTRAINT_ID_AS_CFD_CONDITION_ID)
    takesCondition(restraint);
#elif defined(BETTERCAD_CF_INTEGER_AS_CFD_CONDITION_ID)
    // No bare integer becomes a canonical identity by accident.
    takesCondition(1);
#elif defined(BETTERCAD_CF_A_VELOCITY_INLET_DOES_NOT_TARGET_A_NODE)
    // The canonical face condition has nowhere for a node to hide.
    [[maybe_unused]] const VelocityInlet wrong{node, Velocity::fromSi(1.0)};
#elif defined(BETTERCAD_CF_A_PRESSURE_IS_NOT_A_VELOCITY)
    // Pa where m/s is wanted: the units framework refuses.
    [[maybe_unused]] const VelocityInlet wrong{.face = {}, .normalSpeed = Pressure::fromSi(1.0)};
#elif defined(BETTERCAD_CF_A_VELOCITY_IS_NOT_A_PRESSURE)
    [[maybe_unused]] const PressureOutlet wrong{.face = {}, .staticPressure = Velocity::fromSi(1.0)};
#elif defined(BETTERCAD_CF_GRAVITY_IS_NOT_A_VELOCITY)
    // Two instances of one template, two types: m/s is not m/s^2.
    [[maybe_unused]] const CfdGravity wrong{.acceleration = Velocity3{}};
#elif defined(BETTERCAD_CF_A_WALL_VELOCITY_IS_NOT_AN_ACCELERATION)
    [[maybe_unused]] const MovingWall wrong{.face = {}, .velocity = Acceleration3{}};
#elif defined(BETTERCAD_CF_THE_MODE_IS_NOT_A_BOOL)
    // An enumeration, not a flag.
    CfdAnalysisDefinition definition;
    definition.mode = true;
#endif
    return 0;
}
