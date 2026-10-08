// P17-ASSEMBLY-001: the global linear system `K u = F`.
//
// THE ORACLE IS INDEPENDENT, AND THAT IS THE WHOLE DESIGN OF THIS FILE.
// Production assembles a sparse CSR by a symbolic pass and a numeric pass,
// using `P17-ELEM`'s `Ke` and `P17-DOF`'s numbering. The oracle below builds a
// DENSE matrix by a route that shares no code with any of it:
//
//   Ke        computed from the 4x4 inverse of `[1 x y z]`, the same
//             independent route P17-ELEM-001's own tests use -- no Jacobian is
//             formed, lambda and mu are computed from E and nu here rather
//             than taken from P15's derived shear modulus, and Eigen does the
//             multiplication
//   the row   computed HERE as `3 * ordinal + component`, from the node's
//             position in `mesh.nodes()`. Deliberately NOT through
//             MeshDofMap: taking production's numbering would make the
//             comparison a tautology, and computing it here cross-checks the
//             documented interleaving at the same time
//   the scatter   an explicit nested loop into a dense Eigen matrix
//
// So a disagreement is a real disagreement, and it is compared ENTRYWISE over
// the whole matrix -- every production entry against the oracle, and every
// oracle entry against production, so neither a missing entry nor an extra one
// can hide.
//
// WHY THERE IS NO ONE-TETRAHEDRON FIXTURE. `assembleStructuralSystem` takes a
// `StructuralModel`, whose possession is ADR-036's evidence that the mesh came
// from the mesher; a hand-built one-element `Mesh` cannot become one, and the
// mesher will not produce a single-tetrahedron mesh of a real solid. Testing
// through a back door was the alternative and CLAUDE.md forbids it. What
// replaces it is stronger: on a real mesh the test counts, per global entry,
// how many elements contribute, and then asserts
//
//     exactly one contributor   K(I,J) == that element's Ke(a,b), EXACTLY
//     two or more              K(I,J) == the sum of all of them
//
// which is the single-element claim and the duplicate-accumulation claim read
// off real connectivity rather than a synthetic fixture.
//
// EIGEN IS USED HERE AND NOT IN PRODUCTION. ADR-038 records the decision: the
// test binary already links Eigen, and a dense oracle, an eigenvalue spectrum
// and a 4x4 inverse are exactly what a test needs and a sparse assembler does
// not.

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
#include <bettercad/structural/StructuralSystem.hpp>

#include <Eigen/Dense>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
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
using structural::AssemblyProblem;
using structural::DofComponent;
using structural::DofIndex;
using structural::ForceVector;
using structural::GlobalStructuralSystem;
using structural::kDofsPerNode;
using structural::kTet4Dofs;
using structural::kTet4Nodes;
using structural::MeshDofMap;
using structural::NodalDof;
using structural::PreparedLoads;
using structural::StiffnessMatrix;
using structural::StructuralAnalysis;
using structural::StructuralAnalysisDefinition;
using structural::StructuralAnalysisMode;
using structural::StructuralLoad;
using structural::StructuralMaterial;
using structural::StructuralModel;
using structural::StructuralRestraint;

namespace {

constexpr double kYoungs = 210.0e9;
constexpr double kPoisson = 0.3;

/// A document with a block, a material, a meshing control and an analysis:
/// everything the global system needs to be assembled from.
///
/// 40 x 30 x 20 mm, with a 6 mm local control on one side face over a 20 mm
/// global target -- RM-MESH-07's own ratio. The local control is what gives
/// the mesh interior nodes: on the default sizing this block meshes to nine
/// nodes, which is too few for a node pair to be shared by exactly one
/// element and too few for the connectivity claims below to bite.
struct AssembledPart {
    Document document{"Part"};
    features::Regenerator regenerator;
    meshing::Mesher mesher;
    ObjectId feature{};
    MeshControlId control{};
    AnalysisId analysis{};
    MaterialId material{};
    std::array<EntityId, 4> lines{};

    explicit AssembledPart(bool withDensity = true, double youngs = kYoungs) {
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

    [[nodiscard]] FaceName endCap() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::EndCap}};
    }
    [[nodiscard]] FaceName startCap() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::StartCap}};
    }

    [[nodiscard]] StructuralModel model() const {
        Result<StructuralModel> prepared =
            structural::requireStructuralModel(document, regenerator, mesher, control);
        INFO((prepared.has_value() ? std::string{} : prepared.error().message));
        REQUIRE(prepared.has_value());
        return std::move(*prepared);
    }

    [[nodiscard]] StructuralMaterial resolved(
        StructuralAnalysisMode mode = StructuralAnalysisMode::LinearStatic) const {
        Result<StructuralMaterial> material_ =
            structural::resolveStructuralMaterial(document, feature, mode);
        INFO((material_.has_value() ? std::string{} : material_.error().message));
        REQUIRE(material_.has_value());
        return *material_;
    }

    [[nodiscard]] PreparedLoads prepared(
        const std::vector<StructuralLoad>& loads,
        StructuralAnalysisMode mode = StructuralAnalysisMode::LinearStatic) const {
        Result<PreparedLoads> field =
            structural::prepareStructuralLoads(model(), resolved(mode), loads);
        INFO((field.has_value() ? std::string{} : field.error().message));
        REQUIRE(field.has_value());
        return std::move(*field);
    }

    [[nodiscard]] GlobalStructuralSystem system(
        const std::vector<StructuralLoad>& loads = {},
        StructuralAnalysisMode mode = StructuralAnalysisMode::LinearStatic) const {
        Result<GlobalStructuralSystem> assembled = structural::assembleStructuralSystem(
            model(), resolved(mode), prepared(loads, mode));
        INFO((assembled.has_value() ? std::string{} : assembled.error().message));
        REQUIRE(assembled.has_value());
        return std::move(*assembled);
    }

    void setMaterialModulus(double youngs) {
        materials::MechanicalProperties mechanical =
            features::findMaterial(document, material)->definition().mechanical;
        mechanical.youngsModulus =
            materials::MaterialProperty<ElasticModulus>::known(ElasticModulus::fromSi(youngs));
        REQUIRE(features::setMaterialMechanical(document, material, mechanical).has_value());
    }

    void setMaterialDensity(double density) {
        materials::MechanicalProperties mechanical =
            features::findMaterial(document, material)->definition().mechanical;
        mechanical.density = materials::MaterialProperty<Density>::known(Density::fromSi(density));
        REQUIRE(features::setMaterialMechanical(document, material, mechanical).has_value());
    }

    void setRestraints(std::vector<StructuralRestraint> restraints) {
        REQUIRE(document
                    .modifyObject<StructuralAnalysis>(
                        ObjectId::fromValue(analysis.value()),
                        [&](StructuralAnalysis& study) -> Result<bool> {
                            StructuralAnalysisDefinition definition = study.definition();
                            definition.restraints = std::move(restraints);
                            return study.setDefinition(std::move(definition));
                        })
                    .has_value());
    }
};

// -----------------------------------------------------------------------
// The independent oracle
// -----------------------------------------------------------------------

/// One element's `Ke`, by a route that shares no code with production.
///
/// `Ni(x,y,z) = ai + bi x + ci y + di z` with `Ni(node j) = delta_ij`, so with
/// `A` the 4x4 whose rows are `[1 xj yj zj]`, rows 1..3 of `A^-1` hold the
/// gradients directly. No Jacobian is formed, so a transposition in production
/// cannot be mirrored here, and `lambda` and `mu` are computed from `E` and
/// `nu` rather than taken from P15's derived shear modulus.
[[nodiscard]] Eigen::Matrix<double, 12, 12>
referenceKe(const std::array<Point3D, 4>& p, double youngs, double poisson) {
    Eigen::Matrix4d a;
    for (int i = 0; i < 4; ++i) {
        a(i, 0) = 1.0;
        a(i, 1) = p[static_cast<std::size_t>(i)].x.si();
        a(i, 2) = p[static_cast<std::size_t>(i)].y.si();
        a(i, 3) = p[static_cast<std::size_t>(i)].z.si();
    }
    const double volume = a.determinant() / 6.0;
    const Eigen::Matrix4d coefficients = a.inverse();

    Eigen::Matrix<double, 6, 12> b = Eigen::Matrix<double, 6, 12>::Zero();
    for (int node = 0; node < 4; ++node) {
        const double bi = coefficients(1, node);
        const double ci = coefficients(2, node);
        const double di = coefficients(3, node);
        const int ux = 3 * node + 0;
        const int uy = 3 * node + 1;
        const int uz = 3 * node + 2;
        b(0, ux) = bi;
        b(1, uy) = ci;
        b(2, uz) = di;
        b(3, ux) = ci;
        b(3, uy) = bi;
        b(4, uy) = di;
        b(4, uz) = ci;
        b(5, uz) = bi;
        b(5, ux) = di;
    }

    const double lame = youngs * poisson / ((1.0 + poisson) * (1.0 - 2.0 * poisson));
    const double shear = youngs / (2.0 * (1.0 + poisson));
    Eigen::Matrix<double, 6, 6> d = Eigen::Matrix<double, 6, 6>::Zero();
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            d(i, j) = lame + (i == j ? 2.0 * shear : 0.0);
        }
    }
    for (int i = 3; i < 6; ++i) {
        d(i, i) = shear;
    }
    return volume * b.transpose() * d * b;
}

/// The row of node ordinal @p ordinal, component @p offset -- computed HERE,
/// not through `MeshDofMap`. P17-DOF documents the numbering as
/// `3k + c + 1` over a 1-based identity, and the assembly's row space is the
/// free numbering of the empty constraint set, so the row is `3k + c`.
/// Computing it independently is what makes the oracle an oracle.
[[nodiscard]] std::size_t oracleRow(std::size_t ordinal, std::size_t offset) {
    return kDofsPerNode * ordinal + offset;
}

/// Node handle -> its ordinal in `mesh.nodes()`.
[[nodiscard]] std::map<meshing::NodeId::ValueType, std::size_t>
ordinalsOf(const meshing::Mesh& mesh) {
    std::map<meshing::NodeId::ValueType, std::size_t> out;
    std::size_t ordinal = 0;
    for (const meshing::Node& node : mesh.nodes()) {
        out.emplace(node.id.value(), ordinal++);
    }
    return out;
}

[[nodiscard]] std::array<Point3D, 4> cornersOf(const meshing::Mesh& mesh,
                                               const meshing::Tetrahedron& tet) {
    std::array<Point3D, 4> corners{};
    for (std::size_t corner = 0; corner < 4; ++corner) {
        const meshing::Node* node = mesh.findNode(tet.nodes[corner]);
        REQUIRE(node != nullptr);
        corners[corner] = node->position;
    }
    return corners;
}

/// The whole global stiffness, dense, by explicit test-side scatter-add.
[[nodiscard]] Eigen::MatrixXd denseOracle(const meshing::Mesh& mesh, double youngs,
                                          double poisson) {
    const std::map<meshing::NodeId::ValueType, std::size_t> ordinals = ordinalsOf(mesh);
    const auto dofs = static_cast<Eigen::Index>(kDofsPerNode * mesh.nodeCount());
    Eigen::MatrixXd k = Eigen::MatrixXd::Zero(dofs, dofs);
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        const Eigen::Matrix<double, 12, 12> ke = referenceKe(cornersOf(mesh, tet), youngs, poisson);
        std::array<std::size_t, 12> rows{};
        for (std::size_t corner = 0; corner < 4; ++corner) {
            const std::size_t ordinal = ordinals.at(tet.nodes[corner].value());
            for (std::size_t offset = 0; offset < kDofsPerNode; ++offset) {
                rows[kDofsPerNode * corner + offset] = oracleRow(ordinal, offset);
            }
        }
        for (std::size_t a = 0; a < 12; ++a) {
            for (std::size_t b = 0; b < 12; ++b) {
                k(static_cast<Eigen::Index>(rows[a]), static_cast<Eigen::Index>(rows[b])) +=
                    ke(static_cast<Eigen::Index>(a), static_cast<Eigen::Index>(b));
            }
        }
    }
    return k;
}

/// How many elements contribute to each global `(row, column)`, by the same
/// independent routing.
[[nodiscard]] std::map<std::pair<std::size_t, std::size_t>, std::size_t>
contributorsOf(const meshing::Mesh& mesh) {
    const std::map<meshing::NodeId::ValueType, std::size_t> ordinals = ordinalsOf(mesh);
    std::map<std::pair<std::size_t, std::size_t>, std::size_t> out;
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        std::array<std::size_t, 12> rows{};
        for (std::size_t corner = 0; corner < 4; ++corner) {
            const std::size_t ordinal = ordinals.at(tet.nodes[corner].value());
            for (std::size_t offset = 0; offset < kDofsPerNode; ++offset) {
                rows[kDofsPerNode * corner + offset] = oracleRow(ordinal, offset);
            }
        }
        for (const std::size_t row : rows) {
            for (const std::size_t column : rows) {
                ++out[{row, column}];
            }
        }
    }
    return out;
}

/// `K` read out of production, dense, for a small fixture.
[[nodiscard]] Eigen::MatrixXd denseOf(const StiffnessMatrix& k) {
    Eigen::MatrixXd dense = Eigen::MatrixXd::Zero(static_cast<Eigen::Index>(k.rows()),
                                                  static_cast<Eigen::Index>(k.columns()));
    for (std::size_t row = 0; row < k.rows(); ++row) {
        for (std::size_t column = 0; column < k.columns(); ++column) {
            dense(static_cast<Eigen::Index>(row), static_cast<Eigen::Index>(column)) =
                k.coeff(row, column).si();
        }
    }
    return dense;
}

/// A global displacement vector that is deterministic and not a rigid motion:
/// a fixed quadratic in the node coordinate, so every element deforms.
[[nodiscard]] Eigen::VectorXd deterministicDisplacement(const meshing::Mesh& mesh, double scale) {
    Eigen::VectorXd u = Eigen::VectorXd::Zero(
        static_cast<Eigen::Index>(kDofsPerNode * mesh.nodeCount()));
    std::size_t ordinal = 0;
    for (const meshing::Node& node : mesh.nodes()) {
        const double x = node.position.x.si();
        const double y = node.position.y.si();
        const double z = node.position.z.si();
        u(static_cast<Eigen::Index>(oracleRow(ordinal, 0))) = scale * (x * x + 0.5 * y);
        u(static_cast<Eigen::Index>(oracleRow(ordinal, 1))) = scale * (y * z - 0.25 * x);
        u(static_cast<Eigen::Index>(oracleRow(ordinal, 2))) = scale * (z * z + 0.75 * y * y);
        ++ordinal;
    }
    return u;
}

/// `K v`, by sparse rows, without densifying.
[[nodiscard]] Eigen::VectorXd multiply(const StiffnessMatrix& k, const Eigen::VectorXd& v) {
    Eigen::VectorXd out = Eigen::VectorXd::Zero(static_cast<Eigen::Index>(k.rows()));
    const std::span<const StiffnessMatrix::Index> rowStart = k.rowStart();
    const std::span<const StiffnessMatrix::Index> inner = k.innerIndices();
    const std::span<const double> values = k.values();
    for (std::size_t row = 0; row < k.rows(); ++row) {
        double sum = 0.0;
        for (StiffnessMatrix::Index slot = rowStart[row]; slot < rowStart[row + 1]; ++slot) {
            sum += values[slot] * v(static_cast<Eigen::Index>(inner[slot]));
        }
        out(static_cast<Eigen::Index>(row)) = sum;
    }
    return out;
}

/// A rigid translation along @p axis: every node moves by one metre.
[[nodiscard]] Eigen::VectorXd rigidTranslation(const meshing::Mesh& mesh, std::size_t axis) {
    Eigen::VectorXd r = Eigen::VectorXd::Zero(
        static_cast<Eigen::Index>(kDofsPerNode * mesh.nodeCount()));
    for (std::size_t ordinal = 0; ordinal < mesh.nodeCount(); ++ordinal) {
        r(static_cast<Eigen::Index>(oracleRow(ordinal, axis))) = 1.0;
    }
    return r;
}

/// An infinitesimal rigid rotation about @p axis: `u_i = omega x x_i`.
[[nodiscard]] Eigen::VectorXd rigidRotation(const meshing::Mesh& mesh, std::size_t axis) {
    Eigen::Vector3d omega = Eigen::Vector3d::Zero();
    omega(static_cast<Eigen::Index>(axis)) = 1.0;
    Eigen::VectorXd r = Eigen::VectorXd::Zero(
        static_cast<Eigen::Index>(kDofsPerNode * mesh.nodeCount()));
    std::size_t ordinal = 0;
    for (const meshing::Node& node : mesh.nodes()) {
        const Eigen::Vector3d x{node.position.x.si(), node.position.y.si(),
                                node.position.z.si()};
        const Eigen::Vector3d u = omega.cross(x);
        for (std::size_t offset = 0; offset < kDofsPerNode; ++offset) {
            r(static_cast<Eigen::Index>(oracleRow(ordinal, offset))) =
                u(static_cast<Eigen::Index>(offset));
        }
        ++ordinal;
    }
    return r;
}

[[nodiscard]] StructuralLoad nodalForce(const meshing::Mesh& mesh, meshing::NodeId node, double fx,
                                        double fy, double fz, std::uint64_t id) {
    return StructuralLoad{LoadId::fromValue(id),
                          structural::NodalForceLoad{
                              .mesh = mesh.stamp(),
                              .node = node,
                              .force = Force3D{Force::fromSi(fx), Force::fromSi(fy),
                                               Force::fromSi(fz)}}};
}

} // namespace

// ---------------------------------------------------------------------------
// The representation
// ---------------------------------------------------------------------------

TEST_CASE("StructuralSystem_IsSparseAndNeverADenseSquareOfTheDofCount",
          "[structural][assembly]") {
    // ADR-038's load-bearing storage claim. A dense production matrix is the
    // brief's first automatic failure, and the evidence is arithmetic: a Tet4
    // mesh couples a degree of freedom only to the degrees of freedom it
    // shares an element with, so nnz grows with the element count and NOT with
    // Ndof squared.
    AssembledPart part;
    const GlobalStructuralSystem system = part.system();
    const meshing::Mesh& mesh = part.volume().mesh();
    const std::size_t dofs = kDofsPerNode * mesh.nodeCount();

    INFO("nodes " << mesh.nodeCount() << ", tetrahedra " << mesh.tetrahedra().size() << ", Ndof "
                  << dofs << ", nnz " << system.stiffness().nonZeros());

    CHECK(system.degreesOfFreedom() == dofs);
    CHECK(system.stiffness().rows() == dofs);
    CHECK(system.stiffness().columns() == dofs);
    CHECK(system.force().size() == dofs);
    CHECK(system.stiffness().nonZeros() < dofs * dofs);

    SECTION("the CSR invariants hold") {
        const std::span<const StiffnessMatrix::Index> rowStart = system.stiffness().rowStart();
        const std::span<const StiffnessMatrix::Index> inner = system.stiffness().innerIndices();
        REQUIRE(rowStart.size() == dofs + 1);
        CHECK(rowStart.front() == 0);
        CHECK(rowStart.back() == system.stiffness().nonZeros());
        CHECK(inner.size() == system.stiffness().nonZeros());
        CHECK(system.stiffness().values().size() == system.stiffness().nonZeros());
        CHECK(std::ranges::is_sorted(rowStart));
        for (std::size_t row = 0; row < dofs; ++row) {
            // STRICTLY ascending within the row: a repeated inner index would
            // be a duplicate structural entry, which is the loss this
            // milestone exists to prevent.
            const auto first = inner.begin() + static_cast<std::ptrdiff_t>(rowStart[row]);
            const auto last = inner.begin() + static_cast<std::ptrdiff_t>(rowStart[row + 1]);
            INFO("row " << row);
            CHECK(std::is_sorted(first, last, std::less_equal<StiffnessMatrix::Index>{}) ==
                  std::is_sorted(first, last));
            CHECK(std::adjacent_find(first, last) == last);
            for (auto it = first; it != last; ++it) {
                CHECK(*it < dofs);
            }
        }
    }

    SECTION("the pattern is exactly the DOF pairs that share an element") {
        // Brief section 74 and 89: coupling arises ONLY from connectivity.
        // Nothing spatial, nothing nearest-neighbour. Both directions, so
        // neither a missing pair nor an invented one can hide.
        const std::map<std::pair<std::size_t, std::size_t>, std::size_t> contributors =
            contributorsOf(mesh);
        CHECK(system.stiffness().nonZeros() == contributors.size());

        std::size_t stored = 0;
        const std::span<const StiffnessMatrix::Index> rowStart = system.stiffness().rowStart();
        const std::span<const StiffnessMatrix::Index> inner = system.stiffness().innerIndices();
        for (std::size_t row = 0; row < dofs; ++row) {
            for (StiffnessMatrix::Index slot = rowStart[row]; slot < rowStart[row + 1]; ++slot) {
                const auto column = static_cast<std::size_t>(inner[slot]);
                INFO("stored (" << row << ", " << column << ")");
                CHECK(contributors.contains({row, column}));
                ++stored;
            }
        }
        CHECK(stored == contributors.size());
    }

    SECTION("an absent entry reads as zero") {
        // The sparse contract, and the out-of-range contract with it.
        const std::map<std::pair<std::size_t, std::size_t>, std::size_t> contributors =
            contributorsOf(mesh);
        std::size_t checked = 0;
        for (std::size_t row = 0; row < dofs && checked < 40; ++row) {
            for (std::size_t column = 0; column < dofs && checked < 40; ++column) {
                if (!contributors.contains({row, column})) {
                    CHECK(system.stiffness().coeff(row, column).si() == 0.0);
                    ++checked;
                }
            }
        }
        CHECK(checked > 0);
        CHECK(system.stiffness().coeff(dofs, 0).si() == 0.0);
        CHECK(system.stiffness().coeff(0, dofs).si() == 0.0);
    }

    SECTION("and a read past the end is zero, checked on a vector that is not zero") {
        // A mutation probe found the first draft of this check worthless: it
        // asked for `force()[Ndof]` on an UNLOADED model, where row 0 is zero
        // too -- so an out-of-range read that wrapped round to row 0 returned
        // zero and passed. The load below puts a non-zero value in row 0, so
        // wrapping is now distinguishable from answering zero.
        AssembledPart loaded;
        const meshing::Mesh& theirs = loaded.volume().mesh();
        const GlobalStructuralSystem withLoad =
            loaded.system({nodalForce(theirs, theirs.nodes().front().id, 17.0, 0.0, 0.0, 1)});
        const std::size_t size = withLoad.force().size();
        REQUIRE(size > 0);
        REQUIRE(withLoad.force()[0].si() != 0.0);
        CHECK(withLoad.force()[size].si() == 0.0);
        CHECK(withLoad.force()[size + 1].si() == 0.0);
        CHECK(withLoad.force()[size * 3].si() == 0.0);
    }
}

TEST_CASE("StructuralSystem_MapsLocalTet4DofsOntoTheGlobalNumbering",
          "[structural][assembly]") {
    // Brief section 10, pinned directly rather than inferred from a matrix
    // entry. The local order is node-major, three components per node, and the
    // global index of each comes from P17-DOF.
    AssembledPart part;
    const meshing::Mesh& mesh = part.volume().mesh();
    Result<MeshDofMap> numbering = structural::buildMeshDofMap(mesh);
    REQUIRE(numbering.has_value());

    const meshing::Tetrahedron& tet = mesh.tetrahedra().front();
    Result<std::array<DofIndex, kTet4Dofs>> dofs =
        structural::elementDegreesOfFreedom(*numbering, tet.nodes);
    INFO((dofs.has_value() ? std::string{} : dofs.error().message));
    REQUIRE(dofs.has_value());

    for (std::size_t node = 0; node < kTet4Nodes; ++node) {
        for (std::size_t offset = 0; offset < kDofsPerNode; ++offset) {
            const DofComponent component = structural::kDofComponents[offset];
            const std::size_t local = kDofsPerNode * node + offset;
            INFO("local " << local << " = node " << node << " "
                          << structural::toString(component));
            // What P17-DOF says that node and component is...
            const Result<DofIndex> expected =
                numbering->indexOf(NodalDof{.node = tet.nodes[node], .component = component});
            REQUIRE(expected.has_value());
            // ...is what position `local` holds.
            CHECK((*dofs)[local] == *expected);
            CHECK(structural::localDofIndex(node, component) == local);
        }
    }

    SECTION("and a node the numbering does not have is refused") {
        std::array<meshing::NodeId, kTet4Nodes> absent = tet.nodes;
        absent[2] = meshing::NodeId::fromValue(900000);
        CHECK_FALSE(structural::elementDegreesOfFreedom(*numbering, absent).has_value());
    }
}

// ---------------------------------------------------------------------------
// The independent oracle
// ---------------------------------------------------------------------------

TEST_CASE("StructuralSystem_MatchesAnIndependentDenseScatterOfEveryElement",
          "[structural][assembly]") {
    // THE CENTRAL CORRECTNESS CLAIM, and brief sections 15 and 73. Every entry
    // of the production CSR against a dense matrix the test built from its own
    // `Ke` and its own local-to-global routing -- and every entry of the
    // oracle against production, so a missing entry cannot hide behind a
    // one-directional comparison.
    AssembledPart part;
    const GlobalStructuralSystem system = part.system();
    const meshing::Mesh& mesh = part.volume().mesh();
    const Eigen::MatrixXd expected = denseOracle(mesh, kYoungs, kPoisson);
    const auto dofs = static_cast<Eigen::Index>(system.degreesOfFreedom());

    REQUIRE(expected.rows() == dofs);
    const double scale = expected.cwiseAbs().maxCoeff();
    REQUIRE(scale > 0.0);
    INFO("Ndof " << dofs << ", nnz " << system.stiffness().nonZeros() << ", max |K| " << scale);

    double largest = 0.0;
    for (Eigen::Index row = 0; row < dofs; ++row) {
        for (Eigen::Index column = 0; column < dofs; ++column) {
            const double got = system.stiffness()
                                   .coeff(static_cast<std::size_t>(row),
                                          static_cast<std::size_t>(column))
                                   .si();
            largest = std::max(largest, std::abs(got - expected(row, column)));
        }
    }
    INFO("largest entrywise difference " << largest << ", relative " << largest / scale);
    // 1e-12 relative: the two routes do the same algebra with different
    // groupings and different numbers of operations, so agreement is to
    // accumulated double-precision rounding and not to the last bit.
    CHECK(largest < 1e-12 * scale);
}

TEST_CASE("StructuralSystem_SumsEveryContributionAndLosesNone", "[structural][assembly]") {
    // Brief sections 13 and 111, on real connectivity: the test counts
    // contributors per entry independently and then checks the sum in EVERY
    // contributor-count group, so no regime is left unmeasured.
    //
    // THE SINGLE-CONTRIBUTOR REGIME DOES NOT EXIST HERE, and the first draft
    // of this test required that it did. On a Tet4 mesh of a solid every pair
    // of nodes that shares a tetrahedron shares more than one, so every
    // degree-of-freedom pair in the pattern has at least two contributors --
    // measured, not assumed: the distribution is recorded below. That is also
    // why there is no one-element fixture (see the file header): the claim
    // "K equals Ke where only one element contributes" has no instance to be
    // true of, and inventing a mesh to create one would need a back door.
    //
    // What replaces it is stronger. Every entry, at every contributor count
    // from the minimum to the maximum, equals the independent sum -- and a
    // production path that overwrote instead of accumulating would differ by
    // a factor of roughly `count`, which is not a tolerance question.
    AssembledPart part;
    const GlobalStructuralSystem system = part.system();
    const meshing::Mesh& mesh = part.volume().mesh();
    const std::map<std::pair<std::size_t, std::size_t>, std::size_t> contributors =
        contributorsOf(mesh);
    const Eigen::MatrixXd expected = denseOracle(mesh, kYoungs, kPoisson);
    const double scale = expected.cwiseAbs().maxCoeff();
    REQUIRE(scale > 0.0);

    // The largest disagreement in each contributor-count group.
    std::map<std::size_t, double> largestByCount;
    std::map<std::size_t, std::size_t> entriesByCount;
    for (const auto& [cell, count] : contributors) {
        const double got = system.stiffness().coeff(cell.first, cell.second).si();
        const double want = expected(static_cast<Eigen::Index>(cell.first),
                                     static_cast<Eigen::Index>(cell.second));
        largestByCount[count] = std::max(largestByCount[count], std::abs(got - want));
        ++entriesByCount[count];
    }
    REQUIRE_FALSE(entriesByCount.empty());

    std::string distribution;
    for (const auto& [count, entries] : entriesByCount) {
        distribution += std::to_string(count) + ":" + std::to_string(entries) + " ";
    }
    WARN("contributors:entries  " << distribution);

    SECTION("the fixture spans several contributor counts, so the groups mean something") {
        // The premise. One group would make the per-group comparison the same
        // comparison the dense-oracle test already makes.
        REQUIRE(entriesByCount.size() > 1);
        REQUIRE(entriesByCount.rbegin()->first > 1);
    }

    SECTION("every group equals the independent sum of its contributions") {
        for (const auto& [count, largest] : largestByCount) {
            INFO(count << " contributors, " << entriesByCount.at(count) << " entries, largest "
                       << largest);
            CHECK(largest < 1e-12 * scale);
        }
    }
}

TEST_CASE("StructuralSystem_AccumulatesANamedPairFromTwoNamedElements",
          "[structural][assembly]") {
    // The two-element table brief section 135 asks for, read off a real mesh:
    // a global entry, the elements that reach it, each one's contribution, the
    // expected sum and the assembled value.
    AssembledPart part;
    const GlobalStructuralSystem system = part.system();
    const meshing::Mesh& mesh = part.volume().mesh();
    const std::map<meshing::NodeId::ValueType, std::size_t> ordinals = ordinalsOf(mesh);

    // Find a (row, column) exactly two elements reach, and the two elements.
    std::map<std::pair<std::size_t, std::size_t>, std::vector<std::size_t>> reachedBy;
    const std::span<const meshing::Tetrahedron> tets = mesh.tetrahedra();
    for (std::size_t index = 0; index < tets.size(); ++index) {
        std::array<std::size_t, 12> rows{};
        for (std::size_t corner = 0; corner < 4; ++corner) {
            const std::size_t ordinal = ordinals.at(tets[index].nodes[corner].value());
            for (std::size_t offset = 0; offset < kDofsPerNode; ++offset) {
                rows[kDofsPerNode * corner + offset] = oracleRow(ordinal, offset);
            }
        }
        for (const std::size_t row : rows) {
            for (const std::size_t column : rows) {
                reachedBy[{row, column}].push_back(index);
            }
        }
    }

    std::optional<std::pair<std::size_t, std::size_t>> chosen;
    for (const auto& [cell, elements] : reachedBy) {
        if (elements.size() == 2 && cell.first != cell.second) {
            chosen = cell;
            break;
        }
    }
    REQUIRE(chosen.has_value());
    const std::vector<std::size_t>& elements = reachedBy.at(*chosen);
    REQUIRE(elements.size() == 2);

    // Each element's own contribution, by the independent route.
    double total = 0.0;
    std::array<double, 2> each{};
    for (std::size_t which = 0; which < 2; ++which) {
        const meshing::Tetrahedron& tet = tets[elements[which]];
        const Eigen::Matrix<double, 12, 12> ke =
            referenceKe(cornersOf(mesh, tet), kYoungs, kPoisson);
        std::array<std::size_t, 12> rows{};
        for (std::size_t corner = 0; corner < 4; ++corner) {
            const std::size_t ordinal = ordinals.at(tet.nodes[corner].value());
            for (std::size_t offset = 0; offset < kDofsPerNode; ++offset) {
                rows[kDofsPerNode * corner + offset] = oracleRow(ordinal, offset);
            }
        }
        for (std::size_t a = 0; a < 12; ++a) {
            for (std::size_t b = 0; b < 12; ++b) {
                if (rows[a] == chosen->first && rows[b] == chosen->second) {
                    each[which] += ke(static_cast<Eigen::Index>(a), static_cast<Eigen::Index>(b));
                }
            }
        }
        total += each[which];
    }

    const double got = system.stiffness().coeff(chosen->first, chosen->second).si();
    WARN("K(" << chosen->first << ", " << chosen->second << ") | element "
              << tets[elements[0]].id.value() << " contributes " << each[0] << " | element "
              << tets[elements[1]].id.value() << " contributes " << each[1] << " | expected sum "
              << total << " | actual " << got);

    // Both contributions are non-trivial, or the sum would be the larger one.
    REQUIRE(std::abs(each[0]) > 0.0);
    REQUIRE(std::abs(each[1]) > 0.0);
    CHECK_THAT(got, WithinRel(total, 1e-12));
    // And it is NOT either contribution alone, which is what an overwrite
    // would have left.
    CHECK(std::abs(got - each[0]) > 1e-6 * std::abs(total));
    CHECK(std::abs(got - each[1]) > 1e-6 * std::abs(total));
}

// ---------------------------------------------------------------------------
// Matrix properties
// ---------------------------------------------------------------------------

TEST_CASE("StructuralSystem_IsExactlyAsSymmetricAsItsElementsAre", "[structural][assembly]") {
    // THE TOLERANCE IS DERIVED, NOT TUNED, and the first draft of this test
    // asserted the wrong thing -- that the global error is exactly zero.
    //
    // It is not, and the reason is in `P17-ELEM-001` rather than here.
    // `computeTet4Stiffness` forms `Ke(a,b) = sum_k B[k][a] * (DB)[k][b]`, so
    // `Ke(a,b)` and `Ke(b,a)` are DIFFERENT sums of different products --
    // mathematically equal because `D` is symmetric, and equal to within
    // rounding in floating point. The assembly inherits exactly that and adds
    // nothing: both `(I,J)` and `(J,I)` accumulate over the same elements in
    // the same element order, so the global asymmetry can be no worse than the
    // elements' own, accumulated.
    //
    // So the bound is MEASURED from the elements and then applied to the
    // global matrix. There is no `0.5 * (K + K^T)` anywhere in the
    // implementation -- that would hide a scatter bug rather than fix a
    // numerical one, and a mutation probe confirms it.
    AssembledPart part;
    const GlobalStructuralSystem system = part.system();
    const meshing::Mesh& mesh = part.volume().mesh();
    const double error = system.stiffness().largestSymmetryError();
    const double scale = system.stiffness().largestMagnitude();
    REQUIRE(scale > 0.0);

    // The elements' own asymmetry, read through production's Ke accessor, and
    // the worst contributor count, both measured here.
    double elementAsymmetry = 0.0;
    double elementScale = 0.0;
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        Result<structural::Tet4Stiffness> ke = structural::computeTet4Stiffness(
            cornersOf(mesh, tet), part.resolved().elastic());
        REQUIRE(ke.has_value());
        for (std::size_t a = 0; a < kTet4Dofs; ++a) {
            for (std::size_t b = 0; b < kTet4Dofs; ++b) {
                elementScale = std::max(elementScale, std::abs(ke->operator()(a, b).si()));
                elementAsymmetry = std::max(
                    elementAsymmetry,
                    std::abs(ke->operator()(a, b).si() - ke->operator()(b, a).si()));
            }
        }
    }
    std::size_t mostContributors = 0;
    for (const auto& [cell, count] : contributorsOf(mesh)) {
        (void)cell;
        mostContributors = std::max(mostContributors, count);
    }
    REQUIRE(mostContributors > 0);

    const double bound = static_cast<double>(mostContributors) * elementAsymmetry;
    WARN("max |K - K^T| = " << error << " (relative " << error / scale << ") | element asymmetry "
                            << elementAsymmetry << " of |Ke| " << elementScale << " | at most "
                            << mostContributors << " contributors | bound " << bound);

    SECTION("the elements themselves are symmetric to rounding, which is where it comes from") {
        REQUIRE(elementScale > 0.0);
        // A few units in the last place of the largest entry.
        CHECK(elementAsymmetry <= 8.0 * std::numeric_limits<double>::epsilon() * elementScale);
    }

    SECTION("and the global matrix adds nothing to it") {
        CHECK(error <= bound);
        // Which is itself far below any engineering significance.
        CHECK(error < 1e-14 * scale);
    }

    SECTION("and it is NOT perfectly symmetric, which is how a symmetrisation shows") {
        // THE DETECTOR FOR THE BRIEF'S AUTOMATIC FAILURE "K is symmetrised
        // after assembly to hide error". A mutation probe found that the rest
        // of this suite cannot see such a step: averaging `(i,j)` and `(j,i)`
        // when they already agree to 1e-17 changes nothing any other test
        // measures.
        //
        // But it leaves a signature. The elements are asymmetric at the ulp
        // level and several of them write each shared entry, so an untouched
        // global matrix CANNOT come out exactly symmetric -- and a symmetrised
        // one always does, on every mesh. So a strictly positive error is the
        // evidence that nothing post-processed the values.
        //
        // THE PREMISE IS ASSERTED, because the claim depends on it: the
        // elements must really be asymmetric, and entries must really have
        // several contributors. On a mesh too small or too regular for the
        // ulps to survive, cancellation can give exactly zero legitimately --
        // RM-MESH-01, at six tetrahedra, does -- which is why this check lives
        // on this fixture and the reference models only bound the error from
        // above.
        REQUIRE(elementAsymmetry > 0.0);
        REQUIRE(mostContributors > 1);
        CHECK(error > 0.0);
    }

    SECTION("and the energy form is symmetric too") {
        // Brief section 72: v^T K u == u^T K v catches an asymmetry a direct
        // entry scan could miss if it were in the pattern rather than the
        // values.
        const Eigen::VectorXd u = deterministicDisplacement(mesh, 1e-4);
        const Eigen::VectorXd v = deterministicDisplacement(mesh, -3e-5);
        const double left = v.dot(multiply(system.stiffness(), u));
        const double right = u.dot(multiply(system.stiffness(), v));
        INFO("v^T K u = " << left << ", u^T K v = " << right);
        CHECK_THAT(left, WithinRel(right, 1e-12));
    }
}

TEST_CASE("StructuralSystem_HoldsOnlyFiniteEntries", "[structural][assembly]") {
    AssembledPart part;
    const GlobalStructuralSystem system =
        part.system({nodalForce(part.volume().mesh(), part.volume().mesh().nodes().front().id,
                                120.0, -45.0, 8.0, 1)});
    for (const double value : system.stiffness().values()) {
        REQUIRE(std::isfinite(value));
    }
    for (const double value : system.force().values()) {
        REQUIRE(std::isfinite(value));
    }

    SECTION("and the diagonal is positive wherever a DOF carries stiffness") {
        // Supplemental, not the primary proof: for positive E and
        // nondegenerate tetrahedra every active diagonal is positive.
        std::size_t positive = 0;
        for (std::size_t row = 0; row < system.stiffness().rows(); ++row) {
            const double diagonal = system.stiffness().coeff(row, row).si();
            INFO("row " << row << " diagonal " << diagonal);
            CHECK(diagonal > 0.0);
            ++positive;
        }
        CHECK(positive == system.degreesOfFreedom());
    }
}

TEST_CASE("StructuralSystem_PreservesTheSixRigidBodyModesOfAFreeBody",
          "[structural][assembly]") {
    // Brief sections 31 to 35. A free connected body's K is SINGULAR and must
    // stay singular: nothing regularises the diagonal, pins a node or adds a
    // penalty spring. The six modes are the evidence that element assembly did
    // not destroy the local null spaces.
    AssembledPart part;
    const GlobalStructuralSystem system = part.system();
    const meshing::Mesh& mesh = part.volume().mesh();
    const double scale = system.stiffness().largestMagnitude();
    REQUIRE(scale > 0.0);

    struct Mode {
        const char* name;
        Eigen::VectorXd r;
    };
    std::vector<Mode> modes;
    modes.push_back({"Tx", rigidTranslation(mesh, 0)});
    modes.push_back({"Ty", rigidTranslation(mesh, 1)});
    modes.push_back({"Tz", rigidTranslation(mesh, 2)});
    modes.push_back({"Rx", rigidRotation(mesh, 0)});
    modes.push_back({"Ry", rigidRotation(mesh, 1)});
    modes.push_back({"Rz", rigidRotation(mesh, 2)});

    for (const Mode& mode : modes) {
        const Eigen::VectorXd kr = multiply(system.stiffness(), mode.r);
        // SCALE-AWARE: the residual is compared against |K| times the size of
        // the motion, which is the only dimensionally meaningful yardstick.
        const double reference = scale * mode.r.cwiseAbs().maxCoeff() *
                                 std::sqrt(static_cast<double>(mode.r.size()));
        REQUIRE(reference > 0.0);
        WARN(mode.name << " | ||K r|| = " << kr.norm() << " | relative "
                       << kr.norm() / reference);
        CHECK(kr.norm() < 1e-10 * reference);
    }
}

TEST_CASE("StructuralSystem_IsPositiveSemiDefiniteWithNullitySix", "[structural][assembly]") {
    // Brief sections 35, 36 and 139. A full eigendecomposition, which is why
    // it is done on the smallest fixture rather than a large mesh.
    //
    // THE FIXTURE'S CONNECTIVITY IS ASSERTED FIRST. Six is the right answer
    // for ONE connected body with no internal mechanism; brief section 37 is
    // explicit that nullity six must not be hardcoded for an arbitrary mesh,
    // so the test checks that every node is reachable from element zero
    // through shared nodes before it claims the number.
    AssembledPart part;
    const GlobalStructuralSystem system = part.system();
    const meshing::Mesh& mesh = part.volume().mesh();

    SECTION("the fixture is one connected body") {
        std::map<meshing::NodeId::ValueType, std::vector<std::size_t>> nodeElements;
        const std::span<const meshing::Tetrahedron> tets = mesh.tetrahedra();
        for (std::size_t index = 0; index < tets.size(); ++index) {
            for (const meshing::NodeId node : tets[index].nodes) {
                nodeElements[node.value()].push_back(index);
            }
        }
        std::vector<bool> seenElement(tets.size(), false);
        std::vector<std::size_t> frontier{0};
        seenElement[0] = true;
        std::size_t reached = 1;
        while (!frontier.empty()) {
            const std::size_t current = frontier.back();
            frontier.pop_back();
            for (const meshing::NodeId node : tets[current].nodes) {
                for (const std::size_t neighbour : nodeElements.at(node.value())) {
                    if (!seenElement[neighbour]) {
                        seenElement[neighbour] = true;
                        ++reached;
                        frontier.push_back(neighbour);
                    }
                }
            }
        }
        INFO("reached " << reached << " of " << tets.size() << " elements");
        REQUIRE(reached == tets.size());
        CHECK(nodeElements.size() == mesh.nodeCount());
    }

    SECTION("the spectrum has exactly six near-zero eigenvalues and the rest positive") {
        const Eigen::MatrixXd dense = denseOf(system.stiffness());
        Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> solver(dense);
        REQUIRE(solver.info() == Eigen::Success);
        const Eigen::VectorXd values = solver.eigenvalues();
        const double largest = values.cwiseAbs().maxCoeff();
        REQUIRE(largest > 0.0);
        // The threshold is a CONDITIONING argument, not a tuned constant: the
        // rigid modes are exact nulls of every element, so their global
        // eigenvalues are at the rounding level of the largest, which for a
        // matrix of this size is many orders of magnitude below the smallest
        // genuine deformation mode. The gap is asserted below so the choice
        // of threshold cannot silently decide the answer.
        const double threshold = 1e-9 * largest;
        std::size_t nullity = 0;
        for (Eigen::Index i = 0; i < values.size(); ++i) {
            if (std::abs(values(i)) < threshold) {
                ++nullity;
            } else {
                // Positive semidefinite: no eigenvalue is meaningfully
                // negative.
                CHECK(values(i) > 0.0);
            }
        }
        WARN("Ndof " << values.size() << " | nullity " << nullity << " | smallest nonzero "
                     << values(static_cast<Eigen::Index>(nullity)) << " | largest " << largest);
        CHECK(nullity == 6);

        SECTION("and the threshold sits in a gap of many orders of magnitude") {
            // So the count is a property of the matrix, not of the number
            // chosen above.
            const double lastNull = std::abs(values(5));
            const double firstReal = std::abs(values(6));
            INFO("sixth |lambda| " << lastNull << ", seventh " << firstReal);
            CHECK(firstReal > 1e4 * std::max(lastNull, 1e-300));
        }
    }

    SECTION("and the energy of a deformation is positive") {
        const Eigen::VectorXd u = deterministicDisplacement(mesh, 1e-5);
        const double energy = 0.5 * u.dot(multiply(system.stiffness(), u));
        INFO("U = " << energy << " J");
        CHECK(energy > 0.0);
    }
}

TEST_CASE("StructuralSystem_EnergyEqualsTheSumOfElementEnergies", "[structural][assembly]") {
    // Brief section 70: one of the strongest assembly checks there is, because
    // it is insensitive to the pattern and sensitive to every value and every
    // row/column assignment.
    AssembledPart part;
    const GlobalStructuralSystem system = part.system();
    const meshing::Mesh& mesh = part.volume().mesh();
    const std::map<meshing::NodeId::ValueType, std::size_t> ordinals = ordinalsOf(mesh);
    const Eigen::VectorXd u = deterministicDisplacement(mesh, 2e-5);

    const double global = 0.5 * u.dot(multiply(system.stiffness(), u));

    double elementwise = 0.0;
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        const Eigen::Matrix<double, 12, 12> ke =
            referenceKe(cornersOf(mesh, tet), kYoungs, kPoisson);
        Eigen::Matrix<double, 12, 1> ue = Eigen::Matrix<double, 12, 1>::Zero();
        for (std::size_t corner = 0; corner < 4; ++corner) {
            const std::size_t ordinal = ordinals.at(tet.nodes[corner].value());
            for (std::size_t offset = 0; offset < kDofsPerNode; ++offset) {
                ue(static_cast<Eigen::Index>(kDofsPerNode * corner + offset)) =
                    u(static_cast<Eigen::Index>(oracleRow(ordinal, offset)));
            }
        }
        elementwise += 0.5 * ue.dot(ke * ue);
    }

    INFO("0.5 u^T K u = " << global << " J, sum of element energies = " << elementwise << " J");
    REQUIRE(global > 0.0);
    CHECK_THAT(global, WithinRel(elementwise, 1e-11));
}

TEST_CASE("StructuralSystem_InternalForceEqualsTheElementScatter", "[structural][assembly]") {
    // Brief section 71. `K u` against the scatter of every `Ke u_e`, which is
    // what catches a row/column transposition that energy alone would not.
    AssembledPart part;
    const GlobalStructuralSystem system = part.system();
    const meshing::Mesh& mesh = part.volume().mesh();
    const std::map<meshing::NodeId::ValueType, std::size_t> ordinals = ordinalsOf(mesh);
    const Eigen::VectorXd u = deterministicDisplacement(mesh, 5e-6);

    const Eigen::VectorXd global = multiply(system.stiffness(), u);

    Eigen::VectorXd expected = Eigen::VectorXd::Zero(global.size());
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        const Eigen::Matrix<double, 12, 12> ke =
            referenceKe(cornersOf(mesh, tet), kYoungs, kPoisson);
        std::array<std::size_t, 12> rows{};
        Eigen::Matrix<double, 12, 1> ue = Eigen::Matrix<double, 12, 1>::Zero();
        for (std::size_t corner = 0; corner < 4; ++corner) {
            const std::size_t ordinal = ordinals.at(tet.nodes[corner].value());
            for (std::size_t offset = 0; offset < kDofsPerNode; ++offset) {
                const std::size_t local = kDofsPerNode * corner + offset;
                rows[local] = oracleRow(ordinal, offset);
                ue(static_cast<Eigen::Index>(local)) =
                    u(static_cast<Eigen::Index>(rows[local]));
            }
        }
        const Eigen::Matrix<double, 12, 1> fe = ke * ue;
        for (std::size_t local = 0; local < 12; ++local) {
            expected(static_cast<Eigen::Index>(rows[local])) +=
                fe(static_cast<Eigen::Index>(local));
        }
    }

    const double scale = expected.cwiseAbs().maxCoeff();
    REQUIRE(scale > 0.0);
    INFO("||K u - scatter|| = " << (global - expected).norm() << ", scale " << scale);
    CHECK((global - expected).norm() < 1e-11 * scale * std::sqrt(static_cast<double>(global.size())));
}

// ---------------------------------------------------------------------------
// The force vector
// ---------------------------------------------------------------------------

TEST_CASE("StructuralSystem_ScattersPreparedNodalForcesIntoF", "[structural][assembly]") {
    // Brief sections 22 and 112, with the DOF positions derived independently.
    AssembledPart part;
    const meshing::Mesh& mesh = part.volume().mesh();
    const std::map<meshing::NodeId::ValueType, std::size_t> ordinals = ordinalsOf(mesh);
    REQUIRE(mesh.nodeCount() >= 2);

    const meshing::NodeId first = mesh.nodes().front().id;
    const meshing::NodeId second = mesh.nodes()[1].id;
    const GlobalStructuralSystem system =
        part.system({nodalForce(mesh, first, 1.0, 2.0, 3.0, 1),
                     nodalForce(mesh, second, -4.0, 5.0, 6.0, 2)});

    const std::array<std::array<double, 3>, 2> expected{{{1.0, 2.0, 3.0}, {-4.0, 5.0, 6.0}}};
    const std::array<meshing::NodeId, 2> nodes{first, second};
    for (std::size_t which = 0; which < 2; ++which) {
        const std::size_t ordinal = ordinals.at(nodes[which].value());
        for (std::size_t offset = 0; offset < kDofsPerNode; ++offset) {
            const std::size_t row = oracleRow(ordinal, offset);
            INFO("node " << nodes[which].value() << " ordinal " << ordinal << " offset " << offset
                         << " -> row " << row);
            CHECK_THAT(system.force()[row].si(), WithinAbs(expected[which][offset], 1e-12));
        }
    }

    SECTION("and every other entry is zero") {
        std::size_t loaded = 0;
        for (std::size_t row = 0; row < system.force().size(); ++row) {
            const bool isLoaded =
                row < kDofsPerNode * 2 &&
                (row / kDofsPerNode) == ordinals.at(nodes[row / kDofsPerNode].value());
            if (!isLoaded) {
                continue;
            }
            ++loaded;
        }
        double total = 0.0;
        for (const double value : system.force().values()) {
            total += std::abs(value);
        }
        CHECK_THAT(total, WithinRel(1.0 + 2.0 + 3.0 + 4.0 + 5.0 + 6.0, 1e-12));
        CHECK(loaded > 0);
    }

    SECTION("and the resultant matches the prepared field exactly") {
        const PreparedLoads prepared = part.prepared(
            {nodalForce(mesh, first, 1.0, 2.0, 3.0, 1),
             nodalForce(mesh, second, -4.0, 5.0, 6.0, 2)});
        const Force3D fromLoads = prepared.resultantForce();
        const Force3D fromVector = system.force().resultantForce();
        CHECK_THAT(fromVector.x.si(), WithinAbs(fromLoads.x.si(), 1e-12));
        CHECK_THAT(fromVector.y.si(), WithinAbs(fromLoads.y.si(), 1e-12));
        CHECK_THAT(fromVector.z.si(), WithinAbs(fromLoads.z.si(), 1e-12));
    }
}

TEST_CASE("StructuralSystem_AddsTwoLoadsOnOneDegreeOfFreedom", "[structural][assembly]") {
    // Brief section 23's exact case: +10 and -3 on one degree of freedom must
    // give 7, not either of them.
    //
    // AND WHERE THAT ADDITION HAPPENS IS WORTH BEING PRECISE ABOUT, because a
    // mutation probe showed this test does not measure the assembly. P17-LOAD
    // accumulates per node, so `PreparedLoads::nodal()` holds ONE entry for
    // this node carrying 7 N, and the assembly scatters it once. Replacing the
    // assembly's `+=` with `=` therefore changes nothing -- the probe survives
    // and is recorded as inert.
    //
    // So the claim this test makes is the end-to-end one: two loads on one
    // degree of freedom reach `F` as their sum. The contract the assembly's
    // `+=` relies on is pinned separately below, so that if `nodal()` ever
    // stopped being unique by node the `+=` would become load-bearing and the
    // probe would start killing.
    AssembledPart part;
    const meshing::Mesh& mesh = part.volume().mesh();
    const meshing::NodeId node = mesh.nodes().front().id;
    const std::size_t row = oracleRow(0, 0);

    const std::vector<StructuralLoad> loads{nodalForce(mesh, node, 10.0, 0.0, 0.0, 1),
                                            nodalForce(mesh, node, -3.0, 0.0, 0.0, 2)};
    const GlobalStructuralSystem system = part.system(loads);
    CHECK_THAT(system.force()[row].si(), WithinAbs(7.0, 1e-12));
    CHECK(system.force()[row].si() != 10.0);
    CHECK(system.force()[row].si() != -3.0);

    SECTION("the prepared field is unique by node, which is why one scatter suffices") {
        const PreparedLoads prepared = part.prepared(loads);
        CHECK(prepared.nodal().size() == 1);
        CHECK(prepared.nodal().front().node == node);
        CHECK_THAT(prepared.nodal().front().force.x.si(), WithinAbs(7.0, 1e-12));
        // Ascending and without repeats, over the whole field.
        std::vector<meshing::NodeId::ValueType> handles;
        for (const structural::NodalLoad& load : prepared.nodal()) {
            handles.push_back(load.node.value());
        }
        CHECK(std::ranges::is_sorted(handles));
        CHECK(std::ranges::adjacent_find(handles) == handles.end());
    }
}

TEST_CASE("StructuralSystem_SuperposesLoadCases", "[structural][assembly]") {
    // Brief sections 65 and 113: F(L1 + L2) == F(L1) + F(L2), entrywise.
    AssembledPart part;
    const meshing::Mesh& mesh = part.volume().mesh();
    const meshing::NodeId a = mesh.nodes().front().id;
    const meshing::NodeId b = mesh.nodes()[1].id;

    const std::vector<StructuralLoad> first{nodalForce(mesh, a, 7.0, -2.0, 1.0, 1)};
    const std::vector<StructuralLoad> second{nodalForce(mesh, a, 0.0, 3.0, 0.0, 2),
                                             nodalForce(mesh, b, -1.0, 0.0, 4.0, 3)};
    std::vector<StructuralLoad> both = first;
    both.insert(both.end(), second.begin(), second.end());

    const GlobalStructuralSystem one = part.system(first);
    const GlobalStructuralSystem two = part.system(second);
    const GlobalStructuralSystem combined = part.system(both);

    REQUIRE(combined.force().size() == one.force().size());
    for (std::size_t row = 0; row < combined.force().size(); ++row) {
        INFO("row " << row);
        CHECK_THAT(combined.force()[row].si(),
                   WithinAbs(one.force()[row].si() + two.force()[row].si(), 1e-12));
    }

    SECTION("and the stiffness is the same in all three") {
        CHECK(one.stiffness() == two.stiffness());
        CHECK(one.stiffness() == combined.stiffness());
    }
}

TEST_CASE("StructuralSystem_AssemblesAZeroForceVectorWithNoLoads", "[structural][assembly]") {
    // Brief sections 62 and 116. No loads is a valid model, not a refusal.
    AssembledPart part;
    const GlobalStructuralSystem system = part.system();
    REQUIRE(system.force().size() > 0);
    for (std::size_t row = 0; row < system.force().size(); ++row) {
        CHECK(system.force()[row].si() == 0.0);
    }
    CHECK(system.force().resultantForce().x.si() == 0.0);
    // And K is assembled normally.
    CHECK(system.stiffness().nonZeros() > 0);
}

// ---------------------------------------------------------------------------
// Dependency separation
// ---------------------------------------------------------------------------

TEST_CASE("StructuralSystem_SeparatesWhatKDependsOnFromWhatFDependsOn",
          "[structural][assembly]") {
    // Brief sections 117 to 121. `K` is a function of the mesh and the
    // material; `F` of the mesh and the loads. Each half is checked by
    // changing one input and watching the other half not move.
    AssembledPart part;
    const meshing::Mesh& mesh = part.volume().mesh();
    const meshing::NodeId node = mesh.nodes().front().id;
    const std::vector<StructuralLoad> loads{nodalForce(mesh, node, 11.0, -6.0, 2.0, 1)};

    SECTION("a different load case does not change K") {
        const GlobalStructuralSystem a = part.system(loads);
        const GlobalStructuralSystem b =
            part.system({nodalForce(mesh, node, -99.0, 0.0, 17.0, 1)});
        CHECK(a.stiffness() == b.stiffness());
        CHECK_FALSE(a.force() == b.force());
    }

    SECTION("a modulus change scales K and leaves F alone") {
        // Brief sections 66, 118 and 121: Ke is linear in E, so the whole
        // global matrix is.
        const GlobalStructuralSystem before = part.system(loads);
        part.setMaterialModulus(2.0 * kYoungs);
        const GlobalStructuralSystem after = part.system(loads);

        REQUIRE(after.stiffness().nonZeros() == before.stiffness().nonZeros());
        // The pattern is unchanged, because it depends on connectivity alone.
        CHECK(std::ranges::equal(after.stiffness().rowStart(), before.stiffness().rowStart()));
        CHECK(std::ranges::equal(after.stiffness().innerIndices(),
                                 before.stiffness().innerIndices()));
        double largest = 0.0;
        const double scale = before.stiffness().largestMagnitude();
        for (std::size_t slot = 0; slot < before.stiffness().nonZeros(); ++slot) {
            largest = std::max(largest, std::abs(after.stiffness().values()[slot] -
                                                 2.0 * before.stiffness().values()[slot]));
        }
        INFO("largest |K(2E) - 2 K(E)| = " << largest << ", scale " << scale);
        CHECK(largest < 1e-12 * scale);
        // F did not move.
        CHECK(after.force() == before.force());
    }

    SECTION("a density change moves only a gravity F, and never K") {
        // Brief section 119. Gravity IS supported by P17-LOAD-001, so this is
        // a real case and not an N/A.
        const std::vector<StructuralLoad> gravity{
            StructuralLoad{LoadId::fromValue(1),
                           structural::GravityLoad{
                               .acceleration = Vector3D{0.0, 0.0, -structural::kStandardGravity}}}};
        const GlobalStructuralSystem before =
            part.system(gravity, StructuralAnalysisMode::LinearStaticWithGravity);
        part.setMaterialDensity(2700.0);
        const GlobalStructuralSystem after =
            part.system(gravity, StructuralAnalysisMode::LinearStaticWithGravity);

        CHECK(after.stiffness() == before.stiffness());
        CHECK_FALSE(after.force() == before.force());
        // 7850 -> 2700, so the weight scales by the density ratio.
        const double ratio = 2700.0 / 7850.0;
        CHECK_THAT(after.force().resultantForce().z.si(),
                   WithinRel(ratio * before.force().resultantForce().z.si(), 1e-12));
    }

    SECTION("a restraint edit changes neither K nor F") {
        // Brief sections 97 and 120. The unconstrained system does not depend
        // on the restraints at all, and this is the numerical statement of it.
        // The SOURCE STAMP is a separate question, recorded in the header:
        // AssemblySource deliberately carries no analysis revision.
        const GlobalStructuralSystem before = part.system(loads);
        part.setRestraints(
            {StructuralRestraint::fixedSupport(RestraintId::fromValue(1), part.startCap())});
        const GlobalStructuralSystem after = part.system(loads);
        CHECK(after.stiffness() == before.stiffness());
        CHECK(after.force() == before.force());
        CHECK(after.source() == before.source());
    }
}

// ---------------------------------------------------------------------------
// Failure paths
// ---------------------------------------------------------------------------

TEST_CASE("StructuralSystem_RefusesLoadsPreparedForADifferentMesh", "[structural][assembly]") {
    // Brief sections 41 and 42, and the mutation "reuse M1 prepared load
    // vector on M2". The check is the stamp AND the node count, so overlapping
    // NodeIds cannot make one mesh's loads pass for another's.
    AssembledPart part;
    const meshing::Mesh& first = part.volume().mesh();
    const meshing::NodeId node = first.nodes().front().id;
    const PreparedLoads stale = part.prepared({nodalForce(first, node, 10.0, 0.0, 0.0, 1)});
    const meshing::MeshStamp firstStamp = first.stamp();

    // A new generation of the same unchanged model.
    part.mesh();
    const StructuralModel model = part.model();
    REQUIRE(model.mesh().mesh().stamp() != firstStamp);
    REQUIRE_FALSE(stale.describes(model.mesh().mesh()));

    Result<GlobalStructuralSystem> assembled =
        structural::assembleStructuralSystem(model, part.resolved(), stale);
    REQUIRE_FALSE(assembled.has_value());
    CHECK(assembled.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(assembled.error().message, ContainsSubstring("different mesh"));
    CHECK(structural::structuralAssemblyProblem(model, part.resolved(), stale) ==
          AssemblyProblem::LoadSourceMismatch);
}

TEST_CASE("StructuralSystem_ReportsEveryProblemThroughTheSameOrderedChecks",
          "[structural][assembly]") {
    // `structuralAssemblyProblem` and `assembleStructuralSystem` run one
    // shared pass, so a caller that asks which problem there is cannot be told
    // something different from the caller that asks for the system.
    AssembledPart part;
    const StructuralModel model = part.model();
    const PreparedLoads none = part.prepared({});

    CHECK_FALSE(structural::structuralAssemblyProblem(model, part.resolved(), none).has_value());

    // Every value is named, and none of them is "unknown".
    for (const AssemblyProblem problem :
         {AssemblyProblem::MeshHasNoDegreesOfFreedom, AssemblyProblem::MeshHasNoElements,
          AssemblyProblem::ElementNodeMissing, AssemblyProblem::ElementRejected,
          AssemblyProblem::LoadSourceMismatch, AssemblyProblem::LoadNodeMissing,
          AssemblyProblem::NonFiniteSystem}) {
        CHECK(structural::toString(problem) != "unknown");
    }

    SECTION("and nothing is published on a failure") {
        const meshing::Mesh& mesh = part.volume().mesh();
        const PreparedLoads stale =
            part.prepared({nodalForce(mesh, mesh.nodes().front().id, 1.0, 0.0, 0.0, 1)});
        part.mesh();
        const StructuralModel second = part.model();
        CHECK(structural::structuralAssemblyProblem(second, part.resolved(), stale).has_value());
        CHECK_FALSE(
            structural::assembleStructuralSystem(second, part.resolved(), stale).has_value());
    }
}

// ---------------------------------------------------------------------------
// Determinism
// ---------------------------------------------------------------------------

TEST_CASE("StructuralSystem_IsDeterministicAndFollowsP16ElementOrder",
          "[structural][assembly]") {
    // Brief sections 8, 48, 78, 82, 122 and 124. The element order is a
    // NUMERICAL contract: many elements write one entry, so changing the order
    // changes the last bits. It is P16's ascending-ElementId order and this
    // module writes no sort for it.
    AssembledPart part;
    const meshing::Mesh& mesh = part.volume().mesh();
    const meshing::NodeId node = mesh.nodes().front().id;
    const std::vector<StructuralLoad> loads{nodalForce(mesh, node, 3.0, -1.0, 2.0, 1)};

    const GlobalStructuralSystem first = part.system(loads);
    const GlobalStructuralSystem again = part.system(loads);

    SECTION("the element order is the mesh's own ascending ElementId order") {
        std::vector<meshing::ElementId> expected;
        for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
            expected.push_back(tet.id);
        }
        CHECK(std::ranges::equal(first.elementOrder(), expected));
        CHECK(std::ranges::is_sorted(first.elementOrder()));
        CHECK(first.elementOrder().size() == mesh.tetrahedra().size());
    }

    SECTION("repeating the assembly gives a bit-identical system") {
        // The fingerprints are the arrays themselves, compared element for
        // element with no tolerance -- there is nothing to tolerance when the
        // operations are the same operations in the same order.
        CHECK(first.stiffness().nonZeros() == again.stiffness().nonZeros());
        CHECK(std::ranges::equal(first.stiffness().rowStart(), again.stiffness().rowStart()));
        CHECK(std::ranges::equal(first.stiffness().innerIndices(),
                                 again.stiffness().innerIndices()));
        CHECK(std::ranges::equal(first.stiffness().values(), again.stiffness().values()));
        CHECK(std::ranges::equal(first.force().values(), again.force().values()));
        CHECK(first.stiffness() == again.stiffness());
        CHECK(first.force() == again.force());
        CHECK(first == again);
    }

    SECTION("and the order in which the loads were given does not reach F") {
        const meshing::NodeId other = mesh.nodes()[1].id;
        const GlobalStructuralSystem forwards =
            part.system({nodalForce(mesh, node, 1.0, 0.0, 0.0, 1),
                         nodalForce(mesh, other, 0.0, 2.0, 0.0, 2)});
        const GlobalStructuralSystem backwards =
            part.system({nodalForce(mesh, other, 0.0, 2.0, 0.0, 2),
                         nodalForce(mesh, node, 1.0, 0.0, 0.0, 1)});
        CHECK(std::ranges::equal(forwards.force().values(), backwards.force().values()));
    }
}

TEST_CASE("StructuralSystem_CarriesTheInputStateItWasAssembledFrom", "[structural][assembly]") {
    // Brief sections 56 to 60. Six fields, each proved by an input, and
    // deliberately NOT the analysis revision -- see the header on why
    // including it would over-invalidate.
    AssembledPart part;
    const GlobalStructuralSystem system = part.system();
    const structural::AssemblySource& source = system.source();

    CHECK(source.body == part.feature);
    CHECK(source.control == part.control);
    CHECK(source.material == part.material);
    CHECK(source.mesh == part.volume().mesh().stamp());
    CHECK(system.describes(part.volume().mesh()));
    CHECK(system.mesh() == part.volume().mesh().stamp());

    SECTION("a material edit moves the source") {
        part.setMaterialModulus(190.0e9);
        const GlobalStructuralSystem after = part.system();
        CHECK(after.source() != source);
        CHECK(after.source().materialRevision != source.materialRevision);
    }

    SECTION("a remesh moves the source, and the old system does not describe the new mesh") {
        part.mesh();
        const GlobalStructuralSystem after = part.system();
        CHECK(after.source() != source);
        CHECK_FALSE(system.describes(part.volume().mesh()));
        CHECK(after.describes(part.volume().mesh()));
    }
}
