// Build-failure tests for mass properties (see CMakeLists.txt in this
// directory). Built once without any BETTERCAD_CF_* macro as a control, which
// must compile.
//
// The subject is the boundary between a second moment of VOLUME and a second
// moment of MASS. They differ by a density, they differ by nine orders of
// magnitude in mm-scale models, and nothing about either number says which it is
// -- so the type system has to. Several cases below also prove an ABSENCE: there
// is no way to store a mass, because mass is derived and never persisted
// (ADR-026), and a milestone that quietly added a setter would be caught here
// rather than by a reviewer noticing.
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/features/MassProperties.hpp>

using namespace bettercad;
using namespace bettercad::literals;

int main() {
    Document document{"Part"};
    geometry::VolumeSecondMoments volume;
    features::InertiaTensor inertia;
    const Density density = Density::fromSi(8000.0);
    const Mass mass = 0.24 * units::kg;

    // The control: the intended way to say all of this.
    volume.xx = 85.0 * units::mm5;
    inertia.xx = density * volume.xx;
    inertia.about = Point3D{1_mm, 2_mm, 3_mm};
    [[maybe_unused]] const features::InertiaTensor shifted =
        features::shiftedFromCentroid(inertia, mass, Point3D{});
    [[maybe_unused]] const double kgMm2 = inertia.xx.in(units::kg_mm2);
    [[maybe_unused]] const double mm5 = volume.xx.in(units::mm5);

#if defined(BETTERCAD_CF_VOLUME_MOMENT_AS_MASS_MOMENT)
    // The central confusion this milestone had to design out: the kernel's number
    // is a second moment of VOLUME, and it becomes an inertia only by being
    // multiplied by a density. Assigning it straight across would be wrong by a
    // factor of the density, which is a plausible-looking number.
    inertia.xx = volume.xx;
#elif defined(BETTERCAD_CF_MASS_MOMENT_AS_VOLUME_MOMENT)
    // And not the other way either.
    volume.xx = inertia.xx;
#elif defined(BETTERCAD_CF_ADD_VOLUME_AND_MASS_MOMENT)
    [[maybe_unused]] const auto sum = volume.xx + inertia.xx;
#elif defined(BETTERCAD_CF_MASS_MOMENT_IN_MM5)
    // mm^5 is not a unit of inertia. Reading an inertia through it would silently
    // scale by 1e9 in a millimetre model.
    [[maybe_unused]] const double wrong = inertia.xx.in(units::mm5);
#elif defined(BETTERCAD_CF_VOLUME_MOMENT_IN_KG_MM2)
    [[maybe_unused]] const double wrong = volume.xx.in(units::kg_mm2);
#elif defined(BETTERCAD_CF_MASS_MOMENT_FROM_DOUBLE)
    // No bare double becomes an inertia: it would carry no statement about which
    // unit it was in, and kg m^2 and kg mm^2 differ by a million.
    inertia.xx = 68.0;
#elif defined(BETTERCAD_CF_MASS_MOMENT_TO_DOUBLE)
    [[maybe_unused]] const double wrong = inertia.xx;
#elif defined(BETTERCAD_CF_VOLUME_TIMES_DENSITY_IS_NOT_A_MOMENT)
    // A volume times a density is a MASS, not a moment. The dimensions catch the
    // missing pair of length factors.
    inertia.xx = density * (30000.0 * units::mm3);
#elif defined(BETTERCAD_CF_SHIFT_WITH_A_VOLUME)
    // Huygens' theorem takes the body's MASS. A volume has the wrong dimension and
    // would give an answer in the wrong units entirely.
    [[maybe_unused]] const features::InertiaTensor wrong =
        features::shiftedFromCentroid(inertia, 30000.0 * units::mm3, Point3D{});
#elif defined(BETTERCAD_CF_SHIFT_WITH_A_DENSITY)
    [[maybe_unused]] const features::InertiaTensor wrong =
        features::shiftedFromCentroid(inertia, density, Point3D{});
#elif defined(BETTERCAD_CF_TENSOR_REFERENCE_FROM_COMPONENTS)
    // The reference point is a Point3D, not three bare numbers: a tensor paired
    // with the wrong reference point is a defect no check can find later.
    inertia.about = 0.0;
#elif defined(BETTERCAD_CF_DOCUMENT_STORES_MASS)
    // ADR-026: mass is DERIVED, never stored. There is no setter, so a mass cannot
    // be written into a document and cannot go stale in a file. This is the
    // absence that keeps the promise.
    [[maybe_unused]] const auto stored = document.setMass(mass);
#elif defined(BETTERCAD_CF_DOCUMENT_READS_MASS)
    // And none to read, either: the only way to a mass is to derive one.
    [[maybe_unused]] const auto read = document.mass();
#elif defined(BETTERCAD_CF_MASS_PROPERTIES_HAVE_NO_SETTER)
    // Nor can a derived result be edited into saying something else.
    features::PartMassProperties properties;
    properties.setMass(mass);
#elif defined(BETTERCAD_CF_BODY_RETURNS_MASS)
    // geometry::Body knows no density, so it cannot answer for mass (ADR-026, and
    // the reason MassProperties has no mass field). Asking it must not compile.
    const geometry::Body body;
    [[maybe_unused]] const auto wrong = body.mass();
#endif

    return 0;
}
