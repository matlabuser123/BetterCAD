#include "reference/DrawingTestSupport.hpp"

#include <bettercad/drawing/Annotations.hpp>
#include <bettercad/drawing/Sheets.hpp>
#include <bettercad/features/Validation.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <numbers>
#include <vector>

using namespace bettercad;
using namespace bettercad::test;
using namespace bettercad::test::drawref;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

// RM-DWG-04 -- the hole pattern.
//
// Every expected number here is arithmetic done in this file, from the
// dimensions the model is defined by. Nothing asks the pattern where it put
// its instances: the drawing is asked what it DREW, and the drawn circles are
// compared with positions computed from the pitch.
namespace {

/// The plate, in millimetres. The builder's parameters, restated here so the
/// test does not read its expectations out of the thing it is testing.
constexpr double kLengthMm = 120.0;
constexpr double kWidthMm = 80.0;
constexpr double kThicknessMm = 10.0;
constexpr double kBoreRadiusMm = 5.0;
constexpr double kClearanceRadiusMm = 8.0;
constexpr double kFirstBoreXMm = 20.0;
constexpr double kFirstBoreYMm = 15.0;
constexpr double kPitchXMm = 80.0;
constexpr double kPitchYMm = 40.0;
/// The clearance hole, off BOTH symmetry axes so a reflected view cannot
/// look correct.
constexpr double kClearanceXMm = 75.0;
constexpr double kClearanceYMm = 62.0;

/// Where the Top view is placed, and therefore where model coordinates land.
/// A view is placed by its projected bounding-box CENTRE, so model (x, y)
/// draws at (placement + (x, y) - centre) when the scale is 1:1.
constexpr double kViewXMm = 150.0;
constexpr double kViewYMm = 100.0;

[[nodiscard]] std::pair<double, double> onSheet(double modelXMm, double modelYMm) {
    return {kViewXMm + modelXMm - kLengthMm / 2.0, kViewYMm + modelYMm - kWidthMm / 2.0};
}

/// The four bore centres the pattern must produce, in the sorted order
/// circleCentresMm() returns them in.
///
/// The rows are at y = 15 and 55 about a plate centre of 40, so the set is
/// NOT symmetric about either axis. That matters: with the rows at 20 and 60
/// -- where this model began -- a mirrored or reflected view would have drawn
/// every circle exactly where one was expected, and this comparison would
/// have passed on a wrong picture.
[[nodiscard]] std::vector<std::pair<double, double>> expectedBoreCentres(double pitchXMm) {
    std::vector<std::pair<double, double>> centres;
    for (int row = 0; row < 2; ++row) {
        for (int column = 0; column < 2; ++column) {
            centres.push_back(onSheet(kFirstBoreXMm + column * pitchXMm,
                                      kFirstBoreYMm + row * kPitchYMm));
        }
    }
    std::ranges::sort(centres);
    return centres;
}

} // namespace

TEST_CASE("DrawingReference_HolePlateHasTheVolumeItsFiveHolesLeave",
          "[reference][drawing][rm-dwg-04]") {
    // V = 120*80*10 - 4*pi*5^2*10 - pi*8^2*10, computed from the numbers
    // above rather than from the model.
    const double expected = kLengthMm * kWidthMm * kThicknessMm -
                            4.0 * std::numbers::pi * kBoreRadiusMm * kBoreRadiusMm * kThicknessMm -
                            std::numbers::pi * kClearanceRadiusMm * kClearanceRadiusMm *
                                kThicknessMm;

    Drawn d = drawn(reference::DrawingReferenceModelKind::HolePlate);
    CHECK(features::validateDocument(d.document()).valid());

    const auto plate = d.document().findByName("Clearance");
    REQUIRE(plate.has_value());
    const geometry::Body* body = d.regenerator().body(*plate);
    REQUIRE(body != nullptr);
    // 1e-6 mm^3 on ~90 000 mm^3: the kernel's own volume of an exact prism
    // less five exact cylinders, so nothing here is conditioned badly.
    CHECK_THAT(volumeMm3(*body), WithinAbs(expected, 1e-6));
}

TEST_CASE("DrawingReference_HolePlateDrawsItsPatternWhereTheArithmeticSaysItIs",
          "[reference][drawing][rm-dwg-04]") {
    // The independent validation of the pattern: the four bores are read off
    // the DRAWN SHEET as circles, and compared with centres computed from the
    // pitch. Not one production helper is asked where an instance went.
    Drawn d = drawn(reference::DrawingReferenceModelKind::HolePlate);
    const drawing::DrawingScene scene = sceneOf(d, drawing::sheets(d.document()).front());

    const std::vector<std::pair<double, double>> bores =
        circleCentresMm(scene, kBoreRadiusMm);
    REQUIRE(bores.size() == 4);
    const std::vector<std::pair<double, double>> expected = expectedBoreCentres(kPitchXMm);
    for (std::size_t i = 0; i < bores.size(); ++i) {
        INFO("bore " << i);
        CHECK_THAT(bores[i].first, WithinAbs(expected[i].first, kPaperMm));
        CHECK_THAT(bores[i].second, WithinAbs(expected[i].second, kPaperMm));
    }

    // And the fifth hole, which is not one of them: one circle, of a
    // different radius, at the middle of the plate.
    const std::vector<std::pair<double, double>> clearance =
        circleCentresMm(scene, kClearanceRadiusMm);
    REQUIRE(clearance.size() == 1);
    const auto [x, y] = onSheet(kClearanceXMm, kClearanceYMm);
    CHECK_THAT(clearance.front().first, WithinAbs(x, kPaperMm));
    CHECK_THAT(clearance.front().second, WithinAbs(y, kPaperMm));
}

TEST_CASE("DrawingReference_HolePlateMeasuresItsPlateAndItsBores",
          "[reference][drawing][rm-dwg-04]") {
    auto m = reference::buildDrawnHolePlateReferenceModel();
    INFO(why(m));
    REQUIRE(m.has_value());
    Drawn model{std::move(m->document)};
    INFO(model.why());
    REQUIRE(model.succeeded());

    // 1e-9 mm is geometric accumulation on a projected, placed measurement;
    // these are distances between exact planes and an exact cylinder, so
    // nothing needs more slack than that.
    // HORIZONTAL and VERTICAL, read off the top view's own axes. Both
    // separations lie in that view's plane, so they are also the true
    // distances -- and that agreement is the assertion: a view whose axes
    // were not the sheet's would give something else.
    CHECK_THAT(measuredMm(model, m->length), WithinAbs(kLengthMm, 1e-9));
    CHECK_THAT(measuredMm(model, m->width), WithinAbs(kWidthMm, 1e-9));

    // A radius and its diameter, on one cylinder, by the one path: exactly
    // twice, with nothing measured a second time.
    CHECK_THAT(measuredMm(model, m->boreRadiusDimension), WithinAbs(kBoreRadiusMm, 1e-9));
    CHECK_THAT(measuredMm(model, m->boreDiameter), WithinAbs(2.0 * kBoreRadiusMm, 1e-9));
    CHECK(measuredMm(model, m->boreDiameter) == 2.0 * measuredMm(model, m->boreRadiusDimension));
    // The copy diagonally opposite reads the same -- which a reference that
    // had collapsed onto the source would also do, so the next case checks
    // WHERE each of them is drawn.
    CHECK_THAT(measuredMm(model, m->farBoreDiameter), WithinAbs(2.0 * kBoreRadiusMm, 1e-9));

    // AN ORDINATE IS SIGNED, and that is the whole of what distinguishes it
    // from a horizontal or a vertical dimension of the same pair of faces.
    // Across from the left-hand end is +120; down from the top edge is -80,
    // and a magnitude-only reading would report +80 and say nothing about
    // which side of the datum the edge is on.
    CHECK_THAT(measuredMm(model, m->ordinateAcross), WithinAbs(kLengthMm, 1e-9));
    CHECK_THAT(measuredMm(model, m->ordinateDown), WithinAbs(-kWidthMm, 1e-9));
    CHECK(measuredMm(model, m->ordinateDown) < 0.0);
    CHECK_THAT(dimensionText(model, m->ordinateDown), ContainsSubstring("-80.00"));

    CHECK(annotationTextOf(model, m->callout) == "Ø16 THRU");
}

TEST_CASE("DrawingReference_HolePlateNamesEachOfFourIdenticalBoresSeparately",
          "[reference][drawing][rm-dwg-04][stref]") {
    // FOUR IDENTICAL CIRCLES, FOUR DIFFERENT NAMES. Each centre mark names
    // one bore -- the source, or one copy of the pattern -- and the four must
    // land on four different places. A reference that had collapsed onto the
    // source would draw four marks on top of each other and measure the same
    // diameter four times, which is exactly what a diameter check alone
    // cannot tell apart.
    auto m = reference::buildDrawnHolePlateReferenceModel();
    INFO(why(m));
    REQUIRE(m.has_value());
    Drawn model{std::move(m->document)};
    INFO(model.why());
    REQUIRE(model.succeeded());

    // Pattern instance i is at (first + column*pitchX, first + row*pitchY),
    // where index = column + row*2 -- the order LinearPattern documents.
    std::vector<std::pair<double, double>> marks;
    for (std::size_t i = 0; i < m->centremarks.size(); ++i) {
        INFO("centre mark " << i);
        const drawing::Resolution state = resolutionOf(model, m->centremarks[i]);
        INFO(state.diagnostic);
        CHECK(state.resolved());

        auto items = drawing::draw(model.document(), m->centremarks[i], model.bodies(),
                                   model.transforms());
        INFO(why(items));
        REQUIRE(items.has_value());
        // Two crossing arms; they meet at the projected centre, which is the
        // midpoint of either.
        REQUIRE(items->lines.size() == 2);
        const drawing::SceneLine& arm = items->lines.front();
        REQUIRE(arm.points.size() == 2);
        marks.emplace_back(
            0.5 * (arm.points[0].x.in(units::mm) + arm.points[1].x.in(units::mm)),
            0.5 * (arm.points[0].y.in(units::mm) + arm.points[1].y.in(units::mm)));

        const auto expected = onSheet(kFirstBoreXMm + static_cast<double>(i % 2) * kPitchXMm,
                                      kFirstBoreYMm + static_cast<double>(i / 2) * kPitchYMm);
        CHECK_THAT(marks.back().first, WithinAbs(expected.first, kPaperMm));
        CHECK_THAT(marks.back().second, WithinAbs(expected.second, kPaperMm));
    }

    // No two of the four are in the same place.
    for (std::size_t i = 0; i < marks.size(); ++i) {
        for (std::size_t j = i + 1; j < marks.size(); ++j) {
            INFO("marks " << i << " and " << j);
            CHECK(std::hypot(marks[i].first - marks[j].first,
                             marks[i].second - marks[j].second) > 1.0);
        }
    }
}

TEST_CASE("DrawingReference_HolePlateMovesTwoBoresWhenThePitchChanges",
          "[reference][drawing][rm-dwg-04][regen]") {
    // A controlled mutation, and the drawing has to follow it: pitch_x
    // 80 -> 70 moves the two bores in the right-hand column 10 mm left and
    // leaves the left-hand column exactly where it was. A pattern that
    // rebuilt from scratch about a different origin would move all four.
    auto m = reference::buildDrawnHolePlateReferenceModel();
    INFO(why(m));
    REQUIRE(m.has_value());
    Drawn model{std::move(m->document)};
    INFO(model.why());
    REQUIRE(model.succeeded());
    const SheetId sheet = drawing::sheets(model.document()).front();

    const std::vector<std::pair<double, double>> before =
        circleCentresMm(sceneOf(model, sheet), kBoreRadiusMm);
    REQUIRE(sameCentres(before, expectedBoreCentres(kPitchXMm)));

    constexpr double kNewPitchXMm = 70.0;
    driveMm(model, "pitch_x", kNewPitchXMm);

    const std::vector<std::pair<double, double>> after =
        circleCentresMm(sceneOf(model, sheet), kBoreRadiusMm);
    REQUIRE(after.size() == 4);
    const std::vector<std::pair<double, double>> expected = expectedBoreCentres(kNewPitchXMm);
    for (std::size_t i = 0; i < after.size(); ++i) {
        INFO("bore " << i);
        CHECK_THAT(after[i].first, WithinAbs(expected[i].first, kPaperMm));
        CHECK_THAT(after[i].second, WithinAbs(expected[i].second, kPaperMm));
    }
    CHECK_FALSE(sameCentres(after, before));

    // And back: the same pitch gives the same picture, to the last item.
    driveMm(model, "pitch_x", kPitchXMm);
    CHECK(sameCentres(circleCentresMm(sceneOf(model, sheet), kBoreRadiusMm), before));
}

TEST_CASE("DrawingReference_HolePlateCalloutFollowsTheHoleItNames",
          "[reference][drawing][rm-dwg-04][regen]") {
    // The callout stores no number (P14-ANNO-001), so widening the hole must
    // change what the drawing says without anyone editing the annotation.
    auto m = reference::buildDrawnHolePlateReferenceModel();
    INFO(why(m));
    REQUIRE(m.has_value());
    Drawn model{std::move(m->document)};
    INFO(model.why());
    REQUIRE(model.succeeded());
    REQUIRE(annotationTextOf(model, m->callout) == "Ø16 THRU");

    driveMm(model, "clearance_d", 20.0);
    CHECK(annotationTextOf(model, m->callout) == "Ø20 THRU");

    driveMm(model, "clearance_d", 16.0);
    CHECK(annotationTextOf(model, m->callout) == "Ø16 THRU");
}

TEST_CASE("DrawingReference_HolePlateDrawsItsBoresAsCirclesInEveryFormat",
          "[reference][drawing][rm-dwg-04][export]") {
    // The four bores must survive as CIRCLES all the way to the file. A
    // sampled polyline would still look right and would still be on the page;
    // what it would not be is a circle a reader could snap to.
    Drawn d = drawn(reference::DrawingReferenceModelKind::HolePlate);
    const drawing::DrawingScene scene = sceneOf(d, drawing::sheets(d.document()).front());

    const auto svg = io::svgDocument(scene);
    const auto dxf = io::dxfDocument(scene);
    REQUIRE(svg.has_value());
    REQUIRE(dxf.has_value());

    // SVG: five <circle> elements, four of r = 5 and one of r = 8.
    const std::vector<drawex::SvgElement> elements = drawex::readSvg(*svg);
    std::size_t boresInSvg = 0;
    std::size_t clearanceInSvg = 0;
    for (const drawex::SvgElement* circle : drawex::allSvg(elements, "circle")) {
        const double r = std::stod(circle->attributes.at("r"));
        boresInSvg += std::abs(r - kBoreRadiusMm) < 1e-6 ? 1U : 0U;
        clearanceInSvg += std::abs(r - kClearanceRadiusMm) < 1e-6 ? 1U : 0U;
    }
    CHECK(boresInSvg == 4);
    CHECK(clearanceInSvg == 1);

    // DXF: the same five, as CIRCLE entities with group code 40 for radius.
    std::size_t boresInDxf = 0;
    std::size_t clearanceInDxf = 0;
    for (const drawex::DxfEntity& entity : drawex::dxfEntities(*dxf)) {
        if (entity.type != "CIRCLE") {
            continue;
        }
        const double r = entity.number(40);
        boresInDxf += std::abs(r - kBoreRadiusMm) < 1e-6 ? 1U : 0U;
        clearanceInDxf += std::abs(r - kClearanceRadiusMm) < 1e-6 ? 1U : 0U;
    }
    CHECK(boresInDxf == 4);
    CHECK(clearanceInDxf == 1);

    // And the DXF's circles are where the sheet's are. DXF keeps +Y up, so no
    // flip is expected here and a flip would show as a mismatch.
    const std::vector<std::pair<double, double>> expected = expectedBoreCentres(kPitchXMm);
    std::vector<std::pair<double, double>> fromDxf;
    for (const drawex::DxfEntity& entity : drawex::dxfEntities(*dxf)) {
        if (entity.type == "CIRCLE" && std::abs(entity.number(40) - kBoreRadiusMm) < 1e-6) {
            fromDxf.emplace_back(entity.number(10), entity.number(20));
        }
    }
    std::ranges::sort(fromDxf);
    REQUIRE(fromDxf.size() == expected.size());
    for (std::size_t i = 0; i < fromDxf.size(); ++i) {
        INFO("DXF circle " << i);
        CHECK_THAT(fromDxf[i].first, WithinAbs(expected[i].first, drawex::kMm));
        CHECK_THAT(fromDxf[i].second, WithinAbs(expected[i].second, drawex::kMm));
    }
}

TEST_CASE("DrawingReference_HolePlateSaysWhatItPatternedInItsNote",
          "[reference][drawing][rm-dwg-04]") {
    // A note's words ARE intent, so they are stored and must survive to the
    // sheet. This is the one text on this drawing that the model does not
    // derive, and it is checked separately for that reason.
    Drawn d = drawn(reference::DrawingReferenceModelKind::HolePlate);
    const drawing::DrawingScene scene = sceneOf(d, drawing::sheets(d.document()).front());
    const std::vector<std::string> texts = sceneTexts(scene);
    CHECK(std::ranges::any_of(texts, [](const std::string& text) {
        return text.find("80 x 40 PITCH") != std::string::npos;
    }));
}
