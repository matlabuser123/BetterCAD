#include <bettercad/core/Units.hpp>
#include <bettercad/core/parameters/Parameter.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <limits>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinULP;

namespace {

Parameter makeWidth() {
    auto width = Parameter::create(ParameterId::fromValue(1), "width", 100_mm, units::mm);
    REQUIRE(width.has_value());
    return *width;
}

} // namespace

// --- Creation ---------------------------------------------------------------

TEST_CASE("Parameters are created with ID, name, value, unit and revision 1", "[parameters]") {
    const Parameter width = makeWidth();

    CHECK(width.id() == ParameterId::fromValue(1));
    CHECK(width.name() == "width");
    CHECK(width.dimension() == dimensions::length);
    CHECK(width.siValue() == 0.1);
    CHECK(width.displayUnit().symbol == "mm");
    CHECK(width.displayValue() == 100.0);
    CHECK_FALSE(width.expression().has_value());
    CHECK(width.revision() == 1);
    CHECK(width.as<Length>().value() == 100_mm);
}

TEST_CASE("Spec examples: width, height and hole diameter", "[parameters]") {
    IdAllocator ids;
    const auto width = Parameter::create(ids.allocate<ParameterId>(), "width", 100_mm, units::mm);
    const auto height = Parameter::create(ids.allocate<ParameterId>(), "height", 50_mm, units::mm);
    const auto hole =
        Parameter::create(ids.allocate<ParameterId>(), "hole_diameter", 10_mm, units::mm);

    REQUIRE(width.has_value());
    REQUIRE(height.has_value());
    REQUIRE(hole.has_value());
    CHECK(width->displayValue() == 100.0);
    CHECK(height->displayValue() == 50.0);
    CHECK(hole->displayValue() == 10.0);
    CHECK(hole->id().value() == 3);
}

TEST_CASE("Parameters of other dimensions keep their dimension and unit", "[parameters]") {
    const auto angle = Parameter::create(ParameterId::fromValue(2), "draft", 3_deg, units::deg);
    const auto pressure =
        Parameter::create(ParameterId::fromValue(3), "p_max", 2.5_MPa, units::MPa);
    const auto ratio = Parameter::createUnitless(ParameterId::fromValue(4), "ratio", 0.75);

    REQUIRE(angle.has_value());
    REQUIRE(pressure.has_value());
    REQUIRE(ratio.has_value());
    CHECK(angle->dimension() == dimensions::angle);
    CHECK_THAT(angle->displayValue(), WithinULP(3.0, 1));
    CHECK(pressure->dimension() == dimensions::pressure);
    CHECK(pressure->siValue() == 2.5e6);
    CHECK(pressure->displayValue() == 2.5);
    CHECK(ratio->dimension() == dimensions::dimensionless);
    CHECK(ratio->displayUnit() == kUnitless);
    CHECK(ratio->displayValue() == 0.75);
}

TEST_CASE("Parameters can be created from a number and a runtime unit", "[parameters]") {
    const auto unit = findUnit("in");
    REQUIRE(unit.has_value());
    const auto p = Parameter::create(ParameterId::fromValue(1), "stock", unit->scale.toSi(2.0), *unit);

    REQUIRE(p.has_value());
    CHECK(p->displayUnit().symbol == "in");
    CHECK_THAT(p->as<Length>()->in(units::mm), WithinULP(50.8, 1));
}

TEST_CASE("Parameter names must be identifiers", "[parameters]") {
    const std::vector<std::string> validNames{"width", "hole_diameter", "_x", "D1", "a",
                                              std::string(64, 'n')};
    for (const std::string& valid : validNames) {
        CAPTURE(valid);
        CHECK(validateParameterName(valid).has_value());
    }
    const std::vector<std::string> invalidNames{"",    "1abc",       "hole diameter",     "width!",
                                                "w-2", "caf\xC3\xA9", std::string(65, 'n')};
    for (const std::string& invalid : invalidNames) {
        CAPTURE(invalid);
        const auto result = validateParameterName(invalid);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::InvalidArgument);
    }
}

TEST_CASE("Invalid parameters are rejected at creation", "[parameters]") {
    SECTION("invalid ID") {
        const auto p = Parameter::create(ParameterId{}, "width", 100_mm, units::mm);
        REQUIRE_FALSE(p.has_value());
        CHECK(p.error().code == ErrorCode::InvalidArgument);
    }
    SECTION("invalid name") {
        const auto p = Parameter::create(ParameterId::fromValue(1), "2wide", 100_mm, units::mm);
        REQUIRE_FALSE(p.has_value());
        CHECK(p.error().code == ErrorCode::InvalidArgument);
    }
    SECTION("non-finite value") {
        const auto p = Parameter::create(ParameterId::fromValue(1), "width",
                                         std::numeric_limits<double>::infinity(), describe(units::m));
        REQUIRE_FALSE(p.has_value());
        CHECK(p.error().code == ErrorCode::InvalidArgument);
    }
    SECTION("unit that is not in the catalog") {
        const UnitDescriptor fake{"mm", dimensions::length, {1.0, 10.0}};
        const auto p = Parameter::create(ParameterId::fromValue(1), "width", 0.1, fake);
        REQUIRE_FALSE(p.has_value());
        CHECK(p.error().code == ErrorCode::InvalidArgument);
    }
}

// --- Modification -----------------------------------------------------------

TEST_CASE("Changing the value updates it and increments the revision", "[parameters]") {
    Parameter width = makeWidth();

    const auto changed = width.setValue(40_mm);
    REQUIRE(changed.has_value());
    CHECK(*changed);
    CHECK(width.displayValue() == 40.0);
    CHECK(width.revision() == 2);

    // Entering a value in another unit keeps the display unit.
    const auto inch = findUnit("in");
    REQUIRE(inch.has_value());
    REQUIRE(width.setValue(2.0, *inch).has_value());
    CHECK_THAT(width.displayValue(), WithinULP(50.8, 1));
    CHECK(width.displayUnit().symbol == "mm");
    CHECK(width.revision() == 3);
}

TEST_CASE("Setting an identical value is not a change", "[parameters]") {
    Parameter width = makeWidth();

    const auto changed = width.setValue(100_mm);
    REQUIRE(changed.has_value());
    CHECK_FALSE(*changed);
    CHECK(width.revision() == 1);
}

TEST_CASE("The display unit can change without changing the value", "[parameters]") {
    Parameter width = makeWidth();

    REQUIRE(width.setDisplayUnit(describe(units::cm)).value());
    CHECK(width.displayUnit().symbol == "cm");
    CHECK(width.displayValue() == 10.0);
    CHECK(width.siValue() == 0.1);
    CHECK(width.revision() == 2);
    CHECK_FALSE(width.setDisplayUnit(describe(units::cm)).value());
    CHECK(width.revision() == 2);
}

TEST_CASE("Expressions are stored and cleared", "[parameters]") {
    Parameter width = makeWidth();

    REQUIRE(width.setExpression("height * 2").value());
    CHECK(width.expression() == "height * 2");
    CHECK(width.revision() == 2);
    CHECK_FALSE(width.setExpression("height * 2").value());

    REQUIRE(width.setExpression(std::nullopt).value());
    CHECK_FALSE(width.expression().has_value());
    CHECK(width.revision() == 3);

    const auto empty = width.setExpression(std::string{});
    REQUIRE_FALSE(empty.has_value());
    CHECK(empty.error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("Renaming validates the new name", "[parameters]") {
    Parameter width = makeWidth();

    REQUIRE(width.rename("plate_width").value());
    CHECK(width.name() == "plate_width");
    const auto bad = width.rename("plate width");
    REQUIRE_FALSE(bad.has_value());
    CHECK(width.name() == "plate_width");
}

// --- Dimension safety -------------------------------------------------------

TEST_CASE("Assigning a value of another dimension fails and changes nothing", "[parameters]") {
    Parameter width = makeWidth();

    const auto angle = width.setValue(45_deg);
    REQUIRE_FALSE(angle.has_value());
    CHECK(angle.error().code == ErrorCode::DimensionMismatch);
    CHECK_THAT(angle.error().message, ContainsSubstring("'width' has dimension length, not angle"));

    const auto kg = findUnit("kg");
    REQUIRE(kg.has_value());
    const auto mass = width.setValue(5.0, *kg);
    REQUIRE_FALSE(mass.has_value());
    CHECK(mass.error().code == ErrorCode::DimensionMismatch);

    const auto unit = width.setDisplayUnit(describe(units::deg));
    REQUIRE_FALSE(unit.has_value());
    CHECK(unit.error().code == ErrorCode::DimensionMismatch);

    CHECK(width.siValue() == 0.1);
    CHECK(width.displayUnit().symbol == "mm");
    CHECK(width.revision() == 1);
}

TEST_CASE("Reading a parameter as the wrong quantity type fails", "[parameters]") {
    const Parameter width = makeWidth();

    CHECK(width.as<Length>().has_value());
    const auto angle = width.as<Angle>();
    REQUIRE_FALSE(angle.has_value());
    CHECK(angle.error().code == ErrorCode::DimensionMismatch);
    CHECK_FALSE(width.as<Area>().has_value());
}

TEST_CASE("Non-finite values are rejected", "[parameters]") {
    Parameter width = makeWidth();

    const auto nan = width.setValue(Length::fromSi(std::numeric_limits<double>::quiet_NaN()));
    REQUIRE_FALSE(nan.has_value());
    CHECK(nan.error().code == ErrorCode::InvalidArgument);
    // A finite input that overflows during unit conversion is rejected too.
    const auto huge = width.setValue(std::numeric_limits<double>::max(), describe(units::km));
    REQUIRE_FALSE(huge.has_value());
    CHECK(width.siValue() == 0.1);
}

TEST_CASE("Equivalence ignores revision counters", "[parameters]") {
    Parameter a = makeWidth();
    Parameter b = makeWidth();
    REQUIRE(b.setValue(40_mm).value());
    REQUIRE(b.setValue(100_mm).value());

    CHECK(b.revision() == 3);
    CHECK(equivalent(a, b));
    REQUIRE(b.setValue(101_mm).value());
    CHECK_FALSE(equivalent(a, b));
}

TEST_CASE("Units resolve by symbol, the empty symbol meaning unitless", "[parameters]") {
    CHECK(resolveUnit("mm").value() == describe(units::mm));
    CHECK(resolveUnit("").value() == kUnitless);
    const auto unknown = resolveUnit("furlong");
    REQUIRE_FALSE(unknown.has_value());
    CHECK(unknown.error().code == ErrorCode::InvalidArgument);
}
