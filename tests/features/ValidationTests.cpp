#include "FeatureTestSupport.hpp"
#include "support/BracketModel.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/ResultBodies.hpp>
#include <bettercad/features/Validation.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using namespace bettercad::sketch;
using bettercad::test::addRectangle;
using bettercad::test::BracketModel;
using bettercad::test::require;
using Catch::Matchers::ContainsSubstring;
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
