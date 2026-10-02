#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/meshing/Export.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/meshing/MeshValidation.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Mesh quality metrics and classification (P16-QUALITY-001).
//
// EVERY METRIC IS DEFINED MATHEMATICALLY BEFORE ANY THRESHOLD EXISTS, which is
// this milestone's primary rule. A metric here is a formula with a range, an
// ideal value and a direction; a threshold is a separate, policy-level choice
// that cannot change a computed number. The backend contributes nothing to
// either -- nglib exposes no quality API at all, so there is no Netgen score
// to reverse-engineer a meaning for.
//
// VALIDITY IS NOT QUALITY, and the boundary is absolute:
//
//   STRUCTURAL (P16-DATA-001's validate(), reused unchanged)
//       a node reference that does not resolve, a repeated handle, a
//       non-finite coordinate, a ZERO or NEGATIVE signed volume, a duplicate
//       tetrahedron. The mesh does not describe a body. Always INVALID.
//
//   QUALITY (here)
//       aspect ratio, radius ratio, dihedral angles, edge distribution. The
//       mesh is a valid discretisation that may solve badly. A matter of
//       degree, and of a threshold someone chose.
//
// An inverted tetrahedron is NOT "poor quality with a low score": it is
// invalid, and this file never converts structural invalidity into a number
// and continues. There is no abs() anywhere in it.
//
// THIS LAYER OBSERVES AND CLASSIFIES. It never moves a node, reorders
// connectivity, drops an element or returns a repaired mesh. Every entry point
// takes a `const Mesh&` and returns values; a mesh optimiser, if one is ever
// wanted, is separate functionality with its own explicit contract.
namespace bettercad::meshing {

/// A quality metric, as a name for reports and thresholds.
///
/// The enumeration order is the report's order, so it is part of the
/// deterministic output rather than an implementation detail.
enum class QualityMetric : std::uint8_t {
    // --- Tet4 ---------------------------------------------------------------
    /// Signed volume, V = 1/6 det(p2-p1, p3-p1, p4-p1). P16-DATA-001's
    /// `signedVolume`, reused: this file does not redefine the convention.
    TetVolume,
    /// det(J) for the linear Tet4 reference map, J = [p2-p1 p3-p1 p4-p1].
    /// EXACTLY 6V, which is why it is derived rather than measured
    /// independently -- see `TetQuality::jacobianDeterminant`.
    TetJacobianDeterminant,
    /// Shortest of the six edges.
    TetMinEdgeLength,
    /// Longest of the six edges.
    TetMaxEdgeLength,
    /// Arithmetic mean of the six edges.
    TetMeanEdgeLength,
    /// l_max / l_min. Dimensionless, [1, inf), ideal 1, worse larger.
    TetAspectRatio,
    /// 3r / R. Dimensionless, (0, 1], ideal 1, worse toward 0.
    TetRadiusRatio,
    /// r = 3V / A_total, the inscribed sphere's radius.
    ///
    /// Named as a metric rather than left as a bare field of `TetQuality`,
    /// because EVERY measured quantity needs a name a report can print and a
    /// finding can point at. The two radii were the only ones without one, and
    /// the circumradius is the quantity that goes non-finite for a
    /// near-degenerate element -- so without a name, the report could say an
    /// element was undefined but not which number failed.
    TetInradius,
    /// R, the circumscribed sphere's radius.
    TetCircumradius,
    /// Smallest of the six INTERNAL dihedral angles. (0, pi), worse toward 0.
    TetMinDihedralAngle,
    /// Largest of the six INTERNAL dihedral angles. (0, pi), worse toward pi.
    TetMaxDihedralAngle,

    // --- Triangle3 ----------------------------------------------------------
    /// Unsigned area.
    TriangleArea,
    TriangleMinEdgeLength,
    TriangleMaxEdgeLength,
    TriangleMeanEdgeLength,
    /// 4 sqrt(3) A / (l1^2 + l2^2 + l3^2). Dimensionless, (0, 1], ideal 1.
    TriangleShapeQuality,
    /// Smallest interior angle. (0, pi), worse toward 0.
    TriangleMinAngle,
    /// Largest interior angle. (0, pi), worse toward pi.
    TriangleMaxAngle,
};

[[nodiscard]] BETTERCAD_MESHING_EXPORT std::string_view toString(QualityMetric metric) noexcept;

/// Which way is better, for a metric that is a quality score at all.
enum class QualityDirection : std::uint8_t {
    /// A larger value is better: radius ratio, minimum angles, shape quality.
    HigherIsBetter,
    /// A smaller value is better: aspect ratio, maximum angles.
    LowerIsBetter,
    /// NOT A QUALITY SCORE. A dimensioned size -- a volume, an edge length, an
    /// area -- summarised for context because it tells a reader how big the
    /// elements are, and never classified: "is this volume good?" has no
    /// scale-free answer, so a threshold on it would be a threshold on the
    /// model's units. `validate(QualityThresholds)` refuses one.
    ContextOnly,
};

[[nodiscard]] BETTERCAD_MESHING_EXPORT QualityDirection direction(QualityMetric metric) noexcept;

/// What a metric is measured in, for reports. SI, as everything is internally.
[[nodiscard]] BETTERCAD_MESHING_EXPORT std::string_view unitOf(QualityMetric metric) noexcept;

/// How an element came out.
enum class QualityClass : std::uint8_t {
    /// Structurally valid and within every configured threshold.
    Valid,
    /// Structurally valid and past a warning threshold. Usable; the report
    /// says so.
    Warning,
    /// Structurally valid and past a failure threshold. The mesh does not
    /// satisfy the active policy; whether that blocks an analysis is P17's
    /// decision, not this layer's.
    Failure,
    /// NOT A QUALITY VERDICT. The element violates a structural invariant, or
    /// a metric could not be computed as a finite number. Never reachable by
    /// a threshold, and never produced by a bad-but-valid element.
    Invalid,
};

[[nodiscard]] BETTERCAD_MESHING_EXPORT std::string_view toString(QualityClass classification) noexcept;

/// Every metric of one tetrahedron.
///
/// `defined` is false when a metric could not be computed as a finite number
/// -- a near-degenerate element can make the circumcentre solve
/// ill-conditioned. The element is then `Invalid`, not "bad but valid": an
/// infinity is never clamped into a finite score.
struct TetQuality {
    ElementId element{};
    /// SIGNED, and positive for a valid element. There is no absolute value
    /// anywhere in this file: the sign is what distinguishes an inverted
    /// element from a correct one, and hiding it is the defect the convention
    /// exists to expose.
    Volume volume{};
    Length minEdge{};
    Length maxEdge{};
    Length meanEdge{};
    /// l_max / l_min.
    double aspectRatio = 0.0;
    /// 3r / R.
    double radiusRatio = 0.0;
    /// r = 3V / A_total, the inscribed sphere's radius.
    Length inradius{};
    /// R = |c - p1| for the circumcentre c.
    Length circumradius{};
    /// INTERNAL dihedral angles, measured inside the material.
    Angle minDihedral{};
    Angle maxDihedral{};
    /// Every metric above is finite.
    bool defined = false;

    /// det(J) = 6V exactly, for the linear reference map
    /// J = [p2-p1  p3-p1  p4-p1].
    ///
    /// Derived rather than stored, because storing it would invite the two
    /// numbers drifting apart. It has the dimension of volume, which is why it
    /// is a `Volume` and not a bare double -- "the Jacobian" of a linear Tet4
    /// is not a dimensionless quantity, and calling it one is how signed
    /// volume and Jacobian get conflated.
    [[nodiscard]] Volume jacobianDeterminant() const noexcept {
        return Volume::fromSi(6.0 * volume.si());
    }

    friend bool operator==(const TetQuality&, const TetQuality&) = default;
};

/// Every metric of one surface triangle.
struct TriangleQuality {
    ElementId element{};
    Area area{};
    Length minEdge{};
    Length maxEdge{};
    Length meanEdge{};
    /// 4 sqrt(3) A / sum l^2.
    double shapeQuality = 0.0;
    Angle minAngle{};
    Angle maxAngle{};
    bool defined = false;

    friend bool operator==(const TriangleQuality&, const TriangleQuality&) = default;
};

/// Metrics of one tetrahedron of @p mesh.
///
/// Fails with InvalidArgument when a node handle does not resolve, and with
/// FailedPrecondition when the element is structurally invalid -- a repeated
/// handle, a non-finite coordinate, or a signed volume that is not positive
/// and finite. QUALITY IS NOT COMPUTED FOR AN INVALID ELEMENT, because a
/// number describing the shape of something that is not a tetrahedron invites
/// being compared against a threshold.
[[nodiscard]] BETTERCAD_MESHING_EXPORT Result<TetQuality> evaluateTetQuality(const Mesh& mesh,
                                                                             const Tetrahedron& tet);

/// Metrics of one triangle of @p mesh. The same failures, with zero or
/// non-finite area as the structural one.
[[nodiscard]] BETTERCAD_MESHING_EXPORT Result<TriangleQuality>
evaluateTriangleQuality(const Mesh& mesh, const Triangle& triangle);

/// One metric's limits. An absent bound means the metric is reported and not
/// classified -- stated by `std::optional`, never by a sentinel value.
struct QualityThreshold {
    std::optional<double> warning{};
    std::optional<double> failure{};

    friend bool operator==(const QualityThreshold&, const QualityThreshold&) = default;
};

/// The active quality policy.
///
/// SEPARATE FROM METRIC COMPUTATION, and that separation is the point: a
/// policy decides how a number is classified and can never change the number.
/// A test runs the same mesh through a lenient and a strict policy and
/// requires identical metrics with different classifications.
///
/// Comparisons are STRICT, so a value exactly equal to a threshold is on the
/// good side of it:
///
///   HigherIsBetter   value < failure -> Failure; else value < warning -> Warning
///   LowerIsBetter    value > failure -> Failure; else value > warning -> Warning
///
/// Angles are in radians, as every SI value here is. A caller writing a
/// policy in degrees converts at the boundary, as with any other quantity.
struct QualityThresholds {
    /// Keyed by metric, so iteration follows the enumeration order and the
    /// report cannot depend on a hash.
    std::map<QualityMetric, QualityThreshold> limits{};

    friend bool operator==(const QualityThresholds&, const QualityThresholds&) = default;
};

/// Checks a policy for contradictions.
///
/// Rejects a non-finite bound; a bound on a `ContextOnly` metric, which has no
/// scale-free good direction; and bounds ordered the wrong way round -- for a
/// higher-is-better metric a failure bound must not exceed its warning bound,
/// and for lower-is-better the reverse. A policy that would classify a value
/// as Failure while calling it better than the warning bound is not a strict
/// policy, it is a mistake.
[[nodiscard]] BETTERCAD_MESHING_EXPORT Result<void> validate(const QualityThresholds& thresholds);

/// BetterCAD's default policy: **report only**, with no thresholds at all.
///
/// A DELIBERATE CHOICE, not an omission, and the milestone brief sanctions it
/// explicitly. Quality thresholds are solver requirements, and no solver
/// exists yet: `P17` owns what a structural analysis needs from a mesh.
/// Inventing numbers now would be fabricating engineering judgement and
/// dressing it as a default, and every reference mesh would then be graded
/// against a standard nobody set.
///
/// So P16 ships the metrics, the machinery and the semantics, and ships no
/// opinion. Under this policy a structurally valid element is `Valid`, the
/// report carries every measured number, and nothing is hidden behind a
/// pass/fail nobody can justify.
[[nodiscard]] BETTERCAD_MESHING_EXPORT QualityThresholds reportOnlyThresholds();

/// One classified observation about one element.
struct QualityFinding {
    /// Which metric, when the finding is about one.
    ///
    /// ABSENT when the finding is about the element as a whole -- a structural
    /// refusal, where no metric was computed at all. Optional rather than a
    /// default enumerator, because naming `TetVolume` with a value of 0 for an
    /// inverted element whose volume is NEGATIVE would be fabricated data in a
    /// structured field, and a GUI or a CLI reading the payload instead of the
    /// message would believe it.
    std::optional<QualityMetric> metric{};
    ElementId element{};
    ElementType type = ElementType::Tetrahedron4;
    /// The value measured, in the metric's SI unit. Absent when no value was
    /// produced, for the same reason `metric` is.
    std::optional<double> value{};
    /// The bound crossed. Absent for an `Invalid` finding, which no threshold
    /// produced.
    std::optional<double> threshold{};
    QualityClass classification = QualityClass::Valid;
    /// A fallback for logs. The payload is the fields above; nothing should
    /// have to parse this.
    std::string message;

    friend bool operator==(const QualityFinding&, const QualityFinding&) = default;
};

/// One metric across the elements it applies to.
struct MetricSummary {
    std::size_t count = 0;
    double minimum = 0.0;
    double maximum = 0.0;
    /// The arithmetic mean over ELEMENTS -- one contribution per element, not
    /// per edge. Stated because "mean edge length" has two readings and the
    /// other one (every edge occurrence, counting a shared edge once per
    /// element that uses it) gives a different number.
    double mean = 0.0;
    /// The element holding the worst value FOR THIS METRIC, by this metric's
    /// own direction. Invalid for a `ContextOnly` metric, which has no worst.
    ElementId worst{};

    friend bool operator==(const MetricSummary&, const MetricSummary&) = default;
};

/// What a mesh's quality is.
///
/// WORST ELEMENT IS PER METRIC, deliberately. A single universal ranking would
/// need weights nobody can justify, and it would hide failure modes: a sliver
/// and a stretched element are bad in different ways, and a combined score
/// would report one number for both. `summaries` carries a worst element for
/// every classifiable metric; there is no overall score.
struct MeshQualityReport {
    /// P16-DATA-001's structural verdict, reused unchanged and run FIRST.
    MeshValidationReport structural{};
    /// `structural.dataValid()`, hoisted for readability.
    bool structurallyValid = false;

    std::size_t tetCount = 0;
    std::size_t triangleCount = 0;

    /// Elements by classification. They sum to tetCount + triangleCount.
    std::size_t validElements = 0;
    std::size_t warningElements = 0;
    std::size_t failureElements = 0;
    std::size_t invalidElements = 0;

    /// Per-element metrics, in ascending ElementId.
    std::vector<TetQuality> tets{};
    std::vector<TriangleQuality> triangles{};

    /// Per-metric aggregates, keyed by metric so iteration is enumeration
    /// order.
    std::map<QualityMetric, MetricSummary> summaries{};

    /// Every classified observation, ordered by classification (most severe
    /// first), then metric, then ElementId. Deterministic, and documented as
    /// such because a report whose order moves cannot be diffed.
    std::vector<QualityFinding> findings{};

    /// The policy the classifications were made under, recorded so a reader
    /// knows what the verdict means.
    QualityThresholds thresholds{};

    /// What is wrong with that policy, when something is.
    ///
    /// A CONTRADICTORY POLICY IS NOT SILENTLY IGNORED. `evaluateMeshQuality`
    /// runs `validate(thresholds)` itself and, if it fails, classifies nothing
    /// -- every metric is measured and reported as under the report-only
    /// default -- records the diagnostic here, and makes `satisfiesPolicy()`
    /// false. A threshold on a dimensioned size would otherwise simply never
    /// be consulted, and a control that quietly does nothing is the defect
    /// P16-SIZE-001's audit found four times over.
    std::optional<Error> thresholdPolicyError{};

    /// Structurally valid, no element failed the active policy, and the policy
    /// itself was usable.
    ///
    /// Note what this is not: it is not "the mesh is good". Under the default
    /// report-only policy nothing can fail, so this reduces to structural
    /// validity -- which is the honest answer when no solver has stated a
    /// requirement.
    ///
    /// A policy that did not validate makes this false, because the verdict
    /// was never reached. Reporting a pass nobody can justify is worse than
    /// reporting that the question could not be answered.
    [[nodiscard]] bool satisfiesPolicy() const noexcept {
        return structurallyValid && failureElements == 0 && invalidElements == 0 &&
               !thresholdPolicyError.has_value();
    }

    friend bool operator==(const MeshQualityReport&, const MeshQualityReport&) = default;
};

/// Evaluates @p mesh against @p thresholds.
///
/// STRUCTURE FIRST. `validate(mesh)` runs before any metric, and its verdict
/// is reported as it is. An element that fails structurally is counted
/// `Invalid` and gets no metrics, so a report can never say a mesh is fine
/// because its numbers looked fine.
///
/// THE POLICY IS VALIDATED TOO. `validate(thresholds)` runs first; a policy
/// that fails it classifies nothing and is reported through
/// `MeshQualityReport::thresholdPolicyError`, rather than being partly honoured
/// and partly ignored.
///
/// An EMPTY mesh is structurally invalid already (`MeshIssueKind::EmptyMesh`),
/// so it cannot come back as a pass for want of bad elements.
///
/// Read-only in its argument, and nothing it returns aliases the mesh.
[[nodiscard]] BETTERCAD_MESHING_EXPORT MeshQualityReport
evaluateMeshQuality(const Mesh& mesh, const QualityThresholds& thresholds = reportOnlyThresholds());

} // namespace bettercad::meshing
