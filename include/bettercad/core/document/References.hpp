#pragma once

#include <bettercad/core/Export.hpp>
#include <bettercad/core/Id.hpp>

#include <optional>
#include <string_view>

// References to reference geometry (P12-DATUM-001): the principal planes and
// axes of the model's coordinate system, datum planes and axes, and the
// principal planes and axes of coordinate system objects. They name document
// objects by ID only; features::resolvePlane() and resolveAxis() turn them
// into model-space geometry from the objects' definitions.
namespace bettercad {

/// A principal plane of a coordinate system, with the frames of
/// Frame3D::xy(), Frame3D::yz() and Frame3D::xz() in that system:
/// XY (X along x, normal z), YZ (X along y, normal x), XZ (X along x,
/// normal -y).
enum class PrincipalPlane {
    XY,
    YZ,
    XZ,
};

/// A principal axis of a coordinate system, through its origin.
enum class PrincipalAxis {
    X,
    Y,
    Z,
};

/// "xy", "yz", "xz".
[[nodiscard]] BETTERCAD_CORE_EXPORT std::string_view toString(PrincipalPlane plane) noexcept;
/// "x", "y", "z".
[[nodiscard]] BETTERCAD_CORE_EXPORT std::string_view toString(PrincipalAxis axis) noexcept;

/// A plane: without an object, the principal plane `plane` of the model's
/// coordinate system; with a datum plane, that plane (`plane` must then be
/// XY); with a coordinate system object, its principal plane `plane`.
struct PlaneReference {
    std::optional<ObjectId> object{};
    PrincipalPlane plane = PrincipalPlane::XY;

    friend constexpr bool operator==(const PlaneReference&, const PlaneReference&) = default;
};

/// An axis: without an object, the principal axis `axis` of the model's
/// coordinate system; with a datum axis, that axis (`axis` must then be Z);
/// with a coordinate system object, its principal axis `axis`.
struct AxisReference {
    std::optional<ObjectId> object{};
    PrincipalAxis axis = PrincipalAxis::Z;

    friend constexpr bool operator==(const AxisReference&, const AxisReference&) = default;
};

} // namespace bettercad
