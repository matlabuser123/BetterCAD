#include <bettercad/features/Material.hpp>

#include <format>
#include <utility>

namespace bettercad::features {

Result<void> validateMaterialDefinition(const MaterialDefinition& definition) {
    if (!definition.origin) {
        return {};
    }
    const materials::MaterialLibraryKey& origin = *definition.origin;
    if (origin.library.empty() || origin.entry.empty()) {
        return makeError(ErrorCode::InvalidArgument,
                         "a material's origin must name a library and an entry");
    }
    if (origin.revision < 1) {
        return makeError(
            ErrorCode::InvalidArgument,
            std::format("a material's origin revision must be 1 or more, not {}", origin.revision));
    }
    return {};
}

Material::Material(std::string name, MaterialDefinition definition)
    : DocumentObject(std::move(name)), definition_(std::move(definition)) {}

Result<std::unique_ptr<Material>> Material::create(std::string name,
                                                  MaterialDefinition definition) {
    if (auto valid = validateMaterialDefinition(definition); !valid) {
        return std::unexpected(valid.error());
    }
    // The name is validated by the document when the object is added, exactly
    // as every other object's is.
    return std::unique_ptr<Material>(new Material(std::move(name), std::move(definition)));
}

std::unique_ptr<DocumentObject> Material::clone() const {
    return std::unique_ptr<DocumentObject>(new Material(*this));
}

bool Material::contentEquals(const DocumentObject& other) const {
    // The base says this is called only with an object of the same typeName(),
    // and equivalent() does check that first -- but this is a public virtual, so
    // a caller can reach it directly, and a static_cast here would be undefined
    // behaviour rather than a wrong answer. Checked, as Sheet does.
    const auto* material = dynamic_cast<const Material*>(&other);
    return material != nullptr && material->definition_ == definition_;
}

Result<bool> Material::setDefinition(MaterialDefinition definition) {
    if (auto valid = validateMaterialDefinition(definition); !valid) {
        return std::unexpected(valid.error());
    }
    if (definition == definition_) {
        return false;
    }
    definition_ = std::move(definition);
    return true;
}

} // namespace bettercad::features
