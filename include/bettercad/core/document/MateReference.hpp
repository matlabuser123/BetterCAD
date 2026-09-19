#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Export.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/References.hpp>

#include <optional>
#include <string_view>
#include <vector>

// What a mate is allowed to point at (P13-MATE-001, implementing ADR-004).
//
// ADR-004 settled this before any mate code existed, on purpose, and states
// the rule in one line:
//
//     A mate may reference anything that moves with the model, and nothing
//     that merely sits where the model used to be.
//
// So a target names geometry that is *semantic* -- a datum, a coordinate
// system's plane or axis, or a face named by the feature that generated it.
// A geometry::FaceSignature is refused, because it matches a plane in model
// space and stops matching the moment that plane moves. P12-REF-001 measured
// what that costs in a part; in an assembly it would break every assembly
// that instances the part, far from the parameter that caused it.
//
// These kinds live in core, beside References.hpp, because ADR-006 puts the
// reference vocabulary here: io and the CLI name a mate target without
// depending on the module that constrains it.
namespace bettercad {

/// Which kind of geometry a target names.
enum class MateTargetKind {
    /// A principal plane, a datum plane, a coordinate system's plane, or the
    /// plane of a named face.
    Plane,
    /// A principal axis, a datum axis, or a coordinate system's axis.
    Axis,
    /// A face named by the feature that generated it.
    Face,
};

/// "plane", "axis", "face".
[[nodiscard]] BETTERCAD_CORE_EXPORT std::string_view toString(MateTargetKind kind) noexcept;

/// One side of a mate: geometry, on a component.
///
/// `component` says which instance the geometry belongs to. Two components of
/// one part are different targets even when they name the same geometry of
/// that part -- which is the whole reason a mate needs an instance and not
/// just a reference.
///
/// Exactly one of `plane`, `axis` and `face` is set, and `kind` says which.
/// The redundancy is deliberate: `kind` is what validation, persistence and
/// diagnostics switch on, so a malformed target is a refused value rather
/// than a silently empty one.
struct MateTarget {
    ComponentId component{};
    MateTargetKind kind = MateTargetKind::Plane;
    std::optional<PlaneReference> plane{};
    std::optional<AxisReference> axis{};
    std::optional<FaceName> face{};

    friend bool operator==(const MateTarget&, const MateTarget&) = default;
};

/// A target naming the plane @p reference on @p component.
[[nodiscard]] BETTERCAD_CORE_EXPORT MateTarget planeTarget(ComponentId component, const PlaneReference& reference);
/// A target naming the axis @p reference on @p component.
[[nodiscard]] BETTERCAD_CORE_EXPORT MateTarget axisTarget(ComponentId component, const AxisReference& reference);
/// A target naming the face @p name on @p component.
[[nodiscard]] BETTERCAD_CORE_EXPORT MateTarget faceTarget(ComponentId component, const FaceName& name);

/// Checks a target on its own: a valid component, exactly the one reference
/// its kind calls for, and that reference self-consistent. InvalidArgument
/// otherwise.
///
/// Whether the component exists, whether the geometry resolves, and whether
/// the geometry belongs to that component's part are checked against the
/// document, not here.
[[nodiscard]] BETTERCAD_CORE_EXPORT Result<void> validate(const MateTarget& target);

/// Whether @p target names something with a direction: a plane or a face
/// through its normal, an axis through its direction.
///
/// Every kind qualifies today. The function exists so the mates that need an
/// orientation say so, rather than assuming it of whatever they are given.
[[nodiscard]] BETTERCAD_CORE_EXPORT bool hasDirection(const MateTarget& target) noexcept;

/// Whether @p target names a plane or a face, both of which a mate treats as
/// a plane (a face through the plane it lies in).
[[nodiscard]] BETTERCAD_CORE_EXPORT bool isPlanar(const MateTarget& target) noexcept;

/// The objects @p target depends on: its component, and the objects its
/// geometry reference names, in order and without repeats.
///
/// This is what makes a mate rebuild when the datum it uses moves, which
/// ADR-004 requires of it.
[[nodiscard]] BETTERCAD_CORE_EXPORT std::vector<ObjectId> referencedObjects(const MateTarget& target);

} // namespace bettercad
