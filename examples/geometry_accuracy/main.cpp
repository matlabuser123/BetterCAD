// Prints geometry-kernel results next to analytic solutions for primitive
// solids, boolean operations and solids of revolution. The relative errors in
// the output are the basis for the tolerances used by tests/core/geometry.
#include <bettercad/core/Units.hpp>
#include <bettercad/core/geometry/Booleans.hpp>
#include <bettercad/core/geometry/Kernel.hpp>
#include <bettercad/core/geometry/Primitives.hpp>
#include <bettercad/core/geometry/Profile.hpp>
#include <bettercad/core/geometry/Sweeps.hpp>

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
    return 0;
}
