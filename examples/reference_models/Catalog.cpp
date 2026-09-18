#include "ReferenceModels.hpp"

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

Result<Document> buildReferenceModel(ReferenceModelKind kind) {
    switch (kind) {
    case ReferenceModelKind::Shaft:
        return documentOf(buildShaftReferenceModel());
    case ReferenceModelKind::Flange:
        return documentOf(buildFlangeReferenceModel());
    case ReferenceModelKind::Pulley:
        return documentOf(buildPulleyReferenceModel());
    case ReferenceModelKind::BearingHousing:
        return documentOf(buildBearingHousingReferenceModel());
    case ReferenceModelKind::MountingBracket:
        return documentOf(buildMountingBracketReferenceModel());
    case ReferenceModelKind::UBolt:
        return documentOf(buildUBoltReferenceModel());
    case ReferenceModelKind::MotorMount:
        return documentOf(buildMotorMountReferenceModel());
    case ReferenceModelKind::GearboxCover:
        return documentOf(buildGearboxCoverReferenceModel());
    case ReferenceModelKind::ManifoldTube:
        return documentOf(buildManifoldTubeReferenceModel());
    case ReferenceModelKind::TransitionDuct:
        return documentOf(buildTransitionDuctReferenceModel());
    case ReferenceModelKind::IndexPlate:
        return documentOf(buildIndexPlateReferenceModel());
    case ReferenceModelKind::RibbedBracket:
        return documentOf(buildRibbedBracketReferenceModel());
    }
    return makeError(ErrorCode::InvalidArgument, "unknown reference model");
}

} // namespace bettercad::reference
