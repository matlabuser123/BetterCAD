// The steady incompressible solve: the outer loop, the verdict, the mapping of
// the coupling's refusals onto the result's failure stages, and the atomic
// publication of a result (P19-STEADY-001, ADR-077).
//
// Every numerical step below belongs to an operator that was qualified before
// this milestone. What is here is the order they run in, the translation of
// their refusals into the result's vocabulary, and the publication.

#include <bettercad/cfd/CfdSteady.hpp>

#include <bettercad/cfd/CfdAnalysis.hpp>
#include <bettercad/cfd/CfdBoundary.hpp>
#include <bettercad/cfd/CfdConvection.hpp>
#include <bettercad/cfd/CfdFields.hpp>
#include <bettercad/cfd/CfdGeometry.hpp>
#include <bettercad/cfd/CfdGradient.hpp>
#include <bettercad/cfd/CfdInitialField.hpp>
#include <bettercad/cfd/CfdMesh.hpp>
#include <bettercad/cfd/CfdMomentum.hpp>
#include <bettercad/cfd/CfdTopology.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/meshing/Mesher.hpp>

#include <cmath>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bettercad::cfd {
namespace {

/// A diagnostic for a failure record rather than for a `Result`'s error arm.
///
/// `makeError` returns `std::unexpected<Error>`, which is what a `Result`
/// wants and not what `CfdSolveFailure::error` is. This is the same shape as
/// `CfdCoupling.cpp`'s own `failureOf`, for the same reason.
[[nodiscard]] Error errorOf(ErrorCode code, std::string message) {
    return Error{code, std::move(message)};
}

/// `grad p` per cell, flattened out of the reconstructed field in `CellIndex`
/// order, which is the order and the length `CfdMomentumInput` documents.
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
            // The field is built from this topology, so this cannot be reached
            // through this call -- but `at` is total by design and a silent
            // default here would be a zero gradient masquerading as a measured
            // one, which is worse than a refusal.
            return makeError(ErrorCode::Internal,
                             std::format("CFD steady: the reconstructed pressure gradient has no value at cell {}", c));
        }
        out.push_back(*one);
    }
    return out;
}

} // namespace

CfdFailureStage stageOf(CfdCouplingProblem problem) noexcept {
    // ONE SWITCH, NO DEFAULT. A seventeenth CfdCouplingProblem upstream stops
    // this translation unit compiling instead of landing in a catch-all, which
    // is the same device `kCfdCouplingProblems`'s static_assert uses one layer
    // down. Sixteen onto nine is deliberately not injective.
    //
    // TWO ASSIGNMENTS COME FROM CfdFailureStage'S OWN DOCUMENTATION rather than
    // from this author's judgement. `InputRefused` is documented as "a document,
    // mesh or fluid gate, an unresolved boundary reference, A PRESSURE DATUM
    // MISSING, UNBALANCED PRESCRIBED FLUX", so PressureLevelNotFixed and
    // PrescribedFluxUnbalanced are input refusals even though the coupling
    // discovers them while assembling.
    switch (problem) {
    case CfdCouplingProblem::SettingsInvalid:
    case CfdCouplingProblem::PressureLevelNotFixed:
    case CfdCouplingProblem::PrescribedFluxUnbalanced:
        return CfdFailureStage::InputRefused;

    case CfdCouplingProblem::GeometryDescribesAnotherTopology:
    case CfdCouplingProblem::BoundaryDescribesAnotherTopology:
    case CfdCouplingProblem::TopologyNamesAnUnknownCell:
    case CfdCouplingProblem::ValueLengthWrong:
    case CfdCouplingProblem::MomentumSystemsWrong:
    case CfdCouplingProblem::MomentumDiagonalNotPositive:
    case CfdCouplingProblem::FaceWeightUnavailable:
    case CfdCouplingProblem::PressureMatrixNotSymmetric:
    case CfdCouplingProblem::EvaluationIsInconsistent:
        return CfdFailureStage::IllPosed;

    case CfdCouplingProblem::LinearSolveRefused:
    case CfdCouplingProblem::LinearSolutionUnusable:
        return CfdFailureStage::LinearSolveFailed;

    case CfdCouplingProblem::ValueNotFinite:
    case CfdCouplingProblem::ResultIsNotFinite:
        return CfdFailureStage::NonFiniteField;
    }
    // Unreachable for any enumerator; a value outside the enum is undefined
    // behaviour already, and IllPosed is the conservative answer.
    return CfdFailureStage::IllPosed;
}

std::expected<CfdSteadyReport, CfdSolveFailure> solveCfdSteady(const Document& document,
                                                               const features::Regenerator& regenerator,
                                                               const meshing::Mesher& mesher,
                                                               CfdAnalysisId analysis,
                                                               const CfdLinearSolve& solve, CfdResultSlot& slot) {
    // ONE EXIT FOR EVERY REFUSAL, so there is no path that returns without
    // publishing. `source` and `iterations` are empty until they are known,
    // which is what CfdSolveFailure documents.
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
    if (definition.mode != CfdAnalysisMode::Steady) {
        // A TRANSIENT ANALYSIS IS NOT A STEADY ONE WITH A LOOSER BUDGET.
        // P19-TRANSIENT-001 owns time stepping, and solving one here would
        // publish a steady result for an analysis whose intent is a history.
        return refuse(CfdFailureStage::InputRefused,
                      errorOf(ErrorCode::FailedPrecondition,
                              std::format("CFD analysis {} is {}, and a steady solve needs a steady analysis",
                                          analysis.value(), toString(definition.mode))));
    }

    // --- 2. ADR-058's four settings, READ OUT OF THE DEFINITION ---------------
    // Not parameters: `canonicalText` writes every one of them into the
    // definition fingerprint that `CfdResultSource::definition` carries, so a
    // run driven by anything else would publish provenance it does not have.
    // See the header.
    // THIS ONE GUARD IS KEPT WHILE THE FOUR BELOW ARE REMOVED, and the
    // difference is deliberate: `validate` already requires a steady analysis to
    // carry steady settings, so this cannot fire either -- but what follows it is
    // `*definition.steady`, and dereferencing an empty optional is undefined
    // behaviour rather than a wrong diagnostic. `P19-LINSOLVE-001` kept exactly
    // such a guard after finding a reachable out-of-bounds read in an
    // already-qualified kernel. An unreachable guard in front of UB is cheap
    // insurance; an unreachable guard in front of a `validate` call is dead code.
    if (!definition.steady.has_value()) {
        return refuse(CfdFailureStage::InputRefused,
                      errorOf(ErrorCode::FailedPrecondition,
                              std::format("CFD analysis {} is steady and carries no steady settings, so it has "
                                          "no iteration budget",
                                          analysis.value())));
    }
    const CfdIterationSettings& iterationSettings = definition.iteration;
    const CfdConvergenceSettings& convergenceSettings = definition.convergence;
    const CfdSteadySettings& steadySettings = *definition.steady;
    const CfdLinearSolverSettings& linearSolverSettings = definition.linearSolver;

    // THE SETTINGS ARE NOT RE-VALIDATED HERE, AND FOUR validate() CALLS WERE
    // REMOVED RATHER THAN KEPT. They could not fire for any input a Document
    // can hold:
    //
    //     CfdAnalysis::create         validates the whole definition
    //     CfdAnalysis::setDefinition  validates the whole definition
    //     the private constructor     reachable only from create
    //     definition_                 assigned only by those two
    //     deserialization             there is none for CfdAnalysis yet
    //
    // and `validate(const CfdAnalysisDefinition&)` reaches `iteration`,
    // `convergence`, `linearSolver` and the steady branch's `*steady`. So every
    // definition reachable from here has already passed every one of those
    // gates, and re-asking is the pattern `currentResultSource` already names:
    // "EVERY INPUT GATE, INHERITED RATHER THAN RE-ASKED (ADR-060)".
    //
    // A TEST-ADEQUACY REVIEW IS WHAT SURFACED THIS, by proposing a mutation
    // that made the second call validate the FIRST record again -- a swap no
    // test could see, because no test can construct the input that would
    // distinguish them. The only honest resolution for an unreachable guard is
    // to remove it; you cannot write a test for one. Third occurrence in this
    // milestone, after the non-finite field loop and before this comment.
    //
    // IF A LOAD PATH IS EVER ADDED FOR CfdAnalysis IT MUST VALIDATE, and this
    // comment is where to look when it is.

    // --- 3. the model: every document, mesh and fluid gate, inherited ---------
    const Result<CfdModel> model = requireCfdModel(document, regenerator, mesher, definition.mesh);
    if (!model.has_value()) {
        return refuse(CfdFailureStage::InputRefused, model.error());
    }

    // --- 4. the provenance, from the ONE authority that builds it -------------
    // Not hand-assembled here: `currentResultSource` is what `resultCurrency`
    // and `analysisState` compare against, and a second builder would be a
    // second answer that drifts. It also applies the pressure-datum rule, which
    // is the reachable failure at this point.
    Result<CfdResultSource> current = currentResultSource(document, regenerator, mesher, analysis);
    if (!current.has_value()) {
        return refuse(CfdFailureStage::InputRefused, current.error());
    }
    source = *current;

    // --- 5. the finite-volume view of the mesh -------------------------------
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

    // --- 6. the boundary and the initial condition ----------------------------
    const Result<PreparedCfdBoundary> boundary =
        prepareCfdBoundary(*model, *topology, *geometry, definition);
    if (!boundary.has_value()) {
        return refuse(CfdFailureStage::InputRefused, boundary.error());
    }
    const Result<PreparedCfdInitialField> initial = prepareCfdInitialField(*model, *topology, definition);
    if (!initial.has_value()) {
        return refuse(CfdFailureStage::InputRefused, initial.error());
    }

    // --- 7. the fields, owned by this call (ADR-068) --------------------------
    // "ownership stays with the consumer", and this is the consumer. The
    // lengths come from the layout and from nowhere else.
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
    // phi is NOT expanded: it is derived from U by the qualified mass-flux
    // operator on every iteration, and an initial condition for it would be a
    // second authority for the same quantity.
    std::vector<double> massFlux;

    // --- 8. the outer loop ---------------------------------------------------
    const Density density = model->fluid().density();
    const DynamicViscosity viscosity = model->fluid().viscosity();
    std::optional<CfdResiduals> firstResiduals;
    CfdSteadyReport report{};
    // ADR-058's two measures, as the QUALIFIED evaluator produced them and not
    // as this file would like them to read. In a closed domain the coupling
    // leaves them defaulted, because the relative measures are undefined rather
    // than small there, and `CfdResultMetadata` has no field that can say
    // "undefined" -- so the default is published and
    // `CfdSteadyReport::massBalanceDefined` is where the distinction lives
    // (ADR-077 L7). Passing the evaluator's own value through unchanged is the
    // part that matters: nothing here invents a measurement.
    CfdMassBalance balance{};
    bool converged = false;

    for (std::uint32_t completed = 0; !cfdCouplingBudgetExhausted(completed, steadySettings);) {
        // a. the mass flux the momentum assembly convects with, from the
        //    CURRENT velocity.
        const CfdMassFluxInput fluxInput{.currentVelocity = velocity, .density = density};
        std::expected<std::vector<double>, CfdConvectionFailure> flux =
            cfdMassFlux(*topology, *geometry, *boundary, fluxInput);
        if (!flux.has_value()) {
            return refuse(CfdFailureStage::IllPosed, flux.error().error);
        }

        // b. the pressure gradient: momentum's second driving force.
        Result<std::vector<PressureGradient3>> gradient =
            flattenPressureGradient(*topology, *geometry, *boundary, pressure);
        if (!gradient.has_value()) {
            return refuse(CfdFailureStage::IllPosed, gradient.error());
        }

        // c. the three momentum systems, re-assembled because their
        //    coefficients depend on the flux and the velocity above.
        //
        //    `faceGradient` IS LEFT EMPTY, which omits diffusion's
        //    non-orthogonal correction: that is ADR-069's documented
        //    limitation, carried here unchanged because nothing in the tree
        //    produces a per-FACE gradient and writing one would be new numerics
        //    this checklist does not authorize. ADR-077 L6.
        std::vector<CfdMomentumSystem> systems;
        systems.reserve(kVectorComponents.size());
        for (const VectorComponent component : kVectorComponents) {
            CfdMomentumInput momentum;
            momentum.density = density;
            momentum.viscosity = viscosity;
            momentum.component = component;
            momentum.faceFlux = *flux;
            momentum.scheme = CfdConvectionScheme::Upwind;
            momentum.currentVelocity = velocity;
            momentum.pressureGradient = *gradient;
            // GRAVITY IS HALF OF ONE TREATMENT AND THIS LINE IS THE OTHER HALF.
            // An adversarial review found it missing. `prepareCfdBoundary`
            // already applies the hydrostatic field at a prescribed-pressure
            // facet -- `hydrostaticPressure(prescribed, centroid, gravity,
            // density)` -- and `ADR-073` Decision 6 states that the two belong
            // together: "`referenceHeight` belongs to prescribed boundary
            // pressures and reading it here would apply the hydrostatic field
            // twice." Omitting the body force left the boundary half applied and
            // the interior half absent, which is a spurious pressure gradient
            // driving a spurious flow -- published under a fingerprint that
            // names gravity, because `canonicalText` writes
            // `gravity.acceleration` and `gravity.reference_height` into it.
            //
            // Nothing refused it: `assembleCfdMomentum` validates gravity only
            // WHEN PRESENT, so an absent record is silently the gravity-free
            // equation. `CfdSteady_GravityReachesTheMomentumEquation` is the
            // regression test, and it fails without this line.
            momentum.gravity = definition.gravity;
            std::expected<CfdMomentumSystem, CfdMomentumFailure> system =
                assembleCfdMomentum(*topology, *geometry, *boundary, momentum);
            if (!system.has_value()) {
                return refuse(CfdFailureStage::IllPosed, system.error().error);
            }
            systems.push_back(std::move(*system));
        }

        // d. one SIMPLE outer iteration.
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
        iterations = completed;

        // e. THERE IS NO NON-FINITE FIELD CHECK HERE, AND ITS ABSENCE IS THE
        //    POINT. "NaN / Inf rejection" is this milestone's checklist item,
        //    and it is discharged by the MAPPING in `stageOf` rather than by a
        //    loop of our own.
        //
        //    One was written first, over `iteration->corrected`'s three arrays.
        //    Then `applyCfdCouplingCorrection` was read, and it already ends:
        //
        //        if (!allFinite(out.faceFlux) || !allFinite(out.velocity) ||
        //            !allFinite(out.pressure)) {
        //            return std::unexpected(internalFailure(
        //                CfdCouplingProblem::ResultIsNotFinite, ...));
        //        }
        //
        //    So `advanceCfdCoupling` never returns a success whose corrected
        //    fields hold a non-finite value, and the check COULD NOT FIRE FOR
        //    ANY INPUT. `CfdBoundary.hpp` states this project's rule for the
        //    same shape of problem -- "An enumerator no input can produce is
        //    worse than no enumerator" -- and `P19-LINSOLVE-001` removed a
        //    clause from both linalg kernels for exactly this reason after a
        //    mutation probe found it (its `M4e`). Here the layer below was read
        //    before a probe had to find it.
        //
        //    What remains is reachable and is where the item lives:
        //    `stageOf(ResultIsNotFinite)` and `stageOf(ValueNotFinite)` both
        //    answer `NonFiniteField`, and `CfdResultFields::create` refuses a
        //    non-finite value as a type invariant for every other caller.
        pressure = iteration->corrected.pressure;
        velocity = iteration->corrected.velocity;
        massFlux = iteration->corrected.faceFlux;

        if (!firstResiduals.has_value()) {
            firstResiduals = iteration->residuals;
        }
        balance = iteration->balance;
        report.outerIterations = completed;
        report.firstResiduals = *firstResiduals;
        report.residuals = iteration->residuals;
        report.massBalanceDefined = iteration->summary.massBalanceDefined;
        report.closedDomain = iteration->summary.closedDomain;
        report.continuityScale = iteration->summary.continuityScale;
        report.largestPressureCorrection = iteration->summary.largestPressureCorrection;

        // f. the criteria, which are the coupling's own pure function.
        const CfdCouplingVerdict verdict =
            cfdCouplingVerdict(iteration->residuals, *firstResiduals, convergenceSettings);
        if (verdict == CfdCouplingVerdict::Diverged) {
            // NAME WHICH RESIDUAL AND BOTH NUMBERS. ARCHITECTURE.md: "A
            // diagnostic says what failed, where, why and which object caused
            // it." An earlier version named only the factor and the iteration,
            // which told a reader that one of four criteria had grown without
            // saying which or from what -- all four being in hand here. An
            // adversarial review found it.
            //
            // A non-finite residual also reaches `Diverged` inside
            // `cfdCouplingVerdict`, but it cannot reach HERE:
            // `advanceCfdCoupling`'s own `isWellFormed(residuals)` gate refuses
            // one first, so growth really is the only route and this message
            // does not have to hedge about which it was.
            const std::array<std::pair<std::string_view, std::pair<double, double>>, 4> criteria{
                std::pair<std::string_view, std::pair<double, double>>{
                    "momentum x", {firstResiduals->momentumX, iteration->residuals.momentumX}},
                std::pair<std::string_view, std::pair<double, double>>{
                    "momentum y", {firstResiduals->momentumY, iteration->residuals.momentumY}},
                std::pair<std::string_view, std::pair<double, double>>{
                    "momentum z", {firstResiduals->momentumZ, iteration->residuals.momentumZ}},
                std::pair<std::string_view, std::pair<double, double>>{
                    "continuity", {firstResiduals->continuity, iteration->residuals.continuity}},
            };
            std::string grew;
            for (const auto& [name, values] : criteria) {
                const auto [start, now] = values;
                if (std::isfinite(start) && start > 0.0 &&
                    now > convergenceSettings.divergenceFactor * start) {
                    if (!grew.empty()) {
                        grew += ", ";
                    }
                    grew += std::format("{} {} from {}", name, now, start);
                }
            }
            return refuse(CfdFailureStage::Diverged,
                          errorOf(ErrorCode::FailedPrecondition,
                                  std::format("CFD steady: outer iteration {} diverged, a residual past {} times "
                                              "its first-iteration value: {}",
                                              completed, convergenceSettings.divergenceFactor, grew)));
        }
        if (verdict == CfdCouplingVerdict::Converged) {
            converged = true;
            break;
        }
    }

    // g. the budget. Reaching it without converging is a FAILURE and never a
    //    result, which CfdSteadySettings states and this reports.
    if (!converged) {
        return refuse(CfdFailureStage::IterationLimitReached,
                      errorOf(ErrorCode::FailedPrecondition,
                                std::format("CFD steady: {} outer iterations did not converge, and the budget is {}",
                                            report.outerIterations, steadySettings.maxIterations)));
    }

    // --- 9. publish ----------------------------------------------------------
    Result<CfdResultFields> fields =
        CfdResultFields::create(*layout, std::move(pressure), std::move(velocity), std::move(massFlux));
    if (!fields.has_value()) {
        return refuse(CfdFailureStage::IllPosed, fields.error());
    }
    const CfdResultMetadata metadata{.outerIterations = report.outerIterations,
                                     .residuals = report.residuals,
                                     .massBalance = balance,
                                     .time = std::nullopt,
                                     .step = std::nullopt};
    Result<CfdResult> result = CfdResult::create(*source, model->mesh(), *layout, metadata, std::move(*fields));
    if (!result.has_value()) {
        return refuse(CfdFailureStage::IllPosed, result.error());
    }
    slot.recordSuccess(std::move(*result));
    return report;
}

} // namespace bettercad::cfd
