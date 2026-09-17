#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/HoleStandardModels.hpp"
#include "support/TestFiles.hpp"
#include "support/occt/StepReadBack.hpp"

#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/io/ModelExport.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <numbers>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using bettercad::test::printOf;
using bettercad::test::readFile;
using bettercad::test::requireReport;
using bettercad::test::TappedPlateModel;
using bettercad::test::TempDir;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;

// P12-HOLE-001: the standards data of a hole in the native format. A file
// stores the designations ("M8", "6H", "H7"), not the dimensions they stand
// for, so a model keeps its engineering intent.

namespace {

constexpr double kRel = 1e-12;
// STEP stores coordinates as decimal text (as in StepExportTests).
constexpr double kRelStep = 1e-9;

std::string replaceOnce(std::string text, std::string_view from, std::string_view to) {
    const auto pos = text.find(from);
    REQUIRE(pos != std::string::npos);
    text.replace(pos, from.size(), to);
    return text;
}

const geometry::Body& bodyOf(const Regenerator& regenerator, ObjectId id) {
    const geometry::Body* body = regenerator.body(id);
    REQUIRE(body != nullptr);
    return *body;
}

} // namespace

TEST_CASE("HoleStandards_SaveLoadPreservesTheStandardsData", "[hole][standards][io][p12][acceptance]") {
    TempDir dir;
    TappedPlateModel m;
    Regenerator before;
    REQUIRE(requireReport(before, m.doc).succeeded());
    const bettercad::test::BodyPrint print = printOf(bodyOf(before, m.reamed));
    const HoleDefinition tapped = m.definitionOf(m.tapped);
    const HoleDefinition blindTapped = m.definitionOf(m.blindTapped);
    const HoleDefinition seat = m.definitionOf(m.seat);
    const HoleDefinition boss = m.definitionOf(m.boss);
    const HoleDefinition reamed = m.definitionOf(m.reamed);

    const std::filesystem::path path = dir.path() / "plate.bcad";
    const Document expected = m.doc.clone();
    REQUIRE(io::saveDocument(m.doc, path).has_value());
    m.doc = Document("Closed");
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    CHECK(equivalent(expected, *loaded));

    // Every field, bit for bit.
    CHECK(loaded->findObjectAs<HoleFeature>(m.tapped)->definition() == tapped);
    CHECK(loaded->findObjectAs<HoleFeature>(m.blindTapped)->definition() == blindTapped);
    CHECK(loaded->findObjectAs<HoleFeature>(m.seat)->definition() == seat);
    CHECK(loaded->findObjectAs<HoleFeature>(m.boss)->definition() == boss);
    CHECK(loaded->findObjectAs<HoleFeature>(m.reamed)->definition() == reamed);
    // The dependency graph still holds the thread's length parameter.
    const DocumentGraph graph = buildDependencyGraph(*loaded);
    CHECK(graph.graph.dependenciesOf(m.blindTapped) ==
          std::set<ObjectId>{m.tapped, ObjectId{m.threadLength}});

    Regenerator after;
    REQUIRE(requireReport(after, *loaded).succeeded());
    CHECK(printOf(bodyOf(after, m.reamed)) == print);

    SECTION("the loaded model is still driven by its parameters") {
        REQUIRE(loaded->setParameterValue(m.bore, 12_mm).has_value());
        REQUIRE(requireReport(after, *loaded).succeeded());
        CHECK_THAT(bodyOf(after, m.reamed).massProperties()->volume.in(units::mm3),
                   WithinRel(TappedPlateModel::expectedVolume(12.0), kRel));
    }
    SECTION("STEP export reads back with the same volume") {
        const std::filesystem::path step = dir.path() / "plate.step";
        REQUIRE(io::exportStep(*loaded, step).has_value());
        const auto contents = bettercad::test::readStepFile(step);
        REQUIRE(contents.has_value());
        CHECK(contents->valid);
        CHECK(contents->solids == 1);
        CHECK_THAT(contents->volumeMm3, WithinRel(TappedPlateModel::expectedVolume(), kRelStep));
    }
}

TEST_CASE("HoleStandards_FileStoresDesignationsNotDimensions", "[hole][standards][io][p12]") {
    TappedPlateModel m;
    const auto text = io::documentToJson(m.doc);
    REQUIRE(text.has_value());

    SECTION("a thread is its size, class and length") {
        CHECK_THAT(*text, ContainsSubstring(R"("thread": {
          "size": "M8",
          "class": "6H"
        })"));
        CHECK_THAT(*text, ContainsSubstring(R"("thread": {
          "size": "M6",
          "class": "6H",
          "length": 0.008,
          "length_parameter": 1
        })"));
    }
    SECTION("a clearance hole is its bolt's size and series") {
        CHECK_THAT(*text, ContainsSubstring(R"("clearance": {
          "size": "M8",
          "series": "medium"
        })"));
    }
    SECTION("a tolerance class is its designation") {
        CHECK_THAT(*text, ContainsSubstring(R"("tolerance": "H13")"));
        CHECK_THAT(*text, ContainsSubstring(R"("tolerance": "H7")"));
    }
    SECTION("a spotface has its own dimensions") {
        CHECK_THAT(*text, ContainsSubstring(R"("type": "spotface")"));
        CHECK_THAT(*text, ContainsSubstring(R"("spotface_diameter": 0.02,
        "spotface_depth": 0.001)"));
    }
    SECTION("a hole whose standard gives its diameter stores no diameter") {
        // The reamed hole's diameter is its own, and is stored.
        CHECK_THAT(*text, ContainsSubstring(R"("diameter": 0.01,
        "diameter_parameter": 2)"));
        // Two of the five holes have a diameter of their own (the reamed one
        // and none other): the rest take it from a thread or from ISO 273.
        std::size_t diameters = 0;
        for (std::size_t at = text->find("\"diameter\""); at != std::string::npos;
             at = text->find("\"diameter\"", at + 1)) {
            ++diameters;
        }
        CHECK(diameters == 1);
    }
}

TEST_CASE("HoleStandards_MalformedStandardsDataIsRejectedWithTheJsonPath", "[hole][standards][io][p12]") {
    TappedPlateModel m;
    const std::string good = io::documentToJson(m.doc).value();
    REQUIRE(io::documentFromJson(good).has_value());
    const auto message = [](const std::string& text) {
        const auto loaded = io::documentFromJson(text);
        REQUIRE_FALSE(loaded.has_value());
        UNSCOPED_INFO(loaded.error().message);
        return loaded.error().message;
    };
    // The file's objects are PlateSketch, Plate, Tapped, BlindTapped, Seat,
    // Boss and Reamed; the parameters are listed apart from them.
    constexpr std::string_view kThread = R"("thread": {
          "size": "M8",
          "class": "6H"
        })";

    SECTION("an unknown size, class or series") {
        CHECK(message(replaceOnce(good, R"("size": "M8",
          "class": "6H")", R"("size": "M9",
          "class": "6H")")) == "objects[2].data.thread.size: 'M9' is not a metric thread size: BetterCAD knows the "
                               "sizes of ISO 965-2 (coarse M1 to M64, fine M8x1 to M64x4)");
        CHECK_THAT(message(replaceOnce(good, R"("class": "6H")", R"("class": "6h")")),
                   ContainsSubstring("objects[2].data.thread.class: '6h' is not an internal thread tolerance class"));
        CHECK(message(replaceOnce(good, R"("series": "medium")", R"("series": "tight")")) ==
              "objects[4].data.clearance.series: unknown value 'tight'");
        CHECK_THAT(message(replaceOnce(good, R"("tolerance": "H7")", R"("tolerance": "K7")")),
                   ContainsSubstring("objects[6].data.tolerance: 'K7' is not a hole tolerance class BetterCAD knows"));
    }
    SECTION("a class whose limits BetterCAD does not know") {
        CHECK_THAT(message(replaceOnce(good, R"("class": "6H")", R"("class": "7H")")),
                   ContainsSubstring("objects[2].data: BetterCAD knows the M8 thread tolerances of grade 6 only"));
    }
    SECTION("the wrong shape") {
        CHECK(message(replaceOnce(good, kThread, R"("thread": "M8-6H")")) ==
              "objects[2].data.thread: expected an object");
        CHECK(message(replaceOnce(good, kThread, R"("thread": {"size": "M8", "class": "6H", "pitch": 0.00125})")) ==
              "objects[2].data.thread.pitch: unknown field");
        CHECK(message(replaceOnce(good, kThread, R"("thread": {"class": "6H"})")) ==
              "objects[2].data.thread.size: missing required field");
        CHECK(message(replaceOnce(good, kThread, R"("thread": {"size": "M8"})")) ==
              "objects[2].data.thread.class: missing required field");
        CHECK(message(replaceOnce(good, kThread, R"("thread": {"size": 8, "class": "6H"})")) ==
              "objects[2].data.thread.size: expected a string");
    }
    SECTION("an unknown type or field is still refused, not ignored") {
        CHECK(message(replaceOnce(good, R"("type": "simple")", R"("type": "counterboard")")) ==
              "objects[2].data.type: unknown value 'counterboard'");
        CHECK(message(replaceOnce(good, kThread, std::string{kThread} + R"(,
        "helix": true)")) == "objects[2].data.helix: unknown field");
    }
    SECTION("a standard size beside a diameter") {
        CHECK(message(replaceOnce(good, kThread, std::string{kThread} + R"(,
        "diameter": 0.0066)")) == "objects[2].data: a threaded hole's diameter comes from its thread; it takes no "
                                  "diameter");
    }
}

TEST_CASE("HoleStandards_FilesWithoutStandardsDataAreUnchanged", "[hole][standards][io][p12]") {
    // The flange of the P11 reference models was written before
    // P12-HOLE-001: its holes have none of the new keys, it loads as the
    // plain holes it was written with, and saving it reproduces the file.
    const std::filesystem::path path =
        std::filesystem::path{BETTERCAD_EXAMPLE_MODELS_DIR} / "reference" / "flange.bcad";
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    const std::string text = readFile(path);
    CHECK_THAT(text, !ContainsSubstring("\"thread\""));
    CHECK_THAT(text, !ContainsSubstring("\"clearance\""));
    CHECK_THAT(text, !ContainsSubstring("\"tolerance\""));
    CHECK_THAT(text, !ContainsSubstring("spotface"));

    std::size_t holes = 0;
    for (const ObjectId id : loaded->itemIds()) {
        const auto* hole = loaded->findObjectAs<HoleFeature>(id);
        if (hole == nullptr) {
            continue;
        }
        ++holes;
        const HoleDefinition& definition = hole->definition();
        CHECK_FALSE(definition.thread.has_value());
        CHECK_FALSE(definition.clearance.has_value());
        CHECK_FALSE(definition.tolerance.has_value());
        CHECK(definition.spotfaceDiameter == Length{});
        CHECK(definition.spotfaceDepth == Length{});
        CHECK(definition.diameter > Length{});
    }
    CHECK(holes == 2);

    // Saved again, the file is unchanged: no key was added to a hole that
    // has no standards data.
    const auto written = io::documentToJson(*loaded);
    REQUIRE(written.has_value());
    CHECK(*written == text);

    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, *loaded).succeeded());
}

TEST_CASE("HoleStandards_ExampleFileMatchesTheBuilder", "[hole][standards][io][example][p12]") {
    // examples/models/tapped_plate.bcad is the model of
    // support/HoleStandardModels.hpp, saved.
    TappedPlateModel m;
    const auto text = io::documentToJson(m.doc);
    REQUIRE(text.has_value());
    CHECK(*text == readFile(std::filesystem::path{BETTERCAD_EXAMPLE_MODELS_DIR} / "tapped_plate.bcad"));
}

