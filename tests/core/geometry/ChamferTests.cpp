#include "GeometryTestSupport.hpp"
#include "TestHelpers.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/geometry/Booleans.hpp>
#include <bettercad/core/geometry/Chamfer.hpp>
#include <bettercad/core/geometry/Edges.hpp>
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
#include <limits>
#include <numbers>
#include <vector>

using namespace bettercad;
using namespace bettercad::geometry;
using namespace bettercad::literals;
using bettercad::test::checkPoint;
using bettercad::test::errorCode;
using bettercad::test::kPositionToleranceMm;
using bettercad::test::kRelTight;
using bettercad::test::requireProperties;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr double pi = std::numbers::pi;

// The reference block: 100 x 50 x 20 mm with a corner at the origin.
Body block() {
    auto box = makeBox(100_mm, 50_mm, 20_mm);
    REQUIRE(box.has_value());
    return *box;
}

/// The 100 mm edge where the top face (z = 20) meets the front face (y = 0).
EdgeSignature topFrontEdge() {
    return lineSignature(Point3D{0_mm, 0_mm, 20_mm}, Direction3D::unitX());
}

Body requireChamfer(const Body& body, const ChamferRequest& request) {
    auto result = chamferEdges(body, request);
    if (!result) {
        FAIL(result.error().message);
    }
    CHECK(result->isValid());
    CHECK(result->topology().solids == 1);
    return *result;
}

ChamferRequest equalDistance(std::vector<EdgeSignature> edges, Length distance) {
    return {.edges = std::move(edges), .distance = distance};
}

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

} // namespace

TEST_CASE("Edges_BoxHasTwelveStraightEdgesBetweenTwoFaces", "[geometry][edges]") {
    const auto edges = listEdges(block());
    REQUIRE(edges.has_value());
    REQUIRE(edges->size() == 12);
    std::vector<double> lengths;
    for (const EdgeInfo& edge : *edges) {
        CHECK(edge.curve == EdgeCurve::Line);
        CHECK(edge.faces == 2);
        REQUIRE(edge.signature.has_value());
        lengths.push_back(edge.length.in(units::mm));
    }
    std::ranges::sort(lengths);
    const std::vector<double> expected{20, 20, 20, 20, 50, 50, 50, 50, 100, 100, 100, 100};
    for (std::size_t i = 0; i < lengths.size(); ++i) {
        CHECK_THAT(lengths[i], WithinRel(expected[i], kRelTight));
    }
}

TEST_CASE("Edges_CylinderHasTwoCirclesAndASeam", "[geometry][edges]") {
    const auto cylinder = makeCylinder(15_mm, 40_mm);
    REQUIRE(cylinder.has_value());
    const auto edges = listEdges(*cylinder);
    REQUIRE(edges.has_value());
    REQUIRE(edges->size() == 3);
    const auto circles = std::ranges::count_if(*edges, [](const EdgeInfo& e) { return e.curve == EdgeCurve::Circle; });
    CHECK(circles == 2);
    const auto seam = std::ranges::find_if(*edges, [](const EdgeInfo& e) { return e.curve == EdgeCurve::Line; });
    REQUIRE(seam != edges->end());
    CHECK(seam->faces == 1); // the side face meets itself along its seam
}

TEST_CASE("EdgeSignature_IsCanonicalWhateverPointAndOrientationDescribeTheCurve", "[geometry][edges]") {
    const EdgeSignature a = lineSignature(Point3D{0_mm, 0_mm, 20_mm}, Direction3D::unitX());
    const EdgeSignature b = lineSignature(Point3D{73_mm, 0_mm, 20_mm}, Direction3D::unitX().reversed());
    CHECK(a == b);
    checkPoint(a.point, 0, 0, 20);
    CHECK(a.direction == Direction3D::unitX());

    const auto circle = circleSignature(Point3D{0_mm, 0_mm, 40_mm}, Direction3D::unitZ().reversed(), 15_mm);
    REQUIRE(circle.has_value());
    CHECK(circle->direction == Direction3D::unitZ());
    CHECK(errorCode(circleSignature(Point3D{}, Direction3D::unitZ(), 0_mm)) == ErrorCode::InvalidArgument);
    CHECK_THAT(describe(a), ContainsSubstring("line through (0, 0, 20) mm along (1, 0, 0)"));
}

TEST_CASE("Edges_SignatureResolvesToExactlyOneEdge", "[geometry][edges]") {
    const Body box = block();
    const auto found = findEdges(box, topFrontEdge());
    REQUIRE(found.has_value());
    REQUIRE(found->size() == 1);
    CHECK_THAT(found->front().length.in(units::mm), WithinRel(100.0, kRelTight));
    checkPoint(found->front().midpoint, 50, 0, 20);

    const auto none = findEdges(box, lineSignature(Point3D{0_mm, 0_mm, 25_mm}, Direction3D::unitX()));
    REQUIRE(none.has_value());
    CHECK(none->empty());
}

TEST_CASE("Chamfer_SingleEdgeMatchesAnalyticVolume", "[geometry][chamfer]") {
    // Removing the corner of a straight 90-degree edge of length L with
    // legs d removes a triangular prism: V = L W H - d^2 L / 2.
    const Body box = block();
    const Body chamfered = requireChamfer(box, equalDistance({topFrontEdge()}, 5_mm));
    const auto props = requireProperties(chamfered);
    const double removed = 0.5 * 5.0 * 5.0 * 100.0;
    CHECK_THAT(props.volume.in(units::mm3), WithinRel(100.0 * 50.0 * 20.0 - removed, kRelTight));
    // Area: the top and front faces each lose a 5 x 100 strip, the two end
    // faces lose a 5 x 5 / 2 triangle, and the 100 x 5 sqrt(2) chamfer face is added.
    const double area = 16000.0 - 2 * 500.0 - 2 * 12.5 + 100.0 * 5.0 * std::numbers::sqrt2;
    CHECK_THAT(props.surfaceArea.in(units::mm2), WithinRel(area, kRelTight));
    CHECK(chamfered.topology().faces == 7);
    CHECK(chamfered.topology().edges == 15);
    // The bounding box is unchanged: other edges still reach every extreme.
    const auto box3 = chamfered.boundingBox().value();
    checkPoint(box3.min, 0, 0, 0);
    checkPoint(box3.max, 100, 50, 20);
    // The chamfer edges replace the reference edge: it no longer resolves.
    CHECK(findEdges(chamfered, topFrontEdge())->empty());
    // The input is not modified.
    CHECK_THAT(requireProperties(box).volume.in(units::mm3), WithinRel(100000.0, kRelTight));
}

TEST_CASE("Chamfer_TwoDistancesPutTheFirstDistanceOnTheReferenceFace", "[geometry][chamfer]") {
    // 5 mm on the top face (outward normal +Z), 2 mm on the front face.
    const ChamferRequest request{.edges = {topFrontEdge()}, .mode = ChamferMode::TwoDistance, .distance = 5_mm,
                                 .distance2 = 2_mm, .referenceSide = Direction3D::unitZ()};
    const auto props = requireProperties(requireChamfer(block(), request));
    const double v0 = 100000.0;
    const double removed = 0.5 * 5.0 * 2.0 * 100.0;
    CHECK_THAT(props.volume.in(units::mm3), WithinRel(v0 - removed, kRelTight));
    // The removed triangle has corners (y, z) = (0, 20), (5, 20), (0, 18), so its
    // centroid is (5/3, 58/3); the solid's centroid moves away from it.
    const double cy = (v0 * 25.0 - removed * 5.0 / 3.0) / (v0 - removed);
    const double cz = (v0 * 10.0 - removed * 58.0 / 3.0) / (v0 - removed);
    checkPoint(props.centerOfMass, 50, cy, cz);
}

TEST_CASE("Chamfer_DistanceAngleMeasuresTheAngleFromTheReferenceFace", "[geometry][chamfer]") {
    // 5 mm on the top face, chamfer face at 30 degrees to it: on the front
    // face the chamfer reaches 5 tan(30 deg) below the edge.
    const ChamferRequest request{.edges = {topFrontEdge()}, .mode = ChamferMode::DistanceAngle, .distance = 5_mm,
                                 .angle = 30_deg, .referenceSide = Direction3D::unitZ()};
    const auto props = requireProperties(requireChamfer(block(), request));
    const double leg = 5.0 * std::tan(pi / 6.0);
    const double v0 = 100000.0;
    const double removed = 0.5 * 5.0 * leg * 100.0;
    CHECK_THAT(props.volume.in(units::mm3), WithinRel(v0 - removed, kRelTight));
    const double cy = (v0 * 25.0 - removed * 5.0 / 3.0) / (v0 - removed);
    const double cz = (v0 * 10.0 - removed * (40.0 + 20.0 - leg) / 3.0) / (v0 - removed);
    checkPoint(props.centerOfMass, 50, cy, cz);
}

TEST_CASE("Chamfer_MultipleSeparateEdgesMatchAnalyticVolume", "[geometry][chamfer]") {
    // Top-front and bottom-back edges share no vertex.
    const EdgeSignature bottomBack = lineSignature(Point3D{0_mm, 50_mm, 0_mm}, Direction3D::unitX());
    const Body chamfered = requireChamfer(block(), equalDistance({topFrontEdge(), bottomBack}, 5_mm));
    CHECK_THAT(requireProperties(chamfered).volume.in(units::mm3),
               WithinRel(100000.0 - 2 * 0.5 * 25.0 * 100.0, kRelTight));
    CHECK(chamfered.topology().faces == 8);
}

TEST_CASE("Chamfer_AdjacentEdgesMeetInAMitredCorner", "[geometry][chamfer]") {
    // The top-front (along X) and top-left (along Y) edges meet at (0, 0, 20).
    // The two triangular prisms overlap near the corner in a pyramid-like
    // region of volume d^3 / 3 (integral of (d - w)^2 over w in [0, d]).
    const EdgeSignature topLeft = lineSignature(Point3D{0_mm, 0_mm, 20_mm}, Direction3D::unitY());
    const Body chamfered = requireChamfer(block(), equalDistance({topFrontEdge(), topLeft}, 5_mm));
    const double d = 5.0;
    const double removed = 0.5 * d * d * 100.0 + 0.5 * d * d * 50.0 - d * d * d / 3.0;
    CHECK_THAT(requireProperties(chamfered).volume.in(units::mm3), WithinRel(100000.0 - removed, kRelTight));
}

TEST_CASE("Chamfer_CircularEdgeOfCylinderMatchesPappus", "[geometry][chamfer]") {
    // Chamfering the top rim of a cylinder removes a ring whose cross-section
    // is the triangle with legs d; by Pappus V = (d^2 / 2) 2 pi (r - d / 3).
    const auto cylinder = makeCylinder(15_mm, 40_mm);
    REQUIRE(cylinder.has_value());
    const auto rim = circleSignature(Point3D{0_mm, 0_mm, 40_mm}, Direction3D::unitZ(), 15_mm);
    REQUIRE(rim.has_value());
    const auto props = requireProperties(requireChamfer(*cylinder, equalDistance({*rim}, 2_mm)));
    const double r = 15.0;
    const double h = 40.0;
    const double d = 2.0;
    CHECK_THAT(props.volume.in(units::mm3), WithinRel(pi * r * r * h - d * d / 2.0 * 2.0 * pi * (r - d / 3.0), kRelTight));
    // Area: the top disc shrinks to radius r - d, the side loses height d, and
    // a conical band of slant d sqrt(2) between radii r and r - d is added.
    const double area = 2 * pi * r * (h - d) + pi * r * r + pi * (r - d) * (r - d) +
                        pi * (r + (r - d)) * d * std::numbers::sqrt2;
    CHECK_THAT(props.surfaceArea.in(units::mm2), WithinRel(area, kRelTight));
}

TEST_CASE("Chamfer_IsDeterministic", "[geometry][chamfer]") {
    const Body box = block();
    const auto a = requireProperties(requireChamfer(box, equalDistance({topFrontEdge()}, 5_mm)));
    const auto b = requireProperties(requireChamfer(box, equalDistance({topFrontEdge()}, 5_mm)));
    CHECK(bits(a.volume.si()) == bits(b.volume.si()));
    CHECK(bits(a.surfaceArea.si()) == bits(b.surfaceArea.si()));
}

TEST_CASE("Chamfer_RejectsInvalidRequestsBeforeTheKernel", "[geometry][chamfer]") {
    const Body box = block();
    const auto invalid = [&](const ChamferRequest& request) { return errorCode(chamferEdges(box, request)); };
    CHECK(invalid(equalDistance({}, 5_mm)) == ErrorCode::InvalidArgument);
    for (const Length d : {0_mm, -(1_mm), Length::fromSi(std::nan("")),
                           Length::fromSi(std::numeric_limits<double>::infinity())}) {
        CAPTURE(d.si());
        CHECK(invalid(equalDistance({topFrontEdge()}, d)) == ErrorCode::InvalidArgument);
    }
    CHECK(invalid(equalDistance({topFrontEdge(), topFrontEdge()}, 5_mm)) == ErrorCode::InvalidArgument);
    ChamferRequest two{.edges = {topFrontEdge()}, .mode = ChamferMode::TwoDistance, .distance = 5_mm,
                       .distance2 = 0_mm, .referenceSide = Direction3D::unitZ()};
    CHECK(invalid(two) == ErrorCode::InvalidArgument);
    two.distance2 = 2_mm;
    two.referenceSide.reset();
    CHECK(invalid(two) == ErrorCode::InvalidArgument);
    ChamferRequest angled{.edges = {topFrontEdge()}, .mode = ChamferMode::DistanceAngle, .distance = 5_mm,
                          .angle = 90_deg, .referenceSide = Direction3D::unitZ()};
    CHECK(invalid(angled) == ErrorCode::InvalidArgument);
    ChamferRequest sided = equalDistance({topFrontEdge()}, 5_mm);
    sided.referenceSide = Direction3D::unitZ();
    CHECK(invalid(sided) == ErrorCode::InvalidArgument);
    CHECK(errorCode(chamferEdges(Body{}, equalDistance({topFrontEdge()}, 5_mm))) == ErrorCode::FailedPrecondition);
}

TEST_CASE("Chamfer_UnresolvableReferencesAreReportedNotGuessed", "[geometry][chamfer]") {
    const Body box = block();
    SECTION("no edge on the curve") {
        const auto result =
            chamferEdges(box, equalDistance({lineSignature(Point3D{0_mm, 0_mm, 25_mm}, Direction3D::unitX())}, 5_mm));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::NotFound);
        CHECK(result.error().message ==
              "chamfer: edge reference 1 (line through (0, 0, 25) mm along (1, 0, 0)) matches no edge of the body");
    }
    SECTION("two edges on the curve") {
        // A notch through the middle of the edge splits it into two collinear edges.
        const auto notch = makeBox(Point3D{45_mm, -(1_mm), 15_mm}, 10_mm, 10_mm, 10_mm);
        REQUIRE(notch.has_value());
        const auto notched = booleanDifference(box, *notch);
        REQUIRE(notched.has_value());
        REQUIRE(findEdges(*notched, topFrontEdge())->size() == 2);
        const auto result = chamferEdges(*notched, equalDistance({topFrontEdge()}, 2_mm));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::FailedPrecondition);
        CHECK_THAT(result.error().message, ContainsSubstring("is ambiguous: 2 edges of the body lie on it"));
    }
    SECTION("a seam, which lies on one face") {
        const auto cylinder = makeCylinder(15_mm, 40_mm);
        REQUIRE(cylinder.has_value());
        const auto edges = listEdges(*cylinder);
        const auto seam = std::ranges::find_if(*edges, [](const EdgeInfo& e) { return e.curve == EdgeCurve::Line; });
        REQUIRE(seam != edges->end());
        const auto result = chamferEdges(*cylinder, equalDistance({*seam->signature}, 1_mm));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::FailedPrecondition);
        CHECK_THAT(result.error().message, ContainsSubstring("bounds 1 face(s)"));
    }
    SECTION("a reference side that does not tell the faces apart") {
        const auto diagonal = Direction3D::fromComponents(0.0, -1.0, 1.0);
        const ChamferRequest request{.edges = {topFrontEdge()}, .mode = ChamferMode::TwoDistance, .distance = 5_mm,
                                     .distance2 = 2_mm, .referenceSide = *diagonal};
        const auto result = chamferEdges(box, request);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::FailedPrecondition);
        CHECK_THAT(result.error().message, ContainsSubstring("does not tell the two faces"));
    }
}

TEST_CASE("Chamfer_FollowsTangentEdgesAndRejectsTwoReferencesToOneChain", "[geometry][chamfer]") {
    // A stadium: two semicircles of radius 5 mm whose centres are 20 mm apart,
    // joined by straight sides, extruded 10 mm. Its top outline is one smooth
    // chain of two lines and two arcs, tangent where they meet.
    const auto p = [](double x, double y) { return Point2D{x * units::mm, y * units::mm}; };
    PlanarRegion stadium{.plane = Frame3D::xy(), .outer = {}, .holes = {}};
    stadium.outer.segments = {
        LineSegment2D{p(0, -5), p(20, -5)},
        ArcSegment2D{p(20, 0), p(20, -5), p(20, 5), true},
        LineSegment2D{p(20, 5), p(0, 5)},
        ArcSegment2D{p(0, 0), p(0, 5), p(0, -5), true},
    };
    const auto slot = makePrism(stadium, 0_mm, 10_mm);
    REQUIRE(slot.has_value());
    const EdgeSignature front = lineSignature(Point3D{0_mm, -(5_mm), 10_mm}, Direction3D::unitX());
    const EdgeSignature back = lineSignature(Point3D{0_mm, 5_mm, 10_mm}, Direction3D::unitX());

    // One reference chamfers the whole chain. The straight sides lose d^2 / 2
    // per unit length; each half turn loses a ring whose triangle has its
    // centroid d / 3 inside the radius, so by Pappus pi (r - d/3) d^2 / 2.
    const double d = 1.0;
    const double r = 5.0;
    const double v0 = (20.0 * 2.0 * r + pi * r * r) * 10.0;
    const double removed = 0.5 * d * d * (2.0 * 20.0 + 2.0 * pi * (r - d / 3.0));
    const Body chamfered = requireChamfer(*slot, equalDistance({front}, 1_mm));
    CHECK_THAT(requireProperties(chamfered).volume.in(units::mm3), WithinRel(v0 - removed, kRelTight));
    CHECK(findEdges(chamfered, back)->empty()); // the opposite side was chamfered with it

    // A second reference into the same chain would chamfer it twice.
    const auto twice = chamferEdges(*slot, equalDistance({front, back}, 1_mm));
    REQUIRE_FALSE(twice.has_value());
    CHECK(twice.error().code == ErrorCode::InvalidArgument);
    CHECK(twice.error().message == "chamfer: edge reference 2 (line through (0, 5, 10) mm along (1, 0, 0)) is "
                                   "already chamfered by an earlier reference (the edges join smoothly)");
}

TEST_CASE("Chamfer_RejectsOversizedDistance", "[geometry][chamfer]") {
    // The front face is only 20 mm tall. Regression: the kernel was once
    // given this chamfer and crashed the process in release builds.
    const Body box = block();
    const auto result = chamferEdges(box, equalDistance({topFrontEdge()}, 25_mm));
    REQUIRE_FALSE(result.has_value());
    UNSCOPED_INFO(result.error().message);
    CHECK(result.error().code == ErrorCode::FailedPrecondition);
    CHECK(result.error().message == "chamfer: edge reference 1 (line through (0, 0, 20) mm along (1, 0, 0)) does not "
                                    "fit: its chamfer needs 25 mm on a face next to the edge, which leaves only 20 mm "
                                    "(a chamfer must leave at least 0.001 mm)");
    CHECK_THAT(requireProperties(box).volume.in(units::mm3), WithinRel(100000.0, kRelTight));
}

TEST_CASE("Chamfer_RefusesChamfersThatDoNotFitBeforeTheKernel", "[geometry][chamfer]") {
    // Every case here crashed the process in release builds when it reached
    // the kernel (OCCT 8.0.1, GCC 16.1 MinGW); see
    // docs/verification/P11-FEAT-002. Now each is refused beforehand.
    const auto refusal = [](const Body& body, const ChamferRequest& request) {
        const auto result = chamferEdges(body, request);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::FailedPrecondition);
        return result.error().message;
    };
    const Body box = block();

    SECTION("a distance equal to the face it runs across") {
        CHECK_THAT(refusal(box, equalDistance({topFrontEdge()}, 20_mm)),
                   ContainsSubstring("needs 20 mm on a face next to the edge, which leaves only 20 mm"));
    }
    SECTION("a second distance larger than its face") {
        const ChamferRequest request{.edges = {topFrontEdge()}, .mode = ChamferMode::TwoDistance, .distance = 5_mm,
                                     .distance2 = 25_mm, .referenceSide = Direction3D::unitZ()};
        CHECK_THAT(refusal(box, request), ContainsSubstring("needs 25 mm on a face next to the edge"));
    }
    SECTION("two chamfers that each fit but meet across a thin face") {
        // A 2 mm plate: its top and bottom front edges bound the 2 mm front face.
        const auto plate = makeBox(100_mm, 50_mm, 2_mm);
        REQUIRE(plate.has_value());
        const EdgeSignature top = lineSignature(Point3D{0_mm, 0_mm, 2_mm}, Direction3D::unitX());
        const EdgeSignature bottom = lineSignature(Point3D{0_mm, 0_mm, 0_mm}, Direction3D::unitX());
        CHECK(refusal(*plate, equalDistance({top, bottom}, 1.5_mm)) ==
              "chamfer: edge reference 1 (line through (0, 0, 2) mm along (1, 0, 0)) and edge reference 2 (line "
              "through (0, 0, 0) mm along (1, 0, 0)) do not fit together: their chamfers need 1.5 mm and 1.5 mm on a "
              "face they share, where the edges are only 2 mm apart (a chamfer must leave at least 0.001 mm)");
        // With room to spare they build: V0 - 2 (d^2 / 2) L.
        const Body both = requireChamfer(*plate, equalDistance({top, bottom}, 0.9_mm));
        CHECK_THAT(requireProperties(both).volume.in(units::mm3), WithinRel(10000.0 - 0.81 * 100.0, kRelTight));
    }
    SECTION("two chamfers from opposite sides of the top face") {
        const auto tall = makeBox(100_mm, 50_mm, 30_mm);
        REQUIRE(tall.has_value());
        const EdgeSignature front = lineSignature(Point3D{0_mm, 0_mm, 30_mm}, Direction3D::unitX());
        const EdgeSignature back = lineSignature(Point3D{0_mm, 50_mm, 30_mm}, Direction3D::unitX());
        CHECK_THAT(refusal(*tall, equalDistance({front, back}, 26_mm)),
                   ContainsSubstring("their chamfers need 26 mm and 26 mm on a face they share, where the edges are "
                                     "only 50 mm apart"));
    }
    SECTION("a rim chamfer wider than the disc it cuts into") {
        const auto rod = makeCylinder(15_mm, 40_mm);
        REQUIRE(rod.has_value());
        const auto rim = circleSignature(Point3D{0_mm, 0_mm, 40_mm}, Direction3D::unitZ(), 15_mm);
        REQUIRE(rim.has_value());
        CHECK_THAT(refusal(*rod, equalDistance({*rim}, 16_mm)), ContainsSubstring("does not fit"));
    }
    SECTION("a smooth chain whose sides meet across the face") {
        // The stadium of the tangent-chain test is 10 mm wide.
        const auto p = [](double x, double y) { return Point2D{x * units::mm, y * units::mm}; };
        PlanarRegion stadium{.plane = Frame3D::xy(), .outer = {}, .holes = {}};
        stadium.outer.segments = {
            LineSegment2D{p(0, -5), p(20, -5)},
            ArcSegment2D{p(20, 0), p(20, -5), p(20, 5), true},
            LineSegment2D{p(20, 5), p(0, 5)},
            ArcSegment2D{p(0, 0), p(0, 5), p(0, -5), true},
        };
        const auto slot = makePrism(stadium, 0_mm, 20_mm);
        REQUIRE(slot.has_value());
        const EdgeSignature front = lineSignature(Point3D{0_mm, -(5_mm), 20_mm}, Direction3D::unitX());
        CHECK_THAT(refusal(*slot, equalDistance({front}, 5.5_mm)),
                   ContainsSubstring("an edge that joins edge reference 1"));
    }
    SECTION("just inside the limit, the chamfer is built") {
        const Body deep = requireChamfer(box, equalDistance({topFrontEdge()}, 19.9_mm));
        CHECK_THAT(requireProperties(deep).volume.in(units::mm3), WithinRel(100000.0 - 0.5 * 19.9 * 19.9 * 100.0, kRelTight));
    }
    CHECK_THAT(requireProperties(box).volume.in(units::mm3), WithinRel(100000.0, kRelTight)); // input untouched
}
