#pragma once

#include "features/FeatureTestSupport.hpp"
#include "support/ShellModels.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Profile.hpp>
#include <bettercad/features/Datums.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/RibFeature.hpp>
#include <bettercad/features/ShellFeature.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bettercad::test {

// P12-FEAT-005 reference models: ribs whose volumes are the areas they fill
// times their thickness, the areas written out (triangles, a square less a
// quarter disc, and Green's theorem for a spline). Every expected value is
// written out from the parameters; none is read from a result.

/// A sketch of fixed points joined by lines (an open chain); @p lines
/// receives the lines in order.
inline std::unique_ptr<sketch::Sketch> fixedChain(const std::string& name,
                                                  const std::vector<std::pair<double, double>>& points,
                                                  std::vector<EntityId>& lines) {
    auto sketch = std::make_unique<sketch::Sketch>(name);
    std::vector<EntityId> ids;
    for (const auto& [u, v] : points) {
        ids.push_back(require(sketch->addPoint(Point2D{u * units::mm, v * units::mm})));
        require(sketch->addFixed(ids.back()));
    }
    lines.clear();
    for (std::size_t i = 0; i + 1 < ids.size(); ++i) {
        lines.push_back(require(sketch->addLine(ids[i], ids[i + 1])));
    }
    return sketch;
}

// An L bracket with ribs across its inside corner:
//
//   floor         10 mm
//   wall          10 mm
//   mid           20 mm
//   thickness     4 mm
//   FloorSketch   XY plane: rectangle x 0..80, y 0..floor
//   Floor         extrude 40 mm (new body)
//   WallSketch    XY plane: rectangle x 0..wall, y 0..60
//   Bracket       extrude 40 mm, joined to Floor
//   Mid           datum plane: the model's XY plane moved up by mid
//   RibSketch     on Mid: the line (40, 10) to (10, 40)
//   Rib           rib on Bracket along that line, `thickness` thick,
//                 symmetric: it fills the corner side (the left of the
//                 line's direction)
//
// The line lies on x + y = 50, so the rib is the triangle between it and the
// corner (wall, floor): legs 50 - wall - floor, whatever the walls are, from
// mid - t/2 to mid + t/2. IDs: floor 1, wall 2, mid 3, thickness 4,
// FloorSketch 5, Floor 6, WallSketch 7, Bracket 8, Mid 9, RibSketch 10,
// Rib 11.
struct RibbedBracketModel : FaceKindModel {
    static constexpr std::string_view kDocumentId = "2b8e6c14-7a93-4d5f-9e01-c3a7f5b8d264";
    ParameterId floor, wall, mid, thickness;
    ObjectId floorSketch, floorBody, wallSketch, bracket, midPlane, ribSketch, rib;
    std::vector<EntityId> ribLines;
    /// The rectangles' lines: y = y0, x = x1, y = y1, x = x0.
    std::array<EntityId, 4> floorLines{};
    std::array<EntityId, 4> wallLines{};

    RibbedBracketModel() : FaceKindModel(kDocumentId, "RibbedBracket") {
        using namespace bettercad::literals;
        using namespace bettercad::features;
        floor = doc.createParameter("floor", 10_mm, units::mm).value();
        wall = doc.createParameter("wall", 10_mm, units::mm).value();
        mid = doc.createParameter("mid", 20_mm, units::mm).value();
        thickness = doc.createParameter("thickness", 4_mm, units::mm).value();
        auto floorProfile = std::make_unique<sketch::Sketch>("FloorSketch");
        floorLines = addSizedRectangle(*floorProfile, 0.0, 0.0, 80.0, 10.0, std::nullopt, floor);
        floorSketch = doc.addObject(std::move(floorProfile)).value();
        floorBody = add(ExtrudeFeature::create("Floor", {.profile = sketchOf(floorSketch), .depth = 40_mm}));
        auto wallProfile = std::make_unique<sketch::Sketch>("WallSketch");
        wallLines = addSizedRectangle(*wallProfile, 0.0, 0.0, 10.0, 60.0, wall, std::nullopt);
        wallSketch = doc.addObject(std::move(wallProfile)).value();
        bracket = add(ExtrudeFeature::create("Bracket", {.profile = sketchOf(wallSketch),
                                                         .depth = 40_mm,
                                                         .operation = FeatureOperation::Join,
                                                         .target = featureOf(floorBody)}));
        midPlane = add(DatumPlane::create(
            "Mid", {.kind = DatumPlaneKind::Offset, .base = {.plane = PrincipalPlane::XY}, .offsetParameter = mid}));
        auto line = fixedChain("RibSketch", {{40, 10}, {10, 40}}, ribLines);
        REQUIRE(line->setAttachment(PlaneReference{.object = midPlane}).has_value());
        ribSketch = doc.addObject(std::move(line)).value();
        rib = add(RibFeature::create("Rib", {.target = featureOf(bracket),
                                             .profile = sketchOf(ribSketch),
                                             .edges = ribLines,
                                             .thicknessParameter = thickness}));
    }

    static double bracketVolume(double f, double w) { return 40.0 * (80.0 * f + w * (60.0 - f)); }
    static std::vector<Part> bracketParts(double f, double w) {
        return {boxPart({0, 0, 0}, {80, f, 40}), boxPart({0, f, 0}, {w, 60, 40})};
    }
    /// The rib's triangle legs.
    static double leg(double f, double w) { return 50.0 - w - f; }
    static Part ribPart(double f, double w, double zLow, double zHigh) {
        const double l = leg(f, w);
        return {l * l / 2.0 * (zHigh - zLow), {w + l / 3.0, f + l / 3.0, (zLow + zHigh) / 2.0}};
    }
    static ExpectedShell shape(double f, double w, double zLow, double zHigh) {
        std::vector<Part> parts = bracketParts(f, w);
        parts.push_back(ribPart(f, w, zLow, zHigh));
        return {parts, {0, 0, 0}, {80, 60, 40}};
    }
};

// Ribs of other profiles on a fixed bracket (floor and wall 10 mm), each on
// the plane z = 20 by 4 mm:
//
//   FloorSketch, Floor, WallSketch, Bracket   as above, fixed
//   Mid           datum plane z = 20
//   ArcSketch     on Mid: the arc about (40, 40) from (10, 40) to (40, 10)
//                 (counter-clockwise), tangent to both walls
//   ArcRib        rib along it, flipped: the corner lies to its right
//   ChainSketch   on Mid: lines (40, 10) (20, 20) (10, 40)
//   ChainRib      rib along the two lines
//   SplineSketch  on Mid: a cubic spline through the poles (40, 10) (30, 14)
//                 (22, 22) (14, 30) (10, 40)
//   SplineRib     rib along it
//   ShortSketch   on Mid: the line (30, 20) to (20, 30), short of the walls
//   ShortRib      rib along it: extended to the walls, the corner triangle
//   AlongRib      the chain rib, all its thickness along the plane's normal
//
// IDs: FloorSketch 1, Floor 2, WallSketch 3, Bracket 4, Mid 5, ArcSketch 6,
// ArcRib 7, ChainSketch 8, ChainRib 9, SplineSketch 10, SplineRib 11,
// ShortSketch 12, ShortRib 13, AlongRib 14.
struct RibProfilesModel : FaceKindModel {
    static constexpr std::string_view kDocumentId = "f41a7d93-0c26-4b8e-a5d7-6e2c9b1f8a30";
    ObjectId floorSketch, floorBody, wallSketch, bracket, midPlane, arcSketch, arcRib, chainSketch, chainRib,
        splineSketch, splineRib, shortSketch, shortRib, alongRib;
    EntityId arc, spline;
    std::vector<EntityId> chainLines, shortLines;

    RibProfilesModel() : FaceKindModel(kDocumentId, "RibProfiles") {
        using namespace bettercad::literals;
        using namespace bettercad::features;
        floorSketch = addFixedRectangle("FloorSketch", 0.0, 0.0, 80.0, 10.0);
        floorBody = add(ExtrudeFeature::create("Floor", {.profile = sketchOf(floorSketch), .depth = 40_mm}));
        wallSketch = addFixedRectangle("WallSketch", 0.0, 10.0, 10.0, 50.0);
        bracket = add(ExtrudeFeature::create("Bracket", {.profile = sketchOf(wallSketch),
                                                         .depth = 40_mm,
                                                         .operation = FeatureOperation::Join,
                                                         .target = featureOf(floorBody)}));
        midPlane = add(DatumPlane::create(
            "Mid", {.kind = DatumPlaneKind::Offset, .base = {.plane = PrincipalPlane::XY}, .offset = 20_mm}));
        const PlaneReference onMid{.object = midPlane};

        auto arcProfile = std::make_unique<sketch::Sketch>("ArcSketch");
        std::vector<EntityId> arcPoints;
        for (const auto& [u, v] : {std::pair{40.0, 40.0}, {10.0, 40.0}, {40.0, 10.0}}) {
            arcPoints.push_back(require(arcProfile->addPoint(Point2D{u * units::mm, v * units::mm})));
            require(arcProfile->addFixed(arcPoints.back()));
        }
        arc = require(arcProfile->addArc(arcPoints[0], arcPoints[1], arcPoints[2]));
        REQUIRE(arcProfile->setAttachment(onMid).has_value());
        arcSketch = doc.addObject(std::move(arcProfile)).value();
        arcRib = add(RibFeature::create("ArcRib", {.target = featureOf(bracket),
                                                   .profile = sketchOf(arcSketch),
                                                   .edges = {arc},
                                                   .thickness = 4_mm,
                                                   .flipped = true}));

        auto chain = fixedChain("ChainSketch", {{40, 10}, {20, 20}, {10, 40}}, chainLines);
        REQUIRE(chain->setAttachment(onMid).has_value());
        chainSketch = doc.addObject(std::move(chain)).value();
        chainRib = add(RibFeature::create("ChainRib", {.target = featureOf(bracket),
                                                       .profile = sketchOf(chainSketch),
                                                       .edges = chainLines,
                                                       .thickness = 4_mm}));

        auto splineProfile = std::make_unique<sketch::Sketch>("SplineSketch");
        spline = require(splineProfile->addSpline(splinePoles()));
        for (const EntityId pole : std::get<sketch::SplineEntity>(splineProfile->findEntity(spline)->geometry).poles) {
            require(splineProfile->addFixed(pole));
        }
        REQUIRE(splineProfile->setAttachment(onMid).has_value());
        splineSketch = doc.addObject(std::move(splineProfile)).value();
        splineRib = add(RibFeature::create("SplineRib", {.target = featureOf(bracket),
                                                         .profile = sketchOf(splineSketch),
                                                         .edges = {spline},
                                                         .thickness = 4_mm}));

        auto shortLine = fixedChain("ShortSketch", {{30, 20}, {20, 30}}, shortLines);
        REQUIRE(shortLine->setAttachment(onMid).has_value());
        shortSketch = doc.addObject(std::move(shortLine)).value();
        shortRib = add(RibFeature::create("ShortRib", {.target = featureOf(bracket),
                                                       .profile = sketchOf(shortSketch),
                                                       .edges = shortLines,
                                                       .thickness = 4_mm}));
        alongRib = add(RibFeature::create("AlongRib", {.target = featureOf(bracket),
                                                       .profile = sketchOf(chainSketch),
                                                       .edges = chainLines,
                                                       .thickness = 4_mm,
                                                       .placement = geometry::RibPlacement::AlongNormal}));
    }

    static std::vector<Point2D> splinePoles() {
        std::vector<Point2D> poles;
        for (const auto& [u, v] : {std::pair{40.0, 10.0}, {30.0, 14.0}, {22.0, 22.0}, {14.0, 30.0}, {10.0, 40.0}}) {
            poles.push_back(Point2D{u * units::mm, v * units::mm});
        }
        return poles;
    }

    static constexpr double kBracket = 40.0 * (800.0 + 500.0);
    static std::vector<Part> bracketParts() {
        return {boxPart({0, 0, 0}, {80, 10, 40}), boxPart({0, 10, 0}, {10, 60, 40})};
    }
    /// The bracket with a rib of @p area centred at (x, y) from z0 to z1.
    static ExpectedShell with(double area, double x, double y, double z0 = 18.0, double z1 = 22.0) {
        std::vector<Part> parts = bracketParts();
        parts.push_back({area * (z1 - z0), {x, y, (z0 + z1) / 2.0}});
        return {parts, {0, 0, 0}, {80, 60, 40}};
    }
    /// The square (10..40)^2 less the quarter disc about (40, 40): its area
    /// and centroid (by symmetry on x = y).
    static double arcArea() { return 900.0 - 225.0 * kPi; }
    static double arcCentroid() {
        // The square's moment less the quarter disc's (its centroid lies
        // 4 r / (3 pi) from (40, 40) towards the corner along each axis).
        const double disc = 225.0 * kPi;
        return (900.0 * 25.0 - disc * (40.0 - 40.0 / kPi)) / arcArea();
    }
    /// The two triangles between the chain and the corner.
    static double chainArea() { return 150.0 + 150.0; }
    static std::pair<double, double> chainCentroid() {
        // Triangles (10, 10) (40, 10) (20, 20) and (10, 10) (20, 20) (10, 40).
        const double x = (150.0 * (70.0 / 3.0) + 150.0 * (40.0 / 3.0)) / 300.0;
        const double y = (150.0 * (40.0 / 3.0) + 150.0 * (70.0 / 3.0)) / 300.0;
        return {x, y};
    }
    /// The region between the spline and the corner, by Green's theorem.
    static geometry::PlanarRegion splineRegion() {
        using geometry::LineSegment2D;
        const auto mm = [](double u, double v) { return Point2D{u * units::mm, v * units::mm}; };
        geometry::ProfileLoop loop;
        loop.segments = {geometry::SplineSegment2D{splinePoles(), 3, false}, LineSegment2D{mm(10, 40), mm(10, 10)},
                         LineSegment2D{mm(10, 10), mm(40, 10)}};
        return {.plane = Frame3D::xy(), .outer = loop, .holes = {}};
    }
};

} // namespace bettercad::test
