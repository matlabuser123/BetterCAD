#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/math/Direction.hpp>
#include <bettercad/features/Export.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/RevolveFeature.hpp>
#include <bettercad/sketch/Sketch.hpp>

// Evaluation of individual features from the current document state. The
// sketch geometry is used as it is; solving sketches first is the job of the
// regeneration graph (Regenerator).
namespace bettercad::features {

/// Depth of an extrude: the driving parameter's value if it has one,
/// otherwise the literal depth. The result must be a positive length.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<Length> resolveDepth(const ExtrudeDefinition& definition,
                                                                   const Document& document);

/// Computes the body of an extrude feature: the profile sketch's closed
/// regions, extruded by the resolved depth, and combined with @p target for
/// Join/Cut/Intersect.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<geometry::Body>
regenerateExtrude(const ExtrudeFeature& feature, const Document& document,
                  const geometry::Body* target = nullptr);

/// Sweep angle of a revolve: the driving parameter's value if it has one
/// (NotFound if missing, DimensionMismatch if not an angle), otherwise the
/// literal angle. The result must be in (0, 360°].
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<Angle> resolveAngle(const RevolveDefinition& definition,
                                                                  const Document& document);

/// The revolve axis in model space, taken from the profile sketch. Fails
/// with NotFound if the axis line does not exist in the sketch, and with
/// InvalidArgument if the entity is not a line or has zero length.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<Axis3D> resolveAxis(const RevolveAxis& axis,
                                                                  const sketch::Sketch& profile);

/// Computes the body of a revolve feature: the profile sketch's closed
/// regions rotated about the resolved axis through the resolved angle in the
/// chosen direction, combined with @p target for Join/Cut/Intersect.
/// Profiles that cross the axis fail with InvalidArgument.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<geometry::Body>
regenerateRevolve(const RevolveFeature& feature, const Document& document,
                  const geometry::Body* target = nullptr);

} // namespace bettercad::features
