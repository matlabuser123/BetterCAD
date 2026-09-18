#include "Cli.hpp"
#include "support/ConfigurationModels.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/io/DocumentFile.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace bettercad;
using bettercad::cli::ExitCode;
using bettercad::test::BoxFamilyModel;
using bettercad::test::TempDir;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::EndsWith;

// P12-PARAM-002 through the same public CLI as everything else: the
// configurations a document has, which one is in force, and the parameter
// values that follow.

namespace {

struct CliResult {
    ExitCode exitCode;
    std::string out;
    std::string err;
};

CliResult runCli(std::initializer_list<std::string_view> args) {
    const std::vector<std::string_view> argv(args.begin(), args.end());
    std::ostringstream out;
    std::ostringstream err;
    const ExitCode code = bettercad::cli::run(argv, out, err);
    return {code, out.str(), err.str()};
}

std::string arg(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

std::filesystem::path saveFamily(const TempDir& dir, BoxFamilyModel& model) {
    const auto path = dir.path() / "family.bcad";
    REQUIRE(io::saveDocument(model.doc, path).has_value());
    return path;
}

} // namespace

TEST_CASE("info lists a document's configurations", "[cli][configurations][p12][acceptance]") {
    TempDir dir;
    BoxFamilyModel model;
    const auto path = saveFamily(dir, model);

    const auto info = runCli({"info", arg(path)});
    CHECK(info.exitCode == ExitCode::Success);
    // Each configuration, how much it overrides, and which is in force.
    // Nothing is active here, so no row is marked.
    CHECK_THAT(info.out, ContainsSubstring("\nConfigurations (3):\n"
                                           "  Small   1 override\n"
                                           "  Medium  1 override\n"
                                           "  Large   1 override\n"));
    // The base configuration's values.
    CHECK_THAT(info.out, ContainsSubstring("\nParameters (3):\n"
                                           "  width   100 mm\n"
                                           "  height  50 mm  (expression: width / 2)\n"
                                           "  depth   25 mm  (expression: width / 4)\n"));
}

TEST_CASE("info marks the active configuration", "[cli][configurations][p12]") {
    TempDir dir;
    BoxFamilyModel model;
    model.activate(model.large);
    const auto path = saveFamily(dir, model);

    const auto info = runCli({"info", arg(path)});
    CHECK(info.exitCode == ExitCode::Success);
    CHECK_THAT(info.out, ContainsSubstring("\nConfigurations (3):\n"
                                           "  Small   1 override\n"
                                           "  Medium  1 override\n"
                                           "  Large   1 override  active\n"));
}

TEST_CASE("info reports the values a chosen configuration gives", "[cli][configurations][p12][acceptance]") {
    // --configuration evaluates the equations under that configuration, so
    // the values printed are the ones the model would be built from: at
    // Large, width = 160 (overridden) and height = 80, depth = 40 (derived).
    TempDir dir;
    BoxFamilyModel model;
    const auto path = saveFamily(dir, model);

    const auto large = runCli({"info", arg(path), "--configuration", "Large"});
    CHECK(large.exitCode == ExitCode::Success);
    CHECK_THAT(large.out, ContainsSubstring("\nParameters (3):\n"
                                            "  width   160 mm  (configuration; base 100)\n"
                                            "  height  80 mm  (expression: width / 2)\n"
                                            "  depth   40 mm  (expression: width / 4)\n"));
    CHECK_THAT(large.out, ContainsSubstring("  Large   1 override  active\n"));

    const auto small = runCli({"info", arg(path), "--configuration", "Small"});
    CHECK(small.exitCode == ExitCode::Success);
    CHECK_THAT(small.out, ContainsSubstring("  width   40 mm  (configuration; base 100)\n"
                                            "  height  20 mm  (expression: width / 2)\n"
                                            "  depth   10 mm  (expression: width / 4)\n"));

    // The file is untouched: choosing a configuration is a way of looking at
    // the document, not an edit to it.
    const auto again = runCli({"info", arg(path)});
    CHECK_THAT(again.out, ContainsSubstring("  width   100 mm\n"));
}

TEST_CASE("validate checks a chosen configuration", "[cli][configurations][p12][acceptance]") {
    TempDir dir;
    BoxFamilyModel model;
    const auto path = saveFamily(dir, model);

    // 160^3 / 8 = 512000 mm^3, computed by hand.
    const auto large = runCli({"validate", arg(path), "--configuration", "Large"});
    CHECK(large.exitCode == ExitCode::Success);
    CHECK_THAT(large.out, ContainsSubstring("  Box (object:5): 1 solid, volume 512000.000 mm^3, area "));
    CHECK_THAT(large.out, EndsWith("bounds (0, 0, 0) to (160, 80, 40) mm\nResult: valid\n"));

    // 40^3 / 8 = 8000 mm^3.
    const auto small = runCli({"validate", arg(path), "--configuration", "Small"});
    CHECK(small.exitCode == ExitCode::Success);
    CHECK_THAT(small.out, ContainsSubstring("  Box (object:5): 1 solid, volume 8000.000 mm^3, area "));
    CHECK_THAT(small.out, EndsWith("bounds (0, 0, 0) to (40, 20, 10) mm\nResult: valid\n"));
}

TEST_CASE("a configuration that does not exist is reported", "[cli][configurations][p12]") {
    TempDir dir;
    BoxFamilyModel model;
    const auto path = saveFamily(dir, model);

    for (const std::string_view command : {"info", "validate"}) {
        const auto result = command == "info"
                                ? runCli({"info", arg(path), "--configuration", "Enormous"})
                                : runCli({"validate", arg(path), "--configuration", "Enormous"});
        INFO("command " << command);
        CHECK(result.exitCode == ExitCode::Failure);
        CHECK_THAT(result.err,
                   ContainsSubstring("no configuration named 'Enormous' in this document"));
    }
    // The option needs a value, and is rejected as a usage error without one.
    const auto missing = runCli({"info", arg(path), "--configuration"});
    CHECK(missing.exitCode == ExitCode::UsageError);
    CHECK_THAT(missing.err, ContainsSubstring("option '--configuration' needs a value"));
    CHECK_THAT(missing.err, ContainsSubstring("info <file.bcad> [--configuration <name>]"));
}

TEST_CASE("a document with no configurations prints no configurations section",
          "[cli][configurations][p12][regression]") {
    // Documents from before P12-PARAM-002 must look exactly as they did.
    TempDir dir;
    Document plain{"Plain"};
    REQUIRE(plain.createParameter("width", Length::fromSi(0.1), units::mm).has_value());
    const auto path = dir.path() / "plain.bcad";
    REQUIRE(io::saveDocument(plain, path).has_value());

    const auto info = runCli({"info", arg(path)});
    CHECK(info.exitCode == ExitCode::Success);
    CHECK_THAT(info.out, !ContainsSubstring("Configurations"));
    CHECK_THAT(info.out, ContainsSubstring("\nParameters (1):\n  width  100 mm\n"));
}
