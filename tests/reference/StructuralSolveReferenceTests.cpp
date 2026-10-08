// P17-SOLVE-001 against the meshing reference models.
//
// WHY THESE ARE SEPARATE from tests/structural/StructuralSolveTests.cpp. Those
// tests validate the kernel against closed-form 2x2 and 3x3 systems and the
// three gates against synthetic near-singular ones. These do the two things a
// synthetic fixture cannot:
//
//   an INDEPENDENT DENSE ORACLE over a real reduced structural system --
//     Gaussian elimination with partial pivoting, written here, which shares
//     no code and no algorithm with the production LDLT
//   the SCALE claims -- a reference model solves, and a mesh an order of
//     magnitude larger still solves in sparse storage
//
// THE ORACLE IS A DIFFERENT ALGORITHM, NOT THE SAME ONE TWICE. Brief section
// 67 is explicit that reusing Eigen's decomposition in the test would not be
// an independent oracle. The elimination below pivots, which LDLT does not; it
// is dense, which the production path never is; and it is written out here in
// twenty lines so a reader can see there is no shared assumption.
//
// THE AXIAL CHECK IS ONE-SIDED, DELIBERATELY. A constant-strain Tet4 is
// STIFFER than the continuum it approximates, so a coarse mesh must give a
// SMALLER displacement than `delta = F L / (A E)`. That inequality is known a
// priori and cannot be tuned, which makes it a real check; demanding equality
// on a six-element mesh would have been a tolerance invented to pass.

#include "reference/MeshTestSupport.hpp"

#include <MeshReferenceModels.hpp>

#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/meshing/MeshSizing.hpp>
#include <bettercad/structural/StructuralConstraints.hpp>
#include <bettercad/structural/StructuralLoadVector.hpp>
#include <bettercad/structural/StructuralSolve.hpp>
#include <bettercad/structural/StructuralSystem.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
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
using structural::DofComponent;
using structural::DofIndex;
using structural::FreeEquationMap;
using structural::FreeSystem;
using structural::GlobalStructuralSystem;
using structural::kDofsPerNode;
using structural::MeshDofMap;
using structural::PreparedLoads;
using structural::SolvedSystem;
using structural::SolverSettings;
using structural::StructuralAnalysisMode;
using structural::StructuralLoad;
using structural::StructuralMaterial;
using structural::StructuralModel;
using structural::StructuralRestraint;

constexpr double kYoungs = 210.0e9;

void assignSteel(Document& document) {
    features::MaterialDefinition definition;
    definition.designation = "Steel";
    definition.mechanical.youngsModulus =
        materials::MaterialProperty<ElasticModulus>::known(ElasticModulus::fromSi(kYoungs));
    definition.mechanical.poissonRatio =
        materials::MaterialProperty<bettercad::PoissonRatio>::known(PoissonRatio::of(0.3));
    definition.mechanical.density =
        materials::MaterialProperty<Density>::known(Density::fromSi(7850.0));
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

[[nodiscard]] GlobalStructuralSystem assemble(MeshedReference& reference,
                                              const std::vector<StructuralLoad>& loads) {
    const StructuralModel model = modelOf(reference);
    Result<StructuralMaterial> material = structural::resolveStructuralMaterial(
        reference.document(), reference.body(), StructuralAnalysisMode::LinearStatic);
    INFO((material.has_value() ? std::string{} : material.error().message));
    REQUIRE(material.has_value());
    Result<PreparedLoads> prepared =
        structural::prepareStructuralLoads(model, *material, loads);
    INFO((prepared.has_value() ? std::string{} : prepared.error().message));
    REQUIRE(prepared.has_value());
    Result<GlobalStructuralSystem> system =
        structural::assembleStructuralSystem(model, *material, *prepared);
    INFO((system.has_value() ? std::string{} : system.error().message));
    REQUIRE(system.has_value());
    return std::move(*system);
}

[[nodiscard]] ConstraintSet fixedSupportOn(MeshedReference& reference, const FaceName& face) {
    const StructuralModel model = modelOf(reference);
    Result<MeshDofMap> numbering = structural::buildMeshDofMap(model.mesh().mesh());
    REQUIRE(numbering.has_value());
    Result<structural::PreparedRestraints> prepared = structural::prepareStructuralRestraints(
        model, *numbering,
        std::vector{StructuralRestraint::fixedSupport(RestraintId::fromValue(1), face)});
    INFO((prepared.has_value() ? std::string{} : prepared.error().message));
    REQUIRE(prepared.has_value());
    return prepared->constraints();
}

/// Every node of @p face pushed along @p axis, sharing @p total between them.
[[nodiscard]] std::vector<StructuralLoad> spreadLoad(MeshedReference& reference,
                                                     const FaceName& face, std::size_t axis,
                                                     double total) {
    const StructuralModel model = modelOf(reference);
    Result<meshing::BoundaryFacetSet> facets = meshing::boundaryFacetsOf(model.map(), face);
    REQUIRE(facets.has_value());
    Result<std::vector<meshing::NodeId>> nodes =
        meshing::boundaryNodesOf(model.map(), model.mesh().mesh(), facets->facets);
    REQUIRE(nodes.has_value());
    REQUIRE_FALSE(nodes->empty());

    const double each = total / static_cast<double>(nodes->size());
    std::array<double, 3> components{};
    components[axis] = each;
    std::vector<StructuralLoad> loads;
    std::uint64_t id = 1;
    for (const meshing::NodeId node : *nodes) {
        loads.push_back(StructuralLoad{
            LoadId::fromValue(id++),
            structural::NodalForceLoad{
                .mesh = model.mesh().mesh().stamp(),
                .node = node,
                .force = Force3D{Force::fromSi(components[0]), Force::fromSi(components[1]),
                                 Force::fromSi(components[2])}}});
    }
    return loads;
}

// -----------------------------------------------------------------------
// The independent oracle: dense Gaussian elimination with partial pivoting
// -----------------------------------------------------------------------

/// Solves `A x = b` densely by Gaussian elimination with partial pivoting.
///
/// A DIFFERENT ALGORITHM FROM PRODUCTION, which is the point. LDLT exploits
/// symmetry, uses no pivoting and applies an AMD reordering; this pivots, is
/// dense, ignores symmetry and reorders nothing. Written out here rather than
/// taken from a library so there is no shared assumption to inherit.
[[nodiscard]] std::vector<double> eliminate(std::size_t rows, std::vector<double> dense,
                                            std::vector<double> rhs) {
    REQUIRE(dense.size() == rows * rows);
    REQUIRE(rhs.size() == rows);
    for (std::size_t column = 0; column < rows; ++column) {
        // Partial pivoting: the largest magnitude in the column, which LDLT
        // never does.
        std::size_t pivot = column;
        for (std::size_t row = column + 1; row < rows; ++row) {
            if (std::abs(dense[row * rows + column]) > std::abs(dense[pivot * rows + column])) {
                pivot = row;
            }
        }
        REQUIRE(std::abs(dense[pivot * rows + column]) > 0.0);
        if (pivot != column) {
            for (std::size_t k = 0; k < rows; ++k) {
                std::swap(dense[column * rows + k], dense[pivot * rows + k]);
            }
            std::swap(rhs[column], rhs[pivot]);
        }
        const double diagonal = dense[column * rows + column];
        for (std::size_t row = column + 1; row < rows; ++row) {
            const double factor = dense[row * rows + column] / diagonal;
            if (factor == 0.0) {
                continue;
            }
            for (std::size_t k = column; k < rows; ++k) {
                dense[row * rows + k] -= factor * dense[column * rows + k];
            }
            rhs[row] -= factor * rhs[column];
        }
    }
    std::vector<double> x(rows, 0.0);
    for (std::size_t i = rows; i-- > 0;) {
        double sum = rhs[i];
        for (std::size_t k = i + 1; k < rows; ++k) {
            sum -= dense[i * rows + k] * x[k];
        }
        x[i] = sum / dense[i * rows + i];
    }
    return x;
}

} // namespace

TEST_CASE("StructuralSolve_MatchesAnIndependentDenseEliminationOfTheFreeSystem",
          "[structural][solve][reference]") {
    // Brief sections 65, 66 and 67. RM-MESH-01 is the smallest reference
    // model -- 8 nodes, 6 tetrahedra -- which makes its reduced system small
    // enough for a dense oracle while still being a real structural problem
    // produced by the whole qualified chain: P17-ELEM, P17-ASSEMBLY, P17-BC
    // and P17-DOF.
    auto built = reference::buildMeshBlockReferenceModel();
    REQUIRE(built.has_value());
    const FaceName bottom = built->bottom();
    const FaceName top = built->top();
    MeshedReference reference(std::move(built->document));
    assignSteel(reference.document());
    reference.require();

    const double applied = 5000.0;
    const GlobalStructuralSystem system =
        assemble(reference, spreadLoad(reference, top, 2, applied));
    const ConstraintSet fixed = fixedSupportOn(reference, bottom);
    Result<SolvedSystem> solved = structural::solveStructuralSystem(system, fixed);
    INFO((solved.has_value() ? std::string{} : solved.error().message));
    REQUIRE(solved.has_value());

    Result<FreeEquationMap> equations =
        structural::buildFreeEquationMap(system.numbering(), fixed);
    REQUIRE(equations.has_value());
    Result<FreeSystem> reduced = structural::extractFreeSystem(system, *equations);
    REQUIRE(reduced.has_value());

    WARN("RM-MESH-01 | Ndof " << system.degreesOfFreedom() << " | Nconstrained " << fixed.size()
                              << " | Nfree " << reduced->rows() << " | nnz(Kff) "
                              << reduced->nonZeros() << " | pivot ratio "
                              << solved->pivotRatio() << " | ||r||2 "
                              << solved->residual().euclideanNorm.si() << " N | normalized "
                              << solved->residual().normalized);

    // The reduced system, densified HERE for the oracle only. Production never
    // densifies anything.
    const std::size_t rows = reduced->rows();
    REQUIRE(rows > 0);
    REQUIRE(rows <= 64);
    std::vector<double> dense(rows * rows, 0.0);
    std::vector<double> rhs(rows, 0.0);
    for (std::size_t row = 0; row < rows; ++row) {
        for (std::size_t column = 0; column < rows; ++column) {
            dense[row * rows + column] = reduced->coeff(row, column).si();
        }
        rhs[row] = reduced->forceAt(row).si();
    }
    const std::vector<double> expected = eliminate(rows, dense, rhs);

    SECTION("the production solution matches the independent elimination") {
        double scale = 0.0;
        for (const double value : expected) {
            scale = std::max(scale, std::abs(value));
        }
        REQUIRE(scale > 0.0);
        double largest = 0.0;
        const std::span<const DofIndex> freeDofs = equations->freeDofs();
        for (std::size_t equation = 0; equation < rows; ++equation) {
            const double got = solved->displacementOf(freeDofs[equation]).si();
            largest = std::max(largest, std::abs(got - expected[equation]));
        }
        WARN("largest |u_production - u_elimination| = " << largest << " m against a scale of "
                                                          << scale << " m, relative "
                                                          << largest / scale);
        // 1e-9 relative: two different factorisations of the same system agree
        // to their shared conditioning, not to the last bit. The measured
        // value is recorded above so the margin is visible.
        CHECK(largest < 1e-9 * scale);
    }

    SECTION("and the solve is a solution, independently of either algorithm") {
        CHECK(solved->residual().normalized <= solved->settings().relativeResidualTolerance);
        CHECK(solved->pivotRatio() > 1.0e4 * solved->settings().pivotFloor);
        for (const double value : solved->values()) {
            REQUIRE(std::isfinite(value));
        }
    }
}

TEST_CASE("StructuralSolve_IsStifferThanTheAxialContinuumAsATet4MustBe",
          "[structural][solve][reference]") {
    // Brief section 64, as a ONE-SIDED check with a reason.
    //
    // For a prism of length `L`, section `A`, under an axial load `F`:
    //
    // ```text
    //     delta = F L / (A E)
    // ```
    //
    // A constant-strain Tet4 cannot represent the exact linear displacement
    // field of this problem on a coarse mesh and is STIFFER than the
    // continuum, so the computed deflection must be SMALLER than `delta` --
    // and positive, and of the same order. That inequality is known before the
    // measurement and cannot be tuned, which is what makes it evidence;
    // demanding equality on six tetrahedra would have been a tolerance
    // invented to pass, and continuum accuracy is P17-REFMOD's subject.
    //
    // RM-MESH-01 is 120 x 70 x 35 mm, loaded along its 35 mm thickness with
    // the opposite face fixed, so `A = 0.120 * 0.070` and `L = 0.035` come
    // from the model's own declared dimensions and not from the mesh.
    auto built = reference::buildMeshBlockReferenceModel();
    REQUIRE(built.has_value());
    const FaceName bottom = built->bottom();
    const FaceName top = built->top();
    MeshedReference reference(std::move(built->document));
    assignSteel(reference.document());
    reference.require();

    const double applied = 1.0e5;
    const double area = 0.120 * 0.070;
    const double length = 0.035;
    const double analytic = applied * length / (area * kYoungs);

    const GlobalStructuralSystem system =
        assemble(reference, spreadLoad(reference, top, 2, applied));
    const ConstraintSet fixed = fixedSupportOn(reference, bottom);
    Result<SolvedSystem> solved = structural::solveStructuralSystem(system, fixed);
    INFO((solved.has_value() ? std::string{} : solved.error().message));
    REQUIRE(solved.has_value());

    // The mean axial displacement of the loaded face's nodes, through P17-DOF.
    const StructuralModel model = modelOf(reference);
    Result<meshing::BoundaryFacetSet> facets = meshing::boundaryFacetsOf(model.map(), top);
    REQUIRE(facets.has_value());
    Result<std::vector<meshing::NodeId>> nodes =
        meshing::boundaryNodesOf(model.map(), model.mesh().mesh(), facets->facets);
    REQUIRE(nodes.has_value());
    REQUIRE_FALSE(nodes->empty());

    double total = 0.0;
    for (const meshing::NodeId node : *nodes) {
        const Result<DofIndex> dof =
            system.numbering().indexOf(structural::NodalDof{.node = node,
                                                            .component = DofComponent::Uz});
        REQUIRE(dof.has_value());
        total += solved->displacementOf(*dof).si();
    }
    const double computed = total / static_cast<double>(nodes->size());

    WARN("RM-MESH-01 axial | F = " << applied << " N | F L / (A E) = " << analytic
                                   << " m | computed mean = " << computed << " m | ratio "
                                   << computed / analytic);

    SECTION("the deflection is positive, so the sign and the direction are right") {
        CHECK(computed > 0.0);
    }

    SECTION("and smaller than the continuum, because a Tet4 is stiffer") {
        CHECK(computed < analytic);
    }

    SECTION("and the same order of magnitude, so it is not merely small") {
        // A one-sided bound alone would be satisfied by zero, which is why
        // this half exists. A tenth of the continuum value on six constant-
        // strain tetrahedra is a weak but honest statement.
        CHECK(computed > 0.1 * analytic);
    }
}

TEST_CASE("StructuralSolve_SolvesTheReferenceModelsWithAFixedSupport",
          "[structural][solve][reference]") {
    // Brief section 105. The gate here is a well-posed solve: finite, residual
    // acceptable, pivot ratio clear of the floor. Continuum accuracy is
    // P17-REFMOD's.
    struct Case {
        const char* name;
        bool hole;
    };

    SECTION("RM-MESH-01, a plain block") {
        auto built = reference::buildMeshBlockReferenceModel();
        REQUIRE(built.has_value());
        const FaceName bottom = built->bottom();
        const FaceName top = built->top();
        MeshedReference reference(std::move(built->document));
        assignSteel(reference.document());
        reference.require();
        const GlobalStructuralSystem system =
            assemble(reference, spreadLoad(reference, top, 0, 2000.0));
        Result<SolvedSystem> solved =
            structural::solveStructuralSystem(system, fixedSupportOn(reference, bottom));
        INFO((solved.has_value() ? std::string{} : solved.error().message));
        REQUIRE(solved.has_value());
        WARN("RM-MESH-01 | Nfree " << solved->freeEquations() << " | nnz(Kff) "
                                   << solved->freeSystemNonZeros() << " | pivot ratio "
                                   << solved->pivotRatio() << " | normalized residual "
                                   << solved->residual().normalized << " | U "
                                   << solved->strainEnergy().si() << " J");
        CHECK(solved->residual().normalized <= solved->settings().relativeResidualTolerance);
        CHECK(solved->pivotRatio() > solved->settings().pivotFloor);
        CHECK(solved->strainEnergy().si() > 0.0);
    }

    SECTION("RM-MESH-03, a plate with a through-hole") {
        auto built = reference::buildMeshPlateWithHoleReferenceModel();
        REQUIRE(built.has_value());
        const FaceName bottom = built->bottom();
        const FaceName top = built->top();
        MeshedReference reference(std::move(built->document));
        assignSteel(reference.document());
        reference.require();
        const GlobalStructuralSystem system =
            assemble(reference, spreadLoad(reference, top, 2, 2000.0));
        Result<SolvedSystem> solved =
            structural::solveStructuralSystem(system, fixedSupportOn(reference, bottom));
        INFO((solved.has_value() ? std::string{} : solved.error().message));
        REQUIRE(solved.has_value());
        WARN("RM-MESH-03 | Nfree " << solved->freeEquations() << " | nnz(Kff) "
                                   << solved->freeSystemNonZeros() << " | pivot ratio "
                                   << solved->pivotRatio() << " | normalized residual "
                                   << solved->residual().normalized);
        CHECK(solved->residual().normalized <= solved->settings().relativeResidualTolerance);
    }

    SECTION("RM-MESH-04, a hollow tube") {
        auto built = reference::buildMeshTubeReferenceModel();
        REQUIRE(built.has_value());
        const FaceName bottom = built->bottomAnnulus();
        const FaceName top = built->topAnnulus();
        MeshedReference reference(std::move(built->document));
        assignSteel(reference.document());
        reference.require();
        const GlobalStructuralSystem system =
            assemble(reference, spreadLoad(reference, top, 2, 2000.0));
        Result<SolvedSystem> solved =
            structural::solveStructuralSystem(system, fixedSupportOn(reference, bottom));
        INFO((solved.has_value() ? std::string{} : solved.error().message));
        REQUIRE(solved.has_value());
        WARN("RM-MESH-04 | Nfree " << solved->freeEquations() << " | nnz(Kff) "
                                   << solved->freeSystemNonZeros() << " | pivot ratio "
                                   << solved->pivotRatio() << " | normalized residual "
                                   << solved->residual().normalized);
        CHECK(solved->residual().normalized <= solved->settings().relativeResidualTolerance);
    }
}

TEST_CASE("StructuralSolve_SolvesALargeMeshWithoutDensifying",
          "[structural][solve][reference]") {
    // Brief sections 106, 107 and 108. The claim a small model cannot make:
    // the free system stays sparse and the factorisation completes at a size
    // where a dense `Nfree x Nfree` would be absurd.
    //
    // RM-MESH-02's cylinder at 0.025 mm deflection and a 6 mm global target is
    // the fixture P17-ASSEMBLY-001 settled on: a curved wall is the only one
    // that responds to a finer surface control.
    //
    // NOT A PERFORMANCE TEST. No wall-clock bound is asserted and no timing is
    // a gate.
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

    const GlobalStructuralSystem system =
        assemble(reference, spreadLoad(reference, top, 2, 10000.0));
    const ConstraintSet fixed = fixedSupportOn(reference, bottom);
    Result<SolvedSystem> solved = structural::solveStructuralSystem(system, fixed);
    INFO((solved.has_value() ? std::string{} : solved.error().message));
    REQUIRE(solved.has_value());

    double largest = 0.0;
    for (const double value : solved->values()) {
        REQUIRE(std::isfinite(value));
        largest = std::max(largest, std::abs(value));
    }

    const double denseMiB = static_cast<double>(solved->freeEquations()) *
                            static_cast<double>(solved->freeEquations()) * 8.0 / 1.048576e6;
    const double sparseMiB =
        static_cast<double>(solved->freeSystemNonZeros()) * 16.0 / 1.048576e6;
    WARN("RM-MESH-02 large | " << volume.mesh().nodeCount() << " nodes | "
                               << volume.mesh().tetrahedra().size() << " tets | Ndof "
                               << solved->degreesOfFreedom() << " | Nfree "
                               << solved->freeEquations() << " | nnz(Kff) "
                               << solved->freeSystemNonZeros() << " | pivot ratio "
                               << solved->pivotRatio() << " | ||r||2 "
                               << solved->residual().euclideanNorm.si() << " N | normalized "
                               << solved->residual().normalized << " | largest |u| " << largest
                               << " m | Kff sparse " << sparseMiB
                               << " MiB against a dense " << denseMiB << " MiB");

    SECTION("the fixture is large enough for the claim to mean anything") {
        REQUIRE(volume.mesh().tetrahedra().size() > 1000);
        REQUIRE(solved->freeEquations() > 1000);
    }

    SECTION("and it solves, with the residual and the pivot ratio both clear") {
        CHECK(solved->residual().normalized <= solved->settings().relativeResidualTolerance);
        CHECK(solved->pivotRatio() > solved->settings().pivotFloor);
        CHECK(largest > 0.0);
        CHECK(largest < 1.0e-3);
        CHECK(solved->strainEnergy().si() > 0.0);
    }

    SECTION("and the free system stayed sparse") {
        // Entries per row is a connectivity property, so it does not grow with
        // the mesh -- whereas a dense matrix's entries per row IS Nfree.
        const double perRow = static_cast<double>(solved->freeSystemNonZeros()) /
                              static_cast<double>(solved->freeEquations());
        INFO("entries per row " << perRow << " against Nfree " << solved->freeEquations());
        CHECK(perRow < static_cast<double>(solved->freeEquations()) / 10.0);
        CHECK(sparseMiB < denseMiB);
    }
}
