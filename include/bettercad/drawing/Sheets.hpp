#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/drawing/Export.hpp>
#include <bettercad/drawing/Sheet.hpp>

#include <cstddef>
#include <string>
#include <vector>

// The document-facing operations on sheets (P14-SHEET-001).
//
// Free functions rather than Document members, because Document is core
// (layer 0) and drawing is layer 4: core must not know that drawings exist
// (ADR-015). They use the ordinary document object lifecycle -- addObject,
// findObjectAs, removeObject -- and add no registry, no allocator and no
// ownership of their own (ADR-010), exactly as assembly::createComponent
// does.
namespace bettercad {
class Document;
}

namespace bettercad::drawing {

/// Adds a sheet and returns its ID.
///
/// Fails, changing nothing, if the definition is malformed or the name is
/// taken. No ID is consumed by a failed call.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<SheetId> createSheet(Document& document, std::string name,
                                                                   const SheetDefinition& definition);

/// Replaces the definition of the sheet with @p id. Returns whether anything
/// changed, so the document only bumps the revision on an effective change.
///
/// This is the supported way to change a sheet's format, orientation,
/// margins, scale or title block. On failure the sheet keeps the definition
/// it had.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<bool> setSheetDefinition(Document& document, SheetId id,
                                                                       const SheetDefinition& definition);

/// The sheet with @p id, or nullptr if there is none of that ID or the object
/// of that ID is not a sheet.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT const Sheet* findSheet(const Document& document,
                                                              SheetId id) noexcept;

/// Every sheet in @p document, in ascending ID order.
///
/// Ascending ID is the sheet order. IDs are never reused, so the order is
/// stable: deleting a sheet does not renumber the others, and the sheet that
/// was third is still after the sheet that was second. A user-chosen order
/// would need an explicit field on the sheet and is not in this milestone.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::vector<SheetId> sheets(const Document& document);

/// Which sheet this is, from 1, in the order sheets() returns — the number a
/// title block shows. It is a POSITION, not identity: deleting an earlier
/// sheet changes it, and it is never the SheetId. Zero if there is no such
/// sheet.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::size_t sheetNumber(const Document& document, SheetId id);

/// How many sheets the document has — the other half of "sheet 2 of 5".
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::size_t sheetCount(const Document& document);

/// Removes the sheet with @p id. Fails if there is no such sheet.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<void> removeSheet(Document& document, SheetId id);

} // namespace bettercad::drawing
