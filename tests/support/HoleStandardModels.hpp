#pragma once

#include "support/BodyPrint.hpp"
#include "support/ShellModels.hpp"

#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/geometry/Hole.hpp>
#include <bettercad/core/standards/ClearanceHoles.hpp>
#include <bettercad/core/standards/MetricThreads.hpp>
#include <bettercad/features/HoleFeature.hpp>

#include <cmath>
#include <numbers>
#include <string>
#include <vector>

namespace bettercad::test {

// P12-HOLE-001 reference model: a plate of holes of standard sizes. Every
// expected value is written out from the standards' own definitions (the
// basic profile of ISO 68-1, the clearance holes of ISO 273) and the
// dimensions below; none is read from a result.

/// The basic minor diameter of an ISO metric thread (ISO 68-1): D - 5/4 H,
/// where H = sqrt(3)/2 P is the height of the fundamental triangle. Both in
/// mm.
inline double basicMinorDiameter(double diameterMm, double pitchMm) {
    return diameterMm - 1.25 * (std::sqrt(3.0) / 2.0) * pitchMm;
}

/// A plate 80 x 50 x 12 mm with the holes of P12-HOLE-001, each consuming
/// the one before it:
///
///   thread_length  8 mm            bore  10 mm
///   PlateSketch    XY plane: rectangle x 0..80, y 0..50
///   Plate          extrude 12 mm (new body)
///   Tapped         M8-6H through hole at (15, 15), threaded full length
///   BlindTapped    M6-6H blind hole 10 mm deep at (15, 35), threaded
///                  thread_length deep
///   Seat           clearance hole for M8 (medium series, 9 mm) at (40, 25),
///                  H13, with a 15 mm counterbore 5 mm deep
///   Boss           M10-6H through hole at (65, 32), threaded full length,
///                  with a 20 mm spotface 1 mm deep
///   Reamed         blind hole 8 mm deep at (65, 8), diameter bore, H7
///
/// Every hole is drilled into the top face (z = 12 mm). Reamed is the only
/// result body. IDs: thread_length 1, bore 2, PlateSketch 3, Plate 4,
/// Tapped 5, BlindTapped 6, Seat 7, Boss 8, Reamed 9.
struct TappedPlateModel : FaceKindModel {
    ParameterId threadLength, bore;
    ObjectId plateSketch, plate, tapped, blindTapped, seat, boss, reamed;

    static constexpr double kWidthMm = 80.0;
    static constexpr double kDepthMm = 50.0;
    static constexpr double kThicknessMm = 12.0;

    static geometry::FaceSignature topFace() {
        return geometry::planeSignature(Point3D{Length{}, Length{}, kThicknessMm * units::mm},
                                        Direction3D::unitZ());
    }

    TappedPlateModel() : FaceKindModel("2f8b41d7-6c05-4a92-9e37-5b0c1d84a6f3", "Plate") {
        using namespace bettercad::literals;
        using geometry::HoleExtent;
        using geometry::HoleType;
        threadLength = doc.createParameter("thread_length", 8_mm, units::mm).value();
        bore = doc.createParameter("bore", 10_mm, units::mm).value();
        plateSketch = addFixedRectangle("PlateSketch", 0.0, 0.0, kWidthMm, kDepthMm);
        plate = addBoss("Plate", plateSketch, kThicknessMm);

        tapped = addHole("Tapped", {.target = featureOf(plate),
                                    .face = topFace(),
                                    .center = Point2D{15_mm, 15_mm},
                                    .thread = thread("M8")});
        blindTapped = addHole("BlindTapped", {.target = featureOf(tapped),
                                              .face = topFace(),
                                              .center = Point2D{15_mm, 35_mm},
                                              .extent = HoleExtent::Blind,
                                              .depth = 10_mm,
                                              .thread = thread("M6", 8_mm, threadLength)});
        seat = addHole("Seat", {.target = featureOf(blindTapped),
                                .face = topFace(),
                                .center = Point2D{40_mm, 25_mm},
                                .type = HoleType::Counterbore,
                                .counterboreDiameter = 15_mm,
                                .counterboreDepth = 5_mm,
                                .clearance = clearance("M8"),
                                .tolerance = standards::HoleToleranceClass{standards::HoleDeviation::H, 13}});
        boss = addHole("Boss", {.target = featureOf(seat),
                                .face = topFace(),
                                .center = Point2D{65_mm, 32_mm},
                                .type = HoleType::Spotface,
                                .spotfaceDiameter = 20_mm,
                                .spotfaceDepth = 1_mm,
                                .thread = thread("M10")});
        reamed = addHole("Reamed", {.target = featureOf(boss),
                                    .face = topFace(),
                                    .center = Point2D{65_mm, 8_mm},
                                    .extent = HoleExtent::Blind,
                                    .diameter = 10_mm,
                                    .diameterParameter = bore,
                                    .depth = 8_mm,
                                    .tolerance = standards::HoleToleranceClass{standards::HoleDeviation::H, 7}});
    }

    static features::HoleThread thread(const std::string& size, Length length = Length{},
                                       std::optional<ParameterId> parameter = std::nullopt) {
        auto parsed = standards::parseMetricThread(size);
        REQUIRE(parsed.has_value());
        features::HoleThread definition{.size = *parsed, .length = length};
        definition.lengthParameter = parameter;
        return definition;
    }

    static features::HoleClearance clearance(const std::string& size,
                                             standards::ClearanceSeries series = standards::ClearanceSeries::Medium) {
        auto parsed = standards::parseMetricThread(size);
        REQUIRE(parsed.has_value());
        return {.bolt = *parsed, .series = series};
    }

    ObjectId addHole(const std::string& name, const features::HoleDefinition& definition) {
        return add(features::HoleFeature::create(name, definition));
    }

    [[nodiscard]] features::HoleDefinition definitionOf(ObjectId hole) const {
        return doc.findObjectAs<features::HoleFeature>(hole)->definition();
    }

    void setDefinition(ObjectId hole, const features::HoleDefinition& definition) {
        REQUIRE(doc.modifyObject<features::HoleFeature>(hole, [&](features::HoleFeature& f) {
                       return f.setDefinition(definition);
                   })
                    .has_value());
    }

    /// The parts of the plate: the block less each hole, from the standards'
    /// own dimensions (mm).
    static std::vector<Part> parts(double boreMm = 10.0) {
        constexpr double t = kThicknessMm;
        // A thread costs no geometry: a tapped hole is cut at the thread's
        // basic minor diameter, and the blind one is 10 mm deep whatever its
        // thread's length.
        const double m8 = basicMinorDiameter(8.0, 1.25);
        const double m6 = basicMinorDiameter(6.0, 1.0);
        const double m10 = basicMinorDiameter(10.0, 1.5);
        return {
            boxPart({0.0, 0.0, 0.0}, {kWidthMm, kDepthMm, t}),
            zCylinderPart(15.0, 15.0, m8 / 2.0, 0.0, t, -1.0),
            zCylinderPart(15.0, 35.0, m6 / 2.0, t - 10.0, t, -1.0),
            zCylinderPart(40.0, 25.0, 9.0 / 2.0, 0.0, t, -1.0),
            zCylinderPart(40.0, 25.0, 15.0 / 2.0, t - 5.0, t, -1.0),
            zCylinderPart(40.0, 25.0, 9.0 / 2.0, t - 5.0, t, 1.0), // the counterbore's share of the bore
            zCylinderPart(65.0, 32.0, m10 / 2.0, 0.0, t, -1.0),
            zCylinderPart(65.0, 32.0, 20.0 / 2.0, t - 1.0, t, -1.0),
            zCylinderPart(65.0, 32.0, m10 / 2.0, t - 1.0, t, 1.0), // the spotface's share of the bore
            zCylinderPart(65.0, 8.0, boreMm / 2.0, t - 8.0, t, -1.0),
        };
    }

    static double expectedVolume(double boreMm = 10.0) {
        return volumeOf(parts(boreMm));
    }
};

} // namespace bettercad::test
