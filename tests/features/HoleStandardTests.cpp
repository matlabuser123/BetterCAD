#include "TestHelpers.hpp"
#include "core/geometry/GeometryTestSupport.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/HoleStandardModels.hpp"

#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/standards/MetricThreads.hpp>
#include <bettercad/features/FeatureCommands.hpp>
#include <bettercad/features/LinearPatternFeature.hpp>
#include <bettercad/features/MirrorFeature.hpp>
#include <bettercad/features/Regeneration.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/ResultBodies.hpp>
#include <bettercad/features/Validation.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <format>
#include <memory>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using bettercad::test::basicMinorDiameter;
using bettercad::test::printOf;
using bettercad::test::requireReport;
using bettercad::test::TappedPlateModel;
using bettercad::test::Vec3;
using bettercad::test::volumeOf;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// P12-HOLE-001: holes of standard sizes. A thread and a tolerance class are
// manufacturing data: the geometry is the hole the standard gives, and the
// rest is described, not cut.

namespace {

constexpr double pi = std::numbers::pi;
// Boxes and cylinders meeting in lines and circles: the kernel's properties
// are exact to rounding.
constexpr double kRel = bettercad::test::kRelTight;
constexpr double kTolMm = 1e-9;

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

/// Regenerates; exactly @p feature must fail, keeping no body.
Error requireFailure(Regenerator& regenerator, Document& doc, ObjectId feature) {
    const RegenerationReport report = requireReport(regenerator, doc);
    REQUIRE(report.failed == std::vector<ObjectId>{feature});
    CHECK(regenerator.body(feature) == nullptr);
    return report.errors.at(feature);
}

double volumeMm3(const Regenerator& regenerator, ObjectId id) {
    const auto properties = bodyOf(regenerator, id).massProperties();
    REQUIRE(properties.has_value());
    return properties->volume.in(units::mm3);
}

HoleCallout calloutOf(const Document& doc, const HoleDefinition& definition) {
    auto callout = holeCallout(definition, doc);
    REQUIRE(callout.has_value());
    return *callout;
}

void setValue(CommandHistory& history, Document& doc, ParameterId parameter, Length value) {
    REQUIRE(history
                .execute(doc, std::make_unique<ModifyParameterCommand>(
                                  parameter, ParameterChanges{.value = DimensionedValue::of(value)}))
                .has_value());
}

} // namespace

TEST_CASE("HoleStandards_TappedPlateHasTheVolumeOfItsStandardHoles", "[hole][standards][p12]") {
    TappedPlateModel m;
    Regenerator regenerator;
    regenerate(regenerator, m.doc);

    SECTION("the plate is the block less the hole each standard gives") {
        CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.reamed});
        const geometry::Body& body = bodyOf(regenerator, m.reamed);
        CHECK(body.isValid());
        CHECK(body.topology().solids == 1);
        const auto properties = body.massProperties();
        REQUIRE(properties.has_value());
        CHECK_THAT(properties->volume.in(units::mm3), WithinRel(TappedPlateModel::expectedVolume(), kRel));
        checkVec(mm(properties->centerOfMass), bettercad::test::weightedCentre(TappedPlateModel::parts()), kTolMm);
        const auto box = body.boundingBox();
        REQUIRE(box.has_value());
        checkVec(mm(box->min), {0.0, 0.0, 0.0}, kTolMm);
        checkVec(mm(box->max), {TappedPlateModel::kWidthMm, TappedPlateModel::kDepthMm,
                                TappedPlateModel::kThicknessMm}, kTolMm);
    }
    SECTION("each hole is cut at the diameter of its own standard") {
        // The tapped holes are cut at the basic minor diameters of ISO 68-1,
        // the clearance hole at the ISO 273 medium diameter for M8.
        const double before = volumeMm3(regenerator, m.plate);
        CHECK_THAT(before - volumeMm3(regenerator, m.tapped),
                   WithinRel(pi / 4.0 * std::pow(basicMinorDiameter(8.0, 1.25), 2) * 12.0, kRel));
        CHECK_THAT(volumeMm3(regenerator, m.tapped) - volumeMm3(regenerator, m.blindTapped),
                   WithinRel(pi / 4.0 * std::pow(basicMinorDiameter(6.0, 1.0), 2) * 10.0, kRel));
        CHECK_THAT(volumeMm3(regenerator, m.blindTapped) - volumeMm3(regenerator, m.seat),
                   WithinRel(pi / 4.0 * (9.0 * 9.0 * 12.0 + (15.0 * 15.0 - 9.0 * 9.0) * 5.0), kRel));
        CHECK_THAT(volumeMm3(regenerator, m.seat) - volumeMm3(regenerator, m.boss),
                   WithinRel(pi / 4.0 * (std::pow(basicMinorDiameter(10.0, 1.5), 2) * 12.0 +
                                         (20.0 * 20.0 - std::pow(basicMinorDiameter(10.0, 1.5), 2)) * 1.0),
                             kRel));
        CHECK_THAT(volumeMm3(regenerator, m.boss) - volumeMm3(regenerator, m.reamed),
                   WithinRel(pi / 4.0 * 10.0 * 10.0 * 8.0, kRel));
    }
    SECTION("the spotface's floor is a named face of the body") {
        const FaceName floor{m.boss, FaceSelector{.role = FaceRole::SpotfaceFloor}};
        const auto faces = geometry::findNamedFaces(bodyOf(regenerator, m.reamed), floor);
        REQUIRE(faces.has_value());
        REQUIRE(faces->size() == 1);
        // The seat around the bore, 1 mm below the top face.
        const double minor = basicMinorDiameter(10.0, 1.5);
        CHECK_THAT(faces->front().area.in(units::mm2), WithinRel(pi / 4.0 * (20.0 * 20.0 - minor * minor), kRel));
        CHECK_THAT(faces->front().centroid.z.in(units::mm), WithinAbs(11.0, kTolMm));
        // A counterbore's floor keeps its own role.
        const FaceName counterbore{m.seat, FaceSelector{.role = FaceRole::CounterboreFloor}};
        CHECK(geometry::findNamedFaces(bodyOf(regenerator, m.reamed), counterbore)->size() == 1);
    }
}

TEST_CASE("HoleStandards_CalloutsDescribeTheStandardsData", "[hole][standards][p12]") {
    TappedPlateModel m;
    Regenerator regenerator;
    regenerate(regenerator, m.doc);

    SECTION("a tapped hole's callout is its thread's designation and limits") {
        const HoleCallout callout = calloutOf(m.doc, m.definitionOf(m.tapped));
        CHECK_THAT(callout.diameter.in(units::mm), WithinAbs(basicMinorDiameter(8.0, 1.25), 1e-12));
        CHECK_FALSE(callout.tolerance.has_value());
        REQUIRE(callout.thread.has_value());
        CHECK(callout.thread->designation == "M8-6H");
        CHECK_THAT(callout.thread->basic.minor.in(units::mm), WithinAbs(6.647, 5e-4));
        CHECK_THAT(callout.thread->basic.pitch.in(units::mm), WithinAbs(7.188, 5e-4));
        // The limits of ISO 965-2 for M8-6H.
        CHECK_THAT(callout.thread->limits.minorDiameter.min.in(units::mm), WithinAbs(6.647, 5e-4));
        CHECK_THAT(callout.thread->limits.minorDiameter.max.in(units::mm), WithinAbs(6.912, 5e-4));
        CHECK_THAT(callout.thread->limits.pitchDiameter.min.in(units::mm), WithinAbs(7.188, 5e-4));
        CHECK_THAT(callout.thread->limits.pitchDiameter.max.in(units::mm), WithinAbs(7.348, 5e-4));
        // A through hole threaded over its whole length has no length.
        CHECK_FALSE(callout.thread->length.has_value());
    }
    SECTION("a blind hole's thread is as long as its parameter says") {
        const HoleCallout callout = calloutOf(m.doc, m.definitionOf(m.blindTapped));
        REQUIRE(callout.thread.has_value());
        CHECK(callout.thread->designation == "M6-6H");
        REQUIRE(callout.thread->length.has_value());
        CHECK_THAT(callout.thread->length->in(units::mm), WithinAbs(8.0, 1e-12));
    }
    SECTION("a clearance hole's callout is its ISO 273 diameter and its class") {
        const HoleCallout callout = calloutOf(m.doc, m.definitionOf(m.seat));
        CHECK_THAT(callout.diameter.in(units::mm), WithinAbs(9.0, 1e-12));
        REQUIRE(callout.tolerance.has_value());
        CHECK(standards::toString(*callout.tolerance) == "H13");
        REQUIRE(callout.deviations.has_value());
        // H13 of a 9 mm hole: 0 to +0.22 mm (IT13 of sizes over 6 up to 10 mm).
        CHECK_THAT(callout.deviations->lower.in(units::um), WithinAbs(0.0, 1e-9));
        CHECK_THAT(callout.deviations->upper.in(units::um), WithinAbs(220.0, 1e-9));
        CHECK_FALSE(callout.thread.has_value());
    }
    SECTION("a reamed hole's limits follow its driven diameter") {
        CHECK_THAT(calloutOf(m.doc, m.definitionOf(m.reamed)).deviations->upper.in(units::um),
                   WithinAbs(15.0, 1e-9));
        CommandHistory history;
        // 10.5 mm is in the next size range of ISO 286: H7 is 0 to +18 um.
        setValue(history, m.doc, m.bore, 10.5_mm);
        CHECK_THAT(calloutOf(m.doc, m.definitionOf(m.reamed)).deviations->upper.in(units::um),
                   WithinAbs(18.0, 1e-9));
        CHECK_THAT(calloutOf(m.doc, m.definitionOf(m.reamed)).diameter.in(units::mm), WithinAbs(10.5, 1e-12));
    }
}

TEST_CASE("HoleStandards_ParametersDriveThreadsAndBores", "[hole][standards][p12]") {
    TappedPlateModel m;
    Regenerator regenerator;
    regenerate(regenerator, m.doc);
    CommandHistory history;

    SECTION("a thread's length changes nothing in the geometry") {
        const bettercad::test::BodyPrint before = printOf(bodyOf(regenerator, m.reamed));
        setValue(history, m.doc, m.threadLength, 5_mm);
        regenerate(regenerator, m.doc);
        CHECK(printOf(bodyOf(regenerator, m.reamed)) == before);
        CHECK_THAT(calloutOf(m.doc, m.definitionOf(m.blindTapped)).thread->length->in(units::mm),
                   WithinAbs(5.0, 1e-12));
    }
    SECTION("a thread longer than its blind hole is refused at regeneration") {
        setValue(history, m.doc, m.threadLength, 11_mm);
        const Error error = requireFailure(regenerator, m.doc, m.blindTapped);
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK_THAT(error.message,
                   ContainsSubstring("the thread (11 mm long) must not be longer than the blind hole (10 mm deep)"));
        // Undoing the parameter change brings the hole back.
        REQUIRE(history.undo(m.doc).has_value());
        regenerate(regenerator, m.doc);
        CHECK_THAT(volumeMm3(regenerator, m.reamed), WithinRel(TappedPlateModel::expectedVolume(), kRel));
    }
    SECTION("a bore drives the hole and its tolerance class") {
        setValue(history, m.doc, m.bore, 12_mm);
        regenerate(regenerator, m.doc);
        CHECK_THAT(volumeMm3(regenerator, m.reamed), WithinRel(TappedPlateModel::expectedVolume(12.0), kRel));
        // A size ISO 286 does not tabulate fails, and keeps no body.
        setValue(history, m.doc, m.bore, 600_mm);
        const Error error = requireFailure(regenerator, m.doc, m.reamed);
        CHECK(error.code == ErrorCode::InvalidArgument);
        CHECK_THAT(error.message, ContainsSubstring("the tolerance class H7: BetterCAD knows the ISO 286 tolerances "
                                                    "of sizes above 0 up to 500 mm, not 600 mm"));
    }
}

TEST_CASE("HoleStandards_ClearanceSeriesChangesTheDiameter", "[hole][standards][p12]") {
    TappedPlateModel m;
    Regenerator regenerator;
    regenerate(regenerator, m.doc);
    const double before = volumeMm3(regenerator, m.reamed);

    HoleDefinition definition = m.definitionOf(m.seat);
    definition.clearance->series = standards::ClearanceSeries::Fine;
    m.setDefinition(m.seat, definition);
    regenerate(regenerator, m.doc);
    // The fine series gives M8 an 8.4 mm hole instead of 9 mm.
    CHECK_THAT(volumeMm3(regenerator, m.reamed) - before,
               WithinRel(pi / 4.0 * (9.0 * 9.0 - 8.4 * 8.4) * 12.0 - pi / 4.0 * (9.0 * 9.0 - 8.4 * 8.4) * 5.0, kRel));
    CHECK_THAT(calloutOf(m.doc, m.definitionOf(m.seat)).diameter.in(units::mm), WithinAbs(8.4, 1e-12));
}

TEST_CASE("HoleStandards_RejectDefinitionsTheStandardsDoNotAllow", "[hole][standards][p12]") {
    TappedPlateModel m;
    const HoleDefinition tapped = m.definitionOf(m.tapped);
    const auto refused = [&](const HoleDefinition& definition) {
        const auto valid = validate(definition);
        REQUIRE_FALSE(valid.has_value());
        CHECK(valid.error().code == ErrorCode::InvalidArgument);
        return valid.error().message;
    };

    SECTION("a hole takes one standard size") {
        HoleDefinition definition = tapped;
        definition.clearance = TappedPlateModel::clearance("M8");
        CHECK(refused(definition) == "a hole takes a thread or a clearance size, not both");
    }
    SECTION("a standard size gives the diameter") {
        HoleDefinition definition = tapped;
        definition.diameter = 7_mm;
        CHECK(refused(definition) == "a threaded hole's diameter comes from its thread; it takes no diameter");
        definition = tapped;
        definition.diameterParameter = m.bore;
        CHECK(refused(definition) == "a threaded hole's diameter comes from its thread; it takes no diameter");
        definition = m.definitionOf(m.seat);
        definition.diameter = 9_mm;
        CHECK(refused(definition) ==
              "a standard clearance hole's diameter comes from ISO 273; it takes no diameter");
    }
    SECTION("a thread carries its own tolerance class") {
        HoleDefinition definition = tapped;
        definition.tolerance = standards::HoleToleranceClass{standards::HoleDeviation::H, 7};
        CHECK(refused(definition) == "a threaded hole takes no tolerance class; its thread has one (6H)");
    }
    SECTION("a thread class whose limits are unknown is refused") {
        HoleDefinition definition = tapped;
        definition.thread->tolerance = {.grade = 7};
        CHECK_THAT(refused(definition), ContainsSubstring("BetterCAD knows the M8 thread tolerances of grade 6 only"));
    }
    SECTION("a tolerance class must be defined for the hole's size") {
        HoleDefinition definition = m.definitionOf(m.reamed);
        definition.diameterParameter.reset();
        definition.diameter = 600_mm;
        CHECK_THAT(refused(definition), ContainsSubstring("up to 500 mm, not 600 mm"));
        definition.diameter = 0.5_mm;
        definition.tolerance = standards::HoleToleranceClass{standards::HoleDeviation::H, 14};
        CHECK_THAT(refused(definition), ContainsSubstring("ISO 286 does not use grade IT14 for sizes up to 1 mm"));
        definition.tolerance = standards::HoleToleranceClass{standards::HoleDeviation::D, 5};
        CHECK_THAT(refused(definition), ContainsSubstring("the hole tolerance class D5 is not one BetterCAD knows"));
    }
    SECTION("a head must clear the thread") {
        HoleDefinition definition = m.definitionOf(m.boss);
        definition.spotfaceDiameter = 9_mm;
        CHECK(refused(definition) ==
              "the spotface diameter (9 mm) must be larger than the thread's major diameter (10 mm)");
        definition = m.definitionOf(m.boss);
        definition.thread->length = 0.5_mm;
        CHECK(refused(definition) == "the thread (0.5 mm long) must be longer than the spotface (1 mm deep)");
    }
    SECTION("a spotface needs its own dimensions, and only a spotface takes them") {
        HoleDefinition definition = m.definitionOf(m.boss);
        definition.spotfaceDiameter = Length{};
        // The hole's own diameter is its thread's basic minor diameter,
        // which the message gives in full (BetterCAD computes it in metres,
        // so its last digits differ from the same formula in millimetres).
        CHECK_THAT(refused(definition),
                   ContainsSubstring("the spotface diameter must be larger than the hole diameter (8.3762023679"));
        CHECK_THAT(refused(definition), ContainsSubstring("mm), got 0 mm"));
        definition = tapped;
        definition.spotfaceDepth = 2_mm;
        CHECK(refused(definition) == "only a spotface hole takes spotface dimensions");
    }
    SECTION("a thread must fit its hole") {
        HoleDefinition definition = m.definitionOf(m.blindTapped);
        definition.thread->lengthParameter.reset();
        definition.thread->length = 12_mm;
        CHECK(refused(definition) == "the thread (12 mm long) must not be longer than the blind hole (10 mm deep)");
        definition.thread->length = -1_mm;
        CHECK(refused(definition) ==
              "the thread length must be positive and finite, or zero for the whole hole, got -1 mm");
    }
}

TEST_CASE("HoleStandards_ValidationReportsTheThreadLengthParameter", "[hole][standards][p12]") {
    TappedPlateModel m;
    const auto angle = m.doc.createParameter("turn", 30_deg, units::deg);
    REQUIRE(angle.has_value());
    HoleDefinition definition = m.definitionOf(m.blindTapped);
    definition.thread->lengthParameter = *angle;
    m.setDefinition(m.blindTapped, definition);

    const ValidationReport report = validateDocument(m.doc);
    CHECK_FALSE(report.valid());
    bool found = false;
    for (const ValidationIssue& issue : report.issues) {
        if (issue.message.find("the thread length is driven by") != std::string::npos) {
            found = true;
            CHECK_THAT(issue.message, ContainsSubstring("turn"));
        }
    }
    CHECK(found);
}

TEST_CASE("HoleStandards_RepeatAndRebuildGiveTheSameBodies", "[hole][standards][p12]") {
    TappedPlateModel m;
    Regenerator regenerator;
    regenerate(regenerator, m.doc);
    const bettercad::test::BodyPrint print = printOf(bodyOf(regenerator, m.reamed));

    SECTION("regenerating again changes nothing") {
        regenerate(regenerator, m.doc);
        CHECK(printOf(bodyOf(regenerator, m.reamed)) == print);
    }
    SECTION("a fresh regenerator builds the same body") {
        Regenerator fresh;
        regenerate(fresh, m.doc);
        CHECK(printOf(bodyOf(fresh, m.reamed)) == print);
    }
    SECTION("a fresh document builds the same body") {
        TappedPlateModel again;
        Regenerator other;
        regenerate(other, again.doc);
        CHECK(printOf(bodyOf(other, again.reamed)) == print);
    }
}

TEST_CASE("HoleStandards_PatternsAndMirrorsRepeatThreadedHoles", "[hole][standards][pattern][p12]") {
    TappedPlateModel m;
    const double minor = basicMinorDiameter(8.0, 1.25);

    SECTION("a linear pattern drills the same tapped hole at each instance") {
        // Three M8 holes 20 mm apart, from the one at (15, 15).
        const ObjectId row = m.add(LinearPatternFeature::create(
            "Row", {.source = TappedPlateModel::featureOf(m.tapped),
                    .first = {.direction = {1.0, 0.0, 0.0}, .count = 3, .spacing = 20_mm}}));
        // The pattern consumes the hole, so the holes after it target the row.
        HoleDefinition next = m.definitionOf(m.blindTapped);
        next.target = TappedPlateModel::featureOf(row);
        m.setDefinition(m.blindTapped, next);

        Regenerator regenerator;
        regenerate(regenerator, m.doc);
        // Two more bores of the thread's minor diameter, through the plate.
        CHECK_THAT(volumeMm3(regenerator, m.reamed),
                   WithinRel(TappedPlateModel::expectedVolume() - 2.0 * pi / 4.0 * minor * minor * 12.0, kRel));
        for (const double x : {15.0, 35.0, 55.0}) {
            CAPTURE(x);
            const auto circle = geometry::circleSignature(Point3D{x * units::mm, 15_mm, 12_mm},
                                                          Direction3D::unitZ(), minor / 2.0 * units::mm);
            REQUIRE(circle.has_value());
            CHECK(geometry::findEdges(bodyOf(regenerator, m.reamed), *circle)->size() == 1);
        }
    }
    SECTION("a mirror repeats the thread as it is: threads are not handed") {
        const ObjectId image = m.add(MirrorFeature::create(
            "Image", {.source = TappedPlateModel::featureOf(m.tapped),
                      .plane = {.origin = Point3D{25_mm, 0_mm, 0_mm}, .normal = {1.0, 0.0, 0.0}}}));
        HoleDefinition next = m.definitionOf(m.blindTapped);
        next.target = TappedPlateModel::featureOf(image);
        m.setDefinition(m.blindTapped, next);

        Regenerator regenerator;
        regenerate(regenerator, m.doc);
        CHECK_THAT(volumeMm3(regenerator, m.reamed),
                   WithinRel(TappedPlateModel::expectedVolume() - pi / 4.0 * minor * minor * 12.0, kRel));
        // The image is at x = 35, the mirror of x = 15 across x = 25.
        const auto circle = geometry::circleSignature(Point3D{35_mm, 15_mm, 12_mm}, Direction3D::unitZ(),
                                                      minor / 2.0 * units::mm);
        REQUIRE(circle.has_value());
        CHECK(geometry::findEdges(bodyOf(regenerator, m.reamed), *circle)->size() == 1);
    }
}

TEST_CASE("HoleStandards_EditingAStandardIsUndoable", "[hole][standards][p12]") {
    TappedPlateModel m;
    Regenerator regenerator;
    regenerate(regenerator, m.doc);
    const bettercad::test::BodyPrint before = printOf(bodyOf(regenerator, m.reamed));
    const HoleDefinition original = m.definitionOf(m.tapped);
    CommandHistory history;

    // M8 to M10: a wider thread, so a wider bore.
    HoleDefinition wider = original;
    wider.thread->size = *standards::parseMetricThread("M10");
    REQUIRE(history.execute(m.doc, std::make_unique<ModifyHoleCommand>(TappedPlateModel::featureOf(m.tapped), wider))
                .has_value());
    regenerate(regenerator, m.doc);
    const double m8 = pi / 4.0 * std::pow(basicMinorDiameter(8.0, 1.25), 2) * 12.0;
    const double m10 = pi / 4.0 * std::pow(basicMinorDiameter(10.0, 1.5), 2) * 12.0;
    CHECK_THAT(volumeMm3(regenerator, m.reamed), WithinRel(TappedPlateModel::expectedVolume() + m8 - m10, kRel));
    CHECK(calloutOf(m.doc, m.definitionOf(m.tapped)).thread->designation == "M10-6H");

    // Undo restores the model and its geometry exactly; redo repeats the edit.
    REQUIRE(history.undo(m.doc).has_value());
    CHECK(m.definitionOf(m.tapped) == original);
    regenerate(regenerator, m.doc);
    CHECK(printOf(bodyOf(regenerator, m.reamed)) == before);
    REQUIRE(history.redo(m.doc).has_value());
    CHECK(m.definitionOf(m.tapped) == wider);
    regenerate(regenerator, m.doc);
    CHECK_THAT(volumeMm3(regenerator, m.reamed), WithinRel(TappedPlateModel::expectedVolume() + m8 - m10, kRel));
}
