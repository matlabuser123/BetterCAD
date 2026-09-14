#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/geometry/Export.hpp>
#include <bettercad/core/math/Direction.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/math/Vector.hpp>
#include <bettercad/core/units/Units.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Edge queries and edge references.
//
// BetterCAD has no persistent topological naming yet (a later milestone).
// Until then, features refer to an edge of a body by the curve the edge lies
// on, never by the kernel's enumeration order or object identity.
namespace bettercad::geometry {

enum class EdgeCurve {
    Line,
    Circle,
    Other, ///< any other curve (ellipse, spline, ...); cannot be referenced yet
};

/// "line", "circle" or "other".
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT std::string_view toString(EdgeCurve curve) noexcept;

/// A reference to an edge by its supporting curve: an infinite line, or a
/// circle given by its centre, axis and radius. It resolves to the one edge
/// of a body that lies on that curve (see findEdges()).
///
/// What it survives, and what it does not:
/// - The edge may change length or move along its curve. For example,
///   widening a box keeps its edges along the width on the same lines.
/// - If the supporting curve itself moves or changes size (e.g. the box gets
///   taller, moving its top edges), the reference matches no edge.
/// - If several edges lie on the curve (e.g. a cut splits the edge in two),
///   the reference is ambiguous.
///
/// Both failures are reported, never guessed around. This is geometric
/// matching, not persistent topological naming.
///
/// Use lineSignature() and circleSignature(), which make the form canonical:
/// the direction (or axis) points into the half-space where its first
/// non-zero component is positive, and a line's point is the point of the
/// line nearest the origin. The same curve then gives the same signature
/// whichever point and orientation it was described with.
struct EdgeSignature {
    EdgeCurve curve = EdgeCurve::Line;
    /// Line: the point of the line nearest the origin. Circle: the centre.
    Point3D point{};
    /// Line: its direction. Circle: its axis. Canonical sign.
    Direction3D direction = Direction3D::unitZ();
    /// Circle only.
    Length radius{};

    friend bool operator==(const EdgeSignature&, const EdgeSignature&) = default;
};

/// Canonical signature of the line through @p point along @p direction.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT EdgeSignature lineSignature(const Point3D& point,
                                                                    const Direction3D& direction);

/// Canonical signature of a circle. Fails with InvalidArgument for a
/// non-finite centre or a radius that is not positive and finite.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<EdgeSignature>
circleSignature(const Point3D& center, const Direction3D& axis, Length radius);

/// Checks a signature from any source (e.g. a file): a Line or Circle curve,
/// a finite point, and for circles a positive finite radius.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<void> validate(const EdgeSignature& signature);

/// The signature of the same curve moved by @p translation (which must be
/// finite), in canonical form. It refers to the moved curve exactly; it is
/// not a search for similar edges.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT EdgeSignature translated(const EdgeSignature& signature,
                                                                 const Translation3D& translation);

/// For messages, e.g. "line through (0, 0, 20) mm along (1, 0, 0)".
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT std::string describe(const EdgeSignature& signature);

/// One edge of a body, described by its geometry.
struct EdgeInfo {
    EdgeCurve curve = EdgeCurve::Other;
    Point3D start{};
    Point3D end{};
    Point3D midpoint{};
    Length length{};
    /// A reference to this edge, for line and circle edges.
    std::optional<EdgeSignature> signature{};
    /// Distinct faces bounded by the edge: 2 for an ordinary edge of a solid,
    /// 1 for a seam of a closed face.
    std::size_t faces = 0;
    /// For an edge between two faces: the angle between their outward
    /// normals at the edge's midpoint. 0 where the faces join smoothly
    /// (tangent), 90° along the edges of a box; it does not tell convex from
    /// concave edges.
    std::optional<Angle> faceAngle{};
};

/// The edges of @p body, degenerate edges excluded. The order is the
/// kernel's and carries no meaning; select edges by their geometry. Fails
/// with FailedPrecondition for an empty body.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<std::vector<EdgeInfo>> listEdges(const Body& body);

/// The edges of @p body that lie on the signature's curve: the line or
/// circle matches within 1e-7 mm and 1e-9 rad. A usable reference matches
/// exactly one edge.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<std::vector<EdgeInfo>> findEdges(const Body& body,
                                                                                const EdgeSignature& signature);

} // namespace bettercad::geometry
