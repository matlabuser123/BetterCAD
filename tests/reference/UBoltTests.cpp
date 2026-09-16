#include "reference/ReferenceTestSupport.hpp"

#include <bettercad/features/FeatureCommands.hpp>
#include <bettercad/features/ResultBodies.hpp>
#include <bettercad/features/Validation.hpp>

#include <catch2/matchers/catch_matchers_string.hpp>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using namespace bettercad::test::refmodel;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;
namespace an = bettercad::test::analytic;
using an::pi;

namespace {

/// The U-bolt's dimensions in mm, as the builder sets them.
struct UBoltSize {
    double rod = 5.0;
    double leg = 60.0;
    double bend = 25.0;
    double chamfer = 1.0;
};

/// How far along its feature chain the U-bolt is.
enum class Stage { Swept, Chamfered };

/// What a 45° chamfer of size c takes off a rod end: the triangle in the
/// section, turned about the rod's axis. revolved() gives its volume and how
/// far it reaches along the rod.
an::Solid chamferRing(const UBoltSize& s) {
    // Counter-clockwise in (radius, height), so the area is positive.
    an::Path p({s.rod - s.chamfer, 0.0});
    p.to({s.rod, -s.chamfer}).to({s.rod, 0.0});
    const an::Loop loop = p.close();
    REQUIRE(an::largestGap(loop) < 1e-12);
    return an::revolved({loop});
}

/// The swept rod: a circular section of radius rod_r carried along two legs
/// and a half turn, so V = pi r^2 (2 leg + pi R) and S = 2 pi r (2 leg +
/// pi R) with the two end discs (Pappus for both). The legs' centroid is
/// half way down; the half torus, a disc turned 180°, has its centroid
/// 2 (R^2 + r^2/4) / (pi R) from the bend's axis.
an::Solid expected(const UBoltSize& s, Stage stage) {
    const double rod = pi * s.rod * s.rod;
    const double legs = rod * 2.0 * s.leg;
    const double torus = rod * pi * s.bend;
    const double bendCentroid = 2.0 * (s.bend * s.bend + s.rod * s.rod / 4.0) / (pi * s.bend);
    double volume = legs + torus;
    double momentZ = legs * (-s.leg / 2.0) + torus * (-s.leg - bendCentroid);
    double area = 2.0 * pi * s.rod * (2.0 * s.leg + pi * s.bend) + 2.0 * pi * s.rod * s.rod;
    if (stage >= Stage::Chamfered) {
        const an::Solid ring = chamferRing(s);
        volume -= 2.0 * ring.volume;
        momentZ -= 2.0 * ring.volume * ring.centroid[2];
        // Each end loses a ring of rod wall and a ring of its end disc, and
        // gains the cone the chamfer cuts.
        const double slant = s.chamfer * std::numbers::sqrt2;
        area += 2.0 * (-2.0 * pi * s.rod * s.chamfer - pi * s.rod * s.rod + pi * (s.rod - s.chamfer) * (s.rod - s.chamfer) +
                       pi * (2.0 * s.rod - s.chamfer) * slant);
    }
    return {.volume = volume, .area = area, .centroid = {s.bend, 0.0, momentZ / volume}};
}

std::array<double, 3> lowCorner(const UBoltSize& s) {
    return {-s.rod, -s.rod, -(s.leg + s.bend + s.rod)};
}
std::array<double, 3> highCorner(const UBoltSize& s) {
    return {2.0 * s.bend + s.rod, s.rod, 0.0};
}

reference::UBoltModel buildUBolt() {
    auto built = reference::buildUBoltReferenceModel();
    INFO((built ? std::string{} : built.error().message));
    REQUIRE(built.has_value());
    return std::move(*built);
}

} // namespace

TEST_CASE("ReferenceModel_UBoltBuildsValidGeometry", "[reference][ubolt]") {
    reference::UBoltModel m = buildUBolt();
    const features::ValidationReport validation = features::validateDocument(m.document);
    for (const features::ValidationIssue& issue : validation.issues) {
        INFO(issue.message);
    }
    CHECK(validation.valid());
    CHECK(validation.issues.empty());

    features::Regenerator regenerator;
    const features::RegenerationReport report = requireRegenerated(regenerator, m.document);
    CHECK(report.regenerated == std::vector<ObjectId>{m.rodSection, m.route, m.rod, m.endChamfers});
    CHECK(features::resultFeatures(m.document) == std::vector<ObjectId>{m.endChamfers});
    CHECK(m.document.findObject(m.rod)->typeName() == "sweep");
    for (const ObjectId feature : {m.rod, m.endChamfers}) {
        INFO("feature " << feature);
        checkSound(featureBody(regenerator, feature));
    }
    checkSound(onlyBody(requireFingerprint(m.document, regenerator)));
}

TEST_CASE("ReferenceModel_UBoltMatchesIndependentProperties", "[reference][ubolt][acceptance]") {
    reference::UBoltModel m = buildUBolt();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const UBoltSize s;

    SECTION("the swept rod: V = pi r^2 (2 leg + pi R)") {
        const double path = 2.0 * s.leg + pi * s.bend;
        CHECK_THAT(expected(s, Stage::Swept).volume, WithinRel(pi * s.rod * s.rod * path, 1e-14));
        const reference::BodyFingerprint body = featureBody(regenerator, m.rod);
        checkProperties(body, expected(s, Stage::Swept));
        checkBounds(body, lowCorner(s), highCorner(s));
    }
    SECTION("the chamfered ends") {
        const reference::BodyFingerprint body = onlyBody(requireFingerprint(m.document, regenerator));
        checkProperties(body, expected(s, Stage::Chamfered));
        checkBounds(body, lowCorner(s), highCorner(s));
        // Each chamfer takes a ring of pi c^2 (r - c/3) off its end.
        const double ring = pi * s.chamfer * s.chamfer * (s.rod - s.chamfer / 3.0);
        CHECK_THAT(expected(s, Stage::Swept).volume - 2.0 * ring,
                   WithinRel(expected(s, Stage::Chamfered).volume, 1e-14));
    }
}

TEST_CASE("ReferenceModel_UBoltRegeneratesAfterParameterChanges", "[reference][ubolt][acceptance]") {
    reference::UBoltModel m = buildUBolt();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const reference::ModelFingerprint baseline = requireFingerprint(m.document, regenerator);

    // Longer legs: the bend and the chamfered ends follow the path.
    REQUIRE(m.document.setParameterValue(m.legLength, 80_mm).has_value());
    const features::RegenerationReport report = requireRegenerated(regenerator, m.document);
    CHECK(report.regenerated == std::vector<ObjectId>{m.route, m.rod, m.endChamfers});
    UBoltSize s;
    s.leg = 80.0;
    const reference::BodyFingerprint body = onlyBody(requireFingerprint(m.document, regenerator));
    checkProperties(body, expected(s, Stage::Chamfered));
    checkBounds(body, lowCorner(s), highCorner(s));
    // The ends are still chamfered at z = 0, and the bend has moved down.
    const geometry::Body& bolt = *regenerator.body(m.endChamfers);
    CHECK(circleEdges(bolt, {0.0, 0.0, 0.0}, Direction3D::unitZ(), s.rod - s.chamfer) == 1);
    CHECK(circleEdges(bolt, {2.0 * s.bend, 0.0, 0.0}, Direction3D::unitZ(), s.rod - s.chamfer) == 1);

    // Back again: the original bolt, to rounding.
    REQUIRE(m.document.setParameterValue(m.legLength, 60_mm).has_value());
    requireRegenerated(regenerator, m.document);
    const reference::FingerprintDifference back =
        reference::compare(baseline, requireFingerprint(m.document, regenerator));
    INFO("restored: volume and area " << back.volumeAreaRelative << " relative, positions " << back.positionMm
                                      << " mm");
    CHECK(back.sameStructure);
    CHECK(back.volumeAreaRelative <= 1e-12);
    CHECK(back.positionMm <= 1e-9);
}

TEST_CASE("ReferenceModel_UBoltRegeneratesDeterministically", "[reference][ubolt]") {
    reference::UBoltModel m = buildUBolt();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const reference::ModelFingerprint baseline = requireFingerprint(m.document, regenerator);
    checkRepeatedRegeneration(m.document, baseline, 10);
    for (int i = 0; i < 3; ++i) {
        reference::UBoltModel again = buildUBolt();
        features::Regenerator fresh;
        requireRegenerated(fresh, again.document);
        CHECK(requireFingerprint(again.document, fresh) == baseline);
    }
}

TEST_CASE("ReferenceModel_UBoltSaveLoadAndExports", "[reference][ubolt][io][export]") {
    reference::UBoltModel m = buildUBolt();
    TempDir dir;
    checkSaveLoad(m.document, dir.path() / "u_bolt.bcad");
    const UBoltSize s;
    const an::Solid finished = expected(s, Stage::Chamfered);
    checkStepExport(m.document, dir.path() / "u_bolt.step", finished.volume, lowCorner(s), highCorner(s));
    checkStlExport(m.document, dir.path() / "u_bolt.stl", finished.volume, finished.area);
}

TEST_CASE("ReferenceModel_UBoltUndoRedoAndFailure", "[reference][ubolt]") {
    reference::UBoltModel m = buildUBolt();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const reference::ModelFingerprint baseline = requireFingerprint(m.document, regenerator);
    const Document original = m.document.clone();
    CommandHistory history;

    execute(history, m.document, ModifyParameterCommand::setValue(m.chamferSize, 1.5_mm));
    requireRegenerated(regenerator, m.document);
    UBoltSize s;
    s.chamfer = 1.5;
    const reference::ModelFingerprint changed = requireFingerprint(m.document, regenerator);
    checkProperties(onlyBody(changed), expected(s, Stage::Chamfered));
    REQUIRE(history.undo(m.document).has_value());
    CHECK(equivalent(m.document, original));
    requireRegenerated(regenerator, m.document);
    CHECK(requireFingerprint(m.document, regenerator) == baseline);
    REQUIRE(history.redo(m.document).has_value());
    requireRegenerated(regenerator, m.document);
    CHECK(requireFingerprint(m.document, regenerator) == changed);
    REQUIRE(history.undo(m.document).has_value());
    requireRegenerated(regenerator, m.document);

    // A chamfer wider than the rod is refused before the kernel runs.
    REQUIRE(m.document.setParameterValue(m.chamferSize, 6_mm).has_value());
    auto report = regenerator.regenerate(m.document);
    REQUIRE(report.has_value());
    CHECK(report->failed == std::vector<ObjectId>{m.endChamfers});
    const Error& error = report->errors.at(m.endChamfers);
    INFO(error.message);
    CHECK(error.code == ErrorCode::FailedPrecondition);
    CHECK_THAT(error.message, ContainsSubstring("EndChamfers"));
    CHECK(regenerator.body(m.endChamfers) == nullptr);
    checkProperties(featureBody(regenerator, m.rod), expected(UBoltSize{}, Stage::Swept));

    REQUIRE(m.document.setParameterValue(m.chamferSize, 1_mm).has_value());
    CHECK(equivalent(m.document, original));
    requireRegenerated(regenerator, m.document);
    CHECK(requireFingerprint(m.document, regenerator) == baseline);
}
