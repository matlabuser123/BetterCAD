#include "FeatureTestSupport.hpp"
#include "TestHelpers.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/FeatureCommands.hpp>
#include <bettercad/features/Regeneration.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <memory>
#include <numbers>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using namespace bettercad::sketch;
using bettercad::test::addRectangle;
using bettercad::test::errorCode;
using bettercad::test::require;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr double pi = std::numbers::pi;
constexpr double kRel = 1e-12;
constexpr double kPosMm = 1e-9;

struct Part {
    Document doc{"Part"};

    SketchId addSketch(std::unique_ptr<Sketch> sketch) {
        auto id = doc.addObject(std::move(sketch));
        REQUIRE(id.has_value());
        return SketchId::fromValue(id->value());
    }

    FeatureId addExtrude(const ExtrudeDefinition& definition, std::string name = "") {
        auto feature = ExtrudeFeature::create(name.empty() ? doc.uniqueName("Extrude") : name, definition);
        REQUIRE(feature.has_value());
        auto id = doc.addObject(std::move(*feature));
        REQUIRE(id.has_value());
        return FeatureId::fromValue(id->value());
    }

    Result<geometry::Body> regenerate(FeatureId id, const geometry::Body* target = nullptr) const {
        const auto* feature = doc.findObjectAs<ExtrudeFeature>(ObjectId{id});
        REQUIRE(feature != nullptr);
        return regenerateExtrude(*feature, doc, target);
    }
};

std::unique_ptr<Sketch> rectangleSketch(Length w, Length h, const Frame3D& plane = Frame3D::xy(),
                                        const std::string& name = "Profile") {
    auto sketch = std::make_unique<Sketch>(name, plane);
    addRectangle(*sketch, 0_mm, 0_mm, w, h);
    return sketch;
}

geometry::Body requireBody(const Result<geometry::Body>& body) {
    INFO((body ? std::string{} : body.error().message));
    REQUIRE(body.has_value());
    CHECK(body->isValid());
    return *body;
}

double volumeMm3(const geometry::Body& body) {
    const auto props = body.massProperties();
    REQUIRE(props.has_value());
    return props->volume.in(units::mm3);
}

void checkBounds(const geometry::Body& body, double x0, double y0, double z0, double x1, double y1,
                 double z1) {
    const auto box = body.boundingBox();
    REQUIRE(box.has_value());
    CHECK_THAT(box->min.x.in(units::mm), WithinAbs(x0, kPosMm));
    CHECK_THAT(box->min.y.in(units::mm), WithinAbs(y0, kPosMm));
    CHECK_THAT(box->min.z.in(units::mm), WithinAbs(z0, kPosMm));
    CHECK_THAT(box->max.x.in(units::mm), WithinAbs(x1, kPosMm));
    CHECK_THAT(box->max.y.in(units::mm), WithinAbs(y1, kPosMm));
    CHECK_THAT(box->max.z.in(units::mm), WithinAbs(z1, kPosMm));
}

} // namespace

TEST_CASE("Spec acceptance: 100 x 50 mm rectangle extruded 20 mm has V = 100000 mm^3",
          "[features][extrude][acceptance]") {
    Part part;
    const SketchId sketch = part.addSketch(rectangleSketch(100_mm, 50_mm));
    const FeatureId extrude = part.addExtrude(ExtrudeDefinition{.profile = sketch, .depth = 20_mm});

    const geometry::Body body = requireBody(part.regenerate(extrude));
    CHECK_THAT(volumeMm3(body), WithinRel(100.0 * 50.0 * 20.0, kRel));
    CHECK(body.topology().faces == 6);
    checkBounds(body, 0, 0, 0, 100, 50, 20);
}

TEST_CASE("Extrude directions", "[features][extrude]") {
    Part part;
    const SketchId sketch = part.addSketch(rectangleSketch(100_mm, 50_mm));

    SECTION("reversed") {
        const FeatureId id = part.addExtrude(
            {.profile = sketch, .depth = 20_mm, .direction = ExtrudeDirection::Reversed});
        const auto body = requireBody(part.regenerate(id));
        checkBounds(body, 0, 0, -20, 100, 50, 0);
        CHECK_THAT(volumeMm3(body), WithinRel(100000.0, kRel));
    }
    SECTION("symmetric") {
        const FeatureId id = part.addExtrude(
            {.profile = sketch, .depth = 20_mm, .direction = ExtrudeDirection::Symmetric});
        const auto body = requireBody(part.regenerate(id));
        checkBounds(body, 0, 0, -10, 100, 50, 10);
        CHECK_THAT(volumeMm3(body), WithinRel(100000.0, kRel));
    }
    SECTION("sketch on the XZ plane extrudes along its normal (-Y)") {
        const SketchId xz = part.addSketch(rectangleSketch(100_mm, 50_mm, Frame3D::xz(), "ProfileXZ"));
        const FeatureId id = part.addExtrude({.profile = xz, .depth = 20_mm});
        const auto body = requireBody(part.regenerate(id));
        checkBounds(body, 0, -20, 0, 100, 0, 50);
    }
}

TEST_CASE("Extruded regions with circles, holes and arcs", "[features][extrude]") {
    Part part;
    SECTION("cylinder from a circle") {
        auto sketch = std::make_unique<Sketch>("Circle");
        require(sketch->addCircle(Point2D{}, 10_mm));
        const FeatureId id = part.addExtrude({.profile = part.addSketch(std::move(sketch)), .depth = 30_mm});
        CHECK_THAT(volumeMm3(requireBody(part.regenerate(id))), WithinRel(pi * 100.0 * 30.0, kRel));
    }
    SECTION("plate with a hole") {
        auto sketch = rectangleSketch(100_mm, 50_mm);
        require(sketch->addCircle(Point2D{50_mm, 25_mm}, 10_mm));
        const FeatureId id = part.addExtrude({.profile = part.addSketch(std::move(sketch)), .depth = 20_mm});
        const auto body = requireBody(part.regenerate(id));
        CHECK_THAT(volumeMm3(body), WithinRel((5000.0 - pi * 100.0) * 20.0, kRel));
        CHECK(body.topology().faces == 7);
    }
    SECTION("slot with arcs") {
        auto sketch = std::make_unique<Sketch>("Slot");
        const EntityId p1 = require(sketch->addPoint(Point2D{0_mm, 0_mm}));
        const EntityId p2 = require(sketch->addPoint(Point2D{40_mm, 0_mm}));
        const EntityId p3 = require(sketch->addPoint(Point2D{40_mm, 20_mm}));
        const EntityId p4 = require(sketch->addPoint(Point2D{0_mm, 20_mm}));
        require(sketch->addLine(p1, p2));
        require(sketch->addArc(require(sketch->addPoint(Point2D{40_mm, 10_mm})), p2, p3));
        require(sketch->addLine(p3, p4));
        require(sketch->addArc(require(sketch->addPoint(Point2D{0_mm, 10_mm})), p4, p1));
        const FeatureId id = part.addExtrude({.profile = part.addSketch(std::move(sketch)), .depth = 5_mm});
        CHECK_THAT(volumeMm3(requireBody(part.regenerate(id))), WithinRel((800.0 + pi * 100.0) * 5.0, kRel));
    }
    SECTION("two separate rectangles give a body with two solids") {
        auto sketch = rectangleSketch(10_mm, 10_mm);
        addRectangle(*sketch, 20_mm, 0_mm, 5_mm, 5_mm);
        const FeatureId id = part.addExtrude({.profile = part.addSketch(std::move(sketch)), .depth = 2_mm});
        const auto body = requireBody(part.regenerate(id));
        CHECK(body.topology().solids == 2);
        CHECK_THAT(volumeMm3(body), WithinRel(250.0, kRel));
    }
}

TEST_CASE("Join, cut and intersect combine with a target body", "[features][extrude]") {
    Part part;
    const SketchId plate = part.addSketch(rectangleSketch(100_mm, 50_mm));
    const FeatureId base = part.addExtrude({.profile = plate, .depth = 20_mm}, "Base");
    const geometry::Body baseBody = requireBody(part.regenerate(base));

    auto holeSketch = std::make_unique<Sketch>("Hole");
    require(holeSketch->addCircle(Point2D{50_mm, 25_mm}, 10_mm));
    const SketchId hole = part.addSketch(std::move(holeSketch));

    const FeatureId cut = part.addExtrude(
        {.profile = hole, .depth = 20_mm, .operation = FeatureOperation::Cut, .target = base}, "Cut");
    CHECK_THAT(volumeMm3(requireBody(part.regenerate(cut, &baseBody))),
               WithinRel(100000.0 - pi * 100.0 * 20.0, kRel));

    const FeatureId join = part.addExtrude(
        {.profile = hole, .depth = 30_mm, .direction = ExtrudeDirection::Reversed,
         .operation = FeatureOperation::Join, .target = base},
        "Boss");
    CHECK_THAT(volumeMm3(requireBody(part.regenerate(join, &baseBody))),
               WithinRel(100000.0 + pi * 100.0 * 30.0, kRel));

    const FeatureId intersect = part.addExtrude(
        {.profile = hole, .depth = 50_mm, .direction = ExtrudeDirection::Symmetric,
         .operation = FeatureOperation::Intersect, .target = base},
        "Core");
    CHECK_THAT(volumeMm3(requireBody(part.regenerate(intersect, &baseBody))),
               WithinRel(pi * 100.0 * 20.0, kRel));

    CHECK(errorCode(part.regenerate(cut)) == ErrorCode::FailedPrecondition); // no target body
}

TEST_CASE("Extrude definitions are validated", "[features][extrude]") {
    const auto sketch = SketchId::fromValue(1);
    CHECK(errorCode(ExtrudeFeature::create("E", {.profile = sketch, .depth = 0_mm})) == ErrorCode::InvalidArgument);
    CHECK(errorCode(ExtrudeFeature::create("E", {.profile = SketchId{}, .depth = 5_mm})) == ErrorCode::InvalidArgument);
    CHECK(errorCode(ExtrudeFeature::create("E", {.profile = sketch, .depth = 5_mm, .operation = FeatureOperation::Cut})) ==
          ErrorCode::InvalidArgument);
    CHECK(errorCode(ExtrudeFeature::create("E", {.profile = sketch, .depth = 5_mm, .target = FeatureId::fromValue(2)})) ==
          ErrorCode::InvalidArgument);
    // A driving parameter replaces the literal depth.
    CHECK(ExtrudeFeature::create("E", {.profile = sketch, .depthParameter = ParameterId::fromValue(3)}).has_value());
}

TEST_CASE("Regeneration reports broken references", "[features][extrude]") {
    Part part;
    const SketchId sketch = part.addSketch(rectangleSketch(10_mm, 10_mm));

    SECTION("missing sketch") {
        const FeatureId id = part.addExtrude({.profile = SketchId::fromValue(99), .depth = 5_mm});
        CHECK(errorCode(part.regenerate(id)) == ErrorCode::NotFound);
    }
    SECTION("profile that is not a sketch") {
        const FeatureId first = part.addExtrude({.profile = sketch, .depth = 5_mm});
        const FeatureId id = part.addExtrude({.profile = SketchId::fromValue(first.value()), .depth = 5_mm});
        CHECK(errorCode(part.regenerate(id)) == ErrorCode::NotFound);
    }
    SECTION("missing or mistyped depth parameter") {
        const FeatureId missing = part.addExtrude({.profile = sketch, .depthParameter = ParameterId::fromValue(99)});
        CHECK(errorCode(part.regenerate(missing)) == ErrorCode::NotFound);
        const auto angle = part.doc.createParameter("draft", 5_deg, units::deg);
        REQUIRE(angle.has_value());
        const FeatureId wrong = part.addExtrude({.profile = sketch, .depthParameter = *angle});
        CHECK(errorCode(part.regenerate(wrong)) == ErrorCode::DimensionMismatch);
    }
    SECTION("open profile") {
        auto open = std::make_unique<Sketch>("Open");
        require(open->addLine(Point2D{0_mm, 0_mm}, Point2D{10_mm, 0_mm}));
        const FeatureId id = part.addExtrude({.profile = part.addSketch(std::move(open)), .depth = 5_mm});
        CHECK(errorCode(part.regenerate(id)) == ErrorCode::FailedPrecondition);
    }
}

TEST_CASE("Extrude features are created and modified through commands", "[features][extrude][commands]") {
    Part part;
    const SketchId sketch = part.addSketch(rectangleSketch(100_mm, 50_mm));
    CommandHistory history;

    auto create = std::make_unique<CreateExtrudeCommand>("Extrude1", ExtrudeDefinition{.profile = sketch, .depth = 20_mm});
    CreateExtrudeCommand* createRaw = create.get();
    REQUIRE(history.execute(part.doc, std::move(create)).has_value());
    const FeatureId id = createRaw->featureId();
    const auto* feature = part.doc.findObjectAs<ExtrudeFeature>(ObjectId{id});
    REQUIRE(feature != nullptr);
    CHECK(feature->typeName() == "extrude");
    CHECK(feature->featureId() == id);

    REQUIRE(history.execute(part.doc, std::make_unique<ModifyExtrudeCommand>(
                                          id, ExtrudeDefinition{.profile = sketch, .depth = 40_mm}))
                .has_value());
    CHECK_THAT(volumeMm3(requireBody(part.regenerate(id))), WithinRel(200000.0, kRel));

    REQUIRE(history.undo(part.doc).has_value());
    CHECK_THAT(volumeMm3(requireBody(part.regenerate(id))), WithinRel(100000.0, kRel));
    REQUIRE(history.undo(part.doc).has_value());
    CHECK(part.doc.findObject(ObjectId{id}) == nullptr);
    REQUIRE(history.redo(part.doc).has_value());
    REQUIRE(history.redo(part.doc).has_value());
    CHECK(part.doc.findObjectAs<ExtrudeFeature>(ObjectId{id})->definition().depth == 40_mm);

    CHECK(errorCode(history.execute(part.doc, std::make_unique<CreateExtrudeCommand>(
                                                  "Bad", ExtrudeDefinition{.profile = sketch}))) ==
          ErrorCode::InvalidArgument);
}
