#include <bettercad/core/Units.hpp>
#include <bettercad/core/parameters/ParameterTable.hpp>
#include <bettercad/io/ParameterJson.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <bit>
#include <cstdint>
#include <limits>
#include <string>

using namespace bettercad;
using namespace bettercad::literals;
using Catch::Matchers::ContainsSubstring;

namespace {

ParameterTable makeTable() {
    IdAllocator ids;
    ParameterTable table;
    auto add = [&](Result<Parameter> parameter) {
        REQUIRE(parameter.has_value());
        REQUIRE(table.add(std::move(*parameter)).has_value());
    };
    add(Parameter::create(ids.allocate<ParameterId>(), "width", 100_mm, units::mm));
    add(Parameter::create(ids.allocate<ParameterId>(), "height", 50_mm, units::mm));
    add(Parameter::create(ids.allocate<ParameterId>(), "hole_diameter", 10_mm, units::mm));
    add(Parameter::create(ids.allocate<ParameterId>(), "draft", 1.5_deg, units::deg));
    add(Parameter::create(ids.allocate<ParameterId>(), "p_max", 2.5_MPa, units::MPa));
    add(Parameter::createUnitless(ids.allocate<ParameterId>(), "ratio", 1.0 / 3.0));
    [[maybe_unused]] const auto deleted = ids.allocate<ParameterId>(); // ID gap, as after a deletion
    add(Parameter::create(ids.allocate<ParameterId>(), "stock", 2_in, units::inch));

    REQUIRE(table.setExpression(ParameterId::fromValue(2), "width / 2").has_value());
    REQUIRE(table.setValue(ParameterId::fromValue(1), 120_mm).has_value());
    return table;
}

std::string replaceOnce(std::string text, std::string_view from, std::string_view to) {
    const auto pos = text.find(from);
    REQUIRE(pos != std::string::npos);
    text.replace(pos, from.size(), to);
    return text;
}

std::string toJson(const ParameterTable& table) {
    auto json = io::parametersToJson(table);
    REQUIRE(json.has_value());
    return *json;
}

ErrorCode loadError(std::string_view json) {
    const auto table = io::parametersFromJson(json);
    REQUIRE_FALSE(table.has_value());
    UNSCOPED_INFO(table.error().message);
    return table.error().code;
}

} // namespace

TEST_CASE("Parameter tables round-trip through JSON", "[io][parameters]") {
    const ParameterTable original = makeTable();

    const std::string json = toJson(original);
    const auto loaded = io::parametersFromJson(json);
    REQUIRE(loaded.has_value());

    CHECK(equivalent(original, *loaded));
    CHECK(loaded->size() == original.size());
    // Writing the loaded table reproduces the file byte for byte.
    CHECK(toJson(*loaded) == json);
}

TEST_CASE("Round trip preserves IDs, names, dimensions, units and expressions", "[io][parameters]") {
    const auto loaded = io::parametersFromJson(toJson(makeTable()));
    REQUIRE(loaded.has_value());

    const Parameter* width = loaded->findByName("width");
    REQUIRE(width != nullptr);
    CHECK(width->id() == ParameterId::fromValue(1));
    CHECK(width->dimension() == dimensions::length);
    CHECK(width->displayUnit() == describe(units::mm));
    CHECK(width->displayValue() == 120.0);

    const Parameter* height = loaded->find(ParameterId::fromValue(2));
    REQUIRE(height != nullptr);
    CHECK(height->expression() == "width / 2");

    const Parameter* draft = loaded->findByName("draft");
    REQUIRE(draft != nullptr);
    CHECK(draft->dimension() == dimensions::angle);
    CHECK(draft->displayUnit().symbol == "deg");

    CHECK(loaded->findByName("p_max")->dimension() == dimensions::pressure);
    CHECK(loaded->findByName("ratio")->displayUnit() == kUnitless);
    CHECK(loaded->findByName("stock")->id() == ParameterId::fromValue(8));
    CHECK(loaded->highestIdValue() == 8);
}

TEST_CASE("SI values round-trip bit for bit", "[io][parameters]") {
    const double value = GENERATE(0.1, 0.1 + 0.2, 1.0 / 3.0, -0.0, 1e-300, 5e-324, 1.7976931348623157e308,
                                  -123456.789e-12, 0.15000000000000002);
    CAPTURE(value);

    ParameterTable table;
    REQUIRE(table.add(*Parameter::create(ParameterId::fromValue(1), "x", value, describe(units::m)))
                .has_value());
    const auto loaded = io::parametersFromJson(toJson(table));
    REQUIRE(loaded.has_value());
    CHECK(std::bit_cast<std::uint64_t>(loaded->find(ParameterId::fromValue(1))->siValue()) ==
          std::bit_cast<std::uint64_t>(value));
}

TEST_CASE("The JSON format is transparent", "[io][parameters]") {
    ParameterTable table;
    REQUIRE(table.add(*Parameter::create(ParameterId::fromValue(1), "width", 100_mm, units::mm))
                .has_value());

    CHECK(toJson(table) == R"({
  "format": "bettercad-parameters",
  "version": 1,
  "parameters": [
    {
      "id": 1,
      "name": "width",
      "si_value": 0.1,
      "unit": "mm",
      "dimension": {
        "length": 1
      }
    }
  ]
}
)");
}

TEST_CASE("An empty table round-trips", "[io][parameters]") {
    const auto loaded = io::parametersFromJson(toJson(ParameterTable{}));
    REQUIRE(loaded.has_value());
    CHECK(loaded->empty());
}

TEST_CASE("Expressions that are not UTF-8 cannot be saved", "[io][parameters]") {
    ParameterTable table;
    REQUIRE(table.add(*Parameter::create(ParameterId::fromValue(1), "width", 100_mm, units::mm))
                .has_value());
    REQUIRE(table.setExpression(ParameterId::fromValue(1), "caf\xE9").has_value());
    const auto json = io::parametersToJson(table);
    REQUIRE_FALSE(json.has_value());
    CHECK(json.error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("Malformed parameter documents are rejected with a path", "[io][parameters]") {
    ParameterTable table;
    REQUIRE(table.add(*Parameter::create(ParameterId::fromValue(1), "width", 100_mm, units::mm))
                .has_value());
    REQUIRE(table.add(*Parameter::create(ParameterId::fromValue(2), "height", 50_mm, units::mm))
                .has_value());
    const std::string good = toJson(table);
    REQUIRE(io::parametersFromJson(good).has_value());

    SECTION("invalid JSON") {
        CHECK(loadError("{ not json") == ErrorCode::ParseError);
        CHECK(loadError("") == ErrorCode::ParseError);
    }
    SECTION("wrong format or version") {
        CHECK(loadError(replaceOnce(good, "bettercad-parameters", "something-else")) ==
              ErrorCode::ParseError);
        CHECK(loadError(replaceOnce(good, "\"version\": 1", "\"version\": 2")) ==
              ErrorCode::ParseError);
    }
    SECTION("missing, unknown and mistyped fields") {
        CHECK(loadError(replaceOnce(good, "\"name\": \"width\",", "")) == ErrorCode::ParseError);
        CHECK(loadError(replaceOnce(good, "\"id\": 1,", "\"id\": 1, \"colour\": \"red\",")) ==
              ErrorCode::ParseError);
        CHECK(loadError(replaceOnce(good, "\"si_value\": 0.1", "\"si_value\": \"0.1\"")) ==
              ErrorCode::ParseError);
        CHECK(loadError(replaceOnce(good, "\"id\": 1,", "\"id\": -1,")) == ErrorCode::ParseError);
        CHECK(loadError(replaceOnce(good, "\"id\": 1,", "\"id\": 1.5,")) == ErrorCode::ParseError);
    }
    SECTION("unknown unit") {
        CHECK(loadError(replaceOnce(good, "\"unit\": \"mm\"", "\"unit\": \"furlong\"")) ==
              ErrorCode::InvalidArgument);
    }
    SECTION("unit disagrees with the declared dimension") {
        const auto result = io::parametersFromJson(replaceOnce(good, "\"unit\": \"mm\"", "\"unit\": \"kg\""));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::DimensionMismatch);
        CHECK_THAT(result.error().message, ContainsSubstring("parameters[0]"));
    }
    SECTION("invalid name, ID 0, duplicates") {
        CHECK(loadError(replaceOnce(good, "\"width\"", "\"plate width\"")) == ErrorCode::InvalidArgument);
        CHECK(loadError(replaceOnce(good, "\"id\": 1,", "\"id\": 0,")) == ErrorCode::InvalidArgument);
        CHECK(loadError(replaceOnce(good, "\"id\": 2,", "\"id\": 1,")) == ErrorCode::AlreadyExists);
        CHECK(loadError(replaceOnce(good, "\"height\"", "\"width\"")) == ErrorCode::AlreadyExists);
    }
    SECTION("errors name the JSON path") {
        const auto result =
            io::parametersFromJson(replaceOnce(good, "\"si_value\": 0.05", "\"si_value\": null"));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().message == "parameters[1].si_value: expected a number");
    }
}
