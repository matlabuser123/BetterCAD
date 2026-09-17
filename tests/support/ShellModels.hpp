#pragma once

#include "features/FeatureTestSupport.hpp"
#include "support/FaceKindModels.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/geometry/Hole.hpp>
#include <bettercad/core/geometry/Shell.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/FilletFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/RevolveFeature.hpp>
#include <bettercad/features/ShellFeature.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bettercad::test {

// P12-FEAT-003 reference models: shells whose volumes, centres and bounds are
// sums and differences of boxes and cylinders. The walls' corners are sharp
// (geometry::shellBody()): inward, a cavity is the body's section shrunk by
// the wall with its corners kept square, from the wall above the floor up to
// the opening; outward, the outside is the section grown by the wall, from
// the wall below the floor up to the opening. A round of radius r on the
// body is a round of r - t in the cavity and r + t outside. Every expected
// value is written out from the parameters; none is read from a result.

/// A part of a composite solid: its signed volume and its centre (mm).
using Part = std::pair<double, Vec3>;

/// An axis-aligned box from @p lo to @p hi (mm), added (+1) or removed (-1).
inline Part boxPart(const Vec3& lo, const Vec3& hi, double sign = 1.0) {
    return {sign * (hi[0] - lo[0]) * (hi[1] - lo[1]) * (hi[2] - lo[2]),
            {(lo[0] + hi[0]) / 2.0, (lo[1] + hi[1]) / 2.0, (lo[2] + hi[2]) / 2.0}};
}

/// A cylinder of radius @p r about the axis through (x, y) along Z, from
/// z0 to z1 (mm).
inline Part zCylinderPart(double x, double y, double r, double z0, double z1, double sign = 1.0) {
    return {sign * kPi * r * r * (z1 - z0), {x, y, (z0 + z1) / 2.0}};
}

/// A cylinder of radius @p r about the Y axis, from y0 to y1 (mm).
inline Part yCylinderPart(double r, double y0, double y1, double sign = 1.0) {
    return {sign * kPi * r * r * (y1 - y0), {0.0, (y0 + y1) / 2.0, 0.0}};
}

/// A prism along Z from z0 to z1 over a w x d rectangle centred at (x, y)
/// whose corners are rounded with radius @p round.
inline Part roundedPrismPart(double x, double y, double w, double d, double round, double z0, double z1,
                             double sign = 1.0) {
    return {sign * (w * d - (4.0 - kPi) * round * round) * (z1 - z0), {x, y, (z0 + z1) / 2.0}};
}

inline double volumeOf(const std::vector<Part>& parts) {
    double total = 0.0;
    for (const auto& [volume, centre] : parts) {
        total += volume;
    }
    return total;
}

/// The expected shape of a shell: volume and centre of its parts, bounds.
struct ExpectedShell {
    std::vector<Part> parts;
    Vec3 lower{};
    Vec3 upper{};

    [[nodiscard]] double volume() const { return volumeOf(parts); }
    [[nodiscard]] Vec3 centre() const { return weightedCentre(parts); }
};

/// The name of @p feature's face with @p role (and @p entity for a side).
inline FaceName nameOf(ObjectId feature, FaceRole role, std::optional<EntityId> entity = std::nullopt) {
    return FaceName{feature, FaceSelector{.role = role, .entity = entity}};
}

/// fixedPolygon() that also returns its lines: line i runs from corner i to
/// corner i + 1.
inline std::unique_ptr<sketch::Sketch> polygonWithLines(const std::string& name, const Frame3D& plane,
                                                        const std::vector<std::pair<double, double>>& corners,
                                                        std::vector<EntityId>& lines) {
    auto sketch = std::make_unique<sketch::Sketch>(name, plane);
    std::vector<EntityId> points;
    for (const auto& [u, v] : corners) {
        points.push_back(require(sketch->addPoint(Point2D{u * units::mm, v * units::mm})));
        require(sketch->addFixed(points.back()));
    }
    lines.clear();
    for (std::size_t i = 0; i < points.size(); ++i) {
        lines.push_back(require(sketch->addLine(points[i], points[(i + 1) % points.size()])));
    }
    return sketch;
}

// A 100 x 60 block shelled three ways:
//
//   height        40 mm
//   wall          5 mm
//   BlockSketch   XY plane: rectangle x 0..100, y 0..60; lines front
//                 (y = 0), right (x = 100), back (y = 60), left (x = 0)
//   Block         extrude by height (new body)
//   Cup           shell Block, open its end cap, walls `wall` inward
//   Tube          shell Block, open both caps, walls `wall` inward
//   Casing        shell Block, open its end cap, walls `wall` outward
//
// All three consume Block. IDs: height 1, wall 2, BlockSketch 3, Block 4,
// Cup 5, Tube 6, Casing 7.
struct ShelledBlockModel : FaceKindModel {
    static constexpr std::string_view kDocumentId = "5d2e8b17-3c4a-4f96-b7e0-9a1c6d3f2e84";
    ParameterId height, wall;
    ObjectId blockSketch, block, cup, tube, casing;
    std::array<EntityId, 4> lines{};

    ShelledBlockModel() : FaceKindModel(kDocumentId, "ShelledBlock") {
        using namespace bettercad::literals;
        using namespace bettercad::features;
        height = doc.createParameter("height", 40_mm, units::mm).value();
        wall = doc.createParameter("wall", 5_mm, units::mm).value();
        blockSketch = addFixedRectangle("BlockSketch", 0.0, 0.0, 100.0, 60.0, &lines);
        block = add(ExtrudeFeature::create("Block", {.profile = sketchOf(blockSketch), .depthParameter = height}));
        cup = add(ShellFeature::create("Cup", {.target = featureOf(block),
                                               .openFaces = {nameOf(block, FaceRole::EndCap)},
                                               .thicknessParameter = wall}));
        tube = add(ShellFeature::create(
            "Tube", {.target = featureOf(block),
                     .openFaces = {nameOf(block, FaceRole::EndCap), nameOf(block, FaceRole::StartCap)},
                     .thicknessParameter = wall}));
        casing = add(ShellFeature::create("Casing", {.target = featureOf(block),
                                                     .openFaces = {nameOf(block, FaceRole::EndCap)},
                                                     .thicknessParameter = wall,
                                                     .side = geometry::ShellSide::Outward}));
    }

    static ExpectedShell cupShape(double h, double t) {
        return {{boxPart({0, 0, 0}, {100, 60, h}), boxPart({t, t, t}, {100 - t, 60 - t, h}, -1.0)},
                {0, 0, 0},
                {100, 60, h}};
    }
    static ExpectedShell tubeShape(double h, double t) {
        return {{boxPart({0, 0, 0}, {100, 60, h}), boxPart({t, t, 0}, {100 - t, 60 - t, h}, -1.0)},
                {0, 0, 0},
                {100, 60, h}};
    }
    static ExpectedShell casingShape(double h, double t) {
        return {{boxPart({-t, -t, -t}, {100 + t, 60 + t, h}), boxPart({0, 0, 0}, {100, 60, h}, -1.0)},
                {-t, -t, -t},
                {100 + t, 60 + t, h}};
    }
    /// The rim left of the top: inside its outline inward, outside outward.
    static double innerRim(double t) { return 6000.0 - (100.0 - 2.0 * t) * (60.0 - 2.0 * t); }
    static double outerRim(double t) { return (100.0 + 2.0 * t) * (60.0 + 2.0 * t) - 6000.0; }
};

// Turned and curved bodies, all shelled by `wall`:
//
//   wall          3 mm
//   CanSketch     XY plane: circle r 20 about the origin
//   Can           extrude 50 mm (new body)
//   CanCup        shell Can, open its end cap, inward
//   CanCase       shell Can, open its end cap, outward
//   ShaftSketch   XY plane: polygon (0, 0) (30, 0) (30, 20) (15, 20)
//                 (15, 60) (0, 60); line 4 is the small end
//   Shaft         revolve 360 deg about the sketch's Y axis (the model's
//                 +Y): r 30 for y in [0, 20], r 15 for y in [20, 60]
//   ShaftBore     shell Shaft, open the side line 4 sweeps (the small
//                 end's disc), inward
//   ShaftSleeve   the same, outward
//
// Inward, the shaft's cavity is r 30 - t for y in [t, 20 - t] and r 15 - t
// from 20 - t up to the end; outward, its outside is r 30 + t for y in
// [-t, 20 + t] and r 15 + t from 20 + t up to the end.
// IDs: wall 1, CanSketch 2, Can 3, CanCup 4, CanCase 5, ShaftSketch 6,
// Shaft 7, ShaftBore 8, ShaftSleeve 9.
struct TurnedShellModel : FaceKindModel {
    static constexpr std::string_view kDocumentId = "a8c41f6e-2d7b-4e53-9f08-6b3e1c5d7a92";
    ParameterId wall;
    ObjectId canSketch, can, canCup, canCase, shaftSketch, shaft, shaftBore, shaftSleeve;
    std::vector<EntityId> shaftLines;

    TurnedShellModel() : FaceKindModel(kDocumentId, "TurnedShell") {
        using namespace bettercad::literals;
        using namespace bettercad::features;
        wall = doc.createParameter("wall", 3_mm, units::mm).value();
        canSketch = addCircleSketch("CanSketch", PlaneReference{}, 0.0, 0.0, 20.0);
        can = addBoss("Can", canSketch, 50.0);
        canCup = add(ShellFeature::create(
            "CanCup", {.target = featureOf(can), .openFaces = {nameOf(can, FaceRole::EndCap)}, .thicknessParameter = wall}));
        canCase = add(ShellFeature::create("CanCase", {.target = featureOf(can),
                                                       .openFaces = {nameOf(can, FaceRole::EndCap)},
                                                       .thicknessParameter = wall,
                                                       .side = geometry::ShellSide::Outward}));
        shaftSketch = doc.addObject(polygonWithLines("ShaftSketch", Frame3D::xy(),
                                                     {{0, 0}, {30, 0}, {30, 20}, {15, 20}, {15, 60}, {0, 60}},
                                                     shaftLines))
                          .value();
        shaft = add(RevolveFeature::create("Shaft", {.profile = sketchOf(shaftSketch), .axis = RevolveAxis::sketchY()}));
        const FaceName smallEnd = nameOf(shaft, FaceRole::Side, shaftLines[4]);
        shaftBore = add(ShellFeature::create(
            "ShaftBore", {.target = featureOf(shaft), .openFaces = {smallEnd}, .thicknessParameter = wall}));
        shaftSleeve = add(ShellFeature::create("ShaftSleeve", {.target = featureOf(shaft),
                                                               .openFaces = {smallEnd},
                                                               .thicknessParameter = wall,
                                                               .side = geometry::ShellSide::Outward}));
    }

    static ExpectedShell canCupShape(double t) {
        return {{zCylinderPart(0, 0, 20, 0, 50), zCylinderPart(0, 0, 20 - t, t, 50, -1.0)},
                {-20, -20, 0},
                {20, 20, 50}};
    }
    static ExpectedShell canCaseShape(double t) {
        return {{zCylinderPart(0, 0, 20 + t, -t, 50), zCylinderPart(0, 0, 20, 0, 50, -1.0)},
                {-20 - t, -20 - t, -t},
                {20 + t, 20 + t, 50}};
    }
    static std::vector<Part> shaftParts(double sign = 1.0) {
        return {yCylinderPart(30, 0, 20, sign), yCylinderPart(15, 20, 60, sign)};
    }
    static ExpectedShell boreShape(double t) {
        std::vector<Part> parts = shaftParts();
        parts.push_back(yCylinderPart(30 - t, t, 20 - t, -1.0));
        parts.push_back(yCylinderPart(15 - t, 20 - t, 60, -1.0));
        return {parts, {-30, 0, -30}, {30, 60, 30}};
    }
    static ExpectedShell sleeveShape(double t) {
        std::vector<Part> parts = shaftParts(-1.0);
        parts.push_back(yCylinderPart(30 + t, -t, 20 + t));
        parts.push_back(yCylinderPart(15 + t, 20 + t, 60));
        return {parts, {-30 - t, -t, -30 - t}, {30 + t, 60, 30 + t}};
    }
};

// Bodies with concave edges, holes and rounds, all shelled by `wall`:
//
//   height        40 mm
//   wall          5 mm
//   round         10 mm
//   EllSketch     XY plane: polygon (0, 0) (100, 0) (100, 30) (40, 30)
//                 (40, 80) (0, 80): arms 100 x 30 and 40 x 80, one
//                 concave corner at (40, 30)
//   Ell           extrude 50 mm (new body)
//   EllCup        shell Ell, open its end cap, inward
//   EllCase       shell Ell, open its end cap, outward
//   PlateSketch   XY plane: rectangle x 0..100, y 0..60
//   Plate         extrude by height (new body)
//   Drill         through hole d 20 at (50, 30) from Plate's bottom face
//                 (z = 0, facing -Z), so it stays put as the plate grows
//   DrillCup      shell Drill, open Plate's end cap, inward: the hole's
//                 wall becomes a tube of radius 10 + t
//   DrillCase     the same, outward: the hole narrows to 10 - t
//   PadSketch     XY plane: rectangle x 200..300, y 0..60
//   Pad           extrude by height (new body)
//   Round         fillet Pad's four vertical edges, radius `round`
//   RoundCup      shell Round, open Pad's end cap, inward
//   RoundCase     the same, outward
//
// IDs: height 1, wall 2, round 3, EllSketch 4, Ell 5, EllCup 6, EllCase 7,
// PlateSketch 8, Plate 9, Drill 10, DrillCup 11, DrillCase 12, PadSketch 13,
// Pad 14, Round 15, RoundCup 16, RoundCase 17.
struct ConcaveShellModel : FaceKindModel {
    static constexpr std::string_view kDocumentId = "3f7a9c2e-6b18-4d45-a0e3-8c5b2d9f1a67";
    ParameterId height, wall, round;
    ObjectId ellSketch, ell, ellCup, ellCase, plateSketch, plate, drill, drillCup, drillCase, padSketch, pad,
        rounded, roundCup, roundCase;

    ConcaveShellModel() : FaceKindModel(kDocumentId, "ConcaveShell") {
        using namespace bettercad::literals;
        using namespace bettercad::features;
        height = doc.createParameter("height", 40_mm, units::mm).value();
        wall = doc.createParameter("wall", 5_mm, units::mm).value();
        round = doc.createParameter("round", 10_mm, units::mm).value();
        ellSketch = doc.addObject(fixedPolygon("EllSketch", Frame3D::xy(),
                                               {{0, 0}, {100, 0}, {100, 30}, {40, 30}, {40, 80}, {0, 80}}))
                        .value();
        ell = addBoss("Ell", ellSketch, 50.0);
        const auto shellOf = [&](const std::string& name, ObjectId target, ObjectId opened,
                                 geometry::ShellSide side) {
            return add(ShellFeature::create(name, {.target = featureOf(target),
                                                   .openFaces = {nameOf(opened, FaceRole::EndCap)},
                                                   .thicknessParameter = wall,
                                                   .side = side}));
        };
        ellCup = shellOf("EllCup", ell, ell, geometry::ShellSide::Inward);
        ellCase = shellOf("EllCase", ell, ell, geometry::ShellSide::Outward);

        plateSketch = addFixedRectangle("PlateSketch", 0.0, 0.0, 100.0, 60.0);
        plate = add(ExtrudeFeature::create("Plate", {.profile = sketchOf(plateSketch), .depthParameter = height}));
        drill = add(HoleFeature::create(
            "Drill", {.target = featureOf(plate),
                      .face = geometry::planeSignature(Point3D{}, Direction3D::unitZ().reversed()),
                      .center = Point2D{50_mm, 30_mm},
                      .diameter = 20_mm}));
        drillCup = shellOf("DrillCup", drill, plate, geometry::ShellSide::Inward);
        drillCase = shellOf("DrillCase", drill, plate, geometry::ShellSide::Outward);

        padSketch = addFixedRectangle("PadSketch", 200.0, 0.0, 100.0, 60.0);
        pad = add(ExtrudeFeature::create("Pad", {.profile = sketchOf(padSketch), .depthParameter = height}));
        std::vector<geometry::EdgeSignature> corners;
        for (const auto& [x, y] : {std::pair{200.0, 0.0}, {300.0, 0.0}, {300.0, 60.0}, {200.0, 60.0}}) {
            corners.push_back(
                geometry::lineSignature(Point3D{x * units::mm, y * units::mm, Length{}}, Direction3D::unitZ()));
        }
        rounded = add(FilletFeature::create(
            "Round", {.target = featureOf(pad), .edges = corners, .radiusParameter = round}));
        roundCup = shellOf("RoundCup", rounded, pad, geometry::ShellSide::Inward);
        roundCase = shellOf("RoundCase", rounded, pad, geometry::ShellSide::Outward);
    }

    /// The L's section as rectangles: the arms, less their overlap, grown
    /// by @p g (negative to shrink) with square corners, from z0 to z1.
    static std::vector<Part> ellParts(double g, double z0, double z1, double sign) {
        return {boxPart({-g, -g, z0}, {100 + g, 30 + g, z1}, sign),
                boxPart({-g, -g, z0}, {40 + g, 80 + g, z1}, sign),
                boxPart({-g, -g, z0}, {40 + g, 30 + g, z1}, -sign)};
    }
    static ExpectedShell ellCupShape(double t) {
        std::vector<Part> parts = ellParts(0.0, 0.0, 50.0, 1.0);
        for (const Part& part : ellParts(-t, t, 50.0, -1.0)) {
            parts.push_back(part);
        }
        return {parts, {0, 0, 0}, {100, 80, 50}};
    }
    static ExpectedShell ellCaseShape(double t) {
        std::vector<Part> parts = ellParts(t, -t, 50.0, 1.0);
        for (const Part& part : ellParts(0.0, 0.0, 50.0, -1.0)) {
            parts.push_back(part);
        }
        return {parts, {-t, -t, -t}, {100 + t, 80 + t, 50}};
    }

    static std::vector<Part> drilledParts(double h, double sign) {
        return {boxPart({0, 0, 0}, {100, 60, h}, sign), zCylinderPart(50, 30, 10, 0, h, -sign)};
    }
    static ExpectedShell drillCupShape(double h, double t) {
        std::vector<Part> parts = drilledParts(h, 1.0);
        parts.push_back(boxPart({t, t, t}, {100 - t, 60 - t, h}, -1.0));
        parts.push_back(zCylinderPart(50, 30, 10 + t, t, h));
        return {parts, {0, 0, 0}, {100, 60, h}};
    }
    static ExpectedShell drillCaseShape(double h, double t) {
        std::vector<Part> parts = drilledParts(h, -1.0);
        parts.push_back(boxPart({-t, -t, -t}, {100 + t, 60 + t, h}));
        parts.push_back(zCylinderPart(50, 30, 10 - t, -t, h, -1.0));
        return {parts, {-t, -t, -t}, {100 + t, 60 + t, h}};
    }
    /// The drilled plate's top rim, inward: the top less the hole and the
    /// cavity's mouth, which the hole's tube does not reach.
    static double drillRim(double t) {
        return (6000.0 - 100.0 * kPi) - ((100.0 - 2.0 * t) * (60.0 - 2.0 * t) - kPi * (10.0 + t) * (10.0 + t));
    }

    static ExpectedShell roundCupShape(double h, double t, double r) {
        return {{roundedPrismPart(250, 30, 100, 60, r, 0, h),
                 roundedPrismPart(250, 30, 100 - 2 * t, 60 - 2 * t, r - t, t, h, -1.0)},
                {200, 0, 0},
                {300, 60, h}};
    }
    static ExpectedShell roundCaseShape(double h, double t, double r) {
        return {{roundedPrismPart(250, 30, 100 + 2 * t, 60 + 2 * t, r + t, -t, h),
                 roundedPrismPart(250, 30, 100, 60, r, 0, h, -1.0)},
                {200 - t, -t, -t},
                {300 + t, 60 + t, h}};
    }
};

} // namespace bettercad::test
