#include "reference/ReferenceTestSupport.hpp"

#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/standards/ClearanceHoles.hpp>
#include <bettercad/core/standards/MetricThreads.hpp>
#include <bettercad/features/DatumCommands.hpp>
#include <bettercad/features/Datums.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <memory>
#include <cstddef>
#include <numbers>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test::refmodel;
using bettercad::reference::buildRibbedBracketReferenceModel;
using bettercad::reference::RibbedBracketModel;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// P12-REF-001: the ribbed angle bracket. Every stage is a primitive with an
// exact volume, the variable fillet included: a linear radius law over a
// straight edge integrates in closed form (see linearFillet below).

namespace {

constexpr double pi = std::numbers::pi;
constexpr double kWidth = 90.0;
constexpr double kBaseDepth = 70.0;
constexpr double kThickness = 10.0;
constexpr double kWallHeight = 65.0;

RibbedBracketModel requireModel() {
    auto model = buildRibbedBracketReferenceModel();
    if (!model) {
        FAIL(model.error().message);
    }
    return std::move(*model);
}

/// The base plate.
double base(double width, double depth, double thickness) { return width * depth * thickness; }

/// The base and the upright joined. The upright stands BEHIND the base's
/// front edge (the XZ sketch's normal points to -Y), so the two meet on the
/// plane y = 0 and share no volume: the union is simply their sum.
double bracket(double width, double depth, double thickness, double wallHeight) {
    return base(width, depth, thickness) + width * thickness * wallHeight;
}

/// The gusset: a triangle bounded by the wall's face, the base's top face
/// and the profile line, extruded to the rib's thickness.
double rib(double reach, double thickness, double toe, double ribThickness) {
    return 0.5 * toe * (reach - thickness) * ribThickness;
}

/// The fillet's cross-section between two faces at right angles is
/// r^2 (1 - pi/4). A CONSTANT fillet of radius r over an edge of length L
/// therefore removes L r^2 (1 - pi/4).
double constantFillet(double radius, double length) {
    return length * radius * radius * (1.0 - pi / 4.0);
}

/// A fillet whose radius runs LINEARLY from r0 to r1 along an edge of length
/// L removes the integral of that cross-section:
///
///   V = (1 - pi/4) int_0^L r(s)^2 ds,   r(s) = r0 + (r1 - r0) s/L
///     = (1 - pi/4) L (r0^2 + r0 r1 + r1^2) / 3
///
/// which is what separates a variable fillet from a constant one: at 2 -> 4
/// mm this is 9.33 L (1 - pi/4), against 16 L (1 - pi/4) for a constant 4 mm
/// and 4 L (1 - pi/4) for a constant 2 mm. Nothing here is measured from
/// BetterCAD; it is the integral of the same cross-section the constant case
/// uses.
double linearFillet(double r0, double r1, double length) {
    return (1.0 - pi / 4.0) * length * (r0 * r0 + r0 * r1 + r1 * r1) / 3.0;
}

/// The filleted edge is NOT the whole height of the upright. The base fills
/// the corner from z = 0 to z = thickness, so the convex edge where the
/// upright's face meets its end only exists above the base: it runs from
/// z = thickness to z = wall_h, i.e. wall_h - thickness long.
constexpr double kFilletEdge = kWallHeight - kThickness;

/// The closed form above describes the PRISMATIC part of the blend. At each
/// end OCCT terminates the variable blend into the face it runs out on,
/// which removes a little more material than a square-ended prism would.
/// Measured at 111.135 mm^3 against 110.162 mm^3, i.e. 8.8e-3 -- so 1.2e-2
/// holds it while still separating the linear law (110) from a constant 4 mm
/// fillet (189) and a constant 2 mm one (47) by a factor of 1.7 and 2.3.
constexpr double kRelFilletEnds = 1.2e-2;

double mm(const Document& doc, ParameterId parameter) {
    const auto value = doc.effectiveParameterValue(parameter);
    REQUIRE(value.has_value());
    return value->siValue * 1000.0;
}

} // namespace

TEST_CASE("ReferenceModel_RibbedBracketMatchesItsDecomposition", "[reference][p12][bracket][acceptance]") {
    RibbedBracketModel m = requireModel();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);

    // The equations.
    CHECK_THAT(mm(m.document, m.ribThickness), WithinRel(6.0, 1e-12));
    CHECK_THAT(mm(m.document, m.ribReach), WithinRel(40.0, 1e-12));
    CHECK_THAT(mm(m.document, m.ribToe), WithinRel(50.0, 1e-12));
    CHECK_THAT(mm(m.document, m.cornerTop), WithinRel(4.0, 1e-12));

    // 1. The base is a box.
    const double plate = base(kWidth, kBaseDepth, kThickness);
    CHECK_THAT(featureBody(regenerator, m.base).volumeMm3, WithinRel(plate, kRel));

    // 2. The upright joined to it: two boxes meeting on a plane.
    const double joined = bracket(kWidth, kBaseDepth, kThickness, kWallHeight);
    const auto wall = featureBody(regenerator, m.wall);
    INFO("bracket expected " << joined << " mm^3, actual " << wall.volumeMm3);
    CHECK_THAT(wall.volumeMm3, WithinRel(joined, kRel));

    // 3. The gusset: a triangular prism.
    const double gusset = rib(40.0, kThickness, 50.0, 6.0);
    const auto ribbed = featureBody(regenerator, m.rib);
    INFO("rib expected " << gusset << " mm^3, actual " << ribbed.volumeMm3 - wall.volumeMm3);
    CHECK_THAT(ribbed.volumeMm3 - wall.volumeMm3, WithinRel(gusset, kRel));

    // 4. The clearance hole: its diameter comes from ISO 273, which the
    // standards layer gives, not from a number written here.
    const auto bolt = standards::parseMetricThread("M8");
    REQUIRE(bolt.has_value());
    const double diameter =
        standards::clearanceHoleDiameter(*bolt, standards::ClearanceSeries::Medium).in(units::mm);
    INFO("ISO 273 medium clearance for M8 is " << diameter << " mm");
    const double bore = pi * (diameter / 2.0) * (diameter / 2.0) * kThickness;
    const auto drilled = featureBody(regenerator, m.boltHole);
    CHECK_THAT(ribbed.volumeMm3 - drilled.volumeMm3, WithinRel(bore, kRel));

    // 5. The variable fillet, 2 mm at the bottom of the corner edge to
    // corner_top_r at the top, over the wall_h - thickness the edge actually
    // spans. Checked against the integral of the cross-section, which is
    // what makes this a test of a VARIABLE fillet: an envelope between the
    // two constant radii would admit a constant 4 mm fillet, since r^2
    // scaling makes that envelope a factor of four wide.
    const auto rounded = featureBody(regenerator, m.cornerRound);
    checkSound(rounded);
    const double removed = drilled.volumeMm3 - rounded.volumeMm3;
    const double linear = linearFillet(2.0, 0.4 * kThickness, kFilletEdge);
    INFO("fillet removed " << removed << " mm^3, linear law " << linear << " mm^3, rel error "
                           << std::abs(removed - linear) / linear << "; a constant 4 mm fillet would remove "
                           << constantFillet(4.0, kFilletEdge) << " and a constant 2 mm one "
                           << constantFillet(2.0, kFilletEdge));
    CHECK_THAT(removed, WithinRel(linear, kRelFilletEnds));
    // And it is decisively neither constant fillet.
    CHECK(removed < 0.75 * constantFillet(4.0, kFilletEdge));
    CHECK(removed > 1.5 * constantFillet(2.0, kFilletEdge));

    // The whole part.
    const auto body = onlyBody(requireFingerprint(m.document, regenerator));
    CHECK(body.volumeMm3 == rounded.volumeMm3);
    checkBounds(body, {0.0, -kThickness, 0.0}, {kWidth, kBaseDepth, kWallHeight});
}

TEST_CASE("ReferenceModel_RibbedBracketFollowsItsParameters", "[reference][p12][bracket][acceptance]") {
    // The wall height drives the gusset's reach and the base depth its toe,
    // so raising the upright makes the stiffener grow with it.
    RibbedBracketModel m = requireModel();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const auto before = requireFingerprint(m.document, regenerator);

    REQUIRE(m.document.setParameterValue(m.wallHeight, 85_mm).has_value());
    requireRegenerated(regenerator, m.document);
    CHECK_THAT(mm(m.document, m.ribReach), WithinRel(60.0, 1e-12));
    const double joined = bracket(kWidth, kBaseDepth, kThickness, 85.0);
    CHECK_THAT(featureBody(regenerator, m.wall).volumeMm3, WithinRel(joined, kRel));
    CHECK_THAT(featureBody(regenerator, m.rib).volumeMm3 - joined,
               WithinRel(rib(60.0, kThickness, 50.0, 6.0), kRel));

    REQUIRE(m.document.setParameterValue(m.thickness, 12_mm).has_value());
    requireRegenerated(regenerator, m.document);
    CHECK_THAT(mm(m.document, m.ribThickness), WithinRel(7.2, 1e-12));
    CHECK_THAT(mm(m.document, m.cornerTop), WithinRel(4.8, 1e-12));
    CHECK_THAT(featureBody(regenerator, m.base).volumeMm3, WithinRel(base(kWidth, kBaseDepth, 12.0), kRel));

    REQUIRE(m.document.setParameterValue(m.wallHeight, 65_mm).has_value());
    REQUIRE(m.document.setParameterValue(m.thickness, 10_mm).has_value());
    requireRegenerated(regenerator, m.document);
    const auto difference = reference::compare(before, requireFingerprint(m.document, regenerator));
    CHECK(difference.sameStructure);
    CHECK(difference.volumeAreaRelative <= kRel);
    CHECK(difference.positionMm <= kPositionMm);
}

TEST_CASE("ReferenceModel_RibbedBracketRefusesAnUnsafeFillet", "[reference][p12][bracket][acceptance]") {
    // A corner radius the faces cannot carry is refused before the kernel
    // runs (P12-FEAT-006), atomically, and the model recovers.
    RibbedBracketModel m = requireModel();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const auto good = requireFingerprint(m.document, regenerator);

    REQUIRE(m.document.setParameterValue(m.cornerBottom, 40_mm).has_value());
    auto report = regenerator.regenerate(m.document);
    REQUIRE(report.has_value());
    INFO(bettercad::test::describe(*report));
    CHECK_FALSE(report->succeeded());
    CHECK(report->failed == std::vector<ObjectId>{m.cornerRound});
    CHECK(regenerator.body(m.cornerRound) == nullptr);
    CHECK_THAT(report->errors.at(m.cornerRound).message,
               Catch::Matchers::ContainsSubstring("does not fit"));
    // The feature below it is untouched: a failure consumes nothing.
    CHECK(regenerator.body(m.boltHole) != nullptr);

    REQUIRE(m.document.setParameterValue(m.cornerBottom, 2_mm).has_value());
    requireRegenerated(regenerator, m.document);
    const auto difference = reference::compare(good, requireFingerprint(m.document, regenerator));
    CHECK(difference.sameStructure);
    CHECK(difference.volumeAreaRelative <= kRel);
}


TEST_CASE("ReferenceModel_RibbedBracketFollowsItsWidth", "[reference][p12][bracket][acceptance]") {
    // REGRESSION (P12-REF-001 adversarial review). `width` was declared and
    // drove the two sketches, but two things were frozen to the width it was
    // authored at: the gusset's sketch plane (a frame at x = 45, i.e.
    // width/2) and the filleted edge (a line signature at x = 90, i.e.
    // width). Widening the bracket left the rib off-centre and the fillet's
    // edge reference matching nothing. The rib now sits on a datum driven by
    // width/2, and the filleted corner is the one at x = 0, which no
    // parameter moves.
    for (const double width : {70.0, 120.0}) {
        DYNAMIC_SECTION("width " << width << " mm") {
            RibbedBracketModel m = requireModel();
            REQUIRE(m.document.setParameterValue(m.width, width * units::mm).has_value());
            features::Regenerator regenerator;
            requireRegenerated(regenerator, m.document);

            CHECK_THAT(mm(m.document, m.halfWidth), WithinRel(width / 2.0, 1e-12));
            // The two plates follow the width exactly.
            CHECK_THAT(featureBody(regenerator, m.base).volumeMm3,
                       WithinRel(base(width, kBaseDepth, kThickness), kRel));
            CHECK_THAT(featureBody(regenerator, m.wall).volumeMm3,
                       WithinRel(bracket(width, kBaseDepth, kThickness, kWallHeight), kRel));
            // The rib is unchanged in size -- it spans the corner, not the
            // width -- and still central, which the bounds show.
            const auto ribbed = featureBody(regenerator, m.rib);
            CHECK_THAT(ribbed.volumeMm3 - featureBody(regenerator, m.wall).volumeMm3,
                       WithinRel(rib(40.0, kThickness, 50.0, 6.0), kRel));
            // The fillet still finds its edge and removes the same amount:
            // the corner it rounds does not depend on the width.
            const auto drilled = featureBody(regenerator, m.boltHole);
            const auto rounded = featureBody(regenerator, m.cornerRound);
            checkSound(rounded);
            CHECK_THAT(drilled.volumeMm3 - rounded.volumeMm3,
                       WithinRel(linearFillet(2.0, 0.4 * kThickness, kFilletEdge), kRelFilletEnds));
            checkBounds(rounded, {0.0, -kThickness, 0.0}, {width, kBaseDepth, kWallHeight});
        }
    }
}

TEST_CASE("ReferenceModel_RibbedBracketIsBuiltOnItsFrame", "[reference][p12][bracket][references]") {
    // REGRESSION (P12-REF-001 adversarial review). MountFrame was created,
    // documented as the datum "so the whole bracket can be moved by moving
    // one datum", and then never referenced: both sketches were drawn on
    // bare principal frames and the rib on a frame frozen at x = 45, so
    // moving the frame moved nothing and no test would have noticed.
    //
    // The two sketches and the rib's datum now hang off the frame, and this
    // test proves it by moving the frame and watching the SOLID geometry
    // follow. It also records, as an executable fact, exactly how far that
    // goes: the features placed by GEOMETRIC references -- the bolt hole on
    // a plane signature, the fillet on an edge signature -- do NOT follow a
    // moved datum, because a signature names a plane in model space, not a
    // face of a part. They fail, loudly and atomically, which is the
    // documented contract (see geometry::FaceSignature). Following a moved
    // datum would need semantic face naming for holes and fillets, which is
    // not in this milestone.
    RibbedBracketModel m = requireModel();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const auto plateBefore = featureBody(regenerator, m.base);
    const auto ribBefore = featureBody(regenerator, m.rib);
    const auto objectsBefore = m.document.objectCount();

    constexpr std::array shift{25.0, -15.0, 40.0};
    CommandHistory history;
    REQUIRE(history
                .execute(m.document,
                         std::make_unique<features::ModifyCoordinateSystemCommand>(
                             m.frame, features::CoordinateSystemDefinition{
                                          .kind = features::CoordinateSystemKind::Offset,
                                          .translation = {25_mm, -15_mm, 40_mm}}))
                .has_value());
    auto report = regenerator.regenerate(m.document);
    REQUIRE(report.has_value());
    INFO(bettercad::test::describe(*report));

    // What is attached to the frame moved with it, rigidly and exactly.
    const auto plateAfter = featureBody(regenerator, m.base);
    const auto ribAfter = featureBody(regenerator, m.rib);
    CHECK_THAT(plateAfter.volumeMm3, WithinRel(plateBefore.volumeMm3, kRel));
    CHECK_THAT(ribAfter.volumeMm3, WithinRel(ribBefore.volumeMm3, kRel));
    for (std::size_t i = 0; i < 3; ++i) {
        INFO("axis " << i);
        CHECK_THAT(plateAfter.minMm[i], WithinAbs(plateBefore.minMm[i] + shift[i], kPositionMm));
        CHECK_THAT(plateAfter.maxMm[i], WithinAbs(plateBefore.maxMm[i] + shift[i], kPositionMm));
        CHECK_THAT(plateAfter.centroidMm[i], WithinAbs(plateBefore.centroidMm[i] + shift[i], kPositionMm));
        CHECK_THAT(ribAfter.centroidMm[i], WithinAbs(ribBefore.centroidMm[i] + shift[i], kPositionMm));
    }

    // And what is placed geometrically did not: it failed, said so, and left
    // nothing stale behind.
    CHECK_FALSE(report->succeeded());
    CHECK_FALSE(report->failed.empty());
    CHECK(regenerator.body(m.cornerRound) == nullptr);
    CHECK(m.document.objectCount() == objectsBefore);

    // Undo puts the frame, and the whole bracket, back exactly.
    REQUIRE(history.undo(m.document).has_value());
    requireRegenerated(regenerator, m.document);
    const auto back = featureBody(regenerator, m.base);
    CHECK_THAT(back.volumeMm3, WithinRel(plateBefore.volumeMm3, kRel));
    for (std::size_t i = 0; i < 3; ++i) {
        INFO("axis " << i);
        CHECK_THAT(back.minMm[i], WithinAbs(plateBefore.minMm[i], kPositionMm));
        CHECK_THAT(back.maxMm[i], WithinAbs(plateBefore.maxMm[i], kPositionMm));
    }
}
