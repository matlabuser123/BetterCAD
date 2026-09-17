#include "features/FeatureTestSupport.hpp"
#include "support/FaceModels.hpp"
#include "support/TestFiles.hpp"
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
#include <format>
#include <memory>
#include <string>
#include <string_view>

using namespace bettercad;
using namespace bettercad::features;
using bettercad::test::FaceBlockModel;
using bettercad::test::readFile;
using bettercad::test::readStepFile;
using bettercad::test::requireReport;
using bettercad::test::TempDir;
using bettercad::test::volumeMm3;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;

// P12-STREF-001: face references in the native format. A reference is the
// feature's ID and the face's role (and entity); nothing about the kernel's
// faces is written.

namespace {

const std::filesystem::path kExamples{BETTERCAD_EXAMPLE_MODELS_DIR};

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

const sketch::Sketch& sketchOf(const Document& doc, ObjectId id) {
    const auto* sketch = doc.findObjectAs<sketch::Sketch>(id);
    REQUIRE(sketch != nullptr);
    return *sketch;
}

} // namespace

TEST_CASE("FaceReferenceFile_SaveLoad_ResolvesTheSameFaces", "[io][references][p12][acceptance]") {
    TempDir dir;
    const auto path = dir.path() / "face_block.bcad";
    auto original = std::make_unique<FaceBlockModel>();
    Regenerator before;
    REQUIRE(requireReport(before, original->doc).succeeded());
    const Document expected = original->doc.clone();
    const FaceBlockModel ids;
    REQUIRE(io::saveDocument(original->doc, path).has_value());
    const double volume = volumeMm3(before, ids.pocket);
    const Frame3D boss = sketchOf(original->doc, ids.bossSketch).placement();
    const Frame3D side = sketchOf(original->doc, ids.sideSketch).placement();
    original.reset();

    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    CHECK(equivalent(expected, *loaded));
    CHECK(sketchOf(*loaded, ids.bossSketch).attachment() == FaceBlockModel::faceOf(ids.base, FaceRole::EndCap));
    CHECK(sketchOf(*loaded, ids.sideSketch).attachment() ==
          FaceBlockModel::faceOf(ids.base, FaceRole::Side, ids.frontLine));
    // Forget the saved placements: regeneration must find the faces again.
    REQUIRE(loaded->modifyObject<sketch::Sketch>(ids.bossSketch, [](sketch::Sketch& s) {
                      return s.setPlacement(Frame3D::xz());
                  }).value());
    REQUIRE(loaded->modifyObject<sketch::Sketch>(ids.sideSketch, [](sketch::Sketch& s) {
                      return s.setPlacement(Frame3D::yz());
                  }).value());
    Regenerator after;
    REQUIRE(requireReport(after, *loaded).succeeded());
    CHECK(bits(volumeMm3(after, ids.pocket)) == bits(volume));
    CHECK(sketchOf(*loaded, ids.bossSketch).placement() == boss);
    CHECK(sketchOf(*loaded, ids.sideSketch).placement() == side);
    CHECK(equivalent(expected, *loaded));

    const std::string text = readFile(path);
    CHECK_THAT(text, ContainsSubstring("\"face\": {\n"));
    CHECK_THAT(text, ContainsSubstring("\"role\": \"end_cap\"\n"));
    CHECK_THAT(text, ContainsSubstring(std::format("\"role\": \"side\",\n            \"entity\": {}\n",
                                                   ids.frontLine.value())));
    CHECK(io::documentToJson(*loaded).value() == text);
}

TEST_CASE("FaceReferenceFile_ExampleFileMatchesTheBuilder", "[io][references][example][p12]") {
    FaceBlockModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const auto text = io::documentToJson(m.doc);
    REQUIRE(text.has_value());
    CHECK(*text == readFile(kExamples / "face_block.bcad"));
}

TEST_CASE("FaceReferenceFile_MalformedReferencesAreRejectedWithThePath", "[io][references][p12]") {
    FaceBlockModel m;
    const std::string good = io::documentToJson(m.doc).value();
    const auto loadError = [&](std::string_view from, std::string_view to) {
        std::string text = good;
        const auto pos = text.find(from);
        REQUIRE(pos != std::string::npos);
        text.replace(pos, from.size(), to);
        const auto loaded = io::documentFromJson(text);
        REQUIRE_FALSE(loaded.has_value());
        return loaded.error().message;
    };
    CHECK_THAT(loadError("\"role\": \"end_cap\"", "\"role\": \"top\""),
               ContainsSubstring(".attachment.face.role: unknown value 'top'"));
    CHECK_THAT(loadError("\"role\": \"end_cap\"", "\"role\": \"end_cap\", \"entity\": 5"),
               ContainsSubstring(".attachment: an end cap is not named by an entity"));
    CHECK_THAT(loadError("\"role\": \"end_cap\"", "\"role\": \"side\""),
               ContainsSubstring(".attachment: a side face is named by a valid profile entity"));
    CHECK_THAT(loadError("\"role\": \"end_cap\"", "\"role\": \"end_cap\", \"id\": 3"),
               ContainsSubstring(".attachment.face.id: unknown field"));
    CHECK_THAT(loadError("\"face\": {", "\"plane\": \"xy\", \"face\": {"),
               ContainsSubstring(".attachment.plane: a face reference has no plane of its own"));
    CHECK_THAT(loadError(std::format("\"entity\": {}", m.frontLine.value()), "\"entity\": 0"),
               ContainsSubstring(".attachment: a side face is named by a valid profile entity"));
    CHECK_THAT(loadError(std::format("\"entity\": {}", m.frontLine.value()), "\"entity\": \"front\""),
               ContainsSubstring(".attachment.face.entity: expected an ID"));
}

TEST_CASE("FaceReferenceFile_ExportedModelReadsBackFromStep", "[io][references][step][p12]") {
    TempDir dir;
    const auto path = dir.path() / "face_block.step";
    FaceBlockModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const auto summary = io::exportStep(m.doc, path);
    REQUIRE(summary.has_value());
    REQUIRE(summary->bodies.size() == 1);
    const auto contents = readStepFile(path);
    REQUIRE(contents.has_value());
    CHECK(contents->valid);
    CHECK(contents->solids == 1);
    // STEP stores decimal text: within 1e-9, as for the other exports.
    CHECK_THAT(contents->volumeMm3, WithinRel(FaceBlockModel::volume(20.0), 1e-9));
    CHECK_THAT(contents->maxMm[2], WithinRel(35.0, 1e-9));
}
