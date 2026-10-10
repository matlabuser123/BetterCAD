// P17-VALID-001 -- the structural acceptance policy and the unbypassable entry.
//
// WHAT THESE TESTS ARE ABOUT. The policy's numbers were MEASURED before they
// were chosen (`tests/reference/StructuralValidationSweepTests.cpp` and
// `docs/verification/P17-VALID-001/`), so these cases are not where the
// thresholds are justified -- they are where the thresholds are PINNED, so a
// silent change to one fails a test, and where every refusal is shown to be
// reachable.
//
// THE ASYMMETRY THESE TESTS EXIST TO DEFEND. Two readings of this milestone are
// wrong and both are tempting:
//
//   "a bad shape score should refuse the solve"
//      No. A qualified cylinder is worse than a 1.98-degree sliver on both the
//      radius ratio and the minimum dihedral, so a shape-quality line refuses
//      BetterCAD's own output. `RejectsOnlyBelowTheMeasuredAccuracyFloor` and
//      `AcceptsASliverBecauseTheKernelHandlesIt` hold that line.
//
//   "six constrained degrees of freedom means the model is supported"
//      No, and the brief forbids the rule explicitly.
//      `DoesNotClaimSixDegreesOfFreedomIsSufficient` proves the asymmetry by
//      ACCEPTING a model that validation cannot refuse and watching the
//      factorisation refuse it.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/meshing/MeshQuality.hpp>
#include <bettercad/meshing/MeshSizing.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/structural/StructuralAnalysisObject.hpp>
#include <bettercad/structural/StructuralValidation.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <numbers>
#include <string>
#include <vector>

namespace {

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;
using meshing::QualityMetric;
using structural::StructuralValidationReport;
using structural::ValidationCode;
using structural::ValidationSeverity;
using structural::ValidationStatus;
using Stage = structural::StructuralValidationReport::Stage;

constexpr double kYoungs = 2.1e11;
constexpr double kPoisson = 0.3;

/// Every `ValidationCode`, so a test can sweep the enumeration instead of
/// listing it again and missing one.
constexpr std::array<ValidationCode, 13> kAllCodes{
    ValidationCode::AnalysisNotFound,
    ValidationCode::AnalysisDefinitionInvalid,
    ValidationCode::InputsUnavailable,
    ValidationCode::MeshStructurallyInvalid,
    ValidationCode::MeshHasInvalidElements,
    ValidationCode::QualityPolicyUnusable,
    ValidationCode::ElementAccuracyBelowFloor,
    ValidationCode::ElementOutsideQualifiedEnvelope,
    ValidationCode::MaterialUnusable,
    ValidationCode::RestraintsUnresolvable,
    ValidationCode::InsufficientRestraint,
    ValidationCode::LoadsUnresolvable,
    ValidationCode::NoLoadsApplied,
};

// ---------------------------------------------------------------------------
// Hand-built single-element meshes, for the quality path
// ---------------------------------------------------------------------------

/// A tetrahedron from four corners, with no mesher involved.
///
/// BUILT THROUGH THE PRODUCTION BUILDER, so these are real meshes and not a
/// test-only shape. It is the only way to pose an element whose radius ratio
/// is chosen: Netgen will not produce one.
[[nodiscard]] Result<meshing::Mesh> tetMesh(const std::array<Point3D, 4>& corners) {
    meshing::MeshBuilder builder;
    for (const Point3D& corner : corners) {
        REQUIRE(builder.addNode(corner).has_value());
    }
    const Result<meshing::ElementId> added = builder.addTetrahedron(
        {meshing::NodeId::fromValue(1), meshing::NodeId::fromValue(2),
         meshing::NodeId::fromValue(3), meshing::NodeId::fromValue(4)},
        meshing::RegionId::fromValue(1));
    if (!added.has_value()) {
        return makeError(added.error().code, added.error().message);
    }
    return builder.build();
}

/// P16's flattening wedge: a 10 mm equilateral base with the apex at
/// @p apexMillimetres. `3r/R` goes as the SQUARE of the apex height, measured:
/// 0.01 mm gives 8.99995e-06 and 1.0e-5 mm gives 9.0e-12.
[[nodiscard]] std::array<Point3D, 4> wedgeCorners(double apexMillimetres) {
    const double a = 0.010;
    const double h = a * std::sqrt(3.0) / 2.0;
    return {Point3D{Length::fromSi(0.0), Length::fromSi(0.0), Length::fromSi(0.0)},
            Point3D{Length::fromSi(a), Length::fromSi(0.0), Length::fromSi(0.0)},
            Point3D{Length::fromSi(a / 2.0), Length::fromSi(h), Length::fromSi(0.0)},
            Point3D{Length::fromSi(a / 2.0), Length::fromSi(h / 3.0),
                    Length::fromSi(apexMillimetres / 1000.0)}};
}

[[nodiscard]] meshing::MeshQualityReport qualityOfWedge(double apexMillimetres) {
    Result<meshing::Mesh> mesh = tetMesh(wedgeCorners(apexMillimetres));
    INFO((mesh.has_value() ? std::string{} : mesh.error().message));
    REQUIRE(mesh.has_value());
    return meshing::evaluateMeshQuality(*mesh, meshing::reportOnlyThresholds());
}

[[nodiscard]] bool carries(const StructuralValidationReport& report, ValidationCode code) {
    return std::ranges::any_of(report.findings, [code](const structural::ValidationFinding& f) {
        return f.code == code;
    });
}

[[nodiscard]] const structural::ValidationFinding* find(const StructuralValidationReport& report,
                                                        ValidationCode code) {
    const auto found =
        std::ranges::find_if(report.findings, [code](const structural::ValidationFinding& f) {
            return f.code == code;
        });
    return found == report.findings.end() ? nullptr : &*found;
}

} // namespace

// ---------------------------------------------------------------------------
// The policy itself
// ---------------------------------------------------------------------------

TEST_CASE("StructuralValidation_PolicyIsAcceptedByP16sOwnPolicyChecker", "[structural][valid]") {
    // P16 refuses a non-finite bound, a bound on a dimensioned size, and
    // bounds ordered the wrong way round. A policy that failed this would be
    // silently ignored and reported through `thresholdPolicyError` -- which is
    // the "a control that quietly does nothing" defect P16-SIZE-001's audit
    // found four times over.
    const meshing::QualityThresholds policy = structural::structuralQualityThresholds();
    const Result<void> valid = meshing::validate(policy);
    INFO((valid.has_value() ? std::string{} : valid.error().message));
    REQUIRE(valid.has_value());
}

TEST_CASE("StructuralValidation_PolicyBindsOneFailureBoundPerDegenerationFamily",
          "[structural][valid]") {
    // THE CENTRAL CLAIM OF ADR-042, as an assertion rather than a paragraph.
    //
    // TWO failure bounds, not one and not four. A tetrahedron degenerates two
    // ways and each metric is blind to the other's family, which is measured:
    // a flattening element's aspect ratio saturates at sqrt(3), and a
    // stretching element's radius ratio understates its error by 38x. An
    // earlier draft of this milestone bound only the radius ratio and accepted
    // a needle whose recovered strain was wrong by 2.5e-09.
    const meshing::QualityThresholds policy = structural::structuralQualityThresholds();

    std::vector<QualityMetric> withFailure;
    std::vector<QualityMetric> withWarning;
    for (const auto& [metric, bounds] : policy.limits) {
        if (bounds.failure.has_value()) {
            withFailure.push_back(metric);
        }
        if (bounds.warning.has_value()) {
            withWarning.push_back(metric);
        }
    }

    SECTION("exactly two failure bounds, one per family, at the derived values") {
        REQUIRE(withFailure.size() == 2);
        REQUIRE(std::ranges::find(withFailure, QualityMetric::TetRadiusRatio) !=
                withFailure.end());
        REQUIRE(std::ranges::find(withFailure, QualityMetric::TetAspectRatio) !=
                withFailure.end());
        REQUIRE(policy.limits.at(QualityMetric::TetRadiusRatio).failure.value() ==
                structural::kStructuralRadiusRatioFloor);
        REQUIRE(policy.limits.at(QualityMetric::TetAspectRatio).failure.value() ==
                structural::kStructuralAspectRatioCeiling);
        REQUIRE(structural::kStructuralRadiusRatioFloor == 1.0e-10);
        REQUIRE(structural::kStructuralAspectRatioCeiling == 3.0e5);
    }

    SECTION("and NO failure bound on either dihedral angle") {
        // DELIBERATE AND MEASURED. No accuracy law was measured against the
        // dihedral angles, and the measured distributions show they cannot
        // separate a qualified cylinder (0.362683 deg) from a deliberate
        // 1.98-degree sliver -- the two sets interleave. A bound here would
        // refuse BetterCAD's own output, and a bound with no law behind it is
        // the invented number this milestone's evidence exists to avoid.
        for (const QualityMetric metric :
             {QualityMetric::TetMinDihedralAngle, QualityMetric::TetMaxDihedralAngle}) {
            INFO("metric " << meshing::toString(metric));
            REQUIRE_FALSE(policy.limits.at(metric).failure.has_value());
        }
    }

    SECTION("and each failure bound is many orders clear of every qualified mesh") {
        // Neither bound may come near what BetterCAD produces. The worst
        // qualified element is 3r/R 3.22641e-04 and aspect 80.0062.
        REQUIRE(3.22641e-04 > structural::kStructuralRadiusRatioFloor * 1.0e5);
        REQUIRE(80.0062 < structural::kStructuralAspectRatioCeiling / 1.0e3);
    }

    SECTION("four warning bounds, and they are the measured qualified envelope") {
        REQUIRE(withWarning.size() == 4);
        // PINNED TO THE MEASUREMENT. Each is the worst value a qualified P16
        // reference model exhibits, rounded away from the qualified set.
        // Changing one without re-measuring fails here.
        REQUIRE(policy.limits.at(QualityMetric::TetRadiusRatio).warning.value() == 3.0e-4);
        REQUIRE(policy.limits.at(QualityMetric::TetMinDihedralAngle).warning.value() == 6.0e-3);
        REQUIRE(policy.limits.at(QualityMetric::TetMaxDihedralAngle).warning.value() == 3.125);
        REQUIRE(policy.limits.at(QualityMetric::TetAspectRatio).warning.value() == 81.0);
    }

    SECTION("the dihedral bounds are RADIANS, which the magnitudes prove") {
        // A policy written in degrees would be a unit error P16's type cannot
        // catch: 6.0 would be read as 6 radians, past pi, and would warn on
        // every element. The minimum bound is well under one radian and the
        // maximum is under pi, which only radians can be.
        REQUIRE(policy.limits.at(QualityMetric::TetMinDihedralAngle).warning.value() < 1.0);
        REQUIRE(policy.limits.at(QualityMetric::TetMaxDihedralAngle).warning.value() <
                std::numbers::pi);
    }

    SECTION("and no bound on a dimensioned size") {
        // P16 refuses one, because "is this volume good?" has no scale-free
        // answer. Asserted here too, because the refusal is what keeps a
        // threshold from silently becoming a threshold on the model's units.
        for (const auto& [metric, bounds] : policy.limits) {
            INFO("metric " << meshing::toString(metric));
            REQUIRE(meshing::direction(metric) != meshing::QualityDirection::ContextOnly);
        }
    }
}

TEST_CASE("StructuralValidation_TheBoundsGuaranteeTheStatedAccuracy", "[structural][valid]") {
    // THE DERIVATIONS, AS ARITHMETIC. Each bound is the inverse of a MEASURED
    // law at the required accuracy, and recomputing them here means the
    // constants and the thresholds cannot drift apart silently.
    SECTION("the flattening family, bounded by the radius ratio") {
        // `err <= C / sqrt(3r/R)`, C <= 2.41122e-15 measured over sixteen
        // orders of radius ratio and two fields.
        constexpr double kWorstFlatteningConstant = 2.41122e-15;
        const double guaranteed =
            kWorstFlatteningConstant / std::sqrt(structural::kStructuralRadiusRatioFloor);
        INFO("guaranteed at the floor: " << guaranteed << ", required "
                                         << structural::kStructuralStrainAccuracy);
        REQUIRE(guaranteed < structural::kStructuralStrainAccuracy);
        // AND WITH REAL MARGIN, not by a hair. 4.1x measured.
        REQUIRE(guaranteed * 4.0 < structural::kStructuralStrainAccuracy);
    }

    SECTION("the stretching family, bounded by the aspect ratio") {
        // `err <= C * aspect`, C <= 4.45e-16 measured over seven orders of
        // aspect ratio. A DIFFERENT law for a DIFFERENT degeneration, which is
        // why two bounds exist.
        constexpr double kWorstStretchingConstant = 4.45e-16;
        const double guaranteed =
            kWorstStretchingConstant * structural::kStructuralAspectRatioCeiling;
        INFO("guaranteed at the ceiling: " << guaranteed << ", required "
                                           << structural::kStructuralStrainAccuracy);
        REQUIRE(guaranteed < structural::kStructuralStrainAccuracy);
        REQUIRE(guaranteed * 4.0 < structural::kStructuralStrainAccuracy);
    }

    SECTION("and the required accuracy is BetterCAD's documented geometric band") {
        REQUIRE(structural::kStructuralStrainAccuracy == 1.0e-9);
    }
}

// ---------------------------------------------------------------------------
// The report's own mechanics
// ---------------------------------------------------------------------------

TEST_CASE("StructuralValidation_SeverityIsAFunctionOfTheCodeAndExactlyTwoAreWarnings",
          "[structural][valid]") {
    std::size_t warnings = 0;
    for (const ValidationCode code : kAllCodes) {
        INFO("code " << structural::toString(code));
        if (structural::severityOf(code) == ValidationSeverity::Warning) {
            ++warnings;
        }
    }
    REQUIRE(warnings == 3);
    REQUIRE(structural::severityOf(ValidationCode::ElementOutsideQualifiedEnvelope) ==
            ValidationSeverity::Warning);
    REQUIRE(structural::severityOf(ValidationCode::NoLoadsApplied) ==
            ValidationSeverity::Warning);
    // A WARNING BY A DECISION THAT WAS REVERSED. P16 measures every metric
    // even when the policy it was handed is contradictory, so refusing here
    // would refuse a sound mesh over an unrelated error in the caller's
    // reporting preferences -- and a mesh control really does carry its own
    // thresholds, so the case is reachable from a document.
    REQUIRE(structural::severityOf(ValidationCode::QualityPolicyUnusable) ==
            ValidationSeverity::Warning);
    // THE ONE THAT MATTERS MOST: the quality hard failure is a FAILURE. A
    // mutation making it a warning would let a mesh past the accuracy floor
    // into the solver, and that is the defect this milestone exists to close.
    REQUIRE(structural::severityOf(ValidationCode::ElementAccuracyBelowFloor) ==
            ValidationSeverity::Failure);
}

TEST_CASE("StructuralValidation_EveryCodeStageAndStatusHasItsOwnName", "[structural][valid]") {
    // A diagnostic that says "unknown" is a diagnostic nobody can act on, and
    // a duplicated name makes two different problems indistinguishable in a
    // log.
    std::vector<std::string> names;
    for (const ValidationCode code : kAllCodes) {
        const std::string name{structural::toString(code)};
        INFO("code name " << name);
        REQUIRE(name != "unknown");
        REQUIRE_FALSE(name.empty());
        names.push_back(name);
    }
    std::ranges::sort(names);
    REQUIRE(std::ranges::adjacent_find(names) == names.end());

    for (const Stage stage : {Stage::Analysis, Stage::Inputs, Stage::Quality, Stage::Material,
                              Stage::Restraints, Stage::Loads, Stage::Complete}) {
        REQUIRE(structural::toString(stage) != "unknown");
    }
    for (const ValidationStatus status : {ValidationStatus::Accepted,
                                          ValidationStatus::AcceptedWithWarnings,
                                          ValidationStatus::Rejected}) {
        REQUIRE(structural::toString(status) != "unknown");
    }
    for (const ValidationSeverity severity :
         {ValidationSeverity::Warning, ValidationSeverity::Failure}) {
        REQUIRE(structural::toString(severity) != "unknown");
    }
}

TEST_CASE("StructuralValidation_StatusIsDerivedSoItCannotDisagreeWithTheFindings",
          "[structural][valid]") {
    StructuralValidationReport report{};

    SECTION("no findings is Accepted") {
        REQUIRE(report.status() == ValidationStatus::Accepted);
        REQUIRE(report.acceptable());
    }

    SECTION("warnings alone are AcceptedWithWarnings, and still acceptable") {
        report.findings.push_back(
            structural::ValidationFinding{.code = ValidationCode::NoLoadsApplied,
                                          .severity = ValidationSeverity::Warning,
                                          .message = "no load"});
        REQUIRE(report.status() == ValidationStatus::AcceptedWithWarnings);
        REQUIRE(report.acceptable());
    }

    SECTION("one failure anywhere in the list is Rejected") {
        // ANYWHERE, including last. A status computed from only the first
        // finding would pass this report, which is why the loop scans all of
        // them rather than trusting the ordering.
        report.findings.push_back(
            structural::ValidationFinding{.code = ValidationCode::NoLoadsApplied,
                                          .severity = ValidationSeverity::Warning,
                                          .message = "no load"});
        report.findings.push_back(
            structural::ValidationFinding{.code = ValidationCode::InsufficientRestraint,
                                          .severity = ValidationSeverity::Failure,
                                          .message = "unsupported"});
        REQUIRE(report.status() == ValidationStatus::Rejected);
        REQUIRE_FALSE(report.acceptable());
    }
}

// ---------------------------------------------------------------------------
// The quality path
// ---------------------------------------------------------------------------

TEST_CASE("StructuralValidation_RejectsOnlyBelowTheMeasuredAccuracyFloor", "[structural][valid]") {
    // THE HARD FAILURE, AT THE BOUNDARY, MEASURED ON BOTH SIDES. `3r/R` goes
    // as the square of the apex height, so these two wedges straddle 1e-10.
    SECTION("an element above the floor is accepted") {
        const meshing::MeshQualityReport quality = qualityOfWedge(1.0e-4);
        const double worst = quality.summaries.at(QualityMetric::TetRadiusRatio).minimum;
        INFO("3r/R " << worst);
        // The premise, asserted so the test cannot pass for the wrong reason.
        REQUIRE(worst > structural::kStructuralRadiusRatioFloor);
        const StructuralValidationReport report =
            structural::validateStructuralMeshQuality(quality);
        REQUIRE_FALSE(carries(report, ValidationCode::ElementAccuracyBelowFloor));
        REQUIRE(report.acceptable());
    }

    SECTION("an element below the floor is refused, and the finding carries the numbers") {
        const meshing::MeshQualityReport quality = qualityOfWedge(1.0e-5);
        const double worst = quality.summaries.at(QualityMetric::TetRadiusRatio).minimum;
        INFO("3r/R " << worst);
        REQUIRE(worst < structural::kStructuralRadiusRatioFloor);
        const StructuralValidationReport report =
            structural::validateStructuralMeshQuality(quality);
        REQUIRE(report.status() == ValidationStatus::Rejected);
        REQUIRE_FALSE(report.acceptable());

        const structural::ValidationFinding* refusal =
            find(report, ValidationCode::ElementAccuracyBelowFloor);
        REQUIRE(refusal != nullptr);
        REQUIRE(refusal->severity == ValidationSeverity::Failure);
        REQUIRE(refusal->metric == QualityMetric::TetRadiusRatio);
        REQUIRE(refusal->element == meshing::ElementId::fromValue(1));
        REQUIRE(refusal->value.has_value());
        REQUIRE(refusal->value.value() == worst);
        REQUIRE(refusal->threshold == structural::kStructuralRadiusRatioFloor);
        // THE PAYLOAD IS THE FIELDS, and the message is for a log -- but the
        // message must not be silent about the one thing a reader needs, which
        // is that this is an accuracy floor and not a shape opinion.
        REQUIRE_THAT(refusal->message, ContainsSubstring("accuracy floor"));
    }

    SECTION("and the refusal is NOT also reported as a warning") {
        // An element past the failure bound is also past the warning bound.
        // Reporting both would be reporting one problem twice, and a GUI
        // counting findings would say two elements are wrong.
        const StructuralValidationReport report =
            structural::validateStructuralMeshQuality(qualityOfWedge(1.0e-5));
        const std::size_t radiusFindings = static_cast<std::size_t>(
            std::ranges::count_if(report.findings, [](const structural::ValidationFinding& f) {
                return f.metric == QualityMetric::TetRadiusRatio;
            }));
        REQUIRE(radiusFindings == 1);
    }
}

TEST_CASE("StructuralValidation_AcceptsASliverBecauseTheKernelHandlesIt", "[structural][valid]") {
    // THE BRIEF'S REQUIREMENT, AS A TEST: "do not turn every imperfect mesh
    // into a hard failure". This element is a 0.198-degree sliver -- the worst
    // shape in the whole measured sweep -- and it is ACCEPTED, because the
    // measured recovered-strain error on it is 1.33e-13.
    const meshing::MeshQualityReport quality = qualityOfWedge(0.01);
    const StructuralValidationReport report = structural::validateStructuralMeshQuality(quality);

    const double minDihedral = quality.summaries.at(QualityMetric::TetMinDihedralAngle).minimum;
    const double aspect = quality.summaries.at(QualityMetric::TetAspectRatio).maximum;
    INFO("minDihedral " << minDihedral * 180.0 / std::numbers::pi << " deg, aspect " << aspect);

    SECTION("and it passes BOTH accuracy bounds, which is why it is accepted") {
        // The sliver is the flattening family at its worst measurable shape,
        // and BOTH bounds let it through: 8.99995e-06 is above the radius
        // floor and 1.73205 is far below the aspect ceiling. Stated as an
        // assertion because "accepted" alone would not say which bounds were
        // consulted.
        const double radius = quality.summaries.at(QualityMetric::TetRadiusRatio).minimum;
        REQUIRE(radius > structural::kStructuralRadiusRatioFloor);
        REQUIRE(aspect < structural::kStructuralAspectRatioCeiling);
    }

    SECTION("it really is a sliver, and the aspect ratio really cannot see it") {
        // Both premises asserted. Without them this test would pass on any
        // element at all, and the second is the whole reason a second shape
        // metric is carried.
        REQUIRE(minDihedral * 180.0 / std::numbers::pi < 0.5);
        REQUIRE(aspect < std::sqrt(3.0) + 1e-9);
    }

    SECTION("it is accepted, with a warning and not a refusal") {
        REQUIRE(report.acceptable());
        REQUIRE(report.status() == ValidationStatus::AcceptedWithWarnings);
        REQUIRE_FALSE(carries(report, ValidationCode::ElementAccuracyBelowFloor));
        REQUIRE(carries(report, ValidationCode::ElementOutsideQualifiedEnvelope));
    }

    SECTION("and the warning says what it means, which is an envelope") {
        const structural::ValidationFinding* warned =
            find(report, ValidationCode::ElementOutsideQualifiedEnvelope);
        REQUIRE(warned != nullptr);
        REQUIRE(warned->severity == ValidationSeverity::Warning);
        REQUIRE_THAT(warned->message, ContainsSubstring("qualified"));
        REQUIRE_THAT(warned->message, ContainsSubstring("not a rejection"));
    }
}

TEST_CASE("StructuralValidation_RefusesAMeshThatIsNotAMesh", "[structural][valid]") {
    // STRUCTURAL INVALIDITY IS NOT A QUALITY SCORE. An inverted element is not
    // a badly shaped tetrahedron, it is not a tetrahedron, and no threshold
    // can produce the classification -- which is why this refusal exists
    // beside the accuracy floor rather than instead of it.
    //
    // Reachable only through this entry point: `requireStructuralModel`
    // guarantees a HELD mesh is structurally valid, because `Mesher::generate`
    // refuses one that is not. The function is public and must not let the
    // library become the validator.
    SECTION("an inverted element") {
        // The regular wedge with two nodes exchanged, which negates the signed
        // volume. NO abs() ANYWHERE: the sign is what distinguishes an
        // inverted element from a correct one.
        std::array<Point3D, 4> corners = wedgeCorners(5.0);
        std::swap(corners[1], corners[2]);
        Result<meshing::Mesh> mesh = tetMesh(corners);
        if (!mesh.has_value()) {
            // The builder may refuse it outright, which is a stronger refusal
            // than the report's and equally correct.
            REQUIRE_THAT(mesh.error().message, ContainsSubstring("volume"));
            return;
        }
        const meshing::MeshQualityReport quality =
            meshing::evaluateMeshQuality(*mesh, meshing::reportOnlyThresholds());
        REQUIRE_FALSE(quality.structurallyValid);
        const StructuralValidationReport report =
            structural::validateStructuralMeshQuality(quality);
        REQUIRE(report.status() == ValidationStatus::Rejected);
        REQUIRE(carries(report, ValidationCode::MeshStructurallyInvalid));
    }

    SECTION("and the quality metrics are not consulted when the mesh is invalid") {
        // SHORT-CIRCUIT. Metrics of something that is not a tetrahedron are
        // numbers inviting comparison against a threshold, and P16 does not
        // compute them for an invalid element anyway. So the report must not
        // carry an accuracy refusal alongside the structural one -- the cause
        // would be buried under a consequence.
        std::array<Point3D, 4> corners = wedgeCorners(5.0);
        std::swap(corners[1], corners[2]);
        Result<meshing::Mesh> mesh = tetMesh(corners);
        if (!mesh.has_value()) {
            return;
        }
        const StructuralValidationReport report = structural::validateStructuralMeshQuality(
            meshing::evaluateMeshQuality(*mesh, meshing::reportOnlyThresholds()));
        REQUIRE_FALSE(carries(report, ValidationCode::ElementAccuracyBelowFloor));
        REQUIRE_FALSE(carries(report, ValidationCode::ElementOutsideQualifiedEnvelope));
        REQUIRE(report.worstByMetric.empty());
    }
}

TEST_CASE("StructuralValidation_WarnsAboutAContradictoryPolicyWithoutRefusing",
          "[structural][valid]") {
    // THE REVERSED DECISION, HELD BY A TEST.
    //
    // An earlier version of this module REFUSED the solve when the report it
    // was handed had been produced under a self-contradictory policy. That was
    // a false rejection: P16 still measures every metric in that case and
    // reports it as under the report-only default, so the summaries this
    // module reads are present and valid, and its own policy is fixed and
    // validated. And the case is reachable from an ordinary document, because
    // a `MeshControl` carries its own `QualityThresholds` and the mesher
    // evaluates the held report under them -- so the old behaviour would have
    // blocked a structurally sound solve over an unrelated error in the
    // user's reporting preferences.
    meshing::QualityThresholds contradictory{};
    // A bound on a dimensioned size: P16 refuses it, because "is this volume
    // good?" has no scale-free answer.
    contradictory.limits[QualityMetric::TetVolume] =
        meshing::QualityThreshold{.warning = 1.0, .failure = std::nullopt};
    REQUIRE_FALSE(meshing::validate(contradictory).has_value());

    Result<meshing::Mesh> mesh = tetMesh(wedgeCorners(5.0));
    REQUIRE(mesh.has_value());
    const meshing::MeshQualityReport quality = meshing::evaluateMeshQuality(*mesh, contradictory);
    REQUIRE(quality.thresholdPolicyError.has_value());
    REQUIRE_FALSE(quality.satisfiesPolicy());

    const StructuralValidationReport report = structural::validateStructuralMeshQuality(quality);

    SECTION("the policy error is surfaced") {
        REQUIRE(carries(report, ValidationCode::QualityPolicyUnusable));
        REQUIRE(report.status() == ValidationStatus::AcceptedWithWarnings);
        REQUIRE(report.acceptable());
    }

    SECTION("and the metrics are still read, which is why refusing was wrong") {
        // THE WHOLE POINT. The finding must not short-circuit the summary
        // read: P17's verdict comes from the measured metrics, and they are
        // all there.
        REQUIRE_FALSE(report.worstByMetric.empty());
        for (const structural::MetricObservation& measured : report.worstByMetric) {
            INFO("metric " << meshing::toString(measured.metric));
            REQUIRE(std::isfinite(measured.value));
        }
    }

    SECTION("and a genuinely bad element is still refused under the same report") {
        // The warning must not become an excuse. A report produced under a
        // broken policy whose METRICS show an element past the accuracy floor
        // is still refused -- the two findings are independent.
        Result<meshing::Mesh> bad = tetMesh(wedgeCorners(1.0e-5));
        REQUIRE(bad.has_value());
        const meshing::MeshQualityReport badQuality =
            meshing::evaluateMeshQuality(*bad, contradictory);
        REQUIRE(badQuality.thresholdPolicyError.has_value());
        const StructuralValidationReport badReport =
            structural::validateStructuralMeshQuality(badQuality);
        REQUIRE(badReport.status() == ValidationStatus::Rejected);
        REQUIRE(carries(badReport, ValidationCode::ElementAccuracyBelowFloor));
        REQUIRE(carries(badReport, ValidationCode::QualityPolicyUnusable));
    }
}

TEST_CASE("StructuralValidation_AgreesWithP16sOwnPerElementClassification",
          "[structural][valid]") {
    // TWO CODE PATHS, ONE ANSWER, ASSERTED RATHER THAN ASSUMED.
    //
    // This module classifies by comparing a SUMMARY EXTREMUM against a bound.
    // P16 classifies each ELEMENT against the same bound. Both are strict and
    // both use `direction(metric)`, so they must agree -- and the place a
    // disagreement would appear is the boundary, where one is strict and the
    // other is not.
    for (const double apex : {10.0, 1.0, 0.1, 0.01, 1.0e-4, 1.0e-5}) {
        Result<meshing::Mesh> mesh = tetMesh(wedgeCorners(apex));
        REQUIRE(mesh.has_value());

        // P16's verdict, under THIS module's thresholds.
        const meshing::MeshQualityReport classified =
            meshing::evaluateMeshQuality(*mesh, structural::structuralQualityThresholds());
        // This module's verdict, from the report-only summaries.
        const StructuralValidationReport mine = structural::validateStructuralMeshQuality(
            meshing::evaluateMeshQuality(*mesh, meshing::reportOnlyThresholds()));

        INFO("apex " << apex << " mm, P16 failureElements " << classified.failureElements
                     << ", warningElements " << classified.warningElements << ", mine "
                     << structural::toString(mine.status()));

        // A FAILING ELEMENT ON ONE SIDE IS A REFUSAL ON THE OTHER.
        REQUIRE((classified.failureElements > 0) ==
                carries(mine, ValidationCode::ElementAccuracyBelowFloor));
        // AND THE METRICS THEMSELVES ARE IDENTICAL UNDER BOTH POLICIES, which
        // is P16's own guarantee -- a policy decides how a number is
        // classified and can never change the number.
        const meshing::MeshQualityReport reportOnly =
            meshing::evaluateMeshQuality(*mesh, meshing::reportOnlyThresholds());
        REQUIRE(classified.tets == reportOnly.tets);
    }
}

TEST_CASE("StructuralValidation_RecordsTheWorstOfEveryMetricWhetherOrNotItPassed",
          "[structural][valid]") {
    // A report that only says what went wrong cannot be compared against the
    // next one, and the measured worst value is the thing a user navigates to.
    const StructuralValidationReport report =
        structural::validateStructuralMeshQuality(qualityOfWedge(10.0));
    REQUIRE(report.acceptable());
    REQUIRE(report.status() == ValidationStatus::Accepted);
    REQUIRE(report.worstByMetric.size() == 4);
    std::vector<QualityMetric> seen;
    for (const structural::MetricObservation& measured : report.worstByMetric) {
        INFO("metric " << meshing::toString(measured.metric));
        REQUIRE(measured.element.isValid());
        REQUIRE(std::isfinite(measured.value));
        seen.push_back(measured.metric);
    }

    // A MEASUREMENT CARRIES NO VERDICT. `MetricObservation` has no code and no
    // severity at all, which is what stops a consumer reading the payload from
    // being told a metric warned when it did not -- the defect adversarial
    // review found in an earlier draft of this milestone.
    static_assert(sizeof(structural::MetricObservation) <
                  sizeof(structural::ValidationFinding));

    // EXACTLY THE FOUR METRICS THE POLICY BINDS, each once.
    std::ranges::sort(seen);
    REQUIRE(std::ranges::adjacent_find(seen) == seen.end());
    for (const QualityMetric metric : seen) {
        REQUIRE(structural::structuralQualityThresholds().limits.contains(metric));
    }
}

TEST_CASE("StructuralValidation_OrdersFindingsDeterministically", "[structural][valid]") {
    // A report whose order moves cannot be diffed. Refusals first, then by
    // code, then by element.
    const meshing::MeshQualityReport quality = qualityOfWedge(1.0e-5);
    const StructuralValidationReport first = structural::validateStructuralMeshQuality(quality);
    const StructuralValidationReport again = structural::validateStructuralMeshQuality(quality);
    REQUIRE(first == again);

    for (std::size_t i = 1; i < first.findings.size(); ++i) {
        const structural::ValidationFinding& a = first.findings[i - 1];
        const structural::ValidationFinding& b = first.findings[i];
        INFO("finding " << i << ": " << structural::toString(a.code) << " then "
                        << structural::toString(b.code));
        if (a.severity != b.severity) {
            REQUIRE(a.severity == ValidationSeverity::Failure);
        } else {
            REQUIRE(a.code <= b.code);
        }
    }
}

// ---------------------------------------------------------------------------
// The analysis path, and the solve
// ---------------------------------------------------------------------------

namespace {

/// A block with a material, a mesh and an analysis whose definition carries the
/// loads and restraints -- which is what distinguishes this fixture from the
/// earlier milestones'. `solveStructuralAnalysis` reads them from the DOCUMENT,
/// so a fixture that passed them as function arguments could not exercise it.
struct ValidatablePart {
    Document document{"Part"};
    features::Regenerator regenerator;
    meshing::Mesher mesher;
    ObjectId feature{};
    MeshControlId control{};
    AnalysisId analysis{};
    MaterialId material{};
    std::array<EntityId, 4> lines{};

    explicit ValidatablePart(bool withDensity = true) {
        auto profile = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        lines = addRectangle(*profile, 0_mm, 0_mm, 40_mm, 30_mm);
        const ObjectId sketchId = require(document.addObject(std::move(profile)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(sketchId.value()), .depth = 20_mm});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));

        auto intent = meshing::MeshControl::create(
            "Mesh",
            meshing::MeshControlDefinition{
                .body = feature,
                .mesh = {.sizing = {.globalTargetSize = 20_mm,
                                    .local = {meshing::LocalMeshSizing{
                                        .face = FaceName{feature,
                                                         FaceSelector{.role = FaceRole::Side,
                                                                      .entity = lines.at(0)}},
                                        .targetSize = 6_mm}}}}});
        REQUIRE(intent.has_value());
        control = MeshControlId::fromValue(require(document.addObject(std::move(*intent))).value());

        features::MaterialDefinition definition;
        definition.designation = "Steel";
        definition.mechanical.youngsModulus =
            materials::MaterialProperty<ElasticModulus>::known(ElasticModulus::fromSi(kYoungs));
        definition.mechanical.poissonRatio =
            materials::MaterialProperty<bettercad::PoissonRatio>::known(
                PoissonRatio::of(kPoisson));
        if (withDensity) {
            definition.mechanical.density =
                materials::MaterialProperty<Density>::known(Density::fromSi(7850.0));
        }
        const Result<MaterialId> id = features::createMaterial(document, "Steel", definition);
        REQUIRE(id.has_value());
        material = *id;
        REQUIRE(features::assignMaterial(document, material).has_value());

        auto study = structural::StructuralAnalysis::create(
            "Study", structural::StructuralAnalysisDefinition{.mesh = control});
        REQUIRE(study.has_value());
        analysis = AnalysisId::fromValue(require(document.addObject(std::move(*study))).value());

        requireReport(regenerator, document);
        remesh();
    }

    const meshing::VolumeMesh& remesh() {
        const Result<const meshing::VolumeMesh*> generated =
            mesher.generate(document, regenerator, control);
        INFO((generated.has_value() ? std::string{} : generated.error().message));
        REQUIRE(generated.has_value());
        return **generated;
    }

    [[nodiscard]] FaceName startCap() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::StartCap}};
    }
    [[nodiscard]] FaceName endCap() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::EndCap}};
    }
    /// A face name that resolves to nothing: a side role naming an entity the
    /// sketch does not contain.
    [[nodiscard]] FaceName missingFace() const {
        return FaceName{feature,
                        FaceSelector{.role = FaceRole::Side, .entity = EntityId::fromValue(99991)}};
    }

    void setAnalysis(std::vector<structural::StructuralLoad> loads,
                     std::vector<structural::StructuralRestraint> restraints,
                     structural::StructuralAnalysisMode mode =
                         structural::StructuralAnalysisMode::LinearStatic) {
        REQUIRE(document
                    .modifyObject<structural::StructuralAnalysis>(
                        ObjectId::fromValue(analysis.value()),
                        [&](structural::StructuralAnalysis& object) -> Result<bool> {
                            structural::StructuralAnalysisDefinition definition =
                                object.definition();
                            definition.loads = std::move(loads);
                            definition.restraints = std::move(restraints);
                            definition.mode = mode;
                            return object.setDefinition(std::move(definition));
                        })
                    .has_value());
    }

    [[nodiscard]] StructuralValidationReport validate() const {
        return structural::validateStructuralAnalysisForSolve(document, regenerator, mesher,
                                                              analysis);
    }

    [[nodiscard]] Result<structural::StructuralSolveOutcome>
    solve(const structural::SolverSettings& settings = {},
          const structural::EquilibriumTolerance& tolerance = {}) const {
        return structural::solveStructuralAnalysis(document, regenerator, mesher, analysis,
                                                   settings, Point3D{}, tolerance);
    }

    /// Every node of the end cap pushed along +Z. A fixed start cap resists it.
    [[nodiscard]] std::vector<structural::StructuralLoad> endLoad(double fz) const {
        Result<structural::StructuralModel> model =
            structural::requireStructuralModel(document, regenerator, mesher, control);
        REQUIRE(model.has_value());
        Result<meshing::BoundaryFacetSet> facets =
            meshing::boundaryFacetsOf(model->map(), endCap());
        REQUIRE(facets.has_value());
        Result<std::vector<meshing::NodeId>> nodes =
            meshing::boundaryNodesOf(model->map(), model->mesh().mesh(), facets->facets);
        REQUIRE(nodes.has_value());
        REQUIRE_FALSE(nodes->empty());
        std::vector<structural::StructuralLoad> loads;
        std::uint64_t id = 1;
        for (const meshing::NodeId node : *nodes) {
            loads.push_back(structural::StructuralLoad{
                LoadId::fromValue(id++),
                structural::NodalForceLoad{
                    .mesh = model->mesh().mesh().stamp(),
                    .node = node,
                    .force = Force3D{Force::fromSi(0.0), Force::fromSi(0.0),
                                     Force::fromSi(fz / static_cast<double>(nodes->size()))}}});
        }
        return loads;
    }

    [[nodiscard]] std::vector<structural::StructuralRestraint> fixedStart() const {
        return {structural::StructuralRestraint::fixedSupport(RestraintId::fromValue(1),
                                                              startCap())};
    }

    /// Z only, on the whole start cap. MEASURED AT FOUR constrained degrees of
    /// freedom, because this cap triangulates into two triangles over four
    /// nodes -- which makes it the fixture for the non-zero half of the
    /// `< 6` refusal, and useless for the asymmetry test.
    [[nodiscard]] std::vector<structural::StructuralRestraint> zOnlyStart() const {
        return {structural::StructuralRestraint{
            RestraintId::fromValue(1), startCap(),
            structural::RestraintComponents::along(structural::DofComponent::Uz)}};
    }

    /// Z only, on both caps AND the refined side face. MORE than six
    /// constrained degrees of freedom, and still singular: no x or y degree of
    /// freedom is constrained ANYWHERE, so x-translation, y-translation and
    /// rotation about z survive however many nodes are held. That is the
    /// fixture the asymmetry test needs, and the reason it holds three faces
    /// rather than one is that one cap only resolves to four.
    [[nodiscard]] std::vector<structural::StructuralRestraint> zOnlyEverywhere() const {
        const structural::RestraintComponents z =
            structural::RestraintComponents::along(structural::DofComponent::Uz);
        return {structural::StructuralRestraint{RestraintId::fromValue(1), startCap(), z},
                structural::StructuralRestraint{RestraintId::fromValue(2), endCap(), z},
                structural::StructuralRestraint{
                    RestraintId::fromValue(3),
                    FaceName{feature, FaceSelector{.role = FaceRole::Side, .entity = lines.at(0)}},
                    z}};
    }
};

} // namespace

TEST_CASE("StructuralValidation_RefusesAnAnalysisTheDocumentDoesNotHave", "[structural][valid]") {
    const ValidatablePart part;
    const StructuralValidationReport report = structural::validateStructuralAnalysisForSolve(
        part.document, part.regenerator, part.mesher, AnalysisId::fromValue(987654));
    REQUIRE(report.status() == ValidationStatus::Rejected);
    REQUIRE(carries(report, ValidationCode::AnalysisNotFound));
    REQUIRE(report.reachedStage == Stage::Analysis);
}

TEST_CASE("StructuralValidation_AcceptsAWellPosedAnalysis", "[structural][valid]") {
    ValidatablePart part;
    part.setAnalysis(part.endLoad(1000.0), part.fixedStart());
    const StructuralValidationReport report = part.validate();
    INFO((report.findings.empty() ? std::string{"no findings"} : report.findings.front().message));
    REQUIRE(report.acceptable());
    REQUIRE(report.reachedStage == Stage::Complete);

    SECTION("and it measured what it checked, rather than reporting a bare verdict") {
        REQUIRE(report.nodeCount > 0);
        REQUIRE(report.tetCount > 0);
        REQUIRE(report.constrainedDegreesOfFreedom >= structural::kMinimumConstrainedDofs);
        REQUIRE(report.loadedNodes > 0);
        REQUIRE(report.worstByMetric.size() == 4);
    }

    SECTION("and it is deterministic") {
        REQUIRE(part.validate() == report);
    }
}

TEST_CASE("StructuralValidation_RefusesAStaleMeshThroughThePublicEntry", "[structural][valid]") {
    // THE GAP P16 RECORDED AND COULD NOT CLOSE: "nothing FORCES the holder of a
    // mesh to ask whether it is stale". `Mesher::mesh()` hands back a stale
    // mesh deliberately, because P16-VIZ inspects them. This is the somebody
    // that asks, reached through the entry point a caller actually uses.
    ValidatablePart part;
    part.setAnalysis(part.endLoad(1000.0), part.fixedStart());
    REQUIRE(part.validate().acceptable());

    // The body changes and is regenerated. The mesh is NOT rebuilt.
    REQUIRE(part.document
                .modifyObject<features::ExtrudeFeature>(
                    part.feature,
                    [](features::ExtrudeFeature& extrude) -> Result<bool> {
                        auto definition = extrude.definition();
                        definition.depth = 35_mm;
                        return extrude.setDefinition(definition).has_value();
                    })
                .has_value());
    requireReport(part.regenerator, part.document);

    SECTION("the mesh is still held, still internally valid, and still handed out") {
        // The premise. Without it this test would prove nothing: the refusal
        // has to be about CURRENCY and not about a missing mesh.
        REQUIRE(part.mesher.mesh(part.control) != nullptr);
        REQUIRE(part.mesher.currency(part.document, part.control) ==
                meshing::MeshCurrency::StaleGeometry);
    }

    SECTION("and validation refuses it, naming staleness") {
        const StructuralValidationReport report = part.validate();
        REQUIRE(report.status() == ValidationStatus::Rejected);
        REQUIRE(carries(report, ValidationCode::InputsUnavailable));
        REQUIRE(report.inputProblem == structural::InputProblem::MeshStale);
        REQUIRE(report.reachedStage == Stage::Inputs);
    }

    SECTION("and the solve entry publishes nothing at all") {
        const Result<structural::StructuralSolveOutcome> outcome = part.solve();
        REQUIRE_FALSE(outcome.has_value());
        REQUIRE(outcome.error().code == ErrorCode::FailedPrecondition);
        REQUIRE_THAT(outcome.error().message, ContainsSubstring("not acceptable"));
    }
}

TEST_CASE("StructuralValidation_RefusesAMeshWhoseSizingIntentMoved", "[structural][valid]") {
    // The other half of staleness: the shape is unchanged, the discretisation
    // request is not.
    ValidatablePart part;
    part.setAnalysis(part.endLoad(1000.0), part.fixedStart());
    REQUIRE(part.validate().acceptable());

    REQUIRE(part.document
                .modifyObject<meshing::MeshControl>(
                    ObjectId::fromValue(part.control.value()),
                    [](meshing::MeshControl& intent) -> Result<bool> {
                        auto definition = intent.definition();
                        definition.mesh.sizing.globalTargetSize = 5_mm;
                        return intent.setDefinition(definition).has_value();
                    })
                .has_value());

    REQUIRE(part.mesher.currency(part.document, part.control) ==
            meshing::MeshCurrency::StaleIntent);
    const StructuralValidationReport report = part.validate();
    REQUIRE(report.status() == ValidationStatus::Rejected);
    REQUIRE(report.inputProblem == structural::InputProblem::MeshStale);
}

TEST_CASE("StructuralValidation_RefusesAnUnsupportedModel", "[structural][valid]") {
    ValidatablePart part;

    SECTION("no restraint at all") {
        part.setAnalysis(part.endLoad(1000.0), {});
        const StructuralValidationReport report = part.validate();
        REQUIRE(report.status() == ValidationStatus::Rejected);
        REQUIRE(carries(report, ValidationCode::InsufficientRestraint));
        REQUIRE(report.constrainedDegreesOfFreedom == 0);
        REQUIRE(report.reachedStage == Stage::Restraints);
    }

    SECTION("a restraint that holds only one component, and so only four degrees of freedom") {
        // THE NON-ZERO HALF OF THE `< 6` RULE, and it is not decoration: with
        // only the empty-restraint case tested, a mutation narrowing the bound
        // to `< 1` would survive. The start cap resolves to FOUR nodes, so one
        // component on it is four constrained degrees of freedom -- fewer than
        // the six a three-dimensional body's rigid-body modes require.
        part.setAnalysis(part.endLoad(1000.0), part.zOnlyStart());
        const StructuralValidationReport report = part.validate();
        INFO("constrained DOFs " << report.constrainedDegreesOfFreedom);
        REQUIRE(report.constrainedDegreesOfFreedom > 0);
        REQUIRE(report.constrainedDegreesOfFreedom < structural::kMinimumConstrainedDofs);
        REQUIRE(report.status() == ValidationStatus::Rejected);
        REQUIRE(carries(report, ValidationCode::InsufficientRestraint));
    }

    SECTION("and the diagnostic refuses to claim the converse") {
        // THE BRIEF FORBIDS the rule `constrained >= 6 -> sufficient`, and a
        // reader who skims the refusal will supply it. So the message says so.
        part.setAnalysis(part.endLoad(1000.0), {});
        const StructuralValidationReport report = part.validate();
        const structural::ValidationFinding* refusal =
            find(report, ValidationCode::InsufficientRestraint);
        REQUIRE(refusal != nullptr);
        REQUIRE_THAT(refusal->message, ContainsSubstring("necessary condition"));
        REQUIRE_THAT(refusal->message, ContainsSubstring("not a sufficient one"));
    }
}

TEST_CASE("StructuralValidation_DoesNotClaimSixDegreesOfFreedomIsSufficient",
          "[structural][valid]") {
    // THE ASYMMETRY, PROVED RATHER THAN DOCUMENTED.
    //
    // Z only on the whole start cap gives far MORE than six constrained
    // degrees of freedom, and removes exactly three rigid modes:
    // z-translation and two rotations. X-translation, y-translation and
    // rotation about z remain. A validator applying the forbidden rule would
    // call this model supported.
    ValidatablePart part;
    part.setAnalysis(part.endLoad(1000.0), part.zOnlyEverywhere());

    const StructuralValidationReport report = part.validate();
    INFO("constrained DOFs " << report.constrainedDegreesOfFreedom);

    SECTION("validation accepts it, because the count rule cannot see the problem") {
        REQUIRE(report.constrainedDegreesOfFreedom > structural::kMinimumConstrainedDofs);
        REQUIRE(report.acceptable());
    }

    SECTION("and the FACTORISATION refuses it, which is where sufficiency is decided") {
        const Result<structural::StructuralSolveOutcome> outcome = part.solve();
        REQUIRE_FALSE(outcome.has_value());
        // The solver's own diagnostic, unchanged and not re-worded here.
        INFO(outcome.error().message);
        REQUIRE_THAT(outcome.error().message, ContainsSubstring("constrained"));
    }
}

TEST_CASE("StructuralValidation_RefusesATargetThatDoesNotResolve", "[structural][valid]") {
    SECTION("a restraint naming a face the mesh does not carry") {
        ValidatablePart part;
        part.setAnalysis(part.endLoad(1000.0),
                         {structural::StructuralRestraint::fixedSupport(
                             RestraintId::fromValue(1), part.missingFace())});
        const StructuralValidationReport report = part.validate();
        REQUIRE(report.status() == ValidationStatus::Rejected);
        REQUIRE(carries(report, ValidationCode::RestraintsUnresolvable));
        REQUIRE(report.reachedStage == Stage::Restraints);
    }

    SECTION("a load naming a face the mesh does not carry") {
        ValidatablePart part;
        part.setAnalysis({structural::StructuralLoad{
                             LoadId::fromValue(1),
                             structural::PressureLoad{.face = part.missingFace(),
                                                      .magnitude = Pressure::fromSi(1.0e6)}}},
                         part.fixedStart());
        const StructuralValidationReport report = part.validate();
        REQUIRE(report.status() == ValidationStatus::Rejected);
        REQUIRE(carries(report, ValidationCode::LoadsUnresolvable));
        REQUIRE(report.reachedStage == Stage::Loads);
    }
}

TEST_CASE("StructuralValidation_WarnsWhenNothingIsLoaded", "[structural][valid]") {
    // A WARNING AND NOT A REFUSAL. With F = 0 the solution is u = 0 exactly,
    // which is a correct answer to a question probably not meant.
    ValidatablePart part;
    part.setAnalysis({}, part.fixedStart());
    const StructuralValidationReport report = part.validate();

    SECTION("it is acceptable, and says why it is worth mentioning") {
        REQUIRE(report.acceptable());
        REQUIRE(report.status() == ValidationStatus::AcceptedWithWarnings);
        REQUIRE(carries(report, ValidationCode::NoLoadsApplied));
        REQUIRE(report.reachedStage == Stage::Complete);
    }

    SECTION("and the solve runs, carries the warning, and really is identically zero") {
        const Result<structural::StructuralSolveOutcome> outcome = part.solve();
        INFO((outcome.has_value() ? std::string{} : outcome.error().message));
        REQUIRE(outcome.has_value());
        // THE WARNING SURVIVES A SUCCESSFUL SOLVE. A result computed under a
        // warning is still a result that should be shown with it.
        REQUIRE(carries(outcome->validation, ValidationCode::NoLoadsApplied));
        REQUIRE(outcome->largestDisplacement.si() == 0.0);
        REQUIRE(outcome->largestVonMises.si() == 0.0);
    }
}

TEST_CASE("StructuralValidation_RefusesAMaterialMissingWhatTheModeRequires",
          "[structural][valid]") {
    // NOT A REPEAT OF THE INPUT BOUNDARY. `requireStructuralModel` asks only
    // for linear-elastic constants, and this material HAS them;
    // `LinearStaticWithGravity` additionally needs a density, which it does
    // not. So the model passes stage 2 and fails stage 4, which is the whole
    // reason the two checks are separate.
    ValidatablePart part{/*withDensity=*/false};
    part.setAnalysis(part.endLoad(1000.0), part.fixedStart(),
                     structural::StructuralAnalysisMode::LinearStaticWithGravity);

    SECTION("the input boundary is satisfied, which is the premise") {
        REQUIRE(structural::requireStructuralModel(part.document, part.regenerator, part.mesher,
                                                   part.control)
                    .has_value());
    }

    SECTION("and the mode's own requirement is not") {
        const StructuralValidationReport report = part.validate();
        REQUIRE(report.status() == ValidationStatus::Rejected);
        REQUIRE(carries(report, ValidationCode::MaterialUnusable));
        REQUIRE(report.reachedStage == Stage::Material);
    }

    SECTION("and the same material IS usable for the mode that does not need it") {
        part.setAnalysis(part.endLoad(1000.0), part.fixedStart(),
                         structural::StructuralAnalysisMode::LinearStatic);
        REQUIRE(part.validate().acceptable());
    }
}

TEST_CASE("StructuralValidation_StopsAtTheFirstRefusingStage", "[structural][valid]") {
    // STAGES ARE DEPENDENT, so a cascade of consequences would bury the cause.
    // This model is wrong in THREE ways at once -- a stale mesh, an
    // unresolvable restraint and no load -- and the report names the stale
    // mesh, because the other two are questions about a mesh that no longer
    // describes the document.
    ValidatablePart part;
    part.setAnalysis({}, {structural::StructuralRestraint::fixedSupport(
                             RestraintId::fromValue(1), part.missingFace())});
    REQUIRE(part.document
                .modifyObject<features::ExtrudeFeature>(
                    part.feature,
                    [](features::ExtrudeFeature& extrude) -> Result<bool> {
                        auto definition = extrude.definition();
                        definition.depth = 35_mm;
                        return extrude.setDefinition(definition).has_value();
                    })
                .has_value());
    requireReport(part.regenerator, part.document);

    const StructuralValidationReport report = part.validate();
    REQUIRE(report.reachedStage == Stage::Inputs);
    REQUIRE(carries(report, ValidationCode::InputsUnavailable));
    REQUIRE_FALSE(carries(report, ValidationCode::RestraintsUnresolvable));
    REQUIRE_FALSE(carries(report, ValidationCode::NoLoadsApplied));
    REQUIRE(report.findings.size() == 1);
}

TEST_CASE("StructuralValidation_SolvesAWellPosedAnalysisEndToEnd", "[structural][valid]") {
    // THE WHOLE CHAIN, through the one entry point, from a Document.
    ValidatablePart part;
    part.setAnalysis(part.endLoad(1000.0), part.fixedStart());
    const Result<structural::StructuralSolveOutcome> outcome = part.solve();
    INFO((outcome.has_value() ? std::string{} : outcome.error().message));
    REQUIRE(outcome.has_value());

    SECTION("the result describes the mesh it was computed on") {
        const Result<structural::StructuralModel> model = structural::requireStructuralModel(
            part.document, part.regenerator, part.mesher, part.control);
        REQUIRE(model.has_value());
        REQUIRE(outcome->result.describes(model->mesh()));
        REQUIRE(outcome->result.displacements().size() == model->mesh().mesh().nodes().size());
        REQUIRE(outcome->result.stresses().size() == model->mesh().mesh().tetrahedra().size());
        REQUIRE(outcome->result.strains().size() == model->mesh().mesh().tetrahedra().size());
        REQUIRE_FALSE(outcome->result.reactions().empty());
    }

    SECTION("and the result is CURRENT, which is P17-DATA's answer and not this file's") {
        const Result<structural::StructuralResultSource> current = structural::currentResultSource(
            part.document, part.regenerator, part.mesher, part.analysis);
        REQUIRE(current.has_value());
        REQUIRE(structural::resultCurrency(&outcome->result, *current) ==
                structural::ResultCurrency::Current);
        REQUIRE(structural::describesTheModel(
            structural::resultCurrency(&outcome->result, *current)));
    }

    SECTION("and every gate's own measurement is recorded rather than recomputed") {
        REQUIRE(outcome->freeEquations > 0);
        REQUIRE(outcome->pivotRatio > 0.0);
        REQUIRE(std::isfinite(outcome->pivotRatio));
        REQUIRE(outcome->strainEnergy.si() > 0.0);
        REQUIRE(outcome->residual.normalized >= 0.0);
        REQUIRE(outcome->residual.normalized < 1.0e-9);
        REQUIRE(outcome->largestDisplacement.si() > 0.0);
        REQUIRE(outcome->largestVonMises.si() > 0.0);
    }

    SECTION("and equilibrium holds, with an independent check that it is THIS problem") {
        REQUIRE(outcome->forceBalance.normalized < 1.0e-12);
        REQUIRE(outcome->momentBalance.normalized < 1.0e-12);
        // The applied load really was 1000 N along +z, so the reaction is
        // -1000 N. Without this the balance could be a pair of zeros.
        REQUIRE_THAT(outcome->forceBalance.reaction.z.si(), WithinRel(-1000.0, 1e-9));
        REQUIRE_THAT(outcome->forceBalance.external.z.si(), WithinRel(1000.0, 1e-9));
    }

    SECTION("and the solve is deterministic") {
        const Result<structural::StructuralSolveOutcome> again = part.solve();
        REQUIRE(again.has_value());
        REQUIRE(again->result.displacements().size() == outcome->result.displacements().size());
        for (std::size_t i = 0; i < again->result.displacements().size(); ++i) {
            REQUIRE(again->result.displacements()[i] == outcome->result.displacements()[i]);
        }
        REQUIRE(again->pivotRatio == outcome->pivotRatio);
        REQUIRE(again->strainEnergy == outcome->strainEnergy);
    }
}

TEST_CASE("StructuralValidation_PublishesNothingWhenAGateRefuses", "[structural][valid]") {
    // EVERY GATE BELOW THIS ENTRY POINT STILL REFUSES, and a refusal means NO
    // result -- not a worse result. A partially populated result is not a
    // degraded answer, it is an answer that claims to be a solution and is not.
    //
    // Neither failure can be injected from outside, so each is reached by
    // tightening the gate BELOW the measured value -- which also proves the
    // gates are live rather than nominal.
    ValidatablePart part;
    part.setAnalysis(part.endLoad(1000.0), part.fixedStart());
    const Result<structural::StructuralSolveOutcome> baseline = part.solve();
    REQUIRE(baseline.has_value());

    SECTION("the baseline really did pass both, which is the premise") {
        REQUIRE(baseline->residual.normalized > 0.0);
        REQUIRE(baseline->forceBalance.normalized > 0.0);
    }

    SECTION("the solver's residual gate") {
        structural::SolverSettings strict{};
        strict.relativeResidualTolerance = 1.0e-300;
        const Result<structural::StructuralSolveOutcome> outcome = part.solve(strict);
        REQUIRE_FALSE(outcome.has_value());
        INFO(outcome.error().message);
        REQUIRE_THAT(outcome.error().message, ContainsSubstring("residual"));
    }

    SECTION("the equilibrium gate") {
        structural::EquilibriumTolerance strict{};
        strict.force = 1.0e-300;
        strict.moment = 1.0e-300;
        const Result<structural::StructuralSolveOutcome> outcome = part.solve({}, strict);
        REQUIRE_FALSE(outcome.has_value());
        INFO(outcome.error().message);
        REQUIRE_THAT(outcome.error().message, ContainsSubstring("quilibrium"));
    }
}

TEST_CASE("StructuralValidation_ComparesStrictlySoAValueOnTheBoundIsOnTheGoodSide",
          "[structural][valid]") {
    // THE EXACT BOUNDARY, WHICH NO MEASURED MESH CAN POSE. Every real radius
    // ratio is an irrational-looking double and will never land exactly on
    // 3.0e-4 or 1e-10, so the strictness of the comparison is untestable
    // through a mesh -- and an untested comparison is where `<` quietly
    // becomes `<=`.
    //
    // So the report is SYNTHESISED. `validateStructuralMeshQuality` takes a
    // `MeshQualityReport` by design, which is exactly what makes this
    // reachable: the summaries can be set to the bound itself. This is not a
    // back door -- it is the function's own parameter, and P16's per-element
    // classification is strict in the same direction, which
    // `AgreesWithP16sOwnPerElementClassification` holds.
    const auto reportWithRadiusRatio = [](double value) {
        meshing::MeshQualityReport quality{};
        quality.structurallyValid = true;
        quality.tetCount = 1;
        quality.summaries[QualityMetric::TetRadiusRatio] =
            meshing::MetricSummary{.count = 1,
                                   .minimum = value,
                                   .maximum = value,
                                   .mean = value,
                                   .worst = meshing::ElementId::fromValue(1)};
        return quality;
    };

    const meshing::QualityThresholds policy = structural::structuralQualityThresholds();
    const double warnBound = policy.limits.at(QualityMetric::TetRadiusRatio).warning.value();
    const double failBound = policy.limits.at(QualityMetric::TetRadiusRatio).failure.value();

    SECTION("exactly on the warning bound does NOT warn") {
        const StructuralValidationReport report =
            structural::validateStructuralMeshQuality(reportWithRadiusRatio(warnBound));
        REQUIRE(report.status() == ValidationStatus::Accepted);
        REQUIRE_FALSE(carries(report, ValidationCode::ElementOutsideQualifiedEnvelope));
    }

    SECTION("and the next double below it does warn") {
        // `nextafter` toward zero: the smallest possible step past the bound.
        // Together with the section above this pins the comparison to ONE
        // representable value, which is as tight as a boundary test can be.
        const StructuralValidationReport report = structural::validateStructuralMeshQuality(
            reportWithRadiusRatio(std::nextafter(warnBound, 0.0)));
        REQUIRE(report.status() == ValidationStatus::AcceptedWithWarnings);
        REQUIRE(carries(report, ValidationCode::ElementOutsideQualifiedEnvelope));
    }

    SECTION("exactly on the accuracy floor is NOT refused") {
        const StructuralValidationReport report =
            structural::validateStructuralMeshQuality(reportWithRadiusRatio(failBound));
        REQUIRE_FALSE(carries(report, ValidationCode::ElementAccuracyBelowFloor));
        // It is below the warning bound, so it warns -- which is the right
        // answer and also proves the summary was read rather than ignored.
        REQUIRE(report.status() == ValidationStatus::AcceptedWithWarnings);
        REQUIRE(report.acceptable());
    }

    SECTION("and the next double below the floor IS refused") {
        const StructuralValidationReport report = structural::validateStructuralMeshQuality(
            reportWithRadiusRatio(std::nextafter(failBound, 0.0)));
        REQUIRE(report.status() == ValidationStatus::Rejected);
        REQUIRE(carries(report, ValidationCode::ElementAccuracyBelowFloor));
    }

    SECTION("a LowerIsBetter metric is strict in the other direction") {
        // The maximum dihedral angle: worse LARGER. The same boundary, with
        // the comparison reversed, so a single `past()` that ignored the
        // direction would pass one of these two sections and fail the other.
        meshing::MeshQualityReport quality{};
        quality.structurallyValid = true;
        quality.tetCount = 1;
        const double bound = policy.limits.at(QualityMetric::TetMaxDihedralAngle).warning.value();
        quality.summaries[QualityMetric::TetMaxDihedralAngle] =
            meshing::MetricSummary{.count = 1,
                                   .minimum = bound,
                                   .maximum = bound,
                                   .mean = bound,
                                   .worst = meshing::ElementId::fromValue(1)};
        REQUIRE(structural::validateStructuralMeshQuality(quality).status() ==
                ValidationStatus::Accepted);

        quality.summaries[QualityMetric::TetMaxDihedralAngle].maximum =
            std::nextafter(bound, 4.0);
        REQUIRE(structural::validateStructuralMeshQuality(quality).status() ==
                ValidationStatus::AcceptedWithWarnings);
    }

    SECTION("and a metric with no measured elements is skipped, not treated as zero") {
        // A summary with `count == 0` carries no value. Comparing its default
        // 0.0 against a HigherIsBetter bound would refuse an empty metric as
        // catastrophically bad -- which is the defect `count` exists to
        // prevent.
        meshing::MeshQualityReport quality{};
        quality.structurallyValid = true;
        quality.tetCount = 1;
        quality.summaries[QualityMetric::TetRadiusRatio] =
            meshing::MetricSummary{.count = 0,
                                   .minimum = 0.0,
                                   .maximum = 0.0,
                                   .mean = 0.0,
                                   .worst = meshing::ElementId{}};
        const StructuralValidationReport report =
            structural::validateStructuralMeshQuality(quality);
        REQUIRE(report.status() == ValidationStatus::Accepted);
        REQUIRE(report.worstByMetric.empty());
    }
}

namespace {

/// A plate thin enough that its mesh carries a QUALITY FINDING, and a document
/// behind it so the finding has to travel the analysis path.
///
/// IT EXISTS BECAUSE A MUTATION SURVIVED. Dropping the quality findings on
/// their way into the analysis-level report changed nothing in any test,
/// because every other fixture meshes cleanly and there was no finding to
/// drop. Measured at 40 x 30 mm with a 20 mm target: a 1 mm plate still comes
/// back `accepted`, a 0.5 mm plate carries one envelope warning (aspect
/// 100.005 against the 81.0 bound), and a 0.1 mm plate carries four.
struct ThinPlatePart {
    Document document{"Thin"};
    features::Regenerator regenerator;
    meshing::Mesher mesher;
    ObjectId feature{};
    MeshControlId control{};
    AnalysisId analysis{};
    MaterialId material{};

    explicit ThinPlatePart(double depthMillimetres = 0.5) {
        auto profile = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        (void)addRectangle(*profile, 0_mm, 0_mm, 40_mm, 30_mm);
        const ObjectId sketchId = require(document.addObject(std::move(profile)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(sketchId.value()),
                      .depth = Length::fromSi(depthMillimetres / 1000.0)});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));

        auto intent = meshing::MeshControl::create(
            "Mesh", meshing::MeshControlDefinition{
                        .body = feature, .mesh = {.sizing = {.globalTargetSize = 20_mm}}});
        REQUIRE(intent.has_value());
        control = MeshControlId::fromValue(require(document.addObject(std::move(*intent))).value());

        features::MaterialDefinition definition;
        definition.designation = "Steel";
        definition.mechanical.youngsModulus =
            materials::MaterialProperty<ElasticModulus>::known(ElasticModulus::fromSi(kYoungs));
        definition.mechanical.poissonRatio =
            materials::MaterialProperty<bettercad::PoissonRatio>::known(
                PoissonRatio::of(kPoisson));
        definition.mechanical.density =
            materials::MaterialProperty<Density>::known(Density::fromSi(7850.0));
        const Result<MaterialId> id = features::createMaterial(document, "Steel", definition);
        REQUIRE(id.has_value());
        material = *id;
        REQUIRE(features::assignMaterial(document, material).has_value());

        auto study = structural::StructuralAnalysis::create(
            "Study", structural::StructuralAnalysisDefinition{
                         .mesh = control,
                         .restraints = {structural::StructuralRestraint::fixedSupport(
                             RestraintId::fromValue(1),
                             FaceName{feature, FaceSelector{.role = FaceRole::StartCap}})}});
        REQUIRE(study.has_value());
        analysis = AnalysisId::fromValue(require(document.addObject(std::move(*study))).value());

        requireReport(regenerator, document);
        const Result<const meshing::VolumeMesh*> built =
            mesher.generate(document, regenerator, control);
        INFO((built.has_value() ? std::string{} : built.error().message));
        REQUIRE(built.has_value());
    }


    [[nodiscard]] StructuralValidationReport validate() const {
        return structural::validateStructuralAnalysisForSolve(document, regenerator, mesher,
                                                              analysis);
    }
};

} // namespace

TEST_CASE("StructuralValidation_CarriesAMeshQualityFindingIntoTheAnalysisReport",
          "[structural][valid]") {
    // THE MERGE, EXERCISED. A mutation that dropped the quality stage's
    // findings on their way into the analysis-level report SURVIVED the whole
    // suite, because every other fixture meshes cleanly and there was nothing
    // to drop. This plate is thin enough that there is.
    const ThinPlatePart part;

    // The mesh-level verdict, which is where the finding originates.
    Result<structural::StructuralModel> model = structural::requireStructuralModel(
        part.document, part.regenerator, part.mesher, part.control);
    REQUIRE(model.has_value());
    const StructuralValidationReport meshLevel =
        structural::validateStructuralMeshQuality(model->quality());

    // THE PREMISE, asserted so this test cannot pass for the wrong reason: the
    // mesh really does carry a finding, and it really is a warning rather than
    // a refusal.
    INFO("mesh-level findings " << meshLevel.findings.size() << ", verdict "
                                << structural::toString(meshLevel.status()));
    REQUIRE_FALSE(meshLevel.findings.empty());
    REQUIRE(meshLevel.status() == ValidationStatus::AcceptedWithWarnings);

    const StructuralValidationReport analysisLevel = part.validate();

    SECTION("the analysis-level report carries the same findings") {
        REQUIRE(analysisLevel.findings.size() >= meshLevel.findings.size());
        for (const structural::ValidationFinding& found : meshLevel.findings) {
            INFO("looking for " << structural::toString(found.code) << " on element "
                                << (found.element ? found.element->value() : 0));
            REQUIRE(std::ranges::any_of(
                analysisLevel.findings, [&found](const structural::ValidationFinding& mine) {
                    return mine.code == found.code && mine.element == found.element &&
                           mine.metric == found.metric && mine.value == found.value;
                }));
        }
    }

    SECTION("and it is still ACCEPTED, because an envelope warning refuses nothing") {
        REQUIRE(analysisLevel.acceptable());
        REQUIRE(analysisLevel.status() == ValidationStatus::AcceptedWithWarnings);
        REQUIRE(carries(analysisLevel, ValidationCode::ElementOutsideQualifiedEnvelope));
        // THE CHAIN RAN TO THE END. A warning must not short-circuit the
        // staged validation -- only a refusal does.
        REQUIRE(analysisLevel.reachedStage == Stage::Complete);
    }

    SECTION("and the measurements come across too, not only the findings") {
        REQUIRE(analysisLevel.worstByMetric.size() == 4);
        REQUIRE(analysisLevel.worstByMetric == meshLevel.worstByMetric);
    }
}
