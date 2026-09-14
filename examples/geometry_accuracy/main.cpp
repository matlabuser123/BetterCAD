// Prints geometry-kernel results next to analytic solutions for primitive
// solids, boolean operations, solids of revolution, chamfers, fillets, holes
// and translated copies. The relative errors in the output are the basis for
// the tolerances used by tests/core/geometry.
#include <bettercad/core/Units.hpp>
#include <bettercad/core/geometry/Booleans.hpp>
#include <bettercad/core/geometry/Chamfer.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/geometry/Fillet.hpp>
#include <bettercad/core/geometry/Hole.hpp>
#include <bettercad/core/geometry/Kernel.hpp>
#include <bettercad/core/geometry/Primitives.hpp>
#include <bettercad/core/geometry/Profile.hpp>
#include <bettercad/core/geometry/Sweeps.hpp>
#include <bettercad/core/geometry/Transform.hpp>

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::geometry;
using namespace bettercad::literals;

namespace {

double relativeError(double actual, double expected) {
    return expected == 0.0 ? std::fabs(actual) : std::fabs(actual - expected) / std::fabs(expected);
}

void report(const char* name, const Result<Body>& body, double expectedVolumeMm3,
            double expectedAreaMm2) {
    if (!body) {
        std::printf("%-34s ERROR: %s\n", name, body.error().message.c_str());
        return;
    }
    const auto props = body->massProperties();
    if (!props) {
        std::printf("%-34s ERROR: %s\n", name, props.error().message.c_str());
        return;
    }
    const double volume = props->volume.in(units::mm3);
    const double area = props->surfaceArea.in(units::mm2);
    std::printf("%-34s V=%.17g expected %.17g (abs err %.2e, rel err %.2e, est %.1e)", name, volume,
                expectedVolumeMm3, std::fabs(volume - expectedVolumeMm3), relativeError(volume, expectedVolumeMm3),
                props->volumeRelativeError);
    if (expectedAreaMm2 > 0.0) {
        std::printf("  A=%.12g (rel err %.2e)", area, relativeError(area, expectedAreaMm2));
    }
    std::printf("  valid=%d solids=%zu\n", body->isValid() ? 1 : 0, body->topology().solids);
}

Point2D mm(double u, double v) {
    return Point2D{u * units::mm, v * units::mm};
}

/// Closed polygon in (radius, height) on the XZ plane.
PlanarRegion polygon(std::initializer_list<std::pair<double, double>> points) {
    const std::vector<std::pair<double, double>> p(points);
    PlanarRegion region{.plane = Frame3D::xz(), .outer = {}, .holes = {}};
    for (std::size_t i = 0; i < p.size(); ++i) {
        region.outer.segments.emplace_back(
            LineSegment2D{mm(p[i].first, p[i].second), mm(p[(i + 1) % p.size()].first, p[(i + 1) % p.size()].second)});
    }
    return region;
}

/// Revolution about the global Z axis, which lies in the XZ plane.
Result<Body> revolve(const PlanarRegion& region, Angle sweep) {
    return makeRevolution(region, Axis3D{Point3D{}, Direction3D::unitZ()}, Angle{}, sweep);
}

} // namespace

int main() {
    constexpr double pi = std::numbers::pi;
    const KernelInfo kernel = geometryKernel();
    std::printf("Kernel: %.*s %.*s\n\n", static_cast<int>(kernel.name.size()), kernel.name.data(),
                static_cast<int>(kernel.version.size()), kernel.version.data());

    report("box 100x50x20 mm", makeBox(100_mm, 50_mm, 20_mm), 100000.0,
           2.0 * (100.0 * 50.0 + 100.0 * 20.0 + 50.0 * 20.0));
    report("cylinder r=10 h=30 mm", makeCylinder(10_mm, 30_mm), pi * 100.0 * 30.0,
           2.0 * pi * 10.0 * (10.0 + 30.0));
    report("sphere r=25 mm", makeSphere(25_mm), 4.0 / 3.0 * pi * std::pow(25.0, 3),
           4.0 * pi * 25.0 * 25.0);
    report("sphere r=0.5 m", makeSphere(0.5_m), 4.0 / 3.0 * pi * std::pow(500.0, 3),
           4.0 * pi * 500.0 * 500.0);

    const auto box = makeBox(100_mm, 50_mm, 20_mm);
    const auto hole = makeCylinder(Axis3D{Point3D{50_mm, 25_mm, -1_mm}}, 10_mm, 22_mm);
    report("box - cylinder (through hole)", booleanDifference(*box, *hole),
           100000.0 - pi * 100.0 * 20.0, 0.0);

    const auto a = makeBox(10_mm, 10_mm, 10_mm);
    const auto b = makeBox(Point3D{5_mm, 0_mm, 0_mm}, 10_mm, 10_mm, 10_mm);
    report("box U box (overlap)", booleanUnion(*a, *b), 1500.0, 0.0);
    report("box n box (overlap)", booleanIntersection(*a, *b), 500.0, 0.0);

    const auto sphere = makeSphere(10_mm);
    const auto octant = makeBox(20_mm, 20_mm, 20_mm);
    report("sphere n octant box", booleanIntersection(*sphere, *octant),
           4.0 / 3.0 * pi * 1000.0 / 8.0, 0.0);

    const auto cx = makeCylinder(Axis3D{Point3D{-20_mm, 0_mm, 0_mm}, Direction3D::unitX()}, 10_mm, 40_mm);
    const auto cy = makeCylinder(Axis3D{Point3D{0_mm, -20_mm, 0_mm}, Direction3D::unitY()}, 10_mm, 40_mm);
    report("bicylinder (Steinmetz)", booleanIntersection(*cx, *cy), 16.0 / 3.0 * 1000.0, 0.0);

    std::printf("\nSolids of revolution about Z (profiles in the XZ plane)\n");
    const PlanarRegion tube = polygon({{10, 0}, {20, 0}, {20, 30}, {10, 30}});
    const double tubeArea = 2 * pi * 20 * 30 + 2 * pi * 10 * 30 + 2 * pi * (400 - 100);
    report("revolve tube r=10..20 h=30", revolve(tube, 360_deg), pi * (400 - 100) * 30, tubeArea);
    report("revolve tube wedge 90 deg", revolve(tube, 90_deg), pi * (400 - 100) * 30 / 4,
           tubeArea / 4 + 2 * 10 * 30);
    report("revolve tube wedge 180 deg", revolve(tube, 180_deg), pi * (400 - 100) * 30 / 2,
           tubeArea / 2 + 2 * 10 * 30);
    report("revolve tube wedge 270 deg", revolve(tube, 270_deg), pi * (400 - 100) * 30 * 3 / 4,
           tubeArea * 3 / 4 + 2 * 10 * 30);
    report("revolve cone r=10 h=20", revolve(polygon({{0, 0}, {10, 0}, {0, 20}}), 360_deg), pi * 100 * 20 / 3.0,
           pi * 10 * std::sqrt(500.0) + pi * 100);
    PlanarRegion halfDisc{.plane = Frame3D::xz(), .outer = {}, .holes = {}};
    halfDisc.outer.segments.emplace_back(ArcSegment2D{mm(0, 0), mm(0, -10), mm(0, 10), true});
    halfDisc.outer.segments.emplace_back(LineSegment2D{mm(0, 10), mm(0, -10)});
    report("revolve sphere r=10", revolve(halfDisc, 360_deg), 4.0 / 3.0 * pi * 1000, 4 * pi * 100);
    PlanarRegion disc{.plane = Frame3D::xz(), .outer = {}, .holes = {}};
    disc.outer.segments.emplace_back(CircleSegment2D{mm(20, 0), 5_mm, true});
    report("revolve torus R=20 a=5", revolve(disc, 360_deg), 2 * pi * pi * 20 * 25, 4 * pi * pi * 20 * 5);
    report("revolve torus wedge 120 deg", revolve(disc, 120_deg), 2 * pi * pi * 20 * 25 / 3, 0.0);

    std::printf("\nChamfers of the box 100x50x20 mm (edges by their supporting lines)\n");
    const EdgeSignature topFront = lineSignature(Point3D{0_mm, 0_mm, 20_mm}, Direction3D::unitX());
    const EdgeSignature topLeft = lineSignature(Point3D{0_mm, 0_mm, 20_mm}, Direction3D::unitY());
    const EdgeSignature bottomBack = lineSignature(Point3D{0_mm, 50_mm, 0_mm}, Direction3D::unitX());
    const auto chamfer = [&](std::vector<EdgeSignature> edges, ChamferMode mode, Length d1, Length d2, Angle angle) {
        ChamferRequest request{.edges = std::move(edges), .mode = mode, .distance = d1};
        if (mode != ChamferMode::EqualDistance) {
            request.distance2 = mode == ChamferMode::TwoDistance ? d2 : Length{};
            request.angle = mode == ChamferMode::DistanceAngle ? angle : Angle{};
            request.referenceSide = Direction3D::unitZ(); // the top face
        }
        return chamferEdges(*box, request);
    };
    // One edge: V = V0 - d^2 L / 2. The top and front faces lose d x L each,
    // the end faces d^2 / 2 each, and the chamfer face adds d sqrt(2) L.
    report("chamfer 1 edge d=5 (L=100)", chamfer({topFront}, ChamferMode::EqualDistance, 5_mm, {}, {}),
           100000.0 - 0.5 * 25.0 * 100.0, 16000.0 - 1000.0 - 25.0 + 500.0 * std::sqrt(2.0));
    report("chamfer 1 edge d=0.5 (L=100)", chamfer({topFront}, ChamferMode::EqualDistance, 0.5_mm, {}, {}),
           100000.0 - 0.5 * 0.25 * 100.0, 16000.0 - 100.0 - 0.25 + 50.0 * std::sqrt(2.0));
    report("chamfer 2 separate edges d=5", chamfer({topFront, bottomBack}, ChamferMode::EqualDistance, 5_mm, {}, {}),
           100000.0 - 2 * 0.5 * 25.0 * 100.0, 0.0);
    // Two edges meeting at a corner: two prisms overlapping in d^3 / 3.
    report("chamfer 2 adjacent edges d=5", chamfer({topFront, topLeft}, ChamferMode::EqualDistance, 5_mm, {}, {}),
           100000.0 - 0.5 * 25.0 * (100.0 + 50.0) + 125.0 / 3.0, 0.0);
    report("chamfer two distances 5/3", chamfer({topFront}, ChamferMode::TwoDistance, 5_mm, 3_mm, {}),
           100000.0 - 0.5 * 5.0 * 3.0 * 100.0, 0.0);
    report("chamfer distance 5 angle 30 deg", chamfer({topFront}, ChamferMode::DistanceAngle, 5_mm, {}, 30_deg),
           100000.0 - 0.5 * 5.0 * 5.0 * std::tan(pi / 6.0) * 100.0, 0.0);
    // The top rim of a cylinder r=15 h=40: by Pappus, V = V0 - pi d^2 (r - d/3).
    const auto rod = makeCylinder(15_mm, 40_mm);
    const auto rim = circleSignature(Point3D{0_mm, 0_mm, 40_mm}, Direction3D::unitZ(), 15_mm);
    report("chamfer cylinder rim d=2", chamferEdges(*rod, {.edges = {*rim}, .distance = 2_mm}),
           pi * 225.0 * 40.0 - pi * 4.0 * (15.0 - 2.0 / 3.0), 0.0);

    std::printf("\nConstant-radius fillets (same box and cylinder)\n");
    // A fillet of radius r takes r^2 (1 - pi/4) out of a square corner per
    // unit length; that area's centroid lies r (10 - 3 pi) / (3 (4 - pi))
    // from the corner along either face.
    const auto corner = [&](double r) { return r * r * (1.0 - pi / 4.0); };
    const auto centroid = [&](double r) { return r * (10.0 - 3.0 * pi) / (3.0 * (4.0 - pi)); };
    const EdgeSignature frontLeft = lineSignature(Point3D{0_mm, 0_mm, 0_mm}, Direction3D::unitZ());
    const auto fillet = [&](std::vector<EdgeSignature> edges, Length r) {
        return filletEdges(*box, {.edges = std::move(edges), .radius = r});
    };
    // One edge: the top and front faces lose r x L, the end faces a corner
    // area each, and the quarter cylinder adds (pi r / 2) L.
    report("fillet 1 edge r=5 (L=100)", fillet({topFront}, 5_mm), 100000.0 - 100.0 * corner(5),
           16000.0 - 1000.0 - 2.0 * corner(5) + pi * 5.0 / 2.0 * 100.0);
    report("fillet 1 edge r=0.5 (L=100)", fillet({topFront}, 0.5_mm), 100000.0 - 100.0 * corner(0.5),
           16000.0 - 100.0 - 2.0 * corner(0.5) + pi * 0.5 / 2.0 * 100.0);
    report("fillet 2 separate edges r=5", fillet({topFront, bottomBack}, 5_mm), 100000.0 - 200.0 * corner(5), 0.0);
    // Two edges at a vertex: the union of the two corner regions, which
    // overlap in r^3 (5/3 - pi/2).
    report("fillet 2 adjacent edges r=5", fillet({topFront, topLeft}, 5_mm),
           100000.0 - corner(5) * 150.0 + 125.0 * (5.0 / 3.0 - pi / 2.0), 0.0);
    // Three edges at a vertex: a spherical corner; the corner cube keeps an
    // eighth of a ball.
    report("fillet 3 edges at a vertex r=5", fillet({topFront, topLeft, frontLeft}, 5_mm),
           100000.0 - corner(5) * (170.0 - 15.0) - 125.0 * (1.0 - pi / 6.0), 0.0);
    // The inner edge of an L (50 x 50 less a 30 x 30 corner, 20 high) is
    // concave: the fillet adds the corner area.
    PlanarRegion l{.plane = Frame3D::xy(), .outer = {}, .holes = {}};
    const std::pair<double, double> lCorners[] = {{0, 0}, {50, 0}, {50, 20}, {20, 20}, {20, 50}, {0, 50}};
    for (std::size_t i = 0; i < 6; ++i) {
        const auto& [x0, y0] = lCorners[i];
        const auto& [x1, y1] = lCorners[(i + 1) % 6];
        l.outer.segments.emplace_back(LineSegment2D{Point2D{x0 * units::mm, y0 * units::mm},
                                                    Point2D{x1 * units::mm, y1 * units::mm}});
    }
    const auto lPrism = makePrism(l, 0_mm, 20_mm);
    const EdgeSignature inner = lineSignature(Point3D{20_mm, 20_mm, 0_mm}, Direction3D::unitZ());
    report("fillet concave L edge r=5", filletEdges(*lPrism, {.edges = {inner}, .radius = 5_mm}),
           32000.0 + corner(5) * 20.0, 0.0);
    // The top rim of the cylinder: by Pappus, V = V0 - 2 pi (R - centroid) A.
    report("fillet cylinder rim r=2", filletEdges(*rod, {.edges = {*rim}, .radius = 2_mm}),
           pi * 225.0 * 40.0 - 2.0 * pi * (15.0 - centroid(2)) * corner(2),
           2.0 * pi * 15.0 * 38.0 + pi * 225.0 + pi * 13.0 * 13.0 + pi * 2.0 / 2.0 * 2.0 * pi * (13.0 + 4.0 / pi));

    std::printf("\nHoles (faces by their planes; the box 100x50x20 mm, centre (50, 25))\n");
    const FaceSignature top = planeSignature(Point3D{0_mm, 0_mm, 20_mm}, Direction3D::unitZ());
    const FaceSignature bottom = planeSignature(Point3D{}, Direction3D::unitZ().reversed());
    const HoleRequest simple{.face = top, .center = mm(50, 25), .diameter = 10_mm};
    // Through: V = L W H - pi r^2 H; the top and bottom lose pi r^2 each, the
    // bore adds 2 pi r H.
    report("hole through d=10", cutHole(*box, simple), 100000.0 - pi * 25.0 * 20.0, 16000.0 + 150.0 * pi);
    HoleRequest blind = simple;
    blind.extent = HoleExtent::Blind;
    blind.depth = 8_mm;
    report("hole blind d=10 h=8", cutHole(*box, blind), 100000.0 - pi * 25.0 * 8.0, 16000.0 + 80.0 * pi);
    HoleRequest fromBelow = blind;
    fromBelow.face = bottom;
    report("hole blind from the bottom face", cutHole(*box, fromBelow), 100000.0 - pi * 25.0 * 8.0, 16000.0 + 80.0 * pi);
    const auto thick = makeBox(100_mm, 50_mm, 40_mm);
    HoleRequest throughBelow = simple;
    throughBelow.face = bottom;
    report("hole through from below, H=40", cutHole(*thick, throughBelow), 200000.0 - pi * 25.0 * 40.0,
           2.0 * (5000.0 + 4000.0 + 2000.0) + 350.0 * pi);
    // Counterbore D=18 h=5: V = V0 - pi r^2 H - pi (R^2 - r^2) h. The top
    // loses pi R^2, the bottom pi r^2; the walls 2 pi R h and 2 pi r (H - h),
    // the shelf pi (R^2 - r^2).
    HoleRequest bored = simple;
    bored.type = HoleType::Counterbore;
    bored.counterboreDiameter = 18_mm;
    bored.counterboreDepth = 5_mm;
    report("hole counterbore 18x5", cutHole(*box, bored), 100000.0 - pi * 25.0 * 20.0 - pi * 56.0 * 5.0,
           16000.0 + (-81.0 - 25.0 + 90.0 + 150.0 + 56.0) * pi);
    // Countersink D=20: the cone from R=10 to r=5 over h = (R - r) / tan(a/2)
    // removes the frustum pi h (R^2 + R r + r^2) / 3 less pi r^2 h beyond the
    // bore; its side is pi (R + r) sqrt((R - r)^2 + h^2).
    for (const double angle : {90.0, 82.0}) {
        const double h = 5.0 / std::tan(angle * pi / 360.0);
        HoleRequest sunk = simple;
        sunk.type = HoleType::Countersink;
        sunk.countersinkDiameter = 20_mm;
        sunk.countersinkAngle = angle * units::deg;
        const std::string name = angle == 90.0 ? "hole countersink 20 at 90 deg" : "hole countersink 20 at 82 deg";
        report(name.c_str(), cutHole(*box, sunk),
               100000.0 - pi * 25.0 * 20.0 - (pi * h * 175.0 / 3.0 - pi * 25.0 * h),
               16000.0 - 100.0 * pi - 25.0 * pi + 2.0 * pi * 5.0 * (20.0 - h) + pi * 15.0 * std::sqrt(25.0 + h * h));
    }
    // An axial blind bore in the end face of a turned cylinder R=15 h=40.
    const auto turned = revolve(polygon({{0, 0}, {15, 0}, {15, 40}, {0, 40}}), 360_deg);
    report("hole in revolved end face d=10 h=20",
           cutHole(*turned, {.face = planeSignature(Point3D{0_mm, 0_mm, 40_mm}, Direction3D::unitZ()),
                             .center = mm(0, 0),
                             .extent = HoleExtent::Blind,
                             .diameter = 10_mm,
                             .depth = 20_mm}),
           pi * 225.0 * 40.0 - pi * 25.0 * 20.0, 2.0 * pi * 15.0 * 40.0 + 2.0 * pi * 225.0 + 2.0 * pi * 5.0 * 20.0);

    std::printf("\nTranslated copies (linear patterns: instance k moved by k s along X)\n");
    const auto cube = makeBox(10_mm, 10_mm, 10_mm);
    const auto row = [&](Length spacing, int count) -> Result<Body> {
        Body result = *cube;
        for (int k = 1; k < count; ++k) {
            auto copy = translated(*cube, Translation3D::along(Direction3D::unitX(), spacing * static_cast<double>(k)));
            if (!copy) {
                return copy;
            }
            auto united = booleanUnion(result, *copy);
            if (!united) {
                return united;
            }
            result = *united;
        }
        return result;
    };
    // Disjoint: count x V; touching: one 40 x 10 x 10 bar; overlapping: the union.
    report("4 cubes 10 mm, 20 mm apart", row(20_mm, 4), 4000.0, 2400.0);
    report("4 cubes 10 mm, 10 mm apart (touching)", row(10_mm, 4), 4000.0, 1800.0);
    report("4 cubes 10 mm, 5 mm apart (overlap)", row(5_mm, 4), 2500.0, 2.0 * (250.0 + 250.0 + 100.0));
    // Five through holes: the plate 120 x 50 x 20 less five translated
    // cylinders r = 5: V = V0 - 5 pi r^2 H; each hole adds 2 pi r H - 2 pi r^2.
    const auto plate = makeBox(120_mm, 50_mm, 20_mm);
    const auto bore = makeCylinder(Axis3D{Point3D{20_mm, 25_mm, -1_mm}}, 5_mm, 22_mm);
    Result<Body> drilled = *plate;
    for (int k = 0; k < 5 && drilled; ++k) {
        auto moved = translated(*bore, Translation3D::along(Direction3D::unitX(), 20_mm * static_cast<double>(k)));
        drilled = moved ? booleanDifference(*drilled, *moved) : moved;
    }
    report("5 through holes 20 mm apart", drilled, 120000.0 - 5.0 * pi * 25.0 * 20.0,
           18800.0 + 5.0 * (2.0 * pi * 5.0 * 20.0 - 2.0 * pi * 25.0));
    return 0;
}
