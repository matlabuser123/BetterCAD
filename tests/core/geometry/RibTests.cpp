#include "GeometryTestSupport.hpp"
#include "TestHelpers.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/geometry/Profile.hpp>
#include <bettercad/core/geometry/Rib.hpp>
#include <bettercad/core/geometry/Split.hpp>
#include <bettercad/core/geometry/Sweeps.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::geometry;
using namespace bettercad::literals;
using Catch::Matchers::StartsWith;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// P12-FEAT-005: ribs. An L bracket, its section in the XZ plane (local u = x,
// v = z) extruded 40 mm along -Y: a floor 80 x 10 and a wall 10 x 60 with its
// inside corner at (10, 10). The ribs lie on the plane y = -20 and fill that
// corner; their sections are written out below (see support/RibModels.hpp
// for the feature-level models).

namespace {

constexpr double kRel = bettercad::test::kRelTight;
constexpr double pi = std::numbers::pi;

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

Point2D p(double u, double v) {
    return Point2D{u * units::mm, v * units::mm};
}

LineSegment2D line(double u0, double v0, double u1, double v1) {
    return LineSegment2D{p(u0, v0), p(u1, v1)};
}

const FaceName kBack{ObjectId::fromValue(7), FaceSelector{.role = FaceRole::EndCap}};

Body bracket() {
    ProfileLoop loop;
    for (const auto& segment : {line(0, 0, 80, 0), line(80, 0, 80, 10), line(80, 10, 10, 10), line(10, 10, 10, 60),
                                line(10, 60, 0, 60), line(0, 60, 0, 0)}) {
        loop.segments.emplace_back(segment);
    }
    const PlanarRegion region{.plane = Frame3D::xz(), .outer = loop, .holes = {}};
    // The prism's last face is its back (y = -40), named to be carried.
    return require(makePrism(region, 0_mm, 40_mm, [](const SweptFace& face) -> std::optional<FaceName> {
        if (face.kind == SweptFace::Kind::Last) {
            return kBack;
        }
        return std::nullopt;
    }));
}

/// The rib plane, y = -20, with the bracket's local axes (u = x, v = z).
Frame3D ribPlane(double y = -20.0) {
    return Frame3D::create(Point3D{0_mm, y * units::mm, 0_mm}, Direction3D::unitY().reversed(), Direction3D::unitX())
        .value();
}

RibRequest rib(std::vector<ProfileSegment> segments, Length thickness = 4_mm) {
    return RibRequest{.profile = PlanarPath{ribPlane(), std::move(segments)}, .thickness = thickness};
}

const FaceName kWall{ObjectId::fromValue(9), FaceSelector{.role = FaceRole::StartCap}};
const FaceName kOtherWall{ObjectId::fromValue(9), FaceSelector{.role = FaceRole::EndCap}};

SweptFaceNamer ribNamer() {
    return [](const SweptFace& face) -> std::optional<FaceName> {
        switch (face.kind) {
        case SweptFace::Kind::First:
            return kWall;
        case SweptFace::Kind::Last:
            return kOtherWall;
        case SweptFace::Kind::Side:
            break;
        }
        return FaceName{ObjectId::fromValue(9),
                        FaceSelector{.role = FaceRole::Side, .entity = EntityId::fromValue(20 + face.segment)}};
    };
}

double namedArea(const Body& body, const FaceName& name) {
    const auto faces = findNamedFaces(body, name);
    REQUIRE(faces.has_value());
    double area = 0.0;
    for (const FaceInfo& face : *faces) {
        area += face.area.in(units::mm2);
    }
    return area;
}

const double kBracket = 80.0 * 10.0 * 40.0 + 10.0 * 50.0 * 40.0;

} // namespace

TEST_CASE("Rib_RequestsAreValidated", "[core][geometry][rib][p12]") {
    CHECK(toString(RibPlacement::Symmetric) == "symmetric");
    CHECK(toString(RibPlacement::AlongNormal) == "along normal");
    CHECK(toString(RibPlacement::AgainstNormal) == "against normal");
    const auto message = [](const RibRequest& request) {
        const auto valid = validate(request);
        REQUIRE_FALSE(valid.has_value());
        CHECK(valid.error().code == ErrorCode::InvalidArgument);
        return valid.error().message;
    };
    REQUIRE(validate(rib({line(40, 10, 10, 40)})).has_value());
    CHECK(message(rib({})) == "a rib needs a profile of one or more segments");
    CHECK(message(rib({CircleSegment2D{p(0, 0), 5_mm}})) ==
          "profile segment 1 is a full circle; a rib profile is an open chain of lines, arcs and open splines");
    CHECK(message(rib({line(0, 0, 10, 0), line(10, 0, 10, 0)})) == "profile segment 2 has zero length");
    CHECK(message(rib({line(0, 0, 10, 0), line(10, 1, 10, 10)})) == "profile segment 1 does not meet the next");
    CHECK(message(rib({line(0, 0, 10, 0), line(10, 0, 10, 10), line(10, 10, 0, 0)})) ==
          "the profile is closed; a rib profile is open");
    CHECK(message(rib({ArcSegment2D{p(0, 0), p(10, 0), p(0, 11), true}})) ==
          "profile segment 1 is an arc whose ends are not on one circle");
    CHECK(message(rib({SplineSegment2D{{p(0, 0), p(1, 1)}, 3, false}})) ==
          "profile segment 1: a spline of degree 3 needs at least 4 poles, got 2");
    CHECK(message(rib({LineSegment2D{Point2D{Length::fromSi(std::nan("")), 0_mm}, p(1, 1)}})) ==
          "profile segment 1 is not finite");
    CHECK(message(rib({line(40, 10, 10, 40)}, 0_mm)) == "the rib thickness must be positive and finite, got 0 mm");
    RibRequest odd = rib({line(40, 10, 10, 40)});
    odd.placement = static_cast<RibPlacement>(7);
    CHECK(message(odd) == "a rib's thickness lies symmetric, along or against the normal");
}

TEST_CASE("Rib_FillsTheCornerOfABracket", "[core][geometry][rib][p12]") {
    const Body body = bracket();
    REQUIRE_THAT(volumeOf(body), WithinRel(kBracket, kRel));
    // A line from floor to wall: the triangle between it and the corner.
    const Body ribbed = require(addRib(body, rib({line(40, 10, 10, 40)}), ribNamer()));
    CHECK(ribbed.isValid());
    CHECK(ribbed.topology().solids == 1);
    CHECK_THAT(volumeOf(ribbed), WithinRel(kBracket + 450.0 * 4.0, kRel));
    const auto props = ribbed.massProperties();
    REQUIRE(props.has_value());
    // The bracket's centre, moved by the rib's (the triangle's centroid,
    // (20, 20) in the plane, at y = -20).
    const double bracketX = (32000.0 * 40.0 + 20000.0 * 5.0) / kBracket;
    const double bracketZ = (32000.0 * 5.0 + 20000.0 * 35.0) / kBracket;
    const double total = kBracket + 1800.0;
    bettercad::test::checkPoint(props->centerOfMass, (kBracket * bracketX + 1800.0 * 20.0) / total,
                                (kBracket * -20.0 + 1800.0 * -20.0) / total,
                                (kBracket * bracketZ + 1800.0 * 20.0) / total);
    // The rib's walls (triangles) and its free side are named; the body keeps
    // its names.
    CHECK_THAT(namedArea(ribbed, kWall), WithinRel(450.0, kRel));
    CHECK_THAT(namedArea(ribbed, kOtherWall), WithinRel(450.0, kRel));
    CHECK_THAT(namedArea(ribbed, FaceName{ObjectId::fromValue(9),
                                          FaceSelector{.role = FaceRole::Side, .entity = EntityId::fromValue(20)}}),
               WithinRel(30.0 * std::numbers::sqrt2 * 4.0, kRel));
    CHECK_THAT(namedArea(ribbed, kBack), WithinRel(1300.0, kRel));
    // The input is untouched.
    CHECK_THAT(volumeOf(body), WithinRel(kBracket, kRel));

    // The same line, reaching into the walls or stopping short of them.
    CHECK_THAT(volumeOf(require(addRib(body, rib({line(45, 5, 5, 45)})))), WithinRel(kBracket + 1800.0, kRel));
    CHECK_THAT(volumeOf(require(addRib(body, rib({line(30, 20, 20, 30)})))), WithinRel(kBracket + 1800.0, kRel));
    // Reversed and flipped fills the same side.
    RibRequest flipped = rib({line(10, 40, 40, 10)});
    flipped.flipped = true;
    CHECK_THAT(volumeOf(require(addRib(body, flipped))), WithinRel(kBracket + 1800.0, kRel));
    // All the thickness on one side: the same volume, moved.
    for (const auto& [placement, y] : {std::pair{RibPlacement::AlongNormal, -22.0},
                                      std::pair{RibPlacement::AgainstNormal, -18.0}}) {
        RibRequest oneSide = rib({line(40, 10, 10, 40)});
        oneSide.placement = placement;
        const Body one = require(addRib(body, oneSide));
        CHECK_THAT(volumeOf(one), WithinRel(kBracket + 1800.0, kRel));
        const auto moved = one.massProperties();
        REQUIRE(moved.has_value());
        CHECK_THAT(moved->centerOfMass.y.in(units::mm),
                   WithinAbs((kBracket * -20.0 + 1800.0 * y) / total, bettercad::test::kPositionToleranceMm));
    }

    // An arc tangent to both walls: the corner less a quarter disc.
    const double arcArea = 900.0 - 225.0 * pi;
    CHECK_THAT(volumeOf(require(addRib(body, rib({ArcSegment2D{p(40, 40), p(40, 10), p(10, 40), false}})))),
               WithinRel(kBracket + 4.0 * arcArea, kRel));
    // Two lines.
    const double twoLines = 0.5 * std::abs((40.0 - 10.0) * (20.0 - 10.0) - (20.0 - 10.0) * (10.0 - 10.0) +
                                           (20.0 - 10.0) * (40.0 - 10.0) - (10.0 - 10.0) * (20.0 - 10.0));
    CHECK_THAT(volumeOf(require(addRib(body, rib({line(40, 10, 20, 20), line(20, 20, 10, 40)})))),
               WithinRel(kBracket + 4.0 * twoLines, kRel));
    // A spline: the region it closes with the walls, by Green's theorem.
    const SplineSegment2D spline{{p(40, 10), p(30, 14), p(22, 22), p(14, 30), p(10, 40)}, 3, false};
    ProfileLoop corner;
    corner.segments = {spline, line(10, 40, 10, 10), line(10, 10, 40, 10)};
    const double splineArea = std::abs(signedArea(corner).in(units::mm2));
    CHECK_THAT(volumeOf(require(addRib(body, rib({spline})))), WithinRel(kBracket + 4.0 * splineArea, 1e-9));
}

TEST_CASE("Rib_FailuresAreStructured", "[core][geometry][rib][p12]") {
    const Body body = bracket();
    const auto fails = [](const Result<Body>& result, ErrorCode code) {
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == code);
        return result.error().message;
    };
    CHECK(fails(addRib(Body{}, rib({line(40, 10, 10, 40)})), ErrorCode::FailedPrecondition) ==
          "rib: the body is empty");
    CHECK(fails(addRib(body, rib({line(40, 10, 10, 40)}, -(1_mm))), ErrorCode::InvalidArgument) ==
          "rib: the rib thickness must be positive and finite, got -1 mm");
    // The other side of the line is open.
    CHECK(fails(addRib(body, rib({line(10, 40, 40, 10)})), ErrorCode::FailedPrecondition) ==
          "rib: the side the rib fills is not closed off by the body: it reaches past the body (fill the other "
          "side, or turn the profile towards the body)");
    // A line inside the wall.
    CHECK(fails(addRib(body, rib({line(3, 20, 3, 30)})), ErrorCode::FailedPrecondition) ==
          "rib: the profile lies inside the body, so there is nothing to fill");
    // A profile that turns back across itself once extended.
    CHECK_THAT(fails(addRib(body, rib({line(40, 10, 20, 30), line(20, 30, 30, 30), line(30, 30, 30, 5)})),
                     ErrorCode::FailedPrecondition),
               StartsWith("rib: the profile, extended along its end tangents, crosses itself ("));
    // A plane beside the bracket: nothing closes the side off.
    RibRequest beside = rib({line(40, 10, 10, 40)});
    beside.profile.plane = ribPlane(20.0);
    CHECK_THAT(fails(addRib(body, beside), ErrorCode::FailedPrecondition),
               StartsWith("rib: the side the rib fills is not closed off by the body"));
}
