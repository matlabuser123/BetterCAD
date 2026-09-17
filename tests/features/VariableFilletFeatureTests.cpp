#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/VariableFilletModels.hpp"

#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/features/FeatureCommands.hpp>
#include <bettercad/features/FilletFeature.hpp>
#include <bettercad/features/LinearPatternFeature.hpp>
#include <bettercad/features/MirrorFeature.hpp>
#include <bettercad/features/Regeneration.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/ResultBodies.hpp>
#include <bettercad/features/Validation.hpp>
#include <bettercad/features/VariableFilletFeature.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <format>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using bettercad::test::ExpectedShell;
using bettercad::test::FaceKindModel;
using bettercad::test::printOf;
using bettercad::test::requireReport;
using bettercad::test::Stations;
using bettercad::test::TaperedBlockModel;
using bettercad::test::Vec3;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::EndsWith;
using Catch::Matchers::StartsWith;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// P12-FEAT-006: variable-radius fillet features. Expected volumes, centres,
// bounds and face areas are computed from the parameters in
// support/VariableFilletModels.hpp.

namespace {

// See VariableFilletTests.cpp: the kernel's adaptive volume integration of
// B-spline fillet faces (1e-9), its bounds of B-spline faces (padded by
// 1e-7 mm), and its default area integration of planes bounded by them
// (1.85e-6 measured).
constexpr double kRelVolume = 1e-9;
constexpr double kTolCentreMm = 1e-7;
constexpr double kTolBounds = 2e-7;
constexpr double kRelFaceArea = 1e-5;

Vec3 mm(const Point3D& p) {
    return {p.x.in(units::mm), p.y.in(units::mm), p.z.in(units::mm)};
}

void checkVec(const Vec3& actual, const Vec3& expected, double tolerance) {
    CHECK_THAT(actual[0], WithinAbs(expected[0], tolerance));
    CHECK_THAT(actual[1], WithinAbs(expected[1], tolerance));
    CHECK_THAT(actual[2], WithinAbs(expected[2], tolerance));
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

void checkShape(const Regenerator& regenerator, ObjectId id, const ExpectedShell& expected) {
    const geometry::Body& body = bodyOf(regenerator, id);
    CHECK(body.isValid());
    CHECK(body.topology().solids == 1);
    const auto props = body.massProperties();
    REQUIRE(props.has_value());
    CHECK_THAT(props->volume.in(units::mm3), WithinRel(expected.volume(), kRelVolume));
    checkVec(mm(props->centerOfMass), expected.centre(), kTolCentreMm);
    const auto box = body.boundingBox();
    REQUIRE(box.has_value());
    checkVec(mm(box->min), expected.lower, kTolBounds);
    checkVec(mm(box->max), expected.upper, kTolBounds);
}

double namedArea(const Regenerator& regenerator, ObjectId id, const FaceName& name) {
    const auto faces = geometry::findNamedFaces(bodyOf(regenerator, id), name);
    REQUIRE(faces.has_value());
    double area = 0.0;
    for (const geometry::FaceInfo& face : *faces) {
        area += face.area.in(units::mm2);
    }
    return area;
}

void setValue(CommandHistory& history, Document& doc, ParameterId parameter, Length value) {
    REQUIRE(history
                .execute(doc, std::make_unique<ModifyParameterCommand>(
                                  parameter, ParameterChanges{.value = DimensionedValue::of(value)}))
                .has_value());
}

void modifyTaper(CommandHistory& history, Document& doc, ObjectId taper,
                 const std::function<void(VariableFilletDefinition&)>& change) {
    VariableFilletDefinition d = doc.findObjectAs<VariableFilletFeature>(taper)->definition();
    change(d);
    REQUIRE(history.execute(doc, std::make_unique<ModifyVariableFilletCommand>(FaceKindModel::featureOf(taper), d))
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

FaceName blockTop(const TaperedBlockModel& m) {
    return FaceName{m.block, FaceSelector{.role = FaceRole::EndCap}};
}

} // namespace

TEST_CASE("VariableFilletFeature_DefinitionsAreValidated", "[features][fillet][variable][p12]") {
    const geometry::EdgeSignature edge = TaperedBlockModel::topEdge(0.0);
    const VariableFilletDefinition good{
        .target = FeatureId::fromValue(1),
        .edges = {{.edge = edge,
                   .stations = {{.position = 0.0, .radius = 3_mm}, {.position = 1.0, .radius = 8_mm}}}}};
    REQUIRE(VariableFilletFeature::create("V", good).has_value());
    const auto message = [&](const std::function<void(VariableFilletDefinition&)>& change) {
        VariableFilletDefinition d = good;
        change(d);
        const auto created = VariableFilletFeature::create("V", d);
        REQUIRE_FALSE(created.has_value());
        CHECK(created.error().code == ErrorCode::InvalidArgument);
        return created.error().message;
    };
    CHECK(message([](auto& d) { d.target = FeatureId{}; }) == "a variable-radius fillet needs a target feature");
    CHECK(message([](auto& d) { d.edges.clear(); }) == "a variable-radius fillet needs at least one edge");
    CHECK(message([](auto& d) { d.edges[0].stations.pop_back(); }) ==
          "edge reference 1: a variable-radius fillet needs two or more radius stations on each edge, got 1");
    CHECK(message([](auto& d) { d.edges[0].stations[1].position = 0.9; }) ==
          "edge reference 1: the last station must be at position 1, got 0.9");
    CHECK(message([](auto& d) { d.edges[0].stations[0].radius = -(1_mm); }) ==
          "edge reference 1, station 1: the radius must be positive and finite, got -1 mm");
    CHECK(message([](auto& d) { d.edges[0].stations[1].radiusParameter = ParameterId{}; }) ==
          "edge reference 1, station 2: the radius parameter ID must be valid");
    CHECK(message([](auto& d) { d.edges.push_back(d.edges[0]); }) ==
          "edge references 1 and 2 refer to the same line through (0, 0, 20) mm along (1, 0, 0)");
    // Literal radii: the law is checked now.
    CHECK_THAT(message([](auto& d) {
                   d.edges[0].stations.insert(d.edges[0].stations.begin() + 1, {.position = 0.5, .radius = 8_mm});
               }),
               StartsWith("edge reference 1: between stations 2 and 3 (8 mm at 0.5, 8 mm at 1) the radius would rise "
                          "to "));

    // A driven radius needs no literal one, and puts the law off until its
    // value is known; a literal radius on the same edge is still checked.
    VariableFilletDefinition driven = good;
    driven.edges[0].stations[0].radius = Length{};
    driven.edges[0].stations[0].radiusParameter = ParameterId::fromValue(7);
    driven.edges[0].stations.insert(driven.edges[0].stations.begin() + 1,
                                    {.position = 0.5, .radius = 8_mm, .radiusParameter = ParameterId::fromValue(9)});
    const auto feature = VariableFilletFeature::create("V", driven);
    REQUIRE(feature.has_value());
    VariableFilletDefinition negative = driven;
    negative.edges[0].stations[2].radius = Length{};
    CHECK(VariableFilletFeature::create("V", negative).error().message ==
          "edge reference 1, station 3: the radius must be positive and finite, got 0 mm");
    // The parameters are dependencies, each once, after the target.
    VariableFilletDefinition twice = driven;
    twice.edges[0].stations[2].radiusParameter = ParameterId::fromValue(7);
    const auto both = VariableFilletFeature::create("V", twice);
    REQUIRE(both.has_value());
    CHECK((*both)->dependencies() ==
          std::vector<ObjectId>{ObjectId::fromValue(1), ObjectId::fromValue(7), ObjectId::fromValue(9)});
    CHECK((*feature)->consumedFeatures() == std::vector<FeatureId>{FeatureId::fromValue(1)});
    CHECK((*feature)->typeName() == "variable_fillet");
    CHECK((*feature)->setDefinition(driven) == false);
    CHECK((*feature)->setDefinition(twice) == true);
    CHECK((*feature)->definition() == twice);
    CHECK_FALSE((*feature)->setDefinition(negative).has_value());
    CHECK((*feature)->definition() == twice);
}

TEST_CASE("VariableFilletFeature_FollowsItsBlockAndRadii",
          "[features][fillet][variable][regeneration][p12][acceptance]") {
    TaperedBlockModel m;
    Regenerator regenerator;
    const auto check = [&](double w, double d, double lo, double hi) {
        CAPTURE(w, d, lo, hi);
        checkShape(regenerator, m.taper, TaperedBlockModel::shape(w, d, lo, hi));
        // Block's top keeps its name, less both strips.
        CHECK_THAT(namedArea(regenerator, m.taper, blockTop(m)),
                   WithinRel(TaperedBlockModel::topArea(w, d, lo, hi), kRelFaceArea));
        // One face more for each rounded edge.
        CHECK(bodyOf(regenerator, m.taper).topology().faces == 8);
    };
    regenerate(regenerator, m.doc);
    check(100.0, 50.0, 3.0, 8.0);
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.taper});
    const DocumentGraph graph = buildDependencyGraph(m.doc);
    const auto deps = graph.graph.dependenciesOf(m.taper);
    for (const ObjectId id : {m.block, ObjectId{m.low}, ObjectId{m.high}}) {
        CHECK(std::ranges::find(deps, id) != deps.end());
    }
    const bettercad::test::BodyPrint first = printOf(bodyOf(regenerator, m.taper));

    // The radii change: only the fillet regenerates, and undo restores it
    // bit for bit.
    CommandHistory history;
    setValue(history, m.doc, m.low, 5_mm);
    CHECK(regenerate(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.taper});
    check(100.0, 50.0, 5.0, 8.0);
    setValue(history, m.doc, m.high, 12_mm);
    regenerate(regenerator, m.doc);
    check(100.0, 50.0, 5.0, 12.0);
    // Decreasing along the edge.
    setValue(history, m.doc, m.low, 15_mm);
    setValue(history, m.doc, m.high, 2_mm);
    regenerate(regenerator, m.doc);
    check(100.0, 50.0, 15.0, 2.0);
    for (int i = 0; i < 4; ++i) {
        REQUIRE(history.undo(m.doc).has_value());
    }
    regenerate(regenerator, m.doc);
    CHECK(printOf(bodyOf(regenerator, m.taper)) == first);

    // The block grows: the stations stay at the same fractions of the
    // longer edges, and the law stretches with them.
    setValue(history, m.doc, m.width, 150_mm);
    regenerate(regenerator, m.doc);
    check(150.0, 50.0, 3.0, 8.0);
    setValue(history, m.doc, m.width, 60_mm);
    regenerate(regenerator, m.doc);
    check(60.0, 50.0, 3.0, 8.0);
    REQUIRE(history.undo(m.doc).has_value());
    REQUIRE(history.undo(m.doc).has_value());
    regenerate(regenerator, m.doc);
    // Undoing a sketch-driving parameter re-solves the sketch, which restores
    // it to rounding (P12-SKETCH-003): checked against the analytic values;
    // a fresh regeneration of a copy gives the same bits.
    check(100.0, 50.0, 3.0, 8.0);
    Document copy = m.doc.clone();
    Regenerator again;
    regenerate(again, copy);
    CHECK(printOf(bodyOf(again, m.taper)) == printOf(bodyOf(regenerator, m.taper)));
    // Redo brings the wider block back.
    REQUIRE(history.redo(m.doc).has_value());
    regenerate(regenerator, m.doc);
    check(150.0, 50.0, 3.0, 8.0);
}

TEST_CASE("VariableFilletFeature_RegeneratesDeterministically", "[features][fillet][variable][regeneration][p12]") {
    TaperedBlockModel a;
    TaperedBlockModel b;
    Regenerator ra;
    Regenerator rb;
    regenerate(ra, a.doc);
    regenerate(rb, b.doc);
    const bettercad::test::BodyPrint print = printOf(bodyOf(ra, a.taper));
    CHECK(print.valid);
    // A fresh document, the same bits.
    CHECK(printOf(bodyOf(rb, b.taper)) == print);
    CHECK(equivalent(a.doc, b.doc));
    // A second pass rebuilds nothing; full rebuilds give the same bits.
    CHECK(requireReport(ra, a.doc).regenerated.empty());
    for (int i = 0; i < 3; ++i) {
        auto again = ra.regenerateAll(a.doc);
        REQUIRE(again.has_value());
        REQUIRE(again->succeeded());
        CHECK(printOf(bodyOf(ra, a.taper)) == print);
    }
    // A new regenerator on the same document.
    Regenerator rc;
    regenerate(rc, a.doc);
    CHECK(printOf(bodyOf(rc, a.taper)) == print);
}

TEST_CASE("VariableFilletFeature_CreationAndEditsAreUndoable", "[features][fillet][variable][commands][p12]") {
    TaperedBlockModel m;
    Regenerator regenerator;
    regenerate(regenerator, m.doc);
    const Document before = m.doc.clone();
    const bettercad::test::BodyPrint tapered = printOf(bodyOf(regenerator, m.taper));
    CommandHistory history;
    // The bottom front edge, 2 mm to 4 mm.
    const VariableFilletDefinition added{
        .target = fid(m.taper),
        .edges = {{.edge = geometry::lineSignature(Point3D{}, Direction3D::unitX()),
                   .stations = {{.position = 0.0, .radius = 2_mm}, {.position = 1.0, .radius = 4_mm}}}}};
    REQUIRE(history.execute(m.doc, std::make_unique<CreateVariableFilletCommand>("Sole", added)).has_value());
    const auto found = m.doc.findByName("Sole");
    REQUIRE(found.has_value());
    const ObjectId sole = *found;
    regenerate(regenerator, m.doc);
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{sole});
    ExpectedShell shape = TaperedBlockModel::shape(100.0, 50.0, 3.0, 8.0);
    shape.parts.push_back(
        bettercad::test::boxEdge({0, 0, 0}, {1, 0, 0}, 100.0, {0, bettercad::test::kHalfRoot2,
                                                               bettercad::test::kHalfRoot2},
                                 {{0.0, 2.0}, {1.0, 4.0}})
            .part());
    checkShape(regenerator, sole, shape);

    // Editing the stations, and undoing the edit.
    modifyTaper(history, m.doc, m.taper, [](VariableFilletDefinition& d) {
        d.edges[1].stations = {{.position = 0.0, .radius = 6_mm}, {.position = 1.0, .radius = 3_mm}};
    });
    regenerate(regenerator, m.doc);
    const auto back = [](double w, double d, Stations s) {
        return bettercad::test::boxEdge({0, d, TaperedBlockModel::kHeight}, {1, 0, 0}, w,
                                        {0, -bettercad::test::kHalfRoot2, -bettercad::test::kHalfRoot2},
                                        std::move(s));
    };
    checkShape(regenerator, m.taper,
               {{bettercad::test::boxPart({0, 0, 0}, {100, 50, 20}), TaperedBlockModel::frontEdge(100, 3, 8).part(),
                 back(100, 50, {{0.0, 6.0}, {1.0, 3.0}}).part()},
                {0, 0, 0},
                {100, 50, 20}});
    REQUIRE(history.undo(m.doc).has_value());
    regenerate(regenerator, m.doc);
    CHECK(printOf(bodyOf(regenerator, m.taper)) == tapered);
    REQUIRE(history.undo(m.doc).has_value());
    CHECK(equivalent(m.doc, before));
    regenerate(regenerator, m.doc);
    CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.taper});
    REQUIRE(history.redo(m.doc).has_value());
    regenerate(regenerator, m.doc);
    checkShape(regenerator, sole, shape);
    // An invalid edit is refused and changes nothing.
    VariableFilletDefinition bad = m.doc.findObjectAs<VariableFilletFeature>(m.taper)->definition();
    bad.edges[0].stations[1].position = 2.0;
    const Document current = m.doc.clone();
    const auto refused = history.execute(m.doc, std::make_unique<ModifyVariableFilletCommand>(fid(m.taper), bad));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == ErrorCode::InvalidArgument);
    CHECK(equivalent(m.doc, current));
}

TEST_CASE("VariableFilletFeature_FailuresAreStructuredAndAtomic", "[features][fillet][variable][p12]") {
    SECTION("radii the faces cannot take, laws that leave their stations, invalid radii") {
        TaperedBlockModel m;
        Regenerator regenerator;
        regenerate(regenerator, m.doc);
        const bettercad::test::BodyPrint good = printOf(bodyOf(regenerator, m.taper));
        CommandHistory history;
        // 25 mm on the 20 mm front face.
        setValue(history, m.doc, m.high, 25_mm);
        RegenerationReport report = requireReport(regenerator, m.doc);
        CHECK_THAT(failureOf(report, regenerator, m.taper, ErrorCode::FailedPrecondition),
                   StartsWith("Taper: variable-radius fillet: edge reference 1 (line through (0, 0, 20) mm along "
                              "(1, 0, 0)) does not fit: its variable-radius fillet needs 25 mm on a face next to the "
                              "edge, which leaves only 20 mm"));
        CHECK(regenerator.body(m.block) != nullptr);
        // A zero radius.
        REQUIRE(history.undo(m.doc).has_value());
        setValue(history, m.doc, m.low, 0_mm);
        report = requireReport(regenerator, m.doc);
        CHECK(failureOf(report, regenerator, m.taper, ErrorCode::InvalidArgument) ==
              "Taper: variable-radius fillet: edge reference 1, station 1: the radius must be positive and finite, "
              "got 0 mm");
        REQUIRE(history.undo(m.doc).has_value());
        // A middle station of 5 mm: fine up to 8 mm, but at 5 mm at both of
        // the last stations the law would bulge above them.
        modifyTaper(history, m.doc, m.taper, [](VariableFilletDefinition& d) {
            d.edges[0].stations.insert(d.edges[0].stations.begin() + 1, {.position = 0.5, .radius = 5_mm});
        });
        regenerate(regenerator, m.doc);
        setValue(history, m.doc, m.high, 5_mm);
        report = requireReport(regenerator, m.doc);
        const std::string bulge = failureOf(report, regenerator, m.taper, ErrorCode::InvalidArgument);
        CHECK_THAT(bulge, StartsWith("Taper: variable-radius fillet: edge reference 1: between stations 2 and 3 (5 mm "
                                     "at 0.5, 5 mm at 1) the radius would rise to 5.21659 mm at 0.68"));
        CHECK_THAT(bulge, EndsWith("; a variable radius must stay between the radii of the stations on either side"));
        for (int i = 0; i < 2; ++i) {
            REQUIRE(history.undo(m.doc).has_value());
        }
        regenerate(regenerator, m.doc);
        CHECK(printOf(bodyOf(regenerator, m.taper)) == good);
    }

    SECTION("edges that move, meet or continue") {
        TaperedBlockModel m;
        Regenerator regenerator;
        CommandHistory history;
        // A deeper block moves the back edge off its line: its reference
        // matches nothing, and no other edge is taken.
        setValue(history, m.doc, m.depth, 60_mm);
        RegenerationReport report = requireReport(regenerator, m.doc);
        CHECK(failureOf(report, regenerator, m.taper, ErrorCode::NotFound) ==
              "Taper: variable-radius fillet: edge reference 2 (line through (0, 50, 20) mm along (1, 0, 0)) matches "
              "no edge of the body");
        REQUIRE(history.undo(m.doc).has_value());
        // A vertical edge meeting the front edge.
        modifyTaper(history, m.doc, m.taper, [](VariableFilletDefinition& d) {
            d.edges.push_back(
                {.edge = geometry::lineSignature(Point3D{100_mm, Length{}, Length{}}, Direction3D::unitZ()),
                 .stations = {{.position = 0.0, .radius = 2_mm}, {.position = 1.0, .radius = 3_mm}}});
        });
        report = requireReport(regenerator, m.doc);
        CHECK(failureOf(report, regenerator, m.taper, ErrorCode::FailedPrecondition) ==
              "Taper: variable-radius fillet: edge reference 3 (line through (100, 0, 0) mm along (0, 0, 1)) meets "
              "edge reference 1 (line through (0, 0, 20) mm along (1, 0, 0)) at a vertex; a variable-radius fillet "
              "rounds edges that meet no other edge it rounds");
        REQUIRE(history.undo(m.doc).has_value());
        regenerate(regenerator, m.doc);

        // A constant round of that vertical edge first: the front edge then
        // runs on into the round.
        const ObjectId round = m.add(FilletFeature::create(
            "Round", {.target = fid(m.block),
                      .edges = {geometry::lineSignature(Point3D{100_mm, Length{}, Length{}}, Direction3D::unitZ())},
                      .radius = 5_mm}));
        modifyTaper(history, m.doc, m.taper, [&](VariableFilletDefinition& d) { d.target = fid(round); });
        report = requireReport(regenerator, m.doc);
        CHECK(failureOf(report, regenerator, m.taper, ErrorCode::FailedPrecondition) ==
              "Taper: variable-radius fillet: edge reference 1 (line through (0, 0, 20) mm along (1, 0, 0)) continues "
              "smoothly into 2 other edge(s); a variable-radius fillet rounds single edges only");
        CHECK(regenerator.body(round) != nullptr);
    }

    SECTION("radii driven by parameters of the wrong kind") {
        TaperedBlockModel m;
        const ParameterId tilt = m.doc.createParameter("tilt", 10_deg, units::deg).value();
        CommandHistory history;
        modifyTaper(history, m.doc, m.taper,
                    [&](VariableFilletDefinition& d) { d.edges[0].stations[0].radiusParameter = tilt; });
        Regenerator regenerator;
        const RegenerationReport report = requireReport(regenerator, m.doc);
        CHECK_THAT(failureOf(report, regenerator, m.taper, ErrorCode::DimensionMismatch), StartsWith("Taper: "));
        const ValidationReport validation = validateDocument(m.doc);
        CHECK(std::ranges::any_of(validation.issues, [&](const ValidationIssue& issue) {
            return issue.message == std::format("Taper ({}): the radius of edge reference 1, station 1 is driven by "
                                                "tilt ({}), which is an angle, not a length",
                                                m.taper, ObjectId{tilt});
        }));
    }

    SECTION("no copies, no named faces") {
        TaperedBlockModel m;
        const ObjectId row = m.add(LinearPatternFeature::create(
            "Row", {.source = fid(m.taper), .first = {.direction = {0.0, 1.0, 0.0}, .count = 2, .spacing = 60_mm}}));
        const ObjectId image = m.add(MirrorFeature::create(
            "Image", {.source = fid(m.taper), .plane = {.origin = Point3D{}, .normal = Vector3D{0.0, 1.0, 0.0}}}));
        auto sketch = std::make_unique<sketch::Sketch>("OnFillet");
        REQUIRE(sketch->setAttachment(FaceKindModel::faceOf(m.taper, {.role = FaceRole::EndCap})).has_value());
        const ObjectId onFillet = m.doc.addObject(std::move(sketch)).value();
        Regenerator regenerator;
        const RegenerationReport report = requireReport(regenerator, m.doc);
        REQUIRE(report.errors.contains(row));
        CHECK_THAT(report.errors.at(row).message, EndsWith("a linear pattern cannot repeat a variable-radius fillet"));
        REQUIRE(report.errors.contains(image));
        CHECK_THAT(report.errors.at(image).message, EndsWith("a mirror cannot repeat a variable-radius fillet"));
        REQUIRE(report.errors.contains(onFillet));
        CHECK(report.errors.at(onFillet).message ==
              std::format("OnFillet ({}): Taper ({}) is a variable fillet, whose faces are not named (extrudes, "
                          "revolves, sweeps, lofts, holes, chamfers and ribs name theirs)",
                          onFillet, m.taper));
        CHECK(regenerator.body(m.taper) != nullptr);
    }
}
