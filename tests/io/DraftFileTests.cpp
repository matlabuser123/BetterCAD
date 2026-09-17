#include "features/FeatureTestSupport.hpp"
#include "support/DraftModels.hpp"
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
using bettercad::test::DraftedBlockModel;
using bettercad::test::DraftedPartsModel;
using bettercad::test::readFile;
using bettercad::test::readStepFile;
using bettercad::test::requireReport;
using bettercad::test::Shift;
using bettercad::test::TempDir;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;

// P12-FEAT-004: draft features in the native format.

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
void checkRoundTrip(const std::vector<ObjectId> Model::*drafts) {
    TempDir dir;
    const auto path = dir.path() / "drafts.bcad";
    Model original;
    Regenerator before;
    regenerate(before, original.doc);
    REQUIRE(io::saveDocument(original.doc, path).has_value());
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    CHECK(equivalent(original.doc, *loaded));
    Regenerator after;
    regenerate(after, *loaded);
    for (const ObjectId id : original.*drafts) {
        CAPTURE(id);
        const auto* draft = loaded->template findObjectAs<DraftFeature>(id);
        REQUIRE(draft != nullptr);
        CHECK(draft->definition() == original.doc.template findObjectAs<DraftFeature>(id)->definition());
        CHECK(volumeBits(after, id) == volumeBits(before, id));
    }
    CHECK(io::documentToJson(*loaded).value() == readFile(path));
}

struct BlockDrafts : DraftedBlockModel {
    std::vector<ObjectId> drafts{tapered, taperedMid, flared, onTop, onBottom};
};
struct PartDrafts : DraftedPartsModel {
    std::vector<ObjectId> drafts{canTaper, boreTaper, pocketTaper, ellTaper, padTaper, padOneSide};
};

} // namespace

TEST_CASE("DraftFile_SaveLoad_RegeneratesTheSameBodies", "[io][draft][p12][acceptance]") {
    checkRoundTrip<BlockDrafts>(&BlockDrafts::drafts);
    checkRoundTrip<PartDrafts>(&PartDrafts::drafts);
}

TEST_CASE("DraftFile_DefinitionsAreWrittenAsJson", "[io][draft][p12]") {
    const DraftedBlockModel m;
    const std::string text = io::documentToJson(m.doc).value();
    // Faces as face names; a principal plane; a driven angle.
    CHECK_THAT(text, ContainsSubstring(std::format(
                         "\"type\": \"draft\",\n      \"name\": \"Tapered\",\n      \"data\": {{\n"
                         "        \"target\": 5,\n        \"faces\": [\n          {{\n"
                         "            \"feature\": 5,\n            \"face\": {{\n"
                         "              \"role\": \"side\",\n              \"entity\": {}\n            }}\n"
                         "          }},\n",
                         m.lines[0].value())));
    CHECK_THAT(text, ContainsSubstring("        ],\n        \"neutral_plane\": {\n          \"plane\": \"xy\"\n"
                                       "        },\n        \"angle\": 0.0,\n        \"angle_parameter\": 2\n      }"));
    // A datum plane; a face; a literal angle.
    CHECK_THAT(text, ContainsSubstring("\"neutral_plane\": {\n          \"object\": 6,\n          \"plane\": \"xy\"\n"
                                       "        },"));
    CHECK_THAT(text, ContainsSubstring("\"neutral_plane\": {\n          \"object\": 5,\n          \"face\": {\n"
                                       "            \"role\": \"start_cap\"\n          }\n        },\n"
                                       "        \"angle\": -0.08726646259971647\n      }"));
}

TEST_CASE("DraftFile_MalformedDefinitionsAreRejectedWithThePath", "[io][draft][p12]") {
    const DraftedBlockModel m;
    const std::string good = io::documentToJson(m.doc).value();
    // Objects: BlockSketch [0], Block [1], Mid [2], Tapered [3], ...
    const std::string plane = "\"neutral_plane\": {\n          \"plane\": \"xy\"\n        },\n        \"angle\": 0.0,";
    CHECK_THAT(loadError(good, plane, "\"neutral_plane\": {\"plane\": \"xw\"}, \"angle\": 0.0,"),
               ContainsSubstring("objects[3].data.neutral_plane.plane: unknown value 'xw'"));
    CHECK_THAT(loadError(good, plane, "\"angle\": 0.0,"),
               ContainsSubstring("objects[3].data.neutral_plane: missing required field"));
    CHECK_THAT(loadError(good, plane, "\"neutral_plane\": {\"object\": 0, \"plane\": \"xy\"}, \"angle\": 0.0,"),
               ContainsSubstring("objects[3].data: the neutral plane: a plane reference must name a valid object"));
    CHECK_THAT(loadError(good, plane, plane + " \"pull\": 1,"),
               ContainsSubstring("objects[3].data.pull: unknown field"));
    CHECK_THAT(loadError(good, "\"angle\": 0.0,\n        \"angle_parameter\": 2\n",
                         "\"angle\": 2.0\n"),
               ContainsSubstring("objects[3].data: the draft angle must be in (-90, 90) deg, got "));
    CHECK_THAT(loadError(good, "\"angle\": 0.0,\n        \"angle_parameter\": 2\n",
                         "\"angle\": \"steep\"\n"),
               ContainsSubstring("objects[3].data.angle: expected a number"));
    // Tapered's faces array, as written.
    std::string faces = "\"faces\": [\n";
    for (std::size_t i = 0; i < m.lines.size(); ++i) {
        faces += std::format("          {{\n            \"feature\": 5,\n            \"face\": {{\n"
                             "              \"role\": \"side\",\n              \"entity\": {}\n            }}\n"
                             "          }}{}\n",
                             m.lines[i].value(), i + 1 < m.lines.size() ? "," : "");
    }
    faces += "        ],";
    CHECK_THAT(loadError(good, faces, "\"faces\": [],"),
               ContainsSubstring("objects[3].data: a draft needs one or more faces"));
    CHECK_THAT(loadError(good, faces, "\"faces\": 7,"),
               ContainsSubstring("objects[3].data.faces: expected an array"));
    CHECK_THAT(loadError(good, faces, "\"faces\": [{\"feature\": 5, \"face\": {\"role\": \"edge\"}}],"),
               ContainsSubstring("objects[3].data.faces[0].face.role: unknown value 'edge'"));
    CHECK_THAT(loadError(good, faces, "\"faces\": [{\"feature\": 5, \"face\": {\"role\": \"end_cap\", \"entity\": 1}}],"),
               ContainsSubstring("objects[3].data.faces[0]: an end cap is not named by an entity"));
    CHECK_THAT(loadError(good, faces, "\"faces\": [{\"feature\": 5}],"),
               ContainsSubstring("objects[3].data.faces[0].face: missing required field"));
    CHECK_THAT(loadError(good, faces, "\"faces\": [{\"feature\": 5, \"face\": {\"role\": \"end_cap\"}, \"id\": 1}],"),
               ContainsSubstring("objects[3].data.faces[0].id: unknown field"));
    CHECK_THAT(loadError(good, "\"target\": 5,\n        \"faces\"", "\"target\": 0,\n        \"faces\""),
               ContainsSubstring("objects[3].data: a draft needs a target feature"));
}

TEST_CASE("DraftFile_ExampleFileMatchesTheBuilder", "[io][draft][example][p12]") {
    DraftedBlockModel m;
    Regenerator regenerator;
    regenerate(regenerator, m.doc);
    const auto text = io::documentToJson(m.doc);
    REQUIRE(text.has_value());
    CHECK(*text == readFile(kExamples / "drafted_block.bcad"));
}

TEST_CASE("DraftFile_ExportedDraftsReadBackFromStep", "[io][draft][step][p12]") {
    TempDir dir;
    const auto path = dir.path() / "drafted_block.step";
    DraftedBlockModel m;
    Regenerator regenerator;
    regenerate(regenerator, m.doc);
    const auto summary = io::exportStep(m.doc, path);
    REQUIRE(summary.has_value());
    CHECK(summary->bodies.size() == 5);
    const auto contents = readStepFile(path);
    REQUIRE(contents.has_value());
    CHECK(contents->valid);
    CHECK(contents->solids == 5);
    // STEP stores decimal text: within 1e-9, as for the other exports.
    double total = 0.0;
    for (const Shift& shift : {Shift::of(5.0, 0.0), Shift::of(5.0, 20.0), Shift::of(-5.0, 0.0), Shift::of(5.0, 40.0),
                               Shift::of(-5.0, 0.0, true)}) {
        total += DraftedBlockModel::shape(40.0, shift).volume();
    }
    CHECK_THAT(contents->volumeMm3, WithinRel(total, 1e-9));
    const double reach = 40.0 * std::tan(5.0 * bettercad::test::kPi / 180.0);
    CHECK_THAT(contents->minMm[0], WithinRel(-reach, 1e-9));
    CHECK_THAT(contents->maxMm[1], WithinRel(60.0 + reach, 1e-9));
    CHECK_THAT(contents->maxMm[2], WithinRel(40.0, 1e-9));
}
