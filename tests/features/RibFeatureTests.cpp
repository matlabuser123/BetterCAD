#include "TestHelpers.hpp"
#include "core/geometry/GeometryTestSupport.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/RibModels.hpp"

#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/geometry/Profile.hpp>
#include <bettercad/features/FaceReferences.hpp>
#include <bettercad/features/FeatureCommands.hpp>
#include <bettercad/features/LinearPatternFeature.hpp>
#include <bettercad/features/Regeneration.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/ResultBodies.hpp>
#include <bettercad/features/ShellFeature.hpp>
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
using bettercad::test::ExpectedShell;
using bettercad::test::FaceKindModel;
using bettercad::test::requireReport;
using bettercad::test::RibbedBracketModel;
using bettercad::test::RibProfilesModel;
using bettercad::test::Vec3;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// P12-FEAT-005: rib features. Expected volumes, centres, bounds and face
// areas are written out from the parameters in support/RibModels.hpp.

namespace {

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

/// A ribbed body against its expected volume, centre and bounds: one valid
/// solid. @p rel loosens the volume and centre checks for splines.
void checkRibbed(const Regenerator& regenerator, ObjectId id, const ExpectedShell& expected, double rel = kRel,
                 double centreMm = kTolCentreMm) {
    const geometry::Body& body = bodyOf(regenerator, id);
    CHECK(body.isValid());
    CHECK(body.topology().solids == 1);
    const auto props = body.massProperties();
    REQUIRE(props.has_value());
    CHECK_THAT(props->volume.in(units::mm3), WithinRel(expected.volume(), rel));
    checkVec(mm(props->centerOfMass), expected.centre(), centreMm);
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

void setValue(CommandHistory& history, Document& doc, ParameterId parameter, Length value) {
    REQUIRE(history
                .execute(doc, std::make_unique<ModifyParameterCommand>(
                                  parameter, ParameterChanges{.value = DimensionedValue::of(value)}))
                .has_value());
}

void modifyRib(CommandHistory& history, Document& doc, ObjectId rib, const std::function<void(RibDefinition&)>& change) {
    RibDefinition d = doc.findObjectAs<RibFeature>(rib)->definition();
    change(d);
    REQUIRE(history.execute(doc, std::make_unique<ModifyRibCommand>(FaceKindModel::featureOf(rib), d)).has_value());
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

FaceName ribFace(ObjectId rib, FaceRole role, std::optional<EntityId> entity = std::nullopt) {
    return FaceName{rib, FaceSelector{.role = role, .entity = entity}};
}

constexpr std::string_view kOpen =
    "rib: the side the rib fills is not closed off by the body: it reaches past the body (fill the other side, or "
    "turn the profile towards the body)";

} // namespace

TEST_CASE("RibFeature_DefinitionsAreValidated", "[features][rib][p12]") {
    const RibDefinition good{.target = FeatureId::fromValue(1),
                             .profile = SketchId::fromValue(2),
                             .edges = {EntityId::fromValue(3), EntityId::fromValue(4)},
                             .thickness = 4_mm};
    REQUIRE(RibFeature::create("R", good).has_value());
    const auto message = [&](const std::function<void(RibDefinition&)>& change) {
        RibDefinition d = good;
        change(d);
        const auto created = RibFeature::create("R", d);
        REQUIRE_FALSE(created.has_value());
        CHECK(created.error().code == ErrorCode::InvalidArgument);
        return created.error().message;
    };
    CHECK(message([](RibDefinition& d) { d.target = FeatureId{}; }) == "a rib needs a target feature");
    CHECK(message([](RibDefinition& d) { d.profile = SketchId{}; }) == "a rib needs a profile sketch");
    CHECK(message([](RibDefinition& d) { d.edges.clear(); }) == "a rib needs one or more profile edges");
    CHECK(message([](RibDefinition& d) { d.edges.push_back(EntityId{}); }) ==
          "profile edge 3 must be a valid entity");
    CHECK(message([](RibDefinition& d) { d.edges.push_back(EntityId::fromValue(3)); }) ==
          "profile edge 3 repeats an earlier edge");
    CHECK(message([](RibDefinition& d) { d.thickness = Length{}; }) ==
          "the rib thickness must be positive and finite, got 0 mm");
    CHECK(message([](RibDefinition& d) { d.thicknessParameter = ParameterId{}; }) ==
          "the thickness parameter ID must be valid");
    CHECK(message([](RibDefinition& d) { d.placement = static_cast<geometry::RibPlacement>(9); }) ==
          "a rib's thickness lies symmetric, along or against the normal");
    // A driven thickness needs no literal one.
    RibDefinition driven = good;
    driven.thickness = Length{};
    driven.thicknessParameter = ParameterId::fromValue(7);
    const auto rib = RibFeature::create("R", driven);
    REQUIRE(rib.has_value());
    CHECK((*rib)->dependencies() ==
          std::vector<ObjectId>{ObjectId::fromValue(1), ObjectId::fromValue(2), ObjectId::fromValue(7)});
    CHECK((*rib)->consumedFeatures() == std::vector<FeatureId>{FeatureId::fromValue(1)});
    CHECK((*rib)->typeName() == "rib");
    CHECK((*rib)->setDefinition(driven) == false);
    RibDefinition flipped = driven;
    flipped.flipped = true;
    CHECK((*rib)->setDefinition(flipped) == true);
}

TEST_CASE("RibFeature_FollowsItsBracketAndThickness", "[features][rib][regeneration][p12][acceptance]") {
    RibbedBracketModel m;
    Regenerator regenerator;
    CommandHistory history;
    const auto check = [&](double f, double w, double mid, double t) {
        CAPTURE(f, w, mid, t);
        checkRibbed(regenerator, m.rib, RibbedBracketModel::shape(f, w, mid - t / 2.0, mid + t / 2.0));
        // The rib names its walls (the triangle, at both sides) and the side
        // its line makes (the line's length between the walls, times t).
        const double l = RibbedBracketModel::leg(f, w);
        CHECK_THAT(namedArea(regenerator, m.rib, ribFace(m.rib, FaceRole::StartCap)), WithinRel(l * l / 2.0, kRel));
        CHECK_THAT(namedArea(regenerator, m.rib, ribFace(m.rib, FaceRole::EndCap)), WithinRel(l * l / 2.0, kRel));
        CHECK_THAT(namedArea(regenerator, m.rib, ribFace(m.rib, FaceRole::Side, m.ribLines[0])),
                   WithinRel(l * std::numbers::sqrt2 * t, kRel));
        // The bracket keeps its names: the floor's upper side less the wall's
        // and the rib's feet, and the wall's inner side less the rib's.
        const FaceName floorTop{m.floorBody, FaceSelector{.role = FaceRole::Side, .entity = m.floorLines[2]}};
        CHECK_THAT(namedArea(regenerator, m.rib, floorTop), WithinRel(80.0 * 40.0 - w * 40.0 - l * t, kRel));
        const FaceName wallSide{m.bracket, FaceSelector{.role = FaceRole::Side, .entity = m.wallLines[1]}};
        CHECK_THAT(namedArea(regenerator, m.rib, wallSide), WithinRel((60.0 - f) * 40.0 - l * t, kRel));
        // The walls lie on the rib's plane and its offsets.
        const auto walls = geometry::findNamedFaces(bodyOf(regenerator, m.rib), ribFace(m.rib, FaceRole::EndCap));
        REQUIRE(walls.has_value());
        REQUIRE(walls->size() == 1);
        REQUIRE(walls->front().signature.has_value());
        CHECK_THAT(walls->front().signature->point.z.in(units::mm), WithinAbs(mid + t / 2.0, kTolCentreMm));
        CHECK(walls->front().signature->normal == Direction3D::unitZ());
    };
    regenerate(regenerator, m.doc);
    check(10.0, 10.0, 20.0, 4.0);
    const auto first = volumes(regenerator, m.doc);
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.rib});
    // Changing only the rib's own parameters and undoing them restores it
    // bit for bit.
    {
        CommandHistory own;
        setValue(own, m.doc, m.thickness, 5_mm);
        regenerate(regenerator, m.doc);
        REQUIRE(own.undo(m.doc).has_value());
        regenerate(regenerator, m.doc);
        CHECK(volumes(regenerator, m.doc) == first);
    }
    const DocumentGraph graph = buildDependencyGraph(m.doc);
    const auto deps = graph.graph.dependenciesOf(m.rib);
    for (const ObjectId id : {m.bracket, m.ribSketch, ObjectId{m.thickness}}) {
        CHECK(std::ranges::find(deps, id) != deps.end());
    }

    // A thicker rib regenerates only the rib.
    setValue(history, m.doc, m.thickness, 6_mm);
    const RegenerationReport thicker = regenerate(regenerator, m.doc);
    CHECK(thicker.regenerated == std::vector<ObjectId>{m.rib});
    check(10.0, 10.0, 20.0, 6.0);
    // The plane moves up; the rib follows its sketch.
    setValue(history, m.doc, m.mid, 30_mm);
    regenerate(regenerator, m.doc);
    check(10.0, 10.0, 30.0, 6.0);
    // The walls change: the same line closes a different triangle.
    setValue(history, m.doc, m.floor, 15_mm);
    setValue(history, m.doc, m.wall, 5_mm);
    regenerate(regenerator, m.doc);
    check(15.0, 5.0, 30.0, 6.0);

    for (int i = 0; i < 4; ++i) {
        REQUIRE(history.undo(m.doc).has_value());
    }
    regenerate(regenerator, m.doc);
    check(10.0, 10.0, 20.0, 4.0);
    // Undoing the sketch-driven walls re-solves their sketches, which
    // restores them to rounding (P12-SKETCH-003): checked above against the
    // analytic values. A fresh regeneration of a copy gives the same bits.
    Document copy = m.doc.clone();
    Regenerator again;
    regenerate(again, copy);
    CHECK(volumes(again, copy) == volumes(regenerator, m.doc));
}

TEST_CASE("RibFeature_ArcsChainsSplinesAndShortProfiles", "[features][rib][regeneration][p12][acceptance]") {
    RibProfilesModel m;
    Regenerator regenerator;
    regenerate(regenerator, m.doc);
    // The arc: the corner less a quarter disc.
    const double c = RibProfilesModel::arcCentroid();
    checkRibbed(regenerator, m.arcRib, RibProfilesModel::with(RibProfilesModel::arcArea(), c, c));
    CHECK_THAT(namedArea(regenerator, m.arcRib, ribFace(m.arcRib, FaceRole::Side, m.arc)),
               WithinRel(std::numbers::pi / 2.0 * 30.0 * 4.0, kRel));
    // Two lines.
    const auto [cx, cy] = RibProfilesModel::chainCentroid();
    checkRibbed(regenerator, m.chainRib, RibProfilesModel::with(RibProfilesModel::chainArea(), cx, cy));
    CHECK_THAT(namedArea(regenerator, m.chainRib, ribFace(m.chainRib, FaceRole::Side, m.chainLines[0])),
               WithinRel(std::hypot(20.0, 10.0) * 4.0, kRel));
    CHECK_THAT(namedArea(regenerator, m.chainRib, ribFace(m.chainRib, FaceRole::Side, m.chainLines[1])),
               WithinRel(std::hypot(10.0, 20.0) * 4.0, kRel));
    checkRibbed(regenerator, m.alongRib, RibProfilesModel::with(RibProfilesModel::chainArea(), cx, cy, 20.0, 24.0));
    // The spline: Green's theorem on the region it closes.
    const geometry::PlanarRegion region = RibProfilesModel::splineRegion();
    const double area = geometry::regionArea(region).in(units::mm2);
    const Point2D centroid = geometry::regionCentroid(region);
    checkRibbed(regenerator, m.splineRib,
                RibProfilesModel::with(area, centroid.x.in(units::mm), centroid.y.in(units::mm)), kRel,
                kTolCentreMm);
    // Face areas come from the kernel's default surface integration, which is
    // not exact on a plane bounded by a spline (measured 4.3e-6 here; see
    // P12-SKETCH-002); the volume above is exact.
    CHECK_THAT(namedArea(regenerator, m.splineRib, ribFace(m.splineRib, FaceRole::StartCap)), WithinRel(area, 1e-5));
    // A line short of the walls is extended to them.
    checkRibbed(regenerator, m.shortRib, RibProfilesModel::with(450.0, 20.0, 20.0));
    // The extensions continue the line, so the rib's free face is one plane
    // from wall to wall, and it carries the line's name.
    CHECK_THAT(namedArea(regenerator, m.shortRib, ribFace(m.shortRib, FaceRole::Side, m.shortLines[0])),
               WithinRel(30.0 * std::numbers::sqrt2 * 4.0, kRel));
}

TEST_CASE("RibFeature_WallsCarrySketchesAndBodiesCarryRibs", "[features][rib][references][p12]") {
    // A sketch on the rib's upper wall, and a boss on it: the boss follows
    // the wall as the rib thickens. A divider rib inside a shelled box.
    RibbedBracketModel m;
    const ObjectId postSketch =
        m.addCircleSketch("PostSketch", FaceKindModel::faceOf(m.rib, {.role = FaceRole::EndCap}), 18.0, 18.0, 2.0);
    const ObjectId post = m.addBoss("Post", postSketch, 3.0);

    const ObjectId boxSketch = m.addFixedRectangle("BoxSketch", 200.0, 0.0, 100.0, 60.0);
    const ObjectId box = m.addBoss("Box", boxSketch, 40.0);
    const ObjectId cup = m.add(ShellFeature::create(
        "Cup", {.target = fid(box), .openFaces = {FaceName{box, {.role = FaceRole::EndCap}}}, .thickness = 5_mm}));
    const ObjectId across = m.add(DatumPlane::create(
        "Across", {.kind = DatumPlaneKind::Offset, .base = {.plane = PrincipalPlane::XZ}, .offset = -(30_mm)}));
    std::vector<EntityId> dividerLine;
    auto dividerSketch = bettercad::test::fixedChain("DividerSketch", {{300, 30}, {200, 30}}, dividerLine);
    REQUIRE(dividerSketch->setAttachment(PlaneReference{.object = across}).has_value());
    const ObjectId dividerSketchId = m.doc.addObject(std::move(dividerSketch)).value();
    const ObjectId divider = m.add(RibFeature::create("Divider", {.target = fid(cup),
                                                                  .profile = FaceKindModel::sketchOf(dividerSketchId),
                                                                  .edges = dividerLine,
                                                                  .thickness = 2_mm}));
    Regenerator regenerator;
    CommandHistory history;
    for (const double t : {4.0, 8.0}) {
        CAPTURE(t);
        if (t != 4.0) {
            setValue(history, m.doc, m.thickness, t * units::mm);
        }
        regenerate(regenerator, m.doc);
        const bettercad::test::Cylinder boss{{18.0, 18.0, 20.0 + t / 2.0}, {0.0, 0.0, 1.0}, 2.0, 3.0};
        checkRibbed(regenerator, post,
                    {{{boss.volume(), boss.centre()}}, boss.lower(), boss.upper()});
    }
    // The divider fills the cavity below its line, between the inner walls:
    // x 205..295, z 5..30, y 29..31.
    using bettercad::test::boxPart;
    checkRibbed(regenerator, divider,
                {{boxPart({200, 0, 0}, {300, 60, 40}), boxPart({205, 5, 5}, {295, 55, 40}, -1.0),
                  boxPart({205, 29, 5}, {295, 31, 30})},
                 {200, 0, 0},
                 {300, 60, 40}});
    CHECK_THAT(volumeOf(regenerator, divider), WithinRel(240000.0 - 90.0 * 50.0 * 35.0 + 90.0 * 25.0 * 2.0, kRel));
    // The shell's rim keeps the box's top name.
    CHECK_THAT(namedArea(regenerator, divider, FaceName{box, {.role = FaceRole::EndCap}}),
               WithinRel(6000.0 - 4500.0, kRel));
}

TEST_CASE("RibFeature_FailuresAreStructuredAndAtomic", "[features][rib][p12]") {
    SECTION("sides that are open, and profiles inside the body") {
        RibbedBracketModel m;
        Regenerator regenerator;
        regenerate(regenerator, m.doc);
        const auto before = volumes(regenerator, m.doc);
        CommandHistory history;
        modifyRib(history, m.doc, m.rib, [](RibDefinition& d) { d.flipped = true; });
        RegenerationReport report = requireReport(regenerator, m.doc);
        CHECK(failureOf(report, regenerator, m.rib, ErrorCode::FailedPrecondition) == std::format("Rib: {}", kOpen));
        // The walls grow past the line: it lies inside the bracket.
        REQUIRE(history.undo(m.doc).has_value());
        setValue(history, m.doc, m.wall, 45_mm);
        report = requireReport(regenerator, m.doc);
        CHECK(failureOf(report, regenerator, m.rib, ErrorCode::FailedPrecondition) ==
              "Rib: rib: the profile lies inside the body, so there is nothing to fill");
        CHECK(regenerator.body(m.bracket) != nullptr);
        REQUIRE(history.undo(m.doc).has_value());
        regenerate(regenerator, m.doc);
        checkRibbed(regenerator, m.rib, RibbedBracketModel::shape(10.0, 10.0, 18.0, 22.0));
        CHECK(volumes(regenerator, m.doc).size() == before.size());
    }
    SECTION("profiles that are not open chains of lines, arcs and splines") {
        RibProfilesModel m;
        auto sketch = std::make_unique<sketch::Sketch>("Odd");
        const EntityId circle = bettercad::test::require(sketch->addCircle(Point2D{20_mm, 20_mm}, 5_mm));
        const EntityId helper = bettercad::test::require(sketch->addLine(Point2D{40_mm, 10_mm}, Point2D{10_mm, 40_mm}));
        REQUIRE(sketch->setConstruction(helper, true).has_value());
        const EntityId a = bettercad::test::require(sketch->addLine(Point2D{40_mm, 10_mm}, Point2D{25_mm, 25_mm}));
        const EntityId b = bettercad::test::require(sketch->addLine(Point2D{26_mm, 25_mm}, Point2D{10_mm, 40_mm}));
        const EntityId c = bettercad::test::require(sketch->addLine(Point2D{10_mm, 40_mm}, Point2D{40_mm, 10_mm}));
        const EntityId d = bettercad::test::require(sketch->addLine(Point2D{25_mm, 25_mm}, Point2D{10_mm, 40_mm}));
        const ObjectId odd = m.doc.addObject(std::move(sketch)).value();
        const auto ribOn = [&](const std::string& name, std::vector<EntityId> edges, SketchId profile) {
            return m.add(RibFeature::create(name, {.target = fid(m.bracket),
                                                   .profile = profile,
                                                   .edges = std::move(edges),
                                                   .thickness = 4_mm}));
        };
        const SketchId oddId = FaceKindModel::sketchOf(odd);
        const ObjectId onCircle = ribOn("OnCircle", {circle}, oddId);
        const ObjectId onHelper = ribOn("OnHelper", {helper}, oddId);
        const ObjectId apart = ribOn("Apart", {a, b}, oddId);
        const ObjectId closed = ribOn("Closed", {a, d, c}, oddId);
        const ObjectId missing = ribOn("Missing", {EntityId::fromValue(99)}, oddId);
        const ObjectId notSketch = ribOn("NotSketch", {a}, FaceKindModel::sketchOf(m.bracket));
        Regenerator regenerator;
        const RegenerationReport report = requireReport(regenerator, m.doc);
        CHECK(failureOf(report, regenerator, onCircle, ErrorCode::InvalidArgument) ==
              std::format("OnCircle: the profile edge {} is a circle, not a line, arc or open spline", circle));
        CHECK(failureOf(report, regenerator, onHelper, ErrorCode::InvalidArgument) ==
              std::format("OnHelper: the profile edge {} is construction geometry, which makes no rib", helper));
        CHECK(failureOf(report, regenerator, apart, ErrorCode::InvalidArgument) ==
              std::format("Apart: the profile is not connected: the edges {} and {} do not meet (their nearest ends "
                          "are 1 mm apart)",
                          a, b));
        CHECK(failureOf(report, regenerator, closed, ErrorCode::InvalidArgument) ==
              "Closed: rib: the profile is closed; a rib profile is open");
        CHECK(failureOf(report, regenerator, missing, ErrorCode::NotFound) ==
              "Missing: the profile edge entity:99 does not exist in sketch 'Odd'");
        CHECK(failureOf(report, regenerator, notSketch, ErrorCode::NotFound) ==
              std::format("NotSketch: profile {} is not a sketch in this document", FaceKindModel::sketchOf(m.bracket)));
        // Validation says why for what it can check without geometry.
        const ValidationReport validation = validateDocument(m.doc);
        CHECK_FALSE(validation.valid());
        const auto has = [&](const std::string& text) {
            return std::ranges::any_of(validation.issues,
                                       [&](const ValidationIssue& issue) { return issue.message == text; });
        };
        CHECK(has(std::format("Missing ({}): the profile edge entity:99 does not exist in Odd ({})", missing, odd)));
        CHECK(has(std::format("NotSketch ({}): the profile is Bracket ({}), which is an extrude, not a sketch",
                              notSketch, m.bracket)));
    }
    SECTION("thicknesses of the wrong kind or value") {
        RibbedBracketModel m;
        const ParameterId tilt = m.doc.createParameter("tilt", 10_deg, units::deg).value();
        CommandHistory history;
        modifyRib(history, m.doc, m.rib, [&](RibDefinition& d) { d.thicknessParameter = tilt; });
        Regenerator regenerator;
        RegenerationReport report = requireReport(regenerator, m.doc);
        CHECK(failureOf(report, regenerator, m.rib, ErrorCode::DimensionMismatch).starts_with("Rib: "));
        const ValidationReport validation = validateDocument(m.doc);
        CHECK(std::ranges::any_of(validation.issues, [&](const ValidationIssue& issue) {
            return issue.message == std::format("Rib ({}): the thickness is driven by tilt ({}), which is an angle, "
                                                "not a length",
                                                m.rib, ObjectId{tilt});
        }));
        REQUIRE(history.undo(m.doc).has_value());
        setValue(history, m.doc, m.thickness, -(2_mm));
        report = requireReport(regenerator, m.doc);
        CHECK(failureOf(report, regenerator, m.rib, ErrorCode::InvalidArgument) ==
              "Rib: rib: the rib thickness must be positive and finite, got -2 mm");
    }
    SECTION("names of rib faces, ribs not repeated") {
        RibbedBracketModel m;
        const auto onFace = [&](const std::string& name, FaceSelector face) {
            auto sketch = std::make_unique<sketch::Sketch>(name);
            REQUIRE(sketch->setAttachment(FaceKindModel::faceOf(m.rib, std::move(face))).has_value());
            return m.doc.addObject(std::move(sketch)).value();
        };
        const ObjectId notEdge =
            onFace("NotEdge", {.role = FaceRole::Side, .entity = EntityId::fromValue(1)});
        const ObjectId bottom = onFace("Bottom", {.role = FaceRole::HoleBottom});
        const ObjectId along = onFace("Along", {.role = FaceRole::Side,
                                                .entity = m.ribLines[0],
                                                .along = EntityId::fromValue(1)});
        const ObjectId row = m.add(LinearPatternFeature::create(
            "Row", {.source = fid(m.rib), .first = {.direction = {0.0, 0.0, 1.0}, .count = 2, .spacing = 10_mm}}));
        Regenerator regenerator;
        const RegenerationReport report = requireReport(regenerator, m.doc);
        REQUIRE(report.errors.contains(notEdge));
        CHECK(report.errors.at(notEdge).message ==
              std::format("NotEdge ({}): Rib ({}): entity:1 is not an edge of its profile", notEdge, m.rib));
        REQUIRE(report.errors.contains(bottom));
        CHECK(report.errors.at(bottom).message ==
              std::format("Bottom ({}): Rib ({}) is a rib, which has no hole bottom", bottom, m.rib));
        REQUIRE(report.errors.contains(along));
        CHECK(report.errors.at(along).message ==
              std::format("Along ({}): Rib ({}) is a rib, whose sides are not named by a path edge", along, m.rib));
        REQUIRE(report.errors.contains(row));
        CHECK_THAT(report.errors.at(row).message, ContainsSubstring("a linear pattern cannot repeat a rib"));
        CHECK(regenerator.body(m.rib) != nullptr);
    }
}

TEST_CASE("RibFeature_RegeneratesDeterministically", "[features][rib][regeneration][p12]") {
    const auto namesOf = [](const Regenerator& r, ObjectId id) {
        const auto faces = geometry::listFaces(bodyOf(r, id));
        REQUIRE(faces.has_value());
        std::vector<std::vector<FaceName>> names;
        for (const geometry::FaceInfo& face : *faces) {
            names.push_back(face.names);
        }
        return names;
    };
    RibProfilesModel a;
    RibProfilesModel b;
    Regenerator ra;
    Regenerator rb;
    regenerate(ra, a.doc);
    regenerate(rb, b.doc);
    CHECK(volumes(ra, a.doc) == volumes(rb, b.doc));
    CHECK(equivalent(a.doc, b.doc));
    for (const ObjectId id : {a.arcRib, a.chainRib, a.splineRib, a.shortRib, a.alongRib}) {
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

TEST_CASE("RibFeature_CreationAndEditsAreUndoable", "[features][rib][commands][p12]") {
    RibProfilesModel m;
    Regenerator regenerator;
    regenerate(regenerator, m.doc);
    const Document before = m.doc.clone();
    CommandHistory history;
    // The short line again, 2 mm thick below its plane: the same corner.
    const RibDefinition added{.target = fid(m.bracket),
                              .profile = FaceKindModel::sketchOf(m.shortSketch),
                              .edges = m.shortLines,
                              .thickness = 2_mm,
                              .placement = geometry::RibPlacement::AgainstNormal};
    REQUIRE(history.execute(m.doc, std::make_unique<CreateRibCommand>("Thin", added)).has_value());
    const auto thin = m.doc.findByName("Thin");
    REQUIRE(thin.has_value());
    regenerate(regenerator, m.doc);
    checkRibbed(regenerator, *thin, RibProfilesModel::with(450.0, 20.0, 20.0, 18.0, 20.0));

    modifyRib(history, m.doc, *thin, [](RibDefinition& d) { d.thickness = 3_mm; });
    regenerate(regenerator, m.doc);
    checkRibbed(regenerator, *thin, RibProfilesModel::with(450.0, 20.0, 20.0, 17.0, 20.0));
    // An invalid edit is refused and changes nothing.
    const Document edited = m.doc.clone();
    RibDefinition none = m.doc.findObjectAs<RibFeature>(*thin)->definition();
    none.edges.clear();
    const auto refused = history.execute(m.doc, std::make_unique<ModifyRibCommand>(fid(*thin), none));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().message == "a rib needs one or more profile edges");
    CHECK(equivalent(edited, m.doc));

    REQUIRE(history.undo(m.doc).has_value());
    regenerate(regenerator, m.doc);
    checkRibbed(regenerator, *thin, RibProfilesModel::with(450.0, 20.0, 20.0, 18.0, 20.0));
    REQUIRE(history.undo(m.doc).has_value());
    CHECK(equivalent(before, m.doc));
    REQUIRE(history.redo(m.doc).has_value());
    REQUIRE(history.redo(m.doc).has_value());
    regenerate(regenerator, m.doc);
    checkRibbed(regenerator, *m.doc.findByName("Thin"), RibProfilesModel::with(450.0, 20.0, 20.0, 17.0, 20.0));
}
