// Build-failure tests for the Tet4 thermal element kernel (see CMakeLists.txt
// in this directory). Built once without any BETTERCAD_CF_* macro as a
// control, which must compile -- so every failure below is attributable to
// its own line and not to a missing header.
//
// Four claims of P18-ELEM-001 are enforced here rather than reviewed:
//
//   1. The material inputs are DIMENSIONED. A conductivity is not a double,
//      not a density; the capacity pair is two distinct quantities that
//      cannot be swapped, and cannot be two doubles. A rho/cp swap gives the
//      same PRODUCT at runtime, so the type is the only place it can be
//      caught.
//
//   2. The steady and transient formulas are SEPARATE. The capacity call
//      takes no conductivity; the conduction result has no capacity total;
//      the capacity has no lumped variant.
//
//   3. The outputs are DIMENSIONED and distinct: W/K is not J/K, and a
//      gradient component is not a temperature.
//
//   4. The kernel knows NO GLOBAL IDENTITY. Corners are coordinates, not
//      NodeIds and not ThermalDofIndexes; temperatures are Temperatures, not
//      doubles; and the three value types cannot be conjured.
#include <bettercad/core/Units.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/thermal/ThermalDof.hpp>
#include <bettercad/thermal/ThermalElement.hpp>
#include <bettercad/thermal/ThermalMaterial.hpp>

#include <array>

using namespace bettercad;
using namespace bettercad::thermal;

namespace {

void takesConductance(ThermalConductance /*unused*/) {}
void takesHeatCapacity(HeatCapacity /*unused*/) {}
void takesTemperature(Temperature /*unused*/) {}
void takesGradient(InverseLength /*unused*/) {}

[[nodiscard]] Point3D at(double x, double y, double z) {
    return Point3D{Length::fromSi(x), Length::fromSi(y), Length::fromSi(z)};
}

} // namespace

int main() {
    const Tet4Corners corners{at(0, 0, 0), at(1, 0, 0), at(0, 1, 0), at(0, 0, 1)};
    const Result<Tet4Geometry> geometry = computeTet4Geometry(corners);
    if (!geometry.has_value()) {
        return 1;
    }
    const ThermalConductivity k = ThermalConductivity::fromSi(45.0);
    const ThermalCapacity pair{.density = Density::fromSi(7800.0),
                               .specificHeatCapacity = SpecificHeatCapacity::fromSi(500.0)};
    const Result<Tet4Conduction> conduction = computeTet4Conduction(*geometry, k);
    const Result<Tet4Capacity> capacity = computeTet4Capacity(*geometry, pair);
    if (!conduction.has_value() || !capacity.has_value()) {
        return 1;
    }
    const Tet4Temperatures temperatures{Temperature::fromSi(300.0), Temperature::fromSi(310.0),
                                        Temperature::fromSi(320.0), Temperature::fromSi(330.0)};

    // Each kind is usable as itself, so the control compiles and every
    // helper is used.
    takesConductance((*conduction)(0, 0));
    takesHeatCapacity((*capacity)(0, 0));
    takesTemperature(temperatures[0]);
    takesGradient(geometry->gradient(0)[0]);
    [[maybe_unused]] const TemperatureGradient3 gradient = geometry->gradientFrom(temperatures);
    [[maybe_unused]] const meshing::NodeId node = meshing::NodeId::fromValue(1);
    [[maybe_unused]] const ThermalDofIndex dof = ThermalDofIndex::fromValue(0);

#if defined(BETTERCAD_CF_A_CONDUCTIVITY_IS_NOT_A_DOUBLE)
    // A bare number has no unit. 45 of what?
    [[maybe_unused]] auto bad = computeTet4Conduction(*geometry, 45.0);
#endif

#if defined(BETTERCAD_CF_A_DENSITY_IS_NOT_A_CONDUCTIVITY)
    // rho offered where k is wanted: W/(m K) and kg/m^3 are different
    // dimensions, so the overload does not exist.
    [[maybe_unused]] auto bad = computeTet4Conduction(*geometry, pair.density);
#endif

#if defined(BETTERCAD_CF_CAPACITY_TAKES_NO_CONDUCTIVITY)
    // THE SEPARATION. Ce depends on rho, cp and geometry; there is no
    // signature through which k can enter it.
    [[maybe_unused]] auto bad = computeTet4Capacity(*geometry, k);
#endif

#if defined(BETTERCAD_CF_A_CAPACITY_PAIR_IS_NOT_TWO_DOUBLES)
    [[maybe_unused]] const ThermalCapacity bad{7800.0, 500.0};
#endif

#if defined(BETTERCAD_CF_RHO_AND_CP_ARE_NOT_INTERCHANGEABLE)
    // rho and cp SWAPPED. The product is the same at runtime, so Ce could
    // not tell; the types can, and do.
    [[maybe_unused]] const ThermalCapacity bad{.density = pair.specificHeatCapacity,
                                               .specificHeatCapacity = pair.density};
#endif

#if defined(BETTERCAD_CF_GEOMETRY_CONSTRUCTED_DIRECTLY)
    // Possession is the evidence the geometry is valid; there is no public
    // constructor.
    [[maybe_unused]] Tet4Geometry bad;
#endif

#if defined(BETTERCAD_CF_CONDUCTION_CONSTRUCTED_DIRECTLY)
    [[maybe_unused]] Tet4Conduction bad;
#endif

#if defined(BETTERCAD_CF_CAPACITY_CONSTRUCTED_DIRECTLY)
    [[maybe_unused]] Tet4Capacity bad;
#endif

#if defined(BETTERCAD_CF_A_CONDUCTANCE_IS_NOT_A_HEAT_CAPACITY)
    // W/K is not J/K: a Ke entry cannot be assembled into C.
    takesHeatCapacity((*conduction)(0, 0));
#endif

#if defined(BETTERCAD_CF_A_HEAT_CAPACITY_IS_NOT_A_CONDUCTANCE)
    takesConductance((*capacity)(0, 0));
#endif

#if defined(BETTERCAD_CF_A_GRADIENT_IS_NOT_A_TEMPERATURE)
    // 1/m is not K.
    takesTemperature(geometry->gradient(0)[0]);
#endif

#if defined(BETTERCAD_CF_CORNERS_ARE_NOT_NODE_IDS)
    // The kernel takes COORDINATES. A node handle is a mesh identity, and
    // the kernel has no mesh to resolve it against (cornersOf does, and
    // takes the mesh).
    [[maybe_unused]] auto bad = computeTet4Geometry(std::array<meshing::NodeId, 4>{node, node, node, node});
#endif

#if defined(BETTERCAD_CF_CORNERS_ARE_NOT_DOF_INDICES)
    // Nor global unknowns: a 4x4 local matrix does not depend on them.
    [[maybe_unused]] auto bad = computeTet4Geometry(std::array<ThermalDofIndex, 4>{dof, dof, dof, dof});
#endif

#if defined(BETTERCAD_CF_TEMPERATURES_ARE_NOT_DOUBLES)
    [[maybe_unused]] auto bad = geometry->gradientFrom(std::array<double, 4>{300.0, 310.0, 320.0, 330.0});
#endif

#if defined(BETTERCAD_CF_THE_GEOMETRY_HAS_NO_CONDUCTIVITY)
    // Geometry is geometry. The material enters at the matrix, not before.
    [[maybe_unused]] auto bad = geometry->conductivity();
#endif

#if defined(BETTERCAD_CF_THE_CONDUCTION_HAS_NO_CAPACITY_TOTAL)
    // Ke has no "total heat capacity"; that is Ce's and only Ce's.
    [[maybe_unused]] auto bad = conduction->total();
#endif

#if defined(BETTERCAD_CF_THE_CAPACITY_HAS_NO_LUMPED_VARIANT)
    // One policy, consistent, frozen. There is no diagonal variant to ask
    // for.
    [[maybe_unused]] auto bad = capacity->lumped();
#endif

    return 0;
}
