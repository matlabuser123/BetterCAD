// Build-failure tests for unit safety (see CMakeLists.txt in this directory).
//
// Built once without any BETTERCAD_CF_* macro as a control, which must compile,
// and once per macro, which must fail to compile with the expected diagnostic.
// The control proves that each failure comes from the selected line alone.
#include <bettercad/core/Units.hpp>

using namespace bettercad;
using namespace bettercad::literals;

int main() {
    [[maybe_unused]] Length length = 1_mm;

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
#endif

    return 0;
}
