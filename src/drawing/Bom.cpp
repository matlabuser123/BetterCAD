#include <bettercad/drawing/Bom.hpp>

#include <bettercad/assembly/Component.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/drawing/Views.hpp>

#include <algorithm>
#include <format>
#include <numeric>
#include <utility>

namespace bettercad::drawing {

std::size_t BillOfMaterials::totalOccurrences() const noexcept {
    return std::accumulate(rows.begin(), rows.end(), std::size_t{0},
                           [](std::size_t running, const BomRow& row) {
                               return running + row.occurrences.size();
                           });
}

Result<BillOfMaterials> billOfMaterials(const Document& document, ViewId view) {
    // The occurrences are the view's -- which is P13's active set, asked now
    // (ADR-021). There is no second answer to what is in the assembly.
    auto occurrences = drawnOccurrences(document, view);
    if (!occurrences) {
        return std::unexpected(occurrences.error());
    }

    BillOfMaterials bom;
    for (const ComponentId occurrence : *occurrences) {
        const auto* component = document.findObjectAs<assembly::Component>(occurrence);
        if (component == nullptr) {
            return makeError(ErrorCode::NotFound,
                             std::format("{} draws {}, which is not a component of this document",
                                         view, occurrence));
        }
        const ObjectReference& part = component->definition().part;

        // GROUPED BY THE PART'S IDENTITY, and by nothing else. sameTarget
        // compares the owning document and the object ID, and deliberately
        // ignores the locator -- two references to one part that were found
        // by different paths are still one part (ADR-003).
        const auto existing = std::ranges::find_if(
            bom.rows, [&](const BomRow& row) { return sameTarget(row.part, part); });
        if (existing != bom.rows.end()) {
            existing->occurrences.push_back(occurrence);
            continue;
        }

        BomRow row;
        row.part = part;
        // The name is read from the part NOW rather than copied when the row
        // was made, so renaming the part changes what the drawing says.
        if (const DocumentObject* object = localTarget(part) ? document.findObject(part.object)
                                                             : nullptr;
            object != nullptr) {
            row.name = object->name();
        } else if (!isInternal(part)) {
            // A part in another document cannot be named from here without
            // the implicit filesystem access ADR-003 forbids. The row exists
            // and counts; what it is called is that milestone's to answer.
            row.name = std::format("{}", part.object);
        } else {
            return makeError(ErrorCode::NotFound,
                             std::format("{} places {}, which is not an object of this document",
                                         occurrence, part.object));
        }
        row.occurrences.push_back(occurrence);
        bom.rows.push_back(std::move(row));
    }

    // ORDER BEFORE NUMBER. Ascending part ObjectId: stable, independent of
    // the order the components were created in, and independent of names and
    // geometry. Sorting on the name would make renaming a part renumber the
    // drawing; sorting on nothing would leave the numbers to whatever order
    // the occurrences came back in.
    std::ranges::sort(bom.rows, [](const BomRow& a, const BomRow& b) {
        return a.part.object.value() < b.part.object.value();
    });
    int item = 1;
    for (BomRow& row : bom.rows) {
        row.item = item++;
        // Ascending ComponentId within a row, for the same reason.
        std::ranges::sort(row.occurrences,
                          [](ComponentId a, ComponentId b) { return a.value() < b.value(); });
    }
    return bom;
}

Result<int> itemNumberOf(const Document& document, ViewId view, ComponentId occurrence) {
    auto bom = billOfMaterials(document, view);
    if (!bom) {
        return std::unexpected(bom.error());
    }
    for (const BomRow& row : bom->rows) {
        if (std::ranges::find(row.occurrences, occurrence) != row.occurrences.end()) {
            return row.item;
        }
    }
    // Not "find another occurrence of the same part". An occurrence that is
    // not drawn has no item number, and a balloon on it is unresolved --
    // which is the honest answer, and the one that stops a balloon quietly
    // labelling a different instance.
    return makeError(ErrorCode::NotFound,
                     std::format("{} is not among the occurrences {} draws, so it has no item "
                                 "number; a suppressed or removed component has none",
                                 occurrence, view));
}

} // namespace bettercad::drawing
