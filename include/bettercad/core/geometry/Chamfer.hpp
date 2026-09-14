#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/geometry/Export.hpp>
#include <bettercad/core/math/Direction.hpp>
#include <bettercad/core/units/Units.hpp>

#include <optional>
#include <string_view>
#include <vector>

namespace bettercad::geometry {

enum class ChamferMode {
    /// The same distance on both faces of each edge.
    EqualDistance,
    /// `distance` on the reference face, `distance2` on the other face.
    TwoDistance,
    /// `distance` on the reference face; the chamfer face makes `angle`
    /// with the reference face.
    DistanceAngle,
};

/// "equal distance", "two distances" or "distance and angle".
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT std::string_view toString(ChamferMode mode) noexcept;

struct ChamferRequest {
    /// The edges to chamfer; each must match exactly one edge of the body.
    std::vector<EdgeSignature> edges{};
    ChamferMode mode = ChamferMode::EqualDistance;
    /// EqualDistance: on both faces. Otherwise: on the reference face.
    Length distance{};
    /// TwoDistance only: on the other face.
    Length distance2{};
    /// DistanceAngle only: in (0, 90°).
    Angle angle{};
    /// TwoDistance and DistanceAngle: of the two faces at an edge, the
    /// reference face is the one whose outward normal (at the middle of the
    /// edge) is closer to this direction.
    std::optional<Direction3D> referenceSide{};

    friend bool operator==(const ChamferRequest&, const ChamferRequest&) = default;
};

/// Checks the parts of a request that do not depend on a body. Fails with
/// InvalidArgument for:
/// - no edges, or an invalid or duplicate signature;
/// - a distance that is not positive and finite, or an angle outside (0, 90°);
/// - a missing or superfluous reference side.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<void> validate(const ChamferRequest& request);

/// Replaces edges of @p body with flat chamfer faces; @p body is not
/// modified. Each reference must match exactly one edge that lies between
/// two faces. The kernel extends a chamfer along edges tangent to it, so a
/// smooth chain of edges is chamfered as a whole. The result must be valid,
/// have the same number of solids and a finite, positive volume.
///
/// Before the kernel runs, every chamfer must fit, by at least 0.001 mm:
/// on each face next to a chamfered edge, the chamfer must stay clear of the
/// face's other edges (those not meeting the chamfered edge), must not run
/// across the face, and must not meet another chamfer on that face. (The
/// kernel cannot be trusted with chamfers that do not fit; see
/// docs/verification/P11-FEAT-002.) The check measures straight-line
/// distances, so on curved faces it may refuse a chamfer that would just fit.
///
/// Errors:
/// - InvalidArgument: see validate(); also two references to the same
///   chain of edges.
/// - NotFound: a reference matches no edge.
/// - FailedPrecondition: a reference is ambiguous, an edge is not between
///   two faces, the reference side cannot tell the faces apart, a chamfer
///   does not fit (e.g. a distance larger than a face allows), or the
///   kernel still cannot build the chamfer.
/// - Internal: the kernel produced an invalid result, or could not measure
///   the room around an edge.
///
/// Messages name the reference by its position in the request and its
/// curve.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<Body> chamferEdges(const Body& body, const ChamferRequest& request);

} // namespace bettercad::geometry
