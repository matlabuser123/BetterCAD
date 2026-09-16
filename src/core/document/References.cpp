#include <bettercad/core/document/References.hpp>

namespace bettercad {

std::string_view toString(PrincipalPlane plane) noexcept {
    switch (plane) {
    case PrincipalPlane::XY:
        return "xy";
    case PrincipalPlane::YZ:
        return "yz";
    case PrincipalPlane::XZ:
        return "xz";
    }
    return "unknown";
}

std::string_view toString(PrincipalAxis axis) noexcept {
    switch (axis) {
    case PrincipalAxis::X:
        return "x";
    case PrincipalAxis::Y:
        return "y";
    case PrincipalAxis::Z:
        return "z";
    }
    return "unknown";
}

} // namespace bettercad
