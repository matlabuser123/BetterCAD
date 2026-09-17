#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/geometry/Export.hpp>
#include <bettercad/core/geometry/Sweeps.hpp>
#include <bettercad/core/units/Units.hpp>

#include <string_view>

// Ribs: walls that fill the space between an open profile and a body
// (P12-FEAT-005).
namespace bettercad::geometry {

/// Where a rib's thickness lies: centred on the profile's plane, or all on
/// the side its normal points to (AlongNormal) or on the other side.
enum class RibPlacement {
    Symmetric,
    AlongNormal,
    AgainstNormal,
};

/// "symmetric", "along normal", "against normal".
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT std::string_view toString(RibPlacement placement) noexcept;

/// A rib: the region on one side of `profile`, closed off by the body, as a
/// wall `thickness` thick.
struct RibRequest {
    /// An open chain of lines, arcs and open splines in the plane's local
    /// coordinates, head to tail, in the order of travel. (Not a closed
    /// path, and no full circles, ellipses or periodic splines.)
    PlanarPath profile{};
    Length thickness{};
    RibPlacement placement = RibPlacement::Symmetric;
    /// The rib fills the region to the left of the profile's direction of
    /// travel (seen with the plane's normal towards the viewer), or to its
    /// right when flipped.
    bool flipped = false;

    friend bool operator==(const RibRequest&, const RibRequest&) = default;
};

/// Checks the parts of a request that do not depend on a body. Fails with
/// InvalidArgument for an empty, closed, disconnected or degenerate profile
/// (segments are named from 1), a closed segment in it, an invalid spline,
/// a thickness that is not positive and finite, or an unknown placement.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<void> validate(const RibRequest& request);

/// @p body with the rib joined to it; @p body is not modified.
///
/// The profile is extended at both ends along its end tangents to a
/// rectangle in its plane that reaches 1 mm beyond the body's and the
/// profile's bounds, and closed along that rectangle on the side the rib
/// fills. That region, as a prism of the rib's thickness, less the body,
/// falls into pieces: the rib is every piece the profile bounds. A profile
/// that ends short of the body is thereby extended to it. Each piece must
/// be closed off by the body: one that reaches the rectangle means the side
/// is open. The pieces are joined to the body; the result keeps the body's
/// number of solids, and its volume is the body's plus the pieces'
/// (within 1e-9 relative).
///
/// Errors:
/// - InvalidArgument: see validate().
/// - FailedPrecondition: the body is empty; the extended profile crosses
///   itself; the side the rib fills is not closed off by the body; the
///   profile lies wholly inside the body, so nothing is filled; the body
///   covers the whole region.
/// - Internal: the kernel failed unexpectedly, or the joined result is not
///   valid, has another number of solids or loses volume.
///
/// The result carries the names of @p body's faces. @p namer names the
/// rib's own faces: the side each profile segment makes (SweptFace::Side,
/// with the segment's index), the wall face at the lower offset (First) and
/// the one at the higher (Last). The faces made by the extensions and the
/// faces the rib takes from the body are not named.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<Body> addRib(const Body& body, const RibRequest& request,
                                                           const SweptFaceNamer& namer = {});

} // namespace bettercad::geometry
