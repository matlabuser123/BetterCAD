#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/drawing/Scene.hpp>
#include <bettercad/io/Export.hpp>

#include <filesystem>
#include <string>

// Writing a drawing out (P14-EXPORT-001, ADR-016).
//
// Each writer TRANSCRIBES a DrawingScene. None of them may recompute a
// projection, a dimension, a BOM quantity or a line's visibility; none of them
// takes a Document, a Body or a scale, and that is enforced by the signatures
// rather than by care: there is nothing here to reach the model through.
//
//     drawing intent -> model -> DrawingScene -> PDF | SVG | DXF
//
// THE SCENE'S COORDINATES, restated because this is where they are consumed:
// millimetres, origin at the sheet's BOTTOM-LEFT, +X right, +Y UP. PDF and DXF
// agree with that and transform nothing but the unit. SVG's y grows DOWNWARD,
// so its writer flips once -- in one place, named, and tested.
//
// DETERMINISM IS A CONTRACT, not a hope. The same scene gives the same bytes,
// every time, in every preset:
//
//     SVG   byte-identical
//     DXF   byte-identical
//     PDF   byte-identical -- no creation date, no producer string, no
//           document ID, nothing that varies between runs
//
// Numbers are written by one formatter, in a fixed notation, with no locale
// and no negative zero.
namespace bettercad::io {

/// The SVG text for @p scene.
///
/// `width` and `height` carry the physical page in millimetres and the
/// `viewBox` is in the same units, so the drawing is the size it says it is
/// rather than a number of pixels.
[[nodiscard]] BETTERCAD_IO_EXPORT Result<std::string> svgDocument(const drawing::DrawingScene& scene);

/// The DXF text for @p scene: AutoCAD R12 ASCII, `$INSUNITS` 4 (millimetres).
///
/// R12 because it is the version every reader accepts, and because a drawing
/// needs LINE, LWPOLYLINE, CIRCLE, ARC and TEXT and R12 has all of them.
[[nodiscard]] BETTERCAD_IO_EXPORT Result<std::string> dxfDocument(const drawing::DrawingScene& scene);

/// The PDF bytes for @p scene: PDF 1.4, one page, true vector content.
///
/// Nothing is rasterised. The page box is the sheet, converted once by
/// `pt = mm * 72 / 25.4`.
[[nodiscard]] BETTERCAD_IO_EXPORT Result<std::string> pdfDocument(const drawing::DrawingScene& scene);

/// Writes the SVG for @p scene to @p path, atomically.
[[nodiscard]] BETTERCAD_IO_EXPORT Result<void> exportSvg(const drawing::DrawingScene& scene,
                                                          const std::filesystem::path& path);
/// Writes the DXF for @p scene to @p path, atomically.
[[nodiscard]] BETTERCAD_IO_EXPORT Result<void> exportDxf(const drawing::DrawingScene& scene,
                                                          const std::filesystem::path& path);
/// Writes the PDF for @p scene to @p path, atomically.
[[nodiscard]] BETTERCAD_IO_EXPORT Result<void> exportPdf(const drawing::DrawingScene& scene,
                                                          const std::filesystem::path& path);

} // namespace bettercad::io
