#include "cli/CliRunner.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/PatternModels.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/features/CircularPatternFeature.hpp>
#include <bettercad/features/LinearPatternFeature.hpp>
#include <bettercad/io/DocumentFile.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <string>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using bettercad::cli::ExitCode;
using bettercad::test::BoltCircleModel;
using bettercad::test::cliPath;
using bettercad::test::CubeRowModel;
using bettercad::test::runCliCommand;
using bettercad::test::TempDir;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::EndsWith;

// P12-PATTERN-001: the CLI says how a pattern spreads its instances, which
// of them are suppressed, and what a pattern of a pattern repeats.

namespace {

std::filesystem::path save(const TempDir& dir, const Document& doc, const std::string& name) {
    const auto path = dir.path() / name;
    REQUIRE(io::saveDocument(doc, path).has_value());
    return path;
}

} // namespace

TEST_CASE("PatternCli_InfoDescribesDistributionAndSymmetry", "[cli][pattern][p12]") {
    TempDir dir;
    CubeRowModel m;
    LinearPatternDefinition d = m.definitionOf(m.row);
    d.first.countParameter.reset();
    d.first.spacingParameter.reset();
    d.first.count = 5;
    d.first.spacing = 80_mm;

    SECTION("a plain row reads as it always did") {
        m.setDefinition(m.row, d);
        const auto result = runCliCommand({"info", cliPath(save(dir, m.doc, "row.bcad"))});
        CHECK(result.exitCode == ExitCode::Success);
        CHECK_THAT(result.out, ContainsSubstring("source Cube, 5 x 80 mm along (1, 0, 0)\n"));
    }
    SECTION("a total length is named for what it is") {
        d.first.distribution = PatternDistribution::TotalLength;
        m.setDefinition(m.row, d);
        const auto result = runCliCommand({"info", cliPath(save(dir, m.doc, "row.bcad"))});
        CHECK(result.exitCode == ExitCode::Success);
        CHECK_THAT(result.out, ContainsSubstring("source Cube, 5 over 80 mm along (1, 0, 0)\n"));
    }
    SECTION("a symmetric row says so") {
        d.first.symmetric = true;
        m.setDefinition(m.row, d);
        const auto result = runCliCommand({"info", cliPath(save(dir, m.doc, "row.bcad"))});
        CHECK(result.exitCode == ExitCode::Success);
        CHECK_THAT(result.out, ContainsSubstring("source Cube, 5 x 80 mm symmetric along (1, 0, 0)\n"));
    }
    SECTION("suppressed instances are listed by index") {
        d.suppressed = {1, 3};
        m.setDefinition(m.row, d);
        const auto result = runCliCommand({"info", cliPath(save(dir, m.doc, "row.bcad"))});
        CHECK(result.exitCode == ExitCode::Success);
        CHECK_THAT(result.out, ContainsSubstring("source Cube, 5 x 80 mm along (1, 0, 0), suppressed 1, 3\n"));
    }
    SECTION("a grid describes both directions") {
        d.first.distribution = PatternDistribution::TotalLength;
        d.second = PatternDirection{.direction = {0.0, 1.0, 0.0}, .count = 3, .spacing = 30_mm, .symmetric = true};
        d.suppressed = {4};
        m.setDefinition(m.row, d);
        const auto result = runCliCommand({"info", cliPath(save(dir, m.doc, "row.bcad"))});
        CHECK(result.exitCode == ExitCode::Success);
        CHECK_THAT(result.out, ContainsSubstring("source Cube, 5 over 80 mm along (1, 0, 0) by 3 x 30 mm symmetric "
                                                 "along (0, 1, 0), suppressed 4\n"));
    }
}

TEST_CASE("PatternCli_InfoDescribesCircularSymmetryAndSuppression", "[cli][pattern][circular][p12]") {
    TempDir dir;
    BoltCircleModel m;
    CircularPatternDefinition d = m.definitionOf(m.bolts);
    d.countParameter.reset();
    d.count = 5;
    d.spacing = CircularSpacing::AngleStep;
    d.angle = 30_deg;
    d.symmetric = true;
    d.suppressed = {2, 4};
    m.setDefinition(m.bolts, d);
    const auto result = runCliCommand({"info", cliPath(save(dir, m.doc, "bolts.bcad"))});
    CHECK(result.exitCode == ExitCode::Success);
    CHECK(result.err.empty());
    CHECK_THAT(result.out, ContainsSubstring("source Bolt, 5 around the axis through (0, 0, 0) mm along (0, 0, 1), "
                                             "30 deg apart, symmetric, suppressed 2, 4\n"));
}

TEST_CASE("PatternCli_ValidateReportsAPatternOfAPattern", "[cli][pattern][nesting][p12]") {
    TempDir dir;
    CubeRowModel m;
    LinearPatternDefinition row = m.definitionOf(m.row);
    row.first.countParameter.reset();
    row.first.spacingParameter.reset();
    row.first.count = 3;
    row.first.spacing = 20_mm;
    m.setDefinition(m.row, row);
    m.addPattern("Grid", {.source = CubeRowModel::featureId(m.row),
                          .first = {.direction = {0.0, 1.0, 0.0}, .count = 4, .spacing = 30_mm}});

    const auto path = save(dir, m.doc, "grid.bcad");
    const auto described = runCliCommand({"info", cliPath(path)});
    CHECK(described.exitCode == ExitCode::Success);
    CHECK_THAT(described.out,
               ContainsSubstring("linear_pattern  Grid        source Row, 4 x 30 mm along (0, 1, 0)\n"));

    const auto result = runCliCommand({"validate", cliPath(path)});
    CHECK(result.exitCode == ExitCode::Success);
    CHECK(result.err.empty());
    // Twelve cubes of 1000 mm^3, one result body.
    CHECK_THAT(result.out, ContainsSubstring("  geometry              ok, 1 result body\n"));
    CHECK_THAT(result.out, ContainsSubstring("Grid (object:7): 12 solids, volume 12000.000 mm^3"));
    CHECK_THAT(result.out, ContainsSubstring("bounds (0, 0, 0) to (50, 100, 10) mm\n"));
    CHECK_THAT(result.out, EndsWith("Result: valid\n"));
}

TEST_CASE("PatternCli_ValidateReportsASuppressedInstanceThatIsNotThere", "[cli][pattern][p12]") {
    TempDir dir;
    CubeRowModel m;
    LinearPatternDefinition d = m.definitionOf(m.row);
    d.first.countParameter.reset();
    d.first.spacingParameter.reset();
    d.first.count = 4;
    d.first.spacing = 20_mm;
    d.suppressed = {9};
    m.setDefinition(m.row, d);
    const auto result = runCliCommand({"validate", cliPath(save(dir, m.doc, "row.bcad"))});
    CHECK(result.exitCode == ExitCode::Failure);
    CHECK_THAT(result.out, ContainsSubstring("a linear pattern of 4 instances has no instance 9 to suppress"));
}
