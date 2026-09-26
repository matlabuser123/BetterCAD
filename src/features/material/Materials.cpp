#include <bettercad/features/Materials.hpp>

#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/units/Format.hpp>

#include <optional>
#include <string>
#include <vector>

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

Result<bool> setMaterialMechanical(Document& document, MaterialId id,
                                   const materials::MechanicalProperties& properties) {
    const Material* material = findMaterial(document, id);
    if (material == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("there is no material {}", id));
    }
    // Only the mechanical part is replaced; everything else is carried over, so
    // there is no way for this to disturb a designation or an origin.
    MaterialDefinition definition = material->definition();
    definition.mechanical = properties;
    return setMaterialDefinition(document, id, definition);
}

namespace {

/// "Al6061T6 (object:12)" -- how a diagnostic names a material, following the
/// drawing layer's style of naming the object a failure is about.
std::string describe(const Material& material) {
    return std::format("{} ({})", material.name(), material.id());
}

} // namespace

Result<materials::LinearElasticConstants> requireLinearElasticConstants(const Document& document,
                                                                       MaterialId id) {
    const Material* material = findMaterial(document, id);
    if (material == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("there is no material {}", id));
    }
    const materials::MechanicalProperties& properties = material->definition().mechanical;

    // Every gap at once. A solver that fails at the first one makes the user
    // discover the rest one run at a time.
    std::vector<std::string> missing;
    const std::optional<ElasticModulus> modulus = properties.youngsModulus.value();
    if (!modulus) {
        missing.emplace_back("no Young's modulus");
    } else if (!isFinite(*modulus) || modulus->si() <= 0.0) {
        missing.emplace_back(
            std::format("a Young's modulus of {}, which is not a usable value", toString(*modulus)));
    }
    const std::optional<PoissonRatio> ratio = properties.poissonRatio.value();
    if (!ratio) {
        missing.emplace_back("no Poisson's ratio");
    } else if (!isFinite(*ratio) || ratio->value() <= materials::limits::minPoissonRatio
               || ratio->value() >= materials::limits::maxPoissonRatio) {
        missing.emplace_back(std::format(
            "a Poisson's ratio of {}, which is outside {} < nu < {}", ratio->value(),
            materials::limits::minPoissonRatio, materials::limits::maxPoissonRatio));
    }

    if (!missing.empty()) {
        std::string joined;
        for (const std::string& item : missing) {
            if (!joined.empty()) {
                joined += " and ";
            }
            joined += item;
        }
        // The missing INPUTS are named, never the derived constants: "the shear
        // modulus is unavailable" tells a user nothing they can act on.
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("{} has {}, which linear elasticity needs",
                                     describe(*material), joined));
    }

    // Both derivations are available exactly when the pair above is valid, so
    // these cannot be empty here.
    materials::LinearElasticConstants constants;
    constants.youngsModulus = *modulus;
    constants.poissonRatio = *ratio;
    constants.shearModulus = *materials::derivedShearModulus(properties).value();
    constants.bulkModulus = *materials::derivedBulkModulus(properties).value();
    return constants;
}

Result<Density> requireDensity(const Document& document, MaterialId id) {
    const Material* material = findMaterial(document, id);
    if (material == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("there is no material {}", id));
    }
    const materials::MechanicalProperties& properties = material->definition().mechanical;
    const std::optional<Density> density = properties.density.value();
    if (!density) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("{} has no density", describe(*material)));
    }
    if (!isFinite(*density) || density->si() <= 0.0) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("{} has a density of {}, which is not a usable value",
                                     describe(*material), toString(*density)));
    }
    return *density;
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
