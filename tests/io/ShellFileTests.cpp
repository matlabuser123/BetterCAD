#include "features/FeatureTestSupport.hpp"
#include "support/ShellModels.hpp"
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
using bettercad::test::ConcaveShellModel;
using bettercad::test::readFile;
using bettercad::test::readStepFile;
using bettercad::test::requireReport;
using bettercad::test::ShelledBlockModel;
using bettercad::test::TempDir;
using bettercad::test::TurnedShellModel;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;

// P12-FEAT-003: shell features in the native format.

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
void checkRoundTrip(const std::vector<ObjectId> Model::*shells) {
    TempDir dir;
    const auto path = dir.path() / "shells.bcad";
    Model original;
    Regenerator before;
    regenerate(before, original.doc);
    REQUIRE(io::saveDocument(original.doc, path).has_value());
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    CHECK(equivalent(original.doc, *loaded));
    Regenerator after;
    regenerate(after, *loaded);
    for (const ObjectId id : original.*shells) {
        CAPTURE(id);
        const auto* shell = loaded->template findObjectAs<ShellFeature>(id);
        REQUIRE(shell != nullptr);
        CHECK(shell->definition() == original.doc.template findObjectAs<ShellFeature>(id)->definition());
        CHECK(volumeBits(after, id) == volumeBits(before, id));
    }
    CHECK(io::documentToJson(*loaded).value() == readFile(path));
}

struct BlockShells : ShelledBlockModel {
    std::vector<ObjectId> shells{cup, tube, casing};
};
struct TurnedShells : TurnedShellModel {
    std::vector<ObjectId> shells{canCup, canCase, shaftBore, shaftSleeve};
};
struct ConcaveShells : ConcaveShellModel {
    std::vector<ObjectId> shells{ellCup, ellCase, drillCup, drillCase, roundCup, roundCase};
};

} // namespace

TEST_CASE("ShellFile_SaveLoad_RegeneratesTheSameBodies", "[io][shell][p12][acceptance]") {
    checkRoundTrip<BlockShells>(&BlockShells::shells);
    checkRoundTrip<TurnedShells>(&TurnedShells::shells);
    checkRoundTrip<ConcaveShells>(&ConcaveShells::shells);
}

TEST_CASE("ShellFile_DefinitionsAreWrittenAsJson", "[io][shell][p12]") {
    const ShelledBlockModel m;
    const std::string text = io::documentToJson(m.doc).value();
    CHECK_THAT(text, ContainsSubstring("\"type\": \"shell\",\n      \"name\": \"Tube\",\n      \"data\": {\n"
                                       "        \"target\": 4,\n        \"open_faces\": [\n          {\n"
                                       "            \"feature\": 4,\n            \"face\": {\n"
                                       "              \"role\": \"end_cap\"\n            }\n          },\n"
                                       "          {\n            \"feature\": 4,\n            \"face\": {\n"
                                       "              \"role\": \"start_cap\"\n            }\n          }\n"
                                       "        ],\n        \"thickness\": 0.0,\n"
                                       "        \"thickness_parameter\": 2,\n        \"side\": \"inward\"\n      }"));
    CHECK_THAT(text, ContainsSubstring("\"thickness_parameter\": 2,\n        \"side\": \"outward\"\n"));
    // A side face names its entity.
    const TurnedShellModel turned;
    CHECK_THAT(io::documentToJson(turned.doc).value(),
               ContainsSubstring(std::format("\"open_faces\": [\n          {{\n            \"feature\": 7,\n"
                                             "            \"face\": {{\n              \"role\": \"side\",\n"
                                             "              \"entity\": {}\n            }}\n          }}\n"
                                             "        ],",
                                             turned.shaftLines[4].value())));
}

TEST_CASE("ShellFile_MalformedDefinitionsAreRejectedWithThePath", "[io][shell][p12]") {
    const ShelledBlockModel m;
    const std::string good = io::documentToJson(m.doc).value();
    // Objects: BlockSketch [0], Block [1], Cup [2], Tube [3], Casing [4].
    const std::string cup = "\"target\": 4,\n        \"open_faces\": [\n          {\n            \"feature\": 4,\n"
                            "            \"face\": {\n              \"role\": \"end_cap\"\n            }\n"
                            "          }\n        ],";
    CHECK_THAT(loadError(good, "\"side\": \"inward\"", "\"side\": \"inside\""),
               ContainsSubstring("objects[2].data.side: unknown value 'inside'"));
    CHECK_THAT(loadError(good, "\"side\": \"inward\"", "\"side\": \"inward\", \"depth\": 1"),
               ContainsSubstring("objects[2].data.depth: unknown field"));
    CHECK_THAT(loadError(good, cup, "\"target\": 4, \"open_faces\": [],"),
               ContainsSubstring("objects[2].data: a shell needs one or more open faces (a closed hollow is not "
                                 "built)"));
    CHECK_THAT(loadError(good, cup, "\"target\": 4,"),
               ContainsSubstring("objects[2].data.open_faces: missing required field"));
    CHECK_THAT(loadError(good, cup, "\"target\": 4, \"open_faces\": {},"),
               ContainsSubstring("objects[2].data.open_faces: expected an array"));
    CHECK_THAT(loadError(good, cup, "\"target\": 4, \"open_faces\": [{\"feature\": 4}],"),
               ContainsSubstring("objects[2].data.open_faces[0].face: missing required field"));
    CHECK_THAT(loadError(good, cup, "\"target\": 4, \"open_faces\": [{\"face\": {\"role\": \"end_cap\"}}],"),
               ContainsSubstring("objects[2].data.open_faces[0].feature: missing required field"));
    CHECK_THAT(loadError(good, cup,
                         "\"target\": 4, \"open_faces\": [{\"feature\": 4, \"face\": {\"role\": \"top\"}}],"),
               ContainsSubstring("objects[2].data.open_faces[0].face.role: unknown value 'top'"));
    CHECK_THAT(loadError(good, cup,
                         "\"target\": 4, \"open_faces\": [{\"feature\": 4, \"face\": {\"role\": \"side\"}}],"),
               ContainsSubstring("objects[2].data.open_faces[0]: a side face is named by a valid profile entity"));
    CHECK_THAT(loadError(good, cup,
                         "\"target\": 4, \"open_faces\": [{\"feature\": 4, \"face\": {\"role\": \"end_cap\"}, "
                         "\"name\": 1}],"),
               ContainsSubstring("objects[2].data.open_faces[0].name: unknown field"));
    CHECK_THAT(loadError(good, cup,
                         "\"target\": 4, \"open_faces\": [{\"feature\": 0, \"face\": {\"role\": \"end_cap\"}}],"),
               ContainsSubstring("objects[2].data: open face 1 must name a valid feature"));
    CHECK_THAT(loadError(good, cup,
                         "\"target\": 4, \"open_faces\": [{\"feature\": 4, \"face\": {\"role\": \"end_cap\"}}, "
                         "{\"feature\": 4, \"face\": {\"role\": \"end_cap\"}}],"),
               ContainsSubstring("objects[2].data: open face 2 repeats an earlier open face"));
    CHECK_THAT(loadError(good, "\"target\": 4,\n        \"open_faces\"", "\"target\": 0,\n        \"open_faces\""),
               ContainsSubstring("objects[2].data: a shell needs a target feature"));
    CHECK_THAT(loadError(good, "\"thickness\": 0.0,\n        \"thickness_parameter\": 2,\n        \"side\": \"inward\"",
                         "\"thickness\": 0.0,\n        \"side\": \"inward\""),
               ContainsSubstring("objects[2].data: the shell thickness must be positive and finite, got 0 mm"));
    CHECK_THAT(loadError(good, "\"thickness\": 0.0,", "\"thickness\": \"thin\","),
               ContainsSubstring("objects[2].data.thickness: expected a number"));
}

TEST_CASE("ShellFile_ExampleFileMatchesTheBuilder", "[io][shell][example][p12]") {
    ShelledBlockModel m;
    Regenerator regenerator;
    regenerate(regenerator, m.doc);
    const auto text = io::documentToJson(m.doc);
    REQUIRE(text.has_value());
    CHECK(*text == readFile(kExamples / "shelled_block.bcad"));
}

TEST_CASE("ShellFile_ExportedShellsReadBackFromStep", "[io][shell][step][p12]") {
    TempDir dir;
    const auto path = dir.path() / "shelled_block.step";
    ShelledBlockModel m;
    Regenerator regenerator;
    regenerate(regenerator, m.doc);
    const auto summary = io::exportStep(m.doc, path);
    REQUIRE(summary.has_value());
    CHECK(summary->bodies.size() == 3);
    const auto contents = readStepFile(path);
    REQUIRE(contents.has_value());
    CHECK(contents->valid);
    CHECK(contents->solids == 3);
    // STEP stores decimal text: within 1e-9, as for the other exports.
    const double total = ShelledBlockModel::cupShape(40.0, 5.0).volume() +
                         ShelledBlockModel::tubeShape(40.0, 5.0).volume() +
                         ShelledBlockModel::casingShape(40.0, 5.0).volume();
    CHECK_THAT(contents->volumeMm3, WithinRel(total, 1e-9));
    CHECK_THAT(contents->minMm[0], WithinRel(-5.0, 1e-9));
    CHECK_THAT(contents->maxMm[0], WithinRel(105.0, 1e-9));
    CHECK_THAT(contents->maxMm[2], WithinRel(40.0, 1e-9));
}
