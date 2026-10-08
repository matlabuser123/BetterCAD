// P17-ASSEMBLY-001 against the meshing reference models.
//
// WHY THESE ARE SEPARATE from tests/structural/StructuralSystemTests.cpp.
// Those tests use one block and an independent dense oracle, which is the
// correctness proof. These use the committed reference models, because five
// claims cannot be made on one fixed fixture:
//
//   the ASSEMBLY TABLE the brief asks for -- nodes, tetrahedra, Ndof, nnz,
//     symmetry and finiteness across models with voids, curvature and local
//     refinement
//   a PRESSURE's prepared nodal field survives assembly, compared against the
//     analytical `p A` of a face whose area is known from the model's own
//     declared dimensions
//   GRAVITY's resultant is `rho V g`, with V the model's analytic volume
//   LOCAL REFINEMENT changes Ndof and nnz and leaves both systems valid, which
//     a single mesh cannot show
//   a LARGE mesh assembles in sparse storage, which is the claim a small one
//     cannot make at all
//
// NO DENSE ORACLE HERE. A dense Ndof x Ndof matrix is a test-only device and
// these meshes are too large for one; the entrywise proof is the unit file's.
// What is checked here is what scales: dimensions, the sparsity bound,
// symmetry over stored entries, finiteness, the rigid-body residuals and the
// force resultants.

#include "reference/MeshTestSupport.hpp"

#include <MeshReferenceModels.hpp>

#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/meshing/MeshSizing.hpp>
#include <bettercad/structural/StructuralLoadVector.hpp>
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
using structural::GlobalStructuralSystem;
using structural::kDofsPerNode;
using structural::PreparedLoads;
using structural::PressureLoad;
using structural::StiffnessMatrix;
using structural::StructuralAnalysisMode;
using structural::StructuralLoad;
using structural::StructuralMaterial;
using structural::StructuralModel;

constexpr double kYoungs = 210.0e9;
constexpr double kDensity = 7850.0;

void assignSteel(Document& document) {
    features::MaterialDefinition definition;
    definition.designation = "Steel";
    definition.mechanical.youngsModulus =
        materials::MaterialProperty<ElasticModulus>::known(ElasticModulus::fromSi(kYoungs));
    definition.mechanical.poissonRatio =
        materials::MaterialProperty<bettercad::PoissonRatio>::known(PoissonRatio::of(0.3));
    definition.mechanical.density =
        materials::MaterialProperty<Density>::known(Density::fromSi(kDensity));
    const Result<MaterialId> id = features::createMaterial(document, "Steel", definition);
    REQUIRE(id.has_value());
    REQUIRE(features::assignMaterial(document, *id).has_value());
}

/// One assembly, measured.
struct Measured {
    std::size_t nodes = 0;
    std::size_t tetrahedra = 0;
    std::size_t dofs = 0;
    std::size_t nonZeros = 0;
    double symmetryError = 0.0;
    double largest = 0.0;
    bool finite = false;
    std::array<double, 3> resultant{};
};

[[nodiscard]] GlobalStructuralSystem assemble(
    MeshedReference& reference, const std::vector<StructuralLoad>& loads,
    StructuralAnalysisMode mode = StructuralAnalysisMode::LinearStatic) {
    Result<StructuralModel> model = structural::requireStructuralModel(
        reference.document(), reference.regenerator(), reference.mesher(), reference.control());
    INFO((model.has_value() ? std::string{} : model.error().message));
    REQUIRE(model.has_value());
    Result<StructuralMaterial> material =
        structural::resolveStructuralMaterial(reference.document(), reference.body(), mode);
    INFO((material.has_value() ? std::string{} : material.error().message));
    REQUIRE(material.has_value());
    Result<PreparedLoads> prepared =
        structural::prepareStructuralLoads(*model, *material, loads);
    INFO((prepared.has_value() ? std::string{} : prepared.error().message));
    REQUIRE(prepared.has_value());
    Result<GlobalStructuralSystem> system =
        structural::assembleStructuralSystem(*model, *material, *prepared);
    INFO((system.has_value() ? std::string{} : system.error().message));
    REQUIRE(system.has_value());
    return std::move(*system);
}

[[nodiscard]] Measured measure(const GlobalStructuralSystem& system,
                               const meshing::Mesh& mesh) {
    Measured out;
    out.nodes = mesh.nodeCount();
    out.tetrahedra = mesh.tetrahedra().size();
    out.dofs = system.degreesOfFreedom();
    out.nonZeros = system.stiffness().nonZeros();
    out.symmetryError = system.stiffness().largestSymmetryError();
    out.largest = system.stiffness().largestMagnitude();
    out.finite = std::ranges::all_of(system.stiffness().values(),
                                     [](double v) { return std::isfinite(v); }) &&
                 std::ranges::all_of(system.force().values(),
                                     [](double v) { return std::isfinite(v); });
    const Force3D resultant = system.force().resultantForce();
    out.resultant = {resultant.x.si(), resultant.y.si(), resultant.z.si()};
    return out;
}

void report(const std::string& model, const Measured& m) {
    WARN(model << " | " << m.nodes << " nodes | " << m.tetrahedra << " tets | " << m.dofs
               << " DOFs | " << m.nonZeros << " nnz | ||K-K^T|| " << m.symmetryError
               << " (relative " << (m.largest > 0.0 ? m.symmetryError / m.largest : 0.0)
               << ") | finite " << (m.finite ? "YES" : "NO") << " | F resultant ("
               << m.resultant[0] << ", " << m.resultant[1] << ", " << m.resultant[2] << ") N");
}

/// The checks that scale: dimensions, the sparsity bound, symmetry over
/// stored entries, finiteness.
void checkScalable(const GlobalStructuralSystem& system, const meshing::Mesh& mesh,
                   const Measured& m) {
    const std::size_t dofs = kDofsPerNode * mesh.nodeCount();
    CHECK(m.dofs == dofs);
    CHECK(system.stiffness().rows() == dofs);
    CHECK(system.stiffness().columns() == dofs);
    CHECK(system.force().size() == dofs);
    CHECK(m.finite);
    CHECK(m.largest > 0.0);
    // SPARSE, and the bound is the structural one: a degree of freedom couples
    // only to the degrees of freedom it shares an element with, so nnz is at
    // most 144 per element and that is far below Ndof squared on every model
    // here.
    CHECK(m.nonZeros <= 144 * m.tetrahedra);
    CHECK(m.nonZeros < dofs * dofs);
    CHECK(m.symmetryError < 1e-14 * m.largest);
    CHECK(system.elementOrder().size() == m.tetrahedra);
    CHECK(std::ranges::is_sorted(system.elementOrder()));
    CHECK(system.describes(mesh));

    const std::span<const StiffnessMatrix::Index> rowStart = system.stiffness().rowStart();
    REQUIRE(rowStart.size() == dofs + 1);
    CHECK(rowStart.front() == 0);
    CHECK(rowStart.back() == m.nonZeros);
    CHECK(std::ranges::is_sorted(rowStart));
}

/// `K r` for a rigid translation along @p axis, without densifying.
[[nodiscard]] double rigidTranslationResidual(const GlobalStructuralSystem& system,
                                              const meshing::Mesh& mesh, std::size_t axis) {
    std::vector<double> r(system.degreesOfFreedom(), 0.0);
    for (std::size_t ordinal = 0; ordinal < mesh.nodeCount(); ++ordinal) {
        r[kDofsPerNode * ordinal + axis] = 1.0;
    }
    const std::span<const StiffnessMatrix::Index> rowStart = system.stiffness().rowStart();
    const std::span<const StiffnessMatrix::Index> inner = system.stiffness().innerIndices();
    const std::span<const double> values = system.stiffness().values();
    double squared = 0.0;
    for (std::size_t row = 0; row < system.stiffness().rows(); ++row) {
        double sum = 0.0;
        for (StiffnessMatrix::Index slot = rowStart[row]; slot < rowStart[row + 1]; ++slot) {
            sum += values[slot] * r[static_cast<std::size_t>(inner[slot])];
        }
        squared += sum * sum;
    }
    return std::sqrt(squared);
}

} // namespace

TEST_CASE("StructuralSystem_AssemblesEveryReferenceModel", "[structural][assembly][reference]") {
    // The assembly table of brief sections 87 and 136, over models with a
    // plain volume, a through-hole, a void and local refinement. P17-ASSEMBLY
    // understands none of those features -- it consumes valid Tet4
    // connectivity -- and that is the point: the voids must not invent
    // coupling across empty space, which the sparsity bound and the rigid-body
    // residuals together establish.
    SECTION("RM-MESH-01, a 120 x 70 x 35 mm block") {
        auto built = reference::buildMeshBlockReferenceModel();
        REQUIRE(built.has_value());
        MeshedReference model(std::move(built->document));
        assignSteel(model.document());
        const meshing::VolumeMesh& volume = model.require();
        const GlobalStructuralSystem system = assemble(model, {});
        const Measured m = measure(system, volume.mesh());
        report("RM-MESH-01", m);
        checkScalable(system, volume.mesh(), m);
        // No loads, so F is exactly zero.
        CHECK(m.resultant[0] == 0.0);
        CHECK(m.resultant[1] == 0.0);
        CHECK(m.resultant[2] == 0.0);
    }

    SECTION("RM-MESH-03, a plate with a through-hole") {
        auto built = reference::buildMeshPlateWithHoleReferenceModel();
        REQUIRE(built.has_value());
        MeshedReference model(std::move(built->document));
        assignSteel(model.document());
        const meshing::VolumeMesh& volume = model.require();
        const GlobalStructuralSystem system = assemble(model, {});
        const Measured m = measure(system, volume.mesh());
        report("RM-MESH-03", m);
        checkScalable(system, volume.mesh(), m);
    }

    SECTION("RM-MESH-04, a hollow tube") {
        auto built = reference::buildMeshTubeReferenceModel();
        REQUIRE(built.has_value());
        MeshedReference model(std::move(built->document));
        assignSteel(model.document());
        const meshing::VolumeMesh& volume = model.require();
        const GlobalStructuralSystem system = assemble(model, {});
        const Measured m = measure(system, volume.mesh());
        report("RM-MESH-04", m);
        checkScalable(system, volume.mesh(), m);
    }

    SECTION("RM-MESH-07, local refinement on one face") {
        auto built = reference::buildMeshLocalRefinementReferenceModel();
        REQUIRE(built.has_value());
        MeshedReference model(std::move(built->document));
        assignSteel(model.document());
        const meshing::VolumeMesh& volume = model.require();
        const GlobalStructuralSystem system = assemble(model, {});
        const Measured m = measure(system, volume.mesh());
        report("RM-MESH-07", m);
        checkScalable(system, volume.mesh(), m);
    }
}

TEST_CASE("StructuralSystem_PreservesTheRigidBodyModesOfAReferenceBody",
          "[structural][assembly][reference]") {
    // Brief sections 31 to 33 on a body with a VOID. If assembly had invented
    // coupling across the hole -- a spatial neighbour rather than a shared
    // node -- the translations would no longer be exact null modes, because
    // the invented stiffness would not be balanced by any element.
    auto built = reference::buildMeshTubeReferenceModel();
    REQUIRE(built.has_value());
    MeshedReference model(std::move(built->document));
    assignSteel(model.document());
    const meshing::VolumeMesh& volume = model.require();
    const GlobalStructuralSystem system = assemble(model, {});
    const double scale = system.stiffness().largestMagnitude();
    REQUIRE(scale > 0.0);

    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double residual = rigidTranslationResidual(system, volume.mesh(), axis);
        const double reference_ =
            scale * std::sqrt(static_cast<double>(system.degreesOfFreedom()));
        WARN("RM-MESH-04 | translation axis " << axis << " | ||K r|| = " << residual
                                              << " | relative " << residual / reference_);
        CHECK(residual < 1e-10 * reference_);
    }
}

TEST_CASE("StructuralSystem_CarriesAPreparedPressureFieldIntoF",
          "[structural][assembly][reference]") {
    // Brief sections 24, 25 and 114. P17-LOAD-001 integrates the pressure;
    // assembly only scatters its nodal field, and the claim is that the
    // scatter preserves it EXACTLY -- compared against the prepared field and
    // against the analytical `p A` of RM-MESH-01's end cap, whose area is
    // 120 x 70 mm from the model's own declared dimensions.
    auto built = reference::buildMeshBlockReferenceModel();
    REQUIRE(built.has_value());
    const FaceName top = built->top();
    MeshedReference model(std::move(built->document));
    assignSteel(model.document());
    const meshing::VolumeMesh& volume = model.require();

    const double pressure = 250000.0;
    const double analyticArea = 0.120 * 0.070;
    const std::vector<StructuralLoad> loads{
        StructuralLoad{LoadId::fromValue(1),
                       PressureLoad{.face = top, .magnitude = Pressure::fromSi(pressure)}}};

    Result<StructuralModel> prepared = structural::requireStructuralModel(
        model.document(), model.regenerator(), model.mesher(), model.control());
    REQUIRE(prepared.has_value());
    Result<StructuralMaterial> material = structural::resolveStructuralMaterial(
        model.document(), model.body(), StructuralAnalysisMode::LinearStatic);
    REQUIRE(material.has_value());
    Result<PreparedLoads> field =
        structural::prepareStructuralLoads(*prepared, *material, loads);
    REQUIRE(field.has_value());
    Result<GlobalStructuralSystem> system =
        structural::assembleStructuralSystem(*prepared, *material, *field);
    INFO((system.has_value() ? std::string{} : system.error().message));
    REQUIRE(system.has_value());

    const Force3D fromLoads = field->resultantForce();
    const Force3D fromVector = system->force().resultantForce();
    WARN("RM-MESH-01 end cap | p = " << pressure << " Pa | expected -p A = "
                                     << -pressure * analyticArea << " N | prepared "
                                     << fromLoads.z.si() << " N | assembled "
                                     << fromVector.z.si() << " N");

    SECTION("the assembled resultant is the analytical -p A, acting inward") {
        // The end cap's outward normal is +Z, and a positive pressure acts
        // INWARD, so the resultant is along -Z.
        CHECK_THAT(fromVector.z.si(), WithinRel(-pressure * analyticArea, 1e-11));
        CHECK_THAT(fromVector.x.si(), WithinAbs(0.0, 1e-8));
        CHECK_THAT(fromVector.y.si(), WithinAbs(0.0, 1e-8));
    }

    SECTION("and every prepared nodal force appears in F unchanged") {
        // Not just the resultant: entry by entry, through the numbering, so a
        // scatter that put the right total in the wrong rows would fail.
        std::size_t ordinal = 0;
        std::vector<std::size_t> ordinalOf(0);
        for (const meshing::Node& node : volume.mesh().nodes()) {
            (void)node;
            ++ordinal;
        }
        REQUIRE(ordinal == volume.mesh().nodeCount());

        std::size_t checked = 0;
        for (const structural::NodalLoad& load : field->nodal()) {
            // The node's ordinal, found here rather than through the
            // numbering, so the row is derived independently.
            std::size_t position = 0;
            bool found = false;
            for (const meshing::Node& node : volume.mesh().nodes()) {
                if (node.id == load.node) {
                    found = true;
                    break;
                }
                ++position;
            }
            REQUIRE(found);
            const std::array<double, 3> expected{load.force.x.si(), load.force.y.si(),
                                                 load.force.z.si()};
            for (std::size_t offset = 0; offset < kDofsPerNode; ++offset) {
                INFO("node " << load.node.value() << " ordinal " << position << " offset "
                             << offset);
                CHECK_THAT(system->force()[kDofsPerNode * position + offset].si(),
                           WithinAbs(expected[offset], 1e-12));
            }
            ++checked;
        }
        REQUIRE(checked > 0);
        WARN("every one of " << checked << " prepared nodal forces appears in F unchanged");
    }
}

TEST_CASE("StructuralSystem_CarriesAPreparedGravityFieldIntoF",
          "[structural][assembly][reference]") {
    // Brief sections 26 and 115. Gravity IS supported by P17-LOAD-001, so
    // this is a real case and not an N/A. The expected weight is `rho V g`
    // with V from RM-MESH-01's own declared 120 x 70 x 35 mm -- not from the
    // mesh -- and assembly recomputes no density and integrates nothing.
    auto built = reference::buildMeshBlockReferenceModel();
    REQUIRE(built.has_value());
    MeshedReference model(std::move(built->document));
    assignSteel(model.document());
    model.require();

    const double volume = 0.120 * 0.070 * 0.035;
    const double expected = -kDensity * volume * structural::kStandardGravity;
    const std::vector<StructuralLoad> loads{
        StructuralLoad{LoadId::fromValue(1),
                       structural::GravityLoad{
                           .acceleration =
                               Vector3D{0.0, 0.0, -structural::kStandardGravity}}}};

    const GlobalStructuralSystem system =
        assemble(model, loads, StructuralAnalysisMode::LinearStaticWithGravity);
    const Force3D resultant = system.force().resultantForce();
    WARN("RM-MESH-01 gravity | rho V g = " << expected << " N | assembled " << resultant.z.si()
                                           << " N");
    CHECK_THAT(resultant.z.si(), WithinRel(expected, 1e-10));
    CHECK_THAT(resultant.x.si(), WithinAbs(0.0, 1e-8));
    CHECK_THAT(resultant.y.si(), WithinAbs(0.0, 1e-8));
}

TEST_CASE("StructuralSystem_AssemblesCoarseAndRefinedMeshesOfOneModel",
          "[structural][assembly][reference]") {
    // Brief section 90. Two discretisations of RM-MESH-07: both valid, with
    // DIFFERENT Ndof, different nnz and different matrix dimensions. Raw
    // matrix equality is not the claim and would be wrong; what holds at every
    // level is that the system is well formed and the rigid modes survive.
    auto built = reference::buildMeshLocalRefinementReferenceModel();
    REQUIRE(built.has_value());
    MeshedReference model(std::move(built->document));
    assignSteel(model.document());

    struct Level {
        double target = 0.0;
        Measured m{};
    };
    std::vector<Level> levels;

    for (const double targetMm : {12.0, 4.0}) {
        meshing::MeshControlDefinition definition = model.definition()->definition();
        REQUIRE(definition.mesh.sizing.local.size() == 1);
        definition.mesh.sizing.local.front().targetSize = Length::fromSi(targetMm * 1e-3);
        REQUIRE(model.document()
                    .modifyObject<meshing::MeshControl>(
                        model.controlObject(),
                        [&definition](meshing::MeshControl& control) {
                            return control.setDefinition(definition);
                        })
                    .has_value());
        const meshing::VolumeMesh& volume = model.require();
        const GlobalStructuralSystem system = assemble(model, {});
        const Measured m = measure(system, volume.mesh());
        report("RM-MESH-07 @ " + std::to_string(targetMm) + " mm", m);
        checkScalable(system, volume.mesh(), m);
        levels.push_back({targetMm, m});
    }
    REQUIRE(levels.size() == 2);

    SECTION("the refinement really changed the discretisation") {
        // The premise, first: without it the comparison below is between a
        // mesh and itself.
        CHECK(levels[1].m.nodes > levels[0].m.nodes);
        CHECK(levels[1].m.tetrahedra > levels[0].m.tetrahedra);
    }

    SECTION("so Ndof and nnz follow it, and both systems are well formed") {
        CHECK(levels[1].m.dofs > levels[0].m.dofs);
        CHECK(levels[1].m.nonZeros > levels[0].m.nonZeros);
        CHECK(levels[0].m.dofs == kDofsPerNode * levels[0].m.nodes);
        CHECK(levels[1].m.dofs == kDofsPerNode * levels[1].m.nodes);
    }
}

TEST_CASE("StructuralSystem_AssemblesALargeMeshInSparseStorage",
          "[structural][assembly][reference]") {
    // Brief sections 91 to 93. The claim that a small mesh cannot make: the
    // production path stays sparse at a size where a dense `Ndof x Ndof`
    // matrix would be absurd, and the arithmetic says so rather than a comment.
    //
    // THE FIXTURE IS CURVED, and a block would not do: P16-SIZE-001 records
    // that "OCCT triangulates a PLANAR face with two triangles whatever the
    // deflection", so only a curved wall responds to a finer surface control.
    // RM-MESH-02's cylinder at a tight deflection is what produces a mesh
    // worth calling large here.
    //
    // NOT A PERFORMANCE TEST. No wall-clock bound is asserted -- that would be
    // a bound on this machine -- and no timing is a gate.
    auto built = reference::buildMeshCylinderReferenceModel();
    REQUIRE(built.has_value());
    MeshedReference model(std::move(built->document));
    assignSteel(model.document());

    // TWO SIZES OF THE SAME MODEL, because "sparse" is a statement about
    // SCALING and not about one number. The first draft of this test asserted
    // that a dense matrix would be more than fifty times the sparse storage,
    // which is exactly the arbitrary threshold brief section 77 warns against
    // -- it came out at thirty-two and the threshold, not the code, decided
    // the verdict. What is asserted instead is that the storage grows with the
    // ELEMENT COUNT and not with Ndof squared, measured across a tenfold
    // change in size.
    struct Level {
        double deflectionMm = 0.0;
        double targetMm = 0.0;
        Measured m{};
    };
    std::vector<Level> levels;

    for (const Level& asked : {Level{0.25, 12.0, {}}, Level{0.025, 6.0, {}}}) {
        meshing::MeshControlDefinition definition = model.definition()->definition();
        definition.mesh.surface.linearDeflection =
            Length::fromSi(asked.deflectionMm * 1.0e-3);
        definition.mesh.sizing.globalTargetSize = Length::fromSi(asked.targetMm * 1.0e-3);
        REQUIRE(model.document()
                    .modifyObject<meshing::MeshControl>(
                        model.controlObject(),
                        [&definition](meshing::MeshControl& control) {
                            return control.setDefinition(definition);
                        })
                    .has_value());
        const meshing::VolumeMesh& volume = model.require();
        const GlobalStructuralSystem system = assemble(model, {});
        const Measured m = measure(system, volume.mesh());
        report("RM-MESH-02 @ " + std::to_string(asked.deflectionMm) + " mm / " +
                   std::to_string(asked.targetMm) + " mm",
               m);
        checkScalable(system, volume.mesh(), m);

        // The rigid modes survive at every size, which is the cheapest
        // whole-matrix correctness check that does not densify.
        const double scale = system.stiffness().largestMagnitude();
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const double residual = rigidTranslationResidual(system, volume.mesh(), axis);
            const double reference_ =
                scale * std::sqrt(static_cast<double>(system.degreesOfFreedom()));
            INFO("Ndof " << m.dofs << ", axis " << axis << ": ||K r|| = " << residual
                         << ", relative " << residual / reference_);
            CHECK(residual < 1e-10 * reference_);
        }

        levels.push_back({asked.deflectionMm, asked.targetMm, m});
    }
    REQUIRE(levels.size() == 2);
    const Measured& small = levels[0].m;
    const Measured& large = levels[1].m;

    const double denseMiB =
        static_cast<double>(large.dofs) * static_cast<double>(large.dofs) * 8.0 / 1.048576e6;
    const double sparseMiB = static_cast<double>(large.nonZeros) * 16.0 / 1.048576e6;
    WARN("small: " << small.nodes << " nodes, " << small.tetrahedra << " tets, " << small.dofs
                   << " DOFs, " << small.nonZeros << " nnz, " << small.nonZeros / small.dofs
                   << " nnz per row");
    WARN("large: " << large.nodes << " nodes, " << large.tetrahedra << " tets, " << large.dofs
                   << " DOFs, " << large.nonZeros << " nnz, " << large.nonZeros / large.dofs
                   << " nnz per row | sparse " << sparseMiB << " MiB, a dense Ndof^2 would be "
                   << denseMiB << " MiB");

    SECTION("the two levels really are an order of magnitude apart") {
        // The premise. Without it the scaling claim below compares a mesh with
        // itself.
        REQUIRE(large.tetrahedra > 1000);
        REQUIRE(large.tetrahedra > 5 * small.tetrahedra);
        REQUIRE(large.dofs > 5 * small.dofs);
    }

    SECTION("nnz grows with the element count, never with Ndof squared") {
        // The structural bound, exact: a degree of freedom couples only to the
        // degrees of freedom it shares an element with, so at most 144 entries
        // per element.
        CHECK(large.nonZeros <= 144 * large.tetrahedra);

        // And the SCALING, which is what sparse means. Entries per row is a
        // property of the mesh's connectivity, not of its size, so it must not
        // grow when the mesh does -- whereas a dense matrix's entries per row
        // is Ndof and grows exactly in proportion.
        const double smallPerRow =
            static_cast<double>(small.nonZeros) / static_cast<double>(small.dofs);
        const double largePerRow =
            static_cast<double>(large.nonZeros) / static_cast<double>(large.dofs);
        INFO("entries per row: " << smallPerRow << " -> " << largePerRow << " while Ndof went "
                                 << small.dofs << " -> " << large.dofs);
        CHECK(largePerRow < 2.0 * smallPerRow);
        // Which a dense matrix could not satisfy: its entries per row IS Ndof.
        CHECK(largePerRow < static_cast<double>(large.dofs) / 10.0);
    }
}
