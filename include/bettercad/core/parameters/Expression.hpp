#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Export.hpp>
#include <bettercad/core/units/DimensionedValue.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace bettercad {

/// Longest expression text accepted, in bytes.
inline constexpr std::size_t kMaxExpressionLength = 512;
/// Deepest nesting of parentheses and unary signs accepted.
inline constexpr std::size_t kMaxExpressionDepth = 32;

/// Supplies the value of a name used in an expression. A failure (an unknown
/// name, or a name of something that is not a value) fails the evaluation
/// with that error, and the evaluation adds where the name occurs.
using SymbolResolver = std::function<Result<DimensionedValue>(std::string_view name)>;

class ExpressionParser;

/// A parsed parameter expression, e.g. `width / 2`, `0.1 * width`,
/// `width - 2 * edge_distance` or `-(a - 3 mm)`.
///
/// Grammar, with the usual precedence and left associativity:
///
/// ```text
/// expression := term (('+' | '-') term)*
/// term       := unary (('*' | '/') unary)*
/// unary      := ('+' | '-') unary | primary
/// primary    := number [unit] | name | '(' expression ')'
/// number     := decimal digits with an optional fraction and exponent
/// name       := [A-Za-z_][A-Za-z0-9_]*
/// ```
///
/// Rules:
/// - A number without a unit is dimensionless. With a unit, it is converted
///   to SI with the unit's exact factor: `100 mm` and `100mm` are 0.1 m.
/// - **A name directly after a number is always a unit**, never a parameter:
///   implicit multiplication does not exist, so `2 width` is refused (write
///   `2 * width`). The unit is the longest catalog symbol that ends where a
///   name would end: `1 mm^2` is an area, `5 min` a time, `3 m/s` a velocity,
///   but `3 mm/speed` is 3 mm divided by the parameter `speed`.
/// - Names anywhere else are parameter names, even where they spell a unit:
///   in `width / s`, `s` is a parameter.
/// - `+` and `-` need operands of the same dimension. `*` and `/` combine
///   dimensions, so `Length / Length` is dimensionless and `Length + Angle`
///   fails with DimensionMismatch. Nothing is converted implicitly.
/// - There are no functions, powers or constants.
///
/// Parsing needs no document: it checks syntax and units, not whether names
/// exist. Evaluation resolves the names. Expression is a value type.
class BETTERCAD_CORE_EXPORT Expression {
public:
    /// Parses @p text. Malformed text (syntax, an unknown unit, a malformed
    /// number) fails with ParseError; text longer than kMaxExpressionLength,
    /// nesting deeper than kMaxExpressionDepth and numbers that do not fit a
    /// double fail with InvalidArgument. Messages quote the offending token
    /// and give its byte offset.
    [[nodiscard]] static Result<Expression> parse(std::string_view text);

    /// The text this expression was parsed from.
    [[nodiscard]] const std::string& text() const noexcept { return text_; }

    /// The names used, ascending and without duplicates: the expression's
    /// dependencies.
    [[nodiscard]] const std::vector<std::string>& references() const noexcept { return references_; }

    /// Byte offset of the first use of @p name; the text size if it is not used.
    [[nodiscard]] std::size_t offsetOf(std::string_view name) const noexcept;

    /// Evaluates the expression. Each name's value comes from @p resolve.
    ///
    /// Fails with the resolver's error for a name it refuses,
    /// DimensionMismatch for a sum or difference of different dimensions,
    /// and InvalidArgument for a division by zero or a value that is not
    /// finite. Messages quote the operands involved and give the operator's
    /// byte offset. Evaluation is a loop over the postfix form with no
    /// recursion, and is deterministic.
    [[nodiscard]] Result<DimensionedValue> evaluate(const SymbolResolver& resolve) const;

private:
    friend class ExpressionParser;

    enum class Op : std::uint8_t {
        Value,    ///< push a literal
        Symbol,   ///< push the value of references_[symbol]
        Add,      ///< a + b
        Subtract, ///< a - b
        Multiply, ///< a * b
        Divide,   ///< a / b
        Negate,   ///< -a
    };

    /// One operation of the postfix form. [begin, end) is the text of the
    /// sub-expression whose value the operation leaves on the stack, and
    /// position the offset of the operator (or of the literal or name).
    struct Step {
        Op op = Op::Value;
        DimensionedValue value{};
        std::size_t symbol = 0;
        std::size_t position = 0;
        std::size_t begin = 0;
        std::size_t end = 0;
    };

    Expression(std::string text, std::vector<Step> steps, std::vector<std::string> references);

    std::string text_;
    std::vector<Step> steps_;
    std::vector<std::string> references_;
};

} // namespace bettercad
