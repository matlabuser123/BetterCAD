#pragma once

#include "support/BlockModel.hpp"

#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/geometry/Hole.hpp>
#include <bettercad/features/HoleFeature.hpp>

#include <numbers>
#include <string>

namespace bettercad::test {

/// A face on the plane z = @p zMm facing up, e.g. the block's top face.
inline geometry::FaceSignature topFace(double zMm = 20) {
    return geometry::planeSignature(Point3D{Length{}, Length{}, zMm * units::mm}, Direction3D::unitZ());
}
/// A face on the plane z = @p zMm facing down, e.g. the block's bottom face.
inline geometry::FaceSignature bottomFace(double zMm = 0) {
    return geometry::planeSignature(Point3D{Length{}, Length{}, zMm * units::mm}, Direction3D::unitZ().reversed());
}

// A drilled block (see BlockModel):
//
//   Pad -> Drill (through hole in the top face) <- diameter, hole_x, hole_y
//
// Drill is a 10 mm through hole at (hole_x, hole_y) = (50, 25) mm in face
// coordinates, which on the top face are the model's x and y. Drill is the
// only result body. IDs: parameters 1-4, Base 5, Pad 6, hole_x 7, hole_y 8,
// Drill 9.
struct HoleBlockModel : BlockModel {
    ParameterId diameter, holeX, holeY;
    ObjectId drill;

    /// V = W L H - pi (d/2)^2 H, in mm^3.
    static double expectedVolume(double widthMm, double lengthMm, double heightMm, double diameterMm) {
        return widthMm * lengthMm * heightMm - std::numbers::pi * diameterMm * diameterMm / 4.0 * heightMm;
    }

    HoleBlockModel() : BlockModel("diameter", Length::fromSi(0.01)), diameter(extra) {
        using namespace bettercad::literals;
        holeX = doc.createParameter("hole_x", 50_mm, units::mm).value();
        holeY = doc.createParameter("hole_y", 25_mm, units::mm).value();
        drill = addHole("Drill", {.target = featureId(pad),
                                  .face = topFace(),
                                  .center = Point2D{50_mm, 25_mm},
                                  .centerUParameter = holeX,
                                  .centerVParameter = holeY,
                                  .diameter = 10_mm,
                                  .diameterParameter = diameter});
    }

    ObjectId addHole(const std::string& name, const features::HoleDefinition& definition) {
        return add<features::HoleFeature>(name, definition);
    }

    [[nodiscard]] features::HoleDefinition definitionOf(ObjectId hole) const {
        return BlockModel::definitionOf<features::HoleFeature>(hole);
    }

    void setDefinition(ObjectId hole, const features::HoleDefinition& definition) {
        BlockModel::setDefinition<features::HoleFeature>(hole, definition);
    }
};

/// The drilled block plus two more holes in the top face, each consuming the
/// one before: Pocket, a blind 6 mm hole 12 mm deep with a 10 mm counterbore
/// 4 mm deep at (20, 25); and Sink, a through 6 mm hole with a 12 mm, 90°
/// countersink at (80, 25). Sink is the only result body.
struct HoleVariants : HoleBlockModel {
    ObjectId pocket, sink;

    HoleVariants() {
        using namespace bettercad::literals;
        using geometry::HoleExtent;
        using geometry::HoleType;
        pocket = addHole("Pocket", {.target = featureId(drill),
                                    .face = topFace(),
                                    .center = Point2D{20_mm, 25_mm},
                                    .type = HoleType::Counterbore,
                                    .extent = HoleExtent::Blind,
                                    .diameter = 6_mm,
                                    .depth = 12_mm,
                                    .counterboreDiameter = 10_mm,
                                    .counterboreDepth = 4_mm});
        sink = addHole("Sink", {.target = featureId(pocket),
                                .face = topFace(),
                                .center = Point2D{80_mm, 25_mm},
                                .type = HoleType::Countersink,
                                .diameter = 6_mm,
                                .countersinkDiameter = 12_mm,
                                .countersinkAngle = 90_deg});
    }

    /// In mm^3, for Drill's diameter @p drillMm. Pocket removes
    /// pi 3^2 12 + pi (5^2 - 3^2) 4 = 172 pi. Sink removes pi 3^2 20 plus the
    /// cone beyond the bore: its frustum pi 3 (6^2 + 6*3 + 3^2) / 3 = 63 pi,
    /// less pi 3^2 3 = 27 pi, so 216 pi in all.
    static double expectedVolume(double drillMm) {
        return HoleBlockModel::expectedVolume(100, 50, 20, drillMm) - (172.0 + 216.0) * std::numbers::pi;
    }

    /// In mm^2, with Drill at 10 mm. Drill: the top and bottom lose 25 pi
    /// each, its wall adds 2 pi 5 20, so +150 pi. Pocket: the top loses 25 pi;
    /// the counterbore's wall 2 pi 5 4, its shelf pi (5^2 - 3^2), the bore's
    /// wall 2 pi 3 8 and the floor 9 pi: +88 pi. Sink: the top loses 36 pi,
    /// the bottom 9 pi; the cone's side pi (6 + 3) 3 sqrt(2) and the bore's
    /// wall 2 pi 3 17: +(57 + 27 sqrt(2)) pi.
    static double expectedArea() {
        return 16000.0 + (150.0 + 88.0 + 57.0 + 27.0 * std::numbers::sqrt2) * std::numbers::pi;
    }
};

} // namespace bettercad::test
