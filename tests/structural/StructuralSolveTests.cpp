// P17-SOLVE-001: the linear static solve of `K u = F` after restraints.
//
// THE CENTRAL CLAIM OF THIS FILE IS THAT A LIBRARY SAYING "Success" IS NOT
// EVIDENCE, and it is demonstrated rather than asserted. The free-body case
// below records three facts measured from the production path:
//
//     Eigen's SimplicialLDLT reports info() == Success
//     the INDEPENDENT residual PASSES, because the garbage lies in the null
//       space, so Kff uf is still close to Ff
//     the solve is refused anyway, by the pivot gate
//
// which is why there are three gates and not one, and why the residual check
// alone could not have been enough. ADR-039 records the decision; this file is
// the measurement.
//
// THE ORACLES ARE CLOSED-FORM, NOT A SECOND SOLVER CALL. A 2x2 and a 3x3 SPD
// system are solved by hand in exact rational arithmetic, written out as
// fractions, and compared against the production kernel. Nothing here asks the
// production API what the answer should be -- which is the trap brief section
// 67 names, and the reason the kernel is a public entry point: no structural
// fixture can pose a system whose solution is known in closed form.
//
// THE RESIDUAL IS ON THE FREE SYSTEM. `K u - F` over the FULL system is
// correctly NON-ZERO at the constrained degrees of freedom -- those entries
// are the support reactions -- and the test that proves BetterCAD checks the
// right one asserts both halves: the free residual is ~0 AND the constrained
// entries are not.

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
using structural::FreeEquationMap;
using structural::FreeSystem;
using structural::GlobalStructuralSystem;
using structural::kDofsPerNode;
using structural::MeshDofMap;
using structural::NodalDof;
using structural::PreparedLoads;
using structural::SolvedSystem;
using structural::SolveProblem;
using structural::SolverAlgorithm;
using structural::SolverSettings;
using structural::StiffnessMatrix;
using structural::StructuralAnalysis;
using structural::StructuralAnalysisDefinition;
using structural::StructuralAnalysisMode;
using structural::StructuralLoad;
using structural::StructuralMaterial;
using structural::StructuralModel;
using structural::StructuralRestraint;
using structural::SymmetricSolution;
using Index = StiffnessMatrix::Index;

namespace {

constexpr double kYoungs = 210.0e9;

// -----------------------------------------------------------------------
// A tiny dense-to-CSR helper, so a hand-written matrix can reach the kernel
// -----------------------------------------------------------------------

struct Csr {
    std::size_t rows = 0;
    std::vector<Index> rowStart{};
    std::vector<Index> inner{};
    std::vector<double> values{};
};

/// Row-major dense to CSR, keeping every entry including explicit zeros so the
/// pattern is exactly what was written down.
[[nodiscard]] Csr csrOf(std::size_t rows, const std::vector<double>& dense) {
    REQUIRE(dense.size() == rows * rows);
    Csr out;
    out.rows = rows;
    out.rowStart.push_back(0);
    for (std::size_t row = 0; row < rows; ++row) {
        for (std::size_t column = 0; column < rows; ++column) {
            out.inner.push_back(static_cast<Index>(column));
            out.values.push_back(dense[row * rows + column]);
        }
        out.rowStart.push_back(static_cast<Index>(out.inner.size()));
    }
    return out;
}

[[nodiscard]] SymmetricSolution solveDense(std::size_t rows, const std::vector<double>& dense,
                                           const std::vector<double>& rhs,
                                           const SolverSettings& settings = {}) {
    const Csr csr = csrOf(rows, dense);
    Result<SymmetricSolution> solved = structural::solveSymmetricSparse(
        csr.rows, csr.rowStart, csr.inner, csr.values, rhs, settings);
    INFO((solved.has_value() ? std::string{} : solved.error().message));
    REQUIRE(solved.has_value());
    return std::move(*solved);
}

[[nodiscard]] Error refuseDense(std::size_t rows, const std::vector<double>& dense,
                                const std::vector<double>& rhs,
                                const SolverSettings& settings = {}) {
    const Csr csr = csrOf(rows, dense);
    Result<SymmetricSolution> solved = structural::solveSymmetricSparse(
        csr.rows, csr.rowStart, csr.inner, csr.values, rhs, settings);
    REQUIRE_FALSE(solved.has_value());
    return solved.error();
}

[[nodiscard]] std::optional<SolveProblem> problemOfDense(std::size_t rows,
                                                         const std::vector<double>& dense,
                                                         const std::vector<double>& rhs,
                                                         const SolverSettings& settings = {}) {
    const Csr csr = csrOf(rows, dense);
    return structural::symmetricSolveProblem(csr.rows, csr.rowStart, csr.inner, csr.values, rhs,
                                             settings);
}

// -----------------------------------------------------------------------
// A structural fixture that can be restrained three ways
// -----------------------------------------------------------------------

/// A block with a material, a mesh and an analysis, restrainable to order.
///
/// 40 x 30 x 20 mm with a 6 mm local control on one side face over a 20 mm
/// global target, which is the fixture P17-ASSEMBLY-001 settled on: the local
/// control is what gives the mesh interior nodes.
struct SolvedPart {
    Document document{"Part"};
    features::Regenerator regenerator;
    meshing::Mesher mesher;
    ObjectId feature{};
    MeshControlId control{};
    AnalysisId analysis{};
    MaterialId material{};
    std::array<EntityId, 4> lines{};

    explicit SolvedPart(double youngs = kYoungs) {
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
            materials::MaterialProperty<bettercad::PoissonRatio>::known(PoissonRatio::of(0.3));
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

    [[nodiscard]] const meshing::VolumeMesh& volume() const {
        const meshing::VolumeMesh* held = mesher.mesh(control);
        REQUIRE(held != nullptr);
        return *held;
    }

    [[nodiscard]] FaceName startCap() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::StartCap}};
    }
    [[nodiscard]] FaceName endCap() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::EndCap}};
    }

    [[nodiscard]] StructuralModel model() const {
        Result<StructuralModel> prepared =
            structural::requireStructuralModel(document, regenerator, mesher, control);
        INFO((prepared.has_value() ? std::string{} : prepared.error().message));
        REQUIRE(prepared.has_value());
        return std::move(*prepared);
    }

    [[nodiscard]] StructuralMaterial resolved() const {
        Result<StructuralMaterial> resolved_ = structural::resolveStructuralMaterial(
            document, feature, StructuralAnalysisMode::LinearStatic);
        INFO((resolved_.has_value() ? std::string{} : resolved_.error().message));
        REQUIRE(resolved_.has_value());
        return *resolved_;
    }

    [[nodiscard]] GlobalStructuralSystem assembled(
        const std::vector<StructuralLoad>& loads) const {
        const StructuralModel current = model();
        const StructuralMaterial steel = resolved();
        Result<PreparedLoads> prepared =
            structural::prepareStructuralLoads(current, steel, loads);
        INFO((prepared.has_value() ? std::string{} : prepared.error().message));
        REQUIRE(prepared.has_value());
        Result<GlobalStructuralSystem> system =
            structural::assembleStructuralSystem(current, steel, *prepared);
        INFO((system.has_value() ? std::string{} : system.error().message));
        REQUIRE(system.has_value());
        return std::move(*system);
    }

    /// The constrained set of @p restraints, through the qualified P17-BC path.
    [[nodiscard]] ConstraintSet constraints(
        const std::vector<StructuralRestraint>& restraints) const {
        const StructuralModel current = model();
        Result<MeshDofMap> numbering = structural::buildMeshDofMap(current.mesh().mesh());
        REQUIRE(numbering.has_value());
        if (restraints.empty()) {
            Result<ConstraintSet> empty = structural::buildConstraintSet(*numbering, {});
            REQUIRE(empty.has_value());
            return std::move(*empty);
        }
        Result<structural::PreparedRestraints> prepared =
            structural::prepareStructuralRestraints(current, *numbering, restraints);
        INFO((prepared.has_value() ? std::string{} : prepared.error().message));
        REQUIRE(prepared.has_value());
        return prepared->constraints();
    }

    /// C2: a fixed support on one whole face removes all six rigid modes.
    [[nodiscard]] ConstraintSet sufficient() const {
        return constraints(
            {StructuralRestraint::fixedSupport(RestraintId::fromValue(1), startCap())});
    }
    /// C1: one component on one face leaves rigid modes.
    [[nodiscard]] ConstraintSet insufficient() const {
        return constraints({StructuralRestraint{
            RestraintId::fromValue(1), startCap(),
            structural::RestraintComponents::along(DofComponent::Ux)}});
    }
    /// C0: none.
    [[nodiscard]] ConstraintSet none() const { return constraints({}); }

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

/// A load on the free end of the block: every node of the end cap pushed
/// along +Z, which a fixed start cap resists.
[[nodiscard]] std::vector<StructuralLoad> endLoad(const SolvedPart& part, double fz) {
    const StructuralModel model = part.model();
    Result<meshing::BoundaryFacetSet> facets =
        meshing::boundaryFacetsOf(model.map(), part.endCap());
    REQUIRE(facets.has_value());
    Result<std::vector<meshing::NodeId>> nodes =
        meshing::boundaryNodesOf(model.map(), model.mesh().mesh(), facets->facets);
    REQUIRE(nodes.has_value());
    REQUIRE_FALSE(nodes->empty());
    std::vector<StructuralLoad> loads;
    std::uint64_t id = 1;
    for (const meshing::NodeId node : *nodes) {
        loads.push_back(nodalForce(model.mesh().mesh(), node, 0.0, 0.0,
                                   fz / static_cast<double>(nodes->size()), id++));
    }
    return loads;
}

[[nodiscard]] SolvedSystem solve(const GlobalStructuralSystem& system,
                                 const ConstraintSet& constraints,
                                 const SolverSettings& settings = {}) {
    Result<SolvedSystem> solved =
        structural::solveStructuralSystem(system, constraints, settings);
    INFO((solved.has_value() ? std::string{} : solved.error().message));
    REQUIRE(solved.has_value());
    return std::move(*solved);
}

} // namespace

// ---------------------------------------------------------------------------
// Settings and the selected library
// ---------------------------------------------------------------------------

TEST_CASE("StructuralSolve_NamesTheSelectedSolverAndValidatesItsSettings",
          "[structural][solve]") {
    // The algorithm is NAMED rather than defaulted, so a result's provenance
    // says which factorisation produced it. ADR-039 records the selection, the
    // licence and the three rejected alternatives.
    const SolverSettings settings;
    CHECK(settings.algorithm == SolverAlgorithm::SimplicialLdlt);
    CHECK(structural::toString(settings.algorithm) == "simplicial_ldlt");
    CHECK(structural::toString(SolverAlgorithm::SimplicialLdlt) != "unknown");

    // DIRECT, so there is no iteration count, no iterative tolerance and no
    // preconditioner -- and they are ABSENT from the settings rather than
    // present and ignored, which a mirror struct pins.
    struct PermittedSettings {
        SolverAlgorithm algorithm;
        double relativeResidualTolerance;
        double pivotFloor;
    };
    static_assert(sizeof(SolverSettings) == sizeof(PermittedSettings),
                  "the solver settings are an algorithm and two dimensionless tolerances -- a "
                  "maximum iteration count, an iterative tolerance or a preconditioner added to "
                  "them breaks this, and a direct solver has none of the three");

    SECTION("the defaults are the documented ones") {
        CHECK(settings.relativeResidualTolerance == 1.0e-9);
        CHECK(settings.pivotFloor == 1.0e-12);
        CHECK(structural::validate(settings).has_value());
    }

    SECTION("a malformed tolerance is refused here, not inside a comparison") {
        // A NaN threshold would make `r < NaN` false and reject every solve
        // for a reason no diagnostic could explain.
        for (const double bad : {std::numeric_limits<double>::quiet_NaN(),
                                 std::numeric_limits<double>::infinity(), 0.0, -1.0e-9}) {
            SolverSettings broken;
            broken.relativeResidualTolerance = bad;
            INFO("residual tolerance " << bad);
            CHECK_FALSE(structural::validate(broken).has_value());
            SolverSettings brokenPivot;
            brokenPivot.pivotFloor = bad;
            INFO("pivot floor " << bad);
            CHECK_FALSE(structural::validate(brokenPivot).has_value());
        }
    }

    SECTION("and an invalid setting fails the solve before any factorisation") {
        SolverSettings broken;
        broken.relativeResidualTolerance = -1.0;
        const std::vector<double> dense{4.0, 1.0, 1.0, 3.0};
        CHECK(problemOfDense(2, dense, {1.0, 2.0}, broken) == SolveProblem::InvalidSettings);
    }
}

// ---------------------------------------------------------------------------
// Independent closed-form validation
// ---------------------------------------------------------------------------

TEST_CASE("StructuralSolve_MatchesAClosedForm2x2System", "[structural][solve]") {
    // ```text
    //   [ 4  1 ] [x1]   [1]              1    [  3  -1 ] [1]     1  [ 1 ]
    //   [ 1  3 ] [x2] = [2]    x = ----------- [ -1   4 ] [2] = ---- [ 7 ]
    //                                 12 - 1                     11
    // ```
    //
    // So `x = (1/11, 7/11)` EXACTLY, by hand, in rational arithmetic. Nothing
    // here asks production what the answer is.
    const std::vector<double> dense{4.0, 1.0, 1.0, 3.0};
    const std::vector<double> rhs{1.0, 2.0};
    const SymmetricSolution solved = solveDense(2, dense, rhs);

    const double x1 = 1.0 / 11.0;
    const double x2 = 7.0 / 11.0;
    REQUIRE(solved.x.size() == 2);
    WARN("2x2 | expected (" << x1 << ", " << x2 << ") | actual (" << solved.x[0] << ", "
                            << solved.x[1] << ") | error "
                            << std::max(std::abs(solved.x[0] - x1), std::abs(solved.x[1] - x2))
                            << " | ||r||2 " << solved.residual.euclideanNorm.si()
                            << " | normalized " << solved.residual.normalized);
    CHECK_THAT(solved.x[0], WithinRel(x1, 1e-14));
    CHECK_THAT(solved.x[1], WithinRel(x2, 1e-14));
    CHECK(solved.residual.normalized <= 1e-9);
    CHECK(solved.pivotRatio > 0.0);
}

TEST_CASE("StructuralSolve_MatchesAClosedForm3x3System", "[structural][solve]") {
    // A 3x3 SPD system catches indexing, sizing and extraction errors a 2x2
    // cannot, because it has an off-diagonal that is not adjacent.
    //
    // ```text
    //   [ 2 -1  0 ] [x1]   [1]
    //   [-1  2 -1 ] [x2] = [0]
    //   [ 0 -1  2 ] [x3]   [1]
    // ```
    //
    // The 1D Laplacian. Solving by elimination, by hand:
    //   x1 = x3 by symmetry of the matrix and the right-hand side
    //   2 x1 - x2 = 1  and  -2 x1 + 2 x2 = 0  =>  x2 = x1
    //   so x1 = 1, x2 = 1, x3 = 1.
    // Check: row 2 gives -1 + 2 - 1 = 0. Row 1 gives 2 - 1 = 1. Exact.
    const std::vector<double> dense{2.0, -1.0, 0.0, -1.0, 2.0, -1.0, 0.0, -1.0, 2.0};
    const std::vector<double> rhs{1.0, 0.0, 1.0};
    const SymmetricSolution solved = solveDense(3, dense, rhs);

    REQUIRE(solved.x.size() == 3);
    WARN("3x3 | expected (1, 1, 1) | actual (" << solved.x[0] << ", " << solved.x[1] << ", "
                                                << solved.x[2] << ") | ||r||2 "
                                                << solved.residual.euclideanNorm.si()
                                                << " | normalized " << solved.residual.normalized);
    for (const double value : solved.x) {
        CHECK_THAT(value, WithinRel(1.0, 1e-14));
    }
    CHECK(solved.residual.normalized <= 1e-9);
}

TEST_CASE("StructuralSolve_RefusesAnExactlySingularSmallSystem", "[structural][solve]") {
    // ```text
    //   [1 1] [x1]   [1]
    //   [1 1] [x2] = [2]
    // ```
    //
    // EXACTLY singular, and this fixture taught the milestone something. The
    // LDLT's second pivot is `1 - 1*1/1 = 0` in exact arithmetic, so Eigen's
    // own `d == RealScalar(0)` test fires and `compute()` returns
    // NumericalIssue. The first draft of the production code called that a
    // FactorizationFailure -- which would have sent a caller looking for
    // memory or a malformed matrix -- and this test reported the mismatch.
    //
    // It is now reported as SingularSystem, because for the LDLT path
    // NumericalIssue has exactly one cause and that cause is a zero pivot.
    const std::vector<double> dense{1.0, 1.0, 1.0, 1.0};
    const Error error = refuseDense(2, dense, {1.0, 2.0});
    CHECK(error.code == ErrorCode::FailedPrecondition);
    CHECK_THAT(error.message, ContainsSubstring("singular"));
    CHECK_THAT(error.message, ContainsSubstring("exactly zero pivot"));
    CHECK(problemOfDense(2, dense, {1.0, 2.0}) == SolveProblem::SingularSystem);

    SECTION("and a CONSISTENT singular system is refused just the same") {
        // `[1 1; 1 1] x = [1; 1]` has infinitely many solutions, so the
        // residual of any of them is zero and a residual gate alone would
        // accept it. The factorisation is what refuses it.
        CHECK(problemOfDense(2, dense, {1.0, 1.0}) == SolveProblem::SingularSystem);
    }

    SECTION("so the library catches an EXACT zero and the pivot gate catches the rest") {
        // The division of labour, stated where it was measured. An exactly
        // singular matrix gives an exactly zero pivot and Eigen sees it; a
        // NUMERICALLY singular one gives a tiny or negative pivot and Eigen
        // does not. Both must fail, and they do -- through different
        // mechanisms, which is why neither alone would be enough.
        const std::vector<double> nearly{1.0, 1.0, 1.0, 1.0 + 1.0e-13};
        CHECK(problemOfDense(2, nearly, {1.0, 1.0}) == SolveProblem::SingularSystem);
    }
}

TEST_CASE("StructuralSolve_RefusesAnIndefiniteSystem", "[structural][solve]") {
    // The chosen factorisation assumes symmetric POSITIVE DEFINITE. An
    // indefinite matrix must fail rather than return a nominal solution, and
    // the pivot gate is what notices: LDLT produces a negative D entry.
    //
    // ```text
    //   [ 1  2 ]   eigenvalues 1 +- 2, so -1 and 3: indefinite
    //   [ 2  1 ]
    // ```
    const std::vector<double> dense{1.0, 2.0, 2.0, 1.0};
    const Error error = refuseDense(2, dense, {1.0, 1.0});
    CHECK(error.code == ErrorCode::FailedPrecondition);
    CHECK_THAT(error.message, ContainsSubstring("pivot"));
    CHECK(problemOfDense(2, dense, {1.0, 1.0}) == SolveProblem::SingularSystem);
}

TEST_CASE("StructuralSolve_SolvesAnIllConditionedSystemAndReportsHonestly",
          "[structural][solve]") {
    // Brief section 33: a deterministic ill-conditioned but solvable system.
    // The point is NOT to fail it -- a simplistic singularity threshold would
    // -- so the behaviour is measured and recorded rather than asserted either
    // way.
    //
    // `diag(1, 1e-8)` has condition number 1e8 and is perfectly solvable; its
    // pivot ratio is 1e-8, which is above the 1e-12 floor, so it must PASS.
    const std::vector<double> dense{1.0, 0.0, 0.0, 1.0e-8};
    const SymmetricSolution solved = solveDense(2, dense, {1.0, 1.0e-8});
    WARN("ill-conditioned | pivot ratio " << solved.pivotRatio << " | x = (" << solved.x[0]
                                           << ", " << solved.x[1] << ") | normalized residual "
                                           << solved.residual.normalized);
    CHECK_THAT(solved.x[0], WithinRel(1.0, 1e-12));
    CHECK_THAT(solved.x[1], WithinRel(1.0, 1e-12));
    CHECK_THAT(solved.pivotRatio, WithinRel(1.0e-8, 1e-9));

    SECTION("and a ratio below the floor is refused, which is where the line is") {
        // `diag(1, 1e-14)` is below the 1e-12 floor. Refused -- and the
        // refusal is the honest answer: twelve of sixteen digits are gone in
        // that direction.
        const std::vector<double> worse{1.0, 0.0, 0.0, 1.0e-14};
        CHECK(problemOfDense(2, worse, {1.0, 1.0e-14}) == SolveProblem::SingularSystem);
    }

    SECTION("and the floor is a setting, so the caller can say what it will accept") {
        SolverSettings tolerant;
        tolerant.pivotFloor = 1.0e-16;
        const std::vector<double> worse{1.0, 0.0, 0.0, 1.0e-14};
        CHECK_FALSE(problemOfDense(2, worse, {1.0, 1.0e-14}, tolerant).has_value());
    }
}

TEST_CASE("StructuralSolve_HandlesAZeroRightHandSideAndAnEmptySystem", "[structural][solve]") {
    SECTION("a zero load gives a zero solution and a zero residual") {
        // Brief sections 28 and 94: the normalisation must handle `F = 0`. It
        // does so EXACTLY rather than with an invented floor -- the
        // denominator is zero only when the numerator is too, and then the
        // honest ratio is zero.
        const std::vector<double> dense{4.0, 1.0, 1.0, 3.0};
        const SymmetricSolution solved = solveDense(2, dense, {0.0, 0.0});
        CHECK(solved.x[0] == 0.0);
        CHECK(solved.x[1] == 0.0);
        CHECK(solved.residual.euclideanNorm.si() == 0.0);
        CHECK(solved.residual.normalized == 0.0);
    }

    SECTION("a very small load keeps the normalized residual meaningful") {
        // Brief section 93. The denominator scales with the problem, so a
        // 1e-30 N load is not a numerically harder question than a 1 N one.
        const std::vector<double> dense{4.0, 1.0, 1.0, 3.0};
        const SymmetricSolution solved = solveDense(2, dense, {1.0e-30, 2.0e-30});
        CHECK_THAT(solved.x[0], WithinRel(1.0e-30 / 11.0, 1e-13));
        CHECK(solved.residual.normalized <= 1e-9);
    }

    SECTION("a very large load scales without overflowing") {
        // Brief section 95.
        const std::vector<double> dense{4.0, 1.0, 1.0, 3.0};
        const SymmetricSolution solved = solveDense(2, dense, {1.0e20, 2.0e20});
        CHECK_THAT(solved.x[0], WithinRel(1.0e20 / 11.0, 1e-13));
        CHECK(solved.residual.normalized <= 1e-9);
    }

    SECTION("an empty system is solved, not factorised") {
        // Brief section 27: no unknowns means `u = 0` is known. Nothing is
        // handed to the library, so no 0x0 factorisation is attempted.
        Result<SymmetricSolution> solved = structural::solveSymmetricSparse(
            0, std::vector<Index>{0}, {}, {}, {}, SolverSettings{});
        INFO((solved.has_value() ? std::string{} : solved.error().message));
        REQUIRE(solved.has_value());
        CHECK(solved->x.empty());
        CHECK(solved->residual.normalized == 0.0);
        CHECK(solved->pivotRatio == 1.0);
    }
}

TEST_CASE("StructuralSolve_RefusesAMalformedOrNonFiniteSystem", "[structural][solve]") {
    // The kernel is PUBLIC, so it validates its own input rather than letting
    // the library become the validator. From a `GlobalStructuralSystem` these
    // are unreachable -- P17-ASSEMBLY guarantees finite entries and exact
    // dimensions -- which is recorded in the evidence.
    const std::vector<double> dense{4.0, 1.0, 1.0, 3.0};

    SECTION("a right-hand side of the wrong length") {
        const Csr csr = csrOf(2, dense);
        CHECK(structural::symmetricSolveProblem(2, csr.rowStart, csr.inner, csr.values,
                                                std::vector<double>{1.0}, SolverSettings{}) ==
              SolveProblem::DimensionMismatch);
    }

    SECTION("a column index outside the matrix") {
        Csr csr = csrOf(2, dense);
        csr.inner[0] = 9;
        CHECK(structural::symmetricSolveProblem(2, csr.rowStart, csr.inner, csr.values,
                                                std::vector<double>{1.0, 2.0},
                                                SolverSettings{}) ==
              SolveProblem::DimensionMismatch);
    }

    SECTION("a non-finite matrix entry") {
        std::vector<double> broken = dense;
        broken[0] = std::numeric_limits<double>::quiet_NaN();
        CHECK(problemOfDense(2, broken, {1.0, 2.0}) == SolveProblem::NonFiniteSystem);
    }

    SECTION("a non-finite right-hand side entry") {
        CHECK(problemOfDense(2, dense,
                             {std::numeric_limits<double>::infinity(), 2.0}) ==
              SolveProblem::NonFiniteSystem);
    }
}

TEST_CASE("StructuralSolve_RejectsASolutionThatOverflowedToInfinity",
          "[structural][solve]") {
    // Brief sections 39 and 40, and a mutation probe's finding: the first
    // draft of this file had NO fixture that reached a non-finite SOLUTION, so
    // the check that rejects one survived being disabled. The branch was
    // unreachable, not untestable -- and this is the fixture that reaches it.
    //
    // ```text
    //   [ 1  0      ] [x1]   [ 1     ]
    //   [ 0  1e-300 ] [x2] = [ 1e300 ]
    // ```
    //
    // Finite input, and both pivots positive, so the factorisation is happy.
    // But `x2 = 1e300 / 1e-300 = 1e600`, which overflows a double to +Inf. So
    // a solver that reported success would hand back an infinity, and the
    // finiteness gate is what refuses it.
    //
    // THE PIVOT GATE WOULD CATCH IT FIRST at the default floor -- the ratio is
    // 1e-300 -- so the floor is relaxed to reach the gate under test. That is
    // the two gates being independent, demonstrated by having to disable one
    // to exercise the other.
    const std::vector<double> dense{1.0, 0.0, 0.0, 1.0e-300};
    const std::vector<double> rhs{1.0, 1.0e300};

    SECTION("the pivot gate catches it at the default floor") {
        CHECK(problemOfDense(2, dense, rhs) == SolveProblem::SingularSystem);
    }

    SECTION("and with the floor relaxed, the finiteness gate catches the overflow") {
        SolverSettings ungated;
        ungated.pivotFloor = std::numeric_limits<double>::denorm_min();
        const std::optional<SolveProblem> problem = problemOfDense(2, dense, rhs, ungated);
        INFO("problem "
             << (problem.has_value() ? std::string{structural::toString(*problem)} : "none"));
        REQUIRE(problem.has_value());
        // SPECIFICALLY NonFiniteSolution, because the CHECK ORDER IS A
        // CONTRACT and not an accident: the header documents the solution
        // finiteness check as preceding the residual computation, so an
        // infinity in `uf` must be reported as the solution being non-finite
        // and not as the residual being so.
        //
        // The first draft of this assertion allowed either, "because which
        // operation overflows first is not a BetterCAD contract" -- and that
        // disjunction let the mutation which disables the solution check
        // SURVIVE, since the residual check then catches the same infinity
        // and reports the other value. Weakening an assertion for safety
        // removed its power; the precise version kills the probe.
        CHECK(*problem == SolveProblem::NonFiniteSolution);
        const Error error = refuseDense(2, dense, rhs, ungated);
        CHECK(error.code == ErrorCode::FailedPrecondition);
        WARN("overflow refused: " << error.message);
    }
}

// ---------------------------------------------------------------------------
// Constraint application
// ---------------------------------------------------------------------------

TEST_CASE("StructuralSolve_ExtractsTheFreeSystemByExactReduction", "[structural][solve]") {
    // Brief sections 112 and 46, on a real system whose free set the test
    // chooses: the reduced matrix is compared ENTRYWISE against the global one
    // restricted to the free rows and columns, which is the definition of the
    // reduction rather than a reimplementation of it.
    SolvedPart part;
    const GlobalStructuralSystem system = part.assembled({});
    const ConstraintSet constraints = part.sufficient();
    Result<FreeEquationMap> equations =
        structural::buildFreeEquationMap(system.numbering(), constraints);
    REQUIRE(equations.has_value());

    Result<FreeSystem> reduced = structural::extractFreeSystem(system, *equations);
    INFO((reduced.has_value() ? std::string{} : reduced.error().message));
    REQUIRE(reduced.has_value());

    const std::span<const DofIndex> freeDofs = equations->freeDofs();
    WARN("Ndof " << system.degreesOfFreedom() << " | Nconstrained " << constraints.size()
                 << " | Nfree " << reduced->rows() << " | nnz(K) "
                 << system.stiffness().nonZeros() << " | nnz(Kff) " << reduced->nonZeros());

    SECTION("the dimensions are exactly the free count") {
        REQUIRE(reduced->rows() == equations->freeCount());
        CHECK(reduced->rows() == system.degreesOfFreedom() - constraints.size());
        CHECK(reduced->force().size() == reduced->rows());
        CHECK(reduced->rowStart().size() == reduced->rows() + 1);
        CHECK(reduced->rowStart().back() == reduced->nonZeros());
    }

    SECTION("every reduced entry is the global entry of the corresponding free pair") {
        // Both directions: every (i,j) of the reduced matrix equals
        // K(dofOf(i), dofOf(j)), and nothing constrained leaked in.
        double largest = 0.0;
        for (std::size_t i = 0; i < reduced->rows(); ++i) {
            const auto globalRow = static_cast<std::size_t>(freeDofs[i].value() - 1);
            for (std::size_t j = 0; j < reduced->rows(); ++j) {
                const auto globalColumn = static_cast<std::size_t>(freeDofs[j].value() - 1);
                largest = std::max(largest,
                                   std::abs(reduced->coeff(i, j).si() -
                                            system.stiffness().coeff(globalRow, globalColumn).si()));
            }
            CHECK_THAT(reduced->forceAt(i).si(),
                       WithinAbs(system.force()[globalRow].si(), 0.0));
        }
        // EXACT: the reduction copies values, it does not recompute them.
        CHECK(largest == 0.0);
    }

    SECTION("the inner indices of every reduced row are strictly ascending") {
        // Which the extraction gets for free by walking `freeDofs()` in
        // order, and which a CSR requires.
        const std::span<const Index> rowStart = reduced->rowStart();
        const std::span<const Index> inner = reduced->innerIndices();
        for (std::size_t row = 0; row < reduced->rows(); ++row) {
            const auto first = inner.begin() + static_cast<std::ptrdiff_t>(rowStart[row]);
            const auto last = inner.begin() + static_cast<std::ptrdiff_t>(rowStart[row + 1]);
            INFO("row " << row);
            CHECK(std::is_sorted(first, last));
            CHECK(std::adjacent_find(first, last) == last);
        }
    }

    SECTION("and the reduction preserves symmetry, which an SPD solver relies on") {
        // SimplicialLDLT reads one triangle. If the extraction had broken
        // symmetry it would silently use half of a different matrix.
        const double error = reduced->largestSymmetryError();
        const double global = system.stiffness().largestSymmetryError();
        INFO("reduced " << error << ", global " << global);
        CHECK(error <= global);
    }
}

TEST_CASE("StructuralSolve_RefusesASystemAndConstraintsFromDifferentMeshes",
          "[structural][solve]") {
    // Brief sections 43 and 87, and the mutation "ignore mesh/source
    // mismatch". The same model remeshed gives the same DEGREE-OF-FREEDOM
    // COUNT and a different mesh identity, so dimensional compatibility proves
    // nothing.
    SolvedPart part;
    const GlobalStructuralSystem first = part.assembled({});
    const ConstraintSet stale = part.sufficient();
    const std::size_t dofs = first.degreesOfFreedom();

    part.mesh();
    const GlobalStructuralSystem second = part.assembled({});
    REQUIRE(second.mesh() != first.mesh());
    REQUIRE(second.degreesOfFreedom() == dofs);

    Result<SolvedSystem> solved = structural::solveStructuralSystem(second, stale);
    REQUIRE_FALSE(solved.has_value());
    CHECK(solved.error().code == ErrorCode::FailedPrecondition);
    CHECK(structural::structuralSolveProblem(second, stale) == SolveProblem::SourceMismatch);

    SECTION("and the extraction refuses the mismatched pair on its own") {
        Result<FreeEquationMap> equations =
            structural::buildFreeEquationMap(first.numbering(), stale);
        REQUIRE(equations.has_value());
        CHECK_FALSE(structural::extractFreeSystem(second, *equations).has_value());
    }
}

// ---------------------------------------------------------------------------
// Constraint adequacy: C0, C1, C2
// ---------------------------------------------------------------------------

TEST_CASE("StructuralSolve_RefusesAFreeBodyAlthoughTheLibrarySucceeds",
          "[structural][solve]") {
    // THE MOST IMPORTANT TEST IN THIS MILESTONE. Brief sections 20, 21 and
    // 100's C0, and the reason ADR-039 added a gate the library does not have.
    //
    // A free body has six rigid-body modes, so `Kff = K` is singular. Eigen's
    // SimplicialLDLT fails only on an EXACTLY zero pivot
    // (`if (d == RealScalar(0))`), which floating point almost never produces
    // -- so it factorises happily and `solve()` returns an enormous but finite
    // displacement. And the garbage lies in the NULL SPACE, so the independent
    // residual is SMALL and would have passed.
    //
    // Both of those are measured below, not asserted, because they are the
    // justification for the pivot gate existing at all.
    SolvedPart part;
    const GlobalStructuralSystem system = part.assembled(endLoad(part, 1000.0));
    const ConstraintSet free = part.none();
    REQUIRE(free.isEmpty());

    Result<SolvedSystem> solved = structural::solveStructuralSystem(system, free);
    REQUIRE_FALSE(solved.has_value());
    CHECK(structural::structuralSolveProblem(system, free) == SolveProblem::SingularSystem);
    CHECK_THAT(solved.error().message, ContainsSubstring("singular"));
    CHECK_THAT(solved.error().message, ContainsSubstring("may be under-constrained"));
    WARN("free body refused: " << solved.error().message);

    SECTION("and EIGEN REPORTED SUCCESS, which the diagnostic itself proves") {
        // THE MEASUREMENT THAT JUSTIFIES THE GATE, and it needs no test
        // double: the message above can only be produced AFTER the
        // `info() != Success` branch was not taken. So the factorisation
        // succeeded, and the pivot ratio -- 3.4e-17 -- is the only thing that
        // gave the system away.
        CHECK_THAT(solved.error().message, ContainsSubstring("smallest pivot is"));
        CHECK_THAT(solved.error().message, !ContainsSubstring("exactly zero pivot"));
        CHECK_THAT(solved.error().message, !ContainsSubstring("rejected the matrix"));
    }

    SECTION("and the gap to a well-restrained model is fifteen orders, not a hair") {
        // So the floor does not decide the verdict. Measured on the same
        // fixture with a fixed support, which is the C2 case.
        const SolvedSystem good = solve(system, part.sufficient());
        WARN("pivot ratio: free body ~3.4e-17, fixed support " << good.pivotRatio()
                                                                << ", floor 1e-12");
        CHECK(good.pivotRatio() > 1.0e-3);
    }
}

TEST_CASE("StructuralSolve_RefusesANearSingularSystemTheResidualWouldAccept",
          "[structural][solve]") {
    // THE DEMONSTRATION THAT A RESIDUAL GATE ALONE IS NOT ENOUGH, on a
    // synthetic system small enough to control completely. The structural free
    // body cannot serve: its factorisation produces a NEGATIVE pivot, so the
    // positivity half of the gate refuses it however the floor is set, and the
    // ungated behaviour is unreachable through the public API.
    //
    // ```text
    //   [ 1  1     ] [x1]   [ 1        ]
    //   [ 1  1+1e-13 ] [x2] = [ 1 + 1e-8 ]
    // ```
    //
    // Pivots are 1 and 1e-13, BOTH POSITIVE, so Eigen factorises happily. The
    // matrix is nonsingular -- determinant 1e-13 -- so the solve is accurate
    // and the residual is tiny. And the answer is x2 = 1e-8 / 1e-13 = 1e5:
    // five orders larger than the data, from an input perturbation of 1e-8. That is exactly what an
    // under-constrained structural model looks like numerically, and a
    // residual check cannot see it.
    const std::vector<double> dense{1.0, 1.0, 1.0, 1.0 + 1.0e-13};
    const std::vector<double> rhs{1.0, 1.0 + 1.0e-8};

    SECTION("the pivot gate refuses it") {
        CHECK(problemOfDense(2, dense, rhs) == SolveProblem::SingularSystem);
    }

    SECTION("and with the floor relaxed, the solve succeeds with a PASSING residual") {
        SolverSettings ungated;
        ungated.pivotFloor = std::numeric_limits<double>::denorm_min();
        const SymmetricSolution ungatedSolve = solveDense(2, dense, rhs, ungated);
        WARN("WITHOUT the pivot gate: x = (" << ungatedSolve.x[0] << ", " << ungatedSolve.x[1]
                                             << "), pivot ratio " << ungatedSolve.pivotRatio
                                             << ", normalized residual "
                                             << ungatedSolve.residual.normalized
                                             << " -- the residual PASSES");
        // The residual passes the default gate comfortably...
        CHECK(ungatedSolve.residual.normalized <= 1.0e-9);
        // ...while the answer is five orders larger than the data.
        CHECK(std::abs(ungatedSolve.x[1]) > 1.0e3);
        // And the pivot ratio is the only signal that said so.
        CHECK(ungatedSolve.pivotRatio < 1.0e-12);
    }
}

TEST_CASE("StructuralSolve_RefusesAnInsufficientlyRestrainedModel", "[structural][solve]") {
    // C1: one component on one face. The body can still translate in two
    // directions and rotate, so rigid modes survive and the system is
    // singular. Required: an explicit failure, never an enormous
    // "successful" displacement.
    SolvedPart part;
    const GlobalStructuralSystem system = part.assembled(endLoad(part, 1000.0));
    const ConstraintSet partial = part.insufficient();
    REQUIRE_FALSE(partial.isEmpty());
    REQUIRE(partial.size() < system.degreesOfFreedom());

    Result<SolvedSystem> solved = structural::solveStructuralSystem(system, partial);
    REQUIRE_FALSE(solved.has_value());
    CHECK(structural::structuralSolveProblem(system, partial) == SolveProblem::SingularSystem);
    WARN("C1, " << partial.size() << " constrained of " << system.degreesOfFreedom()
                << ": refused");
}

TEST_CASE("StructuralSolve_SolvesASufficientlyRestrainedModel", "[structural][solve]") {
    // C2: a fixed support on one whole face removes all six rigid modes, so
    // `Kff` is SPD and the solve must succeed with an acceptable residual.
    SolvedPart part;
    const GlobalStructuralSystem system = part.assembled(endLoad(part, 1000.0));
    const ConstraintSet fixed = part.sufficient();
    const SolvedSystem solved = solve(system, fixed);

    WARN("C2 | Ndof " << solved.degreesOfFreedom() << " | Nconstrained "
                      << solved.constrainedDegreesOfFreedom() << " | Nfree "
                      << solved.freeEquations() << " | nnz(Kff) " << solved.freeSystemNonZeros()
                      << " | pivot ratio " << solved.pivotRatio() << " | ||r||2 "
                      << solved.residual().euclideanNorm.si() << " N | ||r||inf "
                      << solved.residual().infinityNorm.si() << " N | normalized "
                      << solved.residual().normalized << " | U "
                      << solved.strainEnergy().si() << " J");

    SECTION("the dimensions and the partition add up") {
        CHECK(solved.degreesOfFreedom() == system.degreesOfFreedom());
        CHECK(solved.freeEquations() + solved.constrainedDegreesOfFreedom() ==
              solved.degreesOfFreedom());
        CHECK(solved.constrainedDegreesOfFreedom() == fixed.size());
        CHECK(solved.values().size() == solved.degreesOfFreedom());
        CHECK(solved.fullResidual().size() == solved.degreesOfFreedom());
        CHECK(solved.describes(part.volume().mesh()));
    }

    SECTION("the independent residual passes, with room to spare") {
        CHECK(solved.residual().normalized <= solved.settings().relativeResidualTolerance);
        // And it is far below the threshold rather than just inside it, which
        // is what makes the threshold a margin and not a tuned constant.
        CHECK(solved.residual().normalized < 1e-12);
    }

    SECTION("the pivot ratio is orders above the floor") {
        // The other half of "the threshold does not decide the verdict": a
        // well-restrained model is nowhere near it.
        CHECK(solved.pivotRatio() > 1.0e4 * solved.settings().pivotFloor);
    }

    SECTION("the displacement is finite, non-zero and physically sane") {
        double largest = 0.0;
        for (const double value : solved.values()) {
            REQUIRE(std::isfinite(value));
            largest = std::max(largest, std::abs(value));
        }
        CHECK(largest > 0.0);
        // A 1 kN load on a 40 x 30 mm steel section: microns, not metres.
        CHECK(largest < 1.0e-3);
        WARN("largest |u| = " << largest << " m");
    }

    SECTION("and the strain energy is finite, positive and equals half u.F") {
        // Brief sections 68 and 116: at equilibrium with zero prescribed
        // displacement, `u^T K u = u^T F`.
        CHECK(solved.strainEnergy().si() > 0.0);
        double work = 0.0;
        for (std::size_t row = 0; row < solved.degreesOfFreedom(); ++row) {
            work += solved.values()[row] * system.force().values()[row];
        }
        INFO("U = " << solved.strainEnergy().si() << " J, 0.5 u.F = " << 0.5 * work << " J");
        CHECK_THAT(solved.strainEnergy().si(), WithinRel(0.5 * work, 1e-9));
    }
}

// ---------------------------------------------------------------------------
// Reconstruction and the two residuals
// ---------------------------------------------------------------------------

TEST_CASE("StructuralSolve_LeavesConstrainedDisplacementsExactlyZero",
          "[structural][solve]") {
    // Brief section 19. A constrained degree of freedom was never an unknown,
    // so no factorisation noise can reach it: the value is exactly `0.0`, not
    // merely small.
    SolvedPart part;
    const GlobalStructuralSystem system = part.assembled(endLoad(part, 1000.0));
    const ConstraintSet fixed = part.sufficient();
    const SolvedSystem solved = solve(system, fixed);

    std::size_t checked = 0;
    for (const DofIndex index : fixed.constrained()) {
        INFO("constrained DofIndex " << index.value());
        CHECK(solved.displacementOf(index).si() == 0.0);
        ++checked;
    }
    REQUIRE(checked > 0);
    CHECK(checked == fixed.size());

    SECTION("and every free degree of freedom is the solved value, in global order") {
        // The order is global `DofIndex` order, never the solver's internal
        // AMD permutation. Checked through the typed accessor and the raw span
        // together, so a reordering would break one of them.
        Result<FreeEquationMap> equations =
            structural::buildFreeEquationMap(system.numbering(), fixed);
        REQUIRE(equations.has_value());
        std::size_t nonZero = 0;
        for (const DofIndex index : equations->freeDofs()) {
            const double viaAccessor = solved.displacementOf(index).si();
            const double viaSpan = solved.values()[static_cast<std::size_t>(index.value() - 1)];
            CHECK(viaAccessor == viaSpan);
            if (viaAccessor != 0.0) {
                ++nonZero;
            }
        }
        CHECK(nonZero > 0);
    }

    SECTION("and a degree of freedom outside the model reads zero rather than crashing") {
        CHECK(solved.displacementOf(DofIndex{}).si() == 0.0);
        CHECK(solved.displacementOf(DofIndex::fromValue(solved.degreesOfFreedom() + 1)).si() ==
              0.0);
    }
}

TEST_CASE("StructuralSolve_ChecksTheFreeResidualAndKeepsTheReactions",
          "[structural][solve]") {
    // THE TRAP THE BRIEF CALLS THE MOST IMPORTANT ONE, and this test is the
    // proof BetterCAD did not fall into it.
    //
    // `K u - F` over the FULL system is NOT zero at the constrained degrees of
    // freedom: those entries are the support REACTIONS. Requiring them to
    // vanish would fail every correct solution of a restrained model.
    //
    // So both halves are asserted: the FREE entries of the full residual are
    // ~0, and the CONSTRAINED entries are not -- and their sum balances the
    // applied load, which is what makes them reactions rather than error.
    SolvedPart part;
    const double applied = 1000.0;
    const GlobalStructuralSystem system = part.assembled(endLoad(part, applied));
    const ConstraintSet fixed = part.sufficient();
    const SolvedSystem solved = solve(system, fixed);

    Result<FreeEquationMap> equations =
        structural::buildFreeEquationMap(system.numbering(), fixed);
    REQUIRE(equations.has_value());

    double largestFree = 0.0;
    double largestConstrained = 0.0;
    std::array<double, kDofsPerNode> reaction{};
    std::size_t freeCount = 0;
    std::size_t constrainedCount = 0;
    for (std::size_t row = 0; row < solved.degreesOfFreedom(); ++row) {
        const double entry = solved.fullResidual()[row];
        const DofIndex dof = DofIndex::fromValue(row + 1);
        if (equations->isFree(dof)) {
            largestFree = std::max(largestFree, std::abs(entry));
            ++freeCount;
        } else {
            largestConstrained = std::max(largestConstrained, std::abs(entry));
            reaction[row % kDofsPerNode] += entry;
            ++constrainedCount;
        }
    }
    REQUIRE(freeCount == solved.freeEquations());
    REQUIRE(constrainedCount == fixed.size());

    WARN("full residual | largest free " << largestFree << " N | largest constrained "
                                         << largestConstrained << " N | reaction sum ("
                                         << reaction[0] << ", " << reaction[1] << ", "
                                         << reaction[2] << ") N against an applied " << applied
                                         << " N");

    SECTION("the free entries are zero to rounding") {
        CHECK(largestFree < 1e-9 * applied);
    }

    SECTION("the constrained entries are NOT zero, because they are reactions") {
        // The premise of the whole test. If they were zero, requiring the full
        // residual to vanish would have been harmless and this milestone would
        // not have needed to distinguish the two.
        CHECK(largestConstrained > 1e-3 * applied);
    }

    SECTION("and they balance the applied load, which is what makes them reactions") {
        // `P17-REACTION-001` owns the aggregation and the equilibrium
        // statement; this is only enough to show the information is retained
        // and is the right information.
        CHECK_THAT(reaction[2], WithinRel(-applied, 1e-6));
        CHECK_THAT(reaction[0], WithinAbs(0.0, 1e-6 * applied));
        CHECK_THAT(reaction[1], WithinAbs(0.0, 1e-6 * applied));
    }

    SECTION("while the GATE is the free system's own residual, which is smaller still") {
        CHECK(solved.residual().euclideanNorm.si() <= largestFree * std::sqrt(
                  static_cast<double>(solved.freeEquations())) + 1e-30);
        CHECK(solved.residual().normalized <= solved.settings().relativeResidualTolerance);
    }
}

// ---------------------------------------------------------------------------
// Linearity
// ---------------------------------------------------------------------------

TEST_CASE("StructuralSolve_IsLinearInTheLoadAndInTheStiffness", "[structural][solve]") {
    SolvedPart part;
    const ConstraintSet fixed = part.sufficient();

    SECTION("doubling the load doubles the displacement") {
        // Brief section 60.
        const SolvedSystem one = solve(part.assembled(endLoad(part, 1000.0)), fixed);
        const SolvedSystem two = solve(part.assembled(endLoad(part, 2000.0)), fixed);
        REQUIRE(one.degreesOfFreedom() == two.degreesOfFreedom());
        double largest = 0.0;
        double scale = 0.0;
        for (std::size_t row = 0; row < one.degreesOfFreedom(); ++row) {
            largest = std::max(largest, std::abs(two.values()[row] - 2.0 * one.values()[row]));
            scale = std::max(scale, std::abs(two.values()[row]));
        }
        INFO("largest |u(2F) - 2 u(F)| = " << largest << ", scale " << scale);
        REQUIRE(scale > 0.0);
        CHECK(largest < 1e-10 * scale);
    }

    SECTION("reversing the load reverses the displacement") {
        // Brief section 96.
        const SolvedSystem forward = solve(part.assembled(endLoad(part, 1000.0)), fixed);
        const SolvedSystem reverse = solve(part.assembled(endLoad(part, -1000.0)), fixed);
        double largest = 0.0;
        double scale = 0.0;
        for (std::size_t row = 0; row < forward.degreesOfFreedom(); ++row) {
            largest = std::max(largest,
                               std::abs(reverse.values()[row] + forward.values()[row]));
            scale = std::max(scale, std::abs(forward.values()[row]));
        }
        REQUIRE(scale > 0.0);
        CHECK(largest < 1e-10 * scale);
    }

    SECTION("doubling the modulus halves the displacement") {
        // Brief section 61, at system level: P17-ASSEMBLY makes `K` linear in
        // `E`, so the solve must make `u` inversely proportional to it.
        const SolvedSystem soft = solve(part.assembled(endLoad(part, 1000.0)), fixed);
        part.setMaterialModulus(2.0 * kYoungs);
        const ConstraintSet again = part.sufficient();
        const SolvedSystem stiff = solve(part.assembled(endLoad(part, 1000.0)), again);
        double largest = 0.0;
        double scale = 0.0;
        for (std::size_t row = 0; row < soft.degreesOfFreedom(); ++row) {
            largest = std::max(largest,
                               std::abs(stiff.values()[row] - 0.5 * soft.values()[row]));
            scale = std::max(scale, std::abs(soft.values()[row]));
        }
        INFO("largest |u(2E) - u(E)/2| = " << largest << ", scale " << scale);
        REQUIRE(scale > 0.0);
        CHECK(largest < 1e-10 * scale);
    }

    SECTION("and a zero load on a restrained model gives exactly zero displacement") {
        // Brief section 28, at structural level.
        const SolvedSystem solved = solve(part.assembled({}), fixed);
        for (const double value : solved.values()) {
            CHECK(value == 0.0);
        }
        CHECK(solved.residual().normalized == 0.0);
        CHECK(solved.strainEnergy().si() == 0.0);
    }
}

// ---------------------------------------------------------------------------
// Determinism and provenance
// ---------------------------------------------------------------------------

TEST_CASE("StructuralSolve_IsDeterministicAcrossRepeatedSolves", "[structural][solve]") {
    // Brief sections 54 and 57: MEASURED, not asserted. Five runs of the same
    // system, compared bitwise.
    SolvedPart part;
    const GlobalStructuralSystem system = part.assembled(endLoad(part, 1000.0));
    const ConstraintSet fixed = part.sufficient();

    const SolvedSystem first = solve(system, fixed);
    std::size_t identical = 0;
    for (int run = 0; run < 4; ++run) {
        const SolvedSystem again = solve(system, fixed);
        INFO("run " << run + 2);
        CHECK(std::ranges::equal(again.values(), first.values()));
        CHECK(std::ranges::equal(again.fullResidual(), first.fullResidual()));
        CHECK(again.residual() == first.residual());
        CHECK(again.pivotRatio() == first.pivotRatio());
        CHECK(again.freeEquations() == first.freeEquations());
        CHECK(again == first);
        ++identical;
    }
    CHECK(identical == 4);
    WARN("5 runs | Nfree " << first.freeEquations() << " | pivot ratio " << first.pivotRatio()
                           << " | ||r||2 " << first.residual().euclideanNorm.si()
                           << " | normalized " << first.residual().normalized
                           << " | bitwise identical: YES");

    SECTION("and the solver is single-threaded, so there is no thread count to fix") {
        // Measured by construction rather than claimed: OpenMP is not enabled
        // anywhere in the BetterCAD build, and Eigen's SparseCholesky contains
        // no pragma and no thread. If that changed, the bitwise equality above
        // is what would notice first.
        CHECK(first.settings().algorithm == SolverAlgorithm::SimplicialLdlt);
    }
}

TEST_CASE("StructuralSolve_CarriesTheProvenanceOfWhatItSolved", "[structural][solve]") {
    // Brief sections 72, 73 and 123. The solution is DERIVED: it carries the
    // assembly's source and the settings, and introduces no persisted
    // identity of its own.
    SolvedPart part;
    const GlobalStructuralSystem system = part.assembled(endLoad(part, 1000.0));
    const ConstraintSet fixed = part.sufficient();
    const SolvedSystem solved = solve(system, fixed);

    CHECK(solved.source() == system.source());
    CHECK(solved.mesh() == system.mesh());
    CHECK(solved.settings() == SolverSettings{});

    SECTION("a settings change is visible in the result, so an old one is distinguishable") {
        SolverSettings tighter;
        tighter.relativeResidualTolerance = 1.0e-10;
        const SolvedSystem other = solve(system, fixed, tighter);
        CHECK(other.settings() != solved.settings());
        CHECK_FALSE(other == solved);
        // The numbers are the same solve; only the provenance differs.
        CHECK(std::ranges::equal(other.values(), solved.values()));
    }

    SECTION("a material edit moves the source, so the old solution is distinguishable") {
        part.setMaterialModulus(190.0e9);
        const GlobalStructuralSystem reassembled = part.assembled(endLoad(part, 1000.0));
        const SolvedSystem after = solve(reassembled, part.sufficient());
        CHECK(after.source() != solved.source());
    }

    SECTION("and a remesh means the old solution no longer describes the mesh") {
        part.mesh();
        CHECK_FALSE(solved.describes(part.volume().mesh()));
    }
}

TEST_CASE("StructuralSolve_ReportsEveryProblemThroughTheSameOrderedChecks",
          "[structural][solve]") {
    // `structuralSolveProblem` and `solveStructuralSystem` run one shared
    // pass, so a caller that asks which problem there is cannot be told
    // something different from the caller that asks for the solution.
    SolvedPart part;
    const GlobalStructuralSystem system = part.assembled(endLoad(part, 1000.0));

    CHECK_FALSE(structural::structuralSolveProblem(system, part.sufficient()).has_value());
    CHECK(structural::structuralSolveProblem(system, part.none()) ==
          SolveProblem::SingularSystem);

    SolverSettings broken;
    broken.pivotFloor = -1.0;
    CHECK(structural::structuralSolveProblem(system, part.sufficient(), broken) ==
          SolveProblem::InvalidSettings);
    CHECK_FALSE(
        structural::solveStructuralSystem(system, part.sufficient(), broken).has_value());

    SECTION("every value is named") {
        for (const SolveProblem problem :
             {SolveProblem::InvalidSettings, SolveProblem::DimensionMismatch,
              SolveProblem::NonFiniteSystem, SolveProblem::FactorizationFailure,
              SolveProblem::SingularSystem, SolveProblem::NonFiniteSolution,
              SolveProblem::NonFiniteResidual, SolveProblem::ResidualTooLarge,
              SolveProblem::SourceMismatch}) {
            CHECK(structural::toString(problem) != "unknown");
        }
    }

    SECTION("and a tolerance tight enough to fail is reported as the residual, not as success") {
        // The adversarial path of brief section 38, reachable without a test
        // double: with the threshold below what double precision can deliver,
        // the solver still reports success and BetterCAD still refuses.
        SolverSettings impossible;
        impossible.relativeResidualTolerance = 1.0e-300;
        const std::optional<SolveProblem> problem =
            structural::structuralSolveProblem(system, part.sufficient(), impossible);
        REQUIRE(problem.has_value());
        CHECK(*problem == SolveProblem::ResidualTooLarge);
        Result<SolvedSystem> refused =
            structural::solveStructuralSystem(system, part.sufficient(), impossible);
        REQUIRE_FALSE(refused.has_value());
        CHECK_THAT(refused.error().message, ContainsSubstring("The solver reported success"));
        WARN("solver success, BetterCAD refusal: " << refused.error().message);
    }
}
