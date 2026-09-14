#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/geometry/Export.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/units/Units.hpp>

#include <string_view>

namespace bettercad::geometry {

/// The shape of a hole's entry.
enum class HoleType {
    Simple,      ///< a plain cylinder
    Counterbore, ///< a wider cylinder at the entry, e.g. for a bolt head
    Countersink, ///< a cone at the entry, e.g. for a flat-head screw
};

/// How deep a hole goes.
enum class HoleExtent {
    Through, ///< through all material along the axis, however thick it is
    Blind,   ///< to a given depth, with a flat bottom
};

/// "simple", "counterbore" or "countersink".
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT std::string_view toString(HoleType type) noexcept;
/// "through" or "blind".
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT std::string_view toString(HoleExtent extent) noexcept;

/// A cylindrical hole drilled into a planar face, perpendicular to it.
///
/// Placement: `face` names the plane and the side of the face; `center` is
/// in that plane's face-local coordinates (see facePoint()). The hole always
/// goes into the material: along the face's inward normal, the opposite of
/// the signature's outward normal. The top face of a block is drilled
/// downwards and its bottom face upwards.
///
/// Fields a type or extent does not use must be zero.
struct HoleRequest {
    FaceSignature face{};
    Point2D center{};
    HoleType type = HoleType::Simple;
    HoleExtent extent = HoleExtent::Through;
    Length diameter{};
    /// Blind only: from the face to the flat bottom.
    Length depth{};
    /// Counterbore only: wider than the hole, and not as deep as a blind hole.
    Length counterboreDiameter{};
    Length counterboreDepth{};
    /// Countersink only: the cone's diameter at the face (wider than the
    /// hole) and its included angle in (0, 180°). The cone narrows to the
    /// hole diameter at depth (D - d) / 2 / tan(angle / 2).
    Length countersinkDiameter{};
    Angle countersinkAngle{};

    friend bool operator==(const HoleRequest&, const HoleRequest&) = default;
};

/// Depth of a countersink cone: (D - d) / 2 / tan(angle / 2).
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Length countersinkDepth(const HoleRequest& request);

/// The same hole moved by @p translation (which must be finite): its face
/// reference moves with it, and its centre is the moved centre in the moved
/// plane's coordinates. A translation within the face's plane keeps the
/// face reference and moves only the centre. Whether the moved hole fits is
/// decided by cutHole(), as for any hole.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT HoleRequest translated(const HoleRequest& request,
                                                               const Translation3D& translation);

/// The same hole moved by @p motion: its face reference moves with it, and
/// its centre is the moved centre in the moved plane's coordinates. A
/// rotation about an axis perpendicular to the face keeps the face
/// reference and turns only the centre. A pure translation is translated()
/// exactly.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT HoleRequest transformed(const HoleRequest& request,
                                                                const RigidTransform3D& motion);

/// Checks everything that does not depend on a body. Fails with
/// InvalidArgument for a non-planar or non-finite face reference, a
/// non-finite centre, a diameter or depth that is not positive and finite,
/// head dimensions that do not exceed the hole (or reach a blind hole's
/// bottom), an angle outside (0, 180°), or a field the type or extent does
/// not use.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<void> validate(const HoleRequest& request);

/// Drills the hole into @p body; @p body is not modified. The cutter is a
/// solid of revolution of the hole's section about its axis, subtracted
/// with booleanDifference(). It starts 0.01 mm outside the face, so its
/// entry never coincides with the face; a through hole's cutter runs 1 mm
/// past the body's bounding box, so it is recomputed when the body changes
/// and never has a guessed depth.
///
/// Before the kernel runs:
/// - The placement face is the one planar face on the reference plane,
///   facing its way, that contains the centre. No face on the plane:
///   NotFound. Faces on the plane but none under the centre:
///   FailedPrecondition. The centre on two faces (on the edge between two
///   coplanar faces): FailedPrecondition, ambiguous.
/// - The hole's outline at the face (the counterbore or countersink if
///   there is one) must lie inside the face, at least 0.001 mm from all of
///   its edges. A hole that would break out of the face's side is refused,
///   not built.
/// - Along the axis, a blind hole (and a counterbore or countersink) must
///   end at least 0.001 mm before the material does. A blind hole that
///   would reach the far side is refused: use a through hole.
///
/// After the kernel: the result must be valid, keep the number of solids,
/// have a finite, positive volume smaller than the input's, and a blind
/// hole must remove exactly its own volume (within 1e-9 of the body's
/// volume); if it removes less, it broke into another cavity or face and is
/// refused. What is not checked: a through hole may pass through other
/// cavities or faces along its axis ("through all"), and nothing beyond the
/// entry face is checked for side breakout.
///
/// Errors: InvalidArgument (see validate()), NotFound, FailedPrecondition
/// (see above), Internal (the kernel failed or produced an invalid result).
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<Body> cutHole(const Body& body, const HoleRequest& request);

} // namespace bettercad::geometry
