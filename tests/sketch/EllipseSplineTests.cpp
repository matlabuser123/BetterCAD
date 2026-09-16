#include "TestHelpers.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/sketch/Solver.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::sketch;
using bettercad::test::errorCode;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// P12-SKETCH-002: ellipse and spline entities in sketches and in the solver.
// Solved geometry is checked with formulas written out here (dot and cross
// products of the solved points, Bernstein derivatives), and lengths with
// the Gauss-Kummer series and the closed form of a parabola's arc length.

namespace {

constexpr double pi = std::numbers::pi;
// The solver iterates until residuals are below 1e-13 m (1e-10 mm); 1e-8 mm
// leaves two orders of margin, as in the P6 and P12-SKETCH-001 tests.
constexpr double kTolMm = 1e-8;
// Construction arithmetic: a few operations on values of tens of mm.
constexpr double kExactMm = 1e-12;

template <typename Id>
Id require(const Result<Id>& id) {
    if (!id) {
        FAIL(id.error().message);
    }
    return *id;
}

double mm(Length value) {
    return value.in(units::mm);
}

Point2D at(const Sketch& sketch, EntityId point) {
    const auto position = sketch.position(point);
    REQUIRE(position.has_value());
    return *position;
}

const EllipseEntity& ellipseOf(const Sketch& sketch, EntityId id) {
    return std::get<EllipseEntity>(sketch.findEntity(id)->geometry);
}

const SplineEntity& splineOf(const Sketch& sketch, EntityId id) {
    return std::get<SplineEntity>(sketch.findEntity(id)->geometry);
}

std::string message(const Result<EntityId>& result, ErrorCode code = ErrorCode::InvalidArgument) {
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == code);
    return result.error().message;
}

std::string message(const Result<ConstraintId>& result) {
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ErrorCode::InvalidArgument);
    return result.error().message;
}

void checkSolved(const SolveResult& result, SolveStatus expected, std::size_t dof) {
    INFO(result.message);
    CHECK(result.status == expected);
    CHECK(result.degreesOfFreedom == dof);
    CHECK(result.maxResidual <= SolverOptions{}.tolerance);
}

/// Cosine of the angle between b - a and d - c.
double cosine(const Point2D& a, const Point2D& b, const Point2D& c, const Point2D& d) {
    const double ux = mm(b.x - a.x);
    const double uy = mm(b.y - a.y);
    const double vx = mm(d.x - c.x);
    const double vy = mm(d.y - c.y);
    return (ux * vx + uy * vy) / (std::hypot(ux, uy) * std::hypot(vx, vy));
}

/// Sine of the angle between b - a and d - c.
double sine(const Point2D& a, const Point2D& b, const Point2D& c, const Point2D& d) {
    const double ux = mm(b.x - a.x);
    const double uy = mm(b.y - a.y);
    const double vx = mm(d.x - c.x);
    const double vy = mm(d.y - c.y);
    return (ux * vy - uy * vx) / (std::hypot(ux, uy) * std::hypot(vx, vy));
}

} // namespace

// --- Entities ------------------------------------------------------------------------------------

TEST_CASE("SketchEllipse_CreatedFromSemiAxesAndRotation", "[sketch][ellipse][p12]") {
    Sketch s("Ellipse");
    const EntityId ellipse = require(s.addEllipse(Point2D{10_mm, 20_mm}, 30_mm, 12_mm, 30_deg));
    CHECK(s.entityType(ellipse) == EntityType::Ellipse);
    CHECK(toString(EntityType::Ellipse) == "ellipse");
    CHECK(s.entityCount() == 4);
    const EllipseEntity& e = ellipseOf(s, ellipse);
    const double c = std::cos(pi / 6.0);
    const double sn = std::sin(pi / 6.0);
    CHECK_THAT(mm(at(s, e.center).x), WithinAbs(10.0, kExactMm));
    CHECK_THAT(mm(at(s, e.xVertex).x), WithinAbs(10.0 + 30.0 * c, kExactMm));
    CHECK_THAT(mm(at(s, e.xVertex).y), WithinAbs(20.0 + 30.0 * sn, kExactMm));
    CHECK_THAT(mm(at(s, e.yVertex).x), WithinAbs(10.0 - 12.0 * sn, kExactMm));
    CHECK_THAT(mm(at(s, e.yVertex).y), WithinAbs(20.0 + 12.0 * c, kExactMm));
    CHECK(s.center(ellipse).value() == at(s, e.center));
    CHECK(s.dependentsOf(e.yVertex) == std::vector<EntityId>{ellipse});

    // On existing points: perpendicular to within the tolerance.
    const EntityId centre = require(s.addPoint(Point2D{0_mm, 0_mm}));
    const EntityId x = require(s.addPoint(Point2D{20_mm, 0_mm}));
    // The Y vertex is 3e-11 m off the perpendicular; the X vertex, 20 mm out,
    // is then 20 * 3e-11 / 8 = 7.5e-11 m off Y's axis: both within 1e-10 m.
    const EntityId y = require(s.addPoint(Point2D{Length::fromSi(3e-11), 8_mm}));
    const EntityId onPoints = require(s.addEllipse(centre, x, y));
    CHECK(ellipseOf(s, onPoints) == EllipseEntity{centre, x, y});
}

TEST_CASE("SketchEllipse_InvalidEllipsesAreRefused", "[sketch][ellipse][p12]") {
    Sketch s("Ellipse");
    const EntityId centre = require(s.addPoint(Point2D{0_mm, 0_mm}));
    const EntityId x = require(s.addPoint(Point2D{20_mm, 0_mm}));
    const EntityId tilted =
        require(s.addPoint(Point2D{10.0 * std::cos(80.0 * pi / 180.0) * units::mm,
                                   10.0 * std::sin(80.0 * pi / 180.0) * units::mm}));
    const EntityId line = require(s.addLine(Point2D{0_mm, 50_mm}, Point2D{10_mm, 50_mm}));
    const std::size_t before = s.entityCount();

    CHECK(message(s.addEllipse(centre, x, x)) == "an ellipse needs three different points");
    CHECK(message(s.addEllipse(centre, x, tilted)) ==
          "an ellipse's axes must be perpendicular, but they are 80 deg apart");
    CHECK(message(s.addEllipse(centre, x, line)) == "entity:6 is a line, expected a point");
    CHECK(message(s.addEllipse(centre, x, EntityId::fromValue(99)), ErrorCode::NotFound) ==
          "entity:99 does not exist in this sketch");
    const EntityId onCentre = require(s.addPoint(Point2D{0_mm, 0_mm}));
    CHECK_THAT(message(s.addEllipse(centre, x, onCentre)),
               ContainsSubstring("an ellipse's semi-axes must be finite and longer than"));
    CHECK_THAT(message(s.addEllipse(Point2D{}, 0_mm, 5_mm)),
               ContainsSubstring("an ellipse's semi-axes must be finite and longer than"));
    CHECK_THAT(message(s.addEllipse(Point2D{}, 5_mm, Length::fromSi(std::numeric_limits<double>::infinity()))),
               ContainsSubstring("got inf mm"));
    CHECK(message(s.addEllipse(Point2D{}, 5_mm, 3_mm, Angle::fromSi(std::numeric_limits<double>::quiet_NaN()))) ==
          "an ellipse's rotation must be finite");
    CHECK(s.entityCount() == before + 1); // only the extra point
}

TEST_CASE("SketchSpline_CreatedOnNewOrExistingPoles", "[sketch][spline][p12]") {
    Sketch s("Spline");
    const EntityId open = require(s.addSpline(
        {Point2D{0_mm, 0_mm}, Point2D{10_mm, 20_mm}, Point2D{30_mm, 20_mm}, Point2D{40_mm, 0_mm}}, 3, false));
    CHECK(s.entityType(open) == EntityType::Spline);
    CHECK(toString(EntityType::Spline) == "spline");
    const SplineEntity spline = splineOf(s, open); // a copy: the entity is removed below
    REQUIRE(spline.poles.size() == 4);
    CHECK(spline.degree == 3);
    CHECK_FALSE(spline.periodic);
    CHECK(s.entityCount() == 5);
    const Endpoints ends = s.endpoints(open).value();
    CHECK(ends.start == Point2D{0_mm, 0_mm});
    CHECK(ends.end == Point2D{40_mm, 0_mm});
    CHECK(endPointIds(*s.findEntity(open)) == std::array{spline.poles.front(), spline.poles.back()});

    // A periodic spline on existing points has no end points.
    const std::vector<EntityId> poles{spline.poles[3], spline.poles[2], spline.poles[1]};
    const EntityId closed = require(s.addSpline(poles, 2, true));
    CHECK(splineOf(s, closed).poles == poles);
    CHECK(errorCode(s.endpoints(closed)) == ErrorCode::InvalidArgument);
    CHECK(s.endpoints(closed).error().message == "entity:6 is a periodic spline, which has no end points");
    CHECK_FALSE(endPointIds(*s.findEntity(closed)).has_value());

    // Poles are kept alive by the splines using them.
    const Result<void> removed = s.removeEntity(spline.poles[1]);
    REQUIRE_FALSE(removed.has_value());
    CHECK(removed.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(removed.error().message, ContainsSubstring("it is used by entity:5"));
    REQUIRE(s.removeEntity(closed).has_value());
    REQUIRE(s.removeEntity(open).has_value());
    CHECK(s.removeEntity(spline.poles[1]).has_value());
}

TEST_CASE("SketchSpline_InvalidSplinesAreRefused", "[sketch][spline][p12]") {
    Sketch s("Spline");
    const EntityId a = require(s.addPoint(Point2D{0_mm, 0_mm}));
    const EntityId b = require(s.addPoint(Point2D{10_mm, 0_mm}));
    const EntityId c = require(s.addPoint(Point2D{10_mm, 10_mm}));
    const EntityId same1 = require(s.addPoint(Point2D{5_mm, 5_mm}));
    const EntityId same2 = require(s.addPoint(Point2D{5_mm, 5_mm}));
    const EntityId same3 = require(s.addPoint(Point2D{5_mm, 5_mm}));
    const EntityId line = require(s.addLine(Point2D{0_mm, 50_mm}, Point2D{10_mm, 50_mm}));
    const std::size_t before = s.entityCount();

    CHECK(message(s.addSpline(std::vector<EntityId>{a, b, a}, 2, false)) == "a spline cannot use entity:1 twice");
    CHECK(message(s.addSpline(std::vector<EntityId>{a, b, c}, 1, false)) == "a spline's degree must be 2 to 5, got 1");
    CHECK(message(s.addSpline(std::vector<EntityId>{a, b, c}, 3, false)) ==
          "a spline of degree 3 needs at least 4 poles, got 3");
    CHECK(message(s.addSpline(std::vector<EntityId>{same1, same2, same3}, 2, true)) ==
          "a spline's poles all coincide");
    CHECK(message(s.addSpline(std::vector<EntityId>{a, b, line}, 2, false)) == "entity:9 is a line, expected a point");
    CHECK(message(s.addSpline(std::vector<EntityId>{a, b, EntityId::fromValue(42)}, 2, false), ErrorCode::NotFound) ==
          "entity:42 does not exist in this sketch");
    CHECK(message(s.addSpline({Point2D{0_mm, 0_mm}, Point2D{Length::fromSi(std::nan("")), 0_mm},
                               Point2D{1_mm, 1_mm}},
                              2, false)) == "pole 2 of a spline is not finite");
    CHECK(message(s.addSpline({Point2D{0_mm, 0_mm}, Point2D{1_mm, 0_mm}}, 2, false)) ==
          "a spline of degree 2 needs at least 3 poles, got 2");
    CHECK(s.entityCount() == before);

    // Loading checks the structure, not the geometry.
    Entity loaded{.id = EntityId::fromValue(20), .geometry = SplineEntity{{same1, same2, same3}, 2, false}};
    CHECK(s.insertEntity(loaded).has_value());
    const auto refused = [&](EntityGeometry geometry) {
        loaded.id = EntityId::fromValue(21);
        loaded.geometry = std::move(geometry);
        const Result<void> inserted = s.insertEntity(loaded);
        REQUIRE_FALSE(inserted.has_value());
        return inserted.error();
    };
    CHECK(refused(SplineEntity{{a, b, c}, 6, false}).message == "a spline's degree must be 2 to 5, got 6");
    CHECK(refused(SplineEntity{{a, b, a}, 2, false}).message == "entity:21 references entity:1 twice");
    CHECK(refused(EllipseEntity{a, b, EntityId::fromValue(77)}).code == ErrorCode::NotFound);
}

// --- Queries -------------------------------------------------------------------------------------

TEST_CASE("SketchEllipseSpline_LengthsMatchIndependentFormulas", "[sketch][ellipse][spline][p12]") {
    Sketch s("Lengths");
    SECTION("an ellipse, by the Gauss-Kummer series") {
        // C = pi (a + b) sum binom(1/2, n)^2 h^n, h = ((a - b) / (a + b))^2
        const auto kummer = [](double a, double b) {
            const double h = std::pow((a - b) / (a + b), 2.0);
            double sum = 0.0;
            double coefficient = 1.0; // binom(1/2, n)
            double power = 1.0;
            for (int n = 0; n < 80; ++n) {
                sum += coefficient * coefficient * power;
                coefficient *= (0.5 - n) / (n + 1);
                power *= h;
            }
            return pi * (a + b) * sum;
        };
        const EntityId wide = require(s.addEllipse(Point2D{5_mm, 5_mm}, 30_mm, 10_mm, 25_deg));
        const EntityId tall = require(s.addEllipse(Point2D{5_mm, 5_mm}, 10_mm, 30_mm, 25_deg));
        const EntityId round = require(s.addEllipse(Point2D{5_mm, 5_mm}, 7_mm, 7_mm));
        CHECK_THAT(mm(s.length(wide).value()), WithinRel(kummer(30.0, 10.0), 1e-13));
        CHECK_THAT(mm(s.length(tall).value()), WithinRel(kummer(30.0, 10.0), 1e-13));
        CHECK_THAT(mm(s.length(round).value()), WithinRel(14.0 * pi, 1e-15));
        CHECK(errorCode(s.radius(wide)) == ErrorCode::InvalidArgument);
    }
    SECTION("a spline on equally spaced poles along a line") {
        std::vector<Point2D> poles;
        for (int i = 0; i <= 5; ++i) {
            poles.push_back(Point2D{(3.0 + 8.0 * i) * units::mm, (-1.0 + 6.0 * i) * units::mm});
        }
        for (int degree = 2; degree <= 5; ++degree) {
            CAPTURE(degree);
            const EntityId straight = require(s.addSpline(poles, degree, false));
            CHECK_THAT(mm(s.length(straight).value()), WithinRel(50.0, 1e-13)); // 5 * |(8, 6)|
        }
    }
    SECTION("a parabola, by its closed-form arc length") {
        // B'(t) = 2 a t + b with a = P0 - 2 P1 + P2, b = 2 (P1 - P0):
        // L = integral of sqrt(A t^2 + B t + C) over [0, 1].
        const double ax = 0.0 - 2.0 * 20.0 + 60.0;
        const double ay = 0.0 - 2.0 * 40.0 + 0.0;
        const double bx = 2.0 * 20.0;
        const double by = 2.0 * 40.0;
        const double A = 4.0 * (ax * ax + ay * ay);
        const double B = 4.0 * (ax * bx + ay * by);
        const double C = bx * bx + by * by;
        const auto antiderivative = [&](double t) {
            const double q = std::sqrt(A * t * t + B * t + C);
            return (2.0 * A * t + B) / (4.0 * A) * q +
                   (4.0 * A * C - B * B) / (8.0 * std::pow(A, 1.5)) *
                       std::log(2.0 * std::sqrt(A) * q + 2.0 * A * t + B);
        };
        const EntityId parabola =
            require(s.addSpline({Point2D{0_mm, 0_mm}, Point2D{20_mm, 40_mm}, Point2D{60_mm, 0_mm}}, 2, false));
        CHECK_THAT(mm(s.length(parabola).value()), WithinRel(antiderivative(1.0) - antiderivative(0.0), 1e-12));
    }
}

TEST_CASE("SketchEllipseSpline_BoundsContainTheCurves", "[sketch][ellipse][spline][p12]") {
    Sketch s("Bounds");
    SECTION("an axis-aligned ellipse is bounded by its vertices") {
        const EntityId e = require(s.addEllipse(Point2D{10_mm, -5_mm}, 30_mm, 12_mm));
        const BoundingBox2D box = s.boundingBox(e).value();
        CHECK_THAT(mm(box.min.x), WithinAbs(-20.0, kExactMm));
        CHECK_THAT(mm(box.max.x), WithinAbs(40.0, kExactMm));
        CHECK_THAT(mm(box.min.y), WithinAbs(-17.0, kExactMm));
        CHECK_THAT(mm(box.max.y), WithinAbs(7.0, kExactMm));
    }
    SECTION("a turned ellipse's bounds are its extreme points") {
        const EntityId e = require(s.addEllipse(Point2D{10_mm, -5_mm}, 30_mm, 12_mm, 30_deg));
        const BoundingBox2D box = s.boundingBox(e).value();
        // Dense sampling of the ellipse: with 720000 samples the extremes are
        // found to 30 mm * (pi / 720000)^2 / 2 < 1e-9 mm.
        double minX = 1e9;
        double maxX = -1e9;
        double minY = 1e9;
        double maxY = -1e9;
        const double c = std::cos(pi / 6.0);
        const double sn = std::sin(pi / 6.0);
        for (int k = 0; k < 720000; ++k) {
            const double t = 2.0 * pi * k / 720000.0;
            const double x = 10.0 + 30.0 * std::cos(t) * c - 12.0 * std::sin(t) * sn;
            const double y = -5.0 + 30.0 * std::cos(t) * sn + 12.0 * std::sin(t) * c;
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
        }
        CHECK_THAT(mm(box.min.x), WithinAbs(minX, 1e-8));
        CHECK_THAT(mm(box.max.x), WithinAbs(maxX, 1e-8));
        CHECK_THAT(mm(box.min.y), WithinAbs(minY, 1e-8));
        CHECK_THAT(mm(box.max.y), WithinAbs(maxY, 1e-8));
    }
    SECTION("a spline is bounded by its poles") {
        const EntityId spline = require(s.addSpline(
            {Point2D{0_mm, 0_mm}, Point2D{10_mm, 40_mm}, Point2D{35_mm, -20_mm}, Point2D{60_mm, 30_mm}}, 3, false));
        const BoundingBox2D box = s.boundingBox(spline).value();
        CHECK(box.min == Point2D{0_mm, -20_mm});
        CHECK(box.max == Point2D{60_mm, 40_mm});
        CHECK(s.boundingBox()->max == box.max);
    }
}

// --- Solver ----------------------------------------------------------------------------------------

TEST_CASE("SketchEllipse_SolverKeepsTheAxesPerpendicular", "[sketch][ellipse][solver][p12]") {
    Sketch s("Ellipse");
    const EntityId ellipse = require(s.addEllipse(Point2D{4_mm, 3_mm}, 25_mm, 15_mm, 10_deg));
    const EllipseEntity e = ellipseOf(s, ellipse);
    SECTION("a free ellipse has five degrees of freedom") {
        checkSolved(analyze(s), SolveStatus::UnderConstrained, 5);
    }
    SECTION("centre, orientation and both semi-axes fix it") {
        require(s.addFixed(e.center));
        require(s.addHorizontal(e.center, e.xVertex));
        require(s.addDistance(e.center, e.xVertex, 30_mm));
        require(s.addDistance(e.center, e.yVertex, 12_mm));
        checkSolved(solve(s), SolveStatus::FullyConstrained, 0);
        const Point2D c = at(s, e.center);
        const Point2D x = at(s, e.xVertex);
        const Point2D y = at(s, e.yVertex);
        CHECK_THAT(mm(x.x - c.x), WithinAbs(30.0, kTolMm));
        CHECK_THAT(mm(x.y - c.y), WithinAbs(0.0, kTolMm));
        CHECK_THAT(mm(y.x - c.x), WithinAbs(0.0, kTolMm));
        CHECK_THAT(mm(y.y - c.y), WithinAbs(12.0, kTolMm));
    }
    SECTION("a vertex moved off its axis is brought back") {
        REQUIRE(s.setPointPosition(e.yVertex, Point2D{0_mm, 20_mm}).has_value());
        require(s.addFixed(e.center));
        require(s.addFixed(e.xVertex));
        const SolveResult result = solve(s);
        checkSolved(result, SolveStatus::UnderConstrained, 1); // the Y vertex slides along its axis
        CHECK_THAT(cosine(at(s, e.center), at(s, e.xVertex), at(s, e.center), at(s, e.yVertex)),
                   WithinAbs(0.0, 1e-10));
    }
    SECTION("a second orientation constraint is redundant") {
        require(s.addHorizontal(e.center, e.xVertex));
        const ConstraintId vertical = require(s.addVertical(e.center, e.yVertex));
        const Sketch before = s;
        const SolveResult result = solve(s);
        CHECK(result.status == SolveStatus::OverConstrained);
        CHECK(result.redundant == std::vector<ConstraintId>{vertical});
        CHECK(s.contentEquals(before));
    }
    SECTION("a Y vertex held on the X axis is inconsistent") {
        require(s.addFixed(e.center));
        require(s.addFixed(e.xVertex));
        const ConstraintId distance = require(s.addDistance(e.center, e.yVertex, 5_mm));
        // The Y vertex on the line through the centre and the X vertex.
        const EntityId axis = require(s.addLine(e.center, e.xVertex));
        const ConstraintId onLine = require(s.addDistance(e.yVertex, axis, 0_mm));
        const Sketch before = s;
        const SolveResult result = solve(s);
        INFO(result.message);
        CHECK(result.status == SolveStatus::Inconsistent);
        CHECK_FALSE(result.conflicting.empty());
        for (const ConstraintId id : result.conflicting) {
            CHECK((id == distance || id == onLine));
        }
        CHECK(s.contentEquals(before));
    }
    SECTION("a vertex on the centre is refused") {
        // With the X vertex on the centre, the perpendicularity condition
        // degenerates into the coincident constraint's equations: the solver
        // reports the dependency and writes nothing.
        const ConstraintId collapse = require(s.addCoincident(e.center, e.xVertex));
        const Sketch before = s;
        const SolveResult result = solve(s);
        CHECK(result.status == SolveStatus::OverConstrained);
        CHECK(result.redundant == std::vector<ConstraintId>{collapse});
        CHECK(s.contentEquals(before));
    }
    SECTION("concentric with a circle") {
        const EntityId circle = require(s.addCircle(Point2D{10_mm, 10_mm}, 5_mm));
        require(s.addConcentric(circle, ellipse));
        checkSolved(solve(s), SolveStatus::UnderConstrained, 5 + 3 - 2);
        CHECK_THAT(mm(distance(s.center(circle).value(), s.center(ellipse).value())), WithinAbs(0.0, kTolMm));
    }
}

TEST_CASE("SketchSpline_PolesAreFreePoints", "[sketch][spline][solver][p12]") {
    Sketch s("Spline");
    const EntityId open = require(s.addSpline({Point2D{0_mm, 0_mm}, Point2D{10_mm, 20_mm}, Point2D{30_mm, 20_mm},
                                               Point2D{40_mm, 0_mm}, Point2D{50_mm, 10_mm}},
                                              3, false));
    const EntityId closed = require(s.addSpline(
        {Point2D{100_mm, 0_mm}, Point2D{120_mm, 0_mm}, Point2D{120_mm, 20_mm}, Point2D{100_mm, 20_mm}}, 2, true));
    checkSolved(analyze(s), SolveStatus::UnderConstrained, 10 + 8);
    for (const EntityId pole : splineOf(s, open).poles) {
        require(s.addFixed(pole));
    }
    checkSolved(analyze(s), SolveStatus::UnderConstrained, 8);

    const auto& poles = splineOf(s, closed).poles;
    for (std::size_t i = 1; i < poles.size(); ++i) {
        require(s.addCoincident(poles[0], poles[i]));
    }
    const SolveResult collapsed = solve(s);
    CHECK(collapsed.status == SolveStatus::SolverFailure);
    CHECK(collapsed.message == "the solution collapses entity:11 to zero size");
}

TEST_CASE("SketchSpline_TangentToALineAtASharedEnd", "[sketch][spline][solver][p12]") {
    Sketch s("Joint");
    const EntityId line = require(s.addLine(Point2D{-40_mm, 0_mm}, Point2D{0_mm, 0_mm}));
    const LineEntity l = std::get<LineEntity>(s.findEntity(line)->geometry);
    const EntityId p1 = require(s.addPoint(Point2D{15_mm, 8_mm}));
    const EntityId p2 = require(s.addPoint(Point2D{30_mm, 20_mm}));
    const EntityId p3 = require(s.addPoint(Point2D{40_mm, 40_mm}));
    const EntityId spline = require(s.addSpline(std::vector<EntityId>{l.end, p1, p2, p3}, 3, false));
    for (const EntityId fixed : {l.start, l.end, p2, p3}) {
        require(s.addFixed(fixed));
    }
    const ConstraintId tangent = require(s.addTangent(spline, line));
    CHECK(s.findConstraint(tangent)->entities == std::vector<EntityId>{line, spline});

    checkSolved(solve(s), SolveStatus::UnderConstrained, 1); // the second pole slides along the line
    // The curve leaves the joint along 3 (P1 - P0) (Bernstein derivative).
    const Point2D p0 = at(s, l.end);
    CHECK_THAT(sine(at(s, l.start), p0, p0, at(s, p1)), WithinAbs(0.0, 1e-10));
    CHECK(mm(at(s, p1).x) > 0.0); // smooth, not a cusp
    CHECK_THAT(mm(at(s, p1).y), WithinAbs(0.0, kTolMm));
}

TEST_CASE("SketchSpline_TangentToAnArcOrASplineAtAJoint", "[sketch][spline][solver][p12]") {
    SECTION("an arc: the spline leaves perpendicular to the radius") {
        Sketch s("Arc");
        const EntityId arc = require(s.addArc(Point2D{0_mm, 0_mm}, 20_mm, 0_deg, 90_deg));
        const ArcEntity a = std::get<ArcEntity>(s.findEntity(arc)->geometry);
        const EntityId p1 = require(s.addPoint(Point2D{-10_mm, 23_mm}));
        const EntityId p2 = require(s.addPoint(Point2D{-30_mm, 30_mm}));
        const EntityId spline = require(s.addSpline(std::vector<EntityId>{a.end, p1, p2}, 2, false));
        for (const EntityId fixed : {a.center, a.start, a.end, p2}) {
            require(s.addFixed(fixed));
        }
        require(s.addTangent(arc, spline));
        checkSolved(solve(s), SolveStatus::UnderConstrained, 1);
        CHECK_THAT(cosine(at(s, a.center), at(s, a.end), at(s, a.end), at(s, p1)), WithinAbs(0.0, 1e-10));
        CHECK_THAT(mm(at(s, p1).y), WithinAbs(20.0, kTolMm));
    }
    SECTION("another spline joined by a coincident constraint") {
        Sketch s("Splines");
        const EntityId first = require(
            s.addSpline({Point2D{0_mm, 0_mm}, Point2D{10_mm, 10_mm}, Point2D{20_mm, 10_mm}}, 2, false));
        const EntityId second = require(s.addSpline(
            {Point2D{20.5_mm, 10.5_mm}, Point2D{30_mm, 15_mm}, Point2D{40_mm, 0_mm}, Point2D{50_mm, 0_mm}}, 3, false));
        const SplineEntity a = splineOf(s, first);
        const SplineEntity b = splineOf(s, second);
        for (const EntityId fixed : {a.poles[0], a.poles[1], a.poles[2], b.poles[2], b.poles[3]}) {
            require(s.addFixed(fixed));
        }
        require(s.addCoincident(a.poles[2], b.poles[0]));
        require(s.addTangent(first, second));
        checkSolved(solve(s), SolveStatus::UnderConstrained, 1);
        CHECK_THAT(mm(distance(at(s, a.poles[2]), at(s, b.poles[0]))), WithinAbs(0.0, kTolMm));
        CHECK_THAT(sine(at(s, a.poles[1]), at(s, a.poles[2]), at(s, b.poles[0]), at(s, b.poles[1])),
                   WithinAbs(0.0, 1e-10));
        CHECK(cosine(at(s, a.poles[1]), at(s, a.poles[2]), at(s, b.poles[0]), at(s, b.poles[1])) > 0.0);
    }
}

TEST_CASE("SketchSpline_TangentNeedsAJoint", "[sketch][spline][solver][p12]") {
    Sketch s("Joints");
    const EntityId line = require(s.addLine(Point2D{-40_mm, 0_mm}, Point2D{0_mm, 0_mm}));
    const EntityId apart =
        require(s.addSpline({Point2D{1_mm, 0_mm}, Point2D{10_mm, 10_mm}, Point2D{20_mm, 10_mm}}, 2, false));
    const EntityId periodic = require(s.addSpline(
        {Point2D{100_mm, 0_mm}, Point2D{120_mm, 0_mm}, Point2D{120_mm, 20_mm}, Point2D{100_mm, 20_mm}}, 2, true));
    const EntityId circle = require(s.addCircle(Point2D{0_mm, 50_mm}, 5_mm));

    CHECK(message(s.addTangent(line, apart)) ==
          "entity:3 and entity:7 share no end point: a tangent constraint with a spline applies where the two "
          "meet (at one point, or at two a coincident constraint joins)");
    CHECK(message(s.addTangent(line, periodic)) ==
          "a tangent constraint takes an open spline, but entity:12 is periodic and has no end points");
    CHECK_THAT(message(s.addTangent(circle, apart)), ContainsSubstring("got circle, spline"));
    CHECK(s.constraintCount() == 0);

    // A joint made by a coincident constraint that is later removed.
    const LineEntity l = std::get<LineEntity>(s.findEntity(line)->geometry);
    const ConstraintId joined = require(s.addCoincident(l.end, splineOf(s, apart).poles.front()));
    const ConstraintId tangent = require(s.addTangent(line, apart));
    REQUIRE(s.removeConstraint(joined).has_value());
    const SolveResult result = solve(s);
    CHECK(result.status == SolveStatus::SolverFailure);
    CHECK(result.message == std::format("{} needs entity:3 and entity:7 to share an end point", tangent));

    // Loading does not look for the joint (a file may list it later).
    Sketch loaded("Loaded");
    for (const Entity& entity : s.entities()) {
        REQUIRE(loaded.insertEntity(entity).has_value());
    }
    CHECK(loaded.insertConstraint(*s.findConstraint(tangent)).has_value());
}

TEST_CASE("SketchEllipseSpline_SolveDeterministically", "[sketch][ellipse][spline][solver][p12]") {
    const auto build = [] {
        Sketch s("Twin");
        const EntityId ellipse = require(s.addEllipse(Point2D{4_mm, 3_mm}, 25_mm, 15_mm, 10_deg));
        const EllipseEntity e = std::get<EllipseEntity>(s.findEntity(ellipse)->geometry);
        require(s.addFixed(e.center));
        require(s.addDistance(e.center, e.xVertex, 30_mm));
        const EntityId line = require(s.addLine(Point2D{-40_mm, 0_mm}, Point2D{0_mm, 0_mm}));
        const LineEntity l = std::get<LineEntity>(s.findEntity(line)->geometry);
        const EntityId p1 = require(s.addPoint(Point2D{15_mm, 8_mm}));
        const EntityId p2 = require(s.addPoint(Point2D{30_mm, 20_mm}));
        const EntityId spline = require(s.addSpline(std::vector<EntityId>{l.end, p1, p2}, 2, false));
        require(s.addTangent(line, spline));
        require(s.addDistance(p2, e.yVertex, 20_mm));
        return s;
    };
    Sketch first = build();
    Sketch second = build();
    const SolveResult one = solve(first);
    const SolveResult two = solve(second);
    REQUIRE(one.solved());
    CHECK(one.iterations == two.iterations);
    CHECK(first.contentEquals(second));
    const SolveResult again = solve(first);
    CHECK(again.solved());
    CHECK_FALSE(again.geometryChanged);
}
