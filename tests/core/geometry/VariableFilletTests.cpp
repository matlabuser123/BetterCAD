#include "GeometryTestSupport.hpp"
#include "TestHelpers.hpp"
#include "support/VariableFilletModels.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/geometry/Fillet.hpp>
#include <bettercad/core/geometry/Primitives.hpp>
#include <bettercad/core/geometry/Profile.hpp>
#include <bettercad/core/geometry/Sweeps.hpp>
#include <bettercad/core/geometry/VariableFillet.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::geometry;
using namespace bettercad::literals;
using bettercad::test::errorCode;
using bettercad::test::kHalfRoot2;
using bettercad::test::Part;
using bettercad::test::printOf;
using bettercad::test::radiusStations;
using bettercad::test::ReferenceLaw;
using bettercad::test::RoundedEdge;
using bettercad::test::Stations;
using bettercad::test::Vec3;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// P12-FEAT-006: variable-radius fillets of straight edges between planes,
// against the references of support/VariableFilletModels.hpp.

namespace {

constexpr double pi = std::numbers::pi;
// Volumes of bodies with variable-radius (B-spline) fillet faces come from
// the kernel's adaptive integration at a relative tolerance of 1e-10
// (Body::massProperties()); the kernel probes measured 1.2e-9 against the
// exact integrals (docs/verification/P12-FEAT-006/kernel-probe). The
// removed volume is a small part of the body, so the body's volume is
// checked relative to the body. Centres move by the same integration error
// times the body's size: 1e-9 x 100 mm.
constexpr double kRelVolume = 1e-9;
constexpr double kTolCentreMm = 1e-7;
// The kernel's bounds of B-spline faces and edges are their exact bounds
// padded by its precision, 1e-7 mm, whatever tolerance is asked for
// (GeomBndLib_BSplineSurface::BoxOptimal enlarges by
// max(tolerance, Precision::Confusion())); a fillet's faces touch the body's
// extremes, so its bounds may be 1e-7 mm loose, plus rounding.
constexpr double kTolBounds = 2e-7;
// Areas of planes bounded by a fillet's contact edges come from the kernel's
// default surface integration (geometry::listFaces()), measured 1.85e-6
// relative off (docs/verification/P12-FEAT-006/kernel-probe/face-area-probe.log;
// with a 1e-10 tolerance the same integration is exact to 2e-16).
constexpr double kRelFaceArea = 1e-5;
// Bodies whose fillets are all constant keep elementary faces (P11).
constexpr double kRelTight = bettercad::test::kRelTight;

Body require(const Result<Body>& body) {
    if (!body) {
        FAIL(body.error().message);
    }
    return *body;
}

Point2D p(double u, double v) {
    return Point2D{u * units::mm, v * units::mm};
}

/// A prism over a polygon in the XY plane from z = 0 to @p height; its top is
/// named @p top, so that names can be seen to be carried.
Body prism(const std::vector<std::pair<double, double>>& corners, double height,
           std::optional<FaceName> top = std::nullopt) {
    ProfileLoop loop;
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const auto& [u0, v0] = corners[i];
        const auto& [u1, v1] = corners[(i + 1) % corners.size()];
        loop.segments.emplace_back(LineSegment2D{p(u0, v0), p(u1, v1)});
    }
    const PlanarRegion region{.plane = Frame3D::xy(), .outer = loop, .holes = {}};
    return require(makePrism(region, 0_mm, height * units::mm, [top](const SweptFace& face) -> std::optional<FaceName> {
        if (face.kind == SweptFace::Kind::Last) {
            return top;
        }
        return std::nullopt;
    }));
}

EdgeSignature line(const Vec3& point, const Direction3D& direction) {
    return lineSignature(Point3D{point[0] * units::mm, point[1] * units::mm, point[2] * units::mm}, direction);
}

VariableFilletRequest request(const EdgeSignature& edge, const Stations& stations) {
    return {.edges = {{.edge = edge, .stations = radiusStations(stations)}}};
}

/// The body's volume, centre and bounds against the parts' and bounds.
void checkShape(const Body& body, const std::vector<Part>& parts, const Vec3& lower, const Vec3& upper,
                double rel = kRelVolume, double centreTol = kTolCentreMm) {
    const auto properties = body.massProperties();
    REQUIRE(properties.has_value());
    const Vec3 centre = bettercad::test::weightedCentre(parts);
    CHECK(body.isValid());
    CHECK(body.topology().solids == 1);
    CHECK_THAT(properties->volume.in(units::mm3), WithinRel(bettercad::test::volumeOf(parts), rel));
    CHECK_THAT(properties->centerOfMass.x.in(units::mm), WithinAbs(centre[0], centreTol));
    CHECK_THAT(properties->centerOfMass.y.in(units::mm), WithinAbs(centre[1], centreTol));
    CHECK_THAT(properties->centerOfMass.z.in(units::mm), WithinAbs(centre[2], centreTol));
    const auto box = body.boundingBox();
    REQUIRE(box.has_value());
    CHECK_THAT(box->min.x.in(units::mm), WithinAbs(lower[0], kTolBounds));
    CHECK_THAT(box->min.y.in(units::mm), WithinAbs(lower[1], kTolBounds));
    CHECK_THAT(box->min.z.in(units::mm), WithinAbs(lower[2], kTolBounds));
    CHECK_THAT(box->max.x.in(units::mm), WithinAbs(upper[0], kTolBounds));
    CHECK_THAT(box->max.y.in(units::mm), WithinAbs(upper[1], kTolBounds));
    CHECK_THAT(box->max.z.in(units::mm), WithinAbs(upper[2], kTolBounds));
}

std::string text(const Stations& stations) {
    std::string result;
    for (const auto& [u, r] : stations) {
        result += std::format("({}, {} mm) ", u, r);
    }
    return result;
}

std::string failure(const Result<Body>& result) {
    REQUIRE_FALSE(result.has_value());
    return result.error().message;
}

/// Where the reference law is highest (@p highest) or lowest between
/// @p from and @p to: a scan in 20000 steps, then golden-section search
/// around the best step.
std::pair<double, double> extreme(const ReferenceLaw& law, double from, double to, bool highest) {
    const auto better = [&](double a, double b) { return highest ? a > b : a < b; };
    double at = from;
    constexpr int kSteps = 20000;
    for (int i = 1; i <= kSteps; ++i) {
        const double u = from + (to - from) * i / kSteps;
        if (better(law(u), law(at))) {
            at = u;
        }
    }
    double a = std::max(from, at - (to - from) / kSteps);
    double b = std::min(to, at + (to - from) / kSteps);
    const double g = (std::sqrt(5.0) - 1.0) / 2.0;
    for (int i = 0; i < 200; ++i) {
        const double c = b - g * (b - a);
        const double d = a + g * (b - a);
        if (better(law(c), law(d))) {
            b = d;
        } else {
            a = c;
        }
    }
    const double u = (a + b) / 2.0;
    return {u, law(u)};
}

} // namespace

TEST_CASE("VariableFillet_RequestsAreValidated", "[geometry][fillet][variable][p12]") {
    const EdgeSignature edge = line({0, 0, 0}, Direction3D::unitX());
    const auto message = [](const VariableFilletRequest& r, StationCheck check = StationCheck::Complete) {
        const auto valid = validate(r, check);
        REQUIRE_FALSE(valid.has_value());
        CHECK(valid.error().code == ErrorCode::InvalidArgument);
        return valid.error().message;
    };

    SECTION("well-formed requests pass") {
        CHECK(validate(request(edge, {{0.0, 3.0}, {1.0, 8.0}})).has_value());
        CHECK(validate(request(edge, {{0.0, 8.0}, {1.0, 3.0}})).has_value());
        CHECK(validate(request(edge, {{0.0, 5.0}, {0.4, 5.0}, {1.0, 5.0}})).has_value());
        CHECK(validate(request(edge, {{0.0, 3.0}, {0.4, 4.0}, {0.7, 6.0}, {1.0, 8.0}})).has_value());
        CHECK(validate(request(edge, {{0.0, 3.0}, {0.25, 4.0}, {0.5, 6.0}, {0.75, 4.0}, {1.0, 3.0}})).has_value());
    }

    SECTION("edges") {
        CHECK(message({}) == "a variable-radius fillet needs at least one edge");
        CHECK(message({.edges = {{.edge = edge, .stations = radiusStations({{0.0, 3.0}, {1.0, 3.0}})},
                                 {.edge = line({5, 0, 0}, Direction3D::unitX()),
                                  .stations = radiusStations({{0.0, 3.0}, {1.0, 3.0}})}}}) ==
              "edge references 1 and 2 refer to the same line through (0, 0, 0) mm along (1, 0, 0)");
        const auto circle = circleSignature(Point3D{}, Direction3D::unitZ(), 10_mm);
        REQUIRE(circle.has_value());
        CHECK(message(request(*circle, {{0.0, 3.0}, {1.0, 3.0}})) ==
              "edge reference 1 (circle around (0, 0, 0) mm with axis (0, 0, 1) and radius 10 mm) is not "
              "straight; a variable-radius fillet rounds straight edges only");
    }

    SECTION("stations") {
        CHECK(message(request(edge, {{0.0, 3.0}})) ==
              "edge reference 1: a variable-radius fillet needs two or more radius stations on each edge, got 1");
        CHECK(message(request(edge, {})) ==
              "edge reference 1: a variable-radius fillet needs two or more radius stations on each edge, got 0");
        CHECK(message(request(edge, {{0.0, 3.0}, {std::numeric_limits<double>::quiet_NaN(), 4.0}, {1.0, 3.0}})) ==
              "edge reference 1, station 2: the position must be finite, got nan");
        CHECK(message(request(edge, {{0.1, 3.0}, {1.0, 3.0}})) ==
              "edge reference 1: the first station must be at position 0, got 0.1");
        CHECK(message(request(edge, {{0.0, 3.0}, {0.9, 3.0}})) ==
              "edge reference 1: the last station must be at position 1, got 0.9");
        CHECK(message(request(edge, {{0.0, 3.0}, {0.5, 4.0}, {0.4, 4.0}, {1.0, 3.0}})) ==
              "edge reference 1: station 3 (at 0.4) does not follow station 2 (at 0.5); stations are listed by "
              "increasing position, at least 1e-06 apart");
        CHECK(message(request(edge, {{0.0, 3.0}, {0.5, 4.0}, {0.5, 4.0}, {1.0, 3.0}})) ==
              "edge reference 1: station 3 (at 0.5) does not follow station 2 (at 0.5); stations are listed by "
              "increasing position, at least 1e-06 apart");
        CHECK(message(request(edge, {{0.0, 3.0}, {0.5, 4.0}, {0.5000005, 4.0}, {1.0, 3.0}})) ==
              "edge reference 1: station 3 (at 0.5000005) does not follow station 2 (at 0.5); stations are listed "
              "by increasing position, at least 1e-06 apart");
        CHECK(message(request(edge, {{0.0, 3.0}, {1.5, 4.0}, {1.0, 3.0}})) ==
              "edge reference 1: station 3 (at 1) does not follow station 2 (at 1.5); stations are listed by "
              "increasing position, at least 1e-06 apart");
        CHECK(message(request(edge, {{-0.5, 3.0}, {1.0, 3.0}})) ==
              "edge reference 1: the first station must be at position 0, got -0.5");
    }

    SECTION("radii") {
        CHECK(message(request(edge, {{0.0, 3.0}, {1.0, 0.0}})) ==
              "edge reference 1, station 2: the radius must be positive and finite, got 0 mm");
        CHECK(message(request(edge, {{0.0, -1.0}, {1.0, 3.0}})) ==
              "edge reference 1, station 1: the radius must be positive and finite, got -1 mm");
        CHECK(message(request(edge, {{0.0, 3.0}, {0.5, std::numeric_limits<double>::infinity()}, {1.0, 3.0}})) ==
              "edge reference 1, station 2: the radius must be positive and finite, got inf mm");
        CHECK(message(request(edge, {{0.0, 3.0}, {1.0, std::numeric_limits<double>::quiet_NaN()}})) ==
              "edge reference 1, station 2: the radius must be positive and finite, got nan mm");
        // Radii the kernel would take as one, without being one.
        CHECK(message(request(edge, {{0.0, 3.0}, {1.0, 3.0000004}})) ==
              "edge reference 1: the station radii differ by only 4e-07 mm; give them one radius, or radii at least "
              "1e-06 mm apart");
    }

    SECTION("laws that leave their stations' range, measured on the reference law") {
        struct Case {
            Stations stations;
            std::size_t span; // 1-based: between stations span and span + 1
            bool rises;
        };
        // The investigation's overshoot cases (docs/verification/P12-FEAT-006;
        // the first span that leaves its range is reported), a slight rise
        // above a peak, a rise of 4.4e-6 mm, and a bulge over equal radii.
        const std::vector<Case> cases{
            {{{0.0, 5.0}, {0.4, 5.0}, {0.6, 36.0}, {1.0, 36.0}}, 1, false},
            {{{0.0, 5.0}, {0.4, 5.0}, {0.6, 20.0}, {1.0, 20.0}}, 1, false},
            {{{0.0, 3.0}, {0.5, 6.0}, {1.0, 4.0}}, 2, true},
            {{{0.0, 3.0}, {0.3, 7.0}, {0.5, 5.0}, {1.0, 4.0}}, 2, true},
            {{{0.0, 3.0}, {0.5, 8.0}, {1.0, 8.0}}, 2, true},
        };
        for (const Case& c : cases) {
            const ReferenceLaw law(c.stations);
            const auto& [u0, r0] = c.stations[c.span - 1];
            const auto& [u1, r1] = c.stations[c.span];
            const auto [at, radius] = extreme(law, u0, u1, c.rises);
            CAPTURE(at, radius);
            CHECK((c.rises ? radius > std::max(r0, r1) : radius < std::min(r0, r1)));
            const std::string expected = std::format(
                "edge reference 1: between stations {} and {} ({:.6g} mm at {}, {:.6g} mm at {}) the radius would "
                "{} to {:.6g} mm at {:.6g}; a variable radius must stay between the radii of the stations on either "
                "side",
                c.span, c.span + 1, r0, u0, r1, u1, c.rises ? "rise" : "fall", radius, at);
            CHECK(message(request(edge, c.stations)) == expected);
            // Without the law they pass.
            CHECK(validate(request(edge, c.stations), StationCheck::WithoutLaw).has_value());
        }
        // The first case: between stations of 5 and 36 mm the law runs from
        // -3.23 to 44.23 mm (the investigation sampled it on a grid). The
        // extremes, from the spline's slope solved exactly in rational
        // arithmetic: -3.2348900088144264 at 0.24711258399416494 and
        // 44.234890008814426 at 0.75288741600583506.
        const ReferenceLaw first(cases.front().stations);
        const auto low = extreme(first, 0.0, 0.4, false);
        const auto high = extreme(first, 0.6, 1.0, true);
        CHECK_THAT(low.second, WithinAbs(-3.2348900088144264, 1e-9));
        CHECK_THAT(low.first, WithinAbs(0.24711258399416494, 1e-6));
        CHECK_THAT(high.second, WithinAbs(44.234890008814426, 1e-9));
        CHECK_THAT(high.first, WithinAbs(0.75288741600583506, 1e-6));
    }

    SECTION("the radius law") {
        // Two stations: the closed form a + (b - a) phi(u), and phi(1/4) = 11/56.
        CHECK_THAT(bettercad::test::twoStationShape(0.25), WithinAbs(11.0 / 56.0, 1e-15));
        for (const auto& [a, b] : {std::pair{3.0, 8.0}, std::pair{8.0, 3.0}, std::pair{2.5, 12.0}}) {
            const auto stations = radiusStations({{0.0, a}, {1.0, b}});
            for (int i = 0; i <= 16; ++i) {
                const double u = i / 16.0;
                const auto r = radiusAt(stations, u);
                REQUIRE(r.has_value());
                CHECK_THAT(r->in(units::mm), WithinAbs(a + (b - a) * bettercad::test::twoStationShape(u), 1e-12));
            }
        }
        // More stations: the Hermite-form reference.
        for (const Stations& s : {Stations{{0.0, 3.0}, {0.4, 4.0}, {0.7, 6.0}, {1.0, 8.0}},
                                  Stations{{0.0, 2.0}, {0.3, 4.0}, {1.0, 7.0}},
                                  Stations{{0.0, 6.0}, {0.5, 4.0}, {1.0, 6.0}}}) {
            const ReferenceLaw law(s);
            const auto stations = radiusStations(s);
            for (int i = 0; i <= 40; ++i) {
                const double u = i / 40.0;
                const auto r = radiusAt(stations, u);
                REQUIRE(r.has_value());
                CHECK_THAT(r->in(units::mm), WithinAbs(law(u), 1e-12));
            }
            for (const auto& [u, radius] : s) {
                CHECK_THAT(radiusAt(stations, u)->in(units::mm), WithinAbs(radius, 1e-12));
            }
        }
        const auto stations = radiusStations({{0.0, 3.0}, {1.0, 8.0}});
        CHECK(radiusAt(stations, 1.5).error().message == "the position must be between 0 and 1, got 1.5");
        CHECK(radiusAt(radiusStations({{0.0, 3.0}}), 0.5).error().message ==
              "the stations: a variable-radius fillet needs two or more radius stations on each edge, got 1");
    }
}

TEST_CASE("VariableFillet_RoundsIsolatedStraightEdges", "[geometry][fillet][variable][p12]") {
    // A 100 x 60 x 40 block, its top named.
    const FaceName top{ObjectId::fromValue(3), FaceSelector{.role = FaceRole::EndCap}};
    const Body block = prism({{0, 0}, {100, 0}, {100, 60}, {0, 60}}, 40, top);
    const Part blockPart = bettercad::test::boxPart({0, 0, 0}, {100, 60, 40});
    // The top front edge (y = 0, z = 40) runs +x; the top back edge
    // (y = 60) runs -x in the kernel (the loop is counter-clockwise).
    const EdgeSignature front = line({0, 0, 40}, Direction3D::unitX());
    const EdgeSignature back = line({0, 60, 40}, Direction3D::unitX());
    const auto frontEdge = [](Stations s) {
        return bettercad::test::boxEdge({0, 0, 40}, {1, 0, 0}, 100, {0, kHalfRoot2, -kHalfRoot2}, std::move(s));
    };
    const auto backEdge = [](Stations s) {
        return bettercad::test::boxEdge({0, 60, 40}, {1, 0, 0}, 100, {0, -kHalfRoot2, -kHalfRoot2}, std::move(s));
    };
    const auto topArea = [](const Body& body, const FaceName& name) {
        const auto faces = findNamedFaces(body, name);
        REQUIRE(faces.has_value());
        REQUIRE(faces->size() == 1);
        return faces->front().area.in(units::mm2);
    };

    SECTION("increasing, decreasing and constant radii on one edge") {
        for (const Stations& s : {Stations{{0.0, 3.0}, {1.0, 8.0}}, Stations{{0.0, 8.0}, {1.0, 3.0}},
                                  Stations{{0.0, 2.5}, {1.0, 12.0}}}) {
            CAPTURE(text(s));
            const Body rounded = require(variableFilletEdges(block, request(front, s)));
            const RoundedEdge e = frontEdge(s);
            checkShape(rounded, {blockPart, e.part()}, {0, 0, 0}, {100, 60, 40});
            // One face more: the fillet.
            CHECK(rounded.topology().faces == 7);
            // The top keeps its name and loses the strip up to the contact line.
            CHECK_THAT(topArea(rounded, top), WithinRel(6000.0 - e.strip(), kRelFaceArea));
        }
        // Equal stations: a constant radius, the kernel's exact cylinder.
        const Stations flat{{0.0, 4.0}, {0.4, 4.0}, {1.0, 4.0}};
        const Body rounded = require(variableFilletEdges(block, request(front, flat)));
        checkShape(rounded, {blockPart, frontEdge(flat).part()}, {0, 0, 0}, {100, 60, 40}, kRelTight, 1e-9);
        const auto constant = filletEdges(block, {.edges = {front}, .radius = 4_mm});
        REQUIRE(constant.has_value());
        CHECK(printOf(rounded) == printOf(*constant));
    }

    SECTION("several stations, on edges the kernel runs either way") {
        const Stations rising{{0.0, 3.0}, {0.4, 4.0}, {0.7, 6.0}, {1.0, 8.0}};
        const Stations uneven{{0.0, 2.0}, {0.3, 4.0}, {1.0, 7.0}};
        const Stations dip{{0.0, 6.0}, {0.5, 4.0}, {1.0, 6.0}};
        const Stations peak{{0.0, 3.0}, {0.25, 4.0}, {0.5, 6.0}, {0.75, 4.0}, {1.0, 3.0}};
        for (const auto& [edge, reference] :
             {std::pair{front, frontEdge(rising)}, std::pair{front, frontEdge(uneven)},
              std::pair{back, backEdge(uneven)}, std::pair{back, backEdge(rising)}, std::pair{back, backEdge(dip)},
              std::pair{front, frontEdge(peak)}}) {
            CAPTURE(describe(edge), text(reference.stations));
            const Body rounded = require(variableFilletEdges(block, request(edge, reference.stations)));
            checkShape(rounded, {blockPart, reference.part()}, {0, 0, 0}, {100, 60, 40});
            CHECK_THAT(topArea(rounded, top), WithinRel(6000.0 - reference.strip(), kRelFaceArea));
        }
        // Both edges at once, each with its own stations.
        const Body both = require(variableFilletEdges(
            block, {.edges = {{.edge = front, .stations = radiusStations(rising)},
                              {.edge = back, .stations = radiusStations(uneven)}}}));
        checkShape(both, {blockPart, frontEdge(rising).part(), backEdge(uneven).part()}, {0, 0, 0}, {100, 60, 40});
        CHECK(both.topology().faces == 8);
        CHECK_THAT(topArea(both, top),
                   WithinRel(6000.0 - frontEdge(rising).strip() - backEdge(uneven).strip(), kRelFaceArea));
    }

    SECTION("faces at 120 degrees, and a concave edge") {
        // A hexagonal prism (circumradius 30, 50 high): the vertical edge at
        // (30, 0) joins faces whose outward normals are 60 degrees apart.
        std::vector<std::pair<double, double>> hexagon;
        for (int i = 0; i < 6; ++i) {
            hexagon.emplace_back(30.0 * std::cos(i * pi / 3.0), 30.0 * std::sin(i * pi / 3.0));
        }
        const Body hex = prism(hexagon, 50);
        const double side = 30.0;
        const double hexArea = 1.5 * std::sqrt(3.0) * side * side;
        const Stations peak{{0.0, 4.0}, {0.5, 6.0}, {1.0, 4.0}};
        const RoundedEdge corner{{30, 0, 0}, {0, 0, 1}, 50, {-1, 0, 0}, pi / 3.0, true, peak};
        const Body roundedHex =
            require(variableFilletEdges(hex, request(line({30, 0, 0}, Direction3D::unitZ()), peak)));
        // The fillet removes the hexagon's corner at x = 30. The body now
        // reaches farthest in x on the fillet's crest where the radius is
        // smallest (4 mm, at both ends): the crest lies r / cos(30 deg) - r
        // inside the corner, beyond the contact lines (r tan(30 deg) cos(60 deg)).
        const double crest = 30.0 - 4.0 * (1.0 / std::cos(pi / 6.0) - 1.0);
        checkShape(roundedHex, {{hexArea * 50.0, {0, 0, 25}}, corner.part()},
                   {-30, -side * std::sqrt(3.0) / 2.0, 0}, {crest, side * std::sqrt(3.0) / 2.0, 50});

        // An L prism: the inside vertical edge at (20, 20) is concave; the
        // fillet adds material there.
        const Body l = prism({{0, 0}, {80, 0}, {80, 20}, {20, 20}, {20, 60}, {0, 60}}, 40);
        const Stations falling{{0.0, 6.0}, {1.0, 2.0}};
        const RoundedEdge inside{{20, 20, 0}, {0, 0, 1}, 40, {kHalfRoot2, kHalfRoot2, 0}, pi / 2.0, false, falling};
        const Body roundedL =
            require(variableFilletEdges(l, request(line({20, 20, 0}, Direction3D::unitZ()), falling)));
        checkShape(roundedL,
                   {bettercad::test::boxPart({0, 0, 0}, {80, 20, 40}),
                    bettercad::test::boxPart({0, 20, 0}, {20, 60, 40}), inside.part()},
                   {0, 0, 0}, {80, 60, 40});
        CHECK(inside.part().first > 0.0);
    }

    SECTION("the result is the same every time") {
        const VariableFilletRequest r{.edges = {{.edge = front, .stations = radiusStations({{0.0, 3.0}, {1.0, 8.0}})},
                                                {.edge = back, .stations = radiusStations({{0.0, 2.0}, {0.3, 4.0},
                                                                                           {1.0, 7.0}})}}};
        const Body first = require(variableFilletEdges(block, r));
        const Body second = require(variableFilletEdges(block, r));
        const Body fresh = require(variableFilletEdges(prism({{0, 0}, {100, 0}, {100, 60}, {0, 60}}, 40, top), r));
        CHECK(printOf(first) == printOf(second));
        CHECK(printOf(first) == printOf(fresh));
        // The input is untouched.
        CHECK(block.topology().faces == 6);
    }
}

TEST_CASE("VariableFillet_RefusesEdgesItCannotVerify", "[geometry][fillet][variable][p12]") {
    const Body block = prism({{0, 0}, {100, 0}, {100, 60}, {0, 60}}, 40);
    const EdgeSignature front = line({0, 0, 40}, Direction3D::unitX());
    const Stations stations{{0.0, 3.0}, {1.0, 8.0}};
    const auto code = [](const Result<Body>& result) {
        REQUIRE_FALSE(result.has_value());
        return result.error().code;
    };

    SECTION("requests and bodies") {
        CHECK(code(variableFilletEdges(Body{}, request(front, stations))) == ErrorCode::FailedPrecondition);
        CHECK(failure(variableFilletEdges(Body{}, request(front, stations))) ==
              "variable-radius fillet: the body is empty");
        const auto bad = variableFilletEdges(block, request(front, {{0.0, 3.0}, {0.5, 8.0}, {1.0, 8.0}}));
        CHECK(code(bad) == ErrorCode::InvalidArgument);
        CHECK(failure(bad).starts_with("variable-radius fillet: edge reference 1: between stations 2 and 3"));
        const auto missing = variableFilletEdges(block, request(line({0, 0, 41}, Direction3D::unitX()), stations));
        CHECK(code(missing) == ErrorCode::NotFound);
        CHECK(failure(missing) == "variable-radius fillet: edge reference 1 (line through (0, 0, 41) mm along "
                                  "(1, 0, 0)) matches no edge of the body");
    }

    SECTION("edges that meet, in one request") {
        const auto corner = variableFilletEdges(
            block, {.edges = {{.edge = front, .stations = radiusStations(stations)},
                              {.edge = line({100, 0, 0}, Direction3D::unitZ()),
                               .stations = radiusStations({{0.0, 2.0}, {1.0, 3.0}})}}});
        CHECK(code(corner) == ErrorCode::FailedPrecondition);
        CHECK(failure(corner) == "variable-radius fillet: edge reference 2 (line through (100, 0, 0) mm along "
                                 "(0, 0, 1)) meets edge reference 1 (line through (0, 0, 40) mm along (1, 0, 0)) at "
                                 "a vertex; a variable-radius fillet rounds edges that meet no other edge it rounds");
    }

    SECTION("an edge that continues smoothly into another") {
        // Round the vertical edge at (100, 0) first: the top front edge then
        // runs on into the round's top arc.
        const Body rounded =
            require(filletEdges(block, {.edges = {line({100, 0, 0}, Direction3D::unitZ())}, .radius = 10_mm}));
        const auto chained = variableFilletEdges(rounded, request(front, stations));
        CHECK(code(chained) == ErrorCode::FailedPrecondition);
        CHECK(failure(chained) == "variable-radius fillet: edge reference 1 (line through (0, 0, 40) mm along "
                                  "(1, 0, 0)) continues smoothly into 2 other edge(s); a variable-radius fillet "
                                  "rounds single edges only");
    }

    SECTION("an edge that is not between two planes") {
        // A D-shaped prism: its straight vertical edges join the flat side to
        // the round one.
        ProfileLoop loop;
        loop.segments.emplace_back(LineSegment2D{p(-20, 0), p(20, 0)});
        loop.segments.emplace_back(ArcSegment2D{.center = p(0, 0), .start = p(20, 0), .end = p(-20, 0)});
        const Body d = require(makePrism({.plane = Frame3D::xy(), .outer = loop, .holes = {}}, 0_mm, 30_mm));
        const auto curved = variableFilletEdges(d, request(line({20, 0, 0}, Direction3D::unitZ()), stations));
        CHECK(code(curved) == ErrorCode::FailedPrecondition);
        CHECK(failure(curved) == "variable-radius fillet: edge reference 1 (line through (20, 0, 0) mm along "
                                 "(0, 0, 1)) is not between two planar faces; a variable-radius fillet rounds edges "
                                 "between planes only");
    }

    SECTION("radii too large for the faces, anywhere along the edge") {
        // 45 mm on a 40 mm face: the investigation's valid-but-wrong case.
        const auto tooLarge = variableFilletEdges(block, request(front, {{0.0, 3.0}, {1.0, 45.0}}));
        CHECK(code(tooLarge) == ErrorCode::FailedPrecondition);
        CHECK(failure(tooLarge).starts_with("variable-radius fillet: edge reference 1 (line through (0, 0, 40) mm "
                                            "along (1, 0, 0)) does not fit: its variable-radius fillet needs 45 mm"));
        // The largest radius counts, not the first.
        const auto middle = variableFilletEdges(block, request(front, {{0.0, 3.0}, {0.5, 41.0}, {1.0, 3.0}}));
        CHECK(code(middle) == ErrorCode::FailedPrecondition);
        CHECK(failure(middle).starts_with("variable-radius fillet: edge reference 1 (line through (0, 0, 40) mm "
                                          "along (1, 0, 0)) does not fit: its variable-radius fillet needs 41 mm"));
        // Just inside the room: 39.9 mm on the 40 mm face builds.
        CHECK(variableFilletEdges(block, request(front, {{0.0, 3.0}, {1.0, 39.9}})).has_value());
    }

    SECTION("stations too close on a short edge") {
        // A 0.5 mm edge: stations 1e-6 apart in position are 5e-7 mm apart.
        const Body sliver = prism({{0, 0}, {0.5, 0}, {0.5, 60}, {0, 60}}, 40);
        const auto close = variableFilletEdges(
            sliver, request(line({0, 0, 40}, Direction3D::unitX()), {{0.0, 0.1}, {0.5, 0.1}, {0.500001, 0.1},
                                                                      {1.0, 0.1}}));
        CHECK(code(close) == ErrorCode::FailedPrecondition);
        CHECK(failure(close) == "variable-radius fillet: edge reference 1 (line through (0, 0, 40) mm along "
                                "(1, 0, 0)) has stations 2 and 3 only 5e-07 mm apart on it; they must be at least "
                                "1e-06 mm apart");
    }
}
