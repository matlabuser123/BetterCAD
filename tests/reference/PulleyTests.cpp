#include "reference/ReferenceTestSupport.hpp"

#include <bettercad/features/FeatureCommands.hpp>
#include <bettercad/features/ResultBodies.hpp>
#include <bettercad/features/Validation.hpp>

#include <catch2/matchers/catch_matchers_string.hpp>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using namespace bettercad::test::refmodel;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;
namespace an = bettercad::test::analytic;
using an::pi;

namespace {

/// The pulley's dimensions in mm, as the builder sets them.
struct PulleySize {
    double outer = 60.0;
    double rimInner = 50.0;
    double rimWidth = 30.0;
    double hub = 25.0;
    double hubLength = 40.0;
    double webStart = 10.0;
    double webThickness = 10.0;
    double bore = 20.0;
    double grooveCentre = 15.0;
    double grooveDepth = 6.0;
    double grooveWidth = 12.0;
    double grooveBottom = 4.0;
    double fillet = 3.0;
    double chamfer = 1.0;

    [[nodiscard]] double webEnd() const { return webStart + webThickness; }
};

/// How far along its feature chain the pulley is.
enum class Stage { Turned, Grooved, Bored, Filleted, Chamfered };

/// The pulley's half section after @p stage, from the dimensions alone: u is
/// the radius, v the height. Counter-clockwise from the axis (or the bore).
an::Loop section(const PulleySize& s, Stage stage) {
    const bool groove = stage >= Stage::Grooved;
    const bool bore = stage >= Stage::Bored;
    const bool fillets = stage >= Stage::Filleted;
    const bool chamfers = stage >= Stage::Chamfered;
    const double rb = bore ? s.bore / 2.0 : 0.0;
    const double f = s.fillet;
    const double c = s.chamfer;
    const double z0 = s.webStart;
    const double z1 = s.webEnd();
    an::Path p({rb, 0.0});
    if (chamfers) {
        p.to({s.hub - c, 0.0}).to({s.hub, c});
    } else {
        p.to({s.hub, 0.0});
    }
    if (fillets) {
        p.to({s.hub, z0 - f}).arcTo({s.hub + f, z0 - f}, f, 180.0, 90.0).to({s.rimInner - f, z0});
        p.arcTo({s.rimInner - f, z0 - f}, f, 90.0, 0.0);
    } else {
        p.to({s.hub, z0}).to({s.rimInner, z0});
    }
    p.to({s.rimInner, 0.0}).to({s.outer, 0.0});
    if (groove) {
        // The V-groove: its mouth is groove_width wide on the rim and its
        // bottom groove_bottom_width wide, groove_depth in.
        p.to({s.outer, s.grooveCentre - s.grooveWidth / 2.0})
            .to({s.outer - s.grooveDepth, s.grooveCentre - s.grooveBottom / 2.0})
            .to({s.outer - s.grooveDepth, s.grooveCentre + s.grooveBottom / 2.0})
            .to({s.outer, s.grooveCentre + s.grooveWidth / 2.0});
    }
    p.to({s.outer, s.rimWidth}).to({s.rimInner, s.rimWidth});
    if (fillets) {
        p.to({s.rimInner, z1 + f}).arcTo({s.rimInner - f, z1 + f}, f, 0.0, -90.0).to({s.hub + f, z1});
        p.arcTo({s.hub + f, z1 + f}, f, 270.0, 180.0);
    } else {
        p.to({s.rimInner, z1}).to({s.hub, z1});
    }
    if (chamfers) {
        p.to({s.hub, s.hubLength - c}).to({s.hub - c, s.hubLength});
    } else {
        p.to({s.hub, s.hubLength});
    }
    p.to({rb, s.hubLength});
    return p.close();
}

an::Solid expected(const PulleySize& s, Stage stage) {
    const an::Loop loop = section(s, stage);
    REQUIRE(an::largestGap(loop) < 1e-12);
    return an::revolved({loop});
}

std::array<double, 3> lowCorner(const PulleySize& s) {
    return {-s.outer, -s.outer, 0.0};
}
std::array<double, 3> highCorner(const PulleySize& s) {
    return {s.outer, s.outer, s.hubLength};
}

reference::PulleyModel buildPulley() {
    auto built = reference::buildPulleyReferenceModel();
    INFO((built ? std::string{} : built.error().message));
    REQUIRE(built.has_value());
    return std::move(*built);
}

std::vector<ObjectId> chain(const reference::PulleyModel& m) {
    return {m.section, m.blank, m.grooveSection, m.groove, m.bore, m.webFillets, m.hubChamfers};
}

} // namespace

TEST_CASE("ReferenceModel_PulleyBuildsValidGeometry", "[reference][pulley]") {
    reference::PulleyModel m = buildPulley();
    const features::ValidationReport validation = features::validateDocument(m.document);
    for (const features::ValidationIssue& issue : validation.issues) {
        INFO(issue.message);
    }
    CHECK(validation.valid());
    CHECK(validation.issues.empty());

    features::Regenerator regenerator;
    const features::RegenerationReport report = requireRegenerated(regenerator, m.document);
    CHECK(report.regenerated == chain(m));
    CHECK(features::resultFeatures(m.document) == std::vector<ObjectId>{m.hubChamfers});
    const std::vector<std::string_view> kinds{"sketch", "revolve", "sketch", "revolve", "hole", "fillet", "chamfer"};
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        const DocumentObject* object = m.document.findObject(chain(m)[i]);
        REQUIRE(object != nullptr);
        CHECK(object->typeName() == kinds[i]);
    }
    for (const ObjectId feature : {m.blank, m.groove, m.bore, m.webFillets, m.hubChamfers}) {
        INFO("feature " << feature);
        checkSound(featureBody(regenerator, feature));
    }
    checkSound(onlyBody(requireFingerprint(m.document, regenerator)));
}

TEST_CASE("ReferenceModel_PulleyMatchesIndependentProperties", "[reference][pulley][acceptance]") {
    reference::PulleyModel m = buildPulley();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const PulleySize s;

    SECTION("the turned blank: hub, web and rim as annular cylinders") {
        // V = pi/4 (d^2 L) for the hub, pi (ro^2 - ri^2) w for the rim and
        // pi (ri^2 - rhub^2) t for the web.
        const double hub = pi * s.hub * s.hub * s.hubLength;
        const double web = pi * (s.rimInner * s.rimInner - s.hub * s.hub) * s.webThickness;
        const double rim = pi * (s.outer * s.outer - s.rimInner * s.rimInner) * s.rimWidth;
        const an::Solid blank = expected(s, Stage::Turned);
        CHECK_THAT(blank.volume, WithinRel(hub + web + rim, 1e-14));
        const reference::BodyFingerprint body = featureBody(regenerator, m.blank);
        checkProperties(body, blank);
        checkBounds(body, lowCorner(s), highCorner(s));
    }
    SECTION("the groove, by Pappus: V = 2 pi R A") {
        // The groove's section is a trapezoid, mouth groove_width on the rim
        // and groove_bottom_width at the bottom, groove_depth deep. Its
        // centroid is h (a + 2b) / (3 (a + b)) in from the mouth.
        const double a = s.grooveWidth;
        const double bb = s.grooveBottom;
        const double area = (a + bb) / 2.0 * s.grooveDepth;
        const double radius = s.outer - s.grooveDepth * (a + 2.0 * bb) / (3.0 * (a + bb));
        const double groove = 2.0 * pi * radius * area;
        CHECK_THAT(expected(s, Stage::Turned).volume - groove, WithinRel(expected(s, Stage::Grooved).volume, 1e-14));
        const reference::BodyFingerprint body = featureBody(regenerator, m.groove);
        checkProperties(body, expected(s, Stage::Grooved));
    }
    SECTION("each step of the chain") {
        const std::vector<std::pair<ObjectId, Stage>> steps{{m.blank, Stage::Turned},
                                                            {m.groove, Stage::Grooved},
                                                            {m.bore, Stage::Bored},
                                                            {m.webFillets, Stage::Filleted},
                                                            {m.hubChamfers, Stage::Chamfered}};
        for (const auto& [feature, stage] : steps) {
            INFO("feature " << feature);
            const reference::BodyFingerprint body = featureBody(regenerator, feature);
            checkProperties(body, expected(s, stage));
            checkBounds(body, lowCorner(s), highCorner(s));
        }
    }
    SECTION("the finished pulley") {
        const reference::BodyFingerprint body = onlyBody(requireFingerprint(m.document, regenerator));
        checkProperties(body, expected(s, Stage::Chamfered));
        checkBounds(body, lowCorner(s), highCorner(s));
        // The four web fillets add their rings and the two hub chamfers take
        // theirs away, each by Pappus.
        const double ring = 2.0 * pi * an::filletArea(s.fillet);
        const double fillets = ring * (2.0 * (s.hub + an::filletCentroid(s.fillet)) +
                                       2.0 * (s.rimInner - an::filletCentroid(s.fillet)));
        const double chamfers = 2.0 * pi * s.chamfer * s.chamfer * (s.hub - s.chamfer / 3.0);
        CHECK_THAT(expected(s, Stage::Bored).volume + fillets - chamfers,
                   WithinRel(expected(s, Stage::Chamfered).volume, 1e-14));
    }
}

TEST_CASE("ReferenceModel_PulleyRegeneratesAfterParameterChanges", "[reference][pulley][acceptance]") {
    reference::PulleyModel m = buildPulley();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const reference::ModelFingerprint baseline = requireFingerprint(m.document, regenerator);

    SECTION("outer diameter 120 -> 140 mm: the rim grows and the groove follows it") {
        REQUIRE(m.document.setParameterValue(m.outerRadius, 70_mm).has_value());
        const features::RegenerationReport report = requireRegenerated(regenerator, m.document);
        CHECK(report.regenerated == chain(m));
        PulleySize s;
        s.outer = 70.0;
        const reference::BodyFingerprint body = onlyBody(requireFingerprint(m.document, regenerator));
        checkProperties(body, expected(s, Stage::Chamfered));
        checkBounds(body, lowCorner(s), highCorner(s));
        // The groove is still groove_depth deep in the rim, and the hub, web
        // and bore are untouched.
        const geometry::Body& pulley = *regenerator.body(m.hubChamfers);
        CHECK(circleEdges(pulley, {0.0, 0.0, s.grooveCentre - s.grooveBottom / 2.0}, Direction3D::unitZ(),
                          s.outer - s.grooveDepth) == 1);
        CHECK(circleEdges(pulley, {0.0, 0.0, s.grooveCentre - s.grooveBottom / 2.0}, Direction3D::unitZ(), 54.0) ==
              0);
        CHECK(circleEdges(pulley, {0.0, 0.0, 0.0}, Direction3D::unitZ(), s.bore / 2.0) == 1);
    }
    SECTION("bore 20 -> 25 mm: only the bore and what follows it are rebuilt") {
        REQUIRE(m.document.setParameterValue(m.boreDiameter, 25_mm).has_value());
        const features::RegenerationReport report = requireRegenerated(regenerator, m.document);
        CHECK(report.regenerated == std::vector<ObjectId>{m.bore, m.webFillets, m.hubChamfers});
        PulleySize s;
        s.bore = 25.0;
        checkProperties(onlyBody(requireFingerprint(m.document, regenerator)), expected(s, Stage::Chamfered));
        const geometry::Body& pulley = *regenerator.body(m.hubChamfers);
        CHECK(circleEdges(pulley, {0.0, 0.0, 0.0}, Direction3D::unitZ(), 12.5) == 1);
        CHECK(circleEdges(pulley, {0.0, 0.0, 0.0}, Direction3D::unitZ(), 10.0) == 0);
    }

    // Both restored: the original pulley, to rounding.
    REQUIRE(m.document.setParameterValue(m.outerRadius, 60_mm).has_value());
    REQUIRE(m.document.setParameterValue(m.boreDiameter, 20_mm).has_value());
    requireRegenerated(regenerator, m.document);
    const reference::FingerprintDifference back =
        reference::compare(baseline, requireFingerprint(m.document, regenerator));
    INFO("restored: volume and area " << back.volumeAreaRelative << " relative, positions " << back.positionMm
                                      << " mm");
    CHECK(back.sameStructure);
    CHECK(back.volumeAreaRelative <= 1e-12);
    CHECK(back.positionMm <= 1e-9);
}

TEST_CASE("ReferenceModel_PulleyRegeneratesDeterministically", "[reference][pulley]") {
    reference::PulleyModel m = buildPulley();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const reference::ModelFingerprint baseline = requireFingerprint(m.document, regenerator);
    checkRepeatedRegeneration(m.document, baseline, 10);
    for (int i = 0; i < 3; ++i) {
        reference::PulleyModel again = buildPulley();
        features::Regenerator fresh;
        requireRegenerated(fresh, again.document);
        CHECK(requireFingerprint(again.document, fresh) == baseline);
    }
}

TEST_CASE("ReferenceModel_PulleySaveLoad", "[reference][pulley][io]") {
    reference::PulleyModel m = buildPulley();
    TempDir dir;
    checkSaveLoad(m.document, dir.path() / "pulley.bcad");
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    REQUIRE(m.document.setParameterValue(m.grooveDepth, 8_mm).has_value());
    requireRegenerated(regenerator, m.document);
    PulleySize s;
    s.grooveDepth = 8.0;
    checkProperties(onlyBody(requireFingerprint(m.document, regenerator)), expected(s, Stage::Chamfered));
}

TEST_CASE("ReferenceModel_PulleyUndoRedo", "[reference][pulley]") {
    reference::PulleyModel m = buildPulley();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const reference::ModelFingerprint baseline = requireFingerprint(m.document, regenerator);
    const Document original = m.document.clone();
    CommandHistory history;

    execute(history, m.document, ModifyParameterCommand::setValue(m.boreDiameter, 25_mm));
    requireRegenerated(regenerator, m.document);
    PulleySize wider;
    wider.bore = 25.0;
    const reference::ModelFingerprint changed = requireFingerprint(m.document, regenerator);
    checkProperties(onlyBody(changed), expected(wider, Stage::Chamfered));
    // The bore diameter drives a hole, not a sketch, so undo and redo are
    // exact for the document and the geometry.
    REQUIRE(history.undo(m.document).has_value());
    CHECK(equivalent(m.document, original));
    requireRegenerated(regenerator, m.document);
    CHECK(requireFingerprint(m.document, regenerator) == baseline);
    REQUIRE(history.redo(m.document).has_value());
    requireRegenerated(regenerator, m.document);
    CHECK(requireFingerprint(m.document, regenerator) == changed);
}

TEST_CASE("ReferenceModel_PulleyExports", "[reference][pulley][io][export]") {
    reference::PulleyModel m = buildPulley();
    TempDir dir;
    const PulleySize s;
    const an::Solid finished = expected(s, Stage::Chamfered);
    checkStepExport(m.document, dir.path() / "pulley.step", finished.volume, lowCorner(s), highCorner(s));
    checkStlExport(m.document, dir.path() / "pulley.stl", finished.volume, finished.area);
}

TEST_CASE("ReferenceModel_PulleyFailsSafelyOnInvalidParameters", "[reference][pulley]") {
    reference::PulleyModel m = buildPulley();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const reference::ModelFingerprint baseline = requireFingerprint(m.document, regenerator);
    const Document original = m.document.clone();
    const PulleySize s;

    // A bore wider than the hub: the hole would break out of the hub's end
    // face, so it is refused before anything is built.
    REQUIRE(m.document.setParameterValue(m.boreDiameter, 52_mm).has_value());
    auto report = regenerator.regenerate(m.document);
    REQUIRE(report.has_value());
    CHECK(report->failed == std::vector<ObjectId>{m.bore});
    CHECK(report->blocked == std::vector<ObjectId>{m.webFillets, m.hubChamfers});
    const Error& error = report->errors.at(m.bore);
    INFO(error.message);
    CHECK(error.code == ErrorCode::FailedPrecondition);
    CHECK_THAT(error.message, ContainsSubstring("Bore"));
    for (const ObjectId feature : {m.bore, m.webFillets, m.hubChamfers}) {
        CHECK(regenerator.body(feature) == nullptr);
    }
    checkProperties(featureBody(regenerator, m.groove), expected(s, Stage::Grooved));
    CHECK_FALSE(features::validateDocument(m.document).valid());
    REQUIRE(m.document.setParameterValue(m.boreDiameter, 20_mm).has_value());
    CHECK(equivalent(m.document, original));
    requireRegenerated(regenerator, m.document);
    CHECK(requireFingerprint(m.document, regenerator) == baseline);
}
