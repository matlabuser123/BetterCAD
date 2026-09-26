#include <bettercad/features/Materials.hpp>

#include <bettercad/core/document/Document.hpp>

#include <format>
#include <utility>

namespace bettercad::features {

Result<MaterialId> createMaterial(Document& document, std::string name,
                                  const MaterialDefinition& definition) {
    // Material::create validates the definition before anything is built, and
    // Document::addObject validates the name before it allocates, so a rejected
    // material consumes no ID and leaves the document untouched.
    auto material = Material::create(std::move(name), definition);
    if (!material) {
        return std::unexpected(material.error());
    }
    auto id = document.addObject(std::move(*material));
    if (!id) {
        return std::unexpected(id.error());
    }
    return MaterialId::fromValue(id->value());
}

Result<MaterialId> importLibraryMaterial(Document& document, std::string name,
                                        const materials::LibraryMaterial& entry) {
    // A copy, not a reference (ADR-025). The document's values are its own from
    // here on, and the key is kept only as provenance.
    MaterialDefinition definition;
    definition.designation = std::string{entry.designation()};
    definition.standard = std::string{entry.standard()};
    definition.family = std::string{entry.family()};
    definition.notes = std::string{entry.notes()};
    definition.origin = entry.key();
    return createMaterial(document, std::move(name), definition);
}

Result<bool> setMaterialDefinition(Document& document, MaterialId id,
                                  const MaterialDefinition& definition) {
    if (findMaterial(document, id) == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("there is no material {}", id));
    }
    // Validated before the document is touched, so a rejected definition leaves
    // the material exactly as it was and the failure stays structured.
    if (auto valid = validateMaterialDefinition(definition); !valid) {
        return std::unexpected(valid.error());
    }
    auto changed = document.modifyObject<Material>(
        id, [&](Material& material) { return material.setDefinition(definition).value_or(false); });
    if (!changed) {
        return std::unexpected(changed.error());
    }
    return *changed;
}

const Material* findMaterial(const Document& document, MaterialId id) noexcept {
    return document.findObjectAs<Material>(id);
}

std::vector<MaterialId> materialIds(const Document& document) {
    std::vector<MaterialId> found;
    // objects() is ascending by ID, so the result is ordered without sorting and
    // without depending on any container's traversal order.
    for (const DocumentObject& object : document.objects()) {
        if (dynamic_cast<const Material*>(&object) != nullptr) {
            found.push_back(MaterialId::fromValue(object.id().value()));
        }
    }
    return found;
}

std::size_t materialCount(const Document& document) { return materialIds(document).size(); }

std::vector<MaterialId> findMaterialsByDesignation(const Document& document,
                                                   std::string_view designation) {
    std::vector<MaterialId> found;
    for (const DocumentObject& object : document.objects()) {
        const auto* material = dynamic_cast<const Material*>(&object);
        // Exact comparison. Folding case or trimming here would merge materials
        // a user meant to keep apart.
        if (material != nullptr && material->definition().designation == designation) {
            found.push_back(material->materialId());
        }
    }
    return found;
}

Result<void> removeMaterial(Document& document, MaterialId id) {
    if (findMaterial(document, id) == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("there is no material {}", id));
    }
    auto removed = document.removeObject(id);
    if (!removed) {
        return std::unexpected(removed.error());
    }
    return {};
}

} // namespace bettercad::features
