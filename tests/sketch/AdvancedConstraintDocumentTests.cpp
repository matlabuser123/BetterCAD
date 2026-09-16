#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/Validation.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/sketch/SketchCommands.hpp>
#include <bettercad/sketch/SketchRegeneration.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using namespace bettercad::sketch;
using bettercad::test::errorCode;
using bettercad::test::require;
using bettercad::test::requireReport;
using bettercad::test::volumeMm3;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;

// P12-SKETCH-001: the new constraints in documents — validation of their
// references, driving parameters, regeneration, persistence and undo.

namespace {

constexpr double pi = std::numbers::pi;
constexpr double kVolumeRel = 1e-12;

struct Lines {
    Sketch sketch{"Checks"};
    EntityId p1, p2, lineA, lineB, circle, arc, arcCentre, point;

    Lines() {
        lineA = require(sketch.addLine(Point2D{0_mm, 0_mm}, Point2D{50_mm, 0_mm}));
        lineB = require(sketch.addLine(Point2D{0_mm, 10_mm}, Point2D{40_mm, 40_mm}));
        const LineEntity a = std::get<LineEntity>(sketch.findEntity(lineA)->geometry);
        p1 = a.start;
        p2 = a.end;
        circle = require(sketch.addCircle(Point2D{80_mm, 0_mm}, 10_mm));
        arc = require(sketch.addArc(Point2D{120_mm, 0_mm}, 5_mm, 0_deg, 90_deg));
        arcCentre = std::get<ArcEntity>(sketch.findEntity(arc)->geometry).center;
        point = require(sketch.addPoint(Point2D{20_mm, 30_mm}));
    }
};

std::string message(const Result<ConstraintId>& result) {
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == ErrorCode::InvalidArgument);
    return result.error().message;
}

// width, height, depth... -> a slot of diameter `slot_width` and centre
// distance `slot_length`, and a wedge of angle `opening`; each extruded.
struct DrivenSketches {
    Document doc{"Driven"};
    ParameterId diameter, length, depth, opening, halfOpening;
    ObjectId slotSketch, slot, wedgeSketch, wedge;

    DrivenSketches() {
        diameter = doc.createParameter("slot_width", 16_mm, units::mm).value();
        length = doc.createParameter("slot_length", 40_mm, units::mm).value();
        depth = doc.createParameter("depth", 10_mm, units::mm).value();
        halfOpening = doc.createParameter("half_opening", 20_deg, units::deg).value();
        opening = doc.createParameter("opening", 1_deg, units::deg).value();
        REQUIRE(doc.setParameterExpression(opening, "2 * half_opening").has_value());

        slotSketch = doc.addObject(makeSlot()).value();
        slot = addExtrude("Slot", slotSketch);
        wedgeSketch = doc.addObject(makeWedge()).value();
        wedge = addExtrude("Wedge", wedgeSketch);
    }

    ObjectId addExtrude(const std::string& name, ObjectId sketch) {
        auto feature = ExtrudeFeature::create(
            name, {.profile = SketchId::fromValue(sketch.value()), .depthParameter = depth});
        REQUIRE(feature.has_value());
        return doc.addObject(std::move(*feature)).value();
    }

    [[nodiscard]] std::unique_ptr<Sketch> makeSlot() const {
        auto s = std::make_unique<Sketch>("SlotSketch");
        const EntityId cL = require(s->addPoint(Point2D{0_mm, 0_mm}));
        const EntityId cR = require(s->addPoint(Point2D{35_mm, 0_mm}));
        const EntityId b0 = require(s->addPoint(Point2D{0_mm, -6_mm}));
        const EntityId t1 = require(s->addPoint(Point2D{0_mm, 6_mm}));
        const EntityId b1 = require(s->addPoint(Point2D{35_mm, -6_mm}));
        const EntityId t0 = require(s->addPoint(Point2D{35_mm, 6_mm}));
        const EntityId right = require(s->addArc(cR, b1, t0));
        const EntityId left = require(s->addArc(cL, t1, b0));
        const EntityId bottom = require(s->addLine(b0, b1));
        const EntityId top = require(s->addLine(t0, t1));
        require(s->addFixed(cL));
        require(s->addHorizontal(cL, cR));
        REQUIRE(s->setConstraintParameter(require(s->addDistance(cL, cR, 1_mm)), length).has_value());
        REQUIRE(s->setConstraintParameter(require(s->addDiameter(left, 1_mm)), diameter).has_value());
        require(s->addEqual(right, left));
        require(s->addTangent(bottom, right));
        require(s->addTangent(top, right));
        require(s->addTangent(top, left));
        require(s->addTangent(bottom, left));
        return s;
    }

    // A triangle with sides 60 and 40 mm at an angle `opening`, 100 mm to the
    // right of the slot. Area 1/2 a b sin(opening).
    [[nodiscard]] std::unique_ptr<Sketch> makeWedge() const {
        auto s = std::make_unique<Sketch>("WedgeSketch");
        const EntityId apex = require(s->addPoint(Point2D{100_mm, 0_mm}));
        const EntityId baseEnd = require(s->addPoint(Point2D{160_mm, 0_mm}));
        const EntityId armEnd = require(s->addPoint(Point2D{135_mm, 15_mm}));
        const EntityId base = require(s->addLine(apex, baseEnd));
        const EntityId arm = require(s->addLine(apex, armEnd));
        require(s->addLine(baseEnd, armEnd));
        require(s->addFixed(apex));
        require(s->addHorizontal(base));
        require(s->addDistance(base, 60_mm));
        require(s->addDistance(arm, 40_mm));
        REQUIRE(s->setConstraintParameter(require(s->addAngle(base, arm, 1_deg)), opening).has_value());
        return s;
    }

    static double slotVolume(double diameterMm, double lengthMm, double depthMm) {
        return (lengthMm * diameterMm + pi * diameterMm * diameterMm / 4.0) * depthMm;
    }
    static double wedgeVolume(double openingDeg, double depthMm) {
        return 0.5 * 60.0 * 40.0 * std::sin(openingDeg * pi / 180.0) * depthMm;
    }
};

} // namespace

// --- References and values --------------------------------------------------------------------

TEST_CASE("SketchConstraint_NewTypesValidateTheirReferences", "[sketch][constraints][p12]") {
    Lines f;
    Sketch& s = f.sketch;

    CHECK(message(s.addAngle(f.lineA, f.circle, 30_deg)) ==
          "an angle constraint takes two lines, got line, circle");
    CHECK(message(s.addTangent(f.lineA, f.lineB)) ==
          "a tangent constraint takes a line and a circle or arc, or two circles or arcs, got line, line");
    CHECK(message(s.addTangent(f.point, f.circle)) ==
          "a tangent constraint takes a line and a circle or arc, or two circles or arcs, got point, circle");
    CHECK(message(s.addConcentric(f.circle, f.lineA)) ==
          "a concentric constraint takes two circles or arcs, got circle, line");
    CHECK(message(s.addMidpoint(f.point, f.circle)) == "a midpoint constraint takes a point and a line, got point, circle");
    CHECK(message(s.addSymmetric(f.point, f.lineA, f.lineB)) ==
          "a symmetric constraint takes two points and a line, got point, line, line");
    CHECK(message(s.addDiameter(f.lineA, 10_mm)) == "a diameter constraint takes a circle or an arc, got line");

    // Degenerate combinations are refused by name.
    CHECK(message(s.addMidpoint(f.p1, f.lineA)) ==
          std::format("{} is an end point of {}; it cannot also be its midpoint", f.p1, f.lineA));
    const EntityId sharing = require(s.addCircle(f.arcCentre, 8_mm));
    CHECK(message(s.addConcentric(f.arc, sharing)) ==
          std::format("{} and {} already share their centre point {}", f.arc, sharing, f.arcCentre));

    // Canonical order: (line, round), (point, line), (point, point, line).
    const ConstraintId tangent = require(s.addTangent(f.circle, f.lineB));
    CHECK(s.findConstraint(tangent)->entities == std::vector<EntityId>{f.lineB, f.circle});
    const ConstraintId middle = require(s.addMidpoint(f.lineB, f.point));
    CHECK(s.findConstraint(middle)->entities == std::vector<EntityId>{f.point, f.lineB});
    const ConstraintId mirror = require(s.addConstraint(ConstraintType::Symmetric, {f.lineB, f.p1, f.point}));
    CHECK(s.findConstraint(mirror)->entities == std::vector<EntityId>{f.p1, f.point, f.lineB});
    CHECK(errorCode(s.addTangent(f.circle, f.circle)) == ErrorCode::InvalidArgument);
    CHECK(errorCode(s.addAngle(f.lineA, EntityId::fromValue(999), 10_deg)) == ErrorCode::NotFound);
}

TEST_CASE("SketchConstraint_AngleAndDiameterValuesAreChecked", "[sketch][constraints][p12]") {
    Lines f;
    Sketch& s = f.sketch;

    for (const Angle bad : {0_deg, 180_deg, -(10_deg), 190_deg, Angle::fromSi(std::nan(""))}) {
        CAPTURE(bad.in(units::deg));
        CHECK_THAT(message(s.addAngle(f.lineA, f.lineB, bad)),
                   ContainsSubstring("an angle constraint needs an angle between 0 and 180 deg (exclusive)"));
    }
    CHECK(message(s.addDiameter(f.circle, 0_mm)) == "diameter 0 mm must be positive");
    CHECK(message(s.addConstraint(ConstraintType::Angle, {f.lineA, f.lineB}, 5_mm)) ==
          "an angle constraint takes an angle, not a length");
    CHECK(message(s.addConstraint(ConstraintType::Distance, {f.lineA}, 5_mm, 10_deg)) ==
          "a distance constraint does not take an angle");
    CHECK(message(s.addConstraint(ConstraintType::Tangent, {f.lineA, f.circle}, 5_mm)) ==
          "a tangent constraint does not take a value");
    CHECK(message(s.addConstraint(ConstraintType::Angle, {f.lineA, f.lineB})) ==
          "an angle constraint needs an angle between 0 and 180 deg (exclusive), got none");

    const ConstraintId angle = require(s.addAngle(f.lineA, f.lineB, 45_deg));
    CHECK(s.findConstraint(angle)->angle == 45_deg);
    CHECK_FALSE(s.findConstraint(angle)->value.has_value());
    CHECK(s.setConstraintAngle(angle, 60_deg).value());
    CHECK_FALSE(s.setConstraintAngle(angle, 60_deg).value());
    CHECK(errorCode(s.setConstraintAngle(angle, 180_deg)) == ErrorCode::InvalidArgument);
    CHECK(s.findConstraint(angle)->angle == 60_deg);
    CHECK(errorCode(s.setConstraintValue(angle, 5_mm)) == ErrorCode::InvalidArgument);
    const ConstraintId diameter = require(s.addDiameter(f.circle, 20_mm));
    CHECK(errorCode(s.setConstraintAngle(diameter, 5_deg)) == ErrorCode::InvalidArgument);

    // Both are drivable; the others are not.
    CHECK(s.setConstraintParameter(angle, ParameterId::fromValue(1)).value());
    CHECK(s.setConstraintParameter(diameter, ParameterId::fromValue(2)).value());
    const ConstraintId concentric = require(s.addConcentric(f.circle, f.arc));
    CHECK(errorCode(s.setConstraintParameter(concentric, ParameterId::fromValue(1))) == ErrorCode::InvalidArgument);
    CHECK(toString(ConstraintType::Angle) == "angle");
    CHECK(toString(ConstraintType::Tangent) == "tangent");
    CHECK(toString(ConstraintType::Concentric) == "concentric");
    CHECK(toString(ConstraintType::Midpoint) == "midpoint");
    CHECK(toString(ConstraintType::Symmetric) == "symmetric");
    CHECK(toString(ConstraintType::Diameter) == "diameter");
}

TEST_CASE("SketchConstraint_DrivingParametersMustHaveTheRightDimension", "[sketch][constraints][p12]") {
    Lines f;
    ParameterTable parameters;
    REQUIRE(parameters.add(*Parameter::create(ParameterId::fromValue(1), "turn", 30_deg, units::deg)).has_value());
    REQUIRE(parameters.add(*Parameter::create(ParameterId::fromValue(2), "size", 24_mm, units::mm)).has_value());
    Sketch& s = f.sketch;
    const ConstraintId angle = require(s.addAngle(f.lineA, f.lineB, 45_deg));
    const ConstraintId diameter = require(s.addDiameter(f.circle, 20_mm));

    REQUIRE(s.setConstraintParameter(angle, ParameterId::fromValue(1)).has_value());
    REQUIRE(s.setConstraintParameter(diameter, ParameterId::fromValue(2)).has_value());
    CHECK(applyDrivingParameters(s, parameters).value());
    CHECK(s.findConstraint(angle)->angle == 30_deg);
    CHECK(s.findConstraint(diameter)->value == 24_mm);

    // An angle driven by a length, or a diameter by an angle, is refused.
    REQUIRE(s.setConstraintParameter(angle, ParameterId::fromValue(2)).has_value());
    CHECK(errorCode(applyDrivingParameters(s, parameters)) == ErrorCode::DimensionMismatch);
    REQUIRE(s.setConstraintParameter(angle, ParameterId::fromValue(1)).has_value());
    REQUIRE(s.setConstraintParameter(diameter, ParameterId::fromValue(1)).has_value());
    CHECK(errorCode(applyDrivingParameters(s, parameters)) == ErrorCode::DimensionMismatch);
}

// --- Regeneration ------------------------------------------------------------------------------

TEST_CASE("SketchDocument_DrivenDiameterAndAngleRegenerateGeometry", "[sketch][regeneration][p12][acceptance]") {
    DrivenSketches m;
    Regenerator regenerator;
    const RegenerationReport first = requireReport(regenerator, m.doc);
    REQUIRE(first.succeeded());
    CHECK_THAT(volumeMm3(regenerator, m.slot), WithinRel(DrivenSketches::slotVolume(16.0, 40.0, 10.0), kVolumeRel));
    CHECK_THAT(volumeMm3(regenerator, m.wedge), WithinRel(DrivenSketches::wedgeVolume(40.0, 10.0), kVolumeRel));

    CommandHistory history;
    const auto set = [&](ParameterId id, DimensionedValue value) {
        REQUIRE(history.execute(m.doc, std::make_unique<ModifyParameterCommand>(id, ParameterChanges{.value = value}))
                    .has_value());
    };
    set(m.diameter, DimensionedValue::of(22_mm));
    set(m.length, DimensionedValue::of(55_mm));
    set(m.halfOpening, DimensionedValue::of(35_deg)); // opening = 70 deg through its expression
    const RegenerationReport second = requireReport(regenerator, m.doc);
    REQUIRE(second.succeeded());
    CHECK(second.updatedParameters == std::vector<ParameterId>{m.opening});
    CHECK(second.regenerated == std::vector<ObjectId>{m.slotSketch, m.slot, m.wedgeSketch, m.wedge});
    CHECK_THAT(volumeMm3(regenerator, m.slot), WithinRel(DrivenSketches::slotVolume(22.0, 55.0, 10.0), kVolumeRel));
    CHECK_THAT(volumeMm3(regenerator, m.wedge), WithinRel(DrivenSketches::wedgeVolume(70.0, 10.0), kVolumeRel));

    // Undo all three edits: the first geometry comes back.
    for (int i = 0; i < 3; ++i) {
        REQUIRE(history.undo(m.doc).has_value());
    }
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CHECK_THAT(volumeMm3(regenerator, m.slot), WithinRel(DrivenSketches::slotVolume(16.0, 40.0, 10.0), kVolumeRel));
    CHECK_THAT(volumeMm3(regenerator, m.wedge), WithinRel(DrivenSketches::wedgeVolume(40.0, 10.0), kVolumeRel));
}

TEST_CASE("SketchDocument_InvalidDrivenValuesFailTheSketch", "[sketch][regeneration][p12]") {
    DrivenSketches m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());

    // 2 * 95 deg is outside (0, 180) deg: the wedge sketch fails, the slot is untouched.
    REQUIRE(m.doc.setParameterValue(m.halfOpening, 95_deg).has_value());
    const RegenerationReport report = requireReport(regenerator, m.doc);
    CHECK(report.failed == std::vector<ObjectId>{m.wedgeSketch});
    CHECK(report.errors.at(m.wedgeSketch).code == ErrorCode::InvalidArgument);
    CHECK_THAT(report.errors.at(m.wedgeSketch).message,
               ContainsSubstring("an angle constraint needs an angle between 0 and 180 deg (exclusive), got 190 deg"));
    CHECK(report.blocked == std::vector<ObjectId>{m.wedge});
    CHECK(regenerator.body(m.slot) != nullptr);

    // A diameter parameter that becomes an angle is a consistency error.
    Document broken = m.doc.clone();
    REQUIRE(broken.setParameterValue(m.halfOpening, 20_deg).has_value());
    REQUIRE(broken.modifyObject<Sketch>(m.slotSketch, [&](Sketch& s) {
                   for (const Constraint& c : s.constraints()) {
                       if (c.type == ConstraintType::Diameter) {
                           return s.setConstraintParameter(c.id, m.halfOpening);
                       }
                   }
                   return Result<bool>{false};
               }).value());
    const ValidationReport validation = validateDocument(broken);
    REQUIRE(validation.count(ValidationCheck::DocumentConsistency, Severity::Error) == 1);
    CHECK_THAT(validation.issues[0].message,
               ContainsSubstring("is driven by half_opening (object:4), which is an angle, not a length"));
    // And an angle driven by a length.
    Document wrong = m.doc.clone();
    REQUIRE(wrong.modifyObject<Sketch>(m.wedgeSketch, [&](Sketch& s) {
                   for (const Constraint& c : s.constraints()) {
                       if (c.type == ConstraintType::Angle) {
                           return s.setConstraintParameter(c.id, m.depth);
                       }
                   }
                   return Result<bool>{false};
               }).value());
    const ValidationReport angle = validateDocument(wrong);
    REQUIRE(angle.count(ValidationCheck::DocumentConsistency, Severity::Error) == 1);
    CHECK_THAT(angle.issues[0].message, ContainsSubstring("which is a length, not an angle"));
}

TEST_CASE("SketchDocument_ValidationSolvesTheNewConstraints", "[sketch][validation][p12]") {
    DrivenSketches m;
    const ValidationReport report = validateDocument(m.doc);
    CHECK(report.valid());
    CHECK(report.issues.empty()); // both sketches are fully constrained
    REQUIRE(report.bodies.size() == 2);
}

// --- Undo, persistence --------------------------------------------------------------------------

TEST_CASE("SketchCommands_ModifySketchCommandUndoesAndRedoesEdits", "[sketch][commands][p12]") {
    Document doc{"Edits"};
    CommandHistory history;
    auto create = std::make_unique<CreateSketchCommand>("Sketch1");
    CreateSketchCommand* created = create.get();
    REQUIRE(history.execute(doc, std::move(create)).has_value());
    const ObjectId id{created->sketchId()};

    EntityId lineA;
    EntityId lineB;
    REQUIRE(history
                .execute(doc, std::make_unique<ModifySketchCommand>(
                                  created->sketchId(), "Add lines", [&](Sketch& s) -> Result<void> {
                                      lineA = require(s.addLine(Point2D{0_mm, 0_mm}, Point2D{50_mm, 0_mm}));
                                      lineB = require(s.addLine(Point2D{0_mm, 10_mm}, Point2D{40_mm, 40_mm}));
                                      return {};
                                  }))
                .has_value());
    const Sketch withLines = *doc.findObjectAs<Sketch>(id);
    ConstraintId angle;
    REQUIRE(history
                .execute(doc, std::make_unique<ModifySketchCommand>(
                                  created->sketchId(), "Add angle", [&](Sketch& s) -> Result<void> {
                                      auto added = s.addAngle(lineA, lineB, 40_deg);
                                      if (!added) {
                                          return std::unexpected(added.error());
                                      }
                                      angle = *added;
                                      return {};
                                  }))
                .has_value());
    CHECK(history.undoDescription() == "Add angle");
    const Sketch withAngle = *doc.findObjectAs<Sketch>(id);
    CHECK(withAngle.findConstraint(angle) != nullptr);

    REQUIRE(history.undo(doc).has_value());
    CHECK(doc.findObjectAs<Sketch>(id)->contentEquals(withLines));
    REQUIRE(history.redo(doc).has_value());
    CHECK(doc.findObjectAs<Sketch>(id)->contentEquals(withAngle));
    // Redo reproduces the IDs; the next constraint gets a new one.
    CHECK(doc.findObjectAs<Sketch>(id)->lastAllocatedConstraintId() == angle.value());

    // A failing edit changes nothing and is not recorded.
    const std::uint64_t revision = doc.revision();
    const auto failed = history.execute(doc, std::make_unique<ModifySketchCommand>(
                                                 created->sketchId(), "Bad angle", [&](Sketch& s) -> Result<void> {
                                                     auto added = s.addAngle(lineA, lineB, 200_deg);
                                                     return added ? Result<void>{} : std::unexpected(added.error());
                                                 }));
    CHECK(errorCode(failed) == ErrorCode::InvalidArgument);
    CHECK(doc.revision() == revision);
    CHECK(history.undoDescription() == "Add angle");
    CHECK(errorCode(history.execute(
              doc, std::make_unique<ModifySketchCommand>(SketchId::fromValue(99), "Missing",
                                                         [](Sketch&) -> Result<void> { return {}; }))) ==
          ErrorCode::NotFound);

    // Undo everything, then redo everything.
    for (int i = 0; i < 3; ++i) {
        REQUIRE(history.undo(doc).has_value());
    }
    CHECK(doc.objectCount() == 0);
    for (int i = 0; i < 3; ++i) {
        REQUIRE(history.redo(doc).has_value());
    }
    CHECK(doc.findObjectAs<Sketch>(id)->contentEquals(withAngle));
}

TEST_CASE("SketchFile_NewConstraintsRoundTrip", "[sketch][io][p12]") {
    test::TempDir dir;
    const auto path = dir.path() / "driven.bcad";
    DrivenSketches m;
    Regenerator before;
    REQUIRE(requireReport(before, m.doc).succeeded());
    const Document expected = m.doc.clone();
    REQUIRE(io::saveDocument(m.doc, path).has_value());

    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    CHECK(equivalent(expected, *loaded));
    Regenerator after;
    const RegenerationReport report = requireReport(after, *loaded);
    REQUIRE(report.succeeded());
    CHECK(std::bit_cast<std::uint64_t>(volumeMm3(after, m.slot)) ==
          std::bit_cast<std::uint64_t>(volumeMm3(before, m.slot)));
    CHECK(std::bit_cast<std::uint64_t>(volumeMm3(after, m.wedge)) ==
          std::bit_cast<std::uint64_t>(volumeMm3(before, m.wedge)));
    CHECK(equivalent(expected, *loaded));

    const std::string text = test::readFile(path);
    CHECK_THAT(text, ContainsSubstring("\"type\": \"angle\","));
    CHECK_THAT(text, ContainsSubstring("\"angle\": 0.6981317007977318,")); // 40 deg, as evaluated
    CHECK_THAT(text, ContainsSubstring("\"type\": \"diameter\","));
    CHECK_THAT(text, ContainsSubstring("\"type\": \"tangent\","));
    // The file is deterministic.
    CHECK(io::documentToJson(*loaded).value() == text);
}

TEST_CASE("SketchFile_MalformedNewConstraintsAreRejectedWithThePath", "[sketch][io][p12]") {
    DrivenSketches m;
    const std::string good = io::documentToJson(m.doc).value();
    const auto replaced = [&](std::string_view from, std::string_view to) {
        std::string text = good;
        const auto pos = text.find(from);
        REQUIRE(pos != std::string::npos);
        text.replace(pos, from.size(), to);
        return io::documentFromJson(text);
    };
    const auto angleAt = good.find("\"type\": \"angle\"");
    REQUIRE(angleAt != std::string::npos);

    const auto noAngle = replaced("\"angle\": 0.017453292519943295,", "");
    REQUIRE_FALSE(noAngle.has_value());
    CHECK_THAT(noAngle.error().message,
               ContainsSubstring("an angle constraint needs an angle between 0 and 180 deg (exclusive), got none"));
    const auto asText = replaced("\"angle\": 0.017453292519943295", "\"angle\": \"1 deg\"");
    REQUIRE_FALSE(asText.has_value());
    CHECK_THAT(asText.error().message, ContainsSubstring(".angle: expected a number"));
    const auto unknown = replaced("\"type\": \"tangent\"", "\"type\": \"kissing\"");
    REQUIRE_FALSE(unknown.has_value());
    CHECK_THAT(unknown.error().message, ContainsSubstring("unknown type 'kissing'"));
}
