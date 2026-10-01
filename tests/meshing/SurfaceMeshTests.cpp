// P16-SURF-001: the engineering surface mesh.
//
// Every expected area and volume here is closed-form and evaluated by hand. None
// was obtained by running BetterCAD and recording what it said.
#include "features/FeatureTestSupport.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Mesh.hpp>
#include <bettercad/core/math/Vector.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/meshing/SurfaceMesh.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <numbers>
#include <set>
#include <tuple>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using meshing::ElementId;
using meshing::EngineeringSurfaceMesh;
using meshing::MeshableGeometry;
using meshing::Node;
using meshing::NodeId;
using meshing::RegionId;
using meshing::SurfaceMeshControls;
using meshing::SurfaceValidation;
using meshing::surfaceMeshFor;
using meshing::Triangle;

namespace {

/// Planar geometry is exact through the kernel, so a box's area and volume may be
/// held to the repository's figure for geometric accumulation.
constexpr double kExact = 1e-9;

/// One extruded solid, with the profile reachable so a test can make it stale.
struct Surfaced {
    Document document{"Part"};
    features::Regenerator regenerator;
    ObjectId feature{};
    ObjectId profile{};
    std::array<EntityId, 4> lines{};

    Surfaced(Length a, Length b, Length c, const Frame3D& plane = Frame3D::xy()) {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", plane);
        lines = addRectangle(*sketch, 0_mm, 0_mm, a, b);
        profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = c});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));
        requireReport(regenerator, document);
    }

    [[nodiscard]] Result<EngineeringSurfaceMesh> surface(const SurfaceMeshControls& controls = {}) const {
        return surfaceMeshFor(document, regenerator, feature, controls);
    }

    [[nodiscard]] EngineeringSurfaceMesh require_() const {
        const Result<EngineeringSurfaceMesh> s = surface();
        REQUIRE(s.has_value());
        return *s;
    }
};

/// A cylinder from a circular profile.
struct SurfacedCylinder {
    Document document{"Cyl"};
    features::Regenerator regenerator;
    ObjectId feature{};

    SurfacedCylinder(Length radius, Length height) {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        REQUIRE(sketch->addCircle(Point2D{0_mm, 0_mm}, radius).has_value());
        const ObjectId profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = height});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));
        requireReport(regenerator, document);
    }

    [[nodiscard]] Result<EngineeringSurfaceMesh> surface(const SurfaceMeshControls& controls = {}) const {
        return surfaceMeshFor(document, regenerator, feature, controls);
    }
};

/// A hollow tube: two concentric circles extruded. Its inner wall is the
/// orientation test that matters, because "outward from the material" there points
/// TOWARD the axis.
struct SurfacedTube {
    Document document{"Tube"};
    features::Regenerator regenerator;
    ObjectId feature{};

    SurfacedTube(Length outer, Length inner, Length height) {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        REQUIRE(sketch->addCircle(Point2D{0_mm, 0_mm}, outer).has_value());
        REQUIRE(sketch->addCircle(Point2D{0_mm, 0_mm}, inner).has_value());
        const ObjectId profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = height});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));
        requireReport(regenerator, document);
    }

    [[nodiscard]] Result<EngineeringSurfaceMesh> surface(const SurfaceMeshControls& controls = {}) const {
        return surfaceMeshFor(document, regenerator, feature, controls);
    }
};

/// Compares two meshes by CONTENT: handles, positions, oriented connectivity and
/// regions. Deliberately NOT `operator==`, which also compares the MeshStamp --
/// and a MeshId is unique per mesh by design (ADR-031), so two independently
/// generated meshes of one body are never equal however identical their content.
void checkSameContent(const meshing::Mesh& actual, const meshing::Mesh& expected) {
    REQUIRE(actual.nodeCount() == expected.nodeCount());
    REQUIRE(actual.triangles().size() == expected.triangles().size());
    REQUIRE(actual.tetrahedra().size() == expected.tetrahedra().size());
    for (std::size_t i = 0; i < expected.nodes().size(); ++i) {
        CHECK(actual.nodes()[i].id == expected.nodes()[i].id);
        // Exact: the same arithmetic on the same shape must not drift.
        CHECK(actual.nodes()[i].position == expected.nodes()[i].position);
    }
    for (std::size_t i = 0; i < expected.triangles().size(); ++i) {
        CHECK(actual.triangles()[i].id == expected.triangles()[i].id);
        CHECK(actual.triangles()[i].nodes == expected.triangles()[i].nodes);
        CHECK(actual.triangles()[i].region == expected.triangles()[i].region);
    }
    CHECK(actual.bounds() == expected.bounds());
}

[[nodiscard]] double mm2(Area a) { return a.in(units::mm2); }
[[nodiscard]] double mm3(Volume v) { return v.in(units::mm3); }

/// Unit normal of a triangle, from the stored winding. Test-side, because a
/// normal is derived state and the mesh does not store one.
struct Normal {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

[[nodiscard]] Normal normalOf(const meshing::Mesh& mesh, const Triangle& t) {
    const Node* a = mesh.findNode(t.nodes[0]);
    const Node* b = mesh.findNode(t.nodes[1]);
    const Node* c = mesh.findNode(t.nodes[2]);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(c != nullptr);
    const double e1x = b->position.x.si() - a->position.x.si();
    const double e1y = b->position.y.si() - a->position.y.si();
    const double e1z = b->position.z.si() - a->position.z.si();
    const double e2x = c->position.x.si() - a->position.x.si();
    const double e2y = c->position.y.si() - a->position.y.si();
    const double e2z = c->position.z.si() - a->position.z.si();
    Normal n{e1y * e2z - e1z * e2y, e1z * e2x - e1x * e2z, e1x * e2y - e1y * e2x};
    const double length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    REQUIRE(length > 0.0);
    return Normal{n.x / length, n.y / length, n.z / length};
}

/// Centroid of a triangle, in metres.
[[nodiscard]] Normal centroidOf(const meshing::Mesh& mesh, const Triangle& t) {
    Normal c{};
    for (const NodeId id : t.nodes) {
        const Node* n = mesh.findNode(id);
        REQUIRE(n != nullptr);
        c.x += n->position.x.si() / 3.0;
        c.y += n->position.y.si() / 3.0;
        c.z += n->position.z.si() / 3.0;
    }
    return c;
}

} // namespace

// ---------------------------------------------------------------------------
// SURF-BOX
// ---------------------------------------------------------------------------

TEST_CASE("SurfBox_IsWatertightManifoldAndCoherentlyOriented", "[meshing][surf][box]") {
    // Asymmetric on purpose: a != b != c, so an axis mix-up cannot pass.
    const Surfaced part{20_mm, 30_mm, 50_mm};
    const EngineeringSurfaceMesh surface = part.require_();

    CHECK(surface.validation.boundaryEdgeCount == 0);
    CHECK(surface.validation.nonManifoldEdgeCount == 0);
    CHECK(surface.validation.orientationConflictCount == 0);
    CHECK(surface.validation.degenerateTriangleCount == 0);
    CHECK(surface.validation.duplicateTriangleCount == 0);
    CHECK(surface.validation.unusedNodeCount == 0);
    CHECK(surface.validation.watertight());
    CHECK(surface.validation.valid());

    // A box has 8 corners, and welding must find exactly those: the raw
    // triangulation has 24 vertices, three per corner.
    CHECK(surface.mesh.nodeCount() == 8);
    CHECK(surface.mesh.triangles().size() == 12); // two per face
    CHECK(surface.faceCount == 6);
}

TEST_CASE("SurfBox_AreaMatchesTheAnalyticalSurfaceArea", "[meshing][surf][box][area]") {
    // A = 2(ab + ac + bc) = 2(20*30 + 20*50 + 30*50) = 2(600 + 1000 + 1500) = 6200 mm^2
    const Surfaced part{20_mm, 30_mm, 50_mm};
    const EngineeringSurfaceMesh surface = part.require_();
    CHECK_THAT(mm2(surface.area), WithinRel(2.0 * (20.0 * 30.0 + 20.0 * 50.0 + 30.0 * 50.0), kExact));

    // And it agrees with the kernel's own integration, which is the authority.
    const Result<MeshableGeometry> prepared =
        meshing::requireMeshableGeometry(part.document, part.regenerator, part.feature);
    REQUIRE(prepared.has_value());
    const Result<geometry::MassProperties> cad = prepared->body.massProperties();
    REQUIRE(cad.has_value());
    CHECK_THAT(mm2(surface.area), WithinRel(cad->surfaceArea.in(units::mm2), kExact));
}

TEST_CASE("SurfBox_EnclosedVolumeFromTheTrianglesMatchesTheCadVolume",
          "[meshing][surf][box][volume]") {
    // The orientation cross-check: 1/6 sum dot(a, cross(b,c)) over outward
    // triangles is the enclosed volume, POSITIVE. V = 20*30*50 = 30000 mm^3.
    const Surfaced part{20_mm, 30_mm, 50_mm};
    const EngineeringSurfaceMesh surface = part.require_();

    CHECK(mm3(surface.enclosedVolume) > 0.0);
    CHECK_THAT(mm3(surface.enclosedVolume), WithinRel(30000.0, kExact));

    const Result<MeshableGeometry> prepared =
        meshing::requireMeshableGeometry(part.document, part.regenerator, part.feature);
    REQUIRE(prepared.has_value());
    CHECK_THAT(mm3(surface.enclosedVolume), WithinRel(mm3(prepared->volume), kExact));
}

TEST_CASE("SurfBox_EulerCharacteristicIsTwo", "[meshing][surf][box][topology]") {
    // An independent topological cross-check for a genus-0 closed surface:
    // chi = V - E + F = 2. For a box: 8 - 18 + 12 = 2.
    const Surfaced part{20_mm, 30_mm, 50_mm};
    const EngineeringSurfaceMesh surface = part.require_();

    std::set<std::pair<NodeId::ValueType, NodeId::ValueType>> edges;
    for (const Triangle& t : surface.mesh.triangles()) {
        for (std::size_t i = 0; i < 3; ++i) {
            const auto u = t.nodes[i].value();
            const auto v = t.nodes[(i + 1) % 3].value();
            edges.insert(u < v ? std::pair{u, v} : std::pair{v, u});
        }
    }
    const auto V = static_cast<long long>(surface.mesh.nodeCount());
    const auto E = static_cast<long long>(edges.size());
    const auto F = static_cast<long long>(surface.mesh.triangles().size());
    CHECK(V == 8);
    CHECK(E == 18);
    CHECK(F == 12);
    CHECK(V - E + F == 2);
}

TEST_CASE("SurfBox_SharpEdgesKeepDistinctFaceNormals", "[meshing][surf][box][normals]") {
    // A box has six planar faces meeting at right angles. Within one CAD face the
    // normals must agree; between adjacent faces they must NOT. This is not a
    // graphics mesh and nothing is smoothed.
    const Surfaced part{20_mm, 30_mm, 50_mm};
    const EngineeringSurfaceMesh surface = part.require_();

    // Six distinct outward directions, one per face, each axis-aligned.
    std::set<std::tuple<long long, long long, long long>> directions;
    for (const Triangle& t : surface.mesh.triangles()) {
        const Normal n = normalOf(surface.mesh, t);
        // Axis-aligned to rounding, so a planar box face cannot hide a tilt.
        CHECK_THAT(std::abs(n.x) + std::abs(n.y) + std::abs(n.z), WithinAbs(1.0, 1e-12));
        directions.insert({std::llround(n.x), std::llround(n.y), std::llround(n.z)});
    }
    CHECK(directions.size() == 6);
}

TEST_CASE("SurfBox_NormalsWithinOnePlanarFaceAgree", "[meshing][surf][box][normals]") {
    // Per-CAD-face coherence, using the face grouping P16-SURF added to the
    // triangulator. A winding inconsistency inside one face would show here even
    // if the global counts happened to work out.
    const Surfaced part{20_mm, 30_mm, 50_mm};
    const Result<MeshableGeometry> prepared =
        meshing::requireMeshableGeometry(part.document, part.regenerator, part.feature);
    REQUIRE(prepared.has_value());
    const Result<geometry::Mesh> raw = geometry::triangulate(prepared->body);
    REQUIRE(raw.has_value());
    REQUIRE(raw->faces.size() == 6);

    for (const geometry::MeshFace& face : raw->faces) {
        REQUIRE(face.triangleCount >= 1);
        Normal first{};
        for (std::size_t i = 0; i < face.triangleCount; ++i) {
            const auto& tri = raw->triangles[face.firstTriangle + i];
            const Point3D& a = raw->vertices[tri[0]];
            const Point3D& b = raw->vertices[tri[1]];
            const Point3D& c = raw->vertices[tri[2]];
            const double e1x = b.x.si() - a.x.si();
            const double e1y = b.y.si() - a.y.si();
            const double e1z = b.z.si() - a.z.si();
            const double e2x = c.x.si() - a.x.si();
            const double e2y = c.y.si() - a.y.si();
            const double e2z = c.z.si() - a.z.si();
            Normal n{e1y * e2z - e1z * e2y, e1z * e2x - e1x * e2z, e1x * e2y - e1y * e2x};
            const double len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
            REQUIRE(len > 0.0);
            n = Normal{n.x / len, n.y / len, n.z / len};
            if (i == 0) {
                first = n;
            } else {
                // Parallel AND same direction: a dot product of +1.
                CHECK_THAT(n.x * first.x + n.y * first.y + n.z * first.z, WithinAbs(1.0, 1e-12));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// SURF-CYLINDER
// ---------------------------------------------------------------------------

TEST_CASE("SurfCylinder_IsWatertightAndItsAreaConvergesOnTheAnalyticalValue",
          "[meshing][surf][cylinder]") {
    // A = 2*pi*r*h + 2*pi*r^2 = 2*pi*12*25 + 2*pi*144, by hand.
    // A triangulated curved surface UNDERSTATES the true area -- chords cut inside
    // the arc -- so this checks convergence from below rather than equality.
    const SurfacedCylinder part{12_mm, 25_mm};
    const double exact = 2.0 * std::numbers::pi * 12.0 * 25.0 + 2.0 * std::numbers::pi * 144.0;

    const SurfaceMeshControls coarse{Length::fromSi(5e-4), Angle::fromSi(0.5)};
    const SurfaceMeshControls fine{Length::fromSi(2e-5), Angle::fromSi(0.05)};
    const Result<EngineeringSurfaceMesh> a = part.surface(coarse);
    const Result<EngineeringSurfaceMesh> b = part.surface(fine);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());

    CHECK(a->validation.watertight());
    CHECK(b->validation.watertight());
    CHECK(a->validation.valid());
    CHECK(b->validation.valid());

    // Finer controls give more triangles and a closer area, from below.
    CHECK(b->mesh.triangles().size() > a->mesh.triangles().size());
    CHECK(mm2(a->area) < exact);
    CHECK(mm2(b->area) < exact);
    CHECK(mm2(b->area) > mm2(a->area));
    // The fine mesh is within a fraction of a percent of the closed form.
    CHECK_THAT(mm2(b->area), WithinRel(exact, 1e-3));
}

TEST_CASE("SurfCylinder_EnclosedVolumeIsPositiveAndApproachesTheAnalyticalVolume",
          "[meshing][surf][cylinder][volume]") {
    // V = pi r^2 h = pi * 144 * 25, by hand. The inscribed triangulation
    // understates it, and refining closes the gap.
    const SurfacedCylinder part{12_mm, 25_mm};
    const double exact = std::numbers::pi * 144.0 * 25.0;

    const Result<EngineeringSurfaceMesh> fine =
        part.surface({Length::fromSi(2e-5), Angle::fromSi(0.05)});
    REQUIRE(fine.has_value());
    CHECK(mm3(fine->enclosedVolume) > 0.0);
    CHECK(mm3(fine->enclosedVolume) < exact);
    CHECK_THAT(mm3(fine->enclosedVolume), WithinRel(exact, 1e-3));
}

TEST_CASE("SurfCylinder_SideNodesLieOnTheExactCylinderAndItsCapsAreFlat",
          "[meshing][surf][cylinder][curved]") {
    // Tessellation NODES lie on the true surface even though triangle interiors
    // cut inside it, so the nodes are what may be checked against the closed form.
    const SurfacedCylinder part{12_mm, 25_mm};
    const Result<EngineeringSurfaceMesh> surface =
        part.surface({Length::fromSi(2e-5), Angle::fromSi(0.05)});
    REQUIRE(surface.has_value());

    std::size_t onWall = 0;
    for (const Node& node : surface->mesh.nodes()) {
        const double x = node.position.x.si();
        const double y = node.position.y.si();
        const double z = node.position.z.si();
        const double r = std::sqrt(x * x + y * y) * 1e3; // mm
        // Every node is either on the wall at r = 12, or on a cap where r <= 12.
        CHECK(r <= 12.0 + 1e-6);
        if (r > 12.0 - 1e-6) {
            ++onWall;
        }
        // And every node is on one of the two end planes or between them.
        const double zMm = z * 1e3;
        CHECK(zMm >= -1e-6);
        CHECK(zMm <= 25.0 + 1e-6);
    }
    CHECK(onWall > 0);
}

TEST_CASE("SurfCylinder_WallNormalsPointRadiallyOutward", "[meshing][surf][cylinder][normals]") {
    // A cylinder's normals are NOT all equal -- each must agree with the LOCAL
    // outward direction, which for the wall is the radial direction at the
    // triangle's centroid.
    const SurfacedCylinder part{12_mm, 25_mm};
    const Result<EngineeringSurfaceMesh> surface =
        part.surface({Length::fromSi(2e-5), Angle::fromSi(0.05)});
    REQUIRE(surface.has_value());

    std::size_t wall = 0;
    std::size_t caps = 0;
    for (const Triangle& t : surface->mesh.triangles()) {
        const Normal n = normalOf(surface->mesh, t);
        const Normal c = centroidOf(surface->mesh, t);
        const double radial = std::sqrt(c.x * c.x + c.y * c.y);
        if (std::abs(n.z) > 0.99) {
            // A cap: outward is +z at the top, -z at the bottom.
            ++caps;
            const double zMm = c.z * 1e3;
            CHECK((n.z > 0.0 ? zMm > 12.0 : zMm < 13.0));
        } else if (radial > 1e-9) {
            // The wall: the normal's radial component must be positive, i.e. it
            // points away from the axis. An inward wall would be negative.
            ++wall;
            const double dotRadial = (n.x * c.x + n.y * c.y) / radial;
            CHECK(dotRadial > 0.9);
        }
    }
    CHECK(wall > 0);
    CHECK(caps > 0);
}

// ---------------------------------------------------------------------------
// SURF-HOLE and the hollow tube
// ---------------------------------------------------------------------------

TEST_CASE("SurfTube_InnerWallNormalsPointTowardTheAxisBecauseThatIsOutOfTheMaterial",
          "[meshing][surf][tube][normals]") {
    // THE ORIENTATION TEST THAT MATTERS. "Outward" means out of the MATERIAL, not
    // away from the global origin. On a tube's inner wall those are opposite
    // directions, so a surface that confused them would pass every other check
    // here and fail this one.
    const SurfacedTube part{20_mm, 12_mm, 40_mm};
    const Result<EngineeringSurfaceMesh> surface =
        part.surface({Length::fromSi(2e-5), Angle::fromSi(0.05)});
    REQUIRE(surface.has_value());
    CHECK(surface->validation.watertight());

    std::size_t inner = 0;
    std::size_t outer = 0;
    for (const Triangle& t : surface->mesh.triangles()) {
        const Normal n = normalOf(surface->mesh, t);
        const Normal c = centroidOf(surface->mesh, t);
        const double radial = std::sqrt(c.x * c.x + c.y * c.y);
        if (std::abs(n.z) > 0.99 || radial < 1e-12) {
            continue; // an annular end face
        }
        const double dotRadial = (n.x * c.x + n.y * c.y) / radial;
        const double rMm = radial * 1e3;
        if (rMm < 16.0) {
            // Inner wall: outward from the material points TOWARD the axis.
            ++inner;
            CHECK(dotRadial < -0.9);
        } else {
            ++outer;
            CHECK(dotRadial > 0.9);
        }
    }
    CHECK(inner > 0);
    CHECK(outer > 0);
}

TEST_CASE("SurfTube_VolumeFromTheSurfaceExcludesTheVoid", "[meshing][surf][tube][volume]") {
    // V = pi (Ro^2 - Ri^2) h = pi (400 - 144) * 40, by hand. If the cavity were
    // capped or its wall missing, the enclosed volume would be the outer
    // cylinder's -- 2.78x larger -- so this single number catches both.
    const SurfacedTube part{20_mm, 12_mm, 40_mm};
    const Result<EngineeringSurfaceMesh> surface =
        part.surface({Length::fromSi(2e-5), Angle::fromSi(0.05)});
    REQUIRE(surface.has_value());

    const double exact = std::numbers::pi * (400.0 - 144.0) * 40.0;
    const double outerOnly = std::numbers::pi * 400.0 * 40.0;
    CHECK(mm3(surface->enclosedVolume) > 0.0);
    CHECK_THAT(mm3(surface->enclosedVolume), WithinRel(exact, 1e-3));
    // Nowhere near the capped-cavity answer. The ratio matters and is easy to get
    // wrong: a tube of these radii is (400 - 144) / 400 = 64% of its outer
    // cylinder, so the capped answer is 1.56x the true one. A "< 50%" guard would
    // be arithmetically impossible rather than strict.
    CHECK(exact < 0.65 * outerOnly);
    CHECK(mm3(surface->enclosedVolume) < 0.8 * outerOnly);
}

TEST_CASE("SurfTube_HasTrianglesOnTheInnerWallSoTheCavityIsRepresented",
          "[meshing][surf][tube][holes]") {
    // Classified geometrically, in the test only: production face mapping is
    // P16-MAP-001's and implementing it here to satisfy a test would start that
    // milestone early.
    const SurfacedTube part{20_mm, 12_mm, 40_mm};
    const Result<EngineeringSurfaceMesh> surface =
        part.surface({Length::fromSi(2e-5), Angle::fromSi(0.05)});
    REQUIRE(surface.has_value());

    std::size_t onInnerWall = 0;
    for (const Node& node : surface->mesh.nodes()) {
        const double x = node.position.x.si();
        const double y = node.position.y.si();
        const double r = std::sqrt(x * x + y * y) * 1e3;
        if (std::abs(r - 12.0) < 1e-6) {
            ++onInnerWall;
        }
    }
    CHECK(onInnerWall > 0);

    // And the opening is NOT capped: no node sits on the axis.
    for (const Node& node : surface->mesh.nodes()) {
        const double r =
            std::sqrt(node.position.x.si() * node.position.x.si() + node.position.y.si() * node.position.y.si()) *
            1e3;
        CHECK(r > 1.0);
    }
}

// ---------------------------------------------------------------------------
// SURF-TRANSFORM
// ---------------------------------------------------------------------------

TEST_CASE("SurfTransform_AreaAndVolumeAreInvariantUnderAPlacement",
          "[meshing][surf][transform]") {
    // The same asymmetric box, built on the XY plane and on the XZ plane: a rigid
    // placement cannot change an area or an enclosed volume. If the enclosed
    // volume moved, the surface would not really be closed -- the relation is only
    // translation-invariant for a boundary that closes.
    const Surfaced flat{20_mm, 30_mm, 50_mm, Frame3D::xy()};
    const Surfaced placed{20_mm, 30_mm, 50_mm, Frame3D::xz()};
    const EngineeringSurfaceMesh a = flat.require_();
    const EngineeringSurfaceMesh b = placed.require_();

    CHECK_THAT(mm2(b.area), WithinRel(mm2(a.area), kExact));
    CHECK_THAT(mm3(b.enclosedVolume), WithinRel(mm3(a.enclosedVolume), kExact));
    CHECK(mm3(b.enclosedVolume) > 0.0); // still outward after the placement
    CHECK(b.validation.watertight());
    CHECK(b.mesh.nodeCount() == a.mesh.nodeCount());
    CHECK(b.mesh.triangles().size() == a.mesh.triangles().size());

    // The placed box really is somewhere else.
    const auto boundsA = a.mesh.bounds();
    const auto boundsB = b.mesh.bounds();
    REQUIRE(boundsA.has_value());
    REQUIRE(boundsB.has_value());
    CHECK_FALSE(*boundsA == *boundsB);
}

TEST_CASE("SurfTransform_EnclosedVolumeSurvivesATranslationOfEveryNode",
          "[meshing][surf][transform][volume]") {
    // The strong invariant: for a genuinely closed surface the 1/6 sum is
    // translation-invariant, because the open terms cancel. Translate every node
    // far from the origin and the volume must not budge. A surface with a crack
    // would drift.
    const Surfaced part{20_mm, 30_mm, 50_mm};
    const EngineeringSurfaceMesh surface = part.require_();

    // 137 mm, not 137 m: fromSi takes metres, and the extreme case is the separate
    // check below where the tolerance is justified rather than assumed.
    const Translation3D far{137_mm, -91_mm, 43_mm};
    meshing::MeshBuilder moved;
    std::map<NodeId::ValueType, NodeId> remap;
    for (const Node& node : surface.mesh.nodes()) {
        const Result<NodeId> added = moved.addNode(node.position + far);
        REQUIRE(added.has_value());
        remap.emplace(node.id.value(), *added);
    }
    for (const Triangle& t : surface.mesh.triangles()) {
        REQUIRE(moved
                    .addTriangle({remap.at(t.nodes[0].value()), remap.at(t.nodes[1].value()),
                                  remap.at(t.nodes[2].value())},
                                 t.region)
                    .has_value());
    }
    const meshing::Mesh translated = moved.build();

    CHECK_THAT(mm3(meshing::enclosedVolume(translated)), WithinRel(mm3(surface.enclosedVolume), 1e-9));
    CHECK_THAT(mm2(meshing::surfaceArea(translated)), WithinRel(mm2(surface.area), kExact));
    CHECK(meshing::validateSurface(translated).watertight());
}

TEST_CASE("SurfTransform_EnclosedVolumeSurvivesATranslationOfAThousandBodyLengths",
          "[meshing][surf][transform][volume]") {
    // The same invariant pushed until floating point is the limit, because that
    // tells us which it is: a drift here is cancellation, not a crack.
    //
    // Translating a 50 mm body by 137 METRES makes each triangle's contribution
    // about 137^3/6 ~ 4e5 m^3 while their sum is 3e-5 m^3 -- a cancellation ratio
    // near 1e10, so about ten significant digits are consumed before the answer
    // appears. The tolerance below is that loss, not slack: 1e-5 relative is what
    // ~1e10 cancellation leaves of a double's 1e-16.
    const Surfaced part{20_mm, 30_mm, 50_mm};
    const EngineeringSurfaceMesh surface = part.require_();

    const Translation3D enormous{Length::fromSi(137.0), Length::fromSi(-91.0), Length::fromSi(43.0)};
    meshing::MeshBuilder moved;
    std::map<NodeId::ValueType, NodeId> remap;
    for (const Node& node : surface.mesh.nodes()) {
        const Result<NodeId> added = moved.addNode(node.position + enormous);
        REQUIRE(added.has_value());
        remap.emplace(node.id.value(), *added);
    }
    for (const Triangle& t : surface.mesh.triangles()) {
        REQUIRE(moved
                    .addTriangle({remap.at(t.nodes[0].value()), remap.at(t.nodes[1].value()),
                                  remap.at(t.nodes[2].value())},
                                 t.region)
                    .has_value());
    }
    const meshing::Mesh translated = moved.build();

    // Still closed, and still the same volume to within what the arithmetic allows.
    CHECK(meshing::validateSurface(translated).watertight());
    CHECK_THAT(mm3(meshing::enclosedVolume(translated)), WithinRel(mm3(surface.enclosedVolume), 1e-5));
    // The area, which involves no cancellation, stays exact.
    CHECK_THAT(mm2(meshing::surfaceArea(translated)), WithinRel(mm2(surface.area), kExact));
}

// ---------------------------------------------------------------------------
// Display / engineering separation
// ---------------------------------------------------------------------------

TEST_CASE("SurfaceMesh_IsUnaffectedByAnEarlierCoarseTriangulationOfTheSameBody",
          "[meshing][surf][separation]") {
    // THE CACHE-CONTAMINATION TEST. A coarse triangulation first, standing in for
    // a display pass; then an engineering request with different controls. The
    // engineering mesh must follow the ENGINEERING controls.
    //
    // It cannot be otherwise, and that is structural rather than lucky:
    // geometry::triangulate meshes a BRepBuilderAPI_Copy with copyMesh=false, so
    // nothing cached on the authoritative faces is read and nothing is written
    // there.
    const SurfacedCylinder part{12_mm, 25_mm};
    const Result<MeshableGeometry> prepared =
        meshing::requireMeshableGeometry(part.document, part.regenerator, part.feature);
    REQUIRE(prepared.has_value());

    // A deliberately coarse pass, as a viewer would want.
    const Result<geometry::Mesh> display =
        geometry::triangulate(prepared->body, {Length::fromSi(2e-3), Angle::fromSi(1.0)});
    REQUIRE(display.has_value());

    const SurfaceMeshControls fine{Length::fromSi(2e-5), Angle::fromSi(0.05)};
    const Result<EngineeringSurfaceMesh> engineering = part.surface(fine);
    REQUIRE(engineering.has_value());

    // The engineering mesh is much finer than the coarse pass, so it did not
    // inherit it.
    CHECK(engineering->mesh.triangles().size() > display->triangles.size() * 4);
    CHECK(engineering->controls == fine);

    // And the reverse order changes nothing: another coarse pass afterwards does
    // not alter what the engineering request returns.
    const Result<geometry::Mesh> again =
        geometry::triangulate(prepared->body, {Length::fromSi(2e-3), Angle::fromSi(1.0)});
    REQUIRE(again.has_value());
    const Result<EngineeringSurfaceMesh> repeated = part.surface(fine);
    REQUIRE(repeated.has_value());
    CHECK(repeated->mesh.triangles().size() == engineering->mesh.triangles().size());
    CHECK(repeated->mesh.nodeCount() == engineering->mesh.nodeCount());
    CHECK_THAT(mm2(repeated->area), WithinRel(mm2(engineering->area), 1e-12));
}

TEST_CASE("SurfaceMesh_GenerationLeavesTheDocumentAndTheBodyUntouched",
          "[meshing][surf][separation]") {
    // Triangulating is derived work, not a CAD edit. Nothing about the model may
    // move because a surface was asked for.
    const Surfaced part{20_mm, 30_mm, 50_mm};
    const std::uint64_t revision = part.document.revision();
    const Result<MeshableGeometry> before =
        meshing::requireMeshableGeometry(part.document, part.regenerator, part.feature);
    REQUIRE(before.has_value());
    const auto topologyBefore = before->body.topology();

    for (int repeat = 0; repeat < 3; ++repeat) {
        REQUIRE(part.surface().has_value());
    }

    const Result<MeshableGeometry> after =
        meshing::requireMeshableGeometry(part.document, part.regenerator, part.feature);
    REQUIRE(after.has_value());
    CHECK(part.document.revision() == revision);
    CHECK(after->body.topology() == topologyBefore);
    CHECK(after->revision == before->revision);
    CHECK_THAT(mm3(after->volume), WithinRel(mm3(before->volume), 1e-12));
    CHECK_THAT(after->body.massProperties()->surfaceArea.in(units::mm2),
               WithinRel(before->body.massProperties()->surfaceArea.in(units::mm2), 1e-12));
}

// ---------------------------------------------------------------------------
// Integration with the geometry boundary
// ---------------------------------------------------------------------------

TEST_CASE("SurfaceMesh_RefusesStaleGeometryBeforeTriangulatingAnything",
          "[meshing][surf][stale]") {
    // Proves P16-SURF goes through P16-GEOM rather than around it. The old body is
    // still there, valid and closed; no triangles may come from it.
    Surfaced part{20_mm, 30_mm, 50_mm};
    REQUIRE(part.surface().has_value());

    REQUIRE(part.document
                .modifyObject<sketch::Sketch>(part.profile,
                                              [&](sketch::Sketch& s) -> Result<bool> {
                                                  return s.addDistance(part.lines[0], 40_mm).has_value();
                                              })
                .has_value());

    REQUIRE(part.regenerator.body(part.feature) != nullptr); // the old body survives
    const Result<EngineeringSurfaceMesh> stale = part.surface();
    REQUIRE_FALSE(stale.has_value());
    CHECK(stale.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(stale.error().message, ContainsSubstring("stale"));

    // And it surfaces again once regenerated, with the new geometry.
    requireReport(part.regenerator, part.document);
    const Result<EngineeringSurfaceMesh> fresh = part.surface();
    REQUIRE(fresh.has_value());
    // The trapezoid of P16-GEOM-001: A = 1/2 (40 + 20) * 30 = 900 mm^2, so
    // V = 900 * 50 = 45000 mm^3.
    CHECK_THAT(mm3(fresh->enclosedVolume), WithinRel(45000.0, kExact));
}

TEST_CASE("SurfaceMesh_RefusesABlockedFeatureWithNoCurrentBody", "[meshing][surf][stale]") {
    Surfaced part{20_mm, 30_mm, 50_mm};
    REQUIRE(part.surface().has_value());

    REQUIRE(part.document
                .modifyObject<sketch::Sketch>(part.profile,
                                              [&](sketch::Sketch& s) -> Result<bool> {
                                                  auto a = s.addDistance(part.lines[0], 20_mm);
                                                  if (!a) {
                                                      return std::unexpected(a.error());
                                                  }
                                                  return s.addDistance(part.lines[0], 60_mm).has_value();
                                              })
                .has_value());
    requireReport(part.regenerator, part.document);

    const Result<EngineeringSurfaceMesh> blocked = part.surface();
    REQUIRE_FALSE(blocked.has_value());
    CHECK_THAT(blocked.error().message, ContainsSubstring("blocked"));
}

TEST_CASE("SurfaceMesh_RefusesNonPositiveOrNonFiniteControls", "[meshing][surf]") {
    const Surfaced part{20_mm, 30_mm, 50_mm};
    const Result<MeshableGeometry> prepared =
        meshing::requireMeshableGeometry(part.document, part.regenerator, part.feature);
    REQUIRE(prepared.has_value());

    for (const SurfaceMeshControls bad : {
             SurfaceMeshControls{Length::fromSi(0.0), Angle::fromSi(0.3)},
             SurfaceMeshControls{Length::fromSi(-1e-4), Angle::fromSi(0.3)},
             SurfaceMeshControls{Length::fromSi(1e-4), Angle::fromSi(0.0)},
             SurfaceMeshControls{Length::fromSi(std::numeric_limits<double>::quiet_NaN()),
                                 Angle::fromSi(0.3)},
         }) {
        const Result<EngineeringSurfaceMesh> result = meshing::generateSurfaceMesh(*prepared, bad);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::InvalidArgument);
    }
}

// ---------------------------------------------------------------------------
// The validator itself
// ---------------------------------------------------------------------------

TEST_CASE("ValidateSurface_CountsBoundaryEdgesOfAnOpenPatch", "[meshing][surf][validation]") {
    // A single triangle: three edges, each used once.
    meshing::MeshBuilder builder;
    REQUIRE(builder.addNode(Point3D{Length::zero(), Length::zero(), Length::zero()}).has_value());
    REQUIRE(builder.addNode(Point3D{Length::fromSi(1.0), Length::zero(), Length::zero()}).has_value());
    REQUIRE(builder.addNode(Point3D{Length::zero(), Length::fromSi(1.0), Length::zero()}).has_value());
    REQUIRE(builder
                .addTriangle({NodeId::fromValue(1), NodeId::fromValue(2), NodeId::fromValue(3)},
                             RegionId::fromValue(1))
                .has_value());

    const SurfaceValidation report = meshing::validateSurface(builder.build());
    CHECK(report.boundaryEdgeCount == 3);
    CHECK(report.nonManifoldEdgeCount == 0);
    CHECK_FALSE(report.watertight());
}

TEST_CASE("ValidateSurface_CountsANonManifoldEdgeSharedByThreeTriangles",
          "[meshing][surf][validation]") {
    // Three triangles on one shared edge. Every edge count is >= 1 and the
    // boundary count alone would not reveal the problem, which is why
    // watertight() requires the non-manifold count too.
    meshing::MeshBuilder builder;
    for (const auto& p : {Point3D{Length::zero(), Length::zero(), Length::zero()},
                          Point3D{Length::fromSi(1.0), Length::zero(), Length::zero()},
                          Point3D{Length::zero(), Length::fromSi(1.0), Length::zero()},
                          Point3D{Length::zero(), Length::fromSi(-1.0), Length::zero()},
                          Point3D{Length::zero(), Length::zero(), Length::fromSi(1.0)}}) {
        REQUIRE(builder.addNode(p).has_value());
    }
    const RegionId r = RegionId::fromValue(1);
    for (const auto third : {NodeId::fromValue(3), NodeId::fromValue(4), NodeId::fromValue(5)}) {
        REQUIRE(builder.addTriangle({NodeId::fromValue(1), NodeId::fromValue(2), third}, r).has_value());
    }

    const SurfaceValidation report = meshing::validateSurface(builder.build());
    CHECK(report.nonManifoldEdgeCount == 1);
    CHECK_FALSE(report.watertight());
}

TEST_CASE("ValidateSurface_CountsAnOrientationConflictThatEdgeCountingCannotSee",
          "[meshing][surf][validation]") {
    // Two triangles sharing an edge and traversing it the SAME way round. Edge
    // incidence is 2 for the shared edge -- perfectly manifold by that measure --
    // and the patch is still inconsistently oriented.
    meshing::MeshBuilder builder;
    for (const auto& p : {Point3D{Length::zero(), Length::zero(), Length::zero()},
                          Point3D{Length::fromSi(1.0), Length::zero(), Length::zero()},
                          Point3D{Length::zero(), Length::fromSi(1.0), Length::zero()},
                          Point3D{Length::fromSi(1.0), Length::fromSi(1.0), Length::zero()}}) {
        REQUIRE(builder.addNode(p).has_value());
    }
    const RegionId r = RegionId::fromValue(1);
    // Both traverse node1 -> node2.
    REQUIRE(builder.addTriangle({NodeId::fromValue(1), NodeId::fromValue(2), NodeId::fromValue(3)}, r)
                .has_value());
    REQUIRE(builder.addTriangle({NodeId::fromValue(1), NodeId::fromValue(2), NodeId::fromValue(4)}, r)
                .has_value());

    const SurfaceValidation report = meshing::validateSurface(builder.build());
    CHECK(report.orientationConflictCount >= 1);
    CHECK_FALSE(report.watertight());
}

TEST_CASE("ValidateSurface_CountsDegenerateAndDuplicateTriangles", "[meshing][surf][validation]") {
    meshing::MeshBuilder builder;
    // Three collinear nodes: distinct handles, zero area.
    REQUIRE(builder.addNode(Point3D{Length::zero(), Length::zero(), Length::zero()}).has_value());
    REQUIRE(builder.addNode(Point3D{Length::fromSi(1.0), Length::fromSi(1.0), Length::fromSi(1.0)})
                .has_value());
    REQUIRE(builder.addNode(Point3D{Length::fromSi(2.0), Length::fromSi(2.0), Length::fromSi(2.0)})
                .has_value());
    const RegionId r = RegionId::fromValue(1);
    const std::array<NodeId, 3> nodes{NodeId::fromValue(1), NodeId::fromValue(2), NodeId::fromValue(3)};
    REQUIRE(builder.addTriangle(nodes, r).has_value());
    // The same three nodes again, REVERSED: the same topological triangle.
    REQUIRE(builder.addTriangle({nodes[0], nodes[2], nodes[1]}, r).has_value());

    const SurfaceValidation report = meshing::validateSurface(builder.build());
    CHECK(report.degenerateTriangleCount == 2);
    CHECK(report.duplicateTriangleCount == 1); // a reversed duplicate still counts
}

TEST_CASE("ValidateSurface_CountsAnUnusedNode", "[meshing][surf][validation]") {
    meshing::MeshBuilder builder;
    for (const auto& p : {Point3D{Length::zero(), Length::zero(), Length::zero()},
                          Point3D{Length::fromSi(1.0), Length::zero(), Length::zero()},
                          Point3D{Length::zero(), Length::fromSi(1.0), Length::zero()},
                          Point3D{Length::fromSi(9.0), Length::fromSi(9.0), Length::fromSi(9.0)}}) {
        REQUIRE(builder.addNode(p).has_value());
    }
    REQUIRE(builder
                .addTriangle({NodeId::fromValue(1), NodeId::fromValue(2), NodeId::fromValue(3)},
                             RegionId::fromValue(1))
                .has_value());
    CHECK(meshing::validateSurface(builder.build()).unusedNodeCount == 1);
}

TEST_CASE("EnclosedVolume_IsNegativeForAnInwardOrientedClosedSurface",
          "[meshing][surf][validation][volume]") {
    // A watertight surface can be coherently oriented and still face inward, and
    // no edge count would notice. The SIGN is what notices, which is why
    // generateSurfaceMesh refuses a non-positive enclosed volume.
    //
    // Take the box's surface and reverse every triangle's winding: still closed,
    // still manifold, still coherent -- and inside out.
    const Surfaced part{20_mm, 30_mm, 50_mm};
    const EngineeringSurfaceMesh surface = part.require_();
    REQUIRE(mm3(surface.enclosedVolume) > 0.0);

    meshing::MeshBuilder flipped;
    std::map<NodeId::ValueType, NodeId> remap;
    for (const Node& node : surface.mesh.nodes()) {
        const Result<NodeId> added = flipped.addNode(node.position);
        REQUIRE(added.has_value());
        remap.emplace(node.id.value(), *added);
    }
    for (const Triangle& t : surface.mesh.triangles()) {
        REQUIRE(flipped
                    .addTriangle({remap.at(t.nodes[0].value()), remap.at(t.nodes[2].value()),
                                  remap.at(t.nodes[1].value())},
                                 t.region)
                    .has_value());
    }
    const meshing::Mesh inward = flipped.build();

    CHECK(meshing::validateSurface(inward).watertight()); // closed and coherent
    CHECK(mm3(meshing::enclosedVolume(inward)) < 0.0);    // and inside out
    CHECK_THAT(mm3(meshing::enclosedVolume(inward)), WithinRel(-mm3(surface.enclosedVolume), kExact));
}

// ---------------------------------------------------------------------------
// Determinism
// ---------------------------------------------------------------------------

TEST_CASE("SurfaceMesh_IsIdenticalOnRepeatedGeneration", "[meshing][surf][determinism]") {
    const Surfaced box{20_mm, 30_mm, 50_mm};
    const SurfacedCylinder cylinder{12_mm, 25_mm};
    const SurfacedTube tube{20_mm, 12_mm, 40_mm};

    const EngineeringSurfaceMesh firstBox = box.require_();
    const Result<EngineeringSurfaceMesh> firstCylinder = cylinder.surface();
    const Result<EngineeringSurfaceMesh> firstTube = tube.surface();
    REQUIRE(firstCylinder.has_value());
    REQUIRE(firstTube.has_value());

    for (int repeat = 0; repeat < 5; ++repeat) {
        const EngineeringSurfaceMesh againBox = box.require_();
        checkSameContent(againBox.mesh, firstBox.mesh);
        CHECK(againBox.validation == firstBox.validation);
        CHECK(againBox.area.si() == firstBox.area.si());
        CHECK(againBox.enclosedVolume.si() == firstBox.enclosedVolume.si());
        CHECK(againBox.revision == firstBox.revision);

        const Result<EngineeringSurfaceMesh> againCylinder = cylinder.surface();
        REQUIRE(againCylinder.has_value());
        checkSameContent(againCylinder->mesh, firstCylinder->mesh);
        CHECK(againCylinder->area.si() == firstCylinder->area.si());

        const Result<EngineeringSurfaceMesh> againTube = tube.surface();
        REQUIRE(againTube.has_value());
        checkSameContent(againTube->mesh, firstTube->mesh);
        CHECK(againTube->enclosedVolume.si() == firstTube->enclosedVolume.si());
    }
}

TEST_CASE("SurfaceMesh_TwoMeshesOfTheSameBodyAreNeverTheSameMesh", "[meshing][surf][determinism]") {
    // The reason the determinism test above compares CONTENT and not `==`. A Mesh
    // carries a MeshStamp, and a MeshId is unique per mesh by design (ADR-031), so
    // two independently generated meshes of one body are deliberately unequal even
    // when every node and triangle agrees. Equality on Mesh is exact
    // representation equality INCLUDING identity, which is the useful meaning for
    // the data model and the wrong tool for this question.
    const Surfaced part{20_mm, 30_mm, 50_mm};
    const EngineeringSurfaceMesh a = part.require_();
    const EngineeringSurfaceMesh b = part.require_();

    CHECK(a.mesh.stamp().mesh != b.mesh.stamp().mesh);
    CHECK_FALSE(a.mesh == b.mesh);
    CHECK_FALSE(a.mesh.owns(b.mesh.stamp()));
    // And yet the content is identical, which is what determinism means here.
    checkSameContent(a.mesh, b.mesh);
}

TEST_CASE("SurfaceMesh_NodeHandlesFollowCoordinateOrderNotFaceTraversalOrder",
          "[meshing][surf][determinism]") {
    // Node numbering is assigned in ascending coordinate order, so it depends on
    // the geometry rather than on the order the kernel happened to explore faces
    // in. That is what makes the enumeration reproducible across presets.
    const Surfaced part{20_mm, 30_mm, 50_mm};
    const EngineeringSurfaceMesh surface = part.require_();

    std::vector<std::tuple<double, double, double>> positions;
    for (const Node& node : surface.mesh.nodes()) {
        positions.emplace_back(node.position.x.si(), node.position.y.si(), node.position.z.si());
    }
    CHECK(std::ranges::is_sorted(positions));
    // And handles ascend with them, which Mesh already guarantees.
    CHECK(surface.mesh.nodes().front().id.value() == 1);
}

TEST_CASE("SurfaceMeshFailure_EveryReasonHasAName", "[meshing][surf]") {
    for (const meshing::SurfaceMeshFailure f :
         {meshing::SurfaceMeshFailure::InvalidControls, meshing::SurfaceMeshFailure::TriangulationFailed,
          meshing::SurfaceMeshFailure::EmptySurface, meshing::SurfaceMeshFailure::NotAValidBoundary,
          meshing::SurfaceMeshFailure::InwardOrientation}) {
        CHECK_FALSE(meshing::toString(f).empty());
        CHECK(meshing::toString(f) != "unknown");
    }
}
