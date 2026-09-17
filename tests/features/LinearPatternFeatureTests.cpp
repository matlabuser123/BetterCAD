#include "FeatureTestSupport.hpp"
#include "TestHelpers.hpp"
#include "core/geometry/GeometryTestSupport.hpp"
#include "support/BracketModel.hpp"
#include "support/FilletModels.hpp"
#include "support/HoleModels.hpp"
#include "support/PatternModels.hpp"
#include "support/TurnedPartModel.hpp"

#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/features/ChamferFeature.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/FeatureCommands.hpp>
#include <bettercad/features/FilletFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/LinearPatternFeature.hpp>
#include <bettercad/features/MirrorFeature.hpp>
#include <bettercad/features/Regeneration.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/ResultBodies.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <numbers>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using bettercad::test::BlockModel;
using bettercad::test::bottomFace;
using bettercad::test::BossRowModel;
using bettercad::test::countOf;
using bettercad::test::CubeRowModel;
using bettercad::test::describe;
using bettercad::test::errorCode;
using bettercad::test::filletCorner;
using bettercad::test::HoleBlockModel;
using bettercad::test::HoleRowModel;
using bettercad::test::kPositionToleranceMm;
using bettercad::test::requireReport;
using bettercad::test::topFace;
using bettercad::test::TurnedPartModel;
using bettercad::test::volumeMm3;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr double pi = std::numbers::pi;
// Patterns of boxes, holes and turned parts have planar, cylindrical and
// conical faces meeting in lines and circles, which the kernel computes to
// rounding level (kRelTight, as for the hole and boolean tests).
constexpr double kRel = bettercad::test::kRelTight;

FeatureId featureId(ObjectId id) {
    return FeatureId::fromValue(id.value());
}

/// Regenerates; exactly @p feature must fail, keeping no body. Returns its error.
Error requireFailure(Regenerator& regenerator, Document& doc, ObjectId feature) {
    const RegenerationReport report = requireReport(regenerator, doc);
    INFO(describe(report));
    REQUIRE(report.failed == std::vector<ObjectId>{feature});
    CHECK(regenerator.state(feature) == NodeState::Failed);
    CHECK(regenerator.body(feature) == nullptr);
    return report.errors.at(feature);
}

const geometry::Body& requireBody(const Regenerator& regenerator, ObjectId feature) {
    const geometry::Body* body = regenerator.body(feature);
    REQUIRE(body != nullptr);
    return *body;
}

std::size_t facesOn(const Regenerator& regenerator, ObjectId feature, const geometry::FaceSignature& face) {
    const auto found = geometry::findFaces(requireBody(regenerator, feature), face);
    REQUIRE(found.has_value());
    return found->size();
}

double areaOn(const Regenerator& regenerator, ObjectId feature, const geometry::FaceSignature& face) {
    const auto found = geometry::findFaces(requireBody(regenerator, feature), face);
    REQUIRE(found.has_value());
    double area = 0.0;
    for (const geometry::FaceInfo& info : *found) {
        area += info.area.in(units::mm2);
    }
    return area;
}

/// A face on the plane x = @p xMm facing -X (the left side of a cube there).
geometry::FaceSignature leftFace(double xMm) {
    return geometry::planeSignature(Point3D{xMm * units::mm, 0_mm, 0_mm}, Direction3D::unitX().reversed());
}
/// A face on the plane x = @p xMm facing +X.
geometry::FaceSignature rightFace(double xMm) {
    return geometry::planeSignature(Point3D{xMm * units::mm, 0_mm, 0_mm}, Direction3D::unitX());
}

geometry::EdgeSignature rim(double x, double y, double z, double r) {
    const auto circle = geometry::circleSignature(Point3D{x * units::mm, y * units::mm, z * units::mm},
                                                  Direction3D::unitZ(), r * units::mm);
    REQUIRE(circle.has_value());
    return *circle;
}

std::size_t circlesOn(const Regenerator& regenerator, ObjectId feature, double x, double y, double z, double r) {
    const auto found = geometry::findEdges(requireBody(regenerator, feature), rim(x, y, z, r));
    REQUIRE(found.has_value());
    return found->size();
}

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

/// Four instances along +X, 20 mm apart, of source object 5.
LinearPatternDefinition literalRow() {
    return {.source = FeatureId::fromValue(5), .first = {.direction = {1.0, 0.0, 0.0}, .count = 4, .spacing = 20_mm}};
}

Error refusal(const LinearPatternDefinition& definition) {
    auto feature = LinearPatternFeature::create("P", definition);
    REQUIRE_FALSE(feature.has_value());
    return feature.error();
}

} // namespace

TEST_CASE("LinearPattern_DefinitionIsValidatedOnCreateAndEdit", "[pattern][features]") {
    const LinearPatternDefinition good = literalRow();
    REQUIRE(LinearPatternFeature::create("P", good).has_value());
    LinearPatternDefinition d = good;

    SECTION("a source and valid parameter IDs") {
        d.source = FeatureId{};
        CHECK(refusal(d).message == "a linear pattern needs a source feature");
        d = good;
        d.first.countParameter = ParameterId{};
        CHECK(refusal(d).message == "the pattern's parameter IDs must be valid");
    }
    SECTION("driving parameters replace the literal count and spacing") {
        d.first.count = 0;
        d.first.countParameter = ParameterId::fromValue(2);
        d.first.spacing = 0_mm;
        d.first.spacingParameter = ParameterId::fromValue(3);
        CHECK(LinearPatternFeature::create("P", d).has_value());
    }
    SECTION("a second direction for a grid, not parallel to the first") {
        d.second = PatternDirection{.direction = {0.0, 1.0, 0.0}, .count = 3, .spacing = 12_mm};
        CHECK(LinearPatternFeature::create("P", d).has_value());
        d.second->direction = {-2.0, 0.0, 0.0};
        const Error parallel = refusal(d);
        CHECK(parallel.code == ErrorCode::InvalidArgument);
        CHECK(parallel.message == "the two directions must not be parallel, got (1, 0, 0) and (-2, 0, 0)");
        d.second->direction = {0.0, 0.0, 0.0};
        CHECK(refusal(d).message == "direction 2: the direction must be a finite, non-zero vector, got (0, 0, 0)");
    }
    SECTION("an invalid edit leaves the feature unchanged") {
        auto feature = LinearPatternFeature::create("P", good);
        REQUIRE(feature.has_value());
        d.first.count = 0;
        CHECK(errorCode((*feature)->setDefinition(d)) == ErrorCode::InvalidArgument);
        CHECK((*feature)->definition() == good);
        const auto unchanged = (*feature)->setDefinition(good);
        REQUIRE(unchanged.has_value());
        CHECK_FALSE(*unchanged);
    }
}

TEST_CASE("LinearPattern_DependsOnItsSourceAndParameters", "[pattern][features]") {
    CubeRowModel m;
    const auto* row = m.doc.findObjectAs<LinearPatternFeature>(m.row);
    REQUIRE(row != nullptr);
    CHECK(row->dependencies() == std::vector<ObjectId>{m.cube, ObjectId{m.count}, ObjectId{m.pitch}});
    CHECK(row->target() == featureId(m.cube)); // the pattern consumes its source
    CHECK(row->typeName() == "linear_pattern");
    CHECK(equivalent(*row->clone(), *row));

    // A second direction's parameters come after the first's.
    const ParameterId rows = m.doc.createParameter("rows", 2.0, kUnitless).value();
    LinearPatternDefinition d = m.definitionOf(m.row);
    d.first.countParameter.reset();
    d.second = PatternDirection{.direction = {0.0, 1.0, 0.0}, .count = 2, .countParameter = rows, .spacing = 15_mm};
    m.setDefinition(m.row, d);
    CHECK(m.doc.findObjectAs<LinearPatternFeature>(m.row)->dependencies() ==
          std::vector<ObjectId>{m.cube, ObjectId{m.pitch}, ObjectId{rows}});
}

TEST_CASE("LinearPattern_CountIncludesOriginal", "[pattern][features][acceptance]") {
    // count = N: the source and N - 1 copies.
    const PatternStep step{.direction = Direction3D::unitX(), .count = 1, .spacing = 20_mm};
    const auto one = patternInstances(step);
    REQUIRE(one.size() == 1);
    CHECK(one[0] == PatternInstance{.index = 0, .first = 0, .second = 0, .offset = Translation3D{}});
    PatternStep two = step;
    two.count = 2;
    const auto pair = patternInstances(two);
    REQUIRE(pair.size() == 2);
    CHECK(pair[1].offset == Translation3D{20_mm, 0_mm, 0_mm});

    CubeRowModel m;
    Regenerator regenerator;
    for (const double count : {1.0, 2.0, 4.0}) {
        CAPTURE(count);
        REQUIRE(m.doc.setParameterValue(m.count, count, kUnitless).has_value());
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        REQUIRE(report.succeeded());
        CHECK(requireBody(regenerator, m.row).topology().solids == static_cast<std::size_t>(count));
        CHECK_THAT(volumeMm3(regenerator, m.row), WithinRel(1000.0 * count, kRel));
    }
    // Count 1 is the source alone: the same geometry as the cube.
    REQUIRE(m.doc.setParameterValue(m.count, 1.0, kUnitless).has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(requireBody(regenerator, m.row).boundingBox().value() ==
          requireBody(regenerator, m.cube).boundingBox().value());
}

TEST_CASE("LinearPattern_SingleDirectionPlacesInstancesExactly", "[pattern][features][acceptance]") {
    CubeRowModel m;
    // The instances in order, each offset k s d_hat from the source.
    const auto instances = resolvePatternInstances(m.definitionOf(m.row), m.doc);
    REQUIRE(instances.has_value());
    REQUIRE(instances->size() == 4);
    for (std::size_t k = 0; k < 4; ++k) {
        CAPTURE(k);
        const PatternInstance& instance = (*instances)[k];
        CHECK(instance.index == k);
        CHECK(instance.first == k);
        CHECK(instance.second == 0);
        CHECK(instance.offset.x == 20_mm * static_cast<double>(k));
        CHECK(instance.offset.y == 0_mm);
        CHECK(instance.offset.z == 0_mm);
    }

    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    CHECK(report.regenerated == std::vector<ObjectId>{m.sketch, m.cube, m.row});
    const geometry::Body& body = requireBody(regenerator, m.row);
    CHECK(body.isValid());
    CHECK(body.topology().solids == 4);
    // A cube's sides at x = 0, 20, 40, 60 and 10 mm further: one face each,
    // and none before the first instance or after the last.
    for (const double x : {0.0, 20.0, 40.0, 60.0}) {
        CAPTURE(x);
        CHECK(facesOn(regenerator, m.row, leftFace(x)) == 1);
        CHECK(facesOn(regenerator, m.row, rightFace(x + 10.0)) == 1);
    }
    CHECK(facesOn(regenerator, m.row, leftFace(80)) == 0);
    CHECK(facesOn(regenerator, m.row, leftFace(-20)) == 0);
    CHECK(facesOn(regenerator, m.row, topFace(10)) == 4);
}

TEST_CASE("LinearPattern_NonOverlappingBodiesMatchesAnalyticVolume", "[pattern][features][acceptance]") {
    CubeRowModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    // V = count x V_source = 4 x 10^3.
    const auto props = requireBody(regenerator, m.row).massProperties().value();
    CHECK_THAT(props.volume.in(units::mm3), WithinRel(4000.0, kRel));
    CHECK_THAT(props.surfaceArea.in(units::mm2), WithinRel(4.0 * 600.0, kRel));
    // The centre of mass is the mean of the instances': (5 + 30, 5, 5).
    CHECK_THAT(props.centerOfMass.x.in(units::mm), WithinAbs(35.0, kPositionToleranceMm));
    // The source's body is an untouched intermediate; the pattern is the result.
    CHECK_THAT(volumeMm3(regenerator, m.cube), WithinRel(1000.0, kRel));
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.row});
}

TEST_CASE("LinearPattern_BoundingBoxMatchesAnalyticExtent", "[pattern][features][acceptance]") {
    // Extent along X: size + (count - 1) x spacing.
    CubeRowModel m;
    Regenerator regenerator;
    for (const double count : {2.0, 4.0, 8.0}) {
        CAPTURE(count);
        REQUIRE(m.doc.setParameterValue(m.count, count, kUnitless).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        const auto box = requireBody(regenerator, m.row).boundingBox().value();
        bettercad::test::checkPoint(box.min, 0, 0, 0);
        bettercad::test::checkPoint(box.max, 10.0 + (count - 1.0) * 20.0, 10, 10);
    }
}

TEST_CASE("LinearPattern_TouchingAndOverlappingInstancesFuse", "[pattern][features][acceptance]") {
    // Instances of a new body are united, like the regions of one extrude:
    // disjoint ones stay separate solids, touching or overlapping ones fuse.
    CubeRowModel m;
    Regenerator regenerator;
    SECTION("touching: one 40 mm bar, no inner faces") {
        REQUIRE(m.doc.setParameterValue(m.pitch, 10_mm).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        const geometry::Body& body = requireBody(regenerator, m.row);
        CHECK(body.isValid());
        CHECK(body.topology().solids == 1);
        CHECK_THAT(volumeMm3(regenerator, m.row), WithinRel(4000.0, kRel));
        CHECK_THAT(body.boundingBox()->max.x.in(units::mm), WithinAbs(40.0, kPositionToleranceMm));
        CHECK(facesOn(regenerator, m.row, rightFace(10)) == 0);
        CHECK(facesOn(regenerator, m.row, topFace(10)) == 1);
        CHECK_THAT(areaOn(regenerator, m.row, topFace(10)), WithinRel(400.0, kRel));
    }
    SECTION("overlapping: the union, 25 mm long") {
        REQUIRE(m.doc.setParameterValue(m.pitch, 5_mm).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK(requireBody(regenerator, m.row).topology().solids == 1);
        CHECK_THAT(volumeMm3(regenerator, m.row), WithinRel(10.0 * 10.0 * 25.0, kRel));
        CHECK(requireBody(regenerator, m.row).isValid());
    }
}

TEST_CASE("LinearPattern_HolePatternMatchesAnalyticVolume", "[pattern][features][hole][acceptance]") {
    HoleRowModel m;
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    const geometry::Body& body = requireBody(regenerator, m.holes);
    CHECK(body.isValid());
    CHECK(body.topology().solids == 1);
    // V = 120 x 50 x 20 - 5 pi 5^2 20.
    const double expected = HoleRowModel::expectedVolume(120, 20, 10, 5);
    CHECK_THAT(expected, WithinRel(120000.0 - 2500.0 * pi, 1e-15));
    const auto props = body.massProperties().value();
    CHECK_THAT(props.volume.in(units::mm3), WithinRel(expected, kRel));
    CHECK_THAT(props.surfaceArea.in(units::mm2),
               WithinRel(2.0 * (6000.0 + 2400.0 + 1000.0) + 5.0 * (2.0 * pi * 5.0 * 20.0 - 2.0 * 25.0 * pi), kRel));
    // Every hole is through, where the pattern puts it, and there is no sixth.
    for (const double x : {20.0, 40.0, 60.0, 80.0, 100.0}) {
        CAPTURE(x);
        CHECK(circlesOn(regenerator, m.holes, x, 25, 20, 5) == 1);
        CHECK(circlesOn(regenerator, m.holes, x, 25, 0, 5) == 1);
    }
    CHECK(circlesOn(regenerator, m.holes, 120, 25, 20, 5) == 0);
    CHECK_THAT(areaOn(regenerator, m.holes, topFace()), WithinRel(6000.0 - 5.0 * 25.0 * pi, kRel));
    // The source hole's body is an intermediate; the pattern is the result.
    CHECK_THAT(volumeMm3(regenerator, m.drill), WithinRel(HoleRowModel::expectedVolume(120, 20, 10, 1), kRel));
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.holes});
}

TEST_CASE("LinearPattern_AdditivePatternMatchesAnalyticVolume", "[pattern][features][acceptance]") {
    BossRowModel m;
    Regenerator regenerator;
    SECTION("separate bosses: V = V_base + 4 x 10 x 10 x 5") {
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        REQUIRE(report.succeeded());
        const geometry::Body& body = requireBody(regenerator, m.bosses);
        CHECK(body.isValid());
        CHECK(body.topology().solids == 1); // each boss fuses with the block
        CHECK_THAT(volumeMm3(regenerator, m.bosses), WithinRel(100000.0 + 4.0 * 500.0, kRel));
        CHECK(facesOn(regenerator, m.bosses, topFace(25)) == 4); // four boss tops
        CHECK_THAT(areaOn(regenerator, m.bosses, topFace(25)), WithinRel(400.0, kRel));
        CHECK_THAT(areaOn(regenerator, m.bosses, topFace(20)), WithinRel(5000.0 - 400.0, kRel));
        CHECK_THAT(body.boundingBox()->max.z.in(units::mm), WithinAbs(25.0, kPositionToleranceMm));
        CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.bosses});
    }
    SECTION("touching bosses fuse into one bar") {
        REQUIRE(m.doc.setParameterValue(m.pitch, 10_mm).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK_THAT(volumeMm3(regenerator, m.bosses), WithinRel(102000.0, kRel));
        CHECK(facesOn(regenerator, m.bosses, topFace(25)) == 1);
        CHECK_THAT(areaOn(regenerator, m.bosses, topFace(25)), WithinRel(400.0, kRel));
    }
    SECTION("overlapping bosses: the union, 25 mm long") {
        REQUIRE(m.doc.setParameterValue(m.pitch, 5_mm).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK(requireBody(regenerator, m.bosses).isValid());
        CHECK_THAT(volumeMm3(regenerator, m.bosses), WithinRel(100000.0 + 25.0 * 10.0 * 5.0, kRel));
    }
}

TEST_CASE("LinearPattern_SubtractiveExtrudePatternMatchesAnalyticVolume", "[pattern][features][acceptance]") {
    // The boss turned into a pocket: cut 5 mm down from the top face.
    BossRowModel m;
    m.setDefinition<ExtrudeFeature>(m.boss, {.profile = BlockModel::sketchId(m.bossSketch),
                                             .depth = 5_mm,
                                             .direction = ExtrudeDirection::Reversed,
                                             .operation = FeatureOperation::Cut,
                                             .target = BlockModel::featureId(m.pad)});
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(requireBody(regenerator, m.bosses).isValid());
    CHECK_THAT(volumeMm3(regenerator, m.bosses), WithinRel(100000.0 - 4.0 * 500.0, kRel));
    CHECK(facesOn(regenerator, m.bosses, topFace(15)) == 4); // four pocket floors
    CHECK_THAT(areaOn(regenerator, m.bosses, topFace(15)), WithinRel(400.0, kRel));
}

TEST_CASE("LinearPattern_RegeneratesWhenSourceChanges", "[pattern][features][hole][acceptance]") {
    HoleRowModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());

    SECTION("a larger hole diameter changes every hole") {
        REQUIRE(m.doc.setParameterValue(m.diameter, 12_mm).has_value());
        const RegenerationReport report = requireReport(regenerator, m.doc);
        CHECK(report.regenerated == std::vector<ObjectId>{m.drill, m.holes});
        CHECK_THAT(volumeMm3(regenerator, m.holes), WithinRel(HoleRowModel::expectedVolume(120, 20, 12, 5), kRel));
        for (const double x : {20.0, 40.0, 60.0, 80.0, 100.0}) {
            CAPTURE(x);
            CHECK(circlesOn(regenerator, m.holes, x, 25, 20, 6) == 1);
            CHECK(circlesOn(regenerator, m.holes, x, 25, 20, 5) == 0);
        }
    }
    SECTION("a thicker block: every through hole stays through") {
        // From the bottom face, the pad's start plane, which stays put.
        HoleDefinition drill = m.holeOf(m.drill);
        drill.face = bottomFace();
        m.setHole(m.drill, drill);
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        REQUIRE(m.doc.setParameterValue(m.height, 40_mm).has_value());
        const RegenerationReport report = requireReport(regenerator, m.doc);
        CHECK(report.regenerated == std::vector<ObjectId>{m.pad, m.drill, m.holes});
        CHECK_THAT(volumeMm3(regenerator, m.holes), WithinRel(HoleRowModel::expectedVolume(120, 40, 10, 5), kRel));
        for (const double x : {20.0, 40.0, 60.0, 80.0, 100.0}) {
            CAPTURE(x);
            CHECK(circlesOn(regenerator, m.holes, x, 25, 40, 5) == 1);
            CHECK(circlesOn(regenerator, m.holes, x, 25, 0, 5) == 1);
        }
    }
    SECTION("a longer block keeps the holes where they are") {
        REQUIRE(m.doc.setParameterValue(m.width, 150_mm).has_value());
        const RegenerationReport report = requireReport(regenerator, m.doc);
        CHECK(report.regenerated == std::vector<ObjectId>{m.base, m.pad, m.drill, m.holes});
        CHECK_THAT(volumeMm3(regenerator, m.holes), WithinRel(HoleRowModel::expectedVolume(150, 20, 10, 5), kRel));
    }
}

TEST_CASE("LinearPattern_RegeneratesWhenCountChanges", "[pattern][features][hole][undo][acceptance]") {
    HoleRowModel m;
    REQUIRE(m.doc.setParameterValue(m.pitch, 12_mm).has_value()); // 8 holes fit at 12 mm
    CommandHistory history;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    std::vector<double> volumes{volumeMm3(regenerator, m.holes)}; // 5 holes

    for (const double count : {2.0, 4.0, 8.0}) {
        CAPTURE(count);
        REQUIRE(history.execute(m.doc, ModifyParameterCommand::setValue(m.count, countOf(count))).has_value());
        const RegenerationReport report = requireReport(regenerator, m.doc);
        CHECK(report.regenerated == std::vector<ObjectId>{m.holes}); // the source is not rebuilt
        CHECK(regenerator.state(m.drill) == NodeState::UpToDate);
        volumes.push_back(volumeMm3(regenerator, m.holes));
        CHECK_THAT(volumes.back(), WithinRel(HoleRowModel::expectedVolume(120, 20, 10, count), kRel));
        std::size_t found = 0;
        for (int k = 0; k < 9; ++k) {
            found += circlesOn(regenerator, m.holes, 20.0 + 12.0 * k, 25, 20, 5);
        }
        CHECK(found == static_cast<std::size_t>(count));
    }
    // Undo and redo step through the same geometry, bit for bit.
    for (std::size_t step = 3; step > 0; --step) {
        REQUIRE(history.undo(m.doc).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK(bits(volumeMm3(regenerator, m.holes)) == bits(volumes[step - 1]));
    }
    for (std::size_t step = 1; step <= 3; ++step) {
        REQUIRE(history.redo(m.doc).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK(bits(volumeMm3(regenerator, m.holes)) == bits(volumes[step]));
    }
}

TEST_CASE("LinearPattern_RegeneratesWhenSpacingChanges", "[pattern][features][acceptance]") {
    SECTION("cubes 10, 15 and 25 mm apart") {
        CubeRowModel m;
        Regenerator regenerator;
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        for (const double pitch : {10.0, 15.0, 25.0}) {
            CAPTURE(pitch);
            REQUIRE(m.doc.setParameterValue(m.pitch, pitch * units::mm).has_value());
            const RegenerationReport report = requireReport(regenerator, m.doc);
            CHECK(report.regenerated == std::vector<ObjectId>{m.row});
            CHECK_THAT(volumeMm3(regenerator, m.row), WithinRel(4000.0, kRel));
            CHECK_THAT(requireBody(regenerator, m.row).boundingBox()->max.x.in(units::mm),
                       WithinAbs(10.0 + 3.0 * pitch, kPositionToleranceMm));
            // The last instance's far side (at 10 mm the cubes touch and fuse,
            // so their inner sides are gone).
            CHECK(facesOn(regenerator, m.row, rightFace(3.0 * pitch + 10.0)) == 1);
            CHECK(facesOn(regenerator, m.row, leftFace(3.0 * pitch)) == (pitch > 10.0 ? 1U : 0U));
        }
    }
    SECTION("holes 15 mm apart") {
        HoleRowModel m;
        Regenerator regenerator;
        REQUIRE(m.doc.setParameterValue(m.pitch, 15_mm).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        for (const double x : {20.0, 35.0, 50.0, 65.0, 80.0}) {
            CAPTURE(x);
            CHECK(circlesOn(regenerator, m.holes, x, 25, 20, 5) == 1);
        }
        CHECK(circlesOn(regenerator, m.holes, 100, 25, 20, 5) == 0);
        CHECK_THAT(volumeMm3(regenerator, m.holes), WithinRel(HoleRowModel::expectedVolume(120, 20, 10, 5), kRel));
    }
}

TEST_CASE("LinearPattern_NormalizesDirection", "[pattern][features][acceptance]") {
    CubeRowModel m;
    Regenerator regenerator;
    const auto along = [&](Vector3D direction) {
        LinearPatternDefinition d = m.definitionOf(m.row);
        d.first.direction = direction;
        m.setDefinition(m.row, d);
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        REQUIRE(report.succeeded());
        CHECK_THAT(volumeMm3(regenerator, m.row), WithinRel(4000.0, kRel));
        return requireBody(regenerator, m.row).boundingBox().value();
    };
    // The axes: 60 mm along each.
    bettercad::test::checkPoint(along({1.0, 0.0, 0.0}).max, 70, 10, 10);
    bettercad::test::checkPoint(along({0.0, 1.0, 0.0}).max, 10, 70, 10);
    bettercad::test::checkPoint(along({0.0, 0.0, 1.0}).max, 10, 10, 70);
    // Only the direction counts, not the vector's length.
    CHECK(along({2.0, 0.0, 0.0}) == along({1.0, 0.0, 0.0}));
    // The diagonal: 20 mm along (1, 1, 0)/sqrt(2), not 20 mm along X and Y.
    const double step = 20.0 / std::numbers::sqrt2;
    const auto diagonal = along({1.0, 1.0, 0.0});
    bettercad::test::checkPoint(diagonal.max, 10.0 + 3.0 * step, 10.0 + 3.0 * step, 10);
    const auto instances = resolvePatternInstances(m.definitionOf(m.row), m.doc);
    REQUIRE(instances.has_value());
    const Translation3D last = instances->back().offset;
    CHECK_THAT(std::hypot(last.x.in(units::mm), last.y.in(units::mm)), WithinRel(60.0, 1e-15));
    CHECK_THAT(along({3.0, 3.0, 0.0}).max.x.in(units::mm), WithinAbs(diagonal.max.x.in(units::mm), 1e-12));
}

TEST_CASE("LinearPattern_ReverseDirectionWorks", "[pattern][features][acceptance]") {
    CubeRowModel m;
    LinearPatternDefinition d = m.definitionOf(m.row);
    d.first.direction = {-1.0, 0.0, 0.0};
    m.setDefinition(m.row, d);
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK_THAT(volumeMm3(regenerator, m.row), WithinRel(4000.0, kRel));
    const auto box = requireBody(regenerator, m.row).boundingBox().value();
    bettercad::test::checkPoint(box.min, -60, 0, 0);
    bettercad::test::checkPoint(box.max, 10, 10, 10);
    for (const double x : {0.0, -20.0, -40.0, -60.0}) {
        CAPTURE(x);
        CHECK(facesOn(regenerator, m.row, leftFace(x)) == 1);
    }
    CHECK(facesOn(regenerator, m.row, leftFace(20)) == 0);
}

TEST_CASE("LinearPattern_RejectsZeroDirection", "[pattern][features][acceptance]") {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    for (const Vector3D bad : {Vector3D{0.0, 0.0, 0.0}, Vector3D{nan, 0.0, 0.0}, Vector3D{inf, 0.0, 0.0},
                               Vector3D{1.0, -inf, 0.0}}) {
        LinearPatternDefinition d = literalRow();
        d.first.direction = bad;
        const Error refused = refusal(d);
        CHECK(refused.code == ErrorCode::InvalidArgument);
        CHECK_THAT(refused.message, StartsWith("direction 1: the direction must be a finite, non-zero vector, got "));
    }
    LinearPatternDefinition d = literalRow();
    d.first.direction = {0.0, 0.0, 0.0};
    CHECK(refusal(d).message == "direction 1: the direction must be a finite, non-zero vector, got (0, 0, 0)");
    d.first.direction = {-0.0, 0.0, 0.0};
    CHECK(refusal(d).message == "direction 1: the direction must be a finite, non-zero vector, got (0, 0, 0)");
}

TEST_CASE("LinearPattern_RejectsInvalidSpacing", "[pattern][features][acceptance]") {
    for (const double mm : {0.0, -20.0, std::nan(""), std::numeric_limits<double>::infinity(),
                            -std::numeric_limits<double>::infinity()}) {
        CAPTURE(mm);
        LinearPatternDefinition d = literalRow();
        d.first.spacing = mm * units::mm;
        const Error refused = refusal(d);
        CHECK(refused.code == ErrorCode::InvalidArgument);
        CHECK_THAT(refused.message, StartsWith("direction 1: the spacing must be positive and finite, got "));
    }
    LinearPatternDefinition d = literalRow();
    d.first.spacing = -(20_mm);
    CHECK(refusal(d).message == "direction 1: the spacing must be positive and finite, got -20 mm");

    // A driving parameter of zero or less fails at regeneration; a parameter
    // cannot hold NaN or infinity at all.
    CubeRowModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    for (const Length pitch : {0_mm, -(5_mm)}) {
        REQUIRE(m.doc.setParameterValue(m.pitch, pitch).has_value());
        const Error error = requireFailure(regenerator, m.doc, m.row);
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK_THAT(error.message, StartsWith("Row: linear pattern: direction 1: the spacing must be positive and "
                                             "finite, got "));
        CHECK(regenerator.state(m.cube) == NodeState::UpToDate);
    }
    CHECK(errorCode(m.doc.setParameterValue(m.pitch, Length::fromSi(std::nan("")))) == ErrorCode::InvalidArgument);
}

TEST_CASE("LinearPattern_RejectsZeroCount", "[pattern][features][acceptance]") {
    LinearPatternDefinition d = literalRow();
    d.first.count = 0;
    const Error refused = refusal(d);
    CHECK(refused.code == ErrorCode::InvalidArgument);
    CHECK(refused.message == "direction 1: the count must be at least 1, got 0");

    // A driving parameter must hold a whole number of at least 1.
    CubeRowModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    for (const double count : {0.0, -1.0, 2.5}) {
        CAPTURE(count);
        REQUIRE(m.doc.setParameterValue(m.count, count, kUnitless).has_value());
        const Error error = requireFailure(regenerator, m.doc, m.row);
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK_THAT(error.message,
                   StartsWith("Row: linear pattern: direction 1: the count must be a whole number from 1 to 500, got "));
    }
    CHECK_THAT(requireFailure(regenerator, m.doc, m.row).message, ContainsSubstring("got 2.5"));
}

TEST_CASE("LinearPattern_RejectsTooManyInstances", "[pattern][features][acceptance]") {
    // A guard against accidental input, checked before any geometry is built.
    static_assert(kMaxPatternInstances == 500);
    LinearPatternDefinition d = literalRow();
    d.first.count = 501;
    CHECK(refusal(d).message == "a linear pattern may have at most 500 instances, got 501");
    d.first.count = 100000;
    CHECK(refusal(d).message == "a linear pattern may have at most 500 instances, got 100000");
    d.first.count = 30;
    d.second = PatternDirection{.direction = {0.0, 1.0, 0.0}, .count = 20, .spacing = 12_mm};
    CHECK(refusal(d).message == "a linear pattern may have at most 500 instances, got 600 (30 x 20)");
    d.first.count = 25;
    CHECK(LinearPatternFeature::create("P", d).has_value()); // exactly 500
    d.first.countParameter = ParameterId::fromValue(2);
    d.second->count = 501;
    CHECK(refusal(d).message == "a linear pattern may have at most 500 instances, got at least 501");

    CubeRowModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    REQUIRE(m.doc.setParameterValue(m.count, 501.0, kUnitless).has_value());
    CHECK(requireFailure(regenerator, m.doc, m.row).message ==
          "Row: linear pattern: direction 1: the count must be a whole number from 1 to 500, got 501");
    // Driven counts that multiply past the limit.
    REQUIRE(m.doc.setParameterValue(m.count, 200.0, kUnitless).has_value());
    LinearPatternDefinition grid = m.definitionOf(m.row);
    grid.second = PatternDirection{.direction = {0.0, 1.0, 0.0}, .count = 3, .spacing = 20_mm};
    m.setDefinition(m.row, grid);
    const Error error = requireFailure(regenerator, m.doc, m.row);
    CHECK(error.code == ErrorCode::InvalidArgument);
    CHECK(error.message == "Row: linear pattern: a linear pattern may have at most 500 instances, got 600 (200 x 3)");
    CHECK(regenerator.state(m.cube) == NodeState::UpToDate);
}

TEST_CASE("LinearPattern_DoesNotAccumulateTransformDrift", "[pattern][features][acceptance]") {
    // Every offset is k s d_hat, computed for its own k: bit for bit the
    // product, however many instances come before it.
    const PatternStep step{.direction = Direction3D::unitX(), .count = 100, .spacing = 1.25_mm};
    const auto instances = patternInstances(step);
    REQUIRE(instances.size() == 100);
    for (const PatternInstance& instance : instances) {
        CHECK(instance.offset.x.si() == static_cast<double>(instance.index) * step.spacing.si());
    }
    CHECK_THAT(instances.back().offset.x.in(units::mm), WithinAbs(99.0 * 1.25, 1e-12));
    const auto slanted = patternInstances({.direction = *Direction3D::fromComponents(1.0, 2.0, 2.0),
                                           .count = 100,
                                           .spacing = 1.25_mm});
    const Translation3D last = slanted.back().offset;
    CHECK_THAT(std::hypot(last.x.in(units::mm), last.y.in(units::mm), last.z.in(units::mm)),
               WithinRel(123.75, 1e-15));

    // A hundred 1 mm cubes, 1.25 mm apart: the last starts at 123.75 mm.
    CubeRowModel m;
    REQUIRE(m.doc.setParameterValue(m.size, 1_mm).has_value());
    REQUIRE(m.doc.setParameterValue(m.count, 100.0, kUnitless).has_value());
    REQUIRE(m.doc.setParameterValue(m.pitch, 1.25_mm).has_value());
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const geometry::Body& body = requireBody(regenerator, m.row);
    CHECK(body.topology().solids == 100);
    CHECK_THAT(volumeMm3(regenerator, m.row), WithinRel(100.0, kRel));
    CHECK_THAT(body.boundingBox()->max.x.in(units::mm), WithinAbs(124.75, kPositionToleranceMm));
    CHECK(facesOn(regenerator, m.row, leftFace(123.75)) == 1);
}

TEST_CASE("LinearPattern_FailsAtomicallyWhenInstanceInvalid", "[pattern][features][hole][acceptance]") {
    HoleRowModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const double five = volumeMm3(regenerator, m.holes);
    const double drilled = volumeMm3(regenerator, m.drill);
    const Document before = m.doc.clone();

    SECTION("a sixth hole would sit on the block's end") {
        REQUIRE(m.doc.setParameterValue(m.count, 6.0, kUnitless).has_value());
        const Document edited = m.doc.clone();
        const Error error = requireFailure(regenerator, m.doc, m.holes);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK_THAT(error.message,
                   StartsWith("Holes: linear pattern: instance 5 at (100, 0, 0) mm: hole: the hole does not fit on its "
                              "face: its entry is 10 mm across, but the centre (120, 25) mm is only "));
        // No partial pattern: no body at all, and the source is untouched.
        CHECK(equivalent(m.doc, edited));
        CHECK(regenerator.state(m.drill) == NodeState::UpToDate);
        CHECK(volumeMm3(regenerator, m.drill) == drilled);
        REQUIRE(m.doc.setParameterValue(m.count, 5.0, kUnitless).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK(volumeMm3(regenerator, m.holes) == five);
    }
    SECTION("a wider spacing pushes the last hole out") {
        REQUIRE(m.doc.setParameterValue(m.pitch, 25_mm).has_value());
        const Error error = requireFailure(regenerator, m.doc, m.holes);
        CHECK_THAT(error.message, StartsWith("Holes: linear pattern: instance 4 at (100, 0, 0) mm: hole: "));
    }
    SECTION("overlapping holes are refused by the hole's own policy") {
        // At 8 mm the second hole would cut into the first one's rim.
        REQUIRE(m.doc.setParameterValue(m.pitch, 8_mm).has_value());
        const Error error = requireFailure(regenerator, m.doc, m.holes);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK_THAT(error.message, StartsWith("Holes: linear pattern: instance 1 at (8, 0, 0) mm: hole: the hole "
                                             "does not fit on its face"));
        CHECK_THAT(error.message, ContainsSubstring("the centre (28, 25) mm is only 3 mm from the face's edge"));
    }
    CHECK(m.doc.findObjectAs<LinearPatternFeature>(m.holes)->definition() ==
          before.findObjectAs<LinearPatternFeature>(m.holes)->definition());
}

TEST_CASE("LinearPattern_PatternsChamferAndFilletByTranslatedReferences", "[pattern][features][chamfer][fillet]") {
    // Five holes; then the first hole's top rim is eased, and that edge
    // operation is patterned: each instance's rim reference is the first
    // one moved by the offset, and must match exactly one edge.
    HoleRowModel m;
    const double holes = HoleRowModel::expectedVolume(120, 20, 10, 5);
    Regenerator regenerator;

    SECTION("chamfers: pi c^2 (r + c/3) per rim, by Pappus") {
        const ParameterId ease = m.doc.createParameter("ease", 1_mm, units::mm).value();
        const ObjectId chamfer = m.add<ChamferFeature>(
            "Ease", {.target = featureId(m.holes), .edges = {rim(20, 25, 20, 5)}, .distanceParameter = ease});
        const ObjectId eases = m.add<LinearPatternFeature>(
            "Eases", {.source = featureId(chamfer), .first = {.direction = {1.0, 0.0, 0.0}, .count = 5, .spacing = 20_mm}});
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        REQUIRE(report.succeeded());
        CHECK_THAT(volumeMm3(regenerator, eases), WithinRel(holes - 5.0 * pi * (5.0 + 1.0 / 3.0), kRel));
        for (const double x : {20.0, 40.0, 60.0, 80.0, 100.0}) {
            CAPTURE(x);
            CHECK(circlesOn(regenerator, eases, x, 25, 20, 6) == 1);
        }
        // The chamfer's distance drives every instance.
        REQUIRE(m.doc.setParameterValue(ease, 2_mm).has_value());
        CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{chamfer, eases});
        CHECK_THAT(volumeMm3(regenerator, eases), WithinRel(holes - 5.0 * pi * 4.0 * (5.0 + 2.0 / 3.0), kRel));
        // A sixth rim does not exist: the moved reference matches no edge,
        // and no other edge is taken instead.
        LinearPatternDefinition six = m.patternOf(eases);
        six.first.count = 6;
        m.setPattern(eases, six);
        const Error error = requireFailure(regenerator, m.doc, eases);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK(error.message == "Eases: linear pattern: instance 5 at (100, 0, 0) mm: chamfer: edge reference 1 "
                               "(circle around (120, 25, 20) mm with axis (0, 0, 1) and radius 5 mm) matches no edge "
                               "of the body");
        CHECK(regenerator.body(chamfer) != nullptr);
    }
    SECTION("fillets: 2 pi (r + u) r_f^2 (1 - pi/4) per rim") {
        const double rf = 2.0;
        const double centroid = rf * (10.0 - 3.0 * pi) / (12.0 - 3.0 * pi);
        const ObjectId fillet =
            m.add<FilletFeature>("Soften", {.target = featureId(m.holes), .edges = {rim(20, 25, 20, 5)}, .radius = 2_mm});
        const ObjectId rounds = m.add<LinearPatternFeature>(
            "Rounds", {.source = featureId(fillet), .first = {.direction = {1.0, 0.0, 0.0}, .count = 5, .spacing = 20_mm}});
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        REQUIRE(report.succeeded());
        CHECK(requireBody(regenerator, rounds).isValid());
        CHECK_THAT(volumeMm3(regenerator, rounds),
                   WithinRel(holes - 5.0 * 2.0 * pi * (5.0 + centroid) * filletCorner(rf), kRel));
        for (const double x : {20.0, 40.0, 60.0, 80.0, 100.0}) {
            CAPTURE(x);
            CHECK(circlesOn(regenerator, rounds, x, 25, 20, 7) == 1);
        }
    }
}

TEST_CASE("LinearPattern_WorksOnRevolvedSource", "[pattern][features][revolve][acceptance]") {
    // The turned part's revolve (R = 15, h = 40 about Z), three times, 40 mm
    // apart along X: separate solids, 3 pi R^2 h in all.
    TurnedPartModel m;
    auto feature = LinearPatternFeature::create(
        "Shafts", {.source = FeatureId::fromValue(m.turn.value()),
                   .first = {.direction = {1.0, 0.0, 0.0}, .count = 3, .spacing = 40_mm}});
    REQUIRE(feature.has_value());
    const ObjectId shafts = m.doc.addObject(std::move(*feature)).value();
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    const geometry::Body& body = requireBody(regenerator, shafts);
    CHECK(body.isValid());
    CHECK(body.topology().solids == 3);
    CHECK_THAT(volumeMm3(regenerator, shafts), WithinRel(3.0 * pi * 225.0 * 40.0, kRel));
    const auto box = body.boundingBox().value();
    CHECK_THAT(box.min.x.in(units::mm), WithinAbs(-15.0, kPositionToleranceMm));
    CHECK_THAT(box.max.x.in(units::mm), WithinAbs(95.0, kPositionToleranceMm));
    // The revolve's sweep drives every instance.
    REQUIRE(m.doc.setParameterValue(m.sweep, 270_deg).has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK_THAT(volumeMm3(regenerator, shafts), WithinRel(3.0 * 0.75 * pi * 225.0 * 40.0, kRel));
}

TEST_CASE("LinearPattern_TwoDirectionGridHasExpectedInstanceCount", "[pattern][features][hole][acceptance]") {
    // Four holes along X (20 mm) by three along Y (12 mm), from (20, 13).
    HoleRowModel m;
    HoleDefinition drill = m.holeOf(m.drill);
    drill.center = Point2D{20_mm, 13_mm};
    m.setHole(m.drill, drill);
    LinearPatternDefinition grid = m.patternOf(m.holes);
    grid.first.countParameter.reset();
    grid.first.count = 4;
    grid.second = PatternDirection{.direction = {0.0, 1.0, 0.0}, .count = 3, .spacing = 12_mm};
    m.setPattern(m.holes, grid);

    // 12 instances, the source once, numbered along X and then row by row.
    const auto instances = resolvePatternInstances(grid, m.doc);
    REQUIRE(instances.has_value());
    REQUIRE(instances->size() == 12);
    for (std::size_t k = 0; k < 12; ++k) {
        CAPTURE(k);
        const PatternInstance& instance = (*instances)[k];
        CHECK(instance.index == k);
        CHECK(instance.first == k % 4);
        CHECK(instance.second == k / 4);
        CHECK(instance.offset.x == 20_mm * static_cast<double>(k % 4));
        CHECK(instance.offset.y == 12_mm * static_cast<double>(k / 4));
    }

    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    CHECK(requireBody(regenerator, m.holes).isValid());
    CHECK_THAT(volumeMm3(regenerator, m.holes), WithinRel(HoleRowModel::expectedVolume(120, 20, 10, 12), kRel));
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 3; ++j) {
            CAPTURE(i, j);
            CHECK(circlesOn(regenerator, m.holes, 20.0 + 20.0 * i, 13.0 + 12.0 * j, 20, 5) == 1);
        }
    }
    CHECK(circlesOn(regenerator, m.holes, 100, 13, 20, 5) == 0);
    CHECK(circlesOn(regenerator, m.holes, 20, 49, 20, 5) == 0);
    // A row count of 1 is the one-direction pattern.
    grid.second->count = 1;
    m.setPattern(m.holes, grid);
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK_THAT(volumeMm3(regenerator, m.holes), WithinRel(HoleRowModel::expectedVolume(120, 20, 10, 4), kRel));
}

TEST_CASE("LinearPattern_RefusesUnsupportedSources", "[pattern][features]") {
    CubeRowModel m;
    Regenerator regenerator;
    // A pattern of a pattern is supported since P12-PATTERN-001; see
     // PatternNestingTests.cpp. A mirror is still not: its image is not a
     // motion of the source's operation, so it has no instance to repeat.
    SECTION("a mirror") {
        auto mirror = MirrorFeature::create(
            "Across", {.source = featureId(m.cube), .plane = {.origin = Point3D{}, .normal = {0.0, 1.0, 0.0}}});
        REQUIRE(mirror.has_value());
        const ObjectId across = m.doc.addObject(std::move(*mirror)).value();
        const ObjectId twice = m.addPattern("Twice", {.source = featureId(across),
                                                      .first = {.direction = {0.0, 1.0, 0.0}, .count = 2, .spacing = 20_mm}});
        const Error error = requireFailure(regenerator, m.doc, twice);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK(error.message == "Twice: linear pattern: a linear pattern cannot repeat a mirror");
        CHECK(regenerator.body(across) != nullptr);
    }
    SECTION("an intersect extrude") {
        auto clip = ExtrudeFeature::create("Clip", {.profile = SketchId::fromValue(m.sketch.value()),
                                                    .depth = 5_mm,
                                                    .operation = FeatureOperation::Intersect,
                                                    .target = featureId(m.cube)});
        REQUIRE(clip.has_value());
        const ObjectId clipId = m.doc.addObject(std::move(*clip)).value();
        LinearPatternDefinition d = m.definitionOf(m.row);
        d.source = featureId(clipId);
        m.setDefinition(m.row, d);
        const Error error = requireFailure(regenerator, m.doc, m.row);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK(error.message == "Row: linear pattern: repeating an intersect extrude is not supported: its instances "
                               "would only intersect each other");
    }
    SECTION("a sketch, which has no body") {
        LinearPatternDefinition d = m.definitionOf(m.row);
        d.source = FeatureId::fromValue(m.sketch.value());
        m.setDefinition(m.row, d);
        const Error error = requireFailure(regenerator, m.doc, m.row);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK(error.message == "Row: a linear pattern needs the body of its source feature");
    }
}

TEST_CASE("LinearPattern_InvalidInputsFailWithStructuredDiagnostics", "[pattern][features][acceptance]") {
    CubeRowModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());

    SECTION("a missing source") {
        REQUIRE(m.doc.removeObject(m.cube).has_value());
        const Error error = requireFailure(regenerator, m.doc, m.row);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK(error.message == "object:6 references object:5, which does not exist");
    }
    SECTION("a missing count parameter") {
        REQUIRE(m.doc.removeParameter(m.count).has_value());
        const Error error = requireFailure(regenerator, m.doc, m.row);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK(error.message == "object:6 references object:2, which does not exist");
    }
    SECTION("a count parameter that is a length") {
        LinearPatternDefinition d = m.definitionOf(m.row);
        d.first.countParameter = m.size;
        m.setDefinition(m.row, d);
        const Error error = requireFailure(regenerator, m.doc, m.row);
        CHECK(error.code == ErrorCode::DimensionMismatch);
        CHECK_THAT(error.message, StartsWith("Row: linear pattern: direction 1: "));
    }
    SECTION("a spacing parameter that is an angle") {
        const ParameterId tilt = m.doc.createParameter("tilt", 5_deg, units::deg).value();
        LinearPatternDefinition d = m.definitionOf(m.row);
        d.first.spacingParameter = tilt;
        m.setDefinition(m.row, d);
        const Error error = requireFailure(regenerator, m.doc, m.row);
        CHECK(error.code == ErrorCode::DimensionMismatch);
        CHECK_THAT(error.message, StartsWith("Row: linear pattern: direction 1: "));
    }
    SECTION("a source that fails blocks the pattern") {
        REQUIRE(m.doc.setParameterValue(m.size, 0_mm).has_value());
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        CHECK_FALSE(report.failed.empty());
        CHECK(std::ranges::find(report.blocked, m.row) != report.blocked.end());
        CHECK(regenerator.body(m.row) == nullptr);
    }
}

TEST_CASE("LinearPattern_FailuresLeaveTheDocumentAndUpstreamBodiesIntact", "[pattern][features][acceptance]") {
    HoleRowModel m;
    CommandHistory history;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const Document before = m.doc.clone();
    const std::uint64_t revision = m.doc.revision();
    const double drilled = volumeMm3(regenerator, m.drill);
    const double patterned = volumeMm3(regenerator, m.holes);

    // An invalid edit is refused before it touches the document.
    LinearPatternDefinition invalid = m.patternOf(m.holes);
    invalid.first.direction = {0.0, 0.0, 0.0};
    CHECK(errorCode(history.execute(m.doc, std::make_unique<ModifyLinearPatternCommand>(featureId(m.holes),
                                                                                         invalid))) ==
          ErrorCode::InvalidArgument);
    CHECK(equivalent(m.doc, before));
    CHECK(m.doc.revision() == revision);
    CHECK_FALSE(history.canUndo());

    // A valid edit the geometry cannot take fails at regeneration only.
    LinearPatternDefinition tooMany = m.patternOf(m.holes);
    tooMany.first.countParameter.reset();
    tooMany.first.count = 7;
    REQUIRE(history.execute(m.doc, std::make_unique<ModifyLinearPatternCommand>(featureId(m.holes), tooMany))
                .has_value());
    const Document edited = m.doc.clone();
    const Error error = requireFailure(regenerator, m.doc, m.holes);
    CHECK(error.code == ErrorCode::FailedPrecondition);
    CHECK(equivalent(m.doc, edited)); // regeneration does not change the model
    CHECK(regenerator.state(m.drill) == NodeState::UpToDate);
    CHECK(volumeMm3(regenerator, m.drill) == drilled);

    // Undo returns to the working model and its exact geometry.
    REQUIRE(history.undo(m.doc).has_value());
    CHECK(equivalent(m.doc, before));
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(volumeMm3(regenerator, m.holes) == patterned);
}

TEST_CASE("LinearPattern_UndoRedoRestoresGeometry", "[pattern][features][undo][acceptance]") {
    // The drilled block (hole at (50, 25) in 100 x 50 x 20 mm).
    HoleBlockModel m;
    CommandHistory history;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const Document initial = m.doc.clone();
    const double one = HoleBlockModel::expectedVolume(100, 50, 20, 10);

    // Create: three holes along X, 20 mm apart.
    const LinearPatternDefinition three{.source = featureId(m.drill),
                                        .first = {.direction = {1.0, 0.0, 0.0}, .count = 3, .spacing = 20_mm}};
    auto create = std::make_unique<CreateLinearPatternCommand>("Holes", three);
    CreateLinearPatternCommand* createRaw = create.get();
    CHECK(create->description() == "Create linear_pattern 'Holes'");
    REQUIRE(history.execute(m.doc, std::move(create)).has_value());
    const ObjectId holes{createRaw->featureId()};
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const double created = volumeMm3(regenerator, holes);
    CHECK_THAT(created, WithinRel(one - 2.0 * 500.0 * pi, kRel));
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{holes});
    const Document afterCreate = m.doc.clone();

    // Modify the count.
    LinearPatternDefinition two = three;
    two.first.count = 2;
    REQUIRE(history.execute(m.doc, std::make_unique<ModifyLinearPatternCommand>(featureId(holes), two)).has_value());
    CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{holes});
    const double counted = volumeMm3(regenerator, holes);
    CHECK_THAT(counted, WithinRel(one - 500.0 * pi, kRel));
    const Document afterCount = m.doc.clone();

    // Modify the spacing.
    LinearPatternDefinition closer = two;
    closer.first.spacing = 15_mm;
    REQUIRE(history.execute(m.doc, std::make_unique<ModifyLinearPatternCommand>(featureId(holes), closer)).has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const double spaced = volumeMm3(regenerator, holes);
    CHECK(circlesOn(regenerator, holes, 65, 25, 20, 5) == 1);
    const Document afterSpacing = m.doc.clone();

    // Modify the direction.
    LinearPatternDefinition turned = closer;
    turned.first.direction = {0.0, 1.0, 0.0};
    REQUIRE(history.execute(m.doc, std::make_unique<ModifyLinearPatternCommand>(featureId(holes), turned)).has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const double directed = volumeMm3(regenerator, holes);
    CHECK_THAT(directed, WithinRel(one - 500.0 * pi, kRel));
    CHECK(circlesOn(regenerator, holes, 50, 40, 20, 5) == 1);
    CHECK(circlesOn(regenerator, holes, 65, 25, 20, 5) == 0);
    const Document afterDirection = m.doc.clone();

    // Undo steps back through each edit, to the same geometry bit for bit.
    REQUIRE(history.undo(m.doc).has_value());
    CHECK(equivalent(m.doc, afterSpacing));
    CHECK(m.doc.findObjectAs<LinearPatternFeature>(holes)->definition() == closer);
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(volumeMm3(regenerator, holes) == spaced);
    CHECK(circlesOn(regenerator, holes, 65, 25, 20, 5) == 1);

    REQUIRE(history.undo(m.doc).has_value());
    CHECK(equivalent(m.doc, afterCount));
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(volumeMm3(regenerator, holes) == counted);

    REQUIRE(history.undo(m.doc).has_value());
    CHECK(equivalent(m.doc, afterCreate));
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(volumeMm3(regenerator, holes) == created);

    for (int i = 0; i < 3; ++i) {
        REQUIRE(history.redo(m.doc).has_value());
    }
    CHECK(equivalent(m.doc, afterDirection));
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(volumeMm3(regenerator, holes) == directed);

    for (int i = 0; i < 4; ++i) {
        REQUIRE(history.undo(m.doc).has_value());
    }
    CHECK(m.doc.findObject(holes) == nullptr);
    CHECK(equivalent(m.doc, initial));
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(regenerator.body(holes) == nullptr);
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.drill});

    REQUIRE(history.redo(m.doc).has_value());
    CHECK(equivalent(m.doc, afterCreate)); // recreated with the same ID
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(volumeMm3(regenerator, holes) == created);

    // Commands check the feature kind.
    CHECK(errorCode(history.execute(m.doc, std::make_unique<ModifyLinearPatternCommand>(featureId(m.drill), two))) ==
          ErrorCode::NotFound);
}

TEST_CASE("LinearPattern_ReusesStableSourceReference", "[pattern][features][acceptance]") {
    HoleRowModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const double patterned = volumeMm3(regenerator, m.holes);
    // Another hole on the same face, which a guess could mistake for the source.
    const ObjectId other = m.add<HoleFeature>(
        "Other", {.target = featureId(m.pad), .face = topFace(), .center = Point2D{60_mm, 40_mm}, .diameter = 6_mm});
    REQUIRE(requireReport(regenerator, m.doc).succeeded());

    // The reference is the source's ID: renaming it changes nothing.
    REQUIRE(m.doc.rename(m.drill, "Bore").has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(m.patternOf(m.holes).source == featureId(m.drill));
    CHECK(volumeMm3(regenerator, m.holes) == patterned);

    // Without its source the pattern fails; it does not adopt the other hole.
    auto removed = m.doc.removeObject(m.drill);
    REQUIRE(removed.has_value());
    const Error error = requireFailure(regenerator, m.doc, m.holes);
    CHECK(error.code == ErrorCode::NotFound);
    CHECK(error.message == "object:10 references object:9, which does not exist");
    CHECK(regenerator.body(other) != nullptr);

    // Put back with its ID (as undo does), the source is found again.
    REQUIRE(m.doc.insertObject(std::move(*removed)).has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(volumeMm3(regenerator, m.holes) == patterned);
}

TEST_CASE("LinearPattern_RegenerationIsDeterministic", "[pattern][features]") {
    HoleRowModel m;
    Regenerator first;
    Regenerator second;
    REQUIRE(requireReport(first, m.doc).succeeded());
    REQUIRE(requireReport(second, m.doc).succeeded());
    const geometry::Body& a = requireBody(first, m.holes);
    const geometry::Body& b = requireBody(second, m.holes);
    const auto pa = a.massProperties().value();
    const auto pb = b.massProperties().value();
    CHECK(bits(pa.volume.si()) == bits(pb.volume.si()));
    CHECK(bits(pa.surfaceArea.si()) == bits(pb.surfaceArea.si()));
    CHECK(bits(pa.centerOfMass.x.si()) == bits(pb.centerOfMass.x.si()));
    CHECK(a.boundingBox().value() == b.boundingBox().value());
    CHECK(a.topology() == b.topology());
    // The same instances, in the same order.
    CHECK(resolvePatternInstances(m.patternOf(m.holes), m.doc).value() ==
          resolvePatternInstances(m.patternOf(m.holes), m.doc).value());
}
