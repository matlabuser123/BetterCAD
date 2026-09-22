#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/geometry/Export.hpp>
#include <bettercad/core/math/Direction.hpp>
#include <bettercad/core/math/Frame.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/math/RigidTransform.hpp>
#include <bettercad/core/math/Vector.hpp>
#include <bettercad/core/units/Units.hpp>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Face queries and face references, the counterpart of Edges.hpp.
//
// A FaceSignature refers to a face by the geometry it lies on, never by the
// kernel's enumeration order or object identity. Only planar faces can be
// referred to that way.
//
// Faces a feature generates also carry persistent names (FaceName,
// P12-STREF-001): the feature and the face's role. A name stays with its face
// through the operations that report a history (see findNamedFaces()).
namespace bettercad::geometry {

enum class FaceSurface {
    Plane,
    Cylinder,
    Cone,
    Sphere,
    Torus,
    Other, ///< any other surface (B-spline, offset, ...)
};

/// "plane", "cylinder", "cone", "sphere", "torus" or "other".
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT std::string_view toString(FaceSurface surface) noexcept;

/// A reference to a planar face by its supporting plane and the side its
/// material is on: the plane through `point` whose outward normal (pointing
/// away from the material) is `normal`.
///
/// What it survives, and what it does not:
/// - The face may grow, shrink or change shape within its plane. For
///   example, widening a block keeps its top face on the same plane.
/// - If the plane moves (e.g. the block gets taller and its top face rises),
///   the reference matches no face.
/// - Two faces of one body can lie on the same plane facing the same way
///   (e.g. the tops of two bosses); a reference alone does not tell them
///   apart. Users of references say how they choose (see cutHole()).
///
/// Both failures are reported, never guessed around. This is geometric
/// matching, not persistent topological naming.
///
/// Use planeSignature(), which makes the point canonical (the point of the
/// plane nearest the origin); the normal keeps its orientation, since it
/// tells the two sides of the plane apart.
struct FaceSignature {
    FaceSurface surface = FaceSurface::Plane;
    Point3D point{};
    Direction3D normal = Direction3D::unitZ();

    friend bool operator==(const FaceSignature&, const FaceSignature&) = default;
};

/// Canonical signature of the plane through @p point with outward normal
/// @p outwardNormal: the point is the plane's point nearest the origin, and
/// neither it nor the normal has negative zeros.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT FaceSignature planeSignature(const Point3D& point,
                                                                     const Direction3D& outwardNormal);

/// Checks a signature from any source (e.g. a file): a plane through a
/// finite point. Fails with InvalidArgument otherwise.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<void> validate(const FaceSignature& signature);

/// The signature of the same plane moved by @p translation (which must be
/// finite), facing the same way, in canonical form. A translation within the
/// plane gives the same signature.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT FaceSignature translated(const FaceSignature& signature,
                                                                 const Translation3D& translation);

/// The signature of the same plane moved by @p motion, facing the moved
/// way, in canonical form. A pure translation is translated() exactly.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT FaceSignature transformed(const FaceSignature& signature,
                                                                  const RigidTransform3D& motion);

/// For messages, e.g. "plane through (0, 0, 20) mm facing (0, 0, 1)".
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT std::string describe(const FaceSignature& signature);

/// Face-local coordinates on the signature's plane. The origin is the point
/// of the plane nearest the model origin; the axes are two model axes
/// projected into the plane: X and Y for a plane that is closest to
/// horizontal, X and Z for one closest to facing along Y, Y and Z otherwise.
/// So on the faces of an axis-aligned box, (u, v) are the model coordinates
/// along the face: (x, y) on top and bottom, (x, z) at front and back,
/// (y, z) at the sides. The axes do not depend on which side the normal
/// faces, so both faces of a plate share coordinates.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Point3D facePoint(const FaceSignature& signature, const Point2D& local);
/// The face-local coordinates of @p point projected onto the plane.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Point2D faceCoordinates(const FaceSignature& signature,
                                                                const Point3D& point);

/// One face of a body, described by its geometry.
/// The exact geometry of a cylindrical face: the axis it turns about and the
/// radius it turns at, read from the kernel's own surface.
///
/// Exact, never measured off a tessellation: a radius dimension on a drawing
/// is a number a part is made to, and a number sampled from a mesh is a
/// number nobody can hold a gauge against.
struct CylindricalFace {
    Axis3D axis{};
    Length radius{};

    friend bool operator==(const CylindricalFace&, const CylindricalFace&) = default;
};

struct FaceInfo {
    FaceSurface surface = FaceSurface::Other;
    Area area{};
    Point3D centroid{};
    /// A reference to this face, for planar faces.
    std::optional<FaceSignature> signature{};
    /// The axis and radius, for cylindrical faces only.
    std::optional<CylindricalFace> cylinder{};
    /// The face's names (P12-STREF-001), sorted.
    std::vector<FaceName> names{};
};

/// The faces of @p body. The order is the kernel's and carries no meaning;
/// select faces by their geometry. Fails with FailedPrecondition for an
/// empty body.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<std::vector<FaceInfo>> listFaces(const Body& body);

/// The planar faces of @p body on the signature's plane, facing its way:
/// the normals agree within 1e-9 rad and the plane passes within 1e-7 mm of
/// the signature's point.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<std::vector<FaceInfo>> findFaces(const Body& body,
                                                                                const FaceSignature& signature);

/// The faces of @p body that carry @p name, in listFaces() order.
///
/// Names are given to faces by the operations that generate them (e.g.
/// makePrism() with a namer) and carried by the boolean operations through
/// the kernel's history: a face split in two carries its name on both parts,
/// faces merged into one carry all their names, a face the operation removed
/// carries none. Other operations give bodies without names. A name is never
/// moved to a face because of its geometry. Fails with FailedPrecondition for
/// an empty body.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<std::vector<FaceInfo>> findNamedFaces(const Body& body,
                                                                                     const FaceName& name);

/// Maps a face name to the name it becomes, or to none (dropped).
using FaceRenamer = std::function<std::optional<FaceName>(const FaceName&)>;

/// The body with its face names mapped by @p rename (P12-SKETCH-003:
/// patterns and mirrors mark their copies). The shape is shared, not
/// copied: bodies are immutable. An empty body is returned as it is.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Body renameFaces(const Body& body, const FaceRenamer& rename);

/// A sketch frame on the signature's plane: the origin and X axis of the
/// face-local coordinates (facePoint()), the outward normal as normal, and
/// Y = normal x X. On a face facing +Z, (x, y) are the face's (u, v); on a
/// face facing -Z, (x, -v). Fails with InvalidArgument for an invalid
/// signature.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<Frame3D> faceFrame(const FaceSignature& signature);

} // namespace bettercad::geometry
