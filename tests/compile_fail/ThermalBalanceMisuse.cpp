// Build-failure tests for the thermal heat balance (see CMakeLists.txt in this
// directory). Built once without any BETTERCAD_CF_* macro as a control, which must
// compile -- so every failure below is attributable to its own line and not to a
// missing header.
//
// Five claims of P18-BALANCE-001 are enforced here rather than reviewed:
//
//   1. The numbers are DIMENSIONED. A heat rate is W and a stored energy J; neither
//      converts to the other, a bare double stands in for neither, and a heat rate is not a
//      temperature. `K` (W/K) and `C` (J/K) are different matrix types, so the swap that
//      would give a plausible wrong answer -- a capacity in the conduction slot of a residual,
//      a conductance in the storage slot of an energy -- does not compile.
//
//   2. The identity spaces do not mix. A prescribed unknown is a `ThermalDofIndex`, a load is a
//      `ThermalLoadId` and a condition a `ThermalBoundaryConditionId`: none is a node handle,
//      and a load's heat is not a convection condition's.
//
//   3. A RESULT CANNOT BE CONJURED OR EDITED. Possession is the evidence that the solution, the
//      system, the loads and the boundary agreed and every term was finite. The states, the terms,
//      the reactions and the integrated energy are read-only views.
//
//   4. THE SCOPE LINE. A balance has no "remainder" reaction that closes the books, no per-condition
//      split of a reaction that two conditions may share, no flux or gradient (P18-POST-001's), no
//      Celsius, no re-integration of the history, no validation verdict (P18-VALID-001's), no
//      persistence, no colour or component choice, no time-dependent load and no lumped capacity.
//      Their absence from the types is the proof that this milestone accounts for heat and does
//      nothing else.
//
//   5. A balance is NOT a `ThermalResult` (that type also carries the fields P18-POST-001 recovers and
//      is assembled by a later milestone), is not a post result, and a solver source is not a balance
//      source without the named conversion.
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/thermal/ThermalBalance.hpp>
#include <bettercad/thermal/ThermalResult.hpp>

#include <span>
#include <vector>

using namespace bettercad;
using namespace bettercad::thermal;

int main() {
    // Dead code that must still COMPILE in the control, so every call below is checked against the
    // real signatures without needing a mesh at run time.
    const ConductanceMatrix* conductance = nullptr;
    const CapacityMatrix* capacity = nullptr;
    const ThermalHeatVector* heat = nullptr;
    const ThermalConstraintMap* constraints = nullptr;
    const std::vector<Temperature>* temperatures = nullptr;
    const ThermalSystem* system = nullptr;
    const ThermalDofMap* map = nullptr;
    const SteadyThermalSolution* steady = nullptr;
    const TransientThermalSolution* transient = nullptr;
    const ThermalBalanceResult* balance = nullptr;
    const PreparedConvectionBoundary* convection = nullptr;
    if (conductance != nullptr && capacity != nullptr && heat != nullptr && constraints != nullptr &&
        temperatures != nullptr && system != nullptr && map != nullptr && steady != nullptr && transient != nullptr &&
        balance != nullptr && convection != nullptr) {
        const BalanceAlgebra input{.conductance = *conductance,
                                   .capacity = capacity,
                                   .heat = *heat,
                                   .constraints = *constraints,
                                   .loads = {},
                                   .convections = {}};
        (void)steadyHeatBalance(input, *temperatures);
        (void)transientStepHeatBalance(input, *temperatures, *temperatures, 1.0, 1);
        (void)steadyFullResidual(*conductance, *heat, *temperatures);
        (void)transientFullResidual(*conductance, *capacity, *heat, *temperatures, *temperatures, 1.0);
        (void)storedEnergyChange(*capacity, *temperatures, *temperatures);
        (void)convectionHeatOf(*convection, *temperatures);
        (void)balanceSteadySolution(*system, *map, *constraints, {}, {}, *steady);
        (void)balanceTransientSolution(*system, *map, *constraints, {}, {}, *transient);
        (void)balance->source();
        (void)balance->mode();
        (void)balance->states();
        (void)balance->stateCount();
        (void)balance->finalState().terms;
        (void)balance->finalState().reactions;
        (void)balance->finalState().surfaceLoads;
        (void)balance->finalState().generationLoads;
        (void)balance->finalState().convections;
        (void)balance->finalState().storedEnergyChange;
        (void)balance->finalState().residual();
        (void)balance->finalState().normalizedResidual();
        (void)balance->integrated();
        (void)balance->balanced();
        (void)balance->worstNormalizedResidual();
    }

#if defined(BETTERCAD_CF_A_POWER_IS_NOT_AN_ENERGY)
    // W and J differ by a time; the conversion is a multiplication by dt, written once.
    [[maybe_unused]] Energy bad = Power{};
#endif

#if defined(BETTERCAD_CF_AN_ENERGY_IS_NOT_A_POWER)
    [[maybe_unused]] Power bad = Energy{};
#endif

#if defined(BETTERCAD_CF_A_HEAT_RATE_IS_NOT_A_TEMPERATURE)
    [[maybe_unused]] Temperature bad = Power{};
#endif

#if defined(BETTERCAD_CF_A_BARE_DOUBLE_IS_NOT_A_POWER)
    // 5 of what: W, or kW?
    [[maybe_unused]] Power bad = 5.0;
#endif

#if defined(BETTERCAD_CF_A_BARE_DOUBLE_IS_NOT_AN_ENERGY)
    [[maybe_unused]] Energy bad = 40.0;
#endif

#if defined(BETTERCAD_CF_A_DOF_INDEX_IS_NOT_A_NODE_ID)
    // A reaction belongs to a prescribed UNKNOWN of the numbering, never to a node handle.
    [[maybe_unused]] ReactionHeat bad{.dof = meshing::NodeId::fromValue(1), .heatInto = Power{}};
#endif

#if defined(BETTERCAD_CF_A_LOAD_ID_IS_NOT_A_CONDITION_ID)
    [[maybe_unused]] ConvectionHeat bad{.condition = ThermalLoadId::fromValue(1)};
#endif

#if defined(BETTERCAD_CF_A_NODE_ID_IS_NOT_A_LOAD_ID)
    [[maybe_unused]] LoadHeat bad{.load = meshing::NodeId::fromValue(1), .heatInto = Power{}};
#endif

#if defined(BETTERCAD_CF_A_CAPACITY_IS_NOT_A_CONDUCTANCE)
    // The swap that would give a plausible wrong answer: C where K is wanted.
    if (capacity != nullptr && heat != nullptr && temperatures != nullptr) {
        (void)steadyFullResidual(*capacity, *heat, *temperatures);
    }
#endif

#if defined(BETTERCAD_CF_A_CONDUCTANCE_IS_NOT_A_CAPACITY)
    // ... and K where C is wanted: the stored energy is 1^T C dT, not 1^T K dT.
    if (conductance != nullptr && temperatures != nullptr) {
        (void)storedEnergyChange(*conductance, *temperatures, *temperatures);
    }
#endif

#if defined(BETTERCAD_CF_A_SOLVER_SOURCE_IS_NOT_A_BALANCE_SOURCE)
    // The conversion (`postSourceOf`, whose type the balance source IS) is named and explicit.
    [[maybe_unused]] ThermalBalanceSource bad = SteadySolutionSource{};
#endif

#if defined(BETTERCAD_CF_A_BALANCE_RESULT_IS_NOT_A_THERMAL_RESULT)
    // A ThermalResult also holds the recovered fields.
    if (balance != nullptr) {
        [[maybe_unused]] const ThermalResult& bad = *balance;
    }
#endif

#if defined(BETTERCAD_CF_A_BALANCE_RESULT_IS_NOT_A_POST_RESULT)
    if (balance != nullptr) {
        [[maybe_unused]] const ThermalPostResult& bad = *balance;
    }
#endif

#if defined(BETTERCAD_CF_A_LOAD_HEAT_IS_NOT_A_CONVECTION_HEAT)
    [[maybe_unused]] ConvectionHeat bad = LoadHeat{};
#endif

#if defined(BETTERCAD_CF_A_BALANCE_RESULT_CONSTRUCTED_DIRECTLY)
    // Possession is the evidence that the solution was current and every term finite; there is no
    // public constructor.
    [[maybe_unused]] ThermalBalanceResult bad(ThermalBalanceSource{}, 0, {}, std::nullopt);
#endif

#if defined(BETTERCAD_CF_THE_RESULT_HAS_NO_REMAINDER_REACTION)
    // A reaction is the residual of a prescribed row. It is never "whatever closes the balance".
    if (balance != nullptr) {
        (void)balance->finalState().remainderReaction();
    }
#endif

#if defined(BETTERCAD_CF_THE_RESULT_HAS_NO_PER_CONDITION_REACTION)
    // Two identical conditions that P18-BC-001 collapsed share their unknowns: splitting the reaction between them
    // would invent a rule.
    if (balance != nullptr) {
        (void)balance->finalState().reactionsOf(ThermalBoundaryConditionId::fromValue(1));
    }
#endif

#if defined(BETTERCAD_CF_THE_RESULT_HAS_NO_CELSIUS)
    if (balance != nullptr) {
        (void)balance->finalState().celsius;
    }
#endif

#if defined(BETTERCAD_CF_THE_RESULT_HAS_NO_HEAT_FLUX)
    if (balance != nullptr) {
        (void)balance->finalState().heatFlux;
    }
#endif

#if defined(BETTERCAD_CF_THE_RESULT_HAS_NO_GRADIENT)
    if (balance != nullptr) {
        (void)balance->finalState().gradient;
    }
#endif

#if defined(BETTERCAD_CF_THE_RESULT_HAS_NO_TIME_INTEGRATION)
    // The balance never runs the integration again to invent a state.
    if (balance != nullptr) {
        (void)balance->reintegrate();
    }
#endif

#if defined(BETTERCAD_CF_THE_RESULT_HAS_NO_VALIDATION_VERDICT)
    // Whether an analysis is ACCEPTED is P18-VALID-001's.
    if (balance != nullptr) {
        (void)balance->finalState().accepted;
    }
#endif

#if defined(BETTERCAD_CF_THE_RESULT_HAS_NO_PERSISTENCE)
    if (balance != nullptr) {
        (void)balance->save();
    }
#endif

#if defined(BETTERCAD_CF_THE_SOURCE_HAS_NO_TOLERANCE_SETTING)
    // The policy is one constant, not an analysis setting.
    [[maybe_unused]] auto bad = ThermalBalanceSource{}.balanceTolerance;
#endif

#if defined(BETTERCAD_CF_THE_SOURCE_HAS_NO_COLOUR_MAP)
    [[maybe_unused]] auto bad = ThermalBalanceSource{}.colourMap;
#endif

#if defined(BETTERCAD_CF_THE_SOURCE_HAS_NO_COMPONENT_SELECTION)
    [[maybe_unused]] auto bad = ThermalBalanceSource{}.selectedCategory;
#endif

#if defined(BETTERCAD_CF_THE_ALGEBRA_HAS_NO_TIME_FUNCTION)
    // The loads and conditions of this version are constant in time.
    if (conductance != nullptr && heat != nullptr && constraints != nullptr) {
        const BalanceAlgebra input{*conductance, nullptr, *heat, *constraints, {}, {}};
        (void)input.loadAt;
    }
#endif

#if defined(BETTERCAD_CF_THE_ALGEBRA_HAS_NO_LUMPING)
    // The capacity is the consistent matrix as assembled; there is no lumped alternative to select.
    if (conductance != nullptr && heat != nullptr && constraints != nullptr) {
        const BalanceAlgebra input{*conductance, nullptr, *heat, *constraints, {}, {}};
        (void)input.lumpedCapacity;
    }
#endif

#if defined(BETTERCAD_CF_THE_STATES_ARE_READ_ONLY)
    // A balance is not edited to make it close; it is taken again.
    if (balance != nullptr) {
        balance->states()[0].reactions.push_back(ReactionHeat{});
    }
#endif

#if defined(BETTERCAD_CF_THE_REACTIONS_ARE_READ_ONLY)
    if (balance != nullptr) {
        balance->finalState().reactions[0] = ReactionHeat{};
    }
#endif

#if defined(BETTERCAD_CF_THE_TERMS_ARE_READ_ONLY)
    if (balance != nullptr) {
        balance->finalState().terms.generatedHeat = Power{};
    }
#endif

#if defined(BETTERCAD_CF_THE_INTEGRATED_ENERGY_IS_READ_ONLY)
    if (balance != nullptr && balance->integrated().has_value()) {
        balance->integrated()->residual = Energy{};
    }
#endif

#if defined(BETTERCAD_CF_THE_SOURCE_IS_READ_ONLY)
    if (balance != nullptr) {
        balance->source().mode = ThermalAnalysisMode::Transient;
    }
#endif

    return 0;
}
