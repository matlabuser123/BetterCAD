#pragma once

#include "features/FeatureTestSupport.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <memory>
#include <numbers>
#include <string>
#include <utility>

namespace bettercad::test {

// Sketches on faces of features (P12-STREF-001):
//
//   plate                    literal, 10 mm
//   height = 2 * plate       driven (20 mm)
//   StepSketch               rectangle x 100..150, y 0..60 on the model's XY
//   Step                     extrude 35 mm (new body)
//   BaseSketch               rectangle x 0..100, y 0..60 on the model's XY
//   Base                     extrude of BaseSketch by height, joined to
//                            Step: Base's body holds Step's faces too
//   BossSketch               on Base's end cap: a circle r 8 at (30, 30)
//   Boss                     extrude 10 mm, joined to Base
//   SideSketch               on the side Base's front line (y = 0) sweeps:
//                            a rectangle x 10..30, y 5..15 in that face's
//                            frame (x along the model's X, y along its Z)
//   Pocket                   extrude 5 mm reversed (into the block), cut
//                            from Boss
//
// Base's top face moves with height; Step's top, in the same body, stays at
// z = 35. At plate = 20 (height 40) the old top plane z = 20 has no face,
// and the nearest parallel face is Step's: the boss must follow Base's top
// to 40. At plate = 17.5 the two tops merge into one face.
//
// IDs: plate 1, height 2, StepSketch 3, Step 4, BaseSketch 5, Base 6,
// BossSketch 7, Boss 8, SideSketch 9, Pocket 10.
struct FaceBlockModel {
    static constexpr std::string_view kDocumentId = "4a1f7c2e-9b3d-4e6a-8c5f-2d7e1b9a3c60";
    Document doc{DocumentId::fromValue(*Uuid::parse(kDocumentId)), "FaceBlock"};
    ParameterId plate, height;
    ObjectId baseSketch, base, stepSketch, step, bossSketch, boss, sideSketch, pocket;
    /// Base's front line (y = 0) in BaseSketch.
    EntityId frontLine;

    FaceBlockModel() {
        using namespace bettercad::literals;
        using namespace bettercad::features;
        plate = doc.createParameter("plate", 10_mm, units::mm).value();
        height = doc.createParameter("height", 20_mm, units::mm).value();
        REQUIRE(doc.setParameterExpression(height, "2 * plate").has_value());

        stepSketch = doc.addObject(rectangleSketch("StepSketch", 100.0, 0.0, 50.0, 60.0)).value();
        step = add(ExtrudeFeature::create("Step", {.profile = sketchOf(stepSketch), .depth = 35_mm}));

        auto baseProfile = rectangleSketch("BaseSketch", 0.0, 0.0, 100.0, 60.0);
        frontLine = lineAlong(*baseProfile, 0.0);
        baseSketch = doc.addObject(std::move(baseProfile)).value();
        base = add(ExtrudeFeature::create("Base", {.profile = sketchOf(baseSketch),
                                                   .depthParameter = height,
                                                   .operation = FeatureOperation::Join,
                                                   .target = featureOf(step)}));

        auto bossProfile = std::make_unique<sketch::Sketch>("BossSketch");
        const EntityId circle = require(bossProfile->addCircle(Point2D{30_mm, 30_mm}, 8_mm));
        require(bossProfile->addFixed(std::get<sketch::CircleEntity>(bossProfile->findEntity(circle)->geometry).center));
        require(bossProfile->addRadius(circle, 8_mm));
        REQUIRE(bossProfile->setAttachment(faceOf(base, FaceRole::EndCap)).has_value());
        bossSketch = doc.addObject(std::move(bossProfile)).value();
        boss = add(ExtrudeFeature::create("Boss", {.profile = sketchOf(bossSketch),
                                                   .depth = 10_mm,
                                                   .operation = FeatureOperation::Join,
                                                   .target = featureOf(base)}));

        auto sideProfile = rectangleSketch("SideSketch", 10.0, 5.0, 20.0, 10.0);
        REQUIRE(sideProfile->setAttachment(faceOf(base, FaceRole::Side, frontLine)).has_value());
        sideSketch = doc.addObject(std::move(sideProfile)).value();
        pocket = add(ExtrudeFeature::create("Pocket", {.profile = sketchOf(sideSketch),
                                                       .depth = 5_mm,
                                                       .direction = ExtrudeDirection::Reversed,
                                                       .operation = FeatureOperation::Cut,
                                                       .target = featureOf(boss)}));
    }

    static PlaneReference faceOf(ObjectId feature, FaceRole role, std::optional<EntityId> entity = std::nullopt) {
        return PlaneReference{.object = feature, .face = FaceSelector{role, entity}};
    }

    /// A fully constrained w x h rectangle with its corner at (x, y) mm.
    static std::unique_ptr<sketch::Sketch> rectangleSketch(const std::string& name, double x, double y, double w,
                                                           double h) {
        using namespace bettercad::literals;
        auto s = std::make_unique<sketch::Sketch>(name);
        const auto lines = addRectangle(*s, x * units::mm, y * units::mm, w * units::mm, h * units::mm);
        for (const EntityId line : lines) {
            require(s->addFixed(std::get<sketch::LineEntity>(s->findEntity(line)->geometry).start));
        }
        return s;
    }

    /// The line of @p sketch whose ends both lie at y = @p y mm.
    static EntityId lineAlong(const sketch::Sketch& sketch, double y) {
        for (const sketch::Entity& entity : sketch.entities()) {
            if (entity.type() != sketch::EntityType::Line) {
                continue;
            }
            const auto ends = sketch.endpoints(entity.id);
            REQUIRE(ends.has_value());
            if (ends->start.y.in(units::mm) == y && ends->end.y.in(units::mm) == y) {
                return entity.id;
            }
        }
        FAIL("no line along y = " << y);
        return {};
    }

    static SketchId sketchOf(ObjectId id) { return SketchId::fromValue(id.value()); }
    static FeatureId featureOf(ObjectId id) { return FeatureId::fromValue(id.value()); }

    template <typename T>
    ObjectId add(Result<std::unique_ptr<T>> object) {
        if (!object) {
            FAIL(object.error().message);
        }
        return doc.addObject(std::move(*object)).value();
    }

    // --- Expected geometry, written out from the definitions ---------------------------------

    /// The pocketed body's volume: Base, Step and Boss minus the pocket
    /// (20 x 10 x 5 mm, inside Base while h >= 15).
    static double volume(double heightMm) {
        return 100.0 * 60.0 * heightMm + 50.0 * 60.0 * 35.0 + std::numbers::pi * 64.0 * 10.0 - 1000.0;
    }

    /// Its centre of mass: Base at (50, 30, h/2), Step at (125, 30, 17.5),
    /// Boss at (30, 30, h + 5), less the pocket at (20, 2.5, 10).
    static std::array<double, 3> centre(double heightMm) {
        const std::array<std::pair<double, std::array<double, 3>>, 4> parts{{
            {100.0 * 60.0 * heightMm, {50.0, 30.0, heightMm / 2.0}},
            {50.0 * 60.0 * 35.0, {125.0, 30.0, 17.5}},
            {std::numbers::pi * 64.0 * 10.0, {30.0, 30.0, heightMm + 5.0}},
            {-1000.0, {20.0, 2.5, 10.0}},
        }};
        double mass = 0.0;
        std::array<double, 3> moment{};
        for (const auto& [m, c] : parts) {
            mass += m;
            for (std::size_t i = 0; i < 3; ++i) {
                moment[i] += m * c[i];
            }
        }
        return {moment[0] / mass, moment[1] / mass, moment[2] / mass};
    }

    /// Its top: the boss's (h + 10) or the step's (35).
    static double top(double heightMm) { return std::max(heightMm + 10.0, 35.0); }
};

} // namespace bettercad::test
