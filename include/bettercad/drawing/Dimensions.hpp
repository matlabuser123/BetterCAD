#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/drawing/Dimension.hpp>
#include <bettercad/drawing/Views.hpp>

#include <string>
#include <vector>

// The document-facing operations on dimensions (P14-DIM-001).
//
// Free functions, not Document members, for the reason Sheets.hpp and
// Views.hpp give: Document is layer 0 and must not know drawings exist
// (ADR-015).
//
// THE NUMBER IS NEVER STORED. `measure()` resolves the dimension's references
// against the model as it is now and works the answer out from the geometry
// it finds. There is no cached value to go stale, and no way to present one:
// a dimension that could not be resolved reports that, and reports no number
// at all.
namespace bettercad::drawing {

/// Checks a definition against @p document: the view exists and is a view,
/// and every object the references name exists and is of a kind that can
/// carry them.
///
/// Whether the referenced geometry is actually THERE -- whether the named
/// face survived the feature's own operation -- is not knowable without the
/// regenerated bodies, so it is measure()'s to report, not this.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<void> checkDimension(const Document& document,
                                                                   const DimensionDefinition& definition);

/// Adds a dimension and returns its ID. Fails, changing nothing, if the
/// definition is malformed or does not check out; no ID is consumed by a
/// failed call.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<DimensionId> createDimension(
    Document& document, std::string name, const DimensionDefinition& definition);

/// Replaces the definition of the dimension with @p id, after the same checks
/// createDimension runs. Returns whether anything changed.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<bool> setDimensionDefinition(
    Document& document, DimensionId id, const DimensionDefinition& definition);

/// The dimension with @p id, or nullptr.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT const Dimension* findDimension(const Document& document,
                                                                      DimensionId id) noexcept;

/// Every dimension in @p document, in ascending ID order.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::vector<DimensionId> dimensions(const Document& document);

/// Every dimension measured in @p view, in ascending ID order.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::vector<DimensionId> dimensionsOn(const Document& document,
                                                                             ViewId view);

/// Removes the dimension with @p id. Fails if there is none.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<void> removeDimension(Document& document,
                                                                    DimensionId id);

// --- Derived (ADR-011). Computed on every call, never stored. ---

/// What the dimension measures, now, from the model as it is.
///
/// The references are resolved against the current regeneration, so a
/// dimension follows the part: change a parameter, regenerate, ask again, and
/// the answer is the new one. Nothing is cached and nothing is carried over.
///
/// A view of a COMPONENT measures the part where the solver put it: the
/// resolved geometry is moved by the component's solved transform before
/// anything is projected. That matters for the view measurements -- a
/// component turned on its side has its width along a different view axis --
/// and not for the model ones, which a rigid motion leaves alone.
///
/// Fails, with a diagnostic naming what could not be found, when a reference
/// resolves to nothing; when the two targets are not in a position the
/// measurement is defined for (two planes that are not parallel have no one
/// distance between them); or when the view cannot be drawn. It never
/// substitutes nearby geometry and never returns a number it could not work
/// out.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<MeasuredDimension> measure(
    const Document& document, DimensionId id, const BodyLookup& bodies,
    const TransformLookup& transforms = {});

} // namespace bettercad::drawing
