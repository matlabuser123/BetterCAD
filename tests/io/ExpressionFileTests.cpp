#include "features/FeatureTestSupport.hpp"
#include "support/DrivenPlateModel.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/Validation.hpp>
#include <bettercad/io/DocumentFile.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <bit>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using bettercad::test::DrivenPlateModel;
using bettercad::test::readFile;
using bettercad::test::requireReport;
using bettercad::test::TempDir;
using bettercad::test::volumeMm3;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;

// P12-PARAM-001: expressions and their evaluated values in the native format.

namespace {

const std::filesystem::path kExamples{BETTERCAD_EXAMPLE_MODELS_DIR};

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

std::string replaceOnce(std::string text, std::string_view from, std::string_view to) {
    const auto pos = text.find(from);
    REQUIRE(pos != std::string::npos);
    text.replace(pos, from.size(), to);
    return text;
}

/// Same validity, topology, volume, area and bounds, bit for bit.
void checkSameBody(const Regenerator& a, const Regenerator& b, ObjectId feature) {
    const geometry::Body* x = a.body(feature);
    const geometry::Body* y = b.body(feature);
    REQUIRE(x != nullptr);
    REQUIRE(y != nullptr);
    CHECK(x->isValid() == y->isValid());
    CHECK(x->topology() == y->topology());
    const auto px = x->massProperties();
    const auto py = y->massProperties();
    REQUIRE(px.has_value());
    REQUIRE(py.has_value());
    CHECK(bits(px->volume.si()) == bits(py->volume.si()));
    CHECK(bits(px->surfaceArea.si()) == bits(py->surfaceArea.si()));
    CHECK(*x->boundingBox() == *y->boundingBox());
}

} // namespace

TEST_CASE("ParameterExpressions_SaveLoad_PreservesExpressionsAndEvaluatedValues",
          "[io][expressions][p12][acceptance]") {
    TempDir dir;
    const std::filesystem::path path = dir.path() / "driven_plate.bcad";

    // Create, regenerate, save, destroy.
    auto original = std::make_unique<DrivenPlateModel>();
    Regenerator before;
    REQUIRE(requireReport(before, original->doc).succeeded());
    const Document expected = original->doc.clone();
    const DrivenPlateModel ids; // for the IDs only: the builder is deterministic
    REQUIRE(io::saveDocument(original->doc, path).has_value());
    original.reset();

    // Load: expressions and evaluated values are exactly as saved.
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    Document& doc = *loaded;
    CHECK(equivalent(expected, doc));
    CHECK_FALSE(doc.isDirty());
    for (const ParameterId id : {ids.height, ids.thickness, ids.holeSpacing, ids.holeY}) {
        const Parameter* saved = expected.parameters().find(id);
        const Parameter* restored = doc.parameters().find(id);
        REQUIRE(restored != nullptr);
        CHECK(restored->expression() == saved->expression());
        CHECK(bits(restored->siValue()) == bits(saved->siValue()));
    }
    CHECK(doc.parameters().find(ids.holeSpacing)->expression() == "width - 2 * edge_distance");

    // The expression dependencies are rebuilt from the text.
    const DocumentGraph graph = buildDependencyGraph(doc);
    CHECK(graph.graph.dependenciesOf(ids.holeSpacing) == std::set<ObjectId>{ids.width, ids.edgeDistance});
    CHECK(graph.graph.dependenciesOf(ids.holeY) == std::set<ObjectId>{ids.height});
    CHECK(graph.unresolved.empty());

    // Regenerating the loaded file changes no value and reproduces the body.
    Regenerator after;
    const RegenerationReport rebuilt = requireReport(after, doc);
    REQUIRE(rebuilt.succeeded());
    CHECK(rebuilt.updatedParameters.empty());
    CHECK(equivalent(expected, doc));
    checkSameBody(before, after, ids.plate);
    CHECK_THAT(volumeMm3(after, ids.plate), WithinRel(DrivenPlateModel::expectedVolumeMm3(100.0), 1e-12));

    // The loaded document stays parametric.
    REQUIRE(doc.setParameterValue(ids.width, 140_mm).has_value());
    const RegenerationReport edited = requireReport(after, doc);
    REQUIRE(edited.succeeded());
    CHECK(edited.updatedParameters == std::vector<ParameterId>{ids.height, ids.holeY, ids.thickness, ids.holeSpacing});
    CHECK_THAT(volumeMm3(after, ids.plate), WithinRel(DrivenPlateModel::expectedVolumeMm3(140.0), 1e-12));
    CHECK_THAT(doc.parameters().find(ids.holeSpacing)->siValue(), WithinRel(0.110, 1e-15));

    // Saving the edited document and loading it again keeps the new values.
    REQUIRE(io::saveDocument(doc, path).has_value());
    const auto reloaded = io::loadDocument(path);
    REQUIRE(reloaded.has_value());
    CHECK(equivalent(doc, *reloaded));
}

TEST_CASE("ParameterExpressions_SavingIsDeterministicAndKeepsTheFormatVersion", "[io][expressions][p12]") {
    DrivenPlateModel a;
    DrivenPlateModel b;
    Regenerator ra;
    Regenerator rb;
    REQUIRE(requireReport(ra, a.doc).succeeded());
    REQUIRE(requireReport(rb, b.doc).succeeded());
    const auto first = io::documentToJson(a.doc);
    const auto second = io::documentToJson(b.doc);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(*first == *second);
    // The expression field keeps its format-version-1 form: the text next to
    // the value it evaluated to.
    CHECK_THAT(*first, ContainsSubstring("\"version\": 1,"));
    CHECK_THAT(*first, ContainsSubstring("      \"name\": \"height\",\n"
                                         "      \"si_value\": 0.05,\n"
                                         "      \"unit\": \"mm\",\n"
                                         "      \"dimension\": {\n"
                                         "        \"length\": 1\n"
                                         "      },\n"
                                         "      \"expression\": \"width / 2\"\n"));
}

TEST_CASE("ParameterExpressions_Load_StaleValueIsReevaluatedAndReported", "[io][expressions][p12]") {
    DrivenPlateModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const std::string text = io::documentToJson(m.doc).value();

    // A file whose stored value disagrees with its expression (edited by
    // hand, or written before expressions were evaluated) loads as written,
    // and regeneration replaces the value and says so.
    auto loaded = io::documentFromJson(replaceOnce(text, "\"si_value\": 0.05,", "\"si_value\": 0.123,"));
    REQUIRE(loaded.has_value());
    CHECK(loaded->parameters().find(m.height)->siValue() == 0.123);
    Regenerator fresh;
    const RegenerationReport report = requireReport(fresh, *loaded);
    REQUIRE(report.succeeded());
    CHECK(report.updatedParameters == std::vector<ParameterId>{m.height});
    CHECK(loaded->parameters().find(m.height)->siValue() == 0.05);
    CHECK(equivalent(*loaded, m.doc));
}

TEST_CASE("ParameterExpressions_Load_RejectsMalformedExpressionsWithThePath", "[io][expressions][p12]") {
    DrivenPlateModel m;
    const std::string text = io::documentToJson(m.doc).value();

    const auto broken = io::documentFromJson(replaceOnce(text, "\"width / 2\"", "\"width / \""));
    REQUIRE_FALSE(broken.has_value());
    CHECK(broken.error().code == ErrorCode::ParseError);
    CHECK(broken.error().message ==
          "parameters[1]: parameter 'height': expression 'width / ': expected a number, a name or '(' at offset 8, "
          "found the end of the expression");

    const auto implicit = io::documentFromJson(replaceOnce(text, "\"0.1 * width\"", "\"0.1 width\""));
    REQUIRE_FALSE(implicit.has_value());
    CHECK(implicit.error().code == ErrorCode::ParseError);
    CHECK_THAT(implicit.error().message, ContainsSubstring("parameters[2]: parameter 'thickness': "
                                                           "expression '0.1 width': unknown unit 'width'"));

    const auto empty = io::documentFromJson(replaceOnce(text, "\"0.1 * width\"", "\"\""));
    REQUIRE_FALSE(empty.has_value());
    CHECK_THAT(empty.error().message, ContainsSubstring("parameters[2]"));
}

TEST_CASE("ParameterExpressions_Load_KeepsUnknownNamesForRegenerationToReport", "[io][expressions][p12]") {
    DrivenPlateModel m;
    const std::string text = io::documentToJson(m.doc).value();

    // An unknown name is a missing reference: the file loads, and regeneration
    // and validation report it with the token.
    auto loaded = io::documentFromJson(replaceOnce(text, "\"width / 2\"", "\"wdth / 2\""));
    REQUIRE(loaded.has_value());
    CHECK(loaded->parameters().find(m.height)->expression() == "wdth / 2");
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, *loaded);
    CHECK(report.failed == std::vector<ObjectId>{m.height});
    CHECK(report.errors.at(m.height).message == "parameter 'height' = wdth / 2: unknown parameter 'wdth' at offset 0");
    CHECK(report.blocked == std::vector<ObjectId>{m.holeY, m.sketch, m.plate});

    const ValidationReport validation = validateDocument(*loaded);
    CHECK(validation.count(ValidationCheck::MissingReferences, Severity::Error) == 1);

    // So does a cycle.
    auto cyclic = io::documentFromJson(replaceOnce(text, "\"width / 2\"", "\"hole_y * 2\""));
    REQUIRE(cyclic.has_value());
    Regenerator second;
    const RegenerationReport cycle = requireReport(second, *cyclic);
    REQUIRE(cycle.cycles.size() == 1);
    CHECK(cycle.cycles.front() == std::vector<ObjectId>{m.holeY, m.height});
}

// examples/models/driven_plate.bcad is the regenerated DrivenPlateModel. It
// serves the CLI process tests, and this test keeps it in step with the
// builder.
TEST_CASE("ParameterExpressions_ExampleFileMatchesTheBuilder", "[io][expressions][example][p12]") {
    DrivenPlateModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const auto text = io::documentToJson(m.doc);
    REQUIRE(text.has_value());
    CHECK(*text == readFile(kExamples / "driven_plate.bcad"));
}
