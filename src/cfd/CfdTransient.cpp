// The transient incompressible solve: the time grid, the march, the per-step
// convergence loop and the atomic publication of a history
// (P19-TRANSIENT-001, ADR-078).
//
// Every numerical step below belongs to an operator that was qualified before
// this milestone -- the transient momentum term included, which is ADR-073's.
// What is here is the grid, the order the operators run in, the per-step
// acceptance, the sampling, and the publication.
//
// THE HELPERS AND THE STEP BODY ARE DELIBERATELY NOT SHARED WITH
// `CfdSteady.cpp`, AND `ADR-078` DECISION 4 IS WHERE THE ARGUMENT IS. P18 made
// the same split for the same reason; a steady analysis is not a one-step
// transient, because the steady momentum form has no `rho V / dt` at all.
// `flattenPressureGradient` below is the one function that is a near copy, and
// the cost is recorded rather than hidden: the ORDER of five operator calls now
// appears in two files, and the mitigation is the large-dt steady limit, which
// is a measurement at one operating point and not a proof of equivalence
// (`ADR-078` L2).

#include <bettercad/cfd/CfdTransient.hpp>

#include <bettercad/cfd/CfdAnalysis.hpp>
#include <bettercad/cfd/CfdBoundary.hpp>
#include <bettercad/cfd/CfdConvection.hpp>
#include <bettercad/cfd/CfdFields.hpp>
#include <bettercad/cfd/CfdGeometry.hpp>
#include <bettercad/cfd/CfdGradient.hpp>
#include <bettercad/cfd/CfdInitialField.hpp>
#include <bettercad/cfd/CfdMesh.hpp>
#include <bettercad/cfd/CfdMomentum.hpp>
#include <bettercad/cfd/CfdSteady.hpp>
#include <bettercad/cfd/CfdTopology.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/meshing/Mesher.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bettercad::cfd {
namespace {

/// A diagnostic for a failure record rather than for a `Result`'s error arm,
/// the same shape as `CfdSteady.cpp`'s and `CfdCoupling.cpp`'s.
[[nodiscard]] Error errorOf(ErrorCode code, std::string message) {
    return Error{code, std::move(message)};
}

/// `grad p` per cell, flattened out of the reconstructed field in `CellIndex`
/// order, which is the order and the length `CfdMomentumInput` documents.
///
/// A NEAR COPY OF `CfdSteady.cpp`'s, AND `ADR-078` DECISION 4 ACCEPTED THAT.
/// Sharing it would mean either a public helper neither header wants or an
/// edit to a file qualified under `P19-STEADY-001`. What is duplicated is the
/// flattening of a qualified operator's output, not the reconstruction itself.
[[nodiscard]] Result<std::vector<PressureGradient3>> flattenPressureGradient(const CfdTopology& topology,
                                                                            const CfdGeometry& geometry,
                                                                            const PreparedCfdBoundary& boundary,
                                                                            std::span<const double> pressure) {
    const CfdGradientInput input{.method = CfdGradientMethod::LeastSquares, .cellValue = pressure};
    Result<CfdPressureGradientField> field = reconstructCfdPressureGradient(topology, geometry, boundary, input);
    if (!field.has_value()) {
        return std::unexpected(field.error());
    }
    std::vector<PressureGradient3> out;
    out.reserve(topology.cells());
    for (std::size_t c = 0; c < topology.cells(); ++c) {
        const std::optional<CfdPressureGradientField::Vector> one = field->at(CellIndex::fromValue(c));
        if (!one.has_value()) {
            // Unreachable through this call -- the field is built from this
            // topology -- but `at` is total by design and a silent default here
            // would be a zero gradient masquerading as a measured one.
            return makeError(
                ErrorCode::Internal,
                std::format("CFD transient: the reconstructed pressure gradient has no value at cell {}", c));
        }
        out.push_back(*one);
    }
    return out;
}

/// Which residuals grew past the divergence factor, named with both numbers.
///
/// ARCHITECTURE.md: "A diagnostic says what failed, where, why and which object
/// caused it." All four criteria are in hand at the point of refusal, so the
/// message names the ones that grew and what they grew from, rather than
/// telling a reader that one of four did.
[[nodiscard]] std::string residualsThatGrew(const CfdResiduals& first, const CfdResiduals& current,
                                            const CfdConvergenceSettings& settings) {
    const std::array<std::pair<std::string_view, std::pair<double, double>>, 4> criteria{
        std::pair<std::string_view, std::pair<double, double>>{"momentum x", {first.momentumX, current.momentumX}},
        std::pair<std::string_view, std::pair<double, double>>{"momentum y", {first.momentumY, current.momentumY}},
        std::pair<std::string_view, std::pair<double, double>>{"momentum z", {first.momentumZ, current.momentumZ}},
        std::pair<std::string_view, std::pair<double, double>>{"continuity", {first.continuity, current.continuity}},
    };
    std::string grew;
    for (const auto& [name, values] : criteria) {
        const auto [start, now] = values;
        if (std::isfinite(start) && start > 0.0 && now > settings.divergenceFactor * start) {
            if (!grew.empty()) {
                grew += ", ";
            }
            grew += std::format("{} {} from {}", name, now, start);
        }
    }
    return grew;
}

} // namespace

// ---------------------------------------------------------------------------
// The time grid
// ---------------------------------------------------------------------------

Result<CfdTimeGrid> CfdTimeGrid::create(Time timeStep, Time endTime) {
    if (!isFinite(timeStep) || timeStep.si() <= 0.0) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("CFD time grid: the time step must be finite and positive, not {} s",
                                     timeStep.si()));
    }
    if (!isFinite(endTime) || endTime.si() <= 0.0) {
        // A ZERO END TIME IS REFUSED HERE AND ACCEPTED BY P18's GRID, and the
        // header says why: a grid of no steps has nothing a CFD run could
        // publish, because `CfdResult::create` requires a transient sample to
        // carry a time greater than zero and a step of at least one.
        return makeError(ErrorCode::InvalidArgument,
                         std::format("CFD time grid: the end time must be finite and positive, not {} s",
                                     endTime.si()));
    }

    const double dt = timeStep.si();
    const double end = endTime.si();
    const double ratio = end / dt;
    // `!(ratio <= max)` also refuses an overflowed or NaN quotient, which the
    // two finiteness gates above cannot produce but a future caller might. The
    // guard is on the COUNT and the step is never enlarged to fit.
    if (!(ratio <= static_cast<double>(kMaxCfdTimeSteps))) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("CFD time grid: an end time of {} s at a step of {} s needs {} steps, more "
                                     "than the limit of {}; the step is not enlarged to fit",
                                     end, dt, ratio, kMaxCfdTimeSteps));
    }

    CfdTimeGrid grid;
    grid.nominal_ = timeStep;
    grid.end_ = endTime;

    const double nearest = std::round(ratio);
    if (nearest >= 1.0 && std::abs(ratio - nearest) <= kCfdTimeGridTolerance * nearest) {
        // `endTime` is a whole number of steps: every step is exactly `dt` and
        // the last time reported is `endTime` itself.
        grid.steps_ = static_cast<std::size_t>(nearest);
        return grid;
    }

    // Otherwise the last step is the remainder, so the march lands on `endTime`
    // exactly rather than overshooting or truncating. `floor(ratio)` is 0 when
    // `dt > endTime`, which gives one step of `endTime` -- the case
    // `validate(CfdTransientSettings)` refuses at the analysis level and this
    // grid answers totally (see the header).
    const double full = std::floor(ratio);
    grid.remainder_ = Time::fromSi(end - full * dt);
    grid.steps_ = static_cast<std::size_t>(full) + 1;
    grid.partial_ = true;
    return grid;
}

Time CfdTimeGrid::timeAt(std::size_t index) const noexcept {
    if (index == 0) {
        return Time{};
    }
    if (index >= steps_) {
        // The last time is `endTime` EXACTLY, never `N * dt`, which is the
        // whole point of the shortened final step.
        return end_;
    }
    // A MULTIPLICATION AND NEVER A RUNNING SUM: `t_k` must not depend on how
    // many additions preceded it, or the grid would not be deterministic in the
    // sense the checklist asks for.
    return Time::fromSi(static_cast<double>(index) * nominal_.si());
}

Time CfdTimeGrid::stepAt(std::size_t index) const noexcept {
    if (index >= steps_) {
        return Time{};
    }
    if (partial_ && index + 1 == steps_) {
        return remainder_;
    }
    return nominal_;
}

Time CfdTimeGrid::integratedSpan() const noexcept {
    if (steps_ == 0) {
        return Time{};
    }
    if (partial_) {
        return Time::fromSi(static_cast<double>(steps_ - 1) * nominal_.si() + remainder_.si());
    }
    return Time::fromSi(static_cast<double>(steps_) * nominal_.si());
}

std::vector<Time> CfdTimeGrid::times() const {
    std::vector<Time> out(steps_ + 1);
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = timeAt(i);
    }
    return out;
}

// ---------------------------------------------------------------------------
// The march
// ---------------------------------------------------------------------------

std::expected<CfdTransientReport, CfdSolveFailure>
solveCfdTransient(const Document& document, const features::Regenerator& regenerator, const meshing::Mesher& mesher,
                  CfdAnalysisId analysis, const CfdLinearSolve& solve, CfdResultSlot& slot) {
    // ONE EXIT FOR EVERY REFUSAL, so there is no path that returns without
    // publishing a failure. `source` and `iterations` are empty until they are
    // known, which is what `CfdSolveFailure` documents.
    std::optional<CfdResultSource> source;
    std::optional<std::uint32_t> iterations;
    const auto refuse = [&slot, &source, &iterations](CfdFailureStage stage, Error error) {
        CfdSolveFailure failure{
            .stage = stage, .error = std::move(error), .source = source, .outerIterations = iterations};
        slot.recordFailure(failure);
        return std::unexpected(std::move(failure));
    };

    // --- 1. the analysis and its mode ----------------------------------------
    const CfdAnalysis* object = findCfdAnalysis(document, analysis);
    if (object == nullptr) {
        return refuse(CfdFailureStage::InputRefused,
                      errorOf(ErrorCode::NotFound, std::format("there is no CFD analysis {}", analysis.value())));
    }
    const CfdAnalysisDefinition& definition = object->definition();
    if (definition.mode != CfdAnalysisMode::Transient) {
        // THE MIRROR OF `solveCfdSteady`'s REFUSAL. A steady analysis has no
        // time step and no end time, so there is no history to produce; and
        // marching one would publish a transient result -- a `time` and a
        // `step` -- for an analysis whose intent is a single converged state.
        return refuse(CfdFailureStage::InputRefused,
                      errorOf(ErrorCode::FailedPrecondition,
                              std::format("CFD analysis {} is {}, and a transient solve needs a transient analysis",
                                          analysis.value(), toString(definition.mode))));
    }

    // --- 2. the settings, READ OUT OF THE DEFINITION -------------------------
    // Not parameters: `canonicalText` writes all four transient fields into the
    // definition fingerprint that `CfdResultSource::definition` carries, so a
    // run driven by anything else would publish provenance it does not have.
    // `ADR-077` Decision 7, inherited unchanged.
    //
    // THIS GUARD IS KEPT AND NO `validate` CALL IS ADDED, exactly as in
    // `solveCfdSteady` and for the same two reasons. `validate(definition)`
    // already requires a transient analysis to carry transient settings, so
    // this cannot fire -- but what follows it is `*definition.transient`, and
    // dereferencing an empty optional is undefined behaviour rather than a
    // wrong diagnostic. Re-validating the settings, by contrast, could not fire
    // for any definition a Document can hold (`CfdAnalysis::create` and
    // `setDefinition` are the only writers and both validate), and an
    // unreachable guard in front of a `validate` call is dead code.
    if (!definition.transient.has_value()) {
        return refuse(CfdFailureStage::InputRefused,
                      errorOf(ErrorCode::FailedPrecondition,
                              std::format("CFD analysis {} is transient and carries no transient settings, so it "
                                          "has no time step and no end time",
                                          analysis.value())));
    }
    const CfdIterationSettings& iterationSettings = definition.iteration;
    const CfdConvergenceSettings& convergenceSettings = definition.convergence;
    const CfdLinearSolverSettings& linearSolverSettings = definition.linearSolver;
    const CfdTransientSettings& transientSettings = *definition.transient;

    // THE PER-STEP BUDGET REUSES THE QUALIFIED PREDICATE (ADR-078 Decision 6).
    // `cfdCouplingBudgetExhausted` takes a `CfdSteadySettings`, which reads
    // oddly here; what it buys is that "exhausted" has ONE definition in the
    // tree, boundary included (`completed >= maxIterations`, so a budget of 1
    // permits exactly one iteration).
    const CfdSteadySettings stepBudget{.maxIterations = transientSettings.maxIterationsPerStep};

    // --- 3. the time grid ----------------------------------------------------
    const Result<CfdTimeGrid> grid = CfdTimeGrid::create(transientSettings.timeStep, transientSettings.endTime);
    if (!grid.has_value()) {
        return refuse(CfdFailureStage::InputRefused, grid.error());
    }
    if (grid->stepCount() == 0) {
        // UNREACHABLE THROUGH A VALID DEFINITION and kept anyway, for the same
        // reason as the optional above: `validate(CfdTransientSettings)`
        // requires `endTime >= timeStep > 0`, so the ratio is at least 1 and
        // the grid has at least one step. What this guards is a `.back()` on
        // the sample vector, which would be undefined behaviour rather than a
        // wrong answer.
        return refuse(CfdFailureStage::InputRefused,
                      errorOf(ErrorCode::FailedPrecondition,
                              std::format("CFD transient: the time grid of CFD analysis {} has no steps, so there "
                                          "is nothing to march",
                                          analysis.value())));
    }

    // --- 4. the model: every document, mesh and fluid gate, inherited --------
    const Result<CfdModel> model = requireCfdModel(document, regenerator, mesher, definition.mesh);
    if (!model.has_value()) {
        return refuse(CfdFailureStage::InputRefused, model.error());
    }

    // --- 5. the provenance, from the ONE authority that builds it ------------
    Result<CfdResultSource> current = currentResultSource(document, regenerator, mesher, analysis);
    if (!current.has_value()) {
        return refuse(CfdFailureStage::InputRefused, current.error());
    }
    source = *current;

    // --- 6. the finite-volume view of the mesh -------------------------------
    const Result<CfdMeshContract> contract = requireCfdMesh(*model);
    if (!contract.has_value()) {
        return refuse(CfdFailureStage::MeshRefused, contract.error());
    }
    const Result<CfdTopology> topology = requireCfdTopology(*model, *contract);
    if (!topology.has_value()) {
        return refuse(CfdFailureStage::MeshRefused, topology.error());
    }
    const Result<CfdGeometry> geometry = requireCfdGeometry(*model, *topology);
    if (!geometry.has_value()) {
        return refuse(CfdFailureStage::MeshRefused, geometry.error());
    }
    const Result<CfdFieldMap> layout = requireCfdFieldMap(*model, *topology);
    if (!layout.has_value()) {
        return refuse(CfdFailureStage::MeshRefused, layout.error());
    }

    // --- 7. the boundary and the initial condition, PREPARED ONCE ------------
    // ADR-078 Decision 7: nothing a definition can hold varies with time, so
    // one preparation drives every step. This is not an optimisation; it is the
    // only thing the canonical data can mean. `prepareCfdBoundary` takes no
    // time argument, and there is none to give it.
    const Result<PreparedCfdBoundary> boundary = prepareCfdBoundary(*model, *topology, *geometry, definition);
    if (!boundary.has_value()) {
        return refuse(CfdFailureStage::InputRefused, boundary.error());
    }
    const Result<PreparedCfdInitialField> initial = prepareCfdInitialField(*model, *topology, definition);
    if (!initial.has_value()) {
        return refuse(CfdFailureStage::InputRefused, initial.error());
    }

    // --- 8. the fields at t = 0, owned by this call (ADR-068) ----------------
    std::vector<double> pressure(layout->size(CfdVariable::Pressure), 0.0);
    std::vector<double> velocity(layout->size(CfdVariable::Velocity), 0.0);
    if (const Result<void> expanded = expandCfdInitialField(*initial, CfdVariable::Pressure, pressure);
        !expanded.has_value()) {
        return refuse(CfdFailureStage::InputRefused, expanded.error());
    }
    if (const Result<void> expanded = expandCfdInitialField(*initial, CfdVariable::Velocity, velocity);
        !expanded.has_value()) {
        return refuse(CfdFailureStage::InputRefused, expanded.error());
    }
    // KEPT AS THE t = 0 STATE, which the report publishes as a plain array and
    // not as a result: no system was solved to produce it (ADR-078 Decision 3).
    const std::vector<double> initialPressure = pressure;
    const std::vector<double> initialVelocity = velocity;
    // phi is NOT expanded: it is derived from U by the qualified mass-flux
    // operator on every iteration, and an initial condition for it would be a
    // second authority for the same quantity.
    std::vector<double> massFlux;

    // --- 9. the stored-sample budget, CHECKED BEFORE MARCHING ----------------
    // Refused rather than silently reduced to fewer samples: quietly
    // downgrading `EveryStep` to `FinalOnly` would publish a history the
    // definition did not ask for under a fingerprint that says it did.
    const std::size_t sampleCount =
        transientSettings.sampling == CfdTimeSampling::EveryStep ? grid->stepCount() : 1;
    const std::size_t valuesPerSample = layout->size(CfdVariable::Pressure) + layout->size(CfdVariable::Velocity) +
                                        layout->size(CfdVariable::MassFlux);
    if (valuesPerSample != 0 && sampleCount > kMaxCfdStoredValues / valuesPerSample) {
        return refuse(CfdFailureStage::InputRefused,
                      errorOf(ErrorCode::InvalidArgument,
                              std::format("CFD transient: {} samples of {} values each would store more than the "
                                          "limit of {}; the sampling policy is not reduced to fit",
                                          sampleCount, valuesPerSample, kMaxCfdStoredValues)));
    }

    // --- 10. the march -------------------------------------------------------
    const Density density = model->fluid().density();
    const DynamicViscosity viscosity = model->fluid().viscosity();
    std::vector<CfdTransientStepRecord> stepRecords;
    stepRecords.reserve(grid->stepCount());
    std::vector<CfdResult> samples;
    samples.reserve(sampleCount);
    std::vector<Time> sampleTimes;
    sampleTimes.reserve(sampleCount);
    std::uint64_t totalOuterIterations = 0;
    // U^n. Assigned at the top of every step and FROZEN for that step's inner
    // loop. See the header: this array and `velocity` are the pair that the
    // type system cannot tell apart.
    std::vector<double> previousVelocity;

    for (std::size_t step = 0; step < grid->stepCount(); ++step) {
        const Time stepSize = grid->stepAt(step);
        const Time stepTime = grid->timeAt(step + 1);

        // a. FREEZE U^n. The copy is the point: `velocity` is overwritten by
        //    every outer iteration below, and the transient source must keep
        //    carrying the state at the START of the step. Passing the current
        //    field here converges to the correct STEADY answer with every
        //    steady assertion passing (ADR-073 Decision 4), which is why two
        //    tests exist to see it.
        previousVelocity = velocity;

        // b. this step's own divergence baseline (ADR-078 Decision 5). Reset
        //    per step, because each step is a fresh nonlinear problem posed
        //    from a new U^n and the scale its convergence is relative to is its
        //    own first iteration.
        std::optional<CfdResiduals> firstResiduals;
        CfdTransientStepRecord record{};
        record.index = static_cast<std::uint32_t>(step + 1);
        record.time = stepTime;
        record.stepSize = stepSize;
        CfdMassBalance balance{};
        bool converged = false;

        // c. the inner loop.
        for (std::uint32_t completed = 0; !cfdCouplingBudgetExhausted(completed, stepBudget);) {
            const CfdMassFluxInput fluxInput{.currentVelocity = velocity, .density = density};
            std::expected<std::vector<double>, CfdConvectionFailure> flux =
                cfdMassFlux(*topology, *geometry, *boundary, fluxInput);
            if (!flux.has_value()) {
                return refuse(CfdFailureStage::IllPosed, flux.error().error);
            }

            Result<std::vector<PressureGradient3>> gradient =
                flattenPressureGradient(*topology, *geometry, *boundary, pressure);
            if (!gradient.has_value()) {
                return refuse(CfdFailureStage::IllPosed, gradient.error());
            }

            std::vector<CfdMomentumSystem> systems;
            systems.reserve(kVectorComponents.size());
            for (const VectorComponent component : kVectorComponents) {
                CfdMomentumInput momentum;
                momentum.density = density;
                momentum.viscosity = viscosity;
                momentum.component = component;
                momentum.faceFlux = *flux;
                momentum.scheme = CfdConvectionScheme::Upwind;
                // THE TWO VELOCITY ARRAYS, AND THE WHOLE MILESTONE TURNS ON
                // THESE TWO LINES BEING THE RIGHT WAY ROUND.
                //
                //   currentVelocity  the latest outer iterate -- diffusion's
                //                    symmetry cross terms are taken from it
                //   previousVelocity U^n, the state at the start of THIS step
                //
                // They are spans of the same length in the same layout, so
                // nothing but this comment and two tests stands between them
                // and a run that converges to the steady answer while
                // reporting a history (ADR-073 Decision 4, ADR-078 Decision 8).
                momentum.currentVelocity = velocity;
                momentum.previousVelocity = previousVelocity;
                momentum.timeStep = stepSize;
                momentum.pressureGradient = *gradient;
                // Gravity is half of one treatment and this is the other half:
                // `prepareCfdBoundary` applies the hydrostatic field at a
                // prescribed-pressure facet, and omitting the body force here
                // would leave the boundary half applied and the interior half
                // absent. An adversarial review found that missing in
                // `solveCfdSteady`; it is not repeated here.
                momentum.gravity = definition.gravity;
                std::expected<CfdMomentumSystem, CfdMomentumFailure> system =
                    assembleCfdMomentum(*topology, *geometry, *boundary, momentum);
                if (!system.has_value()) {
                    return refuse(CfdFailureStage::IllPosed, system.error().error);
                }
                if (component == VectorComponent::X) {
                    // RECORDED, NOT GUARDED. `transientTermApplied` is true
                    // exactly when a time step was supplied, and this call
                    // always supplies one, so a guard could not fire for any
                    // input. A test asserts it instead.
                    record.transientTermApplied = system->summary.transientTermApplied;
                    record.smallestTransientCoefficient = system->summary.smallestTransientCoefficient;
                    record.largestTransientCoefficient = system->summary.largestTransientCoefficient;
                }
                systems.push_back(std::move(*system));
            }

            const CfdCouplingIterationInput iterationInput{.momentum = systems,
                                                           .velocity = velocity,
                                                           .pressure = pressure,
                                                           .density = density,
                                                           .iteration = iterationSettings,
                                                           .linearSolver = linearSolverSettings};
            std::expected<CfdCouplingIteration, CfdCouplingFailure> iteration =
                advanceCfdCoupling(*topology, *geometry, *boundary, iterationInput, solve);
            if (!iteration.has_value()) {
                return refuse(stageOf(iteration.error().problem), iteration.error().error);
            }

            ++completed;
            ++totalOuterIterations;
            // `CfdSolveFailure::outerIterations` is a 32-bit count. A march of
            // `kMaxCfdTimeSteps` steps at a 32-bit per-step budget can exceed
            // it, so the FAILURE report saturates while the accepted report
            // carries the 64-bit total. Saturating is honest here because the
            // field means "how far the iteration got" and the step that failed
            // is named in the diagnostic.
            iterations = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(totalOuterIterations, std::numeric_limits<std::uint32_t>::max()));

            // THERE IS NO NON-FINITE FIELD CHECK HERE, and its absence is the
            // same decision `ADR-077` Decision 4 recorded:
            // `applyCfdCouplingCorrection` already refuses non-finite corrected
            // fields as `ResultIsNotFinite`, so `advanceCfdCoupling` never
            // returns a success whose fields hold one. "Reject NaN / Inf" is
            // discharged by `stageOf`, which answers `NonFiniteField` for both
            // `ResultIsNotFinite` and `ValueNotFinite`, and by
            // `CfdResultFields::create`, which refuses a non-finite value for
            // every caller that is not this one.
            pressure = iteration->corrected.pressure;
            velocity = iteration->corrected.velocity;
            massFlux = iteration->corrected.faceFlux;

            if (!firstResiduals.has_value()) {
                firstResiduals = iteration->residuals;
            }
            balance = iteration->balance;
            // THE RECORD'S COPY WAS MISSING UNTIL AN ADVERSARIAL REVIEW FOUND
            // IT. `balance` was updated and published into the sample's
            // metadata, but `record.balance` was never assigned, so every step
            // record carried a default-constructed CfdMassBalance -- two zeros
            // where the qualified evaluator's two measures belong. "Per-step
            // mass balance" is one of this milestone's nineteen items, and it
            // was reporting zeros.
            //
            // The suite PASSED on it, which is the part worth recording: the
            // assertion was `isfinite(step.balance.maxCellContinuity)`, and
            // zero is finite. The repair asserts that each sampled step's
            // record equals the sample's own metadata, which has no tolerance
            // to be satisfied by a default.
            record.balance = balance;
            record.outerIterations = completed;
            record.firstResiduals = *firstResiduals;
            record.residuals = iteration->residuals;
            record.massBalanceDefined = iteration->summary.massBalanceDefined;
            record.closedDomain = iteration->summary.closedDomain;
            record.continuityScale = iteration->summary.continuityScale;
            record.largestPressureCorrection = iteration->summary.largestPressureCorrection;

            const CfdCouplingVerdict verdict =
                cfdCouplingVerdict(iteration->residuals, *firstResiduals, convergenceSettings);
            if (verdict == CfdCouplingVerdict::Diverged) {
                return refuse(
                    CfdFailureStage::Diverged,
                    errorOf(ErrorCode::FailedPrecondition,
                            std::format("CFD transient: step {} of {} (t = {} s) diverged at outer iteration {}, a "
                                        "residual past {} times its first-iteration value for this step: {}",
                                        step + 1, grid->stepCount(), stepTime.si(), completed,
                                        convergenceSettings.divergenceFactor,
                                        residualsThatGrew(*firstResiduals, iteration->residuals,
                                                          convergenceSettings))));
            }
            if (verdict == CfdCouplingVerdict::Converged) {
                converged = true;
                break;
            }
        }

        // d. A STEP THAT DOES NOT CONVERGE FAILS THE RUN. No retry, no smaller
        //    step, no partial history. `CfdTransientSettings` states it and P18
        //    states it; publishing the steps that did converge would be a claim
        //    about a time span that was never integrated.
        if (!converged) {
            return refuse(CfdFailureStage::IterationLimitReached,
                          errorOf(ErrorCode::FailedPrecondition,
                                  std::format("CFD transient: step {} of {} (t = {} s) did not converge in {} outer "
                                              "iterations, and the budget is {} per step; the step is not retried "
                                              "with a smaller one",
                                              step + 1, grid->stepCount(), stepTime.si(), record.outerIterations,
                                              transientSettings.maxIterationsPerStep)));
        }

        stepRecords.push_back(record);

        // e. sample, if the policy asks for this step. `EveryStep` is every
        //    step; `FinalOnly` is the last one.
        const bool wanted =
            transientSettings.sampling == CfdTimeSampling::EveryStep || step + 1 == grid->stepCount();
        if (!wanted) {
            continue;
        }
        // THE ARRAYS ARE COPIED RATHER THAN MOVED, because the march continues
        // from them. One code path for every sample, including the last.
        Result<CfdResultFields> fields = CfdResultFields::create(*layout, pressure, velocity, massFlux);
        if (!fields.has_value()) {
            return refuse(CfdFailureStage::IllPosed, fields.error());
        }
        const CfdResultMetadata metadata{.outerIterations = record.outerIterations,
                                         .residuals = record.residuals,
                                         .massBalance = balance,
                                         .time = stepTime,
                                         .step = record.index};
        Result<CfdResult> sample =
            CfdResult::create(*source, model->mesh(), *layout, metadata, std::move(*fields));
        if (!sample.has_value()) {
            return refuse(CfdFailureStage::IllPosed, sample.error());
        }
        samples.push_back(std::move(*sample));
        sampleTimes.push_back(stepTime);
    }

    // --- 11. publish ---------------------------------------------------------
    // ATOMIC OVER THE WHOLE MARCH: this is the first and only call to
    // `recordSuccess`, reached only when every step converged. The final sample
    // is the analysis's current answer (ADR-078 Decision 2) and the rest of the
    // history is the report's.
    slot.recordSuccess(samples.back());
    return CfdTransientReport{.grid = *grid,
                              .steps = std::move(stepRecords),
                              .samples = std::move(samples),
                              .sampleTimes = std::move(sampleTimes),
                              .initialPressure = initialPressure,
                              .initialVelocity = initialVelocity,
                              .sampling = transientSettings.sampling,
                              .totalOuterIterations = totalOuterIterations};
}

} // namespace bettercad::cfd
