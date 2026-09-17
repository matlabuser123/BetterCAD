#include "FeatureTestSupport.hpp"
#include "TestHelpers.hpp"
#include "core/geometry/GeometryTestSupport.hpp"
#include "support/BracketModel.hpp"
#include "support/PatternModels.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/features/CircularPatternFeature.hpp>
#include <bettercad/features/FaceReferences.hpp>
#include <bettercad/features/LinearPatternFeature.hpp>
#include <bettercad/features/Regeneration.hpp>
#include <bettercad/features/Regenerator.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using bettercad::test::BoltCircleModel;
using bettercad::test::countOf;
using bettercad::test::CubeRowModel;
using bettercad::test::describe;
using bettercad::test::HoleRowModel;
using bettercad::test::kPositionToleranceMm;
using bettercad::test::requireReport;
using bettercad::test::volumeMm3;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// P12-PATTERN-001: a pattern whose source is another pattern. Each instance
// of the outer pattern makes every instance of the inner one again, moved
// with it, and the faces it makes carry both steps: the inner pattern and
// its instance, then the outer pattern and its instance. The expected
// positions here are the products of the two definitions, worked out by
// hand.

namespace {

constexpr double pi = std::numbers::pi;
constexpr double kRel = bettercad::test::kRelTight;
constexpr double kCubeMm3 = 1000.0;

FeatureId featureId(ObjectId id) {
    return FeatureId::fromValue(id.value());
}

const geometry::Body& requireBody(const Regenerator& regenerator, ObjectId feature) {
    const geometry::Body* body = regenerator.body(feature);
    REQUIRE(body != nullptr);
    return *body;
}

/// The (x, y) of the lower-left corner of every cube, sorted, in mm: the
/// corner of a cube is where its -X and -Y faces meet, which identifies the
/// instance that made it.
std::vector<std::pair<double, double>> cubeCorners(const Regenerator& regenerator, ObjectId feature) {
    std::vector<std::pair<double, double>> corners;
    const auto faces = geometry::listFaces(requireBody(regenerator, feature));
    REQUIRE(faces.has_value());
    for (const geometry::FaceInfo& face : *faces) {
        if (face.signature && face.signature->normal.dot(Direction3D::unitX()) < -0.5) {
            // The -X face's centroid is at the cube's own x, mid-way in y.
            corners.emplace_back(face.centroid.x.in(units::mm), face.centroid.y.in(units::mm) - 5.0);
        }
    }
    std::ranges::sort(corners);
    return corners;
}

/// One expected corner, in millimetres, so that a list of them reads as a
/// list of positions.
std::pair<double, double> at(double x, double y) {
    return {x, y};
}

void checkCorners(const std::vector<std::pair<double, double>>& actual,
                  const std::vector<std::pair<double, double>>& expected) {
    REQUIRE(actual.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        INFO("cube " << i);
        CHECK_THAT(actual[i].first, WithinAbs(expected[i].first, kPositionToleranceMm));
        CHECK_THAT(actual[i].second, WithinAbs(expected[i].second, kPositionToleranceMm));
    }
}

/// Regenerates; exactly @p feature must fail, keeping no body.
Error requireFailure(Regenerator& regenerator, Document& doc, ObjectId feature) {
    const RegenerationReport report = requireReport(regenerator, doc);
    INFO(describe(report));
    REQUIRE(report.failed == std::vector<ObjectId>{feature});
    CHECK(regenerator.body(feature) == nullptr);
    return report.errors.at(feature);
}

/// The cube row with a literal count and spacing, so that a test can set
/// them without touching the parameters.
void setRow(CubeRowModel& m, std::uint32_t count, Length spacing, std::vector<std::uint32_t> suppressed = {}) {
    LinearPatternDefinition d = m.definitionOf(m.row);
    d.first.countParameter.reset();
    d.first.spacingParameter.reset();
    d.first.count = count;
    d.first.spacing = spacing;
    d.suppressed = std::move(suppressed);
    m.setDefinition(m.row, d);
}

} // namespace

// ---------------------------------------------------------------------------
// A linear pattern of a linear pattern
// ---------------------------------------------------------------------------

TEST_CASE("LinearPattern_RepeatsAnotherLinearPattern", "[pattern][nesting][p12][acceptance]") {
    CubeRowModel m;
    setRow(m, 3, 20_mm); // cubes at x = 0, 20, 40
    const ObjectId grid = m.addPattern("Grid", {.source = CubeRowModel::featureId(m.row),
                                                .first = {.direction = {0.0, 1.0, 0.0},
                                                          .count = 4,
                                                          .spacing = 30_mm}});
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());

    // 3 cubes along X, repeated 4 times along Y: 12 separate cubes.
    CHECK_THAT(volumeMm3(regenerator, grid), WithinRel(12.0 * kCubeMm3, kRel));
    CHECK(requireBody(regenerator, grid).topology().solids == 12);
    std::vector<std::pair<double, double>> expected;
    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 3; ++i) {
            expected.push_back(at(i * 20.0, j * 30.0));
        }
    }
    std::ranges::sort(expected);
    checkCorners(cubeCorners(regenerator, grid), expected);

    // The grid spans the two rows' extents together.
    const auto bounds = requireBody(regenerator, grid).boundingBox();
    REQUIRE(bounds.has_value());
    bettercad::test::checkPoint(bounds->min, 0.0, 0.0, 0.0);
    bettercad::test::checkPoint(bounds->max, 50.0, 100.0, 10.0);
    // Equal cubes: the centroid is the mean of their centres.
    const auto properties = requireBody(regenerator, grid).massProperties();
    REQUIRE(properties.has_value());
    bettercad::test::checkPoint(properties->centerOfMass, 5.0 + 20.0, 5.0 + 45.0, 5.0);
}

TEST_CASE("LinearPattern_NestedCopiesKeepTheProvenanceOfBothPatterns", "[pattern][nesting][references][p12]") {
    // A face of the cube at (20, 30) is the cube's own face, copied by
    // instance 1 of Row and then by instance 1 of Grid: neither step is
    // lost, and neither is confused with the other.
    CubeRowModel m;
    setRow(m, 3, 20_mm);
    const ObjectId grid = m.addPattern("Grid", {.source = CubeRowModel::featureId(m.row),
                                                .first = {.direction = {0.0, 1.0, 0.0},
                                                          .count = 4,
                                                          .spacing = 30_mm}});
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const BodyLookup bodies = [&](ObjectId id) { return regenerator.body(id); };
    const auto startCap = [&](std::vector<FaceCopy> copies) {
        return FaceName{m.cube, {.role = FaceRole::StartCap, .copies = std::move(copies)}};
    };
    /// The centre of the named start cap: every one lies in z = 0, so only
    /// the face itself, not the plane it lies in, tells the instances apart.
    const auto centreOf = [&](std::vector<FaceCopy> copies) {
        const auto found = geometry::findNamedFaces(requireBody(regenerator, grid), startCap(std::move(copies)));
        REQUIRE(found.has_value());
        REQUIRE(found->size() == 1);
        return found->front().centroid;
    };

    // The seed itself: no copy step at all, so the cube at (0, 0).
    bettercad::test::checkPoint(centreOf({}), 5.0, 5.0, 0.0);
    // Instance 2 of Row alone, in the grid's own row (Grid instance 0).
    bettercad::test::checkPoint(centreOf({FaceCopy{m.row, 2}}), 45.0, 5.0, 0.0);
    // Instance 3 of Grid alone: the seed's own cube, moved along Y.
    bettercad::test::checkPoint(centreOf({FaceCopy{grid, 3}}), 5.0, 95.0, 0.0);
    // Both steps, the inner pattern's first: the cube at (20, 30).
    bettercad::test::checkPoint(centreOf({FaceCopy{m.row, 1}, FaceCopy{grid, 1}}), 25.0, 35.0, 0.0);

    // The steps in the other order are a different name, and name nothing.
    const auto reversed = geometry::findNamedFaces(requireBody(regenerator, grid),
                                                   startCap({FaceCopy{grid, 1}, FaceCopy{m.row, 1}}));
    REQUIRE(reversed.has_value());
    CHECK(reversed->empty());
    // So is an instance neither pattern has.
    const auto missing = geometry::findNamedFaces(requireBody(regenerator, grid),
                                                  startCap({FaceCopy{m.row, 1}, FaceCopy{grid, 9}}));
    REQUIRE(missing.has_value());
    CHECK(missing->empty());
    // A downstream reference to one of them fails rather than finding another.
    const auto resolved = resolveFacePlane(m.doc, startCap({FaceCopy{grid, 1}, FaceCopy{m.row, 1}}), bodies);
    REQUIRE_FALSE(resolved.has_value());
    CHECK(resolved.error().code == ErrorCode::NotFound);
}

TEST_CASE("LinearPattern_SuppressionInsideANestedPatternLeavesEveryCopyOut", "[pattern][nesting][p12]") {
    CubeRowModel m;
    setRow(m, 4, 20_mm, {2}); // cubes at x = 0, 20, 60 (instance 2 suppressed)
    const ObjectId grid = m.addPattern("Grid", {.source = CubeRowModel::featureId(m.row),
                                                .first = {.direction = {0.0, 1.0, 0.0},
                                                          .count = 3,
                                                          .spacing = 30_mm}});
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK_THAT(volumeMm3(regenerator, grid), WithinRel(9.0 * kCubeMm3, kRel));
    std::vector<std::pair<double, double>> expected;
    for (int j = 0; j < 3; ++j) {
        for (const double x : {0.0, 20.0, 60.0}) {
            expected.push_back(at(x, j * 30.0));
        }
    }
    std::ranges::sort(expected);
    checkCorners(cubeCorners(regenerator, grid), expected);

    SECTION("and the outer pattern can suppress one of its own") {
        LinearPatternDefinition d = m.definitionOf(grid);
        d.suppressed = {1};
        m.setDefinition(grid, d);
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK_THAT(volumeMm3(regenerator, grid), WithinRel(6.0 * kCubeMm3, kRel));
        std::vector<std::pair<double, double>> rows;
        for (const double y : {0.0, 60.0}) {
            for (const double x : {0.0, 20.0, 60.0}) {
                rows.push_back(at(x, y));
            }
        }
        std::ranges::sort(rows);
        checkCorners(cubeCorners(regenerator, grid), rows);
    }
}

TEST_CASE("LinearPattern_NestedPatternsFollowTheirSourceParameters", "[pattern][nesting][p12]") {
    CubeRowModel m;
    const ObjectId grid = m.addPattern("Grid", {.source = CubeRowModel::featureId(m.row),
                                                .first = {.direction = {0.0, 1.0, 0.0},
                                                          .count = 2,
                                                          .spacing = 40_mm}});
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    // The model's row is 4 cubes 20 mm apart, driven by count and pitch.
    CHECK_THAT(volumeMm3(regenerator, grid), WithinRel(8.0 * kCubeMm3, kRel));

    REQUIRE(m.doc.setParameterValue(m.count, 3.0, kUnitless).has_value());
    const RegenerationReport fewer = requireReport(regenerator, m.doc);
    INFO(describe(fewer));
    REQUIRE(fewer.succeeded());
    CHECK(fewer.regenerated == std::vector<ObjectId>{m.row, grid});
    CHECK_THAT(volumeMm3(regenerator, grid), WithinRel(6.0 * kCubeMm3, kRel));

    REQUIRE(m.doc.setParameterValue(m.pitch, 25_mm).has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    std::vector<std::pair<double, double>> expected;
    for (const double y : {0.0, 40.0}) {
        for (const double x : {0.0, 25.0, 50.0}) {
            expected.push_back(at(x, y));
        }
    }
    std::ranges::sort(expected);
    checkCorners(cubeCorners(regenerator, grid), expected);

    // The seed itself: a bigger cube changes every copy.
    REQUIRE(m.doc.setParameterValue(m.size, 12_mm).has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK_THAT(volumeMm3(regenerator, grid), WithinRel(6.0 * 12.0 * 12.0 * 12.0, kRel));
}

// ---------------------------------------------------------------------------
// Mixing the two kinds of pattern
// ---------------------------------------------------------------------------

TEST_CASE("CircularPattern_RepeatsALinearPattern", "[pattern][nesting][circular][p12][acceptance]") {
    // Three holes in a row, the row turned about the block's centre: the
    // second turn puts the same three holes 180 degrees away.
    HoleRowModel m;
    REQUIRE(m.doc.setParameterValue(m.width, 160_mm).has_value());
    LinearPatternDefinition row = m.patternOf(m.holes);
    row.first.countParameter.reset();
    row.first.spacingParameter.reset();
    row.first.count = 3;
    row.first.spacing = 20_mm;
    m.setPattern(m.holes, row);
    auto turned = CircularPatternFeature::create(
        "Turned", {.source = featureId(m.holes),
                   .axis = {.origin = Point3D{70_mm, 25_mm, 0_mm}, .direction = {0.0, 0.0, 1.0}},
                   .count = 2});
    REQUIRE(turned.has_value());
    const ObjectId ring = m.doc.addObject(std::move(*turned)).value();

    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    // Six holes: the drill at x = 20, 40, 60, and half a turn about
    // (70, 25) maps x to 140 - x, so its images at 120, 100 and 80.
    CHECK_THAT(volumeMm3(regenerator, ring), WithinRel(HoleRowModel::expectedVolume(160, 20, 10, 6), kRel));
    const geometry::Body& body = requireBody(regenerator, ring);
    for (const double x : {20.0, 40.0, 60.0, 80.0, 100.0, 120.0}) {
        const auto rim = geometry::circleSignature(Point3D{x * units::mm, 25_mm, 20_mm}, Direction3D::unitZ(), 5_mm);
        REQUIRE(rim.has_value());
        const auto found = geometry::findEdges(body, *rim);
        REQUIRE(found.has_value());
        INFO("hole at x = " << x);
        CHECK(found->size() == 1);
    }
}

TEST_CASE("LinearPattern_RepeatsACircularPattern", "[pattern][nesting][circular][p12][acceptance]") {
    // A bolt circle repeated along X: the whole circle of six holes again,
    // 12 mm away, which is more than one diameter, so the twelve holes are
    // separate and the volume is the flange less twelve of them.
    BoltCircleModel m;
    auto row = LinearPatternFeature::create("Row", {.source = featureId(m.bolts),
                                                    .first = {.direction = {1.0, 0.0, 0.0},
                                                              .count = 2,
                                                              .spacing = 12_mm}});
    REQUIRE(row.has_value());
    const ObjectId shifted = m.doc.addObject(std::move(*row)).value();
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    CHECK_THAT(volumeMm3(regenerator, shifted), WithinRel(BoltCircleModel::expectedVolume(60, 10, 10, 12), kRel));

    // Each of the six bolts, and each of them again 12 mm along X.
    const geometry::Body& body = requireBody(regenerator, shifted);
    for (std::size_t i = 0; i < 6; ++i) {
        const double radians = static_cast<double>(i) * pi / 3.0;
        for (const double shift : {0.0, 12.0}) {
            const auto rim = geometry::circleSignature(
                Point3D{(40.0 * std::cos(radians) + shift) * units::mm, 40.0 * std::sin(radians) * units::mm, 0_mm},
                Direction3D::unitZ(), 5_mm);
            REQUIRE(rim.has_value());
            const auto found = geometry::findEdges(body, *rim);
            REQUIRE(found.has_value());
            INFO("bolt " << i << " shifted " << shift);
            CHECK(found->size() == 1);
        }
    }
    CHECK(body.topology().solids == 1);
    const auto bounds = body.boundingBox();
    REQUIRE(bounds.has_value());
    bettercad::test::checkPoint(bounds->min, -60.0, -60.0, 0.0);
    bettercad::test::checkPoint(bounds->max, 60.0, 60.0, 10.0);
}

TEST_CASE("CircularPattern_RepeatsAnotherCircularPattern", "[pattern][nesting][circular][p12]") {
    // Two bolts 30 degrees apart, that pair repeated four times about the
    // same axis: eight bolts at 0, 30, 90, 120, 180, 210, 270 and 300.
    BoltCircleModel m;
    CircularPatternDefinition pair = m.definitionOf(m.bolts);
    pair.countParameter.reset();
    pair.count = 2;
    pair.spacing = CircularSpacing::AngleStep;
    pair.angle = 30_deg;
    m.setDefinition(m.bolts, pair);
    const ObjectId quad = m.addPattern("Quad", {.source = featureId(m.bolts),
                                                .axis = {.origin = Point3D{}, .direction = {0.0, 0.0, 1.0}},
                                                .count = 4});
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    CHECK_THAT(volumeMm3(regenerator, quad), WithinRel(BoltCircleModel::expectedVolume(60, 10, 10, 8), kRel));

    const geometry::Body& body = requireBody(regenerator, quad);
    for (const double degrees : {0.0, 30.0, 90.0, 120.0, 180.0, 210.0, 270.0, 300.0}) {
        const double radians = degrees * pi / 180.0;
        const auto rim = geometry::circleSignature(
            Point3D{40.0 * std::cos(radians) * units::mm, 40.0 * std::sin(radians) * units::mm, 0_mm},
            Direction3D::unitZ(), 5_mm);
        REQUIRE(rim.has_value());
        const auto found = geometry::findEdges(body, *rim);
        REQUIRE(found.has_value());
        INFO(degrees << " deg");
        CHECK(found->size() == 1);
    }
}

TEST_CASE("LinearPattern_RepeatsAPatternOfAPattern", "[pattern][nesting][p12]") {
    // Three levels: 2 cubes along X, that pair twice along Y, the four
    // twice along Z -- eight cubes at the corners of a 20 x 30 x 40 box.
    CubeRowModel m;
    setRow(m, 2, 20_mm);
    const ObjectId grid = m.addPattern("Grid", {.source = CubeRowModel::featureId(m.row),
                                                .first = {.direction = {0.0, 1.0, 0.0},
                                                          .count = 2,
                                                          .spacing = 30_mm}});
    const ObjectId stack = m.addPattern("Stack", {.source = CubeRowModel::featureId(grid),
                                                  .first = {.direction = {0.0, 0.0, 1.0},
                                                            .count = 2,
                                                            .spacing = 40_mm}});
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    CHECK_THAT(volumeMm3(regenerator, stack), WithinRel(8.0 * kCubeMm3, kRel));
    const auto bounds = requireBody(regenerator, stack).boundingBox();
    REQUIRE(bounds.has_value());
    bettercad::test::checkPoint(bounds->min, 0.0, 0.0, 0.0);
    bettercad::test::checkPoint(bounds->max, 30.0, 40.0, 50.0);
    const auto properties = requireBody(regenerator, stack).massProperties();
    REQUIRE(properties.has_value());
    bettercad::test::checkPoint(properties->centerOfMass, 15.0, 20.0, 25.0);

    // Every step is in the name, innermost first: the cube at (20, 30, 40),
    // whose start cap is centred at (25, 35, 40).
    const auto corner = geometry::findNamedFaces(
        requireBody(regenerator, stack),
        FaceName{m.cube, {.role = FaceRole::StartCap,
                          .copies = {FaceCopy{m.row, 1}, FaceCopy{grid, 1}, FaceCopy{stack, 1}}}});
    REQUIRE(corner.has_value());
    REQUIRE(corner->size() == 1);
    bettercad::test::checkPoint(corner->front().centroid, 25.0, 35.0, 40.0);
}

// ---------------------------------------------------------------------------
// Failure paths
// ---------------------------------------------------------------------------

TEST_CASE("LinearPattern_NestedInstancesCountTowardsTheCap", "[pattern][nesting][p12]") {
    // The work is the product: 30 instances of a 20-instance row would be
    // 600 cubes, over the 500 cap.
    CubeRowModel m;
    setRow(m, 20, 20_mm);
    const ObjectId grid = m.addPattern("Grid", {.source = CubeRowModel::featureId(m.row),
                                                .first = {.direction = {0.0, 1.0, 0.0},
                                                          .count = 30,
                                                          .spacing = 30_mm}});
    Regenerator regenerator;
    const Error error = requireFailure(regenerator, m.doc, grid);
    CHECK(error.code == ErrorCode::InvalidArgument);
    CHECK(error.message == "Grid: linear pattern: a linear pattern may have at most 500 instances, got 600 (30 x the "
                           "20 instances of Row)");
    // The source is untouched: a failed pattern commits nothing.
    CHECK(regenerator.body(m.row) != nullptr);

    SECTION("suppressed instances of the source do not count") {
        // Sixteen of the row's twenty instances suppressed leaves 4, so the
        // grid asks for 30 x 4 = 120 instances, within the cap.
        LinearPatternDefinition row = m.definitionOf(m.row);
        for (std::uint32_t i = 1; i <= 16; ++i) {
            row.suppressed.push_back(i);
        }
        m.setDefinition(m.row, row);
        LinearPatternDefinition outer = m.definitionOf(grid);
        outer.first.count = 3;
        m.setDefinition(grid, outer);
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        REQUIRE(report.succeeded());
        CHECK_THAT(volumeMm3(regenerator, grid), WithinRel(12.0 * kCubeMm3, kRel));
    }
}

TEST_CASE("LinearPattern_NestedFailurePropagatesAndCommitsNothing", "[pattern][nesting][p12]") {
    // The inner pattern's own source fails: the outer pattern must fail too,
    // and no half-built body may become the document's result.
    HoleRowModel m;
    REQUIRE(m.doc.setParameterValue(m.width, 160_mm).has_value());
    auto turned = CircularPatternFeature::create(
        "Turned", {.source = featureId(m.holes),
                   .axis = {.origin = Point3D{70_mm, 25_mm, 0_mm}, .direction = {0.0, 0.0, 1.0}},
                   .count = 2});
    REQUIRE(turned.has_value());
    const ObjectId ring = m.doc.addObject(std::move(*turned)).value();
    Regenerator regenerator;
    // The holes start at x = 20 and step 20 mm: the ninth would be at 180,
    // past the 160 mm block, so the row itself fails.
    REQUIRE(m.doc.setParameterValue(m.count, 9.0, kUnitless).has_value());
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    CHECK(regenerator.body(m.holes) == nullptr);
    CHECK(regenerator.body(ring) == nullptr);
    CHECK(std::ranges::find(report.failed, m.holes) != report.failed.end());
    // The block below the failed row is untouched.
    CHECK(regenerator.body(m.pad) != nullptr);

    REQUIRE(m.doc.setParameterValue(m.count, 3.0, kUnitless).has_value());
    const RegenerationReport fixed = requireReport(regenerator, m.doc);
    INFO(describe(fixed));
    CHECK(fixed.succeeded());
    CHECK(regenerator.body(ring) != nullptr);
}

TEST_CASE("LinearPattern_RefusesToRepeatItself", "[pattern][nesting][p12]") {
    // A self-referencing pattern is a dependency cycle; the regenerator
    // reports it and the pattern never builds.
    CubeRowModel m;
    LinearPatternDefinition d = m.definitionOf(m.row);
    d.source = CubeRowModel::featureId(m.row);
    m.setDefinition(m.row, d);
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    CHECK_FALSE(report.succeeded());
    CHECK(regenerator.body(m.row) == nullptr);
    REQUIRE(report.errors.contains(m.row));
    CHECK_THAT(report.errors.at(m.row).message, ContainsSubstring("cycle"));
}

TEST_CASE("LinearPattern_RefusesACycleOfTwoPatterns", "[pattern][nesting][p12]") {
    CubeRowModel m;
    const ObjectId grid = m.addPattern("Grid", {.source = CubeRowModel::featureId(m.row),
                                                .first = {.direction = {0.0, 1.0, 0.0},
                                                          .count = 2,
                                                          .spacing = 30_mm}});
    LinearPatternDefinition row = m.definitionOf(m.row);
    row.source = CubeRowModel::featureId(grid);
    m.setDefinition(m.row, row);
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    CHECK_FALSE(report.succeeded());
    CHECK(regenerator.body(m.row) == nullptr);
    CHECK(regenerator.body(grid) == nullptr);
    REQUIRE(report.errors.contains(m.row));
    CHECK_THAT(report.errors.at(m.row).message, ContainsSubstring("cycle"));
}

TEST_CASE("LinearPattern_NestedPatternsAreDeterministic", "[pattern][nesting][p12][determinism]") {
    const auto build = []() {
        auto model = std::make_unique<CubeRowModel>();
        setRow(*model, 3, 20_mm);
        const ObjectId grid = model->addPattern("Grid", {.source = CubeRowModel::featureId(model->row),
                                                         .first = {.direction = {0.0, 1.0, 0.0},
                                                                   .count = 3,
                                                                   .spacing = 30_mm}});
        return std::pair{std::move(model), grid};
    };
    const auto fingerprint = [](Document& doc, ObjectId feature, Regenerator& regenerator) {
        REQUIRE(requireReport(regenerator, doc).succeeded());
        const geometry::Body* body = regenerator.body(feature);
        REQUIRE(body != nullptr);
        const auto properties = body->massProperties().value();
        return std::vector<double>{properties.volume.si(), properties.surfaceArea.si(),
                                   properties.centerOfMass.x.si(), properties.centerOfMass.y.si(),
                                   properties.centerOfMass.z.si()};
    };
    auto [first, grid] = build();
    Regenerator a;
    const std::vector<double> once = fingerprint(first->doc, grid, a);
    Regenerator again;
    CHECK(fingerprint(first->doc, grid, again) == once);
    auto [second, otherGrid] = build();
    Regenerator b;
    CHECK(fingerprint(second->doc, otherGrid, b) == once);
}
