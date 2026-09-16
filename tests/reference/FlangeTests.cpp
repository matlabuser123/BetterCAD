#include "reference/ReferenceTestSupport.hpp"

#include <bettercad/features/FeatureCommands.hpp>
#include <bettercad/features/ResultBodies.hpp>
#include <bettercad/features/Validation.hpp>

#include <catch2/matchers/catch_matchers_string.hpp>

#include <numbers>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using namespace bettercad::test::refmodel;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
namespace an = bettercad::test::analytic;
using an::pi;

namespace {

/// The flange's dimensions in mm, as the builder sets them.
struct FlangeSize {
    double outer = 50.0;
    double thickness = 12.0;
    double bore = 30.0;
    double boltCircle = 35.0;
    double bolt = 8.0;
    int count = 6;
    double chamfer = 1.0;
    double fillet = 2.0;
};

/// How far along its feature chain the flange is.
enum class Stage { Disc, Bored, OneBoltHole, Patterned, Chamfered, Filleted };

/// The flange's section without its bolt holes, from the dimensions alone:
/// u is the radius, v the height. Counter-clockwise from the bottom of the
/// bore (or the axis).
an::Loop section(const FlangeSize& s, Stage stage) {
    const double rb = stage >= Stage::Bored ? s.bore / 2.0 : 0.0;
    const double r = s.outer;
    const double t = s.thickness;
    const double c = s.chamfer;
    const double f = s.fillet;
    an::Path p({rb, 0.0});
    if (stage >= Stage::Filleted) {
        p.to({r - f, 0.0}).arcTo({r - f, f}, f, -90.0, 0.0);
    } else {
        p.to({r, 0.0});
    }
    if (stage >= Stage::Chamfered) {
        p.to({r, t - c}).to({r - c, t}).to({rb + c, t}).to({rb, t - c});
    } else {
        p.to({r, t}).to({rb, t});
    }
    return p.close();
}

std::size_t holesAt(Stage stage, const FlangeSize& s) {
    if (stage == Stage::OneBoltHole) {
        return 1;
    }
    return stage >= Stage::Patterned ? static_cast<std::size_t>(s.count) : 0;
}

/// The revolved section less the bolt holes, cylinders of radius rh through
/// the thickness: V -= n pi rh^2 t, the faces lose 2 n pi rh^2 and the holes
/// add their walls, n 2 pi rh t. The holes' centroid is at mid-thickness,
/// and a full circle of them adds up to the axis; a single hole moves the
/// centroid away from it.
an::Solid expected(const FlangeSize& s, Stage stage) {
    const an::Loop loop = section(s, stage);
    REQUIRE(an::largestGap(loop) < 1e-12);
    const an::Solid disc = an::revolved({loop});
    const double n = static_cast<double>(holesAt(stage, s));
    const double rh = s.bolt / 2.0;
    const double hole = pi * rh * rh * s.thickness;
    an::Solid result = disc;
    result.volume = disc.volume - n * hole;
    result.area = disc.area - 2.0 * n * pi * rh * rh + n * 2.0 * pi * rh * s.thickness;
    result.centroid[2] = (disc.volume * disc.centroid[2] - n * hole * s.thickness / 2.0) / result.volume;
    if (stage == Stage::OneBoltHole) {
        result.centroid[0] = -hole * s.boltCircle / result.volume;
    }
    return result;
}

std::array<double, 3> lowCorner(const FlangeSize& s) {
    return {-s.outer, -s.outer, 0.0};
}
std::array<double, 3> highCorner(const FlangeSize& s) {
    return {s.outer, s.outer, s.thickness};
}

reference::FlangeModel buildFlange() {
    auto built = reference::buildFlangeReferenceModel();
    INFO((built ? std::string{} : built.error().message));
    REQUIRE(built.has_value());
    return std::move(*built);
}

std::vector<ObjectId> chain(const reference::FlangeModel& m) {
    return {m.discSketch, m.disc, m.bore, m.boltHole, m.boltCircle, m.edgeChamfers, m.rimFillet};
}

/// Each bolt hole i of n is at angle 2 pi i / n on the bolt circle, through
/// the flange: its rims are circles of the hole's radius on both faces.
/// Half way between two holes there is none, and there are no other holes.
void checkBoltHoles(const geometry::Body& body, const FlangeSize& s) {
    const double rh = s.bolt / 2.0;
    for (int i = 0; i < s.count; ++i) {
        const double angle = 2.0 * pi * i / s.count;
        const double between = angle + pi / s.count;
        INFO("hole " << i << " of " << s.count << " at " << angle * 180.0 / pi << " deg");
        for (const double z : {0.0, s.thickness}) {
            CHECK(circleEdges(body, {s.boltCircle * std::cos(angle), s.boltCircle * std::sin(angle), z},
                              Direction3D::unitZ(), rh) == 1);
            CHECK(circleEdges(body, {s.boltCircle * std::cos(between), s.boltCircle * std::sin(between), z},
                              Direction3D::unitZ(), rh) == 0);
        }
    }
    const auto edges = geometry::listEdges(body);
    REQUIRE(edges.has_value());
    std::size_t rims = 0;
    for (const geometry::EdgeInfo& edge : *edges) {
        if (edge.signature && edge.signature->curve == geometry::EdgeCurve::Circle &&
            std::abs(edge.signature->radius.in(units::mm) - rh) < 1e-9) {
            ++rims;
        }
    }
    CHECK(rims == 2 * static_cast<std::size_t>(s.count));
}

} // namespace

TEST_CASE("ReferenceModel_FlangeBuildsValidGeometry", "[reference][flange]") {
    reference::FlangeModel m = buildFlange();
    const features::ValidationReport validation = features::validateDocument(m.document);
    for (const features::ValidationIssue& issue : validation.issues) {
        INFO(issue.message);
    }
    CHECK(validation.valid());
    CHECK(validation.issues.empty());

    features::Regenerator regenerator;
    const features::RegenerationReport report = requireRegenerated(regenerator, m.document);
    CHECK(report.regenerated == chain(m));
    CHECK(features::resultFeatures(m.document) == std::vector<ObjectId>{m.rimFillet});
    const std::vector<std::string_view> kinds{"sketch", "extrude", "hole", "hole", "circular_pattern", "chamfer",
                                              "fillet"};
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        const DocumentObject* object = m.document.findObject(chain(m)[i]);
        REQUIRE(object != nullptr);
        CHECK(object->typeName() == kinds[i]);
    }
    for (const ObjectId feature : {m.disc, m.bore, m.boltHole, m.boltCircle, m.edgeChamfers, m.rimFillet}) {
        INFO("feature " << feature);
        checkSound(featureBody(regenerator, feature));
    }
    const reference::ModelFingerprint print = requireFingerprint(m.document, regenerator);
    checkSound(onlyBody(print));
    CHECK(print.featureCount == 6);
}

TEST_CASE("ReferenceModel_FlangeMatchesAnalyticVolume", "[reference][flange][acceptance]") {
    reference::FlangeModel m = buildFlange();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const FlangeSize s;

    SECTION("before the edge treatments: V = pi/4 (Do^2 - Dbore^2 - N Dhole^2) t") {
        const double base = pi / 4.0 * 100.0 * 100.0 * 12.0;
        const double centre = pi / 4.0 * 30.0 * 30.0 * 12.0;
        const double bolts = 6.0 * pi / 4.0 * 8.0 * 8.0 * 12.0;
        const double volume = base - centre - bolts;
        const an::Solid patterned = expected(s, Stage::Patterned);
        CHECK_THAT(patterned.volume, WithinRel(volume, 1e-14));
        const reference::BodyFingerprint body = featureBody(regenerator, m.boltCircle);
        checkProperties(body, patterned);
        checkBounds(body, lowCorner(s), highCorner(s));
    }
    SECTION("each step of the chain") {
        const std::vector<std::pair<ObjectId, Stage>> steps{{m.disc, Stage::Disc},
                                                            {m.bore, Stage::Bored},
                                                            {m.boltHole, Stage::OneBoltHole},
                                                            {m.boltCircle, Stage::Patterned},
                                                            {m.edgeChamfers, Stage::Chamfered},
                                                            {m.rimFillet, Stage::Filleted}};
        for (const auto& [feature, stage] : steps) {
            INFO("feature " << feature);
            const reference::BodyFingerprint body = featureBody(regenerator, feature);
            checkProperties(body, expected(s, stage));
            checkBounds(body, lowCorner(s), highCorner(s));
        }
    }
    SECTION("the finished flange") {
        const reference::BodyFingerprint body = onlyBody(requireFingerprint(m.document, regenerator));
        const an::Solid finished = expected(s, Stage::Filleted);
        checkProperties(body, finished);
        checkBounds(body, lowCorner(s), highCorner(s));
        // The edge treatments by Pappus: the rim and bore chamfers remove
        // triangles c^2/2 at radii R - c/3 and r + c/3, the fillet the corner
        // r^2 (1 - pi/4) at R - u.
        const double r = s.outer;
        const double rb = s.bore / 2.0;
        const double c = s.chamfer;
        const double chamfers = pi * c * c * ((r - c / 3.0) + (rb + c / 3.0));
        const double fillet = 2.0 * pi * (r - an::filletCentroid(s.fillet)) * an::filletArea(s.fillet);
        CHECK_THAT(expected(s, Stage::Patterned).volume - chamfers - fillet, WithinRel(finished.volume, 1e-14));
    }
}

TEST_CASE("ReferenceModel_FlangeBoltPatternPositionsCorrect", "[reference][flange][acceptance]") {
    reference::FlangeModel m = buildFlange();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    FlangeSize s;
    // x_i = R cos(2 pi i / N), y_i = R sin(2 pi i / N), i = 0 ... N - 1.
    checkBoltHoles(*regenerator.body(m.rimFillet), s);
    SECTION("eight holes") {
        REQUIRE(m.document.setParameterValue(m.boltCount, 8.0, kUnitless).has_value());
        requireRegenerated(regenerator, m.document);
        s.count = 8;
        checkBoltHoles(*regenerator.body(m.rimFillet), s);
    }
    SECTION("on a Ø80 circle") {
        REQUIRE(m.document.setParameterValue(m.boltCircleRadius, 40_mm).has_value());
        requireRegenerated(regenerator, m.document);
        s.boltCircle = 40.0;
        checkBoltHoles(*regenerator.body(m.rimFillet), s);
    }
}

TEST_CASE("ReferenceModel_FlangeRegeneratesAfterParameterChanges", "[reference][flange][acceptance]") {
    reference::FlangeModel m = buildFlange();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const reference::ModelFingerprint baseline = requireFingerprint(m.document, regenerator);
    FlangeSize s;

    // Six holes to eight: only the pattern and what follows it are rebuilt.
    REQUIRE(m.document.setParameterValue(m.boltCount, 8.0, kUnitless).has_value());
    features::RegenerationReport report = requireRegenerated(regenerator, m.document);
    CHECK(report.regenerated == std::vector<ObjectId>{m.boltCircle, m.edgeChamfers, m.rimFillet});
    s.count = 8;
    checkProperties(onlyBody(requireFingerprint(m.document, regenerator)), expected(s, Stage::Filleted));
    checkBoltHoles(*regenerator.body(m.rimFillet), s);

    // Bolt circle Ø70 to Ø80: the first hole moves and the pattern follows;
    // the bore and the edge treatments are unchanged.
    REQUIRE(m.document.setParameterValue(m.boltCircleRadius, 40_mm).has_value());
    report = requireRegenerated(regenerator, m.document);
    CHECK(report.regenerated == std::vector<ObjectId>{m.boltHole, m.boltCircle, m.edgeChamfers, m.rimFillet});
    s.boltCircle = 40.0;
    const reference::BodyFingerprint body = onlyBody(requireFingerprint(m.document, regenerator));
    checkProperties(body, expected(s, Stage::Filleted));
    checkBounds(body, lowCorner(s), highCorner(s));
    checkBoltHoles(*regenerator.body(m.rimFillet), s);
    CHECK(circleEdges(*regenerator.body(m.rimFillet), {0.0, 0.0, 0.0}, Direction3D::unitZ(), 15.0) == 1);

    // Back to the baseline: bit for bit (no sketch depends on these).
    REQUIRE(m.document.setParameterValue(m.boltCount, 6.0, kUnitless).has_value());
    REQUIRE(m.document.setParameterValue(m.boltCircleRadius, 35_mm).has_value());
    requireRegenerated(regenerator, m.document);
    CHECK(requireFingerprint(m.document, regenerator) == baseline);

    // A larger outer diameter moves the rim: the disc, bore and holes follow,
    // but the chamfer refers to the top rim by its circle of radius 50 mm,
    // which the body no longer has (see geometry::EdgeSignature). It fails
    // with NotFound; no other edge is used. Restoring the diameter restores
    // the model, to rounding (the sketch is solved again).
    REQUIRE(m.document.setParameterValue(m.outerRadius, 55_mm).has_value());
    auto moved = regenerator.regenerate(m.document);
    REQUIRE(moved.has_value());
    CHECK(moved->failed == std::vector<ObjectId>{m.edgeChamfers});
    CHECK(moved->blocked == std::vector<ObjectId>{m.rimFillet});
    CHECK(moved->errors.at(m.edgeChamfers).code == ErrorCode::NotFound);
    CHECK_THAT(moved->errors.at(m.edgeChamfers).message, ContainsSubstring("matches no edge of the body"));
    FlangeSize wider;
    wider.outer = 55.0;
    checkProperties(featureBody(regenerator, m.boltCircle), expected(wider, Stage::Patterned));
    REQUIRE(m.document.setParameterValue(m.outerRadius, 50_mm).has_value());
    requireRegenerated(regenerator, m.document);
    const reference::FingerprintDifference back =
        reference::compare(baseline, requireFingerprint(m.document, regenerator));
    CHECK(back.sameStructure);
    CHECK(back.volumeAreaRelative <= 1e-12);
    CHECK(back.positionMm <= 1e-9);
}

TEST_CASE("ReferenceModel_FlangeRegeneratesDeterministically", "[reference][flange]") {
    reference::FlangeModel m = buildFlange();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const reference::ModelFingerprint baseline = requireFingerprint(m.document, regenerator);
    checkRepeatedRegeneration(m.document, baseline, 10);
    for (int i = 0; i < 3; ++i) {
        reference::FlangeModel again = buildFlange();
        features::Regenerator fresh;
        requireRegenerated(fresh, again.document);
        CHECK(requireFingerprint(again.document, fresh) == baseline);
    }
    // A change of the bolt circle and its reversal: the original, bit for
    // bit (no sketch is solved again).
    REQUIRE(m.document.setParameterValue(m.boltCircleRadius, 40_mm).has_value());
    requireRegenerated(regenerator, m.document);
    CHECK_FALSE(requireFingerprint(m.document, regenerator) == baseline);
    REQUIRE(m.document.setParameterValue(m.boltCircleRadius, 35_mm).has_value());
    requireRegenerated(regenerator, m.document);
    CHECK(requireFingerprint(m.document, regenerator) == baseline);
}

TEST_CASE("ReferenceModel_FlangeSaveLoad", "[reference][flange][io]") {
    reference::FlangeModel m = buildFlange();
    TempDir dir;
    checkSaveLoad(m.document, dir.path() / "flange.bcad");
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    REQUIRE(m.document.setParameterValue(m.boltCount, 8.0, kUnitless).has_value());
    requireRegenerated(regenerator, m.document);
    FlangeSize s;
    s.count = 8;
    checkProperties(onlyBody(requireFingerprint(m.document, regenerator)), expected(s, Stage::Filleted));
}

TEST_CASE("ReferenceModel_FlangeUndoRedo", "[reference][flange]") {
    reference::FlangeModel m = buildFlange();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const reference::ModelFingerprint baseline = requireFingerprint(m.document, regenerator);
    const Document original = m.document.clone();
    CommandHistory history;

    ParameterChanges eight;
    eight.value = DimensionedValue{dimensions::dimensionless, 8.0};
    execute(history, m.document, std::make_unique<ModifyParameterCommand>(m.boltCount, eight));
    execute(history, m.document, ModifyParameterCommand::setValue(m.boltCircleRadius, 40_mm));
    requireRegenerated(regenerator, m.document);
    FlangeSize s;
    s.count = 8;
    s.boltCircle = 40.0;
    const reference::ModelFingerprint changed = requireFingerprint(m.document, regenerator);
    checkProperties(onlyBody(changed), expected(s, Stage::Filleted));

    REQUIRE(history.undo(m.document).has_value());
    REQUIRE(history.undo(m.document).has_value());
    CHECK(equivalent(m.document, original));
    requireRegenerated(regenerator, m.document);
    CHECK(requireFingerprint(m.document, regenerator) == baseline);
    REQUIRE(history.redo(m.document).has_value());
    REQUIRE(history.redo(m.document).has_value());
    requireRegenerated(regenerator, m.document);
    CHECK(requireFingerprint(m.document, regenerator) == changed);
}

TEST_CASE("ReferenceModel_FlangeExports", "[reference][flange][io][export]") {
    reference::FlangeModel m = buildFlange();
    TempDir dir;
    const FlangeSize s;
    const an::Solid finished = expected(s, Stage::Filleted);
    checkStepExport(m.document, dir.path() / "flange.step", finished.volume, lowCorner(s), highCorner(s));
    checkStlExport(m.document, dir.path() / "flange.stl", finished.volume, finished.area);
}

TEST_CASE("ReferenceModel_FlangeFailsSafelyOnInvalidParameters", "[reference][flange]") {
    reference::FlangeModel m = buildFlange();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const reference::ModelFingerprint baseline = requireFingerprint(m.document, regenerator);
    const Document original = m.document.clone();
    const FlangeSize s;

    // A Ø96 bolt circle puts the Ø8 holes through the rim (they would reach
    // r = 52 mm on a 50 mm flange): the hole is refused, not built, and the
    // pattern and edge treatments after it are blocked.
    REQUIRE(m.document.setParameterValue(m.boltCircleRadius, 48_mm).has_value());
    auto report = regenerator.regenerate(m.document);
    REQUIRE(report.has_value());
    CHECK(report->failed == std::vector<ObjectId>{m.boltHole});
    CHECK(report->blocked == std::vector<ObjectId>{m.boltCircle, m.edgeChamfers, m.rimFillet});
    const Error& error = report->errors.at(m.boltHole);
    INFO(error.message);
    CHECK(error.code == ErrorCode::FailedPrecondition);
    CHECK_THAT(error.message, ContainsSubstring("BoltHole"));
    for (const ObjectId feature : {m.boltHole, m.boltCircle, m.edgeChamfers, m.rimFillet}) {
        CHECK(regenerator.body(feature) == nullptr);
    }
    checkProperties(featureBody(regenerator, m.bore), expected(s, Stage::Bored));
    CHECK_FALSE(features::validateDocument(m.document).valid());
    REQUIRE(m.document.setParameterValue(m.boltCircleRadius, 35_mm).has_value());
    CHECK(equivalent(m.document, original));
    requireRegenerated(regenerator, m.document);
    CHECK(requireFingerprint(m.document, regenerator) == baseline);
}
