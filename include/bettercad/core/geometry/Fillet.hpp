#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/geometry/Export.hpp>
#include <bettercad/core/units/Units.hpp>

#include <vector>

namespace bettercad::geometry {

/// A constant-radius fillet: each edge is replaced by a rounded face of the
/// given radius, tangent to the two faces the edge joined. Convex edges lose
/// material, concave edges gain it.
struct FilletRequest {
    /// The edges to round; each must match exactly one edge of the body.
    std::vector<EdgeSignature> edges{};
    Length radius{};

    friend bool operator==(const FilletRequest&, const FilletRequest&) = default;
};

/// Checks the parts of a request that do not depend on a body. Fails with
/// InvalidArgument for no edges, an invalid or duplicate signature, or a
/// radius that is not positive and finite.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<void> validate(const FilletRequest& request);

/// Rounds edges of @p body; @p body is not modified. Each reference must
/// match exactly one edge that lies between two faces meeting at an angle
/// (not smoothly). A fillet continues along edges tangent to a selected edge,
/// so a smooth chain of edges is rounded as a whole; where several selected
/// edges meet at a vertex, the kernel blends the corner. The result must be
/// valid, have the same number of solids and a finite, positive volume.
///
/// Before the kernel runs, every fillet must fit, by at least 0.001 mm, as
/// for chamfers (see chamferEdges()). A fillet of radius r touches each face
/// at r·tan(γ/2) from the edge, where γ is the angle between the two faces'
/// outward normals (r where faces meet square); the largest γ along the edge
/// is used. That strip must stay clear of the face's other edges, must not
/// run across the face, and must not meet another fillet's strip on it. The
/// width is exact for planar faces and for the planar and cylindrical faces
/// of turned parts; distances are straight lines, so the check errs towards
/// refusing on curved faces.
///
/// Errors:
/// - InvalidArgument: see validate(); also two references to the same chain
///   of edges.
/// - NotFound: a reference matches no edge.
/// - FailedPrecondition: a reference is ambiguous; an edge is not between
///   two faces; its faces join smoothly, so there is no corner to round; a
///   fillet does not fit (e.g. a radius larger than a face allows); or the
///   kernel still cannot build the fillet.
/// - Internal: the kernel produced an invalid result, or could not measure
///   the geometry around an edge.
///
/// Messages name the reference by its position in the request and its curve.
///
/// The result carries the names of @p body's faces (see findNamedFaces()).
/// The fillet faces themselves are not named.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<Body> filletEdges(const Body& body, const FilletRequest& request);

} // namespace bettercad::geometry
