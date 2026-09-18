#pragma once

#include "features/FeatureTestSupport.hpp"
#include "support/BlockModel.hpp"
#include "support/SweepModels.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/parameters/Parameter.hpp>
#include <bettercad/features/LoftFeature.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

namespace bettercad::test {

/// pi h/3 (r1^2 + r1 r2 + r2^2), in mm^3.
inline double frustumVolume(double r1, double r2, double h) {
    return std::numbers::pi * h / 3.0 * (r1 * r1 + r1 * r2 + r2 * r2);
}

/// Fraction of a circular frustum's height at which its centroid lies,
/// measured from the r1 end: (r1^2 + 2 r1 r2 + 3 r2^2) / (4 (r1^2 + r1 r2 + r2^2)).
inline double frustumCentroid(double r1, double r2) {
    return (r1 * r1 + 2.0 * r1 * r2 + 3.0 * r2 * r2) / (4.0 * (r1 * r1 + r1 * r2 + r2 * r2));
}

/// A sketch on @p plane with a rectangle whose first corner is fixed at the
/// plane's origin, its width and height driven by the parameters.
inline std::unique_ptr<sketch::Sketch> drivenRectangle(const std::string& name, const Frame3D& plane,
                                                       ParameterId width, ParameterId height) {
    using namespace bettercad::literals;
    using namespace bettercad::sketch;
    auto rectangle = std::make_unique<Sketch>(name, plane);
    const auto lines = addRectangle(*rectangle, 0_mm, 0_mm, 5_mm, 5_mm);
    require(rectangle->addFixed(std::get<LineEntity>(rectangle->findEntity(lines[0])->geometry).start));
    require(rectangle->addHorizontal(lines[0]));
    require(rectangle->addHorizontal(lines[2]));
    require(rectangle->addVertical(lines[1]));
    require(rectangle->addVertical(lines[3]));
    const ConstraintId w = require(rectangle->addDistance(lines[0], 1_mm));
    const ConstraintId h = require(rectangle->addDistance(lines[1], 1_mm));
    REQUIRE(rectangle->setConstraintParameter(w, width).has_value());
    REQUIRE(rectangle->setConstraintParameter(h, height).has_value());
    return rectangle;
}

/// A sketch on @p plane with a closed polygon of fixed corners (in mm, in
/// order). Returns the sketch; its lines are the polygon's sides.
inline std::unique_ptr<sketch::Sketch> fixedPolygon(const std::string& name, const Frame3D& plane,
                                                    const std::vector<std::pair<double, double>>& corners) {
    auto sketch = std::make_unique<sketch::Sketch>(name, plane);
    std::vector<EntityId> points;
    for (const auto& [u, v] : corners) {
        points.push_back(require(sketch->addPoint(Point2D{u * units::mm, v * units::mm})));
        require(sketch->addFixed(points.back()));
    }
    for (std::size_t i = 0; i < points.size(); ++i) {
        require(sketch->addLine(points[i], points[(i + 1) % points.size()]));
    }
    return sketch;
}

/// A sketch on @p plane with a circle about the fixed point (u, v) mm whose
/// radius is driven by @p radius.
inline std::unique_ptr<sketch::Sketch> drivenCircle(const std::string& name, const Frame3D& plane, double u,
                                                    double v, ParameterId radius) {
    auto sketch = std::make_unique<sketch::Sketch>(name, plane);
    addDrivenCircle(*sketch, Point2D{u * units::mm, v * units::mm}, radius);
    return sketch;
}

/// The XY plane moved to z (mm), facing +Z (or -Z with @p down).
inline Frame3D levelPlane(double z, bool down = false) {
    return Frame3D::create(Point3D{Length{}, Length{}, z * units::mm},
                           down ? Direction3D::unitZ().reversed() : Direction3D::unitZ(), Direction3D::unitX())
        .value();
}

/// Common helpers of the loft fixtures.
struct LoftModelBase {
    Document doc;

    explicit LoftModelBase(std::string name) : doc(std::move(name)) {}

    ObjectId addSketch(std::unique_ptr<sketch::Sketch> sketch) { return doc.addObject(std::move(sketch)).value(); }

    ObjectId addLoft(const std::string& name, const features::LoftDefinition& d) {
        auto feature = features::LoftFeature::create(name, d);
        REQUIRE(feature.has_value());
        return doc.addObject(std::move(*feature)).value();
    }

    [[nodiscard]] features::LoftDefinition definitionOf(ObjectId id) const {
        return doc.findObjectAs<features::LoftFeature>(id)->definition();
    }

    void setDefinition(ObjectId id, const features::LoftDefinition& d) {
        REQUIRE(doc.modifyObject<features::LoftFeature>(id, [&](features::LoftFeature& f) {
                       return f.setDefinition(d);
                   }).has_value());
    }
};

// Two equal rectangles, a prism:
//
//   width, height -> Bottom (XY plane: rectangle with its corner fixed at the origin)
//   width, height -> Top (the same rectangle, drawn again)
//   Bottom, Top (moved up by length) -> Loft (new body)
//
// The loft is the box [0, 10] x [0, 20] x [0, 100]: V = A h = 20000. IDs:
// width 1, height 2, length 3, Bottom 4, Top 5, Loft 6.
struct RectangleLoftModel : LoftModelBase {
    ParameterId width, height, length;
    ObjectId bottom, top, loft;

    RectangleLoftModel() : LoftModelBase("Prism") {
        using namespace bettercad::literals;
        width = doc.createParameter("width", 10_mm, units::mm).value();
        height = doc.createParameter("height", 20_mm, units::mm).value();
        length = doc.createParameter("length", 100_mm, units::mm).value();
        bottom = addSketch(drivenRectangle("Bottom", Frame3D::xy(), width, height));
        top = addSketch(drivenRectangle("Top", Frame3D::xy(), width, height));
        loft = addLoft("Loft", definition());
    }

    [[nodiscard]] features::LoftDefinition definition() const {
        return {.sections = {{.sketch = sketchIdOf(bottom)}, {.sketch = sketchIdOf(top), .offsetParameter = length}}};
    }
};

// A circular frustum:
//
//   r1 -> Bottom (XY plane: circle about the fixed origin)
//   r2 -> Top (XY plane: circle about the fixed origin)
//   Bottom, Top (moved up by height) -> Loft (new body)
//
// V = pi h/3 (r1^2 + r1 r2 + r2^2) = 1750 pi for r1 = 10, r2 = 5, h = 30.
// IDs: r1 1, r2 2, height 3, Bottom 4, Top 5, Loft 6.
struct FrustumLoftModel : LoftModelBase {
    ParameterId r1, r2, height;
    ObjectId bottom, top, loft;

    explicit FrustumLoftModel(features::FeatureOperation operation = features::FeatureOperation::NewBody)
        : LoftModelBase("Frustum") {
        using namespace bettercad::literals;
        r1 = doc.createParameter("r1", 10_mm, units::mm).value();
        r2 = doc.createParameter("r2", 5_mm, units::mm).value();
        height = doc.createParameter("height", 30_mm, units::mm).value();
        bottom = addSketch(drivenCircle("Bottom", Frame3D::xy(), 0, 0, r1));
        top = addSketch(drivenCircle("Top", Frame3D::xy(), 0, 0, r2));
        features::LoftDefinition d = definition();
        d.operation = operation;
        loft = addLoft("Loft", d);
    }

    [[nodiscard]] features::LoftDefinition definition() const {
        return {.sections = {{.sketch = sketchIdOf(bottom)}, {.sketch = sketchIdOf(top), .offsetParameter = height}}};
    }
};

// Three circular sections, r 5, 10, 5 at z 0, 50, 100: two frustums.
//
//   rEnd -> Bottom, Top; rMid -> Middle (all on the XY plane, about the origin)
//   Bottom, Middle (moved up by mid), Top (moved up by length) -> Loft
//
// IDs: rEnd 1, rMid 2, mid 3, length 4, Bottom 5, Middle 6, Top 7, Loft 8.
struct ThreeSectionLoftModel : LoftModelBase {
    ParameterId rEnd, rMid, mid, length;
    ObjectId bottom, middle, top, loft;

    ThreeSectionLoftModel() : LoftModelBase("Barrel") {
        using namespace bettercad::literals;
        rEnd = doc.createParameter("r_end", 5_mm, units::mm).value();
        rMid = doc.createParameter("r_mid", 10_mm, units::mm).value();
        mid = doc.createParameter("mid", 50_mm, units::mm).value();
        length = doc.createParameter("length", 100_mm, units::mm).value();
        bottom = addSketch(drivenCircle("Bottom", Frame3D::xy(), 0, 0, rEnd));
        middle = addSketch(drivenCircle("Middle", Frame3D::xy(), 0, 0, rMid));
        top = addSketch(drivenCircle("Top", Frame3D::xy(), 0, 0, rEnd));
        loft = addLoft("Loft", definition());
    }

    [[nodiscard]] features::LoftDefinition definition() const {
        return {.sections = {{.sketch = sketchIdOf(bottom)},
                             {.sketch = sketchIdOf(middle), .offsetParameter = mid},
                             {.sketch = sketchIdOf(top), .offsetParameter = length}}};
    }
};

// An oblique frustum whose top circle is moved sideways by a parameter:
//
//   r1 -> Bottom (circle about the fixed origin)
//   r2, shift -> Top (a fixed point at (-10, 0); the circle's centre level
//                with it, shift away: centre x = shift - 10)
//   Bottom, Top (moved up by height) -> Loft
//
// shift = 10 puts the top centre over the origin; shift = 20 moves it to
// x = 10. Cavalieri: V = pi h/3 (r1^2 + r1 r2 + r2^2) either way. IDs: r1 1,
// r2 2, height 3, shift 4, Bottom 5, Top 6, Loft 7.
struct OffsetLoftModel : LoftModelBase {
    ParameterId r1, r2, height, shift;
    ObjectId bottom, top, loft;

    OffsetLoftModel() : LoftModelBase("Lean") {
        using namespace bettercad::literals;
        r1 = doc.createParameter("r1", 10_mm, units::mm).value();
        r2 = doc.createParameter("r2", 5_mm, units::mm).value();
        height = doc.createParameter("height", 30_mm, units::mm).value();
        shift = doc.createParameter("shift", 10_mm, units::mm).value();
        bottom = addSketch(drivenCircle("Bottom", Frame3D::xy(), 0, 0, r1));

        auto sketch = std::make_unique<sketch::Sketch>("Top");
        const EntityId anchor = require(sketch->addPoint(Point2D{-(10_mm), 0_mm}));
        require(sketch->addFixed(anchor));
        const EntityId circle = require(sketch->addCircle(Point2D{}, 1_mm));
        const EntityId center = std::get<sketch::CircleEntity>(sketch->findEntity(circle)->geometry).center;
        require(sketch->addHorizontal(anchor, center));
        const ConstraintId d = require(sketch->addDistance(anchor, center, 1_mm));
        REQUIRE(sketch->setConstraintParameter(d, shift).has_value());
        const ConstraintId r = require(sketch->addRadius(circle, 1_mm));
        REQUIRE(sketch->setConstraintParameter(r, r2).has_value());
        top = addSketch(std::move(sketch));
        loft = addLoft("Loft", {.sections = {{.sketch = sketchIdOf(bottom)},
                                             {.sketch = sketchIdOf(top), .offsetParameter = height}}});
    }
};

// Similar rectangles centred on the Z axis: 20 x 10 at z = 0 to 10 x 5 at
// z = height = 30, a pyramidal frustum: V = h/3 (A1 + A2 + sqrt(A1 A2)) =
// 3500. IDs: height 1, Bottom 2, Top 3, Loft 4.
struct RectangularFrustumModel : LoftModelBase {
    ParameterId height;
    ObjectId bottom, top, loft;

    RectangularFrustumModel() : LoftModelBase("Taper") {
        using namespace bettercad::literals;
        height = doc.createParameter("height", 30_mm, units::mm).value();
        bottom = addSketch(fixedPolygon("Bottom", Frame3D::xy(), {{-10, -5}, {10, -5}, {10, 5}, {-10, 5}}));
        top = addSketch(fixedPolygon("Top", Frame3D::xy(), {{-5, -2.5}, {5, -2.5}, {5, 2.5}, {-5, 2.5}}));
        loft = addLoft("Loft", {.sections = {{.sketch = sketchIdOf(bottom)},
                                             {.sketch = sketchIdOf(top), .offsetParameter = height}}});
    }
};

// The parametric block (see BlockModel) with a tapered boss joined to its
// top: a 40 x 20 rectangle on the top face (z = 20) lofted to a 20 x 10 one
// `rise` above it, both centred at (50, 25). V = 100000 + h/3 (800 + 200 +
// 400) = 114000 for rise = 30. IDs: width 1, length 2, height 3, rise 4,
// Base 5, Pad 6, BossBottom 7, BossTop 8, Boss 9.
struct LoftBossModel : BlockModel {
    ObjectId bossBottom, bossTop, boss;

    LoftBossModel() : BlockModel("rise", Length::fromSi(0.030)) {
        const Frame3D top = levelPlane(20);
        bossBottom = doc.addObject(fixedPolygon("BossBottom", top, {{30, 15}, {70, 15}, {70, 35}, {30, 35}})).value();
        bossTop = doc.addObject(fixedPolygon("BossTop", top, {{40, 20}, {60, 20}, {60, 30}, {40, 30}})).value();
        boss = add<features::LoftFeature>(
            "Boss", {.sections = {{.sketch = sketchIdOf(bossBottom)}, {.sketch = sketchIdOf(bossTop), .offsetParameter = extra}},
                     .operation = features::FeatureOperation::Join,
                     .target = featureId(pad)});
    }
};

// The parametric block with a tapered blind hole: a circle r 8 on the top
// face (z = 20) lofted to a circle r 4 `depth` below it, both about
// (50, 25). The deeper section's sketch faces down (normal -Z), so its
// positive offset moves it into the block. Removed: pi h/3 (64 + 32 + 16) =
// 560 pi for depth = 15; V = 100000 - 560 pi. IDs: width 1, length 2,
// height 3, depth 4, Base 5, Pad 6, Mouth 7, Tip 8, Taper 9.
struct TaperedHoleModel : BlockModel {
    ObjectId mouth, tip, taper;

    explicit TaperedHoleModel(features::FeatureOperation operation = features::FeatureOperation::Cut)
        : BlockModel("depth", Length::fromSi(0.015)) {
        using namespace bettercad::literals;
        auto upper = std::make_unique<sketch::Sketch>("Mouth", levelPlane(20));
        const EntityId c1 = require(upper->addCircle(Point2D{50_mm, 25_mm}, 8_mm));
        require(upper->addFixed(std::get<sketch::CircleEntity>(upper->findEntity(c1)->geometry).center));
        require(upper->addRadius(c1, 8_mm));
        mouth = doc.addObject(std::move(upper)).value();
        // Facing down, the sketch's X axis is +X, so its v axis is -Y: the
        // centre (50, 25) is at (50, -25) there.
        auto lower = std::make_unique<sketch::Sketch>("Tip", levelPlane(20, true));
        const EntityId c2 = require(lower->addCircle(Point2D{50_mm, -(25_mm)}, 4_mm));
        require(lower->addFixed(std::get<sketch::CircleEntity>(lower->findEntity(c2)->geometry).center));
        require(lower->addRadius(c2, 4_mm));
        tip = doc.addObject(std::move(lower)).value();
        taper = add<features::LoftFeature>(
            "Taper", {.sections = {{.sketch = sketchIdOf(mouth)}, {.sketch = sketchIdOf(tip), .offsetParameter = extra}},
                      .operation = operation,
                      .target = featureId(pad)});
    }

    /// In mm^3: the block less the tapered hole (wholly inside).
    static double expectedVolume(double heightMm, double depthMm) {
        return 100.0 * 50.0 * heightMm - frustumVolume(8, 4, depthMm);
    }
};

// A loft between sections of different shapes (P12-LOFT-001):
//
//   side -> Square (XY: a square of half-width side, centred on the origin)
//   radius, height -> Round (XY at z = height: a circle of radius r)
//   Square + Round -> Taper (loft)
//
// A square of half-width a lofted to a circle of radius r over h. Matched
// corner to equal quarter arc, the mixed area is M = 16 r R / pi with
// R = a sqrt(2) the square's circumradius, so
//   V = h/6 (A0 + 4 Am + A1), Am = (A0 + M + A1)/4,
//   A0 = 4 a^2, A1 = pi r^2.
// IDs: side 1, radius 2, height 3, Square 4, Round 5, Taper 6.
struct ShapeLoftModel : LoftModelBase {
    ParameterId side, radius, height;
    ObjectId square, round, taper;

    /// In mm^3, from the closed forms above.
    static double expectedVolume(double sideMm, double radiusMm, double heightMm) {
        const double a0 = 4.0 * sideMm * sideMm;
        const double a1 = std::numbers::pi * radiusMm * radiusMm;
        const double m = 16.0 * radiusMm * (sideMm * std::numbers::sqrt2) / std::numbers::pi;
        return heightMm / 6.0 * (a0 + (a0 + m + a1) + a1);
    }

    explicit ShapeLoftModel(features::LoftInterpolation interpolation = features::LoftInterpolation::Ruled)
        : LoftModelBase("Taper") {
        using namespace bettercad::literals;
        using namespace bettercad::sketch;
        side = doc.createParameter("side", 10_mm, units::mm).value();
        radius = doc.createParameter("radius", 5_mm, units::mm).value();
        height = doc.createParameter("height", 30_mm, units::mm).value();

        // The square's corners are fixed, so its shape is its own.
        square = addSketch(fixedPolygon("Square", Frame3D::xy(), {{-10, -10}, {10, -10}, {10, 10}, {-10, 10}}));
        round = addSketch(drivenCircle("Round", Frame3D::xy(), 0, 0, radius));
        taper = addLoft("Taper", {.sections = {{.sketch = sketchIdOf(square)},
                                               {.sketch = sketchIdOf(round), .offsetParameter = height}},
                                  .interpolation = interpolation});
    }
};

// Three circular sections, r 10, 5, 10, equally spaced over 50 mm, lofted
// smoothly (P12-LOFT-001):
//
//   Bottom (r 10 at z = 0), Waist (r 5 at z = 25), Top (r 10 at z = 50)
//
// Ruled it is two frustums, 8750 pi / 3; smooth it is the quadratic through
// the three radii, 7000 pi / 3. Both are closed forms.
// IDs: rEnd 1, rMid 2, mid 3, top 4, Bottom 5, Waist 6, Top 7, Spool 8.
struct SmoothLoftModel : LoftModelBase {
    ParameterId rEnd, rMid, mid, top;
    ObjectId bottom, waist, upper, spool;

    /// In mm^3: two frustums.
    static double ruledVolume() { return 8750.0 * std::numbers::pi / 3.0; }
    /// In mm^3: pi h times the integral of the quadratic through the radii.
    static double smoothVolume() { return 7000.0 * std::numbers::pi / 3.0; }

    explicit SmoothLoftModel(features::LoftInterpolation interpolation = features::LoftInterpolation::Smooth)
        : LoftModelBase("Spool") {
        using namespace bettercad::literals;
        rEnd = doc.createParameter("rEnd", 10_mm, units::mm).value();
        rMid = doc.createParameter("rMid", 5_mm, units::mm).value();
        mid = doc.createParameter("mid", 25_mm, units::mm).value();
        top = doc.createParameter("top", 50_mm, units::mm).value();

        bottom = addSketch(drivenCircle("Bottom", Frame3D::xy(), 0, 0, rEnd));
        waist = addSketch(drivenCircle("Waist", Frame3D::xy(), 0, 0, rMid));
        upper = addSketch(drivenCircle("Top", Frame3D::xy(), 0, 0, rEnd));
        spool = addLoft("Spool", {.sections = {{.sketch = sketchIdOf(bottom)},
                                               {.sketch = sketchIdOf(waist), .offsetParameter = mid},
                                               {.sketch = sketchIdOf(upper), .offsetParameter = top}},
                                  .interpolation = interpolation});
    }
};

} // namespace bettercad::test
