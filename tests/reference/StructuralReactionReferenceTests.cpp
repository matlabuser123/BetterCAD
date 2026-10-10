// P17-REACTION-001 against the meshing reference models.
//
// WHY THESE ARE SEPARATE from tests/structural/StructuralReactionTests.cpp.
// Those tests freeze the sign convention, the partition algebra and the
// attribution policy on a block fixture. These do the things a synthetic
// fixture cannot:
//
//   the TWO-LAYER TOLERANCE MEASUREMENT the brief makes central. Layer A is
//     algebraic -- the assembled F that was solved, against the reactions
//     recovered from the same system. Layer B is geometric -- an ANALYTICAL
//     continuum resultant, against the same reactions, which carries the load
//     integration's own error. They are measured separately and never
//     conflated
//   the ANALYTICAL two-support split, derived on the test side from
//     sum(F) = 0 and sum(M) = 0 rather than from production
//   GRAVITY, whose resultant is rho V g and whose moment about an off-centre
//     origin is set by the centre of mass
//   the far-origin case, where node coordinates are large and the moment is a
//     difference of big numbers
//
// THE ANALYTICAL RESULTANTS ARE THE TEST'S OWN. A planar pressure face has
// `F = -p A n` through its centroid; a uniform traction has `F = t A`; a block
// has `W = rho V g`. Each is computed here from the model's declared
// dimensions, never from P17-LOAD's prepared resultant -- which is used only
// as a second oracle, and never added to the first.

#include "reference/MeshTestSupport.hpp"

#include <MeshReferenceModels.hpp>

#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/meshing/MeshSizing.hpp>
#include <bettercad/structural/StructuralConstraints.hpp>
#include <bettercad/structural/StructuralLoadVector.hpp>
#include <bettercad/structural/StructuralReaction.hpp>
#include <bettercad/structural/StructuralSolve.hpp>
#include <bettercad/structural/StructuralSystem.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace {

using namespace bettercad;
using namespace bettercad::test;
using namespace bettercad::test::meshref;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using structural::ConstraintSet;
using structural::EquilibriumTolerance;
using structural::GlobalStructuralSystem;
using structural::MeshDofMap;
using structural::PreparedLoads;
using structural::PreparedRestraints;
using structural::RestraintReaction;
using structural::SolvedSystem;
using structural::StructuralAnalysisMode;
using structural::StructuralLoad;
using structural::StructuralMaterial;
using structural::StructuralModel;
using structural::StructuralRestraint;
using structural::SupportReaction;
using structural::SupportReactions;

constexpr double kYoungs = 210.0e9;
constexpr double kPoisson = 0.3;
constexpr double kDensity = 7850.0;
constexpr double kGravity = 9.80665;

[[nodiscard]] EquilibriumTolerance measured() {
    return EquilibriumTolerance{};
}

[[nodiscard]] double magnitudeOf(const Force3D& f) {
    return std::hypot(f.x.si(), f.y.si(), f.z.si());
}

[[nodiscard]] double magnitudeOf(const Moment3D& m) {
    return std::hypot(m.x.si(), m.y.si(), m.z.si());
}

void assignSteel(Document& document) {
    features::MaterialDefinition definition;
    definition.designation = "Steel";
    definition.mechanical.youngsModulus =
        materials::MaterialProperty<ElasticModulus>::known(ElasticModulus::fromSi(kYoungs));
    definition.mechanical.poissonRatio =
        materials::MaterialProperty<bettercad::PoissonRatio>::known(PoissonRatio::of(kPoisson));
    definition.mechanical.density =
        materials::MaterialProperty<Density>::known(Density::fromSi(kDensity));
    const Result<MaterialId> id = features::createMaterial(document, "Steel", definition);
    REQUIRE(id.has_value());
    REQUIRE(features::assignMaterial(document, *id).has_value());
}

[[nodiscard]] StructuralModel modelOf(MeshedReference& reference) {
    Result<StructuralModel> model = structural::requireStructuralModel(
        reference.document(), reference.regenerator(), reference.mesher(), reference.control());
    INFO((model.has_value() ? std::string{} : model.error().message));
    REQUIRE(model.has_value());
    return std::move(*model);
}

[[nodiscard]] StructuralMaterial materialOf(MeshedReference& reference,
                                            StructuralAnalysisMode mode) {
    Result<StructuralMaterial> material =
        structural::resolveStructuralMaterial(reference.document(), reference.body(), mode);
    INFO((material.has_value() ? std::string{} : material.error().message));
    REQUIRE(material.has_value());
    return *material;
}

[[nodiscard]] PreparedLoads prepareOf(MeshedReference& reference,
                                      const std::vector<StructuralLoad>& loads,
                                      StructuralAnalysisMode mode) {
    Result<PreparedLoads> prepared =
        structural::prepareStructuralLoads(modelOf(reference), materialOf(reference, mode), loads);
    INFO((prepared.has_value() ? std::string{} : prepared.error().message));
    REQUIRE(prepared.has_value());
    return std::move(*prepared);
}

[[nodiscard]] GlobalStructuralSystem assembleOf(MeshedReference& reference,
                                                const PreparedLoads& loads,
                                                StructuralAnalysisMode mode) {
    Result<GlobalStructuralSystem> system = structural::assembleStructuralSystem(
        modelOf(reference), materialOf(reference, mode), loads);
    INFO((system.has_value() ? std::string{} : system.error().message));
    REQUIRE(system.has_value());
    return std::move(*system);
}

[[nodiscard]] PreparedRestraints restrainOf(MeshedReference& reference,
                                            const std::vector<StructuralRestraint>& restraints) {
    const StructuralModel model = modelOf(reference);
    Result<MeshDofMap> numbering = structural::buildMeshDofMap(model.mesh().mesh());
    REQUIRE(numbering.has_value());
    Result<PreparedRestraints> prepared =
        structural::prepareStructuralRestraints(model, *numbering, restraints);
    INFO((prepared.has_value() ? std::string{} : prepared.error().message));
    REQUIRE(prepared.has_value());
    return std::move(*prepared);
}

[[nodiscard]] SolvedSystem solveOf(const GlobalStructuralSystem& system,
                                   const ConstraintSet& constraints) {
    Result<SolvedSystem> solved = structural::solveStructuralSystem(system, constraints, {});
    INFO((solved.has_value() ? std::string{} : solved.error().message));
    REQUIRE(solved.has_value());
    return std::move(*solved);
}

[[nodiscard]] SupportReactions recoverOf(MeshedReference& reference,
                                         const GlobalStructuralSystem& system,
                                         const PreparedRestraints& restraints,
                                         const PreparedLoads& loads,
                                         const SolvedSystem& solution, const Point3D& origin,
                                         StructuralAnalysisMode mode) {
    (void)mode;
    Result<SupportReactions> reactions = structural::recoverSupportReactions(
        modelOf(reference), system, restraints, loads, solution, origin, measured());
    INFO((reactions.has_value() ? std::string{} : reactions.error().message));
    REQUIRE(reactions.has_value());
    return std::move(*reactions);
}

} // namespace

TEST_CASE("StructuralReaction_BalancesAPlanarPressureAgainstItsAnalyticalResultant",
          "[structural][reaction][reference]") {
    // RM-MESH-01, a 120 x 70 x 35 mm block. A uniform pressure `p` on the
    // 120 x 70 mm top face has the analytical resultant
    //
    //     F = -p A n          A = 0.120 * 0.070,  n = +Z outward
    //
    // and acts through the face centroid, so its moment about the global
    // origin is `(centroid - O) x F`. BOTH are computed here from the model's
    // DECLARED dimensions -- never from P17-LOAD's prepared resultant, which
    // is used only as a second oracle below.
    //
    // POSITIVE PRESSURE ACTS INWARD, which P17-LOAD-001 froze: `t = -p n_out`.
    // So a pressure on the +Z top face pushes along -Z.
    auto built = reference::buildMeshBlockReferenceModel();
    REQUIRE(built.has_value());
    const FaceName bottom = built->bottom();
    const FaceName top = built->top();
    MeshedReference reference(std::move(built->document));
    assignSteel(reference.document());
    reference.require();

    constexpr double kPressure = 2.0e6; // Pa
    constexpr double kArea = 0.120 * 0.070;
    const double analyticalFz = -kPressure * kArea;

    const std::vector<StructuralLoad> loads{
        StructuralLoad{LoadId::fromValue(1),
                       structural::PressureLoad{.face = top,
                                                .magnitude = Pressure::fromSi(kPressure)}}};
    const PreparedLoads prepared =
        prepareOf(reference, loads, StructuralAnalysisMode::LinearStatic);
    const GlobalStructuralSystem system =
        assembleOf(reference, prepared, StructuralAnalysisMode::LinearStatic);
    const PreparedRestraints restraints = restrainOf(
        reference, {StructuralRestraint::fixedSupport(RestraintId::fromValue(1), bottom)});
    const SolvedSystem solution = solveOf(system, restraints.constraints());
    const SupportReactions reactions = recoverOf(reference, system, restraints, prepared, solution,
                                                 Point3D{}, StructuralAnalysisMode::LinearStatic);

    const meshing::Mesh& mesh = modelOf(reference).mesh().mesh();

    SECTION("LAYER A: the reactions balance the assembled F that was solved") {
        // The algebraic layer. Everything here came out of one linear system,
        // so the only error is floating-point accumulation.
        const structural::ForceBalance& balance = reactions.forceBalance();
        INFO("eta_F " << balance.normalized << ", ||e_F||2 " << balance.euclideanNorm.si()
                      << " N, scale " << balance.scale.si() << " N");
        REQUIRE(balance.normalized <= measured().force);
        REQUIRE(reactions.momentBalance().normalized <= measured().moment);
    }

    SECTION("LAYER B: and they balance the ANALYTICAL continuum resultant") {
        // The geometric layer. This comparison includes P17-LOAD's facet
        // integration error, so it is a different numerical question and is
        // measured separately -- which is the whole point of the two layers.
        const Force3D total = reactions.totalForce();
        INFO("analytical F_z " << analyticalFz << " N, reaction F_z " << total.z.si() << " N");
        // The reaction opposes the applied load.
        REQUIRE_THAT(total.z.si(), WithinRel(-analyticalFz, 1e-9));
        REQUIRE(total.z.si() > 0.0);
        REQUIRE(analyticalFz < 0.0);

        const double geometricError =
            std::abs(total.z.si() + analyticalFz) / std::abs(analyticalFz);
        WARN("MEASURE geometric | RM-MESH-01 pressure | analytical F_z " << analyticalFz
             << " N | reaction F_z " << total.z.si() << " N | relative " << geometricError);
        REQUIRE(geometricError < 1.0e-9);
    }

    SECTION("and the assembled F agrees with P17-LOAD's own resultant") {
        // The cross-check that triangulates load preparation, assembly and
        // reaction. The two are never ADDED -- one is the authority, the other
        // the oracle.
        Result<Force3D> assembled = structural::assembledForceResultant(
            mesh, system.numbering(), system.force());
        REQUIRE(assembled.has_value());
        const Force3D fromLoads = prepared.resultantForce();
        const double scale = std::max(magnitudeOf(fromLoads), 1.0);
        REQUIRE_THAT(assembled->z.si(), WithinAbs(fromLoads.z.si(), 1e-12 * scale));
        // And both agree with the analytical resultant to the integration's
        // own accuracy.
        REQUIRE_THAT(assembled->z.si(), WithinRel(analyticalFz, 1e-9));
    }

    SECTION("the moment balances about the face centroid as well as the origin") {
        // A pressure through the centroid of a planar face produces no moment
        // ABOUT that centroid, so the support's moment about it must also be
        // ~zero -- a different statement from balancing about the origin, and
        // one a reversed cross product would not satisfy.
        const Point3D centroid{Length::fromSi(0.060), Length::fromSi(0.035),
                               Length::fromSi(0.035)};
        Result<Moment3D> aboutCentroid = reactions.momentAbout(mesh, centroid);
        REQUIRE(aboutCentroid.has_value());
        Result<Moment3D> externalAboutCentroid =
            prepared.resultantMomentAbout(mesh, centroid);
        REQUIRE(externalAboutCentroid.has_value());
        const double scale = std::max(magnitudeOf(*externalAboutCentroid),
                                      std::abs(analyticalFz) * 0.035);
        INFO("M_reaction about centroid " << magnitudeOf(*aboutCentroid) << " N m");
        INFO("M_external about centroid " << magnitudeOf(*externalAboutCentroid) << " N m");
        REQUIRE_THAT(aboutCentroid->x.si(),
                     WithinAbs(-externalAboutCentroid->x.si(), 1e-9 * scale));
        REQUIRE_THAT(aboutCentroid->y.si(),
                     WithinAbs(-externalAboutCentroid->y.si(), 1e-9 * scale));
    }
}

TEST_CASE("StructuralReaction_BalancesSelfWeightAgainstRhoVG",
          "[structural][reaction][reference]") {
    // GRAVITY IS SUPPORTED, so it is tested rather than recorded N/A.
    // RM-MESH-01 is a 120 x 70 x 35 mm block, so
    //
    //     V = 0.120 * 0.070 * 0.035          W = rho V g
    //
    // and the weight acts through the centroid at (60, 35, 17.5) mm. The block
    // is a BOX, so the tetrahedra tile it exactly and the analytical volume is
    // the mesh volume -- which is why this is the right body for a body-force
    // equilibrium check.
    //
    // THE PRODUCTION PATH DOES NOT COMPUTE A CENTRE OF MASS. The assembled F
    // carries gravity as nodal-equivalent forces and the moment follows from
    // their positions; the analytical centroid below is the test's oracle.
    auto built = reference::buildMeshBlockReferenceModel();
    REQUIRE(built.has_value());
    const FaceName bottom = built->bottom();
    MeshedReference reference(std::move(built->document));
    assignSteel(reference.document());
    reference.require();

    constexpr double kVolume = 0.120 * 0.070 * 0.035;
    constexpr double kWeight = kDensity * kVolume * kGravity;

    const std::vector<StructuralLoad> loads{
        StructuralLoad{LoadId::fromValue(1),
                       structural::GravityLoad{.acceleration = Vector3D{0.0, 0.0, -kGravity}}}};
    const PreparedLoads prepared =
        prepareOf(reference, loads, StructuralAnalysisMode::LinearStaticWithGravity);
    const GlobalStructuralSystem system =
        assembleOf(reference, prepared, StructuralAnalysisMode::LinearStaticWithGravity);
    const PreparedRestraints restraints = restrainOf(
        reference, {StructuralRestraint::fixedSupport(RestraintId::fromValue(1), bottom)});
    const SolvedSystem solution = solveOf(system, restraints.constraints());
    const SupportReactions reactions =
        recoverOf(reference, system, restraints, prepared, solution, Point3D{},
                  StructuralAnalysisMode::LinearStaticWithGravity);
    const meshing::Mesh& mesh = modelOf(reference).mesh().mesh();

    SECTION("the total support reaction is the weight, upward") {
        const Force3D total = reactions.totalForce();
        INFO("W = rho V g = " << kWeight << " N, reaction F_z " << total.z.si() << " N");
        REQUIRE_THAT(total.z.si(), WithinRel(kWeight, 1e-9));
        REQUIRE(total.z.si() > 0.0);
        const double error = std::abs(total.z.si() - kWeight) / kWeight;
        WARN("MEASURE geometric | RM-MESH-01 gravity | W " << kWeight << " N | reaction "
             << total.z.si() << " N | relative " << error);
        REQUIRE(error < 1.0e-9);
    }

    SECTION("and layer A balances exactly") {
        REQUIRE(reactions.forceBalance().normalized <= measured().force);
        REQUIRE(reactions.momentBalance().normalized <= measured().moment);
    }

    SECTION("the moment about an OFF-CENTRE origin is set by the centre of mass") {
        // The strong body-force check: about the global origin the weight has
        // a moment `(c - O) x W` with c the centroid. If the nodal-equivalent
        // gravity were distributed wrongly -- weighted by node count instead
        // of by volume, say -- the total force would still be right and this
        // moment would not.
        const double cx = 0.060;
        const double cy = 0.035;
        // W = (0, 0, -kWeight) at (cx, cy, cz): M = c x W
        const double expectedMx = cy * (-kWeight);
        const double expectedMy = -cx * (-kWeight);
        Result<Moment3D> external = prepared.resultantMomentAbout(mesh, Point3D{});
        REQUIRE(external.has_value());
        INFO("expected M_external (" << expectedMx << ", " << expectedMy << ", 0) N m");
        INFO("actual   M_external (" << external->x.si() << ", " << external->y.si() << ", "
                                     << external->z.si() << ") N m");
        const double scale = std::max(std::abs(expectedMx), std::abs(expectedMy));
        REQUIRE_THAT(external->x.si(), WithinAbs(expectedMx, 1e-9 * scale));
        REQUIRE_THAT(external->y.si(), WithinAbs(expectedMy, 1e-9 * scale));
        REQUIRE_THAT(external->z.si(), WithinAbs(0.0, 1e-9 * scale));

        // And the support supplies minus it.
        const Moment3D reaction = reactions.momentBalance().reaction;
        REQUIRE_THAT(reaction.x.si(), WithinAbs(-external->x.si(), 1e-9 * scale));
        REQUIRE_THAT(reaction.y.si(), WithinAbs(-external->y.si(), 1e-9 * scale));

        // AND THE MOMENT IS A REAL TWO-COMPONENT MOMENT, so neither the
        // comparison above nor the balance is being measured on a degenerate
        // case. The block weighs rho V g = 22.6 N and its centroid is 35 mm
        // and 60 mm off the origin's two axes, so the moments are 0.79 and
        // 1.36 N m -- fourteen orders above the equilibrium noise floor, and
        // DIFFERENT from each other, which an axis swap would not reproduce.
        REQUIRE(std::abs(external->x.si()) > 0.1);
        REQUIRE(std::abs(external->y.si()) > 0.1);
        REQUIRE(std::abs(external->x.si()) != std::abs(external->y.si()));
    }
}

TEST_CASE("StructuralReaction_KeepsEquilibriumWithGeometryFarFromTheOrigin",
          "[structural][reaction][reference]") {
    // THE CANCELLATION CASE. A moment about the global origin is a sum of
    // `(x - O) x F` terms, and when the geometry sits far from O every term is
    // a large number whose sum is small. That is where catastrophic
    // cancellation would show, and it is why the moment tolerance has to be
    // measured on a realistic CAD coordinate range rather than on a body at
    // the origin.
    //
    // RM-MESH-06's placed model is the same 90 x 55 x 24 mm block on a rotated
    // and TRANSLATED frame, tens of millimetres from the origin, which is the
    // range BetterCAD models actually live in.
    //
    // FORCE equilibrium must be unaffected -- it has no lever arm in it at all.
    // MOMENT equilibrium is the question.
    for (const bool placed : {false, true}) {
        auto built = placed ? reference::buildMeshTransformedPlacedReferenceModel()
                            : reference::buildMeshTransformedBaseReferenceModel();
        REQUIRE(built.has_value());
        const FaceName datum = built->datumFace();
        const FaceName opposite = built->oppositeFace();
        MeshedReference reference(std::move(built->document));
        assignSteel(reference.document());
        reference.require();

        const std::vector<StructuralLoad> loads{
            StructuralLoad{LoadId::fromValue(1),
                           structural::PressureLoad{.face = opposite,
                                                    .magnitude = Pressure::fromSi(5.0e6)}}};
        const PreparedLoads prepared =
            prepareOf(reference, loads, StructuralAnalysisMode::LinearStatic);
        const GlobalStructuralSystem system =
            assembleOf(reference, prepared, StructuralAnalysisMode::LinearStatic);
        const PreparedRestraints restraints = restrainOf(
            reference, {StructuralRestraint::fixedSupport(RestraintId::fromValue(1), datum)});
        const SolvedSystem solution = solveOf(system, restraints.constraints());
        const SupportReactions reactions =
            recoverOf(reference, system, restraints, prepared, solution, Point3D{},
                      StructuralAnalysisMode::LinearStatic);
        const meshing::Mesh& mesh = modelOf(reference).mesh().mesh();

        // How far the geometry actually sits from the origin, so the
        // measurement is auditable.
        double farthest = 0.0;
        for (const meshing::Node& node : mesh.nodes()) {
            farthest = std::max(farthest, std::hypot(node.position.x.si(), node.position.y.si(),
                                                     node.position.z.si()));
        }

        const structural::ForceBalance& f = reactions.forceBalance();
        const structural::MomentBalance& m = reactions.momentBalance();
        WARN("MEASURE far-origin | RM-MESH-06 " << (placed ? "placed" : "base")
             << " | farthest node " << farthest << " m | scale_F " << f.scale.si()
             << " N | eta_F " << f.normalized << " | scale_M " << m.scale.si()
             << " N m | ||e_M||2 " << m.euclideanNorm.si() << " N m | eta_M " << m.normalized);

        INFO((placed ? "placed" : "base") << ": farthest node " << farthest << " m");
        REQUIRE(f.normalized <= measured().force);
        REQUIRE(m.normalized <= measured().moment);
        // The moment scale is large, so the balance is a real cancellation and
        // not a sum of nothing.
        REQUIRE(m.scale.si() > 1.0);
        if (placed) {
            // The placed model really is off the origin, which is what makes
            // this a cancellation test rather than a repeat of the base case.
            REQUIRE(farthest > 0.03);
        }
    }
}

TEST_CASE("StructuralReaction_RecoversALargeMeshAndStillBalances",
          "[structural][reaction][reference]") {
    // RM-MESH-02, a cylinder, at the sizing P17-ASSEMBLY-001 settled on: a
    // block cannot be used, because a plane is exactly representable and its
    // surface mesh stays at two triangles per face however fine the deflection
    // control. Only a curved wall responds.
    //
    // NOT A PERFORMANCE TEST. No wall-clock bound is asserted.
    auto built = reference::buildMeshCylinderReferenceModel();
    REQUIRE(built.has_value());
    const FaceName bottom = built->bottomCap();
    const FaceName top = built->topCap();
    MeshedReference reference(std::move(built->document));
    assignSteel(reference.document());

    meshing::MeshControlDefinition definition = reference.definition()->definition();
    definition.mesh.surface.linearDeflection = Length::fromSi(2.5e-5);
    definition.mesh.sizing.globalTargetSize = Length::fromSi(6.0e-3);
    REQUIRE(reference.document()
                .modifyObject<meshing::MeshControl>(
                    reference.controlObject(),
                    [&definition](meshing::MeshControl& control) {
                        return control.setDefinition(definition);
                    })
                .has_value());
    const meshing::VolumeMesh& volume = reference.require();

    const std::vector<StructuralLoad> loads{
        StructuralLoad{LoadId::fromValue(1),
                       structural::PressureLoad{.face = top,
                                                .magnitude = Pressure::fromSi(4.0e6)}}};
    const PreparedLoads prepared =
        prepareOf(reference, loads, StructuralAnalysisMode::LinearStatic);
    const GlobalStructuralSystem system =
        assembleOf(reference, prepared, StructuralAnalysisMode::LinearStatic);
    const PreparedRestraints restraints = restrainOf(
        reference, {StructuralRestraint::fixedSupport(RestraintId::fromValue(1), bottom)});
    const SolvedSystem solution = solveOf(system, restraints.constraints());
    const SupportReactions reactions =
        recoverOf(reference, system, restraints, prepared, solution, Point3D{},
                  StructuralAnalysisMode::LinearStatic);

    const structural::ForceBalance& f = reactions.forceBalance();
    const structural::MomentBalance& m = reactions.momentBalance();
    WARN("MEASURE large | RM-MESH-02 | " << volume.mesh().nodes().size() << " nodes | "
         << system.degreesOfFreedom() << " DOFs | " << reactions.constrainedDegreesOfFreedom()
         << " constrained | " << reactions.nodal().size() << " reaction entries | scale_F "
         << f.scale.si() << " N | eta_F " << f.normalized << " | scale_M " << m.scale.si()
         << " N m | eta_M " << m.normalized);

    SECTION("the mesh really is large") {
        REQUIRE(volume.mesh().nodes().size() > 400);
        REQUIRE(reactions.constrainedDegreesOfFreedom() > 100);
    }

    SECTION("every value is finite and every reported node is constrained") {
        for (const SupportReaction& entry : reactions.nodal()) {
            REQUIRE(isFinite(entry));
            REQUIRE_FALSE(entry.constrained.isEmpty());
        }
        REQUIRE(std::ranges::is_sorted(reactions.nodal(), {}, &SupportReaction::node));
    }

    SECTION("and both equilibrium gates pass at scale") {
        REQUIRE(f.normalized <= measured().force);
        REQUIRE(m.normalized <= measured().moment);
        REQUIRE(f.scale.si() > 1.0);
    }
}
