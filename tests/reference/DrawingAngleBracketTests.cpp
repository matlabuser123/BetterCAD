#include "reference/DrawingTestSupport.hpp"

#include <bettercad/drawing/Sheets.hpp>
#include <bettercad/drawing/Views.hpp>

#include <BuildSupport.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <utility>

using namespace bettercad;
using namespace bettercad::test;
using namespace bettercad::test::drawref;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

// RM-DWG-02 as its builder sets it, restated here by hand.
constexpr double kLength = 120.0;
constexpr double kHeight = 60.0;
constexpr double kWidth = 50.0;
constexpr double kShoulder = 20.0;
constexpr double kCutback = 40.0;

/// A 120 x 60 rectangle less the 40 x 40 triangle cut off its corner, times
/// the width. Derived here; never read from the model.
[[nodiscard]] constexpr double expectedVolumeMm3() {
    const double rectangle = kLength * kHeight;
    const double corner = kCutback * (kHeight - kShoulder) / 2.0;
    return (rectangle - corner) * kWidth;
}
static_assert(expectedVolumeMm3() == 320000.0, "the arithmetic in the design note");

[[nodiscard]] reference::DrawnAngleBracketModel built() {
    auto model = reference::buildDrawnAngleBracketReferenceModel();
    INFO(why(model));
    REQUIRE(model.has_value());
    return std::move(*model);
}

} // namespace

TEST_CASE("DrawingReference_AngleBracketMeasuresItsSidesAndItsCorner",
          "[reference][drawing][rm-dwg-02]") {
    reference::DrawnAngleBracketModel m = built();
    Drawn d{std::move(m.document)};
    INFO(d.why());
    REQUIRE(d.succeeded());

    const geometry::Body* body = d.regenerator().body(m.body);
    REQUIRE(body != nullptr);
    CHECK_THAT(volumeMm3(*body), WithinRel(expectedVolumeMm3(), refmodel::kRel));

    CHECK_THAT(measuredMm(d, m.overall), WithinAbs(kLength, refmodel::kPositionMm));
    CHECK_THAT(measuredMm(d, m.rise), WithinAbs(kHeight, refmodel::kPositionMm));

    // The corner is cut back 40 over a 40 rise, so the slant is exactly 45
    // degrees from the base. An angular dimension reads
    // 180 - angle(outward normals); two parallel faces read 0, so this number
    // is the convention and not an accident of the geometry.
    CHECK_THAT(measuredDegrees(d, m.corner), WithinAbs(45.0, refmodel::kPositionMm));
}

TEST_CASE("DrawingReference_AngleBracketDrawsItsFrontViewAtHalfSize",
          "[reference][drawing][rm-dwg-02]") {
    // The front view overrides the sheet's 1:1 with 1:2. The GEOMETRY halves;
    // the lettering and the line weights are paper sizes and do not.
    reference::DrawnAngleBracketModel m = built();
    Drawn d{std::move(m.document)};
    REQUIRE(d.succeeded());

    const auto scaleOf = [&](ViewId view) {
        const auto scale = drawing::effectiveScale(d.document(), view);
        INFO(why(scale));
        REQUIRE(scale.has_value());
        return scale->factor();
    };
    CHECK_THAT(scaleOf(m.front), WithinAbs(0.5, 1e-15));
    CHECK_THAT(scaleOf(m.top), WithinAbs(1.0, 1e-15));

    // Measured on the PAPER, not asked of the scale: two model points 120 mm
    // apart must land 60 mm apart in the 1:2 view and 120 mm apart in the 1:1
    // one. toSheet maps a model point through the view's own projection.
    const auto paperSpan = [&](ViewId view) {
        const auto a = drawing::toSheet(d.document(), view, reference::detail::pointMm(0.0, 0.0, 0.0),
                                        d.bodies(), d.transforms());
        const auto b = drawing::toSheet(d.document(), view, reference::detail::pointMm(kLength, 0.0, 0.0),
                                        d.bodies(), d.transforms());
        INFO(why(a) << why(b));
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        return std::hypot(b->x.in(units::mm) - a->x.in(units::mm),
                          b->y.in(units::mm) - a->y.in(units::mm));
    };
    CHECK_THAT(paperSpan(m.front), WithinAbs(kLength / 2.0, kPaperMm));
    CHECK_THAT(paperSpan(m.top), WithinAbs(kLength, kPaperMm));

    // The pen and the lettering are unchanged by any of it.
    const drawing::DrawingScene scene = sceneOf(d, m.sheet);
    for (const drawing::SceneText& text : scene.items.texts) {
        INFO("text '" << text.text << "'");
        CHECK(text.height.in(units::mm) > 0.0);
        CHECK(text.height.in(units::mm) < 10.0);
    }
    for (const drawing::SceneLine& line : scene.items.lines) {
        const double width = line.width.in(units::mm);
        CHECK((width == drawing::weights::kThin.in(units::mm)
               || width == drawing::weights::kThick.in(units::mm)));
    }
}

TEST_CASE("DrawingReference_AngleBracketHalfScaleSurvivesIntoAllThreeFiles",
          "[reference][drawing][rm-dwg-02][export]") {
    // THE SCALE, READ BACK OUT OF THE FILES. The front view is 1:2, so the
    // bracket's 120 mm bottom edge is 60.000 mm of paper -- and that is what
    // a plotter will draw, so it is what has to be in the file rather than
    // what the scene believed.
    //
    // The two ends of that edge are model points; where they land on the
    // sheet comes from the view's own projection, and the files are then
    // searched for those coordinates with parsers that share no code with the
    // writers.
    reference::DrawnAngleBracketModel m = built();
    Drawn d{std::move(m.document)};
    INFO(d.why());
    REQUIRE(d.succeeded());

    const auto sheetPoint = [&](double xMm) {
        auto at = drawing::toSheet(d.document(), m.front, reference::detail::pointMm(xMm, 0.0, 0.0),
                                   d.bodies(), d.transforms());
        INFO(why(at));
        REQUIRE(at.has_value());
        return std::pair<double, double>{at->x.in(units::mm), at->y.in(units::mm)};
    };
    const auto [x0, y0] = sheetPoint(0.0);
    const auto [x1, y1] = sheetPoint(kLength);
    // Computed here, from the model length and the scale, and not asked of
    // the view: 120 mm at 1:2 is 60 mm of paper.
    CHECK_THAT(std::hypot(x1 - x0, y1 - y0), WithinAbs(kLength / 2.0, kPaperMm));

    const drawing::DrawingScene scene = sceneOf(d, m.sheet);
    const auto svg = io::svgDocument(scene);
    const auto dxf = io::dxfDocument(scene);
    const auto pdf = io::pdfDocument(scene);
    INFO(why(svg) << why(dxf) << why(pdf));
    REQUIRE(svg.has_value());
    REQUIRE(dxf.has_value());
    REQUIRE(pdf.has_value());

    const double height = scene.height.in(units::mm);

    // --- PDF: the content stream's path points, converted from points back
    // to millimetres by the reader. PDF keeps +Y up, so no flip is expected.
    const std::vector<Point2D> pdfPoints = drawex::pdfPathPoints(drawex::pdfContentStream(*pdf));
    const auto pdfHas = [&](double xMm, double yMm) {
        return std::ranges::any_of(pdfPoints, [&](const Point2D& point) {
            return std::abs(point.x.in(units::mm) - xMm) < 1e-3 &&
                   std::abs(point.y.in(units::mm) - yMm) < 1e-3;
        });
    };
    CHECK(pdfHas(x0, y0));
    CHECK(pdfHas(x1, y1));

    // --- SVG: the same two points, with y flipped once, as the writer says
    // it does. Finding them UNflipped would be the bug that flip exists to
    // avoid, so both are checked.
    const std::vector<drawex::SvgElement> elements = drawex::readSvg(*svg);
    std::vector<std::pair<double, double>> svgPoints;
    for (const drawex::SvgElement* line : drawex::allSvg(elements, "polyline")) {
        const auto found = line->attributes.find("points");
        if (found == line->attributes.end()) {
            continue;
        }
        std::istringstream pairs{found->second};
        for (std::string pair; pairs >> pair;) {
            const std::size_t comma = pair.find(',');
            REQUIRE(comma != std::string::npos);
            svgPoints.emplace_back(std::stod(pair.substr(0, comma)),
                                   std::stod(pair.substr(comma + 1)));
        }
    }
    const auto svgHas = [&](double xMm, double yMm) {
        return std::ranges::any_of(svgPoints, [&](const std::pair<double, double>& point) {
            return std::abs(point.first - xMm) < 1e-3 && std::abs(point.second - yMm) < 1e-3;
        });
    };
    CHECK(svgHas(x0, height - y0));
    CHECK(svgHas(x1, height - y1));
    CHECK_FALSE(svgHas(x0, y0));

    // --- DXF: LINE and LWPOLYLINE vertices, +Y up like the sheet.
    std::vector<std::pair<double, double>> dxfPoints;
    for (const drawex::DxfEntity& entity : drawex::dxfEntities(*dxf)) {
        for (const drawex::DxfPair& pair : entity.pairs) {
            if (pair.code != 10 && pair.code != 11) {
                continue;
            }
            // The matching y follows its x.
            const auto y = [&]() -> std::optional<double> {
                bool seen = false;
                for (const drawex::DxfPair& candidate : entity.pairs) {
                    if (&candidate == &pair) {
                        seen = true;
                        continue;
                    }
                    if (seen && candidate.code == pair.code + 10) {
                        return std::stod(candidate.value);
                    }
                }
                return std::nullopt;
            }();
            if (y) {
                dxfPoints.emplace_back(std::stod(pair.value), *y);
            }
        }
    }
    const auto dxfHas = [&](double xMm, double yMm) {
        return std::ranges::any_of(dxfPoints, [&](const std::pair<double, double>& point) {
            return std::abs(point.first - xMm) < 1e-3 && std::abs(point.second - yMm) < 1e-3;
        });
    };
    CHECK(dxfHas(x0, y0));
    CHECK(dxfHas(x1, y1));

    // And the page is A3 landscape in every one of them: 420 x 297 mm, which
    // is 1190.5512 x 841.8898 pt.
    CHECK_THAT(scene.width.in(units::mm), WithinAbs(420.0, 1e-9));
    const std::vector<double> box = drawex::pdfMediaBox(*pdf);
    REQUIRE(box.size() == 4);
    CHECK_THAT(box[2], WithinAbs(420.0 * 72.0 / 25.4, 1e-3));
    CHECK_THAT(box[3], WithinAbs(297.0 * 72.0 / 25.4, 1e-3));
}

TEST_CASE("DrawingReference_AngleBracketSecondSheetShowsTheSlantSquareOn",
          "[reference][drawing][rm-dwg-02][auxiliary]") {
    // THE VIEW NO STANDARD DIRECTION GIVES. The 45 degree face is
    // foreshortened in every one of the six orthographic views, so its true
    // shape is only visible looking along its own normal.
    //
    // The slant runs from (120, 20) to (80, 60) in the sketch plane and the
    // body is 50 deep, so square-on it is a rectangle of
    //
    //     40 * sqrt(2) = 56.568542494923804 mm   along the slant
    //     50 mm                                  along the extrude
    //
    // Both computed here from the profile the builder is defined by.
    reference::DrawnAngleBracketModel m = built();
    Drawn d{std::move(m.document)};
    INFO(d.why());
    REQUIRE(d.succeeded());

    // Two sheets, in two formats and two orientations.
    const std::vector<SheetId> sheets = drawing::sheets(d.document());
    REQUIRE(sheets.size() == 2);
    const drawing::Sheet* first = drawing::findSheet(d.document(), m.sheet);
    const drawing::Sheet* second = drawing::findSheet(d.document(), m.secondSheet);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(first->definition().format == drawing::SheetFormat::A3);
    CHECK(first->definition().orientation == drawing::SheetOrientation::Landscape);
    CHECK(second->definition().format == drawing::SheetFormat::A4);
    CHECK(second->definition().orientation == drawing::SheetOrientation::Portrait);
    // A4 portrait is 210 x 297, which the scene has to be the size of.
    const drawing::DrawingScene sheet2 = sceneOf(d, m.secondSheet);
    CHECK_THAT(sheet2.width.in(units::mm), WithinAbs(210.0, 1e-9));
    CHECK_THAT(sheet2.height.in(units::mm), WithinAbs(297.0, 1e-9));
    // And the sheets are numbered, which is what a title block shows.
    CHECK(drawing::sheetCount(d.document()) == 2);
    CHECK(drawing::sheetNumber(d.document(), m.sheet) == 1);
    CHECK(drawing::sheetNumber(d.document(), m.secondSheet) == 2);

    const drawing::View* auxiliary = drawing::findView(d.document(), m.auxiliary);
    REQUIRE(auxiliary != nullptr);
    CHECK(auxiliary->definition().kind == drawing::ViewKind::Auxiliary);

    // The slant is seen TRUE SIZE. Its two model extents are computed above;
    // at the sheet's 1:2 they draw at half that, and the projected extent is
    // measured off the view rather than asked of the scale.
    auto drawn = drawing::projectedGeometry(d.document(), m.auxiliary, d.bodies(), d.transforms());
    INFO(why(drawn));
    REQUIRE(drawn.has_value());
    const double across = (drawn->bounds.max.x - drawn->bounds.min.x).in(units::mm);
    const double along = (drawn->bounds.max.y - drawn->bounds.min.y).in(units::mm);

    // THE VIEW'S OWN AXES. The reference direction becomes the view's X, so
    // the extrude's depth runs ACROSS the sheet and the profile runs up it.
    //
    //   across = the 50 mm depth                                -> 25 at 1:2
    //   along  = the profile's extent perpendicular to the view
    //            normal, which is along (-1, 1, 0)/sqrt(2):
    //            t = (y - x)/sqrt(2) over the five profile
    //            vertices (0,0) (120,0) (120,20) (80,60) (0,60)
    //            runs from -120/sqrt(2) to +60/sqrt(2), so
    //            180/sqrt(2) = 127.27922061357856 mm            -> 63.639... at 1:2
    //
    // Both derived here from the profile the builder is defined by. The slant
    // FACE itself is 40*sqrt(2) = 56.568542494923804 mm of that extent, and
    // its other side -- the 50 mm depth -- is what the aligned dimension
    // below measures.
    constexpr double kProfileExtentMm = 180.0 / std::numbers::sqrt2;
    static_assert(kCutback * std::numbers::sqrt2 < kProfileExtentMm,
                  "the slant face lies within the part's extent in this view");
    CHECK_THAT(across, WithinAbs(kWidth / 2.0, kPaperMm));
    CHECK_THAT(along, WithinAbs(kProfileExtentMm / 2.0, kPaperMm));

    // 50.000 mm, the bracket's width, as an ALIGNED dimension. Aligned
    // measures the separation AS DRAWN: here it lies in the view plane, so it
    // reads the true distance, and a view that had foreshortened the width
    // would report less.
    CHECK_THAT(measuredMm(d, m.acrossSlant), WithinAbs(kWidth, 1e-9));
}

TEST_CASE("DrawingReference_AngleBracketSheetsAreDrawnAndExportedSeparately",
          "[reference][drawing][rm-dwg-02][export]") {
    // TWO SHEETS, TWO FILES. Each is a page of its own size with its own
    // contents; nothing from one may appear on the other, and a writer that
    // drew the document rather than the sheet would produce two identical
    // files.
    reference::DrawnAngleBracketModel m = built();
    Drawn d{std::move(m.document)};
    REQUIRE(d.succeeded());

    const drawing::DrawingScene a = sceneOf(d, m.sheet);
    const drawing::DrawingScene b = sceneOf(d, m.secondSheet);
    CHECK_THAT(a.width.in(units::mm), WithinAbs(420.0, 1e-9));
    CHECK_THAT(b.width.in(units::mm), WithinAbs(210.0, 1e-9));
    CHECK_FALSE(sameScene(a, b));

    const auto svgA = io::svgDocument(a);
    const auto svgB = io::svgDocument(b);
    REQUIRE(svgA.has_value());
    REQUIRE(svgB.has_value());
    CHECK(*svgA != *svgB);
    CHECK_THAT(*svgA, Catch::Matchers::ContainsSubstring("width=\"420.0000mm\""));
    CHECK_THAT(*svgB, Catch::Matchers::ContainsSubstring("width=\"210.0000mm\""));

    // The note lives on sheet 1 and must not appear on sheet 2.
    const std::vector<std::string> textsA = sceneTexts(a);
    const std::vector<std::string> textsB = sceneTexts(b);
    CHECK(std::ranges::find(textsA, "FRONT VIEW SCALE 1:2") != textsA.end());
    CHECK(std::ranges::find(textsB, "FRONT VIEW SCALE 1:2") == textsB.end());
    // And sheet 2 carries the dimension that is only on it.
    CHECK(std::ranges::find(textsB, "50.00") != textsB.end());
    CHECK(std::ranges::find(textsA, "50.00") == textsA.end());
}
