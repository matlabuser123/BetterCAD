// P16-MAP-001: geometry <-> mesh correspondence.
//
// WHAT THIS SUITE IS ABOUT. A load or a restraint is a statement about a FACE
// OF A PART, and the mesh it ends up applied to is derived state that a remesh
// replaces wholesale. So the tests here are mostly about one question asked in
// several ways: does canonical CAD intent still select the right mesh entities
// after the mesh, or the model, has changed -- and does it say so plainly when
// it cannot?
//
// HOW THE MAPPING IS CHECKED, and why that is not circular. The mapping
// attributes a facet to a CAD face by generation provenance: the kernel
// triangulates face by face, and that grouping is carried through unification,
// sorting and tetrahedralisation. The tests check the result GEOMETRICALLY --
// every facet of the +x face has all three vertices on x = xmax and faces +x --
// using the kernel's own `FaceInfo` for the surface it should lie on. Geometry
// is never how the mapping decides; it is only how the tests judge it.
//
// THE EPSILON, once, with its source. Triangulation nodes lie ON the surface
// they came from: P16-SURF-001 measured a cylinder's nodes at the true radius
// within 1e-9 m, and planar faces are exact. So 1e-9 m absolute is a
// representation epsilon over positions the kernel itself produced, not a
// modelling tolerance, and the mapping needs no tolerance at all.
#include "features/FeatureTestSupport.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/geometry/Mesh.hpp>
#include <bettercad/core/materials/MaterialProperty.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/meshing/MeshQuality.hpp>
#include <bettercad/meshing/SurfaceMesh.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <memory>
#include <numbers>
#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;
using meshing::BoundaryFacetSet;
using meshing::ElementId;
using meshing::FacetSource;
using meshing::GeometryMeshMap;
using meshing::MappedFace;
using meshing::MappedRegion;
using meshing::MappingState;
using meshing::Mesh;
using meshing::NamedBoundarySet;
using meshing::Node;
using meshing::NodeId;
using meshing::ResolvedBoundarySet;
using meshing::Tetrahedron;
using meshing::TetrahedronFace;
using meshing::Triangle;
using meshing::VolumeMesh;
using meshing::VolumeMeshControls;

namespace {

/// A representation epsilon over kernel-produced positions. See the file note.
constexpr double kOnSurface = 1e-9;

// --------------------------------------------------------------------------
// Fixtures
// --------------------------------------------------------------------------

/// A block whose three sides all differ, so no face can be mistaken for
/// another by its size, and whose four side faces are separately named.
struct Block {
    Document document{"Part"};
    features::Regenerator regenerator;
    ObjectId feature{};
    ObjectId profile{};
    std::array<EntityId, 4> lines{};

    Block(Length a = 30_mm, Length b = 20_mm, Length c = 10_mm,
          const Frame3D& plane = Frame3D::xy()) {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", plane);
        lines = addRectangle(*sketch, 0_mm, 0_mm, a, b);
        profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = c});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));
        requireReport(regenerator, document);
    }

    [[nodiscard]] FaceName top() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::EndCap}};
    }
    [[nodiscard]] FaceName bottom() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::StartCap}};
    }
    /// The face swept by profile line @p which (0..3).
    [[nodiscard]] FaceName side(std::size_t which) const {
        return FaceName{feature, FaceSelector{.role = FaceRole::Side, .entity = lines[which]}};
    }
    [[nodiscard]] std::vector<FaceName> allFaces() const {
        return {bottom(), top(), side(0), side(1), side(2), side(3)};
    }

    [[nodiscard]] VolumeMesh mesh(const VolumeMeshControls& controls = {}) {
        Result<VolumeMesh> result = meshing::volumeMeshFor(document, regenerator, feature, controls);
        if (!result.has_value()) {
            FAIL("volume mesh refused: " << result.error().message);
        }
        return *result;
    }

    [[nodiscard]] GeometryMeshMap map(const VolumeMesh& mesh) {
        Result<GeometryMeshMap> result =
            meshing::geometryMeshMapFor(document, regenerator, feature, mesh);
        if (!result.has_value()) {
            FAIL("mapping refused: " << result.error().message);
        }
        return *result;
    }

    void setDepth(Length depth) {
        REQUIRE(document
                    .modifyObject<features::ExtrudeFeature>(
                        feature,
                        [depth](features::ExtrudeFeature& extrude) -> Result<bool> {
                            auto definition = extrude.definition();
                            definition.depth = depth;
                            return extrude.setDefinition(definition).has_value();
                        })
                    .has_value());
        requireReport(regenerator, document);
    }
};

/// A cylinder, or -- with an inner circle -- a hollow tube whose bore is a
/// named face in its own right.
struct Round {
    Document document{"Round"};
    features::Regenerator regenerator;
    ObjectId feature{};
    EntityId outerCircle{};
    std::optional<EntityId> innerCircle{};

    Round(Length outer, Length height, std::optional<Length> inner = std::nullopt) {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        outerCircle = require(sketch->addCircle(Point2D{0_mm, 0_mm}, outer));
        if (inner.has_value()) {
            innerCircle = require(sketch->addCircle(Point2D{0_mm, 0_mm}, *inner));
        }
        const ObjectId profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = height});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));
        requireReport(regenerator, document);
    }

    [[nodiscard]] FaceName wall() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::Side, .entity = outerCircle}};
    }
    [[nodiscard]] FaceName bore() const {
        REQUIRE(innerCircle.has_value());
        return FaceName{feature, FaceSelector{.role = FaceRole::Side, .entity = *innerCircle}};
    }
    [[nodiscard]] FaceName top() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::EndCap}};
    }
    [[nodiscard]] FaceName bottom() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::StartCap}};
    }

    [[nodiscard]] VolumeMesh mesh(const VolumeMeshControls& controls) {
        Result<VolumeMesh> result = meshing::volumeMeshFor(document, regenerator, feature, controls);
        if (!result.has_value()) {
            FAIL("volume mesh refused: " << result.error().message);
        }
        return *result;
    }

    [[nodiscard]] GeometryMeshMap map(const VolumeMesh& mesh) {
        Result<GeometryMeshMap> result =
            meshing::geometryMeshMapFor(document, regenerator, feature, mesh);
        if (!result.has_value()) {
            FAIL("mapping refused: " << result.error().message);
        }
        return *result;
    }
};

/// A plate with a hole drilled through or into it.
struct Bored {
    Document document{"Bored"};
    features::Regenerator regenerator;
    ObjectId solid{};
    ObjectId bore{};

    explicit Bored(geometry::HoleExtent extent = geometry::HoleExtent::Through,
                   Length depth = 4_mm) {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        (void)addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
        const ObjectId profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = 10_mm});
        REQUIRE(extrude.has_value());
        solid = require(document.addObject(std::move(*extrude)));
        const geometry::FaceSignature startPlane =
            geometry::planeSignature(Point3D{0_mm, 0_mm, 0_mm}, Direction3D::unitZ().reversed());
        features::HoleDefinition definition{.target = FeatureId::fromValue(solid.value()),
                                           .face = startPlane,
                                           .center = Point2D{20_mm, 15_mm},
                                           .extent = extent,
                                           .diameter = 12_mm};
        if (extent == geometry::HoleExtent::Blind) {
            definition.depth = depth;
        }
        auto hole = features::HoleFeature::create("Bore", definition);
        if (!hole.has_value()) {
            FAIL("hole refused: " << hole.error().message);
        }
        bore = require(document.addObject(std::move(*hole)));
        requireReport(regenerator, document);
    }

    [[nodiscard]] FaceName holeBottom() const {
        return FaceName{bore, FaceSelector{.role = FaceRole::HoleBottom}};
    }

    /// Turns a blind hole into a through hole: the flat bottom ceases to
    /// exist, so its name has nothing left to name.
    void drillThrough() {
        const Result<bool> edited = document.modifyObject<features::HoleFeature>(
            bore, [](features::HoleFeature& hole) -> Result<bool> {
                auto definition = hole.definition();
                definition.extent = geometry::HoleExtent::Through;
                // A through hole takes no depth, and the feature refuses one.
                definition.depth = Length{};
                const Result<bool> set = hole.setDefinition(definition);
                if (!set.has_value()) {
                    return std::unexpected(set.error());
                }
                return *set;
            });
        if (!edited.has_value()) {
            FAIL("the edit was refused: " << edited.error().message);
        }
        // AND IT REALLY CHANGED SOMETHING. modifyObject reports "no change" as
        // a plain false, so an edit that quietly did nothing would leave every
        // assertion below describing the original model.
        REQUIRE(*edited);
        requireReport(regenerator, document);
    }

    [[nodiscard]] VolumeMesh mesh(const VolumeMeshControls& controls) {
        Result<VolumeMesh> result = meshing::volumeMeshFor(document, regenerator, bore, controls);
        if (!result.has_value()) {
            FAIL("volume mesh refused: " << result.error().message);
        }
        return *result;
    }

    [[nodiscard]] GeometryMeshMap map(const VolumeMesh& mesh) {
        Result<GeometryMeshMap> result =
            meshing::geometryMeshMapFor(document, regenerator, bore, mesh);
        if (!result.has_value()) {
            FAIL("mapping refused: " << result.error().message);
        }
        return *result;
    }
};

[[nodiscard]] VolumeMeshControls curved(Length target) {
    VolumeMeshControls controls;
    controls.sizing.globalTargetSize = target;
    controls.surface.linearDeflection = Length::fromSi(1e-4);
    return controls;
}

// --------------------------------------------------------------------------
// Geometric judgement, independent of the mapping
// --------------------------------------------------------------------------

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

[[nodiscard]] Vec3 of(const Point3D& p) {
    return Vec3{p.x.si(), p.y.si(), p.z.si()};
}
[[nodiscard]] Vec3 sub(const Vec3& a, const Vec3& b) {
    return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}
[[nodiscard]] double dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
[[nodiscard]] Vec3 cross(const Vec3& a, const Vec3& b) {
    return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
[[nodiscard]] double norm(const Vec3& a) {
    return std::sqrt(dot(a, a));
}

[[nodiscard]] std::array<Point3D, 3> cornersOf(const Mesh& mesh, ElementId facet) {
    const Triangle* triangle = mesh.findTriangle(facet);
    REQUIRE(triangle != nullptr);
    std::array<Point3D, 3> points{};
    for (std::size_t i = 0; i < 3; ++i) {
        const Node* node = mesh.findNode(triangle->nodes[i]);
        REQUIRE(node != nullptr);
        points[i] = node->position;
    }
    return points;
}

/// The facet's normal from its stored winding: the direction the mesh says is
/// out of the material.
[[nodiscard]] Vec3 outwardNormalOf(const Mesh& mesh, ElementId facet) {
    const std::array<Point3D, 3> p = cornersOf(mesh, facet);
    const Vec3 n = cross(sub(of(p[1]), of(p[0])), sub(of(p[2]), of(p[0])));
    const double length = norm(n);
    REQUIRE(length > 0.0);
    return Vec3{n.x / length, n.y / length, n.z / length};
}

[[nodiscard]] double distanceToAxis(const Point3D& point, const Axis3D& axis) {
    const Vec3 origin = of(axis.origin);
    const Vec3 direction{axis.direction.x(), axis.direction.y(), axis.direction.z()};
    const Vec3 offset = sub(of(point), origin);
    return norm(cross(offset, direction)) / norm(direction);
}

/// Every vertex of every facet lies on the surface the CAD face lies on, and
/// every facet faces the way that face faces.
///
/// THE TEST'S OWN JUDGEMENT, from the kernel's `FaceInfo`: the mapping never
/// looks at a coordinate, so this is an independent check of its answer rather
/// than a restatement of its method.
void checkFacetsLieOnFace(const Mesh& mesh, const MappedFace& face) {
    REQUIRE_FALSE(face.facets.empty());
    for (const ElementId facet : face.facets) {
        const std::array<Point3D, 3> p = cornersOf(mesh, facet);
        if (face.signature.has_value()) {
            const Vec3 normal{face.signature->normal.x(), face.signature->normal.y(),
                              face.signature->normal.z()};
            const Vec3 base = of(face.signature->point);
            for (const Point3D& corner : p) {
                CHECK(std::abs(dot(sub(of(corner), base), normal)) < kOnSurface);
            }
            // Outward means away from the material, which is what a boundary
            // facet's winding also means. They must agree.
            CHECK(dot(outwardNormalOf(mesh, facet), normal) > 0.9);
        } else if (face.cylinder.has_value()) {
            const double radius = face.cylinder->radius.si();
            for (const Point3D& corner : p) {
                CHECK(std::abs(distanceToAxis(corner, face.cylinder->axis) - radius) < kOnSurface);
            }
        } else {
            FAIL("the fixture's faces are all planar or cylindrical");
        }
    }
}

[[nodiscard]] std::vector<ElementId> allBoundaryFacets(const Mesh& mesh) {
    std::vector<ElementId> facets;
    for (const Triangle& triangle : mesh.triangles()) {
        facets.push_back(triangle.id);
    }
    return facets;
}

[[nodiscard]] const MappedFace& faceNamed(const GeometryMeshMap& map, const FaceName& name) {
    for (const MappedFace& face : map.faces()) {
        if (std::ranges::find(face.names, name) != face.names.end()) {
            return face;
        }
    }
    FAIL("the map holds no face carrying that name");
    return map.faces().front();
}

[[nodiscard]] BoundaryFacetSet facetsOf(const GeometryMeshMap& map, const FaceName& name) {
    Result<BoundaryFacetSet> set = meshing::boundaryFacetsOf(map, name);
    if (!set.has_value()) {
        FAIL("forward mapping refused: " << set.error().message);
    }
    return *set;
}

} // namespace

// ---------------------------------------------------------------------------
// The vocabulary each fixture really has
// ---------------------------------------------------------------------------

TEST_CASE("Map_RecordsTheFaceVocabularyOfTheReferenceFixtures", "[meshing][map][fixtures]") {
    // RECORDED, so that the suite's assumptions about which faces are named are
    // visible in the qualification log rather than buried in the tests. The
    // interesting line is the bored plate's last face.
    struct Row {
        std::string label;
        GeometryMeshMap map;
    };
    std::string text;
    const auto record = [&text](std::string_view label, const GeometryMeshMap& map) {
        text += std::format("\n{}: {} CAD faces, {} boundary facets, {} mapped, {} unmapped, "
                            "{} named, {} unnamed, {} without facets",
                            label, map.report().cadFaceCount, map.report().boundaryFacetCount,
                            map.report().mappedFacetCount, map.report().unmappedFacetCount,
                            map.report().namedFaceCount, map.report().unnamedFaceCount,
                            map.report().facesWithoutFacets);
        for (const MappedFace& face : map.faces()) {
            text += std::format("\n    [{}] {:9} {:4} facets", face.index,
                                geometry::toString(face.surface), face.facets.size());
            if (face.cylinder.has_value()) {
                text += std::format(" r={:.3f}mm", face.cylinder->radius.si() * 1e3);
            }
            text += face.names.empty() ? "  NO NAME" : "";
            for (const FaceName& name : face.names) {
                text += std::format("  {}/{}", name.feature, toString(name.face.role));
                if (name.face.entity.has_value()) {
                    text += std::format("/{}", *name.face.entity);
                }
            }
        }
    };

    {
        Block block;
        const VolumeMesh mesh = block.mesh();
        const GeometryMeshMap map = block.map(mesh);
        record("BOX 30x20x10", map);
        CHECK(map.report().cadFaceCount == 6);
        CHECK(map.report().unnamedFaceCount == 0);
    }
    {
        Round cylinder{8_mm, 20_mm};
        const VolumeMesh mesh = cylinder.mesh(curved(6_mm));
        const GeometryMeshMap map = cylinder.map(mesh);
        record("CYLINDER r8 h20", map);
        CHECK(map.report().cadFaceCount == 3);
        CHECK(map.report().unnamedFaceCount == 0);
    }
    {
        Round tube{12_mm, 20_mm, 6_mm};
        const VolumeMesh mesh = tube.mesh(curved(6_mm));
        const GeometryMeshMap map = tube.map(mesh);
        record("TUBE r12/r6 h20", map);
        CHECK(map.report().cadFaceCount == 4);
        // THE BORE OF A TUBE IS A NAMED FACE: the profile's inner circle sweeps
        // it, so it is a `Side` face like any other.
        CHECK(map.report().unnamedFaceCount == 0);
    }
    {
        Bored bored;
        const VolumeMesh mesh = bored.mesh(curved(8_mm));
        const GeometryMeshMap map = bored.map(mesh);
        record("BORED 40x30x10, 12mm hole", map);
        CHECK(map.report().cadFaceCount == 7);
        // THE WALL OF A DRILLED HOLE CARRIES NO NAME, because `cutHole` names
        // only a hole's flat faces. A documented limit of the naming
        // infrastructure, not of the correspondence: the wall is still mapped
        // and still answers in reverse.
        CHECK(map.report().unnamedFaceCount == 1);
    }
    WARN(text);
}

// ---------------------------------------------------------------------------
// MAP-BOX: coverage, partition, bidirectional consistency
// ---------------------------------------------------------------------------

TEST_CASE("MapBox_AttributesEveryBoundaryFacetToExactlyOneCadFace", "[meshing][map][box]") {
    Block block;
    const VolumeMesh mesh = block.mesh();
    const GeometryMeshMap map = block.map(mesh);

    CHECK(map.report().cadFaceCount == 6);
    CHECK(map.report().boundaryFacetCount == mesh.boundaryTriangleCount());
    CHECK(map.report().mappedFacetCount == map.report().boundaryFacetCount);
    CHECK(map.report().unmappedFacetCount == 0);
    CHECK(map.report().facetsWithSeveralFaces == 0);
    CHECK(map.report().facesWithoutFacets == 0);
    CHECK(map.report().issues.empty());
    CHECK(map.report().complete());
}

TEST_CASE("MapBox_PartitionsTheBoundaryWithNoGapAndNoOverlap", "[meshing][map][box][coverage]") {
    // ONE OF THE STRONGEST GATES IN THE SUITE. The union of the six faces'
    // facets must be every boundary facet, and no facet may appear twice. A
    // mapping that lost a facet, or claimed one for two faces, fails here even
    // if every individual face looks right.
    Block block;
    const VolumeMesh mesh = block.mesh();
    const GeometryMeshMap map = block.map(mesh);

    std::vector<ElementId> union_;
    for (const FaceName& name : block.allFaces()) {
        const BoundaryFacetSet set = facetsOf(map, name);
        REQUIRE(set.fullyResolved());
        union_.insert(union_.end(), set.facets.begin(), set.facets.end());
    }
    const std::size_t total = union_.size();
    std::ranges::sort(union_);
    union_.erase(std::ranges::unique(union_).begin(), union_.end());
    CHECK(union_.size() == total); // pairwise disjoint

    std::vector<ElementId> all = allBoundaryFacets(mesh.mesh());
    std::ranges::sort(all);
    CHECK(union_ == all);
}

TEST_CASE("MapBox_EveryFaceMapsOnlyToFacetsLyingOnIt", "[meshing][map][box][geometry]") {
    Block block;
    const VolumeMesh mesh = block.mesh();
    const GeometryMeshMap map = block.map(mesh);

    for (const MappedFace& face : map.faces()) {
        checkFacetsLieOnFace(mesh.mesh(), face);
    }
}

TEST_CASE("MapBox_SelectingOneFaceExcludesTheOtherFive", "[meshing][map][box]") {
    // The asymmetric block puts its +x face at x = 30 and its +y face at
    // y = 20, so a facet on one cannot satisfy the other's plane.
    Block block;
    const VolumeMesh mesh = block.mesh();
    const GeometryMeshMap map = block.map(mesh);

    const BoundaryFacetSet top = facetsOf(map, block.top());
    REQUIRE(top.fullyResolved());
    REQUIRE_FALSE(top.facets.empty());
    for (const ElementId facet : top.facets) {
        for (const Point3D& corner : cornersOf(mesh.mesh(), facet)) {
            CHECK_THAT(corner.z.si(), WithinRel(0.010, 1e-9));
        }
        CHECK(outwardNormalOf(mesh.mesh(), facet).z > 0.9);
    }

    const BoundaryFacetSet bottom = facetsOf(map, block.bottom());
    for (const ElementId facet : bottom.facets) {
        CHECK(std::ranges::find(top.facets, facet) == top.facets.end());
        CHECK(outwardNormalOf(mesh.mesh(), facet).z < -0.9);
    }
}

TEST_CASE("MapBox_TwoFacesMeetingAtAnEdgeStayDistinct", "[meshing][map][box][adversarial]") {
    // AT A SHARP EDGE the facets of two CAD faces share nodes, and their
    // vertices satisfy both faces' planes. Identity comes from provenance, so
    // the facet sets stay disjoint -- a coordinate-based classifier would have
    // to choose, and could choose wrong.
    Block block;
    const VolumeMesh mesh = block.mesh();
    const GeometryMeshMap map = block.map(mesh);

    const BoundaryFacetSet top = facetsOf(map, block.top());
    const BoundaryFacetSet side = facetsOf(map, block.side(1));
    REQUIRE(top.fullyResolved());
    REQUIRE(side.fullyResolved());

    std::set<ElementId> topSet(top.facets.begin(), top.facets.end());
    for (const ElementId facet : side.facets) {
        CHECK_FALSE(topSet.contains(facet));
    }

    // And they really do touch: the two faces share nodes along their edge.
    const Result<std::vector<NodeId>> topNodes =
        meshing::boundaryNodesOf(map, mesh.mesh(), top.facets);
    const Result<std::vector<NodeId>> sideNodes =
        meshing::boundaryNodesOf(map, mesh.mesh(), side.facets);
    REQUIRE(topNodes.has_value());
    REQUIRE(sideNodes.has_value());
    std::vector<NodeId> shared;
    std::ranges::set_intersection(*topNodes, *sideNodes, std::back_inserter(shared));
    CHECK(shared.size() >= 2);
}

TEST_CASE("MapBox_ReverseMappingAgreesWithTheForwardMapping",
          "[meshing][map][box][bidirectional]") {
    // MANDATORY WHERE PROVENANCE PROMISES AN EXACT MAPPING: for every face F
    // and every facet b of F, b's source must be F.
    Block block;
    const VolumeMesh mesh = block.mesh();
    const GeometryMeshMap map = block.map(mesh);

    std::size_t checked = 0;
    std::size_t mismatched = 0;
    for (const FaceName& name : block.allFaces()) {
        const MappedFace& face = faceNamed(map, name);
        for (const ElementId facet : face.facets) {
            const Result<FacetSource> source = meshing::sourceFaceOf(map, facet);
            REQUIRE(source.has_value());
            ++checked;
            if (source->face != face.index ||
                std::ranges::find(source->names, name) == source->names.end()) {
                ++mismatched;
            }
        }
    }
    CHECK(checked == mesh.boundaryTriangleCount());
    CHECK(mismatched == 0);
}

// ---------------------------------------------------------------------------
// MAP-CYLINDER
// ---------------------------------------------------------------------------

TEST_CASE("MapCylinder_LateralFaceExcludesTheCapsAndTheCapsExcludeTheWall",
          "[meshing][map][cylinder]") {
    // REGION-TAG LEAKAGE IS THE DEFECT THIS CATCHES, in both directions.
    Round cylinder{8_mm, 20_mm};
    const VolumeMesh mesh = cylinder.mesh(curved(6_mm));
    const GeometryMeshMap map = cylinder.map(mesh);
    REQUIRE(map.report().complete());

    const BoundaryFacetSet wall = facetsOf(map, cylinder.wall());
    const BoundaryFacetSet top = facetsOf(map, cylinder.top());
    const BoundaryFacetSet bottom = facetsOf(map, cylinder.bottom());
    REQUIRE(wall.fullyResolved());
    REQUIRE(top.fullyResolved());
    REQUIRE(bottom.fullyResolved());

    // No facet is on two of them.
    std::set<ElementId> seen;
    for (const BoundaryFacetSet* set : {&wall, &top, &bottom}) {
        for (const ElementId facet : set->facets) {
            CHECK(seen.insert(facet).second);
        }
    }
    CHECK(seen.size() == mesh.boundaryTriangleCount());

    // The wall's facets are on the cylinder and nowhere near either cap plane;
    // the caps' are flat and at the right z.
    const MappedFace wallFace = faceNamed(map, cylinder.wall());
    REQUIRE(wallFace.cylinder.has_value());
    for (const ElementId facet : wall.facets) {
        for (const Point3D& corner : cornersOf(mesh.mesh(), facet)) {
            CHECK(std::abs(distanceToAxis(corner, wallFace.cylinder->axis) - 0.008) < kOnSurface);
        }
        // A cap facet would have a vertical normal; a wall facet's is radial.
        CHECK(std::abs(outwardNormalOf(mesh.mesh(), facet).z) < 1e-6);
    }
    for (const ElementId facet : top.facets) {
        CHECK(outwardNormalOf(mesh.mesh(), facet).z > 0.9);
        for (const Point3D& corner : cornersOf(mesh.mesh(), facet)) {
            CHECK_THAT(corner.z.si(), WithinRel(0.020, 1e-9));
        }
    }
}

// ---------------------------------------------------------------------------
// MAP-TUBE: a bore that is a named face, and two cylinders of the same kind
// ---------------------------------------------------------------------------

TEST_CASE("MapTube_BoreIsSelectableAndMapsOnlyToTheBore", "[meshing][map][tube][hole]") {
    // THE HOLE-WALL CASE, on the fixture where the hole wall has a canonical
    // name: a tube's bore is swept by the profile's inner circle, so it is a
    // `Side` face and is selectable like any other.
    Round tube{12_mm, 20_mm, 6_mm};
    const VolumeMesh mesh = tube.mesh(curved(6_mm));
    const GeometryMeshMap map = tube.map(mesh);
    REQUIRE(map.report().complete());

    const BoundaryFacetSet bore = facetsOf(map, tube.bore());
    REQUIRE(bore.fullyResolved());
    REQUIRE_FALSE(bore.facets.empty());

    const MappedFace boreFace = faceNamed(map, tube.bore());
    REQUIRE(boreFace.cylinder.has_value());
    CHECK_THAT(boreFace.cylinder->radius.si(), WithinRel(0.006, 1e-9));

    for (const ElementId facet : bore.facets) {
        const std::array<Point3D, 3> p = cornersOf(mesh.mesh(), facet);
        for (const Point3D& corner : p) {
            CHECK(std::abs(distanceToAxis(corner, boreFace.cylinder->axis) - 0.006) < kOnSurface);
        }
        // OUTWARD MEANS OUT OF THE MATERIAL, which on a bore points INWARD
        // toward the axis. Confusing the two with "radially outward" is the
        // mistake this asserts against.
        const Vec3 normal = outwardNormalOf(mesh.mesh(), facet);
        const Vec3 centroid{(p[0].x.si() + p[1].x.si() + p[2].x.si()) / 3.0,
                            (p[0].y.si() + p[1].y.si() + p[2].y.si()) / 3.0, 0.0};
        CHECK(dot(normal, centroid) < 0.0);
    }
}

TEST_CASE("MapTube_OuterWallAndBoreStayDistinct", "[meshing][map][tube][adversarial]") {
    // Two cylindrical faces about the SAME AXIS, differing only in radius.
    // Nothing is classified by geometry, so there is nothing for them to
    // confuse.
    Round tube{12_mm, 20_mm, 6_mm};
    const VolumeMesh mesh = tube.mesh(curved(6_mm));
    const GeometryMeshMap map = tube.map(mesh);

    const BoundaryFacetSet wall = facetsOf(map, tube.wall());
    const BoundaryFacetSet bore = facetsOf(map, tube.bore());
    REQUIRE(wall.fullyResolved());
    REQUIRE(bore.fullyResolved());
    for (const ElementId facet : bore.facets) {
        CHECK(std::ranges::find(wall.facets, facet) == wall.facets.end());
    }

    // And the reverse query names the bore, not the outer wall.
    for (const ElementId facet : bore.facets) {
        const Result<FacetSource> source = meshing::sourceFaceOf(map, facet);
        REQUIRE(source.has_value());
        CHECK(std::ranges::find(source->names, tube.bore()) != source->names.end());
        CHECK(std::ranges::find(source->names, tube.wall()) == source->names.end());
    }
}

TEST_CASE("MapTube_AnnularCapsAreDistinctFromBothWalls", "[meshing][map][tube]") {
    Round tube{12_mm, 20_mm, 6_mm};
    const VolumeMesh mesh = tube.mesh(curved(6_mm));
    const GeometryMeshMap map = tube.map(mesh);

    std::set<ElementId> seen;
    for (const FaceName& name : {tube.wall(), tube.bore(), tube.top(), tube.bottom()}) {
        const BoundaryFacetSet set = facetsOf(map, name);
        REQUIRE(set.fullyResolved());
        for (const ElementId facet : set.facets) {
            CHECK(seen.insert(facet).second);
        }
    }
    CHECK(seen.size() == mesh.boundaryTriangleCount());
}

// ---------------------------------------------------------------------------
// MAP-HOLE: a drilled hole, whose wall the naming infrastructure does not name
// ---------------------------------------------------------------------------

TEST_CASE("MapBored_DrilledHoleWallIsMappedInReverseAndCarriesNoName",
          "[meshing][map][hole][limitation]") {
    // THE LIMIT, STATED AS A TEST. `cutHole` names a hole's flat faces and not
    // its cylindrical wall, so a drilled wall has no canonical reference and
    // cannot be the target of a forward query. What this layer must NOT do is
    // paper over that: the wall is fully mapped, its facets answer in reverse
    // with an empty name list, and the report counts the unnamed face.
    Bored bored;
    const VolumeMesh mesh = bored.mesh(curved(8_mm));
    const GeometryMeshMap map = bored.map(mesh);

    CHECK(map.report().complete());
    CHECK(map.report().unnamedFaceCount == 1);

    const MappedFace* wall = nullptr;
    for (const MappedFace& face : map.faces()) {
        if (face.names.empty()) {
            wall = &face;
        }
    }
    REQUIRE(wall != nullptr);
    CHECK(wall->surface == geometry::FaceSurface::Cylinder);
    REQUIRE(wall->cylinder.has_value());
    CHECK_THAT(wall->cylinder->radius.si(), WithinRel(0.006, 1e-9));
    REQUIRE_FALSE(wall->facets.empty());

    for (const ElementId facet : wall->facets) {
        const Result<FacetSource> source = meshing::sourceFaceOf(map, facet);
        REQUIRE(source.has_value());
        CHECK(source->face == wall->index);
        // EMPTY, not fabricated. No nearby named face is offered in its place.
        CHECK(source->names.empty());
    }
    checkFacetsLieOnFace(mesh.mesh(), *wall);
}

TEST_CASE("MapBored_NamedFacesCoverEverythingExceptTheHoleWall", "[meshing][map][hole]") {
    Bored bored;
    const VolumeMesh mesh = bored.mesh(curved(8_mm));
    const GeometryMeshMap map = bored.map(mesh);

    std::size_t namedFacets = 0;
    std::size_t unnamedFacets = 0;
    for (const MappedFace& face : map.faces()) {
        if (face.names.empty()) {
            unnamedFacets += face.facets.size();
        } else {
            namedFacets += face.facets.size();
        }
    }
    CHECK(namedFacets + unnamedFacets == mesh.boundaryTriangleCount());
    CHECK(unnamedFacets > 0);
    CHECK(namedFacets > 0);
}

TEST_CASE("MapBored_TheCapsKeepTheirNamesThroughTheBoring", "[meshing][map][hole][stref]") {
    // A through hole is a TOPOLOGY-CHANGING edit, and P12-STREF's history
    // carries a name to every face a named face became: the start and end caps
    // become annuli and keep their names. So this is the PRESERVING direction,
    // and it is the existing infrastructure's guarantee rather than a new one.
    Bored bored;
    const VolumeMesh mesh = bored.mesh(curved(8_mm));
    const GeometryMeshMap map = bored.map(mesh);

    const FaceName endCap{bored.solid, FaceSelector{.role = FaceRole::EndCap}};
    const BoundaryFacetSet set = facetsOf(map, endCap);
    CHECK(set.fullyResolved());
    CHECK_FALSE(set.facets.empty());
    for (const ElementId facet : set.facets) {
        CHECK(outwardNormalOf(mesh.mesh(), facet).z > 0.9);
    }
}

// ---------------------------------------------------------------------------
// MAP-TRANSFORM
// ---------------------------------------------------------------------------

TEST_CASE("MapTransform_FollowsTheBodyIntoItsNewFrame", "[meshing][map][transform]") {
    // The same block on two sketch planes. The CAD selection means the same
    // thing; the mesh coordinates do not. A mapping done in a stale frame, or
    // judged against hard-coded world axes, would fail here.
    Block atOrigin{30_mm, 20_mm, 10_mm, Frame3D::xy()};
    Block onXZ{30_mm, 20_mm, 10_mm, Frame3D::xz()};

    const VolumeMesh a = atOrigin.mesh();
    const VolumeMesh b = onXZ.mesh();
    const GeometryMeshMap mapA = atOrigin.map(a);
    const GeometryMeshMap mapB = onXZ.map(b);

    CHECK(mapA.report().complete());
    CHECK(mapB.report().complete());
    CHECK(mapA.report().cadFaceCount == mapB.report().cadFaceCount);

    // Every facet still lies on the face the kernel says it lies on -- in the
    // transformed frame, because that is where the authoritative geometry is.
    for (const MappedFace& face : mapB.faces()) {
        checkFacetsLieOnFace(b.mesh(), face);
    }

    // The two bodies really are in different places: the XZ plane faces -Y, so
    // this extrusion runs to negative y.
    const BoundaryFacetSet topA = facetsOf(mapA, atOrigin.top());
    const BoundaryFacetSet topB = facetsOf(mapB, onXZ.top());
    REQUIRE(topA.fullyResolved());
    REQUIRE(topB.fullyResolved());
    for (const ElementId facet : topA.facets) {
        CHECK(outwardNormalOf(a.mesh(), facet).z > 0.9);
    }
    for (const ElementId facet : topB.facets) {
        CHECK(outwardNormalOf(b.mesh(), facet).y < -0.9);
    }
    WARN(std::format("transform: XY block {} CAD faces / {} facets, end_cap {} facets facing +z; "
                     "XZ block {} CAD faces / {} facets, end_cap {} facets facing -y",
                     mapA.report().cadFaceCount, mapA.report().boundaryFacetCount,
                     topA.facets.size(), mapB.report().cadFaceCount,
                     mapB.report().boundaryFacetCount, topB.facets.size()));
}

// ---------------------------------------------------------------------------
// MAP-REMESH
// ---------------------------------------------------------------------------

TEST_CASE("MapRemesh_TheSameReferenceResolvesAgainstACoarseAndAFineMesh",
          "[meshing][map][remesh]") {
    // THE PRIMARY TEST. Canonical intent is the CAD reference; the facets are
    // derived and are expected to differ.
    Round cylinder{8_mm, 20_mm};
    const VolumeMesh coarse = cylinder.mesh(curved(8_mm));
    const GeometryMeshMap coarseMap = cylinder.map(coarse);
    const BoundaryFacetSet coarseWall = facetsOf(coarseMap, cylinder.wall());

    VolumeMeshControls fineControls = curved(2_mm);
    fineControls.surface.linearDeflection = Length::fromSi(2e-5);
    const VolumeMesh fine = cylinder.mesh(fineControls);
    const GeometryMeshMap fineMap = cylinder.map(fine);
    const BoundaryFacetSet fineWall = facetsOf(fineMap, cylinder.wall());

    CHECK(coarseWall.fullyResolved());
    CHECK(fineWall.fullyResolved());
    CHECK(fineWall.facets.size() > coarseWall.facets.size());

    // Both map to the same CAD face, and both are geometrically right.
    checkFacetsLieOnFace(coarse.mesh(), faceNamed(coarseMap, cylinder.wall()));
    checkFacetsLieOnFace(fine.mesh(), faceNamed(fineMap, cylinder.wall()));
    CHECK(coarseMap.report().complete());
    CHECK(fineMap.report().complete());

    // The two meshes are different generations, so a handle from one is not a
    // handle in the other.
    CHECK_FALSE(fine.mesh().owns(coarseMap.meshStamp()));
    CHECK_FALSE(coarse.mesh().owns(fineMap.meshStamp()));

    // Recorded, so the qualification log carries the figures the evidence
    // cites rather than the evidence quoting a number nobody can see.
    WARN(std::format("remesh: cylinder r8 h20 side face -- coarse {} tets, {} boundary facets, "
                     "{} on the face; fine {} tets, {} boundary facets, {} on the face",
                     coarse.tetrahedronCount(), coarse.boundaryTriangleCount(),
                     coarseWall.facets.size(), fine.tetrahedronCount(),
                     fine.boundaryTriangleCount(), fineWall.facets.size()));
}

TEST_CASE("MapRemesh_AMapIsRefusedAgainstADifferentMeshGeneration",
          "[meshing][map][remesh][stale]") {
    // No old facet handle is treated as authority: a query that mixes a map
    // with another mesh is refused outright.
    Block block;
    const VolumeMesh coarse = block.mesh();
    const GeometryMeshMap coarseMap = block.map(coarse);
    const VolumeMesh fine = block.mesh(curved(4_mm));

    const Result<std::vector<NodeId>> nodes =
        meshing::boundaryNodesOf(coarseMap, fine.mesh(), allBoundaryFacets(fine.mesh()));
    REQUIRE_FALSE(nodes.has_value());
    CHECK_THAT(nodes.error().message, ContainsSubstring("mapping_stale"));

    const Result<MappedRegion> region = meshing::regionOf(coarseMap, fine.mesh());
    REQUIRE_FALSE(region.has_value());
    CHECK_THAT(region.error().message, ContainsSubstring("mapping_stale"));
}

TEST_CASE("Map_RefusesAMeshBuiltFromAnEarlierGeometryRevision", "[meshing][map][stale]") {
    Block block;
    const VolumeMesh before = block.mesh();
    block.setDepth(25_mm);

    // The mesh is now from a superseded revision. Mapping it against the
    // CURRENT geometry would answer about a body that no longer exists.
    const Result<GeometryMeshMap> map =
        meshing::geometryMeshMapFor(block.document, block.regenerator, block.feature, before);
    REQUIRE_FALSE(map.has_value());
    CHECK(map.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(map.error().message, ContainsSubstring("mapping_stale"));
}

TEST_CASE("Map_RefusesAMeshOfADifferentFeature", "[meshing][map][stale]") {
    // TWO SOLIDS IN ONE DOCUMENT, so the two features have different ObjectIds
    // and the mismatch is detectable. (Two separate documents would not be:
    // ObjectIds and revision counters are per document, so two structurally
    // identical models hash alike -- see the known limitations.)
    Block block;
    auto second = std::make_unique<sketch::Sketch>("Other", Frame3D::xy());
    (void)addRectangle(*second, 100_mm, 0_mm, 8_mm, 6_mm);
    const ObjectId otherProfile = require(block.document.addObject(std::move(second)));
    auto otherExtrude = features::ExtrudeFeature::create(
        "Other_solid", {.profile = SketchId::fromValue(otherProfile.value()), .depth = 5_mm});
    REQUIRE(otherExtrude.has_value());
    const ObjectId otherFeature = require(block.document.addObject(std::move(*otherExtrude)));
    requireReport(block.regenerator, block.document);

    const Result<VolumeMesh> other =
        meshing::volumeMeshFor(block.document, block.regenerator, otherFeature, {});
    REQUIRE(other.has_value());

    const Result<GeometryMeshMap> map =
        meshing::geometryMeshMapFor(block.document, block.regenerator, block.feature, *other);
    REQUIRE_FALSE(map.has_value());
    CHECK_THAT(map.error().message, ContainsSubstring("mapping_stale"));
    // AND IT NAMES BOTH FEATURES. The revision check alone would also refuse
    // this -- a different feature hashes differently -- so what the source
    // check uniquely contributes is a diagnostic that says WHICH two things did
    // not match. Asserting only the refusal left that contribution untested,
    // and a mutation removing the check survived the suite.
    CHECK_THAT(map.error().message, ContainsSubstring(std::format("{}", block.feature)));
    CHECK_THAT(map.error().message, ContainsSubstring(std::format("{}", otherFeature)));
    CHECK_THAT(map.error().message, ContainsSubstring("was built from"));
}

TEST_CASE("Map_SurvivesATopologyPreservingEdit", "[meshing][map][regeneration]") {
    // The edit the existing reference system says should preserve a selection:
    // the block gets taller and its end cap rises with it.
    Block block;
    const VolumeMesh before = block.mesh();
    const GeometryMeshMap mapBefore = block.map(before);
    REQUIRE(facetsOf(mapBefore, block.top()).fullyResolved());

    block.setDepth(25_mm);
    const VolumeMesh after = block.mesh();
    const GeometryMeshMap mapAfter = block.map(after);

    const BoundaryFacetSet top = facetsOf(mapAfter, block.top());
    CHECK(top.fullyResolved());
    CHECK(mapAfter.report().complete());
    // And it followed the face, rather than staying where it was.
    for (const ElementId facet : top.facets) {
        for (const Point3D& corner : cornersOf(after.mesh(), facet)) {
            CHECK_THAT(corner.z.si(), WithinRel(0.025, 1e-9));
        }
    }
}

// ---------------------------------------------------------------------------
// MAP-TOPOLOGY-CHANGE
// ---------------------------------------------------------------------------

TEST_CASE("MapTopologyChange_ADeletedFaceBecomesUnresolvedAndIsNeverRebound",
          "[meshing][map][topology]") {
    // A blind hole has a flat bottom and a name for it. Drilling the hole
    // through REMOVES that face: the name now names nothing.
    //
    // UNRESOLVED IS THE PASS. P16 does not claim that an arbitrary
    // topology-changing edit preserves a face's semantic identity -- that is
    // P21's problem -- and the failure this guards against is pretending it
    // did, by binding the reference to the nearest surviving face.
    Bored blind{geometry::HoleExtent::Blind, 4_mm};
    const VolumeMesh before = blind.mesh(curved(8_mm));
    const GeometryMeshMap mapBefore = blind.map(before);

    const BoundaryFacetSet bottomBefore = facetsOf(mapBefore, blind.holeBottom());
    REQUIRE(bottomBefore.fullyResolved());
    REQUIRE_FALSE(bottomBefore.facets.empty());

    blind.drillThrough();
    const VolumeMesh after = blind.mesh(curved(8_mm));
    const GeometryMeshMap mapAfter = blind.map(after);

    const BoundaryFacetSet bottomAfter = facetsOf(mapAfter, blind.holeBottom());
    REQUIRE(bottomAfter.requested.size() == 1);
    CHECK(bottomAfter.requested.front().state == MappingState::Unresolved);
    CHECK(bottomAfter.requested.front().faces.empty());
    CHECK(bottomAfter.facets.empty());
    CHECK_FALSE(bottomAfter.fullyResolved());
    // The reference is REPORTED, not dropped: it is still in `requested`.
    CHECK(bottomAfter.requested.front().reference == blind.holeBottom());

    // And the mapping of the body is otherwise complete, so the unresolved
    // reference is not a symptom of a broken map.
    CHECK(mapAfter.report().complete());
}

TEST_CASE("Map_AReferenceToAnotherObjectDoesNotFallBackToThisBody",
          "[meshing][map][topology][adversarial]") {
    Block block;
    const VolumeMesh mesh = block.mesh();
    const GeometryMeshMap map = block.map(mesh);

    const FaceName elsewhere{ObjectId::fromValue(9999), FaceSelector{.role = FaceRole::EndCap}};
    const BoundaryFacetSet set = facetsOf(map, elsewhere);
    CHECK(set.requested.front().state == MappingState::Unresolved);
    CHECK(set.facets.empty());
}

TEST_CASE("Map_RefusesAMalformedSelectorRatherThanResolvingIt", "[meshing][map][validation]") {
    Block block;
    const VolumeMesh mesh = block.mesh();
    const GeometryMeshMap map = block.map(mesh);

    // An EndCap does not take a profile entity; core's own rule says so.
    const FaceName malformed{block.feature,
                             FaceSelector{.role = FaceRole::EndCap, .entity = block.lines[0]}};
    const Result<BoundaryFacetSet> set = meshing::boundaryFacetsOf(map, malformed);
    REQUIRE_FALSE(set.has_value());
    CHECK(set.error().code == ErrorCode::InvalidArgument);
    CHECK_THAT(set.error().message, ContainsSubstring("geometry_reference_invalid"));
}

// ---------------------------------------------------------------------------
// Facets, interior faces, nodes, owning elements, region
// ---------------------------------------------------------------------------

TEST_CASE("MapFacet_EveryTetrahedronFaceIsEitherBoundaryOrExplicitlyInterior",
          "[meshing][map][interior]") {
    // EXHAUSTIVE over the box's 12 tetrahedra and all 4 faces of each. An
    // interior face must have NO CAD source -- not the nearest one -- and the
    // number of boundary faces found must be exactly the mesh's own boundary
    // count, which also proves this layer's face-ordinal table agrees with the
    // one `tetrahedralBoundary` uses.
    Block block;
    const VolumeMesh mesh = block.mesh();
    const GeometryMeshMap map = block.map(mesh);

    std::size_t boundaryFaces = 0;
    std::size_t interiorFaces = 0;
    for (const Tetrahedron& tet : mesh.mesh().tetrahedra()) {
        for (std::size_t ordinal = 0; ordinal < 4; ++ordinal) {
            const Result<FacetSource> source =
                meshing::sourceFaceOf(map, mesh.mesh(), TetrahedronFace{tet.id, ordinal});
            if (source.has_value()) {
                ++boundaryFaces;
                CHECK(source->face < map.faces().size());
            } else {
                ++interiorFaces;
                CHECK_THAT(source.error().message,
                           ContainsSubstring("no_boundary_correspondence"));
                CHECK_THAT(source.error().message, ContainsSubstring("interior"));
            }
        }
    }
    CHECK(boundaryFaces == mesh.boundaryTriangleCount());
    CHECK(boundaryFaces + interiorFaces == mesh.mesh().tetrahedra().size() * 4);
    CHECK(interiorFaces > 0);
}

TEST_CASE("MapFacet_RefusesAHandleThatIsNotABoundaryTriangle", "[meshing][map][validation]") {
    Block block;
    const VolumeMesh mesh = block.mesh();
    const GeometryMeshMap map = block.map(mesh);

    // A tetrahedron's handle is not a facet's.
    const ElementId tet = mesh.mesh().tetrahedra().front().id;
    const Result<FacetSource> source = meshing::sourceFaceOf(map, tet);
    REQUIRE_FALSE(source.has_value());
    CHECK_THAT(source.error().message, ContainsSubstring("no_boundary_correspondence"));

    const Result<std::vector<NodeId>> nodes = meshing::boundaryNodesOf(
        map, mesh.mesh(), std::array<ElementId, 1>{ElementId::fromValue(99999)});
    REQUIRE_FALSE(nodes.has_value());
    CHECK_THAT(nodes.error().message, ContainsSubstring("mesh_facet_invalid"));

    const Result<FacetSource> badOrdinal =
        meshing::sourceFaceOf(map, mesh.mesh(), TetrahedronFace{tet, 4});
    REQUIRE_FALSE(badOrdinal.has_value());
    CHECK_THAT(badOrdinal.error().message, ContainsSubstring("mesh_facet_invalid"));
}

TEST_CASE("MapNodes_AreDerivedFromTheFacetsAndDeterministicallyOrdered",
          "[meshing][map][nodes]") {
    Round cylinder{8_mm, 20_mm};
    const VolumeMesh mesh = cylinder.mesh(curved(6_mm));
    const GeometryMeshMap map = cylinder.map(mesh);
    const BoundaryFacetSet top = facetsOf(map, cylinder.top());

    const Result<std::vector<NodeId>> nodes =
        meshing::boundaryNodesOf(map, mesh.mesh(), top.facets);
    REQUIRE(nodes.has_value());
    REQUIRE_FALSE(nodes->empty());
    CHECK(std::ranges::is_sorted(*nodes));
    CHECK(std::ranges::adjacent_find(*nodes) == nodes->end());

    // EVERY node exists, and every node belongs to at least one selected facet.
    std::set<NodeId> onFacets;
    for (const ElementId facet : top.facets) {
        const Triangle* triangle = mesh.mesh().findTriangle(facet);
        REQUIRE(triangle != nullptr);
        onFacets.insert(triangle->nodes.begin(), triangle->nodes.end());
    }
    for (const NodeId node : *nodes) {
        CHECK(mesh.mesh().findNode(node) != nullptr);
        CHECK(onFacets.contains(node));
    }
    CHECK(nodes->size() == onFacets.size());

    // And they are the nodes of the face: all at the cap's z.
    for (const NodeId node : *nodes) {
        CHECK_THAT(mesh.mesh().findNode(node)->position.z.si(), WithinRel(0.020, 1e-9));
    }
}

TEST_CASE("MapElements_EveryBoundaryFacetHasExactlyOneOwningTetrahedron",
          "[meshing][map][elements]") {
    Block block;
    const VolumeMesh mesh = block.mesh();
    const GeometryMeshMap map = block.map(mesh);

    const Result<std::vector<ElementId>> owners =
        meshing::owningTetrahedraOf(map, mesh.mesh(), allBoundaryFacets(mesh.mesh()));
    REQUIRE(owners.has_value());
    CHECK(std::ranges::is_sorted(*owners));
    CHECK(std::ranges::adjacent_find(*owners) == owners->end());
    for (const ElementId owner : *owners) {
        CHECK(mesh.mesh().findTetrahedron(owner) != nullptr);
    }

    // One face's owners are a subset, and each is a real tetrahedron.
    const BoundaryFacetSet top = facetsOf(map, block.top());
    const Result<std::vector<ElementId>> topOwners =
        meshing::owningTetrahedraOf(map, mesh.mesh(), top.facets);
    REQUIRE(topOwners.has_value());
    CHECK_FALSE(topOwners->empty());
    CHECK(topOwners->size() <= owners->size());
}

TEST_CASE("MapRegion_IsEveryTetrahedronOfTheOneSolid", "[meshing][map][region]") {
    Block block;
    const VolumeMesh mesh = block.mesh();
    const GeometryMeshMap map = block.map(mesh);

    const Result<MappedRegion> region = meshing::regionOf(map, mesh.mesh());
    REQUIRE(region.has_value());
    CHECK(region->source == block.feature);
    CHECK(region->region.isValid());
    CHECK(region->elements.size() == mesh.tetrahedronCount());
    CHECK(std::ranges::is_sorted(region->elements));

    // Element -> region, and a boundary triangle is NOT a volume element.
    const Result<ObjectId> source =
        meshing::regionSourceOf(map, mesh.mesh(), region->elements.front());
    REQUIRE(source.has_value());
    CHECK(*source == block.feature);

    const Result<ObjectId> notVolume =
        meshing::regionSourceOf(map, mesh.mesh(), mesh.mesh().triangles().front().id);
    REQUIRE_FALSE(notVolume.has_value());
    CHECK_THAT(notVolume.error().message, ContainsSubstring("mesh_facet_invalid"));
}

// ---------------------------------------------------------------------------
// MAP-LOCAL-SIZE: one reference contract, shared with P16-SIZE-001
// ---------------------------------------------------------------------------

TEST_CASE("MapLocalSize_UsesTheSameCanonicalReferenceAsSizing", "[meshing][map][sizing]") {
    // ONE FACE IDENTITY SYSTEM. The value handed to a local sizing control and
    // the value handed to a mapping query are the SAME `FaceName`, and both
    // resolve it through `geometry::findNamedFaces`. There is no second
    // selection mechanism to drift from this one.
    Round cylinder{8_mm, 20_mm};
    const FaceName target = cylinder.top();

    VolumeMeshControls controls = curved(6_mm);
    controls.sizing.local.push_back(meshing::LocalMeshSizing{.face = target, .targetSize = 1.5_mm});
    const VolumeMesh mesh = cylinder.mesh(controls);
    const GeometryMeshMap map = cylinder.map(mesh);

    // Sizing resolved that very reference...
    REQUIRE(mesh.sizing().local.size() == 1);
    CHECK(mesh.sizing().local.front().face == target);
    CHECK(mesh.sizing().local.front().state == meshing::SizingSelectionState::Resolved);
    // ...and the mapping resolves the same one.
    const BoundaryFacetSet set = facetsOf(map, target);
    REQUIRE(set.fullyResolved());
    CHECK(set.requested.front().reference == target);
    CHECK(mesh.sizing().local.front().face == set.requested.front().reference);
}

TEST_CASE("MapLocalSize_MapsTheFaceThatWasActuallyRefined", "[meshing][map][sizing]") {
    // The facets the mapping attributes to the refined face must be the ones in
    // the refined region: finer than the facets of the opposite face, which the
    // control did not name.
    Round cylinder{8_mm, 20_mm};
    VolumeMeshControls controls = curved(6_mm);
    controls.sizing.local.push_back(
        meshing::LocalMeshSizing{.face = cylinder.top(), .targetSize = 1.5_mm});
    const VolumeMesh mesh = cylinder.mesh(controls);
    const GeometryMeshMap map = cylinder.map(mesh);
    REQUIRE(map.report().complete());

    const BoundaryFacetSet top = facetsOf(map, cylinder.top());
    const BoundaryFacetSet bottom = facetsOf(map, cylinder.bottom());
    REQUIRE(top.fullyResolved());
    REQUIRE(bottom.fullyResolved());

    const auto meanEdge = [&mesh](const BoundaryFacetSet& set) {
        double total = 0.0;
        std::size_t count = 0;
        for (const ElementId facet : set.facets) {
            const std::array<Point3D, 3> p = cornersOf(mesh.mesh(), facet);
            for (std::size_t i = 0; i < 3; ++i) {
                total += norm(sub(of(p[(i + 1) % 3]), of(p[i])));
                ++count;
            }
        }
        return count == 0 ? 0.0 : total / static_cast<double>(count);
    };

    // Both discs are triangulated by the same surface mesher, so the SURFACE
    // facets are comparable; the refinement acts on the volume beneath. The
    // assertion is therefore about the volume elements the facets own.
    const Result<std::vector<ElementId>> topOwners =
        meshing::owningTetrahedraOf(map, mesh.mesh(), top.facets);
    const Result<std::vector<ElementId>> bottomOwners =
        meshing::owningTetrahedraOf(map, mesh.mesh(), bottom.facets);
    REQUIRE(topOwners.has_value());
    REQUIRE(bottomOwners.has_value());

    const auto meanTetEdge = [&mesh](const std::vector<ElementId>& tets) {
        static constexpr std::array<std::array<std::size_t, 2>, 6> kEdges{
            {{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3}}};
        double total = 0.0;
        std::size_t count = 0;
        for (const ElementId id : tets) {
            const Tetrahedron* tet = mesh.mesh().findTetrahedron(id);
            REQUIRE(tet != nullptr);
            for (const auto& edge : kEdges) {
                const Node* a = mesh.mesh().findNode(tet->nodes[edge[0]]);
                const Node* b = mesh.mesh().findNode(tet->nodes[edge[1]]);
                REQUIRE(a != nullptr);
                REQUIRE(b != nullptr);
                total += norm(sub(of(a->position), of(b->position)));
                ++count;
            }
        }
        return count == 0 ? 0.0 : total / static_cast<double>(count);
    };

    const double refined = meanTetEdge(*topOwners);
    const double coarse = meanTetEdge(*bottomOwners);
    INFO("refined " << refined * 1e3 << " mm vs unrefined " << coarse * 1e3 << " mm");
    CHECK(refined < coarse);
    CHECK(meanEdge(top) > 0.0);
}

// ---------------------------------------------------------------------------
// Named boundary sets
// ---------------------------------------------------------------------------

TEST_CASE("BoundarySet_KeepsItsIdentityAndIntentAcrossARemesh", "[meshing][map][sets]") {
    // THE AUTHORITY SEPARATION, demonstrated. What is stored is the geometry
    // selection; the facets are recomputed per mesh. This is what makes a P17
    // restraint survive a remesh.
    Round cylinder{8_mm, 20_mm};
    const NamedBoundarySet fixedEnd{BoundarySetId::fromValue(1), "fixed_end", {cylinder.bottom()}};
    REQUIRE(meshing::validate(fixedEnd).has_value());

    const VolumeMesh coarse = cylinder.mesh(curved(8_mm));
    const Result<ResolvedBoundarySet> onCoarse =
        meshing::resolveBoundarySet(fixedEnd, cylinder.map(coarse));
    REQUIRE(onCoarse.has_value());
    REQUIRE(onCoarse->fullyResolved());

    VolumeMeshControls fineControls = curved(2_mm);
    fineControls.surface.linearDeflection = Length::fromSi(2e-5);
    const VolumeMesh fine = cylinder.mesh(fineControls);
    const Result<ResolvedBoundarySet> onFine =
        meshing::resolveBoundarySet(fixedEnd, cylinder.map(fine));
    REQUIRE(onFine.has_value());
    REQUIRE(onFine->fullyResolved());

    // Identity and intent unchanged...
    CHECK(onFine->id == onCoarse->id);
    CHECK(onFine->name == onCoarse->name);
    CHECK(onFine->mapping.requested.front().reference ==
          onCoarse->mapping.requested.front().reference);
    // ...and the derived facets are NOT the same set, which is the point.
    CHECK(onFine->mapping.facets.size() > onCoarse->mapping.facets.size());
    CHECK(onFine->mapping.facets != onCoarse->mapping.facets);
}

TEST_CASE("BoundarySet_OfABoreMapsOnlyToTheBore", "[meshing][map][sets][hole]") {
    Round tube{12_mm, 20_mm, 6_mm};
    const NamedBoundarySet bolt{BoundarySetId::fromValue(7), "bolt_hole", {tube.bore()}};
    const VolumeMesh mesh = tube.mesh(curved(6_mm));
    const GeometryMeshMap map = tube.map(mesh);

    const Result<ResolvedBoundarySet> resolved = meshing::resolveBoundarySet(bolt, map);
    REQUIRE(resolved.has_value());
    REQUIRE(resolved->fullyResolved());
    const BoundaryFacetSet wall = facetsOf(map, tube.wall());
    for (const ElementId facet : resolved->mapping.facets) {
        CHECK(std::ranges::find(wall.facets, facet) == wall.facets.end());
        const Result<FacetSource> source = meshing::sourceFaceOf(map, facet);
        REQUIRE(source.has_value());
        CHECK(std::ranges::find(source->names, tube.bore()) != source->names.end());
    }
}

TEST_CASE("BoundarySet_MayHoldSeveralFacesAndResolvesToTheirUnion", "[meshing][map][sets]") {
    Block block;
    const VolumeMesh mesh = block.mesh();
    const GeometryMeshMap map = block.map(mesh);

    const NamedBoundarySet both{BoundarySetId::fromValue(2), "ends", {block.top(), block.bottom()}};
    const Result<ResolvedBoundarySet> resolved = meshing::resolveBoundarySet(both, map);
    REQUIRE(resolved.has_value());
    REQUIRE(resolved->fullyResolved());
    CHECK(resolved->mapping.requested.size() == 2);

    std::vector<ElementId> expected = facetsOf(map, block.top()).facets;
    const BoundaryFacetSet bottom = facetsOf(map, block.bottom());
    expected.insert(expected.end(), bottom.facets.begin(), bottom.facets.end());
    std::ranges::sort(expected);
    CHECK(resolved->mapping.facets == expected);
    CHECK(std::ranges::adjacent_find(resolved->mapping.facets) == resolved->mapping.facets.end());
}

TEST_CASE("BoundarySet_WithAnUnresolvableFaceStaysAndReportsIt", "[meshing][map][sets]") {
    // A set is not deleted because its geometry went away, and the surviving
    // faces still resolve. Partial resolution is reported per reference.
    Block block;
    const VolumeMesh mesh = block.mesh();
    const GeometryMeshMap map = block.map(mesh);

    const FaceName gone{block.feature, FaceSelector{.role = FaceRole::HoleBottom}};
    const NamedBoundarySet mixed{BoundarySetId::fromValue(3), "mixed", {block.top(), gone}};
    const Result<ResolvedBoundarySet> resolved = meshing::resolveBoundarySet(mixed, map);
    REQUIRE(resolved.has_value());
    CHECK(resolved->id == BoundarySetId::fromValue(3));
    CHECK(resolved->name == "mixed");
    CHECK_FALSE(resolved->fullyResolved());
    REQUIRE(resolved->mapping.requested.size() == 2);
    CHECK(resolved->mapping.requested[0].state == MappingState::Resolved);
    CHECK(resolved->mapping.requested[1].state == MappingState::Unresolved);
    CHECK_FALSE(resolved->mapping.facets.empty()); // the resolved half still works
}

TEST_CASE("BoundarySet_TwoSetsMayNameTheSameFaceAndAreNotMerged", "[meshing][map][sets]") {
    // Two different physics may refer to one surface. Deduplicating a user's
    // intent because the resolved facets are equal would be deciding for them.
    Block block;
    const VolumeMesh mesh = block.mesh();
    const GeometryMeshMap map = block.map(mesh);

    const NamedBoundarySet pressure{BoundarySetId::fromValue(4), "pressure", {block.top()}};
    const NamedBoundarySet heat{BoundarySetId::fromValue(5), "heat_flux", {block.top()}};
    const Result<ResolvedBoundarySet> a = meshing::resolveBoundarySet(pressure, map);
    const Result<ResolvedBoundarySet> b = meshing::resolveBoundarySet(heat, map);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(a->mapping.facets == b->mapping.facets);
    CHECK_FALSE(a->id == b->id);
    CHECK(a->name != b->name);
}

TEST_CASE("BoundarySet_NameIsNotIdentity", "[meshing][map][sets]") {
    Block block;
    const VolumeMesh mesh = block.mesh();
    const GeometryMeshMap map = block.map(mesh);

    // Two distinct sets with the SAME display name resolve independently;
    // neither replaces the other.
    const NamedBoundarySet first{BoundarySetId::fromValue(10), "fixed", {block.top()}};
    const NamedBoundarySet second{BoundarySetId::fromValue(11), "fixed", {block.bottom()}};
    const Result<ResolvedBoundarySet> a = meshing::resolveBoundarySet(first, map);
    const Result<ResolvedBoundarySet> b = meshing::resolveBoundarySet(second, map);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(a->name == b->name);
    CHECK_FALSE(a->id == b->id);
    CHECK(a->mapping.facets != b->mapping.facets);
}

TEST_CASE("BoundarySet_RefusesAMalformedSet", "[meshing][map][sets][validation]") {
    Block block;
    const FaceName top = block.top();

    CHECK_FALSE(meshing::validate(NamedBoundarySet{BoundarySetId{}, "x", {top}}).has_value());
    CHECK_FALSE(
        meshing::validate(NamedBoundarySet{BoundarySetId::fromValue(1), "", {top}}).has_value());
    CHECK_FALSE(
        meshing::validate(NamedBoundarySet{BoundarySetId::fromValue(1), "x", {}}).has_value());

    // The same face twice is a mistake, not a union.
    const Result<void> duplicate = meshing::validate(
        NamedBoundarySet{BoundarySetId::fromValue(1), "x", {top, top}});
    REQUIRE_FALSE(duplicate.has_value());
    CHECK_THAT(duplicate.error().message, ContainsSubstring("same reference"));

    // A malformed selector is refused by core's own rule.
    const FaceName malformed{block.feature,
                             FaceSelector{.role = FaceRole::EndCap, .entity = block.lines[0]}};
    CHECK_FALSE(meshing::validate(NamedBoundarySet{BoundarySetId::fromValue(1), "x", {malformed}})
                    .has_value());
}

// ---------------------------------------------------------------------------
// Independence: the mapping is about geometry and the mesh, and nothing else
// ---------------------------------------------------------------------------

TEST_CASE("Map_IsUnchangedByMaterialAssignment", "[meshing][map][independence]") {
    Block block;
    const VolumeMesh mesh = block.mesh();
    const GeometryMeshMap before = block.map(mesh);

    features::MaterialDefinition steel;
    steel.designation = "Synthetic";
    steel.mechanical.density = materials::MaterialProperty<Density>::known(Density::fromSi(7850.0));
    const Result<MaterialId> id = features::createMaterial(block.document, "Synthetic", steel);
    REQUIRE(id.has_value());
    REQUIRE(features::assignMaterial(block.document, *id).has_value());
    requireReport(block.regenerator, block.document);

    const GeometryMeshMap after = block.map(mesh);
    CHECK(after == before);
}

TEST_CASE("Map_IsUnchangedByEvaluatingMeshQuality", "[meshing][map][independence]") {
    // Quality is derived, read-only analysis. Running it must not move a facet
    // from one CAD face to another.
    Block block;
    const VolumeMesh mesh = block.mesh();
    const GeometryMeshMap before = block.map(mesh);

    const meshing::MeshQualityReport quality = meshing::evaluateMeshQuality(mesh.mesh());
    CHECK(quality.structurallyValid);

    const GeometryMeshMap after = block.map(mesh);
    CHECK(after == before);
}

TEST_CASE("Map_IsUnchangedByADisplayTriangulation", "[meshing][map][independence]") {
    // A viewer asks the kernel for its own tessellation at its own deflection.
    // Engineering correspondence must not notice.
    Block block;
    const VolumeMesh mesh = block.mesh();
    const GeometryMeshMap before = block.map(mesh);

    const Result<meshing::MeshableGeometry> prepared =
        meshing::requireMeshableGeometry(block.document, block.regenerator, block.feature);
    REQUIRE(prepared.has_value());
    const Result<geometry::Mesh> display =
        geometry::triangulate(prepared->body, {.linearDeflection = Length::fromSi(5e-3)});
    REQUIRE(display.has_value());
    CHECK_FALSE(display->triangles.empty());

    const GeometryMeshMap after = block.map(mesh);
    CHECK(after == before);
}

// ---------------------------------------------------------------------------
// Determinism
// ---------------------------------------------------------------------------

TEST_CASE("Map_IsIdenticalOnRepeatedConstruction", "[meshing][map][determinism]") {
    // The WHOLE map compared as a value: face order, names, facet lists, the
    // report and its issue order.
    Round tube{12_mm, 20_mm, 6_mm};
    const VolumeMesh mesh = tube.mesh(curved(6_mm));
    const GeometryMeshMap first = tube.map(mesh);
    for (int run = 0; run < 4; ++run) {
        CHECK(tube.map(mesh) == first);
    }

    // And so are the derived sets.
    const BoundaryFacetSet bore = facetsOf(first, tube.bore());
    for (int run = 0; run < 4; ++run) {
        CHECK(facetsOf(first, tube.bore()) == bore);
    }
    const Result<std::vector<NodeId>> nodes =
        meshing::boundaryNodesOf(first, mesh.mesh(), bore.facets);
    REQUIRE(nodes.has_value());
    for (int run = 0; run < 4; ++run) {
        CHECK(meshing::boundaryNodesOf(first, mesh.mesh(), bore.facets) == nodes);
    }
}

TEST_CASE("MapSurface_CadFaceMapsToItsEngineeringSurfaceTriangles",
          "[meshing][map][surface]") {
    // THE SURFACE DIRECTION, which is where the provenance is recorded. The
    // volume map is the solver-facing one, but a consumer of an engineering
    // SURFACE mesh gets the same attribution from the surface itself -- and it
    // partitions the surface exactly as the volume map partitions the boundary.
    Block block;
    const Result<meshing::MeshableGeometry> prepared =
        meshing::requireMeshableGeometry(block.document, block.regenerator, block.feature);
    REQUIRE(prepared.has_value());
    const Result<meshing::EngineeringSurfaceMesh> surface =
        meshing::surfaceMeshFor(block.document, block.regenerator, block.feature);
    REQUIRE(surface.has_value());
    const Result<std::vector<geometry::FaceInfo>> faces = geometry::listFaces(prepared->body);
    REQUIRE(faces.has_value());

    // One group per CAD face, lined up with listFaces().
    REQUIRE(surface->faceTriangles.size() == faces->size());
    REQUIRE(surface->faceCount == faces->size());

    std::vector<ElementId> union_;
    for (const std::vector<ElementId>& group : surface->faceTriangles) {
        CHECK_FALSE(group.empty());
        CHECK(std::ranges::is_sorted(group));
        union_.insert(union_.end(), group.begin(), group.end());
    }
    const std::size_t total = union_.size();
    std::ranges::sort(union_);
    union_.erase(std::ranges::unique(union_).begin(), union_.end());
    CHECK(union_.size() == total); // no triangle in two groups

    std::vector<ElementId> all;
    for (const Triangle& triangle : surface->mesh.triangles()) {
        all.push_back(triangle.id);
    }
    std::ranges::sort(all);
    CHECK(union_ == all);

    // And the attribution agrees with the geometry, face by face.
    for (std::size_t index = 0; index < faces->size(); ++index) {
        const geometry::FaceInfo& info = (*faces)[index];
        REQUIRE(info.signature.has_value());
        const Vec3 normal{info.signature->normal.x(), info.signature->normal.y(),
                          info.signature->normal.z()};
        const Vec3 base = of(info.signature->point);
        for (const ElementId facet : surface->faceTriangles[index]) {
            for (const Point3D& corner : cornersOf(surface->mesh, facet)) {
                CHECK(std::abs(dot(sub(of(corner), base), normal)) < kOnSurface);
            }
        }
    }
}

TEST_CASE("MapReport_AnEmptyMappingIsNotComplete", "[meshing][map][report]") {
    // THE ZERO-COVERAGE TRAP, the analogue of the backend reporting success
    // with no tetrahedra. A report with no facets at all must not pass: a
    // mapping that mapped nothing has not mapped everything.
    CHECK_FALSE(meshing::GeometryMeshMappingReport{}.complete());

    meshing::GeometryMeshMappingReport report;
    report.boundaryFacetCount = 10;
    report.mappedFacetCount = 10;
    CHECK(report.complete());
    report.unmappedFacetCount = 1;
    CHECK_FALSE(report.complete());
    report.unmappedFacetCount = 0;
    report.facesWithoutFacets = 1;
    CHECK_FALSE(report.complete());
    report.facesWithoutFacets = 0;
    report.facetsWithSeveralFaces = 1;
    CHECK_FALSE(report.complete());
}

TEST_CASE("Map_RefusesAnEmptyReferenceList", "[meshing][map][validation]") {
    Block block;
    const VolumeMesh mesh = block.mesh();
    const GeometryMeshMap map = block.map(mesh);
    const Result<BoundaryFacetSet> set =
        meshing::boundaryFacetsOf(map, std::span<const FaceName>{});
    REQUIRE_FALSE(set.has_value());
    CHECK(set.error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("MappingVocabulary_EveryStateAndIssueHasADistinctName", "[meshing][map]") {
    constexpr std::array states{MappingState::Resolved, MappingState::Unresolved};
    std::vector<std::string_view> names;
    for (const MappingState state : states) {
        CHECK_FALSE(meshing::toString(state).empty());
        names.push_back(meshing::toString(state));
    }
    constexpr std::array kinds{
        meshing::MappingIssueKind::GeometryReferenceInvalid,
        meshing::MappingIssueKind::GeometryReferenceUnresolved,
        meshing::MappingIssueKind::MappingStale,
        meshing::MappingIssueKind::MeshFacetInvalid,
        meshing::MappingIssueKind::NoBoundaryCorrespondence,
        meshing::MappingIssueKind::FaceWithoutFacets,
        meshing::MappingIssueKind::FacetAttributedTwice,
    };
    for (const meshing::MappingIssueKind kind : kinds) {
        CHECK_FALSE(meshing::toString(kind).empty());
        names.push_back(meshing::toString(kind));
    }
    std::ranges::sort(names);
    CHECK(std::ranges::adjacent_find(names) == names.end());
    CHECK(names.size() == states.size() + kinds.size());
}
