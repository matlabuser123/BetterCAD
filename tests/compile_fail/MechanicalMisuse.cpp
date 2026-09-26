// Build-failure tests for mechanical material properties (see CMakeLists.txt in
// this directory). Built once without any BETTERCAD_CF_* macro as a control,
// which must compile: without that, a property type that had quietly stopped
// being type-safe would still pass every negative case below.
#include <bettercad/core/materials/MaterialLibrary.hpp>
#include <bettercad/core/materials/MaterialProperty.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/core/units/Units.hpp>

using namespace bettercad;
using namespace bettercad::literals;
using materials::Elongation;
using materials::Hardness;
using materials::HardnessScale;
using materials::MaterialProperty;
using materials::MechanicalProperties;

int main() {
    MechanicalProperties properties;

    // The control: every one of these is the intended way to say it.
    properties.density = MaterialProperty<Density>::known(Density::fromSi(7850.0));
    properties.youngsModulus = MaterialProperty<ElasticModulus>::known(210_GPa);
    properties.poissonRatio = MaterialProperty<PoissonRatio>::known(PoissonRatio::of(0.30));
    properties.yieldStrength = MaterialProperty<Stress>::known(250_MPa);
    properties.elongation = MaterialProperty<Elongation>::known(Elongation::of(0.12));
    properties.hardness =
        MaterialProperty<Hardness>::known(Hardness::of(60.0, HardnessScale::RockwellC));
    [[maybe_unused]] const MaterialProperty<ElasticModulus> shear =
        materials::derivedShearModulus(properties);
    // The control uses the library too, so the include above cannot rot
    // unnoticed and leave the immutability case failing for the wrong reason.
    [[maybe_unused]] const materials::LibraryMaterial entry =
        materials::builtInMaterials().front();

#if defined(BETTERCAD_CF_DENSITY_AS_YOUNGS_MODULUS)
    // A density is not a modulus, however much both are "a material number".
    properties.youngsModulus = MaterialProperty<ElasticModulus>::known(Density::fromSi(7850.0));
#elif defined(BETTERCAD_CF_CONDUCTIVITY_AS_YIELD_STRENGTH)
    properties.yieldStrength =
        MaterialProperty<Stress>::known(ThermalConductivity::fromSi(50.0));
#elif defined(BETTERCAD_CF_SPECIFIC_HEAT_AS_SHEAR_MODULUS)
    [[maybe_unused]] const MaterialProperty<ElasticModulus> wrong =
        MaterialProperty<SpecificHeatCapacity>::known(SpecificHeatCapacity::fromSi(500.0));
#elif defined(BETTERCAD_CF_POISSON_FROM_DOUBLE)
    // A bare number does not become a Poisson ratio on its own, which is what
    // keeps it from being confused with a kinematic viscosity or a strain.
    properties.poissonRatio = MaterialProperty<PoissonRatio>::known(0.30);
#elif defined(BETTERCAD_CF_ELONGATION_FROM_DOUBLE)
    // Nor does a bare number become an elongation: 0.12 and 12 mean different
    // things and the type is where that is settled.
    properties.elongation = MaterialProperty<Elongation>::known(0.12);
#elif defined(BETTERCAD_CF_HARDNESS_WITHOUT_SCALE)
    // A hardness number without its scale is a number without meaning.
    properties.hardness = MaterialProperty<Hardness>::known(Hardness::of(60.0));
#elif defined(BETTERCAD_CF_HARDNESS_AS_STRESS)
    // Hardness is not a pressure, so it cannot be compared with a strength.
    [[maybe_unused]] const Stress asStress = Hardness::of(60.0, HardnessScale::RockwellC);
#elif defined(BETTERCAD_CF_PROPERTY_TO_VALUE)
    // A property is not its value: an Unknown one would otherwise become a
    // number somewhere far from here.
    [[maybe_unused]] const ElasticModulus modulus = properties.youngsModulus;
#elif defined(BETTERCAD_CF_MUTATE_LIBRARY_ENTRY)
    // The built-in library is reference data. There is nothing to mutate, and
    // the span it comes back in says so.
    materials::builtInMaterials()[0] = materials::builtInMaterials()[1];
#endif

    return 0;
}
