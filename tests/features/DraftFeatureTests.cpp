#include "TestHelpers.hpp"
#include "core/geometry/GeometryTestSupport.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/DraftModels.hpp"

#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/features/Datums.hpp>
#include <bettercad/features/FaceReferences.hpp>
#include <bettercad/features/FeatureCommands.hpp>
#include <bettercad/features/FilletFeature.hpp>
#include <bettercad/features/LinearPatternFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/ResultBodies.hpp>
#include <bettercad/features/RevolveFeature.hpp>
#include <bettercad/features/SplitFeature.hpp>
#include <bettercad/features/Validation.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <format>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using bettercad::test::DraftedBlockModel;
using bettercad::test::DraftedPartsModel;
using bettercad::test::ExpectedShell;
using bettercad::test::FaceKindModel;
using bettercad::test::grownCircle;
using bettercad::test::kPi;
using bettercad::test::nameOf;
using bettercad::test::Quadratic;
using bettercad::test::requireReport;
using bettercad::test::Shift;
using bettercad::test::shrunkRectangle;
using bettercad::test::sideWidth;
using bettercad::test::turnedArea;
using bettercad::test::Vec3;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// P12-FEAT-004: draft features. Expected volumes, centres, bounds and face
// areas are written out from the parameters in support/DraftModels.hpp.

namespace {

// Planes, cylinders and cones are integrated to rounding (the kernel probe
// measured at most 4e-16).
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

/// A draft's body against its expected volume, centre and bounds, with the
/// topology of the body it drafted: one valid solid.
void checkDraft(const Regenerator& regenerator, ObjectId id, ObjectId source, const ExpectedShell& expected) {
    const geometry::Body& body = bodyOf(regenerator, id);
    CHECK(body.isValid());
    CHECK(body.topology().solids == 1);
    CHECK(body.topology() == bodyOf(regenerator, source).topology());
    const auto props = body.massProperties();
    REQUIRE(props.has_value());
    CHECK_THAT(props->volume.in(units::mm3), WithinRel(expected.volume(), kRel));
    checkVec(mm(props->centerOfMass), expected.centre(), kTolCentreMm);
    const auto box = body.boundingBox();
    REQUIRE(box.has_value());
    checkVec(mm(box->min), expected.lower, kTolBounds);
    checkVec(mm(box->max), expected.upper, kTolBounds);
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

void setValue(CommandHistory& history, Document& doc, ParameterId parameter, const DimensionedValue& value) {
    REQUIRE(history
                .execute(doc, std::make_unique<ModifyParameterCommand>(parameter, ParameterChanges{.value = value}))
                .has_value());
}

void modifyDraft(CommandHistory& history, Document& doc, ObjectId draft,
                 const std::function<void(DraftDefinition&)>& change) {
    DraftDefinition d = doc.findObjectAs<DraftFeature>(draft)->definition();
    change(d);
    REQUIRE(history.execute(doc, std::make_unique<ModifyDraftCommand>(FaceKindModel::featureOf(draft), d))
                .has_value());
}

/// @p item failed with @p code, keeping no body; returns its message.
std::string failureOf(const RegenerationReport& report, const Regenerator& regenerator, ObjectId item,
                      ErrorCode code) {
    CHECK(std::ranges::find(report.failed, item) != report.failed.end());
    REQUIRE(report.errors.contains(item));
    CHECK(report.errors.at(item).code == code);
    CHECK(regenerator.body(item) == nullptr);
    return report.errors.at(item).message;
}

FeatureId fid(ObjectId id) {
    return FaceKindModel::featureOf(id);
}

constexpr std::string_view kCannotTurn =
    "cannot be turned about the neutral plane (kernel: a face cannot be recomputed); a face parallel to the "
    "plane, or a chain of tangent faces that reaches one, cannot be drafted";

} // namespace

TEST_CASE("DraftFeature_DefinitionsAreValidated", "[features][draft][p12]") {
    const FeatureId target = FeatureId::fromValue(1);
    const FaceName side = nameOf(ObjectId::fromValue(1), FaceRole::Side, EntityId::fromValue(3));
    const auto message = [](const DraftDefinition& d) {
        const auto created = DraftFeature::create("D", d);
        REQUIRE_FALSE(created.has_value());
        CHECK(created.error().code == ErrorCode::InvalidArgument);
        return created.error().message;
    };
    REQUIRE(DraftFeature::create("D", {.target = target, .faces = {side}, .angle = 3_deg}).has_value());
    REQUIRE(DraftFeature::create("D", {.target = target, .faces = {side}}).has_value()); // zero angle
    CHECK(message({.faces = {side}, .angle = 3_deg}) == "a draft needs a target feature");
    CHECK(message({.target = target, .angle = 3_deg}) == "a draft needs one or more faces");
    CHECK(message({.target = target, .faces = {side, side}, .angle = 3_deg}) == "face 2 repeats an earlier face");
    CHECK(message({.target = target, .faces = {nameOf(ObjectId::fromValue(1), FaceRole::Side)}, .angle = 3_deg}) ==
          "face 1: a side face is named by a valid profile entity");
    CHECK(message({.target = target, .faces = {side}, .neutralPlane = {.object = ObjectId{}}, .angle = 3_deg}) ==
          "the neutral plane: a plane reference must name a valid object");
    CHECK(message({.target = target, .faces = {side}, .angle = 90_deg}) ==
          "the draft angle must be in (-90, 90) deg, got 90 deg");
    CHECK(message({.target = target,
                   .faces = {side},
                   .angle = Angle::fromSi(std::numeric_limits<double>::infinity())}) ==
          "the draft angle must be in (-90, 90) deg, got inf deg");
    CHECK(message({.target = target, .faces = {side}, .angleParameter = ParameterId{}}) ==
          "the angle parameter ID must be valid");
    // While a parameter drives the angle the literal is not used; the driven
    // value's range is checked when it is known.
    REQUIRE(DraftFeature::create("D", {.target = target,
                                       .faces = {side},
                                       .angle = 120_deg,
                                       .angleParameter = ParameterId::fromValue(9)})
                .has_value());

    // Dependencies: the target, the parameter, the plane's objects, the faces'
    // features and the features that copied them, each once, in that order.
    FaceName copied = nameOf(ObjectId::fromValue(4), FaceRole::Side, EntityId::fromValue(2));
    copied.face.copies = {FaceCopy{ObjectId::fromValue(6), 2}};
    const auto draft = DraftFeature::create(
        "D", {.target = FeatureId::fromValue(7),
              .faces = {side, copied},
              .neutralPlane = {.object = ObjectId::fromValue(8), .face = FaceSelector{.role = FaceRole::EndCap}},
              .angleParameter = ParameterId::fromValue(9)});
    REQUIRE(draft.has_value());
    CHECK((*draft)->dependencies() == std::vector<ObjectId>{ObjectId::fromValue(7), ObjectId::fromValue(9),
                                                            ObjectId::fromValue(8), ObjectId::fromValue(1),
                                                            ObjectId::fromValue(4), ObjectId::fromValue(6)});
    CHECK((*draft)->consumedFeatures() == std::vector<FeatureId>{FeatureId::fromValue(7)});
    CHECK((*draft)->typeName() == "draft");
    auto edited = DraftFeature::create("D", {.target = target, .faces = {side}, .angle = 3_deg});
    REQUIRE(edited.has_value());
    CHECK((*edited)->setDefinition({.target = target, .faces = {side}, .angle = 3_deg}) == false);
    CHECK((*edited)->setDefinition({.target = target, .faces = {side}, .angle = 4_deg}) == true);
}

TEST_CASE("DraftFeature_BlockFollowsItsAngleAndHeight", "[features][draft][regeneration][p12][acceptance]") {
    DraftedBlockModel m;
    Regenerator regenerator;
    CommandHistory history;
    const auto check = [&](double h, double a, double r) {
        CAPTURE(h, a, r);
        const std::array<std::pair<ObjectId, Shift>, 5> drafts{{
            {m.tapered, Shift::of(a, 0.0)},
            {m.taperedMid, Shift::of(a, r)},
            {m.flared, Shift::of(-5.0, 0.0)},
            {m.onTop, Shift::of(a, h)},
            {m.onBottom, Shift::of(-5.0, 0.0, true)},
        }};
        for (std::size_t k = 0; k < drafts.size(); ++k) {
            const auto& [id, shift] = drafts[k];
            CAPTURE(k);
            checkDraft(regenerator, id, m.block, DraftedBlockModel::shape(h, shift));
            // The turned faces keep their names, with their turned areas.
            const double angle = k == 2 || k == 4 ? 5.0 : a;
            const auto areas = DraftedBlockModel::areas(h, shift, angle);
            for (std::size_t i = 0; i < m.lines.size(); ++i) {
                CHECK_THAT(namedArea(regenerator, id, nameOf(m.block, FaceRole::Side, m.lines[i])),
                           WithinRel(areas[i], kRel));
            }
            CHECK_THAT(namedArea(regenerator, id, nameOf(m.block, FaceRole::EndCap)), WithinRel(areas[4], kRel));
            CHECK_THAT(namedArea(regenerator, id, nameOf(m.block, FaceRole::StartCap)), WithinRel(areas[5], kRel));
        }
        // Pulled down from the bottom at -5 deg is Tapered at 5 deg.
        if (a == 5.0) {
            CHECK_THAT(volumeOf(regenerator, m.onBottom), WithinRel(volumeOf(regenerator, m.tapered), kRel));
        }
    };
    regenerate(regenerator, m.doc);
    check(40.0, 5.0, 20.0);
    const auto first = volumes(regenerator, m.doc);
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.tapered, m.taperedMid, m.flared, m.onTop, m.onBottom});
    const DocumentGraph graph = buildDependencyGraph(m.doc);
    const auto deps = graph.graph.dependenciesOf(m.taperedMid);
    for (const ObjectId id : {m.block, m.mid, ObjectId{m.taper}}) {
        CHECK(std::ranges::find(deps, id) != deps.end());
    }

    // A steeper taper rebuilds only the drafts it drives.
    setValue(history, m.doc, m.taper, DimensionedValue::of(8_deg));
    const RegenerationReport steeper = regenerate(regenerator, m.doc);
    for (const ObjectId id : {m.tapered, m.taperedMid, m.onTop}) {
        CHECK(std::ranges::find(steeper.regenerated, id) != steeper.regenerated.end());
    }
    for (const ObjectId id : {m.block, m.flared, m.onBottom}) {
        CHECK(std::ranges::find(steeper.regenerated, id) == steeper.regenerated.end());
    }
    check(40.0, 8.0, 20.0);
    // A taller block moves its top: OnTop turns about it where it now is.
    setValue(history, m.doc, m.height, DimensionedValue::of(55_mm));
    regenerate(regenerator, m.doc);
    check(55.0, 8.0, 20.0);
    // The mid plane moves; a negative taper flares.
    setValue(history, m.doc, m.rise, DimensionedValue::of(35_mm));
    setValue(history, m.doc, m.taper, DimensionedValue::of(-(2.5_deg)));
    regenerate(regenerator, m.doc);
    check(55.0, -2.5, 35.0);

    for (int i = 0; i < 4; ++i) {
        REQUIRE(history.undo(m.doc).has_value());
    }
    regenerate(regenerator, m.doc);
    check(40.0, 5.0, 20.0);
    CHECK(volumes(regenerator, m.doc) == first);
    Document copy = m.doc.clone();
    Regenerator again;
    regenerate(again, copy);
    CHECK(volumes(again, copy) == first);
}

TEST_CASE("DraftFeature_CurvedPocketedConcaveAndRoundedBodies",
          "[features][draft][regeneration][p12][acceptance]") {
    DraftedPartsModel m;
    Regenerator regenerator;
    CommandHistory history;
    const auto check = [&](double a, double h, double r) {
        CAPTURE(a, h, r);
        checkDraft(regenerator, m.canTaper, m.can, DraftedPartsModel::canShape(a));
        checkDraft(regenerator, m.boreTaper, m.bore, DraftedPartsModel::boreShape(a, h));
        checkDraft(regenerator, m.pocketTaper, m.pocket, DraftedPartsModel::pocketShape(a, h));
        checkDraft(regenerator, m.ellTaper, m.ell, DraftedPartsModel::ellShape(a));
        checkDraft(regenerator, m.padTaper, m.rounded, DraftedPartsModel::padShape(a, h, r));
        // One side turns its whole tangent chain.
        checkDraft(regenerator, m.padOneSide, m.rounded, DraftedPartsModel::padShape(a, h, r));

        const double t = std::tan(a * kPi / 180.0);
        const double slant = 1.0 / std::cos(a * kPi / 180.0);
        // The can: a frustum.
        const double top = 20.0 - 50.0 * t;
        CHECK_THAT(namedArea(regenerator, m.canTaper, nameOf(m.can, FaceRole::Side, m.canCircle)),
                   WithinRel(kPi * (20.0 + top) * 50.0 * slant, kRel));
        CHECK_THAT(namedArea(regenerator, m.canTaper, nameOf(m.can, FaceRole::EndCap)),
                   WithinRel(kPi * top * top, kRel));
        // The bore widens going up.
        const double mouth = 10.0 + h * t;
        CHECK_THAT(namedArea(regenerator, m.boreTaper, nameOf(m.bore, FaceRole::Side, m.boreCircle)),
                   WithinRel(kPi * (10.0 + mouth) * h * slant, kRel));
        CHECK_THAT(namedArea(regenerator, m.boreTaper, nameOf(m.plate, FaceRole::EndCap)),
                   WithinRel(6000.0 - kPi * mouth * mouth, kRel));
        // The pocket: its mouth stays 60 x 30, its floor shrinks, its walls
        // narrow going down.
        const Shift up = Shift::of(a, h);
        const Shift inward{-up.slope, -up.offset};
        CHECK_THAT(namedArea(regenerator, m.pocketTaper, nameOf(m.block, FaceRole::EndCap)),
                   WithinRel(4200.0, kRel));
        CHECK_THAT(namedArea(regenerator, m.pocketTaper, nameOf(m.pocket, FaceRole::StartCap)),
                   WithinRel(shrunkRectangle(60, 30, inward).at(15.0), kRel));
        for (std::size_t i = 0; i < m.pocketLines.size(); ++i) {
            CHECK_THAT(namedArea(regenerator, m.pocketTaper, nameOf(m.pocket, FaceRole::Side, m.pocketLines[i])),
                       WithinRel(turnedArea(sideWidth(i % 2 == 0 ? 60.0 : 30.0, inward), 15.0, h, a), kRel));
        }
        // The L's top shrinks with square corners.
        const Shift s = Shift::of(a, 0.0);
        const Quadratic ellTop{shrunkRectangle(100, 30, s).c0 + shrunkRectangle(40, 80, s).c0 -
                                   shrunkRectangle(40, 30, s).c0,
                               shrunkRectangle(100, 30, s).c1 + shrunkRectangle(40, 80, s).c1 -
                                   shrunkRectangle(40, 30, s).c1,
                               shrunkRectangle(100, 30, s).c2 + shrunkRectangle(40, 80, s).c2 -
                                   shrunkRectangle(40, 30, s).c2};
        CHECK_THAT(namedArea(regenerator, m.ellTaper, nameOf(m.ell, FaceRole::EndCap)),
                   WithinRel(ellTop.at(50.0), kRel));
        CHECK_THAT(namedArea(regenerator, m.ellTaper, nameOf(m.ell, FaceRole::StartCap)), WithinRel(5000.0, kRel));
        // The pad's planar sides keep their width between the rounds.
        for (std::size_t i = 0; i < m.padLines.size(); ++i) {
            const double width = (i % 2 == 0 ? 100.0 : 60.0) - 2.0 * r;
            for (const ObjectId id : {m.padTaper, m.padOneSide}) {
                CHECK_THAT(namedArea(regenerator, id, nameOf(m.pad, FaceRole::Side, m.padLines[i])),
                           WithinRel(width * h * slant, kRel));
            }
        }
        CHECK_THAT(namedArea(regenerator, m.padTaper, nameOf(m.pad, FaceRole::EndCap)),
                   WithinRel(shrunkRectangle(100, 60, s, r).at(h), kRel));
    };
    regenerate(regenerator, m.doc);
    check(3.0, 40.0, 10.0);
    const auto first = volumes(regenerator, m.doc);

    setValue(history, m.doc, m.angle, DimensionedValue::of(5_deg));
    setValue(history, m.doc, m.height, DimensionedValue::of(55_mm));
    setValue(history, m.doc, m.round, DimensionedValue::of(8_mm));
    regenerate(regenerator, m.doc);
    check(5.0, 55.0, 8.0);
    for (int i = 0; i < 3; ++i) {
        REQUIRE(history.undo(m.doc).has_value());
    }
    regenerate(regenerator, m.doc);
    CHECK(volumes(regenerator, m.doc) == first);
}

TEST_CASE("DraftFeature_FailuresAreStructuredAndAtomic", "[features][draft][p12]") {
    SECTION("angles that make faces vanish") {
        DraftedBlockModel m;
        Regenerator regenerator;
        regenerate(regenerator, m.doc);
        const auto before = volumes(regenerator, m.doc);
        CommandHistory history;
        // At 37 deg the block's 60 mm top would be gone; about the mid plane
        // and about the top there is room.
        setValue(history, m.doc, m.taper, DimensionedValue::of(37_deg));
        const RegenerationReport report = requireReport(regenerator, m.doc);
        CHECK(failureOf(report, regenerator, m.tapered, ErrorCode::FailedPrecondition) ==
              "Tapered: draft: the kernel cannot build the draft: kernel: an edge cannot be recomputed; an angle "
              "this large may make faces vanish");
        CHECK(report.failed == std::vector<ObjectId>{m.tapered});
        checkDraft(regenerator, m.taperedMid, m.block, DraftedBlockModel::shape(40.0, Shift::of(37.0, 20.0)));
        checkDraft(regenerator, m.onTop, m.block, DraftedBlockModel::shape(40.0, Shift::of(37.0, 40.0)));
        // A driven angle out of range.
        setValue(history, m.doc, m.taper, DimensionedValue::of(95_deg));
        const RegenerationReport steep = requireReport(regenerator, m.doc);
        CHECK(failureOf(steep, regenerator, m.tapered, ErrorCode::InvalidArgument) ==
              "Tapered: draft: the draft angle must be in (-90, 90) deg, got 95 deg");
        // Undo restores the bodies, bit for bit.
        REQUIRE(history.undo(m.doc).has_value());
        REQUIRE(history.undo(m.doc).has_value());
        regenerate(regenerator, m.doc);
        CHECK(volumes(regenerator, m.doc) == before);
    }
    SECTION("faces the draft cannot turn") {
        DraftedBlockModel m;
        // The top is parallel to the neutral plane.
        const ObjectId withTop = m.add(DraftFeature::create(
            "WithTop", {.target = fid(m.block),
                        .faces = {m.sides().front(), nameOf(m.block, FaceRole::EndCap)},
                        .angle = 3_deg}));
        // A block whose top edges are rounded: its sides' tangent chains run
        // through the rounds into the top.
        std::array<EntityId, 4> cappedLines{};
        const ObjectId cappedSketch = m.addFixedRectangle("CappedSketch", 200.0, 0.0, 100.0, 60.0, &cappedLines);
        const ObjectId capped = m.addBoss("Capped", cappedSketch, 40.0);
        const std::vector<geometry::EdgeSignature> topEdges{
            geometry::lineSignature(Point3D{200_mm, 0_mm, 40_mm}, Direction3D::unitX()),
            geometry::lineSignature(Point3D{200_mm, 60_mm, 40_mm}, Direction3D::unitX()),
            geometry::lineSignature(Point3D{200_mm, 0_mm, 40_mm}, Direction3D::unitY()),
            geometry::lineSignature(Point3D{300_mm, 0_mm, 40_mm}, Direction3D::unitY())};
        const ObjectId domed =
            m.add(FilletFeature::create("Domed", {.target = fid(capped), .edges = topEdges, .radius = 5_mm}));
        const ObjectId chained = m.add(DraftFeature::create(
            "Chained", {.target = fid(domed),
                        .faces = {nameOf(capped, FaceRole::Side, cappedLines[1])},
                        .angle = 3_deg}));
        // A torus: a circle revolved about an axis beside it.
        auto ringSketch = std::make_unique<sketch::Sketch>("RingSketch");
        const EntityId ring = bettercad::test::require(ringSketch->addCircle(Point2D{430_mm, 0_mm}, 5_mm));
        bettercad::test::require(
            ringSketch->addFixed(std::get<sketch::CircleEntity>(ringSketch->findEntity(ring)->geometry).center));
        bettercad::test::require(ringSketch->addRadius(ring, 5_mm));
        const ObjectId ringSketchId = m.doc.addObject(std::move(ringSketch)).value();
        const ObjectId torus = m.add(RevolveFeature::create(
            "Torus", {.profile = FaceKindModel::sketchOf(ringSketchId), .axis = RevolveAxis::sketchY()}));
        const ObjectId turnedRing = m.add(DraftFeature::create(
            "TurnedRing", {.target = fid(torus), .faces = {nameOf(torus, FaceRole::Side, ring)}, .angle = 3_deg}));
        Regenerator regenerator;
        const RegenerationReport report = requireReport(regenerator, m.doc);
        CHECK(failureOf(report, regenerator, withTop, ErrorCode::FailedPrecondition) ==
              std::format("WithTop: draft: face 2 {}", kCannotTurn));
        CHECK(failureOf(report, regenerator, chained, ErrorCode::FailedPrecondition) ==
              std::format("Chained: draft: face 1 {}", kCannotTurn));
        CHECK(failureOf(report, regenerator, turnedRing, ErrorCode::FailedPrecondition) ==
              "TurnedRing: draft: face 1 is a torus; only planes, cylinders and cones can be drafted");
        CHECK(report.failed.size() == 3);
        CHECK(regenerator.body(domed) != nullptr);
        CHECK(regenerator.body(torus) != nullptr);
    }
    SECTION("faces the target's body does not have, and planes that are not planes") {
        DraftedBlockModel m;
        const ObjectId otherSketch = m.addFixedRectangle("OtherSketch", 200.0, 0.0, 10.0, 10.0);
        const ObjectId other = m.addBoss("Other", otherSketch, 10.0);
        const ObjectId level = m.add(DatumPlane::create(
            "Level", {.kind = DatumPlaneKind::Offset, .base = {.plane = PrincipalPlane::XY}, .offset = 10_mm}));
        const ObjectId base = m.add(SplitFeature::create(
            "Base", {.target = fid(m.block), .plane = {.object = level}, .keep = geometry::SplitKeep::Back}));
        const ObjectId middle = m.add(DatumPlane::create(
            "Middle", {.kind = DatumPlaneKind::Offset, .base = {.plane = PrincipalPlane::YZ}, .offset = 50_mm}));
        const ObjectId halves = m.add(SplitFeature::create(
            "Halves", {.target = fid(m.block), .plane = {.object = middle}, .keep = geometry::SplitKeep::Both}));
        const auto draftOf = [&](const std::string& name, ObjectId target, std::vector<FaceName> faces,
                             PlaneReference plane = {}) {
            return m.add(DraftFeature::create(
                name, {.target = fid(target), .faces = std::move(faces), .neutralPlane = plane, .angle = 3_deg}));
        };
        const ObjectId wrongBody = draftOf("WrongBody", m.block, {nameOf(other, FaceRole::EndCap)});
        const ObjectId cutAway = draftOf("CutAway", base, {nameOf(m.block, FaceRole::EndCap)});
        const ObjectId unnamed = draftOf("Unnamed", base, {nameOf(base, FaceRole::EndCap)});
        const ObjectId twoSolids = draftOf("TwoSolids", halves, m.sides());
        const ObjectId onSketch = draftOf("OnSketch", m.block, m.sides(), PlaneReference{.object = m.blockSketch});
        const ObjectId noBody = draftOf("NoBody", m.blockSketch, m.sides());
        Regenerator regenerator;
        const RegenerationReport report = requireReport(regenerator, m.doc);
        CHECK(failureOf(report, regenerator, wrongBody, ErrorCode::NotFound) ==
              std::format("WrongBody: face 1, the end cap of Other ({}), is not a face of the body of Block ({})",
                          other, m.block));
        CHECK(failureOf(report, regenerator, cutAway, ErrorCode::NotFound) ==
              std::format("CutAway: face 1, the end cap of Block ({}), is not a face of the body of Base ({})",
                          m.block, base));
        CHECK(failureOf(report, regenerator, unnamed, ErrorCode::InvalidArgument) ==
              std::format("Unnamed: face 1: Base ({}) is a split, whose faces are not named (extrudes, revolves, "
                          "sweeps, lofts, holes, chamfers and ribs name theirs)",
                          base));
        CHECK(failureOf(report, regenerator, twoSolids, ErrorCode::FailedPrecondition) ==
              "TwoSolids: draft: a draft turns faces of one solid; the body has 2");
        CHECK(failureOf(report, regenerator, onSketch, ErrorCode::InvalidArgument) ==
              std::format("OnSketch: the neutral plane: BlockSketch ({}) is a sketch, not a datum plane or a "
                          "coordinate system",
                          m.blockSketch));
        CHECK(failureOf(report, regenerator, noBody, ErrorCode::FailedPrecondition) ==
              "NoBody: a draft needs the body of its target feature");
        CHECK(regenerator.body(base) != nullptr);

        // Validation says why for what it can check without geometry.
        const ValidationReport validation = validateDocument(m.doc);
        CHECK_FALSE(validation.valid());
        const auto has = [&](const std::string& text) {
            return std::ranges::any_of(validation.issues,
                                       [&](const ValidationIssue& issue) { return issue.message == text; });
        };
        CHECK(has(std::format("Unnamed ({}): face 1 is the end cap of Base ({}): Base ({}) is a split, whose faces "
                              "are not named (extrudes, revolves, sweeps, lofts, holes, chamfers and ribs name theirs)",
                              unnamed, base, base)));
        CHECK(has(std::format("OnSketch ({}): the neutral plane is BlockSketch ({}), which is a sketch, not a datum "
                              "plane or a coordinate system",
                              onSketch, m.blockSketch)));
        CHECK(has(std::format("NoBody ({}): the target is BlockSketch ({}), which is a sketch, not a feature with a "
                              "body",
                              noBody, m.blockSketch)));
        // The faces and the plane are dependencies.
        const DocumentGraph graph = buildDependencyGraph(m.doc);
        CHECK(std::ranges::find(graph.graph.dependenciesOf(wrongBody), other) !=
              graph.graph.dependenciesOf(wrongBody).end());
        CHECK(std::ranges::find(graph.graph.dependenciesOf(onSketch), m.blockSketch) !=
              graph.graph.dependenciesOf(onSketch).end());
    }
    SECTION("angles of the wrong kind") {
        DraftedBlockModel m;
        CommandHistory history;
        modifyDraft(history, m.doc, m.tapered, [&](DraftDefinition& d) { d.angleParameter = m.height; });
        Regenerator regenerator;
        const RegenerationReport report = requireReport(regenerator, m.doc);
        CHECK(failureOf(report, regenerator, m.tapered, ErrorCode::DimensionMismatch).starts_with("Tapered: "));
        const ValidationReport validation = validateDocument(m.doc);
        CHECK(std::ranges::any_of(validation.issues, [&](const ValidationIssue& issue) {
            return issue.message == std::format("Tapered ({}): the angle is driven by height ({}), which is a "
                                                "length, not an angle",
                                                m.tapered, ObjectId{m.height});
        }));
    }
    SECTION("drafts are not repeated and their faces are not named") {
        DraftedBlockModel m;
        const ObjectId row = m.add(LinearPatternFeature::create(
            "Row", {.source = fid(m.tapered), .first = {.direction = {1.0, 0.0, 0.0}, .count = 2, .spacing = 150_mm}}));
        auto onDraft = std::make_unique<sketch::Sketch>("OnDraft");
        REQUIRE(onDraft->setAttachment(FaceKindModel::faceOf(m.tapered, {.role = FaceRole::EndCap})).has_value());
        const ObjectId onDraftId = m.doc.addObject(std::move(onDraft)).value();
        Regenerator regenerator;
        const RegenerationReport report = requireReport(regenerator, m.doc);
        REQUIRE(report.errors.contains(row));
        CHECK_THAT(report.errors.at(row).message, ContainsSubstring("a linear pattern cannot repeat a draft"));
        REQUIRE(report.errors.contains(onDraftId));
        CHECK(report.errors.at(onDraftId).message ==
              std::format("OnDraft ({}): Tapered ({}) is a draft, whose faces are not named (extrudes, revolves, "
                          "sweeps, lofts, holes, chamfers and ribs name theirs)",
                          onDraftId, m.tapered));
    }
}

TEST_CASE("DraftFeature_RegeneratesDeterministically", "[features][draft][regeneration][p12]") {
    const auto namesOf = [](const Regenerator& r, ObjectId id) {
        const auto faces = geometry::listFaces(bodyOf(r, id));
        REQUIRE(faces.has_value());
        std::vector<std::vector<FaceName>> names;
        for (const geometry::FaceInfo& face : *faces) {
            names.push_back(face.names);
        }
        return names;
    };
    DraftedPartsModel a;
    DraftedPartsModel b;
    Regenerator ra;
    Regenerator rb;
    regenerate(ra, a.doc);
    regenerate(rb, b.doc);
    CHECK(volumes(ra, a.doc) == volumes(rb, b.doc));
    CHECK(equivalent(a.doc, b.doc));
    for (const ObjectId id : {a.canTaper, a.boreTaper, a.pocketTaper, a.ellTaper, a.padTaper, a.padOneSide}) {
        CAPTURE(id);
        CHECK(namesOf(ra, id) == namesOf(rb, id));
        CHECK(bodyOf(ra, id).topology() == bodyOf(rb, id).topology());
    }
    const auto before = volumes(ra, a.doc);
    CHECK(requireReport(ra, a.doc).regenerated.empty());
    auto again = ra.regenerateAll(a.doc);
    REQUIRE(again.has_value());
    REQUIRE(again->succeeded());
    CHECK(volumes(ra, a.doc) == before);
}

TEST_CASE("DraftFeature_CreationAndEditsAreUndoable", "[features][draft][commands][p12]") {
    DraftedBlockModel m;
    Regenerator regenerator;
    regenerate(regenerator, m.doc);
    const Document before = m.doc.clone();
    CommandHistory history;
    // The front alone, 10 deg about the model's XY plane.
    const DraftDefinition front{.target = fid(m.block), .faces = {m.sides().front()}, .angle = 10_deg};
    REQUIRE(history.execute(m.doc, std::make_unique<CreateDraftCommand>("Front", front)).has_value());
    const auto created = m.doc.findByName("Front");
    REQUIRE(created.has_value());
    regenerate(regenerator, m.doc);
    const double wedge = 100.0 * 800.0 * std::tan(10.0 * kPi / 180.0);
    CHECK_THAT(volumeOf(regenerator, *created), WithinRel(240000.0 - wedge, kRel));

    // Pulled down from the same plane the wedge is added.
    modifyDraft(history, m.doc, *created, [](DraftDefinition& d) { d.angle = -(10_deg); });
    regenerate(regenerator, m.doc);
    CHECK_THAT(volumeOf(regenerator, *created), WithinRel(240000.0 + wedge, kRel));
    // An invalid edit is refused and changes nothing.
    const Document edited = m.doc.clone();
    DraftDefinition none = m.doc.findObjectAs<DraftFeature>(*created)->definition();
    none.faces.clear();
    const auto refused = history.execute(m.doc, std::make_unique<ModifyDraftCommand>(fid(*created), none));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().message == "a draft needs one or more faces");
    CHECK(equivalent(edited, m.doc));

    REQUIRE(history.undo(m.doc).has_value());
    regenerate(regenerator, m.doc);
    CHECK_THAT(volumeOf(regenerator, *created), WithinRel(240000.0 - wedge, kRel));
    REQUIRE(history.undo(m.doc).has_value());
    CHECK(equivalent(before, m.doc));
    REQUIRE(history.redo(m.doc).has_value());
    REQUIRE(history.redo(m.doc).has_value());
    regenerate(regenerator, m.doc);
    CHECK_THAT(volumeOf(regenerator, *m.doc.findByName("Front")), WithinRel(240000.0 + wedge, kRel));
}
