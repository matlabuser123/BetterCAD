#include <bettercad/core/Units.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <limits>
#include <numbers>
#include <type_traits>

using namespace bettercad;
using namespace bettercad::literals;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using Catch::Matchers::WithinULP;

namespace {

// ---------------------------------------------------------------------------
// Compile-time dimensional analysis. Every static_assert below is checked when
// this file compiles, so a regression breaks the build of the test suite.
// Build-failure tests in tests/compile_fail/ additionally show the compiler
// diagnostic for direct misuse.
// ---------------------------------------------------------------------------
template <typename A, typename B>
concept Addable = requires(A a, B b) { a + b; };
template <typename A, typename B>
concept Subtractable = requires(A a, B b) { a - b; };
template <typename A, typename B>
concept Comparable = requires(A a, B b) {
    a < b;
    a == b;
};
template <typename A, typename B>
concept CompoundAddable = requires(A a, B b) { a += b; };
template <typename Q>
concept HasSqrt = requires(Q q) { bettercad::sqrt(q); };

// Like dimensions combine.
static_assert(Addable<Length, Length>);
static_assert(Subtractable<Pressure, Pressure>);
static_assert(Comparable<Angle, Angle>);
static_assert(CompoundAddable<Volume, Volume>);

// Mixed dimensions are rejected.
static_assert(!Addable<Length, Angle>);
static_assert(!Addable<Length, Area>);
static_assert(!Addable<Force, Pressure>);
static_assert(!Subtractable<Mass, Time>);
static_assert(!Comparable<Length, Time>);
static_assert(!CompoundAddable<Length, Volume>);

// No implicit mixing with raw numbers, in either direction.
static_assert(!Addable<Length, double>);
static_assert(!Addable<double, Angle>);
static_assert(!std::is_convertible_v<double, Length>);
static_assert(!std::is_constructible_v<Length, double>);
static_assert(!std::is_convertible_v<Length, double>);
static_assert(!std::is_constructible_v<double, Length>);

// No conversion between dimensions.
static_assert(!std::is_convertible_v<Length, Angle>);
static_assert(!std::is_constructible_v<Length, Mass>);
static_assert(!std::is_convertible_v<Quantity<dimensions::force / dimensions::length>, Pressure>);

// Angle is a dimension of its own, not a plain ratio.
static_assert(!dimensions::angle.isDimensionless());
static_assert(!std::is_convertible_v<Angle, double>);

// Multiplication and division derive dimensions.
static_assert(std::is_same_v<decltype(Length{} * Length{}), Area>);
static_assert(std::is_same_v<decltype(Area{} * Length{}), Volume>);
static_assert(std::is_same_v<decltype(Volume{} / Area{}), Length>);
static_assert(std::is_same_v<decltype(Length{} / Time{}), Velocity>);
static_assert(std::is_same_v<decltype(Velocity{} / Time{}), Acceleration>);
static_assert(std::is_same_v<decltype(Mass{} * Acceleration{}), Force>);
static_assert(std::is_same_v<decltype(Force{} / Area{}), Pressure>);
static_assert(std::is_same_v<decltype(Mass{} / Volume{}), Density>);
static_assert(std::is_same_v<decltype(1.0 / Time{}), Quantity<Dimension{.time = -1}>>);
static_assert(std::is_same_v<decltype(Length{} * Angle{}), Quantity<Dimension{.length = 1, .angle = 1}>>);
// Fully cancelled dimensions yield a plain number.
static_assert(std::is_same_v<decltype(Length{} / Length{}), double>);
static_assert(std::is_same_v<decltype(Pressure{} * Area{} / Force{}), double>);

// Square roots exist only for even exponents.
static_assert(HasSqrt<Area>);
static_assert(!HasSqrt<Volume>);
static_assert(!HasSqrt<Length>);
static_assert(std::is_same_v<decltype(bettercad::sqrt(Area{})), Length>);

static_assert(QuantityType<Length> && QuantityType<const Pressure&>);
static_assert(!QuantityType<double>);

// Arithmetic is usable in constant expressions.
static_assert((100_mm).si() == 0.1);
static_assert(250_mm + 750_mm == 1_m);
static_assert(1_m == 1000_mm);
static_assert(2.5_MPa == 2'500'000_Pa);
static_assert(-(3_s) < 0_s);

} // namespace

TEST_CASE("Default-constructed and zero quantities are zero", "[units]") {
    CHECK(Length{}.si() == 0.0);
    CHECK(Pressure::zero().si() == 0.0);
    CHECK(Length{} == 0_mm);
}

TEST_CASE("Like quantities add and subtract", "[units]") {
    const Length a = 100_mm;
    const Length b = 50_mm;

    CHECK_THAT((a + b).in(units::mm), WithinULP(150.0, 1));
    CHECK_THAT((a - b).in(units::mm), WithinULP(50.0, 1));
    CHECK_THAT((b - a).in(units::mm), WithinULP(-50.0, 1));
    CHECK((-a).si() == -a.si());
    CHECK((+a) == a);
}

TEST_CASE("Quantities scale by plain numbers", "[units]") {
    const Force f = 10_N;

    CHECK((f * 2.5).in(units::N) == 25.0);
    CHECK((2.5 * f).in(units::N) == 25.0);
    CHECK((f / 4.0).in(units::N) == 2.5);
    CHECK((f * 2).in(units::N) == 20.0);
}

TEST_CASE("Compound assignment updates the quantity", "[units]") {
    Length l = 10_mm;
    l += 5_mm;
    CHECK_THAT(l.in(units::mm), WithinULP(15.0, 1));
    l -= 3_mm;
    CHECK_THAT(l.in(units::mm), WithinULP(12.0, 1));
    l *= 2.0;
    CHECK_THAT(l.in(units::mm), WithinULP(24.0, 1));
    l /= 4.0;
    CHECK_THAT(l.in(units::mm), WithinULP(6.0, 1));
}

TEST_CASE("Like quantities compare by value regardless of the unit used", "[units]") {
    CHECK(1_m == 1000_mm);
    CHECK(1_mm < 1_cm);
    CHECK(1_km > 999_m);
    CHECK(1_MPa >= 1000_kPa);
    CHECK(1_deg != 1_rad);
}

TEST_CASE("Products and quotients derive the correct quantity", "[units]") {
    SECTION("area and volume") {
        const Area area = 100_mm * 50_mm;
        const Volume volume = 100_mm * 50_mm * 20_mm;
        CHECK_THAT(area.in(units::mm2), WithinRel(5000.0, 1e-15));
        CHECK_THAT(volume.in(units::mm3), WithinRel(100000.0, 1e-15));
        CHECK_THAT((volume / area).in(units::mm), WithinRel(20.0, 1e-15));
    }
    SECTION("velocity and acceleration") {
        const Velocity v = 100_m / 20_s;
        CHECK(v.in(units::m_per_s) == 5.0);
        CHECK_THAT((36_km / 1_h).in(units::m_per_s), WithinULP(10.0, 1));
        const Acceleration a = v / 2_s;
        CHECK(a.in(units::m_per_s2) == 2.5);
    }
    SECTION("force, pressure and density") {
        const Force weight = 2_kg * 9.81_m_per_s2;
        CHECK_THAT(weight.in(units::N), WithinULP(19.62, 1));

        const Pressure stress = 1_kN / 100_mm2;
        CHECK_THAT(stress.in(units::MPa), WithinRel(10.0, 1e-15));

        const Density steel = 7850_kg / 1_m3;
        CHECK(steel.in(units::kg_per_m3) == 7850.0);
        CHECK(steel.in(units::g_per_cm3) == 7.85);
    }
    SECTION("ratios of like quantities are plain numbers") {
        const double ratio = 50_mm / 100_mm;
        CHECK_THAT(ratio, WithinULP(0.5, 1));
    }
    SECTION("reciprocals") {
        const auto frequency = 1.0 / 20_ms;
        CHECK_THAT(frequency.si(), WithinULP(50.0, 1));
    }
}

TEST_CASE("Square root of an area is a length", "[units]") {
    const Length side = bettercad::sqrt(2500_mm2);
    CHECK_THAT(side.in(units::mm), WithinULP(50.0, 1));
}

TEST_CASE("abs, isFinite and approxEqual", "[units]") {
    CHECK(bettercad::abs(-(5_mm)) == 5_mm);
    CHECK(bettercad::abs(5_mm) == 5_mm);

    CHECK(isFinite(1_m));
    CHECK_FALSE(isFinite(Length::fromSi(std::numeric_limits<double>::infinity())));
    CHECK_FALSE(isFinite(Length::fromSi(std::numeric_limits<double>::quiet_NaN())));

    CHECK(approxEqual(100_mm, 100.0000001_mm, 1_um));
    CHECK_FALSE(approxEqual(100_mm, 100.01_mm, 1_um));
}

TEST_CASE("Trigonometric functions take and return angles", "[units]") {
    CHECK_THAT(sin(30_deg), WithinAbs(0.5, 1e-15));
    CHECK_THAT(cos(60_deg), WithinAbs(0.5, 1e-15));
    CHECK_THAT(tan(45_deg), WithinAbs(1.0, 1e-15));
    CHECK_THAT(arcsin(0.5).in(units::deg), WithinAbs(30.0, 1e-12));
    CHECK_THAT(arccos(0.0).in(units::rad), WithinULP(std::numbers::pi / 2.0, 1));
    CHECK_THAT(arctan(1.0).in(units::deg), WithinAbs(45.0, 1e-12));
    // The C library functions stay usable on plain doubles alongside these.
    CHECK_THAT(std::asin(0.5), WithinULP(std::numbers::pi / 6.0, 1));
    CHECK_THAT(atan2(1_mm, 1_mm).in(units::deg), WithinAbs(45.0, 1e-12));
    CHECK_THAT(atan2(-1_mm, -1_mm).in(units::deg), WithinAbs(-135.0, 1e-12));
}
