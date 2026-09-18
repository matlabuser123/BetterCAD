#pragma once

#include "features/FeatureTestSupport.hpp"
#include "support/BlockModel.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/parameters/Parameter.hpp>
#include <bettercad/features/SweepFeature.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

namespace bettercad::test {

inline SketchId sketchIdOf(ObjectId id) {
    return SketchId::fromValue(id.value());
}

inline FeatureId featureIdOf(ObjectId id) {
    return FeatureId::fromValue(id.value());
}

/// Adds a circle about @p center to @p sketch, its centre fixed and its
/// radius driven by @p radius (a length parameter). Returns the circle.
inline EntityId addDrivenCircle(sketch::Sketch& sketch, Point2D center, ParameterId radius) {
    using namespace bettercad::literals;
    const EntityId circle = require(sketch.addCircle(center, 1_mm));
    require(sketch.addFixed(std::get<sketch::CircleEntity>(sketch.findEntity(circle)->geometry).center));
    const ConstraintId r = require(sketch.addRadius(circle, 1_mm));
    REQUIRE(sketch.setConstraintParameter(r, radius).has_value());
    return circle;
}

/// A sketch on the XZ plane with one line from the origin up the Z axis
/// (local +Y), its start fixed and its length driven by @p length.
inline std::unique_ptr<sketch::Sketch> straightPathSketch(ParameterId length, EntityId& line) {
    using namespace bettercad::literals;
    auto path = std::make_unique<sketch::Sketch>("Path", Frame3D::xz());
    line = require(path->addLine(Point2D{}, Point2D{0_mm, 50_mm}));
    require(path->addFixed(std::get<sketch::LineEntity>(path->findEntity(line)->geometry).start));
    require(path->addVertical(line));
    const ConstraintId d = require(path->addDistance(line, 1_mm));
    REQUIRE(path->setConstraintParameter(d, length).has_value());
    return path;
}

// The primary straight sweep:
//
//   width, height -> Profile (XY plane: rectangle with its corner fixed at the origin)
//   length        -> Path (XZ plane: a line from the origin up the Z axis)
//   Profile, Path -> Sweep (new body)
//
// The path starts at the profile's corner, so the sweep is the box
// [0, 10] x [0, 20] x [0, 100] mm: V = A L = 200 x 100. IDs: width 1,
// height 2, length 3, Profile 4, Path 5, Sweep 6.
struct StraightSweepModel {
    Document doc{"Sweep"};
    ParameterId width, height, length;
    ObjectId profile, path, sweep;
    EntityId pathLine;

    StraightSweepModel() {
        using namespace bettercad::literals;
        using namespace bettercad::sketch;
        width = doc.createParameter("width", 10_mm, units::mm).value();
        height = doc.createParameter("height", 20_mm, units::mm).value();
        length = doc.createParameter("length", 100_mm, units::mm).value();

        auto rectangle = std::make_unique<Sketch>("Profile");
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
        profile = doc.addObject(std::move(rectangle)).value();
        path = doc.addObject(straightPathSketch(length, pathLine)).value();
        sweep = addSweep("Sweep", definition());
    }

    [[nodiscard]] features::SweepDefinition definition() const {
        return {.profile = sketchIdOf(profile), .path = {.sketch = sketchIdOf(path), .edges = {pathLine}}};
    }

    ObjectId addSweep(const std::string& name, const features::SweepDefinition& d) {
        auto feature = features::SweepFeature::create(name, d);
        REQUIRE(feature.has_value());
        return doc.addObject(std::move(*feature)).value();
    }

    [[nodiscard]] features::SweepDefinition definitionOf(ObjectId id) const {
        return doc.findObjectAs<features::SweepFeature>(id)->definition();
    }

    void setDefinition(ObjectId id, const features::SweepDefinition& d) {
        REQUIRE(doc.modifyObject<features::SweepFeature>(id, [&](features::SweepFeature& f) {
                       return f.setDefinition(d);
                   }).has_value());
    }
};

// A circle swept along a straight path:
//
//   radius -> Profile (XY plane: circle about the fixed origin)
//   length -> Path (XZ plane: a line from the origin up the Z axis)
//
// V = pi r^2 L = 2500 pi for r = 5, L = 100. IDs: radius 1, length 2,
// Profile 3, Path 4, Sweep 5.
struct StraightTubeModel {
    Document doc{"Tube"};
    ParameterId radius, length;
    ObjectId profile, path, sweep;
    EntityId circle, pathLine;

    StraightTubeModel() {
        using namespace bettercad::literals;
        radius = doc.createParameter("radius", 5_mm, units::mm).value();
        length = doc.createParameter("length", 100_mm, units::mm).value();
        auto disc = std::make_unique<sketch::Sketch>("Profile");
        circle = addDrivenCircle(*disc, Point2D{}, radius);
        profile = doc.addObject(std::move(disc)).value();
        path = doc.addObject(straightPathSketch(length, pathLine)).value();
        auto feature = features::SweepFeature::create(
            "Sweep", {.profile = sketchIdOf(profile), .path = {.sketch = sketchIdOf(path), .edges = {pathLine}}});
        REQUIRE(feature.has_value());
        sweep = doc.addObject(std::move(*feature)).value();
    }
};

// A curved sweep: a circle swept a quarter turn.
//
//   radius -> Profile (XY plane: circle about the fixed origin)
//   bend   -> Path (XZ plane: arc about (bend, 0) from the origin, counter-
//             clockwise, to (bend, -bend))
//
// The path leaves the origin down the Z axis (against the profile's normal)
// and bends towards +X. Pappus: V = pi r^2 bend pi/2 = 40 pi^2 for r = 2,
// bend = 20. The swept tube lies in (-r, -r, -bend - r) to (bend, r, 0).
// IDs: radius 1, bend 2, Profile 3, Path 4, Sweep 5.
struct ArcSweepModel {
    Document doc{"Bend"};
    ParameterId radius, bend;
    ObjectId profile, path, sweep;
    EntityId circle, pathArc;

    /// In mm^3: pi r^2 R theta.
    static double expectedVolume(double radiusMm, double bendMm, double sweepRadians = std::numbers::pi / 2.0) {
        return std::numbers::pi * radiusMm * radiusMm * bendMm * sweepRadians;
    }

    ArcSweepModel() {
        using namespace bettercad::literals;
        radius = doc.createParameter("radius", 2_mm, units::mm).value();
        bend = doc.createParameter("bend", 20_mm, units::mm).value();
        auto disc = std::make_unique<sketch::Sketch>("Profile");
        circle = addDrivenCircle(*disc, Point2D{}, radius);
        profile = doc.addObject(std::move(disc)).value();

        auto route = std::make_unique<sketch::Sketch>("Path", Frame3D::xz());
        const EntityId start = require(route->addPoint(Point2D{}));
        const EntityId center = require(route->addPoint(Point2D{20_mm, 0_mm}));
        const EntityId end = require(route->addPoint(Point2D{20_mm, -(20_mm)}));
        pathArc = require(route->addArc(center, start, end));
        require(route->addFixed(start));
        require(route->addHorizontal(start, center));
        const ConstraintId d = require(route->addDistance(start, center, 1_mm));
        REQUIRE(route->setConstraintParameter(d, bend).has_value());
        require(route->addVertical(center, end)); // with the arc: end = centre - (0, bend)
        path = doc.addObject(std::move(route)).value();

        auto feature = features::SweepFeature::create(
            "Sweep", {.profile = sketchIdOf(profile), .path = {.sketch = sketchIdOf(path), .edges = {pathArc}}});
        REQUIRE(feature.has_value());
        sweep = doc.addObject(std::move(*feature)).value();
    }
};

// A circle swept around a full circle, a torus:
//
//   radius, bend -> Profile (XZ plane: circle of radius `radius` whose centre
//                   is `bend` from a fixed origin point along +X)
//   bend         -> Path (XY plane: circle of radius `bend` about the fixed origin)
//
// The path starts at (bend, 0, 0) and runs counter-clockwise about +Z,
// leaving along +Y, across the profile's plane. V = 2 pi^2 bend r^2 =
// 160 pi^2 for bend = 20, r = 2. The profile can also be revolved about the
// Z axis (its sketch's Y axis) into the same torus. IDs: radius 1, bend 2,
// Profile 3, Path 4, Sweep 5.
struct TorusSweepModel {
    Document doc{"Ring"};
    ParameterId radius, bend;
    ObjectId profile, path, sweep;
    EntityId pathCircle;

    /// In mm^3: 2 pi^2 R r^2.
    static double expectedVolume(double radiusMm, double bendMm) {
        return 2.0 * std::numbers::pi * std::numbers::pi * bendMm * radiusMm * radiusMm;
    }

    TorusSweepModel() {
        using namespace bettercad::literals;
        radius = doc.createParameter("radius", 2_mm, units::mm).value();
        bend = doc.createParameter("bend", 20_mm, units::mm).value();

        auto section = std::make_unique<sketch::Sketch>("Profile", Frame3D::xz());
        const EntityId origin = require(section->addPoint(Point2D{}));
        require(section->addFixed(origin));
        const EntityId disc = require(section->addCircle(Point2D{20_mm, 0_mm}, 1_mm));
        const EntityId center = std::get<sketch::CircleEntity>(section->findEntity(disc)->geometry).center;
        require(section->addHorizontal(origin, center));
        const ConstraintId offset = require(section->addDistance(origin, center, 1_mm));
        REQUIRE(section->setConstraintParameter(offset, bend).has_value());
        const ConstraintId r = require(section->addRadius(disc, 1_mm));
        REQUIRE(section->setConstraintParameter(r, radius).has_value());
        profile = doc.addObject(std::move(section)).value();

        auto ring = std::make_unique<sketch::Sketch>("Path");
        pathCircle = addDrivenCircle(*ring, Point2D{}, bend);
        path = doc.addObject(std::move(ring)).value();

        auto feature = features::SweepFeature::create(
            "Sweep", {.profile = sketchIdOf(profile), .path = {.sketch = sketchIdOf(path), .edges = {pathCircle}}});
        REQUIRE(feature.has_value());
        sweep = doc.addObject(std::move(*feature)).value();
    }
};

// A circle r = 2 mm (Profile, YZ plane, about the origin) swept along fixed
// paths in the XY plane that leave the origin along +X:
//   Polyline:    (0, 0) -> (50, 0) -> (50, 50), a mitred right-angle corner;
//   LineArcLine: (0, 0) -> (50, 0), a tangent quarter arc about (50, 20) to
//                (70, 20), then up to (70, 70).
// Each is A L exactly (the corner's mitre loses inside what it adds outside):
// 400 pi and 4 pi (100 + 10 pi) mm^3. IDs: Profile 1, Path 2, Sweep 3.
struct FixedPathSweepModel {
    enum class Route { Polyline, LineArcLine };

    Document doc{"Route"};
    ObjectId profile, path, sweep;
    std::vector<EntityId> edges;

    explicit FixedPathSweepModel(Route route) {
        using namespace bettercad::literals;
        auto disc = std::make_unique<sketch::Sketch>("Profile", Frame3D::yz());
        const EntityId circle = require(disc->addCircle(Point2D{}, 2_mm));
        require(disc->addFixed(std::get<sketch::CircleEntity>(disc->findEntity(circle)->geometry).center));
        require(disc->addRadius(circle, 2_mm));
        profile = doc.addObject(std::move(disc)).value();

        auto sketch = std::make_unique<sketch::Sketch>("Path");
        const auto point = [&](double x, double y, bool fixed) {
            const EntityId p = require(sketch->addPoint(Point2D{x * units::mm, y * units::mm}));
            if (fixed) {
                require(sketch->addFixed(p));
            }
            return p;
        };
        const EntityId p0 = point(0, 0, true);
        const EntityId p1 = point(50, 0, true);
        if (route == Route::Polyline) {
            const EntityId p2 = point(50, 50, true);
            edges = {require(sketch->addLine(p0, p1)), require(sketch->addLine(p1, p2))};
        } else {
            const EntityId c = point(50, 20, true);
            const EntityId p2 = point(70, 20, false);
            const EntityId p3 = point(70, 70, false);
            require(sketch->addHorizontal(c, p2)); // with the arc: p2 = c + (20, 0)
            require(sketch->addVertical(p2, p3));
            require(sketch->addDistance(p2, p3, 50_mm));
            edges = {require(sketch->addLine(p0, p1)), require(sketch->addArc(c, p1, p2)),
                     require(sketch->addLine(p2, p3))};
        }
        path = doc.addObject(std::move(sketch)).value();
        auto feature = features::SweepFeature::create(
            "Sweep", {.profile = sketchIdOf(profile), .path = {.sketch = sketchIdOf(path), .edges = edges}});
        REQUIRE(feature.has_value());
        sweep = doc.addObject(std::move(*feature)).value();
    }
};

// The parametric block (see BlockModel) with a swept channel:
//
//   radius -> ChannelProfile (plane x = -1, facing +X: circle about (y, z) = (25, 10))
//             ChannelPath (plane z = 10: line from (-1, 25) to (101, 25), along +X)
//   Pad, ChannelProfile, ChannelPath -> Channel (sweep, cut from Pad)
//
// The channel passes 1 mm beyond both ends of the 100 mm block, so the cut
// removes pi r^2 x 100: V = L W H - 2500 pi for r = 5. IDs: width 1,
// length 2, height 3, radius 4, Base 5, Pad 6, ChannelProfile 7,
// ChannelPath 8, Channel 9.
struct ChannelModel : BlockModel {
    ParameterId radius;
    ObjectId channelProfile, channelPath, channel;
    EntityId channelLine;

    /// In mm^3: L W H - pi r^2 100 (L = 100, W = 50).
    static double expectedVolume(double heightMm, double radiusMm) {
        return 100.0 * 50.0 * heightMm - std::numbers::pi * radiusMm * radiusMm * 100.0;
    }

    explicit ChannelModel(features::FeatureOperation operation = features::FeatureOperation::Cut)
        : BlockModel("radius", Length::fromSi(0.005)), radius(extra) {
        using namespace bettercad::literals;
        const auto facingX =
            Frame3D::create(Point3D{-(1_mm), 0_mm, 0_mm}, Direction3D::unitX(), Direction3D::unitY()).value();
        auto disc = std::make_unique<sketch::Sketch>("ChannelProfile", facingX);
        addDrivenCircle(*disc, Point2D{25_mm, 10_mm}, radius);
        channelProfile = doc.addObject(std::move(disc)).value();

        const auto level = Frame3D::create(Point3D{0_mm, 0_mm, 10_mm}, Direction3D::unitZ(), Direction3D::unitX()).value();
        auto route = std::make_unique<sketch::Sketch>("ChannelPath", level);
        channelLine = require(route->addLine(Point2D{-(1_mm), 25_mm}, Point2D{101_mm, 25_mm}));
        require(route->addFixed(std::get<sketch::LineEntity>(route->findEntity(channelLine)->geometry).start));
        require(route->addHorizontal(channelLine));
        require(route->addDistance(channelLine, 102_mm));
        channelPath = doc.addObject(std::move(route)).value();

        channel = add<features::SweepFeature>(
            "Channel", {.profile = sketchIdOf(channelProfile),
                        .path = {.sketch = sketchIdOf(channelPath), .edges = {channelLine}},
                        .operation = operation,
                        .target = operation == features::FeatureOperation::NewBody
                                      ? std::nullopt
                                      : std::optional<FeatureId>{featureId(pad)}});
    }
};

// The parametric block with a swept handle joined to its top:
//
//   HandleProfile (plane z = 20: circle r = 2 about (70, 25))
//   HandlePath (plane y = 25, facing -Y: arc about (50, 20) from (70, 20)
//               counter-clockwise over the top to (30, 20))
//   Pad, HandleProfile, HandlePath -> Handle (sweep, joined to Pad)
//
// The half-turn tube stands on the top face at x = 70 and x = 30:
// V = L W H + pi r^2 R pi = 100000 + 80 pi^2. IDs: width 1, length 2,
// height 3, spare 4, Base 5, Pad 6, HandleProfile 7, HandlePath 8, Handle 9.
struct HandleModel : BlockModel {
    ObjectId handleProfile, handlePath, handle;
    EntityId handleArc;

    HandleModel() : BlockModel("spare", Length::fromSi(0.001)) {
        using namespace bettercad::literals;
        const auto top = Frame3D::create(Point3D{0_mm, 0_mm, 20_mm}, Direction3D::unitZ(), Direction3D::unitX()).value();
        auto disc = std::make_unique<sketch::Sketch>("HandleProfile", top);
        const EntityId circle = require(disc->addCircle(Point2D{70_mm, 25_mm}, 2_mm));
        require(disc->addFixed(std::get<sketch::CircleEntity>(disc->findEntity(circle)->geometry).center));
        require(disc->addRadius(circle, 2_mm));
        handleProfile = doc.addObject(std::move(disc)).value();

        const auto side =
            Frame3D::create(Point3D{0_mm, 25_mm, 0_mm}, Direction3D::unitY().reversed(), Direction3D::unitX()).value();
        auto route = std::make_unique<sketch::Sketch>("HandlePath", side);
        const EntityId c = require(route->addPoint(Point2D{50_mm, 20_mm}));
        const EntityId start = require(route->addPoint(Point2D{70_mm, 20_mm}));
        const EntityId end = require(route->addPoint(Point2D{30_mm, 20_mm}));
        require(route->addFixed(c));
        require(route->addFixed(start));
        require(route->addHorizontal(c, end)); // with the arc: end = c - (20, 0)
        handleArc = require(route->addArc(c, start, end));
        handlePath = doc.addObject(std::move(route)).value();

        handle = add<features::SweepFeature>("Handle",
                                             {.profile = sketchIdOf(handleProfile),
                                              .path = {.sketch = sketchIdOf(handlePath), .edges = {handleArc}},
                                              .operation = features::FeatureOperation::Join,
                                              .target = featureId(pad)});
    }
};

// A bar swept along a path that leaves every plane (P12-SWEEP-001):
//
//   BarProfile (XY: 4 x 4 mm square centred on the origin)
//   Rise  (XZ: a line from the origin up +Z)          -> run 1
//   Cross (plane z = 40 facing +Y: a line along +X)   -> run 2
//   Turn  (plane x = 40 facing +Z: a line along +Y)   -> run 3
//   BarProfile + Rise, Cross, Turn -> Bar (sweep)
//
// The path runs (0,0,0) -> (0,0,40) -> (40,0,40) -> (40,40,40): 120 mm in
// all, with two right-angled corners, so V = 16 x 120 = 1920 mm^3. The
// profile's centroid rides on the path, as a spatial sweep requires.
// IDs: BarProfile 1, Rise 2, Cross 3, Turn 4, Bar 5.
struct SpatialBarModel {
    Document doc{"Bar"};
    ObjectId profile, rise, cross, turn, bar;
    EntityId riseLine, crossLine, turnLine;

    /// In mm^3: the section's area times the path's length.
    static double expectedVolume(double sideMm = 4.0, double lengthMm = 120.0) {
        return sideMm * sideMm * lengthMm;
    }

    SpatialBarModel() {
        using namespace bettercad::literals;
        using namespace bettercad::sketch;
        auto square = std::make_unique<Sketch>("BarProfile");
        const auto lines = addRectangle(*square, -(2_mm), -(2_mm), 4_mm, 4_mm);
        require(square->addFixed(std::get<LineEntity>(square->findEntity(lines[0])->geometry).start));
        profile = doc.addObject(std::move(square)).value();

        // Each run is drawn in its own sketch, on its own plane; they join
        // in model space, which no one sketch could do.
        const auto straight = [&](const std::string& name, const Frame3D& plane, Point2D from, Point2D to,
                                  EntityId& edge) {
            auto route = std::make_unique<Sketch>(name, plane);
            edge = require(route->addLine(from, to));
            require(route->addFixed(std::get<LineEntity>(route->findEntity(edge)->geometry).start));
            require(route->addFixed(std::get<LineEntity>(route->findEntity(edge)->geometry).end));
            return doc.addObject(std::move(route)).value();
        };
        rise = straight("Rise", Frame3D::xz(), Point2D{}, Point2D{0_mm, 40_mm}, riseLine);
        cross = straight("Cross",
                         Frame3D::create(Point3D{0_mm, 0_mm, 40_mm}, Direction3D::unitY(), Direction3D::unitX())
                             .value(),
                         Point2D{}, Point2D{40_mm, 0_mm}, crossLine);
        turn = straight("Turn",
                        Frame3D::create(Point3D{40_mm, 0_mm, 40_mm}, Direction3D::unitZ(), Direction3D::unitY())
                            .value(),
                        Point2D{}, Point2D{40_mm, 0_mm}, turnLine);

        auto sweep = features::SweepFeature::create(
            "Bar", {.profile = sketchIdOf(profile),
                    .path = {.sketch = sketchIdOf(rise),
                             .edges = {riseLine},
                             .runs = {{.sketch = sketchIdOf(cross), .edges = {crossLine}},
                                      {.sketch = sketchIdOf(turn), .edges = {turnLine}}}}});
        REQUIRE(sweep.has_value());
        bar = doc.addObject(std::move(*sweep)).value();
    }

    [[nodiscard]] features::SweepDefinition definition() const {
        return doc.findObjectAs<features::SweepFeature>(bar)->definition();
    }
    void setDefinition(const features::SweepDefinition& definition) {
        REQUIRE(doc.modifyObject<features::SweepFeature>(bar, [&](features::SweepFeature& f) {
                       return f.setDefinition(definition);
                   }).has_value());
    }
};

// A twisted bar (P12-SWEEP-001):
//
//   twist (angle parameter, 90 deg)
//   TwistProfile (XY: 8 x 2 mm rectangle centred on the origin)
//   Rise (XZ: a line from the origin 100 mm up +Z, driven by length)
//   -> Twisted (sweep, twist driven by the parameter)
//
// The section turns theta(u) = u x twist about the path, measured from the
// profile sketch's X axis. V = 16 x 100 = 1600 mm^3 whatever the twist is.
// IDs: twist 1, length 2, TwistProfile 3, Rise 4, Twisted 5.
struct TwistedBarModel {
    Document doc{"Twist"};
    ParameterId twist, length;
    ObjectId profile, rise, twisted;
    EntityId riseLine;

    /// The section's half-extents across the profile's X and Y axes when it
    /// has turned by @p phi: a |cos| + b |sin| and a |sin| + b |cos|.
    static double acrossX(double phi, double a = 4.0, double b = 1.0) {
        return a * std::abs(std::cos(phi)) + b * std::abs(std::sin(phi));
    }
    static double acrossY(double phi, double a = 4.0, double b = 1.0) {
        return a * std::abs(std::sin(phi)) + b * std::abs(std::cos(phi));
    }
    /// In mm^3: the area times the length, however it turns.
    static double expectedVolume(double lengthMm = 100.0) { return 8.0 * 2.0 * lengthMm; }

    TwistedBarModel() {
        using namespace bettercad::literals;
        using namespace bettercad::sketch;
        twist = doc.createParameter("twist", 90_deg, units::deg).value();
        length = doc.createParameter("length", 100_mm, units::mm).value();

        auto rectangle = std::make_unique<Sketch>("TwistProfile");
        const auto lines = addRectangle(*rectangle, -(4_mm), -(1_mm), 8_mm, 2_mm);
        require(rectangle->addFixed(std::get<LineEntity>(rectangle->findEntity(lines[0])->geometry).start));
        profile = doc.addObject(std::move(rectangle)).value();

        auto route = std::make_unique<Sketch>("Rise", Frame3D::xz());
        riseLine = require(route->addLine(Point2D{}, Point2D{0_mm, 100_mm}));
        require(route->addFixed(std::get<LineEntity>(route->findEntity(riseLine)->geometry).start));
        require(route->addVertical(riseLine));
        const ConstraintId along = require(route->addDistance(riseLine, 100_mm));
        REQUIRE(route->setConstraintParameter(along, length).has_value());
        rise = doc.addObject(std::move(route)).value();

        auto sweep = features::SweepFeature::create(
            "Twisted", {.profile = sketchIdOf(profile),
                        .path = {.sketch = sketchIdOf(rise), .edges = {riseLine}},
                        .twistParameter = twist});
        REQUIRE(sweep.has_value());
        twisted = doc.addObject(std::move(*sweep)).value();
    }

    [[nodiscard]] features::SweepDefinition definition() const {
        return doc.findObjectAs<features::SweepFeature>(twisted)->definition();
    }
    void setDefinition(const features::SweepDefinition& definition) {
        REQUIRE(doc.modifyObject<features::SweepFeature>(twisted, [&](features::SweepFeature& f) {
                       return f.setDefinition(definition);
                   }).has_value());
    }
};

// A bar carried by a guide curve (P12-SWEEP-001):
//
//   GuideProfile (XY: 8 x 2 mm rectangle centred on the origin)
//   Rise  (XZ: a line from the origin 100 mm up +Z)
//   Lead  (a plane containing the guide: a line from (10, 0, 0) to
//          (0, 10, 100))
//   -> Guided (sweep, carried by Lead)
//
// The guide's offset from the path turns from +X to +Y over the length: a
// quarter turn, by the guide's own definition. IDs: GuideProfile 1, Rise 2,
// Lead 3, Guided 4.
struct GuidedBarModel {
    Document doc{"Guide"};
    ObjectId profile, rise, lead, guided;
    EntityId riseLine, leadLine;

    static double expectedVolume(double lengthMm = 100.0) { return 8.0 * 2.0 * lengthMm; }

    GuidedBarModel() {
        using namespace bettercad::literals;
        using namespace bettercad::sketch;
        auto rectangle = std::make_unique<Sketch>("GuideProfile");
        const auto lines = addRectangle(*rectangle, -(4_mm), -(1_mm), 8_mm, 2_mm);
        require(rectangle->addFixed(std::get<LineEntity>(rectangle->findEntity(lines[0])->geometry).start));
        profile = doc.addObject(std::move(rectangle)).value();

        auto route = std::make_unique<Sketch>("Rise", Frame3D::xz());
        riseLine = require(route->addLine(Point2D{}, Point2D{0_mm, 100_mm}));
        require(route->addFixed(std::get<LineEntity>(route->findEntity(riseLine)->geometry).start));
        require(route->addFixed(std::get<LineEntity>(route->findEntity(riseLine)->geometry).end));
        rise = doc.addObject(std::move(route)).value();

        // The guide runs from (10, 0, 0) to (0, 10, 100); its plane holds
        // both, with the line along the plane's own X axis.
        const auto direction = Direction3D::fromComponents(-10.0, 10.0, 100.0).value();
        const auto normal = direction.cross(Direction3D::unitX()).value();
        const auto plane = Frame3D::create(Point3D{10_mm, 0_mm, 0_mm}, normal, direction).value();
        const double span = std::sqrt(10.0 * 10.0 + 10.0 * 10.0 + 100.0 * 100.0);
        auto guide = std::make_unique<Sketch>("Lead", plane);
        leadLine = require(guide->addLine(Point2D{}, Point2D{span * units::mm, 0_mm}));
        require(guide->addFixed(std::get<LineEntity>(guide->findEntity(leadLine)->geometry).start));
        require(guide->addFixed(std::get<LineEntity>(guide->findEntity(leadLine)->geometry).end));
        lead = doc.addObject(std::move(guide)).value();

        auto sweep = features::SweepFeature::create(
            "Guided", {.profile = sketchIdOf(profile),
                       .path = {.sketch = sketchIdOf(rise), .edges = {riseLine}},
                       .guide = features::SweepPath{.sketch = sketchIdOf(lead), .edges = {leadLine}}});
        REQUIRE(sweep.has_value());
        guided = doc.addObject(std::move(*sweep)).value();
    }

    [[nodiscard]] features::SweepDefinition definition() const {
        return doc.findObjectAs<features::SweepFeature>(guided)->definition();
    }
    void setDefinition(const features::SweepDefinition& definition) {
        REQUIRE(doc.modifyObject<features::SweepFeature>(guided, [&](features::SweepFeature& f) {
                       return f.setDefinition(definition);
                   }).has_value());
    }
};

} // namespace bettercad::test
