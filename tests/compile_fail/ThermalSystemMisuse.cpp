// Build-failure tests for the global thermal system (see CMakeLists.txt in this
// directory). Built once without any BETTERCAD_CF_* macro as a control, which must
// compile -- so every failure below is attributable to its own line and not to a
// missing header.
//
// Five claims of P18-ASSEMBLY-001 are enforced here rather than reviewed:
//
//   1. K and C are DIFFERENT TYPES with different units. A conductance matrix
//      (W/K) is not a capacity matrix (J/K); an entry of one is not an entry of the
//      other, not a power and not a bare double; the equations K T = Q and
//      C dT/dt + K T = Q are equations between powers, and handing C where K is
//      wanted is a unit error, not a plausible-looking one.
//
//   2. Rows are UNKNOWNS. A node handle or an element identity is not a row, and an
//      unknown is not a bare integer: the numbering is crossed explicitly, with
//      `.value()`, in the one place that does it.
//
//   3. A SYSTEM AND ITS CONTAINERS CANNOT BE CONJURED OR EDITED. Possession is the
//      evidence that the inputs were current and compatible, every element valid and
//      every coefficient finite; the arrays are read-only views, so a solver cannot
//      write through what it was handed.
//
//   4. The scope line to P18-SOLVE-001. A system has no solution, no temperatures,
//      no reduced or constrained matrix, no penalty, no residual, no reaction, no
//      factorisation, no `C / dt`. Their absence from the type is the proof that
//      this milestone assembles the unconstrained system and does not solve it.
//
//   5. The provenance holds only what the matrices depend on. A system's source has
//      no solver settings, no time step, no initial condition and no prescribed
//      temperature: editing one of those cannot stale it, and the compiler says so.
//
// Nothing here is dense: there is no `toDense`, no `symmetrise`, no `regularise`.
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/thermal/ThermalSystem.hpp>

#include <optional>

using namespace bettercad;
using namespace bettercad::thermal;

int main() {
    // Dead code that must still COMPILE in the control, so every call below is
    // checked against the real signatures without needing a mesh at run time.
    const ThermalSystem* system = nullptr;
    const meshing::Mesh* mesh = nullptr;
    const ThermalDofMap* map = nullptr;
    if (system != nullptr && mesh != nullptr && map != nullptr) {
        const ConductanceMatrix& k = system->conductance();
        (void)k.coeff(0, 0);
        (void)k.coeffSi(0, 0);
        (void)k.rows();
        (void)k.nonZeros();
        (void)k.rowStart();
        (void)k.innerIndices();
        (void)k.values();
        (void)k.largestSymmetryError();
        (void)k.largestMagnitude();
        (void)system->capacity();
        (void)system->heat()[0];
        (void)system->heat().total();
        (void)system->source();
        (void)system->dofCount();
        (void)system->elementOrder();
        (void)system->convectionFacetCount();
        (void)system->describes(*map);
        (void)system->describes(*mesh);
        const ThermalAssemblyData data{.mesh = *mesh,
                                       .map = *map,
                                       .conductivity = ThermalConductivity::fromSi(45.0),
                                       .capacity = std::nullopt,
                                       .heatInput = {},
                                       .convections = {}};
        (void)assembleThermalSystemFrom(data, ThermalSystemSource{});
    }

#if defined(BETTERCAD_CF_A_CONDUCTANCE_MATRIX_IS_NOT_A_CAPACITY_MATRIX)
    // K (W/K) where C (J/K) is wanted.
    if (system != nullptr) {
        [[maybe_unused]] const CapacityMatrix& bad = system->conductance();
    }
#endif

#if defined(BETTERCAD_CF_A_CAPACITY_MATRIX_IS_NOT_A_CONDUCTANCE_MATRIX)
    if (system != nullptr && system->capacity().has_value()) {
        [[maybe_unused]] const ConductanceMatrix& bad = *system->capacity();
    }
#endif

#if defined(BETTERCAD_CF_A_CONDUCTANCE_ENTRY_IS_NOT_A_CAPACITY_ENTRY)
    if (system != nullptr) {
        [[maybe_unused]] HeatCapacity bad = system->conductance().coeff(0, 0);
    }
#endif

#if defined(BETTERCAD_CF_A_CONDUCTANCE_ENTRY_IS_NOT_A_POWER)
    // A matrix entry in W/K is not a source in W.
    if (system != nullptr) {
        [[maybe_unused]] Power bad = system->conductance().coeff(0, 0);
    }
#endif

#if defined(BETTERCAD_CF_A_POWER_IS_NOT_A_CONDUCTANCE_ENTRY)
    if (system != nullptr) {
        [[maybe_unused]] ThermalConductance bad = system->heat()[0];
    }
#endif

#if defined(BETTERCAD_CF_A_HEAT_ENTRY_IS_NOT_A_DOUBLE)
    // A source has a unit: it is watts, not a number.
    if (system != nullptr) {
        [[maybe_unused]] double bad = system->heat()[0];
    }
#endif

#if defined(BETTERCAD_CF_A_NODE_ID_IS_NOT_A_ROW)
    // A node handle is an identity, not an index: a row is a rank among the handles.
    if (system != nullptr) {
        [[maybe_unused]] double bad = system->conductance().coeffSi(meshing::NodeId::fromValue(1), 0);
    }
#endif

#if defined(BETTERCAD_CF_AN_ELEMENT_ID_IS_NOT_A_ROW)
    if (system != nullptr) {
        [[maybe_unused]] double bad = system->conductance().coeffSi(meshing::ElementId::fromValue(1), 0);
    }
#endif

#if defined(BETTERCAD_CF_AN_UNKNOWN_IS_NOT_A_BARE_ROW)
    // The numbering is crossed explicitly, with `.value()`, where it is crossed.
    if (system != nullptr) {
        [[maybe_unused]] double bad = system->conductance().coeffSi(ThermalDofIndex::fromValue(1), 0);
    }
#endif

#if defined(BETTERCAD_CF_A_DOUBLE_IS_NOT_A_CONDUCTIVITY)
    // 45 of what: W/(m K), W/(mm K)?
    if (mesh != nullptr && map != nullptr) {
        [[maybe_unused]] const ThermalAssemblyData bad{.mesh = *mesh,
                                                       .map = *map,
                                                       .conductivity = 45.0,
                                                       .capacity = std::nullopt,
                                                       .heatInput = {},
                                                       .convections = {}};
    }
#endif

#if defined(BETTERCAD_CF_A_DENSITY_IS_NOT_A_CONDUCTIVITY)
    if (mesh != nullptr && map != nullptr) {
        [[maybe_unused]] const ThermalAssemblyData bad{.mesh = *mesh,
                                                       .map = *map,
                                                       .conductivity = Density::fromSi(7850.0),
                                                       .capacity = std::nullopt,
                                                       .heatInput = {},
                                                       .convections = {}};
    }
#endif

#if defined(BETTERCAD_CF_A_VECTOR_OF_DOUBLES_IS_NOT_A_HEAT_INPUT)
    // The prepared heat input is a vector of Power. A bare double has no unit.
    if (mesh != nullptr && map != nullptr) {
        const double raw[2] = {0.0, 0.0};
        [[maybe_unused]] const ThermalAssemblyData bad{.mesh = *mesh,
                                                       .map = *map,
                                                       .conductivity = ThermalConductivity::fromSi(45.0),
                                                       .capacity = std::nullopt,
                                                       .heatInput = std::span<const double>(raw),
                                                       .convections = {}};
    }
#endif

#if defined(BETTERCAD_CF_A_SYSTEM_CONSTRUCTED_DIRECTLY)
    // Possession is the evidence that the inputs were current and compatible; the
    // constructor is private and `assembleThermalSystemFrom` is the only friend.
    if (system != nullptr) {
        [[maybe_unused]] ThermalSystem bad(system->conductance(), std::nullopt, system->heat(), system->source(), {}, 0);
    }
#endif

#if defined(BETTERCAD_CF_A_SPARSE_MATRIX_CONSTRUCTED_DIRECTLY)
    // The only way in is `fromCsr`, which validates the layout.
    [[maybe_unused]] ThermalSparseMatrix bad;
#endif

#if defined(BETTERCAD_CF_A_HEAT_VECTOR_CONSTRUCTED_DIRECTLY)
    // The only way in is `fromEntries`, which refuses a non-finite entry.
    [[maybe_unused]] ThermalHeatVector bad;
#endif

#if defined(BETTERCAD_CF_THE_MATRIX_VALUES_ARE_READ_ONLY)
    // A solver is handed a view of the matrix; it cannot write through it.
    if (system != nullptr) {
        system->conductance().values()[0] = 0.0;
    }
#endif

#if defined(BETTERCAD_CF_THE_MATRIX_PATTERN_IS_READ_ONLY)
    if (system != nullptr) {
        system->conductance().innerIndices()[0] = 0;
    }
#endif

#if defined(BETTERCAD_CF_THE_HEAT_VECTOR_IS_READ_ONLY)
    if (system != nullptr) {
        system->heat().values()[0] = 1.0;
    }
#endif

#if defined(BETTERCAD_CF_THE_SOURCE_IS_READ_ONLY)
    // A system's provenance is not edited to make it current; it is assembled again.
    if (system != nullptr) {
        system->source().mode = ThermalAnalysisMode::Transient;
    }
#endif

#if defined(BETTERCAD_CF_THE_SYSTEM_HAS_NO_SOLUTION)
    [[maybe_unused]] auto bad = system->solution();
#endif

#if defined(BETTERCAD_CF_THE_SYSTEM_HAS_NO_TEMPERATURES)
    [[maybe_unused]] auto bad = system->temperatures();
#endif

#if defined(BETTERCAD_CF_THE_SYSTEM_HAS_NO_PENALTY)
    // Dirichlet is enforced exactly by the later solve, never by a large number.
    [[maybe_unused]] auto bad = system->penalty();
#endif

#if defined(BETTERCAD_CF_THE_SYSTEM_HAS_NO_REDUCED_MATRIX)
    // Elimination of the prescribed unknowns is P18-SOLVE-001's.
    [[maybe_unused]] auto bad = system->reducedConductance();
#endif

#if defined(BETTERCAD_CF_THE_SYSTEM_HAS_NO_CONSTRAINED_MATRIX)
    [[maybe_unused]] auto bad = system->constrainedConductance();
#endif

#if defined(BETTERCAD_CF_THE_SYSTEM_HAS_NO_RESIDUAL)
    [[maybe_unused]] auto bad = system->residual();
#endif

#if defined(BETTERCAD_CF_THE_SYSTEM_HAS_NO_REACTIONS)
    [[maybe_unused]] auto bad = system->reactions();
#endif

#if defined(BETTERCAD_CF_THE_SYSTEM_HAS_NO_FACTORISATION)
    [[maybe_unused]] auto bad = system->factorisation();
#endif

#if defined(BETTERCAD_CF_THE_SYSTEM_HAS_NO_CAPACITY_OVER_TIME_STEP)
    // C / dt is P18-TRANSIENT-001's, because dt is not an input here.
    [[maybe_unused]] auto bad = system->capacityOverTimeStep();
#endif

#if defined(BETTERCAD_CF_THE_MATRIX_HAS_NO_DENSE_FORM)
    // A large mesh must never be made dense to be inspected.
    [[maybe_unused]] auto bad = system->conductance().toDense();
#endif

#if defined(BETTERCAD_CF_THE_MATRIX_HAS_NO_SYMMETRISE)
    // A gate, never a repair.
    [[maybe_unused]] auto bad = system->conductance().symmetrise();
#endif

#if defined(BETTERCAD_CF_THE_MATRIX_HAS_NO_REGULARISE)
    [[maybe_unused]] auto bad = system->conductance().regularise();
#endif

#if defined(BETTERCAD_CF_THE_MATRIX_HAS_NO_SETTER)
    [[maybe_unused]] auto bad = system->conductance().setCoeff(0, 0, 0.0);
#endif

#if defined(BETTERCAD_CF_THE_SOURCE_HAS_NO_SOLVER_SETTINGS)
    // The tolerance and the pivot floor do not enter K, C or Q.
    [[maybe_unused]] auto bad = system->source().solver;
#endif

#if defined(BETTERCAD_CF_THE_SOURCE_HAS_NO_TIME_STEP)
    [[maybe_unused]] auto bad = system->source().timeStep;
#endif

#if defined(BETTERCAD_CF_THE_SOURCE_HAS_NO_INITIAL_CONDITION)
    [[maybe_unused]] auto bad = system->source().initialCondition;
#endif

#if defined(BETTERCAD_CF_THE_SOURCE_HAS_NO_PRESCRIBED_TEMPERATURES)
    // A T_bar edit changes the constraint map, not this system.
    [[maybe_unused]] auto bad = system->source().prescribedTemperatures;
#endif

    return 0;
}
