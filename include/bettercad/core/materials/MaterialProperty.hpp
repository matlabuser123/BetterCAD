#pragma once

#include <bettercad/core/Export.hpp>
#include <bettercad/core/units/Units.hpp>

#include <compare>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

// An engineering property is Known, Unknown or Derived (ADR-027).
//
// ADR-027 fixed the three states and deferred their spelling: "The exact
// spelling belongs to P15-UNITS-001; what this ADR fixes is that three states
// exist, Unknown is representable, and nothing fabricates a default."
// P15-UNITS-001 added the quantities and left the property type alone, so this
// is where it lands (P15-MECH-001).
//
// Unknown is NOT an error and NOT a zero. Most real datasheets give density, E
// and yield and say nothing about thermal expansion, and a material
// characterised that far is a legitimate material, not a broken one.
namespace bettercad::materials {

/// What a property knows about itself.
///
/// `Derived` exists because a value computed exactly from others must never be
/// indistinguishable from one somebody measured. A derived value is never
/// stored (ADR-027): it is produced on request from the properties it comes
/// from, so there is one source of truth and nothing to disagree with.
enum class PropertyState : std::uint8_t {
    Unknown,
    Known,
    Derived,
};

/// "unknown", "known" or "derived".
[[nodiscard]] BETTERCAD_CORE_EXPORT std::string_view toString(PropertyState state) noexcept;

/// One engineering property: a value and what is known about it.
///
/// @tparam Value the property's value type -- a Quantity for anything with a
///         dimension, or PoissonRatio, Elongation or Hardness for the ones that
///         carry meaning without one.
///
/// There is deliberately no conversion to @p Value and no `valueOr()`. Both
/// would let an Unknown property become a number somewhere far from here, which
/// is the failure this type exists to prevent: `value()` returns an optional and
/// the caller has to say what to do when there is nothing in it.
template <typename Value>
class MaterialProperty {
public:
    using ValueType = Value;

    /// Nothing is known about this property. The default, so a material with no
    /// mechanical data needs no placeholders to be constructible.
    constexpr MaterialProperty() = default;

    [[nodiscard]] static constexpr MaterialProperty unknown() noexcept { return {}; }

    /// A value somebody supplied.
    [[nodiscard]] static constexpr MaterialProperty known(Value value) noexcept {
        return MaterialProperty{PropertyState::Known, std::move(value)};
    }

    /// A value computed from other properties. Made only by the code that does
    /// the computing; there is no setter that stores one.
    [[nodiscard]] static constexpr MaterialProperty derived(Value value) noexcept {
        return MaterialProperty{PropertyState::Derived, std::move(value)};
    }

    [[nodiscard]] constexpr PropertyState state() const noexcept { return state_; }
    [[nodiscard]] constexpr bool isUnknown() const noexcept {
        return state_ == PropertyState::Unknown;
    }
    [[nodiscard]] constexpr bool isKnown() const noexcept { return state_ == PropertyState::Known; }
    [[nodiscard]] constexpr bool isDerived() const noexcept {
        return state_ == PropertyState::Derived;
    }
    /// Known or Derived: there is a number to read.
    [[nodiscard]] constexpr bool hasValue() const noexcept { return !isUnknown(); }

    /// The value, or std::nullopt when nothing is known. An Unknown property is
    /// empty here -- never zero.
    [[nodiscard]] constexpr std::optional<Value> value() const noexcept {
        if (isUnknown()) {
            return std::nullopt;
        }
        return value_;
    }

    friend constexpr bool operator==(const MaterialProperty&, const MaterialProperty&) = default;

private:
    constexpr MaterialProperty(PropertyState state, Value value) noexcept
        : value_(std::move(value)), state_(state) {}

    Value value_{};
    PropertyState state_ = PropertyState::Unknown;
};

/// Elongation at break, as a FRACTION: 0.12 is 12 %.
///
/// The representation is fixed here rather than left to a caller, because "12"
/// meaning both 12 % and 1200 % is exactly the ambiguity that puts a factor of
/// 100 into an engineering result. A fraction is what BetterCAD stores, matching
/// the rule that values are SI internally and converted only at the boundary;
/// `percent()` is that boundary.
///
/// A distinct type for the reason PoissonRatio is one: a bare `double` would
/// pass wherever a ratio, a factor or a Poisson ratio is wanted.
class Elongation {
public:
    constexpr Elongation() = default;

    /// From a fraction: `Elongation::of(0.12)` is 12 %.
    [[nodiscard]] static constexpr Elongation of(double fraction) noexcept {
        return Elongation{fraction};
    }
    /// From a percentage: `Elongation::ofPercent(12.0)` is 12 %.
    [[nodiscard]] static constexpr Elongation ofPercent(double percent) noexcept {
        return Elongation{percent / 100.0};
    }

    [[nodiscard]] constexpr double value() const noexcept { return fraction_; }
    [[nodiscard]] constexpr double percent() const noexcept { return fraction_ * 100.0; }

    friend constexpr bool operator==(const Elongation&, const Elongation&) noexcept = default;
    friend constexpr auto operator<=>(const Elongation&, const Elongation&) noexcept = default;

private:
    constexpr explicit Elongation(double fraction) noexcept : fraction_(fraction) {}

    double fraction_ = 0.0;
};

/// Whether @p elongation is a finite number.
[[nodiscard]] constexpr bool isFinite(Elongation elongation) noexcept {
    const double value = elongation.value();
    return value == value && value != std::numeric_limits<double>::infinity()
           && value != -std::numeric_limits<double>::infinity();
}

/// The hardness scale a number was measured on.
///
/// Only the scales BetterCAD can name are here. There is no `Other` and no free
/// text: a hardness whose scale is unknown is a number without meaning, and
/// `MaterialProperty<Hardness>::unknown()` is how a material says it has no
/// hardness.
enum class HardnessScale : std::uint8_t {
    Brinell,
    Vickers,
    RockwellB,
    RockwellC,
};

/// "HBW", "HV", "HRB", "HRC".
[[nodiscard]] BETTERCAD_CORE_EXPORT std::string_view toString(HardnessScale scale) noexcept;

/// A hardness: a number AND the scale it was measured on.
///
/// The scale is part of the value, not a label beside it. 60 HRC and 60 HRB are
/// different hardnesses of different materials, so they compare unequal, and a
/// number on its own cannot be constructed.
///
/// Hardness is deliberately not a Pressure. Some indentation tests are defined
/// as a force over an area and their numbers are quoted without units by
/// convention; treating an HRC number as a pressure would make it arithmetically
/// comparable with a yield strength, which it is not.
///
/// No conversion between scales is offered. Published correlations exist, are
/// approximate, and are material-dependent; none is applied without a verified
/// source and a milestone that asks for it.
class Hardness {
public:
    constexpr Hardness() = default;

    [[nodiscard]] static constexpr Hardness of(double value, HardnessScale scale) noexcept {
        return Hardness{value, scale};
    }

    [[nodiscard]] constexpr double value() const noexcept { return value_; }
    [[nodiscard]] constexpr HardnessScale scale() const noexcept { return scale_; }

    /// Equal only if BOTH the number and the scale are equal.
    friend constexpr bool operator==(const Hardness&, const Hardness&) noexcept = default;

private:
    constexpr Hardness(double value, HardnessScale scale) noexcept
        : value_(value), scale_(scale) {}

    double value_ = 0.0;
    HardnessScale scale_ = HardnessScale::Brinell;
};

/// Whether @p hardness carries a finite number.
[[nodiscard]] constexpr bool isFinite(Hardness hardness) noexcept {
    const double value = hardness.value();
    return value == value && value != std::numeric_limits<double>::infinity()
           && value != -std::numeric_limits<double>::infinity();
}

/// "60 HRC", "95 HRB".
[[nodiscard]] BETTERCAD_CORE_EXPORT std::string toString(Hardness hardness);

} // namespace bettercad::materials
