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

/// The bracket's dimensions in mm, as the builder sets them.
struct BracketSize {
    double width = 100.0;
    double baseWidth = 60.0;
    double baseThickness = 10.0;
    double plateHeight = 70.0;
    double plateThickness = 10.0;
    double holeDiameter = 8.0;
    double holeInset = 15.0;
    double holeRow = 25.0;
    double holePitchX = 70.0;
    double holePitchY = 20.0;
    double plateHoleX = 20.0;
    double plateHoleZ = 45.0;
    double centreX = 50.0;
    double gussetBack = 5.0;
    double gussetEmbed = 5.0;
    double gussetTop = 50.0;
    double footThickness = 10.0;
    double footDepth = 45.0;
    double tipThickness = 6.0;
    double tipDepth = 7.0;
    double fillet = 5.0;
    double chamfer = 2.0;
};

/// How far along its feature chain the bracket is.
enum class Stage { Base, Joined, Drilled, Patterned, PlateDrilled, PlateMirrored, Filleted, Chamfered, Gusseted };

/// The bracket's L-shaped cross-section after @p stage: u is y (depth), v is
/// z (height). The bracket is this section swept the full width, less the
/// holes and plus the gusset.
an::Loop section(const BracketSize& s, Stage stage) {
    const bool joined = stage >= Stage::Joined;
    const bool fillets = stage >= Stage::Filleted;
    const bool chamfers = stage >= Stage::Chamfered;
    const double t = s.baseThickness;
    const double p = s.plateThickness;
    const double f = s.fillet;
    const double c = s.chamfer;
    an::Path path(chamfers ? an::Vec2{c, 0.0} : an::Vec2{0.0, 0.0});
    path.to({s.baseWidth, 0.0}).to({s.baseWidth, t});
    if (!joined) {
        // The base alone.
        path.to({0.0, t});
        return path.close();
    }
    if (fillets) {
        path.to({p + f, t}).arcTo({p + f, t + f}, f, 270.0, 180.0);
    } else {
        path.to({p, t});
    }
    path.to({p, s.plateHeight}).to({0.0, s.plateHeight});
    if (chamfers) {
        path.to({0.0, c});
    } else {
        path.to({0.0, 0.0});
    }
    return path.close();
}

/// The gusset's half thickness and depth at height @p z, interpolated
/// between its two sections (the loft is ruled, so both run linearly).
double gussetHalfThickness(const BracketSize& s, double z) {
    const double t = (z - s.gussetEmbed) / (s.gussetTop - s.gussetEmbed);
    return (s.footThickness + (s.tipThickness - s.footThickness) * t) / 2.0;
}
double gussetFront(const BracketSize& s, double z) {
    const double t = (z - s.gussetEmbed) / (s.gussetTop - s.gussetEmbed);
    return s.gussetBack + s.footDepth + (s.tipDepth - s.footDepth) * t;
}

/// The fillet's cross-section at the inside corner: the square f x f in the
/// corner less the quarter disc.
an::Loop filletSpandrel(const BracketSize& s) {
    const double p = s.plateThickness;
    const double t = s.baseThickness;
    const double f = s.fillet;
    an::Path path({p, t});
    path.to({p + f, t}).arcTo({p + f, t + f}, f, 270.0, 180.0);
    return path.close();
}

/// Volume and moments of what the gusset adds: the part of the lofted rib
/// that is outside the base (z > base_t) and in front of the plate
/// (y > plate_t), less what the inside fillet already fills there.
struct GussetContribution {
    double volume = 0.0;
    double momentY = 0.0;
    double momentZ = 0.0;
};

GussetContribution gussetAdds(const BracketSize& s) {
    const double t = s.baseThickness;
    const double p = s.plateThickness;
    const auto width = [&](double z) { return 2.0 * gussetHalfThickness(s, z); };
    const auto front = [&](double z) { return gussetFront(s, z); };
    GussetContribution g;
    g.volume = an::integrate([&](double z) { return width(z) * (front(z) - p); }, t, s.gussetTop);
    g.momentY = an::integrate(
        [&](double z) { return width(z) * (front(z) * front(z) - p * p) / 2.0; }, t, s.gussetTop);
    g.momentZ = an::integrate([&](double z) { return width(z) * (front(z) - p) * z; }, t, s.gussetTop);
    // The rib passes through the fillet, which is already there. The rib's
    // width runs linearly with z, so its average over the fillet's section is
    // its width at that section's centroid.
    const std::vector<an::Loop> spandrel{filletSpandrel(s)};
    const double area = an::moment(spandrel, 0, 0);
    const double centroidZ = an::moment(spandrel, 0, 1) / area;
    const double centroidY = an::moment(spandrel, 1, 0) / area;
    g.volume -= width(centroidZ) * area;
    g.momentY -= width(centroidZ) * area * centroidY;
    g.momentZ -= width(centroidZ) * area * centroidZ;
    return g;
}

std::size_t baseHolesAt(Stage stage) {
    if (stage == Stage::Drilled) {
        return 1;
    }
    return stage >= Stage::Patterned ? 4 : 0;
}
std::size_t plateHolesAt(Stage stage) {
    if (stage == Stage::PlateDrilled) {
        return 1;
    }
    return stage >= Stage::PlateMirrored ? 2 : 0;
}

/// The L section swept the full width, less the holes, plus the gusset.
an::Solid expected(const BracketSize& s, Stage stage) {
    const an::Loop loop = section(s, stage);
    REQUIRE(an::largestGap(loop) < 1e-12);
    const std::vector<an::Loop> loops{loop};
    const double area = an::moment(loops, 0, 0);
    const double rh = s.holeDiameter / 2.0;
    const double baseHole = pi * rh * rh * s.baseThickness;
    const double plateHole = pi * rh * rh * s.plateThickness;
    const double baseHoles = static_cast<double>(baseHolesAt(stage));
    const double plateHoles = static_cast<double>(plateHolesAt(stage));
    const GussetContribution gusset = stage >= Stage::Gusseted ? gussetAdds(s) : GussetContribution{};

    an::Solid result;
    result.volume = area * s.width - baseHoles * baseHole - plateHoles * plateHole + gusset.volume;
    // Along the width: the section at both ends, the sides swept, each
    // hole's wall for its two openings, and the gusset's own surface, which
    // is not worth an exact figure here (see checkProperties(..., false)).
    result.area = 0.0;
    // y: the base holes sit on their two rows, the plate holes in the plate.
    const double holeRows = baseHoles == 1.0 ? s.holeRow : s.holeRow + s.holePitchY / 2.0;
    const double momentY = an::moment(loops, 1, 0) * s.width - baseHoles * baseHole * holeRows -
                           plateHoles * plateHole * s.plateThickness / 2.0 + gusset.momentY;
    const double momentZ = an::moment(loops, 0, 1) * s.width - baseHoles * baseHole * s.baseThickness / 2.0 -
                           plateHoles * plateHole * s.plateHoleZ + gusset.momentZ;
    // x: the bracket is symmetric about its middle once both plate holes and
    // all four base holes are there; before that the single holes pull it.
    double momentX = area * s.width * s.width / 2.0;
    if (baseHoles == 1.0) {
        momentX -= baseHole * s.holeInset;
    } else {
        momentX -= baseHoles * baseHole * s.width / 2.0;
    }
    if (plateHoles == 1.0) {
        momentX -= plateHole * s.plateHoleX;
    } else {
        momentX -= plateHoles * plateHole * s.width / 2.0;
    }
    momentX += gusset.volume * s.centreX;
    result.centroid = {momentX / result.volume, momentY / result.volume, momentZ / result.volume};
    return result;
}

std::array<double, 3> lowCorner() {
    return {0.0, 0.0, 0.0};
}
std::array<double, 3> highCorner(const BracketSize& s, Stage stage = Stage::Gusseted) {
    return {s.width, s.baseWidth, stage == Stage::Base ? s.baseThickness : s.plateHeight};
}

/// How close the kernel's centroid comes to the exact one, in mm. The
/// gusset is a loft, whose sides the kernel keeps as B-spline surfaces even
/// where they are flat (see docs/verification/P11-FEAT-009): its centroid
/// then differs by up to 3.4e-6 mm (measured), where the rest of the bracket
/// agrees to rounding.
double centroidTolerance(Stage stage) {
    return stage >= Stage::Gusseted ? 1e-5 : refmodel::kPositionMm;
}

void checkBracket(const reference::BodyFingerprint& body, const BracketSize& s, Stage stage) {
    checkProperties(body, expected(s, stage), /*checkArea=*/false, centroidTolerance(stage));
    checkBounds(body, lowCorner(), highCorner(s, stage));
}

reference::MountingBracketModel buildBracket() {
    auto built = reference::buildMountingBracketReferenceModel();
    INFO((built ? std::string{} : built.error().message));
    REQUIRE(built.has_value());
    return std::move(*built);
}

std::vector<ObjectId> chain(const reference::MountingBracketModel& m) {
    return {m.baseSection, m.basePlate,  m.plateSection, m.backPlate,   m.baseHole,    m.baseHoles, m.plateHole,
            m.plateHoles,  m.innerFillet, m.outerChamfer, m.gussetFoot, m.gussetTip,  m.gusset};
}

void checkHoles(const geometry::Body& body, const BracketSize& s) {
    const double rh = s.holeDiameter / 2.0;
    for (const double x : {s.holeInset, s.holeInset + s.holePitchX}) {
        for (const double y : {s.holeRow, s.holeRow + s.holePitchY}) {
            INFO("base hole at (" << x << ", " << y << ")");
            CHECK(circleEdges(body, {x, y, s.baseThickness}, Direction3D::unitZ(), rh) == 1);
        }
    }
    for (const double x : {s.plateHoleX, 2.0 * s.centreX - s.plateHoleX}) {
        INFO("plate hole at x = " << x);
        CHECK(circleEdges(body, {x, s.plateThickness, s.plateHoleZ}, Direction3D::unitY(), rh) == 1);
        CHECK(circleEdges(body, {x, 0.0, s.plateHoleZ}, Direction3D::unitY(), rh) == 1);
    }
}

} // namespace

TEST_CASE("ReferenceModel_MountingBracketBuildsValidGeometry", "[reference][bracket]") {
    reference::MountingBracketModel m = buildBracket();
    const features::ValidationReport validation = features::validateDocument(m.document);
    for (const features::ValidationIssue& issue : validation.issues) {
        INFO(issue.message);
    }
    CHECK(validation.valid());
    CHECK(validation.issues.empty());

    features::Regenerator regenerator;
    const features::RegenerationReport report = requireRegenerated(regenerator, m.document);
    CHECK(report.regenerated == chain(m));
    CHECK(features::resultFeatures(m.document) == std::vector<ObjectId>{m.gusset});
    const std::vector<std::string_view> kinds{"sketch", "extrude", "sketch", "extrude", "hole",   "linear_pattern",
                                              "hole",   "mirror",  "fillet", "chamfer", "sketch", "sketch",
                                              "loft"};
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        const DocumentObject* object = m.document.findObject(chain(m)[i]);
        REQUIRE(object != nullptr);
        CHECK(object->typeName() == kinds[i]);
    }
    for (const ObjectId feature : {m.basePlate, m.backPlate, m.baseHole, m.baseHoles, m.plateHole, m.plateHoles,
                                   m.innerFillet, m.outerChamfer, m.gusset}) {
        INFO("feature " << feature);
        checkSound(featureBody(regenerator, feature));
    }
    checkSound(onlyBody(requireFingerprint(m.document, regenerator)));
}

TEST_CASE("ReferenceModel_MountingBracketMatchesIndependentProperties", "[reference][bracket][acceptance]") {
    reference::MountingBracketModel m = buildBracket();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const BracketSize s;

    SECTION("the two plates: V = Vbase + Vvertical - Voverlap") {
        const double base = s.width * s.baseWidth * s.baseThickness;
        const double plate = s.width * s.plateThickness * s.plateHeight;
        const double overlap = s.width * s.plateThickness * s.baseThickness;
        CHECK_THAT(expected(s, Stage::Joined).volume, WithinRel(base + plate - overlap, 1e-14));
        CHECK_THAT(base + plate - overlap, WithinRel(120000.0, 1e-14));
        checkBracket(featureBody(regenerator, m.backPlate), s, Stage::Joined);
    }
    SECTION("each step of the chain") {
        const std::vector<std::pair<ObjectId, Stage>> steps{
            {m.basePlate, Stage::Base},        {m.backPlate, Stage::Joined},
            {m.baseHole, Stage::Drilled},      {m.baseHoles, Stage::Patterned},
            {m.plateHole, Stage::PlateDrilled}, {m.plateHoles, Stage::PlateMirrored},
            {m.innerFillet, Stage::Filleted},  {m.outerChamfer, Stage::Chamfered},
            {m.gusset, Stage::Gusseted}};
        for (const auto& [feature, stage] : steps) {
            INFO("feature " << feature);
            checkBracket(featureBody(regenerator, feature), s, stage);
        }
    }
    SECTION("the finished bracket") {
        checkBracket(onlyBody(requireFingerprint(m.document, regenerator)), s, Stage::Gusseted);
        checkHoles(*regenerator.body(m.gusset), s);
        // The gusset is a prismatoid: h/6 (A0 + 4 Am + A1) of rib, less the
        // part inside the plates and the fillet.
        const double h = s.gussetTop - s.gussetEmbed;
        const double a0 = s.footThickness * s.footDepth;
        const double a1 = s.tipThickness * s.tipDepth;
        const double am = (s.footThickness + s.tipThickness) / 2.0 * (s.footDepth + s.tipDepth) / 2.0;
        const double rib = h / 6.0 * (a0 + 4.0 * am + a1);
        INFO("the whole rib is " << rib << " mm^3; " << gussetAdds(s).volume << " of it is new material");
        CHECK(gussetAdds(s).volume < rib);
        CHECK_THAT(expected(s, Stage::Chamfered).volume + gussetAdds(s).volume,
                   WithinRel(expected(s, Stage::Gusseted).volume, 1e-14));
    }
}

TEST_CASE("ReferenceModel_MountingBracketRegeneratesAfterParameterChanges", "[reference][bracket][acceptance]") {
    reference::MountingBracketModel m = buildBracket();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const reference::ModelFingerprint baseline = requireFingerprint(m.document, regenerator);

    SECTION("plate height 70 -> 90 mm") {
        REQUIRE(m.document.setParameterValue(m.plateHeight, 90_mm).has_value());
        requireRegenerated(regenerator, m.document);
        BracketSize s;
        s.plateHeight = 90.0;
        checkBracket(onlyBody(requireFingerprint(m.document, regenerator)), s, Stage::Gusseted);
        // The holes, the fillet, the chamfer and the gusset stay where they
        // belong; only the plate is taller.
        checkHoles(*regenerator.body(m.gusset), s);
        CHECK(lineEdges(*regenerator.body(m.gusset), {0.0, 0.0, 0.0}, Direction3D::unitX()) == 0); // chamfered away
    }
    SECTION("base width 60 -> 80 mm") {
        REQUIRE(m.document.setParameterValue(m.baseWidth, 80_mm).has_value());
        requireRegenerated(regenerator, m.document);
        BracketSize s;
        s.baseWidth = 80.0;
        checkBracket(onlyBody(requireFingerprint(m.document, regenerator)), s, Stage::Gusseted);
        checkHoles(*regenerator.body(m.gusset), s);
    }
    SECTION("a taller gusset") {
        REQUIRE(m.document.setParameterValue(m.gussetTop, 60_mm).has_value());
        const features::RegenerationReport report = requireRegenerated(regenerator, m.document);
        CHECK(report.regenerated == std::vector<ObjectId>{m.gusset});
        BracketSize s;
        s.gussetTop = 60.0;
        checkBracket(onlyBody(requireFingerprint(m.document, regenerator)), s, Stage::Gusseted);
    }

    // Restored and regenerated: the bracket we started from, to rounding.
    for (const auto& [parameter, value] : std::vector<std::pair<ParameterId, Length>>{
             {m.plateHeight, 70_mm}, {m.baseWidth, 60_mm}, {m.gussetTop, 50_mm}}) {
        REQUIRE(m.document.setParameterValue(parameter, value).has_value());
    }
    requireRegenerated(regenerator, m.document);
    const reference::FingerprintDifference back =
        reference::compare(baseline, requireFingerprint(m.document, regenerator));
    INFO("restored: volume and area " << back.volumeAreaRelative << " relative, positions " << back.positionMm
                                      << " mm");
    CHECK(back.sameStructure);
    CHECK(back.volumeAreaRelative <= 1e-12);
    CHECK(back.positionMm <= 1e-9);
}

TEST_CASE("ReferenceModel_MountingBracketRegeneratesDeterministically", "[reference][bracket]") {
    reference::MountingBracketModel m = buildBracket();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const reference::ModelFingerprint baseline = requireFingerprint(m.document, regenerator);
    checkRepeatedRegeneration(m.document, baseline, 10);
    for (int i = 0; i < 3; ++i) {
        reference::MountingBracketModel again = buildBracket();
        features::Regenerator fresh;
        requireRegenerated(fresh, again.document);
        CHECK(requireFingerprint(again.document, fresh) == baseline);
    }
}

TEST_CASE("ReferenceModel_MountingBracketSaveLoad", "[reference][bracket][io]") {
    reference::MountingBracketModel m = buildBracket();
    TempDir dir;
    checkSaveLoad(m.document, dir.path() / "mounting_bracket.bcad");
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    REQUIRE(m.document.setParameterValue(m.plateHeight, 90_mm).has_value());
    requireRegenerated(regenerator, m.document);
    BracketSize s;
    s.plateHeight = 90.0;
    checkBracket(onlyBody(requireFingerprint(m.document, regenerator)), s, Stage::Gusseted);
}

TEST_CASE("ReferenceModel_MountingBracketUndoRedo", "[reference][bracket]") {
    reference::MountingBracketModel m = buildBracket();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const reference::ModelFingerprint baseline = requireFingerprint(m.document, regenerator);
    const Document original = m.document.clone();
    CommandHistory history;

    execute(history, m.document, ModifyParameterCommand::setValue(m.gussetTop, 60_mm));
    requireRegenerated(regenerator, m.document);
    BracketSize s;
    s.gussetTop = 60.0;
    const reference::ModelFingerprint changed = requireFingerprint(m.document, regenerator);
    checkBracket(onlyBody(changed), s, Stage::Gusseted);
    // The gusset's height drives a loft section offset, not a sketch, so
    // undo and redo are exact.
    REQUIRE(history.undo(m.document).has_value());
    CHECK(equivalent(m.document, original));
    requireRegenerated(regenerator, m.document);
    CHECK(requireFingerprint(m.document, regenerator) == baseline);
    REQUIRE(history.redo(m.document).has_value());
    requireRegenerated(regenerator, m.document);
    CHECK(requireFingerprint(m.document, regenerator) == changed);
}

TEST_CASE("ReferenceModel_MountingBracketExports", "[reference][bracket][io][export]") {
    reference::MountingBracketModel m = buildBracket();
    TempDir dir;
    const BracketSize s;
    const an::Solid finished = expected(s, Stage::Gusseted);
    checkStepExport(m.document, dir.path() / "mounting_bracket.step", finished.volume, lowCorner(), highCorner(s));
    // The STL bound needs the kernel's area, which the section alone does not
    // give for the gusset.
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const double area = onlyBody(requireFingerprint(m.document, regenerator)).areaMm2;
    checkStlExport(m.document, dir.path() / "mounting_bracket.stl", finished.volume, area);
}

TEST_CASE("ReferenceModel_MountingBracketFailsSafelyOnInvalidParameters", "[reference][bracket]") {
    reference::MountingBracketModel m = buildBracket();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const reference::ModelFingerprint baseline = requireFingerprint(m.document, regenerator);
    const Document original = m.document.clone();
    const BracketSize s;

    // A hole 3 mm from the edge would break out of the base: it is refused
    // before anything is built, and everything after it is blocked.
    REQUIRE(m.document.setParameterValue(m.holeInset, 3_mm).has_value());
    auto report = regenerator.regenerate(m.document);
    REQUIRE(report.has_value());
    CHECK(report->failed == std::vector<ObjectId>{m.baseHole});
    CHECK(report->blocked ==
          std::vector<ObjectId>{m.baseHoles, m.plateHole, m.plateHoles, m.innerFillet, m.outerChamfer, m.gusset});
    const Error& error = report->errors.at(m.baseHole);
    INFO(error.message);
    CHECK(error.code == ErrorCode::FailedPrecondition);
    CHECK_THAT(error.message, ContainsSubstring("BaseHole"));
    for (const ObjectId feature : {m.baseHole, m.baseHoles, m.plateHoles, m.gusset}) {
        CHECK(regenerator.body(feature) == nullptr);
    }
    checkBracket(featureBody(regenerator, m.backPlate), s, Stage::Joined);
    CHECK_FALSE(features::validateDocument(m.document).valid());

    // Recovery: the bracket we started from, bit for bit.
    REQUIRE(m.document.setParameterValue(m.holeInset, 15_mm).has_value());
    CHECK(equivalent(m.document, original));
    requireRegenerated(regenerator, m.document);
    CHECK(requireFingerprint(m.document, regenerator) == baseline);
}
