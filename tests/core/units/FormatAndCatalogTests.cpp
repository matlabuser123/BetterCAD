#include <bettercad/core/Units.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <format>
#include <set>
#include <sstream>
#include <string>
#include <string_view>

using namespace bettercad;
using namespace bettercad::literals;
using Catch::Matchers::WithinULP;

TEST_CASE("SI unit symbols are derived from dimensions", "[units][format]") {
    CHECK(siUnitSymbol(dimensions::dimensionless).empty());
    CHECK(siUnitSymbol(dimensions::length) == "m");
    CHECK(siUnitSymbol(dimensions::area) == "m^2");
    CHECK(siUnitSymbol(dimensions::volume) == "m^3");
    CHECK(siUnitSymbol(dimensions::angle) == "rad");
    CHECK(siUnitSymbol(dimensions::mass) == "kg");
    CHECK(siUnitSymbol(dimensions::time) == "s");
    CHECK(siUnitSymbol(dimensions::temperature) == "K");
    CHECK(siUnitSymbol(dimensions::velocity) == "m/s");
    CHECK(siUnitSymbol(dimensions::acceleration) == "m/s^2");
    CHECK(siUnitSymbol(dimensions::force) == "N");
    CHECK(siUnitSymbol(dimensions::pressure) == "Pa");
    CHECK(siUnitSymbol(dimensions::density) == "kg/m^3");
    CHECK(siUnitSymbol(dimensions::angle / dimensions::time) == "rad/s");
    CHECK(siUnitSymbol(dimensions::time.inverse()) == "1/s");
    CHECK(siUnitSymbol(dimensions::force / dimensions::length) == "kg/s^2");
    CHECK(siUnitSymbol(Dimension{.length = 1, .mass = -1, .time = -1}) == "m/(kg*s)");
}

TEST_CASE("Quantities format with their unit", "[units][format]") {
    CHECK(toString(100_mm, units::mm) == "100 mm");
    CHECK(toString(2.5_MPa, units::MPa) == "2.5 MPa");
    CHECK(toString(2.5_MPa) == "2500000 Pa");
    CHECK(toString(100_mm) == "0.1 m");
    CHECK(std::format("{}", 9.81_m_per_s2) == "9.81 m/s^2");
    CHECK(std::format("{:.3f}", 100_mm) == "0.100 m");
    CHECK(std::format("{:>8.1f}|", 5_kg) == "     5.0 kg|");

    std::ostringstream os;
    os << 2_kN;
    CHECK(os.str() == "2000 N");
}

TEST_CASE("Unit catalog lists every unit with a unique symbol", "[units][catalog]") {
    const auto catalog = unitCatalog();
    CHECK(catalog.size() == 37);

    std::set<std::string_view> symbols;
    for (const UnitDescriptor& unit : catalog) {
        CAPTURE(unit.symbol);
        CHECK(symbols.insert(unit.symbol).second);
        CHECK(unit.scale.numerator > 0.0);
        CHECK(unit.scale.denominator > 0.0);
        CHECK_THAT(unit.scale.fromSi(unit.scale.toSi(1.0)), WithinULP(1.0, 1));
    }
}

TEST_CASE("Units can be looked up by symbol at run time", "[units][catalog]") {
    const auto mm = findUnit("mm");
    REQUIRE(mm.has_value());
    CHECK(mm->dimension == dimensions::length);
    CHECK(mm->scale.toSi(100.0) == 0.1);
    CHECK(*mm == describe(units::mm));

    const auto mpa = findUnit("MPa");
    REQUIRE(mpa.has_value());
    CHECK(mpa->dimension == dimensions::pressure);
    CHECK(mpa->scale.toSi(2.5) == 2.5e6);

    const auto deg = findUnit("deg");
    REQUIRE(deg.has_value());
    CHECK(deg->dimension == dimensions::angle);
    CHECK(deg->scale.toSi(180.0) == (180_deg).si());

    CHECK(findUnit("kg/m^3")->dimension == dimensions::density);
    CHECK(findUnit("in")->dimension == dimensions::length);
}

TEST_CASE("Unknown unit symbols are not found", "[units][catalog]") {
    CHECK_FALSE(findUnit("").has_value());
    CHECK_FALSE(findUnit("furlong").has_value());
    CHECK_FALSE(findUnit("MM").has_value());  // symbols are case-sensitive
    CHECK_FALSE(findUnit("mpa").has_value());
    CHECK_FALSE(findUnit(" mm").has_value());
}
