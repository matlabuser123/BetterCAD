#include <bettercad/core/materials/MaterialProperty.hpp>

#include <format>

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

std::string toString(Hardness hardness) {
    // The scale is never omitted. A hardness printed as a bare number is a
    // number whose meaning has been thrown away.
    return std::format("{} {}", hardness.value(), toString(hardness.scale()));
}

} // namespace bettercad::materials
