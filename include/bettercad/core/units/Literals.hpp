#pragma once

#include <bettercad/core/units/Units.hpp>

// User-defined literals for quantities: `100_mm`, `45_deg`, `2.5_MPa`.
// Bring them into scope with `using namespace bettercad::literals;`.

namespace bettercad {

namespace detail {

template <Dimension D>
[[nodiscard]] constexpr Quantity<D> quantityFromLiteral(long double value,
                                                        const Unit<D>& unit) noexcept {
    return static_cast<double>(value) * unit;
}

template <Dimension D>
[[nodiscard]] constexpr Quantity<D> quantityFromLiteral(unsigned long long value,
                                                        const Unit<D>& unit) noexcept {
    return static_cast<double>(value) * unit;
}

} // namespace detail

namespace literals {

// clang-format off
// Length
constexpr Length operator""_m(long double v) noexcept { return detail::quantityFromLiteral(v, units::m); }
constexpr Length operator""_m(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::m); }
constexpr Length operator""_mm(long double v) noexcept { return detail::quantityFromLiteral(v, units::mm); }
constexpr Length operator""_mm(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::mm); }
constexpr Length operator""_cm(long double v) noexcept { return detail::quantityFromLiteral(v, units::cm); }
constexpr Length operator""_cm(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::cm); }
constexpr Length operator""_um(long double v) noexcept { return detail::quantityFromLiteral(v, units::um); }
constexpr Length operator""_um(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::um); }
constexpr Length operator""_km(long double v) noexcept { return detail::quantityFromLiteral(v, units::km); }
constexpr Length operator""_km(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::km); }
constexpr Length operator""_in(long double v) noexcept { return detail::quantityFromLiteral(v, units::inch); }
constexpr Length operator""_in(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::inch); }
constexpr Length operator""_ft(long double v) noexcept { return detail::quantityFromLiteral(v, units::ft); }
constexpr Length operator""_ft(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::ft); }

// Area
constexpr Area operator""_m2(long double v) noexcept { return detail::quantityFromLiteral(v, units::m2); }
constexpr Area operator""_m2(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::m2); }
constexpr Area operator""_cm2(long double v) noexcept { return detail::quantityFromLiteral(v, units::cm2); }
constexpr Area operator""_cm2(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::cm2); }
constexpr Area operator""_mm2(long double v) noexcept { return detail::quantityFromLiteral(v, units::mm2); }
constexpr Area operator""_mm2(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::mm2); }

// Volume
constexpr Volume operator""_m3(long double v) noexcept { return detail::quantityFromLiteral(v, units::m3); }
constexpr Volume operator""_m3(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::m3); }
constexpr Volume operator""_L(long double v) noexcept { return detail::quantityFromLiteral(v, units::L); }
constexpr Volume operator""_L(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::L); }
constexpr Volume operator""_cm3(long double v) noexcept { return detail::quantityFromLiteral(v, units::cm3); }
constexpr Volume operator""_cm3(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::cm3); }
constexpr Volume operator""_mm3(long double v) noexcept { return detail::quantityFromLiteral(v, units::mm3); }
constexpr Volume operator""_mm3(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::mm3); }

// Angle
constexpr Angle operator""_rad(long double v) noexcept { return detail::quantityFromLiteral(v, units::rad); }
constexpr Angle operator""_rad(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::rad); }
constexpr Angle operator""_deg(long double v) noexcept { return detail::quantityFromLiteral(v, units::deg); }
constexpr Angle operator""_deg(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::deg); }

// Mass
constexpr Mass operator""_kg(long double v) noexcept { return detail::quantityFromLiteral(v, units::kg); }
constexpr Mass operator""_kg(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::kg); }
constexpr Mass operator""_g(long double v) noexcept { return detail::quantityFromLiteral(v, units::g); }
constexpr Mass operator""_g(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::g); }
constexpr Mass operator""_t(long double v) noexcept { return detail::quantityFromLiteral(v, units::tonne); }
constexpr Mass operator""_t(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::tonne); }

// Time
constexpr Time operator""_s(long double v) noexcept { return detail::quantityFromLiteral(v, units::s); }
constexpr Time operator""_s(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::s); }
constexpr Time operator""_ms(long double v) noexcept { return detail::quantityFromLiteral(v, units::ms); }
constexpr Time operator""_ms(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::ms); }
constexpr Time operator""_min(long double v) noexcept { return detail::quantityFromLiteral(v, units::minute); }
constexpr Time operator""_min(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::minute); }
constexpr Time operator""_h(long double v) noexcept { return detail::quantityFromLiteral(v, units::hour); }
constexpr Time operator""_h(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::hour); }

// Temperature
constexpr Temperature operator""_K(long double v) noexcept { return detail::quantityFromLiteral(v, units::K); }
constexpr Temperature operator""_K(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::K); }

// Velocity
constexpr Velocity operator""_m_per_s(long double v) noexcept { return detail::quantityFromLiteral(v, units::m_per_s); }
constexpr Velocity operator""_m_per_s(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::m_per_s); }
constexpr Velocity operator""_mm_per_s(long double v) noexcept { return detail::quantityFromLiteral(v, units::mm_per_s); }
constexpr Velocity operator""_mm_per_s(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::mm_per_s); }
constexpr Velocity operator""_km_per_h(long double v) noexcept { return detail::quantityFromLiteral(v, units::km_per_h); }
constexpr Velocity operator""_km_per_h(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::km_per_h); }

// Acceleration
constexpr Acceleration operator""_m_per_s2(long double v) noexcept { return detail::quantityFromLiteral(v, units::m_per_s2); }
constexpr Acceleration operator""_m_per_s2(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::m_per_s2); }

// Force
constexpr Force operator""_N(long double v) noexcept { return detail::quantityFromLiteral(v, units::N); }
constexpr Force operator""_N(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::N); }
constexpr Force operator""_kN(long double v) noexcept { return detail::quantityFromLiteral(v, units::kN); }
constexpr Force operator""_kN(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::kN); }

// Pressure / stress
constexpr Pressure operator""_Pa(long double v) noexcept { return detail::quantityFromLiteral(v, units::Pa); }
constexpr Pressure operator""_Pa(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::Pa); }
constexpr Pressure operator""_kPa(long double v) noexcept { return detail::quantityFromLiteral(v, units::kPa); }
constexpr Pressure operator""_kPa(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::kPa); }
constexpr Pressure operator""_MPa(long double v) noexcept { return detail::quantityFromLiteral(v, units::MPa); }
constexpr Pressure operator""_MPa(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::MPa); }
constexpr Pressure operator""_GPa(long double v) noexcept { return detail::quantityFromLiteral(v, units::GPa); }
constexpr Pressure operator""_GPa(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::GPa); }
constexpr Pressure operator""_bar(long double v) noexcept { return detail::quantityFromLiteral(v, units::bar); }
constexpr Pressure operator""_bar(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::bar); }

// Density
constexpr Density operator""_kg_per_m3(long double v) noexcept { return detail::quantityFromLiteral(v, units::kg_per_m3); }
constexpr Density operator""_kg_per_m3(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::kg_per_m3); }
constexpr Density operator""_g_per_cm3(long double v) noexcept { return detail::quantityFromLiteral(v, units::g_per_cm3); }
constexpr Density operator""_g_per_cm3(unsigned long long v) noexcept { return detail::quantityFromLiteral(v, units::g_per_cm3); }
// clang-format on

} // namespace literals

} // namespace bettercad
