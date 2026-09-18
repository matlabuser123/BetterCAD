#include "reference/ReferenceTestSupport.hpp"

#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/geometry/Faces.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test::refmodel;
using bettercad::reference::buildMotorMountReferenceModel;
using bettercad::reference::MotorMountModel;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// P12-REF-001: the configuration-driven motor mount. Every expected value
// here is computed from the model's definition by hand, never read back from
// BetterCAD.

namespace {

constexpr double pi = std::numbers::pi;

/// The bracket's dimensions at a given width, from the equations written out:
/// height = W/2, thickness = W/15, bolt_d = thickness, edge = 2*thickness,
/// bolt_span = W - 2*edge.
struct Expected {
    double width, height, thickness, boltDiameter, edge, boltSpan;
    double plate, holes, pilot, total;

    explicit Expected(double w)
        : width(w), height(w / 2.0), thickness(w / 15.0), boltDiameter(w / 15.0), edge(2.0 * w / 15.0),
          boltSpan(w - 4.0 * w / 15.0) {
        plate = width * height * thickness;
        holes = 2.0 * pi * (boltDiameter / 2.0) * (boltDiameter / 2.0) * thickness;
        pilot = pi * MotorMountModel::kPilotRadiusMm * MotorMountModel::kPilotRadiusMm *
                MotorMountModel::kPilotHeightMm;
        total = plate - holes + pilot;
    }
};

MotorMountModel requireModel() {
    auto model = buildMotorMountReferenceModel();
    if (!model) {
        FAIL(model.error().message);
    }
    return std::move(*model);
}

/// Activates a configuration by name and regenerates.
void activate(MotorMountModel& m, ConfigurationId id, features::Regenerator& regenerator) {
    REQUIRE(m.document.setActiveConfiguration(id).has_value());
    requireRegenerated(regenerator, m.document);
}

double mm(const Document& doc, ParameterId parameter) {
    const auto value = doc.effectiveParameterValue(parameter);
    REQUIRE(value.has_value());
    return value->siValue * 1000.0;
}

} // namespace

TEST_CASE("ReferenceModel_MotorMountBuildsAtEveryConfiguration", "[reference][p12][motor-mount][acceptance]") {
    MotorMountModel m = requireModel();
    features::Regenerator regenerator;

    struct Case {
        const char* name;
        ConfigurationId MotorMountModel::*which;
        double widthMm;
    };
    for (const Case& c : {Case{"Small", &MotorMountModel::small, 90.0},
                          Case{"Medium", &MotorMountModel::medium, 120.0},
                          Case{"Large", &MotorMountModel::large, 180.0}}) {
        DYNAMIC_SECTION(c.name) {
            activate(m, m.*c.which, regenerator);
            const Expected e(c.widthMm);

            // The equations, each against the closed form.
            CHECK_THAT(mm(m.document, m.width), WithinRel(e.width, 1e-12));
            CHECK_THAT(mm(m.document, m.height), WithinRel(e.height, 1e-12));
            CHECK_THAT(mm(m.document, m.thickness), WithinRel(e.thickness, 1e-12));
            CHECK_THAT(mm(m.document, m.boltDiameter), WithinRel(e.boltDiameter, 1e-12));
            CHECK_THAT(mm(m.document, m.edge), WithinRel(e.edge, 1e-12));
            CHECK_THAT(mm(m.document, m.boltSpan), WithinRel(e.boltSpan, 1e-12));

            // The geometry, decomposed: the plate is a box, each bolt hole a
            // cylinder through it, the pilot boss a cylinder on top.
            const auto plate = featureBody(regenerator, m.plate);
            INFO("plate expected " << e.plate << " mm^3, actual " << plate.volumeMm3);
            CHECK_THAT(plate.volumeMm3, WithinRel(e.plate, kRel));

            const auto drilled = featureBody(regenerator, m.boltHoles);
            INFO("plate less holes expected " << e.plate - e.holes << " mm^3, actual " << drilled.volumeMm3);
            CHECK_THAT(drilled.volumeMm3, WithinRel(e.plate - e.holes, kRel));

            const auto print = requireFingerprint(m.document, regenerator);
            const auto body = onlyBody(print);
            checkSound(body);
            INFO("total expected " << e.total << " mm^3, actual " << body.volumeMm3);
            CHECK_THAT(body.volumeMm3, WithinRel(e.total, kRel));
            checkBounds(body, {0.0, 0.0, 0.0}, {e.width, e.height, e.thickness + MotorMountModel::kPilotHeightMm});
        }
    }
}

TEST_CASE("ReferenceModel_MotorMountPilotFollowsThePlateFace", "[reference][p12][motor-mount][references]") {
    // The pilot sketch names the plate's end cap, whose height is an
    // equation. Every configuration moves it, and the sketch must follow the
    // face it named -- not a plane that merely used to be right.
    MotorMountModel m = requireModel();
    features::Regenerator regenerator;

    const auto placementZ = [&]() {
        const auto* s = m.document.findObjectAs<sketch::Sketch>(m.pilotSketch);
        REQUIRE(s != nullptr);
        return s->placement().origin().z.in(units::mm);
    };
    const auto capZ = [&]() {
        const geometry::Body* body = regenerator.body(m.plate);
        REQUIRE(body != nullptr);
        const auto found = geometry::findNamedFaces(*body, FaceName{m.plate, {.role = FaceRole::EndCap}});
        REQUIRE(found.has_value());
        REQUIRE(found->size() == 1);
        REQUIRE(found->front().signature.has_value());
        return found->front().signature->point.z.in(units::mm);
    };

    for (const auto& [which, widthMm] : {std::pair{m.small, 90.0}, {m.medium, 120.0}, {m.large, 180.0}}) {
        activate(m, which, regenerator);
        const double thickness = widthMm / 15.0;
        INFO("width " << widthMm << " mm, thickness " << thickness << " mm");
        CHECK_THAT(capZ(), WithinAbs(thickness, kPositionMm));
        CHECK_THAT(placementZ(), WithinAbs(thickness, kPositionMm));
        // The boss sits on the cap and rises by its own height.
        const auto body = onlyBody(requireFingerprint(m.document, regenerator));
        CHECK_THAT(body.maxMm[2], WithinAbs(thickness + MotorMountModel::kPilotHeightMm, kBoundsPaddingMm));
    }

    // Back at Small, nothing lies where Large's cap was.
    activate(m, m.small, regenerator);
    const geometry::Body* body = regenerator.body(m.plate);
    REQUIRE(body != nullptr);
    const auto stale = geometry::findFaces(
        *body, geometry::planeSignature(Point3D{0_mm, 0_mm, 12_mm}, Direction3D::unitZ()));
    REQUIRE(stale.has_value());
    CHECK(stale->empty());
    CHECK_THAT(capZ(), WithinAbs(6.0, kPositionMm));
}

TEST_CASE("ReferenceModel_MotorMountSurvivesAWholeConfigurationCycle",
          "[reference][p12][motor-mount][determinism]") {
    // Small -> Medium -> Large -> Small, then four repeats, each compared
    // with the FIRST result so a drift that accumulated would show.
    MotorMountModel m = requireModel();
    features::Regenerator regenerator;
    activate(m, m.small, regenerator);
    const auto first = requireFingerprint(m.document, regenerator);
    const Expected small(90.0);

    for (const ConfigurationId id : {m.medium, m.large, m.small}) {
        activate(m, id, regenerator);
    }
    auto difference = reference::compare(first, requireFingerprint(m.document, regenerator));
    CHECK(difference.sameStructure);
    CHECK(difference.volumeAreaRelative <= kRel);
    CHECK(difference.positionMm <= kPositionMm);

    double worst = 0.0;
    for (int lap = 0; lap < 4; ++lap) {
        activate(m, m.large, regenerator);
        activate(m, m.small, regenerator);
        const auto now = requireFingerprint(m.document, regenerator);
        const auto lapDifference = reference::compare(first, now);
        CAPTURE(lap, lapDifference.volumeAreaRelative, lapDifference.positionMm);
        CHECK(lapDifference.sameStructure);
        CHECK(lapDifference.volumeAreaRelative <= kRel);
        worst = std::max(worst, lapDifference.volumeAreaRelative);
        // The parameter values come back exactly, every lap.
        CHECK(mm(m.document, m.width) == 90.0);
        CHECK_THAT(onlyBody(now).volumeMm3, WithinRel(small.total, kRel));
    }
    INFO("largest relative difference over the cycle: " << worst);
    CHECK(worst <= kRel);

    // A document built fresh and put straight into Small agrees.
    MotorMountModel fresh = requireModel();
    features::Regenerator clean;
    activate(fresh, fresh.small, clean);
    const auto freshPrint = requireFingerprint(fresh.document, clean);
    const auto against = reference::compare(first, freshPrint);
    CHECK(against.sameStructure);
    CHECK(against.volumeAreaRelative <= kRel);
    CHECK_THAT(onlyBody(freshPrint).volumeMm3, WithinRel(small.total, kRel));
}

TEST_CASE("ReferenceModel_MotorMountStaysCentredInEveryConfiguration",
          "[reference][p12][motor-mount][acceptance]") {
    // REGRESSION (P12-REF-001 adversarial review). The bolt holes were
    // driven ACROSS the plate (centerUParameter = edge) but frozen UP it at
    // a literal 30 mm, and the pilot boss's centre was the literal
    // (45, 22.5). The plate is width x width/2, so those literals were the
    // middle of the 90 mm plate only: in Medium and Large both features sat
    // off-centre. No volume check could ever have caught it, because moving
    // a hole or a boss does not change how much material it removes or adds
    // -- which is why this test measures POSITION, not volume.
    for (const auto& [name, widthMm] : std::vector<std::pair<const char*, double>>{
             {"Small", 90.0}, {"Medium", 120.0}, {"Large", 180.0}}) {
        DYNAMIC_SECTION(name) {
            MotorMountModel m = requireModel();
            const ConfigurationId configuration =
                m.document.configurations().findByName(name)->id();
            REQUIRE(m.document.setActiveConfiguration(configuration).has_value());
            features::Regenerator regenerator;
            requireRegenerated(regenerator, m.document);

            const double height = widthMm / 2.0;
            const double thickness = widthMm / 15.0;
            CHECK_THAT(mm(m.document, m.halfWidth), WithinRel(widthMm / 2.0, 1e-12));
            CHECK_THAT(mm(m.document, m.halfHeight), WithinRel(height / 2.0, 1e-12));

            // The plate spans 0..width by 0..height, so a part whose holes
            // and boss are centred is symmetric about v = height/2. The
            // centroid of the whole part is the check: the boss sits on the
            // plate's middle and the two bolt holes on the same line, so the
            // centroid must lie exactly there in v, and at width/2 in u.
            const auto body = onlyBody(requireFingerprint(m.document, regenerator));
            checkSound(body);
            INFO("plate " << widthMm << " x " << height << " x " << thickness
                          << ", centroid (" << body.centroidMm[0] << ", " << body.centroidMm[1] << ")");
            CHECK_THAT(body.centroidMm[0], WithinAbs(widthMm / 2.0, 1e-9));
            CHECK_THAT(body.centroidMm[1], WithinAbs(height / 2.0, 1e-9));
            checkBounds(body, {0.0, 0.0, 0.0}, {widthMm, height, thickness + 10.0});
        }
    }
}

TEST_CASE("ReferenceModel_MotorMountFailsAndRecovers", "[reference][p12][motor-mount][acceptance]") {
    // The one model with no failure-path coverage (P12-REF-001 adversarial
    // review).
    //
    // Note what could NOT be used to fail it: every dimension of this part
    // is an equation on `width`, so the whole bracket is scale-invariant and
    // no width, however small, ever puts a hole off the plate. That is worth
    // stating -- it is a property of the model, and the first attempt at
    // this test assumed the opposite and passed a regeneration it expected
    // to fail. The failure has to come from breaking a relation, not from
    // scaling one.
    //
    // So: free the bolt inset from its equation and drive it past the edge
    // of the plate. The hole then has no face to start on, which must fail
    // atomically -- nothing downstream keeps a body, the document is
    // untouched apart from the parameter -- and recover exactly.
    MotorMountModel m = requireModel();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const auto good = requireFingerprint(m.document, regenerator);
    const auto objectsBefore = m.document.objectCount();

    REQUIRE(m.document.setParameterExpression(m.edge, std::nullopt).has_value());
    REQUIRE(m.document.setParameterValue(m.edge, 200_mm).has_value());
    auto report = regenerator.regenerate(m.document);
    REQUIRE(report.has_value());
    INFO(bettercad::test::describe(*report));
    CHECK_FALSE(report->succeeded());
    CHECK_FALSE(report->failed.empty());
    // The diagnostic says what failed, where and why -- not just "false".
    CHECK_THAT(bettercad::test::describe(*report),
               ContainsSubstring("BoltHole") && ContainsSubstring("is not on a face of the body"));
    // Atomic: the hole and everything after it kept no body.
    CHECK(regenerator.body(m.boltHole) == nullptr);
    CHECK(regenerator.body(m.boltHoles) == nullptr);
    CHECK(regenerator.body(m.pilot) == nullptr);
    CHECK(m.document.objectCount() == objectsBefore);

    // Put the equation back; the model returns exactly.
    REQUIRE(m.document.setParameterExpression(m.edge, "2 * thickness").has_value());
    requireRegenerated(regenerator, m.document);
    const auto difference = reference::compare(good, requireFingerprint(m.document, regenerator));
    CHECK(difference.sameStructure);
    CHECK(difference.volumeAreaRelative <= kRel);
    CHECK(difference.positionMm <= kPositionMm);
}
