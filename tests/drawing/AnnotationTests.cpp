#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/drawing/Annotations.hpp>
#include <bettercad/drawing/Sheets.hpp>
#include <bettercad/drawing/Views.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using drawing::AnnotationDefinition;
using drawing::AnnotationTarget;
using drawing::AnnotationType;
using drawing::DrawingScale;
using drawing::LineStyle;
using drawing::MaterialRemoval;
using drawing::SceneItems;
using drawing::SceneLine;
using drawing::SceneText;
using drawing::SheetDefinition;
using drawing::StandardView;
using drawing::SurfaceFinish;
using drawing::TextStyle;
using drawing::ViewDefinition;

// P14-ANNO-001: annotations, and the one invariant they all turn on.
//
//     WHERE an annotation points follows the model through the view's
//     projection, so it moves when the view's scale changes.
//
//     HOW BIG it is drawn is paper millimetres, and nothing multiplies it.
//
// A 3.5 mm note is 3.5 mm at 1:1 and 3.5 mm at 1:10. Every symbol size below
// is asserted at five scales for that reason, and the positions are asserted
// to move, so that "nothing changes" could not pass by everything being
// frozen.
//
// WHERE THE EXPECTED NUMBERS COME FROM. The plate is 100 x 60 x 20 and its
// holes are at (20, 30) and (70, 30), all written into the fixture. A top
// view centres on the plate's projected bounds, so its centre is (50, 30) and
// the first hole sits 30 mm to the left of it -- which lands at
// placement + (-30, 0) x factor. Every expected sheet position is that
// arithmetic, done here.
namespace {

constexpr double kMm = 1e-9;

struct Fixture {
    Document document{"Drawing"};
    ObjectId plate{};
    ObjectId holeA{};
    ObjectId holeB{};
    SheetId sheet{};
    ViewId view{};
    features::Regenerator regenerator;

    drawing::BodyLookup bodies() {
        const features::Regenerator* r = &regenerator;
        return [r](ObjectId object) { return r->body(object); };
    }
    void regenerate() { REQUIRE(regenerator.regenerateAll(document).has_value()); }
};

/// The plate's top face, at z = 20 facing +Z.
geometry::FaceSignature topOfPlate() {
    return geometry::planeSignature(Point3D{0_mm, 0_mm, 20_mm}, Direction3D::unitZ());
}

/// A 100 x 60 x 20 plate with two IDENTICAL 10 mm through holes, at (20, 30)
/// and (70, 30). Identical on purpose: an annotation pointed at one of them
/// must not find the other when it goes.
Fixture makePlate(DrawingScale scale = DrawingScale{1, 1},
                  StandardView orientation = StandardView::Top, bool blind = false) {
    Fixture f;
    auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
    addRectangle(*sketch, 0_mm, 0_mm, 100_mm, 60_mm);
    const ObjectId sketchId = require(f.document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Plate", {.profile = SketchId::fromValue(sketchId.value()), .depth = 20_mm});
    REQUIRE(extrude.has_value());
    f.plate = require(f.document.addObject(std::move(*extrude)));

    auto first = features::HoleFeature::create(
        "HoleA", {.target = FeatureId::fromValue(f.plate.value()),
                  .face = topOfPlate(),
                  .center = Point2D{20_mm, 30_mm},
                  .extent = blind ? geometry::HoleExtent::Blind : geometry::HoleExtent::Through,
                  .diameter = 10_mm,
                  .depth = blind ? 12_mm : Length{}});
    REQUIRE(first.has_value());
    f.holeA = require(f.document.addObject(std::move(*first)));

    auto second = features::HoleFeature::create(
        "HoleB", {.target = FeatureId::fromValue(f.holeA.value()),
                  .face = topOfPlate(),
                  .center = Point2D{70_mm, 30_mm},
                  .diameter = 10_mm});
    REQUIRE(second.has_value());
    f.holeB = require(f.document.addObject(std::move(*second)));

    f.sheet = require(drawing::createSheet(
        f.document, "Sheet1",
        SheetDefinition{.format = drawing::SheetFormat::A3,
                        .orientation = drawing::SheetOrientation::Landscape,
                        .margins = drawing::SheetMargins{10_mm, 10_mm, 10_mm, 10_mm},
                        .scale = scale}));
    f.view = require(drawing::createView(
        f.document, "TopView",
        ViewDefinition{.sheet = f.sheet,
                       .source = ObjectReference{f.holeB},
                       .orientation = orientation,
                       .placement = Point2D{200_mm, 150_mm}}));
    f.regenerate();
    return f;
}

AnnotationTarget atHole(ObjectId hole) { return AnnotationTarget{.object = hole}; }

AnnotationId add(Fixture& f, const std::string& name, AnnotationDefinition definition) {
    definition.view = f.view;
    return require(drawing::createAnnotation(f.document, name, definition));
}

SceneItems drawn(Fixture& f, AnnotationId id) {
    auto items = drawing::draw(f.document, id, f.bodies());
    REQUIRE(items.has_value());
    return std::move(*items);
}

/// The length of a two-point line, in millimetres.
double lengthOf(const SceneLine& line) {
    REQUIRE(line.points.size() >= 2);
    return std::hypot((line.points.back().x - line.points.front().x).in(units::mm),
                      (line.points.back().y - line.points.front().y).in(units::mm));
}

} // namespace

// --- Text notes --------------------------------------------------------------------------

TEST_CASE("Annotation_ANoteIsWordsOnPaperAndPointsAtNothing", "[drawing][annotation][p14]") {
    Fixture f = makePlate();
    const AnnotationId id = add(f, "Note",
                                {.type = AnnotationType::Note,
                                 .text = "DEBURR ALL EDGES",
                                 .placement = Point2D{30_mm, 20_mm}});
    const SceneItems items = drawn(f, id);

    REQUIRE(items.texts.size() == 1);
    CHECK(items.texts.front().text == "DEBURR ALL EDGES");
    CHECK(items.texts.front().at == Point2D{30_mm, 20_mm});
    CHECK_THAT(items.texts.front().height.in(units::mm), WithinAbs(3.5, kMm));
    CHECK(items.lines.empty()); // a note has no leader: that is what a leader is for
}

TEST_CASE("Annotation_ANoteThatPointsAtSomethingIsRefused", "[drawing][annotation][p14]") {
    Fixture f = makePlate();
    const auto bad = drawing::createAnnotation(
        f.document, "Bad",
        {.view = f.view, .type = AnnotationType::Note, .target = atHole(f.holeA), .text = "X"});
    REQUIRE_FALSE(bad.has_value());
    CHECK_THAT(bad.error().message, ContainsSubstring("use a leader to point"));
}

TEST_CASE("Annotation_ANoteMustSaySomething", "[drawing][annotation][p14]") {
    Fixture f = makePlate();
    const auto bad = drawing::createAnnotation(
        f.document, "Bad", {.view = f.view, .type = AnnotationType::Note, .text = ""});
    REQUIRE_FALSE(bad.has_value());
    CHECK_THAT(bad.error().message, ContainsSubstring("must say something"));
}

// --- The projection: where an annotation points ------------------------------------------

TEST_CASE("Annotation_PointsWhereTheViewDrewTheThingItLabels", "[drawing][annotation][p14]") {
    // The plate is 100 x 60, so a top view centres on (50, 30) and the hole at
    // (20, 30) lands 30 mm to the left of the view's placement. That is the
    // one number a leader's arrow must hit, or it is pointing at nothing.
    Fixture f = makePlate();
    const AnnotationId id = add(f, "Callout",
                                {.type = AnnotationType::Leader,
                                 .target = atHole(f.holeA),
                                 .text = "THIS ONE",
                                 .placement = Point2D{250_mm, 200_mm}});
    const SceneItems items = drawn(f, id);

    // The leader's last point is its arrow tip.
    REQUIRE(items.lines.size() == 2); // the leader, and its arrowhead
    const Point2D tip = items.lines.front().points.back();
    CHECK_THAT(tip.x.in(units::mm), WithinAbs(170.0, 1e-6)); // 200 - 30
    CHECK_THAT(tip.y.in(units::mm), WithinAbs(150.0, 1e-6));

    // And the text stayed exactly where it was put.
    REQUIRE(items.texts.size() == 1);
    CHECK(items.texts.front().at == Point2D{250_mm, 200_mm});
}

// --- THE HARD GATE: paper sizes do not scale ---------------------------------------------

TEST_CASE("Annotation_TextIsTheSameSizeOnPaperAtEveryScale", "[drawing][annotation][p14]") {
    // A 3.5 mm note is 3.5 mm whether the view beside it is drawn full size,
    // a tenth size or five times size. Nothing about lettering is a property
    // of the geometry's scale.
    for (const DrawingScale scale : {DrawingScale{1, 1}, DrawingScale{1, 2}, DrawingScale{2, 1},
                                     DrawingScale{1, 10}, DrawingScale{5, 1}}) {
        INFO("scale " << scale.paper << ":" << scale.model);
        Fixture f = makePlate(scale);
        const AnnotationId note = add(f, "Note",
                                      {.type = AnnotationType::Note,
                                       .text = "DEBURR ALL EDGES",
                                       .placement = Point2D{30_mm, 20_mm}});
        const AnnotationId callout = add(f, "Callout",
                                         {.type = AnnotationType::HoleCallout,
                                          .target = atHole(f.holeA),
                                          .placement = Point2D{250_mm, 200_mm}});
        const AnnotationId datum = add(f, "DatumA",
                                       {.type = AnnotationType::Datum,
                                        .target = atHole(f.holeA),
                                        .text = "A",
                                        .placement = Point2D{60_mm, 40_mm}});

        for (const AnnotationId id : {note, callout, datum}) {
            const SceneItems items = drawn(f, id);
            REQUIRE_FALSE(items.texts.empty());
            for (const SceneText& text : items.texts) {
                CHECK_THAT(text.height.in(units::mm), WithinAbs(3.5, kMm));
            }
        }
    }
}

TEST_CASE("Annotation_ACentreMarkIsTheSameSizeOnPaperButMovesWithTheModel",
          "[drawing][annotation][p14]") {
    // Both halves of the invariant in one test, because either alone could
    // pass for the wrong reason: a centre mark that never moved would also
    // never change size.
    //
    // The hole is 30 mm left of the plate's centre in the model, so on the
    // sheet it is 30 x factor to the left of the view's placement. Its arms
    // are 2.5 mm whatever the factor is.
    struct Case {
        DrawingScale scale;
        double offset;
    };
    for (const Case& c : {Case{DrawingScale{1, 1}, 30.0}, Case{DrawingScale{1, 2}, 15.0},
                          Case{DrawingScale{2, 1}, 60.0}, Case{DrawingScale{1, 10}, 3.0},
                          Case{DrawingScale{5, 1}, 150.0}}) {
        INFO("scale " << c.scale.paper << ":" << c.scale.model);
        Fixture f = makePlate(c.scale);
        const AnnotationId id = add(f, "Mark",
                                    {.type = AnnotationType::Centremark,
                                     .target = atHole(f.holeA),
                                     .armLength = 2.5_mm});
        const SceneItems items = drawn(f, id);

        REQUIRE(items.lines.size() == 2); // one arm across, one up
        for (const SceneLine& arm : items.lines) {
            CHECK(arm.style == LineStyle::Centre);
            CHECK_THAT(lengthOf(arm), WithinAbs(5.0, 1e-6)); // two half-arms of 2.5
        }
        // The arms cross where the hole was drawn, which MOVES with the scale.
        const Point2D across = items.lines.front().points.front();
        const Point2D up = items.lines.back().points.front();
        CHECK_THAT((across.x + Length::fromSi(0.0025)).in(units::mm),
                   WithinAbs(200.0 - c.offset, 1e-6));
        CHECK_THAT(up.x.in(units::mm), WithinAbs(200.0 - c.offset, 1e-6));
    }
}

TEST_CASE("Annotation_ALeadersArrowIsTheSameSizeOnPaperAtEveryScale",
          "[drawing][annotation][p14]") {
    for (const DrawingScale scale : {DrawingScale{1, 1}, DrawingScale{1, 10}, DrawingScale{5, 1}}) {
        INFO("scale " << scale.paper << ":" << scale.model);
        Fixture f = makePlate(scale);
        const AnnotationId id = add(f, "Callout",
                                    {.type = AnnotationType::Leader,
                                     .target = atHole(f.holeA),
                                     .text = "HERE",
                                     .placement = Point2D{250_mm, 200_mm}});
        const SceneItems items = drawn(f, id);
        REQUIRE(items.lines.size() == 2);
        const SceneLine& head = items.lines.back();
        REQUIRE(head.points.size() == 4); // tip, barb, barb, tip

        // The head is one text height long, whatever the view scale.
        const Point2D tip = head.points.front();
        const Point2D barb = head.points[1];
        const double back = std::hypot((barb.x - tip.x).in(units::mm),
                                       (barb.y - tip.y).in(units::mm));
        // sqrt(1^2 + 0.25^2) x 3.5 = 3.6072 mm from tip to a barb.
        CHECK_THAT(back, WithinAbs(std::hypot(1.0, 0.25) * 3.5, 1e-6));
    }
}

TEST_CASE("Annotation_ADatumBoxIsTheSameSizeOnPaperAtEveryScale",
          "[drawing][annotation][p14]") {
    for (const DrawingScale scale : {DrawingScale{1, 1}, DrawingScale{1, 10}, DrawingScale{5, 1}}) {
        INFO("scale " << scale.paper << ":" << scale.model);
        Fixture f = makePlate(scale);
        const AnnotationId id = add(f, "DatumA",
                                    {.type = AnnotationType::Datum,
                                     .target = atHole(f.holeA),
                                     .text = "A",
                                     .placement = Point2D{60_mm, 40_mm}});
        const SceneItems items = drawn(f, id);
        // The box is the first line: five points, two text heights across and
        // two high.
        REQUIRE_FALSE(items.lines.empty());
        const SceneLine& box = items.lines.front();
        REQUIRE(box.points.size() == 5);
        double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
        for (const Point2D& p : box.points) {
            minX = std::min(minX, p.x.in(units::mm));
            maxX = std::max(maxX, p.x.in(units::mm));
            minY = std::min(minY, p.y.in(units::mm));
            maxY = std::max(maxY, p.y.in(units::mm));
        }
        CHECK_THAT(maxX - minX, WithinAbs(7.0, 1e-6)); // 2 x 1.0 x 3.5
        CHECK_THAT(maxY - minY, WithinAbs(7.0, 1e-6));
    }
}

// --- Centrelines -------------------------------------------------------------------------

TEST_CASE("Annotation_ACentrelineRunsThroughTheProjectedAxis", "[drawing][annotation][p14]") {
    // Seen from the FRONT the hole's axis lies in the view plane, so it draws
    // as a line. The plate is 100 wide and 20 thick, so a front view centres
    // on (50, 10); the hole at x = 20 is 30 to the left, and its axis runs
    // straight up and down.
    Fixture f = makePlate(DrawingScale{1, 1}, StandardView::Front);
    const AnnotationId id = add(f, "Axis",
                                {.type = AnnotationType::Centreline,
                                 .target = atHole(f.holeA),
                                 .extension = 3_mm});
    const SceneItems items = drawn(f, id);

    REQUIRE(items.lines.size() == 1);
    const SceneLine& line = items.lines.front();
    CHECK(line.style == LineStyle::Centre);
    REQUIRE(line.points.size() == 2);

    // Vertical, and through x = 170.
    CHECK_THAT(line.points.front().x.in(units::mm), WithinAbs(170.0, 1e-6));
    CHECK_THAT(line.points.back().x.in(units::mm), WithinAbs(170.0, 1e-6));
    // It spans the plate's 20 mm thickness plus 3 mm of extension at each end.
    CHECK_THAT(lengthOf(line), WithinAbs(20.0 + 6.0, 1e-6));
}

TEST_CASE("Annotation_ACentrelineWhoseAxisPointsAtTheViewerIsRefused",
          "[drawing][annotation][p14]") {
    // Seen from the top, the hole's axis runs into the page. It has no
    // direction to draw along, and a centre mark is what that view wants --
    // which the refusal says.
    Fixture f = makePlate(DrawingScale{1, 1}, StandardView::Top);
    const AnnotationId id = add(f, "Axis",
                                {.type = AnnotationType::Centreline, .target = atHole(f.holeA)});
    const auto items = drawing::draw(f.document, id, f.bodies());
    REQUIRE_FALSE(items.has_value());
    CHECK_THAT(items.error().message, ContainsSubstring("centre mark"));
}

TEST_CASE("Annotation_ACentrelinesExtensionIsPaperAndItsSpanIsNot",
          "[drawing][annotation][p14]") {
    // The plate's 20 mm thickness is drawn at the view's scale; the 3 mm
    // extension at each end is not.
    for (const auto& [scale, span] :
         std::vector<std::pair<DrawingScale, double>>{{DrawingScale{1, 1}, 20.0},
                                                      {DrawingScale{1, 2}, 10.0},
                                                      {DrawingScale{2, 1}, 40.0}}) {
        INFO("scale " << scale.paper << ":" << scale.model);
        Fixture f = makePlate(scale, StandardView::Front);
        const AnnotationId id = add(f, "Axis",
                                    {.type = AnnotationType::Centreline,
                                     .target = atHole(f.holeA),
                                     .extension = 3_mm});
        CHECK_THAT(lengthOf(drawn(f, id).lines.front()), WithinAbs(span + 6.0, 1e-6));
    }
}

// --- Hole callouts -----------------------------------------------------------------------

TEST_CASE("Annotation_AHoleCalloutReadsTheHole", "[drawing][annotation][p14]") {
    Fixture f = makePlate();
    const AnnotationId id = add(f, "Callout",
                                {.type = AnnotationType::HoleCallout,
                                 .target = atHole(f.holeA),
                                 .placement = Point2D{250_mm, 200_mm}});
    auto text = drawing::annotationText(f.document, id, f.bodies());
    REQUIRE(text.has_value());
    CHECK(*text == "Ø10 THRU");
}

TEST_CASE("Annotation_ABlindHoleCalloutSaysHowDeep", "[drawing][annotation][p14]") {
    Fixture f = makePlate(DrawingScale{1, 1}, StandardView::Top, true);
    const AnnotationId id = add(f, "Callout",
                                {.type = AnnotationType::HoleCallout,
                                 .target = atHole(f.holeA),
                                 .placement = Point2D{250_mm, 200_mm}});
    auto text = drawing::annotationText(f.document, id, f.bodies());
    REQUIRE(text.has_value());
    CHECK(*text == "Ø10 DEEP 12");
}

TEST_CASE("Annotation_AHoleCalloutFollowsTheHoleWhenItChanges", "[drawing][annotation][p14]") {
    // The whole reason a callout stores a reference and not a number. Same
    // annotation, same target, no drawing edit.
    Fixture f = makePlate();
    const AnnotationId id = add(f, "Callout",
                                {.type = AnnotationType::HoleCallout,
                                 .target = atHole(f.holeA),
                                 .placement = Point2D{250_mm, 200_mm}});
    CHECK(*drawing::annotationText(f.document, id, f.bodies()) == "Ø10 THRU");

    const auto* hole = f.document.findObjectAs<features::HoleFeature>(f.holeA);
    REQUIRE(hole != nullptr);
    auto definition = hole->definition();
    definition.diameter = 12_mm;
    REQUIRE(f.document
                .modifyObject<features::HoleFeature>(
                    f.holeA, [&](features::HoleFeature& h) { return h.setDefinition(definition); })
                .has_value());
    f.regenerate();

    CHECK(*drawing::annotationText(f.document, id, f.bodies()) == "Ø12 THRU");

    // And blind, with a depth.
    definition.extent = geometry::HoleExtent::Blind;
    definition.depth = 8_mm;
    REQUIRE(f.document
                .modifyObject<features::HoleFeature>(
                    f.holeA, [&](features::HoleFeature& h) { return h.setDefinition(definition); })
                .has_value());
    f.regenerate();
    CHECK(*drawing::annotationText(f.document, id, f.bodies()) == "Ø12 DEEP 8");
}

TEST_CASE("Annotation_AHoleCalloutStoresNoTextOfItsOwn", "[drawing][annotation][p14]") {
    Fixture f = makePlate();
    const auto bad = drawing::createAnnotation(
        f.document, "Bad",
        {.view = f.view,
         .type = AnnotationType::HoleCallout,
         .target = atHole(f.holeA),
         .text = "Ø10 THRU"});
    REQUIRE_FALSE(bad.has_value());
    CHECK_THAT(bad.error().message, ContainsSubstring("takes its words from the model"));
}

TEST_CASE("Annotation_AHoleCalloutOfSomethingThatIsNotAHoleIsRefused",
          "[drawing][annotation][p14]") {
    Fixture f = makePlate();
    const AnnotationId id = add(f, "Callout",
                                {.type = AnnotationType::HoleCallout,
                                 .target = AnnotationTarget{.object = f.plate},
                                 .placement = Point2D{250_mm, 200_mm}});
    const auto text = drawing::annotationText(f.document, id, f.bodies());
    REQUIRE_FALSE(text.has_value());
    CHECK_THAT(text.error().message, ContainsSubstring("is not a hole"));
}

// --- Surface finish and datums -----------------------------------------------------------

TEST_CASE("Annotation_ASurfaceFinishCarriesItsRoughness", "[drawing][annotation][p14]") {
    Fixture f = makePlate();
    const AnnotationId id = add(
        f, "Finish",
        {.type = AnnotationType::SurfaceFinish,
         .target = atHole(f.holeA),
         .placement = Point2D{80_mm, 60_mm},
         .finish = SurfaceFinish{Length::fromSi(3.2e-6), MaterialRemoval::Required}});
    const SceneItems items = drawn(f, id);

    REQUIRE_FALSE(items.texts.empty());
    CHECK(items.texts.front().text == "Ra 3.2");
    // The tick, the removal bar, the leader and its arrowhead.
    CHECK(items.lines.size() >= 3);
}

TEST_CASE("Annotation_ASurfaceFinishNeedsARoughnessAndARoughnessNeedsASurfaceFinish",
          "[drawing][annotation][p14]") {
    Fixture f = makePlate();
    const auto missing = drawing::createAnnotation(
        f.document, "Bad",
        {.view = f.view, .type = AnnotationType::SurfaceFinish, .target = atHole(f.holeA)});
    REQUIRE_FALSE(missing.has_value());
    CHECK_THAT(missing.error().message, ContainsSubstring("must give a roughness"));

    const auto stray = drawing::createAnnotation(
        f.document, "Bad",
        {.view = f.view,
         .type = AnnotationType::Note,
         .text = "X",
         .finish = SurfaceFinish{Length::fromSi(3.2e-6), MaterialRemoval::Any}});
    REQUIRE_FALSE(stray.has_value());
    CHECK_THAT(stray.error().message, ContainsSubstring("takes no roughness"));

    const auto negative = drawing::createAnnotation(
        f.document, "Bad",
        {.view = f.view,
         .type = AnnotationType::SurfaceFinish,
         .target = atHole(f.holeA),
         .finish = SurfaceFinish{Length::fromSi(-1e-6), MaterialRemoval::Any}});
    REQUIRE_FALSE(negative.has_value());
    CHECK_THAT(negative.error().message, ContainsSubstring("greater than zero"));
}

TEST_CASE("Annotation_ADatumIsOneLetterAndNotJustAnyLetter", "[drawing][annotation][p14]") {
    Fixture f = makePlate();
    const auto refuse = [&](const std::string& letter, std::string_view expected) {
        INFO("datum '" << letter << "'");
        const auto bad = drawing::createAnnotation(
            f.document, "Bad",
            {.view = f.view,
             .type = AnnotationType::Datum,
             .target = atHole(f.holeA),
             .text = letter});
        REQUIRE_FALSE(bad.has_value());
        CHECK_THAT(bad.error().message, ContainsSubstring(std::string{expected}));
    };
    refuse("AB", "a single letter");
    refuse("a", "capital A to Z");
    refuse("1", "capital A to Z");
    // ISO 5459 skips these three: they read as 1 and 0.
    refuse("I", "I, O or Q");
    refuse("O", "I, O or Q");
    refuse("Q", "I, O or Q");

    const AnnotationId good = add(f, "DatumA",
                                  {.type = AnnotationType::Datum,
                                   .target = atHole(f.holeA),
                                   .text = "A",
                                   .placement = Point2D{60_mm, 40_mm}});
    const SceneItems items = drawn(f, good);
    REQUIRE(items.texts.size() == 1);
    CHECK(items.texts.front().text == "A");
    CHECK(items.texts.front().anchor == drawing::TextAnchor::MiddleCentre);
}

// --- References ---------------------------------------------------------------------------

TEST_CASE("Annotation_AMissingTargetFailsAndDrawsNothing", "[drawing][annotation][p14]") {
    Fixture f = makePlate();
    const AnnotationId id = add(f, "Callout",
                                {.type = AnnotationType::HoleCallout,
                                 .target = atHole(f.holeA),
                                 .placement = Point2D{250_mm, 200_mm}});
    CHECK(*drawing::annotationText(f.document, id, f.bodies()) == "Ø10 THRU");

    // HoleB is built on HoleA, so removing A means removing B first.
    REQUIRE(f.document.removeObject(f.holeB).has_value());
    REQUIRE(f.document.removeObject(f.holeA).has_value());
    f.regenerate();

    const auto text = drawing::annotationText(f.document, id, f.bodies());
    REQUIRE_FALSE(text.has_value());
    CHECK_THAT(text.error().message, ContainsSubstring("Callout"));
}

TEST_CASE("Annotation_NeverRebindsToAnIdenticalNeighbour", "[drawing][annotation][p14]") {
    // The hard gate. Two holes of the same diameter, the same depth, in the
    // same face, 50 mm apart. A callout is pointed at the first and the first
    // is removed. A resolver that looked for "a hole like the one that went"
    // would find the second and carry on showing a number as if nothing had
    // happened.
    Fixture f = makePlate();
    const AnnotationId id = add(f, "Callout",
                                {.type = AnnotationType::HoleCallout,
                                 .target = atHole(f.holeA),
                                 .placement = Point2D{250_mm, 200_mm}});
    CHECK(*drawing::annotationText(f.document, id, f.bodies()) == "Ø10 THRU");

    // Rebuild without HoleA but WITH an identical hole still present, by
    // re-pointing B at the plate and deleting A.
    const auto* b = f.document.findObjectAs<features::HoleFeature>(f.holeB);
    REQUIRE(b != nullptr);
    auto definition = b->definition();
    definition.target = FeatureId::fromValue(f.plate.value());
    REQUIRE(f.document
                .modifyObject<features::HoleFeature>(
                    f.holeB, [&](features::HoleFeature& h) { return h.setDefinition(definition); })
                .has_value());
    REQUIRE(f.document.removeObject(f.holeA).has_value());
    f.regenerate();

    // The survivor is still there, still 10 mm, still through, still in the
    // same face.
    REQUIRE(f.document.findObjectAs<features::HoleFeature>(f.holeB) != nullptr);

    const auto text = drawing::annotationText(f.document, id, f.bodies());
    REQUIRE_FALSE(text.has_value()); // and NOT the survivor's callout
}

TEST_CASE("Annotation_DependsOnItsViewAndOnWhatItPointsAt", "[drawing][annotation][p14]") {
    Fixture f = makePlate();
    const AnnotationId id = add(f, "Mark",
                                {.type = AnnotationType::Centremark, .target = atHole(f.holeA)});
    const std::vector<ObjectId> dependencies =
        drawing::findAnnotation(f.document, id)->dependencies();
    CHECK(std::ranges::find(dependencies, ObjectId{f.view}) != dependencies.end());
    CHECK(std::ranges::find(dependencies, f.holeA) != dependencies.end());
}

TEST_CASE("Annotation_UnrelatedEditsDoNotRetargetIt", "[drawing][annotation][p14]") {
    Fixture f = makePlate();
    const AnnotationId id = add(f, "Callout",
                                {.type = AnnotationType::HoleCallout,
                                 .target = atHole(f.holeA),
                                 .placement = Point2D{250_mm, 200_mm}});
    const AnnotationDefinition before = drawing::findAnnotation(f.document, id)->definition();

    auto spare = std::make_unique<sketch::Sketch>("Spare", Frame3D::yz());
    addRectangle(*spare, 0_mm, 0_mm, 5_mm, 5_mm);
    const ObjectId spareId = require(f.document.addObject(std::move(spare)));
    REQUIRE(f.document.rename(spareId, "Renamed").has_value());
    REQUIRE(f.document.removeObject(spareId).has_value());
    f.regenerate();

    CHECK(drawing::findAnnotation(f.document, id)->definition() == before);
    CHECK(*drawing::annotationText(f.document, id, f.bodies()) == "Ø10 THRU");
}

// --- Failure atomicity ---------------------------------------------------------------------

TEST_CASE("Annotation_ARejectedEditLeavesItExactlyAsItWas", "[drawing][annotation][p14]") {
    Fixture f = makePlate();
    const AnnotationId id = add(f, "Note",
                                {.type = AnnotationType::Note,
                                 .text = "DEBURR ALL EDGES",
                                 .placement = Point2D{30_mm, 20_mm}});
    const AnnotationDefinition before = drawing::findAnnotation(f.document, id)->definition();

    const auto refuse = [&](std::string_view what, AnnotationDefinition d,
                            std::string_view expected) {
        INFO(what);
        const auto result = drawing::setAnnotationDefinition(f.document, id, d);
        REQUIRE_FALSE(result.has_value());
        CHECK_THAT(result.error().message, ContainsSubstring(std::string{expected}));
        CHECK(drawing::findAnnotation(f.document, id)->definition() == before);
        CHECK(drawn(f, id).texts.front().text == "DEBURR ALL EDGES");
    };

    AnnotationDefinition noView = before;
    noView.view = ViewId{};
    refuse("no view", noView, "must name the view");

    AnnotationDefinition noHeight = before;
    noHeight.style.height = 0_mm;
    refuse("no text height", noHeight, "greater than zero");

    AnnotationDefinition negativeHeight = before;
    negativeHeight.style.height = -1_mm;
    refuse("a negative text height", negativeHeight, "greater than zero");

    AnnotationDefinition twoTargets = before;
    twoTargets.type = AnnotationType::Leader;
    twoTargets.target.object = f.holeA;
    twoTargets.target.axis = AxisReference{};
    refuse("a target naming two things", twoTargets, "points at one thing");

    AnnotationDefinition missingView = before;
    missingView.view = ViewId::fromValue(9999);
    refuse("a view that is not there", missingView, "not a view of this document");

    AnnotationDefinition noMark = before;
    noMark.type = AnnotationType::Centremark;
    noMark.target = atHole(f.holeA);
    noMark.text.clear();
    noMark.armLength = 0_mm;
    refuse("a centre mark with no arms", noMark, "longer than nothing");
}

// --- Persistence ------------------------------------------------------------------------

TEST_CASE("Annotation_RoundTripsAndStillDrawsTheSame",
          "[drawing][annotation][p14][persistence]") {
    Fixture f = makePlate();
    std::vector<AnnotationId> ids;
    ids.push_back(add(f, "Note", {.type = AnnotationType::Note,
                                  .text = "DEBURR ALL EDGES",
                                  .style = TextStyle{5_mm},
                                  .placement = Point2D{30_mm, 20_mm}}));
    ids.push_back(add(f, "Callout", {.type = AnnotationType::HoleCallout,
                                     .target = atHole(f.holeA),
                                     .placement = Point2D{250_mm, 200_mm}}));
    ids.push_back(add(f, "Mark", {.type = AnnotationType::Centremark,
                                  .target = atHole(f.holeB),
                                  .armLength = 4_mm}));
    ids.push_back(add(f, "DatumA", {.type = AnnotationType::Datum,
                                    .target = atHole(f.holeA),
                                    .text = "B",
                                    .placement = Point2D{60_mm, 40_mm}}));
    ids.push_back(add(f, "Finish",
                      {.type = AnnotationType::SurfaceFinish,
                       .target = atHole(f.holeB),
                       .placement = Point2D{80_mm, 60_mm},
                       .finish = SurfaceFinish{Length::fromSi(1.6e-6), MaterialRemoval::Prohibited}}));

    std::vector<SceneItems> before;
    for (const AnnotationId id : ids) {
        before.push_back(drawn(f, id));
    }

    const TempDir directory;
    const std::filesystem::path path = directory.path() / "annotations.bcad";
    REQUIRE(io::saveDocument(f.document, path).has_value());
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());

    features::Regenerator reloaded;
    REQUIRE(reloaded.regenerateAll(*loaded).has_value());
    const features::Regenerator* r = &reloaded;
    const drawing::BodyLookup bodies = [r](ObjectId object) { return r->body(object); };

    for (std::size_t i = 0; i < ids.size(); ++i) {
        INFO("annotation " << ids[i].value());
        const drawing::Annotation* annotation = drawing::findAnnotation(*loaded, ids[i]);
        REQUIRE(annotation != nullptr);
        CHECK(annotation->definition() ==
              drawing::findAnnotation(f.document, ids[i])->definition());
        auto after = drawing::draw(*loaded, ids[i], bodies);
        REQUIRE(after.has_value());
        CHECK(after->lines == before[i].lines);
        CHECK(after->texts == before[i].texts);
    }
}

TEST_CASE("Annotation_TheFileCarriesNoDerivedText", "[drawing][annotation][p14][persistence]") {
    Fixture f = makePlate();
    (void)add(f, "Callout", {.type = AnnotationType::HoleCallout,
                             .target = atHole(f.holeA),
                             .placement = Point2D{250_mm, 200_mm}});

    const TempDir directory;
    const std::filesystem::path path = directory.path() / "a.bcad";
    REQUIRE(io::saveDocument(f.document, path).has_value());
    const std::string text = readFile(path);

    const auto at = text.find(R"("type": "annotation")");
    REQUIRE(at != std::string::npos);
    const std::string section = text.substr(at, 600);
    CHECK_THAT(section, !ContainsSubstring("THRU"));
    CHECK_THAT(section, !ContainsSubstring("\"text\""));
}

TEST_CASE("Annotation_AFileNamingAnUnknownTypeIsRefused",
          "[drawing][annotation][p14][persistence]") {
    Fixture f = makePlate();
    (void)add(f, "Note", {.type = AnnotationType::Note,
                          .text = "DEBURR ALL EDGES",
                          .placement = Point2D{30_mm, 20_mm}});
    const TempDir directory;
    const std::filesystem::path path = directory.path() / "bad.bcad";
    REQUIRE(io::saveDocument(f.document, path).has_value());

    std::string text = readFile(path);
    const std::string from = R"("type": "note")";
    const auto at = text.find(from);
    REQUIRE(at != std::string::npos);
    text.replace(at, from.size(), R"("type": "scribble")");
    writeFile(path, text);

    const auto loaded = io::loadDocument(path);
    REQUIRE_FALSE(loaded.has_value());
    CHECK_THAT(loaded.error().message, ContainsSubstring("unknown annotation type 'scribble'"));
}

// --- Determinism ---------------------------------------------------------------------------

TEST_CASE("Annotation_DrawsIdenticallyEveryTime", "[drawing][annotation][p14][determinism]") {
    Fixture f = makePlate();
    const AnnotationId callout = add(f, "Callout",
                                     {.type = AnnotationType::HoleCallout,
                                      .target = atHole(f.holeA),
                                      .placement = Point2D{250_mm, 200_mm}});
    const AnnotationId mark = add(f, "Mark",
                                  {.type = AnnotationType::Centremark, .target = atHole(f.holeB)});

    for (const AnnotationId id : {callout, mark}) {
        INFO("annotation " << id.value());
        const SceneItems first = drawn(f, id);
        for (int i = 0; i < 6; ++i) {
            const SceneItems again = drawn(f, id);
            CHECK(again.lines == first.lines); // every point, bit for bit
            CHECK(again.texts == first.texts);
        }
    }
}

TEST_CASE("Annotation_ASheetDrawsItsAnnotationsInIdOrder",
          "[drawing][annotation][p14][determinism]") {
    // The order has to come from something that does not vary, or two runs of
    // one drawing would not compare.
    Fixture f = makePlate();
    const AnnotationId first = add(f, "Note", {.type = AnnotationType::Note,
                                               .text = "ONE",
                                               .placement = Point2D{10_mm, 10_mm}});
    const AnnotationId second = add(f, "Other", {.type = AnnotationType::Note,
                                                 .text = "TWO",
                                                 .placement = Point2D{20_mm, 20_mm}});
    CHECK(first < second);
    CHECK(drawing::annotationsOn(f.document, f.view) ==
          std::vector<AnnotationId>{first, second});

    auto all = drawing::drawAnnotations(f.document, f.view, f.bodies());
    REQUIRE(all.has_value());
    REQUIRE(all->texts.size() == 2);
    CHECK(all->texts[0].text == "ONE");
    CHECK(all->texts[1].text == "TWO");
}

TEST_CASE("Annotation_ASheetThatCannotDrawOneDoesNotDrawAnyOfThem",
          "[drawing][annotation][p14]") {
    // A drawing that quietly dropped the annotation it could not resolve
    // would look complete and not be.
    Fixture f = makePlate();
    (void)add(f, "Note", {.type = AnnotationType::Note,
                          .text = "DEBURR ALL EDGES",
                          .placement = Point2D{30_mm, 20_mm}});
    (void)add(f, "Callout", {.type = AnnotationType::HoleCallout,
                             .target = atHole(f.holeA),
                             .placement = Point2D{250_mm, 200_mm}});

    REQUIRE(f.document.removeObject(f.holeB).has_value());
    REQUIRE(f.document.removeObject(f.holeA).has_value());
    f.regenerate();

    const auto all = drawing::drawAnnotations(f.document, f.view, f.bodies());
    REQUIRE_FALSE(all.has_value());
}

TEST_CASE("Annotation_IsListedOnItsViewAndCanBeRemoved", "[drawing][annotation][p14]") {
    Fixture f = makePlate();
    CHECK(drawing::annotations(f.document).empty());
    const AnnotationId id = add(f, "Note", {.type = AnnotationType::Note,
                                            .text = "X",
                                            .placement = Point2D{10_mm, 10_mm}});
    CHECK(drawing::annotations(f.document) == std::vector<AnnotationId>{id});
    CHECK(drawing::annotationsOn(f.document, f.view) == std::vector<AnnotationId>{id});

    REQUIRE(drawing::removeAnnotation(f.document, id).has_value());
    CHECK(drawing::annotations(f.document).empty());
    CHECK(drawing::findAnnotation(f.document, id) == nullptr);
}

// --- The scene boundary --------------------------------------------------------------------

TEST_CASE("Annotation_EverythingItDrawsPassesTheScenesOwnChecks",
          "[drawing][annotation][p14]") {
    // ADR-016: the scene validates itself once, in `drawing`, so every writer
    // inherits the guarantee rather than trusting.
    Fixture f = makePlate(DrawingScale{1, 1}, StandardView::Front);
    std::vector<AnnotationId> ids;
    ids.push_back(add(f, "Note", {.type = AnnotationType::Note,
                                  .text = "DEBURR ALL EDGES",
                                  .placement = Point2D{30_mm, 20_mm}}));
    ids.push_back(add(f, "Callout", {.type = AnnotationType::HoleCallout,
                                     .target = atHole(f.holeA),
                                     .placement = Point2D{250_mm, 200_mm}}));
    ids.push_back(add(f, "Axis", {.type = AnnotationType::Centreline,
                                  .target = atHole(f.holeA),
                                  .extension = 3_mm}));
    ids.push_back(add(f, "Mark", {.type = AnnotationType::Centremark,
                                  .target = atHole(f.holeB)}));
    ids.push_back(add(f, "DatumA", {.type = AnnotationType::Datum,
                                    .target = atHole(f.holeA),
                                    .text = "C",
                                    .placement = Point2D{60_mm, 40_mm}}));
    for (const AnnotationId id : ids) {
        INFO("annotation " << id.value());
        CHECK(validate(drawn(f, id)).has_value());
    }
    CHECK(validate(*drawing::drawAnnotations(f.document, f.view, f.bodies())).has_value());
}

TEST_CASE("Annotation_TheSceneRefusesWhatAWriterCouldNotDraw", "[drawing][annotation][p14]") {
    SceneItems oneSidedLine;
    oneSidedLine.lines.push_back(SceneLine{.points = {Point2D{1_mm, 1_mm}}});
    CHECK_THAT(validate(oneSidedLine).error().message, ContainsSubstring("at least two points"));

    SceneItems emptyText;
    emptyText.texts.push_back(SceneText{.at = Point2D{}, .text = "", .height = 3.5_mm});
    CHECK_THAT(validate(emptyText).error().message, ContainsSubstring("nothing in it"));

    SceneItems noHeight;
    noHeight.texts.push_back(SceneText{.at = Point2D{}, .text = "A", .height = 0_mm});
    CHECK_THAT(validate(noHeight).error().message, ContainsSubstring("greater than zero"));
}

// --- What the review found ------------------------------------------------------------

TEST_CASE("Annotation_ACentrelineIsTheSameLengthWhetherHiddenLinesAreShown",
          "[drawing][annotation][p14]") {
    // The defect this closes: the centreline's span was measured from the
    // lines the view was SHOWING, so turning hidden detail off shortened it.
    // A display setting must not change the geometry of an annotation, and
    // this one would have done it invisibly -- the line would simply have
    // been a bit short.
    //
    // The plate is 20 thick with a hole through it; in a front view the
    // hole's own edges are hidden, so they are exactly what a policy change
    // removes.
    const auto spanWith = [](bool showHidden) {
        Fixture f = makePlate(DrawingScale{1, 1}, StandardView::Front);
        ViewDefinition view = drawing::findView(f.document, f.view)->definition();
        view.hiddenLine.showHidden = showHidden;
        REQUIRE(drawing::setViewDefinition(f.document, f.view, view).has_value());
        const AnnotationId id = add(f, "Axis",
                                    {.type = AnnotationType::Centreline,
                                     .target = atHole(f.holeA),
                                     .extension = 3_mm});
        return lengthOf(drawn(f, id).lines.front());
    };

    const double shown = spanWith(true);
    const double hidden = spanWith(false);
    CHECK_THAT(shown, WithinAbs(26.0, 1e-6)); // the plate's 20, plus 3 either end
    CHECK_THAT(hidden, WithinAbs(shown, 1e-9));
}
