#include "FeatureTestSupport.hpp"
#include "TestHelpers.hpp"
#include "core/geometry/GeometryTestSupport.hpp"
#include "support/BracketModel.hpp"
#include "support/PatternModels.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/document/Commands.hpp>
#include <bettercad/features/CircularPatternFeature.hpp>
#include <bettercad/features/FeatureCommands.hpp>
#include <bettercad/features/LinearPatternFeature.hpp>
#include <bettercad/features/Regeneration.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/FaceReferences.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using bettercad::test::BoltCircleModel;
using bettercad::test::countOf;
using bettercad::test::CubeRowModel;
using bettercad::test::describe;
using bettercad::test::kPositionToleranceMm;
using bettercad::test::requireReport;
using bettercad::test::volumeMm3;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// P12-PATTERN-001: symmetric patterns, patterns given a total length, and
// patterns with suppressed instances. Every expected position here is worked
// out from the definition by hand, not from the implementation: instance i
// of a plain direction sits at i s, of a symmetric one at m(i) s with
// m = 0, +1, -1, +2, -2, ..., and a total length L over N instances is
// s = L / (N - 1).

namespace {

constexpr double pi = std::numbers::pi;
constexpr double kRel = bettercad::test::kRelTight;
/// One cube of CubeRowModel: 10 x 10 x 10 mm.
constexpr double kCubeMm3 = 1000.0;

const geometry::Body& requireBody(const Regenerator& regenerator, ObjectId feature) {
    const geometry::Body* body = regenerator.body(feature);
    REQUIRE(body != nullptr);
    return *body;
}

/// The x of the left face of every cube in the row, in increasing order.
std::vector<double> cubeLeftFaces(const Regenerator& regenerator, ObjectId feature) {
    std::vector<double> found;
    const geometry::Body& body = requireBody(regenerator, feature);
    const auto faces = geometry::listFaces(body);
    REQUIRE(faces.has_value());
    for (const geometry::FaceInfo& face : *faces) {
        // The -X side of a cube: a 10 x 10 mm plane facing -X.
        if (face.signature && face.signature->normal.dot(Direction3D::unitX()) < -0.5) {
            found.push_back(face.centroid.x.in(units::mm));
        }
    }
    std::ranges::sort(found);
    return found;
}

/// The offsets the definition gives, in millimetres along X, by instance.
std::vector<double> offsetsAlongX(const LinearPatternDefinition& definition, const Document& doc) {
    const auto instances = resolvePatternInstances(definition, doc);
    REQUIRE(instances.has_value());
    std::vector<double> offsets;
    for (const PatternInstance& instance : *instances) {
        CHECK_THAT(instance.offset.y.in(units::mm), WithinAbs(0.0, kPositionToleranceMm));
        CHECK_THAT(instance.offset.z.in(units::mm), WithinAbs(0.0, kPositionToleranceMm));
        offsets.push_back(instance.offset.x.in(units::mm));
    }
    return offsets;
}

void checkOffsets(const std::vector<double>& actual, const std::vector<double>& expected) {
    REQUIRE(actual.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        INFO("instance " << i);
        CHECK_THAT(actual[i], WithinAbs(expected[i], kPositionToleranceMm));
    }
}

/// The message of a definition the pattern refuses outright.
std::string rejection(const LinearPatternDefinition& definition) {
    auto feature = LinearPatternFeature::create("Bad", definition);
    REQUIRE_FALSE(feature.has_value());
    CHECK(feature.error().code == ErrorCode::InvalidArgument);
    return feature.error().message;
}

std::string circularRejection(const CircularPatternDefinition& definition) {
    auto feature = CircularPatternFeature::create("Bad", definition);
    REQUIRE_FALSE(feature.has_value());
    CHECK(feature.error().code == ErrorCode::InvalidArgument);
    return feature.error().message;
}

/// Regenerates; exactly @p feature must fail, keeping no body.
Error requireFailure(Regenerator& regenerator, Document& doc, ObjectId feature) {
    const RegenerationReport report = requireReport(regenerator, doc);
    INFO(describe(report));
    REQUIRE(report.failed == std::vector<ObjectId>{feature});
    CHECK(regenerator.body(feature) == nullptr);
    return report.errors.at(feature);
}

/// A cube row of @p count cubes, its count and spacing no longer driven.
LinearPatternDefinition plainRow(const CubeRowModel& m, std::uint32_t count, Length spacing) {
    LinearPatternDefinition d = m.definitionOf(m.row);
    d.first.countParameter.reset();
    d.first.spacingParameter.reset();
    d.first.count = count;
    d.first.spacing = spacing;
    return d;
}

} // namespace

// ---------------------------------------------------------------------------
// Symmetric patterns
// ---------------------------------------------------------------------------

TEST_CASE("LinearPattern_SymmetricInstancesSitEitherSideOfTheSource", "[pattern][p12][acceptance]") {
    CubeRowModel m;
    LinearPatternDefinition d = plainRow(m, 5, 20_mm);
    d.first.symmetric = true;
    m.setDefinition(m.row, d);

    // By hand: m(i) = 0, +1, -1, +2, -2 times 20 mm.
    checkOffsets(offsetsAlongX(d, m.doc), {0.0, 20.0, -20.0, 40.0, -40.0});

    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    // Five separate cubes: the volume is exactly five seeds, and the row
    // spans 4 x 20 mm centred on the source, whose own span is [0, 10].
    CHECK_THAT(volumeMm3(regenerator, m.row), WithinRel(5.0 * kCubeMm3, kRel));
    const geometry::Body& body = requireBody(regenerator, m.row);
    CHECK(body.topology().solids == 5); // five cubes that touch nothing
    const auto bounds = body.boundingBox();
    REQUIRE(bounds.has_value());
    bettercad::test::checkPoint(bounds->min, -40.0, 0.0, 0.0);
    bettercad::test::checkPoint(bounds->max, 50.0, 10.0, 10.0);
    // The centroid of equal cubes at -40, -20, 0, 20, 40 (plus the seed's
    // own 5 mm half-width) is the source's own centre.
    const auto properties = body.massProperties();
    REQUIRE(properties.has_value());
    bettercad::test::checkPoint(properties->centerOfMass, 5.0, 5.0, 5.0);
    CHECK(cubeLeftFaces(regenerator, m.row) == std::vector<double>{-40.0, -20.0, 0.0, 20.0, 40.0});
}

TEST_CASE("LinearPattern_SymmetricKeepsWhatAnIndexMeansWhenTheCountGrows", "[pattern][p12]") {
    // Instance 3 is +2 steps whether the pattern has 5 instances or 9: the
    // copies are numbered outward, so a downstream reference to a face of
    // instance 3 still means the same instance.
    CubeRowModel m;
    LinearPatternDefinition five = plainRow(m, 5, 20_mm);
    five.first.symmetric = true;
    m.setDefinition(m.row, five);
    const std::vector<double> before = offsetsAlongX(five, m.doc);

    LinearPatternDefinition nine = five;
    nine.first.count = 9;
    m.setDefinition(m.row, nine);
    const std::vector<double> after = offsetsAlongX(nine, m.doc);
    REQUIRE(after.size() == 9);
    for (std::size_t i = 0; i < before.size(); ++i) {
        INFO("instance " << i);
        CHECK_THAT(after[i], WithinAbs(before[i], kPositionToleranceMm));
    }
    checkOffsets(after, {0.0, 20.0, -20.0, 40.0, -40.0, 60.0, -60.0, 80.0, -80.0});
}

TEST_CASE("LinearPattern_SymmetricNeedsAnOddCount", "[pattern][p12]") {
    CubeRowModel m;
    LinearPatternDefinition d = plainRow(m, 4, 20_mm);
    d.first.symmetric = true;
    CHECK(rejection(d) == "direction 1: a symmetric direction needs an odd count, so the source is its middle "
                          "instance, got 4");
    d.first.count = 5;
    REQUIRE(LinearPatternFeature::create("Good", d).has_value());

    SECTION("a driven count is checked when it is resolved") {
        LinearPatternDefinition driven = d;
        driven.first.countParameter = m.count;
        m.setDefinition(m.row, driven);
        REQUIRE(m.doc.setParameterValue(m.count, 6.0, kUnitless).has_value());
        Regenerator regenerator;
        const Error error = requireFailure(regenerator, m.doc, m.row);
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK(error.message == "Row: linear pattern: direction 1: a symmetric direction needs an odd count, so the "
                               "source is its middle instance, got 6");
        REQUIRE(m.doc.setParameterValue(m.count, 7.0, kUnitless).has_value());
        CHECK(requireReport(regenerator, m.doc).succeeded());
        CHECK_THAT(volumeMm3(regenerator, m.row), WithinRel(7.0 * kCubeMm3, kRel));
    }
}

TEST_CASE("CircularPattern_SymmetricTurnsItsCopiesBothWays", "[pattern][circular][p12][acceptance]") {
    BoltCircleModel m;
    CircularPatternDefinition d = m.definitionOf(m.bolts);
    d.countParameter.reset();
    d.count = 5;
    d.spacing = CircularSpacing::AngleStep;
    d.angle = 30_deg;
    d.symmetric = true;
    m.setDefinition(m.bolts, d);

    const auto instances = resolveCircularPatternInstances(d, m.doc);
    REQUIRE(instances.has_value());
    // By hand: 0, +30, -30, +60, -60 degrees.
    const std::vector<double> expected{0.0, 30.0, -30.0, 60.0, -60.0};
    REQUIRE(instances->size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        INFO("instance " << i);
        CHECK_THAT((*instances)[i].angle.in(units::deg), WithinAbs(expected[i], 1e-9));
    }

    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK_THAT(volumeMm3(regenerator, m.bolts), WithinRel(BoltCircleModel::expectedVolume(60, 10, 10, 5), kRel));
    // Each bolt is 40 mm from the axis at its own angle.
    const geometry::Body& body = requireBody(regenerator, m.bolts);
    for (const double degrees : expected) {
        const double radians = degrees * pi / 180.0;
        const auto hole = geometry::circleSignature(
            Point3D{40.0 * std::cos(radians) * units::mm, 40.0 * std::sin(radians) * units::mm, 0_mm},
            Direction3D::unitZ(), 5_mm);
        REQUIRE(hole.has_value());
        const auto found = geometry::findEdges(body, *hole);
        REQUIRE(found.has_value());
        INFO(degrees << " deg");
        CHECK(found->size() == 1);
    }
}

TEST_CASE("CircularPattern_SymmetricIsNotAFullCircle", "[pattern][circular][p12]") {
    BoltCircleModel m;
    CircularPatternDefinition d = m.definitionOf(m.bolts);
    d.countParameter.reset();
    d.count = 5;
    d.symmetric = true;
    CHECK(circularRejection(d) ==
          "a full-circle pattern takes no symmetry: its instances already go all the way round");
    d.spacing = CircularSpacing::IncludedAngle;
    d.angle = 120_deg;
    d.count = 4;
    CHECK(circularRejection(d) ==
          "a symmetric pattern needs an odd count, so the source is its middle instance, got 4");
}

// ---------------------------------------------------------------------------
// Total length
// ---------------------------------------------------------------------------

TEST_CASE("LinearPattern_TotalLengthDividesTheRowBetweenItsInstances", "[pattern][p12][acceptance]") {
    CubeRowModel m;
    LinearPatternDefinition d = plainRow(m, 5, 80_mm);
    d.first.distribution = PatternDistribution::TotalLength;
    m.setDefinition(m.row, d);

    // By hand: s = L / (N - 1) = 80 / 4 = 20 mm; the seed is instance 0, so
    // the whole row spans exactly L.
    checkOffsets(offsetsAlongX(d, m.doc), {0.0, 20.0, 40.0, 60.0, 80.0});

    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK_THAT(volumeMm3(regenerator, m.row), WithinRel(5.0 * kCubeMm3, kRel));
    CHECK(cubeLeftFaces(regenerator, m.row) == std::vector<double>{0.0, 20.0, 40.0, 60.0, 80.0});

    SECTION("a smaller count spreads the same length further apart") {
        LinearPatternDefinition three = d;
        three.first.count = 3;
        m.setDefinition(m.row, three);
        checkOffsets(offsetsAlongX(three, m.doc), {0.0, 40.0, 80.0});
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK_THAT(volumeMm3(regenerator, m.row), WithinRel(3.0 * kCubeMm3, kRel));
        CHECK(cubeLeftFaces(regenerator, m.row) == std::vector<double>{0.0, 40.0, 80.0});
    }
    SECTION("a longer row keeps its count") {
        LinearPatternDefinition longer = d;
        longer.first.spacing = 120_mm;
        m.setDefinition(m.row, longer);
        checkOffsets(offsetsAlongX(longer, m.doc), {0.0, 30.0, 60.0, 90.0, 120.0});
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK(cubeLeftFaces(regenerator, m.row) == std::vector<double>{0.0, 30.0, 60.0, 90.0, 120.0});
    }
    SECTION("the same row as a spacing of L / (N - 1)") {
        LinearPatternDefinition spacing = d;
        spacing.first.distribution = PatternDistribution::Spacing;
        spacing.first.spacing = 20_mm;
        CHECK(offsetsAlongX(spacing, m.doc) == offsetsAlongX(d, m.doc));
    }
    SECTION("symmetric about the source, still spanning L") {
        LinearPatternDefinition both = d;
        both.first.symmetric = true;
        m.setDefinition(m.row, both);
        checkOffsets(offsetsAlongX(both, m.doc), {0.0, 20.0, -20.0, 40.0, -40.0});
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK(cubeLeftFaces(regenerator, m.row) == std::vector<double>{-40.0, -20.0, 0.0, 20.0, 40.0});
    }
}

TEST_CASE("LinearPattern_TotalLengthNeedsSomethingToDivide", "[pattern][p12]") {
    CubeRowModel m;
    LinearPatternDefinition d = plainRow(m, 1, 80_mm);
    d.first.distribution = PatternDistribution::TotalLength;
    CHECK(rejection(d) == "direction 1: a total length needs at least 2 instances to divide it between, got 1");

    SECTION("a driven count of 1 fails when it is resolved") {
        LinearPatternDefinition driven = d;
        driven.first.count = 5;
        driven.first.countParameter = m.count;
        m.setDefinition(m.row, driven);
        REQUIRE(m.doc.setParameterValue(m.count, 1.0, kUnitless).has_value());
        Regenerator regenerator;
        const Error error = requireFailure(regenerator, m.doc, m.row);
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK(error.message == "Row: linear pattern: direction 1: a total length needs at least 2 instances to "
                               "divide it between, got 1");
    }
    SECTION("the length itself must be positive and finite") {
        LinearPatternDefinition invalid = plainRow(m, 5, 0_mm);
        invalid.first.distribution = PatternDistribution::TotalLength;
        CHECK(rejection(invalid) == "direction 1: the total length must be positive and finite, got 0 mm");
        invalid.first.spacing = Length::fromSi(std::numeric_limits<double>::quiet_NaN());
        CHECK(rejection(invalid) == "direction 1: the total length must be positive and finite, got nan mm");
        invalid.first.spacing = -(80_mm);
        CHECK(rejection(invalid) == "direction 1: the total length must be positive and finite, got -80 mm");
    }
    SECTION("a parameter can drive the total length") {
        LinearPatternDefinition driven = plainRow(m, 5, 80_mm);
        driven.first.distribution = PatternDistribution::TotalLength;
        driven.first.spacingParameter = m.pitch;
        m.setDefinition(m.row, driven);
        Regenerator regenerator;
        REQUIRE(m.doc.setParameterValue(m.pitch, 80_mm).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK(cubeLeftFaces(regenerator, m.row) == std::vector<double>{0.0, 20.0, 40.0, 60.0, 80.0});
        // The pattern follows the parameter, dividing the new length again:
        // 60 / 4 = 15 mm, which still leaves 5 mm between the cubes.
        REQUIRE(m.doc.setParameterValue(m.pitch, 60_mm).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK(cubeLeftFaces(regenerator, m.row) == std::vector<double>{0.0, 15.0, 30.0, 45.0, 60.0});
        // And it is still refused when it goes to nothing.
        REQUIRE(m.doc.setParameterValue(m.pitch, 0_mm).has_value());
        CHECK(requireFailure(regenerator, m.doc, m.row).message ==
              "Row: linear pattern: direction 1: the total length must be positive and finite, got 0 mm");
    }
}

// ---------------------------------------------------------------------------
// Suppressed instances
// ---------------------------------------------------------------------------

TEST_CASE("LinearPattern_SuppressedInstancesMakeNoGeometry", "[pattern][p12][acceptance]") {
    CubeRowModel m;
    LinearPatternDefinition d = plainRow(m, 8, 20_mm);
    d.suppressed = {2, 5};
    m.setDefinition(m.row, d);

    // The indices do not move: instance 6 is still 6 steps from the source.
    checkOffsets(offsetsAlongX(d, m.doc), {0.0, 20.0, 40.0, 60.0, 80.0, 100.0, 120.0, 140.0});
    const auto instances = resolvePatternInstances(d, m.doc);
    REQUIRE(instances.has_value());
    std::vector<std::size_t> suppressed;
    for (const PatternInstance& instance : *instances) {
        if (instance.suppressed) {
            suppressed.push_back(instance.index);
        }
    }
    CHECK(suppressed == std::vector<std::size_t>{2, 5});

    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    // Six cubes, not eight, and the gaps are at 40 and 100 mm.
    CHECK_THAT(volumeMm3(regenerator, m.row), WithinRel(6.0 * kCubeMm3, kRel));
    CHECK(cubeLeftFaces(regenerator, m.row) == std::vector<double>{0.0, 20.0, 60.0, 80.0, 120.0, 140.0});
    // The last instance still reaches 140 mm: nothing was renumbered.
    const auto bounds = requireBody(regenerator, m.row).boundingBox();
    REQUIRE(bounds.has_value());
    bettercad::test::checkPoint(bounds->max, 150.0, 10.0, 10.0);
}

TEST_CASE("LinearPattern_SuppressingAnInstanceDoesNotRenameTheOthers", "[pattern][p12][references]") {
    // A face of instance 6 is named by its own copy index. Suppressing
    // instance 2 leaves that name pointing at the same geometry, and the
    // name of instance 2 resolves to nothing at all.
    CubeRowModel m;
    LinearPatternDefinition d = plainRow(m, 8, 20_mm);
    m.setDefinition(m.row, d);
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());

    const BodyLookup bodies = [&](ObjectId id) { return regenerator.body(id); };
    const auto nameOfInstance = [&](std::uint32_t instance) {
        FaceSelector selector{.role = FaceRole::StartCap};
        if (instance != 0) {
            selector.copies = {FaceCopy{m.row, instance}};
        }
        return FaceName{m.cube, selector};
    };
    /// Where the named face actually is: its centroid, which is its own
    /// cube's centre in x. A plane would not tell the instances apart, since
    /// every start cap lies in z = 0.
    const auto centreOf = [&](std::uint32_t instance) {
        const auto found = geometry::findNamedFaces(requireBody(regenerator, m.row), nameOfInstance(instance));
        REQUIRE(found.has_value());
        REQUIRE(found->size() == 1);
        return found->front().centroid.x.in(units::mm);
    };
    CHECK_THAT(centreOf(6), WithinAbs(125.0, kPositionToleranceMm));

    d.suppressed = {2};
    m.setDefinition(m.row, d);
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK_THAT(centreOf(6), WithinAbs(125.0, kPositionToleranceMm));
    CHECK_THAT(centreOf(3), WithinAbs(65.0, kPositionToleranceMm));
    // Instance 2 is gone; it is never silently answered with instance 3.
    const auto second = geometry::findNamedFaces(requireBody(regenerator, m.row), nameOfInstance(2));
    REQUIRE(second.has_value());
    CHECK(second->empty());
    // A downstream reference to it fails, rather than moving to another.
    const auto resolved = resolveFacePlane(m.doc, nameOfInstance(2), bodies);
    REQUIRE_FALSE(resolved.has_value());
    CHECK(resolved.error().code == ErrorCode::NotFound);
}

TEST_CASE("LinearPattern_ReEnablingAnInstanceRestoresTheSameIdentity", "[pattern][p12]") {
    CubeRowModel m;
    LinearPatternDefinition plain = plainRow(m, 8, 20_mm);
    m.setDefinition(m.row, plain);
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const geometry::MassProperties before = requireBody(regenerator, m.row).massProperties().value();
    const geometry::TopologySummary topology = requireBody(regenerator, m.row).topology();

    LinearPatternDefinition suppressed = plain;
    suppressed.suppressed = {2, 5};
    m.setDefinition(m.row, suppressed);
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK_THAT(volumeMm3(regenerator, m.row), WithinRel(6.0 * kCubeMm3, kRel));

    m.setDefinition(m.row, plain);
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const geometry::MassProperties after = requireBody(regenerator, m.row).massProperties().value();
    CHECK(after.volume.si() == before.volume.si());
    CHECK(after.surfaceArea.si() == before.surfaceArea.si());
    CHECK(after.centerOfMass.x.si() == before.centerOfMass.x.si());
    CHECK(requireBody(regenerator, m.row).topology() == topology);
    CHECK(cubeLeftFaces(regenerator, m.row) ==
          std::vector<double>{0.0, 20.0, 40.0, 60.0, 80.0, 100.0, 120.0, 140.0});
}

TEST_CASE("LinearPattern_SuppressesTheFirstCopyTheLastAndOnesBetween", "[pattern][p12]") {
    CubeRowModel m;
    Regenerator regenerator;
    const auto rowWith = [&](std::vector<std::uint32_t> suppressed) {
        LinearPatternDefinition d = plainRow(m, 5, 20_mm);
        d.suppressed = std::move(suppressed);
        m.setDefinition(m.row, d);
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        return cubeLeftFaces(regenerator, m.row);
    };
    CHECK(rowWith({1}) == std::vector<double>{0.0, 40.0, 60.0, 80.0});
    CHECK(rowWith({4}) == std::vector<double>{0.0, 20.0, 40.0, 60.0});
    CHECK(rowWith({2}) == std::vector<double>{0.0, 20.0, 60.0, 80.0});
    CHECK(rowWith({1, 2, 3}) == std::vector<double>{0.0, 80.0});
    CHECK(rowWith({}) == std::vector<double>{0.0, 20.0, 40.0, 60.0, 80.0});
}

TEST_CASE("CircularPattern_SuppressedBoltsLeaveTheirHolesUndrilled", "[pattern][circular][p12][acceptance]") {
    BoltCircleModel m;
    CircularPatternDefinition d = m.definitionOf(m.bolts);
    d.suppressed = {1, 4};
    m.setDefinition(m.bolts, d);
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    // Six instances, two suppressed: four holes are drilled.
    CHECK_THAT(volumeMm3(regenerator, m.bolts), WithinRel(BoltCircleModel::expectedVolume(60, 10, 10, 4), kRel));

    const geometry::Body& body = requireBody(regenerator, m.bolts);
    for (std::size_t i = 0; i < 6; ++i) {
        const double radians = static_cast<double>(i) * pi / 3.0;
        const auto rim = geometry::circleSignature(
            Point3D{40.0 * std::cos(radians) * units::mm, 40.0 * std::sin(radians) * units::mm, 0_mm},
            Direction3D::unitZ(), 5_mm);
        REQUIRE(rim.has_value());
        const auto found = geometry::findEdges(body, *rim);
        REQUIRE(found.has_value());
        INFO("instance " << i);
        CHECK(found->size() == (i == 1 || i == 4 ? 0u : 1u));
    }
}

// ---------------------------------------------------------------------------
// Failure paths
// ---------------------------------------------------------------------------

TEST_CASE("LinearPattern_RejectsInvalidSuppression", "[pattern][p12]") {
    CubeRowModel m;
    LinearPatternDefinition d = plainRow(m, 5, 20_mm);

    SECTION("the source itself") {
        d.suppressed = {0, 2};
        CHECK(rejection(d) == "a linear pattern cannot suppress instance 0: it is the source itself");
    }
    SECTION("the same instance twice") {
        d.suppressed = {2, 2};
        CHECK(rejection(d) == "a linear pattern's suppressed instances are listed once, by increasing index, got 2 "
                              "after 2");
    }
    SECTION("out of order") {
        d.suppressed = {3, 1};
        CHECK(rejection(d) == "a linear pattern's suppressed instances are listed once, by increasing index, got 1 "
                              "after 3");
    }
    SECTION("an instance the pattern does not have") {
        d.suppressed = {5};
        m.setDefinition(m.row, d);
        Regenerator regenerator;
        const Error error = requireFailure(regenerator, m.doc, m.row);
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK(error.message == "Row: linear pattern: a linear pattern of 5 instances has no instance 5 to suppress");
    }
    SECTION("every copy") {
        d.suppressed = {1, 2, 3, 4};
        m.setDefinition(m.row, d);
        Regenerator regenerator;
        const Error error = requireFailure(regenerator, m.doc, m.row);
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK(error.message == "Row: linear pattern: a linear pattern cannot suppress every copy: 4 of 5 instances "
                               "leaves the source alone");
    }
    SECTION("a count that falls below a suppressed index") {
        d.suppressed = {4};
        d.first.countParameter = m.count;
        m.setDefinition(m.row, d);
        Regenerator regenerator;
        REQUIRE(m.doc.setParameterValue(m.count, 5.0, kUnitless).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        REQUIRE(m.doc.setParameterValue(m.count, 3.0, kUnitless).has_value());
        const Error error = requireFailure(regenerator, m.doc, m.row);
        CHECK(error.message == "Row: linear pattern: a linear pattern of 3 instances has no instance 4 to suppress");
        // Atomic: the failed pattern kept no body and the source still has its own.
        CHECK(regenerator.body(m.cube) != nullptr);
    }
}

TEST_CASE("CircularPattern_RejectsInvalidSuppression", "[pattern][circular][p12]") {
    BoltCircleModel m;
    CircularPatternDefinition d = m.definitionOf(m.bolts);
    d.suppressed = {0};
    CHECK(circularRejection(d) == "a circular pattern cannot suppress instance 0: it is the source itself");
    d.suppressed = {2, 1};
    CHECK(circularRejection(d) == "a circular pattern's suppressed instances are listed once, by increasing index, "
                                  "got 1 after 2");

    d.suppressed = {6};
    m.setDefinition(m.bolts, d);
    Regenerator regenerator;
    const Error error = requireFailure(regenerator, m.doc, m.bolts);
    CHECK(error.code == ErrorCode::InvalidArgument);
    CHECK(error.message == "Bolts: circular pattern: a circular pattern of 6 instances has no instance 6 to "
                           "suppress");
}

// ---------------------------------------------------------------------------
// Determinism
// ---------------------------------------------------------------------------

TEST_CASE("LinearPattern_DistributionAndSuppressionAreDeterministic", "[pattern][p12][determinism]") {
    const auto build = [](std::uint32_t count, bool symmetric, PatternDistribution distribution,
                          std::vector<std::uint32_t> suppressed) {
        auto model = std::make_unique<CubeRowModel>();
        LinearPatternDefinition d = model->definitionOf(model->row);
        d.first.countParameter.reset();
        d.first.spacingParameter.reset();
        d.first.count = count;
        d.first.spacing = 100_mm;
        d.first.distribution = distribution;
        d.first.symmetric = symmetric;
        d.suppressed = std::move(suppressed);
        model->setDefinition(model->row, d);
        return model;
    };
    const auto fingerprint = [](CubeRowModel& model, Regenerator& regenerator) {
        REQUIRE(requireReport(regenerator, model.doc).succeeded());
        const geometry::Body* body = regenerator.body(model.row);
        REQUIRE(body != nullptr);
        const auto properties = body->massProperties().value();
        return std::vector<double>{properties.volume.si(), properties.surfaceArea.si(),
                                   properties.centerOfMass.x.si(), properties.centerOfMass.y.si(),
                                   properties.centerOfMass.z.si()};
    };

    for (const bool symmetric : {false, true}) {
        for (const auto distribution : {PatternDistribution::Spacing, PatternDistribution::TotalLength}) {
            INFO("symmetric " << symmetric << ", total length " << (distribution == PatternDistribution::TotalLength));
            auto first = build(5, symmetric, distribution, {2});
            Regenerator a;
            const std::vector<double> once = fingerprint(*first, a);
            // The same document regenerated again from scratch.
            Regenerator again;
            CHECK(fingerprint(*first, again) == once);
            // A second document built the same way.
            auto second = build(5, symmetric, distribution, {2});
            Regenerator b;
            CHECK(fingerprint(*second, b) == once);
        }
    }
}

// ---------------------------------------------------------------------------
// Touching and overlapping instances
// ---------------------------------------------------------------------------

TEST_CASE("LinearPattern_TouchingAndOverlappingSymmetricInstancesFuse", "[pattern][p12][acceptance]") {
    // New-body instances are united: separate where they do not meet, fused
    // where they touch or overlap, with the volume of the union, not of the
    // instances counted separately.
    CubeRowModel m;
    Regenerator regenerator;
    SECTION("apart: 5 cubes 20 mm apart about the source stay 5 solids") {
        LinearPatternDefinition d = plainRow(m, 5, 20_mm);
        d.first.symmetric = true;
        m.setDefinition(m.row, d);
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        const geometry::Body& body = requireBody(regenerator, m.row);
        CHECK(body.isValid());
        CHECK(body.topology().solids == 5);
        CHECK_THAT(volumeMm3(regenerator, m.row), WithinRel(5.0 * kCubeMm3, kRel));
    }
    SECTION("touching: 5 cubes 10 mm apart form one 50 mm bar") {
        LinearPatternDefinition d = plainRow(m, 5, 10_mm);
        d.first.symmetric = true;
        m.setDefinition(m.row, d);
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        const geometry::Body& body = requireBody(regenerator, m.row);
        CHECK(body.isValid());
        CHECK(body.topology().solids == 1);
        // Five 10 mm cubes end to end: 50 x 10 x 10 mm, centred on the source.
        CHECK_THAT(volumeMm3(regenerator, m.row), WithinRel(5.0 * kCubeMm3, kRel));
        bettercad::test::checkPoint(body.boundingBox()->min, -20.0, 0.0, 0.0);
        bettercad::test::checkPoint(body.boundingBox()->max, 30.0, 10.0, 10.0);
    }
    SECTION("overlapping: 5 cubes 4 mm apart cover 26 mm, not 50") {
        // The union spans from -8 to 18 mm: 26 x 10 x 10 mm.
        LinearPatternDefinition d = plainRow(m, 5, 4_mm);
        d.first.symmetric = true;
        m.setDefinition(m.row, d);
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        const geometry::Body& body = requireBody(regenerator, m.row);
        CHECK(body.isValid());
        CHECK(body.topology().solids == 1);
        CHECK_THAT(volumeMm3(regenerator, m.row), WithinRel(26.0 * 10.0 * 10.0, kRel));
        bettercad::test::checkPoint(body.boundingBox()->min, -8.0, 0.0, 0.0);
        bettercad::test::checkPoint(body.boundingBox()->max, 18.0, 10.0, 10.0);
    }
    SECTION("a total length of 16 mm over 5 instances is the same 4 mm step") {
        LinearPatternDefinition d = plainRow(m, 5, 16_mm);
        d.first.distribution = PatternDistribution::TotalLength;
        d.first.symmetric = true;
        m.setDefinition(m.row, d);
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK_THAT(volumeMm3(regenerator, m.row), WithinRel(26.0 * 10.0 * 10.0, kRel));
    }
}

// ---------------------------------------------------------------------------
// Undo and redo
// ---------------------------------------------------------------------------

TEST_CASE("LinearPattern_SuppressionAndDistributionAreUndoable", "[pattern][p12][undo][acceptance]") {
    CubeRowModel m;
    CommandHistory history;
    Regenerator regenerator;
    const LinearPatternDefinition plain = plainRow(m, 5, 20_mm);
    REQUIRE(history.execute(m.doc, std::make_unique<ModifyLinearPatternCommand>(
                                      FeatureId::fromValue(m.row.value()), plain))
                .has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const std::vector<double> apart{0.0, 20.0, 40.0, 60.0, 80.0};
    CHECK(cubeLeftFaces(regenerator, m.row) == apart);

    // Suppress two instances.
    LinearPatternDefinition suppressed = plain;
    suppressed.suppressed = {1, 3};
    REQUIRE(history.execute(m.doc, std::make_unique<ModifyLinearPatternCommand>(
                                      FeatureId::fromValue(m.row.value()), suppressed))
                .has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(cubeLeftFaces(regenerator, m.row) == std::vector<double>{0.0, 40.0, 80.0});

    // Then spread the row over a total length instead.
    LinearPatternDefinition total = suppressed;
    total.first.distribution = PatternDistribution::TotalLength;
    total.first.spacing = 120_mm;
    REQUIRE(history.execute(m.doc, std::make_unique<ModifyLinearPatternCommand>(
                                      FeatureId::fromValue(m.row.value()), total))
                .has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(cubeLeftFaces(regenerator, m.row) == std::vector<double>{0.0, 60.0, 120.0});

    // Undo takes back the distribution, then the suppression.
    REQUIRE(history.undo(m.doc).has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(m.definitionOf(m.row) == suppressed);
    CHECK(cubeLeftFaces(regenerator, m.row) == std::vector<double>{0.0, 40.0, 80.0});
    REQUIRE(history.undo(m.doc).has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(m.definitionOf(m.row) == plain);
    CHECK(cubeLeftFaces(regenerator, m.row) == apart);

    // Redo brings the same indices back, not new ones.
    REQUIRE(history.redo(m.doc).has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(m.definitionOf(m.row).suppressed == std::vector<std::uint32_t>{1, 3});
    CHECK(cubeLeftFaces(regenerator, m.row) == std::vector<double>{0.0, 40.0, 80.0});
    REQUIRE(history.redo(m.doc).has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(cubeLeftFaces(regenerator, m.row) == std::vector<double>{0.0, 60.0, 120.0});
}
