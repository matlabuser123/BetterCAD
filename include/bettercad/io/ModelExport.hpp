#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Mesh.hpp>
#include <bettercad/io/Export.hpp>
#include <bettercad/io/Stl.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Export of a document's geometry to neutral formats. Both exports regenerate
// a copy of the document and write its result bodies (features whose bodies
// no other feature consumes; see features::resultFeatures()). Files are
// replaced atomically.
namespace bettercad::io {

struct ExportedBody {
    ObjectId feature{};
    std::string name{};
    /// Triangles written for this body (STL only).
    std::size_t triangles = 0;
};

struct ExportSummary {
    std::vector<ExportedBody> bodies{};
    std::uintmax_t bytes = 0;
};

/// Writes the result bodies as STEP (AP214, millimetres), one product per
/// body named after its feature. Fails with FailedPrecondition if the model
/// does not regenerate or has no bodies, and IoError if the file cannot be
/// written.
[[nodiscard]] BETTERCAD_IO_EXPORT Result<ExportSummary> exportStep(const Document& document,
                                                                   const std::filesystem::path& path);

struct StlExportOptions {
    geometry::MeshOptions mesh{};
    StlFormat format = StlFormat::Binary;
};

/// Triangulates the result bodies and writes them as one STL solid in
/// millimetres. Errors as for exportStep().
[[nodiscard]] BETTERCAD_IO_EXPORT Result<ExportSummary> exportStl(const Document& document,
                                                                  const std::filesystem::path& path,
                                                                  const StlExportOptions& options = {});

} // namespace bettercad::io
