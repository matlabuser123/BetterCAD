#include <bettercad/drawing/Sheets.hpp>

#include <bettercad/core/document/Document.hpp>

#include <format>
#include <utility>

namespace bettercad::drawing {

Result<SheetId> createSheet(Document& document, std::string name, const SheetDefinition& definition) {
    // Sheet::create validates the definition before anything is built, so a
    // rejected sheet consumes no ID and leaves the document untouched.
    auto sheet = Sheet::create(std::move(name), definition);
    if (!sheet) {
        return std::unexpected(sheet.error());
    }
    auto id = document.addObject(std::move(*sheet));
    if (!id) {
        return std::unexpected(id.error());
    }
    return SheetId::fromValue(id->value());
}

Result<bool> setSheetDefinition(Document& document, SheetId id, const SheetDefinition& definition) {
    if (findSheet(document, id) == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("there is no sheet {}", id));
    }
    // Validated before the document is touched: modifyObject would otherwise
    // see setDefinition fail and have already bumped nothing, but the error
    // would be lost. Checking here keeps the failure structured and the
    // sheet exactly as it was.
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    auto changed = document.modifyObject<Sheet>(
        id, [&](Sheet& sheet) { return sheet.setDefinition(definition).value_or(false); });
    if (!changed) {
        return std::unexpected(changed.error());
    }
    return *changed;
}

const Sheet* findSheet(const Document& document, SheetId id) noexcept {
    return document.findObjectAs<Sheet>(id);
}

std::vector<SheetId> sheets(const Document& document) {
    std::vector<SheetId> found;
    // objects() is ascending by ID, so the result is ordered without sorting
    // and without depending on any container's traversal order.
    for (const DocumentObject& object : document.objects()) {
        if (dynamic_cast<const Sheet*>(&object) != nullptr) {
            found.push_back(SheetId::fromValue(object.id().value()));
        }
    }
    return found;
}

std::size_t sheetNumber(const Document& document, SheetId id) {
    const std::vector<SheetId> all = sheets(document);
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (all[i] == id) {
            return i + 1;
        }
    }
    return 0;
}

std::size_t sheetCount(const Document& document) {
    return sheets(document).size();
}

Result<void> removeSheet(Document& document, SheetId id) {
    if (findSheet(document, id) == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("there is no sheet {}", id));
    }
    auto removed = document.removeObject(id);
    if (!removed) {
        return std::unexpected(removed.error());
    }
    return {};
}

} // namespace bettercad::drawing
