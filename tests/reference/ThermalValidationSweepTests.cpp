// P18-VALID-001 -- the thermal mesh policy against every mesh BetterCAD has qualified.
//
// THE TEST THE POLICY HAS TO PASS. "Do not turn every imperfect mesh into a hard failure": a thermal acceptance policy that refuses a mesh
// BetterCAD produced and P16 QUALIFIED is refusing BetterCAD's own output. The two thresholds that refuse a mesh (3r/R below 7.0e-6, aspect
// ratio above 700) were derived from a measured law (ThermalValidationLawTests.cpp) and not from this set; this file shows how far the
// set is from them, and that the four envelope WARNING bounds are exactly the measured worst case rounded away from it, so that no
// qualified mesh warns either.
//
// NOTHING IS RECOMPUTED. Every number is read from P16's own `MeshQualityReport::summaries`.

#include "reference/MeshTestSupport.hpp"

#include <MeshReferenceModels.hpp>

#include <bettercad/meshing/MeshQuality.hpp>
#include <bettercad/thermal/ThermalValidation.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace bettercad;
using namespace bettercad::test;
using namespace bettercad::test::meshref;
using meshing::MeshQualityReport;
using meshing::QualityMetric;

struct Verdict {
    MeshQualityReport quality;
    thermal::ThermalMeshVerdict verdict;
};

template <typename Built>
[[nodiscard]] Verdict verdictOf(Built built) {
    REQUIRE(built.has_value());
    MeshedReference ref(std::move(built->document));
    MeshQualityReport quality = meshing::evaluateMeshQuality(ref.require().mesh(), meshing::reportOnlyThresholds());
    thermal::ThermalMeshVerdict verdict = thermal::classifyThermalMesh(quality);
    return Verdict{.quality = std::move(quality), .verdict = std::move(verdict)};
}

/// Every qualified reference model, in a fixed order.
[[nodiscard]] std::vector<std::pair<std::string, Verdict>> allQualified() {
    std::vector<std::pair<std::string, Verdict>> out;
    out.emplace_back("RM-MESH-01 block", verdictOf(reference::buildMeshBlockReferenceModel()));
    out.emplace_back("RM-MESH-02 cylinder", verdictOf(reference::buildMeshCylinderReferenceModel()));
    out.emplace_back("RM-MESH-03 plate with hole", verdictOf(reference::buildMeshPlateWithHoleReferenceModel()));
    out.emplace_back("RM-MESH-04 hollow tube", verdictOf(reference::buildMeshTubeReferenceModel()));
    out.emplace_back("RM-MESH-05 thin plate", verdictOf(reference::buildMeshThinPlateReferenceModel()));
    out.emplace_back("RM-MESH-06 transformed placed", verdictOf(reference::buildMeshTransformedPlacedReferenceModel()));
    out.emplace_back("RM-MESH-07 local refinement", verdictOf(reference::buildMeshLocalRefinementReferenceModel()));
    return out;
}

[[nodiscard]] double worstOf(const MeshQualityReport& report, QualityMetric metric) {
    const meshing::MetricSummary& s = report.summaries.at(metric);
    return meshing::direction(metric) == meshing::QualityDirection::HigherIsBetter ? s.minimum : s.maximum;
}

} // namespace

TEST_CASE("ThermalValidation_AcceptsEveryQualifiedReferenceMeshWithoutAFinding", "[thermal][validation][reference][mesh]") {
    for (const auto& [name, v] : allQualified()) {
        const double radius = worstOf(v.quality, QualityMetric::TetRadiusRatio);
        const double aspect = worstOf(v.quality, QualityMetric::TetAspectRatio);
        const double minDihedral = worstOf(v.quality, QualityMetric::TetMinDihedralAngle);
        const double maxDihedral = worstOf(v.quality, QualityMetric::TetMaxDihedralAngle);
        WARN(std::format("SWEEP {} | tets {} | worst 3r/R {:.6e} (floor {:.1e}, margin {:.3e}x) | worst aspect {:.4f} (ceiling {:.0f}, margin {:.3e}x) | "
                         "min dihedral {:.6e} | max dihedral {:.6e}",
                         name, v.quality.tetCount, radius, thermal::kThermalRadiusRatioFloor, radius / thermal::kThermalRadiusRatioFloor, aspect,
                         thermal::kThermalAspectRatioCeiling, thermal::kThermalAspectRatioCeiling / aspect, minDihedral, maxDihedral));
        INFO("model " << name);
        REQUIRE(v.quality.structurallyValid);
        // ACCEPTED: no refusal ...
        CHECK(v.verdict.structural.empty());
        CHECK(std::ranges::none_of(v.verdict.quality,
                                   [](const thermal::ValidationFinding& f) { return f.severity == thermal::ValidationSeverity::Error; }));
        // ... AND NOT BY ACCIDENT: it clears each failure bound by a wide margin, so the bounds are not within sight of anything qualified.
        CHECK(radius > thermal::kThermalRadiusRatioFloor * 40.0);
        CHECK(aspect < thermal::kThermalAspectRatioCeiling / 8.0);
        // ... and no qualified mesh WARNS either: the warning bounds were set to the worst qualified value and rounded away from it.
        CHECK(v.verdict.quality.empty());
    }
}

TEST_CASE("ThermalValidation_TheEnvelopeBoundsAreTheMeasuredWorstCase", "[thermal][validation][reference][mesh]") {
    // The other direction of the same claim, and the one that keeps an envelope an envelope: each bound sits just OUTSIDE the measured worst
    // value -- not comfortably clear of it, which would make the warnings fire only on absurd meshes.
    double worstRadius = 1.0;
    double worstMinDihedral = 4.0;
    double worstMaxDihedral = 0.0;
    double worstAspect = 1.0;
    for (const auto& [name, v] : allQualified()) {
        worstRadius = std::min(worstRadius, worstOf(v.quality, QualityMetric::TetRadiusRatio));
        worstMinDihedral = std::min(worstMinDihedral, worstOf(v.quality, QualityMetric::TetMinDihedralAngle));
        worstMaxDihedral = std::max(worstMaxDihedral, worstOf(v.quality, QualityMetric::TetMaxDihedralAngle));
        worstAspect = std::max(worstAspect, worstOf(v.quality, QualityMetric::TetAspectRatio));
    }
    const meshing::QualityThresholds policy = thermal::thermalQualityThresholds();
    const double radiusBound = policy.limits.at(QualityMetric::TetRadiusRatio).warning.value();
    const double minDihedralBound = policy.limits.at(QualityMetric::TetMinDihedralAngle).warning.value();
    const double maxDihedralBound = policy.limits.at(QualityMetric::TetMaxDihedralAngle).warning.value();
    const double aspectBound = policy.limits.at(QualityMetric::TetAspectRatio).warning.value();
    WARN(std::format("ENVELOPE | measured worst 3r/R {:.9e} vs bound {:.9e} | aspect {:.9f} vs {:.9f} | min dihedral {:.9e} vs {:.9e} | max dihedral {:.9f} vs {:.9f}",
                     worstRadius, radiusBound, worstAspect, aspectBound, worstMinDihedral, minDihedralBound, worstMaxDihedral, maxDihedralBound));
    SECTION("each bound is on the good side of the measured worst, so nothing qualified warns") {
        CHECK(worstRadius > radiusBound);
        CHECK(worstMinDihedral > minDihedralBound);
        CHECK(worstMaxDihedral < maxDihedralBound);
        CHECK(worstAspect < aspectBound);
    }
    SECTION("and each is TIGHT against it, within a factor of two") {
        CHECK(worstRadius < radiusBound * 2.0);
        CHECK(worstMinDihedral < minDihedralBound * 2.0);
        CHECK(worstAspect > aspectBound / 2.0);
        CHECK(maxDihedralBound - worstMaxDihedral < 0.05);
    }
    SECTION("and every failure bound is far beyond the envelope") {
        CHECK(worstRadius > thermal::kThermalRadiusRatioFloor * 40.0);
        CHECK(worstAspect < thermal::kThermalAspectRatioCeiling / 8.0);
    }
}
