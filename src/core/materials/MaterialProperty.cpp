#include <bettercad/core/materials/MaterialProperty.hpp>

#include <bettercad/core/units/Format.hpp>

#include <format>
#include <optional>
#include <string>

namespace bettercad::materials {

std::string_view toString(PropertyState state) noexcept {
    switch (state) {
    case PropertyState::Unknown:
        return "unknown";
    case PropertyState::Known:
        return "known";
    case PropertyState::Derived:
        return "derived";
    }
    return "unknown";
}

std::string_view toString(HardnessScale scale) noexcept {
    switch (scale) {
    case HardnessScale::Brinell:
        return "HBW";
    case HardnessScale::Vickers:
        return "HV";
    case HardnessScale::RockwellB:
        return "HRB";
    case HardnessScale::RockwellC:
        return "HRC";
    }
    return "HBW";
}

std::optional<std::string> referenceTemperatureProblem(const std::optional<Temperature>& at,
                                                      std::string_view name) {
    if (!at) {
        return std::nullopt;
    }
    if (!isFinite(*at)) {
        return std::format("the reference temperature of {} is not a finite value", name);
    }
    // An ABSOLUTE thermodynamic temperature. Zero kelvin and below are not
    // temperatures a measurement was made at.
    if (at->si() <= 0.0) {
        return std::format("the reference temperature of {} must be above absolute zero, not {}",
                           name, toString(*at));
    }
    return std::nullopt;
}

std::string toString(ElectricalResistivity resistivity) {
    return std::format("{} Ohm m", resistivity.ohmMetres());
}

std::string toString(Hardness hardness) {
    // The scale is never omitted. A hardness printed as a bare number is a
    // number whose meaning has been thrown away.
    return std::format("{} {}", hardness.value(), toString(hardness.scale()));
}

} // namespace bettercad::materials
