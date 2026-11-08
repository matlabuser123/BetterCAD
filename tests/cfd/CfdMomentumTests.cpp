/// \file
/// P19-MOM-001 — the momentum assembly, ADR-073.
///
/// WHAT IS CHECKED WHERE, AND WHY THERE.
///
/// ```text
/// the two-cell pair     one internal face, and the ONLY fixture whose two
///                       cell volumes differ (by a factor of two): where the
///                       five-term source order, the pressure term, the body
///                       force and the two reported transient extrema are
///                       pinned against closed-form arithmetic, because a
///                       fixture with equal volumes cannot tell the smallest
///                       transient coefficient from the largest
/// the one-cell tet      no internal face at all, so the matrix is 1x1 and the
///                       boundary treatment plus the three cell-wise terms are
///                       the WHOLE answer. Where the TRANSIENT coefficient is
///                       pinned against closed-form arithmetic, and where a
///                       hand-made inward flux drives the diagonal negative.
///                       (An earlier version of this line said the pressure and
///                       gravity coefficients were pinned here too; they are
///                       pinned on the two-cell pair, and an independent review
///                       checked the attribution)
/// the tet chain         N cells in a line, each sharing ONE face with the
///                       next, so an inner cell has two internal faces and two
///                       boundary ones. It is where the row-sum additivity
///                       identity and the constant-steady-solution residual
///                       are asserted, reached with `outlets()` so that
///                       nothing is prescribed rather than by finding a cell
///                       with no boundary face -- THERE IS NO SUCH CELL IN ANY
///                       FIXTURE HERE, and two earlier comments said the chain
///                       was "the only fixture with an INTERIOR cell" while the
///                       test body that needed one recorded that it had looked
///                       and found none. An independent review found the
///                       contradiction between the comments and the code
/// the reference bodies  the decomposition and the row-sum identity on meshes
///                       nobody designed to flatter the operator
/// ```
///
/// THE INDEPENDENT ORACLE IS THREE THINGS, AND WHAT EACH ONE ESTABLISHES IS
/// NOT THE SAME. The checklist asks for an "independent small-system oracle",
/// and a single test cannot honestly carry that name here, so the claim is
/// split and each part is labelled:
///
/// ```text
/// (a) THE CLOSED-FORM SYSTEM. On the one-cell tet and the two-cell pair,
///     every coefficient is computed in this file from the geometry accessors
///     by the published formulas and asserted. This pins the transient,
///     pressure and gravity terms, which have NO other oracle -- but it takes
///     the two sub-operators' own coefficient formulas AS GIVEN, so for the
///     convection and diffusion parts it is a transcription and not an
///     independent derivation. Saying otherwise would be the exact false claim
///     P19-GRAD-001 recorded as its r5 defect and P19-DIV-001 found again:
///     an oracle transcribed from production, asserted as independent. Those
///     two formulas are qualified, and ADR-069's and ADR-072's own suites
///     validate them against analytic references; this file does not re-do
///     that work and does not claim to
/// (b) THE RESIDUAL TESTS, which are independent of every coefficient formula.
///     A uniform velocity field is a steady solution of the convection and
///     diffusion operators for a conservative flux, whatever their
///     coefficients are, because both row-sum to zero away from a
///     prescribed-value face; and gravity must cancel a hydrostatic pressure
///     gradient whatever either term's arithmetic looks like. These are
///     statements about the OPERATOR, not about its entries
/// (c) THE BIT-EXACT DECOMPOSITION, which is independent of both
///     sub-operators' internals: it asserts only that the momentum matrix IS
///     their slot-wise sum plus the transient diagonal. This is the whole
///     falsifiable content of the checklist's "no hidden stabilization", and
///     it holds whatever the two operators compute
/// ```
///
/// THE FIXTURES ARE A FIFTH COPY, AND THAT IS STATED RATHER THAN HIDDEN.
/// `buildMesh`, `struct Hand`, `orthogonalPoints` and their neighbours also
/// exist in `CfdDiffusionTests.cpp`, `CfdGradientTests.cpp`,
/// `CfdDivergenceTests.cpp` and `CfdConvectionTests.cpp`. Extracting them would
/// edit four qualified suites to serve a fifth, which is the
/// refactor-with-a-feature the working order forbids. What must not be
/// duplicated is an ORACLE, and the one place this file comes close is labelled
/// (a) above.
///
/// WHAT THIS FILE DELIBERATELY DOES NOT ASSERT.
///
/// There is no claim that the matrix is symmetric. Diffusion's part is
/// symmetric to the last bit and convection's is asymmetric by construction,
/// so their sum is asymmetric wherever `phi` is not zero, and the suite asserts
/// the ASYMMETRY instead.
///
/// There is no claim that the row-sum additivity identity is BIT-exact.
/// ADR-073's Q3 measured it over 50,000 random rows before any of this existed:
/// bit-exact on 47.39%, worst `1.085e-14` relative to the row's own scale,
/// because both sides sum the same addends in different orders. A test
/// asserting bit-equality would fail on a correct operator, which is
/// P19-DIV-001's recorded mistake.
///
/// There is no claim that the hydrostatic cancellation is bit-zero. Measured at
/// exactly `0.0` on 65.05% of 200,000 triples, worst `3.649e-16` of the term
/// (ADR-073 Q2), so the bound is relative and `kCfdMomentumHydrostaticTolerance`
/// carries the measurement.
///
/// There is no claim that a large time step reproduces the steady system
/// exactly. The transient term is lost in the diagonal only when it falls below
/// its last bit (ADR-073 Q4), so the steady limit is asserted as a BOUND.
///
/// TWO OF THE SIXTEEN REFUSALS ARE NOT TESTED THROUGH THE PUBLIC API, and
/// AN EARLIER VERSION OF THIS PARAGRAPH NAMED THREE, TWO OF THEM WRONGLY.
/// It listed `GeometryDescribesAnotherTopology` as unreachable "on this path
/// (both are built from one mesh here)", which admits in the same sentence that
/// it was a property of the fixture and not of the API -- the qualified
/// precedent reaches it in three lines, and so now does
/// `CfdMomentum_RefusesMetricsBuiltForAnotherMesh`. It also omitted
/// `BoundaryDescribesAnotherTopology` and `ConvectionRefusedTheInput` from the
/// count entirely while testing neither. An independent review counted every
/// enumerator against the suite and found five unasserted, two of them
/// reachable; the two are tested now, and the consequence had been worse than a
/// missing assertion: DELETING phase 1 or phase 2 from the gate left the suite
/// green, because the fault was re-reported later as
/// `ConvectionRefusedTheInput` -- naming convection for a topology fault, which
/// is the exact diagnostic regression those checks are documented to prevent.
///
/// The two that remain unreached, recorded as such rather than claimed covered
/// -- the treatment P19-BC-001 gave `DegenerateFacet`:
///
/// ```text
/// TopologyNamesAnUnknownCell  `buildCfdTopology` holds, so no face names a
///                             cell outside the mesh
/// ConvectionRefusedTheInput   every input phase of `cfdConvectionProblem` is
///                             pre-empted by an earlier momentum phase --
///                             geometry, boundary, topology, component, length
///                             and finiteness -- and its remaining enumerators
///                             (`DensityNotUsable` on the producer path,
///                             `PrescribedValueMissing`, `PolicyIsAnOutcome`,
///                             `FaceWeightUnavailable`) are themselves
///                             unreachable from a constructible input. Its
///                             OUTPUT refusal is reached and is reported as
///                             `ResultIsNotFinite`, which is tested
/// ```
///
/// `AssemblyIsInconsistent` IS REACHED, through diffusion's untagged channel,
/// and is tested. `BoundaryDescribesAnotherTopology`'s CONDITIONAL arm -- the
/// mesh stamp and the counts, compared only when the boundary carries a
/// non-default source -- is unreached, because it needs a foreign boundary
/// whose face count matches, which no fixture here provides.

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
#include <bettercad/cfd/CfdMomentum.hpp>
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
#include <type_traits>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;

using cfd::Acceleration3;
using cfd::BoundaryFaceIndex;
using cfd::CellIndex;
using cfd::CfdBoundaryCondition;
using cfd::CfdBoundaryConditionKind;
using cfd::CfdBoundaryPolicy;
using cfd::CfdConvectionInput;
using cfd::CfdConvectionScheme;
using cfd::CfdConvectionSystem;
using cfd::CfdDiffusionInput;
using cfd::CfdDiffusionSystem;
using cfd::CfdGeometry;
using cfd::CfdGravity;
using cfd::CfdMassFluxInput;
using cfd::CfdMomentumFailure;
using cfd::CfdMomentumInput;
using cfd::CfdMomentumProblem;
using cfd::CfdMomentumSystem;
using cfd::CfdResolvedTarget;
using cfd::CfdSparseMatrix;
using cfd::CfdTopology;
using cfd::FaceIndex;
using cfd::MassFlowRate;
using cfd::MovingWall;
using cfd::NoSlipWall;
using cfd::PressureGradient3;
using cfd::PressureOutlet;
using cfd::PreparedCfdBoundary;
using cfd::SlipWall;
using cfd::VectorComponent;
using cfd::VelocityInlet;
using meshing::ElementId;
using meshing::NodeId;

namespace {

constexpr auto kRegion = meshing::RegionId::fromValue(1);

/// A CONSTANT NO POWER OF TWO DIVIDES, so a mutation that drops or doubles a
/// factor cannot be hidden by exact binary arithmetic. `0.25` or `17.25` would
/// make every product exact and could not distinguish a correct coefficient
/// from one scaled by a half.
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

/// The orthogonal pair at METRE scale instead of millimetre.
///
/// IT EXISTS FOR ONE TEST AND THE REASON IS ARITHMETIC, not variety. Diffusion's
/// coefficient is `mu * faceArea / orthogonalDistance`, and on a 10 mm tet
/// `faceArea / orthogonalDistance` is about `0.01`, so even the largest finite
/// viscosity -- `1.5e308` -- gives `1.5e306`, which is finite. Overflowing that
/// product needs `faceArea / orthogonalDistance` of order one or more, which
/// needs a mesh of order one metre.
///
/// MY FIRST ATTEMPT AT THAT TEST USED THE MILLIMETRE FIXTURE AND THE ASSEMBLY
/// SUCCEEDED, so the test failed on its own premise rather than on the
/// operator. The independent review that identified the overflow route had
/// stated the condition -- `faceArea/orthogonalDistance > 1.2` -- and the
/// fixture did not meet it. Recorded because the route is real and only the
/// fixture was wrong.
[[nodiscard]] std::vector<Point3D> metreScalePoints() {
    return {Point3D{0_m, 0_m, 0_m},  Point3D{10_m, 0_m, 0_m},  Point3D{0_m, 10_m, 0_m},
            Point3D{0_m, 0_m, 10_m}, Point3D{10_m, 10_m, 10_m}};
}

[[nodiscard]] std::vector<Point3D> oneCellPoints() {
    return {Point3D{0_mm, 0_mm, 0_mm}, Point3D{10_mm, 0_mm, 0_mm}, Point3D{0_mm, 10_mm, 0_mm},
            Point3D{0_mm, 0_mm, 10_mm}};
}
const std::vector<std::array<std::size_t, 4>> kOneCell{{0, 1, 2, 3}};

/// A CHAIN OF @p cells TETRAHEDRA, each sharing exactly one face with the next.
///
/// IT HAS NO FULLY INTERIOR CELL, AND AN EARLIER VERSION OF THIS COMMENT SAID
/// IT WAS THE ONLY FIXTURE THAT DID. Tets `{i, i+1, i+2, i+3}` share one face
/// with their successor and one with their predecessor, so every cell of every
/// chain has at least two boundary faces and no cell's row is the operator's
/// own alone. `CfdMomentum_AdmitsAConstantAsASteadySolutionForAConservativeFlux`
/// found that out the hard way -- its first draft searched for such a cell and
/// its own guard refused to let it pass having checked nothing -- and reaches
/// the property with `outlets()` instead, where `U` is `ZeroGradient` at every
/// boundary face so nothing is prescribed. An independent review found that the
/// comments had not been corrected when the test was.
///
/// WHAT IT IS ACTUALLY FOR: it is the only fixture with a cell that has MORE
/// THAN ONE internal face, which the row-sum additivity identity and the
/// bidiagonal structure both need.
///
/// AND ITS CELLS ARE ALL CONGRUENT, which is a property to be careful of rather
/// than to rely on. The points walk a screw motion -- a 120-degree rotation
/// about z and a translation of `rise` -- which is an isometry, so `Tet_i` maps
/// onto `Tet_{i+1}` and every cell has the same volume to rounding. Any test
/// that needs two DIFFERENT cell volumes must use the two-cell pair, whose
/// volumes differ by a factor of two; an independent review found two summary
/// fields asserted on a chain where they could not be distinguished.
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

    [[nodiscard]] PreparedCfdBoundary walls() const {
        return uniform(
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});
    }

    [[nodiscard]] PreparedCfdBoundary outlets() const {
        return uniform(CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1),
                                            PressureOutlet{someFace(), 0_Pa}});
    }

    [[nodiscard]] PreparedCfdBoundary slips() const {
        return uniform(
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), SlipWall{someFace()}});
    }

    [[nodiscard]] PreparedCfdBoundary inlets(Velocity speed) const {
        return uniform(CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1),
                                            VelocityInlet{someFace(), speed}});
    }
};

// ---------------------------------------------------------------------------
// Inputs
// ---------------------------------------------------------------------------

/// WATER, AND THE SAME WATER THE REFERENCE BODIES CARRY. The reference
/// fixtures assign this density and viscosity to the document's P15 material,
/// and the momentum input is given the same two values, so a reader is never
/// comparing a matrix built from one density against a body made of another.
/// Momentum takes both explicitly, so they would not have to agree -- which is
/// exactly why making them agree is worth a line.
constexpr Density kDensity = 998_kg_per_m3;
constexpr DynamicViscosity kViscosity = 1.002_mPa_s;

[[nodiscard]] std::vector<double> uniformVelocity(std::size_t cells,
                                                  const std::array<double, 3>& value) {
    std::vector<double> out(3 * cells, 0.0);
    for (std::size_t c = 0; c < cells; ++c) {
        for (std::size_t k = 0; k < 3; ++k) {
            out[k * cells + c] = value[k];
        }
    }
    return out;
}

/// A velocity that DIFFERS CELL BY CELL, so a term that reads the wrong array
/// or the wrong cell is visible. `kDiscriminatingConstant` scales it for the
/// reason that constant exists.
[[nodiscard]] std::vector<double> varyingVelocity(std::size_t cells, double scale) {
    std::vector<double> out(3 * cells, 0.0);
    for (std::size_t c = 0; c < cells; ++c) {
        for (std::size_t k = 0; k < 3; ++k) {
            out[k * cells + c] =
                scale * (1.0 + static_cast<double>(c)) * (1.0 + 0.25 * static_cast<double>(k));
        }
    }
    return out;
}

[[nodiscard]] std::vector<double> produceFlux(const Hand& hand, const PreparedCfdBoundary& boundary,
                                             std::span<const double> velocity) {
    const CfdMassFluxInput input{velocity, kDensity};
    std::expected<std::vector<double>, cfd::CfdConvectionFailure> flux =
        cfd::cfdMassFlux(hand.topology, hand.geometry, boundary, input);
    INFO((flux.has_value() ? std::string{} : flux.error().error.message));
    REQUIRE(flux.has_value());
    return std::move(*flux);
}

/// A steady, pressure-free, gravity-free input: the smallest momentum system
/// there is, and the baseline the decomposition is measured against.
[[nodiscard]] CfdMomentumInput steadyInput(std::span<const double> flux,
                                           std::span<const double> currentVelocity) {
    CfdMomentumInput input;
    input.density = kDensity;
    input.viscosity = kViscosity;
    input.component = VectorComponent::X;
    input.faceFlux = flux;
    input.scheme = CfdConvectionScheme::Upwind;
    input.currentVelocity = currentVelocity;
    return input;
}

[[nodiscard]] CfdMomentumSystem assemble(const Hand& hand, const PreparedCfdBoundary& boundary,
                                         const CfdMomentumInput& input) {
    std::expected<CfdMomentumSystem, CfdMomentumFailure> system =
        cfd::assembleCfdMomentum(hand.topology, hand.geometry, boundary, input);
    INFO((system.has_value() ? std::string{} : system.error().error.message));
    REQUIRE(system.has_value());
    return std::move(*system);
}

[[nodiscard]] CfdMomentumFailure refusal(const Hand& hand, const PreparedCfdBoundary& boundary,
                                         const CfdMomentumInput& input) {
    std::expected<CfdMomentumSystem, CfdMomentumFailure> system =
        cfd::assembleCfdMomentum(hand.topology, hand.geometry, boundary, input);
    REQUIRE_FALSE(system.has_value());
    // THE PREDICTORS AGREE, asserted on every refusal rather than in one test
    // of its own, so a tag that the assembly and the predictor disagree about
    // cannot pass.
    const std::optional<CfdMomentumProblem> predicted =
        cfd::cfdMomentumProblem(hand.topology, hand.geometry, boundary, input);
    const std::optional<CfdMomentumFailure> predictedFailure =
        cfd::cfdMomentumFailure(hand.topology, hand.geometry, boundary, input);
    if (system.error().problem != CfdMomentumProblem::ResultIsNotFinite &&
        system.error().problem != CfdMomentumProblem::AssemblyIsInconsistent) {
        REQUIRE(predicted.has_value());
        CHECK(*predicted == system.error().problem);
        REQUIRE(predictedFailure.has_value());
        CHECK(predictedFailure->problem == system.error().problem);
    }
    return system.error();
}

// ---------------------------------------------------------------------------
// The sub-assemblies, for the decomposition
// ---------------------------------------------------------------------------

[[nodiscard]] CfdConvectionSystem convectionOf(const Hand& hand,
                                               const PreparedCfdBoundary& boundary,
                                               const CfdMomentumInput& input) {
    const CfdConvectionInput sub{input.faceFlux, input.scheme, input.component};
    std::expected<CfdConvectionSystem, cfd::CfdConvectionFailure> system =
        cfd::assembleCfdConvection(hand.topology, hand.geometry, boundary, sub);
    INFO((system.has_value() ? std::string{} : system.error().error.message));
    REQUIRE(system.has_value());
    return std::move(*system);
}

[[nodiscard]] CfdDiffusionSystem diffusionOf(const Hand& hand, const PreparedCfdBoundary& boundary,
                                            const CfdMomentumInput& input) {
    const CfdDiffusionInput sub{input.viscosity, input.component, input.currentVelocity,
                                input.faceGradient};
    Result<CfdDiffusionSystem> system =
        cfd::assembleCfdDiffusion(hand.topology, hand.geometry, boundary, sub);
    INFO((system.has_value() ? std::string{} : system.error().message));
    REQUIRE(system.has_value());
    return std::move(*system);
}

/// The transient coefficient of @p cell, RE-DERIVED here from the density, the
/// cell volume and the time step, in the order `rho * V / dt` that the
/// implementation uses.
///
/// THE ORDER IS COPIED DELIBERATELY, and that is the one place in this file
/// where copying the production order is the point rather than a defect: the
/// claim under test is that the matrix is bit-for-bit the declared sum, and a
/// different grouping of the same three factors is a different double (ADR-073
/// Q1 measured 78.06% agreement). An oracle in another order would fail on a
/// correct assembly.
[[nodiscard]] double transientOracle(const CfdGeometry& geometry, std::size_t cell, Density density,
                                     Time timeStep) {
    const double volume = geometry.cellVolume(CellIndex::fromValue(cell)).si();
    return density.si() * volume / timeStep.si();
}

[[nodiscard]] double coeff(const CfdSparseMatrix& matrix, std::size_t row, std::size_t column) {
    return matrix.coeffSi(row, column);
}

/// A copy of @p values with its FIRST entry replaced by a NaN.
///
/// WRITTEN AS A GUARDED LOOP AND NOT AS `out.back() = nan` OR `out[0] = nan`,
/// AND THE REASON IS A RELEASE-ONLY BUILD FAILURE RATHER THAN TASTE. Under
/// `-O3`, once the producer is inlined, GCC's `-Warray-bounds` cannot prove the
/// copied vector is non-empty and reports
///
/// ```text
/// error: array subscript -1 is outside array bounds of
///        'double [1152921504606846975]' [-Werror=array-bounds=]
/// ```
///
/// The project builds Release with `-Werror`, so the `back()` form compiles in
/// `debug-ext` and BREAKS THE BUILD in `release-ext` -- found by building the
/// second preset, which is the only way it could have been found.
/// CLAUDE.md's adversarial review asks "could Debug and Release differ"; this
/// is that.
///
/// THIS COMMENT ONCE CLAIMED THE CLASS WAS CAUGHT BEFORE THE FREEZE, AND THAT
/// CLAIM WAS WRONG. Fixing THIS site did not eliminate the class:
/// `shortenedWithLeadingNan` below carried the same defect, was written after
/// the pre-freeze `release-ext` build, and was never covered by one.
/// P19-MOM-001's first qualification attempt failed on it 66m28s in. A fix at
/// one site is not a fix of a class, and a preset built before the last edit
/// has not been built.
///
/// All three candidate reformulations, the FAILING one included, compile clean
/// in a standalone translation unit under the same flags, so the diagnostic is
/// not a property of the construct and a local reword was not guaranteed to
/// help. A range-for that breaks after one iteration writes exactly one element
/// and runs only when there is one, so there is no subscript for the analysis
/// to bound at all.
///
/// The poisoned entry moves from the last to the FIRST, which is if anything a
/// stronger test: a finiteness scan that checked only the final entry now
/// fails, and one that checked only the first was already caught by the
/// previous placement.
[[nodiscard]] std::vector<double> withPoisonAt(std::span<const double> values, double poison) {
    std::vector<double> out(values.begin(), values.end());
    for (double& entry : out) {
        entry = poison;
        break;
    }
    return out;
}

/// The same, with a NaN, for the sites that want only the one value.
[[nodiscard]] std::vector<double> withLeadingNan(std::span<const double> values) {
    return withPoisonAt(values, std::numeric_limits<double>::quiet_NaN());
}

/// A copy of @p values one entry SHORT and with a leading NaN: both faults at
/// once, for the phase-order assertion that a wrong length is reported before a
/// non-finite value.
///
/// BUILT ONE SHORT RATHER THAN COPIED AND POPPED, and this helper is the reason
/// `withPoisonAt`'s comment above no longer claims the class was caught before
/// the freeze. `out.pop_back()` here was the same defect in the same file, and
/// P19-MOM-001's FIRST QUALIFICATION ATTEMPT FAILED ON THIS LINE:
///
/// ```text
/// inlined from 'shortenedWithLeadingNan' at CfdMomentumTests.cpp:629,
/// inlined from 'CATCH2_INTERNAL_TEST_130()' at CfdMomentumTests.cpp:2236:69:
/// stl_construct.h:88:9: error: array subscript -1 is outside array bounds of
///        'double [1152921504606846975]' [-Werror=array-bounds=]
/// ```
///
/// `pop_back()` destroys `*(end() - 1)`, and at `-O3` with this helper inlined
/// into its caller GCC cannot prove the vector is non-empty, so it bounds that
/// subscript at -1. Copying an EXPLICIT RANGE that is already one short, and
/// empty when @p values is empty, destroys nothing and leaves no subscript for
/// the analysis to bound. The length is computed, not assumed: an empty input
/// yields an empty result rather than wrapping to SIZE_MAX.
[[nodiscard]] std::vector<double> shortenedWithLeadingNan(std::span<const double> values) {
    const std::size_t kept = values.empty() ? std::size_t{0} : values.size() - 1;
    std::vector<double> out(values.begin(),
                            values.begin() + static_cast<std::ptrdiff_t>(kept));
    for (double& entry : out) {
        entry = std::numeric_limits<double>::quiet_NaN();
        break;
    }
    return out;
}

/// The net signed flux out of @p cell: `sum` over its faces of `phi`, positive
/// out of the owner and negative into the neighbour.
///
/// A FIXTURE CONSTRUCTOR, NOT AN ORACLE, and the distinction is the one this
/// file's header draws. It is used to BUILD an exactly conservative `phi` and
/// then to assert that the thing built is conservative -- an assertion about
/// the fixture, not about the operator. No momentum quantity is predicted with
/// it.
[[nodiscard]] double netOutflow(const CfdTopology& topology, std::span<const double> phi,
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

/// `sum |a_ij|` over the row: the scale the row-sum additivity bound divides
/// by, and NOT the row SUM, which nearly cancels (ADR-073 Q3a against Q3b).
///
/// THE ABSOLUTE SUM AND NOT THE MAXIMUM, which is `CfdConvectionTests.cpp`'s
/// choice after a review found it dividing by `largestMagnitude()` over the
/// WHOLE matrix: "the scale a floating-point sum's error belongs to is the
/// magnitude THAT ROW accumulated". The error of summing `n` terms is bounded
/// by `n eps sum|terms|`, so the accumulated magnitude is the right
/// denominator.
///
/// ADR-073's Q3b MEASURED THE DEPARTURE AGAINST `max |a_ij|`, which is the
/// STRICTER denominator of the two, and found a worst `1.085e-14`. Since
/// `sum |a_ij| >= max |a_ij|`, the same absolute error over this larger scale
/// is no greater, so `kCfdMomentumRowSumTolerance` holds here a fortiori. The
/// bound is therefore conservative rather than tight, and that is stated
/// rather than left for a reader to work out.
[[nodiscard]] double rowScale(const CfdSparseMatrix& matrix, std::size_t row) {
    double scale = 0.0;
    const auto first = static_cast<std::size_t>(matrix.rowStart()[row]);
    const auto last = static_cast<std::size_t>(matrix.rowStart()[row + 1]);
    for (std::size_t s = first; s < last; ++s) {
        scale += std::abs(matrix.values()[s]);
    }
    // A FLOOR, SO THAT AN ALL-ZERO ROW GIVES A USABLE BOUND RATHER THAN ZERO.
    // Every bound in this file is written multiplicatively, so a zero scale
    // would turn a tolerance into an exact-equality assertion on a row the
    // operator legitimately left empty -- a one-cell mesh with no faces, or a
    // cell whose every coefficient cancelled. `1e-300` is far below any
    // coefficient a real mesh produces (the smallest here is of order `1e-9`
    // kg/s) and far above `DBL_MIN`, so it can neither mask a real departure
    // nor underflow. An independent review noted it was a bare magic number.
    return std::max(scale, 1.0e-300);
}

void assignWaterTo(Document& document) {
    features::MaterialDefinition water;
    water.designation = "Water";
    water.mechanical.density = materials::MaterialProperty<Density>::known(kDensity);
    water.thermal.dynamicViscosity =
        materials::MaterialProperty<DynamicViscosity>::known(kViscosity);
    const Result<MaterialId> id = features::createMaterial(document, "CfdWater", water);
    REQUIRE(id.has_value());
    REQUIRE(features::assignMaterial(document, *id).has_value());
}

/// One meshed reference body, prepared the way `CfdConvectionTests.cpp`
/// prepares it: the same construction, because these are the same bodies and a
/// second way of building them would be a second thing that could be wrong.
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

    /// Every facet a no-slip wall: a CLOSED body, where `phi` is exactly zero
    /// at every boundary face, so the mesh's own shape is the only thing
    /// varying between bodies.
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

// ---------------------------------------------------------------------------
// The dimensional identities
// ---------------------------------------------------------------------------

TEST_CASE("CfdMomentum_TheDimensionalIdentitiesHoldAtCompileTime", "[cfd][mom]") {
    // AN EARLIER VERSION OF THIS COMMENT CLAIMED THESE WERE "RE-DERIVED, NOT
    // READ FROM THE HEADER'S ALIASES, so a wrong alias in the header cannot
    // make both pass". That was vacuous and an independent review said so: the
    // header DEFINES `CfdTransientCoefficient` as
    // `decltype(Density{} * Volume{} / Time{})`, which is the expression below
    // with the alias inlined, so there is no state in which the two could
    // disagree.
    //
    // WHAT THIS CASE IS ACTUALLY FOR: it NAMES the dimensional claim in the
    // suite, so that a reader of the test list sees it and an evidence file can
    // cite a case rather than a comment. The work is done by the header's own
    // assertions and by the typed arithmetic in `assembleCfdMomentum` that
    // makes them bite; if any were false this file would not compile, which is
    // a stronger failure than a runtime one. The body is deliberately not doing
    // the work, and the inequality at the end is the one line here that the
    // header does not already assert.
    static_assert(std::is_same_v<decltype(Density{} * Volume{} / Time{}), cfd::MassFlowRate>);
    static_assert(
        std::is_same_v<decltype(DynamicViscosity{} * Area{} / Length{}), cfd::MassFlowRate>);
    static_assert(std::is_same_v<decltype(cfd::MassFlowRate{} * Velocity{}), Force>);
    static_assert(std::is_same_v<decltype(Volume{} * cfd::PressureGradient{}), Force>);
    static_assert(std::is_same_v<decltype(Density{} * Volume{} * Acceleration{}), Force>);
    static_assert(std::is_same_v<decltype(Density{} * Volume{} / Time{} * Velocity{}), Force>);
    static_assert(std::is_same_v<decltype(cfd::MassFlowRate{} * Velocity{}),
                                 decltype(Volume{} * cfd::PressureGradient{})>);

    // And the one identity the header does NOT assert, because it is about this
    // suite's own arithmetic rather than the operator's: a mass flow rate is
    // not a force, so a test that compared a coefficient against a source entry
    // would be comparing two different quantities.
    static_assert(!std::is_same_v<cfd::MassFlowRate, Force>);
    SUCCEED("the seven dimensional identities are proved at compile time");
}

// ---------------------------------------------------------------------------
// Shape
// ---------------------------------------------------------------------------

TEST_CASE("CfdMomentum_AssemblesASquareMatrixWhosePatternIsTheCellAdjacency", "[cfd][mom]") {
    const Hand hand = Hand::make(chainPoints(4), chainTets(4));
    const PreparedCfdBoundary boundary = hand.walls();
    const std::vector<double> velocity = uniformVelocity(hand.topology.cells(), {0.0, 0.0, 0.0});
    const std::vector<double> flux = produceFlux(hand, boundary, velocity);
    const CfdMomentumSystem system = assemble(hand, boundary, steadyInput(flux, velocity));

    const std::size_t cells = hand.topology.cells();
    CHECK(system.matrix.rows() == cells);
    CHECK(system.matrix.columns() == cells);
    CHECK(system.source.size() == cells);
    CHECK(system.summary.cells == cells);

    // Every diagonal is present, whether or not a face reaches the cell, and
    // every internal face's two off-diagonals are too.
    for (std::size_t c = 0; c < cells; ++c) {
        INFO("cell " << c);
        CHECK(system.matrix.coeffSi(c, c) != 0.0);
    }
    std::size_t expectedNonZeros = cells;
    for (std::size_t f = 0; f < hand.topology.internalFaces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        if (hand.topology.neighbour(face).has_value()) {
            expectedNonZeros += 2;
        }
    }
    CHECK(system.matrix.nonZeros() == expectedNonZeros);
}

TEST_CASE("CfdMomentum_ReportsWhichTermsWereAppliedAndWhichComponent", "[cfd][mom]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> velocity = uniformVelocity(cells, {0.0, 0.0, 0.0});
    const std::vector<double> flux = produceFlux(hand, boundary, velocity);

    SECTION("the steady form reports no transient, no pressure and no gravity") {
        const CfdMomentumSystem system = assemble(hand, boundary, steadyInput(flux, velocity));
        CHECK_FALSE(system.summary.transientTermApplied);
        CHECK_FALSE(system.summary.pressureGradientApplied);
        CHECK_FALSE(system.summary.gravityApplied);
        CHECK(system.summary.smallestTransientCoefficient.si() == 0.0);
        CHECK(system.summary.largestTransientCoefficient.si() == 0.0);
    }

    SECTION("each component is reported as the one that was assembled") {
        for (const VectorComponent component : cfd::kVectorComponents) {
            CfdMomentumInput input = steadyInput(flux, velocity);
            input.component = component;
            const CfdMomentumSystem system = assemble(hand, boundary, input);
            CHECK(system.summary.component == component);
            CHECK(system.summary.convection.scheme == CfdConvectionScheme::Upwind);
        }
    }

    SECTION("every term applied is reported as applied") {
        const std::vector<double> previous = varyingVelocity(cells, 0.1);
        const std::vector<PressureGradient3> gradient(
            cells, PressureGradient3{cfd::PressureGradient::fromSi(1.0),
                                     cfd::PressureGradient::fromSi(2.0),
                                     cfd::PressureGradient::fromSi(3.0)});
        CfdMomentumInput input = steadyInput(flux, velocity);
        input.timeStep = Time::fromSi(0.01);
        input.previousVelocity = previous;
        input.pressureGradient = gradient;
        input.gravity = CfdGravity{Acceleration3{Acceleration::fromSi(0.0),
                                                 Acceleration::fromSi(0.0),
                                                 Acceleration::fromSi(-9.80665)},
                                   0_m};
        const CfdMomentumSystem system = assemble(hand, boundary, input);
        CHECK(system.summary.transientTermApplied);
        CHECK(system.summary.pressureGradientApplied);
        CHECK(system.summary.gravityApplied);
        CHECK(system.summary.smallestTransientCoefficient.si() > 0.0);
        CHECK(system.summary.largestTransientCoefficient.si() >=
              system.summary.smallestTransientCoefficient.si());
    }
}

// ---------------------------------------------------------------------------
// (c) The decomposition -- the falsifiable form of "no hidden stabilization"
// ---------------------------------------------------------------------------

TEST_CASE("CfdMomentum_TheSteadyMatrixIsExactlyConvectionPlusDiffusionBitForBit", "[cfd][mom]") {
    // THE CENTREPIECE OF THE MILESTONE. If the assembly added an artificial
    // viscosity, a diagonal floor, a limiter or a relaxation factor -- however
    // small -- this assertion fails, because it is an equality of doubles and
    // not a tolerance.
    const Hand hand = Hand::make(chainPoints(5), chainTets(5));
    const PreparedCfdBoundary boundary = hand.inlets(Velocity::fromSi(0.3));
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> velocity = varyingVelocity(cells, 0.2);
    const std::vector<double> flux = produceFlux(hand, boundary, velocity);

    for (const CfdConvectionScheme scheme :
         {CfdConvectionScheme::Upwind, CfdConvectionScheme::Central}) {
        for (const VectorComponent component : cfd::kVectorComponents) {
            CfdMomentumInput input = steadyInput(flux, velocity);
            input.scheme = scheme;
            input.component = component;
            const CfdMomentumSystem system = assemble(hand, boundary, input);
            const CfdConvectionSystem convection = convectionOf(hand, boundary, input);
            const CfdDiffusionSystem diffusion = diffusionOf(hand, boundary, input);

            INFO("scheme " << cfd::toString(scheme) << ", component " << cfd::toString(component));
            REQUIRE(system.matrix.nonZeros() == convection.matrix.nonZeros());
            REQUIRE(system.matrix.nonZeros() == diffusion.matrix.nonZeros());
            // NEITHER SUB-TERM IS IDENTICALLY ZERO ON THIS FIXTURE, asserted so
            // that the equality below is a statement about a sum of two real
            // things. An independent review noted that this test guarded
            // nothing while its neighbours guarded their premises carefully, so
            // a fixture change that zeroed either operator would have left it
            // passing and empty.
            REQUIRE(convection.matrix.largestMagnitude() > 0.0);
            REQUIRE(diffusion.matrix.largestMagnitude() > 0.0);
            bool anySource = false;
            for (std::size_t c = 0; c < cells; ++c) {
                anySource = anySource || convection.source[c] != 0.0 || diffusion.source[c] != 0.0;
            }
            REQUIRE(anySource);
            for (std::size_t s = 0; s < system.matrix.nonZeros(); ++s) {
                INFO("slot " << s);
                // BIT-FOR-BIT, in the declared order.
                CHECK(system.matrix.values()[s] ==
                      convection.matrix.values()[s] + diffusion.matrix.values()[s]);
            }
            for (std::size_t c = 0; c < cells; ++c) {
                INFO("cell " << c);
                CHECK(system.source[c] == convection.source[c] + diffusion.source[c]);
            }
        }
    }
}

TEST_CASE("CfdMomentum_TheTransientDiagonalIsAddedAfterTheSumAndNowhereElse", "[cfd][mom]") {
    const Hand hand = Hand::make(chainPoints(4), chainTets(4));
    const PreparedCfdBoundary boundary = hand.inlets(Velocity::fromSi(0.25));
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> velocity = varyingVelocity(cells, 0.15);
    const std::vector<double> previous = varyingVelocity(cells, 0.37);
    const std::vector<double> flux = produceFlux(hand, boundary, velocity);
    const Time timeStep = Time::fromSi(0.004);

    CfdMomentumInput input = steadyInput(flux, velocity);
    input.timeStep = timeStep;
    input.previousVelocity = previous;
    const CfdMomentumSystem system = assemble(hand, boundary, input);
    const CfdConvectionSystem convection = convectionOf(hand, boundary, input);
    const CfdDiffusionSystem diffusion = diffusionOf(hand, boundary, input);

    for (std::size_t r = 0; r < cells; ++r) {
        const auto first = static_cast<std::size_t>(system.matrix.rowStart()[r]);
        const auto last = static_cast<std::size_t>(system.matrix.rowStart()[r + 1]);
        for (std::size_t s = first; s < last; ++s) {
            const bool isDiagonal = system.matrix.inner()[s] == static_cast<cfd::CfdSparseIndex>(r);
            const double base = convection.matrix.values()[s] + diffusion.matrix.values()[s];
            INFO("row " << r << " slot " << s << (isDiagonal ? " (diagonal)" : " (off-diagonal)"));
            if (isDiagonal) {
                // `(conv + diff) + trans`, in exactly that grouping.
                CHECK(system.matrix.values()[s] ==
                      base + transientOracle(hand.geometry, r, kDensity, timeStep));
            } else {
                // AND NOWHERE ELSE: an off-diagonal receives nothing from the
                // transient term, because backward Euler is a point value in
                // time and couples no neighbour.
                CHECK(system.matrix.values()[s] == base);
            }
        }
    }
}

TEST_CASE("CfdMomentum_TheSourceIsExactlyTheFiveTermsInTheDeclaredOrder", "[cfd][mom]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.inlets(Velocity::fromSi(0.4));
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> velocity = varyingVelocity(cells, 0.2);
    const std::vector<double> previous = varyingVelocity(cells, 0.6);
    const std::vector<double> flux = produceFlux(hand, boundary, velocity);
    const Time timeStep = Time::fromSi(0.002);

    std::vector<PressureGradient3> gradient;
    for (std::size_t c = 0; c < cells; ++c) {
        const double k = kDiscriminatingConstant * (1.0 + static_cast<double>(c));
        gradient.push_back(PressureGradient3{cfd::PressureGradient::fromSi(k),
                                             cfd::PressureGradient::fromSi(-0.5 * k),
                                             cfd::PressureGradient::fromSi(0.25 * k)});
    }
    const Acceleration3 g{Acceleration::fromSi(1.5), Acceleration::fromSi(-9.80665),
                          Acceleration::fromSi(0.75)};

    for (const VectorComponent component : cfd::kVectorComponents) {
        const auto axis = static_cast<std::size_t>(component);
        CfdMomentumInput input = steadyInput(flux, velocity);
        input.component = component;
        input.timeStep = timeStep;
        input.previousVelocity = previous;
        input.pressureGradient = gradient;
        input.gravity = CfdGravity{g, 0_m};

        const CfdMomentumSystem system = assemble(hand, boundary, input);
        const CfdConvectionSystem convection = convectionOf(hand, boundary, input);
        const CfdDiffusionSystem diffusion = diffusionOf(hand, boundary, input);

        for (std::size_t c = 0; c < cells; ++c) {
            const double volume = hand.geometry.cellVolume(CellIndex::fromValue(c)).si();
            const double trans = transientOracle(hand.geometry, c, kDensity, timeStep);
            // THE DECLARED ORDER, reproduced term by term. A different order is
            // a different double on 42.55% of random five-term sums (ADR-073
            // Q5), so this oracle has to walk the same order the header states.
            double expected = convection.source[c];
            expected += diffusion.source[c];
            expected += trans * previous[axis * cells + c];
            expected += -(volume * gradient[c].component(component).si());
            expected += (kDensity.si() * volume) * g.component(component).si();

            INFO("component " << cfd::toString(component) << ", cell " << c);
            CHECK(system.source[c] == expected);
        }
    }
}

TEST_CASE("CfdMomentum_AddsNothingToADiagonalThatBackflowDrivesNegative", "[cfd][mom]") {
    // A DIAGONAL FLOOR WOULD BE INVISIBLE ON A WELL-BEHAVED MESH and visible
    // here. A pressure outlet gives the velocity the ZeroGradient policy, so an
    // INFLOW face there puts a negative `phi` straight onto the diagonal
    // (CfdConvection.hpp's `backflowFaces`), and with no diffusion-side
    // contribution on a zero-gradient face the diagonal can go negative. A
    // `max(a_ii, eps)`, a `std::abs` or an added artificial viscosity would all
    // raise it.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const PreparedCfdBoundary boundary = hand.outlets();
    const std::size_t cells = hand.topology.cells();
    // A velocity pointing INTO the cell through more faces than out of it makes
    // the net boundary flux negative.
    const std::vector<double> velocity = uniformVelocity(cells, {-2.0, -2.0, -2.0});
    const std::vector<double> flux = produceFlux(hand, boundary, velocity);

    const CfdMomentumInput input = steadyInput(flux, velocity);
    const CfdMomentumSystem system = assemble(hand, boundary, input);
    const CfdConvectionSystem convection = convectionOf(hand, boundary, input);
    const CfdDiffusionSystem diffusion = diffusionOf(hand, boundary, input);

    INFO("backflow faces " << system.summary.convection.backflowFaces);
    REQUIRE(system.summary.convection.backflowFaces > 0);
    for (std::size_t c = 0; c < cells; ++c) {
        INFO("cell " << c);
        CHECK(coeff(system.matrix, c, c) ==
              coeff(convection.matrix, c, c) + coeff(diffusion.matrix, c, c));
    }
    // AND THE DOMINANCE IS REPORTED RATHER THAN REPAIRED.
    CHECK(system.summary.rowsNotDiagonallyDominant <= cells);
}

// ---------------------------------------------------------------------------
// (a) The closed-form terms
// ---------------------------------------------------------------------------

TEST_CASE("CfdMomentum_TheTransientDiagonalIsDensityTimesVolumeOverTheTimeStep", "[cfd][mom]") {
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const PreparedCfdBoundary boundary = hand.walls();
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> velocity = uniformVelocity(cells, {0.0, 0.0, 0.0});
    const std::vector<double> previous = uniformVelocity(cells, {0.0, 0.0, 0.0});
    const std::vector<double> flux = produceFlux(hand, boundary, velocity);

    // A WALL GIVES phi == 0 EXACTLY and a FixedValue zero velocity, so the
    // convection matrix is empty off the diagonal and the convection diagonal
    // is zero: the transient term is the only thing that moves when `dt`
    // changes, and halving `dt` must exactly double it.
    const Time coarse = Time::fromSi(0.008);
    const Time fine = Time::fromSi(0.004);

    CfdMomentumInput input = steadyInput(flux, velocity);
    input.timeStep = coarse;
    input.previousVelocity = previous;
    const CfdMomentumSystem slow = assemble(hand, boundary, input);
    input.timeStep = fine;
    const CfdMomentumSystem fast = assemble(hand, boundary, input);
    const CfdDiffusionSystem diffusion = diffusionOf(hand, boundary, input);

    // THE THREE BOUNDS BELOW ARE DERIVED, AND AN EARLIER DRAFT LEFT THEM BARE.
    // CLAUDE.md requires a reason for every tolerance; an independent review
    // found these three were the only numbers in the milestone without one.
    //
    // The transient coefficient is recovered here as a DIFFERENCE of two
    // diagonals, `fl(a_diff + trans) - a_diff`, so it carries the rounding of a
    // sum whose other addend is `a_diff`:
    //
    // ```text
    // |fl(a + t) - a - t|  <=  eps |fl(a + t)|,  so the RELATIVE error in the
    // recovered t is at most  eps (1 + a/t)
    // ```
    //
    // On this fixture `a_diff / trans` is of order one, so the bound is a few
    // `eps`; `kRecovered` is 32 `eps` (about `7.1e-15`), which is an order of
    // magnitude of margin over the derivation rather than a round number
    // chosen to pass. The doubling check compares two such recoveries, so its
    // bound is twice that.
    constexpr double kRecovered = 32.0 * std::numeric_limits<double>::epsilon();
    for (std::size_t c = 0; c < cells; ++c) {
        const double volume = hand.geometry.cellVolume(CellIndex::fromValue(c)).si();
        INFO("cell " << c << ", volume " << volume);
        // The closed form, with the diffusion diagonal subtracted off.
        CHECK_THAT(coeff(slow.matrix, c, c) - coeff(diffusion.matrix, c, c),
                   Catch::Matchers::WithinRel(kDensity.si() * volume / coarse.si(), kRecovered));
        // AND HALVING dt DOUBLES IT, which no constant offset can satisfy.
        const double slowTransient = coeff(slow.matrix, c, c) - coeff(diffusion.matrix, c, c);
        const double fastTransient = coeff(fast.matrix, c, c) - coeff(diffusion.matrix, c, c);
        CHECK_THAT(fastTransient, Catch::Matchers::WithinRel(2.0 * slowTransient, 2.0 * kRecovered));
    }
    // The REPORTED coefficient is not a recovered difference -- it is the
    // product itself -- so it doubles to within a couple of `eps`.
    CHECK_THAT(fast.summary.largestTransientCoefficient.si(),
               Catch::Matchers::WithinRel(2.0 * slow.summary.largestTransientCoefficient.si(),
                                          4.0 * std::numeric_limits<double>::epsilon()));
}

TEST_CASE("CfdMomentum_TheTransientSourceCarriesThePreviousFieldAndNotTheCurrentOne",
          "[cfd][mom]") {
    // THE ONE TEST THAT CAN SEE THIS, and the reason it exists. Both velocity
    // arrays are `std::span<const double>` of length `3 * cells` in the same
    // layout, so the type system cannot tell them apart. Passing the CURRENT
    // field where the previous one belongs leaves the diagonal carrying
    // `rho V / dt` and the source carrying `rho V / dt * U_current`, which is
    // the steady system perturbed toward its own current state -- it CONVERGES
    // TO THE CORRECT STEADY ANSWER and passes every steady assertion in this
    // file (ADR-073 Decision 4).
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> current = varyingVelocity(cells, 0.31);
    const std::vector<double> previous = varyingVelocity(cells, 0.73);
    const std::vector<double> flux = produceFlux(hand, boundary, current);
    const Time timeStep = Time::fromSi(0.005);

    // THE TWO FIELDS MUST ACTUALLY DIFFER, or the test proves nothing.
    REQUIRE(current != previous);

    CfdMomentumInput input = steadyInput(flux, current);
    input.timeStep = timeStep;
    input.previousVelocity = previous;
    const CfdMomentumSystem system = assemble(hand, boundary, input);
    const CfdConvectionSystem convection = convectionOf(hand, boundary, input);
    const CfdDiffusionSystem diffusion = diffusionOf(hand, boundary, input);

    for (std::size_t c = 0; c < cells; ++c) {
        const double trans = transientOracle(hand.geometry, c, kDensity, timeStep);
        const double base = convection.source[c] + diffusion.source[c];
        const double withPrevious = base + trans * previous[c];
        const double withCurrent = base + trans * current[c];
        INFO("cell " << c);
        CHECK(system.source[c] == withPrevious);
        // AND THE TWO ARE DISTINGUISHABLE, so the assertion above is not
        // vacuously true on this fixture.
        CHECK(withPrevious != withCurrent);
    }
}

TEST_CASE("CfdMomentum_ThePressureTermIsMinusVolumeTimesTheGradientComponent", "[cfd][mom]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> velocity = uniformVelocity(cells, {0.0, 0.0, 0.0});
    const std::vector<double> flux = produceFlux(hand, boundary, velocity);

    std::vector<PressureGradient3> gradient;
    for (std::size_t c = 0; c < cells; ++c) {
        const double k = kDiscriminatingConstant * (1.0 + static_cast<double>(c));
        gradient.push_back(PressureGradient3{cfd::PressureGradient::fromSi(k),
                                             cfd::PressureGradient::fromSi(2.0 * k),
                                             cfd::PressureGradient::fromSi(-3.0 * k)});
    }

    const CfdMomentumInput bare = steadyInput(flux, velocity);
    const CfdMomentumSystem without = assemble(hand, boundary, bare);

    for (const VectorComponent component : cfd::kVectorComponents) {
        CfdMomentumInput input = bare;
        input.component = component;
        input.pressureGradient = gradient;
        const CfdMomentumSystem with = assemble(hand, boundary, input);
        CfdMomentumInput bareComponent = bare;
        bareComponent.component = component;
        const CfdMomentumSystem baseline = assemble(hand, boundary, bareComponent);

        for (std::size_t c = 0; c < cells; ++c) {
            const double volume = hand.geometry.cellVolume(CellIndex::fromValue(c)).si();
            const double expected = -(volume * gradient[c].component(component).si());
            INFO("component " << cfd::toString(component) << ", cell " << c);
            CHECK(with.source[c] == baseline.source[c] + expected);
            // THE SIGN IS THE POINT: a positive gradient drives the flow the
            // other way, so the term must be NEGATIVE where the gradient is
            // positive.
            CHECK((expected < 0.0) == (gradient[c].component(component).si() > 0.0));
        }
        // The matrix is untouched by the pressure term: it is a source.
        CHECK(with.matrix == baseline.matrix);
    }
    CHECK_FALSE(without.summary.pressureGradientApplied);
}

TEST_CASE("CfdMomentum_TheBodyForceIsDensityTimesVolumeTimesGravity", "[cfd][mom]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> velocity = uniformVelocity(cells, {0.0, 0.0, 0.0});
    const std::vector<double> flux = produceFlux(hand, boundary, velocity);
    const Acceleration3 g{Acceleration::fromSi(0.0), Acceleration::fromSi(0.0),
                          Acceleration::fromSi(-9.80665)};

    for (const VectorComponent component : cfd::kVectorComponents) {
        CfdMomentumInput baseline = steadyInput(flux, velocity);
        baseline.component = component;
        const CfdMomentumSystem without = assemble(hand, boundary, baseline);

        CfdMomentumInput input = baseline;
        input.gravity = CfdGravity{g, 0_m};
        const CfdMomentumSystem with = assemble(hand, boundary, input);

        for (std::size_t c = 0; c < cells; ++c) {
            const double volume = hand.geometry.cellVolume(CellIndex::fromValue(c)).si();
            const double expected = (kDensity.si() * volume) * g.component(component).si();
            INFO("component " << cfd::toString(component) << ", cell " << c);
            CHECK(with.source[c] == without.source[c] + expected);
        }
        CHECK(with.matrix == without.matrix);
    }
}

TEST_CASE("CfdMomentum_TheReferenceHeightOfGravityChangesNothingHere", "[cfd][mom]") {
    // `CfdGravity::referenceHeight` BELONGS TO PRESCRIBED BOUNDARY PRESSURES,
    // not to the body force: ADR-058 puts the hydrostatic part inside `p` and
    // P19-BC-001 already varies a prescribed pressure along a face as
    // `p_b - rho |g| (h - h_ref)`. Reading it again here would apply the
    // hydrostatic field twice, so the assembly must be INDIFFERENT to it
    // (ADR-073 Decision 6).
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> velocity = uniformVelocity(cells, {0.0, 0.0, 0.0});
    const std::vector<double> flux = produceFlux(hand, boundary, velocity);
    const Acceleration3 g{Acceleration::fromSi(0.0), Acceleration::fromSi(0.0),
                          Acceleration::fromSi(-9.80665)};

    CfdMomentumInput input = steadyInput(flux, velocity);
    input.gravity = CfdGravity{g, 0_m};
    const CfdMomentumSystem atOrigin = assemble(hand, boundary, input);
    input.gravity = CfdGravity{g, Length::fromSi(kDiscriminatingConstant)};
    const CfdMomentumSystem raised = assemble(hand, boundary, input);

    CHECK(atOrigin.matrix == raised.matrix);
    CHECK(atOrigin.source == raised.source);
    CHECK(atOrigin.summary == raised.summary);
}

TEST_CASE("CfdMomentum_OmittingATermOmitsItExactlyAndSaysSo", "[cfd][mom]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> velocity = uniformVelocity(cells, {0.0, 0.0, 0.0});
    const std::vector<double> flux = produceFlux(hand, boundary, velocity);

    // BOTH TERMS ARE APPLIED IN `loaded`, AND THE SECTIONS BELOW CLEAR THEM.
    // An earlier draft compared `bare` against `bare` with the same two fields
    // already empty -- so both sections assembled the identical input twice and
    // compared it with itself, and no production change of any kind could make
    // them fail. An independent review found it.
    const std::vector<PressureGradient3> gradient(
        cells, PressureGradient3{cfd::PressureGradient::fromSi(kDiscriminatingConstant),
                                 cfd::PressureGradient::fromSi(-kDiscriminatingConstant),
                                 cfd::PressureGradient::fromSi(0.5 * kDiscriminatingConstant)});
    const CfdGravity gravity{Acceleration3{Acceleration::fromSi(1.5), Acceleration::fromSi(-9.80665),
                                           Acceleration::fromSi(0.75)},
                             0_m};
    const CfdMomentumInput bare = steadyInput(flux, velocity);
    const CfdMomentumSystem baseline = assemble(hand, boundary, bare);
    CfdMomentumInput loadedInput = bare;
    loadedInput.pressureGradient = gradient;
    loadedInput.gravity = gravity;
    const CfdMomentumSystem loaded = assemble(hand, boundary, loadedInput);
    // The premise: the two terms actually moved the source, so clearing them is
    // a change and not a no-op.
    REQUIRE(loaded.source != baseline.source);

    SECTION("clearing the pressure gradient removes exactly that term") {
        CfdMomentumInput input = loadedInput;
        input.pressureGradient = std::span<const PressureGradient3>{};
        const CfdMomentumSystem system = assemble(hand, boundary, input);
        CHECK_FALSE(system.summary.pressureGradientApplied);
        for (std::size_t c = 0; c < cells; ++c) {
            const double volume = hand.geometry.cellVolume(CellIndex::fromValue(c)).si();
            const double removed = -(volume * gradient[c].component(bare.component).si());
            INFO("cell " << c);
            CHECK(system.source[c] != loaded.source[c]);
            CHECK_THAT(system.source[c],
                       Catch::Matchers::WithinRel(loaded.source[c] - removed, 1.0e-13));
        }
    }

    SECTION("clearing the gravity record removes exactly that term") {
        CfdMomentumInput input = loadedInput;
        input.gravity = std::nullopt;
        const CfdMomentumSystem system = assemble(hand, boundary, input);
        CHECK_FALSE(system.summary.gravityApplied);
        for (std::size_t c = 0; c < cells; ++c) {
            const double volume = hand.geometry.cellVolume(CellIndex::fromValue(c)).si();
            const double removed =
                (kDensity.si() * volume) * gravity.acceleration.component(bare.component).si();
            INFO("cell " << c);
            CHECK(system.source[c] != loaded.source[c]);
            CHECK_THAT(system.source[c],
                       Catch::Matchers::WithinRel(loaded.source[c] - removed, 1.0e-13));
        }
    }

    SECTION("a zero pressure gradient is NOT the same thing as an absent one") {
        // The one section of this test that was ever non-vacuous.
        // It contributes `-(V * 0.0)`, which is `-0.0`, and `x + -0.0 == x`, so
        // the SOURCE agrees -- but the summary must still distinguish them,
        // because one is a caller who supplied a field and one is a caller who
        // did not.
        const std::vector<PressureGradient3> zero(cells, PressureGradient3{});
        CfdMomentumInput input = bare;
        input.pressureGradient = zero;
        const CfdMomentumSystem system = assemble(hand, boundary, input);
        CHECK(system.summary.pressureGradientApplied);
        CHECK_FALSE(baseline.summary.pressureGradientApplied);
        CHECK(system.source == baseline.source);
    }
}

// ---------------------------------------------------------------------------
// (b) The residual tests
// ---------------------------------------------------------------------------
//
// THE BANNER USED TO SAY "independent of every coefficient formula" OF ALL FOUR
// TESTS BELOW, AND IT IS TRUE OF TWO. An independent review checked the
// membership. `GravityAndAHydrostaticPressureGradientCancel` and
// `AdmitsAConstantAsASteadySolutionForAConservativeFlux` are independent: each
// is a statement about the operator that holds whatever its coefficients are.
// `RowSumIsTheSumOfTheTermsOwnRowSums` uses `transientOracle`, which copies
// production's `rho * V / dt` deliberately, and
// `ALargeTimeStepApproachesTheSteadySystem...` derives its bound in the test
// but compares against the operator's own matrix -- so both belong to (a)'s
// family, and the two sign tests above are where the independence actually
// lives.
//
// Saying it of all four was the same shape of claim this file's own header
// warns about: a coverage sentence that an evidence file would then cite.

TEST_CASE("CfdMomentum_GravityAndAHydrostaticPressureGradientCancel", "[cfd][mom]") {
    // A FLUID AT REST STAYS AT REST. With a pressure field whose gradient is
    // exactly `rho g`, the pressure term and the body force must cancel and the
    // momentum source must carry nothing from either.
    //
    // THIS IS INDEPENDENT OF BOTH TERMS' ARITHMETIC: it tests the two SIGNS
    // against each other, which no single-term test does. A sign error in
    // either one turns the cancellation into a doubling.
    //
    // NOT BIT-ZERO, AND ADR-073's Q2 SAYS WHY: the two terms are the same
    // product in different groupings, exactly `0.0` on 65.05% of 200,000
    // random triples with a worst residual of `3.649e-16` of the term. The
    // bound is `kCfdMomentumHydrostaticTolerance`.
    const Hand hand = Hand::make(chainPoints(4), chainTets(4));
    const std::size_t cells = hand.topology.cells();

    // GRAVITY HAS THREE NON-ZERO COMPONENTS, AND AN EARLIER DRAFT USED
    // `(0, 0, -9.80665)`. With gravity on one axis the X and Y iterations had
    // `term == 0` and both addends exactly +/-0.0, so two of the three
    // components asserted `residual == 0.0` on a pair of zeros and carried no
    // content -- an independent review counted them.
    const Acceleration3 g{Acceleration::fromSi(1.5), Acceleration::fromSi(-9.80665),
                          Acceleration::fromSi(0.75)};

    // grad p == rho g, which is hydrostatic equilibrium.
    const std::vector<PressureGradient3> hydrostatic(
        cells, PressureGradient3{cfd::PressureGradient::fromSi(kDensity.si() * g.x.si()),
                                 cfd::PressureGradient::fromSi(kDensity.si() * g.y.si()),
                                 cfd::PressureGradient::fromSi(kDensity.si() * g.z.si())});

    // AND THE CANCELLATION IS CHECKED ON TWO BASES, which the same review asked
    // for: `walls()` with a still fluid, where the convection and diffusion
    // sources are exactly zero so the residual is `fl(p + g)` and nothing else
    // -- that is ADR-073's Q2 reproduced faithfully -- and `inlets()` with a
    // moving fluid, where they are not, which is the only way the cancellation
    // is ever actually used.
    struct Base {
        std::string_view name;
        PreparedCfdBoundary boundary;
        std::vector<double> velocity;
        bool sourceIsZero;
    };
    std::vector<Base> bases;
    bases.push_back(Base{"a closed wall, still fluid", hand.walls(),
                         uniformVelocity(cells, {0.0, 0.0, 0.0}), true});
    bases.push_back(Base{"velocity inlets, moving fluid", hand.inlets(Velocity::fromSi(0.3)),
                         varyingVelocity(cells, 0.2), false});

    for (const Base& base : bases) {
        const std::vector<double> flux = produceFlux(hand, base.boundary, base.velocity);
        for (const VectorComponent component : cfd::kVectorComponents) {
            CfdMomentumInput baseline = steadyInput(flux, base.velocity);
            baseline.component = component;
            const CfdMomentumSystem without = assemble(hand, base.boundary, baseline);

            CfdMomentumInput input = baseline;
            input.pressureGradient = hydrostatic;
            input.gravity = CfdGravity{g, 0_m};
            const CfdMomentumSystem with = assemble(hand, base.boundary, input);

            // The premise of the second base, asserted rather than assumed.
            if (!base.sourceIsZero) {
                bool anyNonZero = false;
                for (std::size_t c = 0; c < cells; ++c) {
                    anyNonZero = anyNonZero || without.source[c] != 0.0;
                }
                INFO(base.name);
                CHECK(anyNonZero);
            }

            for (std::size_t c = 0; c < cells; ++c) {
                const double volume = hand.geometry.cellVolume(CellIndex::fromValue(c)).si();
                const double term =
                    std::abs((kDensity.si() * volume) * g.component(component).si());
                const double residual = with.source[c] - without.source[c];
                // THE BOUND IS RELATIVE TO THE TERM, and on the non-zero base
                // also to the base it was added to, because recovering a
                // difference from a sum carries that sum's rounding.
                const double scale =
                    std::max(term, cfd::kCfdMomentumHydrostaticTolerance == 0.0
                                       ? 0.0
                                       : std::abs(without.source[c]));
                INFO(base.name << ", component " << cfd::toString(component) << ", cell " << c
                               << ", term " << term << ", base " << without.source[c]);
                REQUIRE(term > 0.0);
                CHECK(std::abs(residual) <= cfd::kCfdMomentumHydrostaticTolerance * scale);
            }
        }
    }
}

TEST_CASE("CfdMomentum_AdmitsAConstantAsASteadySolutionForAConservativeFlux", "[cfd][mom]") {
    // THE OPERATOR'S NULL PROPERTY, independent of every coefficient formula.
    // For a conservative `phi` and a boundary that prescribes NO value, the
    // constant vector must satisfy `A x = b` with `b` exactly zero: convection
    // because its coefficients sum to `phi_f` at every face and the face fluxes
    // sum to zero over a cell, diffusion because its diagonal is the sum of the
    // same coefficients its off-diagonals negate and a ZeroGradient face
    // contributes neither. So `A 1 = 0`, which needs no solve.
    //
    // A FIRST DRAFT ASKED FOR A CELL WITH NO BOUNDARY FACE AT ALL and found
    // none: in a chain of tets built as {i, i+1, i+2, i+3} every cell shares at
    // most two of its four faces, so EVERY cell has a boundary face and the
    // test was vacuous. Its own `REQUIRE(checked > 0)` guard caught that rather
    // than letting it pass having checked nothing -- which is the whole reason
    // the guard was written. The property is reached here the way
    // `CfdConvectionTests.cpp` reaches it instead: pressure outlets everywhere,
    // so `U` is ZeroGradient and nothing is prescribed, and a `phi` made
    // conservative BY CONSTRUCTION.
    const std::size_t cells = 5;
    const Hand hand = Hand::make(chainPoints(cells), chainTets(cells));
    const PreparedCfdBoundary boundary = hand.outlets();

    // A CONSERVATIVE `phi` WITHOUT ASSUMING THE WINDING: a uniform flux through
    // every internal face, then one boundary face per cell carrying the exact
    // negation of whatever net outflow that cell ended up with. `d + (-d)` is
    // exactly zero for a single double, so the construction is bit-exact
    // whichever way each face happens to be wound.
    std::vector<double> phi(hand.topology.faces(), 0.0);
    for (std::size_t f = 0; f < hand.topology.internalFaces(); ++f) {
        phi[f] = kDiscriminatingConstant;
    }
    for (std::size_t c = 0; c < cells; ++c) {
        const double net = netOutflow(hand.topology, phi, c);
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
    // THE CONSTRUCTION IS ASSERTED, NOT ASSUMED.
    for (std::size_t c = 0; c < cells; ++c) {
        INFO("cell " << c);
        CHECK(netOutflow(hand.topology, phi, c) == 0.0);
    }

    const std::vector<double> velocity = uniformVelocity(cells, {0.0, 0.0, 0.0});
    for (const CfdConvectionScheme scheme :
         {CfdConvectionScheme::Upwind, CfdConvectionScheme::Central}) {
        CfdMomentumInput input = steadyInput(phi, velocity);
        input.scheme = scheme;
        const CfdMomentumSystem system = assemble(hand, boundary, input);

        // No facet prescribes a value, and there is no pressure term and no
        // gravity, so the source is EXACTLY zero -- not merely small.
        for (std::size_t c = 0; c < cells; ++c) {
            INFO("scheme " << cfd::toString(scheme) << " cell " << c);
            CHECK(system.source[c] == 0.0);
        }
        CHECK(system.summary.convection.prescribedInflowFaces == 0);

        const std::vector<double> constant(cells, kDiscriminatingConstant);
        Result<std::vector<double>> residual = system.matrix.multiply(constant);
        REQUIRE(residual.has_value());
        for (std::size_t c = 0; c < cells; ++c) {
            // PER-ROW SCALE, which is the review finding CfdConvectionTests.cpp
            // records: a scale taken over the whole matrix admits an arbitrarily
            // large relative error in a small-magnitude row.
            const double scale = kDiscriminatingConstant * rowScale(system.matrix, c);
            INFO("scheme " << cfd::toString(scheme) << " cell " << c << ", scale " << scale);
            CHECK(std::abs((*residual)[c]) <= cfd::kCfdMomentumRowSumTolerance * scale);
        }
    }
}

TEST_CASE("CfdMomentum_RowSumIsTheSumOfTheTermsOwnRowSums", "[cfd][mom]") {
    // ROW-SUM ADDITIVITY, to `kCfdMomentumRowSumTolerance` RELATIVE TO THE ROW
    // SCALE and not to the sum. ADR-073's Q3 measured both: relative to the sum
    // the worst departure is `5.264e-12`, relative to the row's own
    // `max |a_ij|` it is `1.085e-14` -- a factor of 500, all of it the
    // cancellation of a nearly-null row.
    const Hand hand = Hand::make(chainPoints(5), chainTets(5));
    const PreparedCfdBoundary boundary = hand.inlets(Velocity::fromSi(0.2));
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> velocity = varyingVelocity(cells, 0.18);
    const std::vector<double> previous = varyingVelocity(cells, 0.44);
    const std::vector<double> flux = produceFlux(hand, boundary, velocity);
    const Time timeStep = Time::fromSi(0.003);

    CfdMomentumInput input = steadyInput(flux, velocity);
    input.timeStep = timeStep;
    input.previousVelocity = previous;
    const CfdMomentumSystem system = assemble(hand, boundary, input);
    const CfdConvectionSystem convection = convectionOf(hand, boundary, input);
    const CfdDiffusionSystem diffusion = diffusionOf(hand, boundary, input);

    for (std::size_t c = 0; c < cells; ++c) {
        const double termwise = convection.matrix.rowSumSi(c) + diffusion.matrix.rowSumSi(c) +
                                transientOracle(hand.geometry, c, kDensity, timeStep);
        const double combined = system.matrix.rowSumSi(c);
        const double scale = rowScale(system.matrix, c);
        INFO("cell " << c << ": combined " << combined << ", termwise " << termwise << ", scale "
                     << scale);
        CHECK(std::abs(combined - termwise) <= cfd::kCfdMomentumRowSumTolerance * scale);
    }
}

TEST_CASE("CfdMomentum_ALargeTimeStepApproachesTheSteadySystemWithinTheTransientBound",
          "[cfd][mom]") {
    // A BOUND AND NOT AN EQUALITY. The transient term vanishes into the diagonal
    // only when it falls below its last bit (ADR-073 Q4 measured the threshold
    // ratio at `1.087e-16`).
    //
    // AND THE FIRST FORM OF THAT BOUND WAS WRONG, WHICH THIS TEST FOUND. It
    // asserted `gap <= max_c rho V_c / dt`, which is the bound in EXACT
    // arithmetic and is not the bound in floating point. The gap is recovered as
    // a difference of two diagonals, and the transient diagonal is
    // `fl(a_steady + trans)` -- so recovering `trans` from it carries the
    // rounding of a sum whose larger addend is `a_steady`:
    //
    // ```text
    // |fl(a + t) - a|  <=  t + eps |fl(a + t)|
    // ```
    //
    // At `dt = 1e9` the measured gap was `1.29646e-12` against a claimed bound
    // of `1.29644e-12` -- larger by `2e-17`, which is `eps` times the diagonal's
    // own magnitude, exactly as that inequality predicts. The term is added
    // here because it is a REAL part of the bound and not slack: it is
    // quantified, it is proportional to the diagonal rather than chosen, and
    // with `trans` at `1e-12` beside a diagonal near `1e-1` it is not optional.
    // Dropping it and loosening the relative factor instead would have been the
    // tolerance-to-pass move this project refuses.
    constexpr double kRecovery = 2.0 * std::numeric_limits<double>::epsilon();
    const Hand hand = Hand::make(chainPoints(4), chainTets(4));
    const PreparedCfdBoundary boundary = hand.inlets(Velocity::fromSi(0.3));
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> velocity = varyingVelocity(cells, 0.22);
    const std::vector<double> previous = varyingVelocity(cells, 0.22);
    const std::vector<double> flux = produceFlux(hand, boundary, velocity);

    const CfdMomentumInput steady = steadyInput(flux, velocity);
    const CfdMomentumSystem reference = assemble(hand, boundary, steady);

    double previousGap = std::numeric_limits<double>::infinity();
    for (const double dt : {1.0e0, 1.0e3, 1.0e6, 1.0e9}) {
        CfdMomentumInput input = steady;
        input.timeStep = Time::fromSi(dt);
        input.previousVelocity = previous;
        const CfdMomentumSystem system = assemble(hand, boundary, input);

        double gap = 0.0;
        double largestDiagonal = 0.0;
        for (std::size_t c = 0; c < cells; ++c) {
            gap = std::max(gap, std::abs(coeff(system.matrix, c, c) -
                                         coeff(reference.matrix, c, c)));
            largestDiagonal = std::max(largestDiagonal, std::abs(coeff(system.matrix, c, c)));
        }
        // THE BOUND IS DERIVED IN THE TEST, NOT READ FROM THE SUMMARY. An
        // earlier draft used `system.summary.largestTransientCoefficient`,
        // which the operator computes from the same `rho V / dt` that produced
        // the diagonal the gap is measured from -- so scaling the transient
        // coefficient by any constant scaled BOTH sides and the test passed for
        // every scale factor. An independent review found that it could not
        // fail for a magnitude error, only for a constant offset or
        // non-monotone behaviour.
        double derivedTransient = 0.0;
        for (std::size_t c = 0; c < cells; ++c) {
            derivedTransient = std::max(derivedTransient,
                                        kDensity.si() *
                                            hand.geometry.cellVolume(CellIndex::fromValue(c)).si() /
                                            dt);
        }
        // And the operator's own report agrees with the derivation, which is a
        // separate claim and is made separately.
        CHECK_THAT(system.summary.largestTransientCoefficient.si(),
                   Catch::Matchers::WithinRel(derivedTransient, 1.0e-14));
        const double bound = derivedTransient + kRecovery * largestDiagonal;
        INFO("dt " << dt << ", gap " << gap << ", transient "
                   << system.summary.largestTransientCoefficient.si() << ", recovery term "
                   << kRecovery * largestDiagonal << ", bound " << bound);
        CHECK(gap <= bound);
        // AND IT SHRINKS MONOTONICALLY, which a constant offset would not.
        CHECK(gap < previousGap);
        previousGap = gap;
    }
}

// ---------------------------------------------------------------------------
// Asymmetry and dominance
// ---------------------------------------------------------------------------

TEST_CASE("CfdMomentum_TheMatrixIsAsymmetricWhereverTheFluxIsNotZero", "[cfd][mom]") {
    // DIFFUSION'S PART IS SYMMETRIC TO THE LAST BIT AND CONVECTION'S IS
    // ASYMMETRIC BY CONSTRUCTION, so their sum is asymmetric wherever `phi` is
    // not zero. A momentum operator that came back symmetric would have lost
    // its convection term, and `fromCsr` is given no symmetry tolerance for
    // exactly this reason.
    const Hand hand = Hand::make(chainPoints(4), chainTets(4));
    const PreparedCfdBoundary boundary = hand.inlets(Velocity::fromSi(0.5));
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> velocity = varyingVelocity(cells, 0.4);
    const std::vector<double> flux = produceFlux(hand, boundary, velocity);

    const CfdMomentumSystem system = assemble(hand, boundary, steadyInput(flux, velocity));
    CHECK(system.matrix.largestSymmetryError() > 0.0);

    // AND A WALLED MESH, WHOSE phi IS BIT-ZERO EVERYWHERE, IS SYMMETRIC --
    // which is what makes the assertion above a statement about convection
    // rather than about this fixture.
    const PreparedCfdBoundary walls = hand.walls();
    const std::vector<double> still = uniformVelocity(cells, {0.0, 0.0, 0.0});
    const std::vector<double> zeroFlux = produceFlux(hand, walls, still);
    REQUIRE(std::ranges::all_of(zeroFlux, [](double v) { return v == 0.0; }));
    const CfdMomentumSystem quiet = assemble(hand, walls, steadyInput(zeroFlux, still));
    CHECK(quiet.matrix.largestSymmetryError() == 0.0);
}

TEST_CASE("CfdMomentum_ReportsDominanceMeasuredFromTheMatrixItReturns", "[cfd][mom]") {
    const Hand hand = Hand::make(chainPoints(5), chainTets(5));
    const PreparedCfdBoundary boundary = hand.inlets(Velocity::fromSi(0.35));
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> velocity = varyingVelocity(cells, 0.3);
    const std::vector<double> flux = produceFlux(hand, boundary, velocity);
    const CfdMomentumSystem system = assemble(hand, boundary, steadyInput(flux, velocity));

    // RE-COUNTED HERE FROM THE RETURNED MATRIX, AND THIS IS A TRANSCRIPTION OF
    // PRODUCTION'S PREDICATE -- the same loop, the same `std::abs`, the same
    // `diagonal < off` comparison. An independent review said so, correctly,
    // and the recurring defect it names is real: `P19-GRAD-001`'s r5 and
    // `P19-DIV-001` both shipped an oracle transcribed from production and
    // called it independent.
    //
    // IT IS KEPT, AND WHAT IT CLAIMS IS NARROWED TO WHAT IT ESTABLISHES: that
    // the REPORTED number describes the RETURNED matrix. A summary field that
    // disagreed with the matrix beside it is a real failure mode and nothing
    // else in the suite watches for it. The count's MEANING is pinned
    // separately, by
    // `CfdMomentum_CountsEveryNonDominantRowOnAFixtureBuiltToHaveThem`, where
    // the expectation is derived from the row sum on a fixture that has
    // non-dominant rows by construction -- so a `notDominant = 0` mutation
    // fails there rather than relying on this test's fixture happening to have
    // one.
    std::size_t counted = 0;
    for (std::size_t r = 0; r < cells; ++r) {
        double diagonal = 0.0;
        double off = 0.0;
        const auto first = static_cast<std::size_t>(system.matrix.rowStart()[r]);
        const auto last = static_cast<std::size_t>(system.matrix.rowStart()[r + 1]);
        for (std::size_t s = first; s < last; ++s) {
            if (system.matrix.inner()[s] == static_cast<cfd::CfdSparseIndex>(r)) {
                diagonal = std::abs(system.matrix.values()[s]);
            } else {
                off += std::abs(system.matrix.values()[s]);
            }
        }
        if (diagonal < off) {
            ++counted;
        }
    }
    CHECK(system.summary.rowsNotDiagonallyDominant == counted);
}

TEST_CASE("CfdMomentum_CarriesTheSubSummariesVerbatim", "[cfd][mom]") {
    const Hand hand = Hand::make(chainPoints(4), chainTets(4));
    const PreparedCfdBoundary boundary = hand.inlets(Velocity::fromSi(0.3));
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> velocity = varyingVelocity(cells, 0.25);
    const std::vector<double> flux = produceFlux(hand, boundary, velocity);
    const CfdMomentumInput input = steadyInput(flux, velocity);

    const CfdMomentumSystem system = assemble(hand, boundary, input);
    CHECK(system.summary.convection == convectionOf(hand, boundary, input).summary);
    CHECK(system.summary.diffusion == diffusionOf(hand, boundary, input).summary);
}

// ---------------------------------------------------------------------------
// Determinism
// ---------------------------------------------------------------------------

TEST_CASE("CfdMomentum_IsBitwiseReproducibleAcrossRepeatedAssemblies", "[cfd][mom]") {
    const Hand hand = Hand::make(chainPoints(5), chainTets(5));
    const PreparedCfdBoundary boundary = hand.inlets(Velocity::fromSi(0.27));
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> velocity = varyingVelocity(cells, 0.19);
    const std::vector<double> previous = varyingVelocity(cells, 0.51);
    const std::vector<double> flux = produceFlux(hand, boundary, velocity);
    const std::vector<PressureGradient3> gradient(
        cells, PressureGradient3{cfd::PressureGradient::fromSi(kDiscriminatingConstant),
                                 cfd::PressureGradient::fromSi(-kDiscriminatingConstant),
                                 cfd::PressureGradient::fromSi(0.5 * kDiscriminatingConstant)});

    CfdMomentumInput input = steadyInput(flux, velocity);
    input.timeStep = Time::fromSi(0.0017);
    input.previousVelocity = previous;
    input.pressureGradient = gradient;
    input.gravity = CfdGravity{Acceleration3{Acceleration::fromSi(0.0), Acceleration::fromSi(0.0),
                                             Acceleration::fromSi(-9.80665)},
                               0_m};

    const CfdMomentumSystem first = assemble(hand, boundary, input);
    for (int repeat = 0; repeat < 4; ++repeat) {
        const CfdMomentumSystem again = assemble(hand, boundary, input);
        INFO("repeat " << repeat);
        CHECK(again.matrix == first.matrix);
        CHECK(again.source == first.source);
        CHECK(again.summary == first.summary);
    }
}

// ---------------------------------------------------------------------------
// The refusals
// ---------------------------------------------------------------------------

TEST_CASE("CfdMomentum_EveryProblemHasAName", "[cfd][mom]") {
    for (const CfdMomentumProblem problem : cfd::kCfdMomentumProblems) {
        const std::string_view name = cfd::toString(problem);
        INFO("problem " << static_cast<unsigned>(problem));
        CHECK_FALSE(name.empty());
        CHECK(name != "unknown momentum problem");
    }
    // AND EVERY NAME IS DISTINCT, so a reader of a message can tell which
    // refusal it was.
    std::vector<std::string_view> names;
    for (const CfdMomentumProblem problem : cfd::kCfdMomentumProblems) {
        names.push_back(cfd::toString(problem));
    }
    std::ranges::sort(names);
    CHECK(std::ranges::adjacent_find(names) == names.end());
    CHECK(names.size() == 16);
}

TEST_CASE("CfdMomentum_RefusesADensityThatIsNotStrictlyPositiveAndFinite", "[cfd][mom]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> velocity = uniformVelocity(cells, {0.0, 0.0, 0.0});
    const std::vector<double> flux = produceFlux(hand, boundary, velocity);

    for (const double bad : {0.0, -1.0, -998.2, std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity()}) {
        CfdMomentumInput input = steadyInput(flux, velocity);
        input.density = Density::fromSi(bad);
        INFO("density " << bad);
        CHECK(refusal(hand, boundary, input).problem == CfdMomentumProblem::DensityNotUsable);
    }
}

TEST_CASE("CfdMomentum_RefusesAViscosityThatIsNotStrictlyPositiveAndFinite", "[cfd][mom]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> velocity = uniformVelocity(cells, {0.0, 0.0, 0.0});
    const std::vector<double> flux = produceFlux(hand, boundary, velocity);

    for (const double bad : {0.0, -1.0e-3, std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity()}) {
        CfdMomentumInput input = steadyInput(flux, velocity);
        input.viscosity = DynamicViscosity::fromSi(bad);
        INFO("viscosity " << bad);
        CHECK(refusal(hand, boundary, input).problem == CfdMomentumProblem::ViscosityNotUsable);
    }
}

TEST_CASE("CfdMomentum_RefusesAZeroGravityVectorRatherThanTreatingItAsAbsence", "[cfd][mom]") {
    // A ZERO VECTOR WOULD BE A SECOND SPELLING OF "NONE", which CfdData.hpp
    // refuses and `validate(CfdGravity)` enforces. Accepting it here would
    // admit a state the canonical type forbids.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> velocity = uniformVelocity(cells, {0.0, 0.0, 0.0});
    const std::vector<double> flux = produceFlux(hand, boundary, velocity);

    CfdMomentumInput input = steadyInput(flux, velocity);
    input.gravity = CfdGravity{Acceleration3{}, 0_m};
    const CfdMomentumFailure failure = refusal(hand, boundary, input);
    CHECK(failure.problem == CfdMomentumProblem::GravityNotUsable);
    CHECK_THAT(failure.error.message, Catch::Matchers::ContainsSubstring("second spelling"));

    SECTION("and a non-finite component too") {
        for (const double bad :
             {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
            CfdMomentumInput nonFinite = steadyInput(flux, velocity);
            nonFinite.gravity = CfdGravity{
                Acceleration3{Acceleration::fromSi(bad), Acceleration::fromSi(0.0),
                              Acceleration::fromSi(-9.80665)},
                0_m};
            INFO("gravity x " << bad);
            CHECK(refusal(hand, boundary, nonFinite).problem ==
                  CfdMomentumProblem::GravityNotUsable);
        }
    }
}

TEST_CASE("CfdMomentum_RefusesATimeStepThatIsNotStrictlyPositiveAndFinite", "[cfd][mom]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> velocity = uniformVelocity(cells, {0.0, 0.0, 0.0});
    const std::vector<double> previous = uniformVelocity(cells, {0.0, 0.0, 0.0});
    const std::vector<double> flux = produceFlux(hand, boundary, velocity);

    for (const double bad : {0.0, -0.001, std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity()}) {
        CfdMomentumInput input = steadyInput(flux, velocity);
        input.timeStep = Time::fromSi(bad);
        input.previousVelocity = previous;
        INFO("time step " << bad);
        CHECK(refusal(hand, boundary, input).problem == CfdMomentumProblem::TimeStepNotUsable);
    }
}

TEST_CASE("CfdMomentum_RefusesTransientInputsThatContradictEachOther", "[cfd][mom]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> velocity = uniformVelocity(cells, {0.0, 0.0, 0.0});
    const std::vector<double> previous = varyingVelocity(cells, 0.2);
    const std::vector<double> flux = produceFlux(hand, boundary, velocity);

    SECTION("a time step with no previous velocity") {
        CfdMomentumInput input = steadyInput(flux, velocity);
        input.timeStep = Time::fromSi(0.001);
        const CfdMomentumFailure failure = refusal(hand, boundary, input);
        CHECK(failure.problem == CfdMomentumProblem::TransientInputsAreInconsistent);
        CHECK_THAT(failure.error.message,
                   Catch::Matchers::ContainsSubstring("previous time level"));
    }

    SECTION("a previous velocity with no time step, because it would be inert") {
        CfdMomentumInput input = steadyInput(flux, velocity);
        input.previousVelocity = previous;
        const CfdMomentumFailure failure = refusal(hand, boundary, input);
        CHECK(failure.problem == CfdMomentumProblem::TransientInputsAreInconsistent);
        CHECK_THAT(failure.error.message,
                   Catch::Matchers::ContainsSubstring("contribute nothing"));
    }
}

TEST_CASE("CfdMomentum_RefusesEachArrayOfTheWrongLengthBeforeItsValues", "[cfd][mom]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();
    const std::size_t cells = hand.topology.cells();
    const std::size_t faces = hand.topology.faces();
    const std::vector<double> velocity = uniformVelocity(cells, {0.1, 0.2, 0.3});
    const std::vector<double> flux = produceFlux(hand, boundary, velocity);

    SECTION("the face flux") {
        std::vector<double> shortFlux = flux;
        shortFlux.pop_back();
        CfdMomentumInput input = steadyInput(shortFlux, velocity);
        const CfdMomentumFailure failure = refusal(hand, boundary, input);
        CHECK(failure.problem == CfdMomentumProblem::ValueLengthWrong);
        CHECK_THAT(failure.error.message, Catch::Matchers::ContainsSubstring("face flux"));
    }

    SECTION("the current velocity") {
        std::vector<double> shortVelocity = velocity;
        shortVelocity.pop_back();
        CfdMomentumInput input = steadyInput(flux, shortVelocity);
        const CfdMomentumFailure failure = refusal(hand, boundary, input);
        CHECK(failure.problem == CfdMomentumProblem::ValueLengthWrong);
        CHECK_THAT(failure.error.message, Catch::Matchers::ContainsSubstring("current velocity"));
    }

    SECTION("the previous velocity") {
        std::vector<double> shortPrevious = velocity;
        shortPrevious.pop_back();
        CfdMomentumInput input = steadyInput(flux, velocity);
        input.timeStep = Time::fromSi(0.001);
        input.previousVelocity = shortPrevious;
        const CfdMomentumFailure failure = refusal(hand, boundary, input);
        CHECK(failure.problem == CfdMomentumProblem::ValueLengthWrong);
        CHECK_THAT(failure.error.message, Catch::Matchers::ContainsSubstring("previous velocity"));
    }

    SECTION("the pressure gradient, which is one per CELL and not per face") {
        const std::vector<PressureGradient3> wrong(faces, PressureGradient3{});
        REQUIRE(faces != cells);
        CfdMomentumInput input = steadyInput(flux, velocity);
        input.pressureGradient = wrong;
        const CfdMomentumFailure failure = refusal(hand, boundary, input);
        CHECK(failure.problem == CfdMomentumProblem::ValueLengthWrong);
        CHECK_THAT(failure.error.message, Catch::Matchers::ContainsSubstring("pressure gradient"));
    }

    SECTION("the face gradient, which is one per FACE and not per cell") {
        const std::vector<cfd::VelocityGradient3> wrong(cells, cfd::VelocityGradient3{});
        CfdMomentumInput input = steadyInput(flux, velocity);
        input.faceGradient = wrong;
        const CfdMomentumFailure failure = refusal(hand, boundary, input);
        CHECK(failure.problem == CfdMomentumProblem::ValueLengthWrong);
        CHECK_THAT(failure.error.message, Catch::Matchers::ContainsSubstring("face gradient"));
    }
}

TEST_CASE("CfdMomentum_RefusesANonFiniteValueInEveryArrayItAccepts", "[cfd][mom]") {
    // THE SAME LIST AS THE LENGTH PHASE, which is the point: an array checked
    // for length and never for value is the hole a review found in
    // `assembleCfdDiffusion`.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();
    const std::size_t cells = hand.topology.cells();
    const std::size_t faces = hand.topology.faces();
    const std::vector<double> velocity = uniformVelocity(cells, {0.1, 0.2, 0.3});
    const std::vector<double> flux = produceFlux(hand, boundary, velocity);

    // ALL THREE NON-FINITE VALUES, NOT ONLY A NaN. An earlier draft poisoned
    // every array with `quiet_NaN()` alone, so changing the gate's
    // `!std::isfinite(x)` to `std::isnan(x)` -- which ADMITS an infinity in
    // `phi`, both velocities and both gradients -- passed the entire suite. An
    // independent review found it, and noted that both qualified precedents
    // (`CfdConvectionTests.cpp`, `CfdDiffusionTests.cpp`) do test infinities.
    // `ValueNotFinite`'s own documentation says "NaN or infinite".
    const std::array<double, 3> poisons{std::numeric_limits<double>::quiet_NaN(),
                                        std::numeric_limits<double>::infinity(),
                                        -std::numeric_limits<double>::infinity()};

    SECTION("the face flux") {
        for (const double poison : poisons) {
            const std::vector<double> bad = withPoisonAt(flux, poison);
            INFO("poison " << poison);
            CHECK(refusal(hand, boundary, steadyInput(bad, velocity)).problem ==
                  CfdMomentumProblem::ValueNotFinite);
        }
    }

    SECTION("the current velocity") {
        for (const double poison : poisons) {
            const std::vector<double> bad = withPoisonAt(velocity, poison);
            INFO("poison " << poison);
            CHECK(refusal(hand, boundary, steadyInput(flux, bad)).problem ==
                  CfdMomentumProblem::ValueNotFinite);
        }
    }

    SECTION("the previous velocity") {
        for (const double poison : poisons) {
            const std::vector<double> bad = withPoisonAt(velocity, poison);
            CfdMomentumInput input = steadyInput(flux, velocity);
            input.timeStep = Time::fromSi(0.001);
            input.previousVelocity = bad;
            INFO("poison " << poison);
            CHECK(refusal(hand, boundary, input).problem == CfdMomentumProblem::ValueNotFinite);
        }
    }

    SECTION("the pressure gradient, every component and every poison") {
        for (const double poison : poisons) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                std::vector<PressureGradient3> bad(cells, PressureGradient3{});
                const auto value = cfd::PressureGradient::fromSi(poison);
                if (axis == 0) {
                    bad.front().x = value;
                } else if (axis == 1) {
                    bad.front().y = value;
                } else {
                    bad.front().z = value;
                }
                CfdMomentumInput input = steadyInput(flux, velocity);
                input.pressureGradient = bad;
                INFO("axis " << axis << ", poison " << poison);
                CHECK(refusal(hand, boundary, input).problem ==
                      CfdMomentumProblem::ValueNotFinite);
            }
        }
    }

    SECTION("the face gradient, every component and every poison") {
        for (const double poison : poisons) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                std::vector<cfd::VelocityGradient3> bad(faces, cfd::VelocityGradient3{});
                const auto value = cfd::VelocityGradient::fromSi(poison);
                if (axis == 0) {
                    bad.front().x = value;
                } else if (axis == 1) {
                    bad.front().y = value;
                } else {
                    bad.front().z = value;
                }
                CfdMomentumInput input = steadyInput(flux, velocity);
                input.faceGradient = bad;
                INFO("axis " << axis << ", poison " << poison);
                CHECK(refusal(hand, boundary, input).problem ==
                      CfdMomentumProblem::ValueNotFinite);
            }
        }
    }
}

TEST_CASE("CfdMomentum_RefusesAComponentThatIsNotOneOfTheThree", "[cfd][mom]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> velocity = uniformVelocity(cells, {0.0, 0.0, 0.0});
    const std::vector<double> flux = produceFlux(hand, boundary, velocity);

    CfdMomentumInput input = steadyInput(flux, velocity);
    input.component = static_cast<VectorComponent>(7);
    CHECK(refusal(hand, boundary, input).problem == CfdMomentumProblem::ComponentOutOfRange);
}

TEST_CASE("CfdMomentum_SurfacesASubOperatorRefusalUnderItsOwnTag", "[cfd][mom]") {
    // DIFFUSION REFUSES A SYMMETRY FACE WITH NO CURRENT VELOCITY
    // (`SymmetryNeedsTheCurrentVelocity`), and momentum's own gate has no such
    // check -- so this is the one reachable route to
    // `DiffusionRefusedTheInput`, and the message must name the sub-problem
    // rather than leave a reader guessing which operator objected.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary slips = hand.slips();
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> velocity = uniformVelocity(cells, {0.1, 0.2, 0.3});
    const std::vector<double> flux = produceFlux(hand, slips, velocity);

    CfdMomentumInput input = steadyInput(flux, velocity);
    input.currentVelocity = std::span<const double>{};
    const CfdMomentumFailure failure = refusal(hand, slips, input);
    CHECK(failure.problem == CfdMomentumProblem::DiffusionRefusedTheInput);
    CHECK_THAT(failure.error.message, Catch::Matchers::ContainsSubstring("diffusion operator"));
    CHECK_THAT(failure.error.message,
               Catch::Matchers::ContainsSubstring(
                   std::string{cfd::toString(cfd::CfdDiffusionProblem::SymmetryNeedsTheCurrentVelocity)}));

    SECTION("and the slip wall assembles once the velocity is supplied") {
        const CfdMomentumSystem system = assemble(hand, slips, steadyInput(flux, velocity));
        CHECK(system.summary.diffusion.symmetryFaces > 0);
    }
}

TEST_CASE("CfdMomentum_RefusesANonFiniteResultWhenTheTransientTermOverflows", "[cfd][mom]") {
    // REACHABLE FROM FINITE, STRICTLY POSITIVE INPUTS, which is the whole
    // argument for the tagged return type: `rho * V / dt` overflows from three
    // values each of which passes its own phase. Both sub-assemblies gate their
    // own output, so neither can see this.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const PreparedCfdBoundary boundary = hand.walls();
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> velocity = uniformVelocity(cells, {0.0, 0.0, 0.0});
    const std::vector<double> previous = uniformVelocity(cells, {0.0, 0.0, 0.0});
    const std::vector<double> flux = produceFlux(hand, boundary, velocity);

    CfdMomentumInput input = steadyInput(flux, velocity);
    input.density = Density::fromSi(1.0e300);
    input.timeStep = Time::fromSi(1.0e-300);
    input.previousVelocity = previous;

    std::expected<CfdMomentumSystem, CfdMomentumFailure> system =
        cfd::assembleCfdMomentum(hand.topology, hand.geometry, boundary, input);
    REQUIRE_FALSE(system.has_value());
    CHECK(system.error().problem == CfdMomentumProblem::ResultIsNotFinite);

    // AND THE PREDICTOR CANNOT SEE IT, because it judges an output the
    // predictor never computes. That is not a gap: it is why the assembly
    // returns a tagged failure of its own.
    CHECK_FALSE(
        cfd::cfdMomentumProblem(hand.topology, hand.geometry, boundary, input).has_value());
}

TEST_CASE("CfdMomentum_TheTaggedFailureCarriesBothTheTagAndTheMessage", "[cfd][mom]") {
    // THE TAG IS THE POINT. P19-BC-001's review found eight refusals whose tags
    // could be swapped with every message assertion still passing, so each
    // refusal asserts its tag against an INDEPENDENTLY NAMED expectation and
    // the message separately.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> velocity = uniformVelocity(cells, {0.0, 0.0, 0.0});
    const std::vector<double> flux = produceFlux(hand, boundary, velocity);

    CfdMomentumInput input = steadyInput(flux, velocity);
    input.density = Density::fromSi(-1.0);
    const CfdMomentumFailure failure = refusal(hand, boundary, input);
    CHECK(failure.problem == CfdMomentumProblem::DensityNotUsable);
    CHECK(failure.error.code == ErrorCode::InvalidArgument);
    CHECK_FALSE(failure.error.message.empty());
    CHECK_THAT(failure.error.message, Catch::Matchers::ContainsSubstring("density"));
    CHECK_THAT(failure.error.message, Catch::Matchers::ContainsSubstring("-1"));
}

TEST_CASE("CfdMomentum_TheDeclaredPhaseOrderIsTheCheckOrder", "[cfd][mom]") {
    // AN INPUT WITH FAULTS IN TWO PHASES REPORTS THE EARLIER ONE. Asserted
    // between the phases a CONSTRUCTIBLE input can reach; nothing is claimed
    // about the three recorded as unreached.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> velocity = uniformVelocity(cells, {0.0, 0.0, 0.0});
    const std::vector<double> flux = produceFlux(hand, boundary, velocity);

    const auto poison = [&](CfdMomentumInput& input) {
        // Every later phase made to fail at once, so each assertion below is
        // about the ORDER and not about the fault.
        input.component = static_cast<VectorComponent>(9);
        input.density = Density::fromSi(-1.0);
        input.viscosity = DynamicViscosity::fromSi(0.0);
        input.gravity = CfdGravity{Acceleration3{}, 0_m};
        input.timeStep = Time::fromSi(-1.0);
    };

    SECTION("the component beats the density") {
        CfdMomentumInput input = steadyInput(flux, velocity);
        poison(input);
        CHECK(refusal(hand, boundary, input).problem == CfdMomentumProblem::ComponentOutOfRange);
    }

    SECTION("the density beats the viscosity") {
        CfdMomentumInput input = steadyInput(flux, velocity);
        poison(input);
        input.component = VectorComponent::X;
        CHECK(refusal(hand, boundary, input).problem == CfdMomentumProblem::DensityNotUsable);
    }

    SECTION("the viscosity beats gravity") {
        CfdMomentumInput input = steadyInput(flux, velocity);
        poison(input);
        input.component = VectorComponent::X;
        input.density = kDensity;
        CHECK(refusal(hand, boundary, input).problem == CfdMomentumProblem::ViscosityNotUsable);
    }

    SECTION("gravity beats the time step") {
        CfdMomentumInput input = steadyInput(flux, velocity);
        poison(input);
        input.component = VectorComponent::X;
        input.density = kDensity;
        input.viscosity = kViscosity;
        CHECK(refusal(hand, boundary, input).problem == CfdMomentumProblem::GravityNotUsable);
    }

    SECTION("the time step beats the transient consistency") {
        CfdMomentumInput input = steadyInput(flux, velocity);
        input.timeStep = Time::fromSi(-1.0);
        // No previous velocity either, so phase 9 would also fire.
        CHECK(refusal(hand, boundary, input).problem == CfdMomentumProblem::TimeStepNotUsable);
    }

    SECTION("the transient consistency beats the array lengths") {
        std::vector<double> shortFlux = flux;
        shortFlux.pop_back();
        CfdMomentumInput input = steadyInput(shortFlux, velocity);
        input.timeStep = Time::fromSi(0.001);
        CHECK(refusal(hand, boundary, input).problem ==
              CfdMomentumProblem::TransientInputsAreInconsistent);
    }

    SECTION("a wrong length beats a non-finite value") {
        const std::vector<double> bad = shortenedWithLeadingNan(flux);
        CfdMomentumInput input = steadyInput(bad, velocity);
        CHECK(refusal(hand, boundary, input).problem == CfdMomentumProblem::ValueLengthWrong);
    }

    SECTION("a non-finite value beats a sub-operator refusal") {
        // A slip wall with no current velocity is diffusion's refusal; a NaN in
        // `phi` is phase 11. The earlier phase wins.
        const PreparedCfdBoundary slips = hand.slips();
        const std::vector<double> slipFlux = produceFlux(hand, slips, velocity);
        const std::vector<double> bad = withLeadingNan(slipFlux);
        CfdMomentumInput input = steadyInput(bad, velocity);
        input.currentVelocity = std::span<const double>{};
        CHECK(refusal(hand, slips, input).problem == CfdMomentumProblem::ValueNotFinite);
    }
}

// ---------------------------------------------------------------------------
// The reference bodies
// ---------------------------------------------------------------------------

TEST_CASE("CfdMomentum_OnTheReferenceBodiesTheDecompositionAndRowSumIdentitiesHold",
          "[cfd][mom][reference]") {
    // MESHES NOBODY DESIGNED TO FLATTER THE OPERATOR. The hand fixtures are
    // built so a property can be read entry by entry; these are built to be a
    // part, and ADR-063 finding F1 measured them at 31 to 66 degrees mean
    // non-orthogonality with maxima near 90.
    //
    // THE TRANSIENT FORM IS USED HERE DELIBERATELY, so the decomposition claim
    // covers the diagonal's extra term on a real mesh and not only on a chain.
    const std::vector<Meshable> models = meshableModels();
    REQUIRE_FALSE(models.empty());

    for (const Meshable& model : models) {
        const Reference reference{model.kind};
        const PreparedCfdBoundary boundary = reference.wallEverywhere();
        const std::size_t cells = reference.topology->cells();
        const std::vector<double> velocity =
            uniformVelocity(cells, {0.05, -0.03, 0.02});
        const CfdMassFluxInput produced{velocity, kDensity};
        std::expected<std::vector<double>, cfd::CfdConvectionFailure> flux =
            cfd::cfdMassFlux(*reference.topology, *reference.geometry, boundary, produced);
        INFO(model.name << ": " << (flux.has_value() ? std::string{} : flux.error().error.message));
        REQUIRE(flux.has_value());

        // THE PREVIOUS FIELD IS NOT THE CURRENT ONE, AND IT VARIES CELL BY
        // CELL. An earlier draft set `input.previousVelocity = velocity` -- the
        // current field passed as the previous one, which is exactly the trap
        // ADR-073 Decision 4 exists to describe -- and both were uniform, so
        // every cell and every component carried the same number and ANY
        // mis-indexing of `previousVelocity` was invisible by construction. An
        // independent review found it in the one test meant to be the hardest.
        const Time timeStep = Time::fromSi(0.01);
        const std::vector<double> previous = varyingVelocity(cells, 0.41);
        REQUIRE(previous != velocity);

        // AND THE OTHER TWO SOURCE TERMS ARE SUPPLIED, so that the five-term
        // order is exercised on a mesh nobody designed to flatter it rather
        // than only on the two hand fixtures. The same review found the
        // reference test never touched `source` at all.
        std::vector<PressureGradient3> gradient;
        for (std::size_t c = 0; c < cells; ++c) {
            const double k = kDiscriminatingConstant * (1.0 + static_cast<double>(c % 7));
            gradient.push_back(PressureGradient3{cfd::PressureGradient::fromSi(k),
                                                 cfd::PressureGradient::fromSi(-0.5 * k),
                                                 cfd::PressureGradient::fromSi(0.25 * k)});
        }
        const Acceleration3 gravity{Acceleration::fromSi(1.5), Acceleration::fromSi(-9.80665),
                                    Acceleration::fromSi(0.75)};

        CfdMomentumInput input = steadyInput(*flux, velocity);
        input.timeStep = timeStep;
        input.previousVelocity = previous;
        input.pressureGradient = gradient;
        input.gravity = CfdGravity{gravity, 0_m};

        std::expected<CfdMomentumSystem, CfdMomentumFailure> system = cfd::assembleCfdMomentum(
            *reference.topology, *reference.geometry, boundary, input);
        INFO(model.name << ": " << (system.has_value() ? std::string{} : system.error().error.message));
        REQUIRE(system.has_value());

        const CfdConvectionInput subConvection{input.faceFlux, input.scheme, input.component};
        std::expected<CfdConvectionSystem, cfd::CfdConvectionFailure> convection =
            cfd::assembleCfdConvection(*reference.topology, *reference.geometry, boundary,
                                       subConvection);
        REQUIRE(convection.has_value());
        const CfdDiffusionInput subDiffusion{input.viscosity, input.component, input.currentVelocity,
                                             input.faceGradient};
        Result<CfdDiffusionSystem> diffusion = cfd::assembleCfdDiffusion(
            *reference.topology, *reference.geometry, boundary, subDiffusion);
        REQUIRE(diffusion.has_value());

        // ---- THE DECOMPOSITION, SLOT BY SLOT, BIT-EXACT -------------------
        //
        // Walked by ROW so the diagonal slot is known rather than searched for,
        // which is what an earlier draft of this test got wrong: it compared
        // every slot against the bare sum and then went looking for the row a
        // mismatching slot belonged to, which is the same information arrived
        // at backwards.
        INFO(model.name << " with " << cells << " cells");
        REQUIRE(system->matrix.nonZeros() == convection->matrix.nonZeros());
        REQUIRE(system->matrix.nonZeros() == diffusion->matrix.nonZeros());
        for (std::size_t r = 0; r < cells; ++r) {
            const auto first = static_cast<std::size_t>(system->matrix.rowStart()[r]);
            const auto last = static_cast<std::size_t>(system->matrix.rowStart()[r + 1]);
            const double trans = transientOracle(*reference.geometry, r, kDensity, timeStep);
            for (std::size_t s = first; s < last; ++s) {
                const double base =
                    convection->matrix.values()[s] + diffusion->matrix.values()[s];
                const bool isDiagonal =
                    system->matrix.inner()[s] == static_cast<cfd::CfdSparseIndex>(r);
                INFO("row " << r << ", slot " << s << (isDiagonal ? " (diagonal)" : ""));
                CHECK(system->matrix.values()[s] == (isDiagonal ? base + trans : base));
            }
        }

        // ---- ROW-SUM ADDITIVITY, to the measured bound --------------------
        for (std::size_t c = 0; c < cells; ++c) {
            const double termwise = convection->matrix.rowSumSi(c) +
                                    diffusion->matrix.rowSumSi(c) +
                                    transientOracle(*reference.geometry, c, kDensity, timeStep);
            INFO(model.name << " cell " << c);
            CHECK(std::abs(system->matrix.rowSumSi(c) - termwise) <=
                  cfd::kCfdMomentumRowSumTolerance * rowScale(system->matrix, c));
        }

        // ---- THE FIVE-TERM SOURCE ORDER, PER CELL -------------------------
        //
        // Reproduced in the declared order on every reference body, which is
        // where it matters: the two hand fixtures have two and four cells, and
        // ADR-073's Q5 measured declared-against-reordered agreement at 57.45%
        // PER SAMPLE -- so six samples agreeing by chance has a few per cent
        // probability, while a few thousand does not. The order claim stops
        // being enforced probabilistically here.
        const auto axis = static_cast<std::size_t>(input.component);
        for (std::size_t c = 0; c < cells; ++c) {
            const double volume = reference.geometry->cellVolume(CellIndex::fromValue(c)).si();
            const double trans = transientOracle(*reference.geometry, c, kDensity, timeStep);
            double expected = convection->source[c];
            expected += diffusion->source[c];
            expected += trans * previous[axis * cells + c];
            expected += -(volume * gradient[c].component(input.component).si());
            expected += (kDensity.si() * volume) * gravity.component(input.component).si();
            INFO(model.name << " cell " << c);
            CHECK(system->source[c] == expected);
        }

        // ---- AND THE SUB-SUMMARIES ARE CARRIED VERBATIM ------------------
        CHECK(system->summary.convection == convection->summary);
        CHECK(system->summary.diffusion == diffusion->summary);
        CHECK(system->summary.cells == cells);
        CHECK(system->summary.transientTermApplied);
        CHECK(system->summary.pressureGradientApplied);
        CHECK(system->summary.gravityApplied);
    }
}

// ---------------------------------------------------------------------------
// The two signs, each checked INDEPENDENTLY of the production formula
// ---------------------------------------------------------------------------

TEST_CASE("CfdMomentum_DiffusionEntersWithAPlusBecauseTheOperatorIsPositiveDefinite",
          "[cfd][mom]") {
    // THE SIGN OF THE DIFFUSION TERM, CHECKED BY PHYSICS RATHER THAN BY THE
    // PRODUCTION FORMULA, and this test exists because an independent review
    // found that nothing checked it. Every other assertion in this file writes
    // `conv + diff` -- the production expression -- so it catches a MUTATION of
    // the sign but cannot validate the CONVENTION; and the one test that looked
    // physical, the constant-steady-solution test below, is provably sign-blind,
    // because the constant is in the null space of each operator SEPARATELY and
    // `A 1 = 0` therefore holds for `a A_conv + b A_diff` at any `a` and `b`,
    // the flipped sign included. So the whole evidence for "no sign is flipped
    // at the meeting point" was one sentence of prose in `CfdConvection.hpp`.
    //
    // A DIFFUSION OPERATOR DISSIPATES ENERGY, so its discrete form must be
    // POSITIVE DEFINITE once some boundary face prescribes a value. With walls
    // everywhere `phi` is bit-zero at every face (ADR-067 gives all three walls
    // `FixedValue 0`), so the convection matrix is identically zero and
    // `A == A_diff`. If the assembly formed `conv - diff`, this matrix would be
    // NEGATIVE definite and `x^T A x` would be negative for every `x`. The
    // claim needs no coefficient formula and no solve.
    const Hand hand = Hand::make(chainPoints(5), chainTets(5));
    const PreparedCfdBoundary boundary = hand.walls();
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> still = uniformVelocity(cells, {0.0, 0.0, 0.0});
    const std::vector<double> flux = produceFlux(hand, boundary, still);
    // The premise, asserted rather than assumed.
    REQUIRE(std::ranges::all_of(flux, [](double v) { return v == 0.0; }));

    const CfdMomentumSystem system = assemble(hand, boundary, steadyInput(flux, still));

    // Several NON-CONSTANT vectors, because a constant is in the null space of
    // the interior and would make the form vanish there.
    const std::vector<std::vector<double>> probes = [&] {
        std::vector<std::vector<double>> out;
        std::vector<double> ramp(cells);
        std::vector<double> alternating(cells);
        std::vector<double> spike(cells, 0.0);
        for (std::size_t c = 0; c < cells; ++c) {
            ramp[c] = 1.0 + static_cast<double>(c);
            alternating[c] = (c % 2 == 0) ? 1.0 : -1.0;
        }
        spike[cells / 2] = kDiscriminatingConstant;
        out.push_back(ramp);
        out.push_back(alternating);
        out.push_back(spike);
        return out;
    }();

    for (std::size_t probe = 0; probe < probes.size(); ++probe) {
        const std::vector<double>& x = probes[probe];
        Result<std::vector<double>> product = system.matrix.multiply(x);
        REQUIRE(product.has_value());
        double form = 0.0;
        for (std::size_t c = 0; c < cells; ++c) {
            form += x[c] * (*product)[c];
        }
        INFO("probe " << probe << ", x^T A x = " << form);
        CHECK(form > 0.0);
    }

    // AND THE DIAGONAL IS POSITIVE, which the same flip would invert. Stated
    // separately because a positive quadratic form does not by itself require
    // it, and because this is the half a reader checks by eye.
    for (std::size_t c = 0; c < cells; ++c) {
        INFO("cell " << c);
        CHECK(coeff(system.matrix, c, c) > 0.0);
    }
}

TEST_CASE("CfdMomentum_ConvectionEntersWithAPlusMeasuredAgainstTheDivergenceOperator",
          "[cfd][mom]") {
    // THE SIGN AND THE MAGNITUDE OF THE CONVECTION TERM, against a SEPARATE
    // OPERATOR AND A SEPARATE CODE PATH. `divergeCfdMassFlux` is
    // P19-DIV-001's, written before this milestone and qualified on its own
    // evidence; it computes the net flux out of a cell by its own traversal.
    //
    // With pressure outlets everywhere `U` is `ZeroGradient` at every boundary
    // face, so diffusion contributes NOTHING there (CfdDiffusion.hpp: a
    // ZeroGradient face does not contribute a coefficient) and its internal
    // faces write a coefficient and its exact negation into the same row. So
    // `rowSum(A_diff, c)` is zero to rounding and
    //
    // ```text
    // rowSum(A_momentum, c) == divergeCfdMassFlux(phi).at(c)
    // ```
    //
    // If convection entered with a minus the row sum would be `-div(phi)`,
    // which this assertion fails for every cell whose divergence is non-zero.
    // Nothing here reads a convection coefficient or re-derives one.
    const Hand hand = Hand::make(chainPoints(5), chainTets(5));
    const PreparedCfdBoundary boundary = hand.outlets();
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> velocity = varyingVelocity(cells, 0.33);
    const std::vector<double> flux = produceFlux(hand, boundary, velocity);

    const cfd::CfdMassFluxDivergenceInput divergenceInput{flux};
    Result<cfd::CfdMassFluxDivergenceField> divergence =
        cfd::divergeCfdMassFlux(hand.topology, hand.geometry, divergenceInput);
    INFO((divergence.has_value() ? std::string{} : divergence.error().message));
    REQUIRE(divergence.has_value());

    const CfdMomentumSystem system = assemble(hand, boundary, steadyInput(flux, velocity));
    CHECK(system.summary.diffusion.zeroGradientFaces == hand.topology.boundaryFaces());

    std::size_t nonTrivial = 0;
    for (std::size_t c = 0; c < cells; ++c) {
        const std::optional<MassFlowRate> net = divergence->at(CellIndex::fromValue(c));
        REQUIRE(net.has_value());
        const double scale = rowScale(system.matrix, c);
        INFO("cell " << c << ": rowSum " << system.matrix.rowSumSi(c) << ", div " << net->si());
        CHECK(std::abs(system.matrix.rowSumSi(c) - net->si()) <=
              cfd::kCfdMomentumRowSumTolerance * scale);
        // THE ASSERTION MUST NOT BE VACUOUS: a cell whose divergence is zero
        // would satisfy it with either sign, so at least one cell has to carry
        // a divergence large enough to distinguish them.
        if (std::abs(net->si()) > cfd::kCfdMomentumRowSumTolerance * scale * 1.0e3) {
            ++nonTrivial;
        }
    }
    INFO("cells whose divergence distinguishes the two signs: " << nonTrivial);
    REQUIRE(nonTrivial > 0);
}

// ---------------------------------------------------------------------------
// No diagonal floor, on a fixture that genuinely goes negative
// ---------------------------------------------------------------------------

TEST_CASE("CfdMomentum_LeavesADiagonalNegativeWhereTheFluxDrivesItNegative", "[cfd][mom]") {
    // A DIAGONAL FLOOR WOULD BE INVISIBLE ON A WELL-BEHAVED MESH AND IS VISIBLE
    // HERE, and the fixture is the second attempt. An independent review traced
    // the first one: a one-cell tet with pressure outlets and a uniform owner
    // velocity has a diagonal of `rho U_P . sum_f S_f`, which is the cell's
    // CLOSURE RESIDUAL -- approximately zero and of indeterminate sign, not
    // negative. The ADR claimed the fixture drove a diagonal negative and it
    // did not, and the test asserted only `rowsNotDiagonallyDominant <= cells`,
    // which is true of any count over `cells` rows and so was vacuous.
    //
    // `phi` IS HAND-MADE HERE, which the operator accepts: it takes a face
    // array and asks nothing about its divergence (ADR-072 Decision 5 refused a
    // conservativeness threshold for the reason ADR-063 finding F1 gives). With
    // pressure outlets every boundary face is `ZeroGradient`, so an INFLOW face
    // puts its negative `phi` straight onto the diagonal as backflow and
    // diffusion adds nothing there. Making every boundary flux negative drives
    // the diagonal negative by construction.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const PreparedCfdBoundary boundary = hand.outlets();
    const std::size_t cells = hand.topology.cells();
    REQUIRE(cells == 1);
    REQUIRE(hand.topology.internalFaces() == 0);

    std::vector<double> phi(hand.topology.faces(), 0.0);
    for (std::size_t f = 0; f < hand.topology.faces(); ++f) {
        phi[f] = -kDiscriminatingConstant;
    }

    const std::vector<double> still = uniformVelocity(cells, {0.0, 0.0, 0.0});
    const CfdMomentumInput input = steadyInput(phi, still);
    const CfdMomentumSystem system = assemble(hand, boundary, input);
    const CfdConvectionSystem convection = convectionOf(hand, boundary, input);
    const CfdDiffusionSystem diffusion = diffusionOf(hand, boundary, input);

    // EVERY boundary face is backflow, and the diagonal is NEGATIVE.
    CHECK(system.summary.convection.backflowFaces == hand.topology.faces());
    CHECK(system.summary.convection.inflowFaces == hand.topology.faces());
    const double diagonal = coeff(system.matrix, 0, 0);
    INFO("diagonal " << diagonal);
    CHECK(diagonal < 0.0);

    // AND IT IS EXACTLY ITS NAMED PARTS: no floor, no clamp, no `std::abs`, no
    // artificial viscosity. This is the assertion the ADR's second falsifiable
    // form promised.
    CHECK(diagonal == coeff(convection.matrix, 0, 0) + coeff(diffusion.matrix, 0, 0));
    // The whole boundary flux is on the diagonal, so it is the sum of `phi`.
    CHECK_THAT(diagonal,
               Catch::Matchers::WithinRel(-kDiscriminatingConstant *
                                              static_cast<double>(hand.topology.faces()),
                                          1.0e-13));
    // And the row is reported as not dominant, since |a_ii| is compared against
    // an off-diagonal sum of zero -- so this is the one fixture where the
    // dominance count is NOT vacuous.
    CHECK(system.summary.rowsNotDiagonallyDominant == 0);
    CHECK(diffusion.matrix.coeffSi(0, 0) == 0.0);
}

// ---------------------------------------------------------------------------
// The refusals the review showed were reachable or missing
// ---------------------------------------------------------------------------

TEST_CASE("CfdMomentum_RefusesASchemeThatIsNotOneOfTheTwo", "[cfd][mom]") {
    // AN OUT-OF-RANGE SCHEME WAS SILENTLY SUBSTITUTED BY UPWIND before this
    // phase existed: `assembleCfdConvection` forms `central = (scheme ==
    // Central)`, which is false for 7. An independent review found that the
    // gate answered for an out-of-range `VectorComponent` and not for an
    // out-of-range `CfdConvectionScheme`, which has exactly the same property.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> velocity = uniformVelocity(cells, {0.0, 0.0, 0.0});
    const std::vector<double> flux = produceFlux(hand, boundary, velocity);

    CfdMomentumInput input = steadyInput(flux, velocity);
    input.scheme = static_cast<CfdConvectionScheme>(7);
    const CfdMomentumFailure failure = refusal(hand, boundary, input);
    CHECK(failure.problem == CfdMomentumProblem::SchemeOutOfRange);
    CHECK_THAT(failure.error.message, Catch::Matchers::ContainsSubstring("silently"));

    SECTION("and both legal schemes are accepted") {
        for (const CfdConvectionScheme scheme : cfd::kCfdConvectionSchemes) {
            CfdMomentumInput good = steadyInput(flux, velocity);
            good.scheme = scheme;
            CHECK(assemble(hand, boundary, good).summary.convection.scheme == scheme);
        }
    }
}

TEST_CASE("CfdMomentum_ReportsAConvectionOverflowAsANonFiniteResultAndNotAnInconsistency",
          "[cfd][mom]") {
    // THE ROUTE AN INDEPENDENT REVIEW FOUND. `cfdConvectionProblem` judges
    // INPUTS only and says so in its own header -- "IT CANNOT SEE
    // `ResultIsNotFinite`, which judges an output this function never
    // computes". So a `phi` whose values are each finite but whose SUM
    // overflows passes momentum's finiteness phase, passes the convection
    // predictor, and is then refused by the convection assembly.
    //
    // AN EARLIER DRAFT REPORTED THAT AS `AssemblyIsInconsistent`, whose
    // documented meaning is that a predictor disagreed with its own assembly --
    // when the two had agreed perfectly and the fault was an overflow. The tag
    // is now the one momentum's own enum already had for it.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const PreparedCfdBoundary boundary = hand.outlets();
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> still = uniformVelocity(cells, {0.0, 0.0, 0.0});

    // Four finite values whose sum overflows on one cell's diagonal.
    std::vector<double> phi(hand.topology.faces(), 1.0e308);
    REQUIRE(hand.topology.faces() >= 2);
    for (const double value : phi) {
        REQUIRE(std::isfinite(value));
    }

    const CfdMomentumInput input = steadyInput(phi, still);
    std::expected<CfdMomentumSystem, CfdMomentumFailure> system =
        cfd::assembleCfdMomentum(hand.topology, hand.geometry, boundary, input);
    REQUIRE_FALSE(system.has_value());
    CHECK(system.error().problem == CfdMomentumProblem::ResultIsNotFinite);
    CHECK_THAT(system.error().error.message,
               Catch::Matchers::ContainsSubstring("every phase accepted"));

    // The gate accepted it, which is the point: no input phase can see this.
    CHECK_FALSE(
        cfd::cfdMomentumProblem(hand.topology, hand.geometry, boundary, input).has_value());
}

TEST_CASE("CfdMomentum_ReachesAssemblyIsInconsistentThroughDiffusionsUntaggedChannel",
          "[cfd][mom]") {
    // THIS PATH WAS DOCUMENTED AS UNREACHABLE AND IS NOT. An independent review
    // traced it: a viscosity of `1.5e308` is finite and strictly positive, so it
    // passes momentum's viscosity phase and `cfdDiffusionProblem`, and then
    // `mu * faceArea / orthogonalDistance` overflows inside the diffusion
    // assembly. Because `assembleCfdDiffusion` returns
    // `Result<CfdDiffusionSystem>` -- a channel with no tag, which
    // `CfdDiffusion.hpp` removed its own `AssembledMatrixInconsistent` for --
    // momentum cannot tell an output overflow from a genuine inconsistency and
    // must not guess.
    //
    // SO IT IS TESTED RATHER THAN CLAIMED UNREACHED, and the limitation is
    // recorded as inherited from P19-DIFF-001's untagged channel rather than
    // denied.
    // THE METRE-SCALE FIXTURE, because `mu * faceArea / orthogonalDistance`
    // cannot overflow on a millimetre one however large a finite `mu` is. The
    // premise is asserted below rather than assumed.
    const Hand hand = Hand::make(metreScalePoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> still = uniformVelocity(cells, {0.0, 0.0, 0.0});
    const std::vector<double> flux = produceFlux(hand, boundary, still);

    CfdMomentumInput input = steadyInput(flux, still);
    input.viscosity = DynamicViscosity::fromSi(1.5e308);
    REQUIRE(std::isfinite(input.viscosity.si()));
    REQUIRE(input.viscosity.si() > 0.0);
    // The geometric ratio that decides whether the product overflows at all.
    double worstRatio = 0.0;
    for (std::size_t f = 0; f < hand.topology.faces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        const double distance = hand.geometry.orthogonalDistance(face).si();
        if (distance > 0.0) {
            worstRatio = std::max(worstRatio, hand.geometry.faceArea(face).si() / distance);
        }
    }
    INFO("worst faceArea/orthogonalDistance = " << worstRatio);
    REQUIRE(worstRatio > 1.2);

    // Both gates accept it.
    CHECK_FALSE(
        cfd::cfdMomentumProblem(hand.topology, hand.geometry, boundary, input).has_value());

    std::expected<CfdMomentumSystem, CfdMomentumFailure> system =
        cfd::assembleCfdMomentum(hand.topology, hand.geometry, boundary, input);
    REQUIRE_FALSE(system.has_value());
    CHECK(system.error().problem == CfdMomentumProblem::AssemblyIsInconsistent);
    CHECK_THAT(system.error().error.message,
               Catch::Matchers::ContainsSubstring("carries no tag"));
    // AND IT IS AN INTERNAL FAULT, NOT THE CALLER'S ARGUMENT. An earlier draft
    // stamped every refusal `InvalidArgument`, including the two that judge this
    // assembly's own behaviour.
    CHECK(system.error().error.code == ErrorCode::Internal);
}

// ---------------------------------------------------------------------------
// The two refusals an independent review showed were reachable in three lines
// ---------------------------------------------------------------------------

TEST_CASE("CfdMomentum_RefusesMetricsBuiltForAnotherMesh", "[cfd][mom]") {
    // PHASE 1 HAD NO TEST AT ALL, and an independent review found both the gap
    // and that the file's own header had called this enumerator unreachable --
    // "on this path (both are built from one mesh here)", which admits in the
    // same sentence that it is a property of the fixture and not of the API.
    // The qualified precedent reaches it in three lines and so does this.
    //
    // THE CONSEQUENCE WAS NOT A MISSING ASSERTION BUT A DELETABLE CHECK:
    // removing phase 1 from the gate left the suite green, because the fault
    // was re-reported later as `ConvectionRefusedTheInput` -- naming convection
    // for a topology fault, which is the exact diagnostic regression the check
    // is documented to prevent.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const Hand other = Hand::make(oneCellPoints(), kOneCell);
    const PreparedCfdBoundary boundary = hand.walls();
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> velocity = uniformVelocity(cells, {0.0, 0.0, 0.0});
    const std::vector<double> flux = produceFlux(hand, boundary, velocity);
    const CfdMomentumInput input = steadyInput(flux, velocity);

    std::expected<CfdMomentumSystem, CfdMomentumFailure> system =
        cfd::assembleCfdMomentum(hand.topology, other.geometry, boundary, input);
    REQUIRE_FALSE(system.has_value());
    CHECK(system.error().problem == CfdMomentumProblem::GeometryDescribesAnotherTopology);
    CHECK_THAT(system.error().error.message,
               Catch::Matchers::ContainsSubstring("different mesh generation"));
    const std::optional<CfdMomentumProblem> predicted =
        cfd::cfdMomentumProblem(hand.topology, other.geometry, boundary, input);
    REQUIRE(predicted.has_value());
    CHECK(*predicted == CfdMomentumProblem::GeometryDescribesAnotherTopology);
}

TEST_CASE("CfdMomentum_RefusesABoundaryPreparedForAnotherMesh", "[cfd][mom]") {
    // PHASE 2, FOR THE SAME REASON AND WITH THE SAME CONSEQUENCE.
    //
    // THIS REACHES THE BOUNDARY-FACE COUNT ARM ONLY. The conditional arm -- the
    // mesh stamp and the cell and boundary-face counts, compared only when the
    // boundary carries a non-default source -- needs a foreign boundary whose
    // face count MATCHES, which these two fixtures cannot provide (4 facets
    // against 6). That arm is recorded as UNREACHED here rather than claimed
    // covered, and it is the arm `P19-DIFF-001` made conditional and
    // `P19-CONV-001`'s first draft dropped altogether.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const Hand other = Hand::make(oneCellPoints(), kOneCell);
    const PreparedCfdBoundary foreign = other.walls();
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> velocity = uniformVelocity(cells, {0.0, 0.0, 0.0});
    const std::vector<double> flux = produceFlux(hand, hand.walls(), velocity);
    const CfdMomentumInput input = steadyInput(flux, velocity);
    REQUIRE(foreign.boundaryFaces() != hand.topology.boundaryFaces());

    std::expected<CfdMomentumSystem, CfdMomentumFailure> system =
        cfd::assembleCfdMomentum(hand.topology, hand.geometry, foreign, input);
    REQUIRE_FALSE(system.has_value());
    CHECK(system.error().problem == CfdMomentumProblem::BoundaryDescribesAnotherTopology);
    const std::optional<CfdMomentumProblem> predicted =
        cfd::cfdMomentumProblem(hand.topology, hand.geometry, foreign, input);
    REQUIRE(predicted.has_value());
    CHECK(*predicted == CfdMomentumProblem::BoundaryDescribesAnotherTopology);
}

// ---------------------------------------------------------------------------
// The deferred correction, which was never exercised through momentum at all
// ---------------------------------------------------------------------------

TEST_CASE("CfdMomentum_PassesTheFaceGradientThroughToTheDeferredCorrection", "[cfd][mom]") {
    // AN INDEPENDENT REVIEW FOUND THAT NO TEST EVER SUPPLIED A WELL-FORMED FACE
    // GRADIENT: it appeared only inside the pass-through helpers and as two
    // refusal fixtures. So dropping `input.faceGradient` from the ASSEMBLY's
    // `CfdDiffusionInput` while leaving the GATE's copy intact silently deleted
    // the non-orthogonal correction and passed the whole suite -- and ADR-063
    // finding F1 measured the reference meshes at 31 to 66 degrees mean
    // non-orthogonality, so that term is not negligible.
    //
    // THE TWO COPIES ARE THE HAZARD. `CfdConvectionInput` and
    // `CfdDiffusionInput` are each constructed twice in production, once in the
    // gate and once in the assembly, and nothing asserted that the two agreed.
    // This test watches the assembly's copy through its effect.
    const Hand hand = Hand::make(chainPoints(4), chainTets(4));
    const PreparedCfdBoundary boundary = hand.inlets(Velocity::fromSi(0.3));
    const std::size_t cells = hand.topology.cells();
    const std::size_t faces = hand.topology.faces();
    const std::vector<double> velocity = varyingVelocity(cells, 0.2);
    const std::vector<double> flux = produceFlux(hand, boundary, velocity);

    // A non-zero face gradient, discriminating in all three components.
    std::vector<cfd::VelocityGradient3> gradient;
    for (std::size_t f = 0; f < faces; ++f) {
        const double k = 1.0 + static_cast<double>(f);
        gradient.push_back(cfd::VelocityGradient3{cfd::VelocityGradient::fromSi(k),
                                                  cfd::VelocityGradient::fromSi(-0.5 * k),
                                                  cfd::VelocityGradient::fromSi(0.25 * k)});
    }

    const CfdMomentumInput without = steadyInput(flux, velocity);
    const CfdMomentumSystem bare = assemble(hand, boundary, without);
    CHECK_FALSE(bare.summary.diffusion.correctionApplied);

    CfdMomentumInput withInput = without;
    withInput.faceGradient = gradient;
    const CfdMomentumSystem corrected = assemble(hand, boundary, withInput);

    // THE CORRECTION IS REPORTED AS APPLIED, which is the flag ADR-073 L3 names
    // as how that limitation is reported.
    CHECK(corrected.summary.diffusion.correctionApplied);
    CHECK(corrected.summary.diffusion.deferredFraction > 0.0);

    // AND IT MOVED THE SOURCE, which is what makes the flag non-decorative. The
    // deferred arm is a SOURCE term, so the matrix must be untouched.
    CHECK(corrected.matrix == bare.matrix);
    CHECK(corrected.source != bare.source);

    // AND THE DECOMPOSITION STILL HOLDS BIT-FOR-BIT with the gradient present,
    // which is the assertion that pins the assembly's copy of the input to the
    // gate's.
    const CfdConvectionSystem convection = convectionOf(hand, boundary, withInput);
    const CfdDiffusionSystem diffusion = diffusionOf(hand, boundary, withInput);
    REQUIRE(diffusion.summary.correctionApplied);
    for (std::size_t s = 0; s < corrected.matrix.nonZeros(); ++s) {
        INFO("slot " << s);
        CHECK(corrected.matrix.values()[s] ==
              convection.matrix.values()[s] + diffusion.matrix.values()[s]);
    }
    for (std::size_t c = 0; c < cells; ++c) {
        INFO("cell " << c);
        CHECK(corrected.source[c] == convection.source[c] + diffusion.source[c]);
    }
}

// ---------------------------------------------------------------------------
// The two reported transient extrema, on the only fixture that can tell them
// apart
// ---------------------------------------------------------------------------

TEST_CASE("CfdMomentum_ReportsTheSmallestAndLargestTransientCoefficientInClosedForm",
          "[cfd][mom]") {
    // THE TWO FIELDS WERE ASSERTED ONLY AS `smallest > 0` AND
    // `largest >= smallest`, ON CHAIN FIXTURES WHOSE CELLS ARE ALL CONGRUENT.
    // An independent review worked out why that could not discriminate them:
    // `chainPoints` walks a screw motion, which is an isometry, so every cell
    // of every chain has the same volume to rounding and `min(rho V/dt)` and
    // `max(rho V/dt)` are indistinguishable at every tolerance this file uses.
    // Reporting the minimum for both fields, or the maximum for both, passed
    // the entire suite. That is the third variation in this repository of one
    // defect: an asserted quantity made independent of the thing under test by
    // a fixture in which the two compared values are equal.
    //
    // THE TWO-CELL PAIR IS THE ONLY FIXTURE WHOSE VOLUMES DIFFER, and the
    // premise is asserted rather than assumed.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();
    const std::size_t cells = hand.topology.cells();
    REQUIRE(cells == 2);
    const std::vector<double> still = uniformVelocity(cells, {0.0, 0.0, 0.0});
    const std::vector<double> flux = produceFlux(hand, boundary, still);
    const Time timeStep = Time::fromSi(0.004);

    double leastVolume = std::numeric_limits<double>::infinity();
    double mostVolume = 0.0;
    for (std::size_t c = 0; c < cells; ++c) {
        const double volume = hand.geometry.cellVolume(CellIndex::fromValue(c)).si();
        leastVolume = std::min(leastVolume, volume);
        mostVolume = std::max(mostVolume, volume);
    }
    INFO("volumes " << leastVolume << " and " << mostVolume);
    REQUIRE(leastVolume != mostVolume);

    CfdMomentumInput input = steadyInput(flux, still);
    input.timeStep = timeStep;
    input.previousVelocity = still;
    const CfdMomentumSystem system = assemble(hand, boundary, input);

    CHECK_THAT(system.summary.smallestTransientCoefficient.si(),
               Catch::Matchers::WithinRel(kDensity.si() * leastVolume / timeStep.si(), 1.0e-14));
    CHECK_THAT(system.summary.largestTransientCoefficient.si(),
               Catch::Matchers::WithinRel(kDensity.si() * mostVolume / timeStep.si(), 1.0e-14));
    // AND THEY ARE DISTINGUISHABLE ON THIS FIXTURE, so neither assertion above
    // can be satisfied by reporting the other field's value.
    CHECK(system.summary.smallestTransientCoefficient.si() <
          system.summary.largestTransientCoefficient.si());
}

// ---------------------------------------------------------------------------
// The source finiteness gate, tripped on its own
// ---------------------------------------------------------------------------

TEST_CASE("CfdMomentum_RefusesANonFiniteSourceWhileEveryCoefficientStaysFinite", "[cfd][mom]") {
    // THE ONLY HONEST JUSTIFICATION FOR TWO SEPARATE FINITENESS LOOPS, and an
    // independent review pointed out that nothing reached this one alone: the
    // transient-overflow test sets `rho = 1e300` and `dt = 1e-300`, which trips
    // the matrix gate, the source gate and the reported-extrema gate at once,
    // so no single gate was tested in isolation and deleting either loop left
    // the suite green.
    //
    // THE STEADY FORM IS THE KEY. With no time step the density does not enter
    // the matrix at all -- convection takes `phi` and diffusion takes `mu` --
    // so a huge density and a huge gravity overflow the BODY FORCE while every
    // coefficient stays finite.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> still = uniformVelocity(cells, {0.0, 0.0, 0.0});
    const std::vector<double> flux = produceFlux(hand, boundary, still);

    // THE HUGE COMPONENT MUST BE THE ONE BEING ASSEMBLED. My first attempt put
    // it on Z while `steadyInput` assembles X, so the body force was
    // `(rho V) * 0.0` -- finite, and the assembly succeeded. The test failed on
    // its own fixture and not on the operator, which is the second time in this
    // file a refusal was asserted against a fixture that could not reach it.
    CfdMomentumInput input = steadyInput(flux, still);
    REQUIRE(input.component == VectorComponent::X);
    input.density = Density::fromSi(1.0e300);
    input.gravity = CfdGravity{Acceleration3{Acceleration::fromSi(-1.0e300),
                                             Acceleration::fromSi(0.0), Acceleration::fromSi(0.0)},
                               0_m};
    REQUIRE(std::isfinite(input.density.si()));
    REQUIRE(std::isfinite(input.gravity->acceleration.x.si()));

    std::expected<CfdMomentumSystem, CfdMomentumFailure> system =
        cfd::assembleCfdMomentum(hand.topology, hand.geometry, boundary, input);
    REQUIRE_FALSE(system.has_value());
    CHECK(system.error().problem == CfdMomentumProblem::ResultIsNotFinite);
    CHECK_THAT(system.error().error.message, Catch::Matchers::ContainsSubstring("source at cell"));

    // AND THE MATRIX WOULD HAVE BEEN FINE, which is what makes this the source
    // gate and not the matrix gate: the same mesh and flux with an ordinary
    // density and gravity assembles, and the steady matrix does not depend on
    // either.
    CfdMomentumInput ordinary = steadyInput(flux, still);
    const CfdMomentumSystem fine = assemble(hand, boundary, ordinary);
    for (std::size_t s = 0; s < fine.matrix.nonZeros(); ++s) {
        CHECK(std::isfinite(fine.matrix.values()[s]));
    }
}

// ---------------------------------------------------------------------------
// Dominance pinned to a value, and the schemes shown to differ
// ---------------------------------------------------------------------------

TEST_CASE("CfdMomentum_CountsEveryNonDominantRowOnAFixtureBuiltToHaveThem", "[cfd][mom]") {
    // THE COUNT WAS NEVER PINNED TO A KNOWN VALUE. Its only two assertions were
    // `<= cells`, which is true of any count over `cells` rows, and a re-count
    // by the same predicate as production -- so `notDominant = 0;` as a
    // mutation survived unless the fixture happened to have a non-dominant row,
    // which nothing asserted. An independent review found both.
    //
    // THE FIXTURE IS BUILT TO HAVE THEM. On a one-cell tet with pressure
    // outlets and an all-inward hand-made `phi`, the single row has a negative
    // diagonal and no off-diagonal, so `|a_ii| >= 0` holds and it IS dominant:
    // that is the `== 0` case, asserted elsewhere. Here a CHAIN with
    // `Central` and a large flux gives rows whose off-diagonal sum exceeds
    // `|a_ii|` -- central differencing is unbounded above a cell Peclet number
    // of two, which is exactly the condition `CfdConvectionScheme` documents --
    // so the count is non-zero by construction and is asserted as such.
    const Hand hand = Hand::make(chainPoints(5), chainTets(5));
    const PreparedCfdBoundary boundary = hand.outlets();
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> velocity = uniformVelocity(cells, {0.0, 0.0, 0.0});

    // A large uniform internal flux: convection dominates diffusion by orders
    // of magnitude, which is the high-Peclet regime.
    std::vector<double> phi(hand.topology.faces(), 0.0);
    for (std::size_t f = 0; f < hand.topology.internalFaces(); ++f) {
        phi[f] = kDiscriminatingConstant;
    }

    CfdMomentumInput input = steadyInput(phi, velocity);
    input.scheme = CfdConvectionScheme::Central;
    const CfdMomentumSystem central = assemble(hand, boundary, input);

    // RE-DERIVED FROM THE ROW SUM, not by re-running production's predicate.
    // The transcription that was there is kept as well, in its own test, for
    // what it is worth -- it pins the reported number to the RETURNED matrix --
    // but the count's meaning is established here instead.
    std::size_t derived = 0;
    for (std::size_t r = 0; r < cells; ++r) {
        const double diagonal = std::abs(coeff(central.matrix, r, r));
        double off = 0.0;
        const auto first = static_cast<std::size_t>(central.matrix.rowStart()[r]);
        const auto last = static_cast<std::size_t>(central.matrix.rowStart()[r + 1]);
        for (std::size_t slot = first; slot < last; ++slot) {
            if (central.matrix.inner()[slot] != static_cast<cfd::CfdSparseIndex>(r)) {
                off += std::abs(central.matrix.values()[slot]);
            }
        }
        if (diagonal < off) {
            ++derived;
        }
    }
    INFO("non-dominant rows under Central: " << derived << " of " << cells);
    REQUIRE(derived > 0);
    CHECK(central.summary.rowsNotDiagonallyDominant == derived);

    SECTION("and the scheme actually changes the matrix, so the choice is read") {
        // WITHOUT THIS THE DECOMPOSITION TEST'S `Central` ITERATION PROVES
        // NOTHING: hard-coding `Upwind` in the assembly's convection input
        // would be caught only if the two schemes differ on the fixture, which
        // nothing asserted. `CfdConvectionTests.cpp` carries the same test for
        // the same reason.
        CfdMomentumInput upwind = input;
        upwind.scheme = CfdConvectionScheme::Upwind;
        const CfdMomentumSystem other = assemble(hand, boundary, upwind);
        CHECK_FALSE(central.matrix == other.matrix);
        CHECK(other.summary.convection.scheme == CfdConvectionScheme::Upwind);
        CHECK(central.summary.convection.scheme == CfdConvectionScheme::Central);
    }
}

TEST_CASE("CfdMomentum_RefusesANonFiniteCoefficientWhileEverySubMatrixStaysFinite", "[cfd][mom]") {
    // THE OTHER HALF OF THE TWO-LOOP JUSTIFICATION, AND IT EXISTS BECAUSE A
    // MUTATION PROBE SURVIVED. Deleting momentum's matrix finiteness loop left
    // the suite green: both reachable non-finite cases -- a transient overflow
    // and a body-force overflow -- also produce a non-finite SOURCE, so the
    // source loop fired with the same tag and the matrix loop was never the
    // only thing watching. A gate nothing would notice the removal of is not a
    // gate.
    //
    // THE ROUTE IS THE SUM, which is the one thing neither sub-operator can
    // see: each gates its own coefficients, and `1.7e308 + 1.7e308` is not
    // finite. ONE CELL, so there is exactly one diagonal and both terms must
    // land on it -- my second attempt used the two-cell pair, where both
    // matrices reached `1e308` on DIFFERENT slots (convection's on the internal
    // face's owner, diffusion's on whichever diagonal accumulated most) and the
    // sum overflowed nowhere.
    //
    // THE SOURCE STAYS FINITE because every facet is a no-slip wall: the
    // prescribed wall velocity is zero, so diffusion's `Gamma * U_b` is zero,
    // and an OUTFLOW face's flux goes to the diagonal rather than the source.
    // The steady form is used, so there is no transient term to overflow.
    const Hand hand = Hand::make(metreScalePoints(), kOneCell);
    const PreparedCfdBoundary boundary = hand.walls();
    const std::size_t cells = hand.topology.cells();
    REQUIRE(cells == 1);
    REQUIRE(hand.topology.internalFaces() == 0);
    const std::vector<double> still = uniformVelocity(cells, {0.0, 0.0, 0.0});

    // A huge flux on EXACTLY ONE face. Spreading it over all four would make
    // convection's own diagonal -- which accumulates them -- overflow, and
    // convection would refuse before momentum saw anything.
    constexpr double kHuge = 1.7e308;
    std::vector<double> phi(hand.topology.faces(), 0.0);
    REQUIRE(hand.topology.faces() >= 1);
    phi[hand.topology.faces() - 1] = kHuge;

    // THE VISCOSITY IS CALIBRATED FROM A MEASUREMENT, NOT COMPUTED FROM THE
    // GEOMETRY. My first attempt set `mu = 1e308 / (faceArea/orthogonalDistance)`
    // for one face, and DIFFUSION'S OWN MATRIX overflowed: its diagonal
    // accumulates `Gamma` over all four of a cell's faces, so the per-face
    // figure understated it by about a factor of four and the sub-assembly
    // refused. The premise here is that both sub-matrices are FINITE, so the
    // scale must come from the quantity that has to stay finite.
    CfdMomentumInput probe = steadyInput(phi, still);
    probe.viscosity = DynamicViscosity::fromSi(1.0);
    const double unitScale = diffusionOf(hand, boundary, probe).matrix.largestMagnitude();
    INFO("diffusion's largest coefficient at mu = 1 Pa s: " << unitScale);
    REQUIRE(unitScale > 0.0);

    CfdMomentumInput input = steadyInput(phi, still);
    input.viscosity = DynamicViscosity::fromSi(kHuge / unitScale);
    REQUIRE(std::isfinite(input.viscosity.si()));
    REQUIRE(input.viscosity.si() > 0.0);

    // BOTH SUB-ASSEMBLIES SUCCEED AND BOTH MATRICES ARE FINITE, which is the
    // premise and is asserted rather than assumed.
    const CfdConvectionSystem convection = convectionOf(hand, boundary, input);
    const CfdDiffusionSystem diffusion = diffusionOf(hand, boundary, input);
    for (std::size_t s = 0; s < convection.matrix.nonZeros(); ++s) {
        REQUIRE(std::isfinite(convection.matrix.values()[s]));
        REQUIRE(std::isfinite(diffusion.matrix.values()[s]));
    }
    INFO("convection max " << convection.matrix.largestMagnitude() << ", diffusion max "
                           << diffusion.matrix.largestMagnitude());
    bool sumOverflows = false;
    for (std::size_t s = 0; s < convection.matrix.nonZeros(); ++s) {
        sumOverflows = sumOverflows ||
                       !std::isfinite(convection.matrix.values()[s] + diffusion.matrix.values()[s]);
    }
    REQUIRE(sumOverflows);
    // And every source entry of both is finite, so the SOURCE loop cannot be
    // what refuses this.
    for (std::size_t c = 0; c < cells; ++c) {
        REQUIRE(std::isfinite(convection.source[c] + diffusion.source[c]));
    }

    std::expected<CfdMomentumSystem, CfdMomentumFailure> system =
        cfd::assembleCfdMomentum(hand.topology, hand.geometry, boundary, input);
    REQUIRE_FALSE(system.has_value());
    CHECK(system.error().problem == CfdMomentumProblem::ResultIsNotFinite);
    CHECK_THAT(system.error().error.message,
               Catch::Matchers::ContainsSubstring("coefficient at slot"));
    // The gate accepted the input: no input phase can see a sum.
    CHECK_FALSE(
        cfd::cfdMomentumProblem(hand.topology, hand.geometry, boundary, input).has_value());
}
