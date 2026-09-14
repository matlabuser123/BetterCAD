#include "Cli.hpp"
#include "support/BracketModel.hpp"
#include "support/ChamferBlockModel.hpp"
#include "support/FilletModels.hpp"
#include "support/HoleModels.hpp"
#include "support/MeshAnalysis.hpp"
#include "support/PatternModels.hpp"
#include "support/TestFiles.hpp"
#include "support/TurnedPartModel.hpp"
#include "support/occt/StepReadBack.hpp"

#include <bettercad/core/BuildInfo.hpp>
#include <bettercad/io/DocumentFile.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using bettercad::cli::ExitCode;
using bettercad::test::BracketModel;
using bettercad::test::readFile;
using bettercad::test::TempDir;
using bettercad::test::writeFile;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::EndsWith;
using Catch::Matchers::StartsWith;
using Catch::Matchers::WithinRel;

namespace {

struct CliResult {
    ExitCode exitCode;
    std::string out;
    std::string err;
};

CliResult runCli(const std::vector<std::string>& args) {
    const std::vector<std::string_view> argv(args.begin(), args.end());
    std::ostringstream out;
    std::ostringstream err;
    const ExitCode code = bettercad::cli::run(argv, out, err);
    return {code, out.str(), err.str()};
}

CliResult runCli(std::initializer_list<std::string_view> args) {
    return runCli(std::vector<std::string>(args.begin(), args.end()));
}

/// UTF-8 text of a path, as the command line passes it.
std::string arg(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

std::filesystem::path saveBracket(const TempDir& dir, BracketModel& model) {
    const auto path = dir.path() / "bracket.bcad";
    REQUIRE(io::saveDocument(model.doc, path).has_value());
    return path;
}

} // namespace

TEST_CASE("CLI --version prints program name and version", "[cli]") {
    const auto result = runCli({"--version"});

    CHECK(result.exitCode == ExitCode::Success);
    CHECK(result.out == "bettercad-cli " + std::string{bettercad::buildInfo().version} + "\n");
    CHECK(result.err.empty());
}

TEST_CASE("CLI version command prints the full build report", "[cli]") {
    const auto result = runCli({"version"});

    CHECK(result.exitCode == ExitCode::Success);
    CHECK(result.out == bettercad::formatBuildInfo(bettercad::buildInfo()));
    CHECK(result.err.empty());
}

TEST_CASE("CLI help lists every command", "[cli]") {
    for (const std::string_view flag : {"--help", "-h", "help"}) {
        CAPTURE(flag);
        const auto result = runCli({flag});

        CHECK(result.exitCode == ExitCode::Success);
        CHECK_THAT(result.out, StartsWith("Usage: bettercad-cli <command>"));
        for (const std::string_view command : {"new <file.bcad>", "info <file.bcad>", "validate <file.bcad>",
                                               "export-step <file.bcad> <file.step>",
                                               "export-stl <file.bcad> <file.stl>", "  help", "  version"}) {
            CHECK_THAT(result.out, ContainsSubstring(std::string{command}));
        }
        CHECK(result.err.empty());
    }
}

TEST_CASE("CLI without arguments prints usage to stderr and fails", "[cli]") {
    const auto result = runCli({});

    CHECK(result.exitCode == ExitCode::UsageError);
    CHECK(result.out.empty());
    CHECK_THAT(result.err, StartsWith("Usage: bettercad-cli"));
}

TEST_CASE("CLI rejects unknown commands", "[cli]") {
    const auto result = runCli({"frobnicate"});

    CHECK(result.exitCode == ExitCode::UsageError);
    CHECK(result.out.empty());
    CHECK_THAT(result.err, ContainsSubstring("unknown command 'frobnicate'"));
}

TEST_CASE("CLI commands reject unexpected arguments", "[cli]") {
    const auto result = runCli({"version", "--bogus"});

    CHECK(result.exitCode == ExitCode::UsageError);
    CHECK(result.out.empty());
    CHECK_THAT(result.err, ContainsSubstring("unexpected argument '--bogus'"));
}

TEST_CASE("CLI commands check their argument counts", "[cli]") {
    for (const auto& args : std::vector<std::vector<std::string>>{
             {"new"}, {"new", "a.bcad", "b.bcad"}, {"info"}, {"validate", "a", "b"}, {"export-step", "a.bcad"},
             {"export-stl", "a.bcad"}, {"info", "--frobnicate", "a.bcad"}, {"new", "a.bcad", "--name"}}) {
        CAPTURE(args);
        const auto result = runCli(args);
        CHECK(result.exitCode == ExitCode::UsageError);
        CHECK(result.out.empty());
        CHECK_THAT(result.err, ContainsSubstring("Usage: bettercad-cli " + args.front()));
    }
}

TEST_CASE("new creates an empty, valid document and refuses to overwrite", "[cli][new]") {
    TempDir dir;
    const auto path = dir.path() / "part.bcad";

    const auto created = runCli({"new", arg(path)});
    CHECK(created.exitCode == ExitCode::Success);
    CHECK_THAT(created.out, StartsWith("Created " + arg(path) + " (document 'part', ID "));
    CHECK(created.err.empty());
    auto document = io::loadDocument(path);
    REQUIRE(document.has_value());
    CHECK(document->name() == "part");
    CHECK(document->objectCount() == 0);
    CHECK(document->parameters().empty());
    CHECK(runCli({"validate", arg(path)}).exitCode == ExitCode::Success);

    const std::string original = readFile(path);
    const auto again = runCli({"new", arg(path)});
    CHECK(again.exitCode == ExitCode::Failure);
    CHECK_THAT(again.err, ContainsSubstring("already exists (use --force to replace it)"));
    CHECK(readFile(path) == original);

    const auto forced = runCli({"new", arg(path), "--force", "--name=Flange plate"});
    CHECK(forced.exitCode == ExitCode::Success);
    document = io::loadDocument(path);
    REQUIRE(document.has_value());
    CHECK(document->name() == "Flange plate");

    const auto invalid = runCli({"new", arg(dir.path() / "x.bcad"), "--name", ""});
    CHECK(invalid.exitCode == ExitCode::UsageError);
    CHECK_FALSE(std::filesystem::exists(dir.path() / "x.bcad"));

    const auto unwritable = runCli({"new", arg(dir.path() / "missing" / "x.bcad")});
    CHECK(unwritable.exitCode == ExitCode::Failure);
    CHECK_THAT(unwritable.err, ContainsSubstring("cannot write"));
}

TEST_CASE("info shows metadata, parameters and objects", "[cli][info]") {
    TempDir dir;
    BracketModel model;
    const auto path = saveBracket(dir, model);

    const auto result = runCli({"info", arg(path)});
    CHECK(result.exitCode == ExitCode::Success);
    CHECK(result.err.empty());
    const std::string& out = result.out;
    CHECK_THAT(out, StartsWith("Document: Bracket\nFile: " + arg(path) + "\nID: " +
                               model.doc.id().value().toString() + "\n"));
    CHECK_THAT(out, ContainsSubstring("Author: BetterCAD tests\n"));
    CHECK_THAT(out, ContainsSubstring("Property material: 6061-T6\n"));
    CHECK_THAT(out, ContainsSubstring("\nParameters (6):\n  width        100 mm\n"));
    CHECK_THAT(out, ContainsSubstring("\n  slot_depth   12 mm  (expression: depth * 0.6)\n"));
    CHECK_THAT(out, ContainsSubstring("\n  draft        1.5 deg\n"));
    CHECK_THAT(out, ContainsSubstring("\nObjects (6):\n"));
    CHECK_THAT(out, ContainsSubstring(
                        "\n  object:8   sketch   Base          14 entities, 10 constraints (1 disabled), "
                        "driven by width, height, hole_radius\n"));
    CHECK_THAT(out, ContainsSubstring(
                        "\n  object:11  extrude  Pocket        profile PocketSketch, depth 5 mm, normal, cut Pad\n"));
    CHECK_THAT(out, ContainsSubstring(
                        "\n  object:13  extrude  Slot          profile SlotSketch, depth slot_depth, symmetric, "
                        "new body\n"));

    const auto missing = runCli({"info", arg(dir.path() / "missing.bcad")});
    CHECK(missing.exitCode == ExitCode::Failure);
    CHECK_THAT(missing.err, ContainsSubstring("does not exist"));
}

TEST_CASE("validate reports each check and the result bodies", "[cli][validate]") {
    TempDir dir;
    BracketModel model;

    SECTION("a valid model") {
        const auto result = runCli({"validate", arg(saveBracket(dir, model))});
        CHECK(result.exitCode == ExitCode::Success);
        CHECK(result.err.empty());
        const std::string& out = result.out;
        CHECK_THAT(out, ContainsSubstring("\n  document consistency  ok\n"
                                          "  missing references    ok\n"
                                          "  dependency cycles     ok\n"
                                          "  sketch constraints    3 warnings\n"
                                          "    warning: Base (object:8) is under-constrained: 5 degree(s) of freedom\n"));
        CHECK_THAT(out, ContainsSubstring("\n  feature regeneration  ok, 6 objects regenerated\n"
                                          "  geometry              ok, 2 result bodies\n"
                                          "Result bodies (2):\n"
                                          "  Pocket (object:11): 1 solid, volume 114853."));
        CHECK_THAT(out, ContainsSubstring("bounds (0, 0, 0) to (100, 60, 20) mm\n"));
        CHECK_THAT(out, EndsWith("Result: valid (3 warnings)\n"));
    }
    SECTION("a model with errors") {
        REQUIRE(model.doc.removeObject(model.slotSketch).has_value());
        REQUIRE(model.doc.setParameterValue(model.depth, -(1_mm)).has_value());
        const auto result = runCli({"validate", arg(saveBracket(dir, model))});
        CHECK(result.exitCode == ExitCode::Failure);
        CHECK_THAT(result.out,
                   ContainsSubstring("  missing references    1 error\n"
                                     "    error: Slot (object:13) references object:12, which does not exist\n"));
        CHECK_THAT(result.out, ContainsSubstring("error: Pad (object:9) failed to regenerate: extrude depth must be "
                                                 "positive, got -1 mm\n"));
        CHECK_THAT(result.out, ContainsSubstring("error: Pocket (object:11) was not regenerated"));
        CHECK_THAT(result.out, EndsWith("Result: invalid (3 errors, 2 warnings)\n"));
    }
    SECTION("a file that does not load") {
        const auto path = dir.path() / "broken.bcad";
        writeFile(path, "{ \"format\": \"bettercad-document\" }");
        const auto result = runCli({"validate", arg(path)});
        CHECK(result.exitCode == ExitCode::Failure);
        CHECK_THAT(result.out, ContainsSubstring("  document consistency  1 error\n    error: broken.bcad: "));
        CHECK_THAT(result.out, EndsWith("Result: invalid (1 error)\n"));
    }
}

TEST_CASE("export-step writes the result bodies", "[cli][export]") {
    TempDir dir;
    BracketModel model;
    const auto output = dir.path() / "bracket.step";

    const auto result = runCli({"export-step", arg(saveBracket(dir, model)), arg(output)});
    CHECK(result.exitCode == ExitCode::Success);
    CHECK(result.err.empty());
    CHECK_THAT(result.out, StartsWith("Wrote " + arg(output) + ": STEP AP214 (mm), 2 bodies (Pocket, Slot), "));
    const auto contents = test::readStepFile(output);
    REQUIRE(contents.has_value());
    CHECK(contents->solids == 2);
    CHECK_THAT(contents->volumeMm3, WithinRel(BracketModel::kPocketVolume + BracketModel::kSlotVolume, 1e-9));
}

TEST_CASE("export-stl writes a closed mesh with the requested accuracy", "[cli][export]") {
    TempDir dir;
    BracketModel model;
    const auto input = saveBracket(dir, model);
    const auto coarse = dir.path() / "coarse.stl";
    const auto fine = dir.path() / "fine.stl";

    const auto first = runCli({"export-stl", arg(input), arg(coarse)});
    CHECK(first.exitCode == ExitCode::Success);
    CHECK_THAT(first.out, ContainsSubstring(": binary STL (mm), 2 bodies (Pocket: "));
    CHECK_THAT(first.out, ContainsSubstring("tolerance 0.1 mm and 20 deg"));
    const auto second = runCli({"export-stl", arg(input), arg(fine), "--ascii", "--tolerance", "0.0004in",
                                "--angle=0.1rad"});
    CHECK(second.exitCode == ExitCode::Success);
    CHECK_THAT(second.out, ContainsSubstring(": ASCII STL (mm)"));
    CHECK_THAT(second.out, ContainsSubstring("tolerance 0.01016 mm and 5.72958 deg"));

    const auto coarseMesh = test::parseBinaryStl(readFile(coarse));
    const auto fineMesh = test::parseAsciiStl(readFile(fine));
    REQUIRE(coarseMesh.has_value());
    REQUIRE(fineMesh.has_value());
    CHECK(fineMesh->triangles.size() > coarseMesh->triangles.size());
    const double exact = BracketModel::kPocketVolume + BracketModel::kSlotVolume;
    CHECK(std::abs(test::enclosedVolume(fineMesh->triangles) - exact) <
          std::abs(test::enclosedVolume(coarseMesh->triangles) - exact));
    CHECK_THAT(test::enclosedVolume(fineMesh->triangles), WithinRel(exact, 1e-3));
}

TEST_CASE("export-stl validates its options", "[cli][export]") {
    for (const auto& [option, message] : std::vector<std::pair<std::vector<std::string>, std::string>>{
             {{"--tolerance", "abc"}, "--tolerance: 'abc' is not a number"},
             {{"--tolerance", "0.1kg"}, "--tolerance: '0.1kg' has dimension mass; expected length"},
             {{"--tolerance", "0.1 furlong"}, "--tolerance: unknown unit 'furlong'"},
             {{"--angle", "5mm"}, "--angle: '5mm' has dimension length; expected angle"},
             {{"--ascii=yes"}, "option '--ascii' takes no value"},
             {{"--ascii", "--ascii"}, "option '--ascii' is given twice"}}) {
        CAPTURE(option);
        std::vector<std::string> args{"export-stl", "in.bcad", "out.stl"};
        args.insert(args.end(), option.begin(), option.end());
        const auto result = runCli(args);
        CHECK(result.exitCode == ExitCode::UsageError);
        CHECK_THAT(result.err, ContainsSubstring(message));
    }
}

TEST_CASE("Exports report models that do not regenerate", "[cli][export]") {
    TempDir dir;
    BracketModel model;
    REQUIRE(model.doc.setParameterValue(model.depth, 0_mm).has_value()); // the pad cannot be built
    const auto input = saveBracket(dir, model);
    for (const std::string command : {"export-step", "export-stl"}) {
        CAPTURE(command);
        const auto output = dir.path() / "out";
        const auto result = runCli({command, arg(input), arg(output)});
        CHECK(result.exitCode == ExitCode::Failure);
        CHECK_THAT(result.err, ContainsSubstring("the model does not regenerate"));
        CHECK_FALSE(std::filesystem::exists(output));
    }
}

TEST_CASE("info and validate describe revolves", "[cli][revolve]") {
    TempDir dir;
    test::TurnedPartModel model;
    const auto path = dir.path() / "shaft.bcad";
    REQUIRE(io::saveDocument(model.doc, path).has_value());

    const auto info = runCli({"info", arg(path)});
    CHECK(info.exitCode == ExitCode::Success);
    CHECK_THAT(info.out, ContainsSubstring(
                             "\n  object:6   revolve  Turn          profile Profile, axis sketch Y axis, angle sweep, "
                             "positive, new body\n"));
    CHECK_THAT(info.out, ContainsSubstring(
                             "\n  object:10  revolve  Groove        profile GrooveSketch, axis line entity:11, "
                             "angle 360 deg, positive, cut Bore\n"));
    CHECK_THAT(info.out, ContainsSubstring("\n  sweep   360 deg\n"));

    const auto validate = runCli({"validate", arg(path)});
    CHECK(validate.exitCode == ExitCode::Success);
    CHECK_THAT(validate.out, ContainsSubstring("  Groove (object:10): 1 solid, volume "));
    CHECK_THAT(validate.out, EndsWith("Result: valid (1 warning)\n"));
}

TEST_CASE("info and validate describe chamfers", "[cli][chamfer]") {
    TempDir dir;
    test::ChamferVariants model;
    const auto path = dir.path() / "block.bcad";
    REQUIRE(io::saveDocument(model.doc, path).has_value());

    const auto info = runCli({"info", arg(path)});
    CHECK(info.exitCode == ExitCode::Success);
    CHECK_THAT(info.out, ContainsSubstring("\n  object:7  chamfer  Edge   target Pad, 1 edge, equal distance size\n"
                                           "  object:8  chamfer  Bevel  target Edge, 1 edge, two distances 4 mm and "
                                           "2 mm\n"
                                           "  object:9  chamfer  Slope  target Bevel, 1 edge, distance and angle 3 mm "
                                           "and 30 deg\n"));

    const auto validate = runCli({"validate", arg(path)});
    CHECK(validate.exitCode == ExitCode::Success);
    CHECK_THAT(validate.out, ContainsSubstring("geometry              ok, 1 result body\n"
                                               "Result bodies (1):\n"
                                               "  Slope (object:9): 1 solid, volume 98220.096 mm^3, area "));
    CHECK_THAT(validate.out, EndsWith("bounds (0, 0, 0) to (100, 50, 20) mm\nResult: valid\n"));

    // A chamfer whose edge has moved makes the document invalid, with the reason.
    REQUIRE(model.doc.setParameterValue(model.height, 30_mm).has_value());
    REQUIRE(io::saveDocument(model.doc, path).has_value());
    const auto broken = runCli({"validate", arg(path)});
    CHECK(broken.exitCode == ExitCode::Failure);
    CHECK_THAT(broken.out, ContainsSubstring("    error: Edge (object:7) failed to regenerate: Edge: chamfer: edge "
                                             "reference 1 (line through (0, 0, 20) mm along (1, 0, 0)) matches no "
                                             "edge of the body\n"));
    CHECK_THAT(broken.out, EndsWith("Result: invalid (3 errors)\n"));
}

TEST_CASE("info and validate describe fillets", "[cli][fillet]") {
    TempDir dir;
    test::FilletVariants model;
    const auto path = dir.path() / "block.bcad";
    REQUIRE(io::saveDocument(model.doc, path).has_value());

    const auto info = runCli({"info", arg(path)});
    CHECK(info.exitCode == ExitCode::Success);
    CHECK_THAT(info.out, ContainsSubstring("\n  object:7  fillet   Round   target Pad, 1 edge, radius radius\n"
                                           "  object:8  fillet   Corner  target Round, 3 edges, radius 3 mm\n"));

    const auto validate = runCli({"validate", arg(path)});
    CHECK(validate.exitCode == ExitCode::Success);
    CHECK_THAT(validate.out, ContainsSubstring("geometry              ok, 1 result body\n"
                                               "Result bodies (1):\n"
                                               "  Corner (object:8): 1 solid, volume 99139.675 mm^3, area "));
    CHECK_THAT(validate.out, EndsWith("bounds (0, 0, 0) to (100, 50, 20) mm\nResult: valid\n"));

    // A fillet whose edge has moved makes the document invalid, with the reason.
    REQUIRE(model.doc.setParameterValue(model.height, 30_mm).has_value());
    REQUIRE(io::saveDocument(model.doc, path).has_value());
    const auto broken = runCli({"validate", arg(path)});
    CHECK(broken.exitCode == ExitCode::Failure);
    CHECK_THAT(broken.out, ContainsSubstring("    error: Round (object:7) failed to regenerate: Round: fillet: edge "
                                             "reference 1 (line through (0, 0, 20) mm along (1, 0, 0)) matches no "
                                             "edge of the body\n"));
    CHECK_THAT(broken.out, EndsWith("Result: invalid (2 errors)\n"));
}

TEST_CASE("info and validate describe holes", "[cli][hole]") {
    TempDir dir;
    test::HoleVariants model;
    const auto path = dir.path() / "block.bcad";
    REQUIRE(io::saveDocument(model.doc, path).has_value());

    const auto info = runCli({"info", arg(path)});
    CHECK(info.exitCode == ExitCode::Success);
    CHECK_THAT(info.out, ContainsSubstring("\n  object:9   hole     Drill   target Pad, simple through hole, diameter "
                                           "diameter, centre (hole_x, hole_y) on plane through (0, 0, 20) mm facing "
                                           "(0, 0, 1)\n"
                                           "  object:10  hole     Pocket  target Drill, counterbore blind hole 12 mm "
                                           "deep, diameter 6 mm, counterbore 10 mm x 4 mm deep, centre (20 mm, 25 mm) "
                                           "on plane through (0, 0, 20) mm facing (0, 0, 1)\n"
                                           "  object:11  hole     Sink    target Pocket, countersink through hole, "
                                           "diameter 6 mm, countersink 12 mm at 90 deg, centre (80 mm, 25 mm) on "
                                           "plane through (0, 0, 20) mm facing (0, 0, 1)\n"));

    // V = 100 x 50 x 20 - 500 pi - 172 pi - 216 pi = 97210.266 mm^3.
    const auto validate = runCli({"validate", arg(path)});
    CHECK(validate.exitCode == ExitCode::Success);
    CHECK_THAT(validate.out, ContainsSubstring("geometry              ok, 1 result body\n"
                                               "Result bodies (1):\n"
                                               "  Sink (object:11): 1 solid, volume 97210.266 mm^3, area "));
    CHECK_THAT(validate.out, EndsWith("bounds (0, 0, 0) to (100, 50, 20) mm\nResult: valid\n"));

    // A hole whose face has moved makes the document invalid, with the reason.
    REQUIRE(model.doc.setParameterValue(model.height, 30_mm).has_value());
    REQUIRE(io::saveDocument(model.doc, path).has_value());
    const auto broken = runCli({"validate", arg(path)});
    CHECK(broken.exitCode == ExitCode::Failure);
    CHECK_THAT(broken.out, ContainsSubstring("    error: Drill (object:9) failed to regenerate: Drill: hole: the "
                                             "placement face (plane through (0, 0, 20) mm facing (0, 0, 1)) matches no "
                                             "face of the body\n"));
    CHECK_THAT(broken.out, EndsWith("Result: invalid (3 errors)\n"));
}

TEST_CASE("info and validate describe linear patterns", "[cli][pattern]") {
    TempDir dir;
    test::HoleRowModel model;
    const auto path = dir.path() / "holes.bcad";
    REQUIRE(io::saveDocument(model.doc, path).has_value());

    const auto info = runCli({"info", arg(path)});
    CHECK(info.exitCode == ExitCode::Success);
    CHECK_THAT(info.out, ContainsSubstring("\n  object:10  linear_pattern  Holes  source Drill, count x pitch along "
                                           "(1, 0, 0)\n"));

    // V = 120 x 50 x 20 - 5 pi 5^2 20 = 112146.018 mm^3.
    const auto validate = runCli({"validate", arg(path)});
    CHECK(validate.exitCode == ExitCode::Success);
    CHECK_THAT(validate.out, ContainsSubstring("geometry              ok, 1 result body\n"
                                               "Result bodies (1):\n"
                                               "  Holes (object:10): 1 solid, volume 112146.018 mm^3, area "));
    CHECK_THAT(validate.out, EndsWith("bounds (0, 0, 0) to (120, 50, 20) mm\nResult: valid\n"));

    // A sixth hole would not fit: the document is invalid, with the reason.
    REQUIRE(model.doc.setParameterValue(model.count, 6.0, kUnitless).has_value());
    REQUIRE(io::saveDocument(model.doc, path).has_value());
    const auto broken = runCli({"validate", arg(path)});
    CHECK(broken.exitCode == ExitCode::Failure);
    CHECK_THAT(broken.out, ContainsSubstring("    error: Holes (object:10) failed to regenerate: Holes: linear pattern: "
                                             "instance 5 at (100, 0, 0) mm: hole: the hole does not fit on its face"));
    CHECK_THAT(broken.out, EndsWith("Result: invalid (1 error)\n"));
}

TEST_CASE("Exports of a document without bodies fail", "[cli][export]") {
    TempDir dir;
    const auto input = dir.path() / "empty.bcad";
    REQUIRE(runCli({"new", arg(input)}).exitCode == ExitCode::Success);
    for (const std::string command : {"export-step", "export-stl"}) {
        CAPTURE(command);
        const auto output = dir.path() / "out";
        const auto result = runCli({command, arg(input), arg(output)});
        CHECK(result.exitCode == ExitCode::Failure);
        CHECK_THAT(result.err, ContainsSubstring("document 'empty' has no bodies to export"));
        CHECK_FALSE(std::filesystem::exists(output));
    }
}

TEST_CASE("File names may contain any Unicode characters", "[cli]") {
    TempDir dir;
    const auto path = dir.path() / std::filesystem::path(u8"Plåt ✓.bcad");
    const auto created = runCli({"new", arg(path)});
    CHECK(created.exitCode == ExitCode::Success);
    CHECK(std::filesystem::exists(path));
    const auto info = runCli({"info", arg(path)});
    CHECK(info.exitCode == ExitCode::Success);
    CHECK_THAT(info.out, StartsWith("Document: Pl\xC3\xA5t \xE2\x9C\x93\n"));
}
