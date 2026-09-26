#pragma once

#include <bettercad/core/units/Dimension.hpp>
#include <bettercad/core/units/Quantity.hpp>

#include <cmath>
#include <limits>
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

// Engineering-data quantities (P15-UNITS-001).
using Energy = Quantity<dimensions::energy>;
using Power = Quantity<dimensions::power>;
using ThermalConductivity = Quantity<dimensions::thermalConductivity>;
using SpecificHeatCapacity = Quantity<dimensions::specificHeatCapacity>;
using ThermalExpansionCoefficient = Quantity<dimensions::thermalExpansion>;
using DynamicViscosity = Quantity<dimensions::dynamicViscosity>;
using KinematicViscosity = Quantity<dimensions::kinematicViscosity>;

/// Stress and elastic moduli, which are pressures dimensionally.
///
/// THESE ARE ALIASES AND NOT DISTINCT TYPES, and saying so is the point. A
/// Quantity is keyed on its dimension alone, so `ElasticModulus` and `Pressure`
/// are the same type: an elastic modulus added to a pressure compiles, while an
/// elastic modulus added to a density does not. The alias buys readability in a
/// signature, never a second layer of safety, and a reader who assumed otherwise
/// would be wrong in a way that matters.
///
/// Separating them by type was considered and rejected for P15: it would need a
/// distinct type with its own arithmetic, and `sigma = E * strain` — where the
/// result IS a stress and the operand IS a modulus — would then need conversions
/// at every step, for a confusion nobody has made. If a real defect ever turns
/// on it, ADR-027's canonical set is the place to revisit.
using Stress = Pressure;
using ElasticModulus = Pressure;

/// Poisson's ratio: dimensionless, and a distinct type all the same.
///
/// It is NOT `Quantity<dimensions::dimensionless>`, because this system returns
/// a plain `double` for any dimension that cancels completely (see
/// detail::quantityOrScalar) — a dimensionless Quantity is not how BetterCAD
/// spells "a pure number".
///
/// So why a type at all? Because `double` would let Poisson's ratio be passed
/// wherever a strain, a factor or a count is wanted, and in particular wherever
/// a KINEMATIC VISCOSITY is wanted — both are written `nu`, and mistaking one
/// for the other is a mistake a compiler should catch rather than a reader. A
/// dimensionless property crossing a public engineering boundary gets a name.
///
/// It carries NO range check. `-1 < nu < 0.5` is physical validity for ordinary
/// isotropic elasticity, not dimensional validity, and P15-ARCH-001 keeps the
/// two apart: the range belongs to the material property layer (P15-MECH-001),
/// which can also decide what to do about auxetic and anisotropic cases.
class PoissonRatio {
public:
    PoissonRatio() = default;

    /// Explicit, like Quantity::fromSi: a pure number becomes a Poisson ratio
    /// only where someone says so.
    [[nodiscard]] static constexpr PoissonRatio of(double value) noexcept {
        return PoissonRatio{value};
    }

    [[nodiscard]] constexpr double value() const noexcept { return value_; }

    friend constexpr bool operator==(const PoissonRatio&, const PoissonRatio&) noexcept = default;
    friend constexpr auto operator<=>(const PoissonRatio&, const PoissonRatio&) noexcept = default;

private:
    constexpr explicit PoissonRatio(double value) noexcept : value_(value) {}

    double value_ = 0.0;
};

/// Whether @p ratio is a finite number. The same question isFinite(Quantity)
/// answers, and the same answer: a non-finite ratio is not engineering data.
[[nodiscard]] constexpr bool isFinite(PoissonRatio ratio) noexcept {
    return ratio.value() == ratio.value() && ratio.value() != std::numeric_limits<double>::infinity()
           && ratio.value() != -std::numeric_limits<double>::infinity();
}

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

// Energy
inline constexpr Unit<dimensions::energy> J{"J", {1.0, 1.0}};
inline constexpr Unit<dimensions::energy> kJ{"kJ", {1000.0, 1.0}};

// Power
inline constexpr Unit<dimensions::power> W{"W", {1.0, 1.0}};
inline constexpr Unit<dimensions::power> kW{"kW", {1000.0, 1.0}};

// Thermal conductivity
inline constexpr Unit<dimensions::thermalConductivity> W_per_m_K{"W/(m K)", {1.0, 1.0}};

// Specific heat capacity
inline constexpr Unit<dimensions::specificHeatCapacity> J_per_kg_K{"J/(kg K)", {1.0, 1.0}};
inline constexpr Unit<dimensions::specificHeatCapacity> kJ_per_kg_K{"kJ/(kg K)", {1000.0, 1.0}};

// Thermal expansion. 1 um/(m K) is 1e-6 strain per kelvin, which is how
// datasheets usually give it, so the scale is a ratio and not 1e-6 written out.
inline constexpr Unit<dimensions::thermalExpansion> per_K{"1/K", {1.0, 1.0}};
inline constexpr Unit<dimensions::thermalExpansion> um_per_m_K{"um/(m K)", {1.0, 1.0e6}};

// Viscosity
inline constexpr Unit<dimensions::dynamicViscosity> Pa_s{"Pa s", {1.0, 1.0}};
inline constexpr Unit<dimensions::dynamicViscosity> mPa_s{"mPa s", {1.0, 1000.0}};
inline constexpr Unit<dimensions::kinematicViscosity> m2_per_s{"m^2/s", {1.0, 1.0}};
inline constexpr Unit<dimensions::kinematicViscosity> mm2_per_s{"mm^2/s", {1.0, 1.0e6}};

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
