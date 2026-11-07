/// \file
/// P19-CONV-001 — convective flux operator and the mass-flux producer, ADR-072.
///
/// WHAT IS CHECKED WHERE, AND WHY THERE.
///
/// ```text
/// the two-cell pair     the only fixture with an internal face whose two
///                       contributions can be read separately, so it is where
///                       the exact-cancellation claim is pinned
/// the one-cell tet      four boundary facets and no internal face: the only
///                       fixture where the boundary treatment is the WHOLE
///                       answer
/// the analytic pair     the owner weight is exactly 2/3, which is the one
///                       quantity asserted on it. Where a coefficient is
///                       pinned against closed-form arithmetic. (Its |d| = 5/4,
///                       d . n = 3/4 and cos(theta) = 3/5 are exact and are NOT
///                       asserted here, which an earlier draft of this comment
///                       implied -- the same sentence P19-DIV-001's review
///                       found overstated in its own suite)
/// the tet chain         N cells in a line, each sharing one face with the
///                       next: the 1D advection fixture, and the only one
///                       where the divergence is BIT-zero at an interior cell
///                       so the transpose identity can be asserted exactly
/// the reference bodies  conservation, the row-sum cross-check against
///                       P19-DIV-001, and the interpolation weight where it
///                       genuinely varies, on meshes nobody designed to
///                       flatter the operator
/// ```
///
/// THAT TABLE USED TO SAY "invariance and determinism" OF THE REFERENCE
/// BODIES, AND BOTH WORDS WERE WRONG. There is no rigid-motion invariance test
/// in this file at all -- the word was borrowed from `CfdDivergenceTests.cpp`,
/// which has one -- and the determinism test runs on the hand-built chain, not
/// on any reference body. An independent review checked the sentence against
/// the tests that exist. It is corrected rather than deleted, because the
/// coverage argument in an evidence file cites this header.
///
/// THE FIXTURES ARE A FOURTH COPY, AND THAT IS STATED RATHER THAN HIDDEN.
/// `buildMesh`, `struct Hand`, `orthogonalPoints` and their neighbours also
/// exist in `CfdDiffusionTests.cpp`, `CfdGradientTests.cpp` and
/// `CfdDivergenceTests.cpp`. Extracting them would edit three qualified suites
/// to serve a fourth, which is the refactor-with-a-feature the working order
/// forbids; ADR-072 Decision 9 makes the same call about the interpolation's
/// application. What must not be duplicated is an ORACLE, and none is.
///
/// THE WEIGHT ORACLE IS RE-DERIVED, NOT TRANSCRIBED, and this is the one claim
/// P19-GRAD-001 (r5) and P19-DIV-001 both got wrong in exactly this place.
/// Production computes `(orthogonalDistance - cellToFace . n) / orthogonalDistance`
/// from `cellToFace`, `orthogonalDistance` and `faceUnitNormal`. The oracle
/// below computes `((C_N - C_f) . n) / ((C_N - C_P) . n)` from `cellCentroid`,
/// `faceCentroid` and a normal taken by NORMALISING `faceAreaVector` -- three
/// different accessors, none of which production reads on this path. A
/// transcription would have agreed with a transposed production formula; this
/// cannot.
///
/// WHAT THIS FILE DELIBERATELY DOES NOT ASSERT.
///
/// There is no claim that the row-sum identity is bit-exact. ADR-072 Decision 5
/// measured it over 20,000 mixed-policy meshes before any of this existed:
/// bit-exact on 31.25% to 36.44%, worst relative `1.94e-14`, because three
/// accumulations in three different orders meet in it. A test asserting
/// bit-equality would fail on a correct operator, which is P19-DIV-001's
/// recorded mistake.
///
/// There is no claim that the matrix is symmetric, because a convection matrix
/// is not: that is ADR-072 Decision 3, and the suite asserts the ASYMMETRY
/// instead.
///
/// FOUR REFUSALS ARE NOT TESTED THROUGH THE PUBLIC API, because no
/// constructible input reaches them, and they are recorded as UNREACHED rather
/// than claimed covered -- the treatment P19-BC-001 gave `DegenerateFacet`:
/// `PolicyIsAnOutcome` (ADR-067's table has no `Computed` in its `U` column),
/// `FaceWeightUnavailable` (`buildCfdGeometry` refuses a face with a
/// non-positive orthogonal distance), `TopologyNamesAnUnknownCell`
/// (`buildCfdTopology` holds) and `AssemblyIsInconsistent` (the symbolic pass
/// reserves every pair, and `fromCsr` cannot refuse what this assembly builds).
///
/// AN EARLIER DRAFT OF THAT LIST NAMED ONLY TWO, AND ALSO OMITTED
/// `ResultIsNotFinite` -- which is REACHABLE and was untested, while being the
/// enumerator whose observability is ADR-072 Decision 10's whole argument for
/// the tagged return type. An independent review found both. It is tested now,
/// on three paths.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "reference/MeshTestSupport.hpp"

#include <MeshReferenceModels.hpp>

#include <bettercad/cfd/CfdBoundary.hpp>
#include <bettercad/cfd/CfdConvection.hpp>
#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdDiffusion.hpp>
#include <bettercad/cfd/CfdDivergence.hpp>
#include <bettercad/cfd/CfdGeometry.hpp>
#include <bettercad/cfd/CfdTopology.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/materials/ThermalProperties.hpp>
#include <bettercad/core/math/Direction.hpp>
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
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;

using cfd::BoundaryFaceIndex;
using cfd::CellIndex;
using cfd::CfdBoundaryCondition;
using cfd::CfdBoundaryConditionKind;
using cfd::CfdBoundaryFace;
using cfd::CfdBoundaryPolicy;
using cfd::CfdBoundaryTreatment;
using cfd::CfdConvectionFailure;
using cfd::CfdConvectionInput;
using cfd::CfdConvectionProblem;
using cfd::CfdConvectionScheme;
using cfd::CfdConvectionSystem;
using cfd::CfdGeometry;
using cfd::CfdMassFluxDivergenceField;
using cfd::CfdMassFluxDivergenceInput;
using cfd::CfdMassFluxInput;
using cfd::CfdResolvedTarget;
using cfd::CfdTopology;
using cfd::CfdVelocityDivergenceField;
using cfd::CfdVelocityDivergenceInput;
using cfd::FaceIndex;
using cfd::MassFlowRate;
using cfd::MovingWall;
using cfd::NoSlipWall;
using cfd::PreparedCfdBoundary;
using cfd::PressureInlet;
using cfd::PressureOutlet;
using cfd::SlipWall;
using cfd::VectorComponent;
using cfd::Velocity3;
using cfd::VelocityInlet;
using cfd::VolumetricFlowRate;
using cfd::assembleCfdConvection;
using cfd::cfdConvectionFailure;
using cfd::cfdConvectionProblem;
using cfd::cfdMassFlux;
using cfd::cfdMassFluxProblem;
using cfd::divergeCfdMassFlux;
using cfd::divergeCfdVelocity;
using cfd::kCfdConvectionProblems;
using cfd::kCfdConvectionRowSumTolerance;
using cfd::kCfdConvectionSchemes;
using meshing::ElementId;
using meshing::NodeId;

namespace {

using Vec = std::array<double, 3>;

const meshing::RegionId kRegion = meshing::RegionId::fromValue(1);

/// NOT A ROUND NUMBER AND NOT A DYADIC ONE, and P19-GRAD-001 is why: `0.0`
/// cannot distinguish an unstable interpolation from a stable one and `17.25`
/// cannot either, because `69/4` makes every product exact.
constexpr double kDiscriminatingConstant = 12345.6789;

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

[[nodiscard]] std::vector<Point3D> orthogonalPoints() {
    return {Point3D{0_mm, 0_mm, 0_mm},  Point3D{10_mm, 0_mm, 0_mm},  Point3D{0_mm, 10_mm, 0_mm},
            Point3D{0_mm, 0_mm, 10_mm}, Point3D{10_mm, 10_mm, 10_mm}};
}
const std::vector<std::array<std::size_t, 4>> kTwoCells{{0, 1, 2, 3}, {1, 2, 3, 4}};

[[nodiscard]] std::vector<Point3D> oneCellPoints() {
    return {Point3D{0_mm, 0_mm, 0_mm}, Point3D{10_mm, 0_mm, 0_mm}, Point3D{0_mm, 10_mm, 0_mm},
            Point3D{0_mm, 0_mm, 10_mm}};
}
const std::vector<std::array<std::size_t, 4>> kOneCell{{0, 1, 2, 3}};

/// `|d| = 5/4`, `d . n = 3/4`, `cos(theta) = 3/5`, and the OWNER WEIGHT IS
/// EXACTLY 2/3 -- all exact, and not orthogonal, so a mutation of the weight is
/// visible.
[[nodiscard]] std::vector<Point3D> analyticPoints() {
    const auto p = [](double x, double y, double z) {
        return Point3D{Length::fromSi(x), Length::fromSi(y), Length::fromSi(z)};
    };
    return {p(0, 0, 0), p(1, 0, 0), p(0, 1, 0), p(0, 0, -1), p(0, 4, 2)};
}
const std::vector<std::array<std::size_t, 4>> kAnalyticPair{{0, 2, 1, 3}, {0, 1, 2, 4}};

/// A CHAIN OF @p cells TETRAHEDRA, each sharing exactly one face with the next.
///
/// THE 1D ADVECTION FIXTURE, and the reason it is built rather than borrowed:
/// `kTwoCells` is a chain of two, and the properties worth asserting -- a
/// bidiagonal matrix, a bit-zero interior divergence, the exact transpose under
/// reversal -- need an INTERIOR cell, which needs at least three.
///
/// Tets `{i, i+1, i+2, i+3}` share the face `{i+1, i+2, i+3}` with their
/// successor, which is the same construction `kTwoCells` uses extended. The
/// points walk a triangular helix so that no four consecutive ones are coplanar.
[[nodiscard]] std::vector<Point3D> chainPoints(std::size_t cells) {
    std::vector<Point3D> out;
    const double radius = 0.01;
    const double rise = 0.01;
    for (std::size_t k = 0; k < cells + 3; ++k) {
        const double angle = 2.0 * std::numbers::pi * static_cast<double>(k) / 3.0;
        out.push_back(Point3D{Length::fromSi(radius * std::cos(angle)),
                              Length::fromSi(radius * std::sin(angle)),
                              Length::fromSi(rise * static_cast<double>(k))});
    }
    return out;
}

[[nodiscard]] std::vector<std::array<std::size_t, 4>> chainTets(std::size_t cells) {
    std::vector<std::array<std::size_t, 4>> out;
    for (std::size_t i = 0; i < cells; ++i) {
        out.push_back({i, i + 1, i + 2, i + 3});
    }
    return out;
}

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

// ---------------------------------------------------------------------------
// Small vector arithmetic, written out here
// ---------------------------------------------------------------------------

[[nodiscard]] Vec siOf(const Point3D& p) { return {p.x.si(), p.y.si(), p.z.si()}; }
[[nodiscard]] Vec siOf(const AreaVector3D& v) { return {v.x.si(), v.y.si(), v.z.si()}; }

[[nodiscard]] double dotOf(const Vec& a, const Vec& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

[[nodiscard]] Vec subOf(const Vec& a, const Vec& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

[[nodiscard]] double normOf(const Vec& a) { return std::hypot(a[0], a[1], a[2]); }

// ---------------------------------------------------------------------------
// Hand-built fixtures
// ---------------------------------------------------------------------------

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

    [[nodiscard]] std::vector<ElementId> facets() const {
        std::vector<ElementId> out;
        for (std::size_t b = 0; b < topology.boundaryFaces(); ++b) {
            out.push_back(topology.triangleOf(BoundaryFaceIndex::fromValue(b)));
        }
        return out;
    }

    [[nodiscard]] Point3D insidePoint() const {
        const std::optional<Point3D> centroid = geometry.cellCentroid(CellIndex::fromValue(0));
        REQUIRE(centroid.has_value());
        return *centroid;
    }

    [[nodiscard]] PreparedCfdBoundary uniform(const CfdBoundaryCondition& condition) const {
        std::vector<ElementId> ascending = facets();
        std::ranges::sort(ascending, [](ElementId a, ElementId b) { return a.value() < b.value(); });
        REQUIRE(std::ranges::adjacent_find(ascending) == ascending.end());
        const CfdResolvedTarget target{condition, ascending};
        const std::array<CfdResolvedTarget, 1> targets{target};
        const bool fixesPressure =
            cfd::treatmentOf(condition.kind()).pressure == CfdBoundaryPolicy::FixedValue;
        const std::optional<cfd::CfdReferencePressure> datum =
            fixesPressure ? std::nullopt
                          : std::optional<cfd::CfdReferencePressure>{
                                cfd::CfdReferencePressure{.value = 0_Pa, .location = insidePoint()}};
        Result<PreparedCfdBoundary> bound =
            cfd::bindCfdBoundary(topology, geometry, mesh, targets, 0, datum, std::nullopt, std::nullopt);
        INFO((bound.has_value() ? std::string{} : bound.error().message));
        REQUIRE(bound.has_value());
        return std::move(*bound);
    }

    /// Every facet a no-slip wall: `U` is FixedValue zero and `phi` is
    /// FixedValue zero.
    [[nodiscard]] PreparedCfdBoundary walls() const {
        return uniform(
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});
    }

    /// Every facet a pressure outlet: `U` is ZeroGradient and `phi` is
    /// COMPUTED, which is the policy neither path may refuse.
    [[nodiscard]] PreparedCfdBoundary outlets() const {
        return uniform(CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1),
                                            PressureOutlet{someFace(), 0_Pa}});
    }

    /// Every facet a slip wall: `U` is Symmetry everywhere.
    [[nodiscard]] PreparedCfdBoundary slips() const {
        return uniform(
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), SlipWall{someFace()}});
    }

    /// Every facet a velocity inlet at @p speed: `U` is FixedValue `-v n_hat`,
    /// so every facet is INFLOW and every one prescribes a value.
    [[nodiscard]] PreparedCfdBoundary inlets(Velocity speed) const {
        return uniform(CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1),
                                            VelocityInlet{someFace(), speed}});
    }
};

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

// ---------------------------------------------------------------------------
// Oracles, re-derived
// ---------------------------------------------------------------------------

/// The owner's interpolation weight, RE-DERIVED from the three centroids and a
/// normal taken by normalising the AREA VECTOR.
///
/// `w = ((C_N - C_f) . n) / ((C_N - C_P) . n)`
///
/// Production reads `cellToFace`, `orthogonalDistance` and `faceUnitNormal` and
/// forms `(total - owner) / total`. This reads `cellCentroid`, `faceCentroid`
/// and `faceAreaVector`. No accessor is shared, so a transposition in
/// production cannot be mirrored here -- which is exactly what P19-GRAD-001's
/// r5 and P19-DIV-001's own review found had happened when the oracle was a
/// transcription.
[[nodiscard]] double weightOracle(const CfdTopology& topology, const CfdGeometry& geometry,
                                  FaceIndex face) {
    const std::optional<CellIndex> neighbour = topology.neighbour(face);
    REQUIRE(neighbour.has_value());
    const std::optional<Point3D> ownerC = geometry.cellCentroid(topology.owner(face));
    const std::optional<Point3D> otherC = geometry.cellCentroid(*neighbour);
    const std::optional<Point3D> faceC = geometry.faceCentroid(face);
    REQUIRE(ownerC.has_value());
    REQUIRE(otherC.has_value());
    REQUIRE(faceC.has_value());
    const Vec area = siOf(geometry.faceAreaVector(face));
    const double magnitude = normOf(area);
    REQUIRE(magnitude > 0.0);
    const Vec n{area[0] / magnitude, area[1] / magnitude, area[2] / magnitude};
    const double numerator = dotOf(subOf(siOf(*otherC), siOf(*faceC)), n);
    const double denominator = dotOf(subOf(siOf(*otherC), siOf(*ownerC)), n);
    REQUIRE(denominator != 0.0);
    return numerator / denominator;
}

/// `sum_f ±phi_f` for one cell, accumulated in FACE order, written out here.
[[nodiscard]] double netOutflowOracle(const CfdTopology& topology, std::span<const double> phi,
                                      std::size_t cell) {
    double total = 0.0;
    for (std::size_t f = 0; f < topology.faces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        if (topology.owner(face).value() == cell) {
            total += phi[f];
        }
        const std::optional<CellIndex> neighbour = topology.neighbour(face);
        if (neighbour.has_value() && neighbour->value() == cell) {
            total -= phi[f];
        }
    }
    return total;
}

/// The PRESCRIBED inflow at one cell: `sum` over its boundary faces whose flux
/// is negative AND whose velocity policy is `FixedValue`.
///
/// RE-DERIVED FROM `phi` AND THE BOUNDARY, never read from the summary. ADR-072
/// Decision 5 reports only the mesh total for exactly this reason: a per-cell
/// array on the summary would invite a test to read the operator's own number
/// and call it an oracle.
[[nodiscard]] double prescribedInflowOracle(const CfdTopology& topology,
                                            const PreparedCfdBoundary& boundary,
                                            std::span<const double> phi, std::size_t cell) {
    double total = 0.0;
    for (std::size_t f = topology.internalFaces(); f < topology.faces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        if (topology.owner(face).value() != cell || phi[f] >= 0.0) {
            continue;
        }
        const std::optional<BoundaryFaceIndex> boundaryFace = topology.boundaryFaceOf(face);
        REQUIRE(boundaryFace.has_value());
        const std::optional<CfdBoundaryTreatment> treatment = boundary.treatment(*boundaryFace);
        REQUIRE(treatment.has_value());
        if (treatment->velocity == CfdBoundaryPolicy::FixedValue) {
            total += phi[f];
        }
    }
    return total;
}

/// A uniform cell velocity, component-blocked per ADR-066.
///
/// USE THIS ONLY WHERE THE INTERPOLATION IS NOT UNDER TEST. With `U_P == U_N`
/// the face value `U_N + w (U_P - U_N)` is INDEPENDENT OF `w` for every weight,
/// so a uniform field cannot see the interpolation at all. An independent
/// review found that this was the suite's only velocity builder, which left
/// `cfdFaceInterpolationWeight` completely unpinned on the producer path and
/// made ADR-072 Decision 9's designated cross-check a tautology -- the defect
/// P19-GRAD-001 recorded as r5 and P19-DIV-001 found again, recurring here. The
/// two builders below exist because of it.
[[nodiscard]] std::vector<double> uniformVelocity(std::size_t cells, const Vec& value) {
    std::vector<double> out(3 * cells, 0.0);
    for (std::size_t c = 0; c < cells; ++c) {
        out[c] = value[0];
        out[cells + c] = value[1];
        out[2 * cells + c] = value[2];
    }
    return out;
}

/// An EXPLICIT velocity per cell, component-blocked. The sharpest fixture for
/// the interpolation: two cells with different velocities make the face value a
/// genuine function of the weight, so a transposed owner/neighbour or a
/// constant weight changes the answer.
[[nodiscard]] std::vector<double> perCellVelocity(const std::vector<Vec>& values) {
    const std::size_t cells = values.size();
    std::vector<double> out(3 * cells, 0.0);
    for (std::size_t c = 0; c < cells; ++c) {
        for (std::size_t i = 0; i < 3; ++i) {
            out[i * cells + c] = values[c][i];
        }
    }
    return out;
}

/// A velocity that varies LINEARLY with the cell centroid: `U = A x + b`.
///
/// On a real mesh this makes the interpolation weight load-bearing at every
/// internal face, and the weight genuinely varies from face to face -- which a
/// hand fixture with a 2:1 apex ratio cannot show, because its weight is
/// exactly 2/3 everywhere.
[[nodiscard]] std::vector<double> linearVelocity(const CfdGeometry& geometry,
                                                 const std::array<double, 9>& tensor,
                                                 const Vec& constant) {
    const std::size_t cells = geometry.cells();
    std::vector<double> out(3 * cells, 0.0);
    for (std::size_t c = 0; c < cells; ++c) {
        const std::optional<Point3D> centroid = geometry.cellCentroid(CellIndex::fromValue(c));
        REQUIRE(centroid.has_value());
        const Vec at = siOf(*centroid);
        for (std::size_t i = 0; i < 3; ++i) {
            out[i * cells + c] = tensor[3 * i + 0] * at[0] + tensor[3 * i + 1] * at[1] +
                                 tensor[3 * i + 2] * at[2] + constant[i];
        }
    }
    return out;
}

/// `U_N + w (U_P - U_N)`, the face value, written out here.
///
/// RE-DERIVED RATHER THAN READ. It is the same convex combination production
/// forms, but the point of the tests below is that the WEIGHT comes from
/// `weightOracle` -- which reads none of the accessors production reads -- so a
/// constant or transposed weight inside the operator disagrees with this.
[[nodiscard]] Vec interpolateOracle(const Vec& owner, const Vec& neighbour, double weight) {
    return {neighbour[0] + weight * (owner[0] - neighbour[0]),
            neighbour[1] + weight * (owner[1] - neighbour[1]),
            neighbour[2] + weight * (owner[2] - neighbour[2])};
}

[[nodiscard]] std::vector<double> produceWith(const Hand& hand,
                                             const PreparedCfdBoundary& boundary,
                                             std::span<const double> cellVelocity, double rho) {
    const CfdMassFluxInput input{cellVelocity, Density::fromSi(rho)};
    std::expected<std::vector<double>, CfdConvectionFailure> flux =
        cfdMassFlux(hand.topology, hand.geometry, boundary, input);
    INFO((flux.has_value() ? std::string{} : flux.error().error.message));
    REQUIRE(flux.has_value());
    return *flux;
}

[[nodiscard]] std::vector<double> produce(const Hand& hand, const PreparedCfdBoundary& boundary,
                                          const Vec& velocity, double rho) {
    return produceWith(hand, boundary, uniformVelocity(hand.topology.cells(), velocity), rho);
}

[[nodiscard]] CfdConvectionSystem assemble(const Hand& hand, const PreparedCfdBoundary& boundary,
                                           std::span<const double> phi,
                                           CfdConvectionScheme scheme = CfdConvectionScheme::Upwind,
                                           VectorComponent component = VectorComponent::X) {
    const CfdConvectionInput input{phi, scheme, component};
    std::expected<CfdConvectionSystem, CfdConvectionFailure> system =
        assembleCfdConvection(hand.topology, hand.geometry, boundary, input);
    INFO((system.has_value() ? std::string{} : system.error().error.message));
    REQUIRE(system.has_value());
    return std::move(*system);
}

// ---------------------------------------------------------------------------
// The reference bodies
// ---------------------------------------------------------------------------

void assignWaterTo(Document& document) {
    features::MaterialDefinition water;
    water.designation = "Water";
    water.mechanical.density = materials::MaterialProperty<Density>::known(998_kg_per_m3);
    water.thermal.dynamicViscosity = materials::MaterialProperty<DynamicViscosity>::known(1.002_mPa_s);
    const Result<MaterialId> id = features::createMaterial(document, "CfdWater", water);
    REQUIRE(id.has_value());
    REQUIRE(features::assignMaterial(document, *id).has_value());
}

/// One meshed reference body, carried through P16's own production entry points
/// -- the same scaffold `CfdDivergenceTests.cpp` uses, because a reference body
/// must be reached the way a client reaches it and not by a shortcut.
struct Reference {
    meshref::MeshedReference model;
    Result<cfd::CfdModel> input;
    Result<cfd::CfdMeshContract> contract;
    Result<CfdTopology> topology;
    Result<CfdGeometry> geometry;

    explicit Reference(bettercad::reference::MeshReferenceModelKind kind)
        : Reference(document(kind)) {}

    explicit Reference(Document built)
        : model(std::move(built)),
          input(std::unexpected(Error{ErrorCode::FailedPrecondition, "not yet"})),
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

    [[nodiscard]] std::vector<ElementId> facets() const {
        std::vector<ElementId> ascending;
        for (std::size_t b = 0; b < topology->boundaryFaces(); ++b) {
            ascending.push_back(topology->triangleOf(BoundaryFaceIndex::fromValue(b)));
        }
        std::ranges::sort(ascending, [](ElementId a, ElementId b) { return a.value() < b.value(); });
        return ascending;
    }

    /// Every facet a no-slip wall: a CLOSED box, where `phi` is exactly zero at
    /// every boundary face and the row sum is the pure internal-face divergence.
    [[nodiscard]] PreparedCfdBoundary wallEverywhere() const {
        const CfdResolvedTarget target{
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}},
            facets()};
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

/// One meshable reference body: its kind, and the name to report it by.
struct Meshable {
    bettercad::reference::MeshReferenceModelKind kind{};
    std::string_view name{};
};

/// The reference bodies P16 meshes. The one model carrying `expectMesh == false`
/// is filtered out and ASSERTED to be the one expected, so a reordering of the
/// table cannot silently drop a different body.
[[nodiscard]] std::vector<Meshable> meshableModels() {
    std::vector<Meshable> out;
    for (const auto& info : bettercad::reference::kMeshReferenceModels) {
        if (!info.expectMesh) {
            CHECK(info.kind == bettercad::reference::MeshReferenceModelKind::OpenProfile);
            continue;
        }
        out.push_back(Meshable{info.kind, info.name});
    }
    CHECK(out.size() == bettercad::reference::kMeshReferenceModels.size() - 1);
    CHECK(out.size() == 8);
    return out;
}

} // namespace

// ===========================================================================
// Item 1 and item 5 — the mass flux and its sign convention
// ===========================================================================

TEST_CASE("CfdMassFlux_ProducesOneValuePerFaceInFaceIndexOrder", "[cfd][conv]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();
    const std::vector<double> phi = produce(hand, boundary, {0.0, 0.0, 0.0}, 998.0);
    CHECK(phi.size() == hand.topology.faces());
}

TEST_CASE("CfdMassFlux_AtAVelocityInletIsMinusDensityTimesSpeedTimesArea", "[cfd][conv]") {
    // THE CLOSED FORM. ADR-067 gives a velocity inlet `U = -v n_hat`, so
    // `phi = rho (U . S) = -rho v |S|`, exactly, with no interpolation
    // anywhere: the prescribed velocity is the value AT the face.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const double speed = 2.5;
    const double rho = 998.0;
    const PreparedCfdBoundary boundary = hand.inlets(Velocity::fromSi(speed));
    const std::vector<double> phi = produce(hand, boundary, {0.0, 0.0, 0.0}, rho);

    REQUIRE(hand.topology.internalFaces() == 0);
    for (std::size_t f = 0; f < hand.topology.faces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        const double area = normOf(siOf(hand.geometry.faceAreaVector(face)));
        const double expected = -rho * speed * area;
        INFO("face " << f);
        CHECK_THAT(phi[f], Catch::Matchers::WithinRel(expected, 1.0e-12));
        // AND IT IS NEGATIVE, which is the sign convention: `phi` is signed OUT
        // of the owner, so an inlet -- flow INTO the fluid -- must be negative.
        CHECK(phi[f] < 0.0);
    }
}

TEST_CASE("CfdMassFlux_AtEveryWallIsExactlyZeroAndNotMerelySmall", "[cfd][conv]") {
    // ADR-072 DECISION 2. All three walls read `FixedValue 0` down the `phi`
    // column, and the producer writes that zero literally. A MovingWall is the
    // case that matters: its prescribed velocity is TANGENTIAL, so a computed
    // `rho (w . S)` would be zero in exact arithmetic and NOT bit-zero once
    // every input has been rounded -- a tiny mass flow through a solid wall.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);

    SECTION("no-slip wall") {
        const PreparedCfdBoundary boundary = hand.walls();
        const std::vector<double> phi = produce(hand, boundary, {1.0, 2.0, 3.0}, 998.0);
        for (std::size_t f = 0; f < phi.size(); ++f) {
            INFO("face " << f);
            CHECK(phi[f] == 0.0);
        }
    }

    SECTION("slip wall") {
        const PreparedCfdBoundary boundary = hand.slips();
        const std::vector<double> phi = produce(hand, boundary, {1.0, 2.0, 3.0}, 998.0);
        for (std::size_t f = 0; f < phi.size(); ++f) {
            INFO("face " << f);
            CHECK(phi[f] == 0.0);
        }
    }

    SECTION("moving wall, whose tangential velocity would NOT cancel bit-exactly") {
        // A tangential velocity on each facet. The point is that even with a
        // large wall velocity the flux is bit-zero, which a computed dot
        // product could not promise.
        const std::vector<ElementId> facets = hand.facets();
        std::vector<FacetCondition> assignment;
        std::uint64_t next = 1;
        for (std::size_t b = 0; b < facets.size(); ++b) {
            // A MOVING WALL MUST ACTUALLY MOVE, AND TANGENTIALLY. P19-BC-001
            // refuses a zero velocity -- "a wall at rest is a no-slip wall" --
            // and refuses a velocity with a normal component. So a tangent is
            // built from the facet's own unit normal by crossing it with
            // whichever axis it is least aligned with, which cannot be
            // parallel to it.
            const FaceIndex face = FaceIndex::fromValue(hand.topology.internalFaces() + b);
            const std::optional<Direction3D> normal = hand.geometry.faceUnitNormal(face);
            REQUIRE(normal.has_value());
            const Vec n{normal->x(), normal->y(), normal->z()};
            std::size_t least = 0;
            for (std::size_t i = 1; i < 3; ++i) {
                if (std::abs(n[i]) < std::abs(n[least])) {
                    least = i;
                }
            }
            Vec axis{0.0, 0.0, 0.0};
            axis[least] = 1.0;
            Vec tangent{n[1] * axis[2] - n[2] * axis[1], n[2] * axis[0] - n[0] * axis[2],
                        n[0] * axis[1] - n[1] * axis[0]};
            const double length = normOf(tangent);
            REQUIRE(length > 0.0);
            const double speed = 3.5;
            const Velocity3 velocity{Velocity::fromSi(speed * tangent[0] / length),
                                     Velocity::fromSi(speed * tangent[1] / length),
                                     Velocity::fromSi(speed * tangent[2] / length)};
            assignment.push_back(FacetCondition{
                facets[b], CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(next++),
                                                MovingWall{someFace(next), velocity}}});
        }
        const PreparedCfdBoundary boundary = bindPerFacet(hand, assignment, true);
        const std::vector<double> phi = produce(hand, boundary, {1.0, 2.0, 3.0}, 998.0);
        for (std::size_t f = 0; f < phi.size(); ++f) {
            INFO("face " << f);
            CHECK(phi[f] == 0.0);
        }
    }
}

TEST_CASE("CfdMassFlux_AtAPressureOutletIsComputedFromTheOwnersVelocity", "[cfd][conv]") {
    // `phi` IS `Computed` HERE AND THIS IS WHAT COMPUTES IT. `U` is
    // ZeroGradient down both pressure rows, so the face velocity is the owner's
    // own: `phi = rho (U_P . S_b)`, with no interpolation and no prescribed
    // value anywhere.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const PreparedCfdBoundary boundary = hand.outlets();
    const Vec velocity{kDiscriminatingConstant, 0.0, 0.0};
    const double rho = 998.0;
    const std::vector<double> phi = produce(hand, boundary, velocity, rho);

    for (std::size_t f = 0; f < hand.topology.faces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        const double expected = rho * dotOf(siOf(hand.geometry.faceAreaVector(face)), velocity);
        INFO("face " << f);
        CHECK_THAT(phi[f], Catch::Matchers::WithinRel(expected, 1.0e-12));
    }
}

TEST_CASE("CfdMassFlux_SignsAnInternalFaceOutOfTheOwner", "[cfd][conv]") {
    // THE CONVENTION, PINNED. A positive `phi` is flow from the OWNER to the
    // neighbour. The oracle reads `topology.owner` and applies ADR-064's
    // winding, which kills any mutation of the sign -- and would not reveal a
    // shared misunderstanding of the convention itself, which is why the
    // closed-form inlet case above exists beside it.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();
    const Vec velocity{kDiscriminatingConstant, 0.0, 0.0};
    const std::vector<double> phi = produce(hand, boundary, velocity, 998.0);

    REQUIRE(hand.topology.internalFaces() == 1);
    const FaceIndex face = FaceIndex::fromValue(0);
    const double expected = 998.0 * dotOf(siOf(hand.geometry.faceAreaVector(face)), velocity);
    CHECK_THAT(phi[0], Catch::Matchers::WithinRel(expected, 1.0e-12));
}

TEST_CASE("CfdMassFlux_RefusesADensityThatIsNotStrictlyPositiveAndFinite", "[cfd][conv]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();
    const std::vector<double> velocity = uniformVelocity(hand.topology.cells(), {0.0, 0.0, 0.0});

    const auto refusal = [&](double rho) {
        const CfdMassFluxInput input{velocity, Density::fromSi(rho)};
        return cfdMassFluxProblem(hand.topology, hand.geometry, boundary, input);
    };

    CHECK(refusal(0.0) == CfdConvectionProblem::DensityNotUsable);
    CHECK(refusal(-998.0) == CfdConvectionProblem::DensityNotUsable);
    CHECK(refusal(std::numeric_limits<double>::quiet_NaN()) ==
          CfdConvectionProblem::DensityNotUsable);
    CHECK(refusal(std::numeric_limits<double>::infinity()) ==
          CfdConvectionProblem::DensityNotUsable);
    CHECK_FALSE(refusal(998.0).has_value());
}

TEST_CASE("CfdMassFlux_RefusesAVelocityOfTheWrongLengthBeforeItsValues", "[cfd][conv]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();

    // A short array holding a NaN: the LENGTH must be reported, not the value,
    // so a wrong length is never described as whatever happens to sit in it.
    std::vector<double> wrong(3 * hand.topology.cells() - 1,
                              std::numeric_limits<double>::quiet_NaN());
    const CfdMassFluxInput input{wrong, 998_kg_per_m3};
    CHECK(cfdMassFluxProblem(hand.topology, hand.geometry, boundary, input) ==
          CfdConvectionProblem::ValueLengthWrong);
}

TEST_CASE("CfdMassFlux_RefusesANonFiniteVelocity", "[cfd][conv]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();
    std::vector<double> velocity = uniformVelocity(hand.topology.cells(), {0.0, 0.0, 0.0});
    velocity[1] = std::numeric_limits<double>::infinity();
    const CfdMassFluxInput input{velocity, 998_kg_per_m3};
    CHECK(cfdMassFluxProblem(hand.topology, hand.geometry, boundary, input) ==
          CfdConvectionProblem::ValueNotFinite);
}

// ===========================================================================
// Item 2, 3, 4 — the interpolation scheme, upwind, central
// ===========================================================================

TEST_CASE("CfdConvection_UpwindPutsTheWholeFaceFluxOnTheUpwindCell", "[cfd][conv]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();
    REQUIRE(hand.topology.internalFaces() == 1);

    const FaceIndex face = FaceIndex::fromValue(0);
    const std::size_t owner = hand.topology.owner(face).value();
    const std::optional<CellIndex> neighbourOf = hand.topology.neighbour(face);
    REQUIRE(neighbourOf.has_value());
    const std::size_t other = neighbourOf->value();

    SECTION("a positive flux makes the OWNER upwind") {
        std::vector<double> phi(hand.topology.faces(), 0.0);
        phi[0] = kDiscriminatingConstant;
        const CfdConvectionSystem system = assemble(hand, boundary, phi);
        CHECK(system.matrix.coeffSi(owner, owner) == kDiscriminatingConstant);
        CHECK(system.matrix.coeffSi(owner, other) == 0.0);
        CHECK(system.matrix.coeffSi(other, owner) == -kDiscriminatingConstant);
        CHECK(system.matrix.coeffSi(other, other) == 0.0);
    }

    SECTION("a negative flux makes the NEIGHBOUR upwind") {
        std::vector<double> phi(hand.topology.faces(), 0.0);
        phi[0] = -kDiscriminatingConstant;
        const CfdConvectionSystem system = assemble(hand, boundary, phi);
        CHECK(system.matrix.coeffSi(owner, owner) == 0.0);
        CHECK(system.matrix.coeffSi(owner, other) == -kDiscriminatingConstant);
        CHECK(system.matrix.coeffSi(other, owner) == 0.0);
        CHECK(system.matrix.coeffSi(other, other) == kDiscriminatingConstant);
    }
}

TEST_CASE("CfdConvection_CentralSplitsTheFluxByTheReDerivedInterpolationWeight", "[cfd][conv]") {
    const Hand hand = Hand::make(analyticPoints(), kAnalyticPair);
    const PreparedCfdBoundary boundary = hand.walls();
    REQUIRE(hand.topology.internalFaces() == 1);

    const FaceIndex face = FaceIndex::fromValue(0);
    const std::size_t owner = hand.topology.owner(face).value();
    const std::size_t other = hand.topology.neighbour(face)->value();
    const double weight = weightOracle(hand.topology, hand.geometry, face);

    // THE CLOSED FORM ON THIS FIXTURE: the owner's weight is exactly 2/3.
    CHECK_THAT(weight, Catch::Matchers::WithinRel(2.0 / 3.0, 1.0e-12));

    std::vector<double> phi(hand.topology.faces(), 0.0);
    phi[0] = kDiscriminatingConstant;
    const CfdConvectionSystem system =
        assemble(hand, boundary, phi, CfdConvectionScheme::Central);

    CHECK_THAT(system.matrix.coeffSi(owner, owner),
               Catch::Matchers::WithinRel(kDiscriminatingConstant * weight, 1.0e-12));
    CHECK_THAT(system.matrix.coeffSi(owner, other),
               Catch::Matchers::WithinRel(kDiscriminatingConstant * (1.0 - weight), 1.0e-12));
    CHECK_THAT(system.matrix.coeffSi(other, owner),
               Catch::Matchers::WithinRel(-kDiscriminatingConstant * weight, 1.0e-12));
    CHECK_THAT(system.matrix.coeffSi(other, other),
               Catch::Matchers::WithinRel(-kDiscriminatingConstant * (1.0 - weight), 1.0e-12));
}

TEST_CASE("CfdConvection_TheSchemeChangesTheMatrixSoTheWeightIsActuallyRead", "[cfd][conv]") {
    // A MUTATION GUARD. If `Central` silently fell through to `Upwind` -- or if
    // the weight were replaced by a constant one -- these two would agree. On
    // the analytic pair the weight is 2/3, so they must not.
    const Hand hand = Hand::make(analyticPoints(), kAnalyticPair);
    const PreparedCfdBoundary boundary = hand.walls();
    std::vector<double> phi(hand.topology.faces(), 0.0);
    phi[0] = kDiscriminatingConstant;

    const CfdConvectionSystem upwind = assemble(hand, boundary, phi, CfdConvectionScheme::Upwind);
    const CfdConvectionSystem central = assemble(hand, boundary, phi, CfdConvectionScheme::Central);
    CHECK_FALSE(upwind.matrix == central.matrix);
    CHECK(upwind.summary.scheme == CfdConvectionScheme::Upwind);
    CHECK(central.summary.scheme == CfdConvectionScheme::Central);
}

TEST_CASE("CfdConvection_UpwindReportsNoSkewnessAndCentralReportsTheWorst", "[cfd][conv]") {
    // `worstSkewness` is the measured bound on the interpolation's own error, so
    // it must be zero where nothing is interpolated and the MAXIMUM where
    // several faces are.
    //
    // THE FIXTURE IS THE CHAIN AND NOT THE ANALYTIC PAIR, because the pair has
    // exactly ONE internal face -- and on a one-element set `max`, `min`,
    // "first" and "last" all agree, so `std::max` could be replaced by any of
    // them and the test would pass. An independent review found that. The chain
    // has four internal faces with differing skewness, and the fixture is
    // asserted to discriminate before the claim is made.
    const Hand hand = Hand::make(chainPoints(5), chainTets(5));
    const PreparedCfdBoundary boundary = hand.walls();
    REQUIRE(hand.topology.internalFaces() >= 3);

    std::vector<double> phi(hand.topology.faces(), 0.0);
    for (std::size_t f = 0; f < hand.topology.internalFaces(); ++f) {
        phi[f] = kDiscriminatingConstant * (1.0 + static_cast<double>(f));
    }

    double worst = 0.0;
    double least = std::numeric_limits<double>::infinity();
    for (std::size_t f = 0; f < hand.topology.internalFaces(); ++f) {
        const std::optional<double> skew = hand.geometry.skewness(FaceIndex::fromValue(f));
        REQUIRE(skew.has_value());
        worst = std::max(worst, std::abs(*skew));
        least = std::min(least, std::abs(*skew));
    }
    // If every face had the same skewness, `max` and `min` would agree and the
    // assertion below could not distinguish them.
    CHECK(worst > least);

    const CfdConvectionSystem upwind = assemble(hand, boundary, phi, CfdConvectionScheme::Upwind);
    const CfdConvectionSystem central = assemble(hand, boundary, phi, CfdConvectionScheme::Central);
    CHECK(upwind.summary.worstSkewness == 0.0);
    CHECK_THAT(central.summary.worstSkewness, Catch::Matchers::WithinRel(worst, 1.0e-12));
    CHECK(central.summary.worstSkewness > least);
}

// ===========================================================================
// Item 6 — internal-face conservation
// ===========================================================================

TEST_CASE("CfdConvection_AnInternalFacesTwoRowsReceiveExactNegations", "[cfd][conv]") {
    // ADR-072 DECISION 3, PINNED DIRECTLY AND BIT-EXACTLY. The face computes one
    // pair of coefficients and writes them to the owner's row and their
    // negations to the neighbour's, so the two columns are exact negations --
    // for BOTH schemes, because the claim is about the traversal and not about
    // the interpolation.
    for (const CfdConvectionScheme scheme : kCfdConvectionSchemes) {
        const Hand hand = Hand::make(analyticPoints(), kAnalyticPair);
        const PreparedCfdBoundary boundary = hand.walls();
        const FaceIndex face = FaceIndex::fromValue(0);
        const std::size_t owner = hand.topology.owner(face).value();
        const std::size_t other = hand.topology.neighbour(face)->value();

        std::vector<double> phi(hand.topology.faces(), 0.0);
        phi[0] = kDiscriminatingConstant;
        const CfdConvectionSystem system = assemble(hand, boundary, phi, scheme);

        INFO("scheme " << toString(scheme));
        // The owner column: what the owner's row got and what the neighbour's
        // row got must sum to exactly zero.
        CHECK(system.matrix.coeffSi(owner, owner) + system.matrix.coeffSi(other, owner) == 0.0);
        CHECK(system.matrix.coeffSi(owner, other) + system.matrix.coeffSi(other, other) == 0.0);
    }
}

TEST_CASE("CfdConvection_TheMatrixIsAsymmetricAndThatIsCorrect", "[cfd][conv]") {
    // ADR-072 DECISION 3. A convection matrix is not symmetric, so there is no
    // `kCfdConvectionSymmetryTolerance` and `fromCsr` is given no symmetry
    // bound. The ASYMMETRY is asserted, because a symmetric convection matrix
    // would mean the upwind direction had been lost.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();
    std::vector<double> phi(hand.topology.faces(), 0.0);
    phi[0] = kDiscriminatingConstant;
    const CfdConvectionSystem system = assemble(hand, boundary, phi);
    CHECK(system.matrix.largestSymmetryError() > 0.0);
}

// ===========================================================================
// Item 7 — boundary treatment
// ===========================================================================

TEST_CASE("CfdConvection_AnOutflowBoundaryFacePutsItsFluxOnTheDiagonal", "[cfd][conv]") {
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const PreparedCfdBoundary boundary = hand.outlets();
    REQUIRE(hand.topology.internalFaces() == 0);

    std::vector<double> phi(hand.topology.faces(), 0.0);
    for (std::size_t f = 0; f < phi.size(); ++f) {
        phi[f] = kDiscriminatingConstant;
    }
    const CfdConvectionSystem system = assemble(hand, boundary, phi);

    CHECK(system.summary.outflowFaces == hand.topology.boundaryFaces());
    CHECK(system.summary.inflowFaces == 0);
    CHECK_THAT(system.matrix.coeffSi(0, 0),
               Catch::Matchers::WithinRel(
                   kDiscriminatingConstant * static_cast<double>(phi.size()), 1.0e-12));
    // Nothing reaches the source: an outflow face's value is the owner's own.
    CHECK(system.source[0] == 0.0);
}

TEST_CASE("CfdConvection_AnInflowFixedValueFacePutsItsFluxInTheSourceAndNotTheMatrix",
          "[cfd][conv]") {
    // THE ONE CASE WHOSE FLUX IS ABSENT FROM THE MATRIX, which is why the
    // row-sum identity needs the PRESCRIBED inflow restored and not all inflow.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const double speed = 2.5;
    const double rho = 998.0;
    const PreparedCfdBoundary boundary = hand.inlets(Velocity::fromSi(speed));
    const std::vector<double> phi = produce(hand, boundary, {0.0, 0.0, 0.0}, rho);
    const CfdConvectionSystem system = assemble(hand, boundary, phi);

    CHECK(system.summary.inflowFaces == hand.topology.boundaryFaces());
    CHECK(system.summary.prescribedInflowFaces == hand.topology.boundaryFaces());
    CHECK(system.summary.backflowFaces == 0);
    // The matrix got nothing at all: every facet is prescribed inflow.
    CHECK(system.matrix.coeffSi(0, 0) == 0.0);

    // And the source is `-sum phi_b U_b[component]`, re-derived here.
    double expected = 0.0;
    for (std::size_t f = 0; f < hand.topology.faces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        const std::optional<BoundaryFaceIndex> boundaryFace = hand.topology.boundaryFaceOf(face);
        REQUIRE(boundaryFace.has_value());
        const std::optional<CfdBoundaryFace> facet = boundary.face(*boundaryFace);
        REQUIRE(facet.has_value());
        REQUIRE(facet->velocity.has_value());
        expected -= phi[f] * facet->velocity->x.si();
    }
    CHECK_THAT(system.source[0], Catch::Matchers::WithinRel(expected, 1.0e-12));
}

TEST_CASE("CfdConvection_AnInflowZeroGradientFaceIsBackflowOnTheDiagonal", "[cfd][conv]") {
    // THE SECOND WAY BOUNDEDNESS FAILS, and it is ADR-067's table rather than
    // the scheme: a zero-gradient inflow face's value IS the owner's, so its
    // NEGATIVE flux lands on the diagonal and reduces dominance directly.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const PreparedCfdBoundary boundary = hand.outlets();   // U is ZeroGradient

    std::vector<double> phi(hand.topology.faces(), -kDiscriminatingConstant);
    const CfdConvectionSystem system = assemble(hand, boundary, phi);

    CHECK(system.summary.inflowFaces == hand.topology.boundaryFaces());
    CHECK(system.summary.prescribedInflowFaces == 0);
    CHECK(system.summary.backflowFaces == hand.topology.boundaryFaces());
    CHECK(system.source[0] == 0.0);
    CHECK_THAT(system.matrix.coeffSi(0, 0),
               Catch::Matchers::WithinRel(
                   -kDiscriminatingConstant * static_cast<double>(phi.size()), 1.0e-12));
    // AND THE DIAGONAL IS NEGATIVE, which is the whole point of counting it.
    CHECK(system.matrix.coeffSi(0, 0) < 0.0);
}

TEST_CASE("CfdConvection_AcceptsAComputedMassFluxPolicyAtBothPressureBoundaries", "[cfd][conv]") {
    // ADR-071 DECISION 5, RELIED ON IN BOTH DIRECTIONS. `phi` is `Computed` at
    // a pressure inlet and a pressure outlet, and neither path may refuse it:
    // the flow rate there is an OUTCOME of the solve, and by the time anyone
    // assembles convection the solve has produced it. Refusing `Computed` would
    // make the operator unusable at every pressure boundary.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);

    SECTION("pressure outlet") {
        const PreparedCfdBoundary boundary = hand.outlets();
        REQUIRE(cfd::treatmentOf(CfdBoundaryConditionKind::PressureOutlet).massFlux ==
                CfdBoundaryPolicy::Computed);
        const std::vector<double> phi = produce(hand, boundary, {1.0, 0.0, 0.0}, 998.0);
        const CfdConvectionInput input{phi, CfdConvectionScheme::Upwind, VectorComponent::X};
        CHECK_FALSE(
            cfdConvectionProblem(hand.topology, hand.geometry, boundary, input).has_value());
    }

    SECTION("pressure inlet") {
        const PreparedCfdBoundary boundary = hand.uniform(CfdBoundaryCondition{
            CfdBoundaryConditionId::fromValue(1), PressureInlet{someFace(), 0_Pa}});
        REQUIRE(cfd::treatmentOf(CfdBoundaryConditionKind::PressureInlet).massFlux ==
                CfdBoundaryPolicy::Computed);
        const std::vector<double> phi = produce(hand, boundary, {1.0, 0.0, 0.0}, 998.0);
        const CfdConvectionInput input{phi, CfdConvectionScheme::Upwind, VectorComponent::X};
        CHECK_FALSE(
            cfdConvectionProblem(hand.topology, hand.geometry, boundary, input).has_value());
    }
}

TEST_CASE("CfdConvection_AWallContributesNothingBecauseTheTableGivesItZeroFlux", "[cfd][conv]") {
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const PreparedCfdBoundary boundary = hand.walls();
    const std::vector<double> phi = produce(hand, boundary, {1.0, 2.0, 3.0}, 998.0);
    const CfdConvectionSystem system = assemble(hand, boundary, phi);

    CHECK(system.matrix.coeffSi(0, 0) == 0.0);
    CHECK(system.source[0] == 0.0);
    CHECK(system.summary.inflow.si() == 0.0);
    CHECK(system.summary.outflow.si() == 0.0);
}

TEST_CASE("CfdConvection_TreatsASlipWallAsZeroGradientRatherThanSkippingIt", "[cfd][conv]") {
    // NOT ADR-071 DECISION 7'S CHOICE, AND THE DIFFERENCE IS DELIBERATE. A
    // divergence may skip a symmetry face because its NORMAL flux is zero by
    // definition; convection transports a TANGENTIAL component across it too,
    // and symmetry prescribes the tangential components as zero-gradient. No
    // correct caller sees a difference, because the table gives a slip wall
    // `phi = 0` -- but a hand-made non-zero `phi` must still obey the row-sum
    // identity, and skipping the face would break it silently.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const PreparedCfdBoundary boundary = hand.slips();
    std::vector<double> phi(hand.topology.faces(), -kDiscriminatingConstant);
    const CfdConvectionSystem system = assemble(hand, boundary, phi);

    CHECK(system.summary.symmetryFaces == hand.topology.boundaryFaces());
    CHECK(system.summary.backflowFaces == hand.topology.boundaryFaces());
    CHECK_THAT(system.matrix.rowSumSi(0),
               Catch::Matchers::WithinRel(netOutflowOracle(hand.topology, phi, 0), 1.0e-13));
}

// ===========================================================================
// Item 9 and item 8 — constant-field transport, the row-sum identity, boundedness
// ===========================================================================

TEST_CASE("CfdConvection_RowSumPlusPrescribedInflowIsTheDivergenceOfPhi", "[cfd][conv]") {
    // THE CROSS-CHECK AGAINST P19-DIV-001, over an INDEPENDENT code path: the
    // divergence scatters per face, the row sum reads the assembled matrix.
    // ADR-072 Decision 5 measured the bound before any of this existed, so the
    // tolerance is `kCfdConvectionRowSumTolerance` and NOT bit-equality.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);

    for (const CfdConvectionScheme scheme : kCfdConvectionSchemes) {
        // ZERO-GRADIENT EVERYWHERE, SO THE PRESCRIBED TERM IS ZERO HERE, and
        // that is now said rather than denied. This comment used to read "a
        // mixture: prescribed inflow at some facets, zero-gradient at others,
        // so BOTH terms of the identity are exercised" while binding
        // `outlets()`, which makes every facet a pressure outlet and every
        // velocity policy `ZeroGradient` -- so `prescribedInflowOracle`
        // returned exactly 0.0 for every cell and the `+ prescribed` addend was
        // provably `+ 0.0` on every iteration. An independent review found it.
        // The mixed case it claimed to be is
        // `CfdConvection_TheRowSumIdentityHoldsWhereInflowIsPartlyPrescribed`.
        const PreparedCfdBoundary boundary = hand.outlets();
        std::vector<double> phi(hand.topology.faces(), 0.0);
        for (std::size_t f = 0; f < phi.size(); ++f) {
            phi[f] = (f % 2 == 0 ? 1.0 : -1.0) * kDiscriminatingConstant *
                     (1.0 + static_cast<double>(f));
        }
        const CfdConvectionSystem system = assemble(hand, boundary, phi, scheme);

        const CfdMassFluxDivergenceInput divergenceInput{phi};
        Result<CfdMassFluxDivergenceField> divergence =
            divergeCfdMassFlux(hand.topology, hand.geometry, divergenceInput);
        INFO((divergence.has_value() ? std::string{} : divergence.error().message));
        REQUIRE(divergence.has_value());

        for (std::size_t c = 0; c < hand.topology.cells(); ++c) {
            const std::optional<MassFlowRate> net = divergence->at(CellIndex::fromValue(c));
            REQUIRE(net.has_value());
            const double prescribed = prescribedInflowOracle(hand.topology, boundary, phi, c);
            INFO("scheme " << toString(scheme) << " cell " << c);
            CHECK_THAT(system.matrix.rowSumSi(c) + prescribed,
                       Catch::Matchers::WithinRel(net->si(), kCfdConvectionRowSumTolerance));
        }
    }
}

TEST_CASE("CfdConvection_TransportsAConstantAsTheNetMassOutflow", "[cfd][conv]") {
    // THIS TEST USED TO BE VACUOUS, AND THE REPLACEMENT IS THE POINT.
    //
    // It asserted `(A (k 1))[c] == k * rowSumSi(c)`, which holds for ANY matrix
    // whatever -- it is linearity of `multiply` against `rowSumSi`, not a
    // property of this operator, and no mutation of the assembly could fail it.
    // An independent review found it. Its fixture comment was wrong too: it
    // claimed "a uniform velocity on a closed box: divergence-free to the
    // mesh's own closure", but on `kTwoCells` with walls the single internal
    // face carries a non-zero `phi` while all three walls are forced to exactly
    // zero by the table, so each cell's net outflow is +-phi and NOT zero.
    //
    // The claim that belongs to the operator is that transporting a constant
    // gives the NET MASS OUTFLOW per cell -- so the oracle is the re-derived
    // outflow, not the matrix's own row sum.
    for (const CfdConvectionScheme scheme : kCfdConvectionSchemes) {
        const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
        const PreparedCfdBoundary boundary = hand.walls();
        const std::vector<double> phi =
            produce(hand, boundary, {kDiscriminatingConstant, 0.0, 0.0}, 998.0);
        const CfdConvectionSystem system = assemble(hand, boundary, phi, scheme);

        // The fixture is established to discriminate: if the net outflow were
        // zero, every expected value below would be zero and the test could
        // not fail.
        REQUIRE(netOutflowOracle(hand.topology, phi, 0) != 0.0);

        std::vector<double> constant(hand.topology.cells(), kDiscriminatingConstant);
        Result<std::vector<double>> product = system.matrix.multiply(constant);
        REQUIRE(product.has_value());

        for (std::size_t c = 0; c < hand.topology.cells(); ++c) {
            // No prescribed inflow on a walls boundary, so the row sum IS the
            // net outflow and `A (k 1)` is `k` times it.
            const double expected = kDiscriminatingConstant *
                                    netOutflowOracle(hand.topology, phi, c);
            INFO("scheme " << toString(scheme) << " cell " << c);
            CHECK_THAT((*product)[c],
                       Catch::Matchers::WithinRel(expected, kCfdConvectionRowSumTolerance));
        }
    }
}

TEST_CASE("CfdConvection_UpwindCoefficientsAreSignedForBoundedness", "[cfd][conv]") {
    // THE STRUCTURAL HALF OF THE BOUNDEDNESS POLICY: under `Upwind` the
    // diagonal is a sum of `max(phi,0)` and every off-diagonal is a
    // `min(phi,0)`, so the signs are right by construction. Asserted on a mesh
    // with real connectivity and an arbitrary flux, not on a fixture chosen to
    // make it true.
    const Hand hand = Hand::make(chainPoints(5), chainTets(5));
    const PreparedCfdBoundary boundary = hand.walls();
    std::vector<double> phi(hand.topology.faces(), 0.0);
    for (std::size_t f = 0; f < hand.topology.internalFaces(); ++f) {
        phi[f] = (f % 3 == 0 ? -1.0 : 1.0) * kDiscriminatingConstant * (1.0 + static_cast<double>(f));
    }
    const CfdConvectionSystem system = assemble(hand, boundary, phi);

    for (std::size_t r = 0; r < hand.topology.cells(); ++r) {
        INFO("row " << r);
        CHECK(system.matrix.coeffSi(r, r) >= 0.0);
        for (std::size_t c = 0; c < hand.topology.cells(); ++c) {
            if (c == r) {
                continue;
            }
            INFO("column " << c);
            CHECK(system.matrix.coeffSi(r, c) <= 0.0);
        }
    }
}

TEST_CASE("CfdConvection_TheDominanceDeficitIsTheNegatedRowSum", "[cfd][conv]") {
    // ADR-072 DECISION 7, MEASURED FIRST: the amount by which upwind fails
    // weak diagonal dominance IS the cell's net outflow, so boundedness and
    // continuity are one measurement and no second number is invented.
    const Hand hand = Hand::make(chainPoints(5), chainTets(5));
    const PreparedCfdBoundary boundary = hand.walls();
    std::vector<double> phi(hand.topology.faces(), 0.0);
    for (std::size_t f = 0; f < hand.topology.internalFaces(); ++f) {
        phi[f] = (f % 3 == 0 ? -1.0 : 1.0) * kDiscriminatingConstant * (1.0 + static_cast<double>(f));
    }
    const CfdConvectionSystem system = assemble(hand, boundary, phi);

    for (std::size_t r = 0; r < hand.topology.cells(); ++r) {
        double off = 0.0;
        double scale = std::abs(system.matrix.coeffSi(r, r));
        for (std::size_t c = 0; c < hand.topology.cells(); ++c) {
            if (c != r) {
                off += std::abs(system.matrix.coeffSi(r, c));
                scale += std::abs(system.matrix.coeffSi(r, c));
            }
        }
        // THE IDENTITY NEEDS `a_ii >= 0`, which is ADR-072 Decision 7's third
        // condition: the deficit is defined with `|a_ii|`, so once backflow
        // drives the diagonal negative the identity breaks whatever the inflow
        // term is. A walls boundary has no backflow, and that is asserted
        // rather than assumed.
        REQUIRE(system.summary.backflowFaces == 0);
        REQUIRE(system.matrix.coeffSi(r, r) >= 0.0);
        const double deficit = off - std::abs(system.matrix.coeffSi(r, r));
        INFO("row " << r);
        // A PER-ROW SCALE, NOT THE MATRIX-WIDE MAXIMUM, and `1e-13` rather than
        // `1e-9`. The earlier bound divided by `largestMagnitude()` over the
        // whole matrix, which on a mesh whose rows span orders of magnitude
        // admits an arbitrarily large RELATIVE error in a small row -- and
        // `1e-9` had no derivation, against CLAUDE.md's "tolerances need a
        // reason". These are sums of at most five doubles of like magnitude, so
        // the arithmetic needs `1e-15`; `1e-13` is two decades of margin.
        CHECK_THAT(deficit, Catch::Matchers::WithinAbs(-system.matrix.rowSumSi(r),
                                                       kCfdConvectionRowSumTolerance * scale));
    }
}

// ===========================================================================
// Item 10 and item 11 — 1D advection and reverse flow
// ===========================================================================

TEST_CASE("CfdConvection_OnAChainWithUniformFluxTheMatrixIsBidiagonalAndHandCheckable",
          "[cfd][conv]") {
    // THE 1D ADVECTION CASE. A chain of cells with ONE uniform flux `q` through
    // every internal face: every interior row reads `+q` on the diagonal and
    // `-q` on its upwind neighbour, and nothing else. Hand arithmetic, no
    // tolerance.
    const std::size_t cells = 5;
    const Hand hand = Hand::make(chainPoints(cells), chainTets(cells));
    REQUIRE(hand.topology.cells() == cells);
    REQUIRE(hand.topology.internalFaces() == cells - 1);

    const PreparedCfdBoundary boundary = hand.walls();
    const double q = kDiscriminatingConstant;
    std::vector<double> phi(hand.topology.faces(), 0.0);
    for (std::size_t f = 0; f < hand.topology.internalFaces(); ++f) {
        phi[f] = q;
    }
    const CfdConvectionSystem system = assemble(hand, boundary, phi);

    for (std::size_t f = 0; f < hand.topology.internalFaces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        const std::size_t owner = hand.topology.owner(face).value();
        const std::size_t other = hand.topology.neighbour(face)->value();
        INFO("internal face " << f);
        // `q > 0` means the owner is upwind, so the owner keeps `+q` and the
        // neighbour's row points back at the owner with `-q`.
        CHECK(system.matrix.coeffSi(other, owner) == -q);
        CHECK(system.matrix.coeffSi(owner, other) == 0.0);
    }
}

TEST_CASE("CfdConvection_AdmitsTheConstantSteadySolutionForAConservativeFlux", "[cfd][conv]") {
    // THE ANALYTIC 1D ADVECTION ANSWER, reached without a solver and without
    // assuming anything about the mesh's winding.
    //
    // The exact steady solution of `div(phi x) = 0` with a uniform upstream
    // value is a CONSTANT profile. So for a conservative `phi` and a boundary
    // that prescribes no value, the constant vector must satisfy `A x = b`
    // exactly -- `b` is zero, because only a FixedValue inflow face writes a
    // source, and every facet here is a pressure outlet. That makes the claim
    // `A 1 = 0`, which needs no linear solve and no forward substitution.
    //
    // A FIRST DRAFT OF THIS TEST SUBSTITUTED FORWARD ALONG THE CHAIN AND WAS
    // WRONG. With every facet a wall the chain is CLOSED -- no inlet and no
    // outlet -- so a uniform internal flux is NOT conservative at the two END
    // cells, and the last cell's diagonal is legitimately zero. ADR-072's own
    // Q3a measurement had already recorded that the ends of an open chain are
    // not conservative; the test was written as though it had not. The flux is
    // made conservative BY CONSTRUCTION here instead.
    const std::size_t cells = 5;
    const Hand hand = Hand::make(chainPoints(cells), chainTets(cells));
    // Pressure outlets: `U` is ZeroGradient everywhere, so nothing is
    // prescribed and the source is empty.
    const PreparedCfdBoundary boundary = hand.outlets();

    // A CONSERVATIVE `phi`, BUILT WITHOUT ASSUMING THE WINDING. Put a uniform
    // flux through every internal face, then give each cell one boundary face
    // carrying exactly the negation of whatever net outflow it ended up with.
    // `d + (-d)` is exactly zero for a single double, so the construction is
    // bit-exact whichever way each face happens to be wound.
    std::vector<double> phi(hand.topology.faces(), 0.0);
    for (std::size_t f = 0; f < hand.topology.internalFaces(); ++f) {
        phi[f] = kDiscriminatingConstant;
    }
    for (std::size_t c = 0; c < cells; ++c) {
        const double net = netOutflowOracle(hand.topology, phi, c);
        bool placed = false;
        for (std::size_t f = hand.topology.internalFaces(); f < hand.topology.faces(); ++f) {
            if (hand.topology.owner(FaceIndex::fromValue(f)).value() == c && phi[f] == 0.0) {
                phi[f] = -net;
                placed = true;
                break;
            }
        }
        INFO("cell " << c << " needs a boundary face to balance on");
        REQUIRE(placed);
    }
    // The construction is asserted, not assumed: every cell is now exactly
    // conservative.
    for (std::size_t c = 0; c < cells; ++c) {
        INFO("cell " << c);
        CHECK(netOutflowOracle(hand.topology, phi, c) == 0.0);
    }

    for (const CfdConvectionScheme scheme : kCfdConvectionSchemes) {
        const CfdConvectionSystem system = assemble(hand, boundary, phi, scheme);
        // No facet prescribes a value, so the source is empty and the constant
        // must be an exact steady solution.
        for (std::size_t c = 0; c < cells; ++c) {
            INFO("scheme " << toString(scheme) << " cell " << c);
            CHECK(system.source[c] == 0.0);
        }
        CHECK(system.summary.prescribedInflowFaces == 0);

        std::vector<double> constant(cells, kDiscriminatingConstant);
        Result<std::vector<double>> residual = system.matrix.multiply(constant);
        REQUIRE(residual.has_value());
        const double scale = kDiscriminatingConstant * system.matrix.largestMagnitude();
        for (std::size_t c = 0; c < cells; ++c) {
            INFO("scheme " << toString(scheme) << " cell " << c);
            CHECK_THAT((*residual)[c],
                       Catch::Matchers::WithinAbs(0.0, kCfdConvectionRowSumTolerance * scale));
        }
    }
}

TEST_CASE("CfdConvection_ReversingTheFluxTransposesTheMatrixOnAConservativeChain",
          "[cfd][conv]") {
    // ADR-072 DECISION 8, DERIVED INDEPENDENTLY OF THE CODE:
    //     A(-phi) == A(phi)^T - diag(div phi)
    // because reversing a face's flux swaps the two cells' roles. On a chain
    // with a uniform flux an INTERIOR cell's divergence is BIT-zero -- one face
    // in, one face out, the same double -- so the reversed matrix is the exact
    // transpose there. Measured bit-exact on 2000 of 2000 chains before any C++
    // existed, which is why this asserts equality and not a tolerance.
    const std::size_t cells = 5;
    const Hand hand = Hand::make(chainPoints(cells), chainTets(cells));
    const PreparedCfdBoundary boundary = hand.walls();
    const double q = kDiscriminatingConstant;
    std::vector<double> forward(hand.topology.faces(), 0.0);
    for (std::size_t f = 0; f < hand.topology.internalFaces(); ++f) {
        forward[f] = q;
    }
    std::vector<double> reversed(forward.size(), 0.0);
    for (std::size_t f = 0; f < forward.size(); ++f) {
        reversed[f] = -forward[f];
    }

    const CfdConvectionSystem a = assemble(hand, boundary, forward);
    const CfdConvectionSystem b = assemble(hand, boundary, reversed);

    for (std::size_t r = 1; r + 1 < cells; ++r) {
        // An interior cell of the chain: its divergence is bit-zero.
        CHECK(netOutflowOracle(hand.topology, forward, r) == 0.0);
        for (std::size_t c = 0; c < cells; ++c) {
            INFO("interior row " << r << " column " << c);
            CHECK(b.matrix.coeffSi(r, c) == a.matrix.coeffSi(c, r));
        }
    }
}

// ===========================================================================
// Item 12 — sparse assembly
// ===========================================================================

TEST_CASE("CfdConvection_AssemblesASquareMatrixWhosePatternIsTheCellAdjacency", "[cfd][conv]") {
    const Hand hand = Hand::make(chainPoints(5), chainTets(5));
    const PreparedCfdBoundary boundary = hand.walls();
    const std::vector<double> phi = produce(hand, boundary, {1.0, 0.0, 0.0}, 998.0);
    const CfdConvectionSystem system = assemble(hand, boundary, phi);

    CHECK(system.matrix.rows() == hand.topology.cells());
    CHECK(system.matrix.columns() == hand.topology.cells());
    CHECK(system.source.size() == hand.topology.cells());
    // One diagonal per cell, plus two off-diagonals per internal face.
    CHECK(system.matrix.nonZeros() ==
          hand.topology.cells() + 2 * hand.topology.internalFaces());
}

TEST_CASE("CfdConvection_ReportsCountsThatAddUpToTheTopologysOwn", "[cfd][conv]") {
    const Hand hand = Hand::make(chainPoints(4), chainTets(4));
    const PreparedCfdBoundary boundary = hand.outlets();
    const std::vector<double> phi = produce(hand, boundary, {1.0, 2.0, 3.0}, 998.0);
    const CfdConvectionSystem system = assemble(hand, boundary, phi);

    CHECK(system.summary.cells == hand.topology.cells());
    CHECK(system.summary.internalFaces == hand.topology.internalFaces());
    CHECK(system.summary.boundaryFaces == hand.topology.boundaryFaces());
    CHECK(system.summary.outflowFaces + system.summary.inflowFaces ==
          hand.topology.boundaryFaces());
    CHECK(system.summary.fixedValueFaces + system.summary.zeroGradientFaces +
              system.summary.symmetryFaces ==
          hand.topology.boundaryFaces());
    CHECK(system.summary.prescribedInflowFaces + system.summary.backflowFaces ==
          system.summary.inflowFaces);
}

// ===========================================================================
// The gate's phase order
// ===========================================================================

TEST_CASE("CfdConvection_TheDeclaredPhaseOrderIsTheCheckOrder", "[cfd][conv]") {
    // An input with faults in two phases reports the EARLIER phase. Only the
    // phases a constructible input can reach are asserted, which is what
    // `kCfdConvectionProblems` documents.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const Hand other = Hand::make(oneCellPoints(), kOneCell);
    const PreparedCfdBoundary boundary = hand.walls();

    SECTION("geometry before everything") {
        // Another mesh's geometry AND a wrong length AND a NaN.
        std::vector<double> wrong(1, std::numeric_limits<double>::quiet_NaN());
        const CfdConvectionInput input{wrong, CfdConvectionScheme::Upwind, VectorComponent::X};
        CHECK(cfdConvectionProblem(hand.topology, other.geometry, boundary, input) ==
              CfdConvectionProblem::GeometryDescribesAnotherTopology);
    }

    SECTION("boundary before the values") {
        const PreparedCfdBoundary foreign = other.walls();
        std::vector<double> wrong(1, std::numeric_limits<double>::quiet_NaN());
        const CfdConvectionInput input{wrong, CfdConvectionScheme::Upwind, VectorComponent::X};
        CHECK(cfdConvectionProblem(hand.topology, hand.geometry, foreign, input) ==
              CfdConvectionProblem::BoundaryDescribesAnotherTopology);
    }

    SECTION("length before value") {
        std::vector<double> wrong(hand.topology.faces() - 1,
                                  std::numeric_limits<double>::quiet_NaN());
        const CfdConvectionInput input{wrong, CfdConvectionScheme::Upwind, VectorComponent::X};
        CHECK(cfdConvectionProblem(hand.topology, hand.geometry, boundary, input) ==
              CfdConvectionProblem::ValueLengthWrong);
    }

    SECTION("density before the velocity's length, on the producer") {
        std::vector<double> wrong(1, 0.0);
        const CfdMassFluxInput input{wrong, Density::fromSi(-1.0)};
        CHECK(cfdMassFluxProblem(hand.topology, hand.geometry, boundary, input) ==
              CfdConvectionProblem::DensityNotUsable);
    }
}

TEST_CASE("CfdConvection_EveryProblemHasAName", "[cfd][conv]") {
    for (const CfdConvectionProblem problem : kCfdConvectionProblems) {
        CHECK_FALSE(toString(problem).empty());
        CHECK(toString(problem) != "<unknown>");
    }
    for (const CfdConvectionScheme scheme : kCfdConvectionSchemes) {
        CHECK_FALSE(toString(scheme).empty());
        CHECK(toString(scheme) != "<unknown>");
    }
}

TEST_CASE("CfdConvection_TheTaggedFailureCarriesBothTheTagAndTheMessage", "[cfd][conv]") {
    // ADR-072 DECISION 10: the assembly returns `std::expected` with a TAGGED
    // failure, so a refusal is read as a tag and not grepped out of a sentence.
    // P19-DIV-001 recorded the absence of this as its L10.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();
    std::vector<double> wrong(hand.topology.faces() - 1, 0.0);
    const CfdConvectionInput input{wrong, CfdConvectionScheme::Upwind, VectorComponent::X};

    std::expected<CfdConvectionSystem, CfdConvectionFailure> system =
        assembleCfdConvection(hand.topology, hand.geometry, boundary, input);
    REQUIRE_FALSE(system.has_value());
    CHECK(system.error().problem == CfdConvectionProblem::ValueLengthWrong);
    CHECK_THAT(system.error().error.message, Catch::Matchers::ContainsSubstring("requires"));

    const std::optional<CfdConvectionFailure> predicted =
        cfdConvectionFailure(hand.topology, hand.geometry, boundary, input);
    REQUIRE(predicted.has_value());
    CHECK(predicted->problem == system.error().problem);
}

// ===========================================================================
// Determinism and the reference bodies
// ===========================================================================

TEST_CASE("CfdConvection_IsBitwiseReproducibleAcrossRepeatedAssemblies", "[cfd][conv]") {
    const Hand hand = Hand::make(chainPoints(5), chainTets(5));
    const PreparedCfdBoundary boundary = hand.outlets();
    const std::vector<double> phi = produce(hand, boundary, {1.0, 2.0, 3.0}, 998.0);

    for (const CfdConvectionScheme scheme : kCfdConvectionSchemes) {
        const CfdConvectionSystem first = assemble(hand, boundary, phi, scheme);
        const CfdConvectionSystem second = assemble(hand, boundary, phi, scheme);
        INFO("scheme " << toString(scheme));
        CHECK(first.matrix == second.matrix);
        CHECK(first.source == second.source);
        CHECK(first.summary == second.summary);
    }
}

TEST_CASE("CfdConvection_OnTheReferenceBodiesTheRowSumIdentityHolds", "[cfd][conv][reference]") {
    // THE SAME IDENTITY, ON MESHES NOBODY DESIGNED TO FLATTER IT. Every facet
    // is a no-slip wall, so `phi` is exactly zero at the boundary and the row
    // sum is the pure internal-face divergence.
    const std::vector<Meshable> models = meshableModels();
    REQUIRE_FALSE(models.empty());

    for (const Meshable& model : models) {
        const Reference reference{model.kind};
        const PreparedCfdBoundary boundary = reference.wallEverywhere();
        const std::vector<double> cellVelocity =
            uniformVelocity(reference.topology->cells(), {kDiscriminatingConstant, 0.0, 0.0});

        const CfdMassFluxInput produced{cellVelocity, 998_kg_per_m3};
        std::expected<std::vector<double>, CfdConvectionFailure> phi =
            cfdMassFlux(*reference.topology, *reference.geometry, boundary, produced);
        INFO(model.name << ": " << (phi.has_value() ? std::string{} : phi.error().error.message));
        REQUIRE(phi.has_value());

        const CfdConvectionInput input{*phi, CfdConvectionScheme::Upwind, VectorComponent::X};
        std::expected<CfdConvectionSystem, CfdConvectionFailure> system =
            assembleCfdConvection(*reference.topology, *reference.geometry, boundary, input);
        INFO(model.name);
        REQUIRE(system.has_value());

        const CfdMassFluxDivergenceInput divergenceInput{*phi};
        Result<CfdMassFluxDivergenceField> divergence =
            divergeCfdMassFlux(*reference.topology, *reference.geometry, divergenceInput);
        REQUIRE(divergence.has_value());

        for (std::size_t c = 0; c < reference.topology->cells(); ++c) {
            const std::optional<MassFlowRate> net = divergence->at(CellIndex::fromValue(c));
            REQUIRE(net.has_value());
            // A PER-ROW SCALE. An earlier version divided by
            // `largestMagnitude()` over the WHOLE matrix, which on a real body
            // -- whose per-cell magnitudes span orders of magnitude -- admits
            // an arbitrarily large relative error in a small-magnitude cell,
            // potentially a complete formula error there. An independent review
            // found it. The scale a floating-point sum's error belongs to is
            // the magnitude THAT ROW accumulated.
            double scale = 0.0;
            for (std::size_t j = 0; j < reference.topology->cells(); ++j) {
                scale += std::abs(system->matrix.coeffSi(c, j));
            }
            scale = std::max(scale, 1.0e-300);
            INFO(model.name << " cell " << c);
            CHECK_THAT(system->matrix.rowSumSi(c),
                       Catch::Matchers::WithinAbs(net->si(),
                                                  kCfdConvectionRowSumTolerance * scale));
        }
    }
}

TEST_CASE("CfdConvection_TheProducedFluxAgreesWithTheVelocityDivergenceToDensity",
          "[cfd][conv][reference]") {
    // THE TEST THAT PINS ADR-072 DECISION 9'S DECLARED DUPLICATION. The face
    // velocity is interpolated in two places -- privately in
    // `CfdDivergence.cpp` and in `CfdConvection.cpp` -- and they must agree.
    // `div(cfdMassFlux(U, rho))` must be `rho` times `div(U)` per cell, so a
    // drift between the two interpolations FAILS HERE rather than producing two
    // slightly different answers in production.
    const std::vector<Meshable> models = meshableModels();
    REQUIRE_FALSE(models.empty());
    const double rho = 998.0;

    for (const Meshable& model : models) {
        const Reference reference{model.kind};
        const PreparedCfdBoundary boundary = reference.wallEverywhere();
        const std::vector<double> cellVelocity =
            uniformVelocity(reference.topology->cells(), {kDiscriminatingConstant, 0.0, 0.0});

        const CfdMassFluxInput produced{cellVelocity, Density::fromSi(rho)};
        std::expected<std::vector<double>, CfdConvectionFailure> phi =
            cfdMassFlux(*reference.topology, *reference.geometry, boundary, produced);
        REQUIRE(phi.has_value());

        const CfdMassFluxDivergenceInput massInput{*phi};
        Result<CfdMassFluxDivergenceField> massDivergence =
            divergeCfdMassFlux(*reference.topology, *reference.geometry, massInput);
        REQUIRE(massDivergence.has_value());

        const CfdVelocityDivergenceInput velocityInput{cellVelocity};
        Result<CfdVelocityDivergenceField> velocityDivergence = divergeCfdVelocity(
            *reference.topology, *reference.geometry, boundary, velocityInput);
        INFO(model.name << ": "
                        << (velocityDivergence.has_value() ? std::string{}
                                                           : velocityDivergence.error().message));
        REQUIRE(velocityDivergence.has_value());

        // THE WALLS ARE WHERE THE TWO PATHS DIVERGE BY DESIGN, so this compares
        // the INTERNAL contribution only: the producer writes the table's zero
        // at a wall, while `divergeCfdVelocity` dots the prescribed wall
        // velocity with the area vector. For a no-slip wall that velocity is
        // zero, so both are zero and the comparison is clean.
        const double scale =
            std::max(massDivergence->largestMagnitude().si(), 1.0e-300);
        for (std::size_t c = 0; c < reference.topology->cells(); ++c) {
            const std::optional<MassFlowRate> mass =
                massDivergence->at(CellIndex::fromValue(c));
            const std::optional<VolumetricFlowRate> volume =
                velocityDivergence->at(CellIndex::fromValue(c));
            REQUIRE(mass.has_value());
            REQUIRE(volume.has_value());
            INFO(model.name << " cell " << c);
            CHECK_THAT(mass->si(),
                       Catch::Matchers::WithinAbs(rho * volume->si(), 1.0e-9 * scale));
        }
    }
}

// ===========================================================================
// Gaps closed after two independent adversarial reviews, run BEFORE the
// qualification freeze. Each test below names the mutation it kills.
// ===========================================================================

TEST_CASE("CfdMassFlux_InterpolatesTheFaceVelocityWithTheReDerivedWeight", "[cfd][conv]") {
    // THE SUITE'S SHARPEST GAP, AND IT IS THE PREVIOUS MILESTONE'S RECORDED
    // DEFECT RECURRING. Every velocity array in the first draft came from
    // `uniformVelocity`, and with `U_P == U_N` the face value
    // `U_N + w (U_P - U_N)` is INDEPENDENT OF `w` for every weight -- so
    // `cfdFaceInterpolationWeight` was completely unpinned on the producer
    // path, `w -> 0.5` survived, an owner/neighbour transposition survived, and
    // ADR-072 Decision 9's designated cross-check was a tautology. That is
    // P19-GRAD-001's r5 and P19-DIV-001's own review finding, in the same
    // place, one milestone later.
    //
    // Two cells with DIFFERENT velocities make the face value a genuine
    // function of the weight.
    const Hand hand = Hand::make(analyticPoints(), kAnalyticPair);
    const PreparedCfdBoundary boundary = hand.walls();
    REQUIRE(hand.topology.internalFaces() == 1);

    const FaceIndex face = FaceIndex::fromValue(0);
    const double rho = 998.0;
    const Vec ownerVelocity{kDiscriminatingConstant, 0.0, 0.0};
    const Vec otherVelocity{-3.0 * kDiscriminatingConstant, 7.5, -2.25};

    const std::size_t owner = hand.topology.owner(face).value();
    const std::size_t other = hand.topology.neighbour(face)->value();
    std::vector<Vec> byCell(hand.topology.cells(), Vec{0.0, 0.0, 0.0});
    byCell[owner] = ownerVelocity;
    byCell[other] = otherVelocity;

    const std::vector<double> phi = produceWith(hand, boundary, perCellVelocity(byCell), rho);

    const double weight = weightOracle(hand.topology, hand.geometry, face);
    const Vec stored = siOf(hand.geometry.faceAreaVector(face));
    const double expected =
        rho * dotOf(stored, interpolateOracle(ownerVelocity, otherVelocity, weight));
    CHECK_THAT(phi[0], Catch::Matchers::WithinRel(expected, 1.0e-12));

    // AND THE FIXTURE IS ESTABLISHED TO DISCRIMINATE: the correct answer must
    // differ from what each wrong weight would give, or the assertion above
    // could pass for the wrong reason.
    const double atOwner = rho * dotOf(stored, ownerVelocity);
    const double atOther = rho * dotOf(stored, otherVelocity);
    const double atMidpoint =
        rho * dotOf(stored, interpolateOracle(ownerVelocity, otherVelocity, 0.5));
    const double transposed =
        rho * dotOf(stored, interpolateOracle(otherVelocity, ownerVelocity, weight));
    CHECK(std::abs(phi[0] - atOwner) > 1.0);
    CHECK(std::abs(phi[0] - atOther) > 1.0);
    CHECK(std::abs(phi[0] - atMidpoint) > 1.0);
    CHECK(std::abs(phi[0] - transposed) > 1.0);
}

TEST_CASE("CfdConvection_CentralTreatsAPrescribedInflowFaceByTheBoundaryRule", "[cfd][conv]") {
    // THE PROBE ADR-072 DECISION 6 NAMES IN AS MANY WORDS, which the first
    // draft of this suite did not run: every `Central` assembly used walls
    // (`phi == 0`) or outlets (ZeroGradient), so no `Central` assembly ever met
    // a `FixedValue` inflow face.
    //
    // `cfdFaceInterpolationWeight` returns 1.0 for a boundary face, so routing
    // boundary faces through the scheme branch would make the face value the
    // owner's and SILENTLY DISCARD the inlet's prescribed value.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const double speed = 2.5;
    const PreparedCfdBoundary boundary = hand.inlets(Velocity::fromSi(speed));
    const std::vector<double> phi = produce(hand, boundary, {0.0, 0.0, 0.0}, 998.0);

    double expected = 0.0;
    for (std::size_t f = 0; f < hand.topology.faces(); ++f) {
        const std::optional<BoundaryFaceIndex> bf =
            hand.topology.boundaryFaceOf(FaceIndex::fromValue(f));
        REQUIRE(bf.has_value());
        const std::optional<CfdBoundaryFace> facet = boundary.face(*bf);
        REQUIRE(facet.has_value());
        REQUIRE(facet->velocity.has_value());
        expected -= phi[f] * facet->velocity->x.si();
    }
    REQUIRE(expected != 0.0);

    for (const CfdConvectionScheme scheme : kCfdConvectionSchemes) {
        const CfdConvectionSystem system = assemble(hand, boundary, phi, scheme);
        INFO("scheme " << toString(scheme));
        CHECK_THAT(system.source[0], Catch::Matchers::WithinRel(expected, 1.0e-12));
        CHECK(system.matrix.coeffSi(0, 0) == 0.0);
        CHECK(system.summary.prescribedInflowFaces == hand.topology.boundaryFaces());
    }
}

TEST_CASE("CfdConvection_AnOutflowFaceUsesTheOwnersValueEvenWhereAVelocityIsPrescribed",
          "[cfd][conv]") {
    // ADR-072 DECISION 6'S FIRST ROW: at `phi_b >= 0` the upwind value is the
    // owner's WHATEVER THE POLICY SAYS, which production implements by testing
    // the sign BEFORE the policy. In the first draft every `FixedValue` face
    // carried `phi == 0` (walls) or `phi < 0` (inlets), so moving the
    // `FixedValue` block above the sign test changed nothing any test read.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const PreparedCfdBoundary boundary = hand.inlets(Velocity::fromSi(2.5));
    const std::size_t facets = hand.topology.boundaryFaces();
    const std::vector<double> phi(hand.topology.faces(), kDiscriminatingConstant);
    const CfdConvectionSystem system = assemble(hand, boundary, phi);

    CHECK(system.summary.outflowFaces == facets);
    CHECK(system.summary.inflowFaces == 0);
    CHECK(system.summary.prescribedInflowFaces == 0);
    CHECK(system.summary.fixedValueFaces == facets);
    CHECK(system.source[0] == 0.0);
    CHECK_THAT(system.matrix.coeffSi(0, 0),
               Catch::Matchers::WithinRel(
                   kDiscriminatingConstant * static_cast<double>(facets), 1.0e-12));
}

TEST_CASE("CfdConvection_AssemblesTheComponentItWasAsked", "[cfd][conv]") {
    // `source[owner] -= phi * prescribed[componentIndex]`. The first draft only
    // ever asked for X, so `componentIndex -> 0` and any Y/Z swap survived.
    // A velocity inlet prescribes `-v n_hat`, so each facet's prescribed vector
    // points along its own normal and the three component sums differ.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const PreparedCfdBoundary boundary = hand.inlets(Velocity::fromSi(2.5));

    // A HAND-MADE `phi`, AND NOT THE PRODUCER'S, BECAUSE CLOSURE WOULD MAKE
    // THE TEST UNABLE TO FAIL. With the produced flux,
    // `phi_f = -rho v |S_f|` and `prescribed_f = -v n_f`, so
    // `source[c] = -rho v^2 sum_f |S_f| n_f[c] = -rho v^2 (sum_f S_f)[c]`,
    // and `sum_f S_f` is zero for a closed cell -- so all three components
    // came out equal (at zero) and the discrimination check below failed on a
    // CORRECT operator. That is P19-DIV-001's own recorded trap: "a closed
    // cell's flux total is `U . sum_f S_f`, which closure makes zero for ANY
    // constant velocity, so a test of which velocity was used could not fail."
    // A flux that is not proportional to the face area does not collapse.
    std::vector<double> phi(hand.topology.faces(), 0.0);
    for (std::size_t f = 0; f < phi.size(); ++f) {
        phi[f] = -kDiscriminatingConstant * (1.0 + static_cast<double>(f));
    }

    std::array<double, 3> sources{0.0, 0.0, 0.0};
    for (const VectorComponent component : cfd::kVectorComponents) {
        const CfdConvectionSystem system =
            assemble(hand, boundary, phi, CfdConvectionScheme::Upwind, component);
        const auto index = static_cast<std::size_t>(component);

        double expected = 0.0;
        for (std::size_t f = 0; f < hand.topology.faces(); ++f) {
            const std::optional<BoundaryFaceIndex> bf =
                hand.topology.boundaryFaceOf(FaceIndex::fromValue(f));
            REQUIRE(bf.has_value());
            const std::optional<CfdBoundaryFace> facet = boundary.face(*bf);
            REQUIRE(facet.has_value());
            REQUIRE(facet->velocity.has_value());
            const Vec prescribed{facet->velocity->x.si(), facet->velocity->y.si(),
                                 facet->velocity->z.si()};
            expected -= phi[f] * prescribed[index];
        }
        INFO("component " << index);
        CHECK_THAT(system.source[0], Catch::Matchers::WithinRel(expected, 1.0e-12));
        sources[index] = system.source[0];
    }

    // THE FIXTURE MUST DISCRIMINATE, or a constant index would pass by
    // symmetry.
    CHECK(sources[0] != sources[1]);
    CHECK(sources[1] != sources[2]);
    CHECK(sources[0] != sources[2]);
}

TEST_CASE("CfdConvection_RefusesATransportedComponentOutsideTheThree", "[cfd][conv]") {
    // `VectorComponent` has a fixed underlying type, so a cast can produce a
    // well-formed value outside the three and `prescribed[7]` on a
    // three-element array is undefined behaviour. The gate claims to be total.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();
    const std::vector<double> phi(hand.topology.faces(), 0.0);
    const CfdConvectionInput input{phi, CfdConvectionScheme::Upwind,
                                   static_cast<VectorComponent>(7)};

    CHECK(cfdConvectionProblem(hand.topology, hand.geometry, boundary, input) ==
          CfdConvectionProblem::ComponentOutOfRange);
    std::expected<CfdConvectionSystem, CfdConvectionFailure> system =
        assembleCfdConvection(hand.topology, hand.geometry, boundary, input);
    REQUIRE_FALSE(system.has_value());
    CHECK(system.error().problem == CfdConvectionProblem::ComponentOutOfRange);
}

TEST_CASE("CfdConvection_RefusesANonFiniteAssembledCoefficient", "[cfd][conv]") {
    // `ResultIsNotFinite` IS REACHABLE AND WAS UNTESTED, which matters because
    // its observability is ADR-072 Decision 10's whole argument for returning a
    // tagged failure rather than a plain `Result`. Every INPUT here is finite;
    // the diagonal overflows in the accumulation.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const PreparedCfdBoundary boundary = hand.outlets();
    const std::vector<double> phi(hand.topology.faces(), 1.0e308);
    for (const double value : phi) {
        REQUIRE(std::isfinite(value));
    }
    std::expected<CfdConvectionSystem, CfdConvectionFailure> system = assembleCfdConvection(
        hand.topology, hand.geometry, boundary,
        CfdConvectionInput{phi, CfdConvectionScheme::Upwind, VectorComponent::X});
    REQUIRE_FALSE(system.has_value());
    CHECK(system.error().problem == CfdConvectionProblem::ResultIsNotFinite);
}

TEST_CASE("CfdMassFlux_RefusesANonFiniteProducedFlux", "[cfd][conv]") {
    // The product `rho (U . S)` overflows from finite inputs.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const PreparedCfdBoundary boundary = hand.outlets();
    const std::vector<double> velocity =
        uniformVelocity(hand.topology.cells(), {1.0e300, 0.0, 0.0});
    const CfdMassFluxInput input{velocity, Density::fromSi(1.0e300)};
    std::expected<std::vector<double>, CfdConvectionFailure> flux =
        cfdMassFlux(hand.topology, hand.geometry, boundary, input);
    REQUIRE_FALSE(flux.has_value());
    CHECK(flux.error().problem == CfdConvectionProblem::ResultIsNotFinite);
}

TEST_CASE("CfdConvection_RefusesANonFiniteReportedTotalEvenWhenEveryCoefficientIsFinite",
          "[cfd][conv]") {
    // THE HOLE AN EARLIER DRAFT LEFT OPEN. The finiteness scan covered the
    // coefficients and the source and then published the flow totals
    // unchecked, so an assembly could succeed with `outflow == +inf` while
    // every coefficient was finite. One huge flux per cell keeps each diagonal
    // finite and overflows the mesh total.
    const std::size_t cells = 4;
    const Hand hand = Hand::make(chainPoints(cells), chainTets(cells));
    const PreparedCfdBoundary boundary = hand.outlets();
    std::vector<double> phi(hand.topology.faces(), 0.0);
    std::size_t placed = 0;
    for (std::size_t c = 0; c < cells; ++c) {
        for (std::size_t f = hand.topology.internalFaces(); f < hand.topology.faces(); ++f) {
            if (hand.topology.owner(FaceIndex::fromValue(f)).value() == c && phi[f] == 0.0) {
                phi[f] = 1.0e308;
                ++placed;
                break;
            }
        }
    }
    REQUIRE(placed == cells);

    std::expected<CfdConvectionSystem, CfdConvectionFailure> system = assembleCfdConvection(
        hand.topology, hand.geometry, boundary,
        CfdConvectionInput{phi, CfdConvectionScheme::Upwind, VectorComponent::X});
    REQUIRE_FALSE(system.has_value());
    CHECK(system.error().problem == CfdConvectionProblem::ResultIsNotFinite);
    CHECK_THAT(system.error().error.message, Catch::Matchers::ContainsSubstring("total"));
}

TEST_CASE("CfdConvection_ReportsTheFlowTotalsWithTheDocumentedSigns", "[cfd][conv]") {
    // The three `MassFlowRate` members were read only on a walls fixture where
    // every one is identically zero, so swapping `inflow` and `outflow`, or
    // taking `|phi|` instead of `phi`, survived.
    const Hand hand = Hand::make(chainPoints(4), chainTets(4));
    const PreparedCfdBoundary boundary = hand.outlets();
    std::vector<double> phi(hand.topology.faces(), 0.0);
    double positives = 0.0;
    double negatives = 0.0;
    for (std::size_t f = hand.topology.internalFaces(); f < hand.topology.faces(); ++f) {
        const double value =
            (f % 2 == 0 ? 1.0 : -1.0) * kDiscriminatingConstant * (1.0 + static_cast<double>(f));
        phi[f] = value;
        if (value >= 0.0) {
            positives += value;
        } else {
            negatives += value;
        }
    }
    REQUIRE(positives > 0.0);
    REQUIRE(negatives < 0.0);

    const CfdConvectionSystem system = assemble(hand, boundary, phi);
    CHECK_THAT(system.summary.outflow.si(), Catch::Matchers::WithinRel(positives, 1.0e-13));
    CHECK_THAT(system.summary.inflow.si(), Catch::Matchers::WithinRel(negatives, 1.0e-13));
    CHECK(system.summary.inflow.si() < 0.0);
    CHECK(system.summary.outflow.si() > 0.0);
    // Every facet is ZeroGradient here, so nothing is prescribed.
    CHECK(system.summary.prescribedInflow.si() == 0.0);
    CHECK(system.summary.backflowFaces > 0);
}

TEST_CASE("CfdConvection_TheRowSumIdentityHoldsWhereInflowIsPartlyPrescribed", "[cfd][conv]") {
    // THE CONFIGURATION ADR-072 DECISION 5 EXISTS FOR, which the first draft
    // never built: prescribed and non-prescribed inflow coexisting. Without it
    // the `prescribedInflow` term is identically zero and the all-inflow form
    // the ADR measured departing by a relative 0.9995 is indistinguishable from
    // the correct one.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const std::vector<ElementId> facets = hand.facets();
    REQUIRE(facets.size() >= 4);

    // Half velocity inlets (U FixedValue), half pressure outlets (U
    // ZeroGradient). A pressure outlet fixes the pressure, so no datum.
    std::vector<FacetCondition> assignment;
    std::uint64_t next = 1;
    for (std::size_t b = 0; b < facets.size(); ++b) {
        const CfdBoundaryCondition condition =
            (b % 2 == 0)
                ? CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(next++),
                                       VelocityInlet{someFace(next), Velocity::fromSi(2.5)}}
                : CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(next++),
                                       PressureOutlet{someFace(next), 0_Pa}};
        assignment.push_back(FacetCondition{facets[b], condition});
    }
    const PreparedCfdBoundary boundary = bindPerFacet(hand, assignment, false);

    // Negative on at least one facet of each policy, positive elsewhere.
    std::vector<double> phi(hand.topology.faces(), 0.0);
    for (std::size_t f = 0; f < hand.topology.faces(); ++f) {
        phi[f] = (f < 2 ? -1.0 : 1.0) * kDiscriminatingConstant * (1.0 + static_cast<double>(f));
    }

    const CfdMassFluxDivergenceInput divergenceInput{phi};
    Result<CfdMassFluxDivergenceField> divergence =
        divergeCfdMassFlux(hand.topology, hand.geometry, divergenceInput);
    REQUIRE(divergence.has_value());

    for (const CfdConvectionScheme scheme : kCfdConvectionSchemes) {
        const CfdConvectionSystem system = assemble(hand, boundary, phi, scheme);
        // THE TEST CANNOT SILENTLY DEGENERATE AGAIN: both terms must be live.
        CHECK(system.summary.prescribedInflowFaces > 0);
        CHECK(system.summary.backflowFaces > 0);

        for (std::size_t c = 0; c < hand.topology.cells(); ++c) {
            const double prescribed = prescribedInflowOracle(hand.topology, boundary, phi, c);
            const std::optional<MassFlowRate> net = divergence->at(CellIndex::fromValue(c));
            REQUIRE(net.has_value());
            REQUIRE(prescribed != 0.0);
            REQUIRE(prescribed != net->si());
            INFO("scheme " << toString(scheme) << " cell " << c);
            CHECK_THAT(system.matrix.rowSumSi(c) + prescribed,
                       Catch::Matchers::WithinRel(net->si(), kCfdConvectionRowSumTolerance));
        }
    }
}

TEST_CASE("CfdMassFlux_AtAPressureInletIsComputedFromTheOwnersVelocity", "[cfd][conv]") {
    // The `PressureInlet` row of the producer's table was reached but its value
    // was never inspected, so giving it the walls' zero would have survived.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const PreparedCfdBoundary boundary = hand.uniform(CfdBoundaryCondition{
        CfdBoundaryConditionId::fromValue(1), PressureInlet{someFace(), 0_Pa}});
    // ALL THREE COMPONENTS NON-ZERO, AND THAT IS NOT COSMETIC. Three of a
    // tetrahedron's four facets lie in coordinate planes, so each is blind to
    // two components: with `{k, 0, 0}` two facets have `S_x == 0` and their
    // flux is legitimately zero, and the `phi[f] != 0.0` check below failed on
    // a CORRECT producer. P19-DIV-001 recorded the same geometry fact.
    const Vec velocity{kDiscriminatingConstant, -2.5, 7.25};
    const double rho = 998.0;
    const std::vector<double> phi = produce(hand, boundary, velocity, rho);

    std::size_t nonZero = 0;
    for (std::size_t f = 0; f < hand.topology.faces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        const double expected = rho * dotOf(siOf(hand.geometry.faceAreaVector(face)), velocity);
        INFO("face " << f);
        CHECK_THAT(phi[f], Catch::Matchers::WithinRel(expected, 1.0e-12));
        if (phi[f] != 0.0) {
            ++nonZero;
        }
    }
    // The fixture must discriminate: giving this row the walls' exact zero
    // would have to change an answer.
    CHECK(nonZero == hand.topology.faces());
}

TEST_CASE("CfdConvection_AZeroFluxBoundaryFaceCountsAsOutflowForEitherSignedZero",
          "[cfd][conv]") {
    // The header documents that `phi == 0` counts as outflow because `phi >= 0`
    // is the branch it takes. Nothing asserted it, so `phi >= 0.0` could become
    // `phi > 0.0` and only the COUNTS would change -- which nothing read.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const PreparedCfdBoundary boundary = hand.walls();
    const std::size_t facets = hand.topology.boundaryFaces();

    SECTION("positive zero") {
        const std::vector<double> phi(hand.topology.faces(), 0.0);
        const CfdConvectionSystem system = assemble(hand, boundary, phi);
        CHECK(system.summary.outflowFaces == facets);
        CHECK(system.summary.inflowFaces == 0);
        CHECK(system.summary.prescribedInflowFaces == 0);
        CHECK(system.summary.backflowFaces == 0);
    }

    SECTION("negative zero, which -0.0 >= 0.0 also makes outflow") {
        const std::vector<double> phi(hand.topology.faces(), -0.0);
        REQUIRE(std::signbit(phi[0]));
        const CfdConvectionSystem system = assemble(hand, boundary, phi);
        CHECK(system.summary.outflowFaces == facets);
        CHECK(system.summary.inflowFaces == 0);
        CHECK(system.summary.prescribedInflowFaces == 0);
        CHECK(system.summary.backflowFaces == 0);
    }
}

TEST_CASE("CfdMassFlux_TheWeightVariesAcrossARealMeshAndIsLoadBearing",
          "[cfd][conv][reference]") {
    // THE HAND FIXTURE'S WEIGHT IS EXACTLY 2/3, so `return 2.0/3.0;` survives
    // every assertion made on it. A real body's weights vary, and the fixture
    // is asserted to vary before the claim is made -- which is the half of
    // P19-GRAD-001's r5 fix that an independent oracle alone does not buy.
    const std::vector<Meshable> models = meshableModels();
    REQUIRE_FALSE(models.empty());
    const double rho = 998.0;
    const std::array<double, 9> tensor{0.7, -0.25, 0.4, 0.15, -0.6, 0.35, -0.45, 0.2, 0.55};
    const Vec offset{kDiscriminatingConstant, -2.5, 7.25};

    bool sawVariation = false;
    for (const Meshable& model : models) {
        const Reference reference{model.kind};
        const PreparedCfdBoundary boundary = reference.wallEverywhere();
        const std::vector<double> cellVelocity =
            linearVelocity(*reference.geometry, tensor, offset);

        const CfdMassFluxInput produced{cellVelocity, Density::fromSi(rho)};
        std::expected<std::vector<double>, CfdConvectionFailure> phi =
            cfdMassFlux(*reference.topology, *reference.geometry, boundary, produced);
        INFO(model.name << ": " << (phi.has_value() ? std::string{} : phi.error().error.message));
        REQUIRE(phi.has_value());

        std::size_t away = 0;
        const std::size_t cells = reference.topology->cells();
        for (std::size_t f = 0; f < reference.topology->internalFaces(); ++f) {
            const FaceIndex face = FaceIndex::fromValue(f);
            const double weight = weightOracle(*reference.topology, *reference.geometry, face);
            if (std::abs(weight - 2.0 / 3.0) > 1.0e-6) {
                ++away;
            }
            const std::size_t owner = reference.topology->owner(face).value();
            const std::size_t other = reference.topology->neighbour(face)->value();
            const Vec ownerVelocity{cellVelocity[owner], cellVelocity[cells + owner],
                                    cellVelocity[2 * cells + owner]};
            const Vec otherVelocity{cellVelocity[other], cellVelocity[cells + other],
                                    cellVelocity[2 * cells + other]};
            const Vec stored = siOf(reference.geometry->faceAreaVector(face));
            const double expected =
                rho * dotOf(stored, interpolateOracle(ownerVelocity, otherVelocity, weight));
            INFO(model.name << " internal face " << f);
            CHECK_THAT((*phi)[f], Catch::Matchers::WithinRel(expected, 1.0e-11));
        }
        if (reference.topology->internalFaces() > 0 &&
            away * 2 > reference.topology->internalFaces()) {
            sawVariation = true;
        }
    }
    // At least one body must present weights that are mostly NOT 2/3, or the
    // comparison above could not distinguish a constant weight.
    CHECK(sawVariation);
}
