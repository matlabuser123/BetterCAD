// Result currentness, inspection and playback for a thermal result (P18-VIZ-001).
//
// NOTHING HERE COMPUTES AN ENGINEERING VALUE. Every inspection field is read from `ThermalPostResult` or from the
// canonical mesh, the currentness is the core's own comparison, and the validation badge is the core's verdict.

#include <bettercad/renderer/ThermalInspection.hpp>

#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/thermal/ThermalSolve.hpp>

#include <cmath>
#include <format>

namespace bettercad::renderer {
namespace {

/// The reasons the held result is not current for @p definition on @p model, in the core's order. The comparison
/// is the solution's own (`staleReasons` over the sources), not a rule of this file.
[[nodiscard]] std::vector<thermal::TransientStaleReason>
reasonsFor(const Document& document, const thermal::ThermalModel& model,
           const thermal::ThermalAnalysisDefinition& definition, const thermal::ThermalPostResult& post) {
    if (definition.mode == thermal::ThermalAnalysisMode::Steady) {
        const Result<thermal::SteadySolutionSource> current = thermal::currentSteadySolutionSource(
            document, model, definition.loads, definition.boundaryConditions, definition.solver);
        if (!current.has_value()) {
            return {};
        }
        return thermal::staleReasons(post.source(), thermal::postSourceOf(*current));
    }
    if (!definition.initialCondition.has_value() || !definition.transient.has_value()) {
        return {};
    }
    const Result<thermal::TransientSolutionSource> current = thermal::currentTransientSolutionSource(
        document, model, definition.loads, definition.boundaryConditions, *definition.initialCondition,
        *definition.transient, definition.solver);
    if (!current.has_value()) {
        return {};
    }
    return thermal::staleReasons(post.source(), thermal::postSourceOf(*current));
}

[[nodiscard]] bool isCurrentFor(const Document& document, const thermal::ThermalModel& model,
                                const thermal::ThermalAnalysisDefinition& definition,
                                const thermal::ThermalPostResult& post) {
    if (definition.mode == thermal::ThermalAnalysisMode::Steady) {
        return thermal::isCurrent(post, document, model, definition.loads, definition.boundaryConditions,
                                  definition.solver);
    }
    if (!definition.initialCondition.has_value() || !definition.transient.has_value()) {
        return false;
    }
    return thermal::isCurrent(post, document, model, definition.loads, definition.boundaryConditions,
                              *definition.initialCondition, *definition.transient, definition.solver);
}

} // namespace

std::string_view toString(ThermalResultState state) noexcept {
    switch (state) {
    case ThermalResultState::NoAnalysis:
        return "no_analysis";
    case ThermalResultState::InputsUnavailable:
        return "inputs_unavailable";
    case ThermalResultState::NoResult:
        return "no_result";
    case ThermalResultState::Current:
        return "current";
    case ThermalResultState::Stale:
        return "stale";
    case ThermalResultState::SolveFailed:
        return "solve_failed";
    }
    return "unknown";
}

std::string_view toString(ThermalValidationBadge badge) noexcept {
    switch (badge) {
    case ThermalValidationBadge::None:
        return "none";
    case ThermalValidationBadge::Pass:
        return "pass";
    case ThermalValidationBadge::PassWithWarnings:
        return "pass_with_warnings";
    case ThermalValidationBadge::Fail:
        return "fail";
    case ThermalValidationBadge::Stale:
        return "stale";
    }
    return "unknown";
}

std::string_view toString(ThermalFieldAvailability availability) noexcept {
    switch (availability) {
    case ThermalFieldAvailability::Available:
        return "available";
    case ThermalFieldAvailability::NoResult:
        return "no_result";
    case ThermalFieldAvailability::StaleResult:
        return "stale_result";
    case ThermalFieldAvailability::InvalidResult:
        return "invalid_result";
    case ThermalFieldAvailability::SolveFailed:
        return "solve_failed";
    }
    return "unknown";
}

ThermalDisplayState thermalDisplayState(const Document& document, const features::Regenerator& regenerator,
                                        const meshing::Mesher& mesher, ThermalAnalysisId analysis,
                                        const thermal::ThermalPostResult* post, const Error* lastFailure,
                                        const thermal::ThermalValidationReport* report) {
    ThermalDisplayState out;
    if (lastFailure != nullptr) {
        out.failure = *lastFailure;
    }

    // THE VALIDATION BADGE IS THE CORE'S VERDICT, and it is shown only while the report is current. A report that
    // no longer describes the model is shown as STALE: a green badge from a validation of a model that has since
    // been edited is the stale-result problem in a small colour.
    //
    // IT MUST BE THIS ANALYSIS'S REPORT, and about this result. A current Pass for another analysis is a green badge on
    // a picture it never judged, so a report that names another analysis (or none) shows nothing, and one whose recorded
    // result source is not the held result's is shown as stale.
    if (report != nullptr && report->analysis.has_value() && *report->analysis == analysis) {
        const bool otherResult = post != nullptr && report->resultSource.has_value() && !(*report->resultSource == post->source());
        if (!otherResult && thermal::isCurrent(*report, document, regenerator, mesher)) {
            out.validation = report->status();
            switch (report->status()) {
            case thermal::ValidationStatus::Pass:
                out.badge = ThermalValidationBadge::Pass;
                break;
            case thermal::ValidationStatus::PassWithWarnings:
                out.badge = ThermalValidationBadge::PassWithWarnings;
                break;
            case thermal::ValidationStatus::Fail:
                out.badge = ThermalValidationBadge::Fail;
                break;
            }
        } else {
            out.badge = ThermalValidationBadge::Stale;
        }
    }

    const thermal::ThermalAnalysis* object = thermal::findThermalAnalysis(document, analysis);
    if (object == nullptr) {
        out.result = ThermalResultState::NoAnalysis;
        return out;
    }
    const thermal::ThermalAnalysisDefinition definition = object->definition();
    out.mesh = mesher.currency(document, definition.mesh);

    const Result<thermal::ThermalModel> model =
        thermal::requireThermalModel(document, regenerator, mesher, definition.mesh);
    if (!model.has_value()) {
        // THE INPUTS DO NOT RESOLVE. A held result cannot be current against a model that cannot even be built,
        // and its reasons are not computable without one, so the input problem IS the reason.
        out.inputProblem = thermal::thermalInputProblem(document, regenerator, mesher, definition.mesh);
        if (lastFailure != nullptr) {
            out.result = ThermalResultState::SolveFailed;
        } else {
            out.result = post != nullptr ? ThermalResultState::Stale : ThermalResultState::InputsUnavailable;
        }
        return out;
    }

    if (post == nullptr) {
        out.result = lastFailure != nullptr ? ThermalResultState::SolveFailed : ThermalResultState::NoResult;
        return out;
    }
    if (isCurrentFor(document, *model, definition, *post)) {
        out.result = ThermalResultState::Current;
        return out;
    }
    // NOT CURRENT. A later FAILED solve does not make it current, and it does not bring the old result back: the
    // failure is reported and the result stays exactly as stale as its source says.
    out.staleReasons = reasonsFor(document, *model, definition, *post);
    out.result = lastFailure != nullptr ? ThermalResultState::SolveFailed : ThermalResultState::Stale;
    return out;
}

ThermalFieldAvailability thermalFieldAvailability(const ThermalDisplayState& state) noexcept {
    switch (state.result) {
    case ThermalResultState::Current:
        // A CURRENT RESULT WHOSE VALIDATION FAILED is not an accepted analysis. A WARNING is: it stays drawn.
        return state.badge == ThermalValidationBadge::Fail ? ThermalFieldAvailability::InvalidResult
                                                           : ThermalFieldAvailability::Available;
    case ThermalResultState::Stale:
        return ThermalFieldAvailability::StaleResult;
    case ThermalResultState::SolveFailed:
        return ThermalFieldAvailability::SolveFailed;
    case ThermalResultState::NoAnalysis:
    case ThermalResultState::InputsUnavailable:
    case ThermalResultState::NoResult:
        break;
    }
    return ThermalFieldAvailability::NoResult;
}

std::string_view thermalBanner(ThermalFieldAvailability availability) noexcept {
    switch (availability) {
    case ThermalFieldAvailability::Available:
        return "";
    case ThermalFieldAvailability::NoResult:
        return "NO RESULT";
    case ThermalFieldAvailability::StaleResult:
        return "STALE RESULT - recompute required";
    case ThermalFieldAvailability::InvalidResult:
        return "INVALID RESULT - validation failed";
    case ThermalFieldAvailability::SolveFailed:
        return "SOLVE FAILED - no current result";
    }
    return "";
}

// ---------------------------------------------------------------------------
// Inspection
// ---------------------------------------------------------------------------

namespace {

[[nodiscard]] bool inspectable(ThermalResultState state) noexcept {
    return state == ThermalResultState::Current || state == ThermalResultState::Stale ||
           state == ThermalResultState::SolveFailed;
}

} // namespace

Result<ThermalNodeInspection> inspectThermalNode(const meshing::Mesh& mesh, const thermal::ThermalDofMap& map,
                                                 const thermal::ThermalPostResult& post, std::size_t state,
                                                 meshing::NodeId node, ThermalResultState resultState) {
    if (!inspectable(resultState)) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("there is no result to inspect ({})", toString(resultState)));
    }
    // THE RESULT MUST BE ABOUT THIS MESH. A stale result of another generation would give a plausible
    // temperature for a node of the same number that is a different node.
    if (!map.describes(mesh) || !post.describes(map)) {
        return makeError(ErrorCode::FailedPrecondition,
                         "the result does not describe the mesh presented (another generation, or another count)");
    }
    if (state >= post.stateCount()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("state {} requested of a result holding {}", state, post.stateCount()));
    }
    const meshing::Node* held = mesh.findNode(node);
    if (held == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("node {} is not a node of the mesh", node.value()));
    }
    const Result<thermal::ThermalDofIndex> dof = map.dofOf(node);
    if (!dof.has_value()) {
        return std::unexpected(dof.error());
    }
    // THROUGH THE NUMBERING, never `node.value()` as an index.
    const Result<Temperature> temperature = post.temperatureOf(state, map, node);
    if (!temperature.has_value()) {
        return std::unexpected(temperature.error());
    }
    ThermalNodeInspection out;
    out.node = node;
    out.dof = *dof;
    out.position = held->position;
    out.temperature = *temperature;
    out.state = state;
    out.time = post.states()[state].time;
    out.resultState = resultState;
    return out;
}

Result<ThermalElementInspection> inspectThermalElement(const meshing::Mesh& mesh,
                                                       const thermal::ThermalPostResult& post, std::size_t state,
                                                       meshing::ElementId element, ThermalResultState resultState) {
    if (!inspectable(resultState)) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("there is no result to inspect ({})", toString(resultState)));
    }
    if (!mesh.owns(post.source().system.mesh)) {
        return makeError(ErrorCode::FailedPrecondition,
                         "the result was recovered on another mesh generation than the mesh presented");
    }
    if (state >= post.stateCount()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("state {} requested of a result holding {}", state, post.stateCount()));
    }
    const meshing::Tetrahedron* held = mesh.findTetrahedron(element);
    if (held == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("element {} is not a tetrahedron of the mesh", element.value()));
    }
    // THROUGH THE ELEMENT IDENTITY, never `element.value()` as an index.
    const std::optional<std::size_t> ordinal = post.ordinalOf(element);
    if (!ordinal.has_value()) {
        return makeError(ErrorCode::NotFound,
                         std::format("the result holds no value for element {}", element.value()));
    }
    const Result<thermal::TemperatureGradient3> gradient = post.gradientOf(state, element);
    if (!gradient.has_value()) {
        return std::unexpected(gradient.error());
    }
    const Result<thermal::HeatFlux3> flux = post.heatFluxOf(state, element);
    if (!flux.has_value()) {
        return std::unexpected(flux.error());
    }
    const Result<HeatFlux> magnitude = post.heatFluxMagnitudeOf(state, element);
    if (!magnitude.has_value()) {
        return std::unexpected(magnitude.error());
    }
    ThermalElementInspection out;
    out.element = element;
    out.ordinal = *ordinal;
    out.nodes = held->nodes;
    out.gradient = *gradient;
    out.gradientMagnitude = TemperatureGradient::fromSi(gradientDisplayNorm(*gradient));
    out.heatFlux = *flux;
    out.heatFluxMagnitude = *magnitude;
    out.state = state;
    out.time = post.states()[state].time;
    out.resultState = resultState;
    return out;
}

Result<Point3D> thermalNavigationTarget(const meshing::Mesh& mesh, const ThermalExtreme& extreme) {
    if (extreme.location == thermal::FieldLocation::Node) {
        const meshing::Node* node = mesh.findNode(extreme.node);
        if (node == nullptr) {
            return makeError(ErrorCode::NotFound,
                             std::format("node {} is not a node of the mesh, so there is nowhere to look", extreme.node.value()));
        }
        return node->position;
    }
    const meshing::Tetrahedron* tet = mesh.findTetrahedron(extreme.element);
    if (tet == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("element {} is not a tetrahedron of the mesh, so there is nowhere to look", extreme.element.value()));
    }
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    for (const meshing::NodeId id : tet->nodes) {
        const meshing::Node* corner = mesh.findNode(id);
        if (corner == nullptr) {
            return makeError(ErrorCode::NotFound, std::format("tetrahedron {} names node {} and the mesh holds no such node", extreme.element.value(), id.value()));
        }
        x += corner->position.x.si();
        y += corner->position.y.si();
        z += corner->position.z.si();
    }
    return Point3D{Length::fromSi(x * 0.25), Length::fromSi(y * 0.25), Length::fromSi(z * 0.25)};
}

// ---------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------

Result<ThermalPlayback> ThermalPlayback::of(const thermal::ThermalPostResult& post) {
    if (post.stateCount() == 0U) {
        return makeError(ErrorCode::FailedPrecondition, "the result holds no state to play");
    }
    ThermalPlayback out;
    out.source_ = post.source();
    out.transient_ = post.mode() == thermal::ThermalAnalysisMode::Transient;
    out.times_.reserve(post.stateCount());
    if (!out.transient_) {
        if (post.stateCount() != 1U) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("a steady result holds {} states; it must hold exactly one",
                                         post.stateCount()));
        }
        out.times_.push_back(0.0);
        return out;
    }
    // THE STORED TIMES, AS THEY ARE: finite and strictly increasing. A GUI that re-sorted or re-gridded them
    // would be inventing a history.
    for (std::size_t i = 0U; i < post.stateCount(); ++i) {
        const std::optional<Time>& t = post.states()[i].time;
        if (!t.has_value() || !std::isfinite(t->si())) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("state {} of a transient result has no finite time", i));
        }
        if (!out.times_.empty() && !(t->si() > out.times_.back())) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("the stored times are not strictly increasing at state {} ({} after {})", i,
                                         t->si(), out.times_.back()));
        }
        out.times_.push_back(t->si());
    }
    return out;
}

std::optional<double> ThermalPlayback::time() const noexcept { return timeOf(frame_); }

std::optional<double> ThermalPlayback::timeOf(std::size_t frame) const noexcept {
    if (!transient_ || frame >= times_.size()) {
        return std::nullopt;
    }
    return times_[frame];
}

Result<void> ThermalPlayback::select(std::size_t frame) {
    if (frame >= times_.size()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("frame {} requested of a playback holding {}", frame, times_.size()));
    }
    frame_ = frame;
    return {};
}

bool ThermalPlayback::next(bool loop) noexcept {
    if (frame_ + 1U < times_.size()) {
        ++frame_;
        return true;
    }
    if (loop && times_.size() > 1U) {
        frame_ = 0U;
        return true;
    }
    return false;
}

bool ThermalPlayback::previous(bool loop) noexcept {
    if (frame_ > 0U) {
        --frame_;
        return true;
    }
    if (loop && times_.size() > 1U) {
        frame_ = times_.size() - 1U;
        return true;
    }
    return false;
}

} // namespace bettercad::renderer
