#pragma once

#include <bettercad/core/units/Dimension.hpp>
#include <bettercad/core/units/Quantity.hpp>

namespace bettercad {

/// A value whose physical dimension is only known at run time: a parameter
/// edit, or what a parameter expression evaluates to. The number is in
/// coherent SI units, the way parameters store their values, so the
/// dimension is still checked wherever the value is used.
struct DimensionedValue {
    Dimension dimension{};
    double siValue = 0.0;

    template <Dimension D>
    [[nodiscard]] static constexpr DimensionedValue of(const Quantity<D>& value) noexcept {
        return {D, value.si()};
    }

    friend constexpr bool operator==(const DimensionedValue&, const DimensionedValue&) = default;
};

} // namespace bettercad
