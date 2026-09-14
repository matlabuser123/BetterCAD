#include "GeometryTestSupport.hpp"
#include "TestHelpers.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/geometry/Booleans.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/geometry/Fillet.hpp>
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
using bettercad::test::kRelTight;
using bettercad::test::requireProperties;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr double pi = std::numbers::pi;
// The area a fillet of radius r takes out of a square corner (the square
// r x r minus the quarter disc), per unit length of a straight edge.
double corner(double r) {
    return r * r * (1.0 - pi / 4.0);
}
// Distance from the corner to the centroid of that area, along either face:
// r (10 - 3 pi) / (3 (4 - pi)).
double cornerCentroid(double r) {
    return r * (10.0 - 3.0 * pi) / (3.0 * (4.0 - pi));
}

Body block() {
    auto box = makeBox(100_mm, 50_mm, 20_mm);
    REQUIRE(box.has_value());
    return *box;
}

EdgeSignature alongX(double y, double z) {
    return lineSignature(Point3D{0_mm, y * units::mm, z * units::mm}, Direction3D::unitX());
}
EdgeSignature alongY(double x, double z) {
    return lineSignature(Point3D{x * units::mm, 0_mm, z * units::mm}, Direction3D::unitY());
}
EdgeSignature alongZ(double x, double y) {
    return lineSignature(Point3D{x * units::mm, y * units::mm, 0_mm}, Direction3D::unitZ());
}

FilletRequest roundEdges(std::vector<EdgeSignature> edges, Length radius) {
    return {.edges = std::move(edges), .radius = radius};
}

Body requireFillet(const Body& body, const FilletRequest& request) {
    auto result = filletEdges(body, request);
    if (!result) {
        FAIL(result.error().message);
    }
    CHECK(result->isValid());
    CHECK(result->topology().solids == 1);
    return *result;
}

std::string refusal(const Body& body, const FilletRequest& request) {
    const auto result = filletEdges(body, request);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ErrorCode::FailedPrecondition);
    return result.error().message;
}

/// The one edge of @p body on @p signature.
EdgeInfo requireEdge(const Body& body, const EdgeSignature& signature) {
    const auto found = findEdges(body, signature);
    REQUIRE(found.has_value());
    REQUIRE(found->size() == 1);
    return found->front();
}

/// A prism, `height` mm tall, of the polygon (mm) in the XY plane.
Body prism(std::initializer_list<std::pair<double, double>> corners, double height) {
    const std::vector<std::pair<double, double>> p(corners);
    PlanarRegion region{.plane = Frame3D::xy(), .outer = {}, .holes = {}};
    for (std::size_t i = 0; i < p.size(); ++i) {
        const auto& [x0, y0] = p[i];
        const auto& [x1, y1] = p[(i + 1) % p.size()];
        region.outer.segments.emplace_back(
            LineSegment2D{Point2D{x0 * units::mm, y0 * units::mm}, Point2D{x1 * units::mm, y1 * units::mm}});
    }
    auto body = makePrism(region, 0_mm, height * units::mm);
    REQUIRE(body.has_value());
    return *body;
}

/// A stadium: two semicircles of radius 5 mm, centres 20 mm apart, joined by
/// straight sides, `height` mm tall. Its top outline is one smooth chain.
Body stadium(double height) {
    const auto p = [](double x, double y) { return Point2D{x * units::mm, y * units::mm}; };
    PlanarRegion region{.plane = Frame3D::xy(), .outer = {}, .holes = {}};
    region.outer.segments = {
        LineSegment2D{p(0, -5), p(20, -5)},
        ArcSegment2D{p(20, 0), p(20, -5), p(20, 5), true},
        LineSegment2D{p(20, 5), p(0, 5)},
        ArcSegment2D{p(0, 0), p(0, 5), p(0, -5), true},
    };
    auto body = makePrism(region, 0_mm, height * units::mm);
    REQUIRE(body.has_value());
    return *body;
}

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

} // namespace

TEST_CASE("Edges_FaceAngleTellsSharpFromSmoothEdges", "[geometry][edges]") {
    // Box edges meet square; the stadium's side lines join plane and cylinder smoothly.
    CHECK_THAT(requireEdge(block(), alongX(0, 20)).faceAngle->in(units::deg), WithinAbs(90.0, 1e-9));
    const EdgeInfo tangent = requireEdge(stadium(10), alongZ(20, -5));
    REQUIRE(tangent.faceAngle.has_value());
    CHECK(tangent.faceAngle->si() < 1e-9);
    // A seam bounds one face, so it has no angle.
    const auto rod = makeCylinder(15_mm, 40_mm);
    REQUIRE(rod.has_value());
    const auto edges = listEdges(*rod);
    REQUIRE(edges.has_value());
    const auto seam = std::ranges::find_if(*edges, [](const EdgeInfo& e) { return e.curve == EdgeCurve::Line; });
    REQUIRE(seam != edges->end());
    CHECK_FALSE(seam->faceAngle.has_value());
}

TEST_CASE("Fillet_SingleStraightEdgeMatchesAnalyticVolume", "[geometry][fillet]") {
    const Body box = block();
    const Body rounded = requireFillet(box, roundEdges({alongX(0, 20)}, 5_mm));
    const MassProperties props = requireProperties(rounded);
    // V = V0 - L r^2 (1 - pi/4): the top front edge, L = 100, r = 5.
    CHECK_THAT(props.volume.in(units::mm3), WithinRel(100000.0 - 100.0 * corner(5), kRelTight));
    // Top and front lose a 5 x 100 strip, the end faces a corner area each;
    // the fillet is a quarter cylinder, (pi r / 2) x L.
    CHECK_THAT(props.surfaceArea.in(units::mm2),
               WithinRel(16000.0 - 1000.0 - 2.0 * corner(5) + pi * 5.0 / 2.0 * 100.0, kRelTight));
    const TopologySummary topology = rounded.topology();
    CHECK(topology.faces == 7);
    CHECK(topology.edges == 15);
    const auto box0 = rounded.boundingBox().value();
    CHECK_THAT(box0.max.z.in(units::mm), WithinAbs(20.0, bettercad::test::kPositionToleranceMm));
    CHECK_THAT(box0.min.y.in(units::mm), WithinAbs(0.0, bettercad::test::kPositionToleranceMm));

    // The rounded face is a cylinder of radius 5 about the line y = 5,
    // z = 15: it meets the top and front faces along lines 5 mm from the old
    // edge, tangentially (G1), and the end faces in arcs of radius 5.
    CHECK(findEdges(rounded, alongX(0, 20))->empty());
    for (const EdgeSignature& seam : {alongX(5, 20), alongX(0, 15)}) {
        const EdgeInfo edge = requireEdge(rounded, seam);
        REQUIRE(edge.faceAngle.has_value());
        CHECK(edge.faceAngle->si() < 1e-9);
    }
    for (const double x : {0.0, 100.0}) {
        const auto arc = circleSignature(Point3D{x * units::mm, 5_mm, 15_mm}, Direction3D::unitX(), 5_mm);
        REQUIRE(arc.has_value());
        const EdgeInfo edge = requireEdge(rounded, *arc);
        CHECK_THAT(edge.length.in(units::mm), WithinRel(pi * 5.0 / 2.0, kRelTight));
        CHECK_THAT(edge.faceAngle->in(units::deg), WithinAbs(90.0, 1e-9));
    }
    CHECK_THAT(requireProperties(box).volume.in(units::mm3), WithinRel(100000.0, kRelTight)); // input untouched
}

TEST_CASE("Fillet_MultipleIndependentEdgesMatchAnalyticVolume", "[geometry][fillet]") {
    // The top front and bottom back edges: their fillets do not meet.
    const Body rounded = requireFillet(block(), roundEdges({alongX(0, 20), alongX(50, 0)}, 5_mm));
    CHECK_THAT(requireProperties(rounded).volume.in(units::mm3), WithinRel(100000.0 - 2 * 100.0 * corner(5), kRelTight));
    CHECK(rounded.topology().faces == 8);
}

TEST_CASE("Fillet_AdjacentEdgesAreBlendedAtTheirCorner", "[geometry][fillet]") {
    const double r = 5.0;
    SECTION("two edges meeting at a vertex remove the union of their corners") {
        // The top front and top left edges. Where they meet, the two corner
        // regions overlap in r^3 (5/3 - pi/2) (see the evidence README).
        const Body rounded = requireFillet(block(), roundEdges({alongX(0, 20), alongY(0, 20)}, 5_mm));
        CHECK_THAT(requireProperties(rounded).volume.in(units::mm3),
                   WithinRel(100000.0 - corner(r) * (100 + 50) + r * r * r * (5.0 / 3.0 - pi / 2.0), kRelTight));
    }
    SECTION("three edges meeting at a vertex end in a spherical corner") {
        // Each edge loses its corner area over its length less r; the corner
        // cube r^3 loses all but an eighth of a ball of radius r.
        const Body rounded = requireFillet(block(), roundEdges({alongX(0, 20), alongY(0, 20), alongZ(0, 0)}, 5_mm));
        CHECK_THAT(requireProperties(rounded).volume.in(units::mm3),
                   WithinRel(100000.0 - corner(r) * (100 + 50 + 20 - 3 * r) - r * r * r * (1.0 - pi / 6.0), kRelTight));
    }
}

TEST_CASE("Fillet_ConcaveEdgeAddsMaterial", "[geometry][fillet]") {
    // An L: the 50 x 50 square less its 30 x 30 corner, 20 high. The inner
    // corner edge is concave, so rounding it fills in r^2 (1 - pi/4) per unit
    // length.
    const Body l = prism({{0, 0}, {50, 0}, {50, 20}, {20, 20}, {20, 50}, {0, 50}}, 20);
    CHECK_THAT(requireProperties(l).volume.in(units::mm3), WithinRel(32000.0, kRelTight));
    CHECK_THAT(requireEdge(l, alongZ(20, 20)).faceAngle->in(units::deg), WithinAbs(90.0, 1e-9));
    for (const double r : {5.0, 29.9}) {
        CAPTURE(r);
        const Body rounded = requireFillet(l, roundEdges({alongZ(20, 20)}, r * units::mm));
        CHECK_THAT(requireProperties(rounded).volume.in(units::mm3), WithinRel(32000.0 + corner(r) * 20.0, kRelTight));
    }
}

TEST_CASE("Fillet_CircularEdgeMatchesPappus", "[geometry][fillet]") {
    // The top rim of a cylinder R = 15, h = 40 with r = 2. The removed
    // corner area turns about the axis at R minus its centroid offset.
    const double R = 15.0;
    const double h = 40.0;
    const double r = 2.0;
    const auto rod = makeCylinder(15_mm, 40_mm);
    REQUIRE(rod.has_value());
    const auto rim = circleSignature(Point3D{0_mm, 0_mm, 40_mm}, Direction3D::unitZ(), 15_mm);
    REQUIRE(rim.has_value());
    const Body rounded = requireFillet(*rod, roundEdges({*rim}, 2_mm));
    const MassProperties props = requireProperties(rounded);
    CHECK_THAT(props.volume.in(units::mm3),
               WithinRel(pi * R * R * h - 2.0 * pi * (R - cornerCentroid(r)) * corner(r), kRelTight));
    // Side and top shrink by r; the torus of the fillet is a quarter circle
    // (arc pi r / 2, centroid 2 r / pi from its centre) swept about the axis.
    const double torus = pi * r / 2.0 * 2.0 * pi * (R - r + 2.0 * r / pi);
    CHECK_THAT(props.surfaceArea.in(units::mm2),
               WithinRel(2.0 * pi * R * (h - r) + pi * R * R + pi * (R - r) * (R - r) + torus, kRelTight));
    // It meets the top face in a circle of radius R - r and the side in one
    // at h - r, tangentially.
    for (const auto& [z, radius] : {std::pair{40.0, 13.0}, std::pair{38.0, 15.0}}) {
        const auto circle = circleSignature(Point3D{0_mm, 0_mm, z * units::mm}, Direction3D::unitZ(), radius * units::mm);
        REQUIRE(circle.has_value());
        const EdgeInfo edge = requireEdge(rounded, *circle);
        REQUIRE(edge.faceAngle.has_value());
        CHECK(edge.faceAngle->si() < 1e-9);
    }
}

TEST_CASE("Fillet_TangentChainIsRoundedAsAWhole", "[geometry][fillet]") {
    // One reference to the stadium's front line rounds its whole top outline:
    // the straight sides lose r^2 (1 - pi/4) per unit length, each half turn
    // pi (5 - centroid offset) times that area.
    const Body slot = stadium(10);
    const double r = 1.0;
    const double v0 = (20.0 * 10.0 + pi * 25.0) * 10.0;
    const Body rounded = requireFillet(slot, roundEdges({alongX(-5, 10)}, 1_mm));
    CHECK_THAT(requireProperties(rounded).volume.in(units::mm3),
               WithinRel(v0 - corner(r) * (2.0 * 20.0 + 2.0 * pi * (5.0 - cornerCentroid(r))), kRelTight));
    CHECK(findEdges(rounded, alongX(5, 10))->empty()); // the back line went with it

    // A second reference into the same chain would round it twice.
    const auto twice = filletEdges(slot, roundEdges({alongX(-5, 10), alongX(5, 10)}, 1_mm));
    REQUIRE_FALSE(twice.has_value());
    CHECK(twice.error().code == ErrorCode::InvalidArgument);
    CHECK(twice.error().message == "fillet: edge reference 2 (line through (0, 5, 10) mm along (1, 0, 0)) is already "
                                   "filleted by an earlier reference (the edges join smoothly)");
}

TEST_CASE("Fillet_RefusesEdgesWhereFacesJoinSmoothly", "[geometry][fillet]") {
    // The line where the stadium's flat side meets its round end is tangent.
    CHECK(refusal(stadium(10), roundEdges({alongZ(20, -5)}, 1_mm)) ==
          "fillet: edge reference 1 (line through (20, -5, 0) mm along (0, 0, 1)) joins its two faces smoothly; there "
          "is no corner to round");
}

TEST_CASE("Fillet_IsDeterministic", "[geometry][fillet]") {
    const Body box = block();
    const FilletRequest request = roundEdges({alongX(0, 20), alongY(0, 20), alongZ(0, 0)}, 5_mm);
    const MassProperties a = requireProperties(requireFillet(box, request));
    const MassProperties b = requireProperties(requireFillet(box, request));
    CHECK(bits(a.volume.si()) == bits(b.volume.si()));
    CHECK(bits(a.surfaceArea.si()) == bits(b.surfaceArea.si()));
    CHECK(bits(a.centerOfMass.x.si()) == bits(b.centerOfMass.x.si()));
}

TEST_CASE("Fillet_RejectsInvalidRequestsBeforeTheKernel", "[geometry][fillet]") {
    const Body box = block();
    const auto message = [&](const FilletRequest& request) {
        const auto result = filletEdges(box, request);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::InvalidArgument);
        return result.error().message;
    };
    CHECK(message(roundEdges({}, 5_mm)) == "fillet: a fillet needs at least one edge");
    for (const double mm : {0.0, -5.0, std::nan(""), std::numeric_limits<double>::infinity(),
                            -std::numeric_limits<double>::infinity()}) {
        CAPTURE(mm);
        CHECK_THAT(message(roundEdges({alongX(0, 20)}, mm * units::mm)),
                   Catch::Matchers::StartsWith("fillet: the fillet radius must be positive and finite, got "));
    }
    CHECK(message(roundEdges({alongX(0, 20), lineSignature(Point3D{7_mm, 0_mm, 20_mm}, Direction3D::unitX().reversed())},
                        5_mm)) ==
          "fillet: edge references 1 and 2 refer to the same line through (0, 0, 20) mm along (1, 0, 0)");
    CHECK(message(roundEdges({EdgeSignature{.curve = EdgeCurve::Circle, .radius = 0_mm}}, 5_mm)) ==
          "fillet: edge reference 1: a circle edge reference needs a positive radius");
    CHECK(errorCode(filletEdges(Body{}, roundEdges({alongX(0, 20)}, 5_mm))) == ErrorCode::FailedPrecondition);
}

TEST_CASE("Fillet_UnresolvableReferencesAreReportedNotGuessed", "[geometry][fillet]") {
    const Body box = block();
    SECTION("no edge on the curve") {
        const auto result = filletEdges(box, roundEdges({alongX(0, 25)}, 5_mm));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::NotFound);
        CHECK(result.error().message ==
              "fillet: edge reference 1 (line through (0, 0, 25) mm along (1, 0, 0)) matches no edge of the body");
    }
    SECTION("two edges on the curve") {
        // A notch through the middle of the edge splits it into two collinear edges.
        const auto notch = makeBox(Point3D{45_mm, -(1_mm), 15_mm}, 10_mm, 10_mm, 10_mm);
        REQUIRE(notch.has_value());
        const auto notched = booleanDifference(box, *notch);
        REQUIRE(notched.has_value());
        CHECK_THAT(refusal(*notched, roundEdges({alongX(0, 20)}, 2_mm)),
                   ContainsSubstring("is ambiguous: 2 edges of the body lie on it"));
    }
    SECTION("a seam, which lies on one face") {
        const auto rod = makeCylinder(15_mm, 40_mm);
        REQUIRE(rod.has_value());
        CHECK_THAT(refusal(*rod, roundEdges({lineSignature(Point3D{15_mm, 0_mm, 0_mm}, Direction3D::unitZ())}, 1_mm)),
                   ContainsSubstring("bounds 1 face(s); a fillet needs an edge between two faces"));
    }
}

TEST_CASE("Fillet_RefusesRadiiThatDoNotFitBeforeTheKernel", "[geometry][fillet]") {
    // Every refused case here crashed the process when given to the kernel
    // (OCCT 8.0.1, GCC 16.1 MinGW); see docs/verification/P11-FEAT-003.
    const Body box = block();
    SECTION("a radius equal to or larger than the face it runs across") {
        CHECK(refusal(box, roundEdges({alongX(0, 20)}, 25_mm)) ==
              "fillet: edge reference 1 (line through (0, 0, 20) mm along (1, 0, 0)) does not fit: its fillet needs "
              "25 mm on a face next to the edge, which leaves only 20 mm (a fillet must leave at least 0.001 mm)");
        CHECK_THAT(refusal(box, roundEdges({alongX(0, 20)}, 20_mm)), ContainsSubstring("needs 20 mm on a face"));
    }
    SECTION("two fillets that each fit but meet across a thin face") {
        const auto plate = makeBox(100_mm, 50_mm, 2_mm);
        REQUIRE(plate.has_value());
        CHECK_THAT(refusal(*plate, roundEdges({alongX(0, 2), alongX(0, 0)}, 1_mm)),
                   ContainsSubstring("do not fit together: their fillets need 1 mm and 1 mm on a face they share, "
                                     "where the edges are only 2 mm apart"));
        const Body both = requireFillet(*plate, roundEdges({alongX(0, 2), alongX(0, 0)}, 0.9_mm));
        CHECK_THAT(requireProperties(both).volume.in(units::mm3), WithinRel(10000.0 - 2 * 100.0 * corner(0.9), kRelTight));
    }
    SECTION("two fillets from opposite sides of the top face") {
        const auto tall = makeBox(100_mm, 50_mm, 30_mm);
        REQUIRE(tall.has_value());
        CHECK_THAT(refusal(*tall, roundEdges({alongX(0, 30), alongX(50, 30)}, 26_mm)),
                   ContainsSubstring("their fillets need 26 mm and 26 mm on a face they share, where the edges are "
                                     "only 50 mm apart"));
    }
    SECTION("a rim fillet as wide as the disc") {
        // The kernel would build r = R, but the check leaves 0.001 mm.
        const auto rod = makeCylinder(15_mm, 40_mm);
        REQUIRE(rod.has_value());
        const auto rim = circleSignature(Point3D{0_mm, 0_mm, 40_mm}, Direction3D::unitZ(), 15_mm);
        REQUIRE(rim.has_value());
        for (const Length r : {15_mm, 16_mm}) {
            CHECK_THAT(refusal(*rod, roundEdges({*rim}, r)), ContainsSubstring("which leaves only 15 mm"));
        }
    }
    SECTION("a concave fillet wider than the faces it fills") {
        const Body l = prism({{0, 0}, {50, 0}, {50, 20}, {20, 20}, {20, 50}, {0, 50}}, 20);
        CHECK_THAT(refusal(l, roundEdges({alongZ(20, 20)}, 30_mm)), ContainsSubstring("which leaves only 30 mm"));
    }
    SECTION("a smooth chain whose sides meet across the face") {
        CHECK_THAT(refusal(stadium(20), roundEdges({alongX(-5, 20)}, 5.5_mm)),
                   ContainsSubstring("an edge that joins edge reference 1"));
    }
    SECTION("just inside the limit, the fillet is built") {
        const Body deep = requireFillet(box, roundEdges({alongX(0, 20)}, 19.9_mm));
        CHECK_THAT(requireProperties(deep).volume.in(units::mm3), WithinRel(100000.0 - 100.0 * corner(19.9), kRelTight));
    }
    CHECK_THAT(requireProperties(box).volume.in(units::mm3), WithinRel(100000.0, kRelTight)); // input untouched
}
