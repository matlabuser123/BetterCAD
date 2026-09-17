#include "TestHelpers.hpp"
#include "core/geometry/GeometryTestSupport.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/BodyOpsModels.hpp"

#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/features/FaceReferences.hpp>
#include <bettercad/features/FeatureCommands.hpp>
#include <bettercad/features/LinearPatternFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/ResultBodies.hpp>
#include <bettercad/features/Validation.hpp>
#include <bettercad/sketch/Sketch.hpp>

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
#include <numbers>
#include <optional>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using bettercad::test::BodyOpsModel;
using bettercad::test::FaceKindModel;
using bettercad::test::requireReport;
using bettercad::test::Vec3;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using geometry::SplitKeep;

// P12-FEAT-002: split and combine features. Expected volumes, centres,
// bounds and face areas are written out from the parameters in
// support/BodyOpsModels.hpp.

namespace {

constexpr double pi = std::numbers::pi;
// Planes and cylinders are integrated to rounding (P12-DATUM-001).
constexpr double kRel = bettercad::test::kRelTight;
constexpr double kTolCentreMm = bettercad::test::kPositionToleranceMm;
constexpr double kTolBounds = 1e-7; // exact bounds (P3)

Vec3 mm(const Point3D& p) {
    return {p.x.in(units::mm), p.y.in(units::mm), p.z.in(units::mm)};
}

void checkVec(const Vec3& actual, const Vec3& expected, double tolerance) {
    CHECK_THAT(actual[0], WithinAbs(expected[0], tolerance));
    CHECK_THAT(actual[1], WithinAbs(expected[1], tolerance));
    CHECK_THAT(actual[2], WithinAbs(expected[2], tolerance));
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

const geometry::Body& bodyOf(const Regenerator& regenerator, ObjectId id) {
    const geometry::Body* body = regenerator.body(id);
    REQUIRE(body != nullptr);
    return *body;
}

double volumeOf(const Regenerator& regenerator, ObjectId id) {
    const auto props = bodyOf(regenerator, id).massProperties();
    REQUIRE(props.has_value());
    return props->volume.in(units::mm3);
}

/// A body against its expected solids, volume, centre and bounds.
void checkBody(const Regenerator& regenerator, ObjectId id, std::size_t solids, double volume, const Vec3& centre,
               const Vec3& min, const Vec3& max) {
    const geometry::Body& body = bodyOf(regenerator, id);
    CHECK(body.isValid());
    CHECK(body.topology().solids == solids);
    const auto props = body.massProperties();
    REQUIRE(props.has_value());
    CHECK_THAT(props->volume.in(units::mm3), WithinRel(volume, kRel));
    checkVec(mm(props->centerOfMass), centre, kTolCentreMm);
    const auto box = body.boundingBox();
    REQUIRE(box.has_value());
    checkVec(mm(box->min), min, kTolBounds);
    checkVec(mm(box->max), max, kTolBounds);
}

/// The total area of the faces of @p id's body that carry @p name.
double namedArea(const Regenerator& regenerator, ObjectId id, const FaceName& name) {
    const auto faces = geometry::findNamedFaces(bodyOf(regenerator, id), name);
    REQUIRE(faces.has_value());
    double area = 0.0;
    for (const geometry::FaceInfo& face : *faces) {
        area += face.area.in(units::mm2);
    }
    return area;
}

/// Every body volume of a document, bit for bit.
std::vector<std::uint64_t> volumes(const Regenerator& regenerator, const Document& doc) {
    std::vector<std::uint64_t> result;
    for (const DocumentObject& object : doc.objects()) {
        if (regenerator.body(object.id()) != nullptr) {
            result.push_back(bits(volumeOf(regenerator, object.id())));
        }
    }
    return result;
}

void setValue(CommandHistory& history, Document& doc, ParameterId parameter, Length value) {
    REQUIRE(history
                .execute(doc, std::make_unique<ModifyParameterCommand>(
                                  parameter, ParameterChanges{.value = DimensionedValue::of(value)}))
                .has_value());
}

void setKeep(CommandHistory& history, Document& doc, ObjectId split, SplitKeep keep) {
    SplitDefinition d = doc.findObjectAs<SplitFeature>(split)->definition();
    d.keep = keep;
    REQUIRE(history.execute(doc, std::make_unique<ModifySplitCommand>(FaceKindModel::featureOf(split), d))
                .has_value());
}

/// Regenerates: @p item fails with @p code and @p message, keeping no body.
RegenerationReport failsWith(Regenerator& regenerator, Document& doc, ObjectId item, ErrorCode code,
                             const std::string& message) {
    RegenerationReport report = requireReport(regenerator, doc);
    CHECK(std::ranges::find(report.failed, item) != report.failed.end());
    REQUIRE(report.errors.contains(item));
    CHECK(report.errors.at(item).code == code);
    CHECK(report.errors.at(item).message == message);
    CHECK(regenerator.body(item) == nullptr);
    return report;
}

FeatureId fid(ObjectId id) {
    return FaceKindModel::featureOf(id);
}

} // namespace

TEST_CASE("SplitCombine_DefinitionsAreValidated", "[features][split][combine][p12]") {
    CHECK(geometry::toString(SplitKeep::Front) == "front");
    CHECK(geometry::toString(SplitKeep::Back) == "back");
    CHECK(geometry::toString(SplitKeep::Both) == "both");
    const auto splitMessage = [](const SplitDefinition& d) {
        const auto created = SplitFeature::create("S", d);
        REQUIRE_FALSE(created.has_value());
        CHECK(created.error().code == ErrorCode::InvalidArgument);
        return created.error().message;
    };
    const SplitDefinition split{.target = FeatureId::fromValue(1), .plane = {}};
    REQUIRE(SplitFeature::create("S", split).has_value());
    CHECK(splitMessage({.plane = {}}) == "a split needs a target feature");
    CHECK(splitMessage({.target = FeatureId::fromValue(1), .plane = {.object = ObjectId{}}}) ==
          "the split plane: a plane reference must name a valid object");
    CHECK(splitMessage({.target = FeatureId::fromValue(1),
                        .plane = {.face = FaceSelector{.role = FaceRole::EndCap}}}) ==
          "the split plane: a face reference must name the feature that generates the face");
    CHECK(splitMessage({.target = FeatureId::fromValue(1), .plane = {}, .keep = static_cast<SplitKeep>(7)}) ==
          "a split keeps the front, the back or both");

    const auto combineMessage = [](const CombineDefinition& d) {
        const auto created = CombineFeature::create("C", d);
        REQUIRE_FALSE(created.has_value());
        CHECK(created.error().code == ErrorCode::InvalidArgument);
        return created.error().message;
    };
    const FeatureId t = FeatureId::fromValue(1);
    const FeatureId u = FeatureId::fromValue(2);
    const FeatureId v = FeatureId::fromValue(3);
    REQUIRE(CombineFeature::create("C", {.target = t, .tools = {u, v}}).has_value());
    CHECK(combineMessage({.tools = {u}}) == "a combine needs a target feature");
    CHECK(combineMessage({.target = t}) == "a combine needs one or more tool features");
    CHECK(combineMessage({.target = t, .tools = {u, FeatureId{}}}) == "tool 2 must be a valid feature");
    CHECK(combineMessage({.target = t, .tools = {u, t}}) ==
          "tool 2 is the target: a body is not combined with itself");
    CHECK(combineMessage({.target = t, .tools = {u, v, u}}) == "tool 3 repeats an earlier tool");
    CHECK(combineMessage({.target = t, .tools = {u}, .operation = FeatureOperation::NewBody}) ==
          "a combine joins, cuts or intersects bodies, got new body");

    // Dependencies and consumption.
    const auto combine = CombineFeature::create("C", {.target = t, .tools = {v, u}});
    REQUIRE(combine.has_value());
    CHECK((*combine)->dependencies() ==
          std::vector<ObjectId>{ObjectId::fromValue(1), ObjectId::fromValue(3), ObjectId::fromValue(2)});
    CHECK((*combine)->consumedFeatures() == std::vector<FeatureId>{t, v, u});
    const auto onFace = SplitFeature::create(
        "S", {.target = t, .plane = {.object = ObjectId::fromValue(5), .face = FaceSelector{.role = FaceRole::EndCap}}});
    REQUIRE(onFace.has_value());
    CHECK((*onFace)->dependencies() == std::vector<ObjectId>{ObjectId::fromValue(1), ObjectId::fromValue(5)});
    CHECK((*onFace)->consumedFeatures() == std::vector<FeatureId>{t});
}

TEST_CASE("Combine_JoinsCutsAndIntersectsAsTheOverlapChanges",
          "[features][combine][regeneration][p12][acceptance]") {
    BodyOpsModel m;
    const ObjectId carved = m.add(CombineFeature::create(
        "Carved", {.target = fid(m.a), .tools = {fid(m.b), fid(m.c)}, .operation = FeatureOperation::Cut}));
    const ObjectId common = m.add(CombineFeature::create(
        "Common", {.target = fid(m.a), .tools = {fid(m.b)}, .operation = FeatureOperation::Intersect}));
    Regenerator regenerator;
    const auto check = [&](double h) {
        CAPTURE(h);
        checkBody(regenerator, m.joined, 1, BodyOpsModel::joinedVolume(h), BodyOpsModel::joinedCentre(h), {0, 0, 0},
                  {150, 60, std::max(h, 40.0)});
        checkBody(regenerator, carved, 1, BodyOpsModel::cutVolume(h), BodyOpsModel::cutCentre(h), {0, 0, 0},
                  {100, 60, h});
        checkBody(regenerator, common, 1, BodyOpsModel::abVolume(h), BodyOpsModel::abCentre(h), {80, 20, 0},
                  {100, 40, std::min(h, 40.0)});
    };
    regenerate(regenerator, m.doc);
    check(20.0);
    // The joined body carries A's top: where B and C stand on it, it is gone.
    CHECK_THAT(namedArea(regenerator, m.joined, FaceName{m.a, {.role = FaceRole::EndCap}}),
               WithinRel(6000.0 - 400.0 - 100.0 * pi, kRel));
    // And B's and C's walls.
    CHECK(namedArea(regenerator, m.joined, FaceName{m.c, {.role = FaceRole::EndCap}}) > 0.0);
    // The consumed features are no longer results.
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.halves, carved, common});

    const auto first = volumes(regenerator, m.doc);
    CommandHistory history;
    setValue(history, m.doc, m.height, 30_mm);
    regenerate(regenerator, m.doc);
    check(30.0);
    setValue(history, m.doc, m.height, 45_mm);
    regenerate(regenerator, m.doc);
    check(45.0);
    // At 45 mm A rises above B (40 mm) and C (30 mm): its top is whole.
    CHECK_THAT(namedArea(regenerator, m.joined, FaceName{m.a, {.role = FaceRole::EndCap}}), WithinRel(6000.0, kRel));
    REQUIRE(history.undo(m.doc).has_value());
    REQUIRE(history.undo(m.doc).has_value());
    regenerate(regenerator, m.doc);
    check(20.0);
    CHECK(volumes(regenerator, m.doc) == first);
}

TEST_CASE("Split_KeepsEachSideAsTheCutMoves", "[features][split][regeneration][p12][acceptance]") {
    BodyOpsModel m;
    Regenerator regenerator;
    CommandHistory history;
    const auto check = [&](double h, double c) {
        CAPTURE(h, c);
        // Both parts, as two solids.
        checkBody(regenerator, m.halves, 2, BodyOpsModel::joinedVolume(h), BodyOpsModel::joinedCentre(h), {0, 0, 0},
                  {150, 60, std::max(h, 40.0)});
        setKeep(history, m.doc, m.halves, SplitKeep::Front);
        regenerate(regenerator, m.doc);
        checkBody(regenerator, m.halves, 1, BodyOpsModel::frontVolume(h, c), BodyOpsModel::frontCentre(h, c),
                  {c, 0, 0}, {150, 60, std::max(h, 40.0)});
        // A's top in front of the cut, less B's footprint.
        CHECK_THAT(namedArea(regenerator, m.halves, FaceName{m.a, {.role = FaceRole::EndCap}}),
                   WithinRel((100.0 - c) * 60.0 - 400.0, kRel));
        setKeep(history, m.doc, m.halves, SplitKeep::Back);
        regenerate(regenerator, m.doc);
        checkBody(regenerator, m.halves, 1, BodyOpsModel::backVolume(h, c), BodyOpsModel::backCentre(h, c),
                  {0, 0, 0}, {c, 60, std::max(h, 30.0)});
        CHECK_THAT(namedArea(regenerator, m.halves, FaceName{m.a, {.role = FaceRole::EndCap}}),
                   WithinRel(c * 60.0 - 100.0 * pi, kRel));
        REQUIRE(history.undo(m.doc).has_value());
        REQUIRE(history.undo(m.doc).has_value());
        regenerate(regenerator, m.doc);
    };
    regenerate(regenerator, m.doc);
    const auto first = volumes(regenerator, m.doc);
    check(20.0, 60.0);
    CHECK(volumes(regenerator, m.doc) == first);
    setValue(history, m.doc, m.cut, 70_mm);
    const RegenerationReport moved = regenerate(regenerator, m.doc);
    for (const ObjectId id : {m.middle, m.halves}) {
        CHECK(std::ranges::find(moved.regenerated, id) != moved.regenerated.end());
    }
    for (const ObjectId id : {m.a, m.b, m.c, m.joined}) {
        CHECK(std::ranges::find(moved.regenerated, id) == moved.regenerated.end());
    }
    check(20.0, 70.0);
    setValue(history, m.doc, m.height, 25_mm);
    regenerate(regenerator, m.doc);
    check(25.0, 70.0);
}

TEST_CASE("Split_ObliqueThroughTheCentreGivesCongruentHalves", "[features][split][p12]") {
    FaceKindModel m("1c7e4a95-8b2f-4d30-a6e1-5f9d2b8c4a73", "Oblique");
    const ObjectId sketch = m.addFixedRectangle("BlockSketch", 0.0, 0.0, 100.0, 60.0);
    const ObjectId block = m.add(ExtrudeFeature::create(
        "Block", {.profile = FaceKindModel::sketchOf(sketch), .depth = 20_mm}));
    const auto normal = Direction3D::fromComponents(1.0, 1.0, 1.0).value();
    const auto xAxis = Direction3D::fromComponents(1.0, -1.0, 0.0).value();
    const ObjectId tilted = m.add(DatumPlane::create(
        "Tilted", {.kind = DatumPlaneKind::Fixed,
                   .frame = Frame3D::create(Point3D{50_mm, 30_mm, 10_mm}, normal, xAxis).value()}));
    const ObjectId front = m.add(SplitFeature::create(
        "Front", {.target = fid(block), .plane = {.object = tilted}, .keep = SplitKeep::Front}));
    const ObjectId back = m.add(SplitFeature::create(
        "Back", {.target = fid(block), .plane = {.object = tilted}, .keep = SplitKeep::Back}));
    Regenerator regenerator;
    regenerate(regenerator, m.doc);
    // The block is symmetric about its centre, and so is the plane through
    // it: the halves are congruent.
    CHECK_THAT(volumeOf(regenerator, front), WithinRel(60000.0, kRel));
    CHECK_THAT(volumeOf(regenerator, back), WithinRel(60000.0, kRel));
    const auto frontProps = bodyOf(regenerator, front).massProperties();
    const auto backProps = bodyOf(regenerator, back).massProperties();
    REQUIRE(frontProps.has_value());
    REQUIRE(backProps.has_value());
    const Vec3 f = mm(frontProps->centerOfMass);
    const Vec3 k = mm(backProps->centerOfMass);
    checkVec({f[0] + k[0], f[1] + k[1], f[2] + k[2]}, {100.0, 60.0, 20.0}, kTolCentreMm);
    // The front half lies on the normal's side of the plane.
    CHECK((f[0] - 50.0) + (f[1] - 30.0) + (f[2] - 10.0) > 0.0);
    // Both are single solids; the block is consumed by both.
    CHECK(bodyOf(regenerator, front).topology().solids == 1);
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{front, back});
}

TEST_CASE("Split_FollowsANamedFace", "[features][split][references][regeneration][p12]") {
    // A tower cut at the height of a step's top: the step's end cap is the
    // split plane (z = ledge, facing +Z); the tower below it is kept.
    FaceKindModel m("4b9d2e71-6a3c-4f85-9e07-c8a1b5d3f290", "Ledge");
    const ParameterId ledge = m.doc.createParameter("ledge", 12_mm, units::mm).value();
    const ObjectId stepSketch = m.addFixedRectangle("StepSketch", 0.0, 0.0, 50.0, 50.0);
    const ObjectId step = m.add(ExtrudeFeature::create(
        "Step", {.profile = FaceKindModel::sketchOf(stepSketch), .depthParameter = ledge}));
    const ObjectId towerSketch = m.addFixedRectangle("TowerSketch", 100.0, 0.0, 20.0, 30.0);
    const ObjectId tower = m.add(ExtrudeFeature::create(
        "Tower", {.profile = FaceKindModel::sketchOf(towerSketch), .depth = 50_mm}));
    const ObjectId stub = m.add(SplitFeature::create(
        "Stub", {.target = fid(tower),
                 .plane = FaceKindModel::faceOf(step, {.role = FaceRole::EndCap}),
                 .keep = SplitKeep::Back}));
    Regenerator regenerator;
    CommandHistory history;
    for (const double z : {12.0, 20.0, 35.0}) {
        CAPTURE(z);
        if (z != 12.0) {
            setValue(history, m.doc, ledge, z * units::mm);
        }
        regenerate(regenerator, m.doc);
        checkBody(regenerator, stub, 1, 600.0 * z, {110.0, 15.0, z / 2.0}, {100, 0, 0}, {120, 30, z});
    }
    // The split depends on the step.
    const DocumentGraph graph = buildDependencyGraph(m.doc);
    const auto deps = graph.graph.dependenciesOf(stub);
    CHECK(std::ranges::find(deps, step) != deps.end());
}

TEST_CASE("SplitCombine_FailuresAreStructuredAndAtomic", "[features][split][combine][p12]") {
    SECTION("a plane beyond the body") {
        BodyOpsModel m;
        Regenerator regenerator;
        regenerate(regenerator, m.doc);
        const double before = volumeOf(regenerator, m.halves);
        CommandHistory history;
        setValue(history, m.doc, m.cut, 160_mm);
        failsWith(regenerator, m.doc, m.halves, ErrorCode::FailedPrecondition,
                  "Halves: split: the plane does not cross the body: all of it lies behind the plane");
        setValue(history, m.doc, m.cut, -(5_mm));
        failsWith(regenerator, m.doc, m.halves, ErrorCode::FailedPrecondition,
                  "Halves: split: the plane does not cross the body: all of it lies in front of the plane");
        REQUIRE(history.undo(m.doc).has_value());
        REQUIRE(history.undo(m.doc).has_value());
        regenerate(regenerator, m.doc);
        CHECK(bits(volumeOf(regenerator, m.halves)) == bits(before));
    }
    SECTION("a plane that crosses the bounds but not the body") {
        FaceKindModel m("9a2c5e18-4d7b-4f63-b0e9-2c8f6a1d5b47", "Corner");
        auto profile = bettercad::test::fixedPolygon(
            "LSketch", Frame3D::xy(), {{0, 0}, {100, 0}, {100, 20}, {20, 20}, {20, 100}, {0, 100}});
        const ObjectId sketch = m.doc.addObject(std::move(profile)).value();
        const ObjectId ell = m.add(ExtrudeFeature::create(
            "Ell", {.profile = FaceKindModel::sketchOf(sketch), .depth = 20_mm}));
        const auto normal = Direction3D::fromComponents(1.0, 1.0, 0.0).value();
        const ObjectId corner = m.add(DatumPlane::create(
            "Corner", {.kind = DatumPlaneKind::Fixed,
                       .frame = Frame3D::create(Point3D{80_mm, 80_mm, 0_mm}, normal, Direction3D::unitZ()).value()}));
        const ObjectId cornered = m.add(SplitFeature::create(
            "Cornered", {.target = fid(ell), .plane = {.object = corner}, .keep = SplitKeep::Back}));
        Regenerator regenerator;
        const RegenerationReport report = failsWith(
            regenerator, m.doc, cornered, ErrorCode::FailedPrecondition,
            "Cornered: split: the plane does not cross the body: nothing of it lies in front of the plane");
        CHECK(report.failed == std::vector<ObjectId>{cornered});
        CHECK(regenerator.body(ell) != nullptr);
    }
    SECTION("combinations that leave nothing") {
        BodyOpsModel m;
        const ObjectId bigSketch = m.addFixedRectangle("BigSketch", -10.0, -10.0, 120.0, 80.0);
        const ObjectId big = m.add(ExtrudeFeature::create(
            "Big", {.profile = FaceKindModel::sketchOf(bigSketch), .depth = 60_mm}));
        const ObjectId farSketch = m.addFixedRectangle("FarSketch", 200.0, 0.0, 10.0, 10.0);
        const ObjectId far = m.add(ExtrudeFeature::create(
            "Far", {.profile = FaceKindModel::sketchOf(farSketch), .depth = 10_mm}));
        const ObjectId erase = m.add(CombineFeature::create(
            "Erase", {.target = fid(m.a), .tools = {fid(big)}, .operation = FeatureOperation::Cut}));
        const ObjectId apart = m.add(CombineFeature::create(
            "Apart", {.target = fid(m.c), .tools = {fid(far)}, .operation = FeatureOperation::Intersect}));
        Regenerator regenerator;
        const RegenerationReport report = requireReport(regenerator, m.doc);
        CHECK(report.failed.size() == 2);
        CHECK(std::ranges::find(report.failed, erase) != report.failed.end());
        CHECK(std::ranges::find(report.failed, apart) != report.failed.end());
        CHECK(report.errors.at(erase).message ==
              std::format("Erase: nothing is left after cutting Big ({})", big));
        CHECK(report.errors.at(apart).message ==
              std::format("Apart: nothing is left after intersecting with Far ({})", far));
        CHECK(report.errors.at(erase).code == ErrorCode::FailedPrecondition);
        CHECK(regenerator.body(erase) == nullptr);
        // The rest of the model is built.
        CHECK(regenerator.body(m.halves) != nullptr);
    }
    SECTION("a tool without a body, and a split plane that is not a plane") {
        BodyOpsModel m;
        const ObjectId withSketch = m.add(CombineFeature::create(
            "WithSketch", {.target = fid(m.b), .tools = {fid(m.cSketch)}, .operation = FeatureOperation::Join}));
        CommandHistory history;
        SplitDefinition d = m.doc.findObjectAs<SplitFeature>(m.halves)->definition();
        d.plane = PlaneReference{.object = m.a};
        REQUIRE(history.execute(m.doc, std::make_unique<ModifySplitCommand>(fid(m.halves), d)).has_value());
        Regenerator regenerator;
        const RegenerationReport report = requireReport(regenerator, m.doc);
        CHECK(report.failed.size() == 2);
        CHECK(std::ranges::find(report.failed, withSketch) != report.failed.end());
        CHECK(std::ranges::find(report.failed, m.halves) != report.failed.end());
        CHECK(report.errors.at(withSketch).message ==
              std::format("WithSketch: tool 1, CSketch ({}), has no body", m.cSketch));
        CHECK(report.errors.at(m.halves).message ==
              "Halves: the split plane: A (object:4) is an extrude, not a datum plane or a coordinate system");
        CHECK(regenerator.body(m.halves) == nullptr);

        // Validation says why.
        const ValidationReport validation = validateDocument(m.doc);
        CHECK_FALSE(validation.valid());
        const auto has = [&](std::string_view text) {
            return std::ranges::any_of(validation.issues, [&](const ValidationIssue& issue) {
                return issue.message.find(text) != std::string::npos;
            });
        };
        CHECK(has(std::format("WithSketch ({}): tool 1 is CSketch (object:7), which is a sketch, not a feature "
                              "with a body",
                              withSketch)));
        CHECK(has("Halves (object:11): the split plane is A (object:4), which is an extrude, not a datum plane or a "
                  "coordinate system"));
    }
    SECTION("splits and combines are not repeated, and their own faces are not named") {
        BodyOpsModel m;
        const ObjectId row = m.add(LinearPatternFeature::create(
            "Row", {.source = fid(m.halves), .first = {.direction = {0.0, 1.0, 0.0}, .count = 2, .spacing = 80_mm}}));
        auto onSplit = std::make_unique<sketch::Sketch>("OnSplit");
        REQUIRE(onSplit->setAttachment(FaceKindModel::faceOf(m.halves, {.role = FaceRole::EndCap})).has_value());
        const ObjectId onSplitId = m.doc.addObject(std::move(onSplit)).value();
        Regenerator regenerator;
        const RegenerationReport report = requireReport(regenerator, m.doc);
        REQUIRE(report.errors.contains(row));
        CHECK_THAT(report.errors.at(row).message, ContainsSubstring("a linear pattern cannot repeat a split"));
        REQUIRE(report.errors.contains(onSplitId));
        CHECK(report.errors.at(onSplitId).message ==
              std::format("OnSplit ({}): Halves (object:11) is a split, whose faces are not named (extrudes, "
                          "revolves, sweeps, lofts, holes and chamfers name theirs)",
                          onSplitId));
    }
}

TEST_CASE("SplitCombine_CreationAndEditsAreUndoable", "[features][split][combine][commands][p12]") {
    BodyOpsModel m;
    Regenerator regenerator;
    regenerate(regenerator, m.doc);
    const Document before = m.doc.clone();
    CommandHistory history;
    auto create = std::make_unique<CreateCombineCommand>(
        "Carved", CombineDefinition{.target = fid(m.a), .tools = {fid(m.b)}, .operation = FeatureOperation::Cut});
    REQUIRE(history.execute(m.doc, std::move(create)).has_value());
    const auto carved = m.doc.findByName("Carved");
    REQUIRE(carved.has_value());
    regenerate(regenerator, m.doc);
    CHECK_THAT(volumeOf(regenerator, *carved), WithinRel(120000.0 - 8000.0, kRel));
    // The cut consumes A and B as well.
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.halves, *carved});

    CombineDefinition joined = m.doc.findObjectAs<CombineFeature>(*carved)->definition();
    joined.operation = FeatureOperation::Join;
    REQUIRE(history.execute(m.doc, std::make_unique<ModifyCombineCommand>(fid(*carved), joined)).has_value());
    regenerate(regenerator, m.doc);
    CHECK_THAT(volumeOf(regenerator, *carved), WithinRel(120000.0 + 56000.0 - 8000.0, kRel));
    // An invalid edit is refused and changes nothing.
    CombineDefinition itself = joined;
    itself.tools = {fid(m.a)};
    const Document edited = m.doc.clone();
    const auto refused = history.execute(m.doc, std::make_unique<ModifyCombineCommand>(fid(*carved), itself));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().message == "tool 1 is the target: a body is not combined with itself");
    CHECK(equivalent(edited, m.doc));

    REQUIRE(history.undo(m.doc).has_value());
    regenerate(regenerator, m.doc);
    CHECK_THAT(volumeOf(regenerator, *carved), WithinRel(120000.0 - 8000.0, kRel));
    REQUIRE(history.undo(m.doc).has_value());
    CHECK(equivalent(before, m.doc));
    regenerate(regenerator, m.doc);
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.halves});
    REQUIRE(history.redo(m.doc).has_value());
    CHECK(m.doc.findObjectAs<CombineFeature>(*carved) != nullptr);
    regenerate(regenerator, m.doc);
    CHECK_THAT(volumeOf(regenerator, *carved), WithinRel(120000.0 - 8000.0, kRel));

    // A split is created and edited the same way.
    auto split = std::make_unique<CreateSplitCommand>(
        "Left", SplitDefinition{.target = fid(*carved), .plane = {.object = m.middle}, .keep = SplitKeep::Back});
    REQUIRE(history.execute(m.doc, std::move(split)).has_value());
    const auto left = m.doc.findByName("Left");
    REQUIRE(left.has_value());
    regenerate(regenerator, m.doc);
    // A less B, behind x = 60.
    CHECK_THAT(volumeOf(regenerator, *left), WithinRel(60.0 * 60.0 * 20.0, kRel));
    REQUIRE(history.undo(m.doc).has_value());
    CHECK(m.doc.findObject(*left) == nullptr);
}

TEST_CASE("SplitCombine_RegenerateDeterministically", "[features][split][combine][regeneration][p12]") {
    BodyOpsModel a;
    BodyOpsModel b;
    Regenerator ra;
    Regenerator rb;
    regenerate(ra, a.doc);
    regenerate(rb, b.doc);
    CHECK(volumes(ra, a.doc) == volumes(rb, b.doc));
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
    CHECK(namesOf(ra, a.halves) == namesOf(rb, b.halves));
    const auto before = volumes(ra, a.doc);
    CHECK(requireReport(ra, a.doc).regenerated.empty());
    auto again = ra.regenerateAll(a.doc);
    REQUIRE(again.has_value());
    REQUIRE(again->succeeded());
    CHECK(volumes(ra, a.doc) == before);
}
