// Build-failure tests for structural result recovery (see CMakeLists.txt in
// this directory). Built once without any BETTERCAD_CF_* macro as a control,
// which must compile -- so every failure below is attributable to its own line.
//
// Four claims of P17-POST-001 are enforced here rather than reviewed:
//
//   1. RECOVERED FIELDS CANNOT BE CONJURED. `RecoveredFields` has a private
//      constructor and exactly one friend, so possessing one is the evidence
//      that the solution, the mesh, the numbering and the material all agreed,
//      that every node and element is present exactly once, and that every
//      value is finite (ADR-036, ADR-040). A hand-built one would be a stress
//      field nobody validated -- and, worse, one carrying a source stamp
//      nobody checked, which is how a stale result is made to look current.
//
//   2. A STRESS IS A PRESSURE AND A STRAIN IS A NUMBER, and the compiler keeps
//      them apart. Von Mises, the principal stresses and the hydrostatic
//      stress are all `Stress`; a strain and a principal strain are plain
//      doubles because a ratio has no unit. Neither converts to the other.
//
//   3. A DISPLACEMENT MAGNITUDE IS A LENGTH IN METRES, not a number. So it
//      cannot be compared against a stress, a strain or a bare `double`
//      threshold by accident -- which is the shape of the MPa/mm leak the core
//      must not have.
//
//   4. THE TENSOR TYPES ARE NOT INTERCHANGEABLE. A `StrainTensor3` holds
//      TENSOR shear (gamma/2) and a `Strain6` holds ENGINEERING shear; passing
//      one where the other is wanted is the convention error this milestone is
//      built against, and it does not compile.
#include <bettercad/core/Units.hpp>
#include <bettercad/structural/StructuralPost.hpp>

using namespace bettercad;
using namespace bettercad::structural;

namespace {

void takesStress(Stress /*unused*/) {}
void takesLength(Length /*unused*/) {}
void takesDouble(double /*unused*/) {}
void takesStrainTensor(const StrainTensor3& /*unused*/) {}

} // namespace

int main() {
    // The legitimate spellings, so the control compiles.
    const Strain6 strain{.xx = 1.0e-4, .gammaXy = 2.0e-4};
    const Stress6 stress{.xx = Stress::fromSi(1.0e6), .xy = Stress::fromSi(2.0e5)};

    const StressTensor3 stressTensor = tensorOf(stress);
    const StrainTensor3 strainTensor = tensorOf(strain);
    takesStress(stressTensor.at(0, 1));
    takesDouble(strainTensor.at(0, 1));
    takesStress(stressTensor.trace());
    takesDouble(strainTensor.trace());

    takesStress(vonMisesStress(stress));
    takesStress(hydrostaticStress(stress));
    takesStrainTensor(strainTensor);
    takesLength(displacementMagnitude(
        Translation3D{Length::fromSi(1.0), Length::fromSi(2.0), Length::fromSi(3.0)}));
    (void)principalStresses(stress);
    (void)principalStrains(strain);

#if defined(BETTERCAD_CF_RECOVERED_FIELDS_CONSTRUCTED_DIRECTLY)
    // Only recoverFields may build one, because only it has checked the four
    // sources against each other and every value for finiteness. There is no
    // default constructor at all.
    const RecoveredFields fields{};
    (void)fields;
#endif

#if defined(BETTERCAD_CF_VON_MISES_AS_DOUBLE)
    // A stress does not decay to a number, so a von Mises value cannot be
    // compared against a bare threshold or against a strain by accident.
    takesDouble(vonMisesStress(stress));
#endif

#if defined(BETTERCAD_CF_PRINCIPAL_STRAIN_AS_STRESS)
    // A principal STRAIN is dimensionless. Treating one as a stress is the
    // unit confusion that would let a strain be printed in pascals.
    const PrincipalStrains principal{.e1 = 1.0e-4};
    takesStress(principal.e1);
#endif

#if defined(BETTERCAD_CF_DISPLACEMENT_MAGNITUDE_AS_DOUBLE)
    // |u| is a Length in metres. If it decayed to a number, a display
    // conversion to millimetres could happen anywhere without the compiler
    // noticing -- and the core must stay SI.
    takesDouble(displacementMagnitude(
        Translation3D{Length::fromSi(1.0), Length::fromSi(0.0), Length::fromSi(0.0)}));
#endif

#if defined(BETTERCAD_CF_STRAIN6_AS_STRAIN_TENSOR)
    // A Strain6 carries ENGINEERING shear and a StrainTensor3 carries TENSOR
    // shear. Passing the first where the second is wanted would diagonalise a
    // matrix holding full gamma in its off-diagonals, which doubles every
    // shear term of every principal strain. It does not compile.
    takesStrainTensor(strain);
#endif

#if defined(BETTERCAD_CF_STRESS_TENSOR_AS_STRAIN_TENSOR)
    // Nor are the two tensor types interchangeable, although both hold six
    // components in the same positions: one is in pascals and one is a ratio.
    takesStrainTensor(stressTensor);
#endif

    return 0;
}
