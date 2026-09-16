#include "TestHelpers.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/parameters/Expression.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <bit>
#include <cstdint>
#include <format>
#include <map>
#include <numbers>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using bettercad::test::errorCode;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;

// P12-PARAM-001: the unit-aware expression grammar and its evaluation.

namespace {

/// A resolver over a fixed set of named values.
SymbolResolver symbols(std::map<std::string, DimensionedValue, std::less<>> values) {
    return [values = std::move(values)](std::string_view name) -> Result<DimensionedValue> {
        const auto it = values.find(name);
        if (it == values.end()) {
            return makeError(ErrorCode::NotFound, std::format("unknown parameter '{}'", name));
        }
        return it->second;
    };
}

Result<DimensionedValue> evaluate(std::string_view text, const SymbolResolver& resolve = {}) {
    const auto expression = Expression::parse(text);
    if (!expression) {
        return std::unexpected(expression.error());
    }
    return expression->evaluate(resolve);
}

DimensionedValue require(std::string_view text, const SymbolResolver& resolve = {}) {
    CAPTURE(text);
    const auto value = evaluate(text, resolve);
    if (!value) {
        FAIL(value.error().message);
    }
    return *value;
}

Error failure(std::string_view text, const SymbolResolver& resolve = {}) {
    CAPTURE(text);
    const auto value = evaluate(text, resolve);
    REQUIRE_FALSE(value.has_value());
    return value.error();
}

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

const SymbolResolver kPlate = symbols({
    {"width", DimensionedValue::of(100_mm)},
    {"edge_distance", DimensionedValue::of(15_mm)},
    {"draft", DimensionedValue::of(2_deg)},
    {"ratio", DimensionedValue{dimensions::dimensionless, 0.25}},
    {"a", DimensionedValue::of(3_mm)},
    {"s", DimensionedValue{dimensions::dimensionless, 4.0}},
    {"speed", DimensionedValue::of(Velocity::fromSi(0.5))},
    {"s2", DimensionedValue::of(2_s)},
});

} // namespace

TEST_CASE("Expression_LiteralsCarryTheirUnitsInSi", "[expressions][p12]") {
    // Each literal is converted with the unit's exact factor, exactly as the
    // unit API converts it.
    struct Case {
        std::string_view text;
        DimensionedValue expected;
    };
    for (const Case& c : std::vector<Case>{
             {"100 mm", DimensionedValue::of(100.0 * units::mm)},
             {"100mm", DimensionedValue::of(100.0 * units::mm)},
             {"12.5 mm", DimensionedValue::of(12.5 * units::mm)},
             {"2.5e1 mm", DimensionedValue::of(25.0 * units::mm)},
             {".5 in", DimensionedValue::of(0.5 * units::inch)},
             {"3 ft", DimensionedValue::of(3.0 * units::ft)},
             {"90 deg", DimensionedValue::of(90.0 * units::deg)},
             {"1.5 rad", DimensionedValue::of(1.5 * units::rad)},
             {"1 mm^2", DimensionedValue::of(1.0 * units::mm2)},
             {"2 cm^3", DimensionedValue::of(2.0 * units::cm3)},
             {"5 min", DimensionedValue::of(5.0 * units::minute)},
             {"4 h", DimensionedValue::of(4.0 * units::hour)},
             {"3 m/s", DimensionedValue::of(3.0 * units::m_per_s)},
             {"9.81 m/s^2", DimensionedValue::of(9.81 * units::m_per_s2)},
             {"2.5 MPa", DimensionedValue::of(2.5 * units::MPa)},
             {"7.85 g/cm^3", DimensionedValue::of(7.85 * units::g_per_cm3)},
             {"42", DimensionedValue{dimensions::dimensionless, 42.0}},
             {"0.1", DimensionedValue{dimensions::dimensionless, 0.1}},
         }) {
        CAPTURE(c.text);
        const DimensionedValue value = require(c.text);
        CHECK(value.dimension == c.expected.dimension);
        CHECK(bits(value.siValue) == bits(c.expected.siValue));
    }
}

TEST_CASE("Expression_ReferencesAreSortedUniqueNames", "[expressions][p12]") {
    const auto expression = Expression::parse("width - 2 * edge_distance + width / ratio");
    REQUIRE(expression.has_value());
    CHECK(expression->text() == "width - 2 * edge_distance + width / ratio");
    CHECK(expression->references() == std::vector<std::string>{"edge_distance", "ratio", "width"});
    CHECK(expression->offsetOf("width") == 0);
    CHECK(expression->offsetOf("edge_distance") == 12);
    CHECK(expression->offsetOf("ratio") == 36);
    CHECK(expression->offsetOf("missing") == expression->text().size());
    CHECK(Expression::parse("2 * 3 mm")->references().empty());
}

TEST_CASE("Expression_AppliesPrecedenceAndLeftAssociativity", "[expressions][p12]") {
    struct Case {
        std::string_view text;
        double expected;
    };
    for (const Case& c : std::vector<Case>{
             {"2 + 3 * 4", 14.0},
             {"(2 + 3) * 4", 20.0},
             {"10 - 4 - 3", 3.0},
             {"10 - (4 - 3)", 9.0},
             {"24 / 4 / 3", 2.0},
             {"8 / 2 * 4", 16.0},
             {"-2 * 3", -6.0},
             {"2 * -3", -6.0},
             {"- -2", 2.0},
             {"+5", 5.0},
             {"-(1 - 3)", 2.0},
             {"1 - -1", 2.0},
             {"((((7))))", 7.0},
             {"  2*(3+4)  ", 14.0},
             {"2\t*\n(3\r+ 4)", 14.0},
         }) {
        CAPTURE(c.text);
        const DimensionedValue value = require(c.text);
        CHECK(value.dimension == dimensions::dimensionless);
        CHECK(value.siValue == c.expected);
    }
}

TEST_CASE("Expression_AcceptanceExamplesEvaluateFromTheirInputs", "[expressions][p12][acceptance]") {
    // height = width / 2, thickness = 0.1 * width,
    // hole_spacing = width - 2 * edge_distance, with width = 100 mm and
    // edge_distance = 15 mm: 50 mm, 10 mm and 70 mm.
    const DimensionedValue height = require("width / 2", kPlate);
    const DimensionedValue thickness = require("0.1 * width", kPlate);
    const DimensionedValue spacing = require("width - 2 * edge_distance", kPlate);
    for (const DimensionedValue& value : {height, thickness, spacing}) {
        CHECK(value.dimension == dimensions::length);
    }
    CHECK(bits(height.siValue) == bits(0.1 / 2.0));
    CHECK(bits(thickness.siValue) == bits(0.1 * 0.1));
    CHECK(bits(spacing.siValue) == bits(0.1 - 2.0 * 0.015));
    CHECK_THAT(height.siValue, WithinRel(0.050, 1e-15));
    CHECK_THAT(thickness.siValue, WithinRel(0.010, 1e-15));
    CHECK_THAT(spacing.siValue, WithinRel(0.070, 1e-15));

    // The same text with other inputs gives the new values.
    const SymbolResolver wider = symbols({{"width", DimensionedValue::of(160_mm)},
                                          {"edge_distance", DimensionedValue::of(15_mm)}});
    CHECK_THAT(require("width / 2", wider).siValue, WithinRel(0.080, 1e-15));
    CHECK_THAT(require("0.1 * width", wider).siValue, WithinRel(0.016, 1e-15));
    CHECK_THAT(require("width - 2 * edge_distance", wider).siValue, WithinRel(0.130, 1e-15));
}

TEST_CASE("Expression_DimensionalAnalysisCombinesAndChecksDimensions", "[expressions][p12]") {
    // Products and quotients combine dimensions.
    CHECK(require("width / width", kPlate).dimension == dimensions::dimensionless);
    CHECK(require("width / width", kPlate).siValue == 1.0);
    CHECK(require("width * width", kPlate).dimension == dimensions::area);
    CHECK(require("width * width * width", kPlate).dimension == dimensions::volume);
    CHECK(require("1 / width", kPlate).dimension == dimensions::length.inverse());
    CHECK(require("width / 3 m/s", kPlate).dimension == dimensions::time);
    CHECK(require("draft * 2", kPlate).dimension == dimensions::angle);
    CHECK(require("width + 5 mm", kPlate).dimension == dimensions::length);
    CHECK(require("width * ratio - 1 in", kPlate).dimension == dimensions::length);
    CHECK_THAT(require("width * width + 1 cm^2", kPlate).siValue, WithinRel(0.0101, 1e-15));

    // Sums and differences need one dimension; nothing is converted.
    const Error angle = failure("width + draft", kPlate);
    CHECK(angle.code == ErrorCode::DimensionMismatch);
    CHECK(angle.message == "cannot add 'width' (length) and 'draft' (angle) at offset 6");
    const Error plain = failure("width - 1", kPlate);
    CHECK(plain.code == ErrorCode::DimensionMismatch);
    CHECK(plain.message == "cannot subtract '1' (dimensionless) from 'width' (length) at offset 6");
    const Error nested = failure("2 * (width + 3 deg) / 4", kPlate);
    CHECK(nested.code == ErrorCode::DimensionMismatch);
    CHECK(nested.message == "cannot add 'width' (length) and '3 deg' (angle) at offset 11");
    CHECK(failure("width * width - width", kPlate).message ==
          "cannot subtract 'width' (length) from 'width * width' (area) at offset 14");
    CHECK(errorCode(evaluate("1 mm + 1 s")) == ErrorCode::DimensionMismatch);
    CHECK(errorCode(evaluate("1 rad - 1")) == ErrorCode::DimensionMismatch);
}

TEST_CASE("Expression_UnitIsTheLongestSymbolEndingAtANameBoundary", "[expressions][p12]") {
    // "mm/s" is a unit only where a name cannot continue.
    CHECK(require("3 mm/s").dimension == dimensions::velocity);
    const DimensionedValue divided = require("3 mm/speed", kPlate);
    CHECK(divided.dimension == dimensions::time);
    CHECK_THAT(divided.siValue, WithinRel(0.003 / 0.5, 1e-15));
    CHECK(require("3 m/s2", kPlate).dimension == dimensions::length / dimensions::time);
    CHECK(require("5 min").siValue == 300.0);
    CHECK(require("5 m").siValue == 5.0);
    CHECK(require("2 in").siValue == 2.0 * units::inch.scale.toSi(1.0));
    // A name that is not directly after a number is a parameter, even if it
    // spells a unit.
    CHECK(require("a / s", kPlate).dimension == dimensions::length);
    CHECK(bits(require("a / s", kPlate).siValue) == bits(0.003 / 4.0));
}

TEST_CASE("Expression_RejectsMalformedSyntaxNamingTheToken", "[expressions][p12]") {
    struct Case {
        std::string_view text;
        ErrorCode code;
        std::string_view message;
    };
    for (const Case& c : std::vector<Case>{
             {"", ErrorCode::ParseError, "the expression is empty"},
             {"  \t ", ErrorCode::ParseError, "the expression is empty"},
             {"width +", ErrorCode::ParseError,
              "expected a number, a name or '(' at offset 7, found the end of the expression"},
             {"* width", ErrorCode::ParseError, "expected a number, a name or '(' at offset 0, found '*'"},
             {"width * * 2", ErrorCode::ParseError, "expected a number, a name or '(' at offset 8, found '*'"},
             {"(width", ErrorCode::ParseError,
              "expected ')' at offset 6 to close the '(' at offset 0, found the end of the expression"},
             {"(width + 2 mm 3)", ErrorCode::ParseError,
              "expected ')' at offset 14 to close the '(' at offset 0, found '3'"},
             {"width)", ErrorCode::ParseError,
              "expected an operator or the end of the expression at offset 5, found ')'"},
             {"width 2", ErrorCode::ParseError,
              "expected an operator or the end of the expression at offset 6, found '2'"},
             {"width height", ErrorCode::ParseError,
              "expected an operator or the end of the expression at offset 6, found 'height'"},
             {"()", ErrorCode::ParseError, "expected a number, a name or '(' at offset 1, found ')'"},
             {"2 ^ 3", ErrorCode::ParseError,
              "expected an operator or the end of the expression at offset 2, found '^'"},
             {"width % 2", ErrorCode::ParseError,
              "expected an operator or the end of the expression at offset 6, found '%'"},
             {"2 width", ErrorCode::ParseError,
              "unknown unit 'width' at offset 2 (a name directly after a number must be a unit; write '*' to "
              "multiply)"},
             {"10 furlong", ErrorCode::ParseError, "unknown unit 'furlong' at offset 3"},
             {"1 mmx", ErrorCode::ParseError, "unknown unit 'mmx' at offset 2"},
             {"3mm2", ErrorCode::ParseError, "unknown unit 'mm2' at offset 1"},
             {".", ErrorCode::ParseError, "'.' at offset 0 is not a number"},
             {"2 * .x", ErrorCode::ParseError, "'.x' at offset 4 is not a number"},
             {"1.5.2", ErrorCode::ParseError,
              "expected an operator or the end of the expression at offset 3, found '.'"},
             {"caf\xC3\xA9", ErrorCode::ParseError,
              "expected an operator or the end of the expression at offset 3, found the byte 0xC3"},
             {"1e999", ErrorCode::InvalidArgument, "the number '1e999' at offset 0 is out of range"},
             {"1e308 km", ErrorCode::InvalidArgument, "'1e308 km' at offset 0 is out of range"},
         }) {
        CAPTURE(c.text);
        const Error error = failure(c.text);
        CHECK(error.code == c.code);
        CHECK_THAT(error.message, ContainsSubstring(std::string{c.message}));
    }
}

TEST_CASE("Expression_EnforcesSizeAndNestingLimits", "[expressions][p12]") {
    // Length.
    const std::string longest = "1" + std::string(kMaxExpressionLength - 1, ' ');
    CHECK(require(longest).siValue == 1.0);
    const Error tooLong = failure(longest + " ");
    CHECK(tooLong.code == ErrorCode::InvalidArgument);
    CHECK(tooLong.message == std::format("the expression is {} bytes long; the limit is {}",
                                         kMaxExpressionLength + 1, kMaxExpressionLength));

    // Parentheses: 32 levels are accepted, 33 are not.
    const auto nested = [](std::size_t depth) {
        return std::string(depth, '(') + "1" + std::string(depth, ')');
    };
    CHECK(require(nested(kMaxExpressionDepth)).siValue == 1.0);
    const Error deep = failure(nested(kMaxExpressionDepth + 1));
    CHECK(deep.code == ErrorCode::InvalidArgument);
    CHECK_THAT(deep.message, ContainsSubstring("nested deeper than 32 levels"));

    // Unary signs count as levels too.
    CHECK(require(std::string(kMaxExpressionDepth, '-') + "1").siValue == 1.0);
    CHECK(errorCode(evaluate(std::string(kMaxExpressionDepth + 1, '-') + "1")) == ErrorCode::InvalidArgument);

    // Names are identifiers of at most 64 characters.
    const std::string name64(64, 'n');
    CHECK(Expression::parse(name64).has_value());
    const Error longName = failure("2 * " + name64 + "n");
    CHECK(longName.code == ErrorCode::InvalidArgument);
    CHECK_THAT(longName.message, ContainsSubstring("is longer than 64 characters at offset 4"));
}

TEST_CASE("Expression_EvaluationFailuresNameTheOffendingToken", "[expressions][p12]") {
    const Error unknown = failure("wdth / 2", kPlate);
    CHECK(unknown.code == ErrorCode::NotFound);
    CHECK(unknown.message == "unknown parameter 'wdth' at offset 0");

    // Without a resolver every name is unknown.
    CHECK(failure("2 * width").message == "unknown parameter 'width' at offset 4");

    // The resolver's own error keeps its code.
    const SymbolResolver refusing = [](std::string_view name) -> Result<DimensionedValue> {
        return makeError(ErrorCode::InvalidArgument, std::format("'{}' is a sketch", name));
    };
    const Error refused = failure("1 mm + Base", refusing);
    CHECK(refused.code == ErrorCode::InvalidArgument);
    CHECK(refused.message == "'Base' is a sketch at offset 7");

    const Error zero = failure("width / (a - a)", kPlate);
    CHECK(zero.code == ErrorCode::InvalidArgument);
    CHECK(zero.message == "division by zero at offset 6: '(a - a)' is zero");
    CHECK(failure("1 / 0").message == "division by zero at offset 2: '0' is zero");
    CHECK(failure("1 / -0").message == "division by zero at offset 2: '-0' is zero");

    const Error overflow = failure("1e300 * 1e300");
    CHECK(overflow.code == ErrorCode::InvalidArgument);
    CHECK(overflow.message == "'1e300 * 1e300' at offset 6 is not finite");
    CHECK(errorCode(evaluate("1e308 * 10 - 1")) == ErrorCode::InvalidArgument);
}

TEST_CASE("Expression_EvaluationIsDeterministic", "[expressions][p12]") {
    const auto first = Expression::parse("(width - 2 * edge_distance) / 3 + 0.1 * width * ratio");
    const auto second = Expression::parse("(width - 2 * edge_distance) / 3 + 0.1 * width * ratio");
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    const DimensionedValue reference = first->evaluate(kPlate).value();
    // Left to right, as written: ((w - 2e) / 3) + ((0.1 * w) * r).
    const double expected = (0.1 - 2.0 * 0.015) / 3.0 + 0.1 * 0.1 * 0.25;
    CHECK(bits(reference.siValue) == bits(expected));
    for (int i = 0; i < 1000; ++i) {
        const DimensionedValue a = first->evaluate(kPlate).value();
        const DimensionedValue b = second->evaluate(kPlate).value();
        REQUIRE(bits(a.siValue) == bits(reference.siValue));
        REQUIRE(bits(b.siValue) == bits(reference.siValue));
        REQUIRE(a.dimension == dimensions::length);
    }
    // A copy is independent of the original and evaluates the same way.
    const Expression copy = *first;
    CHECK(bits(copy.evaluate(kPlate).value().siValue) == bits(reference.siValue));
    CHECK(copy.references() == first->references());
}
