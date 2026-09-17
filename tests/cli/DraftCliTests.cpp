#include "cli/CliRunner.hpp"
#include "support/DraftModels.hpp"
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
using bettercad::test::DraftedBlockModel;
using bettercad::test::runCliCommand;
using bettercad::test::TempDir;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::EndsWith;

// P12-FEAT-004: the CLI describes drafts and validates models built with
// them.

namespace {

std::filesystem::path save(const TempDir& dir, const Document& doc, const std::string& name) {
    const auto path = dir.path() / name;
    REQUIRE(io::saveDocument(doc, path).has_value());
    return path;
}

} // namespace

TEST_CASE("DraftCli_InfoDescribesDrafts", "[cli][draft][p12]") {
    TempDir dir;
    const DraftedBlockModel m;
    const auto result = runCliCommand({"info", cliPath(save(dir, m.doc, "drafts.bcad"))});
    CHECK(result.exitCode == ExitCode::Success);
    CHECK(result.err.empty());
    const std::string sides = std::format("the side from {} of Block and the side from {} of Block and the side "
                                          "from {} of Block and the side from {} of Block",
                                          m.lines[0], m.lines[1], m.lines[2], m.lines[3]);
    CHECK_THAT(result.out, ContainsSubstring(std::format("  object:7   draft        Tapered      target Block, faces "
                                                         "{}, neutral plane the model's xy, angle taper\n",
                                                         sides)));
    CHECK_THAT(result.out, ContainsSubstring(std::format("  object:8   draft        TaperedMid   target Block, faces "
                                                         "{}, neutral plane Mid, angle taper\n",
                                                         sides)));
    CHECK_THAT(result.out, ContainsSubstring(std::format("  object:9   draft        Flared       target Block, faces "
                                                         "{}, neutral plane the model's xy, angle -5 deg\n",
                                                         sides)));
    CHECK_THAT(result.out, ContainsSubstring(std::format("  object:10  draft        OnTop        target Block, faces "
                                                         "{}, neutral plane the end cap of Block, angle taper\n",
                                                         sides)));
}

TEST_CASE("DraftCli_ValidateReportsTheDrafts", "[cli][draft][p12]") {
    TempDir dir;
    DraftedBlockModel m;
    SECTION("a valid model") {
        const auto result = runCliCommand({"validate", cliPath(save(dir, m.doc, "drafts.bcad"))});
        CHECK(result.exitCode == ExitCode::Success);
        CHECK_THAT(result.out, ContainsSubstring("  feature regeneration  ok, 8 objects regenerated\n"
                                                 "  geometry              ok, 5 result bodies\n"));
        CHECK_THAT(result.out, ContainsSubstring("Tapered (object:7): 1 solid, volume 218256.066 mm^3"));
        CHECK_THAT(result.out, ContainsSubstring("TaperedMid (object:8): 1 solid, volume 240163.291 mm^3"));
        CHECK_THAT(result.out, ContainsSubstring("Flared (object:9): 1 solid, volume 263050.262 mm^3"));
        CHECK_THAT(result.out, ContainsSubstring("OnTop (object:10): 1 solid, volume 263050.262 mm^3"));
        CHECK_THAT(result.out, ContainsSubstring("OnBottom (object:11): 1 solid, volume 218256.066 mm^3"));
        CHECK_THAT(result.out, EndsWith("Result: valid\n"));
    }
    SECTION("a taper too steep for the block") {
        REQUIRE(m.doc.setParameterValue(m.taper, Angle::fromSi(0.65)).has_value());
        const auto result = runCliCommand({"validate", cliPath(save(dir, m.doc, "steep.bcad"))});
        CHECK(result.exitCode == ExitCode::Failure);
        CHECK_THAT(result.out, ContainsSubstring("Tapered: draft: the kernel cannot build the draft: kernel: an edge "
                                                 "cannot be recomputed"));
        CHECK_THAT(result.out, ContainsSubstring("Result: invalid"));
    }
}
