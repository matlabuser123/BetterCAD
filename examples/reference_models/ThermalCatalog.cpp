#include "ThermalReferenceModels.hpp"

#include <utility>

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

Result<Document> buildThermalReferenceModel(ThermalReferenceModelKind kind) {
    switch (kind) {
    case ThermalReferenceModelKind::Patch:
        return documentOf(buildThermalPatchReferenceModel());
    case ThermalReferenceModelKind::Slab:
        return documentOf(buildThermalSlabReferenceModel());
    case ThermalReferenceModelKind::Generation:
        return documentOf(buildThermalGenerationReferenceModel());
    case ThermalReferenceModelKind::Tube:
        return documentOf(buildThermalTubeReferenceModel());
    case ThermalReferenceModelKind::Convection:
        return documentOf(buildThermalConvectionReferenceModel());
    case ThermalReferenceModelKind::TransformedBase:
        return documentOf(buildThermalTransformedBaseReferenceModel());
    case ThermalReferenceModelKind::TransformedPlaced:
        return documentOf(buildThermalTransformedPlacedReferenceModel());
    case ThermalReferenceModelKind::LocalBaseline:
        return documentOf(buildThermalLocalRefinementReferenceModel(ThermalLocalRefinement::None));
    case ThermalReferenceModelKind::LocalTarget:
        return documentOf(buildThermalLocalRefinementReferenceModel(ThermalLocalRefinement::Start));
    case ThermalReferenceModelKind::LocalMirror:
        return documentOf(buildThermalLocalRefinementReferenceModel(ThermalLocalRefinement::End));
    case ThermalReferenceModelKind::Lumped:
        return documentOf(buildThermalLumpedReferenceModel());
    case ThermalReferenceModelKind::InsulatedGeneration:
        return documentOf(buildThermalInsulatedGenerationReferenceModel());
    case ThermalReferenceModelKind::SurfaceFlux:
        return documentOf(buildThermalSurfaceFluxReferenceModel());
    case ThermalReferenceModelKind::TransientSlab:
        return documentOf(buildThermalTransientSlabReferenceModel());
    case ThermalReferenceModelKind::Singular:
        return documentOf(buildThermalSingularReferenceModel());
    case ThermalReferenceModelKind::SingularBalanced:
        return documentOf(buildThermalSingularBalancedReferenceModel());
    case ThermalReferenceModelKind::StaleMesh:
        return documentOf(buildThermalStaleMeshReferenceModel());
    case ThermalReferenceModelKind::UnresolvedLoad:
        return documentOf(buildThermalUnresolvedReferenceModel(ThermalUnresolvedTarget::Load));
    case ThermalReferenceModelKind::UnresolvedTemperature:
        return documentOf(buildThermalUnresolvedReferenceModel(ThermalUnresolvedTarget::Temperature));
    case ThermalReferenceModelKind::UnresolvedConvection:
        return documentOf(buildThermalUnresolvedReferenceModel(ThermalUnresolvedTarget::Convection));
    }
    return makeError(ErrorCode::InvalidArgument, "unknown thermal reference model");
}

} // namespace bettercad::reference
