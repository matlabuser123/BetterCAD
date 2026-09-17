#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/geometry/Export.hpp>
#include <bettercad/core/units/Units.hpp>

#include <vector>

namespace bettercad::geometry {

/// A radius at one point of an edge.
struct RadiusStation {
    /// Where along the edge, from 0 to 1 of its length: 0 at the end that
    /// comes first along the edge's line in its canonical direction
    /// (EdgeSignature::direction), 1 at the other end. The canonical direction
    /// belongs to the line, not to the kernel's edge, so a regeneration never
    /// reverses the stations.
    double position = 0.0;
    Length radius{};

    friend bool operator==(const RadiusStation&, const RadiusStation&) = default;
};

/// One edge of a variable-radius fillet and its radius stations, in order.
struct VariableFilletEdge {
    /// A straight edge (a line signature).
    EdgeSignature edge{};
    /// Two or more; the first at 0, the last at 1, positions increasing by
    /// at least kMinimumStationGap.
    std::vector<RadiusStation> stations{};

    friend bool operator==(const VariableFilletEdge&, const VariableFilletEdge&) = default;
};

/// A variable-radius fillet: each edge is replaced by a rounded face whose
/// radius varies along the edge, tangent to the edge's two faces.
///
/// The radius law. At the stations the radius is exactly the station's.
/// Between them it follows the geometry kernel's law (OCCT 8.0.1), which
/// BetterCAD computes itself and checks: the clamped cubic spline in the
/// position u, with knots at -1/2, at every station and at 3/2, through the
/// first station's radius at -1/2 and the last station's at 3/2, and with
/// zero slope at -1/2 and 3/2. (The kernel extends a fillet whose ends meet
/// no other rounded edge by half its length at each end and repeats the end
/// radii there; docs/verification/P12-FEAT-006.) With two stations of radii
/// a and b this is r(u) = a + (b - a) (u - 4/7 u (1 - u) (1 - 2 u)).
///
/// Such a spline can swing beyond its data. A request whose law would leave
/// the range of the two stations around any point of the edge is refused, so
/// between two stations the radius always stays between their radii.
struct VariableFilletRequest {
    std::vector<VariableFilletEdge> edges{};

    friend bool operator==(const VariableFilletRequest&, const VariableFilletRequest&) = default;
};

/// Stations closer than this (in position) are refused.
inline constexpr double kMinimumStationGap = 1e-6;
/// Radii of one edge that are not all equal must spread over at least this
/// much, so that the kernel (which takes radii within 1e-7 mm of each other
/// as one) builds the law that was asked for.
inline constexpr double kMinimumRadiusSpreadMm = 1e-6;

/// What validate() checks.
enum class StationCheck {
    /// Everything, the radius law included.
    Complete,
    /// Everything but the law (whose radii may not be known yet: they may be
    /// driven by parameters).
    WithoutLaw,
};

/// Checks the parts of a request that do not depend on a body. Fails with
/// InvalidArgument for: no edges; an invalid or duplicate signature, or one
/// that is not a line; fewer than two stations on an edge; a position that
/// is not finite, a first station not at 0 or a last not at 1, positions
/// that do not increase by kMinimumStationGap; a radius that is not positive
/// and finite; and, with StationCheck::Complete, radii that differ by less
/// than kMinimumRadiusSpreadMm without being equal, or a law that leaves
/// the range of the two stations around it (the message gives the radius it
/// would reach and where).
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<void> validate(const VariableFilletRequest& request,
                                                              StationCheck check = StationCheck::Complete);

/// The radius of the law above at @p position (in [0, 1]) for @p stations,
/// which must pass validate(). For reports and tools; the fillet computes
/// its own. Fails with InvalidArgument for invalid stations or a position
/// outside [0, 1].
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<Length> radiusAt(const std::vector<RadiusStation>& stations,
                                                               double position);

/// Rounds straight edges of @p body with radii varying along them; @p body
/// is not modified.
///
/// Each reference must match exactly one edge of the body that lies between
/// two planar faces meeting at an angle, that continues smoothly into no
/// other edge, and that shares no vertex with another edge of the request.
/// These are the edges whose fillet the kernel builds from the edge's own
/// stations alone; on others its law also depends on the blends around it.
///
/// Before the kernel runs, the fillet must fit as a constant one of the
/// largest radius on the edge would (see filletEdges()). The result is kept
/// only if the kernel reports completion and:
/// - it is one valid solid for each solid of @p body, with finite, positive
///   volume and no self-intersection;
/// - one fillet face was generated from each edge;
/// - the kernel's radius law along each edge is the law above, within
///   1e-9 mm (a constant radius for equal stations);
/// - sampled points of each fillet face lie, within 1e-7 mm, on the arc of
///   the law's radius that is tangent to both faces in the plane normal to
///   the edge, and the face's boundary touches each face at the law's
///   distance from the edge, r tan(g / 2) for faces whose outward normals
///   are g apart.
///
/// Errors:
/// - InvalidArgument: see validate().
/// - NotFound: a reference matches no edge.
/// - FailedPrecondition: a reference is ambiguous; an edge is not between
///   two faces, or not between two planar faces; its faces join smoothly or
///   fold back on each other; it continues smoothly into another edge; it
///   meets another edge of the request; two of its stations lie closer than
///   1e-6 mm on it; the fillet does not fit; or the kernel cannot build it.
/// - Internal: the kernel's result fails one of the checks above.
///
/// Messages name the reference by its position in the request and its line.
///
/// The result carries the names of @p body's faces (see findNamedFaces()).
/// The fillet faces themselves are not named.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<Body> variableFilletEdges(const Body& body,
                                                                        const VariableFilletRequest& request);

} // namespace bettercad::geometry
