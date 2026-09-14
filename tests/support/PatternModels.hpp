#pragma once

#include "features/FeatureTestSupport.hpp"
#include "support/BlockModel.hpp"
#include "support/HoleModels.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/parameters/Parameter.hpp>
#include <bettercad/features/CircularPatternFeature.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/LinearPatternFeature.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <numbers>
#include <string>

namespace bettercad::test {

/// A count for a dimensionless count parameter.
inline Quantity<dimensions::dimensionless> countOf(double value) {
    return Quantity<dimensions::dimensionless>::fromSi(value);
}

// A row of cubes: a pattern of a new-body extrude.
//
//   size -> CubeSketch (XY: square with a corner fixed at the origin)
//        -> Cube (extrude, new body, depth size)
//   Cube -> Row (linear pattern along +X) <- count (dimensionless), pitch
//
// The cube spans [0, 10]^3 mm; Row has 4 cubes 20 mm apart, at x = 0, 20,
// 40 and 60 mm, and is the only result body. IDs: size 1, count 2, pitch 3,
// CubeSketch 4, Cube 5, Row 6.
struct CubeRowModel {
    Document doc{"Cubes"};
    ParameterId size, count, pitch;
    ObjectId sketch, cube, row;

    static FeatureId featureId(ObjectId id) { return FeatureId::fromValue(id.value()); }

    CubeRowModel() {
        using namespace bettercad::literals;
        using namespace bettercad::sketch;
        size = doc.createParameter("size", 10_mm, units::mm).value();
        count = doc.createParameter("count", 4.0, kUnitless).value();
        pitch = doc.createParameter("pitch", 20_mm, units::mm).value();

        auto square = std::make_unique<Sketch>("CubeSketch");
        const auto lines = addRectangle(*square, 0_mm, 0_mm, 5_mm, 5_mm);
        require(square->addFixed(std::get<LineEntity>(square->findEntity(lines[0])->geometry).start));
        require(square->addHorizontal(lines[0]));
        require(square->addHorizontal(lines[2]));
        require(square->addVertical(lines[1]));
        require(square->addVertical(lines[3]));
        const ConstraintId w = require(square->addDistance(lines[0], 1_mm));
        const ConstraintId h = require(square->addDistance(lines[1], 1_mm));
        REQUIRE(square->setConstraintParameter(w, size).has_value());
        REQUIRE(square->setConstraintParameter(h, size).has_value());
        sketch = doc.addObject(std::move(square)).value();

        auto extrude = features::ExtrudeFeature::create(
            "Cube", {.profile = SketchId::fromValue(sketch.value()), .depthParameter = size});
        REQUIRE(extrude.has_value());
        cube = doc.addObject(std::move(*extrude)).value();
        row = addPattern("Row", {.source = featureId(cube),
                                 .first = {.direction = {1.0, 0.0, 0.0},
                                           .count = 4,
                                           .countParameter = count,
                                           .spacing = 20_mm,
                                           .spacingParameter = pitch}});
    }

    ObjectId addPattern(const std::string& name, const features::LinearPatternDefinition& definition) {
        auto feature = features::LinearPatternFeature::create(name, definition);
        REQUIRE(feature.has_value());
        return doc.addObject(std::move(*feature)).value();
    }

    [[nodiscard]] features::LinearPatternDefinition definitionOf(ObjectId pattern) const {
        return doc.findObjectAs<features::LinearPatternFeature>(pattern)->definition();
    }

    void setDefinition(ObjectId pattern, const features::LinearPatternDefinition& definition) {
        REQUIRE(doc.modifyObject<features::LinearPatternFeature>(pattern, [&](features::LinearPatternFeature& f) {
                       return f.setDefinition(definition);
                   }).has_value());
    }
};

// A row of holes (see BlockModel), 120 x 50 x 20 mm:
//
//   Pad -> Drill (10 mm through hole in the top face at (20, 25)) <- diameter
//   Drill -> Holes (linear pattern along +X) <- count (dimensionless), pitch
//
// Holes has 5 holes 20 mm apart, at x = 20, 40, 60, 80 and 100 mm; the last
// is 15 mm from the far end. It is the only result body. IDs: parameters
// 1-4, Base 5, Pad 6, count 7, pitch 8, Drill 9, Holes 10.
struct HoleRowModel : BlockModel {
    ParameterId diameter, count, pitch;
    ObjectId drill, holes;

    /// In mm^3: V = L W H - n pi (d/2)^2 H.
    static double expectedVolume(double lengthMm, double heightMm, double diameterMm, double holes) {
        return lengthMm * 50.0 * heightMm - holes * std::numbers::pi * diameterMm * diameterMm / 4.0 * heightMm;
    }

    HoleRowModel() : BlockModel("diameter", Length::fromSi(0.01)), diameter(extra) {
        using namespace bettercad::literals;
        REQUIRE(doc.setParameterValue(width, 120_mm).has_value());
        count = doc.createParameter("count", 5.0, kUnitless).value();
        pitch = doc.createParameter("pitch", 20_mm, units::mm).value();
        drill = add<features::HoleFeature>("Drill", {.target = featureId(pad),
                                                     .face = topFace(),
                                                     .center = Point2D{20_mm, 25_mm},
                                                     .diameter = 10_mm,
                                                     .diameterParameter = diameter});
        holes = add<features::LinearPatternFeature>("Holes", {.source = featureId(drill),
                                                              .first = {.direction = {1.0, 0.0, 0.0},
                                                                        .count = 5,
                                                                        .countParameter = count,
                                                                        .spacing = 20_mm,
                                                                        .spacingParameter = pitch}});
    }

    [[nodiscard]] features::LinearPatternDefinition patternOf(ObjectId pattern) const {
        return definitionOf<features::LinearPatternFeature>(pattern);
    }
    void setPattern(ObjectId pattern, const features::LinearPatternDefinition& definition) {
        setDefinition<features::LinearPatternFeature>(pattern, definition);
    }
    [[nodiscard]] features::HoleDefinition holeOf(ObjectId hole) const {
        return definitionOf<features::HoleFeature>(hole);
    }
    void setHole(ObjectId hole, const features::HoleDefinition& definition) {
        setDefinition<features::HoleFeature>(hole, definition);
    }
};

// A row of bosses on the block (see BlockModel), 100 x 50 x 20 mm:
//
//   BossSketch (plane z = 20 mm: 10 x 10 mm square at (5, 20))
//     -> Boss (extrude 5 mm, join with Pad)
//   Boss -> Bosses (linear pattern along +X) <- pitch
//
// Bosses has 4 bosses 20 mm apart, at x = 5, 25, 45 and 65 mm, each
// 10 x 10 x 5 mm. It is the only result body. IDs: parameters 1-4 (the
// fourth is pitch), Base 5, Pad 6, BossSketch 7, Boss 8, Bosses 9.
struct BossRowModel : BlockModel {
    ParameterId pitch;
    ObjectId bossSketch, boss, bosses;

    BossRowModel() : BlockModel("pitch", Length::fromSi(0.02)), pitch(extra) {
        using namespace bettercad::literals;
        const auto plane = Frame3D::create(Point3D{0_mm, 0_mm, 20_mm}, Direction3D::unitZ(), Direction3D::unitX());
        REQUIRE(plane.has_value());
        auto square = std::make_unique<sketch::Sketch>("BossSketch", *plane);
        addRectangle(*square, 5_mm, 20_mm, 10_mm, 10_mm);
        bossSketch = doc.addObject(std::move(square)).value();
        boss = add<features::ExtrudeFeature>("Boss", {.profile = sketchId(bossSketch),
                                                      .depth = 5_mm,
                                                      .operation = features::FeatureOperation::Join,
                                                      .target = featureId(pad)});
        bosses = add<features::LinearPatternFeature>(
            "Bosses", {.source = featureId(boss),
                       .first = {.direction = {1.0, 0.0, 0.0}, .count = 4, .spacing = 20_mm, .spacingParameter = pitch}});
    }
};

// A ring of cubes: a circular pattern of a new-body extrude.
//
//   size -> CubeSketch (XY: square with its corner fixed at (45, -5))
//        -> Cube (extrude, new body, depth size)
//   Cube -> Ring (circular pattern about the Z axis, full circle) <- count
//
// The cube spans [45, 55] x [-5, 5] x [0, 10] mm (centre (50, 0, 5)); Ring
// has 4 cubes, at 0, 90, 180 and 270 degrees. IDs: size 1, count 2,
// CubeSketch 3, Cube 4, Ring 5.
struct CubeRingModel {
    Document doc{"Ring"};
    ParameterId size, count;
    ObjectId sketch, cube, ring;

    static FeatureId featureId(ObjectId id) { return FeatureId::fromValue(id.value()); }

    CubeRingModel() {
        using namespace bettercad::literals;
        using namespace bettercad::sketch;
        size = doc.createParameter("size", 10_mm, units::mm).value();
        count = doc.createParameter("count", 4.0, kUnitless).value();

        auto square = std::make_unique<Sketch>("CubeSketch");
        const auto lines = addRectangle(*square, 45_mm, -(5_mm), 5_mm, 5_mm);
        require(square->addFixed(std::get<LineEntity>(square->findEntity(lines[0])->geometry).start));
        require(square->addHorizontal(lines[0]));
        require(square->addHorizontal(lines[2]));
        require(square->addVertical(lines[1]));
        require(square->addVertical(lines[3]));
        const ConstraintId w = require(square->addDistance(lines[0], 1_mm));
        const ConstraintId h = require(square->addDistance(lines[1], 1_mm));
        REQUIRE(square->setConstraintParameter(w, size).has_value());
        REQUIRE(square->setConstraintParameter(h, size).has_value());
        sketch = doc.addObject(std::move(square)).value();

        auto extrude = features::ExtrudeFeature::create(
            "Cube", {.profile = SketchId::fromValue(sketch.value()), .depthParameter = size});
        REQUIRE(extrude.has_value());
        cube = doc.addObject(std::move(*extrude)).value();
        ring = addPattern("Ring", {.source = featureId(cube),
                                   .axis = {.origin = Point3D{}, .direction = {0.0, 0.0, 1.0}},
                                   .count = 4,
                                   .countParameter = count});
    }

    ObjectId addPattern(const std::string& name, const features::CircularPatternDefinition& definition) {
        auto feature = features::CircularPatternFeature::create(name, definition);
        REQUIRE(feature.has_value());
        return doc.addObject(std::move(*feature)).value();
    }

    [[nodiscard]] features::CircularPatternDefinition definitionOf(ObjectId pattern) const {
        return doc.findObjectAs<features::CircularPatternFeature>(pattern)->definition();
    }

    void setDefinition(ObjectId pattern, const features::CircularPatternDefinition& definition) {
        REQUIRE(doc.modifyObject<features::CircularPatternFeature>(pattern, [&](features::CircularPatternFeature& f) {
                       return f.setDefinition(definition);
                   }).has_value());
    }
};

// A bolt circle: a flange with six through holes.
//
//   radius -> Disc (XY: circle about the origin) -> Flange (extrude) <- thickness
//   Flange -> Bolt (through hole from the bottom face at (40, 0)) <- diameter
//   Bolt -> Bolts (circular pattern about the Z axis, full circle) <- count
//
// The flange is 60 mm in radius and 10 mm thick; the bolt circle is 40 mm in
// radius. Bolt starts on the bottom face, the flange's start plane, so any
// thickness keeps it through. Bolts is the only result body. IDs: radius 1,
// thickness 2, diameter 3, count 4, Disc 5, Flange 6, Bolt 7, Bolts 8.
struct BoltCircleModel {
    Document doc{"Flange"};
    ParameterId radius, thickness, diameter, count;
    ObjectId disc, flange, bolt, bolts;

    static FeatureId featureId(ObjectId id) { return FeatureId::fromValue(id.value()); }

    /// In mm^3: V = pi R^2 H - n pi (d/2)^2 H.
    static double expectedVolume(double radiusMm, double thicknessMm, double diameterMm, double holes) {
        return std::numbers::pi * thicknessMm * (radiusMm * radiusMm - holes * diameterMm * diameterMm / 4.0);
    }

    BoltCircleModel() {
        using namespace bettercad::literals;
        using namespace bettercad::sketch;
        radius = doc.createParameter("radius", 60_mm, units::mm).value();
        thickness = doc.createParameter("thickness", 10_mm, units::mm).value();
        diameter = doc.createParameter("diameter", 10_mm, units::mm).value();
        count = doc.createParameter("count", 6.0, kUnitless).value();

        auto circle = std::make_unique<Sketch>("Disc");
        const EntityId rim = require(circle->addCircle(Point2D{}, 50_mm));
        require(circle->addFixed(std::get<CircleEntity>(circle->findEntity(rim)->geometry).center));
        const ConstraintId r = require(circle->addRadius(rim, 50_mm));
        REQUIRE(circle->setConstraintParameter(r, radius).has_value());
        disc = doc.addObject(std::move(circle)).value();

        auto extrude = features::ExtrudeFeature::create(
            "Flange", {.profile = SketchId::fromValue(disc.value()), .depthParameter = thickness});
        REQUIRE(extrude.has_value());
        flange = doc.addObject(std::move(*extrude)).value();
        auto hole = features::HoleFeature::create("Bolt", {.target = featureId(flange),
                                                           .face = bottomFace(),
                                                           .center = Point2D{40_mm, 0_mm},
                                                           .diameter = 10_mm,
                                                           .diameterParameter = diameter});
        REQUIRE(hole.has_value());
        bolt = doc.addObject(std::move(*hole)).value();
        bolts = addPattern("Bolts", {.source = featureId(bolt),
                                     .axis = {.origin = Point3D{}, .direction = {0.0, 0.0, 1.0}},
                                     .count = 6,
                                     .countParameter = count});
    }

    ObjectId addPattern(const std::string& name, const features::CircularPatternDefinition& definition) {
        auto feature = features::CircularPatternFeature::create(name, definition);
        REQUIRE(feature.has_value());
        return doc.addObject(std::move(*feature)).value();
    }

    [[nodiscard]] features::CircularPatternDefinition definitionOf(ObjectId pattern) const {
        return doc.findObjectAs<features::CircularPatternFeature>(pattern)->definition();
    }

    void setDefinition(ObjectId pattern, const features::CircularPatternDefinition& definition) {
        REQUIRE(doc.modifyObject<features::CircularPatternFeature>(pattern, [&](features::CircularPatternFeature& f) {
                       return f.setDefinition(definition);
                   }).has_value());
    }
};

// A peg off every axis: a cylinder of radius 3 mm from z = 17 to 23 mm about
// the line x = 40, y = 10 (centre (40, 10, 20)), a new-body extrude of a
// circle. Its two rims are circles that no other instance shares, so each
// instance of a pattern of it can be found by its rims. IDs: PegSketch 1,
// Peg 2.
struct PegModel {
    Document doc{"Peg"};
    ObjectId sketch, peg;

    PegModel() {
        using namespace bettercad::literals;
        const auto plane = Frame3D::create(Point3D{0_mm, 0_mm, 17_mm}, Direction3D::unitZ(), Direction3D::unitX());
        REQUIRE(plane.has_value());
        auto circle = std::make_unique<sketch::Sketch>("PegSketch", *plane);
        require(circle->addCircle(Point2D{40_mm, 10_mm}, 3_mm));
        sketch = doc.addObject(std::move(circle)).value();
        auto extrude = features::ExtrudeFeature::create(
            "Peg", {.profile = SketchId::fromValue(sketch.value()), .depth = 6_mm});
        REQUIRE(extrude.has_value());
        peg = doc.addObject(std::move(*extrude)).value();
    }
};

} // namespace bettercad::test
