#include "TestHelpers.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/drawing/View.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <numbers>
#include <string>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::WithinAbs;
using drawing::ProjectedDirection;
using drawing::ProjectionConvention;
using drawing::StandardView;

// P14-VIEW-001: orthographic projection, validated against arithmetic done
// here rather than against the production routine.
//
// WHERE THE EXPECTED NUMBERS COME FROM. ADR-013 fixes the convention: a view
// frame's normal points FROM THE MODEL TOWARD THE VIEWER, its X axis is right
// on the sheet and its Y axis is up. Under that reading the six standard
// views have these bases, written out by hand from the definition of what it
// means to stand somewhere and look at the origin:
//
//     view     right      up        normal (toward viewer)   viewer stands at
//     Front    +X         +Z        -Y                       -Y
//     Rear     -X         +Z        +Y                       +Y
//     Right    +Y         +Z        +X                       +X
//     Left     -Y         +Z        -X                       -X
//     Top      +X         +Y        +Z                       +Z
//     Bottom   +X         -Y        -Z                       -Z
//
// and the projection of a model point P is
//
//     x = (P - O) . right        y = (P - O) . up
//
// Every expected coordinate below is that dot product computed by hand. No
// expected value is read out of BetterCAD.
namespace {

constexpr double kUnit = 1e-12;  // dimensionless, well-conditioned algebra
constexpr double kMm = 1e-9;     // millimetres, after two subtractions

struct Basis {
    std::array<double, 3> right;
    std::array<double, 3> up;
    std::array<double, 3> normal;
};

/// The six standard bases, by hand.
Basis expectedBasis(StandardView view) {
    switch (view) {
    case StandardView::Front:
        return {{1, 0, 0}, {0, 0, 1}, {0, -1, 0}};
    case StandardView::Rear:
        return {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}};
    case StandardView::Right:
        return {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}};
    case StandardView::Left:
        return {{0, -1, 0}, {0, 0, 1}, {-1, 0, 0}};
    case StandardView::Top:
        return {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    case StandardView::Bottom:
        return {{1, 0, 0}, {0, -1, 0}, {0, 0, -1}};
    case StandardView::Isometric:
        break;
    }
    FAIL("no hand-written basis for that view");
    return {};
}

std::array<double, 3> axes(const Direction3D& d) { return {d.x(), d.y(), d.z()}; }

double dot(const std::array<double, 3>& a, const std::array<double, 3>& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

constexpr std::array kOrthographic{StandardView::Front, StandardView::Rear,  StandardView::Left,
                                   StandardView::Right, StandardView::Top,   StandardView::Bottom};

} // namespace

// --- The basis itself ----------------------------------------------------------------

TEST_CASE("ViewProjection_EveryStandardBasisMatchesTheHandWrittenOne", "[drawing][view][p14]") {
    for (const StandardView view : kOrthographic) {
        INFO("view " << drawing::toString(view));
        const auto basis = drawing::basisOf(view);
        REQUIRE(basis.has_value());
        const Basis want = expectedBasis(view);

        for (std::size_t i = 0; i < 3; ++i) {
            CHECK_THAT(axes(basis->xAxis())[i], WithinAbs(want.right[i], kUnit));
            CHECK_THAT(axes(basis->yAxis())[i], WithinAbs(want.up[i], kUnit));
            CHECK_THAT(axes(basis->normal())[i], WithinAbs(want.normal[i], kUnit));
        }
    }
}

TEST_CASE("ViewProjection_EveryBasisIsOrthonormalAndRightHanded", "[drawing][view][p14]") {
    // Frame3D::fromAxes checks this to 1e-12 and refuses otherwise, so a
    // failure here means basisOf() handed it something wrong. Checked anyway,
    // because a left-handed basis mirrors the drawing and nothing else would
    // say so.
    for (const StandardView view : {StandardView::Front, StandardView::Rear, StandardView::Left,
                                    StandardView::Right, StandardView::Top, StandardView::Bottom,
                                    StandardView::Isometric}) {
        INFO("view " << drawing::toString(view));
        const auto b = drawing::basisOf(view);
        REQUIRE(b.has_value());
        const auto r = axes(b->xAxis());
        const auto u = axes(b->yAxis());
        const auto n = axes(b->normal());

        CHECK_THAT(dot(r, r), WithinAbs(1.0, kUnit));
        CHECK_THAT(dot(u, u), WithinAbs(1.0, kUnit));
        CHECK_THAT(dot(n, n), WithinAbs(1.0, kUnit));
        CHECK_THAT(dot(r, u), WithinAbs(0.0, kUnit));
        CHECK_THAT(dot(r, n), WithinAbs(0.0, kUnit));
        CHECK_THAT(dot(u, n), WithinAbs(0.0, kUnit));

        // Right-handed: up == normal x right.
        const std::array<double, 3> cross{n[1] * r[2] - n[2] * r[1], n[2] * r[0] - n[0] * r[2],
                                          n[0] * r[1] - n[1] * r[0]};
        for (std::size_t i = 0; i < 3; ++i) {
            CHECK_THAT(cross[i], WithinAbs(u[i], kUnit));
        }
    }
}

TEST_CASE("ViewProjection_NoTwoStandardViewsShareABasis", "[drawing][view][p14]") {
    // Rear must not equal Front, Left must not equal Right, and so on. A
    // copy-paste in basisOf() would otherwise pass every other test here.
    for (std::size_t i = 0; i < kOrthographic.size(); ++i) {
        for (std::size_t j = i + 1; j < kOrthographic.size(); ++j) {
            INFO(drawing::toString(kOrthographic[i]) << " vs " << drawing::toString(kOrthographic[j]));
            const auto a = drawing::basisOf(kOrthographic[i]);
            const auto b = drawing::basisOf(kOrthographic[j]);
            REQUIRE(a.has_value());
            REQUIRE(b.has_value());
            CHECK_FALSE(*a == *b);
        }
    }
}

TEST_CASE("ViewProjection_OppositeViewsHaveOpposedNormals", "[drawing][view][p14]") {
    // The property that catches a Rear that is really a Front.
    const std::array<std::pair<StandardView, StandardView>, 3> opposed{{
        {StandardView::Front, StandardView::Rear},
        {StandardView::Left, StandardView::Right},
        {StandardView::Top, StandardView::Bottom},
    }};
    for (const auto& [one, other] : opposed) {
        INFO(drawing::toString(one) << " vs " << drawing::toString(other));
        const auto a = drawing::basisOf(one);
        const auto b = drawing::basisOf(other);
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        const auto na = axes(a->normal());
        const auto nb = axes(b->normal());
        for (std::size_t i = 0; i < 3; ++i) {
            CHECK_THAT(na[i], WithinAbs(-nb[i], kUnit));
        }
    }
}

// --- Projecting known points ---------------------------------------------------------

TEST_CASE("ViewProjection_KnownPointsProjectToHandComputedCoordinates", "[drawing][view][p14]") {
    // An ASYMMETRIC point: every coordinate different, and no two equal in
    // magnitude, so a swapped or negated axis cannot produce the right answer
    // by accident.
    const Point3D p{30.0 * units::mm, 70.0 * units::mm, 110.0 * units::mm};

    // x = P . right, y = P . up, with the origin at the model origin.
    struct Expected {
        StandardView view;
        double x;
        double y;
    };
    constexpr std::array kWant{
        Expected{StandardView::Front, 30.0, 110.0},   // (+X, +Z)
        Expected{StandardView::Rear, -30.0, 110.0},   // (-X, +Z)
        Expected{StandardView::Right, 70.0, 110.0},   // (+Y, +Z)
        Expected{StandardView::Left, -70.0, 110.0},   // (-Y, +Z)
        Expected{StandardView::Top, 30.0, 70.0},      // (+X, +Y)
        Expected{StandardView::Bottom, 30.0, -70.0},  // (+X, -Y)
    };

    double worst = 0.0;
    for (const Expected& want : kWant) {
        INFO("view " << drawing::toString(want.view));
        const auto basis = drawing::basisOf(want.view);
        REQUIRE(basis.has_value());
        const Point2D got = drawing::projectToViewPlane(*basis, p);
        CHECK_THAT(got.x.in(units::mm), WithinAbs(want.x, kMm));
        CHECK_THAT(got.y.in(units::mm), WithinAbs(want.y, kMm));
        worst = std::max({worst, std::abs(got.x.in(units::mm) - want.x),
                          std::abs(got.y.in(units::mm) - want.y)});
    }
    // Recorded rather than merely asserted: the evidence quotes this.
    INFO("maximum projection error " << worst << " mm");
    CHECK(worst < kMm);
}

TEST_CASE("ViewProjection_TheOriginAndTheAxesProjectAsTheyMust", "[drawing][view][p14]") {
    for (const StandardView view : kOrthographic) {
        INFO("view " << drawing::toString(view));
        const auto basis = drawing::basisOf(view);
        REQUIRE(basis.has_value());

        // The model origin is the view origin, so it projects to (0, 0).
        const Point2D o = drawing::projectToViewPlane(*basis, Point3D{});
        CHECK_THAT(o.x.si(), WithinAbs(0.0, kUnit));
        CHECK_THAT(o.y.si(), WithinAbs(0.0, kUnit));

        // A point on the view normal projects to the origin too -- that is
        // what "orthographic" means -- and its depth is its distance.
        const Direction3D n = basis->normal();
        const Point3D along{100.0 * units::mm * n.x(), 100.0 * units::mm * n.y(),
                            100.0 * units::mm * n.z()};
        const Point2D flat = drawing::projectToViewPlane(*basis, along);
        CHECK_THAT(flat.x.in(units::mm), WithinAbs(0.0, kMm));
        CHECK_THAT(flat.y.in(units::mm), WithinAbs(0.0, kMm));
        CHECK_THAT(drawing::depthInView(*basis, along).in(units::mm), WithinAbs(100.0, kMm));
    }
}

TEST_CASE("ViewProjection_DepthIsPositiveTowardTheViewer", "[drawing][view][p14]") {
    // Front looks along +Y from -Y, so a point at -Y is nearer the viewer and
    // must have positive depth. This is the check that catches a normal
    // pointing the wrong way, which would mirror every view.
    const auto front = drawing::basisOf(StandardView::Front);
    REQUIRE(front.has_value());
    const Point3D near{0_mm, -50.0 * units::mm, 0_mm};
    const Point3D far{0_mm, 50.0 * units::mm, 0_mm};
    CHECK(drawing::depthInView(*front, near).si() > 0.0);
    CHECK(drawing::depthInView(*front, far).si() < 0.0);
}

TEST_CASE("ViewProjection_AnAsymmetricBoxProjectsToTheRightExtentsInEveryView",
          "[drawing][view][p14]") {
    // 100 x 60 x 40 -- three different sizes, so a swapped pair of axes
    // changes the answer. A cube could not detect that.
    constexpr double W = 100.0, D = 60.0, H = 40.0;
    std::vector<Point3D> corners;
    for (const double x : {0.0, W}) {
        for (const double y : {0.0, D}) {
            for (const double z : {0.0, H}) {
                corners.push_back({x * units::mm, y * units::mm, z * units::mm});
            }
        }
    }

    struct Expected {
        StandardView view;
        double width;
        double height;
    };
    constexpr std::array kWant{
        Expected{StandardView::Front, W, H},   Expected{StandardView::Rear, W, H},
        Expected{StandardView::Right, D, H},   Expected{StandardView::Left, D, H},
        Expected{StandardView::Top, W, D},     Expected{StandardView::Bottom, W, D},
    };

    for (const Expected& want : kWant) {
        INFO("view " << drawing::toString(want.view));
        const auto basis = drawing::basisOf(want.view);
        REQUIRE(basis.has_value());
        double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
        for (const Point3D& c : corners) {
            const Point2D q = drawing::projectToViewPlane(*basis, c);
            minX = std::min(minX, q.x.in(units::mm));
            maxX = std::max(maxX, q.x.in(units::mm));
            minY = std::min(minY, q.y.in(units::mm));
            maxY = std::max(maxY, q.y.in(units::mm));
        }
        CHECK_THAT(maxX - minX, WithinAbs(want.width, kMm));
        CHECK_THAT(maxY - minY, WithinAbs(want.height, kMm));
    }
}

// --- Isometric -----------------------------------------------------------------------

TEST_CASE("ViewProjection_IsometricForeshortensAllThreeAxesEqually", "[drawing][view][p14]") {
    // The defining property. A model axis of unit length projects to
    // sqrt(1 - (axis . normal)^2); with the normal at (1,1,1)/sqrt(3) that is
    // sqrt(1 - 1/3) = sqrt(2/3) for all three, computed here, not read back.
    const auto basis = drawing::basisOf(StandardView::Isometric);
    REQUIRE(basis.has_value());
    const double expected = std::sqrt(2.0 / 3.0);

    const std::array<Point3D, 3> unitAxes{Point3D{1.0 * units::m, 0_mm, 0_mm},
                                          Point3D{0_mm, 1.0 * units::m, 0_mm},
                                          Point3D{0_mm, 0_mm, 1.0 * units::m}};
    for (std::size_t i = 0; i < 3; ++i) {
        INFO("model axis " << i);
        const Point2D q = drawing::projectToViewPlane(*basis, unitAxes[i]);
        const double length = std::hypot(q.x.si(), q.y.si());
        CHECK_THAT(length, WithinAbs(expected, 1e-12));
    }
}

TEST_CASE("ViewProjection_IsometricKeepsTheModelZUpright", "[drawing][view][p14]") {
    // The choice that makes it the standard isometric rather than one of the
    // other orientations with equal foreshortening: +Z projects straight up,
    // so its x component is zero and its y component is positive.
    const auto basis = drawing::basisOf(StandardView::Isometric);
    REQUIRE(basis.has_value());
    const Point2D up = drawing::projectToViewPlane(*basis, Point3D{0_mm, 0_mm, 1.0 * units::m});
    CHECK_THAT(up.x.si(), WithinAbs(0.0, 1e-12));
    CHECK(up.y.si() > 0.0);

    // And the two horizontal axes come down symmetrically either side.
    const Point2D ax = drawing::projectToViewPlane(*basis, Point3D{1.0 * units::m, 0_mm, 0_mm});
    const Point2D ay = drawing::projectToViewPlane(*basis, Point3D{0_mm, 1.0 * units::m, 0_mm});
    CHECK_THAT(ax.x.si(), WithinAbs(-ay.x.si(), 1e-12));
    CHECK_THAT(ax.y.si(), WithinAbs(ay.y.si(), 1e-12));
    CHECK(ax.y.si() < 0.0);
}

// --- Projected views derive their basis ----------------------------------------------

TEST_CASE("ViewProjection_AProjectedViewsBasisIsTheStandardOneItShouldBe",
          "[drawing][view][p14]") {
    // A Top view projected from a Front parent must be the standard Top view,
    // and so on. This is what stops a projected view acquiring an orientation
    // that contradicts its parent (ADR-018).
    const auto front = drawing::basisOf(StandardView::Front);
    REQUIRE(front.has_value());

    const std::array<std::pair<ProjectedDirection, StandardView>, 4> want{{
        {ProjectedDirection::Top, StandardView::Top},
        {ProjectedDirection::Bottom, StandardView::Bottom},
        {ProjectedDirection::Right, StandardView::Right},
        {ProjectedDirection::Left, StandardView::Left},
    }};
    for (const auto& [direction, equivalent] : want) {
        INFO(drawing::toString(direction) << " from front should equal " << drawing::toString(equivalent));
        const auto derived = drawing::projectedBasis(*front, direction);
        const auto standard = drawing::basisOf(equivalent);
        REQUIRE(derived.has_value());
        REQUIRE(standard.has_value());
        for (std::size_t i = 0; i < 3; ++i) {
            CHECK_THAT(axes(derived->xAxis())[i], WithinAbs(axes(standard->xAxis())[i], kUnit));
            CHECK_THAT(axes(derived->yAxis())[i], WithinAbs(axes(standard->yAxis())[i], kUnit));
            CHECK_THAT(axes(derived->normal())[i], WithinAbs(axes(standard->normal())[i], kUnit));
        }
    }
}

TEST_CASE("ViewProjection_EveryDerivedBasisIsStillOrthonormalAndRightHanded",
          "[drawing][view][p14]") {
    // Derived from every standard parent, not just Front: the rotation must
    // not lose handedness anywhere.
    for (const StandardView parent : kOrthographic) {
        const auto base = drawing::basisOf(parent);
        REQUIRE(base.has_value());
        for (const ProjectedDirection d : {ProjectedDirection::Top, ProjectedDirection::Bottom,
                                           ProjectedDirection::Left, ProjectedDirection::Right}) {
            INFO(drawing::toString(d) << " from " << drawing::toString(parent));
            const auto derived = drawing::projectedBasis(*base, d);
            REQUIRE(derived.has_value());
            const auto r = axes(derived->xAxis());
            const auto u = axes(derived->yAxis());
            const auto n = axes(derived->normal());
            CHECK_THAT(dot(r, u), WithinAbs(0.0, kUnit));
            CHECK_THAT(dot(r, n), WithinAbs(0.0, kUnit));
            CHECK_THAT(dot(u, n), WithinAbs(0.0, kUnit));
            const std::array<double, 3> cross{n[1] * r[2] - n[2] * r[1], n[2] * r[0] - n[0] * r[2],
                                              n[0] * r[1] - n[1] * r[0]};
            for (std::size_t i = 0; i < 3; ++i) {
                CHECK_THAT(cross[i], WithinAbs(u[i], kUnit));
            }
            // A projected view must look somewhere else than its parent.
            CHECK_FALSE(*derived == *base);
        }
    }
}

// --- The projection convention -------------------------------------------------------

TEST_CASE("ViewProjection_FirstAndThirdAnglePlaceViewsOnOppositeSides", "[drawing][view][p14]") {
    // ADR-018, as coordinates rather than as a flag. First angle puts a Top
    // view BELOW its parent and a Right view to its LEFT; third angle puts
    // both on the other side.
    struct Expected {
        ProjectedDirection direction;
        double firstX, firstY;
    };
    constexpr std::array kWant{
        Expected{ProjectedDirection::Top, 0.0, -1.0},
        Expected{ProjectedDirection::Bottom, 0.0, 1.0},
        Expected{ProjectedDirection::Right, -1.0, 0.0},
        Expected{ProjectedDirection::Left, 1.0, 0.0},
    };
    for (const Expected& want : kWant) {
        INFO("direction " << drawing::toString(want.direction));
        const auto [fx, fy] = drawing::placementStep(want.direction, ProjectionConvention::FirstAngle);
        CHECK(fx == want.firstX);
        CHECK(fy == want.firstY);
        // Third angle is exactly the opposite, every time.
        const auto [tx, ty] = drawing::placementStep(want.direction, ProjectionConvention::ThirdAngle);
        CHECK(tx == -want.firstX);
        CHECK(ty == -want.firstY);
    }
}

TEST_CASE("ViewProjection_TopAndBottomAlignVerticallyLeftAndRightHorizontally",
          "[drawing][view][p14]") {
    // The alignment contract, independent of convention: a Top or Bottom view
    // shares its parent's x, a Left or Right view shares its parent's y.
    for (const ProjectionConvention c : {ProjectionConvention::FirstAngle,
                                         ProjectionConvention::ThirdAngle}) {
        for (const ProjectedDirection d : {ProjectedDirection::Top, ProjectedDirection::Bottom}) {
            const auto [dx, dy] = drawing::placementStep(d, c);
            INFO(drawing::toString(d) << " under " << drawing::toString(c));
            CHECK(dx == 0.0);           // shares the parent's x
            CHECK(std::abs(dy) == 1.0); // moves only vertically
        }
        for (const ProjectedDirection d : {ProjectedDirection::Left, ProjectedDirection::Right}) {
            const auto [dx, dy] = drawing::placementStep(d, c);
            INFO(drawing::toString(d) << " under " << drawing::toString(c));
            CHECK(dy == 0.0);           // shares the parent's y
            CHECK(std::abs(dx) == 1.0); // moves only horizontally
        }
    }
}

// --- Determinism ---------------------------------------------------------------------

TEST_CASE("ViewProjection_TheSameViewAlwaysGivesTheSameBasisAndProjection",
          "[drawing][view][p14][determinism]") {
    const Point3D p{13.0 * units::mm, -29.0 * units::mm, 47.0 * units::mm};
    for (const StandardView view : {StandardView::Front, StandardView::Rear, StandardView::Left,
                                    StandardView::Right, StandardView::Top, StandardView::Bottom,
                                    StandardView::Isometric}) {
        INFO("view " << drawing::toString(view));
        const auto first = drawing::basisOf(view);
        const auto second = drawing::basisOf(view);
        REQUIRE(first.has_value());
        REQUIRE(second.has_value());
        // Bit for bit: the basis is built from the same constants every time.
        CHECK(*first == *second);
        const Point2D a = drawing::projectToViewPlane(*first, p);
        const Point2D b = drawing::projectToViewPlane(*second, p);
        CHECK(a.x.si() == b.x.si());
        CHECK(a.y.si() == b.y.si());
    }
}
