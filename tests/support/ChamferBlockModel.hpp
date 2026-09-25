#pragma once

#include "support/BlockModel.hpp"

#include <bettercad/features/ChamferFeature.hpp>

#include <cmath>
#include <numbers>
#include <string>

namespace bettercad::test {

// A chamfered block (see BlockModel):
//
//   Pad -> Edge (chamfer of the top front edge, equal distance) <- size
//
// The top front edge lies on the line y = 0, z = 20 mm along X; a 5 mm
// chamfer removes a prism of cross-section size^2 / 2 along the full width.
// Edge is the only result body.
struct ChamferBlockModel : BlockModel {
    ParameterId size;
    ObjectId edge;

    /// V = W L H - (d^2 / 2) W, in mm^3.
    static double expectedVolume(double widthMm, double lengthMm, double heightMm, double sizeMm) {
        return widthMm * lengthMm * heightMm - 0.5 * sizeMm * sizeMm * widthMm;
    }

    ChamferBlockModel() : BlockModel("size", Length::fromSi(0.005)), size(extra) {
        using namespace bettercad::literals;
        edge = addChamfer("Edge", {.target = featureId(pad), .edges = { features::ChamferEdge{alongX(0, 20)}}, .distance = 5_mm,
                                   .distanceParameter = size});
    }

    ObjectId addChamfer(const std::string& name, const features::ChamferDefinition& definition) {
        return add<features::ChamferFeature>(name, definition);
    }

    [[nodiscard]] features::ChamferDefinition definitionOf(ObjectId chamfer) const {
        return BlockModel::definitionOf<features::ChamferFeature>(chamfer);
    }

    void setDefinition(ObjectId chamfer, const features::ChamferDefinition& definition) {
        BlockModel::setDefinition<features::ChamferFeature>(chamfer, definition);
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
                                     .edges = { features::ChamferEdge{alongX(50, 20)}},
                                     .mode = ChamferMode::TwoDistance,
                                     .distance = 4_mm,
                                     .distance2 = 2_mm,
                                     .referenceSide = Direction3D::unitZ()});
        slope = addChamfer("Slope", {.target = featureId(bevel),
                                     .edges = { features::ChamferEdge{alongY(0, 0)}},
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
