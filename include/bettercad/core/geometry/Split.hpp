#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/geometry/Export.hpp>
#include <bettercad/core/math/Frame.hpp>

#include <span>
#include <string_view>
#include <vector>

// Splitting a body with a plane, and bodies of several separate solids
// (P12-FEAT-002).
namespace bettercad::geometry {

/// Which part of a split body is kept: what lies on the side the plane's
/// normal points to (Front), on the other side (Back), or both parts as
/// separate solids of one body.
enum class SplitKeep {
    Front,
    Back,
    Both,
};

/// "front", "back", "both".
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT std::string_view toString(SplitKeep keep) noexcept;

/// @p body cut by the plane of @p plane (its origin and normal), keeping the
/// part(s) @p keep says. The parts are the body's intersection with, and
/// difference from, a box on the plane's front side that encloses the body's
/// bounds (1 mm beyond them), so they carry the body's face names through the
/// kernel's history; the new faces on the plane are not named.
///
/// Fails with InvalidArgument for an empty body, and with FailedPrecondition
/// when the plane does not cross the body: when all of it lies on one side
/// of the plane (within 1e-7 mm), or nothing of it lies on a side although
/// its bounds cross the plane.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<Body> splitBody(const Body& body, const Frame3D& plane,
                                                              SplitKeep keep);

/// One body holding the solids of @p parts side by side, not united, with
/// their face names. Fails with InvalidArgument when there are no parts or a
/// part is empty, and with Internal when the kernel's checker rejects the
/// result.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<Body> gatherSolids(std::span<const Body> parts);

/// The solids of @p body, each as a body with the names of its faces
/// (P12-FEAT-005), in the kernel's order of the body's solids. An empty
/// body has none.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<std::vector<Body>> solidsOf(const Body& body);

} // namespace bettercad::geometry
