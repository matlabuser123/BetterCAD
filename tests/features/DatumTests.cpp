#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/DatumModels.hpp"

#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/DatumCommands.hpp>
#include <bettercad/features/Datums.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/MirrorFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/Validation.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/sketch/SketchCommands.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::features;
using namespace bettercad::literals;
using bettercad::test::DatumBlockModel;
using bettercad::test::errorCode;
using bettercad::test::require;
using bettercad::test::requireReport;
using bettercad::test::volumeMm3;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// P12-DATUM-001: datum planes, datum axes and coordinate systems. Expected
// frames are computed here with rotation matrices written out and plane
// equations solved by hand, not with the resolution code.

namespace {

constexpr double pi = std::numbers::pi;
// Model-space coordinates of tens of mm after a few rotations.
constexpr double kTolMm = 1e-12;
constexpr double kTolUnit = 1e-14;

using Vec = std::array<double, 3>;
using Mat = std::array<Vec, 3>; // rows

Vec vec(const Direction3D& d) {
    return {d.x(), d.y(), d.z()};
}

Vec mm(const Point3D& p) {
    return {p.x.in(units::mm), p.y.in(units::mm), p.z.in(units::mm)};
}

Mat rotationX(double a) {
    return {{{1, 0, 0}, {0, std::cos(a), -std::sin(a)}, {0, std::sin(a), std::cos(a)}}};
}

Mat rotationY(double a) {
    return {{{std::cos(a), 0, std::sin(a)}, {0, 1, 0}, {-std::sin(a), 0, std::cos(a)}}};
}

Mat rotationZ(double a) {
    return {{{std::cos(a), -std::sin(a), 0}, {std::sin(a), std::cos(a), 0}, {0, 0, 1}}};
}

Mat multiply(const Mat& a, const Mat& b) {
    Mat c{};
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            for (std::size_t k = 0; k < 3; ++k) {
                c[i][j] += a[i][k] * b[k][j];
            }
        }
    }
    return c;
}

Vec apply(const Mat& m, const Vec& v) {
    return {m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2], m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2],
            m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2]};
}

/// Column i of m.
Vec column(const Mat& m, std::size_t i) {
    return {m[0][i], m[1][i], m[2][i]};
}

void checkVec(const Vec& actual, const Vec& expected, double tolerance) {
    CHECK_THAT(actual[0], WithinAbs(expected[0], tolerance));
    CHECK_THAT(actual[1], WithinAbs(expected[1], tolerance));
    CHECK_THAT(actual[2], WithinAbs(expected[2], tolerance));
}

void checkFrame(const Frame3D& frame, const Vec& origin, const Vec& x, const Vec& normal) {
    checkVec(mm(frame.origin()), origin, kTolMm);
    checkVec(vec(frame.xAxis()), x, kTolUnit);
    checkVec(vec(frame.normal()), normal, kTolUnit);
}

Frame3D requireFrame(const Result<Frame3D>& frame) {
    if (!frame) {
        FAIL(frame.error().message);
    }
    return *frame;
}

Axis3D requireAxis(const Result<Axis3D>& axis) {
    if (!axis) {
        FAIL(axis.error().message);
    }
    return *axis;
}

template <typename T>
ObjectId add(Document& doc, Result<std::unique_ptr<T>> object) {
    if (!object) {
        FAIL(object.error().message);
    }
    return doc.addObject(std::move(*object)).value();
}

ObjectId offsetPlane(Document& doc, const std::string& name, PlaneReference base, Length offset) {
    return add(doc, DatumPlane::create(name, {.kind = DatumPlaneKind::Offset, .base = base, .offset = offset}));
}

Error resolutionError(const Result<Frame3D>& result) {
    REQUIRE_FALSE(result.has_value());
    return result.error();
}

void setValue(CommandHistory& history, Document& doc, ParameterId id, DimensionedValue value) {
    REQUIRE(history.execute(doc, std::make_unique<ModifyParameterCommand>(id, ParameterChanges{.value = value}))
                .has_value());
}

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

} // namespace

// --- Definitions -----------------------------------------------------------------------------------

TEST_CASE("Datum_KindAndReferenceNamesAreStable", "[features][datum][p12]") {
    CHECK(toString(PrincipalPlane::XY) == "xy");
    CHECK(toString(PrincipalPlane::YZ) == "yz");
    CHECK(toString(PrincipalPlane::XZ) == "xz");
    CHECK(toString(PrincipalAxis::X) == "x");
    CHECK(toString(PrincipalAxis::Z) == "z");
    CHECK(toString(DatumPlaneKind::Angled) == "angled");
    CHECK(toString(DatumAxisKind::Intersection) == "intersection");
    CHECK(toString(CoordinateSystemKind::Offset) == "offset");
    CHECK(DatumPlane::kTypeName == "datum_plane");
    CHECK(DatumAxis::kTypeName == "datum_axis");
    CHECK(CoordinateSystem::kTypeName == "coordinate_system");
}

TEST_CASE("Datum_DefinitionsAreCheckedForTheirKind", "[features][datum][p12]") {
    const auto message = [](const Result<void>& result) {
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::InvalidArgument);
        return result.error().message;
    };
    const ObjectId other = ObjectId::fromValue(3);
    CHECK(message(validate(DatumPlaneDefinition{.kind = DatumPlaneKind::Fixed, .base = {.object = other}})) ==
          "a fixed datum plane has no base plane");
    CHECK(message(validate(DatumPlaneDefinition{.kind = DatumPlaneKind::Offset, .frame = Frame3D::yz()})) ==
          "an offset datum plane is placed from its base plane and has no frame of its own");
    CHECK(message(validate(DatumPlaneDefinition{.kind = DatumPlaneKind::Offset, .angle = 10_deg})) ==
          "an offset datum plane has no axis or angle");
    CHECK(message(validate(DatumPlaneDefinition{.kind = DatumPlaneKind::Angled, .offset = 1_mm})) ==
          "an angled datum plane has no offset");
    CHECK(message(validate(DatumPlaneDefinition{
              .kind = DatumPlaneKind::Offset, .offset = Length::fromSi(std::nan(""))})) ==
          "the plane's offset and angle must be finite");
    CHECK(message(validate(DatumPlaneDefinition{.kind = DatumPlaneKind::Offset,
                                                .offsetParameter = ParameterId{}})) ==
          "the offset parameter ID must be valid");
    CHECK(message(validate(DatumPlaneDefinition{.kind = DatumPlaneKind::Offset,
                                                .base = {.object = ObjectId{}}})) ==
          "the base plane must be a valid object ID");
    CHECK(message(validate(DatumAxisDefinition{.kind = DatumAxisKind::Fixed, .first = {.object = other}})) ==
          "a fixed datum axis has no planes");
    CHECK(message(validate(DatumAxisDefinition{.kind = DatumAxisKind::Intersection})) ==
          "an intersection datum axis needs two different planes");
    CHECK(message(validate(DatumAxisDefinition{.kind = DatumAxisKind::Intersection,
                                               .axis = {Point3D{1_mm, 0_mm, 0_mm}, Direction3D::unitZ()},
                                               .second = {.object = other}})) ==
          "an intersection datum axis is where its planes meet and has no line of its own");
    CHECK(message(validate(CoordinateSystemDefinition{.kind = CoordinateSystemKind::Fixed,
                                                      .translation = {1_mm, 0_mm, 0_mm}})) ==
          "a fixed coordinate system has no base, translation or rotation");
    CHECK(message(validate(CoordinateSystemDefinition{.kind = CoordinateSystemKind::Offset,
                                                      .frame = Frame3D::xz()})) ==
          "an offset coordinate system is placed from its base and has no frame of its own");
    CHECK(errorCode(DatumPlane::create("Bad", {.kind = DatumPlaneKind::Fixed, .base = {.object = other}})) ==
          ErrorCode::InvalidArgument);

    // Dependencies in field order.
    const auto plane = DatumPlane::create(
        "P", {.kind = DatumPlaneKind::Angled, .base = {.object = ObjectId::fromValue(4)},
              .axis = {.object = ObjectId::fromValue(5)}, .angleParameter = ParameterId::fromValue(2)});
    REQUIRE(plane.has_value());
    CHECK((*plane)->dependencies() ==
          std::vector<ObjectId>{ObjectId::fromValue(4), ObjectId::fromValue(5), ObjectId::fromValue(2)});
}

// --- Resolution --------------------------------------------------------------------------------------

TEST_CASE("Datum_PlanesMatchIndependentGeometry", "[features][datum][p12]") {
    Document doc{"Planes"};
    const ParameterId lift = doc.createParameter("lift", 12.5_mm, units::mm).value();
    const ParameterId turn = doc.createParameter("turn", 35_deg, units::deg).value();
    const ObjectId raised = add(doc, DatumPlane::create("Raised", {.kind = DatumPlaneKind::Offset,
                                                                    .base = {},
                                                                    .offsetParameter = lift}));
    const ObjectId higher = offsetPlane(doc, "Higher", {.object = raised}, 7.5_mm);
    const ObjectId side = offsetPlane(doc, "Side", {.object = {}, .plane = PrincipalPlane::XZ}, 4_mm);
    const ObjectId hinged = add(doc, DatumPlane::create("Hinged", {.kind = DatumPlaneKind::Angled,
                                                                    .base = {.object = higher},
                                                                    .axis = {.object = {}, .axis = PrincipalAxis::X},
                                                                    .angleParameter = turn}));

    SECTION("principal planes of the model") {
        checkFrame(requireFrame(resolvePlane(doc, {})), {0, 0, 0}, {1, 0, 0}, {0, 0, 1});
        checkFrame(requireFrame(resolvePlane(doc, {.object = {}, .plane = PrincipalPlane::YZ})), {0, 0, 0},
                   {0, 1, 0}, {1, 0, 0});
        checkFrame(requireFrame(resolvePlane(doc, {.object = {}, .plane = PrincipalPlane::XZ})), {0, 0, 0},
                   {1, 0, 0}, {0, -1, 0});
    }
    SECTION("offsets along the base normal, chained") {
        checkFrame(requireFrame(resolvePlane(doc, {.object = raised})), {0, 0, 12.5}, {1, 0, 0}, {0, 0, 1});
        checkFrame(requireFrame(resolvePlane(doc, {.object = higher})), {0, 0, 20}, {1, 0, 0}, {0, 0, 1});
        // XZ's normal is -Y: a positive offset moves towards -Y.
        checkFrame(requireFrame(resolvePlane(doc, {.object = side})), {0, -4, 0}, {1, 0, 0}, {0, -1, 0});
    }
    SECTION("an angled plane turns about its axis, right-handed") {
        // The model's X axis lies in the plane z = 20 only if the plane passes
        // through it: it does not, so the hinge must be an axis in that plane.
        const auto off = resolvePlane(doc, {.object = hinged});
        REQUIRE_FALSE(off.has_value());
        CHECK(off.error().code == ErrorCode::FailedPrecondition);
        CHECK(off.error().message ==
              "Hinged (object:6): its axis does not lie in its base plane (it is 0 deg and 20 mm off it)");

        // Hinged about the model's X axis from the model's XY plane instead.
        REQUIRE(doc.modifyObject<DatumPlane>(hinged, [&](DatumPlane& p) {
                       auto d = p.definition();
                       d.base = {};
                       return p.setDefinition(d);
                   }).value());
        const Frame3D frame = requireFrame(resolvePlane(doc, {.object = hinged}));
        const Mat r = rotationX(35.0 * pi / 180.0);
        checkFrame(frame, {0, 0, 0}, apply(r, {1, 0, 0}), apply(r, {0, 0, 1}));
        checkVec(vec(frame.yAxis()), apply(r, {0, 1, 0}), kTolUnit);
    }
}

TEST_CASE("Datum_AxisIsWhereTwoPlanesMeet", "[features][datum][p12]") {
    Document doc{"Axes"};
    // Plane 1: z = 20 turned by 40 deg about the line y = 0, z = 20 (along X);
    // plane 2: x = 15.
    const ObjectId top = offsetPlane(doc, "Top", {}, 20_mm);
    const ObjectId hingeLine = add(doc, DatumAxis::create("HingeLine", {.kind = DatumAxisKind::Fixed,
                                                                         .axis = {Point3D{0_mm, 0_mm, 20_mm},
                                                                                  Direction3D::unitX()}}));
    const ObjectId slope = add(doc, DatumPlane::create("Slope", {.kind = DatumPlaneKind::Angled,
                                                                  .base = {.object = top},
                                                                  .axis = {.object = hingeLine},
                                                                  .angle = 40_deg}));
    const ObjectId wall = offsetPlane(doc, "Wall", {.object = {}, .plane = PrincipalPlane::YZ}, 15_mm);
    const ObjectId ridge = add(doc, DatumAxis::create("Ridge", {.kind = DatumAxisKind::Intersection,
                                                                 .first = {.object = slope},
                                                                 .second = {.object = wall}}));
    const Axis3D axis = requireAxis(resolveAxis(doc, {.object = ridge}));

    // Independent: n1 = Rx(40) (0, 0, 1), n2 = (1, 0, 0); both planes contain
    // their points (0, 0, 20) and (15, 0, 0).
    const Vec n1 = apply(rotationX(40.0 * pi / 180.0), {0, 0, 1});
    const Vec n2{1, 0, 0};
    const Vec d{n1[1] * n2[2] - n1[2] * n2[1], n1[2] * n2[0] - n1[0] * n2[2], n1[0] * n2[1] - n1[1] * n2[0]};
    const double length = std::hypot(d[0], d[1], d[2]);
    checkVec(vec(axis.direction), {d[0] / length, d[1] / length, d[2] / length}, kTolUnit);
    const Vec p = mm(axis.origin);
    // On both planes...
    CHECK_THAT(n1[0] * p[0] + n1[1] * p[1] + n1[2] * (p[2] - 20.0), WithinAbs(0.0, kTolMm));
    CHECK_THAT(p[0] - 15.0, WithinAbs(0.0, kTolMm));
    // ... and nearest the origin: perpendicular to the line.
    CHECK_THAT((p[0] * d[0] + p[1] * d[1] + p[2] * d[2]) / length, WithinAbs(0.0, kTolMm));

    // Principal axes of the model and a fixed axis resolve as given.
    const Axis3D y = requireAxis(resolveAxis(doc, {.object = {}, .axis = PrincipalAxis::Y}));
    checkVec(vec(y.direction), {0, 1, 0}, 0.0);
    checkVec(mm(requireAxis(resolveAxis(doc, {.object = hingeLine})).origin), {0, 0, 20}, 0.0);
}

TEST_CASE("Datum_CoordinateSystemsTurnAboutTheirBaseAxesThenMove", "[features][datum][p12]") {
    Document doc{"Systems"};
    const ParameterId yaw = doc.createParameter("yaw", -60_deg, units::deg).value();
    const ParameterId shift = doc.createParameter("shift", 7_mm, units::mm).value();
    const ObjectId base = add(doc, CoordinateSystem::create(
                                       "Base", {.kind = CoordinateSystemKind::Offset,
                                                .translation = {100_mm, 20_mm, -5_mm},
                                                .rotation = {0_deg, 0_deg, 90_deg}}));
    const ObjectId child = add(doc, CoordinateSystem::create(
                                        "Child", {.kind = CoordinateSystemKind::Offset,
                                                  .base = base,
                                                  .translation = {10_mm, 0_mm, 3_mm},
                                                  .translationParameters = {std::nullopt, shift, std::nullopt},
                                                  .rotation = {30_deg, 45_deg, 0_deg},
                                                  .rotationParameters = {std::nullopt, std::nullopt, yaw}}));
    // Base: B = Rz(90), at (100, 20, -5). Child: axes B Rz(-60) Ry(45) Rx(30)
    // (about the base's fixed axes, X first), origin b0 + B (10, 7, 3).
    const Mat b = rotationZ(pi / 2.0);
    const Mat axes = multiply(b, multiply(rotationZ(-60.0 * pi / 180.0),
                                          multiply(rotationY(pi / 4.0), rotationX(pi / 6.0))));
    const Vec moved = apply(b, {10, 7, 3});
    const Vec origin{100 + moved[0], 20 + moved[1], -5 + moved[2]};

    const Frame3D frame = requireFrame(resolveCoordinateSystem(doc, child));
    checkFrame(frame, origin, column(axes, 0), column(axes, 2));
    checkVec(vec(frame.yAxis()), column(axes, 1), kTolUnit);

    SECTION("principal planes and axes of a coordinate system") {
        checkFrame(requireFrame(resolvePlane(doc, {.object = child, .plane = PrincipalPlane::XY})), origin,
                   column(axes, 0), column(axes, 2));
        checkFrame(requireFrame(resolvePlane(doc, {.object = child, .plane = PrincipalPlane::YZ})), origin,
                   column(axes, 1), column(axes, 0));
        const Vec y = column(axes, 1);
        checkFrame(requireFrame(resolvePlane(doc, {.object = child, .plane = PrincipalPlane::XZ})), origin,
                   column(axes, 0), {-y[0], -y[1], -y[2]});
        const Axis3D x = requireAxis(resolveAxis(doc, {.object = child, .axis = PrincipalAxis::X}));
        checkVec(mm(x.origin), origin, kTolMm);
        checkVec(vec(x.direction), column(axes, 0), kTolUnit);
    }
    SECTION("the model's coordinate system") {
        checkFrame(requireFrame(resolveCoordinateSystem(doc, std::nullopt)), {0, 0, 0}, {1, 0, 0}, {0, 0, 1});
        const auto fixed = add(doc, CoordinateSystem::create("Fixed", {.kind = CoordinateSystemKind::Fixed,
                                                                       .frame = Frame3D::yz()}));
        CHECK(requireFrame(resolveCoordinateSystem(doc, fixed)) == Frame3D::yz());
    }
}

TEST_CASE("Datum_ResolutionRefusesMissingWrongAndDegenerateReferences", "[features][datum][p12]") {
    Document doc{"Refusals"};
    const ParameterId angle = doc.createParameter("angle", 10_deg, units::deg).value();
    auto sketch = std::make_unique<sketch::Sketch>("Sketch1");
    const ObjectId sketchId = doc.addObject(std::move(sketch)).value();
    const ObjectId plane = offsetPlane(doc, "Plane1", {}, 5_mm);
    const ObjectId axis = add(doc, DatumAxis::create("Axis1", {.kind = DatumAxisKind::Fixed}));

    Error e = resolutionError(resolvePlane(doc, {.object = ObjectId::fromValue(99)}));
    CHECK(e.code == ErrorCode::NotFound);
    CHECK(e.message == "object:99 does not exist");
    e = resolutionError(resolvePlane(doc, {.object = sketchId}));
    CHECK(e.code == ErrorCode::InvalidArgument);
    CHECK(e.message == "Sketch1 (object:2) is a sketch, not a datum plane or a coordinate system");
    e = resolutionError(resolvePlane(doc, {.object = plane, .plane = PrincipalPlane::YZ}));
    CHECK(e.message == "Plane1 (object:3) is a datum plane, which has only one plane: refer to it as xy, not yz");
    const auto asX = resolveAxis(doc, {.object = axis, .axis = PrincipalAxis::X});
    REQUIRE_FALSE(asX.has_value());
    CHECK(asX.error().message == "Axis1 (object:4) is a datum axis, which is one axis: refer to it as z, not x");
    const auto planeAsAxis = resolveAxis(doc, {.object = plane});
    REQUIRE_FALSE(planeAsAxis.has_value());
    CHECK(planeAsAxis.error().message ==
          "Plane1 (object:3) is a datum plane, not a datum axis or a coordinate system");
    e = resolutionError(resolveCoordinateSystem(doc, axis));
    CHECK(e.message == "Axis1 (object:4) is a datum axis, not a coordinate system");

    // Parallel planes do not meet.
    const ObjectId parallel = add(doc, DatumAxis::create("Nowhere", {.kind = DatumAxisKind::Intersection,
                                                                      .first = {.object = plane},
                                                                      .second = {}}));
    const auto nowhere = resolveAxis(doc, {.object = parallel});
    REQUIRE_FALSE(nowhere.has_value());
    CHECK(nowhere.error().code == ErrorCode::FailedPrecondition);
    CHECK(nowhere.error().message == "Nowhere (object:5): its planes are parallel and do not meet");

    // A length driven by an angle.
    const ObjectId wrong = add(doc, DatumPlane::create("Wrong", {.kind = DatumPlaneKind::Offset,
                                                                  .base = {},
                                                                  .offsetParameter = angle}));
    e = resolutionError(resolvePlane(doc, {.object = wrong}));
    CHECK(e.code == ErrorCode::DimensionMismatch);
    CHECK_THAT(e.message, ContainsSubstring("Wrong (object:6): "));

    // A cycle: each plane is placed from the other.
    const ObjectId a = offsetPlane(doc, "A", {}, 1_mm);
    const ObjectId b = offsetPlane(doc, "B", {.object = a}, 1_mm);
    REQUIRE(doc.modifyObject<DatumPlane>(a, [&](DatumPlane& p) {
                   return p.setDefinition({.kind = DatumPlaneKind::Offset, .base = {.object = b}, .offset = 1_mm});
               }).value());
    e = resolutionError(resolvePlane(doc, {.object = a}));
    CHECK(e.code == ErrorCode::FailedPrecondition);
    CHECK_THAT(e.message, ContainsSubstring("its references nest more than 64 deep"));
}

// --- Documents -----------------------------------------------------------------------------------------

TEST_CASE("DatumModel_RegeneratesWithAnalyticGeometry", "[features][datum][regeneration][p12][acceptance]") {
    DatumBlockModel m;
    Regenerator regenerator;
    const RegenerationReport first = requireReport(regenerator, m.doc);
    INFO((first.errors.empty() ? std::string{} : first.errors.begin()->second.message));
    REQUIRE(first.succeeded());

    const auto checkModel = [&](double h, double half, double tilt, double r) {
        CAPTURE(h, half, tilt, r);
        const geometry::Body* pattern = regenerator.body(m.pattern);
        REQUIRE(pattern != nullptr);
        const auto props = pattern->massProperties();
        REQUIRE(props.has_value());
        CHECK_THAT(props->volume.in(units::mm3), WithinRel(DatumBlockModel::patternVolume(h, r), 1e-12));
        const auto centre = DatumBlockModel::patternCentre(h, r, half);
        checkVec(mm(props->centerOfMass), centre, 1e-9);

        const geometry::Body* peg = regenerator.body(m.peg);
        REQUIRE(peg != nullptr);
        CHECK_THAT(volumeMm3(regenerator, m.peg), WithinRel(pi * 25.0 * 10.0, 1e-12));
        const auto pegBox = peg->boundingBox();
        REQUIRE(pegBox.has_value());
        checkVec(mm(pegBox->min), {145, 5, 0}, 1e-7);
        checkVec(mm(pegBox->max), {155, 15, 10}, 1e-7);

        const geometry::Body* fin = regenerator.body(m.fin);
        REQUIRE(fin != nullptr);
        CHECK_THAT(volumeMm3(regenerator, m.fin), WithinRel(20.0 * 20.0 * 5.0, 1e-12));
        const auto finBox = fin->boundingBox();
        REQUIRE(finBox.has_value());
        const auto expected = DatumBlockModel::finBounds(tilt);
        checkVec(mm(finBox->min), {expected[0], expected[1], expected[2]}, 1e-7);
        checkVec(mm(finBox->max), {expected[3], expected[4], expected[5]}, 1e-7);

        // The attached sketches' placements.
        const auto* boss = m.doc.findObjectAs<sketch::Sketch>(m.bossSketch);
        checkFrame(boss->placement(), {0, 0, h}, {1, 0, 0}, {0, 0, 1});
        const auto* pegSketch = m.doc.findObjectAs<sketch::Sketch>(m.pegSketch);
        checkFrame(pegSketch->placement(), {150, 0, 0}, {0, 1, 0}, {0, 0, 1});
    };
    checkModel(20.0, 50.0, 30.0, 8.0);

    CommandHistory history;
    setValue(history, m.doc, m.height, DimensionedValue::of(25_mm));
    setValue(history, m.doc, m.halfLength, DimensionedValue::of(45_mm));
    setValue(history, m.doc, m.tilt, DimensionedValue::of(50_deg));
    const RegenerationReport second = requireReport(regenerator, m.doc);
    REQUIRE(second.succeeded());
    CHECK(std::ranges::find(second.regenerated, m.topPlane) != second.regenerated.end());
    CHECK(std::ranges::find(second.regenerated, m.bossSketch) != second.regenerated.end());
    CHECK(std::ranges::find(second.regenerated, m.spindle) != second.regenerated.end());
    CHECK(std::ranges::find(second.regenerated, m.pegSketch) == second.regenerated.end());
    checkModel(25.0, 45.0, 50.0, 8.0);

    const Frame3D before = m.doc.findObjectAs<sketch::Sketch>(m.bossSketch)->placement();
    for (int i = 0; i < 3; ++i) {
        REQUIRE(history.undo(m.doc).has_value());
    }
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    checkModel(20.0, 50.0, 30.0, 8.0);
    CHECK(m.doc.findObjectAs<sketch::Sketch>(m.bossSketch)->placement() != before);
    CHECK(m.doc.findObjectAs<sketch::Sketch>(m.bossSketch)->placement() ==
          requireFrame(resolvePlane(m.doc, {.object = m.topPlane})));
}

TEST_CASE("DatumModel_FailuresBlockDependentsAndRecover", "[features][datum][regeneration][p12]") {
    DatumBlockModel m;
    Regenerator regenerator;
    REQUIRE(requireReport(regenerator, m.doc).succeeded());
    CommandHistory history;

    SECTION("parallel planes") {
        REQUIRE(history
                    .execute(m.doc, std::make_unique<ModifyDatumPlaneCommand>(
                                        m.rearPlane, DatumPlaneDefinition{.kind = DatumPlaneKind::Offset,
                                                                          .base = {.object = {},
                                                                                   .plane = PrincipalPlane::YZ},
                                                                          .offset = 70_mm}))
                    .has_value());
        const RegenerationReport report = requireReport(regenerator, m.doc);
        CHECK(report.failed == std::vector<ObjectId>{m.spindle});
        CHECK(report.errors.at(m.spindle).message == "Spindle (object:12): its planes are parallel and do not meet");
        CHECK(report.blocked == std::vector<ObjectId>{m.pattern});
        CHECK(regenerator.body(m.pattern) == nullptr);
        CHECK(regenerator.body(m.peg) != nullptr);
        REQUIRE(history.undo(m.doc).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
        CHECK(regenerator.body(m.pattern) != nullptr);
    }
    SECTION("a sketch attached to an axis") {
        REQUIRE(history
                    .execute(m.doc, std::make_unique<sketch::ModifySketchCommand>(
                                        DatumBlockModel::sketchOf(m.bossSketch), "Attach to the spindle",
                                        [&](sketch::Sketch& s) -> Result<void> {
                                            auto set = s.setAttachment(PlaneReference{m.spindle});
                                            if (!set) {
                                                return std::unexpected(set.error());
                                            }
                                            return {};
                                        }))
                    .has_value());
        const Frame3D placement = m.doc.findObjectAs<sketch::Sketch>(m.bossSketch)->placement();
        const RegenerationReport report = requireReport(regenerator, m.doc);
        CHECK(report.failed == std::vector<ObjectId>{m.bossSketch});
        CHECK(report.errors.at(m.bossSketch).code == ErrorCode::InvalidArgument);
        CHECK(report.errors.at(m.bossSketch).message ==
              "BossSketch (object:8): Spindle (object:12) is a datum axis, not a datum plane or a coordinate system");
        CHECK(report.blocked == std::vector<ObjectId>{m.boss, m.pattern});
        CHECK(m.doc.findObjectAs<sketch::Sketch>(m.bossSketch)->placement() == placement);
        REQUIRE(history.undo(m.doc).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
    }
    SECTION("a cycle between two planes") {
        REQUIRE(history
                    .execute(m.doc, std::make_unique<ModifyDatumPlaneCommand>(
                                        m.middle, DatumPlaneDefinition{.kind = DatumPlaneKind::Offset,
                                                                       .base = {.object = m.topPlane}}))
                    .has_value());
        REQUIRE(history
                    .execute(m.doc, std::make_unique<ModifyDatumPlaneCommand>(
                                        m.topPlane, DatumPlaneDefinition{.kind = DatumPlaneKind::Offset,
                                                                         .base = {.object = m.middle}}))
                    .has_value());
        const RegenerationReport report = requireReport(regenerator, m.doc);
        REQUIRE(report.cycles.size() == 1);
        CHECK(report.errors.at(m.topPlane).message == "dependency cycle: TopPlane, Middle");
        CHECK(std::ranges::find(report.blocked, m.pattern) != report.blocked.end());
        REQUIRE(history.undo(m.doc).has_value());
        REQUIRE(history.undo(m.doc).has_value());
        REQUIRE(requireReport(regenerator, m.doc).succeeded());
    }
}

TEST_CASE("DatumModel_DependenciesAreGraphEdges", "[features][datum][p12]") {
    DatumBlockModel m;
    const DocumentGraph graph = buildDependencyGraph(m.doc);
    const auto dependsOn = [&](ObjectId item, ObjectId on) {
        const auto deps = graph.graph.dependenciesOf(item);
        return std::ranges::find(deps, on) != deps.end();
    };
    CHECK(dependsOn(m.topPlane, ObjectId{m.height}));
    CHECK(dependsOn(m.bossSketch, m.topPlane));
    CHECK(dependsOn(m.bossSketch, ObjectId{m.bossRadius}));
    CHECK(dependsOn(m.middle, ObjectId{m.halfLength}));
    CHECK(dependsOn(m.spindle, m.middle));
    CHECK(dependsOn(m.spindle, m.rearPlane));
    CHECK(dependsOn(m.pattern, m.spindle));
    CHECK(dependsOn(m.pattern, m.boss));
    CHECK(dependsOn(m.pegSketch, m.station));
    CHECK(dependsOn(m.tiltPlane, ObjectId{m.tilt}));
    CHECK(dependsOn(m.finSketch, m.tiltPlane));
    CHECK(graph.missing.empty());
    const auto downstream = graph.graph.downstreamOf({ObjectId{m.height}});
    for (const ObjectId id : {m.block, m.topPlane, m.bossSketch, m.boss, m.pattern}) {
        CHECK(downstream.contains(id));
    }
    CHECK_FALSE(downstream.contains(m.peg));
}

TEST_CASE("DatumMirror_ReflectsAcrossADatumPlane", "[features][datum][mirror][p12]") {
    Document doc{"Mirror"};
    auto profile = std::make_unique<sketch::Sketch>("Profile");
    require(profile->addCircle(Point2D{20_mm, 30_mm}, 5_mm));
    const ObjectId profileId = doc.addObject(std::move(profile)).value();
    const ObjectId post = add(doc, ExtrudeFeature::create(
                                       "Post", {.profile = SketchId::fromValue(profileId.value()), .depth = 10_mm}));
    const ObjectId middle = offsetPlane(doc, "Middle", {.object = {}, .plane = PrincipalPlane::YZ}, 50_mm);
    MirrorDefinition definition{.source = FeatureId::fromValue(post.value()),
                                .plane = {.offset = 10_mm, .reference = PlaneReference{middle}},
                                .scope = MirrorScope::Body};
    const ObjectId mirror = add(doc, MirrorFeature::create("Mirror", definition));
    CHECK((*doc.findObjectAs<MirrorFeature>(mirror)).dependencies() ==
          std::vector<ObjectId>{post, middle});

    Regenerator regenerator;
    const RegenerationReport report = requireReport(regenerator, doc);
    INFO((report.errors.empty() ? std::string{} : report.errors.begin()->second.message));
    REQUIRE(report.succeeded());
    const auto props = regenerator.body(mirror)->massProperties();
    REQUIRE(props.has_value());
    // Two posts, the image across x = 60 at x = 100: centre at x = 60.
    CHECK_THAT(props->volume.in(units::mm3), WithinRel(2.0 * pi * 25.0 * 10.0, 1e-12));
    CHECK_THAT(props->centerOfMass.x.in(units::mm), WithinAbs(60.0, 1e-9));

    // A plane given by a reference has no origin or normal of its own.
    definition.plane.origin = Point3D{1_mm, 0_mm, 0_mm};
    const auto both = MirrorFeature::create("Both", definition);
    REQUIRE_FALSE(both.has_value());
    CHECK(both.error().message == "a mirror plane given by a reference has no origin or normal of its own");
}

TEST_CASE("DatumCommands_CreateAndModifyAreUndoable", "[features][datum][commands][p12]") {
    Document doc{"Commands"};
    CommandHistory history;
    auto create = std::make_unique<CreateDatumPlaneCommand>(
        "Plane1", DatumPlaneDefinition{.kind = DatumPlaneKind::Offset, .base = {}, .offset = 5_mm});
    const auto* command = create.get();
    REQUIRE(history.execute(doc, std::move(create)).has_value());
    const ObjectId id = command->objectId();
    CHECK(history.undoDescription() == "Create datum_plane 'Plane1'");
    REQUIRE(doc.findObjectAs<DatumPlane>(id) != nullptr);

    REQUIRE(history
                .execute(doc, std::make_unique<ModifyDatumPlaneCommand>(
                                  id, DatumPlaneDefinition{.kind = DatumPlaneKind::Offset, .offset = 9_mm}))
                .has_value());
    CHECK(doc.findObjectAs<DatumPlane>(id)->definition().offset == 9_mm);
    REQUIRE(history.undo(doc).has_value());
    CHECK(doc.findObjectAs<DatumPlane>(id)->definition().offset == 5_mm);
    REQUIRE(history.undo(doc).has_value());
    CHECK(doc.findObject(id) == nullptr);
    REQUIRE(history.redo(doc).has_value());
    CHECK(doc.findObjectAs<DatumPlane>(id) != nullptr); // the same ID again
    REQUIRE(history.redo(doc).has_value());
    CHECK(doc.findObjectAs<DatumPlane>(id)->definition().offset == 9_mm);

    auto axis = std::make_unique<CreateDatumAxisCommand>("Axis1", DatumAxisDefinition{});
    REQUIRE(history.execute(doc, std::move(axis)).has_value());
    auto system = std::make_unique<CreateCoordinateSystemCommand>("CS1", CoordinateSystemDefinition{});
    REQUIRE(history.execute(doc, std::move(system)).has_value());
    CHECK(doc.objectCount() == 3);

    // Refused edits change nothing.
    const auto refused = history.execute(
        doc, std::make_unique<ModifyDatumPlaneCommand>(
                 id, DatumPlaneDefinition{.kind = DatumPlaneKind::Fixed, .offset = 1_mm}));
    CHECK(errorCode(refused) == ErrorCode::InvalidArgument);
    CHECK(errorCode(history.execute(doc, std::make_unique<ModifyDatumAxisCommand>(id, DatumAxisDefinition{}))) ==
          ErrorCode::NotFound);
}

TEST_CASE("DatumValidation_ReportsWrongKindsAndDimensions", "[features][datum][validation][p12]") {
    DatumBlockModel m;
    const ValidationReport clean = validateDocument(m.doc);
    INFO((clean.issues.empty() ? std::string{} : clean.issues.front().message));
    CHECK(clean.count(ValidationCheck::DocumentConsistency, Severity::Error) == 0);
    CHECK(clean.valid());

    Document broken = m.doc.clone();
    REQUIRE(broken.modifyObject<DatumPlane>(m.topPlane, [&](DatumPlane& p) {
                     auto d = p.definition();
                     d.offsetParameter = m.tilt;
                     return p.setDefinition(d);
                 }).value());
    REQUIRE(broken.modifyObject<DatumAxis>(m.spindle, [&](DatumAxis& a) {
                     auto d = a.definition();
                     d.first = {.object = m.blockSketch};
                     return a.setDefinition(d);
                 }).value());
    REQUIRE(broken.modifyObject<sketch::Sketch>(m.finSketch, [&](sketch::Sketch& s) {
                     return s.setAttachment(PlaneReference{m.block});
                 }).value());
    const ValidationReport report = validateDocument(broken);
    CHECK_FALSE(report.valid());
    std::vector<std::string> messages;
    for (const ValidationIssue& issue : report.issues) {
        if (issue.check == ValidationCheck::DocumentConsistency) {
            messages.push_back(issue.message);
        }
    }
    const auto has = [&](std::string_view text) {
        return std::ranges::any_of(messages,
                                   [&](const std::string& line) { return line.find(text) != std::string::npos; });
    };
    CHECK(has("TopPlane (object:7): the offset is driven by tilt (object:3), which is an angle, not a length"));
    CHECK(has("Spindle (object:12): the first plane is BlockSketch (object:5), which is a sketch, not a datum plane "
              "or a coordinate system"));
    CHECK(has("FinSketch (object:18): the attachment is Block (object:6), which is an extrude, not a datum plane "
              "or a coordinate system"));
}

TEST_CASE("DatumModel_RegeneratesDeterministically", "[features][datum][regeneration][p12]") {
    DatumBlockModel a;
    DatumBlockModel b;
    Regenerator ra;
    Regenerator rb;
    REQUIRE(requireReport(ra, a.doc).succeeded());
    REQUIRE(requireReport(rb, b.doc).succeeded());
    for (const ObjectId id : {a.pattern, a.peg, a.fin}) {
        CHECK(bits(volumeMm3(ra, id)) == bits(volumeMm3(rb, id)));
    }
    CHECK(equivalent(a.doc, b.doc));
    // A second pass changes nothing.
    const RegenerationReport again = requireReport(ra, a.doc);
    CHECK(again.regenerated.empty());
}
