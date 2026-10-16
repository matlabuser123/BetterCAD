// The derived thermal result, where its currentness is decided, and the
// lifecycle of a held result (P18-DATA-001).

#include <bettercad/thermal/ThermalResult.hpp>

#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/thermal/ThermalAnalysis.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace bettercad::thermal {
namespace {

/// Where @p node sits in @p mesh's enumeration, or npos. Binary search, and
/// it is correct only because `Mesh` guarantees nodes are stored in ascending
/// NodeId -- the guarantee this relies on instead of assuming dense IDs.
[[nodiscard]] std::size_t positionOf(const meshing::Mesh& mesh, meshing::NodeId node) noexcept {
    const std::span<const meshing::Node> nodes = mesh.nodes();
    const auto found = std::ranges::lower_bound(
        nodes, node, {}, [](const meshing::Node& candidate) { return candidate.id; });
    if (found == nodes.end() || found->id != node) {
        return static_cast<std::size_t>(-1);
    }
    return static_cast<std::size_t>(found - nodes.begin());
}

} // namespace

ThermalResult::ThermalResult(ThermalResultSource source, std::vector<Temperature> temperatures,
                             std::vector<TemperatureGradient3> gradients,
                             std::vector<HeatFlux3> heatFluxes, HeatBalance heatBalance,
                             std::optional<Time> time) noexcept
    : source_(source)
    , temperatures_(std::move(temperatures))
    , gradients_(std::move(gradients))
    , heatFluxes_(std::move(heatFluxes))
    , heatBalance_(heatBalance)
    , time_(time) {}

Result<ThermalResult> ThermalResult::create(ThermalResultSource source,
                                            const meshing::VolumeMesh& mesh,
                                            std::vector<Temperature> temperatures,
                                            std::vector<TemperatureGradient3> gradients,
                                            std::vector<HeatFlux3> heatFluxes,
                                            HeatBalance heatBalance, std::optional<Time> time) {
    // THE SOURCE MUST NAME THIS MESH. Checked first, because every other check
    // is against `mesh` and would otherwise validate the result against a mesh
    // it does not claim to belong to.
    if (!(source.mesh == mesh.mesh().stamp())) {
        return makeError(ErrorCode::InvalidArgument,
                         "thermal result: the source stamp does not name this mesh");
    }

    const std::size_t nodes = mesh.nodeCount();
    const std::size_t tetrahedra = mesh.tetrahedronCount();
    if (nodes == 0 || tetrahedra == 0) {
        return makeError(ErrorCode::FailedPrecondition,
                         "thermal result: a mesh with no nodes or no elements cannot carry a "
                         "solution");
    }
    if (temperatures.size() != nodes) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("thermal result: {} temperatures for {} nodes",
                                     temperatures.size(), nodes));
    }
    if (gradients.size() != tetrahedra || heatFluxes.size() != tetrahedra) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("thermal result: {} gradients and {} heat fluxes for {} "
                                     "tetrahedra",
                                     gradients.size(), heatFluxes.size(), tetrahedra));
    }

    for (std::size_t i = 0; i < temperatures.size(); ++i) {
        // THE SAME RULE EVERY INPUT TEMPERATURE PASSES. A field with a node
        // below 0 K is not a solution of a conduction problem whose every
        // datum was absolute; refusing it here turns a wrong solve into a
        // reported failure instead of a displayed temperature.
        if (!isAbsoluteTemperature(temperatures[i])) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("thermal result: the temperature of node {} is {} K, "
                                         "which is not a finite absolute temperature",
                                         mesh.mesh().nodes()[i].id.value(), temperatures[i].si()));
        }
    }
    for (std::size_t i = 0; i < gradients.size(); ++i) {
        if (!isFinite(gradients[i])) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("thermal result: the gradient of element {} is not finite",
                                         i));
        }
        if (!isFinite(heatFluxes[i])) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("thermal result: the heat flux of element {} is not finite",
                                         i));
        }
    }
    if (!isFinite(heatBalance)) {
        return makeError(ErrorCode::InvalidArgument,
                         "thermal result: a heat balance term is not finite");
    }

    // MODE-COMPLETE, LIKE THE DEFINITION. A steady field has no time and no
    // capacity term; a transient sample has both. "No magic zero": a steady
    // result with `storedEnergyRate = 0 W` would claim a term that does not
    // exist, and a transient one without it would have no balance to check.
    switch (source.mode) {
    case ThermalAnalysisMode::Steady:
        if (time.has_value()) {
            return makeError(ErrorCode::InvalidArgument,
                             "thermal result: a steady result has no time");
        }
        if (heatBalance.storedEnergyRate.has_value()) {
            return makeError(ErrorCode::InvalidArgument,
                             "thermal result: a steady result has no stored-energy rate; the "
                             "term does not exist and is not zero");
        }
        break;
    case ThermalAnalysisMode::Transient:
        if (!time.has_value() || !isFinite(*time) || time->si() < 0.0) {
            return makeError(ErrorCode::InvalidArgument,
                             "thermal result: a transient result needs a finite, non-negative "
                             "time");
        }
        if (!heatBalance.storedEnergyRate.has_value()) {
            return makeError(ErrorCode::InvalidArgument,
                             "thermal result: a transient result needs a stored-energy rate in "
                             "its balance");
        }
        break;
    }

    return ThermalResult(source, std::move(temperatures), std::move(gradients),
                         std::move(heatFluxes), heatBalance, time);
}

bool ThermalResult::describes(const meshing::VolumeMesh& mesh) const noexcept {
    // STAMPS, NEVER COUNTS.
    return source_.mesh.isValid() && source_.mesh == mesh.mesh().stamp();
}

Result<Temperature> ThermalResult::temperatureOf(const meshing::VolumeMesh& mesh,
                                                 meshing::NodeId node) const {
    if (!describes(mesh)) {
        return makeError(ErrorCode::FailedPrecondition,
                         "thermal result: this result was not computed on that mesh");
    }
    const std::size_t position = positionOf(mesh.mesh(), node);
    if (position == static_cast<std::size_t>(-1) || position >= temperatures_.size()) {
        return makeError(ErrorCode::NotFound,
                         std::format("thermal result: node {} is not in this mesh", node.value()));
    }
    return temperatures_[position];
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

Result<ThermalResultSource> currentResultSource(const Document& document,
                                                const features::Regenerator& regenerator,
                                                const meshing::Mesher& mesher,
                                                ThermalAnalysisId analysis) {
    const ThermalAnalysis* intent = findThermalAnalysis(document, analysis);
    if (intent == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("there is no thermal analysis {}", analysis.value()));
    }

    // EVERY INPUT GATE, INHERITED RATHER THAN RE-ASKED (ADR-051).
    const Result<ThermalModel> model =
        requireThermalModel(document, regenerator, mesher, intent->definition().mesh);
    if (!model.has_value()) {
        return std::unexpected(model.error());
    }

    // The material's identity and revision, which the input boundary has no
    // reason to carry: it needs k, and currentness needs to know whether the
    // material k came from has since been edited.
    const Result<const features::Material*> material = features::requireEffectiveMaterial(document);
    if (!material.has_value()) {
        return std::unexpected(material.error());
    }

    return ThermalResultSource{
        .body = model->body(),
        .control = model->control(),
        .geometry = model->geometryRevision(),
        .mesh = model->mesh().mesh().stamp(),
        .material = (*material)->materialId(),
        .materialRevision = (*material)->revision(),
        .analysis = analysis,
        .analysisRevision = intent->revision(),
        .mode = intent->definition().mode,
    };
}

ResultCurrency resultCurrency(const ThermalResult* result, const ThermalResultSource& current) {
    if (result == nullptr) {
        return ResultCurrency::NoResult;
    }
    return result->source() == current ? ResultCurrency::Current : ResultCurrency::Stale;
}

AnalysisState analysisState(const Document& document, const features::Regenerator& regenerator,
                            const meshing::Mesher& mesher, ThermalAnalysisId analysis,
                            const ThermalResult* lastResult, const Error* lastFailure) {
    if (findThermalAnalysis(document, analysis) == nullptr) {
        return AnalysisState::NoAnalysis;
    }
    if (lastFailure != nullptr) {
        // A FAILED ATTEMPT IS ITS OWN STATE, whether or not an older result
        // survives it, and it is never a current one.
        return AnalysisState::SolveFailed;
    }

    const Result<ThermalResultSource> current = currentResultSource(document, regenerator, mesher, analysis);
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

void ThermalResultSlot::recordSuccess(ThermalResult result) {
    result_ = std::move(result);
    failure_.reset();
}

void ThermalResultSlot::recordFailure(Error failure) {
    // THE RESULT IS NOT TOUCHED. Not replaced, not cleared, not re-stamped.
    failure_ = std::move(failure);
}

void ThermalResultSlot::reset() noexcept {
    result_.reset();
    failure_.reset();
}

} // namespace bettercad::thermal
