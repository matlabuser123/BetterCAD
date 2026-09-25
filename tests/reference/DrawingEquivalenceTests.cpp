#include "cli/CliRunner.hpp"
#include "reference/DrawingTestSupport.hpp"

#include <bettercad/drawing/Annotations.hpp>
#include <bettercad/drawing/Dimensions.hpp>
#include <bettercad/drawing/Sheets.hpp>
#include <bettercad/drawing/Bom.hpp>
#include <bettercad/drawing/Views.hpp>
#include <bettercad/io/DocumentFile.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <string>
#include <string_view>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using namespace bettercad::test::drawref;
using bettercad::cli::ExitCode;
using Catch::Matchers::ContainsSubstring;

// P14-REFMOD-001: the CLI path and the core path, over one reference model.
//
// A production reference model that passed only through the interface it was
// built with would prove less than it looks like it does. These build the
// SAME drawing on the SAME part twice -- once through the public core API and
// once through bettercad-cli -- and require the two to agree on the canonical
// intent, on the IDs, and on the bytes of what they export.
namespace {

/// RM-DWG-01's part, with its drawing taken off again.
///
/// Built by the reference builder, so the geometry, the parameters, the
/// sketch entities and the face names are the committed model's rather than a
/// simplified stand-in. Its drawing objects are then removed through the
/// public API, in dependency order, leaving a document that is exactly the
/// part -- and leaving the ID counter where it was, so the two copies below
/// allocate the same IDs in the same order.
[[nodiscard]] Document partOfStepPlate() {
    auto model = reference::buildDrawnStepPlateReferenceModel();
    INFO(why(model));
    REQUIRE(model.has_value());
    Document document = std::move(model->document);
    for (const AnnotationId id : drawing::annotations(document)) {
        REQUIRE(drawing::removeAnnotation(document, id).has_value());
    }
    for (const DimensionId id : drawing::dimensions(document)) {
        REQUIRE(drawing::removeDimension(document, id).has_value());
    }
    // Views before sheets: a view names the sheet it sits on, and a projected
    // view names its parent, so they come off in reverse order of creation.
    std::vector<ViewId> views = drawing::views(document);
    for (auto id = views.rbegin(); id != views.rend(); ++id) {
        REQUIRE(drawing::removeView(document, *id).has_value());
    }
    for (const SheetId id : drawing::sheets(document)) {
        REQUIRE(drawing::removeSheet(document, id).has_value());
    }
    REQUIRE(drawing::sheets(document).empty());
    return document;
}

/// RM-DWG-07's assembly, with its drawing taken off again.
///
/// Seven occurrences of three parts, all grounded, and no sheet -- so the two
/// paths below start from one document and allocate the same IDs in the same
/// order.
[[nodiscard]] Document assemblyOfBoltedStack() {
    auto model = reference::buildDrawnBoltedStackReferenceModel();
    INFO(why(model));
    REQUIRE(model.has_value());
    Document document = std::move(model->document);
    for (const AnnotationId id : drawing::annotations(document)) {
        REQUIRE(drawing::removeAnnotation(document, id).has_value());
    }
    std::vector<ViewId> views = drawing::views(document);
    for (auto id = views.rbegin(); id != views.rend(); ++id) {
        REQUIRE(drawing::removeView(document, *id).has_value());
    }
    for (const SheetId id : drawing::sheets(document)) {
        REQUIRE(drawing::removeSheet(document, id).has_value());
    }
    REQUIRE(drawing::sheets(document).empty());
    return document;
}

/// The component named @p name, required to exist.
[[nodiscard]] ComponentId componentNamed(const Document& document, std::string_view name) {
    const ObjectId object = document.findByName(name).value_or(ObjectId{});
    INFO("no component named " << name);
    REQUIRE(object.isValid());
    return ComponentId::fromValue(object.value());
}

/// The drawing both paths build: a sheet, a view of the plate, the plate's
/// thickness between the two faces the extrude caps, and a note.
///
/// Deliberately small. What is under test is that two interfaces write the
/// same intent, not how much of the drawing surface either of them reaches --
/// P14-CLI-001 qualified the command set, and the eight reference models
/// exercise the rest.
constexpr double kViewXMm = 150.0;
constexpr double kViewYMm = 150.0;
constexpr double kTextXMm = 150.0;
constexpr double kTextYMm = 100.0;
constexpr double kNoteXMm = 40.0;
constexpr double kNoteYMm = 40.0;
constexpr const char* kNote = "BREAK SHARP EDGES 0.3 MAX";

} // namespace

TEST_CASE("DrawingReference_TheCliAndTheCoreApiWriteTheSameDrawing",
          "[reference][drawing][cli][equivalence]") {
    TempDir dir;

    // --- through the core API -------------------------------------------------
    Document core = partOfStepPlate();
    const ObjectId plate = core.findByName("Plate").value_or(ObjectId{});
    REQUIRE(plate.isValid());

    const SheetId sheet = require(drawing::createSheet(
        core, "Sheet1",
        drawing::SheetDefinition{.format = drawing::SheetFormat::A3, .scale = {1, 1}}));
    const ViewId view = require(drawing::createView(
        core, "Front",
        drawing::ViewDefinition{.sheet = sheet,
                                .source = ObjectReference{plate},
                                .orientation = drawing::StandardView::Front,
                                .placement = Point2D{kViewXMm * units::mm, kViewYMm * units::mm}}));
    const auto cap = [&](FaceRole role) {
        return drawing::DimensionTarget{
            .plane = PlaneReference{.object = plate, .face = FaceSelector{.role = role}}};
    };
    const DimensionId thickness = require(drawing::createDimension(
        core, "Thickness",
        drawing::DimensionDefinition{.view = view,
                                     .from = cap(FaceRole::StartCap),
                                     .to = cap(FaceRole::EndCap),
                                     .format = {.decimals = 2},
                                     .placement = Point2D{kTextXMm * units::mm,
                                                          kTextYMm * units::mm}}));
    const AnnotationId note = require(drawing::createAnnotation(
        core, "GeneralNote",
        drawing::AnnotationDefinition{.view = view,
                                      .type = drawing::AnnotationType::Note,
                                      .text = kNote,
                                      .placement = Point2D{kNoteXMm * units::mm,
                                                           kNoteYMm * units::mm}}));

    // --- through bettercad-cli ------------------------------------------------
    const auto path = dir.path() / "step_plate_part.bcad";
    {
        Document stripped = partOfStepPlate();
        REQUIRE(io::saveDocument(stripped, path).has_value());
    }
    const std::string file = cliPath(path);
    const auto ok = [&](const std::vector<std::string>& args) {
        const auto run = runCliCommand(args);
        INFO(run.err);
        REQUIRE(run.exitCode == ExitCode::Success);
    };
    ok({"sheet-add", file, "--name", "Sheet1", "--format", "A3", "--scale", "1:1"});
    ok({"view-add", file, "--name", "Front", "--sheet", "Sheet1", "--source", "Plate", "--x",
        "150mm", "--y", "150mm"});
    ok({"dimension-add", file, "--name", "Thickness", "--view", "Front", "--type", "linear",
        "--from", "face:Plate:start_cap", "--to", "face:Plate:end_cap", "--decimals", "2", "--x",
        "150mm", "--y", "100mm"});
    ok({"annotation-add", file, "--name", "GeneralNote", "--view", "Front", "--type", "note",
        "--text", kNote, "--x", "40mm", "--y", "40mm"});

    auto loaded = io::loadDocument(path);
    INFO(why(loaded));
    REQUIRE(loaded.has_value());
    Document fromCli = std::move(*loaded);

    // --- the same intent ------------------------------------------------------
    //
    // ADR-011 keeps every derived thing out of the file, so this JSON IS the
    // canonical state: the objects, their IDs, their references, the face
    // roles, the formats and the placements. Comparing it compares everything
    // either interface was supposed to write.
    auto coreJson = io::documentToJson(core);
    auto cliJson = io::documentToJson(fromCli);
    INFO(why(coreJson) << why(cliJson));
    REQUIRE(coreJson.has_value());
    REQUIRE(cliJson.has_value());
    CHECK(*coreJson == *cliJson);

    // The IDs in particular, named rather than left to the comparison above:
    // an equivalence that held only because both paths happened to produce
    // the same text would not be one a reference could rely on.
    CHECK(drawing::sheets(fromCli) == std::vector<SheetId>{sheet});
    CHECK(drawing::views(fromCli) == std::vector<ViewId>{view});
    CHECK(drawing::dimensions(fromCli) == std::vector<DimensionId>{thickness});
    CHECK(drawing::annotations(fromCli) == std::vector<AnnotationId>{note});

    // --- and the same drawing -------------------------------------------------
    //
    // Identical intent regenerated twice must draw one picture and write one
    // set of bytes. This is the half that would catch an equivalence that was
    // true of the file and false of the output.
    Drawn a{std::move(core)};
    Drawn b{std::move(fromCli)};
    INFO(a.why() << b.why());
    REQUIRE(a.succeeded());
    REQUIRE(b.succeeded());
    const drawing::DrawingScene sceneA = sceneOf(a, sheet);
    const drawing::DrawingScene sceneB = sceneOf(b, sheet);
    CHECK(sameScene(sceneA, sceneB));
    CHECK(*io::svgDocument(sceneA) == *io::svgDocument(sceneB));
    CHECK(*io::dxfDocument(sceneA) == *io::dxfDocument(sceneB));
    CHECK(*io::pdfDocument(sceneA) == *io::pdfDocument(sceneB));

    // The dimension reads the plate's thickness by both routes, which is the
    // one number on this drawing that is derived from geometry.
    CHECK(measuredMm(a, thickness) == measuredMm(b, thickness));
    CHECK(dimensionText(a, thickness) == "20.00");
}

TEST_CASE("DrawingReference_TheCliReportsTheSameReferenceModelTheCoreBuilt",
          "[reference][drawing][cli][equivalence]") {
    // The other direction: a model built through the CORE, saved, and read
    // back by the CLI. What the CLI says about it has to be what the core
    // measures -- the CLI computes its numbers on demand from the model and
    // stores none of them, so a disagreement would mean one of the two had a
    // second answer.
    TempDir dir;
    auto model = reference::buildDrawnHolePlateReferenceModel();
    INFO(why(model));
    REQUIRE(model.has_value());
    const DimensionId length = model->length;
    const DimensionId boreDiameter = model->boreDiameter;
    const AnnotationId callout = model->callout;

    const auto path = dir.path() / "hole_plate.bcad";
    REQUIRE(io::saveDocument(model->document, path).has_value());

    Drawn core{std::move(model->document)};
    INFO(core.why());
    REQUIRE(core.succeeded());

    const auto report = runCliCommand({"drawing", cliPath(path)});
    INFO(report.err);
    REQUIRE(report.exitCode == ExitCode::Success);

    // Each number the core measures must appear in the CLI's report, written
    // the way the dimension's own format says.
    CHECK_THAT(report.out, ContainsSubstring(dimensionText(core, length)));
    CHECK_THAT(report.out, ContainsSubstring(dimensionText(core, boreDiameter)));
    CHECK_THAT(report.out, ContainsSubstring("120.00"));
    CHECK_THAT(report.out, ContainsSubstring("10.00"));
    // Every reference resolved, by both accounts.
    CHECK_THAT(report.out, !ContainsSubstring("unresolved"));
    CHECK_THAT(report.out, ContainsSubstring("Regeneration: ok"));
    CHECK(resolutionOf(core, callout).resolved());
}

TEST_CASE("DrawingReference_TheCliBuildsAnAssemblyDrawingWithItsBomAndBalloons",
          "[reference][drawing][cli][equivalence][bom]") {
    // THE FULL HEADLESS WORKFLOW, on the assembly reference model: load a
    // document, add a sheet, create an assembly view, put a bill-of-materials
    // table on it, balloon two occurrences of the SAME part, save, reload,
    // regenerate and export -- all through published commands, and then
    // compared with the identical drawing built through the core API.
    //
    // The balloons are the part that matters. `--target object:Bolt1` names an
    // OCCURRENCE, and two balloons on two instances of one bolt must come out
    // with the same item number and different arrows. A CLI that resolved the
    // target to the PART would write two annotations that looked right and
    // pointed at the same place.
    TempDir dir;

    // --- through the core API -------------------------------------------------
    Document core = assemblyOfBoltedStack();
    const ComponentId firstBolt = componentNamed(core, "Bolt1");
    const ComponentId lastBolt = componentNamed(core, "Bolt4");

    const SheetId sheet = require(drawing::createSheet(
        core, "Sheet1",
        drawing::SheetDefinition{.format = drawing::SheetFormat::A3, .scale = {1, 1}}));
    const ViewId view = require(drawing::createView(
        core, "Front",
        drawing::ViewDefinition{.sheet = sheet,
                                .subject = drawing::ViewSubject::Assembly,
                                .orientation = drawing::StandardView::Front,
                                .placement = Point2D{120_mm, 150_mm}}));
    const AnnotationId table = require(drawing::createAnnotation(
        core, "BomTable",
        drawing::AnnotationDefinition{.view = view,
                                      .type = drawing::AnnotationType::BomTable,
                                      .placement = Point2D{280_mm, 240_mm}}));
    const auto coreBalloon = [&](const std::string& name, ComponentId occurrence, double x,
                                 double y) {
        return require(drawing::createAnnotation(
            core, name,
            drawing::AnnotationDefinition{
                .view = view,
                .type = drawing::AnnotationType::Balloon,
                .target = drawing::AnnotationTarget{
                    .object = ObjectId::fromValue(occurrence.value())},
                .placement = Point2D{x * units::mm, y * units::mm}}));
    };
    const AnnotationId balloonA = coreBalloon("BoltBalloonA", firstBolt, 60.0, 200.0);
    const AnnotationId balloonB = coreBalloon("BoltBalloonB", lastBolt, 190.0, 200.0);

    // --- through bettercad-cli ------------------------------------------------
    const auto path = dir.path() / "bolted_stack.bcad";
    {
        Document stripped = assemblyOfBoltedStack();
        REQUIRE(io::saveDocument(stripped, path).has_value());
    }
    const std::string file = cliPath(path);
    const auto ok = [&](const std::vector<std::string>& args) {
        const auto run = runCliCommand(args);
        INFO(run.err);
        REQUIRE(run.exitCode == ExitCode::Success);
    };
    ok({"sheet-add", file, "--name", "Sheet1", "--format", "A3", "--scale", "1:1"});
    ok({"view-add", file, "--name", "Front", "--sheet", "Sheet1", "--assembly", "--x", "120mm",
        "--y", "150mm"});
    ok({"annotation-add", file, "--name", "BomTable", "--view", "Front", "--type", "bom_table",
        "--x", "280mm", "--y", "240mm"});
    ok({"annotation-add", file, "--name", "BoltBalloonA", "--view", "Front", "--type", "balloon",
        "--target", "object:Bolt1", "--x", "60mm", "--y", "200mm"});
    ok({"annotation-add", file, "--name", "BoltBalloonB", "--view", "Front", "--type", "balloon",
        "--target", "object:Bolt4", "--x", "190mm", "--y", "200mm"});

    // Reload: a separate parse of the file the edits wrote.
    auto loaded = io::loadDocument(path);
    INFO(why(loaded));
    REQUIRE(loaded.has_value());
    Document fromCli = std::move(*loaded);

    // --- the same intent, the same IDs ----------------------------------------
    auto coreJson = io::documentToJson(core);
    auto cliJson = io::documentToJson(fromCli);
    INFO(why(coreJson) << why(cliJson));
    REQUIRE(coreJson.has_value());
    REQUIRE(cliJson.has_value());
    CHECK(*coreJson == *cliJson);
    CHECK(drawing::annotations(fromCli) ==
          std::vector<AnnotationId>{table, balloonA, balloonB});

    // --- the same bill, the same balloons -------------------------------------
    Drawn a{std::move(core)};
    Drawn b{std::move(fromCli)};
    INFO(a.why() << b.why());
    REQUIRE(a.succeeded());
    REQUIRE(b.succeeded());

    const drawing::BillOfMaterials bom = bomOf(b, view);
    REQUIRE(bom.rows.size() == 3);
    CHECK(bom.totalOccurrences() == reference::DrawnBoltedStackModel::kOccurrences);
    CHECK(bom == bomOf(a, view));

    // Two balloons, one number, two places -- by the CLI's route as by the
    // core's.
    CHECK(annotationTextOf(b, balloonA) == annotationTextOf(a, balloonA));
    CHECK(annotationTextOf(b, balloonA) == annotationTextOf(b, balloonB));
    const auto tipOf = [](const Drawn& model, AnnotationId id) {
        auto items = drawing::draw(model.document(), id, model.bodies(), model.transforms());
        INFO(why(items));
        REQUIRE(items.has_value());
        REQUIRE_FALSE(items->lines.empty());
        REQUIRE_FALSE(items->lines.front().points.empty());
        return items->lines.front().points.back();
    };
    const Point2D tipA = tipOf(b, balloonA);
    const Point2D tipB = tipOf(b, balloonB);
    CHECK(std::hypot((tipA.x - tipB.x).si(), (tipA.y - tipB.y).si()) > 0.01);
    CHECK(tipA == tipOf(a, balloonA));

    // --- and the same drawing, to the byte -------------------------------------
    const drawing::DrawingScene sceneA = sceneOf(a, sheet);
    const drawing::DrawingScene sceneB = sceneOf(b, sheet);
    CHECK(sameScene(sceneA, sceneB));
    CHECK(*io::svgDocument(sceneA) == *io::svgDocument(sceneB));
    CHECK(*io::dxfDocument(sceneA) == *io::dxfDocument(sceneB));
    CHECK(*io::pdfDocument(sceneA) == *io::pdfDocument(sceneB));
}
