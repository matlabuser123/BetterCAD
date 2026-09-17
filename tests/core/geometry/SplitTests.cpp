#include "GeometryTestSupport.hpp"
#include "TestHelpers.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/geometry/Primitives.hpp>
#include <bettercad/core/geometry/Split.hpp>
#include <bettercad/core/geometry/Sweeps.hpp>
#include <bettercad/core/geometry/Transform.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <optional>
#include <vector>

using namespace bettercad;
using namespace bettercad::geometry;
using namespace bettercad::literals;
using bettercad::test::errorCode;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// P12-FEAT-002: splitting bodies with planes, and bodies of separate solids.

namespace {

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

/// The plane x = @p x (mm) facing +X.
Frame3D planeAtX(double x) {
    return Frame3D::create(Point3D{x * units::mm, 0_mm, 0_mm}, Direction3D::unitX(), Direction3D::unitY()).value();
}

/// A 100 x 60 x 20 mm box at the origin whose top is named.
Body namedBox() {
    const ObjectId feature = ObjectId::fromValue(1);
    ProfileLoop loop;
    const std::array<Point2D, 4> corners{Point2D{0_mm, 0_mm}, Point2D{100_mm, 0_mm}, Point2D{100_mm, 60_mm},
                                         Point2D{0_mm, 60_mm}};
    for (std::size_t i = 0; i < corners.size(); ++i) {
        loop.segments.emplace_back(LineSegment2D{corners[i], corners[(i + 1) % corners.size()]});
    }
    const PlanarRegion region{.plane = Frame3D::xy(), .outer = loop, .holes = {}};
    return require(makePrism(region, 0_mm, 20_mm, [feature](const SweptFace& face) -> std::optional<FaceName> {
        if (face.kind == SweptFace::Kind::Last) {
            return FaceName{feature, FaceSelector{.role = FaceRole::EndCap}};
        }
        return std::nullopt;
    }));
}

double topArea(const Body& body) {
    const auto faces =
        findNamedFaces(body, FaceName{ObjectId::fromValue(1), FaceSelector{.role = FaceRole::EndCap}});
    REQUIRE(faces.has_value());
    double area = 0.0;
    for (const FaceInfo& face : *faces) {
        area += face.area.in(units::mm2);
    }
    return area;
}

} // namespace

TEST_CASE("Split_KeepsTheSidesOfAPlane", "[core][geometry][split][p12]") {
    const Body box = namedBox();
    const Body front = require(splitBody(box, planeAtX(30.0), SplitKeep::Front));
    const Body back = require(splitBody(box, planeAtX(30.0), SplitKeep::Back));
    const Body both = require(splitBody(box, planeAtX(30.0), SplitKeep::Both));
    CHECK_THAT(volumeOf(front), WithinRel(70.0 * 60.0 * 20.0, 1e-12));
    CHECK_THAT(volumeOf(back), WithinRel(30.0 * 60.0 * 20.0, 1e-12));
    CHECK_THAT(volumeOf(both), WithinRel(100.0 * 60.0 * 20.0, 1e-12));
    CHECK(front.topology().solids == 1);
    CHECK(back.topology().solids == 1);
    CHECK(both.topology().solids == 2);
    CHECK(both.isValid());
    const auto frontBox = front.boundingBox();
    REQUIRE(frontBox.has_value());
    CHECK_THAT(frontBox->min.x.in(units::mm), WithinAbs(30.0, 1e-9));
    const auto backBox = back.boundingBox();
    REQUIRE(backBox.has_value());
    CHECK_THAT(backBox->max.x.in(units::mm), WithinAbs(30.0, 1e-9));
    // The names go with the parts; the faces on the plane have none.
    CHECK_THAT(topArea(front), WithinRel(70.0 * 60.0, 1e-12));
    CHECK_THAT(topArea(back), WithinRel(30.0 * 60.0, 1e-12));
    CHECK_THAT(topArea(both), WithinRel(100.0 * 60.0, 1e-12));
    const auto faces = listFaces(front);
    REQUIRE(faces.has_value());
    CHECK(faces->size() == 6);
    CHECK(std::ranges::count_if(*faces, [](const FaceInfo& face) { return face.names.empty(); }) == 5);
    // A plane facing the other way swaps the sides.
    const Frame3D reversed =
        Frame3D::create(Point3D{30_mm, 0_mm, 0_mm}, Direction3D::unitX().reversed(), Direction3D::unitY()).value();
    CHECK_THAT(volumeOf(require(splitBody(box, reversed, SplitKeep::Front))), WithinRel(30.0 * 60.0 * 20.0, 1e-12));
}

TEST_CASE("Split_RefusesPlanesThatDoNotCrossTheBody", "[core][geometry][split][p12]") {
    const Body box = namedBox();
    const auto message = [&](const Frame3D& plane) {
        const auto split = splitBody(box, plane, SplitKeep::Both);
        REQUIRE_FALSE(split.has_value());
        CHECK(split.error().code == ErrorCode::FailedPrecondition);
        return split.error().message;
    };
    CHECK(message(planeAtX(100.0)) == "split: the plane does not cross the body: all of it lies behind the plane");
    CHECK(message(planeAtX(0.0)) == "split: the plane does not cross the body: all of it lies in front of the plane");
    CHECK(message(planeAtX(-1.0)) ==
          "split: the plane does not cross the body: all of it lies in front of the plane");
    CHECK(errorCode(splitBody(Body{}, planeAtX(0.0), SplitKeep::Both)) == ErrorCode::InvalidArgument);
    CHECK(errorCode(splitBody(box, planeAtX(50.0), static_cast<SplitKeep>(9))) == ErrorCode::InvalidArgument);
    // Two boxes 100 mm apart, split by a plane in the gap: each side holds
    // one box. (A plane that crosses the bounds but misses the body is
    // SplitCombine_FailuresAreStructuredAndAtomic's L-shaped case.)
    const Body pair = require(gatherSolids(std::vector<Body>{
        box, require(translated(box, Translation3D{200_mm, 0_mm, 0_mm}))}));
    const Body split = require(splitBody(pair, planeAtX(150.0), SplitKeep::Front));
    CHECK_THAT(volumeOf(split), WithinRel(120000.0, 1e-12));
}

TEST_CASE("GatherSolids_KeepsSolidsApart", "[core][geometry][split][p12]") {
    const Body box = namedBox();
    const Body moved = require(translated(box, Translation3D{0_mm, 0_mm, 20_mm}));
    // Touching boxes stay two solids (a union would fuse them).
    const Body stack = require(gatherSolids(std::vector<Body>{box, moved}));
    CHECK(stack.topology().solids == 2);
    CHECK(stack.isValid());
    CHECK_THAT(volumeOf(stack), WithinRel(240000.0, 1e-12));
    // Both tops keep their name.
    CHECK_THAT(topArea(stack), WithinRel(12000.0, 1e-12));
    CHECK(errorCode(gatherSolids(std::vector<Body>{})) == ErrorCode::InvalidArgument);
    CHECK(errorCode(gatherSolids(std::vector<Body>{box, Body{}})) == ErrorCode::InvalidArgument);
}
