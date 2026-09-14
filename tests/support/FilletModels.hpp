#pragma once

#include "support/BlockModel.hpp"
#include "support/TurnedPartModel.hpp"

#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/features/FilletFeature.hpp>

#include <catch2/catch_test_macros.hpp>

#include <numbers>
#include <string>

namespace bettercad::test {

/// The area a fillet of radius r takes out of (or adds to) a square corner:
/// the square r x r less the quarter disc, per unit length of a straight edge.
inline double filletCorner(double rMm) {
    return rMm * rMm * (1.0 - std::numbers::pi / 4.0);
}

// A filleted block (see BlockModel):
//
//   Pad -> Round (fillet of the top front edge) <- radius
//
// The top front edge lies on the line y = 0, z = 20 mm along X; a 5 mm
// fillet removes a prism of cross-section r^2 (1 - pi/4) along the full
// width. Round is the only result body.
struct FilletBlockModel : BlockModel {
    ParameterId radius;
    ObjectId round;

    /// V = W L H - W r^2 (1 - pi/4), in mm^3.
    static double expectedVolume(double widthMm, double lengthMm, double heightMm, double radiusMm) {
        return widthMm * lengthMm * heightMm - widthMm * filletCorner(radiusMm);
    }

    FilletBlockModel() : BlockModel("radius", Length::fromSi(0.005)), radius(extra) {
        using namespace bettercad::literals;
        round = addFillet("Round", {.target = featureId(pad), .edges = {alongX(0, 20)}, .radius = 5_mm,
                                    .radiusParameter = radius});
    }

    ObjectId addFillet(const std::string& name, const features::FilletDefinition& definition) {
        return add<features::FilletFeature>(name, definition);
    }

    [[nodiscard]] features::FilletDefinition definitionOf(ObjectId fillet) const {
        return BlockModel::definitionOf<features::FilletFeature>(fillet);
    }

    void setDefinition(ObjectId fillet, const features::FilletDefinition& definition) {
        BlockModel::setDefinition<features::FilletFeature>(fillet, definition);
    }
};

/// The filleted block plus Corner: a literal 3 mm fillet of the three edges
/// meeting at the bottom back right vertex (100, 50, 0), far from Round.
/// Corner is the only result body.
struct FilletVariants : FilletBlockModel {
    ObjectId corner;

    FilletVariants() {
        using namespace bettercad::literals;
        corner = addFillet("Corner", {.target = featureId(round),
                                      .edges = {alongX(50, 0), alongY(100, 0), alongZ(100, 50)},
                                      .radius = 3_mm});
    }

    /// In mm^3. The three edges lose the corner area over their lengths less
    /// r; the corner cube r^3 keeps only an eighth of a ball.
    static double expectedVolume(double radiusMm) {
        const double r = 3.0;
        return FilletBlockModel::expectedVolume(100, 50, 20, radiusMm) - filletCorner(r) * (100 + 50 + 20 - 3 * r) -
               r * r * r * (1.0 - std::numbers::pi / 6.0);
    }
};

/// The turned part with both rims of its top face rounded by Rims, 2 mm:
/// the outer circle (radius 15) and the edge of the bore (radius 5).
struct FilletedShaft : TurnedPartModel {
    ObjectId rims;

    FilletedShaft() {
        using namespace bettercad::literals;
        const auto outer = geometry::circleSignature(Point3D{0_mm, 0_mm, 40_mm}, Direction3D::unitZ(), 15_mm);
        const auto inner = geometry::circleSignature(Point3D{0_mm, 0_mm, 40_mm}, Direction3D::unitZ(), 5_mm);
        REQUIRE(outer.has_value());
        REQUIRE(inner.has_value());
        auto feature = features::FilletFeature::create(
            "Rims", {.target = featureId(groove), .edges = {*outer, *inner}, .radius = 2_mm});
        REQUIRE(feature.has_value());
        rims = doc.addObject(std::move(*feature)).value();
    }

    /// In mm^3, for a sweep of @p sweepDeg. Each rim's corner area turns
    /// about the axis at its centroid, which lies the same distance inside
    /// the outer rim as outside the bore's, so the two remove
    /// 2 pi r^2 (1 - pi/4) (R + b) per full turn (Pappus).
    static double expectedVolume(double sweepDeg) {
        return TurnedPartModel::expectedVolume(15, 40, sweepDeg, 5) -
               sweepDeg / 360.0 * 2.0 * std::numbers::pi * filletCorner(2.0) * (15.0 + 5.0);
    }
};

} // namespace bettercad::test
