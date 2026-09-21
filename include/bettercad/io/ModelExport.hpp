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

// Export of a document's geometry to neutral formats. Files are replaced
// atomically.
//
// What is exported depends on what the document IS (P13-STEP-001):
//
//     no components   the result bodies -- features whose bodies no other
//                     feature consumes (features::resultFeatures()), each in
//                     the model's own coordinates. Every part file exports
//                     exactly as it always has.
//
//     components      the ASSEMBLY: each part once, placed by each active
//                     component at the transform the SOLVE derived. A
//                     suppressed component is absent. STEP writes this as a
//                     product structure; STL, having no notion of structure,
//                     writes the placed geometry.
//
// The transform is the solved one, never the placement intent: a mate moves a
// component, and exporting where the engineer first put it would be exporting
// an assembly that was never assembled (ADR-005, ADR-008).
namespace bettercad::io {

struct ExportedBody {
    ObjectId feature{};
    std::string name{};
    /// Triangles written for this body (STL only).
    std::size_t triangles = 0;
};

/// One placed instance in an assembly export.
struct ExportedComponent {
    ComponentId component{};
    /// The object the component places.
    ObjectId part{};
    std::string name{};
};

struct ExportSummary {
    /// What geometry was written, in the terms the format wrote it in.
    ///
    /// For a part export, the result bodies. For a STEP assembly, the
    /// PARTS -- each once, named after its feature, however many components
    /// place it, because that is what the file contains. For an STL
    /// assembly, one entry per INSTANCE, named after the component, because
    /// STL has no product structure and each instance really is written out
    /// in full. `components` is the count that means the same thing in both.
    std::vector<ExportedBody> bodies{};
    /// The instances written, in ascending component order. Empty for a
    /// document that has no components, which is how a caller tells a part
    /// export from an assembly export.
    std::vector<ExportedComponent> components{};
    std::uintmax_t bytes = 0;
};

/// Writes STEP (AP214, millimetres).
///
/// For a document with no components, one product per result body, named
/// after its feature -- unchanged. For a document with components, a product
/// structure: one product per part, and one component instance per active
/// component at its solved transform, under one assembly product named after
/// the document.
///
/// Fails with FailedPrecondition if the model does not regenerate, if it has
/// nothing to write, or if an assembly does not solve; with NotFound if a
/// component's part cannot be resolved; and with IoError if the file cannot
/// be written. A failed export writes nothing.
[[nodiscard]] BETTERCAD_IO_EXPORT Result<ExportSummary> exportStep(const Document& document,
                                                                   const std::filesystem::path& path);

struct StlExportOptions {
    geometry::MeshOptions mesh{};
    StlFormat format = StlFormat::Binary;
};

/// Triangulates what exportStep() would write and emits it as one STL solid
/// in millimetres. An assembly is triangulated in its solved positions; STL
/// has no product structure, so the instances are flattened and the
/// limitation is in the format, not in this path. Errors as for
/// exportStep().
[[nodiscard]] BETTERCAD_IO_EXPORT Result<ExportSummary> exportStl(const Document& document,
                                                                  const std::filesystem::path& path,
                                                                  const StlExportOptions& options = {});

} // namespace bettercad::io
