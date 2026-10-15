#include "StructuralReferenceModels.hpp"

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

Result<Document> buildStructuralReferenceModel(StructuralReferenceModelKind kind) {
    switch (kind) {
    case StructuralReferenceModelKind::Patch:
        return documentOf(buildStructuralPatchReferenceModel());
    case StructuralReferenceModelKind::AxialBar:
        return documentOf(buildStructuralAxialBarReferenceModel());
    case StructuralReferenceModelKind::Cantilever:
        return documentOf(buildStructuralCantileverReferenceModel());
    case StructuralReferenceModelKind::PressureBlock:
        return documentOf(buildStructuralPressureBlockReferenceModel());
    case StructuralReferenceModelKind::GravityBlock:
        return documentOf(buildStructuralGravityBlockReferenceModel());
    case StructuralReferenceModelKind::TransformedBase:
        return documentOf(buildStructuralTransformedBaseReferenceModel());
    case StructuralReferenceModelKind::TransformedPlaced:
        return documentOf(buildStructuralTransformedPlacedReferenceModel());
    case StructuralReferenceModelKind::LocalRefinementRoot:
        return documentOf(buildStructuralLocalRefinementRootReferenceModel());
    case StructuralReferenceModelKind::LocalRefinementTip:
        return documentOf(buildStructuralLocalRefinementTipReferenceModel());
    case StructuralReferenceModelKind::UnderConstrained:
        return documentOf(buildStructuralUnderConstrainedReferenceModel());
    case StructuralReferenceModelKind::StaleMesh:
        return documentOf(buildStructuralStaleMeshReferenceModel());
    case StructuralReferenceModelKind::Unresolved:
        return documentOf(buildStructuralUnresolvedReferenceModel());
    }
    return makeError(ErrorCode::InvalidArgument, "unknown structural reference model");
}

} // namespace bettercad::reference
