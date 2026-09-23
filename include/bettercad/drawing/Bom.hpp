#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/ObjectReference.hpp>
#include <bettercad/drawing/Export.hpp>

#include <cstddef>
#include <string>
#include <vector>

// The bill of materials of an assembly drawing (P14-BOM-001, on ADR-011,
// ADR-017 and ADR-022).
//
// NOTHING HERE IS STORED. Every row, every quantity and every item number is
// computed from the assembly as it is now, on every call. That is not an
// optimisation left undone: a quantity is not intent. Nobody DECIDES that
// there are four brackets -- there are four brackets because four occurrences
// of the bracket are active, and storing that number would create a second
// answer to a question the assembly has already answered. The two can then
// differ, silently, on an issued drawing.
//
// WHAT MAKES TWO OCCURRENCES ONE ROW is the identity of the part they place:
// `ComponentDefinition::part`, an ObjectReference. Never the display name,
// never the geometry, never a hash of the current shape, and never the order
// anything was traversed in. Two separately defined parts that happen to be
// identical boxes are two rows.
//
// WHAT A ROW KEEPS is every occurrence grouped into it, not just how many.
// A quantity-only row would be anonymous count data, and a balloon could not
// get back from it to the instance it labels.
namespace bettercad::drawing {

/// One line of a bill of materials.
struct BomRow {
    /// 1, 2, 3 ... unique within the BOM, assigned over a deterministic order
    /// (ascending part ObjectId). Contiguous and recomputed every time, so
    /// removing the last occurrence of item 2 makes the old item 3 into
    /// item 2 -- see ADR-022 on why numbers are compact rather than retained.
    int item = 0;
    /// The part definition every occurrence in this row places. THE grouping
    /// key.
    ObjectReference part{};
    /// The part object's name, for the table to print. DERIVED: renaming the
    /// part changes what the drawing says, because the name was never copied
    /// into the BOM.
    std::string name{};
    /// Every ACTIVE occurrence grouped here, in ascending ComponentId order.
    /// The row keeps them rather than counting them, so provenance survives
    /// grouping.
    std::vector<ComponentId> occurrences{};

    /// How many there are. Not a stored field: a quantity that could disagree
    /// with the list it counts is the whole failure this design avoids.
    [[nodiscard]] std::size_t quantity() const noexcept { return occurrences.size(); }

    friend bool operator==(const BomRow&, const BomRow&) = default;
};

/// What an assembly drawing lists.
struct BillOfMaterials {
    /// In ascending part ObjectId, which is also item-number order.
    std::vector<BomRow> rows{};

    /// The total number of active occurrences, across every row. Equal to the
    /// size of the view's occurrence set, which is what makes it worth
    /// asserting: if grouping ever dropped or duplicated an occurrence, this
    /// would stop matching.
    [[nodiscard]] BETTERCAD_DRAWING_EXPORT std::size_t totalOccurrences() const noexcept;

    friend bool operator==(const BillOfMaterials&, const BillOfMaterials&) = default;
};

/// The bill of materials of what @p view draws, computed now.
///
/// The occurrences are `drawnOccurrences(document, view)` -- the active
/// configuration and both layers of suppression, asked of P13 rather than
/// reimplemented -- grouped by the identity of the part each one places.
///
/// Fails when @p view does not draw the assembly (a BOM of one object is not
/// a bill of materials), when the view does not exist, and when an occurrence
/// places a part this document does not have.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<BillOfMaterials> billOfMaterials(
    const Document& document, ViewId view);

/// The item number @p occurrence carries in @p view's bill of materials.
///
/// This is the whole of a balloon's meaning: the number is a function of the
/// occurrence, resolved through the row its part groups into. A balloon that
/// stored a number could show the right occurrence and the wrong figure.
///
/// Fails with NotFound when the occurrence is not among the ones the view
/// draws -- which is what a suppressed or deleted occurrence is, and is why a
/// balloon on one becomes unresolved rather than quietly labelling another
/// instance of the same part.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<int> itemNumberOf(const Document& document,
                                                                 ViewId view,
                                                                 ComponentId occurrence);

} // namespace bettercad::drawing
