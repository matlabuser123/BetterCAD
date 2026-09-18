#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/BracketModel.hpp"
#include "support/SweepModels.hpp"
#include "support/TestFiles.hpp"
#include "support/occt/StepReadBack.hpp"

#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/features/Regeneration.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/SweepFeature.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/io/ModelExport.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <bit>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using bettercad::test::describe;
using bettercad::test::GuidedBarModel;
using bettercad::test::readFile;
using bettercad::test::requireReport;
using bettercad::test::SpatialBarModel;
using bettercad::test::TempDir;
using bettercad::test::TwistedBarModel;
using bettercad::test::volumeMm3;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;

// P12-SWEEP-001: the runs of a spatial path, the twist and the guide survive
// the file, and a sweep written before this milestone still means what it
// meant.

namespace {

constexpr double kRel = 1e-12;
constexpr double kRelGuided = 1e-5;
// STEP stores coordinates as decimal text; a volume computed from a re-read
// file agrees with the original to well within this (as in StepExportTests).
constexpr double kRelStep = 1e-9;

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

void checkSameGeometry(const Regenerator& expected, const Regenerator& actual, ObjectId feature) {
    INFO("feature " << feature);
    const geometry::Body* a = expected.body(feature);
    const geometry::Body* b = actual.body(feature);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    const auto pa = a->massProperties().value();
    const auto pb = b->massProperties().value();
    CHECK(bits(pa.volume.si()) == bits(pb.volume.si()));
    CHECK(bits(pa.surfaceArea.si()) == bits(pb.surfaceArea.si()));
    CHECK(bits(pa.centerOfMass.x.si()) == bits(pb.centerOfMass.x.si()));
    CHECK(bits(pa.centerOfMass.y.si()) == bits(pb.centerOfMass.y.si()));
    CHECK(bits(pa.centerOfMass.z.si()) == bits(pb.centerOfMass.z.si()));
    CHECK(a->boundingBox().value() == b->boundingBox().value());
    CHECK(a->topology() == b->topology());
}

Document saveDestroyLoad(Document& doc, const std::filesystem::path& path) {
    const Document expected = doc.clone();
    REQUIRE(io::saveDocument(doc, path).has_value());
    doc = Document("Closed");

    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    CHECK(equivalent(expected, *loaded));
    CHECK(loaded->itemIds() == expected.itemIds());
    const DocumentGraph a = buildDependencyGraph(expected);
    const DocumentGraph b = buildDependencyGraph(*loaded);
    REQUIRE(a.graph.nodes() == b.graph.nodes());
    for (const ObjectId node : a.graph.nodes()) {
        CHECK(a.graph.dependenciesOf(node) == b.graph.dependenciesOf(node));
    }
    return std::move(*loaded);
}

} // namespace

TEST_CASE("SpatialSweep_SaveLoadPreservesEveryRun", "[sweep][io][p12][acceptance]") {
    TempDir dir;
    SpatialBarModel m;
    Regenerator before;
    const RegenerationReport built = requireReport(before, m.doc);
    INFO(describe(built));
    REQUIRE(built.succeeded());
    const SweepDefinition definition = m.definition();

    const auto text = io::documentToJson(m.doc);
    REQUIRE(text.has_value());
    CHECK_THAT(*text, ContainsSubstring(R"("runs": [)"));

    Document loaded = saveDestroyLoad(m.doc, dir.path() / "bar.bcad");
    const SweepDefinition& read = loaded.findObjectAs<SweepFeature>(m.bar)->definition();
    CHECK(read == definition);
    REQUIRE(read.path.runs.size() == 2);
    CHECK(read.path.runs[0].sketch == SketchId::fromValue(m.cross.value()));
    CHECK(read.path.runs[1].sketch == SketchId::fromValue(m.turn.value()));
    CHECK(read.path.runs[0].edges == std::vector<EntityId>{m.crossLine});
    // The resolved path is the same path, run for run.
    CHECK(resolveSweptPath(read, loaded).value() == resolveSweptPath(definition, loaded).value());

    Regenerator after;
    const RegenerationReport rebuilt = requireReport(after, loaded);
    INFO(describe(rebuilt));
    REQUIRE(rebuilt.succeeded());
    CHECK(rebuilt.regenerated == built.regenerated);
    checkSameGeometry(before, after, m.bar);
}

TEST_CASE("SpatialSweep_SaveLoadPreservesTheTwist", "[sweep][io][p12][acceptance]") {
    TempDir dir;
    TwistedBarModel m;
    Regenerator before;
    REQUIRE(requireReport(before, m.doc).succeeded());

    SECTION("driven by a parameter") {
        const auto text = io::documentToJson(m.doc);
        REQUIRE(text.has_value());
        CHECK_THAT(*text, ContainsSubstring(R"("twist_parameter": 1)"));

        Document loaded = saveDestroyLoad(m.doc, dir.path() / "twist.bcad");
        const SweepDefinition& read = loaded.findObjectAs<SweepFeature>(m.twisted)->definition();
        CHECK(read.twistParameter == m.twist);
        CHECK(read.twist == Angle{});
        Regenerator after;
        REQUIRE(requireReport(after, loaded).succeeded());
        checkSameGeometry(before, after, m.twisted);

        // Still parametric: the loaded sweep follows its twist parameter.
        REQUIRE(loaded.setParameterValue(m.twist, 180_deg).has_value());
        REQUIRE(requireReport(after, loaded).succeeded());
        CHECK_THAT(volumeMm3(after, m.twisted), WithinRel(TwistedBarModel::expectedVolume(), kRelGuided));
    }
    SECTION("a literal angle") {
        SweepDefinition d = m.definition();
        d.twistParameter.reset();
        d.twist = 180_deg;
        m.setDefinition(d);
        Regenerator literal;
        REQUIRE(requireReport(literal, m.doc).succeeded());
        const auto text = io::documentToJson(m.doc);
        REQUIRE(text.has_value());
        CHECK_THAT(*text, ContainsSubstring(R"("twist": 3.14159)"));

        Document loaded = saveDestroyLoad(m.doc, dir.path() / "twist.bcad");
        const SweepDefinition& read = loaded.findObjectAs<SweepFeature>(m.twisted)->definition();
        CHECK(read.twist == 180_deg);
        CHECK_FALSE(read.twistParameter.has_value());
        Regenerator after;
        REQUIRE(requireReport(after, loaded).succeeded());
        checkSameGeometry(literal, after, m.twisted);
    }
}

TEST_CASE("SpatialSweep_SaveLoadPreservesTheGuide", "[sweep][io][p12][acceptance]") {
    TempDir dir;
    GuidedBarModel m;
    Regenerator before;
    REQUIRE(requireReport(before, m.doc).succeeded());
    // Taken before the document is destroyed by the round trip.
    const SweepDefinition definition = m.definition();

    const auto text = io::documentToJson(m.doc);
    REQUIRE(text.has_value());
    CHECK_THAT(*text, ContainsSubstring(R"("guide": {)"));

    Document loaded = saveDestroyLoad(m.doc, dir.path() / "guide.bcad");
    const SweepDefinition& read = loaded.findObjectAs<SweepFeature>(m.guided)->definition();
    REQUIRE(read.guide.has_value());
    CHECK(read.guide->sketch == SketchId::fromValue(m.lead.value()));
    CHECK(read.guide->edges == std::vector<EntityId>{m.leadLine});
    CHECK(read == definition);

    Regenerator after;
    REQUIRE(requireReport(after, loaded).succeeded());
    checkSameGeometry(before, after, m.guided);
}

TEST_CASE("SpatialSweep_AFileWithoutTheNewFieldsIsAPlanarSweep", "[sweep][io][p12]") {
    // A sweep written before P12-SWEEP-001 has no runs, no twist and no
    // guide: it must not be read as having any, and writing it back must
    // not add them.
    TempDir dir;
    bettercad::test::ChannelModel m;
    const auto path = dir.path() / "old.bcad";
    REQUIRE(io::saveDocument(m.doc, path).has_value());
    const std::string original = readFile(path);
    CHECK_THAT(original, !ContainsSubstring("\"runs\""));
    CHECK_THAT(original, !ContainsSubstring("\"twist\""));
    CHECK_THAT(original, !ContainsSubstring("\"guide\""));
    CHECK_THAT(original, !ContainsSubstring("along_sketch"));

    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    const SweepDefinition& read = loaded->findObjectAs<SweepFeature>(m.channel)->definition();
    CHECK(read.path.runs.empty());
    CHECK(read.twist == Angle{});
    CHECK_FALSE(read.twistParameter.has_value());
    CHECK_FALSE(read.guide.has_value());
    CHECK(read == m.doc.findObjectAs<SweepFeature>(m.channel)->definition());

    // Written back byte for byte.
    const auto again = dir.path() / "again.bcad";
    REQUIRE(io::saveDocument(*loaded, again).has_value());
    CHECK(readFile(again) == original);
}

TEST_CASE("SpatialSweep_RebuildsIdenticallyFromAFile", "[sweep][io][p12][determinism]") {
    TempDir dir;
    SECTION("a spatial path") {
        SpatialBarModel m;
        Regenerator before;
        REQUIRE(requireReport(before, m.doc).succeeded());
        Document loaded = saveDestroyLoad(m.doc, dir.path() / "round.bcad");
        Regenerator after;
        REQUIRE(requireReport(after, loaded).succeeded());
        checkSameGeometry(before, after, m.bar);
    }
    SECTION("a twisted sweep") {
        TwistedBarModel m;
        Regenerator before;
        REQUIRE(requireReport(before, m.doc).succeeded());
        Document loaded = saveDestroyLoad(m.doc, dir.path() / "round.bcad");
        Regenerator after;
        REQUIRE(requireReport(after, loaded).succeeded());
        checkSameGeometry(before, after, m.twisted);
    }
    SECTION("a guided sweep") {
        GuidedBarModel m;
        Regenerator before;
        REQUIRE(requireReport(before, m.doc).succeeded());
        Document loaded = saveDestroyLoad(m.doc, dir.path() / "round.bcad");
        Regenerator after;
        REQUIRE(requireReport(after, loaded).succeeded());
        checkSameGeometry(before, after, m.guided);
    }
}

TEST_CASE("SpatialSweep_ExportsStep", "[sweep][io][export][p12][acceptance]") {
    // The kernel reads the exported file back (test tooling only: STEP
    // import is not a product feature) and the solid measures the same.
    TempDir dir;
    const auto exported = [&](Document& doc, ObjectId feature, const std::string& name, double expected,
                              double tolerance) {
        Regenerator regenerator;
        const RegenerationReport report = requireReport(regenerator, doc);
        INFO(describe(report));
        REQUIRE(report.succeeded());
        CHECK_THAT(volumeMm3(regenerator, feature), WithinRel(expected, tolerance));
        const auto path = dir.path() / name;
        REQUIRE(io::exportStep(doc, path).has_value());
        const auto read = bettercad::test::readStepFile(path);
        REQUIRE(read.has_value());
        INFO(name << ": exported " << volumeMm3(regenerator, feature) << " mm^3, read back " << read->volumeMm3);
        CHECK(read->solids == 1);
        CHECK(read->valid);
        CHECK_THAT(read->volumeMm3, WithinRel(volumeMm3(regenerator, feature), kRelStep));
    };

    SECTION("a spatial path") {
        SpatialBarModel m;
        exported(m.doc, m.bar, "bar.step", SpatialBarModel::expectedVolume(), kRel);
    }
    SECTION("a twisted sweep") {
        TwistedBarModel m;
        exported(m.doc, m.twisted, "twist.step", TwistedBarModel::expectedVolume(), kRelGuided);
    }
    SECTION("a guided sweep") {
        GuidedBarModel m;
        exported(m.doc, m.guided, "guide.step", GuidedBarModel::expectedVolume(), kRelGuided);
    }
}
