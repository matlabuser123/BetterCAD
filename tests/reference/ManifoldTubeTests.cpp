#include "reference/ReferenceTestSupport.hpp"

#include <bettercad/core/document/Commands.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test::refmodel;
using bettercad::reference::buildManifoldTubeReferenceModel;
using bettercad::reference::ManifoldTubeModel;
using Catch::Matchers::WithinRel;

// P12-REF-001: the swept manifold tube. Its volume is Pappus's: the section's
// area times the distance its centroid travels, which is why the section is
// built centred on the path.

namespace {

constexpr double pi = std::numbers::pi;

/// A twisted sweep is built from an auxiliary spine the kernel fits through
/// samples (P12-SWEEP-001), so its volume is not exact. Measured on this
/// model against Pappus: 5.6e-16 at no twist, 1.5e-7 at 45 deg, 4.0e-8 at
/// 90 deg and 2.5e-8 at 180 deg. 1e-6 covers the worst of them with room,
/// and is far below any wrong path, which would be out by per cent.
constexpr double kRelTwisted = 1e-6;

ManifoldTubeModel requireModel() {
    auto model = buildManifoldTubeReferenceModel();
    if (!model) {
        FAIL(model.error().message);
    }
    return std::move(*model);
}

/// Pappus: the square's area times the centroid's path -- the drop, then a
/// quarter turn of each bend radius.
double pappus(double sideMm, double dropMm, double firstBendMm) {
    const double secondBend = firstBendMm * 5.0 / 6.0;
    return sideMm * sideMm * (dropMm + (pi / 2.0) * (firstBendMm + secondBend));
}

double mm(const Document& doc, ParameterId parameter) {
    const auto value = doc.effectiveParameterValue(parameter);
    REQUIRE(value.has_value());
    return value->siValue * 1000.0;
}

} // namespace

TEST_CASE("ReferenceModel_ManifoldTubeMatchesPappus", "[reference][p12][manifold][acceptance]") {
    ManifoldTubeModel m = requireModel();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);

    // The equation: the second bend follows the first.
    CHECK_THAT(mm(m.document, m.secondBend), WithinRel(25.0, 1e-12));

    const double expected = pappus(12.0, 50.0, 30.0);
    const auto tube = featureBody(regenerator, m.tube);
    checkSound(tube);
    INFO("Pappus expected " << expected << " mm^3, actual " << tube.volumeMm3 << ", rel error "
                            << std::abs(tube.volumeMm3 - expected) / expected);
    CHECK_THAT(tube.volumeMm3, WithinRel(expected, kRelTwisted));

    // The mounting flange above the inlet: a square plate meeting the tube
    // on the plane z = 0, so it simply adds its own box.
    const auto body = onlyBody(requireFingerprint(m.document, regenerator));
    checkSound(body);
    CHECK_THAT(body.volumeMm3 - tube.volumeMm3, WithinRel(40.0 * 40.0 * 12.0, kRel));

    // The path ends where the geometry says: the drop and the first bend
    // take it to z = -(50 + 30), and the two bends to x = 30 + 25.
    CHECK(body.minMm[2] < -79.0);
    CHECK(body.maxMm[0] > 54.0);
}

TEST_CASE("ReferenceModel_ManifoldTubeTwistIsWhatCostsTheExactness",
          "[reference][p12][manifold][acceptance]") {
    // The measurement behind kRelTwisted, kept as a test so it cannot drift
    // unnoticed. The path leaves any one plane in every case, so what the
    // numbers separate is the TWIST, not the spatial path: with no twist the
    // kernel meets Pappus to rounding, and with one it does not, because a
    // twisted sweep rides an auxiliary spine fitted through samples.
    const double expected = pappus(12.0, 50.0, 30.0);
    struct Case {
        double twistDeg;
        double bound;
    };
    // Each bound is the measured error rounded up one significant figure.
    for (const Case& c : {Case{0.0, 1e-14}, Case{45.0, 2e-7}, Case{90.0, 5e-8}, Case{180.0, 3e-8}}) {
        DYNAMIC_SECTION("twist " << c.twistDeg << " deg") {
            ManifoldTubeModel m = requireModel();
            features::Regenerator regenerator;
            REQUIRE(m.document.setParameterValue(m.twist, c.twistDeg * units::deg).has_value());
            requireRegenerated(regenerator, m.document);
            const auto body = featureBody(regenerator, m.tube);
            checkSound(body);
            const double error = std::abs(body.volumeMm3 - expected) / expected;
            INFO("twist " << c.twistDeg << " deg: volume " << body.volumeMm3 << ", rel error " << error);
            CHECK(error <= c.bound);
        }
    }
}

TEST_CASE("ReferenceModel_ManifoldTubeFollowsItsParameters", "[reference][p12][manifold][acceptance]") {
    // Lengthening the drop and widening the first bend both move the path,
    // and the second bend follows the first through its equation.
    ManifoldTubeModel m = requireModel();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const auto before = requireFingerprint(m.document, regenerator);

    REQUIRE(m.document.setParameterValue(m.drop, 70_mm).has_value());
    requireRegenerated(regenerator, m.document);
    CHECK_THAT(featureBody(regenerator, m.tube).volumeMm3, WithinRel(pappus(12.0, 70.0, 30.0), kRelTwisted));

    REQUIRE(m.document.setParameterValue(m.firstBend, 36_mm).has_value());
    requireRegenerated(regenerator, m.document);
    CHECK_THAT(mm(m.document, m.secondBend), WithinRel(30.0, 1e-12));
    CHECK_THAT(featureBody(regenerator, m.tube).volumeMm3, WithinRel(pappus(12.0, 70.0, 36.0), kRelTwisted));

    // Restored, the model comes back.
    REQUIRE(m.document.setParameterValue(m.drop, 50_mm).has_value());
    REQUIRE(m.document.setParameterValue(m.firstBend, 30_mm).has_value());
    requireRegenerated(regenerator, m.document);
    const auto difference = reference::compare(before, requireFingerprint(m.document, regenerator));
    CHECK(difference.sameStructure);
    CHECK(difference.volumeAreaRelative <= kRelTwisted);
}

TEST_CASE("ReferenceModel_ManifoldTubeRefusesAPathItCannotSweep", "[reference][p12][manifold][acceptance]") {
    // A bend tighter than the section's half-diagonal folds the sweep over
    // itself. The failure must be structured, leave no body, and be
    // recoverable.
    ManifoldTubeModel m = requireModel();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const auto good = requireFingerprint(m.document, regenerator);

    REQUIRE(m.document.setParameterValue(m.firstBend, 1_mm).has_value());
    auto report = regenerator.regenerate(m.document);
    REQUIRE(report.has_value());
    INFO(bettercad::test::describe(*report));
    CHECK_FALSE(report->succeeded());
    CHECK(report->failed == std::vector<ObjectId>{m.tube});
    CHECK(regenerator.body(m.tube) == nullptr);
    CHECK_FALSE(report->errors.at(m.tube).message.empty());

    REQUIRE(m.document.setParameterValue(m.firstBend, 30_mm).has_value());
    requireRegenerated(regenerator, m.document);
    const auto difference = reference::compare(good, requireFingerprint(m.document, regenerator));
    CHECK(difference.sameStructure);
    CHECK(difference.volumeAreaRelative <= kRelTwisted);
}

TEST_CASE("ReferenceModel_ManifoldTubeFollowsItsSection", "[reference][p12][manifold][acceptance]") {
    // REGRESSION (P12-REF-001 adversarial review). `side` drove the square
    // section's edge length, but the two constraints that CENTRE the section
    // on the path were written as the literal 6 mm -- half the side it was
    // authored at. At any other side the centroid slid off the path, which
    // silently costs the model the very property its comment claims and this
    // file's validation rests on: that the centroid rides the path, so
    // Pappus gives the volume exactly. The centring is now `side / 2`.
    //
    // No test drove `side` before, so nothing noticed. This one does, and
    // checks the Pappus identity still holds at the new size.
    // Both sides are ones the kernel can sweep: a square of half-diagonal
    // side/sqrt2 twisting 90 degrees through the 25 mm second bend. At 18 mm
    // the twisted envelope no longer fits it and the sweep fails -- which
    // ManifoldTubeRefusesAPathItCannotSweep covers, and which is a limit of
    // the path, not of the centring this test is about.
    for (const double side : {8.0, 15.0}) {
        DYNAMIC_SECTION("side " << side << " mm") {
            ManifoldTubeModel m = requireModel();
            REQUIRE(m.document.setParameterValue(m.side, side * units::mm).has_value());
            features::Regenerator regenerator;
            requireRegenerated(regenerator, m.document);

            // Still centred: half the side on each of the two constraints.
            CHECK_THAT(mm(m.document, m.halfSide), WithinRel(side / 2.0, 1e-12));
            // The flange follows `side` by its own equations.
            CHECK_THAT(mm(m.document, m.flangeSide), WithinRel(side * 10.0 / 3.0, 1e-12));
            CHECK_THAT(mm(m.document, m.halfFlange), WithinRel(side * 10.0 / 6.0, 1e-12));

            // Pappus on the swept run: a centred section of area side^2
            // carried along the path its centroid rides.
            const auto tube = featureBody(regenerator, m.tube);
            checkSound(tube);
            const double expected = pappus(side, 50.0, 30.0);
            INFO("Pappus expects " << expected << " mm^3, actual " << tube.volumeMm3 << ", rel error "
                                   << std::abs(tube.volumeMm3 - expected) / expected);
            CHECK_THAT(tube.volumeMm3, WithinRel(expected, kRelTwisted));
        }
    }
}
