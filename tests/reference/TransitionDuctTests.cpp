#include "reference/ReferenceTestSupport.hpp"

#include <bettercad/core/document/Commands.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>
#include <optional>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test::refmodel;
using bettercad::reference::buildTransitionDuctReferenceModel;
using bettercad::reference::TransitionDuctModel;
using Catch::Matchers::WithinRel;

// P12-REF-001: the square-to-round duct and its smooth nozzle -- both halves
// of P12-LOFT-001 in one part, each against a closed form derived here.

namespace {

constexpr double pi = std::numbers::pi;

TransitionDuctModel requireModel() {
    auto model = buildTransitionDuctReferenceModel();
    if (!model) {
        FAIL(model.error().message);
    }
    return std::move(*model);
}

/// A ruled loft from a square of side `a` to a circle of radius `r` over
/// height `h`. Between matched sections the cross-section is a quadratic in
/// the height, so the prismatoid formula is exact:
///
///   V = h/6 (A0 + 4 Am + A1),  Am = (A0 + M + A1)/4
///
/// with M the mixed area, which for a regular n-gon of circumradius R
/// against a circle of radius r is (2 n^2 r R / pi) sin^2(pi/n); for a
/// square (n = 4, R = a/sqrt2) that is 16 r R / pi.
double squareToCircle(double a, double r, double h) {
    const double circumradius = a / std::numbers::sqrt2;
    const double mixed = 16.0 * r * circumradius / pi;
    return h / 6.0 * (2.0 * a * a + mixed + 2.0 * pi * r * r);
}

/// pi H times the integral of the quadratic through r1, r2, r3 at
/// s = 0, 1/2, 1, squared. Written out rather than sampled.
double quadraticOfRevolution(double r1, double r2, double r3, double h) {
    const double a = 2.0 * r1 - 4.0 * r2 + 2.0 * r3;
    const double b = -3.0 * r1 + 4.0 * r2 - r3;
    const double c = r1;
    const double integral = a * a / 5.0 + a * b / 2.0 + (2.0 * a * c + b * b) / 3.0 + b * c + c * c;
    return pi * h * integral;
}

/// pi h/3 (r1^2 + r1 r2 + r2^2).
double frustum(double r1, double r2, double h) {
    return pi * h / 3.0 * (r1 * r1 + r1 * r2 + r2 * r2);
}

double mm(const Document& doc, ParameterId parameter) {
    const auto value = doc.effectiveParameterValue(parameter);
    REQUIRE(value.has_value());
    return value->siValue * 1000.0;
}

} // namespace

TEST_CASE("ReferenceModel_TransitionDuctRuledHalfIsExact", "[reference][p12][duct][acceptance]") {
    // Square to round, ruled: BetterCAD matches the sections itself and the
    // prismatoid of the mixed area is exact, so this half needs no envelope.
    TransitionDuctModel m = requireModel();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);

    CHECK_THAT(mm(m.document, m.midRadius), WithinRel(20.0, 1e-12));
    CHECK_THAT(mm(m.document, m.outletRadius), WithinRel(25.0, 1e-12));
    CHECK_THAT(mm(m.document, m.midOffset), WithinRel(90.0, 1e-12));
    CHECK_THAT(mm(m.document, m.outletOffset), WithinRel(120.0, 1e-12));

    const double expected = squareToCircle(80.0, 30.0, 60.0);
    const auto duct = featureBody(regenerator, m.duct);
    checkSound(duct);
    INFO("duct expected " << expected << " mm^3, actual " << duct.volumeMm3 << ", rel error "
                          << std::abs(duct.volumeMm3 - expected) / expected);
    CHECK_THAT(duct.volumeMm3, WithinRel(expected, kRel));
    checkBounds(duct, {-40.0, -40.0, 0.0}, {40.0, 40.0, 60.0});
}

TEST_CASE("ReferenceModel_TransitionDuctSmoothNozzleFollowsTheQuadraticOnlyWhenItsEndsAreEqual",
          "[reference][p12][duct][acceptance]") {
    // P12-LOFT-001 recorded that a smooth loft through three EQUALLY SPACED
    // sections is the quadratic through them, and measured it on a
    // SYMMETRIC spool (r 10, 5, 10). A symmetric case cannot tell one
    // parameterization from another, and this model shows the claim is
    // narrower than it reads: with the ends equal the quadratic is exact,
    // and with them unequal it is not.
    //
    //   ends equal   (30, 20, 30): measured within 1.6e-11 of the quadratic
    //   ends unequal (30, 20, 25): measured 1.0e-3 away from it
    //
    // Nothing is wrong with the geometry -- P12-LOFT-001 guards a smooth
    // loft by the RULED loft of the same sections, which is still checked
    // exactly, not by the quadratic. What is corrected is the claim.
    //
    // Be clear about what each number here is worth. The ENVELOPE below
    // (under the ruled loft of the same sections, over half of it) is
    // analytic: it follows from the sections alone and holds whatever
    // parameterization the kernel picks. The 1.1e-3 bound on the unequal
    // case is NOT analytic -- it is the measured deviation rounded up, and
    // it is here as a regression guard, to catch the kernel's smooth loft
    // moving, not as evidence that the quadratic describes it. The exact
    // claim is the equal-ended one at 1e-10.
    const double height = 60.0;
    struct Case {
        double outletRadius;
        double bound;
        const char* what;
    };
    for (const Case& c : {Case{30.0, 1e-10, "ends equal"}, Case{25.0, 1.1e-3, "ends unequal"}}) {
        DYNAMIC_SECTION(c.what) {
            TransitionDuctModel m = requireModel();
            // Free the outlet from its equation so the ends can be made equal.
            REQUIRE(m.document.setParameterExpression(m.outletRadius, std::nullopt).has_value());
            REQUIRE(m.document.setParameterValue(m.outletRadius, c.outletRadius * units::mm).has_value());
            features::Regenerator regenerator;
            requireRegenerated(regenerator, m.document);

            const double nozzle =
                featureBody(regenerator, m.nozzle).volumeMm3 - featureBody(regenerator, m.duct).volumeMm3;
            const double quadratic = quadraticOfRevolution(30.0, 20.0, c.outletRadius, height);
            const double error = std::abs(nozzle - quadratic) / quadratic;
            INFO(c.what << ": nozzle " << nozzle << " mm^3, quadratic " << quadratic << " mm^3, rel error "
                        << error);
            CHECK(error <= c.bound);

            // Whatever the parameterization, the smooth solid stays inside
            // the envelope P12-LOFT-001 states, below the ruled loft of the
            // same sections and above half of it.
            const double ruled = frustum(30.0, 20.0, height / 2.0) +
                                 frustum(20.0, c.outletRadius, height / 2.0);
            CHECK(nozzle < ruled);
            CHECK(nozzle > 0.5 * ruled);
        }
    }
}

TEST_CASE("ReferenceModel_TransitionDuctIsOneSolid", "[reference][p12][duct][acceptance]") {
    // The nozzle is joined to the duct at their shared section, so the part
    // is one solid and its volume is the two halves added.
    TransitionDuctModel m = requireModel();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);

    const auto body = onlyBody(requireFingerprint(m.document, regenerator));
    checkSound(body);
    const double duct = featureBody(regenerator, m.duct).volumeMm3;
    const double nozzle = featureBody(regenerator, m.nozzle).volumeMm3;
    CHECK_THAT(nozzle - duct, WithinRel(quadraticOfRevolution(30.0, 20.0, 25.0, 60.0), 1.1e-3));
    CHECK_THAT(duct, WithinRel(squareToCircle(80.0, 30.0, 60.0), kRel));
    // The inlet flange, a square plate below z = 0, adds its own box.
    CHECK_THAT(body.volumeMm3 - nozzle, WithinRel(110.0 * 110.0 * 10.0, kRel));
    checkBounds(body, {-55.0, -55.0, -10.0}, {55.0, 55.0, 120.0});
}

TEST_CASE("ReferenceModel_TransitionDuctFollowsItsParameters", "[reference][p12][duct][acceptance]") {
    // The throat drives the two sections above it, and the duct height
    // drives where all three sit. Change, check against the closed form,
    // restore, check.
    TransitionDuctModel m = requireModel();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const auto before = requireFingerprint(m.document, regenerator);

    REQUIRE(m.document.setParameterValue(m.throatRadius, 36_mm).has_value());
    requireRegenerated(regenerator, m.document);
    CHECK_THAT(mm(m.document, m.midRadius), WithinRel(24.0, 1e-12));
    CHECK_THAT(mm(m.document, m.outletRadius), WithinRel(30.0, 1e-12));
    CHECK_THAT(featureBody(regenerator, m.duct).volumeMm3, WithinRel(squareToCircle(80.0, 36.0, 60.0), kRel));

    REQUIRE(m.document.setParameterValue(m.ductHeight, 75_mm).has_value());
    requireRegenerated(regenerator, m.document);
    CHECK_THAT(mm(m.document, m.midOffset), WithinRel(112.5, 1e-12));
    CHECK_THAT(mm(m.document, m.outletOffset), WithinRel(150.0, 1e-12));
    CHECK_THAT(featureBody(regenerator, m.duct).volumeMm3, WithinRel(squareToCircle(80.0, 36.0, 75.0), kRel));

    REQUIRE(m.document.setParameterValue(m.throatRadius, 30_mm).has_value());
    REQUIRE(m.document.setParameterValue(m.ductHeight, 60_mm).has_value());
    requireRegenerated(regenerator, m.document);
    const auto difference = reference::compare(before, requireFingerprint(m.document, regenerator));
    CHECK(difference.sameStructure);
    CHECK(difference.volumeAreaRelative <= kRel);
    CHECK(difference.positionMm <= kPositionMm);
}

TEST_CASE("ReferenceModel_TransitionDuctRefusesADegenerateSection", "[reference][p12][duct][acceptance]") {
    // A throat of no size leaves the loft nothing to pass through. It must
    // fail atomically and recover.
    TransitionDuctModel m = requireModel();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const auto good = requireFingerprint(m.document, regenerator);

    REQUIRE(m.document.setParameterValue(m.throatRadius, 0_mm).has_value());
    auto report = regenerator.regenerate(m.document);
    REQUIRE(report.has_value());
    INFO(bettercad::test::describe(*report));
    CHECK_FALSE(report->succeeded());
    CHECK_FALSE(report->failed.empty());
    CHECK(regenerator.body(m.nozzle) == nullptr);

    REQUIRE(m.document.setParameterValue(m.throatRadius, 30_mm).has_value());
    requireRegenerated(regenerator, m.document);
    const auto difference = reference::compare(good, requireFingerprint(m.document, regenerator));
    CHECK(difference.sameStructure);
    CHECK(difference.volumeAreaRelative <= kRel);
}

TEST_CASE("ReferenceModel_TransitionDuctFollowsItsInlet", "[reference][p12][duct][acceptance]") {
    // REGRESSION (P12-REF-001 adversarial review). `inlet_side` drove the
    // square's edge length, but the two constraints CENTRING that square on
    // the axis were written as the literal 40 mm -- half the side it was
    // authored at. At any other inlet the square sat off-axis from the round
    // sections above it, so the loft was no longer concentric and
    // squareToCircle() -- which assumes concentric sections -- would have
    // been quietly describing a different solid. Both are now inlet_side/2.
    //
    // No test drove `inlet_side` before. This one does.
    for (const double inlet : {64.0, 96.0}) {
        DYNAMIC_SECTION("inlet " << inlet << " mm") {
            TransitionDuctModel m = requireModel();
            REQUIRE(m.document.setParameterValue(m.inletSide, inlet * units::mm).has_value());
            features::Regenerator regenerator;
            requireRegenerated(regenerator, m.document);

            CHECK_THAT(mm(m.document, m.halfInlet), WithinRel(inlet / 2.0, 1e-12));
            CHECK_THAT(mm(m.document, m.flangeSide), WithinRel(inlet + 30.0, 1e-12));
            CHECK_THAT(mm(m.document, m.halfFlange), WithinRel((inlet + 30.0) / 2.0, 1e-12));

            // The prismatoid closed form still describes the ruled half,
            // which it only can while the sections stay concentric.
            const double expected = squareToCircle(inlet, 30.0, 60.0);
            const auto duct = featureBody(regenerator, m.duct);
            checkSound(duct);
            INFO("duct expected " << expected << " mm^3, actual " << duct.volumeMm3 << ", rel error "
                                  << std::abs(duct.volumeMm3 - expected) / expected);
            CHECK_THAT(duct.volumeMm3, WithinRel(expected, kRel));
            // Centred means the bounds are symmetric about the axis.
            checkBounds(duct, {-inlet / 2.0, -inlet / 2.0, 0.0}, {inlet / 2.0, inlet / 2.0, 60.0});
        }
    }
}
