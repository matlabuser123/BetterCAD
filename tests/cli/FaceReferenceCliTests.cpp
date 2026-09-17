#include "cli/CliRunner.hpp"
#include "support/FaceModels.hpp"
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
using bettercad::test::FaceBlockModel;
using bettercad::test::runCliCommand;
using bettercad::test::TempDir;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::EndsWith;

// P12-STREF-001: the CLI describes sketches on faces and validates models
// built on them.

namespace {

std::filesystem::path save(const TempDir& dir, const Document& doc, const std::string& name) {
    const auto path = dir.path() / name;
    REQUIRE(io::saveDocument(doc, path).has_value());
    return path;
}

} // namespace

TEST_CASE("FaceReferenceCli_InfoDescribesFaceAttachments", "[cli][references][p12]") {
    TempDir dir;
    FaceBlockModel m;
    const auto result = runCliCommand({"info", cliPath(save(dir, m.doc, "faces.bcad"))});
    CHECK(result.exitCode == ExitCode::Success);
    CHECK(result.err.empty());
    CHECK_THAT(result.out, ContainsSubstring("BossSketch  2 entities, 2 constraints, on the end cap of Base\n"));
    CHECK_THAT(result.out, ContainsSubstring(std::format("SideSketch  8 entities, 4 constraints, on the side from {} "
                                                         "of Base\n",
                                                         m.frontLine)));
}

TEST_CASE("FaceReferenceCli_ValidateReportsFaceReferenceProblems", "[cli][references][p12]") {
    TempDir dir;
    FaceBlockModel m;

    SECTION("a valid model") {
        const auto result = runCliCommand({"validate", cliPath(save(dir, m.doc, "faces.bcad"))});
        CHECK(result.exitCode == ExitCode::Success);
        CHECK_THAT(result.out, ContainsSubstring("  feature regeneration  ok, 8 objects regenerated\n"
                                                 "  geometry              ok, 1 result body\n"));
        CHECK_THAT(result.out, ContainsSubstring("Pocket (object:10): 1 solid, volume 226010.619 mm^3"));
        CHECK_THAT(result.out, ContainsSubstring("bounds (0, 0, 0) to (150, 60, 35) mm\n"));
        CHECK_THAT(result.out, EndsWith("Result: valid\n"));
    }
    SECTION("a face of a sketch, and an entity the profile does not have") {
        REQUIRE(m.doc.modifyObject<sketch::Sketch>(m.bossSketch, [&](sketch::Sketch& s) {
                         return s.setAttachment(FaceBlockModel::faceOf(m.stepSketch, FaceRole::EndCap));
                     }).has_value());
        REQUIRE(m.doc.modifyObject<sketch::Sketch>(m.sideSketch, [&](sketch::Sketch& s) {
                         return s.setAttachment(
                             FaceBlockModel::faceOf(m.step, FaceRole::Side, EntityId::fromValue(99)));
                     }).has_value());
        const auto result = runCliCommand({"validate", cliPath(save(dir, m.doc, "broken.bcad"))});
        CHECK(result.exitCode == ExitCode::Failure);
        CHECK_THAT(result.out, ContainsSubstring("BossSketch (object:7): the attachment is the end cap of StepSketch "
                                                 "(object:3): StepSketch (object:3) is a sketch, not a feature, and "
                                                 "has no faces"));
        CHECK_THAT(result.out, ContainsSubstring("SideSketch (object:9): the attachment is the side from entity:99 of "
                                                 "Step (object:4): Step (object:4): entity:99 is not an entity of its "
                                                 "profile StepSketch (object:3)"));
        CHECK_THAT(result.out, ContainsSubstring("Result: invalid"));
    }
}
