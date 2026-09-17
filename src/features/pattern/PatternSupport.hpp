#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/math/RigidTransform.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/features/CircularPatternFeature.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// What linear and circular patterns share: how the source's operation is
// repeated, how the instances are built (atomically, in order), and how a
// driven count is resolved.
namespace bettercad::features::detail {

/// Applies the source's operation, moved by a rigid motion, to a body; the
/// faces it makes are named as copies (P12-SKETCH-003) by the given chain of
/// steps, in the order they were made. A pattern of a pattern passes the
/// inner pattern's step first, then the outer one's (P12-PATTERN-001).
using InstanceOperation = std::function<Result<geometry::Body>(const geometry::Body&, const RigidTransform3D&,
                                                               const std::vector<FaceCopy>&)>;

/// The operation of @p source, resolved once and repeated at each instance:
/// an extrude or revolve's tool is moved and united (new body, join) or
/// subtracted (cut), except that a through-all cut's tool is rebuilt at each
/// instance to reach through the body there (P12-FEAT-001; the operation
/// then refers to @p source and @p document, and is used while they live);
/// a hole, chamfer or fillet is applied with its face or edge references
/// moved exactly, with all of its own checks; a pattern repeats every one of
/// its own instances, moved with it (P12-PATTERN-001), and refers to
/// @p document while it lives. Intersect sources are refused; @p pattern
/// names the pattern kind in messages ("linear pattern").
[[nodiscard]] Result<InstanceOperation> instanceOperation(const DocumentObject& source, const Document& document,
                                                          std::string_view pattern);

/// How many instances @p source itself makes, if it is a pattern, with its
/// suppressed instances left out. 1 for anything else (P12-PATTERN-001).
[[nodiscard]] Result<std::size_t> instanceCountOf(const DocumentObject& source, const Document& document);

/// Rejects a pattern of @p instances instances whose source is a pattern of
/// its own, when the product exceeds the cap: each of our instances makes
/// every instance of the source again, so the work is the product
/// (P12-PATTERN-001). @p pattern names the pattern kind in the message.
[[nodiscard]] Result<void> checkNestedCount(const DocumentObject& source, const Document& document,
                                            std::size_t instances, std::string_view pattern);

/// One instance to build: where it goes, how messages name it (e.g.
/// "instance 5 at (100, 0, 0) mm") and its index (from 1; the source is 0).
struct PatternPlacement {
    RigidTransform3D motion{};
    std::string label;
    std::uint32_t instance = 1;
};

/// The pattern body: @p sourceBody (instance 0) with @p apply repeated at
/// every placement, in order, each instance's faces named as copies by
/// @p pattern. The first failure fails the whole pattern, with the
/// placement's label in front of the reason; no partial result is ever
/// returned. The result must be a valid body with a finite, positive volume.
[[nodiscard]] Result<geometry::Body> buildPattern(const geometry::Body& sourceBody, const InstanceOperation& apply,
                                                  const std::vector<PatternPlacement>& placements, ObjectId pattern);

/// A pattern's count: @p literal, or the value of a dimensionless
/// parameter, which must be a whole number from 1 to kMaxPatternInstances
/// (InvalidArgument; DimensionMismatch if the parameter is not
/// dimensionless). @p prefix starts every message (e.g. "direction 1: ").
[[nodiscard]] Result<std::size_t> resolvePatternCount(std::uint32_t literal, const std::optional<ParameterId>& parameter,
                                                      const Document& document, std::string_view prefix);

/// The angle rules of a circular pattern (InvalidArgument otherwise): an
/// included angle or angle step that is positive, finite and below 360°,
/// and, when @p count is known, (count - 1) angle steps below 360°, so no
/// instance reaches the source again. A full circle takes any angle (it has
/// none). Angles within 1e-9 rad of 360° count as 360°.
[[nodiscard]] Result<void> checkCircularAngle(CircularSpacing spacing, std::optional<std::size_t> count, Angle angle);

} // namespace bettercad::features::detail
