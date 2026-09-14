#include "FeatureTestSupport.hpp"

#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/FeatureCommands.hpp>
#include <bettercad/features/Regenerator.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <memory>
#include <numbers>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using namespace bettercad::sketch;
using bettercad::test::addRectangle;
using bettercad::test::require;
using bettercad::test::requireReport;
using bettercad::test::volumeMm3;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;

namespace {

constexpr double pi = std::numbers::pi;
constexpr double kRel = 1e-12;

// width, height -> Sketch001 -> Extrude001 <- depth
// otherDepth -> Extrude002 <- Sketch002   (an unrelated chain)
struct Model {
    Document doc{"Part"};
    CommandHistory history;
    ParameterId width, height, depth, otherDepth;
    ObjectId sketch1, extrude1, sketch2, extrude2;
    ConstraintId widthConstraint;
    EntityId bottomLine;

    Model() {
        width = doc.createParameter("width", 100_mm, units::mm).value();
        height = doc.createParameter("height", 50_mm, units::mm).value();
        depth = doc.createParameter("depth", 20_mm, units::mm).value();
        otherDepth = doc.createParameter("otherDepth", 10_mm, units::mm).value();

        auto sketch = std::make_unique<Sketch>("Sketch001");
        const auto lines = addRectangle(*sketch, 0_mm, 0_mm, 90_mm, 40_mm);
        bottomLine = lines[0];
        require(sketch->addFixed(std::get<LineEntity>(sketch->findEntity(lines[0])->geometry).start));
        require(sketch->addHorizontal(lines[0]));
        require(sketch->addHorizontal(lines[2]));
        require(sketch->addVertical(lines[1]));
        require(sketch->addVertical(lines[3]));
        widthConstraint = require(sketch->addDistance(lines[0], 1_mm));
        const ConstraintId heightConstraint = require(sketch->addDistance(lines[1], 1_mm));
        REQUIRE(sketch->setConstraintParameter(widthConstraint, width).has_value());
        REQUIRE(sketch->setConstraintParameter(heightConstraint, height).has_value());
        sketch1 = doc.addObject(std::move(sketch)).value();

        extrude1 = addExtrude("Extrude001", {.profile = SketchId::fromValue(sketch1.value()), .depthParameter = depth});

        auto circle = std::make_unique<Sketch>("Sketch002");
        require(circle->addCircle(Point2D{300_mm, 0_mm}, 5_mm));
        sketch2 = doc.addObject(std::move(circle)).value();
        extrude2 = addExtrude("Extrude002", {.profile = SketchId::fromValue(sketch2.value()), .depthParameter = otherDepth});
    }

    ObjectId addExtrude(const std::string& name, const ExtrudeDefinition& definition) {
        auto feature = ExtrudeFeature::create(name, definition);
        REQUIRE(feature.has_value());
        return doc.addObject(std::move(*feature)).value();
    }

    void setParameter(ParameterId id, Length value) {
        REQUIRE(history.execute(doc, ModifyParameterCommand::setValue(id, value)).has_value());
    }
};

} // namespace

TEST_CASE("The first pass builds everything in dependency order", "[regeneration]") {
    Model m;
    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);

    CHECK(report.succeeded());
    CHECK(report.regenerated == std::vector<ObjectId>{m.sketch1, m.extrude1, m.sketch2, m.extrude2});
    CHECK_THAT(volumeMm3(regenerator, m.extrude1), WithinRel(100.0 * 50.0 * 20.0, kRel));
    CHECK_THAT(volumeMm3(regenerator, m.extrude2), WithinRel(pi * 25.0 * 10.0, kRel));
    CHECK(regenerator.state(m.sketch1) == NodeState::Regenerated);
    CHECK(regenerator.state(ObjectId{m.width}) == NodeState::UpToDate);
    // The sketch took its dimensions from the parameters.
    const auto* sketch = m.doc.findObjectAs<Sketch>(m.sketch1);
    CHECK(sketch->findConstraint(m.widthConstraint)->value == 100_mm);
}

TEST_CASE("Without changes, nothing is regenerated", "[regeneration]") {
    Model m;
    Regenerator regenerator;
    requireReport(regenerator, m.doc);
    const RegenerationReport second = requireReport(regenerator, m.doc);

    CHECK(second.changed.empty());
    CHECK(second.regenerated.empty());
    CHECK(regenerator.state(m.extrude1) == NodeState::UpToDate);
    CHECK(regenerator.body(m.extrude1) != nullptr);
}

TEST_CASE("Spec example: changing width rebuilds Sketch001 and Extrude001 only",
          "[regeneration][acceptance]") {
    Model m;
    Regenerator regenerator;
    requireReport(regenerator, m.doc);
    const geometry::Body* unrelatedBefore = regenerator.body(m.extrude2);

    m.setParameter(m.width, 120_mm);
    const RegenerationReport report = requireReport(regenerator, m.doc);

    CHECK(report.changed == std::vector<ObjectId>{ObjectId{m.width}});
    CHECK(report.regenerated == std::vector<ObjectId>{m.sketch1, m.extrude1});
    CHECK(report.succeeded());
    CHECK_THAT(volumeMm3(regenerator, m.extrude1), WithinRel(120.0 * 50.0 * 20.0, kRel));
    const auto* sketch = m.doc.findObjectAs<Sketch>(m.sketch1);
    CHECK_THAT(sketch->length(m.bottomLine)->in(units::mm), WithinRel(120.0, kRel));
    // The unrelated feature was not rebuilt: same state, same body object.
    CHECK(regenerator.state(m.extrude2) == NodeState::UpToDate);
    CHECK(regenerator.body(m.extrude2) == unrelatedBefore);
}

TEST_CASE("Changing the depth rebuilds only the extrude", "[regeneration]") {
    Model m;
    Regenerator regenerator;
    requireReport(regenerator, m.doc);

    m.setParameter(m.depth, 40_mm);
    const RegenerationReport report = requireReport(regenerator, m.doc);
    CHECK(report.regenerated == std::vector<ObjectId>{m.extrude1});
    CHECK_THAT(volumeMm3(regenerator, m.extrude1), WithinRel(200000.0, kRel));

    // Undo is a change too, and restores the previous result.
    REQUIRE(m.history.undo(m.doc).has_value());
    CHECK(requireReport(regenerator, m.doc).regenerated == std::vector<ObjectId>{m.extrude1});
    CHECK_THAT(volumeMm3(regenerator, m.extrude1), WithinRel(100000.0, kRel));
}

TEST_CASE("Editing a feature rebuilds that feature and what depends on it", "[regeneration]") {
    Model m;
    const ObjectId cut = m.addExtrude(
        "Cut001", {.profile = SketchId::fromValue(m.sketch2.value()), .depth = 5_mm,
                   .operation = FeatureOperation::Cut, .target = FeatureId::fromValue(m.extrude2.value())});
    Regenerator regenerator;
    const RegenerationReport first = requireReport(regenerator, m.doc);
    REQUIRE(first.succeeded());
    // Extrude002 must be built before the cut that uses its body.
    const auto position = [&](ObjectId id) {
        return std::ranges::find(first.regenerated, id) - first.regenerated.begin();
    };
    CHECK(position(m.extrude2) < position(cut));
    CHECK_THAT(volumeMm3(regenerator, cut), WithinRel(pi * 25.0 * 5.0, kRel)); // 10 mm cylinder minus 5 mm

    REQUIRE(m.history.execute(m.doc, std::make_unique<ModifyExtrudeCommand>(
                                          FeatureId::fromValue(m.extrude2.value()),
                                          ExtrudeDefinition{.profile = SketchId::fromValue(m.sketch2.value()),
                                                            .depth = 30_mm}))
                .has_value());
    const RegenerationReport report = requireReport(regenerator, m.doc);
    CHECK(report.regenerated == std::vector<ObjectId>{m.extrude2, cut});
    CHECK_THAT(volumeMm3(regenerator, cut), WithinRel(pi * 25.0 * 25.0, kRel));
}

TEST_CASE("Failures are reported and block dependents until fixed", "[regeneration]") {
    Model m;
    Regenerator regenerator;
    requireReport(regenerator, m.doc);

    SECTION("an invalid driving value") {
        m.setParameter(m.width, -(10_mm));
        const RegenerationReport report = requireReport(regenerator, m.doc);
        CHECK(report.failed == std::vector<ObjectId>{m.sketch1});
        CHECK(report.blocked == std::vector<ObjectId>{m.extrude1});
        CHECK(report.errors.at(m.sketch1).code == ErrorCode::InvalidArgument);
    }
    SECTION("a sketch that becomes inconsistent") {
        // A second constraint on the bottom line, driven by height, conflicts with width.
        REQUIRE(m.doc.modifyObject<Sketch>(m.sketch1, [&](Sketch& s) -> Result<bool> {
                   auto id = s.addDistance(m.bottomLine, 50_mm);
                   if (!id) {
                       return std::unexpected(id.error());
                   }
                   return s.setConstraintParameter(*id, m.height);
               }).value());
        const RegenerationReport report = requireReport(regenerator, m.doc);
        CHECK(report.failed == std::vector<ObjectId>{m.sketch1});
        CHECK_THAT(report.errors.at(m.sketch1).message, ContainsSubstring("INCONSISTENT"));
        CHECK(report.blocked == std::vector<ObjectId>{m.extrude1});
    }

    // Failed and blocked items have no (stale) result; unrelated items keep theirs.
    CHECK(regenerator.body(m.extrude1) == nullptr);
    CHECK(regenerator.state(m.extrude1) == NodeState::Blocked);
    CHECK(regenerator.error(m.sketch1) != nullptr);
    CHECK(regenerator.body(m.extrude2) != nullptr);
    CHECK(regenerator.state(m.extrude2) == NodeState::UpToDate);

    // Failed items are retried until they succeed.
    m.history.clear();
    REQUIRE(m.history.execute(m.doc, ModifyParameterCommand::setValue(m.width, 110_mm)).has_value());
    const auto* sketch = m.doc.findObjectAs<Sketch>(m.sketch1);
    const bool conflicting = sketch->constraintCount() > 7;
    if (conflicting) {
        REQUIRE(m.doc.modifyObject<Sketch>(m.sketch1, [](Sketch& s) {
                   return s.removeConstraint(ConstraintId::fromValue(8)).transform([](auto&&) { return true; });
               }).value());
    }
    const RegenerationReport fixed = requireReport(regenerator, m.doc);
    CHECK(fixed.succeeded());
    CHECK(fixed.regenerated == std::vector<ObjectId>{m.sketch1, m.extrude1});
    CHECK_THAT(volumeMm3(regenerator, m.extrude1), WithinRel(110.0 * 50.0 * 20.0, kRel));
}

TEST_CASE("Missing references are reported as failures", "[regeneration]") {
    Model m;
    Regenerator regenerator;
    requireReport(regenerator, m.doc);

    REQUIRE(m.history.execute(m.doc, std::make_unique<DeleteObjectCommand>(m.sketch2)).has_value());
    const RegenerationReport report = requireReport(regenerator, m.doc);
    CHECK(report.failed == std::vector<ObjectId>{m.extrude2});
    CHECK(report.errors.at(m.extrude2).code == ErrorCode::NotFound);
    CHECK(regenerator.body(m.extrude2) == nullptr);
    CHECK(regenerator.state(m.sketch2) == std::nullopt); // forgotten

    REQUIRE(m.history.undo(m.doc).has_value());
    const RegenerationReport restored = requireReport(regenerator, m.doc);
    CHECK(restored.regenerated == std::vector<ObjectId>{m.sketch2, m.extrude2});
    CHECK(restored.succeeded());
}

TEST_CASE("Dependency cycles are reported and block their dependents", "[regeneration]") {
    Model m;
    const auto profile = SketchId::fromValue(m.sketch2.value());
    const ObjectId a = m.addExtrude("A", {.profile = profile, .depth = 1_mm, .operation = FeatureOperation::Join,
                                          .target = FeatureId::fromValue(m.extrude2.value())});
    const ObjectId b = m.addExtrude("B", {.profile = profile, .depth = 1_mm, .operation = FeatureOperation::Join,
                                          .target = FeatureId::fromValue(a.value())});
    const ObjectId c = m.addExtrude("C", {.profile = profile, .depth = 1_mm, .operation = FeatureOperation::Join,
                                          .target = FeatureId::fromValue(b.value())});
    // Point A at B: A -> B -> A.
    REQUIRE(m.doc.modifyObject<ExtrudeFeature>(a, [&](ExtrudeFeature& feature) {
                   ExtrudeDefinition definition = feature.definition();
                   definition.target = FeatureId::fromValue(b.value());
                   return feature.setDefinition(definition);
               }).value());

    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, m.doc);
    REQUIRE(report.cycles.size() == 1);
    CHECK(report.cycles.front() == std::vector<ObjectId>{a, b});
    CHECK(report.failed == std::vector<ObjectId>{a, b});
    CHECK_THAT(report.errors.at(a).message, ContainsSubstring("dependency cycle: A, B"));
    CHECK(report.blocked == std::vector<ObjectId>{c});
    // Everything outside the cycle still regenerates.
    CHECK(regenerator.state(m.extrude1) == NodeState::Regenerated);
    CHECK(regenerator.body(m.extrude1) != nullptr);
}

TEST_CASE("A regenerator belongs to one document; regenerateAll starts over", "[regeneration]") {
    Model m;
    Regenerator regenerator;
    requireReport(regenerator, m.doc);

    Document other("Other");
    CHECK_FALSE(regenerator.regenerate(other).has_value());

    const auto all = regenerator.regenerateAll(m.doc);
    REQUIRE(all.has_value());
    CHECK(all->regenerated.size() == 4);
}

TEST_CASE("Regeneration results are deterministic", "[regeneration]") {
    Model first;
    Model second;
    Regenerator a;
    Regenerator b;
    requireReport(a, first.doc);
    requireReport(b, second.doc);
    first.setParameter(first.width, 123.4_mm);
    second.setParameter(second.width, 123.4_mm);
    requireReport(a, first.doc);
    requireReport(b, second.doc);

    CHECK(std::bit_cast<std::uint64_t>(volumeMm3(a, first.extrude1)) ==
          std::bit_cast<std::uint64_t>(volumeMm3(b, second.extrude1)));
    CHECK(first.doc.findObject(first.sketch1)->contentEquals(*second.doc.findObject(second.sketch1)));
}
