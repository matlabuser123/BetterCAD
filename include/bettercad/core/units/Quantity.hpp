#pragma once

#include <bettercad/core/units/Dimension.hpp>

#include <cmath>
#include <compare>
#include <string_view>
#include <type_traits>

namespace bettercad {

/// Exact conversion factor of a unit: one unit equals numerator/denominator
/// coherent SI units. A ratio (rather than a single factor) keeps decimal
/// conversions such as mm = 1/1000 m to a single, correctly rounded operation.
struct UnitScale {
    double numerator = 1.0;
    double denominator = 1.0;

    [[nodiscard]] constexpr double toSi(double value) const noexcept {
        return value * numerator / denominator;
    }
    [[nodiscard]] constexpr double fromSi(double siValue) const noexcept {
        return siValue * denominator / numerator;
    }

    friend constexpr bool operator==(const UnitScale&, const UnitScale&) = default;
};

/// A unit of measurement of dimension D, e.g. millimetre for length.
template <Dimension D>
struct Unit {
    std::string_view symbol;
    UnitScale scale;
};

/// A physical quantity of dimension D.
///
/// The value is stored in coherent SI units (m, kg, s, K, rad). Construct it
/// from a unit (`2.0 * units::mm`), a literal (`2_mm`) or explicitly from an
/// SI value (`Length::fromSi(0.002)`); there is deliberately no implicit
/// conversion from or to plain `double`.
///
/// Addition, subtraction and comparison require identical dimensions.
/// Multiplication and division derive the resulting dimension; a result
/// whose dimensions cancel completely is returned as `double`.
template <Dimension D>
class Quantity {
public:
    static constexpr Dimension dimension = D;

    /// Zero.
    constexpr Quantity() noexcept = default;

    [[nodiscard]] static constexpr Quantity fromSi(double siValue) noexcept {
        Quantity q;
        q.value_ = siValue;
        return q;
    }

    [[nodiscard]] static constexpr Quantity zero() noexcept { return Quantity{}; }

    /// Value in coherent SI units.
    [[nodiscard]] constexpr double si() const noexcept { return value_; }

    /// Value expressed in @p unit.
    [[nodiscard]] constexpr double in(const Unit<D>& unit) const noexcept {
        return unit.scale.fromSi(value_);
    }

    constexpr Quantity& operator+=(const Quantity& rhs) noexcept {
        value_ += rhs.value_;
        return *this;
    }
    constexpr Quantity& operator-=(const Quantity& rhs) noexcept {
        value_ -= rhs.value_;
        return *this;
    }
    constexpr Quantity& operator*=(double factor) noexcept {
        value_ *= factor;
        return *this;
    }
    constexpr Quantity& operator/=(double divisor) noexcept {
        value_ /= divisor;
        return *this;
    }

    // Same-dimension operators are hidden friends: they take part in overload
    // resolution only for this exact dimension, so mixing dimensions finds no
    // viable operator and fails to compile.
    [[nodiscard]] friend constexpr Quantity operator+(const Quantity& q) noexcept { return q; }
    [[nodiscard]] friend constexpr Quantity operator-(const Quantity& q) noexcept {
        return fromSi(-q.value_);
    }
    [[nodiscard]] friend constexpr Quantity operator+(const Quantity& a, const Quantity& b) noexcept {
        return fromSi(a.value_ + b.value_);
    }
    [[nodiscard]] friend constexpr Quantity operator-(const Quantity& a, const Quantity& b) noexcept {
        return fromSi(a.value_ - b.value_);
    }
    [[nodiscard]] friend constexpr Quantity operator*(const Quantity& q, double factor) noexcept {
        return fromSi(q.value_ * factor);
    }
    [[nodiscard]] friend constexpr Quantity operator*(double factor, const Quantity& q) noexcept {
        return fromSi(factor * q.value_);
    }
    [[nodiscard]] friend constexpr Quantity operator/(const Quantity& q, double divisor) noexcept {
        return fromSi(q.value_ / divisor);
    }

    friend constexpr bool operator==(const Quantity&, const Quantity&) noexcept = default;
    friend constexpr auto operator<=>(const Quantity&, const Quantity&) noexcept = default;

private:
    double value_ = 0.0;
};

template <typename T>
struct IsQuantity : std::false_type {};

template <Dimension D>
struct IsQuantity<Quantity<D>> : std::true_type {};

/// Any Quantity<D>.
template <typename T>
concept QuantityType = IsQuantity<std::remove_cvref_t<T>>::value;

/// `value` expressed in `unit`, e.g. `2.5 * units::MPa`.
template <Dimension D>
[[nodiscard]] constexpr Quantity<D> operator*(double value, const Unit<D>& unit) noexcept {
    return Quantity<D>::fromSi(unit.scale.toSi(value));
}

namespace detail {

template <Dimension D>
[[nodiscard]] constexpr auto quantityOrScalar(double siValue) noexcept {
    if constexpr (D.isDimensionless()) {
        return siValue;
    } else {
        return Quantity<D>::fromSi(siValue);
    }
}

} // namespace detail

template <Dimension A, Dimension B>
[[nodiscard]] constexpr auto operator*(const Quantity<A>& a, const Quantity<B>& b) noexcept {
    return detail::quantityOrScalar<A * B>(a.si() * b.si());
}

template <Dimension A, Dimension B>
[[nodiscard]] constexpr auto operator/(const Quantity<A>& a, const Quantity<B>& b) noexcept {
    return detail::quantityOrScalar<A / B>(a.si() / b.si());
}

template <Dimension D>
[[nodiscard]] constexpr Quantity<D.inverse()> operator/(double value,
                                                        const Quantity<D>& q) noexcept {
    return Quantity<D.inverse()>::fromSi(value / q.si());
}

template <Dimension D>
[[nodiscard]] Quantity<D> abs(const Quantity<D>& q) noexcept {
    return Quantity<D>::fromSi(std::fabs(q.si()));
}

/// Square root; only defined when every exponent of D is even (e.g. area).
template <Dimension D>
    requires(D.hasEvenExponents())
[[nodiscard]] Quantity<D.halved()> sqrt(const Quantity<D>& q) noexcept {
    return Quantity<D.halved()>::fromSi(std::sqrt(q.si()));
}

template <Dimension D>
[[nodiscard]] bool isFinite(const Quantity<D>& q) noexcept {
    return std::isfinite(q.si());
}

/// True if |a - b| <= tolerance.
template <Dimension D>
[[nodiscard]] bool approxEqual(const Quantity<D>& a, const Quantity<D>& b,
                               const Quantity<D>& tolerance) noexcept {
    return std::fabs(a.si() - b.si()) <= tolerance.si();
}

} // namespace bettercad
