#include <bettercad/structural/StructuralData.hpp>

#include <cmath>

namespace bettercad::structural {

std::string_view toString(DofComponent component) noexcept {
    switch (component) {
    case DofComponent::Ux:
        return "ux";
    case DofComponent::Uy:
        return "uy";
    case DofComponent::Uz:
        return "uz";
    }
    return "unknown";
}

std::string_view toString(TensorComponent component) noexcept {
    switch (component) {
    case TensorComponent::XX:
        return "xx";
    case TensorComponent::YY:
        return "yy";
    case TensorComponent::ZZ:
        return "zz";
    case TensorComponent::XY:
        return "xy";
    case TensorComponent::YZ:
        return "yz";
    case TensorComponent::ZX:
        return "zx";
    }
    return "unknown";
}

bool isFinite(const Strain6& strain) noexcept {
    return std::isfinite(strain.xx) && std::isfinite(strain.yy) && std::isfinite(strain.zz) &&
           std::isfinite(strain.gammaXy) && std::isfinite(strain.gammaYz) &&
           std::isfinite(strain.gammaZx);
}

bool isFinite(const Stress6& stress) noexcept {
    return bettercad::isFinite(stress.xx) && bettercad::isFinite(stress.yy) &&
           bettercad::isFinite(stress.zz) && bettercad::isFinite(stress.xy) &&
           bettercad::isFinite(stress.yz) && bettercad::isFinite(stress.zx);
}

} // namespace bettercad::structural
