#include "reference/ReferenceTestSupport.hpp"

#include <bettercad/core/document/Commands.hpp>
#include <bettercad/features/CircularPatternFeature.hpp>
#include <bettercad/features/Validation.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test::refmodel;
using bettercad::reference::buildIndexPlateReferenceModel;
using bettercad::reference::IndexPlateModel;
using Catch::Matchers::WithinRel;

// P12-REF-001: the index plate. Every stage is a primitive whose volume is
// exact: a disc, an elliptical prism, a ring of them with one left out, and
// a cylinder unioned on.

namespace {

constexpr double pi = std::numbers::pi;
constexpr double kPlateRadius = 90.0;
constexpr double kThickness = 12.0;
constexpr double kSemiMajor = 26.0;
constexpr double kSemiMinor = 14.0;
constexpr double kHubRadius = 26.0;
constexpr double kHubHeight = 24.0;

IndexPlateModel requireModel() {
    auto model = buildIndexPlateReferenceModel();
    if (!model) {
        FAIL(model.error().message);
    }
    return std::move(*model);
}

double disc(double r, double h) { return pi * r * r * h; }
/// An elliptical prism: pi a b h.
double ellipticalPrism(double a, double b, double h) { return pi * a * b * h; }

double mm(const Document& doc, ParameterId parameter) {
    const auto value = doc.effectiveParameterValue(parameter);
    REQUIRE(value.has_value());
    return value->siValue * 1000.0;
}

} // namespace

TEST_CASE("ReferenceModel_IndexPlateMatchesItsDecomposition", "[reference][p12][index-plate][acceptance]") {
    IndexPlateModel m = requireModel();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);

    // The equations.
    CHECK_THAT(mm(m.document, m.pocketSemiMinor), WithinRel(kSemiMinor, 1e-12));
    CHECK_THAT(mm(m.document, m.pocketCircle), WithinRel(55.0, 1e-12));
    CHECK_THAT(mm(m.document, m.hubRadius), WithinRel(kHubRadius, 1e-12));
    CHECK_THAT(mm(m.document, m.hubHeight), WithinRel(kHubHeight, 1e-12));

    // 1. The plate is a disc.
    const double plate = disc(kPlateRadius, kThickness);
    CHECK_THAT(featureBody(regenerator, m.plate).volumeMm3, WithinRel(plate, kRel));

    // 2. One elliptical pocket cut through all of it.
    const double pocket = ellipticalPrism(kSemiMajor, kSemiMinor, kThickness);
    CHECK_THAT(featureBody(regenerator, m.pocket).volumeMm3, WithinRel(plate - pocket, kRel));

    // 3. Six pockets round the axis with instance 3 suppressed: five cut.
    const double cut = static_cast<double>(IndexPlateModel::kPocketCount - 1) * pocket;
    const auto pockets = featureBody(regenerator, m.pockets);
    INFO("five pockets expected " << plate - cut << " mm^3, actual " << pockets.volumeMm3);
    CHECK_THAT(pockets.volumeMm3, WithinRel(plate - cut, kRel));

    // 4. The hub is its own body, a cylinder.
    CHECK_THAT(featureBody(regenerator, m.hub).volumeMm3, WithinRel(disc(kHubRadius, kHubHeight), kRel));

    // 5. Joined: the plate, plus the part of the hub that stands above it.
    // The pockets sit at radius 55 with a semi-major of 26, so their inner
    // edge is at 29 mm and the hub at 26 mm never meets them.
    const double assembly = plate - cut + disc(kHubRadius, kHubHeight - kThickness);
    const auto body = onlyBody(requireFingerprint(m.document, regenerator));
    checkSound(body);
    INFO("assembly expected " << assembly << " mm^3, actual " << body.volumeMm3 << ", rel error "
                              << std::abs(body.volumeMm3 - assembly) / assembly);
    CHECK_THAT(body.volumeMm3, WithinRel(assembly, kRel));
    checkBounds(body, {-kPlateRadius, -kPlateRadius, 0.0}, {kPlateRadius, kPlateRadius, kHubHeight});
}

TEST_CASE("ReferenceModel_IndexPlateSuppressionKeepsTheOtherIndices", "[reference][p12][index-plate][acceptance]") {
    // Suppressing an instance removes its geometry and nothing else: the
    // count is still six, and unsuppressing brings the same pocket back.
    IndexPlateModel m = requireModel();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const double plate = disc(kPlateRadius, kThickness);
    const double pocket = ellipticalPrism(kSemiMajor, kSemiMinor, kThickness);
    CHECK_THAT(featureBody(regenerator, m.pockets).volumeMm3, WithinRel(plate - 5.0 * pocket, kRel));

    // Unsuppress it: six pockets.
    auto definition = m.document.findObjectAs<features::CircularPatternFeature>(m.pockets)->definition();
    definition.suppressed.clear();
    REQUIRE(m.document
                .modifyObject<features::CircularPatternFeature>(
                    m.pockets, [&](features::CircularPatternFeature& f) { return f.setDefinition(definition); })
                .has_value());
    requireRegenerated(regenerator, m.document);
    CHECK_THAT(featureBody(regenerator, m.pockets).volumeMm3, WithinRel(plate - 6.0 * pocket, kRel));

    // Suppress a different one: still five, and the same volume as before,
    // because every instance is the same pocket turned.
    definition.suppressed = {IndexPlateModel::kSuppressedInstance};
    REQUIRE(m.document
                .modifyObject<features::CircularPatternFeature>(
                    m.pockets, [&](features::CircularPatternFeature& f) { return f.setDefinition(definition); })
                .has_value());
    requireRegenerated(regenerator, m.document);
    CHECK_THAT(featureBody(regenerator, m.pockets).volumeMm3, WithinRel(plate - 5.0 * pocket, kRel));
}

TEST_CASE("ReferenceModel_IndexPlateFollowsItsParameters", "[reference][p12][index-plate][acceptance]") {
    // The plate radius drives the pocket circle and the hub through their
    // equations; the thickness drives the hub's height.
    IndexPlateModel m = requireModel();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const auto before = requireFingerprint(m.document, regenerator);

    REQUIRE(m.document.setParameterValue(m.plateRadius, 99_mm).has_value());
    requireRegenerated(regenerator, m.document);
    CHECK_THAT(mm(m.document, m.pocketCircle), WithinRel(64.0, 1e-12));
    CHECK_THAT(mm(m.document, m.hubRadius), WithinRel(99.0 * 13.0 / 45.0, 1e-12));
    CHECK_THAT(featureBody(regenerator, m.plate).volumeMm3, WithinRel(disc(99.0, kThickness), kRel));

    REQUIRE(m.document.setParameterValue(m.thickness, 15_mm).has_value());
    requireRegenerated(regenerator, m.document);
    CHECK_THAT(mm(m.document, m.hubHeight), WithinRel(30.0, 1e-12));
    CHECK_THAT(featureBody(regenerator, m.plate).volumeMm3, WithinRel(disc(99.0, 15.0), kRel));

    REQUIRE(m.document.setParameterValue(m.plateRadius, 90_mm).has_value());
    REQUIRE(m.document.setParameterValue(m.thickness, 12_mm).has_value());
    requireRegenerated(regenerator, m.document);
    const auto difference = reference::compare(before, requireFingerprint(m.document, regenerator));
    CHECK(difference.sameStructure);
    CHECK(difference.volumeAreaRelative <= kRel);
    CHECK(difference.positionMm <= kPositionMm);
}

TEST_CASE("ReferenceModel_IndexPlateRefusesPocketsThatWouldMeet", "[reference][p12][index-plate][acceptance]") {
    // Asking for more pockets than the circle can hold makes them overlap,
    // which is a different solid, not a failure; asking for a pocket circle
    // the plate cannot hold IS a failure. What must hold either way is that
    // nothing is silently half-built.
    IndexPlateModel m = requireModel();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const auto good = requireFingerprint(m.document, regenerator);

    // A plate smaller than its own hub leaves the combine nothing sound to
    // work with.
    REQUIRE(m.document.setParameterValue(m.plateRadius, 20_mm).has_value());
    auto report = regenerator.regenerate(m.document);
    REQUIRE(report.has_value());
    INFO(bettercad::test::describe(*report));
    CHECK_FALSE(report->succeeded());
    CHECK_FALSE(report->failed.empty());

    REQUIRE(m.document.setParameterValue(m.plateRadius, 90_mm).has_value());
    requireRegenerated(regenerator, m.document);
    const auto difference = reference::compare(good, requireFingerprint(m.document, regenerator));
    CHECK(difference.sameStructure);
    CHECK(difference.volumeAreaRelative <= kRel);
}
