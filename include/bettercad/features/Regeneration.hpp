#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Export.hpp>

namespace bettercad::features {

/// Depth of an extrude: the driving parameter's value if it has one,
/// otherwise the literal depth. The result must be a positive length.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<Length> resolveDepth(const ExtrudeDefinition& definition,
                                                                   const Document& document);

/// Computes the body of an extrude feature from the current document state:
/// the profile sketch's closed regions, extruded by the resolved depth, and
/// combined with @p target for Join/Cut/Intersect.
///
/// The sketch geometry is used as it is; solving the sketch first is the job
/// of the regeneration graph.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<geometry::Body>
regenerateExtrude(const ExtrudeFeature& feature, const Document& document,
                  const geometry::Body* target = nullptr);

} // namespace bettercad::features
