// The derived CFD result, where its currentness is decided, and the lifecycle
// of what is held for one analysis (P19-DATA-001).

#include <bettercad/cfd/CfdResult.hpp>

#include <bettercad/cfd/CfdAnalysis.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/features/Regenerator.hpp>

#include <cmath>
#include <format>
#include <utility>

namespace bettercad::cfd {

CfdResultFields::CfdResultFields(std::vector<double> pressure, std::vector<double> velocity,
                                 std::vector<double> massFlux) noexcept
    : pressure_(std::move(pressure)), velocity_(std::move(velocity)), massFlux_(std::move(massFlux)) {}

Result<CfdResultFields> CfdResultFields::create(const CfdFieldMap& layout, std::vector<double> pressure,
                                                std::vector<double> velocity, std::vector<double> massFlux) {
    if (layout.cells() == 0) {
        return makeError(ErrorCode::FailedPrecondition,
                         "CFD result fields: a layout with no cells cannot carry a solution");
    }

    // EACH LENGTH NAMED INDIVIDUALLY. One message saying "a length is wrong"
    // would make a caller guess which of three arrays it built badly.
    struct Expected {
        const char* name;
        CfdVariable variable;
        std::size_t actual;
    };
    const std::array<Expected, 3> expected{
        Expected{"pressure", CfdVariable::Pressure, pressure.size()},
        Expected{"velocity", CfdVariable::Velocity, velocity.size()},
        Expected{"mass flux", CfdVariable::MassFlux, massFlux.size()},
    };
    for (const Expected& one : expected) {
        const std::size_t want = layout.size(one.variable);
        if (one.actual != want) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("CFD result fields: the {} array holds {} values and this layout wants {}",
                                         one.name, one.actual, want));
        }
    }

    // EVERY VALUE FINITE, in CellIndex then FaceIndex order so the index a
    // diagnostic names is the one a reader can look up. See the header on why
    // this is an invariant rather than a guard `solveCfdSteady` makes redundant.
    const std::array<std::pair<const char*, std::span<const double>>, 3> arrays{
        std::pair<const char*, std::span<const double>>{"pressure", pressure},
        std::pair<const char*, std::span<const double>>{"velocity", velocity},
        std::pair<const char*, std::span<const double>>{"mass flux", massFlux},
    };
    for (const auto& [name, values] : arrays) {
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (!std::isfinite(values[i])) {
                return makeError(ErrorCode::InvalidArgument,
                                 std::format("CFD result fields: {}[{}] is not finite, so this is not a solution",
                                             name, i));
            }
        }
    }

    return CfdResultFields(std::move(pressure), std::move(velocity), std::move(massFlux));
}

CfdResult::CfdResult(CfdResultSource source, CfdResultMetadata metadata, CfdResultFields fields) noexcept
    : source_(std::move(source)), metadata_(std::move(metadata)), fields_(std::move(fields)) {}

Result<CfdResult> CfdResult::create(CfdResultSource source, const meshing::VolumeMesh& mesh,
                                    const CfdFieldMap& layout, CfdResultMetadata metadata,
                                    CfdResultFields fields) {
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
    // THE LAYOUT MUST NAME THE SAME MESH. `CfdResultFields` proves its lengths
    // match SOME layout; only this says it is the layout this result claims.
    if (!(layout.mesh() == mesh.mesh().stamp())) {
        return makeError(ErrorCode::InvalidArgument,
                         "CFD result: the field layout does not describe the mesh this result names");
    }
    if (fields.cells() != layout.cells() || fields.faces() != layout.faces()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("CFD result: the fields hold {} cells and {} faces and this layout has {} and {}",
                                     fields.cells(), fields.faces(), layout.cells(), layout.faces()));
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

    return CfdResult(std::move(source), std::move(metadata), std::move(fields));
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

    // THE FLUID, AS THE BOUNDARY RESOLVED IT: the material's identity and its revision
    // at resolution, from the same CfdFluid the model holds -- so the source names
    // exactly what was consumed. A density or a viscosity edit moves the revision.

    return CfdResultSource{
        .body = model->body(),
        .control = model->control(),
        .geometry = model->geometryRevision(),
        .mesh = model->mesh().mesh().stamp(),
        .fluid = model->materialId(),
        .fluidRevision = model->fluid().revision(),
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
