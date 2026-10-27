// The derived CFD result, where its currentness is decided, and the lifecycle
// of what is held for one analysis (P19-DATA-001).

#include <bettercad/cfd/CfdResult.hpp>

#include <bettercad/cfd/CfdAnalysis.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/features/Regenerator.hpp>

#include <format>
#include <utility>

namespace bettercad::cfd {

CfdResult::CfdResult(CfdResultSource source, CfdResultMetadata metadata) noexcept
    : source_(std::move(source)), metadata_(std::move(metadata)) {}

Result<CfdResult> CfdResult::create(CfdResultSource source, const meshing::VolumeMesh& mesh,
                                    CfdResultMetadata metadata) {
    // THE SOURCE MUST NAME THIS MESH, checked first: every later field will be
    // validated against `mesh`, and a result must not be checked against a mesh
    // it does not claim to belong to.
    if (!(source.mesh == mesh.mesh().stamp())) {
        return makeError(ErrorCode::InvalidArgument, "CFD result: the source stamp does not name this mesh");
    }
    if (mesh.nodeCount() == 0 || mesh.tetrahedronCount() == 0) {
        return makeError(ErrorCode::FailedPrecondition,
                         "CFD result: a mesh with no cells cannot carry a solution");
    }
    if (metadata.outerIterations == 0) {
        return makeError(ErrorCode::InvalidArgument,
                         "CFD result: an accepted result took at least one outer iteration");
    }
    if (!isWellFormed(metadata.residuals)) {
        return makeError(ErrorCode::InvalidArgument,
                         "CFD result: a residual is not finite or is negative, so it is not a norm");
    }
    if (!isWellFormed(metadata.massBalance)) {
        return makeError(ErrorCode::InvalidArgument,
                         "CFD result: a mass-balance measure is not finite or is negative");
    }

    // MODE-COMPLETE, LIKE THE DEFINITION. "No magic zero": a steady result at
    // `t = 0 s` would claim a time it does not have.
    switch (source.mode) {
    case CfdAnalysisMode::Steady:
        if (metadata.time.has_value() || metadata.step.has_value()) {
            return makeError(ErrorCode::InvalidArgument, "CFD result: a steady result has no time and no step");
        }
        break;
    case CfdAnalysisMode::Transient:
        if (!metadata.time.has_value() || !isFinite(*metadata.time) || metadata.time->si() <= 0.0) {
            return makeError(ErrorCode::InvalidArgument,
                             "CFD result: a transient sample needs a finite time after t = 0");
        }
        if (!metadata.step.has_value() || *metadata.step == 0) {
            return makeError(ErrorCode::InvalidArgument,
                             "CFD result: a transient sample needs its step, counted from 1");
        }
        break;
    default:
        return makeError(ErrorCode::InvalidArgument, "CFD result: the mode is not one this build recognises");
    }

    return CfdResult(std::move(source), std::move(metadata));
}

bool CfdResult::describes(const meshing::VolumeMesh& mesh) const noexcept {
    // STAMPS, NEVER COUNTS.
    return source_.mesh.isValid() && source_.mesh == mesh.mesh().stamp();
}

std::string_view toString(CfdFailureStage stage) noexcept {
    switch (stage) {
    case CfdFailureStage::InputRefused:
        return "input_refused";
    case CfdFailureStage::MeshRefused:
        return "mesh_refused";
    case CfdFailureStage::IllPosed:
        return "ill_posed";
    case CfdFailureStage::LinearSolveFailed:
        return "linear_solve_failed";
    case CfdFailureStage::Diverged:
        return "diverged";
    case CfdFailureStage::IterationLimitReached:
        return "iteration_limit_reached";
    case CfdFailureStage::NonFiniteField:
        return "non_finite_field";
    case CfdFailureStage::ContinuityNotClosed:
        return "continuity_not_closed";
    case CfdFailureStage::MassBalanceFailed:
        return "mass_balance_failed";
    }
    return "unknown";
}

std::string_view toString(ResultCurrency currency) noexcept {
    switch (currency) {
    case ResultCurrency::NoResult:
        return "no result";
    case ResultCurrency::Current:
        return "current";
    case ResultCurrency::Stale:
        return "stale";
    }
    return "unknown";
}

std::string_view toString(AnalysisState state) noexcept {
    switch (state) {
    case AnalysisState::NoAnalysis:
        return "no analysis";
    case AnalysisState::InputsUnavailable:
        return "inputs unavailable";
    case AnalysisState::Ready:
        return "ready";
    case AnalysisState::SolvedCurrent:
        return "solved: current";
    case AnalysisState::SolvedStale:
        return "solved: stale";
    case AnalysisState::SolveFailed:
        return "solve failed";
    }
    return "unknown";
}

Result<CfdResultSource> currentResultSource(const Document& document, const features::Regenerator& regenerator,
                                            const meshing::Mesher& mesher, CfdAnalysisId analysis) {
    const CfdAnalysis* intent = findCfdAnalysis(document, analysis);
    if (intent == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("there is no CFD analysis {}", analysis.value()));
    }

    // EVERY INPUT GATE, INHERITED RATHER THAN RE-ASKED (ADR-060).
    const Result<CfdModel> model = requireCfdModel(document, regenerator, mesher, intent->definition().mesh);
    if (!model.has_value()) {
        return std::unexpected(model.error());
    }

    // THE PRESSURE DATUM, judged by the one rule (`pressureDatumProblem`). A
    // definition may be stored without one -- it is built a condition at a time --
    // but an analysis whose pressure is determined only up to a constant has no
    // inputs a solve could use, so it is InputsUnavailable and never Ready. An
    // overdetermined datum cannot reach here: the definition refuses it.
    if (pressureDatumProblem(intent->definition()) == PressureDatumProblem::Missing) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("CFD analysis {}: no boundary condition fixes the pressure and there is no "
                                     "reference pressure ({})",
                                     analysis.value(), toString(PressureDatumProblem::Missing)));
    }

    // THE FLUID'S REVISION, from the material the boundary resolved -- the same
    // object, so the source names exactly what was consumed. A density edit moves
    // it; so will a viscosity edit once P19-FLUID-001 gives P15 one.
    const features::Material* fluid = features::findMaterial(document, model->materialId());
    if (fluid == nullptr) {
        // requireCfdModel has just resolved this id through the same document, so
        // this is P15's invariant checked rather than assumed, not a reachable
        // state; it is not claimed to be tested.
        return makeError(ErrorCode::Internal,
                         std::format("the fluid material {} was resolved and then not found", model->materialId()));
    }

    return CfdResultSource{
        .body = model->body(),
        .control = model->control(),
        .geometry = model->geometryRevision(),
        .mesh = model->mesh().mesh().stamp(),
        .fluid = model->materialId(),
        .fluidRevision = fluid->revision(),
        .analysis = analysis,
        .analysisRevision = intent->revision(),
        .definition = fingerprint(intent->definition()),
        .mode = intent->definition().mode,
    };
}

ResultCurrency resultCurrency(const CfdResult* result, const CfdResultSource& current) {
    if (result == nullptr) {
        return ResultCurrency::NoResult;
    }
    return result->source() == current ? ResultCurrency::Current : ResultCurrency::Stale;
}

AnalysisState analysisState(const Document& document, const features::Regenerator& regenerator,
                            const meshing::Mesher& mesher, CfdAnalysisId analysis, const CfdResult* lastResult,
                            const CfdSolveFailure* lastFailure) {
    if (findCfdAnalysis(document, analysis) == nullptr) {
        return AnalysisState::NoAnalysis;
    }
    if (lastFailure != nullptr) {
        // A FAILED ATTEMPT IS ITS OWN STATE, and never a current one.
        return AnalysisState::SolveFailed;
    }
    const Result<CfdResultSource> current = currentResultSource(document, regenerator, mesher, analysis);
    if (!current.has_value()) {
        return AnalysisState::InputsUnavailable;
    }
    switch (resultCurrency(lastResult, *current)) {
    case ResultCurrency::NoResult:
        return AnalysisState::Ready;
    case ResultCurrency::Current:
        return AnalysisState::SolvedCurrent;
    case ResultCurrency::Stale:
        return AnalysisState::SolvedStale;
    }
    return AnalysisState::InputsUnavailable;
}

void CfdResultSlot::recordSuccess(CfdResult result) {
    result_ = std::move(result);
    failure_.reset();
}

void CfdResultSlot::recordFailure(CfdSolveFailure failure) {
    // THE RESULT IS NOT TOUCHED. Not replaced, not cleared, not re-stamped.
    failure_ = std::move(failure);
}

void CfdResultSlot::reset() noexcept {
    result_.reset();
    failure_.reset();
}

} // namespace bettercad::cfd
