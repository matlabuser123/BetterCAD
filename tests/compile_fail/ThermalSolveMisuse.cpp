// Build-failure tests for the steady thermal solve (see CMakeLists.txt in this
// directory). Built once without any BETTERCAD_CF_* macro as a control, which
// must compile -- so every failure below is attributable to its own line and not
// to a missing header.
//
// Five claims of P18-SOLVE-001 are enforced here rather than reviewed:
//
//   1. The numbers are DIMENSIONED. A temperature is K, a heat rate is W, a
//      conductance is W/K; a residual norm in watts is not a temperature, a
//      conductance norm is not a power, and none of them is a bare double.
//
//   2. The index spaces do not mix. An unknown (`ThermalDofIndex`), a row of the
//      reduced system (`ThermalFreeEquationIndex`) and a mesh node (`NodeId`) are
//      three different things, and none converts to another: a solver that took a
//      free-equation row for an unknown would read the wrong temperature.
//
//   3. A solution and a reduced system CANNOT BE CONJURED OR EDITED. Possession is
//      the evidence that the system and its constraints agreed, the model was well
//      posed and the independent residual passed; the arrays are read-only views.
//
//   4. THE SCOPE LINE. A solution has no gradient, no flux, no reaction, no heat
//      balance, no time step. The settings have no algorithm selector, no
//      regularisation, no reference temperature and no iteration count. The
//      reduced system has no dense form. Their absence from the types is the
//      proof that this milestone solves K T = Q and does not post-process,
//      balance, regularise or march in time.
//
//   5. The settings are the THERMAL ones. P17's structural `SolverSettings` is a
//      different type and does not convert: the two physics share a kernel
//      (ADR-052), not a settings record.
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/structural/StructuralData.hpp>
#include <bettercad/thermal/ThermalSolve.hpp>

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
    const SteadyThermalSolution* solution = nullptr;
    const ReducedThermalSystem* reduced = nullptr;
    const ThermalAlgebraicSolution* algebraic = nullptr;
    const ThermalSolverSettings settings{};
    if (system != nullptr && map != nullptr && constraints != nullptr && equations != nullptr && solution != nullptr &&
        reduced != nullptr && algebraic != nullptr) {
        (void)solveSteadyThermalSystem(*system, *map, *constraints, settings);
        (void)solveThermalAlgebra(system->conductance(), system->heat(), *equations, *constraints, settings);
        (void)reduceThermalSystem(system->conductance(), system->heat(), *equations, *constraints);
        (void)validateSteadySolution(system->conductance(), system->heat(), *equations, *constraints, solution->temperatures(), settings);
        (void)analyseAnchoring(*reduced);
        (void)solution->temperatures();
        (void)solution->residual();
        (void)solution->fullResidual();
        (void)solution->source();
        (void)solution->pivotRatio();
        (void)solution->temperatureOf(ThermalDofIndex::fromValue(0));
        (void)reduced->kffAt(0, 0);
        (void)reduced->kfcAt(0, 0);
        (void)reduced->effectiveHeat();
        (void)algebraic->temperatures;
    }

#if defined(BETTERCAD_CF_A_TEMPERATURE_IS_NOT_A_DOUBLE)
    // A solved temperature has a unit: it is kelvin, not a number.
    if (solution != nullptr) {
        [[maybe_unused]] double bad = solution->temperatures()[0];
    }
#endif

#if defined(BETTERCAD_CF_A_RESIDUAL_IN_WATTS_IS_NOT_A_TEMPERATURE)
    if (solution != nullptr) {
        [[maybe_unused]] Temperature bad = solution->residual().euclideanNorm;
    }
#endif

#if defined(BETTERCAD_CF_A_CONDUCTANCE_NORM_IS_NOT_A_POWER)
    // W/K is not W: a factor of a temperature.
    if (solution != nullptr) {
        [[maybe_unused]] Power bad = solution->residual().conductanceInfinityNorm;
    }
#endif

#if defined(BETTERCAD_CF_A_POWER_IS_NOT_A_CONDUCTANCE_NORM)
    if (solution != nullptr) {
        [[maybe_unused]] ThermalConductance bad = solution->residual().heatNorm;
    }
#endif

#if defined(BETTERCAD_CF_A_BARE_DOUBLE_IS_NOT_A_PRESCRIBED_TEMPERATURE)
    // 300 of what: kelvin, or degrees Celsius?
    [[maybe_unused]] const ThermalTemperatureConstraint bad{ThermalDofIndex::fromValue(0), 300.0};
#endif

#if defined(BETTERCAD_CF_A_NODE_ID_IS_NOT_A_FREE_EQUATION)
    // A mesh node is an identity; a free equation is a row of the reduced system.
    [[maybe_unused]] ThermalFreeEquationIndex bad = meshing::NodeId::fromValue(1);
#endif

#if defined(BETTERCAD_CF_AN_UNKNOWN_IS_NOT_A_FREE_EQUATION)
    // The unknown of a node and the row of the reduced system are different numbers once
    // anything is prescribed; the conversion is made by the free-equation map or by nothing.
    [[maybe_unused]] ThermalFreeEquationIndex bad = ThermalDofIndex::fromValue(1);
#endif

#if defined(BETTERCAD_CF_A_FREE_EQUATION_IS_NOT_AN_UNKNOWN)
    [[maybe_unused]] ThermalDofIndex bad = ThermalFreeEquationIndex::fromValue(1);
#endif

#if defined(BETTERCAD_CF_A_STRUCTURAL_SETTINGS_RECORD_IS_NOT_A_THERMAL_ONE)
    // The two physics share a kernel (ADR-052), not a settings record.
    if (system != nullptr && map != nullptr && constraints != nullptr) {
        [[maybe_unused]] auto bad = solveSteadyThermalSystem(*system, *map, *constraints, structural::SolverSettings{});
    }
#endif

#if defined(BETTERCAD_CF_A_SOLUTION_CONSTRUCTED_DIRECTLY)
    // Possession is the evidence that the system was current, the model well posed and the
    // residual accepted; there is no public constructor.
    if (algebraic != nullptr) {
        [[maybe_unused]] SteadyThermalSolution bad(SteadySolutionSource{}, *algebraic);
    }
#endif

#if defined(BETTERCAD_CF_A_REDUCED_SYSTEM_CONSTRUCTED_DIRECTLY)
    [[maybe_unused]] ReducedThermalSystem bad;
#endif

#if defined(BETTERCAD_CF_THE_SOLUTION_TEMPERATURES_ARE_READ_ONLY)
    // A solved field is not edited to make it fit; it is solved again.
    if (solution != nullptr) {
        solution->temperatures()[0] = Temperature::fromSi(0.0);
    }
#endif

#if defined(BETTERCAD_CF_THE_REDUCED_RIGHT_HAND_SIDE_IS_READ_ONLY)
    if (reduced != nullptr) {
        reduced->effectiveHeat()[0] = 0.0;
    }
#endif

#if defined(BETTERCAD_CF_THE_SOLUTION_SOURCE_IS_READ_ONLY)
    if (solution != nullptr) {
        solution->source().settings.pivotFloor = 1.0;
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
    // The full residual is retained; its interpretation as boundary heat is a later milestone's.
    [[maybe_unused]] auto bad = solution->reactions();
#endif

#if defined(BETTERCAD_CF_THE_SOLUTION_HAS_NO_HEAT_BALANCE)
    [[maybe_unused]] auto bad = solution->heatBalance();
#endif

#if defined(BETTERCAD_CF_THE_SOLUTION_HAS_NO_TIME_STEP)
    // The transient solve is P18-TRANSIENT-001's.
    [[maybe_unused]] auto bad = solution->timeStep();
#endif

#if defined(BETTERCAD_CF_THE_SOLUTION_HAS_NO_PENALTY)
    // Dirichlet is applied by exact reduction, never by a large number.
    [[maybe_unused]] auto bad = solution->penalty();
#endif

#if defined(BETTERCAD_CF_THE_SOLUTION_HAS_NO_FACTORISATION)
    // A factorisation is solver workspace: derived, never retained, never persisted.
    [[maybe_unused]] auto bad = solution->factorisation();
#endif

#if defined(BETTERCAD_CF_THE_REDUCED_SYSTEM_HAS_NO_DENSE_FORM)
    // The reduced matrix is sparse; there is no dense form to ask for.
    [[maybe_unused]] auto bad = reduced->toDense();
#endif

#if defined(BETTERCAD_CF_THE_SETTINGS_HAVE_NO_ALGORITHM_SELECTOR)
    // One algorithm exists; a selector would be a field with one legal value, and a
    // fallback would hide a singular model.
    [[maybe_unused]] auto bad = settings.algorithm;
#endif

#if defined(BETTERCAD_CF_THE_SETTINGS_HAVE_NO_REGULARISATION)
    [[maybe_unused]] auto bad = settings.regularisation;
#endif

#if defined(BETTERCAD_CF_THE_SETTINGS_HAVE_NO_REFERENCE_TEMPERATURE)
    // A singular model fails; no datum is chosen for the user.
    [[maybe_unused]] auto bad = settings.referenceTemperature;
#endif

#if defined(BETTERCAD_CF_THE_SETTINGS_HAVE_NO_ITERATION_COUNT)
    [[maybe_unused]] auto bad = settings.maxIterations;
#endif

    return 0;
}
