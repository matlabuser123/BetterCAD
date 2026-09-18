#pragma once

#include "features/FeatureTestSupport.hpp"
#include "support/FaceKindModels.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/Uuid.hpp>
#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/ParameterExpressions.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/LinearPatternFeature.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <numbers>
#include <string>
#include <vector>

namespace bettercad::test {

// Models for P12-PARAM-002: equations plus named configurations.

// ---------------------------------------------------------------------------
// The closed-form family
// ---------------------------------------------------------------------------
//
// One free parameter and two equations, chosen so the volume has an exact
// closed form that owes nothing to BetterCAD:
//
//   width  = W                     free, and the only thing a configuration sets
//   height = width / 2
//   depth  = width / 4
//
//   V = W * W/2 * W/4 = W^3 / 8
//
// Configurations: Small W = 40, Medium W = 80, Large W = 160, so
// V = 8000, 64000 and 512000 mm^3 exactly. Only `width` is overridden --
// `height` and `depth` follow from the equations, which is the point: a
// configuration never duplicates a derived value.
struct BoxFamilyModel {
    static constexpr std::string_view kDocumentId = "2d9c4b17-8e63-4a05-9f2c-7b1d6a3e8c40";

    Document doc{DocumentId::fromValue(*Uuid::parse(kDocumentId)), "BoxFamily"};
    CommandHistory history;
    ParameterId width, height, depth;
    ObjectId sketch, box;
    ConfigurationId small, medium, large;

    BoxFamilyModel() {
        using namespace bettercad::literals;
        width = doc.createParameter("width", 100_mm, units::mm).value();
        height = doc.createParameter("height", 1_mm, units::mm).value();
        depth = doc.createParameter("depth", 1_mm, units::mm).value();
        REQUIRE(doc.setParameterExpression(height, "width / 2").has_value());
        REQUIRE(doc.setParameterExpression(depth, "width / 4").has_value());

        sketch = doc.addObject(makeSketch()).value();
        auto feature = features::ExtrudeFeature::create(
            "Box", {.profile = SketchId::fromValue(sketch.value()), .depthParameter = depth});
        REQUIRE(feature.has_value());
        box = doc.addObject(std::move(*feature)).value();

        small = configure("Small", 40_mm);
        medium = configure("Medium", 80_mm);
        large = configure("Large", 160_mm);
        // The placeholders above are replaced by the equations, so the model
        // is coherent before anyone regenerates or saves it.
        REQUIRE(evaluateParameterExpressions(doc).succeeded());
    }

    ConfigurationId configure(const std::string& name, Length widthValue) {
        const ConfigurationId id = doc.createConfiguration(name).value();
        REQUIRE(doc.setConfigurationOverride(id, width, widthValue).has_value());
        return id;
    }

    [[nodiscard]] std::unique_ptr<sketch::Sketch> makeSketch() const {
        using namespace bettercad::literals;
        using namespace bettercad::sketch;
        auto s = std::make_unique<Sketch>("BoxSketch");
        const auto lines = addRectangle(*s, 0_mm, 0_mm, 100_mm, 50_mm);
        const EntityId origin = std::get<LineEntity>(s->findEntity(lines[0])->geometry).start;
        require(s->addFixed(origin));
        require(s->addHorizontal(lines[0]));
        require(s->addHorizontal(lines[2]));
        require(s->addVertical(lines[1]));
        require(s->addVertical(lines[3]));
        drive(*s, require(s->addDistance(lines[0], 1_mm)), width);
        drive(*s, require(s->addDistance(lines[1], 1_mm)), height);
        return s;
    }

    static void drive(sketch::Sketch& s, ConstraintId constraint, ParameterId parameter) {
        REQUIRE(s.setConstraintParameter(constraint, parameter).has_value());
    }

    void activate(std::optional<ConfigurationId> id) {
        REQUIRE(history.execute(doc, std::make_unique<SetActiveConfigurationCommand>(id)).has_value());
    }

    /// W^3 / 8, in mm^3, written out by hand.
    [[nodiscard]] static double expectedVolumeMm3(double widthMm) {
        return widthMm * widthMm * widthMm / 8.0;
    }
    /// The box is [0, W] x [0, W/2] x [0, W/4], so its centroid is its middle.
    [[nodiscard]] static std::array<double, 3> expectedCentroidMm(double widthMm) {
        return {widthMm / 2.0, widthMm / 4.0, widthMm / 8.0};
    }
};

// ---------------------------------------------------------------------------
// The mechanical reference family
// ---------------------------------------------------------------------------
//
// A mounting bracket whose whole shape comes from one free parameter and a
// chain of equations, across several qualified feature kinds: an extrude, a
// parametric hole, a linear pattern of that hole, and a sketch attached to a
// named face of the plate.
//
//   width         = W                             free
//   height        = width / 2
//   thickness     = 0.08 * width
//   edge          = 2 * thickness                 = 0.16 * width
//   hole_diameter = thickness                     = 0.08 * width
//   hole_space    = width - 2 * edge              = 0.68 * width
//
// Configurations set only `width`; everything else follows. The plate is
// W x W/2 x 0.08 W with two through holes of diameter 0.08 W, so by hand
//
//   V = W (W/2)(0.08 W) - 2 pi (0.04 W)^2 (0.08 W)
//     = 0.04 W^3 - 0.000256 pi W^3
//
// The holes are drilled from the plate's START plane, z = 0, which no
// configuration moves; drilling from the top would mean naming a plane whose
// height is itself parametric. The attached sketch, by contrast, names the
// plate's end cap, whose height *is* parametric: that is the reference a
// configuration switch has to keep resolving, and the model exists to test
// it (P12-STREF-001 semantics under P12-PARAM-002).
struct BracketFamilyModel : FaceKindModel {
    static constexpr std::string_view kDocumentId = "b41f7c28-3a6d-4e19-85b0-9c2e7d4a1f36";
    /// Where the first hole is centred, in mm; inside the smallest plate
    /// (50 x 25) and clear of its edges at the largest hole (radius 8).
    static constexpr double kHoleX = 12.0;
    static constexpr double kHoleY = 12.0;

    CommandHistory history;
    ParameterId width, height, thickness, edge, holeDiameter, holeSpace;
    ObjectId plateSketch, plate, bore, holes, topSketch;
    ConfigurationId small, medium, large;

    BracketFamilyModel() : FaceKindModel(kDocumentId, "BracketFamily") {
        using namespace bettercad::literals;
        using namespace bettercad::features;
        width = doc.createParameter("width", 100_mm, units::mm).value();
        height = doc.createParameter("height", 1_mm, units::mm).value();
        thickness = doc.createParameter("thickness", 1_mm, units::mm).value();
        edge = doc.createParameter("edge", 1_mm, units::mm).value();
        holeDiameter = doc.createParameter("hole_diameter", 1_mm, units::mm).value();
        holeSpace = doc.createParameter("hole_space", 1_mm, units::mm).value();
        REQUIRE(doc.setParameterExpression(height, "width / 2").has_value());
        REQUIRE(doc.setParameterExpression(thickness, "0.08 * width").has_value());
        REQUIRE(doc.setParameterExpression(edge, "2 * thickness").has_value());
        REQUIRE(doc.setParameterExpression(holeDiameter, "thickness").has_value());
        REQUIRE(doc.setParameterExpression(holeSpace, "width - 2 * edge").has_value());

        auto sketch = std::make_unique<sketch::Sketch>("PlateSketch");
        addSizedRectangle(*sketch, 0.0, 0.0, 100.0, 50.0, width, height);
        plateSketch = doc.addObject(std::move(sketch)).value();
        plate = add(ExtrudeFeature::create(
            "Plate", {.profile = sketchOf(plateSketch), .depthParameter = thickness}));
        bore = add(HoleFeature::create("Bore", {.target = featureOf(plate),
                                                .face = startPlane(),
                                                .center = Point2D{kHoleX * units::mm, kHoleY * units::mm},
                                                .extent = geometry::HoleExtent::Through,
                                                .diameter = 8_mm,
                                                .diameterParameter = holeDiameter}));
        holes = add(LinearPatternFeature::create(
            "Holes", {.source = featureOf(bore),
                      .first = {.direction = {1.0, 0.0, 0.0}, .count = 2, .spacingParameter = holeSpace}}));
        // The reference under test: a sketch on the plate's end cap, whose
        // height is 0.08 W and therefore moves with every configuration.
        topSketch = addCircleSketch("TopSketch", faceOf(plate, {.role = FaceRole::EndCap}), 40.0, 12.0, 3.0);

        small = configure("Small", 50_mm);
        medium = configure("Medium", 100_mm);
        large = configure("Large", 200_mm);
        REQUIRE(evaluateParameterExpressions(doc).succeeded());
    }

    /// The plate's start plane, z = 0, which no configuration moves.
    static geometry::FaceSignature startPlane() {
        return geometry::planeSignature(Point3D{}, Direction3D::unitZ().reversed());
    }

    ConfigurationId configure(const std::string& name, Length widthValue) {
        const ConfigurationId id = doc.createConfiguration(name).value();
        REQUIRE(doc.setConfigurationOverride(id, width, widthValue).has_value());
        return id;
    }

    void activate(std::optional<ConfigurationId> id) {
        REQUIRE(history.execute(doc, std::make_unique<SetActiveConfigurationCommand>(id)).has_value());
    }

    [[nodiscard]] double si(ParameterId parameter) const {
        return doc.effectiveParameterValue(parameter)->siValue;
    }

    /// The plate with its two through holes, in mm^3, from the definitions
    /// above written out by hand.
    [[nodiscard]] static double expectedVolumeMm3(double widthMm) {
        const double cube = widthMm * widthMm * widthMm;
        return 0.04 * cube - 0.000256 * std::numbers::pi * cube;
    }
    /// The end cap's height, which the attached sketch must follow.
    [[nodiscard]] static double expectedThicknessMm(double widthMm) { return 0.08 * widthMm; }
};

} // namespace bettercad::test
