#include <bettercad/core/parameters/Expression.hpp>

#include <bettercad/core/Naming.hpp>
#include <bettercad/core/units/Format.hpp>
#include <bettercad/core/units/UnitCatalog.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace bettercad {

namespace {

constexpr bool isNameStart(char c) noexcept {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

constexpr bool isDigit(char c) noexcept {
    return c >= '0' && c <= '9';
}

constexpr bool isNameChar(char c) noexcept {
    return isNameStart(c) || isDigit(c);
}

constexpr bool isSpace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/// Text for a message: 'text', shortened if long.
std::string quote(std::string_view text) {
    constexpr std::size_t kMaxQuoted = 40;
    if (text.size() > kMaxQuoted) {
        return std::format("'{}...'", text.substr(0, kMaxQuoted));
    }
    return std::format("'{}'", text);
}

/// The unit catalog with the longest symbols first; equal lengths in symbol
/// order, so the order is fixed.
const std::vector<UnitDescriptor>& unitsLongestFirst() {
    static const std::vector<UnitDescriptor> units = [] {
        const auto catalog = unitCatalog();
        std::vector<UnitDescriptor> sorted(catalog.begin(), catalog.end());
        std::ranges::sort(sorted, [](const UnitDescriptor& a, const UnitDescriptor& b) {
            return a.symbol.size() != b.symbol.size() ? a.symbol.size() > b.symbol.size() : a.symbol < b.symbol;
        });
        return sorted;
    }();
    return units;
}

/// The longest catalog unit whose symbol starts @p text and is not followed
/// by a name character, so that "mm/speed" is "mm" and not "mm/s".
std::optional<UnitDescriptor> matchUnit(std::string_view text) {
    for (const UnitDescriptor& unit : unitsLongestFirst()) {
        if (unit.symbol.empty() || !text.starts_with(unit.symbol)) {
            continue;
        }
        if (text.size() > unit.symbol.size() && isNameChar(text[unit.symbol.size()])) {
            continue;
        }
        return unit;
    }
    return std::nullopt;
}

/// The leading name characters of @p text.
std::string_view leadingName(std::string_view text) {
    std::size_t length = 0;
    while (length < text.size() && isNameChar(text[length])) {
        ++length;
    }
    return text.substr(0, length);
}

} // namespace

/// Recursive-descent parser producing the postfix form. It is a friend of
/// Expression so that it can build Expression's private steps. Every parse
/// function returns the text span of what it parsed, for messages.
class ExpressionParser {
public:
    [[nodiscard]] static Result<Expression> build(std::string_view text);

private:
    using Op = Expression::Op;
    using Step = Expression::Step;

    struct Span {
        std::size_t begin = 0;
        std::size_t end = 0;
    };

    explicit ExpressionParser(std::string_view text) noexcept : text_(text) {}

    void skipSpace() noexcept {
        while (pos_ < text_.size() && isSpace(text_[pos_])) {
            ++pos_;
        }
    }

    [[nodiscard]] char peek() const noexcept { return pos_ < text_.size() ? text_[pos_] : '\0'; }

    /// The token at the current position, for messages.
    [[nodiscard]] std::string currentToken() const {
        if (pos_ >= text_.size()) {
            return "the end of the expression";
        }
        const std::string_view rest = text_.substr(pos_);
        if (isNameChar(rest.front())) {
            return quote(leadingName(rest));
        }
        const auto byte = static_cast<unsigned char>(rest.front());
        if (byte < 0x20 || byte >= 0x7F) {
            // Not printable ASCII: never cut a UTF-8 sequence into a message.
            return std::format("the byte 0x{:02X}", byte);
        }
        return quote(rest.substr(0, 1));
    }

    [[nodiscard]] std::unexpected<Error> syntaxError(std::string_view expected) const {
        return makeError(ErrorCode::ParseError,
                         std::format("expected {} at offset {}, found {}", expected, pos_, currentToken()));
    }

    [[nodiscard]] Result<void> checkDepth(std::size_t depth) const {
        if (depth > kMaxExpressionDepth) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("nested deeper than {} levels at offset {}", kMaxExpressionDepth, pos_));
        }
        return {};
    }

    void emit(Op op, Span span, std::size_t position, DimensionedValue value = {}, std::string name = {}) {
        steps_.push_back(Step{.op = op,
                              .value = value,
                              .symbol = 0,
                              .position = position,
                              .begin = span.begin,
                              .end = span.end});
        names_.push_back(std::move(name));
    }

    /// expression := term (('+' | '-') term)*
    [[nodiscard]] Result<Span> parseExpression(std::size_t depth) {
        if (auto deep = checkDepth(depth); !deep) {
            return std::unexpected(deep.error());
        }
        auto first = parseTerm(depth);
        if (!first) {
            return first;
        }
        Span left = *first;
        while (true) {
            skipSpace();
            const char c = peek();
            if (c != '+' && c != '-') {
                return left;
            }
            const std::size_t at = pos_++;
            auto right = parseTerm(depth);
            if (!right) {
                return right;
            }
            left = Span{left.begin, right->end};
            emit(c == '+' ? Op::Add : Op::Subtract, left, at);
        }
    }

    /// term := unary (('*' | '/') unary)*
    [[nodiscard]] Result<Span> parseTerm(std::size_t depth) {
        auto first = parseUnary(depth);
        if (!first) {
            return first;
        }
        Span left = *first;
        while (true) {
            skipSpace();
            const char c = peek();
            if (c != '*' && c != '/') {
                return left;
            }
            const std::size_t at = pos_++;
            auto right = parseUnary(depth);
            if (!right) {
                return right;
            }
            left = Span{left.begin, right->end};
            emit(c == '*' ? Op::Multiply : Op::Divide, left, at);
        }
    }

    /// unary := ('+' | '-') unary | primary; each sign is one nesting level.
    [[nodiscard]] Result<Span> parseUnary(std::size_t depth) {
        skipSpace();
        const char c = peek();
        if (c != '+' && c != '-') {
            return parsePrimary(depth);
        }
        const std::size_t at = pos_++;
        if (auto deep = checkDepth(depth + 1); !deep) {
            return std::unexpected(deep.error());
        }
        auto operand = parseUnary(depth + 1);
        if (!operand) {
            return operand;
        }
        const Span span{at, operand->end};
        if (c == '-') {
            emit(Op::Negate, span, at);
        }
        return span;
    }

    /// primary := number [unit] | name | '(' expression ')'
    [[nodiscard]] Result<Span> parsePrimary(std::size_t depth) {
        skipSpace();
        const char c = peek();
        if (c == '(') {
            const std::size_t open = pos_++;
            auto inner = parseExpression(depth + 1);
            if (!inner) {
                return inner;
            }
            skipSpace();
            if (peek() != ')') {
                return makeError(ErrorCode::ParseError,
                                 std::format("expected ')' at offset {} to close the '(' at offset {}, found {}",
                                             pos_, open, currentToken()));
            }
            ++pos_;
            // The last step computes the value inside; quote it with its
            // parentheses.
            steps_.back().begin = open;
            steps_.back().end = pos_;
            return Span{open, pos_};
        }
        if (isDigit(c) || c == '.') {
            return parseNumber();
        }
        if (isNameStart(c)) {
            return parseName();
        }
        return syntaxError("a number, a name or '('");
    }

    /// A decimal number, optionally followed by a unit.
    [[nodiscard]] Result<Span> parseNumber() {
        const std::size_t at = pos_;
        double number = 0.0;
        const char* const first = text_.data() + at;
        const char* const last = text_.data() + text_.size();
        const auto [stop, ec] = std::from_chars(first, last, number, std::chars_format::general);
        if (ec == std::errc::invalid_argument) {
            // A leading digit always parses, so this is a '.' without digits.
            const std::string_view token = text_.substr(at, 1 + leadingName(text_.substr(at + 1)).size());
            return makeError(ErrorCode::ParseError,
                             std::format("{} at offset {} is not a number", quote(token), at));
        }
        if (ec == std::errc::result_out_of_range) {
            const std::string_view token{first, static_cast<std::size_t>(stop - first)};
            return makeError(ErrorCode::InvalidArgument,
                             std::format("the number {} at offset {} is out of range", quote(token), at));
        }
        pos_ = static_cast<std::size_t>(stop - text_.data());

        std::size_t unitAt = pos_;
        while (unitAt < text_.size() && isSpace(text_[unitAt])) {
            ++unitAt;
        }
        DimensionedValue value{dimensions::dimensionless, number};
        if (unitAt < text_.size() && isNameStart(text_[unitAt])) {
            const std::string_view rest = text_.substr(unitAt);
            const std::optional<UnitDescriptor> unit = matchUnit(rest);
            if (!unit) {
                return makeError(ErrorCode::ParseError,
                                 std::format("unknown unit {} at offset {} (a name directly after a number must "
                                             "be a unit; write '*' to multiply)",
                                             quote(leadingName(rest)), unitAt));
            }
            value = DimensionedValue{unit->dimension, unit->scale.toSi(number)};
            pos_ = unitAt + unit->symbol.size();
        }
        const Span span{at, pos_};
        if (!std::isfinite(value.siValue)) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{} at offset {} is out of range", quote(text_.substr(at, pos_ - at)), at));
        }
        emit(Op::Value, span, at, value);
        return span;
    }

    /// A parameter name.
    [[nodiscard]] Result<Span> parseName() {
        const std::size_t at = pos_;
        const std::string_view name = leadingName(text_.substr(at));
        pos_ += name.size();
        if (auto valid = validateIdentifier(name, "parameter"); !valid) {
            return makeError(valid.error().code, std::format("{} at offset {}", valid.error().message, at));
        }
        const Span span{at, pos_};
        emit(Op::Symbol, span, at, {}, std::string{name});
        return span;
    }

    std::string_view text_;
    std::size_t pos_ = 0;
    std::vector<Step> steps_;
    /// The name of each step (empty except for Symbol steps).
    std::vector<std::string> names_;
};

Result<Expression> ExpressionParser::build(std::string_view text) {
    if (text.size() > kMaxExpressionLength) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("the expression is {} bytes long; the limit is {}", text.size(),
                                     kMaxExpressionLength));
    }
    if (std::ranges::all_of(text, isSpace)) {
        return makeError(ErrorCode::ParseError, "the expression is empty");
    }

    ExpressionParser parser{text};
    if (auto parsed = parser.parseExpression(0); !parsed) {
        return std::unexpected(parsed.error());
    }
    parser.skipSpace();
    if (parser.pos_ < text.size()) {
        return parser.syntaxError("an operator or the end of the expression");
    }

    const std::set<std::string> unique(parser.names_.begin(), parser.names_.end());
    std::vector<std::string> references;
    for (const std::string& name : unique) {
        if (!name.empty()) {
            references.push_back(name);
        }
    }
    std::vector<Step> steps = std::move(parser.steps_);
    for (std::size_t i = 0; i < steps.size(); ++i) {
        if (steps[i].op == Op::Symbol) {
            const auto found = std::ranges::lower_bound(references, parser.names_[i]);
            steps[i].symbol = static_cast<std::size_t>(found - references.begin());
        }
    }
    return Expression{std::string{text}, std::move(steps), std::move(references)};
}

Expression::Expression(std::string text, std::vector<Step> steps, std::vector<std::string> references)
    : text_(std::move(text)), steps_(std::move(steps)), references_(std::move(references)) {}

Result<Expression> Expression::parse(std::string_view text) {
    return ExpressionParser::build(text);
}

std::size_t Expression::offsetOf(std::string_view name) const noexcept {
    for (const Step& step : steps_) {
        if (step.op == Op::Symbol && references_[step.symbol] == name) {
            return step.position;
        }
    }
    return text_.size();
}

Result<DimensionedValue> Expression::evaluate(const SymbolResolver& resolve) const {
    struct Entry {
        DimensionedValue value{};
        std::size_t begin = 0;
        std::size_t end = 0;
    };
    std::vector<Entry> stack;
    stack.reserve(steps_.size());
    const auto pop = [&stack] {
        const Entry top = stack.back();
        stack.pop_back();
        return top;
    };
    const auto sourceOf = [this](const Entry& entry) {
        return quote(std::string_view{text_}.substr(entry.begin, entry.end - entry.begin));
    };
    const auto operandText = [&sourceOf](const Entry& entry) {
        return std::format("{} ({})", sourceOf(entry), describeDimension(entry.value.dimension));
    };

    for (const Step& step : steps_) {
        switch (step.op) {
        case Op::Value:
            stack.push_back({step.value, step.begin, step.end});
            break;
        case Op::Symbol: {
            const std::string& name = references_[step.symbol];
            if (!resolve) {
                return makeError(ErrorCode::NotFound,
                                 std::format("unknown parameter '{}' at offset {}", name, step.position));
            }
            auto value = resolve(name);
            if (!value) {
                return makeError(value.error().code,
                                 std::format("{} at offset {}", value.error().message, step.position));
            }
            stack.push_back({*value, step.begin, step.end});
            break;
        }
        case Op::Negate: {
            const Entry operand = pop();
            stack.push_back({{operand.value.dimension, -operand.value.siValue}, step.begin, step.end});
            break;
        }
        case Op::Add:
        case Op::Subtract: {
            const Entry b = pop();
            const Entry a = pop();
            if (a.value.dimension != b.value.dimension) {
                const std::string message =
                    step.op == Op::Add
                        ? std::format("cannot add {} and {} at offset {}", operandText(a), operandText(b),
                                      step.position)
                        : std::format("cannot subtract {} from {} at offset {}", operandText(b), operandText(a),
                                      step.position);
                return makeError(ErrorCode::DimensionMismatch, message);
            }
            const double sum = step.op == Op::Add ? a.value.siValue + b.value.siValue
                                                  : a.value.siValue - b.value.siValue;
            stack.push_back({{a.value.dimension, sum}, step.begin, step.end});
            break;
        }
        case Op::Multiply: {
            const Entry b = pop();
            const Entry a = pop();
            stack.push_back({{a.value.dimension * b.value.dimension, a.value.siValue * b.value.siValue},
                             step.begin,
                             step.end});
            break;
        }
        case Op::Divide: {
            const Entry b = pop();
            const Entry a = pop();
            if (b.value.siValue == 0.0) {
                return makeError(ErrorCode::InvalidArgument,
                                 std::format("division by zero at offset {}: {} is zero", step.position,
                                             sourceOf(b)));
            }
            stack.push_back({{a.value.dimension / b.value.dimension, a.value.siValue / b.value.siValue},
                             step.begin,
                             step.end});
            break;
        }
        }
        const Entry& top = stack.back();
        if (!std::isfinite(top.value.siValue)) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{} at offset {} is not finite", sourceOf(top), step.position));
        }
    }

    // The grammar leaves exactly one value; anything else is a bug.
    if (stack.size() != 1) {
        return makeError(ErrorCode::Internal,
                         std::format("expression '{}' left {} values instead of one", text_, stack.size()));
    }
    return stack.front().value;
}

} // namespace bettercad
