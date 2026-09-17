#include "cli/CliRunner.hpp"
#include "support/BodyOpsModels.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/io/DocumentFile.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <string>

using namespace bettercad;
using bettercad::cli::ExitCode;
using bettercad::test::BodyOpsModel;
using bettercad::test::cliPath;
using bettercad::test::runCliCommand;
using bettercad::test::TempDir;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::EndsWith;

// P12-FEAT-002: the CLI describes splits and combines and validates models
// built with them.

namespace {

std::filesystem::path save(const TempDir& dir, const Document& doc, const std::string& name) {
    const auto path = dir.path() / name;
    REQUIRE(io::saveDocument(doc, path).has_value());
    return path;
}

} // namespace

TEST_CASE("BodyOpsCli_InfoDescribesSplitsAndCombines", "[cli][split][combine][p12]") {
    TempDir dir;
    const BodyOpsModel m;
    const auto result = runCliCommand({"info", cliPath(save(dir, m.doc, "ops.bcad"))});
    CHECK(result.exitCode == ExitCode::Success);
    CHECK(result.err.empty());
    CHECK_THAT(result.out, ContainsSubstring("  object:9   combine      Joined   join A with B, C\n"));
    CHECK_THAT(result.out,
               ContainsSubstring("  object:10  datum_plane  Middle   offset from the model's yz by cut\n"));
    CHECK_THAT(result.out, ContainsSubstring("  object:11  split        Halves   target Joined, plane Middle, keep "
                                             "both\n"));
}

TEST_CASE("BodyOpsCli_ValidateReportsTheSplitBody", "[cli][split][combine][p12]") {
    TempDir dir;
    BodyOpsModel m;
    SECTION("a valid model") {
        const auto result = runCliCommand({"validate", cliPath(save(dir, m.doc, "ops.bcad"))});
        CHECK(result.exitCode == ExitCode::Success);
        CHECK_THAT(result.out, ContainsSubstring("  feature regeneration  ok, 9 objects regenerated\n"
                                                 "  geometry              ok, 1 result body\n"));
        CHECK_THAT(result.out, ContainsSubstring("Halves (object:11): 2 solids, volume 171141.593 mm^3"));
        CHECK_THAT(result.out, ContainsSubstring("bounds (0, 0, 0) to (150, 60, 40) mm\n"));
        CHECK_THAT(result.out, EndsWith("Result: valid\n"));
    }
    SECTION("a split beyond the body") {
        REQUIRE(m.doc.setParameterValue(m.cut, Length::fromSi(0.2)).has_value());
        const auto result = runCliCommand({"validate", cliPath(save(dir, m.doc, "beyond.bcad"))});
        CHECK(result.exitCode == ExitCode::Failure);
        CHECK_THAT(result.out, ContainsSubstring("Halves: split: the plane does not cross the body: all of it lies "
                                                 "behind the plane"));
        CHECK_THAT(result.out, ContainsSubstring("Result: invalid"));
    }
}
