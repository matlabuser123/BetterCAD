#include "TestHelpers.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::sketch;
using bettercad::test::errorCode;

namespace {

template <typename Id>
Id require(const Result<Id>& id) {
    REQUIRE(id.has_value());
    return *id;
}

// A 100 x 50 mm rectangle of four connected lines, a circle and an arc.
struct Fixture {
    Sketch sketch{"Sketch1"};
    EntityId p1, p2, p3, p4;
    EntityId bottom, right, top, left;
    EntityId circle, arc;

    Fixture() {
        p1 = require(sketch.addPoint(Point2D{0_mm, 0_mm}));
        p2 = require(sketch.addPoint(Point2D{100_mm, 0_mm}));
        p3 = require(sketch.addPoint(Point2D{100_mm, 50_mm}));
        p4 = require(sketch.addPoint(Point2D{0_mm, 50_mm}));
        bottom = require(sketch.addLine(p1, p2));
        right = require(sketch.addLine(p2, p3));
        top = require(sketch.addLine(p3, p4));
        left = require(sketch.addLine(p4, p1));
        circle = require(sketch.addCircle(Point2D{50_mm, 25_mm}, 10_mm));
        arc = require(sketch.addArc(Point2D{200_mm, 0_mm}, 20_mm, 0_deg, 90_deg));
    }
};

} // namespace

// --- Representation -----------------------------------------------------------------------

TEST_CASE("Each constraint type records its references, value and enabled state",
          "[sketch][constraints]") {
    Fixture f;
    Sketch& s = f.sketch;

    const ConstraintId coincident = require(s.addCoincident(f.p1, f.p2));
    const ConstraintId horizontal = require(s.addHorizontal(f.bottom));
    const ConstraintId horizontalPoints = require(s.addHorizontal(f.p3, f.p4));
    const ConstraintId vertical = require(s.addVertical(f.right));
    const ConstraintId parallel = require(s.addParallel(f.bottom, f.top));
    const ConstraintId perpendicular = require(s.addPerpendicular(f.bottom, f.left));
    const ConstraintId distance = require(s.addDistance(f.bottom, 100_mm));
    const ConstraintId radius = require(s.addRadius(f.circle, 10_mm));
    const ConstraintId equal = require(s.addEqual(f.circle, f.arc));
    const ConstraintId fixed = require(s.addFixed(f.p4));

    const auto check = [&](ConstraintId id, ConstraintType type, std::vector<EntityId> entities,
                           std::optional<Length> value) {
        const Constraint* c = s.findConstraint(id);
        REQUIRE(c != nullptr);
        CHECK(c->id == id);
        CHECK(c->type == type);
        CHECK(c->entities == entities);
        CHECK(c->value == value);
        CHECK_FALSE(c->parameter.has_value());
        CHECK(c->enabled);
    };
    check(coincident, ConstraintType::Coincident, {f.p1, f.p2}, std::nullopt);
    check(horizontal, ConstraintType::Horizontal, {f.bottom}, std::nullopt);
    check(horizontalPoints, ConstraintType::Horizontal, {f.p3, f.p4}, std::nullopt);
    check(vertical, ConstraintType::Vertical, {f.right}, std::nullopt);
    check(parallel, ConstraintType::Parallel, {f.bottom, f.top}, std::nullopt);
    check(perpendicular, ConstraintType::Perpendicular, {f.bottom, f.left}, std::nullopt);
    check(distance, ConstraintType::Distance, {f.bottom}, 100_mm);
    check(radius, ConstraintType::Radius, {f.circle}, 10_mm);
    check(equal, ConstraintType::Equal, {f.circle, f.arc}, std::nullopt);
    check(fixed, ConstraintType::Fixed, {f.p4}, std::nullopt);

    CHECK(s.constraintCount() == 10);
    CHECK(toString(ConstraintType::Fixed) == "fixed");
    CHECK(toString(ConstraintType::Perpendicular) == "perpendicular");
}

TEST_CASE("Spec setup: rectangle with horizontal/vertical constraints, width and height",
          "[sketch][constraints]") {
    Fixture f;
    Sketch& s = f.sketch;
    require(s.addHorizontal(f.bottom));
    require(s.addHorizontal(f.top));
    require(s.addVertical(f.left));
    require(s.addVertical(f.right));
    const ConstraintId width = require(s.addDistance(f.bottom, 100_mm));
    const ConstraintId height = require(s.addDistance(f.right, 50_mm));

    CHECK(s.constraintCount() == 6);
    CHECK(s.findConstraint(width)->value == 100_mm);
    CHECK(s.findConstraint(height)->value == 50_mm);
    CHECK(s.constraintsReferencing(f.bottom).size() == 2);
}

TEST_CASE("Constraint IDs are stable and never reused", "[sketch][constraints]") {
    Fixture f;
    Sketch& s = f.sketch;
    // A rejected constraint does not consume an ID.
    CHECK(errorCode(s.addParallel(f.bottom, f.circle)) == ErrorCode::InvalidArgument);
    const ConstraintId first = require(s.addHorizontal(f.bottom));
    const ConstraintId second = require(s.addVertical(f.right));
    CHECK(first.value() == 1);
    CHECK(second.value() == 2);

    REQUIRE(s.removeConstraint(first).has_value());
    CHECK(s.findConstraint(second)->type == ConstraintType::Vertical);
    const ConstraintId third = require(s.addHorizontal(f.top));
    CHECK(third.value() == 3);
}

TEST_CASE("Point-line distances are stored as (point, line)", "[sketch][constraints]") {
    Fixture f;
    const Point2D apex{50_mm, 80_mm};
    const EntityId point = require(f.sketch.addPoint(apex));

    const ConstraintId lineFirst = require(f.sketch.addDistance(f.bottom, point, 80_mm));
    CHECK(f.sketch.findConstraint(lineFirst)->entities == std::vector<EntityId>{point, f.bottom});
    // Zero is allowed: the point lies on the line.
    require(f.sketch.addDistance(point, f.top, 0_mm));
}

// --- Reference validation -------------------------------------------------------------------

TEST_CASE("Constraints cannot reference entities that do not exist", "[sketch][constraints]") {
    Fixture f;
    Sketch& s = f.sketch;
    const EntityId missing = EntityId::fromValue(9999);

    CHECK(errorCode(s.addCoincident(f.p1, missing)) == ErrorCode::NotFound);
    CHECK(errorCode(s.addHorizontal(missing)) == ErrorCode::NotFound);
    CHECK(errorCode(s.addVertical(missing, f.p1)) == ErrorCode::NotFound);
    CHECK(errorCode(s.addParallel(f.bottom, missing)) == ErrorCode::NotFound);
    CHECK(errorCode(s.addPerpendicular(missing, f.top)) == ErrorCode::NotFound);
    CHECK(errorCode(s.addDistance(missing, 10_mm)) == ErrorCode::NotFound);
    CHECK(errorCode(s.addRadius(missing, 10_mm)) == ErrorCode::NotFound);
    CHECK(errorCode(s.addEqual(f.bottom, missing)) == ErrorCode::NotFound);
    CHECK(errorCode(s.addHorizontal(EntityId{})) == ErrorCode::NotFound);
    CHECK(s.constraintCount() == 0);
}

TEST_CASE("Constraints check the kinds and number of referenced entities", "[sketch][constraints]") {
    Fixture f;
    Sketch& s = f.sketch;
    const auto invalid = [](const Result<ConstraintId>& r) {
        return errorCode(r) == ErrorCode::InvalidArgument;
    };

    CHECK(invalid(s.addCoincident(f.p1, f.bottom)));
    CHECK(invalid(s.addHorizontal(f.circle)));
    CHECK(invalid(s.addHorizontal(f.p1, f.bottom)));
    CHECK(invalid(s.addVertical(f.arc)));
    CHECK(invalid(s.addParallel(f.bottom, f.p1)));
    CHECK(invalid(s.addPerpendicular(f.circle, f.arc)));
    CHECK(invalid(s.addDistance(f.circle, 5_mm)));
    CHECK(invalid(s.addDistance(f.bottom, f.top, 5_mm))); // line-line distance is not supported
    CHECK(invalid(s.addRadius(f.bottom, 5_mm)));
    CHECK(invalid(s.addRadius(f.p1, 5_mm)));
    CHECK(invalid(s.addEqual(f.bottom, f.circle)));
    CHECK(invalid(s.addEqual(f.p1, f.p2)));
    CHECK(invalid(s.addFixed(f.bottom)));
    CHECK(invalid(s.addConstraint(ConstraintType::Fixed, {f.p1}, 1_mm)));
    CHECK(invalid(s.addConstraint(ConstraintType::Horizontal, {})));
    CHECK(invalid(s.addConstraint(ConstraintType::Parallel, {f.bottom, f.top, f.left})));
    CHECK(s.constraintCount() == 0);
}

TEST_CASE("Constraints must reference distinct entities", "[sketch][constraints]") {
    Fixture f;
    CHECK(errorCode(f.sketch.addCoincident(f.p1, f.p1)) == ErrorCode::InvalidArgument);
    CHECK(errorCode(f.sketch.addParallel(f.top, f.top)) == ErrorCode::InvalidArgument);
    CHECK(errorCode(f.sketch.addEqual(f.circle, f.circle)) == ErrorCode::InvalidArgument);
    CHECK(errorCode(f.sketch.addDistance(f.p2, f.p2, 1_mm)) == ErrorCode::InvalidArgument);
}

TEST_CASE("Only distance and radius constraints carry values, and values are checked",
          "[sketch][constraints]") {
    Fixture f;
    Sketch& s = f.sketch;
    const Length nan = Length::fromSi(std::numeric_limits<double>::quiet_NaN());

    CHECK(errorCode(s.addConstraint(ConstraintType::Distance, {f.bottom})) == ErrorCode::InvalidArgument);
    CHECK(errorCode(s.addConstraint(ConstraintType::Horizontal, {f.bottom}, 5_mm)) ==
          ErrorCode::InvalidArgument);
    CHECK(errorCode(s.addDistance(f.bottom, nan)) == ErrorCode::InvalidArgument);
    CHECK(errorCode(s.addDistance(f.bottom, 0_mm)) == ErrorCode::InvalidArgument);
    CHECK(errorCode(s.addDistance(f.p1, f.p3, -(1_mm))) == ErrorCode::InvalidArgument);
    CHECK(errorCode(s.addRadius(f.circle, 0_mm)) == ErrorCode::InvalidArgument);
    CHECK(s.constraintCount() == 0);
}

TEST_CASE("Entities referenced by constraints cannot be removed", "[sketch][constraints]") {
    Fixture f;
    Sketch& s = f.sketch;
    const ConstraintId radius = require(s.addRadius(f.circle, 10_mm));

    CHECK(s.constraintsReferencing(f.circle) == std::vector<ConstraintId>{radius});
    CHECK(errorCode(s.removeEntity(f.circle)) == ErrorCode::FailedPrecondition);
    REQUIRE(s.removeConstraint(radius).has_value());
    REQUIRE(s.removeEntity(f.circle).has_value());
    CHECK(errorCode(s.removeConstraint(radius)) == ErrorCode::NotFound);
}

// --- Editing ----------------------------------------------------------------------------------

TEST_CASE("Constraints can be disabled, changed and driven by parameters", "[sketch][constraints]") {
    Fixture f;
    Sketch& s = f.sketch;
    const ConstraintId width = require(s.addDistance(f.bottom, 100_mm));
    const ConstraintId horizontal = require(s.addHorizontal(f.bottom));

    CHECK(s.setConstraintEnabled(horizontal, false).value());
    CHECK_FALSE(s.findConstraint(horizontal)->enabled);
    CHECK_FALSE(s.setConstraintEnabled(horizontal, false).value());

    CHECK(s.setConstraintValue(width, 120_mm).value());
    CHECK(s.findConstraint(width)->value == 120_mm);
    CHECK_FALSE(s.setConstraintValue(width, 120_mm).value());
    CHECK(errorCode(s.setConstraintValue(width, -(1_mm))) == ErrorCode::InvalidArgument);
    CHECK(errorCode(s.setConstraintValue(horizontal, 1_mm)) == ErrorCode::InvalidArgument);
    CHECK(s.findConstraint(width)->value == 120_mm);

    const auto parameter = ParameterId::fromValue(7);
    CHECK(s.setConstraintParameter(width, parameter).value());
    CHECK(s.findConstraint(width)->parameter == parameter);
    CHECK(s.setConstraintParameter(width, std::nullopt).value());
    CHECK(errorCode(s.setConstraintParameter(horizontal, parameter)) == ErrorCode::InvalidArgument);
    CHECK(errorCode(s.setConstraintParameter(width, ParameterId{})) == ErrorCode::InvalidArgument);
    CHECK(errorCode(s.setConstraintEnabled(ConstraintId::fromValue(99), true)) == ErrorCode::NotFound);
}

TEST_CASE("insertConstraint restores a constraint with its ID and validates it",
          "[sketch][constraints]") {
    Fixture f;
    Sketch& s = f.sketch;
    const ConstraintId id = require(s.addDistance(f.bottom, 100_mm));
    const Constraint removed = s.removeConstraint(id).value();

    REQUIRE(s.insertConstraint(removed).has_value());
    CHECK(*s.findConstraint(id) == removed);
    CHECK(errorCode(s.insertConstraint(removed)) == ErrorCode::AlreadyExists);

    Constraint dangling = removed;
    dangling.id = ConstraintId::fromValue(50);
    dangling.entities = {EntityId::fromValue(4242)};
    CHECK(errorCode(s.insertConstraint(dangling)) == ErrorCode::NotFound);

    Constraint noId = removed;
    noId.id = ConstraintId{};
    CHECK(errorCode(s.insertConstraint(noId)) == ErrorCode::InvalidArgument);

    Constraint loaded = removed;
    loaded.id = ConstraintId::fromValue(10);
    REQUIRE(s.insertConstraint(loaded).has_value());
    CHECK(require(s.addHorizontal(f.top)).value() == 11);
}

TEST_CASE("Constraints are part of the sketch content", "[sketch][constraints]") {
    Fixture a;
    Fixture b;
    CHECK(a.sketch.contentEquals(b.sketch));

    require(a.sketch.addHorizontal(a.bottom));
    CHECK_FALSE(a.sketch.contentEquals(b.sketch));
    require(b.sketch.addHorizontal(b.bottom));
    CHECK(a.sketch.contentEquals(b.sketch));

    const auto copy = a.sketch.clone();
    CHECK(a.sketch.contentEquals(*copy));
    REQUIRE(a.sketch.setConstraintEnabled(ConstraintId::fromValue(1), false).value());
    CHECK_FALSE(a.sketch.contentEquals(*copy));
}
