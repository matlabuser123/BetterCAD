#include "cli/CliRunner.hpp"
#include "support/RibModels.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/io/DocumentFile.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <string>

using namespace bettercad;
using bettercad::cli::ExitCode;
using bettercad::test::cliPath;
using bettercad::test::RibbedBracketModel;
using bettercad::test::RibProfilesModel;
using bettercad::test::runCliCommand;
using bettercad::test::TempDir;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::EndsWith;

// P12-FEAT-005: the CLI describes ribs and validates models built with them.

namespace {

std::filesystem::path save(const TempDir& dir, const Document& doc, const std::string& name) {
    const auto path = dir.path() / name;
    REQUIRE(io::saveDocument(doc, path).has_value());
    return path;
}

} // namespace

TEST_CASE("RibCli_InfoDescribesRibs", "[cli][rib][p12]") {
    TempDir dir;
    const RibbedBracketModel m;
    const auto result = runCliCommand({"info", cliPath(save(dir, m.doc, "rib.bcad"))});
    CHECK(result.exitCode == ExitCode::Success);
    CHECK(result.err.empty());
    CHECK_THAT(result.out, ContainsSubstring("  object:11  rib          Rib          target Bracket, profile RibSketch "
                                             "(1 edge), thickness thickness, symmetric, left side\n"));
    const RibProfilesModel profiles;
    const auto described = runCliCommand({"info", cliPath(save(dir, profiles.doc, "profiles.bcad"))});
    CHECK(described.exitCode == ExitCode::Success);
    CHECK_THAT(described.out, ContainsSubstring("ArcRib        target Bracket, profile ArcSketch (1 edge), thickness "
                                                "4 mm, symmetric, right side\n"));
    CHECK_THAT(described.out, ContainsSubstring("AlongRib      target Bracket, profile ChainSketch (2 edges), "
                                                "thickness 4 mm, along normal, left side\n"));
}

TEST_CASE("RibCli_ValidateReportsTheRib", "[cli][rib][p12]") {
    TempDir dir;
    RibbedBracketModel m;
    SECTION("a valid model") {
        const auto result = runCliCommand({"validate", cliPath(save(dir, m.doc, "rib.bcad"))});
        CHECK(result.exitCode == ExitCode::Success);
        CHECK_THAT(result.out, ContainsSubstring("  feature regeneration  ok, 7 objects regenerated\n"
                                                 "  geometry              ok, 1 result body\n"));
        CHECK_THAT(result.out, ContainsSubstring("Rib (object:11): 1 solid, volume 53800.000 mm^3"));
        CHECK_THAT(result.out, ContainsSubstring("bounds (0, 0, 0) to (80, 60, 40) mm\n"));
        CHECK_THAT(result.out, EndsWith("Result: valid\n"));
    }
    SECTION("a rib whose side is open") {
        auto definition = m.doc.findObjectAs<features::RibFeature>(m.rib)->definition();
        definition.flipped = true;
        REQUIRE(m.doc.modifyObject<features::RibFeature>(m.rib, [&](features::RibFeature& rib) {
                        return rib.setDefinition(definition);
                    }).has_value());
        const auto result = runCliCommand({"validate", cliPath(save(dir, m.doc, "open.bcad"))});
        CHECK(result.exitCode == ExitCode::Failure);
        CHECK_THAT(result.out, ContainsSubstring("Rib: rib: the side the rib fills is not closed off by the body"));
        CHECK_THAT(result.out, ContainsSubstring("Result: invalid"));
    }
}
