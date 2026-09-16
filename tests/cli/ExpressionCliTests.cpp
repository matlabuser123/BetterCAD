#include "cli/CliRunner.hpp"
#include "support/DrivenPlateModel.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <string>

using namespace bettercad;
using namespace bettercad::literals;
using bettercad::cli::ExitCode;
using bettercad::test::cliPath;
using bettercad::test::DrivenPlateModel;
using bettercad::test::runCliCommand;
using bettercad::test::TempDir;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::EndsWith;

// P12-PARAM-001: the CLI shows and validates parameter expressions.

namespace {

std::filesystem::path save(const TempDir& dir, const Document& doc, const std::string& name) {
    const auto path = dir.path() / name;
    REQUIRE(io::saveDocument(doc, path).has_value());
    return path;
}

} // namespace

TEST_CASE("ExpressionCli_InfoShowsDrivenParametersAndTheirValues", "[cli][expressions][p12]") {
    TempDir dir;
    DrivenPlateModel m;
    features::Regenerator regenerator;
    REQUIRE(regenerator.regenerate(m.doc).has_value());

    const auto result = runCliCommand({"info", cliPath(save(dir, m.doc, "plate.bcad"))});
    CHECK(result.exitCode == ExitCode::Success);
    CHECK(result.err.empty());
    CHECK_THAT(result.out, ContainsSubstring("Parameters (7):\n"
                                             "  hole_y         25 mm  (expression: height / 2)\n"
                                             "  height         50 mm  (expression: width / 2)\n"
                                             "  thickness      10 mm  (expression: 0.1 * width)\n"
                                             "  hole_spacing   70 mm  (expression: width - 2 * edge_distance)\n"
                                             "  width          100 mm\n"
                                             "  edge_distance  15 mm\n"
                                             "  hole_radius    5 mm\n"));
}

TEST_CASE("ExpressionCli_ValidateEvaluatesExpressionsAndReportsTheirErrors", "[cli][expressions][p12]") {
    TempDir dir;
    DrivenPlateModel m;

    SECTION("a valid model saved before evaluation") {
        const auto result = runCliCommand({"validate", cliPath(save(dir, m.doc, "plate.bcad"))});
        CHECK(result.exitCode == ExitCode::Success);
        CHECK_THAT(result.out, ContainsSubstring("  feature regeneration  ok, 2 objects regenerated\n"
                                                 "  geometry              ok, 1 result body\n"
                                                 "Result bodies (1):\n"
                                                 "  Plate (object:9): 1 solid, volume 48429.204 mm^3"));
        CHECK_THAT(result.out, ContainsSubstring("bounds (0, 0, 0) to (100, 50, 10) mm\n"));
        CHECK_THAT(result.out, EndsWith("Result: valid\n"));
    }
    SECTION("an unknown name and a dimension error") {
        REQUIRE(m.doc.setParameterExpression(m.holeY, "hieght / 2").has_value());
        REQUIRE(m.doc.setParameterExpression(m.thickness, "0.1 * width / 1 s").has_value());
        const auto result = runCliCommand({"validate", cliPath(save(dir, m.doc, "broken.bcad"))});
        CHECK(result.exitCode == ExitCode::Failure);
        CHECK_THAT(result.out,
                   ContainsSubstring("  missing references    1 error\n"
                                     "    error: hole_y (object:1): expression 'hieght / 2': unknown parameter "
                                     "'hieght' at offset 0\n"));
        CHECK_THAT(result.out, ContainsSubstring("    error: thickness (object:3) failed to evaluate: parameter "
                                                 "'thickness' = 0.1 * width / 1 s: the result has dimension "
                                                 "velocity, but the parameter has dimension length\n"));
        CHECK_THAT(result.out, ContainsSubstring("    error: PlateSketch (object:8) was not regenerated"));
        CHECK_THAT(result.out, ContainsSubstring("    error: Plate (object:9) was not regenerated"));
        CHECK_THAT(result.out, EndsWith("Result: invalid (4 errors)\n"));
    }
    SECTION("a cycle") {
        REQUIRE(m.doc.setParameterExpression(m.height, "hole_y * 2").has_value());
        const auto result = runCliCommand({"validate", cliPath(save(dir, m.doc, "cycle.bcad"))});
        CHECK(result.exitCode == ExitCode::Failure);
        CHECK_THAT(result.out, ContainsSubstring("  dependency cycles     1 error\n"
                                                 "    error: dependency cycle: hole_y (object:1), height (object:2)\n"));
    }
}

TEST_CASE("ExpressionCli_ExportFollowsTheEvaluatedValues", "[cli][expressions][p12]") {
    TempDir dir;
    DrivenPlateModel m;
    REQUIRE(m.doc.setParameterValue(m.width, 140_mm).has_value());
    const auto input = save(dir, m.doc, "plate.bcad");
    const auto output = dir.path() / "plate.stl";

    const auto result = runCliCommand({"export-stl", cliPath(input), cliPath(output)});
    CHECK(result.exitCode == ExitCode::Success);
    CHECK_THAT(result.out, ContainsSubstring("binary STL (mm), 1 body (Plate: "));
    CHECK(std::filesystem::exists(output));
}
