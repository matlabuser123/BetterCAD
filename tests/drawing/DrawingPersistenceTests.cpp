#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Configurations.hpp>
#include <bettercad/assembly/Resolution.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/drawing/Annotations.hpp>
#include <bettercad/drawing/Bom.hpp>
#include <bettercad/drawing/Commands.hpp>
#include <bettercad/drawing/Dimensions.hpp>
#include <bettercad/drawing/Regeneration.hpp>
#include <bettercad/drawing/Resolution.hpp>
#include <bettercad/drawing/Section.hpp>
#include <bettercad/drawing/Sheets.hpp>
#include <bettercad/drawing/Tolerance.hpp>
#include <bettercad/drawing/Views.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <clocale>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <format>
#include <memory>
#include <string>
#include <string_view>
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
using drawing::DimensionDefinition;
using drawing::DimensionTarget;
using drawing::DimensionType;
using drawing::DrawingScale;
using drawing::SheetDefinition;
using drawing::StandardView;
using drawing::ViewDefinition;
using drawing::ViewSubject;
using features::NodeState;

// P14-PERSIST-001: what a .bcad file says about a drawing.
//
// THE ONE RULE. The file carries canonical drawing INTENT and never treats
// generated drawing geometry as authoritative.
//
// WHAT THIS SUITE IS FOR, and what it deliberately is not. Every P14
// milestone shipped its own serializer AND its own persistence tests as it
// went: SheetFileTests has eleven, ViewKindTests round-trips every view kind,
// DimensionTests asserts the file carries no measured value, ToleranceTests
// the same for GD&T, BomTests that a saved quantity cannot override the
// assembly. None of that is repeated here.
//
// What no per-type test can do is look at the WHOLE file at once, and that is
// what this milestone is: one document carrying every P14 object kind
// together, and the cross-cutting properties that only appear when they are
// all in it --
//
//     one document, every kind, one round trip, every ID compared
//     the same document serialized repeatedly, byte for byte
//     save -> load -> save, byte for byte
//     the file searched for derived geometry, system-wide
//     the file searched for positional identity, system-wide
//     malformed input, across the kinds, with nothing published
//     a reference saved while UNRESOLVED, and its recovery after loading
//
// A per-type test can say "a dimension carries no measured value". Only a
// whole-drawing test can say "this file contains no derived geometry at all".
namespace {

constexpr double kMm = 1e-9;

struct Drawing {
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
    features::RegenerationReport regenerate() {
        assembly::registerHandlers(regenerator, nullptr, nullptr);
        drawing::registerHandlers(regenerator);
        auto report = regenerator.regenerateAll(document);
        REQUIRE(report.has_value());
        return *report;
    }
    [[nodiscard]] std::string json() const {
        auto text = io::documentToJson(document);
        REQUIRE(text.has_value());
        return *text;
    }
};

struct Block {
    ObjectId sketch{};
    ObjectId pad{};
    std::array<EntityId, 4> lines{};
    std::array<EntityId, 4> corners{};
};

Block addBlock(Drawing& d, const std::string& name, Length width = 100_mm, Length depth = 60_mm,
               Length height = 40_mm) {
    Block block;
    auto sketch = std::make_unique<sketch::Sketch>(name + "Profile", Frame3D::xy());
    block.corners = {
        require(sketch->addPoint(Point2D{0_mm, 0_mm})),
        require(sketch->addPoint(Point2D{width, 0_mm})),
        require(sketch->addPoint(Point2D{width, depth})),
        require(sketch->addPoint(Point2D{0_mm, depth})),
    };
    for (std::size_t i = 0; i < 4; ++i) {
        block.lines[i] = require(sketch->addLine(block.corners[i], block.corners[(i + 1) % 4]));
    }
    block.sketch = require(d.document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        name, {.profile = SketchId::fromValue(block.sketch.value()), .depth = height});
    REQUIRE(extrude.has_value());
    block.pad = require(d.document.addObject(std::move(*extrude)));
    return block;
}

PlaneReference sideOf(const Block& block, std::size_t entity) {
    return PlaneReference{.object = block.pad,
                          .face = FaceSelector{.role = FaceRole::Side, .entity = block.lines[entity]}};
}

/// require(), but it says WHICH object could not be created and why. The
/// fixture below builds fifteen, and a bare assertion inside a helper names
/// none of them.
template <typename T>
T made(std::string_view what, Result<T> result) {
    std::string why;
    if (!result) {
        why = std::format("{}: {}", what, result.error().message);
    }
    INFO(why);
    REQUIRE(result.has_value());
    return *result;
}

SheetDefinition a3(DrawingScale scale = DrawingScale{1, 1}) {
    return SheetDefinition{.format = drawing::SheetFormat::A3,
                           .orientation = drawing::SheetOrientation::Landscape,
                           .margins = drawing::SheetMargins{10_mm, 10_mm, 10_mm, 10_mm},
                           .scale = scale};
}

/// Everything P14 can put in a document, in one document.
///
/// Two sheets; a base view, a projected view, a section and a detail; an
/// assembly view of two occurrences of one part; a dimension carrying an ISO
/// 286 fit; a datum feature symbol and a feature-control frame citing three
/// datums in order; a note; a BOM table; and two balloons.
struct Everything {
    Drawing d;
    SheetId sheetA{};
    SheetId sheetB{};
    Block block{};
    ObjectId part{};
    ComponentId first{};
    ComponentId second{};
    ViewId base{};
    ViewId projected{};
    ViewId section{};
    ViewId detail{};
    ViewId assemblyView{};
    DimensionId width{};
    AnnotationId note{};
    AnnotationId datum{};
    AnnotationId frame{};
    AnnotationId bom{};
    AnnotationId balloonA{};
    AnnotationId balloonB{};
};

Everything everything() {
    Everything e;
    e.sheetA = made("sheet one", drawing::createSheet(e.d.document, "SheetOne", a3()));
    e.sheetB = made("sheet two",
                    drawing::createSheet(e.d.document, "SheetTwo", a3(DrawingScale{1, 2})));
    e.block = addBlock(e.d, "Block");
    e.part = e.block.pad;
    e.first = require(assembly::createComponent(
        e.d.document, "First",
        {.part = e.part, .placement = ComponentPlacement{.translation = {0_mm, 0_mm, 0_mm}}}));
    e.second = require(assembly::createComponent(
        e.d.document, "Second",
        {.part = e.part, .placement = ComponentPlacement{.translation = {160_mm, 0_mm, 0_mm}}}));
    e.d.regenerate();

    e.base = made("the base view", drawing::createView(
        e.d.document, "Front",
        ViewDefinition{.sheet = e.sheetA,
                       .source = ObjectReference{e.part},
                       .orientation = StandardView::Front,
                       .placement = Point2D{150_mm, 150_mm}}));
    e.projected = made("the projected view", drawing::createView(
        e.d.document, "Top",
        ViewDefinition{.kind = drawing::ViewKind::Projected,
                       .sheet = e.sheetA,
                       .parent = e.base,
                       .direction = drawing::ProjectedDirection::Top,
                       .spacing = 90_mm}));
    e.section = made("the section view", drawing::createView(
        e.d.document, "SectionAA",
        ViewDefinition{.kind = drawing::ViewKind::Section,
                       .sheet = e.sheetA,
                       .parent = e.base,
                       .section = drawing::CuttingPlane{.origin = Point3D{50_mm, 30_mm, 20_mm},
                                                        .normal = Direction3D::unitY()},
                       .spacing = 90_mm}));
    e.detail = made("the detail view", drawing::createView(
        e.d.document, "DetailB",
        ViewDefinition{.kind = drawing::ViewKind::Detail,
                       .sheet = e.sheetA,
                       .parent = e.base,
                       .detail = drawing::DetailRegion{.centre = Point2D{150_mm, 150_mm},
                                                       .radius = 20_mm},
                       .scale = DrawingScale{2, 1},
                       .placement = Point2D{330_mm, 150_mm}}));
    e.assemblyView = made("the assembly view", drawing::createView(
        e.d.document, "Assembly",
        ViewDefinition{.sheet = e.sheetB,
                       .subject = ViewSubject::Assembly,
                       .orientation = StandardView::Front,
                       .placement = Point2D{200_mm, 150_mm}}));

    // A dimension whose tolerance is an ISO 286 fit, so the file has to carry
    // the DESIGNATION and not the deviations it resolves to.
    e.width = made("the width dimension", drawing::createDimension(
        e.d.document, "Width",
        DimensionDefinition{
            .view = e.base,
            .type = DimensionType::Linear,
            .from = DimensionTarget{.plane = sideOf(e.block, 3)},
            .to = DimensionTarget{.plane = sideOf(e.block, 1)},
            .format = drawing::DimensionFormat{.decimals = 2},
            // A FIT, and nothing else: P14-TOL-001 refuses a tolerance that
            // carries both a designation and a deviation pair, because the
            // designation already says what the deviations are. So the file
            // has to carry "H7" and resolve it through ISO 286 on every call.
            .tolerance = drawing::DimensionTolerance{
                .fit = drawing::FitDesignation{.role = drawing::FitRole::Hole,
                                               .letter = 'H',
                                               .grade = 7},
                .display = drawing::ToleranceDisplay::Limits},
            .placement = Point2D{150_mm, 90_mm}}));

    e.note = made("the note", drawing::createAnnotation(
        e.d.document, "Note",
        AnnotationDefinition{.view = e.base,
                             .type = AnnotationType::Note,
                             .text = "BREAK SHARP EDGES",
                             .placement = Point2D{40_mm, 40_mm}}));
    e.datum = made("the datum symbol", drawing::createAnnotation(
        e.d.document, "DatumA",
        AnnotationDefinition{.view = e.base,
                             .type = AnnotationType::Datum,
                             .target = AnnotationTarget{.plane = sideOf(e.block, 0)},
                             .text = "A",
                             .placement = Point2D{100_mm, 60_mm}}));
    // Three datums IN ORDER. A|B|C must not come back as B|A|C.
    e.frame = made("the feature-control frame", drawing::createAnnotation(
        e.d.document, "Frame",
        AnnotationDefinition{
            .view = e.base,
            .type = AnnotationType::FeatureControlFrame,
            .target = AnnotationTarget{.plane = sideOf(e.block, 1)},
            .placement = Point2D{120_mm, 50_mm},
            .frame = drawing::FeatureControlFrame{
                .characteristic = drawing::GeometricCharacteristic::Position,
                .zone = drawing::ToleranceZone::Cylindrical,
                .tolerance = 0.05_mm,
                .datums = {drawing::DatumReference{'A'}, drawing::DatumReference{'B'},
                           drawing::DatumReference{'C'}}}}));

    e.bom = made("the BOM table", drawing::createAnnotation(
        e.d.document, "Bom",
        AnnotationDefinition{.view = e.assemblyView,
                             .type = AnnotationType::BomTable,
                             .placement = Point2D{300_mm, 250_mm}}));
    e.balloonA = made("balloon A", drawing::createAnnotation(
        e.d.document, "B1",
        AnnotationDefinition{.view = e.assemblyView,
                             .type = AnnotationType::Balloon,
                             .target = AnnotationTarget{.object = ObjectId{e.first}},
                             .placement = Point2D{120_mm, 120_mm}}));
    e.balloonB = made("balloon B", drawing::createAnnotation(
        e.d.document, "B2",
        AnnotationDefinition{.view = e.assemblyView,
                             .type = AnnotationType::Balloon,
                             .target = AnnotationTarget{.object = ObjectId{e.second}},
                             .placement = Point2D{280_mm, 120_mm}}));
    e.d.regenerate();
    return e;
}

/// Every object id in the document, ascending.
std::vector<std::uint64_t> idsOf(const Document& document) {
    std::vector<std::uint64_t> ids;
    for (const DocumentObject& object : document.objects()) {
        ids.push_back(object.id().value());
    }
    std::ranges::sort(ids);
    return ids;
}

} // namespace

// --- One document, every kind, one round trip ---------------------------------------------------

TEST_CASE("Persistence_AWholeDrawingRoundTripsWithEveryIdAndEveryReference",
          "[drawing][persist][p14]") {
    Everything e = everything();
    TempDir dir;
    const auto path = dir.path() / "everything.bcad";

    const std::string before = e.d.json();
    const std::vector<std::uint64_t> idsBefore = idsOf(e.d.document);
    const std::size_t objectsBefore = e.d.document.objectCount();
    REQUIRE(io::saveDocument(e.d.document, path).has_value());

    // Destroyed, not merely re-read: the loaded document shares nothing with
    // the one that was saved.
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    Drawing after;
    after.document = std::move(*loaded);

    // The canonical state, byte for byte.
    CHECK(after.json() == before);
    CHECK(idsOf(after.document) == idsBefore);
    CHECK(after.document.objectCount() == objectsBefore);

    // Every drawing object is back, under the ID it had.
    CHECK(drawing::findSheet(after.document, e.sheetA) != nullptr);
    CHECK(drawing::findSheet(after.document, e.sheetB) != nullptr);
    for (const ViewId view : {e.base, e.projected, e.section, e.detail, e.assemblyView}) {
        INFO("view " << view.value());
        CHECK(drawing::findView(after.document, view) != nullptr);
    }
    CHECK(drawing::findDimension(after.document, e.width) != nullptr);
    for (const AnnotationId annotation :
         {e.note, e.datum, e.frame, e.bom, e.balloonA, e.balloonB}) {
        INFO("annotation " << annotation.value());
        CHECK(drawing::findAnnotation(after.document, annotation) != nullptr);
    }

    // The relationships, not just the objects.
    CHECK(drawing::findView(after.document, e.projected)->definition().parent ==
          std::optional<ViewId>{e.base});
    CHECK(drawing::findView(after.document, e.section)->definition().parent ==
          std::optional<ViewId>{e.base});
    CHECK(drawing::findView(after.document, e.detail)->definition().parent ==
          std::optional<ViewId>{e.base});
    CHECK(drawing::findView(after.document, e.base)->definition().sheet == e.sheetA);
    CHECK(drawing::findView(after.document, e.assemblyView)->definition().sheet == e.sheetB);
    CHECK(drawing::findDimension(after.document, e.width)->definition().view == e.base);

    // The balloons still name their OWN occurrences, and not each other's.
    CHECK(drawing::findAnnotation(after.document, e.balloonA)->definition().target.object ==
          ObjectId{e.first});
    CHECK(drawing::findAnnotation(after.document, e.balloonB)->definition().target.object ==
          ObjectId{e.second});

    // Datum order survives exactly: A|B|C, not B|A|C.
    const auto& frame = drawing::findAnnotation(after.document, e.frame)->definition().frame;
    REQUIRE(frame.has_value());
    REQUIRE(frame->datums.size() == 3);
    CHECK(frame->datums[0].letter == 'A');
    CHECK(frame->datums[1].letter == 'B');
    CHECK(frame->datums[2].letter == 'C');

    // The fit is carried as its DESIGNATION, so it still resolves to the
    // standard's numbers rather than to whatever was written down.
    const auto& tolerance = drawing::findDimension(after.document, e.width)->definition().tolerance;
    REQUIRE(tolerance.has_value());
    REQUIRE(tolerance->fit.has_value());
    CHECK(tolerance->fit->letter == 'H');
    CHECK(tolerance->fit->grade == 7);
    CHECK(tolerance->display == drawing::ToleranceDisplay::Limits);
}

TEST_CASE("Persistence_ALoadedDrawingRegeneratesAndDrawsWhatItDrewBefore",
          "[drawing][persist][p14]") {
    Everything e = everything();
    TempDir dir;
    const auto path = dir.path() / "everything.bcad";

    const auto measuredBefore = drawing::measure(e.d.document, e.width, e.d.bodies(), e.d.transforms());
    REQUIRE(measuredBefore.has_value());
    const auto drawnBefore =
        drawing::projectedGeometry(e.d.document, e.base, e.d.bodies(), e.d.transforms());
    REQUIRE(drawnBefore.has_value());
    const auto bomBefore = drawing::billOfMaterials(e.d.document, e.assemblyView);
    REQUIRE(bomBefore.has_value());
    REQUIRE(io::saveDocument(e.d.document, path).has_value());

    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    Drawing after;
    after.document = std::move(*loaded);
    // The PRODUCTION regeneration path, not a persistence-only one.
    const auto report = after.regenerate();
    CHECK(report.succeeded());

    const auto measuredAfter =
        drawing::measure(after.document, e.width, after.bodies(), after.transforms());
    REQUIRE(measuredAfter.has_value());
    CHECK(measuredAfter->text == measuredBefore->text);
    CHECK_THAT(measuredAfter->length->in(units::mm),
               WithinAbs(measuredBefore->length->in(units::mm), kMm));

    const auto drawnAfter =
        drawing::projectedGeometry(after.document, e.base, after.bodies(), after.transforms());
    REQUIRE(drawnAfter.has_value());
    CHECK(drawnAfter->edges.size() == drawnBefore->edges.size());
    CHECK_THAT((drawnAfter->bounds.max.x - drawnAfter->bounds.min.x).in(units::mm),
               WithinAbs((drawnBefore->bounds.max.x - drawnBefore->bounds.min.x).in(units::mm), kMm));

    const auto bomAfter = drawing::billOfMaterials(after.document, e.assemblyView);
    REQUIRE(bomAfter.has_value());
    CHECK(bomAfter->totalOccurrences() == bomBefore->totalOccurrences());
    CHECK(bomAfter->rows.size() == bomBefore->rows.size());

    // Every drawing object regenerated, none was silently left unvalidated.
    for (const ObjectId id : {ObjectId{e.base}, ObjectId{e.projected}, ObjectId{e.section},
                              ObjectId{e.detail}, ObjectId{e.assemblyView}, ObjectId{e.width},
                              ObjectId{e.note}, ObjectId{e.frame}, ObjectId{e.bom},
                              ObjectId{e.balloonA}}) {
        INFO("object " << id.value());
        const auto state = after.regenerator.state(id);
        REQUIRE(state.has_value());
        CHECK(*state == NodeState::Regenerated);
    }
}

// --- Deterministic serialization ----------------------------------------------------------------

TEST_CASE("Persistence_TheSameDrawingSerializesToTheSameBytesEveryTime",
          "[drawing][persist][p14][determinism]") {
    Everything e = everything();
    std::vector<std::string> runs;
    for (int run = 0; run < 5; ++run) {
        runs.push_back(e.d.json());
    }
    CHECK(std::ranges::adjacent_find(runs, std::not_equal_to<>{}) == runs.end());

    // Nothing in the output depends on where anything happens to sit in
    // memory, so a deep copy of the document serializes identically too.
    Document copy = e.d.document.clone();
    auto copied = io::documentToJson(copy);
    REQUIRE(copied.has_value());
    CHECK(*copied == runs.front());
}

TEST_CASE("Persistence_SaveThenLoadThenSaveIsByteIdentical",
          "[drawing][persist][p14][determinism]") {
    // The stronger statement: the format is a fixed point. If loading
    // normalized anything -- reordered a collection, dropped a default,
    // rounded a number -- the second file would differ from the first.
    Everything e = everything();
    TempDir dir;
    const auto first = dir.path() / "first.bcad";
    const auto second = dir.path() / "second.bcad";

    REQUIRE(io::saveDocument(e.d.document, first).has_value());
    auto loaded = io::loadDocument(first);
    REQUIRE(loaded.has_value());
    REQUIRE(io::saveDocument(*loaded, second).has_value());

    const auto readAll = [](const std::filesystem::path& path) {
        std::ifstream stream{path, std::ios::binary};
        REQUIRE(stream.good());
        return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    };
    CHECK(readAll(first) == readAll(second));
    CHECK_FALSE(readAll(first).empty());

    // And a third pass, so a drift that needed two generations to show would
    // not hide.
    auto again = io::loadDocument(second);
    REQUIRE(again.has_value());
    const auto third = dir.path() / "third.bcad";
    REQUIRE(io::saveDocument(*again, third).has_value());
    CHECK(readAll(third) == readAll(first));
}

// --- Nothing derived is in the file --------------------------------------------------------------

TEST_CASE("Persistence_TheFileHoldsNoDerivedDrawingGeometryAnywhere",
          "[drawing][persist][p14][derived]") {
    // A per-type test can say "a dimension carries no measured value". This
    // is the whole-document statement: with every P14 object kind in one
    // file, NONE of the derived vocabulary appears anywhere in it.
    Everything e = everything();
    const std::string json = e.d.json();

    // Searched as JSON KEYS, not as bare substrings. That distinction is the
    // test working rather than the test being lucky: "row" is inside
    // "row_height" and "quantity" inside "quantity_width", and both of those
    // are TABLE LAYOUT settings -- how tall a row is drawn, how wide the
    // quantity column is -- which are drawing intent and belong in the file.
    // What must not be there is a row, or a quantity, or an item number.
    for (const std::string_view forbidden :
         {"\"visible\"", "\"hidden\"", "\"edges\"", "\"silhouette\"", "\"hlr\"",
          "\"polyline\"", "\"segments\"", "\"measured\"", "\"measured_value\"",
          "\"value\"", "\"quantity\"", "\"item_number\"", "\"item\"", "\"row\"",
          "\"rows\"", "\"occurrences\"", "\"leader\"", "\"arrowhead\"",
          "\"extension_line\"", "\"hatch_line\"", "\"lines\"", "\"texts\"", "\"scene\"",
          "\"bounds\"", "\"transform\"", "\"solved\"", "\"cut_loop\"",
          "\"usable_region\"", "\"sheet_number\"", "\"sheet_count\"", "\"width_mm\"",
          "\"height_mm\""}) {
        INFO("forbidden key: " << forbidden);
        CHECK_THAT(json, !ContainsSubstring(std::string{forbidden}));
    }

    // The table's own layout IS there, and is intent: it says how the table is
    // drawn, never what it says.
    CHECK_THAT(json, ContainsSubstring("\"row_height\""));
    CHECK_THAT(json, ContainsSubstring("\"quantity_width\""));

    // The measured width is 100 mm and the block is 100 x 60 x 40. The file
    // must not contain the DRAWN answer -- and the model's own 0.1 m does
    // belong to the sketch, so this checks the text a dimension would show.
    CHECK_THAT(json, !ContainsSubstring("\"100.00\""));
    CHECK_THAT(json, !ContainsSubstring("\"text\": \"100"));
    // The H7 fit resolves to +0.000/+0.035 at 100 mm. The file carries the
    // DESIGNATION, so neither deviation may appear in it.
    CHECK_THAT(json, !ContainsSubstring("3.5e-05"));
    CHECK_THAT(json, !ContainsSubstring("0.000035"));

    // What IS there: the intent.
    CHECK_THAT(json, ContainsSubstring("\"sheet\""));
    CHECK_THAT(json, ContainsSubstring("\"role\": \"side\""));
    CHECK_THAT(json, ContainsSubstring("\"entity\""));
    CHECK_THAT(json, ContainsSubstring("BREAK SHARP EDGES"));
}

TEST_CASE("Persistence_SolvedAssemblyTransformsAreNotInTheFile",
          "[drawing][persist][p14][derived]") {
    // ADR-005: a placement is intent and a solved transform is derived. The
    // second occurrence is placed at x = 160 mm, which IS intent and is in
    // the file; what must not be there is anything the solver produced.
    Everything e = everything();
    REQUIRE(e.d.regenerator.transform(e.first) != nullptr); // it HAS solved
    const std::string json = e.d.json();

    CHECK_THAT(json, !ContainsSubstring("solved"));
    CHECK_THAT(json, !ContainsSubstring("\"matrix\""));
    CHECK_THAT(json, !ContainsSubstring("\"rotation_matrix\""));
    // The intent is there.
    CHECK_THAT(json, ContainsSubstring("0.16"));
}

TEST_CASE("Persistence_TheFileCarriesNoCommandHistory", "[drawing][persist][p14]") {
    // P14-CMD-001's boundary, restated where the file is the subject: a
    // session's undo stack is not document state.
    Everything e = everything();
    CommandHistory history;
    REQUIRE(history
                .execute(e.d.document,
                         std::make_unique<drawing::MoveViewCommand>(e.base, Point2D{111_mm, 99_mm}))
                .has_value());
    REQUIRE(history.undoCount() == 1);

    const std::string json = e.d.json();
    CHECK_THAT(json, !ContainsSubstring("history"));
    CHECK_THAT(json, !ContainsSubstring("\"undo\""));
    CHECK_THAT(json, !ContainsSubstring("\"redo\""));
    CHECK_THAT(json, !ContainsSubstring("command"));
    // The EDIT is in the file; the fact that a command made it is not.
    CHECK_THAT(json, ContainsSubstring("0.111"));
}

// --- Every reference is semantic -----------------------------------------------------------------

TEST_CASE("Persistence_EveryPersistedReferenceIsSemanticAndNeverPositional",
          "[drawing][persist][p14][stref]") {
    // P14-STREF-001's rule, asserted against a file containing every kind of
    // drawing reference at once: a view to a model object, a projected view
    // to its parent, a dimension to two named faces, a datum symbol to a
    // face, a frame to a face, and two balloons to two occurrences.
    Everything e = everything();
    const std::string json = e.d.json();

    for (const std::string_view forbidden :
         {"face_index", "edge_index", "shape_index", "vertex_index", "ordinal", "traversal",
          "pointer", "address", "handle", "occt", "tshape", "nearest", "closest", "screen",
          "index"}) {
        INFO("forbidden identity: " << forbidden);
        CHECK_THAT(json, !ContainsSubstring(std::string{forbidden}));
    }

    // And the semantic forms ARE present.
    CHECK_THAT(json, ContainsSubstring("\"role\": \"side\""));
    CHECK_THAT(json, ContainsSubstring("\"entity\""));
    CHECK_THAT(json, ContainsSubstring("\"parent\""));
    CHECK_THAT(json, ContainsSubstring("\"target\""));
}

// --- Unresolved references ------------------------------------------------------------------------

TEST_CASE("Persistence_AReferenceSavedWhileUnresolvedStaysUnresolvedAndThenRecovers",
          "[drawing][persist][p14][stref]") {
    // Unresolved is not malformed. A balloon whose occurrence this
    // configuration suppresses is valid intent whose target is not here NOW,
    // so it must SAVE, LOAD, still be unresolved, and recover when the
    // configuration comes back -- naming the same occurrence throughout, with
    // an identical sibling sitting there for a rebinding implementation to
    // take instead.
    Everything e = everything();
    const ConfigurationId reduced = require(e.d.document.createConfiguration("Reduced"));
    REQUIRE(assembly::suppressComponent(e.d.document, reduced, e.second, true).has_value());
    REQUIRE(e.d.document.setActiveConfiguration(reduced).has_value());
    e.d.regenerate();

    const auto stateBefore =
        drawing::annotationResolution(e.d.document, e.balloonB, e.d.bodies(), e.d.transforms());
    REQUIRE(stateBefore.state == drawing::ResolutionState::Unresolved);

    TempDir dir;
    const auto path = dir.path() / "suppressed.bcad";
    REQUIRE(io::saveDocument(e.d.document, path).has_value());

    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    Drawing after;
    after.document = std::move(*loaded);
    // The configuration that was active is part of the document, so it is
    // still active after the load.
    CHECK(after.document.activeConfiguration() == reduced);
    after.regenerate();

    // Still unresolved, and still naming what it always named.
    const auto stateAfter =
        drawing::annotationResolution(after.document, e.balloonB, after.bodies(), after.transforms());
    CHECK(stateAfter.state == drawing::ResolutionState::Unresolved);
    CHECK(drawing::findAnnotation(after.document, e.balloonB)->definition().target.object ==
          ObjectId{e.second});
    // The sibling is active and identical, and is NOT adopted.
    CHECK(drawing::drawnOccurrences(after.document, e.assemblyView)->size() == 1);

    // Recovery, after the load.
    REQUIRE(after.document.setActiveConfiguration(std::nullopt).has_value());
    after.regenerate();
    const auto recovered =
        drawing::annotationResolution(after.document, e.balloonB, after.bodies(), after.transforms());
    CHECK(recovered.state == drawing::ResolutionState::Resolved);
    CHECK(drawing::findAnnotation(after.document, e.balloonB)->definition().target.object ==
          ObjectId{e.second});
}

// --- Malformed files --------------------------------------------------------------------------------

TEST_CASE("Persistence_MalformedDrawingFilesAreRefusedAndNothingIsPublished",
          "[drawing][persist][p14][malformed]") {
    // Load is parse-then-publish: `loadDocument` builds a candidate and
    // returns it, so a refusal cannot half-populate anything. Each case below
    // corrupts ONE thing in an otherwise valid file.
    Everything e = everything();
    const std::string good = e.d.json();
    REQUIRE(io::documentFromJson(good).has_value());

    const auto refuse = [&](const std::string& what, const std::string& from, const std::string& to) {
        INFO(what);
        REQUIRE(good.find(from) != std::string::npos);
        std::string broken = good;
        broken.replace(broken.find(from), from.size(), to);
        const auto loaded = io::documentFromJson(broken);
        CHECK_FALSE(loaded.has_value());
        if (!loaded) {
            INFO("diagnostic: " << loaded.error().message);
            CHECK_FALSE(loaded.error().message.empty());
        }
    };

    refuse("an unknown object type", "\"type\": \"sheet\"", "\"type\": \"sheat\"");
    refuse("an unknown field in a sheet", "\"format\": \"A3\"", "\"formats\": \"A3\"");
    refuse("an unknown sheet format", "\"format\": \"A3\"", "\"format\": \"A97\"");
    refuse("an unknown orientation", "\"orientation\": \"landscape\"", "\"orientation\": \"sideways\"");
    refuse("an unknown view kind", "\"kind\": \"projected\"", "\"kind\": \"squashed\"");
    refuse("an unknown dimension type", "\"type\": \"linear\"", "\"type\": \"wiggly\"");
    refuse("an unknown annotation type", "\"type\": \"note\"", "\"type\": \"scribble\"");
    refuse("a non-finite placement", "\"x\": 0.15", "\"x\": 1e999");
    refuse("a string where a number belongs", "\"y\": 0.15", "\"y\": \"middle\"");
    refuse("a null where an object belongs", "\"margins\":", "\"marginz\":");
}

TEST_CASE("Persistence_ADuplicateObjectIdIsRefused", "[drawing][persist][p14][malformed]") {
    // Two objects claiming one identity is not a drawing with a small problem;
    // every reference in the file becomes ambiguous.
    Everything e = everything();
    const std::string good = e.d.json();

    const std::string first = std::format("\"id\": {},", e.sheetA.value());
    const std::string second = std::format("\"id\": {},", e.sheetB.value());
    REQUIRE(good.find(first) != std::string::npos);
    REQUIRE(good.find(second) != std::string::npos);
    std::string broken = good;
    broken.replace(broken.find(second), second.size(), first);

    const auto loaded = io::documentFromJson(broken);
    std::string why = "it loaded";
    if (!loaded) {
        why = loaded.error().message;
    }
    INFO(why);
    CHECK_FALSE(loaded.has_value());
}

TEST_CASE("Persistence_AFailedLoadLeavesTheLiveDocumentUntouched",
          "[drawing][persist][p14][malformed]") {
    // The API shape is what guarantees this: loadDocument returns a NEW
    // document, so a caller's document cannot be half-written by a failed
    // load. Asserted rather than assumed.
    Everything e = everything();
    const std::string before = e.d.json();
    const std::size_t objects = e.d.document.objectCount();

    TempDir dir;
    const auto path = dir.path() / "broken.bcad";
    {
        std::ofstream stream{path, std::ios::binary};
        stream << "{\"document\": {\"id\": \"not-a-uuid\"}}";
    }
    const auto loaded = io::loadDocument(path);
    CHECK_FALSE(loaded.has_value());

    CHECK(e.d.json() == before);
    CHECK(e.d.document.objectCount() == objects);
    // ...and the live document still draws.
    CHECK(drawing::projectedGeometry(e.d.document, e.base, e.d.bodies(), e.d.transforms())
              .has_value());
}

TEST_CASE("Persistence_AFailedSaveDoesNotDestroyThePreviousFile",
          "[drawing][persist][p14][malformed]") {
    // A save to a path that cannot be written must fail and say so. The
    // established file is a separate, valid one and must survive.
    Everything e = everything();
    TempDir dir;
    const auto good = dir.path() / "good.bcad";
    REQUIRE(io::saveDocument(e.d.document, good).has_value());
    const auto size = std::filesystem::file_size(good);
    REQUIRE(size > 0);

    // A directory that does not exist: there is nowhere to write.
    const auto nowhere = dir.path() / "no" / "such" / "place" / "x.bcad";
    const auto saved = io::saveDocument(e.d.document, nowhere);
    CHECK_FALSE(saved.has_value());
    CHECK_FALSE(std::filesystem::exists(nowhere));

    // The earlier file is untouched and still loads.
    CHECK(std::filesystem::file_size(good) == size);
    CHECK(io::loadDocument(good).has_value());
}

TEST_CASE("Persistence_MalformedDrawingSemanticsAreRefusedNotJustMalformedSyntax",
          "[drawing][persist][p14][malformed]") {
    // The second half of malformed input: files that PARSE and then mean
    // something impossible. Syntax is the easy half.
    Everything e = everything();
    const std::string good = e.d.json();

    const auto refuse = [&](const std::string& what, const std::string& from, const std::string& to) {
        INFO(what);
        REQUIRE(good.find(from) != std::string::npos);
        std::string broken = good;
        broken.replace(broken.find(from), from.size(), to);
        const auto loaded = io::documentFromJson(broken);
        std::string why = "it loaded";
        if (!loaded) {
            why = loaded.error().message;
        }
        INFO(why);
        CHECK_FALSE(loaded.has_value());
    };

    // A scale is an exact rational pair, so a zero denominator is not a
    // rounding problem -- it is a drawing at infinite magnification. The
    // array is rewritten by locating its brackets rather than by matching
    // the file's indentation, which is the pretty-printer's business and not
    // this test's.
    const auto withScale = [&](const std::string& pair) {
        const std::size_t key = good.find("\"scale\"");
        REQUIRE(key != std::string::npos);
        const std::size_t open = good.find('[', key);
        const std::size_t close = good.find(']', open);
        REQUIRE(open != std::string::npos);
        REQUIRE(close != std::string::npos);
        std::string broken = good;
        broken.replace(open, close - open + 1, pair);
        return broken;
    };
    const auto refuseJson = [&](const std::string& what, const std::string& broken) {
        INFO(what);
        const auto loaded = io::documentFromJson(broken);
        std::string why = "it loaded";
        if (!loaded) {
            why = loaded.error().message;
        }
        INFO(why);
        CHECK_FALSE(loaded.has_value());
    };
    refuseJson("a scale with a zero denominator", withScale("[1, 0]"));
    refuseJson("a negative scale", withScale("[-1, 2]"));
    refuseJson("a scale with a zero numerator", withScale("[0, 1]"));

    // A frame's datums are an ORDERED array of single capitals (ISO 5459),
    // so the array is rewritten by its brackets, as the scale was.
    const auto withDatums = [&](const std::string& list) {
        const std::size_t key = good.find("\"datums\"");
        REQUIRE(key != std::string::npos);
        const std::size_t open = good.find('[', key);
        const std::size_t close = good.find(']', open);
        REQUIRE(open != std::string::npos);
        REQUIRE(close != std::string::npos);
        std::string broken = good;
        broken.replace(open, close - open + 1, list);
        return broken;
    };
    refuseJson("a datum letter that is not a capital", withDatums("[\"A\", \"b\", \"C\"]"));
    refuseJson("a datum letter ISO 5459 forbids", withDatums("[\"A\", \"I\", \"C\"]"));
    refuseJson("a datum that is not one letter", withDatums("[\"A\", \"BB\", \"C\"]"));
    refuseJson("a datum that is a number", withDatums("[\"A\", 2, \"C\"]"));
    refuse("an unknown geometric characteristic", "\"characteristic\": \"position\"",
           "\"characteristic\": \"vibe\"");
    refuse("an unknown tolerance zone", "\"zone\": \"cylindrical\"", "\"zone\": \"blobby\"");
}

TEST_CASE("Persistence_AViewCycleInAFileLoadsAndIsThenDiagnosedByRegeneration",
          "[drawing][persist][p14][malformed]") {
    // A DECISION RECORDED, not a defect. A view that names itself as its
    // parent is structurally well formed -- every field is the right type and
    // in range -- and it LOADS.
    //
    // It has to. Document-level validation cannot run per object during a
    // load: objects are read one at a time, so a view whose parent appears
    // LATER in the file would be refused for a forward reference that is
    // perfectly legal. createView() can check against the document because by
    // then the document exists; the reader cannot.
    //
    // So the architecture diagnoses rather than rejects, which is what the
    // brief allows -- and P14-REGEN-001 is what makes the diagnosis arrive
    // instead of being silent. This test pins BOTH halves: it loads, and it
    // is reported.
    Everything e = everything();
    const std::string good = e.d.json();
    const std::string self = std::format("\"parent\": {}", e.base.value());
    const std::string cycle = std::format("\"parent\": {}", e.projected.value());
    REQUIRE(good.find(self) != std::string::npos);

    std::string broken = good;
    broken.replace(broken.find(self), self.size(), cycle);
    auto loaded = io::documentFromJson(broken);
    REQUIRE(loaded.has_value()); // it loads, by design

    Drawing after;
    after.document = std::move(*loaded);
    const auto report = after.regenerate();
    CHECK_FALSE(report.succeeded());
    const Error* error = after.regenerator.error(ObjectId{e.projected});
    REQUIRE(error != nullptr);
    INFO(error->message);
    // And the diagnosis is the DEPENDENCY GRAPH's, not a drawing-specific
    // walk: View::dependencies() names the parent, so a view that names
    // itself is an ordinary cycle in the graph and the regenerator's existing
    // cycle detection reports it. That only reaches drawing objects because
    // P14-REGEN-001 put them in the graph properly.
    CHECK_THAT(error->message, ContainsSubstring("dependency cycle"));
    CHECK(error->code == ErrorCode::FailedPrecondition);
    // And it cannot be drawn either: the same walk refuses.
    CHECK_FALSE(drawing::projectedGeometry(after.document, e.projected, after.bodies(),
                                           after.transforms())
                    .has_value());
}

TEST_CASE("Persistence_NumbersAreWrittenTheSameWhateverTheLocale",
          "[drawing][persist][p14][determinism]") {
    // A German locale writes 1,25 for 1.25. If the serializer went through
    // the C locale, a drawing saved on one machine would not load on another.
    Everything e = everything();
    const std::string neutral = e.d.json();
    CHECK_THAT(neutral, ContainsSubstring("0.15"));

    const char* previous = std::setlocale(LC_ALL, nullptr);
    const std::string saved = previous == nullptr ? std::string{"C"} : std::string{previous};
    // Several spellings, because which one exists depends on the platform.
    const char* applied = nullptr;
    for (const char* name : {"de_DE.UTF-8", "de-DE", "German_Germany.1252", "de_DE"}) {
        applied = std::setlocale(LC_ALL, name);
        if (applied != nullptr) {
            break;
        }
    }
    if (applied == nullptr) {
        // Report it rather than passing quietly: a test that silently does
        // nothing is worse than one that is absent.
        WARN("no comma-decimal locale is installed; the locale check did not run");
        std::setlocale(LC_ALL, saved.c_str());
        return;
    }
    INFO("locale in force: " << applied);
    // The decimal separator really did change for the C library...
    char probe[32] = {};
    std::snprintf(probe, sizeof(probe), "%.2f", 1.25);
    INFO("printf writes: " << probe);

    const std::string underLocale = e.d.json();
    std::setlocale(LC_ALL, saved.c_str());

    CHECK(underLocale == neutral);
    CHECK_THAT(underLocale, !ContainsSubstring("0,15"));
    // And a file written under that locale still loads.
    auto reloaded = io::documentFromJson(underLocale);
    CHECK(reloaded.has_value());
}

TEST_CASE("Persistence_SavingAfterAModelEditLeavesNoStaleDrawingInTheFile",
          "[drawing][persist][p14][derived]") {
    // create -> save -> edit the model -> regenerate -> save again -> load.
    // The drawing must reflect the LATEST model, and the first file's numbers
    // must not survive anywhere in the second.
    Everything e = everything();
    TempDir dir;
    const auto first = dir.path() / "first.bcad";
    const auto second = dir.path() / "second.bcad";

    const auto measured = [](Drawing& d, DimensionId id) {
        auto m = drawing::measure(d.document, id, d.bodies(), d.transforms());
        REQUIRE(m.has_value());
        REQUIRE(m->length.has_value());
        return m->length->in(units::mm);
    };
    REQUIRE_THAT(measured(e.d, e.width), WithinAbs(100.0, kMm));
    REQUIRE(io::saveDocument(e.d.document, first).has_value());

    // Widen the block. The dimension names the two faces by the sketch
    // entities that sweep them, so its intent does not move.
    REQUIRE(e.d.document
                .modifyObject<sketch::Sketch>(
                    e.block.sketch,
                    [&](sketch::Sketch& sk) {
                        REQUIRE(sk.setPointPosition(e.block.corners[1], Point2D{155_mm, 0_mm})
                                    .has_value());
                        REQUIRE(sk.setPointPosition(e.block.corners[2], Point2D{155_mm, 60_mm})
                                    .has_value());
                        return true;
                    })
                .has_value());
    e.d.regenerate();
    CHECK_THAT(measured(e.d, e.width), WithinAbs(155.0, kMm));
    REQUIRE(io::saveDocument(e.d.document, second).has_value());

    auto loaded = io::loadDocument(second);
    REQUIRE(loaded.has_value());
    Drawing after;
    after.document = std::move(*loaded);
    REQUIRE(after.regenerate().succeeded());
    CHECK_THAT(measured(after, e.width), WithinAbs(155.0, kMm));

    // Neither file carries the drawn number, so neither could have gone stale.
    auto text = io::documentToJson(after.document);
    REQUIRE(text.has_value());
    CHECK_THAT(*text, !ContainsSubstring("\"100.00\""));
    CHECK_THAT(*text, !ContainsSubstring("\"155.00\""));
    // What changed between the two files is the SKETCH, which is the model.
    CHECK_THAT(*text, ContainsSubstring("0.155"));
}

// --- Backward compatibility --------------------------------------------------------------------------

TEST_CASE("Persistence_EveryCommittedModelStillLoads", "[drawing][persist][p14][legacy]") {
    // Every committed model predates this milestone. None may have stopped
    // loading, and none may have acquired drawing objects it never had.
    const std::filesystem::path models{BETTERCAD_EXAMPLE_MODELS_DIR};
    REQUIRE(std::filesystem::exists(models));
    std::size_t checked = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator{models}) {
        if (!entry.is_regular_file() || entry.path().extension() != ".bcad") {
            continue;
        }
        INFO("model " << entry.path().filename().string());
        auto loaded = io::loadDocument(entry.path());
        REQUIRE(loaded.has_value());
        CHECK(drawing::sheets(*loaded).empty());
        CHECK(drawing::views(*loaded).empty());
        CHECK(drawing::dimensions(*loaded).empty());
        CHECK(drawing::annotations(*loaded).empty());
        // And each still serializes to exactly what it was loaded from, so
        // the drawing work has not disturbed the existing format.
        auto again = io::documentToJson(*loaded);
        REQUIRE(again.has_value());
        ++checked;
    }
    CHECK(checked >= 20);
}
