// P17-LOAD-001: canonical structural loads and their nodal force field.
//
// WHAT THESE TESTS ARE FOR. Two kinds of claim, and they need different
// evidence. The integration claims -- a constant traction over a linear
// triangle gives each corner `A t / 3`, a positive pressure acts inward, the
// resultant force and the resultant MOMENT are both preserved -- are checked
// against values derived by hand in the test, never against a production
// helper. The authority claims -- a facet handle is never load authority, an
// unresolved target is refused rather than treated as zero, a mesh-local node
// load cannot survive a remesh -- are checked by constructing the dangerous
// thing and watching it be refused.
//
// THE MOMENT IS NOT OPTIONAL. A wrong nodal distribution can preserve the
// total force exactly and still be wrong -- putting the whole facet force on
// one corner does -- so every resultant test that can have a non-zero moment
// checks both, on asymmetric geometry.
//
// EXPECTED VALUES ARE COMPUTED HERE. `facetAreaVector`, `facetArea` and
// `facetNodalForce` are production, so the analytical tests form their
// expectations from the triangle's coordinates with arithmetic written out in
// the test.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/structural/StructuralAnalysisObject.hpp>
#include <bettercad/structural/StructuralLoadVector.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using structural::GravityLoad;
using structural::LoadContribution;
using structural::LoadKind;
using structural::LoadProblem;
using structural::NodalForceLoad;
using structural::NodalLoad;
using structural::PreparedLoads;
using structural::PressureLoad;
using structural::StructuralAnalysis;
using structural::StructuralAnalysisDefinition;
using structural::StructuralAnalysisMode;
using structural::StructuralLoad;
using structural::StructuralMaterial;
using structural::StructuralModel;
using structural::SurfaceTractionLoad;

namespace {

[[nodiscard]] Point3D at(double x, double y, double z) {
    return Point3D{Length::fromSi(x), Length::fromSi(y), Length::fromSi(z)};
}

[[nodiscard]] Traction3D traction(double x, double y, double z) {
    return Traction3D{Pressure::fromSi(x), Pressure::fromSi(y), Pressure::fromSi(z)};
}

/// The analytical triangle: `A = 3 m^2`, centroid `(2/3, 1, 0)`, outward
/// normal `+Z` for this winding. Asymmetric, so its centroid is not at the
/// origin and a moment about the origin is non-zero.
[[nodiscard]] std::array<Point3D, 3> referenceTriangle() {
    return {at(0, 0, 0), at(2, 0, 0), at(0, 3, 0)};
}

/// A block with a material, a meshing control and an analysis: everything a
/// load needs to be prepared against.
///
/// 40 x 30 mm in plan, extruded 20 mm along +Z, so the start cap lies at
/// z = 0 with outward normal -Z and the end cap at z = 20 mm with outward
/// normal +Z. Both have an exactly known area of 1.2e-3 m^2, which is what
/// makes them the analytical fixtures.
struct LoadedPart {
    Document document{"Part"};
    features::Regenerator regenerator;
    meshing::Mesher mesher;
    ObjectId feature{};
    MeshControlId control{};
    AnalysisId analysis{};
    MaterialId material{};
    std::array<EntityId, 4> lines{};

    static constexpr double kWidth = 0.040;
    static constexpr double kDepth = 0.030;
    static constexpr double kHeight = 0.020;
    /// 40 mm x 30 mm, in m^2. Derived from the dimensions above, by hand.
    static constexpr double kCapArea = kWidth * kDepth;

    explicit LoadedPart(bool withDensity = true) {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        lines = addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
        const ObjectId profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = 20_mm});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));

        auto intent =
            meshing::MeshControl::create("Mesh", meshing::MeshControlDefinition{.body = feature});
        REQUIRE(intent.has_value());
        control = MeshControlId::fromValue(require(document.addObject(std::move(*intent))).value());

        features::MaterialDefinition definition;
        definition.designation = "Steel";
        definition.mechanical.youngsModulus =
            materials::MaterialProperty<ElasticModulus>::known(210_GPa);
        definition.mechanical.poissonRatio =
            materials::MaterialProperty<bettercad::PoissonRatio>::known(PoissonRatio::of(0.3));
        if (withDensity) {
            definition.mechanical.density =
                materials::MaterialProperty<Density>::known(Density::fromSi(7850.0));
        }
        const Result<MaterialId> id = features::createMaterial(document, "Steel", definition);
        REQUIRE(id.has_value());
        material = *id;
        REQUIRE(features::assignMaterial(document, material).has_value());

        auto study =
            StructuralAnalysis::create("Study", StructuralAnalysisDefinition{.mesh = control});
        REQUIRE(study.has_value());
        analysis = AnalysisId::fromValue(require(document.addObject(std::move(*study))).value());

        requireReport(regenerator, document);
        const Result<const meshing::VolumeMesh*> mesh =
            mesher.generate(document, regenerator, control);
        INFO((mesh.has_value() ? std::string{} : mesh.error().message));
        REQUIRE(mesh.has_value());
    }

    [[nodiscard]] FaceName startCap() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::StartCap}};
    }
    [[nodiscard]] FaceName endCap() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::EndCap}};
    }
    [[nodiscard]] FaceName side(std::size_t which) const {
        return FaceName{feature, FaceSelector{.role = FaceRole::Side, .entity = lines.at(which)}};
    }

    [[nodiscard]] const meshing::VolumeMesh& volume() const {
        const meshing::VolumeMesh* held = mesher.mesh(control);
        REQUIRE(held != nullptr);
        return *held;
    }

    [[nodiscard]] StructuralModel model() const {
        Result<StructuralModel> prepared =
            structural::requireStructuralModel(document, regenerator, mesher, control);
        INFO((prepared.has_value() ? std::string{} : prepared.error().message));
        REQUIRE(prepared.has_value());
        return std::move(*prepared);
    }

    [[nodiscard]] StructuralMaterial resolved(StructuralAnalysisMode mode) const {
        Result<StructuralMaterial> material_ =
            structural::resolveStructuralMaterial(document, feature, mode);
        INFO((material_.has_value() ? std::string{} : material_.error().message));
        REQUIRE(material_.has_value());
        return *material_;
    }

    void remesh() {
        const Result<const meshing::VolumeMesh*> again =
            mesher.generate(document, regenerator, control);
        INFO((again.has_value() ? std::string{} : again.error().message));
        REQUIRE(again.has_value());
    }
};

[[nodiscard]] PreparedLoads prepare(const LoadedPart& part, const std::vector<StructuralLoad>& loads,
                                    StructuralAnalysisMode mode = StructuralAnalysisMode::LinearStatic) {
    const StructuralModel model = part.model();
    Result<PreparedLoads> prepared =
        structural::prepareStructuralLoads(model, part.resolved(mode), loads);
    INFO((prepared.has_value() ? std::string{} : prepared.error().message));
    REQUIRE(prepared.has_value());
    return std::move(*prepared);
}

/// The resultant moment about @p origin, computed in the test from the nodal
/// field and the mesh's own coordinates. Deliberately NOT production's
/// `resultantMomentAbout`, so that the two can be compared.
[[nodiscard]] std::array<double, 3> momentHere(const PreparedLoads& loads,
                                               const meshing::Mesh& mesh,
                                               const Point3D& origin) {
    std::array<double, 3> total{};
    for (const NodalLoad& load : loads.nodal()) {
        const meshing::Node* node = mesh.findNode(load.node);
        REQUIRE(node != nullptr);
        const double rx = node->position.x.si() - origin.x.si();
        const double ry = node->position.y.si() - origin.y.si();
        const double rz = node->position.z.si() - origin.z.si();
        const double fx = load.force.x.si();
        const double fy = load.force.y.si();
        const double fz = load.force.z.si();
        total[0] += ry * fz - rz * fy;
        total[1] += rz * fx - rx * fz;
        total[2] += rx * fy - ry * fx;
    }
    return total;
}

[[nodiscard]] std::array<double, 3> forceHere(const PreparedLoads& loads) {
    std::array<double, 3> total{};
    for (const NodalLoad& load : loads.nodal()) {
        total[0] += load.force.x.si();
        total[1] += load.force.y.si();
        total[2] += load.force.z.si();
    }
    return total;
}

} // namespace

// ---------------------------------------------------------------------------
// The schema
// ---------------------------------------------------------------------------

TEST_CASE("StructuralLoad_CanonicalFaceLoadsCarryNoMeshHandle", "[structural][load]") {
    // THE CENTRAL RULE, in compile-time form: a face load is a FaceName and a
    // physical value, and there is nowhere in it for a boundary facet or a
    // node to hide. The mirrors declare the same members in the same order, so
    // an added field changes the size.
    struct PermittedTraction {
        FaceName face;
        Traction3D traction;
    };
    struct PermittedPressure {
        FaceName face;
        Pressure magnitude;
    };
    STATIC_REQUIRE(sizeof(SurfaceTractionLoad) == sizeof(PermittedTraction));
    STATIC_REQUIRE(sizeof(PressureLoad) == sizeof(PermittedPressure));
    STATIC_REQUIRE_FALSE(std::is_constructible_v<SurfaceTractionLoad, meshing::ElementId>);
    STATIC_REQUIRE_FALSE(std::is_constructible_v<PressureLoad, meshing::ElementId>);
    STATIC_REQUIRE_FALSE(std::is_constructible_v<SurfaceTractionLoad, meshing::NodeId>);

    // A GRAVITY LOAD HOLDS NO DENSITY. ADR-028: a solver maintains no material
    // data of its own, so the density stays P15's and the load carries only an
    // acceleration.
    STATIC_REQUIRE(sizeof(GravityLoad) == sizeof(Vector3D));
    STATIC_REQUIRE_FALSE(std::is_constructible_v<GravityLoad, Density>);

    // AND THE NODAL LOAD IS EXPLICITLY MESH-LOCAL: it carries the stamp,
    // because a NodeId without one means nothing.
    STATIC_REQUIRE(std::is_constructible_v<NodalForceLoad>);
    const NodalForceLoad nodal{};
    CHECK_FALSE(nodal.mesh.isValid());
    CHECK_FALSE(nodal.node.isValid());

    // The payloads are different types, so a pressure cannot be assigned from
    // a traction even though both target a face.
    STATIC_REQUIRE_FALSE(std::is_assignable_v<SurfaceTractionLoad&, PressureLoad>);
    STATIC_REQUIRE_FALSE(std::is_assignable_v<PressureLoad&, SurfaceTractionLoad>);

    // Units: a traction component is a Pressure and a force component is a
    // Force, and the two do not interconvert.
    STATIC_REQUIRE(std::is_same_v<decltype(Traction3D{}.x), Pressure>);
    STATIC_REQUIRE(std::is_same_v<decltype(Force3D{}.x), Force>);
    STATIC_REQUIRE_FALSE(std::is_assignable_v<Force&, Pressure>);
    // And traction x area really is a force, by dimension.
    STATIC_REQUIRE(std::is_same_v<decltype(Pressure{} * Area{}), Force>);
    // A moment is N m, which shares a dimension with an energy.
    STATIC_REQUIRE(std::is_same_v<Torque, Energy>);
    STATIC_REQUIRE(std::is_same_v<decltype(Force{} * Length{}), Torque>);
}

TEST_CASE("StructuralLoad_EachKindReportsItselfAndItsTarget", "[structural][load]") {
    const LoadId id = LoadId::fromValue(7);
    const FaceName face{ObjectId::fromValue(3), FaceSelector{.role = FaceRole::EndCap}};

    const StructuralLoad nodal{id, NodalForceLoad{}};
    CHECK(nodal.kind() == LoadKind::NodalForce);
    CHECK(nodal.nodalForce() != nullptr);
    CHECK(nodal.traction() == nullptr);
    CHECK_FALSE(nodal.target().has_value());

    const StructuralLoad applied{id, SurfaceTractionLoad{.face = face, .traction = traction(1, 2, 3)}};
    CHECK(applied.kind() == LoadKind::SurfaceTraction);
    CHECK(applied.target() == std::optional<FaceName>{face});

    const StructuralLoad squeezed{id, PressureLoad{.face = face, .magnitude = Pressure::fromSi(5)}};
    CHECK(squeezed.kind() == LoadKind::Pressure);
    CHECK(squeezed.target() == std::optional<FaceName>{face});

    const StructuralLoad weight{id, GravityLoad{.acceleration = Vector3D{0, 0, -9.80665}}};
    CHECK(weight.kind() == LoadKind::Gravity);
    CHECK_FALSE(weight.target().has_value());

    CHECK(structural::toString(LoadKind::NodalForce) == "nodal_force");
    CHECK(structural::toString(LoadKind::SurfaceTraction) == "surface_traction");
    CHECK(structural::toString(LoadKind::Pressure) == "pressure");
    CHECK(structural::toString(LoadKind::Gravity) == "gravity");

    // The convenience constant is the CGPM value and is offered, not assumed:
    // a default-constructed gravity load has no acceleration at all.
    CHECK(structural::kStandardGravity == 9.80665);
    CHECK(GravityLoad{}.acceleration.x == 0.0);
}

// ---------------------------------------------------------------------------
// Facet integration, against values derived by hand
// ---------------------------------------------------------------------------

TEST_CASE("StructuralLoad_FacetAreaVectorIsHalfTheEdgeCrossProduct", "[structural][load]") {
    // Triangle (0,0,0), (2,0,0), (0,3,0): u = (2,0,0), v = (0,3,0), so
    // u x v = (0,0,6) and half of it is (0,0,3). Area 3, normal +Z. Worked out
    // here, not read from production.
    const std::array<Point3D, 3> t = referenceTriangle();
    const Vector3D a = structural::facetAreaVector(t[0], t[1], t[2]);
    CHECK(a.x == 0.0);
    CHECK(a.y == 0.0);
    CHECK(a.z == 3.0);
    CHECK(structural::facetArea(t[0], t[1], t[2]).si() == 3.0);

    SECTION("the HALF is not optional") {
        // Without it the area would be 6: the cross product of two edges spans
        // the parallelogram. Asserted against the wrong answer as well as the
        // right one.
        CHECK(structural::facetArea(t[0], t[1], t[2]).si() != 6.0);
    }

    SECTION("reversing the winding reverses the normal and keeps the area") {
        const Vector3D flipped = structural::facetAreaVector(t[0], t[2], t[1]);
        CHECK(flipped.z == -3.0);
        CHECK(structural::facetArea(t[0], t[2], t[1]).si() == 3.0);
    }

    SECTION("a degenerate triangle has zero area and no direction") {
        const Vector3D none = structural::facetAreaVector(at(0, 0, 0), at(1, 0, 0), at(2, 0, 0));
        CHECK(none.x == 0.0);
        CHECK(none.y == 0.0);
        CHECK(none.z == 0.0);
        CHECK(structural::facetArea(at(0, 0, 0), at(1, 0, 0), at(2, 0, 0)).si() == 0.0);
    }

    SECTION("a skew triangle, by hand") {
        // (1,1,1), (3,1,2), (1,4,5): u = (2,0,1), v = (0,3,4), so
        // u x v = (0*4 - 1*3, 1*0 - 2*4, 2*3 - 0*0) = (-3, -8, 6), halved
        // (-1.5, -4, 3), magnitude sqrt(2.25 + 16 + 9) = sqrt(27.25).
        const Vector3D a2 = structural::facetAreaVector(at(1, 1, 1), at(3, 1, 2), at(1, 4, 5));
        CHECK_THAT(a2.x, WithinRel(-1.5, 1e-15));
        CHECK_THAT(a2.y, WithinRel(-4.0, 1e-15));
        CHECK_THAT(a2.z, WithinRel(3.0, 1e-15));
        CHECK_THAT(structural::facetArea(at(1, 1, 1), at(3, 1, 2), at(1, 4, 5)).si(),
                   WithinRel(std::sqrt(27.25), 1e-15));
    }
}

TEST_CASE("StructuralLoad_ConstantTractionGivesEachCornerOneThirdOfTheTotal",
          "[structural][load]") {
    // The consistent load vector of a three-node linear triangle under a
    // constant load. A = 3 and t = [2,-1,4], so A t / 3 = [2,-1,4] exactly --
    // the area and the divisor cancel, which makes this the clearest possible
    // check of the factor.
    const Area area = Area::fromSi(3.0);
    const Force3D perNode = structural::facetNodalForce(area, traction(2, -1, 4));
    CHECK(perNode.x.si() == 2.0);
    CHECK(perNode.y.si() == -1.0);
    CHECK(perNode.z.si() == 4.0);

    SECTION("the three thirds sum to A t exactly") {
        const Force3D total = perNode + perNode + perNode;
        CHECK(total.x.si() == 6.0);
        CHECK(total.y.si() == -3.0);
        CHECK(total.z.si() == 12.0);
    }

    SECTION("not A t / 2, and not the whole force on one corner") {
        CHECK(perNode.z.si() != 3.0 * 4.0 / 2.0);
        CHECK(perNode.z.si() != 3.0 * 4.0);
    }

    SECTION("a general area, worked out by hand") {
        // A = 0.0012 m^2, t = [0, 0, -1000] Pa, so each corner gets
        // -1000 * 0.0012 / 3 = -0.4 N.
        const Force3D per =
            structural::facetNodalForce(Area::fromSi(0.0012), traction(0, 0, -1000));
        CHECK_THAT(per.z.si(), WithinRel(-0.4, 1e-15));
    }
}

// ---------------------------------------------------------------------------
// Face loads on a real mesh
// ---------------------------------------------------------------------------

TEST_CASE("StructuralLoad_TractionPreservesTheResultantForceAndMoment",
          "[structural][load]") {
    // The hard gate. A constant traction over a CAD face must give `F = t A`,
    // and the nodal distribution must also carry the right FIRST MOMENT --
    // which putting the whole facet force on one corner would not. The
    // expected values come from the block's own dimensions, by hand.
    const LoadedPart part;
    const double area = LoadedPart::kCapArea; // 40 mm x 30 mm = 1.2e-3 m^2
    const Traction3D applied = traction(2000.0, -1500.0, 4000.0);

    const std::vector<StructuralLoad> loads{
        StructuralLoad{LoadId::fromValue(1),
                       SurfaceTractionLoad{.face = part.endCap(), .traction = applied}}};
    const PreparedLoads prepared = prepare(part, loads);

    REQUIRE_FALSE(prepared.nodal().empty());
    CHECK(prepared.describes(part.volume().mesh()));
    REQUIRE(prepared.contributions().size() == 1);
    const LoadContribution& contribution = prepared.contributions()[0];
    CHECK(contribution.load == LoadId::fromValue(1));
    CHECK(contribution.kind == LoadKind::SurfaceTraction);
    CHECK(contribution.elements > 0);

    SECTION("the facet areas sum to the analytical CAD face area") {
        // Independent of the load: a planar face's facets tile it exactly, so
        // this triangulates an area error before it becomes a force error.
        INFO("facet area sum " << contribution.area.si() << " m^2, analytical " << area);
        CHECK_THAT(contribution.area.si(), WithinRel(area, 1e-12));
    }

    SECTION("the resultant force is t A") {
        const std::array<double, 3> expected{applied.x.si() * area, applied.y.si() * area,
                                             applied.z.si() * area};
        const std::array<double, 3> actual = forceHere(prepared);
        INFO("expected [" << expected[0] << ", " << expected[1] << ", " << expected[2]
                          << "] N, actual [" << actual[0] << ", " << actual[1] << ", "
                          << actual[2] << "] N");
        for (std::size_t axis = 0; axis < 3; ++axis) {
            CHECK_THAT(actual[axis], WithinRel(expected[axis], 1e-12));
        }
        // Production's own accessor agrees with the sum computed here.
        const Force3D reported = prepared.resultantForce();
        CHECK_THAT(reported.x.si(), WithinRel(actual[0], 1e-14));
        CHECK_THAT(reported.y.si(), WithinRel(actual[1], 1e-14));
        CHECK_THAT(reported.z.si(), WithinRel(actual[2], 1e-14));
    }

    SECTION("the resultant moment is A (xc - O) x t, about an off-centre origin") {
        // For a constant traction on a planar face the first moment is the
        // area times the centroid offset crossed into t. The end cap spans
        // x in [0, 0.040] and y in [0, 0.030] at z = 0.020, so its centroid is
        // (0.020, 0.015, 0.020) -- from the dimensions, not from the mesh.
        const Point3D origin = at(-0.1, 0.05, -0.2);
        const double cx = LoadedPart::kWidth / 2.0 - origin.x.si();
        const double cy = LoadedPart::kDepth / 2.0 - origin.y.si();
        const double cz = LoadedPart::kHeight - origin.z.si();
        const double fx = applied.x.si() * area;
        const double fy = applied.y.si() * area;
        const double fz = applied.z.si() * area;
        const std::array<double, 3> expected{cy * fz - cz * fy, cz * fx - cx * fz,
                                             cx * fy - cy * fx};
        const std::array<double, 3> actual = momentHere(prepared, part.volume().mesh(), origin);
        INFO("expected [" << expected[0] << ", " << expected[1] << ", " << expected[2]
                          << "] N m, actual [" << actual[0] << ", " << actual[1] << ", "
                          << actual[2] << "] N m");
        for (std::size_t axis = 0; axis < 3; ++axis) {
            CHECK_THAT(actual[axis], WithinRel(expected[axis], 1e-10));
        }
        // Production's own moment accessor agrees.
        const Result<Moment3D> reported =
            prepared.resultantMomentAbout(part.volume().mesh(), origin);
        REQUIRE(reported.has_value());
        CHECK_THAT(reported->x.si(), WithinRel(actual[0], 1e-12));
        CHECK_THAT(reported->y.si(), WithinRel(actual[1], 1e-12));
        CHECK_THAT(reported->z.si(), WithinRel(actual[2], 1e-12));
    }

    SECTION("a moment accessor refuses a mesh it was not prepared against") {
        const LoadedPart other;
        const Result<Moment3D> refused =
            prepared.resultantMomentAbout(other.volume().mesh(), at(0, 0, 0));
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().code == ErrorCode::FailedPrecondition);
    }
}

TEST_CASE("StructuralLoad_PositivePressureActsInwardAlongTheOutwardNormal",
          "[structural][load]") {
    // THE FROZEN SIGN CONVENTION. The end cap's outward normal is +Z, so a
    // positive pressure must push along -Z. The expected force is `-p A n`,
    // formed here from the block's dimensions.
    const LoadedPart part;
    const double area = LoadedPart::kCapArea;
    const double p = 250000.0; // 0.25 MPa

    const std::vector<StructuralLoad> loads{
        StructuralLoad{LoadId::fromValue(1),
                       PressureLoad{.face = part.endCap(), .magnitude = Pressure::fromSi(p)}}};
    const PreparedLoads prepared = prepare(part, loads);
    const std::array<double, 3> actual = forceHere(prepared);

    INFO("p = " << p << " Pa, A = " << area << " m^2, F = [" << actual[0] << ", " << actual[1]
                << ", " << actual[2] << "] N");
    CHECK_THAT(actual[0], WithinAbs(0.0, 1e-9 * p * area));
    CHECK_THAT(actual[1], WithinAbs(0.0, 1e-9 * p * area));
    CHECK_THAT(actual[2], WithinRel(-p * area, 1e-12));
    // INWARD, asserted as a sign and not merely as a magnitude.
    CHECK(actual[2] < 0.0);

    SECTION("the opposite face is pushed the other way, because its normal is") {
        // The start cap's outward normal is -Z, so the same positive pressure
        // pushes along +Z. Two faces of one body, opposite signs: that is what
        // "follows the normal" means, and a global-direction pressure fails it.
        const std::vector<StructuralLoad> other{
            StructuralLoad{LoadId::fromValue(1),
                           PressureLoad{.face = part.startCap(),
                                        .magnitude = Pressure::fromSi(p)}}};
        const std::array<double, 3> onStart = forceHere(prepare(part, other));
        CHECK_THAT(onStart[2], WithinRel(p * area, 1e-12));
        CHECK(onStart[2] > 0.0);
    }

    SECTION("negative pressure is suction and is not clamped") {
        const std::vector<StructuralLoad> suction{
            StructuralLoad{LoadId::fromValue(1),
                           PressureLoad{.face = part.endCap(),
                                        .magnitude = Pressure::fromSi(-p)}}};
        const std::array<double, 3> pulled = forceHere(prepare(part, suction));
        CHECK_THAT(pulled[2], WithinRel(p * area, 1e-12));
        CHECK(pulled[2] > 0.0);
    }

    SECTION("the area is applied ONCE") {
        // The area vector carries both the outward direction and the area, so
        // a formulation that used the vector AND multiplied by the area would
        // square it. Asserted against that wrong answer as well as the right.
        CHECK(std::abs(actual[2]) < 2.0 * p * area);
        CHECK(std::abs(std::abs(actual[2]) - p * area * area) > 0.1 * p * area);
    }

    SECTION("pressure on a closed surface has no net force") {
        // Every face of the block, so the pressure acts on a closed boundary.
        // The continuum result is zero net force for a uniform pressure, and
        // it is the sharpest orientation regression available: one flipped
        // facet normal would leave a residual.
        std::vector<StructuralLoad> all{
            StructuralLoad{LoadId::fromValue(1),
                           PressureLoad{.face = part.startCap(),
                                        .magnitude = Pressure::fromSi(p)}},
            StructuralLoad{LoadId::fromValue(2),
                           PressureLoad{.face = part.endCap(),
                                        .magnitude = Pressure::fromSi(p)}}};
        for (std::size_t which = 0; which < 4; ++which) {
            all.push_back(StructuralLoad{LoadId::fromValue(3 + which),
                                         PressureLoad{.face = part.side(which),
                                                      .magnitude = Pressure::fromSi(p)}});
        }
        const PreparedLoads closed = prepare(part, all);
        const std::array<double, 3> net = forceHere(closed);
        // The scale is the largest face force, so the residual is measured
        // against something physical rather than against 1.
        const double scale = p * LoadedPart::kWidth * LoadedPart::kDepth;
        INFO("net [" << net[0] << ", " << net[1] << ", " << net[2]
                     << "] N against a face force of " << scale << " N");
        for (const double component : net) {
            CHECK_THAT(component, WithinAbs(0.0, 1e-10 * scale));
        }
    }
}

TEST_CASE("StructuralLoad_TractionIgnoresFacetWindingAndPressureDoesNot",
          "[structural][load]") {
    // The distinction between the two load types, at the level of one facet.
    // A traction is a global vector and only the positive area matters; a
    // pressure follows the oriented normal. Checked on the analytical triangle
    // through the public helpers, because a mesh cannot be handed a flipped
    // facet without corrupting it.
    const std::array<Point3D, 3> t = referenceTriangle();
    const Area forward = structural::facetArea(t[0], t[1], t[2]);
    const Area reversed = structural::facetArea(t[0], t[2], t[1]);
    CHECK(forward.si() == reversed.si());

    SECTION("traction: the same force either way") {
        const Force3D a = structural::facetNodalForce(forward, traction(5, -2, 7));
        const Force3D b = structural::facetNodalForce(reversed, traction(5, -2, 7));
        CHECK(a == b);
    }

    SECTION("pressure: the force reverses with the winding") {
        const double p = 1000.0;
        const Vector3D forwardVector = structural::facetAreaVector(t[0], t[1], t[2]);
        const Vector3D reversedVector = structural::facetAreaVector(t[0], t[2], t[1]);
        CHECK(forwardVector.z == -reversedVector.z);
        const double forwardZ = -p * forwardVector.z / std::abs(forwardVector.z);
        const double reversedZ = -p * reversedVector.z / std::abs(reversedVector.z);
        CHECK(forwardZ == -reversedZ);
        CHECK(forwardZ < 0.0);
    }
}

TEST_CASE("StructuralLoad_BoundaryFacetWindingIsOutwardOfTheOwningTetrahedron",
          "[structural][load]") {
    // THE CHAIN PRESSURE RESTS ON, CHECKED RATHER THAN TRUSTED. P16 stores
    // boundary triangles "with the winding the tetrahedra imply", and
    // kTetFaces' windings "give outward normals" for a positively oriented
    // tetrahedron. So for every boundary facet the area vector must point AWAY
    // from the owning tetrahedron's fourth node. If that ever stops holding,
    // every pressure in the system has the wrong sign -- and the failure would
    // be P16's, which is why this is a test and not an assumption.
    const LoadedPart part;
    const meshing::Mesh& mesh = part.volume().mesh();
    REQUIRE_FALSE(mesh.triangles().empty());

    std::size_t checked = 0;
    for (const meshing::Triangle& facet : mesh.triangles()) {
        std::array<Point3D, 3> corners{};
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const meshing::Node* node = mesh.findNode(facet.nodes[corner]);
            REQUIRE(node != nullptr);
            corners[corner] = node->position;
        }
        const Vector3D areaVector =
            structural::facetAreaVector(corners[0], corners[1], corners[2]);

        bool found = false;
        for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
            std::size_t shared = 0;
            meshing::NodeId opposite{};
            for (const meshing::NodeId node : tet.nodes) {
                if (std::ranges::find(facet.nodes, node) != facet.nodes.end()) {
                    ++shared;
                } else {
                    opposite = node;
                }
            }
            if (shared != 3) {
                continue;
            }
            found = true;
            const meshing::Node* apex = mesh.findNode(opposite);
            REQUIRE(apex != nullptr);
            // The vector from the facet to the apex points INTO the material,
            // so its dot with an outward area vector must be negative.
            const double dx = apex->position.x.si() - corners[0].x.si();
            const double dy = apex->position.y.si() - corners[0].y.si();
            const double dz = apex->position.z.si() - corners[0].z.si();
            const double dot = areaVector.x * dx + areaVector.y * dy + areaVector.z * dz;
            if (!(dot < 0.0)) {
                FAIL("facet " << facet.id.value()
                              << " winds INTO its owning tetrahedron: dot " << dot);
            }
            ++checked;
            break;
        }
        REQUIRE(found);
    }
    INFO(checked << " boundary facets checked against their owning tetrahedron");
    CHECK(checked == mesh.triangles().size());
}

// ---------------------------------------------------------------------------
// Authority: what may and may not be a load target
// ---------------------------------------------------------------------------

TEST_CASE("StructuralLoad_RefusesAnUnresolvedTargetRatherThanTreatingItAsZero",
          "[structural][load]") {
    const LoadedPart part;

    SECTION("a face of another object names no face of this body") {
        const FaceName foreign{ObjectId::fromValue(9999),
                               FaceSelector{.role = FaceRole::EndCap}};
        const std::vector<StructuralLoad> loads{
            StructuralLoad{LoadId::fromValue(4),
                           PressureLoad{.face = foreign,
                                        .magnitude = Pressure::fromSi(1000.0)}}};
        const StructuralModel model = part.model();
        const StructuralMaterial material = part.resolved(StructuralAnalysisMode::LinearStatic);
        CHECK(structural::structuralLoadProblem(model, material, loads) ==
              std::optional<LoadProblem>{LoadProblem::TargetUnresolved});
        const Result<PreparedLoads> refused =
            structural::prepareStructuralLoads(model, material, loads);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().code == ErrorCode::NotFound);
        // The diagnostic names the load, which is what makes it usable when an
        // analysis carries many.
        CHECK_THAT(refused.error().message, ContainsSubstring("load"));
        CHECK_THAT(refused.error().message, ContainsSubstring("Nothing nearby is substituted"));
    }

    SECTION("a role the body does not have") {
        // A hole bottom on a plain extrude: a well-formed selector that names
        // no face here. It must not fall back to a nearby cap.
        const FaceName absent{part.feature, FaceSelector{.role = FaceRole::HoleBottom}};
        const std::vector<StructuralLoad> loads{
            StructuralLoad{LoadId::fromValue(5),
                           SurfaceTractionLoad{.face = absent,
                                               .traction = traction(0, 0, -100)}}};
        const StructuralModel model = part.model();
        CHECK(structural::structuralLoadProblem(
                  model, part.resolved(StructuralAnalysisMode::LinearStatic), loads) ==
              std::optional<LoadProblem>{LoadProblem::TargetUnresolved});
    }

    SECTION("a resolvable face still works, so the refusal is about the target") {
        const std::vector<StructuralLoad> loads{
            StructuralLoad{LoadId::fromValue(6),
                           SurfaceTractionLoad{.face = part.endCap(),
                                               .traction = traction(0, 0, -100)}}};
        CHECK_FALSE(structural::structuralLoadProblem(
                        part.model(), part.resolved(StructuralAnalysisMode::LinearStatic), loads)
                        .has_value());
    }

    SECTION("every LoadProblem has a name") {
        CHECK(structural::toString(LoadProblem::DuplicateLoadId) == "duplicate_load_id");
        CHECK(structural::toString(LoadProblem::NonFiniteValue) == "non_finite_value");
        CHECK(structural::toString(LoadProblem::TargetUnresolved) == "target_unresolved");
        CHECK(structural::toString(LoadProblem::TargetInvalid) == "target_invalid");
        CHECK(structural::toString(LoadProblem::TargetWithoutFacets) == "target_without_facets");
        CHECK(structural::toString(LoadProblem::NodeNotInMesh) == "node_not_in_mesh");
        CHECK(structural::toString(LoadProblem::DegenerateFacet) == "degenerate_facet");
        CHECK(structural::toString(LoadProblem::DensityMissing) == "density_missing");
    }
}

TEST_CASE("StructuralLoad_RefusesAMalformedFaceSelector", "[structural][load]") {
    // A Side role with no entity is malformed on core's own terms, which is a
    // different failure from a well-formed reference that resolves to nothing.
    const LoadedPart part;
    const FaceName malformed{part.feature, FaceSelector{.role = FaceRole::Side}};
    const std::vector<StructuralLoad> loads{
        StructuralLoad{LoadId::fromValue(7),
                       PressureLoad{.face = malformed, .magnitude = Pressure::fromSi(10.0)}}};
    const std::optional<LoadProblem> problem = structural::structuralLoadProblem(
        part.model(), part.resolved(StructuralAnalysisMode::LinearStatic), loads);
    INFO("problem " << (problem.has_value() ? structural::toString(*problem) : "none"));
    REQUIRE(problem.has_value());
    // Either refusal is defensible and both are failures; what matters is that
    // it is not accepted.
    CHECK((*problem == LoadProblem::TargetInvalid || *problem == LoadProblem::TargetUnresolved));
}

TEST_CASE("StructuralLoad_NodalForceIsMeshLocalAndDiesWithItsMesh", "[structural][load]") {
    // The declared policy, tested. A NodeId is a handle into one generation of
    // one mesh, so a nodal force carries the stamp and is refused against any
    // other -- including one with the same node count and the same numeric
    // handles, which a remesh of an unchanged model produces.
    LoadedPart part;
    const meshing::MeshStamp before = part.volume().mesh().stamp();
    const meshing::NodeId node = part.volume().mesh().nodes()[0].id;
    const Force3D force{Force::fromSi(1.0), Force::fromSi(2.0), Force::fromSi(3.0)};

    const std::vector<StructuralLoad> loads{
        StructuralLoad{LoadId::fromValue(1),
                       NodalForceLoad{.mesh = before, .node = node, .force = force}}};

    SECTION("it applies exactly, with no integration") {
        const PreparedLoads prepared = prepare(part, loads);
        REQUIRE(prepared.nodal().size() == 1);
        CHECK(prepared.nodal()[0].node == node);
        CHECK(prepared.nodal()[0].force == force);
        const std::array<double, 3> total = forceHere(prepared);
        CHECK(total[0] == 1.0);
        CHECK(total[1] == 2.0);
        CHECK(total[2] == 3.0);

        // And its moment about the origin is the node position crossed into
        // the force, computed here from the node's own coordinates.
        const meshing::Node* position = part.volume().mesh().findNode(node);
        REQUIRE(position != nullptr);
        const double x = position->position.x.si();
        const double y = position->position.y.si();
        const double z = position->position.z.si();
        const std::array<double, 3> expected{y * 3.0 - z * 2.0, z * 1.0 - x * 3.0,
                                             x * 2.0 - y * 1.0};
        const Result<Moment3D> moment =
            prepared.resultantMomentAbout(part.volume().mesh(), at(0, 0, 0));
        REQUIRE(moment.has_value());
        CHECK_THAT(moment->x.si(), WithinAbs(expected[0], 1e-15));
        CHECK_THAT(moment->y.si(), WithinAbs(expected[1], 1e-15));
        CHECK_THAT(moment->z.si(), WithinAbs(expected[2], 1e-15));
    }

    SECTION("a node the mesh does not have is refused, with no nearest-node fallback") {
        const std::vector<StructuralLoad> absent{
            StructuralLoad{LoadId::fromValue(1),
                           NodalForceLoad{.mesh = before,
                                          .node = meshing::NodeId::fromValue(999999),
                                          .force = force}}};
        const StructuralModel model = part.model();
        CHECK(structural::structuralLoadProblem(
                  model, part.resolved(StructuralAnalysisMode::LinearStatic), absent) ==
              std::optional<LoadProblem>{LoadProblem::NodeNotInMesh});
    }

    SECTION("a remesh invalidates it, even with the same handles") {
        part.remesh();
        const meshing::MeshStamp after = part.volume().mesh().stamp();
        REQUIRE(after != before);
        // The handle is still a node of the new mesh, which is exactly why the
        // stamp and not the handle is what decides.
        REQUIRE(part.volume().mesh().findNode(node) != nullptr);
        const StructuralModel model = part.model();
        CHECK(structural::structuralLoadProblem(
                  model, part.resolved(StructuralAnalysisMode::LinearStatic), loads) ==
              std::optional<LoadProblem>{LoadProblem::NodeNotInMesh});
        const Result<PreparedLoads> refused = structural::prepareStructuralLoads(
            model, part.resolved(StructuralAnalysisMode::LinearStatic), loads);
        REQUIRE_FALSE(refused.has_value());
        CHECK_THAT(refused.error().message, ContainsSubstring("different mesh generation"));

        // Rebinding it to the current mesh works, which is the intended
        // recovery and proves the refusal was about the generation.
        const std::vector<StructuralLoad> rebound{
            StructuralLoad{LoadId::fromValue(1),
                           NodalForceLoad{.mesh = after, .node = node, .force = force}}};
        CHECK_FALSE(structural::structuralLoadProblem(
                        model, part.resolved(StructuralAnalysisMode::LinearStatic), rebound)
                        .has_value());
    }
}

TEST_CASE("StructuralLoad_RefusesNonFiniteValuesAndDuplicateIdentities",
          "[structural][load]") {
    const LoadedPart part;
    const StructuralModel model = part.model();
    const StructuralMaterial material = part.resolved(StructuralAnalysisMode::LinearStatic);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();

    SECTION("a non-finite force component") {
        const std::vector<StructuralLoad> loads{
            StructuralLoad{LoadId::fromValue(1),
                           NodalForceLoad{.mesh = part.volume().mesh().stamp(),
                                          .node = part.volume().mesh().nodes()[0].id,
                                          .force = Force3D{Force::fromSi(nan), {}, {}}}}};
        CHECK(structural::structuralLoadProblem(model, material, loads) ==
              std::optional<LoadProblem>{LoadProblem::NonFiniteValue});
    }

    SECTION("a non-finite traction component") {
        const std::vector<StructuralLoad> loads{
            StructuralLoad{LoadId::fromValue(1),
                           SurfaceTractionLoad{.face = part.endCap(),
                                               .traction = traction(0, inf, 0)}}};
        CHECK(structural::structuralLoadProblem(model, material, loads) ==
              std::optional<LoadProblem>{LoadProblem::NonFiniteValue});
    }

    SECTION("a non-finite pressure") {
        const std::vector<StructuralLoad> loads{
            StructuralLoad{LoadId::fromValue(1),
                           PressureLoad{.face = part.endCap(),
                                        .magnitude = Pressure::fromSi(nan)}}};
        CHECK(structural::structuralLoadProblem(model, material, loads) ==
              std::optional<LoadProblem>{LoadProblem::NonFiniteValue});
    }

    SECTION("a non-finite acceleration") {
        const std::vector<StructuralLoad> loads{
            StructuralLoad{LoadId::fromValue(1),
                           GravityLoad{.acceleration = Vector3D{0, 0, -inf}}}};
        CHECK(structural::structuralLoadProblem(model, material, loads) ==
              std::optional<LoadProblem>{LoadProblem::NonFiniteValue});
    }

    SECTION("two records for one LoadId") {
        const std::vector<StructuralLoad> loads{
            StructuralLoad{LoadId::fromValue(1),
                           PressureLoad{.face = part.endCap(),
                                        .magnitude = Pressure::fromSi(100.0)}},
            StructuralLoad{LoadId::fromValue(1),
                           PressureLoad{.face = part.startCap(),
                                        .magnitude = Pressure::fromSi(200.0)}}};
        CHECK(structural::structuralLoadProblem(model, material, loads) ==
              std::optional<LoadProblem>{LoadProblem::DuplicateLoadId});
        const Result<PreparedLoads> refused =
            structural::prepareStructuralLoads(model, material, loads);
        REQUIRE_FALSE(refused.has_value());
        CHECK_THAT(refused.error().message, ContainsSubstring("more than once"));
    }

    SECTION("two DIFFERENT ids on the same face superpose and are accepted") {
        // Overlapping targets are not an error: linear statics superposes, and
        // refusing them would decide the user's model for them.
        const std::vector<StructuralLoad> loads{
            StructuralLoad{LoadId::fromValue(1),
                           PressureLoad{.face = part.endCap(),
                                        .magnitude = Pressure::fromSi(100.0)}},
            StructuralLoad{LoadId::fromValue(2),
                           PressureLoad{.face = part.endCap(),
                                        .magnitude = Pressure::fromSi(200.0)}}};
        CHECK_FALSE(structural::structuralLoadProblem(model, material, loads).has_value());
        const PreparedLoads prepared = prepare(part, loads);
        const std::array<double, 3> total = forceHere(prepared);
        // 300 Pa inward over the cap.
        CHECK_THAT(total[2], WithinRel(-300.0 * LoadedPart::kCapArea, 1e-12));
    }

    SECTION("an atomic refusal publishes nothing") {
        // One bad load refuses the whole set, so a consumer never receives a
        // partially prepared field.
        const std::vector<StructuralLoad> mixed{
            StructuralLoad{LoadId::fromValue(1),
                           PressureLoad{.face = part.endCap(),
                                        .magnitude = Pressure::fromSi(100.0)}},
            StructuralLoad{LoadId::fromValue(2),
                           PressureLoad{.face = FaceName{ObjectId::fromValue(9999),
                                                         FaceSelector{.role = FaceRole::EndCap}},
                                        .magnitude = Pressure::fromSi(200.0)}}};
        CHECK_FALSE(structural::prepareStructuralLoads(model, material, mixed).has_value());
    }
}

// ---------------------------------------------------------------------------
// Superposition, ordering and determinism
// ---------------------------------------------------------------------------

TEST_CASE("StructuralLoad_SuperposesAndIsIndependentOfListOrder", "[structural][load]") {
    const LoadedPart part;
    const StructuralLoad a{LoadId::fromValue(1),
                           SurfaceTractionLoad{.face = part.endCap(),
                                               .traction = traction(1000, 0, -2000)}};
    const StructuralLoad b{LoadId::fromValue(2),
                           PressureLoad{.face = part.side(0),
                                        .magnitude = Pressure::fromSi(50000.0)}};

    const PreparedLoads onlyA = prepare(part, {a});
    const PreparedLoads onlyB = prepare(part, {b});
    const PreparedLoads both = prepare(part, {a, b});
    const PreparedLoads reversed = prepare(part, {b, a});

    SECTION("the combined resultant is the sum of the separate ones") {
        const std::array<double, 3> fa = forceHere(onlyA);
        const std::array<double, 3> fb = forceHere(onlyB);
        const std::array<double, 3> fboth = forceHere(both);
        const double scale = std::max({std::abs(fa[0]), std::abs(fa[2]), std::abs(fb[1]), 1.0});
        for (std::size_t axis = 0; axis < 3; ++axis) {
            INFO("axis " << axis << ": " << fa[axis] << " + " << fb[axis] << " vs "
                         << fboth[axis]);
            CHECK_THAT(fboth[axis], WithinAbs(fa[axis] + fb[axis], 1e-11 * scale));
        }
    }

    SECTION("and so is the combined moment") {
        const Point3D origin = at(0.01, -0.02, 0.005);
        const std::array<double, 3> ma = momentHere(onlyA, part.volume().mesh(), origin);
        const std::array<double, 3> mb = momentHere(onlyB, part.volume().mesh(), origin);
        const std::array<double, 3> mboth = momentHere(both, part.volume().mesh(), origin);
        double scale = 1.0;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            scale = std::max(scale, std::abs(ma[axis]) + std::abs(mb[axis]));
        }
        for (std::size_t axis = 0; axis < 3; ++axis) {
            CHECK_THAT(mboth[axis], WithinAbs(ma[axis] + mb[axis], 1e-11 * scale));
        }
    }

    SECTION("reversing the list gives the same nodal field") {
        // The nodal field accumulates into an ascending map, so the emitted
        // order cannot depend on the input order. The per-node values can
        // differ at roundoff only if the summation order differs, which is
        // what the map prevents for a node loaded by one load -- and both
        // loads here reach disjoint face sets apart from the shared edge.
        REQUIRE(reversed.nodal().size() == both.nodal().size());
        double worst = 0.0;
        double scale = 1.0;
        for (std::size_t i = 0; i < both.nodal().size(); ++i) {
            CHECK(reversed.nodal()[i].node == both.nodal()[i].node);
            const Force3D& p = both.nodal()[i].force;
            const Force3D& q = reversed.nodal()[i].force;
            worst = std::max({worst, std::abs(p.x.si() - q.x.si()),
                              std::abs(p.y.si() - q.y.si()), std::abs(p.z.si() - q.z.si())});
            scale = std::max({scale, std::abs(p.x.si()), std::abs(p.y.si()),
                              std::abs(p.z.si())});
        }
        INFO("worst per-node difference " << worst << " against a scale of " << scale);
        CHECK(worst <= 1e-14 * scale);
    }

    SECTION("removing a load leaves exactly the other contribution") {
        const PreparedLoads again = prepare(part, {a});
        REQUIRE(again.nodal().size() == onlyA.nodal().size());
        for (std::size_t i = 0; i < again.nodal().size(); ++i) {
            CHECK(again.nodal()[i] == onlyA.nodal()[i]);
        }
    }
}

TEST_CASE("StructuralLoad_IsDeterministicOverRepeatedPreparation", "[structural][load]") {
    // The operations are a fixed traversal with no state, so repetition within
    // one build should be bit-identical -- asserted as equality of the whole
    // field rather than with a tolerance that would hide a dependence on
    // something that varies.
    const LoadedPart part;
    const std::vector<StructuralLoad> loads{
        StructuralLoad{LoadId::fromValue(1),
                       SurfaceTractionLoad{.face = part.endCap(),
                                           .traction = traction(1000, -500, 2000)}},
        StructuralLoad{LoadId::fromValue(2),
                       PressureLoad{.face = part.side(1),
                                    .magnitude = Pressure::fromSi(75000.0)}},
        StructuralLoad{LoadId::fromValue(3),
                       GravityLoad{.acceleration = Vector3D{0, 0, -structural::kStandardGravity}}}};

    const PreparedLoads first =
        prepare(part, loads, StructuralAnalysisMode::LinearStaticWithGravity);
    REQUIRE_FALSE(first.nodal().empty());
    CHECK(std::ranges::is_sorted(first.nodal(), {}, &NodalLoad::node));

    for (int repeat = 0; repeat < 8; ++repeat) {
        const PreparedLoads again =
            prepare(part, loads, StructuralAnalysisMode::LinearStaticWithGravity);
        INFO("repeat " << repeat);
        CHECK(again == first);
        REQUIRE(again.nodal().size() == first.nodal().size());
        for (std::size_t i = 0; i < first.nodal().size(); ++i) {
            CHECK(again.nodal()[i] == first.nodal()[i]);
        }
        CHECK(again.contributions().size() == first.contributions().size());
    }
}

// ---------------------------------------------------------------------------
// Gravity
// ---------------------------------------------------------------------------

TEST_CASE("StructuralLoad_GravityWeighsTheMeshedBodyAndNeedsADensity",
          "[structural][load]") {
    // The total weight must be `rho V g`, with V the meshed volume -- and the
    // block's analytic volume is known from its dimensions, so the expectation
    // does not come from the mesh.
    const LoadedPart part;
    const double density = 7850.0;
    const double g = structural::kStandardGravity;
    const double analyticVolume = LoadedPart::kWidth * LoadedPart::kDepth * LoadedPart::kHeight;

    const std::vector<StructuralLoad> loads{
        StructuralLoad{LoadId::fromValue(1),
                       GravityLoad{.acceleration = Vector3D{0, 0, -g}}}};
    const PreparedLoads prepared =
        prepare(part, loads, StructuralAnalysisMode::LinearStaticWithGravity);

    SECTION("the total weight is rho V g, against the ANALYTIC volume") {
        const std::array<double, 3> total = forceHere(prepared);
        const double expected = -density * analyticVolume * g;
        INFO("analytic V " << analyticVolume << " m^3, mesh V "
                           << part.volume().tetrahedralVolume().si() << " m^3, expected "
                           << expected << " N, actual " << total[2] << " N");
        CHECK_THAT(total[0], WithinAbs(0.0, 1e-9 * std::abs(expected)));
        CHECK_THAT(total[1], WithinAbs(0.0, 1e-9 * std::abs(expected)));
        // A planar-faced block is tiled exactly by its tetrahedra, so the mesh
        // volume equals the analytic one to accumulation and the weight does
        // too.
        CHECK_THAT(total[2], WithinRel(expected, 1e-10));
        // And it acts downwards, because the acceleration does.
        CHECK(total[2] < 0.0);
    }

    SECTION("the moment is that of the weight at the centre of volume") {
        // For a homogeneous block the centre of mass is its geometric centre,
        // which the dimensions give directly.
        const Point3D origin = at(0, 0, 0);
        const double weight = -density * analyticVolume * g;
        const double cx = LoadedPart::kWidth / 2.0;
        const double cy = LoadedPart::kDepth / 2.0;
        const std::array<double, 3> expected{cy * weight, -cx * weight, 0.0};
        const std::array<double, 3> actual = momentHere(prepared, part.volume().mesh(), origin);
        const double scale = std::abs(weight) * LoadedPart::kWidth;
        INFO("expected [" << expected[0] << ", " << expected[1] << ", " << expected[2]
                          << "], actual [" << actual[0] << ", " << actual[1] << ", " << actual[2]
                          << "] N m");
        CHECK_THAT(actual[0], WithinRel(expected[0], 1e-10));
        CHECK_THAT(actual[1], WithinRel(expected[1], 1e-10));
        CHECK_THAT(actual[2], WithinAbs(0.0, 1e-10 * scale));
    }

    SECTION("every node of the mesh carries some of the weight") {
        // A body force reaches every node, unlike a surface load.
        CHECK(prepared.nodal().size() == part.volume().mesh().nodes().size());
        REQUIRE(prepared.contributions().size() == 1);
        CHECK(prepared.contributions()[0].kind == LoadKind::Gravity);
        CHECK(prepared.contributions()[0].elements ==
              part.volume().mesh().tetrahedra().size());
    }

    SECTION("the direction is explicit, with no implicit -Z") {
        const std::vector<StructuralLoad> sideways{
            StructuralLoad{LoadId::fromValue(1),
                           GravityLoad{.acceleration = Vector3D{g, 0, 0}}}};
        const std::array<double, 3> total =
            forceHere(prepare(part, sideways, StructuralAnalysisMode::LinearStaticWithGravity));
        const double expected = density * analyticVolume * g;
        CHECK_THAT(total[0], WithinRel(expected, 1e-10));
        CHECK_THAT(total[2], WithinAbs(0.0, 1e-9 * expected));
    }

    SECTION("a material with no density refuses, with no default") {
        // P15 owns the density and reports it missing. There is no 7850 here.
        CHECK(structural::structuralLoadProblem(
                  part.model(), part.resolved(StructuralAnalysisMode::LinearStatic), loads) ==
              std::optional<LoadProblem>{LoadProblem::DensityMissing});
        const Result<PreparedLoads> refused = structural::prepareStructuralLoads(
            part.model(), part.resolved(StructuralAnalysisMode::LinearStatic), loads);
        REQUIRE_FALSE(refused.has_value());
        CHECK_THAT(refused.error().message, ContainsSubstring("no density"));
    }

    SECTION("a load set WITHOUT gravity never consults the density") {
        // The no-gravity material carries none, and a traction does not care.
        const std::vector<StructuralLoad> surface{
            StructuralLoad{LoadId::fromValue(1),
                           SurfaceTractionLoad{.face = part.endCap(),
                                               .traction = traction(0, 0, -1000)}}};
        CHECK_FALSE(structural::structuralLoadProblem(
                        part.model(), part.resolved(StructuralAnalysisMode::LinearStatic), surface)
                        .has_value());
    }

    SECTION("a density edit changes the weight") {
        LoadedPart heavier;
        REQUIRE(features::setMaterialMechanical(
                    heavier.document, heavier.material,
                    [] {
                        materials::MechanicalProperties properties;
                        properties.youngsModulus =
                            materials::MaterialProperty<ElasticModulus>::known(210_GPa);
                        properties.poissonRatio =
                            materials::MaterialProperty<bettercad::PoissonRatio>::known(
                                PoissonRatio::of(0.3));
                        properties.density =
                            materials::MaterialProperty<Density>::known(Density::fromSi(2700.0));
                        return properties;
                    }())
                    .has_value());
        const std::array<double, 3> aluminium = forceHere(
            prepare(heavier, loads, StructuralAnalysisMode::LinearStaticWithGravity));
        const double expected = -2700.0 * analyticVolume * g;
        INFO("aluminium weight " << aluminium[2] << " N, expected " << expected << " N");
        CHECK_THAT(aluminium[2], WithinRel(expected, 1e-10));
    }
}

// ---------------------------------------------------------------------------
// Scale laws
// ---------------------------------------------------------------------------

TEST_CASE("StructuralLoad_FollowsTheSurfaceAndBodyScaleLaws", "[structural][load]") {
    // A traction is force per area, so `F ∝ s^2` and `M ∝ s^3` about a
    // correspondingly scaled origin. Gravity is force per volume, so
    // `F ∝ s^3`. These are the sharpest unit checks in the milestone: a
    // formulation that applied the area twice, or dropped it, breaks the
    // exponent.
    const double p = 100000.0;
    const Traction3D applied = traction(1000, -500, 2000);

    struct Measured {
        double scale;
        std::array<double, 3> traction;
        std::array<double, 3> pressure;
        std::array<double, 3> gravity;
        std::array<double, 3> moment;
    };
    std::vector<Measured> measured;

    for (const double millimetres : {20.0, 40.0, 80.0}) {
        // A geometrically similar block: every dimension scales together.
        Document document{"Scaled"};
        features::Regenerator regenerator;
        meshing::Mesher mesher;
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        addRectangle(*sketch, 0_mm, 0_mm, Length::fromSi(millimetres * 1e-3),
                     Length::fromSi(millimetres * 0.75e-3));
        const ObjectId profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()),
                      .depth = Length::fromSi(millimetres * 0.5e-3)});
        REQUIRE(extrude.has_value());
        const ObjectId feature = require(document.addObject(std::move(*extrude)));
        auto intent =
            meshing::MeshControl::create("Mesh", meshing::MeshControlDefinition{.body = feature});
        REQUIRE(intent.has_value());
        const MeshControlId control =
            MeshControlId::fromValue(require(document.addObject(std::move(*intent))).value());

        features::MaterialDefinition definition;
        definition.designation = "Steel";
        definition.mechanical.youngsModulus =
            materials::MaterialProperty<ElasticModulus>::known(210_GPa);
        definition.mechanical.poissonRatio =
            materials::MaterialProperty<bettercad::PoissonRatio>::known(PoissonRatio::of(0.3));
        definition.mechanical.density =
            materials::MaterialProperty<Density>::known(Density::fromSi(7850.0));
        const Result<MaterialId> id = features::createMaterial(document, "Steel", definition);
        REQUIRE(id.has_value());
        REQUIRE(features::assignMaterial(document, *id).has_value());
        requireReport(regenerator, document);
        REQUIRE(mesher.generate(document, regenerator, control).has_value());

        Result<StructuralModel> model =
            structural::requireStructuralModel(document, regenerator, mesher, control);
        INFO((model.has_value() ? std::string{} : model.error().message));
        REQUIRE(model.has_value());
        Result<StructuralMaterial> material = structural::resolveStructuralMaterial(
            document, feature, StructuralAnalysisMode::LinearStaticWithGravity);
        REQUIRE(material.has_value());
        const FaceName cap{feature, FaceSelector{.role = FaceRole::EndCap}};

        const std::vector<StructuralLoad> tractionOnly{
            StructuralLoad{LoadId::fromValue(1),
                           SurfaceTractionLoad{.face = cap, .traction = applied}}};
        const std::vector<StructuralLoad> pressureOnly{
            StructuralLoad{LoadId::fromValue(1),
                           PressureLoad{.face = cap, .magnitude = Pressure::fromSi(p)}}};
        const std::vector<StructuralLoad> gravityOnly{
            StructuralLoad{LoadId::fromValue(1),
                           GravityLoad{.acceleration =
                                           Vector3D{0, 0, -structural::kStandardGravity}}}};

        Result<PreparedLoads> t = structural::prepareStructuralLoads(*model, *material, tractionOnly);
        Result<PreparedLoads> q = structural::prepareStructuralLoads(*model, *material, pressureOnly);
        Result<PreparedLoads> w = structural::prepareStructuralLoads(*model, *material, gravityOnly);
        REQUIRE(t.has_value());
        REQUIRE(q.has_value());
        REQUIRE(w.has_value());

        // The origin scales with the geometry, so the moment scales as s^3.
        const Point3D origin = at(-millimetres * 1e-3, millimetres * 0.5e-3, -millimetres * 1e-3);
        measured.push_back(Measured{millimetres / 20.0, forceHere(*t), forceHere(*q),
                                    forceHere(*w),
                                    momentHere(*t, model->mesh().mesh(), origin)});
    }

    REQUIRE(measured.size() == 3);
    for (std::size_t i = 1; i < measured.size(); ++i) {
        const double s = measured[i].scale / measured[0].scale;
        const double area = s * s;
        const double volume = s * s * s;
        INFO("scale " << s);
        CHECK_THAT(measured[i].traction[2] / measured[0].traction[2], WithinRel(area, 1e-9));
        CHECK_THAT(measured[i].pressure[2] / measured[0].pressure[2], WithinRel(area, 1e-9));
        CHECK_THAT(measured[i].gravity[2] / measured[0].gravity[2], WithinRel(volume, 1e-8));
        CHECK_THAT(measured[i].moment[0] / measured[0].moment[0], WithinRel(volume, 1e-8));
    }
}
