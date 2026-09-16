#include "cli/CliRunner.hpp"
#include "support/DatumModels.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/features/Datums.hpp>
#include <bettercad/io/DocumentFile.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <string>

using namespace bettercad;
using namespace bettercad::literals;
using bettercad::cli::ExitCode;
using bettercad::test::cliPath;
using bettercad::test::DatumBlockModel;
using bettercad::test::runCliCommand;
using bettercad::test::TempDir;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::EndsWith;

// P12-DATUM-001: the CLI describes reference geometry and validates models
// built on it.

namespace {

std::filesystem::path save(const TempDir& dir, const Document& doc, const std::string& name) {
    const auto path = dir.path() / name;
    REQUIRE(io::saveDocument(doc, path).has_value());
    return path;
}

} // namespace

TEST_CASE("DatumCli_InfoDescribesDatumsAndAttachments", "[cli][datum][p12]") {
    TempDir dir;
    DatumBlockModel m;
    // A fixed plane, a fixed axis and a fixed coordinate system too.
    auto fixedPlane = features::DatumPlane::create(
        "Level", {.kind = features::DatumPlaneKind::Fixed,
                  .frame = *Frame3D::create(Point3D{0_mm, 0_mm, 5_mm}, Direction3D::unitZ(), Direction3D::unitX())});
    REQUIRE(fixedPlane.has_value());
    REQUIRE(m.doc.addObject(std::move(*fixedPlane)).has_value());
    auto fixedAxis = features::DatumAxis::create("Upright", {});
    REQUIRE(fixedAxis.has_value());
    REQUIRE(m.doc.addObject(std::move(*fixedAxis)).has_value());
    auto fixedSystem =
        features::CoordinateSystem::create("World", {.kind = features::CoordinateSystemKind::Fixed});
    REQUIRE(fixedSystem.has_value());
    REQUIRE(m.doc.addObject(std::move(*fixedSystem)).has_value());

    const auto result = runCliCommand({"info", cliPath(save(dir, m.doc, "datums.bcad"))});
    CHECK(result.exitCode == ExitCode::Success);
    CHECK(result.err.empty());
    CHECK_THAT(result.out, ContainsSubstring("datum_plane        TopPlane     offset from the model's xy by height\n"));
    CHECK_THAT(result.out, ContainsSubstring("sketch             BossSketch   2 entities, 2 constraints, driven by "
                                             "boss_radius, on TopPlane\n"));
    CHECK_THAT(result.out, ContainsSubstring("datum_plane        RearPlane    offset from the model's xz by -30 mm\n"));
    CHECK_THAT(result.out, ContainsSubstring("datum_axis         Spindle      where Middle and RearPlane meet\n"));
    CHECK_THAT(result.out, ContainsSubstring("circular_pattern   Pattern      source Boss, 3 around Spindle, "
                                             "full circle\n"));
    CHECK_THAT(result.out, ContainsSubstring("coordinate_system  Station      from the model's: moved (150 mm, 0 mm, "
                                             "0 mm), turned (0 deg, 0 deg, 90 deg)\n"));
    CHECK_THAT(result.out, ContainsSubstring("sketch             PegSketch    2 entities, 2 constraints, on Station's "
                                             "xy\n"));
    CHECK_THAT(result.out, ContainsSubstring("datum_plane        TiltPlane    turned from the model's xy about the "
                                             "model's y by tilt\n"));
    CHECK_THAT(result.out, ContainsSubstring("Level        fixed through (0, 0, 5) mm facing (0, 0, 1)\n"));
    CHECK_THAT(result.out, ContainsSubstring("Upright      fixed through (0, 0, 0) mm along (0, 0, 1)\n"));
    CHECK_THAT(result.out, ContainsSubstring("World        fixed at (0, 0, 0) mm, x along (1, 0, 0), z along (0, 0, 1)\n"));
}

TEST_CASE("DatumCli_ValidateReportsDatumProblems", "[cli][datum][p12]") {
    TempDir dir;
    DatumBlockModel m;

    SECTION("a valid model") {
        const auto result = runCliCommand({"validate", cliPath(save(dir, m.doc, "datums.bcad"))});
        CHECK(result.exitCode == ExitCode::Success);
        CHECK_THAT(result.out, ContainsSubstring("  feature regeneration  ok, 15 objects regenerated\n"
                                                 "  geometry              ok, 3 result bodies\n"));
        CHECK_THAT(result.out, ContainsSubstring("Peg (object:16): 1 solid, volume 785.398 mm^3"));
        CHECK_THAT(result.out, ContainsSubstring("bounds (145, 5, 0) to (155, 15, 10) mm\n"));
        CHECK_THAT(result.out, EndsWith("Result: valid\n"));
    }
    SECTION("parallel planes and a wrong kind") {
        REQUIRE(m.doc.modifyObject<features::DatumPlane>(m.rearPlane, [](features::DatumPlane& p) {
                         return p.setDefinition({.kind = features::DatumPlaneKind::Offset,
                                                 .base = {.object = {}, .plane = PrincipalPlane::YZ},
                                                 .offset = 70_mm});
                     }).has_value());
        REQUIRE(m.doc.modifyObject<sketch::Sketch>(m.finSketch, [&](sketch::Sketch& s) {
                         return s.setAttachment(PlaneReference{m.peg});
                     }).has_value());
        const auto result = runCliCommand({"validate", cliPath(save(dir, m.doc, "broken.bcad"))});
        CHECK(result.exitCode == ExitCode::Failure);
        CHECK_THAT(result.out, ContainsSubstring("FinSketch (object:18): the attachment is Peg (object:16), which is "
                                                 "an extrude, not a datum plane or a coordinate system"));
        CHECK_THAT(result.out, ContainsSubstring("Spindle (object:12): its planes are parallel and do not meet"));
        CHECK_THAT(result.out, ContainsSubstring("Result: invalid"));
    }
}
