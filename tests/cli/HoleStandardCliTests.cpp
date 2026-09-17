#include "cli/CliRunner.hpp"
#include "support/HoleStandardModels.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/io/DocumentFile.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <format>
#include <string>

using namespace bettercad;
using namespace bettercad::literals;
using bettercad::cli::ExitCode;
using bettercad::test::cliPath;
using bettercad::test::runCliCommand;
using bettercad::test::TappedPlateModel;
using bettercad::test::TempDir;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::EndsWith;

// P12-HOLE-001: the CLI describes a hole by the standards it follows, not by
// the dimensions they give it.

namespace {

std::filesystem::path save(const TempDir& dir, const Document& doc, const std::string& name) {
    const auto path = dir.path() / name;
    REQUIRE(io::saveDocument(doc, path).has_value());
    return path;
}

} // namespace

TEST_CASE("HoleStandardsCli_InfoDescribesThreadsAndStandardSizes", "[cli][hole][standards][p12]") {
    TempDir dir;
    const TappedPlateModel m;
    const auto result = runCliCommand({"info", cliPath(save(dir, m.doc, "plate.bcad"))});
    CHECK(result.exitCode == ExitCode::Success);
    CHECK(result.err.empty());
    const std::string face = "on plane through (0, 0, 12) mm facing (0, 0, 1)\n";
    CHECK_THAT(result.out, ContainsSubstring("  object:5  hole     Tapped       target Plate, simple through hole, "
                                             "thread M8-6H full length, centre (15 mm, 15 mm) " +
                                             face));
    // A driven thread length shows its parameter's name.
    CHECK_THAT(result.out, ContainsSubstring("  object:6  hole     BlindTapped  target Tapped, simple blind hole "
                                             "10 mm deep, thread M6-6H thread_length long, centre (15 mm, 35 mm) " +
                                             face));
    CHECK_THAT(result.out, ContainsSubstring("  object:7  hole     Seat         target BlindTapped, counterbore "
                                             "through hole, clearance for M8 (medium) H13, counterbore 15 mm x 5 mm "
                                             "deep, centre (40 mm, 25 mm) " +
                                             face));
    CHECK_THAT(result.out, ContainsSubstring("  object:8  hole     Boss         target Seat, spotface through hole, "
                                             "thread M10-6H full length, spotface 20 mm x 1 mm deep, centre (65 mm, "
                                             "32 mm) " +
                                             face));
    CHECK_THAT(result.out, ContainsSubstring("  object:9  hole     Reamed       target Boss, simple blind hole 8 mm "
                                             "deep, diameter bore H7, centre (65 mm, 8 mm) " +
                                             face));
}

TEST_CASE("HoleStandardsCli_ValidateBuildsThePlate", "[cli][hole][standards][p12]") {
    TempDir dir;
    TappedPlateModel m;
    SECTION("a valid model") {
        const auto result = runCliCommand({"validate", cliPath(save(dir, m.doc, "plate.bcad"))});
        CHECK(result.exitCode == ExitCode::Success);
        CHECK_THAT(result.out, ContainsSubstring("  feature regeneration  ok, 7 objects regenerated\n"
                                                 "  geometry              ok, 1 result body\n"));
        CHECK_THAT(result.out, ContainsSubstring(std::format("Reamed (object:9): 1 solid, volume {:.3f} mm^3",
                                                             TappedPlateModel::expectedVolume())));
        CHECK_THAT(result.out, EndsWith("Result: valid\n"));
    }
    SECTION("a thread longer than its blind hole") {
        REQUIRE(m.doc.setParameterValue(m.threadLength, 20_mm).has_value());
        const auto result = runCliCommand({"validate", cliPath(save(dir, m.doc, "long.bcad"))});
        CHECK(result.exitCode == ExitCode::Failure);
        CHECK_THAT(result.out, ContainsSubstring("BlindTapped: hole: the thread (20 mm long) must not be longer than "
                                                 "the blind hole (10 mm deep)"));
        CHECK_THAT(result.out, ContainsSubstring("Result: invalid"));
    }
}
