#pragma once

#include <bettercad/assembly/Export.hpp>
#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Placement.hpp>
#include <bettercad/core/math/RigidTransform.hpp>

// Turning placement intent into the transform it means (P13-XFORM-001,
// implementing ADR-005).
//
// This is the whole of the derived side, and it is deliberately a function
// rather than a store. ADR-005 makes the solved transform derived state, so
// there is nothing here to cache, invalidate or persist: ask for a
// component's transform and it is computed from the intent and the parameter
// values in force at that moment. A stale transform cannot exist because no
// transform is kept.
//
// What resolution adds over the intent is everything a ComponentPlacement
// cannot know on its own: whether its parameters exist, whether they are the
// right dimension, what they are worth under the active configuration, and
// whether the result is finite.
namespace bettercad {
class Document;
}

namespace bettercad::assembly {

/// The transform @p placement means, under the parameter values in force in
/// @p document.
///
/// Rotations are applied about the model's X, then Y, then Z axis through
/// its origin -- extrinsic, so the composed matrix is Rz * Ry * Rx -- and
/// the translation follows, along the model's own axes rather than the
/// rotated ones. That is the convention `CoordinateSystemKind::Offset`
/// already uses for datums.
///
/// Fails with NotFound if a driving parameter does not exist,
/// DimensionMismatch if one is not a length (translation) or an angle
/// (rotation), and InvalidArgument if any resolved value is not finite.
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT Result<RigidTransform3D> resolvePlacement(const Document& document,
                                                                                  const ComponentPlacement& placement);

/// The transform of the component with @p id, from its own placement.
/// NotFound if there is no component of that ID.
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT Result<RigidTransform3D> placementOf(const Document& document, ComponentId id);

} // namespace bettercad::assembly
