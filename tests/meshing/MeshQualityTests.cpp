// P16-QUALITY-001: mesh quality metrics and classification.
//
// EVERY EXPECTED NUMBER HERE IS A CLOSED FORM, derived by hand and written as
// algebra rather than as a recorded output. The two reference tetrahedra are
// chosen so that they disagree on every shape metric, which is what makes them
// two references and not one:
//
//                          regular (edge a)        corner (legs a)
//   V                      sqrt(2)/12 a^3          a^3/6
//   edges                  a, a, a, a, a, a        a x3, a sqrt(2) x3
//   aspect l_max/l_min     1                       sqrt(2)
//   total area             sqrt(3) a^2             (3 + sqrt(3))/2 a^2
//   inradius 3V/A          sqrt(6)/12 a            (3 - sqrt(3))/6 a
//   circumradius           sqrt(6)/4 a             sqrt(3)/2 a
//   radius ratio 3r/R      1                       sqrt(3) - 1
//   dihedrals              all acos(1/3)           acos(1/sqrt(3)) x3, pi/2 x3
//
// THE CLASSIC DIHEDRAL ERROR IS PINNED by the first column: the internal
// dihedral of a regular tetrahedron is acos(1/3) = 70.5288 deg, and the angle
// between its OUTWARD NORMALS is the supplement, acos(-1/3) = 109.4712 deg. A
// test that accepted either would accept an implementation that reports every
// well-shaped element as a bad one.
//
// WHAT IS DELIBERATELY NOT RE-TESTED: P16-DATA-001 qualified the structural
// report -- missing and repeated handles, the inverted and coplanar elements,
// region sharing, the empty mesh, ordering and determinism -- and P16-VOL-001
// qualified duplicate detection and the production pipeline's geometry. What is
// new here is the metrics, their invariances, the structure/quality boundary
// seen from the quality side, and the threshold machinery.
#include "features/FeatureTestSupport.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/meshing/MeshQuality.hpp>
#include <bettercad/meshing/MeshValidation.hpp>
#include <bettercad/meshing/SurfaceMesh.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using meshing::ElementId;
using meshing::ElementType;
using meshing::Mesh;
using meshing::MeshBuilder;
using meshing::MeshIssueKind;
using meshing::MeshQualityReport;
using meshing::Node;
using meshing::NodeId;
using meshing::QualityClass;
using meshing::QualityDirection;
using meshing::QualityFinding;
using meshing::QualityMetric;
using meshing::QualityThreshold;
using meshing::QualityThresholds;
using meshing::RegionId;
using meshing::Tetrahedron;
using meshing::TetQuality;
using meshing::Triangle;
using meshing::TriangleQuality;
using meshing::VolumeMesh;
using meshing::VolumeMeshControls;

namespace {

/// Well-conditioned double-precision algebra on exactly representable inputs.
/// The reference coordinates themselves carry sqrt(3) and sqrt(6), so the
/// metrics inherit a few units in the last place -- a regular tetrahedron built
/// this way measures its aspect ratio as 1 + 2e-16, not exactly 1 -- which is
/// why these are relative comparisons at 1e-12 rather than equality.
constexpr double kAlgebraic = 1e-12;

/// Angles, in radians, absolutely. An angle near pi/2 has magnitude ~1.57, so
/// an absolute bound of 1e-12 rad is the same strength as the relative one and
/// does not quietly loosen for small angles.
constexpr double kAngle = 1e-12;

constexpr RegionId kRegion = RegionId::fromValue(1);
constexpr RegionId kOtherRegion = RegionId::fromValue(2);

[[nodiscard]] NodeId n(NodeId::ValueType value) {
    return NodeId::fromValue(value);
}

/// A point from SI metres. The reference tetrahedra are defined on a side
/// length in metres so the algebra in each test reads as the closed form it is.
[[nodiscard]] Point3D si(double x, double y, double z) {
    return Point3D{Length::fromSi(x), Length::fromSi(y), Length::fromSi(z)};
}

using Corners = std::array<Point3D, 4>;

/// A regular tetrahedron of edge @p a, wound so that its signed volume is
/// positive.
[[nodiscard]] Corners regularTet(double a) {
    const double s3 = std::sqrt(3.0);
    const double s6 = std::sqrt(6.0);
    return Corners{si(0.0, 0.0, 0.0), si(a, 0.0, 0.0), si(a * 0.5, a * s3 / 2.0, 0.0),
                   si(a * 0.5, a * s3 / 6.0, a * s6 / 3.0)};
}

/// The corner of a cube: three mutually perpendicular legs of length @p a.
[[nodiscard]] Corners cornerTet(double a) {
    return Corners{si(0.0, 0.0, 0.0), si(a, 0.0, 0.0), si(0.0, a, 0.0), si(0.0, 0.0, a)};
}

/// An equilateral base of side @p a with its apex @p h above the base centroid.
/// Small @p h gives a sliver; large @p h gives a spike. One family, so a
/// sequence varies exactly one thing.
[[nodiscard]] Corners wedgeTet(double a, double h) {
    const double s3 = std::sqrt(3.0);
    return Corners{si(0.0, 0.0, 0.0), si(a, 0.0, 0.0), si(a * 0.5, a * s3 / 2.0, 0.0),
                   si(a * 0.5, a * s3 / 6.0, h)};
}

[[nodiscard]] Corners translated(const Corners& corners, double dx, double dy, double dz) {
    Corners moved{};
    for (std::size_t i = 0; i < corners.size(); ++i) {
        moved[i] = si(corners[i].x.si() + dx, corners[i].y.si() + dy, corners[i].z.si() + dz);
    }
    return moved;
}

/// Rotation by @p angle about the normalised axis (1, 2, 3), which shares no
/// plane with the reference coordinates: an axis-aligned rotation would leave
/// coordinates merely permuted and would not exercise the arithmetic.
[[nodiscard]] Corners rotated(const Corners& corners, double angle) {
    const double norm = std::sqrt(1.0 + 4.0 + 9.0);
    const double ux = 1.0 / norm;
    const double uy = 2.0 / norm;
    const double uz = 3.0 / norm;
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    const double t = 1.0 - c;
    // Rodrigues, written out: the test's reference frame must not come from
    // production code.
    const std::array<std::array<double, 3>, 3> r{{
        {c + ux * ux * t, ux * uy * t - uz * s, ux * uz * t + uy * s},
        {uy * ux * t + uz * s, c + uy * uy * t, uy * uz * t - ux * s},
        {uz * ux * t - uy * s, uz * uy * t + ux * s, c + uz * uz * t},
    }};
    Corners turned{};
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const double x = corners[i].x.si();
        const double y = corners[i].y.si();
        const double z = corners[i].z.si();
        turned[i] = si(r[0][0] * x + r[0][1] * y + r[0][2] * z,
                       r[1][0] * x + r[1][1] * y + r[1][2] * z,
                       r[2][0] * x + r[2][1] * y + r[2][2] * z);
    }
    return turned;
}

[[nodiscard]] Corners scaled(const Corners& corners, double factor) {
    Corners bigger{};
    for (std::size_t i = 0; i < corners.size(); ++i) {
        bigger[i] = si(corners[i].x.si() * factor, corners[i].y.si() * factor,
                       corners[i].z.si() * factor);
    }
    return bigger;
}

/// Mirrored in z, which reverses orientation and makes the signed volume
/// negative without changing a single edge length. The element that a
/// shape-only metric cannot distinguish from a correct one.
[[nodiscard]] Corners mirrored(const Corners& corners) {
    Corners flipped{};
    for (std::size_t i = 0; i < corners.size(); ++i) {
        flipped[i] = si(corners[i].x.si(), corners[i].y.si(), -corners[i].z.si());
    }
    return flipped;
}

[[nodiscard]] Mesh tetMesh(const Corners& corners) {
    MeshBuilder builder;
    for (const Point3D& corner : corners) {
        REQUIRE(builder.addNode(corner).has_value());
    }
    REQUIRE(builder.addTetrahedron({n(1), n(2), n(3), n(4)}, kRegion).has_value());
    return builder.build();
}

[[nodiscard]] Mesh triangleMesh(const std::array<Point3D, 3>& corners) {
    MeshBuilder builder;
    for (const Point3D& corner : corners) {
        REQUIRE(builder.addNode(corner).has_value());
    }
    REQUIRE(builder.addTriangle({n(1), n(2), n(3)}, kRegion).has_value());
    return builder.build();
}

/// Metrics of the one tetrahedron of @p mesh, through the production API.
[[nodiscard]] TetQuality tetQualityOf(const Mesh& mesh) {
    REQUIRE(mesh.tetrahedra().size() == 1);
    const Result<TetQuality> quality = evaluateTetQuality(mesh, mesh.tetrahedra().front());
    if (!quality.has_value()) {
        FAIL("tet quality refused: " << quality.error().message);
    }
    return *quality;
}

[[nodiscard]] TriangleQuality triangleQualityOf(const Mesh& mesh) {
    REQUIRE(mesh.triangles().size() == 1);
    const Result<TriangleQuality> quality = evaluateTriangleQuality(mesh, mesh.triangles().front());
    if (!quality.has_value()) {
        FAIL("triangle quality refused: " << quality.error().message);
    }
    return *quality;
}

/// An equilateral triangle of side @p a in the z = 0 plane.
[[nodiscard]] std::array<Point3D, 3> equilateral(double a) {
    const double s3 = std::sqrt(3.0);
    return {si(0.0, 0.0, 0.0), si(a, 0.0, 0.0), si(a * 0.5, a * s3 / 2.0, 0.0)};
}

/// A base of length @p a with its apex @p h above the midpoint: isoceles,
/// equilateral when h = a sqrt(3)/2, and flatter as h falls.
[[nodiscard]] std::array<Point3D, 3> isoceles(double a, double h) {
    return {si(0.0, 0.0, 0.0), si(a, 0.0, 0.0), si(a * 0.5, h, 0.0)};
}

[[nodiscard]] QualityThresholds policy(QualityMetric metric, std::optional<double> warning,
                                       std::optional<double> failure) {
    QualityThresholds thresholds;
    thresholds.limits[metric] = QualityThreshold{.warning = warning, .failure = failure};
    REQUIRE(meshing::validate(thresholds).has_value());
    return thresholds;
}

[[nodiscard]] std::vector<QualityClass> classesOf(const MeshQualityReport& report) {
    std::vector<QualityClass> classes;
    classes.reserve(report.findings.size());
    for (const QualityFinding& finding : report.findings) {
        classes.push_back(finding.classification);
    }
    return classes;
}

/// Every metric, for the exhaustiveness tests. Written out so that adding an
/// enumerator without naming it here fails a test rather than silently
/// escaping the name, unit and direction checks.
constexpr std::array<QualityMetric, 18> kAllMetrics{
    QualityMetric::TetVolume,
    QualityMetric::TetJacobianDeterminant,
    QualityMetric::TetMinEdgeLength,
    QualityMetric::TetMaxEdgeLength,
    QualityMetric::TetMeanEdgeLength,
    QualityMetric::TetAspectRatio,
    QualityMetric::TetRadiusRatio,
    QualityMetric::TetInradius,
    QualityMetric::TetCircumradius,
    QualityMetric::TetMinDihedralAngle,
    QualityMetric::TetMaxDihedralAngle,
    QualityMetric::TriangleArea,
    QualityMetric::TriangleMinEdgeLength,
    QualityMetric::TriangleMaxEdgeLength,
    QualityMetric::TriangleMeanEdgeLength,
    QualityMetric::TriangleShapeQuality,
    QualityMetric::TriangleMinAngle,
    QualityMetric::TriangleMaxAngle,
};

/// One extruded solid, for the production-pipeline cases.
struct Extruded {
    Document document{"Part"};
    features::Regenerator regenerator;
    ObjectId feature{};

    [[nodiscard]] FaceName top() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::EndCap}};
    }

    [[nodiscard]] VolumeMesh require_(const VolumeMeshControls& controls = {}) {
        Result<VolumeMesh> mesh = meshing::volumeMeshFor(document, regenerator, feature, controls);
        if (!mesh.has_value()) {
            FAIL("volume mesh refused: " << mesh.error().message);
        }
        return *mesh;
    }

protected:
    void extrudeProfile(std::unique_ptr<sketch::Sketch> sketch, Length depth) {
        const ObjectId profile = require(document.addObject(std::move(sketch)));
        auto solid = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = depth});
        REQUIRE(solid.has_value());
        feature = require(document.addObject(std::move(*solid)));
        requireReport(regenerator, document);
    }
};

/// A rectangular block.
struct Block : Extruded {
    Block(Length a, Length b, Length c) {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        (void)addRectangle(*sketch, 0_mm, 0_mm, a, b);
        extrudeProfile(std::move(sketch), c);
    }
};

/// A cylinder, or -- with an inner circle -- a hollow tube whose void must stay
/// empty.
struct Round : Extruded {
    Round(Length outer, Length height, std::optional<Length> inner = std::nullopt) {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        REQUIRE(sketch->addCircle(Point2D{0_mm, 0_mm}, outer).has_value());
        if (inner.has_value()) {
            REQUIRE(sketch->addCircle(Point2D{0_mm, 0_mm}, *inner).has_value());
        }
        extrudeProfile(std::move(sketch), height);
    }
};

/// A 60 x 60 x 20 mm block with a 20 mm hole bored through it.
struct Bored {
    Document document{"Bored"};
    features::Regenerator regenerator;
    ObjectId bore{};

    Bored() {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        (void)addRectangle(*sketch, 0_mm, 0_mm, 60_mm, 60_mm);
        const ObjectId profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = 20_mm});
        REQUIRE(extrude.has_value());
        const ObjectId solid = require(document.addObject(std::move(*extrude)));
        const geometry::FaceSignature startPlane =
            geometry::planeSignature(Point3D{0_mm, 0_mm, 0_mm}, Direction3D::unitZ().reversed());
        auto hole = features::HoleFeature::create("Bore",
                                                  {.target = FeatureId::fromValue(solid.value()),
                                                   .face = startPlane,
                                                   .center = Point2D{30_mm, 30_mm},
                                                   .diameter = 20_mm});
        REQUIRE(hole.has_value());
        bore = require(document.addObject(std::move(*hole)));
        requireReport(regenerator, document);
    }

    [[nodiscard]] VolumeMesh require_(const VolumeMeshControls& controls = {}) {
        Result<VolumeMesh> mesh = meshing::volumeMeshFor(document, regenerator, bore, controls);
        if (!mesh.has_value()) {
            FAIL("volume mesh refused: " << mesh.error().message);
        }
        return *mesh;
    }
};

/// Boundary deflection fine enough that a curved body's surface is not the
/// binding constraint, coarse enough that the suite stays quick.
[[nodiscard]] VolumeMeshControls curvedTarget(Length size) {
    VolumeMeshControls controls;
    controls.sizing.globalTargetSize = size;
    controls.surface.linearDeflection = Length::fromSi(5e-5);
    return controls;
}

/// Checks that every metric of every element of a production mesh lies inside
/// the range its definition promises. The shared assertion of the integration
/// cases: a metric outside its own range is an implementation defect whatever
/// the geometry.
void checkMetricsAreInRange(const MeshQualityReport& report) {
    for (const TetQuality& tet : report.tets) {
        CHECK(tet.defined);
        CHECK(tet.volume.si() > 0.0);
        CHECK(tet.minEdge.si() > 0.0);
        CHECK(tet.minEdge.si() <= tet.meanEdge.si());
        CHECK(tet.meanEdge.si() <= tet.maxEdge.si());
        CHECK(tet.inradius.si() > 0.0);
        // r <= R/3 for every tetrahedron, with equality only for the regular
        // one: the radius ratio's range is (0, 1] and nothing may leave it.
        CHECK(tet.circumradius.si() >= 3.0 * tet.inradius.si() * (1.0 - kAlgebraic));
        CHECK(tet.aspectRatio >= 1.0 - kAlgebraic);
        CHECK(tet.radiusRatio > 0.0);
        CHECK(tet.radiusRatio <= 1.0 + kAlgebraic);
        CHECK(tet.minDihedral.si() > 0.0);
        CHECK(tet.minDihedral.si() <= tet.maxDihedral.si());
        CHECK(tet.maxDihedral.si() < std::numbers::pi);
        // 6V, exactly, with no drift: a derived quantity rather than a second
        // measurement.
        CHECK(tet.jacobianDeterminant().si() == 6.0 * tet.volume.si());
    }
    for (const TriangleQuality& triangle : report.triangles) {
        CHECK(triangle.defined);
        CHECK(triangle.area.si() > 0.0);
        CHECK(triangle.minEdge.si() > 0.0);
        CHECK(triangle.minEdge.si() <= triangle.meanEdge.si());
        CHECK(triangle.meanEdge.si() <= triangle.maxEdge.si());
        CHECK(triangle.shapeQuality > 0.0);
        CHECK(triangle.shapeQuality <= 1.0 + kAlgebraic);
        CHECK(triangle.minAngle.si() > 0.0);
        CHECK(triangle.minAngle.si() <= triangle.maxAngle.si());
        CHECK(triangle.maxAngle.si() < std::numbers::pi);
    }
}

/// Emits a production mesh's characteristic shape metrics into the test log,
/// so the qualification run records the numbers the evidence cites.
///
/// REPORTED, NEVER GATED. There is no assertion on any of these values,
/// because BetterCAD states no quality requirement: a threshold chosen so that
/// these bodies come out green would be a fabricated engineering judgement
/// dressed as a default, and P17 owns the real one.
void reportShape(std::string_view label, const MeshQualityReport& report) {
    constexpr double kDeg = 180.0 / std::numbers::pi;
    std::string line = std::string{label};
    line += ": " + std::to_string(report.tetCount) + " Tet4, " +
            std::to_string(report.triangleCount) + " Tri3";
    if (report.summaries.contains(QualityMetric::TetRadiusRatio)) {
        const auto& rr = report.summaries.at(QualityMetric::TetRadiusRatio);
        const auto& asp = report.summaries.at(QualityMetric::TetAspectRatio);
        const auto& lo = report.summaries.at(QualityMetric::TetMinDihedralAngle);
        const auto& hi = report.summaries.at(QualityMetric::TetMaxDihedralAngle);
        const auto& edge = report.summaries.at(QualityMetric::TetMeanEdgeLength);
        line += " | 3r/R min " + std::to_string(rr.minimum) + " mean " + std::to_string(rr.mean);
        line += " | aspect max " + std::to_string(asp.maximum) + " mean " +
                std::to_string(asp.mean);
        line += " | dihedral " + std::to_string(lo.minimum * kDeg) + " to " +
                std::to_string(hi.maximum * kDeg) + " deg";
        line += " | element mean edge " + std::to_string(edge.minimum * 1e3) + " to " +
                std::to_string(edge.maximum * 1e3) + " mm";
    }
    if (report.summaries.contains(QualityMetric::TriangleShapeQuality)) {
        const auto& q = report.summaries.at(QualityMetric::TriangleShapeQuality);
        const auto& lo = report.summaries.at(QualityMetric::TriangleMinAngle);
        line += " | tri q min " + std::to_string(q.minimum) + " mean " + std::to_string(q.mean);
        line += " | tri min angle " + std::to_string(lo.minimum * kDeg) + " deg";
    }
    WARN(line);
}

} // namespace

// ---------------------------------------------------------------------------
// The metric vocabulary
// ---------------------------------------------------------------------------

TEST_CASE("QualityMetric_EveryMetricHasADistinctNameAUnitAndADirection",
          "[meshing][quality][metrics]") {
    std::vector<std::string_view> names;
    for (const QualityMetric metric : kAllMetrics) {
        const std::string_view name = meshing::toString(metric);
        CHECK(!name.empty());
        CHECK(name != "unknown_quality_metric");
        CHECK(meshing::unitOf(metric) != "?");
        names.push_back(name);
    }
    std::ranges::sort(names);
    CHECK(std::ranges::adjacent_find(names) == names.end());

    // THE LIST ABOVE IS COMPLETE, and this is what keeps it so. The
    // enumeration starts at 0 and is contiguous, so the last enumerator's
    // value plus one is the number of metrics: adding one without naming it
    // here fails this line, rather than silently escaping the name, unit and
    // direction checks.
    CHECK(static_cast<std::size_t>(QualityMetric::TriangleMaxAngle) + 1 == kAllMetrics.size());
}

TEST_CASE("QualityMetric_ADimensionedSizeIsContextOnlyAndAShapeMetricIsNot",
          "[meshing][quality][metrics]") {
    // The division that keeps a threshold off a quantity whose "good" value
    // depends on the model's scale.
    CHECK(meshing::direction(QualityMetric::TetVolume) == QualityDirection::ContextOnly);
    CHECK(meshing::direction(QualityMetric::TetMinEdgeLength) == QualityDirection::ContextOnly);
    CHECK(meshing::direction(QualityMetric::TetInradius) == QualityDirection::ContextOnly);
    CHECK(meshing::direction(QualityMetric::TetCircumradius) == QualityDirection::ContextOnly);
    CHECK(meshing::direction(QualityMetric::TriangleArea) == QualityDirection::ContextOnly);

    CHECK(meshing::direction(QualityMetric::TetAspectRatio) == QualityDirection::LowerIsBetter);
    CHECK(meshing::direction(QualityMetric::TetMaxDihedralAngle) == QualityDirection::LowerIsBetter);
    CHECK(meshing::direction(QualityMetric::TetRadiusRatio) == QualityDirection::HigherIsBetter);
    CHECK(meshing::direction(QualityMetric::TetMinDihedralAngle) ==
          QualityDirection::HigherIsBetter);
    CHECK(meshing::direction(QualityMetric::TriangleShapeQuality) ==
          QualityDirection::HigherIsBetter);
}

TEST_CASE("QualityClass_EveryClassHasADistinctName", "[meshing][quality][metrics]") {
    const std::array<QualityClass, 4> classes{QualityClass::Valid, QualityClass::Warning,
                                              QualityClass::Failure, QualityClass::Invalid};
    std::vector<std::string_view> names;
    for (const QualityClass classification : classes) {
        names.push_back(meshing::toString(classification));
        CHECK(names.back() != "unknown_quality_class");
    }
    std::ranges::sort(names);
    CHECK(std::ranges::adjacent_find(names) == names.end());
}

// ---------------------------------------------------------------------------
// Tet4: the analytical references
// ---------------------------------------------------------------------------

TEST_CASE("QualityTet_RegularTetrahedronMatchesItsClosedFormMetrics",
          "[meshing][quality][tet][analytical]") {
    constexpr double a = 0.01; // 10 mm
    const TetQuality q = tetQualityOf(tetMesh(regularTet(a)));

    CHECK(q.defined);
    // V = sqrt(2)/12 a^3 = 1.1785113019775791e-7 m^3 for a = 10 mm.
    CHECK_THAT(q.volume.si(), WithinRel(std::sqrt(2.0) / 12.0 * a * a * a, kAlgebraic));
    // det(J) = 6V, exactly.
    CHECK(q.jacobianDeterminant().si() == 6.0 * q.volume.si());

    // All six edges equal a.
    CHECK_THAT(q.minEdge.si(), WithinRel(a, kAlgebraic));
    CHECK_THAT(q.maxEdge.si(), WithinRel(a, kAlgebraic));
    CHECK_THAT(q.meanEdge.si(), WithinRel(a, kAlgebraic));
    CHECK_THAT(q.aspectRatio, WithinRel(1.0, kAlgebraic));

    // r = 3V/A with A = sqrt(3) a^2, so r = sqrt(6)/12 a = 2.0412414523193151 mm.
    CHECK_THAT(q.inradius.si(), WithinRel(std::sqrt(6.0) / 12.0 * a, kAlgebraic));
    // R = sqrt(6)/4 a = 6.123724356957945 mm.
    CHECK_THAT(q.circumradius.si(), WithinRel(std::sqrt(6.0) / 4.0 * a, kAlgebraic));
    // 3r/R = 1: the regular tetrahedron is the unique maximiser, so this is the
    // metric's upper bound and not merely a value.
    CHECK_THAT(q.radiusRatio, WithinRel(1.0, kAlgebraic));

    // acos(1/3) = 1.2309594173407747 rad = 70.52877936550931 deg, all six.
    //
    // NOT acos(-1/3) = 109.4712 deg, which is the angle between the outward
    // normals. Both bounds are asserted so that an implementation reporting the
    // supplement fails here rather than somewhere downstream.
    const double internal = std::acos(1.0 / 3.0);
    CHECK_THAT(q.minDihedral.si(), WithinAbs(internal, kAngle));
    CHECK_THAT(q.maxDihedral.si(), WithinAbs(internal, kAngle));
    CHECK(q.maxDihedral.si() < std::acos(-1.0 / 3.0) - 0.1);
}

TEST_CASE("QualityTet_CubeCornerTetrahedronMatchesItsClosedFormMetrics",
          "[meshing][quality][tet][analytical]") {
    // THE SECOND, INDEPENDENT REFERENCE. It disagrees with the regular
    // tetrahedron on every shape metric, so an implementation cannot satisfy
    // both by accident -- in particular its dihedral range straddles the
    // regular one's single value, 54.74 deg below and 90 deg above.
    constexpr double a = 0.01;
    const TetQuality q = tetQualityOf(tetMesh(cornerTet(a)));

    CHECK(q.defined);
    CHECK_THAT(q.volume.si(), WithinRel(a * a * a / 6.0, kAlgebraic));
    CHECK(q.jacobianDeterminant().si() == 6.0 * q.volume.si());

    // Three legs of a, three face diagonals of a sqrt(2).
    CHECK_THAT(q.minEdge.si(), WithinRel(a, kAlgebraic));
    CHECK_THAT(q.maxEdge.si(), WithinRel(a * std::sqrt(2.0), kAlgebraic));
    CHECK_THAT(q.meanEdge.si(), WithinRel(a * (1.0 + std::sqrt(2.0)) / 2.0, kAlgebraic));
    CHECK_THAT(q.aspectRatio, WithinRel(std::sqrt(2.0), kAlgebraic));

    // A = 3/2 a^2 (three right isoceles legs) + sqrt(3)/2 a^2 (the equilateral
    // slant of side a sqrt(2)), so r = 3V/A = (3 - sqrt(3))/6 a.
    CHECK_THAT(q.inradius.si(), WithinRel((3.0 - std::sqrt(3.0)) / 6.0 * a, kAlgebraic));
    // The circumcentre is the cube's centre, so R = sqrt(3)/2 a.
    CHECK_THAT(q.circumradius.si(), WithinRel(std::sqrt(3.0) / 2.0 * a, kAlgebraic));
    // 3r/R = (3 - sqrt(3))/sqrt(3) = sqrt(3) - 1 = 0.7320508075688772.
    CHECK_THAT(q.radiusRatio, WithinRel(std::sqrt(3.0) - 1.0, kAlgebraic));

    // Three dihedrals of pi/2 along the mutually perpendicular legs, three of
    // acos(1/sqrt(3)) = 54.735610317245346 deg where a leg face meets the slant.
    CHECK_THAT(q.minDihedral.si(), WithinAbs(std::acos(1.0 / std::sqrt(3.0)), kAngle));
    CHECK_THAT(q.maxDihedral.si(), WithinAbs(std::numbers::pi / 2.0, kAngle));
}

TEST_CASE("QualityTet_MetricsAreUnchangedByTranslation", "[meshing][quality][tet][invariance]") {
    const TetQuality origin = tetQualityOf(tetMesh(regularTet(0.01)));
    // A displacement 10 000 times the element. The circumcentre solve is done
    // relative to the first corner, so a formulation using absolute positions
    // would lose most of its significant digits here.
    const TetQuality moved = tetQualityOf(tetMesh(translated(regularTet(0.01), 100.0, -50.0, 7.0)));

    // A TOLERANCE WITH A DERIVATION, not a number chosen to make this pass.
    // Storing a coordinate at 100 m quantises it to eps * 100 = 2.2e-14 m, and
    // every metric is built from differences of such coordinates over a 10 mm
    // edge, so the relative error admitted is eps * offset / edge = 2.2e-12.
    // That is a property of representing the INPUT and no implementation can do
    // better; 1e-11 leaves a factor of five over it.
    constexpr double kDisplaced = 1e-11;

    CHECK_THAT(moved.volume.si(), WithinRel(origin.volume.si(), kDisplaced));
    CHECK_THAT(moved.minEdge.si(), WithinRel(origin.minEdge.si(), kDisplaced));
    CHECK_THAT(moved.maxEdge.si(), WithinRel(origin.maxEdge.si(), kDisplaced));
    CHECK_THAT(moved.inradius.si(), WithinRel(origin.inradius.si(), kDisplaced));
    CHECK_THAT(moved.circumradius.si(), WithinRel(origin.circumradius.si(), kDisplaced));
    CHECK_THAT(moved.aspectRatio, WithinRel(origin.aspectRatio, kDisplaced));
    CHECK_THAT(moved.radiusRatio, WithinRel(origin.radiusRatio, kDisplaced));
    CHECK_THAT(moved.minDihedral.si(), WithinAbs(origin.minDihedral.si(), kDisplaced));
    CHECK_THAT(moved.maxDihedral.si(), WithinAbs(origin.maxDihedral.si(), kDisplaced));
}

TEST_CASE("QualityTet_MetricsAreUnchangedByRotation", "[meshing][quality][tet][invariance]") {
    const TetQuality upright = tetQualityOf(tetMesh(cornerTet(0.01)));
    const TetQuality turned = tetQualityOf(tetMesh(rotated(cornerTet(0.01), 0.7)));

    CHECK_THAT(turned.volume.si(), WithinRel(upright.volume.si(), kAlgebraic));
    CHECK_THAT(turned.minEdge.si(), WithinRel(upright.minEdge.si(), kAlgebraic));
    CHECK_THAT(turned.maxEdge.si(), WithinRel(upright.maxEdge.si(), kAlgebraic));
    CHECK_THAT(turned.inradius.si(), WithinRel(upright.inradius.si(), kAlgebraic));
    CHECK_THAT(turned.circumradius.si(), WithinRel(upright.circumradius.si(), kAlgebraic));
    CHECK_THAT(turned.aspectRatio, WithinRel(upright.aspectRatio, kAlgebraic));
    CHECK_THAT(turned.radiusRatio, WithinRel(upright.radiusRatio, kAlgebraic));
    CHECK_THAT(turned.minDihedral.si(), WithinAbs(upright.minDihedral.si(), kAngle));
    CHECK_THAT(turned.maxDihedral.si(), WithinAbs(upright.maxDihedral.si(), kAngle));
    // A rotation preserves orientation, so the volume stays POSITIVE. Asserted
    // separately from the magnitude: an implementation that took an absolute
    // value would pass the comparison above and fail the point of it.
    CHECK(turned.volume.si() > 0.0);
}

TEST_CASE("QualityTet_DimensionlessMetricsAreUnchangedByUniformScaling",
          "[meshing][quality][tet][invariance]") {
    constexpr double a = 0.01;
    constexpr double k = 1000.0;
    const TetQuality small = tetQualityOf(tetMesh(cornerTet(a)));
    const TetQuality large = tetQualityOf(tetMesh(scaled(cornerTet(a), k)));

    // SCALE-FREE: the shape metrics do not move at all.
    CHECK_THAT(large.aspectRatio, WithinRel(small.aspectRatio, kAlgebraic));
    CHECK_THAT(large.radiusRatio, WithinRel(small.radiusRatio, kAlgebraic));
    CHECK_THAT(large.minDihedral.si(), WithinAbs(small.minDihedral.si(), kAngle));
    CHECK_THAT(large.maxDihedral.si(), WithinAbs(small.maxDihedral.si(), kAngle));

    // DIMENSIONED: the sizes scale by exactly the right power, which is what
    // makes "is this volume good?" a question with no scale-free answer.
    CHECK_THAT(large.volume.si(), WithinRel(small.volume.si() * k * k * k, kAlgebraic));
    CHECK_THAT(large.minEdge.si(), WithinRel(small.minEdge.si() * k, kAlgebraic));
    CHECK_THAT(large.maxEdge.si(), WithinRel(small.maxEdge.si() * k, kAlgebraic));
    CHECK_THAT(large.meanEdge.si(), WithinRel(small.meanEdge.si() * k, kAlgebraic));
    CHECK_THAT(large.inradius.si(), WithinRel(small.inradius.si() * k, kAlgebraic));
    CHECK_THAT(large.circumradius.si(), WithinRel(small.circumradius.si() * k, kAlgebraic));
}

// ---------------------------------------------------------------------------
// Tet4: validity is not quality
// ---------------------------------------------------------------------------

TEST_CASE("QualityTet_RefusesAMirroredTetrahedronRatherThanScoringIt",
          "[meshing][quality][tet][validity]") {
    // THE CASE THE WHOLE BOUNDARY EXISTS FOR. A mirrored regular tetrahedron
    // has identical edge lengths, identical face areas and identical unsigned
    // dihedral angles to the original: by every shape metric it is perfect. Its
    // signed volume is negative, so it is INVALID, and no number is produced
    // for it.
    const Mesh mesh = tetMesh(mirrored(regularTet(0.01)));
    const Result<TetQuality> quality = evaluateTetQuality(mesh, mesh.tetrahedra().front());

    REQUIRE_FALSE(quality.has_value());
    CHECK(quality.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(quality.error().message, ContainsSubstring("inverted"));
}

TEST_CASE("QualityTet_RefusesACoplanarTetrahedron", "[meshing][quality][tet][validity]") {
    const Mesh mesh = tetMesh(Corners{si(0.0, 0.0, 0.0), si(0.01, 0.0, 0.0), si(0.0, 0.01, 0.0),
                                      si(0.01, 0.01, 0.0)});
    const Result<TetQuality> quality = evaluateTetQuality(mesh, mesh.tetrahedra().front());

    REQUIRE_FALSE(quality.has_value());
    CHECK(quality.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(quality.error().message, ContainsSubstring("degenerate"));
}

TEST_CASE("QualityTet_RefusesAnElementNamingANodeTheMeshDoesNotHave",
          "[meshing][quality][tet][validity]") {
    const Mesh mesh = tetMesh(regularTet(0.01));
    const Tetrahedron dangling{ElementId::fromValue(9), {n(1), n(2), n(3), n(77)}, kRegion};

    const Result<TetQuality> quality = evaluateTetQuality(mesh, dangling);
    REQUIRE_FALSE(quality.has_value());
    CHECK(quality.error().code == ErrorCode::InvalidArgument);
    CHECK_THAT(quality.error().message, ContainsSubstring("node:77"));
    CHECK_THAT(quality.error().message, ContainsSubstring("element:9"));
}

TEST_CASE("QualityTet_RefusesAnElementThatRepeatsANodeHandle",
          "[meshing][quality][tet][validity]") {
    const Mesh mesh = tetMesh(regularTet(0.01));
    const Tetrahedron repeated{ElementId::fromValue(9), {n(1), n(1), n(3), n(4)}, kRegion};

    const Result<TetQuality> quality = evaluateTetQuality(mesh, repeated);
    REQUIRE_FALSE(quality.has_value());
    CHECK(quality.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(quality.error().message, ContainsSubstring("repeats a node handle, node:1"));
}

TEST_CASE("QualityTet_RefusesATetrahedronWhoseVolumeOverflows",
          "[meshing][quality][tet][validity]") {
    // Finite coordinates whose determinant is not finite. MeshBuilder refuses a
    // non-finite COORDINATE at entry, so this is how the non-finite branch is
    // actually reached -- and it is reachable, which is why it is a refusal and
    // not an assertion.
    const Mesh mesh = tetMesh(cornerTet(1e200));
    const Result<TetQuality> quality = evaluateTetQuality(mesh, mesh.tetrahedra().front());

    REQUIRE_FALSE(quality.has_value());
    CHECK(quality.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(quality.error().message, ContainsSubstring("non-finite signed volume"));
}

TEST_CASE("QualityTet_RefusesATetrahedronWhoseFaceAreaOverflows",
          "[meshing][quality][tet][validity]") {
    // A body 1e90 m across. The signed volume is finite (1e270), but a face's
    // cross product is 1e180 and squaring it for the magnitude overflows. The
    // refusal names the face rather than normalising an infinity.
    const Mesh mesh = tetMesh(cornerTet(1e90));
    const Result<TetQuality> quality = evaluateTetQuality(mesh, mesh.tetrahedra().front());

    REQUIRE_FALSE(quality.has_value());
    CHECK(quality.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(quality.error().message, ContainsSubstring("zero or not finite"));
}

TEST_CASE("QualityTet_MarksAnElementUndefinedWhenItsCircumradiusOverflows",
          "[meshing][quality][tet][validity]") {
    // THE ILL-CONDITIONED CIRCUMCENTRE SOLVE, reached deliberately. The
    // circumradius is the numerator over 12V, so flattening an element drives it
    // up: a 1 m equilateral base with its apex 1e-200 m above it has
    // R = 0.173/h = 1.7e199 m, whose square overflows.
    //
    // The element is REPORTED WITH `defined == false`. An infinity is never
    // clamped into a plausible-looking score, and the mesh report counts the
    // element Invalid rather than giving it a radius ratio of zero that a
    // threshold could be relaxed past.
    const Mesh mesh = tetMesh(wedgeTet(1.0, 1e-200));
    const Result<TetQuality> quality = evaluateTetQuality(mesh, mesh.tetrahedra().front());

    REQUIRE(quality.has_value());
    CHECK(quality->volume.si() > 0.0);
    CHECK(std::isfinite(quality->inradius.si()));
    CHECK_FALSE(std::isfinite(quality->circumradius.si()));
    CHECK_FALSE(quality->defined);

    const MeshQualityReport report = meshing::evaluateMeshQuality(mesh);
    CHECK(report.invalidElements == 1);
    CHECK(report.validElements == 0);
    CHECK_FALSE(report.satisfiesPolicy());
    // Structurally the mesh is fine -- the coordinates and the signed volume are
    // finite and positive -- so this is the quality layer's own undefined case
    // and not a restatement of P16-DATA-001's.
    CHECK(report.structurallyValid);
    // No summaries either: an undefined element contributes no samples, rather
    // than contributing an infinity that would poison every aggregate.
    CHECK(report.summaries.empty());
    // AND THE REPORT NAMES THE NUMBER THAT FAILED. Before the two radii had
    // metric names there was nothing for this finding to point at, so it said
    // only "a shape metric" -- and a mutation deleting the finiteness check
    // survived the suite, because no measured quantity reaching the check could
    // ever be non-finite. See ADVERSARIAL_REVIEW.md finding 1.
    REQUIRE(report.findings.size() == 1);
    CHECK(report.findings.front().classification == QualityClass::Invalid);
    CHECK(report.findings.front().metric == QualityMetric::TetCircumradius);
    CHECK_THAT(report.findings.front().message, ContainsSubstring("tet_circumradius"));
    CHECK_THAT(report.findings.front().message, ContainsSubstring("not finite"));
    CHECK_FALSE(report.findings.front().threshold.has_value());
}

// ---------------------------------------------------------------------------
// Tet4: what a bad element looks like
// ---------------------------------------------------------------------------

TEST_CASE("QualityTet_SliverIsValidButScoresFarWorseThanARegularElement",
          "[meshing][quality][tet][shape]") {
    const TetQuality good = tetQualityOf(tetMesh(regularTet(0.01)));
    // An equilateral base of 10 mm with its apex 0.2 mm above it.
    const TetQuality sliver = tetQualityOf(tetMesh(wedgeTet(0.01, 0.0002)));

    // STILL VALID: positive volume, four distinct nodes, every metric finite.
    CHECK(sliver.defined);
    CHECK(sliver.volume.si() > 0.0);

    CHECK(sliver.radiusRatio < good.radiusRatio / 10.0);
    CHECK(sliver.minDihedral.si() < good.minDihedral.si() / 3.0);
    CHECK(sliver.maxDihedral.si() > good.maxDihedral.si() * 2.0);
    // 3r/R = 0.0036 and min dihedral 3.96 deg for this element, against 1 and
    // 70.53 deg for the regular one.
    CHECK_THAT(sliver.radiusRatio, WithinRel(0.0035912, 1e-4));
}

TEST_CASE("QualityTet_AspectRatioAloneCannotDetectASliver", "[meshing][quality][tet][shape]") {
    // WHY TWO SHAPE METRICS ARE CARRIED, demonstrated rather than asserted in a
    // comment. As the wedge flattens, l_max/l_min saturates at sqrt(3) -- the
    // ratio of an equilateral base's edge to nothing shorter -- while the radius
    // ratio collapses by three orders of magnitude. An implementation that
    // shipped the aspect ratio alone would call a 1.98 deg sliver a 1.73.
    const TetQuality thick = tetQualityOf(tetMesh(wedgeTet(0.01, 0.001)));
    const TetQuality thin = tetQualityOf(tetMesh(wedgeTet(0.01, 0.0001)));

    CHECK(thin.aspectRatio < thick.aspectRatio * 1.05);
    CHECK(thin.aspectRatio < std::sqrt(3.0));
    CHECK(thin.radiusRatio < thick.radiusRatio / 50.0);
}

TEST_CASE("QualityTet_QualityFallsMonotonicallyAsAnElementFlattens",
          "[meshing][quality][tet][shape]") {
    // One family, one parameter. The sequence is the evidence that the metrics
    // ORDER elements rather than merely labelling two of them.
    const std::array<double, 6> heights{0.01, 0.005, 0.0025, 0.001, 0.0005, 0.0002};
    std::vector<TetQuality> sequence;
    for (const double h : heights) {
        sequence.push_back(tetQualityOf(tetMesh(wedgeTet(0.01, h))));
    }
    for (std::size_t i = 1; i < sequence.size(); ++i) {
        CHECK(sequence[i].defined);
        CHECK(sequence[i].radiusRatio < sequence[i - 1].radiusRatio);
        CHECK(sequence[i].minDihedral.si() < sequence[i - 1].minDihedral.si());
        CHECK(sequence[i].maxDihedral.si() > sequence[i - 1].maxDihedral.si());
        CHECK(sequence[i].volume.si() < sequence[i - 1].volume.si());
    }
    // And at the end of the family the element becomes coplanar, at which point
    // it stops being a bad tetrahedron and becomes not one at all.
    const Mesh flat = tetMesh(wedgeTet(0.01, 0.0));
    CHECK_FALSE(evaluateTetQuality(flat, flat.tetrahedra().front()).has_value());
}

TEST_CASE("QualityTet_StretchedElementHasAHighAspectRatio", "[meshing][quality][tet][shape]") {
    // A corner tetrahedron with one leg eight times the others. The aspect
    // ratio is the metric that catches THIS, which is the complement of the
    // sliver case above.
    const Mesh mesh = tetMesh(Corners{si(0.0, 0.0, 0.0), si(0.08, 0.0, 0.0), si(0.0, 0.01, 0.0),
                                      si(0.0, 0.0, 0.01)});
    const TetQuality q = tetQualityOf(mesh);

    CHECK(q.defined);
    // l_max is the diagonal sqrt(80^2 + 10^2) mm and l_min is 10 mm.
    CHECK_THAT(q.aspectRatio, WithinRel(std::sqrt(0.08 * 0.08 + 0.01 * 0.01) / 0.01, kAlgebraic));
    CHECK(q.aspectRatio > 8.0);
    CHECK(q.radiusRatio < 0.25);
}

// ---------------------------------------------------------------------------
// Triangle3
// ---------------------------------------------------------------------------

TEST_CASE("QualityTriangle_EquilateralTriangleMatchesItsClosedFormMetrics",
          "[meshing][quality][triangle][analytical]") {
    constexpr double a = 0.01;
    const TriangleQuality q = triangleQualityOf(triangleMesh(equilateral(a)));

    CHECK(q.defined);
    // A = sqrt(3)/4 a^2.
    CHECK_THAT(q.area.si(), WithinRel(std::sqrt(3.0) / 4.0 * a * a, kAlgebraic));
    CHECK_THAT(q.minEdge.si(), WithinRel(a, kAlgebraic));
    CHECK_THAT(q.maxEdge.si(), WithinRel(a, kAlgebraic));
    CHECK_THAT(q.meanEdge.si(), WithinRel(a, kAlgebraic));
    // q = 4 sqrt(3) A / sum l^2 = 4 sqrt(3) (sqrt(3)/4 a^2) / 3a^2 = 1, the
    // metric's upper bound and the equilateral triangle's defining property.
    CHECK_THAT(q.shapeQuality, WithinRel(1.0, kAlgebraic));
    CHECK_THAT(q.minAngle.si(), WithinAbs(std::numbers::pi / 3.0, kAngle));
    CHECK_THAT(q.maxAngle.si(), WithinAbs(std::numbers::pi / 3.0, kAngle));
}

TEST_CASE("QualityTriangle_RightIsocelesTriangleMatchesItsClosedFormMetrics",
          "[meshing][quality][triangle][analytical]") {
    // The second independent reference: legs a, a and hypotenuse a sqrt(2).
    constexpr double a = 0.01;
    const TriangleQuality q =
        triangleQualityOf(triangleMesh({si(0.0, 0.0, 0.0), si(a, 0.0, 0.0), si(0.0, a, 0.0)}));

    CHECK_THAT(q.area.si(), WithinRel(a * a / 2.0, kAlgebraic));
    CHECK_THAT(q.minEdge.si(), WithinRel(a, kAlgebraic));
    CHECK_THAT(q.maxEdge.si(), WithinRel(a * std::sqrt(2.0), kAlgebraic));
    CHECK_THAT(q.meanEdge.si(), WithinRel(a * (2.0 + std::sqrt(2.0)) / 3.0, kAlgebraic));
    // q = 4 sqrt(3) (a^2/2) / 4a^2 = sqrt(3)/2 = 0.8660254037844386.
    CHECK_THAT(q.shapeQuality, WithinRel(std::sqrt(3.0) / 2.0, kAlgebraic));
    CHECK_THAT(q.minAngle.si(), WithinAbs(std::numbers::pi / 4.0, kAngle));
    CHECK_THAT(q.maxAngle.si(), WithinAbs(std::numbers::pi / 2.0, kAngle));
}

TEST_CASE("QualityTriangle_ShapeQualityIsScaleFreeAndRotationInvariant",
          "[meshing][quality][triangle][invariance]") {
    const std::array<Point3D, 3> flat{si(0.0, 0.0, 0.0), si(0.01, 0.0, 0.0), si(0.004, 0.003, 0.0)};
    // The same triangle lifted out of the z = 0 plane and scaled: a triangle in
    // three dimensions has no privileged plane, and a formulation that worked
    // in two would fail here.
    const std::array<Point3D, 3> tilted{si(0.0, 0.0, 0.0), si(0.0, 0.01, 0.0),
                                        si(0.0, 0.004, 0.003)};
    const TriangleQuality a = triangleQualityOf(triangleMesh(flat));
    const TriangleQuality b = triangleQualityOf(triangleMesh(tilted));
    CHECK_THAT(b.shapeQuality, WithinRel(a.shapeQuality, kAlgebraic));
    CHECK_THAT(b.minAngle.si(), WithinAbs(a.minAngle.si(), kAngle));
    CHECK_THAT(b.maxAngle.si(), WithinAbs(a.maxAngle.si(), kAngle));

    const TriangleQuality big = triangleQualityOf(triangleMesh(
        {si(0.0, 0.0, 0.0), si(10.0, 0.0, 0.0), si(4.0, 3.0, 0.0)}));
    CHECK_THAT(big.shapeQuality, WithinRel(a.shapeQuality, kAlgebraic));
    CHECK_THAT(big.area.si(), WithinRel(a.area.si() * 1e6, kAlgebraic));
}

TEST_CASE("QualityTriangle_ShapeQualityFallsMonotonicallyAsATriangleFlattens",
          "[meshing][quality][triangle][shape]") {
    const std::array<double, 5> heights{0.008, 0.004, 0.002, 0.001, 0.0005};
    std::vector<TriangleQuality> sequence;
    for (const double h : heights) {
        sequence.push_back(triangleQualityOf(triangleMesh(isoceles(0.01, h))));
    }
    for (std::size_t i = 1; i < sequence.size(); ++i) {
        CHECK(sequence[i].shapeQuality < sequence[i - 1].shapeQuality);
        CHECK(sequence[i].minAngle.si() < sequence[i - 1].minAngle.si());
        CHECK(sequence[i].maxAngle.si() > sequence[i - 1].maxAngle.si());
    }
    // h = 2 mm on a 10 mm base: q = 0.4385, min angle 21.80 deg.
    CHECK_THAT(sequence[2].shapeQuality, WithinRel(0.43849387533389295, kAlgebraic));
    CHECK_THAT(sequence[2].minAngle.si(), WithinAbs(0.38050637711236523, kAngle));
}

TEST_CASE("QualityTriangle_RefusesACollinearTriangle", "[meshing][quality][triangle][validity]") {
    const Mesh mesh =
        triangleMesh({si(0.0, 0.0, 0.0), si(0.01, 0.0, 0.0), si(0.02, 0.0, 0.0)});
    const Result<TriangleQuality> quality =
        evaluateTriangleQuality(mesh, mesh.triangles().front());

    REQUIRE_FALSE(quality.has_value());
    CHECK(quality.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(quality.error().message, ContainsSubstring("degenerate"));
    // A degenerate triangle is INVALID, not shape quality zero. The distinction
    // matters because zero is a number a threshold can be relaxed past.
    CHECK_THAT(quality.error().message, ContainsSubstring("collinear"));
}

TEST_CASE("QualityTriangle_RefusesATriangleWhoseAreaOverflows",
          "[meshing][quality][triangle][validity]") {
    const Mesh mesh =
        triangleMesh({si(0.0, 0.0, 0.0), si(1e200, 0.0, 0.0), si(0.0, 1e200, 0.0)});
    const Result<TriangleQuality> quality =
        evaluateTriangleQuality(mesh, mesh.triangles().front());

    REQUIRE_FALSE(quality.has_value());
    CHECK_THAT(quality.error().message, ContainsSubstring("non-finite area"));
}

// ---------------------------------------------------------------------------
// The policy: thresholds classify, and never compute
// ---------------------------------------------------------------------------

TEST_CASE("QualityPolicy_BetterCadShipsNoThresholdsAtAll", "[meshing][quality][policy]") {
    // A DELIBERATE DEFAULT, asserted so that a later commit cannot quietly
    // invent engineering judgement. Quality thresholds are solver requirements
    // and P17 owns those.
    CHECK(meshing::reportOnlyThresholds().limits.empty());

    const MeshQualityReport report = meshing::evaluateMeshQuality(tetMesh(wedgeTet(0.01, 0.0002)));
    // A 3.96 deg sliver. Under the default policy it is measured, reported and
    // classified Valid -- because no one has said what a solver needs.
    CHECK(report.tetCount == 1);
    CHECK(report.validElements == 1);
    CHECK(report.failureElements == 0);
    CHECK(report.findings.empty());
    CHECK(report.satisfiesPolicy());
    REQUIRE(report.tets.size() == 1);
    CHECK(report.tets.front().radiusRatio < 0.01);
}

TEST_CASE("QualityPolicy_ChangingThePolicyChangesClassificationsAndNotMetrics",
          "[meshing][quality][policy]") {
    const Mesh mesh = tetMesh(wedgeTet(0.01, 0.0002));
    const MeshQualityReport lenient = meshing::evaluateMeshQuality(mesh);
    const MeshQualityReport strict = meshing::evaluateMeshQuality(
        mesh, policy(QualityMetric::TetRadiusRatio, 0.5, 0.1));

    // IDENTICAL NUMBERS. Every measured field, compared as a whole struct.
    REQUIRE(lenient.tets.size() == 1);
    REQUIRE(strict.tets.size() == 1);
    CHECK(lenient.tets.front() == strict.tets.front());
    CHECK(lenient.summaries == strict.summaries);

    // DIFFERENT VERDICTS.
    CHECK(lenient.failureElements == 0);
    CHECK(strict.failureElements == 1);
    CHECK(lenient.satisfiesPolicy());
    CHECK_FALSE(strict.satisfiesPolicy());
    // And the mesh is still structurally valid under both: a failing threshold
    // never becomes a structural complaint.
    CHECK(lenient.structurallyValid);
    CHECK(strict.structurallyValid);
}

TEST_CASE("QualityPolicy_AValueExactlyAtAThresholdIsOnTheGoodSideOfIt",
          "[meshing][quality][policy]") {
    const Mesh mesh = tetMesh(cornerTet(0.01));
    const double measured = tetQualityOf(mesh).radiusRatio;

    // Equal is good: the comparison is strict.
    const MeshQualityReport atBound = meshing::evaluateMeshQuality(
        mesh, policy(QualityMetric::TetRadiusRatio, measured, std::nullopt));
    CHECK(atBound.validElements == 1);
    CHECK(atBound.warningElements == 0);

    // One representable step worse, and it is a warning. Nothing between the
    // two values exists, so this pins the inequality rather than approximating
    // it.
    const MeshQualityReport pastBound = meshing::evaluateMeshQuality(
        mesh, policy(QualityMetric::TetRadiusRatio, std::nextafter(measured, 1.0), std::nullopt));
    CHECK(pastBound.warningElements == 1);
    CHECK(pastBound.validElements == 0);
    REQUIRE(pastBound.findings.size() == 1);
    CHECK(pastBound.findings.front().classification == QualityClass::Warning);
    CHECK(pastBound.findings.front().metric == QualityMetric::TetRadiusRatio);
    CHECK(pastBound.findings.front().threshold.has_value());
}

TEST_CASE("QualityPolicy_FailureTakesPrecedenceOverWarning", "[meshing][quality][policy]") {
    const MeshQualityReport report = meshing::evaluateMeshQuality(
        tetMesh(wedgeTet(0.01, 0.0002)), policy(QualityMetric::TetRadiusRatio, 0.5, 0.1));
    CHECK(report.failureElements == 1);
    CHECK(report.warningElements == 0);
    // ONE finding, not two: an element past both bounds is reported at its
    // worst, not once per bound.
    REQUIRE(report.findings.size() == 1);
    CHECK(report.findings.front().classification == QualityClass::Failure);
}

TEST_CASE("QualityPolicy_ClassifiesALowerIsBetterMetricTheOtherWayRound",
          "[meshing][quality][policy]") {
    // The aspect ratio fails when it is TOO LARGE. A single comparison for both
    // directions is the defect this catches.
    const Mesh stretched = tetMesh(Corners{si(0.0, 0.0, 0.0), si(0.08, 0.0, 0.0),
                                           si(0.0, 0.01, 0.0), si(0.0, 0.0, 0.01)});
    const MeshQualityReport bad = meshing::evaluateMeshQuality(
        stretched, policy(QualityMetric::TetAspectRatio, 3.0, 6.0));
    CHECK(bad.failureElements == 1);

    const MeshQualityReport good = meshing::evaluateMeshQuality(
        tetMesh(regularTet(0.01)), policy(QualityMetric::TetAspectRatio, 3.0, 6.0));
    CHECK(good.validElements == 1);
    CHECK(good.findings.empty());
}

TEST_CASE("QualityPolicy_AppliesToSurfaceTrianglesToo", "[meshing][quality][policy]") {
    const MeshQualityReport report = meshing::evaluateMeshQuality(
        triangleMesh(isoceles(0.01, 0.0005)),
        policy(QualityMetric::TriangleShapeQuality, 0.6, 0.2));
    CHECK(report.triangleCount == 1);
    CHECK(report.failureElements == 1);
    REQUIRE(report.findings.size() == 1);
    CHECK(report.findings.front().type == ElementType::Triangle3);
}

TEST_CASE("QualityPolicy_RefusesAThresholdOnADimensionedSize", "[meshing][quality][policy]") {
    // "Every tetrahedron must have a volume above 1" is a statement about
    // metres, not about quality, and would mean different things for a bracket
    // and a bridge.
    QualityThresholds thresholds;
    thresholds.limits[QualityMetric::TetVolume] = QualityThreshold{.warning = 1.0};
    const Result<void> checked = meshing::validate(thresholds);

    REQUIRE_FALSE(checked.has_value());
    CHECK(checked.error().code == ErrorCode::InvalidArgument);
    CHECK_THAT(checked.error().message, ContainsSubstring("tet_volume"));
    CHECK_THAT(checked.error().message, ContainsSubstring("dimensioned size"));
}

TEST_CASE("QualityPolicy_RefusesANonFiniteThreshold", "[meshing][quality][policy]") {
    QualityThresholds nan;
    nan.limits[QualityMetric::TetRadiusRatio] =
        QualityThreshold{.warning = std::numeric_limits<double>::quiet_NaN()};
    CHECK_FALSE(meshing::validate(nan).has_value());

    QualityThresholds infinite;
    infinite.limits[QualityMetric::TetAspectRatio] =
        QualityThreshold{.failure = std::numeric_limits<double>::infinity()};
    const Result<void> checked = meshing::validate(infinite);
    REQUIRE_FALSE(checked.has_value());
    CHECK_THAT(checked.error().message, ContainsSubstring("non-finite threshold"));
}

TEST_CASE("QualityPolicy_RefusesAFailureBoundOnTheGoodSideOfItsWarning",
          "[meshing][quality][policy]") {
    // Higher-is-better: a failure bound ABOVE the warning bound would classify a
    // value as Failure while calling it better than the warning.
    QualityThresholds higher;
    higher.limits[QualityMetric::TetRadiusRatio] =
        QualityThreshold{.warning = 0.2, .failure = 0.5};
    const Result<void> first = meshing::validate(higher);
    REQUIRE_FALSE(first.has_value());
    CHECK_THAT(first.error().message, ContainsSubstring("higher-is-better"));

    // Lower-is-better: the reverse.
    QualityThresholds lower;
    lower.limits[QualityMetric::TetAspectRatio] = QualityThreshold{.warning = 8.0, .failure = 4.0};
    const Result<void> second = meshing::validate(lower);
    REQUIRE_FALSE(second.has_value());
    CHECK_THAT(second.error().message, ContainsSubstring("lower-is-better"));

    // And the right way round is accepted in both directions.
    CHECK(meshing::validate(policy(QualityMetric::TetRadiusRatio, 0.5, 0.2)).has_value());
    CHECK(meshing::validate(policy(QualityMetric::TetAspectRatio, 4.0, 8.0)).has_value());
}

TEST_CASE("QualityPolicy_AcceptsAPolicyWithOnlyOneBound", "[meshing][quality][policy]") {
    CHECK(meshing::validate(policy(QualityMetric::TetRadiusRatio, 0.3, std::nullopt)).has_value());
    CHECK(meshing::validate(policy(QualityMetric::TetRadiusRatio, std::nullopt, 0.1)).has_value());
    CHECK(meshing::validate(QualityThresholds{}).has_value());

    // A failure bound alone classifies, and reports no warnings.
    const MeshQualityReport report = meshing::evaluateMeshQuality(
        tetMesh(wedgeTet(0.01, 0.0002)),
        policy(QualityMetric::TetRadiusRatio, std::nullopt, 0.1));
    CHECK(report.failureElements == 1);
    CHECK(report.warningElements == 0);
}

TEST_CASE("QualityPolicy_AContradictoryPolicyIsReportedAndClassifiesNothing",
          "[meshing][quality][policy]") {
    // A BOUND ON A DIMENSIONED SIZE NAMES A METRIC NOTHING CLASSIFIES, so an
    // unchecked policy would be partly honoured and partly ignored without
    // saying so. evaluateMeshQuality validates the policy itself.
    QualityThresholds contradictory;
    contradictory.limits[QualityMetric::TetVolume] = QualityThreshold{.warning = 1.0};
    contradictory.limits[QualityMetric::TetRadiusRatio] = QualityThreshold{.failure = 0.9};
    REQUIRE_FALSE(meshing::validate(contradictory).has_value());

    const Mesh mesh = tetMesh(wedgeTet(0.01, 0.0002));
    const MeshQualityReport report = meshing::evaluateMeshQuality(mesh, contradictory);

    REQUIRE(report.thresholdPolicyError.has_value());
    CHECK(report.thresholdPolicyError->code == ErrorCode::InvalidArgument);
    CHECK_THAT(report.thresholdPolicyError->message, ContainsSubstring("tet_volume"));
    // NOTHING CLASSIFIED -- not even the radius-ratio bound, which on its own
    // would have been usable. Half a policy is not a policy.
    CHECK(report.failureElements == 0);
    CHECK(report.warningElements == 0);
    CHECK(report.findings.empty());
    // AND NOT A PASS. The verdict was never reached, and reporting one nobody
    // can justify is worse than reporting that the question went unanswered.
    CHECK_FALSE(report.satisfiesPolicy());
    // The metrics are all still there: a bad policy cannot change a number.
    REQUIRE(report.tets.size() == 1);
    CHECK(report.tets.front() == tetQualityOf(mesh));
    CHECK(report.summaries.at(QualityMetric::TetRadiusRatio).count == 1);
    // And the policy as given is recorded, so a reader can see what was asked.
    CHECK(report.thresholds == contradictory);
}

TEST_CASE("QualityPolicy_IsRecordedInTheReportItProduced", "[meshing][quality][policy]") {
    const QualityThresholds thresholds = policy(QualityMetric::TetRadiusRatio, 0.5, 0.1);
    const MeshQualityReport report =
        meshing::evaluateMeshQuality(tetMesh(regularTet(0.01)), thresholds);
    // So that a reader of a report knows what its verdict means.
    CHECK(report.thresholds == thresholds);
}

// ---------------------------------------------------------------------------
// The mesh report
// ---------------------------------------------------------------------------

TEST_CASE("QualityReport_NamesTheWorstElementSeparatelyForEachMetric",
          "[meshing][quality][report]") {
    // THE REASON THERE IS NO OVERALL SCORE. Two elements, each worse than the
    // other by a different metric:
    //
    //   corner tetrahedron   aspect 1.41, max dihedral 90.00 deg
    //   spike (h = 5a)       aspect 5.03, max dihedral 86.70 deg
    //
    // so the worst aspect ratio is the spike and the worst maximum dihedral is
    // the corner. A single weighted ranking would have to prefer one of these
    // answers, and no weights justify that choice.
    MeshBuilder builder;
    for (const Point3D& corner : cornerTet(0.01)) {
        REQUIRE(builder.addNode(corner).has_value());
    }
    for (const Point3D& corner : translated(wedgeTet(0.01, 0.05), 1.0, 0.0, 0.0)) {
        REQUIRE(builder.addNode(corner).has_value());
    }
    const ElementId cornerId =
        require(builder.addTetrahedron({n(1), n(2), n(3), n(4)}, kRegion));
    const ElementId spikeId =
        require(builder.addTetrahedron({n(5), n(6), n(7), n(8)}, kRegion));
    const MeshQualityReport report = meshing::evaluateMeshQuality(builder.build());

    REQUIRE(report.tets.size() == 2);
    CHECK(report.summaries.at(QualityMetric::TetAspectRatio).worst == spikeId);
    CHECK(report.summaries.at(QualityMetric::TetMaxDihedralAngle).worst == cornerId);
    CHECK(report.summaries.at(QualityMetric::TetMinDihedralAngle).worst == cornerId);
    CHECK(report.summaries.at(QualityMetric::TetRadiusRatio).worst == spikeId);

    // A dimensioned size has NO worst element, and says so with an invalid
    // handle rather than naming an arbitrary one.
    CHECK_FALSE(report.summaries.at(QualityMetric::TetVolume).worst.isValid());
    CHECK(report.summaries.at(QualityMetric::TetVolume).count == 2);
}

TEST_CASE("QualityReport_SummarisesEachMetricOverTheElementsItAppliesTo",
          "[meshing][quality][report]") {
    MeshBuilder builder;
    for (const Point3D& corner : regularTet(0.01)) {
        REQUIRE(builder.addNode(corner).has_value());
    }
    for (const Point3D& corner : translated(cornerTet(0.01), 1.0, 0.0, 0.0)) {
        REQUIRE(builder.addNode(corner).has_value());
    }
    REQUIRE(builder.addTetrahedron({n(1), n(2), n(3), n(4)}, kRegion).has_value());
    REQUIRE(builder.addTetrahedron({n(5), n(6), n(7), n(8)}, kRegion).has_value());
    const MeshQualityReport report = meshing::evaluateMeshQuality(builder.build());

    const auto& aspect = report.summaries.at(QualityMetric::TetAspectRatio);
    CHECK(aspect.count == 2);
    CHECK_THAT(aspect.minimum, WithinRel(1.0, kAlgebraic));
    CHECK_THAT(aspect.maximum, WithinRel(std::sqrt(2.0), kAlgebraic));
    // The mean is over ELEMENTS: (1 + sqrt(2))/2.
    CHECK_THAT(aspect.mean, WithinRel((1.0 + std::sqrt(2.0)) / 2.0, kAlgebraic));

    // Only tet metrics appear: a mesh with no triangles gets no triangle
    // summaries, rather than summaries over nothing.
    CHECK_FALSE(report.summaries.contains(QualityMetric::TriangleShapeQuality));
    CHECK(report.triangleCount == 0);
}

TEST_CASE("QualityReport_CountsAnInvalidElementAsInvalidAndGivesItNoMetrics",
          "[meshing][quality][report]") {
    // One good tetrahedron and one mirrored one. The report must not average the
    // two into something acceptable.
    MeshBuilder builder;
    for (const Point3D& corner : regularTet(0.01)) {
        REQUIRE(builder.addNode(corner).has_value());
    }
    for (const Point3D& corner : translated(mirrored(regularTet(0.01)), 1.0, 0.0, 0.0)) {
        REQUIRE(builder.addNode(corner).has_value());
    }
    REQUIRE(builder.addTetrahedron({n(1), n(2), n(3), n(4)}, kRegion).has_value());
    const ElementId inverted =
        require(builder.addTetrahedron({n(5), n(6), n(7), n(8)}, kRegion));
    const MeshQualityReport report = meshing::evaluateMeshQuality(builder.build());

    CHECK(report.tetCount == 2);
    CHECK(report.validElements == 1);
    CHECK(report.invalidElements == 1);
    // NO METRICS for the invalid element: one entry, not two.
    REQUIRE(report.tets.size() == 1);
    CHECK(report.tets.front().element != inverted);
    // Structure is reported as structure, and the whole mesh does not satisfy
    // any policy.
    CHECK_FALSE(report.structurallyValid);
    CHECK_FALSE(report.satisfiesPolicy());
    const bool sawInversion =
        std::ranges::any_of(report.structural.issues, [](const meshing::MeshIssue& issue) {
            return issue.kind == MeshIssueKind::InvertedTetrahedron;
        });
    CHECK(sawInversion);
    // And the finding carries the element and the reason.
    REQUIRE(report.findings.size() == 1);
    CHECK(report.findings.front().classification == QualityClass::Invalid);
    CHECK(report.findings.front().element == inverted);
    CHECK_FALSE(report.findings.front().threshold.has_value());
    // NO METRIC AND NO VALUE. Nothing was computed for this element, and
    // naming TetVolume with a value of 0 for an element whose volume is
    // NEGATIVE would be fabricated structured data that a consumer reading the
    // payload rather than the message would believe.
    CHECK_FALSE(report.findings.front().metric.has_value());
    CHECK_FALSE(report.findings.front().value.has_value());
    CHECK_THAT(report.findings.front().message, ContainsSubstring("inverted"));
}

TEST_CASE("QualityReport_DoesNotCallABadlyShapedElementInvalid", "[meshing][quality][report]") {
    // The other half of the boundary. A 2 deg sliver is a valid discretisation
    // that will solve badly, and the report must say exactly that.
    const MeshQualityReport report = meshing::evaluateMeshQuality(tetMesh(wedgeTet(0.01, 0.0001)));
    CHECK(report.structurallyValid);
    CHECK(report.invalidElements == 0);
    CHECK(report.validElements == 1);
    REQUIRE(report.tets.size() == 1);
    CHECK(report.tets.front().defined);
    CHECK(report.tets.front().minDihedral.si() < 0.04); // ~1.98 deg
}

TEST_CASE("QualityReport_AnEmptyMeshIsStructurallyInvalid", "[meshing][quality][report]") {
    // It cannot pass for want of bad elements.
    const MeshQualityReport report = meshing::evaluateMeshQuality(MeshBuilder{}.build());
    CHECK_FALSE(report.structurallyValid);
    CHECK_FALSE(report.satisfiesPolicy());
    CHECK(report.tetCount == 0);
    CHECK(report.triangleCount == 0);
    CHECK(report.summaries.empty());
    const bool sawEmpty =
        std::ranges::any_of(report.structural.issues, [](const meshing::MeshIssue& issue) {
            return issue.kind == MeshIssueKind::EmptyMesh;
        });
    CHECK(sawEmpty);
}

TEST_CASE("QualityReport_ReportsAStructuralDefectThatIsNotAboutOneElement",
          "[meshing][quality][report]") {
    // Two regions sharing a node: structurally invalid (ADR-032), and every
    // element individually fine. The report must not call the mesh acceptable
    // because its per-element metrics are.
    MeshBuilder builder;
    for (const Point3D& corner : cornerTet(0.01)) {
        REQUIRE(builder.addNode(corner).has_value());
    }
    REQUIRE(builder.addNode(si(-0.01, -0.01, -0.01)).has_value());
    REQUIRE(builder.addTetrahedron({n(1), n(2), n(3), n(4)}, kRegion).has_value());
    // Wound for a positive signed volume: 1/6 (p2-p5).((p3-p5)x(p1-p5)) > 0.
    REQUIRE(builder.addTetrahedron({n(5), n(2), n(3), n(1)}, kOtherRegion).has_value());

    const MeshQualityReport report = meshing::evaluateMeshQuality(builder.build());
    CHECK_FALSE(report.structurallyValid);
    CHECK_FALSE(report.satisfiesPolicy());
    // Both elements still measured: the structural defect is about the mesh, so
    // it does not make either element's metrics undefined.
    CHECK(report.tets.size() == 2);
    CHECK(report.validElements == 2);
}

TEST_CASE("QualityReport_OrdersFindingsMostSevereFirst", "[meshing][quality][report]") {
    // Three elements under one policy: a failure, a warning and a clean one.
    MeshBuilder builder;
    for (const Point3D& corner : regularTet(0.01)) {
        REQUIRE(builder.addNode(corner).has_value());
    }
    for (const Point3D& corner : translated(cornerTet(0.01), 1.0, 0.0, 0.0)) {
        REQUIRE(builder.addNode(corner).has_value());
    }
    for (const Point3D& corner : translated(wedgeTet(0.01, 0.0002), 2.0, 0.0, 0.0)) {
        REQUIRE(builder.addNode(corner).has_value());
    }
    REQUIRE(builder.addTetrahedron({n(1), n(2), n(3), n(4)}, kRegion).has_value());
    REQUIRE(builder.addTetrahedron({n(5), n(6), n(7), n(8)}, kRegion).has_value());
    REQUIRE(builder.addTetrahedron({n(9), n(10), n(11), n(12)}, kRegion).has_value());

    // Radius ratio: regular 1.0, corner 0.732, sliver 0.0036.
    const MeshQualityReport report = meshing::evaluateMeshQuality(
        builder.build(), policy(QualityMetric::TetRadiusRatio, 0.8, 0.1));
    CHECK(report.validElements == 1);
    CHECK(report.warningElements == 1);
    CHECK(report.failureElements == 1);

    const std::vector<QualityClass> classes = classesOf(report);
    REQUIRE(classes.size() == 2);
    CHECK(classes[0] == QualityClass::Failure);
    CHECK(classes[1] == QualityClass::Warning);
    CHECK(std::ranges::is_sorted(classes, [](QualityClass a, QualityClass b) {
        return static_cast<int>(a) > static_cast<int>(b);
    }));
}

TEST_CASE("QualityReport_ReportsOneFindingPerCrossedBoundAndCountsTheElementOnce",
          "[meshing][quality][report]") {
    // Findings are per OBSERVATION; the counts are per ELEMENT. A sliver past
    // two different metrics' bounds is two observations about one element.
    QualityThresholds thresholds;
    thresholds.limits[QualityMetric::TetRadiusRatio] = QualityThreshold{.failure = 0.1};
    thresholds.limits[QualityMetric::TetMaxDihedralAngle] =
        QualityThreshold{.failure = 2.0}; // rad, about 114.6 deg
    REQUIRE(meshing::validate(thresholds).has_value());

    const MeshQualityReport report =
        meshing::evaluateMeshQuality(tetMesh(wedgeTet(0.01, 0.0002)), thresholds);
    CHECK(report.findings.size() == 2);
    CHECK(report.failureElements == 1);
    CHECK(report.validElements == 0);
    // The element is counted at its WORST, once, and the classification counts
    // sum to the element count.
    CHECK(report.validElements + report.warningElements + report.failureElements +
              report.invalidElements ==
          report.tetCount + report.triangleCount);
}

TEST_CASE("QualityReport_ClassificationCountsSumToTheElementCount",
          "[meshing][quality][report]") {
    // Asserted over a production mesh as well as a synthetic one, because the
    // bookkeeping is what makes every other count in the report trustworthy.
    Round cylinder{6_mm, 20_mm};
    const VolumeMesh mesh = cylinder.require_(curvedTarget(6_mm));
    const MeshQualityReport report = meshing::evaluateMeshQuality(
        mesh.mesh(), policy(QualityMetric::TetRadiusRatio, 0.3, 0.05));
    CHECK(report.validElements + report.warningElements + report.failureElements +
              report.invalidElements ==
          report.tetCount + report.triangleCount);
    CHECK(report.tetCount + report.triangleCount > 0);
    // A real mesh under a policy somebody chose: some elements fail it. The
    // NUMBER is not asserted -- no requirement exists -- but that the
    // machinery distinguishes them is.
    CHECK(report.warningElements + report.failureElements > 0);
    CHECK(report.invalidElements == 0);
}

TEST_CASE("QualityReport_IsIdenticalOnRepeatedEvaluation", "[meshing][quality][determinism]") {
    MeshBuilder builder;
    for (const Point3D& corner : regularTet(0.01)) {
        REQUIRE(builder.addNode(corner).has_value());
    }
    for (const Point3D& corner : translated(wedgeTet(0.01, 0.0002), 1.0, 0.0, 0.0)) {
        REQUIRE(builder.addNode(corner).has_value());
    }
    REQUIRE(builder.addTetrahedron({n(1), n(2), n(3), n(4)}, kRegion).has_value());
    REQUIRE(builder.addTetrahedron({n(5), n(6), n(7), n(8)}, kRegion).has_value());
    REQUIRE(builder.addTriangle({n(1), n(2), n(3)}, kRegion).has_value());
    const Mesh mesh = builder.build();
    const QualityThresholds thresholds = policy(QualityMetric::TetRadiusRatio, 0.8, 0.1);

    // THE WHOLE REPORT, compared as a value: per-element metrics, summaries,
    // findings, their order and the recorded policy.
    const MeshQualityReport first = meshing::evaluateMeshQuality(mesh, thresholds);
    for (int run = 0; run < 5; ++run) {
        CHECK(meshing::evaluateMeshQuality(mesh, thresholds) == first);
    }
}

TEST_CASE("QualityReport_LeavesTheMeshExactlyAsItFoundIt", "[meshing][quality][readonly]") {
    // P16-QUALITY OBSERVES. It does not move a node, reorder connectivity or
    // drop an element, and nothing it returns aliases the mesh.
    MeshBuilder builder;
    for (const Point3D& corner : regularTet(0.01)) {
        REQUIRE(builder.addNode(corner).has_value());
    }
    for (const Point3D& corner : translated(mirrored(regularTet(0.01)), 1.0, 0.0, 0.0)) {
        REQUIRE(builder.addNode(corner).has_value());
    }
    REQUIRE(builder.addTetrahedron({n(1), n(2), n(3), n(4)}, kRegion).has_value());
    REQUIRE(builder.addTetrahedron({n(5), n(6), n(7), n(8)}, kRegion).has_value());
    const Mesh mesh = builder.build();
    const Mesh before = mesh;

    MeshQualityReport report = meshing::evaluateMeshQuality(
        mesh, policy(QualityMetric::TetRadiusRatio, 0.9, 0.5));
    // Including the inverted element, which an optimiser would be tempted to
    // repair: the mesh still holds it afterwards.
    CHECK(mesh == before);
    // Mutating the report cannot reach back into the mesh.
    report.tets.clear();
    report.findings.clear();
    CHECK(mesh == before);
    CHECK(mesh.tetrahedra().size() == 2);
}

// ---------------------------------------------------------------------------
// Production meshes: the metrics over geometry the pipeline really produces
// ---------------------------------------------------------------------------

TEST_CASE("QualityProduction_BlockMeshHasNoInvalidElementsAndEveryMetricInRange",
          "[meshing][quality][production]") {
    Block block{40_mm, 40_mm, 40_mm};
    const VolumeMesh mesh = block.require_();
    const MeshQualityReport report = meshing::evaluateMeshQuality(mesh.mesh());

    CHECK(report.structurallyValid);
    CHECK(report.tetCount > 0);
    CHECK(report.invalidElements == 0);
    CHECK(report.validElements == report.tetCount + report.triangleCount);
    CHECK(report.satisfiesPolicy());
    checkMetricsAreInRange(report);
    reportShape("block 40x40x40 mm, default sizing", report);
}

TEST_CASE("QualityProduction_BlockMeshMatchesTheClosedFormForItsOwnTetrahedra",
          "[meshing][quality][production][analytical]") {
    // INDEPENDENT VALIDATION ON THE PRODUCTION PATH, which the synthetic
    // reference tetrahedra cannot give: these metrics are computed over a mesh
    // the real CAD -> surface -> Netgen pipeline produced, and compared against
    // algebra derived by hand from the geometry that pipeline must produce.
    //
    // A cube of side a has 6 planar faces, each triangulated by OCCT into two
    // right isoceles triangles, and Netgen joins each to the cube's centre: 12
    // congruent tetrahedra on 9 nodes. One of them is
    //
    //     (0,0,0)  (a,a,0)  (a,0,0)  (a/2,a/2,a/2)
    //
    // whose closed forms are
    //
    //     V       = a^3/12                     (and 12V = a^3, the cube)
    //     edges   = a, a, a sqrt(2), and three of a sqrt(3)/2
    //     aspect  = a sqrt(2) / (a sqrt(3)/2) = sqrt(8/3)
    //     A       = a^2/2 + 3 a^2 sqrt(2)/4 = a^2 (2 + 3 sqrt(2))/4
    //     r       = 3V/A = a/(2 + 3 sqrt(2))
    //     R       = 3a/4                       (the circumcentre is OUTSIDE it,
    //                                           at z = -a/4, which is ordinary
    //                                           for an obtuse tetrahedron and is
    //                                           why R is not derived from a face)
    //     3r/R    = 4/(2 + 3 sqrt(2)) = 0.6407544820340816
    //     dihedrals: pi/4 and 2pi/3, nothing between
    //
    // EVERY tetrahedron is held to this, not just the worst: 12 congruent
    // elements means min and max coincide, and an implementation that was right
    // on average would fail.
    Block block{40_mm, 40_mm, 40_mm};
    const VolumeMesh mesh = block.require_();
    const MeshQualityReport report = meshing::evaluateMeshQuality(mesh.mesh());

    // A TOLERANCE SET BY THE MESH, NOT BY THE ARITHMETIC, and measured rather
    // than guessed. The eight cube corners come through exactly, but Netgen
    // places the ninth node -- the interior one -- about 6e-11 m from the
    // geometric centre of a 40 mm cube, which is 1.7e-9 relative on the short
    // edges and, after the circumcentre solve, 4.5e-9 on the radius ratio and
    // 3e-9 rad on the dihedrals. Measured figures, from this mesh.
    //
    // So this comparison is held at 1e-7 relative, which is 4e-9 m on this body.
    // It is not a loosened version of the synthetic references' 1e-12 -- those
    // have exact coordinates and keep it. What this test exists to catch is a
    // WRONG FORMULA on the production path, and a wrong formula is out by order
    // one: reporting the supplement instead of the dihedral is 0.68 rad, seven
    // orders of magnitude outside this bound.
    constexpr double kMeshed = 1e-7;
    constexpr double a = 0.04;
    REQUIRE(report.tetCount == 12);
    REQUIRE(report.tets.size() == 12);
    for (const TetQuality& tet : report.tets) {
        CHECK_THAT(tet.volume.si(), WithinRel(a * a * a / 12.0, kMeshed));
        CHECK_THAT(tet.minEdge.si(), WithinRel(a * std::sqrt(3.0) / 2.0, kMeshed));
        CHECK_THAT(tet.maxEdge.si(), WithinRel(a * std::sqrt(2.0), kMeshed));
        CHECK_THAT(tet.meanEdge.si(),
                   WithinRel(a * (2.0 + std::sqrt(2.0) + 3.0 * std::sqrt(3.0) / 2.0) / 6.0,
                             kMeshed));
        CHECK_THAT(tet.aspectRatio, WithinRel(std::sqrt(8.0 / 3.0), kMeshed));
        CHECK_THAT(tet.inradius.si(), WithinRel(a / (2.0 + 3.0 * std::sqrt(2.0)), kMeshed));
        CHECK_THAT(tet.circumradius.si(), WithinRel(3.0 * a / 4.0, kMeshed));
        CHECK_THAT(tet.radiusRatio, WithinRel(4.0 / (2.0 + 3.0 * std::sqrt(2.0)), kMeshed));
        CHECK_THAT(tet.minDihedral.si(), WithinAbs(std::numbers::pi / 4.0, kMeshed));
        CHECK_THAT(tet.maxDihedral.si(), WithinAbs(2.0 * std::numbers::pi / 3.0, kMeshed));
    }
    // And the twelve volumes sum to the cube, which is the arithmetic that makes
    // "congruent tetrahedra of the cube" the right decomposition and not merely
    // a plausible one.
    double total = 0.0;
    for (const TetQuality& tet : report.tets) {
        total += tet.volume.si();
    }
    CHECK_THAT(total, WithinRel(a * a * a, kMeshed));
}

TEST_CASE("QualityProduction_CylinderMeshHasNoInvalidElementsAndEveryMetricInRange",
          "[meshing][quality][production]") {
    Round cylinder{6_mm, 20_mm};
    const VolumeMesh mesh = cylinder.require_(curvedTarget(3_mm));
    const MeshQualityReport report = meshing::evaluateMeshQuality(mesh.mesh());

    CHECK(report.structurallyValid);
    CHECK(report.tetCount > 100);
    CHECK(report.invalidElements == 0);
    checkMetricsAreInRange(report);
    // The characteristic shape of a real mesh, reported rather than gated: the
    // worst radius ratio on this body is a number nobody has set a limit for,
    // and inventing one here is exactly what the brief forbids.
    CHECK(report.summaries.at(QualityMetric::TetRadiusRatio).worst.isValid());
    CHECK(report.summaries.at(QualityMetric::TetRadiusRatio).minimum > 0.0);
    CHECK(report.summaries.at(QualityMetric::TetRadiusRatio).mean >
          report.summaries.at(QualityMetric::TetRadiusRatio).minimum);
    reportShape("cylinder r6 h20 mm, 3 mm target", report);
}

TEST_CASE("QualityProduction_HollowTubeMeshHasNoInvalidElements",
          "[meshing][quality][production]") {
    Round tube{10_mm, 20_mm, 6_mm};
    const VolumeMesh mesh = tube.require_(curvedTarget(3_mm));
    const MeshQualityReport report = meshing::evaluateMeshQuality(mesh.mesh());

    CHECK(report.structurallyValid);
    CHECK(report.tetCount > 100);
    CHECK(report.invalidElements == 0);
    checkMetricsAreInRange(report);
    reportShape("hollow tube r10/r6 h20 mm, 3 mm target", report);
}

TEST_CASE("QualityProduction_BlockWithAThroughHoleHasNoInvalidElements",
          "[meshing][quality][production]") {
    Bored bored;
    const VolumeMesh mesh = bored.require_(curvedTarget(8_mm));
    const MeshQualityReport report = meshing::evaluateMeshQuality(mesh.mesh());

    CHECK(report.structurallyValid);
    CHECK(report.tetCount > 50);
    CHECK(report.invalidElements == 0);
    checkMetricsAreInRange(report);
    reportShape("block 60x60x20 mm bored 20 mm, 8 mm target", report);
}

TEST_CASE("QualityProduction_LocalRefinementIntroducesNoInvalidElements",
          "[meshing][quality][production]") {
    // Refinement puts elements of two sizes next to each other, which is where
    // a mesher is most likely to produce a bad transition element. The report
    // records what happened; it does not decide that the transition is
    // unacceptable.
    Round cylinder{6_mm, 20_mm};
    VolumeMeshControls controls = curvedTarget(6_mm);
    controls.sizing.local.push_back(
        meshing::LocalMeshSizing{.face = cylinder.top(), .targetSize = 1.5_mm});
    const VolumeMesh mesh = cylinder.require_(controls);
    const MeshQualityReport report = meshing::evaluateMeshQuality(mesh.mesh());

    CHECK(report.structurallyValid);
    CHECK(report.invalidElements == 0);
    checkMetricsAreInRange(report);
    // Element sizes really do span a range here, so the size summaries are not
    // describing a uniform mesh.
    const auto& edges = report.summaries.at(QualityMetric::TetMeanEdgeLength);
    CHECK(edges.maximum > edges.minimum * 2.0);
    reportShape("cylinder r6 h20 mm, global 6 mm with 1.5 mm on the top disc", report);
}

TEST_CASE("QualityProduction_SurfaceMeshTrianglesAreEvaluated",
          "[meshing][quality][production]") {
    Round cylinder{6_mm, 20_mm};
    meshing::SurfaceMeshControls controls;
    controls.linearDeflection = Length::fromSi(5e-5);
    const Result<meshing::EngineeringSurfaceMesh> surface = meshing::surfaceMeshFor(
        cylinder.document, cylinder.regenerator, cylinder.feature, controls);
    REQUIRE(surface.has_value());

    const MeshQualityReport report = meshing::evaluateMeshQuality(surface->mesh);
    CHECK(report.structurallyValid);
    CHECK(report.tetCount == 0);
    CHECK(report.triangleCount > 0);
    CHECK(report.invalidElements == 0);
    checkMetricsAreInRange(report);
    // A surface mesh gets triangle summaries and no tet summaries.
    CHECK(report.summaries.contains(QualityMetric::TriangleShapeQuality));
    CHECK_FALSE(report.summaries.contains(QualityMetric::TetRadiusRatio));
    reportShape("cylinder r6 h20 mm SURFACE, 0.05 mm deflection", report);
}

TEST_CASE("QualityProduction_IsIdenticalOnRepeatedEvaluationOfARealMesh",
          "[meshing][quality][determinism][production]") {
    Block block{40_mm, 40_mm, 40_mm};
    const VolumeMesh mesh = block.require_();
    const MeshQualityReport first = meshing::evaluateMeshQuality(mesh.mesh());
    for (int run = 0; run < 3; ++run) {
        CHECK(meshing::evaluateMeshQuality(mesh.mesh()) == first);
    }
}
