#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/drawing/Dimensions.hpp>
#include <bettercad/drawing/Sheets.hpp>
#include <bettercad/drawing/Views.hpp>
#include <bettercad/features/Datums.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <cmath>
#include <memory>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using drawing::DimensionDefinition;
using drawing::DimensionFormat;
using drawing::DimensionTarget;
using drawing::DimensionType;
using drawing::DrawingScale;
using drawing::OrdinateAxis;
using drawing::SheetDefinition;
using drawing::StandardView;
using drawing::ViewDefinition;

// P14-DIM-001: dimensions, against arithmetic done here.
//
// WHERE THE EXPECTED NUMBERS COME FROM. Every fixture is a prism whose
// dimensions are written into its sketch, so every expected answer is one of
// those numbers or a closed form on them -- a 3-4-5 triangle's angles are
// atan2(4,3) and atan2(3,4), and its offsets project onto view axes as 4 and
// 3. No expected value is read back from the routine under test.
//
// TOLERANCES. Values that come through the kernel (a face's plane, a
// cylinder's radius) are compared to 1e-9 mm: they are exact surfaces read
// exactly, and the only error is the double arithmetic of resolving them.
// Angles from atan2 of exact components are compared to 1e-12 rad. Text is
// compared exactly.
namespace {

constexpr double kMm = 1e-9;

struct Fixture {
    Document document{"Drawing"};
    ObjectId part{};
    SheetId sheet{};
    ViewId view{};
    std::array<EntityId, 4> lines{};
    features::Regenerator regenerator;

    drawing::BodyLookup bodies() {
        const features::Regenerator* r = &regenerator;
        return [r](ObjectId object) { return r->body(object); };
    }
    void regenerate() { REQUIRE(regenerator.regenerateAll(document).has_value()); }
};

/// The plane of a named SIDE face: the one swept by @p entity of the
/// feature's profile.
PlaneReference sideFace(ObjectId feature, EntityId entity) {
    return PlaneReference{.object = feature,
                          .face = FaceSelector{.role = FaceRole::Side, .entity = entity}};
}

/// The plane of a named cap face.
PlaneReference capFace(ObjectId feature, FaceRole role) {
    return PlaneReference{.object = feature, .face = FaceSelector{.role = role}};
}

DimensionTarget onPlane(const PlaneReference& reference) {
    return DimensionTarget{.plane = reference};
}

SheetId addSheet(Document& document, DrawingScale scale = DrawingScale{1, 1}) {
    return require(drawing::createSheet(
        document, "Sheet1",
        SheetDefinition{.format = drawing::SheetFormat::A3,
                        .orientation = drawing::SheetOrientation::Landscape,
                        .margins = drawing::SheetMargins{10_mm, 10_mm, 10_mm, 10_mm},
                        .scale = scale}));
}

/// A 100 x 60 x 40 block: the rectangle (0,0)-(100,60) on XY, extruded 40 up
/// +Z. Its four side faces are named by the rectangle's four lines, and its
/// caps sit at z = 0 and z = 40.
Fixture makeBlock(StandardView orientation = StandardView::Front,
                  DrawingScale scale = DrawingScale{1, 1}) {
    Fixture f;
    auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
    f.lines = addRectangle(*sketch, 0_mm, 0_mm, 100_mm, 60_mm);
    const ObjectId sketchId = require(f.document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Block", {.profile = SketchId::fromValue(sketchId.value()), .depth = 40_mm});
    REQUIRE(extrude.has_value());
    f.part = require(f.document.addObject(std::move(*extrude)));

    f.sheet = addSheet(f.document, scale);
    f.view = require(drawing::createView(
        f.document, "MainView",
        ViewDefinition{.sheet = f.sheet,
                       .source = ObjectReference{f.part},
                       .orientation = orientation,
                       .placement = Point2D{200_mm, 150_mm}}));
    f.regenerate();
    return f;
}

/// A 3-4-5 prism: the triangle (0,0) (30,0) (0,40) on XY, extruded 60 up +Z.
/// Its hypotenuse runs from (30,0) to (0,40), so that face's outward normal
/// is (40, 30, 0) / 50 and the triangle's angles are atan2(40,30) = 53.13 and
/// atan2(30,40) = 36.87 degrees.
struct Wedge {
    Fixture f;
    EntityId base{};       ///< the leg along y = 0, outward normal (0,-1,0)
    EntityId upright{};    ///< the leg along x = 0, outward normal (-1,0,0)
    EntityId hypotenuse{}; ///< outward normal (0.8, 0.6, 0)
};

Wedge makeWedge(StandardView orientation = StandardView::Top) {
    Wedge w;
    auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
    const EntityId a = require(sketch->addPoint(Point2D{0_mm, 0_mm}));
    const EntityId b = require(sketch->addPoint(Point2D{30_mm, 0_mm}));
    const EntityId c = require(sketch->addPoint(Point2D{0_mm, 40_mm}));
    w.base = require(sketch->addLine(a, b));
    w.hypotenuse = require(sketch->addLine(b, c));
    w.upright = require(sketch->addLine(c, a));
    const ObjectId sketchId = require(w.f.document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Wedge", {.profile = SketchId::fromValue(sketchId.value()), .depth = 60_mm});
    REQUIRE(extrude.has_value());
    w.f.part = require(w.f.document.addObject(std::move(*extrude)));

    w.f.sheet = addSheet(w.f.document);
    w.f.view = require(drawing::createView(
        w.f.document, "MainView",
        ViewDefinition{.sheet = w.f.sheet,
                       .source = ObjectReference{w.f.part},
                       .orientation = orientation,
                       .placement = Point2D{200_mm, 150_mm}}));
    w.f.regenerate();
    return w;
}

/// A rod: a circle of radius 20 on XY, extruded 80 up +Z. Its cylindrical
/// side face is named by the circle.
struct Rod {
    Fixture f;
    EntityId circle{};
};

Rod makeRod() {
    Rod r;
    auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
    r.circle = require(sketch->addCircle(Point2D{0_mm, 0_mm}, 20_mm));
    const ObjectId sketchId = require(r.f.document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Rod", {.profile = SketchId::fromValue(sketchId.value()), .depth = 80_mm});
    REQUIRE(extrude.has_value());
    r.f.part = require(r.f.document.addObject(std::move(*extrude)));

    r.f.sheet = addSheet(r.f.document);
    r.f.view = require(drawing::createView(
        r.f.document, "MainView",
        ViewDefinition{.sheet = r.f.sheet,
                       .source = ObjectReference{r.f.part},
                       .orientation = StandardView::Front,
                       .placement = Point2D{200_mm, 150_mm}}));
    r.f.regenerate();
    return r;
}

/// Measures, requiring success, and returns the length in millimetres.
double lengthOf(Fixture& f, DimensionId id) {
    auto measured = drawing::measure(f.document, id, f.bodies());
    REQUIRE(measured.has_value());
    REQUIRE(measured->length.has_value());
    return measured->length->in(units::mm);
}

double degreesOf(Fixture& f, DimensionId id) {
    auto measured = drawing::measure(f.document, id, f.bodies());
    REQUIRE(measured.has_value());
    REQUIRE(measured->angle.has_value());
    return measured->angle->in(units::deg);
}

/// Adds a dimension with the display unit its type calls for -- an angle
/// written in millimetres is refused, which is the point of that rule and not
/// something a test helper should have to work around.
DimensionId add(Fixture& f, const std::string& name, DimensionType type,
                const DimensionTarget& from, const DimensionTarget& to) {
    DimensionFormat format;
    if (drawing::isAngular(type)) {
        format.unit = "deg";
    }
    return require(drawing::createDimension(
        f.document, name,
        DimensionDefinition{
            .view = f.view, .type = type, .from = from, .to = to, .format = format}));
}

} // namespace

// --- Linear: the true model distance ---------------------------------------------------

TEST_CASE("Dimension_LinearMeasuresTheBlocksOwnThreeSizes", "[drawing][dimension][p14]") {
    // The block is 100 x 60 x 40 because its sketch says 100 x 60 and its
    // extrude says 40. Each dimension below is between the two named faces
    // that bound one of those, so each must give back the number that was
    // typed into the model.
    Fixture f = makeBlock();

    const DimensionId width = add(f, "Width", DimensionType::Linear,
                                  onPlane(sideFace(f.part, f.lines[3])),
                                  onPlane(sideFace(f.part, f.lines[1])));
    const DimensionId depth = add(f, "Depth", DimensionType::Linear,
                                  onPlane(sideFace(f.part, f.lines[0])),
                                  onPlane(sideFace(f.part, f.lines[2])));
    const DimensionId height = add(f, "Height", DimensionType::Linear,
                                   onPlane(capFace(f.part, FaceRole::StartCap)),
                                   onPlane(capFace(f.part, FaceRole::EndCap)));

    CHECK_THAT(lengthOf(f, width), WithinAbs(100.0, kMm));
    CHECK_THAT(lengthOf(f, depth), WithinAbs(60.0, kMm));
    CHECK_THAT(lengthOf(f, height), WithinAbs(40.0, kMm));
}

TEST_CASE("Dimension_LinearBetweenPlanesThatAreNotParallelIsRefused",
          "[drawing][dimension][p14]") {
    // Two faces at an angle have no ONE distance between them: the answer
    // would depend on where along them it was taken. Refused, with the
    // measurement that IS defined named in the message.
    Fixture f = makeBlock();
    const DimensionId bad = add(f, "Bad", DimensionType::Linear,
                                onPlane(sideFace(f.part, f.lines[0])),
                                onPlane(sideFace(f.part, f.lines[1])));
    const auto measured = drawing::measure(f.document, bad, f.bodies());
    REQUIRE_FALSE(measured.has_value());
    CHECK(errorCode(measured) == ErrorCode::FailedPrecondition);
    CHECK_THAT(measured.error().message, ContainsSubstring("not parallel"));
    CHECK_THAT(measured.error().message, ContainsSubstring("angular"));
}

// --- Horizontal, vertical and aligned: the VIEW's own axes -----------------------------

TEST_CASE("Dimension_ViewMeasurementsUseTheViewsAxesAndNotTheWorldsAxes",
          "[drawing][dimension][p14]") {
    // A front view looks along +Y: its right is +X and its up is +Z. So the
    // block's width lies along the view's X, its height along the view's Y,
    // and its DEPTH along the view's normal -- where it cannot be drawn at
    // all, and every view measurement of it is zero while the model distance
    // is still 60.
    Fixture f = makeBlock(StandardView::Front);
    const DimensionTarget left = onPlane(sideFace(f.part, f.lines[3]));
    const DimensionTarget right = onPlane(sideFace(f.part, f.lines[1]));
    const DimensionTarget near = onPlane(sideFace(f.part, f.lines[0]));
    const DimensionTarget far = onPlane(sideFace(f.part, f.lines[2]));

    CHECK_THAT(lengthOf(f, add(f, "W1", DimensionType::Horizontal, left, right)),
               WithinAbs(100.0, kMm));
    CHECK_THAT(lengthOf(f, add(f, "W2", DimensionType::Vertical, left, right)),
               WithinAbs(0.0, kMm));
    CHECK_THAT(lengthOf(f, add(f, "W3", DimensionType::Aligned, left, right)),
               WithinAbs(100.0, kMm));

    // The depth is edge-on in this view: nothing of it is drawable, and the
    // model still knows it is 60.
    CHECK_THAT(lengthOf(f, add(f, "D1", DimensionType::Horizontal, near, far)),
               WithinAbs(0.0, kMm));
    CHECK_THAT(lengthOf(f, add(f, "D2", DimensionType::Vertical, near, far)),
               WithinAbs(0.0, kMm));
    CHECK_THAT(lengthOf(f, add(f, "D3", DimensionType::Aligned, near, far)),
               WithinAbs(0.0, kMm));
    CHECK_THAT(lengthOf(f, add(f, "D4", DimensionType::Linear, near, far)),
               WithinAbs(60.0, kMm));
}

TEST_CASE("Dimension_TheSameDistanceFallsOnADifferentAxisInADifferentView",
          "[drawing][dimension][p14]") {
    // The depth that a front view cannot show is what a top view measures up
    // its own Y. Same model, same references, different view: the number is
    // the model's, the AXIS is the view's.
    Fixture top = makeBlock(StandardView::Top);
    const DimensionTarget near = onPlane(sideFace(top.part, top.lines[0]));
    const DimensionTarget far = onPlane(sideFace(top.part, top.lines[2]));

    CHECK_THAT(lengthOf(top, add(top, "D1", DimensionType::Horizontal, near, far)),
               WithinAbs(0.0, kMm));
    CHECK_THAT(lengthOf(top, add(top, "D2", DimensionType::Vertical, near, far)),
               WithinAbs(60.0, kMm));
}

TEST_CASE("Dimension_AlignedSplitsIntoTheViewAxesAsThreeFourFive",
          "[drawing][dimension][p14]") {
    // The wedge's hypotenuse face has outward normal (40, 30, 0) / 50, so a
    // plane 50 mm off it is displaced (40, 30, 0). Seen from the top -- right
    // +X, up +Y -- that is 40 across, 30 up, and 50 along, which is the one
    // case where horizontal, vertical and aligned are three different known
    // numbers and none of them is the model distance by accident.
    Wedge w = makeWedge(StandardView::Top);
    auto datum = features::DatumPlane::create(
        "Offset", {.kind = features::DatumPlaneKind::Offset,
                   .base = sideFace(w.f.part, w.hypotenuse),
                   .offset = 50_mm});
    REQUIRE(datum.has_value());
    const ObjectId offsetPlane = require(w.f.document.addObject(std::move(*datum)));
    w.f.regenerate();

    const DimensionTarget face = onPlane(sideFace(w.f.part, w.hypotenuse));
    const DimensionTarget off = onPlane(PlaneReference{.object = offsetPlane});

    CHECK_THAT(lengthOf(w.f, add(w.f, "H", DimensionType::Horizontal, face, off)),
               WithinAbs(40.0, 1e-6));
    CHECK_THAT(lengthOf(w.f, add(w.f, "V", DimensionType::Vertical, face, off)),
               WithinAbs(30.0, 1e-6));
    CHECK_THAT(lengthOf(w.f, add(w.f, "A", DimensionType::Aligned, face, off)),
               WithinAbs(50.0, 1e-6));
    CHECK_THAT(lengthOf(w.f, add(w.f, "L", DimensionType::Linear, face, off)),
               WithinAbs(50.0, 1e-6));
}

// --- Scale is a drawing choice, never a measurement ------------------------------------

TEST_CASE("Dimension_TheDrawingScaleNeverChangesTheMeasuredValue",
          "[drawing][dimension][p14]") {
    // The hard gate. A 100 mm feature is 100 mm whether it is drawn full
    // size, half size or twice size -- what changes is how long the line on
    // the paper is, which is not this number.
    for (const DrawingScale scale : {DrawingScale{1, 1}, DrawingScale{1, 2}, DrawingScale{2, 1},
                                     DrawingScale{1, 10}, DrawingScale{5, 1}}) {
        INFO("scale " << scale.paper << ":" << scale.model);
        Fixture f = makeBlock(StandardView::Front, scale);
        const DimensionId width = add(f, "Width", DimensionType::Horizontal,
                                      onPlane(sideFace(f.part, f.lines[3])),
                                      onPlane(sideFace(f.part, f.lines[1])));
        CHECK_THAT(lengthOf(f, width), WithinAbs(100.0, kMm));
        CHECK_THAT(lengthOf(f, add(f, "Model", DimensionType::Linear,
                                   onPlane(sideFace(f.part, f.lines[3])),
                                   onPlane(sideFace(f.part, f.lines[1])))),
                   WithinAbs(100.0, kMm));
    }
}

TEST_CASE("Dimension_MovingTheViewOnTheSheetNeverChangesTheMeasuredValue",
          "[drawing][dimension][p14]") {
    Fixture f = makeBlock();
    const DimensionId width = add(f, "Width", DimensionType::Horizontal,
                                  onPlane(sideFace(f.part, f.lines[3])),
                                  onPlane(sideFace(f.part, f.lines[1])));
    const double before = lengthOf(f, width);

    ViewDefinition moved = drawing::findView(f.document, f.view)->definition();
    moved.placement = Point2D{40_mm, 90_mm};
    REQUIRE(drawing::setViewDefinition(f.document, f.view, moved).has_value());

    CHECK_THAT(lengthOf(f, width), WithinAbs(before, 0.0));
    CHECK_THAT(lengthOf(f, width), WithinAbs(100.0, kMm));
}

// --- Angular ---------------------------------------------------------------------------

TEST_CASE("Dimension_AngularReadsTheTrianglesOwnAngles", "[drawing][dimension][p14]") {
    // A 30-40-50 triangle has angles atan2(40,30) = 53.130 and
    // atan2(30,40) = 36.870 degrees, and a right angle between its legs. All
    // three are measured between named faces of the same prism, so the three
    // results must also add to 180.
    Wedge w = makeWedge();
    const DimensionTarget base = onPlane(sideFace(w.f.part, w.base));
    const DimensionTarget upright = onPlane(sideFace(w.f.part, w.upright));
    const DimensionTarget hypotenuse = onPlane(sideFace(w.f.part, w.hypotenuse));

    const double atBase = degreesOf(w.f, add(w.f, "A1", DimensionType::Angular, base, hypotenuse));
    const double atTop = degreesOf(w.f, add(w.f, "A2", DimensionType::Angular, upright, hypotenuse));
    const double atCorner = degreesOf(w.f, add(w.f, "A3", DimensionType::Angular, base, upright));

    const double expectedBase = std::atan2(40.0, 30.0) * 180.0 / std::numbers::pi;
    const double expectedTop = std::atan2(30.0, 40.0) * 180.0 / std::numbers::pi;
    CHECK_THAT(atBase, WithinAbs(expectedBase, 1e-9));
    CHECK_THAT(atTop, WithinAbs(expectedTop, 1e-9));
    CHECK_THAT(atCorner, WithinAbs(90.0, 1e-9));
    CHECK_THAT(atBase + atTop + atCorner, WithinAbs(180.0, 1e-9));
}

TEST_CASE("Dimension_AngularReadsZeroBetweenTwoFacesOfASlab", "[drawing][dimension][p14]") {
    // Two faces of a block face opposite ways, so their outward normals are
    // opposed and the angle a protractor reads between the FACES is nought.
    // This is the case that says the measurement is between the planes and
    // not between their normals.
    Fixture f = makeBlock();
    const double angle = degreesOf(f, add(f, "Flat", DimensionType::Angular,
                                          onPlane(sideFace(f.part, f.lines[3])),
                                          onPlane(sideFace(f.part, f.lines[1]))));
    CHECK_THAT(angle, WithinAbs(0.0, 1e-9));
}

TEST_CASE("Dimension_AngularIsStableWhereAcosWouldNotBe", "[drawing][dimension][p14]") {
    // Nearly parallel is where an angular dimension is most often placed and
    // where acos(dot) loses all its precision and eventually walks out of its
    // domain into NaN. atan2 of the cross against the dot does not.
    Wedge w = makeWedge();
    int index = 0;
    for (const double degrees : {0.001, 0.01, 179.99}) {
        INFO("about " << degrees << " degrees");
        ++index;
        auto datum = features::DatumPlane::create(
            "Tilted" + std::to_string(index), {.kind = features::DatumPlaneKind::Angled,
                       .base = sideFace(w.f.part, w.base),
                       .axis = AxisReference{.axis = PrincipalAxis::Z},
                       .angle = Angle::fromSi(degrees * std::numbers::pi / 180.0)});
        REQUIRE(datum.has_value());
        const ObjectId tilted = require(w.f.document.addObject(std::move(*datum)));
        w.f.regenerate();
        const DimensionId id = add(w.f, "Near" + std::to_string(static_cast<int>(degrees * 1000)),
                                   DimensionType::Angular,
                                   onPlane(sideFace(w.f.part, w.base)),
                                   onPlane(PlaneReference{.object = tilted}));
        const double measured = degreesOf(w.f, id);
        CHECK(std::isfinite(measured));
        CHECK(measured >= 0.0);
        CHECK(measured <= 180.0);
    }
}

// --- Radius and diameter ---------------------------------------------------------------

TEST_CASE("Dimension_RadiusAndDiameterComeFromTheCylinderItself",
          "[drawing][dimension][p14]") {
    // The rod's sketch says radius 20, so its cylindrical face is radius 20
    // and its diameter is 40. Read off the kernel's own surface, never
    // measured from a drawn curve.
    Rod r = makeRod();
    const DimensionTarget bore{.cylinder = FaceName{.feature = r.f.part,
                                                    .face = FaceSelector{.role = FaceRole::Side,
                                                                         .entity = r.circle}}};
    const DimensionId radius = require(drawing::createDimension(
        r.f.document, "Radius",
        DimensionDefinition{.view = r.f.view, .type = DimensionType::Radius, .from = bore}));
    const DimensionId diameter = require(drawing::createDimension(
        r.f.document, "Diameter",
        DimensionDefinition{.view = r.f.view, .type = DimensionType::Diameter, .from = bore}));

    CHECK_THAT(lengthOf(r.f, radius), WithinAbs(20.0, kMm));
    CHECK_THAT(lengthOf(r.f, diameter), WithinAbs(40.0, kMm));
    // Exactly twice, by the one path: the two can never disagree about one
    // face because the diameter is the radius doubled and not a second
    // measurement of the same thing.
    CHECK(lengthOf(r.f, diameter) == 2.0 * lengthOf(r.f, radius));
}

TEST_CASE("Dimension_TheScaleDoesNotChangeARadiusEither", "[drawing][dimension][p14]") {
    Rod r = makeRod();
    const DimensionTarget bore{.cylinder = FaceName{.feature = r.f.part,
                                                    .face = FaceSelector{.role = FaceRole::Side,
                                                                         .entity = r.circle}}};
    const DimensionId radius = require(drawing::createDimension(
        r.f.document, "Radius",
        DimensionDefinition{.view = r.f.view, .type = DimensionType::Radius, .from = bore}));
    const double before = lengthOf(r.f, radius);

    SheetDefinition sheet = drawing::findSheet(r.f.document, r.f.sheet)->definition();
    sheet.scale = DrawingScale{1, 4};
    REQUIRE(drawing::setSheetDefinition(r.f.document, r.f.sheet, sheet).has_value());
    CHECK(lengthOf(r.f, radius) == before);
}

TEST_CASE("Dimension_ARadiusOfSomethingWithNoRadiusIsRefused", "[drawing][dimension][p14]") {
    // A planar face has no radius, and the refusal says so rather than
    // returning a number from somewhere.
    Fixture f = makeBlock();
    const auto bad = drawing::createDimension(
        f.document, "Bad",
        DimensionDefinition{.view = f.view,
                            .type = DimensionType::Radius,
                            .from = onPlane(sideFace(f.part, f.lines[0]))});
    REQUIRE_FALSE(bad.has_value());
    CHECK_THAT(bad.error().message, ContainsSubstring("has no radius"));

    // And a cylindrical NAME on a planar face is caught when it is measured.
    Rod r = makeRod();
    const DimensionTarget notACylinder{
        .cylinder = FaceName{.feature = r.f.part, .face = FaceSelector{.role = FaceRole::EndCap}}};
    const DimensionId id = require(drawing::createDimension(
        r.f.document, "Flat",
        DimensionDefinition{.view = r.f.view, .type = DimensionType::Radius, .from = notACylinder}));
    const auto measured = drawing::measure(r.f.document, id, r.f.bodies());
    REQUIRE_FALSE(measured.has_value());
    CHECK_THAT(measured.error().message, ContainsSubstring("not a cylinder"));
}

// --- Ordinate --------------------------------------------------------------------------

TEST_CASE("Dimension_OrdinateIsSignedFromItsDatum", "[drawing][dimension][p14]") {
    // An ordinate says which side of the datum it is on, so it is signed: a
    // negative one is information and not an error. Measured from the model's
    // own YZ plane, the block's right-hand face is at +100 and its left-hand
    // face at 0; measured from the right-hand face, the left one is at -100.
    Fixture f = makeBlock(StandardView::Top);
    const DimensionTarget origin = onPlane(PlaneReference{.plane = PrincipalPlane::YZ});
    const DimensionTarget left = onPlane(sideFace(f.part, f.lines[3]));
    const DimensionTarget right = onPlane(sideFace(f.part, f.lines[1]));

    const auto ordinate = [&](const std::string& name, const DimensionTarget& from,
                              const DimensionTarget& to) {
        return require(drawing::createDimension(
            f.document, name,
            DimensionDefinition{.view = f.view,
                                .type = DimensionType::Ordinate,
                                .from = from,
                                .to = to,
                                .ordinate = OrdinateAxis::X}));
    };

    CHECK_THAT(lengthOf(f, ordinate("Right", origin, right)), WithinAbs(100.0, kMm));
    CHECK_THAT(lengthOf(f, ordinate("Left", origin, left)), WithinAbs(0.0, kMm));
    CHECK_THAT(lengthOf(f, ordinate("Back", right, left)), WithinAbs(-100.0, kMm));
}

TEST_CASE("Dimension_ChangingTheOrdinateDatumChangesTheCoordinate",
          "[drawing][dimension][p14]") {
    Fixture f = makeBlock(StandardView::Top);
    const DimensionId id = require(drawing::createDimension(
        f.document, "Ord",
        DimensionDefinition{.view = f.view,
                            .type = DimensionType::Ordinate,
                            .from = onPlane(PlaneReference{.plane = PrincipalPlane::YZ}),
                            .to = onPlane(sideFace(f.part, f.lines[1])),
                            .ordinate = OrdinateAxis::X}));
    CHECK_THAT(lengthOf(f, id), WithinAbs(100.0, kMm));

    DimensionDefinition moved = drawing::findDimension(f.document, id)->definition();
    moved.from = onPlane(sideFace(f.part, f.lines[3]));
    REQUIRE(drawing::setDimensionDefinition(f.document, id, moved).has_value());
    CHECK_THAT(lengthOf(f, id), WithinAbs(100.0, kMm)); // the left face is also at x = 0
}

// --- Model-driven values ---------------------------------------------------------------

TEST_CASE("Dimension_FollowsTheModelWhenAParameterChanges", "[drawing][dimension][p14]") {
    // The guarantee the whole design exists for. The stored dimension holds
    // no number, so changing the model and regenerating gives the new one --
    // same DimensionId, same references, no edit to the drawing.
    Fixture f = makeBlock();
    const DimensionId height = add(f, "Height", DimensionType::Linear,
                                   onPlane(capFace(f.part, FaceRole::StartCap)),
                                   onPlane(capFace(f.part, FaceRole::EndCap)));
    CHECK_THAT(lengthOf(f, height), WithinAbs(40.0, kMm));

    for (const double depth : {125.0, 7.5, 0.25, 999.0}) {
        INFO("depth " << depth);
        const auto* extrude = f.document.findObjectAs<features::ExtrudeFeature>(f.part);
        REQUIRE(extrude != nullptr);
        auto definition = extrude->definition();
        definition.depth = Length::fromSi(depth / 1000.0);
        REQUIRE(f.document
                    .modifyObject<features::ExtrudeFeature>(
                        f.part, [&](features::ExtrudeFeature& e) { return e.setDefinition(definition); })
                    .has_value());
        f.regenerate();
        CHECK_THAT(lengthOf(f, height), WithinAbs(depth, kMm));
    }
}

// --- Units, precision and formatting ---------------------------------------------------

TEST_CASE("Dimension_FormatsRoundHalfAwayFromZeroAndNotHalfToEven",
          "[drawing][dimension][p14]") {
    // std::format rounds half to EVEN, so 0.125 at two decimals would come
    // out 0.12 because the digit before it is even. A drawing office rounds
    // it up. Getting this wrong is one dimension disagreeing with another
    // produced from the same number.
    DimensionFormat format;
    format.decimals = 2;
    for (const auto& [value, expected] :
         std::vector<std::pair<double, std::string>>{{0.125, "0.13"},
                                                     {0.135, "0.14"},
                                                     {0.145, "0.15"},
                                                     {2.005, "2.01"},
                                                     {-0.125, "-0.13"},
                                                     {-2.005, "-2.01"}}) {
        INFO(value << " mm");
        const auto text = drawing::formatLength(Length::fromSi(value / 1000.0), format);
        REQUIRE(text.has_value());
        CHECK(*text == expected);
    }
}

TEST_CASE("Dimension_FormatsWriteTheDecimalsAsked", "[drawing][dimension][p14]") {
    const Length value = 12.3456_mm;
    for (const auto& [decimals, expected] :
         std::vector<std::pair<std::uint8_t, std::string>>{
             {0, "12"}, {1, "12.3"}, {2, "12.35"}, {3, "12.346"}, {4, "12.3456"}}) {
        INFO(static_cast<int>(decimals) << " decimals");
        DimensionFormat format;
        format.decimals = decimals;
        const auto text = drawing::formatLength(value, format);
        REQUIRE(text.has_value());
        CHECK(*text == expected);
    }
}

TEST_CASE("Dimension_TrailingZerosAreADrawingOfficeChoice", "[drawing][dimension][p14]") {
    DimensionFormat kept;
    kept.decimals = 3;
    kept.trailingZeros = true;
    DimensionFormat dropped = kept;
    dropped.trailingZeros = false;

    for (const auto& [value, with, without] :
         std::vector<std::tuple<Length, std::string, std::string>>{
             {100_mm, "100.000", "100"},
             {100.5_mm, "100.500", "100.5"},
             {0_mm, "0.000", "0"},
             {0.25_mm, "0.250", "0.25"}}) {
        INFO(value.in(units::mm) << " mm");
        CHECK(*drawing::formatLength(value, kept) == with);
        CHECK(*drawing::formatLength(value, dropped) == without);
    }
}

TEST_CASE("Dimension_TheDisplayUnitConvertsAndTheModelDoesNot",
          "[drawing][dimension][p14]") {
    // 100 mm is 10 cm is 0.1 m is 3.9370 inches. The model holds one number;
    // the unit decides how it reads.
    const Length value = 100_mm;
    for (const auto& [unit, decimals, expected] :
         std::vector<std::tuple<std::string, std::uint8_t, std::string>>{
             {"mm", 1, "100.0"}, {"cm", 1, "10.0"}, {"m", 3, "0.100"}, {"in", 4, "3.9370"}}) {
        INFO(unit);
        DimensionFormat format;
        format.unit = unit;
        format.decimals = decimals;
        const auto text = drawing::formatLength(value, format);
        REQUIRE(text.has_value());
        CHECK(*text == expected);
    }

    DimensionFormat withSymbol;
    withSymbol.decimals = 1;
    withSymbol.showUnit = true;
    CHECK(*drawing::formatLength(value, withSymbol) == "100.0 mm");
}

TEST_CASE("Dimension_AnglesAreWrittenInAngleUnits", "[drawing][dimension][p14]") {
    const Angle right = Angle::fromSi(std::numbers::pi / 2.0);
    DimensionFormat degrees;
    degrees.unit = "deg";
    degrees.decimals = 1;
    CHECK(*drawing::formatAngle(right, degrees) == "90.0");

    DimensionFormat radians;
    radians.unit = "rad";
    radians.decimals = 4;
    CHECK(*drawing::formatAngle(right, radians) == "1.5708");
}

TEST_CASE("Dimension_AnAngleCannotBeWrittenInMillimetres", "[drawing][dimension][p14]") {
    // Not a formatting choice but a category error: the drawing would read as
    // a length.
    Wedge w = makeWedge();
    const auto bad = drawing::createDimension(
        w.f.document, "Bad",
        DimensionDefinition{.view = w.f.view,
                            .type = DimensionType::Angular,
                            .from = onPlane(sideFace(w.f.part, w.base)),
                            .to = onPlane(sideFace(w.f.part, w.hypotenuse)),
                            .format = DimensionFormat{.unit = "mm"}});
    REQUIRE_FALSE(bad.has_value());
    CHECK_THAT(bad.error().message, ContainsSubstring("cannot be written in mm"));

    const auto alsoBad = drawing::createDimension(
        w.f.document, "AlsoBad",
        DimensionDefinition{.view = w.f.view,
                            .type = DimensionType::Linear,
                            .from = onPlane(sideFace(w.f.part, w.base)),
                            .to = onPlane(sideFace(w.f.part, w.hypotenuse)),
                            .format = DimensionFormat{.unit = "deg"}});
    REQUIRE_FALSE(alsoBad.has_value());
    CHECK_THAT(alsoBad.error().message, ContainsSubstring("cannot be written in deg"));
}

TEST_CASE("Dimension_ChangingThePrecisionDoesNotChangeTheMeasurement",
          "[drawing][dimension][p14]") {
    // Precision decides how the number READS. The number itself is the
    // model's, and rounding it for display must not round it in the model.
    Fixture f = makeBlock();
    const DimensionId id = add(f, "Width", DimensionType::Linear,
                               onPlane(sideFace(f.part, f.lines[3])),
                               onPlane(sideFace(f.part, f.lines[1])));
    const double before = lengthOf(f, id);

    for (const std::uint8_t decimals : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{6}}) {
        DimensionDefinition definition = drawing::findDimension(f.document, id)->definition();
        definition.format.decimals = decimals;
        REQUIRE(drawing::setDimensionDefinition(f.document, id, definition).has_value());
        auto measured = drawing::measure(f.document, id, f.bodies());
        REQUIRE(measured.has_value());
        CHECK(measured->length->si() == Length::fromSi(before / 1000.0).si());
    }
}

// --- References -------------------------------------------------------------------------

TEST_CASE("Dimension_UnrelatedEditsDoNotRetargetIt", "[drawing][dimension][p14]") {
    // Adding, renaming and deleting other objects must leave a dimension
    // pointing where it was pointed.
    Fixture f = makeBlock();
    const DimensionId width = add(f, "Width", DimensionType::Linear,
                                  onPlane(sideFace(f.part, f.lines[3])),
                                  onPlane(sideFace(f.part, f.lines[1])));
    const DimensionDefinition before = drawing::findDimension(f.document, width)->definition();
    CHECK_THAT(lengthOf(f, width), WithinAbs(100.0, kMm));

    auto spare = std::make_unique<sketch::Sketch>("Spare", Frame3D::yz());
    addRectangle(*spare, 0_mm, 0_mm, 5_mm, 5_mm);
    const ObjectId spareId = require(f.document.addObject(std::move(spare)));
    auto datum = features::DatumPlane::create(
        "Loose", {.kind = features::DatumPlaneKind::Offset,
                  .base = PlaneReference{.plane = PrincipalPlane::XY},
                  .offset = 7_mm});
    REQUIRE(datum.has_value());
    const ObjectId datumId = require(f.document.addObject(std::move(*datum)));
    REQUIRE(f.document.rename(datumId, "Renamed").has_value());
    REQUIRE(f.document.removeObject(spareId).has_value());
    f.regenerate();

    CHECK(drawing::findDimension(f.document, width)->definition() == before);
    CHECK_THAT(lengthOf(f, width), WithinAbs(100.0, kMm));
}

TEST_CASE("Dimension_AMissingReferenceFailsAndNeverShowsTheOldNumber",
          "[drawing][dimension][p14]") {
    // The failure that matters most. When what a dimension measured is gone,
    // it must say so -- not keep showing the last number it managed to get,
    // which is how a drawing comes to disagree with the part.
    Fixture f = makeBlock();
    const DimensionId height = add(f, "Height", DimensionType::Linear,
                                   onPlane(capFace(f.part, FaceRole::StartCap)),
                                   onPlane(capFace(f.part, FaceRole::EndCap)));
    CHECK_THAT(lengthOf(f, height), WithinAbs(40.0, kMm));

    REQUIRE(f.document.removeObject(f.part).has_value());
    f.regenerate();

    const auto measured = drawing::measure(f.document, height, f.bodies());
    REQUIRE_FALSE(measured.has_value());
    CHECK_THAT(measured.error().message, ContainsSubstring("Height"));
}

TEST_CASE("Dimension_NeverRebindsToGeometryThatMerelyLooksTheSame",
          "[drawing][dimension][p14]") {
    // The hard gate. Two features make faces on exactly the same plane. A
    // dimension is pointed at one of them and that feature is deleted. A
    // resolver that matched geometry would find the survivor -- same plane,
    // same normal, indistinguishable -- and go on showing a number as if
    // nothing had happened. Resolution is by NAME, so it must not.
    Fixture f;
    auto sketchA = std::make_unique<sketch::Sketch>("ProfileA", Frame3D::xy());
    addRectangle(*sketchA, 0_mm, 0_mm, 40_mm, 40_mm);
    const ObjectId a = require(f.document.addObject(std::move(sketchA)));
    auto first = features::ExtrudeFeature::create(
        "First", {.profile = SketchId::fromValue(a.value()), .depth = 40_mm});
    REQUIRE(first.has_value());
    const ObjectId firstId = require(f.document.addObject(std::move(*first)));

    auto sketchB = std::make_unique<sketch::Sketch>("ProfileB", Frame3D::xy());
    addRectangle(*sketchB, 100_mm, 0_mm, 40_mm, 40_mm);
    const ObjectId b = require(f.document.addObject(std::move(sketchB)));
    auto second = features::ExtrudeFeature::create(
        "Second", {.profile = SketchId::fromValue(b.value()), .depth = 40_mm});
    REQUIRE(second.has_value());
    const ObjectId secondId = require(f.document.addObject(std::move(*second)));

    f.sheet = addSheet(f.document);
    f.view = require(drawing::createView(
        f.document, "MainView",
        ViewDefinition{.sheet = f.sheet,
                       .source = ObjectReference{firstId},
                       .orientation = StandardView::Front,
                       .placement = Point2D{200_mm, 150_mm}}));
    f.regenerate();

    // Both prisms have an end cap at z = 40: the same plane, facing the same
    // way, telling nothing apart.
    const DimensionId height = require(drawing::createDimension(
        f.document, "Height",
        DimensionDefinition{.view = f.view,
                            .type = DimensionType::Linear,
                            .from = onPlane(capFace(firstId, FaceRole::StartCap)),
                            .to = onPlane(capFace(firstId, FaceRole::EndCap))}));
    CHECK_THAT(lengthOf(f, height), WithinAbs(40.0, kMm));

    // The survivor still has a face on that plane.
    REQUIRE(f.document.removeObject(firstId).has_value());
    f.regenerate();
    REQUIRE(f.regenerator.body(secondId) != nullptr);

    const auto measured = drawing::measure(f.document, height, f.bodies());
    REQUIRE_FALSE(measured.has_value()); // and NOT 40 mm from the other prism
}

// --- Failure atomicity -------------------------------------------------------------------

TEST_CASE("Dimension_ARejectedEditLeavesTheDimensionExactlyAsItWas",
          "[drawing][dimension][p14]") {
    Fixture f = makeBlock();
    const DimensionId id = add(f, "Width", DimensionType::Linear,
                               onPlane(sideFace(f.part, f.lines[3])),
                               onPlane(sideFace(f.part, f.lines[1])));
    const DimensionDefinition before = drawing::findDimension(f.document, id)->definition();

    const auto refuse = [&](std::string_view what, DimensionDefinition d,
                            std::string_view expected) {
        INFO(what);
        const auto result = drawing::setDimensionDefinition(f.document, id, d);
        REQUIRE_FALSE(result.has_value());
        CHECK_THAT(result.error().message, ContainsSubstring(std::string{expected}));
        // Nothing moved.
        CHECK(drawing::findDimension(f.document, id)->definition() == before);
        CHECK_THAT(lengthOf(f, id), WithinAbs(100.0, kMm));
    };

    DimensionDefinition noView = before;
    noView.view = ViewId{};
    refuse("no view", noView, "must name the view");

    DimensionDefinition tooPrecise = before;
    tooPrecise.format.decimals = 9;
    refuse("nine decimals", tooPrecise, "at most 6 decimals");

    DimensionDefinition unknownUnit = before;
    unknownUnit.format.unit = "furlong";
    refuse("an unknown unit", unknownUnit, "not a unit this build knows");

    DimensionDefinition radiusWithTwo = before;
    radiusWithTwo.type = DimensionType::Radius;
    refuse("a radius with two targets", radiusWithTwo, "takes no second target");

    DimensionDefinition noTarget = before;
    noTarget.to = DimensionTarget{};
    refuse("nothing to measure to", noTarget, "must name both");

    DimensionDefinition twoAtOnce = before;
    twoAtOnce.from.axis = AxisReference{};
    refuse("a target naming two things", twoAtOnce, "names one thing");

    DimensionDefinition missingView = before;
    missingView.view = ViewId::fromValue(9999);
    refuse("a view that is not there", missingView, "not a view of this document");
}

// --- Persistence -------------------------------------------------------------------------

TEST_CASE("Dimension_RoundTripsAndStillMeasuresTheSame",
          "[drawing][dimension][p14][persistence]") {
    // Create -> save -> destroy -> load -> regenerate -> compare, and compare
    // the MEASUREMENT as well as the intent, because a dimension that came
    // back with the right references and the wrong answer would be worse than
    // one that did not come back.
    Fixture f = makeBlock();
    Rod ignored = makeRod();
    (void)ignored;

    const DimensionId width = require(drawing::createDimension(
        f.document, "Width",
        DimensionDefinition{.view = f.view,
                            .type = DimensionType::Horizontal,
                            .from = onPlane(sideFace(f.part, f.lines[3])),
                            .to = onPlane(sideFace(f.part, f.lines[1])),
                            .format = DimensionFormat{.decimals = 3,
                                                      .trailingZeros = false,
                                                      .unit = "cm",
                                                      .showUnit = true},
                            .placement = Point2D{123_mm, 45_mm}}));
    const DimensionId angle = require(drawing::createDimension(
        f.document, "Corner",
        DimensionDefinition{.view = f.view,
                            .type = DimensionType::Angular,
                            .from = onPlane(sideFace(f.part, f.lines[0])),
                            .to = onPlane(sideFace(f.part, f.lines[1])),
                            .format = DimensionFormat{.decimals = 1, .unit = "deg"}}));
    const DimensionId ordinate = require(drawing::createDimension(
        f.document, "Ord",
        DimensionDefinition{.view = f.view,
                            .type = DimensionType::Ordinate,
                            .from = onPlane(PlaneReference{.plane = PrincipalPlane::YZ}),
                            .to = onPlane(sideFace(f.part, f.lines[1])),
                            .ordinate = OrdinateAxis::Y}));

    std::vector<std::pair<DimensionId, std::string>> expected;
    for (const DimensionId id : {width, angle, ordinate}) {
        auto measured = drawing::measure(f.document, id, f.bodies());
        REQUIRE(measured.has_value());
        expected.emplace_back(id, measured->text);
    }

    const TempDir directory;
    const std::filesystem::path path = directory.path() / "dimensions.bcad";
    REQUIRE(io::saveDocument(f.document, path).has_value());
    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());

    features::Regenerator reloaded;
    REQUIRE(reloaded.regenerateAll(*loaded).has_value());
    const features::Regenerator* r = &reloaded;
    const drawing::BodyLookup bodies = [r](ObjectId object) { return r->body(object); };

    for (const auto& [id, text] : expected) {
        INFO("dimension " << id.value());
        const drawing::Dimension* dimension = drawing::findDimension(*loaded, id);
        REQUIRE(dimension != nullptr);
        CHECK(dimension->definition() ==
              drawing::findDimension(f.document, id)->definition());
        auto measured = drawing::measure(*loaded, id, bodies);
        REQUIRE(measured.has_value());
        CHECK(measured->text == text);
    }
}

TEST_CASE("Dimension_TheFileCarriesNoMeasuredValue", "[drawing][dimension][p14][persistence]") {
    // A file that carried the number could disagree with the model it
    // describes. The width is 100 mm and the string "100" must not appear as
    // a stored value anywhere in the dimension's data.
    Fixture f = makeBlock();
    (void)require(drawing::createDimension(
        f.document, "Width",
        DimensionDefinition{.view = f.view,
                            .type = DimensionType::Linear,
                            .from = onPlane(sideFace(f.part, f.lines[3])),
                            .to = onPlane(sideFace(f.part, f.lines[1]))}));

    const TempDir directory;
    const std::filesystem::path path = directory.path() / "d.bcad";
    REQUIRE(io::saveDocument(f.document, path).has_value());
    const std::string text = readFile(path);

    const auto at = text.find(R"("type": "dimension")");
    REQUIRE(at != std::string::npos);
    const std::string section = text.substr(at, 900);
    CHECK_THAT(section, !ContainsSubstring("\"value\""));
    CHECK_THAT(section, !ContainsSubstring("\"measured\""));
    CHECK_THAT(section, !ContainsSubstring("\"text\""));
}

TEST_CASE("Dimension_AFileNamingAnUnknownTypeIsRefused",
          "[drawing][dimension][p14][persistence]") {
    Fixture f = makeBlock();
    (void)require(drawing::createDimension(
        f.document, "Width",
        DimensionDefinition{.view = f.view,
                            .type = DimensionType::Linear,
                            .from = onPlane(sideFace(f.part, f.lines[3])),
                            .to = onPlane(sideFace(f.part, f.lines[1]))}));

    const TempDir directory;
    const std::filesystem::path path = directory.path() / "bad.bcad";
    REQUIRE(io::saveDocument(f.document, path).has_value());

    std::string text = readFile(path);
    const std::string from = R"("type": "linear")";
    const auto at = text.find(from);
    REQUIRE(at != std::string::npos);
    text.replace(at, from.size(), R"("type": "chamfered")");
    writeFile(path, text);

    const auto loaded = io::loadDocument(path);
    REQUIRE_FALSE(loaded.has_value());
    CHECK_THAT(loaded.error().message, ContainsSubstring("unknown dimension type 'chamfered'"));
}

// --- Determinism ---------------------------------------------------------------------------

TEST_CASE("Dimension_MeasuresTheSameValueAndTextEveryTime",
          "[drawing][dimension][p14][determinism]") {
    Fixture f = makeBlock();
    const DimensionId id = add(f, "Width", DimensionType::Aligned,
                               onPlane(sideFace(f.part, f.lines[3])),
                               onPlane(sideFace(f.part, f.lines[1])));
    auto first = drawing::measure(f.document, id, f.bodies());
    REQUIRE(first.has_value());
    for (int i = 0; i < 8; ++i) {
        auto again = drawing::measure(f.document, id, f.bodies());
        REQUIRE(again.has_value());
        CHECK(again->length->si() == first->length->si()); // bit for bit
        CHECK(again->text == first->text);
    }
}

TEST_CASE("Dimension_DependsOnItsViewAndOnWhatItMeasures", "[drawing][dimension][p14]") {
    // The dependency graph has to know, or a model change would not reach the
    // drawing.
    Fixture f = makeBlock();
    const DimensionId id = add(f, "Width", DimensionType::Linear,
                               onPlane(sideFace(f.part, f.lines[3])),
                               onPlane(sideFace(f.part, f.lines[1])));
    const std::vector<ObjectId> dependencies =
        drawing::findDimension(f.document, id)->dependencies();
    CHECK(std::ranges::find(dependencies, ObjectId{f.view}) != dependencies.end());
    CHECK(std::ranges::find(dependencies, f.part) != dependencies.end());
}

TEST_CASE("Dimension_IsListedOnTheViewItMeasuresIn", "[drawing][dimension][p14]") {
    Fixture f = makeBlock();
    CHECK(drawing::dimensions(f.document).empty());
    const DimensionId id = add(f, "Width", DimensionType::Linear,
                               onPlane(sideFace(f.part, f.lines[3])),
                               onPlane(sideFace(f.part, f.lines[1])));
    CHECK(drawing::dimensions(f.document) == std::vector<DimensionId>{id});
    CHECK(drawing::dimensionsOn(f.document, f.view) == std::vector<DimensionId>{id});

    REQUIRE(drawing::removeDimension(f.document, id).has_value());
    CHECK(drawing::dimensions(f.document).empty());
    CHECK(drawing::findDimension(f.document, id) == nullptr);
}

// --- Configurations -----------------------------------------------------------------------

TEST_CASE("Dimension_ReadsWhicheverConfigurationIsActive", "[drawing][dimension][p14]") {
    // A configuration changes what the model IS, so it changes what a
    // dimension measures -- without touching the dimension, which still
    // points at the same two faces by the same names.
    Fixture f;
    auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
    f.lines = addRectangle(*sketch, 0_mm, 0_mm, 100_mm, 60_mm);
    const ObjectId sketchId = require(f.document.addObject(std::move(sketch)));
    const ParameterId depth = require(f.document.createParameter("depth", 40_mm, units::mm));
    auto extrude = features::ExtrudeFeature::create(
        "Block", {.profile = SketchId::fromValue(sketchId.value()),
                  .depth = 40_mm,
                  .depthParameter = depth});
    REQUIRE(extrude.has_value());
    f.part = require(f.document.addObject(std::move(*extrude)));
    f.sheet = addSheet(f.document);
    f.view = require(drawing::createView(
        f.document, "MainView",
        ViewDefinition{.sheet = f.sheet,
                       .source = ObjectReference{f.part},
                       .orientation = StandardView::Front,
                       .placement = Point2D{200_mm, 150_mm}}));
    f.regenerate();

    const DimensionId height = add(f, "Height", DimensionType::Linear,
                                   onPlane(capFace(f.part, FaceRole::StartCap)),
                                   onPlane(capFace(f.part, FaceRole::EndCap)));
    CHECK_THAT(lengthOf(f, height), WithinAbs(40.0, kMm));

    const ConfigurationId tall = require(f.document.createConfiguration("Tall"));
    REQUIRE(f.document.setConfigurationOverride(tall, depth, 85_mm).has_value());
    const DimensionDefinition before = drawing::findDimension(f.document, height)->definition();

    REQUIRE(f.document.setActiveConfiguration(tall).has_value());
    f.regenerate();
    CHECK_THAT(lengthOf(f, height), WithinAbs(85.0, kMm));
    // The dimension did not change -- the model did.
    CHECK(drawing::findDimension(f.document, height)->definition() == before);

    REQUIRE(f.document.setActiveConfiguration(std::nullopt).has_value());
    f.regenerate();
    CHECK_THAT(lengthOf(f, height), WithinAbs(40.0, kMm));
}
