#pragma once

#include "features/FeatureTestSupport.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/Uuid.hpp>
#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <numbers>
#include <string>

namespace bettercad::test {

// A plate whose dimensions are driven by parameter expressions
// (P12-PARAM-001):
//
//   width           100 mm                        free
//   edge_distance    15 mm                        free
//   hole_radius       5 mm                        free
//   height          = width / 2
//   thickness       = 0.1 * width
//   hole_spacing    = width - 2 * edge_distance
//   hole_y          = height / 2                  a chain: width -> height -> hole_y
//
// PlateSketch is a width x height rectangle with a corner at the origin and
// two holes of radius hole_radius on the line y = hole_y: the first
// edge_distance from the left edge, the second hole_spacing to its right.
// Plate extrudes it by thickness.
//
// The driven parameters are created first, and hole_y before height, so the
// creation (ID) order is not the dependency order: a correct evaluation order
// can only come from the dependencies.
struct DrivenPlateModel {
    static constexpr std::string_view kDocumentId = "6e0f2a64-4c1b-4d7a-9b8e-2f5d3c1a0b9e";

    Document doc{DocumentId::fromValue(*Uuid::parse(kDocumentId)), "DrivenPlate"};
    CommandHistory history;
    ParameterId holeY, height, thickness, holeSpacing, width, edgeDistance, holeRadius;
    ObjectId sketch, plate;

    DrivenPlateModel() {
        using namespace bettercad::literals;
        // Placeholder values: evaluation replaces them.
        holeY = doc.createParameter("hole_y", 1_mm, units::mm).value();
        height = doc.createParameter("height", 1_mm, units::mm).value();
        thickness = doc.createParameter("thickness", 1_mm, units::mm).value();
        holeSpacing = doc.createParameter("hole_spacing", 1_mm, units::mm).value();
        width = doc.createParameter("width", 100_mm, units::mm).value();
        edgeDistance = doc.createParameter("edge_distance", 15_mm, units::mm).value();
        holeRadius = doc.createParameter("hole_radius", 5_mm, units::mm).value();
        REQUIRE(doc.setParameterExpression(holeY, "height / 2").has_value());
        REQUIRE(doc.setParameterExpression(height, "width / 2").has_value());
        REQUIRE(doc.setParameterExpression(thickness, "0.1 * width").has_value());
        REQUIRE(doc.setParameterExpression(holeSpacing, "width - 2 * edge_distance").has_value());

        sketch = doc.addObject(makeSketch()).value();
        auto feature = features::ExtrudeFeature::create(
            "Plate", {.profile = SketchId::fromValue(sketch.value()), .depthParameter = thickness});
        REQUIRE(feature.has_value());
        plate = doc.addObject(std::move(*feature)).value();
    }

    [[nodiscard]] std::unique_ptr<sketch::Sketch> makeSketch() const {
        using namespace bettercad::literals;
        using namespace bettercad::sketch;
        auto s = std::make_unique<Sketch>("PlateSketch");
        const auto lines = addRectangle(*s, 0_mm, 0_mm, 90_mm, 40_mm);
        const EntityId origin = std::get<LineEntity>(s->findEntity(lines[0])->geometry).start;
        require(s->addFixed(origin));
        require(s->addHorizontal(lines[0]));
        require(s->addHorizontal(lines[2]));
        require(s->addVertical(lines[1]));
        require(s->addVertical(lines[3]));
        drive(*s, require(s->addDistance(lines[0], 1_mm)), width);
        drive(*s, require(s->addDistance(lines[1], 1_mm)), height);

        const EntityId first = require(s->addCircle(Point2D{10_mm, 20_mm}, 4_mm));
        const EntityId second = require(s->addCircle(Point2D{70_mm, 20_mm}, 4_mm));
        const EntityId c1 = std::get<CircleEntity>(s->findEntity(first)->geometry).center;
        const EntityId c2 = std::get<CircleEntity>(s->findEntity(second)->geometry).center;
        drive(*s, require(s->addDistance(c1, lines[3], 1_mm)), edgeDistance);
        drive(*s, require(s->addDistance(c1, lines[0], 1_mm)), holeY);
        require(s->addHorizontal(c1, c2));
        drive(*s, require(s->addDistance(c1, c2, 1_mm)), holeSpacing);
        drive(*s, require(s->addRadius(first, 1_mm)), holeRadius);
        drive(*s, require(s->addRadius(second, 1_mm)), holeRadius);
        return s;
    }

    static void drive(sketch::Sketch& s, ConstraintId constraint, ParameterId parameter) {
        REQUIRE(s.setConstraintParameter(constraint, parameter).has_value());
    }

    /// Sets a free parameter through the undoable command.
    void set(ParameterId parameter, Length value) {
        REQUIRE(history.execute(doc, ModifyParameterCommand::setValue(parameter, value)).has_value());
    }

    [[nodiscard]] double si(ParameterId parameter) const { return doc.parameters().find(parameter)->siValue(); }

    /// The plate's volume in mm^3 for a given width and hole radius, from the
    /// definitions above written out by hand: height = w / 2,
    /// thickness = w / 10, two holes of radius r.
    [[nodiscard]] static double expectedVolumeMm3(double widthMm, double radiusMm = 5.0) {
        const double heightMm = widthMm / 2.0;
        const double thicknessMm = widthMm / 10.0;
        return (widthMm * heightMm - 2.0 * std::numbers::pi * radiusMm * radiusMm) * thicknessMm;
    }
};

} // namespace bettercad::test
