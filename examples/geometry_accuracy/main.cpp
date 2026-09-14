// Prints geometry-kernel results next to analytic solutions for primitive
// solids and boolean operations. The relative errors in the output are the
// basis for the tolerances used by tests/core/geometry.
#include <bettercad/core/Units.hpp>
#include <bettercad/core/geometry/Booleans.hpp>
#include <bettercad/core/geometry/Kernel.hpp>
#include <bettercad/core/geometry/Primitives.hpp>

#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>

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
    std::printf("%-34s V=%.12g (rel err %.2e, est %.1e)", name, volume,
                relativeError(volume, expectedVolumeMm3), props->volumeRelativeError);
    if (expectedAreaMm2 > 0.0) {
        std::printf("  A=%.12g (rel err %.2e)", area, relativeError(area, expectedAreaMm2));
    }
    std::printf("  valid=%d\n", body->isValid() ? 1 : 0);
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
    return 0;
}
