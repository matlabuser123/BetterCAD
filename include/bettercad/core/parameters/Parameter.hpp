#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Export.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/Naming.hpp>
#include <bettercad/core/units/Quantity.hpp>
#include <bettercad/core/units/UnitCatalog.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace bettercad {

/// Display unit of dimensionless parameters (empty symbol).
inline constexpr UnitDescriptor kUnitless{"", dimensions::dimensionless, {1.0, 1.0}};

inline constexpr std::size_t kMaxParameterNameLength = kMaxNameLength;

/// Parameter names are identifiers; see validateIdentifier().
[[nodiscard]] inline Result<void> validateParameterName(std::string_view name) {
    return validateIdentifier(name, "parameter");
}

/// The catalog unit with @p symbol; the empty symbol means kUnitless.
[[nodiscard]] BETTERCAD_CORE_EXPORT Result<UnitDescriptor> resolveUnit(std::string_view symbol);

/// A named engineering value with a fixed physical dimension, e.g.
/// `width = 100 mm`.
///
/// - The value is stored in SI units; the display unit only affects how the
///   value is shown and entered.
/// - The dimension is fixed by the display unit at creation. Assigning a value
///   or display unit of another dimension fails with DimensionMismatch and
///   leaves the parameter unchanged.
/// - Values must be finite.
/// - revision() starts at 1 and increments on every effective change. Setting
///   an identical value is not a change.
/// - The optional expression is stored as text. A Parameter or ParameterTable
///   does not interpret it; a Document checks its syntax and evaluates it
///   (see bettercad/core/document/ParameterExpressions.hpp).
class BETTERCAD_CORE_EXPORT Parameter {
public:
    [[nodiscard]] static Result<Parameter> create(ParameterId id, std::string name, double siValue,
                                                  const UnitDescriptor& displayUnit);

    template <Dimension D>
    [[nodiscard]] static Result<Parameter> create(ParameterId id, std::string name,
                                                  const Quantity<D>& value, const Unit<D>& displayUnit) {
        return create(id, std::move(name), value.si(), describe(displayUnit));
    }

    /// Dimensionless parameter, e.g. a ratio or scale factor.
    [[nodiscard]] static Result<Parameter> createUnitless(ParameterId id, std::string name,
                                                          double value) {
        return create(id, std::move(name), value, kUnitless);
    }

    [[nodiscard]] ParameterId id() const noexcept { return id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] Dimension dimension() const noexcept { return displayUnit_.dimension; }
    [[nodiscard]] double siValue() const noexcept { return siValue_; }
    [[nodiscard]] const UnitDescriptor& displayUnit() const noexcept { return displayUnit_; }
    /// Value expressed in the display unit.
    [[nodiscard]] double displayValue() const noexcept { return displayUnit_.scale.fromSi(siValue_); }
    [[nodiscard]] const std::optional<std::string>& expression() const noexcept { return expression_; }
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

    /// Value as a typed quantity; fails if Q has a different dimension.
    template <QuantityType Q>
    [[nodiscard]] Result<Q> as() const {
        if (Q::dimension != dimension()) {
            return makeError(ErrorCode::DimensionMismatch, dimensionMismatchMessage(Q::dimension));
        }
        return Q::fromSi(siValue_);
    }

    // Mutators return whether the parameter changed.

    template <Dimension D>
    Result<bool> setValue(const Quantity<D>& value) {
        return setSiValue(D, value.si());
    }
    /// Sets the value from a number in @p unit, e.g. user input "2 in".
    /// The display unit is not changed.
    Result<bool> setValue(double value, const UnitDescriptor& unit);
    Result<bool> setSiValue(Dimension dimension, double siValue);
    Result<bool> setDisplayUnit(const UnitDescriptor& unit);
    /// Stores an expression's text; std::nullopt clears it. Only emptiness is
    /// checked here (see the class notes).
    Result<bool> setExpression(std::optional<std::string> expression);
    /// Validates the name syntax only; uniqueness is the owning table's job.
    Result<bool> rename(std::string name);

private:
    Parameter(ParameterId id, std::string name, double siValue, const UnitDescriptor& displayUnit);

    [[nodiscard]] std::string dimensionMismatchMessage(const Dimension& other) const;

    ParameterId id_;
    std::string name_;
    double siValue_ = 0.0;
    UnitDescriptor displayUnit_ = kUnitless;
    std::optional<std::string> expression_;
    std::uint64_t revision_ = 1;
};

/// Same identity and content (ID, name, value, unit, expression); revisions
/// are change counters and are ignored.
[[nodiscard]] BETTERCAD_CORE_EXPORT bool equivalent(const Parameter& a, const Parameter& b) noexcept;

} // namespace bettercad
