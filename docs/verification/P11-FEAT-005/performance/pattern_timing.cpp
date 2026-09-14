// Performance sanity check for P11-FEAT-005 (evidence, not part of the build).
//
// Builds linear patterns of 10, 50, 100 and 200 instances through the public
// feature API (document, sketch, extrude, hole, linear pattern, regenerator)
// and prints the wall-clock time of one full regeneration together with the
// result's solid count and volume against the analytic volume, so a fast but
// wrong result would show. One run per case, in one process.
//
//   holes: a plate (10 N + 10) x 50 x 10 mm, a 6 mm through hole at (10, 25),
//          patterned N times 10 mm apart along X: V = V0 - N pi 3^2 10.
//   cubes: a 5 mm cube (new body), patterned N times 10 mm apart along X:
//          N separate solids, V = N 125.
//
// Built against the Release libraries of the qualified tree, e.g.:
//   c++ -std=c++23 -O2 -Iinclude -Ibuild/release/generated/include
//       pattern_timing.cpp -Lbuild/release/lib -lbettercad_features
//       -lbettercad_sketch -lbettercad_geometry -lbettercad_core
//       -L<deps>/lib -lTK... (as for bettercad_geometry)
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/LinearPatternFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <numbers>
#include <string>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;

namespace {

/// An unconstrained rectangle sketch in the XY plane.
ObjectId addRectangle(Document& doc, const std::string& name, double widthMm, double lengthMm) {
    auto sketch = std::make_unique<sketch::Sketch>(name);
    const Point2D corners[] = {{0_mm, 0_mm}, {widthMm * units::mm, 0_mm}, {widthMm * units::mm, lengthMm * units::mm},
                               {0_mm, lengthMm * units::mm}};
    EntityId points[4];
    for (int i = 0; i < 4; ++i) {
        points[i] = sketch->addPoint(corners[i]).value();
    }
    for (int i = 0; i < 4; ++i) {
        (void)sketch->addLine(points[i], points[(i + 1) % 4]).value();
    }
    return doc.addObject(std::move(sketch)).value();
}

template <typename F>
ObjectId add(Document& doc, const std::string& name, const typename F::Definition& definition) {
    return doc.addObject(F::create(name, definition).value()).value();
}

FeatureId featureId(ObjectId id) {
    return FeatureId::fromValue(id.value());
}

void report(const char* kind, unsigned count, Document& doc, ObjectId pattern, double expectedMm3) {
    Regenerator regenerator;
    const auto start = std::chrono::steady_clock::now();
    const auto result = regenerator.regenerate(doc);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const geometry::Body* body = regenerator.body(pattern);
    if (!result || body == nullptr) {
        const Error* error = regenerator.error(pattern);
        std::printf("%-6s N=%4u  FAILED: %s\n", kind, count, error != nullptr ? error->message.c_str() : "?");
        return;
    }
    const double volume = body->massProperties().value().volume.in(units::mm3);
    std::printf("%-6s N=%4u  regenerate %8.3f s  solids %4zu  valid %d  V=%.6f expected %.6f (rel err %.1e)\n", kind,
                count, seconds, body->topology().solids, body->isValid() ? 1 : 0, volume, expectedMm3,
                std::fabs(volume - expectedMm3) / expectedMm3);
}

void holes(unsigned count) {
    Document doc{"Holes"};
    const double length = 10.0 * count + 10.0;
    const ObjectId sketch = addRectangle(doc, "Plate", length, 50.0);
    const ObjectId plate =
        add<ExtrudeFeature>(doc, "Pad", {.profile = SketchId::fromValue(sketch.value()), .depth = 10_mm});
    const ObjectId drill = add<HoleFeature>(
        doc, "Drill",
        {.target = featureId(plate),
         .face = geometry::planeSignature(Point3D{0_mm, 0_mm, 10_mm}, Direction3D::unitZ()),
         .center = Point2D{10_mm, 25_mm},
         .diameter = 6_mm});
    const ObjectId pattern = add<LinearPatternFeature>(
        doc, "Row", {.source = featureId(drill), .first = {.direction = {1.0, 0.0, 0.0}, .count = count, .spacing = 10_mm}});
    report("holes", count, doc, pattern, length * 50.0 * 10.0 - count * std::numbers::pi * 9.0 * 10.0);
}

void cubes(unsigned count) {
    Document doc{"Cubes"};
    const ObjectId sketch = addRectangle(doc, "Square", 5.0, 5.0);
    const ObjectId cube = add<ExtrudeFeature>(doc, "Cube", {.profile = SketchId::fromValue(sketch.value()), .depth = 5_mm});
    const ObjectId pattern = add<LinearPatternFeature>(
        doc, "Row", {.source = featureId(cube), .first = {.direction = {1.0, 0.0, 0.0}, .count = count, .spacing = 10_mm}});
    report("cubes", count, doc, pattern, 125.0 * count);
}

} // namespace

int main() {
    for (const unsigned count : {10U, 50U, 100U, 200U}) {
        holes(count);
    }
    for (const unsigned count : {10U, 50U, 100U, 200U}) {
        cubes(count);
    }
    return 0;
}
