#include "FeatureTestSupport.hpp"

#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Regeneration.hpp>
#include <bettercad/sketch/Solver.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <bit>
#include <cstdint>
#include <memory>
#include <numbers>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using namespace bettercad::sketch;
using bettercad::test::addRectangle;
using bettercad::test::require;
using Catch::Matchers::WithinRel;

namespace {

constexpr double kRel = 1e-12;

double regeneratedVolumeMm3(const Document& doc, FeatureId id) {
    const auto* feature = doc.findObjectAs<ExtrudeFeature>(ObjectId{id});
    REQUIRE(feature != nullptr);
    const auto body = regenerateExtrude(*feature, doc);
    INFO((body ? std::string{} : body.error().message));
    REQUIRE(body.has_value());
    const auto props = body->massProperties();
    REQUIRE(props.has_value());
    return props->volume.in(units::mm3);
}

} // namespace

TEST_CASE("Changing the depth parameter from 20 mm to 40 mm regenerates the solid",
          "[features][regeneration][acceptance]") {
    Document doc("Part");
    CommandHistory history;
    const auto depth = doc.createParameter("depth", 20_mm, units::mm);
    REQUIRE(depth.has_value());

    auto sketch = std::make_unique<Sketch>("Sketch1");
    addRectangle(*sketch, 0_mm, 0_mm, 100_mm, 50_mm);
    const auto sketchId = doc.addObject(std::move(sketch));
    REQUIRE(sketchId.has_value());
    auto feature = ExtrudeFeature::create(
        "Extrude1", {.profile = SketchId::fromValue(sketchId->value()), .depthParameter = *depth});
    REQUIRE(feature.has_value());
    const auto featureObject = doc.addObject(std::move(*feature));
    REQUIRE(featureObject.has_value());
    const auto extrude = FeatureId::fromValue(featureObject->value());

    CHECK_THAT(regeneratedVolumeMm3(doc, extrude), WithinRel(100000.0, kRel));

    REQUIRE(history.execute(doc, ModifyParameterCommand::setValue(*depth, 40_mm)).has_value());
    CHECK_THAT(regeneratedVolumeMm3(doc, extrude), WithinRel(200000.0, kRel));

    REQUIRE(history.undo(doc).has_value());
    CHECK_THAT(regeneratedVolumeMm3(doc, extrude), WithinRel(100000.0, kRel));
    REQUIRE(history.redo(doc).has_value());
    CHECK_THAT(regeneratedVolumeMm3(doc, extrude), WithinRel(200000.0, kRel));
}

TEST_CASE("Sketch changes flow into the extrude after solving", "[features][regeneration]") {
    Document doc("Part");
    auto sketch = std::make_unique<Sketch>("Sketch1");
    const auto lines = addRectangle(*sketch, 0_mm, 0_mm, 100_mm, 50_mm);
    const auto corner = std::get<LineEntity>(sketch->findEntity(lines[0])->geometry).start;
    require(sketch->addFixed(corner));
    require(sketch->addHorizontal(lines[0]));
    require(sketch->addHorizontal(lines[2]));
    require(sketch->addVertical(lines[1]));
    require(sketch->addVertical(lines[3]));
    const ConstraintId width = require(sketch->addDistance(lines[0], 100_mm));
    require(sketch->addDistance(lines[1], 50_mm));
    const auto sketchObject = doc.addObject(std::move(sketch));
    REQUIRE(sketchObject.has_value());
    auto feature = ExtrudeFeature::create(
        "Extrude1", {.profile = SketchId::fromValue(sketchObject->value()), .depth = 20_mm});
    const auto featureObject = doc.addObject(std::move(*feature));
    REQUIRE(featureObject.has_value());
    const auto extrude = FeatureId::fromValue(featureObject->value());

    REQUIRE(doc.modifyObject<Sketch>(*sketchObject, [&](Sketch& s) -> Result<bool> {
                   if (auto set = s.setConstraintValue(width, 120_mm); !set) {
                       return set;
                   }
                   const SolveResult result = solve(s);
                   if (!result.solved()) {
                       return makeError(ErrorCode::FailedPrecondition, result.message);
                   }
                   return true;
               }).value());
    CHECK_THAT(regeneratedVolumeMm3(doc, extrude), WithinRel(120.0 * 50.0 * 20.0, kRel));
}

TEST_CASE("Analytic volume regression for extruded rectangles", "[features][regeneration][regression]") {
    const auto [w, h, d] = GENERATE(table<double, double, double>({
        {100.0, 50.0, 20.0},
        {100.0, 50.0, 40.0},
        {1.0, 1.0, 1.0},
        {0.5, 250.0, 3.25},
        {1200.0, 800.0, 15.0},
    }));
    CAPTURE(w, h, d);

    Document doc("Part");
    auto sketch = std::make_unique<Sketch>("Sketch1");
    addRectangle(*sketch, 0_mm, 0_mm, w * units::mm, h * units::mm);
    const auto sketchObject = doc.addObject(std::move(sketch));
    REQUIRE(sketchObject.has_value());
    auto feature = ExtrudeFeature::create(
        "Extrude1", {.profile = SketchId::fromValue(sketchObject->value()), .depth = d * units::mm});
    REQUIRE(feature.has_value());
    const auto featureObject = doc.addObject(std::move(*feature));
    REQUIRE(featureObject.has_value());
    const auto extrude = FeatureId::fromValue(featureObject->value());

    const double first = regeneratedVolumeMm3(doc, extrude);
    const double second = regeneratedVolumeMm3(doc, extrude);
    CHECK_THAT(first, WithinRel(w * h * d, kRel));
    // Regeneration is deterministic: bit-identical on repetition.
    CHECK(std::bit_cast<std::uint64_t>(first) == std::bit_cast<std::uint64_t>(second));
}
