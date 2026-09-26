// Build-failure tests for unit safety (see CMakeLists.txt in this directory).
//
// Built once without any BETTERCAD_CF_* macro as a control, which must compile,
// and once per macro, which must fail to compile with the expected diagnostic.
// The control proves that each failure comes from the selected line alone.
#include <bettercad/core/Units.hpp>

using namespace bettercad;
using namespace bettercad::literals;

namespace {

void takesDensity(Density /*unused*/) {}
void takesConductivity(ThermalConductivity /*unused*/) {}
void takesExpansion(ThermalExpansionCoefficient /*unused*/) {}
void takesKinematicViscosity(KinematicViscosity /*unused*/) {}

} // namespace

int main() {
    [[maybe_unused]] Length length = 1_mm;

    // The control passes each helper the type it asks for, so the helpers are
    // used and every case below fails for its own reason. These calls are also
    // the positive half of the contract: the right quantity IS accepted.
    takesDensity(7850_kg_per_m3);
    takesConductivity(50_W_per_m_K);
    takesExpansion(12_um_per_m_K);
    takesKinematicViscosity(1_mm2_per_s);

#if defined(BETTERCAD_CF_ADD_LENGTH_ANGLE)
    [[maybe_unused]] auto result = 1_mm + 1_deg;
#elif defined(BETTERCAD_CF_ASSIGN_MASS_TO_LENGTH)
    length = 5_kg;
#elif defined(BETTERCAD_CF_ASSIGN_DOUBLE_TO_LENGTH)
    length = 5.0;
#elif defined(BETTERCAD_CF_LENGTH_TO_DOUBLE)
    [[maybe_unused]] double value = length;
#elif defined(BETTERCAD_CF_FORCE_PER_LENGTH_AS_PRESSURE)
    [[maybe_unused]] Pressure pressure = 1_N / 1_m;
#elif defined(BETTERCAD_CF_COMPARE_LENGTH_TIME)
    [[maybe_unused]] bool less = 1_mm < 1_s;
#elif defined(BETTERCAD_CF_SQRT_OF_VOLUME)
    [[maybe_unused]] auto side = bettercad::sqrt(1_mm3);

// --- P15-UNITS-001: the engineering-data quantities ------------------------
//
// Each of these is a mistake somebody would plausibly make with a raw double,
// and each must be a build failure rather than a wrong number.
#elif defined(BETTERCAD_CF_ADD_DENSITY_PRESSURE)
    [[maybe_unused]] auto mixed = 7850_kg_per_m3 + 200_GPa;
#elif defined(BETTERCAD_CF_MODULUS_AS_CONDUCTIVITY)
    takesConductivity(200_GPa);
#elif defined(BETTERCAD_CF_LENGTH_AS_DENSITY)
    takesDensity(1_mm);
#elif defined(BETTERCAD_CF_SPECIFIC_HEAT_AS_EXPANSION)
    [[maybe_unused]] ThermalExpansionCoefficient alpha = 500_J_per_kg_K;
#elif defined(BETTERCAD_CF_MASS_PER_AREA_AS_DENSITY)
    // kg/m^2 is not kg/m^3: one missing length makes it a different quantity.
    [[maybe_unused]] Density areal = 1_kg / 1_m2;
#elif defined(BETTERCAD_CF_TEMPERATURE_AS_EXPANSION)
    // A temperature is not an inverse temperature.
    takesExpansion(Temperature::fromSi(300.0));
#elif defined(BETTERCAD_CF_POISSON_FROM_DOUBLE)
    // A pure number does not silently become Poisson's ratio.
    [[maybe_unused]] PoissonRatio nu = 0.3;
#elif defined(BETTERCAD_CF_POISSON_TO_DOUBLE)
    [[maybe_unused]] double bare = PoissonRatio::of(0.3);
#elif defined(BETTERCAD_CF_POISSON_AS_KINEMATIC_VISCOSITY)
    // Both are written "nu". Only one of them is a viscosity.
    takesKinematicViscosity(PoissonRatio::of(0.3));
#elif defined(BETTERCAD_CF_DYNAMIC_AS_KINEMATIC_VISCOSITY)
    takesKinematicViscosity(1_Pa_s);
#elif defined(BETTERCAD_CF_CONDUCTIVITY_AS_SPECIFIC_HEAT)
    [[maybe_unused]] SpecificHeatCapacity cp = 50_W_per_m_K;
#endif

    return 0;
}
