// Build-failure tests for the structural acceptance policy (see CMakeLists.txt
// in this directory). Built once without any BETTERCAD_CF_* macro as a control,
// which must compile -- so every failure below is attributable to its own line.
//
// Six claims of P17-VALID-001 are enforced here rather than reviewed:
//
//   1. THE QUALITY ENTRY POINT TAKES A REPORT, NOT A MESH. P16 owns every
//      metric and there is one definition of each in the repository. A P17
//      function that accepted a `Mesh` would be a function that could compute
//      a radius ratio, and the whole point of the module boundary is that it
//      cannot. `validateStructuralMeshQuality(mesh)` must not compile.
//
//   2. THE SOLVE ENTRY POINT CANNOT BE HANDED A MESH EITHER. This is the gap
//      P16 recorded and could not close: a function taking a `VolumeMesh`
//      cannot tell a current mesh from one describing a body the user has
//      since changed. `solveStructuralAnalysis` takes a document, a
//      regenerator and a mesher and ASKS, so there is no overload to pass a
//      mesh to -- which is what makes the staleness check unbypassable rather
//      than merely documented (ADR-036, ADR-042).
//
//   3. A STATUS IS DERIVED AND NOT SET. `StructuralValidationReport::status()`
//      is a function of the findings, so a report carrying a refusal cannot be
//      made to claim acceptance. There is no `status` data member to assign.
//
//   4. THE ACCURACY FLOOR IS CONSTANT. `kStructuralRadiusRatioFloor` is
//      `constexpr`, so the one threshold that refuses a mesh cannot be
//      loosened at runtime. The brief forbids automatic tolerance loosening;
//      this makes the manual kind a compile error too.
//
//   5. A SOLVE OUTCOME CANNOT BE EMPTY. `StructuralSolveOutcome` holds a
//      `StructuralResult`, which has no default constructor and no public one
//      at all -- only the validating factory. So a default-constructed outcome
//      is unrepresentable, and a half-published solve cannot be expressed
//      rather than merely being discouraged.
//
//   6. A SEVERITY IS NOT A STATUS. `ValidationSeverity` says whether one
//      finding refuses; `ValidationStatus` says what the whole report came to.
//      Both are small enums and conflating them would silently compile if
//      either were an integer, so they are distinct scoped types.
#include <bettercad/core/Units.hpp>
#include <bettercad/meshing/MeshQuality.hpp>
#include <bettercad/structural/StructuralValidation.hpp>

using namespace bettercad;
using namespace bettercad::structural;

namespace {

void takesStatus(ValidationStatus /*unused*/) {}

} // namespace

int main() {
    // The legitimate spellings, so the control compiles.
    const meshing::QualityThresholds policy = structuralQualityThresholds();
    (void)policy.limits.size();

    StructuralValidationReport report;
    (void)report.status();
    (void)report.acceptable();
    (void)report.reachedStage;
    (void)report.constrainedDegreesOfFreedom;

    (void)severityOf(ValidationCode::ElementAccuracyBelowFloor);
    (void)toString(ValidationCode::NoLoadsApplied);
    (void)toString(ValidationSeverity::Warning);
    (void)toString(ValidationStatus::Rejected);
    (void)toString(StructuralValidationReport::Stage::Complete);
    takesStatus(ValidationStatus::Accepted);

    (void)kStructuralRadiusRatioFloor;
    (void)kStructuralStrainAccuracy;
    (void)kMinimumConstrainedDofs;
    (void)kSufficientRestraintIsTheSolversToJudge.size();

#if defined(BETTERCAD_CF_QUALITY_ENTRY_TAKES_A_MESH)
    // 1. One definition of every metric, and it is P16's. A P17 entry point
    //    that took a mesh could compute one.
    meshing::MeshBuilder builder;
    const Result<meshing::Mesh> mesh = builder.build();
    (void)validateStructuralMeshQuality(*mesh);
#endif

#if defined(BETTERCAD_CF_SOLVE_ENTRY_TAKES_A_MESH)
    // 2. The staleness hole, closed in the signature. There is no overload
    //    that accepts a mesh, a VolumeMesh or a StructuralModel.
    const meshing::VolumeMesh* held = nullptr;
    (void)solveStructuralAnalysis(*held, AnalysisId::fromValue(1));
#endif

#if defined(BETTERCAD_CF_STATUS_IS_ASSIGNED)
    // 3. A derived verdict cannot be overwritten, so a rejected report cannot
    //    be made to say Accepted.
    report.status = ValidationStatus::Accepted;
#endif

#if defined(BETTERCAD_CF_ACCURACY_FLOOR_IS_LOOSENED)
    // 4. No automatic tolerance loosening, and no manual loosening either.
    kStructuralRadiusRatioFloor = 1.0e-3;
#endif

#if defined(BETTERCAD_CF_SOLVE_OUTCOME_WITHOUT_A_RESULT)
    // 5. A half-published solve is unrepresentable: the result has no public
    //    constructor, only the validating factory.
    StructuralSolveOutcome empty;
    (void)empty.pivotRatio;
#endif

#if defined(BETTERCAD_CF_SEVERITY_USED_AS_A_STATUS)
    // 6. One finding's severity is not the whole report's verdict.
    takesStatus(ValidationSeverity::Failure);
#endif

    return 0;
}
