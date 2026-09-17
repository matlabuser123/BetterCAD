#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/FaceModels.hpp"

#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/features/Datums.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/FaceReferences.hpp>
#include <bettercad/features/LinearPatternFeature.hpp>
#include <bettercad/features/MirrorFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/Validation.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/sketch/SketchCommands.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using bettercad::test::FaceBlockModel;
using bettercad::test::errorCode;
using bettercad::test::require;
using bettercad::test::requireReport;
using bettercad::test::volumeMm3;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// P12-STREF-001: stable feature face references. Expected planes, volumes,
// centres and bounds are written out from the parameters (see
// support/FaceModels.hpp), never read from the references.

namespace {

constexpr double kTolMm = 1e-12;   // positions of faces of axis-aligned boxes
constexpr double kTolUnit = 1e-14; // frame axes
constexpr double kTolBounds = 1e-7; // kernel bounding boxes (P3)

using Vec = std::array<double, 3>;

Vec mm(const Point3D& p) {
    return {p.x.in(units::mm), p.y.in(units::mm), p.z.in(units::mm)};
}

void checkVec(const Vec& actual, const Vec& expected, double tolerance) {
    CHECK_THAT(actual[0], WithinAbs(expected[0], tolerance));
    CHECK_THAT(actual[1], WithinAbs(expected[1], tolerance));
    CHECK_THAT(actual[2], WithinAbs(expected[2], tolerance));
}

void checkFrame(const Frame3D& frame, const Vec& origin, const Vec& x, const Vec& y, const Vec& normal) {
    checkVec(mm(frame.origin()), origin, kTolMm);
    checkVec({frame.xAxis().x(), frame.xAxis().y(), frame.xAxis().z()}, x, kTolUnit);
    checkVec({frame.yAxis().x(), frame.yAxis().y(), frame.yAxis().z()}, y, kTolUnit);
    checkVec({frame.normal().x(), frame.normal().y(), frame.normal().z()}, normal, kTolUnit);
}

const sketch::Sketch& sketchOf(const Document& doc, ObjectId id) {
    const auto* sketch = doc.findObjectAs<sketch::Sketch>(id);
    REQUIRE(sketch != nullptr);
    return *sketch;
}

/// Checks the model's result against its analytic volume, centre and bounds.
void checkModel(const Regenerator& regenerator, const FaceBlockModel& m, double heightMm) {
    CAPTURE(heightMm);
    const geometry::Body* body = regenerator.body(m.pocket);
    REQUIRE(body != nullptr);
    CHECK(body->isValid());
    const auto props = body->massProperties();
    REQUIRE(props.has_value());
    CHECK_THAT(props->volume.in(units::mm3), WithinRel(FaceBlockModel::volume(heightMm), 1e-12));
    checkVec(mm(props->centerOfMass), FaceBlockModel::centre(heightMm), 1e-9);
    const auto box = body->boundingBox();
    REQUIRE(box.has_value());
    checkVec(mm(box->min), {0, 0, 0}, kTolBounds);
    checkVec(mm(box->max), {150, 60, FaceBlockModel::top(heightMm)}, kTolBounds);
    // The attached sketches' planes.
    checkFrame(sketchOf(m.doc, m.bossSketch).placement(), {0, 0, heightMm}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1});
    checkFrame(sketchOf(m.doc, m.sideSketch).placement(), {0, 0, 0}, {1, 0, 0}, {0, 0, 1}, {0, -1, 0});
}

void setPlate(CommandHistory& history, Document& doc, ParameterId plate, Length value) {
    REQUIRE(history
                .execute(doc, std::make_unique<ModifyParameterCommand>(
                                  plate, ParameterChanges{.value = DimensionedValue::of(value)}))
                .has_value());
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

ObjectId addSketch(Document& doc, const std::string& name, const PlaneReference& reference) {
    auto sketch = FaceBlockModel::rectangleSketch(name, 1.0, 1.0, 2.0, 2.0);
    REQUIRE(sketch->setAttachment(reference).has_value());
    return doc.addObject(std::move(sketch)).value();
}

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

} // namespace

// --- References on their own ----------------------------------------------------------------------

TEST_CASE("FaceReference_ReferencesAreCheckedOnTheirOwn", "[features][references][p12]") {
    const auto message = [](const Result<void>& result) {
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::InvalidArgument);
        return result.error().message;
    };
    CHECK(toString(FaceRole::StartCap) == "start_cap");
    CHECK(toString(FaceRole::EndCap) == "end_cap");
    CHECK(toString(FaceRole::Side) == "side");
    const ObjectId feature = ObjectId::fromValue(4);
    CHECK(validate(FaceSelector{FaceRole::EndCap, std::nullopt}).has_value());
    CHECK(message(validate(FaceSelector{FaceRole::Side, std::nullopt})) ==
          "a side face is named by a valid profile entity");
    CHECK(message(validate(FaceSelector{FaceRole::Side, EntityId{}})) ==
          "a side face is named by a valid profile entity");
    CHECK(message(validate(FaceSelector{FaceRole::EndCap, EntityId::fromValue(3)})) ==
          "an end cap is not named by an entity");
    CHECK(message(validate(FaceSelector{FaceRole::StartCap, EntityId::fromValue(3)})) ==
          "a start cap is not named by an entity");
    CHECK(message(validate(PlaneReference{.face = FaceSelector{}})) ==
          "a face reference must name the feature that generates the face");
    CHECK(message(validate(PlaneReference{.object = feature, .plane = PrincipalPlane::YZ, .face = FaceSelector{}})) ==
          "a face reference has no principal plane of its own, got yz");
    CHECK(message(validate(PlaneReference{.object = ObjectId{}})) == "a plane reference must name a valid object");
    CHECK(validate(PlaneReference{.object = feature, .face = FaceSelector{}}).has_value());

    // Owners refuse invalid references when they are set.
    sketch::Sketch sketch("S");
    const auto refused = sketch.setAttachment(PlaneReference{.object = feature, .face = FaceSelector{FaceRole::Side}});
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().message == "a sketch's attachment: a side face is named by a valid profile entity");
    const auto datum = DatumPlane::create(
        "D", {.kind = DatumPlaneKind::Offset,
              .base = {.object = feature, .face = FaceSelector{FaceRole::EndCap, EntityId::fromValue(2)}}});
    REQUIRE_FALSE(datum.has_value());
    CHECK(datum.error().message == "the base plane: an end cap is not named by an entity");
    // Mirrors resolve their plane without the regenerated bodies.
    const auto mirror = MirrorFeature::create(
        "M", {.source = FeatureId::fromValue(4),
              .plane = {.reference = PlaneReference{.object = feature, .face = FaceSelector{}}},
              .scope = MirrorScope::Body});
    REQUIRE_FALSE(mirror.has_value());
    CHECK(mirror.error().message == "a mirror plane refers to a datum plane or a coordinate system, not to a face");
}

TEST_CASE("FaceReference_ExtrudesNameTheirCapsAndSides", "[features][references][p12]") {
    const auto build = [](ExtrudeDirection direction, bool hole) {
        Document doc{"Caps"};
        auto profile = FaceBlockModel::rectangleSketch("Profile", 0.0, 0.0, 40.0, 30.0);
        EntityId circle{};
        if (hole) {
            circle = require(profile->addCircle(Point2D{20_mm, 15_mm}, 5_mm));
        }
        const EntityId front = FaceBlockModel::lineAlong(*profile, 0.0);
        const ObjectId sketch = doc.addObject(std::move(profile)).value();
        const ObjectId pad = doc.addObject(*ExtrudeFeature::create(
                                               "Pad", {.profile = SketchId::fromValue(sketch.value()),
                                                       .depth = 12_mm,
                                                       .direction = direction}))
                                 .value();
        Regenerator regenerator;
        REQUIRE(requireReport(regenerator, doc).succeeded());
        const BodyLookup bodies = [&](ObjectId id) { return regenerator.body(id); };
        const auto plane = [&](FaceSelector face) {
            auto frame = resolveFacePlane(doc, FaceName{pad, face}, bodies);
            INFO((frame ? std::string{} : frame.error().message));
            REQUIRE(frame.has_value());
            return *frame;
        };
        struct Planes {
            Frame3D start;
            Frame3D end;
            Frame3D side;
            std::size_t holeFaces = 0;
        } result{plane({FaceRole::StartCap, std::nullopt}), plane({FaceRole::EndCap, std::nullopt}),
                 plane({FaceRole::Side, front})};
        if (hole) {
            const auto wall = geometry::findNamedFaces(*regenerator.body(pad),
                                                       FaceName{pad, {FaceRole::Side, circle}});
            REQUIRE(wall.has_value());
            result.holeFaces = wall->size();
            REQUIRE(wall->size() == 1);
            CHECK(wall->front().surface == geometry::FaceSurface::Cylinder);
            const auto refused = resolveFacePlane(doc, FaceName{pad, {FaceRole::Side, circle}}, bodies);
            REQUIRE_FALSE(refused.has_value());
            CHECK(refused.error().code == ErrorCode::InvalidArgument);
            CHECK(refused.error().message ==
                  std::format("the side from {} of Pad ({}) is a cylinder, not a plane", circle, pad));
        }
        return result;
    };
    SECTION("along the normal: the start on the sketch plane, the end at the depth") {
        const auto r = build(ExtrudeDirection::Normal, true);
        checkFrame(r.start, {0, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 0, -1});
        checkFrame(r.end, {0, 0, 12}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1});
        checkFrame(r.side, {0, 0, 0}, {1, 0, 0}, {0, 0, 1}, {0, -1, 0});
        CHECK(r.holeFaces == 1);
    }
    SECTION("reversed: the start on the sketch plane, the end below it") {
        const auto r = build(ExtrudeDirection::Reversed, false);
        checkFrame(r.start, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1});
        checkFrame(r.end, {0, 0, -12}, {1, 0, 0}, {0, -1, 0}, {0, 0, -1});
    }
    SECTION("symmetric: the start behind the sketch plane, the end in front of it") {
        const auto r = build(ExtrudeDirection::Symmetric, false);
        checkFrame(r.start, {0, 0, -6}, {1, 0, 0}, {0, -1, 0}, {0, 0, -1});
        checkFrame(r.end, {0, 0, 6}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1});
    }
}

// --- Following the face ---------------------------------------------------------------------------

TEST_CASE("FaceReference_SketchOnAnEndCapFollowsTheDepth", "[features][references][regeneration][p12][acceptance]") {
    FaceBlockModel m;
    Regenerator regenerator;
    const RegenerationReport first = requireReport(regenerator, m.doc);
    INFO((first.errors.empty() ? std::string{} : first.errors.begin()->second.message));
    REQUIRE(first.succeeded());
    // height is driven by the expression 2 * plate.
    CHECK(m.doc.parameters().find(m.height)->expression() == "2 * plate");
    checkModel(regenerator, m, 20.0);
    const double volume20 = volumeMm3(regenerator, m.pocket);
    const Frame3D placement20 = sketchOf(m.doc, m.bossSketch).placement();

    CommandHistory history;
    setPlate(history, m.doc, m.plate, 15_mm);
    const RegenerationReport second = requireReport(regenerator, m.doc);
    REQUIRE(second.succeeded());
    CHECK(second.updatedParameters == std::vector<ParameterId>{m.height});
    for (const ObjectId id : {m.base, m.bossSketch, m.boss, m.pocket}) {
        CHECK(std::ranges::find(second.regenerated, id) != second.regenerated.end());
    }
    checkModel(regenerator, m, 30.0);

    setPlate(history, m.doc, m.plate, 25_mm);
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    checkModel(regenerator, m, 50.0);

    // Undo restores the first state bit for bit.
    REQUIRE(history.undo(m.doc).has_value());
    REQUIRE(history.undo(m.doc).has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    checkModel(regenerator, m, 20.0);
    CHECK(bits(volumeMm3(regenerator, m.pocket)) == bits(volume20));
    CHECK(sketchOf(m.doc, m.bossSketch).placement() == placement20);
}

TEST_CASE("FaceReference_NeverTakesAnotherFeaturesFace", "[features][references][regeneration][p12][acceptance]") {
    FaceBlockModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CommandHistory history;
    const BodyLookup bodies = [&](ObjectId id) { return regenerator.body(id); };
    const FaceName baseTop{m.base, {FaceRole::EndCap, std::nullopt}};

    SECTION("the step's top is nearer the old plane; the boss follows Base's top") {
        // Base's body holds Step's top at z = 35, facing up, like Base's own.
        const auto stepTop = geometry::findNamedFaces(*regenerator.body(m.base),
                                                      FaceName{m.step, {FaceRole::EndCap, std::nullopt}});
        REQUIRE(stepTop.has_value());
        REQUIRE(stepTop->size() == 1);
        CHECK_THAT(stepTop->front().signature->point.z.in(units::mm), WithinAbs(35.0, kTolMm));

        setPlate(history, m.doc, m.plate, 20_mm); // height 40
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        // Nothing lies on the old plane z = 20 any more; a face-by-plane
        // reference fails, and the nearest parallel face is Step's.
        const auto old = geometry::findFaces(*regenerator.body(m.base),
                                             geometry::planeSignature(Point3D{0_mm, 0_mm, 20_mm}, Direction3D::unitZ()));
        REQUIRE(old.has_value());
        CHECK(old->empty());
        checkModel(regenerator, m, 40.0);
        const auto top = geometry::findNamedFaces(*regenerator.body(m.base), baseTop);
        REQUIRE(top.has_value());
        REQUIRE(top->size() == 1);
        CHECK_THAT(top->front().signature->point.z.in(units::mm), WithinAbs(40.0, kTolMm));
        CHECK_THAT(top->front().area.in(units::mm2), WithinRel(6000.0, 1e-12));
    }
    SECTION("at the step's height the tops merge into one face with both names") {
        setPlate(history, m.doc, m.plate, 17.5_mm); // height 35
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        const auto top = geometry::findNamedFaces(*regenerator.body(m.base), baseTop);
        REQUIRE(top.has_value());
        REQUIRE(top->size() == 1);
        CHECK_THAT(top->front().area.in(units::mm2), WithinRel(9000.0, 1e-12));
        CHECK(top->front().names ==
              std::vector<FaceName>{FaceName{m.step, {FaceRole::EndCap, std::nullopt}}, baseTop});
        checkModel(regenerator, m, 35.0);
    }
    SECTION("a face the feature's own operation removed is not found, though another lies on its plane") {
        // The pocket's start cap lay on the front face and was cut away.
        const ObjectId late = addSketch(m.doc, "Late", FaceBlockModel::faceOf(m.pocket, FaceRole::StartCap));
        const RegenerationReport report = requireReport(regenerator, m.doc);
        CHECK(report.failed == std::vector<ObjectId>{late});
        const Error& error = report.errors.at(late);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK(error.message == "Late (object:11): the start cap of Pocket (object:10) is not a face of its body (the "
                               "feature's operation left no such face)");
        const auto front = geometry::findFaces(*regenerator.body(m.pocket),
                                               geometry::planeSignature(Point3D{}, Direction3D::unitY().reversed()));
        REQUIRE(front.has_value());
        CHECK_FALSE(front->empty());
        CHECK(sketchOf(m.doc, late).placement() == Frame3D::xy());
        // The pocket's floor is its end cap, 5 mm in, facing out of the material.
        const auto floor = resolveFacePlane(m.doc, FaceName{m.pocket, {FaceRole::EndCap, std::nullopt}}, bodies);
        REQUIRE(floor.has_value());
        checkFrame(*floor, {0, 5, 0}, {1, 0, 0}, {0, 0, 1}, {0, -1, 0});
    }
    SECTION("a face split by the feature's own join is still one plane") {
        Document doc{"Split"};
        const ObjectId wallSketch = doc.addObject(FaceBlockModel::rectangleSketch("WallSketch", 40, -10, 20, 80)).value();
        const ObjectId wall = doc.addObject(*ExtrudeFeature::create(
                                                "Wall", {.profile = SketchId::fromValue(wallSketch.value()),
                                                         .depth = 50_mm}))
                                  .value();
        const ObjectId padSketch = doc.addObject(FaceBlockModel::rectangleSketch("PadSketch", 0, 0, 100, 60)).value();
        const ObjectId pad = doc.addObject(*ExtrudeFeature::create(
                                               "Pad", {.profile = SketchId::fromValue(padSketch.value()),
                                                       .depth = 20_mm,
                                                       .operation = FeatureOperation::Join,
                                                       .target = FeatureId::fromValue(wall.value())}))
                                 .value();
        const ObjectId onTop = addSketch(doc, "OnTop", FaceBlockModel::faceOf(pad, FaceRole::EndCap));
        Regenerator local;
        REQUIRE(requireReport(local, doc).succeeded());
        const auto parts = geometry::findNamedFaces(*local.body(pad), FaceName{pad, {FaceRole::EndCap, std::nullopt}});
        REQUIRE(parts.has_value());
        CHECK(parts->size() == 2);
        checkFrame(sketchOf(doc, onTop).placement(), {0, 0, 20}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1});
    }
}

TEST_CASE("FaceReference_SketchOnASideFollowsItsEntity", "[features][references][regeneration][p12]") {
    // A rectangle whose right line (x = width) moves with the parameter.
    Document doc{"Sides"};
    const ParameterId width = doc.createParameter("width", 100_mm, units::mm).value();
    auto profile = std::make_unique<sketch::Sketch>("Profile");
    const EntityId p1 = require(profile->addPoint(Point2D{0_mm, 0_mm}));
    const EntityId p2 = require(profile->addPoint(Point2D{90_mm, 0_mm}));
    const EntityId p3 = require(profile->addPoint(Point2D{90_mm, 60_mm}));
    const EntityId p4 = require(profile->addPoint(Point2D{0_mm, 60_mm}));
    const EntityId bottom = require(profile->addLine(p1, p2));
    const EntityId right = require(profile->addLine(p2, p3));
    const EntityId top = require(profile->addLine(p3, p4));
    require(profile->addLine(p4, p1));
    require(profile->addFixed(p1));
    require(profile->addFixed(p4));
    require(profile->addHorizontal(bottom));
    require(profile->addVertical(right));
    require(profile->addHorizontal(top));
    REQUIRE(profile->setConstraintParameter(require(profile->addDistance(bottom, 100_mm)), width).has_value());
    const ObjectId sketch = doc.addObject(std::move(profile)).value();
    const ObjectId block = doc.addObject(*ExtrudeFeature::create(
                                             "Block", {.profile = SketchId::fromValue(sketch.value()), .depth = 20_mm}))
                               .value();
    auto padProfile = FaceBlockModel::rectangleSketch("PadSketch", 10.0, 5.0, 20.0, 10.0);
    REQUIRE(padProfile->setAttachment(FaceBlockModel::faceOf(block, FaceRole::Side, right)).has_value());
    const ObjectId padSketch = doc.addObject(std::move(padProfile)).value();
    const ObjectId pad = doc.addObject(*ExtrudeFeature::create("Pad", {.profile = SketchId::fromValue(padSketch.value()),
                                                                       .depth = 5_mm,
                                                                       .operation = FeatureOperation::Join,
                                                                       .target = FeatureId::fromValue(block.value())}))
                             .value();
    Regenerator regenerator;
    for (const double w : {100.0, 130.0, 70.0}) {
        CAPTURE(w);
        REQUIRE(doc.setParameterValue(width, w * units::mm).has_value());
        const RegenerationReport report = requireReport(regenerator, doc);
        INFO((report.errors.empty() ? std::string{} : report.errors.begin()->second.message));
        REQUIRE(report.succeeded());
        // The right face faces +X: x along the model's Y, y along its Z.
        checkFrame(sketchOf(doc, padSketch).placement(), {w, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 0});
        // The pad: 20 (along Y) x 10 (along Z) x 5 (along X) at y 10..30, z 5..15.
        CHECK_THAT(volumeMm3(regenerator, pad), WithinRel(w * 60.0 * 20.0 + 1000.0, 1e-12));
        const auto box = regenerator.body(pad)->boundingBox();
        REQUIRE(box.has_value());
        checkVec(mm(box->max), {w + 5.0, 60.0, 20.0}, kTolBounds);
    }
}

TEST_CASE("FaceReference_DatumsMayBePlacedFromFaces", "[features][references][datum][p12]") {
    FaceBlockModel m;
    const ObjectId above = m.doc.addObject(*DatumPlane::create(
                                               "Above", {.kind = DatumPlaneKind::Offset,
                                                         .base = FaceBlockModel::faceOf(m.base, FaceRole::EndCap),
                                                         .offset = 5_mm}))
                               .value();
    const ObjectId edge = m.doc.addObject(*DatumAxis::create(
                                              "Edge", {.kind = DatumAxisKind::Intersection,
                                                       .first = FaceBlockModel::faceOf(m.base, FaceRole::Side,
                                                                                       m.frontLine),
                                                       .second = FaceBlockModel::faceOf(m.base, FaceRole::EndCap)}))
                              .value();
    Regenerator regenerator;
    const BodyLookup bodies = [&](ObjectId id) { return regenerator.body(id); };
    CommandHistory history;
    for (const double plate : {10.0, 12.5}) {
        const double h = 2.0 * plate;
        CAPTURE(h);
        if (plate != 10.0) {
            setPlate(history, m.doc, m.plate, plate * units::mm);
        }
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        const auto plane = resolvePlane(m.doc, PlaneReference{above}, bodies);
        REQUIRE(plane.has_value());
        checkFrame(*plane, {0, 0, h + 5.0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1});
        // Where the front face (normal -Y) meets the top (normal +Z): along
        // (-Y) x Z = -X, through (0, 0, h).
        const auto line = resolveAxis(m.doc, AxisReference{edge}, bodies);
        REQUIRE(line.has_value());
        checkVec(mm(line->origin), {0, 0, h}, kTolMm);
        checkVec({line->direction.x(), line->direction.y(), line->direction.z()}, {-1, 0, 0}, kTolUnit);
    }
    // The datums depend on the feature.
    const DocumentGraph graph = buildDependencyGraph(m.doc);
    const auto deps = graph.graph.dependenciesOf(above);
    CHECK(std::ranges::find(deps, m.base) != deps.end());
}

// --- Failures ----------------------------------------------------------------------------------------------

TEST_CASE("FaceReference_FailuresAreStructuredAndBlockDependents", "[features][references][regeneration][p12]") {
    FaceBlockModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CommandHistory history;
    const Frame3D placement = sketchOf(m.doc, m.bossSketch).placement();
    const auto failsWith = [&](ObjectId item, ErrorCode code, const std::string& message) {
        const RegenerationReport report = requireReport(regenerator, m.doc);
        CHECK(std::ranges::find(report.failed, item) != report.failed.end());
        REQUIRE(report.errors.contains(item));
        CHECK(report.errors.at(item).code == code);
        CHECK(report.errors.at(item).message == message);
        return report;
    };
    const auto recovers = [&] {
        REQUIRE(history.undo(m.doc).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        checkModel(regenerator, m, 20.0);
    };

    SECTION("the producing feature is deleted") {
        REQUIRE(history.execute(m.doc, std::make_unique<DeleteObjectCommand>(m.base)).has_value());
        const RegenerationReport report = failsWith(
            m.bossSketch, ErrorCode::NotFound, "object:7 references object:6, which does not exist");
        CHECK(std::ranges::find(report.blocked, m.boss) != report.blocked.end());
        CHECK(sketchOf(m.doc, m.bossSketch).placement() == placement);
        const auto direct = resolvePlane(m.doc, FaceBlockModel::faceOf(m.base, FaceRole::EndCap),
                                         [&](ObjectId id) { return regenerator.body(id); });
        REQUIRE_FALSE(direct.has_value());
        CHECK(direct.error().message == "object:6 does not exist");
        recovers();
    }
    SECTION("a sketch is not a feature") {
        REQUIRE(attach(history, m.doc, m.bossSketch, FaceBlockModel::faceOf(m.stepSketch, FaceRole::EndCap))
                    .has_value());
        const RegenerationReport report =
            failsWith(m.bossSketch, ErrorCode::InvalidArgument,
                      "BossSketch (object:7): StepSketch (object:3) is a sketch, not a feature, and has no faces");
        CHECK(report.blocked == std::vector<ObjectId>{m.boss, m.pocket});
        CHECK(sketchOf(m.doc, m.bossSketch).placement() == placement);
        recovers();
    }
    // P12-SKETCH-003: a pattern's faces are named as copies of its source's.
    SECTION("a pattern, whose faces are copies, named by the face they copy") {
        const ObjectId row = m.doc.addObject(*LinearPatternFeature::create(
                                                 "Row", {.source = FeatureId::fromValue(m.boss.value()),
                                                         .first = {.direction = {0.0, 1.0, 0.0},
                                                                   .count = 2,
                                                                   .spacing = 20_mm}}))
                                 .value();
        const ObjectId onRow = addSketch(m.doc, "OnRow", FaceBlockModel::faceOf(row, FaceRole::EndCap));
        failsWith(onRow, ErrorCode::InvalidArgument,
                  "OnRow (object:12): Row (object:11) is a linear pattern, whose faces are copies: name the face it "
                  "copies, and the copy");
    }
    SECTION("an entity the profile does not have, a point, construction geometry") {
        REQUIRE(attach(history, m.doc, m.sideSketch,
                       FaceBlockModel::faceOf(m.base, FaceRole::Side, EntityId::fromValue(99)))
                    .has_value());
        failsWith(m.sideSketch, ErrorCode::NotFound,
                  "SideSketch (object:9): Base (object:6): entity:99 is not an entity of its profile BaseSketch "
                  "(object:5)");
        REQUIRE(history.undo(m.doc).has_value());

        const EntityId corner = sketchOf(m.doc, m.baseSketch).entities().front().id;
        REQUIRE(sketchOf(m.doc, m.baseSketch).entities().front().type() == sketch::EntityType::Point);
        REQUIRE(attach(history, m.doc, m.sideSketch, FaceBlockModel::faceOf(m.base, FaceRole::Side, corner))
                    .has_value());
        failsWith(m.sideSketch, ErrorCode::InvalidArgument,
                  std::format("SideSketch (object:9): Base (object:6): {} of its profile is a point, which sweeps no "
                              "face",
                              corner));
        REQUIRE(history.undo(m.doc).has_value());

        EntityId helper{};
        REQUIRE(history
                    .execute(m.doc, std::make_unique<sketch::ModifySketchCommand>(
                                        SketchId::fromValue(m.baseSketch.value()), "Add a helper line",
                                        [&](sketch::Sketch& s) -> Result<void> {
                                            auto line = s.addLine(Point2D{0_mm, 30_mm}, Point2D{100_mm, 30_mm});
                                            if (!line) {
                                                return std::unexpected(line.error());
                                            }
                                            helper = *line;
                                            auto construction = s.setConstruction(*line, true);
                                            if (!construction) {
                                                return std::unexpected(construction.error());
                                            }
                                            const auto& ends = std::get<sketch::LineEntity>(
                                                s.findEntity(*line)->geometry);
                                            for (const EntityId point : {ends.start, ends.end}) {
                                                if (auto fixed = s.addFixed(point); !fixed) {
                                                    return std::unexpected(fixed.error());
                                                }
                                            }
                                            return {};
                                        }))
                    .has_value());
        REQUIRE(attach(history, m.doc, m.sideSketch, FaceBlockModel::faceOf(m.base, FaceRole::Side, helper))
                    .has_value());
        failsWith(m.sideSketch, ErrorCode::InvalidArgument,
                  std::format("SideSketch (object:9): Base (object:6): {} of its profile is construction geometry, "
                              "which sweeps no face",
                              helper));
        REQUIRE(history.undo(m.doc).has_value());
        recovers();
    }
    SECTION("a face that is not a plane") {
        const EntityId circle = sketchOf(m.doc, m.bossSketch).entities().back().id;
        REQUIRE(sketchOf(m.doc, m.bossSketch).findEntity(circle)->type() == sketch::EntityType::Circle);
        const ObjectId onWall = addSketch(m.doc, "OnWall", FaceBlockModel::faceOf(m.boss, FaceRole::Side, circle));
        failsWith(onWall, ErrorCode::InvalidArgument,
                  std::format("OnWall (object:11): the side from {} of Boss (object:8) is a cylinder, not a plane",
                              circle));
    }
    SECTION("the producing feature fails: its dependents are blocked") {
        setPlate(history, m.doc, m.plate, 0_mm); // depth 0
        const RegenerationReport report = requireReport(regenerator, m.doc);
        CHECK(report.failed == std::vector<ObjectId>{m.base});
        for (const ObjectId id : {m.bossSketch, m.boss, m.sideSketch, m.pocket}) {
            CHECK(std::ranges::find(report.blocked, id) != report.blocked.end());
        }
        CHECK(sketchOf(m.doc, m.bossSketch).placement() == placement);
        recovers();
    }
    SECTION("a sketch attached to the feature it profiles is a cycle") {
        REQUIRE(attach(history, m.doc, m.baseSketch, FaceBlockModel::faceOf(m.base, FaceRole::EndCap)).has_value());
        const RegenerationReport report = requireReport(regenerator, m.doc);
        REQUIRE(report.cycles.size() == 1);
        CHECK_THAT(report.errors.at(m.base).message, ContainsSubstring("dependency cycle: "));
        CHECK_THAT(report.errors.at(m.base).message, ContainsSubstring("BaseSketch"));
        CHECK(report.errors.at(m.baseSketch).message == report.errors.at(m.base).message);
        CHECK(std::ranges::find(report.blocked, m.pocket) != report.blocked.end());
        recovers();
    }
    SECTION("without the regenerated bodies a face cannot be found") {
        const auto frame = resolvePlane(m.doc, FaceBlockModel::faceOf(m.base, FaceRole::EndCap));
        REQUIRE_FALSE(frame.has_value());
        CHECK(frame.error().code == ErrorCode::FailedPrecondition);
        CHECK(frame.error().message == "the end cap of Base (object:6) is found in its feature's regenerated body, "
                                       "which is not available here");
        const auto noBody = resolvePlane(m.doc, FaceBlockModel::faceOf(m.base, FaceRole::EndCap),
                                         [](ObjectId) -> const geometry::Body* { return nullptr; });
        REQUIRE_FALSE(noBody.has_value());
        CHECK(noBody.error().message == "the end cap of Base (object:6) cannot be found: Base (object:6) has no body");
    }
}

TEST_CASE("FaceReference_ValidationReportsBadFaceReferences", "[features][references][validation][p12]") {
    FaceBlockModel m;
    const ValidationReport clean = validateDocument(m.doc);
    INFO((clean.issues.empty() ? std::string{} : clean.issues.front().message));
    CHECK(clean.valid());

    Document broken = m.doc.clone();
    REQUIRE(broken.modifyObject<sketch::Sketch>(m.bossSketch, [&](sketch::Sketch& s) {
                     return s.setAttachment(FaceBlockModel::faceOf(m.stepSketch, FaceRole::EndCap));
                 }).value());
    REQUIRE(broken.modifyObject<sketch::Sketch>(m.sideSketch, [&](sketch::Sketch& s) {
                     return s.setAttachment(FaceBlockModel::faceOf(m.base, FaceRole::Side, EntityId::fromValue(99)));
                 }).value());
    const ValidationReport report = validateDocument(broken);
    CHECK_FALSE(report.valid());
    std::vector<std::string> messages;
    for (const ValidationIssue& issue : report.issues) {
        if (issue.check == ValidationCheck::DocumentConsistency) {
            messages.push_back(issue.message);
        }
    }
    const auto has = [&](std::string_view text) {
        return std::ranges::any_of(messages,
                                   [&](const std::string& line) { return line.find(text) != std::string::npos; });
    };
    CHECK(has("BossSketch (object:7): the attachment is the end cap of StepSketch (object:3): StepSketch (object:3) "
              "is a sketch, not a feature, and has no faces"));
    CHECK(has("SideSketch (object:9): the attachment is the side from entity:99 of Base (object:6): Base (object:6): "
              "entity:99 is not an entity of its profile BaseSketch (object:5)"));
}

// --- Commands and determinism ----------------------------------------------------------------------------

TEST_CASE("FaceReference_AttachingIsUndoable", "[features][references][commands][p12]") {
    FaceBlockModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CommandHistory history;
    // Move the side sketch to Base's top: the pocket (20 x 10 x 5 mm) is now
    // cut down from the top at x 10..30, y 5..15, clear of the boss.
    REQUIRE(attach(history, m.doc, m.sideSketch, FaceBlockModel::faceOf(m.base, FaceRole::EndCap)).has_value());
    CHECK(history.undoDescription() == "Attach");
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    checkFrame(sketchOf(m.doc, m.sideSketch).placement(), {0, 0, 20}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1});
    CHECK_THAT(volumeMm3(regenerator, m.pocket), WithinRel(FaceBlockModel::volume(20.0), 1e-12));
    const auto box = regenerator.body(m.pocket)->massProperties();
    REQUIRE(box.has_value());
    const std::array<double, 3> centre = FaceBlockModel::centre(20.0);
    // The removed 1000 mm^3 moved from (20, 2.5, 10) to (20, 10, 17.5).
    const double total = FaceBlockModel::volume(20.0);
    checkVec(mm(box->centerOfMass),
             {centre[0], centre[1] - 1000.0 * (10.0 - 2.5) / total, centre[2] - 1000.0 * (17.5 - 10.0) / total},
             1e-9);
    REQUIRE(history.undo(m.doc).has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    checkModel(regenerator, m, 20.0);
    REQUIRE(history.redo(m.doc).has_value());
    CHECK(sketchOf(m.doc, m.sideSketch).attachment() == FaceBlockModel::faceOf(m.base, FaceRole::EndCap));
}

TEST_CASE("FaceReference_RegeneratesDeterministically", "[features][references][regeneration][p12]") {
    FaceBlockModel a;
    FaceBlockModel b;
    Regenerator ra;
    Regenerator rb;
    REQUIRE(requireReport(ra, a.doc).succeeded());
    REQUIRE(requireReport(rb, b.doc).succeeded());
    CHECK(bits(volumeMm3(ra, a.pocket)) == bits(volumeMm3(rb, b.pocket)));
    CHECK(equivalent(a.doc, b.doc));
    CHECK(sketchOf(a.doc, a.bossSketch).placement() == sketchOf(b.doc, b.bossSketch).placement());
    // The same names on the same faces, in the same order.
    const auto namesOf = [](const Regenerator& r, ObjectId id) {
        const auto faces = geometry::listFaces(*r.body(id));
        REQUIRE(faces.has_value());
        std::vector<std::vector<FaceName>> names;
        for (const geometry::FaceInfo& face : *faces) {
            names.push_back(face.names);
        }
        return names;
    };
    CHECK(namesOf(ra, a.base) == namesOf(rb, b.base));
    // A second pass rebuilds nothing; a full rebuild gives the same result.
    CHECK(requireReport(ra, a.doc).regenerated.empty());
    const double before = volumeMm3(ra, a.pocket);
    const Frame3D placement = sketchOf(a.doc, a.sideSketch).placement();
    auto again = ra.regenerateAll(a.doc);
    REQUIRE(again.has_value());
    REQUIRE(again->succeeded());
    CHECK(bits(volumeMm3(ra, a.pocket)) == bits(before));
    CHECK(sketchOf(a.doc, a.sideSketch).placement() == placement);
}
