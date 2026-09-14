#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/geometry/Export.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/units/Units.hpp>

#include <array>
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

/// Indexed triangle mesh. Triangles are counter-clockwise seen from outside
/// the solid. Vertices are stored per face: a vertex on an edge shared by two
/// faces appears once for each face (with identical coordinates).
struct Mesh {
    std::vector<Point3D> vertices{};
    std::vector<std::array<std::uint32_t, 3>> triangles{};
};

/// Triangulates the faces of @p body. The body itself is not modified.
/// Fails with FailedPrecondition for an empty body, InvalidArgument for
/// non-positive or non-finite deflections, and Internal if the kernel cannot
/// mesh some face.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<Mesh> triangulate(const Body& body,
                                                                 const MeshOptions& options = {});

} // namespace bettercad::geometry
