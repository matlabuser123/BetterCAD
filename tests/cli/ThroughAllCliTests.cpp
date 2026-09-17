#include "cli/CliRunner.hpp"
#include "support/TestFiles.hpp"
#include "support/ThroughCutModels.hpp"

#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/io/DocumentFile.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <string>

using namespace bettercad;
using bettercad::cli::ExitCode;
using bettercad::test::cliPath;
using bettercad::test::runCliCommand;
using bettercad::test::TempDir;
using bettercad::test::ThroughSlabModel;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::EndsWith;

// P12-FEAT-001: the CLI describes through-all extrudes and validates models
// built with them.

namespace {

std::filesystem::path save(const TempDir& dir, const Document& doc, const std::string& name) {
    const auto path = dir.path() / name;
    REQUIRE(io::saveDocument(doc, path).has_value());
    return path;
}

} // namespace

TEST_CASE("ThroughAllCli_InfoDescribesTheTermination", "[cli][extrude][p12]") {
    TempDir dir;
    const ThroughSlabModel m;
    const auto result = runCliCommand({"info", cliPath(save(dir, m.doc, "slab.bcad"))});
    CHECK(result.exitCode == ExitCode::Success);
    CHECK(result.err.empty());
    CHECK_THAT(result.out, ContainsSubstring("  object:3  extrude  Slab         profile SlabSketch, depth thickness, "
                                             "normal, new body\n"));
    CHECK_THAT(result.out, ContainsSubstring("  object:5  extrude  Cut          profile CutSketch, through all, "
                                             "reversed, cut Slab\n"));
    CHECK_THAT(result.out, ContainsSubstring("  object:7  extrude  Slant        profile SlantSketch, through all, "
                                             "symmetric, cut Cut\n"));
    CHECK_THAT(result.out, ContainsSubstring("  object:9  extrude  Peg          profile WallSketch, depth 5 mm, "
                                             "normal, new body\n"));
}

TEST_CASE("ThroughAllCli_ValidateReportsTheCutAndItsFailures", "[cli][extrude][p12]") {
    TempDir dir;
    ThroughSlabModel m;
    SECTION("a valid model") {
        const auto result = runCliCommand({"validate", cliPath(save(dir, m.doc, "slab.bcad"))});
        CHECK(result.exitCode == ExitCode::Success);
        CHECK_THAT(result.out, ContainsSubstring("  feature regeneration  ok, 8 objects regenerated\n"
                                                 "  geometry              ok, 2 result bodies\n"));
        CHECK_THAT(result.out, ContainsSubstring("Slant (object:7): 1 solid, volume 102164.962 mm^3"));
        CHECK_THAT(result.out, ContainsSubstring("bounds (0, 0, 0) to (100, 60, 20) mm\n"));
        CHECK_THAT(result.out, ContainsSubstring("Peg (object:9): 1 solid, volume 141.372 mm^3"));
        CHECK_THAT(result.out, ContainsSubstring("bounds (60, 27, 7) to (65, 33, 13) mm\n"));
        CHECK_THAT(result.out, EndsWith("Result: valid\n"));
    }
    SECTION("a cut along the normal from the top face removes nothing") {
        REQUIRE(m.doc.modifyObject<features::ExtrudeFeature>(m.cut, [](features::ExtrudeFeature& cut) {
                         features::ExtrudeDefinition d = cut.definition();
                         d.direction = features::ExtrudeDirection::Normal;
                         return cut.setDefinition(d);
                     }).has_value());
        const auto result = runCliCommand({"validate", cliPath(save(dir, m.doc, "broken.bcad"))});
        CHECK(result.exitCode == ExitCode::Failure);
        CHECK_THAT(result.out, ContainsSubstring("Cut: the target lies wholly behind the sketch plane, so cutting "
                                                 "through all along its normal removes nothing"));
        CHECK_THAT(result.out, ContainsSubstring("Result: invalid"));
    }
}
