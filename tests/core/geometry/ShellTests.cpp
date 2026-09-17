#include "GeometryTestSupport.hpp"
#include "TestHelpers.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/geometry/Booleans.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/geometry/Primitives.hpp>
#include <bettercad/core/geometry/Shell.hpp>
#include <bettercad/core/geometry/Split.hpp>
#include <bettercad/core/geometry/Sweeps.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <optional>
#include <vector>

using namespace bettercad;
using namespace bettercad::geometry;
using namespace bettercad::literals;
using bettercad::test::errorCode;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// P12-FEAT-003: hollowing bodies into walls. Expected volumes are sums and
// differences of boxes (see support/ShellModels.hpp for the feature-level
// models).

namespace {

constexpr double kRel = bettercad::test::kRelTight;

Body require(const Result<Body>& body) {
    if (!body) {
        FAIL(body.error().message);
    }
    return *body;
}

double volumeOf(const Body& body) {
    const auto props = body.massProperties();
    REQUIRE(props.has_value());
    return props->volume.in(units::mm3);
}

const FaceName kTop{ObjectId::fromValue(1), FaceSelector{.role = FaceRole::EndCap}};

/// A 100 x 60 x 20 mm box at the origin whose top is named kTop.
Body namedBox() {
    ProfileLoop loop;
    const std::array<Point2D, 4> corners{Point2D{0_mm, 0_mm}, Point2D{100_mm, 0_mm}, Point2D{100_mm, 60_mm},
                                         Point2D{0_mm, 60_mm}};
    for (std::size_t i = 0; i < corners.size(); ++i) {
        loop.segments.emplace_back(LineSegment2D{corners[i], corners[(i + 1) % corners.size()]});
    }
    const PlanarRegion region{.plane = Frame3D::xy(), .outer = loop, .holes = {}};
    return require(makePrism(region, 0_mm, 20_mm, [](const SweptFace& face) -> std::optional<FaceName> {
        if (face.kind == SweptFace::Kind::Last) {
            return kTop;
        }
        return std::nullopt;
    }));
}

/// The faces of @p body that carry @p name, and those that carry none.
struct NameCount {
    std::size_t named = 0;
    double namedArea = 0.0;
    std::size_t unnamed = 0;
};

NameCount countNames(const Body& body, const FaceName& name) {
    const auto faces = listFaces(body);
    REQUIRE(faces.has_value());
    NameCount count;
    for (const FaceInfo& face : *faces) {
        if (std::ranges::binary_search(face.names, name)) {
            ++count.named;
            count.namedArea += face.area.in(units::mm2);
        } else if (face.names.empty()) {
            ++count.unnamed;
        }
    }
    return count;
}

ShellRequest openTop(Length thickness, ShellSide side = ShellSide::Inward) {
    return ShellRequest{.openFaces = {kTop}, .thickness = thickness, .side = side};
}

} // namespace

TEST_CASE("Shell_RequestsAreValidated", "[core][geometry][shell][p12]") {
    CHECK(toString(ShellSide::Inward) == "inward");
    CHECK(toString(ShellSide::Outward) == "outward");
    CHECK(toString(static_cast<ShellSide>(9)) == "unknown");
    const auto message = [](const ShellRequest& request) {
        const auto valid = validate(request);
        REQUIRE_FALSE(valid.has_value());
        CHECK(valid.error().code == ErrorCode::InvalidArgument);
        return valid.error().message;
    };
    REQUIRE(validate(openTop(2_mm)).has_value());
    CHECK(message({.thickness = 2_mm}) == "a shell needs one or more open faces (a closed hollow is not built)");
    CHECK(message({.openFaces = {kTop, FaceName{}}, .thickness = 2_mm}) == "open face 2 must name a valid feature");
    CHECK(message({.openFaces = {FaceName{ObjectId::fromValue(1), {.role = FaceRole::Side}}}, .thickness = 2_mm}) ==
          "open face 1: a side face is named by a valid profile entity");
    CHECK(message({.openFaces = {kTop, kTop}, .thickness = 2_mm}) == "open face 2 repeats an earlier open face");
    CHECK(message(openTop(0_mm)) == "the shell thickness must be positive and finite, got 0 mm");
    CHECK(message(openTop(-(2_mm))) == "the shell thickness must be positive and finite, got -2 mm");
    CHECK(message(openTop(Length::fromSi(std::numeric_limits<double>::infinity()))) ==
          "the shell thickness must be positive and finite, got inf mm");
    CHECK(message(openTop(2_mm, static_cast<ShellSide>(9))) == "a shell's walls lie inward or outward");
}

TEST_CASE("Shell_HollowsABoxAndCarriesItsNames", "[core][geometry][shell][p12]") {
    const Body box = namedBox();
    // Inward: the outside stays; the cavity is 90 x 50 x 15.
    const Body cup = require(shellBody(box, openTop(5_mm)));
    CHECK(cup.isValid());
    CHECK(cup.topology().solids == 1);
    CHECK(cup.topology().faces == 11);
    CHECK_THAT(volumeOf(cup), WithinRel(120000.0 - 90.0 * 50.0 * 15.0, kRel));
    const auto bounds = cup.boundingBox();
    REQUIRE(bounds.has_value());
    bettercad::test::checkPoint(bounds->min, 0.0, 0.0, 0.0);
    bettercad::test::checkPoint(bounds->max, 100.0, 60.0, 20.0);
    // The top's name is on its rim; the five walls are not named.
    NameCount names = countNames(cup, kTop);
    CHECK(names.named == 1);
    CHECK_THAT(names.namedArea, WithinRel(6000.0 - 4500.0, kRel));
    CHECK(names.unnamed == 10);
    // The input is untouched.
    CHECK_THAT(volumeOf(box), WithinRel(120000.0, kRel));

    // Outward: the inside stays; the outside is 110 x 70 x 25, from z = -5.
    const Body casing = require(shellBody(box, openTop(5_mm, ShellSide::Outward)));
    CHECK(casing.topology().faces == 11);
    CHECK_THAT(volumeOf(casing), WithinRel(110.0 * 70.0 * 25.0 - 120000.0, kRel));
    const auto outer = casing.boundingBox();
    REQUIRE(outer.has_value());
    bettercad::test::checkPoint(outer->min, -5.0, -5.0, -5.0);
    bettercad::test::checkPoint(outer->max, 105.0, 65.0, 20.0);
    names = countNames(casing, kTop);
    CHECK(names.named == 1);
    CHECK_THAT(names.namedArea, WithinRel(7700.0 - 6000.0, kRel));

    // The same request gives the same body.
    const Body again = require(shellBody(box, openTop(5_mm)));
    CHECK(volumeOf(again) == volumeOf(cup));
    CHECK(again.topology() == cup.topology());
}

TEST_CASE("Shell_OpensEveryFaceANameIsOn", "[core][geometry][shell][p12]") {
    // A slot across the top (x 45..55, 10 deep) divides it into two faces,
    // both named: both are opened. With 2 mm walls the cavity is two
    // chambers (x 2..43 and 57..98, from z = 2) joined under the slot's
    // floor (x 43..57, z 2..8).
    const Body slotted = require(
        booleanDifference(namedBox(), require(makeBox(Point3D{45_mm, -(1_mm), 10_mm}, 10_mm, 62_mm, 11_mm))));
    REQUIRE(countNames(slotted, kTop).named == 2);
    const Body shelled = require(shellBody(slotted, openTop(2_mm)));
    CHECK(shelled.isValid());
    CHECK(shelled.topology().solids == 1);
    const double cavity = 2.0 * 41.0 * 56.0 * 18.0 + 14.0 * 56.0 * 6.0;
    CHECK_THAT(volumeOf(shelled), WithinRel(114000.0 - cavity, kRel));
    const NameCount names = countNames(shelled, kTop);
    CHECK(names.named == 2);
    CHECK_THAT(names.namedArea, WithinRel(2.0 * (45.0 * 60.0 - 41.0 * 56.0), kRel));
}

TEST_CASE("Shell_FailuresAreStructured", "[core][geometry][shell][p12]") {
    const Body box = namedBox();
    SECTION("empty and invalid inputs") {
        auto empty = shellBody(Body{}, openTop(2_mm));
        REQUIRE_FALSE(empty.has_value());
        CHECK(empty.error().code == ErrorCode::FailedPrecondition);
        CHECK(empty.error().message == "shell: the body is empty");
        auto thin = shellBody(box, openTop(0_mm));
        REQUIRE_FALSE(thin.has_value());
        CHECK(thin.error().code == ErrorCode::InvalidArgument);
        CHECK(thin.error().message == "shell: the shell thickness must be positive and finite, got 0 mm");
    }
    SECTION("an open face the body does not have") {
        const ShellRequest request{
            .openFaces = {kTop, FaceName{ObjectId::fromValue(2), {.role = FaceRole::EndCap}}},
            .thickness = 2_mm};
        auto shelled = shellBody(box, request);
        REQUIRE_FALSE(shelled.has_value());
        CHECK(shelled.error().code == ErrorCode::NotFound);
        CHECK(shelled.error().message == "shell: open face 2 is not a face of the body");
        // A body without names has no faces to open.
        auto plain = shellBody(require(makeBox(10_mm, 10_mm, 10_mm)), openTop(1_mm));
        REQUIRE_FALSE(plain.has_value());
        CHECK(plain.error().message == "shell: open face 1 is not a face of the body");
    }
    SECTION("several solids") {
        const Body halves = require(splitBody(
            box, Frame3D::create(Point3D{50_mm, 0_mm, 0_mm}, Direction3D::unitX(), Direction3D::unitY()).value(),
            SplitKeep::Both));
        auto shelled = shellBody(halves, openTop(2_mm));
        REQUIRE_FALSE(shelled.has_value());
        CHECK(shelled.error().code == ErrorCode::FailedPrecondition);
        CHECK(shelled.error().message == "shell: a shell hollows one solid; the body has 2");
    }
    SECTION("walls too thick for the body") {
        // Half the width (30 mm) closes the cavity; 35 mm walls overlap. The
        // kernel reports success for both; the shell refuses them.
        for (const double t : {30.0, 35.0, 60.0}) {
            CAPTURE(t);
            auto shelled = shellBody(box, openTop(t * units::mm));
            REQUIRE_FALSE(shelled.has_value());
            CHECK(shelled.error().code == ErrorCode::FailedPrecondition);
            CHECK_THAT(shelled.error().message,
                       ContainsSubstring(std::format("shell: the kernel cannot build walls {} mm thick inward on this "
                                                     "body: ",
                                                     t)));
            CHECK_THAT(shelled.error().message,
                       ContainsSubstring("; walls thicker than half the body where it is thinnest, or an inward "
                                         "wall at least as thick as a round it follows, cannot be built"));
        }
        // Outward there is room.
        CHECK(shellBody(box, openTop(35_mm, ShellSide::Outward)).has_value());
    }
}
