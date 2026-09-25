#include "reference/ReferenceTestSupport.hpp"

#include <bettercad/features/ChamferFeature.hpp>
#include <bettercad/features/FeatureCommands.hpp>
#include <bettercad/features/FilletFeature.hpp>
#include <bettercad/features/ResultBodies.hpp>
#include <bettercad/features/Validation.hpp>

#include <catch2/matchers/catch_matchers_string.hpp>

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

/// The shaft's dimensions in mm, as the builder sets them.
struct ShaftSize {
    double r1 = 15.0;
    double l1 = 40.0;
    double r2 = 20.0;
    double l2 = 40.0;
    double r3 = 12.5;
    double half = 60.0;
    double drill = 4.0;
    double drillDepth = 10.0;
    double sink = 8.5;
    double sinkAngle = 60.0;
    double fillet = 1.5;
    double chamfer = 1.0;

    [[nodiscard]] double length() const { return 2.0 * half; }
    [[nodiscard]] double l3() const { return length() - l1 - l2; }
};

/// How far along its feature chain the shaft is.
enum class Stage { Turned, DriveEndDrilled, BothEndsDrilled, Filleted, Chamfered };

/// The half cross-section after @p stage, built from the dimensions alone:
/// u is the radius, v the height. Counter-clockwise from the axis at the
/// bottom of the drive-end hole (or at z = 0).
an::Loop section(const ShaftSize& s, Stage stage) {
    const bool driveHole = stage >= Stage::DriveEndDrilled;
    const bool tailHole = stage >= Stage::BothEndsDrilled;
    const bool fillets = stage >= Stage::Filleted;
    const bool chamfers = stage >= Stage::Chamfered;
    const double rd = s.drill / 2.0;
    const double rs = s.sink / 2.0;
    const double cone = an::countersinkDepth(s.sink, s.drill, s.sinkAngle);
    const double top = s.length();
    const double f = s.fillet;
    const double c = s.chamfer;
    const double z1 = s.l1;
    const double z2 = s.l1 + s.l2;
    an::Path p(driveHole ? an::Vec2{0.0, s.drillDepth} : an::Vec2{0.0, 0.0});
    if (driveHole) {
        p.to({rd, s.drillDepth}).to({rd, cone}).to({rs, 0.0});
    }
    if (chamfers) {
        p.to({s.r1 - c, 0.0}).to({s.r1, c});
    } else {
        p.to({s.r1, 0.0});
    }
    if (fillets) {
        p.to({s.r1, z1 - f}).arcTo({s.r1 + f, z1 - f}, f, 180.0, 90.0);
    } else {
        p.to({s.r1, z1});
    }
    p.to({s.r2, z1}).to({s.r2, z2});
    if (fillets) {
        p.to({s.r3 + f, z2}).arcTo({s.r3 + f, z2 + f}, f, 270.0, 180.0);
    } else {
        p.to({s.r3, z2});
    }
    if (chamfers) {
        p.to({s.r3, top - c}).to({s.r3 - c, top});
    } else {
        p.to({s.r3, top});
    }
    if (tailHole) {
        p.to({rs, top}).to({rd, top - cone}).to({rd, top - s.drillDepth}).to({0.0, top - s.drillDepth});
    } else {
        p.to({0.0, top});
    }
    return p.close();
}

an::Solid expected(const ShaftSize& s, Stage stage) {
    const an::Loop loop = section(s, stage);
    REQUIRE(an::largestGap(loop) < 1e-12);
    return an::revolved({loop});
}

std::array<double, 3> lowCorner(const ShaftSize& s) {
    return {-s.r2, -s.r2, 0.0};
}
std::array<double, 3> highCorner(const ShaftSize& s) {
    return {s.r2, s.r2, s.length()};
}

reference::ShaftModel buildShaft() {
    auto built = reference::buildShaftReferenceModel();
    INFO((built ? std::string{} : built.error().message));
    REQUIRE(built.has_value());
    return std::move(*built);
}

std::vector<ObjectId> chain(const reference::ShaftModel& m) {
    return {m.profile, m.turn, m.centreDrill, m.tailCentreDrill, m.shoulderFillets, m.endChamfers};
}

} // namespace

TEST_CASE("ReferenceModel_ShaftBuildsValidGeometry", "[reference][shaft]") {
    reference::ShaftModel m = buildShaft();

    // Every check of the validator passes with no warning: all references
    // resolve, the sketch is fully constrained, every feature regenerates
    // and the result is a valid solid.
    const features::ValidationReport validation = features::validateDocument(m.document);
    for (const features::ValidationIssue& issue : validation.issues) {
        INFO(issue.message);
    }
    CHECK(validation.valid());
    CHECK(validation.issues.empty());

    features::Regenerator regenerator;
    const features::RegenerationReport report = requireRegenerated(regenerator, m.document);
    CHECK(report.regenerated == chain(m));
    CHECK(m.document.parameters().size() == 10);
    CHECK(features::resultFeatures(m.document) == std::vector<ObjectId>{m.endChamfers});

    // The chain: a sketch, then five features, each building on the last.
    const std::vector<std::string_view> kinds{"sketch", "revolve", "hole", "mirror", "fillet", "chamfer"};
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        const DocumentObject* object = m.document.findObject(chain(m)[i]);
        REQUIRE(object != nullptr);
        CHECK(object->typeName() == kinds[i]);
    }
    for (const ObjectId feature : {m.turn, m.centreDrill, m.tailCentreDrill, m.shoulderFillets, m.endChamfers}) {
        INFO("feature " << feature);
        checkSound(featureBody(regenerator, feature));
    }
    const reference::ModelFingerprint print = requireFingerprint(m.document, regenerator);
    checkSound(onlyBody(print));
    CHECK(print.featureCount == 5);
}

TEST_CASE("ReferenceModel_ShaftMatchesAnalyticProperties", "[reference][shaft][acceptance]") {
    reference::ShaftModel m = buildShaft();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const ShaftSize s;

    SECTION("the turned blank: V = pi/4 (d1^2 L1 + d2^2 L2 + d3^2 L3)") {
        // Direct closed forms, independent of the section integrator.
        const double v1 = pi * s.r1 * s.r1 * s.l1;
        const double v2 = pi * s.r2 * s.r2 * s.l2;
        const double v3 = pi * s.r3 * s.r3 * s.l3();
        const double volume = pi / 4.0 * (30.0 * 30.0 * 40.0 + 40.0 * 40.0 * 40.0 + 25.0 * 25.0 * 40.0);
        CHECK_THAT(volume, WithinRel(31250.0 * pi, 1e-15));
        CHECK_THAT(v1 + v2 + v3, WithinRel(volume, 1e-15));
        const double area = 2.0 * pi * (s.r1 * s.l1 + s.r2 * s.l2 + s.r3 * s.l3()) + 2.0 * pi * s.r2 * s.r2;
        const double centroid = (v1 * s.l1 / 2.0 + v2 * (s.l1 + s.l2 / 2.0) + v3 * (s.l1 + s.l2 + s.l3() / 2.0)) /
                                volume;
        const an::Solid blank{.volume = volume, .area = area, .centroid = {0.0, 0.0, centroid}};
        // The integrator agrees with the closed forms.
        const an::Solid integrated = expected(s, Stage::Turned);
        CHECK_THAT(integrated.volume, WithinRel(volume, 1e-14));
        CHECK_THAT(integrated.area, WithinRel(area, 1e-14));
        CHECK_THAT(integrated.centroid[2], WithinAbs(centroid, 1e-12));
        const reference::BodyFingerprint turned = featureBody(regenerator, m.turn);
        checkProperties(turned, blank);
        checkBounds(turned, lowCorner(s), highCorner(s));
    }
    SECTION("each step of the chain") {
        const std::vector<std::pair<ObjectId, Stage>> steps{{m.centreDrill, Stage::DriveEndDrilled},
                                                            {m.tailCentreDrill, Stage::BothEndsDrilled},
                                                            {m.shoulderFillets, Stage::Filleted},
                                                            {m.endChamfers, Stage::Chamfered}};
        for (const auto& [feature, stage] : steps) {
            INFO("feature " << feature);
            const reference::BodyFingerprint body = featureBody(regenerator, feature);
            checkProperties(body, expected(s, stage));
            checkBounds(body, lowCorner(s), highCorner(s));
        }
    }
    SECTION("the finished shaft") {
        const reference::BodyFingerprint body = onlyBody(requireFingerprint(m.document, regenerator));
        const an::Solid finished = expected(s, Stage::Chamfered);
        checkProperties(body, finished);
        checkBounds(body, lowCorner(s), highCorner(s));
        CHECK(body.topology.solids == 1);
        // The corrections, each by Pappus, add up to the integrated section.
        const double cone = an::countersinkDepth(s.sink, s.drill, s.sinkAngle);
        const double hole = pi * cone / 3.0 * (s.sink * s.sink + s.sink * s.drill + s.drill * s.drill) / 4.0 +
                            pi * s.drill * s.drill / 4.0 * (s.drillDepth - cone);
        const double chamfers = pi * s.chamfer * s.chamfer * ((s.r1 - s.chamfer / 3.0) + (s.r3 - s.chamfer / 3.0));
        const double fillets =
            2.0 * pi * an::filletArea(s.fillet) * ((s.r1 + an::filletCentroid(s.fillet)) + (s.r3 + an::filletCentroid(s.fillet)));
        CHECK_THAT(31250.0 * pi - 2.0 * hole - chamfers + fillets, WithinRel(finished.volume, 1e-14));
    }
}

TEST_CASE("ReferenceModel_ShaftRegeneratesAfterParameterChanges", "[reference][shaft][acceptance]") {
    reference::ShaftModel m = buildShaft();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const reference::ModelFingerprint baseline = requireFingerprint(m.document, regenerator);

    SECTION("middle diameter 40 -> 50 mm: every downstream feature follows") {
        REQUIRE(m.document.setParameterValue(m.r2, 25_mm).has_value());
        const features::RegenerationReport report = requireRegenerated(regenerator, m.document);
        CHECK(report.regenerated == chain(m));
        ShaftSize s;
        s.r2 = 25.0;
        const reference::BodyFingerprint body = onlyBody(requireFingerprint(m.document, regenerator));
        checkProperties(body, expected(s, Stage::Chamfered));
        checkBounds(body, lowCorner(s), highCorner(s));
        // No stale collar: its rims are the new circles and the old ones are
        // gone; the holes are still in both ends.
        const geometry::Body& shaft = *regenerator.body(m.endChamfers);
        for (const double z : {40.0, 80.0}) {
            CHECK(circleEdges(shaft, {0.0, 0.0, z}, Direction3D::unitZ(), 25.0) == 1);
            CHECK(circleEdges(shaft, {0.0, 0.0, z}, Direction3D::unitZ(), 20.0) == 0);
        }
        CHECK(circleEdges(shaft, {0.0, 0.0, 0.0}, Direction3D::unitZ(), 4.25) == 1);
        CHECK(circleEdges(shaft, {0.0, 0.0, 120.0}, Direction3D::unitZ(), 4.25) == 1);
        checkProperties(featureBody(regenerator, m.shoulderFillets), expected(s, Stage::Filleted));

        // Back to 40 mm: the original shaft, to rounding (the sketch is
        // solved again from the 50 mm state).
        REQUIRE(m.document.setParameterValue(m.r2, 20_mm).has_value());
        requireRegenerated(regenerator, m.document);
        const reference::FingerprintDifference back =
            reference::compare(baseline, requireFingerprint(m.document, regenerator));
        CHECK(back.sameStructure);
        CHECK(back.volumeAreaRelative <= 1e-12);
        CHECK(back.positionMm <= 1e-9);
    }
    SECTION("overall length 120 -> 130 mm: the blank, holes and fillets follow; the moved chamfer edge is "
            "reported, not guessed") {
        REQUIRE(m.document.setParameterValue(m.halfLength, 65_mm).has_value());
        auto report = regenerator.regenerate(m.document);
        REQUIRE(report.has_value());
        ShaftSize s;
        s.half = 65.0;
        // Everything up to the fillets is rebuilt at the new length. The tail
        // centre hole moves with the mirror plane, which half_length drives.
        CHECK(report->failed == std::vector<ObjectId>{m.endChamfers});
        CHECK(report->blocked.empty());
        checkProperties(featureBody(regenerator, m.turn), expected(s, Stage::Turned));
        checkProperties(featureBody(regenerator, m.tailCentreDrill), expected(s, Stage::BothEndsDrilled));
        checkProperties(featureBody(regenerator, m.shoulderFillets), expected(s, Stage::Filleted));
        CHECK(circleEdges(*regenerator.body(m.tailCentreDrill), {0.0, 0.0, 130.0}, Direction3D::unitZ(), 4.25) ==
              1);
        // The chamfer refers to the tail rim by its circle at z = 120, which
        // the body no longer has. A geometric reference does not follow a
        // moved edge (see geometry::EdgeSignature): the chamfer fails with
        // NotFound and keeps no body. The rim at z = 130 is not substituted.
        const Error& error = report->errors.at(m.endChamfers);
        CHECK(error.code == ErrorCode::NotFound);
        CHECK_THAT(error.message, ContainsSubstring("EndChamfers"));
        CHECK_THAT(error.message, ContainsSubstring("matches no edge of the body"));
        CHECK_THAT(error.message, ContainsSubstring("(0, 0, 120) mm"));
        CHECK(regenerator.body(m.endChamfers) == nullptr);

        // A user re-selects the edge (an undoable command), and the shaft is
        // complete at the new length.
        CommandHistory history;
        features::ChamferDefinition moved =
            m.document.findObjectAs<features::ChamferFeature>(m.endChamfers)->definition();
        // RE-SELECT, not replace: the user is pointing this same chamfer input
        // at the edge in its new place, so the selection keeps its identity and
        // any drawing reference to the face it cuts follows it. Assigning a
        // whole ChamferEdge here would mint a new identity and strand those
        // references -- which is why ChamferEdge will not convert from a bare
        // curve implicitly (ADR-024).
        moved.edges[1].curve =
            geometry::circleSignature(Point3D{0_mm, 0_mm, 130_mm}, Direction3D::unitZ(), 12.5_mm).value();
        execute(history, m.document,
                std::make_unique<features::ModifyChamferCommand>(FeatureId::fromValue(m.endChamfers.value()), moved));
        requireRegenerated(regenerator, m.document);
        const reference::BodyFingerprint longer = onlyBody(requireFingerprint(m.document, regenerator));
        checkProperties(longer, expected(s, Stage::Chamfered));
        checkBounds(longer, lowCorner(s), highCorner(s));

        // Undo the re-selection and restore the length: the original shaft.
        REQUIRE(history.undo(m.document).has_value());
        REQUIRE(m.document.setParameterValue(m.halfLength, 60_mm).has_value());
        requireRegenerated(regenerator, m.document);
        const reference::FingerprintDifference back =
            reference::compare(baseline, requireFingerprint(m.document, regenerator));
        CHECK(back.sameStructure);
        CHECK(back.volumeAreaRelative <= 1e-12);
        CHECK(back.positionMm <= 1e-9);
    }
}

TEST_CASE("ReferenceModel_ShaftRegeneratesDeterministically", "[reference][shaft]") {
    reference::ShaftModel m = buildShaft();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const reference::ModelFingerprint baseline = requireFingerprint(m.document, regenerator);

    // Ten full rebuilds (the sketch solved and every body rebuilt each time),
    // bit for bit.
    checkRepeatedRegeneration(m.document, baseline, 10);
    // The builder gives the same model every time: IDs, names and geometry.
    for (int i = 0; i < 3; ++i) {
        reference::ShaftModel again = buildShaft();
        features::Regenerator fresh;
        requireRegenerated(fresh, again.document);
        CHECK(requireFingerprint(again.document, fresh) == baseline);
    }
    // A change and its reversal: the original, to rounding.
    REQUIRE(m.document.setParameterValue(m.r2, 25_mm).has_value());
    requireRegenerated(regenerator, m.document);
    CHECK_FALSE(requireFingerprint(m.document, regenerator) == baseline);
    REQUIRE(m.document.setParameterValue(m.r2, 20_mm).has_value());
    requireRegenerated(regenerator, m.document);
    const reference::FingerprintDifference back =
        reference::compare(baseline, requireFingerprint(m.document, regenerator));
    INFO("restored: volume and area " << back.volumeAreaRelative << " relative, positions " << back.positionMm
                                      << " mm");
    CHECK(back.sameStructure);
    CHECK(back.volumeAreaRelative <= 1e-12);
    CHECK(back.positionMm <= 1e-9);
}

TEST_CASE("ReferenceModel_ShaftSaveLoad", "[reference][shaft][io]") {
    reference::ShaftModel m = buildShaft();
    TempDir dir;
    checkSaveLoad(m.document, dir.path() / "shaft.bcad");
    // Still parametric after loading.
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    REQUIRE(m.document.setParameterValue(m.r2, 25_mm).has_value());
    requireRegenerated(regenerator, m.document);
    ShaftSize s;
    s.r2 = 25.0;
    checkProperties(onlyBody(requireFingerprint(m.document, regenerator)), expected(s, Stage::Chamfered));
}

TEST_CASE("ReferenceModel_ShaftUndoRedo", "[reference][shaft]") {
    reference::ShaftModel m = buildShaft();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const reference::ModelFingerprint baseline = requireFingerprint(m.document, regenerator);
    const Document original = m.document.clone();
    CommandHistory history;

    // A dimension that drives the sketch: undo restores the parameters
    // exactly, and the geometry to rounding (the sketch is solved again, so
    // its points may differ in the last digits).
    execute(history, m.document, ModifyParameterCommand::setValue(m.r2, 25_mm));
    requireRegenerated(regenerator, m.document);
    const reference::ModelFingerprint wide = requireFingerprint(m.document, regenerator);
    ShaftSize wider;
    wider.r2 = 25.0;
    checkProperties(onlyBody(wide), expected(wider, Stage::Chamfered));
    REQUIRE(history.undo(m.document).has_value());
    requireRegenerated(regenerator, m.document);
    CHECK(equivalent(m.document.parameters(), original.parameters()));
    reference::FingerprintDifference back = reference::compare(baseline, requireFingerprint(m.document, regenerator));
    CHECK(back.sameStructure);
    CHECK(back.volumeAreaRelative <= 1e-12);
    CHECK(back.positionMm <= 1e-9);
    REQUIRE(history.redo(m.document).has_value());
    requireRegenerated(regenerator, m.document);
    back = reference::compare(wide, requireFingerprint(m.document, regenerator));
    CHECK(back.sameStructure);
    CHECK(back.volumeAreaRelative <= 1e-12);
    CHECK(back.positionMm <= 1e-9);

    // A dimension of a later feature: nothing is solved again, so undo and
    // redo are exact, for the document and the geometry.
    REQUIRE(history.undo(m.document).has_value());
    requireRegenerated(regenerator, m.document);
    const reference::ModelFingerprint before = requireFingerprint(m.document, regenerator);
    const Document unrounded = m.document.clone();
    execute(history, m.document, ModifyParameterCommand::setValue(m.filletRadius, 2.5_mm));
    requireRegenerated(regenerator, m.document);
    ShaftSize rounder;
    rounder.fillet = 2.5;
    const reference::ModelFingerprint rounded = requireFingerprint(m.document, regenerator);
    checkProperties(onlyBody(rounded), expected(rounder, Stage::Chamfered));
    REQUIRE(history.undo(m.document).has_value());
    requireRegenerated(regenerator, m.document);
    CHECK(requireFingerprint(m.document, regenerator) == before);
    REQUIRE(history.redo(m.document).has_value());
    requireRegenerated(regenerator, m.document);
    CHECK(requireFingerprint(m.document, regenerator) == rounded);
    REQUIRE(history.undo(m.document).has_value());
    CHECK(equivalent(m.document, unrounded));
    CHECK(equivalent(m.document.parameters(), original.parameters()));
}

TEST_CASE("ReferenceModel_ShaftExports", "[reference][shaft][io][export]") {
    reference::ShaftModel m = buildShaft();
    TempDir dir;
    const ShaftSize s;
    const an::Solid finished = expected(s, Stage::Chamfered);
    checkStepExport(m.document, dir.path() / "shaft.step", finished.volume, lowCorner(s), highCorner(s));
    checkStlExport(m.document, dir.path() / "shaft.stl", finished.volume, finished.area);
}

TEST_CASE("ReferenceModel_ShaftFailsSafelyOnInvalidParameters", "[reference][shaft]") {
    reference::ShaftModel m = buildShaft();
    features::Regenerator regenerator;
    requireRegenerated(regenerator, m.document);
    const reference::ModelFingerprint baseline = requireFingerprint(m.document, regenerator);
    const Document original = m.document.clone();
    const ShaftSize s;

    // A shoulder fillet wider than the 5 mm journal shoulder: refused before
    // the kernel, naming the fillet; the chamfer after it is blocked.
    REQUIRE(m.document.setParameterValue(m.filletRadius, 6_mm).has_value());
    auto report = regenerator.regenerate(m.document);
    REQUIRE(report.has_value());
    CHECK(report->failed == std::vector<ObjectId>{m.shoulderFillets});
    CHECK(report->blocked == std::vector<ObjectId>{m.endChamfers});
    const Error& error = report->errors.at(m.shoulderFillets);
    INFO(error.message);
    CHECK(error.code == ErrorCode::FailedPrecondition);
    CHECK_THAT(error.message, ContainsSubstring("ShoulderFillets"));
    CHECK_THAT(error.message, ContainsSubstring("does not fit"));
    // Nothing invalid is kept: the failed and blocked features have no body,
    // and everything before them is intact.
    CHECK(regenerator.body(m.shoulderFillets) == nullptr);
    CHECK(regenerator.body(m.endChamfers) == nullptr);
    checkProperties(featureBody(regenerator, m.tailCentreDrill), expected(s, Stage::BothEndsDrilled));
    // The validator reports the model as invalid instead of failing, and the
    // edit is the only change to the document.
    CHECK_FALSE(features::validateDocument(m.document).valid());
    REQUIRE(m.document.setParameterValue(m.filletRadius, 1.5_mm).has_value());
    CHECK(equivalent(m.document, original));

    // Recovery: the original shaft, bit for bit.
    requireRegenerated(regenerator, m.document);
    CHECK(requireFingerprint(m.document, regenerator) == baseline);
}
