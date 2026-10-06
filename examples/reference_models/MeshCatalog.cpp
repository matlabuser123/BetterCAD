#include "MeshReferenceModels.hpp"

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

Result<Document> buildMeshReferenceModel(MeshReferenceModelKind kind) {
    switch (kind) {
    case MeshReferenceModelKind::Block:
        return documentOf(buildMeshBlockReferenceModel());
    case MeshReferenceModelKind::Cylinder:
        return documentOf(buildMeshCylinderReferenceModel());
    case MeshReferenceModelKind::PlateWithHole:
        return documentOf(buildMeshPlateWithHoleReferenceModel());
    case MeshReferenceModelKind::Tube:
        return documentOf(buildMeshTubeReferenceModel());
    case MeshReferenceModelKind::ThinPlate:
        return documentOf(buildMeshThinPlateReferenceModel());
    case MeshReferenceModelKind::TransformedBase:
        return documentOf(buildMeshTransformedBaseReferenceModel());
    case MeshReferenceModelKind::TransformedPlaced:
        return documentOf(buildMeshTransformedPlacedReferenceModel());
    case MeshReferenceModelKind::LocalRefinement:
        return documentOf(buildMeshLocalRefinementReferenceModel());
    case MeshReferenceModelKind::OpenProfile:
        return documentOf(buildMeshOpenProfileReferenceModel());
    }
    return makeError(ErrorCode::InvalidArgument, "unknown mesh reference model");
}

} // namespace bettercad::reference
