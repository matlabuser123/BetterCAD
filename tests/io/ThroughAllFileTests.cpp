#include "features/FeatureTestSupport.hpp"
#include "support/TestFiles.hpp"
#include "support/ThroughCutModels.hpp"
#include "support/occt/StepReadBack.hpp"

#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/io/ModelExport.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <bit>
#include <cstdint>
#include <filesystem>
#include <numbers>
#include <string>
#include <string_view>

using namespace bettercad;
using namespace bettercad::features;
using bettercad::test::readFile;
using bettercad::test::readStepFile;
using bettercad::test::requireReport;
using bettercad::test::TempDir;
using bettercad::test::ThroughSlabModel;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;

// P12-FEAT-001: through-all extrudes in the native format. The termination
// is written; no depth is.

namespace {

const std::filesystem::path kExamples{BETTERCAD_EXAMPLE_MODELS_DIR};

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

double volumeOf(const Regenerator& regenerator, ObjectId id) {
    const auto props = regenerator.body(id)->massProperties();
    REQUIRE(props.has_value());
    return props->volume.in(units::mm3);
}

void regenerate(Regenerator& regenerator, Document& doc) {
    const RegenerationReport report = requireReport(regenerator, doc);
    INFO((report.errors.empty() ? std::string{} : report.errors.begin()->second.message));
    REQUIRE(report.succeeded());
}

std::string loadError(const std::string& text, std::string_view from, std::string_view to) {
    std::string edited = text;
    const auto pos = edited.find(from);
    REQUIRE(pos != std::string::npos);
    edited.replace(pos, from.size(), to);
    const auto loaded = io::documentFromJson(edited);
    REQUIRE_FALSE(loaded.has_value());
    return loaded.error().message;
}

} // namespace

TEST_CASE("ThroughAllFile_SaveLoad_RegeneratesTheSameBodies", "[io][extrude][p12][acceptance]") {
    TempDir dir;
    const auto path = dir.path() / "through_slab.bcad";
    ThroughSlabModel original;
    Regenerator before;
    regenerate(before, original.doc);
    REQUIRE(io::saveDocument(original.doc, path).has_value());

    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    CHECK(equivalent(original.doc, *loaded));
    const ExtrudeDefinition cut = loaded->findObjectAs<ExtrudeFeature>(original.cut)->definition();
    CHECK(cut.termination == ExtrudeTermination::ThroughAll);
    CHECK(cut.direction == ExtrudeDirection::Reversed);
    CHECK(cut.depth == Length{});
    CHECK_FALSE(cut.depthParameter.has_value());
    CHECK(loaded->findObjectAs<ExtrudeFeature>(original.slab)->definition().termination ==
          ExtrudeTermination::Blind);

    Regenerator after;
    regenerate(after, *loaded);
    for (const ObjectId id : {original.slab, original.cut, original.slant, original.peg}) {
        CHECK(bits(volumeOf(after, id)) == bits(volumeOf(before, id)));
    }
    CHECK(io::documentToJson(*loaded).value() == readFile(path));
}

TEST_CASE("ThroughAllFile_TheTerminationIsWrittenAndNoDepth", "[io][extrude][p12]") {
    const ThroughSlabModel m;
    const std::string text = io::documentToJson(m.doc).value();
    CHECK_THAT(text, ContainsSubstring("\"name\": \"Cut\",\n      \"data\": {\n        \"profile\": 4,\n"
                                       "        \"termination\": \"through_all\",\n"
                                       "        \"direction\": \"reversed\",\n"
                                       "        \"operation\": \"cut\",\n"
                                       "        \"target\": 3\n      }"));
    CHECK_THAT(text, ContainsSubstring("\"termination\": \"through_all\",\n        \"direction\": \"symmetric\""));
    // Blind extrudes are written as before: a depth, no termination.
    CHECK_THAT(text, ContainsSubstring("\"name\": \"Slab\",\n      \"data\": {\n        \"profile\": 2,\n"
                                       "        \"depth\": 0.0,\n        \"depth_parameter\": 1,\n"));
    CHECK(text.find("\"termination\": \"blind\"") == std::string::npos);
    // A file may still say so explicitly.
    std::string explicitBlind = text;
    const std::string pegData = "\"profile\": 8,\n        \"depth\"";
    const auto pos = explicitBlind.find(pegData);
    REQUIRE(pos != std::string::npos);
    explicitBlind.replace(pos, pegData.size(), "\"profile\": 8,\n        \"termination\": \"blind\",\n        \"depth\"");
    const auto loaded = io::documentFromJson(explicitBlind);
    REQUIRE(loaded.has_value());
    CHECK(equivalent(m.doc, *loaded));
}

TEST_CASE("ThroughAllFile_MalformedTerminationsAreRejectedWithThePath", "[io][extrude][p12]") {
    const ThroughSlabModel m;
    const std::string good = io::documentToJson(m.doc).value();
    const std::string termination = "\"termination\": \"through_all\"";
    CHECK_THAT(loadError(good, termination, "\"termination\": \"up_to_next\""),
               ContainsSubstring("objects[3].data.termination: unknown value 'up_to_next'"));
    CHECK_THAT(loadError(good, termination, "\"termination\": 1"),
               ContainsSubstring("objects[3].data.termination: expected a string"));
    CHECK_THAT(loadError(good, termination, termination + ", \"depth\": 0.01"),
               ContainsSubstring("objects[3].data.depth: a through-all extrude has no depth"));
    CHECK_THAT(loadError(good, termination, termination + ", \"depth_parameter\": 1"),
               ContainsSubstring("objects[3].data.depth_parameter: a through-all extrude has no depth"));
    CHECK_THAT(loadError(good, "\"operation\": \"cut\",\n        \"target\": 3",
                         "\"operation\": \"join\",\n        \"target\": 3"),
               ContainsSubstring("objects[3].data: a through-all extrude must be a cut, got join"));
    CHECK_THAT(loadError(good, termination, "\"termination\": \"blind\""),
               ContainsSubstring("objects[3].data.depth: missing required field"));
}

TEST_CASE("ThroughAllFile_ExampleFileMatchesTheBuilder", "[io][extrude][example][p12]") {
    ThroughSlabModel m;
    Regenerator regenerator;
    regenerate(regenerator, m.doc);
    const auto text = io::documentToJson(m.doc);
    REQUIRE(text.has_value());
    CHECK(*text == readFile(kExamples / "through_slab.bcad"));
}

TEST_CASE("ThroughAllFile_ExportedSlabReadsBackFromStep", "[io][extrude][step][p12]") {
    TempDir dir;
    const auto path = dir.path() / "through_slab.step";
    ThroughSlabModel m;
    Regenerator regenerator;
    regenerate(regenerator, m.doc);
    const auto summary = io::exportStep(m.doc, path);
    REQUIRE(summary.has_value());
    CHECK(summary->bodies.size() == 2); // Slant and Peg
    const auto contents = readStepFile(path);
    REQUIRE(contents.has_value());
    CHECK(contents->valid);
    CHECK(contents->solids == 2);
    // STEP stores decimal text: within 1e-9, as for the other exports.
    CHECK_THAT(contents->volumeMm3,
               WithinRel(ThroughSlabModel::volume(20.0) + std::numbers::pi * 9.0 * 5.0, 1e-9));
    CHECK_THAT(contents->maxMm[0], WithinRel(100.0, 1e-9));
    CHECK_THAT(contents->maxMm[2], WithinRel(20.0, 1e-9));
}
