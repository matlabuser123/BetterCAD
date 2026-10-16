// The canonical thermal analysis, as a document object (P18-DATA-001).

#include <bettercad/thermal/ThermalAnalysisObject.hpp>

#include <bettercad/core/document/Document.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace bettercad::thermal {

Result<void> validate(const ThermalAnalysisDefinition& definition) {
    if (!definition.mesh.isValid()) {
        return makeError(ErrorCode::InvalidArgument,
                         "thermal analysis: the invalid handle cannot name the mesh to analyse");
    }
    switch (definition.mode) {
    case ThermalAnalysisMode::Steady:
    case ThermalAnalysisMode::Transient:
        break;
    default:
        return makeError(ErrorCode::InvalidArgument,
                         "thermal analysis: the mode is not one this build recognises");
    }
    // THE SETTINGS, THROUGH THEIR OWN VALIDATORS. Not a second copy of the
    // rules: each `validate` owns what a usable value is, and the same
    // function is the one the solve will use.
    if (const Result<void> settings = validate(definition.solver); !settings.has_value()) {
        return std::unexpected(settings.error());
    }

    // EVERY LOAD AND EVERY BOUNDARY CONDITION, EACH ON ITS OWN TERMS, and the
    // identities unique within their collection. The order is kept as given;
    // what is refused is two records claiming to be the same one.
    for (std::size_t i = 0; i < definition.loads.size(); ++i) {
        if (const Result<void> load = validate(definition.loads[i]); !load.has_value()) {
            return std::unexpected(load.error());
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (definition.loads[j].id() == definition.loads[i].id()) {
                return makeError(ErrorCode::InvalidArgument,
                                 std::format("thermal analysis: {} appears twice among the loads",
                                             definition.loads[i].id()));
            }
        }
    }
    for (std::size_t i = 0; i < definition.boundaryConditions.size(); ++i) {
        if (const Result<void> condition = validate(definition.boundaryConditions[i]);
            !condition.has_value()) {
            return std::unexpected(condition.error());
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (definition.boundaryConditions[j].id() == definition.boundaryConditions[i].id()) {
                return makeError(
                    ErrorCode::InvalidArgument,
                    std::format("thermal analysis: {} appears twice among the boundary conditions",
                                definition.boundaryConditions[i].id()));
            }
        }
    }

    // MODE-COMPLETE. A steady analysis carries nothing it would ignore; a
    // transient one carries everything it needs. See the header for why an
    // inert field is refused rather than tolerated.
    if (definition.mode == ThermalAnalysisMode::Steady) {
        if (definition.initialCondition.has_value()) {
            return makeError(ErrorCode::InvalidArgument,
                             "thermal analysis: a steady analysis has no initial condition; the "
                             "field would be inert intent whose edit stales a result for nothing");
        }
        if (definition.transient.has_value()) {
            return makeError(ErrorCode::InvalidArgument,
                             "thermal analysis: a steady analysis has no time step or end time");
        }
        return Result<void>{};
    }
    if (!definition.initialCondition.has_value()) {
        return makeError(ErrorCode::InvalidArgument,
                         "thermal analysis: a transient analysis needs an initial temperature");
    }
    if (const Result<void> initial = validate(*definition.initialCondition); !initial.has_value()) {
        return std::unexpected(initial.error());
    }
    if (!definition.transient.has_value()) {
        return makeError(ErrorCode::InvalidArgument,
                         "thermal analysis: a transient analysis needs a time step and an end time");
    }
    if (const Result<void> transient = validate(*definition.transient); !transient.has_value()) {
        return std::unexpected(transient.error());
    }
    return Result<void>{};
}

Result<std::unique_ptr<ThermalAnalysis>> ThermalAnalysis::create(std::string name,
                                                                 ThermalAnalysisDefinition definition) {
    if (const Result<void> checked = validate(definition); !checked.has_value()) {
        return std::unexpected(checked.error());
    }
    return std::unique_ptr<ThermalAnalysis>{new ThermalAnalysis{std::move(name), std::move(definition)}};
}

ThermalAnalysis::ThermalAnalysis(std::string name, ThermalAnalysisDefinition definition)
    : DocumentObject(std::move(name)), definition_(std::move(definition)) {}

std::unique_ptr<DocumentObject> ThermalAnalysis::clone() const {
    // THE COPY CONSTRUCTOR, because a clone must carry the ID.
    //
    // `AddObjectCommand::undo` keeps the removed object and redo re-inserts
    // `clone()`, and `insertObject` refuses an object with no ID. A clone
    // built from (name, definition) would be a fresh object with no ID, so an
    // added analysis could never be redone. P16-CMD-001 found this by probe;
    // P17-DATA-001 inherited it; so does this.
    return std::unique_ptr<DocumentObject>(new ThermalAnalysis(*this));
}

bool ThermalAnalysis::contentEquals(const DocumentObject& other) const {
    const auto* analysis = dynamic_cast<const ThermalAnalysis*>(&other);
    // Semantic equality of the INTENT, which is what "undo restored exactly
    // the previous state" has to mean. The definition is a value type with a
    // defaulted operator==, and there is nothing derived in it to exclude.
    return analysis != nullptr && definition_ == analysis->definition_;
}

std::vector<ObjectId> ThermalAnalysis::dependencies() const {
    if (!definition_.mesh.isValid()) {
        return {};
    }
    // The meshing control, so the document's graph knows that editing the
    // control -- or the body under it -- reaches this analysis.
    return {ObjectId::fromValue(definition_.mesh.value())};
}

Result<bool> ThermalAnalysis::setDefinition(ThermalAnalysisDefinition definition) {
    if (const Result<void> checked = validate(definition); !checked.has_value()) {
        return std::unexpected(checked.error());
    }
    if (definition == definition_) {
        // NO EFFECTIVE CHANGE, so no revision moves and no result is
        // invalidated. The contract MeshControl::setDefinition established and
        // StructuralAnalysis::setDefinition kept.
        return false;
    }
    definition_ = std::move(definition);
    return true;
}

const ThermalAnalysis* findThermalAnalysis(const Document& document, ThermalAnalysisId id) noexcept {
    for (const DocumentObject& object : document.objects()) {
        if (object.id().value() == id.value()) {
            // A dynamic_cast rather than a trusted ID: an object of another
            // kind carrying this number -- a structural analysis, say -- is
            // not a thermal analysis, and answering nullptr is what makes its
            // ID fail here instead of quietly working.
            return dynamic_cast<const ThermalAnalysis*>(&object);
        }
    }
    return nullptr;
}

std::vector<ThermalAnalysisId> thermalAnalysisIds(const Document& document) {
    std::vector<ThermalAnalysisId> analyses;
    for (const DocumentObject& object : document.objects()) {
        if (dynamic_cast<const ThermalAnalysis*>(&object) != nullptr) {
            analyses.push_back(ThermalAnalysisId::fromValue(object.id().value()));
        }
    }
    std::ranges::sort(analyses, [](ThermalAnalysisId a, ThermalAnalysisId b) {
        return a.value() < b.value();
    });
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
    case StaleReason::Mode:
        return "mode";
    }
    return "unknown";
}

std::vector<StaleReason> staleReasons(const ThermalResultSource& result,
                                      const ThermalResultSource& current) {
    // EVERY MISMATCH, NOT THE FIRST, in the enum's order so that the answer is
    // the same in every preset and on every run.
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
    if (result.material != current.material || result.materialRevision != current.materialRevision) {
        reasons.push_back(StaleReason::Material);
    }
    if (result.analysis != current.analysis || result.analysisRevision != current.analysisRevision) {
        reasons.push_back(StaleReason::Analysis);
    }
    if (result.mode != current.mode) {
        reasons.push_back(StaleReason::Mode);
    }
    return reasons;
}

} // namespace bettercad::thermal
