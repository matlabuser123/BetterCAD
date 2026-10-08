// Build-failure tests for the linear static solve (see CMakeLists.txt in this
// directory). Built once without any BETTERCAD_CF_* macro as a control, which
// must compile -- so every failure below is attributable to its own line.
//
// Three claims of P17-SOLVE-001 are enforced here rather than reviewed:
//
//   1. A REDUCED SYSTEM AND A SOLUTION CANNOT BE CONJURED. Each has a private
//      constructor and exactly one friend, so possessing one is the evidence
//      that the system and the constraints belonged to the same mesh, that the
//      factorisation and its pivot ratio passed, that every component is
//      finite, and that the independent residual passed (ADR-036, ADR-039). A
//      hand-built SolvedSystem would be a displacement field nobody validated.
//
//   2. THE SETTINGS OF A DIRECT SOLVER DO NOT HAVE AN ITERATION COUNT. There
//      is no `maximumIterations`, no `relativeTolerance` and no
//      preconditioner, because SimplicialLDLT is direct and a field that
//      existed but did nothing would be a false capability. Referring to one
//      must not compile.
//
//   3. A RESIDUAL IS A FORCE AND A DISPLACEMENT IS A LENGTH. `K u = F` is
//      dimensional and the compiler keeps it so.
#include <bettercad/core/Units.hpp>
#include <bettercad/structural/StructuralSolve.hpp>

using namespace bettercad;
using namespace bettercad::structural;

namespace {

void takesForce(Force /*unused*/) {}
void takesLength(Length /*unused*/) {}

} // namespace

int main() {
    // The legitimate spellings, so the control compiles.
    SolverSettings settings;
    settings.relativeResidualTolerance = 1.0e-9;
    settings.pivotFloor = 1.0e-12;
    settings.algorithm = SolverAlgorithm::SimplicialLdlt;
    takesForce(Force::fromSi(1.0));
    takesLength(Length::fromSi(1.0));
    (void)validate(settings);

#if defined(BETTERCAD_CF_FREE_SYSTEM_CONSTRUCTED_DIRECTLY)
    // Only extractFreeSystem may build one, because only it has checked that
    // the global system and the numbering belong to the same mesh.
    const FreeSystem reduced{};
    (void)reduced;
#endif

#if defined(BETTERCAD_CF_SOLVED_SYSTEM_CONSTRUCTED_DIRECTLY)
    // And only solveStructuralSystem may build a solution, because only it has
    // applied all three gates. There is no default constructor at all.
    const SolvedSystem solved{};
    (void)solved;
#endif

#if defined(BETTERCAD_CF_SETTINGS_HAVE_MAXIMUM_ITERATIONS)
    // A direct solver has no iteration count. The field is ABSENT rather than
    // present and ignored, which is what this refusal proves.
    settings.maximumIterations = 100;
#endif

#if defined(BETTERCAD_CF_SETTINGS_HAVE_PRECONDITIONER)
    settings.preconditioner = 0;
#endif

#if defined(BETTERCAD_CF_RESIDUAL_NORM_AS_LENGTH)
    // ||r|| is a force in newtons, not a displacement in metres.
    const ResidualMetrics metrics;
    takesLength(metrics.euclideanNorm);
#endif

#if defined(BETTERCAD_CF_RESIDUAL_NORM_AS_DOUBLE)
    // Nor does it decay to a number, so it cannot be compared against the
    // dimensionless normalized residual by accident -- which is exactly the
    // confusion brief section 81 warns about.
    const ResidualMetrics metrics2;
    const double raw = metrics2.euclideanNorm;
    (void)raw;
#endif

    return 0;
}
