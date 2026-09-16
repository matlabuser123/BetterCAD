// Minimal reproducer (evidence, not part of the build) for the P12-SKETCH-003
// blocker: the face references BetterCAD has cannot keep a sketch on "its"
// face when a parameter moves that face.
//
// Public API only. A stepped block:
//   Base  100 x 60 rectangle at x 0..100, extruded by the parameter `height`
//         (20 mm);
//   Step  50 x 60 rectangle at x 100..150, extruded 35 mm and joined to Base.
// A sketch is to be placed on Base's top face.
//
// 1. The only persistent reference to a face is a FaceSignature: its plane
//    and outward side. Base's top face is "the plane z = 20 facing +Z".
// 2. `height` 20 -> 40 mm. Base's top face is now at z = 40. The stored
//    signature matches no face (the sketch would fail with NotFound).
// 3. The body's face list carries only geometry: surface, area, centroid and
//    signature. No field says which feature made a face or which face it
//    was before. Any rule that picks "the face it became" from geometry
//    alone is a guess: the nearest face parallel to the old plane is the
//    Step's top (z = 35), not Base's (z = 40).
// 4. Only the feature's definition knows that Base's end face lies at
//    z = height: the face's provenance (feature + role), which the reference
//    architecture does not record.
//
// Built against the qualified Release build, e.g.:
//   c++ -std=c++23 -O2 -DBETTERCAD_CORE_STATIC_DEFINE
//       -DBETTERCAD_FEATURES_STATIC_DEFINE -DBETTERCAD_GEOMETRY_STATIC_DEFINE
//       -DBETTERCAD_SKETCH_STATIC_DEFINE -I<repo>/include
//       -I<build>/generated/include face_reference_reproducer.cpp
//       <build>/lib/libbettercad_features.a <build>/lib/libbettercad_geometry.a
//       <OCCT import libraries as for the examples>
//       <build>/lib/libbettercad_sketch.a <build>/lib/libbettercad_core.a
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/parameters/Parameter.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;

namespace {

template <typename T>
T need(Result<T> result, const char* what) {
    if (!result) {
        std::printf("FAILED: %s: %s\n", what, result.error().message.c_str());
        std::exit(2);
    }
    return std::move(*result);
}

/// A fully constrained rectangle [x0, x1] x [y0, y1] (mm) on the model's XY plane.
std::unique_ptr<sketch::Sketch> rectangle(const std::string& name, double x0, double y0, double x1, double y1) {
    auto s = std::make_unique<sketch::Sketch>(name);
    const auto p = [&](double x, double y) {
        const EntityId id = need(s->addPoint(Point2D{x * units::mm, y * units::mm}), "point");
        need(s->addFixed(id), "fixed");
        return id;
    };
    const EntityId a = p(x0, y0);
    const EntityId b = p(x1, y0);
    const EntityId c = p(x1, y1);
    const EntityId d = p(x0, y1);
    need(s->addLine(a, b), "line");
    need(s->addLine(b, c), "line");
    need(s->addLine(c, d), "line");
    need(s->addLine(d, a), "line");
    return s;
}

void listUpwardFaces(const geometry::Body& body) {
    const auto faces = need(geometry::listFaces(body), "listFaces");
    for (const geometry::FaceInfo& face : faces) {
        if (!face.signature || face.signature->normal != Direction3D::unitZ()) {
            continue;
        }
        std::printf("    %s  area %.3f mm^2  centroid (%.3f, %.3f, %.3f) mm  signature: %s\n",
                    std::string(geometry::toString(face.surface)).c_str(), face.area.in(units::mm2),
                    face.centroid.x.in(units::mm), face.centroid.y.in(units::mm), face.centroid.z.in(units::mm),
                    geometry::describe(*face.signature).c_str());
    }
}

} // namespace

int main() {
    std::printf("P12-SKETCH-003 face reference reproducer\n\n");
    Document doc{"SteppedBlock"};
    const ParameterId height = need(doc.createParameter("height", 20_mm, units::mm), "height");
    const ObjectId baseSketch = need(doc.addObject(rectangle("BaseSketch", 0, 0, 100, 60)), "BaseSketch");
    const ObjectId base = need(
        doc.addObject(need(features::ExtrudeFeature::create(
                               "Base", {.profile = SketchId::fromValue(baseSketch.value()), .depthParameter = height}),
                           "Base")),
        "Base");
    const ObjectId stepSketch = need(doc.addObject(rectangle("StepSketch", 100, 0, 150, 60)), "StepSketch");
    const ObjectId step = need(
        doc.addObject(need(features::ExtrudeFeature::create(
                               "Step", {.profile = SketchId::fromValue(stepSketch.value()),
                                        .depth = 35_mm,
                                        .operation = features::FeatureOperation::Join,
                                        .target = FeatureId::fromValue(base.value())}),
                           "Step")),
        "Step");

    features::Regenerator regenerator;
    if (!need(regenerator.regenerate(doc), "regenerate").succeeded()) {
        std::printf("FAILED: the model does not regenerate\n");
        return 2;
    }
    std::printf("1. height = 20 mm. Faces of the result facing +Z:\n");
    listUpwardFaces(*regenerator.body(step));
    // The user picks Base's top face: the only persistent form is its signature.
    const geometry::FaceSignature picked =
        geometry::planeSignature(Point3D{0_mm, 0_mm, 20_mm}, Direction3D::unitZ());
    const auto before = need(geometry::findFaces(*regenerator.body(step), picked), "findFaces");
    std::printf("   Reference to Base's top face: %s -> %zu face(s)\n\n", geometry::describe(picked).c_str(),
                before.size());

    need(doc.setParameterValue(height, 40_mm), "height = 40");
    if (!need(regenerator.regenerate(doc), "regenerate").succeeded()) {
        std::printf("FAILED: the model does not regenerate at height 40\n");
        return 2;
    }
    std::printf("2. height = 40 mm. Faces of the result facing +Z:\n");
    listUpwardFaces(*regenerator.body(step));
    const auto after = need(geometry::findFaces(*regenerator.body(step), picked), "findFaces");
    std::printf("   The same reference now matches %zu face(s): a sketch on it would fail with NotFound.\n\n",
                after.size());

    // 3. A geometric guess: the face parallel to the old plane nearest to it.
    const auto faces = need(geometry::listFaces(*regenerator.body(step)), "listFaces");
    std::optional<geometry::FaceInfo> nearest;
    double best = std::numeric_limits<double>::infinity();
    for (const geometry::FaceInfo& face : faces) {
        if (!face.signature || face.signature->normal != picked.normal) {
            continue;
        }
        const double distance = std::abs(face.signature->point.z.in(units::mm) - picked.point.z.in(units::mm));
        if (distance < best) {
            best = distance;
            nearest = face;
        }
    }
    std::printf("3. FaceInfo holds surface, area, centroid and signature only; nothing names the feature\n"
                "   that made a face or the face it was before. The nearest face parallel to the old\n"
                "   plane is at z = %.3f mm (centroid x = %.3f mm): Step's top face, not Base's.\n\n",
                nearest ? nearest->centroid.z.in(units::mm) : -1.0,
                nearest ? nearest->centroid.x.in(units::mm) : -1.0);

    // 4. What does identify it: Base's definition. Its end face lies at
    //    z = depth along the profile normal.
    const auto* extrude = doc.findObjectAs<features::ExtrudeFeature>(base);
    const Parameter* driving = doc.parameters().find(*extrude->definition().depthParameter);
    const double endZ = Length::fromSi(driving->siValue()).in(units::mm);
    const auto fromDefinition = need(
        geometry::findFaces(*regenerator.body(step),
                            geometry::planeSignature(Point3D{0_mm, 0_mm, endZ * units::mm}, Direction3D::unitZ())),
        "findFaces");
    std::printf("4. Base's definition puts its end face at z = %.3f mm, which matches %zu face(s) (area\n"
                "   %.3f mm^2). Only the feature knows that role; the reference architecture does not\n"
                "   record it.\n",
                endZ, fromDefinition.size(), fromDefinition.empty() ? 0.0 : fromDefinition.front().area.in(units::mm2));

    const bool reproduced = before.size() == 1 && after.empty() && nearest &&
                            std::abs(nearest->centroid.z.in(units::mm) - 35.0) < 1e-9 && fromDefinition.size() == 1;
    std::printf("\nREPRODUCED: %s\n", reproduced ? "yes" : "no");
    return reproduced ? 0 : 1;
}
