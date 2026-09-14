#include "FeatureTestSupport.hpp"
#include "TestHelpers.hpp"

#include <bettercad/features/Profiles.hpp>
#include <bettercad/sketch/Solver.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <numbers>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::sketch;
using bettercad::features::extractRegions;
using bettercad::test::addRectangle;
using bettercad::test::errorCode;
using bettercad::test::require;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;

namespace {

constexpr double pi = std::numbers::pi;
constexpr double kRel = 1e-12;

std::vector<geometry::PlanarRegion> requireRegions(const Sketch& sketch) {
    auto regions = extractRegions(sketch);
    INFO((regions ? std::string{} : regions.error().message));
    REQUIRE(regions.has_value());
    return *regions;
}

double areaMm2(const geometry::PlanarRegion& region) {
    return geometry::regionArea(region).in(units::mm2);
}

} // namespace

TEST_CASE("A rectangle of connected lines is one closed region", "[features][profiles]") {
    Sketch sketch("Sketch1");
    addRectangle(sketch, 0_mm, 0_mm, 100_mm, 50_mm);

    const auto regions = requireRegions(sketch);
    REQUIRE(regions.size() == 1);
    CHECK(regions[0].outer.segments.size() == 4);
    CHECK(regions[0].holes.empty());
    CHECK(regions[0].plane == sketch.placement());
    CHECK_THAT(geometry::signedArea(regions[0].outer).in(units::mm2), WithinRel(5000.0, kRel));
}

TEST_CASE("Loop orientation is normalized to counter-clockwise", "[features][profiles]") {
    Sketch sketch("Sketch1");
    addRectangle(sketch, 0_mm, 0_mm, 100_mm, 50_mm, /*clockwise=*/true);
    const auto regions = requireRegions(sketch);
    REQUIRE(regions.size() == 1);
    CHECK(geometry::signedArea(regions[0].outer) > Area{});
}

TEST_CASE("Lines closed by coincident constraints form a profile once solved", "[features][profiles]") {
    Sketch sketch("Sketch1");
    const EntityId a = require(sketch.addLine(Point2D{0_mm, 0_mm}, Point2D{100_mm, 0_mm}));
    const EntityId b = require(sketch.addLine(Point2D{101_mm, 1_mm}, Point2D{50_mm, 80_mm}));
    const EntityId c = require(sketch.addLine(Point2D{49_mm, 79_mm}, Point2D{1_mm, -1_mm}));
    const auto start = [&](EntityId l) { return std::get<LineEntity>(sketch.findEntity(l)->geometry).start; };
    const auto end = [&](EntityId l) { return std::get<LineEntity>(sketch.findEntity(l)->geometry).end; };
    require(sketch.addCoincident(end(a), start(b)));
    require(sketch.addCoincident(end(b), start(c)));
    require(sketch.addCoincident(end(c), start(a)));

    // Before solving, the ends are 1-2 mm apart: the profile is open.
    CHECK(errorCode(extractRegions(sketch)) == ErrorCode::FailedPrecondition);

    REQUIRE(solve(sketch).solved());
    const auto regions = requireRegions(sketch);
    REQUIRE(regions.size() == 1);
    CHECK(regions[0].outer.segments.size() == 3);
}

TEST_CASE("A circle is a closed region on its own", "[features][profiles]") {
    Sketch sketch("Sketch1");
    require(sketch.addCircle(Point2D{10_mm, 10_mm}, 10_mm));
    const auto regions = requireRegions(sketch);
    REQUIRE(regions.size() == 1);
    CHECK(std::holds_alternative<geometry::CircleSegment2D>(regions[0].outer.segments.front()));
    CHECK_THAT(areaMm2(regions[0]), WithinRel(pi * 100.0, kRel));
}

TEST_CASE("Nested loops become holes; loops inside holes become islands", "[features][profiles]") {
    SECTION("plate with a hole") {
        Sketch sketch("Sketch1");
        addRectangle(sketch, 0_mm, 0_mm, 100_mm, 50_mm);
        require(sketch.addCircle(Point2D{50_mm, 25_mm}, 10_mm));
        const auto regions = requireRegions(sketch);
        REQUIRE(regions.size() == 1);
        REQUIRE(regions[0].holes.size() == 1);
        CHECK(geometry::signedArea(regions[0].holes[0]) < Area{}); // holes run clockwise
        CHECK_THAT(areaMm2(regions[0]), WithinRel(5000.0 - pi * 100.0, kRel));
    }
    SECTION("island inside a hole") {
        Sketch sketch("Sketch1");
        addRectangle(sketch, 0_mm, 0_mm, 100_mm, 100_mm);
        require(sketch.addCircle(Point2D{50_mm, 50_mm}, 30_mm));
        require(sketch.addCircle(Point2D{50_mm, 50_mm}, 10_mm));
        const auto regions = requireRegions(sketch);
        REQUIRE(regions.size() == 2);
        double total = 0.0;
        for (const auto& region : regions) {
            total += areaMm2(region);
        }
        CHECK_THAT(total, WithinRel(10000.0 - pi * 900.0 + pi * 100.0, kRel));
    }
    SECTION("disjoint loops are separate regions") {
        Sketch sketch("Sketch1");
        addRectangle(sketch, 0_mm, 0_mm, 10_mm, 10_mm);
        addRectangle(sketch, 20_mm, 0_mm, 5_mm, 5_mm);
        const auto regions = requireRegions(sketch);
        REQUIRE(regions.size() == 2);
        CHECK(regions[0].holes.empty());
        CHECK(regions[1].holes.empty());
    }
}

TEST_CASE("Profiles may contain arcs", "[features][profiles]") {
    // A slot: two 40 mm lines joined by two half circles of radius 10 mm.
    Sketch sketch("Slot");
    const EntityId p1 = require(sketch.addPoint(Point2D{0_mm, 0_mm}));
    const EntityId p2 = require(sketch.addPoint(Point2D{40_mm, 0_mm}));
    const EntityId p3 = require(sketch.addPoint(Point2D{40_mm, 20_mm}));
    const EntityId p4 = require(sketch.addPoint(Point2D{0_mm, 20_mm}));
    const EntityId c1 = require(sketch.addPoint(Point2D{40_mm, 10_mm}));
    const EntityId c2 = require(sketch.addPoint(Point2D{0_mm, 10_mm}));
    require(sketch.addLine(p1, p2));
    require(sketch.addArc(c1, p2, p3));
    require(sketch.addLine(p3, p4));
    require(sketch.addArc(c2, p4, p1));

    const auto regions = requireRegions(sketch);
    REQUIRE(regions.size() == 1);
    CHECK(regions[0].outer.segments.size() == 4);
    CHECK_THAT(areaMm2(regions[0]), WithinRel(40.0 * 20.0 + pi * 100.0, kRel));
}

TEST_CASE("Construction geometry is not part of profiles", "[features][profiles]") {
    Sketch sketch("Sketch1");
    addRectangle(sketch, 0_mm, 0_mm, 100_mm, 50_mm);
    const EntityId diagonal = require(sketch.addLine(Point2D{0_mm, 0_mm}, Point2D{100_mm, 50_mm}));
    CHECK(errorCode(extractRegions(sketch)) == ErrorCode::FailedPrecondition); // branches
    REQUIRE(sketch.setConstruction(diagonal, true).value());
    CHECK(requireRegions(sketch).size() == 1);
}

TEST_CASE("Open, branching and empty sketches have no valid profile", "[features][profiles]") {
    SECTION("open") {
        Sketch sketch("Sketch1");
        const EntityId a = require(sketch.addPoint(Point2D{0_mm, 0_mm}));
        const EntityId b = require(sketch.addPoint(Point2D{100_mm, 0_mm}));
        const EntityId c = require(sketch.addPoint(Point2D{100_mm, 50_mm}));
        const EntityId d = require(sketch.addPoint(Point2D{0_mm, 50_mm}));
        require(sketch.addLine(a, b));
        require(sketch.addLine(b, c));
        require(sketch.addLine(c, d));
        const auto regions = extractRegions(sketch);
        REQUIRE_FALSE(regions.has_value());
        CHECK(regions.error().code == ErrorCode::FailedPrecondition);
        CHECK_THAT(regions.error().message, ContainsSubstring("open"));
    }
    SECTION("branching") {
        Sketch sketch("Sketch1");
        const EntityId center = require(sketch.addPoint(Point2D{0_mm, 0_mm}));
        for (const Point2D& p : {Point2D{10_mm, 0_mm}, Point2D{0_mm, 10_mm}, Point2D{-10_mm, 0_mm}}) {
            require(sketch.addLine(center, require(sketch.addPoint(p))));
        }
        const auto regions = extractRegions(sketch);
        REQUIRE_FALSE(regions.has_value());
        CHECK_THAT(regions.error().message, ContainsSubstring("branches"));
    }
    SECTION("empty") {
        Sketch sketch("Sketch1");
        require(sketch.addPoint(Point2D{}));
        CHECK(errorCode(extractRegions(sketch)) == ErrorCode::FailedPrecondition);
    }
}
