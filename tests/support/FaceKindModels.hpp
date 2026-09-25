#pragma once

#include "features/FeatureTestSupport.hpp"
#include "support/LoftModels.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/geometry/Hole.hpp>
#include <bettercad/core/parameters/Parameter.hpp>
#include <bettercad/features/ChamferFeature.hpp>
#include <bettercad/features/CircularPatternFeature.hpp>
#include <bettercad/features/Datums.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/LinearPatternFeature.hpp>
#include <bettercad/features/LoftFeature.hpp>
#include <bettercad/features/MirrorFeature.hpp>
#include <bettercad/features/RevolveFeature.hpp>
#include <bettercad/features/SweepFeature.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bettercad::test {

// P12-SKETCH-003 reference models: sketches on the planar faces that
// revolves, sweeps, lofts, holes and chamfers generate, and on the copies
// that patterns and mirrors make. Every feature built on such a sketch is a
// new body, a cylinder, so its volume, centre and bounds are checked on their
// own. Expected frames follow geometry::faceFrame()'s rule: the origin is the
// plane's point nearest the model origin, X is the model's X (or, for planes
// facing nearer X than Y or Z, the model's Y) projected into the plane, and
// Y = normal x X. Every expected value is written out from the parameters;
// none is read from a reference.

using Vec3 = std::array<double, 3>;

/// A cylinder by its base circle's centre, unit axis, radius and length (mm).
struct Cylinder {
    Vec3 base{};
    Vec3 axis{};
    double radius = 0.0;
    double length = 0.0;

    [[nodiscard]] double volume() const { return std::numbers::pi * radius * radius * length; }
    [[nodiscard]] Vec3 centre() const { return along(length / 2.0); }
    /// Each coordinate spans the two end circles, which reach r sqrt(1 - a_i^2)
    /// either side of their centres along model axis i.
    [[nodiscard]] Vec3 lower() const { return bound(false); }
    [[nodiscard]] Vec3 upper() const { return bound(true); }

private:
    [[nodiscard]] Vec3 along(double t) const {
        return {base[0] + t * axis[0], base[1] + t * axis[1], base[2] + t * axis[2]};
    }
    [[nodiscard]] Vec3 bound(bool upperSide) const {
        const Vec3 top = along(length);
        Vec3 out{};
        for (std::size_t i = 0; i < 3; ++i) {
            const double reach = radius * std::sqrt(std::max(0.0, 1.0 - axis[i] * axis[i]));
            out[i] = upperSide ? std::max(base[i], top[i]) + reach : std::min(base[i], top[i]) - reach;
        }
        return out;
    }
};

/// A sketch frame as expected: origin (mm) and unit axes.
struct ExpectedFrame {
    Vec3 origin{};
    Vec3 x{};
    Vec3 y{};
    Vec3 normal{};
};

/// A sketch on a face, the frame it must have, and the boss extruded from it.
struct ExpectedBoss {
    ObjectId sketch;
    ObjectId boss;
    ExpectedFrame frame;
    Cylinder cylinder;
};

/// Shared construction for the models below.
struct FaceKindModel {
    Document doc;

    FaceKindModel(std::string_view documentId, std::string name)
        : doc{DocumentId::fromValue(*Uuid::parse(documentId)), std::move(name)} {}

    static SketchId sketchOf(ObjectId id) { return SketchId::fromValue(id.value()); }
    static FeatureId featureOf(ObjectId id) { return FeatureId::fromValue(id.value()); }

    static PlaneReference faceOf(ObjectId feature, FaceSelector face) {
        return PlaneReference{.object = feature, .face = std::move(face)};
    }

    template <typename T>
    ObjectId add(Result<std::unique_ptr<T>> object) {
        if (!object) {
            FAIL(object.error().message);
        }
        return doc.addObject(std::move(*object)).value();
    }

    /// A sketch on @p reference with a circle of radius @p r about the fixed
    /// point (u, v), all in mm.
    ObjectId addCircleSketch(const std::string& name, const PlaneReference& reference, double u, double v,
                             double r) {
        auto sketch = std::make_unique<sketch::Sketch>(name);
        const EntityId circle =
            require(sketch->addCircle(Point2D{u * units::mm, v * units::mm}, r * units::mm));
        require(sketch->addFixed(std::get<sketch::CircleEntity>(sketch->findEntity(circle)->geometry).center));
        require(sketch->addRadius(circle, r * units::mm));
        REQUIRE(sketch->setAttachment(reference).has_value());
        return doc.addObject(std::move(sketch)).value();
    }

    /// A new body: @p sketch extruded along its normal by @p depthMm.
    ObjectId addBoss(const std::string& name, ObjectId sketch, double depthMm) {
        return add(features::ExtrudeFeature::create(
            name, {.profile = sketchOf(sketch), .depth = depthMm * units::mm}));
    }

    /// A sketch of a fully constrained w x h rectangle with its corner at
    /// (x, y) mm on the model's XY plane. Returns its lines: y = y0, x = x1,
    /// y = y1, x = x0.
    ObjectId addFixedRectangle(const std::string& name, double x, double y, double w, double h,
                               std::array<EntityId, 4>* lines = nullptr,
                               const std::optional<PlaneReference>& attachment = std::nullopt) {
        auto sketch = std::make_unique<sketch::Sketch>(name);
        const auto sides = addRectangle(*sketch, x * units::mm, y * units::mm, w * units::mm, h * units::mm);
        for (const EntityId line : sides) {
            require(sketch->addFixed(std::get<sketch::LineEntity>(sketch->findEntity(line)->geometry).start));
        }
        if (attachment) {
            REQUIRE(sketch->setAttachment(*attachment).has_value());
        }
        if (lines != nullptr) {
            *lines = sides;
        }
        return doc.addObject(std::move(sketch)).value();
    }

    /// A rectangle whose corner (x, y) mm is fixed, with horizontal and
    /// vertical sides and its sides' lengths constrained: literal or driven.
    static std::array<EntityId, 4> addSizedRectangle(sketch::Sketch& sketch, double x, double y, double w,
                                                     double h, std::optional<ParameterId> width,
                                                     std::optional<ParameterId> height) {
        using namespace bettercad::sketch;
        const auto lines = addRectangle(sketch, x * units::mm, y * units::mm, w * units::mm, h * units::mm);
        require(sketch.addFixed(std::get<LineEntity>(sketch.findEntity(lines[0])->geometry).start));
        require(sketch.addHorizontal(lines[0]));
        require(sketch.addHorizontal(lines[2]));
        require(sketch.addVertical(lines[1]));
        require(sketch.addVertical(lines[3]));
        const ConstraintId across = require(sketch.addDistance(lines[0], w * units::mm));
        const ConstraintId up = require(sketch.addDistance(lines[1], h * units::mm));
        if (width) {
            REQUIRE(sketch.setConstraintParameter(across, *width).has_value());
        }
        if (height) {
            REQUIRE(sketch.setConstraintParameter(up, *height).has_value());
        }
        return lines;
    }
};

inline constexpr double kPi = std::numbers::pi;
inline constexpr double kHalfRoot2 = std::numbers::sqrt2 / 2.0;

/// Centroid distance of an annular sector (radii r1, r2, half-angle
/// @p half radians) from its centre: 2/3 (r2^3 - r1^3) / (r2^2 - r1^2) sin(h) / h.
inline double sectorCentroid(double r1, double r2, double half) {
    return 2.0 / 3.0 * (r2 * r2 * r2 - r1 * r1 * r1) / (r2 * r2 - r1 * r1) * std::sin(half) / half;
}

/// The mass-weighted mean of parts (volume, centre).
inline Vec3 weightedCentre(const std::vector<std::pair<double, Vec3>>& parts) {
    double total = 0.0;
    Vec3 moment{};
    for (const auto& [volume, centre] : parts) {
        total += volume;
        for (std::size_t i = 0; i < 3; ++i) {
            moment[i] += volume * centre[i];
        }
    }
    return {moment[0] / total, moment[1] / total, moment[2] / total};
}

// A ring sector with a boss on each of its flat faces:
//
//   turn          angle, 120 deg
//   ring_length   20 mm
//   RingSketch    XY plane: rectangle x 20..40, y 0..ring_length, its
//                 corner (20, 0) fixed; lines bottom (y = 0), outer
//                 (x = 40), top (y = ring_length), inner (x = 20)
//   Ring          revolve by turn about the sketch's Y axis (the model's
//                 +Y), symmetric: from -turn/2 to +turn/2
//   EndSketch     on Ring's end cap: circle r 4 about (10, 30)
//   EndBoss       extrude 8 mm (new body)
//   StartSketch   on Ring's start cap: circle r 4 about (10, -30)
//   StartBoss     extrude 6 mm
//   TopSketch     on the side the top line sweeps: circle r 4 about (30, 0)
//   TopBoss       extrude 5 mm
//
// About +Y, the angle a turns +X to (cos a, 0, -sin a). With h = turn/2, the
// end cap lies at a = h facing (-sin h, 0, -cos h), the start cap at -h
// facing (-sin h, 0, cos h). While h is over 45 deg, a cap faces nearer X
// than Z, so its sketch X axis is the model's Y and its Y axis (normal x X)
// is (cos h, 0, -sin h) on the end cap (out along it) and (-cos h, 0, -sin h)
// on the start cap (in along it). Both caps' planes hold the Y axis, so their
// frames start at the model's origin. The top face (y = ring_length, facing
// +Y) has X = +X and Y = -Z. The outer and inner lines sweep cylinders.
//
// IDs: turn 1, ring_length 2, RingSketch 3, Ring 4, EndSketch 5, EndBoss 6,
// StartSketch 7, StartBoss 8, TopSketch 9, TopBoss 10.
struct RevolvedRingModel : FaceKindModel {
    static constexpr std::string_view kDocumentId = "7c3e9a14-5b2d-4f8e-a1c6-3d9b2e7f4a51";
    static constexpr double kInner = 20.0;
    static constexpr double kOuter = 40.0;
    ParameterId turn, ringLength;
    ObjectId ringSketch, ring, endSketch, endBoss, startSketch, startBoss, topSketch, topBoss;
    EntityId bottom, outer, top, inner;

    RevolvedRingModel() : FaceKindModel(kDocumentId, "RevolvedRing") {
        using namespace bettercad::literals;
        using namespace bettercad::features;
        turn = doc.createParameter("turn", 120_deg, units::deg).value();
        ringLength = doc.createParameter("ring_length", 20_mm, units::mm).value();

        auto profile = std::make_unique<sketch::Sketch>("RingSketch");
        const auto lines = addSizedRectangle(*profile, kInner, 0.0, kOuter - kInner, 20.0, std::nullopt, ringLength);
        bottom = lines[0];
        outer = lines[1];
        top = lines[2];
        inner = lines[3];
        ringSketch = doc.addObject(std::move(profile)).value();
        ring = add(RevolveFeature::create("Ring", {.profile = sketchOf(ringSketch),
                                                   .axis = RevolveAxis::sketchY(),
                                                   .angleParameter = turn,
                                                   .direction = RevolveDirection::Symmetric}));

        endSketch = addCircleSketch("EndSketch", faceOf(ring, {.role = FaceRole::EndCap}), 10.0, 30.0, 4.0);
        endBoss = addBoss("EndBoss", endSketch, 8.0);
        startSketch = addCircleSketch("StartSketch", faceOf(ring, {.role = FaceRole::StartCap}), 10.0, -30.0, 4.0);
        startBoss = addBoss("StartBoss", startSketch, 6.0);
        topSketch =
            addCircleSketch("TopSketch", faceOf(ring, {.role = FaceRole::Side, .entity = top}), 30.0, 0.0, 4.0);
        topBoss = addBoss("TopBoss", topSketch, 5.0);
    }

    /// Ring: turn/2 (R^2 - r^2) L.
    static double volume(double turnDeg, double lengthMm) {
        return turnDeg * kPi / 180.0 / 2.0 * (kOuter * kOuter - kInner * kInner) * lengthMm;
    }

    /// On +X (the sector is symmetric about it), halfway along Y.
    static Vec3 centre(double turnDeg, double lengthMm) {
        return {sectorCentroid(kInner, kOuter, turnDeg * kPi / 360.0), lengthMm / 2.0, 0.0};
    }

    /// x from the inner radius at the ends of the sector (h < 90 deg) to the
    /// outer radius at a = 0; z to the outer radius at the ends.
    static std::pair<Vec3, Vec3> bounds(double turnDeg, double lengthMm) {
        const double half = turnDeg * kPi / 360.0;
        return {{kInner * std::cos(half), 0.0, -kOuter * std::sin(half)},
                {kOuter, lengthMm, kOuter * std::sin(half)}};
    }

    [[nodiscard]] std::vector<ExpectedBoss> bosses(double turnDeg, double lengthMm) const {
        const double half = turnDeg * kPi / 360.0;
        const double c = std::cos(half);
        const double s = std::sin(half);
        const Vec3 endNormal{-s, 0.0, -c};
        const Vec3 startNormal{-s, 0.0, c};
        return {
            {endSketch, endBoss, {{0, 0, 0}, {0, 1, 0}, {c, 0, -s}, endNormal},
             {{30.0 * c, 10.0, -30.0 * s}, endNormal, 4.0, 8.0}},
            {startSketch, startBoss, {{0, 0, 0}, {0, 1, 0}, {-c, 0, -s}, startNormal},
             {{30.0 * c, 10.0, 30.0 * s}, startNormal, 4.0, 6.0}},
            {topSketch, topBoss, {{0, lengthMm, 0}, {1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
             {{30.0, lengthMm, 0.0}, {0, 1, 0}, 4.0, 5.0}},
        };
    }
};

// A bar swept up, round a bend and across, with a boss on five of its faces:
//
//   lift, rise, run, thick   10, 40, 50, 8 mm
//   Floor         datum plane: the model's XY plane raised by lift
//   BarProfile    on Floor: rectangle x -5..5, y -4..thick-4, its corner
//                 (-5, -4) fixed; lines bottom, right (x = 5), top, left
//   BarPath       XZ plane (sketch X = +X, Y = +Z): a fixed origin; p0 =
//                 (0, lift) above it; Up from p0 to p1 = p0 + (0, rise);
//                 Bend, a quarter arc about c = p1 - (30, 0) from p1 to
//                 p2 = c + (0, 30); Across from p2 to p3 = p2 - (run, 0)
//   Bar           sweep of BarProfile along Up, Bend, Across (new body)
//   StartSketch   on Bar's start cap: circle r 3 about (0, 0)
//   StartBoss     extrude 4 mm
//   EndSketch     on Bar's end cap: circle r 3 about (0, -80)
//   EndBoss       extrude 6 mm
//   SideSketch    on the side the right line sweeps along Up: circle r 3
//                 about (0, 30)
//   SideBoss      extrude 4 mm
//   AcrossSketch  on the side the right line sweeps along Across: circle
//                 r 3 about (-55, 0)
//   AcrossBoss    extrude 5 mm
//   BendSketch    on the side the top line sweeps along Bend: circle r 3
//                 about (-9, -71)
//   BendBoss      extrude 5 mm
//
// With e = lift, P = rise, Q = run, t = thick, R = 30: the start cap is the
// plane z = e facing -Z (sketch Y = -Y), the end cap x = -R - Q facing -X
// (sketch X = +Y, Y = -Z). The profile keeps its Y axis along the model's Y
// while the bend turns its X axis from +X to +Z. So the right line sweeps
// the plane x = 5 (facing +X; sketch X = +Y, Y = +Z) along Up, the cylinder
// of radius R + 5 about the bend's axis along Bend, and the plane
// z = e + P + R + 5 (facing +Z) along Across; the top line sweeps planes
// y = t - 4 (facing +Y; sketch Y = -Z) all the way.
//
// IDs: lift 1, rise 2, run 3, thick 4, Floor 5, BarProfile 6, BarPath 7,
// Bar 8, StartSketch 9, StartBoss 10, EndSketch 11, EndBoss 12,
// SideSketch 13, SideBoss 14, AcrossSketch 15, AcrossBoss 16, BendSketch 17,
// BendBoss 18.
struct SweptBarModel : FaceKindModel {
    static constexpr std::string_view kDocumentId = "2e8b5d71-9c4a-4e3f-b6d2-8a1f5c9e7b32";
    static constexpr double kBend = 30.0;
    ParameterId lift, rise, run, thick;
    ObjectId floor, barProfile, barPath, bar, startSketch, startBoss, endSketch, endBoss, sideSketch, sideBoss,
        acrossSketch, acrossBoss, bendSketch, bendBoss;
    EntityId bottom, right, top, left, up, bend, across;

    SweptBarModel() : FaceKindModel(kDocumentId, "SweptBar") {
        using namespace bettercad::literals;
        using namespace bettercad::features;
        lift = doc.createParameter("lift", 10_mm, units::mm).value();
        rise = doc.createParameter("rise", 40_mm, units::mm).value();
        run = doc.createParameter("run", 50_mm, units::mm).value();
        thick = doc.createParameter("thick", 8_mm, units::mm).value();

        floor = add(DatumPlane::create("Floor", {.kind = DatumPlaneKind::Offset, .base = {}, .offsetParameter = lift}));
        auto profile = std::make_unique<sketch::Sketch>("BarProfile");
        const auto lines = addSizedRectangle(*profile, -5.0, -4.0, 10.0, 8.0, std::nullopt, thick);
        bottom = lines[0];
        right = lines[1];
        top = lines[2];
        left = lines[3];
        REQUIRE(profile->setAttachment(PlaneReference{.object = floor}).has_value());
        barProfile = doc.addObject(std::move(profile)).value();

        auto route = std::make_unique<sketch::Sketch>("BarPath", Frame3D::xz());
        const auto point = [&](double u, double v) {
            return require(route->addPoint(Point2D{u * units::mm, v * units::mm}));
        };
        const auto driven = [&](ConstraintId constraint, ParameterId parameter) {
            REQUIRE(route->setConstraintParameter(constraint, parameter).has_value());
        };
        const EntityId origin = point(0, 0);
        const EntityId p0 = point(0, 10);
        const EntityId p1 = point(0, 50);
        const EntityId c = point(-30, 50);
        const EntityId p2 = point(-30, 80);
        const EntityId p3 = point(-80, 80);
        require(route->addFixed(origin));
        require(route->addVertical(origin, p0));
        driven(require(route->addDistance(origin, p0, 1_mm)), lift);
        up = require(route->addLine(p0, p1));
        require(route->addVertical(up));
        driven(require(route->addDistance(up, 1_mm)), rise);
        require(route->addHorizontal(p1, c));
        require(route->addDistance(p1, c, 30_mm));
        bend = require(route->addArc(c, p1, p2));
        require(route->addVertical(c, p2)); // with the arc: p2 = c + (0, 30)
        across = require(route->addLine(p2, p3));
        require(route->addHorizontal(across));
        driven(require(route->addDistance(across, 1_mm)), run);
        barPath = doc.addObject(std::move(route)).value();

        bar = add(SweepFeature::create(
            "Bar", {.profile = sketchOf(barProfile), .path = {.sketch = sketchOf(barPath), .edges = {up, bend, across}}}));

        startSketch = addCircleSketch("StartSketch", faceOf(bar, {.role = FaceRole::StartCap}), 0.0, 0.0, 3.0);
        startBoss = addBoss("StartBoss", startSketch, 4.0);
        endSketch = addCircleSketch("EndSketch", faceOf(bar, {.role = FaceRole::EndCap}), 0.0, -80.0, 3.0);
        endBoss = addBoss("EndBoss", endSketch, 6.0);
        sideSketch = addCircleSketch("SideSketch", faceOf(bar, side(right, up)), 0.0, 30.0, 3.0);
        sideBoss = addBoss("SideBoss", sideSketch, 4.0);
        acrossSketch = addCircleSketch("AcrossSketch", faceOf(bar, side(right, across)), -55.0, 0.0, 3.0);
        acrossBoss = addBoss("AcrossBoss", acrossSketch, 5.0);
        bendSketch = addCircleSketch("BendSketch", faceOf(bar, side(top, bend)), -9.0, -71.0, 3.0);
        bendBoss = addBoss("BendBoss", bendSketch, 5.0);
    }

    static FaceSelector side(EntityId entity, EntityId along) {
        return {.role = FaceRole::Side, .entity = entity, .along = along};
    }

    /// A = 10 t along the path P + pi R / 2 + Q (the profile's centroid, x = 0,
    /// is on the path; its y offset lies along the bend's axis).
    static double volume(double riseMm, double runMm, double thickMm) {
        return 10.0 * thickMm * (riseMm + kPi * kBend / 2.0 + runMm);
    }

    /// Up, Bend and Across, each at its own centroid. The bend's is the
    /// annular sector's (radii R -+ 5, half-angle 45 deg), along the diagonal
    /// (1, 0, 1) / sqrt 2 from the bend's axis at c = (-R, y, e + P).
    static Vec3 centre(double liftMm, double riseMm, double runMm, double thickMm) {
        const double area = 10.0 * thickMm;
        const double y = thickMm / 2.0 - 4.0;
        const double radial = sectorCentroid(kBend - 5.0, kBend + 5.0, kPi / 4.0) * kHalfRoot2;
        return weightedCentre({
            {area * riseMm, {0.0, y, liftMm + riseMm / 2.0}},
            {area * kPi * kBend / 2.0, {-kBend + radial, y, liftMm + riseMm + radial}},
            {area * runMm, {-kBend - runMm / 2.0, y, liftMm + riseMm + kBend}},
        });
    }

    static std::pair<Vec3, Vec3> bounds(double liftMm, double riseMm, double runMm, double thickMm) {
        return {{-kBend - runMm, -4.0, liftMm}, {5.0, thickMm - 4.0, liftMm + riseMm + kBend + 5.0}};
    }

    [[nodiscard]] std::vector<ExpectedBoss> bosses(double liftMm, double riseMm, double runMm,
                                                   double thickMm) const {
        const double ceiling = liftMm + riseMm + kBend + 5.0;
        const double end = -kBend - runMm;
        return {
            {startSketch, startBoss, {{0, 0, liftMm}, {1, 0, 0}, {0, -1, 0}, {0, 0, -1}},
             {{0.0, 0.0, liftMm}, {0, 0, -1}, 3.0, 4.0}},
            {endSketch, endBoss, {{end, 0, 0}, {0, 1, 0}, {0, 0, -1}, {-1, 0, 0}},
             {{end, 0.0, 80.0}, {-1, 0, 0}, 3.0, 6.0}},
            {sideSketch, sideBoss, {{5, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 0}},
             {{5.0, 0.0, 30.0}, {1, 0, 0}, 3.0, 4.0}},
            {acrossSketch, acrossBoss, {{0, 0, ceiling}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
             {{-55.0, 0.0, ceiling}, {0, 0, 1}, 3.0, 5.0}},
            {bendSketch, bendBoss, {{0, thickMm - 4.0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
             {{-9.0, thickMm - 4.0, 71.0}, {0, 1, 0}, 3.0, 5.0}},
        };
    }
};

// A square frustum lofted between two raised squares, with a boss on each
// end:
//
//   base_offset, top_offset   5, 35 mm
//   BottomSquare  XY plane: square of side 40 about the origin
//   TopSquare     XY plane: square of side 20 about the origin
//   Frustum       ruled loft from BottomSquare raised by base_offset to
//                 TopSquare raised by top_offset
//   TopSketch     on Frustum's end cap (z = top_offset, facing +Z): circle
//                 r 4 about (-3, 4)
//   TopBoss       extrude 5 mm
//   BottomSketch  on Frustum's start cap (z = base_offset, facing -Z; sketch
//                 Y = -Y): circle r 4 about (5, 6)
//   BottomBoss    extrude 3 mm
//
// A loft's sides are not named (they are B-splines, P11).
// IDs: base_offset 1, top_offset 2, BottomSquare 3, TopSquare 4, Frustum 5,
// TopSketch 6, TopBoss 7, BottomSketch 8, BottomBoss 9.
struct LoftedFrustumModel : FaceKindModel {
    static constexpr std::string_view kDocumentId = "5a9d2c68-3e7b-4d1f-8c5a-6b2e9f4d1a73";
    ParameterId baseOffset, topOffset;
    ObjectId bottomSquare, topSquare, frustum, topSketch, topBoss, bottomSketch, bottomBoss;

    LoftedFrustumModel() : FaceKindModel(kDocumentId, "LoftedFrustum") {
        using namespace bettercad::literals;
        using namespace bettercad::features;
        baseOffset = doc.createParameter("base_offset", 5_mm, units::mm).value();
        topOffset = doc.createParameter("top_offset", 35_mm, units::mm).value();
        bottomSquare = doc.addObject(fixedPolygon("BottomSquare", Frame3D::xy(),
                                                  {{-20.0, -20.0}, {20.0, -20.0}, {20.0, 20.0}, {-20.0, 20.0}}))
                           .value();
        topSquare = doc.addObject(fixedPolygon("TopSquare", Frame3D::xy(),
                                               {{-10.0, -10.0}, {10.0, -10.0}, {10.0, 10.0}, {-10.0, 10.0}}))
                        .value();
        frustum = add(LoftFeature::create(
            "Frustum", {.sections = {{.sketch = sketchOf(bottomSquare), .offsetParameter = baseOffset},
                                     {.sketch = sketchOf(topSquare), .offsetParameter = topOffset}}}));
        topSketch = addCircleSketch("TopSketch", faceOf(frustum, {.role = FaceRole::EndCap}), -3.0, 4.0, 4.0);
        topBoss = addBoss("TopBoss", topSketch, 5.0);
        bottomSketch = addCircleSketch("BottomSketch", faceOf(frustum, {.role = FaceRole::StartCap}), 5.0, 6.0, 4.0);
        bottomBoss = addBoss("BottomBoss", bottomSketch, 3.0);
    }

    /// h/3 (A1 + A2 + sqrt(A1 A2)) = 2800 h / 3.
    static double volume(double baseMm, double topMm) { return (topMm - baseMm) * 2800.0 / 3.0; }

    static Vec3 centre(double baseMm, double topMm) {
        return {0.0, 0.0, baseMm + (topMm - baseMm) * frustumCentroid(40.0, 20.0)};
    }

    [[nodiscard]] std::vector<ExpectedBoss> bosses(double baseMm, double topMm) const {
        return {
            {topSketch, topBoss, {{0, 0, topMm}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
             {{-3.0, 4.0, topMm}, {0, 0, 1}, 4.0, 5.0}},
            {bottomSketch, bottomBoss, {{0, 0, baseMm}, {1, 0, 0}, {0, -1, 0}, {0, 0, -1}},
             {{5.0, -6.0, baseMm}, {0, 0, -1}, 4.0, 3.0}},
        };
    }
};

// A block with two blind holes and something standing on each flat face in
// them:
//
//   bore_depth    12 mm
//   BlockSketch   XY plane: rectangle 60 x 40 at the origin
//   Block         extrude 30 mm
//   Bore          hole in Block's top: simple, blind, diameter 10, depth
//                 bore_depth, at (15, 20)
//   Seat          hole in Bore's result, same face: counterbored, blind,
//                 diameter 8, depth 20, counterbore diameter 16 and depth
//                 5, at (45, 20)
//   PinSketch     on Bore's bottom: circle r 3 about (15, 20)
//   Pin           extrude 4 mm
//   CollarSketch  on Seat's counterbore floor: circle r 1.5 about (51, 20)
//   Collar        extrude 3 mm
//   StudSketch    on Seat's bottom: circle r 2 about (45, 20)
//   Stud          extrude 2 mm
//
// Every flat face in a hole faces up (+Z; sketch X = +X, Y = +Y): Bore's
// bottom at z = 30 - bore_depth, Seat's floor at 30 - its counterbore depth,
// Seat's bottom at z = 10. A counterbore's depth has no parameter: editing
// Seat moves its floor.
// IDs: bore_depth 1, BlockSketch 2, Block 3, Bore 4, Seat 5, PinSketch 6,
// Pin 7, CollarSketch 8, Collar 9, StudSketch 10, Stud 11.
struct DrilledBlockModel : FaceKindModel {
    static constexpr std::string_view kDocumentId = "9f1c6e37-2a8d-4b5e-9d7c-4e3a8b2f6c14";
    ParameterId boreDepth;
    ObjectId blockSketch, block, bore, seat, pinSketch, pin, collarSketch, collar, studSketch, stud;

    static geometry::FaceSignature topFace() {
        using namespace bettercad::literals;
        return geometry::planeSignature(Point3D{0_mm, 0_mm, 30_mm}, Direction3D::unitZ());
    }

    DrilledBlockModel() : FaceKindModel(kDocumentId, "DrilledBlock") {
        using namespace bettercad::literals;
        using namespace bettercad::features;
        using geometry::HoleExtent;
        using geometry::HoleType;
        boreDepth = doc.createParameter("bore_depth", 12_mm, units::mm).value();
        blockSketch = addFixedRectangle("BlockSketch", 0.0, 0.0, 60.0, 40.0);
        block = add(ExtrudeFeature::create("Block", {.profile = sketchOf(blockSketch), .depth = 30_mm}));
        bore = add(HoleFeature::create("Bore", {.target = featureOf(block),
                                               .face = topFace(),
                                               .center = Point2D{15_mm, 20_mm},
                                               .extent = HoleExtent::Blind,
                                               .diameter = 10_mm,
                                               .depthParameter = boreDepth}));
        seat = add(HoleFeature::create("Seat", {.target = featureOf(bore),
                                               .face = topFace(),
                                               .center = Point2D{45_mm, 20_mm},
                                               .type = HoleType::Counterbore,
                                               .extent = HoleExtent::Blind,
                                               .diameter = 8_mm,
                                               .depth = 20_mm,
                                               .counterboreDiameter = 16_mm,
                                               .counterboreDepth = 5_mm}));
        pinSketch = addCircleSketch("PinSketch", faceOf(bore, {.role = FaceRole::HoleBottom}), 15.0, 20.0, 3.0);
        pin = addBoss("Pin", pinSketch, 4.0);
        collarSketch =
            addCircleSketch("CollarSketch", faceOf(seat, {.role = FaceRole::CounterboreFloor}), 51.0, 20.0, 1.5);
        collar = addBoss("Collar", collarSketch, 3.0);
        studSketch = addCircleSketch("StudSketch", faceOf(seat, {.role = FaceRole::HoleBottom}), 45.0, 20.0, 2.0);
        stud = addBoss("Stud", studSketch, 2.0);
    }

    /// Seat's body: the block less both holes.
    static double volume(double boreMm, double counterboreMm) {
        return 60.0 * 40.0 * 30.0 - kPi * 25.0 * boreMm - kPi * 64.0 * counterboreMm -
               kPi * 16.0 * (20.0 - counterboreMm);
    }

    static Vec3 centre(double boreMm, double counterboreMm) {
        const double bore = kPi * 25.0 * boreMm;
        const double counterbore = kPi * 64.0 * counterboreMm;
        const double shank = kPi * 16.0 * (20.0 - counterboreMm);
        return weightedCentre({
            {72000.0, {30.0, 20.0, 15.0}},
            {-bore, {15.0, 20.0, 30.0 - boreMm / 2.0}},
            {-counterbore, {45.0, 20.0, 30.0 - counterboreMm / 2.0}},
            {-shank, {45.0, 20.0, (10.0 + 30.0 - counterboreMm) / 2.0}},
        });
    }

    [[nodiscard]] std::vector<ExpectedBoss> bosses(double boreMm, double counterboreMm) const {
        const auto up = [](double z) { return ExpectedFrame{{0, 0, z}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}; };
        return {
            {pinSketch, pin, up(30.0 - boreMm), {{15.0, 20.0, 30.0 - boreMm}, {0, 0, 1}, 3.0, 4.0}},
            {collarSketch, collar, up(30.0 - counterboreMm),
             {{51.0, 20.0, 30.0 - counterboreMm}, {0, 0, 1}, 1.5, 3.0}},
            {studSketch, stud, up(10.0), {{45.0, 20.0, 10.0}, {0, 0, 1}, 2.0, 2.0}},
        };
    }
};

// A block with both top long edges chamfered and a boss on each chamfer:
//
//   bevel         4 mm
//   BlockSketch   XY plane: rectangle 60 x 40 at the origin
//   Block         extrude 30 mm
//   Bevel         equal-distance chamfer by bevel of Block's top front edge
//                 (edge reference 1: y = 0, z = 30) and top back edge
//                 (edge reference 2: y = 40, z = 30)
//   FrontSketch   on the face of edge reference 1: circle r 1 about (30, 21)
//   FrontBoss     extrude 3 mm
//   BackSketch    on the face of edge reference 2: circle r 1 about (30, 7)
//   BackBoss      extrude 3 mm
//
// With d = bevel and s = 1/sqrt 2: the front chamfer is the plane z - y =
// 30 - d facing (0, -s, s), its frame at (0, -(30 - d)/2, (30 - d)/2) with
// X = +X and Y = (0, s, s); the strip spans v in [(30 - d) s, (30 + d) s].
// The back chamfer is y + z = 70 - d facing (0, s, s), its frame at
// (0, (70 - d)/2, (70 - d)/2) with Y = (0, s, -s); v in [(10 - d) s,
// (10 + d) s].
// IDs: bevel 1, BlockSketch 2, Block 3, Bevel 4, FrontSketch 5,
// FrontBoss 6, BackSketch 7, BackBoss 8.
struct BevelledBlockModel : FaceKindModel {
    static constexpr std::string_view kDocumentId = "3b7e1a92-6d4c-4f2a-a8e5-1c9d7b3e5f26";
    ParameterId bevel;
    ObjectId blockSketch, block, chamfer, frontSketch, frontBoss, backSketch, backBoss;

    static geometry::EdgeSignature topEdge(double yMm) {
        return geometry::lineSignature(Point3D{Length{}, yMm * units::mm, 30.0 * units::mm}, Direction3D::unitX());
    }

    BevelledBlockModel() : FaceKindModel(kDocumentId, "BevelledBlock") {
        using namespace bettercad::literals;
        using namespace bettercad::features;
        bevel = doc.createParameter("bevel", 4_mm, units::mm).value();
        blockSketch = addFixedRectangle("BlockSketch", 0.0, 0.0, 60.0, 40.0);
        block = add(ExtrudeFeature::create("Block", {.profile = sketchOf(blockSketch), .depth = 30_mm}));
        chamfer = add(ChamferFeature::create("Bevel", {.target = featureOf(block),
                                                      .edges = { features::ChamferEdge{topEdge(0.0)}, features::ChamferEdge{topEdge(40.0)}},
                                                      .distanceParameter = bevel}));
        frontSketch = addCircleSketch("FrontSketch", faceOf(chamfer, edgeFace(1)), 30.0, 21.0, 1.0);
        frontBoss = addBoss("FrontBoss", frontSketch, 3.0);
        backSketch = addCircleSketch("BackSketch", faceOf(chamfer, edgeFace(2)), 30.0, 7.0, 1.0);
        backBoss = addBoss("BackBoss", backSketch, 3.0);
    }

    /// The bevel's two selections are identified 1 and 2: this model builds
    /// the chamfer in one place with both edges unidentified, and
    /// ChamferFeature::create allocates them in list order. What the id means
    /// is a selection, not a position, and nothing here depends on the two
    /// coinciding beyond this fixture (ADR-024).
    static FaceSelector edgeFace(std::uint64_t selection) {
        return {.role = FaceRole::Chamfer, .edge = ChamferEdgeId::fromValue(selection)};
    }

    /// Two triangular prisms (d^2 / 2 x 60) off the block.
    static double volume(double bevelMm) { return 72000.0 - 60.0 * bevelMm * bevelMm; }

    /// Each prism's centroid lies d/3 in from both faces at its edge.
    static Vec3 centre(double bevelMm) {
        const double prism = 30.0 * bevelMm * bevelMm;
        return weightedCentre({
            {72000.0, {30.0, 20.0, 15.0}},
            {-prism, {30.0, bevelMm / 3.0, 30.0 - bevelMm / 3.0}},
            {-prism, {30.0, 40.0 - bevelMm / 3.0, 30.0 - bevelMm / 3.0}},
        });
    }

    [[nodiscard]] std::vector<ExpectedBoss> bosses(double bevelMm) const {
        const double s = kHalfRoot2;
        const double front = (30.0 - bevelMm) / 2.0;
        const double back = (70.0 - bevelMm) / 2.0;
        const Vec3 frontNormal{0.0, -s, s};
        const Vec3 backNormal{0.0, s, s};
        return {
            {frontSketch, frontBoss, {{0, -front, front}, {1, 0, 0}, {0, s, s}, frontNormal},
             {{30.0, -front + 21.0 * s, front + 21.0 * s}, frontNormal, 1.0, 3.0}},
            {backSketch, backBoss, {{0, back, back}, {1, 0, 0}, {0, s, -s}, backNormal},
             {{30.0, back + 7.0 * s, back - 7.0 * s}, backNormal, 1.0, 3.0}},
        };
    }
};

// A row of posts on a plate, mirrored, with sketches on the copies:
//
//   pitch 30 mm, posts 3
//   PlateSketch   XY plane: rectangle 100 x 30 at the origin
//   Plate         extrude 10 mm
//   PostSketch    on Plate's end cap: rectangle x 10..20, y 10..20; lines
//                 front (y = 10), right (x = 20), back (y = 20), left
//   Post          extrude 20 mm, joined to Plate
//   Row           linear pattern of Post along +X: posts instances, pitch
//                 apart
//   Flip          mirror of Row's body in the plane x = 100, keeping it
//   CopySketch    on the right side of Row's copy 2: circle r 3 about (15, 20)
//   CopyBoss      extrude 4 mm
//   ImageSketch   on that face's image in Flip: circle r 3 about (15, -20)
//   ImageBoss     extrude 4 mm
//   Gauge         datum plane 5 mm out from the right side of Row's copy 1
//   GaugeSketch   on Gauge: circle r 2 about (15, 20)
//   GaugeBoss     extrude 3 mm
//
// Copy k's right side is the plane x = 20 + k pitch facing +X (sketch X =
// +Y, Y = +Z); its image is x = 180 - 2 pitch facing -X (Y = -Z). Gauge
// lies at x = 25 + pitch. Every post's top is at z = 30, so the copies are
// told apart by their sides.
// IDs: pitch 1, posts 2, PlateSketch 3, Plate 4, PostSketch 5, Post 6,
// Row 7, Flip 8, CopySketch 9, CopyBoss 10, ImageSketch 11, ImageBoss 12,
// Gauge 13, GaugeSketch 14, GaugeBoss 15.
struct PostRowModel : FaceKindModel {
    static constexpr std::string_view kDocumentId = "6d2f8b45-1e9a-4c7d-b3f6-9a5e2c8d4b87";
    ParameterId pitch, posts;
    ObjectId plateSketch, plate, postSketch, post, row, flip, copySketch, copyBoss, imageSketch, imageBoss, gauge,
        gaugeSketch, gaugeBoss;
    EntityId front, right, back, left;

    PostRowModel() : FaceKindModel(kDocumentId, "PostRow") {
        using namespace bettercad::literals;
        using namespace bettercad::features;
        pitch = doc.createParameter("pitch", 30_mm, units::mm).value();
        posts = doc.createParameter("posts", 3.0, kUnitless).value();
        plateSketch = addFixedRectangle("PlateSketch", 0.0, 0.0, 100.0, 30.0);
        plate = add(ExtrudeFeature::create("Plate", {.profile = sketchOf(plateSketch), .depth = 10_mm}));
        std::array<EntityId, 4> lines{};
        postSketch = addFixedRectangle("PostSketch", 10.0, 10.0, 10.0, 10.0, &lines,
                                       faceOf(plate, {.role = FaceRole::EndCap}));
        front = lines[0];
        right = lines[1];
        back = lines[2];
        left = lines[3];
        post = add(ExtrudeFeature::create("Post", {.profile = sketchOf(postSketch),
                                                   .depth = 20_mm,
                                                   .operation = FeatureOperation::Join,
                                                   .target = featureOf(plate)}));
        row = add(LinearPatternFeature::create(
            "Row", {.source = featureOf(post),
                    .first = {.direction = {1.0, 0.0, 0.0}, .countParameter = posts, .spacingParameter = pitch}}));
        flip = add(MirrorFeature::create("Flip", {.source = featureOf(row),
                                                  .plane = {.origin = Point3D{100_mm, 0_mm, 0_mm},
                                                            .normal = {1.0, 0.0, 0.0}},
                                                  .scope = MirrorScope::Body}));

        copySketch = addCircleSketch("CopySketch", faceOf(post, rightOf({{row, 2}})), 15.0, 20.0, 3.0);
        copyBoss = addBoss("CopyBoss", copySketch, 4.0);
        imageSketch = addCircleSketch("ImageSketch", faceOf(post, rightOf({{row, 2}, {flip, 1}})), 15.0, -20.0, 3.0);
        imageBoss = addBoss("ImageBoss", imageSketch, 4.0);
        gauge = add(DatumPlane::create(
            "Gauge", {.kind = DatumPlaneKind::Offset, .base = faceOf(post, rightOf({{row, 1}})), .offset = 5_mm}));
        gaugeSketch = addCircleSketch("GaugeSketch", PlaneReference{.object = gauge}, 15.0, 20.0, 2.0);
        gaugeBoss = addBoss("GaugeBoss", gaugeSketch, 3.0);
    }

    /// Post's right side, as copied by @p copies.
    [[nodiscard]] FaceSelector rightOf(std::vector<FaceCopy> copies) const {
        return {.role = FaceRole::Side, .entity = right, .copies = std::move(copies)};
    }

    /// Row's body: the plate and its posts.
    static double rowVolume(double postCount) { return 30000.0 + 2000.0 * postCount; }

    static Vec3 rowCentre(double pitchMm, double postCount) {
        std::vector<std::pair<double, Vec3>> parts{{30000.0, {50.0, 15.0, 5.0}}};
        for (int k = 0; k < static_cast<int>(postCount); ++k) {
            parts.push_back({2000.0, {15.0 + k * pitchMm, 15.0, 20.0}});
        }
        return weightedCentre(parts);
    }

    [[nodiscard]] std::vector<ExpectedBoss> bosses(double pitchMm) const {
        const double copy = 20.0 + 2.0 * pitchMm;
        const double image = 180.0 - 2.0 * pitchMm;
        const double plane = 25.0 + pitchMm;
        return {
            {copySketch, copyBoss, {{copy, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 0}},
             {{copy, 15.0, 20.0}, {1, 0, 0}, 3.0, 4.0}},
            {imageSketch, imageBoss, {{image, 0, 0}, {0, 1, 0}, {0, 0, -1}, {-1, 0, 0}},
             {{image, 15.0, 20.0}, {-1, 0, 0}, 3.0, 4.0}},
            {gaugeSketch, gaugeBoss, {{plane, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 0}},
             {{plane, 15.0, 20.0}, {1, 0, 0}, 2.0, 3.0}},
        };
    }
};

// Lugs patterned round a hub, with a sketch on a copy's side:
//
//   spokes 4
//   HubSketch     XY plane: circle r 40 about the origin
//   Hub           extrude 10 mm
//   LugSketch     on Hub's end cap: rectangle x 20..30, y -5..5; lines
//                 lower (y = -5), outer, upper (y = 5), inner
//   Lug           extrude 10 mm, joined to Hub
//   Spokes        circular pattern of Lug about +Z: spokes instances round
//                 the full circle
//   SpokeSketch   on the upper side of Spokes' copy 1: circle r 3 about
//                 (25, -15)
//   SpokeBoss     extrude 4 mm
//
// Copy 1 lies at a = 360 deg / spokes; its upper side faces
// n = (-sin a, cos a, 0), 5 mm from the axis. While a is within 45 deg of
// 90 deg the face is nearer facing X than Y, so its sketch X axis is the
// radial direction (cos a, sin a, 0) and Y = -Z; the frame starts at 5 n.
// IDs: spokes 1, HubSketch 2, Hub 3, LugSketch 4, Lug 5, Spokes 6,
// SpokeSketch 7, SpokeBoss 8.
struct SpokeHubModel : FaceKindModel {
    static constexpr std::string_view kDocumentId = "8e4a3c19-7f2b-4a6d-9e1c-5b8d3f7a2e68";
    ParameterId spokes;
    ObjectId hubSketch, hub, lugSketch, lug, pattern, spokeSketch, spokeBoss;
    EntityId lower, outer, upper, inner;

    SpokeHubModel() : FaceKindModel(kDocumentId, "SpokeHub") {
        using namespace bettercad::literals;
        using namespace bettercad::features;
        spokes = doc.createParameter("spokes", 4.0, kUnitless).value();
        auto disc = std::make_unique<sketch::Sketch>("HubSketch");
        const EntityId circle = require(disc->addCircle(Point2D{}, 40_mm));
        require(disc->addFixed(std::get<sketch::CircleEntity>(disc->findEntity(circle)->geometry).center));
        require(disc->addRadius(circle, 40_mm));
        hubSketch = doc.addObject(std::move(disc)).value();
        hub = add(ExtrudeFeature::create("Hub", {.profile = sketchOf(hubSketch), .depth = 10_mm}));
        std::array<EntityId, 4> lines{};
        lugSketch =
            addFixedRectangle("LugSketch", 20.0, -5.0, 10.0, 10.0, &lines, faceOf(hub, {.role = FaceRole::EndCap}));
        lower = lines[0];
        outer = lines[1];
        upper = lines[2];
        inner = lines[3];
        lug = add(ExtrudeFeature::create("Lug", {.profile = sketchOf(lugSketch),
                                                 .depth = 10_mm,
                                                 .operation = FeatureOperation::Join,
                                                 .target = featureOf(hub)}));
        pattern = add(CircularPatternFeature::create(
            "Spokes", {.source = featureOf(lug),
                       .axis = {.origin = Point3D{}, .direction = {0.0, 0.0, 1.0}},
                       .countParameter = spokes}));
        spokeSketch = addCircleSketch(
            "SpokeSketch",
            faceOf(lug, {.role = FaceRole::Side, .entity = upper, .copies = {{pattern, 1}}}), 25.0, -15.0, 3.0);
        spokeBoss = addBoss("SpokeBoss", spokeSketch, 4.0);
    }

    static double volume(double count) { return 16000.0 * kPi + 1000.0 * count; }

    /// The lugs balance about the axis.
    static Vec3 centre(double count) {
        return weightedCentre({{16000.0 * kPi, {0.0, 0.0, 5.0}}, {1000.0 * count, {0.0, 0.0, 15.0}}});
    }

    [[nodiscard]] std::vector<ExpectedBoss> bosses(double count) const {
        const double a = 2.0 * kPi / count;
        const Vec3 n{-std::sin(a), std::cos(a), 0.0};
        const Vec3 radial{std::cos(a), std::sin(a), 0.0};
        return {
            {spokeSketch, spokeBoss, {{5.0 * n[0], 5.0 * n[1], 0.0}, radial, {0, 0, -1}, n},
             {{5.0 * n[0] + 25.0 * radial[0], 5.0 * n[1] + 25.0 * radial[1], 15.0}, n, 3.0, 4.0}},
        };
    }
};

} // namespace bettercad::test
