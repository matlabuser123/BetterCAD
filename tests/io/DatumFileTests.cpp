#include "features/FeatureTestSupport.hpp"
#include "support/DatumModels.hpp"
#include "support/TestFiles.hpp"
#include "support/occt/StepReadBack.hpp"

#include <bettercad/features/Datums.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/io/ModelExport.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <bit>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

using namespace bettercad;
using namespace bettercad::features;
using bettercad::test::DatumBlockModel;
using bettercad::test::readFile;
using bettercad::test::readStepFile;
using bettercad::test::requireReport;
using bettercad::test::TempDir;
using bettercad::test::volumeMm3;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;

// P12-DATUM-001: datum planes, datum axes, coordinate systems, sketch
// attachments and datum references in the native format.

namespace {

const std::filesystem::path kExamples{BETTERCAD_EXAMPLE_MODELS_DIR};

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

} // namespace

TEST_CASE("DatumFile_SaveLoad_PreservesDatumsAndRebuildsTheSameGeometry", "[io][datum][p12][acceptance]") {
    TempDir dir;
    const auto path = dir.path() / "datum_block.bcad";
    auto original = std::make_unique<DatumBlockModel>();
    Regenerator before;
    REQUIRE(requireReport(before, original->doc).succeeded());
    const Document expected = original->doc.clone();
    const DatumBlockModel ids;
    REQUIRE(io::saveDocument(original->doc, path).has_value());
    const double pattern = volumeMm3(before, ids.pattern);
    const double peg = volumeMm3(before, ids.peg);
    const double fin = volumeMm3(before, ids.fin);
    original.reset();

    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    CHECK(equivalent(expected, *loaded));
    Regenerator after;
    const RegenerationReport report = requireReport(after, *loaded);
    REQUIRE(report.succeeded());
    CHECK(bits(volumeMm3(after, ids.pattern)) == bits(pattern));
    CHECK(bits(volumeMm3(after, ids.peg)) == bits(peg));
    CHECK(bits(volumeMm3(after, ids.fin)) == bits(fin));
    // The saved placements were the regenerated ones: nothing moves.
    CHECK(equivalent(expected, *loaded));

    const std::string text = readFile(path);
    CHECK_THAT(text, ContainsSubstring("\"type\": \"datum_plane\","));
    CHECK_THAT(text, ContainsSubstring("\"type\": \"datum_axis\","));
    CHECK_THAT(text, ContainsSubstring("\"type\": \"coordinate_system\","));
    CHECK_THAT(text, ContainsSubstring("\"attachment\": {"));
    CHECK_THAT(text, ContainsSubstring("\"offset_parameter\": 1"));
    CHECK_THAT(text, ContainsSubstring("\"reference\": {"));
    CHECK(io::documentToJson(*loaded).value() == text);
}

TEST_CASE("DatumFile_ExampleFileMatchesTheBuilder", "[io][datum][example][p12]") {
    DatumBlockModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const auto text = io::documentToJson(m.doc);
    REQUIRE(text.has_value());
    CHECK(*text == readFile(kExamples / "datum_block.bcad"));
}

TEST_CASE("DatumFile_MalformedDatumsAreRejectedWithThePath", "[io][datum][p12]") {
    DatumBlockModel m;
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
    CHECK_THAT(loadError("\"kind\": \"offset\"", "\"kind\": \"sideways\""),
               ContainsSubstring(".data.kind: unknown value 'sideways'"));
    CHECK_THAT(loadError("\"plane\": \"xy\"", "\"plane\": \"uv\""),
               ContainsSubstring(".plane: unknown value 'uv'"));
    CHECK_THAT(loadError("\"kind\": \"intersection\"", "\"kind\": \"fixed\""),
               ContainsSubstring(".data.first: unknown field"));
    CHECK_THAT(loadError("\"offset_parameter\": 1", "\"offset_parameter\": \"height\""),
               ContainsSubstring(".offset_parameter: expected an ID"));
    CHECK_THAT(loadError("\"rz\": ", "\"spin\": 1, \"rz\": "), ContainsSubstring(".data.spin: unknown field"));
    CHECK_THAT(loadError("\"attachment\": {", "\"attachment\": {\"extra\": 1, "),
               ContainsSubstring(".attachment.extra: unknown field"));
    // A pattern axis given by a reference has no origin or direction.
    CHECK_THAT(loadError("\"reference\": {", "\"origin\": [0, 0, 0], \"reference\": {"),
               ContainsSubstring("a pattern axis given by a reference has no origin or direction"));
}

TEST_CASE("DatumFile_ExportedBodiesReadBackFromStep", "[io][datum][step][p12]") {
    TempDir dir;
    const auto path = dir.path() / "datum_block.step";
    DatumBlockModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const double total = volumeMm3(regenerator, m.pattern) + volumeMm3(regenerator, m.peg) +
                         volumeMm3(regenerator, m.fin);
    const auto summary = io::exportStep(m.doc, path);
    REQUIRE(summary.has_value());
    CHECK(summary->bodies.size() == 3);
    const auto contents = readStepFile(path);
    REQUIRE(contents.has_value());
    CHECK(contents->valid);
    CHECK(contents->solids == 3);
    // STEP stores decimal text: within 1e-9, as for the other exports.
    CHECK_THAT(contents->volumeMm3, WithinRel(total, 1e-9));
    CHECK_THAT(contents->maxMm[0], WithinRel(155.0, 1e-9));
}
