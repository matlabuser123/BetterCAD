// Kernel probe for P11-FEAT-004 (evidence, not part of the build).
//
// For hole-like configurations near the edge of validity, on a 100 x 50 x 20
// mm box, it prints:
//   kernel:  what booleanDifference() of a plain cylinder gives, with no
//            BetterCAD preflight (solids, validity, volume);
//   cutHole: what geometry::cutHole() reports for the same hole.
// One case per process (see the log's header), so a crash would show as the
// process's exit code rather than end the run.
//
// Built against the Release libraries of the qualified tree, e.g.:
//   c++ -std=c++23 -O2 -Iinclude -Ibuild/release/generated/include
//       hole_kernel_probe.cpp -Lbuild/release/lib -lbettercad_geometry
//       -lbettercad_core -L<deps>/lib -lTKDESTEP -lTKXSBase -lTKXCAF -lTKLCAF
//       -lTKMesh -lTKFillet -lTKBool -lTKBO -lTKPrim -lTKTopAlgo -lTKBRep
//       -lTKGeomBase -lTKG2d -lTKG3d -lTKMath -lTKernel
#include <bettercad/core/Error.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/geometry/Booleans.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/geometry/Hole.hpp>
#include <bettercad/core/geometry/Primitives.hpp>

#include <cstdio>
#include <string>
#include <string_view>

using namespace bettercad;
using namespace bettercad::geometry;
using namespace bettercad::literals;

namespace {

struct Case {
    const char* name;
    double x, y;      // axis position, mm
    double top;       // where the cylinder starts (it goes down), mm
    double r, h;      // radius and length, mm
    HoleExtent extent; // the equivalent hole
    double depth;     // blind depth, mm
};

// The block's top face is z = 20; its front face y = 0; its far face z = 0.
constexpr Case kCases[] = {
    {"through", 50, 25, 21, 5, 22, HoleExtent::Through, 0},           // both ends overextended
    {"flush-entry", 50, 25, 20, 5, 10, HoleExtent::Blind, 10},        // entry coplanar with the top face
    {"far-flush", 50, 25, 20.01, 5, 20.01, HoleExtent::Blind, 20},    // bottom coplanar with the far face
    {"far-hair", 50, 25, 20.01, 5, 20.0100001, HoleExtent::Blind, 20.0000001}, // a hair past it
    {"tangent", 50, 5, 21, 5, 22, HoleExtent::Through, 0},            // touches the front face
    {"near-tangent", 50, 5.0000001, 21, 5, 22, HoleExtent::Through, 0},
    {"breakout", 50, 3, 21, 5, 22, HoleExtent::Through, 0},           // breaks through the front face
    {"corner", 0, 0, 21, 5, 22, HoleExtent::Through, 0},              // centred on a vertical edge
    {"outside", 200, 25, 21, 5, 22, HoleExtent::Through, 0},          // misses the box
    {"tiny", 50, 25, 21, 1e-6, 22, HoleExtent::Through, 0},           // 1 nm radius
};

void printBody(const char* label, const Result<Body>& body) {
    if (!body) {
        const std::string_view code = toString(body.error().code);
        std::printf("%s: %.*s: %s", label, static_cast<int>(code.size()), code.data(), body.error().message.c_str());
        return;
    }
    const auto props = body->massProperties();
    std::printf("%s: solids %zu valid %d volume %.12f", label, body->topology().solids, body->isValid() ? 1 : 0,
                props ? props->volume.in(units::mm3) : -1.0);
}

} // namespace

int main(int argc, char** argv) {
    const std::string name = argc > 1 ? argv[1] : "through";
    const auto box = makeBox(100_mm, 50_mm, 20_mm);
    if (!box) {
        std::printf("setup failed\n");
        return 1;
    }
    for (const Case& c : kCases) {
        if (name != c.name) {
            continue;
        }
        const auto tool = makeCylinder(
            Axis3D{Point3D{c.x * units::mm, c.y * units::mm, c.top * units::mm}, Direction3D::unitZ().reversed()},
            c.r * units::mm, c.h * units::mm);
        if (!tool) {
            std::printf("tool: %s\n", tool.error().message.c_str());
            return 1;
        }
        printBody("kernel", booleanDifference(*box, *tool));
        std::printf(" | ");
        const HoleRequest hole{.face = planeSignature(Point3D{0_mm, 0_mm, 20_mm}, Direction3D::unitZ()),
                               .center = Point2D{c.x * units::mm, c.y * units::mm},
                               .extent = c.extent,
                               .diameter = 2.0 * c.r * units::mm,
                               .depth = c.depth * units::mm};
        printBody("cutHole", cutHole(*box, hole));
        std::printf("\n");
        return 0;
    }
    std::printf("no such case '%s'\n", name.c_str());
    return 2;
}
