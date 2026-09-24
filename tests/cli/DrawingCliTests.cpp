#include "cli/CliRunner.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Configurations.hpp>
#include <bettercad/assembly/Resolution.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/drawing/Annotations.hpp>
#include <bettercad/drawing/Bom.hpp>
#include <bettercad/drawing/Commands.hpp>
#include <bettercad/drawing/Dimensions.hpp>
#include <bettercad/drawing/Regeneration.hpp>
#include <bettercad/drawing/Sheets.hpp>
#include <bettercad/drawing/Views.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using bettercad::cli::ExitCode;
using bettercad::test::cliPath;
using bettercad::test::require;
using bettercad::test::runCliCommand;
using bettercad::test::TempDir;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

// P14-CLI-001: a drawing made, edited and reported without a window.
//
// THE ONE RULE. The CLI is an ADAPTER over the qualified core, never a second
// CAD engine. So the test that matters most here is not "did the command
// work" but **CLI/CORE EQUIVALENCE**: the same edit, made through the command
// line and through the P14-CMD-001 command object, has to leave the same
// canonical document. Anything the CLI decided for itself would show up as a
// difference in those bytes.
//
// THREE FAILURES THIS FILE IS WATCHING FOR, because they are what "the
// command returned 0" hides:
//
//   THE SCRIPT THAT HALF SUCCEEDS.  Every atomicity case compares the FILE'S
//   BYTES before and after, never the exit code. A command can report failure
//   and still have written something.
//
//   THE STALE NUMBER.  A dimension's value is derived. Every check of one is
//   made after the model has MOVED, so a CLI that had cached or copied it
//   would print the old number and be caught.
//
//   THE QUANTITY THE CLI WORKED OUT.  A BOM row's quantity comes from the
//   assembly. The BOM cases change the assembly and re-ask, so a count the
//   CLI had done itself could not keep up.
namespace {

constexpr double kMm = 1e-9;

/// Reads a file's bytes, for the before/after comparisons that prove a failed
/// command wrote nothing.
std::string bytesOf(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    REQUIRE(stream.good());
    return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

/// The canonical state of the document in @p path, which is what CLI/core
/// equivalence is compared on.
std::string canonicalOf(const std::filesystem::path& path) {
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    auto json = io::documentToJson(*loaded);
    REQUIRE(json.has_value());
    return *json;
}

struct Part {
    ObjectId sketch{};
    ObjectId pad{};
    std::array<EntityId, 4> lines{};
    std::array<EntityId, 4> corners{};
};

/// A 100 x 60 x 40 block, saved to @p path. The four profile entities are
/// kept so a side face can be named the way the drawing vocabulary names one.
Part writePart(const std::filesystem::path& path, const std::string& name = "Plate") {
    Document document{name};
    Part part;
    auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
    const std::array<EntityId, 4> corners{
        require(sketch->addPoint(Point2D{0_mm, 0_mm})),
        require(sketch->addPoint(Point2D{100_mm, 0_mm})),
        require(sketch->addPoint(Point2D{100_mm, 60_mm})),
        require(sketch->addPoint(Point2D{0_mm, 60_mm})),
    };
    for (std::size_t i = 0; i < 4; ++i) {
        part.lines[i] = require(sketch->addLine(corners[i], corners[(i + 1) % 4]));
    }
    part.corners = corners;
    part.sketch = require(document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Block", {.profile = SketchId::fromValue(part.sketch.value()), .depth = 40_mm});
    REQUIRE(extrude.has_value());
    part.pad = require(document.addObject(std::move(*extrude)));
    REQUIRE(io::saveDocument(document, path).has_value());
    return part;
}

/// Runs a CLI command and requires it to succeed, reporting its own
/// diagnostics when it does not.
void cliOk(const std::vector<std::string>& args) {
    const auto run = runCliCommand(args);
    INFO(run.err);
    INFO(run.out);
    REQUIRE(run.exitCode == ExitCode::Success);
}

} // namespace

// --- The command surface ---------------------------------------------------------------------------

TEST_CASE("DrawingCli_EveryDrawingVerbIsReachableAndReportsWhatItMade", "[cli][drawing][p14]") {
    TempDir dir;
    const auto path = dir.path() / "part.bcad";
    writePart(path);
    const std::string file = cliPath(path);

    const auto sheet = runCliCommand({"sheet-add", file, "--format", "A3", "--scale", "1:1"});
    INFO(sheet.err);
    REQUIRE(sheet.exitCode == ExitCode::Success);
    CHECK_THAT(sheet.out, ContainsSubstring("Created Sheet1"));
    CHECK_THAT(sheet.out, ContainsSubstring("A3 landscape, scale 1:1"));

    cliOk({"view-add", file, "--sheet", "Sheet1", "--source", "Block", "--x", "150mm", "--y", "150mm"});
    cliOk({"view-add", file, "--sheet", "Sheet1", "--parent", "View1", "--direction", "top",
           "--spacing", "90mm", "--name", "TopView"});
    cliOk({"view-move", file, "View1", "--x", "160mm", "--y", "140mm"});
    cliOk({"view-set", file, "View1", "--scale", "1:2"});
    cliOk({"dimension-add", file, "--view", "View1", "--type", "linear", "--from",
           "face:Block:start_cap", "--to", "face:Block:end_cap", "--decimals", "2"});
    cliOk({"dimension-set", file, "Dimension1", "--decimals", "3"});
    cliOk({"annotation-add", file, "--view", "View1", "--type", "note", "--text", "BREAK EDGES",
           "--x", "40mm", "--y", "40mm"});
    cliOk({"annotation-set", file, "Annotation1", "--text", "BREAK SHARP EDGES"});

    // Everything is in the file, under the names the CLI reported.
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    CHECK(drawing::sheets(*loaded).size() == 1);
    CHECK(drawing::views(*loaded).size() == 2);
    CHECK(drawing::dimensions(*loaded).size() == 1);
    CHECK(drawing::annotations(*loaded).size() == 1);

    // ...and the removals take them away again.
    cliOk({"annotation-remove", file, "Annotation1"});
    cliOk({"dimension-remove", file, "Dimension1"});
    cliOk({"view-remove", file, "TopView"});
    cliOk({"view-remove", file, "View1"});
    cliOk({"sheet-remove", file, "Sheet1"});
    auto emptied = io::loadDocument(path);
    REQUIRE(emptied.has_value());
    CHECK(drawing::sheets(*emptied).empty());
    CHECK(drawing::views(*emptied).empty());
    CHECK(drawing::dimensions(*emptied).empty());
    CHECK(drawing::annotations(*emptied).empty());
}

// --- CLI / core equivalence ---------------------------------------------------------------------------

TEST_CASE("DrawingCli_EveryEditLeavesTheSameCanonicalStateAsTheCoreCommand",
          "[cli][drawing][p14][equivalence]") {
    // THE milestone's central claim, one row of the matrix at a time. Two
    // copies of one baseline: one edited through the command line, one
    // through the P14-CMD-001 command object directly. The canonical states
    // must be the same bytes -- so anything the CLI decided for itself, a
    // default it invented or a field it filled in differently, fails here.
    TempDir dir;
    const auto viaCli = dir.path() / "cli.bcad";
    const auto viaCore = dir.path() / "core.bcad";
    const Part part = writePart(viaCli);
    std::filesystem::copy_file(viaCli, viaCore);
    const std::string file = cliPath(viaCli);
    REQUIRE(canonicalOf(viaCli) == canonicalOf(viaCore));

    // --- through the CLI
    cliOk({"sheet-add", file, "--format", "A3", "--scale", "1:1"});
    cliOk({"view-add", file, "--sheet", "Sheet1", "--source", "Block", "--x", "150mm", "--y", "150mm"});
    cliOk({"view-move", file, "View1", "--x", "120mm", "--y", "90mm"});
    cliOk({"dimension-add", file, "--view", "View1", "--type", "linear", "--from",
           "face:Block:start_cap", "--to", "face:Block:end_cap", "--decimals", "2"});
    cliOk({"annotation-add", file, "--view", "View1", "--type", "note", "--text", "NOTE", "--x",
           "40mm", "--y", "40mm"});

    // --- through the core commands, in the same order
    {
        auto loaded = io::loadDocument(viaCore);
        REQUIRE(loaded.has_value());
        Document document = std::move(*loaded);

        drawing::CreateSheetCommand sheet{
            "Sheet1", drawing::SheetDefinition{.format = drawing::SheetFormat::A3,
                                               .orientation = drawing::SheetOrientation::Landscape,
                                               .scale = drawing::DrawingScale{1, 1}}};
        REQUIRE(sheet.execute(document).has_value());

        drawing::CreateViewCommand view{
            "View1", drawing::ViewDefinition{.sheet = sheet.sheetId(),
                                             .source = ObjectReference{part.pad},
                                             .orientation = drawing::StandardView::Front,
                                             .placement = Point2D{150_mm, 150_mm}}};
        REQUIRE(view.execute(document).has_value());

        drawing::MoveViewCommand move{view.viewId(), Point2D{120_mm, 90_mm}};
        REQUIRE(move.execute(document).has_value());

        const auto face = [&](FaceRole role) {
            return drawing::DimensionTarget{
                .plane = PlaneReference{.object = part.pad, .face = FaceSelector{.role = role}}};
        };
        drawing::CreateDimensionCommand dimension{
            "Dimension1",
            drawing::DimensionDefinition{.view = view.viewId(),
                                         .type = drawing::DimensionType::Linear,
                                         .from = face(FaceRole::StartCap),
                                         .to = face(FaceRole::EndCap),
                                         .format = drawing::DimensionFormat{.decimals = 2}}};
        REQUIRE(dimension.execute(document).has_value());

        drawing::CreateAnnotationCommand note{
            "Annotation1", drawing::AnnotationDefinition{.view = view.viewId(),
                                                         .type = drawing::AnnotationType::Note,
                                                         .text = "NOTE",
                                                         .placement = Point2D{40_mm, 40_mm}}};
        REQUIRE(note.execute(document).has_value());
        REQUIRE(io::saveDocument(document, viaCore).has_value());
    }

    CHECK(canonicalOf(viaCli) == canonicalOf(viaCore));

    // And the DERIVED results agree too, which is the half that canonical
    // equality does not by itself prove.
    const auto measured = [](const std::filesystem::path& path) {
        auto loaded = io::loadDocument(path);
        REQUIRE(loaded.has_value());
        Document document = std::move(*loaded);
        features::Regenerator regenerator;
        drawing::registerHandlers(regenerator);
        REQUIRE(regenerator.regenerateAll(document).has_value());
        const auto ids = drawing::dimensions(document);
        REQUIRE(ids.size() == 1);
        auto value = drawing::measure(document, ids.front(),
                                      [&](ObjectId o) { return regenerator.body(o); });
        REQUIRE(value.has_value());
        return value->text;
    };
    CHECK(measured(viaCli) == measured(viaCore));
    CHECK(measured(viaCli) == "40.00");
}

// --- Exit codes and diagnostics ------------------------------------------------------------------------

TEST_CASE("DrawingCli_TheCommandLineBeingWrongIsExitTwoAndTheEditBeingRefusedIsExitOne",
          "[cli][drawing][p14][exit]") {
    // The distinction the CLI makes everywhere else, kept for the drawing
    // verbs: 2 means "I could not read what you wrote", 1 means "I read it and
    // the model said no". Both leave the file alone.
    TempDir dir;
    const auto path = dir.path() / "part.bcad";
    writePart(path);
    const std::string file = cliPath(path);
    cliOk({"sheet-add", file, "--format", "A3"});
    cliOk({"view-add", file, "--sheet", "Sheet1", "--source", "Block"});

    struct Case {
        std::string what;
        std::vector<std::string> args;
        ExitCode expected;
        std::string says;
    };
    const std::vector<Case> cases{
        {"an unknown option", {"sheet-add", file, "--frobnicate", "3"}, ExitCode::UsageError, "frobnicate"},
        {"an option with no value", {"sheet-add", file, "--format"}, ExitCode::UsageError, "--format"},
        {"an unknown sheet format", {"sheet-add", file, "--format", "A97"}, ExitCode::UsageError, "sheet format"},
        {"an unknown orientation", {"sheet-add", file, "--orientation", "sideways"}, ExitCode::UsageError, "orientation"},
        {"a scale that is not a pair", {"sheet-add", file, "--scale", "half"}, ExitCode::UsageError, "paper:model"},
        {"a scale with a zero denominator", {"sheet-add", file, "--scale", "1:0"}, ExitCode::UsageError, "scale"},
        {"a negative scale", {"sheet-add", file, "--scale", "-1:2"}, ExitCode::UsageError, "positive whole number"},
        {"a length with no unit meaning", {"view-move", file, "View1", "--x", "wide", "--y", "1mm"}, ExitCode::UsageError, "wide"},
        {"half a placement", {"view-move", file, "View1", "--x", "10mm"}, ExitCode::UsageError, "go together"},
        {"--view missing", {"dimension-add", file, "--type", "linear", "--from", "face:Block:start_cap"}, ExitCode::UsageError, "--view is required"},
        {"--type missing", {"dimension-add", file, "--view", "View1", "--from", "face:Block:start_cap"}, ExitCode::UsageError, "--type is required"},
        {"an unknown dimension type", {"dimension-add", file, "--view", "View1", "--type", "wiggly", "--from", "face:Block:start_cap"}, ExitCode::UsageError, "dimension type"},
        {"a target in no grammar", {"dimension-add", file, "--view", "View1", "--type", "linear", "--from", "somewhere"}, ExitCode::UsageError, "geometry kind"},
        {"a face role that is not one", {"dimension-add", file, "--view", "View1", "--type", "linear", "--from", "face:Block:sideish"}, ExitCode::UsageError, "face role"},
        {"an object target on a dimension", {"dimension-add", file, "--view", "View1", "--type", "linear", "--from", "object:Block"}, ExitCode::UsageError, "not an object"},
        {"decimals out of range", {"dimension-add", file, "--view", "View1", "--type", "linear", "--from", "face:Block:start_cap", "--decimals", "9"}, ExitCode::UsageError, "0 to 6"},
        {"a sheet that is not there", {"sheet-remove", file, "Sheet404"}, ExitCode::Failure, "Sheet404"},
        {"a view that is not there", {"view-move", file, "View404", "--x", "1mm", "--y", "1mm"}, ExitCode::Failure, "View404"},
        {"a selector naming the wrong kind", {"view-move", file, "Sheet1", "--x", "1mm", "--y", "1mm"}, ExitCode::Failure, "expected a view"},
        {"a view whose sheet is not a sheet", {"view-add", file, "--sheet", "Block", "--source", "Block"}, ExitCode::Failure, "expected a sheet"},
        {"an annotation type that is not one", {"annotation-add", file, "--view", "View1", "--type", "doodle"}, ExitCode::UsageError, "annotation type"},
    };

    const std::string before = bytesOf(path);
    for (const Case& test : cases) {
        INFO(test.what);
        const auto run = runCliCommand(test.args);
        CHECK(run.exitCode == test.expected);
        CHECK_THAT(run.err, ContainsSubstring(test.says));
        CHECK(run.out.empty());
        // Nothing was written: the file is byte for byte what it was.
        CHECK(bytesOf(path) == before);
    }
}

TEST_CASE("DrawingCli_ARefusedEditWritesNothingEvenWhenItFailsLate",
          "[cli][drawing][p14][atomic]") {
    // A command that gets all the way to the model before being refused --
    // the sheet exists, the selector resolves, and the EDIT is rejected -- must
    // still leave the file untouched. The spine gives that (load, apply, save
    // only on success), and this is the assertion that it does.
    TempDir dir;
    const auto path = dir.path() / "part.bcad";
    writePart(path);
    const std::string file = cliPath(path);
    cliOk({"sheet-add", file, "--format", "A3"});
    cliOk({"view-add", file, "--sheet", "Sheet1", "--source", "Block"});
    cliOk({"view-add", file, "--sheet", "Sheet1", "--parent", "View1", "--direction", "top",
           "--spacing", "90mm", "--name", "TopView"});
    const std::string before = bytesOf(path);

    // Refused by the model: a view with a child projected from it cannot go.
    const auto removal = runCliCommand({"view-remove", file, "View1"});
    CHECK(removal.exitCode == ExitCode::Failure);
    CHECK_THAT(removal.err, ContainsSubstring("projected from it"));
    CHECK(bytesOf(path) == before);

    // Refused by the validator: a projected view stores no placement.
    const auto move = runCliCommand({"view-move", file, "TopView", "--x", "10mm", "--y", "10mm"});
    CHECK(move.exitCode == ExitCode::Failure);
    CHECK_THAT(move.err, ContainsSubstring("derived from its parent"));
    CHECK(bytesOf(path) == before);
}

// --- Batch ----------------------------------------------------------------------------------------------

TEST_CASE("DrawingCli_ABatchThatFailsPartWayWritesNothingAndSaysWhichLine",
          "[cli][drawing][p14][atomic]") {
    // The failure the batch driver exists to prevent: a script of twelve edits
    // that applies the first seven and leaves a document that is neither what
    // the script describes nor what it started from.
    TempDir dir;
    const auto path = dir.path() / "part.bcad";
    writePart(path);
    const auto script = dir.path() / "drawing.txt";
    {
        std::ofstream out{script};
        out << "# a drawing, and one line that cannot work\n";
        out << "sheet-add --format A3 --scale 1:1\n";
        out << "view-add --sheet Sheet1 --source Block --x 150mm --y 150mm\n";
        out << "dimension-add --view View1 --type linear --from face:Block:start_cap --to face:Block:end_cap\n";
        out << "annotation-add --view View404 --type note --text NO\n";
        out << "annotation-add --view View1 --type note --text NEVER REACHED\n";
    }
    const std::string before = bytesOf(path);

    const auto run = runCliCommand({"batch", cliPath(path), cliPath(script)});
    CHECK(run.exitCode != ExitCode::Success);
    CHECK_THAT(run.err, ContainsSubstring("View404"));
    // The line is named, so the fix is obvious and the rerun is the recovery.
    CHECK_THAT(run.err, ContainsSubstring("5"));
    // NOTHING was written -- not the three edits that would have worked.
    CHECK(bytesOf(path) == before);
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    CHECK(drawing::sheets(*loaded).empty());
    CHECK(drawing::views(*loaded).empty());
}

TEST_CASE("DrawingCli_AWholeDrawingAppliesAsOneBatchAndTheReportAgrees",
          "[cli][drawing][p14][e2e]") {
    // The end-to-end workflow, through public CLI commands only: a script
    // builds a drawing in one transaction, and a SEPARATE run reports it. No
    // state passes between them but the file.
    TempDir dir;
    const auto path = dir.path() / "part.bcad";
    writePart(path);
    const auto script = dir.path() / "drawing.txt";
    {
        std::ofstream out{script};
        out << "sheet-add --name Sheet1 --format A3 --scale 1:1\n";
        out << "view-add --name Front --sheet Sheet1 --source Block --x 150mm --y 150mm\n";
        out << "view-add --name Top --sheet Sheet1 --parent Front --direction top --spacing 90mm\n";
        out << "dimension-add --name Thickness --view Front --type linear "
               "--from face:Block:start_cap --to face:Block:end_cap --decimals 2\n";
        out << "annotation-add --name Note --view Front --type note --text FINISH_ALL_OVER "
               "--x 40mm --y 40mm\n";
    }

    const auto applied = runCliCommand({"batch", cliPath(path), cliPath(script)});
    INFO(applied.err);
    CHECK(applied.exitCode == ExitCode::Success);

    const auto report = runCliCommand({"drawing", cliPath(path)});
    INFO(report.err);
    CHECK(report.exitCode == ExitCode::Success);
    CHECK_THAT(report.out, ContainsSubstring("Sheets (1):"));
    CHECK_THAT(report.out, ContainsSubstring("Views (2):"));
    CHECK_THAT(report.out, ContainsSubstring("Dimensions (1):"));
    CHECK_THAT(report.out, ContainsSubstring("Annotations (1):"));
    // The block is 40 mm thick, so the dimension reads 40.00 -- a number the
    // fixture fixes and the CLI never computes.
    CHECK_THAT(report.out, ContainsSubstring("40.00"));
    CHECK_THAT(report.out, ContainsSubstring("Regeneration: ok"));
    CHECK_THAT(report.out, ContainsSubstring("resolved"));
}

TEST_CASE("DrawingCli_TheReportedDimensionFollowsTheModelAndIsNeverACachedNumber",
          "[cli][drawing][p14][derived]") {
    // The stale-number test. The drawing is made, reported, the MODEL is
    // changed underneath it, and it is reported again. A CLI that had stored
    // what it printed would print it twice.
    TempDir dir;
    const auto path = dir.path() / "part.bcad";
    const Part part = writePart(path);
    const std::string file = cliPath(path);
    cliOk({"sheet-add", file, "--format", "A3"});
    cliOk({"view-add", file, "--sheet", "Sheet1", "--source", "Block"});
    cliOk({"dimension-add", file, "--view", "View1", "--type", "linear", "--from",
           "face:Block:start_cap", "--to", "face:Block:end_cap", "--decimals", "2"});

    const auto first = runCliCommand({"drawing", file});
    CHECK(first.exitCode == ExitCode::Success);
    CHECK_THAT(first.out, ContainsSubstring("40.00"));

    // Make the block 55 mm thick, through the core, and save.
    {
        auto loaded = io::loadDocument(path);
        REQUIRE(loaded.has_value());
        Document document = std::move(*loaded);
        REQUIRE(document
                    .modifyObject<features::ExtrudeFeature>(
                        part.pad,
                        [](features::ExtrudeFeature& feature) {
                            features::ExtrudeDefinition definition = feature.definition();
                            definition.depth = 55_mm;
                            return feature.setDefinition(definition);
                        })
                    .has_value());
        REQUIRE(io::saveDocument(document, path).has_value());
    }

    const auto second = runCliCommand({"drawing", file});
    CHECK(second.exitCode == ExitCode::Success);
    CHECK_THAT(second.out, ContainsSubstring("55.00"));
    CHECK_THAT(second.out, !ContainsSubstring("40.00"));
}

// --- Assemblies: BOM and balloons -----------------------------------------------------------------------

namespace {

/// A|B|C at 3|2|1, saved to @p path: the assembly the brief asks the BOM to
/// report.
void writeAssembly(const std::filesystem::path& path) {
    Document document{"Machine"};
    const auto addPart = [&](const std::string& name, Length size) {
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
        const ObjectId sketchId = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            name, {.profile = SketchId::fromValue(sketchId.value()), .depth = size});
        REQUIRE(extrude.has_value());
        return require(document.addObject(std::move(*extrude)));
    };
    const ObjectId a = addPart("PartA", 20_mm);
    const ObjectId b = addPart("PartB", 30_mm);
    const ObjectId c = addPart("PartC", 40_mm);
    int at = 0;
    const auto place = [&](const std::string& name, ObjectId part) {
        at += 60;
        (void)require(assembly::createComponent(
            document, name,
            {.part = part,
             .placement = ComponentPlacement{.translation = {Length::fromSi(at * 1e-3), 0_mm, 0_mm}}}));
    };
    place("A1", a);
    place("A2", a);
    place("A3", a);
    place("B1", b);
    place("B2", b);
    place("C1", c);
    REQUIRE(io::saveDocument(document, path).has_value());
}

} // namespace

TEST_CASE("DrawingCli_TheBillOfMaterialsReportsWhatTheAssemblyHasAndNotWhatTheCliCounted",
          "[cli][drawing][p14][bom]") {
    // A x3, B x2, C x1 -- and then the assembly changes and the same command
    // has to say something different. The quantities come from
    // drawing::billOfMaterials(); the CLI counts nothing.
    TempDir dir;
    const auto path = dir.path() / "machine.bcad";
    writeAssembly(path);
    const std::string file = cliPath(path);
    cliOk({"sheet-add", file, "--format", "A3"});
    cliOk({"view-add", file, "--sheet", "Sheet1", "--assembly", "--name", "Assembly"});
    cliOk({"annotation-add", file, "--view", "Assembly", "--type", "bom_table", "--x", "300mm",
           "--y", "250mm"});

    const auto report = runCliCommand({"drawing", file});
    INFO(report.out);
    INFO(report.err);
    CHECK(report.exitCode == ExitCode::Success);
    CHECK_THAT(report.out, ContainsSubstring("Bill of materials"));
    CHECK_THAT(report.out, ContainsSubstring("6 occurrences"));
    CHECK_THAT(report.out, ContainsSubstring("PartA"));
    CHECK_THAT(report.out, ContainsSubstring("x3"));
    CHECK_THAT(report.out, ContainsSubstring("x2"));
    CHECK_THAT(report.out, ContainsSubstring("x1"));

    // Remove one A. The row must follow the assembly.
    cliOk({"component-remove", file, "A3"});
    const auto after = runCliCommand({"drawing", file});
    INFO(after.out);
    CHECK(after.exitCode == ExitCode::Success);
    CHECK_THAT(after.out, ContainsSubstring("5 occurrences"));
    CHECK_THAT(after.out, ContainsSubstring("x2"));
    CHECK_THAT(after.out, !ContainsSubstring("x3"));
}

TEST_CASE("DrawingCli_ABalloonNamesAnOccurrenceAndItsNumberFollowsTheAssembly",
          "[cli][drawing][p14][bom]") {
    // ADR-022: a balloon's canonical target is the occurrence, never the item
    // number it draws. So the CLI takes an occurrence, and the number moves
    // when the assembly does.
    TempDir dir;
    const auto path = dir.path() / "machine.bcad";
    writeAssembly(path);
    const std::string file = cliPath(path);
    cliOk({"sheet-add", file, "--format", "A3"});
    cliOk({"view-add", file, "--sheet", "Sheet1", "--assembly", "--name", "Assembly"});
    cliOk({"annotation-add", file, "--view", "Assembly", "--type", "balloon", "--target",
           "object:C1", "--name", "BalloonC", "--x", "120mm", "--y", "120mm"});

    // The stored target is the OCCURRENCE.
    {
        auto loaded = io::loadDocument(path);
        REQUIRE(loaded.has_value());
        const auto ids = drawing::annotations(*loaded);
        REQUIRE(ids.size() == 1);
        const auto& definition = drawing::findAnnotation(*loaded, ids.front())->definition();
        REQUIRE(definition.target.object.has_value());
        const DocumentObject* named = loaded->findObject(*definition.target.object);
        REQUIRE(named != nullptr);
        CHECK(named->name() == "C1");
    }

    const auto report = runCliCommand({"drawing", file});
    INFO(report.out);
    CHECK(report.exitCode == ExitCode::Success);
    CHECK_THAT(report.out, ContainsSubstring("balloon"));
    CHECK_THAT(report.out, ContainsSubstring("resolved"));

    // Retargeting is an explicit edit and never something that happens by
    // itself.
    cliOk({"annotation-set", file, "BalloonC", "--target", "object:B1"});
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    const auto ids = drawing::annotations(*loaded);
    const auto& definition = drawing::findAnnotation(*loaded, ids.front())->definition();
    CHECK(loaded->findObject(*definition.target.object)->name() == "B1");
}

// --- Configuration, and unresolved references -------------------------------------------------------------

TEST_CASE("DrawingCli_SwitchingConfigurationChangesWhatTheDrawingReports",
          "[cli][drawing][p14][config]") {
    TempDir dir;
    const auto path = dir.path() / "machine.bcad";
    writeAssembly(path);
    const std::string file = cliPath(path);
    cliOk({"sheet-add", file, "--format", "A3"});
    cliOk({"view-add", file, "--sheet", "Sheet1", "--assembly", "--name", "Assembly"});
    cliOk({"annotation-add", file, "--view", "Assembly", "--type", "balloon", "--target",
           "object:C1", "--name", "BalloonC", "--x", "120mm", "--y", "120mm"});
    cliOk({"configuration-add", file, "Reduced"});
    cliOk({"suppress", file, "C1", "--configuration", "Reduced"});

    // Base: six occurrences, and the balloon resolves.
    const auto base = runCliCommand({"drawing", file});
    CHECK(base.exitCode == ExitCode::Success);
    CHECK_THAT(base.out, ContainsSubstring("6 occurrences"));

    // Reduced: five, and the balloon's occurrence is not in this build, which
    // the report says honestly rather than hiding.
    const auto reduced = runCliCommand({"drawing", file, "--configuration", "Reduced"});
    INFO(reduced.out);
    CHECK(reduced.exitCode == ExitCode::Failure); // the drawing does not fully regenerate
    CHECK_THAT(reduced.out, ContainsSubstring("5 occurrences"));
    CHECK_THAT(reduced.out, ContainsSubstring("unresolved"));

    // Back to base: the SAME balloon resolves again -- recovery, not
    // rebinding, and the stored target never moved.
    const auto again = runCliCommand({"drawing", file});
    CHECK(again.exitCode == ExitCode::Success);
    CHECK_THAT(again.out, ContainsSubstring("6 occurrences"));
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    const auto ids = drawing::annotations(*loaded);
    const auto& definition = drawing::findAnnotation(*loaded, ids.front())->definition();
    CHECK(loaded->findObject(*definition.target.object)->name() == "C1");
}

TEST_CASE("DrawingCli_TheReportFailsWhenTheDrawingDoesAndSaysWhy", "[cli][drawing][p14][exit]") {
    // "Never return 0 simply because the process did not crash." A drawing
    // whose reference has stopped resolving exits non-zero and names the
    // object.
    TempDir dir;
    const auto path = dir.path() / "part.bcad";
    const Part part = writePart(path);
    const std::string file = cliPath(path);
    cliOk({"sheet-add", file, "--format", "A3"});
    cliOk({"view-add", file, "--sheet", "Sheet1", "--source", "Block"});
    cliOk({"dimension-add", file, "--view", "View1", "--type", "linear", "--from",
           "face:Block:side:" + std::to_string(part.lines[3].value()), "--to",
           "face:Block:side:" + std::to_string(part.lines[1].value()), "--decimals", "2"});
    const auto healthy = runCliCommand({"drawing", file});
    REQUIRE(healthy.exitCode == ExitCode::Success);
    CHECK_THAT(healthy.out, ContainsSubstring("100.00"));

    // Take the sketch entity the dimension names away, keeping the solid
    // buildable: the rectangle becomes a triangle.
    {
        auto loaded = io::loadDocument(path);
        REQUIRE(loaded.has_value());
        Document document = std::move(*loaded);
        REQUIRE(document
                    .modifyObject<sketch::Sketch>(
                        part.sketch,
                        [&, corners = part.corners](sketch::Sketch& s) {
                            // A triangle: the solid still builds, and only
                            // the face the dimension names has gone. Breaking
                            // the profile instead would fail the EXTRUDE, and
                            // prove nothing about the drawing.
                            REQUIRE(s.removeEntity(part.lines[1]).has_value());
                            REQUIRE(s.removeEntity(part.lines[2]).has_value());
                            REQUIRE(s.addLine(corners[1], corners[3]).has_value());
                            return true;
                        })
                    .has_value());
        REQUIRE(io::saveDocument(document, path).has_value());
    }

    const auto broken = runCliCommand({"drawing", file});
    INFO(broken.out);
    CHECK(broken.exitCode == ExitCode::Failure);
    CHECK_THAT(broken.out, ContainsSubstring("Dimension1"));
}

// --- Determinism ---------------------------------------------------------------------------------------------

TEST_CASE("DrawingCli_TheSameScriptTwiceGivesTheSameFileAndTheSameReport",
          "[cli][drawing][p14][determinism]") {
    TempDir dir;
    const auto script = dir.path() / "drawing.txt";
    {
        std::ofstream out{script};
        out << "sheet-add --format A3 --scale 1:1\n";
        out << "view-add --sheet Sheet1 --source Block --x 150mm --y 150mm\n";
        out << "dimension-add --view View1 --type linear --from face:Block:start_cap "
               "--to face:Block:end_cap --decimals 2\n";
        out << "annotation-add --view View1 --type note --text NOTE --x 40mm --y 40mm\n";
    }

    std::vector<std::string> files;
    std::vector<std::string> reports;
    for (int run = 0; run < 3; ++run) {
        const auto path = dir.path() / std::format("run{}.bcad", run);
        writePart(path);
        const auto applied = runCliCommand({"batch", cliPath(path), cliPath(script)});
        INFO(applied.err);
        REQUIRE(applied.exitCode == ExitCode::Success);
        files.push_back(canonicalOf(path));
        const auto report = runCliCommand({"drawing", cliPath(path)});
        REQUIRE(report.exitCode == ExitCode::Success);
        reports.push_back(report.out);
    }
    // The documents differ only in their own UUIDs, so the DRAWING part of
    // each is compared through the report, which names objects by ID and name.
    CHECK(reports[0] == reports[1]);
    CHECK(reports[1] == reports[2]);
    CHECK_FALSE(reports[0].empty());
}

// --- Adversarial: what the command line must NOT let anyone say ---------------------------------------

TEST_CASE("DrawingCli_ThereIsNoWayToNameGeometryByPosition", "[cli][drawing][p14][stref]") {
    // P14-STREF-001's rule, at the one place a user could try to break it.
    // The target grammar has no index form -- not "face 3", not "edge 7", not
    // an ordinal -- so a positional reference is not something the CLI
    // refuses, it is something it cannot express. These are the spellings
    // someone would reach for, and each is an unreadable target rather than a
    // fragile one.
    TempDir dir;
    const auto path = dir.path() / "part.bcad";
    writePart(path);
    const std::string file = cliPath(path);
    cliOk({"sheet-add", file, "--format", "A3"});
    cliOk({"view-add", file, "--sheet", "Sheet1", "--source", "Block"});
    const std::string before = bytesOf(path);

    for (const std::string target :
         {"face:3", "face:Block:3", "index:3", "edge:7", "faceindex:2", "Block:face:2",
          "nearest:100,60", "screen:400,300"}) {
        INFO(target);
        const auto run = runCliCommand({"dimension-add", file, "--view", "View1", "--type", "linear",
                                        "--from", target});
        CHECK(run.exitCode != ExitCode::Success);
        CHECK(bytesOf(path) == before);
    }

    // What IS expressible is the semantic form, and it works.
    cliOk({"dimension-add", file, "--view", "View1", "--type", "linear", "--from",
           "face:Block:start_cap", "--to", "face:Block:end_cap"});
}

TEST_CASE("DrawingCli_ABalloonOnAPartRatherThanAnOccurrenceIsDiagnosedNotAccepted",
          "[cli][drawing][p14][bom]") {
    // ADR-022's distinction, which a command line is exactly the place to get
    // wrong: "PartA" is the part DEFINITION and "A1" is an OCCURRENCE of it.
    //
    // WHERE IT IS CAUGHT is the architecture's answer and not the CLI's.
    // checkAnnotation() validates that the object a target names EXISTS -- it
    // says so in as many words, and leaves what the object IS to the resolver,
    // which needs the regenerated bodies. So the command is accepted and
    // REGENERATION reports it, exactly as a view cycle in a file is
    // (P14-PERSIST-001). The CLI inherits that rather than inventing a
    // stricter rule of its own, which would be a second opinion about what a
    // balloon may name.
    //
    // What matters is that it is never SILENT, and that the state is the
    // right one of the three: Invalid, not Unresolved, because a part
    // definition cannot become a balloon target in any configuration.
    TempDir dir;
    const auto path = dir.path() / "machine.bcad";
    writeAssembly(path);
    const std::string file = cliPath(path);
    cliOk({"sheet-add", file, "--format", "A3"});
    cliOk({"view-add", file, "--sheet", "Sheet1", "--assembly", "--name", "Assembly"});

    cliOk({"annotation-add", file, "--view", "Assembly", "--type", "balloon", "--target",
           "object:PartA", "--name", "Wrong", "--x", "10mm", "--y", "10mm"});

    const auto report = runCliCommand({"drawing", file});
    INFO(report.out);
    CHECK(report.exitCode == ExitCode::Failure);
    CHECK_THAT(report.out, ContainsSubstring("invalid"));
    CHECK_THAT(report.out, ContainsSubstring("not a hole or a component"));
    CHECK_THAT(report.out, ContainsSubstring("Regeneration: FAILED"));

    // Pointed at the OCCURRENCE instead, the same drawing is sound.
    cliOk({"annotation-set", file, "Wrong", "--target", "object:A1"});
    const auto fixed = runCliCommand({"drawing", file});
    INFO(fixed.out);
    CHECK(fixed.exitCode == ExitCode::Success);
    CHECK_THAT(fixed.out, ContainsSubstring("Regeneration: ok"));
    CHECK_THAT(fixed.out, !ContainsSubstring("invalid"));
}

TEST_CASE("DrawingCli_AMalformedSelectorNeverFallsBackToTheFirstObject",
          "[cli][drawing][p14][stref]") {
    // "Can malformed IDs default to object 0 or the first object?" No: 0 is
    // not a valid ID, an empty selector is neither an ID nor a name, and a
    // name that is not there is NotFound rather than a guess.
    TempDir dir;
    const auto path = dir.path() / "part.bcad";
    writePart(path);
    const std::string file = cliPath(path);
    cliOk({"sheet-add", file, "--format", "A3"});
    cliOk({"view-add", file, "--sheet", "Sheet1", "--source", "Block"});
    const std::string before = bytesOf(path);

    for (const std::string selector : {"0", "", "999999", "-1", "1.5", "View 1", "%%"}) {
        INFO("selector '" << selector << "'");
        const auto run = runCliCommand({"view-move", file, selector, "--x", "1mm", "--y", "1mm"});
        CHECK(run.exitCode != ExitCode::Success);
        CHECK(bytesOf(path) == before);
    }
    // The view really is still where it was, which is the point of the
    // byte comparison above stated in the model's own terms.
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    const auto views = drawing::views(*loaded);
    REQUIRE(views.size() == 1);
    CHECK_THAT(drawing::findView(*loaded, views.front())->definition().placement.x.in(units::mm),
               WithinAbs(0.0, kMm));
}

TEST_CASE("DrawingCli_APathWithSpacesAndNonAsciiWorks", "[cli][drawing][p14]") {
    // Windows and PowerShell make this the easiest thing in the CLI to get
    // wrong. The arguments reach the command as UTF-8 and are turned into a
    // path once, so a directory with a space in it is not a quoting problem
    // for the model to care about.
    TempDir dir;
    const auto folder = dir.path() / "Drawing Office";
    std::filesystem::create_directories(folder);
    const auto path = folder / "front plate.bcad";
    writePart(path);
    const std::string file = cliPath(path);

    cliOk({"sheet-add", file, "--format", "A3"});
    cliOk({"view-add", file, "--sheet", "Sheet1", "--source", "Block"});
    const auto report = runCliCommand({"drawing", file});
    INFO(report.err);
    CHECK(report.exitCode == ExitCode::Success);
    CHECK_THAT(report.out, ContainsSubstring("Sheets (1):"));
    CHECK(std::filesystem::exists(path));
}

// --- The export boundary -----------------------------------------------------------------------------------------

TEST_CASE("DrawingCli_EachDrawingFormatIsWrittenAndSaysWhatItWrote",
          "[cli][drawing][p14][export]") {
    // P14-EXPORT-001's commands, through the CLI. The CLI chooses the writer
    // and builds nothing: the geometry is the scene's and the file is the
    // writer's.
    TempDir dir;
    const auto path = dir.path() / "part.bcad";
    writePart(path);
    const std::string file = cliPath(path);
    cliOk({"sheet-add", file, "--format", "A3", "--scale", "1:1"});
    cliOk({"view-add", file, "--sheet", "Sheet1", "--source", "Block", "--x", "150mm", "--y", "150mm"});
    cliOk({"annotation-add", file, "--view", "View1", "--type", "note", "--text", "NOTE", "--x",
           "40mm", "--y", "40mm"});

    for (const auto& [command, extension, says] :
         std::vector<std::tuple<std::string, std::string, std::string>>{
             {"export-svg", "svg", "SVG"},
             {"export-dxf", "dxf", "DXF"},
             {"export-pdf", "pdf", "PDF"}}) {
        INFO(command);
        const auto out = dir.path() / ("drawing." + extension);
        const auto run = runCliCommand({command, file, cliPath(out)});
        INFO(run.err);
        CHECK(run.exitCode == ExitCode::Success);
        CHECK_THAT(run.out, ContainsSubstring(says));
        CHECK_THAT(run.out, ContainsSubstring("420 x 297 mm"));
        REQUIRE(std::filesystem::exists(out));
        CHECK(std::filesystem::file_size(out) > 100);
    }

    // Each file really is its own format, checked at its first bytes.
    const auto head = [&](const std::string& extension, std::size_t count) {
        std::ifstream stream{dir.path() / ("drawing." + extension), std::ios::binary};
        std::string text(count, '\0');
        stream.read(text.data(), static_cast<std::streamsize>(count));
        return text;
    };
    CHECK_THAT(head("svg", 40), ContainsSubstring("<?xml"));
    CHECK_THAT(head("pdf", 8), ContainsSubstring("%PDF-1.4"));
    CHECK_THAT(head("dxf", 12), ContainsSubstring("SECTION"));
}

TEST_CASE("DrawingCli_AnExportThatCannotBeTrustedIsRefusedRatherThanWritten",
          "[cli][drawing][p14][export]") {
    // "No false success." Every one of these exits non-zero and leaves no
    // file, because a drawing written from a sheet that does not resolve is
    // worse than no drawing.
    TempDir dir;
    const auto path = dir.path() / "part.bcad";
    const Part part = writePart(path);
    const std::string file = cliPath(path);
    cliOk({"sheet-add", file, "--format", "A3"});
    // Placed on the page: a view with no --x/--y sits at the sheet's corner,
    // where half of it hangs off, and the scene refuses to export that.
    cliOk({"view-add", file, "--sheet", "Sheet1", "--source", "Block", "--x", "150mm", "--y",
           "150mm"});

    const auto out = dir.path() / "drawing.svg";
    const auto refuse = [&](const std::string& what, const std::vector<std::string>& args,
                            const std::filesystem::path& expected) {
        INFO(what);
        const auto run = runCliCommand(args);
        CHECK(run.exitCode != ExitCode::Success);
        CHECK(run.out.empty());
        CHECK_FALSE(std::filesystem::exists(expected));
    };

    refuse("a path that cannot be written",
           {"export-svg", file, cliPath(dir.path() / "no" / "such" / "x.svg")},
           dir.path() / "no" / "such" / "x.svg");
    refuse("a sheet that is not there", {"export-svg", file, cliPath(out), "--sheet", "Sheet404"}, out);
    refuse("a selector naming a view", {"export-svg", file, cliPath(out), "--sheet", "View1"}, out);
    refuse("one path instead of two", {"export-svg", file}, out);

    // A document with no sheets has no drawing to write.
    const auto bare = dir.path() / "bare.bcad";
    writePart(bare, "Bare");
    refuse("a document with no sheets", {"export-svg", cliPath(bare), cliPath(out)}, out);

    // Two sheets and no --sheet: refused rather than guessed at, because
    // writing the wrong page silently is worse than being asked which.
    cliOk({"sheet-add", file, "--format", "A3", "--name", "Sheet2"});
    refuse("two sheets and no --sheet", {"export-svg", file, cliPath(out)}, out);
    // Naming one works.
    cliOk({"export-svg", file, cliPath(out), "--sheet", "Sheet1"});
    CHECK(std::filesystem::exists(out));

    // A BROKEN drawing: the dimension's face stops existing, so the sheet does
    // not regenerate and nothing is written.
    const auto broken = dir.path() / "broken.svg";
    cliOk({"dimension-add", file, "--view", "View1", "--type", "linear", "--from",
           "face:Block:side:" + std::to_string(part.lines[3].value()), "--to",
           "face:Block:side:" + std::to_string(part.lines[1].value()), "--x", "150mm", "--y",
           "100mm"});
    {
        auto loaded = io::loadDocument(path);
        REQUIRE(loaded.has_value());
        Document document = std::move(*loaded);
        REQUIRE(document
                    .modifyObject<sketch::Sketch>(
                        part.sketch,
                        [&, corners = part.corners](sketch::Sketch& sk) {
                            REQUIRE(sk.removeEntity(part.lines[1]).has_value());
                            REQUIRE(sk.removeEntity(part.lines[2]).has_value());
                            REQUIRE(sk.addLine(corners[1], corners[3]).has_value());
                            return true;
                        })
                    .has_value());
        REQUIRE(io::saveDocument(document, path).has_value());
    }
    const auto run = runCliCommand({"export-svg", file, cliPath(broken), "--sheet", "Sheet1"});
    CHECK(run.exitCode == ExitCode::Failure);
    CHECK_THAT(run.err, ContainsSubstring("does not regenerate"));
    CHECK_FALSE(std::filesystem::exists(broken));
}

TEST_CASE("DrawingCli_ExportingTwiceGivesTheSameBytes", "[cli][drawing][p14][export]") {
    TempDir dir;
    const auto path = dir.path() / "part.bcad";
    writePart(path);
    const std::string file = cliPath(path);
    cliOk({"sheet-add", file, "--format", "A3"});
    cliOk({"view-add", file, "--sheet", "Sheet1", "--source", "Block", "--x", "150mm", "--y",
           "150mm"});

    for (const auto& [command, extension] : std::vector<std::pair<std::string, std::string>>{
             {"export-svg", "svg"}, {"export-dxf", "dxf"}, {"export-pdf", "pdf"}}) {
        INFO(command);
        const auto first = dir.path() / ("first." + extension);
        const auto second = dir.path() / ("second." + extension);
        cliOk({command, file, cliPath(first)});
        cliOk({command, file, cliPath(second)});
        CHECK(bytesOf(first) == bytesOf(second));
        CHECK_FALSE(bytesOf(first).empty());
    }
}
