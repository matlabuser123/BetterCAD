#pragma once

#include <bettercad/core/Export.hpp>
#include <bettercad/core/units/Dimension.hpp>
#include <bettercad/core/units/Quantity.hpp>

#include <optional>
#include <span>
#include <string_view>

namespace bettercad {

/// Runtime description of a unit, used where the dimension is only known at
/// run time: parameter values, parsing and serialization.
struct UnitDescriptor {
    std::string_view symbol;
    Dimension dimension;
    UnitScale scale;

    friend constexpr bool operator==(const UnitDescriptor&, const UnitDescriptor&) = default;
};

template <Dimension D>
[[nodiscard]] constexpr UnitDescriptor describe(const Unit<D>& unit) noexcept {
    return {unit.symbol, D, unit.scale};
}

/// Every unit defined in bettercad::units, with unique symbols.
[[nodiscard]] BETTERCAD_CORE_EXPORT std::span<const UnitDescriptor> unitCatalog() noexcept;

/// The unit with exactly this symbol (case-sensitive: "mm", "MPa"), if any.
[[nodiscard]] BETTERCAD_CORE_EXPORT std::optional<UnitDescriptor>
findUnit(std::string_view symbol) noexcept;

} // namespace bettercad
