#include "FeatureTestSupport.hpp"
#include "TestHelpers.hpp"
#include "core/geometry/GeometryTestSupport.hpp"
#include "support/BracketModel.hpp"
#include "support/ConfigurationModels.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/Regeneration.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/document/ParameterExpressions.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/Validation.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <bit>
#include <chrono>
#include <format>
#include <ostream>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using bettercad::test::BoxFamilyModel;
using bettercad::test::describe;
using bettercad::test::kPositionToleranceMm;
using bettercad::test::requireReport;
using bettercad::test::volumeMm3;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// P12-PARAM-002 driving real geometry. Every expected number is a closed
// form written out here, never read back from the model.

namespace {

// The box's faces are planes and its edges are lines, so the kernel's volume
// is exact to rounding.
constexpr double kRel = bettercad::test::kRelTight;

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

struct Fingerprint {
    double volume = 0.0;
    double area = 0.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    friend bool operator==(const Fingerprint&, const Fingerprint&) = default;
    friend std::ostream& operator<<(std::ostream& os, const Fingerprint& f) {
        return os << std::format("V={:.17g} A={:.17g} c=({:.17g}, {:.17g}, {:.17g})", f.volume, f.area, f.x,
                                 f.y, f.z);
    }
};

/// Volume, area and centroid by bits: what "the same solid" means here.
Fingerprint fingerprint(const Regenerator& regenerator, ObjectId feature) {
    const geometry::Body* body = regenerator.body(feature);
    REQUIRE(body != nullptr);
    const auto properties = body->massProperties().value();
    return {properties.volume.si(), properties.surfaceArea.si(), properties.centerOfMass.x.si(),
            properties.centerOfMass.y.si(), properties.centerOfMass.z.si()};
}

/// Regenerates and checks the box against W^3 / 8, its centroid and its
/// bounds -- all computed from W alone.
void checkBox(Regenerator& regenerator, BoxFamilyModel& m, double widthMm) {
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    REQUIRE(report.succeeded());

    const double expected = BoxFamilyModel::expectedVolumeMm3(widthMm);
    INFO("width " << widthMm << " mm, expected " << expected << " mm^3");
    CHECK_THAT(volumeMm3(regenerator, m.box), WithinRel(expected, kRel));

    const geometry::Body* body = regenerator.body(m.box);
    REQUIRE(body != nullptr);
    const auto centroid = BoxFamilyModel::expectedCentroidMm(widthMm);
    const auto properties = body->massProperties().value();
    CHECK_THAT(properties.centerOfMass.x.in(units::mm), WithinAbs(centroid[0], kPositionToleranceMm));
    CHECK_THAT(properties.centerOfMass.y.in(units::mm), WithinAbs(centroid[1], kPositionToleranceMm));
    CHECK_THAT(properties.centerOfMass.z.in(units::mm), WithinAbs(centroid[2], kPositionToleranceMm));

    const auto box = body->boundingBox();
    REQUIRE(box.has_value());
    CHECK_THAT(box->max.x.in(units::mm), WithinAbs(widthMm, kPositionToleranceMm));
    CHECK_THAT(box->max.y.in(units::mm), WithinAbs(widthMm / 2.0, kPositionToleranceMm));
    CHECK_THAT(box->max.z.in(units::mm), WithinAbs(widthMm / 4.0, kPositionToleranceMm));
}

} // namespace

TEST_CASE("Configurations_BuildTheWholeFamilyFromOneParameter", "[configurations][features][p12][acceptance]") {
    // The acceptance case: three configurations set `width` only, and each
    // gives the volume W^3 / 8 that the equations imply.
    BoxFamilyModel m;
    Regenerator regenerator;

    SECTION("the base configuration") {
        checkBox(regenerator, m, 100.0);
    }
    SECTION("Small") {
        m.activate(m.small);
        checkBox(regenerator, m, 40.0);
    }
    SECTION("Medium") {
        m.activate(m.medium);
        checkBox(regenerator, m, 80.0);
    }
    SECTION("Large") {
        m.activate(m.large);
        checkBox(regenerator, m, 160.0);
    }
}

TEST_CASE("Configurations_DerivedParametersAreNeverDuplicated", "[configurations][features][p12][acceptance]") {
    // Each configuration overrides exactly one parameter; `height` and
    // `depth` are equations and belong to no configuration.
    BoxFamilyModel m;
    for (const ConfigurationId id : {m.small, m.medium, m.large}) {
        const Configuration* configuration = m.doc.configurations().find(id);
        REQUIRE(configuration != nullptr);
        INFO("configuration " << configuration->name());
        CHECK(configuration->size() == 1);
        CHECK(configuration->overrides(m.width));
        CHECK_FALSE(configuration->overrides(m.height));
        CHECK_FALSE(configuration->overrides(m.depth));
    }

    // And the derived values really do follow: at Large, height = 80 and
    // depth = 40, neither of them written down anywhere.
    Regenerator regenerator;
    m.activate(m.large);
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK_THAT(m.doc.parameters().find(m.height)->displayValue(), WithinRel(80.0, kRel));
    CHECK_THAT(m.doc.parameters().find(m.depth)->displayValue(), WithinRel(40.0, kRel));
}

TEST_CASE("Configurations_SwitchingThereAndBackRestoresTheSameModel",
          "[configurations][features][p12][acceptance]") {
    // Small -> Large -> Small must give Small back.
    //
    // What is EXACT: the parameter values in force, and everything computed
    // from them -- the overrides are stored numbers and the equations are
    // arithmetic, so they reproduce bit for bit.
    //
    // What is not: the solid, to its last bit or two. The sketch solver
    // starts from the geometry the sketch currently holds, so the point it
    // converges to depends on where it started. That is the solver's
    // behaviour, not the configuration's, and it predates this milestone:
    // setting `width` to 160 mm and back to 100 mm with no configuration
    // anywhere gives a volume of 0.00012500000000000003 m^3 where building
    // 100 mm directly gives 0.000125 -- one unit in the last place. It is
    // measured here at 1.5e-15 relative over four round trips, it does not
    // grow with the number of switches, and kRelTight (1e-12) is three
    // orders clear of it.
    BoxFamilyModel there;
    Regenerator a;
    there.activate(there.small);
    REQUIRE(requireReport(a, there.doc).succeeded());
    const Fingerprint first = fingerprint(a, there.box);
    const double exact = BoxFamilyModel::expectedVolumeMm3(40.0);

    there.activate(there.large);
    REQUIRE(requireReport(a, there.doc).succeeded());
    there.activate(there.small);
    REQUIRE(requireReport(a, there.doc).succeeded());

    // The values in force are exactly Small's again, by bits.
    CHECK(bits(there.doc.effectiveParameterValue(there.width)->siValue) == bits(0.04));
    CHECK(bits(there.doc.parameters().find(there.height)->siValue()) == bits(0.02));
    CHECK(bits(there.doc.parameters().find(there.depth)->siValue()) == bits(0.01));
    CHECK(there.doc.activeConfiguration() == there.small);

    const Fingerprint back = fingerprint(a, there.box);
    INFO("first " << first << "\nback  " << back);
    CHECK_THAT(back.volume, WithinRel(first.volume, kRel));
    CHECK_THAT(back.volume * 1e9, WithinRel(exact, kRel));
    CHECK_THAT(back.area, WithinRel(first.area, kRel));
    CHECK_THAT(back.x, WithinRel(first.x, kRel));
    CHECK_THAT(back.y, WithinRel(first.y, kRel));
    CHECK_THAT(back.z, WithinRel(first.z, kRel));

    // A document built from scratch and put straight into Small agrees with
    // the switched one to the same bound.
    BoxFamilyModel fresh;
    Regenerator b;
    fresh.activate(fresh.small);
    REQUIRE(requireReport(b, fresh.doc).succeeded());
    const Fingerprint clean = fingerprint(b, fresh.box);
    CHECK_THAT(clean.volume, WithinRel(back.volume, kRel));
    CHECK_THAT(clean.volume * 1e9, WithinRel(exact, kRel));

    // And it does not accumulate: four more round trips stay inside the same
    // bound of the first result, rather than walking away from it.
    for (int i = 0; i < 4; ++i) {
        there.activate(there.medium);
        REQUIRE(requireReport(a, there.doc).succeeded());
        there.activate(there.small);
        REQUIRE(requireReport(a, there.doc).succeeded());
        const Fingerprint trip = fingerprint(a, there.box);
        CAPTURE(i, trip.volume, first.volume);
        CHECK_THAT(trip.volume, WithinRel(first.volume, kRel));
        CHECK_THAT(trip.volume * 1e9, WithinRel(exact, kRel));
        // The topology is identical every time, not merely close.
        CHECK(a.body(there.box)->topology() == b.body(fresh.box)->topology());
    }
}

TEST_CASE("Configurations_TheValuesInForceAreExactlyReproducible",
          "[configurations][features][p12][acceptance][determinism]") {
    // The part of a configuration that BetterCAD computes itself -- the
    // overrides and the equations that follow from them -- is exact, so it
    // is asserted by bits rather than by tolerance. Building the family in
    // any order gives the same numbers.
    const auto valuesUnder = [](ConfigurationId BoxFamilyModel::*which, bool wander) {
        auto m = std::make_unique<BoxFamilyModel>();
        Regenerator regenerator;
        if (wander) {
            for (const auto id : {m->large, m->small, m->medium}) {
                m->activate(id);
                REQUIRE(requireReport(regenerator, m->doc).succeeded());
            }
        }
        m->activate((*m).*which);
        REQUIRE(requireReport(regenerator, m->doc).succeeded());
        return std::vector<std::uint64_t>{bits(m->doc.effectiveParameterValue(m->width)->siValue),
                                          bits(m->doc.parameters().find(m->height)->siValue()),
                                          bits(m->doc.parameters().find(m->depth)->siValue())};
    };
    for (const auto which : {&BoxFamilyModel::small, &BoxFamilyModel::medium, &BoxFamilyModel::large}) {
        CHECK(valuesUnder(which, /*wander=*/false) == valuesUnder(which, /*wander=*/true));
    }
    // Small is 40 mm, so height = 20 mm and depth = 10 mm, exactly.
    CHECK(valuesUnder(&BoxFamilyModel::small, false) ==
          std::vector<std::uint64_t>{bits(0.04), bits(0.02), bits(0.01)});
}

TEST_CASE("Configurations_RegenerationIsDeterministic", "[configurations][features][p12][determinism]") {
    // The same configuration regenerated twice, and built twice from
    // scratch, gives the same solid by bits.
    const auto build = [](ConfigurationId BoxFamilyModel::*which) {
        auto m = std::make_unique<BoxFamilyModel>();
        Regenerator regenerator;
        m->activate((*m).*which);
        REQUIRE(requireReport(regenerator, m->doc).succeeded());
        const Fingerprint once = fingerprint(regenerator, m->box);
        REQUIRE(requireReport(regenerator, m->doc).succeeded());
        CHECK(fingerprint(regenerator, m->box) == once);
        return once;
    };
    CHECK(build(&BoxFamilyModel::medium) == build(&BoxFamilyModel::medium));
    CHECK_FALSE(build(&BoxFamilyModel::small) == build(&BoxFamilyModel::large));
}

TEST_CASE("Configurations_ReachDrivenSketchConstraintsAndFeatureParameters",
          "[configurations][features][p12][acceptance]") {
    // The override has to reach two different consumers: the sketch
    // constraints that `width` and `height` drive, and the extrude's depth
    // parameter. The bounds prove the sketch followed, the depth proves the
    // feature did.
    BoxFamilyModel m;
    Regenerator regenerator;
    m.activate(m.large);
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const geometry::Body* body = regenerator.body(m.box);
    REQUIRE(body != nullptr);
    const auto box = body->boundingBox().value();
    // The sketch: 160 x 80.
    CHECK_THAT(box.max.x.in(units::mm), WithinAbs(160.0, kPositionToleranceMm));
    CHECK_THAT(box.max.y.in(units::mm), WithinAbs(80.0, kPositionToleranceMm));
    // The feature: depth = 40.
    CHECK_THAT(box.max.z.in(units::mm), WithinAbs(40.0, kPositionToleranceMm));
}

TEST_CASE("Configurations_UndoAndRedoOfEveryOperation", "[configurations][features][undo][p12][acceptance]") {
    BoxFamilyModel m;
    Regenerator regenerator;
    const std::size_t before = m.doc.configurations().size();

    SECTION("create") {
        auto command = std::make_unique<CreateConfigurationCommand>("Huge");
        CreateConfigurationCommand* raw = command.get();
        REQUIRE(m.history.execute(m.doc, std::move(command)).has_value());
        const ConfigurationId created = raw->configurationId();
        CHECK(m.doc.configurations().size() == before + 1);

        REQUIRE(m.history.undo(m.doc).has_value());
        CHECK(m.doc.configurations().size() == before);
        REQUIRE(m.history.redo(m.doc).has_value());
        CHECK(m.doc.configurations().size() == before + 1);
        // Redo restores the same ID, so anything that referred to it still does.
        CHECK(m.doc.configurations().find(created) != nullptr);
    }
    SECTION("modify an override") {
        REQUIRE(m.history
                    .execute(m.doc, ModifyConfigurationCommand::setOverride(m.small, m.width, 45_mm))
                    .has_value());
        m.activate(m.small);
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK_THAT(volumeMm3(regenerator, m.box),
                   WithinRel(BoxFamilyModel::expectedVolumeMm3(45.0), kRel));

        // Undo the activation, then the override.
        REQUIRE(m.history.undo(m.doc).has_value());
        REQUIRE(m.history.undo(m.doc).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK_THAT(volumeMm3(regenerator, m.box),
                   WithinRel(BoxFamilyModel::expectedVolumeMm3(100.0), kRel));

        REQUIRE(m.history.redo(m.doc).has_value());
        REQUIRE(m.history.redo(m.doc).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK_THAT(volumeMm3(regenerator, m.box),
                   WithinRel(BoxFamilyModel::expectedVolumeMm3(45.0), kRel));
    }
    SECTION("delete") {
        m.activate(m.medium);
        REQUIRE(m.history.execute(m.doc, std::make_unique<DeleteConfigurationCommand>(m.medium)).has_value());
        CHECK(m.doc.configurations().size() == before - 1);
        // Deleting the active one falls back to the base configuration.
        CHECK(m.doc.activeConfiguration() == std::nullopt);

        REQUIRE(m.history.undo(m.doc).has_value());
        CHECK(m.doc.configurations().size() == before);
        // Undo restores it as the active one, with its override.
        CHECK(m.doc.activeConfiguration() == m.medium);
        CHECK(m.doc.effectiveParameterValue(m.width)->siValue == 0.08);

        REQUIRE(m.history.redo(m.doc).has_value());
        CHECK(m.doc.configurations().size() == before - 1);
        CHECK(m.doc.activeConfiguration() == std::nullopt);
    }
    SECTION("switch") {
        m.activate(m.small);
        m.activate(m.large);
        CHECK(m.doc.activeConfiguration() == m.large);
        REQUIRE(m.history.undo(m.doc).has_value());
        CHECK(m.doc.activeConfiguration() == m.small);
        REQUIRE(m.history.undo(m.doc).has_value());
        CHECK(m.doc.activeConfiguration() == std::nullopt);
        REQUIRE(m.history.redo(m.doc).has_value());
        CHECK(m.doc.activeConfiguration() == m.small);
    }
}

TEST_CASE("Configurations_AFailingConfigurationChangesNothing", "[configurations][features][p12][acceptance]") {
    // A configuration whose geometry cannot be built must fail atomically:
    // a structured diagnostic, no body, and a document that still holds the
    // model it held before.
    BoxFamilyModel m;
    Regenerator regenerator;
    m.activate(m.small);
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const Fingerprint good = fingerprint(regenerator, m.box);

    const ConfigurationId broken = m.doc.createConfiguration("Broken").value();
    REQUIRE(m.doc.setConfigurationOverride(broken, m.width, 0_mm).has_value());
    const Document before = m.doc.clone();

    m.activate(broken);
    const RegenerationReport report = requireReport(regenerator, m.doc);
    INFO(describe(report));
    CHECK_FALSE(report.succeeded());
    CHECK(regenerator.body(m.box) == nullptr);
    // Regeneration changed no engineering state: only the evaluated values
    // of driven parameters, which the clone already holds.
    CHECK(m.doc.configurations().size() == before.configurations().size());
    CHECK(m.doc.parameters().find(m.width)->siValue() == 0.1);

    // And the model recovers when the configuration is fixed.
    REQUIRE(m.doc.setConfigurationOverride(broken, m.width, 40_mm).has_value());
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK(fingerprint(regenerator, m.box) == good);
}

TEST_CASE("Configurations_TheBaseValuesAreNeverTouched", "[configurations][features][p12][acceptance]") {
    // However many configurations are activated and regenerated, the
    // parameters' own values are the base values they started with. That is
    // what makes a configuration an override rather than an edit.
    BoxFamilyModel m;
    Regenerator regenerator;
    for (const auto id : {std::optional{m.small}, std::optional{m.large}, std::optional<ConfigurationId>{},
                          std::optional{m.medium}}) {
        m.activate(id);
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK(m.doc.parameters().find(m.width)->siValue() == 0.1);
    }
    // Editing a parameter edits the base, not the active configuration.
    m.activate(m.small);
    REQUIRE(m.doc.setParameterValue(m.width, 120_mm).has_value());
    CHECK(m.doc.parameters().find(m.width)->siValue() == 0.12);
    CHECK(m.doc.effectiveParameterValue(m.width)->siValue == 0.04);
    m.activate(std::nullopt);
    CHECK(m.doc.effectiveParameterValue(m.width)->siValue == 0.12);
}

// ---------------------------------------------------------------------------
// The mechanical reference family, and stable references under it
// ---------------------------------------------------------------------------

TEST_CASE("ConfigurationFamily_BuildsARealBracketAtEverySize",
          "[configurations][features][p12][acceptance]") {
    // Several qualified feature kinds at once -- a parametric extrude, a
    // parametric hole, a linear pattern of it and a sketch attached to a
    // named face -- driven by one free parameter through a chain of
    // equations. The volume is the closed form, not a reading.
    using bettercad::test::BracketFamilyModel;
    BracketFamilyModel m;
    Regenerator regenerator;

    struct Case {
        const char* name;
        ConfigurationId BracketFamilyModel::*which;
        double widthMm;
    };
    const Case cases[] = {{"Small", &BracketFamilyModel::small, 50.0},
                          {"Medium", &BracketFamilyModel::medium, 100.0},
                          {"Large", &BracketFamilyModel::large, 200.0}};
    for (const Case& c : cases) {
        DYNAMIC_SECTION(c.name) {
            m.activate(m.*c.which);
            const RegenerationReport report = requireReport(regenerator, m.doc);
            INFO(describe(report));
            REQUIRE(report.succeeded());

            // Every derived parameter followed the one that was overridden.
            const double w = c.widthMm;
            CHECK_THAT(m.si(m.width) * 1000.0, WithinRel(w, kRel));
            CHECK_THAT(m.si(m.height) * 1000.0, WithinRel(w / 2.0, kRel));
            CHECK_THAT(m.si(m.thickness) * 1000.0, WithinRel(0.08 * w, kRel));
            CHECK_THAT(m.si(m.edge) * 1000.0, WithinRel(0.16 * w, kRel));
            CHECK_THAT(m.si(m.holeDiameter) * 1000.0, WithinRel(0.08 * w, kRel));
            CHECK_THAT(m.si(m.holeSpace) * 1000.0, WithinRel(0.68 * w, kRel));

            // The plate with its two through holes, against the closed form.
            const geometry::Body* body = regenerator.body(m.holes);
            REQUIRE(body != nullptr);
            CHECK(body->isValid());
            const double volume = body->massProperties()->volume.in(units::mm3);
            INFO("width " << w << " mm, expected " << BracketFamilyModel::expectedVolumeMm3(w));
            CHECK_THAT(volume, WithinRel(BracketFamilyModel::expectedVolumeMm3(w),
                                         bettercad::test::kRelApproximatedIntersection));
            const auto box = body->boundingBox();
            REQUIRE(box.has_value());
            CHECK_THAT(box->max.x.in(units::mm), WithinAbs(w, kPositionToleranceMm));
            CHECK_THAT(box->max.y.in(units::mm), WithinAbs(w / 2.0, kPositionToleranceMm));
            CHECK_THAT(box->max.z.in(units::mm), WithinAbs(0.08 * w, kPositionToleranceMm));
        }
    }
}

TEST_CASE("ConfigurationFamily_AttachedSketchesFollowTheFaceTheyName",
          "[configurations][features][references][p12][acceptance]") {
    // The stable-reference requirement. TopSketch names the plate's end cap,
    // whose height is 0.08 W and so moves with every configuration. The name
    // must keep resolving, to the face it names and not to a nearby one.
    using bettercad::test::BracketFamilyModel;
    BracketFamilyModel m;
    Regenerator regenerator;

    const auto placementZ = [&]() {
        const auto* s = m.doc.findObjectAs<sketch::Sketch>(m.topSketch);
        REQUIRE(s != nullptr);
        return s->placement().origin().z.in(units::mm);
    };
    const auto endCapZ = [&]() {
        const geometry::Body* body = regenerator.body(m.plate);
        REQUIRE(body != nullptr);
        const auto found = geometry::findNamedFaces(*body, FaceName{m.plate, {.role = FaceRole::EndCap}});
        REQUIRE(found.has_value());
        REQUIRE(found->size() == 1);
        REQUIRE(found->front().signature.has_value());
        return found->front().signature->point.z.in(units::mm);
    };

    for (const auto& [which, w] : {std::pair{m.small, 50.0}, {m.medium, 100.0}, {m.large, 200.0}}) {
        m.activate(which);
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        INFO("width " << w << " mm");
        // The named face is where the equations put it, and the sketch is on it.
        CHECK_THAT(endCapZ(), WithinAbs(BracketFamilyModel::expectedThicknessMm(w), kPositionToleranceMm));
        CHECK_THAT(placementZ(), WithinAbs(BracketFamilyModel::expectedThicknessMm(w), kPositionToleranceMm));
    }

    // Small -> Large -> Small puts it back exactly where it was.
    m.activate(m.small);
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK_THAT(placementZ(), WithinAbs(4.0, kPositionToleranceMm));

    // And it never snapped to a plane that merely used to be right: at Small
    // the plate is 4 mm thick, so nothing lies at Large's 16 mm.
    const geometry::Body* body = regenerator.body(m.plate);
    REQUIRE(body != nullptr);
    const auto stale = geometry::findFaces(
        *body, geometry::planeSignature(Point3D{0_mm, 0_mm, 16_mm}, Direction3D::unitZ()));
    REQUIRE(stale.has_value());
    CHECK(stale->empty());
}

TEST_CASE("ConfigurationFamily_SaveLoadKeepsTheWholeFamily", "[configurations][features][io][p12][acceptance]") {
    using bettercad::test::BracketFamilyModel;
    using bettercad::test::TempDir;
    TempDir dir;
    BracketFamilyModel m;
    m.activate(m.large);
    Regenerator before;
    REQUIRE(requireReport(before, m.doc).succeeded());
    const double volume = before.body(m.holes)->massProperties()->volume.in(units::mm3);

    const auto path = dir.path() / "bracket.bcad";
    REQUIRE(io::saveDocument(m.doc, path).has_value());
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    Regenerator after;
    REQUIRE(requireReport(after, *loaded).succeeded());
    CHECK(bits(after.body(m.holes)->massProperties()->volume.in(units::mm3)) == bits(volume));
    CHECK_THAT(after.body(m.holes)->massProperties()->volume.in(units::mm3),
               WithinRel(BracketFamilyModel::expectedVolumeMm3(200.0),
                         bettercad::test::kRelApproximatedIntersection));
}

TEST_CASE("Configurations_AnOverrideThatBreaksAnEquationIsReported",
          "[configurations][features][validation][p12][acceptance]") {
    // A configuration can make an equation fail even though every override
    // is individually valid. The failure must be structured, must name the
    // parameter, and must block what depends on it rather than half-build.
    Document doc{"Ratio"};
    REQUIRE(doc.createParameter("span", 100_mm, units::mm).has_value());
    const ParameterId count = doc.createParameter("count", 4.0, kUnitless).value();
    const ParameterId pitch = doc.createParameter("pitch", 1_mm, units::mm).value();
    REQUIRE(doc.setParameterExpression(pitch, "span / count").has_value());
    REQUIRE(evaluateParameterExpressions(doc).succeeded());
    CHECK(doc.parameters().find(pitch)->siValue() == 0.025);

    const ConfigurationId degenerate = doc.createConfiguration("Degenerate").value();
    REQUIRE(doc.setConfigurationOverride(degenerate, count, DimensionedValue{dimensions::dimensionless, 0.0})
                .has_value());
    REQUIRE(doc.setActiveConfiguration(degenerate).has_value());

    const ParameterEvaluationReport report = evaluateParameterExpressions(doc);
    CHECK_FALSE(report.succeeded());
    REQUIRE(report.failed.contains(pitch));
    const Error& error = report.failed.at(pitch);
    CHECK_THAT(error.message, ContainsSubstring("parameter 'pitch' = span / count:"));
    // The last good value is kept, not a half-computed one.
    CHECK(doc.parameters().find(pitch)->siValue() == 0.025);

    // validateDocument reports it too, as an error against the parameter.
    const ValidationReport validation = validateDocument(doc);
    CHECK_FALSE(validation.valid());
    const bool named = std::ranges::any_of(validation.issues, [&](const ValidationIssue& issue) {
        return issue.item == ObjectId{pitch} && issue.severity == Severity::Error;
    });
    CHECK(named);

    // Back to the base configuration and it evaluates again.
    REQUIRE(doc.setActiveConfiguration(std::nullopt).has_value());
    CHECK(evaluateParameterExpressions(doc).succeeded());
    CHECK(validateDocument(doc).valid());
}

TEST_CASE("Configurations_ADrivenParameterIsNeverAlsoOverridden",
          "[configurations][features][p12][acceptance][regression]") {
    // The invariant every path must keep: a parameter's value comes from its
    // expression or from a configuration, never from both. Overriding a
    // driven parameter was already refused; giving an expression to one that
    // is overridden has to be refused for the same reason.
    BoxFamilyModel m;
    // `width` is free and Small overrides it.
    const auto driven = m.doc.setParameterExpression(m.width, "depth * 4");
    REQUIRE_FALSE(driven.has_value());
    CHECK(driven.error().code == ErrorCode::FailedPrecondition);
    CHECK(driven.error().message ==
          "parameter 'width' is overridden by configurations 'Small', 'Medium' and 'Large'; clear the "
          "overrides to drive it by an expression");
    CHECK_FALSE(m.doc.parameters().find(m.width)->expression().has_value());

    // Clearing the overrides makes it possible again.
    for (const ConfigurationId id : {m.small, m.medium, m.large}) {
        REQUIRE(m.doc.clearConfigurationOverride(id, m.width).has_value());
    }
    CHECK(m.doc.setParameterExpression(m.width, "depth * 4").has_value());

    // And the other way round is still refused, as it was before.
    const ConfigurationId fresh = m.doc.createConfiguration("Fresh").value();
    const auto overridden = m.doc.setConfigurationOverride(fresh, m.width, 10_mm);
    REQUIRE_FALSE(overridden.has_value());
    CHECK(overridden.error().code == ErrorCode::FailedPrecondition);
}

TEST_CASE("Configurations_SwitchingCostIsMeasured", "[configurations][features][performance][p12]") {
    // A baseline, not a threshold: what it costs to switch configuration and
    // regenerate the reference family. Asserting a time here would only make
    // the suite flaky on a busy machine, so the number is reported and
    // recorded in docs/verification/P12-PARAM-002 instead. What IS asserted
    // is that every cycle rebuilt the model.
    using bettercad::test::BracketFamilyModel;
    BracketFamilyModel m;
    Regenerator regenerator;
    m.activate(m.medium);
    REQUIRE(requireReport(regenerator, m.doc).succeeded());

    constexpr int kCycles = 20;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kCycles; ++i) {
        m.activate(i % 2 == 0 ? m.small : m.large);
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        REQUIRE(regenerator.body(m.holes) != nullptr);
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const double ms = std::chrono::duration<double, std::milli>(elapsed).count();
    WARN("configuration switch + regenerate: " << ms / kCycles << " ms per cycle over " << kCycles
                                               << " cycles (extrude + hole + pattern + attached sketch)");

    // Regenerating the same configuration again does far less work, because
    // nothing is dirty; that it still gives a body is what matters here.
    const auto idleStart = std::chrono::steady_clock::now();
    for (int i = 0; i < kCycles; ++i) {
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
    }
    const double idleMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - idleStart).count();
    WARN("regenerate with nothing changed: " << idleMs / kCycles << " ms per pass");
}

TEST_CASE("Configurations_AWholeCycleThroughEverySizeReturnsToTheStart",
          "[configurations][features][p12][acceptance][determinism]") {
    // Small -> Medium -> Large -> Small, and then four repeats of an
    // A -> B -> A, against a document freshly built into Small. The values
    // in force are exact; the solid is bounded and must not walk.
    BoxFamilyModel m;
    Regenerator regenerator;
    const auto valuesNow = [&]() {
        return std::vector<std::uint64_t>{bits(m.doc.effectiveParameterValue(m.width)->siValue),
                                          bits(m.doc.parameters().find(m.height)->siValue()),
                                          bits(m.doc.parameters().find(m.depth)->siValue())};
    };

    m.activate(m.small);
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    const std::vector<std::uint64_t> smallValues = valuesNow();
    const Fingerprint smallFirst = fingerprint(regenerator, m.box);
    const geometry::TopologySummary smallTopology = regenerator.body(m.box)->topology();

    // The full lap.
    for (const ConfigurationId id : {m.medium, m.large, m.small}) {
        m.activate(id);
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
    }
    CHECK(valuesNow() == smallValues);
    CHECK(regenerator.body(m.box)->topology() == smallTopology);
    CHECK_THAT(fingerprint(regenerator, m.box).volume, WithinRel(smallFirst.volume, kRel));

    // Four repeats of Small -> Large -> Small. Each is compared with the
    // FIRST result, so a drift that accumulated would show as a growing
    // difference rather than being hidden by comparing with the previous lap.
    double worst = 0.0;
    for (int lap = 0; lap < 4; ++lap) {
        m.activate(m.large);
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        m.activate(m.small);
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CAPTURE(lap);
        // Exact, every lap: no configuration state leaks between switches.
        CHECK(valuesNow() == smallValues);
        CHECK(m.doc.activeConfiguration() == m.small);
        CHECK(m.doc.parameters().find(m.width)->siValue() == 0.1);
        const Fingerprint now = fingerprint(regenerator, m.box);
        CHECK(regenerator.body(m.box)->topology() == smallTopology);
        CHECK_THAT(now.volume, WithinRel(smallFirst.volume, kRel));
        worst = std::max(worst, std::abs(now.volume - smallFirst.volume) / smallFirst.volume);
    }
    // Against a document built straight into Small, never switched.
    BoxFamilyModel fresh;
    Regenerator clean;
    fresh.activate(fresh.small);
    REQUIRE(requireReport(clean, fresh.doc).succeeded());
    const Fingerprint freshSmall = fingerprint(clean, fresh.box);
    CHECK(std::vector<std::uint64_t>{bits(fresh.doc.effectiveParameterValue(fresh.width)->siValue),
                                     bits(fresh.doc.parameters().find(fresh.height)->siValue()),
                                     bits(fresh.doc.parameters().find(fresh.depth)->siValue())} == smallValues);
    const double againstFresh = std::abs(freshSmall.volume - smallFirst.volume) / smallFirst.volume;
    worst = std::max(worst, againstFresh);
    INFO("largest relative volume difference over the whole cycle: " << worst);
    // Measured at 1.5e-15; kRelTight (1e-12) is three orders clear, and the
    // bound is the same on the fourth lap as on the first.
    CHECK(worst < kRel);
    CHECK_THAT(freshSmall.volume * 1e9, WithinRel(BoxFamilyModel::expectedVolumeMm3(40.0), kRel));
}

TEST_CASE("Configurations_ACycleIsStillRefusedWhileAConfigurationIsActive",
          "[configurations][features][p12][acceptance]") {
    // A configuration cannot make or break a cycle -- a parameter in one is
    // driven, and a driven parameter is never overridden -- but the two must
    // still work together: the cycle is reported, nothing is evaluated, and
    // every parameter keeps its value.
    Document doc{"Cyclic"};
    const ParameterId seed = doc.createParameter("seed", 10_mm, units::mm).value();
    const ParameterId a = doc.createParameter("a", 1_mm, units::mm).value();
    const ParameterId b = doc.createParameter("b", 1_mm, units::mm).value();
    const ParameterId c = doc.createParameter("c", 1_mm, units::mm).value();
    REQUIRE(doc.setParameterExpression(a, "c + seed").has_value());
    REQUIRE(doc.setParameterExpression(b, "a * 2").has_value());
    REQUIRE(doc.setParameterExpression(c, "b / 2").has_value());

    const ConfigurationId big = doc.createConfiguration("Big").value();
    REQUIRE(doc.setConfigurationOverride(big, seed, 40_mm).has_value());
    REQUIRE(doc.setActiveConfiguration(big).has_value());

    const ParameterEvaluationReport report = evaluateParameterExpressions(doc);
    CHECK_FALSE(report.succeeded());
    REQUIRE(report.cycles.size() == 1);
    CHECK(report.cycles.front() == std::vector<ParameterId>{a, b, c});
    CHECK(report.evaluated.empty());
    // Nothing was written: each keeps the value it had.
    for (const ParameterId id : {a, b, c}) {
        CHECK(doc.parameters().find(id)->siValue() == 0.001);
    }
    // The override itself is untouched and still in force.
    CHECK(doc.effectiveParameterValue(seed)->siValue == 0.04);

    // Breaking the cycle makes the whole chain follow the configuration:
    // a = 40, b = 80, c = 40.
    REQUIRE(doc.setParameterExpression(a, "seed").has_value());
    REQUIRE(evaluateParameterExpressions(doc).succeeded());
    CHECK_THAT(doc.parameters().find(a)->siValue(), WithinRel(0.04, kRel));
    CHECK_THAT(doc.parameters().find(b)->siValue(), WithinRel(0.08, kRel));
    CHECK_THAT(doc.parameters().find(c)->siValue(), WithinRel(0.04, kRel));
}
