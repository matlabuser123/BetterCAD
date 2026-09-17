#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/BracketModel.hpp"
#include "support/PatternModels.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/features/CircularPatternFeature.hpp>
#include <bettercad/features/LinearPatternFeature.hpp>
#include <bettercad/features/Regeneration.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <bit>
#include <cstdint>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using bettercad::test::BoltCircleModel;
using bettercad::test::CubeRowModel;
using bettercad::test::describe;
using bettercad::test::readFile;
using bettercad::test::requireReport;
using bettercad::test::TempDir;
using bettercad::test::volumeMm3;
using bettercad::test::writeFile;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;

// P12-PATTERN-001: the distribution, the symmetry, the suppressed instances
// and a nested pattern's reference to its source all survive the file, and a
// pattern written before the milestone still reads as the pattern it was.

namespace {

constexpr double kRel = 1e-12;
constexpr double kCubeMm3 = 1000.0;

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

/// Saves, destroys and loads @p doc, checking the model, its IDs, its order
/// and its dependency graph are unchanged.
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

} // namespace

TEST_CASE("LinearPattern_SaveLoadPreservesDistributionSymmetryAndSuppression", "[pattern][io][p12][acceptance]") {
    TempDir dir;
    CubeRowModel m;
    LinearPatternDefinition d = m.definitionOf(m.row);
    d.first.countParameter.reset();
    d.first.spacingParameter.reset();
    d.first.count = 7;
    d.first.spacing = 120_mm;
    d.first.distribution = PatternDistribution::TotalLength;
    d.first.symmetric = true;
    d.second = PatternDirection{.direction = {0.0, 1.0, 0.0}, .count = 3, .spacing = 30_mm};
    d.suppressed = {2, 5, 17};
    m.setDefinition(m.row, d);

    Regenerator before;
    const RegenerationReport built = requireReport(before, m.doc);
    INFO(describe(built));
    REQUIRE(built.succeeded());
    // 21 instances, three of them suppressed.
    CHECK_THAT(volumeMm3(before, m.row), WithinRel(18.0 * kCubeMm3, kRel));

    const auto text = io::documentToJson(m.doc);
    REQUIRE(text.has_value());
    CHECK_THAT(*text, ContainsSubstring(R"("distribution": "total_length")"));
    CHECK_THAT(*text, ContainsSubstring(R"("symmetric": true)"));
    CHECK_THAT(*text, ContainsSubstring(R"("suppressed": [
          2,
          5,
          17
        ])"));

    Document loaded = saveDestroyLoad(m.doc, dir.path() / "row.bcad");
    const LinearPatternDefinition& read = loaded.findObjectAs<LinearPatternFeature>(m.row)->definition();
    CHECK(read == d);
    CHECK(read.first.distribution == PatternDistribution::TotalLength);
    CHECK(read.first.symmetric);
    CHECK(read.suppressed == std::vector<std::uint32_t>{2, 5, 17});
    CHECK(resolvePatternInstances(read, loaded).value() == resolvePatternInstances(d, loaded).value());

    Regenerator after;
    const RegenerationReport rebuilt = requireReport(after, loaded);
    INFO(describe(rebuilt));
    REQUIRE(rebuilt.succeeded());
    CHECK(rebuilt.regenerated == built.regenerated);
    checkSameGeometry(before, after, m.row);
}

TEST_CASE("CircularPattern_SaveLoadPreservesSymmetryAndSuppression", "[pattern][circular][io][p12]") {
    TempDir dir;
    BoltCircleModel m;
    CircularPatternDefinition d = m.definitionOf(m.bolts);
    d.countParameter.reset();
    d.count = 5;
    d.spacing = CircularSpacing::AngleStep;
    d.angle = 30_deg;
    d.symmetric = true;
    d.suppressed = {3};
    m.setDefinition(m.bolts, d);

    Regenerator before;
    REQUIRE(requireReport(before, m.doc).succeeded());
    CHECK_THAT(volumeMm3(before, m.bolts), WithinRel(BoltCircleModel::expectedVolume(60, 10, 10, 4), kRel));

    Document loaded = saveDestroyLoad(m.doc, dir.path() / "bolts.bcad");
    const CircularPatternDefinition& read = loaded.findObjectAs<CircularPatternFeature>(m.bolts)->definition();
    CHECK(read == d);
    CHECK(read.symmetric);
    CHECK(read.suppressed == std::vector<std::uint32_t>{3});
    Regenerator after;
    REQUIRE(requireReport(after, loaded).succeeded());
    checkSameGeometry(before, after, m.bolts);
}

TEST_CASE("LinearPattern_SaveLoadPreservesANestedPattern", "[pattern][nesting][io][p12][acceptance]") {
    TempDir dir;
    CubeRowModel m;
    LinearPatternDefinition row = m.definitionOf(m.row);
    row.first.countParameter.reset();
    row.first.spacingParameter.reset();
    row.first.count = 3;
    row.first.spacing = 20_mm;
    m.setDefinition(m.row, row);
    const ObjectId grid = m.addPattern("Grid", {.source = CubeRowModel::featureId(m.row),
                                                .first = {.direction = {0.0, 1.0, 0.0},
                                                          .count = 4,
                                                          .spacing = 30_mm}});
    Regenerator before;
    const RegenerationReport built = requireReport(before, m.doc);
    INFO(describe(built));
    REQUIRE(built.succeeded());
    CHECK_THAT(volumeMm3(before, grid), WithinRel(12.0 * kCubeMm3, kRel));
    // The cube at (20, 30) is named by both steps, innermost first.
    const FaceName nested{m.cube, {.role = FaceRole::StartCap,
                                   .copies = {FaceCopy{m.row, 1}, FaceCopy{grid, 1}}}};
    const auto faces = geometry::findNamedFaces(*before.body(grid), nested);
    REQUIRE(faces.has_value());
    REQUIRE(faces->size() == 1);

    Document loaded = saveDestroyLoad(m.doc, dir.path() / "grid.bcad");
    // The outer pattern still points at the inner one, which still points
    // at the cube: the chain of sources survives.
    const LinearPatternDefinition& outer = loaded.findObjectAs<LinearPatternFeature>(grid)->definition();
    CHECK(outer.source == CubeRowModel::featureId(m.row));
    CHECK(loaded.findObjectAs<LinearPatternFeature>(m.row)->definition().source ==
          CubeRowModel::featureId(m.cube));

    Regenerator after;
    const RegenerationReport rebuilt = requireReport(after, loaded);
    INFO(describe(rebuilt));
    REQUIRE(rebuilt.succeeded());
    CHECK(rebuilt.regenerated == built.regenerated);
    checkSameGeometry(before, after, grid);
    // And the nested copy is still named the same way in the rebuilt body.
    const auto reloaded = geometry::findNamedFaces(*after.body(grid), nested);
    REQUIRE(reloaded.has_value());
    REQUIRE(reloaded->size() == 1);
    CHECK(reloaded->front().centroid == faces->front().centroid);
}

TEST_CASE("LinearPattern_AFileWithoutTheNewFieldsIsAPlainPattern", "[pattern][io][p12]") {
    // A pattern written before P12-PATTERN-001 has no distribution, no
    // symmetry and no suppressed instances: it must not be read as one that
    // has them, and writing it back must not add them.
    TempDir dir;
    CubeRowModel m;
    const auto path = dir.path() / "old.bcad";
    REQUIRE(io::saveDocument(m.doc, path).has_value());
    const std::string original = readFile(path);
    CHECK_THAT(original, !ContainsSubstring("distribution"));
    CHECK_THAT(original, !ContainsSubstring("symmetric"));
    CHECK_THAT(original, !ContainsSubstring("suppressed"));

    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    const LinearPatternDefinition& read = loaded->findObjectAs<LinearPatternFeature>(m.row)->definition();
    CHECK(read.first.distribution == PatternDistribution::Spacing);
    CHECK_FALSE(read.first.symmetric);
    CHECK(read.suppressed.empty());
    CHECK(read == m.definitionOf(m.row));

    // Written back byte for byte.
    const auto again = dir.path() / "again.bcad";
    REQUIRE(io::saveDocument(*loaded, again).has_value());
    CHECK(readFile(again) == original);
}

TEST_CASE("LinearPattern_MalformedInstanceFieldsAreRejectedWithTheJsonPath", "[pattern][io][p12]") {
    TempDir dir;
    CubeRowModel m;
    LinearPatternDefinition d = m.definitionOf(m.row);
    d.first.distribution = PatternDistribution::TotalLength;
    d.first.symmetric = true;
    d.suppressed = {2};
    m.setDefinition(m.row, d);
    const auto path = dir.path() / "row.bcad";
    REQUIRE(io::saveDocument(m.doc, path).has_value());
    const std::string good = readFile(path);

    const auto messageFor = [&](std::string_view from, std::string_view to) {
        const auto pos = good.find(from);
        REQUIRE(pos != std::string::npos);
        std::string broken = good;
        broken.replace(pos, from.size(), to);
        const auto other = dir.path() / "broken.bcad";
        writeFile(other, broken);
        auto result = io::loadDocument(other);
        REQUIRE_FALSE(result.has_value());
        return result.error().message;
    };

    CHECK_THAT(messageFor(R"("distribution": "total_length")", R"("distribution": "spread")"),
               ContainsSubstring("distribution"));
    CHECK_THAT(messageFor(R"("symmetric": true)", R"("symmetric": "yes")"), ContainsSubstring("symmetric"));
    CHECK_THAT(messageFor(R"("suppressed": [
          2
        ])",
                          R"("suppressed": [
          -2
        ])"),
               ContainsSubstring("suppressed[0]"));
    CHECK_THAT(messageFor(R"("suppressed": [
          2
        ])",
                          R"("suppressed": [
          0
        ])"),
               ContainsSubstring("cannot suppress instance 0"));
}

TEST_CASE("LinearPattern_RebuildsIdenticallyFromAFile", "[pattern][io][p12][determinism]") {
    // Build, save, destroy, load, regenerate: the same numbers, bit for bit,
    // for a symmetric row, a row given a total length, a row with suppressed
    // instances, and a pattern of a pattern.
    TempDir dir;
    for (const bool symmetric : {false, true}) {
        for (const auto distribution : {PatternDistribution::Spacing, PatternDistribution::TotalLength}) {
            INFO("symmetric " << symmetric << ", total length "
                              << (distribution == PatternDistribution::TotalLength));
            CubeRowModel m;
            LinearPatternDefinition d = m.definitionOf(m.row);
            d.first.countParameter.reset();
            d.first.spacingParameter.reset();
            d.first.count = 5;
            d.first.spacing = 100_mm;
            d.first.distribution = distribution;
            d.first.symmetric = symmetric;
            d.suppressed = {3};
            m.setDefinition(m.row, d);
            const ObjectId grid = m.addPattern("Grid", {.source = CubeRowModel::featureId(m.row),
                                                        .first = {.direction = {0.0, 1.0, 0.0},
                                                                  .count = 3,
                                                                  .spacing = 40_mm}});
            Regenerator before;
            REQUIRE(requireReport(before, m.doc).succeeded());

            Document loaded = saveDestroyLoad(m.doc, dir.path() / "round.bcad");
            Regenerator after;
            REQUIRE(requireReport(after, loaded).succeeded());
            checkSameGeometry(before, after, m.row);
            checkSameGeometry(before, after, grid);
        }
    }
}
