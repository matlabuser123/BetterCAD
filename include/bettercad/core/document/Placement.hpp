#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Export.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/units/Units.hpp>

#include <array>
#include <optional>
#include <vector>

// Where a component sits (P13-XFORM-001, implementing ADR-005).
//
// This is placement INTENT, and intent is the only thing persisted. The
// RigidTransform3D a placement means is derived: assembly::placementOf()
// computes it from this and the parameter values in force, and nothing
// stores the result. ADR-005 rejected persisting a solved transform for the
// same reason a feature stores its inputs rather than its B-Rep -- a file
// that stored the answer could hold a position its own constraints do not
// produce.
//
// The value type lives in core, beside References.hpp, because ADR-006 put
// the reference and definition types that carry no geometry here: io and the
// CLI name a placement without depending on the assembly module that
// resolves it.
namespace bettercad {

/// How a component is placed relative to the model's coordinate system:
/// turned about the model's X, then Y, then Z axis, then moved along those
/// same axes.
///
/// The convention is deliberately the one `CoordinateSystemKind::Offset`
/// already uses for datum coordinate systems, down to the order of
/// operations, so the product has a single rotation convention rather than
/// one per subsystem:
///
/// * rotations are **extrinsic** -- each is about an axis of the model's
///   frame through its origin, not about the axes left by the previous
///   rotation, so the composed matrix is Rz * Ry * Rx;
/// * each is right-handed (counter-clockwise looking against the axis);
/// * the translation is along the model's axes, **not** the rotated ones,
///   and is applied after the rotations.
///
/// Each component of each triple is either a literal or driven by a
/// parameter, which is what makes a placement follow a design change and
/// follow the active configuration (the value in force is
/// Document::effectiveParameterValue()).
///
/// A default-constructed placement is the identity: no rotation, no
/// translation, no parameters. A component created without a placement has
/// it, and nothing is written to the file for it.
struct ComponentPlacement {
    /// Along the model's X, Y and Z; each literal or a length parameter.
    std::array<Length, 3> translation{};
    std::array<std::optional<ParameterId>, 3> translationParameters{};
    /// About the model's X, Y and Z, in that order; each literal or an angle
    /// parameter.
    std::array<Angle, 3> rotation{};
    std::array<std::optional<ParameterId>, 3> rotationParameters{};

    friend bool operator==(const ComponentPlacement&, const ComponentPlacement&) = default;
};

/// Checks a placement on its own: every literal finite. A component of the
/// triple that is driven by a parameter is not checked here, because its
/// value is not known until the parameter is read; resolution checks that.
/// InvalidArgument otherwise.
[[nodiscard]] BETTERCAD_CORE_EXPORT Result<void> validate(const ComponentPlacement& placement);

/// Whether @p placement is the identity: no rotation, no translation and no
/// parameters driving either. This is what a component has unless it is
/// given one, and what the writer omits from the file.
[[nodiscard]] BETTERCAD_CORE_EXPORT bool isIdentity(const ComponentPlacement& placement) noexcept;

/// The parameters a placement is driven by, in X, Y, Z order with the
/// translations before the rotations, without duplicates. A component
/// depends on each of them, so editing one moves the component.
[[nodiscard]] BETTERCAD_CORE_EXPORT std::vector<ParameterId> referencedParameters(const ComponentPlacement& placement);

} // namespace bettercad
