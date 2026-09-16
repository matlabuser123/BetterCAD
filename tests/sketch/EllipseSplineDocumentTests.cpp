#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/TestFiles.hpp"
#include "support/occt/StepReadBack.hpp"

#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/LoftFeature.hpp>
#include <bettercad/features/Profiles.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/RevolveFeature.hpp>
#include <bettercad/features/SweepFeature.hpp>
#include <bettercad/features/Validation.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/io/ModelExport.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/sketch/SketchCommands.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <bit>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using namespace bettercad::sketch;
using bettercad::test::errorCode;
using bettercad::test::readFile;
using bettercad::test::readStepFile;
using bettercad::test::require;
using bettercad::test::requireReport;
using bettercad::test::TempDir;
using bettercad::test::volumeMm3;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// P12-SKETCH-002: ellipses and splines in documents: profiles, features,
// regeneration, validation, persistence, export and undo.

namespace {

constexpr double pi = std::numbers::pi;
// Solids bounded by planes and elliptic cylinders: the kernel's volumes are
// exact to rounding (as for P3's circular cylinders).
constexpr double kRelExact = 1e-12;
// Solids with B-spline faces: the kernel integrates them numerically. The
// measured deviation is recorded in docs/verification/P12-SKETCH-002.
constexpr double kRelSpline = 1e-9;

std::vector<geometry::PlanarRegion> requireRegions(const Sketch& sketch) {
    auto regions = extractRegions(sketch);
    INFO((regions ? std::string{} : regions.error().message));
    REQUIRE(regions.has_value());
    return *regions;
}

double areaMm2(const geometry::PlanarRegion& region) {
    return geometry::regionArea(region).in(units::mm2);
}

SketchId sketchIdOf(ObjectId id) {
    return SketchId::fromValue(id.value());
}

// Three sketches, each extruded by `depth`:
//
//   semi_major, depth -> EllipseSketch: ellipse a = semi_major, b = 12 mm,
//                        centred at the origin, fully constrained
//   depth             -> SplineSketch: periodic quadratic spline on the
//                        square (100, 0)..(160, 60), poles fixed; area 5/6 of
//                        the square = 3000 mm^2
//   depth             -> DSketch: a line and a cubic spline arch sharing its
//                        ends (area 3/5 * 60 * 45 = 1620 mm^2), with an
//                        elliptic hole a = 10, b = 5
//
// IDs: semi_major 1, depth 2, EllipseSketch 3, EllipseExtrude 4, SplineSketch
// 5, SplineExtrude 6, DSketch 7, DExtrude 8.
struct CurvedModel {
    Document doc{"Curved"};
    ParameterId semiMajor, depth;
    ObjectId ellipseSketch, ellipseExtrude, splineSketch, splineExtrude, dSketch, dExtrude;

    CurvedModel() {
        semiMajor = doc.createParameter("semi_major", 30_mm, units::mm).value();
        depth = doc.createParameter("depth", 10_mm, units::mm).value();
        ellipseSketch = doc.addObject(makeEllipse()).value();
        ellipseExtrude = addExtrude("EllipseExtrude", ellipseSketch);
        splineSketch = doc.addObject(makeSpline()).value();
        splineExtrude = addExtrude("SplineExtrude", splineSketch);
        dSketch = doc.addObject(makeD()).value();
        dExtrude = addExtrude("DExtrude", dSketch);
    }

    ObjectId addExtrude(const std::string& name, ObjectId sketch) {
        auto feature = ExtrudeFeature::create(name, {.profile = sketchIdOf(sketch), .depthParameter = depth});
        REQUIRE(feature.has_value());
        return doc.addObject(std::move(*feature)).value();
    }

    [[nodiscard]] std::unique_ptr<Sketch> makeEllipse() const {
        auto s = std::make_unique<Sketch>("EllipseSketch");
        const EntityId ellipse = require(s->addEllipse(Point2D{0_mm, 0_mm}, 25_mm, 10_mm, 5_deg));
        const EllipseEntity e = std::get<EllipseEntity>(s->findEntity(ellipse)->geometry);
        require(s->addFixed(e.center));
        require(s->addHorizontal(e.center, e.xVertex));
        REQUIRE(s->setConstraintParameter(require(s->addDistance(e.center, e.xVertex, 1_mm)), semiMajor).has_value());
        require(s->addDistance(e.center, e.yVertex, 12_mm));
        return s;
    }

    [[nodiscard]] static std::unique_ptr<Sketch> makeSpline() {
        auto s = std::make_unique<Sketch>("SplineSketch");
        const EntityId spline = require(s->addSpline(
            {Point2D{100_mm, 0_mm}, Point2D{160_mm, 0_mm}, Point2D{160_mm, 60_mm}, Point2D{100_mm, 60_mm}}, 2, true));
        for (const EntityId pole : std::get<SplineEntity>(s->findEntity(spline)->geometry).poles) {
            require(s->addFixed(pole));
        }
        return s;
    }

    [[nodiscard]] static std::unique_ptr<Sketch> makeD() {
        auto s = std::make_unique<Sketch>("DSketch");
        const EntityId start = require(s->addPoint(Point2D{200_mm, 0_mm}));
        const EntityId end = require(s->addPoint(Point2D{260_mm, 0_mm}));
        const EntityId up = require(s->addPoint(Point2D{260_mm, 45_mm}));
        const EntityId over = require(s->addPoint(Point2D{200_mm, 45_mm}));
        require(s->addLine(start, end));
        require(s->addSpline(std::vector<EntityId>{end, up, over, start}, 3, false));
        require(s->addEllipse(Point2D{230_mm, 15_mm}, 10_mm, 5_mm));
        return s;
    }

    static double ellipseVolume(double a, double h) { return pi * a * 12.0 * h; }
    static double splineVolume(double h) { return 3000.0 * h; }
    static double dVolume(double h) { return (1620.0 - pi * 50.0) * h; }
};

void setLength(CommandHistory& history, Document& doc, ParameterId id, Length value) {
    REQUIRE(history
                .execute(doc, std::make_unique<ModifyParameterCommand>(
                                  id, ParameterChanges{.value = DimensionedValue::of(value)}))
                .has_value());
}

void checkVolumes(const Regenerator& regenerator, const CurvedModel& m, double a, double h) {
    CAPTURE(a, h);
    CHECK_THAT(volumeMm3(regenerator, m.ellipseExtrude), WithinRel(CurvedModel::ellipseVolume(a, h), kRelExact));
    CHECK_THAT(volumeMm3(regenerator, m.splineExtrude), WithinRel(CurvedModel::splineVolume(h), kRelSpline));
    CHECK_THAT(volumeMm3(regenerator, m.dExtrude), WithinRel(CurvedModel::dVolume(h), kRelSpline));
}

} // namespace

// --- Profiles ------------------------------------------------------------------------------------

TEST_CASE("SketchProfiles_EllipsesAndSplinesFormRegions", "[sketch][profiles][p12]") {
    SECTION("an ellipse inside a rectangle is a hole") {
        Sketch s("Plate");
        test::addRectangle(s, 0_mm, 0_mm, 100_mm, 60_mm);
        require(s.addEllipse(Point2D{50_mm, 30_mm}, 20_mm, 10_mm, 40_deg));
        const EntityId helper = require(s.addEllipse(Point2D{150_mm, 30_mm}, 20_mm, 10_mm));
        REQUIRE(s.setConstruction(helper, true).has_value());
        const auto regions = requireRegions(s);
        REQUIRE(regions.size() == 1);
        REQUIRE(regions[0].holes.size() == 1);
        CHECK(std::holds_alternative<geometry::EllipseSegment2D>(regions[0].holes[0].segments.front()));
        CHECK_THAT(areaMm2(regions[0]), WithinRel(6000.0 - pi * 200.0, 1e-12));
    }
    SECTION("containment follows the curve, not its control polygon") {
        // The spline passes the corner (60, 60) at (52.5, 52.5). A circle at
        // (57, 57) is inside the control square but outside the curve: a
        // region of its own. A circle at (48, 48) is inside the curve: a hole.
        Sketch s("Nested");
        require(s.addSpline({Point2D{0_mm, 0_mm}, Point2D{60_mm, 0_mm}, Point2D{60_mm, 60_mm}, Point2D{0_mm, 60_mm}},
                            2, true));
        require(s.addCircle(Point2D{57_mm, 57_mm}, 1_mm));
        require(s.addCircle(Point2D{48_mm, 48_mm}, 1_mm));
        const auto regions = requireRegions(s);
        REQUIRE(regions.size() == 2);
        CHECK(std::holds_alternative<geometry::SplineSegment2D>(regions[0].outer.segments.front()));
        REQUIRE(regions[0].holes.size() == 1);
        CHECK_THAT(areaMm2(regions[0]), WithinRel(3000.0 - pi, 1e-12));
        CHECK(regions[1].holes.empty());
        CHECK_THAT(areaMm2(regions[1]), WithinRel(pi, 1e-12));
        // Around them all, an ellipse: the spline and the outer circle become
        // its holes, and the inner circle, inside the spline, a region again.
        require(s.addEllipse(Point2D{30_mm, 30_mm}, 80_mm, 70_mm));
        const auto nested = requireRegions(s);
        REQUIRE(nested.size() == 2);
        CHECK(nested[0].holes.empty());
        CHECK_THAT(areaMm2(nested[0]), WithinRel(pi, 1e-12));
        CHECK(std::holds_alternative<geometry::EllipseSegment2D>(nested[1].outer.segments.front()));
        CHECK(nested[1].holes.size() == 2);
        CHECK_THAT(areaMm2(nested[1]), WithinRel(pi * 80.0 * 70.0 - 3000.0 - pi, 1e-12));
    }
    SECTION("an open spline closes a loop with a line") {
        const auto d = CurvedModel::makeD();
        const auto regions = requireRegions(*d);
        REQUIRE(regions.size() == 1);
        REQUIRE(regions[0].outer.segments.size() == 2);
        CHECK_THAT(areaMm2(regions[0]), WithinRel(1620.0 - pi * 50.0, 1e-12));
    }
    SECTION("an open spline ending where it starts is a loop") {
        // A teardrop: for a closed cubic Bezier from the origin the area is
        // 3/20 cross(P1, P2) = 360 mm^2, and the centroid is at x = 120/7.
        Sketch s("Teardrop");
        require(s.addSpline({Point2D{0_mm, 0_mm}, Point2D{40_mm, -30_mm}, Point2D{40_mm, 30_mm}, Point2D{0_mm, 0_mm}},
                            3, false));
        const auto regions = requireRegions(s);
        REQUIRE(regions.size() == 1);
        CHECK_THAT(areaMm2(regions[0]), WithinRel(360.0, 1e-12));
        CHECK_THAT(geometry::regionCentroid(regions[0]).x.in(units::mm), WithinRel(120.0 / 7.0, 1e-12));
    }
    SECTION("an open spline without a neighbour is an open profile") {
        Sketch s("Open");
        require(s.addSpline({Point2D{0_mm, 0_mm}, Point2D{40_mm, -30_mm}, Point2D{40_mm, 30_mm}}, 2, false));
        const auto regions = extractRegions(s);
        REQUIRE_FALSE(regions.has_value());
        CHECK(regions.error().code == ErrorCode::FailedPrecondition);
        CHECK_THAT(regions.error().message, ContainsSubstring("the profile is open: an edge ends at (0, 0) mm"));
    }
    SECTION("a spline whose poles were moved onto one point is invalid") {
        Sketch s("Collapsed");
        const EntityId spline =
            require(s.addSpline({Point2D{0_mm, 0_mm}, Point2D{40_mm, -30_mm}, Point2D{40_mm, 30_mm}}, 2, true));
        for (const EntityId pole : std::get<SplineEntity>(s.findEntity(spline)->geometry).poles) {
            REQUIRE(s.setPointPosition(pole, Point2D{5_mm, 5_mm}).has_value());
        }
        const auto regions = extractRegions(s);
        REQUIRE_FALSE(regions.has_value());
        CHECK(regions.error().code == ErrorCode::InvalidArgument);
        CHECK(regions.error().message == "entity:4 is not a valid spline: a spline's poles all coincide");
    }
}

// --- Features ------------------------------------------------------------------------------------

TEST_CASE("SketchFeatures_DrivenEllipseAndSplinesExtrudeToTheirVolumes", "[sketch][regeneration][p12][acceptance]") {
    CurvedModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    checkVolumes(regenerator, m, 30.0, 10.0);

    CommandHistory history;
    setLength(history, m.doc, m.semiMajor, 40_mm);
    setLength(history, m.doc, m.depth, 15_mm);
    const RegenerationReport second = requireReport(regenerator, m.doc);
    REQUIRE(second.succeeded());
    CHECK(second.regenerated == std::vector<ObjectId>{m.ellipseSketch, m.ellipseExtrude, m.splineExtrude, m.dExtrude});
    checkVolumes(regenerator, m, 40.0, 15.0);

    REQUIRE(history.undo(m.doc).has_value());
    REQUIRE(history.undo(m.doc).has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    checkVolumes(regenerator, m, 30.0, 10.0);

    // The elliptic prism: two planar ends and one elliptic side.
    const geometry::Body* body = regenerator.body(m.ellipseExtrude);
    REQUIRE(body != nullptr);
    CHECK(body->topology().faces == 3);
    const auto box = body->boundingBox();
    REQUIRE(box.has_value());
    CHECK_THAT(box->max.x.in(units::mm), WithinAbs(30.0, 1e-7));
    CHECK_THAT(box->max.y.in(units::mm), WithinAbs(12.0, 1e-7));
    CHECK_THAT(box->max.z.in(units::mm), WithinAbs(10.0, 1e-7));
}

TEST_CASE("SketchFeatures_RevolvedAndSweptEllipsesFollowPappus", "[sketch][regeneration][p12]") {
    Document doc{"Pappus"};
    auto profile = std::make_unique<Sketch>("Profile");
    require(profile->addEllipse(Point2D{50_mm, 0_mm}, 10_mm, 6_mm));
    const ObjectId profileId = doc.addObject(std::move(profile)).value();
    auto revolve = RevolveFeature::create("Ring", {.profile = sketchIdOf(profileId), .axis = RevolveAxis::sketchY()});
    REQUIRE(revolve.has_value());
    const ObjectId ring = doc.addObject(std::move(*revolve)).value();

    auto tubeProfile = std::make_unique<Sketch>("TubeProfile");
    require(tubeProfile->addEllipse(Point2D{0_mm, 0_mm}, 10_mm, 6_mm, 20_deg));
    const ObjectId tubeProfileId = doc.addObject(std::move(tubeProfile)).value();
    auto path = std::make_unique<Sketch>("Path", Frame3D::xz());
    const EntityId line = require(path->addLine(Point2D{0_mm, 0_mm}, Point2D{0_mm, 80_mm}));
    const ObjectId pathId = doc.addObject(std::move(path)).value();
    auto sweep = SweepFeature::create(
        "Tube", {.profile = sketchIdOf(tubeProfileId), .path = {.sketch = sketchIdOf(pathId), .edges = {line}}});
    REQUIRE(sweep.has_value());
    const ObjectId tube = doc.addObject(std::move(*sweep)).value();

    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, doc);
    INFO((report.errors.empty() ? std::string{} : report.errors.begin()->second.message));
    REQUIRE(report.succeeded());
    CHECK_THAT(volumeMm3(regenerator, ring), WithinRel(2.0 * pi * 50.0 * pi * 60.0, 1e-9));
    CHECK_THAT(volumeMm3(regenerator, tube), WithinRel(pi * 60.0 * 80.0, 1e-9));
}

TEST_CASE("SketchFeatures_SplinePathsAndEllipseLoftsAreRefused", "[sketch][regeneration][validation][p12]") {
    Document doc{"Refused"};
    auto circle = std::make_unique<Sketch>("Circle");
    require(circle->addCircle(Point2D{0_mm, 0_mm}, 5_mm));
    const ObjectId circleId = doc.addObject(std::move(circle)).value();
    auto path = std::make_unique<Sketch>("Path", Frame3D::xz());
    const EntityId curve =
        require(path->addSpline({Point2D{0_mm, 0_mm}, Point2D{0_mm, 30_mm}, Point2D{20_mm, 60_mm}}, 2, false));
    const ObjectId pathId = doc.addObject(std::move(path)).value();
    auto sweep = SweepFeature::create(
        "Sweep", {.profile = sketchIdOf(circleId), .path = {.sketch = sketchIdOf(pathId), .edges = {curve}}});
    REQUIRE(sweep.has_value());
    const ObjectId sweepId = doc.addObject(std::move(*sweep)).value();

    auto bottom = std::make_unique<Sketch>("Bottom");
    require(bottom->addEllipse(Point2D{0_mm, 0_mm}, 10_mm, 6_mm));
    const ObjectId bottomId = doc.addObject(std::move(bottom)).value();
    auto loft = LoftFeature::create("Loft", {.sections = {{.sketch = sketchIdOf(bottomId), .offset = 0_mm},
                                                          {.sketch = sketchIdOf(circleId), .offset = 30_mm}}});
    REQUIRE(loft.has_value());
    const ObjectId loftId = doc.addObject(std::move(*loft)).value();

    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, doc);
    CHECK(report.failed == std::vector<ObjectId>{sweepId, loftId});
    CHECK(report.errors.at(sweepId).code == ErrorCode::InvalidArgument);
    CHECK(report.errors.at(sweepId).message ==
          "Sweep: the path edge entity:4 is a spline, not a line, arc or circle");
    CHECK_THAT(report.errors.at(loftId).message,
               ContainsSubstring("section 1 has an ellipse; loft sections are made of lines, arcs and circles"));

    const ValidationReport validation = validateDocument(doc);
    CHECK_FALSE(validation.valid());
    bool pathIssue = false;
    for (const ValidationIssue& issue : validation.issues) {
        pathIssue = pathIssue ||
                    issue.message.find("the path edge entity:4 is a spline, not a line, arc or circle") !=
                        std::string::npos;
    }
    CHECK(pathIssue);
}

TEST_CASE("SketchDocument_ValidationAcceptsEllipsesAndSplines", "[sketch][validation][p12]") {
    CurvedModel m;
    const ValidationReport report = validateDocument(m.doc);
    INFO((report.issues.empty() ? std::string{} : report.issues.front().message));
    CHECK(report.valid());
    CHECK(report.count(ValidationCheck::DocumentConsistency, Severity::Error) == 0);
    CHECK(report.bodies.size() == 3);
}

// --- Undo, persistence, export -----------------------------------------------------------------------

TEST_CASE("SketchCommands_EllipseAndSplineEditsUndoAndRedo", "[sketch][commands][p12]") {
    Document doc{"Edits"};
    CommandHistory history;
    auto create = std::make_unique<CreateSketchCommand>("Sketch1");
    CreateSketchCommand* created = create.get();
    REQUIRE(history.execute(doc, std::move(create)).has_value());
    const ObjectId id{created->sketchId()};
    const Sketch empty = *doc.findObjectAs<Sketch>(id);

    EntityId spline;
    REQUIRE(history
                .execute(doc, std::make_unique<ModifySketchCommand>(
                                  created->sketchId(), "Add curves", [&](Sketch& s) -> Result<void> {
                                      require(s.addEllipse(Point2D{0_mm, 0_mm}, 20_mm, 10_mm));
                                      spline = require(s.addSpline(
                                          {Point2D{30_mm, 0_mm}, Point2D{60_mm, 0_mm}, Point2D{60_mm, 30_mm}}, 2,
                                          true));
                                      return {};
                                  }))
                .has_value());
    const Sketch withCurves = *doc.findObjectAs<Sketch>(id);
    CHECK(withCurves.entityCount() == 8);
    REQUIRE(history.undo(doc).has_value());
    CHECK(doc.findObjectAs<Sketch>(id)->contentEquals(empty));
    REQUIRE(history.redo(doc).has_value());
    CHECK(doc.findObjectAs<Sketch>(id)->contentEquals(withCurves));
    CHECK(doc.findObjectAs<Sketch>(id)->entityType(spline) == EntityType::Spline);
}

TEST_CASE("SketchFile_EllipsesAndSplinesRoundTrip", "[sketch][io][p12]") {
    TempDir dir;
    const auto path = dir.path() / "curved.bcad";
    CurvedModel m;
    Regenerator before;
    REQUIRE(requireReport(before, m.doc).succeeded());
    const Document expected = m.doc.clone();
    REQUIRE(io::saveDocument(m.doc, path).has_value());

    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    CHECK(equivalent(expected, *loaded));
    Regenerator after;
    REQUIRE(requireReport(after, *loaded).succeeded());
    for (const ObjectId feature : {m.ellipseExtrude, m.splineExtrude, m.dExtrude}) {
        CAPTURE(feature);
        CHECK(std::bit_cast<std::uint64_t>(volumeMm3(after, feature)) ==
              std::bit_cast<std::uint64_t>(volumeMm3(before, feature)));
    }

    const std::string text = readFile(path);
    CHECK_THAT(text, ContainsSubstring("\"type\": \"ellipse\","));
    CHECK_THAT(text, ContainsSubstring("\"x_vertex\": 2,"));
    CHECK_THAT(text, ContainsSubstring("\"y_vertex\": 3,"));
    CHECK_THAT(text, ContainsSubstring("\"type\": \"bspline\","));
    CHECK_THAT(text, ContainsSubstring("\"degree\": 3,"));
    CHECK_THAT(text, ContainsSubstring("\"periodic\": true,"));
    CHECK(io::documentToJson(*loaded).value() == text);
}

TEST_CASE("SketchFile_MalformedEllipsesAndSplinesAreRejectedWithThePath", "[sketch][io][p12]") {
    CurvedModel m;
    const std::string good = io::documentToJson(m.doc).value();
    const auto loadError = [&](std::string_view from, std::string_view to) {
        std::string text = good;
        const auto pos = text.find(from);
        REQUIRE(pos != std::string::npos);
        text.replace(pos, from.size(), to);
        const auto loaded = io::documentFromJson(text);
        REQUIRE_FALSE(loaded.has_value());
        return loaded.error().message;
    };
    // The file name of a spline is "bspline"; "spline" stays unknown.
    CHECK_THAT(loadError("\"type\": \"bspline\"", "\"type\": \"spline\""), ContainsSubstring("unknown type 'spline'"));
    CHECK_THAT(loadError("\"degree\": 2", "\"degree\": 9"),
               ContainsSubstring(".data.entities[4]: a spline's degree must be 2 to 5, got 9"));
    CHECK_THAT(loadError("\"degree\": 2", "\"degree\": 2.5"), ContainsSubstring(".degree: expected an integer"));
    CHECK_THAT(loadError("\"periodic\": true", "\"periodic\": 1"), ContainsSubstring(".periodic: expected true or false"));
    CHECK_THAT(loadError("\"x_vertex\": 2", "\"x_vertex\": \"two\""), ContainsSubstring(".x_vertex: expected"));
    CHECK_THAT(loadError("\"x_vertex\": 2", "\"x_vertex\": 99"),
               ContainsSubstring("entity:99 does not exist in this sketch"));
    CHECK_THAT(loadError("\"x_vertex\": 2", "\"major\": 2"), ContainsSubstring("major"));
    CHECK_THAT(loadError("\"poles\": [", "\"poles\": [\"a\", "), ContainsSubstring(".poles[0]: expected"));
}

TEST_CASE("SketchExport_EllipsesAndSplinesReadBackFromStep", "[sketch][io][step][p12]") {
    TempDir dir;
    const auto path = dir.path() / "curved.step";
    CurvedModel m;
    const auto summary = io::exportStep(m.doc, path);
    INFO((summary ? std::string{} : summary.error().message));
    REQUIRE(summary.has_value());
    CHECK(summary->bodies.size() == 3);
    const auto contents = readStepFile(path);
    REQUIRE(contents.has_value());
    CHECK(contents->valid);
    CHECK(contents->solids == 3);
    const double total =
        CurvedModel::ellipseVolume(30.0, 10.0) + CurvedModel::splineVolume(10.0) + CurvedModel::dVolume(10.0);
    CHECK_THAT(contents->volumeMm3, WithinRel(total, 1e-9));
}
