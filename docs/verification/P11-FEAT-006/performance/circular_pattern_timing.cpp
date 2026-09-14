// Performance sanity check for P11-FEAT-006 (evidence, not part of the build).
//
// Builds circular patterns of 10, 50 and 100 instances through the public
// feature API (document, sketch, extrude, hole, circular pattern,
// regenerator) and prints the wall-clock time of one full regeneration
// together with the result's solid count and volume against the analytic
// volume, so a fast but wrong result would show. One run per case, in one
// process. The sizes match the linear pattern's timing (P11-FEAT-005): the
// instances are about 10 mm apart along the bolt circle, whose radius is
// r = 10 N / (2 pi).
//
//   holes: a disc R = r + 10 mm, 10 mm thick, a 6 mm through hole at (r, 0),
//          patterned N times around Z: V = pi R^2 10 - N pi 3^2 10.
//   cubes: a 5 mm cube (new body) with a corner at (r, 0), patterned N times
//          around Z: N separate solids, V = N 125.
//
// Built against the Release libraries of the qualified tree, e.g.:
//   c++ -std=c++23 -O2 -Iinclude -Ibuild/release/generated/include
//       circular_pattern_timing.cpp -Lbuild/release/lib -lbettercad_features
//       -lbettercad_sketch -lbettercad_geometry -lbettercad_core
//       -L<deps>/lib -lTK... (as for bettercad_geometry)
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/features/CircularPatternFeature.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>
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

constexpr double pi = std::numbers::pi;

/// An unconstrained square sketch in the XY plane with a corner at (x, 0).
ObjectId addSquare(Document& doc, const std::string& name, double xMm, double sizeMm) {
    auto sketch = std::make_unique<sketch::Sketch>(name);
    const Point2D corners[] = {{xMm * units::mm, 0_mm},
                               {(xMm + sizeMm) * units::mm, 0_mm},
                               {(xMm + sizeMm) * units::mm, sizeMm * units::mm},
                               {xMm * units::mm, sizeMm * units::mm}};
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

double boltCircleMm(unsigned count) {
    return 10.0 * count / (2.0 * pi);
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
    const double r = boltCircleMm(count);
    const double radius = r + 10.0;
    auto circle = std::make_unique<sketch::Sketch>("Disc");
    (void)circle->addCircle(Point2D{}, radius * units::mm).value();
    const ObjectId sketch = doc.addObject(std::move(circle)).value();
    const ObjectId disc =
        add<ExtrudeFeature>(doc, "Flange", {.profile = SketchId::fromValue(sketch.value()), .depth = 10_mm});
    const ObjectId drill = add<HoleFeature>(
        doc, "Bolt",
        {.target = featureId(disc),
         .face = geometry::planeSignature(Point3D{0_mm, 0_mm, 10_mm}, Direction3D::unitZ()),
         .center = Point2D{r * units::mm, 0_mm},
         .diameter = 6_mm});
    const ObjectId pattern = add<CircularPatternFeature>(
        doc, "Bolts", {.source = featureId(drill), .axis = {.origin = Point3D{}, .direction = {0.0, 0.0, 1.0}},
                       .count = count});
    report("holes", count, doc, pattern, pi * radius * radius * 10.0 - count * pi * 9.0 * 10.0);
}

void cubes(unsigned count) {
    Document doc{"Cubes"};
    const ObjectId sketch = addSquare(doc, "Square", boltCircleMm(count), 5.0);
    const ObjectId cube = add<ExtrudeFeature>(doc, "Cube", {.profile = SketchId::fromValue(sketch.value()), .depth = 5_mm});
    const ObjectId pattern = add<CircularPatternFeature>(
        doc, "Ring", {.source = featureId(cube), .axis = {.origin = Point3D{}, .direction = {0.0, 0.0, 1.0}},
                      .count = count});
    report("cubes", count, doc, pattern, 125.0 * count);
}

} // namespace

int main() {
    for (const unsigned count : {10U, 50U, 100U}) {
        holes(count);
    }
    for (const unsigned count : {10U, 50U, 100U}) {
        cubes(count);
    }
    return 0;
}
