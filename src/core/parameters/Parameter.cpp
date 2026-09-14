#include <bettercad/core/parameters/Parameter.hpp>
#include <bettercad/core/units/Format.hpp>

#include <cmath>
#include <format>

namespace bettercad {

namespace {

// Replaces a caller-supplied descriptor by the identical catalog entry, so the
// stored symbol refers to static storage and fabricated units are rejected.
Result<UnitDescriptor> canonicalUnit(const UnitDescriptor& unit) {
    auto known = resolveUnit(unit.symbol);
    if (!known) {
        return known;
    }
    if (*known != unit) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("unit '{}' does not match the unit catalog definition",
                                     unit.symbol));
    }
    return known;
}

Result<void> requireFinite(std::string_view name, double value) {
    if (!std::isfinite(value)) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("parameter '{}' value must be finite, got {}", name, value));
    }
    return {};
}

} // namespace

Result<UnitDescriptor> resolveUnit(std::string_view symbol) {
    if (symbol.empty()) {
        return kUnitless;
    }
    if (auto unit = findUnit(symbol)) {
        return *unit;
    }
    return makeError(ErrorCode::InvalidArgument, std::format("unknown unit '{}'", symbol));
}

Parameter::Parameter(ParameterId id, std::string name, double siValue,
                     const UnitDescriptor& displayUnit)
    : id_(id), name_(std::move(name)), siValue_(siValue), displayUnit_(displayUnit) {}

Result<Parameter> Parameter::create(ParameterId id, std::string name, double siValue,
                                    const UnitDescriptor& displayUnit) {
    if (!id.isValid()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("parameter '{}' needs a valid ID", name));
    }
    if (auto valid = validateParameterName(name); !valid) {
        return std::unexpected(valid.error());
    }
    if (auto finite = requireFinite(name, siValue); !finite) {
        return std::unexpected(finite.error());
    }
    auto unit = canonicalUnit(displayUnit);
    if (!unit) {
        return std::unexpected(unit.error());
    }
    return Parameter{id, std::move(name), siValue, *unit};
}

Result<bool> Parameter::setValue(double value, const UnitDescriptor& unit) {
    auto canonical = canonicalUnit(unit);
    if (!canonical) {
        return std::unexpected(canonical.error());
    }
    return setSiValue(canonical->dimension, canonical->scale.toSi(value));
}

Result<bool> Parameter::setSiValue(Dimension dimension, double siValue) {
    if (dimension != this->dimension()) {
        return makeError(ErrorCode::DimensionMismatch, dimensionMismatchMessage(dimension));
    }
    if (auto finite = requireFinite(name_, siValue); !finite) {
        return std::unexpected(finite.error());
    }
    if (siValue == siValue_) {
        return false;
    }
    siValue_ = siValue;
    ++revision_;
    return true;
}

Result<bool> Parameter::setDisplayUnit(const UnitDescriptor& unit) {
    auto canonical = canonicalUnit(unit);
    if (!canonical) {
        return std::unexpected(canonical.error());
    }
    if (canonical->dimension != dimension()) {
        return makeError(ErrorCode::DimensionMismatch,
                         dimensionMismatchMessage(canonical->dimension));
    }
    if (*canonical == displayUnit_) {
        return false;
    }
    displayUnit_ = *canonical;
    ++revision_;
    return true;
}

Result<bool> Parameter::setExpression(std::optional<std::string> expression) {
    if (expression && expression->empty()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("parameter '{}': an expression must not be empty", name_));
    }
    if (expression == expression_) {
        return false;
    }
    expression_ = std::move(expression);
    ++revision_;
    return true;
}

Result<bool> Parameter::rename(std::string name) {
    if (auto valid = validateParameterName(name); !valid) {
        return std::unexpected(valid.error());
    }
    if (name == name_) {
        return false;
    }
    name_ = std::move(name);
    ++revision_;
    return true;
}

std::string Parameter::dimensionMismatchMessage(const Dimension& other) const {
    return std::format("parameter '{}' has dimension {}, not {}", name_,
                       describeDimension(dimension()), describeDimension(other));
}

bool equivalent(const Parameter& a, const Parameter& b) noexcept {
    return a.id() == b.id() && a.name() == b.name() && a.siValue() == b.siValue() &&
           a.displayUnit() == b.displayUnit() && a.expression() == b.expression();
}

} // namespace bettercad
