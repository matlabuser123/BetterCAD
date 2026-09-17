#pragma once

#include "features/FeatureTestSupport.hpp"
#include "support/FaceKindModels.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/CombineFeature.hpp>
#include <bettercad/features/Datums.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/SplitFeature.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>

namespace bettercad::test {

// P12-FEAT-002 reference model: bodies combined, then split.
//
//   height 20 mm, cut 60 mm
//   ASketch   XY plane: rectangle x 0..100, y 0..60
//   A         extrude by height (new body)
//   BSketch   XY plane: rectangle x 80..150, y 20..40
//   B         extrude 40 mm (new body)
//   CSketch   XY plane: circle r 10 about (40, 30)
//   C         extrude 30 mm (new body)
//   Joined    combine: join A with B and C
//   Middle    datum plane: the model's YZ plane moved along +X by cut
//   Halves    split Joined by Middle, keeping both parts
//
// With h = height and c = cut, for c in [50, 80] (C lies wholly behind
// Middle, B wholly in front): A and B overlap in [80, 100] x [20, 40] x
// [0, min(h, 40)], A and C in the disc x [0, min(h, 30)], and B and C not
// at all. The front part is A beyond c with B; the back part is A before c
// with C.
// IDs: height 1, cut 2, ASketch 3, A 4, BSketch 5, B 6, CSketch 7, C 8,
// Joined 9, Middle 10, Halves 11.
struct BodyOpsModel : FaceKindModel {
    static constexpr std::string_view kDocumentId = "e6b1d840-2f57-4c3a-8d19-0a4c7e5b9f62";
    static constexpr double pi = std::numbers::pi;
    ParameterId height, cut;
    ObjectId aSketch, a, bSketch, b, cSketch, c, joined, middle, halves;

    BodyOpsModel() : FaceKindModel(kDocumentId, "BodyOps") {
        using namespace bettercad::literals;
        using namespace bettercad::features;
        height = doc.createParameter("height", 20_mm, units::mm).value();
        cut = doc.createParameter("cut", 60_mm, units::mm).value();
        aSketch = addFixedRectangle("ASketch", 0.0, 0.0, 100.0, 60.0);
        a = add(ExtrudeFeature::create("A", {.profile = sketchOf(aSketch), .depthParameter = height}));
        bSketch = addFixedRectangle("BSketch", 80.0, 20.0, 70.0, 20.0);
        b = add(ExtrudeFeature::create("B", {.profile = sketchOf(bSketch), .depth = 40_mm}));
        auto disc = std::make_unique<sketch::Sketch>("CSketch");
        const EntityId circle = require(disc->addCircle(Point2D{40_mm, 30_mm}, 10_mm));
        require(disc->addFixed(std::get<sketch::CircleEntity>(disc->findEntity(circle)->geometry).center));
        require(disc->addRadius(circle, 10_mm));
        cSketch = doc.addObject(std::move(disc)).value();
        c = add(ExtrudeFeature::create("C", {.profile = sketchOf(cSketch), .depth = 30_mm}));
        joined = add(CombineFeature::create(
            "Joined", {.target = featureOf(a), .tools = {featureOf(b), featureOf(c)}, .operation = FeatureOperation::Join}));
        middle = add(DatumPlane::create("Middle", {.kind = DatumPlaneKind::Offset,
                                                   .base = {.object = {}, .plane = PrincipalPlane::YZ},
                                                   .offsetParameter = cut}));
        halves = add(SplitFeature::create("Halves", {.target = featureOf(joined),
                                                     .plane = PlaneReference{.object = middle},
                                                     .keep = geometry::SplitKeep::Both}));
    }

    static double aVolume(double h) { return 6000.0 * h; }
    static constexpr double kBVolume = 70.0 * 20.0 * 40.0;
    static double cVolume() { return 100.0 * pi * 30.0; }
    static double abVolume(double h) { return 400.0 * std::min(h, 40.0); }
    static double acVolume(double h) { return 100.0 * pi * std::min(h, 30.0); }

    static Vec3 aCentre(double h) { return {50.0, 30.0, h / 2.0}; }
    static constexpr Vec3 kBCentre{115.0, 30.0, 20.0};
    static constexpr Vec3 kCCentre{40.0, 30.0, 15.0};
    static Vec3 abCentre(double h) { return {90.0, 30.0, std::min(h, 40.0) / 2.0}; }
    static Vec3 acCentre(double h) { return {40.0, 30.0, std::min(h, 30.0) / 2.0}; }

    /// Joined: A + B + C - (A and B) - (A and C).
    static double joinedVolume(double h) {
        return aVolume(h) + kBVolume + cVolume() - abVolume(h) - acVolume(h);
    }
    static Vec3 joinedCentre(double h) {
        return weightedCentre({{aVolume(h), aCentre(h)},
                               {kBVolume, kBCentre},
                               {cVolume(), kCCentre},
                               {-abVolume(h), abCentre(h)},
                               {-acVolume(h), acCentre(h)}});
    }

    /// A cut by B and C: A - (A and B) - (A and C).
    static double cutVolume(double h) { return aVolume(h) - abVolume(h) - acVolume(h); }
    static Vec3 cutCentre(double h) {
        return weightedCentre({{aVolume(h), aCentre(h)}, {-abVolume(h), abCentre(h)}, {-acVolume(h), acCentre(h)}});
    }

    /// The front part: A beyond c, with B.
    static double frontVolume(double h, double cutMm) {
        return (100.0 - cutMm) * 60.0 * h + kBVolume - abVolume(h);
    }
    static Vec3 frontCentre(double h, double cutMm) {
        return weightedCentre({{(100.0 - cutMm) * 60.0 * h, {(100.0 + cutMm) / 2.0, 30.0, h / 2.0}},
                               {kBVolume, kBCentre},
                               {-abVolume(h), abCentre(h)}});
    }

    /// The back part: A before c, with C.
    static double backVolume(double h, double cutMm) {
        return cutMm * 60.0 * h + cVolume() - acVolume(h);
    }
    static Vec3 backCentre(double h, double cutMm) {
        return weightedCentre({{cutMm * 60.0 * h, {cutMm / 2.0, 30.0, h / 2.0}},
                               {cVolume(), kCCentre},
                               {-acVolume(h), acCentre(h)}});
    }
};

} // namespace bettercad::test
