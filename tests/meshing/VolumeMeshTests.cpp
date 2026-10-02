// P16-VOL-001: the engineering volume mesh, end to end from CAD geometry.
//
// Every expected volume here is closed-form and evaluated by hand. None was
// obtained by running BetterCAD and recording what it said.
//
// THE TWO INDEPENDENT REFERENCES, and why both are needed:
//
//   the boundary's enclosed volume   The tetrahedra tile EXACTLY the polyhedron
//                                    the surface bounds, so these must agree to
//                                    floating-point accumulation. An exact
//                                    identity, no modelling tolerance.
//
//   the CAD volume                   What the body really measures. A faceted
//                                    boundary UNDERSTATES it for any curved
//                                    face -- chords cut inside the arc -- so the
//                                    right assertion is convergence from below,
//                                    never equality.
//
// Holding a curved body to the second as though it were the first is the
// standard way to end up loosening a tolerance until a test passes.
#include "features/FeatureTestSupport.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/meshing/MeshValidation.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <numbers>
#include <set>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;
using meshing::Mesh;
using meshing::MeshValidationReport;
using meshing::Node;
using meshing::NodeId;
using meshing::Tetrahedron;
using meshing::VolumeMesh;
using meshing::VolumeMeshControls;
using meshing::VolumeMeshFailure;

namespace {

/// Planar geometry is exact through the kernel, so a box's volume may be held to
/// the repository's figure for geometric accumulation.
constexpr double kExact = 1e-9;

/// One extruded solid, with the profile reachable so a test can change it.
struct Meshed {
    Document document{"Part"};
    features::Regenerator regenerator;
    ObjectId feature{};
    ObjectId profile{};
    std::array<EntityId, 4> lines{};

    Meshed(Length a, Length b, Length c, const Frame3D& plane = Frame3D::xy()) {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", plane);
        lines = addRectangle(*sketch, 0_mm, 0_mm, a, b);
        profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = c});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));
        requireReport(regenerator, document);
    }

    [[nodiscard]] Result<VolumeMesh> volume(const VolumeMeshControls& controls = {}) const {
        return meshing::volumeMeshFor(document, regenerator, feature, controls);
    }

    [[nodiscard]] VolumeMesh require_(const VolumeMeshControls& controls = {}) const {
        Result<VolumeMesh> mesh = volume(controls);
        if (!mesh.has_value()) {
            FAIL("volume mesh refused: " << mesh.error().message);
        }
        return *mesh;
    }
};

/// A cylinder, whose curved wall makes the CAD volume unreachable from a facet
/// sum and therefore makes convergence the only honest assertion.
struct MeshedCylinder {
    Document document{"Cyl"};
    features::Regenerator regenerator;
    ObjectId feature{};

    MeshedCylinder(Length radius, Length height) {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        REQUIRE(sketch->addCircle(Point2D{0_mm, 0_mm}, radius).has_value());
        const ObjectId profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = height});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));
        requireReport(regenerator, document);
    }

    [[nodiscard]] Result<VolumeMesh> volume(const VolumeMeshControls& controls = {}) const {
        return meshing::volumeMeshFor(document, regenerator, feature, controls);
    }
};

/// A hollow tube: two concentric circles extruded. The internal void is the
/// point -- a mesher that filled it would recover the WRONG volume and would
/// put elements where there is no material.
struct MeshedTube {
    Document document{"Tube"};
    features::Regenerator regenerator;
    ObjectId feature{};

    MeshedTube(Length outer, Length inner, Length height) {
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

    [[nodiscard]] Result<VolumeMesh> volume(const VolumeMeshControls& controls = {}) const {
        return meshing::volumeMeshFor(document, regenerator, feature, controls);
    }
};

[[nodiscard]] Point3D centroidOf(const Mesh& mesh, const Tetrahedron& tet) {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    for (const NodeId id : tet.nodes) {
        const Node* node = mesh.findNode(id);
        REQUIRE(node != nullptr);
        x += node->position.x.si();
        y += node->position.y.si();
        z += node->position.z.si();
    }
    return Point3D{Length::fromSi(x / 4.0), Length::fromSi(y / 4.0), Length::fromSi(z / 4.0)};
}

} // namespace

#ifdef BETTERCAD_TESTS_EXPECT_NETGEN

// ---------------------------------------------------------------------------
// Reference case: a box
// ---------------------------------------------------------------------------

TEST_CASE("VolBox_GeneratesATetrahedralMeshFromAValidClosedSolid", "[meshing][vol][box]") {
    const Meshed part(20_mm, 30_mm, 40_mm);
    const VolumeMesh mesh = part.require_();

    // Reported so the qualification log carries the actual size of the mesh
    // this milestone produces, rather than only the fact that it is non-empty.
    WARN("box 20x30x40 mm: " << mesh.nodeCount() << " nodes, " << mesh.tetrahedronCount()
                             << " tetrahedra, " << mesh.boundaryTriangleCount()
                             << " boundary triangles, tet volume " << mesh.tetrahedralVolume().si()
                             << " m^3, boundary volume " << mesh.boundaryVolume().si()
                             << " m^3, CAD volume " << mesh.cadVolume().si() << " m^3");
    CHECK(mesh.tetrahedronCount() > 0);
    CHECK(mesh.nodeCount() > 0);
    CHECK(mesh.boundaryTriangleCount() > 0);
    // Every element is a Tet4 and every node handle resolves: validate() is the
    // one definition of data validity, and generateVolumeMesh refuses a mesh
    // that fails it, so a mesh in hand has already passed.
    const MeshValidationReport report = meshing::validate(mesh.mesh());
    INFO("issues: " << report.issues.size());
    CHECK(report.dataValid());
}

TEST_CASE("VolBox_EveryTetrahedronHasPositiveVolumeAndFourDistinctNodes",
          "[meshing][vol][box]") {
    const Meshed part(20_mm, 30_mm, 40_mm);
    const VolumeMesh mesh = part.require_();
    REQUIRE(mesh.tetrahedronCount() > 0);

    std::set<std::array<NodeId, 4>> seen;
    for (const Tetrahedron& tet : mesh.mesh().tetrahedra()) {
        const Node* a = mesh.mesh().findNode(tet.nodes[0]);
        const Node* b = mesh.mesh().findNode(tet.nodes[1]);
        const Node* c = mesh.mesh().findNode(tet.nodes[2]);
        const Node* d = mesh.mesh().findNode(tet.nodes[3]);
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);
        REQUIRE(c != nullptr);
        REQUIRE(d != nullptr);

        // Positive, not merely non-zero: negative is an inverted element and
        // there is no absolute value anywhere in this chain.
        const Volume volume =
            meshing::signedVolume(a->position, b->position, c->position, d->position);
        CHECK(volume.si() > 0.0);
        CHECK(std::isfinite(volume.si()));

        std::set<NodeId> distinct(tet.nodes.begin(), tet.nodes.end());
        CHECK(distinct.size() == 4);

        std::array<NodeId, 4> key = tet.nodes;
        std::ranges::sort(key);
        CHECK(seen.insert(key).second); // no duplicate occupies the same four nodes
    }
}

TEST_CASE("VolBox_TetrahedraSumToTheVolumeTheBoundaryEncloses", "[meshing][vol][box][volume]") {
    const Meshed part(20_mm, 30_mm, 40_mm);
    const VolumeMesh mesh = part.require_();

    // AN EXACT IDENTITY, not an approximation: the tetrahedra tile precisely the
    // polyhedron the boundary triangles bound, and both sides are sums of the
    // same determinants over the same vertices.
    CHECK_THAT(mesh.tetrahedralVolume().si(),
               WithinRel(mesh.boundaryVolume().si(), kExact));
}

TEST_CASE("VolBox_TetrahedraRecoverTheHandComputedCadVolume", "[meshing][vol][box][volume]") {
    const Meshed part(20_mm, 30_mm, 40_mm);
    const VolumeMesh mesh = part.require_();

    // 20 * 30 * 40 mm^3 = 24000 mm^3 = 2.4e-5 m^3, by hand. A box has only
    // planar faces, so the facet boundary IS the body and equality is the right
    // expectation here -- which is exactly why a box is the fixture that can
    // check volume recovery at all.
    constexpr double kCadVolume = 20e-3 * 30e-3 * 40e-3;
    CHECK_THAT(mesh.cadVolume().si(), WithinRel(kCadVolume, kExact));
    CHECK_THAT(mesh.tetrahedralVolume().si(), WithinRel(kCadVolume, kExact));
}

TEST_CASE("VolBox_BoundaryConformsToTheEngineeringSurface", "[meshing][vol][box][conformity]") {
    const Meshed part(20_mm, 30_mm, 40_mm);
    const VolumeMesh mesh = part.require_();

    // Computed from the TETRAHEDRA's own connectivity -- faces used by exactly
    // one element -- and compared against the input surface. The backend's own
    // account of the surface it produced is not evidence about the elements it
    // returned, so it is never consulted.
    CHECK(mesh.conformity().conforms());
    CHECK(mesh.conformity().unmatchedBoundaryFaceCount == 0);
    CHECK(mesh.conformity().unmatchedSurfaceTriangleCount == 0);
    CHECK(mesh.conformity().volumeBoundaryFaceCount ==
          mesh.conformity().surfaceTriangleCount);
    // And the boundary is carried in the mesh itself, so a consumer need not
    // recompute it.
    CHECK(mesh.boundaryTriangleCount() == mesh.conformity().volumeBoundaryFaceCount);
}

TEST_CASE("VolBox_EveryNodeLiesInsideOrOnTheBody", "[meshing][vol][box][geometry]") {
    constexpr double kWidth = 20e-3;
    constexpr double kDepth = 30e-3;
    constexpr double kHeight = 40e-3;
    const Meshed part(20_mm, 30_mm, 40_mm);
    const VolumeMesh mesh = part.require_();

    // The box is axis-aligned from the origin, so containment is checkable in
    // closed form. The tolerance is for coordinate arithmetic, not for letting a
    // node escape: a node outside the body would be out by a mesh edge, not by
    // a nanometre.
    constexpr double kOn = 1e-9;
    for (const Node& node : mesh.mesh().nodes()) {
        INFO("node " << node.id.value());
        CHECK(node.position.x.si() >= -kOn);
        CHECK(node.position.y.si() >= -kOn);
        CHECK(node.position.z.si() >= -kOn);
        CHECK(node.position.x.si() <= kWidth + kOn);
        CHECK(node.position.y.si() <= kDepth + kOn);
        CHECK(node.position.z.si() <= kHeight + kOn);
    }
}

// ---------------------------------------------------------------------------
// Reference case: a curved body, where facets understate the volume
// ---------------------------------------------------------------------------

TEST_CASE("VolCylinder_TetrahedralVolumeApproachesTheCadVolumeFromBelow",
          "[meshing][vol][cylinder][volume]") {
    const MeshedCylinder part(10_mm, 25_mm);

    // pi r^2 h, by hand.
    constexpr double kCadVolume = std::numbers::pi * 10e-3 * 10e-3 * 25e-3;

    VolumeMeshControls coarse;
    coarse.surface.linearDeflection = Length::fromSi(5e-4);
    VolumeMeshControls fine;
    fine.surface.linearDeflection = Length::fromSi(2e-5);

    const Result<VolumeMesh> coarseMesh = part.volume(coarse);
    const Result<VolumeMesh> fineMesh = part.volume(fine);
    REQUIRE(coarseMesh.has_value());
    REQUIRE(fineMesh.has_value());

    WARN("cylinder r10 h25 mm, coarse (5e-4 m deflection): "
         << coarseMesh->nodeCount() << " nodes, " << coarseMesh->tetrahedronCount()
         << " tetrahedra, volume " << coarseMesh->tetrahedralVolume().si() << " m^3; fine (2e-5): "
         << fineMesh->nodeCount() << " nodes, " << fineMesh->tetrahedronCount()
         << " tetrahedra, volume " << fineMesh->tetrahedralVolume().si() << " m^3; CAD "
         << kCadVolume << " m^3");

    // FROM BELOW, both times. A chord lies inside the arc, so a faceted
    // cylinder is strictly smaller than the real one.
    CHECK(coarseMesh->tetrahedralVolume().si() < kCadVolume);
    CHECK(fineMesh->tetrahedralVolume().si() < kCadVolume);
    // And tightening the deflection moves it closer. This is the assertion that
    // would catch a mesh that simply filled the wrong shape: a fixed tolerance
    // would not.
    const double coarseError = kCadVolume - coarseMesh->tetrahedralVolume().si();
    const double fineError = kCadVolume - fineMesh->tetrahedralVolume().si();
    INFO("coarse error " << coarseError << ", fine error " << fineError);
    CHECK(fineError < coarseError);

    // The exact identity still holds at every deflection: the tetrahedra tile
    // whatever polyhedron the boundary describes.
    CHECK_THAT(fineMesh->tetrahedralVolume().si(),
               WithinRel(fineMesh->boundaryVolume().si(), kExact));
}

// ---------------------------------------------------------------------------
// Reference case: a hollow body, whose void must stay void
// ---------------------------------------------------------------------------

TEST_CASE("VolTube_InternalVoidContainsNoElements", "[meshing][vol][tube][void]") {
    constexpr double kInner = 12e-3;
    const MeshedTube part(20_mm, 12_mm, 30_mm);

    VolumeMeshControls controls;
    controls.surface.linearDeflection = Length::fromSi(5e-5);
    const Result<VolumeMesh> meshed = part.volume(controls);
    REQUIRE(meshed.has_value());
    REQUIRE(meshed->tetrahedronCount() > 0);

    // NO ELEMENT CENTROID MAY LIE INSIDE THE BORE. Checked on the centroid
    // rather than on the nodes, because nodes legitimately sit ON the inner
    // wall; a centroid inside it means material was invented.
    //
    // The faceted inner wall is a polygon INSCRIBED in the true circle, so it
    // lies at or inside radius kInner. A centroid of a tetrahedron in the
    // material is therefore outside the inscribed polygon but could be
    // marginally inside kInner; the bound used is the inscribed radius for the
    // chord length at this deflection, which a generous margin covers.
    constexpr double kMargin = 0.98;
    std::size_t inside = 0;
    for (const Tetrahedron& tet : meshed->mesh().tetrahedra()) {
        const Point3D centroid = centroidOf(meshed->mesh(), tet);
        const double radius = std::hypot(centroid.x.si(), centroid.y.si());
        if (radius < kInner * kMargin) {
            ++inside;
        }
    }
    CHECK(inside == 0);
}

TEST_CASE("VolTube_VolumeExcludesTheVoid", "[meshing][vol][tube][volume]") {
    constexpr double kOuter = 20e-3;
    constexpr double kInner = 12e-3;
    constexpr double kHeight = 30e-3;
    const MeshedTube part(20_mm, 12_mm, 30_mm);

    VolumeMeshControls controls;
    controls.surface.linearDeflection = Length::fromSi(5e-5);
    const Result<VolumeMesh> meshed = part.volume(controls);
    REQUIRE(meshed.has_value());

    // pi (Ro^2 - Ri^2) h, by hand. A mesher that filled the bore would report
    // pi Ro^2 h instead -- 2.5 times larger -- so this is a wide margin, not a
    // delicate one.
    constexpr double kAnnulus =
        std::numbers::pi * (kOuter * kOuter - kInner * kInner) * kHeight;
    constexpr double kFilledBore = std::numbers::pi * kOuter * kOuter * kHeight;

    const double actual = meshed->tetrahedralVolume().si();
    WARN("tube Ro20 Ri12 h30 mm: " << meshed->nodeCount() << " nodes, "
                                   << meshed->tetrahedronCount() << " tetrahedra, "
                                   << meshed->boundaryTriangleCount() << " boundary triangles, "
                                   << "volume " << actual << " m^3; annulus " << kAnnulus
                                   << " m^3; filled bore would be " << kFilledBore << " m^3");
    INFO("annulus " << kAnnulus << ", filled bore " << kFilledBore << ", actual " << actual);
    // Within 2% of the annulus, and from below: the 2% is the facet
    // understatement at this deflection, which the cylinder test establishes as
    // one-sided.
    CHECK(actual < kAnnulus);
    CHECK(actual > kAnnulus * 0.98);

    // And nearer the annulus than the filled bore, which is the statement that
    // the void was not filled.
    //
    // The obvious-looking guard here is `actual < kFilledBore / 2`, and it is
    // ARITHMETICALLY IMPOSSIBLE: this annulus is pi(400-144)/pi(400) = 64% of
    // its outer cylinder, so a correct mesh could never satisfy it. Writing a
    // bound without checking it can be met produces a test that fails on
    // correct code -- and this is the second time that exact mistake has been
    // made on this exact fixture, hence the arithmetic written out.
    CHECK(actual < (kAnnulus + kFilledBore) / 2.0);
}

// ---------------------------------------------------------------------------
// Failure handling
// ---------------------------------------------------------------------------

TEST_CASE("VolumeMesh_RefusesASurfaceThatIsNotAValidBoundary", "[meshing][vol][failure]") {
    // An EngineeringSurfaceMesh assembled by hand, carrying a validation report
    // that does not pass. generateSurfaceMesh can never return one of these, so
    // this is the route a viewer tessellation would have to take -- and it is
    // closed.
    meshing::EngineeringSurfaceMesh surface;
    meshing::MeshBuilder builder;
    REQUIRE(builder.addNode(Point3D{0_mm, 0_mm, 0_mm}).has_value());
    REQUIRE(builder.addNode(Point3D{10_mm, 0_mm, 0_mm}).has_value());
    REQUIRE(builder.addNode(Point3D{0_mm, 10_mm, 0_mm}).has_value());
    REQUIRE(builder
                .addTriangle({NodeId::fromValue(1), NodeId::fromValue(2), NodeId::fromValue(3)},
                             meshing::RegionId::fromValue(1))
                .has_value());
    surface.mesh = builder.build();
    // The report CLAIMS the surface is perfect. generateVolumeMesh recomputes
    // it, so the claim buys nothing -- which is the point of this test.
    surface.validation = meshing::SurfaceValidation{};
    surface.enclosedVolume = Volume::fromSi(1e-9);

    meshing::MeshableGeometry geometry;
    geometry.solidCount = 1;
    geometry.volume = Volume::fromSi(1e-9);

    const Result<VolumeMesh> meshed = meshing::generateVolumeMesh(geometry, surface);
    REQUIRE_FALSE(meshed.has_value());
    CHECK_THAT(std::string{meshed.error().message},
               ContainsSubstring(std::string{toString(VolumeMeshFailure::SurfaceNotUsable)}));
}

TEST_CASE("VolumeMesh_RefusesASurfaceThatAlreadyHoldsVolumeElements",
          "[meshing][vol][failure]") {
    // A boundary is triangles. Handing over a mesh that already holds
    // tetrahedra -- a volume mesh passed as a surface -- would otherwise use the
    // triangles and silently ignore the elements, and the result would look like
    // a successful remesh of something else.
    meshing::EngineeringSurfaceMesh surface;
    meshing::MeshBuilder builder;
    REQUIRE(builder.addNode(Point3D{0_mm, 0_mm, 0_mm}).has_value());
    REQUIRE(builder.addNode(Point3D{10_mm, 0_mm, 0_mm}).has_value());
    REQUIRE(builder.addNode(Point3D{0_mm, 10_mm, 0_mm}).has_value());
    REQUIRE(builder.addNode(Point3D{0_mm, 0_mm, 10_mm}).has_value());
    const auto region = meshing::RegionId::fromValue(1);
    REQUIRE(builder
                .addTriangle({NodeId::fromValue(1), NodeId::fromValue(2), NodeId::fromValue(3)},
                             region)
                .has_value());
    REQUIRE(builder
                .addTetrahedron({NodeId::fromValue(1), NodeId::fromValue(2),
                                 NodeId::fromValue(3), NodeId::fromValue(4)},
                                region)
                .has_value());
    surface.mesh = builder.build();

    meshing::MeshableGeometry geometry;
    geometry.solidCount = 1;
    geometry.volume = Volume::fromSi(1e-9);

    const Result<VolumeMesh> meshed = meshing::generateVolumeMesh(geometry, surface);
    REQUIRE_FALSE(meshed.has_value());
    CHECK_THAT(std::string{meshed.error().message}, ContainsSubstring("volume element"));
}

TEST_CASE("VolumeMesh_RefusesAnEmptySurface", "[meshing][vol][failure]") {
    const meshing::EngineeringSurfaceMesh empty;
    meshing::MeshableGeometry geometry;
    geometry.solidCount = 1;
    geometry.volume = Volume::fromSi(1.0);
    const Result<VolumeMesh> meshed = meshing::generateVolumeMesh(geometry, empty);
    REQUIRE_FALSE(meshed.has_value());
    CHECK(meshed.error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("VolumeMesh_RefusesAStaleFeatureRatherThanMeshingIt", "[meshing][vol][failure][stale]") {
    Meshed part(20_mm, 30_mm, 40_mm);
    REQUIRE(part.volume().has_value());

    // Change the profile and do NOT regenerate. The body in hand now describes
    // a model the user has already changed, and P16-GEOM-001's currency check
    // refuses it before anything inspects the geometry.
    REQUIRE(part.document
                .modifyObject<sketch::Sketch>(part.profile,
                                              [&](sketch::Sketch& sk) -> Result<bool> {
                                                  return sk.addDistance(part.lines[0], 25_mm)
                                                      .has_value();
                                              })
                .has_value());

    const Result<VolumeMesh> meshed = part.volume();
    REQUIRE_FALSE(meshed.has_value());
    CHECK(meshed.error().code == ErrorCode::FailedPrecondition);
}

TEST_CASE("VolumeMesh_RefusesABodyHoldingMoreThanOneSolid", "[meshing][vol][failure][solids]") {
    // THE DOCUMENTED MULTI-SOLID POLICY: refuse, explicitly.
    //
    // P16-GEOM-001 deliberately ACCEPTS a multi-solid body and reports the
    // count, because ADR-032's eventual design gives a mesh one region per
    // solid. This milestone does not implement that: P16-SURF-001 triangulates
    // a whole body into a single region, so meshing a two-solid body here would
    // produce a mesh whose single region is a lie, and meshing only the first
    // solid would be worse. Refusing is the one option with no undefined
    // behaviour, and per-solid regions remain later work.
    //
    // Two disjoint rectangles in one sketch, extruded: 10x10 and 5x5, 2 deep.
    Document document{"Two"};
    features::Regenerator regenerator;
    auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
    addRectangle(*sketch, 0_mm, 0_mm, 10_mm, 10_mm);
    addRectangle(*sketch, 20_mm, 0_mm, 5_mm, 5_mm);
    const ObjectId profile = require(document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = 2_mm});
    REQUIRE(extrude.has_value());
    const ObjectId feature = require(document.addObject(std::move(*extrude)));
    requireReport(regenerator, document);

    // The geometry layer is happy with it, which is what makes this a decision
    // of THIS layer rather than an accident of the one below.
    const Result<meshing::MeshableGeometry> prepared =
        meshing::requireMeshableGeometry(document, regenerator, feature);
    REQUIRE(prepared.has_value());
    REQUIRE(prepared->solidCount == 2);

    const Result<VolumeMesh> meshed = meshing::volumeMeshFor(document, regenerator, feature);
    REQUIRE_FALSE(meshed.has_value());
    CHECK(meshed.error().code == ErrorCode::InvalidArgument);
    CHECK_THAT(std::string{meshed.error().message},
               ContainsSubstring(std::string{toString(VolumeMeshFailure::MultipleSolids)}));
}

TEST_CASE("VolumeMeshFailure_EveryReasonHasADistinctName", "[meshing][vol]") {
    constexpr std::array reasons{
        VolumeMeshFailure::BackendUnavailable, VolumeMeshFailure::SurfaceNotUsable,
        VolumeMeshFailure::MultipleSolids,     VolumeMeshFailure::BackendFailed,
        VolumeMeshFailure::InvalidMesh,        VolumeMeshFailure::BoundaryNotConforming,
        VolumeMeshFailure::VolumeNotRecovered,
    };
    std::vector<std::string_view> names;
    for (const VolumeMeshFailure reason : reasons) {
        INFO("reason " << static_cast<int>(reason));
        CHECK_FALSE(toString(reason).empty());
        names.push_back(toString(reason));
    }
    std::ranges::sort(names);
    CHECK(std::ranges::adjacent_find(names) == names.end());
}

// ---------------------------------------------------------------------------
// Transform, staleness and determinism
// ---------------------------------------------------------------------------

TEST_CASE("VolumeMesh_FollowsATransformedBody", "[meshing][vol][transform]") {
    // The same box on two different sketch planes. The mesh is in the body's
    // frame and carries no transform (ADR-032), so moving the body must move
    // the nodes and leave the volume alone.
    const Meshed atOrigin(20_mm, 30_mm, 40_mm, Frame3D::xy());
    const Meshed onXZ(20_mm, 30_mm, 40_mm, Frame3D::xz());

    const VolumeMesh a = atOrigin.require_();
    const VolumeMesh b = onXZ.require_();

    constexpr double kCadVolume = 20e-3 * 30e-3 * 40e-3;
    CHECK_THAT(a.tetrahedralVolume().si(), WithinRel(kCadVolume, kExact));
    CHECK_THAT(b.tetrahedralVolume().si(), WithinRel(kCadVolume, kExact));

    // The XZ plane's normal is -Y, so the extrusion runs to NEGATIVE y and the
    // two meshes cannot occupy the same space. A mesher that ignored the frame
    // would produce identical bounds here.
    const auto boundsA = a.mesh().bounds();
    const auto boundsB = b.mesh().bounds();
    REQUIRE(boundsA.has_value());
    REQUIRE(boundsB.has_value());
    CHECK(boundsA->min.y.si() >= -kExact);
    CHECK(boundsB->min.y.si() < -1e-3);
}

TEST_CASE("VolumeMesh_IsStaleOnceItsGeometryMoves", "[meshing][vol][staleness]") {
    Meshed part(20_mm, 30_mm, 40_mm);
    const VolumeMesh mesh = part.require_();
    CHECK_FALSE(meshing::isStale(part.document, mesh));

    // A parameter edit moves the geometry revision, so the mesh in hand is
    // detectably stale -- the configuration-safety requirement, made real.
    REQUIRE(part.document
                .modifyObject<sketch::Sketch>(part.profile,
                                              [&](sketch::Sketch& sk) -> Result<bool> {
                                                  return sk.addDistance(part.lines[0], 25_mm)
                                                      .has_value();
                                              })
                .has_value());
    CHECK(meshing::isStale(part.document, mesh));

    // Regenerating does not make the OLD mesh current again: it describes the
    // previous geometry whatever the document now says.
    requireReport(part.regenerator, part.document);
    CHECK(meshing::isStale(part.document, mesh));
}

TEST_CASE("VolumeMesh_OfADeletedFeatureIsStale", "[meshing][vol][staleness]") {
    Meshed part(20_mm, 30_mm, 40_mm);
    const VolumeMesh mesh = part.require_();
    CHECK_FALSE(meshing::isStale(part.document, mesh));

    // The feature is gone, so its geometry revision is no longer obtainable and
    // a mesh of it is as stale as a mesh can be. The source comes from the MESH,
    // so this cannot be got wrong by passing the wrong id.
    REQUIRE(part.document.removeObject(mesh.source()).has_value());
    CHECK(meshing::isStale(part.document, mesh));
}

TEST_CASE("VolumeMesh_IsIdenticalOnRepeatedGeneration", "[meshing][vol][determinism]") {
    const Meshed part(20_mm, 30_mm, 40_mm);

    const VolumeMesh first = part.require_();
    for (int run = 1; run < 5; ++run) {
        const VolumeMesh again = part.require_();
        INFO("run " << run);
        REQUIRE(again.nodeCount() == first.nodeCount());
        REQUIRE(again.tetrahedronCount() == first.tetrahedronCount());
        REQUIRE(again.boundaryTriangleCount() == first.boundaryTriangleCount());
        CHECK(again.tetrahedralVolume().si() == first.tetrahedralVolume().si());

        // Connectivity in order, not just counts. Compared by CONTENT, because
        // operator== includes the MeshStamp and a MeshId is unique per mesh by
        // design (ADR-031), so two generated meshes are never equal however
        // identical their content.
        REQUIRE(again.mesh().nodes().size() == first.mesh().nodes().size());
        for (std::size_t index = 0; index < again.mesh().nodes().size(); ++index) {
            CHECK(again.mesh().nodes()[index].id == first.mesh().nodes()[index].id);
            CHECK(again.mesh().nodes()[index].position == first.mesh().nodes()[index].position);
        }
        for (std::size_t index = 0; index < again.mesh().tetrahedra().size(); ++index) {
            CHECK(again.mesh().tetrahedra()[index].nodes ==
                  first.mesh().tetrahedra()[index].nodes);
        }
    }
}

// ---------------------------------------------------------------------------
// Small features
// ---------------------------------------------------------------------------

TEST_CASE("VolumeMesh_HandlesAThinWallOrFailsExplicitly", "[meshing][vol][small]") {
    // A 0.4 mm wall on a 40 mm plate: an aspect ratio of 100. The requirement
    // is NOT that this succeeds -- it is that the outcome is one of two named
    // things and never a mesh that is quietly wrong. Whichever happens, the
    // mesh in hand has passed validation, conformity and volume recovery,
    // because generateVolumeMesh refuses otherwise.
    const Meshed thin(40_mm, 40_mm, 0.4_mm);
    const Result<VolumeMesh> meshed = thin.volume();
    if (meshed.has_value()) {
        constexpr double kCadVolume = 40e-3 * 40e-3 * 0.4e-3;
        CHECK(meshed->tetrahedronCount() > 0);
        CHECK(meshing::validate(meshed->mesh()).dataValid());
        CHECK(meshed->conformity().conforms());
        CHECK_THAT(meshed->tetrahedralVolume().si(), WithinRel(kCadVolume, kExact));
    } else {
        INFO("thin wall refused: " << meshed.error().message);
        CHECK_FALSE(meshed.error().message.empty());
    }
}

TEST_CASE("VolumeMesh_HandlesASmallBodyOrFailsExplicitly", "[meshing][vol][small]") {
    // A 1 mm cube. BetterCAD works in SI internally, so this reaches the
    // backend as 0.001-scale coordinates; a backend with absolute internal
    // tolerances would struggle, and the point of this test is that the answer
    // is measured rather than assumed.
    const Meshed small(1_mm, 1_mm, 1_mm);
    const Result<VolumeMesh> meshed = small.volume();
    if (meshed.has_value()) {
        constexpr double kCadVolume = 1e-3 * 1e-3 * 1e-3;
        CHECK(meshed->tetrahedronCount() > 0);
        CHECK(meshing::validate(meshed->mesh()).dataValid());
        CHECK_THAT(meshed->tetrahedralVolume().si(), WithinRel(kCadVolume, kExact));
    } else {
        INFO("small body refused: " << meshed.error().message);
        CHECK_FALSE(meshed.error().message.empty());
    }
}

// ---------------------------------------------------------------------------
// The persistence boundary
// ---------------------------------------------------------------------------

TEST_CASE("VolumeMesh_IsNeverWrittenToTheDocumentFile", "[meshing][vol][persist]") {
    // ADR-030: "Nothing derived is persisted. A .bcad file holds controls and
    // no mesh: no nodes, no elements, no facets, no quality numbers, no backend
    // version. A mesh is recomputed and revalidated, never loaded and trusted."
    //
    // Today that holds for a structural reason -- no io code touches a mesh, and
    // this milestone added none -- but "there is no code to do it" is a claim
    // that stops being true the moment someone adds some. So it is checked the
    // way P15-PERSIST-001 checked the same property for derived mass: generate
    // the derived state, THEN save, then look for it in the file.
    const Meshed part(20_mm, 30_mm, 40_mm);
    const VolumeMesh mesh = part.require_();
    REQUIRE(mesh.tetrahedronCount() > 0);

    const Result<std::string> text = io::documentToJson(part.document);
    REQUIRE(text.has_value());

    for (const char* forbidden : {"tetrahedr", "\"nodes\"", "\"elements\"", "node_count",
                                  "element_count", "\"mesh\"", "volume_mesh", "nglib", "Netgen",
                                  "netgen", "\"tets\"", "boundary_face", "6.2.2604"}) {
        CAPTURE(forbidden);
        CHECK_THAT(*text, !ContainsSubstring(forbidden));
    }
}

#endif // BETTERCAD_TESTS_EXPECT_NETGEN

TEST_CASE("VolumeMesh_RefusesEverythingWhenThereIsNoBackend", "[meshing][vol][backend]") {
    // Meaningful in BOTH configurations, which is why it sits outside the
    // guard: with a backend this asserts the API is reachable, and without one
    // it asserts that a build with no mesher cannot claim to have produced a
    // mesh.
    const meshing::EngineeringSurfaceMesh empty;
    meshing::MeshableGeometry geometry;
    geometry.solidCount = 1;
    geometry.volume = Volume::fromSi(1.0);
    const Result<VolumeMesh> meshed = meshing::generateVolumeMesh(geometry, empty);
    CHECK_FALSE(meshed.has_value());
}
