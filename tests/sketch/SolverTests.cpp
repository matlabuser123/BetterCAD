#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/sketch/Solver.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <memory>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::sketch;
using Catch::Matchers::WithinAbs;

namespace {

// Geometric checks in millimetres. The solver iterates until residuals are
// below 1e-13 m (1e-10 mm); 1e-8 mm leaves a margin of two orders.
constexpr double kTolMm = 1e-8;

template <typename Id>
Id require(const Result<Id>& id) {
    REQUIRE(id.has_value());
    return *id;
}

Point2D at(const Sketch& sketch, EntityId point) {
    const auto position = sketch.position(point);
    REQUIRE(position.has_value());
    return *position;
}

void checkPoint(const Point2D& p, double xMm, double yMm) {
    CHECK_THAT(p.x.in(units::mm), WithinAbs(xMm, kTolMm));
    CHECK_THAT(p.y.in(units::mm), WithinAbs(yMm, kTolMm));
}

double lengthMm(const Sketch& sketch, EntityId entity) {
    const auto length = sketch.length(entity);
    REQUIRE(length.has_value());
    return length->in(units::mm);
}

void checkSolved(const SolveResult& result, SolveStatus expected, std::size_t dof) {
    INFO(result.message);
    CHECK(result.status == expected);
    CHECK(result.degreesOfFreedom == dof);
    CHECK(result.maxResidual <= SolverOptions{}.tolerance);
    CHECK(result.conflicting.empty());
    CHECK(result.redundant.empty());
}

struct Rectangle {
    Sketch sketch{"Rectangle"};
    EntityId p1, p2, p3, p4, bottom, right, top, left;
    ConstraintId width, height;

    // Four connected lines, deliberately not rectangular yet.
    Rectangle() {
        p1 = require(sketch.addPoint(Point2D{0_mm, 0_mm}));
        p2 = require(sketch.addPoint(Point2D{90_mm, 5_mm}));
        p3 = require(sketch.addPoint(Point2D{95_mm, 45_mm}));
        p4 = require(sketch.addPoint(Point2D{-3_mm, 52_mm}));
        bottom = require(sketch.addLine(p1, p2));
        right = require(sketch.addLine(p2, p3));
        top = require(sketch.addLine(p3, p4));
        left = require(sketch.addLine(p4, p1));
        require(sketch.addHorizontal(bottom));
        require(sketch.addHorizontal(top));
        require(sketch.addVertical(left));
        require(sketch.addVertical(right));
        width = require(sketch.addDistance(bottom, 100_mm));
        height = require(sketch.addDistance(right, 50_mm));
    }
};

} // namespace

// --- Simple cases -----------------------------------------------------------------------

TEST_CASE("Solver: horizontal line", "[solver]") {
    Sketch sketch("Sketch1");
    const EntityId line = require(sketch.addLine(Point2D{0_mm, 0_mm}, Point2D{100_mm, 10_mm}));
    require(sketch.addHorizontal(line));

    const SolveResult result = solve(sketch);
    checkSolved(result, SolveStatus::UnderConstrained, 3);
    CHECK(result.unknowns == 4);
    CHECK(result.equations == 1);
    CHECK(result.geometryChanged);
    // Minimum-norm correction: both ends move by half the offset.
    const Endpoints ends = sketch.endpoints(line).value();
    checkPoint(ends.start, 0.0, 5.0);
    checkPoint(ends.end, 100.0, 5.0);
}

TEST_CASE("Solver: vertical line", "[solver]") {
    Sketch sketch("Sketch1");
    const EntityId line = require(sketch.addLine(Point2D{0_mm, 0_mm}, Point2D{10_mm, 100_mm}));
    require(sketch.addVertical(line));

    checkSolved(solve(sketch), SolveStatus::UnderConstrained, 3);
    const Endpoints ends = sketch.endpoints(line).value();
    checkPoint(ends.start, 5.0, 0.0);
    checkPoint(ends.end, 5.0, 100.0);
}

TEST_CASE("Solver: fixed distance", "[solver]") {
    Sketch sketch("Sketch1");
    const EntityId line = require(sketch.addLine(Point2D{0_mm, 0_mm}, Point2D{100_mm, 0_mm}));
    require(sketch.addDistance(line, 80_mm));

    checkSolved(solve(sketch), SolveStatus::UnderConstrained, 3);
    CHECK_THAT(lengthMm(sketch, line), WithinAbs(80.0, kTolMm));
    const Endpoints ends = sketch.endpoints(line).value();
    checkPoint(ends.start, 10.0, 0.0);
    checkPoint(ends.end, 90.0, 0.0);
}

TEST_CASE("Solver: circle radius", "[solver]") {
    Sketch sketch("Sketch1");
    const EntityId circle = require(sketch.addCircle(Point2D{10_mm, 10_mm}, 5_mm));
    require(sketch.addRadius(circle, 12.5_mm));

    const SolveResult result = solve(sketch);
    checkSolved(result, SolveStatus::UnderConstrained, 2); // the centre is free
    CHECK_THAT(sketch.radius(circle)->in(units::mm), WithinAbs(12.5, kTolMm));
    checkPoint(sketch.center(circle).value(), 10.0, 10.0);

    const EntityId center = std::get<CircleEntity>(sketch.findEntity(circle)->geometry).center;
    require(sketch.addFixed(center));
    checkSolved(solve(sketch), SolveStatus::FullyConstrained, 0);
}

// --- Acceptance: rectangle ---------------------------------------------------------------

TEST_CASE("Solver: rectangle reaches 100 x 50 mm as a closed profile", "[solver][acceptance]") {
    Rectangle r;

    SECTION("without an anchor it is solved but can still translate") {
        const SolveResult result = solve(r.sketch);
        checkSolved(result, SolveStatus::UnderConstrained, 2);
        CHECK(result.unknowns == 8);
        CHECK(result.equations == 6);
    }
    SECTION("with a fixed corner it is fully constrained") {
        require(r.sketch.addFixed(r.p1));
        const SolveResult result = solve(r.sketch);
        checkSolved(result, SolveStatus::FullyConstrained, 0);
        checkPoint(at(r.sketch, r.p1), 0.0, 0.0);
        checkPoint(at(r.sketch, r.p2), 100.0, 0.0);
        checkPoint(at(r.sketch, r.p3), 100.0, 50.0);
        checkPoint(at(r.sketch, r.p4), 0.0, 50.0);
    }

    CHECK_THAT(lengthMm(r.sketch, r.bottom), WithinAbs(100.0, kTolMm));
    CHECK_THAT(lengthMm(r.sketch, r.top), WithinAbs(100.0, kTolMm));
    CHECK_THAT(lengthMm(r.sketch, r.right), WithinAbs(50.0, kTolMm));
    CHECK_THAT(lengthMm(r.sketch, r.left), WithinAbs(50.0, kTolMm));
    // Closed: each line ends where the next one starts.
    const std::vector<EntityId> loop{r.bottom, r.right, r.top, r.left};
    for (std::size_t i = 0; i < loop.size(); ++i) {
        CHECK(r.sketch.endpoints(loop[i])->end == r.sketch.endpoints(loop[(i + 1) % loop.size()])->start);
    }
}

TEST_CASE("Solver: separate lines are closed into a rectangle by coincident constraints",
          "[solver][acceptance]") {
    Sketch sketch("Rectangle");
    const EntityId bottom = require(sketch.addLine(Point2D{0_mm, 0_mm}, Point2D{95_mm, 3_mm}));
    const EntityId right = require(sketch.addLine(Point2D{97_mm, -2_mm}, Point2D{102_mm, 48_mm}));
    const EntityId top = require(sketch.addLine(Point2D{99_mm, 51_mm}, Point2D{4_mm, 47_mm}));
    const EntityId left = require(sketch.addLine(Point2D{-2_mm, 55_mm}, Point2D{1_mm, 2_mm}));
    const std::vector<EntityId> loop{bottom, right, top, left};
    const auto endPoint = [&](EntityId line) { return std::get<LineEntity>(sketch.findEntity(line)->geometry).end; };
    const auto startPoint = [&](EntityId line) { return std::get<LineEntity>(sketch.findEntity(line)->geometry).start; };
    for (std::size_t i = 0; i < loop.size(); ++i) {
        require(sketch.addCoincident(endPoint(loop[i]), startPoint(loop[(i + 1) % loop.size()])));
    }
    require(sketch.addHorizontal(bottom));
    require(sketch.addVertical(right));
    require(sketch.addHorizontal(top));
    require(sketch.addVertical(left));
    require(sketch.addDistance(bottom, 100_mm));
    require(sketch.addDistance(right, 50_mm));
    require(sketch.addFixed(startPoint(bottom)));

    const SolveResult result = solve(sketch);
    checkSolved(result, SolveStatus::FullyConstrained, 0);
    CHECK(result.unknowns == 14);
    CHECK(result.equations == 14);
    CHECK_THAT(lengthMm(sketch, bottom), WithinAbs(100.0, kTolMm));
    CHECK_THAT(lengthMm(sketch, right), WithinAbs(50.0, kTolMm));
    for (std::size_t i = 0; i < loop.size(); ++i) {
        const Length gap = distance(sketch.endpoints(loop[i])->end,
                                    sketch.endpoints(loop[(i + 1) % loop.size()])->start);
        CHECK(gap <= SolverOptions{}.tolerance);
    }
    checkPoint(sketch.endpoints(top)->start, 100.0, 50.0);
}

// --- Acceptance: conflicts ---------------------------------------------------------------

TEST_CASE("Solver: conflicting lengths are reported, not applied", "[solver][acceptance]") {
    Sketch sketch("Sketch1");
    const EntityId line = require(sketch.addLine(Point2D{0_mm, 0_mm}, Point2D{150_mm, 0_mm}));
    const ConstraintId short100 = require(sketch.addDistance(line, 100_mm));
    const ConstraintId long200 = require(sketch.addDistance(line, 200_mm));
    const Endpoints before = sketch.endpoints(line).value();

    const SolveResult result = solve(sketch);
    INFO(result.message);
    CHECK(result.status == SolveStatus::Inconsistent);
    CHECK(result.conflicting == std::vector<ConstraintId>{short100, long200});
    CHECK_THAT(result.maxResidual.in(units::mm), WithinAbs(50.0, 1e-6)); // best compromise: 150 mm
    CHECK_FALSE(result.geometryChanged);
    CHECK(sketch.endpoints(line).value() == before); // no arbitrary geometry

    // Disabling one of them makes the sketch solvable again.
    REQUIRE(sketch.setConstraintEnabled(long200, false).value());
    checkSolved(solve(sketch), SolveStatus::UnderConstrained, 3);
    CHECK_THAT(lengthMm(sketch, line), WithinAbs(100.0, kTolMm));
}

TEST_CASE("Solver: redundant constraints are over-constrained and not applied", "[solver]") {
    SECTION("the same constraint twice") {
        Sketch sketch("Sketch1");
        const EntityId line = require(sketch.addLine(Point2D{0_mm, 0_mm}, Point2D{100_mm, 10_mm}));
        require(sketch.addHorizontal(line));
        const ConstraintId again = require(sketch.addHorizontal(line));
        const Sketch before = sketch;

        const SolveResult result = solve(sketch);
        CHECK(result.status == SolveStatus::OverConstrained);
        CHECK(result.redundant == std::vector<ConstraintId>{again});
        CHECK_FALSE(result.geometryChanged);
        CHECK(sketch.contentEquals(before));
    }
    SECTION("a parallel constraint implied by two horizontals") {
        Rectangle r;
        const ConstraintId parallel = require(r.sketch.addParallel(r.bottom, r.top));
        const SolveResult result = solve(r.sketch);
        CHECK(result.status == SolveStatus::OverConstrained);
        CHECK(result.redundant == std::vector<ConstraintId>{parallel});
    }
    SECTION("a distance between two fixed points") {
        Sketch sketch("Sketch1");
        const EntityId a = require(sketch.addPoint(Point2D{0_mm, 0_mm}));
        const EntityId b = require(sketch.addPoint(Point2D{30_mm, 40_mm}));
        require(sketch.addFixed(a));
        require(sketch.addFixed(b));
        const ConstraintId matching = require(sketch.addDistance(a, b, 50_mm));
        const SolveResult consistent = analyze(sketch);
        CHECK(consistent.status == SolveStatus::OverConstrained);
        CHECK(consistent.redundant == std::vector<ConstraintId>{matching});

        REQUIRE(sketch.setConstraintValue(matching, 60_mm).value());
        const SolveResult impossible = analyze(sketch);
        CHECK(impossible.status == SolveStatus::Inconsistent);
        CHECK(impossible.conflicting == std::vector<ConstraintId>{matching});
    }
}

// --- Other constraint types ----------------------------------------------------------------

TEST_CASE("Solver: coincident, parallel and perpendicular", "[solver]") {
    Sketch sketch("Sketch1");
    const EntityId base = require(sketch.addLine(Point2D{0_mm, 0_mm}, Point2D{100_mm, 0_mm}));
    const auto& baseLine = std::get<LineEntity>(sketch.findEntity(base)->geometry);
    require(sketch.addFixed(baseLine.start));
    require(sketch.addFixed(baseLine.end));

    SECTION("coincident") {
        const EntityId a = require(sketch.addPoint(Point2D{10_mm, 5_mm}));
        require(sketch.addCoincident(a, baseLine.end));
        checkSolved(solve(sketch), SolveStatus::FullyConstrained, 0);
        checkPoint(at(sketch, a), 100.0, 0.0);
    }
    SECTION("parallel") {
        const EntityId other = require(sketch.addLine(Point2D{0_mm, 10_mm}, Point2D{100_mm, 30_mm}));
        require(sketch.addParallel(other, base));
        checkSolved(solve(sketch), SolveStatus::UnderConstrained, 3);
        const Endpoints ends = sketch.endpoints(other).value();
        CHECK_THAT((ends.end.y - ends.start.y).in(units::mm), WithinAbs(0.0, kTolMm));
    }
    SECTION("perpendicular") {
        const EntityId other = require(sketch.addLine(Point2D{50_mm, 0_mm}, Point2D{60_mm, 80_mm}));
        require(sketch.addPerpendicular(other, base));
        checkSolved(solve(sketch), SolveStatus::UnderConstrained, 3);
        const Endpoints ends = sketch.endpoints(other).value();
        CHECK_THAT((ends.end.x - ends.start.x).in(units::mm), WithinAbs(0.0, kTolMm));
    }
    SECTION("point-to-line distance keeps the point on its side") {
        const EntityId above = require(sketch.addPoint(Point2D{50_mm, 30_mm}));
        const EntityId below = require(sketch.addPoint(Point2D{20_mm, -5_mm}));
        require(sketch.addDistance(above, base, 20_mm));
        require(sketch.addDistance(below, base, 15_mm));
        checkSolved(solve(sketch), SolveStatus::UnderConstrained, 2);
        CHECK_THAT(at(sketch, above).y.in(units::mm), WithinAbs(20.0, kTolMm));
        CHECK_THAT(at(sketch, below).y.in(units::mm), WithinAbs(-15.0, kTolMm));
    }
    SECTION("equal lengths") {
        const EntityId other = require(sketch.addLine(Point2D{0_mm, 20_mm}, Point2D{60_mm, 20_mm}));
        require(sketch.addEqual(other, base));
        checkSolved(solve(sketch), SolveStatus::UnderConstrained, 3);
        CHECK_THAT(lengthMm(sketch, other), WithinAbs(100.0, kTolMm));
    }
}

TEST_CASE("Solver: arcs keep both ends on their circle", "[solver]") {
    Sketch sketch("Sketch1");
    const EntityId arc = require(sketch.addArc(Point2D{0_mm, 0_mm}, 10_mm, 0_deg, 90_deg));
    const EntityId circle = require(sketch.addCircle(Point2D{50_mm, 0_mm}, 4_mm));
    require(sketch.addRadius(arc, 15_mm));
    require(sketch.addEqual(circle, arc));

    const SolveResult result = solve(sketch);
    INFO(result.message);
    CHECK(result.status == SolveStatus::UnderConstrained);
    const auto& geometry = std::get<ArcEntity>(sketch.findEntity(arc)->geometry);
    const Point2D c = at(sketch, geometry.center);
    CHECK_THAT(distance(c, at(sketch, geometry.start)).in(units::mm), WithinAbs(15.0, kTolMm));
    CHECK_THAT(distance(c, at(sketch, geometry.end)).in(units::mm), WithinAbs(15.0, kTolMm));
    CHECK_THAT(sketch.radius(circle)->in(units::mm), WithinAbs(15.0, kTolMm));
}

// --- Edge cases and diagnostics ---------------------------------------------------------------

TEST_CASE("Solver: empty and unconstrained sketches", "[solver]") {
    Sketch empty("Empty");
    const SolveResult none = solve(empty);
    CHECK(none.status == SolveStatus::FullyConstrained);
    CHECK(none.unknowns == 0);
    CHECK_FALSE(none.geometryChanged);

    Sketch loose("Loose");
    require(loose.addPoint(Point2D{1_mm, 2_mm}));
    require(loose.addCircle(Point2D{}, 3_mm));
    const SolveResult free = solve(loose);
    CHECK(free.status == SolveStatus::UnderConstrained);
    CHECK(free.degreesOfFreedom == 5); // point 2 + circle centre 2 + radius 1
    CHECK_FALSE(free.geometryChanged);
}

TEST_CASE("Solver: geometry that would collapse is rejected", "[solver]") {
    Sketch sketch("Sketch1");
    const EntityId line = require(sketch.addLine(Point2D{0_mm, 0_mm}, Point2D{10_mm, 0_mm}));
    const auto& ends = std::get<LineEntity>(sketch.findEntity(line)->geometry);
    require(sketch.addCoincident(ends.start, ends.end));
    const Sketch before = sketch;

    const SolveResult result = solve(sketch);
    CHECK(result.status == SolveStatus::SolverFailure);
    CHECK(sketch.contentEquals(before));
}

TEST_CASE("Solver: running out of iterations is a solver failure", "[solver]") {
    Rectangle r;
    const Sketch before = r.sketch;
    const SolveResult result = solve(r.sketch, SolverOptions{.tolerance = Length::fromSi(1e-10), .maxIterations = 0});
    CHECK(result.status == SolveStatus::SolverFailure);
    CHECK_FALSE(result.geometryChanged);
    CHECK(r.sketch.contentEquals(before));

    const SolveResult badOptions = solve(r.sketch, SolverOptions{.tolerance = Length{}, .maxIterations = 10});
    CHECK(badOptions.status == SolveStatus::SolverFailure);
}

TEST_CASE("Solver: status names", "[solver]") {
    CHECK(toString(SolveStatus::UnderConstrained) == "UNDER_CONSTRAINED");
    CHECK(toString(SolveStatus::FullyConstrained) == "FULLY_CONSTRAINED");
    CHECK(toString(SolveStatus::OverConstrained) == "OVER_CONSTRAINED");
    CHECK(toString(SolveStatus::Inconsistent) == "INCONSISTENT");
    CHECK(toString(SolveStatus::SolverFailure) == "SOLVER_FAILURE");
}

TEST_CASE("Solver: results are deterministic and analyze() does not modify", "[solver]") {
    Rectangle a;
    Rectangle b;
    const Sketch untouched = a.sketch;

    const SolveResult diagnosis = analyze(a.sketch);
    CHECK(diagnosis.status == SolveStatus::UnderConstrained);
    CHECK(a.sketch.contentEquals(untouched));

    const SolveResult first = solve(a.sketch);
    const SolveResult second = solve(b.sketch);
    CHECK(first.iterations == second.iterations);
    CHECK(a.sketch.contentEquals(b.sketch)); // bit-identical positions

    // Solving an already solved sketch changes nothing.
    const SolveResult again = solve(a.sketch);
    CHECK(again.solved());
    CHECK_FALSE(again.geometryChanged);
}

TEST_CASE("Solver: a sketch inside a document is solved through modifyObject", "[solver][document]") {
    Document doc("Part");
    auto sketch = std::make_unique<Sketch>("Sketch1");
    const EntityId line = require(sketch->addLine(Point2D{0_mm, 0_mm}, Point2D{100_mm, 10_mm}));
    require(sketch->addHorizontal(line));
    const auto id = doc.addObject(std::move(sketch));
    REQUIRE(id.has_value());
    const auto revision = doc.revision();

    SolveResult result;
    REQUIRE(doc.modifyObject<Sketch>(*id, [&](Sketch& s) -> Result<bool> {
                   result = solve(s);
                   return result.geometryChanged;
               }).value());
    CHECK(result.status == SolveStatus::UnderConstrained);
    CHECK(doc.revision() == revision + 1);
}
