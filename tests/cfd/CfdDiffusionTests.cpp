/// \file
/// P19-DIFF-001 — the finite-volume diffusion operator, ADR-069.
///
/// WHERE EACH CLAIM IS CHECKED, AND WHY IT IS CHECKED THERE. No P16 reference
/// mesh is near orthogonal -- the best maximum is the Block's 64.09 degrees, and
/// ADR-063 Finding F1 records the whole table -- so the arithmetic cannot be
/// checked on one. It is checked on two hand-built fixtures whose every quantity
/// is known in closed form:
///
/// ```text
/// the orthogonal pair   two tets sharing a face on x + y + z = 10 mm, whose
///                       centroids are (2.5,2.5,2.5) and (5,5,5), so d is
///                       EXACTLY parallel to n and theta is exactly zero. The
///                       deferred arm is identically zero there
/// the analytic pair     |d| = 5/4, d . n = 3/4, cos(theta) = 3/5 exactly, so
///                       the coefficient, the implicit arm and the deferred arm
///                       are all rationals
/// ```
///
/// The reference meshes carry the STRUCTURAL claims instead -- every cell
/// visited, the pattern matching the adjacency, the signs, determinism -- where
/// being poorly shaped does not matter.
///
/// THE ORACLES ARE WRITTEN OUT AGAIN, not read from the implementation. The
/// coefficient `mu A / (d . n)`, the three decompositions and the one-dimensional
/// problem each appear here in their own arithmetic, so a transposition in
/// production cannot be mirrored here.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "reference/MeshTestSupport.hpp"

#include <MeshReferenceModels.hpp>

#include <bettercad/cfd/CfdBoundary.hpp>
#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdDiffusion.hpp>
#include <bettercad/cfd/CfdGeometry.hpp>
#include <bettercad/cfd/CfdSparse.hpp>
#include <bettercad/cfd/CfdTopology.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/materials/ThermalProperties.hpp>
#include <bettercad/core/math/Vector.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/meshing/Mesh.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;

using cfd::BoundaryFaceIndex;
using cfd::CellIndex;
using cfd::CfdBoundaryCondition;
using cfd::CfdBoundaryConditionKind;
using cfd::CfdBoundaryPolicy;
using cfd::CfdDiffusionInput;
using cfd::CfdDiffusionProblem;
using cfd::CfdDiffusionSummary;
using cfd::CfdDiffusionSystem;
using cfd::CfdFaceDecomposition;
using cfd::CfdGeometry;
using cfd::CfdResolvedTarget;
using cfd::CfdSparseMatrix;
using cfd::CfdSparseProblem;
using cfd::CfdTopology;
using cfd::FaceIndex;
using cfd::MovingWall;
using cfd::NoSlipWall;
using cfd::PreparedCfdBoundary;
using cfd::PressureOutlet;
using cfd::SlipWall;
using cfd::VectorComponent;
using cfd::Velocity3;
using cfd::VelocityGradient;
using cfd::VelocityGradient3;
using meshing::ElementId;
using meshing::NodeId;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

const meshing::RegionId kRegion = meshing::RegionId::fromValue(1);

/// The four faces of a tetrahedron, wound outward for a positively oriented
/// node order. The same table `CfdBoundaryTests.cpp` uses.
constexpr std::array<std::array<std::size_t, 3>, 4> kFaces{{{0, 2, 1}, {0, 1, 3}, {1, 2, 3}, {0, 3, 2}}};

using FaceUse = std::pair<std::size_t, std::array<std::size_t, 3>>;

[[nodiscard]] std::map<std::array<std::size_t, 3>, FaceUse>
faceUses(const std::vector<std::array<std::size_t, 4>>& tets) {
    std::map<std::array<std::size_t, 3>, FaceUse> uses;
    for (const auto& tet : tets) {
        for (const auto& face : kFaces) {
            std::array<std::size_t, 3> wound{tet[face[0]], tet[face[1]], tet[face[2]]};
            std::array<std::size_t, 3> key = wound;
            std::ranges::sort(key);
            auto found = uses.find(key);
            if (found == uses.end()) {
                uses.emplace(key, FaceUse{1, wound});
            } else {
                ++found->second.first;
            }
        }
    }
    return uses;
}

[[nodiscard]] meshing::Mesh buildMesh(const std::vector<Point3D>& points,
                                      const std::vector<std::array<std::size_t, 4>>& tets) {
    meshing::MeshBuilder builder;
    std::vector<NodeId> nodes;
    for (const Point3D& p : points) {
        nodes.push_back(require(builder.addNode(p)));
    }
    for (const auto& tet : tets) {
        REQUIRE(builder.addTetrahedron({nodes[tet[0]], nodes[tet[1]], nodes[tet[2]], nodes[tet[3]]},
                                       kRegion));
    }
    for (const auto& [key, use] : faceUses(tets)) {
        const auto& [count, wound] = use;
        if (count == 1) {
            REQUIRE(builder.addTriangle({nodes[wound[0]], nodes[wound[1]], nodes[wound[2]]}, kRegion));
        }
    }
    return builder.build();
}

/// Two cells sharing one face on `x + y + z = 10 mm`, where `d` is EXACTLY
/// parallel to the face normal. The one orthogonal internal face available.
[[nodiscard]] std::vector<Point3D> orthogonalPoints() {
    return {Point3D{0_mm, 0_mm, 0_mm},  Point3D{10_mm, 0_mm, 0_mm},  Point3D{0_mm, 10_mm, 0_mm},
            Point3D{0_mm, 0_mm, 10_mm}, Point3D{10_mm, 10_mm, 10_mm}};
}
const std::vector<std::array<std::size_t, 4>> kTwoCells{{0, 1, 2, 3}, {1, 2, 3, 4}};

/// One cell: four boundary faces and no internal face.
[[nodiscard]] std::vector<Point3D> oneCellPoints() {
    return {Point3D{0_mm, 0_mm, 0_mm}, Point3D{10_mm, 0_mm, 0_mm}, Point3D{0_mm, 10_mm, 0_mm},
            Point3D{0_mm, 0_mm, 10_mm}};
}
const std::vector<std::array<std::size_t, 4>> kOneCell{{0, 1, 2, 3}};

/// The analytic pair, in METRES, from `CfdGeometryTests.cpp`'s own fixture:
/// `|d| = 5/4`, `d . n = 3/4`, `cos(theta) = 3/5`, all exact.
[[nodiscard]] std::vector<Point3D> analyticPoints() {
    const auto p = [](double x, double y, double z) {
        return Point3D{Length::fromSi(x), Length::fromSi(y), Length::fromSi(z)};
    };
    return {p(0, 0, 0), p(1, 0, 0), p(0, 1, 0), p(0, 0, -1), p(0, 4, 2)};
}
const std::vector<std::array<std::size_t, 4>> kAnalyticPair{{0, 2, 1, 3}, {0, 1, 2, 4}};

[[nodiscard]] std::vector<meshing::MappedFace> onePatch(const meshing::Mesh& mesh) {
    meshing::MappedFace face;
    face.index = 0;
    for (const meshing::Triangle& triangle : mesh.triangles()) {
        face.facets.push_back(triangle.id);
    }
    return {face};
}

[[nodiscard]] FaceName someFace(std::uint64_t feature = 1) {
    return FaceName{ObjectId::fromValue(feature), FaceSelector{.role = FaceRole::EndCap}};
}

/// A mesh, its topology, its metrics and a prepared boundary, all through the
/// plain builders, so the operator can be shown configurations a document
/// cannot produce.
struct Hand {
    meshing::Mesh mesh;
    CfdTopology topology;
    CfdGeometry geometry;

    [[nodiscard]] static Hand make(const std::vector<Point3D>& points,
                                   const std::vector<std::array<std::size_t, 4>>& tets) {
        meshing::Mesh mesh = buildMesh(points, tets);
        Result<CfdTopology> topology = cfd::buildCfdTopology(mesh, onePatch(mesh));
        INFO((topology.has_value() ? std::string{} : topology.error().message));
        REQUIRE(topology.has_value());
        Result<CfdGeometry> geometry = cfd::buildCfdGeometry(mesh, *topology);
        INFO((geometry.has_value() ? std::string{} : geometry.error().message));
        REQUIRE(geometry.has_value());
        return Hand{std::move(mesh), std::move(*topology), std::move(*geometry)};
    }

    /// Every boundary facet, in `BoundaryFaceIndex` order.
    [[nodiscard]] std::vector<ElementId> facets() const {
        std::vector<ElementId> out;
        for (std::size_t b = 0; b < topology.boundaryFaces(); ++b) {
            out.push_back(topology.triangleOf(BoundaryFaceIndex::fromValue(b)));
        }
        return out;
    }

    /// A point strictly inside cell 0, for a reference pressure: the centroid,
    /// which `locatePoint` will place in that cell.
    [[nodiscard]] Point3D insidePoint() const {
        const std::optional<Point3D> centroid = geometry.cellCentroid(CellIndex::fromValue(0));
        REQUIRE(centroid.has_value());
        return *centroid;
    }

    /// A boundary where every facet carries @p condition.
    [[nodiscard]] PreparedCfdBoundary uniform(const CfdBoundaryCondition& condition) const {
        // ASCENDING AND WITHOUT REPEATS, which is `CfdResolvedTarget`'s own
        // documented contract -- `boundaryFacetsOf` returns them that way.
        // `facets()` returns them in BoundaryFaceIndex order, which for the
        // analytic pair is NOT ascending in ElementId, so sorting here is the
        // contract and not a convenience.
        std::vector<ElementId> ascending = facets();
        std::ranges::sort(ascending, [](ElementId a, ElementId b) { return a.value() < b.value(); });
        REQUIRE(std::ranges::adjacent_find(ascending) == ascending.end());
        const CfdResolvedTarget target{condition, ascending};
        const std::array<CfdResolvedTarget, 1> targets{target};
        // ADR-067 Decision 7 refuses BOTH arms of the pressure datum: a
        // reference pressure is required when no condition fixes the pressure
        // and refused when one does. So the datum is passed exactly when the
        // condition's own pressure policy is not FixedValue, read off ADR-067's
        // table rather than decided here.
        const bool conditionFixesPressure =
            cfd::treatmentOf(condition.kind()).pressure == CfdBoundaryPolicy::FixedValue;
        const std::optional<cfd::CfdReferencePressure> datum =
            conditionFixesPressure ? std::nullopt
                                   : std::optional<cfd::CfdReferencePressure>{
                                         cfd::CfdReferencePressure{.value = 0_Pa,
                                                                   .location = insidePoint()}};
        Result<PreparedCfdBoundary> bound = cfd::bindCfdBoundary(
            topology, geometry, mesh, targets, 0, datum, std::nullopt, std::nullopt);
        INFO((bound.has_value() ? std::string{} : bound.error().message));
        REQUIRE(bound.has_value());
        return std::move(*bound);
    }
};

/// THE COEFFICIENT ORACLE, written out again rather than read from the
/// operator: `Gamma_f = mu |S_f| / (d . n)`.
[[nodiscard]] double gammaOracle(const CfdGeometry& geometry, FaceIndex face, DynamicViscosity mu) {
    const AreaVector3D s = geometry.faceAreaVector(face);
    const double area = std::hypot(s.x.si(), s.y.si(), s.z.si());
    return mu.si() * area / geometry.orthogonalDistance(face).si();
}

/// `|T| / |E|` for each of the three standard decompositions, in closed form,
/// so the operator's choice is pinned against the two it did NOT make. This is
/// the ratio that governs a deferred-correction iteration, and the one
/// `CfdDiffusionSummary::deferredFraction` reports -- NOT `|T| / |S|`, which has
/// different closed forms. ADR-069's Options table carries both columns.
[[nodiscard]] double overRelaxedRatio(double thetaRadians) { return std::sin(thetaRadians); }
[[nodiscard]] double minimumCorrectionRatio(double thetaRadians) { return std::tan(thetaRadians); }
[[nodiscard]] double orthogonalCorrectionRatio(double thetaRadians) {
    return std::sqrt(2.0 - 2.0 * std::cos(thetaRadians));
}

[[nodiscard]] CfdDiffusionInput plainInput(DynamicViscosity mu, VectorComponent component) {
    CfdDiffusionInput input;
    input.viscosity = mu;
    input.component = component;
    return input;
}

[[nodiscard]] double maxAbs(std::span<const double> values) {
    double largest = 0.0;
    for (const double value : values) {
        largest = std::max(largest, std::abs(value));
    }
    return largest;
}

} // namespace

// ---------------------------------------------------------------------------
// The coefficient, in closed form
// ---------------------------------------------------------------------------

TEST_CASE("CfdDiffusion_AnOrthogonalFaceDefersNothing", "[cfd][diff]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    REQUIRE(hand.topology.internalFaces() == 1);

    // The one internal face is exactly orthogonal, so the deferred arm is
    // identically zero and the implicit arm IS the area vector.
    FaceIndex internal = FaceIndex::fromValue(0);
    for (std::size_t f = 0; f < hand.topology.faces(); ++f) {
        if (hand.topology.isInternal(FaceIndex::fromValue(f))) {
            internal = FaceIndex::fromValue(f);
        }
    }
    const std::optional<CfdFaceDecomposition> split = cfd::cfdFaceDecomposition(hand.geometry, internal);
    REQUIRE(split.has_value());

    const AreaVector3D s = hand.geometry.faceAreaVector(internal);
    // `tan(0) == 0`, so every decomposition agrees here -- which is exactly why
    // this fixture cannot distinguish them and the analytic pair below must.
    // RELATIVE TO THE FACE'S OWN AREA, not an absolute 1e-18. A review pointed
    // out that an absolute bound on a quantity in square metres is tied to this
    // fixture being expressed in millimetres: the same geometry in metres has
    // arms about 1e6 times larger and a correct operator would fail it. The
    // residue is a cancellation at the scale of |S_f|, so that is the scale the
    // bound belongs on.
    const double faceArea = hand.geometry.faceArea(internal).si();
    REQUIRE(faceArea > 0.0);
    const double deferredNorm =
        std::hypot(split->deferredArm.x.si(), split->deferredArm.y.si(), split->deferredArm.z.si());
    CHECK(deferredNorm <= 64.0 * std::numeric_limits<double>::epsilon() * faceArea);
    // And the ratio is dimensionless, so a scale-free bound is right for it.
    // 1e-14 rather than 1e-15: the fixture's own cosine computes to one eps
    // ABOVE one, so this quantity is rounding noise of order 2e-16 and a
    // 1e-15 bound left only a factor of four.
    CHECK_THAT(split->deferredFraction, WithinAbs(0.0, 1e-14));
    CHECK_THAT(split->implicitArm.x.si(), WithinRel(s.x.si(), 1e-15));
    CHECK_THAT(split->implicitArm.y.si(), WithinRel(s.y.si(), 1e-15));
    CHECK_THAT(split->implicitArm.z.si(), WithinRel(s.z.si(), 1e-15));
}

TEST_CASE("CfdDiffusion_TheAnalyticFacePairFixesTheDeferredArmInClosedForm", "[cfd][diff]") {
    const Hand hand = Hand::make(analyticPoints(), kAnalyticPair);
    REQUIRE(hand.topology.internalFaces() == 1);
    FaceIndex internal = FaceIndex::fromValue(0);
    for (std::size_t f = 0; f < hand.topology.faces(); ++f) {
        if (hand.topology.isInternal(FaceIndex::fromValue(f))) {
            internal = FaceIndex::fromValue(f);
        }
    }

    // The fixture's own exact numbers, asserted first so the rest rests on a
    // premise rather than an assumption.
    CHECK_THAT(hand.geometry.orthogonalDistance(internal).si(), WithinRel(0.75, 1e-15));
    const std::optional<double> cosine = hand.geometry.orthogonalityCosine(internal);
    REQUIRE(cosine.has_value());
    CHECK_THAT(*cosine, WithinRel(0.6, 1e-15));

    const std::optional<CfdFaceDecomposition> split = cfd::cfdFaceDecomposition(hand.geometry, internal);
    REQUIRE(split.has_value());

    // OVER-RELAXED GIVES `sin(theta)`, which at `cos = 3/5` is exactly 4/5.
    // The other two give 4/3 and sqrt(0.8) at the same face, so this assertion
    // discriminates between the three rather than merely recording one.
    const double theta = std::acos(0.6);
    CHECK_THAT(split->deferredFraction, WithinRel(overRelaxedRatio(theta), 1e-13));
    CHECK_THAT(split->deferredFraction, WithinRel(0.8, 1e-13));
    // Minimum correction would give 4/3 here, over half as much again.
    CHECK_THAT(minimumCorrectionRatio(theta), WithinRel(4.0 / 3.0, 1e-13));
    CHECK(std::abs(minimumCorrectionRatio(theta) - split->deferredFraction) > 0.5);
    // And orthogonal correction sqrt(2 - 2cos) = sqrt(0.8) = 0.894, which is
    // the closest of the three and still 0.094 away -- twelve orders outside
    // the 1e-13 tolerance above, not the four an earlier comment claimed.
    CHECK(std::abs(orthogonalCorrectionRatio(theta) - split->deferredFraction) > 0.09);

    // `T_f . S_f == 0` identically for this decomposition: the property the
    // deferred correction's conditioning rests on.
    const AreaVector3D s = hand.geometry.faceAreaVector(internal);
    const double dot = split->deferredArm.x.si() * s.x.si() + split->deferredArm.y.si() * s.y.si() +
                       split->deferredArm.z.si() * s.z.si();
    const double scale = std::hypot(split->deferredArm.x.si(), split->deferredArm.y.si(),
                                    split->deferredArm.z.si()) *
                         std::hypot(s.x.si(), s.y.si(), s.z.si());
    CHECK_THAT(dot / scale, WithinAbs(0.0, 1e-14));
}

// ---------------------------------------------------------------------------
// The assembled matrix
// ---------------------------------------------------------------------------

TEST_CASE("CfdDiffusion_AnInternalFaceContributesEqualAndOpposite", "[cfd][diff]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.uniform(
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});
    const Result<CfdDiffusionSystem> system = cfd::assembleCfdDiffusion(
        hand.topology, hand.geometry, boundary, plainInput(1.002_mPa_s, VectorComponent::X));
    INFO((system.has_value() ? std::string{} : system.error().message));
    REQUIRE(system.has_value());

    FaceIndex internal = FaceIndex::fromValue(0);
    for (std::size_t f = 0; f < hand.topology.faces(); ++f) {
        if (hand.topology.isInternal(FaceIndex::fromValue(f))) {
            internal = FaceIndex::fromValue(f);
        }
    }
    const std::size_t owner = hand.topology.owner(internal).value();
    const std::optional<CellIndex> neighbour = hand.topology.neighbour(internal);
    REQUIRE(neighbour.has_value());
    const std::size_t other = neighbour->value();

    const double gamma = gammaOracle(hand.geometry, internal, 1.002_mPa_s);
    const CfdSparseMatrix& a = system->matrix;

    // The off-diagonal pair IS the coefficient, negated, and read from either
    // side it is the SAME double -- not merely close.
    CHECK_THAT(a.coeffSi(owner, other), WithinRel(-gamma, 1e-14));
    CHECK(a.coeffSi(owner, other) == a.coeffSi(other, owner));
}

TEST_CASE("CfdDiffusion_TheMatrixIsSymmetricBitForBit", "[cfd][diff]") {
    const Hand hand = Hand::make(analyticPoints(), kAnalyticPair);
    const PreparedCfdBoundary boundary = hand.uniform(
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});
    const Result<CfdDiffusionSystem> system = cfd::assembleCfdDiffusion(
        hand.topology, hand.geometry, boundary, plainInput(1.002_mPa_s, VectorComponent::Y));
    REQUIRE(system.has_value());

    // EXACTLY ZERO, not within a tolerance. `Gamma_f` read from the owner and
    // from the neighbour is the same floating-point value, because
    // `d' . n' = (-d) . (-n) = d . n` with both negations exact in IEEE-754 and
    // the area is one stored number. A tolerance here would assert less than
    // the truth.
    CHECK(system->matrix.largestSymmetryError() == 0.0);
}

TEST_CASE("CfdDiffusion_TheDiagonalIsPositiveAndEveryOffDiagonalIsNegative", "[cfd][diff]") {
    // P18 refused to assert this for its finite-element conduction matrix,
    // because an obtuse tetrahedron can give a positive off-diagonal. A
    // cell-centred finite-volume off-diagonal is `-Gamma_f` with `Gamma_f`
    // strictly positive on every held face (ADR-065 Decision 5), so here the
    // sign pattern IS an invariant and is asserted.
    const Hand hand = Hand::make(analyticPoints(), kAnalyticPair);
    const PreparedCfdBoundary boundary = hand.uniform(
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});
    const Result<CfdDiffusionSystem> system = cfd::assembleCfdDiffusion(
        hand.topology, hand.geometry, boundary, plainInput(1.002_mPa_s, VectorComponent::Z));
    REQUIRE(system.has_value());
    const CfdSparseMatrix& a = system->matrix;

    std::size_t offDiagonals = 0;
    for (std::size_t row = 0; row < a.rows(); ++row) {
        CHECK(a.coeffSi(row, row) > 0.0);
        for (std::size_t column = 0; column < a.columns(); ++column) {
            if (row == column) {
                continue;
            }
            const double value = a.coeffSi(row, column);
            if (value != 0.0) {
                CHECK(value < 0.0);
                ++offDiagonals;
            }
        }
    }
    // Two cells sharing one face: exactly two off-diagonal entries, so the loop
    // above is not vacuously passing on an empty pattern.
    CHECK(offDiagonals == 2);
}

TEST_CASE("CfdDiffusion_AConstantFieldIsInTheNullSpaceWithoutAPrescribedFace", "[cfd][diff]") {
    // Every boundary face a pressure outlet, whose velocity policy is
    // ZeroGradient, so NOTHING touches a diagonal from the boundary and the
    // matrix is singular with the constant vector in its null space. That is
    // physically correct and it is the sharpest available test that no
    // regularisation was slipped in.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.uniform(CfdBoundaryCondition{
        CfdBoundaryConditionId::fromValue(1), PressureOutlet{someFace(), 0_Pa}});
    const Result<CfdDiffusionSystem> system = cfd::assembleCfdDiffusion(
        hand.topology, hand.geometry, boundary, plainInput(1.002_mPa_s, VectorComponent::X));
    INFO((system.has_value() ? std::string{} : system.error().message));
    REQUIRE(system.has_value());

    CHECK(system->summary.zeroGradientFaces == hand.topology.boundaryFaces());
    CHECK(system->summary.fixedValueFaces == 0);
    CHECK(system->summary.constantModeIsInTheNullSpace);

    const std::vector<double> ones(system->matrix.columns(), 1.0);
    const Result<std::vector<double>> product = system->matrix.multiply(ones);
    REQUIRE(product.has_value());
    // Relative to the matrix's own scale: an absolute bound would be
    // meaningless on a stiff matrix, which is thermal's recorded reason for the
    // same normalisation.
    CHECK(maxAbs(*product) <= 1e-14 * system->matrix.largestMagnitude());
    // And the source is untouched, because a zero-gradient face contributes
    // nothing at all rather than a zero.
    CHECK(maxAbs(system->source) == 0.0);
}

TEST_CASE("CfdDiffusion_APrescribedFaceLiftsTheConstantMode", "[cfd][diff]") {
    // The complement, and it is needed: a null-mode test alone passes on a
    // matrix that is all zeros.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.uniform(
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});
    const Result<CfdDiffusionSystem> system = cfd::assembleCfdDiffusion(
        hand.topology, hand.geometry, boundary, plainInput(1.002_mPa_s, VectorComponent::X));
    REQUIRE(system.has_value());

    CHECK(system->summary.fixedValueFaces == hand.topology.boundaryFaces());
    CHECK_FALSE(system->summary.constantModeIsInTheNullSpace);

    const std::vector<double> ones(system->matrix.columns(), 1.0);
    const Result<std::vector<double>> product = system->matrix.multiply(ones);
    REQUIRE(product.has_value());
    CHECK(maxAbs(*product) > 0.1 * system->matrix.largestMagnitude());

    // `A 1` is exactly the boundary coefficients of each row, which is an
    // independent reading of the same numbers.
    for (std::size_t cell = 0; cell < hand.topology.cells(); ++cell) {
        double expected = 0.0;
        for (std::size_t f = 0; f < hand.topology.faces(); ++f) {
            const FaceIndex face = FaceIndex::fromValue(f);
            if (hand.topology.isInternal(face) || hand.topology.owner(face).value() != cell) {
                continue;
            }
            expected += gammaOracle(hand.geometry, face, 1.002_mPa_s);
        }
        CHECK_THAT((*product)[cell], WithinRel(expected, 1e-13));
    }
}

TEST_CASE("CfdDiffusion_AWallAtRestPrescribesZeroIntoTheSource", "[cfd][diff]") {
    // RENAMED, because the old name promised a contrast with a moving wall that
    // the body never made -- both reviews found it, and `MovingWall` was
    // imported and never constructed. The contrast now lives in
    // `AMovingWallPrescribesItsOwnComponentIntoTheSource` below, which is where
    // the non-zero Dirichlet arm is actually measured.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary still = hand.uniform(
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});
    const Result<CfdDiffusionSystem> atRest = cfd::assembleCfdDiffusion(
        hand.topology, hand.geometry, still, plainInput(1.002_mPa_s, VectorComponent::X));
    REQUIRE(atRest.has_value());
    // A wall at rest prescribes zero, so the matrix carries the coefficients
    // and the source is exactly zero -- exactly, because `gamma * 0.0` is zero
    // and nothing else is written.
    CHECK(maxAbs(atRest->source) == 0.0);
    CHECK(atRest->summary.fixedValueFaces == hand.topology.boundaryFaces());
}

// ---------------------------------------------------------------------------
// The sparse structure
// ---------------------------------------------------------------------------

TEST_CASE("CfdDiffusion_TheSparsePatternIsTheTopologysAdjacency", "[cfd][diff]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.uniform(
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});
    const Result<CfdDiffusionSystem> system = cfd::assembleCfdDiffusion(
        hand.topology, hand.geometry, boundary, plainInput(1.002_mPa_s, VectorComponent::X));
    REQUIRE(system.has_value());
    const CfdSparseMatrix& a = system->matrix;

    CHECK(a.rows() == hand.topology.cells());
    CHECK(a.columns() == hand.topology.cells());
    // Two diagonals plus two off-diagonals for the one shared face.
    CHECK(a.nonZeros() == hand.topology.cells() + 2 * hand.topology.internalFaces());

    // The adjacency, built again from the topology rather than read from the
    // matrix.
    for (std::size_t row = 0; row < a.rows(); ++row) {
        std::vector<std::size_t> expected{row};
        for (std::size_t f = 0; f < hand.topology.faces(); ++f) {
            const FaceIndex face = FaceIndex::fromValue(f);
            if (!hand.topology.isInternal(face)) {
                continue;
            }
            const std::size_t owner = hand.topology.owner(face).value();
            const std::optional<CellIndex> neighbour = hand.topology.neighbour(face);
            REQUIRE(neighbour.has_value());
            if (owner == row) {
                expected.push_back(neighbour->value());
            } else if (neighbour->value() == row) {
                expected.push_back(owner);
            }
        }
        std::ranges::sort(expected);
        expected.erase(std::ranges::unique(expected).begin(), expected.end());

        std::vector<std::size_t> actual;
        for (auto slot = a.rowStart()[row]; slot < a.rowStart()[row + 1]; ++slot) {
            actual.push_back(static_cast<std::size_t>(a.inner()[static_cast<std::size_t>(slot)]));
        }
        CHECK(actual == expected);
        // And ascending, which is what makes `coeffSi` a binary search.
        CHECK(std::ranges::is_sorted(actual));
    }
}

TEST_CASE("CfdDiffusion_ACellWithNoInternalFaceStillHasItsDiagonal", "[cfd][diff]") {
    // One cell, four boundary faces, no internal face. A row that received no
    // off-diagonal must still exist and hold its diagonal: an absent diagonal
    // is a different statement from a zero one.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    REQUIRE(hand.topology.cells() == 1);
    REQUIRE(hand.topology.internalFaces() == 0);
    const PreparedCfdBoundary boundary = hand.uniform(
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});
    const Result<CfdDiffusionSystem> system = cfd::assembleCfdDiffusion(
        hand.topology, hand.geometry, boundary, plainInput(1.002_mPa_s, VectorComponent::X));
    INFO((system.has_value() ? std::string{} : system.error().message));
    REQUIRE(system.has_value());

    CHECK(system->matrix.rows() == 1);
    CHECK(system->matrix.nonZeros() == 1);
    CHECK(system->matrix.coeffSi(0, 0) > 0.0);
    CHECK(system->summary.internalFaces == 0);
}

// ---------------------------------------------------------------------------
// Determinism
// ---------------------------------------------------------------------------

TEST_CASE("CfdDiffusion_TwoAssembliesOfOneInputAreIdentical", "[cfd][diff]") {
    const Hand hand = Hand::make(analyticPoints(), kAnalyticPair);
    const PreparedCfdBoundary boundary = hand.uniform(
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});
    const CfdDiffusionInput input = plainInput(1.002_mPa_s, VectorComponent::X);
    const Result<CfdDiffusionSystem> first =
        cfd::assembleCfdDiffusion(hand.topology, hand.geometry, boundary, input);
    const Result<CfdDiffusionSystem> second =
        cfd::assembleCfdDiffusion(hand.topology, hand.geometry, boundary, input);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    // Bit-identical, through the matrix's own defaulted equality.
    CHECK(first->matrix == second->matrix);
    CHECK(first->source == second->source);
    CHECK(first->summary == second->summary);
}

// ---------------------------------------------------------------------------
// Refusals, by tag
// ---------------------------------------------------------------------------

TEST_CASE("CfdDiffusion_AnUnusableViscosityIsRefusedByName", "[cfd][diff]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.uniform(
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});

    for (const double bad : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity()}) {
        CfdDiffusionInput input = plainInput(DynamicViscosity::fromSi(bad), VectorComponent::X);
        const std::optional<CfdDiffusionProblem> predicted =
            cfd::cfdDiffusionProblem(hand.topology, hand.geometry, boundary, input);
        REQUIRE(predicted.has_value());
        CHECK(*predicted == CfdDiffusionProblem::ViscosityNotUsable);
        const Result<CfdDiffusionSystem> refused =
            cfd::assembleCfdDiffusion(hand.topology, hand.geometry, boundary, input);
        CHECK_FALSE(refused.has_value());
    }

    // And a usable one is ACCEPTED, so the gate is not simply refusing
    // everything -- the hole a review found in P19-IC-001's own finiteness
    // cases, where no test asserted that a finite extreme passes. A nominal
    // value is not an extreme, which a review pointed out about an earlier
    // version of this comment, so both are here.
    for (const double usable : {1.002e-3, std::numeric_limits<double>::min(),
                                std::numeric_limits<double>::denorm_min(), 1.0e300}) {
        const CfdDiffusionInput good = plainInput(DynamicViscosity::fromSi(usable), VectorComponent::X);
        INFO(usable);
        CHECK_FALSE(cfd::cfdDiffusionProblem(hand.topology, hand.geometry, boundary, good).has_value());
    }
}

TEST_CASE("CfdDiffusion_ThePredictorTheFailureAndThePathNeverDisagree", "[cfd][diff]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.uniform(
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});

    const std::array<CfdDiffusionInput, 4> inputs{
        plainInput(1.002_mPa_s, VectorComponent::X),
        plainInput(DynamicViscosity::fromSi(-1.0), VectorComponent::X),
        plainInput(DynamicViscosity::fromSi(0.0), VectorComponent::Y),
        plainInput(1.002_mPa_s, VectorComponent::Z),
    };
    for (const CfdDiffusionInput& input : inputs) {
        const std::optional<CfdDiffusionProblem> predicted =
            cfd::cfdDiffusionProblem(hand.topology, hand.geometry, boundary, input);
        const std::optional<cfd::CfdDiffusionFailure> failure =
            cfd::cfdDiffusionFailure(hand.topology, hand.geometry, boundary, input);
        const Result<CfdDiffusionSystem> assembled =
            cfd::assembleCfdDiffusion(hand.topology, hand.geometry, boundary, input);

        CHECK(predicted.has_value() == !assembled.has_value());
        CHECK(failure.has_value() == predicted.has_value());
        if (failure.has_value() && predicted.has_value()) {
            // The TAG, not only the message: this is the assertion P19-BC-001's
            // review found missing, where eight refusals could be swapped with
            // every message assertion still passing.
            CHECK(failure->problem == *predicted);
            REQUIRE_FALSE(assembled.has_value());
            CHECK(failure->error.message == assembled.error().message);
        }
    }
}

TEST_CASE("CfdDiffusion_EveryProblemHasADistinctName", "[cfd][diff]") {
    std::vector<std::string> names;
    for (const CfdDiffusionProblem problem : cfd::kCfdDiffusionProblems) {
        names.emplace_back(cfd::toString(problem));
    }
    // AGAINST AN INDEPENDENT NUMBER, not against the array the names were
    // built from. A review pointed out that comparing `names.size()` with
    // `kCfdDiffusionProblems.size()` cannot fail when `names` holds one entry
    // per element of that array. Nine is written out here, and the header
    // carries a static_assert for the same count, so an enumerator added
    // without being put in the array fails there.
    CHECK(names.size() == 9);
    CHECK(cfd::kCfdDiffusionProblems.size() == 9);
    std::ranges::sort(names);
    CHECK(std::ranges::unique(names).begin() == names.end());
    for (const std::string& name : names) {
        CHECK_FALSE(name.empty());
        CHECK(name != "unknown");
    }
}

TEST_CASE("CfdDiffusion_EverySparseProblemHasADistinctName", "[cfd][diff]") {
    std::vector<std::string> names;
    for (const CfdSparseProblem problem : cfd::kCfdSparseProblems) {
        names.emplace_back(cfd::toString(problem));
    }
    CHECK(names.size() == 6);
    CHECK(cfd::kCfdSparseProblems.size() == 6);
    std::ranges::sort(names);
    CHECK(std::ranges::unique(names).begin() == names.end());
    for (const std::string& name : names) {
        CHECK_FALSE(name.empty());
        CHECK(name != "unknown");
    }
}

// ---------------------------------------------------------------------------
// The linear-field patch test: the exactness of the SPLIT
// ---------------------------------------------------------------------------

TEST_CASE("CfdDiffusion_ALinearFieldSplitsExactlyIntoTheTwoArms", "[cfd][diff]") {
    // THE CLAIM ADR-069 DECISION 5 MAKES, tested where it lives: for a linear
    // field `U = g . x + c`, `U_N - U_P = g . d` EXACTLY, so the implicit term
    // is exactly `mu g . E_f`, the correction is exactly `mu g . T_f`, and the
    // two sum to the true flux `mu g . S_f`.
    //
    // PER FACE, NOT PER CELL, and that is deliberate rather than a retreat. A
    // cell residual would need the boundary to prescribe the linear field's
    // value at each face centroid, and no condition kind can express an
    // arbitrary vector there: a moving wall must lie in the face and a velocity
    // inlet is normal to it. The exactness of the split is the permanent claim;
    // the per-face form tests it without inventing a boundary condition the
    // document cannot hold.
    //
    // THE TOLERANCE IS IN UNITS OF `eps * (the products summed)`, not of the
    // answer. P18-ELEM-001's review recorded this as finding F4 after three of
    // its own tests failed at 1e-11: a large constant offset multiplies terms
    // that then cancel, so the error is bounded by the size of the products and
    // not by the size of the result. The offset below is deliberately large.
    const DynamicViscosity mu = 1.002_mPa_s;
    // THE OFFSET IS LARGE ON PURPOSE, and larger than P18's. A review measured
    // that at 305.7 the normalization was not load-bearing: a plain
    // `WithinRel(1e-13)` would have passed too, so the test did not actually
    // have the shape its comment claimed. At 1e6 against a field variation of
    // about 5 the cancellation is severe and the eps-units form is the only one
    // that holds.
    const double offset = 1.0e6;
    const std::array<double, 3> g{7.2, -3.5, 11.4};

    struct Fixture {
        std::string name;
        std::vector<Point3D> points;
        std::vector<std::array<std::size_t, 4>> tets;
    };
    const std::vector<Fixture> fixtures{{"orthogonal pair", orthogonalPoints(), kTwoCells},
                                        {"analytic pair", analyticPoints(), kAnalyticPair}};

    for (const Fixture& fixture : fixtures) {
        INFO(fixture.name);
        const Hand hand = Hand::make(fixture.points, fixture.tets);
        std::size_t internalChecked = 0;
        double worstNormalised = 0.0;

        for (std::size_t f = 0; f < hand.topology.faces(); ++f) {
            const FaceIndex face = FaceIndex::fromValue(f);
            if (!hand.topology.isInternal(face)) {
                continue;
            }
            const std::optional<CfdFaceDecomposition> split =
                cfd::cfdFaceDecomposition(hand.geometry, face);
            REQUIRE(split.has_value());

            const std::optional<Point3D> cp = hand.geometry.cellCentroid(hand.topology.owner(face));
            const std::optional<CellIndex> neighbour = hand.topology.neighbour(face);
            REQUIRE(neighbour.has_value());
            const std::optional<Point3D> cn = hand.geometry.cellCentroid(*neighbour);
            REQUIRE(cp.has_value());
            REQUIRE(cn.has_value());

            // The two cell values of the linear field, with the offset present
            // in both so that it cancels in the difference.
            const double uP = offset + g[0] * cp->x.si() + g[1] * cp->y.si() + g[2] * cp->z.si();
            const double uN = offset + g[0] * cn->x.si() + g[1] * cn->y.si() + g[2] * cn->z.si();

            const double gamma = gammaOracle(hand.geometry, face, mu);
            const double implicitTerm = gamma * (uN - uP);
            const double correctionTerm =
                mu.si() * (g[0] * split->deferredArm.x.si() + g[1] * split->deferredArm.y.si() +
                           g[2] * split->deferredArm.z.si());

            // The true flux, from the area vector and the gradient alone --
            // neither of which the two terms above were built from.
            const AreaVector3D s = hand.geometry.faceAreaVector(face);
            const double exact = mu.si() * (g[0] * s.x.si() + g[1] * s.y.si() + g[2] * s.z.si());

            // The size of the products summed, which is what bounds the error.
            //
            // THE `mu |g| |E_f|` TERM IS HERE BECAUSE A REVIEW SHOWED THE SUM
            // WITHOUT IT IS NOT A GENERAL BOUND: forming `T_f = S_f - E_f`
            // rounds at the scale of `|E_f| = A / cos(theta)`, which exceeds
            // `|exact|` by `1/cos(theta)` and would dominate on a face near 90
            // degrees. Neither fixture reaches that, so the test passed either
            // way -- which is exactly why the formula had to be fixed rather
            // than left to the fixtures.
            const double implicitArmNorm = std::hypot(split->implicitArm.x.si(),
                                                      split->implicitArm.y.si(),
                                                      split->implicitArm.z.si());
            const double gradientNorm = std::hypot(g[0], g[1], g[2]);
            const double products = std::abs(gamma * uP) + std::abs(gamma * uN) +
                                    std::abs(correctionTerm) + std::abs(exact) +
                                    mu.si() * gradientNorm * implicitArmNorm;
            const double normalised = std::abs(implicitTerm + correctionTerm - exact) /
                                      (std::numeric_limits<double>::epsilon() * products);
            worstNormalised = std::max(worstNormalised, normalised);
            ++internalChecked;
        }

        // The sweep is not vacuous.
        CHECK(internalChecked == hand.topology.internalFaces());
        CHECK(internalChecked >= 1);
        WARN(std::format("{}: worst split error {:.3f} eps-units, bound 64", fixture.name,
                         worstNormalised));
        CHECK(worstNormalised <= 64.0);
    }
}

TEST_CASE("CfdDiffusion_TheDeferredArmIsLargeOnASkewFaceSoDroppingItWouldMatter", "[cfd][diff]") {
    // THE COMPLEMENT, and without it the case above would pass on an operator
    // whose deferred arm was identically zero everywhere. On the analytic face
    // the deferred arm is 4/5 of the implicit arm, so omitting the correction
    // is a large error and not a rounding one -- which is the measured basis for
    // ADR-069's statement that without a gradient the operator is exact only on
    // an orthogonal mesh.
    const Hand hand = Hand::make(analyticPoints(), kAnalyticPair);
    FaceIndex internal = FaceIndex::fromValue(0);
    for (std::size_t f = 0; f < hand.topology.faces(); ++f) {
        if (hand.topology.isInternal(FaceIndex::fromValue(f))) {
            internal = FaceIndex::fromValue(f);
        }
    }
    const std::optional<CfdFaceDecomposition> split = cfd::cfdFaceDecomposition(hand.geometry, internal);
    REQUIRE(split.has_value());
    const double implicitNorm =
        std::hypot(split->implicitArm.x.si(), split->implicitArm.y.si(), split->implicitArm.z.si());
    const double deferredNorm =
        std::hypot(split->deferredArm.x.si(), split->deferredArm.y.si(), split->deferredArm.z.si());
    REQUIRE(implicitNorm > 0.0);
    CHECK_THAT(deferredNorm / implicitNorm, WithinRel(0.8, 1e-13));
    // And on the orthogonal fixture it is zero, so the two fixtures bracket the
    // claim rather than agreeing by accident.
    const Hand straight = Hand::make(orthogonalPoints(), kTwoCells);
    FaceIndex flat = FaceIndex::fromValue(0);
    for (std::size_t f = 0; f < straight.topology.faces(); ++f) {
        if (straight.topology.isInternal(FaceIndex::fromValue(f))) {
            flat = FaceIndex::fromValue(f);
        }
    }
    const std::optional<CfdFaceDecomposition> none = cfd::cfdFaceDecomposition(straight.geometry, flat);
    REQUIRE(none.has_value());
    CHECK_THAT(none->deferredFraction, WithinAbs(0.0, 1e-15));
}

// ---------------------------------------------------------------------------
// An independent one-dimensional oracle
// ---------------------------------------------------------------------------

TEST_CASE("CfdDiffusion_AnIndependentOneDimensionalOracleFixesTheCoefficient", "[cfd][diff]") {
    // A DIFFERENT ROUTE TO THE SAME NUMBER. For one-dimensional diffusion
    // between two cells of spacing `h` across an area `A`, the conductance is
    // `mu A / h`: the textbook result, written out here with `A` by Heron's
    // formula and `h` from centroids averaged over the MESH NODES -- neither of
    // which is how CfdGeometry computes them, so a transposition in the metrics
    // could not be mirrored here.
    //
    // The orthogonal fixture is the only one where the finite-volume
    // coefficient and the one-dimensional formula must agree, because there `d`
    // is parallel to `n` and so `d . n == |d|`. On a skew face the two
    // legitimately differ by `1 / cos(theta)`, which the case below measures.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    FaceIndex internal = FaceIndex::fromValue(0);
    for (std::size_t f = 0; f < hand.topology.faces(); ++f) {
        if (hand.topology.isInternal(FaceIndex::fromValue(f))) {
            internal = FaceIndex::fromValue(f);
        }
    }

    const std::array<NodeId, 3> nodes = hand.topology.faceNodes(internal);
    std::array<Point3D, 3> corner{};
    for (std::size_t i = 0; i < 3; ++i) {
        const meshing::Node* node = hand.mesh.findNode(nodes[i]);
        REQUIRE(node != nullptr);
        corner[i] = node->position;
    }
    const auto sideLength = [](const Point3D& a, const Point3D& b) {
        return std::hypot((a.x - b.x).si(), (a.y - b.y).si(), (a.z - b.z).si());
    };
    const double sideA = sideLength(corner[0], corner[1]);
    const double sideB = sideLength(corner[1], corner[2]);
    const double sideC = sideLength(corner[2], corner[0]);
    const double semi = 0.5 * (sideA + sideB + sideC);
    const double heronArea = std::sqrt(semi * (semi - sideA) * (semi - sideB) * (semi - sideC));

    const auto centroidOf = [&](CellIndex cell) {
        const meshing::Tetrahedron* tet = hand.mesh.findTetrahedron(hand.topology.cellOf(cell));
        REQUIRE(tet != nullptr);
        std::array<double, 3> sum{0.0, 0.0, 0.0};
        for (const NodeId id : tet->nodes) {
            const meshing::Node* node = hand.mesh.findNode(id);
            REQUIRE(node != nullptr);
            sum[0] += node->position.x.si();
            sum[1] += node->position.y.si();
            sum[2] += node->position.z.si();
        }
        return std::array<double, 3>{sum[0] / 4.0, sum[1] / 4.0, sum[2] / 4.0};
    };
    const std::array<double, 3> owner = centroidOf(hand.topology.owner(internal));
    const std::optional<CellIndex> neighbour = hand.topology.neighbour(internal);
    REQUIRE(neighbour.has_value());
    const std::array<double, 3> other = centroidOf(*neighbour);
    const double spacing = std::hypot(other[0] - owner[0], other[1] - owner[1], other[2] - owner[2]);

    const DynamicViscosity mu = 1.002_mPa_s;
    const double oracle = mu.si() * heronArea / spacing;

    const PreparedCfdBoundary boundary = hand.uniform(
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});
    const Result<CfdDiffusionSystem> system = cfd::assembleCfdDiffusion(
        hand.topology, hand.geometry, boundary, plainInput(mu, VectorComponent::X));
    REQUIRE(system.has_value());
    const std::size_t p = hand.topology.owner(internal).value();
    const std::size_t n = neighbour->value();

    // 1e-12 rather than 1e-15: Heron's formula and a four-node average are a
    // different accumulation from a cross product and the metrics' own
    // centroid, so the two agree to geometric accumulation, not to the last bit.
    CHECK_THAT(-system->matrix.coeffSi(p, n), WithinRel(oracle, 1e-12));
}

TEST_CASE("CfdDiffusion_OnASkewFaceTheCoefficientExceedsTheOneDimensionalOneByOneOverCosine",
          "[cfd][diff]") {
    // The same comparison on the analytic face, where the two must DIFFER by a
    // known factor. Without this, the one-dimensional oracle above would be
    // consistent with an operator that divided by `|d|` -- the orthogonal
    // correction's coefficient -- rather than by `d . n`.
    const Hand hand = Hand::make(analyticPoints(), kAnalyticPair);
    FaceIndex internal = FaceIndex::fromValue(0);
    for (std::size_t f = 0; f < hand.topology.faces(); ++f) {
        if (hand.topology.isInternal(FaceIndex::fromValue(f))) {
            internal = FaceIndex::fromValue(f);
        }
    }
    const std::optional<Translation3D> d = hand.geometry.ownerToNeighbour(internal);
    REQUIRE(d.has_value());
    const double spacing = magnitude(*d).si();
    CHECK_THAT(spacing, WithinRel(1.25, 1e-14));

    const DynamicViscosity mu = 1.002_mPa_s;
    const AreaVector3D s = hand.geometry.faceAreaVector(internal);
    const double area = std::hypot(s.x.si(), s.y.si(), s.z.si());
    const double oneDimensional = mu.si() * area / spacing;

    // READ OUT OF THE ASSEMBLED MATRIX, which is a correction. This test first
    // compared `gammaOracle` against `oneDimensional` -- two test-side
    // quantities -- so it reduced to the fixture premise `cos(theta) = 3/5` and
    // an operator dividing by `|d|` would have passed it unchanged. Both reviews
    // found it. The off-diagonal of a real assembly is the only reading that
    // can fail.
    const PreparedCfdBoundary boundary = hand.uniform(
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});
    const Result<CfdDiffusionSystem> system = cfd::assembleCfdDiffusion(
        hand.topology, hand.geometry, boundary, plainInput(mu, VectorComponent::X));
    REQUIRE(system.has_value());
    const std::size_t p = hand.topology.owner(internal).value();
    const std::optional<CellIndex> other = hand.topology.neighbour(internal);
    REQUIRE(other.has_value());
    const double assembled = -system->matrix.coeffSi(p, other->value());

    // `cos(theta) = 3/5`, so the finite-volume coefficient is 5/3 of the
    // one-dimensional one. Exactly, because both are rationals here.
    CHECK_THAT(assembled / oneDimensional, WithinRel(5.0 / 3.0, 1e-13));
}

// ---------------------------------------------------------------------------
// The scale laws, one per factor
// ---------------------------------------------------------------------------

TEST_CASE("CfdDiffusion_TheScaleLawsSeparateViscosityFromAreaAndDistance", "[cfd][diff]") {
    // `mu`, the face area and the orthogonal distance appear ONLY as the product
    // `mu A / (d . n)`, so no assertion on a coefficient can tell them apart --
    // P18 recorded the same hazard for `rho * cp`, where swapping the two gave
    // bit-identical matrices. The type system is the first protection: the three
    // are a DynamicViscosity, an Area and a Length, so a swap is a compile
    // error, and the compile-fail group asserts that. This case is the second:
    // doubling the viscosity ALONE must double every coefficient, which pins the
    // factor that can be varied without rebuilding the mesh.
    const Hand hand = Hand::make(analyticPoints(), kAnalyticPair);
    const PreparedCfdBoundary boundary = hand.uniform(
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});

    const Result<CfdDiffusionSystem> single = cfd::assembleCfdDiffusion(
        hand.topology, hand.geometry, boundary, plainInput(1.002_mPa_s, VectorComponent::X));
    const Result<CfdDiffusionSystem> doubled = cfd::assembleCfdDiffusion(
        hand.topology, hand.geometry, boundary, plainInput(2.004_mPa_s, VectorComponent::X));
    REQUIRE(single.has_value());
    REQUIRE(doubled.has_value());
    REQUIRE(single->matrix.nonZeros() == doubled->matrix.nonZeros());

    for (std::size_t slot = 0; slot < single->matrix.nonZeros(); ++slot) {
        const double one = single->matrix.values()[slot];
        const double two = doubled->matrix.values()[slot];
        CHECK_THAT(two, WithinRel(2.0 * one, 1e-14));
    }
    // The pattern is a function of connectivity alone, so it does not move.
    CHECK(std::ranges::equal(single->matrix.inner(), doubled->matrix.inner()));
    CHECK(std::ranges::equal(single->matrix.rowStart(), doubled->matrix.rowStart()));
    // And the deferred fraction is PURELY GEOMETRIC, so it does not move either:
    // a viscosity that leaked into it would show here.
    CHECK(single->summary.deferredFraction == doubled->summary.deferredFraction);
}

// ---------------------------------------------------------------------------
// The correction, reported and applied
// ---------------------------------------------------------------------------

TEST_CASE("CfdDiffusion_TheOmittedCorrectionIsReportedAndTheSuppliedOneIsApplied", "[cfd][diff]") {
    const Hand hand = Hand::make(analyticPoints(), kAnalyticPair);
    const PreparedCfdBoundary boundary = hand.uniform(
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});
    const DynamicViscosity mu = 1.002_mPa_s;

    // Omitted: reported as such, and the deferred fraction is the geometry's
    // worst `sin(theta)` rather than zero -- the number a reader needs in order
    // to know how wrong the assembled operator is.
    const Result<CfdDiffusionSystem> without = cfd::assembleCfdDiffusion(
        hand.topology, hand.geometry, boundary, plainInput(mu, VectorComponent::X));
    REQUIRE(without.has_value());
    CHECK_FALSE(without->summary.correctionApplied);
    CHECK(without->summary.deferredFraction > 0.5);

    // Supplied: the flag flips and the SOURCE moves, which is where the
    // correction lands. A flag that flipped without the source moving would be
    // a report of work that was not done.
    std::vector<VelocityGradient3> gradients(hand.topology.faces());
    for (VelocityGradient3& g : gradients) {
        g.x = VelocityGradient::fromSi(7.2);
        g.y = VelocityGradient::fromSi(-3.5);
        g.z = VelocityGradient::fromSi(11.4);
    }
    CfdDiffusionInput corrected = plainInput(mu, VectorComponent::X);
    corrected.faceGradient = gradients;
    const Result<CfdDiffusionSystem> with =
        cfd::assembleCfdDiffusion(hand.topology, hand.geometry, boundary, corrected);
    REQUIRE(with.has_value());
    CHECK(with->summary.correctionApplied);
    CHECK(maxAbs(with->source) > 0.0);
    // The MATRIX is untouched by a correction: it is an explicit term by
    // construction, and a correction that reached a coefficient would break the
    // sign and symmetry claims.
    CHECK(with->matrix == without->matrix);
    // And the deferred fraction is the same geometry either way.
    CHECK(with->summary.deferredFraction == without->summary.deferredFraction);
}

TEST_CASE("CfdDiffusion_AWronglySizedCorrectionIsRefusedByName", "[cfd][diff]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.uniform(
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});
    const std::size_t right = hand.topology.faces();

    for (const std::size_t wrong : {right - 1, right + 1}) {
        const std::vector<VelocityGradient3> gradients(wrong);
        CfdDiffusionInput input = plainInput(1.002_mPa_s, VectorComponent::X);
        input.faceGradient = gradients;
        const std::optional<CfdDiffusionProblem> predicted =
            cfd::cfdDiffusionProblem(hand.topology, hand.geometry, boundary, input);
        REQUIRE(predicted.has_value());
        CHECK(*predicted == CfdDiffusionProblem::CorrectionLengthWrong);
    }

    // And the right length is accepted, so the gate is not refusing everything.
    const std::vector<VelocityGradient3> exact(right);
    CfdDiffusionInput good = plainInput(1.002_mPa_s, VectorComponent::X);
    good.faceGradient = exact;
    CHECK_FALSE(cfd::cfdDiffusionProblem(hand.topology, hand.geometry, boundary, good).has_value());
}

// ---------------------------------------------------------------------------
// The symmetry arm
// ---------------------------------------------------------------------------

TEST_CASE("CfdDiffusion_ASlipWallIsRefusedWithoutTheCurrentVelocity", "[cfd][diff]") {
    // ADR-067 made `Symmetry` a fourth policy because collapsing it onto
    // fixed-value or zero-gradient "would be silently wrong at every symmetry
    // plane". Its cross terms need the current velocity, and the assembly
    // refuses rather than dropping them -- dropping them is exactly the silent
    // error that policy exists to prevent.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary =
        hand.uniform(CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), SlipWall{someFace()}});

    const CfdDiffusionInput bare = plainInput(1.002_mPa_s, VectorComponent::X);
    const std::optional<CfdDiffusionProblem> predicted =
        cfd::cfdDiffusionProblem(hand.topology, hand.geometry, boundary, bare);
    REQUIRE(predicted.has_value());
    CHECK(*predicted == CfdDiffusionProblem::SymmetryNeedsTheCurrentVelocity);
    CHECK_FALSE(cfd::assembleCfdDiffusion(hand.topology, hand.geometry, boundary, bare).has_value());

    // Supplied at the right length, it is accepted.
    const std::vector<double> current(3 * hand.topology.cells(), 0.0);
    CfdDiffusionInput supplied = plainInput(1.002_mPa_s, VectorComponent::X);
    supplied.currentVelocity = current;
    CHECK_FALSE(cfd::cfdDiffusionProblem(hand.topology, hand.geometry, boundary, supplied).has_value());
}

TEST_CASE("CfdDiffusion_AWronglySizedCurrentVelocityIsRefusedByName", "[cfd][diff]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.uniform(
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});
    const std::size_t right = 3 * hand.topology.cells();
    for (const std::size_t wrong : {right - 1, right + 1}) {
        const std::vector<double> current(wrong, 0.0);
        CfdDiffusionInput input = plainInput(1.002_mPa_s, VectorComponent::X);
        input.currentVelocity = current;
        const std::optional<CfdDiffusionProblem> predicted =
            cfd::cfdDiffusionProblem(hand.topology, hand.geometry, boundary, input);
        REQUIRE(predicted.has_value());
        CHECK(*predicted == CfdDiffusionProblem::CurrentVelocityLengthWrong);
    }
}

TEST_CASE("CfdDiffusion_ASlipWallsDiagonalIsTheNormalProjectionPerComponent", "[cfd][diff]") {
    // THE ORACLE IS THE NORMALS, read from the geometry and squared here rather
    // than asked of the operator: each cell's diagonal must be
    // `sum_internal Gamma + sum_boundary Gamma n_i^2`. This covers the case a
    // count-based flag got wrong -- a face whose normal is perpendicular to this
    // component contributes exactly zero -- without having to construct one.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary =
        hand.uniform(CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), SlipWall{someFace()}});
    const DynamicViscosity mu = 1.002_mPa_s;
    const std::vector<double> current(3 * hand.topology.cells(), 0.0);

    for (const VectorComponent component : cfd::kVectorComponents) {
        INFO(cfd::toString(component));
        CfdDiffusionInput input = plainInput(mu, component);
        input.currentVelocity = current;
        const Result<CfdDiffusionSystem> system =
            cfd::assembleCfdDiffusion(hand.topology, hand.geometry, boundary, input);
        INFO((system.has_value() ? std::string{} : system.error().message));
        REQUIRE(system.has_value());
        CHECK(system->summary.symmetryFaces == hand.topology.boundaryFaces());

        const auto axis = static_cast<std::size_t>(component);
        for (std::size_t cell = 0; cell < hand.topology.cells(); ++cell) {
            double expected = 0.0;
            for (std::size_t f = 0; f < hand.topology.faces(); ++f) {
                const FaceIndex face = FaceIndex::fromValue(f);
                const double gamma = gammaOracle(hand.geometry, face, mu);
                if (hand.topology.isInternal(face)) {
                    const std::optional<CellIndex> neighbour = hand.topology.neighbour(face);
                    REQUIRE(neighbour.has_value());
                    if (hand.topology.owner(face).value() == cell || neighbour->value() == cell) {
                        expected += gamma;
                    }
                    continue;
                }
                if (hand.topology.owner(face).value() != cell) {
                    continue;
                }
                const std::optional<Direction3D> normal = hand.geometry.faceUnitNormal(face);
                REQUIRE(normal.has_value());
                const std::array<double, 3> n{normal->x(), normal->y(), normal->z()};
                expected += gamma * n[axis] * n[axis];
            }
            CHECK_THAT(system->matrix.coeffSi(cell, cell), WithinRel(expected, 1e-12));
        }
    }
}

TEST_CASE("CfdDiffusion_WithoutASlipWallTheThreeComponentsShareOneMatrix", "[cfd][diff]") {
    // `Gamma_f` carries no component, so for fixed-value and zero-gradient
    // boundaries the three component systems are the SAME matrix -- bit for bit.
    // A component index that leaked into a coefficient would break this.
    const Hand hand = Hand::make(analyticPoints(), kAnalyticPair);
    const PreparedCfdBoundary boundary = hand.uniform(
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});
    const Result<CfdDiffusionSystem> x = cfd::assembleCfdDiffusion(
        hand.topology, hand.geometry, boundary, plainInput(1.002_mPa_s, VectorComponent::X));
    const Result<CfdDiffusionSystem> y = cfd::assembleCfdDiffusion(
        hand.topology, hand.geometry, boundary, plainInput(1.002_mPa_s, VectorComponent::Y));
    const Result<CfdDiffusionSystem> z = cfd::assembleCfdDiffusion(
        hand.topology, hand.geometry, boundary, plainInput(1.002_mPa_s, VectorComponent::Z));
    REQUIRE(x.has_value());
    REQUIRE(y.has_value());
    REQUIRE(z.has_value());
    CHECK(x->matrix == y->matrix);
    CHECK(y->matrix == z->matrix);

    // AND WITH A SLIP WALL THEY DIFFER, which is what makes the equality above
    // a statement about the coefficient rather than about the fixture.
    const PreparedCfdBoundary slipping =
        hand.uniform(CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), SlipWall{someFace()}});
    const std::vector<double> current(3 * hand.topology.cells(), 0.0);
    CfdDiffusionInput sx = plainInput(1.002_mPa_s, VectorComponent::X);
    CfdDiffusionInput sy = plainInput(1.002_mPa_s, VectorComponent::Y);
    sx.currentVelocity = current;
    sy.currentVelocity = current;
    const Result<CfdDiffusionSystem> slipX =
        cfd::assembleCfdDiffusion(hand.topology, hand.geometry, slipping, sx);
    const Result<CfdDiffusionSystem> slipY =
        cfd::assembleCfdDiffusion(hand.topology, hand.geometry, slipping, sy);
    REQUIRE(slipX.has_value());
    REQUIRE(slipY.has_value());
    CHECK_FALSE(slipX->matrix == slipY->matrix);
}

// ---------------------------------------------------------------------------
// Phase order
// ---------------------------------------------------------------------------

TEST_CASE("CfdDiffusion_AnUnusableViscosityIsRefusedBeforeAMissingVelocity", "[cfd][diff]") {
    // Two faults at once, and the EARLIER phase must win. Without this the
    // phase order is a comment: a reordering would leave every single-fault
    // case passing.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary =
        hand.uniform(CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), SlipWall{someFace()}});
    // A slip wall with no current velocity would be SymmetryNeedsTheCurrentVelocity,
    // and a bad viscosity is an earlier phase.
    const CfdDiffusionInput both = plainInput(DynamicViscosity::fromSi(-1.0), VectorComponent::X);
    const std::optional<CfdDiffusionProblem> predicted =
        cfd::cfdDiffusionProblem(hand.topology, hand.geometry, boundary, both);
    REQUIRE(predicted.has_value());
    CHECK(*predicted == CfdDiffusionProblem::ViscosityNotUsable);

    // And with the viscosity fixed, the later fault is what is reported -- so
    // the second fault really was present and was being masked.
    const CfdDiffusionInput later = plainInput(1.002_mPa_s, VectorComponent::X);
    const std::optional<CfdDiffusionProblem> second =
        cfd::cfdDiffusionProblem(hand.topology, hand.geometry, boundary, later);
    REQUIRE(second.has_value());
    CHECK(*second == CfdDiffusionProblem::SymmetryNeedsTheCurrentVelocity);
}

TEST_CASE("CfdDiffusion_AGeometryOfAnotherMeshIsRefusedFirst", "[cfd][diff]") {
    // The metrics of one mesh against the topology of another: the first phase,
    // and it is checked before anything reads either.
    const Hand two = Hand::make(orthogonalPoints(), kTwoCells);
    const Hand one = Hand::make(oneCellPoints(), kOneCell);
    const PreparedCfdBoundary boundary = two.uniform(
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});

    // A deliberately bad viscosity as well, so the PHASE ORDER is what decides.
    const CfdDiffusionInput input = plainInput(DynamicViscosity::fromSi(-1.0), VectorComponent::X);
    const std::optional<CfdDiffusionProblem> predicted =
        cfd::cfdDiffusionProblem(two.topology, one.geometry, boundary, input);
    REQUIRE(predicted.has_value());
    CHECK(*predicted == CfdDiffusionProblem::GeometryDescribesAnotherTopology);
}

TEST_CASE("CfdDiffusion_ABoundaryOfAnotherTopologyIsRefusedByName", "[cfd][diff]") {
    // A seam-built boundary makes no currency claim, so what is checked is the
    // STRUCTURAL agreement: a boundary holding a different number of faces
    // cannot be indexed by this topology.
    const Hand two = Hand::make(orthogonalPoints(), kTwoCells);
    const Hand one = Hand::make(oneCellPoints(), kOneCell);
    REQUIRE(two.topology.boundaryFaces() != one.topology.boundaryFaces());
    const PreparedCfdBoundary other = one.uniform(
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});
    const std::optional<CfdDiffusionProblem> predicted = cfd::cfdDiffusionProblem(
        two.topology, two.geometry, other, plainInput(1.002_mPa_s, VectorComponent::X));
    REQUIRE(predicted.has_value());
    CHECK(*predicted == CfdDiffusionProblem::BoundaryDescribesAnotherTopology);
}

// ---------------------------------------------------------------------------
// The sparse container's own gate
// ---------------------------------------------------------------------------

TEST_CASE("CfdDiffusion_TheSparseGateRefusesEachMalformedLayoutByName", "[cfd][diff]") {
    using Index = cfd::CfdSparseIndex;
    const auto tag = [](std::size_t rows, std::size_t columns, std::vector<Index> rowStart,
                        std::vector<Index> inner, std::vector<double> values,
                        std::optional<double> symmetry = std::nullopt) {
        const std::optional<CfdSparseProblem> predicted =
            cfd::cfdSparseProblem(rows, columns, rowStart, inner, values, symmetry);
        const std::expected<CfdSparseMatrix, cfd::CfdSparseFailure> built = CfdSparseMatrix::fromCsr(
            rows, columns, std::move(rowStart), std::move(inner), std::move(values), symmetry);
        // The predictor and the door never disagree.
        CHECK(predicted.has_value() == !built.has_value());
        if (!built.has_value() && predicted.has_value()) {
            CHECK(built.error().problem == *predicted);
        }
        return predicted;
    };

    // A well-formed 2x2, so the helper is shown to accept as well as refuse.
    CHECK_FALSE(tag(2, 2, {0, 2, 4}, {0, 1, 0, 1}, {2.0, -1.0, -1.0, 2.0}).has_value());

    CHECK(tag(2, 2, {0, 2}, {0, 1}, {1.0, 1.0}) == CfdSparseProblem::DimensionsInconsistent);
    CHECK(tag(2, 2, {0, 2, 4}, {0, 1, 0}, {1.0, 1.0}) == CfdSparseProblem::DimensionsInconsistent);
    CHECK(tag(2, 2, {1, 2, 4}, {0, 1, 0, 1}, {1.0, 1.0, 1.0, 1.0}) ==
          CfdSparseProblem::RowStartInvalid);
    CHECK(tag(2, 2, {0, 3, 2}, {0, 1, 0, 1}, {1.0, 1.0, 1.0, 1.0}) ==
          CfdSparseProblem::RowStartInvalid);
    CHECK(tag(2, 2, {0, 2, 3}, {0, 1, 0, 1}, {1.0, 1.0, 1.0, 1.0}) ==
          CfdSparseProblem::RowStartInvalid);
    CHECK(tag(2, 2, {0, 2, 4}, {0, 5, 0, 1}, {1.0, 1.0, 1.0, 1.0}) ==
          CfdSparseProblem::ColumnOutOfRange);
    CHECK(tag(2, 2, {0, 2, 4}, {1, 0, 0, 1}, {1.0, 1.0, 1.0, 1.0}) ==
          CfdSparseProblem::ColumnsNotStrictlyAscending);
    CHECK(tag(2, 2, {0, 2, 4}, {0, 0, 0, 1}, {1.0, 1.0, 1.0, 1.0}) ==
          CfdSparseProblem::ColumnsNotStrictlyAscending);
    CHECK(tag(2, 2, {0, 2, 4}, {0, 1, 0, 1},
              {1.0, std::numeric_limits<double>::quiet_NaN(), 1.0, 1.0}) ==
          CfdSparseProblem::NonFiniteEntry);
    // Asymmetric, and only refused when symmetry is REQUIRED -- so the same
    // layout passes without the request, which is what makes the gate a gate
    // rather than a blanket rule.
    CHECK_FALSE(tag(2, 2, {0, 2, 4}, {0, 1, 0, 1}, {2.0, -1.0, -3.0, 2.0}).has_value());
    CHECK(tag(2, 2, {0, 2, 4}, {0, 1, 0, 1}, {2.0, -1.0, -3.0, 2.0}, 1e-13) ==
          CfdSparseProblem::NotSymmetric);
}

TEST_CASE("CfdDiffusion_TheSparseMatrixReadsRowSumsAndProductsReproducibly", "[cfd][diff]") {
    const std::expected<CfdSparseMatrix, cfd::CfdSparseFailure> built = CfdSparseMatrix::fromCsr(
        2, 2, {0, 2, 4}, {0, 1, 0, 1}, {2.0, -1.0, -1.0, 3.0}, 1e-13);
    REQUIRE(built.has_value());
    CHECK(built->rows() == 2);
    CHECK(built->nonZeros() == 4);
    CHECK_THAT(built->rowSumSi(0), WithinRel(1.0, 1e-15));
    CHECK_THAT(built->rowSumSi(1), WithinRel(2.0, 1e-15));
    // Out of range reads zero rather than being undefined.
    CHECK(built->rowSumSi(7) == 0.0);
    CHECK(built->coeffSi(7, 7) == 0.0);
    CHECK(built->largestMagnitude() == 3.0);
    CHECK(built->largestSymmetryError() == 0.0);

    const std::vector<double> x{1.0, 2.0};
    const Result<std::vector<double>> product = built->multiply(x);
    REQUIRE(product.has_value());
    CHECK_THAT((*product)[0], WithinRel(0.0 + 2.0 * 1.0 - 1.0 * 2.0, 1e-15));
    CHECK_THAT((*product)[1], WithinRel(-1.0 * 1.0 + 3.0 * 2.0, 1e-15));

    // A length mismatch is a refusal, not a read past the end.
    const std::vector<double> wrong{1.0};
    CHECK_FALSE(built->multiply(wrong).has_value());
}

// ---------------------------------------------------------------------------
// The reference bodies: the STRUCTURAL claims, on the meshes a solve will face
// ---------------------------------------------------------------------------
//
// The arithmetic is checked on the hand-built pairs above, because no reference
// mesh is orthogonal and none has a closed-form coefficient. What the reference
// bodies carry is everything that does not need exact numbers: that every cell
// is reached, that the pattern is the adjacency, that the signs hold at scale,
// that the matrix is symmetric to the last bit on 808 cells as well as on two,
// and that the reported deferred fraction really is the geometry's worst angle.

namespace {

void assignWaterTo(Document& document) {
    features::MaterialDefinition water;
    water.designation = "Water";
    water.mechanical.density = materials::MaterialProperty<Density>::known(998_kg_per_m3);
    water.thermal.dynamicViscosity =
        materials::MaterialProperty<DynamicViscosity>::known(1.002_mPa_s);
    const Result<MaterialId> id = features::createMaterial(document, "CfdWater", water);
    REQUIRE(id.has_value());
    REQUIRE(features::assignMaterial(document, *id).has_value());
}

/// A reference body, meshed, with its topology, its metrics and a boundary that
/// makes every facet a no-slip wall.
struct Reference {
    meshref::MeshedReference model;
    Result<cfd::CfdModel> input;
    Result<cfd::CfdMeshContract> contract;
    Result<CfdTopology> topology;
    Result<CfdGeometry> geometry;

    explicit Reference(bettercad::reference::MeshReferenceModelKind kind) : Reference(document(kind)) {}

    explicit Reference(Document built)
        : model(std::move(built)), input(std::unexpected(Error{ErrorCode::FailedPrecondition, "not yet"})),
          contract(std::unexpected(Error{ErrorCode::FailedPrecondition, "not yet"})),
          topology(std::unexpected(Error{ErrorCode::FailedPrecondition, "not yet"})),
          geometry(std::unexpected(Error{ErrorCode::FailedPrecondition, "not yet"})) {
        assignWaterTo(model.document());
        (void)model.regenerate();
        (void)model.require();
        input = cfd::requireCfdModel(model.document(), model.regenerator(), model.mesher(),
                                     model.control());
        INFO((input.has_value() ? std::string{} : input.error().message));
        REQUIRE(input.has_value());
        contract = cfd::requireCfdMesh(*input);
        REQUIRE(contract.has_value());
        topology = cfd::requireCfdTopology(*input, *contract);
        INFO((topology.has_value() ? std::string{} : topology.error().message));
        REQUIRE(topology.has_value());
        geometry = cfd::requireCfdGeometry(*input, *topology);
        INFO((geometry.has_value() ? std::string{} : geometry.error().message));
        REQUIRE(geometry.has_value());
    }

    static Document document(bettercad::reference::MeshReferenceModelKind kind) {
        Result<Document> built = bettercad::reference::buildMeshReferenceModel(kind);
        REQUIRE(built.has_value());
        return std::move(*built);
    }

    [[nodiscard]] PreparedCfdBoundary wallEverywhere() const {
        std::vector<ElementId> ascending;
        for (std::size_t b = 0; b < topology->boundaryFaces(); ++b) {
            ascending.push_back(topology->triangleOf(BoundaryFaceIndex::fromValue(b)));
        }
        std::ranges::sort(ascending, [](ElementId a, ElementId b) { return a.value() < b.value(); });
        const CfdResolvedTarget target{
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}},
            ascending};
        const std::array<CfdResolvedTarget, 1> targets{target};
        const std::optional<Point3D> centroid = geometry->cellCentroid(CellIndex::fromValue(0));
        REQUIRE(centroid.has_value());
        const cfd::CfdReferencePressure datum{.value = 0_Pa, .location = *centroid};
        Result<PreparedCfdBoundary> bound =
            cfd::bindCfdBoundary(*topology, *geometry, input->mesh().mesh(), targets, 0, datum,
                                 std::nullopt, std::nullopt);
        INFO((bound.has_value() ? std::string{} : bound.error().message));
        REQUIRE(bound.has_value());
        return std::move(*bound);
    }
};

} // namespace

TEST_CASE("CfdDiffusion_EveryReferenceBodyAssemblesWithThePatternOfItsTopology", "[cfd][diff]") {
    std::size_t meshed = 0;
    for (const bettercad::reference::MeshReferenceModelInfo& info :
         bettercad::reference::kMeshReferenceModels) {
        if (!info.expectMesh) {
            // The one model with no body, and asserted to be that one: a filter
            // that quietly grew would make this sweep smaller than it claims.
            CHECK(info.kind == bettercad::reference::MeshReferenceModelKind::OpenProfile);
            continue;
        }
        INFO(info.id << " " << info.name);
        const Reference body(info.kind);
        const PreparedCfdBoundary boundary = body.wallEverywhere();
        const Result<CfdDiffusionSystem> system = cfd::assembleCfdDiffusion(
            *body.topology, *body.geometry, boundary, plainInput(1.002_mPa_s, VectorComponent::X));
        INFO((system.has_value() ? std::string{} : system.error().message));
        REQUIRE(system.has_value());
        ++meshed;

        const CfdSparseMatrix& a = system->matrix;
        CHECK(a.rows() == body.topology->cells());
        CHECK(a.columns() == body.topology->cells());
        // One diagonal per cell plus two entries per internal face, and nothing
        // else: the pattern is connectivity, with no pruning of exact zeros.
        CHECK(a.nonZeros() == body.topology->cells() + 2 * body.topology->internalFaces());
        CHECK(system->summary.internalFaces == body.topology->internalFaces());
        CHECK(system->summary.fixedValueFaces == body.topology->boundaryFaces());
        CHECK(system->summary.cells == body.topology->cells());
        // Every cell was reached: no row is empty.
        for (std::size_t row = 0; row < a.rows(); ++row) {
            CHECK(a.rowStart()[row + 1] > a.rowStart()[row]);
        }
        // Symmetric to the last bit at scale, not only on two cells.
        CHECK(a.largestSymmetryError() == 0.0);
    }
    CHECK(meshed == bettercad::reference::kMeshReferenceModels.size() - 1);
    CHECK(meshed == 8);
}

TEST_CASE("CfdDiffusion_EveryReferenceBodyHasAPositiveDiagonalAndNonPositiveOffDiagonals",
          "[cfd][diff]") {
    std::size_t meshed = 0;
    std::size_t offDiagonalsSeen = 0;
    for (const bettercad::reference::MeshReferenceModelInfo& info :
         bettercad::reference::kMeshReferenceModels) {
        if (!info.expectMesh) {
            CHECK(info.kind == bettercad::reference::MeshReferenceModelKind::OpenProfile);
            continue;
        }
        INFO(info.id << " " << info.name);
        const Reference body(info.kind);
        const PreparedCfdBoundary boundary = body.wallEverywhere();
        const Result<CfdDiffusionSystem> system = cfd::assembleCfdDiffusion(
            *body.topology, *body.geometry, boundary, plainInput(1.002_mPa_s, VectorComponent::Y));
        REQUIRE(system.has_value());
        ++meshed;

        const CfdSparseMatrix& a = system->matrix;
        for (std::size_t row = 0; row < a.rows(); ++row) {
            for (auto slot = a.rowStart()[row]; slot < a.rowStart()[row + 1]; ++slot) {
                const auto index = static_cast<std::size_t>(slot);
                const auto column = static_cast<std::size_t>(a.inner()[index]);
                const double value = a.values()[index];
                if (column == row) {
                    CHECK(value > 0.0);
                } else {
                    CHECK(value < 0.0);
                    ++offDiagonalsSeen;
                }
            }
        }
    }
    CHECK(meshed == 8);
    // The off-diagonal loop is not vacuous: 2 * (6 + 946 + 172 + 1472 + 18 + 18
    // + 18 + 528) internal faces across the eight bodies.
    CHECK(offDiagonalsSeen == 2 * (6 + 946 + 172 + 1472 + 18 + 18 + 18 + 528));
}

TEST_CASE("CfdDiffusion_TheDeferredFractionIsTheSineOfTheWorstNonOrthogonality", "[cfd][diff]") {
    // AN INDEPENDENT READING OF THE SAME NUMBER. ADR-065 records the worst
    // non-orthogonality of every reference mesh, and for the over-relaxed
    // decomposition the deferred fraction is `sin(theta)`. So the summary's
    // figure must be the sine of the geometry's own worst angle -- computed here
    // from `nonOrthogonality`, which the operator never calls.
    std::size_t meshed = 0;
    for (const bettercad::reference::MeshReferenceModelInfo& info :
         bettercad::reference::kMeshReferenceModels) {
        if (!info.expectMesh) {
            // The kind assertion and the count below were missing from this one
            // sweep while both its siblings had them -- a review found it. A
            // filter that quietly grew would otherwise make this sweep smaller
            // than it claims, or empty.
            CHECK(info.kind == bettercad::reference::MeshReferenceModelKind::OpenProfile);
            continue;
        }
        INFO(info.id << " " << info.name);
        const Reference body(info.kind);
        const PreparedCfdBoundary boundary = body.wallEverywhere();
        const Result<CfdDiffusionSystem> system = cfd::assembleCfdDiffusion(
            *body.topology, *body.geometry, boundary, plainInput(1.002_mPa_s, VectorComponent::Z));
        REQUIRE(system.has_value());
        ++meshed;

        double worstSine = 0.0;
        for (std::size_t f = 0; f < body.topology->faces(); ++f) {
            const std::optional<Angle> angle =
                body.geometry->nonOrthogonality(FaceIndex::fromValue(f));
            REQUIRE(angle.has_value());
            worstSine = std::max(worstSine, std::sin(angle->si()));
        }
        CHECK_THAT(system->summary.deferredFraction, WithinRel(worstSine, 1e-9));
        // And it is a real quantity on these meshes, not a rounding artefact:
        // ADR-063 F1 measured 31 to 66 degrees mean, so the worst sine is large.
        CHECK(system->summary.deferredFraction > 0.5);
    }
    CHECK(meshed == bettercad::reference::kMeshReferenceModels.size() - 1);
    CHECK(meshed == 8);
}

// ---------------------------------------------------------------------------
// THE SOURCE VECTOR
// ---------------------------------------------------------------------------
//
// Both reviews found the same hole and called it the one thing not to ship: the
// matrix was tested from several independent directions while `source` was
// covered only by `maxAbs(source) == 0.0` or `> 0.0`. Seven distinct sign and
// index mutations survived the suite, including an inverted non-orthogonal
// correction -- the term ADR-063 F1 calls mandatory. Everything below exists to
// close that.

namespace {

/// One condition per boundary facet, so the three arms can be made to coexist.
///
/// `Hand::uniform` puts ONE kind on every facet, which is why no assembly in
/// the suite saw two arms at once until this existed.
struct FacetCondition {
    ElementId facet{};
    CfdBoundaryCondition condition;
};

[[nodiscard]] PreparedCfdBoundary bindPerFacet(const Hand& hand,
                                               std::span<const FacetCondition> assignment,
                                               bool needsDatum) {
    std::vector<CfdResolvedTarget> targets;
    for (const FacetCondition& one : assignment) {
        targets.push_back(CfdResolvedTarget{one.condition, {one.facet}});
    }
    std::optional<cfd::CfdReferencePressure> datum;
    if (needsDatum) {
        datum = cfd::CfdReferencePressure{.value = 0_Pa, .location = hand.insidePoint()};
    }
    Result<PreparedCfdBoundary> bound = cfd::bindCfdBoundary(
        hand.topology, hand.geometry, hand.mesh, targets, 0, datum, std::nullopt, std::nullopt);
    INFO((bound.has_value() ? std::string{} : bound.error().message));
    REQUIRE(bound.has_value());
    return std::move(*bound);
}

/// The boundary facet whose three nodes all satisfy @p onPlane, found from the
/// MESH rather than from the geometry's normals, so a fixture does not depend on
/// the accessor a test then asserts about.
template <typename Predicate>
[[nodiscard]] ElementId facetWhere(const Hand& hand, Predicate onPlane) {
    for (std::size_t b = 0; b < hand.topology.boundaryFaces(); ++b) {
        const BoundaryFaceIndex slot = BoundaryFaceIndex::fromValue(b);
        const FaceIndex face = hand.topology.faceOf(slot);
        const std::array<NodeId, 3> nodes = hand.topology.faceNodes(face);
        bool all = true;
        for (const NodeId id : nodes) {
            const meshing::Node* node = hand.mesh.findNode(id);
            REQUIRE(node != nullptr);
            all = all && onPlane(node->position);
        }
        if (all) {
            return hand.topology.triangleOf(slot);
        }
    }
    FAIL("no boundary facet lies in the requested plane");
    return ElementId{};
}

[[nodiscard]] bool nearZero(Length value) { return std::abs(value.si()) < 1e-12; }

} // namespace

TEST_CASE("CfdDiffusion_AMovingWallPrescribesItsOwnComponentIntoTheSource", "[cfd][diff]") {
    // THE NON-ZERO DIRICHLET ARM, which was dead. Every fixture in this file
    // used NoSlipWall (U = 0), PressureOutlet (ZeroGradient) or SlipWall, so
    // `rhs[owner] += gamma * prescribed` only ever ran with `prescribed == 0`
    // and two tests positively asserted the source was zero -- which is what a
    // completely dead arm produces. A wrong component, a dropped `gamma` and a
    // sign flip all survived.
    //
    // The wall velocity must LIE IN THE FACE (P19-BC-001 refuses a moving wall
    // whose velocity does not), so it is chosen tangential to the slanted
    // facet `x + y + z = 10 mm`, whose normal is (1,1,1)/sqrt(3): the velocity
    // (2, -1, -1) m/s dots to zero with it and has THREE DISTINCT NON-ZERO
    // components, which is what pins the component selection.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const Velocity3 wall{Velocity::fromSi(2.0), Velocity::fromSi(-1.0), Velocity::fromSi(-1.0)};
    const ElementId slanted = facetWhere(hand, [](const Point3D& p) {
        return std::abs((p.x + p.y + p.z).si() - 0.010) < 1e-12;
    });

    std::vector<FacetCondition> assignment;
    std::uint64_t id = 1;
    for (const ElementId facet : hand.facets()) {
        if (facet.value() == slanted.value()) {
            assignment.push_back({facet, CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(id++),
                                                              MovingWall{someFace(id), wall}}});
        } else {
            assignment.push_back({facet, CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(id++),
                                                              NoSlipWall{someFace(id)}}});
        }
    }
    const PreparedCfdBoundary boundary = bindPerFacet(hand, assignment, true);
    const DynamicViscosity mu = 1.002_mPa_s;

    // The slanted face's own coefficient, from the oracle.
    FaceIndex slantedFace = FaceIndex::fromValue(0);
    for (std::size_t f = 0; f < hand.topology.faces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        const std::optional<BoundaryFaceIndex> slot = hand.topology.boundaryFaceOf(face);
        if (slot.has_value() && hand.topology.triangleOf(*slot).value() == slanted.value()) {
            slantedFace = face;
        }
    }
    const double gammaSlanted = gammaOracle(hand.geometry, slantedFace, mu);

    for (const VectorComponent component : cfd::kVectorComponents) {
        INFO(cfd::toString(component));
        const Result<CfdDiffusionSystem> system =
            cfd::assembleCfdDiffusion(hand.topology, hand.geometry, boundary, plainInput(mu, component));
        INFO((system.has_value() ? std::string{} : system.error().message));
        REQUIRE(system.has_value());
        REQUIRE(system->source.size() == 1);
        // Only the moving wall prescribes a non-zero value, so the whole source
        // is that one face's `gamma * U_b,i`. The three components give 2, -1
        // and -1 m/s, so a wrong component or a dropped gamma shows here.
        const double expected = gammaSlanted * wall.component(component).si();
        CHECK_THAT(system->source[0], WithinRel(expected, 1e-13));
    }

    // And the three differ from each other, so the loop is not agreeing by
    // accident on a symmetric value.
    const Result<CfdDiffusionSystem> x =
        cfd::assembleCfdDiffusion(hand.topology, hand.geometry, boundary, plainInput(mu, VectorComponent::X));
    const Result<CfdDiffusionSystem> y =
        cfd::assembleCfdDiffusion(hand.topology, hand.geometry, boundary, plainInput(mu, VectorComponent::Y));
    REQUIRE(x.has_value());
    REQUIRE(y.has_value());
    CHECK(x->source[0] != y->source[0]);
    CHECK_THAT(x->source[0] / y->source[0], WithinRel(-2.0, 1e-13));
}

TEST_CASE("CfdDiffusion_ASlipWallsCrossTermsReadTheComponentBlockedVelocity", "[cfd][diff]") {
    // THE CROSS TERMS, which were dead: every slip-wall case passed an all-zero
    // current velocity, so `currentVelocity[j * cells + owner]` was never read
    // at a non-zero index-dependent value. A cell-blocked index
    // (`owner * 3 + j`), a dropped `j == componentIndex` skip and an inverted
    // sign all survived.
    //
    // Two cells, and the three components are given DISTINCT values per cell,
    // so a cell-blocked read returns a different number rather than the same
    // one -- which is what makes the layout observable.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    REQUIRE(hand.topology.cells() == 2);
    const DynamicViscosity mu = 1.002_mPa_s;
    const PreparedCfdBoundary boundary =
        hand.uniform(CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), SlipWall{someFace()}});

    // component-blocked: index = component * cells + cell  (ADR-066)
    const std::size_t cells = hand.topology.cells();
    std::vector<double> current(3 * cells, 0.0);
    const std::array<std::array<double, 2>, 3> values{{{1.0, 2.0}, {3.0, 5.0}, {7.0, 11.0}}};
    for (std::size_t c = 0; c < 3; ++c) {
        for (std::size_t cell = 0; cell < cells; ++cell) {
            current[c * cells + cell] = values[c][cell];
        }
    }

    for (const VectorComponent component : cfd::kVectorComponents) {
        INFO(cfd::toString(component));
        CfdDiffusionInput input = plainInput(mu, component);
        input.currentVelocity = current;
        const Result<CfdDiffusionSystem> system =
            cfd::assembleCfdDiffusion(hand.topology, hand.geometry, boundary, input);
        INFO((system.has_value() ? std::string{} : system.error().message));
        REQUIRE(system.has_value());

        const auto axis = static_cast<std::size_t>(component);
        for (std::size_t cell = 0; cell < cells; ++cell) {
            // The oracle: -sum_f Gamma_f n_i sum_{j != i} n_j U_j,P, built from
            // the face normals and the SAME component-blocked convention,
            // written out here rather than read from the operator.
            double expected = 0.0;
            for (std::size_t f = 0; f < hand.topology.faces(); ++f) {
                const FaceIndex face = FaceIndex::fromValue(f);
                if (hand.topology.isInternal(face) || hand.topology.owner(face).value() != cell) {
                    continue;
                }
                const std::optional<Direction3D> normal = hand.geometry.faceUnitNormal(face);
                REQUIRE(normal.has_value());
                const std::array<double, 3> n{normal->x(), normal->y(), normal->z()};
                double cross = 0.0;
                for (std::size_t j = 0; j < 3; ++j) {
                    if (j == axis) {
                        continue;
                    }
                    cross += n[j] * values[j][cell];
                }
                expected -= gammaOracle(hand.geometry, face, mu) * n[axis] * cross;
            }
            CHECK_THAT(system->source[cell], WithinRel(expected, 1e-12));
        }
    }

    // And the source is NOT zero, so the oracle above is comparing against
    // something: an all-zero current velocity would make this vacuous, which is
    // exactly what the earlier cases did.
    CfdDiffusionInput probe = plainInput(mu, VectorComponent::X);
    probe.currentVelocity = current;
    const Result<CfdDiffusionSystem> nonZero =
        cfd::assembleCfdDiffusion(hand.topology, hand.geometry, boundary, probe);
    REQUIRE(nonZero.has_value());
    CHECK(maxAbs(nonZero->source) > 0.0);
}

TEST_CASE("CfdDiffusion_TheCorrectionLandsOnBothSidesWithOppositeSigns", "[cfd][diff]") {
    // THE CORRECTION'S VALUE AND SIGN, THROUGH THE ASSEMBLY. The only test that
    // supplied a gradient asserted `maxAbs(source) > 0.0`, so the entire
    // non-orthogonal correction could have been sign-flipped -- the sharpest of
    // the seven surviving mutations, because ADR-063 F1 calls that term
    // mandatory.
    //
    // `T_f` is wound out of the OWNER, so the neighbour sees its negation, and
    // the two contributions must be equal and opposite.
    const Hand hand = Hand::make(analyticPoints(), kAnalyticPair);
    const DynamicViscosity mu = 1.002_mPa_s;
    // Every boundary face ZeroGradient, so NOTHING but the internal face's
    // correction reaches the source and the assertion is not diluted.
    const PreparedCfdBoundary boundary = hand.uniform(CfdBoundaryCondition{
        CfdBoundaryConditionId::fromValue(1), PressureOutlet{someFace(), 0_Pa}});

    const std::array<double, 3> g{7.2, -3.5, 11.4};
    std::vector<VelocityGradient3> gradients(hand.topology.faces());
    for (VelocityGradient3& one : gradients) {
        one.x = VelocityGradient::fromSi(g[0]);
        one.y = VelocityGradient::fromSi(g[1]);
        one.z = VelocityGradient::fromSi(g[2]);
    }
    CfdDiffusionInput input = plainInput(mu, VectorComponent::X);
    input.faceGradient = gradients;
    const Result<CfdDiffusionSystem> system =
        cfd::assembleCfdDiffusion(hand.topology, hand.geometry, boundary, input);
    INFO((system.has_value() ? std::string{} : system.error().message));
    REQUIRE(system.has_value());

    FaceIndex internal = FaceIndex::fromValue(0);
    for (std::size_t f = 0; f < hand.topology.faces(); ++f) {
        if (hand.topology.isInternal(FaceIndex::fromValue(f))) {
            internal = FaceIndex::fromValue(f);
        }
    }
    const std::optional<CfdFaceDecomposition> split = cfd::cfdFaceDecomposition(hand.geometry, internal);
    REQUIRE(split.has_value());
    const double correction = mu.si() * (g[0] * split->deferredArm.x.si() +
                                         g[1] * split->deferredArm.y.si() +
                                         g[2] * split->deferredArm.z.si());
    REQUIRE(std::abs(correction) > 0.0);

    const std::size_t owner = hand.topology.owner(internal).value();
    const std::optional<CellIndex> neighbour = hand.topology.neighbour(internal);
    REQUIRE(neighbour.has_value());
    const std::size_t other = neighbour->value();

    // THE SIGN, not just the magnitude.
    CHECK_THAT(system->source[owner], WithinRel(correction, 1e-13));
    CHECK_THAT(system->source[other], WithinRel(-correction, 1e-13));
    // And equal and opposite, so a correction applied twice to one side fails.
    CHECK_THAT(system->source[owner] + system->source[other], WithinAbs(0.0, 1e-14 * std::abs(correction)));
}

TEST_CASE("CfdDiffusion_AFaceVaryingGradientIsReadAtItsOwnFace", "[cfd][diff]") {
    // THE FACE INDEX. The earlier correction test filled every face with the
    // SAME gradient, so `input.faceGradient[faceSlot]` could have read any
    // index -- off by one, the boundary index, or a hard-coded zero -- and
    // passed. A gradient that varies per face makes the index observable.
    const Hand hand = Hand::make(analyticPoints(), kAnalyticPair);
    const DynamicViscosity mu = 1.002_mPa_s;
    // NO-SLIP, NOT ZERO-GRADIENT, AND THE MUTATION SWEEP IS WHY. With every
    // boundary face zero-gradient, the ONLY face contributing a correction is
    // the single internal one -- and `isInternal(face)` is
    // `face.value() < internalFaces`, so internal faces are numbered FIRST and
    // that face is index 0. Reading the gradient at a hard-coded index 0 was
    // therefore indistinguishable from reading it at its own face, and the
    // mutation `faceGradient[faceSlot * 0]` SURVIVED this test while its name
    // claimed to pin exactly that. A prescribed-value boundary makes all six
    // boundary faces contribute too, at indices 1 through 6, so a fixed index
    // is wrong for every one of them.
    const PreparedCfdBoundary boundary = hand.uniform(
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});

    std::vector<VelocityGradient3> gradients(hand.topology.faces());
    for (std::size_t f = 0; f < gradients.size(); ++f) {
        // Distinct per face, and distinct per component.
        gradients[f].x = VelocityGradient::fromSi(1.0 + static_cast<double>(f));
        gradients[f].y = VelocityGradient::fromSi(-2.0 - static_cast<double>(f));
        gradients[f].z = VelocityGradient::fromSi(3.0 + 2.0 * static_cast<double>(f));
    }
    CfdDiffusionInput input = plainInput(mu, VectorComponent::X);
    input.faceGradient = gradients;
    const Result<CfdDiffusionSystem> system =
        cfd::assembleCfdDiffusion(hand.topology, hand.geometry, boundary, input);
    REQUIRE(system.has_value());

    // The oracle: EVERY contributing face's own gradient against its own
    // deferred arm, signed by the winding. A no-slip wall prescribes zero, so
    // the Dirichlet term adds nothing and the source is the corrections alone.
    std::vector<double> expected(hand.topology.cells(), 0.0);
    std::size_t contributingAboveZero = 0;
    for (std::size_t f = 0; f < hand.topology.faces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        const std::optional<CfdFaceDecomposition> split = cfd::cfdFaceDecomposition(hand.geometry, face);
        REQUIRE(split.has_value());
        const double term = mu.si() * (gradients[f].x.si() * split->deferredArm.x.si() +
                                       gradients[f].y.si() * split->deferredArm.y.si() +
                                       gradients[f].z.si() * split->deferredArm.z.si());
        if (hand.topology.isInternal(face)) {
            const std::optional<CellIndex> neighbour = hand.topology.neighbour(face);
            REQUIRE(neighbour.has_value());
            expected[hand.topology.owner(face).value()] += term;
            expected[neighbour->value()] -= term;
        } else {
            expected[hand.topology.owner(face).value()] += term;
        }
        if (f > 0) {
            ++contributingAboveZero;
        }
    }
    // THE PREMISE THAT MAKES THE INDEX OBSERVABLE: faces above index 0
    // contribute, so a hard-coded index 0 is wrong for them.
    CHECK(contributingAboveZero == hand.topology.faces() - 1);
    CHECK(contributingAboveZero >= 6);
    for (std::size_t cell = 0; cell < hand.topology.cells(); ++cell) {
        INFO(cell);
        CHECK_THAT(system->source[cell], WithinRel(expected[cell], 1e-12));
    }
    CHECK(maxAbs(expected) > 0.0);
}

// ---------------------------------------------------------------------------
// A mixed boundary, and the null-mode regression
// ---------------------------------------------------------------------------

TEST_CASE("CfdDiffusion_AMixedBoundaryPartitionsTheCountsAndTheDiagonal", "[cfd][diff]") {
    // THREE ARMS AT ONCE, which no assembly in the suite saw. A counter
    // incremented in the wrong switch arm, or two kinds accumulating into one
    // diagonal, needs two kinds present to show.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    REQUIRE(hand.topology.boundaryFaces() == 4);
    const DynamicViscosity mu = 1.002_mPa_s;

    const ElementId atZ = facetWhere(hand, [](const Point3D& p) { return nearZero(p.z); });
    const ElementId atY = facetWhere(hand, [](const Point3D& p) { return nearZero(p.y); });
    const ElementId atX = facetWhere(hand, [](const Point3D& p) { return nearZero(p.x); });

    std::vector<FacetCondition> assignment;
    std::uint64_t id = 1;
    for (const ElementId facet : hand.facets()) {
        const std::uint64_t mine = id++;
        if (facet.value() == atZ.value()) {
            assignment.push_back({facet, CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(mine),
                                                              SlipWall{someFace(mine)}}});
        } else if (facet.value() == atY.value()) {
            assignment.push_back({facet, CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(mine),
                                                              PressureOutlet{someFace(mine), 0_Pa}}});
        } else if (facet.value() == atX.value()) {
            assignment.push_back({facet, CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(mine),
                                                              NoSlipWall{someFace(mine)}}});
        } else {
            assignment.push_back({facet, CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(mine),
                                                              NoSlipWall{someFace(mine)}}});
        }
    }
    // A pressure outlet fixes the pressure, so no reference pressure.
    const PreparedCfdBoundary boundary = bindPerFacet(hand, assignment, false);

    const std::vector<double> current(3 * hand.topology.cells(), 0.0);
    CfdDiffusionInput input = plainInput(mu, VectorComponent::X);
    input.currentVelocity = current;
    const Result<CfdDiffusionSystem> system =
        cfd::assembleCfdDiffusion(hand.topology, hand.geometry, boundary, input);
    INFO((system.has_value() ? std::string{} : system.error().message));
    REQUIRE(system.has_value());

    // THE PARTITION, which no test stated: the three counters must sum to the
    // boundary faces, with all three non-zero.
    const CfdDiffusionSummary& summary = system->summary;
    CHECK(summary.fixedValueFaces == 2);
    CHECK(summary.zeroGradientFaces == 1);
    CHECK(summary.symmetryFaces == 1);
    CHECK(summary.fixedValueFaces + summary.zeroGradientFaces + summary.symmetryFaces ==
          hand.topology.boundaryFaces());

    // And the diagonal is the per-kind oracle: Gamma for a prescribed face,
    // Gamma n_i^2 for a symmetry face, nothing for a zero-gradient face.
    double expected = 0.0;
    for (const FacetCondition& one : assignment) {
        FaceIndex face = FaceIndex::fromValue(0);
        for (std::size_t f = 0; f < hand.topology.faces(); ++f) {
            const FaceIndex candidate = FaceIndex::fromValue(f);
            const std::optional<BoundaryFaceIndex> slot = hand.topology.boundaryFaceOf(candidate);
            if (slot.has_value() && hand.topology.triangleOf(*slot).value() == one.facet.value()) {
                face = candidate;
            }
        }
        const double gamma = gammaOracle(hand.geometry, face, mu);
        switch (cfd::treatmentOf(one.condition.kind()).velocity) {
        case CfdBoundaryPolicy::FixedValue:
            expected += gamma;
            break;
        case CfdBoundaryPolicy::Symmetry: {
            const std::optional<Direction3D> normal = hand.geometry.faceUnitNormal(face);
            REQUIRE(normal.has_value());
            expected += gamma * normal->x() * normal->x();
            break;
        }
        case CfdBoundaryPolicy::ZeroGradient:
        case CfdBoundaryPolicy::Computed:
            break;
        }
    }
    CHECK_THAT(system->matrix.coeffSi(0, 0), WithinRel(expected, 1e-12));
}

TEST_CASE("CfdDiffusion_ASlipWallPerpendicularToTheComponentLeavesTheConstantMode", "[cfd][diff]") {
    // THE REGRESSION TEST FOR A DEFECT THIS MILESTONE FIXED DURING
    // IMPLEMENTATION, and which no test covered. `constantModeIsInTheNullSpace`
    // was first computed from `fixedValueFaces == 0 && symmetryFaces == 0`,
    // which is wrong for a symmetry face whose normal is perpendicular to this
    // component's axis: it adds `Gamma n_i^2` with `n_i == 0`, touches no
    // diagonal, and leaves the constant mode intact.
    //
    // ON A UNIFORM BOUNDARY THAT STATE IS UNREACHABLE -- a closed surface's
    // normals cannot all be perpendicular to one axis -- which is why both
    // reviews had to point out that the fix had no regression test and that a
    // MIXED boundary is what reaches it. Here the `z = 0` facet, whose normal is
    // (0, 0, -1), is the only symmetry face, and the component is X.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const DynamicViscosity mu = 1.002_mPa_s;
    const ElementId atZ = facetWhere(hand, [](const Point3D& p) { return nearZero(p.z); });

    std::vector<FacetCondition> assignment;
    std::uint64_t id = 1;
    for (const ElementId facet : hand.facets()) {
        const std::uint64_t mine = id++;
        if (facet.value() == atZ.value()) {
            assignment.push_back({facet, CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(mine),
                                                              SlipWall{someFace(mine)}}});
        } else {
            assignment.push_back({facet, CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(mine),
                                                              PressureOutlet{someFace(mine), 0_Pa}}});
        }
    }
    const PreparedCfdBoundary boundary = bindPerFacet(hand, assignment, false);

    const std::vector<double> current(3 * hand.topology.cells(), 0.0);
    CfdDiffusionInput input = plainInput(mu, VectorComponent::X);
    input.currentVelocity = current;
    const Result<CfdDiffusionSystem> system =
        cfd::assembleCfdDiffusion(hand.topology, hand.geometry, boundary, input);
    INFO((system.has_value() ? std::string{} : system.error().message));
    REQUIRE(system.has_value());

    // The premise: there IS a symmetry face, and its normal has no x part.
    REQUIRE(system->summary.symmetryFaces == 1);
    REQUIRE(system->summary.fixedValueFaces == 0);

    // THE DISCRIMINATION. The count-based implementation that was replaced says
    // false here, because `symmetryFaces > 0`. The measured one says true,
    // because nothing was added to a diagonal.
    CHECK(system->summary.constantModeIsInTheNullSpace);

    // And the matrix agrees, which is the independent reading: a one-cell mesh
    // with no internal face and no boundary contribution has a zero diagonal.
    CHECK(system->matrix.coeffSi(0, 0) == 0.0);
    CHECK(system->matrix.rowSumSi(0) == 0.0);
}

TEST_CASE("CfdDiffusion_AZeroGradientFaceDoesNotRaiseTheDeferredFraction", "[cfd][diff]") {
    // A ZeroGradient face defers NOTHING -- its diffusive flux is zero, so both
    // arms vanish -- and an earlier draft maxed the reported fraction over every
    // face including those, overstating what had been omitted while the header
    // said "assembled". A review found the two did not agree.
    //
    // The one-cell tet has no internal face, so with every facet ZeroGradient
    // nothing contributes at all and the fraction must be exactly zero, even
    // though the faces themselves are skew.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const PreparedCfdBoundary boundary = hand.uniform(CfdBoundaryCondition{
        CfdBoundaryConditionId::fromValue(1), PressureOutlet{someFace(), 0_Pa}});
    const Result<CfdDiffusionSystem> system = cfd::assembleCfdDiffusion(
        hand.topology, hand.geometry, boundary, plainInput(1.002_mPa_s, VectorComponent::X));
    REQUIRE(system.has_value());
    CHECK(system->summary.zeroGradientFaces == 4);
    CHECK(system->summary.deferredFraction == 0.0);

    // And the faces really are skew, so the zero above is the policy and not
    // the geometry: with the same mesh as walls, the fraction is large.
    const PreparedCfdBoundary walls = hand.uniform(
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});
    const Result<CfdDiffusionSystem> prescribed = cfd::assembleCfdDiffusion(
        hand.topology, hand.geometry, walls, plainInput(1.002_mPa_s, VectorComponent::X));
    REQUIRE(prescribed.has_value());
    CHECK(prescribed->summary.deferredFraction > 0.4);
}

// ---------------------------------------------------------------------------
// The face interpolation (ADR-069 Decision 7)
// ---------------------------------------------------------------------------

TEST_CASE("CfdDiffusion_TheInterpolationWeightIsBoundedAndTheViscosityIsTheIdentity",
          "[cfd][diff]") {
    // BOTH REVIEWS FOUND DECISION 7 CLAIMED AN IMPLEMENTATION THAT DID NOT
    // EXIST. It said the weight was "named and implemented now, with the single
    // call site a variable viscosity will enter through", and that a test
    // "asserts it over the full range of `w_f`" -- and a grep of the whole
    // module found no weight, no call site and no test. This is that test, and
    // the accessor it needs now exists.
    //
    // For a constant viscosity `mu_f = w mu_P + (1 - w) mu_N` is `mu` for every
    // weight, so the only thing a test can assert is that it IS the identity
    // over the range of weights a real mesh presents -- and that the weight is
    // a weight, which is the part a variable viscosity will depend on.
    const DynamicViscosity mu = 1.002_mPa_s;
    double smallest = 1.0;
    double largest = 0.0;
    std::size_t internalSeen = 0;
    std::size_t boundarySeen = 0;
    std::size_t meshed = 0;

    for (const bettercad::reference::MeshReferenceModelInfo& info :
         bettercad::reference::kMeshReferenceModels) {
        if (!info.expectMesh) {
            CHECK(info.kind == bettercad::reference::MeshReferenceModelKind::OpenProfile);
            continue;
        }
        INFO(info.id << " " << info.name);
        const Reference body(info.kind);
        ++meshed;
        for (std::size_t f = 0; f < body.topology->faces(); ++f) {
            const FaceIndex face = FaceIndex::fromValue(f);
            const std::optional<double> weight = cfd::cfdFaceInterpolationWeight(*body.geometry, face);
            REQUIRE(weight.has_value());
            // A WEIGHT, so STRICTLY inside (0, 1) for a held face: both normal
            // projections are positive because ADR-065 Decision 5 proves the
            // orthogonal distance is the quarter-sum of the two apex heights.
            //
            // THIS BOUND IS WHAT CAUGHT THE FIRST FORMULA. Written as the raw
            // distance ratio |C_N - C_f| / |C_N - C_P| the weight reached 3.61
            // on the Cylinder, because a skewed face's centroid is not on the
            // segment joining the two centroids -- so that ratio is not a
            // convex weight and does not define an interpolation.
            CHECK(*weight > 0.0);
            CHECK(*weight <= 1.0);
            if (body.topology->isInternal(face)) {
                smallest = std::min(smallest, *weight);
                largest = std::max(largest, *weight);
                ++internalSeen;
            } else {
                // A boundary face has no neighbour to interpolate with.
                CHECK(*weight == 1.0);
                ++boundarySeen;
            }
            // THE IDENTITY, at every one of those weights.
            CHECK(cfd::cfdFaceViscosity(*body.geometry, face, mu).si() == mu.si());
        }
    }

    CHECK(meshed == 8);
    // The sweep covered a real RANGE and not one value, which is what makes
    // "over the full range of w_f" a measured statement.
    CHECK(internalSeen == 6 + 946 + 172 + 1472 + 18 + 18 + 18 + 528);
    CHECK(boundarySeen == 12 + 140 + 160 + 288 + 12 + 12 + 12 + 12);
    WARN(std::format("interpolation weight over {} internal faces: {:.6f} to {:.6f}", internalSeen,
                     smallest, largest));
    CHECK(largest > smallest);
    CHECK(smallest > 0.0);
    CHECK(largest < 1.0);
}

// ---------------------------------------------------------------------------
// The finiteness of the two caller-supplied arrays
// ---------------------------------------------------------------------------

TEST_CASE("CfdDiffusion_ANonFiniteCurrentVelocityIsRefusedByName", "[cfd][diff]") {
    // A REVIEW FOUND BOTH CALLER-SUPPLIED ARRAYS VALIDATED FOR LENGTH AND NEVER
    // FOR VALUE, so one NaN flowed through the cross terms into the source
    // while the matrix stayed provably finite -- which made
    // `CfdSparseMatrix`'s "possession is the evidence" guarantee look like it
    // covered the whole system when it covered only the coefficients. There is
    // no finiteness check on `source` anywhere, by anything, so the refusal has
    // to be at the input.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary =
        hand.uniform(CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), SlipWall{someFace()}});
    const std::size_t length = 3 * hand.topology.cells();

    for (const double bad : {std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity(),
                             -std::numeric_limits<double>::infinity()}) {
        for (std::size_t slot = 0; slot < length; ++slot) {
            std::vector<double> current(length, 0.0);
            current[slot] = bad;
            CfdDiffusionInput input = plainInput(1.002_mPa_s, VectorComponent::X);
            input.currentVelocity = current;
            INFO(slot);
            const std::optional<CfdDiffusionProblem> predicted =
                cfd::cfdDiffusionProblem(hand.topology, hand.geometry, boundary, input);
            REQUIRE(predicted.has_value());
            CHECK(*predicted == CfdDiffusionProblem::CurrentVelocityNotFinite);
            CHECK_FALSE(
                cfd::assembleCfdDiffusion(hand.topology, hand.geometry, boundary, input).has_value());
        }
    }

    // And a finite EXTREME is accepted, so the gate is not a magnitude bound in
    // disguise -- the hole a review found in P19-IC-001's finiteness cases.
    std::vector<double> extreme(length, 0.0);
    extreme[0] = std::numeric_limits<double>::max();
    extreme[1] = std::numeric_limits<double>::denorm_min();
    CfdDiffusionInput good = plainInput(1.002_mPa_s, VectorComponent::X);
    good.currentVelocity = extreme;
    CHECK_FALSE(cfd::cfdDiffusionProblem(hand.topology, hand.geometry, boundary, good).has_value());
}

TEST_CASE("CfdDiffusion_ANonFiniteFaceGradientIsRefusedByName", "[cfd][diff]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.uniform(
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});

    // Each of the three components in turn, so a check that looked at only one
    // of them fails -- the conjunction P19-IC-001's own review had to close.
    for (std::size_t component = 0; component < 3; ++component) {
        for (const double bad : {std::numeric_limits<double>::quiet_NaN(),
                                 std::numeric_limits<double>::infinity()}) {
            std::vector<VelocityGradient3> gradients(hand.topology.faces());
            if (component == 0) {
                gradients[1].x = VelocityGradient::fromSi(bad);
            } else if (component == 1) {
                gradients[1].y = VelocityGradient::fromSi(bad);
            } else {
                gradients[1].z = VelocityGradient::fromSi(bad);
            }
            CfdDiffusionInput input = plainInput(1.002_mPa_s, VectorComponent::X);
            input.faceGradient = gradients;
            INFO(component);
            const std::optional<CfdDiffusionProblem> predicted =
                cfd::cfdDiffusionProblem(hand.topology, hand.geometry, boundary, input);
            REQUIRE(predicted.has_value());
            CHECK(*predicted == CfdDiffusionProblem::CorrectionNotFinite);
        }
    }

    // A finite extreme is accepted.
    std::vector<VelocityGradient3> extreme(hand.topology.faces());
    extreme[0].x = VelocityGradient::fromSi(std::numeric_limits<double>::max());
    CfdDiffusionInput good = plainInput(1.002_mPa_s, VectorComponent::X);
    good.faceGradient = extreme;
    CHECK_FALSE(cfd::cfdDiffusionProblem(hand.topology, hand.geometry, boundary, good).has_value());
}

TEST_CASE("CfdDiffusion_ALengthFaultIsReportedBeforeAValueFault", "[cfd][diff]") {
    // Both arrays wrong in both ways at once: the LENGTH is the earlier phase,
    // so a wrong length is reported as a length and not as whatever happens to
    // sit in it.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.uniform(
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});

    std::vector<double> current(3 * hand.topology.cells() + 1,
                               std::numeric_limits<double>::quiet_NaN());
    CfdDiffusionInput velocityInput = plainInput(1.002_mPa_s, VectorComponent::X);
    velocityInput.currentVelocity = current;
    const std::optional<CfdDiffusionProblem> velocityFault =
        cfd::cfdDiffusionProblem(hand.topology, hand.geometry, boundary, velocityInput);
    REQUIRE(velocityFault.has_value());
    CHECK(*velocityFault == CfdDiffusionProblem::CurrentVelocityLengthWrong);

    std::vector<VelocityGradient3> gradients(hand.topology.faces() + 1);
    gradients[0].x = VelocityGradient::fromSi(std::numeric_limits<double>::quiet_NaN());
    CfdDiffusionInput gradientInput = plainInput(1.002_mPa_s, VectorComponent::X);
    gradientInput.faceGradient = gradients;
    const std::optional<CfdDiffusionProblem> gradientFault =
        cfd::cfdDiffusionProblem(hand.topology, hand.geometry, boundary, gradientInput);
    REQUIRE(gradientFault.has_value());
    CHECK(*gradientFault == CfdDiffusionProblem::CorrectionLengthWrong);
}

// ---------------------------------------------------------------------------
// The scale laws the ADR demands, one per factor
// ---------------------------------------------------------------------------

TEST_CASE("CfdDiffusion_TheGeometricScaleLawSeparatesAreaFromDistance", "[cfd][diff]") {
    // THE TWO LAWS THAT WERE MISSING. ADR-069's Verification section demands a
    // scale law per factor of `mu A / (d . n)` -- three of them -- and only the
    // viscosity one existed. A review pointed out that the type protection the
    // viscosity test defers to evaporates inside the assembly, where both the
    // area and the distance are bare doubles after `.si()`, so
    // `mu * orthogonalDistance / area` compiles.
    //
    // Scaling the whole fixture by `k` scales the area by `k^2` and the distance
    // by `k`, so a correct coefficient scales by `k`. An operator dividing by a
    // squared distance would scale by 1, and one multiplying by the distance by
    // `k^3`. The exponent is what separates the two factors.
    const DynamicViscosity mu = 1.002_mPa_s;
    const auto coefficientAt = [&](double k) {
        std::vector<Point3D> scaled;
        for (const Point3D& p : analyticPoints()) {
            scaled.push_back(Point3D{Length::fromSi(p.x.si() * k), Length::fromSi(p.y.si() * k),
                                     Length::fromSi(p.z.si() * k)});
        }
        const Hand hand = Hand::make(scaled, kAnalyticPair);
        FaceIndex internal = FaceIndex::fromValue(0);
        for (std::size_t f = 0; f < hand.topology.faces(); ++f) {
            if (hand.topology.isInternal(FaceIndex::fromValue(f))) {
                internal = FaceIndex::fromValue(f);
            }
        }
        const PreparedCfdBoundary boundary = hand.uniform(
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});
        const Result<CfdDiffusionSystem> system = cfd::assembleCfdDiffusion(
            hand.topology, hand.geometry, boundary, plainInput(mu, VectorComponent::X));
        INFO((system.has_value() ? std::string{} : system.error().message));
        REQUIRE(system.has_value());
        const std::size_t p = hand.topology.owner(internal).value();
        const std::optional<CellIndex> other = hand.topology.neighbour(internal);
        REQUIRE(other.has_value());
        return -system->matrix.coeffSi(p, other->value());
    };

    const double base = coefficientAt(1.0);
    REQUIRE(base > 0.0);
    for (const double k : {2.0, 3.0, 0.5}) {
        INFO(k);
        // EXPONENT ONE, exactly: area k^2 over distance k.
        CHECK_THAT(coefficientAt(k) / base, WithinRel(k, 1e-12));
        // And not the exponents a confusion of the two factors would give.
        CHECK(std::abs(coefficientAt(k) / base - 1.0) > 0.1);
        CHECK(std::abs(coefficientAt(k) / base - k * k * k) > 0.1);
    }
}

// ---------------------------------------------------------------------------
// The decomposition on a boundary face, and the row sum
// ---------------------------------------------------------------------------

TEST_CASE("CfdDiffusion_TheDecompositionIsAvailableOnABoundaryFace", "[cfd][diff]") {
    // Every call site in this file was inside an `isInternal` filter, so the
    // boundary branch of `cfdFaceDecomposition` -- which the assembly depends on
    // for every boundary coefficient -- was never read directly. A review found
    // it. On a boundary face `d` is the cell-to-face vector, whose normal
    // projection ADR-065 states is EXACTLY `h_P / 4`.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    REQUIRE(hand.topology.internalFaces() == 0);
    std::size_t seen = 0;
    std::size_t orthogonalityChecked = 0;
    std::size_t numericallyZero = 0;
    for (std::size_t f = 0; f < hand.topology.faces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        REQUIRE_FALSE(hand.topology.isInternal(face));
        const std::optional<CfdFaceDecomposition> split = cfd::cfdFaceDecomposition(hand.geometry, face);
        REQUIRE(split.has_value());
        ++seen;
        // The arms sum to the area vector, on a boundary face as on an internal
        // one: that is the defining property of the split.
        const AreaVector3D s = hand.geometry.faceAreaVector(face);
        const double area = std::hypot(s.x.si(), s.y.si(), s.z.si());
        for (const auto& [arm, exact] :
             std::array<std::pair<double, double>, 3>{{{split->implicitArm.x.si() + split->deferredArm.x.si(), s.x.si()},
                                                       {split->implicitArm.y.si() + split->deferredArm.y.si(), s.y.si()},
                                                       {split->implicitArm.z.si() + split->deferredArm.z.si(), s.z.si()}}}) {
            CHECK(std::abs(arm - exact) <= 64.0 * std::numeric_limits<double>::epsilon() * area);
        }
        // And the deferred arm is orthogonal to the area vector, which is the
        // over-relaxed split's identity and holds on a boundary face too.
        const double dot = split->deferredArm.x.si() * s.x.si() + split->deferredArm.y.si() * s.y.si() +
                           split->deferredArm.z.si() * s.z.si();
        const double deferredNorm = std::hypot(split->deferredArm.x.si(), split->deferredArm.y.si(),
                                               split->deferredArm.z.si());
        // GUARDED ON THE SCALE, NOT ON ZERO. On the tet's slanted facet `d` is
        // parallel to `n` by symmetry, so `T_f` is numerically zero -- and a
        // vector that is pure rounding noise has an ARBITRARY direction, which
        // measured as cos = 1.000000000000000222 against the area vector. A
        // `> 0.0` guard admits that noise and the assertion became meaningless.
        // The identity is exact in exact arithmetic; it is only observable
        // where the arm is a real vector.
        if (deferredNorm > 1e-12 * area) {
            CHECK_THAT(dot / (deferredNorm * area), WithinAbs(0.0, 1e-14));
            ++orthogonalityChecked;
        } else {
            // And where it is not, it must be genuinely tiny rather than
            // merely short -- the complement, so this branch is not an escape.
            CHECK(deferredNorm <= 1e-12 * area);
            ++numericallyZero;
        }
    }
    CHECK(seen == 4);
    // THREE FACES CARRY THE ORTHOGONALITY CLAIM AND ONE IS THE SYMMETRY AXIS,
    // asserted so the split between the two branches cannot drift silently:
    // the slanted facet x + y + z = 10 mm is the one where the cell centroid
    // lies on the face normal, so its deferred arm vanishes.
    CHECK(orthogonalityChecked == 3);
    CHECK(numericallyZero == 1);
}

TEST_CASE("CfdDiffusion_APureNeumannRowSumsToZeroBitExactly", "[cfd][diff]") {
    // ADR-069's Verification section asked for "bit exactness on the sum per
    // row" and the suite measured `multiply(ones)` against a relative bound
    // instead, never calling `rowSumSi` -- which `CfdSparse.hpp` says exists
    // precisely because this is the operator's central null property. A review
    // found both.
    //
    // THE BIT-EXACT CLAIM HOLDS ONLY WHERE EACH ROW IS ONE `+Gamma` AND ONE
    // `-Gamma`, and that is a correction to the ADR rather than to the code: in
    // general the diagonal accumulates in face order while the row sum
    // subtracts in column order, and those need not cancel to the last bit. The
    // two-cell fixture has exactly one internal face per row, so here it does.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    REQUIRE(hand.topology.internalFaces() == 1);
    const PreparedCfdBoundary boundary = hand.uniform(CfdBoundaryCondition{
        CfdBoundaryConditionId::fromValue(1), PressureOutlet{someFace(), 0_Pa}});
    const Result<CfdDiffusionSystem> system = cfd::assembleCfdDiffusion(
        hand.topology, hand.geometry, boundary, plainInput(1.002_mPa_s, VectorComponent::X));
    REQUIRE(system.has_value());
    REQUIRE(system->summary.constantModeIsInTheNullSpace);

    for (std::size_t row = 0; row < system->matrix.rows(); ++row) {
        INFO(row);
        CHECK(system->matrix.rowSumSi(row) == 0.0);
    }

    // And on a reference body, where rows have up to four internal faces, the
    // same property holds only to rounding -- measured rather than assumed, so
    // the distinction above is not a story.
    const Reference block(bettercad::reference::MeshReferenceModelKind::Block);
    std::vector<ElementId> ascending;
    for (std::size_t b = 0; b < block.topology->boundaryFaces(); ++b) {
        ascending.push_back(block.topology->triangleOf(BoundaryFaceIndex::fromValue(b)));
    }
    std::ranges::sort(ascending, [](ElementId a, ElementId b) { return a.value() < b.value(); });
    const CfdResolvedTarget target{
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), PressureOutlet{someFace(), 0_Pa}},
        ascending};
    const std::array<CfdResolvedTarget, 1> targets{target};
    Result<PreparedCfdBoundary> open =
        cfd::bindCfdBoundary(*block.topology, *block.geometry, block.input->mesh().mesh(), targets, 0,
                             std::nullopt, std::nullopt, std::nullopt);
    INFO((open.has_value() ? std::string{} : open.error().message));
    REQUIRE(open.has_value());
    const Result<CfdDiffusionSystem> blockSystem = cfd::assembleCfdDiffusion(
        *block.topology, *block.geometry, *open, plainInput(1.002_mPa_s, VectorComponent::X));
    REQUIRE(blockSystem.has_value());
    REQUIRE(blockSystem->summary.constantModeIsInTheNullSpace);
    double worst = 0.0;
    for (std::size_t row = 0; row < blockSystem->matrix.rows(); ++row) {
        worst = std::max(worst, std::abs(blockSystem->matrix.rowSumSi(row)));
    }
    WARN(std::format("worst row sum on the Block, relative to the largest entry: {:.3e}",
                     worst / blockSystem->matrix.largestMagnitude()));
    CHECK(worst <= 1e-14 * blockSystem->matrix.largestMagnitude());
}

TEST_CASE("CfdDiffusion_TheInterpolationWeightMatchesAnIndependentCentroidReading", "[cfd][diff]") {
    // THE WEIGHT'S VALUE, WHICH THE IDENTITY TEST CANNOT PIN. For a constant
    // viscosity `w mu + (1 - w) mu` is `mu` for ANY weight, so asserting
    // `mu_f == mu` says nothing about `w` -- a weight of `1 - w`, or of 0.5, or
    // of anything in range, passes it. The range and bounds test narrows it but
    // does not fix it either.
    //
    // So the value is read a SECOND WAY: from the owner and neighbour cell
    // centroids and the face centroid, through the topology, which is not how
    // `cfdFaceInterpolationWeight` computes it -- it uses `ownerToNeighbour` and
    // `cellToFace` and never asks which cell is which. Two routes to one number.
    const Hand straight = Hand::make(orthogonalPoints(), kTwoCells);
    const Hand skew = Hand::make(analyticPoints(), kAnalyticPair);
    std::size_t internalChecked = 0;

    for (const Hand* hand : {&straight, &skew}) {
        for (std::size_t f = 0; f < hand->topology.faces(); ++f) {
            const FaceIndex face = FaceIndex::fromValue(f);
            const std::optional<double> weight =
                cfd::cfdFaceInterpolationWeight(hand->geometry, face);
            REQUIRE(weight.has_value());
            if (!hand->topology.isInternal(face)) {
                CHECK(*weight == 1.0);
                continue;
            }
            const std::optional<CellIndex> neighbour = hand->topology.neighbour(face);
            REQUIRE(neighbour.has_value());
            const std::optional<Point3D> owner =
                hand->geometry.cellCentroid(hand->topology.owner(face));
            const std::optional<Point3D> other = hand->geometry.cellCentroid(*neighbour);
            const std::optional<Point3D> centre = hand->geometry.faceCentroid(face);
            REQUIRE(owner.has_value());
            REQUIRE(other.has_value());
            REQUIRE(centre.has_value());

            // The PROJECTED distances, built from the explicit centroids and
            // the face normal -- where the implementation reads cellToFace and
            // orthogonalDistance and never asks which cell is which.
            const std::optional<Direction3D> normal = hand->geometry.faceUnitNormal(face);
            REQUIRE(normal.has_value());
            const auto project = [&](const Point3D& from, const Point3D& to) {
                return (to.x - from.x).si() * normal->x() + (to.y - from.y).si() * normal->y() +
                       (to.z - from.z).si() * normal->z();
            };
            const double b = project(*centre, *other);
            const double total = project(*owner, *other);
            REQUIRE(total > 0.0);
            REQUIRE(b > 0.0);
            CHECK_THAT(*weight, WithinRel(b / total, 1e-12));
            ++internalChecked;
        }
    }
    // Not vacuous: one internal face in each fixture.
    CHECK(internalChecked == 2);

    // AND THE WEIGHT IS NOT ONE HALF on the skew fixture, so a hard-coded
    // midpoint -- the most likely wrong implementation -- would fail.
    FaceIndex internal = FaceIndex::fromValue(0);
    for (std::size_t f = 0; f < skew.topology.faces(); ++f) {
        if (skew.topology.isInternal(FaceIndex::fromValue(f))) {
            internal = FaceIndex::fromValue(f);
        }
    }
    const std::optional<double> skewWeight = cfd::cfdFaceInterpolationWeight(skew.geometry, internal);
    REQUIRE(skewWeight.has_value());
    CHECK(std::abs(*skewWeight - 0.5) > 0.05);
    WARN(std::format("interpolation weight on the analytic face: {:.9f}", *skewWeight));
}
