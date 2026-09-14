#include <bettercad/core/Uuid.hpp>

#include <catch2/catch_test_macros.hpp>

#include <format>
#include <random>
#include <set>
#include <string>

using bettercad::Uuid;

TEST_CASE("The default UUID is nil", "[uuid]") {
    const Uuid nil;
    CHECK(nil.isNil());
    CHECK(nil.toString() == "00000000-0000-0000-0000-000000000000");
}

TEST_CASE("Generated UUIDs are version 4, RFC variant and distinct", "[uuid]") {
    std::set<Uuid> seen;
    for (int i = 0; i < 1000; ++i) {
        const Uuid uuid = Uuid::generateV4();
        CHECK_FALSE(uuid.isNil());
        CHECK(uuid.version() == 4);
        CHECK(uuid.isRfcVariant());
        seen.insert(uuid);
    }
    CHECK(seen.size() == 1000);
}

TEST_CASE("Seeded generation is reproducible", "[uuid]") {
    std::mt19937_64 first(12345);
    std::mt19937_64 second(12345);
    const Uuid a = Uuid::generateV4(first);
    const Uuid b = Uuid::generateV4(second);

    CHECK(a == b);
    CHECK(a.version() == 4);
    CHECK(a.isRfcVariant());
    CHECK(Uuid::generateV4(first) != a);

    // 32-bit engines work too.
    std::mt19937 narrow(7);
    CHECK(Uuid::generateV4(narrow).version() == 4);
}

TEST_CASE("UUIDs round-trip through their canonical text form", "[uuid]") {
    for (int i = 0; i < 100; ++i) {
        const Uuid uuid = Uuid::generateV4();
        const std::string text = uuid.toString();
        CAPTURE(text);
        REQUIRE(text.size() == 36);
        const auto parsed = Uuid::parse(text);
        REQUIRE(parsed.has_value());
        CHECK(*parsed == uuid);
    }
}

TEST_CASE("UUID parsing accepts either case and formats lowercase", "[uuid]") {
    const auto upper = Uuid::parse("123E4567-E89B-42D3-A456-426614174000");
    REQUIRE(upper.has_value());
    CHECK(upper->toString() == "123e4567-e89b-42d3-a456-426614174000");
    CHECK(upper->bytes()[0] == 0x12);
    CHECK(upper->bytes()[15] == 0x00);
    CHECK(upper->version() == 4);
    CHECK(std::format("{}", *upper) == "123e4567-e89b-42d3-a456-426614174000");
}

TEST_CASE("Malformed UUID text is rejected", "[uuid]") {
    CHECK_FALSE(Uuid::parse("").has_value());
    CHECK_FALSE(Uuid::parse("123e4567-e89b-42d3-a456-42661417400").has_value());   // short
    CHECK_FALSE(Uuid::parse("123e4567-e89b-42d3-a456-4266141740000").has_value()); // long
    CHECK_FALSE(Uuid::parse("123e4567e89b-42d3-a456-4266141740000").has_value());  // dash moved
    CHECK_FALSE(Uuid::parse("123e4567-e89b-42d3-a456_426614174000").has_value());  // wrong separator
    CHECK_FALSE(Uuid::parse("g23e4567-e89b-42d3-a456-426614174000").has_value());  // not hex
    CHECK_FALSE(Uuid::parse("{23e4567-e89b-42d3-a456-426614174000").has_value());
}
