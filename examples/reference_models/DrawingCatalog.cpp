#include "DrawingReferenceModels.hpp"

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

Result<Document> buildDrawingReferenceModel(DrawingReferenceModelKind kind) {
    switch (kind) {
    case DrawingReferenceModelKind::StepPlate:
        return documentOf(buildDrawnStepPlateReferenceModel());
    case DrawingReferenceModelKind::AngleBracket:
        return documentOf(buildDrawnAngleBracketReferenceModel());
    case DrawingReferenceModelKind::PocketBlock:
        return documentOf(buildDrawnPocketBlockReferenceModel());
    case DrawingReferenceModelKind::HolePlate:
        return documentOf(buildDrawnHolePlateReferenceModel());
    case DrawingReferenceModelKind::ToleranceBlock:
        return documentOf(buildDrawnToleranceBlockReferenceModel());
    case DrawingReferenceModelKind::ClampSet:
        return documentOf(buildDrawnClampSetReferenceModel());
    case DrawingReferenceModelKind::BoltedStack:
        return documentOf(buildDrawnBoltedStackReferenceModel());
    case DrawingReferenceModelKind::GuardedFrame:
        return documentOf(buildDrawnGuardedFrameReferenceModel());
    }
    return makeError(ErrorCode::InvalidArgument, "unknown drawing reference model");
}

} // namespace bettercad::reference
