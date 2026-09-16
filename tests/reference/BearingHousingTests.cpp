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

/// The housing's dimensions in mm, as the builder sets them.
struct HousingSize {
    double halfLength = 60.0;
    double width = 60.0;
    double thickness = 12.0;
    double boss = 35.0;
    double axisHeight = 45.0;
    double bore = 20.0;
    double mountDiameter = 10.0;
    double mountX = 48.0;
    double mountY = 15.0;
    double fillet = 3.0;
    double chamfer = 1.5;
};

/// How far along its feature chain the housing is.
enum class Stage { Half, Whole, Bored, Drilled, Patterned, Filleted, Chamfered };

/// The housing's front section after @p stage: u is x, v is z. The part is
/// this section extruded along Y, less the mounting holes.
std::vector<an::Loop> section(const HousingSize& s, Stage stage) {
    const bool whole = stage >= Stage::Whole;
    const bool fillets = stage >= Stage::Filleted;
    const bool chamfers = stage >= Stage::Chamfered;
    const double t = s.thickness;
    const double f = s.fillet;
    const double c = s.chamfer;
    const double x = s.halfLength;
    // The right half, from the middle of the base along the bottom.
    an::Path p({0.0, 0.0});
    p.to({x, 0.0});
    if (chamfers) {
        p.to({x, t - c}).to({x - c, t});
    } else {
        p.to({x, t});
    }
    if (fillets) {
        p.to({s.boss + f, t}).arcTo({s.boss + f, t + f}, f, 270.0, 180.0);
    } else {
        p.to({s.boss, t});
    }
    p.to({s.boss, s.axisHeight});
    if (whole) {
        // Over the crown and down the other side, mirrored.
        p.arcTo({0.0, s.axisHeight}, s.boss, 0.0, 180.0).to({-s.boss, t + (fillets ? f : 0.0)});
        if (fillets) {
            p.arcTo({-s.boss - f, t + f}, f, 0.0, -90.0);
        }
        p.to({-x + (chamfers ? c : 0.0), t});
        if (chamfers) {
            p.to({-x, t - c});
        }
        p.to({-x, 0.0});
    } else {
        p.arcTo({0.0, s.axisHeight}, s.boss, 0.0, 90.0).to({0.0, 0.0});
    }
    std::vector<an::Loop> loops{p.close()};
    if (stage >= Stage::Bored) {
        loops.push_back(an::circle({0.0, s.axisHeight}, s.bore, /*clockwise=*/true));
    }
    return loops;
}

std::size_t holesAt(Stage stage) {
    if (stage == Stage::Drilled) {
        return 1;
    }
    return stage >= Stage::Patterned ? 4 : 0;
}

/// The section extruded along Y, less the mounting holes: cylinders of
/// radius mount_d/2 through the base, whose four positions add up to the
/// middle. Areas: the two end faces, the sides swept along the width, and
/// each hole's wall in place of its two openings.
an::Solid expected(const HousingSize& s, Stage stage) {
    const std::vector<an::Loop> loops = section(s, stage);
    for (const an::Loop& loop : loops) {
        REQUIRE(an::largestGap(loop) < 1e-12);
    }
    const double area = an::moment(loops, 0, 0);
    const double width = s.width;
    const double rh = s.mountDiameter / 2.0;
    const double hole = pi * rh * rh * s.thickness;
    const double holes = static_cast<double>(holesAt(stage));
    const double volume = area * width - holes * hole;
    an::Solid result;
    result.volume = volume;
    result.area = 2.0 * area + an::perimeter(loops) * width - holes * 2.0 * pi * rh * rh +
                  holes * 2.0 * pi * rh * s.thickness;
    result.centroid = {(an::moment(loops, 1, 0) * width - holes * hole * (stage == Stage::Drilled ? s.mountX : 0.0)) /
                           volume,
                       (holes == 1.0 ? -hole * s.mountY / volume : 0.0),
                       (an::moment(loops, 0, 1) * width - holes * hole * s.thickness / 2.0) / volume};
    return result;
}

std::array<double, 3> lowCorner(const HousingSize& s) {
    return {-s.halfLength, -s.width / 2.0, 0.0};
}
std::array<double, 3> highCorner(const HousingSize& s) {
    return {s.halfLength, s.width / 2.0, s.axisHeight + s.boss};
}

reference::BearingHousingModel buildHousing() {
    auto built = reference::buildBearingHousingReferenceModel();
    INFO((built ? std::string{} : built.error().message));
    REQUIRE(built.has_value());
    return std::move(*built);
}

std::vector<ObjectId> chain(const reference::BearingHousingModel& m) {
    return {m.baseSection, m.base,      m.bossSection, m.boss,        m.housing,
            m.boreSection, m.bore,      m.mountHole,   m.mountHoles,  m.bossFillets,
            m.baseChamfers};
}

/// The four mounting holes are at (+-mount_x, +-mount_y) through the base.
void checkMountHoles(const geometry::Body& body, const HousingSize& s) {
    for (const double x : {-s.mountX, s.mountX}) {
        for (const double y : {-s.mountY, s.mountY}) {
            INFO("hole at (" << x << ", " << y << ")");
            CHECK(circleEdges(body, {x, y, s.thickness}, Direction3D::unitZ(), s.mountDiameter / 2.0) == 1);
            CHECK(circleEdges(body, {x, y, 0.0}, Direction3D::unitZ(), s.mountDiameter / 2.0) == 1);
        }
    }
}

} // namespace

TEST_CASE("ReferenceModel_BearingHousingBuildsValidGeometry", "[reference][housing]") {
    reference::BearingHousingModel m = buildHousing();
    const features::ValidationReport validation = features::validateDocument(m.document);
    for (const features::ValidationIssue& issue : validation.issues) {
        INFO(issue.message);
    }
    CHECK(validation.valid());
    CHECK(validation.issues.empty());

    features::Regenerator regenerator;
    const features::RegenerationReport report = requireRegenerated(regenerator, m.document);
    CHECK(report.regenerated == chain(m));
    CHECK(features::resultFeatures(m.document) == std::vector<ObjectId>{m.baseChamfers});
    const std::vector<std::string_view> kinds{"sketch", "extrude", "sketch",         "extrude", "mirror", "sketch",
                                              "extrude", "hole",   "linear_pattern", "fillet",  "chamfer"};
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        const DocumentObject* object = m.document.findObject(chain(m)[i]);
        REQUIRE(object != nullptr);
        CHECK(object->typeName() == kinds[i]);
    }
    for (const ObjectId feature :
         {m.base, m.boss, m.housing, m.bore, m.mountHole, m.mountHoles, m.bossFillets, m.baseChamfers}) {
        INFO("feature " << feature);
        checkSound(featureBody(regenerator, feature));
    }
    checkSound(onlyBody(requireFingerprint(m.document, regenerator)));
}

TEST_CASE("ReferenceModel_BearingHousingMatchesIndependentProperties", "[reference][housing][acceptance]") {
    reference::BearingHousingModel m = buildHousing();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const HousingSize s;

    SECTION("the halves and the whole blank") {
        // Base plus arch, less what they share: V = (L T + (2 R H + pi R^2 / 2)
        // - 2 R T) W for the whole blank, half of it for each half.
        const double arch = 2.0 * s.boss * s.axisHeight + pi * s.boss * s.boss / 2.0;
        const double blank = (2.0 * s.halfLength * s.thickness + arch - 2.0 * s.boss * s.thickness) * s.width;
        CHECK_THAT(expected(s, Stage::Whole).volume, WithinRel(blank, 1e-14));
        checkProperties(featureBody(regenerator, m.boss), expected(s, Stage::Half));
        const reference::BodyFingerprint whole = featureBody(regenerator, m.housing);
        checkProperties(whole, expected(s, Stage::Whole));
        checkBounds(whole, lowCorner(s), highCorner(s));
        // The mirror image is exactly the same size as the half it came from.
        CHECK_THAT(whole.volumeMm3, WithinRel(2.0 * featureBody(regenerator, m.boss).volumeMm3, 1e-12));
    }
    SECTION("each step of the chain") {
        const std::vector<std::pair<ObjectId, Stage>> steps{{m.housing, Stage::Whole},
                                                            {m.bore, Stage::Bored},
                                                            {m.mountHole, Stage::Drilled},
                                                            {m.mountHoles, Stage::Patterned},
                                                            {m.bossFillets, Stage::Filleted},
                                                            {m.baseChamfers, Stage::Chamfered}};
        for (const auto& [feature, stage] : steps) {
            INFO("feature " << feature);
            const reference::BodyFingerprint body = featureBody(regenerator, feature);
            checkProperties(body, expected(s, stage));
            checkBounds(body, lowCorner(s), highCorner(s));
        }
    }
    SECTION("the finished housing") {
        const reference::BodyFingerprint body = onlyBody(requireFingerprint(m.document, regenerator));
        checkProperties(body, expected(s, Stage::Chamfered));
        checkBounds(body, lowCorner(s), highCorner(s));
        checkMountHoles(*regenerator.body(m.baseChamfers), s);
        // The bore runs right through, and its axis is where it belongs.
        CHECK(circleEdges(*regenerator.body(m.baseChamfers), {0.0, -s.width / 2.0, s.axisHeight},
                          Direction3D::unitY(), s.bore) == 1);
        CHECK(circleEdges(*regenerator.body(m.baseChamfers), {0.0, s.width / 2.0, s.axisHeight},
                          Direction3D::unitY(), s.bore) == 1);
        // The two fillets and two chamfers run the full width.
        const double fillets = 2.0 * an::filletArea(s.fillet) * s.width;
        const double chamfers = 2.0 * s.chamfer * s.chamfer / 2.0 * s.width;
        CHECK_THAT(expected(s, Stage::Patterned).volume + fillets - chamfers,
                   WithinRel(expected(s, Stage::Chamfered).volume, 1e-14));
    }
}

TEST_CASE("ReferenceModel_BearingHousingRegeneratesAfterParameterChanges", "[reference][housing][acceptance]") {
    reference::BearingHousingModel m = buildHousing();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const reference::ModelFingerprint baseline = requireFingerprint(m.document, regenerator);

    SECTION("bearing bore 40 -> 45 mm") {
        REQUIRE(m.document.setParameterValue(m.boreRadius, 22.5_mm).has_value());
        const features::RegenerationReport report = requireRegenerated(regenerator, m.document);
        CHECK(report.regenerated ==
              std::vector<ObjectId>{m.boreSection, m.bore, m.mountHole, m.mountHoles, m.bossFillets, m.baseChamfers});
        HousingSize s;
        s.bore = 22.5;
        const reference::BodyFingerprint body = onlyBody(requireFingerprint(m.document, regenerator));
        checkProperties(body, expected(s, Stage::Chamfered));
        checkBounds(body, lowCorner(s), highCorner(s));
        const geometry::Body& housing = *regenerator.body(m.baseChamfers);
        CHECK(circleEdges(housing, {0.0, -s.width / 2.0, s.axisHeight}, Direction3D::unitY(), 22.5) == 1);
        CHECK(circleEdges(housing, {0.0, -s.width / 2.0, s.axisHeight}, Direction3D::unitY(), 20.0) == 0);
        checkMountHoles(housing, s);
    }
    SECTION("base width 60 -> 70 mm: the part stays symmetric about its middle") {
        REQUIRE(m.document.setParameterValue(m.baseWidth, 70_mm).has_value());
        const features::RegenerationReport report = requireRegenerated(regenerator, m.document);
        // The width drives the two extrude depths and the bore's cutter, not
        // any sketch, so every feature is rebuilt but no sketch is solved
        // again.
        CHECK(report.regenerated == std::vector<ObjectId>{m.base, m.boss, m.housing, m.bore, m.mountHole,
                                                          m.mountHoles, m.bossFillets, m.baseChamfers});
        HousingSize s;
        s.width = 70.0;
        const reference::BodyFingerprint body = onlyBody(requireFingerprint(m.document, regenerator));
        checkProperties(body, expected(s, Stage::Chamfered));
        checkBounds(body, lowCorner(s), highCorner(s));
        // Still symmetric: the centre of mass stays on the middle plane and
        // the holes keep their places, the bore runs through the new faces.
        CHECK(std::abs(body.centroidMm[1]) <= kPositionMm);
        checkMountHoles(*regenerator.body(m.baseChamfers), s);
        CHECK(circleEdges(*regenerator.body(m.baseChamfers), {0.0, -35.0, s.axisHeight}, Direction3D::unitY(),
                          s.bore) == 1);
    }

    // Restored: the original housing, to rounding (the sketches are solved
    // again).
    REQUIRE(m.document.setParameterValue(m.boreRadius, 20_mm).has_value());
    REQUIRE(m.document.setParameterValue(m.baseWidth, 60_mm).has_value());
    requireRegenerated(regenerator, m.document);
    const reference::FingerprintDifference back =
        reference::compare(baseline, requireFingerprint(m.document, regenerator));
    INFO("restored: volume and area " << back.volumeAreaRelative << " relative, positions " << back.positionMm
                                      << " mm");
    CHECK(back.sameStructure);
    CHECK(back.volumeAreaRelative <= 1e-12);
    CHECK(back.positionMm <= 1e-9);
}

TEST_CASE("ReferenceModel_BearingHousingRegeneratesDeterministically", "[reference][housing]") {
    reference::BearingHousingModel m = buildHousing();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const reference::ModelFingerprint baseline = requireFingerprint(m.document, regenerator);
    checkRepeatedRegeneration(m.document, baseline, 10);
    for (int i = 0; i < 3; ++i) {
        reference::BearingHousingModel again = buildHousing();
        features::Regenerator fresh;
        requireRegenerated(fresh, again.document);
        CHECK(requireFingerprint(again.document, fresh) == baseline);
    }
}

TEST_CASE("ReferenceModel_BearingHousingSaveLoad", "[reference][housing][io]") {
    reference::BearingHousingModel m = buildHousing();
    TempDir dir;
    checkSaveLoad(m.document, dir.path() / "bearing_housing.bcad");
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    REQUIRE(m.document.setParameterValue(m.baseWidth, 70_mm).has_value());
    requireRegenerated(regenerator, m.document);
    HousingSize s;
    s.width = 70.0;
    checkProperties(onlyBody(requireFingerprint(m.document, regenerator)), expected(s, Stage::Chamfered));
}

TEST_CASE("ReferenceModel_BearingHousingUndoRedo", "[reference][housing]") {
    reference::BearingHousingModel m = buildHousing();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const reference::ModelFingerprint baseline = requireFingerprint(m.document, regenerator);
    const Document original = m.document.clone();
    CommandHistory history;

    execute(history, m.document, ModifyParameterCommand::setValue(m.mountDiameter, 12_mm));
    requireRegenerated(regenerator, m.document);
    HousingSize s;
    s.mountDiameter = 12.0;
    const reference::ModelFingerprint changed = requireFingerprint(m.document, regenerator);
    checkProperties(onlyBody(changed), expected(s, Stage::Chamfered));
    // The hole diameter drives no sketch, so undo and redo are exact.
    REQUIRE(history.undo(m.document).has_value());
    CHECK(equivalent(m.document, original));
    requireRegenerated(regenerator, m.document);
    CHECK(requireFingerprint(m.document, regenerator) == baseline);
    REQUIRE(history.redo(m.document).has_value());
    requireRegenerated(regenerator, m.document);
    CHECK(requireFingerprint(m.document, regenerator) == changed);
}

TEST_CASE("ReferenceModel_BearingHousingExports", "[reference][housing][io][export]") {
    reference::BearingHousingModel m = buildHousing();
    TempDir dir;
    const HousingSize s;
    const an::Solid finished = expected(s, Stage::Chamfered);
    checkStepExport(m.document, dir.path() / "bearing_housing.step", finished.volume, lowCorner(s), highCorner(s));
    checkStlExport(m.document, dir.path() / "bearing_housing.stl", finished.volume, finished.area);
}

TEST_CASE("ReferenceModel_BearingHousingFailsSafelyOnInvalidParameters", "[reference][housing]") {
    reference::BearingHousingModel m = buildHousing();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const reference::ModelFingerprint baseline = requireFingerprint(m.document, regenerator);
    const Document original = m.document.clone();
    const HousingSize s;

    SECTION("a mounting hole off the end of the base") {
        // 58 mm from the middle puts the Ø10 hole through the end face.
        REQUIRE(m.document.setParameterValue(m.mountX, 58_mm).has_value());
        auto report = regenerator.regenerate(m.document);
        REQUIRE(report.has_value());
        CHECK(report->failed == std::vector<ObjectId>{m.mountHole});
        CHECK(report->blocked == std::vector<ObjectId>{m.mountHoles, m.bossFillets, m.baseChamfers});
        const Error& error = report->errors.at(m.mountHole);
        INFO(error.message);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        CHECK_THAT(error.message, ContainsSubstring("MountHole"));
        for (const ObjectId feature : {m.mountHole, m.mountHoles, m.bossFillets, m.baseChamfers}) {
            CHECK(regenerator.body(feature) == nullptr);
        }
        checkProperties(featureBody(regenerator, m.bore), expected(s, Stage::Bored));
        REQUIRE(m.document.setParameterValue(m.mountX, 48_mm).has_value());
    }
    SECTION("a bore that eats the whole boss") {
        // A Ø100 bore in a Ø70 boss cuts the arch away entirely, and with
        // it the edges where the boss met the base: the fillet says so
        // instead of rounding something else.
        REQUIRE(m.document.setParameterValue(m.boreRadius, 50_mm).has_value());
        auto report = regenerator.regenerate(m.document);
        REQUIRE(report.has_value());
        CHECK(report->failed == std::vector<ObjectId>{m.bossFillets});
        CHECK(report->blocked == std::vector<ObjectId>{m.baseChamfers});
        const Error& error = report->errors.at(m.bossFillets);
        INFO(error.message);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK_THAT(error.message, ContainsSubstring("BossFillets"));
        CHECK_THAT(error.message, ContainsSubstring("matches no edge of the body"));
        CHECK(regenerator.body(m.bossFillets) == nullptr);
        CHECK(regenerator.body(m.baseChamfers) == nullptr);
        REQUIRE(m.document.setParameterValue(m.boreRadius, 20_mm).has_value());
    }

    // Restored and regenerated (which solves the sketches again from the
    // restored parameters), the model is the one we started from.
    requireRegenerated(regenerator, m.document);
    CHECK(equivalent(m.document, original));
    CHECK(features::validateDocument(m.document).issues.empty());
    const reference::FingerprintDifference back =
        reference::compare(baseline, requireFingerprint(m.document, regenerator));
    CHECK(back.sameStructure);
    CHECK(back.volumeAreaRelative <= 1e-12);
    CHECK(back.positionMm <= 1e-9);
}
