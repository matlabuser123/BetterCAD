#include "GeometryTestSupport.hpp"
#include "TestHelpers.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/geometry/Booleans.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/geometry/Hole.hpp>
#include <bettercad/core/geometry/Primitives.hpp>
#include <bettercad/core/geometry/Profile.hpp>
#include <bettercad/core/geometry/Sweeps.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <numbers>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::geometry;
using namespace bettercad::literals;
using bettercad::test::errorCode;
using bettercad::test::kPositionToleranceMm;
using bettercad::test::kRelTight;
using bettercad::test::requireProperties;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr double pi = std::numbers::pi;

Body block() {
    auto box = makeBox(100_mm, 50_mm, 20_mm);
    REQUIRE(box.has_value());
    return *box;
}

FaceSignature top(double z = 20) {
    return planeSignature(Point3D{0_mm, 0_mm, z * units::mm}, Direction3D::unitZ());
}
FaceSignature bottom(double z = 0) {
    return planeSignature(Point3D{0_mm, 0_mm, z * units::mm}, Direction3D::unitZ().reversed());
}

Point2D at(double u, double v) {
    return Point2D{u * units::mm, v * units::mm};
}

HoleRequest through(const FaceSignature& face, Point2D centre, Length diameter) {
    return {.face = face, .center = centre, .diameter = diameter};
}

HoleRequest blind(const FaceSignature& face, Point2D centre, Length diameter, Length depth) {
    return {.face = face, .center = centre, .extent = HoleExtent::Blind, .diameter = diameter, .depth = depth};
}

Body requireHole(const Body& body, const HoleRequest& request) {
    auto result = cutHole(body, request);
    if (!result) {
        FAIL(result.error().message);
    }
    CHECK(result->isValid());
    CHECK(result->topology().solids == 1);
    return *result;
}

std::string refusal(const Body& body, const HoleRequest& request, ErrorCode code = ErrorCode::FailedPrecondition) {
    const auto result = cutHole(body, request);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == code);
    return result.error().message;
}

std::size_t facesOn(const Body& body, const FaceSignature& signature) {
    const auto found = findFaces(body, signature);
    REQUIRE(found.has_value());
    return found->size();
}

std::size_t circles(const Body& body, double x, double y, double z, double r) {
    const auto circle = circleSignature(Point3D{x * units::mm, y * units::mm, z * units::mm}, Direction3D::unitZ(),
                                        r * units::mm);
    REQUIRE(circle.has_value());
    const auto found = findEdges(body, *circle);
    REQUIRE(found.has_value());
    return found->size();
}

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

} // namespace

TEST_CASE("Faces_BoxHasSixPlanarFacesWithReferences", "[geometry][faces]") {
    const Body box = block();
    const auto faces = listFaces(box);
    REQUIRE(faces.has_value());
    REQUIRE(faces->size() == 6);
    double area = 0.0;
    for (const FaceInfo& face : *faces) {
        CHECK(face.surface == FaceSurface::Plane);
        CHECK(face.signature.has_value());
        area += face.area.in(units::mm2);
    }
    CHECK_THAT(area, WithinRel(16000.0, kRelTight));

    const auto upper = findFaces(box, top());
    REQUIRE(upper.has_value());
    REQUIRE(upper->size() == 1);
    CHECK_THAT(upper->front().area.in(units::mm2), WithinRel(5000.0, kRelTight));
    bettercad::test::checkPoint(upper->front().centroid, 50, 25, 20);
    CHECK(*upper->front().signature == top());
    // The side of the plane matters: nothing at z = 20 faces down.
    CHECK(facesOn(box, planeSignature(Point3D{0_mm, 0_mm, 20_mm}, Direction3D::unitZ().reversed())) == 0);
    CHECK(facesOn(box, bottom()) == 1);

    // A cylinder's side is curved: it cannot be referenced.
    const auto rod = makeCylinder(15_mm, 40_mm);
    REQUIRE(rod.has_value());
    const auto rodFaces = listFaces(*rod);
    REQUIRE(rodFaces.has_value());
    REQUIRE(rodFaces->size() == 3);
    const auto side = std::ranges::find_if(*rodFaces, [](const FaceInfo& f) { return f.surface == FaceSurface::Cylinder; });
    REQUIRE(side != rodFaces->end());
    CHECK_FALSE(side->signature.has_value());
    CHECK(describe(top()) == "plane through (0, 0, 20) mm facing (0, 0, 1)");
    CHECK(errorCode(validate(FaceSignature{.surface = FaceSurface::Cylinder})) == ErrorCode::InvalidArgument);
}

TEST_CASE("FaceSignature_IsCanonicalAndHasModelAlignedCoordinates", "[geometry][faces]") {
    // Any point of the plane gives the same reference.
    CHECK(planeSignature(Point3D{37_mm, 12_mm, 20_mm}, Direction3D::unitZ()) == top());
    // On the faces of an axis-aligned box, face coordinates are model coordinates.
    const auto roundTrip = [](const FaceSignature& face, const Point3D& p, double u, double v) {
        const Point2D local = faceCoordinates(face, p);
        CHECK_THAT(local.x.in(units::mm), WithinAbs(u, kPositionToleranceMm));
        CHECK_THAT(local.y.in(units::mm), WithinAbs(v, kPositionToleranceMm));
        bettercad::test::checkPoint(facePoint(face, local), p.x.in(units::mm), p.y.in(units::mm), p.z.in(units::mm));
    };
    roundTrip(top(), Point3D{30_mm, 10_mm, 20_mm}, 30, 10);
    roundTrip(bottom(), Point3D{30_mm, 10_mm, 0_mm}, 30, 10); // both sides of a plate agree
    roundTrip(planeSignature(Point3D{}, Direction3D::unitY().reversed()), Point3D{30_mm, 0_mm, 7_mm}, 30, 7);
    roundTrip(planeSignature(Point3D{100_mm, 0_mm, 0_mm}, Direction3D::unitX()), Point3D{100_mm, 10_mm, 7_mm}, 10, 7);
}

TEST_CASE("Hole_ThroughHoleMatchesAnalyticVolume", "[geometry][hole]") {
    const Body box = block();
    const Body holed = requireHole(box, through(top(), at(50, 25), 10_mm));
    const MassProperties props = requireProperties(holed);
    // V = L W H - pi r^2 H with r = 5, H = 20.
    CHECK_THAT(props.volume.in(units::mm3), WithinRel(100000.0 - pi * 25.0 * 20.0, kRelTight));
    // Top and bottom lose a disc each; the bore adds 2 pi r H.
    CHECK_THAT(props.surfaceArea.in(units::mm2), WithinRel(16000.0 - 2.0 * pi * 25.0 + 2.0 * pi * 5.0 * 20.0, kRelTight));
    CHECK(holed.topology().faces == 7);
    // Through: the bore meets both the top and the bottom face.
    CHECK(circles(holed, 50, 25, 20, 5) == 1);
    CHECK(circles(holed, 50, 25, 0, 5) == 1);
    const auto holedBox = holed.boundingBox().value();
    CHECK_THAT(holedBox.max.x.in(units::mm), WithinAbs(100.0, kPositionToleranceMm));
    CHECK_THAT(requireProperties(box).volume.in(units::mm3), WithinRel(100000.0, kRelTight)); // input untouched
}

TEST_CASE("Hole_BlindHoleMatchesAnalyticVolume", "[geometry][hole]") {
    const Body holed = requireHole(block(), blind(top(), at(50, 25), 10_mm, 8_mm));
    const MassProperties props = requireProperties(holed);
    // V = L W H - pi r^2 h, h = 8; a flat bottom 8 mm down.
    CHECK_THAT(props.volume.in(units::mm3), WithinRel(100000.0 - pi * 25.0 * 8.0, kRelTight));
    CHECK_THAT(props.surfaceArea.in(units::mm2), WithinRel(16000.0 + 2.0 * pi * 5.0 * 8.0, kRelTight));
    const auto floor = findFaces(holed, top(12));
    REQUIRE(floor.has_value());
    REQUIRE(floor->size() == 1);
    CHECK_THAT(floor->front().area.in(units::mm2), WithinRel(pi * 25.0, kRelTight));
    CHECK(circles(holed, 50, 25, 0, 5) == 0); // not through
}

TEST_CASE("Hole_DrillsIntoTheMaterialFromEitherSide", "[geometry][hole]") {
    // From the top face the hole goes down; from the bottom face, up.
    const Body fromTop = requireHole(block(), blind(top(), at(50, 25), 10_mm, 8_mm));
    const Body fromBelow = requireHole(block(), blind(bottom(), at(50, 25), 10_mm, 8_mm));
    CHECK_THAT(requireProperties(fromBelow).volume.in(units::mm3),
               WithinRel(requireProperties(fromTop).volume.in(units::mm3), kRelTight));
    CHECK(facesOn(fromTop, top(12)) == 1);   // floor 8 mm below the top, facing up
    CHECK(facesOn(fromBelow, bottom(8)) == 1); // ceiling 8 mm above the bottom, facing down
    CHECK(requireProperties(fromTop).centerOfMass.z < requireProperties(fromBelow).centerOfMass.z);
    // A reference to the wrong side of the top plane matches nothing.
    const HoleRequest outward = blind(planeSignature(Point3D{0_mm, 0_mm, 20_mm}, Direction3D::unitZ().reversed()),
                                      at(50, 25), 10_mm, 8_mm);
    CHECK(refusal(block(), outward, ErrorCode::NotFound) ==
          "hole: the placement face (plane through (0, 0, 20) mm facing (0, 0, -1)) matches no face of the body");
}

TEST_CASE("Hole_CounterboreMatchesAnalyticVolume", "[geometry][hole]") {
    // A 10 mm hole with an 18 mm counterbore 5 mm deep:
    // V = V0 - pi r^2 H - pi (R^2 - r^2) h.
    const HoleRequest bored{.face = top(), .center = at(50, 25), .type = HoleType::Counterbore,
                            .diameter = 10_mm, .counterboreDiameter = 18_mm, .counterboreDepth = 5_mm};
    const Body holed = requireHole(block(), bored);
    CHECK_THAT(requireProperties(holed).volume.in(units::mm3),
               WithinRel(100000.0 - pi * 25.0 * 20.0 - pi * (81.0 - 25.0) * 5.0, kRelTight));
    const auto shelf = findFaces(holed, top(15));
    REQUIRE(shelf.has_value());
    REQUIRE(shelf->size() == 1);
    CHECK_THAT(shelf->front().area.in(units::mm2), WithinRel(pi * (81.0 - 25.0), kRelTight));

    HoleRequest blindBored = bored;
    blindBored.extent = HoleExtent::Blind;
    blindBored.depth = 12_mm;
    CHECK_THAT(requireProperties(requireHole(block(), blindBored)).volume.in(units::mm3),
               WithinRel(100000.0 - pi * 25.0 * 12.0 - pi * (81.0 - 25.0) * 5.0, kRelTight));
}

TEST_CASE("Hole_CountersinkMatchesAnalyticVolume", "[geometry][hole]") {
    // A 10 mm hole with a 20 mm countersink: the cone narrows from R = 10 to
    // r = 5 over h = (R - r) / tan(angle / 2); beyond the cylinder it removes
    // the frustum pi h (R^2 + R r + r^2) / 3 less pi r^2 h.
    const auto extra = [](double angleDeg) {
        const double h = 5.0 / std::tan(angleDeg * pi / 180.0 / 2.0);
        return pi * h * (100.0 + 50.0 + 25.0) / 3.0 - pi * 25.0 * h;
    };
    for (const double angle : {90.0, 82.0}) {
        CAPTURE(angle);
        const HoleRequest sunk{.face = top(), .center = at(50, 25), .type = HoleType::Countersink, .diameter = 10_mm,
                               .countersinkDiameter = 20_mm, .countersinkAngle = angle * units::deg};
        const Body holed = requireHole(block(), sunk);
        CHECK_THAT(requireProperties(holed).volume.in(units::mm3),
                   WithinRel(100000.0 - pi * 25.0 * 20.0 - extra(angle), kRelTight));
        CHECK(circles(holed, 50, 25, 20, 10) == 1); // the cone meets the top face at R
        CHECK(circles(holed, 50, 25, 20.0 - 5.0 / std::tan(angle * pi / 360.0), 5) == 1); // and the bore at r

        HoleRequest blindSunk = sunk;
        blindSunk.extent = HoleExtent::Blind;
        blindSunk.depth = 12_mm;
        CHECK_THAT(requireProperties(requireHole(block(), blindSunk)).volume.in(units::mm3),
                   WithinRel(100000.0 - pi * 25.0 * 12.0 - extra(angle), kRelTight));
    }
    CHECK_THAT(countersinkDepth({.diameter = 10_mm, .countersinkDiameter = 20_mm, .countersinkAngle = 90_deg})
                   .in(units::mm),
               WithinAbs(5.0, 1e-12));
}

TEST_CASE("Hole_PositionIsWhereTheFaceCoordinatesSay", "[geometry][hole]") {
    const Body box = block();
    // Off centre: the same volume, at (20, 10).
    const Body offCentre = requireHole(box, through(top(), at(20, 10), 10_mm));
    CHECK_THAT(requireProperties(offCentre).volume.in(units::mm3), WithinRel(100000.0 - pi * 25.0 * 20.0, kRelTight));
    CHECK(circles(offCentre, 20, 10, 20, 5) == 1);
    // Close to the front face: 0.5 mm of wall is left.
    const Body nearEdge = requireHole(box, through(top(), at(50, 5.5), 10_mm));
    CHECK_THAT(requireProperties(nearEdge).volume.in(units::mm3), WithinRel(100000.0 - pi * 25.0 * 20.0, kRelTight));
    // On the front face (u, v) = (x, z): a hole along +Y through 50 mm.
    const Body sideways = requireHole(box, through(planeSignature(Point3D{}, Direction3D::unitY().reversed()),
                                                   at(50, 10), 10_mm));
    CHECK_THAT(requireProperties(sideways).volume.in(units::mm3), WithinRel(100000.0 - pi * 25.0 * 50.0, kRelTight));
}

TEST_CASE("Hole_RejectsInvalidRequestsBeforeTheKernel", "[geometry][hole]") {
    const Body box = block();
    const auto invalid = [&](const HoleRequest& request) { return refusal(box, request, ErrorCode::InvalidArgument); };
    for (const double mm : {0.0, -10.0, std::nan(""), std::numeric_limits<double>::infinity()}) {
        CAPTURE(mm);
        CHECK_THAT(invalid(through(top(), at(50, 25), mm * units::mm)),
                   StartsWith("hole: the hole diameter must be positive and finite, got "));
        CHECK_THAT(invalid(blind(top(), at(50, 25), 10_mm, mm * units::mm)),
                   StartsWith("hole: the hole depth must be positive and finite, got "));
    }
    HoleRequest r = through(top(), at(50, 25), 10_mm);
    r.depth = 5_mm;
    CHECK(invalid(r) == "hole: a through hole takes no depth; it goes through all material");
    r = through(top(), at(std::nan(""), 25), 10_mm);
    CHECK(invalid(r) == "hole: the hole centre must be finite");
    r = through(top(), at(50, 25), 10_mm);
    r.counterboreDepth = 2_mm;
    CHECK(invalid(r) == "hole: only a counterbore hole takes counterbore dimensions");
    r = through(top(), at(50, 25), 10_mm);
    r.countersinkAngle = 90_deg;
    CHECK(invalid(r) == "hole: only a countersink hole takes countersink dimensions");

    HoleRequest bored{.face = top(), .center = at(50, 25), .type = HoleType::Counterbore, .diameter = 10_mm,
                      .counterboreDiameter = 10_mm, .counterboreDepth = 5_mm};
    CHECK_THAT(invalid(bored), StartsWith("hole: the counterbore diameter must be larger than the hole diameter"));
    bored.counterboreDiameter = 18_mm;
    bored.counterboreDepth = 0_mm;
    CHECK_THAT(invalid(bored), StartsWith("hole: the counterbore depth must be positive and finite"));
    bored.counterboreDepth = 8_mm;
    bored.extent = HoleExtent::Blind;
    bored.depth = 8_mm;
    CHECK(invalid(bored) == "hole: the counterbore (8 mm deep) must be shallower than the blind hole (8 mm deep)");

    HoleRequest sunk{.face = top(), .center = at(50, 25), .type = HoleType::Countersink, .diameter = 10_mm,
                     .countersinkDiameter = 8_mm, .countersinkAngle = 90_deg};
    CHECK_THAT(invalid(sunk), StartsWith("hole: the countersink diameter must be larger than the hole diameter"));
    sunk.countersinkDiameter = 20_mm;
    for (const Angle angle : {0_deg, 180_deg, -(90_deg), Angle::fromSi(std::nan(""))}) {
        sunk.countersinkAngle = angle;
        CHECK_THAT(invalid(sunk), StartsWith("hole: the countersink angle must be in (0, 180) deg, got "));
    }
    sunk.countersinkAngle = 90_deg;
    sunk.extent = HoleExtent::Blind;
    sunk.depth = 4_mm; // the cone alone is 5 mm deep
    CHECK_THAT(invalid(sunk), StartsWith("hole: the countersink (5 mm deep) must be shallower than the blind hole"));

    CHECK(invalid(through(FaceSignature{.surface = FaceSurface::Cylinder}, at(0, 0), 10_mm)) ==
          "hole: placement face: only planar faces can be referenced, not a cylinder");
    CHECK(errorCode(cutHole(Body{}, through(top(), at(50, 25), 10_mm))) == ErrorCode::FailedPrecondition);
}

TEST_CASE("Hole_UnresolvablePlacementIsReportedNotGuessed", "[geometry][hole]") {
    const Body box = block();
    SECTION("no face on the plane") {
        CHECK(refusal(box, through(top(21), at(50, 25), 10_mm), ErrorCode::NotFound) ==
              "hole: the placement face (plane through (0, 0, 21) mm facing (0, 0, 1)) matches no face of the body");
    }
    SECTION("a centre off the face") {
        CHECK(refusal(box, through(top(), at(150, 25), 10_mm)) ==
              "hole: the centre (150, 25) mm is not on a face of the body on the plane through (0, 0, 20) mm facing "
              "(0, 0, 1) (1 face(s) lie on that plane elsewhere)");
    }
    SECTION("a centre on the edge between two coplanar faces") {
        // A redundant corner at (50, 0) splits the front face in two at x = 50.
        PlanarRegion region{.plane = Frame3D::xy(), .outer = {}, .holes = {}};
        const std::pair<double, double> corners[] = {{0, 0}, {50, 0}, {100, 0}, {100, 50}, {0, 50}};
        for (std::size_t i = 0; i < 5; ++i) {
            const auto& [x0, y0] = corners[i];
            const auto& [x1, y1] = corners[(i + 1) % 5];
            region.outer.segments.emplace_back(
                LineSegment2D{Point2D{x0 * units::mm, y0 * units::mm}, Point2D{x1 * units::mm, y1 * units::mm}});
        }
        const auto split = makePrism(region, 0_mm, 20_mm);
        REQUIRE(split.has_value());
        const FaceSignature front = planeSignature(Point3D{}, Direction3D::unitY().reversed());
        REQUIRE(facesOn(*split, front) == 2);
        CHECK(refusal(*split, through(front, at(50, 10), 6_mm)) ==
              "hole: the placement is ambiguous: the centre (50, 10) mm lies on 2 faces on the plane through (0, 0, 0) "
              "mm facing (0, -1, 0)");
        // Away from the split, the one face under the centre is used.
        CHECK_THAT(requireProperties(requireHole(*split, through(front, at(25, 10), 6_mm))).volume.in(units::mm3),
                   WithinRel(100000.0 - pi * 9.0 * 50.0, kRelTight));
    }
}

TEST_CASE("Hole_RefusesHolesThatDoNotFitTheFace", "[geometry][hole]") {
    const Body box = block();
    // The top face is 50 mm deep: a 60 mm hole cannot fit.
    CHECK_THAT(refusal(box, through(top(), at(50, 25), 60_mm)),
               StartsWith("hole: the hole does not fit on its face: its entry is 60 mm across, but the centre (50, 25) "
                          "mm is only 25 mm from the face's edge"));
    // 4 mm from the front face a 10 mm hole would break out of it.
    CHECK_THAT(refusal(box, through(top(), at(50, 4), 10_mm)), ContainsSubstring("is only 4 mm from the face's edge"));
    CHECK_THAT(refusal(box, through(top(), at(50, 5.0005), 10_mm)), ContainsSubstring("does not fit on its face"));
    // The counterbore, not the hole, is what must fit.
    const HoleRequest bored{.face = top(), .center = at(50, 25), .type = HoleType::Counterbore, .diameter = 10_mm,
                            .counterboreDiameter = 52_mm, .counterboreDepth = 5_mm};
    CHECK_THAT(refusal(box, bored), ContainsSubstring("its entry is 52 mm across"));
}

TEST_CASE("Hole_BlindHoleMayNotReachTheFarSide", "[geometry][hole]") {
    const Body box = block();
    for (const Length depth : {20_mm, 19.9995_mm, 25_mm}) {
        CHECK_THAT(refusal(box, blind(top(), at(50, 25), 10_mm, depth)),
                   ContainsSubstring("would reach the far side: there are only 20 mm of material along its axis; make it "
                                     "a through hole"));
    }
    CHECK_THAT(requireProperties(requireHole(box, blind(top(), at(50, 25), 10_mm, 19.9_mm))).volume.in(units::mm3),
               WithinRel(100000.0 - pi * 25.0 * 19.9, kRelTight));
    // A through hole's counterbore may not be as deep as the part.
    const HoleRequest bored{.face = top(), .center = at(50, 25), .type = HoleType::Counterbore, .diameter = 10_mm,
                            .counterboreDiameter = 18_mm, .counterboreDepth = 20_mm};
    CHECK(refusal(box, bored) == "hole: the counterbore (20 mm deep) is as deep as the material along the axis (20 mm)");
}

TEST_CASE("Hole_BlindHoleThatBreaksIntoACavityIsRefused", "[geometry][hole]") {
    // A channel along X, radius 3 at y = 31, z = 10, passes beside the axis of
    // a hole at y = 25 but inside its 5 mm radius: a 12 mm blind hole would
    // open into it, so it removes less than its own volume.
    const auto channel = makeCylinder(Axis3D{Point3D{-(1_mm), 31_mm, 10_mm}, Direction3D::unitX()}, 3_mm, 102_mm);
    REQUIRE(channel.has_value());
    const auto channelled = booleanDifference(block(), *channel);
    REQUIRE(channelled.has_value());
    CHECK_THAT(refusal(*channelled, blind(top(), at(50, 25), 10_mm, 12_mm)),
               StartsWith("hole: the blind hole breaks out of the material: it removes "));
    // Shallower, it stays in the material.
    CHECK_THAT(requireProperties(requireHole(*channelled, blind(top(), at(50, 25), 10_mm, 5_mm))).volume.in(units::mm3),
               WithinRel(100000.0 - pi * 9.0 * 100.0 - pi * 25.0 * 5.0, kRelTight));
}

TEST_CASE("Hole_OnRevolvedEndFace", "[geometry][hole][revolve]") {
    // A cylinder R = 15, h = 40 revolved about Z; its end face is the plane z = 40.
    PlanarRegion profile{.plane = Frame3D::xz(), .outer = {}, .holes = {}};
    const std::pair<double, double> corners[] = {{0, 0}, {15, 0}, {15, 40}, {0, 40}};
    for (std::size_t i = 0; i < 4; ++i) {
        const auto& [u0, v0] = corners[i];
        const auto& [u1, v1] = corners[(i + 1) % 4];
        profile.outer.segments.emplace_back(
            LineSegment2D{Point2D{u0 * units::mm, v0 * units::mm}, Point2D{u1 * units::mm, v1 * units::mm}});
    }
    const auto rod = makeRevolution(profile, Axis3D{Point3D{}, Direction3D::unitZ()}, 0_deg, 360_deg);
    REQUIRE(rod.has_value());
    const double v0 = pi * 225.0 * 40.0;
    // An axial blind bore d = 10, 20 deep, and an off-axis 4 mm through hole.
    const Body bored = requireHole(*rod, blind(top(40), at(0, 0), 10_mm, 20_mm));
    CHECK_THAT(requireProperties(bored).volume.in(units::mm3), WithinRel(v0 - pi * 25.0 * 20.0, kRelTight));
    const Body bolted = requireHole(bored, through(top(40), at(0, 10), 4_mm));
    CHECK_THAT(requireProperties(bolted).volume.in(units::mm3),
               WithinRel(v0 - pi * 25.0 * 20.0 - pi * 4.0 * 40.0, kRelTight));
}

TEST_CASE("Hole_IsDeterministic", "[geometry][hole]") {
    const Body box = block();
    const HoleRequest sunk{.face = top(), .center = at(30, 20), .type = HoleType::Countersink, .diameter = 8_mm,
                           .countersinkDiameter = 16_mm, .countersinkAngle = 90_deg};
    const MassProperties a = requireProperties(requireHole(box, sunk));
    const MassProperties b = requireProperties(requireHole(box, sunk));
    CHECK(bits(a.volume.si()) == bits(b.volume.si()));
    CHECK(bits(a.surfaceArea.si()) == bits(b.surfaceArea.si()));
    CHECK(bits(a.centerOfMass.x.si()) == bits(b.centerOfMass.x.si()));
}

TEST_CASE("Hole_ThatRemovesNothingMeasurableIsRefused", "[geometry][hole]") {
    CHECK(refusal(block(), through(top(), at(50, 25), 1e-6_mm)) == "hole: the hole removes no material");
}

// P12-HOLE-001: spotfaces and cosmetic threads.

TEST_CASE("Hole_SpotfaceIsCutLikeACounterbore", "[geometry][hole][p12]") {
    // A 10 mm hole with a 20 mm spotface 1 mm deep, as a seat.
    const HoleRequest seated{.face = top(), .center = at(50, 25), .type = HoleType::Spotface, .diameter = 10_mm,
                             .spotfaceDiameter = 20_mm, .spotfaceDepth = 1_mm};
    const Body holed = requireHole(block(), seated);
    CHECK_THAT(requireProperties(holed).volume.in(units::mm3),
               WithinRel(100000.0 - pi * 25.0 * 20.0 - pi * (100.0 - 25.0) * 1.0, kRelTight));
    const auto floor = findFaces(holed, top(19));
    REQUIRE(floor.has_value());
    REQUIRE(floor->size() == 1);
    CHECK_THAT(floor->front().area.in(units::mm2), WithinRel(pi * (100.0 - 25.0), kRelTight));

    // The same dimensions as a counterbore give the same solid: the type
    // records the intent, and names the floor differently.
    const HoleRequest bored{.face = top(), .center = at(50, 25), .type = HoleType::Counterbore, .diameter = 10_mm,
                            .counterboreDiameter = 20_mm, .counterboreDepth = 1_mm};
    const MassProperties spotfaced = requireProperties(holed);
    const MassProperties counterbored = requireProperties(requireHole(block(), bored));
    CHECK(bits(spotfaced.volume.si()) == bits(counterbored.volume.si()));
    CHECK(bits(spotfaced.surfaceArea.si()) == bits(counterbored.surfaceArea.si()));

    HoleRequest blindSeat = seated;
    blindSeat.extent = HoleExtent::Blind;
    blindSeat.depth = 12_mm;
    CHECK_THAT(requireProperties(requireHole(block(), blindSeat)).volume.in(units::mm3),
               WithinRel(100000.0 - pi * 25.0 * 12.0 - pi * (100.0 - 25.0) * 1.0, kRelTight));
}

TEST_CASE("Hole_CosmeticThreadCutsNoGeometry", "[geometry][hole][p12]") {
    // The hole is the thread's core: a threaded hole is the same solid as the
    // same hole without a thread, bit for bit.
    HoleRequest tapped = blind(top(), at(50, 25), 6.647_mm, 15_mm);
    const MassProperties plain = requireProperties(requireHole(block(), tapped));
    for (const Length length : {Length{}, 5_mm, 15_mm}) {
        CAPTURE(length.in(units::mm));
        tapped.thread = CosmeticThread{.majorDiameter = 8_mm, .length = length};
        const MassProperties threaded = requireProperties(requireHole(block(), tapped));
        CHECK(bits(threaded.volume.si()) == bits(plain.volume.si()));
        CHECK(bits(threaded.surfaceArea.si()) == bits(plain.surfaceArea.si()));
        CHECK(bits(threaded.centerOfMass.z.si()) == bits(plain.centerOfMass.z.si()));
    }
}

TEST_CASE("Hole_RefusesThreadsThatDoNotFitTheirHole", "[geometry][hole][p12]") {
    const auto invalid = [](const HoleRequest& request) {
        const auto result = validate(request);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::InvalidArgument);
        return result.error().message;
    };
    HoleRequest tapped = blind(top(), at(50, 25), 6.647_mm, 15_mm);
    tapped.thread = CosmeticThread{.majorDiameter = 8_mm, .length = 10_mm};
    CHECK(validate(tapped).has_value());

    SECTION("the thread must be wider than the hole it is cut in") {
        HoleRequest request = tapped;
        request.thread->majorDiameter = 6_mm;
        CHECK(invalid(request) ==
              "the thread's major diameter must be larger than the hole diameter (6.647 mm), got 6 mm");
        request.thread->majorDiameter = Length::fromSi(std::numeric_limits<double>::infinity());
        CHECK_THAT(invalid(request), StartsWith("the thread's major diameter must be larger"));
    }
    SECTION("the thread's length must be positive and fit a blind hole") {
        HoleRequest request = tapped;
        request.thread->length = -1_mm;
        CHECK(invalid(request) ==
              "the thread length must be positive and finite, or zero for the whole hole, got -1 mm");
        request.thread->length = 16_mm;
        CHECK(invalid(request) == "the thread (16 mm long) must not be longer than the blind hole (15 mm deep)");
        // Threaded to the bottom is allowed.
        request.thread->length = 15_mm;
        CHECK(validate(request).has_value());
    }
    SECTION("a head must clear the thread and be shorter than it") {
        HoleRequest request = tapped;
        request.type = HoleType::Counterbore;
        request.counterboreDiameter = 7.5_mm;
        request.counterboreDepth = 3_mm;
        CHECK(invalid(request) ==
              "the counterbore diameter (7.5 mm) must be larger than the thread's major diameter (8 mm)");
        request.counterboreDiameter = 12_mm;
        CHECK(validate(request).has_value());
        request.thread->length = 3_mm;
        CHECK(invalid(request) == "the thread (3 mm long) must be longer than the counterbore (3 mm deep)");
        // A thread over the whole hole always outlasts the head.
        request.thread->length = Length{};
        CHECK(validate(request).has_value());
    }
    SECTION("a through hole's thread must stay in the material under its face") {
        HoleRequest request = through(top(), at(50, 25), 6.647_mm);
        request.thread = CosmeticThread{.majorDiameter = 8_mm, .length = 20_mm};
        CHECK(cutHole(block(), request).has_value());
        request.thread->length = 25_mm;
        CHECK(refusal(block(), request) == "hole: the thread (25 mm long) is longer than the material along the "
                                           "axis under the face (20 mm)");
    }
}
