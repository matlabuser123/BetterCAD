#include "TestHelpers.hpp"
#include "core/geometry/GeometryTestSupport.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/FaceKindModels.hpp"

#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/features/Datums.hpp>
#include <bettercad/features/FaceReferences.hpp>
#include <bettercad/features/FeatureCommands.hpp>
#include <bettercad/features/FilletFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/Validation.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/sketch/SketchCommands.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using bettercad::test::BevelledBlockModel;
using bettercad::test::DrilledBlockModel;
using bettercad::test::ExpectedBoss;
using bettercad::test::ExpectedFrame;
using bettercad::test::FaceKindModel;
using bettercad::test::LoftedFrustumModel;
using bettercad::test::PostRowModel;
using bettercad::test::requireReport;
using bettercad::test::RevolvedRingModel;
using bettercad::test::SpokeHubModel;
using bettercad::test::SweptBarModel;
using bettercad::test::Vec3;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// P12-SKETCH-003: sketches on the planar faces of revolves, sweeps, lofts,
// holes and chamfers, and on the copies patterns and mirrors make. Expected
// frames, volumes, centres and bounds are written out from the parameters in
// support/FaceKindModels.hpp, never read from a reference.

namespace {

// Planes and cylinders are integrated to rounding (P12-DATUM-001).
constexpr double kRel = bettercad::test::kRelTight;
constexpr double kTolCentreMm = bettercad::test::kPositionToleranceMm;
// Frames come from the kernel's planes: their points to about 1e-14 mm,
// their normals to about 1e-16 (as in P12-STREF-001).
constexpr double kTolMm = 1e-12;
constexpr double kTolUnit = 1e-14;
// Exact bounds (P3), at the kernel's precision.
constexpr double kTolBounds = 1e-7;
// Body::boundingBox() bounds a loft's B-spline sides numerically and pads
// them by the kernel's confusion tolerance, 1e-7 mm (as in LoftFeatureTests).
constexpr double kLoftBoundsPaddingMm = 1e-7;

Vec3 mm(const Point3D& p) {
    return {p.x.in(units::mm), p.y.in(units::mm), p.z.in(units::mm)};
}

Vec3 components(const Direction3D& d) {
    return {d.x(), d.y(), d.z()};
}

void checkVec(const Vec3& actual, const Vec3& expected, double tolerance) {
    CHECK_THAT(actual[0], WithinAbs(expected[0], tolerance));
    CHECK_THAT(actual[1], WithinAbs(expected[1], tolerance));
    CHECK_THAT(actual[2], WithinAbs(expected[2], tolerance));
}

void checkFrame(const Frame3D& frame, const ExpectedFrame& expected) {
    checkVec(mm(frame.origin()), expected.origin, kTolMm);
    checkVec(components(frame.xAxis()), expected.x, kTolUnit);
    checkVec(components(frame.yAxis()), expected.y, kTolUnit);
    checkVec(components(frame.normal()), expected.normal, kTolUnit);
}

const sketch::Sketch& sketchOf(const Document& doc, ObjectId id) {
    const auto* sketch = doc.findObjectAs<sketch::Sketch>(id);
    REQUIRE(sketch != nullptr);
    return *sketch;
}

std::string label(const Document& doc, ObjectId id) {
    return std::format("{} ({})", doc.nameOf(id).value_or("?"), id);
}

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

RegenerationReport regenerate(Regenerator& regenerator, Document& doc) {
    RegenerationReport report = requireReport(regenerator, doc);
    INFO((report.errors.empty() ? std::string{}
                                : std::format("{}: {}", report.errors.begin()->first,
                                              report.errors.begin()->second.message)));
    REQUIRE(report.succeeded());
    return report;
}

/// A body against its expected volume (relative @p rel), centre and bounds.
void checkBody(const Regenerator& regenerator, ObjectId id, double volume, const Vec3& centre,
               const std::optional<std::pair<Vec3, Vec3>>& bounds, double rel = kRel) {
    const geometry::Body* body = regenerator.body(id);
    REQUIRE(body != nullptr);
    CHECK(body->isValid());
    CHECK(body->topology().solids == 1);
    const auto props = body->massProperties();
    REQUIRE(props.has_value());
    CHECK_THAT(props->volume.in(units::mm3), WithinRel(volume, rel));
    checkVec(mm(props->centerOfMass), centre, kTolCentreMm);
    if (bounds) {
        const auto box = body->boundingBox();
        REQUIRE(box.has_value());
        checkVec(mm(box->min), bounds->first, kTolBounds);
        checkVec(mm(box->max), bounds->second, kTolBounds);
    }
}

/// The body's box contains the exact box and exceeds it by at most @p padding.
void checkPaddedBox(const Regenerator& regenerator, ObjectId id, const Vec3& min, const Vec3& max,
                    double padding) {
    const geometry::Body* body = regenerator.body(id);
    REQUIRE(body != nullptr);
    const auto box = body->boundingBox();
    REQUIRE(box.has_value());
    const Vec3 lo = mm(box->min);
    const Vec3 hi = mm(box->max);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        CAPTURE(axis, lo[axis], hi[axis]);
        CHECK(lo[axis] <= min[axis] + kTolCentreMm);
        CHECK(lo[axis] >= min[axis] - padding - kTolCentreMm);
        CHECK(hi[axis] >= max[axis] - kTolCentreMm);
        CHECK(hi[axis] <= max[axis] + padding + kTolCentreMm);
    }
}

/// Every sketch's frame, and the cylinder extruded from it.
void checkBosses(const Regenerator& regenerator, const Document& doc, const std::vector<ExpectedBoss>& bosses) {
    for (const ExpectedBoss& expected : bosses) {
        INFO(label(doc, expected.sketch));
        checkFrame(sketchOf(doc, expected.sketch).placement(), expected.frame);
        checkBody(regenerator, expected.boss, expected.cylinder.volume(), expected.cylinder.centre(),
                  std::pair{expected.cylinder.lower(), expected.cylinder.upper()});
    }
}

/// Every sketch placement and body volume of a document, bit for bit.
struct Snapshot {
    std::vector<Frame3D> placements;
    std::vector<std::uint64_t> volumes;

    friend bool operator==(const Snapshot&, const Snapshot&) = default;
};

Snapshot snapshot(const Regenerator& regenerator, const Document& doc) {
    Snapshot result;
    for (const DocumentObject& object : doc.objects()) {
        const ObjectId id = object.id();
        if (const auto* sketch = dynamic_cast<const sketch::Sketch*>(&object)) {
            result.placements.push_back(sketch->placement());
        }
        if (const geometry::Body* body = regenerator.body(id)) {
            const auto props = body->massProperties();
            REQUIRE(props.has_value());
            result.volumes.push_back(bits(props->volume.si()));
        }
    }
    return result;
}

void setValue(CommandHistory& history, Document& doc, ParameterId parameter, DimensionedValue value) {
    REQUIRE(history
                .execute(doc, std::make_unique<ModifyParameterCommand>(parameter, ParameterChanges{.value = value}))
                .has_value());
}

DimensionedValue countOf(double value) {
    return DimensionedValue{.dimension = dimensions::dimensionless, .siValue = value};
}

void undo(CommandHistory& history, Document& doc, int times) {
    for (int i = 0; i < times; ++i) {
        REQUIRE(history.undo(doc).has_value());
    }
}

Result<void> attach(CommandHistory& history, Document& doc, ObjectId sketch, const PlaneReference& reference) {
    auto executed = history.execute(
        doc, std::make_unique<sketch::ModifySketchCommand>(
                 SketchId::fromValue(sketch.value()), "Attach", [reference](sketch::Sketch& s) -> Result<void> {
                     auto set = s.setAttachment(reference);
                     if (!set) {
                         return std::unexpected(set.error());
                     }
                     return {};
                 }));
    if (!executed) {
        return std::unexpected(executed.error());
    }
    return {};
}

/// In @p report, @p item failed with @p code and @p message, and each of
/// @p blocked was blocked.
void checkFailure(const RegenerationReport& report, ObjectId item, ErrorCode code, const std::string& message,
                  const std::vector<ObjectId>& blocked = {}) {
    CHECK(std::ranges::find(report.failed, item) != report.failed.end());
    REQUIRE(report.errors.contains(item));
    CHECK(report.errors.at(item).code == code);
    CHECK(report.errors.at(item).message == message);
    for (const ObjectId id : blocked) {
        CHECK(std::ranges::find(report.blocked, id) != report.blocked.end());
    }
}

/// Regenerates: @p item fails as checkFailure() expects.
RegenerationReport failsWith(Regenerator& regenerator, Document& doc, ObjectId item, ErrorCode code,
                             const std::string& message, const std::vector<ObjectId>& blocked = {}) {
    RegenerationReport report = requireReport(regenerator, doc);
    checkFailure(report, item, code, message, blocked);
    return report;
}

/// Attaches @p sketch to @p reference, expects the failure, and undoes it.
void refused(CommandHistory& history, Regenerator& regenerator, Document& doc, ObjectId sketch,
             const PlaneReference& reference, ErrorCode code, const std::string& message) {
    CAPTURE(message);
    REQUIRE(attach(history, doc, sketch, reference).has_value());
    const Frame3D before = sketchOf(doc, sketch).placement();
    failsWith(regenerator, doc, sketch, code, message);
    CHECK(sketchOf(doc, sketch).placement() == before);
    undo(history, doc, 1);
}

} // namespace

// --- Each kind of face follows its feature ---------------------------------------------------------------

TEST_CASE("SketchOnFace_RevolveCapsAndSidesFollowTheTurnAndLength",
          "[features][references][revolve][regeneration][p12][acceptance]") {
    RevolvedRingModel m;
    Regenerator regenerator;
    const auto check = [&](double turn, double length) {
        CAPTURE(turn, length);
        checkBody(regenerator, m.ring, RevolvedRingModel::volume(turn, length),
                  RevolvedRingModel::centre(turn, length), RevolvedRingModel::bounds(turn, length));
        checkBosses(regenerator, m.doc, m.bosses(turn, length));
    };
    regenerate(regenerator, m.doc);
    check(120.0, 20.0);
    const Snapshot first = snapshot(regenerator, m.doc);

    CommandHistory history;
    setValue(history, m.doc, m.turn, DimensionedValue::of(150_deg));
    const RegenerationReport turned = regenerate(regenerator, m.doc);
    for (const ObjectId id : {m.ring, m.endSketch, m.endBoss, m.startSketch, m.startBoss}) {
        CHECK(std::ranges::find(turned.regenerated, id) != turned.regenerated.end());
    }
    check(150.0, 20.0);
    // Undo restores every placement and volume bit for bit.
    undo(history, m.doc, 1);
    regenerate(regenerator, m.doc);
    CHECK(snapshot(regenerator, m.doc) == first);

    setValue(history, m.doc, m.turn, DimensionedValue::of(150_deg));
    setValue(history, m.doc, m.ringLength, DimensionedValue::of(30_mm));
    regenerate(regenerator, m.doc);
    check(150.0, 30.0);
    // ring_length drives a sketch dimension: undo re-solves the profile from
    // its 30 mm shape, which the solver returns to 20 mm to rounding, not bit
    // for bit (a property of the sketch solver, P0).
    undo(history, m.doc, 2);
    regenerate(regenerator, m.doc);
    check(120.0, 20.0);
}

TEST_CASE("SketchOnFace_RevolveStartCapStaysOnTheSketchPlaneInEveryDirection",
          "[features][references][revolve][regeneration][p12]") {
    // With t = 120 deg: a positive turn spans 0..t, a negative one -t..0.
    // Either way the start cap is the sketch plane z = 0, facing away from
    // the material (+Z for positive, -Z for negative), and the end cap lies
    // at the turn.
    RevolvedRingModel m;
    Regenerator regenerator;
    CommandHistory history;
    const double t = 120.0 * bettercad::test::kPi / 180.0;
    const double c = std::cos(t);
    const double s = std::sin(t);
    const auto turnTo = [&](RevolveDirection direction) {
        RevolveDefinition d = m.doc.findObjectAs<RevolveFeature>(m.ring)->definition();
        d.direction = direction;
        REQUIRE(history
                    .execute(m.doc, std::make_unique<ModifyFeatureCommand<RevolveFeature>>(
                                        FaceKindModel::featureOf(m.ring), d))
                    .has_value());
        regenerate(regenerator, m.doc);
    };
    const double radial = bettercad::test::sectorCentroid(20.0, 40.0, t / 2.0);

    SECTION("positive") {
        turnTo(RevolveDirection::Positive);
        checkBody(regenerator, m.ring, RevolvedRingModel::volume(120.0, 20.0),
                  {radial * std::cos(t / 2.0), 10.0, -radial * std::sin(t / 2.0)}, std::nullopt);
        const Vec3 end{-s, 0.0, -c};
        checkBosses(regenerator, m.doc,
                    {{m.startSketch, m.startBoss, {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
                      {{10.0, -30.0, 0.0}, {0, 0, 1}, 4.0, 6.0}},
                     {m.endSketch, m.endBoss, {{0, 0, 0}, {0, 1, 0}, {c, 0, -s}, end},
                      {{30.0 * c, 10.0, -30.0 * s}, end, 4.0, 8.0}}});
    }
    SECTION("negative") {
        turnTo(RevolveDirection::Negative);
        checkBody(regenerator, m.ring, RevolvedRingModel::volume(120.0, 20.0),
                  {radial * std::cos(t / 2.0), 10.0, radial * std::sin(t / 2.0)}, std::nullopt);
        const Vec3 end{-s, 0.0, c};
        checkBosses(regenerator, m.doc,
                    {{m.startSketch, m.startBoss, {{0, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 0, -1}},
                      {{10.0, 30.0, 0.0}, {0, 0, -1}, 4.0, 6.0}},
                     {m.endSketch, m.endBoss, {{0, 0, 0}, {0, 1, 0}, {-c, 0, -s}, end},
                      {{-30.0 * c, 10.0, -30.0 * s}, end, 4.0, 8.0}}});
    }
    undo(history, m.doc, 1);
    regenerate(regenerator, m.doc);
    checkBosses(regenerator, m.doc, m.bosses(120.0, 20.0));
}

TEST_CASE("SketchOnFace_SweepCapsAndSidesFollowTheirPathEdges",
          "[features][references][sweep][regeneration][p12][acceptance]") {
    SweptBarModel m;
    Regenerator regenerator;
    const auto check = [&](double lift, double rise, double run, double thick) {
        CAPTURE(lift, rise, run, thick);
        checkBody(regenerator, m.bar, SweptBarModel::volume(rise, run, thick),
                  SweptBarModel::centre(lift, rise, run, thick), SweptBarModel::bounds(lift, rise, run, thick));
        checkBosses(regenerator, m.doc, m.bosses(lift, rise, run, thick));
    };
    regenerate(regenerator, m.doc);
    check(10.0, 40.0, 50.0, 8.0);

    CommandHistory history;
    setValue(history, m.doc, m.lift, DimensionedValue::of(15_mm)); // the start cap rises
    regenerate(regenerator, m.doc);
    check(15.0, 40.0, 50.0, 8.0);
    setValue(history, m.doc, m.rise, DimensionedValue::of(50_mm)); // the bend and Across rise
    regenerate(regenerator, m.doc);
    check(15.0, 50.0, 50.0, 8.0);
    setValue(history, m.doc, m.run, DimensionedValue::of(60_mm)); // the end cap moves out
    regenerate(regenerator, m.doc);
    check(15.0, 50.0, 60.0, 8.0);
    setValue(history, m.doc, m.thick, DimensionedValue::of(12_mm)); // the top rises
    regenerate(regenerator, m.doc);
    check(15.0, 50.0, 60.0, 12.0);

    // Every parameter here drives a sketch dimension: undo restores the
    // model to rounding (see the revolve case above).
    undo(history, m.doc, 4);
    regenerate(regenerator, m.doc);
    check(10.0, 40.0, 50.0, 8.0);
}

TEST_CASE("SketchOnFace_LoftCapsFollowTheSectionOffsets",
          "[features][references][loft][regeneration][p12][acceptance]") {
    LoftedFrustumModel m;
    Regenerator regenerator;
    const auto check = [&](double base, double top) {
        CAPTURE(base, top);
        checkBody(regenerator, m.frustum, LoftedFrustumModel::volume(base, top),
                  LoftedFrustumModel::centre(base, top), std::nullopt);
        checkPaddedBox(regenerator, m.frustum, {-20, -20, base}, {20, 20, top}, kLoftBoundsPaddingMm);
        checkBosses(regenerator, m.doc, m.bosses(base, top));
    };
    regenerate(regenerator, m.doc);
    check(5.0, 35.0);
    const Snapshot first = snapshot(regenerator, m.doc);

    CommandHistory history;
    setValue(history, m.doc, m.baseOffset, DimensionedValue::of(8_mm));
    setValue(history, m.doc, m.topOffset, DimensionedValue::of(45_mm));
    regenerate(regenerator, m.doc);
    check(8.0, 45.0);

    undo(history, m.doc, 2);
    regenerate(regenerator, m.doc);
    check(5.0, 35.0);
    CHECK(snapshot(regenerator, m.doc) == first);
}

TEST_CASE("SketchOnFace_HoleBottomsAndCounterboreFloorsFollowTheirDepths",
          "[features][references][hole][regeneration][p12][acceptance]") {
    DrilledBlockModel m;
    Regenerator regenerator;
    const auto check = [&](double bore, double counterbore) {
        CAPTURE(bore, counterbore);
        checkBody(regenerator, m.seat, DrilledBlockModel::volume(bore, counterbore),
                  DrilledBlockModel::centre(bore, counterbore), std::pair{Vec3{0, 0, 0}, Vec3{60, 40, 30}});
        checkBosses(regenerator, m.doc, m.bosses(bore, counterbore));
    };
    regenerate(regenerator, m.doc);
    check(12.0, 5.0);
    const Snapshot first = snapshot(regenerator, m.doc);

    CommandHistory history;
    setValue(history, m.doc, m.boreDepth, DimensionedValue::of(18_mm));
    regenerate(regenerator, m.doc);
    check(18.0, 5.0);
    // A counterbore's depth has no parameter: the edit moves the floor, not
    // Seat's bottom.
    HoleDefinition deeper = m.doc.findObjectAs<HoleFeature>(m.seat)->definition();
    deeper.counterboreDepth = 8_mm;
    REQUIRE(history
                .execute(m.doc, std::make_unique<ModifyFeatureCommand<HoleFeature>>(
                                    FaceKindModel::featureOf(m.seat), deeper))
                .has_value());
    regenerate(regenerator, m.doc);
    check(18.0, 8.0);

    undo(history, m.doc, 2);
    regenerate(regenerator, m.doc);
    check(12.0, 5.0);
    CHECK(snapshot(regenerator, m.doc) == first);
}

TEST_CASE("SketchOnFace_ChamferFacesFollowTheDistance",
          "[features][references][chamfer][regeneration][p12][acceptance]") {
    BevelledBlockModel m;
    Regenerator regenerator;
    const auto check = [&](double bevel) {
        CAPTURE(bevel);
        checkBody(regenerator, m.chamfer, BevelledBlockModel::volume(bevel), BevelledBlockModel::centre(bevel),
                  std::pair{Vec3{0, 0, 0}, Vec3{60, 40, 30}});
        checkBosses(regenerator, m.doc, m.bosses(bevel));
    };
    regenerate(regenerator, m.doc);
    check(4.0);
    const Snapshot first = snapshot(regenerator, m.doc);

    CommandHistory history;
    setValue(history, m.doc, m.bevel, DimensionedValue::of(6_mm));
    regenerate(regenerator, m.doc);
    check(6.0);

    undo(history, m.doc, 1);
    regenerate(regenerator, m.doc);
    check(4.0);
    CHECK(snapshot(regenerator, m.doc) == first);
}

// --- Copies ----------------------------------------------------------------------------------------------

TEST_CASE("SketchOnFace_PatternAndMirrorCopiesFollowTheirInstance",
          "[features][references][pattern][mirror][regeneration][p12][acceptance]") {
    PostRowModel m;
    Regenerator regenerator;
    const auto checkRow = [&](double pitch, double posts) {
        CAPTURE(pitch, posts);
        const Vec3 centre = PostRowModel::rowCentre(pitch, posts);
        checkBody(regenerator, m.row, PostRowModel::rowVolume(posts), centre,
                  std::pair{Vec3{0, 0, 0}, Vec3{100, 30, 30}});
        checkBody(regenerator, m.flip, 2.0 * PostRowModel::rowVolume(posts), {100.0, centre[1], centre[2]},
                  std::pair{Vec3{0, 0, 0}, Vec3{200, 30, 30}});
    };
    const auto check = [&](double pitch, double posts) {
        checkRow(pitch, posts);
        checkBosses(regenerator, m.doc, m.bosses(pitch));
    };
    regenerate(regenerator, m.doc);
    check(30.0, 3.0);
    const Snapshot first = snapshot(regenerator, m.doc);

    CommandHistory history;
    setValue(history, m.doc, m.pitch, DimensionedValue::of(35_mm));
    const RegenerationReport spaced = regenerate(regenerator, m.doc);
    for (const ObjectId id : {m.row, m.flip, m.copySketch, m.imageSketch, m.gauge, m.gaugeSketch}) {
        CHECK(std::ranges::find(spaced.regenerated, id) != spaced.regenerated.end());
    }
    check(35.0, 3.0);

    // Two posts: copy 2 is gone. Copy 1's right side (x = 55) is still
    // there, and is not taken instead.
    const Frame3D copyPlacement = sketchOf(m.doc, m.copySketch).placement();
    setValue(history, m.doc, m.posts, countOf(2.0));
    const std::string copy2 = std::format("the side from {} of Post (object:6), copy 2 of Row (object:7)", m.right);
    const RegenerationReport fewer = requireReport(regenerator, m.doc);
    checkFailure(fewer, m.copySketch, ErrorCode::NotFound,
                 std::format("CopySketch (object:9): {} is not a face of its body (the feature's operation left no "
                             "such face)",
                             copy2),
                 {m.copyBoss});
    checkFailure(fewer, m.imageSketch, ErrorCode::NotFound,
                 std::format("ImageSketch (object:11): {}, copy 1 of Flip (object:8) is not a face of its body (the "
                             "feature's operation left no such face)",
                             copy2),
                 {m.imageBoss});
    CHECK(fewer.failed == std::vector<ObjectId>{m.copySketch, m.imageSketch});
    checkRow(35.0, 2.0);
    const auto copy1 =
        geometry::findNamedFaces(*regenerator.body(m.row), FaceName{m.post, m.rightOf({{m.row, 1}})});
    REQUIRE(copy1.has_value());
    REQUIRE(copy1->size() == 1);
    CHECK_THAT(copy1->front().signature->point.x.in(units::mm), WithinAbs(55.0, kTolMm));
    // The failed sketches keep their last placements; Gauge still follows copy 1.
    checkBosses(regenerator, m.doc, {m.bosses(35.0)[2]});
    CHECK(sketchOf(m.doc, m.copySketch).placement() == copyPlacement);

    undo(history, m.doc, 1);
    regenerate(regenerator, m.doc);
    check(35.0, 3.0);
    undo(history, m.doc, 1);
    regenerate(regenerator, m.doc);
    check(30.0, 3.0);
    CHECK(snapshot(regenerator, m.doc) == first);
}

TEST_CASE("SketchOnFace_CircularCopiesFollowTheCount",
          "[features][references][pattern][regeneration][p12][acceptance]") {
    SpokeHubModel m;
    Regenerator regenerator;
    const auto check = [&](double spokes) {
        CAPTURE(spokes);
        checkBody(regenerator, m.pattern, SpokeHubModel::volume(spokes), SpokeHubModel::centre(spokes),
                  std::pair{Vec3{-40, -40, 0}, Vec3{40, 40, 20}});
        checkBosses(regenerator, m.doc, m.bosses(spokes));
    };
    regenerate(regenerator, m.doc);
    check(4.0);
    const Snapshot first = snapshot(regenerator, m.doc);

    CommandHistory history;
    setValue(history, m.doc, m.spokes, countOf(3.0)); // copy 1 turns from 90 to 120 deg
    regenerate(regenerator, m.doc);
    check(3.0);
    setValue(history, m.doc, m.spokes, countOf(1.0)); // no copies
    failsWith(regenerator, m.doc, m.spokeSketch, ErrorCode::NotFound,
              std::format("SpokeSketch (object:7): the side from {} of Lug (object:5), copy 1 of Spokes (object:6) "
                          "is not a face of its body (the feature's operation left no such face)",
                          m.upper),
              {m.spokeBoss});

    undo(history, m.doc, 1);
    regenerate(regenerator, m.doc);
    check(3.0);
    undo(history, m.doc, 1);
    regenerate(regenerator, m.doc);
    check(4.0);
    CHECK(snapshot(regenerator, m.doc) == first);
}

TEST_CASE("SketchOnFace_ReferencesToCopiesDependOnTheCopyingFeatures", "[features][references][pattern][p12]") {
    PostRowModel m;
    const DocumentGraph graph = buildDependencyGraph(m.doc);
    const auto dependsOn = [&](ObjectId item, ObjectId on) {
        const auto deps = graph.graph.dependenciesOf(item);
        return std::ranges::find(deps, on) != deps.end();
    };
    CHECK(dependsOn(m.copySketch, m.post));
    CHECK(dependsOn(m.copySketch, m.row));
    CHECK_FALSE(dependsOn(m.copySketch, m.flip));
    CHECK(dependsOn(m.imageSketch, m.post));
    CHECK(dependsOn(m.imageSketch, m.row));
    CHECK(dependsOn(m.imageSketch, m.flip));
    CHECK(dependsOn(m.gauge, m.row));
    CHECK(sketchOf(m.doc, m.imageSketch).dependencies() == std::vector<ObjectId>{m.post, m.row, m.flip});

    // Deleting the mirror fails the sketch on its copy before any face is looked for.
    Regenerator regenerator;
    regenerate(regenerator, m.doc);
    CommandHistory history;
    REQUIRE(history.execute(m.doc, std::make_unique<DeleteObjectCommand>(m.flip)).has_value());
    failsWith(regenerator, m.doc, m.imageSketch, ErrorCode::NotFound,
              "object:11 references object:8, which does not exist", {m.imageBoss});
    const auto direct = resolvePlane(m.doc, sketchOf(m.doc, m.imageSketch).attachment().value(),
                                     [&](ObjectId id) { return regenerator.body(id); });
    REQUIRE_FALSE(direct.has_value());
    CHECK(direct.error().message == "object:8 does not exist");
    undo(history, m.doc, 1);
    regenerate(regenerator, m.doc);
    checkBosses(regenerator, m.doc, m.bosses(30.0));
}

// --- Failures --------------------------------------------------------------------------------------------

TEST_CASE("SketchOnFace_CurvedAndRemovedFacesFailWithoutSubstitution",
          "[features][references][regeneration][p12][acceptance]") {
    SECTION("a full turn has no caps; its flat side remains") {
        RevolvedRingModel m;
        Regenerator regenerator;
        regenerate(regenerator, m.doc);
        CommandHistory history;
        setValue(history, m.doc, m.turn, DimensionedValue::of(360_deg));
        const RegenerationReport report = requireReport(regenerator, m.doc);
        checkFailure(report, m.endSketch, ErrorCode::NotFound,
                     "EndSketch (object:5): the end cap of Ring (object:4) is not a face of its body (the feature's "
                     "operation left no such face)",
                     {m.endBoss});
        checkFailure(report, m.startSketch, ErrorCode::NotFound,
                     "StartSketch (object:7): the start cap of Ring (object:4) is not a face of its body (the "
                     "feature's operation left no such face)",
                     {m.startBoss});
        CHECK(report.failed == std::vector<ObjectId>{m.endSketch, m.startSketch});
        // A full ring: pi (R^2 - r^2) L, centred on the axis.
        checkBody(regenerator, m.ring, RevolvedRingModel::volume(360.0, 20.0), {0.0, 10.0, 0.0},
                  std::pair{Vec3{-40, 0, -40}, Vec3{40, 20, 40}});
        checkBosses(regenerator, m.doc, {m.bosses(360.0, 20.0)[2]});
        undo(history, m.doc, 1);
        regenerate(regenerator, m.doc);
        checkBosses(regenerator, m.doc, m.bosses(120.0, 20.0));
    }
    SECTION("cylinders are not planes") {
        RevolvedRingModel ring;
        Regenerator ringRegenerator;
        regenerate(ringRegenerator, ring.doc);
        CommandHistory ringHistory;
        for (const EntityId curved : {ring.outer, ring.inner}) {
            refused(ringHistory, ringRegenerator, ring.doc, ring.topSketch,
                    RevolvedRingModel::faceOf(ring.ring, {.role = FaceRole::Side, .entity = curved}),
                    ErrorCode::InvalidArgument,
                    std::format("TopSketch (object:9): the side from {} of Ring (object:4) is a cylinder, not a "
                                "plane",
                                curved));
        }
        SweptBarModel bar;
        Regenerator barRegenerator;
        regenerate(barRegenerator, bar.doc);
        CommandHistory barHistory;
        for (const EntityId curved : {bar.right, bar.left}) {
            refused(barHistory, barRegenerator, bar.doc, bar.bendSketch,
                    SweptBarModel::faceOf(bar.bar, SweptBarModel::side(curved, bar.bend)),
                    ErrorCode::InvalidArgument,
                    std::format("BendSketch (object:17): the side from {} along {} of Bar (object:8) is a "
                                "cylinder, not a plane",
                                curved, bar.bend));
        }
        // The top line's side along Up is flat, and lies where the bend's does.
        REQUIRE(attach(barHistory, bar.doc, bar.bendSketch,
                       SweptBarModel::faceOf(bar.bar, SweptBarModel::side(bar.top, bar.up)))
                    .has_value());
        regenerate(barRegenerator, bar.doc);
        checkBosses(barRegenerator, bar.doc, {bar.bosses(10.0, 40.0, 50.0, 8.0)[4]});
    }
    SECTION("a name belongs to one feature: faces alike in kind are not confused") {
        DrilledBlockModel m;
        Regenerator regenerator;
        regenerate(regenerator, m.doc);
        // Seat's body holds both holes' bottoms (z = 18 and z = 10), each
        // under its own feature's name, and one counterbore floor, Seat's.
        const geometry::Body& body = *regenerator.body(m.seat);
        const auto named = [&](ObjectId feature, FaceRole role) {
            auto faces = geometry::findNamedFaces(body, FaceName{feature, {.role = role}});
            REQUIRE(faces.has_value());
            return *faces;
        };
        const auto boreBottom = named(m.bore, FaceRole::HoleBottom);
        const auto seatBottom = named(m.seat, FaceRole::HoleBottom);
        REQUIRE(boreBottom.size() == 1);
        REQUIRE(seatBottom.size() == 1);
        CHECK_THAT(boreBottom[0].signature->point.z.in(units::mm), WithinAbs(18.0, kTolMm));
        CHECK_THAT(seatBottom[0].signature->point.z.in(units::mm), WithinAbs(10.0, kTolMm));
        CHECK(named(m.bore, FaceRole::CounterboreFloor).empty());
        CHECK(named(m.seat, FaceRole::CounterboreFloor).size() == 1);
        // A reference to Bore's bottom resolves in Bore's own body.
        const auto plane = resolvePlane(m.doc, DrilledBlockModel::faceOf(m.bore, {.role = FaceRole::HoleBottom}),
                                        [&](ObjectId id) { return id == m.bore ? regenerator.body(id) : nullptr; });
        REQUIRE(plane.has_value());
        checkFrame(*plane, {{0, 0, 18}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}});
    }
}

TEST_CASE("SketchOnFace_RolesAndCopiesAFeatureDoesNotHaveAreRefused", "[features][references][p12]") {
    SECTION("revolves, sweeps and lofts") {
        RevolvedRingModel ring;
        Regenerator ringRegenerator;
        regenerate(ringRegenerator, ring.doc);
        CommandHistory history;
        CommandHistory barHistory;
        CommandHistory loftHistory;
        refused(history, ringRegenerator, ring.doc, ring.topSketch,
                RevolvedRingModel::faceOf(ring.ring, {.role = FaceRole::Side, .entity = ring.top, .along = ring.top}),
                ErrorCode::InvalidArgument,
                "TopSketch (object:9): Ring (object:4) is a revolve, whose sides are not named by a path edge");
        refused(history, ringRegenerator, ring.doc, ring.topSketch,
                RevolvedRingModel::faceOf(ring.ring, {.role = FaceRole::HoleBottom}), ErrorCode::InvalidArgument,
                "TopSketch (object:9): Ring (object:4) is a revolve, which has no hole bottom");

        SweptBarModel bar;
        Regenerator barRegenerator;
        regenerate(barRegenerator, bar.doc);
        refused(barHistory, barRegenerator, bar.doc, bar.sideSketch,
                SweptBarModel::faceOf(bar.bar, {.role = FaceRole::Side, .entity = bar.right}),
                ErrorCode::InvalidArgument,
                "SideSketch (object:13): Bar (object:8) is a sweep, whose sides are named by a profile entity and a "
                "path edge");
        refused(barHistory, barRegenerator, bar.doc, bar.sideSketch,
                SweptBarModel::faceOf(bar.bar, SweptBarModel::side(bar.right, EntityId::fromValue(99))),
                ErrorCode::NotFound, "SideSketch (object:13): Bar (object:8): entity:99 is not an edge of its path");
        refused(barHistory, barRegenerator, bar.doc, bar.sideSketch,
                SweptBarModel::faceOf(bar.bar, SweptBarModel::side(EntityId::fromValue(99), bar.up)),
                ErrorCode::NotFound,
                "SideSketch (object:13): Bar (object:8): entity:99 is not an entity of its profile BarProfile "
                "(object:6)");
        refused(barHistory, barRegenerator, bar.doc, bar.sideSketch,
                SweptBarModel::faceOf(bar.bar, {.role = FaceRole::Chamfer, .edge = 1}), ErrorCode::InvalidArgument,
                "SideSketch (object:13): Bar (object:8) is a sweep, which has no chamfer face");

        LoftedFrustumModel loft;
        Regenerator loftRegenerator;
        regenerate(loftRegenerator, loft.doc);
        const auto squareLine = sketchOf(loft.doc, loft.bottomSquare).entities().back().id;
        refused(loftHistory, loftRegenerator, loft.doc, loft.topSketch,
                LoftedFrustumModel::faceOf(loft.frustum, {.role = FaceRole::Side, .entity = squareLine}),
                ErrorCode::InvalidArgument,
                "TopSketch (object:6): Frustum (object:5) is a loft, whose sides are not planes and are not named");
        refused(loftHistory, loftRegenerator, loft.doc, loft.topSketch,
                LoftedFrustumModel::faceOf(loft.frustum, {.role = FaceRole::CounterboreFloor}),
                ErrorCode::InvalidArgument,
                "TopSketch (object:6): Frustum (object:5) is a loft, which has no counterbore floor");
    }
    SECTION("holes and chamfers") {
        DrilledBlockModel holes;
        Regenerator holeRegenerator;
        regenerate(holeRegenerator, holes.doc);
        CommandHistory history;
        refused(history, holeRegenerator, holes.doc, holes.pinSketch,
                DrilledBlockModel::faceOf(holes.bore, {.role = FaceRole::CounterboreFloor}),
                ErrorCode::InvalidArgument,
                "PinSketch (object:6): Bore (object:4) is not counterbored, so it has no counterbore floor");
        refused(history, holeRegenerator, holes.doc, holes.pinSketch,
                DrilledBlockModel::faceOf(holes.bore, {.role = FaceRole::EndCap}), ErrorCode::InvalidArgument,
                "PinSketch (object:6): Bore (object:4) is a hole, which has no end cap");
        // Made a through hole, Bore has no bottom; the pin fails, nothing else.
        HoleDefinition through = holes.doc.findObjectAs<HoleFeature>(holes.bore)->definition();
        through.extent = geometry::HoleExtent::Through;
        through.depthParameter.reset();
        REQUIRE(history
                    .execute(holes.doc, std::make_unique<ModifyFeatureCommand<HoleFeature>>(
                                            FaceKindModel::featureOf(holes.bore), through))
                    .has_value());
        const RegenerationReport report =
            failsWith(holeRegenerator, holes.doc, holes.pinSketch, ErrorCode::InvalidArgument,
                      "PinSketch (object:6): Bore (object:4) is a through hole, which has no bottom", {holes.pin});
        CHECK(report.failed == std::vector<ObjectId>{holes.pinSketch});
        undo(history, holes.doc, 1);
        regenerate(holeRegenerator, holes.doc);

        BevelledBlockModel bevel;
        Regenerator bevelRegenerator;
        regenerate(bevelRegenerator, bevel.doc);
        CommandHistory bevelHistory;
        refused(bevelHistory, bevelRegenerator, bevel.doc, bevel.backSketch,
                BevelledBlockModel::faceOf(bevel.chamfer, BevelledBlockModel::edgeFace(3)), ErrorCode::NotFound,
                "BackSketch (object:7): Bevel (object:4) has 2 edge references, not 3");
        refused(bevelHistory, bevelRegenerator, bevel.doc, bevel.backSketch,
                BevelledBlockModel::faceOf(bevel.chamfer, {.role = FaceRole::EndCap}), ErrorCode::InvalidArgument,
                "BackSketch (object:7): Bevel (object:4) is a chamfer, which has no end cap");
        // A fillet names no faces of its own.
        const ObjectId round = bevel.add(FilletFeature::create(
            "Round", {.target = FaceKindModel::featureOf(bevel.chamfer),
                      .edges = {geometry::lineSignature(Point3D{}, Direction3D::unitX())},
                      .radius = 2_mm}));
        regenerate(bevelRegenerator, bevel.doc);
        refused(bevelHistory, bevelRegenerator, bevel.doc, bevel.backSketch,
                BevelledBlockModel::faceOf(round, {.role = FaceRole::EndCap}), ErrorCode::InvalidArgument,
                "BackSketch (object:7): Round (object:9) is a fillet, whose faces are not named (extrudes, revolves, "
                "sweeps, lofts, holes and chamfers name theirs)");
        // Its body carries the chamfer's names, on the rounded faces.
        const auto back = geometry::findNamedFaces(*bevelRegenerator.body(round),
                                                   FaceName{bevel.chamfer, BevelledBlockModel::edgeFace(2)});
        REQUIRE(back.has_value());
        CHECK(back->size() == 1);
    }
    SECTION("copies") {
        PostRowModel m;
        Regenerator regenerator;
        regenerate(regenerator, m.doc);
        CommandHistory history;
        refused(history, regenerator, m.doc, m.copySketch,
                PostRowModel::faceOf(m.row, {.role = FaceRole::EndCap}), ErrorCode::InvalidArgument,
                "CopySketch (object:9): Row (object:7) is a linear pattern, whose faces are copies: name the face it "
                "copies, and the copy");
        refused(history, regenerator, m.doc, m.copySketch,
                PostRowModel::faceOf(m.flip, {.role = FaceRole::EndCap}), ErrorCode::InvalidArgument,
                "CopySketch (object:9): Flip (object:8) is a mirror, whose faces are copies: name the face it "
                "copies, and the copy");
        refused(history, regenerator, m.doc, m.copySketch, PostRowModel::faceOf(m.post, m.rightOf({{m.plate, 1}})),
                ErrorCode::InvalidArgument,
                "CopySketch (object:9): Plate (object:4) is an extrude, which makes no copies (patterns and mirrors "
                "do)");
        refused(history, regenerator, m.doc, m.copySketch,
                PostRowModel::faceOf(m.post, m.rightOf({{m.row, 1}, {m.flip, 2}})), ErrorCode::InvalidArgument,
                "CopySketch (object:9): Flip (object:8) is a mirror, whose only copy is instance 1, not 2");
        // A copy the pattern never made; and copies in the wrong order.
        refused(history, regenerator, m.doc, m.copySketch, PostRowModel::faceOf(m.post, m.rightOf({{m.row, 9}})),
                ErrorCode::NotFound,
                std::format("CopySketch (object:9): the side from {} of Post (object:6), copy 9 of Row (object:7) is "
                            "not a face of its body (the feature's operation left no such face)",
                            m.right));
        refused(history, regenerator, m.doc, m.imageSketch,
                PostRowModel::faceOf(m.post, m.rightOf({{m.flip, 1}, {m.row, 2}})), ErrorCode::NotFound,
                std::format("ImageSketch (object:11): the side from {} of Post (object:6), copy 1 of Flip "
                            "(object:8), copy 2 of Row (object:7) is not a face of its body (the feature's operation "
                            "left no such face)",
                            m.right));
        // Instance 0 is the original, which is named without a copy.
        sketch::Sketch loose("Loose");
        const auto zero = loose.setAttachment(PostRowModel::faceOf(m.post, m.rightOf({{m.row, 0}})));
        REQUIRE_FALSE(zero.has_value());
        CHECK(zero.error().message == "a sketch's attachment: a copy is an instance from 1 (instance 0 is the "
                                      "original)");
        const auto none = loose.setAttachment(PostRowModel::faceOf(m.post, m.rightOf({{ObjectId{}, 1}})));
        REQUIRE_FALSE(none.has_value());
        CHECK(none.error().message == "a sketch's attachment: a copy must name a valid feature");
        // The original's face is found in Post's own body.
        REQUIRE(attach(history, m.doc, m.copySketch, PostRowModel::faceOf(m.post, m.rightOf({}))).has_value());
        regenerate(regenerator, m.doc);
        checkFrame(sketchOf(m.doc, m.copySketch).placement(), {{20, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 0}});
    }
}

TEST_CASE("SketchOnFace_ValidationReportsRolesAndCopiesTheFeatureDoesNotHave",
          "[features][references][validation][p12]") {
    for (const auto& build : std::vector<std::function<Document()>>{
             [] { return RevolvedRingModel{}.doc.clone(); }, [] { return SweptBarModel{}.doc.clone(); },
             [] { return LoftedFrustumModel{}.doc.clone(); }, [] { return DrilledBlockModel{}.doc.clone(); },
             [] { return BevelledBlockModel{}.doc.clone(); }, [] { return PostRowModel{}.doc.clone(); },
             [] { return SpokeHubModel{}.doc.clone(); }}) {
        const Document doc = build();
        const ValidationReport clean = validateDocument(doc);
        INFO(doc.name() << ": " << (clean.issues.empty() ? std::string{} : clean.issues.front().message));
        CHECK(clean.valid());
    }

    PostRowModel m;
    const auto reattach = [&](ObjectId sketch, const PlaneReference& reference) {
        REQUIRE(m.doc.modifyObject<sketch::Sketch>(sketch, [&](sketch::Sketch& s) {
                         return s.setAttachment(reference);
                     }).value());
    };
    reattach(m.copySketch, PostRowModel::faceOf(m.post, m.rightOf({{m.plate, 2}})));
    reattach(m.imageSketch, PostRowModel::faceOf(m.post, m.rightOf({{m.row, 2}, {m.flip, 3}})));
    reattach(m.gaugeSketch, PostRowModel::faceOf(m.post, m.rightOf({{ObjectId::fromValue(99), 1}})));
    const ValidationReport report = validateDocument(m.doc);
    CHECK_FALSE(report.valid());
    std::vector<std::string> messages;
    for (const ValidationIssue& issue : report.issues) {
        messages.push_back(issue.message);
    }
    const auto has = [&](std::string_view text) {
        return std::ranges::any_of(messages,
                                   [&](const std::string& line) { return line.find(text) != std::string::npos; });
    };
    CHECK(has(std::format("CopySketch (object:9): the attachment is the side from {} of Post (object:6), copy 2 of "
                          "Plate (object:4): Plate (object:4) is an extrude, which makes no copies (patterns and "
                          "mirrors do)",
                          m.right)));
    CHECK(has(std::format("ImageSketch (object:11): the attachment is the side from {} of Post (object:6), copy 2 of "
                          "Row (object:7), copy 3 of Flip (object:8): Flip (object:8) is a mirror, whose only copy is "
                          "instance 1, not 3",
                          m.right)));
    // A missing copying feature is a missing reference, reported once.
    CHECK(has("object:99"));
    CHECK(std::ranges::count_if(messages, [](const std::string& line) {
              return line.find("object:99") != std::string::npos;
          }) == 1);
}

// --- Determinism ---------------------------------------------------------------------------------------

TEST_CASE("SketchOnFace_RegeneratesDeterministically", "[features][references][regeneration][p12]") {
    const auto namesOf = [](const Regenerator& r, const Document& doc) {
        std::vector<std::vector<FaceName>> names;
        for (const DocumentObject& object : doc.objects()) {
            if (const geometry::Body* body = r.body(object.id())) {
                const auto faces = geometry::listFaces(*body);
                REQUIRE(faces.has_value());
                for (const geometry::FaceInfo& face : *faces) {
                    names.push_back(face.names);
                }
            }
        }
        return names;
    };
    const auto twice = [&](const std::function<Document()>& build) {
        Document a = build();
        Document b = build();
        Regenerator ra;
        Regenerator rb;
        regenerate(ra, a);
        regenerate(rb, b);
        INFO(a.name());
        CHECK(snapshot(ra, a) == snapshot(rb, b));
        CHECK(namesOf(ra, a) == namesOf(rb, b));
        CHECK(equivalent(a, b));
        // Named faces carry their names in order, and some copies are named.
        const Snapshot before = snapshot(ra, a);
        CHECK(requireReport(ra, a).regenerated.empty());
        auto again = ra.regenerateAll(a);
        REQUIRE(again.has_value());
        REQUIRE(again->succeeded());
        CHECK(snapshot(ra, a) == before);
    };
    twice([] { return RevolvedRingModel{}.doc.clone(); });
    twice([] { return SweptBarModel{}.doc.clone(); });
    twice([] { return LoftedFrustumModel{}.doc.clone(); });
    twice([] { return DrilledBlockModel{}.doc.clone(); });
    twice([] { return BevelledBlockModel{}.doc.clone(); });
    twice([] { return PostRowModel{}.doc.clone(); });
    twice([] { return SpokeHubModel{}.doc.clone(); });
}
