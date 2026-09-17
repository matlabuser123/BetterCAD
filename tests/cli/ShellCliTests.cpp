#include "cli/CliRunner.hpp"
#include "support/ShellModels.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/io/DocumentFile.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <format>
#include <string>

using namespace bettercad;
using bettercad::cli::ExitCode;
using bettercad::test::cliPath;
using bettercad::test::runCliCommand;
using bettercad::test::ShelledBlockModel;
using bettercad::test::TempDir;
using bettercad::test::TurnedShellModel;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::EndsWith;

// P12-FEAT-003: the CLI describes shells and validates models built with
// them.

namespace {

std::filesystem::path save(const TempDir& dir, const Document& doc, const std::string& name) {
    const auto path = dir.path() / name;
    REQUIRE(io::saveDocument(doc, path).has_value());
    return path;
}

} // namespace

TEST_CASE("ShellCli_InfoDescribesShells", "[cli][shell][p12]") {
    TempDir dir;
    const ShelledBlockModel m;
    const auto result = runCliCommand({"info", cliPath(save(dir, m.doc, "shells.bcad"))});
    CHECK(result.exitCode == ExitCode::Success);
    CHECK(result.err.empty());
    CHECK_THAT(result.out, ContainsSubstring("  object:5  shell    Cup          target Block, open the end cap of "
                                             "Block, thickness wall, inward\n"));
    CHECK_THAT(result.out, ContainsSubstring("  object:6  shell    Tube         target Block, open the end cap of "
                                             "Block and the start cap of Block, thickness wall, inward\n"));
    CHECK_THAT(result.out, ContainsSubstring("  object:7  shell    Casing       target Block, open the end cap of "
                                             "Block, thickness wall, outward\n"));
    // A literal thickness and a side face.
    TurnedShellModel turned;
    const ObjectId thin = turned.add(features::ShellFeature::create(
        "Thin", {.target = TurnedShellModel::featureOf(turned.shaft),
                 .openFaces = {bettercad::test::nameOf(turned.shaft, FaceRole::Side, turned.shaftLines[4])},
                 .thickness = Length::fromSi(0.0015)}));
    const auto described = runCliCommand({"info", cliPath(save(dir, turned.doc, "turned.bcad"))});
    CHECK(described.exitCode == ExitCode::Success);
    CHECK_THAT(described.out,
               ContainsSubstring(std::format("  {}  shell    Thin         target Shaft, open the side from {} of "
                                             "Shaft, thickness 1.5 mm, inward\n",
                                             thin, turned.shaftLines[4])));
}

TEST_CASE("ShellCli_ValidateReportsTheShells", "[cli][shell][p12]") {
    TempDir dir;
    ShelledBlockModel m;
    SECTION("a valid model") {
        const auto result = runCliCommand({"validate", cliPath(save(dir, m.doc, "shells.bcad"))});
        CHECK(result.exitCode == ExitCode::Success);
        CHECK_THAT(result.out, ContainsSubstring("  feature regeneration  ok, 5 objects regenerated\n"
                                                 "  geometry              ok, 3 result bodies\n"));
        CHECK_THAT(result.out, ContainsSubstring("Cup (object:5): 1 solid, volume 82500.000 mm^3"));
        CHECK_THAT(result.out, ContainsSubstring("Tube (object:6): 1 solid, volume 60000.000 mm^3"));
        CHECK_THAT(result.out, ContainsSubstring("Casing (object:7): 1 solid, volume 106500.000 mm^3"));
        CHECK_THAT(result.out, ContainsSubstring("bounds (-5, -5, -5) to (105, 65, 40) mm\n"));
        CHECK_THAT(result.out, EndsWith("Result: valid\n"));
    }
    SECTION("walls too thick for the block") {
        REQUIRE(m.doc.setParameterValue(m.wall, Length::fromSi(0.035)).has_value());
        const auto result = runCliCommand({"validate", cliPath(save(dir, m.doc, "thick.bcad"))});
        CHECK(result.exitCode == ExitCode::Failure);
        CHECK_THAT(result.out, ContainsSubstring("Cup: shell: the kernel cannot build walls 35 mm thick inward on "
                                                 "this body: its result is not a shell of the body: 5 of the 5 "
                                                 "remaining faces have no wall"));
        CHECK_THAT(result.out, ContainsSubstring("Result: invalid"));
    }
}
