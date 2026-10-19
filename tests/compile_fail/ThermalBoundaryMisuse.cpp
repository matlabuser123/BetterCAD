// Build-failure tests for the thermal boundary conditions (see CMakeLists.txt in
// this directory). Built once without any BETTERCAD_CF_* macro as a control, which
// must compile -- so every failure below is attributable to its own line and not
// to a missing header.
//
// Five claims of P18-BC-001 are enforced here rather than reviewed:
//
//   1. The values are DIMENSIONED and distinct. A convection coefficient is
//      W/(m^2 K), a temperature is K, a conductivity is W/(m K), a conductance
//      entry is W/K and a source is W; none converts to another and none is a
//      bare double. A coefficient handed a conductivity is a boundary term wrong
//      by a length; a conductance handed where a power is wanted is a factor of
//      a temperature.
//
//   2. A canonical condition holds NOTHING MESH-LOCAL. The prescribed
//      temperature has a FaceName and a temperature; the convection has a
//      FaceName, a coefficient and an ambient. There is no node, facet or
//      unknown to find in either, and neither has a conductivity.
//
//   3. Identities do not convert. A node handle, a load identity and a
//      restraint identity are not a boundary-condition identity; a list of nodes
//      is not a list of facets; a node is not an unknown.
//
//   4. A PREPARED boundary and a convection term cannot be conjured and cannot be
//      edited.
//
//   5. The scope line. A prepared boundary has no penalty, no stiffness matrix, no
//      solution, no material and no radiation; the convection term has no
//      conductivity. Their absence from the types is the proof that this
//      milestone prepares exact constraints and exact boundary terms and does not
//      assemble or solve.
#include <bettercad/core/document/References.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/thermal/ThermalBoundary.hpp>
#include <bettercad/thermal/ThermalConvection.hpp>

#include <span>

using namespace bettercad;
using namespace bettercad::thermal;

int main() {
    const FaceName face{ObjectId::fromValue(3), FaceSelector{.role = FaceRole::EndCap}};
    const PrescribedTemperature held{.face = face, .value = Temperature::fromSi(350.0)};
    const ConvectionBoundary convection{.face = face,
                                        .coefficient = HeatTransferCoefficient::fromSi(25.0),
                                        .ambient = Temperature::fromSi(293.15)};
    const ThermalBoundaryCondition heldCondition{ThermalBoundaryConditionId::fromValue(1), held};
    const ThermalBoundaryCondition convectionCondition{ThermalBoundaryConditionId::fromValue(2), convection};

    // Dead code that must still COMPILE in the control, so every call below is
    // checked against the real signatures without needing a mesh at run time.
    const meshing::Mesh* mesh = nullptr;
    const ThermalDofMap* map = nullptr;
    const PreparedThermalBoundary* prepared = nullptr;
    const Tri3Convection* term = nullptr;
    std::span<const meshing::ElementId> facets{};
    if (mesh != nullptr && map != nullptr && prepared != nullptr && term != nullptr) {
        (void)integrateConvection(*mesh, *map, facets, convection.coefficient, convection.ambient);
        (void)uniqueFacetDofs(*mesh, *map, facets);
        (void)prepared->constraints();
        (void)prepared->convections();
        (void)prepared->convectionConductance();
        (void)(*term)(0, 0);
        (void)term->source(0);
    }
    (void)heldCondition.kind();
    (void)convectionCondition.kind();
    (void)computeTri3Convection(Area::fromSi(3.0), convection.coefficient, convection.ambient);

#if defined(BETTERCAD_CF_A_COEFFICIENT_IS_NOT_A_DOUBLE)
    // A bare number has no unit. 25 of what: W/(m^2 K), W/(mm^2 K), W/(m K)?
    [[maybe_unused]] const ConvectionBoundary bad{.face = face, .coefficient = 25.0, .ambient = Temperature::fromSi(293.15)};
#endif

#if defined(BETTERCAD_CF_A_COEFFICIENT_IS_NOT_A_CONDUCTIVITY)
    // W/(m K) where W/(m^2 K) is wanted: a length's worth of error.
    [[maybe_unused]] const ConvectionBoundary bad{
        .face = face, .coefficient = ThermalConductivity::fromSi(45.0), .ambient = Temperature::fromSi(293.15)};
#endif

#if defined(BETTERCAD_CF_A_TEMPERATURE_IS_NOT_A_DOUBLE)
    // 350 what: kelvin, or degrees Celsius?
    [[maybe_unused]] const PrescribedTemperature bad{.face = face, .value = 350.0};
#endif

#if defined(BETTERCAD_CF_A_TEMPERATURE_IS_NOT_A_COEFFICIENT)
    [[maybe_unused]] const ConvectionBoundary bad{
        .face = face, .coefficient = Temperature::fromSi(300.0), .ambient = Temperature::fromSi(293.15)};
#endif

#if defined(BETTERCAD_CF_A_COEFFICIENT_IS_NOT_AN_AMBIENT)
    // h and T_inf swapped: both are numbers in the same struct, and the compiler says no.
    [[maybe_unused]] const ConvectionBoundary bad{
        .face = face, .coefficient = HeatTransferCoefficient::fromSi(25.0), .ambient = HeatTransferCoefficient::fromSi(293.15)};
#endif

#if defined(BETTERCAD_CF_THE_CONVECTION_TAKES_NO_CONDUCTIVITY)
    // The boundary terms are functions of h, T_inf and the facet. Passing a
    // conductivity where the coefficient goes is a type error, not a silent k h.
    [[maybe_unused]] auto bad = computeTri3Convection(Area::fromSi(3.0), ThermalConductivity::fromSi(45.0),
                                                      Temperature::fromSi(293.15));
#endif

#if defined(BETTERCAD_CF_A_CONDUCTANCE_IS_NOT_A_POWER)
    // A matrix entry in W/K is not a source in W.
    if (term != nullptr) {
        [[maybe_unused]] Power bad = (*term)(0, 0);
    }
#endif

#if defined(BETTERCAD_CF_A_POWER_IS_NOT_A_CONDUCTANCE)
    if (term != nullptr) {
        [[maybe_unused]] ThermalConductance bad = term->source(0);
    }
#endif

#if defined(BETTERCAD_CF_A_NODE_ID_IS_NOT_A_CONDITION_ID)
    // A mesh node handle is not the identity of a boundary condition.
    [[maybe_unused]] const ThermalBoundaryCondition bad{meshing::NodeId::fromValue(1), held};
#endif

#if defined(BETTERCAD_CF_A_LOAD_ID_IS_NOT_A_CONDITION_ID)
    [[maybe_unused]] const ThermalBoundaryCondition bad{ThermalLoadId::fromValue(1), held};
#endif

#if defined(BETTERCAD_CF_A_RESTRAINT_ID_IS_NOT_A_CONDITION_ID)
    [[maybe_unused]] const ThermalBoundaryCondition bad{RestraintId::fromValue(1), held};
#endif

#if defined(BETTERCAD_CF_A_NODE_LIST_IS_NOT_A_FACET_LIST)
    // Facets are boundary triangles; nodes are their corners. Passing the wrong
    // kind of handle list would index the mesh by the wrong thing.
    if (mesh != nullptr && map != nullptr) {
        [[maybe_unused]] auto bad = integrateConvection(*mesh, *map, std::span<const meshing::NodeId>{},
                                                        convection.coefficient, convection.ambient);
    }
#endif

#if defined(BETTERCAD_CF_A_NODE_IS_NOT_AN_UNKNOWN)
    // A request names an UNKNOWN. The node a facet corner passed through is
    // resolved to one by the numbering and is never the unknown itself.
    [[maybe_unused]] const TemperatureRequest bad{
        .condition = ThermalBoundaryConditionId::fromValue(1), .dof = meshing::NodeId::fromValue(3),
        .value = Temperature::fromSi(300.0)};
#endif

#if defined(BETTERCAD_CF_A_DOUBLE_IS_NOT_A_REQUESTED_TEMPERATURE)
    [[maybe_unused]] const TemperatureRequest bad{
        .condition = ThermalBoundaryConditionId::fromValue(1), .dof = ThermalDofIndex::fromValue(0), .value = 300.0};
#endif

#if defined(BETTERCAD_CF_A_PREPARED_BOUNDARY_CONSTRUCTED_DIRECTLY)
    // Possession is the evidence that every condition resolved and no two
    // prescriptions contradicted; there is no public constructor.
    [[maybe_unused]] PreparedThermalBoundary bad;
#endif

#if defined(BETTERCAD_CF_A_CONVECTION_TERM_CONSTRUCTED_DIRECTLY)
    [[maybe_unused]] Tri3Convection bad;
#endif

#if defined(BETTERCAD_CF_THE_PRESCRIBED_TEMPERATURE_HAS_NO_NODE)
    [[maybe_unused]] auto bad = held.node;
#endif

#if defined(BETTERCAD_CF_THE_PRESCRIBED_TEMPERATURE_HAS_NO_FACET)
    [[maybe_unused]] auto bad = held.facet;
#endif

#if defined(BETTERCAD_CF_THE_PRESCRIBED_TEMPERATURE_HAS_NO_UNKNOWN)
    [[maybe_unused]] auto bad = held.dof;
#endif

#if defined(BETTERCAD_CF_THE_CONVECTION_HAS_NO_NODE)
    [[maybe_unused]] auto bad = convection.node;
#endif

#if defined(BETTERCAD_CF_THE_CONVECTION_HAS_NO_FACET)
    [[maybe_unused]] auto bad = convection.facet;
#endif

#if defined(BETTERCAD_CF_THE_CONVECTION_HAS_NO_UNKNOWN)
    [[maybe_unused]] auto bad = convection.dof;
#endif

#if defined(BETTERCAD_CF_THE_CONVECTION_HAS_NO_CONDUCTIVITY)
    // k is consumed by the weak-form substitution; it is not an input of the boundary terms.
    [[maybe_unused]] auto bad = convection.conductivity;
#endif

#if defined(BETTERCAD_CF_THE_CONVECTION_TERM_HAS_NO_CONDUCTIVITY)
    if (term != nullptr) {
        [[maybe_unused]] auto bad = term->conductivity();
    }
#endif

#if defined(BETTERCAD_CF_THE_PREPARED_BOUNDARY_HAS_NO_PENALTY)
    // Dirichlet is enforced exactly by the later solve, never by a large number.
    [[maybe_unused]] auto bad = prepared->penalty();
#endif

#if defined(BETTERCAD_CF_THE_PREPARED_BOUNDARY_HAS_NO_STIFFNESS_MATRIX)
    // Global assembly is P18-ASSEMBLY-001's.
    [[maybe_unused]] auto bad = prepared->stiffness();
#endif

#if defined(BETTERCAD_CF_THE_PREPARED_BOUNDARY_HAS_NO_SOLUTION)
    [[maybe_unused]] auto bad = prepared->solution();
#endif

#if defined(BETTERCAD_CF_THE_PREPARED_BOUNDARY_HAS_NO_MATERIAL)
    // The constraints and the convection terms depend on no k, rho or cp.
    [[maybe_unused]] auto bad = prepared->material();
#endif

#if defined(BETTERCAD_CF_THE_PREPARED_BOUNDARY_HAS_NO_RADIATION)
    [[maybe_unused]] auto bad = prepared->radiation();
#endif

#if defined(BETTERCAD_CF_THE_CONSTRAINT_TEMPERATURES_ARE_READ_ONLY)
    // A derived constraint is not edited in place; it is prepared again.
    if (prepared != nullptr) {
        prepared->constraints().constrained()[0].temperature = Temperature::fromSi(0.0);
    }
#endif

#if defined(BETTERCAD_CF_THE_CONVECTION_COEFFICIENT_IS_READ_ONLY)
    if (prepared != nullptr) {
        prepared->convections()[0].coefficient = HeatTransferCoefficient::fromSi(0.0);
    }
#endif

    return 0;
}
