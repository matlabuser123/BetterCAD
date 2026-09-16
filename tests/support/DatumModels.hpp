#pragma once

#include "features/FeatureTestSupport.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/features/CircularPatternFeature.hpp>
#include <bettercad/features/Datums.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <memory>
#include <numbers>
#include <string>
#include <utility>

namespace bettercad::test {

// Reference geometry driving a small part (P12-DATUM-001):
//
//   height                 -> Block (XY rectangle 100 x 60 extruded height)
//   height                 -> TopPlane: the model's XY plane moved up by height
//   boss_radius, TopPlane  -> BossSketch (on TopPlane: circle at (30, 30))
//                          -> Boss: joined to Block, 10 mm high
//   half_length            -> Middle: the model's YZ plane moved by half_length (x = half_length)
//                          -> RearPlane: the model's XZ plane (normal -Y) moved by -30 mm (y = 30)
//   Middle, RearPlane      -> Spindle: where they meet, the vertical line x = half_length, y = 30
//   Boss, Spindle          -> Pattern: three bosses around Spindle
//                          -> Station: a coordinate system at (150, 0, 0) turned 90 deg about Z
//   Station                -> PegSketch (on Station's XY: circle at local (10, 0), r 5) -> Peg, 10 mm
//   tilt                   -> TiltPlane: the model's XY plane turned by tilt about the model's Y axis
//   TiltPlane              -> FinSketch (rectangle u -60..-40, v 0..20) -> Fin, 5 mm
//
// IDs: height 1, half_length 2, tilt 3, boss_radius 4, BlockSketch 5, Block 6,
// TopPlane 7, BossSketch 8, Boss 9, Middle 10, RearPlane 11, Spindle 12,
// Pattern 13, Station 14, PegSketch 15, Peg 16, TiltPlane 17, FinSketch 18,
// Fin 19.
struct DatumBlockModel {
    static constexpr std::string_view kDocumentId = "0b6e3c52-7d1f-4a8e-9c3d-5f2a8b1e6d40";
    Document doc{DocumentId::fromValue(*Uuid::parse(kDocumentId)), "DatumBlock"};
    ParameterId height, halfLength, tilt, bossRadius;
    ObjectId blockSketch, block, topPlane, bossSketch, boss, middle, rearPlane, spindle, pattern, station, pegSketch,
        peg, tiltPlane, finSketch, fin;

    DatumBlockModel() {
        using namespace bettercad::literals;
        using namespace bettercad::features;
        height = doc.createParameter("height", 20_mm, units::mm).value();
        halfLength = doc.createParameter("half_length", 50_mm, units::mm).value();
        tilt = doc.createParameter("tilt", 30_deg, units::deg).value();
        bossRadius = doc.createParameter("boss_radius", 8_mm, units::mm).value();

        blockSketch = doc.addObject(rectangleSketch("BlockSketch", 0.0, 0.0, 100.0, 60.0)).value();
        block = add(ExtrudeFeature::create("Block", {.profile = sketchOf(blockSketch), .depthParameter = height}));

        topPlane = add(DatumPlane::create(
            "TopPlane", {.kind = DatumPlaneKind::Offset, .base = {}, .offsetParameter = height}));
        auto bossProfile = std::make_unique<sketch::Sketch>("BossSketch");
        const EntityId circle = require(bossProfile->addCircle(Point2D{30_mm, 30_mm}, 1_mm));
        require(bossProfile->addFixed(std::get<sketch::CircleEntity>(bossProfile->findEntity(circle)->geometry).center));
        REQUIRE(bossProfile->setConstraintParameter(require(bossProfile->addRadius(circle, 1_mm)), bossRadius)
                    .has_value());
        REQUIRE(bossProfile->setAttachment(PlaneReference{topPlane}).has_value());
        bossSketch = doc.addObject(std::move(bossProfile)).value();
        boss = add(ExtrudeFeature::create("Boss", {.profile = sketchOf(bossSketch),
                                                   .depth = 10_mm,
                                                   .operation = FeatureOperation::Join,
                                                   .target = featureOf(block)}));

        middle = add(DatumPlane::create("Middle", {.kind = DatumPlaneKind::Offset,
                                                   .base = {.object = {}, .plane = PrincipalPlane::YZ},
                                                   .offsetParameter = halfLength}));
        rearPlane = add(DatumPlane::create("RearPlane", {.kind = DatumPlaneKind::Offset,
                                                         .base = {.object = {}, .plane = PrincipalPlane::XZ},
                                                         .offset = -30_mm}));
        spindle = add(DatumAxis::create("Spindle", {.kind = DatumAxisKind::Intersection,
                                                    .first = {.object = middle},
                                                    .second = {.object = rearPlane}}));
        pattern = add(CircularPatternFeature::create(
            "Pattern", {.source = featureOf(boss), .axis = {.reference = AxisReference{spindle}}, .count = 3}));

        station = add(CoordinateSystem::create(
            "Station", {.kind = CoordinateSystemKind::Offset,
                        .translation = {150_mm, 0_mm, 0_mm},
                        .rotation = {0_deg, 0_deg, 90_deg}}));
        auto pegProfile = std::make_unique<sketch::Sketch>("PegSketch");
        const EntityId pegCircle = require(pegProfile->addCircle(Point2D{10_mm, 0_mm}, 5_mm));
        require(pegProfile->addFixed(std::get<sketch::CircleEntity>(pegProfile->findEntity(pegCircle)->geometry).center));
        require(pegProfile->addRadius(pegCircle, 5_mm));
        REQUIRE(pegProfile->setAttachment(PlaneReference{station}).has_value());
        pegSketch = doc.addObject(std::move(pegProfile)).value();
        peg = add(ExtrudeFeature::create("Peg", {.profile = sketchOf(pegSketch), .depth = 10_mm}));

        tiltPlane = add(DatumPlane::create("TiltPlane", {.kind = DatumPlaneKind::Angled,
                                                         .base = {},
                                                         .axis = {.object = {}, .axis = PrincipalAxis::Y},
                                                         .angleParameter = tilt}));
        auto finProfile = rectangleSketch("FinSketch", -60.0, 0.0, 20.0, 20.0);
        REQUIRE(finProfile->setAttachment(PlaneReference{tiltPlane}).has_value());
        finSketch = doc.addObject(std::move(finProfile)).value();
        fin = add(ExtrudeFeature::create("Fin", {.profile = sketchOf(finSketch), .depth = 5_mm}));
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

    static SketchId sketchOf(ObjectId id) { return SketchId::fromValue(id.value()); }
    static FeatureId featureOf(ObjectId id) { return FeatureId::fromValue(id.value()); }

    template <typename T>
    ObjectId add(Result<std::unique_ptr<T>> object) {
        if (!object) {
            FAIL(object.error().message);
        }
        return doc.addObject(std::move(*object)).value();
    }

    // --- Expected geometry, written out from the definitions -----------------------------------

    /// The pattern body: the block and three bosses.
    static double patternVolume(double heightMm, double radiusMm) {
        return 100.0 * 60.0 * heightMm + 3.0 * std::numbers::pi * radiusMm * radiusMm * 10.0;
    }

    /// Its centre of mass: the block's at (50, 30, h/2); the bosses' at
    /// height h + 5, spread evenly around the spindle (half_length, 30), so
    /// together at the spindle's point.
    static std::array<double, 3> patternCentre(double heightMm, double radiusMm, double halfLengthMm) {
        const double block = 100.0 * 60.0 * heightMm;
        const double bosses = 3.0 * std::numbers::pi * radiusMm * radiusMm * 10.0;
        const double total = block + bosses;
        return {(block * 50.0 + bosses * halfLengthMm) / total, (block * 30.0 + bosses * 30.0) / total,
                (block * heightMm / 2.0 + bosses * (heightMm + 5.0)) / total};
    }

    /// The fin's bounds: the corners (u, v, w) of the extruded rectangle,
    /// u in [-60, -40], v in [0, 20], w in [0, 5], mapped by the plane turned
    /// by tilt about +Y: x = u cos t + w sin t, y = v, z = -u sin t + w cos t.
    static std::array<double, 6> finBounds(double tiltDeg) {
        const double t = tiltDeg * std::numbers::pi / 180.0;
        std::array<double, 6> box{1e300, 1e300, 1e300, -1e300, -1e300, -1e300};
        for (const double u : {-60.0, -40.0}) {
            for (const double v : {0.0, 20.0}) {
                for (const double w : {0.0, 5.0}) {
                    const std::array<double, 3> p{u * std::cos(t) + w * std::sin(t), v,
                                                  -u * std::sin(t) + w * std::cos(t)};
                    for (std::size_t i = 0; i < 3; ++i) {
                        box[i] = std::min(box[i], p[i]);
                        box[i + 3] = std::max(box[i + 3], p[i]);
                    }
                }
            }
        }
        return box;
    }
};

} // namespace bettercad::test
