#pragma once

#include "features/FeatureTestSupport.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>

#include <format>
#include <memory>
#include <numbers>
#include <string>

namespace bettercad::test {

// A bracket that uses every kind of saved data:
//
//   width, height, hole_radius -> Base -> Pad <- depth
//   PocketSketch (plane on Pad's top face, facing down) -> Pocket (cut from Pad)
//   SlotSketch (tilted plane; arcs and lines) -> Slot (symmetric) <- slot_depth
//
// plus an angle parameter, a parameter expression, metadata, a construction
// arc, a disabled constraint, all nine constraint types, and deleted items
// that leave ID gaps at every level. The result bodies are Pocket and Slot.
struct BracketModel {
    Document doc{"Bracket"};
    ParameterId width, height, depth, holeRadius, slotDepth, draft;
    ObjectId base, pad, pocketSketch, pocket, slotSketch, slot;

    static constexpr double kPadVolume = (100.0 * 60.0 - std::numbers::pi * 8.0 * 8.0) * 20.0; // mm^3
    static constexpr double kPocketVolume = kPadVolume - 15.0 * 15.0 * 5.0;
    static constexpr double kSlotVolume = (20.0 * 10.0 + std::numbers::pi * 5.0 * 5.0) * 12.0;

    BracketModel() {
        using namespace bettercad::literals;
        using features::ExtrudeDirection;
        using features::FeatureOperation;
        width = doc.createParameter("width", 100_mm, units::mm).value();
        height = doc.createParameter("height", 60_mm, units::mm).value();
        depth = doc.createParameter("depth", 20_mm, units::mm).value();
        holeRadius = doc.createParameter("hole_radius", 8_mm, units::mm).value();
        slotDepth = doc.createParameter("slot_depth", 12_mm, units::mm).value();
        draft = doc.createParameter("draft", 1.5_deg, units::deg).value();
        REQUIRE(doc.setParameterExpression(slotDepth, "depth * 0.6").has_value());
        const ParameterId scratch = doc.createParameter("scratch", 1_mm, units::mm).value();
        REQUIRE(doc.removeParameter(scratch).has_value());
        REQUIRE(doc.setMetadata({.description = "P9 round-trip fixture",
                                 .author = "BetterCAD tests",
                                 .properties = {{"material", "6061-T6"}, {"note", "\xC2\xB5m \"quoted\"\ttab"}}})
                    .has_value());

        base = doc.addObject(makeBase()).value();
        pad = addExtrude("Pad", {.profile = sketchId(base), .depthParameter = depth});
        pocketSketch = doc.addObject(makePocketSketch()).value();
        pocket = addExtrude("Pocket", {.profile = sketchId(pocketSketch), .depth = 5_mm,
                                       .operation = FeatureOperation::Cut, .target = featureId(pad)});
        slotSketch = doc.addObject(makeSlotSketch()).value();
        slot = addExtrude("Slot", {.profile = sketchId(slotSketch), .depthParameter = slotDepth,
                                   .direction = ExtrudeDirection::Symmetric});
        const ObjectId scratchSketch = doc.addObject(std::make_unique<sketch::Sketch>("Scratch")).value();
        REQUIRE(doc.removeObject(scratchSketch).has_value());
    }

    static SketchId sketchId(ObjectId id) { return SketchId::fromValue(id.value()); }
    static FeatureId featureId(ObjectId id) { return FeatureId::fromValue(id.value()); }

    ObjectId addExtrude(const std::string& name, const features::ExtrudeDefinition& definition) {
        auto feature = features::ExtrudeFeature::create(name, definition);
        REQUIRE(feature.has_value());
        return doc.addObject(std::move(*feature)).value();
    }

    // 100 x 60 rectangle (driven by width and height) with a hole of radius
    // hole_radius at (50, 30).
    [[nodiscard]] std::unique_ptr<sketch::Sketch> makeBase() const {
        using namespace bettercad::literals;
        using namespace bettercad::sketch;
        auto sketch = std::make_unique<Sketch>("Base");
        const auto lines = addRectangle(*sketch, 0_mm, 0_mm, 90_mm, 40_mm);
        require(sketch->addFixed(std::get<LineEntity>(sketch->findEntity(lines[0])->geometry).start));
        require(sketch->addHorizontal(lines[0]));
        require(sketch->addHorizontal(lines[2]));
        require(sketch->addVertical(lines[1]));
        require(sketch->addPerpendicular(lines[0], lines[3]));
        const ConstraintId w = require(sketch->addDistance(lines[0], 1_mm));
        const ConstraintId h = require(sketch->addDistance(lines[1], 1_mm));
        REQUIRE(sketch->setConstraintParameter(w, width).has_value());
        REQUIRE(sketch->setConstraintParameter(h, height).has_value());

        const EntityId hole = require(sketch->addCircle(Point2D{50_mm, 30_mm}, 5_mm));
        require(sketch->addFixed(std::get<CircleEntity>(sketch->findEntity(hole)->geometry).center));
        const ConstraintId r = require(sketch->addRadius(hole, 5_mm));
        REQUIRE(sketch->setConstraintParameter(r, holeRadius).has_value());

        // Construction geometry is saved but is not part of the profile.
        const EntityId arc = require(sketch->addArc(Point2D{50_mm, 30_mm}, 20_mm, 0_deg, 90_deg));
        REQUIRE(sketch->setConstruction(arc, true).has_value());
        const ConstraintId arcRadius = require(sketch->addRadius(arc, 20_mm));
        REQUIRE(sketch->setConstraintEnabled(arcRadius, false).has_value());

        const EntityId stray = require(sketch->addPoint(Point2D{-5_mm, -5_mm}));
        const ConstraintId strayFixed = require(sketch->addFixed(stray));
        REQUIRE(sketch->removeConstraint(strayFixed).has_value());
        REQUIRE(sketch->removeEntity(stray).has_value());
        return sketch;
    }

    // Plane on the top face of Pad, facing down: local (u, v) is global
    // (u, -v, 20 mm). The 15 x 15 square covers x 10..25, y 10..25.
    [[nodiscard]] static std::unique_ptr<sketch::Sketch> makePocketSketch() {
        using namespace bettercad::literals;
        const auto plane =
            Frame3D::create(Point3D{0_mm, 0_mm, 20_mm}, Direction3D::unitZ().reversed(), Direction3D::unitX());
        REQUIRE(plane.has_value());
        auto sketch = std::make_unique<sketch::Sketch>("PocketSketch", *plane);
        const auto lines = addRectangle(*sketch, 10_mm, -(25_mm), 15_mm, 15_mm);
        require(sketch->addEqual(lines[0], lines[1]));
        return sketch;
    }

    // A slot on a tilted plane: two half circles of radius 5 joined by lines
    // 20 long, area 20 x 10 + pi 5^2. (An Equal constraint on the two arcs
    // would be redundant here: with parallel lines, both radii are already
    // half their distance to first order, and the solver reports that as
    // over-constrained.)
    [[nodiscard]] static std::unique_ptr<sketch::Sketch> makeSlotSketch() {
        using namespace bettercad::literals;
        using namespace bettercad::sketch;
        const auto normal = Direction3D::fromComponents(1.0, 2.0, 3.0);
        REQUIRE(normal.has_value());
        const auto plane = Frame3D::create(Point3D{200_mm, 0_mm, 0_mm}, *normal, Direction3D::unitZ());
        REQUIRE(plane.has_value());
        auto sketch = std::make_unique<Sketch>("SlotSketch", *plane);
        const EntityId left = require(sketch->addArc(Point2D{0_mm, 0_mm}, 5_mm, 90_deg, 270_deg));
        const EntityId right = require(sketch->addArc(Point2D{20_mm, 0_mm}, 5_mm, -(90_deg), 90_deg));
        const ArcEntity l = std::get<ArcEntity>(sketch->findEntity(left)->geometry);
        const ArcEntity r = std::get<ArcEntity>(sketch->findEntity(right)->geometry);
        const EntityId bottom = require(sketch->addLine(l.end, r.start));
        // The top line has its own end points, made coincident with the arc ends.
        const EntityId top = require(sketch->addLine(*sketch->position(r.end), *sketch->position(l.start)));
        const LineEntity t = std::get<LineEntity>(sketch->findEntity(top)->geometry);
        require(sketch->addCoincident(t.start, r.end));
        require(sketch->addCoincident(t.end, l.start));
        require(sketch->addHorizontal(bottom));
        require(sketch->addParallel(bottom, top));
        require(sketch->addRadius(left, 5_mm));
        require(sketch->addDistance(l.center, r.center, 20_mm));
        return sketch;
    }
};

/// Errors of a regeneration report, for failure messages.
inline std::string describe(const features::RegenerationReport& report) {
    std::string text;
    for (const auto& [id, error] : report.errors) {
        text += std::format("{}: {}\n", id, error.message);
    }
    for (const ObjectId id : report.blocked) {
        text += std::format("{}: blocked\n", id);
    }
    return text;
}

} // namespace bettercad::test
