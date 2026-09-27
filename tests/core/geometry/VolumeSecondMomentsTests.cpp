#include "GeometryTestSupport.hpp"

#include <bettercad/core/geometry/Booleans.hpp>
#include <bettercad/core/geometry/Primitives.hpp>
#include <bettercad/core/geometry/Profile.hpp>
#include <bettercad/core/geometry/Sweeps.hpp>
#include <bettercad/core/units/Units.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <numbers>
#include <type_traits>
#include <utility>

using namespace bettercad;
using namespace bettercad::geometry;
using namespace bettercad::literals;
using bettercad::test::checkPoint;
using bettercad::test::kRelTight;
using bettercad::test::requireProperties;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// P15-MASS-001, first gate: the reference point, the units and the tensor sign
// convention of Body::centroidalVolumeSecondMoments(), settled against closed-form
// integrals BEFORE the mass layer was built on them. Every expected value below is
// a hand-evaluated integral, shown as the integral it came from. None was produced
// by BetterCAD.
//
// Two of the three were open questions the kernel's own documentation does not
// answer, and this file is what answered them:
//
//   REFERENCE POINT -- GProp_GProps takes a location and describes it as the point
//   "used for inertia accumulation", while MatrixOfInertia() is documented as
//   being "in the central coordinate system". Measured: the result is about the
//   body's CENTROID, whatever location the framework was given. A first draft of
//   this file expected the moments of a corner box about the ORIGIN and failed,
//   which is the only reason the adapter does not claim the wrong frame.
//
//   SIGN -- the same header calls the off-diagonals "the products of inertia",
//   which is the name of the POSITIVE convention, while laying them out as an
//   inertia matrix, which is the negated one. No symmetric body distinguishes
//   them: about its own centroid a box, a cylinder and a sphere all have three
//   zeros there. The staircase below is the fixture that does.

namespace {

constexpr double pi = std::numbers::pi;
/// Off-diagonals that vanish by symmetry, in mm^5. These bodies are ~10 mm, so
/// their mm^5 magnitudes run to ~1e4; 1e-9 mm^5 is rounding, not cancellation.
constexpr double kZeroMm5 = 1e-9;

Body requireBody(Result<Body> body) {
    REQUIRE(body.has_value());
    return std::move(*body);
}

VolumeSecondMoments requireMoments(const Body& body) {
    const Result<VolumeSecondMoments> moments = body.centroidalVolumeSecondMoments();
    REQUIRE(moments.has_value());
    return *moments;
}

double mm5(VolumeSecondMoment value) {
    return value.in(units::mm5);
}

} // namespace

TEST_CASE("Centroidal volume second moments match the hand-computed integrals of a box",
          "[geometry][mass]") {
    // The box [0,a] x [0,b] x [0,c], a = 2, b = 3, c = 5 mm: three distinct edge
    // lengths, so the three diagonal components differ and none can be confused
    // with another. A corner sits on the origin, which is what makes this fixture
    // discriminating about the REFERENCE POINT -- the centroidal and the
    // about-origin values differ by a factor of four here, not by rounding.
    //
    //   V      = abc                                    = 30       mm^3
    //   centre = (a/2, b/2, c/2)                        = (1, 1.5, 2.5) mm
    //   xx     = V (b^2 + c^2) / 12 = 30 x 34/12        = 85       mm^5
    //   yy     = V (a^2 + c^2) / 12 = 30 x 29/12        = 72.5     mm^5
    //   zz     = V (a^2 + b^2) / 12 = 30 x 13/12        = 32.5     mm^5
    //
    // About the ORIGIN the same box would read 340, 290 and 130, with off-diagonals
    // -45, -75 and -112.5. Those are the values the first draft of this test
    // expected; the kernel returned the three below instead.
    const Body box = requireBody(makeBox(2_mm, 3_mm, 5_mm));
    const MassProperties properties = requireProperties(box);
    CHECK_THAT(properties.volume.in(units::mm3), WithinRel(30.0, kRelTight));
    checkPoint(properties.centerOfMass, 1.0, 1.5, 2.5);

    const VolumeSecondMoments moments = requireMoments(box);
    CHECK_THAT(mm5(moments.xx), WithinRel(85.0, kRelTight));
    CHECK_THAT(mm5(moments.yy), WithinRel(72.5, kRelTight));
    CHECK_THAT(mm5(moments.zz), WithinRel(32.5, kRelTight));
    // Zero about a box's own centroid by symmetry -- which is exactly why this
    // fixture says nothing about the sign convention. See the staircase.
    CHECK_THAT(mm5(moments.xy), WithinAbs(0.0, kZeroMm5));
    CHECK_THAT(mm5(moments.xz), WithinAbs(0.0, kZeroMm5));
    CHECK_THAT(mm5(moments.yz), WithinAbs(0.0, kZeroMm5));
}

TEST_CASE("Centroidal volume second moments are about the centroid, not the origin",
          "[geometry][mass]") {
    // The same box, translated 100 mm along each axis. About the centroid the
    // moments are invariant under translation, so they must not move at all; about
    // the origin they would grow by V x 2 x 100^2 ~ 6e5, four orders of magnitude.
    // This is the cheapest possible statement that the reference point travels with
    // the body, and it is a statement no single-position fixture can make.
    const Body atOrigin = requireBody(makeBox(2_mm, 3_mm, 5_mm));
    const Body moved = requireBody(makeBox(Point3D{100_mm, 100_mm, 100_mm}, 2_mm, 3_mm, 5_mm));

    const VolumeSecondMoments here = requireMoments(atOrigin);
    const VolumeSecondMoments there = requireMoments(moved);
    CHECK_THAT(mm5(there.xx), WithinRel(mm5(here.xx), kRelTight));
    CHECK_THAT(mm5(there.yy), WithinRel(mm5(here.yy), kRelTight));
    CHECK_THAT(mm5(there.zz), WithinRel(mm5(here.zz), kRelTight));
    CHECK_THAT(mm5(there.xx), WithinRel(85.0, kRelTight));
}

TEST_CASE("Centroidal volume second moments are millimetres to the fifth, not metres",
          "[geometry][mass][units]") {
    // A 10 mm cube about its own centroid: xx = V (a^2 + a^2) / 12 = 1000 x 200/12.
    // Reading the kernel's number as m^5 fails here by fifteen orders of magnitude
    // rather than subtly, and reading it through any other power of the length
    // scale fails too.
    const Body cube = requireBody(makeBox(10_mm, 10_mm, 10_mm));
    const VolumeSecondMoments moments = requireMoments(cube);
    const double expectedMm5 = 1000.0 * 200.0 / 12.0;
    CHECK_THAT(mm5(moments.xx), WithinRel(expectedMm5, kRelTight));
    CHECK_THAT(moments.xx.in(units::m5), WithinRel(expectedMm5 * 1.0e-15, kRelTight));
}

TEST_CASE("Centroidal volume second moments of a staircase fix the tensor sign convention",
          "[geometry][mass]") {
    // THE FIXTURE THAT SETTLES THE SIGN. Three fused boxes, each sharing a whole
    // face with the next so the union is a valid manifold solid, arranged so that
    // the body is symmetric about no plane through its centroid. All three products
    // are therefore non-zero, and their signs follow from where the material sits
    // -- no rotation is involved, so there is no direction convention to get wrong
    // and confuse with the sign under test.
    //
    //   B1 = [0,4] x [0,2] x [0,1]   V = 8   centre (2,   1,   0.5)
    //   B2 = [0,1] x [0,2] x [1,3]   V = 4   centre (0.5, 1,   2  )   shares z = 1
    //   B3 = [0,1] x [2,4] x [1,3]   V = 4   centre (0.5, 3,   2  )   shares y = 2
    //
    //   V = 16,  centroid = ( 20/16, 24/16, 20/16 ) = (1.25, 1.5, 1.25) mm
    //
    // Composing by Huygens, each box contributing its own centroidal moments plus
    // V d^2 for d from its centre to the composite centroid:
    //
    //   d1 = ( 0.75, -0.5, -0.75)   d2 = (-0.75, -0.5,  0.75)
    //   d3 = (-0.75,  1.5,  0.75)
    //
    //   xx = (10/3 + 8/3 + 8/3) + (6.5  + 3.25 + 11.25) = 26/3 + 21 = 89/3
    //   yy = (34/3 + 5/3 + 5/3) + (9.0  + 4.5  +  4.5 ) = 44/3 + 18 = 98/3
    //   zz = (40/3 + 5/3 + 5/3) + (6.5  + 3.25 + 11.25) = 50/3 + 21 = 113/3
    //
    // The products, where each axis-aligned box contributes no own-centroidal
    // product and only its V dx dy term:
    //
    //   integral xy dV = 8(0.75)(-0.5) + 4(-0.75)(-0.5) + 4(-0.75)( 1.5)
    //                  = -3.0 + 1.5 - 4.5 = -6.0
    //   integral xz dV = 8(0.75)(-0.75) + 4(-0.75)(0.75) + 4(-0.75)(0.75)
    //                  = -4.5 - 2.25 - 2.25 = -9.0
    //   integral yz dV = 8(-0.5)(-0.75) + 4(-0.5)(0.75) + 4( 1.5)(0.75)
    //                  = 3.0 - 1.5 + 4.5 = +6.0
    //
    // So under the TENSOR convention, which negates them:
    //
    //   xy = +6,  xz = +9,  yz = -6   mm^5
    //
    // and under the positive-products convention they would be -6, -9 and +6. The
    // two differ in the sign of every component, and xy and yz differ from each
    // other in sign as well, so a swap between those two is caught too.
    const Body b1 = requireBody(makeBox(Point3D{0_mm, 0_mm, 0_mm}, 4_mm, 2_mm, 1_mm));
    const Body b2 = requireBody(makeBox(Point3D{0_mm, 0_mm, 1_mm}, 1_mm, 2_mm, 2_mm));
    const Body b3 = requireBody(makeBox(Point3D{0_mm, 2_mm, 1_mm}, 1_mm, 2_mm, 2_mm));
    const Body staircase = requireBody(booleanUnion(requireBody(booleanUnion(b1, b2)), b3));

    // The fuse produced what the arithmetic above assumes: one solid of volume 16
    // with that centroid. Without this the moment values would be checked against
    // the wrong body.
    const MassProperties properties = requireProperties(staircase);
    REQUIRE(staircase.topology().solids == 1);
    CHECK_THAT(properties.volume.in(units::mm3), WithinRel(16.0, kRelTight));
    checkPoint(properties.centerOfMass, 1.25, 1.5, 1.25);

    const VolumeSecondMoments moments = requireMoments(staircase);
    CHECK_THAT(mm5(moments.xx), WithinRel(89.0 / 3.0, kRelTight));
    CHECK_THAT(mm5(moments.yy), WithinRel(98.0 / 3.0, kRelTight));
    CHECK_THAT(mm5(moments.zz), WithinRel(113.0 / 3.0, kRelTight));

    CHECK_THAT(mm5(moments.xy), WithinRel(6.0, kRelTight));
    CHECK_THAT(mm5(moments.xz), WithinRel(9.0, kRelTight));
    CHECK_THAT(mm5(moments.yz), WithinRel(-6.0, kRelTight));
    // Asserted again as signs, because this is the whole point of the fixture and
    // a relative-tolerance match on a negative number is easy to misread.
    CHECK(mm5(moments.xy) > 0.0);
    CHECK(mm5(moments.xz) > 0.0);
    CHECK(mm5(moments.yz) < 0.0);
}

TEST_CASE("Centroidal volume second moments of a two-solid body are the combined bodys",
          "[geometry][mass]") {
    // A body may hold more than one solid, and then the moments must be those of
    // the WHOLE body about the WHOLE body's centroid -- not of one solid, and not
    // of each about its own centre. Nothing in the diagonal of a single-solid
    // fixture would reveal the difference, so it is checked here on two disjoint
    // 2 mm cubes.
    //
    //   B1 = [0,2]^3 offset to (0,0,0)   V = 8   centre (1, 1, 1)
    //   B2 = [10,12] x [6,8] x [0,2]     V = 8   centre (11, 7, 1)
    //   V = 16,  centroid = (6, 4, 1) mm
    //
    // Each cube's own centroidal moment is V(b^2+c^2)/12 = 8 x 8/12 = 16/3 on every
    // axis, and d1 = (-5,-3,0), d2 = (5,3,0):
    //
    //   xx = 32/3 + 8(9) + 8(9)   = 32/3 + 144
    //   yy = 32/3 + 8(25) + 8(25) = 32/3 + 400
    //   zz = 32/3 + 8(34) + 8(34) = 32/3 + 544
    //   xy = -[8(-5)(-3) + 8(5)(3)] = -240
    //   xz = yz = 0
    const Body first = requireBody(makeBox(Point3D{0_mm, 0_mm, 0_mm}, 2_mm, 2_mm, 2_mm));
    const Body second = requireBody(makeBox(Point3D{10_mm, 6_mm, 0_mm}, 2_mm, 2_mm, 2_mm));
    const Body both = requireBody(booleanUnion(first, second));
    // The fixture is only about two solids if there ARE two solids.
    REQUIRE(both.topology().solids == 2);

    const MassProperties properties = requireProperties(both);
    CHECK_THAT(properties.volume.in(units::mm3), WithinRel(16.0, kRelTight));
    checkPoint(properties.centerOfMass, 6.0, 4.0, 1.0);

    const VolumeSecondMoments moments = requireMoments(both);
    CHECK_THAT(mm5(moments.xx), WithinRel(32.0 / 3.0 + 144.0, kRelTight));
    CHECK_THAT(mm5(moments.yy), WithinRel(32.0 / 3.0 + 400.0, kRelTight));
    CHECK_THAT(mm5(moments.zz), WithinRel(32.0 / 3.0 + 544.0, kRelTight));
    CHECK_THAT(mm5(moments.xy), WithinRel(-240.0, kRelTight));
    CHECK_THAT(mm5(moments.xz), WithinAbs(0.0, kZeroMm5));
    CHECK_THAT(mm5(moments.yz), WithinAbs(0.0, kZeroMm5));
    // One cube alone would read 16/3 on every axis with no product at all, which is
    // what this rules out.
    CHECK(mm5(moments.xx) > 100.0);
}

TEST_CASE("Centroidal volume second moments of a cylinder match the closed-form integrals",
          "[geometry][mass]") {
    // Cylinder r = 2, h = 6 about its own centroid:
    //
    //   V  = pi r^2 h                  = 24 pi   mm^3
    //   zz = V r^2 / 2                 = 48 pi   mm^5
    //   xx = yy = V (r^2/4 + h^2/12)
    //          = V (1 + 3) = 4 V       = 96 pi   mm^5
    //
    // A curved body, and one whose two transverse components must come out equal
    // without being computed as equal.
    const Body cylinder = requireBody(makeCylinder(2_mm, 6_mm));
    CHECK_THAT(requireProperties(cylinder).volume.in(units::mm3), WithinRel(24.0 * pi, kRelTight));

    const VolumeSecondMoments moments = requireMoments(cylinder);
    CHECK_THAT(mm5(moments.zz), WithinRel(48.0 * pi, kRelTight));
    CHECK_THAT(mm5(moments.xx), WithinRel(96.0 * pi, kRelTight));
    CHECK_THAT(mm5(moments.yy), WithinRel(96.0 * pi, kRelTight));
    CHECK_THAT(mm5(moments.xy), WithinAbs(0.0, kZeroMm5));
    CHECK_THAT(mm5(moments.xz), WithinAbs(0.0, kZeroMm5));
    CHECK_THAT(mm5(moments.yz), WithinAbs(0.0, kZeroMm5));
}

TEST_CASE("Centroidal volume second moments of a sphere match 8 pi r^5 / 15",
          "[geometry][mass]") {
    // A sphere about its centre: xx = yy = zz = (2/5) V r^2 = 8 pi r^5 / 15.
    // r = 3 gives 8 pi x 243 / 15. Fully curved, so this is not a second statement
    // of the box: it says the kernel reaches the same tolerance when no face is
    // planar.
    const Body sphere = requireBody(makeSphere(3_mm));
    const VolumeSecondMoments moments = requireMoments(sphere);
    const double expected = 8.0 * pi * 243.0 / 15.0;
    CHECK_THAT(mm5(moments.xx), WithinRel(expected, kRelTight));
    CHECK_THAT(mm5(moments.yy), WithinRel(expected, kRelTight));
    CHECK_THAT(mm5(moments.zz), WithinRel(expected, kRelTight));
    CHECK_THAT(mm5(moments.xy), WithinAbs(0.0, kZeroMm5));
}

TEST_CASE("Volume second moments carry the dimension of a second moment of volume",
          "[geometry][mass][units]") {
    static_assert(std::is_same_v<decltype(VolumeSecondMoments{}.xx), VolumeSecondMoment>);
    static_assert(std::is_same_v<VolumeSecondMoment, Quantity<dimensions::volumeSecondMoment>>);
    // The claim the whole mass layer rests on, checked by the type system rather
    // than asserted in a comment: a density times a second moment of volume IS a
    // mass moment of inertia.
    static_assert(std::is_same_v<decltype(Density{} * VolumeSecondMoment{}), MassMomentOfInertia>);
    static_assert(std::is_same_v<MassMomentOfInertia, Quantity<dimensions::massMomentOfInertia>>);
    static_assert(!std::is_same_v<VolumeSecondMoment, Volume>);
    static_assert(!std::is_same_v<VolumeSecondMoment, MassMomentOfInertia>);
    SUCCEED("the dimensional relations hold at compile time");
}

TEST_CASE("Centroidal volume second moments refuse an empty body", "[geometry][mass]") {
    const Result<VolumeSecondMoments> moments = Body{}.centroidalVolumeSecondMoments();
    REQUIRE_FALSE(moments.has_value());
    CHECK(moments.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(moments.error().message, ContainsSubstring("volume second moments"));
    CHECK_THAT(moments.error().message, ContainsSubstring("empty body"));
}

TEST_CASE("Centroidal volume second moments refuse a body with a swept-curve face",
          "[geometry][mass]") {
    // An elliptic prism. Its side is a surface of extrusion, whose volume the
    // kernel misreports (P12-SKETCH-002), which is why massProperties() integrates
    // that face itself. There is no such integration for second moments, so this
    // refuses rather than returning the kernel's number for exactly the shape class
    // the kernel is not trusted on.
    const EllipseSegment2D ellipse{Point2D{0_mm, 0_mm}, Point2D{30_mm, 0_mm}, 10_mm, true};
    const PlanarRegion region{.plane = Frame3D::xy(), .outer = ProfileLoop{{ellipse}}, .holes = {}};
    const Body prism = requireBody(makePrism(region, 0_mm, 25_mm));
    // The volume still works, so this is a refusal of second moments alone, not of
    // the body.
    CHECK(prism.massProperties().has_value());

    const Result<VolumeSecondMoments> moments = prism.centroidalVolumeSecondMoments();
    REQUIRE_FALSE(moments.has_value());
    CHECK(moments.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(moments.error().message, ContainsSubstring("swept-curve face"));
}

TEST_CASE("Centroidal volume second moments repeat bit for bit",
          "[geometry][mass][determinism]") {
    const Body box = requireBody(makeBox(2_mm, 3_mm, 5_mm));
    const VolumeSecondMoments first = requireMoments(box);
    for (int pass = 0; pass < 8; ++pass) {
        CHECK(requireMoments(box) == first);
    }
}
