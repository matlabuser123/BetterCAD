// Build-failure tests for the transient thermal solve (see CMakeLists.txt in this
// directory). Built once without any BETTERCAD_CF_* macro as a control, which
// must compile -- so every failure below is attributable to its own line and not
// to a missing header.
//
// Five claims of P18-TRANSIENT-001 are enforced here rather than reviewed:
//
//   1. The numbers are DIMENSIONED, and K and C are different types. A temperature
//      is K, a time is s; a conductance (W/K) is not a capacity (J/K), so handing
//      `C` where `K` is wanted -- the swap that gives a plausible wrong answer --
//      does not compile; and neither a bare double nor a time stands in for a
//      temperature.
//
//   2. A history and a time grid CANNOT BE CONJURED OR EDITED. Possession is the
//      evidence that the system and its constraints agreed, the grid ended exactly
//      at t_end, every step was accepted by two independent residuals and every
//      prescribed unknown held its value; the arrays are read-only views.
//
//   3. THE SCOPE LINE. A history has no gradient, no flux, no reaction, no heat
//      balance (P18-POST-001 and P18-BALANCE-001), no interpolation and no restart.
//      The settings have no algorithm selector, no regularisation, no lumping, no
//      theta, no method selector, no adaptive step and no retry count: the method is
//      one (backward Euler, ADR-049) and a failed step fails the run. Their absence
//      from the types is the proof that this milestone marches C dT/dt + K T = Q at
//      a fixed step and does nothing else.
//
//   4. NOTHING IS TIME-DEPENDENT. A load, a boundary condition and the transient
//      settings carry numbers, not functions of time: there is no time profile to
//      silently freeze.
//
//   5. The steady and the transient solves are different things. A steady solution
//      is not a transient one, a transient failure is not a steady one, and P17's
//      structural settings record converts to neither.
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/structural/StructuralData.hpp>
#include <bettercad/thermal/ThermalSolve.hpp>
#include <bettercad/thermal/ThermalTransient.hpp>

#include <span>
#include <vector>

using namespace bettercad;
using namespace bettercad::thermal;

int main() {
    // Dead code that must still COMPILE in the control, so every call below is
    // checked against the real signatures without needing a mesh at run time.
    const ThermalSystem* system = nullptr;
    const ThermalDofMap* map = nullptr;
    const ThermalConstraintMap* constraints = nullptr;
    const ThermalFreeEquationMap* equations = nullptr;
    const TransientThermalSolution* solution = nullptr;
    const SteadyThermalSolution* steadySolution = nullptr;
    const ThermalTransientAlgebra* algebra = nullptr;
    const TimeGrid* grid = nullptr;
    const std::vector<Temperature>* initialState = nullptr;
    const ThermalSolverSettings settings{};
    const TransientSettings transient{};
    const UniformInitialTemperature initial{};
    if (system != nullptr && map != nullptr && constraints != nullptr && equations != nullptr && solution != nullptr &&
        steadySolution != nullptr && algebra != nullptr && grid != nullptr && initialState != nullptr && system->capacity().has_value()) {
        (void)solveTransientThermalSystem(*system, *map, *constraints, initial, transient, settings);
        (void)solveThermalTransientAlgebra(system->conductance(), *system->capacity(), system->heat(), *equations, *constraints,
                                           *initialState, *grid, TransientOutputPolicy::EveryStep, settings);
        (void)validateTransientStep(system->conductance(), *system->capacity(), system->heat(), *equations, *constraints,
                                    *initialState, *initialState, 1.0, settings);
        (void)effectiveMatrix(system->conductance(), *system->capacity(), 1.0);
        (void)TimeGrid::create(1.0, 10.0);
        (void)grid->timeAt(0);
        (void)grid->stepAt(0);
        (void)grid->stepCount();
        (void)solution->grid();
        (void)solution->source();
        (void)solution->sampleTimes();
        (void)solution->timeOf(0);
        (void)solution->temperaturesAt(0);
        (void)solution->finalTemperatures();
        (void)solution->initialTemperatures();
        (void)solution->steps();
        (void)solution->worstReducedResidual();
        (void)solution->worstPhysicalResidual();
        (void)solution->temperatureOf(0, ThermalDofIndex::fromValue(0));
        (void)algebra->samples;
    }

#if defined(BETTERCAD_CF_A_CAPACITY_IS_NOT_A_CONDUCTANCE)
    // C where K belongs: the swap that would solve a different equation without complaint.
    if (system != nullptr && system->capacity().has_value()) {
        (void)effectiveMatrix(*system->capacity(), system->conductance(), 1.0);
    }
#endif

#if defined(BETTERCAD_CF_A_CONDUCTANCE_IS_NOT_A_CAPACITY)
    if (system != nullptr && equations != nullptr && constraints != nullptr && grid != nullptr && initialState != nullptr) {
        (void)solveThermalTransientAlgebra(system->conductance(), system->conductance(), system->heat(), *equations, *constraints,
                                           *initialState, *grid, TransientOutputPolicy::EveryStep, settings);
    }
#endif

#if defined(BETTERCAD_CF_A_BARE_DOUBLE_IS_NOT_AN_INITIAL_TEMPERATURE)
    // 300 of what: kelvin, or degrees Celsius?
    [[maybe_unused]] const UniformInitialTemperature bad{300.0};
#endif

#if defined(BETTERCAD_CF_A_BARE_DOUBLE_IS_NOT_A_TIME_STEP)
    // 0.1 of what: seconds, or milliseconds?
    [[maybe_unused]] const TransientSettings bad{.timeStep = 0.1, .endTime = Time{}};
#endif

#if defined(BETTERCAD_CF_A_TIME_IS_NOT_A_TEMPERATURE)
    if (solution != nullptr) {
        [[maybe_unused]] Temperature bad = solution->timeOf(0);
    }
#endif

#if defined(BETTERCAD_CF_A_TEMPERATURE_IS_NOT_A_TIME)
    if (solution != nullptr) {
        [[maybe_unused]] Time bad = solution->finalTemperatures()[0];
    }
#endif

#if defined(BETTERCAD_CF_A_TEMPERATURE_IS_NOT_A_DOUBLE)
    // A solved temperature has a unit: it is kelvin, not a number.
    if (solution != nullptr) {
        [[maybe_unused]] double bad = solution->finalTemperatures()[0];
    }
#endif

#if defined(BETTERCAD_CF_A_STEADY_SOLUTION_IS_NOT_A_TRANSIENT_ONE)
    if (steadySolution != nullptr) {
        [[maybe_unused]] const TransientThermalSolution& bad = *steadySolution;
    }
#endif

#if defined(BETTERCAD_CF_A_TRANSIENT_FAILURE_IS_NOT_A_STEADY_ONE)
    [[maybe_unused]] SteadySolveFailure bad = TransientFailure{};
#endif

#if defined(BETTERCAD_CF_A_STRUCTURAL_SETTINGS_RECORD_IS_NOT_A_THERMAL_ONE)
    // The physics share a kernel (ADR-052), not a settings record.
    if (system != nullptr && map != nullptr && constraints != nullptr) {
        [[maybe_unused]] auto bad = solveTransientThermalSystem(*system, *map, *constraints, initial, transient, structural::SolverSettings{});
    }
#endif

#if defined(BETTERCAD_CF_A_SOLUTION_CONSTRUCTED_DIRECTLY)
    // Possession is the evidence that every step was accepted; there is no public constructor.
    if (algebra != nullptr) {
        [[maybe_unused]] TransientThermalSolution bad(TransientSolutionSource{}, *algebra);
    }
#endif

#if defined(BETTERCAD_CF_A_TIME_GRID_CONSTRUCTED_DIRECTLY)
    // A grid that was never checked to end exactly at t_end is unrepresentable.
    [[maybe_unused]] TimeGrid bad;
#endif

#if defined(BETTERCAD_CF_THE_HISTORY_TEMPERATURES_ARE_READ_ONLY)
    // A solved field is not edited to make it fit; it is solved again.
    if (solution != nullptr) {
        solution->temperaturesAt(0)[0] = Temperature::fromSi(0.0);
    }
#endif

#if defined(BETTERCAD_CF_THE_SAMPLE_TIMES_ARE_READ_ONLY)
    if (solution != nullptr) {
        solution->sampleTimes()[0] = Time::fromSi(0.0);
    }
#endif

#if defined(BETTERCAD_CF_THE_STEP_RECORDS_ARE_READ_ONLY)
    if (solution != nullptr) {
        solution->steps()[0].reducedResidual = 0.0;
    }
#endif

#if defined(BETTERCAD_CF_THE_SOLUTION_SOURCE_IS_READ_ONLY)
    if (solution != nullptr) {
        solution->source().transient.output = TransientOutputPolicy::FinalOnly;
    }
#endif

#if defined(BETTERCAD_CF_THE_SOLUTION_HAS_NO_GRADIENT)
    // grad(T) is P18-POST-001's.
    [[maybe_unused]] auto bad = solution->gradients();
#endif

#if defined(BETTERCAD_CF_THE_SOLUTION_HAS_NO_HEAT_FLUX)
    [[maybe_unused]] auto bad = solution->heatFluxes();
#endif

#if defined(BETTERCAD_CF_THE_SOLUTION_HAS_NO_REACTIONS)
    [[maybe_unused]] auto bad = solution->reactions();
#endif

#if defined(BETTERCAD_CF_THE_SOLUTION_HAS_NO_HEAT_BALANCE)
    // The per-step residual is here; the heat balance that interprets the boundary rates is P18-BALANCE-001's.
    [[maybe_unused]] auto bad = solution->heatBalance();
#endif

#if defined(BETTERCAD_CF_THE_SOLUTION_HAS_NO_INTERPOLATION)
    // The stored states are the integrator's own. An off-grid time is not interpolated into existence.
    [[maybe_unused]] auto bad = solution->interpolate(Time{});
#endif

#if defined(BETTERCAD_CF_THE_SOLUTION_HAS_NO_RESTART)
    // Every run starts from the canonical initial condition.
    [[maybe_unused]] auto bad = solution->restart();
#endif

#if defined(BETTERCAD_CF_THE_SOLUTION_HAS_NO_PENALTY)
    // Dirichlet is applied by exact reduction, never by a large number.
    [[maybe_unused]] auto bad = solution->penalty();
#endif

#if defined(BETTERCAD_CF_THE_SOLUTION_HAS_NO_FACTORISATION)
    // A factorisation is solver workspace: derived, never retained, never persisted.
    [[maybe_unused]] auto bad = solution->factorisation();
#endif

#if defined(BETTERCAD_CF_THE_SETTINGS_HAVE_NO_ALGORITHM_SELECTOR)
    [[maybe_unused]] auto bad = settings.algorithm;
#endif

#if defined(BETTERCAD_CF_THE_SETTINGS_HAVE_NO_REGULARISATION)
    // A singular system fails; no epsilon is added to it.
    [[maybe_unused]] auto bad = settings.regularisation;
#endif

#if defined(BETTERCAD_CF_THE_SETTINGS_HAVE_NO_LUMPING)
    // The consistent capacity is used as assembled. A lumped one is a different equation.
    [[maybe_unused]] auto bad = settings.lumpedCapacity;
#endif

#if defined(BETTERCAD_CF_THE_SETTINGS_HAVE_NO_RETRY)
    // A failed step fails the run; there is no second attempt with a smaller step.
    [[maybe_unused]] auto bad = settings.maxRetries;
#endif

#if defined(BETTERCAD_CF_THE_TRANSIENT_SETTINGS_HAVE_NO_METHOD_SELECTOR)
    // Backward Euler is the one method (ADR-049); a selector would be a field with one legal value.
    [[maybe_unused]] auto bad = transient.method;
#endif

#if defined(BETTERCAD_CF_THE_TRANSIENT_SETTINGS_HAVE_NO_THETA)
    [[maybe_unused]] auto bad = transient.theta;
#endif

#if defined(BETTERCAD_CF_THE_TRANSIENT_SETTINGS_HAVE_NO_ADAPTIVE_STEP)
    [[maybe_unused]] auto bad = transient.adaptive;
#endif

#if defined(BETTERCAD_CF_THE_TRANSIENT_SETTINGS_HAVE_NO_OUTPUT_TIMES)
    // The policy is final-only or every-step; there is no list of off-grid times to interpolate to.
    [[maybe_unused]] auto bad = transient.outputTimes;
#endif

#if defined(BETTERCAD_CF_THE_TRANSIENT_SETTINGS_HAVE_NO_TIME_PROFILE)
    // Nothing is a function of time; there is no profile to silently freeze.
    [[maybe_unused]] auto bad = transient.timeProfile;
#endif

#if defined(BETTERCAD_CF_A_LOAD_HAS_NO_TIME_FUNCTION)
    const ThermalLoad* load = nullptr;
    [[maybe_unused]] auto bad = load->timeFunction;
#endif

#if defined(BETTERCAD_CF_A_BOUNDARY_CONDITION_HAS_NO_TIME_FUNCTION)
    const ThermalBoundaryCondition* condition = nullptr;
    [[maybe_unused]] auto bad = condition->timeFunction;
#endif

    return 0;
}
