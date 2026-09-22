#include "TestHelpers.hpp"
#include "cli/CliRunner.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/drawing/Sheet.hpp>
#include <bettercad/drawing/Sheets.hpp>
#include <bettercad/io/DocumentFile.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <string>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using drawing::DrawingScale;
using drawing::SheetDefinition;
using drawing::SheetFormat;
using drawing::SheetMargins;
using drawing::SheetOrientation;

// P14-SHEET-001: the CLI's description of a sheet.
//
// ADR-010 lists four dispatch sites a new object kind must reach, and records
// that this one -- the CLI's describeObject -- is the only one that fails
// SILENTLY: its fall-through returns an empty string, so `info` prints a row
// with an ID, a type and a name and nothing else. P13-COMP-001 found that
// exact miss going untested once already, which is why these exist.
namespace {

std::filesystem::path saveWithSheets(const TempDir& dir) {
    Document document{"Drawing"};
    SheetDefinition first{.format = SheetFormat::A3,
                          .orientation = SheetOrientation::Landscape,
                          .margins = SheetMargins{10_mm, 10_mm, 10_mm, 10_mm},
                          .scale = DrawingScale{1, 2}};
    SheetDefinition second = first;
    second.format = SheetFormat::A4;
    second.orientation = SheetOrientation::Portrait;
    second.scale = DrawingScale{2, 1};

    REQUIRE(drawing::createSheet(document, "Overall", first).has_value());
    REQUIRE(drawing::createSheet(document, "Detail", second).has_value());

    const auto path = dir.path() / "drawing.bcad";
    REQUIRE(io::saveDocument(document, path).has_value());
    return path;
}

} // namespace

TEST_CASE("SheetCli_InfoDescribesEverySheet", "[cli][drawing][sheet][p14]") {
    TempDir dir;
    const auto result = runCliCommand({"info", cliPath(saveWithSheets(dir))});

    CHECK(result.exitCode == cli::ExitCode::Success);
    CHECK(result.err.empty());

    // The description is not empty -- the failure this test exists to catch.
    // A3 landscape is 420 x 297; A4 portrait is 210 x 297.
    CHECK_THAT(result.out, ContainsSubstring("sheet"));
    CHECK_THAT(result.out, ContainsSubstring("Overall"));
    CHECK_THAT(result.out, ContainsSubstring("A3 landscape, 420 x 297 mm, scale 1:2, sheet 1 of 2"));
    CHECK_THAT(result.out, ContainsSubstring("Detail"));
    CHECK_THAT(result.out, ContainsSubstring("A4 portrait, 210 x 297 mm, scale 2:1, sheet 2 of 2"));
    CHECK_THAT(result.out, ContainsSubstring("Objects (2):\n"));
}

TEST_CASE("SheetCli_ValidateAcceptsADocumentOfSheets", "[cli][drawing][sheet][p14]") {
    // A sheet produces no geometry, so a document of nothing but sheets is
    // valid. It must not be reported as broken for having no bodies.
    TempDir dir;
    const auto result = runCliCommand({"validate", cliPath(saveWithSheets(dir))});
    CHECK(result.exitCode == cli::ExitCode::Success);
}
