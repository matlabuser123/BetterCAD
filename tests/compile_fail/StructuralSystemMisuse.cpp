// Build-failure tests for the global system (see CMakeLists.txt in this
// directory). Built once without any BETTERCAD_CF_* macro as a control, which
// must compile -- so every failure below is attributable to its own line and
// not to a missing header.
//
// Two claims of P17-ASSEMBLY-001 are enforced here rather than reviewed:
//
//   1. A MATRIX, A VECTOR AND A SYSTEM CANNOT BE CONJURED. Each has a private
//      constructor and exactly one friend, so possessing one is the evidence
//      that its dimensions match a real numbering, that every element was
//      accepted by P17-ELEM, that every load resolved against the same mesh,
//      and that no entry is non-finite (ADR-036). A hand-built K would defeat
//      every check in the module -- and so would a default-constructed one,
//      which is why there is not one.
//
//   2. A STIFFNESS IS NOT A FORCE AND NEITHER IS A BARE DOUBLE. `coeff()`
//      returns `Stiffness` (N/m) and `operator[]` returns `Force` (N), and the
//      two do not convert to each other or to a number. `K u = F` is an
//      equation between physical quantities, and the compiler is what keeps it
//      one: a milestone that mixed them would produce a plausible answer in
//      the wrong units.
#include <bettercad/core/Units.hpp>
#include <bettercad/structural/StructuralSystem.hpp>

using namespace bettercad;
using namespace bettercad::structural;

namespace {

void takesStiffness(Stiffness /*unused*/) {}
void takesForce(Force /*unused*/) {}

} // namespace

int main() {
    // The legitimate spellings, so the control compiles.
    takesStiffness(Stiffness::fromSi(1.0));
    takesForce(Force::fromSi(1.0));

#if defined(BETTERCAD_CF_STIFFNESS_MATRIX_CONSTRUCTED_DIRECTLY)
    // Possession is the evidence. Only assembleStructuralSystem may build one,
    // because only it has validated the pattern, the dimensions and every
    // entry.
    const StiffnessMatrix k{};
    (void)k;
#endif

#if defined(BETTERCAD_CF_FORCE_VECTOR_CONSTRUCTED_DIRECTLY)
    const ForceVector f{};
    (void)f;
#endif

#if defined(BETTERCAD_CF_GLOBAL_SYSTEM_CONSTRUCTED_DIRECTLY)
    // And the system has no default constructor at all: it cannot exist
    // without a matrix, a vector and a numbering that were each already
    // validated by the function entitled to build them.
    const GlobalStructuralSystem system{};
    (void)system;
#endif

#if defined(BETTERCAD_CF_STIFFNESS_AS_FORCE)
    // N/m is not N. `K u = F` is dimensional, and this is where that is
    // enforced rather than reviewed.
    takesForce(Stiffness::fromSi(1.0));
#endif

#if defined(BETTERCAD_CF_FORCE_AS_STIFFNESS)
    takesStiffness(Force::fromSi(1.0));
#endif

#if defined(BETTERCAD_CF_STIFFNESS_AS_DOUBLE)
    // Nor does a stiffness decay to a number, so it cannot be summed with a
    // force or written into a raw buffer by accident. `values()` is the
    // deliberate SI escape hatch and it is documented as one.
    const double raw = Stiffness::fromSi(1.0);
    (void)raw;
#endif

    return 0;
}
