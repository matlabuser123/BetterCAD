#pragma once

#include <bettercad/core/Export.hpp>
#include <bettercad/core/units/Quantity.hpp>

#include <algorithm>
#include <format>
#include <ostream>
#include <string>

namespace bettercad {

/// Coherent SI unit symbol of a dimension, e.g. "m", "m/s^2", "kg/m^3",
/// "N" or "Pa". Empty for dimensionless.
[[nodiscard]] BETTERCAD_CORE_EXPORT std::string siUnitSymbol(const Dimension& dimension);

/// Name of a dimension for messages: "length", "pressure", "dimensionless",
/// or "quantity [kg/s^2]" for dimensions without a common name.
[[nodiscard]] BETTERCAD_CORE_EXPORT std::string describeDimension(const Dimension& dimension);

/// Quantity in coherent SI units, e.g. "0.1 m".
template <Dimension D>
[[nodiscard]] std::string toString(const Quantity<D>& q) {
    return std::format("{}", q);
}

/// Quantity in a chosen unit, e.g. "100 mm". Uses the shortest decimal
/// representation that round-trips.
template <Dimension D>
[[nodiscard]] std::string toString(const Quantity<D>& q, const Unit<D>& unit) {
    return std::format("{} {}", q.in(unit), unit.symbol);
}

template <Dimension D>
std::ostream& operator<<(std::ostream& os, const Quantity<D>& q) {
    return os << toString(q);
}

} // namespace bettercad

/// Formats the SI value with the standard floating-point format spec, followed
/// by the SI unit symbol: std::format("{:.3f}", 2.5_MPa) == "2500000.000 Pa".
template <bettercad::Dimension D>
struct std::formatter<bettercad::Quantity<D>> : std::formatter<double> {
    template <typename FormatContext>
    auto format(const bettercad::Quantity<D>& q, FormatContext& ctx) const {
        auto out = std::formatter<double>::format(q.si(), ctx);
        const std::string symbol = bettercad::siUnitSymbol(D);
        if (!symbol.empty()) {
            *out++ = ' ';
            out = std::ranges::copy(symbol, out).out;
        }
        return out;
    }
};
