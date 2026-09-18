#include "FeatureTestSupport.hpp"
#include "TestHelpers.hpp"
#include "core/geometry/GeometryTestSupport.hpp"
#include "support/BracketModel.hpp"
#include "support/SweepModels.hpp"

#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Booleans.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/features/FaceReferences.hpp>
#include <bettercad/features/FeatureCommands.hpp>
#include <bettercad/features/Regeneration.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/SweepFeature.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <utility>
#include <cmath>
#include <cstdint>
#include <memory>
#include <format>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using bettercad::test::describe;
using bettercad::test::GuidedBarModel;
using bettercad::test::kPositionToleranceMm;
using bettercad::test::requireReport;
using bettercad::test::SpatialBarModel;
using bettercad::test::TwistedBarModel;
using bettercad::test::volumeMm3;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// P12-SWEEP-001 at the feature level: paths of several runs, twist and guide
// curves as parametric definitions. The expected numbers are the analytic
// ones (the section's area times the path's length, and the extents a
// section turned by theta reaches); nothing is read back from the sweep to
// decide what the sweep should have done.

namespace {

constexpr double pi = std::numbers::pi;
constexpr double kRel = bettercad::test::kRelTight;
// A guided sweep follows a curve fitted through samples of the path; the
// kernel probe measured its worst departure from the analytic volume at
// 1.9e-6, and positions at a few microns.
constexpr double kRelGuided = 1e-5;
constexpr double kGuidedPositionMm = 2e-3;

const geometry::Body& requireBody(const Regenerator& regenerator, ObjectId feature) {
    const geometry::Body* body = regenerator.body(feature);
    REQUIRE(body != nullptr);
    return *body;
}

std::array<double, 6> boxMm(const geometry::Body& body) {
    const BoundingBox3D box = body.boundingBox().value();
    return {box.min.x.in(units::mm), box.min.y.in(units::mm), box.min.z.in(units::mm),
            box.max.x.in(units::mm), box.max.y.in(units::mm), box.max.z.in(units::mm)};
}

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

/// Regenerates; exactly @p feature must fail, keeping no body.
Error requireFailure(Regenerator& regenerator, Document& doc, ObjectId feature) {
    const RegenerationReport report = requireReport(regenerator, doc);
    INFO(describe(report));
    REQUIRE(report.failed == std::vector<ObjectId>{feature});
    CHECK(regenerator.body(feature) == nullptr);
    return report.errors.at(feature);
}

/// The message a definition is refused with outright.
std::string refusal(const SweepDefinition& definition) {
    auto feature = SweepFeature::create("Bad", definition);
    REQUIRE_FALSE(feature.has_value());
    CHECK(feature.error().code == ErrorCode::InvalidArgument);
    return feature.error().message;
}

} // namespace

// ---------------------------------------------------------------------------
// Non-planar paths
// ---------------------------------------------------------------------------

TEST_CASE("SpatialSweep_RunsFromSeveralSketchesMakeOnePath", "[sweep][features][p12][acceptance]") {
    SpatialBarModel m;
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());

    // 4 x 4 mm of section along 40 + 40 + 40 mm of path.
    CHECK_THAT(volumeMm3(regenerator, m.bar), WithinRel(SpatialBarModel::expectedVolume(), kRel));
    const geometry::Body& body = requireBody(regenerator, m.bar);
    CHECK(body.isValid());
    CHECK(body.topology().solids == 1);
    // It reaches 2 mm either side of each run, and stops at its two caps.
    const auto box = boxMm(body);
    CHECK_THAT(box[0], WithinAbs(-2.0, kPositionToleranceMm));
    CHECK_THAT(box[1], WithinAbs(-2.0, kPositionToleranceMm));
    CHECK_THAT(box[2], WithinAbs(0.0, kPositionToleranceMm));
    CHECK_THAT(box[3], WithinAbs(42.0, kPositionToleranceMm));
    CHECK_THAT(box[4], WithinAbs(40.0, kPositionToleranceMm));
    CHECK_THAT(box[5], WithinAbs(42.0, kPositionToleranceMm));
}

TEST_CASE("SpatialSweep_DependsOnEveryRunsSketch", "[sweep][features][p12]") {
    SpatialBarModel m;
    const SweepFeature* sweep = m.doc.findObjectAs<SweepFeature>(m.bar);
    REQUIRE(sweep != nullptr);
    // The profile, then the path's runs in the order of travel.
    CHECK(sweep->dependencies() == std::vector<ObjectId>{m.profile, m.rise, m.cross, m.turn});

    // The guide has none here, and the twist parameter is absent too.
    CHECK_FALSE(m.definition().guide.has_value());
    CHECK_FALSE(m.definition().twistParameter.has_value());
}

TEST_CASE("SpatialSweep_SidesAreNamedByTheEdgeTheyRunAlong", "[sweep][features][references][p12]") {
    // A side face names the profile entity that swept it and the path edge
    // it ran along, whichever run's sketch drew that edge.
    SpatialBarModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const geometry::Body& body = requireBody(regenerator, m.bar);

    const sketch::Sketch* profile = m.doc.findObjectAs<sketch::Sketch>(m.profile);
    REQUIRE(profile != nullptr);
    const std::array<std::pair<ObjectId, EntityId>, 3> runs{
        {{m.rise, m.riseLine}, {m.cross, m.crossLine}, {m.turn, m.turnLine}}};
    std::size_t named = 0;
    for (const auto& [runSketch, edge] : runs) {
        for (const sketch::Entity& entity : profile->entities()) {
            if (entity.type() != sketch::EntityType::Line) {
                continue;
            }
            const FaceName name{m.bar,
                                {.role = FaceRole::Side,
                                 .entity = entity.id,
                                 .along = edge,
                                 .alongSketch = SketchId::fromValue(runSketch.value())}};
            const auto found = geometry::findNamedFaces(body, name);
            REQUIRE(found.has_value());
            INFO("run " << runSketch << ", edge " << edge);
            CHECK(found->size() == 1); // exactly one face, not one per run
            named += found->size();
        }
    }
    // Four profile lines along each of the three path edges, each named
    // once: the sketch tells the runs apart even though their edges share
    // an ID (entity IDs are numbered per sketch).
    CHECK(named == 12);
    CHECK(m.riseLine == m.crossLine); // the ambiguity the sketch resolves

    // The same edge attributed to another run's sketch. addRectangle adds
    // its corner points before its lines, so take the first line.
    EntityId first{};
    for (const sketch::Entity& entity : profile->entities()) {
        if (entity.type() == sketch::EntityType::Line) {
            first = entity.id;
            break;
        }
    }
    REQUIRE(first.isValid());
    const auto crossed = geometry::findNamedFaces(
        body, FaceName{m.bar, {.role = FaceRole::Side,
                               .entity = first,
                               .along = m.riseLine,
                               .alongSketch = SketchId::fromValue(m.cross.value())}});
    REQUIRE(crossed.has_value());
    // It does name one: Cross's own edge has the same ID. Its face is the
    // one on that run, not the one on Rise.
    CHECK(crossed->size() == 1);

    // An edge of no run of this path names nothing at all.
    const FaceName stranger{m.bar, {.role = FaceRole::Side,
                                    .entity = first,
                                    .along = EntityId::fromValue(9999),
                                    .alongSketch = SketchId::fromValue(m.rise.value())}};
    const auto missing = geometry::findNamedFaces(body, stranger);
    REQUIRE(missing.has_value());
    CHECK(missing->empty());
}

TEST_CASE("SpatialSweep_RejectsRunsThatDoNotMeet", "[sweep][features][p12]") {
    SpatialBarModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());

    SECTION("a gap between two runs") {
        // Draw the second run on a plane 5 mm above where the first ends.
        auto stray = std::make_unique<sketch::Sketch>(
            "Stray", Frame3D::create(Point3D{0_mm, 0_mm, 45_mm}, Direction3D::unitY(), Direction3D::unitX()).value());
        const EntityId line = stray->addLine(Point2D{}, Point2D{40_mm, 0_mm}).value();
        const ObjectId strayId = m.doc.addObject(std::move(stray)).value();
        SweepDefinition d = m.definition();
        d.path.runs[0] = SweepPathRun{.sketch = SketchId::fromValue(strayId.value()), .edges = {line}};
        m.setDefinition(d);
        const Error error = requireFailure(regenerator, m.doc, m.bar);
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK_THAT(error.message, ContainsSubstring("the path is not connected"));
        CHECK(regenerator.body(m.bar) == nullptr);
    }
    SECTION("a run whose sketch is gone") {
        REQUIRE(m.doc.removeObject(m.turn).has_value());
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        CHECK_FALSE(report.succeeded());
        CHECK(regenerator.body(m.bar) == nullptr);
    }
}

TEST_CASE("SpatialSweep_RefusesAnInvalidRunOutright", "[sweep][features][p12]") {
    SpatialBarModel m;
    SweepDefinition d = m.definition();
    SECTION("a run without a sketch") {
        d.path.runs[0].sketch = SketchId{};
        CHECK(refusal(d) == "path run 2 needs a sketch");
    }
    SECTION("a run without edges") {
        d.path.runs[1].edges.clear();
        CHECK(refusal(d) == "path run 3 needs at least one edge");
    }
    SECTION("an edge listed twice within one run") {
        // Across runs the same ID means different edges: entity IDs are
        // numbered per sketch, so only a repeat inside one run is a mistake.
        d.path.runs[1].edges = {m.turnLine, m.turnLine};
        CHECK(refusal(d) == std::format("{} is listed twice in the path", m.turnLine));
    }
}

// ---------------------------------------------------------------------------
// Twist
// ---------------------------------------------------------------------------

TEST_CASE("SpatialSweep_TwistTurnsTheSectionByTheAngleGiven", "[sweep][features][p12][acceptance]") {
    TwistedBarModel m;
    Regenerator regenerator;
    const auto build = [&](Angle twist) {
        REQUIRE(m.doc.setParameterValue(m.twist, twist).has_value());
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        REQUIRE(report.succeeded());
        return boxMm(requireBody(regenerator, m.twisted));
    };

    SECTION("no twist is the bar it always was") {
        const auto box = build(0_deg);
        CHECK_THAT(volumeMm3(regenerator, m.twisted), WithinRel(TwistedBarModel::expectedVolume(), kRel));
        CHECK_THAT(box[3], WithinAbs(4.0, kPositionToleranceMm));
        CHECK_THAT(box[4], WithinAbs(1.0, kPositionToleranceMm));
    }
    SECTION("a quarter turn") {
        const auto box = build(90_deg);
        CHECK_THAT(volumeMm3(regenerator, m.twisted), WithinRel(TwistedBarModel::expectedVolume(), kRelGuided));
        // phi covers [0, 90 deg], so both extents reach sqrt(a^2 + b^2).
        CHECK_THAT(box[3], WithinAbs(std::hypot(4.0, 1.0), kGuidedPositionMm));
        CHECK_THAT(box[4], WithinAbs(std::hypot(4.0, 1.0), kGuidedPositionMm));
    }
    SECTION("the other way round") {
        const auto box = build(-(90_deg));
        CHECK_THAT(volumeMm3(regenerator, m.twisted), WithinRel(TwistedBarModel::expectedVolume(), kRelGuided));
        CHECK_THAT(box[1], WithinAbs(-std::hypot(4.0, 1.0), kGuidedPositionMm));
    }
    SECTION("an eighth turn reaches the radius one way only") {
        const auto box = build(45_deg);
        CHECK_THAT(box[3], WithinAbs(std::hypot(4.0, 1.0), kGuidedPositionMm));
        CHECK_THAT(box[4], WithinAbs(TwistedBarModel::acrossY(pi / 4.0), kGuidedPositionMm));
    }
    SECTION("a half turn") {
        const auto box = build(180_deg);
        CHECK_THAT(volumeMm3(regenerator, m.twisted), WithinRel(TwistedBarModel::expectedVolume(), kRelGuided));
        CHECK_THAT(box[3], WithinAbs(std::hypot(4.0, 1.0), kGuidedPositionMm));
    }
    SECTION("a full turn is a full turn, not none") {
        const auto box = build(360_deg);
        CHECK_THAT(volumeMm3(regenerator, m.twisted), WithinRel(TwistedBarModel::expectedVolume(), kRelGuided));
        CHECK_THAT(box[4], WithinAbs(std::hypot(4.0, 1.0), kGuidedPositionMm));
        CHECK(box[4] > 2.0); // the untwisted bar reaches only 1 mm
    }
}

TEST_CASE("SpatialSweep_TwistFollowsItsParameterAndItsPath", "[sweep][features][p12][acceptance]") {
    TwistedBarModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK_THAT(volumeMm3(regenerator, m.twisted), WithinRel(TwistedBarModel::expectedVolume(100.0), kRelGuided));

    // A longer path: the same turn spread over more length, so the same
    // extents and a volume that grows with the length.
    REQUIRE(m.doc.setParameterValue(m.length, 150_mm).has_value());
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());
    CHECK_THAT(volumeMm3(regenerator, m.twisted), WithinRel(TwistedBarModel::expectedVolume(150.0), kRelGuided));
    const auto box = boxMm(requireBody(regenerator, m.twisted));
    CHECK_THAT(box[5], WithinAbs(150.0, kGuidedPositionMm));
    CHECK_THAT(box[3], WithinAbs(std::hypot(4.0, 1.0), kGuidedPositionMm));

    // The twist parameter is a dependency, and drives the feature.
    const SweepFeature* sweep = m.doc.findObjectAs<SweepFeature>(m.twisted);
    REQUIRE(sweep != nullptr);
    const std::vector<ObjectId> dependencies = sweep->dependencies();
    CHECK(std::ranges::find(dependencies, ObjectId{m.twist}) != dependencies.end());
}

TEST_CASE("SpatialSweep_RejectsAnInvalidTwist", "[sweep][features][p12]") {
    TwistedBarModel m;
    SweepDefinition d = m.definition();
    d.twistParameter.reset();

    SECTION("a twist that is not a number") {
        d.twist = Angle::fromSi(std::numeric_limits<double>::quiet_NaN());
        CHECK_THAT(refusal(d), ContainsSubstring("the twist must be finite"));
    }
    SECTION("a parameter of the wrong kind") {
        d.twistParameter = m.length; // a length, not an angle
        m.setDefinition(d);
        Regenerator regenerator;
        const Error error = requireFailure(regenerator, m.doc, m.twisted);
        CHECK_THAT(error.message, ContainsSubstring("has dimension length, not angle"));
    }
    SECTION("a parameter that does not exist") {
        REQUIRE(m.doc.removeParameter(m.twist).has_value());
        Regenerator regenerator;
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        CHECK_FALSE(report.succeeded());
        CHECK(regenerator.body(m.twisted) == nullptr);
    }
}

// ---------------------------------------------------------------------------
// Guide curves
// ---------------------------------------------------------------------------

TEST_CASE("SpatialSweep_AGuideCurveCarriesTheSection", "[sweep][features][p12][acceptance]") {
    GuidedBarModel m;
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());

    CHECK_THAT(volumeMm3(regenerator, m.guided), WithinRel(GuidedBarModel::expectedVolume(), kRelGuided));
    // The guide's offset turns from +X to +Y: a quarter turn, so the section
    // reaches its own radius both ways, as a 90 degree twist would.
    const auto box = boxMm(requireBody(regenerator, m.guided));
    CHECK_THAT(box[3], WithinAbs(std::hypot(4.0, 1.0), kGuidedPositionMm));
    CHECK_THAT(box[4], WithinAbs(std::hypot(4.0, 1.0), kGuidedPositionMm));
    CHECK(box[4] > 2.0); // not the untwisted bar

    // The guide's sketch is a dependency: moving it rebuilds the sweep.
    const SweepFeature* sweep = m.doc.findObjectAs<SweepFeature>(m.guided);
    REQUIRE(sweep != nullptr);
    CHECK(sweep->dependencies() == std::vector<ObjectId>{m.profile, m.rise, m.lead});
}

TEST_CASE("SpatialSweep_RefusesAGuideAndATwistTogether", "[sweep][features][p12]") {
    GuidedBarModel m;
    SweepDefinition d = m.definition();
    d.twist = 90_deg;
    CHECK(refusal(d) == "a sweep takes a twist or a guide curve, not both: a guide already says how the section "
                        "turns");
    d.twist = Angle{};
    d.twistParameter = ParameterId::fromValue(1);
    CHECK(refusal(d) == "a sweep takes a twist or a guide curve, not both: a guide already says how the section "
                        "turns");
}

TEST_CASE("SpatialSweep_RejectsAGuideThatCannotBeUsed", "[sweep][features][p12]") {
    GuidedBarModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());

    SECTION("a guide whose sketch is gone") {
        REQUIRE(m.doc.removeObject(m.lead).has_value());
        const RegenerationReport report = requireReport(regenerator, m.doc);
        INFO(describe(report));
        CHECK_FALSE(report.succeeded());
        CHECK(regenerator.body(m.guided) == nullptr);
    }
    SECTION("a guide edge that is not in its sketch") {
        SweepDefinition d = m.definition();
        d.guide->edges = {EntityId::fromValue(9999)};
        m.setDefinition(d);
        const Error error = requireFailure(regenerator, m.doc, m.guided);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK_THAT(error.message, ContainsSubstring("does not exist"));
    }
    SECTION("a guide in the profile's own sketch") {
        SweepDefinition d = m.definition();
        d.guide->sketch = d.profile;
        CHECK(refusal(d) == "the guide must be in another sketch than the profile");
    }
}

// ---------------------------------------------------------------------------
// Atomicity, undo and determinism
// ---------------------------------------------------------------------------

TEST_CASE("SpatialSweep_FailuresKeepNoBodyAndRecover", "[sweep][features][p12][acceptance]") {
    TwistedBarModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const double before = volumeMm3(regenerator, m.twisted);

    // A path of no length cannot be swept.
    REQUIRE(m.doc.setParameterValue(m.length, 0_mm).has_value());
    const RegenerationReport failed = requireReport(regenerator, m.doc);
    INFO(describe(failed));
    CHECK_FALSE(failed.succeeded());
    CHECK(regenerator.body(m.twisted) == nullptr);

    REQUIRE(m.doc.setParameterValue(m.length, 100_mm).has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(bits(volumeMm3(regenerator, m.twisted)) == bits(before));
}

TEST_CASE("SpatialSweep_UndoRedoRestoresTheDefinitionAndTheBody", "[sweep][features][undo][p12][acceptance]") {
    TwistedBarModel m;
    CommandHistory history;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const double quarter = volumeMm3(regenerator, m.twisted);
    const SweepDefinition original = m.definition();

    // Replace the driven twist with a literal half turn.
    SweepDefinition half = original;
    half.twistParameter.reset();
    half.twist = 180_deg;
    REQUIRE(history.execute(m.doc, std::make_unique<ModifySweepCommand>(FeatureId::fromValue(m.twisted.value()),
                                                                        half))
                .has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(m.definition().twist == 180_deg);
    CHECK_FALSE(m.definition().twistParameter.has_value());

    REQUIRE(history.undo(m.doc).has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(m.definition() == original);
    CHECK(bits(volumeMm3(regenerator, m.twisted)) == bits(quarter));

    REQUIRE(history.redo(m.doc).has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(m.definition().twist == 180_deg);
}

TEST_CASE("SpatialSweep_RegenerationIsDeterministic", "[sweep][features][p12][determinism]") {
    const auto fingerprint = [](Document& doc, ObjectId feature, Regenerator& regenerator) {
        REQUIRE(requireReport(regenerator, doc).succeeded());
        const geometry::Body* body = regenerator.body(feature);
        REQUIRE(body != nullptr);
        const auto properties = body->massProperties().value();
        return std::vector<double>{properties.volume.si(), properties.surfaceArea.si(),
                                   properties.centerOfMass.x.si(), properties.centerOfMass.y.si(),
                                   properties.centerOfMass.z.si()};
    };

    SECTION("a spatial path") {
        auto first = std::make_unique<SpatialBarModel>();
        Regenerator a;
        const std::vector<double> once = fingerprint(first->doc, first->bar, a);
        Regenerator again;
        CHECK(fingerprint(first->doc, first->bar, again) == once);
        auto second = std::make_unique<SpatialBarModel>();
        Regenerator b;
        CHECK(fingerprint(second->doc, second->bar, b) == once);
    }
    SECTION("a twisted sweep") {
        auto first = std::make_unique<TwistedBarModel>();
        Regenerator a;
        const std::vector<double> once = fingerprint(first->doc, first->twisted, a);
        Regenerator again;
        CHECK(fingerprint(first->doc, first->twisted, again) == once);
        auto second = std::make_unique<TwistedBarModel>();
        Regenerator b;
        CHECK(fingerprint(second->doc, second->twisted, b) == once);
    }
    SECTION("a guided sweep") {
        auto first = std::make_unique<GuidedBarModel>();
        Regenerator a;
        const std::vector<double> once = fingerprint(first->doc, first->guided, a);
        Regenerator again;
        CHECK(fingerprint(first->doc, first->guided, again) == once);
        auto second = std::make_unique<GuidedBarModel>();
        Regenerator b;
        CHECK(fingerprint(second->doc, second->guided, b) == once);
    }
}
