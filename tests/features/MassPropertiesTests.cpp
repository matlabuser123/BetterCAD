#include "features/FeatureTestSupport.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Booleans.hpp>
#include <bettercad/core/geometry/Primitives.hpp>
#include <bettercad/core/geometry/Transform.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/MassProperties.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <memory>
#include <numbers>
#include <string>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using features::InertiaTensor;
using features::MaterialDefinition;
using features::PartMassProperties;

// P15-MASS-001: mass, centre of mass and inertia, derived from geometry and a
// material's density (ADR-026).
//
// THE EXPECTED VALUES HERE ARE CLOSED-FORM, EVALUATED BY HAND. None was obtained
// by running BetterCAD and recording what it said, and none was obtained by
// calling one BetterCAD routine to check another. Where a fixture does compare two
// BetterCAD paths -- the kernel integrating a rotated body against the rotation of
// an integrated tensor -- it is because that comparison is the claim under test,
// and both sides are also checked against closed forms separately.
//
// THE REFERENCE FIXTURE, from which most of the file follows. A 20 x 30 x 50 mm
// box with a corner at the origin, of density 8000 kg/m^3:
//
//   V  = 20 x 30 x 50 = 30000 mm^3 = 3e-5 m^3
//   m  = 8000 x 3e-5  = 0.24 kg
//   cm = (10, 15, 25) mm
//
//   about the CENTRE OF MASS, from I = m (b^2 + c^2) / 12:
//     xx = 0.24 x (900 + 2500)/12 = 0.24 x 850/3 = 68  kg mm^2
//     yy = 0.24 x (400 + 2500)/12 = 0.24 x 725/3 = 58  kg mm^2
//     zz = 0.24 x (400 +  900)/12 = 0.24 x 325/3 = 26  kg mm^2
//     xy = xz = yz = 0                                (symmetry)
//
//   about the ORIGIN, by Huygens with d = cm = (10, 15, 25):
//     xx = 68 + 0.24 (225 + 625) = 68 + 204 = 272  kg mm^2
//     yy = 58 + 0.24 (100 + 625) = 58 + 174 = 232  kg mm^2
//     zz = 26 + 0.24 (100 + 225) = 26 +  78 = 104  kg mm^2
//     xy = -0.24 x 10 x 15 = -36   xz = -0.24 x 10 x 25 = -60
//     yz = -0.24 x 15 x 25 = -90                        kg mm^2
//
// Every one of those is an integer, which is why these dimensions were chosen.

namespace {

constexpr double pi = std::numbers::pi;
/// The measured budget of the kernel's second-moment integration is <= 5e-16 for
/// planar-faced bodies, <= 6e-16 for spheres and <= 2.9e-13 for cylinders over
/// four decades of size (docs/verification/P15-MASS-001/kernel-probe). 1e-12
/// covers the worst of those with headroom; it is not a concession, and no fixture
/// here needs more than 1e-13.
constexpr double kRel = 1e-12;
/// Products that vanish by symmetry, in kg mm^2, against bodies whose non-zero
/// components are of order 10 to 1000.
constexpr double kZero = 1e-12;

Density steelDensity() {
    return Density::fromSi(8000.0);
}

MaterialDefinition withDensity(Density density) {
    MaterialDefinition definition;
    definition.designation = "Steel";
    definition.mechanical.density = materials::MaterialProperty<Density>::known(density);
    return definition;
}

/// A document holding one extruded box with a corner at the origin, and a material
/// assigned to the part.
struct BoxPart {
    Document document{"Part"};
    features::Regenerator regenerator;
    ObjectId feature{};
    ObjectId profile{};
    std::array<EntityId, 4> lines{};
    MaterialId material{};

    BoxPart(Length a, Length b, Length c, Density density = steelDensity(),
            Length originX = 0_mm, Length originY = 0_mm) {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        lines = addRectangle(*sketch, originX, originY, a, b);
        profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = c});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));
        const Result<MaterialId> id =
            features::createMaterial(document, "Steel", withDensity(density));
        REQUIRE(id);
        material = *id;
        REQUIRE(features::assignMaterial(document, material));
        requireReport(regenerator, document);
    }

    [[nodiscard]] Result<PartMassProperties> properties() {
        return features::partMassProperties(document, regenerator, feature);
    }

    [[nodiscard]] PartMassProperties requireProperties() {
        const Result<PartMassProperties> result = properties();
        REQUIRE(result.has_value());
        return *result;
    }
};

double kgMm2(MassMomentOfInertia value) {
    return value.in(units::kg_mm2);
}

/// Positions in millimetres. 1e-9 mm is the repository's position tolerance
/// (GeometryTestSupport::kPositionToleranceMm), restated here because a features
/// test does not include the geometry tests' header.
void checkPoint(const Point3D& actual, double xMm, double yMm, double zMm) {
    CHECK_THAT(actual.x.in(units::mm), WithinAbs(xMm, 1e-9));
    CHECK_THAT(actual.y.in(units::mm), WithinAbs(yMm, 1e-9));
    CHECK_THAT(actual.z.in(units::mm), WithinAbs(zMm, 1e-9));
}

void checkTensor(const InertiaTensor& tensor, double xx, double yy, double zz, double xy, double xz,
                 double yz) {
    CHECK_THAT(kgMm2(tensor.xx), WithinRel(xx, kRel));
    CHECK_THAT(kgMm2(tensor.yy), WithinRel(yy, kRel));
    CHECK_THAT(kgMm2(tensor.zz), WithinRel(zz, kRel));
    if (xy == 0.0) {
        CHECK_THAT(kgMm2(tensor.xy), WithinAbs(0.0, kZero));
    } else {
        CHECK_THAT(kgMm2(tensor.xy), WithinRel(xy, kRel));
    }
    if (xz == 0.0) {
        CHECK_THAT(kgMm2(tensor.xz), WithinAbs(0.0, kZero));
    } else {
        CHECK_THAT(kgMm2(tensor.xz), WithinRel(xz, kRel));
    }
    if (yz == 0.0) {
        CHECK_THAT(kgMm2(tensor.yz), WithinAbs(0.0, kZero));
    } else {
        CHECK_THAT(kgMm2(tensor.yz), WithinRel(yz, kRel));
    }
}

} // namespace

// --- the reference cuboid ---------------------------------------------------

TEST_CASE("MassProperties_OfACuboidMatchTheClosedFormMassCentroidAndInertia",
          "[features][mass]") {
    BoxPart part{20_mm, 30_mm, 50_mm};
    const PartMassProperties properties = part.requireProperties();

    CHECK(properties.feature == part.feature);
    CHECK(properties.material == part.material);
    CHECK_THAT(properties.density.in(units::kg_per_m3), WithinRel(8000.0, kRel));
    CHECK_THAT(properties.volume.in(units::mm3), WithinRel(30000.0, kRel));
    CHECK_THAT(properties.mass.in(units::kg), WithinRel(0.24, kRel));
    checkPoint(properties.centreOfMass, 10.0, 15.0, 25.0);

    // m (b^2 + c^2) / 12 for each axis, products zero by symmetry.
    CHECK(properties.aboutCentreOfMass.about == properties.centreOfMass);
    checkTensor(properties.aboutCentreOfMass, 68.0, 58.0, 26.0, 0.0, 0.0, 0.0);

    // Huygens to the origin, with d = (10, 15, 25) mm.
    CHECK(properties.aboutOrigin.about == Point3D{});
    checkTensor(properties.aboutOrigin, 272.0, 232.0, 104.0, -36.0, -60.0, -90.0);
}

TEST_CASE("MassProperties_OfACuboidPutTheOriginTensorsOffDiagonalsNegative",
          "[features][mass]") {
    // Asserted as signs, separately, because the tensor convention is the one
    // thing here that a plausible-looking wrong answer would hide: every magnitude
    // above is correct under both conventions.
    BoxPart part{20_mm, 30_mm, 50_mm};
    const PartMassProperties properties = part.requireProperties();
    CHECK(kgMm2(properties.aboutOrigin.xy) < 0.0);
    CHECK(kgMm2(properties.aboutOrigin.xz) < 0.0);
    CHECK(kgMm2(properties.aboutOrigin.yz) < 0.0);
    // And the diagonal is positive, always: a second moment of a positive mass
    // about any axis is positive.
    CHECK(kgMm2(properties.aboutOrigin.xx) > 0.0);
    CHECK(kgMm2(properties.aboutCentreOfMass.xx) > 0.0);
}

TEST_CASE("MassProperties_SatisfyTheTriangleInequalityOfTheInertiaTensor",
          "[features][mass]") {
    // A physical inertia tensor obeys Ixx + Iyy >= Izz on every pairing, because
    // each principal moment is an integral of a sum of two squares. It is a check
    // on the RESULT rather than on the arithmetic that produced it, so it would
    // catch a wrong component mapping that the closed forms happened to match.
    BoxPart part{20_mm, 30_mm, 50_mm};
    const PartMassProperties properties = part.requireProperties();
    for (const InertiaTensor& tensor : {properties.aboutCentreOfMass, properties.aboutOrigin}) {
        const double xx = kgMm2(tensor.xx);
        const double yy = kgMm2(tensor.yy);
        const double zz = kgMm2(tensor.zz);
        CHECK(xx + yy >= zz);
        CHECK(yy + zz >= xx);
        CHECK(xx + zz >= yy);
    }
}

// --- a cylinder -------------------------------------------------------------

TEST_CASE("MassProperties_OfACylinderMatchTheClosedFormAxialAndTransverseInertia",
          "[features][mass]") {
    // r = 20, h = 60 mm, density 8000 kg/m^3, built as a primitive because the
    // arithmetic is the point and a sketch of a circle would add nothing:
    //
    //   V  = pi r^2 h = 24000 pi mm^3       m = 0.192 pi kg
    //   cm = (0, 0, 30) mm
    //   zz = m r^2 / 2            = 38.4 pi  kg mm^2   (the axis)
    //   xx = yy = m (r^2/4 + h^2/12)
    //      = 0.192 pi (100 + 300) = 76.8 pi  kg mm^2
    //
    // about the origin, which is the centre of the base, d = (0,0,30):
    //   xx = yy = 76.8 pi + m x 900 = 76.8 pi + 172.8 pi = 249.6 pi
    //   zz = 38.4 pi   (the axis passes through the origin)
    //   products all zero
    const Result<geometry::Body> body = geometry::makeCylinder(20_mm, 60_mm);
    REQUIRE(body);
    const Result<geometry::VolumeSecondMoments> moments = body->centroidalVolumeSecondMoments();
    REQUIRE(moments);
    const Result<geometry::MassProperties> geometric = body->massProperties();
    REQUIRE(geometric);

    const Density density = steelDensity();
    const Mass mass = density * geometric->volume;
    CHECK_THAT(mass.in(units::kg), WithinRel(0.192 * pi, kRel));
    CHECK_THAT((density * moments->zz).in(units::kg_mm2), WithinRel(38.4 * pi, kRel));
    CHECK_THAT((density * moments->xx).in(units::kg_mm2), WithinRel(76.8 * pi, kRel));
    CHECK_THAT((density * moments->yy).in(units::kg_mm2), WithinRel(76.8 * pi, kRel));

    InertiaTensor centroidal;
    centroidal.xx = density * moments->xx;
    centroidal.yy = density * moments->yy;
    centroidal.zz = density * moments->zz;
    centroidal.about = geometric->centerOfMass;
    const InertiaTensor aboutOrigin =
        features::shiftedFromCentroid(centroidal, mass, Point3D{});
    checkTensor(aboutOrigin, 249.6 * pi, 249.6 * pi, 38.4 * pi, 0.0, 0.0, 0.0);
}

// --- a void -----------------------------------------------------------------

TEST_CASE("MassProperties_OfAPlateWithAHoleSubtractTheVoidsInertia", "[features][mass]") {
    // A 40 x 40 x 10 plate with a 5 mm-radius hole through its centre. Both the
    // plate and the void are centred on (20, 20, 5), so the composite centroid is
    // there too and the moments subtract directly -- no parallel-axis term, which
    // is what makes this fixture about the SUBTRACTION and nothing else.
    //
    //   V  = 16000 - 250 pi                                        mm^3
    //   zz = 16000 (40^2 + 40^2)/12 - 250 pi (5^2/2)
    //      = 4266666.666... - 3125 pi                              mm^5
    //   xx = 16000 (40^2 + 10^2)/12 - 250 pi (25/4 + 100/12)
    //      = 2266666.666... - 3645.8333... pi                      mm^5
    //
    // A wrong sign on the void would show as a volume that is too large, so the
    // volume is checked too: an inertia that subtracted a void it had added would
    // still be wrong in a way the diagonal alone might not reveal.
    const Result<geometry::Body> plate = geometry::makeBox(40_mm, 40_mm, 10_mm);
    REQUIRE(plate);
    const Result<geometry::Body> drill =
        geometry::makeCylinder(Axis3D{Point3D{20_mm, 20_mm, -5_mm}, Direction3D::unitZ()}, 5_mm, 20_mm);
    REQUIRE(drill);
    const Result<geometry::Body> drilled = geometry::booleanDifference(*plate, *drill);
    REQUIRE(drilled);

    const Result<geometry::MassProperties> geometric = drilled->massProperties();
    REQUIRE(geometric);
    CHECK_THAT(geometric->volume.in(units::mm3), WithinRel(16000.0 - 250.0 * pi, kRel));
    checkPoint(geometric->centerOfMass, 20.0, 20.0, 5.0);

    const Result<geometry::VolumeSecondMoments> moments = drilled->centroidalVolumeSecondMoments();
    REQUIRE(moments);
    const double expectedZz = 16000.0 * 3200.0 / 12.0 - 250.0 * pi * 12.5;
    const double expectedXx = 16000.0 * 1700.0 / 12.0 - 250.0 * pi * (6.25 + 100.0 / 12.0);
    CHECK_THAT(moments->zz.in(units::mm5), WithinRel(expectedZz, kRel));
    CHECK_THAT(moments->xx.in(units::mm5), WithinRel(expectedXx, kRel));
    CHECK_THAT(moments->yy.in(units::mm5), WithinRel(expectedXx, kRel));
    // The void is on the axis of symmetry, so no product appears.
    CHECK_THAT(moments->xy.in(units::mm5), WithinAbs(0.0, 1e-9));
}

// --- translation ------------------------------------------------------------

TEST_CASE("MassProperties_AreUnchangedAboutTheCentroidWhenTheBodyMoves", "[features][mass]") {
    // The same box at two positions. The centroidal tensor is invariant under
    // translation -- that is what makes it the one worth storing -- while the
    // origin tensor must move by exactly Huygens' term. Both halves are asserted,
    // because a routine that returned the centroidal tensor for both would pass
    // the first half alone.
    BoxPart atOrigin{20_mm, 30_mm, 50_mm};
    BoxPart moved{20_mm, 30_mm, 50_mm, steelDensity(), 100_mm, 200_mm};
    const PartMassProperties here = atOrigin.requireProperties();
    const PartMassProperties there = moved.requireProperties();

    CHECK_THAT(there.mass.in(units::kg), WithinRel(here.mass.in(units::kg), kRel));
    checkPoint(there.centreOfMass, 110.0, 215.0, 25.0);
    checkTensor(there.aboutCentreOfMass, 68.0, 58.0, 26.0, 0.0, 0.0, 0.0);

    // About the origin, with d = (110, 215, 25):
    //   xx = 68 + 0.24 (215^2 + 25^2) = 68 + 0.24 x 46850 = 68 + 11244 = 11312
    //   yy = 58 + 0.24 (110^2 + 25^2) = 58 + 0.24 x 12725 = 58 +  3054 =  3112
    //   zz = 26 + 0.24 (110^2 + 215^2)= 26 + 0.24 x 58325 = 26 + 13998 = 14024
    //   xy = -0.24 x 110 x 215 = -5676
    //   xz = -0.24 x 110 x  25 =  -660
    //   yz = -0.24 x 215 x  25 = -1290
    checkTensor(there.aboutOrigin, 11312.0, 3112.0, 14024.0, -5676.0, -660.0, -1290.0);
}

// --- rotation ---------------------------------------------------------------

TEST_CASE("MassProperties_RotateAsRIRTransposeForANonCubeAtANonRightAngle",
          "[features][mass]") {
    // The reference box turned 30 degrees about Z through its own centroid.
    // Deliberately not a cube and not a right angle: a cube's tensor is isotropic
    // in this plane and would rotate into itself, and 90 degrees would only
    // permute the components. With A = 68 and B = 58 kg mm^2 and c = cos 30,
    // s = sin 30,
    //
    //   xx' = c^2 A + s^2 B = 0.75 x 68 + 0.25 x 58 = 65.5
    //   yy' = s^2 A + c^2 B = 0.25 x 68 + 0.75 x 58 = 60.5
    //   zz' = C                                     = 26
    //   xy' = s c (A - B)   = (sqrt(3)/4) x 10       = 4.330127018922193
    //   xz' = yz' = 0
    //
    // The trace is invariant: 65.5 + 60.5 + 26 = 152 = 68 + 58 + 26.
    BoxPart part{20_mm, 30_mm, 50_mm};
    const PartMassProperties upright = part.requireProperties();
    const RigidTransform3D turn =
        RigidTransform3D::rotation(Axis3D{upright.centreOfMass, Direction3D::unitZ()}, 30_deg);
    const PartMassProperties turned = features::transformed(upright, turn);

    // Mass, volume and density are invariant under a rigid motion.
    CHECK_THAT(turned.mass.in(units::kg), WithinRel(0.24, kRel));
    CHECK_THAT(turned.volume.in(units::mm3), WithinRel(30000.0, kRel));
    CHECK(turned.material == upright.material);
    // The rotation is about the centroid, so the centroid does not move.
    checkPoint(turned.centreOfMass, 10.0, 15.0, 25.0);

    const double sc = std::sqrt(3.0) / 4.0;
    checkTensor(turned.aboutCentreOfMass, 65.5, 60.5, 26.0, sc * 10.0, 0.0, 0.0);
    CHECK_THAT(kgMm2(turned.aboutCentreOfMass.xx) + kgMm2(turned.aboutCentreOfMass.yy) +
                   kgMm2(turned.aboutCentreOfMass.zz),
               WithinRel(152.0, kRel));
}

TEST_CASE("MassProperties_OfARotatedBodyAgreeWithRotatingTheTensor", "[features][mass]") {
    // The claim that makes transformed() worth having: integrating the MOVED body
    // and moving the INTEGRATED tensor give the same answer. The two paths share
    // no arithmetic -- one is the kernel's quadrature over rotated faces, the other
    // is A I A^T in this module -- so agreement between them is evidence about
    // both. Each side is also pinned to a closed form in the tests above and below.
    const Result<geometry::Body> upright = geometry::makeBox(20_mm, 30_mm, 50_mm);
    REQUIRE(upright);
    const Result<geometry::MassProperties> uprightGeometry = upright->massProperties();
    REQUIRE(uprightGeometry);
    const Result<geometry::VolumeSecondMoments> uprightMoments =
        upright->centroidalVolumeSecondMoments();
    REQUIRE(uprightMoments);

    // A rotation about an axis that is neither a principal axis of the box nor
    // through its centroid, at an angle that is not a multiple of 45 degrees, so
    // that all three products come out non-zero and distinct.
    const RigidTransform3D motion = RigidTransform3D::rotation(
        Axis3D{Point3D{7_mm, -3_mm, 11_mm},
               Direction3D::fromComponents(1.0, 2.0, 3.0).value()},
        37_deg);
    const Result<geometry::Body> moved = geometry::transformed(*upright, motion);
    REQUIRE(moved);
    const Result<geometry::VolumeSecondMoments> movedMoments =
        moved->centroidalVolumeSecondMoments();
    REQUIRE(movedMoments);

    const Density density = steelDensity();
    InertiaTensor before;
    before.xx = density * uprightMoments->xx;
    before.yy = density * uprightMoments->yy;
    before.zz = density * uprightMoments->zz;
    before.xy = density * uprightMoments->xy;
    before.xz = density * uprightMoments->xz;
    before.yz = density * uprightMoments->yz;
    before.about = uprightGeometry->centerOfMass;

    const InertiaTensor predicted = features::transformed(before, motion);
    // All three products must be far larger than the tolerance the comparison below
    // uses, or the fixture would agree about nothing. That tolerance times the
    // diagonal is ~6e-11 kg mm^2, so 0.1 is nine orders of magnitude above the
    // noise floor -- the measured values are 0.8 and up.
    REQUIRE(std::abs(kgMm2(predicted.xy)) > 0.1);
    REQUIRE(std::abs(kgMm2(predicted.xz)) > 0.1);
    REQUIRE(std::abs(kgMm2(predicted.yz)) > 0.1);

    CHECK_THAT(kgMm2(density * movedMoments->xx), WithinRel(kgMm2(predicted.xx), kRel));
    CHECK_THAT(kgMm2(density * movedMoments->yy), WithinRel(kgMm2(predicted.yy), kRel));
    CHECK_THAT(kgMm2(density * movedMoments->zz), WithinRel(kgMm2(predicted.zz), kRel));
    CHECK_THAT(kgMm2(density * movedMoments->xy), WithinRel(kgMm2(predicted.xy), kRel));
    CHECK_THAT(kgMm2(density * movedMoments->xz), WithinRel(kgMm2(predicted.xz), kRel));
    CHECK_THAT(kgMm2(density * movedMoments->yz), WithinRel(kgMm2(predicted.yz), kRel));

    // The predicted reference point is where the body's centroid actually went.
    const Result<geometry::MassProperties> movedGeometry = moved->massProperties();
    REQUIRE(movedGeometry);
    CHECK_THAT(predicted.about.x.in(units::mm),
               WithinAbs(movedGeometry->centerOfMass.x.in(units::mm), 1e-9));
    CHECK_THAT(predicted.about.y.in(units::mm),
               WithinAbs(movedGeometry->centerOfMass.y.in(units::mm), 1e-9));
    CHECK_THAT(predicted.about.z.in(units::mm),
               WithinAbs(movedGeometry->centerOfMass.z.in(units::mm), 1e-9));
}

TEST_CASE("MassProperties_RotationPreservesTheTraceAndTheEigenvalueInvariants",
          "[features][mass]") {
    // A I A^T is a similarity transformation, so the trace, the sum of the 2x2
    // principal minors and the determinant are all invariant. Three independent
    // scalar checks on a rotation by an arbitrary angle about an arbitrary axis,
    // needing no closed form for the rotated tensor at all.
    BoxPart part{20_mm, 30_mm, 50_mm};
    const PartMassProperties upright = part.requireProperties();
    const RigidTransform3D motion = RigidTransform3D::rotation(
        Axis3D{Point3D{1_mm, 2_mm, 3_mm}, Direction3D::fromComponents(-2.0, 5.0, 1.0).value()},
        73_deg);
    const InertiaTensor before = upright.aboutCentreOfMass;
    const InertiaTensor after = features::transformed(before, motion);

    const auto trace = [](const InertiaTensor& t) {
        return kgMm2(t.xx) + kgMm2(t.yy) + kgMm2(t.zz);
    };
    const auto minors = [](const InertiaTensor& t) {
        const double xx = kgMm2(t.xx);
        const double yy = kgMm2(t.yy);
        const double zz = kgMm2(t.zz);
        const double xy = kgMm2(t.xy);
        const double xz = kgMm2(t.xz);
        const double yz = kgMm2(t.yz);
        return xx * yy - xy * xy + yy * zz - yz * yz + xx * zz - xz * xz;
    };
    const auto determinant = [](const InertiaTensor& t) {
        const double xx = kgMm2(t.xx);
        const double yy = kgMm2(t.yy);
        const double zz = kgMm2(t.zz);
        const double xy = kgMm2(t.xy);
        const double xz = kgMm2(t.xz);
        const double yz = kgMm2(t.yz);
        return xx * (yy * zz - yz * yz) - xy * (xy * zz - yz * xz) + xz * (xy * yz - yy * xz);
    };
    CHECK_THAT(trace(after), WithinRel(trace(before), kRel));
    CHECK_THAT(minors(after), WithinRel(minors(before), kRel));
    CHECK_THAT(determinant(after), WithinRel(determinant(before), kRel));
}

TEST_CASE("MassProperties_LeaveATensorsComponentsAloneUnderAPureTranslation",
          "[features][mass]") {
    // transformed(InertiaTensor, ...) is about a MATERIAL POINT, so a translation
    // must move the reference point and leave all six components untouched. This is
    // the contract that makes the function usable for any rigid motion rather than
    // rotations only, and the one a reader would most likely assume wrongly.
    BoxPart part{20_mm, 30_mm, 50_mm};
    const InertiaTensor centroidal = part.requireProperties().aboutCentreOfMass;
    const RigidTransform3D slide =
        RigidTransform3D::translation(Translation3D{40_mm, -70_mm, 5_mm});
    const InertiaTensor moved = features::transformed(centroidal, slide);

    CHECK(kgMm2(moved.xx) == kgMm2(centroidal.xx));
    CHECK(kgMm2(moved.yy) == kgMm2(centroidal.yy));
    CHECK(kgMm2(moved.zz) == kgMm2(centroidal.zz));
    CHECK(kgMm2(moved.xy) == kgMm2(centroidal.xy));
    CHECK(kgMm2(moved.xz) == kgMm2(centroidal.xz));
    CHECK(kgMm2(moved.yz) == kgMm2(centroidal.yz));
    // Only the reference point moved: (10, 15, 25) + (40, -70, 5).
    checkPoint(moved.about, 50.0, -55.0, 30.0);
    CHECK_FALSE(moved.about == centroidal.about);
}

TEST_CASE("MassProperties_MirrorTheInertiaTensorForAReflection", "[features][mass]") {
    // A reflection is orthogonal too, so A I A^T holds and no separate path is
    // needed. Mirroring across the YZ plane negates x, so the products that carry
    // one x factor change sign and nothing else moves.
    BoxPart part{20_mm, 30_mm, 50_mm};
    const PartMassProperties properties = part.requireProperties();
    const InertiaTensor origin = properties.aboutOrigin;
    const RigidTransform3D mirror =
        RigidTransform3D::reflection(Point3D{}, Direction3D::unitX());
    const InertiaTensor mirrored = features::transformed(origin, mirror);

    CHECK_THAT(kgMm2(mirrored.xx), WithinRel(kgMm2(origin.xx), kRel));
    CHECK_THAT(kgMm2(mirrored.yy), WithinRel(kgMm2(origin.yy), kRel));
    CHECK_THAT(kgMm2(mirrored.zz), WithinRel(kgMm2(origin.zz), kRel));
    CHECK_THAT(kgMm2(mirrored.xy), WithinRel(-kgMm2(origin.xy), kRel));
    CHECK_THAT(kgMm2(mirrored.xz), WithinRel(-kgMm2(origin.xz), kRel));
    CHECK_THAT(kgMm2(mirrored.yz), WithinRel(kgMm2(origin.yz), kRel));
}

// --- the parallel-axis theorem ----------------------------------------------

TEST_CASE("MassProperties_ShiftFromTheCentroidByHuygensTheorem", "[features][mass]") {
    // shiftedFromCentroid() on its own, against the reference fixture's hand
    // values, and then back again: shifting to the origin and shifting that result
    // back is not what the function does, so the round trip is made by shifting
    // the CENTROIDAL tensor to two different points and checking each.
    InertiaTensor centroidal;
    centroidal.xx = 68.0 * units::kg_mm2;
    centroidal.yy = 58.0 * units::kg_mm2;
    centroidal.zz = 26.0 * units::kg_mm2;
    centroidal.about = Point3D{10_mm, 15_mm, 25_mm};
    const Mass mass = 0.24 * units::kg;

    const InertiaTensor origin = features::shiftedFromCentroid(centroidal, mass, Point3D{});
    checkTensor(origin, 272.0, 232.0, 104.0, -36.0, -60.0, -90.0);
    CHECK(origin.about == Point3D{});

    // Shifting to the centroid itself must return the centroidal tensor exactly:
    // d = 0, so every added term is zero and the result is bit-identical.
    const InertiaTensor same =
        features::shiftedFromCentroid(centroidal, mass, centroidal.about);
    CHECK(same == centroidal);

    // The moment about any point is at least the moment about the centroid, which
    // is the theorem's content and holds for every point.
    const InertiaTensor far =
        features::shiftedFromCentroid(centroidal, mass, Point3D{-40_mm, 7_mm, 300_mm});
    CHECK(kgMm2(far.xx) > kgMm2(centroidal.xx));
    CHECK(kgMm2(far.yy) > kgMm2(centroidal.yy));
    CHECK(kgMm2(far.zz) > kgMm2(centroidal.zz));
}

// --- density ----------------------------------------------------------------

TEST_CASE("MassProperties_ScaleLinearlyWithDensityAndNotAtAllInVolume",
          "[features][mass]") {
    // Doubling the density doubles the mass and every moment, and moves neither
    // the volume nor the centre of mass. A mass that did not follow its density, or
    // a centroid that did, would both be caught here.
    BoxPart light{20_mm, 30_mm, 50_mm, Density::fromSi(8000.0)};
    BoxPart heavy{20_mm, 30_mm, 50_mm, Density::fromSi(16000.0)};
    const PartMassProperties a = light.requireProperties();
    const PartMassProperties b = heavy.requireProperties();

    CHECK_THAT(b.volume.in(units::mm3), WithinRel(a.volume.in(units::mm3), kRel));
    checkPoint(b.centreOfMass, 10.0, 15.0, 25.0);
    CHECK_THAT(b.mass.in(units::kg), WithinRel(2.0 * a.mass.in(units::kg), kRel));
    CHECK_THAT(kgMm2(b.aboutCentreOfMass.xx), WithinRel(2.0 * kgMm2(a.aboutCentreOfMass.xx), kRel));
    CHECK_THAT(kgMm2(b.aboutOrigin.yz), WithinRel(2.0 * kgMm2(a.aboutOrigin.yz), kRel));
    CHECK_THAT(b.mass.in(units::kg), WithinRel(0.48, kRel));
}

TEST_CASE("MassProperties_FollowAnEditedDensityWithNoRegeneration", "[features][mass]") {
    // Mass is derived, never stored (ADR-026), so changing the density changes the
    // answer immediately -- there is no cache to invalidate and no regeneration
    // needed, because the geometry did not move. If a value were being cached this
    // would return the old mass.
    BoxPart part{20_mm, 30_mm, 50_mm};
    CHECK_THAT(part.requireProperties().mass.in(units::kg), WithinRel(0.24, kRel));

    materials::MechanicalProperties properties;
    properties.density = materials::MaterialProperty<Density>::known(Density::fromSi(2700.0));
    REQUIRE(features::setMaterialMechanical(part.document, part.material, properties));
    // 2700 x 3e-5 = 0.081 kg
    CHECK_THAT(part.requireProperties().mass.in(units::kg), WithinRel(0.081, kRel));
    CHECK_THAT(kgMm2(part.requireProperties().aboutCentreOfMass.xx),
               WithinRel(0.081 * 850.0 / 3.0, kRel));
}

TEST_CASE("MassProperties_FollowAChangedGeometryAfterRegeneration", "[features][mass]") {
    // The other half: the geometry moves, the mass follows. The depth is edited on
    // the feature, so a regeneration is needed, and the point of the test is that
    // the new volume reaches the mass rather than a remembered one.
    BoxPart part{20_mm, 30_mm, 50_mm};
    CHECK_THAT(part.requireProperties().mass.in(units::kg), WithinRel(0.24, kRel));

    const auto* extrude = part.document.findObjectAs<features::ExtrudeFeature>(part.feature);
    REQUIRE(extrude != nullptr);
    features::ExtrudeDefinition definition = extrude->definition();
    definition.depth = 100_mm;
    REQUIRE(part.document.modifyObject<features::ExtrudeFeature>(
        part.feature, [&](features::ExtrudeFeature& f) { return f.setDefinition(definition); }));
    requireReport(part.regenerator, part.document);

    // 20 x 30 x 100 = 60000 mm^3, so 0.48 kg, and the centroid rises to z = 50.
    const PartMassProperties doubled = part.requireProperties();
    CHECK_THAT(doubled.volume.in(units::mm3), WithinRel(60000.0, kRel));
    CHECK_THAT(doubled.mass.in(units::kg), WithinRel(0.48, kRel));
    checkPoint(doubled.centreOfMass, 10.0, 15.0, 50.0);
    // xx = m (b^2 + c^2)/12 = 0.48 (900 + 10000)/12 = 0.48 x 908.3333... = 436
    CHECK_THAT(kgMm2(doubled.aboutCentreOfMass.xx), WithinRel(0.48 * 10900.0 / 12.0, kRel));
}

// --- failure paths ----------------------------------------------------------

TEST_CASE("MassProperties_FailWithADiagnosticWhenNoMaterialIsAssigned", "[features][mass]") {
    // Never a mass of zero for a part whose material was never chosen (ADR-026).
    Document document{"Part"};
    features::Regenerator regenerator;
    auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
    addRectangle(*sketch, 0_mm, 0_mm, 20_mm, 30_mm);
    const ObjectId profile = require(document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = 50_mm});
    REQUIRE(extrude.has_value());
    const ObjectId feature = require(document.addObject(std::move(*extrude)));
    requireReport(regenerator, document);

    const Result<PartMassProperties> properties =
        features::partMassProperties(document, regenerator, feature);
    REQUIRE_FALSE(properties.has_value());
    CHECK(properties.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(properties.error().message, ContainsSubstring("no material"));
}

TEST_CASE("MassProperties_FailDifferentlyWhenTheAssignedMaterialWasDeleted",
          "[features][mass]") {
    // A DISTINCT diagnostic from "nothing was assigned", because the user needs to
    // do something different about it (P15-ASSIGN-001's unresolved state).
    BoxPart part{20_mm, 30_mm, 50_mm};
    REQUIRE(part.properties().has_value());
    REQUIRE(features::removeMaterial(part.document, part.material));

    const Result<PartMassProperties> properties = part.properties();
    REQUIRE_FALSE(properties.has_value());
    CHECK_THAT(properties.error().message, ContainsSubstring("no such material"));
    // And it names the ID that is dangling, so the user can tell which one went.
    CHECK_THAT(properties.error().message, ContainsSubstring("material:"));
}

TEST_CASE("MassProperties_FailWithADiagnosticWhenTheDensityIsUnknown", "[features][mass]") {
    // A material with every other property and NO density. unknown is not zero, and
    // a mass of zero is a physically meaningful answer that must not be returned
    // for a question nobody answered (ADR-027).
    Document document{"Part"};
    features::Regenerator regenerator;
    auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
    addRectangle(*sketch, 0_mm, 0_mm, 20_mm, 30_mm);
    const ObjectId profile = require(document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = 50_mm});
    REQUIRE(extrude.has_value());
    const ObjectId feature = require(document.addObject(std::move(*extrude)));

    MaterialDefinition definition;
    definition.designation = "Unmeasured";
    definition.mechanical.youngsModulus =
        materials::MaterialProperty<ElasticModulus>::known(210_GPa);
    const Result<MaterialId> id = features::createMaterial(document, "Unmeasured", definition);
    REQUIRE(id);
    REQUIRE(features::assignMaterial(document, *id));
    requireReport(regenerator, document);

    const Result<PartMassProperties> properties =
        features::partMassProperties(document, regenerator, feature);
    REQUIRE_FALSE(properties.has_value());
    CHECK(properties.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(properties.error().message, ContainsSubstring("no density"));
    CHECK_THAT(properties.error().message, ContainsSubstring("Unmeasured"));
}

TEST_CASE("MassProperties_GiveThreeDistinctDiagnosticsForTheThreeMaterialFaults",
          "[features][mass]") {
    // The three must not collapse into one message. Asserted as a set, in one test,
    // so that making two of them identical fails here rather than passing three
    // tests that each only look at its own.
    const auto messageFor = [](bool assign, bool density) {
        Document document{"Part"};
        features::Regenerator regenerator;
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        addRectangle(*sketch, 0_mm, 0_mm, 20_mm, 30_mm);
        const ObjectId profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = 50_mm});
        REQUIRE(extrude.has_value());
        const ObjectId feature = require(document.addObject(std::move(*extrude)));
        MaterialDefinition definition;
        definition.designation = "Steel";
        if (density) {
            definition.mechanical.density =
                materials::MaterialProperty<Density>::known(Density::fromSi(8000.0));
        }
        const Result<MaterialId> id = features::createMaterial(document, "Steel", definition);
        REQUIRE(id);
        if (assign) {
            REQUIRE(features::assignMaterial(document, *id));
        }
        requireReport(regenerator, document);
        if (assign && density) {
            REQUIRE(features::removeMaterial(document, *id));
        }
        const Result<PartMassProperties> properties =
            features::partMassProperties(document, regenerator, feature);
        REQUIRE_FALSE(properties.has_value());
        return std::string{properties.error().message};
    };
    const std::string unassigned = messageFor(false, true);
    const std::string deleted = messageFor(true, true);
    const std::string noDensity = messageFor(true, false);
    CHECK(unassigned != deleted);
    CHECK(unassigned != noDensity);
    CHECK(deleted != noDensity);
    // And none of them is empty or a bare code.
    CHECK(unassigned.size() > 20);
    CHECK(deleted.size() > 20);
    CHECK(noDensity.size() > 20);
}

TEST_CASE("MassProperties_FailForAnObjectThatProducesNoBody", "[features][mass]") {
    // A sketch is a perfectly good object with no body, and the diagnostic says so
    // rather than telling the user to regenerate something that is already built.
    BoxPart part{20_mm, 30_mm, 50_mm};
    const DocumentObject* profile = part.document.findObjectByName("Profile");
    REQUIRE(profile != nullptr);
    const ObjectId sketch = profile->id();
    const Result<PartMassProperties> properties =
        features::partMassProperties(part.document, part.regenerator, sketch);
    REQUIRE_FALSE(properties.has_value());
    CHECK_THAT(properties.error().message, ContainsSubstring("no body"));
    CHECK_THAT(properties.error().message, ContainsSubstring("Profile"));
}

TEST_CASE("MassProperties_FailForAnObjectThatDoesNotExist", "[features][mass]") {
    BoxPart part{20_mm, 30_mm, 50_mm};
    const Result<PartMassProperties> properties =
        features::partMassProperties(part.document, part.regenerator, ObjectId::fromValue(9999));
    REQUIRE_FALSE(properties.has_value());
    CHECK(properties.error().code == ErrorCode::NotFound);
}

TEST_CASE("MassProperties_FailAndReportNoBodyWhenRegenerationFails", "[features][mass]") {
    // A REAL failed regeneration, reached by over-constraining the sketch the
    // extrude is built from: two different lengths for one line, which the solver
    // reports as INCONSISTENT, blocking the extrude.
    //
    // The first draft of this test tried to reach the same state with a zero depth
    // and silently succeeded without reaching it, because setDefinition rejects a
    // zero depth outright. It proved nothing for a whole run. The assertions below
    // therefore START by establishing that the failure actually happened.
    BoxPart part{20_mm, 30_mm, 50_mm};
    REQUIRE(part.properties().has_value());

    REQUIRE(part.document
                .modifyObject<sketch::Sketch>(part.profile,
                                              [&](sketch::Sketch& s) -> Result<bool> {
                                                  const Result<ConstraintId> first =
                                                      s.addDistance(part.lines[0], 20_mm);
                                                  if (!first) {
                                                      return std::unexpected(first.error());
                                                  }
                                                  const Result<ConstraintId> second =
                                                      s.addDistance(part.lines[0], 25_mm);
                                                  if (!second) {
                                                      return std::unexpected(second.error());
                                                  }
                                                  return true;
                                              })
                .has_value());
    const features::RegenerationReport report = requireReport(part.regenerator, part.document);
    // The failure is real, and it reached the feature.
    REQUIRE_FALSE(report.succeeded());
    REQUIRE((part.regenerator.state(part.feature) == features::NodeState::Blocked ||
             part.regenerator.state(part.feature) == features::NodeState::Failed));

    // THE INVARIANT THIS RESTS ON: the regenerator drops the body rather than
    // keeping the last good one, so there is no stale geometry to compute a mass
    // from. Asserted here, at the point that depends on it, because
    // partMassProperties() has a second guard that is unreachable while this holds
    // and would become the live one if it ever stopped.
    CHECK(part.regenerator.body(part.feature) == nullptr);

    const Result<PartMassProperties> properties = part.properties();
    REQUIRE_FALSE(properties.has_value());
    CHECK(properties.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(properties.error().message, ContainsSubstring("no body"));
    // The diagnostic says WHY there is no body, so the user is not told to
    // regenerate something that did regenerate and failed.
    CHECK_THAT(properties.error().message, ContainsSubstring("blocked"));
    // And never a mass of zero.
    CHECK_FALSE(properties.has_value());
}

// --- the carried regeneration defect ----------------------------------------

TEST_CASE("MassProperties_RefuseToAnswerUnderAConfigurationThatOverridesAParameter",
          "[features][mass]") {
    // A CARRIED DEFECT, guarded rather than pinned. A configuration override
    // changes a parameter's effective value without changing the parameter object,
    // so features::Regenerator does not mark the features that read it dirty and
    // their bodies remain the base configuration's. Mass is density x volume, so
    // answering here would report a wrong mass as a right one.
    //
    // This test asserts the REFUSAL, not the stale number. It deliberately does not
    // record what the stale volume is, because that would turn a defect into a
    // contract. When the regeneration defect is fixed, this test is deleted with the
    // guard it covers, and a test that the mass FOLLOWS the configuration replaces
    // it.
    BoxPart part{20_mm, 30_mm, 50_mm};
    REQUIRE(part.properties().has_value());

    const Result<ParameterId> depth =
        part.document.createParameter("depth", 50_mm, units::mm);
    REQUIRE(depth);
    const Result<ConfigurationId> tall = part.document.createConfiguration("Tall");
    REQUIRE(tall);
    REQUIRE(part.document.setConfigurationOverride(*tall, *depth, 100_mm));
    REQUIRE(part.document.setActiveConfiguration(*tall));

    const Result<PartMassProperties> properties = part.properties();
    REQUIRE_FALSE(properties.has_value());
    CHECK(properties.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(properties.error().message, ContainsSubstring("Tall"));
    CHECK_THAT(properties.error().message, ContainsSubstring("does not yet rebuild"));

    // And the base configuration answers again, unchanged: the guard is about the
    // active configuration, not a latch that poisons the document.
    REQUIRE(part.document.setActiveConfiguration(std::nullopt));
    const Result<PartMassProperties> again = part.properties();
    REQUIRE(again.has_value());
    CHECK_THAT(again->mass.in(units::kg), WithinRel(0.24, kRel));
}

TEST_CASE("MassProperties_AnswerUnderAConfigurationThatOverridesNoParameter",
          "[features][mass]") {
    // The guard is narrow on purpose: a configuration that overrides no parameter
    // cannot make the geometry stale, so it must not be refused. Over-refusing
    // would be a defect of its own.
    BoxPart part{20_mm, 30_mm, 50_mm};
    const Result<ConfigurationId> plain = part.document.createConfiguration("Plain");
    REQUIRE(plain);
    REQUIRE(part.document.setActiveConfiguration(*plain));

    const Result<PartMassProperties> properties = part.properties();
    REQUIRE(properties.has_value());
    CHECK_THAT(properties->mass.in(units::kg), WithinRel(0.24, kRel));
}

// --- nothing is persisted ---------------------------------------------------

TEST_CASE("MassProperties_AreRecomputedRatherThanRememberedAcrossCalls",
          "[features][mass][determinism]") {
    // Bit-for-bit repeatable, and repeatable because it is recomputed: the same
    // inputs give the same bits, and the previous section showed that changed
    // inputs give changed values. Together those say there is no cache.
    BoxPart part{20_mm, 30_mm, 50_mm};
    const PartMassProperties first = part.requireProperties();
    for (int pass = 0; pass < 8; ++pass) {
        CHECK(part.requireProperties() == first);
    }
}
