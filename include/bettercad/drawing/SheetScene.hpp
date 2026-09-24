#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/drawing/Export.hpp>
#include <bettercad/drawing/Scene.hpp>
#include <bettercad/drawing/Views.hpp>

// Assembling one finished sheet (P14-EXPORT-001, ADR-016).
//
// This is the last thing the drawing module does. Everything above it decided
// what a drawing MEANS; this turns that into the neutral vector scene a writer
// transcribes, and after it there is no more engineering left to do:
//
//     intent -> model -> views, dimensions, annotations -> SHEET SCENE -> writer
//
// A writer is handed a DrawingScene and needs nothing else. If one ever has to
// reach back past this for something, this is what is incomplete.
//
// WHAT IT PUTS ON THE PAGE, in this order, so two runs give one sequence:
//
//     the frame            the sheet's border, at its margins
//     each view            in ascending ID order; its edges styled by what
//                          hidden-line removal already decided, its section
//                          hatch, its dimensions, then its annotations
//
// NOTHING IS RECLASSIFIED HERE. An edge is dashed because HLR called it
// hidden, not because this looked at it again; a dimension's number is
// measure()'s; a BOM row's quantity is the assembly's. This maps decisions to
// ink and makes none of its own.
namespace bettercad::drawing {

/// The whole of @p sheet, ready to write out.
///
/// Fails, naming what could not be produced, if any view, dimension or
/// annotation on the sheet fails to resolve -- a sheet that quietly dropped
/// what it could not draw would be a drawing that looked complete and was
/// not, which is the same rule drawAnnotations() follows.
///
/// The result is validated before it is returned, so a writer never receives a
/// scene with a point off the page or a text run with nothing in it.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<DrawingScene> sheetScene(
    const Document& document, SheetId sheet, const BodyLookup& bodies,
    const TransformLookup& transforms = {});

} // namespace bettercad::drawing
