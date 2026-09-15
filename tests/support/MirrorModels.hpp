#pragma once

#include "features/FeatureTestSupport.hpp"
#include "support/BlockModel.hpp"
#include "support/HoleModels.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/parameters/Parameter.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/MirrorFeature.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <numbers>
#include <string>

namespace bettercad::test {

/// Adds a square to @p sketch with its corner fixed at (x, y) and both sides
/// driven by @p side (a length parameter).
inline void addDrivenSquare(sketch::Sketch& sketch, Length x, Length y, ParameterId side) {
    using namespace bettercad::literals;
    using namespace bettercad::sketch;
    const auto lines = addRectangle(sketch, x, y, 5_mm, 5_mm);
    require(sketch.addFixed(std::get<LineEntity>(sketch.findEntity(lines[0])->geometry).start));
    require(sketch.addHorizontal(lines[0]));
    require(sketch.addHorizontal(lines[2]));
    require(sketch.addVertical(lines[1]));
    require(sketch.addVertical(lines[3]));
    const ConstraintId w = require(sketch.addDistance(lines[0], 1_mm));
    const ConstraintId h = require(sketch.addDistance(lines[1], 1_mm));
    REQUIRE(sketch.setConstraintParameter(w, side).has_value());
    REQUIRE(sketch.setConstraintParameter(h, side).has_value());
}

// A cube and its mirror image: a mirror of a new-body extrude.
//
//   size -> CubeSketch (plane z = -5: square with its corner fixed at (x0, -5))
//        -> Cube (extrude, new body, depth size)
//   Cube -> Mirror (feature mirror across the plane x = 0, original kept)
//
// With the default corner x0 = 15, the cube spans [15, 25] x [-5, 5] x
// [-5, 5] mm (centre (20, 0, 0)) and its mirror image [-25, -15] x [-5, 5] x
// [-5, 5]. IDs: size 1, CubeSketch 2, Cube 3, Mirror 4.
struct CubeMirrorModel {
    Document doc{"Cubes"};
    ParameterId size;
    ObjectId sketch, cube, mirror;

    static FeatureId featureId(ObjectId id) { return FeatureId::fromValue(id.value()); }

    explicit CubeMirrorModel(Length cornerX = Length::fromSi(0.015)) {
        using namespace bettercad::literals;
        size = doc.createParameter("size", 10_mm, units::mm).value();
        const auto plane = Frame3D::create(Point3D{0_mm, 0_mm, -(5_mm)}, Direction3D::unitZ(), Direction3D::unitX());
        REQUIRE(plane.has_value());
        auto square = std::make_unique<sketch::Sketch>("CubeSketch", *plane);
        addDrivenSquare(*square, cornerX, -(5_mm), size);
        sketch = doc.addObject(std::move(square)).value();
        auto extrude = features::ExtrudeFeature::create(
            "Cube", {.profile = SketchId::fromValue(sketch.value()), .depthParameter = size});
        REQUIRE(extrude.has_value());
        cube = doc.addObject(std::move(*extrude)).value();
        mirror = addMirror("Mirror", {.source = featureId(cube), .plane = {.origin = Point3D{}, .normal = {1.0, 0.0, 0.0}}});
    }

    ObjectId addMirror(const std::string& name, const features::MirrorDefinition& definition) {
        auto feature = features::MirrorFeature::create(name, definition);
        REQUIRE(feature.has_value());
        return doc.addObject(std::move(*feature)).value();
    }

    [[nodiscard]] features::MirrorDefinition definitionOf(ObjectId id) const {
        return doc.findObjectAs<features::MirrorFeature>(id)->definition();
    }

    void setDefinition(ObjectId id, const features::MirrorDefinition& definition) {
        REQUIRE(doc.modifyObject<features::MirrorFeature>(id, [&](features::MirrorFeature& f) {
                       return f.setDefinition(definition);
                   }).has_value());
    }
};

// A block with a hole and its mirror image (see HoleBlockModel):
//
//   Pad -> Drill (10 mm through hole from the bottom face at (hole_x, hole_y) = (30, 25))
//   Drill -> Mirror (feature mirror across the plane through the origin
//            along X, moved by `mid` = 50 mm: x = 50) <- mid
//
// The block is 100 x 50 x 20 mm; the mirror image of the hole is at x = 70.
// Drill starts on the bottom face, the pad's start plane, so any height
// keeps both holes through. Mirror is the only result body. IDs: width 1,
// length 2, height 3, diameter 4, Base 5, Pad 6, hole_x 7, hole_y 8,
// Drill 9, mid 10, Mirror 11.
struct HoleMirrorModel : HoleBlockModel {
    ParameterId mid;
    ObjectId mirror;

    /// In mm^3: V = L W H - 2 pi (d/2)^2 H.
    static double expectedVolume(double lengthMm, double heightMm, double diameterMm, double holes = 2) {
        return lengthMm * 50.0 * heightMm - holes * std::numbers::pi * diameterMm * diameterMm / 4.0 * heightMm;
    }

    HoleMirrorModel() {
        using namespace bettercad::literals;
        REQUIRE(doc.setParameterValue(holeX, 30_mm).has_value());
        features::HoleDefinition drillDefinition = definitionOf(drill);
        drillDefinition.face = bottomFace();
        setDefinition(drill, drillDefinition);
        mid = doc.createParameter("mid", 50_mm, units::mm).value();
        mirror = add<features::MirrorFeature>(
            "Mirror", {.source = featureId(drill),
                       .plane = {.origin = Point3D{}, .normal = {1.0, 0.0, 0.0}, .offsetParameter = mid}});
    }

    [[nodiscard]] features::MirrorDefinition mirrorOf(ObjectId id) const {
        return BlockModel::definitionOf<features::MirrorFeature>(id);
    }
    void setMirror(ObjectId id, const features::MirrorDefinition& definition) {
        BlockModel::setDefinition<features::MirrorFeature>(id, definition);
    }
};

// A plate with a boss and its mirror image (see BlockModel):
//
//   Pad (100 x 50 x 10 mm)
//   BossSketch (plane z = 10: square with its corner fixed at (25, 20), sides `boss`)
//     -> Boss (extrude 5 mm, join with Pad)
//   Boss -> Mirror (feature mirror across x = 50)
//
// The boss (10 x 10 x 5 mm, centre (30, 25, 12.5)) and its mirror image
// (centre (70, 25, 12.5)) stand apart on the plate. Mirror is the only
// result body. IDs: width 1, length 2, height 3, boss 4, Base 5, Pad 6,
// BossSketch 7, Boss 8, Mirror 9.
struct BossMirrorModel : BlockModel {
    ParameterId boss;
    ObjectId bossSketch, bossFeature, mirror;

    BossMirrorModel() : BlockModel("boss", Length::fromSi(0.01)), boss(extra) {
        using namespace bettercad::literals;
        REQUIRE(doc.setParameterValue(height, 10_mm).has_value());
        const auto plane = Frame3D::create(Point3D{0_mm, 0_mm, 10_mm}, Direction3D::unitZ(), Direction3D::unitX());
        REQUIRE(plane.has_value());
        auto square = std::make_unique<sketch::Sketch>("BossSketch", *plane);
        addDrivenSquare(*square, 25_mm, 20_mm, boss);
        bossSketch = doc.addObject(std::move(square)).value();
        bossFeature = add<features::ExtrudeFeature>("Boss", {.profile = sketchId(bossSketch),
                                                             .depth = 5_mm,
                                                             .operation = features::FeatureOperation::Join,
                                                             .target = featureId(pad)});
        mirror = add<features::MirrorFeature>(
            "Mirror", {.source = featureId(bossFeature),
                       .plane = {.origin = Point3D{50_mm, 0_mm, 0_mm}, .normal = {1.0, 0.0, 0.0}}});
    }

    [[nodiscard]] features::MirrorDefinition mirrorOf(ObjectId id) const {
        return definitionOf<features::MirrorFeature>(id);
    }
    void setMirror(ObjectId id, const features::MirrorDefinition& definition) {
        setDefinition<features::MirrorFeature>(id, definition);
    }
};

} // namespace bettercad::test
