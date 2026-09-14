#pragma once

namespace bettercad {

/// Physical dimension as integer exponents of the base quantities.
///
/// Angle is modelled as its own base dimension rather than as SI's
/// dimensionless radian, so that engineering APIs can distinguish an angle
/// from a plain ratio. Temperature is included so that thermal quantities can
/// be added later without changing this type.
///
/// Dimension is a structural type and is used as a template argument of
/// Quantity, which makes dimensional analysis a compile-time check.
struct Dimension {
    int length = 0;
    int mass = 0;
    int time = 0;
    int temperature = 0;
    int angle = 0;

    friend constexpr bool operator==(const Dimension&, const Dimension&) = default;

    /// Dimension of the product of two quantities.
    [[nodiscard]] friend constexpr Dimension operator*(const Dimension& a,
                                                       const Dimension& b) noexcept {
        return {a.length + b.length, a.mass + b.mass, a.time + b.time,
                a.temperature + b.temperature, a.angle + b.angle};
    }

    /// Dimension of the quotient of two quantities.
    [[nodiscard]] friend constexpr Dimension operator/(const Dimension& a,
                                                       const Dimension& b) noexcept {
        return {a.length - b.length, a.mass - b.mass, a.time - b.time,
                a.temperature - b.temperature, a.angle - b.angle};
    }

    [[nodiscard]] constexpr bool isDimensionless() const noexcept { return *this == Dimension{}; }

    [[nodiscard]] constexpr Dimension inverse() const noexcept { return Dimension{} / *this; }

    /// True if the dimension has a square root with integer exponents.
    [[nodiscard]] constexpr bool hasEvenExponents() const noexcept {
        return length % 2 == 0 && mass % 2 == 0 && time % 2 == 0 && temperature % 2 == 0 &&
               angle % 2 == 0;
    }

    /// Exponents divided by two. Precondition: hasEvenExponents().
    [[nodiscard]] constexpr Dimension halved() const noexcept {
        return {length / 2, mass / 2, time / 2, temperature / 2, angle / 2};
    }
};

namespace dimensions {

inline constexpr Dimension dimensionless{};
inline constexpr Dimension length{.length = 1};
inline constexpr Dimension mass{.mass = 1};
inline constexpr Dimension time{.time = 1};
inline constexpr Dimension temperature{.temperature = 1};
inline constexpr Dimension angle{.angle = 1};

inline constexpr Dimension area = length * length;
inline constexpr Dimension volume = area * length;
inline constexpr Dimension velocity = length / time;
inline constexpr Dimension acceleration = velocity / time;
inline constexpr Dimension force = mass * acceleration;
inline constexpr Dimension pressure = force / area;
inline constexpr Dimension density = mass / volume;

} // namespace dimensions

} // namespace bettercad
