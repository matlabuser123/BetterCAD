#include "features/FeatureTestSupport.hpp"
#include "support/RibModels.hpp"
#include "support/TestFiles.hpp"
#include "support/occt/StepReadBack.hpp"

#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/io/ModelExport.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <bit>
#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using bettercad::test::readFile;
using bettercad::test::readStepFile;
using bettercad::test::requireReport;
using bettercad::test::RibbedBracketModel;
using bettercad::test::RibProfilesModel;
using bettercad::test::TempDir;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;

// P12-FEAT-005: rib features in the native format.

namespace {

const std::filesystem::path kExamples{BETTERCAD_EXAMPLE_MODELS_DIR};

void regenerate(Regenerator& regenerator, Document& doc) {
    const RegenerationReport report = requireReport(regenerator, doc);
    INFO((report.errors.empty() ? std::string{} : report.errors.begin()->second.message));
    REQUIRE(report.succeeded());
}

std::uint64_t volumeBits(const Regenerator& regenerator, ObjectId id) {
    const auto props = regenerator.body(id)->massProperties();
    REQUIRE(props.has_value());
    return std::bit_cast<std::uint64_t>(props->volume.si());
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

/// Save, load and regenerate: the same definitions and the same bodies, bit
/// for bit, and the loaded document writes the same file.
template <typename Model>
void checkRoundTrip(const std::vector<ObjectId> Model::*ribs) {
    TempDir dir;
    const auto path = dir.path() / "ribs.bcad";
    Model original;
    Regenerator before;
    regenerate(before, original.doc);
    REQUIRE(io::saveDocument(original.doc, path).has_value());
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    CHECK(equivalent(original.doc, *loaded));
    Regenerator after;
    regenerate(after, *loaded);
    for (const ObjectId id : original.*ribs) {
        CAPTURE(id);
        const auto* rib = loaded->template findObjectAs<RibFeature>(id);
        REQUIRE(rib != nullptr);
        CHECK(rib->definition() == original.doc.template findObjectAs<RibFeature>(id)->definition());
        CHECK(volumeBits(after, id) == volumeBits(before, id));
    }
    CHECK(io::documentToJson(*loaded).value() == readFile(path));
}

struct BracketRibs : RibbedBracketModel {
    std::vector<ObjectId> ribs{rib};
};
struct ProfileRibs : RibProfilesModel {
    std::vector<ObjectId> ribs{arcRib, chainRib, splineRib, shortRib, alongRib};
};

} // namespace

TEST_CASE("RibFile_SaveLoad_RegeneratesTheSameBodies", "[io][rib][p12][acceptance]") {
    checkRoundTrip<BracketRibs>(&BracketRibs::ribs);
    checkRoundTrip<ProfileRibs>(&ProfileRibs::ribs);
}

TEST_CASE("RibFile_DefinitionsAreWrittenAsJson", "[io][rib][p12]") {
    const RibbedBracketModel m;
    const std::string text = io::documentToJson(m.doc).value();
    CHECK_THAT(text, ContainsSubstring(std::format(
                         "\"type\": \"rib\",\n      \"name\": \"Rib\",\n      \"data\": {{\n"
                         "        \"target\": 8,\n        \"profile\": 10,\n        \"edges\": [\n          {}\n"
                         "        ],\n        \"thickness\": 0.0,\n        \"thickness_parameter\": 4,\n"
                         "        \"placement\": \"symmetric\",\n        \"flipped\": false\n      }}",
                         m.ribLines[0].value())));
    const RibProfilesModel profiles;
    const std::string other = io::documentToJson(profiles.doc).value();
    CHECK_THAT(other, ContainsSubstring("\"thickness\": 0.004,\n        \"placement\": \"symmetric\",\n"
                                        "        \"flipped\": true\n"));
    CHECK_THAT(other, ContainsSubstring("\"placement\": \"along_normal\",\n        \"flipped\": false\n"));
}

TEST_CASE("RibFile_MalformedDefinitionsAreRejectedWithThePath", "[io][rib][p12]") {
    const RibbedBracketModel m;
    const std::string good = io::documentToJson(m.doc).value();
    // Objects: FloorSketch [0], Floor [1], WallSketch [2], Bracket [3], Mid [4],
    // RibSketch [5], Rib [6].
    const std::string edges = std::format("\"edges\": [\n          {}\n        ],", m.ribLines[0].value());
    CHECK_THAT(loadError(good, "\"placement\": \"symmetric\"", "\"placement\": \"both\""),
               ContainsSubstring("objects[6].data.placement: unknown value 'both'"));
    CHECK_THAT(loadError(good, "\"flipped\": false", "\"flipped\": 0"),
               ContainsSubstring("objects[6].data.flipped: expected true or false"));
    CHECK_THAT(loadError(good, "\"flipped\": false", "\"flipped\": false, \"side\": \"left\""),
               ContainsSubstring("objects[6].data.side: unknown field"));
    CHECK_THAT(loadError(good, edges, "\"edges\": [],"),
               ContainsSubstring("objects[6].data: a rib needs one or more profile edges"));
    CHECK_THAT(loadError(good, edges, "\"edges\": 5,"),
               ContainsSubstring("objects[6].data.edges: expected an array"));
    CHECK_THAT(loadError(good, edges, "\"edges\": [\"line\"],"),
               ContainsSubstring("objects[6].data.edges[0]: expected an ID"));
    CHECK_THAT(loadError(good, edges, std::format("\"edges\": [{0}, {0}],", m.ribLines[0].value())),
               ContainsSubstring("objects[6].data: profile edge 2 repeats an earlier edge"));
    CHECK_THAT(loadError(good, "\"target\": 8,\n        \"profile\": 10,", "\"target\": 8,\n        \"profile\": 0,"),
               ContainsSubstring("objects[6].data: a rib needs a profile sketch"));
    CHECK_THAT(loadError(good, "\"target\": 8,\n        \"profile\": 10,", "\"target\": 0,\n        \"profile\": 10,"),
               ContainsSubstring("objects[6].data: a rib needs a target feature"));
    CHECK_THAT(loadError(good, "\"target\": 8,\n        \"profile\": 10,", "\"target\": 8,"),
               ContainsSubstring("objects[6].data.profile: missing required field"));
    CHECK_THAT(loadError(good, "\"thickness\": 0.0,\n        \"thickness_parameter\": 4,", "\"thickness\": 0.0,"),
               ContainsSubstring("objects[6].data: the rib thickness must be positive and finite, got 0 mm"));
}

TEST_CASE("RibFile_ExampleFileMatchesTheBuilder", "[io][rib][example][p12]") {
    RibbedBracketModel m;
    Regenerator regenerator;
    regenerate(regenerator, m.doc);
    const auto text = io::documentToJson(m.doc);
    REQUIRE(text.has_value());
    CHECK(*text == readFile(kExamples / "ribbed_bracket.bcad"));
}

TEST_CASE("RibFile_ExportedRibsReadBackFromStep", "[io][rib][step][p12]") {
    TempDir dir;
    const auto path = dir.path() / "ribbed_bracket.step";
    RibbedBracketModel m;
    Regenerator regenerator;
    regenerate(regenerator, m.doc);
    const auto summary = io::exportStep(m.doc, path);
    REQUIRE(summary.has_value());
    CHECK(summary->bodies.size() == 1);
    const auto contents = readStepFile(path);
    REQUIRE(contents.has_value());
    CHECK(contents->valid);
    CHECK(contents->solids == 1);
    // STEP stores decimal text: within 1e-9, as for the other exports.
    CHECK_THAT(contents->volumeMm3, WithinRel(RibbedBracketModel::shape(10.0, 10.0, 18.0, 22.0).volume(), 1e-9));
    CHECK_THAT(contents->maxMm[0], WithinRel(80.0, 1e-9));
    CHECK_THAT(contents->maxMm[1], WithinRel(60.0, 1e-9));
}
