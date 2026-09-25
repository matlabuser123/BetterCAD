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
//     "objects":    [ { "id": N, "type": "sketch" | "extrude" | "revolve" | "chamfer" | "fillet" | "hole" |
//                       "linear_pattern" | "circular_pattern" | "mirror" | "sweep" | "loft", "name": ...,
//                       "data": {...} } ]
//   }
//
// All lengths are metres and all stored numbers round-trip exactly. IDs are
// stored explicitly, and references between items (the dependency
// relationships) are stored as IDs. Geometry is derived and is regenerated
// after loading.
namespace bettercad::io {

inline constexpr std::string_view kDocumentFormat = "bettercad-document";

/// The format version, and the rule for changing it (P13-PERSIST-001).
///
/// **Bump this only for a change that an existing reader would get WRONG.**
/// Not for growth: the schema has grown a great deal since this was written
/// -- parameters and expressions, configurations, datums, stable face
/// references, components, placements, mates, mechanical joints, suppression
/// -- and the number has correctly never moved, because every one of those
/// was additive.
///
/// What makes additive growth safe is that both directions already fail
/// safely without the version doing any work:
///
///     new reader, old file   loads. An absent section means "none of
///                            those", which is what an older document meant
///                            by not having one.
///     old reader, new file   REFUSES. An unrecognised object type is a
///                            parse error naming it -- "unknown object type
///                            'component'" -- never a silent skip, so no
///                            data is lost by a reader that predates it.
///
/// The version exists for the case those two rules cannot cover: a field
/// that changes meaning, a unit that changes, a default that flips, an
/// ordering that becomes significant. An old reader meets such a file, sees
/// nothing it fails to recognise, and misreads it. That is what a bump
/// prevents, and it is the only thing that should cause one.
///
/// So: adding a kind, an object type or an optional field needs no bump.
/// Changing what an existing field means needs one, and needs the reader to
/// say which versions it accepts.
///
/// VERSION 2 (ADR-024) names a chamfer face by its selection's stable ID
/// rather than by the selection's position. A version-2 document writes
/// `"chamfer_edge": id` in a face selector and `{"id": n, "edge": <signature>}`
/// plus `"last_edge_id"` in a chamfer, none of which a version-1 reader
/// accepts -- so the version says so rather than leaving it to a field it
/// happens not to recognise.
///
/// A version-1 document still loads. Its chamfer selections arrive without
/// ids and are given 1..N in the file's own order, and its `"edge": n` face
/// references convert to the id n, which is the same face the file named; see
/// faceSelectorFromJson. A version-1 document is never written again.
inline constexpr int kDocumentFormatVersion = 2;
/// The oldest document version this build reads.
inline constexpr int kOldestReadableDocumentVersion = 1;
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
