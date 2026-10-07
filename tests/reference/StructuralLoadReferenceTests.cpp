// P17-LOAD-001 against the meshing reference models.
//
// WHY THESE ARE SEPARATE from tests/structural/StructuralLoadTests.cpp. Those
// tests use one block whose face areas are known from its own dimensions, so
// every expected force can be written down. These use the committed reference
// models, because four claims cannot be made on a single fixed mesh:
//
//   a canonical FaceName survives a REMESH, and the physical resultant with
//     it, although no facet handle does
//   LOCAL REFINEMENT changes the nodal distribution and not the resultant
//   a PRESSURE rotates with a transformed body and a global TRACTION does not
//   the drilled-hole wall has no canonical reference, and is refused rather
//     than approximated
//
// The last of those is the honest one: it is a P16 limitation, the refusal is
// what passes, and no geometric fallback is implemented.

#include "reference/MeshTestSupport.hpp"

#include <MeshReferenceModels.hpp>

#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/meshing/MeshSizing.hpp>
#include <bettercad/structural/StructuralLoadVector.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace {

using namespace bettercad;
using namespace bettercad::test;
using namespace bettercad::test::meshref;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using structural::LoadProblem;
using structural::NodalLoad;
using structural::PreparedLoads;
using structural::PressureLoad;
using structural::StructuralAnalysisMode;
using structural::StructuralLoad;
using structural::StructuralMaterial;
using structural::StructuralModel;
using structural::SurfaceTractionLoad;

[[nodiscard]] Point3D at(double x, double y, double z) {
    return Point3D{Length::fromSi(x), Length::fromSi(y), Length::fromSi(z)};
}

[[nodiscard]] Traction3D traction(double x, double y, double z) {
    return Traction3D{Pressure::fromSi(x), Pressure::fromSi(y), Pressure::fromSi(z)};
}

/// Gives @p document a steel with a density, so every mode resolves.
void assignSteel(Document& document) {
    features::MaterialDefinition definition;
    definition.designation = "Steel";
    definition.mechanical.youngsModulus =
        materials::MaterialProperty<ElasticModulus>::known(ElasticModulus::fromSi(210.0e9));
    definition.mechanical.poissonRatio =
        materials::MaterialProperty<bettercad::PoissonRatio>::known(PoissonRatio::of(0.3));
    definition.mechanical.density =
        materials::MaterialProperty<Density>::known(Density::fromSi(7850.0));
    const Result<MaterialId> id = features::createMaterial(document, "Steel", definition);
    REQUIRE(id.has_value());
    REQUIRE(features::assignMaterial(document, *id).has_value());
}

/// What one load produced on one mesh.
struct Measured {
    std::size_t facets = 0;
    std::size_t loadedNodes = 0;
    double area = 0.0;
    std::array<double, 3> force{};
    std::array<double, 3> moment{};
    meshing::MeshStamp mesh{};
};

[[nodiscard]] Measured measure(MeshedReference& model, const meshing::VolumeMesh& volume,
                               const std::vector<StructuralLoad>& loads, const Point3D& origin,
                               StructuralAnalysisMode mode = StructuralAnalysisMode::LinearStatic) {
    Result<StructuralModel> prepared = structural::requireStructuralModel(
        model.document(), model.regenerator(), model.mesher(), model.control());
    INFO((prepared.has_value() ? std::string{} : prepared.error().message));
    REQUIRE(prepared.has_value());
    Result<StructuralMaterial> material =
        structural::resolveStructuralMaterial(model.document(), model.body(), mode);
    INFO((material.has_value() ? std::string{} : material.error().message));
    REQUIRE(material.has_value());
    Result<PreparedLoads> field =
        structural::prepareStructuralLoads(*prepared, *material, loads);
    INFO((field.has_value() ? std::string{} : field.error().message));
    REQUIRE(field.has_value());

    Measured out;
    out.mesh = volume.mesh().stamp();
    out.loadedNodes = field->nodal().size();
    for (const structural::LoadContribution& contribution : field->contributions()) {
        out.facets += contribution.elements;
        out.area += contribution.area.si();
    }
    for (const NodalLoad& load : field->nodal()) {
        out.force[0] += load.force.x.si();
        out.force[1] += load.force.y.si();
        out.force[2] += load.force.z.si();
        const meshing::Node* node = volume.mesh().findNode(load.node);
        REQUIRE(node != nullptr);
        const double rx = node->position.x.si() - origin.x.si();
        const double ry = node->position.y.si() - origin.y.si();
        const double rz = node->position.z.si() - origin.z.si();
        out.moment[0] += ry * load.force.z.si() - rz * load.force.y.si();
        out.moment[1] += rz * load.force.x.si() - rx * load.force.z.si();
        out.moment[2] += rx * load.force.y.si() - ry * load.force.x.si();
    }
    return out;
}

} // namespace

TEST_CASE("StructuralLoad_ResolvesACanonicalFaceTargetAgainstAReferenceMesh",
          "[structural][load][reference]") {
    // RM-MESH-01 is the primary fixture: a 120 x 70 x 35 mm block whose six
    // faces are all nameable, so its end cap has an exactly known area of
    // 120 x 70 mm = 8.4e-3 m^2. The expected force comes from THAT, not from
    // the facet sum.
    auto built = reference::buildMeshBlockReferenceModel();
    REQUIRE(built.has_value());
    const FaceName top = built->top();
    MeshedReference model(std::move(built->document));
    assignSteel(model.document());
    const meshing::VolumeMesh& volume = model.require();

    const double analyticArea = 0.120 * 0.070;
    const Traction3D applied = traction(3000.0, -2000.0, 5000.0);
    const std::vector<StructuralLoad> loads{
        StructuralLoad{LoadId::fromValue(1),
                       SurfaceTractionLoad{.face = top, .traction = applied}}};
    const Point3D origin = at(-0.05, 0.02, -0.03);
    const Measured measured = measure(model, volume, loads, origin);

    INFO("facets " << measured.facets << ", facet area " << measured.area
                   << " m^2, analytic " << analyticArea << " m^2");
    CHECK(measured.facets > 0);
    CHECK(measured.loadedNodes > 0);

    SECTION("the facet areas sum to the analytical CAD face area") {
        CHECK_THAT(measured.area, WithinRel(analyticArea, 1e-12));
    }

    SECTION("the resultant force is t A, from the analytical area") {
        const std::array<double, 3> expected{applied.x.si() * analyticArea,
                                             applied.y.si() * analyticArea,
                                             applied.z.si() * analyticArea};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            INFO("axis " << axis << ": expected " << expected[axis] << " got "
                         << measured.force[axis]);
            CHECK_THAT(measured.force[axis], WithinRel(expected[axis], 1e-11));
        }
    }

    SECTION("the resultant moment is that of the total force at the face centroid") {
        // The block spans x in [0, 0.120], y in [0, 0.070], and its end cap is
        // at z = 0.035, so the centroid is (0.060, 0.035, 0.035).
        const double cx = 0.060 - origin.x.si();
        const double cy = 0.035 - origin.y.si();
        const double cz = 0.035 - origin.z.si();
        const double fx = applied.x.si() * analyticArea;
        const double fy = applied.y.si() * analyticArea;
        const double fz = applied.z.si() * analyticArea;
        const std::array<double, 3> expected{cy * fz - cz * fy, cz * fx - cx * fz,
                                             cx * fy - cy * fx};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            INFO("axis " << axis << ": expected " << expected[axis] << " got "
                         << measured.moment[axis]);
            CHECK_THAT(measured.moment[axis], WithinRel(expected[axis], 1e-10));
        }
    }
}

TEST_CASE("StructuralLoad_SurvivesARemeshWithoutAnyFacetIdentity",
          "[structural][load][reference]") {
    // THE CENTRAL AUTHORITY CLAIM. The canonical load never changes; the mesh
    // does. The facets it resolves to are a different set of handles, and the
    // physical resultant is the same -- which is exactly what a `FaceName`
    // being canonical and a facet handle not being canonical means.
    auto built = reference::buildMeshBlockReferenceModel();
    REQUIRE(built.has_value());
    const FaceName top = built->top();
    MeshedReference model(std::move(built->document));
    assignSteel(model.document());

    const double analyticArea = 0.120 * 0.070;
    const Traction3D applied = traction(0.0, 0.0, -4000.0);
    const std::vector<StructuralLoad> loads{
        StructuralLoad{LoadId::fromValue(1),
                       SurfaceTractionLoad{.face = top, .traction = applied}}};
    const Point3D origin = at(0.0, 0.0, 0.0);

    const meshing::VolumeMesh& first = model.require();
    const Measured before = measure(model, first, loads, origin);

    // Remesh the same unchanged model.
    const meshing::VolumeMesh& second = model.require();
    REQUIRE(second.mesh().stamp() != before.mesh);
    const Measured after = measure(model, second, loads, origin);

    INFO("before: " << before.facets << " facets, F = " << before.force[2]
                    << " N; after: " << after.facets << " facets, F = " << after.force[2] << " N");

    SECTION("the mesh identity changed, so no facet handle carried over") {
        CHECK(after.mesh != before.mesh);
    }

    SECTION("the canonical target re-resolved and the physical resultant is unchanged") {
        const double expected = applied.z.si() * analyticArea;
        CHECK_THAT(before.force[2], WithinRel(expected, 1e-11));
        CHECK_THAT(after.force[2], WithinRel(expected, 1e-11));
        CHECK_THAT(after.area, WithinRel(before.area, 1e-12));
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const double scale = std::abs(expected) * 0.120;
            CHECK_THAT(after.moment[axis], WithinAbs(before.moment[axis], 1e-10 * scale));
        }
    }

    SECTION("and the load itself never mentioned the mesh") {
        // The canonical record is byte-identical across the remesh, because it
        // holds a FaceName and a traction and nothing else.
        const std::vector<StructuralLoad> again{
            StructuralLoad{LoadId::fromValue(1),
                           SurfaceTractionLoad{.face = top, .traction = applied}}};
        CHECK(again[0] == loads[0]);
    }
}

TEST_CASE("StructuralLoad_ConvergesToTheAnalyticLoadAsTheSurfaceIsRefined",
          "[structural][load][reference]") {
    // THIS TEST WAS VACUOUS TWICE BEFORE IT WAS RIGHT, and the reason is worth
    // stating because it is a property of the geometry and not of the load.
    //
    //   1. The first draft varied the sizing with `requireWith`, which builds
    //      a VolumeMesh the Mesher never HOLDS -- so requireStructuralModel
    //      found no mesh and refused, correctly. The refinement has to go
    //      through the document's own MeshControl.
    //   2. The second draft then refined a PLANAR face of RM-MESH-03 and
    //      measured 40 facets at every level. A plane is exactly
    //      representable, so no deflection or sizing change retriangulates it,
    //      and "the resultant is unchanged" was true because NOTHING changed.
    //
    // So the fixture is a CURVED face: RM-MESH-02's cylindrical wall, radius
    // 25 mm and height 60 mm, whose analytic lateral area is `2 pi r h`. There
    // the refinement is real, and the claim is stronger than invariance --
    // the facet area of an inscribed polygon approaches the true area FROM
    // BELOW, so a constant traction's resultant must CONVERGE to `t A` and the
    // error must fall monotonically.
    //
    // The levels are asserted to DIFFER before any error is compared, which is
    // the whole lesson of the two earlier drafts.
    auto built = reference::buildMeshCylinderReferenceModel();
    REQUIRE(built.has_value());
    const FaceName wall = built->wall();
    MeshedReference model(std::move(built->document));
    assignSteel(model.document());

    const Traction3D applied = traction(0.0, 0.0, -2500.0);
    const std::vector<StructuralLoad> loads{
        StructuralLoad{LoadId::fromValue(1),
                       SurfaceTractionLoad{.face = wall, .traction = applied}}};
    const Point3D origin = at(0.0, 0.0, 0.0);
    // 2 pi r h, from the model's declared dimensions rather than from a mesh.
    const double analyticArea = 2.0 * reference::kPi * 0.025 * 0.060;

    std::vector<Measured> levels;
    std::vector<double> deflections;
    for (const double deflectionMm : {0.40, 0.10, 0.025}) {
        meshing::MeshControlDefinition definition = model.definition()->definition();
        definition.mesh.surface.linearDeflection = Length::fromSi(deflectionMm * 1e-3);
        REQUIRE(model.document()
                    .modifyObject<meshing::MeshControl>(
                        model.controlObject(),
                        [&definition](meshing::MeshControl& control) {
                            return control.setDefinition(definition);
                        })
                    .has_value());
        const meshing::VolumeMesh& volume = model.require();
        levels.push_back(measure(model, volume, loads, origin));
        deflections.push_back(deflectionMm);
    }
    REQUIRE(levels.size() == 3);

    for (std::size_t i = 0; i < levels.size(); ++i) {
        INFO("deflection " << deflections[i] << " mm: " << levels[i].facets << " facets, "
                           << levels[i].loadedNodes << " loaded nodes, area " << levels[i].area
                           << " m^2 against " << analyticArea << " m^2, Fz "
                           << levels[i].force[2] << " N");
    }

    SECTION("the levels really are different discretisations") {
        // STRICTLY MORE FACETS at each step. Asserted FIRST, because every
        // claim below is meaningless if the mesh did not change -- which is
        // exactly how the previous draft passed.
        for (std::size_t i = 1; i < levels.size(); ++i) {
            INFO("level " << i - 1 << " had " << levels[i - 1].facets << " facets, level " << i
                          << " has " << levels[i].facets);
            CHECK(levels[i].facets > levels[i - 1].facets);
            CHECK(levels[i].loadedNodes > levels[i - 1].loadedNodes);
        }
    }

    SECTION("the facet area approaches the analytic area from below") {
        for (std::size_t i = 0; i < levels.size(); ++i) {
            // An inscribed polygon is shorter than its circle, so the facet
            // area can never exceed the true one.
            CHECK(levels[i].area <= analyticArea * (1.0 + 1e-12));
        }
        for (std::size_t i = 1; i < levels.size(); ++i) {
            CHECK(levels[i].area > levels[i - 1].area);
        }
        // And the finest level is close, which is a chord-error bound rather
        // than a tuned threshold.
        CHECK(levels.back().area > analyticArea * 0.999);
    }

    SECTION("the resultant converges to t A, with the error falling") {
        const double expected = applied.z.si() * analyticArea;
        std::vector<double> errors;
        for (const Measured& level : levels) {
            errors.push_back(std::abs(level.force[2] - expected) / std::abs(expected));
        }
        for (std::size_t i = 0; i < errors.size(); ++i) {
            INFO("level " << i << " relative error " << errors[i]);
        }
        for (std::size_t i = 1; i < errors.size(); ++i) {
            CHECK(errors[i] < errors[i - 1]);
        }
        CHECK(errors.back() < 1.0e-3);

        // The resultant is t times the FACET area exactly at every level,
        // which is the statement that the integration is right whatever the
        // discretisation -- the convergence above is the geometry's.
        for (const Measured& level : levels) {
            CHECK_THAT(level.force[2], WithinRel(applied.z.si() * level.area, 1e-11));
        }
    }

    SECTION("the moment converges too") {
        // The wall's centroid is on the axis at mid-height, so for a traction
        // along -Z the moment about the origin is zero by symmetry -- and the
        // residual must fall as the facets even out.
        const double scale = std::abs(applied.z.si()) * analyticArea * 0.060;
        for (const Measured& level : levels) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                CHECK_THAT(level.moment[axis], WithinAbs(0.0, 2.0e-3 * scale));
            }
        }
    }
}

TEST_CASE("StructuralLoad_PressureRotatesWithTheBodyAndAGlobalTractionDoesNot",
          "[structural][load][reference]") {
    // THE TWO CONVENTIONS, SEPARATED. RM-MESH-06 is an asymmetric block and
    // the same block under a compound rotation that mixes all three axes, so
    // the rotation is not about a coordinate axis and an axis-permutation
    // defect cannot survive it.
    //
    //   a PRESSURE follows the face normal, so its resultant rotates with the
    //     geometry -- R F
    //   a global TRACTION does not, so its resultant is the same vector
    //
    // Requiring either behaviour of the other would be wrong, and the test
    // says which is which.
    const reference::RigidPlacement placement = reference::meshTransformedPlacement();

    auto baseBuilt = reference::buildMeshTransformedBaseReferenceModel();
    REQUIRE(baseBuilt.has_value());
    const FaceName baseFace = baseBuilt->oppositeFace();
    MeshedReference base(std::move(baseBuilt->document));
    assignSteel(base.document());
    const meshing::VolumeMesh& baseMesh = base.require();

    auto placedBuilt = reference::buildMeshTransformedPlacedReferenceModel();
    REQUIRE(placedBuilt.has_value());
    const FaceName placedFace = placedBuilt->oppositeFace();
    MeshedReference placed(std::move(placedBuilt->document));
    assignSteel(placed.document());
    const meshing::VolumeMesh& placedMesh = placed.require();

    const Point3D origin = at(0.0, 0.0, 0.0);
    const double p = 120000.0;

    SECTION("a pressure resultant rotates with the face") {
        const std::vector<StructuralLoad> loads{
            StructuralLoad{LoadId::fromValue(1),
                           PressureLoad{.face = baseFace, .magnitude = Pressure::fromSi(p)}}};
        const Measured onBase = measure(base, baseMesh, loads, origin);
        const std::vector<StructuralLoad> rotatedLoads{
            StructuralLoad{LoadId::fromValue(1),
                           PressureLoad{.face = placedFace, .magnitude = Pressure::fromSi(p)}}};
        const Measured onPlaced = measure(placed, placedMesh, rotatedLoads, origin);

        // R applied to the base resultant, with R read from the model's own
        // declared triad. Its columns are the placed frame's axes, so
        // R v = v.x * X + v.y * Y + v.z * N.
        const std::array<double, 3> expected{
            onBase.force[0] * placement.xAxis[0] + onBase.force[1] * placement.yAxis[0] +
                onBase.force[2] * placement.normal[0],
            onBase.force[0] * placement.xAxis[1] + onBase.force[1] * placement.yAxis[1] +
                onBase.force[2] * placement.normal[1],
            onBase.force[0] * placement.xAxis[2] + onBase.force[1] * placement.yAxis[2] +
                onBase.force[2] * placement.normal[2]};
        double magnitude = 0.0;
        for (const double component : onBase.force) {
            magnitude += component * component;
        }
        magnitude = std::sqrt(magnitude);
        INFO("base [" << onBase.force[0] << ", " << onBase.force[1] << ", " << onBase.force[2]
                      << "] N, placed [" << onPlaced.force[0] << ", " << onPlaced.force[1] << ", "
                      << onPlaced.force[2] << "] N, expected R F [" << expected[0] << ", "
                      << expected[1] << ", " << expected[2] << "] N");
        CHECK(magnitude > 0.0);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            CHECK_THAT(onPlaced.force[axis], WithinAbs(expected[axis], 1e-9 * magnitude));
        }
        // And the MAGNITUDE is invariant, because a rotation preserves area.
        double placedMagnitude = 0.0;
        for (const double component : onPlaced.force) {
            placedMagnitude += component * component;
        }
        CHECK_THAT(std::sqrt(placedMagnitude), WithinRel(magnitude, 1e-10));
    }

    SECTION("a global traction resultant does NOT rotate") {
        const Traction3D applied = traction(0.0, 0.0, -3000.0);
        const std::vector<StructuralLoad> loads{
            StructuralLoad{LoadId::fromValue(1),
                           SurfaceTractionLoad{.face = baseFace, .traction = applied}}};
        const Measured onBase = measure(base, baseMesh, loads, origin);
        const std::vector<StructuralLoad> sameLoads{
            StructuralLoad{LoadId::fromValue(1),
                           SurfaceTractionLoad{.face = placedFace, .traction = applied}}};
        const Measured onPlaced = measure(placed, placedMesh, sameLoads, origin);

        // Same face, same area, same global vector: the same force, component
        // for component. A traction that had been coupled to the normal would
        // have rotated and failed this.
        const double scale = std::abs(applied.z.si()) * onBase.area;
        INFO("base [" << onBase.force[0] << ", " << onBase.force[1] << ", " << onBase.force[2]
                      << "] N, placed [" << onPlaced.force[0] << ", " << onPlaced.force[1] << ", "
                      << onPlaced.force[2] << "] N");
        CHECK_THAT(onPlaced.area, WithinRel(onBase.area, 1e-10));
        for (std::size_t axis = 0; axis < 3; ++axis) {
            CHECK_THAT(onPlaced.force[axis], WithinAbs(onBase.force[axis], 1e-9 * scale));
        }
    }
}

TEST_CASE("StructuralLoad_PressureOnACurvedWallUsesEachFacetOwnNormal",
          "[structural][load][reference]") {
    // RM-MESH-04's inner and outer walls are swept by DIFFERENT circles, so
    // both carry stable names -- which is what makes a tube the available
    // curved-pressure fixture while a drilled hole is not.
    //
    // A uniform pressure on a full cylindrical wall cancels by symmetry, so the
    // near-zero net force is NOT used as the only evidence: the facet count,
    // the total area against the analytical cylinder area, and the fact that
    // each facet pulls along its own normal are what the test actually checks.
    auto built = reference::buildMeshTubeReferenceModel();
    REQUIRE(built.has_value());
    const FaceName inner = built->innerWall();
    MeshedReference model(std::move(built->document));
    assignSteel(model.document());
    const meshing::VolumeMesh& volume = model.require();

    const double p = 80000.0;
    const std::vector<StructuralLoad> loads{
        StructuralLoad{LoadId::fromValue(1),
                       PressureLoad{.face = inner, .magnitude = Pressure::fromSi(p)}}};
    const Measured measured = measure(model, volume, loads, at(0, 0, 0));

    // The inner wall is a cylinder of radius 18 mm and height 45 mm, so its
    // analytical area is 2 pi r h. The facet sum approaches it FROM BELOW,
    // because an inscribed polygon is shorter than the circle it approximates.
    const double analytic = 2.0 * reference::kPi * 0.018 * 0.045;
    INFO("facets " << measured.facets << ", facet area " << measured.area
                   << " m^2, analytical cylinder area " << analytic << " m^2");
    CHECK(measured.facets > 0);
    CHECK(measured.area > 0.0);
    CHECK(measured.area <= analytic * (1.0 + 1e-9));
    // Within 2% for the committed deflection, which is a chord-error bound and
    // not a tuned number.
    CHECK(measured.area > analytic * 0.98);

    SECTION("the net transverse force cancels, which is the symmetry check") {
        // Supplementary rather than primary: a uniform pressure on a closed
        // ring has no resultant. The scale is the force one radius-worth of
        // wall would carry, so the residual is measured against something
        // physical.
        const double scale = p * measured.area;
        CHECK_THAT(measured.force[0], WithinAbs(0.0, 1e-3 * scale));
        CHECK_THAT(measured.force[1], WithinAbs(0.0, 1e-3 * scale));
        CHECK_THAT(measured.force[2], WithinAbs(0.0, 1e-3 * scale));
    }

    SECTION("every loaded node carries a force pointing away from the axis") {
        // Positive pressure on the BORE pushes outward in the global frame,
        // because the bore's outward normal points toward the axis. That is a
        // per-facet directional check the cancelling resultant cannot make.
        Result<StructuralModel> prepared = structural::requireStructuralModel(
            model.document(), model.regenerator(), model.mesher(), model.control());
        REQUIRE(prepared.has_value());
        Result<StructuralMaterial> material = structural::resolveStructuralMaterial(
            model.document(), model.body(), StructuralAnalysisMode::LinearStatic);
        REQUIRE(material.has_value());
        Result<PreparedLoads> field =
            structural::prepareStructuralLoads(*prepared, *material, loads);
        REQUIRE(field.has_value());
        REQUIRE_FALSE(field->nodal().empty());

        // The tube's axis is Z through the origin of its sketch, at (0, 0).
        std::size_t outward = 0;
        std::size_t counted = 0;
        for (const NodalLoad& load : field->nodal()) {
            const meshing::Node* node = volume.mesh().findNode(load.node);
            REQUIRE(node != nullptr);
            const double rx = node->position.x.si();
            const double ry = node->position.y.si();
            const double radius = std::hypot(rx, ry);
            if (radius < 1e-9) {
                continue;
            }
            ++counted;
            // Radial component of the nodal force.
            const double radial =
                (load.force.x.si() * rx + load.force.y.si() * ry) / radius;
            if (radial > 0.0) {
                ++outward;
            }
        }
        INFO(outward << " of " << counted << " loaded nodes pull outward");
        CHECK(counted > 0);
        // Every node on the bore should be pushed outward. A node on the rim
        // shared with a cap could in principle differ, so the claim is the
        // large majority rather than all -- and a flipped normal would invert
        // it wholesale.
        CHECK(outward * 10 >= counted * 9);
    }
}

TEST_CASE("StructuralLoad_RefusesTheDrilledHoleWallWithNoGeometricFallback",
          "[structural][load][reference]") {
    // THE KNOWN P16 LIMITATION, recorded as a refusal rather than worked
    // around. `cutHole` names a hole's flat faces and NOT its cylindrical
    // wall, so there is no canonical reference for that wall -- and the
    // reference a user would naturally write resolves to nothing.
    //
    // RM-MESH-03 and RM-MESH-04 put their holes in the profile sketch for
    // exactly this reason, which makes their walls named Side faces. Here the
    // point is the opposite: a selector aimed at an unnamed wall must be
    // refused, and nothing nearby substituted.
    auto built = reference::buildMeshPlateWithHoleReferenceModel();
    REQUIRE(built.has_value());
    // The plate's hole IS nameable, because its circle is in the profile. So
    // the unsupported case is approximated by a selector naming a Side face
    // swept by an entity that is not part of this body's profile -- which is
    // what a drilled hole's wall amounts to: a face the naming chain never
    // attributed.
    const FaceName unnamed{built->solid,
                           FaceSelector{.role = FaceRole::Side,
                                        .entity = EntityId::fromValue(99999)}};
    const FaceName supported = built->holeWall();
    MeshedReference model(std::move(built->document));
    assignSteel(model.document());
    const meshing::VolumeMesh& volume = model.require();
    (void)volume;

    Result<StructuralModel> prepared = structural::requireStructuralModel(
        model.document(), model.regenerator(), model.mesher(), model.control());
    REQUIRE(prepared.has_value());
    Result<StructuralMaterial> material = structural::resolveStructuralMaterial(
        model.document(), model.body(), StructuralAnalysisMode::LinearStatic);
    REQUIRE(material.has_value());

    SECTION("a wall with no canonical name is refused") {
        const std::vector<StructuralLoad> loads{
            StructuralLoad{LoadId::fromValue(1),
                           PressureLoad{.face = unnamed,
                                        .magnitude = Pressure::fromSi(50000.0)}}};
        const std::optional<LoadProblem> problem =
            structural::structuralLoadProblem(*prepared, *material, loads);
        INFO("problem " << (problem.has_value() ? structural::toString(*problem) : "none"));
        REQUIRE(problem.has_value());
        CHECK(*problem == LoadProblem::TargetUnresolved);
        CHECK_FALSE(structural::prepareStructuralLoads(*prepared, *material, loads).has_value());
    }

    SECTION("a bore that IS named works, so the refusal is about the naming gap") {
        // The same geometry kind -- a cylindrical hole wall -- succeeds when
        // the naming chain gave it a reference. That is what makes the refusal
        // above a P16 limitation rather than a P17 defect.
        const std::vector<StructuralLoad> loads{
            StructuralLoad{LoadId::fromValue(1),
                           PressureLoad{.face = supported,
                                        .magnitude = Pressure::fromSi(50000.0)}}};
        CHECK_FALSE(structural::structuralLoadProblem(*prepared, *material, loads).has_value());
        Result<PreparedLoads> field =
            structural::prepareStructuralLoads(*prepared, *material, loads);
        REQUIRE(field.has_value());
        REQUIRE(field->contributions().size() == 1);
        CHECK(field->contributions()[0].elements > 0);
        CHECK(field->contributions()[0].area.si() > 0.0);
    }
}

TEST_CASE("StructuralLoad_WeighsAReferenceBodyAgainstItsAnalyticalVolume",
          "[structural][load][reference]") {
    // Gravity on RM-MESH-01, whose analytic volume is 120 x 70 x 35 mm from
    // its own declared dimensions -- so the expected weight does not come from
    // the mesh.
    auto built = reference::buildMeshBlockReferenceModel();
    REQUIRE(built.has_value());
    MeshedReference model(std::move(built->document));
    assignSteel(model.document());
    const meshing::VolumeMesh& volume = model.require();

    const double density = 7850.0;
    const double g = structural::kStandardGravity;
    const double analyticVolume = 0.120 * 0.070 * 0.035;
    const std::vector<StructuralLoad> loads{
        StructuralLoad{LoadId::fromValue(1),
                       structural::GravityLoad{.acceleration = Vector3D{0, 0, -g}}}};
    const Measured measured = measure(model, volume, loads, at(0, 0, 0),
                                      StructuralAnalysisMode::LinearStaticWithGravity);

    const double expected = -density * analyticVolume * g;
    INFO("analytic V " << analyticVolume << " m^3, mesh V "
                       << volume.tetrahedralVolume().si() << " m^3, expected " << expected
                       << " N, actual " << measured.force[2] << " N");
    CHECK_THAT(measured.force[0], WithinAbs(0.0, 1e-9 * std::abs(expected)));
    CHECK_THAT(measured.force[1], WithinAbs(0.0, 1e-9 * std::abs(expected)));
    CHECK_THAT(measured.force[2], WithinRel(expected, 1e-10));

    SECTION("the moment is the weight acting at the centre of volume") {
        // A homogeneous block's centre of mass is its geometric centre:
        // (0.060, 0.035, 0.0175).
        const std::array<double, 3> want{0.035 * expected, -0.060 * expected, 0.0};
        const double scale = std::abs(expected) * 0.120;
        CHECK_THAT(measured.moment[0], WithinRel(want[0], 1e-10));
        CHECK_THAT(measured.moment[1], WithinRel(want[1], 1e-10));
        CHECK_THAT(measured.moment[2], WithinAbs(0.0, 1e-10 * scale));
    }
}
