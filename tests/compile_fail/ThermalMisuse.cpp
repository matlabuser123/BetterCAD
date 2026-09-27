// Build-failure tests for thermal material properties (see CMakeLists.txt in
// this directory). Built once without any BETTERCAD_CF_* macro as a control,
// which must compile: without it, a property type that had quietly stopped being
// type-safe would still pass every negative case below.
#include <bettercad/core/materials/MaterialProperty.hpp>
#include <bettercad/core/materials/ThermalProperties.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/core/units/Units.hpp>

using namespace bettercad;
using namespace bettercad::literals;
using materials::ElectricalResistivity;
using materials::MaterialProperty;
using materials::ThermalProperties;

int main() {
    ThermalProperties properties;

    // The control: each of these is the intended way to say it.
    properties.thermalConductivity = MaterialProperty<ThermalConductivity>::known(50_W_per_m_K);
    properties.specificHeatCapacity =
        MaterialProperty<SpecificHeatCapacity>::known(500_J_per_kg_K);
    properties.thermalExpansion =
        MaterialProperty<ThermalExpansionCoefficient>::known(12.0e-6_per_K);
    properties.meltingTemperature = MaterialProperty<Temperature>::known(1800_K, 293.15_K);
    properties.electricalResistivity =
        MaterialProperty<ElectricalResistivity>::known(ElectricalResistivity::ofOhmMetres(1.4e-7));

#if defined(BETTERCAD_CF_PRESSURE_AS_CONDUCTIVITY)
    // A pressure is not a conductivity, however much both are "a material
    // number". This also guards against k being confused with a stiffness.
    properties.thermalConductivity = MaterialProperty<ThermalConductivity>::known(210_GPa);
#elif defined(BETTERCAD_CF_ENERGY_AS_SPECIFIC_HEAT)
    // An energy is not a specific heat: cp is per unit mass AND per kelvin.
    properties.specificHeatCapacity = MaterialProperty<SpecificHeatCapacity>::known(10000_J);
#elif defined(BETTERCAD_CF_HEAT_CAPACITY_AS_SPECIFIC_HEAT)
    // Nor is a TOTAL heat capacity (J/K) a SPECIFIC one (J/(kg K)). The mass is
    // the difference, and it is a dimension, so the compiler can see it.
    properties.specificHeatCapacity =
        MaterialProperty<SpecificHeatCapacity>::known(10000_J / 10_K);
#elif defined(BETTERCAD_CF_POISSON_AS_THERMAL_EXPANSION)
    // Both are written with Greek letters and neither is the other: alpha has
    // units of 1/K, nu has none.
    properties.thermalExpansion =
        MaterialProperty<ThermalExpansionCoefficient>::known(PoissonRatio::of(0.3));
#elif defined(BETTERCAD_CF_DIMENSIONLESS_AS_THERMAL_EXPANSION)
    // A thermal expansion coefficient is NOT dimensionless, which is the mistake
    // Dimension.hpp says the composed dimension exists to make impossible.
    properties.thermalExpansion = MaterialProperty<ThermalExpansionCoefficient>::known(12.0e-6);
#elif defined(BETTERCAD_CF_DENSITY_AS_RESISTIVITY)
    // Mass density and electrical resistivity are both written rho. This is the
    // confusion ADR-029 gave resistivity a named type to prevent.
    properties.electricalResistivity =
        MaterialProperty<ElectricalResistivity>::known(Density::fromSi(7850.0));
#elif defined(BETTERCAD_CF_RESISTIVITY_FROM_DOUBLE)
    // A bare number does not become a resistivity, so it cannot be stored as an
    // untyped double by accident.
    properties.electricalResistivity = MaterialProperty<ElectricalResistivity>::known(1.4e-7);
#elif defined(BETTERCAD_CF_RESISTIVITY_TO_DOUBLE)
    [[maybe_unused]] const double value =
        ElectricalResistivity::ofOhmMetres(1.4e-7);
#elif defined(BETTERCAD_CF_TEMPERATURE_FROM_DOUBLE)
    // A melting point is a Temperature, not a number of kelvin someone remembered
    // to convert.
    properties.meltingTemperature = MaterialProperty<Temperature>::known(1800.0);
#elif defined(BETTERCAD_CF_CONDUCTIVITY_AS_TEMPERATURE)
    // And a conductivity is not a temperature, so a reference temperature slot
    // cannot be filled with the value it describes.
    properties.meltingTemperature = MaterialProperty<Temperature>::known(50_W_per_m_K);
#endif

    return 0;
}
