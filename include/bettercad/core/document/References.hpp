#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Export.hpp>
#include <bettercad/core/Id.hpp>

#include <compare>
#include <optional>
#include <string_view>

// References to reference geometry (P12-DATUM-001): the principal planes and
// axes of the model's coordinate system, datum planes and axes, and the
// principal planes and axes of coordinate system objects. They name document
// objects by ID only; features::resolvePlane() and resolveAxis() turn them
// into model-space geometry from the objects' definitions.
//
// Faces of features (P12-STREF-001) are named by the feature that generates
// them and their role in it, never by a kernel face, its index or its
// address. Features attach these names to the faces of their bodies, and
// resolution looks for the name, with no geometric fallback.
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

/// The role of a face in the feature that generates it.
enum class FaceRole {
    /// Where the feature's sweep starts: an extrude's face on its sketch
    /// plane (for a symmetric extrude, the face behind the plane).
    StartCap,
    /// Where it ends: an extrude's face at its depth.
    EndCap,
    /// The face swept by one entity of the profile sketch.
    Side,
};

/// "start_cap", "end_cap", "side".
[[nodiscard]] BETTERCAD_CORE_EXPORT std::string_view toString(FaceRole role) noexcept;

/// One of the faces a feature generates: its role and, for a side face, the
/// profile entity that sweeps it.
struct FaceSelector {
    FaceRole role = FaceRole::EndCap;
    std::optional<EntityId> entity{};

    friend constexpr bool operator==(const FaceSelector&, const FaceSelector&) = default;
    friend constexpr auto operator<=>(const FaceSelector&, const FaceSelector&) = default;
};

/// Checks a selector: a side face names a valid entity, a cap none.
/// InvalidArgument otherwise.
[[nodiscard]] BETTERCAD_CORE_EXPORT Result<void> validate(const FaceSelector& selector);

/// The persistent name of a face: the feature that generates it and the
/// face's role there.
struct FaceName {
    ObjectId feature{};
    FaceSelector face{};

    friend constexpr bool operator==(const FaceName&, const FaceName&) = default;
    friend constexpr auto operator<=>(const FaceName&, const FaceName&) = default;
};

/// A plane: without an object, the principal plane `plane` of the model's
/// coordinate system; with a datum plane, that plane (`plane` must then be
/// XY); with a coordinate system object, its principal plane `plane`; with a
/// feature and `face`, the plane of that face of the feature's body, facing
/// out of the material (`plane` must then be XY).
struct PlaneReference {
    std::optional<ObjectId> object{};
    PrincipalPlane plane = PrincipalPlane::XY;
    std::optional<FaceSelector> face{};

    friend constexpr bool operator==(const PlaneReference&, const PlaneReference&) = default;
};

/// Checks a reference on its own: a valid object ID; a face only with an
/// object, a valid selector and the default plane. InvalidArgument
/// otherwise. Whether the objects exist and are of the right kind is
/// checked when the reference is resolved.
[[nodiscard]] BETTERCAD_CORE_EXPORT Result<void> validate(const PlaneReference& reference);

/// An axis: without an object, the principal axis `axis` of the model's
/// coordinate system; with a datum axis, that axis (`axis` must then be Z);
/// with a coordinate system object, its principal axis `axis`.
struct AxisReference {
    std::optional<ObjectId> object{};
    PrincipalAxis axis = PrincipalAxis::Z;

    friend constexpr bool operator==(const AxisReference&, const AxisReference&) = default;
};

} // namespace bettercad
