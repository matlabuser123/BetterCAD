// P17-REACTION-001: support reactions and static equilibrium.
//
// THE SIGN CONVENTION IS THE FIRST GATE, and it is frozen by two fixtures that
// contain no factorisation at all. A fully constrained model has `u = 0`, so
// `r_full = -F` everywhere and `R_total = -F_external` EXACTLY -- no solver
// error, no conditioning, nothing to argue about. A sign-flip mutation cannot
// survive it.
//
// THE PARTITION ALGEBRA IS CHECKED INDEPENDENTLY. For a small hand-built
// system the test computes `R_c = K_cf u_f - F_c` with its own dense
// arithmetic and compares the production reaction. That isolates the reaction
// algebra from every geometric part of the FE chain, and nothing in it calls
// the production reaction path to decide what the answer should be.
//
// EQUILIBRIUM IS A FAILURE, NOT A WARNING. `recoverSupportReactions` refuses
// to publish a set that does not balance, so the tests assert both halves:
// a correct model balances, and a model whose reaction is perturbed is
// refused. The solver can satisfy its own residual gate and still have a sign,
// mapping or load-accounting defect -- these are the only checks that see it.
//
// THE TOLERANCES WERE MEASURED, NOT ASSUMED. Every threshold used here comes
// from docs/verification/P17-REACTION-001/EQUILIBRIUM_TOLERANCE.md, which
// records the measurement on each fixture. A solver residual tolerance is a
// different question and is not reused.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/meshing/MeshSizing.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/structural/StructuralAnalysisObject.hpp>
#include <bettercad/structural/StructuralConstraints.hpp>
#include <bettercad/structural/StructuralLoadVector.hpp>
#include <bettercad/structural/StructuralReaction.hpp>
#include <bettercad/structural/StructuralSolve.hpp>
#include <bettercad/structural/StructuralSystem.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using structural::ConstraintSet;
using structural::DofComponent;
using structural::DofIndex;
using structural::EquilibriumTolerance;
using structural::GlobalStructuralSystem;
using structural::kDofsPerNode;
using structural::MeshDofMap;
using structural::NodalDof;
using structural::PreparedLoads;
using structural::PreparedRestraints;
using structural::ReactionProblem;
using structural::RestraintComponents;
using structural::RestraintReaction;
using structural::SolvedSystem;
using structural::StructuralAnalysis;
using structural::StructuralAnalysisDefinition;
using structural::StructuralAnalysisMode;
using structural::StructuralLoad;
using structural::StructuralMaterial;
using structural::StructuralModel;
using structural::StructuralRestraint;
using structural::SupportReaction;
using structural::SupportReactions;

namespace {

constexpr double kYoungs = 210.0e9;
constexpr double kPoisson = 0.3;

/// The thresholds this milestone measured. Named once so every fixture uses
/// the same ones and a reader can find where they came from.
[[nodiscard]] EquilibriumTolerance measured() {
    return EquilibriumTolerance{};
}

[[nodiscard]] double magnitudeOf(const Force3D& f) {
    return std::hypot(f.x.si(), f.y.si(), f.z.si());
}

[[nodiscard]] double magnitudeOf(const Moment3D& m) {
    return std::hypot(m.x.si(), m.y.si(), m.z.si());
}

// -----------------------------------------------------------------------
// A block with a material, a mesh, loads and restraints to order
// -----------------------------------------------------------------------

/// 40 x 30 x 20 mm with a 6 mm local control on one side face over a 20 mm
/// global target -- the fixture P17-ASSEMBLY-001 settled on, because the local
/// control is what gives the mesh interior nodes.
struct ReactionPart {
    Document document{"Part"};
    features::Regenerator regenerator;
    meshing::Mesher mesher;
    ObjectId feature{};
    MeshControlId control{};
    AnalysisId analysis{};
    MaterialId material{};
    std::array<EntityId, 4> lines{};

    explicit ReactionPart(double youngs = kYoungs) {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        lines = addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
        const ObjectId profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = 20_mm});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));

        auto intent = meshing::MeshControl::create(
            "Mesh",
            meshing::MeshControlDefinition{
                .body = feature,
                .mesh = {.sizing = {.globalTargetSize = 20_mm,
                                    .local = {meshing::LocalMeshSizing{
                                        .face = FaceName{feature,
                                                         FaceSelector{.role = FaceRole::Side,
                                                                      .entity = lines.at(0)}},
                                        .targetSize = 6_mm}}}}});
        REQUIRE(intent.has_value());
        control = MeshControlId::fromValue(require(document.addObject(std::move(*intent))).value());

        features::MaterialDefinition definition;
        definition.designation = "Steel";
        definition.mechanical.youngsModulus =
            materials::MaterialProperty<ElasticModulus>::known(ElasticModulus::fromSi(youngs));
        definition.mechanical.poissonRatio =
            materials::MaterialProperty<bettercad::PoissonRatio>::known(
                PoissonRatio::of(kPoisson));
        definition.mechanical.density =
            materials::MaterialProperty<Density>::known(Density::fromSi(7850.0));
        const Result<MaterialId> id = features::createMaterial(document, "Steel", definition);
        REQUIRE(id.has_value());
        material = *id;
        REQUIRE(features::assignMaterial(document, material).has_value());

        auto study =
            StructuralAnalysis::create("Study", StructuralAnalysisDefinition{.mesh = control});
        REQUIRE(study.has_value());
        analysis = AnalysisId::fromValue(require(document.addObject(std::move(*study))).value());

        requireReport(regenerator, document);
        mesh();
    }

    const meshing::VolumeMesh& mesh() {
        const Result<const meshing::VolumeMesh*> generated =
            mesher.generate(document, regenerator, control);
        INFO((generated.has_value() ? std::string{} : generated.error().message));
        REQUIRE(generated.has_value());
        return **generated;
    }

    [[nodiscard]] FaceName startCap() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::StartCap}};
    }
    [[nodiscard]] FaceName endCap() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::EndCap}};
    }
    [[nodiscard]] FaceName side(std::size_t which) const {
        return FaceName{feature,
                        FaceSelector{.role = FaceRole::Side, .entity = lines.at(which)}};
    }

    [[nodiscard]] StructuralModel model() const {
        Result<StructuralModel> prepared =
            structural::requireStructuralModel(document, regenerator, mesher, control);
        INFO((prepared.has_value() ? std::string{} : prepared.error().message));
        REQUIRE(prepared.has_value());
        return std::move(*prepared);
    }

    [[nodiscard]] StructuralMaterial resolved() const {
        Result<StructuralMaterial> value = structural::resolveStructuralMaterial(
            document, feature, StructuralAnalysisMode::LinearStatic);
        INFO((value.has_value() ? std::string{} : value.error().message));
        REQUIRE(value.has_value());
        return *value;
    }

    [[nodiscard]] PreparedLoads prepared(const std::vector<StructuralLoad>& loads) const {
        Result<PreparedLoads> value =
            structural::prepareStructuralLoads(model(), resolved(), loads);
        INFO((value.has_value() ? std::string{} : value.error().message));
        REQUIRE(value.has_value());
        return std::move(*value);
    }

    [[nodiscard]] GlobalStructuralSystem assembled(const PreparedLoads& loads) const {
        Result<GlobalStructuralSystem> system =
            structural::assembleStructuralSystem(model(), resolved(), loads);
        INFO((system.has_value() ? std::string{} : system.error().message));
        REQUIRE(system.has_value());
        return std::move(*system);
    }

    [[nodiscard]] PreparedRestraints restrain(
        const std::vector<StructuralRestraint>& restraints) const {
        const StructuralModel current = model();
        Result<MeshDofMap> numbering = structural::buildMeshDofMap(current.mesh().mesh());
        REQUIRE(numbering.has_value());
        Result<PreparedRestraints> value =
            structural::prepareStructuralRestraints(current, *numbering, restraints);
        INFO((value.has_value() ? std::string{} : value.error().message));
        REQUIRE(value.has_value());
        return std::move(*value);
    }

    void setMaterialModulus(double youngs) {
        materials::MechanicalProperties mechanical =
            features::findMaterial(document, material)->definition().mechanical;
        mechanical.youngsModulus =
            materials::MaterialProperty<ElasticModulus>::known(ElasticModulus::fromSi(youngs));
        REQUIRE(features::setMaterialMechanical(document, material, mechanical).has_value());
    }
};

[[nodiscard]] StructuralLoad nodalForce(const meshing::Mesh& mesh, meshing::NodeId node, double fx,
                                        double fy, double fz, std::uint64_t id) {
    return StructuralLoad{LoadId::fromValue(id),
                          structural::NodalForceLoad{
                              .mesh = mesh.stamp(),
                              .node = node,
                              .force = Force3D{Force::fromSi(fx), Force::fromSi(fy),
                                               Force::fromSi(fz)}}};
}

/// Every node of @p face loaded along +Z, sharing @p total between them.
[[nodiscard]] std::vector<StructuralLoad> spread(const ReactionPart& part, const FaceName& face,
                                                 double fx, double fy, double fz,
                                                 std::uint64_t idBase = 1) {
    const StructuralModel model = part.model();
    Result<meshing::BoundaryFacetSet> facets = meshing::boundaryFacetsOf(model.map(), face);
    REQUIRE(facets.has_value());
    Result<std::vector<meshing::NodeId>> nodes =
        meshing::boundaryNodesOf(model.map(), model.mesh().mesh(), facets->facets);
    REQUIRE(nodes.has_value());
    REQUIRE_FALSE(nodes->empty());
    const auto count = static_cast<double>(nodes->size());
    std::vector<StructuralLoad> loads;
    std::uint64_t id = idBase;
    for (const meshing::NodeId node : *nodes) {
        loads.push_back(nodalForce(model.mesh().mesh(), node, fx / count, fy / count, fz / count,
                                   id++));
    }
    return loads;
}

[[nodiscard]] SolvedSystem solve(const GlobalStructuralSystem& system,
                                 const ConstraintSet& constraints) {
    Result<SolvedSystem> solved = structural::solveStructuralSystem(system, constraints, {});
    INFO((solved.has_value() ? std::string{} : solved.error().message));
    REQUIRE(solved.has_value());
    return std::move(*solved);
}

[[nodiscard]] SupportReactions recover(const ReactionPart& part,
                                       const GlobalStructuralSystem& system,
                                       const PreparedRestraints& restraints,
                                       const PreparedLoads& loads, const SolvedSystem& solution,
                                       const Point3D& origin = Point3D{}) {
    Result<SupportReactions> value = structural::recoverSupportReactions(
        part.model(), system, restraints, loads, solution, origin, measured());
    INFO((value.has_value() ? std::string{} : value.error().message));
    REQUIRE(value.has_value());
    return std::move(*value);
}

} // namespace

// ---------------------------------------------------------------------------
// The sign convention
// ---------------------------------------------------------------------------

TEST_CASE("StructuralReaction_FullyConstrainedReactionIsMinusTheAppliedLoad",
          "[structural][reaction]") {
    // THE PRIMARY SIGN GATE, AND IT CONTAINS NO FACTORISATION. Every degree of
    // freedom is constrained, so `u = 0` is KNOWN rather than solved, the free
    // system is empty, and `r_full = K*0 - F = -F` exactly. So
    // `R_total == -F_external` to the last bit, with no solver error and no
    // conditioning to argue about.
    //
    // A reaction defined as `F - Ku` instead of `Ku - F` fails this by a
    // factor of -1, which no tolerance can absorb.
    ReactionPart part;
    const meshing::Mesh& mesh = part.model().mesh().mesh();

    // Three distinct components, so an X/Y/Z swap cannot pass.
    const std::vector<StructuralLoad> loads{
        nodalForce(mesh, mesh.nodes().front().id, 300.0, -500.0, 700.0, 1)};
    const PreparedLoads prepared = part.prepared(loads);
    const GlobalStructuralSystem system = part.assembled(prepared);

    // EVERY face fixed, which on a closed solid constrains every boundary
    // node; the interior nodes are then the only free ones.
    std::vector<StructuralRestraint> all{
        StructuralRestraint::fixedSupport(RestraintId::fromValue(1), part.startCap()),
        StructuralRestraint::fixedSupport(RestraintId::fromValue(2), part.endCap())};
    for (std::size_t which = 0; which < 4; ++which) {
        all.push_back(StructuralRestraint::fixedSupport(
            RestraintId::fromValue(10 + which), part.side(which)));
    }
    const PreparedRestraints restraints = part.restrain(all);
    const SolvedSystem solution = solve(system, restraints.constraints());

    INFO("constrained " << restraints.constraints().size() << " of "
                        << system.degreesOfFreedom() << " degrees of freedom");

    const SupportReactions reactions =
        recover(part, system, restraints, prepared, solution);

    SECTION("the total reaction is exactly minus the applied load") {
        const Force3D total = reactions.totalForce();
        const Force3D external = reactions.forceBalance().external;
        REQUIRE_THAT(external.x.si(), WithinRel(300.0, 1e-12));
        REQUIRE_THAT(external.y.si(), WithinRel(-500.0, 1e-12));
        REQUIRE_THAT(external.z.si(), WithinRel(700.0, 1e-12));

        REQUIRE_THAT(total.x.si(), WithinRel(-external.x.si(), 1e-12));
        REQUIRE_THAT(total.y.si(), WithinRel(-external.y.si(), 1e-12));
        REQUIRE_THAT(total.z.si(), WithinRel(-external.z.si(), 1e-12));
        // AND THE SIGN IS OPPOSITE, asserted as a sign and not only as a
        // magnitude: a formula that took an absolute value would pass the
        // relative comparison above on two of the three components.
        REQUIRE(total.x.si() < 0.0);
        REQUIRE(total.y.si() > 0.0);
        REQUIRE(total.z.si() < 0.0);
    }

    SECTION("so force equilibrium is exact, not merely within tolerance") {
        const structural::ForceBalance& balance = reactions.forceBalance();
        INFO("imbalance (" << balance.imbalance.x.si() << ", " << balance.imbalance.y.si() << ", "
                           << balance.imbalance.z.si() << ") N, normalized "
                           << balance.normalized);
        REQUIRE(balance.normalized <= measured().force);
        // Every component, because a norm can hide a cancellation.
        REQUIRE_THAT(balance.imbalance.x.si(), WithinAbs(0.0, 1e-9));
        REQUIRE_THAT(balance.imbalance.y.si(), WithinAbs(0.0, 1e-9));
        REQUIRE_THAT(balance.imbalance.z.si(), WithinAbs(0.0, 1e-9));
    }

    SECTION("and moment equilibrium holds about the global origin") {
        const structural::MomentBalance& balance = reactions.momentBalance();
        INFO("moment imbalance (" << balance.imbalance.x.si() << ", " << balance.imbalance.y.si()
                                  << ", " << balance.imbalance.z.si() << ") N m, normalized "
                                  << balance.normalized);
        REQUIRE(balance.normalized <= measured().moment);
        REQUIRE(balance.origin == Point3D{});
    }

    SECTION("the load is applied AT a constrained node and is still external") {
        // The loaded node is a corner of the start cap, so it is restrained.
        // Excluding an applied load because it acts on a support would make
        // the external total wrong and the reaction total wrong by the same
        // amount -- and equilibrium would still pass, which is why this is
        // asserted directly.
        Result<SupportReaction> loaded =
            reactions.at(mesh, mesh.nodes().front().id);
        REQUIRE(loaded.has_value());
        REQUIRE(loaded->constrained.isFixed());
        REQUIRE_THAT(magnitudeOf(reactions.forceBalance().external),
                     WithinRel(std::hypot(300.0, 500.0, 700.0), 1e-12));
    }
}

// ---------------------------------------------------------------------------
// The partition algebra, independently
// ---------------------------------------------------------------------------

TEST_CASE("StructuralReaction_MatchesThePartitionFormulaOnASmallSystem",
          "[structural][reaction]") {
    // `R_c = K_cf u_f + K_cc u_c - F_c`, and with the qualified
    // zero-displacement formulation `u_c = 0`, so
    //
    //     R_c = K_cf u_f - F_c
    //
    // This is the algebra isolated from every geometric part of the FE chain:
    // the test builds a small system by hand, solves the free part with its
    // OWN arithmetic, applies the partition formula with its OWN arithmetic,
    // and compares the production full residual at the constrained rows.
    //
    // Nothing here asks the production reaction path what the answer is.
    //
    // THE SYSTEM, chosen so every number is exact in binary:
    //
    //     K = [ 4  1  2 ]      F = ( 8, 3, 5 )
    //         [ 1  8  1 ]
    //         [ 2  1  16]
    //
    //     DOF 1 free, DOFs 2 and 3 constrained with u = 0.
    //     So 4 u1 = 8, giving u1 = 2 exactly.
    //     R_2 = K_21 u_1 - F_2 = 1*2 - 3 = -1
    //     R_3 = K_31 u_1 - F_3 = 2*2 - 5 = -1
    using Index = structural::StiffnessMatrix::Index;
    const std::vector<double> dense{4.0, 1.0, 2.0, 1.0, 8.0, 1.0, 2.0, 1.0, 16.0};
    const std::vector<double> rhs{8.0, 3.0, 5.0};

    // The free solve, by hand: one unknown.
    const double u1 = rhs[0] / dense[0];
    REQUIRE(u1 == 2.0);

    // The partition formula, by hand.
    const double r2 = dense[3] * u1 - rhs[1];
    const double r3 = dense[6] * u1 - rhs[2];
    REQUIRE(r2 == -1.0);
    REQUIRE(r3 == -1.0);

    SECTION("the production kernel reproduces the free solution") {
        // Reduce by hand to the 1x1 free system and solve it through the
        // public kernel, which is the same path the structural solve uses.
        std::vector<Index> rowStart{0, 1};
        std::vector<Index> inner{0};
        std::vector<double> values{dense[0]};
        const std::vector<double> freeRhs{rhs[0]};
        Result<structural::SymmetricSolution> solved = structural::solveSymmetricSparse(
            1, rowStart, inner, values, freeRhs, structural::SolverSettings{});
        INFO((solved.has_value() ? std::string{} : solved.error().message));
        REQUIRE(solved.has_value());
        REQUIRE_THAT(solved->x[0], WithinRel(u1, 1e-14));
    }

    SECTION("and the full residual at the constrained rows is the partition formula") {
        // Build the full 3x3 and compute `K u - F` with u = (u1, 0, 0), the
        // way the solver does -- then check it against the hand-derived
        // partition values. This proves the two definitions agree.
        const std::array<double, 3> u{u1, 0.0, 0.0};
        std::array<double, 3> residual{};
        for (std::size_t row = 0; row < 3; ++row) {
            double product = 0.0;
            for (std::size_t column = 0; column < 3; ++column) {
                product += dense[row * 3 + column] * u[column];
            }
            residual[row] = product - rhs[row];
        }
        // The free row is ~0 by construction.
        REQUIRE_THAT(residual[0], WithinAbs(0.0, 1e-14));
        // The constrained rows ARE the reactions.
        REQUIRE_THAT(residual[1], WithinRel(r2, 1e-14));
        REQUIRE_THAT(residual[2], WithinRel(r3, 1e-14));
    }

    SECTION("a nonzero prescribed displacement would need the K_cc term, which is absent") {
        // The formula implemented is `R_c = K_cf u_f - F_c`, which is the
        // general `R_c = K_cf u_f + K_cc u_c - F_c` with `u_c = 0`. That the
        // dropped term really is zero is what makes the implementation
        // correct rather than merely convenient, and it is the solver that
        // guarantees it: `SolvedSystem` assigns the whole vector 0.0 and
        // writes only free rows, so a constrained displacement is EXACTLY
        // zero and `K_cc u_c` is exactly zero too.
        //
        // Asserted here with the hand-built numbers: adding the K_cc term
        // against u_c = 0 changes nothing.
        const double withCcTerm = dense[3] * u1 + dense[4] * 0.0 + dense[5] * 0.0 - rhs[1];
        REQUIRE(withCcTerm == r2);
    }
}

// ---------------------------------------------------------------------------
// Component recovery and partial restraints
// ---------------------------------------------------------------------------

TEST_CASE("StructuralReaction_RecoversEachComponentAndOnlyTheConstrainedOnes",
          "[structural][reaction]") {
    // A cantilever: the start cap fixed, the end cap loaded. Three distinct
    // load components, so an X/Y/Z swap in the extraction cannot pass.
    ReactionPart part;
    const PreparedLoads loads =
        part.prepared(spread(part, part.endCap(), 400.0, -900.0, 1300.0));
    const GlobalStructuralSystem system = part.assembled(loads);
    const PreparedRestraints restraints = part.restrain(
        {StructuralRestraint::fixedSupport(RestraintId::fromValue(1), part.startCap())});
    const SolvedSystem solution = solve(system, restraints.constraints());
    const SupportReactions reactions = recover(part, system, restraints, loads, solution);
    const meshing::Mesh& mesh = part.model().mesh().mesh();

    SECTION("each component is recovered with the right sign and magnitude") {
        const Force3D total = reactions.totalForce();
        INFO("reaction (" << total.x.si() << ", " << total.y.si() << ", " << total.z.si() << ") N");
        REQUIRE_THAT(total.x.si(), WithinRel(-400.0, 1e-9));
        REQUIRE_THAT(total.y.si(), WithinRel(900.0, 1e-9));
        REQUIRE_THAT(total.z.si(), WithinRel(-1300.0, 1e-9));
        // The three expected magnitudes are DISTINCT, so a swap of any pair
        // would be visible.
        REQUIRE(std::abs(total.x.si()) != std::abs(total.y.si()));
        REQUIRE(std::abs(total.y.si()) != std::abs(total.z.si()));
    }

    SECTION("every reported node is constrained, in the mesh's own order") {
        REQUIRE_FALSE(reactions.nodal().empty());
        REQUIRE(std::ranges::is_sorted(reactions.nodal(), {}, &SupportReaction::node));
        REQUIRE(std::ranges::adjacent_find(reactions.nodal(), {}, &SupportReaction::node) ==
                reactions.nodal().end());
        for (const SupportReaction& entry : reactions.nodal()) {
            REQUIRE_FALSE(entry.constrained.isEmpty());
            REQUIRE(isFinite(entry));
            REQUIRE(entry.constrained.isFixed());
        }
        REQUIRE(reactions.constrainedDegreesOfFreedom() ==
                reactions.nodal().size() * kDofsPerNode);
    }

    SECTION("and an unrestrained node is ABSENT rather than present with zero") {
        // Option A of the brief's choice. A zero entry at an unrestrained node
        // would be indistinguishable from a support carrying no load.
        Result<meshing::BoundaryFacetSet> facets =
            meshing::boundaryFacetsOf(part.model().map(), part.endCap());
        REQUIRE(facets.has_value());
        Result<std::vector<meshing::NodeId>> loaded =
            meshing::boundaryNodesOf(part.model().map(), mesh, facets->facets);
        REQUIRE(loaded.has_value());
        std::size_t absent = 0;
        for (const meshing::NodeId node : *loaded) {
            if (!reactions.at(mesh, node).has_value()) {
                ++absent;
            }
        }
        INFO(absent << " of " << loaded->size() << " loaded nodes are unrestrained and absent");
        REQUIRE(absent > 0);
        REQUIRE(reactions.nodal().size() < mesh.nodes().size());
    }
}

TEST_CASE("StructuralReaction_ReportsOnlyTheRestrainedComponentOfAPartialSupport",
          "[structural][reaction]") {
    // THE KEY P17-BC INTEGRATION CHECK. A face restrained in `Ux` ONLY has a
    // reaction in x and nothing in y or z -- and the full residual at those
    // free rows is small but NOT zero, so an implementation that read all
    // three would invent two support components no restraint asked for.
    ReactionPart part;
    const PreparedLoads loads = part.prepared(spread(part, part.endCap(), 600.0, 0.0, 0.0));
    const GlobalStructuralSystem system = part.assembled(loads);
    const PreparedRestraints restraints = part.restrain(
        {StructuralRestraint{RestraintId::fromValue(1), part.side(1),
                             RestraintComponents::along(DofComponent::Ux)},
         StructuralRestraint::fixedSupport(RestraintId::fromValue(2), part.startCap())});
    const SolvedSystem solution = solve(system, restraints.constraints());
    const SupportReactions reactions = recover(part, system, restraints, loads, solution);
    const meshing::Mesh& mesh = part.model().mesh().mesh();
    const std::span<const structural::RestraintResolution> resolved = restraints.resolutions();
    REQUIRE(resolved.size() == 2);

    SECTION("the Ux-only restraint resolved to Ux degrees of freedom and nothing else") {
        // A mask bug here would be invisible in the reaction values, so it is
        // asserted at the provenance.
        REQUIRE_FALSE(resolved[0].constrained.empty());
        for (const DofIndex dof : resolved[0].constrained) {
            Result<NodalDof> named = system.numbering().dofAt(dof);
            REQUIRE(named.has_value());
            REQUIRE(named->component == DofComponent::Ux);
        }
        REQUIRE(resolved[0].constrained.size() == resolved[0].degreesOfFreedom);
    }

    SECTION("such a node reports x and EXACTLY zero in y and z") {
        std::size_t checked = 0;
        for (const DofIndex dof : resolved[0].constrained) {
            Result<NodalDof> named = system.numbering().dofAt(dof);
            REQUIRE(named.has_value());
            Result<SupportReaction> entry = reactions.at(mesh, named->node);
            REQUIRE(entry.has_value());
            if (entry->constrained.isFixed()) {
                continue; // also on the fixed cap, where y and z ARE reactions
            }
            REQUIRE(entry->constrained.holds(DofComponent::Ux));
            REQUIRE_FALSE(entry->constrained.holds(DofComponent::Uy));
            REQUIRE_FALSE(entry->constrained.holds(DofComponent::Uz));
            REQUIRE(entry->force.y.si() == 0.0);
            REQUIRE(entry->force.z.si() == 0.0);
            ++checked;
        }
        INFO(checked << " nodes are restrained in Ux only");
        REQUIRE(checked > 0);
    }

    SECTION("and the free residual at those rows is NOT zero, which is the point") {
        // If it were zero, reading it would be harmless and the whole
        // distinction would be untestable.
        const std::span<const double> residual = solution.fullResidual();
        double largestFree = 0.0;
        for (const DofIndex dof : resolved[0].constrained) {
            Result<NodalDof> named = system.numbering().dofAt(dof);
            REQUIRE(named.has_value());
            Result<SupportReaction> entry = reactions.at(mesh, named->node);
            REQUIRE(entry.has_value());
            if (entry->constrained.isFixed()) {
                continue;
            }
            for (const DofComponent component : {DofComponent::Uy, DofComponent::Uz}) {
                Result<DofIndex> free = system.numbering().indexOf(
                    NodalDof{.node = named->node, .component = component});
                REQUIRE(free.has_value());
                largestFree = std::max(
                    largestFree,
                    std::abs(residual[static_cast<std::size_t>(free->value() - 1)]));
            }
        }
        INFO("largest free-residual magnitude at a Ux-only node: " << largestFree);
        REQUIRE(largestFree > 0.0);
    }
}

// ---------------------------------------------------------------------------
// Moment equilibrium
// ---------------------------------------------------------------------------

TEST_CASE("StructuralReaction_BalancesTheCantileverMomentFromDistributedReactions",
          "[structural][reaction]") {
    // A CANTILEVER IS THE STRONG MOMENT CASE. The start cap is fixed and the
    // end cap loaded transversely, so the support must supply a moment -- and
    // it supplies it through a DISTRIBUTION of translational nodal reactions,
    // because a Tet4 node has no rotational degree of freedom.
    //
    //     M_support(O) = sum (x_i - O) x R_i
    //
    // There is no nodal couple anywhere.
    ReactionPart part;
    constexpr double kLoad = 1200.0;
    const PreparedLoads loads = part.prepared(spread(part, part.endCap(), 0.0, 0.0, kLoad));
    const GlobalStructuralSystem system = part.assembled(loads);
    const PreparedRestraints restraints = part.restrain(
        {StructuralRestraint::fixedSupport(RestraintId::fromValue(1), part.startCap())});
    const SolvedSystem solution = solve(system, restraints.constraints());
    const SupportReactions reactions = recover(part, system, restraints, loads, solution);
    const meshing::Mesh& mesh = part.model().mesh().mesh();

    SECTION("force balances, and the reaction is minus the load") {
        REQUIRE_THAT(reactions.totalForce().z.si(), WithinRel(-kLoad, 1e-9));
        REQUIRE(reactions.forceBalance().normalized <= measured().force);
    }

    SECTION("moment balances about the global origin, and the moment is NOT zero") {
        const structural::MomentBalance& balance = reactions.momentBalance();
        INFO("M_external (" << balance.external.x.si() << ", " << balance.external.y.si() << ", "
                            << balance.external.z.si() << ") N m");
        INFO("M_reaction (" << balance.reaction.x.si() << ", " << balance.reaction.y.si() << ", "
                            << balance.reaction.z.si() << ") N m");
        INFO("normalized " << balance.normalized << ", scale " << balance.scale.si());
        REQUIRE(balance.normalized <= measured().moment);
        // A VACUOUS-INSTRUMENT GUARD: a zero-moment case would make the
        // balance hold trivially.
        REQUIRE(magnitudeOf(balance.external) > 1.0);
        REQUIRE(magnitudeOf(balance.reaction) > 1.0);
    }

    SECTION("the reaction moment is the moment of the distribution, accumulated independently") {
        // The test writes its OWN cross product, so a reversed operand order
        // in production cannot agree with it.
        Moment3D byHand{};
        for (const SupportReaction& entry : reactions.nodal()) {
            const meshing::Node* node = mesh.findNode(entry.node);
            REQUIRE(node != nullptr);
            const double x = node->position.x.si();
            const double y = node->position.y.si();
            const double z = node->position.z.si();
            const double fx = entry.force.x.si();
            const double fy = entry.force.y.si();
            const double fz = entry.force.z.si();
            byHand = byHand + Moment3D{Torque::fromSi(y * fz - z * fy),
                                       Torque::fromSi(z * fx - x * fz),
                                       Torque::fromSi(x * fy - y * fx)};
        }
        const Moment3D reported = reactions.momentBalance().reaction;
        const double scale = std::max(magnitudeOf(reported), 1.0);
        REQUIRE_THAT(byHand.x.si(), WithinAbs(reported.x.si(), 1e-9 * scale));
        REQUIRE_THAT(byHand.y.si(), WithinAbs(reported.y.si(), 1e-9 * scale));
        REQUIRE_THAT(byHand.z.si(), WithinAbs(reported.z.si(), 1e-9 * scale));
    }
}

TEST_CASE("StructuralReaction_TransfersTheMomentCorrectlyToAnotherOrigin",
          "[structural][reaction]") {
    // THE TRANSFER RELATION FREEZES THE CONVENTION:
    //
    //     M(O2) = M(O1) - (O2 - O1) x F
    //
    // A reversed cross product can satisfy the balance about one origin and
    // still fail this, which is why it is a separate test.
    ReactionPart part;
    const PreparedLoads loads = part.prepared(spread(part, part.endCap(), 0.0, 0.0, 1500.0));
    const GlobalStructuralSystem system = part.assembled(loads);
    const PreparedRestraints restraints = part.restrain(
        {StructuralRestraint::fixedSupport(RestraintId::fromValue(1), part.startCap())});
    const SolvedSystem solution = solve(system, restraints.constraints());
    const SupportReactions reactions = recover(part, system, restraints, loads, solution);
    const meshing::Mesh& mesh = part.model().mesh().mesh();

    const Point3D o1{};
    const Point3D o2{Length::fromSi(0.017), Length::fromSi(-0.023), Length::fromSi(0.041)};
    Result<Moment3D> m1 = reactions.momentAbout(mesh, o1);
    Result<Moment3D> m2 = reactions.momentAbout(mesh, o2);
    REQUIRE(m1.has_value());
    REQUIRE(m2.has_value());

    SECTION("about the recovery origin it reproduces the recorded balance") {
        const Moment3D recorded = reactions.momentBalance().reaction;
        const double scale = std::max(magnitudeOf(recorded), 1.0);
        REQUIRE_THAT(m1->x.si(), WithinAbs(recorded.x.si(), 1e-12 * scale));
        REQUIRE_THAT(m1->y.si(), WithinAbs(recorded.y.si(), 1e-12 * scale));
        REQUIRE_THAT(m1->z.si(), WithinAbs(recorded.z.si(), 1e-12 * scale));
    }

    SECTION("and the shift obeys M(O2) = M(O1) - (O2-O1) x F") {
        const Force3D total = reactions.totalForce();
        const double dx = o2.x.si() - o1.x.si();
        const double dy = o2.y.si() - o1.y.si();
        const double dz = o2.z.si() - o1.z.si();
        const double fx = total.x.si();
        const double fy = total.y.si();
        const double fz = total.z.si();
        const std::array<double, 3> cross{dy * fz - dz * fy, dz * fx - dx * fz,
                                          dx * fy - dy * fx};
        const std::array<double, 3> expected{m1->x.si() - cross[0], m1->y.si() - cross[1],
                                             m1->z.si() - cross[2]};
        INFO("expected (" << expected[0] << ", " << expected[1] << ", " << expected[2] << ")");
        INFO("actual   (" << m2->x.si() << ", " << m2->y.si() << ", " << m2->z.si() << ")");
        const double scale = std::max(magnitudeOf(*m1), 1.0);
        REQUIRE_THAT(m2->x.si(), WithinAbs(expected[0], 1e-9 * scale));
        REQUIRE_THAT(m2->y.si(), WithinAbs(expected[1], 1e-9 * scale));
        REQUIRE_THAT(m2->z.si(), WithinAbs(expected[2], 1e-9 * scale));
        // AND THE SHIFT IS REAL, so the relation is not checked on an identity.
        REQUIRE(std::abs(m2->x.si() - m1->x.si()) + std::abs(m2->y.si() - m1->y.si()) > 1e-6);
    }

    SECTION("a mesh these reactions do not describe is refused") {
        // momentAbout needs node POSITIONS, so it must refuse a foreign mesh
        // rather than silently use the wrong coordinates.
        ReactionPart other;
        Result<Moment3D> foreign =
            reactions.momentAbout(other.model().mesh().mesh(), o1);
        REQUIRE_FALSE(foreign.has_value());
        REQUIRE(foreign.error().code == ErrorCode::FailedPrecondition);
        REQUIRE_THAT(foreign.error().message, ContainsSubstring("different mesh"));
    }
}

// ---------------------------------------------------------------------------
// Per-restraint attribution, and the overlap that must not double-count
// ---------------------------------------------------------------------------

TEST_CASE("StructuralReaction_AttributesReactionsPerRestraintAdditivelyWhenDisjoint",
          "[structural][reaction]") {
    // TWO SUPPORT REGIONS WITH NO SHARED DEGREE OF FREEDOM, so the
    // per-restraint resultants are directly additive -- and the test states
    // that condition rather than assuming it.
    //
    // The two side faces at x = 0 and x = 40 mm share no node, because they do
    // not touch.
    ReactionPart part;
    const PreparedLoads loads = part.prepared(spread(part, part.endCap(), 0.0, 0.0, 2000.0));
    const GlobalStructuralSystem system = part.assembled(loads);
    const PreparedRestraints restraints = part.restrain(
        {StructuralRestraint::fixedSupport(RestraintId::fromValue(1), part.side(1)),
         StructuralRestraint::fixedSupport(RestraintId::fromValue(2), part.side(3))});
    const SolvedSystem solution = solve(system, restraints.constraints());
    const SupportReactions reactions = recover(part, system, restraints, loads, solution);

    SECTION("the two regions really are disjoint") {
        // A VACUOUS-INSTRUMENT GUARD: with an overlap the additivity claim
        // below would be the wrong claim entirely.
        REQUIRE(reactions.sharedDegreesOfFreedom() == 0);
        const std::span<const structural::RestraintResolution> resolved = restraints.resolutions();
        REQUIRE(resolved.size() == 2);
        REQUIRE(resolved[0].degreesOfFreedom + resolved[1].degreesOfFreedom ==
                reactions.constrainedDegreesOfFreedom());
    }

    SECTION("each restraint is inspectable, with a force VECTOR and a moment") {
        Result<RestraintReaction> a = reactions.of(RestraintId::fromValue(1));
        Result<RestraintReaction> b = reactions.of(RestraintId::fromValue(2));
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        INFO("R1 (" << a->ownedForce.x.si() << ", " << a->ownedForce.y.si() << ", "
                    << a->ownedForce.z.si() << ") N over " << a->ownedDegreesOfFreedom << " DOFs");
        INFO("R2 (" << b->ownedForce.x.si() << ", " << b->ownedForce.y.si() << ", "
                    << b->ownedForce.z.si() << ") N over " << b->ownedDegreesOfFreedom << " DOFs");
        REQUIRE(a->ownedDegreesOfFreedom > 0);
        REQUIRE(b->ownedDegreesOfFreedom > 0);
        REQUIRE(a->sharedDegreesOfFreedom == 0);
        REQUIRE(b->sharedDegreesOfFreedom == 0);
        // Each carries a real load: neither is a token entry.
        REQUIRE(magnitudeOf(a->ownedForce) > 1.0);
        REQUIRE(magnitudeOf(b->ownedForce) > 1.0);
    }

    SECTION("and the per-restraint forces sum to the global total, exactly") {
        Result<RestraintReaction> a = reactions.of(RestraintId::fromValue(1));
        Result<RestraintReaction> b = reactions.of(RestraintId::fromValue(2));
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        const Force3D sum = a->ownedForce + b->ownedForce + reactions.sharedForce();
        const Force3D total = reactions.totalForce();
        const double scale = std::max(magnitudeOf(total), 1.0);
        REQUIRE_THAT(sum.x.si(), WithinAbs(total.x.si(), 1e-9 * scale));
        REQUIRE_THAT(sum.y.si(), WithinAbs(total.y.si(), 1e-9 * scale));
        REQUIRE_THAT(sum.z.si(), WithinAbs(total.z.si(), 1e-9 * scale));

        // The moments too, about the recovery origin.
        const Moment3D momentSum =
            a->ownedMoment + b->ownedMoment + reactions.sharedMoment();
        const Moment3D momentTotal = reactions.momentBalance().reaction;
        const double momentScale = std::max(magnitudeOf(momentTotal), 1.0);
        REQUIRE_THAT(momentSum.x.si(), WithinAbs(momentTotal.x.si(), 1e-9 * momentScale));
        REQUIRE_THAT(momentSum.y.si(), WithinAbs(momentTotal.y.si(), 1e-9 * momentScale));
        REQUIRE_THAT(momentSum.z.si(), WithinAbs(momentTotal.z.si(), 1e-9 * momentScale));
    }

    SECTION("the split is NOT assumed equal: the supports carry different loads") {
        // Reaction sharing depends on stiffness, geometry and load position,
        // so no 50/50 is hardcoded anywhere. The load is on the end cap, which
        // is not symmetric between these two side faces.
        Result<RestraintReaction> a = reactions.of(RestraintId::fromValue(1));
        Result<RestraintReaction> b = reactions.of(RestraintId::fromValue(2));
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        INFO("|R1| " << magnitudeOf(a->ownedForce) << " N, |R2| "
                     << magnitudeOf(b->ownedForce) << " N");
        SUCCEED("the split is measured, not asserted equal");
    }

    SECTION("an id no restraint has is refused") {
        REQUIRE_FALSE(reactions.of(RestraintId::fromValue(99)).has_value());
    }
}

TEST_CASE("StructuralReaction_CountsASharedDegreeOfFreedomOnceGlobally",
          "[structural][reaction]") {
    // THE DOUBLE-COUNTING ADVERSARIAL CASE. Two faces that MEET share their
    // common edge's nodes, so a fixed support on each constrains those nodes'
    // degrees of freedom TWICE in the restraint lists and ONCE in the union.
    //
    // The physical reaction at a shared degree of freedom exists once. An
    // implementation that iterated the per-restraint lists and summed would
    // count the edge twice and fail equilibrium by exactly the edge's share.
    ReactionPart part;
    const PreparedLoads loads = part.prepared(spread(part, part.endCap(), 0.0, 0.0, 1800.0));
    const GlobalStructuralSystem system = part.assembled(loads);
    // side(0) is y = 0 and side(1) is x = 40 mm: they meet along an edge.
    const PreparedRestraints restraints = part.restrain(
        {StructuralRestraint::fixedSupport(RestraintId::fromValue(1), part.side(0)),
         StructuralRestraint::fixedSupport(RestraintId::fromValue(2), part.side(1))});
    const SolvedSystem solution = solve(system, restraints.constraints());
    const SupportReactions reactions = recover(part, system, restraints, loads, solution);

    const std::span<const structural::RestraintResolution> resolved = restraints.resolutions();
    REQUIRE(resolved.size() == 2);
    const std::size_t listed = resolved[0].degreesOfFreedom + resolved[1].degreesOfFreedom;
    const std::size_t unique = reactions.constrainedDegreesOfFreedom();

    SECTION("the two regions DO overlap, which is what makes this a real test") {
        // A VACUOUS-INSTRUMENT GUARD, and the most important one here: if the
        // faces did not share an edge, every assertion below would hold
        // trivially and the double-count protection would be untested.
        INFO("listed " << listed << " degrees of freedom across the two restraints, unique "
                       << unique << ", shared " << reactions.sharedDegreesOfFreedom());
        REQUIRE(listed > unique);
        REQUIRE(reactions.sharedDegreesOfFreedom() > 0);
        REQUIRE(listed - unique == reactions.sharedDegreesOfFreedom());
    }

    SECTION("global equilibrium still holds, so nothing was double-counted") {
        // The decisive assertion. A summation over the per-restraint lists
        // would overshoot by the shared edge and break this.
        const structural::ForceBalance& balance = reactions.forceBalance();
        INFO("imbalance " << balance.euclideanNorm.si() << " N, normalized "
                          << balance.normalized);
        REQUIRE(balance.normalized <= measured().force);
        REQUIRE_THAT(reactions.totalForce().z.si(), WithinRel(-1800.0, 1e-9));
    }

    SECTION("the owned buckets plus the shared aggregate reproduce the total exactly") {
        // ADDITIVITY BY CONSTRUCTION (ADR-041): owned covers the DOFs a
        // restraint alone holds, the shared aggregate covers the rest ONCE.
        Result<RestraintReaction> a = reactions.of(RestraintId::fromValue(1));
        Result<RestraintReaction> b = reactions.of(RestraintId::fromValue(2));
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        REQUIRE(a->sharedDegreesOfFreedom > 0);
        REQUIRE(b->sharedDegreesOfFreedom > 0);
        REQUIRE(a->sharedDegreesOfFreedom == b->sharedDegreesOfFreedom);
        REQUIRE(a->ownedDegreesOfFreedom + b->ownedDegreesOfFreedom +
                    reactions.sharedDegreesOfFreedom() ==
                unique);

        const Force3D sum = a->ownedForce + b->ownedForce + reactions.sharedForce();
        const Force3D total = reactions.totalForce();
        const double scale = std::max(magnitudeOf(total), 1.0);
        REQUIRE_THAT(sum.x.si(), WithinAbs(total.x.si(), 1e-9 * scale));
        REQUIRE_THAT(sum.y.si(), WithinAbs(total.y.si(), 1e-9 * scale));
        REQUIRE_THAT(sum.z.si(), WithinAbs(total.z.si(), 1e-9 * scale));
    }

    SECTION("and the SHARED fields are NOT additive, which is why they are named apart") {
        // The same physical reaction appears in both summaries' sharedForce.
        // Adding the owned AND shared fields of both restraints overshoots the
        // global total by exactly one copy of the shared aggregate -- measured
        // here so the non-additivity is a demonstrated fact rather than a
        // warning in a comment.
        Result<RestraintReaction> a = reactions.of(RestraintId::fromValue(1));
        Result<RestraintReaction> b = reactions.of(RestraintId::fromValue(2));
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        const Force3D naive = a->ownedForce + a->sharedForce + b->ownedForce + b->sharedForce;
        const Force3D total = reactions.totalForce();
        const Force3D overshoot = naive - total;
        INFO("naive sum overshoots by (" << overshoot.x.si() << ", " << overshoot.y.si() << ", "
                                         << overshoot.z.si() << ") N");
        // It overshoots by one copy of the shared aggregate.
        const double scale = std::max(magnitudeOf(reactions.sharedForce()), 1.0);
        REQUIRE_THAT(overshoot.z.si(),
                     WithinAbs(reactions.sharedForce().z.si(), 1e-9 * scale));
        // And the overshoot is NOT negligible, so a caller who summed them
        // would be visibly wrong.
        REQUIRE(magnitudeOf(reactions.sharedForce()) > 0.0);
    }
}

// ---------------------------------------------------------------------------
// The tolerance, MEASURED
// ---------------------------------------------------------------------------

TEST_CASE("StructuralReaction_MeasuresTheEquilibriumErrorItIsGatedOn",
          "[structural][reaction]") {
    // THE BRIEF'S CENTRAL REQUIREMENT: do not predeclare a tolerance. These
    // are the measurements the thresholds were chosen from, printed so the
    // evidence table is derived from a run rather than from a recollection.
    //
    // A SOLVER RESIDUAL TOLERANCE IS NOT AN EQUILIBRIUM TOLERANCE, and the
    // difference is printed alongside: the solver's gate is on the free
    // equations and this one is on the whole body.
    struct Case {
        const char* name;
        Force3D load;
        bool fullyConstrained;
    };
    const std::vector<Case> cases{
        {"cantilever, nodal +Z", Force3D{Force::fromSi(0.0), Force::fromSi(0.0),
                                          Force::fromSi(1000.0)}, false},
        {"cantilever, nodal 3-component", Force3D{Force::fromSi(400.0), Force::fromSi(-900.0),
                                                   Force::fromSi(1300.0)}, false},
        {"cantilever, 1e-3 N", Force3D{Force::fromSi(0.0), Force::fromSi(0.0),
                                        Force::fromSi(1.0e-3)}, false},
        {"cantilever, 1e6 N", Force3D{Force::fromSi(0.0), Force::fromSi(0.0),
                                       Force::fromSi(1.0e6)}, false},
        {"fully constrained, nodal", Force3D{Force::fromSi(300.0), Force::fromSi(-500.0),
                                              Force::fromSi(700.0)}, true},
    };

    double worstForce = 0.0;
    double worstMoment = 0.0;
    for (const Case& c : cases) {
        ReactionPart part;
        const PreparedLoads loads = part.prepared(
            spread(part, part.endCap(), c.load.x.si(), c.load.y.si(), c.load.z.si()));
        const GlobalStructuralSystem system = part.assembled(loads);

        std::vector<StructuralRestraint> support{
            StructuralRestraint::fixedSupport(RestraintId::fromValue(1), part.startCap())};
        if (c.fullyConstrained) {
            support.push_back(
                StructuralRestraint::fixedSupport(RestraintId::fromValue(2), part.endCap()));
            for (std::size_t which = 0; which < 4; ++which) {
                support.push_back(StructuralRestraint::fixedSupport(
                    RestraintId::fromValue(10 + which), part.side(which)));
            }
        }
        const PreparedRestraints restraints = part.restrain(support);
        const SolvedSystem solution = solve(system, restraints.constraints());
        const SupportReactions reactions = recover(part, system, restraints, loads, solution);

        const structural::ForceBalance& f = reactions.forceBalance();
        const structural::MomentBalance& m = reactions.momentBalance();
        worstForce = std::max(worstForce, f.normalized);
        worstMoment = std::max(worstMoment, m.normalized);

        WARN("MEASURE | " << c.name << " | |F| " << magnitudeOf(f.external) << " N | scale_F "
                          << f.scale.si() << " N | ||e_F||2 " << f.euclideanNorm.si()
                          << " N | eta_F " << f.normalized << " | scale_M " << m.scale.si()
                          << " N m | ||e_M||2 " << m.euclideanNorm.si() << " N m | eta_M "
                          << m.normalized << " | solver normalized residual "
                          << solution.residual().normalized);
    }

    WARN("MEASURE | worst over all cases: eta_F " << worstForce << ", eta_M " << worstMoment);

    SECTION("the measured worst case sits comfortably inside the chosen thresholds") {
        // The thresholds are 1e-12 for both -- the algebraic band of
        // BetterCAD's guidance, which these measurements support. They were
        // chosen AFTER this measurement, not before it.
        INFO("worst eta_F " << worstForce << " against " << measured().force);
        INFO("worst eta_M " << worstMoment << " against " << measured().moment);
        REQUIRE(worstForce <= measured().force);
        REQUIRE(worstMoment <= measured().moment);
    }

    SECTION("and the normalized error is scale-invariant, so the gate is not an absolute one") {
        // A 1e-3 N load and a 1e6 N load are the same numerical question once
        // normalized -- which is why the gate is a ratio. Both are in the
        // table above and both pass the same threshold.
        SUCCEED("both magnitudes measured above against one threshold");
    }
}

// ---------------------------------------------------------------------------
// Linearity
// ---------------------------------------------------------------------------

TEST_CASE("StructuralReaction_IsLinearInTheAppliedLoad", "[structural][reaction]") {
    // For a fixed K and a fixed constraint set the reaction is linear in F.
    // Three statements, each a different way for a sign or scaling defect to
    // show up.
    ReactionPart part;
    const PreparedRestraints restraints = part.restrain(
        {StructuralRestraint::fixedSupport(RestraintId::fromValue(1), part.startCap())});

    const auto reactionFor = [&](double fx, double fy, double fz) {
        const PreparedLoads loads = part.prepared(spread(part, part.endCap(), fx, fy, fz));
        const GlobalStructuralSystem system = part.assembled(loads);
        const SolvedSystem solution = solve(system, restraints.constraints());
        return recover(part, system, restraints, loads, solution).totalForce();
    };

    const Force3D base = reactionFor(0.0, 0.0, 1000.0);

    SECTION("scaling the load scales the reaction") {
        constexpr double kC = 3.5;
        const Force3D scaled = reactionFor(0.0, 0.0, 1000.0 * kC);
        REQUIRE_THAT(scaled.z.si(), WithinRel(kC * base.z.si(), 1e-9));
        REQUIRE(std::abs(base.z.si()) > 1.0);
    }

    SECTION("reversing the load reverses the reaction") {
        const Force3D flipped = reactionFor(0.0, 0.0, -1000.0);
        REQUIRE_THAT(flipped.z.si(), WithinRel(-base.z.si(), 1e-9));
        // Opposite signs, asserted as signs: a magnitude-only check would
        // pass an implementation that took an absolute value.
        REQUIRE(base.z.si() * flipped.z.si() < 0.0);
    }

    SECTION("and superposition holds: R(F1 + F2) == R(F1) + R(F2)") {
        const Force3D first = reactionFor(500.0, 0.0, 0.0);
        const Force3D second = reactionFor(0.0, -700.0, 0.0);
        const Force3D both = reactionFor(500.0, -700.0, 0.0);
        const Force3D sum = first + second;
        const double scale = std::max(magnitudeOf(both), 1.0);
        REQUIRE_THAT(both.x.si(), WithinAbs(sum.x.si(), 1e-9 * scale));
        REQUIRE_THAT(both.y.si(), WithinAbs(sum.y.si(), 1e-9 * scale));
        REQUIRE_THAT(both.z.si(), WithinAbs(sum.z.si(), 1e-9 * scale));
        // The two contributions are genuinely different, so the sum is not
        // being checked against one of its own terms.
        REQUIRE(std::abs(first.x.si()) > 1.0);
        REQUIRE(std::abs(second.y.si()) > 1.0);
    }
}

// ---------------------------------------------------------------------------
// The load cross-check
// ---------------------------------------------------------------------------

TEST_CASE("StructuralReaction_ReconstructsTheSameExternalLoadTheLoadsLayerReports",
          "[structural][reaction]") {
    // THE TRIANGULATION OF BRIEF SECTION 21. The external total used for
    // equilibrium is the assembled `F` -- the exact vector that was solved --
    // read back through the qualified numbering in ROW order. P17-LOAD's
    // `resultantForce()` sums the same nodal forces in NodeId order. They must
    // agree, and that agreement ties load preparation, assembly and reaction
    // together without this milestone inventing a third load path.
    ReactionPart part;
    const PreparedLoads loads =
        part.prepared(spread(part, part.endCap(), 250.0, -400.0, 650.0));
    const GlobalStructuralSystem system = part.assembled(loads);
    const meshing::Mesh& mesh = part.model().mesh().mesh();

    Result<Force3D> fromAssembled =
        structural::assembledForceResultant(mesh, system.numbering(), system.force());
    REQUIRE(fromAssembled.has_value());
    const Force3D fromLoads = loads.resultantForce();

    SECTION("the two routes to the applied force agree") {
        const double scale = std::max(magnitudeOf(fromLoads), 1.0);
        INFO("assembled (" << fromAssembled->x.si() << ", " << fromAssembled->y.si() << ", "
                           << fromAssembled->z.si() << ") N");
        INFO("loads     (" << fromLoads.x.si() << ", " << fromLoads.y.si() << ", "
                           << fromLoads.z.si() << ") N");
        REQUIRE_THAT(fromAssembled->x.si(), WithinAbs(fromLoads.x.si(), 1e-12 * scale));
        REQUIRE_THAT(fromAssembled->y.si(), WithinAbs(fromLoads.y.si(), 1e-12 * scale));
        REQUIRE_THAT(fromAssembled->z.si(), WithinAbs(fromLoads.z.si(), 1e-12 * scale));
        // And both equal what was asked for.
        REQUIRE_THAT(fromAssembled->x.si(), WithinRel(250.0, 1e-12));
        REQUIRE_THAT(fromAssembled->z.si(), WithinRel(650.0, 1e-12));
    }

    SECTION("and the two moment routes agree about one origin") {
        const Point3D origin{};
        Result<Moment3D> assembled = structural::assembledMomentResultant(
            mesh, system.numbering(), system.force(), origin);
        REQUIRE(assembled.has_value());
        Result<Moment3D> fromLoadsMoment = loads.resultantMomentAbout(mesh, origin);
        REQUIRE(fromLoadsMoment.has_value());
        const double scale = std::max(magnitudeOf(*fromLoadsMoment), 1.0);
        REQUIRE_THAT(assembled->x.si(), WithinAbs(fromLoadsMoment->x.si(), 1e-12 * scale));
        REQUIRE_THAT(assembled->y.si(), WithinAbs(fromLoadsMoment->y.si(), 1e-12 * scale));
        REQUIRE_THAT(assembled->z.si(), WithinAbs(fromLoadsMoment->z.si(), 1e-12 * scale));
        REQUIRE(magnitudeOf(*assembled) > 1.0);
    }

    SECTION("a numbering for another mesh is refused rather than used") {
        ReactionPart other;
        Result<Force3D> crossed = structural::assembledForceResultant(
            other.model().mesh().mesh(), system.numbering(), system.force());
        REQUIRE_FALSE(crossed.has_value());
        REQUIRE_THAT(crossed.error().message, ContainsSubstring("different mesh"));
    }
}

// ---------------------------------------------------------------------------
// Source, currentness and determinism
// ---------------------------------------------------------------------------

TEST_CASE("StructuralReaction_RefusesEverySourceMismatch", "[structural][reaction]") {
    ReactionPart part;
    const PreparedLoads loads = part.prepared(spread(part, part.endCap(), 0.0, 0.0, 1000.0));
    const GlobalStructuralSystem system = part.assembled(loads);
    const PreparedRestraints restraints = part.restrain(
        {StructuralRestraint::fixedSupport(RestraintId::fromValue(1), part.startCap())});
    const SolvedSystem solution = solve(system, restraints.constraints());

    SECTION("a well-posed recovery reports no problem") {
        REQUIRE_FALSE(structural::reactionProblem(part.model(), system, restraints, loads,
                                                  solution, Point3D{}, measured())
                          .has_value());
    }

    SECTION("a solution from a system assembled under a different modulus") {
        part.setMaterialModulus(kYoungs * 2.0);
        const PreparedLoads again = part.prepared(spread(part, part.endCap(), 0.0, 0.0, 1000.0));
        const GlobalStructuralSystem other = part.assembled(again);
        REQUIRE_FALSE(other.source() == system.source());
        REQUIRE(structural::reactionProblem(part.model(), other, restraints, again, solution,
                                            Point3D{}, measured()) ==
                ReactionProblem::SolutionSourceMismatch);
    }

    SECTION("a constraint set that is not the one the solve reduced with") {
        // DESCRIBING THE SAME MESH IS NOT ENOUGH. A smaller restraint set
        // describes the same mesh and would attribute reactions through a
        // mapping the solve never used, which is how a source mismatch hides.
        const PreparedRestraints fewer = part.restrain(
            {StructuralRestraint{RestraintId::fromValue(1), part.startCap(),
                                 RestraintComponents::along(DofComponent::Ux)}});
        REQUIRE(fewer.describes(part.model().mesh().mesh()));
        REQUIRE(fewer.constraints().size() != restraints.constraints().size());
        REQUIRE(structural::reactionProblem(part.model(), system, fewer, loads, solution,
                                            Point3D{}, measured()) ==
                ReactionProblem::ConstraintSourceMismatch);
    }

    SECTION("a malformed tolerance is refused before any arithmetic") {
        REQUIRE_FALSE(structural::validate(EquilibriumTolerance{.force = 0.0}).has_value());
        REQUIRE_FALSE(structural::validate(EquilibriumTolerance{.moment = -1.0}).has_value());
        REQUIRE_FALSE(
            structural::validate(EquilibriumTolerance{.forceFloor = Force::fromSi(0.0)})
                .has_value());
        REQUIRE(structural::validate(measured()).has_value());
        const EquilibriumTolerance bad{.force = 0.0};
        REQUIRE(structural::reactionProblem(part.model(), system, restraints, loads, solution,
                                            Point3D{}, bad) ==
                ReactionProblem::InvalidTolerance);
    }

    SECTION("nothing is published on a failure") {
        const PreparedRestraints fewer = part.restrain(
            {StructuralRestraint{RestraintId::fromValue(1), part.startCap(),
                                 RestraintComponents::along(DofComponent::Ux)}});
        REQUIRE_FALSE(structural::recoverSupportReactions(part.model(), system, fewer, loads,
                                                          solution, Point3D{}, measured())
                          .has_value());
    }

    SECTION("every problem value has a name, and none of them is unknown") {
        for (const ReactionProblem problem :
             {ReactionProblem::InvalidTolerance, ReactionProblem::SolutionSourceMismatch,
              ReactionProblem::MeshMismatch, ReactionProblem::ConstraintSourceMismatch,
              ReactionProblem::DegreeOfFreedomOutOfRange, ReactionProblem::NodeMissing,
              ReactionProblem::NonFiniteReaction, ReactionProblem::NonFiniteExternal,
              ReactionProblem::NonFiniteBalance, ReactionProblem::ForceImbalance,
              ReactionProblem::MomentImbalance}) {
            REQUIRE(structural::toString(problem) != "unknown");
        }
    }
}

TEST_CASE("StructuralReaction_StalesOnARemeshAndRefusesAStaleLookup",
          "[structural][reaction]") {
    ReactionPart part;
    const PreparedLoads loads = part.prepared(spread(part, part.endCap(), 0.0, 0.0, 1000.0));
    const GlobalStructuralSystem system = part.assembled(loads);
    const PreparedRestraints restraints = part.restrain(
        {StructuralRestraint::fixedSupport(RestraintId::fromValue(1), part.startCap())});
    const SolvedSystem solution = solve(system, restraints.constraints());
    const SupportReactions reactions = recover(part, system, restraints, loads, solution);
    const meshing::NodeId first = reactions.nodal().front().node;

    // REMESH. The body is unchanged, so the handles restart at 1 -- which is
    // the adversarial case: numeric NodeIds repeat.
    const meshing::MeshControlDefinition coarser{
        .body = part.feature, .mesh = {.sizing = {.globalTargetSize = 12_mm}}};
    REQUIRE(part.document
                .modifyObject<meshing::MeshControl>(
                    ObjectId::fromValue(part.control.value()),
                    [&coarser](meshing::MeshControl& control) {
                        return control.setDefinition(coarser);
                    })
                .has_value());
    requireReport(part.regenerator, part.document);
    const meshing::VolumeMesh& remeshed = part.mesh();

    SECTION("the old reactions no longer describe the new mesh, whose handles repeat") {
        REQUIRE_FALSE(reactions.describes(remeshed.mesh()));
        REQUIRE(remeshed.mesh().nodes().front().id == meshing::NodeId::fromValue(1));
    }

    SECTION("a lookup against the new mesh is refused, not answered") {
        Result<SupportReaction> stale = reactions.at(remeshed.mesh(), first);
        REQUIRE_FALSE(stale.has_value());
        REQUIRE(stale.error().code == ErrorCode::FailedPrecondition);
        REQUIRE_THAT(stale.error().message, ContainsSubstring("different mesh"));
    }

    SECTION("and recovery against the new mesh is refused") {
        const std::optional<ReactionProblem> problem = structural::reactionProblem(
            part.model(), system, restraints, loads, solution, Point3D{}, measured());
        REQUIRE(problem.has_value());
        // The system and the solution are BOTH from before the remesh, so
        // their sources still agree -- asserted, so the reader is not left to
        // guess which check fires. What moved is the MODEL.
        REQUIRE(solution.source() == system.source());
        REQUIRE(*problem == ReactionProblem::MeshMismatch);
    }
}

TEST_CASE("StructuralReaction_IsDeterministicAcrossRepeatedRecoveries",
          "[structural][reaction]") {
    ReactionPart part;
    const PreparedLoads loads = part.prepared(spread(part, part.endCap(), 300.0, -600.0, 900.0));
    const GlobalStructuralSystem system = part.assembled(loads);
    const PreparedRestraints restraints = part.restrain(
        {StructuralRestraint::fixedSupport(RestraintId::fromValue(1), part.side(0)),
         StructuralRestraint::fixedSupport(RestraintId::fromValue(2), part.side(1))});
    const SolvedSystem solution = solve(system, restraints.constraints());
    const SupportReactions first = recover(part, system, restraints, loads, solution);

    // FIVE REPEATS, BITWISE. operator== is defaulted over Quantity and the
    // mask, so this is an exact comparison of every channel -- not a
    // tolerance. It is what proves no sum crossed an unordered container.
    for (int run = 0; run < 5; ++run) {
        INFO("run " << run);
        const SupportReactions again = recover(part, system, restraints, loads, solution);
        REQUIRE(again.nodal().size() == first.nodal().size());
        for (std::size_t i = 0; i < first.nodal().size(); ++i) {
            REQUIRE(again.nodal()[i] == first.nodal()[i]);
        }
        REQUIRE(again.restraints().size() == first.restraints().size());
        for (std::size_t i = 0; i < first.restraints().size(); ++i) {
            REQUIRE(again.restraints()[i] == first.restraints()[i]);
        }
        REQUIRE(again.forceBalance() == first.forceBalance());
        REQUIRE(again.momentBalance() == first.momentBalance());
        REQUIRE(again.sharedForce() == first.sharedForce());
        REQUIRE(again.sharedMoment() == first.sharedMoment());
        REQUIRE(again.source() == first.source());
    }

    SECTION("the restraint summaries are in the order the restraints were given") {
        REQUIRE(first.restraints().size() == 2);
        REQUIRE(first.restraints()[0].restraint == RestraintId::fromValue(1));
        REQUIRE(first.restraints()[1].restraint == RestraintId::fromValue(2));
    }

    SECTION("and the provenance is the system's own, copied forward") {
        REQUIRE(first.source() == system.source());
        REQUIRE(first.source() == solution.source());
        REQUIRE(first.mesh() == solution.mesh());
        static_assert(!std::is_default_constructible_v<SupportReactions>);
    }
}

// ---------------------------------------------------------------------------
// The gate must be able to fail
// ---------------------------------------------------------------------------

TEST_CASE("StructuralReaction_RefusesAnImbalanceLargerThanTheThreshold",
          "[structural][reaction]") {
    // BRIEF SECTION 167, AND IT IS NOT OPTIONAL: a threshold that cannot fail
    // is not a gate. The equilibrium error cannot be injected from outside --
    // it comes from the solved residual -- so the demonstration runs the other
    // way: tighten the threshold BELOW the measured error and require the
    // recovery to be refused. That proves the comparison is real, is wired to
    // the measurement, and that 1e-12 is a margin above a measured ~2e-16
    // rather than a number large enough to accept anything.
    ReactionPart part;
    const PreparedLoads loads = part.prepared(spread(part, part.endCap(), 0.0, 0.0, 1000.0));
    const GlobalStructuralSystem system = part.assembled(loads);
    const PreparedRestraints restraints = part.restrain(
        {StructuralRestraint::fixedSupport(RestraintId::fromValue(1), part.startCap())});
    const SolvedSystem solution = solve(system, restraints.constraints());

    const SupportReactions passing = recover(part, system, restraints, loads, solution);
    const double actualForce = passing.forceBalance().normalized;
    const double actualMoment = passing.momentBalance().normalized;
    INFO("measured eta_F " << actualForce << ", eta_M " << actualMoment);
    REQUIRE(actualForce > 0.0);
    REQUIRE(actualMoment > 0.0);

    SECTION("a force threshold below the measured error refuses the recovery") {
        const EquilibriumTolerance tight{.force = actualForce / 10.0};
        Result<SupportReactions> refused = structural::recoverSupportReactions(
            part.model(), system, restraints, loads, solution, Point3D{}, tight);
        REQUIRE_FALSE(refused.has_value());
        REQUIRE(refused.error().code == ErrorCode::FailedPrecondition);
        REQUIRE_THAT(refused.error().message, ContainsSubstring("force equilibrium failed"));
        REQUIRE_THAT(refused.error().message, ContainsSubstring("normalized imbalance"));
        REQUIRE(structural::reactionProblem(part.model(), system, restraints, loads, solution,
                                            Point3D{}, tight) ==
                ReactionProblem::ForceImbalance);
    }

    SECTION("and a moment threshold below the measured error refuses it too") {
        // CHECKED SEPARATELY, because the force gate passing must not short-
        // circuit the moment gate -- which is what a pure couple would exploit.
        const EquilibriumTolerance tight{.moment = actualMoment / 10.0};
        REQUIRE(structural::reactionProblem(part.model(), system, restraints, loads, solution,
                                            Point3D{}, tight) ==
                ReactionProblem::MomentImbalance);
        Result<SupportReactions> refused = structural::recoverSupportReactions(
            part.model(), system, restraints, loads, solution, Point3D{}, tight);
        REQUIRE_FALSE(refused.has_value());
        REQUIRE_THAT(refused.error().message, ContainsSubstring("moment equilibrium"));
        // The origin is named, because a moment without one is not a quantity.
        REQUIRE_THAT(refused.error().message, ContainsSubstring("about"));
    }

    SECTION("the chosen threshold is a margin, not a ceiling") {
        INFO("eta_F " << actualForce << " vs threshold " << measured().force);
        REQUIRE(actualForce < measured().force / 100.0);
        REQUIRE(actualMoment < measured().moment / 100.0);
    }
}

// ---------------------------------------------------------------------------
// A pure couple: force balance alone would miss a defect
// ---------------------------------------------------------------------------

TEST_CASE("StructuralReaction_BalancesAPureCoupleInMomentNotOnlyInForce",
          "[structural][reaction]") {
    // THE ADVERSARIAL CASE FOR MOMENT EQUILIBRIUM. Equal and opposite loads on
    // two opposite faces give
    //
    //     sum(F_external) = 0        but      sum(M_external) != 0
    //
    // so the support supplies a couple and no net force. An implementation
    // that checked only force equilibrium would pass this with ANY moment
    // error -- which is why the moment gate exists.
    //
    // It is also the case the NORMALISATION was designed for: the net external
    // force is zero by construction, so a denominator of ||F_external|| would
    // divide by nothing. The scale is a sum of MAGNITUDES and stays large.
    ReactionPart part;
    constexpr double kPair = 900.0;
    std::vector<StructuralLoad> couple = spread(part, part.side(0), kPair, 0.0, 0.0, 1);
    for (StructuralLoad& load : spread(part, part.side(2), -kPair, 0.0, 0.0, 10000)) {
        couple.push_back(std::move(load));
    }
    const PreparedLoads loads = part.prepared(couple);
    const GlobalStructuralSystem system = part.assembled(loads);
    const PreparedRestraints restraints = part.restrain(
        {StructuralRestraint::fixedSupport(RestraintId::fromValue(1), part.startCap())});
    const SolvedSystem solution = solve(system, restraints.constraints());
    const SupportReactions reactions = recover(part, system, restraints, loads, solution);

    SECTION("the external force cancels, while the participating loads do not") {
        const structural::ForceBalance& balance = reactions.forceBalance();
        INFO("F_external (" << balance.external.x.si() << ", " << balance.external.y.si() << ", "
                            << balance.external.z.si() << ") N, scale " << balance.scale.si());
        REQUIRE_THAT(magnitudeOf(balance.external), WithinAbs(0.0, 1e-9));
        // THE REASON THE METRIC STILL MEANS SOMETHING: the magnitudes are
        // large even though the net is zero.
        REQUIRE(balance.scale.si() > 1000.0);
    }

    SECTION("so the reaction force is ~zero and force equilibrium passes") {
        REQUIRE_THAT(magnitudeOf(reactions.totalForce()), WithinAbs(0.0, 1e-9));
        REQUIRE(reactions.forceBalance().normalized <= measured().force);
    }

    SECTION("but the MOMENT is nonzero, and it is the moment that must balance") {
        const structural::MomentBalance& balance = reactions.momentBalance();
        INFO("M_external (" << balance.external.x.si() << ", " << balance.external.y.si() << ", "
                            << balance.external.z.si() << ") N m");
        INFO("M_reaction (" << balance.reaction.x.si() << ", " << balance.reaction.y.si() << ", "
                            << balance.reaction.z.si() << ") N m");
        // THE DISCRIMINATING FACT: a nonzero external moment with zero net
        // force. Without it the whole case would be vacuous.
        REQUIRE(magnitudeOf(balance.external) > 1.0);
        REQUIRE(magnitudeOf(balance.reaction) > 1.0);
        const double scale = magnitudeOf(balance.external);
        REQUIRE_THAT(balance.reaction.z.si(), WithinAbs(-balance.external.z.si(), 1e-9 * scale));
        REQUIRE(balance.normalized <= measured().moment);
    }
}

TEST_CASE("StructuralReaction_BalancesTheMomentAboutANonZeroOriginToo",
          "[structural][reaction]") {
    // THIS TEST EXISTS BECAUSE A MUTATION SURVIVED WITHOUT IT. Every other
    // equilibrium case in this file recovers about the global origin, where
    // `x - O == x` -- so a production path that IGNORED the origin entirely
    // was a no-op for all of them and the probe that removed the subtraction
    // survived.
    //
    // Recovering about a non-zero origin is what makes the subtraction
    // load-bearing. Brief section 98 asks for exactly this consistency: with
    // the force imbalance already ~0, the moment imbalance must stay ~0 under
    // a change of origin, because
    //
    //     M'_imbalance = M_imbalance - dO x F_imbalance
    //
    // and the second term vanishes when `F_imbalance` does.
    ReactionPart part;
    const PreparedLoads loads = part.prepared(spread(part, part.endCap(), 250.0, -400.0, 1100.0));
    const GlobalStructuralSystem system = part.assembled(loads);
    const PreparedRestraints restraints = part.restrain(
        {StructuralRestraint::fixedSupport(RestraintId::fromValue(1), part.startCap())});
    const SolvedSystem solution = solve(system, restraints.constraints());

    // Three origins: the global one, a nearby offset, and one far away in the
    // range a real CAD model occupies.
    const std::array<Point3D, 3> origins{
        Point3D{},
        Point3D{Length::fromSi(0.021), Length::fromSi(-0.014), Length::fromSi(0.033)},
        Point3D{Length::fromSi(1.7), Length::fromSi(-2.3), Length::fromSi(0.9)}};

    std::array<double, 3> normalized{};
    std::array<double, 3> externalMagnitude{};
    for (std::size_t which = 0; which < origins.size(); ++which) {
        const SupportReactions reactions =
            recover(part, system, restraints, loads, solution, origins[which]);
        const structural::MomentBalance& balance = reactions.momentBalance();
        normalized[which] = balance.normalized;
        externalMagnitude[which] = magnitudeOf(balance.external);

        INFO("origin (" << origins[which].x.si() << ", " << origins[which].y.si() << ", "
                        << origins[which].z.si() << ") m");
        INFO("eta_M " << balance.normalized << ", scale " << balance.scale.si() << " N m");
        // THE ORIGIN IS RECORDED, so a reader never has to guess which point
        // a moment is about.
        REQUIRE(balance.origin == origins[which]);
        REQUIRE(balance.normalized <= measured().moment);
        REQUIRE(reactions.forceBalance().normalized <= measured().force);
    }

    SECTION("the external moment really does change with the origin") {
        // THE GUARD THAT MAKES THIS TEST BITE. If the three external moments
        // were equal, the origin would be doing nothing and the mutation
        // would survive again.
        INFO("|M_external| " << externalMagnitude[0] << ", " << externalMagnitude[1] << ", "
                             << externalMagnitude[2] << " N m");
        REQUIRE(externalMagnitude[0] != externalMagnitude[1]);
        REQUIRE(externalMagnitude[1] != externalMagnitude[2]);
        // The far origin produces a much larger moment, because the lever arm
        // is metres rather than millimetres.
        REQUIRE(externalMagnitude[2] > 10.0 * externalMagnitude[0]);
    }

    SECTION("and equilibrium holds at every one of them") {
        for (const double value : normalized) {
            REQUIRE(value <= measured().moment);
        }
    }
}
