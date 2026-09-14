#pragma once

#include <bettercad/core/units/Dimension.hpp>
#include <bettercad/core/units/Quantity.hpp>

#include <cmath>
#include <numbers>

namespace bettercad {

using Length = Quantity<dimensions::length>;
using Area = Quantity<dimensions::area>;
using Volume = Quantity<dimensions::volume>;
using Angle = Quantity<dimensions::angle>;
using Mass = Quantity<dimensions::mass>;
using Time = Quantity<dimensions::time>;
using Temperature = Quantity<dimensions::temperature>;
using Velocity = Quantity<dimensions::velocity>;
using Acceleration = Quantity<dimensions::acceleration>;
using Force = Quantity<dimensions::force>;
using Pressure = Quantity<dimensions::pressure>;
using Density = Quantity<dimensions::density>;

/// Units of measurement. Every unit here is also listed in unitCatalog().
namespace units {

// Length
inline constexpr Unit<dimensions::length> m{"m", {1.0, 1.0}};
inline constexpr Unit<dimensions::length> mm{"mm", {1.0, 1000.0}};
inline constexpr Unit<dimensions::length> cm{"cm", {1.0, 100.0}};
inline constexpr Unit<dimensions::length> um{"um", {1.0, 1.0e6}};
inline constexpr Unit<dimensions::length> km{"km", {1000.0, 1.0}};
inline constexpr Unit<dimensions::length> inch{"in", {254.0, 10000.0}};
inline constexpr Unit<dimensions::length> ft{"ft", {3048.0, 10000.0}};

// Area
inline constexpr Unit<dimensions::area> m2{"m^2", {1.0, 1.0}};
inline constexpr Unit<dimensions::area> cm2{"cm^2", {1.0, 1.0e4}};
inline constexpr Unit<dimensions::area> mm2{"mm^2", {1.0, 1.0e6}};

// Volume
inline constexpr Unit<dimensions::volume> m3{"m^3", {1.0, 1.0}};
inline constexpr Unit<dimensions::volume> L{"L", {1.0, 1000.0}};
inline constexpr Unit<dimensions::volume> cm3{"cm^3", {1.0, 1.0e6}};
inline constexpr Unit<dimensions::volume> mm3{"mm^3", {1.0, 1.0e9}};

// Angle
inline constexpr Unit<dimensions::angle> rad{"rad", {1.0, 1.0}};
inline constexpr Unit<dimensions::angle> deg{"deg", {std::numbers::pi, 180.0}};

// Mass
inline constexpr Unit<dimensions::mass> kg{"kg", {1.0, 1.0}};
inline constexpr Unit<dimensions::mass> g{"g", {1.0, 1000.0}};
inline constexpr Unit<dimensions::mass> tonne{"t", {1000.0, 1.0}};

// Time
inline constexpr Unit<dimensions::time> s{"s", {1.0, 1.0}};
inline constexpr Unit<dimensions::time> ms{"ms", {1.0, 1000.0}};
inline constexpr Unit<dimensions::time> minute{"min", {60.0, 1.0}};
inline constexpr Unit<dimensions::time> hour{"h", {3600.0, 1.0}};

// Temperature (absolute scale only; offset scales such as Celsius need an
// affine conversion and are not supported yet)
inline constexpr Unit<dimensions::temperature> K{"K", {1.0, 1.0}};

// Velocity
inline constexpr Unit<dimensions::velocity> m_per_s{"m/s", {1.0, 1.0}};
inline constexpr Unit<dimensions::velocity> mm_per_s{"mm/s", {1.0, 1000.0}};
inline constexpr Unit<dimensions::velocity> km_per_h{"km/h", {1000.0, 3600.0}};

// Acceleration
inline constexpr Unit<dimensions::acceleration> m_per_s2{"m/s^2", {1.0, 1.0}};

// Force
inline constexpr Unit<dimensions::force> N{"N", {1.0, 1.0}};
inline constexpr Unit<dimensions::force> kN{"kN", {1000.0, 1.0}};

// Pressure / stress
inline constexpr Unit<dimensions::pressure> Pa{"Pa", {1.0, 1.0}};
inline constexpr Unit<dimensions::pressure> kPa{"kPa", {1.0e3, 1.0}};
inline constexpr Unit<dimensions::pressure> MPa{"MPa", {1.0e6, 1.0}};
inline constexpr Unit<dimensions::pressure> GPa{"GPa", {1.0e9, 1.0}};
inline constexpr Unit<dimensions::pressure> bar{"bar", {1.0e5, 1.0}};

// Density
inline constexpr Unit<dimensions::density> kg_per_m3{"kg/m^3", {1.0, 1.0}};
inline constexpr Unit<dimensions::density> g_per_cm3{"g/cm^3", {1000.0, 1.0}};

} // namespace units

// Trigonometry on angles. sin/cos/tan overload the C functions by parameter
// type. The inverse functions are named arcsin/arccos/arctan because
// asin(double) -> Angle would collide with the C library's asin(double).
[[nodiscard]] inline double sin(const Angle& angle) noexcept {
    return std::sin(angle.si());
}
[[nodiscard]] inline double cos(const Angle& angle) noexcept {
    return std::cos(angle.si());
}
[[nodiscard]] inline double tan(const Angle& angle) noexcept {
    return std::tan(angle.si());
}
[[nodiscard]] inline Angle arcsin(double ratio) noexcept {
    return Angle::fromSi(std::asin(ratio));
}
[[nodiscard]] inline Angle arccos(double ratio) noexcept {
    return Angle::fromSi(std::acos(ratio));
}
[[nodiscard]] inline Angle arctan(double ratio) noexcept {
    return Angle::fromSi(std::atan(ratio));
}
/// Angle of the vector (x, y) from the positive x axis, in (-pi, pi].
template <Dimension D>
[[nodiscard]] Angle atan2(const Quantity<D>& y, const Quantity<D>& x) noexcept {
    return Angle::fromSi(std::atan2(y.si(), x.si()));
}

} // namespace bettercad
