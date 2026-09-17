#pragma once

// Parsing of the numbers in standard designations: exact, in whole units, so
// that a designation matches a table entry without floating-point rounding.

#include <cstdint>
#include <optional>
#include <string_view>

namespace bettercad::standards::detail {

/// A whole number from 1 to 999 written without leading zeros ("7", "13").
inline std::optional<int> parseWholeNumber(std::string_view text) {
    if (text.empty() || text.size() > 3 || text.front() == '0') {
        return std::nullopt;
    }
    int value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        value = 10 * value + (c - '0');
    }
    return value;
}

/// A positive length in millimetres written as digits with at most three
/// decimals, without leading zeros before other digits or trailing zeros
/// after the point ("8", "1.25", "0.8"), in micrometres.
inline std::optional<std::uint32_t> parseMillimetresInMicrometres(std::string_view text) {
    const std::size_t point = text.find('.');
    const std::string_view whole = text.substr(0, point);
    const std::string_view decimals = point == std::string_view::npos ? std::string_view{} : text.substr(point + 1);
    const bool hasPoint = point != std::string_view::npos;
    if (whole.empty() || whole.size() > 4 || (whole.size() > 1 && whole.front() == '0') ||
        (hasPoint && (decimals.empty() || decimals.size() > 3 || decimals.back() == '0'))) {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    for (const char c : whole) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        value = 10 * value + static_cast<std::uint32_t>(c - '0');
    }
    std::uint32_t fraction = 0;
    std::uint32_t scale = 1000;
    for (const char c : decimals) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        scale /= 10;
        fraction += scale * static_cast<std::uint32_t>(c - '0');
    }
    const std::uint32_t micrometres = 1000 * value + fraction;
    if (micrometres == 0) {
        return std::nullopt;
    }
    return micrometres;
}

} // namespace bettercad::standards::detail
