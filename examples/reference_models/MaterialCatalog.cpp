#include "MaterialReferenceModels.hpp"

namespace bettercad::reference {

namespace {

template <typename Model>
Result<Document> documentOf(Result<Model> model) {
    if (!model) {
        return std::unexpected(model.error());
    }
    return std::move(model->document);
}

} // namespace

Result<Document> buildMaterialReferenceModel(MaterialReferenceModelKind kind) {
    switch (kind) {
    case MaterialReferenceModelKind::Block:
        return documentOf(buildMaterialBlockReferenceModel());
    case MaterialReferenceModelKind::Shaft:
        return documentOf(buildMaterialShaftReferenceModel());
    case MaterialReferenceModelKind::Tube:
        return documentOf(buildMaterialTubeReferenceModel());
    case MaterialReferenceModelKind::PartA:
        return documentOf(buildMaterialPartAReferenceModel());
    case MaterialReferenceModelKind::PartB:
        return documentOf(buildMaterialPartBReferenceModel());
    case MaterialReferenceModelKind::Assembly:
        return documentOf(buildMaterialAssemblyReferenceModel());
    case MaterialReferenceModelKind::Custom:
        return documentOf(buildMaterialCustomReferenceModel());
    case MaterialReferenceModelKind::Incomplete:
        return documentOf(buildMaterialIncompleteReferenceModel());
    }
    return makeError(ErrorCode::InvalidArgument, "unknown material reference model");
}

} // namespace bettercad::reference
