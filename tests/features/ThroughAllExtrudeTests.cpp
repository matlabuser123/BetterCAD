#include "TestHelpers.hpp"
#include "core/geometry/GeometryTestSupport.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/ThroughCutModels.hpp"

#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/features/Datums.hpp>
#include <bettercad/features/FaceReferences.hpp>
#include <bettercad/features/FeatureCommands.hpp>
#include <bettercad/features/Regeneration.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <format>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using bettercad::test::ExpectedBoss;
using bettercad::test::ExpectedFrame;
using bettercad::test::FaceKindModel;
using bettercad::test::HollowSectionModel;
using bettercad::test::PerforatedPlateModel;
using bettercad::test::requireReport;
using bettercad::test::ThroughSlabModel;
using bettercad::test::Vec3;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// P12-FEAT-001: extrudes that cut through all the material of their target.
// Expected volumes, centres, bounds and frames are written out from the
// parameters in support/ThroughCutModels.hpp.

namespace {

constexpr double pi = std::numbers::pi;
// Planes and cylinders are integrated to rounding (P12-DATUM-001).
constexpr double kRel = bettercad::test::kRelTight;
constexpr double kTolCentreMm = bettercad::test::kPositionToleranceMm;
constexpr double kTolMm = 1e-12;    // frame origins (P12-STREF-001)
constexpr double kTolUnit = 1e-14;  // frame axes
constexpr double kTolBounds = 1e-7; // exact bounds (P3)

Vec3 mm(const Point3D& p) {
    return {p.x.in(units::mm), p.y.in(units::mm), p.z.in(units::mm)};
}

void checkVec(const Vec3& actual, const Vec3& expected, double tolerance) {
    CHECK_THAT(actual[0], WithinAbs(expected[0], tolerance));
    CHECK_THAT(actual[1], WithinAbs(expected[1], tolerance));
    CHECK_THAT(actual[2], WithinAbs(expected[2], tolerance));
}

void checkFrame(const Frame3D& frame, const ExpectedFrame& expected) {
    checkVec(mm(frame.origin()), expected.origin, kTolMm);
    checkVec({frame.xAxis().x(), frame.xAxis().y(), frame.xAxis().z()}, expected.x, kTolUnit);
    checkVec({frame.yAxis().x(), frame.yAxis().y(), frame.yAxis().z()}, expected.y, kTolUnit);
    checkVec({frame.normal().x(), frame.normal().y(), frame.normal().z()}, expected.normal, kTolUnit);
}

const sketch::Sketch& sketchOf(const Document& doc, ObjectId id) {
    const auto* sketch = doc.findObjectAs<sketch::Sketch>(id);
    REQUIRE(sketch != nullptr);
    return *sketch;
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

double volumeOf(const Regenerator& regenerator, ObjectId id) {
    const geometry::Body* body = regenerator.body(id);
    REQUIRE(body != nullptr);
    const auto props = body->massProperties();
    REQUIRE(props.has_value());
    return props->volume.in(units::mm3);
}

/// A body against its expected volume, centre and bounds.
void checkBody(const Regenerator& regenerator, ObjectId id, double volume, const Vec3& centre, const Vec3& min,
               const Vec3& max) {
    const geometry::Body* body = regenerator.body(id);
    REQUIRE(body != nullptr);
    CHECK(body->isValid());
    CHECK(body->topology().solids == 1);
    const auto props = body->massProperties();
    REQUIRE(props.has_value());
    CHECK_THAT(props->volume.in(units::mm3), WithinRel(volume, kRel));
    checkVec(mm(props->centerOfMass), centre, kTolCentreMm);
    const auto box = body->boundingBox();
    REQUIRE(box.has_value());
    checkVec(mm(box->min), min, kTolBounds);
    checkVec(mm(box->max), max, kTolBounds);
}

void checkBoss(const Regenerator& regenerator, const Document& doc, const ExpectedBoss& expected) {
    checkFrame(sketchOf(doc, expected.sketch).placement(), expected.frame);
    checkBody(regenerator, expected.boss, expected.cylinder.volume(), expected.cylinder.centre(),
              expected.cylinder.lower(), expected.cylinder.upper());
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
        if (const auto* sketch = dynamic_cast<const sketch::Sketch*>(&object)) {
            result.placements.push_back(sketch->placement());
        }
        if (regenerator.body(object.id()) != nullptr) {
            result.volumes.push_back(bits(volumeOf(regenerator, object.id())));
        }
    }
    return result;
}

void setValue(CommandHistory& history, Document& doc, ParameterId parameter, DimensionedValue value) {
    REQUIRE(history
                .execute(doc, std::make_unique<ModifyParameterCommand>(parameter, ParameterChanges{.value = value}))
                .has_value());
}

void editExtrude(CommandHistory& history, Document& doc, ObjectId id, const ExtrudeDefinition& definition) {
    REQUIRE(history.execute(doc, std::make_unique<ModifyExtrudeCommand>(FaceKindModel::featureOf(id), definition))
                .has_value());
}

ExtrudeDefinition definitionOf(const Document& doc, ObjectId id) {
    return doc.findObjectAs<ExtrudeFeature>(id)->definition();
}

/// Regenerates: @p item fails with @p code and @p message, keeping no body,
/// and each of @p blocked is blocked.
void failsWith(Regenerator& regenerator, Document& doc, ObjectId item, ErrorCode code, const std::string& message,
               const std::vector<ObjectId>& blocked) {
    const RegenerationReport report = requireReport(regenerator, doc);
    CHECK(report.failed == std::vector<ObjectId>{item});
    REQUIRE(report.errors.contains(item));
    CHECK(report.errors.at(item).code == code);
    CHECK(report.errors.at(item).message == message);
    CHECK(regenerator.body(item) == nullptr);
    for (const ObjectId id : blocked) {
        CHECK(std::ranges::find(report.blocked, id) != report.blocked.end());
    }
}

} // namespace

TEST_CASE("ThroughAllExtrude_DefinitionIsValidated", "[features][extrude][p12]") {
    CHECK(toString(ExtrudeTermination::Blind) == "blind");
    CHECK(toString(ExtrudeTermination::ThroughAll) == "through all");
    const ExtrudeDefinition good{.profile = SketchId::fromValue(1),
                                 .operation = FeatureOperation::Cut,
                                 .target = FeatureId::fromValue(2),
                                 .termination = ExtrudeTermination::ThroughAll};
    REQUIRE(validate(good).has_value());
    const auto message = [](const ExtrudeDefinition& definition) {
        const auto created = ExtrudeFeature::create("E", definition);
        REQUIRE_FALSE(created.has_value());
        CHECK(created.error().code == ErrorCode::InvalidArgument);
        return created.error().message;
    };
    ExtrudeDefinition d = good;
    d.depth = 10_mm;
    CHECK(message(d) == "a through-all extrude has no depth: it reaches through its target");
    d = good;
    d.depthParameter = ParameterId::fromValue(3);
    CHECK(message(d) == "a through-all extrude has no depth: it reaches through its target");
    for (const auto& [operation, text] : {std::pair{FeatureOperation::NewBody, "new body"},
                                          std::pair{FeatureOperation::Join, "join"},
                                          std::pair{FeatureOperation::Intersect, "intersect"}}) {
        d = good;
        d.operation = operation;
        d.target = operation == FeatureOperation::NewBody ? std::nullopt : good.target;
        CHECK(message(d) == std::format("a through-all extrude must be a cut, got {}", text));
    }
    d = good;
    d.target.reset();
    CHECK(message(d) == "a cut feature needs a target feature");
    // A blind extrude still needs its depth.
    d = good;
    d.termination = ExtrudeTermination::Blind;
    CHECK(message(d) == "extrude depth must be positive, got 0 mm");
    // The termination is part of the definition's identity.
    CHECK_FALSE(good == d);
    // A through-all extrude depends on its profile and target only.
    const auto feature = ExtrudeFeature::create("E", good);
    REQUIRE(feature.has_value());
    CHECK((*feature)->dependencies() == std::vector<ObjectId>{ObjectId::fromValue(1), ObjectId::fromValue(2)});
}

TEST_CASE("ThroughAllExtrude_CutsThroughAtEveryThickness",
          "[features][extrude][regeneration][p12][acceptance]") {
    ThroughSlabModel m;
    Regenerator regenerator;
    const auto check = [&](double t) {
        CAPTURE(t);
        checkBody(regenerator, m.slant, ThroughSlabModel::volume(t), ThroughSlabModel::centre(t), {0, 0, 0},
                  {100, 60, t});
        CHECK_THAT(volumeOf(regenerator, m.cut), WithinRel(ThroughSlabModel::cutVolume(t), kRel));
        checkFrame(sketchOf(m.doc, m.cutSketch).placement(), {{0, 0, t}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}});
        checkBoss(regenerator, m.doc, m.pegBoss());
    };
    regenerate(regenerator, m.doc);
    check(20.0);
    const Snapshot first = snapshot(regenerator, m.doc);

    CommandHistory history;
    setValue(history, m.doc, m.thickness, DimensionedValue::of(30_mm));
    const RegenerationReport thicker = regenerate(regenerator, m.doc);
    for (const ObjectId id : {m.slab, m.cutSketch, m.cut, m.slant}) {
        CHECK(std::ranges::find(thicker.regenerated, id) != thicker.regenerated.end());
    }
    check(30.0);
    setValue(history, m.doc, m.thickness, DimensionedValue::of(12_mm));
    regenerate(regenerator, m.doc);
    check(12.0);

    // No depth is stored anywhere: the definitions are unchanged.
    CHECK(definitionOf(m.doc, m.cut).depth == Length{});
    CHECK_FALSE(definitionOf(m.doc, m.cut).depthParameter.has_value());

    REQUIRE(history.undo(m.doc).has_value());
    REQUIRE(history.undo(m.doc).has_value());
    regenerate(regenerator, m.doc);
    check(20.0);
    CHECK(snapshot(regenerator, m.doc) == first);
}

TEST_CASE("ThroughAllExtrude_GoesTheWayItsDirectionSays", "[features][extrude][regeneration][p12]") {
    SECTION("from the top face: reversed and symmetric cut through; along the normal there is nothing") {
        ThroughSlabModel m;
        Regenerator regenerator;
        regenerate(regenerator, m.doc);
        const double reversed = volumeOf(regenerator, m.slant);
        CommandHistory history;
        ExtrudeDefinition d = definitionOf(m.doc, m.cut);
        d.direction = ExtrudeDirection::Symmetric;
        editExtrude(history, m.doc, m.cut, d);
        regenerate(regenerator, m.doc);
        CHECK_THAT(volumeOf(regenerator, m.slant), WithinRel(reversed, kRel));
        checkBody(regenerator, m.slant, ThroughSlabModel::volume(20.0), ThroughSlabModel::centre(20.0), {0, 0, 0},
                  {100, 60, 20});

        d.direction = ExtrudeDirection::Normal;
        editExtrude(history, m.doc, m.cut, d);
        failsWith(regenerator, m.doc, m.cut, ErrorCode::FailedPrecondition,
                  "Cut: the target lies wholly behind the sketch plane, so cutting through all along its normal "
                  "removes nothing",
                  {m.slant, m.wallSketch, m.peg});
        // Undo recovers the cut.
        REQUIRE(history.undo(m.doc).has_value());
        REQUIRE(history.undo(m.doc).has_value());
        regenerate(regenerator, m.doc);
        CHECK(bits(volumeOf(regenerator, m.slant)) == bits(reversed));
    }
    SECTION("from below the slab: along the normal cuts through; against it there is nothing") {
        FaceKindModel base("0f3a9b51-7c2d-4e8b-a6f4-3d1c5e9b7a28", "Below");
        const ObjectId sketch = base.addFixedRectangle("SlabSketch", 0.0, 0.0, 100.0, 60.0);
        const ObjectId slab = base.add(ExtrudeFeature::create(
            "Slab", {.profile = FaceKindModel::sketchOf(sketch), .depth = 20_mm}));
        auto below = std::make_unique<sketch::Sketch>(
            "Below", Frame3D::create(Point3D{0_mm, 0_mm, -(5_mm)}, Direction3D::unitZ(), Direction3D::unitX())
                         .value());
        const auto lines = bettercad::test::addRectangle(*below, 10_mm, 10_mm, 20_mm, 10_mm);
        for (const EntityId line : lines) {
            bettercad::test::require(
                below->addFixed(std::get<sketch::LineEntity>(below->findEntity(line)->geometry).start));
        }
        const ObjectId belowSketch = base.doc.addObject(std::move(below)).value();
        const ObjectId cut = base.add(ExtrudeFeature::create(
            "Up", bettercad::test::throughAll(belowSketch, slab, ExtrudeDirection::Normal)));
        Regenerator regenerator;
        regenerate(regenerator, base.doc);
        checkBody(regenerator, cut, 120000.0 - 200.0 * 20.0,
                  bettercad::test::weightedCentre({{120000.0, {50, 30, 10}}, {-4000.0, {20, 15, 10}}}), {0, 0, 0},
                  {100, 60, 20});
        CommandHistory history;
        ExtrudeDefinition d = definitionOf(base.doc, cut);
        d.direction = ExtrudeDirection::Reversed;
        editExtrude(history, base.doc, cut, d);
        failsWith(regenerator, base.doc, cut, ErrorCode::FailedPrecondition,
                  "Up: the target lies wholly in front of the sketch plane, so cutting through all against its "
                  "normal removes nothing",
                  {});
    }
}

TEST_CASE("ThroughAllExtrude_RepeatsThroughTheBodyAtEveryInstance",
          "[features][extrude][pattern][mirror][regeneration][p12][acceptance]") {
    SECTION("a row of holes through a plate of any thickness") {
        PerforatedPlateModel m;
        Regenerator regenerator;
        const auto check = [&](double t, double count) {
            CAPTURE(t, count);
            checkBody(regenerator, m.row, PerforatedPlateModel::volume(t, count),
                      PerforatedPlateModel::centre(t, count), {0, 0, 0}, {100, 60, t});
        };
        regenerate(regenerator, m.doc);
        check(10.0, 5.0);
        CommandHistory history;
        setValue(history, m.doc, m.thickness, DimensionedValue::of(25_mm));
        regenerate(regenerator, m.doc);
        check(25.0, 5.0);
        setValue(history, m.doc, m.holes, DimensionedValue{.dimension = dimensions::dimensionless, .siValue = 3.0});
        regenerate(regenerator, m.doc);
        check(25.0, 3.0);
    }

    // The side walls are thicker than the top and bottom: a copy turned to
    // face +X needs a tool twice as long as the original's.
    const auto sideArea = [](const Regenerator& regenerator, ObjectId holder, const FaceName& name) {
        const auto faces = geometry::findNamedFaces(*regenerator.body(holder), name);
        REQUIRE(faces.has_value());
        REQUIRE(faces->size() == 1);
        CHECK(faces->front().surface == geometry::FaceSurface::Cylinder);
        return faces->front().area.in(units::mm2);
    };
    SECTION("a circular pattern turns the hole onto each wall") {
        HollowSectionModel m;
        const ObjectId ring = m.add(CircularPatternFeature::create(
            "Ring", {.source = FaceKindModel::featureOf(m.port),
                     .axis = {.origin = Point3D{}, .direction = {0.0, 1.0, 0.0}},
                     .count = 4}));
        Regenerator regenerator;
        regenerate(regenerator, m.doc);
        const double holes = 2.0 * HollowSectionModel::thinHole() + 2.0 * HollowSectionModel::thickHole();
        checkBody(regenerator, ring, HollowSectionModel::kSectionVolume - holes, {0, 30, 0}, {-30, 0, -15},
                  {30, 60, 15});
        // Each copy's wall is its own: 2 pi r x the wall's thickness.
        const auto wall = [&](std::vector<FaceCopy> copies) {
            return sideArea(regenerator, ring,
                            FaceName{m.port, {.role = FaceRole::Side, .entity = m.portCircle, .copies = copies}});
        };
        CHECK_THAT(wall({}), WithinRel(2.0 * pi * 3.0 * 5.0, kRel));            // +Z
        CHECK_THAT(wall({{ring, 1}}), WithinRel(2.0 * pi * 3.0 * 10.0, kRel));  // +X
        CHECK_THAT(wall({{ring, 2}}), WithinRel(2.0 * pi * 3.0 * 5.0, kRel));   // -Z
        CHECK_THAT(wall({{ring, 3}}), WithinRel(2.0 * pi * 3.0 * 10.0, kRel));  // -X
    }
    SECTION("a feature mirror in the plane x = z turns the hole onto the +X wall") {
        HollowSectionModel m;
        const ObjectId swap = m.add(MirrorFeature::create(
            "Swap", {.source = FaceKindModel::featureOf(m.port),
                     .plane = {.origin = Point3D{}, .normal = {1.0, 0.0, -1.0}}}));
        Regenerator regenerator;
        regenerate(regenerator, m.doc);
        checkBody(regenerator, swap,
                  HollowSectionModel::kSectionVolume - HollowSectionModel::thinHole() -
                      HollowSectionModel::thickHole(),
                  bettercad::test::weightedCentre({{HollowSectionModel::kSectionVolume, {0, 30, 0}},
                                                   {-HollowSectionModel::thinHole(), {0, 30, 12.5}},
                                                   {-HollowSectionModel::thickHole(), {25, 30, 0}}}),
                  {-30, 0, -15}, {30, 60, 15});
        CHECK_THAT(sideArea(regenerator, swap,
                            FaceName{m.port, {.role = FaceRole::Side, .entity = m.portCircle, .copies = {{swap, 1}}}}),
                   WithinRel(2.0 * pi * 3.0 * 10.0, kRel));
    }
}

TEST_CASE("ThroughAllExtrude_NamesItsSidesAndLosesItsCaps", "[features][extrude][references][p12]") {
    ThroughSlabModel m;
    Regenerator regenerator;
    regenerate(regenerator, m.doc);
    const BodyLookup bodies = [&](ObjectId id) { return regenerator.body(id); };
    const auto plane = [&](FaceSelector face) {
        return resolveFacePlane(m.doc, FaceName{m.cut, std::move(face)}, bodies);
    };
    const auto side = [&](EntityId entity) {
        auto frame = plane({.role = FaceRole::Side, .entity = entity});
        INFO((frame ? std::string{} : frame.error().message));
        REQUIRE(frame.has_value());
        return *frame;
    };
    // The slot's walls face into the slot.
    checkFrame(side(m.slotLeft), {{60, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 0}});
    checkFrame(side(m.slotRight), {{90, 0, 0}, {0, 1, 0}, {0, 0, -1}, {-1, 0, 0}});
    checkFrame(side(m.slotBottom), {{0, 20, 0}, {1, 0, 0}, {0, 0, -1}, {0, 1, 0}});
    checkFrame(side(m.slotTop), {{0, 40, 0}, {1, 0, 0}, {0, 0, 1}, {0, -1, 0}});
    // The hole's wall is a cylinder.
    const auto wall = plane({.role = FaceRole::Side, .entity = m.hole});
    REQUIRE_FALSE(wall.has_value());
    CHECK(wall.error().message == std::format("the side from {} of Cut (object:5) is a cylinder, not a plane", m.hole));
    // Both caps lie outside the slab or on its top: the cut leaves neither.
    for (const auto& [role, text] : {std::pair{FaceRole::StartCap, "start cap"}, std::pair{FaceRole::EndCap, "end cap"}}) {
        const auto cap = plane({.role = role});
        REQUIRE_FALSE(cap.has_value());
        CHECK(cap.error().code == ErrorCode::NotFound);
        CHECK(cap.error().message == std::format("the {} of Cut (object:5) is not a face of its body (the feature's "
                                                 "operation left no such face)",
                                                 text));
    }
}

TEST_CASE("ThroughAllExtrude_EditsAreUndoableAndAtomic", "[features][extrude][commands][p12]") {
    PerforatedPlateModel m;
    Regenerator regenerator;
    regenerate(regenerator, m.doc);
    const double through = volumeOf(regenerator, m.row);
    CommandHistory history;
    // A blind 4 mm pocket instead: 5 pockets of 16 pi x 4.
    ExtrudeDefinition blind = definitionOf(m.doc, m.perf);
    blind.termination = ExtrudeTermination::Blind;
    blind.depth = 4_mm;
    editExtrude(history, m.doc, m.perf, blind);
    regenerate(regenerator, m.doc);
    CHECK_THAT(volumeOf(regenerator, m.row), WithinRel(60000.0 - 5.0 * 16.0 * pi * 4.0, kRel));
    REQUIRE(history.undo(m.doc).has_value());
    regenerate(regenerator, m.doc);
    CHECK(bits(volumeOf(regenerator, m.row)) == bits(through));
    REQUIRE(history.redo(m.doc).has_value());
    regenerate(regenerator, m.doc);
    CHECK_THAT(volumeOf(regenerator, m.row), WithinRel(60000.0 - 5.0 * 16.0 * pi * 4.0, kRel));
    REQUIRE(history.undo(m.doc).has_value());

    // An invalid edit is refused and changes nothing.
    ExtrudeDefinition joined = definitionOf(m.doc, m.perf);
    joined.operation = FeatureOperation::Join;
    const Document before = m.doc.clone();
    const auto refused = history.execute(
        m.doc, std::make_unique<ModifyExtrudeCommand>(FaceKindModel::featureOf(m.perf), joined));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().message == "a through-all extrude must be a cut, got join");
    CHECK(equivalent(before, m.doc));

    // Without its target's body the tool cannot be made.
    const auto tool = extrudeTool(*m.doc.findObjectAs<ExtrudeFeature>(m.perf), m.doc);
    REQUIRE_FALSE(tool.has_value());
    CHECK(tool.error().code == ErrorCode::FailedPrecondition);
    CHECK(tool.error().message == "Perf: a through-all extrude needs the body of its target feature");
    regenerate(regenerator, m.doc);
    CHECK(bits(volumeOf(regenerator, m.row)) == bits(through));
}

TEST_CASE("ThroughAllExtrude_RegeneratesDeterministically", "[features][extrude][regeneration][p12]") {
    ThroughSlabModel a;
    ThroughSlabModel b;
    Regenerator ra;
    Regenerator rb;
    regenerate(ra, a.doc);
    regenerate(rb, b.doc);
    CHECK(snapshot(ra, a.doc) == snapshot(rb, b.doc));
    CHECK(equivalent(a.doc, b.doc));
    const auto namesOf = [](const Regenerator& r, ObjectId id) {
        const auto faces = geometry::listFaces(*r.body(id));
        REQUIRE(faces.has_value());
        std::vector<std::vector<FaceName>> names;
        for (const geometry::FaceInfo& face : *faces) {
            names.push_back(face.names);
        }
        return names;
    };
    CHECK(namesOf(ra, a.slant) == namesOf(rb, b.slant));
    const Snapshot before = snapshot(ra, a.doc);
    CHECK(requireReport(ra, a.doc).regenerated.empty());
    auto again = ra.regenerateAll(a.doc);
    REQUIRE(again.has_value());
    REQUIRE(again->succeeded());
    CHECK(snapshot(ra, a.doc) == before);
}
