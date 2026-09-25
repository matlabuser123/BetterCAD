#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "io/DrawingExportSupport.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/drawing/Scene.hpp>
#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Resolution.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/drawing/Annotations.hpp>
#include <bettercad/drawing/Bom.hpp>
#include <bettercad/drawing/Dimensions.hpp>
#include <bettercad/drawing/Regeneration.hpp>
#include <bettercad/drawing/Section.hpp>
#include <bettercad/drawing/SheetScene.hpp>
#include <bettercad/drawing/Sheets.hpp>
#include <bettercad/drawing/Tolerance.hpp>
#include <bettercad/drawing/Views.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/io/DrawingExport.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <charconv>
#include <clocale>
#include <limits>
#include <numbers>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using drawing::DrawingScene;
using drawing::LineStyle;
using drawing::SceneArc;
using drawing::SceneItems;
using drawing::SceneLine;
using drawing::SceneText;
using drawing::TextAnchor;
using namespace bettercad::test::drawex;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

// P14-EXPORT-001: a drawing written out, and read back by something else.
//
// THE RULE THIS FILE ENFORCES. A writer TRANSCRIBES a scene. So every check
// below reads the GENERATED FILE with a parser written here -- not by asking
// the writer what it thinks it wrote. A writer agreeing with itself proves
// nothing; the mistakes worth catching are serialization mistakes, and those
// are only visible from the outside.
//
// The three parsers below are deliberately small and deliberately independent:
// a group-code reader for DXF, an attribute scraper for SVG, an object and
// content-stream reader for PDF. None of them shares a line of code with the
// writers.
//
// THE COORDINATE TRAP. The scene's origin is the sheet's bottom-left with +Y
// UP. PDF and DXF agree; SVG's y grows DOWN and its writer flips. So the
// fixture is deliberately ASYMMETRIC -- a line low on the page, text high on
// it, a circle to the right -- because a forgotten or doubled flip cannot hide
// in a symmetric drawing.
namespace {

// --- The analytic fixture (the brief's own) ------------------------------------------------------
//
// A4 landscape, 297 x 210 mm, with four things at coordinates chosen so that
// every one of them would move if a transform were wrong.

constexpr double kPageWidth = 297.0;
constexpr double kPageHeight = 210.0;

DrawingScene analyticScene() {
    DrawingScene scene{.width = Length::fromSi(kPageWidth * 1e-3),
                       .height = Length::fromSi(kPageHeight * 1e-3)};
    // A 100 mm line along the bottom: (20,20) -> (120,20).
    scene.items.lines.push_back(SceneLine{.points = {Point2D{20_mm, 20_mm}, Point2D{120_mm, 20_mm}},
                                          .style = LineStyle::Continuous,
                                          .width = drawing::weights::kThick});
    // A hidden line 20 mm above it.
    scene.items.lines.push_back(SceneLine{.points = {Point2D{20_mm, 40_mm}, Point2D{120_mm, 40_mm}},
                                          .style = LineStyle::Dashed,
                                          .width = drawing::weights::kThin});
    // A circle of radius 10 at (200,100).
    scene.items.arcs.push_back(SceneArc{.centre = Point2D{200_mm, 100_mm},
                                        .radius = 10_mm,
                                        .start = Angle::fromSi(0.0),
                                        .sweep = Angle::fromSi(2.0 * std::numbers::pi),
                                        .style = LineStyle::Continuous,
                                        .width = drawing::weights::kThick});
    // Text high on the page, where a vertical flip would put it at y = 30.
    scene.items.texts.push_back(SceneText{.at = Point2D{30_mm, 180_mm},
                                          .text = "TEST",
                                          .height = 5_mm,
                                          .anchor = TextAnchor::BaselineLeft});
    return scene;
}

} // namespace

// --- The scene contract ------------------------------------------------------------------------------

TEST_CASE("Export_TheSceneRefusesWhatNoWriterCouldDrawHonestly", "[io][export][p14][scene]") {
    // ADR-016 puts the check in `drawing`, ONCE, so that three writers inherit
    // it rather than each trusting or re-deriving it.
    DrawingScene scene = analyticScene();
    REQUIRE(validate(scene).has_value());

    const auto refuse = [](DrawingScene broken, std::string_view says) {
        const auto valid = validate(broken);
        std::string why = "it was accepted";
        if (!valid) {
            why = valid.error().message;
        }
        INFO(why);
        REQUIRE_FALSE(valid.has_value());
        CHECK_THAT(valid.error().message, ContainsSubstring(std::string{says}));
    };

    DrawingScene noPage = scene;
    noPage.width = Length::fromSi(0.0);
    refuse(noPage, "greater than zero");

    DrawingScene nan = scene;
    nan.items.lines.front().points.front() = Point2D{Length::fromSi(std::nan("")), 20_mm};
    refuse(nan, "not finite");

    DrawingScene infinite = scene;
    infinite.items.arcs.front().radius =
        Length::fromSi(std::numeric_limits<double>::infinity());
    refuse(infinite, "finite");

    DrawingScene negativeWidth = scene;
    negativeWidth.items.lines.front().width = Length::fromSi(-0.001);
    refuse(negativeWidth, "width");

    DrawingScene emptyText = scene;
    emptyText.items.texts.front().text.clear();
    refuse(emptyText, "nothing in it");

    DrawingScene zeroHeight = scene;
    zeroHeight.items.texts.front().height = Length::fromSi(0.0);
    refuse(zeroHeight, "height");

    DrawingScene onePoint = scene;
    onePoint.items.lines.front().points.resize(1);
    refuse(onePoint, "two points");

    DrawingScene noSweep = scene;
    noSweep.items.arcs.front().sweep = Angle::fromSi(0.0);
    refuse(noSweep, "sweeps nothing");

    // OFF THE PAGE, which is what a forgotten flip looks like.
    DrawingScene belowPage = scene;
    belowPage.items.texts.front().at = Point2D{30_mm, Length::fromSi(-0.03)};
    refuse(belowPage, "off a");

    DrawingScene pastPage = scene;
    pastPage.items.lines.front().points.back() = Point2D{400_mm, 20_mm};
    refuse(pastPage, "off a");

    // A circle centred on the page can still run off it, so the EXTENT is
    // what is checked and not the centre.
    DrawingScene bigCircle = scene;
    bigCircle.items.arcs.front().radius = 150_mm;
    refuse(bigCircle, "off a");
}

TEST_CASE("Export_EveryWriterRefusesAnInvalidSceneRatherThanWritingIt", "[io][export][p14][scene]") {
    DrawingScene broken = analyticScene();
    broken.items.texts.front().text.clear();
    CHECK_FALSE(io::svgDocument(broken).has_value());
    CHECK_FALSE(io::dxfDocument(broken).has_value());
    CHECK_FALSE(io::pdfDocument(broken).has_value());
}

// --- DXF ----------------------------------------------------------------------------------------------

TEST_CASE("Export_TheDxfHoldsTheFixtureAtItsOwnCoordinates", "[io][export][p14][dxf]") {
    const auto text = io::dxfDocument(analyticScene());
    REQUIRE(text.has_value());

    // Units said, not assumed: 4 is millimetres. Leaving this out is how a
    // 297 mm sheet arrives somewhere as 297 inches.
    CHECK(dxfHeader(*text, "$INSUNITS") == std::optional<std::string>{"4"});
    CHECK(dxfHeader(*text, "$ACADVER") == std::optional<std::string>{"AC1009"});
    const auto extMax = dxfHeader(*text, "$EXTMAX");
    REQUIRE(extMax.has_value());
    CHECK_THAT(std::stod(*extMax), WithinAbs(kPageWidth, kMm));

    const std::vector<DxfEntity> entities = dxfEntities(*text);
    REQUIRE(entities.size() == 4);

    // The 100 mm line, at the coordinates the fixture put it at. DXF agrees
    // with the scene, so nothing is flipped and nothing is scaled.
    const DxfEntity& solid = entities[0];
    CHECK(solid.type == "LINE");
    CHECK(solid.value(8) == std::optional<std::string>{"VISIBLE"});
    CHECK(solid.value(6) == std::optional<std::string>{"CONTINUOUS"});
    CHECK_THAT(solid.number(10), WithinAbs(20.0, kMm));
    CHECK_THAT(solid.number(20), WithinAbs(20.0, kMm));
    CHECK_THAT(solid.number(11), WithinAbs(120.0, kMm));
    CHECK_THAT(solid.number(21), WithinAbs(20.0, kMm));
    CHECK_THAT(solid.number(11) - solid.number(10), WithinAbs(100.0, kMm)); // 100 mm, measured
    CHECK(solid.value(370) == std::optional<std::string>{"50"});            // 0.50 mm, in hundredths

    // The hidden line is DASHED and on its own layer, so a receiving package
    // can turn it off.
    const DxfEntity& hidden = entities[1];
    CHECK(hidden.type == "LINE");
    CHECK(hidden.value(8) == std::optional<std::string>{"HIDDEN"});
    CHECK(hidden.value(6) == std::optional<std::string>{"DASHED"});
    CHECK_THAT(hidden.number(20), WithinAbs(40.0, kMm));
    CHECK(hidden.value(370) == std::optional<std::string>{"25"}); // 0.25 mm

    // The circle is a CIRCLE, not a hundred tiny segments.
    const DxfEntity& circle = entities[2];
    CHECK(circle.type == "CIRCLE");
    CHECK_THAT(circle.number(10), WithinAbs(200.0, kMm));
    CHECK_THAT(circle.number(20), WithinAbs(100.0, kMm));
    CHECK_THAT(circle.number(40), WithinAbs(10.0, kMm));

    // The text, at its own point, with its own height.
    const DxfEntity& label = entities[3];
    CHECK(label.type == "TEXT");
    CHECK(label.value(1) == std::optional<std::string>{"TEST"});
    CHECK_THAT(label.number(10), WithinAbs(30.0, kMm));
    CHECK_THAT(label.number(20), WithinAbs(180.0, kMm)); // HIGH on the page
    CHECK_THAT(label.number(40), WithinAbs(5.0, kMm));

    // The linetypes the entities name are actually defined in the file.
    CHECK_THAT(*text, ContainsSubstring("DASHED"));
    CHECK_THAT(*text, ContainsSubstring("CENTER"));
}

// --- SVG ----------------------------------------------------------------------------------------------

TEST_CASE("Export_TheSvgHoldsTheFixtureWithItsYAxisFlippedExactlyOnce",
          "[io][export][p14][svg]") {
    const auto text = io::svgDocument(analyticScene());
    REQUIRE(text.has_value());
    const std::vector<SvgElement> elements = readSvg(*text);

    const SvgElement* root = findSvg(elements, "svg");
    REQUIRE(root != nullptr);
    // PHYSICAL size, in millimetres, so the drawing is the size it says.
    CHECK(root->attributes.at("width") == "297.0000mm");
    CHECK(root->attributes.at("height") == "210.0000mm");
    CHECK(root->attributes.at("viewBox") == "0 0 297.0000 210.0000");

    const std::vector<const SvgElement*> polylines = allSvg(elements, "polyline");
    REQUIRE(polylines.size() == 2);
    // THE FLIP, measured: the scene puts this line at y = 20 from the BOTTOM,
    // and SVG counts from the top, so it must be at 210 - 20 = 190.
    CHECK(polylines[0]->attributes.at("points") == "20.0000,190.0000 120.0000,190.0000");
    CHECK(polylines[0]->attributes.at("stroke-width") == "0.5000");
    CHECK_FALSE(polylines[0]->attributes.contains("stroke-dasharray"));

    // The hidden line is dashed, and 20 mm nearer the bottom means 20 less
    // in SVG's y: 210 - 40 = 170.
    CHECK(polylines[1]->attributes.at("points") == "20.0000,170.0000 120.0000,170.0000");
    CHECK(polylines[1]->attributes.at("stroke-dasharray") == "4.0000 2.0000");
    CHECK(polylines[1]->attributes.at("stroke-width") == "0.2500");

    // A circle is a <circle>, and its centre flips like everything else.
    const std::vector<const SvgElement*> circles = allSvg(elements, "circle");
    REQUIRE(circles.size() == 1);
    CHECK(circles[0]->attributes.at("cx") == "200.0000");
    CHECK(circles[0]->attributes.at("cy") == "110.0000"); // 210 - 100
    CHECK(circles[0]->attributes.at("r") == "10.0000");

    // Text high on the page lands LOW in SVG's y: 210 - 180 = 30. If the flip
    // were missing this would read 180, and if it were doubled, 180 again --
    // which is why the fixture puts the text where those differ from 30.
    const std::vector<const SvgElement*> texts = allSvg(elements, "text");
    REQUIRE(texts.size() == 1);
    CHECK(texts[0]->attributes.at("x") == "30.0000");
    CHECK(texts[0]->attributes.at("y") == "30.0000");
    CHECK(texts[0]->attributes.at("font-size") == "5.0000");
    CHECK(texts[0]->body == "TEST");

    // Lines are stroked and nothing is filled: a default fill would flood
    // every closed polyline in the drawing.
    const SvgElement* group = findSvg(elements, "g");
    REQUIRE(group != nullptr);
    CHECK(group->attributes.at("fill") == "none");
    CHECK(group->attributes.at("stroke") == "black");
}

TEST_CASE("Export_TheSvgEscapesTextRatherThanBreakingTheDocument", "[io][export][p14][svg]") {
    DrawingScene scene = analyticScene();
    scene.items.texts.front().text = R"(A & B < C > D " E ' F)";
    const auto text = io::svgDocument(scene);
    REQUIRE(text.has_value());
    CHECK_THAT(*text, ContainsSubstring("A &amp; B &lt; C &gt; D"));
    // The raw characters are not there to break the XML.
    const std::vector<SvgElement> elements = readSvg(*text);
    REQUIRE(findSvg(elements, "svg") != nullptr);
    CHECK(allSvg(elements, "text").size() == 1);
}

// --- Text encoding --------------------------------------------------------------------------------
//
// The scene holds UTF-8. SVG declares that and takes the bytes; PDF and DXF
// are SINGLE-BYTE formats and must transcode. Getting that wrong is a silent,
// plausible-looking corruption, so every byte is stated explicitly here rather
// than left to this file's own encoding.

namespace encoding {
// UTF-8 for the characters a dimensioned drawing really carries.
constexpr std::string_view kDiameter = "\xC3\x98";          // U+00D8, a diameter callout
constexpr std::string_view kPlusMinus = "\xC2\xB1";         // U+00B1, a symmetric tolerance
constexpr std::string_view kDegree = "\xC2\xB0";            // U+00B0, an angular dimension
constexpr std::string_view kPerpendicular = "\xE2\x9F\x82"; // U+27C2, an ISO 1101 symbol

// The ONE byte WinAnsi gives each of the first three.
constexpr char kDiameterByte = '\xD8';
constexpr char kPlusMinusByte = '\xB1';
constexpr char kDegreeByte = '\xB0';

[[nodiscard]] inline std::string callout() {
    return std::string{kDiameter} + "20 " + std::string{kPlusMinus} + "0.05 at 30" +
           std::string{kDegree};
}

/// The one TEXT entity of a scene that has one.
[[nodiscard]] inline std::string dxfText(std::string_view document) {
    const std::vector<DxfEntity> entities = dxfEntities(document);
    const DxfEntity* found = nullptr;
    for (const DxfEntity& entity : entities) {
        if (entity.type == "TEXT") {
            REQUIRE(found == nullptr); // exactly one, so the check is unambiguous
            found = &entity;
        }
    }
    REQUIRE(found != nullptr);
    const auto value = found->value(1);
    REQUIRE(value.has_value());
    return *value;
}
} // namespace encoding

TEST_CASE("Export_TextIsTranscodedIntoEachFormatsOwnEncodingRatherThanCopiedAsBytes",
          "[io][export][p14][text]") {
    // A REGRESSION. The writers escaped the scene's UTF-8 BYTES, so a reader
    // applying /WinAnsiEncoding showed "\303\230 20" as two glyphs where the
    // drawing said one: a diameter callout reading something else. Found by
    // adversarial review, because no test had put a non-ASCII character
    // through a writer.
    using namespace encoding;
    DrawingScene scene = analyticScene();
    scene.items.texts.front().text = callout();

    SECTION("SVG keeps the UTF-8 it declares") {
        const auto svg = io::svgDocument(scene);
        REQUIRE(svg.has_value());
        CHECK_THAT(*svg, ContainsSubstring("encoding=\"UTF-8\""));
        // Carried through untouched: the document says UTF-8 and means it.
        const std::vector<SvgElement> elements = readSvg(*svg);
        const std::vector<const SvgElement*> texts = allSvg(elements, "text");
        REQUIRE(texts.size() == 1);
        CHECK(texts.front()->body == callout());
    }

    SECTION("DXF transcodes into the code page it declares") {
        const auto dxf = io::dxfDocument(scene);
        REQUIRE(dxf.has_value());
        // The declaration is what makes the bytes readable at all: without it
        // a reader falls back on its own locale's page.
        CHECK(dxfHeader(*dxf, "$DWGCODEPAGE") == std::optional<std::string>{"ANSI_1252"});

        const std::string text = dxfText(*dxf);
        const std::string expected = std::string{kDiameterByte} + "20 " +
                                     std::string{kPlusMinusByte} + "0.05 at 30" +
                                     std::string{kDegreeByte};
        CHECK(text == expected);
        // One byte per character: three characters shorter than the UTF-8.
        CHECK(text.size() == callout().size() - 3);
        // The UTF-8 lead bytes are not in the file.
        CHECK(text.find('\xC3') == std::string::npos);
        CHECK(text.find('\xC2') == std::string::npos);
    }

    SECTION("PDF escapes the WinAnsi byte, not the UTF-8 pair") {
        const auto pdf = io::pdfDocument(scene);
        REQUIRE(pdf.has_value());
        const std::string content = pdfContentStream(*pdf);
        CHECK_THAT(*pdf, ContainsSubstring("/Encoding /WinAnsiEncoding"));
        // 0xD8 is 330 octal, 0xB1 is 261, 0xB0 is 260.
        CHECK_THAT(content, ContainsSubstring("(\\33020 \\2610.05 at 30\\260) Tj"));
        // The corruption this test exists for: the two UTF-8 bytes, escaped.
        CHECK_THAT(content, !ContainsSubstring("\\303\\230"));
        CHECK_THAT(content, !ContainsSubstring("\\302\\261"));
    }
}

TEST_CASE("Export_ASymbolNoSingleByteEncodingCanCarryIsVisiblyMissingNotQuietlyWrong",
          "[io][export][p14][text]") {
    // WinAnsi has no GD&T symbol, and neither has any of PDF's 14 standard
    // fonts -- so no encoding choice reaches them and only an embedded font
    // would, which is a font subsystem and not this milestone. The rule is
    // that a character which cannot be written becomes '?', which nobody
    // reads as a tolerance, rather than a glyph that looks like one.
    //
    // A KNOWN LIMITATION, pinned here so it cannot change quietly.
    using namespace encoding;
    DrawingScene scene = analyticScene();
    scene.items.texts.front().text = std::string{kPerpendicular} + " 0.1 A";

    const auto svg = io::svgDocument(scene);
    REQUIRE(svg.has_value());
    const std::vector<const SvgElement*> texts = allSvg(readSvg(*svg), "text");
    REQUIRE(texts.size() == 1);
    CHECK(texts.front()->body == std::string{kPerpendicular} + " 0.1 A"); // SVG carries it

    // One '?' per CHARACTER, not per byte: a three-byte symbol is one missing
    // glyph and not three.
    const auto dxf = io::dxfDocument(scene);
    REQUIRE(dxf.has_value());
    CHECK(dxfText(*dxf) == "? 0.1 A");

    const auto pdf = io::pdfDocument(scene);
    REQUIRE(pdf.has_value());
    CHECK_THAT(pdfContentStream(*pdf), ContainsSubstring("(? 0.1 A) Tj"));
}

TEST_CASE("Export_MalformedTextCannotBreakTheStructureOfAFile", "[io][export][p14][text]") {
    // A writer produces a well-formed file from any input. Malformed UTF-8,
    // and a newline -- which in DXF would corrupt not the text but the LINE
    // STRUCTURE, since the format is a list of lines and every pair after it
    // would be read one line out.
    using namespace encoding;
    DrawingScene scene = analyticScene();
    scene.items.texts.front().text = std::string{"A\xC3"} + "B\nC\xFF" + "D";

    const auto dxf = io::dxfDocument(scene);
    REQUIRE(dxf.has_value());
    CHECK(dxfText(*dxf) == "A?B?C?D");
    // The entity's later codes still parse, which is the proof that the file
    // did not shift by a line.
    const std::vector<DxfEntity> entities = dxfEntities(*dxf);
    for (const DxfEntity& entity : entities) {
        if (entity.type == "TEXT") {
            CHECK(entity.value(40).has_value()); // height
            CHECK(entity.value(50).has_value()); // rotation
        }
    }

    const auto pdf = io::pdfDocument(scene);
    REQUIRE(pdf.has_value());
    CHECK_THAT(pdfContentStream(*pdf), ContainsSubstring("(A?B?C?D) Tj"));

    const auto svg = io::svgDocument(scene);
    REQUIRE(svg.has_value());
    CHECK(allSvg(readSvg(*svg), "text").size() == 1);
}

// --- PDF ----------------------------------------------------------------------------------------------

TEST_CASE("Export_ThePdfPageIsTheSheetInPointsAndItsGeometryIsVector",
          "[io][export][p14][pdf]") {
    const auto text = io::pdfDocument(analyticScene());
    REQUIRE(text.has_value());
    CHECK_THAT(*text, ContainsSubstring("%PDF-1.4"));

    // THE PAGE BOX, read from the file and compared with the conversion done
    // HERE rather than by the writer's own helper: 72 points to the inch.
    const std::vector<double> box = pdfMediaBox(*text);
    REQUIRE(box.size() == 4);
    CHECK_THAT(box[0], WithinAbs(0.0, 1e-4));
    CHECK_THAT(box[1], WithinAbs(0.0, 1e-4));
    CHECK_THAT(box[2], WithinAbs(297.0 * 72.0 / 25.4, 1e-3)); // 841.8898 pt
    CHECK_THAT(box[3], WithinAbs(210.0 * 72.0 / 25.4, 1e-3)); // 595.2756 pt
    // A4's figures, stated independently of any helper.
    CHECK_THAT(box[2], WithinAbs(841.8898, 1e-3));
    CHECK_THAT(box[3], WithinAbs(595.2756, 1e-3));

    const std::string content = pdfContentStream(*text);
    // VECTOR, not a picture: path operators are there and no image is.
    CHECK_THAT(content, ContainsSubstring(" m\n"));
    CHECK_THAT(content, ContainsSubstring(" l\n"));
    CHECK_THAT(content, ContainsSubstring("S\n"));
    CHECK_THAT(content, ContainsSubstring(" c\n")); // the circle, as Béziers
    CHECK_THAT(*text, !ContainsSubstring("/Image"));
    CHECK_THAT(*text, !ContainsSubstring("/DCTDecode"));
    CHECK_THAT(*text, !ContainsSubstring("/XObject"));

    // The 100 mm line, measured back through the points, in millimetres.
    const std::vector<Point2D> points = pdfPathPoints(content);
    REQUIRE(points.size() >= 4);
    CHECK_THAT(points[0].x.in(units::mm), WithinAbs(20.0, 1e-3));
    CHECK_THAT(points[0].y.in(units::mm), WithinAbs(20.0, 1e-3)); // PDF's y is UP, like the scene
    CHECK_THAT(points[1].x.in(units::mm), WithinAbs(120.0, 1e-3));
    CHECK_THAT((points[1].x - points[0].x).in(units::mm), WithinAbs(100.0, 1e-3));
    // The hidden line, 20 mm higher, not 20 mm lower.
    CHECK_THAT(points[2].y.in(units::mm), WithinAbs(40.0, 1e-3));

    // The dash pattern is in POINTS, and 4 mm is 11.3386 pt.
    CHECK_THAT(content, ContainsSubstring("11.3386"));
    // The text is there, as text.
    CHECK_THAT(content, ContainsSubstring("(TEST) Tj"));
    CHECK_THAT(*text, ContainsSubstring("/Helvetica"));

    // The cross-reference table points at real objects.
    CHECK_THAT(*text, ContainsSubstring("xref"));
    CHECK_THAT(*text, ContainsSubstring("startxref"));
    CHECK_THAT(*text, ContainsSubstring("%%EOF"));
}

TEST_CASE("Export_ThePdfCarriesNothingThatVariesBetweenRuns", "[io][export][p14][pdf]") {
    // The usual reason a PDF is not reproducible: a creation date, a producer
    // string, a document ID. This one has none, by choice.
    const auto text = io::pdfDocument(analyticScene());
    REQUIRE(text.has_value());
    CHECK_THAT(*text, !ContainsSubstring("/CreationDate"));
    CHECK_THAT(*text, !ContainsSubstring("/ModDate"));
    CHECK_THAT(*text, !ContainsSubstring("/Producer"));
    CHECK_THAT(*text, !ContainsSubstring("/Creator"));
    CHECK_THAT(*text, !ContainsSubstring("/ID"));
}

// --- Determinism ----------------------------------------------------------------------------------------

TEST_CASE("Export_EveryFormatIsByteIdenticalForTheSameScene",
          "[io][export][p14][determinism]") {
    // The gate, at its strongest setting: not "structurally the same" but the
    // same bytes, for all three.
    const DrawingScene scene = analyticScene();
    for (int run = 0; run < 5; ++run) {
        INFO("run " << run);
        CHECK(*io::svgDocument(scene) == *io::svgDocument(scene));
        CHECK(*io::dxfDocument(scene) == *io::dxfDocument(scene));
        CHECK(*io::pdfDocument(scene) == *io::pdfDocument(scene));
    }
    // And against a separately built copy of the same scene, so the answer
    // cannot come from anything remembered.
    const DrawingScene again = analyticScene();
    CHECK(*io::svgDocument(scene) == *io::svgDocument(again));
    CHECK(*io::dxfDocument(scene) == *io::dxfDocument(again));
    CHECK(*io::pdfDocument(scene) == *io::pdfDocument(again));
}

TEST_CASE("Export_NumbersAreWrittenTheSameWhateverTheLocale", "[io][export][p14][determinism]") {
    // A German locale writes 1,25 for 1.25. Through printf that would put
    // commas into a PDF content stream and into every DXF coordinate.
    const DrawingScene scene = analyticScene();
    const std::string neutralSvg = *io::svgDocument(scene);
    const std::string neutralDxf = *io::dxfDocument(scene);
    const std::string neutralPdf = *io::pdfDocument(scene);

    const char* previous = std::setlocale(LC_ALL, nullptr);
    const std::string saved = previous == nullptr ? std::string{"C"} : std::string{previous};
    const char* applied = nullptr;
    for (const char* name : {"de_DE.UTF-8", "de-DE", "German_Germany.1252", "de_DE"}) {
        applied = std::setlocale(LC_ALL, name);
        if (applied != nullptr) {
            break;
        }
    }
    if (applied == nullptr) {
        WARN("no comma-decimal locale is installed; the locale check did not run");
        std::setlocale(LC_ALL, saved.c_str());
        return;
    }
    INFO("locale in force: " << applied);
    const std::string underLocale = *io::svgDocument(scene);
    const std::string dxfUnder = *io::dxfDocument(scene);
    const std::string pdfUnder = *io::pdfDocument(scene);
    std::setlocale(LC_ALL, saved.c_str());

    CHECK(underLocale == neutralSvg);
    CHECK(dxfUnder == neutralDxf);
    CHECK(pdfUnder == neutralPdf);
    CHECK_THAT(underLocale, !ContainsSubstring("20,0000"));
}

TEST_CASE("Export_NegativeZeroIsWrittenAsZero", "[io][export][p14][determinism]") {
    // -0.0 and 0.0 are the same point and must be the same text, or a
    // coordinate that happened to arrive as -0 would break byte determinism.
    DrawingScene scene = analyticScene();
    scene.items.lines.front().points.front() = Point2D{Length::fromSi(-0.0), Length::fromSi(-0.0)};
    const auto svg = io::svgDocument(scene);
    const auto dxf = io::dxfDocument(scene);
    const auto pdf = io::pdfDocument(scene);
    REQUIRE(svg.has_value());
    CHECK_THAT(*svg, !ContainsSubstring("-0.0000"));
    CHECK_THAT(*dxf, !ContainsSubstring("-0.0000"));
    CHECK_THAT(*pdf, !ContainsSubstring("-0.0000"));
}

// --- Cross-format ------------------------------------------------------------------------------------------

TEST_CASE("Export_TheThreeFormatsPutTheSameGeometryInTheSamePhysicalPlaces",
          "[io][export][p14][cross]") {
    // Not a byte comparison across formats, which would mean nothing: each is
    // read back with its own parser and the PHYSICAL positions are compared.
    const DrawingScene scene = analyticScene();
    const std::string svg = *io::svgDocument(scene);
    const std::string dxf = *io::dxfDocument(scene);
    const std::string pdf = *io::pdfDocument(scene);

    // The page, three ways.
    const std::vector<SvgElement> elements = readSvg(svg);
    CHECK(findSvg(elements, "svg")->attributes.at("width") == "297.0000mm");
    CHECK_THAT(std::stod(*dxfHeader(dxf, "$EXTMAX")), WithinAbs(297.0, kMm));
    CHECK_THAT(pdfMediaBox(pdf)[2] * 25.4 / 72.0, WithinAbs(297.0, 1e-3));

    // The circle, three ways, in millimetres from the bottom-left.
    const std::vector<DxfEntity> entities = dxfEntities(dxf);
    const auto circle = std::ranges::find(entities, "CIRCLE", &DxfEntity::type);
    REQUIRE(circle != entities.end());
    const double dxfCx = circle->number(10);
    const double dxfCy = circle->number(20);
    const std::vector<const SvgElement*> svgCircles = allSvg(elements, "circle");
    REQUIRE(svgCircles.size() == 1);
    const double svgCx = std::stod(svgCircles[0]->attributes.at("cx"));
    const double svgCyFromTop = std::stod(svgCircles[0]->attributes.at("cy"));
    const double svgCy = kPageHeight - svgCyFromTop; // back into scene coordinates
    CHECK_THAT(svgCx, WithinAbs(dxfCx, kMm));
    CHECK_THAT(svgCy, WithinAbs(dxfCy, kMm));
    CHECK_THAT(dxfCx, WithinAbs(200.0, kMm));
    CHECK_THAT(dxfCy, WithinAbs(100.0, kMm));

    // The 100 mm line's length, three ways.
    const auto line = std::ranges::find(entities, "LINE", &DxfEntity::type);
    REQUIRE(line != entities.end());
    CHECK_THAT(line->number(11) - line->number(10), WithinAbs(100.0, kMm));
    const std::vector<const SvgElement*> polylines = allSvg(elements, "polyline");
    CHECK(polylines[0]->attributes.at("points") == "20.0000,190.0000 120.0000,190.0000");
    const std::vector<Point2D> pdfPoints = pdfPathPoints(pdfContentStream(pdf));
    CHECK_THAT((pdfPoints[1].x - pdfPoints[0].x).in(units::mm), WithinAbs(100.0, 1e-3));

    // The text, three ways, at the same physical point.
    const auto label = std::ranges::find(entities, "TEXT", &DxfEntity::type);
    REQUIRE(label != entities.end());
    CHECK_THAT(label->number(10), WithinAbs(30.0, kMm));
    CHECK_THAT(label->number(20), WithinAbs(180.0, kMm));
    const std::vector<const SvgElement*> svgTexts = allSvg(elements, "text");
    CHECK_THAT(std::stod(svgTexts[0]->attributes.at("x")), WithinAbs(30.0, kMm));
    CHECK_THAT(kPageHeight - std::stod(svgTexts[0]->attributes.at("y")), WithinAbs(180.0, kMm));
}

// --- Line weights are paper sizes ----------------------------------------------------------------------------

TEST_CASE("Export_ALineIsTheSameWidthOnPaperWhateverTheDrawingIsOf",
          "[io][export][p14][weights]") {
    // A line weight is a property of the PEN, not of the view's scale. The
    // scene already carries it in paper millimetres; this checks that no
    // writer multiplies it by anything.
    for (const double pageScale : {1.0, 2.0, 0.5}) {
        INFO("a page " << pageScale << " times the size");
        DrawingScene scene = analyticScene();
        // Change the drawing's size, and nothing about the pens.
        for (SceneLine& line : scene.items.lines) {
            for (Point2D& point : line.points) {
                point = Point2D{point.x * pageScale, point.y * pageScale};
            }
        }
        scene.items.arcs.front().centre =
            Point2D{scene.items.arcs.front().centre.x * pageScale,
                    scene.items.arcs.front().centre.y * pageScale};
        scene.items.arcs.front().radius = scene.items.arcs.front().radius * pageScale;
        scene.items.texts.front().at =
            Point2D{scene.items.texts.front().at.x * pageScale,
                    scene.items.texts.front().at.y * pageScale};
        scene.width = scene.width * pageScale;
        scene.height = scene.height * pageScale;

        const auto dxf = io::dxfDocument(scene);
        REQUIRE(dxf.has_value());
        const std::vector<DxfEntity> entities = dxfEntities(*dxf);
        CHECK(entities[0].value(370) == std::optional<std::string>{"50"}); // still 0.50 mm
        CHECK(entities[1].value(370) == std::optional<std::string>{"25"}); // still 0.25 mm

        const auto svg = io::svgDocument(scene);
        REQUIRE(svg.has_value());
        const std::vector<const SvgElement*> polylines = allSvg(readSvg(*svg), "polyline");
        CHECK(polylines[0]->attributes.at("stroke-width") == "0.5000");
        CHECK(polylines[1]->attributes.at("stroke-width") == "0.2500");
    }
}

// --- Writing to disk -----------------------------------------------------------------------------------------

TEST_CASE("Export_WritingToDiskIsAtomicAndAFailureLeavesTheOldFileAlone",
          "[io][export][p14][failure]") {
    TempDir dir;
    const DrawingScene scene = analyticScene();
    const auto svg = dir.path() / "drawing.svg";
    const auto dxf = dir.path() / "drawing.dxf";
    const auto pdf = dir.path() / "drawing.pdf";

    REQUIRE(io::exportSvg(scene, svg).has_value());
    REQUIRE(io::exportDxf(scene, dxf).has_value());
    REQUIRE(io::exportPdf(scene, pdf).has_value());
    CHECK(bytesOf(svg) == *io::svgDocument(scene));
    CHECK(bytesOf(dxf) == *io::dxfDocument(scene));
    CHECK(bytesOf(pdf) == *io::pdfDocument(scene));
    const auto sizeBefore = std::filesystem::file_size(svg);

    // Nowhere to write: the call fails, and no half-written file is left.
    const auto nowhere = dir.path() / "no" / "such" / "place" / "x.svg";
    CHECK_FALSE(io::exportSvg(scene, nowhere).has_value());
    CHECK_FALSE(std::filesystem::exists(nowhere));
    CHECK_FALSE(io::exportDxf(scene, nowhere).has_value());
    CHECK_FALSE(io::exportPdf(scene, nowhere).has_value());

    // An invalid scene fails before anything is opened.
    DrawingScene broken = scene;
    broken.items.texts.front().text.clear();
    const auto over = dir.path() / "drawing.svg";
    CHECK_FALSE(io::exportSvg(broken, over).has_value());
    // The good file that was already there is untouched.
    CHECK(std::filesystem::file_size(over) == sizeBefore);
    CHECK(bytesOf(over) == *io::svgDocument(scene));
}

// --- A real drawing, not a hand-built scene ---------------------------------------------------------
//
// The fixture above exercises the WRITERS. This one exercises the SCENE: a
// regenerated BetterCAD drawing with views, hidden-line removal, a section
// with hatch, dimensions, annotations, GD&T, an assembly view, a BOM and
// balloons -- assembled by sheetScene() and written out by all three writers.
//
// It is what proves the export boundary is COMPLETE. A missing primitive or a
// scene that could not carry something shows up here and nowhere else.

namespace {

struct Drawn {
    Document document{"Machine"};
    features::Regenerator regenerator;

    drawing::BodyLookup bodies() {
        const features::Regenerator* r = &regenerator;
        return [r](ObjectId object) { return r->body(object); };
    }
    drawing::TransformLookup transforms() {
        const features::Regenerator* r = &regenerator;
        return [r](ComponentId c) { return r->transform(c); };
    }
    void regenerate() {
        assembly::registerHandlers(regenerator, nullptr, nullptr);
        drawing::registerHandlers(regenerator);
        auto report = regenerator.regenerateAll(document);
        REQUIRE(report.has_value());
        INFO("regeneration must succeed for the scene to be assembled");
        REQUIRE(report->succeeded());
    }
};

ObjectId addBlock(Drawn& d, const std::string& name, Length size) {
    auto sketch = std::make_unique<sketch::Sketch>(name + "Profile", Frame3D::xy());
    const std::array<EntityId, 4> corners{
        require(sketch->addPoint(Point2D{0_mm, 0_mm})),
        require(sketch->addPoint(Point2D{size, 0_mm})),
        require(sketch->addPoint(Point2D{size, size})),
        require(sketch->addPoint(Point2D{0_mm, size})),
    };
    for (std::size_t i = 0; i < 4; ++i) {
        (void)require(sketch->addLine(corners[i], corners[(i + 1) % 4]));
    }
    const ObjectId sketchId = require(d.document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        name, {.profile = SketchId::fromValue(sketchId.value()), .depth = size});
    REQUIRE(extrude.has_value());
    return require(d.document.addObject(std::move(*extrude)));
}

/// A production drawing: two views of a part, a dimension with an ISO 286
/// fit, a note, a feature-control frame, and a second sheet carrying an
/// assembly view with a BOM and two balloons.
/// require(), but it names WHICH object could not be created and why.
template <typename T>
T made(std::string_view what, Result<T> result) {
    std::string reason;
    if (!result) {
        reason = std::format("{}: {}", what, result.error().message);
    }
    INFO(reason);
    REQUIRE(result.has_value());
    return *result;
}

struct Production {
    Drawn d;
    SheetId sheet{};
    SheetId assemblySheet{};
    ViewId front{};
    ViewId top{};
    ViewId assemblyView{};
};

Production production() {
    Production p;
    p.sheet = made("sheet one", drawing::createSheet(
        p.d.document, "Sheet1",
        drawing::SheetDefinition{.format = drawing::SheetFormat::A3,
                                 .orientation = drawing::SheetOrientation::Landscape,
                                 .margins = drawing::SheetMargins{10_mm, 10_mm, 10_mm, 10_mm},
                                 .scale = drawing::DrawingScale{1, 1}}));
    p.assemblySheet = made("sheet two", drawing::createSheet(
        p.d.document, "Sheet2",
        drawing::SheetDefinition{.format = drawing::SheetFormat::A3,
                                 .orientation = drawing::SheetOrientation::Landscape,
                                 .margins = drawing::SheetMargins{10_mm, 10_mm, 10_mm, 10_mm},
                                 .scale = drawing::DrawingScale{1, 2}}));
    const ObjectId block = addBlock(p.d, "Block", 40_mm);
    const ComponentId first = require(assembly::createComponent(
        p.d.document, "First",
        {.part = block, .placement = ComponentPlacement{.translation = {0_mm, 0_mm, 0_mm}}}));
    // BEHIND the first and offset from it, so the assembly view has real
    // occlusion. Two blocks side by side hide nothing from each other, and a
    // lone cube hides nothing from itself that survives the coincident-line
    // merge -- its back edges draw exactly where its front ones do, and
    // P14-HLR-001's ISO 128 precedence drops the hidden one.
    const ComponentId second = require(assembly::createComponent(
        p.d.document, "Second",
        {.part = block, .placement = ComponentPlacement{.translation = {15_mm, 60_mm, 0_mm}}}));
    p.d.regenerate();

    p.front = made("the front view", drawing::createView(
        p.d.document, "Front",
        drawing::ViewDefinition{.sheet = p.sheet,
                                .source = ObjectReference{block},
                                .orientation = drawing::StandardView::Front,
                                .placement = Point2D{120_mm, 150_mm}}));
    p.top = made("the top view", drawing::createView(
        p.d.document, "Top",
        drawing::ViewDefinition{.kind = drawing::ViewKind::Projected,
                                .sheet = p.sheet,
                                .parent = p.front,
                                .direction = drawing::ProjectedDirection::Top,
                                .spacing = 80_mm}));
    p.assemblyView = made("the assembly view", drawing::createView(
        p.d.document, "Assembly",
        drawing::ViewDefinition{.sheet = p.assemblySheet,
                                .subject = drawing::ViewSubject::Assembly,
                                .orientation = drawing::StandardView::Front,
                                .placement = Point2D{200_mm, 150_mm}}));

    const auto cap = [&](FaceRole role) {
        return drawing::DimensionTarget{
            .plane = PlaneReference{.object = block, .face = FaceSelector{.role = role}}};
    };
    (void)made("the thickness dimension", drawing::createDimension(
        p.d.document, "Thickness",
        drawing::DimensionDefinition{
            .view = p.front,
            .type = drawing::DimensionType::Linear,
            .from = cap(FaceRole::StartCap),
            .to = cap(FaceRole::EndCap),
            .format = drawing::DimensionFormat{.decimals = 2},
            .tolerance = drawing::DimensionTolerance{
                .fit = drawing::FitDesignation{
                    .role = drawing::FitRole::Hole, .letter = 'H', .grade = 7},
                .display = drawing::ToleranceDisplay::Limits},
            .placement = Point2D{120_mm, 100_mm}}));
    (void)made("the note", drawing::createAnnotation(
        p.d.document, "Note",
        drawing::AnnotationDefinition{.view = p.front,
                                      .type = drawing::AnnotationType::Note,
                                      .text = "BREAK SHARP EDGES",
                                      .placement = Point2D{40_mm, 40_mm}}));
    (void)made("the frame", drawing::createAnnotation(
        p.d.document, "Frame",
        drawing::AnnotationDefinition{
            .view = p.front,
            .type = drawing::AnnotationType::FeatureControlFrame,
            // A frame must say what it is about: flatness OF a face.
            .target = drawing::AnnotationTarget{
                .plane = PlaneReference{.object = block,
                                        .face = FaceSelector{.role = FaceRole::StartCap}}},
            .placement = Point2D{60_mm, 60_mm},
            .frame = drawing::FeatureControlFrame{
                .characteristic = drawing::GeometricCharacteristic::Flatness,
                .zone = drawing::ToleranceZone::Width,
                .tolerance = 0.05_mm}}));
    (void)made("the BOM table", drawing::createAnnotation(
        p.d.document, "Bom",
        drawing::AnnotationDefinition{.view = p.assemblyView,
                                      .type = drawing::AnnotationType::BomTable,
                                      .placement = Point2D{300_mm, 250_mm}}));
    (void)made("balloon one", drawing::createAnnotation(
        p.d.document, "B1",
        drawing::AnnotationDefinition{.view = p.assemblyView,
                                      .type = drawing::AnnotationType::Balloon,
                                      .target = drawing::AnnotationTarget{.object = ObjectId{first}},
                                      .placement = Point2D{120_mm, 120_mm}}));
    (void)made("balloon two", drawing::createAnnotation(
        p.d.document, "B2",
        drawing::AnnotationDefinition{.view = p.assemblyView,
                                      .type = drawing::AnnotationType::Balloon,
                                      .target = drawing::AnnotationTarget{.object = ObjectId{second}},
                                      .placement = Point2D{280_mm, 120_mm}}));
    p.d.regenerate();
    return p;
}

} // namespace

TEST_CASE("Export_AProductionDrawingAssemblesIntoASceneAndWritesToAllThreeFormats",
          "[io][export][p14][production]") {
    Production p = production();

    auto scene = drawing::sheetScene(p.d.document, p.sheet, p.d.bodies(), p.d.transforms());
    INFO(why(scene));
    REQUIRE(scene.has_value());

    // The page is the SHEET, in millimetres: A3 landscape.
    CHECK_THAT(scene->width.in(units::mm), WithinAbs(420.0, kMm));
    CHECK_THAT(scene->height.in(units::mm), WithinAbs(297.0, kMm));

    // It carries the frame, the two views' edges, the dimension and the two
    // annotations. Nothing was dropped on the way.
    CHECK(scene->items.lines.size() > 10);
    CHECK_FALSE(scene->items.texts.empty());

    // The dimension's VALUE is in the scene as text, and it is the number the
    // model gives -- the block is 40 mm thick with an H7 fit, so the upper
    // limit reads 40.03.
    const auto hasText = [&](std::string_view wanted) {
        return std::ranges::any_of(scene->items.texts, [&](const SceneText& text) {
            return text.text.find(wanted) != std::string::npos;
        });
    };
    CHECK(hasText("40"));
    CHECK(hasText("BREAK SHARP EDGES"));

    // All three writers transcribe it.
    const auto svg = io::svgDocument(*scene);
    const auto dxf = io::dxfDocument(*scene);
    const auto pdf = io::pdfDocument(*scene);
    INFO(why(svg));
    REQUIRE(svg.has_value());
    REQUIRE(dxf.has_value());
    REQUIRE(pdf.has_value());

    // The SVG declares the physical sheet.
    const std::vector<SvgElement> elements = readSvg(*svg);
    CHECK(findSvg(elements, "svg")->attributes.at("width") == "420.0000mm");
    // The DXF says millimetres and holds every entity the scene had.
    CHECK(dxfHeader(*dxf, "$INSUNITS") == std::optional<std::string>{"4"});
    const std::vector<DxfEntity> entities = dxfEntities(*dxf);
    const std::size_t sceneItems =
        scene->items.lines.size() + scene->items.arcs.size() + scene->items.texts.size();
    CHECK(entities.size() == sceneItems); // every primitive, none silently dropped
    // The PDF page is A3 landscape in points.
    CHECK_THAT(pdfMediaBox(*pdf)[2], WithinAbs(420.0 * 72.0 / 25.4, 1e-3));
    CHECK_THAT(pdfMediaBox(*pdf)[3], WithinAbs(297.0 * 72.0 / 25.4, 1e-3));

    // The note's words survive into every format.
    CHECK_THAT(*svg, ContainsSubstring("BREAK SHARP EDGES"));
    CHECK_THAT(*dxf, ContainsSubstring("BREAK SHARP EDGES"));
    CHECK_THAT(*pdf, ContainsSubstring("BREAK SHARP EDGES"));
}

TEST_CASE("Export_TheAssemblySheetCarriesItsBomAndBalloonsIntoEveryFormat",
          "[io][export][p14][production]") {
    Production p = production();
    auto scene = drawing::sheetScene(p.d.document, p.assemblySheet, p.d.bodies(), p.d.transforms());
    INFO(why(scene));
    REQUIRE(scene.has_value());

    // The BOM's rows and the balloons' numbers are the ASSEMBLY's, arriving
    // through the scene. Two occurrences of one part: one row, quantity 2,
    // item 1 -- and both balloons say 1.
    const auto bom = drawing::billOfMaterials(p.d.document, p.assemblyView);
    REQUIRE(bom.has_value());
    REQUIRE(bom->rows.size() == 1);
    CHECK(bom->rows.front().quantity() == 2);

    const auto texts = scene->items.texts;
    const auto count = [&](std::string_view wanted) {
        return std::ranges::count_if(texts, [&](const SceneText& text) { return text.text == wanted; });
    };
    CHECK(count("1") >= 2); // the two balloons, both item 1

    const auto dxf = io::dxfDocument(*scene);
    REQUIRE(dxf.has_value());
    const std::vector<DxfEntity> entities = dxfEntities(*dxf);
    const auto textEntities = std::ranges::count_if(
        entities, [](const DxfEntity& e) { return e.type == "TEXT"; });
    CHECK(static_cast<std::size_t>(textEntities) == texts.size());
}

TEST_CASE("Export_TurningHiddenLinesOffRemovesThemAndMovesNothingElse",
          "[io][export][p14][production]") {
    // The exporter renders what hidden-line removal decided and never re-runs
    // it. The way to check that is to change the VIEW's policy and require the
    // visible geometry to be untouched while the dashed lines go.
    //
    // On the ASSEMBLY sheet, because that is where the occlusion is: one
    // occurrence stands behind the other. A lone cube hides nothing that
    // survives the coincident-line merge.
    Production p = production();

    const auto dashedCount = [](const DrawingScene& scene) {
        return std::ranges::count_if(scene.items.lines, [](const SceneLine& line) {
            return line.style == LineStyle::Dashed;
        });
    };
    const auto solidLines = [](const DrawingScene& scene) {
        std::vector<SceneLine> solid;
        for (const SceneLine& line : scene.items.lines) {
            if (line.style != LineStyle::Dashed) {
                solid.push_back(line);
            }
        }
        return solid;
    };

    auto withHidden =
        drawing::sheetScene(p.d.document, p.assemblySheet, p.d.bodies(), p.d.transforms());
    INFO(why(withHidden));
    REQUIRE(withHidden.has_value());
    INFO("the assembly view must actually occlude something for this to mean anything");
    REQUIRE(dashedCount(*withHidden) > 0);
    const std::vector<SceneLine> before = solidLines(*withHidden);

    drawing::ViewDefinition quiet = drawing::findView(p.d.document, p.assemblyView)->definition();
    quiet.hiddenLine.showHidden = false;
    REQUIRE(drawing::setViewDefinition(p.d.document, p.assemblyView, quiet).has_value());
    p.d.regenerate();

    auto without =
        drawing::sheetScene(p.d.document, p.assemblySheet, p.d.bodies(), p.d.transforms());
    REQUIRE(without.has_value());
    CHECK(dashedCount(*without) == 0);
    // The visible lines did NOT move: turning hidden detail off changes what is
    // shown and nothing about where anything is.
    CHECK(solidLines(*without) == before);

    // And no HIDDEN-layer entity survives into the DXF.
    const auto dxf = io::dxfDocument(*without);
    REQUIRE(dxf.has_value());
    for (const DxfEntity& entity : dxfEntities(*dxf)) {
        CHECK(entity.value(8) != std::optional<std::string>{"HIDDEN"});
    }
    // ...while the one with hidden detail has them, dashed and narrow.
    const auto withDxf = io::dxfDocument(*withHidden);
    REQUIRE(withDxf.has_value());
    bool sawHidden = false;
    for (const DxfEntity& entity : dxfEntities(*withDxf)) {
        if (entity.value(8) == std::optional<std::string>{"HIDDEN"}) {
            sawHidden = true;
            CHECK(entity.value(6) == std::optional<std::string>{"DASHED"});
            CHECK(entity.value(370) == std::optional<std::string>{"25"});
        }
    }
    CHECK(sawHidden);
}

TEST_CASE("Export_AViewAtOneToTwoDrawsHalfTheSizeAndKeepsItsPenAndItsLettering",
          "[io][export][p14][production]") {
    // A view's scale changes its GEOMETRY and nothing else: a line weight is a
    // pen and a text height is lettering, and neither is multiplied by it.
    //
    // The geometry is measured through the VIEW's own projected extent rather
    // than by spanning the sheet, because a sheet also carries a frame, a
    // dimension and a note, none of which is the view.
    Production p = production();

    const auto viewWidth = [&](ViewId id) {
        auto drawn = drawing::projectedGeometry(p.d.document, id, p.d.bodies(), p.d.transforms());
        REQUIRE(drawn.has_value());
        return (drawn->bounds.max.x - drawn->bounds.min.x).in(units::mm);
    };
    const double atOneToOne = viewWidth(p.front);
    CHECK_THAT(atOneToOne, WithinAbs(40.0, 1e-6)); // the 40 mm block, at 1:1

    const auto widths = [](const DrawingScene& scene) {
        std::vector<double> found;
        for (const SceneLine& line : scene.items.lines) {
            found.push_back(line.width.in(units::mm));
        }
        std::ranges::sort(found);
        found.erase(std::unique(found.begin(), found.end()), found.end());
        return found;
    };
    const auto heights = [](const DrawingScene& scene) {
        std::vector<double> found;
        for (const SceneText& text : scene.items.texts) {
            found.push_back(text.height.in(units::mm));
        }
        std::ranges::sort(found);
        found.erase(std::unique(found.begin(), found.end()), found.end());
        return found;
    };
    auto full = drawing::sheetScene(p.d.document, p.sheet, p.d.bodies(), p.d.transforms());
    REQUIRE(full.has_value());

    drawing::ViewDefinition halved = drawing::findView(p.d.document, p.front)->definition();
    halved.scale = drawing::DrawingScale{1, 2};
    REQUIRE(drawing::setViewDefinition(p.d.document, p.front, halved).has_value());
    p.d.regenerate();

    // HALF the size on paper, exactly.
    CHECK_THAT(viewWidth(p.front), WithinAbs(20.0, 1e-6));
    CHECK_THAT(viewWidth(p.front), WithinAbs(atOneToOne / 2.0, 1e-6));

    auto half = drawing::sheetScene(p.d.document, p.sheet, p.d.bodies(), p.d.transforms());
    REQUIRE(half.has_value());
    // The pens and the lettering did not change with it.
    CHECK(widths(*full) == widths(*half));
    CHECK(heights(*full) == heights(*half));

    // And that survives into the file: the same stroke widths, either way.
    const auto svgFull = io::svgDocument(*full);
    const auto svgHalf = io::svgDocument(*half);
    REQUIRE(svgFull.has_value());
    REQUIRE(svgHalf.has_value());
    CHECK_THAT(*svgFull, ContainsSubstring("stroke-width=\"0.5000\""));
    CHECK_THAT(*svgHalf, ContainsSubstring("stroke-width=\"0.5000\""));
}
