#include "cli/CliRunner.hpp"
#include "support/TestFiles.hpp"
#include "support/VariableFilletModels.hpp"

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
using bettercad::test::TaperedBlockModel;
using bettercad::test::TempDir;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::EndsWith;

// P12-FEAT-006: the CLI describes variable-radius fillets and validates
// models built with them.

namespace {

std::filesystem::path save(const TempDir& dir, const Document& doc, const std::string& name) {
    const auto path = dir.path() / name;
    REQUIRE(io::saveDocument(doc, path).has_value());
    return path;
}

} // namespace

TEST_CASE("VariableFilletCli_InfoDescribesVariableFillets", "[cli][fillet][variable][p12]") {
    TempDir dir;
    const TaperedBlockModel m;
    const auto result = runCliCommand({"info", cliPath(save(dir, m.doc, "taper.bcad"))});
    CHECK(result.exitCode == ExitCode::Success);
    CHECK(result.err.empty());
    CHECK_THAT(result.out, ContainsSubstring("  object:7  variable_fillet  Taper        target Block, 2 edges: "
                                             "low at 0, high at 1; 2 mm at 0, 4 mm at 0.3, 7 mm at 1\n"));
}

TEST_CASE("VariableFilletCli_ValidateReportsTheFillet", "[cli][fillet][variable][p12]") {
    TempDir dir;
    TaperedBlockModel m;
    SECTION("a valid model") {
        const auto result = runCliCommand({"validate", cliPath(save(dir, m.doc, "taper.bcad"))});
        CHECK(result.exitCode == ExitCode::Success);
        CHECK_THAT(result.out, ContainsSubstring("  feature regeneration  ok, 3 objects regenerated\n"
                                                 "  geometry              ok, 1 result body\n"));
        // The volume of the block less both fillets (support/VariableFilletModels.hpp).
        const double volume = TaperedBlockModel::shape(100.0, 50.0, 3.0, 8.0).volume();
        CHECK_THAT(result.out, ContainsSubstring(std::format("Taper (object:7): 1 solid, volume {:.3f} mm^3", volume)));
        CHECK_THAT(result.out, EndsWith("Result: valid\n"));
    }
    SECTION("a fillet too large for its faces") {
        REQUIRE(m.doc.setParameterValue(m.high, 25_mm).has_value());
        const auto result = runCliCommand({"validate", cliPath(save(dir, m.doc, "large.bcad"))});
        CHECK(result.exitCode == ExitCode::Failure);
        CHECK_THAT(result.out, ContainsSubstring("Taper: variable-radius fillet: edge reference 1 (line through "
                                                 "(0, 0, 20) mm along (1, 0, 0)) does not fit"));
        CHECK_THAT(result.out, ContainsSubstring("Result: invalid"));
    }
}
