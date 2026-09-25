#include "reference/DrawingTestSupport.hpp"

#include <bettercad/drawing/Annotations.hpp>
#include <bettercad/drawing/Dimensions.hpp>
#include <bettercad/drawing/Sheets.hpp>
#include <bettercad/features/Validation.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <numbers>
#include <ranges>

using namespace bettercad;
using namespace bettercad::test;
using namespace bettercad::test::drawref;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

// RM-DWG-01, as its builder sets it. Restated HERE, by hand, so a change to
// the builder that nobody meant shows up as a failure rather than as a new
// expectation.
constexpr double kPlateLength = 100.0;
constexpr double kPlateWidth = 60.0;
constexpr double kPlateThickness = 20.0;
constexpr double kStepLength = 40.0;
constexpr double kStepWidth = 20.0;
constexpr double kStepHeight = 10.0;
constexpr double kHoleDiameter = 12.0;

/// V = plate + step - bore, derived here and never read from the model.
[[nodiscard]] double expectedVolumeMm3() {
    const double plate = kPlateLength * kPlateWidth * kPlateThickness;
    const double step = kStepLength * kStepWidth * kStepHeight;
    const double bore = std::numbers::pi * (kHoleDiameter / 2.0) * (kHoleDiameter / 2.0)
                        * kPlateThickness;
    return plate + step - bore;
}

[[nodiscard]] reference::DrawnStepPlateModel built() {
    auto model = reference::buildDrawnStepPlateReferenceModel();
    INFO(why(model));
    REQUIRE(model.has_value());
    return std::move(*model);
}

} // namespace

TEST_CASE("DrawingReference_StepPlateBuildsItsPartAndItsDrawing",
          "[reference][drawing][rm-dwg-01]") {
    // The smoke test: the model builds, regenerates, and its geometry is the
    // arithmetic above -- before any drawing question is asked.
    reference::DrawnStepPlateModel m = built();
    Drawn d{std::move(m.document)};
    INFO(d.why());
    REQUIRE(d.succeeded());

    CHECK(features::validateDocument(d.document()).valid());

    const geometry::Body* body = d.regenerator().body(m.hole);
    REQUIRE(body != nullptr);
    const double measured = volumeMm3(*body);
    INFO("expected " << expectedVolumeMm3() << " measured " << measured);
    CHECK_THAT(measured, WithinRel(expectedVolumeMm3(), refmodel::kRel));

    // The drawing is really in the document, and the objects are the ones the
    // builder made.
    CHECK(drawing::sheets(d.document()).size() == 1);
    CHECK(drawing::viewsOn(d.document(), m.sheet).size() == 3);
    CHECK(drawing::dimensions(d.document()).size() == 2);
    CHECK(drawing::annotations(d.document()).size() == 2);
}

TEST_CASE("DrawingReference_StepPlateDimensionsMeasureWhatTheyName",
          "[reference][drawing][rm-dwg-01]") {
    // Every number here is the builder's arithmetic, not the model's answer.
    reference::DrawnStepPlateModel m = built();
    Drawn d{std::move(m.document)};
    REQUIRE(d.succeeded());

    CHECK_THAT(measuredMm(d, m.length), WithinAbs(kPlateLength, refmodel::kPositionMm));
    CHECK_THAT(measuredMm(d, m.thickness), WithinAbs(kPlateThickness, refmodel::kPositionMm));
}

TEST_CASE("DrawingReference_StepPlateSheetDrawsOnThePage", "[reference][drawing][rm-dwg-01]") {
    // The scene is the export boundary, and it refuses anything off the page.
    // Building it at all is therefore a real assertion about every placement
    // in the model.
    reference::DrawnStepPlateModel m = built();
    Drawn d{std::move(m.document)};
    REQUIRE(d.succeeded());

    const drawing::DrawingScene scene = sceneOf(d, m.sheet);
    CHECK_THAT(scene.width.in(units::mm), WithinAbs(420.0, kPaperMm));
    CHECK_THAT(scene.height.in(units::mm), WithinAbs(297.0, kPaperMm));
    CHECK_FALSE(scene.items.lines.empty());
    CHECK_FALSE(scene.items.texts.empty());
}


TEST_CASE("DrawingReference_StepPlateDrawsItsHoleAsARealCircle",
          "[reference][drawing][rm-dwg-01]") {
    // A REGRESSION. Looking down the bore, the hole's edge is an exact circle
    // and must reach the scene as one -- so DXF gets a CIRCLE, SVG a <circle>
    // and PDF four Beziers, and a receiving package can snap to its centre.
    //
    // It did not. `circleThrough` builds a circle from three points, and a
    // CLOSED edge has start == end, so the determinant is identically zero and
    // it returned nothing. Every hole in every drawing fell back to a sampled
    // polyline of over a thousand points. P14-EXPORT-001 missed it because its
    // circle test used a scene built BY HAND that already held a SceneArc;
    // no test had ever asked a real model for one.
    reference::DrawnStepPlateModel m = built();
    Drawn d{std::move(m.document)};
    REQUIRE(d.succeeded());

    // The top view looks down the bore axis, so the projection really does
    // contain an exact circle -- that much always worked.
    const auto geometry = drawing::projectedGeometry(d.document(), m.top, d.bodies(),
                                                     d.transforms());
    REQUIRE(geometry.has_value());
    const std::size_t circles = static_cast<std::size_t>(std::ranges::count_if(
        geometry->edges,
        [](const auto& edge) { return edge.curve == bettercad::geometry::EdgeCurve::Circle; }));
    CHECK(circles == 1);

    const drawing::DrawingScene scene = sceneOf(d, m.sheet);
    REQUIRE(scene.items.arcs.size() == 1);
    const drawing::SceneArc& arc = scene.items.arcs.front();
    // Ø12 at 1:1 is a radius of 6.000 mm on paper, whatever else moves.
    CHECK_THAT(arc.radius.in(units::mm), WithinAbs(kHoleDiameter / 2.0, kPaperMm));
    // A closed edge is a whole turn.
    CHECK_THAT(std::abs(arc.sweep.in(units::deg)), WithinAbs(360.0, 1e-9));

    // And no polyline is left standing in for it. The sampled fallback was
    // over a thousand points; nothing a drawing needs is anywhere near that.
    for (const drawing::SceneLine& line : scene.items.lines) {
        INFO("a polyline of " << line.points.size() << " points is a curve that was not recovered");
        CHECK(line.points.size() <= 32);
    }
}
