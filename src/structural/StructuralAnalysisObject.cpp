// The canonical structural analysis, as a document object (P17-DATA-001).

#include <bettercad/structural/StructuralAnalysisObject.hpp>

#include <bettercad/core/document/Document.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace bettercad::structural {

Result<void> validate(const StructuralAnalysisDefinition& definition) {
    if (!definition.mesh.isValid()) {
        return makeError(ErrorCode::InvalidArgument,
                         "structural analysis: the invalid handle cannot name the mesh to analyse");
    }
    return Result<void>{};
}

Result<std::unique_ptr<StructuralAnalysis>>
StructuralAnalysis::create(std::string name, StructuralAnalysisDefinition definition) {
    if (const Result<void> checked = validate(definition); !checked.has_value()) {
        return std::unexpected(checked.error());
    }
    return std::unique_ptr<StructuralAnalysis>{
        new StructuralAnalysis{std::move(name), std::move(definition)}};
}

StructuralAnalysis::StructuralAnalysis(std::string name, StructuralAnalysisDefinition definition)
    : DocumentObject(std::move(name)), definition_(std::move(definition)) {}

std::unique_ptr<DocumentObject> StructuralAnalysis::clone() const {
    // THE COPY CONSTRUCTOR, because a clone must carry the ID.
    //
    // `AddObjectCommand::undo` keeps the removed object and redo re-inserts
    // `clone()`, and `insertObject` refuses an object with no ID. A clone built
    // from (name, definition) would be a fresh object with no ID, so an added
    // analysis could never be redone. P16-CMD-001 found this by probe and
    // wrote it down; it is inherited here rather than rediscovered.
    return std::unique_ptr<DocumentObject>(new StructuralAnalysis(*this));
}

bool StructuralAnalysis::contentEquals(const DocumentObject& other) const {
    const auto* analysis = dynamic_cast<const StructuralAnalysis*>(&other);
    // Semantic equality of the INTENT, which is what "undo restored exactly the
    // previous state" has to mean. The definition is a value type with a
    // defaulted operator==, and there is nothing derived in it to exclude.
    return analysis != nullptr && definition_ == analysis->definition_;
}

std::vector<ObjectId> StructuralAnalysis::dependencies() const {
    if (!definition_.mesh.isValid()) {
        return {};
    }
    // The meshing control, so the document's graph knows that editing the
    // control -- or the body under it -- reaches this analysis.
    return {ObjectId::fromValue(definition_.mesh.value())};
}

Result<bool> StructuralAnalysis::setDefinition(StructuralAnalysisDefinition definition) {
    if (const Result<void> checked = validate(definition); !checked.has_value()) {
        return std::unexpected(checked.error());
    }
    if (definition == definition_) {
        // NO EFFECTIVE CHANGE, so no revision moves and no result is
        // invalidated. Re-setting the same value is not an edit, and treating
        // it as one would stale a solve for nothing. The contract
        // MeshControl::setDefinition established.
        return false;
    }
    definition_ = std::move(definition);
    return true;
}

const StructuralAnalysis* findStructuralAnalysis(const Document& document, AnalysisId id) noexcept {
    for (const DocumentObject& object : document.objects()) {
        if (object.id().value() == id.value()) {
            // A dynamic_cast rather than a trusted ID: an object of another
            // kind carrying this number is not an analysis, and answering
            // nullptr is what makes a sketch's ID fail here instead of
            // quietly working.
            return dynamic_cast<const StructuralAnalysis*>(&object);
        }
    }
    return nullptr;
}

std::vector<AnalysisId> structuralAnalysisIds(const Document& document) {
    std::vector<AnalysisId> analyses;
    for (const DocumentObject& object : document.objects()) {
        if (dynamic_cast<const StructuralAnalysis*>(&object) != nullptr) {
            analyses.push_back(AnalysisId::fromValue(object.id().value()));
        }
    }
    std::ranges::sort(analyses, [](AnalysisId a, AnalysisId b) { return a.value() < b.value(); });
    return analyses;
}

std::string_view toString(StaleReason reason) noexcept {
    switch (reason) {
    case StaleReason::Body:
        return "body";
    case StaleReason::Control:
        return "mesh_control";
    case StaleReason::Geometry:
        return "geometry";
    case StaleReason::Mesh:
        return "mesh";
    case StaleReason::Material:
        return "material";
    case StaleReason::Analysis:
        return "analysis";
    }
    return "unknown";
}

std::vector<StaleReason> staleReasons(const StructuralResultSource& result,
                                      const StructuralResultSource& current) {
    // EVERY MISMATCH, NOT THE FIRST. A user who changed the geometry and the
    // material should be told both, and the order is the enum's so that the
    // answer is the same in every preset and on every run.
    std::vector<StaleReason> reasons;
    if (result.body != current.body) {
        reasons.push_back(StaleReason::Body);
    }
    if (result.control != current.control) {
        reasons.push_back(StaleReason::Control);
    }
    if (!(result.geometry == current.geometry)) {
        reasons.push_back(StaleReason::Geometry);
    }
    if (!(result.mesh == current.mesh)) {
        reasons.push_back(StaleReason::Mesh);
    }
    if (result.material != current.material ||
        result.materialRevision != current.materialRevision) {
        reasons.push_back(StaleReason::Material);
    }
    if (result.analysis != current.analysis ||
        result.analysisRevision != current.analysisRevision) {
        reasons.push_back(StaleReason::Analysis);
    }
    return reasons;
}

} // namespace bettercad::structural
