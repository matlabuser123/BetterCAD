#include "features/FeatureTestSupport.hpp"
#include "support/FilletModels.hpp"
#include "support/TestFiles.hpp"
#include "support/VariableFilletModels.hpp"
#include "support/occt/StepReadBack.hpp"

#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/VariableFilletFeature.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/io/ModelExport.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <string>
#include <string_view>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using bettercad::test::printOf;
using bettercad::test::readFile;
using bettercad::test::readStepFile;
using bettercad::test::requireReport;
using bettercad::test::TaperedBlockModel;
using bettercad::test::TempDir;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// P12-FEAT-006: variable-radius fillet features in the native format.

namespace {

const std::filesystem::path kExamples{BETTERCAD_EXAMPLE_MODELS_DIR};

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

// The taper as written (objects: BlockSketch [0], Block [1], Taper [2]).
constexpr std::string_view kTaper = R"("type": "variable_fillet",
      "name": "Taper",
      "data": {
        "target": 6,
        "edges": [
          {
            "edge": {
              "curve": "line",
              "point": [
                0.0,
                0.0,
                0.02
              ],
              "direction": [
                1.0,
                0.0,
                0.0
              ]
            },
            "stations": [
              {
                "position": 0.0,
                "radius": 0.003,
                "radius_parameter": 3
              },
              {
                "position": 1.0,
                "radius": 0.008,
                "radius_parameter": 4
              }
            ]
          },
          {
            "edge": {
              "curve": "line",
              "point": [
                0.0,
                0.05,
                0.02
              ],
              "direction": [
                1.0,
                0.0,
                0.0
              ]
            },
            "stations": [
              {
                "position": 0.0,
                "radius": 0.002
              },
              {
                "position": 0.3,
                "radius": 0.004
              },
              {
                "position": 1.0,
                "radius": 0.007
              }
            ]
          }
        ]
      })";

} // namespace

TEST_CASE("VariableFilletFile_SaveLoad_RegeneratesTheSameBodies", "[io][fillet][variable][p12][acceptance]") {
    TempDir dir;
    const auto path = dir.path() / "tapered.bcad";
    TaperedBlockModel m;
    Regenerator before;
    regenerate(before, m.doc);
    const bettercad::test::BodyPrint print = printOf(*before.body(m.taper));
    const Document copy = m.doc.clone();
    REQUIRE(io::saveDocument(m.doc, path).has_value());
    // Destroy the model, then load it back.
    m.doc = Document("Closed");
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    CHECK(equivalent(copy, *loaded));
    CHECK(loaded->id() == copy.id());
    CHECK(loaded->itemIds() == copy.itemIds());
    const auto* taper = loaded->findObjectAs<VariableFilletFeature>(m.taper);
    REQUIRE(taper != nullptr);
    CHECK(taper->definition() == copy.findObjectAs<VariableFilletFeature>(m.taper)->definition());
    Regenerator after;
    regenerate(after, *loaded);
    CHECK(printOf(*after.body(m.taper)) == print);
    CHECK(io::documentToJson(*loaded).value() == readFile(path));
    // Positions and radii come back bit for bit, the odd ones included.
    VariableFilletDefinition odd = taper->definition();
    odd.edges[1].stations[1] = {.position = 0.1 + 0.2, .radius = Length::fromSi(0.1 * 0.04)};
    REQUIRE(loaded->modifyObject<VariableFilletFeature>(m.taper, [&](VariableFilletFeature& f) {
        return f.setDefinition(odd);
    }).has_value());
    const auto again = io::documentFromJson(io::documentToJson(*loaded).value());
    REQUIRE(again.has_value());
    CHECK(again->findObjectAs<VariableFilletFeature>(m.taper)->definition() == odd);
}

TEST_CASE("VariableFilletFile_ConstantFilletsAreWrittenAsBefore", "[io][fillet][variable][p12]") {
    // A constant fillet keeps its own type and fields next to a variable one.
    bettercad::test::FilletBlockModel m;
    const ObjectId taper = m.add<VariableFilletFeature>(
        "Taper", {.target = bettercad::test::BlockModel::featureId(m.round),
                  .edges = {{.edge = bettercad::test::BlockModel::alongX(50, 0),
                             .stations = {{.position = 0.0, .radius = 2_mm}, {.position = 1.0, .radius = 4_mm}}}}});
    const std::string text = io::documentToJson(m.doc).value();
    CHECK_THAT(text, ContainsSubstring("\"type\": \"fillet\",\n      \"name\": \"Round\",\n      \"data\": {\n"
                                       "        \"target\": 6,\n        \"edges\": ["));
    CHECK_THAT(text, ContainsSubstring("\"radius\": 0.005,\n        \"radius_parameter\": 4\n      }"));
    CHECK_THAT(text, ContainsSubstring("\"type\": \"variable_fillet\",\n      \"name\": \"Taper\""));
    const auto loaded = io::documentFromJson(text);
    REQUIRE(loaded.has_value());
    CHECK(equivalent(m.doc, *loaded));
    Regenerator regenerator;
    Document doc = loaded->clone();
    regenerate(regenerator, doc);
    CHECK(regenerator.body(taper) != nullptr);
}

TEST_CASE("VariableFilletFile_DefinitionsAreWrittenAsJson", "[io][fillet][variable][p12]") {
    const TaperedBlockModel m;
    CHECK_THAT(io::documentToJson(m.doc).value(), ContainsSubstring(std::string{kTaper}));
}

TEST_CASE("VariableFilletFile_MalformedDefinitionsAreRejectedWithThePath", "[io][fillet][variable][p12]") {
    const TaperedBlockModel m;
    const std::string good = io::documentToJson(m.doc).value();
    // The taper's object with other data.
    const auto withData = [&](std::string_view data) {
        return loadError(good, kTaper,
                         std::string{"\"type\": \"variable_fillet\",\n      \"name\": \"Taper\",\n      \"data\": "} +
                             std::string{data});
    };
    const std::string lastStation = R"({
                "position": 1.0,
                "radius": 0.007
              })";
    CHECK_THAT(loadError(good, "\"target\": 6,\n        \"edges\": [",
                         "\"target\": 6, \"order\": 1,\n        \"edges\": ["),
               ContainsSubstring("objects[2].data.order: unknown field"));
    CHECK_THAT(loadError(good, "\"target\": 6,\n        \"edges\": [", "\"target\": 0,\n        \"edges\": ["),
               ContainsSubstring("objects[2].data: a variable-radius fillet needs a target feature"));
    CHECK_THAT(loadError(good, "\"target\": 6,\n        \"edges\": [", "\"target\": 6,\n        \"lines\": ["),
               ContainsSubstring("objects[2].data.lines: unknown field"));
    CHECK_THAT(withData(R"({"target": 6, "edges": 3})"), ContainsSubstring("objects[2].data.edges: expected an array"));
    CHECK_THAT(withData(R"({"target": 6})"), ContainsSubstring("objects[2].data.edges: missing required field"));
    CHECK_THAT(withData(R"({"target": 6, "edges": []})"),
               ContainsSubstring("objects[2].data: a variable-radius fillet needs at least one edge"));
    CHECK_THAT(withData(R"({"target": 6, "edges": [{"stations": []}]})"),
               ContainsSubstring("objects[2].data.edges[0].edge: missing required field"));
    CHECK_THAT(withData(R"({"target": 6, "edges": [7]})"),
               ContainsSubstring("objects[2].data.edges[0]: expected an object"));
    CHECK_THAT(loadError(good, R"("edge": {
              "curve": "line",
              "point": [
                0.0,
                0.05,)", R"("line": {
              "curve": "line",
              "point": [
                0.0,
                0.05,)"),
               ContainsSubstring("objects[2].data.edges[1].line: unknown field"));
    const std::string backPoint = "\",\n              \"point\": [\n                0.0,\n                0.05,";
    CHECK_THAT(loadError(good, "\"curve\": \"line" + backPoint, "\"curve\": \"spline" + backPoint),
               ContainsSubstring("objects[2].data.edges[1].edge.curve: unknown value 'spline'"));
    CHECK_THAT(loadError(good, lastStation, R"({
                "position": 1.0,
                "radius": "7 mm"
              })"),
               ContainsSubstring("objects[2].data.edges[1].stations[2].radius: expected a number"));
    CHECK_THAT(loadError(good, lastStation, R"({
                "radius": 0.007
              })"),
               ContainsSubstring("objects[2].data.edges[1].stations[2].position: missing required field"));
    CHECK_THAT(loadError(good, lastStation, R"({
                "position": 1.0,
                "radius": 0.007,
                "fixed": true
              })"),
               ContainsSubstring("objects[2].data.edges[1].stations[2].fixed: unknown field"));
    CHECK_THAT(loadError(good, lastStation, R"({
                "position": 1.0,
                "radius": 0.007,
                "radius_parameter": "high"
              })"),
               ContainsSubstring("objects[2].data.edges[1].stations[2].radius_parameter: expected an ID"));
    CHECK_THAT(loadError(good, lastStation, R"({
                "position": 0.9,
                "radius": 0.007
              })"),
               ContainsSubstring("objects[2].data: edge reference 2: the last station must be at position 1, got 0.9"));
    CHECK_THAT(loadError(good, lastStation, R"({
                "position": 1.0,
                "radius": 0.004
              })"),
               ContainsSubstring("objects[2].data: edge reference 2: between stations 2 and 3 (4 mm at 0.3, 4 mm at 1) "
                                 "the radius would rise to"));
    CHECK_THAT(withData(R"({"target": 6, "edges": [{"edge": {"curve": "line", "point": [0, 0, 0],
                                                             "direction": [1, 0, 0]},
                                                    "stations": 7}]})"),
               ContainsSubstring("objects[2].data.edges[0].stations: expected an array"));
    CHECK_THAT(withData(R"({"target": 6, "edges": [{"edge": {"curve": "line", "point": [0, 0, 0],
                                                             "direction": [1, 0, 0]},
                                                    "stations": [{"position": 0, "radius": 0.001}, 5]}]})"),
               ContainsSubstring("objects[2].data.edges[0].stations[1]: expected an object"));
}

TEST_CASE("VariableFilletFile_ExampleFileMatchesTheBuilder", "[io][fillet][variable][example][p12]") {
    TaperedBlockModel m;
    Regenerator regenerator;
    regenerate(regenerator, m.doc);
    const auto text = io::documentToJson(m.doc);
    REQUIRE(text.has_value());
    CHECK(*text == readFile(kExamples / "tapered_block.bcad"));
}

TEST_CASE("VariableFilletFile_ExportedFilletsReadBackFromStep", "[io][fillet][variable][step][p12]") {
    TempDir dir;
    const auto path = dir.path() / "tapered_block.step";
    TaperedBlockModel m;
    Regenerator regenerator;
    regenerate(regenerator, m.doc);
    const auto summary = io::exportStep(m.doc, path);
    REQUIRE(summary.has_value());
    CHECK(summary->bodies.size() == 1);
    const auto contents = readStepFile(path);
    REQUIRE(contents.has_value());
    CHECK(contents->valid);
    CHECK(contents->solids == 1);
    // STEP stores decimal text, and the reader integrates the B-spline
    // fillet faces by Gauss-Kronrod over knot spans (4e-10 measured in the
    // probes): within 1e-9.
    CHECK_THAT(contents->volumeMm3, WithinRel(TaperedBlockModel::shape(100.0, 50.0, 3.0, 8.0).volume(), 1e-9));
    CHECK_THAT(contents->maxMm[0], WithinAbs(100.0, 2e-7));
    CHECK_THAT(contents->maxMm[1], WithinAbs(50.0, 2e-7));
    CHECK_THAT(contents->maxMm[2], WithinAbs(20.0, 2e-7));
}
