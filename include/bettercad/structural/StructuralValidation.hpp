#pragma once

// P17-VALID-001 -- the structural acceptance policy, and the one path that runs
// every gate.
//
// WHAT THIS FILE OWNS. P16-QUALITY-001 ships mesh metrics and states no
// opinion: `reportOnlyThresholds()` classifies nothing, and its own header says
// "`P17` owns what a structural analysis needs from a mesh".
// `StructuralModel::quality()` names this milestone as the owner. This is where
// that debt is paid, and it is paid with measurements -- see ADR-042 and
// `docs/verification/P17-VALID-001/`.
//
// THE TWO NUMBERS THAT REJECT A MESH ARE DERIVED, NOT CHOSEN, and there are
// TWO of them because a tetrahedron degenerates in two independent ways and
// NEITHER METRIC CAN SEE THE OTHER'S FAMILY. That was measured, not assumed,
// and an earlier draft of this milestone shipped one bound and was wrong.
//
//     FLATTENING, toward a plane -- a wedge with its apex brought down.
//         err  ~  C / sqrt(3r/R),    C between 2.59e-16 and 2.41e-15
//         measured over SIXTEEN orders of radius ratio, two displacement
//         fields three orders of strain apart. The ASPECT RATIO SATURATES at
//         sqrt(3) here and sees nothing.
//
//     STRETCHING, toward a line -- a needle with its base shrunk.
//         err  ~  C * aspect,        C between 1.76e-16 and 4.45e-16
//         measured over SEVEN orders of aspect ratio. THE RADIUS RATIO IS
//         THE POOR PREDICTOR here: a needle at 3r/R 1.73e-03 errs 4.45e-13
//         while a wedge at a WORSE 9.0e-04 errs only 1.17e-14, 38x less.
//
// So the recovered-strain error is not a function of either metric alone, and
// a single bound cannot guarantee an accuracy. Inverting each law at
// BetterCAD's documented 1e-9 band for geometric accumulation:
//
//     3r/R   >= 5.81e-12   ->  bound set at 1e-10, a 17x margin
//     aspect <= 2.25e+06   ->  bound set at 3e5, a 7.5x margin
//
// Each is many orders clear of the worst element of any qualified P16
// reference mesh (3r/R 3.22641e-04 and aspect 80.0062), so neither refuses
// anything BetterCAD produces. Full derivation and the two-sided verification
// in `docs/verification/P17-VALID-001/ACCURACY_LAW.md`.
//
// AND THE DIHEDRAL ANGLES CARRY NO FAILURE BOUND AT ALL, because no law was
// measured against them and the measured distributions show they cannot
// separate a qualified mesh from a pathological one:
//
//     on the MINIMUM DIHEDRAL the two sets INTERLEAVE -- a qualified cylinder
//     (0.362683 deg) is worse than a deliberate 1.98-degree sliver
//
// So a dihedral rejection line would refuse the cylinder BetterCAD itself
// produces. A 0.198-degree sliver PASSES both hard failures here, correctly:
// its measured strain error is 1.33e-13. Hard failure means "the kernel cannot
// recover a strain on this element to better than 1e-9", never "this element
// is badly shaped".
//
// WHAT IT NEVER DOES. Validation observes. It never moves a node, swaps a
// tetrahedron's nodes, takes an absolute value of a volume, deletes an
// element, merges nodes, remeshes, loosens a tolerance, repairs a support or
// invents a material property. Every entry point takes const references and
// returns a report.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/math/Vector.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/meshing/MeshQuality.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/structural/Export.hpp>
#include <bettercad/structural/StructuralAnalysis.hpp>
#include <bettercad/structural/StructuralConstraints.hpp>
#include <bettercad/structural/StructuralLoadVector.hpp>
#include <bettercad/structural/StructuralPost.hpp>
#include <bettercad/structural/StructuralReaction.hpp>
#include <bettercad/structural/StructuralResult.hpp>
#include <bettercad/structural/StructuralSolve.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bettercad::features {
class Regenerator;
} // namespace bettercad::features

namespace bettercad::structural {

// ---------------------------------------------------------------------------
// The policy
// ---------------------------------------------------------------------------

/// The radius ratio below which an element is refused.
///
/// DERIVED FROM A MEASUREMENT, and the derivation is in
/// `docs/verification/P17-VALID-001/ACCURACY_LAW.md`:
///
/// ```text
/// err <= C / sqrt(3r/R)          C <= 2.41122e-15, measured over 16 orders
/// err <= 1e-9                    required: BetterCAD's geometric band
///   =>  3r/R >= 5.81398e-12      the crossing point
/// 1e-10                          chosen, a 17x margin, bounding err at
///                                2.41e-10 -- 4.1x inside the requirement
/// ```
///
/// NO EXPORT MACRO, and it must not have one: a `BETTERCAD_*_EXPORT` on a
/// header-defined `constexpr` expands to `dllimport` and fails in a shared
/// build only, which is the slowest possible place to find out.
inline constexpr double kStructuralRadiusRatioFloor = 1.0e-10;

/// The aspect ratio above which an element is refused.
///
/// THE SECOND HALF OF THE SAME GUARANTEE, and it is not redundant with the
/// floor above: the two bound DIFFERENT degeneration families, and each metric
/// is blind to the other's.
///
/// ```text
/// err <= C * aspect              C <= 4.45e-16, measured over 7 orders
/// err <= 1e-9                    required: BetterCAD's geometric band
///   =>  aspect <= 2.25e+06       the crossing point
/// 3e5                            chosen, a 7.5x margin, bounding err at
///                                1.34e-10 -- and still 3750x above the
///                                worst qualified reference mesh (80.0062)
/// ```
///
/// NOT 1e5, AND THE REASON IS WORTH KEEPING. The needle fixture that measures
/// this law has an aspect ratio of 100000.2 at a 1e-4 mm base, so a bound at
/// 1e5 would sit a fifth of a unit from a measurement -- a threshold whose
/// verdict on its own evidence turns on rounding. 3e5 lies cleanly between
/// that fixture and the next one at 1e6.
inline constexpr double kStructuralAspectRatioCeiling = 3.0e5;

/// The recovered-strain accuracy the floor above guarantees.
inline constexpr double kStructuralStrainAccuracy = 1.0e-9;

/// The smallest number of constrained degrees of freedom that CAN remove a
/// 3D body's rigid-body modes.
///
/// A NECESSARY CONDITION AND NOT A SUFFICIENT ONE. See
/// `kSufficientRestraintIsTheSolversToJudge` below, which exists to be read.
inline constexpr std::size_t kMinimumConstrainedDofs = 6;

/// BetterCAD's structural mesh policy.
///
/// TWO HARD FAILURES, one per degeneration family, and WARNINGS on four
/// metrics at the measured edge of the qualified envelope:
///
/// ```text
/// metric                 warning      failure    qualified worst
/// TetRadiusRatio         3.0e-4       1e-10      0.000322641   RM-MESH-05
/// TetAspectRatio         81.0         3e5        80.0062       RM-MESH-05
/// TetMinDihedralAngle    6.0e-3 rad   none       0.006330012   RM-MESH-02
/// TetMaxDihedralAngle    3.125 rad    none       3.119060453   RM-MESH-05
/// ```
///
/// The two failure bounds are NOT two opinions about the same thing. A
/// flattening element's error tracks the radius ratio while its aspect ratio
/// saturates at sqrt(3); a stretching element's error tracks the aspect ratio
/// while its radius ratio understates the damage by 38x. Each bound catches the
/// family the other is blind to, which is why one of them alone was wrong.
///
/// Each warning bound is the worst value any qualified P16 reference model
/// exhibits, rounded AWAY from the qualified set so that no qualified mesh
/// warns and so the comparison cannot turn on a last-bit difference between
/// the measurement and the next build's. Nothing was rounded toward a
/// pathology to make a fixture warn.
///
/// WHAT A WARNING MEANS, exactly, and the diagnostic says it: this element is
/// outside the range of every mesh BetterCAD has qualified. It is an envelope,
/// not an engineering judgement. ITS WEAKNESS IS RECORDED WITH IT: the
/// envelope cannot flag a 1.98-degree sliver, because the qualified cylinder is
/// worse than one on both metrics -- and the measured distributions show no
/// threshold could.
///
/// Angles are in RADIANS, as every SI value in a `QualityThresholds` is. A
/// policy written in degrees would be a unit error P16's type cannot catch.
///
/// P16'S DEFAULT IS UNTOUCHED. This is a value handed to
/// `meshing::evaluateMeshQuality`, not a change to it: `reportOnlyThresholds()`
/// still classifies nothing and no meshing source file changed.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT meshing::QualityThresholds
structuralQualityThresholds();

// ---------------------------------------------------------------------------
// The report
// ---------------------------------------------------------------------------

/// Why a structural analysis is not acceptable as it stands, or what about it
/// deserves saying.
///
/// EVERY VALUE IS REACHABLE AND EVERY ONE IS TESTED. The list was trimmed to
/// that: a refusal nobody can reach is a refusal nobody has checked. Two of
/// them are reachable only through the quality entry point rather than through
/// a document, because `requireStructuralModel` already guarantees a held mesh
/// is structurally valid -- `Mesher::generate` refuses one that is not -- and
/// they are kept because the quality function is public and must not let the
/// library become the validator.
enum class ValidationCode : std::uint8_t {
    /// The document has no analysis with that id.
    AnalysisNotFound,
    /// The analysis's own definition does not validate, on its own terms.
    AnalysisDefinitionInvalid,
    /// `requireStructuralModel` refused. The report's `inputProblem` carries
    /// which of its reasons, unchanged: a stale mesh, an ineligible body, a
    /// configuration override, a missing material. THIS IS WHERE
    /// STALE-INPUT DETECTION ENTERS, and it is inherited rather than
    /// re-implemented -- P16 recorded that nothing forces a mesh holder to ask
    /// whether it is stale, and this is the somebody that asks.
    InputsUnavailable,
    /// The mesh violates a structural invariant. Not a quality score: an
    /// inverted, degenerate or repeated-handle element is not a tetrahedron.
    MeshStructurallyInvalid,
    /// At least one element is `QualityClass::Invalid` -- structurally refused,
    /// or a metric that could not be computed as a finite number. No threshold
    /// produces this classification.
    MeshHasInvalidElements,
    /// The quality policy the report was produced under is
    /// self-contradictory, so P16 classified nothing. A WARNING, and the
    /// reasoning matters because an earlier version of this file made it a
    /// refusal.
    ///
    /// A MESH CONTROL CARRIES ITS OWN `QualityThresholds` and the mesher
    /// evaluates the held report under them, so this is reachable from an
    /// ordinary document: a user puts a bound on a dimensioned size, or
    /// orders two bounds the wrong way round, and P16 records the diagnostic.
    ///
    /// But P16 still MEASURES every metric in that case and reports it as
    /// under the report-only default -- only the classification is skipped.
    /// This module reads `summaries`, so everything it needs is present and
    /// valid, and its own policy is fixed and validated. Refusing the solve
    /// would be refusing a structurally sound, accurate mesh because of an
    /// unrelated error in the user's REPORTING preferences, which is both the
    /// false rejection the brief warns against and a contradiction of ADR-042
    /// Decision 7 -- P16 stays report-only and P17 does not reach into its
    /// verdict.
    ///
    /// So the policy error is surfaced and the solve proceeds on P17's own
    /// verdict.
    QualityPolicyUnusable,
    /// An element is past one of the two accuracy bounds -- its radius ratio
    /// below `kStructuralRadiusRatioFloor`, or its aspect ratio above
    /// `kStructuralAspectRatioCeiling` -- so the recovered strain on it is not
    /// guaranteed to `kStructuralStrainAccuracy`. THE QUALITY HARD FAILURE,
    /// and the finding names which metric and which element.
    ElementAccuracyBelowFloor,
    /// An element is worse than every element of every qualified P16 reference
    /// mesh, on at least one metric. A WARNING, and never a refusal.
    ElementOutsideQualifiedEnvelope,
    /// The material does not supply what this analysis MODE requires. Distinct
    /// from `InputsUnavailable`, which only asks for linear-elastic constants:
    /// `LinearStaticWithGravity` additionally needs a density, and a model can
    /// pass the input boundary and fail here.
    MaterialUnusable,
    /// A restraint does not resolve against the current mesh.
    RestraintsUnresolvable,
    /// Fewer than `kMinimumConstrainedDofs` degrees of freedom are
    /// constrained, so rigid-body modes NECESSARILY remain.
    InsufficientRestraint,
    /// A load does not resolve against the current mesh.
    LoadsUnresolvable,
    /// The analysis applies no load at all. `u = 0` exactly, which is a
    /// correct answer to a question probably not meant. A WARNING.
    NoLoadsApplied,
};

[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::string_view
toString(ValidationCode code) noexcept;

/// Whether a finding refuses the solve.
enum class ValidationSeverity : std::uint8_t {
    /// Worth saying; the solve proceeds. Nothing in this module turns a
    /// warning into a refusal, and `status()` is what enforces that.
    Warning,
    /// The solve is refused.
    Failure,
};

[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::string_view
toString(ValidationSeverity severity) noexcept;

/// The severity `code` always carries.
///
/// A FUNCTION OF THE CODE, not a field a caller chooses, so the same problem
/// cannot be a failure in one report and a warning in another. The THREE
/// warnings are `ElementOutsideQualifiedEnvelope`, `NoLoadsApplied` and
/// `QualityPolicyUnusable`; everything else refuses.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT ValidationSeverity
severityOf(ValidationCode code) noexcept;

/// One metric's worst value and the element holding it.
///
/// NOT A `ValidationFinding`, and the distinction is the point. A finding is a
/// classified PROBLEM and carries a code and a severity; this is a
/// MEASUREMENT, recorded for every classifiable metric whether or not it
/// crossed a bound.
///
/// AN EARLIER DRAFT OF THIS MILESTONE REUSED `ValidationFinding` HERE and set
/// every entry's code to `ElementOutsideQualifiedEnvelope`, so a consumer
/// reading the payload rather than the message was told a metric had warned
/// when it had not. That is fabricated data in a structured field -- the exact
/// defect P16's own `QualityFinding` documentation warns about when it refuses
/// to name `TetVolume` with a value of 0 for an inverted element. Adversarial
/// review caught it; this type is the fix.
struct MetricObservation {
    meshing::QualityMetric metric{};
    /// The element holding the worst value FOR THIS METRIC, by this metric's
    /// own direction -- read from `MetricSummary::worst`, never recomputed.
    meshing::ElementId element{};
    /// The worst value, in the metric's SI unit.
    double value = 0.0;

    friend bool operator==(const MetricObservation&, const MetricObservation&) = default;
};

/// One classified observation about one analysis.
struct ValidationFinding {
    ValidationCode code = ValidationCode::AnalysisNotFound;
    /// `severityOf(code)`, carried so a reader of the payload does not have to
    /// call it, and asserted equal to it on construction.
    ValidationSeverity severity = ValidationSeverity::Failure;
    /// Which metric, when the finding is about one. Absent otherwise -- never
    /// a default enumerator, for the reason `QualityFinding` gives: naming a
    /// metric that was not measured is fabricated data in a structured field.
    std::optional<meshing::QualityMetric> metric{};
    /// Which element, when the finding is about one.
    std::optional<meshing::ElementId> element{};
    /// The value measured, in the metric's SI unit.
    std::optional<double> value{};
    /// The bound crossed.
    std::optional<double> threshold{};
    /// A fallback for logs. The payload is the fields above; nothing should
    /// have to parse this.
    std::string message;

    friend bool operator==(const ValidationFinding&, const ValidationFinding&) = default;
};

/// What a validation came to.
enum class ValidationStatus : std::uint8_t {
    /// Nothing to report. The solve may proceed.
    Accepted,
    /// Findings, none of them refusals. The solve may proceed AND the
    /// warnings are worth showing: an envelope warning means results near
    /// that element carry larger discretisation error.
    AcceptedWithWarnings,
    /// At least one refusal. The solve must not proceed.
    Rejected,
};

[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::string_view
toString(ValidationStatus status) noexcept;

/// Everything validation found, and what it measured on the way.
///
/// THE STATUS IS DERIVED, NEVER STORED. `status()` is computed from the
/// findings, so a report carrying a `Failure` cannot claim to be `Accepted`.
/// The construction `MeshQualityReport::satisfiesPolicy()` uses, for the same
/// reason: it makes the inconsistent state unrepresentable rather than
/// unlikely.
///
/// DETERMINISTIC. `findings` is ordered by severity (refusals first), then by
/// code, then by element -- so a report can be diffed. No wall clock, no
/// pointer identity, no unordered container anywhere in its construction.
struct StructuralValidationReport {
    std::vector<ValidationFinding> findings{};

    /// `requireStructuralModel`'s reason, when that is what refused.
    std::optional<InputProblem> inputProblem{};

    /// What was measured before the chain stopped. Zero for a stage never
    /// reached, which `reachedStage` distinguishes from a stage that measured
    /// zero.
    std::size_t nodeCount = 0;
    std::size_t tetCount = 0;
    std::size_t constrainedDegreesOfFreedom = 0;
    std::size_t loadedNodes = 0;

    /// The worst value of each classifiable Tet4 metric, and the element
    /// holding it, READ from `MeshQualityReport::summaries` rather than
    /// recomputed. Empty when the quality stage was not reached.
    ///
    /// MEASUREMENTS, NOT FINDINGS, and separate from `findings` for that
    /// reason: every classifiable metric appears here whether it passed or
    /// not, so an entry carries no verdict and `status()` does not consult
    /// this list.
    ///
    /// ONE DEFINITION OF EVERY METRIC EXISTS IN THE REPOSITORY and it is in
    /// `src/meshing/MeshQuality.cpp`. A second radius-ratio formula in P17 is
    /// the defect the module boundary exists to prevent.
    std::vector<MetricObservation> worstByMetric{};

    /// How far the staged chain got. Stages are DEPENDENT -- a restraint
    /// cannot resolve without a mesh -- so the chain stops at the first
    /// refusing stage rather than reporting a cascade of consequences.
    enum class Stage : std::uint8_t {
        Analysis,
        Inputs,
        Quality,
        Material,
        Restraints,
        Loads,
        /// Every stage ran.
        Complete,
    };
    Stage reachedStage = Stage::Analysis;

    /// Refusals first, then warnings. Derived, so it cannot disagree with
    /// `findings`.
    ///
    /// DEFINED INLINE, AND NOT BY PREFERENCE. An out-of-line member of a
    /// struct that carries no export macro is not exported, and that defect
    /// has now recurred in three BetterCAD modules -- caught only by
    /// `debug-shared-ext`, which is the slowest place to find it. A plain data
    /// struct keeps its logic in the header.
    [[nodiscard]] ValidationStatus status() const noexcept {
        bool anyWarning = false;
        for (const ValidationFinding& finding : findings) {
            if (finding.severity == ValidationSeverity::Failure) {
                return ValidationStatus::Rejected;
            }
            anyWarning = true;
        }
        return anyWarning ? ValidationStatus::AcceptedWithWarnings : ValidationStatus::Accepted;
    }

    /// Whether a solve may proceed. `status() != Rejected`.
    [[nodiscard]] bool acceptable() const noexcept {
        return status() != ValidationStatus::Rejected;
    }

    friend bool operator==(const StructuralValidationReport&,
                           const StructuralValidationReport&) = default;
};

[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::string_view
toString(StructuralValidationReport::Stage stage) noexcept;

// ---------------------------------------------------------------------------
// The mesh entry point
// ---------------------------------------------------------------------------

/// Classifies @p quality under BetterCAD's structural policy.
///
/// Takes a REPORT rather than a mesh, deliberately: the metrics are P16's and
/// are not recomputed here. It reads `summaries` -- a minimum, maximum, mean
/// and worst element per metric, present whatever policy produced the report --
/// and compares them against `structuralQualityThresholds()` using P16's own
/// `direction(metric)`, so the good direction is asked rather than assumed.
///
/// PUBLIC AND USABLE ON ANY REPORT, which is what makes
/// `MeshStructurallyInvalid` and `MeshHasInvalidElements` reachable: a HELD
/// mesh can never be structurally invalid, because `Mesher::generate` refuses
/// one, so those two refusals would otherwise be untestable. They are not
/// defence in depth for its own sake; they are the contract of a function the
/// library does not control the input to.
///
/// `QualityPolicyUnusable` is different and is reachable from an ordinary
/// document: a `MeshControl` carries its own `QualityThresholds` and the
/// mesher evaluates the held report under them. It is a WARNING for that
/// reason -- refusing would block a sound solve over an unrelated error in the
/// caller's reporting preferences.
///
/// Comparisons are STRICT in the same direction P16's are, so a value exactly
/// equal to a bound is on the good side of it.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT StructuralValidationReport
validateStructuralMeshQuality(const meshing::MeshQualityReport& quality);

// ---------------------------------------------------------------------------
// The analysis entry point
// ---------------------------------------------------------------------------

/// Everything a solve requires of @p analysis, checked against the document as
/// it stands.
///
/// ALWAYS RETURNS A REPORT, never an error: "this model is not ready, and
/// here is why" is the answer, not a failure to answer. A GUI greys out a
/// button with it and a CLI prints it, neither paying for a factorisation.
///
/// THE ORDER IS PART OF THE CONTRACT, because the stages depend on each other
/// and a cascade of consequences buries the cause:
///
/// ```text
/// 1  the analysis exists and its definition validates
/// 2  requireStructuralModel -- geometry current, MESH CURRENT, mesh held,
///    generation not failed, material present and linear-elastic
/// 3  mesh quality under the structural policy
/// 4  the material supplies what this MODE needs
/// 5  the restraints resolve, and enough degrees of freedom are constrained
/// 6  the loads resolve
/// ```
///
/// WITHIN a stage every finding is reported. That is a real difference only at
/// the quality stage, which can report four metrics at once, and it is stated
/// precisely rather than generously: the restraint and load stages call
/// `prepareStructuralRestraints` and `prepareStructuralLoads`, which each
/// return ONE diagnostic naming the first target that did not resolve. That is
/// P17-BC-001's and P17-LOAD-001's contract and this milestone does not
/// re-litigate it.
///
/// BETWEEN stages the chain stops. That is deliberately not what
/// `staleReasons` does, and the difference is principled -- stale reasons are
/// INDEPENDENT, so a user fixing one should not have to re-run to find the
/// other, while a restraint failure caused by a stale mesh is a true statement
/// that sends the reader to the wrong place.
///
/// Read-only in every argument. Nothing is regenerated, remeshed, healed,
/// repaired or assigned, and in particular this never calls
/// `Mesher::generate`: a solve that silently remeshed would hide both the cost
/// and the intent change behind it.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT StructuralValidationReport
validateStructuralAnalysisForSolve(const Document& document,
                                   const features::Regenerator& regenerator,
                                   const meshing::Mesher& mesher, AnalysisId analysis);

/// Why sufficiency is NOT decided here, kept where it will be read.
///
/// ```text
/// constrained DOFs <  6   ->   NECESSARILY singular. Refused, with a
///                              diagnostic naming the count.
/// constrained DOFs >= 6   ->   SAYS NOTHING. Six constrained degrees of
///                              freedom all in z remove z-translation and two
///                              rotations and leave three rigid modes.
/// ```
///
/// The first is linear algebra: a 3D continuum's rigid-body null space is
/// six-dimensional and the rank of a constraint set cannot exceed the number
/// of degrees of freedom in it, so fewer than six cannot remove six. It holds
/// whatever the mesh.
///
/// The second is why `P17-SOLVE-001` keeps the verdict. Its pivot gate
/// measures `min|D| / max|D|` of the actual factorisation and refuses a
/// singular system, and it says "may be under-constrained" because it
/// genuinely cannot distinguish a mechanism from a disconnected unrestrained
/// region from bad conditioning. What this check adds is a NAME for the case
/// it can be sure of, before a factorisation spends time proving it.
inline constexpr std::string_view kSufficientRestraintIsTheSolversToJudge =
    "fewer than six constrained degrees of freedom is necessarily singular; six or more is "
    "not sufficiency, and the factorisation's pivot ratio decides";

// ---------------------------------------------------------------------------
// The solve
// ---------------------------------------------------------------------------

/// A validated, solved, equilibrium-checked structural analysis.
///
/// WHAT IT CARRIES BEYOND THE RESULT is the measured evidence of every gate
/// that was passed, each read from the stage that OWNS it rather than
/// recomputed here:
///
/// ```text
/// residual, pivotRatio      P17-SOLVE-001's gates, as it measured them
/// forceBalance,             P17-REACTION-001's equilibrium, at the tolerance
/// momentBalance             the caller asked for
/// largestDisplacement,      P17-POST-001's extremes
/// largestVonMises
/// ```
///
/// THIS MILESTONE ADDS NO SECOND THRESHOLD TO ANY OF THEM. Solver-residual
/// acceptance, equilibrium acceptance and result finiteness are already gates
/// that refuse rather than warn -- `solveStructuralSystem` publishes nothing
/// on a residual failure, `recoverSupportReactions` nothing on an imbalance,
/// and `StructuralResult::create` nothing on a non-finite value. Acceptance
/// here is defined as "that gate passed, and this is the number it passed
/// with". A second tolerance beside a measured one would be a second answer
/// that could drift, and loosening one is forbidden outright.
struct StructuralSolveOutcome {
    /// The input report. Carries any WARNINGS, which a successful solve does
    /// not discard: a result computed on a mesh outside the qualified envelope
    /// is still a result that should be shown with the warning.
    StructuralValidationReport validation{};
    /// The published result, keyed to the mesh it was computed on.
    StructuralResult result;

    ResidualMetrics residual{};
    double pivotRatio = 0.0;
    Energy strainEnergy{};
    std::size_t freeEquations = 0;

    ForceBalance forceBalance{};
    MomentBalance momentBalance{};

    Length largestDisplacement{};
    Stress largestVonMises{};
};

/// Validates, assembles, solves, recovers and publishes -- or fails saying
/// which stage refused.
///
/// THE ONLY PATH FROM A DOCUMENT TO A `StructuralResult` IN THIS LIBRARY, and
/// that is the point. Every gate this calls already existed before this
/// milestone; what did not exist was anything that ran them in order.
/// `solveStructuralSystem` had no production caller, which meant the safety was
/// available rather than enforced -- the same gap P16 recorded for meshes
/// ("nothing FORCES the holder of a mesh to ask whether it is stale") and
/// ADR-036 answered with possession rather than documentation.
///
/// What it does, in order:
///
/// ```text
/// 1  validateStructuralAnalysisForSolve, and STOP on Rejected
/// 2  resolve the material for the analysis mode
/// 3  prepare the restraints and the loads against the CURRENT mesh
/// 4  assemble K and F
/// 5  solve, with P17-SOLVE's settings gates, pivot gate and residual gate
/// 6  recover the fields
/// 7  recover the reactions and CHECK EQUILIBRIUM
/// 8  publish a StructuralResult, which refuses a non-finite value
/// ```
///
/// Nothing is published unless every one of those passed. There is no partial
/// outcome: a failure returns an error and no result, because a result that
/// claims to be a solution and is not is worse than no result.
///
/// HONESTLY ON "CANNOT BE BYPASSED": the stages below this remain public,
/// because P17's analytical validation needs them -- a structural fixture
/// cannot pose a system whose answer is known in closed form. So a caller
/// determined to assemble and solve by hand still can, and gets a
/// `SolvedSystem` with every one of ITS own gates applied. What they cannot do
/// is obtain a `StructuralResult` for a document without going through
/// `currentResultSource`, which calls `requireStructuralModel` and inherits
/// every input gate. This is the production path and the only one that is used.
///
/// @p origin is the point moments are taken about. It is explicit because
/// equilibrium holds about EVERY origin and a caller reporting about a support
/// wants that one -- and because a mutation proved a default of zero made an
/// origin-ignoring path a no-op.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Result<StructuralSolveOutcome>
solveStructuralAnalysis(const Document& document, const features::Regenerator& regenerator,
                        const meshing::Mesher& mesher, AnalysisId analysis,
                        const SolverSettings& settings = {},
                        const Point3D& origin = Point3D{},
                        const EquilibriumTolerance& tolerance = {});

} // namespace bettercad::structural
