#include "FeatureTestSupport.hpp"
#include "support/BracketModel.hpp"
#include "support/ChamferBlockModel.hpp"
#include "support/FilletModels.hpp"
#include "support/HoleModels.hpp"
#include "support/LoftModels.hpp"
#include "support/MirrorModels.hpp"
#include "support/PatternModels.hpp"
#include "support/SweepModels.hpp"
#include "support/TurnedPartModel.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/ChamferFeature.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/FilletFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/LoftFeature.hpp>
#include <bettercad/features/MirrorFeature.hpp>
#include <bettercad/features/ResultBodies.hpp>
#include <bettercad/features/RevolveFeature.hpp>
#include <bettercad/features/SweepFeature.hpp>
#include <bettercad/features/Validation.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <format>
#include <memory>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using namespace bettercad::sketch;
using bettercad::test::addRectangle;
using bettercad::test::BoltCircleModel;
using bettercad::test::BracketModel;
using bettercad::test::ChannelModel;
using bettercad::test::ChamferBlockModel;
using bettercad::test::FilletBlockModel;
using bettercad::test::HoleBlockModel;
using bettercad::test::HoleMirrorModel;
using bettercad::test::HoleRowModel;
using bettercad::test::require;
using bettercad::test::TaperedHoleModel;
using bettercad::test::TurnedPartModel;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;
using Catch::Matchers::WithinRel;

namespace {

/// The issues of one check, for assertions.
std::vector<ValidationIssue> issuesOf(const ValidationReport& report, ValidationCheck check) {
    std::vector<ValidationIssue> result;
    std::ranges::copy_if(report.issues, std::back_inserter(result),
                         [&](const ValidationIssue& issue) { return issue.check == check; });
    return result;
}

std::string describe(const ValidationReport& report) {
    std::string text;
    for (const ValidationIssue& issue : report.issues) {
        text += std::format("[{}] {}: {}\n", toString(issue.check), toString(issue.severity), issue.message);
    }
    return text;
}

template <typename Mutation>
void modifySketch(Document& doc, ObjectId id, Mutation&& mutation) {
    REQUIRE(doc.modifyObject<Sketch>(id, [&](Sketch& sketch) -> Result<bool> {
                   std::forward<Mutation>(mutation)(sketch);
                   return true;
               }).has_value());
}

void setDefinition(Document& doc, ObjectId id, const ExtrudeDefinition& definition) {
    REQUIRE(doc.modifyObject<ExtrudeFeature>(id, [&](ExtrudeFeature& feature) {
                   return feature.setDefinition(definition);
               }).has_value());
}

} // namespace

TEST_CASE("A sound model passes every check", "[validation]") {
    BracketModel model;
    const ValidationReport report = validateDocument(model.doc);
    INFO(describe(report));

    CHECK(report.valid());
    CHECK(report.count(Severity::Error) == 0);
    // No sketch is fully constrained: Base's construction arc is free, the
    // pocket square has only an Equal constraint, and the slot can move in
    // its plane. Those are warnings, not errors.
    const auto sketches = issuesOf(report, ValidationCheck::SketchConstraints);
    REQUIRE(sketches.size() == 3);
    CHECK(sketches[0].item == model.base);
    CHECK(sketches[0].message == "Base (object:8) is under-constrained: 5 degree(s) of freedom");
    CHECK(sketches[1].item == model.pocketSketch);
    CHECK(sketches[2].item == model.slotSketch);
    CHECK(std::ranges::all_of(sketches, [](const auto& issue) { return issue.severity == Severity::Warning; }));
    CHECK(report.count(Severity::Warning) == 3);
    CHECK(report.regenerated == 6);

    REQUIRE(report.bodies.size() == 2);
    CHECK(report.bodies[0].feature == model.pocket);
    CHECK(report.bodies[1].feature == model.slot);
    CHECK(report.bodies[0].valid);
    REQUIRE(report.bodies[0].properties.has_value());
    CHECK_THAT(report.bodies[0].properties->volume.in(units::mm3), WithinRel(BracketModel::kPocketVolume, 1e-12));
    CHECK_THAT(report.bodies[1].properties->volume.in(units::mm3), WithinRel(BracketModel::kSlotVolume, 1e-12));
    REQUIRE(report.bodies[0].boundingBox.has_value());
    CHECK(report.bodies[0].boundingBox->max.x.in(units::mm) == 100.0);
    CHECK(report.bodies[0].topology.solids == 1);
}

TEST_CASE("Validation works on a copy", "[validation]") {
    BracketModel model;
    const std::uint64_t revision = model.doc.revision();
    const Document before = model.doc.clone();
    [[maybe_unused]] const ValidationReport report = validateDocument(model.doc);
    CHECK(model.doc.revision() == revision);
    CHECK(equivalent(before, model.doc));
}

TEST_CASE("Result features exclude bodies consumed by other features", "[validation]") {
    BracketModel model;
    CHECK(resultFeatures(model.doc) == std::vector<ObjectId>{model.pocket, model.slot});
    CHECK(resultFeatures(Document("Empty")).empty());
}

TEST_CASE("Missing references are reported once, under their own check", "[validation]") {
    BracketModel model;
    REQUIRE(model.doc.removeObject(model.pocketSketch).has_value());
    const ValidationReport report = validateDocument(model.doc);
    INFO(describe(report));

    const auto missing = issuesOf(report, ValidationCheck::MissingReferences);
    REQUIRE(missing.size() == 1);
    CHECK(missing[0].item == model.pocket);
    CHECK_THAT(missing[0].message, ContainsSubstring("Pocket (object:11) references object:10"));
    // The pocket cannot regenerate; that is not repeated as a regeneration error.
    CHECK(issuesOf(report, ValidationCheck::FeatureRegeneration).empty());
    CHECK_FALSE(report.valid());
    // The rest of the model still produces its bodies.
    REQUIRE(report.bodies.size() == 1);
    CHECK(report.bodies[0].feature == model.slot);
}

TEST_CASE("Dependency cycles are reported and block their dependents", "[validation]") {
    BracketModel model;
    // Make the pad cut the pocket, which already cuts the pad.
    ExtrudeDefinition definition = model.doc.findObjectAs<ExtrudeFeature>(model.pad)->definition();
    definition.operation = FeatureOperation::Join;
    definition.target = BracketModel::featureId(model.pocket);
    setDefinition(model.doc, model.pad, definition);

    const ValidationReport report = validateDocument(model.doc);
    INFO(describe(report));
    const auto cycles = issuesOf(report, ValidationCheck::DependencyCycles);
    REQUIRE(cycles.size() == 1);
    CHECK(cycles[0].message == "dependency cycle: Pad (object:9), Pocket (object:11)");
    CHECK(issuesOf(report, ValidationCheck::FeatureRegeneration).empty());
    CHECK_FALSE(report.valid());
}

TEST_CASE("Sketches that do not solve are sketch-constraint errors", "[validation]") {
    BracketModel model;
    SECTION("conflicting dimensions") {
        // A second length on the bottom line, driven by height, contradicts width.
        modifySketch(model.doc, model.base, [&](Sketch& sketch) {
            const ConstraintId id = require(sketch.addDistance(EntityId::fromValue(5), 50_mm));
            REQUIRE(sketch.setConstraintParameter(id, model.height).has_value());
        });
        const ValidationReport report = validateDocument(model.doc);
        const auto sketches = issuesOf(report, ValidationCheck::SketchConstraints);
        REQUIRE_FALSE(sketches.empty());
        CHECK(sketches[0].severity == Severity::Error);
        CHECK(sketches[0].item == model.base);
        CHECK_THAT(sketches[0].message, ContainsSubstring("INCONSISTENT"));
        // Pad and Pocket cannot be built: reported as regeneration errors.
        const auto regeneration = issuesOf(report, ValidationCheck::FeatureRegeneration);
        REQUIRE(regeneration.size() == 2);
        CHECK(regeneration[0].item == model.pad);
        CHECK_THAT(regeneration[0].message, ContainsSubstring("because an item it depends on failed"));
        CHECK_FALSE(report.valid());
    }
    SECTION("redundant constraints") {
        modifySketch(model.doc, model.base,
                     [&](Sketch& sketch) { require(sketch.addHorizontal(EntityId::fromValue(5))); });
        const ValidationReport report = validateDocument(model.doc);
        const auto sketches = issuesOf(report, ValidationCheck::SketchConstraints);
        REQUIRE_FALSE(sketches.empty());
        CHECK_THAT(sketches[0].message, ContainsSubstring("OVER_CONSTRAINED"));
    }
    SECTION("an invalid driving value") {
        REQUIRE(model.doc.setParameterValue(model.holeRadius, -(1_mm)).has_value());
        const ValidationReport report = validateDocument(model.doc);
        const auto sketches = issuesOf(report, ValidationCheck::SketchConstraints);
        REQUIRE_FALSE(sketches.empty());
        CHECK(sketches[0].item == model.base);
        CHECK(sketches[0].severity == Severity::Error);
    }
}

TEST_CASE("References to the wrong kind of item are consistency errors", "[validation]") {
    BracketModel model;
    SECTION("a profile that is not a sketch") {
        ExtrudeDefinition definition = model.doc.findObjectAs<ExtrudeFeature>(model.slot)->definition();
        definition.profile = SketchId::fromValue(model.pad.value());
        setDefinition(model.doc, model.slot, definition);
        const auto issues = issuesOf(validateDocument(model.doc), ValidationCheck::DocumentConsistency);
        REQUIRE(issues.size() == 1);
        CHECK(issues[0].message ==
              "Slot (object:13): the profile is Pad (object:9), which is an extrude, not a sketch");
    }
    SECTION("a depth parameter that is an angle") {
        ExtrudeDefinition definition = model.doc.findObjectAs<ExtrudeFeature>(model.slot)->definition();
        definition.depthParameter = model.draft;
        setDefinition(model.doc, model.slot, definition);
        const auto issues = issuesOf(validateDocument(model.doc), ValidationCheck::DocumentConsistency);
        REQUIRE(issues.size() == 1);
        CHECK(issues[0].message ==
              "Slot (object:13): the depth is driven by draft (object:6), which is an angle, not a length");
    }
    SECTION("a depth 'parameter' that is an object") {
        ExtrudeDefinition definition = model.doc.findObjectAs<ExtrudeFeature>(model.slot)->definition();
        definition.depthParameter = ParameterId::fromValue(model.base.value());
        setDefinition(model.doc, model.slot, definition);
        const auto issues = issuesOf(validateDocument(model.doc), ValidationCheck::DocumentConsistency);
        REQUIRE(issues.size() == 1);
        CHECK_THAT(issues[0].message, ContainsSubstring("Base (object:8), which is a sketch, not a parameter"));
    }
    SECTION("a target without a body") {
        ExtrudeDefinition definition = model.doc.findObjectAs<ExtrudeFeature>(model.pocket)->definition();
        definition.target = FeatureId::fromValue(model.base.value());
        setDefinition(model.doc, model.pocket, definition);
        const ValidationReport report = validateDocument(model.doc);
        const auto issues = issuesOf(report, ValidationCheck::DocumentConsistency);
        REQUIRE(issues.size() == 1);
        CHECK_THAT(issues[0].message,
                   ContainsSubstring("the target is Base (object:8), which is a sketch, not a feature with a body"));
        // Not repeated when the pocket then fails to regenerate.
        CHECK(issuesOf(report, ValidationCheck::FeatureRegeneration).empty());
    }
    SECTION("a constraint driven by an angle") {
        modifySketch(model.doc, model.base, [&](Sketch& sketch) {
            REQUIRE(sketch.setConstraintParameter(ConstraintId::fromValue(6), model.draft).has_value());
        });
        const auto issues = issuesOf(validateDocument(model.doc), ValidationCheck::DocumentConsistency);
        REQUIRE(issues.size() == 1);
        CHECK(issues[0].message ==
              "Base (object:8): constraint:6 is driven by draft (object:6), which is an angle, not a length");
    }
}

TEST_CASE("Features that fail to build are regeneration errors", "[validation]") {
    BracketModel model;
    // An open profile: remove one side of the pocket square.
    modifySketch(model.doc, model.pocketSketch, [](Sketch& sketch) {
        REQUIRE(sketch.removeConstraint(ConstraintId::fromValue(1)).has_value());
        REQUIRE(sketch.removeEntity(EntityId::fromValue(5)).has_value());
    });
    const ValidationReport report = validateDocument(model.doc);
    INFO(describe(report));
    const auto regeneration = issuesOf(report, ValidationCheck::FeatureRegeneration);
    REQUIRE(regeneration.size() == 1);
    CHECK(regeneration[0].item == model.pocket);
    CHECK_THAT(regeneration[0].message, ContainsSubstring("Pocket (object:11) failed to regenerate"));
    CHECK_FALSE(report.valid());
}

TEST_CASE("Bodies without material are geometry errors", "[validation]") {
    // A cut that removes the whole target leaves nothing.
    Document doc("Void");
    auto block = std::make_unique<Sketch>("Block");
    addRectangle(*block, 0_mm, 0_mm, 10_mm, 10_mm);
    const ObjectId sketch = doc.addObject(std::move(block)).value();
    auto pad = ExtrudeFeature::create("Pad", {.profile = SketchId::fromValue(sketch.value()), .depth = 5_mm});
    const ObjectId padId = doc.addObject(std::move(*pad)).value();
    auto cut = ExtrudeFeature::create("Cut", {.profile = SketchId::fromValue(sketch.value()),
                                              .depth = 5_mm,
                                              .operation = FeatureOperation::Cut,
                                              .target = FeatureId::fromValue(padId.value())});
    const ObjectId cutId = doc.addObject(std::move(*cut)).value();

    const ValidationReport report = validateDocument(doc);
    INFO(describe(report));
    const auto geometry = issuesOf(report, ValidationCheck::Geometry);
    REQUIRE(geometry.size() == 1);
    CHECK(geometry[0].item == cutId);
    CHECK_FALSE(report.valid());
    REQUIRE(report.bodies.size() == 1);
    CHECK_FALSE(report.bodies[0].valid);
}

TEST_CASE("An empty document is valid", "[validation]") {
    const ValidationReport report = validateDocument(Document("Empty"));
    CHECK(report.valid());
    CHECK(report.issues.empty());
    CHECK(report.bodies.empty());
}

TEST_CASE("Revolve models are validated like any other", "[validation][revolve]") {
    TurnedPartModel m;
    const auto setRevolve = [&](ObjectId id, auto&& change) {
        RevolveDefinition d = m.doc.findObjectAs<RevolveFeature>(id)->definition();
        change(d);
        REQUIRE(m.doc.modifyObject<RevolveFeature>(id, [&](RevolveFeature& f) { return f.setDefinition(d); })
                    .has_value());
    };

    SECTION("a sound model: only the unconstrained groove sketch is worth a warning") {
        const ValidationReport report = validateDocument(m.doc);
        INFO(describe(report));
        CHECK(report.valid());
        REQUIRE(report.issues.size() == 1);
        CHECK(report.issues[0].item == m.grooveSketch);
        CHECK(report.issues[0].severity == Severity::Warning);
        CHECK(resultFeatures(m.doc) == std::vector<ObjectId>{m.groove});
        REQUIRE(report.bodies.size() == 1);
        CHECK(report.bodies[0].valid);
        CHECK_THAT(report.bodies[0].properties->volume.in(units::mm3),
                   WithinRel(TurnedPartModel::expectedVolume(15, 40, 360, 5), 1e-12));
    }
    SECTION("an angle driven by a length") {
        setRevolve(m.turn, [&](RevolveDefinition& d) { d.angleParameter = m.radius; });
        const auto issues = issuesOf(validateDocument(m.doc), ValidationCheck::DocumentConsistency);
        REQUIRE(issues.size() == 1);
        CHECK(issues[0].message ==
              "Turn (object:6): the angle is driven by radius (object:1), which is a length, not an angle");
    }
    SECTION("an axis entity that is not a line") {
        setRevolve(m.groove, [](RevolveDefinition& d) { d.axis = RevolveAxis::alongLine(EntityId::fromValue(1)); });
        const ValidationReport report = validateDocument(m.doc);
        const auto issues = issuesOf(report, ValidationCheck::DocumentConsistency);
        REQUIRE(issues.size() == 1);
        CHECK(issues[0].message == "Groove (object:10): the axis is entity:1, which is a point, not a line");
        CHECK(issuesOf(report, ValidationCheck::FeatureRegeneration).empty()); // reported once
    }
    SECTION("an axis line that does not exist") {
        setRevolve(m.groove, [](RevolveDefinition& d) { d.axis = RevolveAxis::alongLine(EntityId::fromValue(99)); });
        const ValidationReport report = validateDocument(m.doc);
        const auto missing = issuesOf(report, ValidationCheck::MissingReferences);
        REQUIRE(missing.size() == 1);
        CHECK(missing[0].message ==
              "Groove (object:10): the axis line entity:99 does not exist in GrooveSketch (object:9)");
        CHECK(issuesOf(report, ValidationCheck::FeatureRegeneration).empty());
    }
    SECTION("a profile that crosses the axis") {
        REQUIRE(m.doc.modifyObject<Sketch>(m.grooveSketch, [](Sketch& s) -> Result<bool> {
                       REQUIRE(s.setPointPosition(EntityId::fromValue(1), Point2D{-(2_mm), 15_mm}).has_value());
                       return s.setPointPosition(EntityId::fromValue(4), Point2D{-(2_mm), 20_mm});
                   }).has_value());
        const ValidationReport report = validateDocument(m.doc);
        const auto regeneration = issuesOf(report, ValidationCheck::FeatureRegeneration);
        REQUIRE(regeneration.size() == 1);
        CHECK(regeneration[0].item == m.groove);
        CHECK_THAT(regeneration[0].message, ContainsSubstring("crosses the revolution axis"));
        CHECK_FALSE(report.valid());
    }
}

TEST_CASE("ChamferFeature_ModelsAreValidatedLikeAnyOther", "[validation][chamfer]") {
    ChamferBlockModel m;

    SECTION("a sound model: the chamfer is the one valid result body") {
        const ValidationReport report = validateDocument(m.doc);
        INFO(describe(report));
        CHECK(report.valid());
        CHECK(report.issues.empty());
        CHECK(report.regenerated == 3);
        REQUIRE(report.bodies.size() == 1);
        CHECK(report.bodies[0].feature == m.edge);
        CHECK(report.bodies[0].valid);
        CHECK(report.bodies[0].topology.faces == 7);
        CHECK_THAT(report.bodies[0].properties->volume.in(units::mm3), WithinRel(98750.0, 1e-12));
    }
    SECTION("a distance driven by an angle") {
        const ParameterId tilt = m.doc.createParameter("tilt", 5_deg, units::deg).value();
        ChamferDefinition d = m.definitionOf(m.edge);
        d.distanceParameter = tilt;
        m.setDefinition(m.edge, d);
        const ValidationReport report = validateDocument(m.doc);
        const auto issues = issuesOf(report, ValidationCheck::DocumentConsistency);
        REQUIRE(issues.size() == 1);
        CHECK(issues[0].message ==
              "Edge (object:7): the distance is driven by tilt (object:8), which is an angle, not a length");
        CHECK(issuesOf(report, ValidationCheck::FeatureRegeneration).empty()); // reported once
    }
    SECTION("a target that is a sketch") {
        ChamferDefinition d = m.definitionOf(m.edge);
        d.target = ChamferBlockModel::featureId(m.base);
        m.setDefinition(m.edge, d);
        const auto issues = issuesOf(validateDocument(m.doc), ValidationCheck::DocumentConsistency);
        REQUIRE(issues.size() == 1);
        CHECK(issues[0].message ==
              "Edge (object:7): the target is Base (object:5), which is a sketch, not a feature with a body");
    }
    SECTION("an edge that the model no longer has") {
        REQUIRE(m.doc.setParameterValue(m.height, 30_mm).has_value());
        const ValidationReport report = validateDocument(m.doc);
        INFO(describe(report));
        const auto regeneration = issuesOf(report, ValidationCheck::FeatureRegeneration);
        REQUIRE(regeneration.size() == 1);
        CHECK(regeneration[0].item == m.edge);
        CHECK(regeneration[0].message == "Edge (object:7) failed to regenerate: Edge: chamfer: edge reference 1 "
                                         "(line through (0, 0, 20) mm along (1, 0, 0)) matches no edge of the body");
        CHECK_FALSE(report.valid());
        CHECK(report.bodies.empty()); // the pad is consumed and the chamfer has no body
    }
}

TEST_CASE("FilletFeature_ModelsAreValidatedLikeAnyOther", "[validation][fillet]") {
    FilletBlockModel m;

    SECTION("a sound model: the fillet is the one valid result body") {
        const ValidationReport report = validateDocument(m.doc);
        INFO(describe(report));
        CHECK(report.valid());
        CHECK(report.issues.empty());
        CHECK(report.regenerated == 3);
        REQUIRE(report.bodies.size() == 1);
        CHECK(report.bodies[0].feature == m.round);
        CHECK(report.bodies[0].valid);
        CHECK(report.bodies[0].topology.faces == 7);
        CHECK_THAT(report.bodies[0].properties->volume.in(units::mm3),
                   WithinRel(FilletBlockModel::expectedVolume(100, 50, 20, 5), 1e-12));
    }
    SECTION("a radius driven by an angle") {
        const ParameterId tilt = m.doc.createParameter("tilt", 5_deg, units::deg).value();
        FilletDefinition d = m.definitionOf(m.round);
        d.radiusParameter = tilt;
        m.setDefinition(m.round, d);
        const ValidationReport report = validateDocument(m.doc);
        const auto issues = issuesOf(report, ValidationCheck::DocumentConsistency);
        REQUIRE(issues.size() == 1);
        CHECK(issues[0].message ==
              "Round (object:7): the radius is driven by tilt (object:8), which is an angle, not a length");
        CHECK(issuesOf(report, ValidationCheck::FeatureRegeneration).empty()); // reported once
    }
    SECTION("a target that is a sketch") {
        FilletDefinition d = m.definitionOf(m.round);
        d.target = FilletBlockModel::featureId(m.base);
        m.setDefinition(m.round, d);
        const auto issues = issuesOf(validateDocument(m.doc), ValidationCheck::DocumentConsistency);
        REQUIRE(issues.size() == 1);
        CHECK(issues[0].message ==
              "Round (object:7): the target is Base (object:5), which is a sketch, not a feature with a body");
    }
    SECTION("an edge that the model no longer has") {
        REQUIRE(m.doc.setParameterValue(m.height, 30_mm).has_value());
        const ValidationReport report = validateDocument(m.doc);
        INFO(describe(report));
        const auto regeneration = issuesOf(report, ValidationCheck::FeatureRegeneration);
        REQUIRE(regeneration.size() == 1);
        CHECK(regeneration[0].item == m.round);
        CHECK(regeneration[0].message == "Round (object:7) failed to regenerate: Round: fillet: edge reference 1 "
                                         "(line through (0, 0, 20) mm along (1, 0, 0)) matches no edge of the body");
        CHECK_FALSE(report.valid());
        CHECK(report.bodies.empty());
    }
}

TEST_CASE("HoleFeature_ModelsAreValidatedLikeAnyOther", "[validation][hole]") {
    HoleBlockModel m;

    SECTION("a sound model: the hole is the one valid result body") {
        const ValidationReport report = validateDocument(m.doc);
        INFO(describe(report));
        CHECK(report.valid());
        CHECK(report.issues.empty());
        CHECK(report.regenerated == 3);
        REQUIRE(report.bodies.size() == 1);
        CHECK(report.bodies[0].feature == m.drill);
        CHECK(report.bodies[0].valid);
        CHECK(report.bodies[0].topology.faces == 7);
        CHECK_THAT(report.bodies[0].properties->volume.in(units::mm3),
                   WithinRel(HoleBlockModel::expectedVolume(100, 50, 20, 10), 1e-12));
    }
    SECTION("dimensions driven by an angle") {
        const ParameterId tilt = m.doc.createParameter("tilt", 5_deg, units::deg).value();
        HoleDefinition d = m.definitionOf(m.drill);
        d.diameterParameter = tilt;
        d.centerVParameter = tilt;
        m.setDefinition(m.drill, d);
        const ValidationReport report = validateDocument(m.doc);
        const auto issues = issuesOf(report, ValidationCheck::DocumentConsistency);
        REQUIRE(issues.size() == 2);
        CHECK(issues[0].message ==
              "Drill (object:9): the diameter is driven by tilt (object:10), which is an angle, not a length");
        CHECK(issues[1].message == "Drill (object:9): the centre's v coordinate is driven by tilt (object:10), which "
                                   "is an angle, not a length");
        CHECK(issuesOf(report, ValidationCheck::FeatureRegeneration).empty()); // reported once
    }
    SECTION("a target that is a sketch") {
        HoleDefinition d = m.definitionOf(m.drill);
        d.target = HoleBlockModel::featureId(m.base);
        m.setDefinition(m.drill, d);
        const auto issues = issuesOf(validateDocument(m.doc), ValidationCheck::DocumentConsistency);
        REQUIRE(issues.size() == 1);
        CHECK(issues[0].message ==
              "Drill (object:9): the target is Base (object:5), which is a sketch, not a feature with a body");
    }
    SECTION("a placement face that the model no longer has") {
        REQUIRE(m.doc.setParameterValue(m.height, 30_mm).has_value());
        const ValidationReport report = validateDocument(m.doc);
        INFO(describe(report));
        const auto regeneration = issuesOf(report, ValidationCheck::FeatureRegeneration);
        REQUIRE(regeneration.size() == 1);
        CHECK(regeneration[0].item == m.drill);
        CHECK(regeneration[0].message == "Drill (object:9) failed to regenerate: Drill: hole: the placement face "
                                         "(plane through (0, 0, 20) mm facing (0, 0, 1)) matches no face of the body");
        CHECK_FALSE(report.valid());
        CHECK(report.bodies.empty());
    }
}

TEST_CASE("LinearPattern_ModelsAreValidatedLikeAnyOther", "[validation][pattern]") {
    HoleRowModel m;

    SECTION("a sound model: the pattern is the one valid result body") {
        const ValidationReport report = validateDocument(m.doc);
        INFO(describe(report));
        CHECK(report.valid());
        CHECK(report.issues.empty());
        CHECK(report.regenerated == 4);
        REQUIRE(report.bodies.size() == 1);
        CHECK(report.bodies[0].feature == m.holes);
        CHECK(report.bodies[0].valid);
        CHECK(report.bodies[0].topology.solids == 1);
        CHECK_THAT(report.bodies[0].properties->volume.in(units::mm3),
                   WithinRel(HoleRowModel::expectedVolume(120, 20, 10, 5), 1e-12));
    }
    SECTION("a count driven by a length and a spacing driven by a number") {
        LinearPatternDefinition d = m.patternOf(m.holes);
        d.first.countParameter = m.width;
        d.first.spacingParameter = m.count;
        m.setPattern(m.holes, d);
        const ValidationReport report = validateDocument(m.doc);
        const auto issues = issuesOf(report, ValidationCheck::DocumentConsistency);
        REQUIRE(issues.size() == 2);
        CHECK(issues[0].message == "Holes (object:10): direction 1's count is driven by width (object:1), which is a "
                                   "length, not dimensionless");
        CHECK(issues[1].message == "Holes (object:10): direction 1's spacing is driven by count (object:7), which is "
                                   "dimensionless, not a length");
        CHECK(issuesOf(report, ValidationCheck::FeatureRegeneration).empty()); // reported once
    }
    SECTION("a source that is a sketch") {
        LinearPatternDefinition d = m.patternOf(m.holes);
        d.source = HoleRowModel::featureId(m.base);
        m.setPattern(m.holes, d);
        const auto issues = issuesOf(validateDocument(m.doc), ValidationCheck::DocumentConsistency);
        REQUIRE(issues.size() == 1);
        CHECK(issues[0].message ==
              "Holes (object:10): the source is Base (object:5), which is a sketch, not a feature with a body");
    }
    SECTION("an instance that does not fit") {
        REQUIRE(m.doc.setParameterValue(m.count, 6.0, kUnitless).has_value());
        const ValidationReport report = validateDocument(m.doc);
        INFO(describe(report));
        const auto regeneration = issuesOf(report, ValidationCheck::FeatureRegeneration);
        REQUIRE(regeneration.size() == 1);
        CHECK(regeneration[0].item == m.holes);
        CHECK_THAT(regeneration[0].message,
                   StartsWith("Holes (object:10) failed to regenerate: Holes: linear pattern: instance 5 at "
                              "(100, 0, 0) mm: hole: the hole does not fit on its face"));
        CHECK_FALSE(report.valid());
        CHECK(report.bodies.empty());
    }
}

TEST_CASE("CircularPattern_ModelsAreValidatedLikeAnyOther", "[validation][pattern][circular]") {
    BoltCircleModel m;

    SECTION("a sound model: the pattern is the one valid result body") {
        const ValidationReport report = validateDocument(m.doc);
        INFO(describe(report));
        CHECK(report.valid());
        CHECK(report.issues.empty());
        CHECK(report.regenerated == 4);
        REQUIRE(report.bodies.size() == 1);
        CHECK(report.bodies[0].feature == m.bolts);
        CHECK(report.bodies[0].valid);
        CHECK(report.bodies[0].topology.solids == 1);
        CHECK_THAT(report.bodies[0].properties->volume.in(units::mm3),
                   WithinRel(BoltCircleModel::expectedVolume(60, 10, 10, 6), 1e-12));
    }
    SECTION("a count driven by a length and an angle driven by a number") {
        CircularPatternDefinition d = m.definitionOf(m.bolts);
        d.countParameter = m.radius;
        d.spacing = CircularSpacing::IncludedAngle;
        d.angleParameter = m.count;
        m.setDefinition(m.bolts, d);
        const ValidationReport report = validateDocument(m.doc);
        const auto issues = issuesOf(report, ValidationCheck::DocumentConsistency);
        REQUIRE(issues.size() == 2);
        CHECK(issues[0].message == "Bolts (object:8): the count is driven by radius (object:1), which is a length, "
                                   "not dimensionless");
        CHECK(issues[1].message == "Bolts (object:8): the angle is driven by count (object:4), which is "
                                   "dimensionless, not an angle");
        CHECK(issuesOf(report, ValidationCheck::FeatureRegeneration).empty()); // reported once
    }
    SECTION("a source that is a sketch") {
        CircularPatternDefinition d = m.definitionOf(m.bolts);
        d.source = BoltCircleModel::featureId(m.disc);
        m.setDefinition(m.bolts, d);
        const auto issues = issuesOf(validateDocument(m.doc), ValidationCheck::DocumentConsistency);
        REQUIRE(issues.size() == 1);
        CHECK(issues[0].message ==
              "Bolts (object:8): the source is Disc (object:5), which is a sketch, not a feature with a body");
    }
    SECTION("an instance that does not fit") {
        REQUIRE(m.doc.setParameterValue(m.count, 40.0, kUnitless).has_value());
        const ValidationReport report = validateDocument(m.doc);
        INFO(describe(report));
        const auto regeneration = issuesOf(report, ValidationCheck::FeatureRegeneration);
        REQUIRE(regeneration.size() == 1);
        CHECK(regeneration[0].item == m.bolts);
        CHECK_THAT(regeneration[0].message,
                   StartsWith("Bolts (object:8) failed to regenerate: Bolts: circular pattern: instance 1 at 9 deg: "
                              "hole: the hole does not fit on its face"));
        CHECK_FALSE(report.valid());
        CHECK(report.bodies.empty());
    }
}

TEST_CASE("MirrorFeature_ModelsAreValidatedLikeAnyOther", "[validation][mirror]") {
    HoleMirrorModel m;

    SECTION("a sound model: the mirror is the one valid result body") {
        const ValidationReport report = validateDocument(m.doc);
        INFO(describe(report));
        CHECK(report.valid());
        CHECK(report.issues.empty());
        CHECK(report.regenerated == 4);
        REQUIRE(report.bodies.size() == 1);
        CHECK(report.bodies[0].feature == m.mirror);
        CHECK(report.bodies[0].valid);
        CHECK(report.bodies[0].topology.solids == 1);
        CHECK_THAT(report.bodies[0].properties->volume.in(units::mm3),
                   WithinRel(HoleMirrorModel::expectedVolume(100, 20, 10), 1e-12));
    }
    SECTION("a plane offset driven by an angle") {
        const ParameterId tilt = m.doc.createParameter("tilt", 5_deg, units::deg).value();
        MirrorDefinition d = m.mirrorOf(m.mirror);
        d.plane.offsetParameter = tilt;
        m.setMirror(m.mirror, d);
        const ValidationReport report = validateDocument(m.doc);
        const auto issues = issuesOf(report, ValidationCheck::DocumentConsistency);
        REQUIRE(issues.size() == 1);
        CHECK(issues[0].message ==
              "Mirror (object:11): the plane's offset is driven by tilt (object:12), which is an angle, not a length");
        CHECK(issuesOf(report, ValidationCheck::FeatureRegeneration).empty()); // reported once
    }
    SECTION("a source that is a sketch") {
        MirrorDefinition d = m.mirrorOf(m.mirror);
        d.source = HoleMirrorModel::featureId(m.base);
        m.setMirror(m.mirror, d);
        const auto issues = issuesOf(validateDocument(m.doc), ValidationCheck::DocumentConsistency);
        REQUIRE(issues.size() == 1);
        CHECK(issues[0].message ==
              "Mirror (object:11): the source is Base (object:5), which is a sketch, not a feature with a body");
    }
    SECTION("a mirror image that does not fit") {
        REQUIRE(m.doc.setParameterValue(m.mid, 64_mm).has_value());
        const ValidationReport report = validateDocument(m.doc);
        INFO(describe(report));
        const auto regeneration = issuesOf(report, ValidationCheck::FeatureRegeneration);
        REQUIRE(regeneration.size() == 1);
        CHECK(regeneration[0].item == m.mirror);
        CHECK_THAT(regeneration[0].message,
                   StartsWith("Mirror (object:11) failed to regenerate: Mirror: mirror: the mirror image across the "
                              "plane through (64, 0, 0) mm facing (1, 0, 0): hole: the hole does not fit on its face"));
        CHECK_FALSE(report.valid());
        CHECK(report.bodies.empty());
    }
}

TEST_CASE("SweepFeature_ModelsAreValidatedLikeAnyOther", "[validation][sweep]") {
    ChannelModel m;

    SECTION("a sound model: the channel is the one valid result body") {
        const ValidationReport report = validateDocument(m.doc);
        INFO(describe(report));
        CHECK(report.valid());
        CHECK(report.issues.empty());
        CHECK(report.regenerated == 5);
        REQUIRE(report.bodies.size() == 1);
        CHECK(report.bodies[0].feature == m.channel);
        CHECK(report.bodies[0].valid);
        CHECK(report.bodies[0].topology.solids == 1);
        CHECK_THAT(report.bodies[0].properties->volume.in(units::mm3),
                   WithinRel(ChannelModel::expectedVolume(20, 5), 1e-12));
    }
    SECTION("a path that is not in a sketch") {
        SweepDefinition d = m.definitionOf<SweepFeature>(m.channel);
        d.path.sketch = SketchId::fromValue(m.pad.value());
        m.setDefinition<SweepFeature>(m.channel, d);
        const ValidationReport report = validateDocument(m.doc);
        const auto issues = issuesOf(report, ValidationCheck::DocumentConsistency);
        REQUIRE(issues.size() == 1);
        CHECK(issues[0].message ==
              "Channel (object:9): the path is in Pad (object:6), which is an extrude, not a sketch");
        CHECK(issuesOf(report, ValidationCheck::FeatureRegeneration).empty()); // reported once
    }
    SECTION("a path edge that is a point, and one that does not exist") {
        SweepDefinition d = m.definitionOf<SweepFeature>(m.channel);
        const EntityId start =
            std::get<LineEntity>(m.doc.findObjectAs<Sketch>(m.channelPath)->findEntity(m.channelLine)->geometry).start;
        d.path.edges = {start};
        m.setDefinition<SweepFeature>(m.channel, d);
        auto issues = issuesOf(validateDocument(m.doc), ValidationCheck::DocumentConsistency);
        REQUIRE(issues.size() == 1);
        CHECK(issues[0].message ==
              std::format("Channel (object:9): the path edge {} is a point, not a line, arc or circle", start));
        d.path.edges = {EntityId::fromValue(99)};
        m.setDefinition<SweepFeature>(m.channel, d);
        const ValidationReport report = validateDocument(m.doc);
        issues = issuesOf(report, ValidationCheck::MissingReferences);
        REQUIRE(issues.size() == 1);
        CHECK(issues[0].message == "Channel (object:9): the path edge entity:99 does not exist in ChannelPath (object:8)");
        CHECK(issuesOf(report, ValidationCheck::FeatureRegeneration).empty());
    }
    SECTION("a sweep that does not build") {
        SweepDefinition d = m.definitionOf<SweepFeature>(m.channel);
        d.profile = SketchId::fromValue(m.base.value()); // not where the path starts
        m.setDefinition<SweepFeature>(m.channel, d);
        const ValidationReport report = validateDocument(m.doc);
        INFO(describe(report));
        const auto regeneration = issuesOf(report, ValidationCheck::FeatureRegeneration);
        REQUIRE(regeneration.size() == 1);
        CHECK(regeneration[0].item == m.channel);
        CHECK(regeneration[0].message == "Channel (object:9) failed to regenerate: Channel: makeSweep: the path must "
                                         "start on the profile's plane, but it starts 10 mm from it");
        CHECK_FALSE(report.valid());
        CHECK(report.bodies.empty());
    }
}

TEST_CASE("LoftFeature_ModelsAreValidatedLikeAnyOther", "[validation][loft]") {
    TaperedHoleModel m;

    SECTION("a sound model: the tapered block is the one valid result body") {
        const ValidationReport report = validateDocument(m.doc);
        INFO(describe(report));
        CHECK(report.valid());
        CHECK(report.issues.empty());
        CHECK(report.regenerated == 5);
        REQUIRE(report.bodies.size() == 1);
        CHECK(report.bodies[0].feature == m.taper);
        CHECK(report.bodies[0].valid);
        CHECK(report.bodies[0].topology.solids == 1);
        CHECK_THAT(report.bodies[0].properties->volume.in(units::mm3),
                   WithinRel(TaperedHoleModel::expectedVolume(20, 15), 1e-12));
    }
    SECTION("a section that is not a sketch") {
        LoftDefinition d = m.definitionOf<LoftFeature>(m.taper);
        d.sections[1].sketch = SketchId::fromValue(m.pad.value());
        m.setDefinition<LoftFeature>(m.taper, d);
        const ValidationReport report = validateDocument(m.doc);
        const auto issues = issuesOf(report, ValidationCheck::DocumentConsistency);
        REQUIRE(issues.size() == 1);
        CHECK(issues[0].message == "Taper (object:9): section 2 is Pad (object:6), which is an extrude, not a sketch");
        CHECK(issuesOf(report, ValidationCheck::FeatureRegeneration).empty()); // reported once
    }
    SECTION("a section's offset driven by an angle") {
        const ParameterId tilt = m.doc.createParameter("tilt", 30_deg, units::deg).value();
        LoftDefinition d = m.definitionOf<LoftFeature>(m.taper);
        d.sections[1].offsetParameter = tilt;
        m.setDefinition<LoftFeature>(m.taper, d);
        const auto issues = issuesOf(validateDocument(m.doc), ValidationCheck::DocumentConsistency);
        REQUIRE(issues.size() == 1);
        CHECK(issues[0].message ==
              "Taper (object:9): section 2's offset is driven by tilt (object:10), which is an angle, not a length");
    }
    SECTION("a section's sketch that no longer exists") {
        REQUIRE(m.doc.removeObject(m.tip).has_value());
        const ValidationReport report = validateDocument(m.doc);
        const auto issues = issuesOf(report, ValidationCheck::MissingReferences);
        REQUIRE(issues.size() == 1);
        CHECK(issues[0].message == "Taper (object:9) references object:8, which does not exist");
        CHECK(issuesOf(report, ValidationCheck::FeatureRegeneration).empty());
    }
    SECTION("a loft that does not build") {
        LoftDefinition d = m.definitionOf<LoftFeature>(m.taper);
        d.sections[1] = {.sketch = SketchId::fromValue(m.base.value())}; // a rectangle against a circle
        m.setDefinition<LoftFeature>(m.taper, d);
        const ValidationReport report = validateDocument(m.doc);
        INFO(describe(report));
        const auto regeneration = issuesOf(report, ValidationCheck::FeatureRegeneration);
        REQUIRE(regeneration.size() == 1);
        CHECK(regeneration[0].item == m.taper);
        CHECK(regeneration[0].message ==
              "Taper (object:9) failed to regenerate: Taper: makeLoft: sections 1 and 2 cannot be matched: section 1 is "
              "a circle and section 2 is 4 lines; lofts between different shapes are not supported");
        CHECK_FALSE(report.valid());
        CHECK(report.bodies.empty());
    }
}
