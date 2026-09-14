#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/io/Export.hpp>

#include <filesystem>
#include <string>
#include <string_view>

// Native BetterCAD document format (.bcad): transparent JSON.
//
//   {
//     "format": "bettercad-document", "version": 1, "units": "SI",
//     "document":   { "id": "<uuid>", "name": ..., "metadata": {...}, "last_allocated_id": N },
//     "parameters": [ ... ],
//     "objects":    [ { "id": N, "type": "sketch" | "extrude" | "revolve", "name": ..., "data": {...} } ]
//   }
//
// All lengths are metres and all stored numbers round-trip exactly. IDs are
// stored explicitly, and references between items (the dependency
// relationships) are stored as IDs. Geometry is derived and is regenerated
// after loading.
namespace bettercad::io {

inline constexpr std::string_view kDocumentFormat = "bettercad-document";
inline constexpr int kDocumentFormatVersion = 1;
inline constexpr std::string_view kDocumentExtension = ".bcad";

/// Serializes a document. Fails with InvalidArgument if it contains an
/// object kind the format does not support.
[[nodiscard]] BETTERCAD_IO_EXPORT Result<std::string> documentToJson(const Document& document);

/// Parses a document written by documentToJson(). The input is validated
/// strictly and errors name the JSON path. The loaded document is clean.
[[nodiscard]] BETTERCAD_IO_EXPORT Result<Document> documentFromJson(std::string_view json);

/// Writes the document atomically: a temporary file next to @p path is
/// written completely and then renamed over @p path. Does not mark the
/// document clean; callers do that after a successful save.
[[nodiscard]] BETTERCAD_IO_EXPORT Result<void> saveDocument(const Document& document,
                                                           const std::filesystem::path& path);

/// Reads and parses a document file. Fails with NotFound if the file does
/// not exist and IoError if it cannot be read.
[[nodiscard]] BETTERCAD_IO_EXPORT Result<Document> loadDocument(const std::filesystem::path& path);

} // namespace bettercad::io
