// Performance measurement for P12-PATTERN-001 (evidence, not part of the
// build).
//
// Times one full regeneration of a linear pattern at the instance counts the
// milestone asks for -- 10, 50, 100, 250 and 500 -- for a plain row, a
// symmetric row, a row with half its instances suppressed, and a pattern of
// a pattern whose product is the same count. Every case prints the
// wall-clock time with the result's solid count and its volume against the
// analytic volume, so a fast but wrong result would show.
//
// The seed is a 5 mm cube extruded as a new body and repeated 10 mm apart,
// as in P11-FEAT-005's cubes case, so the plain row's numbers can be
// compared with that baseline directly. A row of N cubes is N separate
// solids of 125 mm^3; suppressing k of them leaves N - k.
//
// Built against the Release libraries of the qualified tree:
//   c++ -std=c++23 -O2 -Iinclude -Ibuild/release/generated/include
//       pattern_instances_timing.cpp -Lbuild/release/lib
//       -lbettercad_features -lbettercad_sketch -lbettercad_geometry
//       -lbettercad_core -L<deps>/lib -lTK... (as for bettercad_geometry)
// See build-and-run.cmd, which does exactly that.
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/LinearPatternFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;

namespace {

/// An unconstrained rectangle sketch in the XY plane.
ObjectId addRectangle(Document& doc, const std::string& name, double widthMm, double lengthMm) {
    auto sketch = std::make_unique<sketch::Sketch>(name);
    const Point2D corners[] = {{0_mm, 0_mm},
                               {widthMm * units::mm, 0_mm},
                               {widthMm * units::mm, lengthMm * units::mm},
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

FeatureId featureId(ObjectId id) {
    return FeatureId::fromValue(id.value());
}

ObjectId addCube(Document& doc) {
    const ObjectId sketch = addRectangle(doc, "Square", 5.0, 5.0);
    auto cube = ExtrudeFeature::create("Cube", {.profile = SketchId::fromValue(sketch.value()), .depth = 5_mm});
    return doc.addObject(std::move(cube.value())).value();
}

ObjectId addPattern(Document& doc, const std::string& name, const LinearPatternDefinition& definition) {
    auto pattern = LinearPatternFeature::create(name, definition);
    return doc.addObject(std::move(pattern.value())).value();
}

void report(const char* kind, unsigned count, Document& doc, ObjectId pattern, unsigned expectedSolids) {
    Regenerator regenerator;
    const auto start = std::chrono::steady_clock::now();
    const auto result = regenerator.regenerate(doc);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const geometry::Body* body = regenerator.body(pattern);
    if (!result || body == nullptr) {
        const Error* error = regenerator.error(pattern);
        std::printf("%-12s N=%4u  FAILED: %s\n", kind, count, error != nullptr ? error->message.c_str() : "?");
        return;
    }
    const double volume = body->massProperties().value().volume.in(units::mm3);
    const double expected = 125.0 * expectedSolids;
    std::printf("%-12s N=%4u  regenerate %8.3f s  solids %4zu  valid %d  V=%.6f expected %.6f (rel err %.1e)\n", kind,
                count, seconds, body->topology().solids, body->isValid() ? 1 : 0, volume, expected,
                std::fabs(volume - expected) / expected);
}

/// N cubes 10 mm apart along X.
void plain(unsigned count) {
    Document doc{"Plain"};
    const ObjectId cube = addCube(doc);
    const ObjectId row = addPattern(
        doc, "Row",
        {.source = featureId(cube), .first = {.direction = {1.0, 0.0, 0.0}, .count = count, .spacing = 10_mm}});
    report("plain", count, doc, row, count);
}

/// The same N cubes, half of them either side of the source. The count is
/// made odd, so the source is the middle instance.
void symmetric(unsigned count) {
    Document doc{"Symmetric"};
    const ObjectId cube = addCube(doc);
    const unsigned odd = count % 2 == 0 ? count - 1 : count;
    const ObjectId row = addPattern(doc, "Row",
                                    {.source = featureId(cube),
                                     .first = {.direction = {1.0, 0.0, 0.0},
                                               .count = odd,
                                               .spacing = 10_mm,
                                               .distribution = PatternDistribution::Spacing,
                                               .symmetric = true}});
    report("symmetric", odd, doc, row, odd);
}

/// N instances with every other copy suppressed: the work should fall with
/// the instances actually built, not with the count.
void suppressed(unsigned count) {
    Document doc{"Suppressed"};
    const ObjectId cube = addCube(doc);
    LinearPatternDefinition definition{
        .source = featureId(cube), .first = {.direction = {1.0, 0.0, 0.0}, .count = count, .spacing = 10_mm}};
    for (unsigned i = 1; i < count; i += 2) {
        definition.suppressed.push_back(i);
    }
    const ObjectId row = addPattern(doc, "Row", definition);
    report("suppressed", count, doc, row, count - static_cast<unsigned>(definition.suppressed.size()));
}

/// A row of @p inner cubes repeated @p outer times along Y: the same
/// inner x outer instances as a flat pattern of that many.
void nested(unsigned inner, unsigned outer) {
    Document doc{"Nested"};
    const ObjectId cube = addCube(doc);
    const ObjectId row = addPattern(
        doc, "Row",
        {.source = featureId(cube), .first = {.direction = {1.0, 0.0, 0.0}, .count = inner, .spacing = 10_mm}});
    const ObjectId grid = addPattern(
        doc, "Grid",
        {.source = featureId(row), .first = {.direction = {0.0, 1.0, 0.0}, .count = outer, .spacing = 10_mm}});
    report("nested", inner * outer, doc, grid, inner * outer);
}

/// The same instances as one pattern with two directions, for comparison.
void grid(unsigned first, unsigned second) {
    Document doc{"Grid"};
    const ObjectId cube = addCube(doc);
    const ObjectId row =
        addPattern(doc, "Row",
                   {.source = featureId(cube),
                    .first = {.direction = {1.0, 0.0, 0.0}, .count = first, .spacing = 10_mm},
                    .second = PatternDirection{.direction = {0.0, 1.0, 0.0}, .count = second, .spacing = 10_mm}});
    report("two-way", first * second, doc, row, first * second);
}

} // namespace

int main() {
    const std::vector<unsigned> counts{10U, 50U, 100U, 250U, 500U};
    for (const unsigned count : counts) {
        plain(count);
    }
    for (const unsigned count : counts) {
        symmetric(count);
    }
    for (const unsigned count : counts) {
        suppressed(count);
    }
    for (const auto [inner, outer] : {std::pair{5U, 2U}, std::pair{10U, 5U}, std::pair{10U, 10U},
                                      std::pair{25U, 10U}, std::pair{25U, 20U}}) {
        nested(inner, outer);
    }
    for (const auto [first, second] : {std::pair{5U, 2U}, std::pair{10U, 5U}, std::pair{10U, 10U},
                                       std::pair{25U, 10U}, std::pair{25U, 20U}}) {
        grid(first, second);
    }
    return 0;
}
