#pragma once

#include "features/FeatureTestSupport.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/features/ChamferFeature.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <numbers>
#include <string>

namespace bettercad::test {

// A chamfered block:
//
//   width, length -> Base (XY plane: rectangle with a corner fixed at the origin)
//                 -> Pad (extrude along +Z) <- height
//   Pad -> Edge (chamfer of the top front edge, equal distance) <- size
//
// The block spans [0, width] x [0, length] x [0, height] = 100 x 50 x 20 mm.
// The top front edge lies on the line y = 0, z = 20 mm along X; a 5 mm
// chamfer removes a prism of cross-section size^2 / 2 along the full width.
// Edge is the only result body.
struct ChamferBlockModel {
    Document doc{"Block"};
    ParameterId width, length, height, size;
    ObjectId base, pad, edge;

    /// V = W L H - (d^2 / 2) W, in mm^3.
    static double expectedVolume(double widthMm, double lengthMm, double heightMm, double sizeMm) {
        return widthMm * lengthMm * heightMm - 0.5 * sizeMm * sizeMm * widthMm;
    }

    /// The line along X through (0, y, z) mm, e.g. alongX(0, 20) is the top front edge.
    static geometry::EdgeSignature alongX(double yMm, double zMm) {
        return geometry::lineSignature(Point3D{Length{}, yMm * units::mm, zMm * units::mm}, Direction3D::unitX());
    }
    /// The line along Y through (x, 0, z) mm.
    static geometry::EdgeSignature alongY(double xMm, double zMm) {
        return geometry::lineSignature(Point3D{xMm * units::mm, Length{}, zMm * units::mm}, Direction3D::unitY());
    }
    /// The line along Z through (x, y, 0) mm.
    static geometry::EdgeSignature alongZ(double xMm, double yMm) {
        return geometry::lineSignature(Point3D{xMm * units::mm, yMm * units::mm, Length{}}, Direction3D::unitZ());
    }

    ChamferBlockModel() {
        using namespace bettercad::literals;
        using namespace bettercad::sketch;
        width = doc.createParameter("width", 100_mm, units::mm).value();
        length = doc.createParameter("length", 50_mm, units::mm).value();
        height = doc.createParameter("height", 20_mm, units::mm).value();
        size = doc.createParameter("size", 5_mm, units::mm).value();

        auto rectangle = std::make_unique<Sketch>("Base");
        const auto lines = addRectangle(*rectangle, 0_mm, 0_mm, 10_mm, 10_mm);
        require(rectangle->addFixed(std::get<LineEntity>(rectangle->findEntity(lines[0])->geometry).start));
        require(rectangle->addHorizontal(lines[0]));
        require(rectangle->addHorizontal(lines[2]));
        require(rectangle->addVertical(lines[1]));
        require(rectangle->addVertical(lines[3]));
        const ConstraintId w = require(rectangle->addDistance(lines[0], 1_mm));
        const ConstraintId l = require(rectangle->addDistance(lines[1], 1_mm));
        REQUIRE(rectangle->setConstraintParameter(w, width).has_value());
        REQUIRE(rectangle->setConstraintParameter(l, length).has_value());
        base = doc.addObject(std::move(rectangle)).value();

        auto extrude = features::ExtrudeFeature::create("Pad", {.profile = sketchId(base), .depthParameter = height});
        REQUIRE(extrude.has_value());
        pad = doc.addObject(std::move(*extrude)).value();

        edge = addChamfer("Edge", {.target = featureId(pad), .edges = {alongX(0, 20)}, .distance = 5_mm,
                                   .distanceParameter = size});
    }

    static SketchId sketchId(ObjectId id) { return SketchId::fromValue(id.value()); }
    static FeatureId featureId(ObjectId id) { return FeatureId::fromValue(id.value()); }

    ObjectId addChamfer(const std::string& name, const features::ChamferDefinition& definition) {
        auto feature = features::ChamferFeature::create(name, definition);
        REQUIRE(feature.has_value());
        return doc.addObject(std::move(*feature)).value();
    }

    [[nodiscard]] features::ChamferDefinition definitionOf(ObjectId chamfer) const {
        return doc.findObjectAs<features::ChamferFeature>(chamfer)->definition();
    }

    void setDefinition(ObjectId chamfer, const features::ChamferDefinition& definition) {
        REQUIRE(doc.modifyObject<features::ChamferFeature>(
                       chamfer, [&](features::ChamferFeature& f) { return f.setDefinition(definition); })
                    .has_value());
    }
};

/// The block with a chamfer of every mode, each consuming the one before:
/// Edge (equal distance, driven by `size`), Bevel (4 mm on the top face and
/// 2 mm on the back face of the top back edge) and Slope (3 mm on the bottom
/// face at 30 degrees to it, along the bottom left edge). Slope is the only
/// result body.
struct ChamferVariants : ChamferBlockModel {
    ObjectId bevel, slope;

    ChamferVariants() {
        using namespace bettercad::literals;
        using geometry::ChamferMode;
        bevel = addChamfer("Bevel", {.target = featureId(edge),
                                     .edges = {alongX(50, 20)},
                                     .mode = ChamferMode::TwoDistance,
                                     .distance = 4_mm,
                                     .distance2 = 2_mm,
                                     .referenceSide = Direction3D::unitZ()});
        slope = addChamfer("Slope", {.target = featureId(bevel),
                                     .edges = {alongY(0, 0)},
                                     .mode = ChamferMode::DistanceAngle,
                                     .distance = 3_mm,
                                     .angle = 30_deg,
                                     .referenceSide = Direction3D::unitZ().reversed()});
    }

    /// In mm^3. The three chamfers are on edges that do not meet, so their
    /// removed prisms (d1 d2 L / 2 each) add up.
    static double expectedVolume(double sizeMm) {
        return ChamferBlockModel::expectedVolume(100, 50, 20, sizeMm) - 0.5 * 4.0 * 2.0 * 100.0 -
               0.5 * 3.0 * (3.0 * std::tan(std::numbers::pi / 6.0)) * 50.0;
    }
};

} // namespace bettercad::test
