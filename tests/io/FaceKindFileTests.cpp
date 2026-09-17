#include "features/FeatureTestSupport.hpp"
#include "support/FaceKindModels.hpp"
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
#include <functional>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using bettercad::test::BevelledBlockModel;
using bettercad::test::DrilledBlockModel;
using bettercad::test::LoftedFrustumModel;
using bettercad::test::PostRowModel;
using bettercad::test::readFile;
using bettercad::test::readStepFile;
using bettercad::test::requireReport;
using bettercad::test::RevolvedRingModel;
using bettercad::test::SpokeHubModel;
using bettercad::test::SweptBarModel;
using bettercad::test::TempDir;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;

// P12-SKETCH-003: the new face roles, sweep path edges, chamfer edge
// references and copies in the native format.

namespace {

const std::filesystem::path kExamples{BETTERCAD_EXAMPLE_MODELS_DIR};

/// Every sketch placement and body volume of a document, bit for bit.
struct Snapshot {
    std::vector<Frame3D> placements;
    std::vector<std::uint64_t> volumes;

    friend bool operator==(const Snapshot&, const Snapshot&) = default;
};

Snapshot snapshot(const Regenerator& regenerator, const Document& doc) {
    Snapshot result;
    for (const DocumentObject& object : doc.objects()) {
        if (const auto* sketch = dynamic_cast<const sketch::Sketch*>(&object)) {
            result.placements.push_back(sketch->placement());
        }
        if (const geometry::Body* body = regenerator.body(object.id())) {
            const auto props = body->massProperties();
            REQUIRE(props.has_value());
            result.volumes.push_back(std::bit_cast<std::uint64_t>(props->volume.si()));
        }
    }
    return result;
}

void regenerate(Regenerator& regenerator, Document& doc) {
    const RegenerationReport report = requireReport(regenerator, doc);
    INFO((report.errors.empty() ? std::string{} : report.errors.begin()->second.message));
    REQUIRE(report.succeeded());
}

const std::vector<std::function<Document()>>& models() {
    static const std::vector<std::function<Document()>> builders{
        [] { return RevolvedRingModel{}.doc.clone(); },  [] { return SweptBarModel{}.doc.clone(); },
        [] { return LoftedFrustumModel{}.doc.clone(); }, [] { return DrilledBlockModel{}.doc.clone(); },
        [] { return BevelledBlockModel{}.doc.clone(); }, [] { return PostRowModel{}.doc.clone(); },
        [] { return SpokeHubModel{}.doc.clone(); }};
    return builders;
}

/// @p text with the first @p from replaced by @p to, loaded: the error message.
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

TEST_CASE("FaceKindFile_SaveLoad_ResolvesEveryFaceBitForBit", "[io][references][p12][acceptance]") {
    TempDir dir;
    for (const auto& build : models()) {
        Document original = build();
        INFO(original.name());
        Regenerator before;
        regenerate(before, original);
        const Snapshot expected = snapshot(before, original);
        const auto path = dir.path() / std::format("{}.bcad", original.name());
        REQUIRE(io::saveDocument(original, path).has_value());

        auto loaded = io::loadDocument(path);
        REQUIRE(loaded.has_value());
        CHECK(equivalent(original, *loaded));
        // Forget the saved placements of attached sketches: regeneration
        // must find every face again.
        std::vector<ObjectId> attached;
        for (const DocumentObject& object : loaded->objects()) {
            const auto* sketch = dynamic_cast<const sketch::Sketch*>(&object);
            if (sketch != nullptr && sketch->attachment()) {
                attached.push_back(object.id());
            }
        }
        CHECK_FALSE(attached.empty());
        for (const ObjectId id : attached) {
            REQUIRE(loaded->modifyObject<sketch::Sketch>(id, [](sketch::Sketch& s) {
                              return s.setPlacement(Frame3D::yz());
                          }).has_value());
        }
        Regenerator after;
        regenerate(after, *loaded);
        CHECK(snapshot(after, *loaded) == expected);
        CHECK(equivalent(original, *loaded));
        CHECK(io::documentToJson(*loaded).value() == readFile(path));
    }
}

TEST_CASE("FaceKindFile_RolesPathEdgesEdgeReferencesAndCopiesAreWrittenAsJson", "[io][references][p12]") {
    const DrilledBlockModel holes;
    const std::string drilled = io::documentToJson(holes.doc).value();
    CHECK_THAT(drilled, ContainsSubstring("\"face\": {\n            \"role\": \"hole_bottom\"\n          }"));
    CHECK_THAT(drilled, ContainsSubstring("\"face\": {\n            \"role\": \"counterbore_floor\"\n          }"));

    const BevelledBlockModel bevel;
    CHECK_THAT(io::documentToJson(bevel.doc).value(),
               ContainsSubstring("\"face\": {\n            \"role\": \"chamfer\",\n            \"edge\": 2\n"));

    const SweptBarModel bar;
    CHECK_THAT(io::documentToJson(bar.doc).value(),
               ContainsSubstring(std::format("\"face\": {{\n            \"role\": \"side\",\n            \"entity\": {},\n"
                                             "            \"along\": {}\n          }}",
                                             bar.top.value(), bar.bend.value())));

    const PostRowModel row;
    const std::string rows = io::documentToJson(row.doc).value();
    CHECK_THAT(rows, ContainsSubstring(std::format(
                         "\"face\": {{\n            \"role\": \"side\",\n            \"entity\": {},\n"
                         "            \"copies\": [\n"
                         "              {{\n                \"feature\": 7,\n                \"instance\": 2\n"
                         "              }},\n"
                         "              {{\n                \"feature\": 8,\n                \"instance\": 1\n"
                         "              }}\n"
                         "            ]\n          }}",
                         row.right.value())));
    // Nothing about the kernel's faces is written.
    for (const std::string_view word : {"face_index", "\"index\"", "occt", "shape"}) {
        CHECK(rows.find(word) == std::string::npos);
    }
}

TEST_CASE("FaceKindFile_MalformedRolesAndCopiesAreRejectedWithThePath", "[io][references][p12]") {
    const PostRowModel m;
    const std::string good = io::documentToJson(m.doc).value();
    // The first copy, the first side and the first end cap are CopySketch's,
    // CopySketch's and PostSketch's.
    const std::string firstCopy = "\"feature\": 7,\n                \"instance\": 2";
    const std::string copies = "[\n              {\n                " + firstCopy + "\n              }\n            ]";
    CHECK_THAT(loadError(good, "\"instance\": 2", "\"instance\": \"two\""),
               ContainsSubstring(".attachment.face.copies[0].instance: expected a non-negative integer"));
    CHECK_THAT(loadError(good, "\"instance\": 2", "\"instance\": 4294967296"),
               ContainsSubstring(".attachment.face.copies[0].instance: expected an integer below 2^32"));
    CHECK_THAT(loadError(good, "\"instance\": 2", "\"instance\": 0"),
               ContainsSubstring(".attachment: a copy is an instance from 1 (instance 0 is the original)"));
    CHECK_THAT(loadError(good, "\"instance\": 2", "\"instance\": 2, \"step\": 1"),
               ContainsSubstring(".attachment.face.copies[0].step: unknown field"));
    CHECK_THAT(loadError(good, firstCopy, "\"feature\": 7"),
               ContainsSubstring(".attachment.face.copies[0].instance: missing required field"));
    CHECK_THAT(loadError(good, firstCopy, "\"instance\": 2"),
               ContainsSubstring(".attachment.face.copies[0].feature: missing required field"));
    CHECK_THAT(loadError(good, firstCopy, "\"feature\": 0,\n                \"instance\": 2"),
               ContainsSubstring(".attachment: a copy must name a valid feature"));
    CHECK_THAT(loadError(good, "\"copies\": " + copies, "\"copies\": 5"),
               ContainsSubstring(".attachment.face.copies: expected an array"));
    CHECK_THAT(loadError(good, "\"copies\": " + copies, "\"copies\": [5]"),
               ContainsSubstring(".attachment.face.copies[0]: expected an object"));

    CHECK_THAT(loadError(good, "\"role\": \"end_cap\"", "\"role\": \"end_cap\", \"along\": 3"),
               ContainsSubstring(".attachment: an end cap is not named by a path edge"));
    CHECK_THAT(loadError(good, "\"role\": \"side\",", "\"role\": \"side\", \"along\": 0,"),
               ContainsSubstring(".attachment: a side face's path edge must be a valid entity"));
    CHECK_THAT(loadError(good, "\"role\": \"side\",", "\"role\": \"side\", \"along\": \"up\","),
               ContainsSubstring(".attachment.face.along: expected an ID"));
    CHECK_THAT(loadError(good, "\"role\": \"side\",", "\"role\": \"side\", \"edge\": 1,"),
               ContainsSubstring(".attachment: a side face is not named by an edge reference"));
    CHECK_THAT(loadError(good, "\"role\": \"end_cap\"", "\"role\": \"chamfer\""),
               ContainsSubstring(".attachment: a chamfer face is named by its edge reference, from 1"));
    CHECK_THAT(loadError(good, "\"role\": \"end_cap\"", "\"role\": \"chamfer\", \"edge\": 0"),
               ContainsSubstring(".attachment: a chamfer face is named by its edge reference, from 1"));
    CHECK_THAT(loadError(good, "\"role\": \"end_cap\"", "\"role\": \"chamfer\", \"edge\": \"one\""),
               ContainsSubstring(".attachment.face.edge: expected a non-negative integer"));
    CHECK_THAT(loadError(good, "\"role\": \"end_cap\"", "\"role\": \"chamfer\", \"edge\": 4294967296"),
               ContainsSubstring(".attachment.face.edge: expected an integer below 2^32"));
    CHECK_THAT(loadError(good, "\"role\": \"end_cap\"", "\"role\": \"hole_bottom\", \"entity\": 3"),
               ContainsSubstring(".attachment: a hole bottom is not named by an entity"));
    CHECK_THAT(loadError(good, "\"role\": \"end_cap\"", "\"role\": \"bottom\""),
               ContainsSubstring(".attachment.face.role: unknown value 'bottom'"));
}

TEST_CASE("FaceKindFile_ExampleFileMatchesTheBuilder", "[io][references][example][p12]") {
    PostRowModel m;
    Regenerator regenerator;
    regenerate(regenerator, m.doc);
    const auto text = io::documentToJson(m.doc);
    REQUIRE(text.has_value());
    CHECK(*text == readFile(kExamples / "post_row.bcad"));
}

TEST_CASE("FaceKindFile_ExportedCopiesReadBackFromStep", "[io][references][step][p12]") {
    TempDir dir;
    const auto path = dir.path() / "post_row.step";
    PostRowModel m;
    Regenerator regenerator;
    regenerate(regenerator, m.doc);
    const auto summary = io::exportStep(m.doc, path);
    REQUIRE(summary.has_value());
    // Flip and the three bosses.
    CHECK(summary->bodies.size() == 4);
    const auto contents = readStepFile(path);
    REQUIRE(contents.has_value());
    CHECK(contents->valid);
    CHECK(contents->solids == 4);
    // STEP stores decimal text: within 1e-9, as for the other exports.
    const double bosses = std::numbers::pi * (9.0 * 4.0 + 9.0 * 4.0 + 4.0 * 3.0);
    CHECK_THAT(contents->volumeMm3, WithinRel(2.0 * PostRowModel::rowVolume(3.0) + bosses, 1e-9));
    CHECK_THAT(contents->maxMm[0], WithinRel(200.0, 1e-9));
}
