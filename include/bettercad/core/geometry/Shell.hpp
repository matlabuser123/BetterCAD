#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/geometry/Export.hpp>
#include <bettercad/core/units/Units.hpp>

#include <string_view>
#include <vector>

// Hollowing a solid into walls (P12-FEAT-003).
namespace bettercad::geometry {

/// Where the walls of a shell lie: inside the body's faces, so its outside
/// stays (Inward), or outside them, so its inside stays (Outward).
enum class ShellSide {
    Inward,
    Outward,
};

/// "inward", "outward".
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT std::string_view toString(ShellSide side) noexcept;

/// A shell: the body hollowed into walls of `thickness`, open where the
/// faces named `openFaces` are removed.
struct ShellRequest {
    /// The faces to remove, by name (see findNamedFaces()). A name several
    /// faces carry (a face a cut divided) removes all of them.
    std::vector<FaceName> openFaces{};
    Length thickness{};
    ShellSide side = ShellSide::Inward;

    friend bool operator==(const ShellRequest&, const ShellRequest&) = default;
};

/// Checks the parts of a request that do not depend on a body. Fails with
/// InvalidArgument for no open faces, an open face with an invalid feature
/// or selector, a repeated open face, a thickness that is not positive and
/// finite, or an unknown side.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<void> validate(const ShellRequest& request);

/// @p body hollowed as @p request says; @p body is not modified. The walls
/// are the remaining faces offset by the thickness to the chosen side.
/// Where two offset faces part at an edge (a concave edge inward, a convex
/// one outward) both are extended until they meet, so the wall's corners
/// are sharp and thicker across the corner than the thickness; where they
/// meet smoothly (a round) the offset stays smooth, a round of radius r
/// becoming r - t or r + t.
///
/// The kernel's result is checked, not trusted: OCCT 8.0.1 returns walls
/// that are too thick for the body as the unchanged body, or as an invalid
/// solid, while reporting success (docs/verification/P12-FEAT-003). A shell
/// is accepted only as one valid solid in which every remaining face has
/// its wall (a face the kernel generated from it), no open face is left,
/// the volume is finite and positive, and an inward shell has removed
/// material.
///
/// Errors:
/// - InvalidArgument: see validate().
/// - FailedPrecondition: the body is empty or not one solid; the kernel
///   cannot build the walls, or its result fails the checks above. Walls
///   thicker than half the body where it is thinnest, and rounds no larger
///   than an inward wall, end here.
/// - NotFound: an open face's name is carried by no face of the body.
/// - Internal: the kernel failed unexpectedly.
///
/// Messages name open faces by their position in the request, from 1.
///
/// The result carries the names of @p body's faces through the kernel's
/// history: a remaining face keeps its names, and an open face's names go
/// to the face the kernel makes of it, the rim around the opening in the
/// open face's plane (inside its outline inward, outside it outward). The
/// walls are not named.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<Body> shellBody(const Body& body, const ShellRequest& request);

} // namespace bettercad::geometry
