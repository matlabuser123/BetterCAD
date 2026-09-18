#include "cli/CliRunner.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/SweepModels.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/features/SweepFeature.hpp>
#include <bettercad/io/DocumentFile.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <string>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using bettercad::cli::ExitCode;
using bettercad::test::cliPath;
using bettercad::test::GuidedBarModel;
using bettercad::test::runCliCommand;
using bettercad::test::SpatialBarModel;
using bettercad::test::TempDir;
using bettercad::test::TwistedBarModel;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::EndsWith;

// P12-SWEEP-001: the CLI says which sketches a path runs through, how far
// the section turns, and which curve carries it.

namespace {

std::filesystem::path save(const TempDir& dir, const Document& doc, const std::string& name) {
    const auto path = dir.path() / name;
    REQUIRE(io::saveDocument(doc, path).has_value());
    return path;
}

} // namespace

TEST_CASE("SpatialSweepCli_InfoNamesEveryRunOfThePath", "[cli][sweep][p12]") {
    TempDir dir;
    const SpatialBarModel m;
    const auto result = runCliCommand({"info", cliPath(save(dir, m.doc, "bar.bcad"))});
    CHECK(result.exitCode == ExitCode::Success);
    CHECK(result.err.empty());
    CHECK_THAT(result.out, ContainsSubstring("profile BarProfile, path Rise (1 edge) then Cross (1 edge) then Turn "
                                             "(1 edge), follow path, new body\n"));
}

TEST_CASE("SpatialSweepCli_InfoDescribesTheTwist", "[cli][sweep][p12]") {
    TempDir dir;
    TwistedBarModel m;
    SECTION("driven by a parameter") {
        const auto result = runCliCommand({"info", cliPath(save(dir, m.doc, "twist.bcad"))});
        CHECK(result.exitCode == ExitCode::Success);
        CHECK_THAT(result.out,
                   ContainsSubstring("profile TwistProfile, path Rise (1 edge), follow path, twisted twist, "
                                     "new body\n"));
    }
    SECTION("a literal angle") {
        SweepDefinition d = m.definition();
        d.twistParameter.reset();
        d.twist = 180_deg;
        m.setDefinition(d);
        const auto result = runCliCommand({"info", cliPath(save(dir, m.doc, "twist.bcad"))});
        CHECK(result.exitCode == ExitCode::Success);
        CHECK_THAT(result.out,
                   ContainsSubstring("profile TwistProfile, path Rise (1 edge), follow path, twisted 180 deg, "
                                     "new body\n"));
    }
    SECTION("no twist at all says nothing about one") {
        SweepDefinition d = m.definition();
        d.twistParameter.reset();
        m.setDefinition(d);
        const auto result = runCliCommand({"info", cliPath(save(dir, m.doc, "twist.bcad"))});
        CHECK(result.exitCode == ExitCode::Success);
        CHECK_THAT(result.out,
                   ContainsSubstring("profile TwistProfile, path Rise (1 edge), follow path, new body\n"));
        CHECK_THAT(result.out, !ContainsSubstring("twisted"));
    }
}

TEST_CASE("SpatialSweepCli_InfoNamesTheGuide", "[cli][sweep][p12]") {
    TempDir dir;
    const GuidedBarModel m;
    const auto result = runCliCommand({"info", cliPath(save(dir, m.doc, "guide.bcad"))});
    CHECK(result.exitCode == ExitCode::Success);
    CHECK(result.err.empty());
    CHECK_THAT(result.out, ContainsSubstring("profile GuideProfile, path Rise (1 edge), follow path, guided by Lead "
                                             "(1 edge), new body\n"));
}

TEST_CASE("SpatialSweepCli_ValidateReportsASpatialSweep", "[cli][sweep][p12]") {
    TempDir dir;
    const SpatialBarModel m;
    const auto result = runCliCommand({"validate", cliPath(save(dir, m.doc, "bar.bcad"))});
    CHECK(result.exitCode == ExitCode::Success);
    CHECK(result.err.empty());
    CHECK_THAT(result.out, ContainsSubstring("  geometry              ok, 1 result body\n"));
    CHECK_THAT(result.out, ContainsSubstring("Bar (object:5): 1 solid, volume 1920.000 mm^3"));
    CHECK_THAT(result.out, ContainsSubstring("bounds (-2, -2, 0) to (42, 40, 42) mm\n"));
    // The model's profile sketch is under-constrained, which is a warning,
    // not a fault: the sweep itself is valid.
    CHECK_THAT(result.out, EndsWith("Result: valid (1 warning)\n"));
}

TEST_CASE("SpatialSweepCli_ValidateReportsADisconnectedPath", "[cli][sweep][p12]") {
    TempDir dir;
    SpatialBarModel m;
    // Point the second run at the third's sketch, so it starts where the
    // first run does not end.
    SweepDefinition d = m.definition();
    d.path.runs[0].sketch = d.path.runs[1].sketch;
    d.path.runs[0].edges = d.path.runs[1].edges;
    m.setDefinition(d);
    const auto result = runCliCommand({"validate", cliPath(save(dir, m.doc, "broken.bcad"))});
    CHECK(result.exitCode == ExitCode::Failure);
    CHECK_THAT(result.out, ContainsSubstring("the path is not connected"));
}
