#include "reference/ReferenceTestSupport.hpp"

#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/standards/MetricThreads.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <numbers>
#include <string>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test::refmodel;
using bettercad::reference::buildGearboxCoverReferenceModel;
using bettercad::reference::GearboxCoverModel;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// P12-REF-001: the cast gearbox cover. Draft and shell have no single closed
// form together, so the model is validated by decomposition: each stage
// against the geometry that stage adds or removes, computed here by hand.

namespace {

constexpr double pi = std::numbers::pi;

GearboxCoverModel requireModel() {
    auto model = buildGearboxCoverReferenceModel();
    if (!model) {
        FAIL(model.error().message);
    }
    return std::move(*model);
}

/// A box L x W x H drafted on all four sides by `angle` about z = 0, so the
/// section at height z is inset by z tan(angle) on each side:
///
///   V = int_0^H (L - 2 z t)(W - 2 z t) dz
///     = LWH - t (L + W) H^2 + (4/3) t^2 H^3,   t = tan(angle)
double draftedBox(double l, double w, double h, double angleDeg) {
    const double t = std::tan(angleDeg * pi / 180.0);
    return l * w * h - t * (l + w) * h * h + (4.0 / 3.0) * t * t * h * h * h;
}

/// The cavity a shell of thickness `t` leaves inside that drafted box, open
/// at the parting face.
///
/// Each of the four sides is offset inward along ITS OWN normal, which for a
/// face tilted by `angle` from the vertical moves it t/cos(angle)
/// horizontally; the flat top is offset straight down by t. So the cavity is
/// bounded by four planes of the SAME draft angle and a flat top, i.e. it is
/// another drafted box -- one whose plan is inset by t/cos(angle) on each
/// side and whose height is t shorter:
///
///   cavity = draftedBox(L - 2t/cos a, W - 2t/cos a, H - t, a)
///
/// so the shell leaves draftedBox(L,W,H,a) - cavity behind. This is a
/// derivation, not a measurement: the shell is the dominant feature of the
/// part and is worth an exact expectation.
double shelledDraftedBox(double l, double w, double h, double angleDeg, double t) {
    const double inset = 2.0 * t / std::cos(angleDeg * pi / 180.0);
    return draftedBox(l, w, h, angleDeg) - draftedBox(l - inset, w - inset, h - t, angleDeg);
}

double mm(const Document& doc, ParameterId parameter) {
    const auto value = doc.effectiveParameterValue(parameter);
    REQUIRE(value.has_value());
    return value->siValue * 1000.0;
}

} // namespace

TEST_CASE("ReferenceModel_GearboxCoverMatchesItsDecomposition", "[reference][p12][gearbox-cover][acceptance]") {
    GearboxCoverModel m = requireModel();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);

    // The equations, against the definitions written out.
    CHECK_THAT(mm(m.document, m.wall), WithinRel(80.0 / 20.0, 1e-12));
    CHECK_THAT(mm(m.document, m.portDiameter), WithinRel(80.0 * 0.3, 1e-12));
    CHECK_THAT(mm(m.document, m.fixingPitch), WithinRel(120.0 / 3.0, 1e-12));

    // 1. The block is an exact box.
    const auto block = featureBody(regenerator, m.block);
    INFO("block expected 384000 mm^3, actual " << block.volumeMm3);
    CHECK_THAT(block.volumeMm3, WithinRel(120.0 * 80.0 * 40.0, kRel));

    // 2. The draft is the integral above: 3 deg on all four sides.
    const double drafted = draftedBox(120.0, 80.0, 40.0, 3.0);
    const auto draft = featureBody(regenerator, m.draft);
    INFO("drafted expected " << drafted << " mm^3, actual " << draft.volumeMm3 << ", rel error "
                             << std::abs(draft.volumeMm3 - drafted) / drafted);
    CHECK_THAT(draft.volumeMm3, WithinRel(drafted, kRel));
    // The draft takes material away going up, and leaves the parting face.
    CHECK(draft.volumeMm3 < block.volumeMm3);
    // The cover hangs below its outside face at z = 0 (see GearboxCover.cpp).
    checkBounds(draft, {0.0, 0.0, -40.0}, {120.0, 80.0, 0.0});

    // 3. The shell hollows it, and its volume is exact: the cavity is
    // another drafted box (see shelledDraftedBox above). This is the
    // dominant feature of the part -- it removes about a fifth of the
    // material -- so it is pinned to a closed form, not merely bounded.
    const auto shell = featureBody(regenerator, m.shell);
    checkSound(shell);
    const double hollow = shelledDraftedBox(120.0, 80.0, 40.0, 3.0, 4.0);
    INFO("shell expected " << hollow << " mm^3, actual " << shell.volumeMm3 << ", rel error "
                           << std::abs(shell.volumeMm3 - hollow) / hollow);
    CHECK_THAT(shell.volumeMm3, WithinRel(hollow, kRel));
    CHECK(shell.volumeMm3 < draft.volumeMm3);
    checkBounds(shell, {0.0, 0.0, -40.0}, {120.0, 80.0, 0.0});

    // 4. The port and the fixing holes go through the FLAT top wall, which
    // the draft does not tilt, so each removes an exact cylinder.
    const double wall = 4.0;
    const double portBore = pi * 12.0 * 12.0 * wall;
    const double spotface = pi * (18.0 * 18.0 - 12.0 * 12.0) * 1.5;
    const auto port = featureBody(regenerator, m.port);
    INFO("port removed expected " << portBore + spotface << " mm^3, actual " << shell.volumeMm3 - port.volumeMm3);
    CHECK_THAT(shell.volumeMm3 - port.volumeMm3, WithinRel(portBore + spotface, kRel));

    // Three M6 holes, tapped through: the bore is the thread's basic minor
    // diameter, which the standard gives (P12-HOLE-001), not a number here.
    const auto thread = standards::parseMetricThread("M6");
    REQUIRE(thread.has_value());
    const double minor = standards::basicDiameters(*thread).minor.in(units::mm);
    const double fixing = 3.0 * pi * (minor / 2.0) * (minor / 2.0) * wall;
    const auto holes = featureBody(regenerator, m.fixingHoles);
    INFO("fixing holes expected " << fixing << " mm^3 (M6 minor " << minor << " mm), actual "
                                  << port.volumeMm3 - holes.volumeMm3);
    CHECK_THAT(port.volumeMm3 - holes.volumeMm3, WithinRel(fixing, kRel));

    // The whole part, against the closed forms end to end: the drafted box,
    // less the cavity, less the port and the three fixing bores.
    const auto print = requireFingerprint(m.document, regenerator);
    checkSound(onlyBody(print));
    const double whole = hollow - portBore - spotface - fixing;
    INFO("whole part expected " << whole << " mm^3, actual " << onlyBody(print).volumeMm3);
    CHECK_THAT(onlyBody(print).volumeMm3, WithinRel(whole, kRel));
}

TEST_CASE("ReferenceModel_GearboxCoverFollowsItsParameters", "[reference][p12][gearbox-cover][acceptance]") {
    // The wall and the port are equations on `width`, so widening the cover
    // moves both. Change, check, restore, check.
    GearboxCoverModel m = requireModel();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const auto before = requireFingerprint(m.document, regenerator);

    REQUIRE(m.document.setParameterValue(m.width, 100_mm).has_value());
    requireRegenerated(regenerator, m.document);
    CHECK_THAT(mm(m.document, m.wall), WithinRel(5.0, 1e-12));
    CHECK_THAT(mm(m.document, m.portDiameter), WithinRel(30.0, 1e-12));
    const auto block = featureBody(regenerator, m.block);
    CHECK_THAT(block.volumeMm3, WithinRel(120.0 * 100.0 * 40.0, kRel));
    const auto draft = featureBody(regenerator, m.draft);
    CHECK_THAT(draft.volumeMm3, WithinRel(draftedBox(120.0, 100.0, 40.0, 3.0), kRel));
    const auto wider = requireFingerprint(m.document, regenerator);
    CHECK(onlyBody(wider).volumeMm3 != onlyBody(before).volumeMm3);

    REQUIRE(m.document.setParameterValue(m.width, 80_mm).has_value());
    requireRegenerated(regenerator, m.document);
    const auto difference = reference::compare(before, requireFingerprint(m.document, regenerator));
    CHECK(difference.sameStructure);
    CHECK(difference.volumeAreaRelative <= kRel);
    CHECK(difference.positionMm <= kPositionMm);
}

TEST_CASE("ReferenceModel_GearboxCoverFailsAndRecovers", "[reference][p12][gearbox-cover][acceptance]") {
    // A wall the body cannot carry must fail atomically, with a diagnostic
    // that names the cause, and the model must come back when it is fixed.
    GearboxCoverModel m = requireModel();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const auto good = requireFingerprint(m.document, regenerator);

    // `wall` is an equation on `width`, so a width of 600 mm asks for a
    // 30 mm wall in a 40 mm box: thicker than the cover can hold.
    const Document before = m.document.clone();
    REQUIRE(m.document.setParameterValue(m.width, 600_mm).has_value());
    auto report = regenerator.regenerate(m.document);
    REQUIRE(report.has_value());
    INFO(bettercad::test::describe(*report));
    CHECK_FALSE(report->succeeded());
    CHECK_FALSE(report->failed.empty());
    const Error& error = report->errors.at(report->failed.front());
    CHECK_FALSE(error.message.empty());
    // Nothing downstream of the failure became a result.
    CHECK(regenerator.body(m.fixingHoles) == nullptr);
    // The document itself is untouched apart from the parameter edit.
    CHECK(m.document.objectCount() == before.objectCount());

    REQUIRE(m.document.setParameterValue(m.width, 80_mm).has_value());
    requireRegenerated(regenerator, m.document);
    const auto difference = reference::compare(good, requireFingerprint(m.document, regenerator));
    CHECK(difference.sameStructure);
    CHECK(difference.volumeAreaRelative <= kRel);
}


TEST_CASE("ReferenceModel_GearboxCoverFollowsItsHeight", "[reference][p12][gearbox-cover][acceptance]") {
    // REGRESSION (P12-REF-001 adversarial review). `height` used to break the
    // model outright: the two holes were placed on a FaceSignature written
    // as the literal plane z = 40, which is where the outside face sat only
    // at the height it was written for. Raising the cover moved that face
    // and regeneration failed with
    //
    //   InspectionPort: hole: the placement face (plane through (0, 0, 40)
    //   mm facing (0, 0, 1)) matches no face of the body
    //
    // which is BetterCAD behaving correctly -- a moved plane matches no face
    // and says so, rather than silently drilling a different face. The model
    // was at fault, and now hangs below its outside face at z = 0, so the
    // plane the holes are placed on never moves. The parting face, which
    // does move, is reached by a driven datum, and the shell's open face by
    // a named face.
    GearboxCoverModel m = requireModel();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const auto before = requireFingerprint(m.document, regenerator);

    REQUIRE(m.document.setParameterValue(m.height, 55_mm).has_value());
    requireRegenerated(regenerator, m.document);
    // The parting datum followed the height, so the cover is 55 deep and
    // still hangs from z = 0.
    CHECK_THAT(mm(m.document, m.partingOffset), WithinRel(-55.0, 1e-12));
    const auto taller = featureBody(regenerator, m.shell);
    checkSound(taller);
    CHECK_THAT(taller.volumeMm3, WithinRel(shelledDraftedBox(120.0, 80.0, 55.0, 3.0, 4.0), kRel));
    checkBounds(taller, {0.0, 0.0, -55.0}, {120.0, 80.0, 0.0});

    // And a shorter one, to be sure nothing is special about growing.
    REQUIRE(m.document.setParameterValue(m.height, 25_mm).has_value());
    requireRegenerated(regenerator, m.document);
    const auto shorter = featureBody(regenerator, m.shell);
    checkSound(shorter);
    CHECK_THAT(shorter.volumeMm3, WithinRel(shelledDraftedBox(120.0, 80.0, 25.0, 3.0, 4.0), kRel));
    checkBounds(shorter, {0.0, 0.0, -25.0}, {120.0, 80.0, 0.0});

    REQUIRE(m.document.setParameterValue(m.height, 40_mm).has_value());
    requireRegenerated(regenerator, m.document);
    const auto difference = reference::compare(before, requireFingerprint(m.document, regenerator));
    CHECK(difference.sameStructure);
    CHECK(difference.volumeAreaRelative <= kRel);
    CHECK(difference.positionMm <= kPositionMm);
}

TEST_CASE("ReferenceModel_GearboxCoverFollowsItsDraftAngle", "[reference][p12][gearbox-cover][acceptance]") {
    // The draft angle drove nothing any test checked. Both the outer solid
    // and the cavity follow it, so the closed form tracks it at two angles.
    GearboxCoverModel m = requireModel();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    for (const double degrees : {1.5, 5.0}) {
        DYNAMIC_SECTION("draft " << degrees << " deg") {
            REQUIRE(m.document.setParameterValue(m.draftAngle, degrees * units::deg).has_value());
            requireRegenerated(regenerator, m.document);
            const auto drafted = featureBody(regenerator, m.draft);
            checkSound(drafted);
            CHECK_THAT(drafted.volumeMm3, WithinRel(draftedBox(120.0, 80.0, 40.0, degrees), kRel));
            const auto hollowed = featureBody(regenerator, m.shell);
            CHECK_THAT(hollowed.volumeMm3,
                       WithinRel(shelledDraftedBox(120.0, 80.0, 40.0, degrees, 4.0), kRel));
        }
    }
}
