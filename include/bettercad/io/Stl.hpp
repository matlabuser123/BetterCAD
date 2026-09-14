#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/geometry/Mesh.hpp>
#include <bettercad/io/Export.hpp>

#include <span>
#include <string>
#include <string_view>

namespace bettercad::io {

enum class StlFormat {
    Binary, ///< 80-byte header, triangle count, 50 bytes per triangle (32-bit floats).
    Ascii,  ///< "solid ... endsolid" text.
};

/// STL data for all triangles of @p meshes, as one solid, in millimetres
/// (STL has no unit field; millimetres are the common convention). Facet
/// normals are computed from the counter-clockwise vertex order. ASCII
/// coordinates are the shortest decimals that round-trip to the same 32-bit
/// floats as the binary format.
///
/// @p name goes into the binary header or the ASCII "solid" line; characters
/// outside printable ASCII are replaced. Fails with InvalidArgument if a
/// coordinate is not finite or does not fit a 32-bit float, or if there are
/// more triangles than binary STL can count.
[[nodiscard]] BETTERCAD_IO_EXPORT Result<std::string> meshesToStl(std::span<const geometry::Mesh> meshes,
                                                                  StlFormat format, std::string_view name);

} // namespace bettercad::io
