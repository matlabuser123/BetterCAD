#pragma once

#include "features/FeatureTestSupport.hpp"
#include "support/BlockModel.hpp"
#include "support/HoleModels.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/parameters/Parameter.hpp>
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

} // namespace bettercad::test
