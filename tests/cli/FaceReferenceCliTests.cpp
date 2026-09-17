#include "cli/CliRunner.hpp"
#include "support/FaceKindModels.hpp"
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
using bettercad::test::BevelledBlockModel;
using bettercad::test::cliPath;
using bettercad::test::DrilledBlockModel;
using bettercad::test::FaceBlockModel;
using bettercad::test::LoftedFrustumModel;
using bettercad::test::PostRowModel;
using bettercad::test::RevolvedRingModel;
using bettercad::test::SweptBarModel;
using bettercad::test::runCliCommand;
using bettercad::test::TempDir;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::EndsWith;

// P12-STREF-001, P12-SKETCH-003: the CLI describes sketches on faces and
// validates models built on them.

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

// --- P12-SKETCH-003: every kind of face, and copies ---------------------------------------------

namespace {

/// The line of @p out that lists the object named @p name.
std::string objectLine(const std::string& out, const std::string& name) {
    std::size_t start = 0;
    while (start < out.size()) {
        std::size_t end = out.find('\n', start);
        if (end == std::string::npos) {
            end = out.size();
        }
        const std::string line = out.substr(start, end - start);
        if (line.starts_with("  object:") && line.find(" " + name + "  ") != std::string::npos) {
            return line;
        }
        start = end + 1;
    }
    FAIL("no line for " << name);
    return {};
}

} // namespace

TEST_CASE("FaceReferenceCli_InfoDescribesFacesOfEveryKindAndCopies", "[cli][references][p12]") {
    TempDir dir;
    const auto info = [&](const Document& doc) {
        const auto result = runCliCommand({"info", cliPath(save(dir, doc, doc.name() + ".bcad"))});
        CHECK(result.exitCode == ExitCode::Success);
        CHECK(result.err.empty());
        return result.out;
    };
    SECTION("copies place sketches; they do not drive them") {
        const PostRowModel m;
        const std::string out = info(m.doc);
        CHECK_THAT(objectLine(out, "CopySketch"),
                   EndsWith(std::format("CopySketch   2 entities, 2 constraints, on the side from {} of Post, copy 2 "
                                        "of Row",
                                        m.right)));
        CHECK_THAT(objectLine(out, "ImageSketch"),
                   EndsWith(std::format("ImageSketch  2 entities, 2 constraints, on the side from {} of Post, copy 2 "
                                        "of Row, copy 1 of Flip",
                                        m.right)));
        CHECK_THAT(objectLine(out, "Gauge"),
                   EndsWith(std::format("Gauge        offset from the side from {} of Post, copy 1 of Row by 5 mm",
                                        m.right)));
    }
    SECTION("revolves, sweeps and lofts") {
        const RevolvedRingModel ring;
        const std::string rings = info(ring.doc);
        CHECK_THAT(objectLine(rings, "EndSketch"), EndsWith("2 entities, 2 constraints, on the end cap of Ring"));
        CHECK_THAT(objectLine(rings, "TopSketch"),
                   EndsWith(std::format("2 entities, 2 constraints, on the side from {} of Ring", ring.top)));
        const SweptBarModel bar;
        const std::string bars = info(bar.doc);
        CHECK_THAT(objectLine(bars, "BarProfile"), EndsWith("8 entities, 7 constraints, driven by thick, on Floor"));
        CHECK_THAT(objectLine(bars, "StartSketch"), EndsWith("2 entities, 2 constraints, on the start cap of Bar"));
        CHECK_THAT(objectLine(bars, "SideSketch"),
                   EndsWith(std::format("2 entities, 2 constraints, on the side from {} along {} of Bar", bar.right,
                                        bar.up)));
        const LoftedFrustumModel loft;
        CHECK_THAT(objectLine(info(loft.doc), "BottomSketch"),
                   EndsWith("2 entities, 2 constraints, on the start cap of Frustum"));
    }
    SECTION("holes and chamfers") {
        const DrilledBlockModel holes;
        const std::string drilled = info(holes.doc);
        CHECK_THAT(objectLine(drilled, "PinSketch"), EndsWith("2 entities, 2 constraints, on the bottom of Bore"));
        CHECK_THAT(objectLine(drilled, "CollarSketch"),
                   EndsWith("2 entities, 2 constraints, on the counterbore floor of Seat"));
        const BevelledBlockModel bevel;
        CHECK_THAT(objectLine(info(bevel.doc), "BackSketch"),
                   EndsWith("2 entities, 2 constraints, on the face of edge reference 2 of Bevel"));
    }
}

TEST_CASE("FaceReferenceCli_ValidateReportsCopiesAndRolesAFeatureDoesNotHave", "[cli][references][p12]") {
    TempDir dir;
    PostRowModel m;
    SECTION("a valid model") {
        const auto result = runCliCommand({"validate", cliPath(save(dir, m.doc, "rows.bcad"))});
        CHECK(result.exitCode == ExitCode::Success);
        CHECK_THAT(result.out, ContainsSubstring("  feature regeneration  ok, 13 objects regenerated\n"
                                                 "  geometry              ok, 4 result bodies\n"));
        CHECK_THAT(result.out, ContainsSubstring("Flip (object:8): 1 solid, volume 72000.000 mm^3"));
        CHECK_THAT(result.out, ContainsSubstring("bounds (80, 12, 17) to (84, 18, 23) mm\n"));
        CHECK_THAT(result.out, ContainsSubstring("bounds (116, 12, 17) to (120, 18, 23) mm\n"));
        CHECK_THAT(result.out, EndsWith("Result: valid\n"));
    }
    SECTION("a copy by an extrude, and a copy the pattern does not make") {
        REQUIRE(m.doc.modifyObject<sketch::Sketch>(m.copySketch, [&](sketch::Sketch& s) {
                         return s.setAttachment(PostRowModel::faceOf(m.post, m.rightOf({{m.plate, 2}})));
                     }).has_value());
        REQUIRE(m.doc.modifyObject<sketch::Sketch>(m.imageSketch, [&](sketch::Sketch& s) {
                         return s.setAttachment(PostRowModel::faceOf(m.post, m.rightOf({{m.row, 9}, {m.flip, 1}})));
                     }).has_value());
        const auto result = runCliCommand({"validate", cliPath(save(dir, m.doc, "broken.bcad"))});
        CHECK(result.exitCode == ExitCode::Failure);
        CHECK_THAT(result.out,
                   ContainsSubstring(std::format("CopySketch (object:9): the attachment is the side from {} of Post "
                                                 "(object:6), copy 2 of Plate (object:4): Plate (object:4) is an "
                                                 "extrude, which makes no copies (patterns and mirrors do)",
                                                 m.right)));
        CHECK_THAT(result.out,
                   ContainsSubstring(std::format("ImageSketch (object:11): the side from {} of Post (object:6), copy 9 "
                                                 "of Row (object:7), copy 1 of Flip (object:8) is not a face of its "
                                                 "body (the feature's operation left no such face)",
                                                 m.right)));
        CHECK_THAT(result.out, ContainsSubstring("Result: invalid"));
    }
}
