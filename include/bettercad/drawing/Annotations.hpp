#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/drawing/Annotation.hpp>
#include <bettercad/drawing/Scene.hpp>
#include <bettercad/drawing/Views.hpp>

#include <string>
#include <vector>

// The document-facing operations on annotations (P14-ANNO-001).
//
// Free functions, not Document members, for the reason Sheets.hpp, Views.hpp
// and Dimensions.hpp give: Document is layer 0 and must not know drawings
// exist (ADR-015).
//
// NOTHING DRAWN IS STORED. `draw()` resolves the annotation's target against
// the model as it is now and builds every line and every character from it,
// so a callout cannot be out of date with the hole it labels and a centre
// mark cannot be left behind by the feature that moved. There is no cached
// geometry, and therefore no stale geometry to present as current.
namespace bettercad::drawing {

/// Checks a definition against @p document: the view exists and is a view,
/// and every object the target names exists.
///
/// Whether the referenced GEOMETRY is there -- whether the named face
/// survived its feature's own operation -- needs the regenerated bodies, so
/// it is draw()'s to report.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<void> checkAnnotation(
    const Document& document, const AnnotationDefinition& definition);

/// Adds an annotation and returns its ID. Fails, changing nothing, if the
/// definition is malformed or does not check out; no ID is consumed by a
/// failed call.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<AnnotationId> createAnnotation(
    Document& document, std::string name, const AnnotationDefinition& definition);

/// Replaces the definition of the annotation with @p id, after the same
/// checks createAnnotation runs. Returns whether anything changed.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<bool> setAnnotationDefinition(
    Document& document, AnnotationId id, const AnnotationDefinition& definition);

/// The annotation with @p id, or nullptr.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT const Annotation* findAnnotation(const Document& document,
                                                                        AnnotationId id) noexcept;

/// Every annotation in @p document, in ascending ID order.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::vector<AnnotationId> annotations(const Document& document);

/// Every annotation on @p view, in ascending ID order.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::vector<AnnotationId> annotationsOn(
    const Document& document, ViewId view);

/// Removes the annotation with @p id. Fails if there is none.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<void> removeAnnotation(Document& document,
                                                                     AnnotationId id);

// --- Derived (ADR-011). Computed on every call, never stored. ---

/// The words this annotation shows, now.
///
/// A note, a leader and a datum give back what was stored. A hole callout
/// reads the hole: its diameter comes from the feature's own resolution, so a
/// diameter driven by a parameter, set by a thread standard or taken from an
/// ISO 273 clearance reads the value in force. `Ø10 THRU`, `Ø8 DEEP 20`.
///
/// Fails, naming the annotation, when the target cannot be resolved. It never
/// gives back the last text it managed to build.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<std::string> annotationText(
    const Document& document, AnnotationId id, const BodyLookup& bodies,
    const TransformLookup& transforms = {});

/// The datum letters the feature-control frame @p id cites that no datum
/// feature symbol in @p document defines, in the order the frame cites them.
///
/// A REPORT, not a refusal. A frame is NOT rejected for citing a datum that
/// has not been drawn yet, because that would make the order an engineer
/// works in part of what is legal -- frames and datum symbols are placed in
/// either order, and a drawing half finished is not a drawing that is wrong.
/// What must not happen is that a drawing is issued claiming a datum nobody
/// defined, and this is what says so.
///
/// Empty for an annotation that is not a feature-control frame, and for a
/// frame whose datums are all defined. Fails only if there is no such
/// annotation.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<std::vector<char>> undefinedDatums(
    const Document& document, AnnotationId id);

/// What this annotation draws, in SHEET MILLIMETRES.
///
/// Where it points is found through the view's own projection, so it moves
/// with the geometry when the view moves or its scale changes. How big it is
/// drawn does not: text height, arrowheads, centre-mark arms, symbol
/// proportions and leader elbows are paper lengths and are never multiplied
/// by a view's scale. That is the invariant this milestone turns on, and
/// there are tests for it at five scales.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<SceneItems> draw(
    const Document& document, AnnotationId id, const BodyLookup& bodies,
    const TransformLookup& transforms = {});

/// Everything the annotations of @p view draw, in ascending annotation ID
/// order so that two runs of one drawing give one sequence.
///
/// Fails if any of them fails: a sheet that quietly dropped the annotation it
/// could not resolve would be a drawing that looked complete and was not.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<SceneItems> drawAnnotations(
    const Document& document, ViewId view, const BodyLookup& bodies,
    const TransformLookup& transforms = {});

} // namespace bettercad::drawing
