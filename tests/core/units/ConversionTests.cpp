#include <bettercad/core/Units.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <numbers>

using namespace bettercad;
using namespace bettercad::literals;
using Catch::Matchers::WithinULP;

TEST_CASE("Spec usage: literals construct typed quantities", "[units][conversion]") {
    const Length width = 100_mm;
    const Length height = 50_mm;
    const Angle angle = 45_deg;
    const Pressure pressure = 2.5_MPa;

    CHECK(width.in(units::mm) == 100.0);
    CHECK(height.in(units::mm) == 50.0);
    CHECK_THAT(angle.in(units::deg), WithinULP(45.0, 1));
    CHECK(pressure.in(units::MPa) == 2.5);
}

TEST_CASE("Millimetres and metres convert to and from SI", "[units][conversion]") {
    // A single conversion is one correctly rounded operation, so these are exact.
    CHECK((100_mm).si() == 0.1);
    CHECK((1_mm).si() == 0.001);
    CHECK((1_m).si() == 1.0);
    CHECK(Length::fromSi(0.1).in(units::mm) == 100.0);
    CHECK(Length::fromSi(1.0).in(units::mm) == 1000.0);
    CHECK((2.5_m).in(units::mm) == 2500.0);
    CHECK((1_km).in(units::m) == 1000.0);
    CHECK((1_m).in(units::km) == 0.001);

    SECTION("round trip mm -> m -> mm") {
        const double value = GENERATE(0.001, 0.1, 1.0, 12.5, 100.0, 999.999, 123456.789);
        CAPTURE(value);
        const Length length = value * units::mm;
        CHECK_THAT(length.si(), WithinULP(value / 1000.0, 0));
        CHECK_THAT(length.in(units::mm), WithinULP(value, 1));
    }
}

TEST_CASE("Imperial lengths use their exact definitions", "[units][conversion]") {
    CHECK_THAT((1_in).in(units::mm), WithinULP(25.4, 1));
    CHECK_THAT((1_ft).in(units::inch), WithinULP(12.0, 1));
    CHECK_THAT((1_ft).si(), WithinULP(0.3048, 0));
}

TEST_CASE("Degrees and radians convert to and from SI", "[units][conversion]") {
    CHECK_THAT((180_deg).si(), WithinULP(std::numbers::pi, 1));
    CHECK_THAT((90_deg).in(units::rad), WithinULP(std::numbers::pi / 2.0, 1));
    CHECK_THAT((360_deg).si(), WithinULP(2.0 * std::numbers::pi, 1));
    CHECK_THAT(Angle::fromSi(std::numbers::pi).in(units::deg), WithinULP(180.0, 1));
    CHECK_THAT((1_rad).in(units::deg), WithinULP(180.0 / std::numbers::pi, 1));
    CHECK((0_deg).si() == 0.0);

    SECTION("round trip deg -> rad -> deg") {
        const double value = GENERATE(-720.0, -45.0, 0.5, 30.0, 45.0, 90.0, 359.999);
        CAPTURE(value);
        CHECK_THAT((value * units::deg).in(units::deg), WithinULP(value, 2));
    }
}

TEST_CASE("Megapascals and pascals convert to and from SI", "[units][conversion]") {
    CHECK((2.5_MPa).si() == 2.5e6);
    CHECK((1_Pa).si() == 1.0);
    CHECK(Pressure::fromSi(2.5e6).in(units::MPa) == 2.5);
    CHECK((1_GPa).in(units::MPa) == 1000.0);
    CHECK((210_GPa).in(units::Pa) == 210e9);
    CHECK((1_bar).in(units::kPa) == 100.0);
    // Floating-point literals arrive as long double; narrowing to double may
    // round twice, so non-representable decimals are compared within 1 ULP.
    CHECK_THAT((101.325_kPa).in(units::Pa), WithinULP(101325.0, 1));
}

TEST_CASE("Other engineering units convert to SI", "[units][conversion]") {
    CHECK((1500_g).in(units::kg) == 1.5);
    CHECK((2_t).in(units::kg) == 2000.0);
    CHECK((2_min).in(units::s) == 120.0);
    CHECK((1_h).in(units::minute) == 60.0);
    CHECK((250_ms).in(units::s) == 0.25);
    CHECK_THAT((293.15_K).si(), WithinULP(293.15, 1));
    CHECK_THAT((36_km_per_h).in(units::m_per_s), WithinULP(10.0, 1));
    CHECK((1500_mm_per_s).in(units::m_per_s) == 1.5);
    CHECK((1_g_per_cm3).in(units::kg_per_m3) == 1000.0);
    CHECK((1_L).in(units::cm3) == 1000.0);
    CHECK((1_m3).in(units::L) == 1000.0);
    CHECK((1_cm2).in(units::mm2) == 100.0);
    CHECK((2_kN).in(units::N) == 2000.0);
}

TEST_CASE("Integer and floating-point literals agree", "[units][conversion]") {
    CHECK(100_mm == 100.0_mm);
    CHECK(45_deg == 45.0_deg);
    CHECK(3_MPa == 3.0_MPa);
    CHECK(7_kg == 7.0_kg);
}
