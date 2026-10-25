#include <bettercad/thermal/ThermalAnalysisRun.hpp>

#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/thermal/ThermalAnalysis.hpp>

#include <utility>

// P18-CLI-001. See the header: this is the ORDER of the qualified links and nothing else. Every function called below is
// another milestone's, with that milestone's own arguments; there is no arithmetic in this file, no comparison against a
// quantity, no constant, and no branch that decides acceptance.
namespace bettercad::thermal {

std::string_view toString(ThermalRunStage stage) noexcept {
    switch (stage) {
    case ThermalRunStage::Model:
        return "model";
    case ThermalRunStage::DofMap:
        return "dof_map";
    case ThermalRunStage::Loads:
        return "loads";
    case ThermalRunStage::Boundary:
        return "boundary";
    case ThermalRunStage::System:
        return "system";
    case ThermalRunStage::Solve:
        return "solve";
    case ThermalRunStage::Post:
        return "post";
    case ThermalRunStage::Balance:
        return "balance";
    }
    return "unknown";
}

namespace {

/// Records a refusal and finishes the run: the validator judges exactly what was built.
void stopAt(ThermalAnalysisRun& run, ThermalRunStage stage, Error error) {
    run.stoppedAt = stage;
    run.failure = std::move(error);
}

} // namespace

ThermalAnalysisRun runThermalAnalysis(const Document& document, const features::Regenerator& regenerator,
                                      const meshing::Mesher& mesher, ThermalAnalysisId analysis) {
    ThermalAnalysisRun run;
    run.analysis = analysis;

    const auto finish = [&]() {
        // The report is produced HERE, once, from the objects as they stand at their final addresses. It reads, never solves.
        run.validation = validateThermalAnalysis(document, regenerator, mesher, analysis, run.derivedState());
        return std::move(run);
    };

    const ThermalAnalysis* object = findThermalAnalysis(document, analysis);
    if (object == nullptr) {
        stopAt(run, ThermalRunStage::Model,
               Error{ErrorCode::NotFound, "the document has no thermal analysis with that id"});
        return finish();
    }
    // A COPY: the chain reads one coherent definition even if a caller edits the document between two of its links.
    const ThermalAnalysisDefinition definition = object->definition();
    run.mode = definition.mode;

    Result<ThermalModel> model = requireThermalModel(document, regenerator, mesher, definition.mesh);
    if (!model.has_value()) {
        stopAt(run, ThermalRunStage::Model, model.error());
        return finish();
    }

    Result<ThermalDofMap> map = buildThermalDofMap(model->mesh().mesh());
    if (!map.has_value()) {
        stopAt(run, ThermalRunStage::DofMap, map.error());
        return finish();
    }
    run.dofMap = std::move(*map);

    Result<PreparedThermalLoads> loads = prepareThermalLoads(*model, *run.dofMap, definition);
    if (!loads.has_value()) {
        stopAt(run, ThermalRunStage::Loads, loads.error());
        return finish();
    }
    run.loads = std::move(*loads);

    Result<PreparedThermalBoundary> boundary = prepareThermalBoundary(*model, *run.dofMap, definition);
    if (!boundary.has_value()) {
        stopAt(run, ThermalRunStage::Boundary, boundary.error());
        return finish();
    }
    run.boundary = std::move(*boundary);

    Result<ThermalSystem> system =
        assembleThermalSystem(document, *model, *run.dofMap, *run.loads, *run.boundary, definition);
    if (!system.has_value()) {
        stopAt(run, ThermalRunStage::System, system.error());
        return finish();
    }
    run.system = std::move(*system);

    if (definition.mode == ThermalAnalysisMode::Steady) {
        Result<SteadyThermalSolution> solved =
            solveSteadyThermal(document, *model, *run.dofMap, *run.system, *run.boundary, definition.loads,
                               definition.boundaryConditions, definition.solver);
        if (!solved.has_value()) {
            stopAt(run, ThermalRunStage::Solve, solved.error());
            return finish();
        }
        run.steady = std::move(*solved);

        Result<ThermalPostResult> post =
            recoverSteadyThermal(document, *model, *run.dofMap, *run.steady, definition.loads,
                                 definition.boundaryConditions, definition.solver);
        if (!post.has_value()) {
            stopAt(run, ThermalRunStage::Post, post.error());
            return finish();
        }
        run.post = std::move(*post);

        Result<ThermalBalanceResult> balance =
            recoverSteadyBalance(document, *model, *run.dofMap, *run.system, *run.steady, *run.loads, *run.boundary,
                                 definition.loads, definition.boundaryConditions, definition.solver);
        if (!balance.has_value()) {
            stopAt(run, ThermalRunStage::Balance, balance.error());
            return finish();
        }
        run.balance = std::move(*balance);
        return finish();
    }

    // A TRANSIENT definition carries both of these by construction (`validate(ThermalAnalysisDefinition)` is mode-complete and
    // `ThermalAnalysis::create` and `setDefinition` run it), so this cannot be reached from a document. It is checked anyway,
    // because dereferencing an empty optional is not a refusal anyone could read.
    if (!definition.initialCondition.has_value() || !definition.transient.has_value()) {
        stopAt(run, ThermalRunStage::Solve,
               Error{ErrorCode::Internal, "a transient analysis lacks its initial temperature or its time settings"});
        return finish();
    }

    Result<TransientThermalSolution> solved =
        solveTransientThermal(document, *model, *run.dofMap, *run.system, *run.boundary, definition);
    if (!solved.has_value()) {
        stopAt(run, ThermalRunStage::Solve, solved.error());
        return finish();
    }
    run.transient = std::move(*solved);

    Result<ThermalPostResult> post = recoverTransientThermal(
        document, *model, *run.dofMap, *run.transient, definition.loads, definition.boundaryConditions,
        *definition.initialCondition, *definition.transient, definition.solver);
    if (!post.has_value()) {
        stopAt(run, ThermalRunStage::Post, post.error());
        return finish();
    }
    run.post = std::move(*post);

    Result<ThermalBalanceResult> balance = recoverTransientBalance(
        document, *model, *run.dofMap, *run.system, *run.transient, *run.loads, *run.boundary, definition.loads,
        definition.boundaryConditions, *definition.initialCondition, *definition.transient, definition.solver);
    if (!balance.has_value()) {
        stopAt(run, ThermalRunStage::Balance, balance.error());
        return finish();
    }
    run.balance = std::move(*balance);
    return finish();
}

} // namespace bettercad::thermal
