#include "features/FeatureTestSupport.hpp"
#include "support/BodyOpsModels.hpp"
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
#include <string>
#include <string_view>

using namespace bettercad;
using namespace bettercad::features;
using bettercad::test::BodyOpsModel;
using bettercad::test::readFile;
using bettercad::test::readStepFile;
using bettercad::test::requireReport;
using bettercad::test::TempDir;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;

// P12-FEAT-002: split and combine features in the native format.

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

} // namespace

TEST_CASE("BodyOpsFile_SaveLoad_RegeneratesTheSameBodies", "[io][split][combine][p12][acceptance]") {
    TempDir dir;
    const auto path = dir.path() / "body_ops.bcad";
    BodyOpsModel original;
    Regenerator before;
    regenerate(before, original.doc);
    REQUIRE(io::saveDocument(original.doc, path).has_value());
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    CHECK(equivalent(original.doc, *loaded));
    const auto* split = loaded->findObjectAs<SplitFeature>(original.halves);
    REQUIRE(split != nullptr);
    CHECK(split->definition() == original.doc.findObjectAs<SplitFeature>(original.halves)->definition());
    const auto* combine = loaded->findObjectAs<CombineFeature>(original.joined);
    REQUIRE(combine != nullptr);
    CHECK(combine->definition().tools ==
          std::vector<FeatureId>{BodyOpsModel::featureOf(original.b), BodyOpsModel::featureOf(original.c)});
    Regenerator after;
    regenerate(after, *loaded);
    for (const ObjectId id : {original.joined, original.halves}) {
        CHECK(volumeBits(after, id) == volumeBits(before, id));
    }
    CHECK(io::documentToJson(*loaded).value() == readFile(path));
}

TEST_CASE("BodyOpsFile_DefinitionsAreWrittenAsJson", "[io][split][combine][p12]") {
    const BodyOpsModel m;
    const std::string text = io::documentToJson(m.doc).value();
    CHECK_THAT(text, ContainsSubstring("\"type\": \"combine\",\n      \"name\": \"Joined\",\n      \"data\": {\n"
                                       "        \"target\": 4,\n        \"tools\": [\n          6,\n          8\n"
                                       "        ],\n        \"operation\": \"join\"\n      }"));
    CHECK_THAT(text, ContainsSubstring("\"type\": \"split\",\n      \"name\": \"Halves\",\n      \"data\": {\n"
                                       "        \"target\": 9,\n        \"plane\": {\n          \"object\": 10,\n"
                                       "          \"plane\": \"xy\"\n        },\n        \"keep\": \"both\"\n      }"));
}

TEST_CASE("BodyOpsFile_MalformedDefinitionsAreRejectedWithThePath", "[io][split][combine][p12]") {
    const BodyOpsModel m;
    const std::string good = io::documentToJson(m.doc).value();
    CHECK_THAT(loadError(good, "\"keep\": \"both\"", "\"keep\": \"middle\""),
               ContainsSubstring("objects[8].data.keep: unknown value 'middle'"));
    CHECK_THAT(loadError(good, "\"keep\": \"both\"", "\"keep\": \"both\", \"side\": 1"),
               ContainsSubstring("objects[8].data.side: unknown field"));
    CHECK_THAT(loadError(good, "\"object\": 10,", "\"object\": 0,"),
               ContainsSubstring("objects[8].data: the split plane: a plane reference must name a valid object"));
    CHECK_THAT(loadError(good, "\"target\": 9,", "\"target\": 0,"),
               ContainsSubstring("objects[8].data: a split needs a target feature"));
    CHECK_THAT(loadError(good, "\"operation\": \"join\"\n", "\"operation\": \"new_body\"\n"),
               ContainsSubstring("objects[6].data.operation: unknown value 'new_body'"));
    CHECK_THAT(loadError(good, "\"tools\": [\n          6,\n          8\n        ]", "\"tools\": []"),
               ContainsSubstring("objects[6].data: a combine needs one or more tool features"));
    CHECK_THAT(loadError(good, "\"tools\": [\n          6,\n          8\n        ]", "\"tools\": [6, 6]"),
               ContainsSubstring("objects[6].data: tool 2 repeats an earlier tool"));
    CHECK_THAT(loadError(good, "\"tools\": [\n          6,\n          8\n        ]", "\"tools\": [6, \"C\"]"),
               ContainsSubstring("objects[6].data.tools[1]: expected an ID"));
    CHECK_THAT(loadError(good, "\"tools\": [\n          6,\n          8\n        ]", "\"tools\": 6"),
               ContainsSubstring("objects[6].data.tools: expected an array"));
}

TEST_CASE("BodyOpsFile_ExampleFileMatchesTheBuilder", "[io][split][combine][example][p12]") {
    BodyOpsModel m;
    Regenerator regenerator;
    regenerate(regenerator, m.doc);
    const auto text = io::documentToJson(m.doc);
    REQUIRE(text.has_value());
    CHECK(*text == readFile(kExamples / "body_ops.bcad"));
}

TEST_CASE("BodyOpsFile_ExportedHalvesReadBackFromStep", "[io][split][step][p12]") {
    TempDir dir;
    const auto path = dir.path() / "body_ops.step";
    BodyOpsModel m;
    Regenerator regenerator;
    regenerate(regenerator, m.doc);
    const auto summary = io::exportStep(m.doc, path);
    REQUIRE(summary.has_value());
    CHECK(summary->bodies.size() == 1); // Halves: two solids
    const auto contents = readStepFile(path);
    REQUIRE(contents.has_value());
    CHECK(contents->valid);
    CHECK(contents->solids == 2);
    // STEP stores decimal text: within 1e-9, as for the other exports.
    CHECK_THAT(contents->volumeMm3, WithinRel(BodyOpsModel::joinedVolume(20.0), 1e-9));
    CHECK_THAT(contents->maxMm[0], WithinRel(150.0, 1e-9));
    CHECK_THAT(contents->maxMm[2], WithinRel(40.0, 1e-9));
}
