#pragma once

#include "features/FeatureTestSupport.hpp"
#include "support/ShellModels.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/features/Datums.hpp>
#include <bettercad/features/DraftFeature.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/FilletFeature.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bettercad::test {

// P12-FEAT-004 reference models: drafts whose sections are rectangles,
// circles, rounded rectangles and L shapes whose outlines move by
// s(z) = tan(a) (z - z0) (pulled along +Z from a neutral plane at z0; the
// opposite sign pulled along -Z). Each section's area is a quadratic in z and
// its centroid does not move, so volumes, centres and turned face areas are
// integrated exactly. Every expected value is written out from the
// parameters; none is read from a result.

/// c0 + c1 z + c2 z^2.
struct Quadratic {
    double c0 = 0.0;
    double c1 = 0.0;
    double c2 = 0.0;

    [[nodiscard]] double at(double z) const { return c0 + (c1 + c2 * z) * z; }
    /// The integral over [z0, z1].
    [[nodiscard]] double integral(double z0, double z1) const { return primitive(z1) - primitive(z0); }
    /// The integral of z times this over [z0, z1].
    [[nodiscard]] double moment(double z0, double z1) const { return momentPrimitive(z1) - momentPrimitive(z0); }

private:
    [[nodiscard]] double primitive(double z) const { return ((c2 * z / 3.0 + c1 / 2.0) * z + c0) * z; }
    [[nodiscard]] double momentPrimitive(double z) const {
        return ((c2 * z / 4.0 + c1 / 3.0) * z + c0 / 2.0) * z * z;
    }
};

/// How far a drafted outline has moved in at height z: s(z) = slope z + offset.
struct Shift {
    double slope = 0.0;
    double offset = 0.0;

    /// Pulled along +Z (or -Z with @p down) from a neutral plane at z0, by
    /// @p angleDeg.
    static Shift of(double angleDeg, double z0, bool down = false) {
        const double t = std::tan(angleDeg * kPi / 180.0) * (down ? -1.0 : 1.0);
        return {t, -t * z0};
    }
    [[nodiscard]] double at(double z) const { return slope * z + offset; }
};

/// Area of a w x d rectangle whose sides have moved in by s, with corners of
/// radius r - s (r = 0: square corners).
inline Quadratic shrunkRectangle(double w, double d, const Shift& s, double r = 0.0) {
    // (w - 2s)(d - 2s) - k (r - s)^2, s = a z + b.
    const double k = r > 0.0 ? 4.0 - kPi : 0.0;
    const double a = s.slope;
    const double b = s.offset;
    return {(w - 2.0 * b) * (d - 2.0 * b) - k * (r - b) * (r - b),
            -2.0 * a * (d - 2.0 * b) - 2.0 * a * (w - 2.0 * b) + 2.0 * k * a * (r - b),
            4.0 * a * a - k * a * a};
}

/// Area of a circle of radius r + s (r - s with @p shrinks).
inline Quadratic grownCircle(double r, const Shift& s, bool shrinks = false) {
    const double sign = shrinks ? -1.0 : 1.0;
    const double a = sign * s.slope;
    const double b = r + sign * s.offset;
    return {kPi * b * b, 2.0 * kPi * a * b, kPi * a * a};
}

/// A prism along Z from z0 to z1 with section @p area centred at (x, y),
/// added (+1) or removed (-1).
inline Part sectionPart(const Quadratic& area, double x, double y, double z0, double z1, double sign = 1.0) {
    const double volume = area.integral(z0, z1);
    return {sign * volume, {x, y, area.moment(z0, z1) / volume}};
}

/// The area of a drafted planar face whose width at height z is @p width,
/// from z0 to z1, turned by @p angleDeg from vertical.
inline double turnedArea(const Quadratic& width, double z0, double z1, double angleDeg) {
    return width.integral(z0, z1) / std::cos(angleDeg * kPi / 180.0);
}

/// Width w - 2 s(z) of a drafted side between two drafted sides.
inline Quadratic sideWidth(double w, const Shift& s) {
    return {w - 2.0 * s.offset, -2.0 * s.slope, 0.0};
}

// A 100 x 60 block drafted five ways:
//
//   height        40 mm
//   taper         5 deg
//   rise          20 mm
//   BlockSketch   XY plane: rectangle x 0..100, y 0..60; lines front
//                 (y = 0), right (x = 100), back (y = 60), left (x = 0)
//   Block         extrude by height (new body)
//   Mid           datum plane: the model's XY plane moved up by rise
//   Tapered       draft Block's four sides about the model's XY plane by
//                 taper: the block narrows going up
//   TaperedMid    the same about Mid: wider below it, narrower above
//   Flared        the same about the model's XY plane by -5 deg
//   OnTop         the same about Block's end cap (pulled up, z = height) by
//                 taper: the top stays, the bottom grows
//   OnBottom      the same about Block's start cap (pulled down, z = 0) by
//                 -5 deg: the shape of Tapered at 5 deg
//
// All five consume Block. IDs: height 1, taper 2, rise 3, BlockSketch 4,
// Block 5, Mid 6, Tapered 7, TaperedMid 8, Flared 9, OnTop 10, OnBottom 11.
struct DraftedBlockModel : FaceKindModel {
    static constexpr std::string_view kDocumentId = "c47d2a9e-8b15-4f36-a0d2-5e9b1c7f3a48";
    ParameterId height, taper, rise;
    ObjectId blockSketch, block, mid, tapered, taperedMid, flared, onTop, onBottom;
    std::array<EntityId, 4> lines{};

    DraftedBlockModel() : FaceKindModel(kDocumentId, "DraftedBlock") {
        using namespace bettercad::literals;
        using namespace bettercad::features;
        height = doc.createParameter("height", 40_mm, units::mm).value();
        taper = doc.createParameter("taper", 5_deg, units::deg).value();
        rise = doc.createParameter("rise", 20_mm, units::mm).value();
        blockSketch = addFixedRectangle("BlockSketch", 0.0, 0.0, 100.0, 60.0, &lines);
        block = add(ExtrudeFeature::create("Block", {.profile = sketchOf(blockSketch), .depthParameter = height}));
        mid = add(DatumPlane::create("Mid", {.kind = DatumPlaneKind::Offset,
                                             .base = {.plane = PrincipalPlane::XY},
                                             .offsetParameter = rise}));
        const auto draftOf = [&](const std::string& name, const PlaneReference& plane,
                                 std::optional<ParameterId> angle, Angle literal) {
            return add(DraftFeature::create(name, {.target = featureOf(block),
                                                   .faces = sides(),
                                                   .neutralPlane = plane,
                                                   .angle = angle ? Angle{} : literal,
                                                   .angleParameter = angle}));
        };
        tapered = draftOf("Tapered", PlaneReference{}, taper, Angle{});
        taperedMid = draftOf("TaperedMid", PlaneReference{.object = mid}, taper, Angle{});
        flared = draftOf("Flared", PlaneReference{}, std::nullopt, -(5_deg));
        onTop = draftOf("OnTop", faceOf(block, {.role = FaceRole::EndCap}), taper, Angle{});
        onBottom = draftOf("OnBottom", faceOf(block, {.role = FaceRole::StartCap}), std::nullopt, -(5_deg));
    }

    [[nodiscard]] std::vector<FaceName> sides() const {
        std::vector<FaceName> names;
        for (const EntityId line : lines) {
            names.push_back(nameOf(block, FaceRole::Side, line));
        }
        return names;
    }

    /// The block of height h drafted with shift s, and its bounds.
    static ExpectedShell shape(double h, const Shift& s) {
        const double widest = std::min(s.at(0.0), s.at(h)); // the most the outline moved out
        return {{sectionPart(shrunkRectangle(100, 60, s), 50, 30, 0, h)},
                {std::min(0.0, widest), std::min(0.0, widest), 0.0},
                {100.0 - std::min(0.0, widest), 60.0 - std::min(0.0, widest), h}};
    }
    /// The drafted areas: the four sides (front, right, back, left), the top
    /// and the bottom.
    static std::array<double, 6> areas(double h, const Shift& s, double angleDeg) {
        const double front = turnedArea(sideWidth(100, s), 0, h, angleDeg);
        const double right = turnedArea(sideWidth(60, s), 0, h, angleDeg);
        return {front, right, front, right, shrunkRectangle(100, 60, s).at(h), shrunkRectangle(100, 60, s).at(0)};
    }
};

// Drafted parts of other shapes, all by `angle`:
//
//   angle         3 deg
//   height        40 mm
//   round         10 mm
//   CanSketch     XY plane: circle r 20 about the origin
//   Can           extrude 50 mm (new body)
//   CanTaper      draft Can's side about the model's XY plane: a frustum
//   PlateSketch   XY plane: rectangle x 0..100, y 0..60
//   Plate         extrude by height
//   BoreSketch    XY plane: circle r 10 about (50, 30)
//   Bore          extrude through all of Plate (cut)
//   BoreTaper     draft Bore's side about the model's XY plane: the hole
//                 widens going up, r 10 + z tan(a)
//   BlockSketch   XY plane: rectangle x 200..300, y 0..60
//   Block         extrude by height
//   Floor         datum plane: the model's XY plane moved up 15 mm
//   PocketSketch  on Floor: rectangle x 220..280, y 15..45; lines front,
//                 right, back, left
//   Pocket        extrude through all of Block (cut): a pocket from z = 15
//   PocketTaper   draft Pocket's four walls about Block's end cap: the pocket
//                 narrows going down, by (height - z) tan(a) on each side
//   EllSketch     XY plane: polygon (0, 100) (100, 100) (100, 130) (40, 130)
//                 (40, 180) (0, 180): arms 100 x 30 and 40 x 80
//   Ell           extrude 50 mm
//   EllTaper      draft Ell's six sides about the model's XY plane
//   PadSketch     XY plane: rectangle x 400..500, y 0..60; lines front,
//                 right, back, left
//   Pad           extrude by height
//   Round         fillet Pad's four vertical edges, radius `round`
//   PadTaper      draft Round's four planar sides (Pad's sides) about the
//                 model's XY plane: the rounds, tangent to them, follow
//   PadOneSide    draft only Round's front side: the whole tangent chain
//                 turns, as for PadTaper
//
// IDs: angle 1, height 2, round 3, CanSketch 4, Can 5, CanTaper 6,
// PlateSketch 7, Plate 8, BoreSketch 9, Bore 10, BoreTaper 11,
// BlockSketch 12, Block 13, Floor 14, PocketSketch 15, Pocket 16,
// PocketTaper 17, EllSketch 18, Ell 19, EllTaper 20, PadSketch 21, Pad 22,
// Round 23, PadTaper 24, PadOneSide 25.
struct DraftedPartsModel : FaceKindModel {
    static constexpr std::string_view kDocumentId = "8e2b5f71-4c96-4d0a-b3e8-1f7a6c2d9b05";
    ParameterId angle, height, round;
    ObjectId canSketch, can, canTaper, plateSketch, plate, boreSketch, bore, boreTaper, blockSketch, block, floor,
        pocketSketch, pocket, pocketTaper, ellSketch, ell, ellTaper, padSketch, pad, rounded, padTaper, padOneSide;
    EntityId canCircle, boreCircle;
    std::array<EntityId, 4> pocketLines{};
    std::array<EntityId, 4> padLines{};
    std::vector<EntityId> ellLines;

    DraftedPartsModel() : FaceKindModel(kDocumentId, "DraftedParts") {
        using namespace bettercad::literals;
        using namespace bettercad::features;
        angle = doc.createParameter("angle", 3_deg, units::deg).value();
        height = doc.createParameter("height", 40_mm, units::mm).value();
        round = doc.createParameter("round", 10_mm, units::mm).value();
        const auto draftOf = [&](const std::string& name, ObjectId target, std::vector<FaceName> faces,
                                 const PlaneReference& plane) {
            return add(DraftFeature::create(name, {.target = featureOf(target),
                                                   .faces = std::move(faces),
                                                   .neutralPlane = plane,
                                                   .angleParameter = angle}));
        };

        canSketch = circleSketch("CanSketch", 0.0, 0.0, 20.0, canCircle);
        can = addBoss("Can", canSketch, 50.0);
        canTaper = draftOf("CanTaper", can, {nameOf(can, FaceRole::Side, canCircle)}, PlaneReference{});

        plateSketch = addFixedRectangle("PlateSketch", 0.0, 0.0, 100.0, 60.0);
        plate = add(ExtrudeFeature::create("Plate", {.profile = sketchOf(plateSketch), .depthParameter = height}));
        boreSketch = circleSketch("BoreSketch", 50.0, 30.0, 10.0, boreCircle);
        bore = add(ExtrudeFeature::create("Bore", {.profile = sketchOf(boreSketch),
                                                   .operation = FeatureOperation::Cut,
                                                   .target = featureOf(plate),
                                                   .termination = ExtrudeTermination::ThroughAll}));
        boreTaper = draftOf("BoreTaper", bore, {nameOf(bore, FaceRole::Side, boreCircle)}, PlaneReference{});

        blockSketch = addFixedRectangle("BlockSketch", 200.0, 0.0, 100.0, 60.0);
        block = add(ExtrudeFeature::create("Block", {.profile = sketchOf(blockSketch), .depthParameter = height}));
        floor = add(DatumPlane::create("Floor", {.kind = DatumPlaneKind::Offset,
                                                 .base = {.plane = PrincipalPlane::XY},
                                                 .offset = 15_mm}));
        pocketSketch = addFixedRectangle("PocketSketch", 220.0, 15.0, 60.0, 30.0, &pocketLines,
                                         PlaneReference{.object = floor});
        pocket = add(ExtrudeFeature::create("Pocket", {.profile = sketchOf(pocketSketch),
                                                       .operation = FeatureOperation::Cut,
                                                       .target = featureOf(block),
                                                       .termination = ExtrudeTermination::ThroughAll}));
        std::vector<FaceName> walls;
        for (const EntityId line : pocketLines) {
            walls.push_back(nameOf(pocket, FaceRole::Side, line));
        }
        pocketTaper = draftOf("PocketTaper", pocket, walls, faceOf(block, {.role = FaceRole::EndCap}));

        ellSketch = doc.addObject(polygonWithLines("EllSketch", Frame3D::xy(),
                                                   {{0, 100}, {100, 100}, {100, 130}, {40, 130}, {40, 180}, {0, 180}},
                                                   ellLines))
                        .value();
        ell = addBoss("Ell", ellSketch, 50.0);
        std::vector<FaceName> ellSides;
        for (const EntityId line : ellLines) {
            ellSides.push_back(nameOf(ell, FaceRole::Side, line));
        }
        ellTaper = draftOf("EllTaper", ell, ellSides, PlaneReference{});

        padSketch = addFixedRectangle("PadSketch", 400.0, 0.0, 100.0, 60.0, &padLines);
        pad = add(ExtrudeFeature::create("Pad", {.profile = sketchOf(padSketch), .depthParameter = height}));
        std::vector<geometry::EdgeSignature> corners;
        for (const auto& [x, y] : {std::pair{400.0, 0.0}, {500.0, 0.0}, {500.0, 60.0}, {400.0, 60.0}}) {
            corners.push_back(
                geometry::lineSignature(Point3D{x * units::mm, y * units::mm, Length{}}, Direction3D::unitZ()));
        }
        rounded = add(FilletFeature::create("Round", {.target = featureOf(pad), .edges = corners, .radiusParameter = round}));
        std::vector<FaceName> padSides;
        for (const EntityId line : padLines) {
            padSides.push_back(nameOf(pad, FaceRole::Side, line));
        }
        padTaper = draftOf("PadTaper", rounded, padSides, PlaneReference{});
        padOneSide = draftOf("PadOneSide", rounded, {padSides.front()}, PlaneReference{});
    }

    /// A sketch with a fixed circle; @p circle receives its entity.
    ObjectId circleSketch(const std::string& name, double x, double y, double r, EntityId& circle) {
        auto sketch = std::make_unique<sketch::Sketch>(name);
        circle = require(sketch->addCircle(Point2D{x * units::mm, y * units::mm}, r * units::mm));
        require(sketch->addFixed(std::get<sketch::CircleEntity>(sketch->findEntity(circle)->geometry).center));
        require(sketch->addRadius(circle, r * units::mm));
        return doc.addObject(std::move(sketch)).value();
    }

    static ExpectedShell canShape(double a) {
        const Shift s = Shift::of(a, 0.0);
        return {{sectionPart(grownCircle(20, s, true), 0, 0, 0, 50)}, {-20, -20, 0}, {20, 20, 50}};
    }
    static ExpectedShell boreShape(double a, double h) {
        const Shift s = Shift::of(a, 0.0);
        return {{sectionPart(shrunkRectangle(100, 60, Shift{}), 50, 30, 0, h),
                 sectionPart(grownCircle(10, s), 50, 30, 0, h, -1.0)},
                {0, 0, 0},
                {100, 60, h}};
    }
    /// The pocket narrows going down from the top (h), where it is 60 x 30.
    static ExpectedShell pocketShape(double a, double h) {
        const Shift s = Shift::of(a, h);
        const Shift inward{-s.slope, -s.offset}; // (h - z) tan(a), growing downwards
        return {{sectionPart(shrunkRectangle(100, 60, Shift{}), 250, 30, 0, h),
                 sectionPart(shrunkRectangle(60, 30, inward), 250, 30, 15, h, -1.0)},
                {200, 0, 0},
                {300, 60, h}};
    }
    static ExpectedShell ellShape(double a) {
        const Shift s = Shift::of(a, 0.0);
        return {{sectionPart(shrunkRectangle(100, 30, s), 50, 115, 0, 50),
                 sectionPart(shrunkRectangle(40, 80, s), 20, 140, 0, 50),
                 sectionPart(shrunkRectangle(40, 30, s), 20, 115, 0, 50, -1.0)},
                {0, 100, 0},
                {100, 180, 50}};
    }
    static ExpectedShell padShape(double a, double h, double r) {
        const Shift s = Shift::of(a, 0.0);
        return {{sectionPart(shrunkRectangle(100, 60, s, r), 450, 30, 0, h)}, {400, 0, 0}, {500, 60, h}};
    }
};

} // namespace bettercad::test
