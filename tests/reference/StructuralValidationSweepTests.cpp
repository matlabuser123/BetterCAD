// P17-VALID-001 -- the measured quality sweep the acceptance policy is built on.
//
// THIS FILE IS THE EVIDENCE, AND IT RAN BEFORE THE POLICY EXISTED. The brief
// forbids choosing a structural mesh threshold from intuition, so this sweep
// was written and run first, over every qualified P16 reference model and over
// the deliberately pathological fixtures, and the policy was written from what
// it printed.
//
// WHAT IT MEASURES, per model: the worst radius ratio, the worst aspect ratio,
// the worst minimum dihedral and the worst maximum dihedral, each with the
// ElementId holding it -- read from P16's own `MeshQualityReport::summaries`,
// which carry a minimum, maximum, mean and worst element for every metric
// whatever policy was active. NOTHING IS RECOMPUTED HERE: a second radius-ratio
// formula in P17 is the defect the architecture boundary exists to prevent.
//
// THE HEADLINE FINDING, which decides the whole policy: a QUALIFIED CYLINDER
// is numerically worse than a deliberate sliver. P16-QUALITY-001 already
// recorded a 1.98 deg sliver at a radius ratio of 0.000899 and a uniform
// cylinder at 0.000451 -- better than twice as bad. So no radius-ratio line
// separates legitimate curved-body meshes from pathological ones, and a hard
// quality rejection on that metric is impossible without refusing meshes
// BetterCAD itself produces and P16 qualified.
//
// AND THE SECOND FINDING, WHICH IS WHERE THE HARD-FAIL THRESHOLD COMES FROM.
// Tet4's reproduction of a linear displacement field DOES degrade as the
// element flattens -- measured, not assumed -- and it degrades as
// `C / sqrt(3r/R)` with C between 2.6e-16 and 2.4e-15 over SIXTEEN orders of
// radius ratio and two fields three orders of strain apart. Inverting that at
// BetterCAD's 1e-9 geometric band gives `3r/R < 1e-10` as a derived hard
// failure -- six orders below anything qualified, so it refuses nothing
// legitimate. The last test case is that measurement, and it pins the law.

#include "reference/MeshTestSupport.hpp"

#include <MeshReferenceModels.hpp>

#include <bettercad/core/units/Literals.hpp>
#include <bettercad/meshing/MeshQuality.hpp>
#include <bettercad/meshing/MeshSizing.hpp>
#include <bettercad/structural/StructuralPost.hpp>
#include <bettercad/structural/StructuralValidation.hpp>
#include <bettercad/structural/Tet4Element.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using namespace bettercad::test::meshref;
using Catch::Matchers::WithinAbs;
using meshing::MeshQualityReport;
using meshing::MetricSummary;
using meshing::QualityMetric;
using structural::ElasticityMatrix;
using structural::kTet4Nodes;

[[nodiscard]] double degrees(double radians) {
    return radians * 180.0 / std::numbers::pi;
}

/// The worst value of @p metric and the element holding it, taken from P16's
/// own summary rather than recomputed.
struct Worst {
    double value = 0.0;
    meshing::ElementId element{};
    bool present = false;
};

[[nodiscard]] Worst worstOf(const MeshQualityReport& report, QualityMetric metric) {
    const auto found = report.summaries.find(metric);
    if (found == report.summaries.end() || found->second.count == 0) {
        return Worst{};
    }
    const MetricSummary& summary = found->second;
    // The direction is P16's, asked rather than assumed.
    const bool higherIsBetter = meshing::direction(metric) == meshing::QualityDirection::HigherIsBetter;
    return Worst{.value = higherIsBetter ? summary.minimum : summary.maximum,
                 .element = summary.worst,
                 .present = true};
}

void report(const std::string& name, const MeshQualityReport& quality) {
    const Worst radius = worstOf(quality, QualityMetric::TetRadiusRatio);
    const Worst aspect = worstOf(quality, QualityMetric::TetAspectRatio);
    const Worst minDih = worstOf(quality, QualityMetric::TetMinDihedralAngle);
    const Worst maxDih = worstOf(quality, QualityMetric::TetMaxDihedralAngle);

    WARN("SWEEP | " << name << " | tets " << quality.tetCount << " | structurallyValid "
                    << (quality.structurallyValid ? "YES" : "NO") << " | worst 3r/R "
                    << radius.value << " (element " << radius.element.value()
                    << ") | worst aspect " << aspect.value << " (element "
                    << aspect.element.value() << ") | worst minDihedral "
                    << degrees(minDih.value) << " deg | worst maxDihedral "
                    << degrees(maxDih.value) << " deg");
}

/// P16's own flattening fixture: an equilateral base with the apex brought
/// down, which is where the aspect ratio's saturation was measured. ONE
/// DEFINITION, shared by the mesh sweep and the element-kernel measurement, so
/// the shapes the second test uses are provably the shapes the first measured.
[[nodiscard]] std::array<Point3D, kTet4Nodes> wedgeCorners(double apexMillimetres) {
    const double a = 0.010; // 10 mm equilateral base
    const double h = a * std::sqrt(3.0) / 2.0;
    return {Point3D{Length::fromSi(0.0), Length::fromSi(0.0), Length::fromSi(0.0)},
            Point3D{Length::fromSi(a), Length::fromSi(0.0), Length::fromSi(0.0)},
            Point3D{Length::fromSi(a / 2.0), Length::fromSi(h), Length::fromSi(0.0)},
            Point3D{Length::fromSi(a / 2.0), Length::fromSi(h / 3.0),
                    Length::fromSi(apexMillimetres / 1000.0)}};
}

/// A NEEDLE, which is the OTHER way a tetrahedron degenerates. The wedge
/// flattens toward a plane; this stretches toward a line -- an equilateral base
/// of @p baseMillimetres with the apex 10 mm away, so shrinking the base drives
/// the element toward a spike rather than a sheet.
///
/// IT EXISTS BECAUSE OF ADVERSARIAL REVIEW. The accuracy law that supplies this
/// milestone's one hard-fail threshold was first measured on the wedge family
/// alone, and a constant measured on one degeneration is not a property of the
/// kernel. If the needle obeyed a different law, the threshold would have been
/// derived from a coincidence.
[[nodiscard]] std::array<Point3D, kTet4Nodes> needleCorners(double baseMillimetres) {
    const double b = baseMillimetres / 1000.0;
    const double h = b * std::sqrt(3.0) / 2.0;
    return {Point3D{Length::fromSi(0.0), Length::fromSi(0.0), Length::fromSi(0.0)},
            Point3D{Length::fromSi(b), Length::fromSi(0.0), Length::fromSi(0.0)},
            Point3D{Length::fromSi(b / 2.0), Length::fromSi(h), Length::fromSi(0.0)},
            Point3D{Length::fromSi(b / 2.0), Length::fromSi(h / 3.0), Length::fromSi(0.010)}};
}

/// One tetrahedron from four corners, through the production builder.
[[nodiscard]] meshing::Mesh oneTetMesh(const std::array<Point3D, kTet4Nodes>& corners) {
    meshing::MeshBuilder builder;
    for (const Point3D& corner : corners) {
        REQUIRE(builder.addNode(corner).has_value());
    }
    REQUIRE(builder
                .addTetrahedron({meshing::NodeId::fromValue(1), meshing::NodeId::fromValue(2),
                                 meshing::NodeId::fromValue(3), meshing::NodeId::fromValue(4)},
                                meshing::RegionId::fromValue(1))
                .has_value());
    Result<meshing::Mesh> mesh = builder.build();
    INFO((mesh.has_value() ? std::string{} : mesh.error().message));
    REQUIRE(mesh.has_value());
    return std::move(*mesh);
}

[[nodiscard]] meshing::Mesh wedgeMesh(double apexMillimetres) {
    return oneTetMesh(wedgeCorners(apexMillimetres));
}

} // namespace

TEST_CASE("StructuralValidation_SweepsTheQualifiedReferenceMeshes",
          "[structural][validation][reference]") {
    // EVERY QUALIFIED P16 REFERENCE, measured under P16's own metrics. These
    // are the meshes a structural acceptance policy MUST NOT refuse without a
    // documented engineering reason, so their numbers are the lower edge of
    // what "legitimate" means.
    SECTION("RM-MESH-01 block") {
        auto built = reference::buildMeshBlockReferenceModel();
        REQUIRE(built.has_value());
        MeshedReference ref(std::move(built->document));
        const meshing::VolumeMesh& volume = ref.require();
        const MeshQualityReport quality =
            meshing::evaluateMeshQuality(volume.mesh(), meshing::reportOnlyThresholds());
        REQUIRE(quality.structurallyValid);
        report("RM-MESH-01 block", quality);
    }

    SECTION("RM-MESH-02 cylinder") {
        auto built = reference::buildMeshCylinderReferenceModel();
        REQUIRE(built.has_value());
        MeshedReference ref(std::move(built->document));
        const meshing::VolumeMesh& volume = ref.require();
        const MeshQualityReport quality =
            meshing::evaluateMeshQuality(volume.mesh(), meshing::reportOnlyThresholds());
        REQUIRE(quality.structurallyValid);
        report("RM-MESH-02 cylinder", quality);
    }

    SECTION("RM-MESH-03 plate with hole") {
        auto built = reference::buildMeshPlateWithHoleReferenceModel();
        REQUIRE(built.has_value());
        MeshedReference ref(std::move(built->document));
        const meshing::VolumeMesh& volume = ref.require();
        const MeshQualityReport quality =
            meshing::evaluateMeshQuality(volume.mesh(), meshing::reportOnlyThresholds());
        REQUIRE(quality.structurallyValid);
        report("RM-MESH-03 plate with hole", quality);
    }

    SECTION("RM-MESH-04 hollow tube") {
        auto built = reference::buildMeshTubeReferenceModel();
        REQUIRE(built.has_value());
        MeshedReference ref(std::move(built->document));
        const meshing::VolumeMesh& volume = ref.require();
        const MeshQualityReport quality =
            meshing::evaluateMeshQuality(volume.mesh(), meshing::reportOnlyThresholds());
        REQUIRE(quality.structurallyValid);
        report("RM-MESH-04 hollow tube", quality);
    }

    SECTION("RM-MESH-05 thin plate") {
        auto built = reference::buildMeshThinPlateReferenceModel();
        REQUIRE(built.has_value());
        MeshedReference ref(std::move(built->document));
        const meshing::VolumeMesh& volume = ref.require();
        const MeshQualityReport quality =
            meshing::evaluateMeshQuality(volume.mesh(), meshing::reportOnlyThresholds());
        REQUIRE(quality.structurallyValid);
        report("RM-MESH-05 thin plate", quality);
    }

    SECTION("RM-MESH-06 transformed, base and placed") {
        for (const bool placed : {false, true}) {
            auto built = placed ? reference::buildMeshTransformedPlacedReferenceModel()
                                : reference::buildMeshTransformedBaseReferenceModel();
            REQUIRE(built.has_value());
            MeshedReference ref(std::move(built->document));
            const meshing::VolumeMesh& volume = ref.require();
            const MeshQualityReport quality =
                meshing::evaluateMeshQuality(volume.mesh(), meshing::reportOnlyThresholds());
            REQUIRE(quality.structurallyValid);
            report(placed ? "RM-MESH-06 transformed placed" : "RM-MESH-06 transformed base",
                   quality);
        }
    }

    SECTION("RM-MESH-07 local refinement") {
        auto built = reference::buildMeshLocalRefinementReferenceModel();
        REQUIRE(built.has_value());
        MeshedReference ref(std::move(built->document));
        const meshing::VolumeMesh& volume = ref.require();
        const MeshQualityReport quality =
            meshing::evaluateMeshQuality(volume.mesh(), meshing::reportOnlyThresholds());
        REQUIRE(quality.structurallyValid);
        report("RM-MESH-07 local refinement", quality);
    }
}

TEST_CASE("StructuralValidation_SweepsTheDeliberatelyPathologicalFixtures",
          "[structural][validation][reference]") {
    // THE OTHER EDGE. A sliver is structurally VALID and badly shaped, which is
    // the distinction P16-QUALITY-001 exists to make -- and the reason a policy
    // cannot treat "bad metric" as "invalid".
    SECTION("the flattening wedge: aspect saturates while the radius ratio collapses") {
        for (const double apex : {10.0, 1.0, 0.1, 0.01}) {
            const meshing::Mesh mesh = wedgeMesh(apex);
            const MeshQualityReport quality =
                meshing::evaluateMeshQuality(mesh, meshing::reportOnlyThresholds());
            // STRUCTURALLY VALID. A sliver is not an invalid element.
            REQUIRE(quality.structurallyValid);
            const Worst radius = worstOf(quality, QualityMetric::TetRadiusRatio);
            const Worst aspect = worstOf(quality, QualityMetric::TetAspectRatio);
            const Worst minDih = worstOf(quality, QualityMetric::TetMinDihedralAngle);
            WARN("SWEEP pathological | wedge apex " << apex << " mm | 3r/R " << radius.value
                 << " | aspect " << aspect.value << " | minDihedral " << degrees(minDih.value)
                 << " deg | structurallyValid YES");
        }
    }

    SECTION("the aspect ratio really does saturate below sqrt(3)") {
        // P16's measured finding, re-asserted here because the whole policy
        // rests on it: a severely flattened element's aspect ratio approaches
        // 1.7320508 and STOPS, so an aspect-only policy cannot see a sliver.
        const meshing::Mesh flat = wedgeMesh(0.01);
        const MeshQualityReport quality =
            meshing::evaluateMeshQuality(flat, meshing::reportOnlyThresholds());
        const Worst aspect = worstOf(quality, QualityMetric::TetAspectRatio);
        const Worst radius = worstOf(quality, QualityMetric::TetRadiusRatio);
        INFO("aspect " << aspect.value << ", 3r/R " << radius.value);
        REQUIRE(aspect.value < std::sqrt(3.0) + 1e-9);
        // While the radius ratio has collapsed by four orders of magnitude.
        REQUIRE(radius.value < 1.0e-3);
    }
}

namespace {

/// A linear displacement field, `u = A x + b`, and its strain BY HAND.
///
/// INDEPENDENT OF PRODUCTION. Nothing here calls `B`, `strainFrom` or any
/// kinematics: the expected strain comes from the analytic derivatives of the
/// field as written. The same device P17-POST-001's reference suite uses, and
/// deliberately a separate local copy rather than a shared helper -- an oracle
/// two milestones share is an oracle that can be weakened once for both.
struct LinearField {
    std::array<double, 4> a{};
    std::array<double, 4> b{};
    std::array<double, 4> c{};

    [[nodiscard]] Translation3D at(const Point3D& p) const {
        const double x = p.x.si();
        const double y = p.y.si();
        const double z = p.z.si();
        return Translation3D{Length::fromSi(a[0] + a[1] * x + a[2] * y + a[3] * z),
                             Length::fromSi(b[0] + b[1] * x + b[2] * y + b[3] * z),
                             Length::fromSi(c[0] + c[1] * x + c[2] * y + c[3] * z)};
    }

    /// `exx = a1, eyy = b2, ezz = c3, gxy = a2+b1, gyz = b3+c2, gzx = c1+a3`.
    [[nodiscard]] std::array<double, 6> analyticStrain() const {
        return {a[1], b[2], c[3], a[2] + b[1], b[3] + c[2], c[1] + a[3]};
    }

    [[nodiscard]] std::array<Translation3D, kTet4Nodes>
    sample(const std::array<Point3D, kTet4Nodes>& nodes) const {
        return {at(nodes[0]), at(nodes[1]), at(nodes[2]), at(nodes[3])};
    }
};

[[nodiscard]] ElasticityMatrix steelElasticity() {
    materials::LinearElasticConstants constants{};
    constants.youngsModulus = ElasticModulus::fromSi(2.1e11);
    constants.poissonRatio = PoissonRatio::of(0.3);
    constants.shearModulus = ElasticModulus::fromSi(2.1e11 / (2.0 * 1.3));
    constants.bulkModulus = ElasticModulus::fromSi(2.1e11 / (3.0 * 0.4));
    Result<ElasticityMatrix> d = structural::isotropicElasticity(constants);
    INFO((d.has_value() ? std::string{} : d.error().message));
    REQUIRE(d.has_value());
    return *d;
}

} // namespace

TEST_CASE("StructuralValidation_MeasuresHowKernelAccuracyDegradesWithElementShape",
          "[structural][validation][reference]") {
    // FINDING 4, AND IT IS MEASURED HERE RATHER THAN ASSERTED IN A DOCUMENT.
    //
    // If element shape degraded Tet4's consistency, the degradation would be a
    // measurable function of shape and a threshold could be derived from it.
    // So the measurement is made: the SAME linear displacement field is
    // imposed on a regular tetrahedron and on every wedge in the sweep above,
    // and the recovered strain is compared against the field's own analytic
    // gradient.
    //
    // Through the PRODUCTION kernel. `recoverElementFields` is the function
    // `recoverFields` calls once per tetrahedron, so this is the path a solve
    // takes and not a parallel one.
    const LinearField field{.a = {1.0e-6, 2.0e-4, -5.0e-4, 3.0e-4},
                            .b = {-3.0e-6, 7.0e-4, -1.0e-4, 4.0e-4},
                            .c = {2.0e-6, -6.0e-4, 8.0e-4, 5.0e-4}};
    const std::array<double, 6> expected = field.analyticStrain();
    // ALL SIX COMPONENTS NONZERO, asserted rather than assumed: a field with a
    // zero component would hide an error in that component behind an absolute
    // comparison against zero.
    for (const double value : expected) {
        REQUIRE(value != 0.0);
    }
    const ElasticityMatrix d = steelElasticity();

    const auto worstRelativeError = [&](const std::array<Point3D, kTet4Nodes>& corners) {
        Result<structural::ElementFields> recovered = structural::recoverElementFields(
            meshing::ElementId::fromValue(1), corners, field.sample(corners), d);
        INFO((recovered.has_value() ? std::string{} : recovered.error().message));
        REQUIRE(recovered.has_value());
        const std::array<double, 6> got{recovered->strain.xx,      recovered->strain.yy,
                                        recovered->strain.zz,      recovered->strain.gammaXy,
                                        recovered->strain.gammaYz, recovered->strain.gammaZx};
        double worst = 0.0;
        for (std::size_t i = 0; i < got.size(); ++i) {
            worst = std::max(worst, std::abs(got[i] - expected[i]) / std::abs(expected[i]));
        }
        return worst;
    };

    // MEASURED, 2026-10-10, debug-ext:
    //
    //   regular tetrahedron   3r/R 1.0          8.13152e-16
    //   wedge apex 10 mm      3r/R 0.977082     2.71051e-16
    //   wedge apex  1 mm      3r/R 0.0849037    1.9877e-15
    //   wedge apex  0.1 mm    3r/R 0.00089946   1.17455e-14
    //   wedge apex  0.01 mm   3r/R 8.99995e-06  1.33176e-13
    //
    // The bound is 1e-12 for every shape: a 7.5x margin on the worst measured
    // value, chosen for the reason P17-REACTION-001 chose its own -- the error
    // grows with conditioning, so a bound with no margin would fail on a
    // machine whose last bit differs, and a bound ten orders loose would not
    // notice a kernel that had actually broken.
    const double kBound = 1.0e-12;

    SECTION("a regular tetrahedron, as the baseline") {
        const double side = 0.010;
        const std::array<Point3D, kTet4Nodes> regular{
            Point3D{Length::fromSi(0.0), Length::fromSi(0.0), Length::fromSi(0.0)},
            Point3D{Length::fromSi(side), Length::fromSi(0.0), Length::fromSi(0.0)},
            Point3D{Length::fromSi(side / 2.0), Length::fromSi(side * std::sqrt(3.0) / 2.0),
                    Length::fromSi(0.0)},
            Point3D{Length::fromSi(side / 2.0), Length::fromSi(side * std::sqrt(3.0) / 6.0),
                    Length::fromSi(side * std::sqrt(2.0 / 3.0))}};
        const double worst = worstRelativeError(regular);
        WARN("SHAPE | regular tetrahedron | worst relative strain error " << worst);
        REQUIRE(worst < kBound);
    }

    SECTION("and every wedge, down to a radius ratio of 9e-06") {
        for (const double apex : {10.0, 1.0, 0.1, 0.01}) {
            const double worst = worstRelativeError(wedgeCorners(apex));
            WARN("SHAPE | wedge apex " << apex << " mm | worst relative strain error " << worst);
            REQUIRE(worst < kBound);
        }
    }

    SECTION("the error GROWS with flattening, and by a bounded amount") {
        // BOTH HALVES MATTER, and neither is decoration.
        //
        // That it grows is what proves this measurement is sensitive to shape
        // at all. Without it the test would pass just as well against a
        // constant, and a reader could not tell "exact at every shape" from
        // "the fixture never varied".
        //
        // That the growth is BOUNDED is the engineering claim: FIVE orders of
        // magnitude of radius ratio (0.977 down to 9.0e-06) buy FEWER THAN
        // FOUR orders of error. Measured at 491x, so the bound below carries a
        // 20x margin -- and it is deliberately not tighter, because a ratio of
        // two machine-epsilon-scale quantities is the least stable number in
        // this file and the real gate on the severe case is `kBound` above.
        const double mild = worstRelativeError(wedgeCorners(10.0));
        const double severe = worstRelativeError(wedgeCorners(0.01));
        INFO("mild " << mild << ", severe " << severe << ", ratio " << severe / mild);
        REQUIRE(severe > mild);
        REQUIRE(severe < 1.0e4 * mild);
    }

    SECTION("a NEEDLE obeys a DIFFERENT law, which is why a second bound exists") {
        // ADVERSARIAL REVIEW'S FINDING, AND THE MEASUREMENT THAT SETTLED IT.
        //
        // The wedge family's law is `err ~ C / sqrt(3r/R)`, and this milestone
        // first derived its ONLY hard-fail bound from it. A constant measured
        // on one degeneration is not a property of the kernel, so the other
        // degeneration was measured -- and it does NOT obey that law. A needle
        // at `3r/R` 1.73e-03 errs 4.45e-13 while a wedge at a WORSE 9.0e-04
        // errs 1.17e-14: thirty-eight times less, at a worse radius ratio.
        //
        // What the needle DOES obey is `err ~ C * aspect`, measured here over
        // seven orders with C between 1.76e-16 and 4.45e-16. That law supplies
        // the aspect-ratio ceiling, and the two bounds together cover both
        // families -- each metric being blind to the other's.
        for (const double base : {10.0, 1.0, 0.1, 0.01, 1.0e-3, 1.0e-4, 1.0e-5, 1.0e-6}) {
            const std::array<Point3D, kTet4Nodes> corners = needleCorners(base);
            const MeshQualityReport quality =
                meshing::evaluateMeshQuality(oneTetMesh(corners), meshing::reportOnlyThresholds());
            REQUIRE(quality.structurallyValid);
            const double radiusRatio = worstOf(quality, QualityMetric::TetRadiusRatio).value;
            const double aspect = worstOf(quality, QualityMetric::TetAspectRatio).value;
            const double worst = worstRelativeError(corners);
            const double fit = radiusRatio > 0.0 ? worst * std::sqrt(radiusRatio) : 0.0;
            // `err / aspect`, the needle family's own fit constant.
            const double perAspect = aspect > 0.0 ? worst / aspect : 0.0;
            WARN("NEEDLE | base " << base << " mm | 3r/R " << radiusRatio << " | aspect "
                 << aspect << " | strain error " << worst << " | err*sqrt(3r/R) " << fit
                 << " | err/aspect " << perAspect);

            // THE NEEDLE'S LAW IS PINNED, because the aspect ceiling is derived
            // from it. Measured between 1.76e-16 and 4.45e-16 over seven orders
            // of aspect ratio, so this band carries 1.8x below and 2.2x above.
            REQUIRE(perAspect > 1.0e-16);
            REQUIRE(perAspect < 1.0e-15);

            // AND THE WEDGE'S LAW DOES NOT HOLD HERE, which is the whole
            // finding rather than a remark about it. `err * sqrt(3r/R)` leaves
            // the wedge family's band once the needle is severe enough, so a
            // policy resting on that one law was resting on a coincidence.
            if (aspect > 500.0) {
                REQUIRE(fit > 1.0e-14);
            }
        }
    }

    SECTION("and the aspect ceiling lands where the measurement puts it, on both sides") {
        // THE SAME TWO-SIDED VERIFICATION THE RADIUS FLOOR GETS. A threshold is
        // only derived if the measurement confirms it where it lands, so these
        // three needles straddle it -- the aspect ratio goes as the inverse of
        // the base, so 1e-4 mm gives 1e5 and 1e-5 mm gives 1e6.
        struct Expectation {
            double base;
            bool refused;
        };
        for (const Expectation& probe :
             {Expectation{.base = 1.0e-4, .refused = false},
              Expectation{.base = 1.0e-5, .refused = true},
              Expectation{.base = 1.0e-6, .refused = true}}) {
            const std::array<Point3D, kTet4Nodes> corners = needleCorners(probe.base);
            const MeshQualityReport quality =
                meshing::evaluateMeshQuality(oneTetMesh(corners), meshing::reportOnlyThresholds());
            const double aspect = worstOf(quality, QualityMetric::TetAspectRatio).value;
            const double worst = worstRelativeError(corners);
            const structural::StructuralValidationReport report =
                structural::validateStructuralMeshQuality(quality);
            const bool refused = !report.acceptable();
            INFO("base " << probe.base << " mm, aspect " << aspect << ", error " << worst
                         << ", verdict " << structural::toString(report.status()));
            REQUIRE(refused == probe.refused);

            // AND THE VERDICT AGREES WITH THE REQUIREMENT, which is what makes
            // the bound derived rather than merely placed: an ACCEPTED needle
            // is inside 1e-9, and the most severe REFUSED one is genuinely
            // outside it. The middle case is refused while still inside, which
            // is the margin erring in the safe direction.
            if (!refused) {
                REQUIRE(worst < structural::kStructuralStrainAccuracy);
            }
            if (probe.base <= 1.0e-6) {
                REQUIRE(worst > structural::kStructuralStrainAccuracy);
            }
        }
    }

    SECTION("and the needle really is a DIFFERENT degeneration, which the aspect ratio shows") {
        // The premise, so the section above cannot pass for the wrong reason.
        // A flattening wedge's aspect ratio saturates at sqrt(3); a needle's
        // grows without bound, because its longest edge stays 10 mm while its
        // shortest shrinks with the base.
        const MeshQualityReport quality = meshing::evaluateMeshQuality(
            oneTetMesh(needleCorners(0.01)), meshing::reportOnlyThresholds());
        const double aspect = worstOf(quality, QualityMetric::TetAspectRatio).value;
        INFO("needle aspect " << aspect);
        REQUIRE(aspect > 100.0);
    }

    SECTION("and where the VALIDITY boundary actually is, walked down by orders") {
        // THE LINE THE POLICY IS BUILT ON, measured rather than extrapolated.
        //
        // The fit above is `err ~ 4e-16 / sqrt(3r/R)`, which would reach 1 ppm
        // at a radius ratio around 1.6e-19. Rather than extrapolate a
        // floating-point fit nineteen orders, this walks the apex down and
        // records what each layer SAYS at each step: whether P16 still calls
        // the mesh structurally valid, and whether the production kernel still
        // recovers a finite strain.
        //
        // That boundary is what separates the two halves of the policy. A
        // badly shaped element that both layers accept is a WARNING; an
        // element either layer refuses is a HARD FAILURE. Knowing where the
        // refusal is makes the hard-fail path reachable rather than notional.
        // TWO FIELDS, THREE ORDERS OF STRAIN APART, and that is the control.
        // A hard-fail threshold is derived from the fit constant below, so the
        // constant has to be a property of the KERNEL and not of the one field
        // that happened to measure it. If it moved with the field, the law
        // would be a coincidence and no threshold could rest on it.
        const std::array<LinearField, 2> fields{
            field,
            LinearField{.a = {4.0e-9, 9.0e-7, -2.0e-7, 6.0e-7},
                        .b = {-1.0e-9, 3.0e-7, 8.0e-7, -5.0e-7},
                        .c = {7.0e-9, 1.0e-6, -4.0e-7, 2.0e-7}}};
        for (std::size_t which = 0; which < fields.size(); ++which) {
        const LinearField& probe = fields[which];
        const std::array<double, 6> probeExpected = probe.analyticStrain();
        for (const double value : probeExpected) {
            REQUIRE(value != 0.0);
        }
        // THE MIDDLE VALUES BRACKET THE DERIVED THRESHOLD. `3r/R` goes as the
        // square of the apex height, so 1.0e-4 mm lands at 9.0e-10 -- ABOVE
        // the 1e-10 hard-fail bound, accepted -- and 3.0e-5 mm at 8.1e-11,
        // just below it, refused. 1.0e-5 mm at 9.0e-12 is the step past that.
        // The threshold's placement is therefore measured on both sides
        // rather than interpolated between distant points.
        for (const double apex :
             {1.0e-2, 1.0e-4, 3.0e-5, 1.0e-5, 1.0e-6, 1.0e-8, 1.0e-10}) {
            const std::array<Point3D, kTet4Nodes> corners = wedgeCorners(apex);
            meshing::MeshBuilder builder;
            for (const Point3D& corner : corners) {
                REQUIRE(builder.addNode(corner).has_value());
            }
            REQUIRE(builder
                        .addTetrahedron({meshing::NodeId::fromValue(1),
                                         meshing::NodeId::fromValue(2),
                                         meshing::NodeId::fromValue(3),
                                         meshing::NodeId::fromValue(4)},
                                        meshing::RegionId::fromValue(1))
                        .has_value());
            Result<meshing::Mesh> mesh = builder.build();
            const bool meshBuilt = mesh.has_value();
            bool structurallyValid = false;
            double radiusRatio = 0.0;
            std::size_t invalidElements = 0;
            if (meshBuilt) {
                const MeshQualityReport quality =
                    meshing::evaluateMeshQuality(*mesh, meshing::reportOnlyThresholds());
                structurallyValid = quality.structurallyValid;
                invalidElements = quality.invalidElements;
                radiusRatio = worstOf(quality, QualityMetric::TetRadiusRatio).value;
            }
            Result<structural::ElementFields> recovered = structural::recoverElementFields(
                meshing::ElementId::fromValue(1), corners, probe.sample(corners), d);
            double strainError = 0.0;
            if (recovered.has_value()) {
                const std::array<double, 6> got{
                    recovered->strain.xx,      recovered->strain.yy,
                    recovered->strain.zz,      recovered->strain.gammaXy,
                    recovered->strain.gammaYz, recovered->strain.gammaZx};
                for (std::size_t i = 0; i < got.size(); ++i) {
                    strainError = std::max(
                        strainError,
                        std::abs(got[i] - probeExpected[i]) / std::abs(probeExpected[i]));
                }
            }
            // `err * sqrt(3r/R)`, the fit constant. Printed so a reader can
            // see it hold rather than take the fit on trust.
            const double fit = radiusRatio > 0.0 ? strainError * std::sqrt(radiusRatio) : 0.0;
            WARN("BOUNDARY | field " << which << " | wedge apex " << apex << " mm | meshBuilt "
                 << (meshBuilt ? "YES" : "NO") << " | structurallyValid "
                 << (structurallyValid ? "YES" : "NO") << " | invalidElements "
                 << invalidElements << " | 3r/R " << radiusRatio << " | kernel "
                 << (recovered.has_value() ? "OK" : "REFUSED") << " | strain error "
                 << strainError << " | err*sqrt(3r/R) " << fit);

            // THE LAW IS PINNED, because a hard-fail threshold is derived
            // from it. `err * sqrt(3r/R)` is measured between 2.83e-16 and
            // 2.41e-15 over sixteen orders of radius ratio and two fields
            // three orders of strain apart, so the band below carries 2.8x
            // below and 4.1x above. A kernel whose gradients had drifted
            // would leave this band while every absolute error still looked
            // small, which is exactly the regression a bound on the error
            // alone would miss.
            REQUIRE(recovered.has_value());
            REQUIRE(fit > 1.0e-16);
            REQUIRE(fit < 1.0e-14);
        }
        }
    }
}

namespace {

/// P16's metrics and P17's verdict for one reference model.
///
/// A GENERIC LAMBDA RATHER THAN A TABLE, because each builder returns its own
/// model type -- `MeshBlockModel`, `MeshCylinderModel` and so on -- so a
/// uniform function-pointer array does not type-check. The duplication at each
/// call site is the price of that, and it is cheaper than erasing the types.
struct Verdict {
    MeshQualityReport quality;
    structural::StructuralValidationReport report;
};

template <typename Built>
[[nodiscard]] Verdict verdictOf(Built built) {
    REQUIRE(built.has_value());
    MeshedReference ref(std::move(built->document));
    const MeshQualityReport quality =
        meshing::evaluateMeshQuality(ref.require().mesh(), meshing::reportOnlyThresholds());
    return Verdict{.quality = quality,
                   .report = structural::validateStructuralMeshQuality(quality)};
}

/// Every qualified reference model's verdict, in a fixed order.
[[nodiscard]] std::vector<std::pair<std::string, Verdict>> allQualifiedVerdicts() {
    std::vector<std::pair<std::string, Verdict>> out;
    out.emplace_back("RM-MESH-01 block", verdictOf(reference::buildMeshBlockReferenceModel()));
    out.emplace_back("RM-MESH-02 cylinder",
                     verdictOf(reference::buildMeshCylinderReferenceModel()));
    out.emplace_back("RM-MESH-03 plate with hole",
                     verdictOf(reference::buildMeshPlateWithHoleReferenceModel()));
    out.emplace_back("RM-MESH-04 hollow tube", verdictOf(reference::buildMeshTubeReferenceModel()));
    out.emplace_back("RM-MESH-05 thin plate",
                     verdictOf(reference::buildMeshThinPlateReferenceModel()));
    out.emplace_back("RM-MESH-06 transformed placed",
                     verdictOf(reference::buildMeshTransformedPlacedReferenceModel()));
    out.emplace_back("RM-MESH-07 local refinement",
                     verdictOf(reference::buildMeshLocalRefinementReferenceModel()));
    return out;
}

} // namespace

TEST_CASE("StructuralValidation_AcceptsEveryQualifiedReferenceMesh",
          "[structural][validation][reference]") {
    // THE TEST THE WHOLE POLICY HAS TO PASS, and the one the brief's warnings
    // are about: "do not turn every imperfect mesh into a hard failure". A
    // structural acceptance policy that refuses a mesh BetterCAD produced and
    // P16 QUALIFIED is refusing BetterCAD's own output, and no amount of
    // engineering justification makes that the right answer.
    //
    // Every one of these meshes contains near-degenerate elements -- the
    // cylinder's worst dihedral is 0.362683 degrees -- and every one is
    // accepted, because the measured accuracy law says the kernel recovers a
    // strain on them to better than 1e-13.
    for (const auto& [name, verdict] : allQualifiedVerdicts()) {
        const Worst radius = worstOf(verdict.quality, QualityMetric::TetRadiusRatio);
        const Worst minDih = worstOf(verdict.quality, QualityMetric::TetMinDihedralAngle);
        INFO("model " << name << ", worst 3r/R " << radius.value << ", worst minDihedral "
                      << degrees(minDih.value) << " deg, verdict "
                      << structural::toString(verdict.report.status()));

        // ACCEPTED. `acceptable()` is what the solve entry point consults.
        REQUIRE(verdict.report.acceptable());

        // AND NOT BY ACCIDENT: the one refusal that exists must not have fired.
        REQUIRE_FALSE(std::ranges::any_of(
            verdict.report.findings, [](const structural::ValidationFinding& found) {
                return found.code == structural::ValidationCode::ElementAccuracyBelowFloor;
            }));

        // THE MARGIN, MEASURED PER MODEL. The floor is 1e-10 and the worst
        // qualified element is 3.22641e-04, so every model clears it by at
        // least five orders. Asserting the MARGIN rather than only the verdict
        // is what would catch a floor quietly raised toward the envelope.
        REQUIRE(radius.value > structural::kStructuralRadiusRatioFloor * 1.0e5);

        // AND NO QUALIFIED MESH WARNS EITHER. The envelope bounds were set to
        // the worst qualified value and rounded away from it, so a warning
        // here would mean the envelope has drifted below something BetterCAD
        // produces -- making the warning mean "BetterCAD made this" instead of
        // anything a user can act on.
        REQUIRE(verdict.report.status() == structural::ValidationStatus::Accepted);
    }
}

TEST_CASE("StructuralValidation_TheEnvelopeBoundsReallyAreTheMeasuredWorstCase",
          "[structural][validation][reference]") {
    // THE OTHER DIRECTION OF THE SAME CLAIM, and the one that keeps the
    // envelope honest. The bounds were derived as "the worst value any
    // qualified reference model exhibits", so each must sit just OUTSIDE the
    // measured worst -- not comfortably clear of it, which would make the
    // warnings fire only on absurd meshes and mean nothing in between.
    double worstRadius = 1.0;
    double worstMinDihedral = 4.0;
    double worstMaxDihedral = 0.0;
    double worstAspect = 1.0;
    for (const auto& [name, verdict] : allQualifiedVerdicts()) {
        worstRadius =
            std::min(worstRadius, worstOf(verdict.quality, QualityMetric::TetRadiusRatio).value);
        worstMinDihedral = std::min(
            worstMinDihedral, worstOf(verdict.quality, QualityMetric::TetMinDihedralAngle).value);
        worstMaxDihedral = std::max(
            worstMaxDihedral, worstOf(verdict.quality, QualityMetric::TetMaxDihedralAngle).value);
        worstAspect =
            std::max(worstAspect, worstOf(verdict.quality, QualityMetric::TetAspectRatio).value);
    }

    const meshing::QualityThresholds policy = structural::structuralQualityThresholds();
    const double radiusBound = policy.limits.at(QualityMetric::TetRadiusRatio).warning.value();
    const double minDihBound =
        policy.limits.at(QualityMetric::TetMinDihedralAngle).warning.value();
    const double maxDihBound =
        policy.limits.at(QualityMetric::TetMaxDihedralAngle).warning.value();
    const double aspectBound = policy.limits.at(QualityMetric::TetAspectRatio).warning.value();

    WARN("ENVELOPE | measured worst 3r/R " << worstRadius << " vs bound " << radiusBound
         << " | minDihedral " << worstMinDihedral << " vs " << minDihBound << " | maxDihedral "
         << worstMaxDihedral << " vs " << maxDihBound << " | aspect " << worstAspect << " vs "
         << aspectBound);

    SECTION("each bound is on the good side of the measured worst, so nothing qualified warns") {
        REQUIRE(worstRadius > radiusBound);
        REQUIRE(worstMinDihedral > minDihBound);
        REQUIRE(worstMaxDihedral < maxDihBound);
        REQUIRE(worstAspect < aspectBound);
    }

    SECTION("and each is TIGHT against it, within a factor of two") {
        // THIS IS THE ASSERTION THAT KEEPS AN ENVELOPE AN ENVELOPE. Without
        // it a bound could be loosened by orders of magnitude and every other
        // test here would still pass -- the warnings would simply stop
        // meaning anything. A factor of two is what the rounding needs and no
        // more.
        REQUIRE(worstRadius < radiusBound * 2.0);
        REQUIRE(worstMinDihedral < minDihBound * 2.0);
        REQUIRE(worstAspect > aspectBound / 2.0);
        // The maximum dihedral approaches pi rather than scaling, so its
        // closeness is a gap and not a ratio.
        REQUIRE(maxDihBound - worstMaxDihedral < 0.05);
    }
}
