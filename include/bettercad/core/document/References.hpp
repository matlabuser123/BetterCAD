#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Export.hpp>
#include <bettercad/core/Id.hpp>

#include <compare>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

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
    /// The face swept by one entity of the profile sketch (for a sweep, along
    /// one edge of its path).
    Side,
    /// A blind hole's flat bottom (P12-SKETCH-003).
    HoleBottom,
    /// A counterbored hole's flat floor around the hole.
    CounterboreFloor,
    /// The face a chamfer cuts for one of its edge references.
    Chamfer,
    /// A spotfaced hole's flat seat around the hole (P12-HOLE-001).
    SpotfaceFloor,
};

/// "start_cap", "end_cap", "side", "hole_bottom", "counterbore_floor",
/// "chamfer", "spotface_floor".
[[nodiscard]] BETTERCAD_CORE_EXPORT std::string_view toString(FaceRole role) noexcept;

/// One step of a face's copying (P12-SKETCH-003): the pattern or mirror that
/// made the copy and the instance it belongs to (a pattern's instance index,
/// 1 for a mirror image; instance 0 is the original, which is not a copy).
struct FaceCopy {
    ObjectId feature{};
    std::uint32_t instance = 0;

    friend constexpr bool operator==(const FaceCopy&, const FaceCopy&) = default;
    friend constexpr auto operator<=>(const FaceCopy&, const FaceCopy&) = default;
};

/// One of the faces a feature generates, possibly as copied since:
/// - `role`, and for a side face the profile `entity` that sweeps it and,
///   for a sweep, the path edge it sweeps `along` and, when the path runs
///   through more than one sketch, `alongSketch`, the sketch that edge
///   belongs to (P12-SWEEP-001): entity IDs are numbered per sketch, so the
///   edge alone would not say which run it is;
/// - for a chamfer's face, the position of its edge reference in the
///   chamfer's list (`edge`, from 1);
/// - the `copies` made of it, in the order they were made (the last copy's
///   feature holds the face).
struct FaceSelector {
    FaceRole role = FaceRole::EndCap;
    std::optional<EntityId> entity{};
    std::optional<EntityId> along{};
    std::optional<SketchId> alongSketch{};
    std::optional<std::uint32_t> edge{};
    std::vector<FaceCopy> copies{};

    friend constexpr bool operator==(const FaceSelector&, const FaceSelector&) = default;
    friend constexpr auto operator<=>(const FaceSelector&, const FaceSelector&) = default;
};

/// Checks a selector on its own: a side face names a valid entity (and may
/// name a valid path edge); a chamfer face names an edge reference from 1;
/// no other role takes an entity, a path edge or an edge reference; every
/// copy names a valid feature and an instance from 1. InvalidArgument
/// otherwise.
[[nodiscard]] BETTERCAD_CORE_EXPORT Result<void> validate(const FaceSelector& selector);

/// The persistent name of a face: the feature that generates it and the
/// face's role there (with the copies made of it).
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

/// The objects a plane reference depends on: its object and, for a copied
/// face, the copying features, in order and without repeats.
[[nodiscard]] BETTERCAD_CORE_EXPORT std::vector<ObjectId> referencedObjects(const PlaneReference& reference);

/// An axis: without an object, the principal axis `axis` of the model's
/// coordinate system; with a datum axis, that axis (`axis` must then be Z);
/// with a coordinate system object, its principal axis `axis`.
struct AxisReference {
    std::optional<ObjectId> object{};
    PrincipalAxis axis = PrincipalAxis::Z;

    friend constexpr bool operator==(const AxisReference&, const AxisReference&) = default;
};

} // namespace bettercad
