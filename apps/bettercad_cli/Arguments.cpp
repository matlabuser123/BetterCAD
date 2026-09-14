#include "Arguments.hpp"

#include <bettercad/core/units/Format.hpp>
#include <bettercad/core/units/UnitCatalog.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>

namespace bettercad::cli {

namespace {

/// Number and unit symbol of "<number>[ ]<unit>", converted to SI.
Result<double> parseSiValue(std::string_view text, const UnitDescriptor& defaultUnit) {
    const char* const begin = text.data();
    const char* const end = text.data() + text.size();
    double number = 0.0;
    const auto [rest, error] = std::from_chars(begin, end, number);
    if (error != std::errc{} || !std::isfinite(number)) {
        return makeError(ErrorCode::InvalidArgument, std::format("'{}' is not a number", text));
    }
    std::string_view symbol{rest, static_cast<std::size_t>(end - rest)};
    while (!symbol.empty() && symbol.front() == ' ') {
        symbol.remove_prefix(1);
    }
    if (symbol.empty()) {
        return defaultUnit.scale.toSi(number);
    }
    const auto unit = findUnit(symbol);
    if (!unit) {
        return makeError(ErrorCode::InvalidArgument, std::format("unknown unit '{}' in '{}'", symbol, text));
    }
    if (unit->dimension != defaultUnit.dimension) {
        return makeError(ErrorCode::DimensionMismatch,
                         std::format("'{}' has dimension {}; expected {}", text, describeDimension(unit->dimension),
                                     describeDimension(defaultUnit.dimension)));
    }
    return unit->scale.toSi(number);
}

} // namespace

std::optional<std::string_view> ParsedArguments::value(std::string_view option) const {
    const auto it = options_.find(option);
    if (it == options_.end()) {
        return std::nullopt;
    }
    return it->second;
}

Result<ParsedArguments> parseArguments(Args args, std::initializer_list<OptionSpec> options) {
    ParsedArguments parsed;
    bool optionsEnded = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if (optionsEnded || !arg.starts_with('-') || arg == "-") {
            parsed.positional_.push_back(arg);
            continue;
        }
        if (arg == "--") {
            optionsEnded = true;
            continue;
        }
        const auto equals = arg.find('=');
        const std::string_view name = arg.substr(0, equals);
        const auto spec = std::ranges::find(options, name, &OptionSpec::name);
        if (spec == options.end()) {
            return makeError(ErrorCode::InvalidArgument, std::format("unexpected argument '{}'", arg));
        }
        if (parsed.options_.contains(name)) {
            return makeError(ErrorCode::InvalidArgument, std::format("option '{}' is given twice", name));
        }
        std::string_view value;
        if (spec->takesValue) {
            if (equals != std::string_view::npos) {
                value = arg.substr(equals + 1);
            } else if (i + 1 < args.size()) {
                value = args[++i];
            } else {
                return makeError(ErrorCode::InvalidArgument, std::format("option '{}' needs a value", name));
            }
        } else if (equals != std::string_view::npos) {
            return makeError(ErrorCode::InvalidArgument, std::format("option '{}' takes no value", name));
        }
        parsed.options_.emplace(name, value);
    }
    return parsed;
}

std::filesystem::path pathFromArgument(std::string_view argument) {
    return std::filesystem::path(std::u8string(argument.begin(), argument.end()));
}

std::string displayPath(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

Result<Length> parseLength(std::string_view text, const Unit<dimensions::length>& defaultUnit) {
    auto si = parseSiValue(text, describe(defaultUnit));
    if (!si) {
        return std::unexpected(si.error());
    }
    return Length::fromSi(*si);
}

Result<Angle> parseAngle(std::string_view text, const Unit<dimensions::angle>& defaultUnit) {
    auto si = parseSiValue(text, describe(defaultUnit));
    if (!si) {
        return std::unexpected(si.error());
    }
    return Angle::fromSi(*si);
}

} // namespace bettercad::cli
