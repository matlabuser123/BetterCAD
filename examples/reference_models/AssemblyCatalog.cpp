#include "AssemblyReferenceModels.hpp"

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

Result<Document> buildAssemblyReferenceModel(AssemblyReferenceModelKind kind) {
    switch (kind) {
    case AssemblyReferenceModelKind::GroundedPair:
        return documentOf(buildGroundedPairReferenceModel());
    case AssemblyReferenceModelKind::ConstrainedStack:
        return documentOf(buildConstrainedStackReferenceModel());
    case AssemblyReferenceModelKind::ShaftInBore:
        return documentOf(buildShaftInBoreReferenceModel());
    case AssemblyReferenceModelKind::JointSet:
        return documentOf(buildJointSetReferenceModel());
    case AssemblyReferenceModelKind::ConfiguredFrame:
        return documentOf(buildConfiguredFrameReferenceModel());
    case AssemblyReferenceModelKind::DrivenCover:
        return documentOf(buildDrivenCoverReferenceModel());
    case AssemblyReferenceModelKind::Machine:
        return documentOf(buildMachineReferenceModel());
    case AssemblyReferenceModelKind::FaultCases:
        return documentOf(buildFaultCasesReferenceModel());
    }
    return makeError(ErrorCode::InvalidArgument, "unknown assembly reference model");
}

} // namespace bettercad::reference
