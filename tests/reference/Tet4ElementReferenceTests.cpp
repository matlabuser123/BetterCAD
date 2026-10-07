// P17-ELEM-001 against the meshing reference models.
//
// WHY THIS FILE IS SEPARATE FROM tests/structural/Tet4ElementTests.cpp. Those
// tests use tetrahedra chosen to make a formula checkable: the reference
// element, a skew one, a thin one, and deterministic random ones. These use
// every tetrahedron of meshes Netgen actually produced through the ordinary
// P16 pipeline, which is the only way to answer a different question -- not
// "is the formula right" but "does the kernel accept what a qualified mesh
// contains, and agree with P16 about it".
//
// Four claims, and each needs a real mesh:
//
//   every element of a qualified mesh is ACCEPTED. A kernel that refused one
//     would make a mesh the project has qualified unsolvable
//   P17's volume equals P16's signed volume for every element, bit for bit
//   every Ke is finite and symmetric at production scale
//   the whole batch is deterministic, and that is measured by a fingerprint
//     over the ordered per-element results
//
// MODELS: RM-MESH-01 (block), RM-MESH-03 (plate with a through-hole) and
// RM-MESH-07 (local refinement), which the brief names.
//
// Eigenanalysis is SAMPLED rather than run on every element: a 12x12
// decomposition per element over three meshes would dominate the suite's
// runtime for a property the unit tests already establish on chosen geometry.
// The sample is deterministic -- every k-th element -- not random.

#include "reference/MeshTestSupport.hpp"

#include <MeshReferenceModels.hpp>

#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/structural/Tet4Element.hpp>

#include <Eigen/Dense>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using namespace bettercad;
using namespace bettercad::test;
using namespace bettercad::test::meshref;
using Catch::Matchers::WithinRel;
using structural::ElasticityMatrix;
using structural::kTet4Dofs;
using structural::kTet4Nodes;
using structural::Tet4Kinematics;
using structural::Tet4Stiffness;

/// One validated structural material for the whole batch. The material does
/// not affect geometry validation, so one is enough -- and using a realistic
/// steel rather than E = 1 keeps the stiffness magnitudes production-like, so
/// a scale-aware tolerance is being exercised at the scale it will meet.
[[nodiscard]] materials::LinearElasticConstants steel() {
    materials::LinearElasticConstants c;
    c.youngsModulus = ElasticModulus::fromSi(210.0e9);
    c.poissonRatio = PoissonRatio::of(0.3);
    c.shearModulus = ElasticModulus::fromSi(210.0e9 / (2.0 * (1.0 + 0.3)));
    c.bulkModulus = ElasticModulus::fromSi(210.0e9 / (3.0 * (1.0 - 2.0 * 0.3)));
    return c;
}

/// What a whole-mesh pass measured.
struct BatchResult {
    std::size_t elements = 0;
    std::size_t accepted = 0;
    std::size_t sampled = 0;
    double minVolume = 0.0;
    double maxVolume = 0.0;
    /// Largest relative asymmetry of any Ke in the batch.
    double worstAsymmetry = 0.0;
    /// Largest relative disagreement with P16's signed volume. Expected zero.
    double worstVolumeDisagreement = 0.0;
    /// Largest relative rigid-body residual over the sampled elements.
    double worstRigidResidual = 0.0;
    /// A fingerprint of the ordered per-element results, for determinism only.
    std::uint64_t fingerprint = 0;
};

/// Mixes @p value into @p state. A test-side checksum for determinism
/// evidence, deliberately NOT an engineering authority: it says two runs
/// produced the same numbers, nothing about whether the numbers are right.
void mix(std::uint64_t& state, double value) {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    state ^= bits + 0x9e3779b97f4a7c15ULL + (state << 6) + (state >> 2);
}

[[nodiscard]] BatchResult runBatch(const meshing::VolumeMesh& volume, std::size_t sampleEvery) {
    const meshing::Mesh& mesh = volume.mesh();
    const Result<ElasticityMatrix> elasticity = structural::isotropicElasticity(steel());
    REQUIRE(elasticity.has_value());

    BatchResult out;
    out.elements = mesh.tetrahedra().size();
    REQUIRE(out.elements > 0);
    bool first = true;

    std::size_t index = 0;
    for (const meshing::Tetrahedron& tetrahedron : mesh.tetrahedra()) {
        std::array<Point3D, kTet4Nodes> nodes{};
        for (std::size_t corner = 0; corner < kTet4Nodes; ++corner) {
            const meshing::Node* node = mesh.findNode(tetrahedron.nodes[corner]);
            REQUIRE(node != nullptr);
            nodes[corner] = node->position;
        }

        // ACCEPTED, and the diagnostic named if not -- a refusal here would
        // mean a qualified mesh contains an element this kernel cannot
        // evaluate, which is a failure of one of the two and worth saying
        // which.
        const Result<Tet4Kinematics> kinematics = structural::computeTet4Kinematics(nodes);
        if (!kinematics.has_value()) {
            FAIL("element " << tetrahedron.id.value() << " refused: "
                            << kinematics.error().message);
        }
        ++out.accepted;

        const double v = kinematics->volume().si();
        if (!(v > 0.0)) {
            FAIL("element " << tetrahedron.id.value() << " has volume " << v);
        }
        if (first) {
            out.minVolume = v;
            out.maxVolume = v;
            first = false;
        } else {
            out.minVolume = std::min(out.minVolume, v);
            out.maxVolume = std::max(out.maxVolume, v);
        }

        // P16's own signed volume, for the same four points in the same order.
        const Volume p16 =
            meshing::signedVolume(nodes[0], nodes[1], nodes[2], nodes[3]);
        const double disagreement = std::abs(v - p16.si()) / std::abs(p16.si());
        out.worstVolumeDisagreement = std::max(out.worstVolumeDisagreement, disagreement);

        const Result<Tet4Stiffness> stiffness =
            structural::computeTet4Stiffness(*kinematics, *elasticity);
        if (!stiffness.has_value()) {
            FAIL("element " << tetrahedron.id.value() << " stiffness refused: "
                            << stiffness.error().message);
        }

        Eigen::Matrix<double, 12, 12> ke;
        for (std::size_t row = 0; row < kTet4Dofs; ++row) {
            for (std::size_t column = 0; column < kTet4Dofs; ++column) {
                ke(static_cast<int>(row), static_cast<int>(column)) =
                    (*stiffness)(row, column).si();
            }
        }
        if (!ke.allFinite()) {
            FAIL("element " << tetrahedron.id.value() << " has a non-finite stiffness");
        }
        const double norm = ke.cwiseAbs().maxCoeff();
        REQUIRE(norm > 0.0);
        out.worstAsymmetry = std::max(out.worstAsymmetry,
                                      (ke - ke.transpose()).cwiseAbs().maxCoeff() / norm);

        // The fingerprint covers the element handle, its volume and a fixed
        // spread of stiffness entries, in element order.
        mix(out.fingerprint, static_cast<double>(tetrahedron.id.value()));
        mix(out.fingerprint, v);
        for (const std::size_t entry : {std::size_t{0}, std::size_t{17}, std::size_t{70},
                                        std::size_t{143}}) {
            mix(out.fingerprint, ke(static_cast<int>(entry / kTet4Dofs),
                                    static_cast<int>(entry % kTet4Dofs)));
        }

        // SAMPLED: the six rigid-body modes on every k-th element.
        if (index % sampleEvery == 0) {
            ++out.sampled;
            Eigen::Vector3d centroid = Eigen::Vector3d::Zero();
            for (const Point3D& node : nodes) {
                centroid += Eigen::Vector3d(node.x.si(), node.y.si(), node.z.si());
            }
            centroid /= 4.0;
            for (int axis = 0; axis < 3; ++axis) {
                Eigen::Matrix<double, 12, 1> translation = Eigen::Matrix<double, 12, 1>::Zero();
                Eigen::Matrix<double, 12, 1> rotation = Eigen::Matrix<double, 12, 1>::Zero();
                Eigen::Vector3d omega = Eigen::Vector3d::Zero();
                omega(axis) = 1.0;
                for (int node = 0; node < 4; ++node) {
                    const Eigen::Vector3d x(nodes[static_cast<std::size_t>(node)].x.si(),
                                            nodes[static_cast<std::size_t>(node)].y.si(),
                                            nodes[static_cast<std::size_t>(node)].z.si());
                    const Eigen::Vector3d u = omega.cross(x - centroid);
                    translation(3 * node + axis) = 1.0;
                    for (int component = 0; component < 3; ++component) {
                        rotation(3 * node + component) = u(component);
                    }
                }
                for (const Eigen::Matrix<double, 12, 1>& mode : {translation, rotation}) {
                    const double residual =
                        (ke * mode).cwiseAbs().maxCoeff() / (norm * mode.norm());
                    out.worstRigidResidual = std::max(out.worstRigidResidual, residual);
                }
            }
        }
        ++index;
    }
    return out;
}

} // namespace

TEST_CASE("Tet4Element_EvaluatesEveryElementOfTheMeshingReferenceModels",
          "[structural][elem][reference]") {
    struct Model {
        const char* id;
        Result<Document> (*build)();
        std::size_t sampleEvery;
    };

    SECTION("RM-MESH-01, a block") {
        auto built = reference::buildMeshBlockReferenceModel();
        REQUIRE(built.has_value());
        MeshedReference model(std::move(built->document));
        const meshing::VolumeMesh& volume = model.require();
        const BatchResult r = runBatch(volume, 1);
        INFO("RM-MESH-01: " << r.elements << " elements, " << r.sampled << " sampled, volumes "
                            << r.minVolume << " .. " << r.maxVolume << " m^3, worst asymmetry "
                            << r.worstAsymmetry << ", worst volume disagreement "
                            << r.worstVolumeDisagreement << ", worst rigid residual "
                            << r.worstRigidResidual);
        CHECK(r.accepted == r.elements);
        CHECK(r.minVolume > 0.0);
        CHECK(r.worstAsymmetry <= 1e-12);
        // Bit-for-bit, because the determinant is grouped exactly as P16
        // groups it. A non-zero value here would mean the two predicates can
        // disagree at the accept/reject boundary.
        CHECK(r.worstVolumeDisagreement == 0.0);
        CHECK(r.worstRigidResidual <= 1e-11);
    }

    SECTION("RM-MESH-03, a plate with a through-hole") {
        auto built = reference::buildMeshPlateWithHoleReferenceModel();
        REQUIRE(built.has_value());
        MeshedReference model(std::move(built->document));
        const meshing::VolumeMesh& volume = model.require();
        const BatchResult r = runBatch(volume, 7);
        INFO("RM-MESH-03: " << r.elements << " elements, " << r.sampled << " sampled, volumes "
                            << r.minVolume << " .. " << r.maxVolume << " m^3, worst asymmetry "
                            << r.worstAsymmetry << ", worst volume disagreement "
                            << r.worstVolumeDisagreement << ", worst rigid residual "
                            << r.worstRigidResidual);
        CHECK(r.accepted == r.elements);
        CHECK(r.minVolume > 0.0);
        CHECK(r.worstAsymmetry <= 1e-12);
        CHECK(r.worstVolumeDisagreement == 0.0);
        CHECK(r.worstRigidResidual <= 1e-11);
    }

    SECTION("RM-MESH-07, local refinement, so element sizes vary deliberately") {
        auto built = reference::buildMeshLocalRefinementReferenceModel();
        REQUIRE(built.has_value());
        MeshedReference model(std::move(built->document));
        const meshing::VolumeMesh& volume = model.require();
        const BatchResult r = runBatch(volume, 5);
        INFO("RM-MESH-07: " << r.elements << " elements, " << r.sampled << " sampled, volumes "
                            << r.minVolume << " .. " << r.maxVolume << " m^3, worst asymmetry "
                            << r.worstAsymmetry << ", worst volume disagreement "
                            << r.worstVolumeDisagreement << ", worst rigid residual "
                            << r.worstRigidResidual);
        CHECK(r.accepted == r.elements);
        CHECK(r.minVolume > 0.0);
        // The refined face gives elements of very different sizes, which is
        // the point of this model: a scale-dependent defect would show as an
        // asymmetry that grows with the spread.
        CHECK(r.maxVolume > r.minVolume);
        CHECK(r.worstAsymmetry <= 1e-12);
        CHECK(r.worstVolumeDisagreement == 0.0);
        CHECK(r.worstRigidResidual <= 1e-11);
    }
}

TEST_CASE("Tet4Element_SumsToTheMeshVolumeOverAWholeReferenceMesh",
          "[structural][elem][reference]") {
    // An INDEPENDENT whole-mesh check: the element volumes this kernel computes
    // must sum to the mesh's tetrahedral volume, which P16 computed by its own
    // route. That is a different claim from per-element agreement -- it would
    // catch a kernel that was right element by element and was handed the wrong
    // connectivity.
    auto built = reference::buildMeshPlateWithHoleReferenceModel();
    REQUIRE(built.has_value());
    MeshedReference model(std::move(built->document));
    const meshing::VolumeMesh& volume = model.require();
    const meshing::Mesh& mesh = volume.mesh();

    double total = 0.0;
    for (const meshing::Tetrahedron& tetrahedron : mesh.tetrahedra()) {
        std::array<Point3D, kTet4Nodes> nodes{};
        for (std::size_t corner = 0; corner < kTet4Nodes; ++corner) {
            const meshing::Node* node = mesh.findNode(tetrahedron.nodes[corner]);
            REQUIRE(node != nullptr);
            nodes[corner] = node->position;
        }
        const Result<Tet4Kinematics> kinematics = structural::computeTet4Kinematics(nodes);
        REQUIRE(kinematics.has_value());
        total += kinematics->volume().si();
    }

    INFO("summed element volumes " << total << " m^3, P16 tetrahedral volume "
                                   << volume.tetrahedralVolume().si() << " m^3");
    // Accumulation over thousands of additions in a different order from
    // P16's, so a relative bound rather than equality.
    CHECK_THAT(total, WithinRel(volume.tetrahedralVolume().si(), 1e-12));
}

TEST_CASE("Tet4Element_IsDeterministicOverAWholeReferenceMesh",
          "[structural][elem][reference]") {
    // The batch fingerprint, repeated. Determinism evidence only: it says two
    // passes produced the same numbers, and says nothing about whether they
    // are right -- the analytical and independent-reference tests do that.
    auto built = reference::buildMeshBlockReferenceModel();
    REQUIRE(built.has_value());
    MeshedReference model(std::move(built->document));
    const meshing::VolumeMesh& volume = model.require();

    const BatchResult first = runBatch(volume, 1);
    for (int repeat = 0; repeat < 4; ++repeat) {
        const BatchResult again = runBatch(volume, 1);
        INFO("repeat " << repeat << ": fingerprint " << again.fingerprint << " vs "
                       << first.fingerprint);
        CHECK(again.fingerprint == first.fingerprint);
        CHECK(again.elements == first.elements);
        CHECK(again.minVolume == first.minVolume);
        CHECK(again.maxVolume == first.maxVolume);
        CHECK(again.worstAsymmetry == first.worstAsymmetry);
    }
}
