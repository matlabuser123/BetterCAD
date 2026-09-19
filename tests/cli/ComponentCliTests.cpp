#include "cli/CliRunner.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/assembly/Components.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/Uuid.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/ObjectReference.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <utility>

using namespace bettercad;
using namespace bettercad::literals;
using bettercad::cli::ExitCode;
using bettercad::test::addRectangle;
using bettercad::test::cliPath;
using bettercad::test::require;
using bettercad::test::runCliCommand;
using bettercad::test::TempDir;
using Catch::Matchers::ContainsSubstring;

// P13-COMP-001: `info` is the headless way to see what a document holds, so
// it must name the part each component places rather than leave the new
// object kind with a blank description.

namespace {

/// A document holding one part: a 40 x 30 x 10 mm block named "Block".
struct PartDocument {
    Document document{"Assembly"};
    ObjectId sketch{};
    ObjectId part{};
};

PartDocument makePart() {
    PartDocument p;
    auto sketch = std::make_unique<sketch::Sketch>("BlockSketch", Frame3D::xy());
    addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
    p.sketch = require(p.document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Block", {.profile = SketchId::fromValue(p.sketch.value()), .depth = 10_mm});
    REQUIRE(extrude.has_value());
    p.part = require(p.document.addObject(std::move(*extrude)));
    return p;
}

std::filesystem::path save(const TempDir& dir, const Document& document, const std::string& name) {
    const auto path = dir.path() / name;
    REQUIRE(io::saveDocument(document, path).has_value());
    return path;
}

} // namespace

TEST_CASE("ComponentCli_InfoNamesThePartEachComponentPlaces", "[cli][assembly][component][p13]") {
    TempDir dir;
    PartDocument p = makePart();
    REQUIRE(assembly::createComponent(p.document, "Block1", {.part = p.part}).has_value());
    REQUIRE(assembly::createComponent(p.document, "Block2", {.part = p.part, .suppressed = true}).has_value());

    const auto result = runCliCommand({"info", cliPath(save(dir, p.document, "assembly.bcad"))});

    CHECK(result.exitCode == ExitCode::Success);
    CHECK(result.err.empty());
    // Both components name the one part they place, by ID and by name, and
    // the suppressed one says so. The part is listed once, as itself.
    CHECK_THAT(result.out, ContainsSubstring("component  Block1       places object:2 (Block)\n"));
    CHECK_THAT(result.out, ContainsSubstring("component  Block2       places object:2 (Block), suppressed\n"));
    CHECK_THAT(result.out, ContainsSubstring("Objects (4):\n"));
}

TEST_CASE("ComponentCli_InfoReportsAComponentWhosePartIsGone", "[cli][assembly][component][p13]") {
    // A document can be saved with the part removed -- nothing validates
    // references on save or load -- so info must say the reference is broken
    // rather than print a blank or invent a name.
    TempDir dir;
    PartDocument p = makePart();
    REQUIRE(assembly::createComponent(p.document, "Block1", {.part = p.part}).has_value());
    REQUIRE(p.document.removeObject(p.part).has_value());

    const auto result = runCliCommand({"info", cliPath(save(dir, p.document, "broken.bcad"))});

    CHECK(result.exitCode == ExitCode::Success);
    CHECK(result.err.empty());
    CHECK_THAT(result.out, ContainsSubstring("places object:2 (missing)\n"));
}

TEST_CASE("ComponentCli_InfoReportsAPartInAnotherDocumentAsUnresolved", "[cli][assembly][component][p13]") {
    // P13-REF-001. `info` reads one file and supplies no resolver, so a part
    // in another document is reported by its identity and said to be
    // unresolved -- which is the state, not a failure. The locator is shown
    // as the hint it is, never as the thing that identifies the target.
    TempDir dir;
    PartDocument p = makePart();
    PartDocument other = makePart();
    const std::string uuid = other.document.id().value().toString();
    REQUIRE(assembly::createComponent(p.document, "Away",
                                      {.part = {other.document.id(), other.part, "parts/other.bcad"}})
                .has_value());

    const auto result = runCliCommand({"info", cliPath(save(dir, p.document, "external.bcad"))});

    CHECK(result.exitCode == ExitCode::Success);
    CHECK(result.err.empty());
    CHECK_THAT(result.out, ContainsSubstring(std::format("places object:2 of document {} (unresolved), "
                                                         "hint parts/other.bcad\n",
                                                         uuid)));
}
