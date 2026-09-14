#pragma once

#include "features/FeatureTestSupport.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/RevolveFeature.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <numbers>
#include <string>

namespace bettercad::test {

// A turned part built around the global Z axis:
//
//   radius, height -> Profile (XZ plane: rectangle with one edge on the axis)
//                  -> Turn (revolve about the sketch Y axis = global Z) <- sweep
//   BoreSketch (XY plane: circle of radius `bore` on the axis)
//                  -> Bore (extrude, symmetric 100 mm, cuts Turn)
//   GrooveSketch (XZ plane: rectangle 13..16 x 15..20 mm, construction line on the axis)
//                  -> Groove (revolve 360° about that line, cuts Bore)
//
// It exercises a parameter-driven angle, both axis kinds, a revolve as the
// target of an extrude and an extrude as the target of a revolve. Groove is
// the only result body.
struct TurnedPartModel {
    Document doc{"Shaft"};
    ParameterId radius, height, sweep, bore;
    ObjectId profile, turn, boreSketch, boreCut, grooveSketch, groove;
    EntityId grooveAxis;

    /// Exact volume in mm^3 of the part for the given parameters: a wedge of
    /// `sweepDeg` of the cylinder, minus the bore and the part of the groove
    /// annulus [13, 16] x [15, 20] mm that lies inside the radius.
    static double expectedVolume(double radiusMm, double heightMm, double sweepDeg, double boreMm) {
        const double wedge = sweepDeg / 360.0 * std::numbers::pi;
        const double grooveOuter = std::min(16.0, radiusMm);
        const double groove = grooveOuter > 13.0 ? (grooveOuter * grooveOuter - 13.0 * 13.0) * 5.0 : 0.0;
        return wedge * ((radiusMm * radiusMm - boreMm * boreMm) * heightMm - groove);
    }

    TurnedPartModel() {
        using namespace bettercad::literals;
        using namespace bettercad::sketch;
        using features::FeatureOperation;
        radius = doc.createParameter("radius", 15_mm, units::mm).value();
        height = doc.createParameter("height", 40_mm, units::mm).value();
        sweep = doc.createParameter("sweep", 360_deg, units::deg).value();
        bore = doc.createParameter("bore", 5_mm, units::mm).value();

        auto rectangle = std::make_unique<Sketch>("Profile", Frame3D::xz());
        const auto lines = addRectangle(*rectangle, 0_mm, 0_mm, 10_mm, 10_mm);
        require(rectangle->addFixed(std::get<LineEntity>(rectangle->findEntity(lines[0])->geometry).start));
        require(rectangle->addHorizontal(lines[0]));
        require(rectangle->addHorizontal(lines[2]));
        require(rectangle->addVertical(lines[1]));
        require(rectangle->addVertical(lines[3]));
        const ConstraintId w = require(rectangle->addDistance(lines[0], 1_mm));
        const ConstraintId h = require(rectangle->addDistance(lines[1], 1_mm));
        REQUIRE(rectangle->setConstraintParameter(w, radius).has_value());
        REQUIRE(rectangle->setConstraintParameter(h, height).has_value());
        profile = doc.addObject(std::move(rectangle)).value();
        turn = addRevolve("Turn", {.profile = sketchId(profile), .axis = features::RevolveAxis::sketchY(),
                                   .angleParameter = sweep});

        auto circle = std::make_unique<Sketch>("BoreSketch");
        const EntityId hole = require(circle->addCircle(Point2D{}, 1_mm));
        require(circle->addFixed(std::get<CircleEntity>(circle->findEntity(hole)->geometry).center));
        const ConstraintId r = require(circle->addRadius(hole, 1_mm));
        REQUIRE(circle->setConstraintParameter(r, bore).has_value());
        boreSketch = doc.addObject(std::move(circle)).value();
        auto cut = features::ExtrudeFeature::create(
            "Bore", {.profile = sketchId(boreSketch), .depth = 100_mm,
                     .direction = features::ExtrudeDirection::Symmetric, .operation = FeatureOperation::Cut,
                     .target = featureId(turn)});
        REQUIRE(cut.has_value());
        boreCut = doc.addObject(std::move(*cut)).value();

        auto groovePlane = std::make_unique<Sketch>("GrooveSketch", Frame3D::xz());
        addRectangle(*groovePlane, 13_mm, 15_mm, 3_mm, 5_mm);
        grooveAxis = require(groovePlane->addLine(Point2D{}, Point2D{0_mm, 10_mm}));
        REQUIRE(groovePlane->setConstruction(grooveAxis, true).has_value());
        grooveSketch = doc.addObject(std::move(groovePlane)).value();
        groove = addRevolve("Groove", {.profile = sketchId(grooveSketch),
                                       .axis = features::RevolveAxis::alongLine(grooveAxis),
                                       .operation = FeatureOperation::Cut,
                                       .target = featureId(boreCut)});
    }

    static SketchId sketchId(ObjectId id) { return SketchId::fromValue(id.value()); }
    static FeatureId featureId(ObjectId id) { return FeatureId::fromValue(id.value()); }

    ObjectId addRevolve(const std::string& name, const features::RevolveDefinition& definition) {
        auto feature = features::RevolveFeature::create(name, definition);
        REQUIRE(feature.has_value());
        return doc.addObject(std::move(*feature)).value();
    }
};

} // namespace bettercad::test
