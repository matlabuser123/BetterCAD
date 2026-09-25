#include "GeometryTestSupport.hpp"
#include "TestHelpers.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/geometry/Draft.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/geometry/Split.hpp>
#include <bettercad/core/geometry/Sweeps.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <vector>

using namespace bettercad;
using namespace bettercad::geometry;
using namespace bettercad::literals;
using Catch::Matchers::StartsWith;
using Catch::Matchers::WithinRel;

// P12-FEAT-004: drafting faces of bodies. The expected volumes integrate the
// shrinking sections exactly (see support/DraftModels.hpp for the
// feature-level models).

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

/// The side of the box swept by segment @p i (0: y = 0, 1: x = 100, 2: y = 60,
/// 3: x = 0), and its top.
FaceName side(std::size_t i) {
    return FaceName{ObjectId::fromValue(1),
                    FaceSelector{.role = FaceRole::Side, .entity = EntityId::fromValue(10 + i)}};
}
const FaceName kTop{ObjectId::fromValue(1), FaceSelector{.role = FaceRole::EndCap}};

/// A 100 x 60 x 40 mm box at the origin whose sides and top are named.
Body namedBox() {
    ProfileLoop loop;
    const std::array<Point2D, 4> corners{Point2D{0_mm, 0_mm}, Point2D{100_mm, 0_mm}, Point2D{100_mm, 60_mm},
                                         Point2D{0_mm, 60_mm}};
    for (std::size_t i = 0; i < corners.size(); ++i) {
        loop.segments.emplace_back(LineSegment2D{corners[i], corners[(i + 1) % corners.size()]});
    }
    const PlanarRegion region{.plane = Frame3D::xy(), .outer = loop, .holes = {}};
    return require(makePrism(region, 0_mm, 40_mm, [](const SweptFace& face) -> std::optional<FaceName> {
        if (face.kind == SweptFace::Kind::Last) {
            return kTop;
        }
        if (face.kind == SweptFace::Kind::Side) {
            return side(face.segment);
        }
        return std::nullopt;
    }));
}

DraftRequest sides(Angle angle, const Frame3D& plane = Frame3D::xy()) {
    return DraftRequest{.faces = {side(0), side(1), side(2), side(3)}, .neutralPlane = plane, .angle = angle};
}

/// The box's volume with its sides moved in by z tan(a) at height z.
double tapered(double degrees) {
    const double t = std::tan(degrees * pi / 180.0);
    return 240000.0 - 2.0 * t * 160.0 * 800.0 + 4.0 * t * t * 64000.0 / 3.0;
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

} // namespace

TEST_CASE("Draft_RequestsAreValidated", "[core][geometry][draft][p12]") {
    const auto message = [](const DraftRequest& request) {
        const auto valid = validate(request);
        REQUIRE_FALSE(valid.has_value());
        CHECK(valid.error().code == ErrorCode::InvalidArgument);
        return valid.error().message;
    };
    REQUIRE(validate(sides(3_deg)).has_value());
    REQUIRE(validate(sides(Angle{})).has_value());
    REQUIRE(validate(sides(-(89_deg))).has_value());
    CHECK(message({.angle = 3_deg}) == "a draft needs one or more faces");
    CHECK(message({.faces = {side(0), FaceName{}}, .angle = 3_deg}) == "face 2 must name a valid feature");
    CHECK(message({.faces = {side(0), side(0)}, .angle = 3_deg}) == "face 2 repeats an earlier face");
    CHECK(message({.faces = {FaceName{ObjectId::fromValue(1), {.role = FaceRole::Chamfer}}}, .angle = 3_deg}) ==
          "face 1: a chamfer face is named by its chamfer edge's ID");
    CHECK(message(sides(90_deg)) == "the draft angle must be in (-90, 90) deg, got 90 deg");
    CHECK(message(sides(-(180_deg))) == "the draft angle must be in (-90, 90) deg, got -180 deg");
    CHECK(message(sides(Angle::fromSi(std::numeric_limits<double>::quiet_NaN()))) ==
          "the draft angle must be in (-90, 90) deg, got nan deg");
}

TEST_CASE("Draft_TurnsFacesAndCarriesTheirNames", "[core][geometry][draft][p12]") {
    const Body box = namedBox();
    const Body drafted = require(draftFaces(box, sides(5_deg)));
    CHECK(drafted.isValid());
    CHECK(drafted.topology() == box.topology());
    CHECK_THAT(volumeOf(drafted), WithinRel(tapered(5.0), kRel));
    // The front narrows from 100 to 100 - 80 tan(5 deg), along a slope
    // 40 / cos(5 deg) long; the top shrinks.
    const double t = std::tan(5.0 * pi / 180.0);
    CHECK_THAT(namedArea(drafted, side(0)), WithinRel((200.0 - 80.0 * t) / 2.0 * 40.0 / std::cos(5.0 * pi / 180.0),
                                                      kRel));
    CHECK_THAT(namedArea(drafted, kTop), WithinRel((100.0 - 80.0 * t) * (60.0 - 80.0 * t), kRel));
    const auto bounds = drafted.boundingBox();
    REQUIRE(bounds.has_value());
    bettercad::test::checkPoint(bounds->min, 0.0, 0.0, 0.0);
    bettercad::test::checkPoint(bounds->max, 100.0, 60.0, 40.0);
    // The input is untouched; the same request gives the same body.
    CHECK_THAT(volumeOf(box), WithinRel(240000.0, kRel));
    CHECK(volumeOf(require(draftFaces(box, sides(5_deg)))) == volumeOf(drafted));

    // A negative angle adds material; a zero angle changes nothing.
    CHECK_THAT(volumeOf(require(draftFaces(box, sides(-(5_deg))))), WithinRel(tapered(-5.0), kRel));
    CHECK_THAT(volumeOf(require(draftFaces(box, sides(Angle{})))), WithinRel(240000.0, kRel));
    // Pulled down (the plane facing -Z), a positive angle adds material above.
    const Frame3D down = Frame3D::create(Point3D{}, Direction3D::unitZ().reversed(), Direction3D::unitX()).value();
    CHECK_THAT(volumeOf(require(draftFaces(box, sides(5_deg, down)))), WithinRel(tapered(-5.0), kRel));
    // One face about a plane above the box moves out below it.
    const Frame3D above =
        Frame3D::create(Point3D{0_mm, 0_mm, 60_mm}, Direction3D::unitZ(), Direction3D::unitX()).value();
    const Body one = require(draftFaces(box, {.faces = {side(1)}, .neutralPlane = above, .angle = 5_deg}));
    CHECK_THAT(volumeOf(one), WithinRel(240000.0 + 60.0 * 1600.0 * t, kRel));
}

TEST_CASE("Draft_FailuresAreStructured", "[core][geometry][draft][p12]") {
    const Body box = namedBox();
    const auto fails = [](const Result<Body>& result, ErrorCode code) {
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == code);
        return result.error().message;
    };
    SECTION("empty and invalid inputs") {
        CHECK(fails(draftFaces(Body{}, sides(3_deg)), ErrorCode::FailedPrecondition) == "draft: the body is empty");
        CHECK(fails(draftFaces(box, sides(90_deg)), ErrorCode::InvalidArgument) ==
              "draft: the draft angle must be in (-90, 90) deg, got 90 deg");
    }
    SECTION("faces the body does not have, or cannot turn") {
        DraftRequest request = sides(3_deg);
        request.faces.push_back(FaceName{ObjectId::fromValue(2), {.role = FaceRole::EndCap}});
        CHECK(fails(draftFaces(box, request), ErrorCode::NotFound) == "draft: face 5 is not a face of the body");
        // The top is parallel to the neutral plane.
        CHECK(fails(draftFaces(box, {.faces = {side(0), kTop}, .angle = 3_deg}), ErrorCode::FailedPrecondition) ==
              "draft: face 2 cannot be turned about the neutral plane (kernel: a face cannot be recomputed); a face "
              "parallel to the plane, or a chain of tangent faces that reaches one, cannot be drafted");
    }
    SECTION("several solids") {
        const Body halves = require(splitBody(
            box, Frame3D::create(Point3D{50_mm, 0_mm, 0_mm}, Direction3D::unitX(), Direction3D::unitY()).value(),
            SplitKeep::Both));
        CHECK(fails(draftFaces(halves, sides(3_deg)), ErrorCode::FailedPrecondition) ==
              "draft: a draft turns faces of one solid; the body has 2");
    }
    SECTION("angles that make faces vanish") {
        // At atan(0.75) (36.87 deg) the 60 mm top shrinks to nothing; beyond,
        // the kernel cannot build the draft.
        for (const double degrees : {37.0, 45.0, 60.0}) {
            CAPTURE(degrees);
            CHECK(fails(draftFaces(box, sides(degrees * units::deg)), ErrorCode::FailedPrecondition) ==
                  "draft: the kernel cannot build the draft: kernel: an edge cannot be recomputed; an angle this "
                  "large may make faces vanish");
        }
        CHECK_THAT(fails(draftFaces(box, sides(Angle::fromSi(std::atan(0.75)))), ErrorCode::FailedPrecondition),
                   StartsWith("draft: the kernel cannot build the draft: "));
        // Just below, the draft is exact.
        CHECK_THAT(volumeOf(require(draftFaces(box, sides(36.5_deg)))), WithinRel(tapered(36.5), kRel));
    }
}
