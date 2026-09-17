#pragma once

#include "features/FeatureTestSupport.hpp"
#include "support/FaceKindModels.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/parameters/Parameter.hpp>
#include <bettercad/features/CircularPatternFeature.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/LinearPatternFeature.hpp>
#include <bettercad/features/MirrorFeature.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <memory>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>

namespace bettercad::test {

// P12-FEAT-001 reference models: cuts through all the material of their
// target. Every expected value is written out from the parameters.

/// A through-all cut of @p profile from @p target in @p direction.
inline features::ExtrudeDefinition throughAll(ObjectId profile, ObjectId target,
                                              features::ExtrudeDirection direction) {
    return {.profile = FaceKindModel::sketchOf(profile),
            .direction = direction,
            .operation = features::FeatureOperation::Cut,
            .target = FaceKindModel::featureOf(target),
            .termination = features::ExtrudeTermination::ThroughAll};
}

// A slab with holes cut through all of it, whatever its thickness:
//
//   thickness     20 mm
//   SlabSketch    XY plane: rectangle 100 x 60 at the origin
//   Slab          extrude by thickness
//   CutSketch     on Slab's end cap: a circle r 8 about (30, 30), and a
//                 rectangle x 60..90, y 20..40 (its lines: y = 20, x = 90,
//                 y = 40, x = 60)
//   Cut           through all, reversed (into the slab), cut from Slab
//   SlantSketch   fixed plane through (50, 30, 10) facing
//                 (0, -sin 30deg, cos 30deg), X along +X: a circle r 5
//                 about its origin
//   Slant         through all, symmetric (both ways), cut from Cut
//   WallSketch    on the side of Cut from the line x = 60 (the slot's
//                 wall, facing +X into the slot; sketch X = +Y, Y = +Z):
//                 a circle r 3 about (30, 10)
//   Peg           extrude 5 mm into the slot (new body)
//
// With t = thickness and a = 30 deg: the circle removes 64 pi t, the slot
// 600 t, and the slanted cylinder pi 25 t / cos a (each horizontal section
// is an ellipse of area pi r^2 / cos a, centred on the axis, which crosses
// z at y = 30 - (z - 10) tan a). The slanted cylinder stays clear of the
// other cuts (x 45..55) and of the slab's sides for t up to 30.
// IDs: thickness 1, SlabSketch 2, Slab 3, CutSketch 4, Cut 5, SlantSketch 6,
// Slant 7, WallSketch 8, Peg 9.
struct ThroughSlabModel : FaceKindModel {
    static constexpr std::string_view kDocumentId = "c41e7a92-3d5b-4f8a-9e26-7b1d4c8a5f03";
    static constexpr double kSlant = std::numbers::pi / 6.0;
    ParameterId thickness;
    ObjectId slabSketch, slab, cutSketch, cut, slantSketch, slant, wallSketch, peg;
    EntityId slotBottom, slotRight, slotTop, slotLeft, hole;

    static Frame3D slantPlane() {
        using namespace bettercad::literals;
        const auto normal = Direction3D::fromComponents(0.0, -std::sin(kSlant), std::cos(kSlant)).value();
        return Frame3D::create(Point3D{50_mm, 30_mm, 10_mm}, normal, Direction3D::unitX()).value();
    }

    ThroughSlabModel() : FaceKindModel(kDocumentId, "ThroughSlab") {
        using namespace bettercad::literals;
        using namespace bettercad::features;
        thickness = doc.createParameter("thickness", 20_mm, units::mm).value();
        slabSketch = addFixedRectangle("SlabSketch", 0.0, 0.0, 100.0, 60.0);
        slab = add(ExtrudeFeature::create("Slab", {.profile = sketchOf(slabSketch), .depthParameter = thickness}));

        auto profile = std::make_unique<sketch::Sketch>("CutSketch");
        hole = require(profile->addCircle(Point2D{30_mm, 30_mm}, 8_mm));
        require(profile->addFixed(std::get<sketch::CircleEntity>(profile->findEntity(hole)->geometry).center));
        require(profile->addRadius(hole, 8_mm));
        const auto lines = addRectangle(*profile, 60_mm, 20_mm, 30_mm, 20_mm);
        for (const EntityId line : lines) {
            require(profile->addFixed(std::get<sketch::LineEntity>(profile->findEntity(line)->geometry).start));
        }
        slotBottom = lines[0];
        slotRight = lines[1];
        slotTop = lines[2];
        slotLeft = lines[3];
        REQUIRE(profile->setAttachment(faceOf(slab, {.role = FaceRole::EndCap})).has_value());
        cutSketch = doc.addObject(std::move(profile)).value();
        cut = add(ExtrudeFeature::create("Cut", throughAll(cutSketch, slab, ExtrudeDirection::Reversed)));

        auto tilted = std::make_unique<sketch::Sketch>("SlantSketch", slantPlane());
        const EntityId disc = require(tilted->addCircle(Point2D{}, 5_mm));
        require(tilted->addFixed(std::get<sketch::CircleEntity>(tilted->findEntity(disc)->geometry).center));
        require(tilted->addRadius(disc, 5_mm));
        slantSketch = doc.addObject(std::move(tilted)).value();
        slant = add(ExtrudeFeature::create("Slant", throughAll(slantSketch, cut, ExtrudeDirection::Symmetric)));

        wallSketch = addCircleSketch("WallSketch", faceOf(cut, {.role = FaceRole::Side, .entity = slotLeft}), 30.0,
                                     10.0, 3.0);
        peg = addBoss("Peg", wallSketch, 5.0);
    }

    static double holeVolume(double t) { return 64.0 * std::numbers::pi * t; }
    static double slotVolume(double t) { return 600.0 * t; }
    static double slantVolume(double t) { return 25.0 * std::numbers::pi * t / std::cos(kSlant); }

    /// Slant's body.
    static double volume(double t) { return 6000.0 * t - holeVolume(t) - slotVolume(t) - slantVolume(t); }

    static Vec3 centre(double t) {
        return weightedCentre({
            {6000.0 * t, {50.0, 30.0, t / 2.0}},
            {-holeVolume(t), {30.0, 30.0, t / 2.0}},
            {-slotVolume(t), {75.0, 30.0, t / 2.0}},
            {-slantVolume(t), {50.0, 30.0 - (t / 2.0 - 10.0) * std::tan(kSlant), t / 2.0}},
        });
    }

    /// Cut's body: the slab less the circle and the slot.
    static double cutVolume(double t) { return 6000.0 * t - holeVolume(t) - slotVolume(t); }

    [[nodiscard]] ExpectedBoss pegBoss() const {
        return {wallSketch, peg, {{60, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 0}},
                {{60.0, 30.0, 10.0}, {1, 0, 0}, 3.0, 5.0}};
    }
};

// A perforated plate: one hole cut through all, repeated along X.
//
//   thickness 10 mm, holes 5
//   PlateSketch   XY plane: rectangle 100 x 60 at the origin
//   Plate         extrude by thickness
//   HoleSketch    on Plate's end cap: circle r 4 about (10, 15)
//   Perf          through all, reversed, cut from Plate
//   Row           linear pattern of Perf along +X: holes instances, 20 mm
//                 apart
//
// V = 6000 t - holes 16 pi t. IDs: thickness 1, holes 2, PlateSketch 3,
// Plate 4, HoleSketch 5, Perf 6, Row 7.
struct PerforatedPlateModel : FaceKindModel {
    static constexpr std::string_view kDocumentId = "58b3d1e6-0c27-4a94-b1f8-2e6d9a7c3b15";
    ParameterId thickness, holes;
    ObjectId plateSketch, plate, holeSketch, perf, row;

    PerforatedPlateModel() : FaceKindModel(kDocumentId, "PerforatedPlate") {
        using namespace bettercad::literals;
        using namespace bettercad::features;
        thickness = doc.createParameter("thickness", 10_mm, units::mm).value();
        holes = doc.createParameter("holes", 5.0, kUnitless).value();
        plateSketch = addFixedRectangle("PlateSketch", 0.0, 0.0, 100.0, 60.0);
        plate = add(ExtrudeFeature::create("Plate", {.profile = sketchOf(plateSketch), .depthParameter = thickness}));
        holeSketch = addCircleSketch("HoleSketch", faceOf(plate, {.role = FaceRole::EndCap}), 10.0, 15.0, 4.0);
        perf = add(ExtrudeFeature::create("Perf", throughAll(holeSketch, plate, ExtrudeDirection::Reversed)));
        row = add(LinearPatternFeature::create(
            "Row", {.source = featureOf(perf),
                    .first = {.direction = {1.0, 0.0, 0.0}, .countParameter = holes, .spacing = 20_mm}}));
    }

    static double volume(double t, double count) { return 6000.0 * t - count * 16.0 * std::numbers::pi * t; }

    /// The holes at x = 10, 30, ... (count of them), all at y = 15.
    static Vec3 centre(double t, double count) {
        std::vector<std::pair<double, Vec3>> parts{{6000.0 * t, {50.0, 30.0, t / 2.0}}};
        for (int k = 0; k < static_cast<int>(count); ++k) {
            parts.push_back({-16.0 * std::numbers::pi * t, {10.0 + 20.0 * k, 15.0, t / 2.0}});
        }
        return weightedCentre(parts);
    }
};

// A rectangular hollow section along +Y with a radial hole cut through all
// of one wall, and copies of that hole on the other walls:
//
//   SectionSketch XZ plane (sketch X = +X, Y = +Z, facing -Y): outer
//                 rectangle x -30..30, z -15..15; inner x -20..20, z -10..10
//   Section       extrude 60 mm, reversed (towards +Y): y 0..60
//   PortSketch    XY plane (z = 0, facing +Z): circle r 3 about (0, 30)
//   Port          through all along +Z, cut from Section: through the top
//                 wall only (z 10..15), 45 pi
//
// The walls are 5 mm thick at the top and bottom and 10 mm at the sides, so
// a copy turned to face +X must reach x = 30 (the original reaches z = 15):
// its tool is measured along the moved normal. A side-wall hole removes
// 90 pi. V = (60 x 30 - 40 x 20) x 60 = 60000 less the holes. IDs:
// SectionSketch 1, Section 2, PortSketch 3, Port 4.
struct HollowSectionModel : FaceKindModel {
    static constexpr std::string_view kDocumentId = "a7d52c08-6e3f-4b19-8c74-19f0e5b2d6a4";
    ObjectId sectionSketch, section, portSketch, port;
    EntityId portCircle;

    HollowSectionModel() : FaceKindModel(kDocumentId, "HollowSection") {
        using namespace bettercad::literals;
        using namespace bettercad::features;
        auto profile = fixedPolygon("SectionSketch", Frame3D::xz(), {{-30.0, -15.0}, {30.0, -15.0}, {30.0, 15.0},
                                                                     {-30.0, 15.0}});
        std::vector<EntityId> inner;
        for (const auto& [u, v] : std::vector<std::pair<double, double>>{{-20.0, -10.0}, {20.0, -10.0},
                                                                         {20.0, 10.0}, {-20.0, 10.0}}) {
            inner.push_back(require(profile->addPoint(Point2D{u * units::mm, v * units::mm})));
            require(profile->addFixed(inner.back()));
        }
        for (std::size_t i = 0; i < inner.size(); ++i) {
            require(profile->addLine(inner[i], inner[(i + 1) % inner.size()]));
        }
        sectionSketch = doc.addObject(std::move(profile)).value();
        section = add(ExtrudeFeature::create("Section", {.profile = sketchOf(sectionSketch),
                                                         .depth = 60_mm,
                                                         .direction = ExtrudeDirection::Reversed}));
        auto disc = std::make_unique<sketch::Sketch>("PortSketch");
        portCircle = require(disc->addCircle(Point2D{0_mm, 30_mm}, 3_mm));
        require(disc->addFixed(std::get<sketch::CircleEntity>(disc->findEntity(portCircle)->geometry).center));
        require(disc->addRadius(portCircle, 3_mm));
        portSketch = doc.addObject(std::move(disc)).value();
        port = add(ExtrudeFeature::create("Port", throughAll(portSketch, section, ExtrudeDirection::Normal)));
    }

    static constexpr double kSectionVolume = 60000.0;
    static double thinHole() { return 45.0 * std::numbers::pi; }
    static double thickHole() { return 90.0 * std::numbers::pi; }
};

} // namespace bettercad::test
