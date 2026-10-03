#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/geometry/Export.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/units/Units.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace bettercad::geometry {

/// How closely a triangulation follows curved surfaces.
struct MeshOptions {
    /// Maximum distance between the triangles and the true surface.
    Length linearDeflection = Length::fromSi(1e-4); // 0.1 mm
    /// Maximum angle between the normals of adjacent triangles on a curved
    /// surface, in addition to the distance limit.
    Angle angularDeflection = Angle::fromSi(0.3490658503988659); // 20 degrees
};

/// Which triangles of a Mesh came from one CAD face.
///
/// Added by P16-SURF-001 so that a higher layer can group triangles by the face
/// that produced them -- which is what makes a per-face check (are these
/// normals coherent?) possible without this layer knowing anything about CAD
/// references. It deliberately carries NO FaceName: attributing a facet to a
/// named face is P16-MAP-001's, and a name here would put a document type in
/// the kernel adapter's output.
///
/// WHAT MAKES IT USABLE AS PROVENANCE (P16-MAP-001): `Mesh::faces[i]` is the
/// i-th face of `listFaces(body)`. Both explore the same authoritative shape
/// with the same traversal, and `triangulate` indexes its groups by the
/// ORIGINAL shape's faces -- pairing each with its meshed copy through the
/// copier's own history -- so the correspondence holds by construction and not
/// because a copy happened to preserve face order. That index is a correlation
/// handle for one call on one body, never an identity: it is not persisted, and
/// `listFaces` documents that the kernel's order carries no meaning.
struct MeshFace {
    /// Index into Mesh::triangles of this face's first triangle.
    std::size_t firstTriangle = 0;
    std::size_t triangleCount = 0;

    friend bool operator==(const MeshFace&, const MeshFace&) = default;
};

/// Indexed triangle mesh. Triangles are counter-clockwise seen from outside
/// the solid. Vertices are stored per face: a vertex on an edge shared by two
/// faces appears once for each face (with identical coordinates).
///
/// THAT PER-FACE DUPLICATION IS THE POINT OF THIS TYPE AND ITS LIMIT. It is a
/// triangle soup suitable for export and display: geometrically watertight, and
/// NOT topologically conforming. An engineering surface mesh needs shared nodes
/// on shared edges, which meshing::generateSurfaceMesh builds from this
/// (P16-SURF-001).
struct Mesh {
    std::vector<Point3D> vertices{};
    std::vector<std::array<std::uint32_t, 3>> triangles{};
    /// One entry per CAD face, in the same order as `listFaces(body)`,
    /// covering `triangles` in order and without gaps. See MeshFace.
    std::vector<MeshFace> faces{};
};

/// Triangulates the faces of @p body. The body itself is not modified.
/// Fails with FailedPrecondition for an empty body, InvalidArgument for
/// non-positive or non-finite deflections, and Internal if the kernel cannot
/// mesh some face.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<Mesh> triangulate(const Body& body,
                                                                 const MeshOptions& options = {});

} // namespace bettercad::geometry
