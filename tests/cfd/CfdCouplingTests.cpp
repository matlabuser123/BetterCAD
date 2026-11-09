/// \file
/// P19-PVCOUPLE-001 — the SIMPLE pressure–velocity coupling. ADR-075.
///
/// WHAT THIS SUITE CAN AND CANNOT PROVE, stated first so a reader is not misled
/// by the number of assertions.
///
/// This operator contains no linear solver, so every claim about a SOLVED field
/// is conditional on the solve this suite supplies. What is NOT conditional, and
/// what carries the milestone's falsifiable content:
///
/// ```text
/// (a) THE IDENTITY. After the face-flux correction, each cell's flux sum is
///     the pressure system's own residual, `(A p') - b`, exactly. This is
///     checkable from one `advanceCfdCoupling` call because the iteration keeps
///     both `p'` and the system, and it is asserted for a DELIBERATELY WRONG
///     `p'` as well -- which is the stronger half, because it shows the identity
///     is structural and not an artifact of `p'` being right.
/// (b) SYMMETRY, EXACTLY. One `a_f` per face written to both rows means
///     `largestSymmetryError()` is 0.0 and not merely small. ADR-075 Q1
///     measured the per-cell alternative at 57.3703% bit-identical, so this is
///     a real property and not a restatement of the loop shape.
/// (c) THE CLOSURE BOUND IS DERIVED. Continuity after correction cannot beat
///     the pressure solve, so the bound moves with
///     `relativeResidualTolerance`. A test drives the supplied solve to a
///     deliberately poor residual and watches the bound follow.
/// (d) TWO ANALYTIC BENCHMARKS. A closed domain at rest is an exact fixed
///     point -- `p'` is zero to the last bit and the velocity does not move --
///     and a duct with an inlet and an outlet must conserve mass globally at
///     convergence. Neither reads anything from the implementation.
/// ```
///
/// THE SOLVE IS WRITTEN IN THIS FILE, by dense Gaussian elimination with
/// partial pivoting, and shares no line with any operator under test. That is
/// what makes it a reference rather than a second opinion from the same source:
/// `P19-GRAD-001`'s r5 and `P19-CONV-001`'s second review both found an
/// "independent" oracle that was a character-for-character transcription of
/// production.
///
/// THE FIXTURES ARE A SEVENTH COPY, AND THAT IS STATED RATHER THAN HIDDEN.
/// `buildMesh`, `struct Hand` and their neighbours exist in the diffusion,
/// convection, gradient, divergence, momentum and continuity suites too. No
/// shared cfd fixture header exists; creating one means editing six test files
/// that belong to qualified milestones, which is a refactor combined with
/// feature work. It is recorded as a limitation with a named follow-up.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"

#include <bettercad/cfd/CfdBoundary.hpp>
#include <bettercad/cfd/CfdContinuity.hpp>
#include <bettercad/cfd/CfdConvection.hpp>
#include <bettercad/cfd/CfdCoupling.hpp>
#include <bettercad/cfd/CfdLinearSolve.hpp>
#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdDiffusion.hpp>
#include <bettercad/cfd/CfdGeometry.hpp>
#include <bettercad/cfd/CfdGradient.hpp>
#include <bettercad/cfd/CfdMesh.hpp>
#include <bettercad/cfd/CfdMomentum.hpp>
#include <bettercad/cfd/CfdTopology.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/math/Direction.hpp>
#include <bettercad/core/units/Literals.hpp>
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
using cfd::CfdBoundaryPolicy;
using cfd::CfdBoundaryTreatment;
using cfd::CfdConvectionScheme;
using cfd::CfdConvergenceSettings;
using cfd::CfdCouplingCorrection;
using cfd::CfdCouplingCorrectionInput;
using cfd::CfdCouplingFailure;
using cfd::CfdCouplingIteration;
using cfd::CfdCouplingIterationInput;
using cfd::CfdCouplingPredictor;
using cfd::CfdCouplingPredictorInput;
using cfd::CfdCouplingProblem;
using cfd::CfdCouplingVerdict;
using cfd::CfdGeometry;
using cfd::CfdIterationSettings;
using cfd::CfdLinearSolve;
using cfd::CfdLinearSolveResult;
using cfd::CfdLinearSolverSettings;
using cfd::CfdMassFluxInput;
using cfd::CfdMomentumInput;
using cfd::CfdMomentumSystem;
using cfd::CfdPressureCorrectionInput;
using cfd::CfdPressureCorrectionSystem;
using cfd::CfdResiduals;
using cfd::CfdResolvedTarget;
using cfd::CfdSparseMatrix;
using cfd::CfdSteadySettings;
using cfd::CfdTopology;
using cfd::FaceIndex;
using cfd::MovingWall;
using cfd::NoSlipWall;
using cfd::PreparedCfdBoundary;
using cfd::PressureInlet;
using cfd::PressureOutlet;
using cfd::SlipWall;
using cfd::VectorComponent;
using cfd::VelocityInlet;
using meshing::ElementId;
using meshing::NodeId;

namespace {

constexpr auto kRegion = meshing::RegionId::fromValue(1);

/// Water, and a viscosity to match. Neither is a power of two.
const Density kDensity = Density::fromSi(998.2);
const DynamicViscosity kViscosity = DynamicViscosity::fromSi(1.002e-3);

/// A CONSTANT NO POWER OF TWO DIVIDES, so a mutation that drops or doubles a
/// factor cannot hide behind exact binary arithmetic.
constexpr double kDiscriminatingConstant = 12345.6789;

constexpr std::array<std::array<std::size_t, 3>, 4> kFaces{
    {{0, 2, 1}, {0, 1, 3}, {1, 2, 3}, {0, 3, 2}}};

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
            REQUIRE(
                builder.addTriangle({nodes[wound[0]], nodes[wound[1]], nodes[wound[2]]}, kRegion));
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

/// A chain of @p cells tetrahedra, each sharing one face with the next.
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

/// A chain whose cells differ markedly in size, so the face interpolation
/// weight is genuinely away from one half.
///
/// WHY THIS EXISTS. A mutation probe reversed `interpolate` -- the exact `1 - w`
/// defect `ADR-075` Q1 was written about -- and SURVIVED. On the uniform chain
/// every internal face's weight is one half to within a bit or two, and at
/// `w = 0.5` the two forms agree exactly, so no assertion could see the
/// difference. The suite's premise check ("some weight is not exactly 0.5") was
/// satisfied by floating-point noise rather than by geometry, which is the
/// vacuous-premise pattern this milestone's own test review taught.
[[nodiscard]] std::vector<Point3D> gradedChainPoints(std::size_t cells) {
    std::vector<Point3D> out;
    const double radius = 0.01;
    double z = 0.0;
    for (std::size_t k = 0; k < cells + 3; ++k) {
        const double angle = 2.0 * std::numbers::pi * static_cast<double>(k) / 3.0;
        out.push_back(Point3D{Length::fromSi(radius * std::cos(angle)),
                              Length::fromSi(radius * std::sin(angle)), Length::fromSi(z)});
        // Each step is 70% longer than the last, so consecutive tetrahedra
        // differ in size by a large factor and the face centroid sits well off
        // the midpoint between the two cell centroids.
        z += 0.004 * std::pow(1.7, static_cast<double>(k));
    }
    return out;
}

[[nodiscard]] std::vector<std::array<std::size_t, 4>> chainTets(std::size_t cells) {
    std::vector<std::array<std::size_t, 4>> out;
    for (std::size_t k = 0; k < cells; ++k) {
        out.push_back({k, k + 1, k + 2, k + 3});
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

    [[nodiscard]] PreparedCfdBoundary bindTargets(std::span<const CfdResolvedTarget> targets,
                                                  bool anyFixesPressure) const {
        const std::optional<cfd::CfdReferencePressure> datum =
            anyFixesPressure ? std::nullopt
                             : std::optional<cfd::CfdReferencePressure>{cfd::CfdReferencePressure{
                                   .value = 0_Pa, .location = insidePoint()}};
        Result<PreparedCfdBoundary> bound = cfd::bindCfdBoundary(
            topology, geometry, mesh, targets, 0, datum, std::nullopt, std::nullopt);
        INFO((bound.has_value() ? std::string{} : bound.error().message));
        REQUIRE(bound.has_value());
        return std::move(*bound);
    }

    [[nodiscard]] PreparedCfdBoundary uniform(const CfdBoundaryCondition& condition) const {
        std::vector<ElementId> ascending = facets();
        std::ranges::sort(ascending, [](ElementId a, ElementId b) { return a.value() < b.value(); });
        REQUIRE(std::ranges::adjacent_find(ascending) == ascending.end());
        const CfdResolvedTarget target{condition, ascending};
        const std::array<CfdResolvedTarget, 1> targets{target};
        return bindTargets(targets, cfd::treatmentOf(condition.kind()).pressure ==
                                        CfdBoundaryPolicy::FixedValue);
    }

    /// @p first on the facet of @p boundaryFace and @p second on all the rest.
    [[nodiscard]] PreparedCfdBoundary oneFacetAt(BoundaryFaceIndex boundaryFace,
                                                 const CfdBoundaryCondition& first,
                                                 const CfdBoundaryCondition& second) const {
        const ElementId chosen = topology.triangleOf(boundaryFace);
        std::vector<ElementId> rest;
        for (const ElementId facet : facets()) {
            if (facet.value() != chosen.value()) {
                rest.push_back(facet);
            }
        }
        std::ranges::sort(rest, [](ElementId a, ElementId b) { return a.value() < b.value(); });
        const std::vector<ElementId> one{chosen};
        const std::array<CfdResolvedTarget, 2> targets{CfdResolvedTarget{first, one},
                                                       CfdResolvedTarget{second, rest}};
        const bool fixes = cfd::treatmentOf(first.kind()).pressure == CfdBoundaryPolicy::FixedValue ||
                           cfd::treatmentOf(second.kind()).pressure == CfdBoundaryPolicy::FixedValue;
        return bindTargets(targets, fixes);
    }

    /// @p first on exactly ONE facet and @p second on all the rest.
    ///
    /// NEEDED BECAUSE A MOVING WALL REFUSES A NON-PLANAR TARGET.
    /// `bindCfdBoundary` says "tangency has no meaning on a curved target, so
    /// it is refused rather than approximated", and a tetrahedral boundary is
    /// never planar as a whole. ONE facet is planar by construction. The first
    /// form of the moving-wall test bound it to every facet and was refused by
    /// the boundary preparation before this operator saw anything.
    [[nodiscard]] PreparedCfdBoundary oneFacetPlusRest(const CfdBoundaryCondition& first,
                                                       const CfdBoundaryCondition& second) const {
        std::vector<ElementId> ascending = facets();
        std::ranges::sort(ascending, [](ElementId a, ElementId b) { return a.value() < b.value(); });
        REQUIRE(ascending.size() >= 2);
        const std::vector<ElementId> one{ascending.front()};
        const std::vector<ElementId> rest(ascending.begin() + 1, ascending.end());
        const std::array<CfdResolvedTarget, 2> targets{CfdResolvedTarget{first, one},
                                                       CfdResolvedTarget{second, rest}};
        const bool fixes = cfd::treatmentOf(first.kind()).pressure == CfdBoundaryPolicy::FixedValue ||
                           cfd::treatmentOf(second.kind()).pressure == CfdBoundaryPolicy::FixedValue;
        return bindTargets(targets, fixes);
    }

    /// Two conditions, the boundary facets dealt between them.
    [[nodiscard]] PreparedCfdBoundary mixed(const CfdBoundaryCondition& first,
                                            const CfdBoundaryCondition& second) const {
        std::vector<ElementId> ascending = facets();
        std::ranges::sort(ascending, [](ElementId a, ElementId b) { return a.value() < b.value(); });
        REQUIRE(ascending.size() >= 2);
        std::vector<ElementId> a;
        std::vector<ElementId> b;
        for (std::size_t k = 0; k < ascending.size(); ++k) {
            (k % 2 == 0 ? a : b).push_back(ascending[k]);
        }
        const std::array<CfdResolvedTarget, 2> targets{CfdResolvedTarget{first, a},
                                                       CfdResolvedTarget{second, b}};
        const bool fixes = cfd::treatmentOf(first.kind()).pressure == CfdBoundaryPolicy::FixedValue ||
                           cfd::treatmentOf(second.kind()).pressure == CfdBoundaryPolicy::FixedValue;
        return bindTargets(targets, fixes);
    }
};

[[nodiscard]] FaceName someFace(std::uint64_t feature = 1) {
    return FaceName{ObjectId::fromValue(feature), FaceSelector{.role = FaceRole::EndCap}};
}

[[nodiscard]] CfdBoundaryCondition wall() {
    return CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace(1)}};
}

[[nodiscard]] CfdBoundaryCondition inlet(double speed) {
    return CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(2),
                                VelocityInlet{someFace(2), Velocity::fromSi(speed)}};
}

[[nodiscard]] CfdBoundaryCondition outlet(double pressure) {
    return CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(3),
                                PressureOutlet{someFace(3), Pressure::fromSi(pressure)}};
}

// ---------------------------------------------------------------------------
// THE INDEPENDENT SOLVE
//
// Dense Gaussian elimination with partial pivoting, written here, sharing no
// line with any operator under test. It also computes the residual in the form
// `CfdResiduals` is documented in, so the port reports a number this operator
// can act on.
// ---------------------------------------------------------------------------

[[nodiscard]] std::vector<std::vector<double>> dense(const CfdSparseMatrix& matrix) {
    std::vector<std::vector<double>> a(matrix.rows(), std::vector<double>(matrix.columns(), 0.0));
    const std::span<const cfd::CfdSparseIndex> rowStart = matrix.rowStart();
    const std::span<const cfd::CfdSparseIndex> inner = matrix.inner();
    const std::span<const double> values = matrix.values();
    for (std::size_t row = 0; row + 1 < rowStart.size(); ++row) {
        for (cfd::CfdSparseIndex slot = rowStart[row]; slot < rowStart[row + 1]; ++slot) {
            a[row][static_cast<std::size_t>(inner[static_cast<std::size_t>(slot)])] =
                values[static_cast<std::size_t>(slot)];
        }
    }
    return a;
}

[[nodiscard]] double infinityNormOf(const CfdSparseMatrix& matrix) {
    const std::span<const cfd::CfdSparseIndex> rowStart = matrix.rowStart();
    const std::span<const double> values = matrix.values();
    double worst = 0.0;
    for (std::size_t row = 0; row + 1 < rowStart.size(); ++row) {
        double sum = 0.0;
        for (cfd::CfdSparseIndex slot = rowStart[row]; slot < rowStart[row + 1]; ++slot) {
            sum += std::abs(values[static_cast<std::size_t>(slot)]);
        }
        worst = std::max(worst, sum);
    }
    return worst;
}

[[nodiscard]] double norm2(std::span<const double> v) {
    double sum = 0.0;
    for (const double x : v) {
        sum += x * x;
    }
    return std::sqrt(sum);
}

/// `A x - b`, computed with this file's own dense matrix.
[[nodiscard]] std::vector<double> residualOf(const CfdSparseMatrix& matrix,
                                             std::span<const double> rhs,
                                             std::span<const double> x) {
    const std::vector<std::vector<double>> a = dense(matrix);
    std::vector<double> r(matrix.rows(), 0.0);
    for (std::size_t i = 0; i < matrix.rows(); ++i) {
        double sum = 0.0;
        for (std::size_t j = 0; j < matrix.columns(); ++j) {
            sum += a[i][j] * x[j];
        }
        r[i] = sum - rhs[i];
    }
    return r;
}

/// A dense direct solve whose reported residual is REPLACED by @p value.
///
/// THE SCALED FORM COULD NOT DO THIS, and the test that needed it said so: the
/// dense solve's own residual is at or near zero, so multiplying it by 1e12
/// still clears a 1e-9 tolerance. A deliberately poor REPORT has to be stated,
/// not scaled. The answer itself is untouched, which is the point: ADR-052's
/// rule is that a solver saying "Success" is never sufficient evidence, so a
/// right answer with a bad residual must still be refused.
[[nodiscard]] CfdLinearSolve reportingResidual(double value);

/// A dense direct solve. `scale` multiplies the reported residual.
[[nodiscard]] CfdLinearSolve denseSolve(double reportedResidualScale = 1.0) {
    return [reportedResidualScale](const CfdSparseMatrix& matrix, std::span<const double> rhs,
                                   const CfdLinearSolverSettings&)
               -> std::expected<CfdLinearSolveResult, Error> {
        const std::size_t n = matrix.rows();
        std::vector<std::vector<double>> a = dense(matrix);
        std::vector<double> b(rhs.begin(), rhs.end());
        for (std::size_t column = 0; column < n; ++column) {
            std::size_t pivot = column;
            for (std::size_t row = column + 1; row < n; ++row) {
                if (std::abs(a[row][column]) > std::abs(a[pivot][column])) {
                    pivot = row;
                }
            }
            if (a[pivot][column] == 0.0) {
                return std::unexpected(
                    Error{ErrorCode::InvalidArgument, "the dense reference found a zero pivot"});
            }
            std::swap(a[column], a[pivot]);
            std::swap(b[column], b[pivot]);
            for (std::size_t row = column + 1; row < n; ++row) {
                const double factor = a[row][column] / a[column][column];
                if (factor == 0.0) {
                    continue;
                }
                for (std::size_t k = column; k < n; ++k) {
                    a[row][k] -= factor * a[column][k];
                }
                b[row] -= factor * b[column];
            }
        }
        std::vector<double> x(n, 0.0);
        for (std::size_t i = n; i-- > 0;) {
            double sum = b[i];
            for (std::size_t j = i + 1; j < n; ++j) {
                sum -= a[i][j] * x[j];
            }
            x[i] = sum / a[i][i];
        }
        const std::vector<double> r = residualOf(matrix, rhs, x);
        const double denominator = infinityNormOf(matrix) * norm2(x) + norm2(rhs);
        const double normalised = denominator > 0.0 ? norm2(r) / denominator : 0.0;
        return CfdLinearSolveResult{std::move(x), normalised * reportedResidualScale};
    };
}

[[nodiscard]] CfdLinearSolve reportingResidual(double value) {
    return [value](const CfdSparseMatrix& matrix, std::span<const double> rhs,
                   const CfdLinearSolverSettings& settings)
               -> std::expected<CfdLinearSolveResult, Error> {
        std::expected<CfdLinearSolveResult, Error> solved = denseSolve()(matrix, rhs, settings);
        if (!solved.has_value()) {
            return solved;
        }
        solved->normalizedResidual = value;
        return solved;
    };
}

// ---------------------------------------------------------------------------
// Convenience
// ---------------------------------------------------------------------------

[[nodiscard]] std::vector<double> zeros(std::size_t n) { return std::vector<double>(n, 0.0); }

[[nodiscard]] std::vector<double> producedFlux(const Hand& hand, const PreparedCfdBoundary& boundary,
                                               std::span<const double> velocity) {
    const CfdMassFluxInput input{.currentVelocity = velocity, .density = kDensity};
    std::expected<std::vector<double>, cfd::CfdConvectionFailure> flux =
        cfd::cfdMassFlux(hand.topology, hand.geometry, boundary, input);
    INFO((flux.has_value() ? std::string{} : flux.error().error.message));
    REQUIRE(flux.has_value());
    return *flux;
}

[[nodiscard]] std::vector<double> unitDiagonal(const Hand& hand, double value) {
    return std::vector<double>(3 * hand.topology.cells(), value);
}

[[nodiscard]] CfdPressureCorrectionSystem assemble(const Hand& hand,
                                                   const PreparedCfdBoundary& boundary,
                                                   std::span<const double> flux,
                                                   std::span<const double> diagonal) {
    const CfdPressureCorrectionInput input{
        .predictedFaceFlux = flux, .momentumDiagonal = diagonal, .density = kDensity};
    std::expected<CfdPressureCorrectionSystem, CfdCouplingFailure> system =
        cfd::assembleCfdPressureCorrection(hand.topology, hand.geometry, boundary, input);
    INFO((system.has_value() ? std::string{} : system.error().error.message));
    REQUIRE(system.has_value());
    return std::move(*system);
}

[[nodiscard]] CfdCouplingFailure refusal(const Hand& hand, const PreparedCfdBoundary& boundary,
                                         const CfdPressureCorrectionInput& input) {
    std::expected<CfdPressureCorrectionSystem, CfdCouplingFailure> system =
        cfd::assembleCfdPressureCorrection(hand.topology, hand.geometry, boundary, input);
    REQUIRE_FALSE(system.has_value());
    return system.error();
}

/// The three momentum systems of a steady, pressure-free assembly.
/// The pressure gradient of @p pressure, from the QUALIFIED operator.
///
/// LEGITIMATE FOR `p` AND NOT FOR `p-prime`: this is the real pressure field,
/// whose prescribed boundary value IS the right one to impose. The pressure
/// CORRECTION is zero at those facets, which is why the coupling does not use
/// this operator for the correction (ADR-075 Decision 12).
[[nodiscard]] std::vector<cfd::PressureGradient3> pressureGradientOf(
    const Hand& hand, const PreparedCfdBoundary& boundary, std::span<const double> pressure) {
    const cfd::CfdGradientInput input{.method = cfd::CfdGradientMethod::LeastSquares,
                                      .cellValue = pressure};
    Result<cfd::CfdPressureGradientField> field =
        cfd::reconstructCfdPressureGradient(hand.topology, hand.geometry, boundary, input);
    INFO((field.has_value() ? std::string{} : field.error().message));
    REQUIRE(field.has_value());
    std::vector<cfd::PressureGradient3> out;
    out.reserve(hand.topology.cells());
    for (std::size_t c = 0; c < hand.topology.cells(); ++c) {
        const std::optional<cfd::CfdPressureGradientField::Vector> g =
            field->at(CellIndex::fromValue(c));
        REQUIRE(g.has_value());
        out.push_back(*g);
    }
    return out;
}

[[nodiscard]] std::vector<CfdMomentumSystem> momentumSystems(
    const Hand& hand, const PreparedCfdBoundary& boundary, std::span<const double> flux,
    std::span<const double> velocity,
    std::span<const cfd::PressureGradient3> pressureGradient = {}) {
    std::vector<CfdMomentumSystem> out;
    for (const VectorComponent component : cfd::kVectorComponents) {
        CfdMomentumInput input;
        input.density = kDensity;
        input.viscosity = kViscosity;
        input.component = component;
        input.faceFlux = flux;
        input.scheme = CfdConvectionScheme::Upwind;
        input.currentVelocity = velocity;
        input.pressureGradient = pressureGradient;
        std::expected<CfdMomentumSystem, cfd::CfdMomentumFailure> system =
            cfd::assembleCfdMomentum(hand.topology, hand.geometry, boundary, input);
        INFO((system.has_value() ? std::string{} : system.error().error.message));
        REQUIRE(system.has_value());
        out.push_back(std::move(*system));
    }
    return out;
}

/// `sum_f phi_f` per cell, computed here by this file's own face walk.
[[nodiscard]] std::vector<double> divergenceOf(const CfdTopology& topology,
                                               std::span<const double> flux) {
    std::vector<double> out(topology.cells(), 0.0);
    for (std::size_t f = 0; f < topology.faces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        out[topology.owner(face).value()] += flux[f];
        if (const std::optional<CellIndex> neighbour = topology.neighbour(face)) {
            out[neighbour->value()] -= flux[f];
        }
    }
    return out;
}

} // namespace

// ===========================================================================
// The premises this operator rests on, asserted rather than trusted
// ===========================================================================

TEST_CASE("CfdCoupling_TheTreatmentTableFixesEitherTheFluxOrThePressureAtEveryKind",
          "[cfd][pvc]") {
    // ADR-075 Decision 3. The pressure-correction boundary condition is read
    // off the `pressure` column, and the equation is OVER-determined at a
    // boundary that fixed both and UNDER-determined at one that fixed neither.
    // So the exclusivity is asserted over the CANONICAL array, not over a list
    // written here.
    //
    // THIS ALSO CAUGHT A NAME: the header's table prints `SlipWall` and the
    // enumerator is `Slip`.
    for (const CfdBoundaryConditionKind kind : cfd::kCfdBoundaryConditionKinds) {
        const CfdBoundaryTreatment treatment = cfd::treatmentOf(kind);
        INFO("kind " << cfd::toString(kind));
        const bool pressureFixed = treatment.pressure == CfdBoundaryPolicy::FixedValue;
        const bool fluxFixed = treatment.massFlux == CfdBoundaryPolicy::FixedValue;
        CHECK(pressureFixed != fluxFixed);
        // And the other half of the same fact: a computed flux is exactly where
        // the pressure is prescribed.
        CHECK((treatment.massFlux == CfdBoundaryPolicy::Computed) == pressureFixed);
        // `Symmetry` never appears in the pressure column.
        CHECK(treatment.pressure != CfdBoundaryPolicy::Symmetry);
    }
}

TEST_CASE("CfdCoupling_EveryFaceNormalHasAUnitSquaredComponentSum", "[cfd][pvc]") {
    // ADR-075 Decision 11. The normal-projected diagonal reduces exactly to the
    // isotropic case BECAUSE `sum_i n_i^2 == 1`. That is an identity of a unit
    // vector and it is measured here on a real mesh rather than assumed.
    const Hand hand = Hand::make(chainPoints(8), chainTets(8));
    double worst = 0.0;
    for (std::size_t f = 0; f < hand.topology.faces(); ++f) {
        const std::optional<Direction3D> normal =
            hand.geometry.faceUnitNormal(FaceIndex::fromValue(f));
        REQUIRE(normal.has_value());
        const double sum = normal->x() * normal->x() + normal->y() * normal->y() +
                           normal->z() * normal->z();
        worst = std::max(worst, std::abs(sum - 1.0));
    }
    INFO("worst |sum n_i^2 - 1| = " << worst);
    CHECK(worst <= cfd::kCfdCouplingNormalTolerance);
}

// ===========================================================================
// The pressure-correction system
// ===========================================================================

TEST_CASE("CfdCoupling_ThePressureMatrixIsSymmetricToTheLastBit", "[cfd][pvc]") {
    // ADR-075 Decision 2. One `a_f` per face written to both rows, so this is
    // EXACTLY zero and not merely under the tolerance. ADR-075 Q1 measured a
    // per-cell assembly at 57.3703% bit-identical, worst relative 1.241051e-14,
    // and `linalg::solveSymmetricSparse` requires symmetry.
    const Hand hand = Hand::make(chainPoints(6), chainTets(6));
    const PreparedCfdBoundary boundary = hand.uniform(wall());
    const std::vector<double> flux = zeros(hand.topology.faces());
    const CfdPressureCorrectionSystem system =
        assemble(hand, boundary, flux, unitDiagonal(hand, kDiscriminatingConstant));

    CHECK(system.matrix.largestSymmetryError() == 0.0);
    CHECK(system.summary.largestSymmetryError == 0.0);
}

TEST_CASE("CfdCoupling_TheDiagonalIsPositiveAndTheOffDiagonalsAreNegative", "[cfd][pvc]") {
    const Hand hand = Hand::make(chainPoints(6), chainTets(6));
    const PreparedCfdBoundary boundary = hand.mixed(inlet(0.1), outlet(0.0));
    const std::vector<double> velocity = zeros(3 * hand.topology.cells());
    const std::vector<double> flux = producedFlux(hand, boundary, velocity);
    const CfdPressureCorrectionSystem system =
        assemble(hand, boundary, flux, unitDiagonal(hand, kDiscriminatingConstant));

    for (std::size_t row = 0; row < system.matrix.rows(); ++row) {
        INFO("row " << row);
        CHECK(system.matrix.coeffSi(row, row) > 0.0);
        for (std::size_t column = 0; column < system.matrix.columns(); ++column) {
            if (column == row) {
                continue;
            }
            CHECK(system.matrix.coeffSi(row, column) <= 0.0);
        }
    }
}

TEST_CASE("CfdCoupling_AZeroGradientBoundaryLeavesTheRowSumAtZero", "[cfd][pvc]") {
    // Every boundary a wall: every face is zero-gradient in pressure, so the
    // matrix is a pure Laplacian and every row sums to zero. That is the
    // constant mode, and it is why a reference cell must pin it.
    const Hand hand = Hand::make(chainPoints(6), chainTets(6));
    const PreparedCfdBoundary boundary = hand.uniform(wall());
    const std::vector<double> flux = zeros(hand.topology.faces());
    const CfdPressureCorrectionSystem system =
        assemble(hand, boundary, flux, unitDiagonal(hand, kDiscriminatingConstant));

    CHECK(system.summary.boundaryFacesFixedPressure == 0);
    CHECK(system.summary.boundaryFacesZeroGradient == hand.topology.boundaryFaces());
    CHECK(system.summary.constantModeWasInTheNullSpace);

    // THE ROW SUM VANISHES AWAY FROM THE PINNED CELL, AND NOT AT ITS
    // NEIGHBOURS -- which is elimination working, not a defect.
    //
    // TWO EARLIER FORMS OF THIS CHECK WERE WRONG AND BOTH ARE RECORDED. The
    // first used an ABSOLUTE 1e-9 bound and failed at 2.12e-09. The second made
    // it relative and still failed, at HALF the row's largest entry -- which is
    // the real answer: a neighbour of the pinned cell lost its off-diagonal
    // when that column was cleared, so its row no longer cancels. The row sum
    // is then the magnitude of the coefficient that was removed, which is a
    // stronger claim than cancellation and is asserted as such below.
    REQUIRE(system.summary.pinnedCell.has_value());
    const std::size_t pinned = system.summary.pinnedCell->value();

    // Which cells share an internal face with the pinned cell.
    std::vector<bool> adjacent(system.matrix.rows(), false);
    for (std::size_t f = 0; f < hand.topology.internalFaces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        const std::size_t owner = hand.topology.owner(face).value();
        const std::size_t neighbour = hand.topology.neighbour(face)->value();
        if (owner == pinned) {
            adjacent[neighbour] = true;
        } else if (neighbour == pinned) {
            adjacent[owner] = true;
        }
    }

    std::size_t cancelling = 0;
    std::size_t eliminated = 0;
    for (std::size_t row = 0; row < system.matrix.rows(); ++row) {
        if (row == pinned) {
            continue;
        }
        double sum = 0.0;
        double largest = 0.0;
        for (std::size_t column = 0; column < system.matrix.columns(); ++column) {
            const double entry = system.matrix.coeffSi(row, column);
            sum += entry;
            largest = std::max(largest, std::abs(entry));
        }
        INFO("row " << row << " sum " << sum << " largest |entry| " << largest
                    << (adjacent[row] ? " (pinned-adjacent)" : ""));
        if (adjacent[row]) {
            // The removed coefficient was negative, so the row now sums to a
            // POSITIVE residue no larger than the row's largest entry.
            CHECK(sum > 0.0);
            CHECK(sum <= largest);
            ++eliminated;
        } else {
            CHECK(std::abs(sum) <= 1.0e-12 * std::max(largest, 1.0e-300));
            ++cancelling;
        }
    }
    // Both cases are present, so neither branch is vacuous.
    CHECK(cancelling > 0);
    CHECK(eliminated > 0);
}

TEST_CASE("CfdCoupling_AFixedPressureBoundaryLiftsTheConstantMode", "[cfd][pvc]") {
    const Hand hand = Hand::make(chainPoints(6), chainTets(6));
    const PreparedCfdBoundary boundary = hand.mixed(inlet(0.1), outlet(0.0));
    const std::vector<double> velocity = zeros(3 * hand.topology.cells());
    const std::vector<double> flux = producedFlux(hand, boundary, velocity);
    const CfdPressureCorrectionSystem system =
        assemble(hand, boundary, flux, unitDiagonal(hand, kDiscriminatingConstant));

    CHECK(system.summary.boundaryFacesFixedPressure > 0);
    CHECK_FALSE(system.summary.constantModeWasInTheNullSpace);
    // Nothing is pinned, because nothing needs to be.
    CHECK_FALSE(system.summary.pinnedCell.has_value());
}

TEST_CASE("CfdCoupling_TheSourceIsTheNegatedDivergenceOfThePredictedFlux", "[cfd][pvc]") {
    // Measured against THIS FILE's own face walk, not against the operator's.
    const Hand hand = Hand::make(chainPoints(5), chainTets(5));
    const PreparedCfdBoundary boundary = hand.mixed(inlet(0.2), outlet(0.0));
    std::vector<double> velocity = zeros(3 * hand.topology.cells());
    for (std::size_t c = 0; c < hand.topology.cells(); ++c) {
        velocity[2 * hand.topology.cells() + c] = 0.05;
    }
    const std::vector<double> flux = producedFlux(hand, boundary, velocity);
    const CfdPressureCorrectionSystem system =
        assemble(hand, boundary, flux, unitDiagonal(hand, kDiscriminatingConstant));

    const std::vector<double> divergence = divergenceOf(hand.topology, flux);
    REQUIRE(system.source.size() == divergence.size());
    for (std::size_t c = 0; c < divergence.size(); ++c) {
        INFO("cell " << c);
        CHECK_THAT(system.source[c], Catch::Matchers::WithinAbs(-divergence[c], 1.0e-18));
    }
}

TEST_CASE("CfdCoupling_TheInternalFaceCountEqualsTheTopologys", "[cfd][pvc]") {
    const Hand hand = Hand::make(chainPoints(7), chainTets(7));
    const PreparedCfdBoundary boundary = hand.uniform(wall());
    const CfdPressureCorrectionSystem system = assemble(
        hand, boundary, zeros(hand.topology.faces()), unitDiagonal(hand, kDiscriminatingConstant));
    CHECK(system.summary.internalFacesCoupled == hand.topology.internalFaces());
    CHECK(system.summary.boundaryFacesFixedPressure + system.summary.boundaryFacesZeroGradient ==
          hand.topology.boundaryFaces());
    CHECK(system.summary.cells == hand.topology.cells());
}

TEST_CASE("CfdCoupling_TheReferenceCellRowAndColumnAreBothCleared", "[cfd][pvc]") {
    // ADR-075 Decision 4. Clearing the COLUMN as well is what keeps the matrix
    // symmetric, and it is exact because the pinned value is zero.
    const Hand hand = Hand::make(chainPoints(6), chainTets(6));
    const PreparedCfdBoundary boundary = hand.uniform(wall());
    const CfdPressureCorrectionSystem system = assemble(
        hand, boundary, zeros(hand.topology.faces()), unitDiagonal(hand, kDiscriminatingConstant));

    REQUIRE(system.summary.pinnedCell.has_value());
    const std::size_t pinned = system.summary.pinnedCell->value();
    for (std::size_t k = 0; k < system.matrix.rows(); ++k) {
        if (k == pinned) {
            continue;
        }
        INFO("index " << k << " against pinned " << pinned);
        CHECK(system.matrix.coeffSi(pinned, k) == 0.0);
        CHECK(system.matrix.coeffSi(k, pinned) == 0.0);
    }
    CHECK(system.matrix.coeffSi(pinned, pinned) > 0.0);
    CHECK(system.source[pinned] == 0.0);
    CHECK(system.matrix.largestSymmetryError() == 0.0);
}

TEST_CASE("CfdCoupling_ThePinnedDiagonalKeepsTheValueItHadRatherThanBecomingOne", "[cfd][pvc]") {
    // A dimensionless 1.0 in a matrix of metre-seconds would wreck the pivot
    // ratio a symmetric kernel measures conditioning by. Either value gives
    // `p'_ref = 0` exactly, so the scale is the only reason to prefer one.
    const Hand hand = Hand::make(chainPoints(6), chainTets(6));
    const PreparedCfdBoundary boundary = hand.uniform(wall());
    const CfdPressureCorrectionSystem system = assemble(
        hand, boundary, zeros(hand.topology.faces()), unitDiagonal(hand, kDiscriminatingConstant));
    REQUIRE(system.summary.pinnedCell.has_value());
    const double kept = system.matrix.coeffSi(system.summary.pinnedCell->value(),
                                              system.summary.pinnedCell->value());
    CHECK(kept != 1.0);
    CHECK(kept > 0.0);

    // And it is on the scale of the other diagonals rather than an outlier.
    double largest = 0.0;
    for (std::size_t row = 0; row < system.matrix.rows(); ++row) {
        largest = std::max(largest, system.matrix.coeffSi(row, row));
    }
    CHECK(kept >= largest * 1.0e-6);
}

TEST_CASE("CfdCoupling_TheCompatibilityResidualIsNearZeroForABalancedFlux", "[cfd][pvc]") {
    // ADR-075 Q2 measured 6.217249e-15 balanced against 5.327555e+00 unbalanced.
    const Hand hand = Hand::make(chainPoints(6), chainTets(6));
    const PreparedCfdBoundary boundary = hand.uniform(wall());
    const CfdPressureCorrectionSystem system = assemble(
        hand, boundary, zeros(hand.topology.faces()), unitDiagonal(hand, kDiscriminatingConstant));
    CHECK(system.summary.compatibilityResidual <= cfd::kCfdCouplingCompatibilityTolerance);
}

TEST_CASE("CfdCoupling_AnUnbalancedPrescribedFluxIsRefusedInThePureNeumannCase", "[cfd][pvc]") {
    // Every boundary a wall means every flux is prescribed, so the system is
    // pure Neumann and its right-hand side MUST sum to zero or there is no
    // solution at all. A flux that does not balance is refused rather than
    // solved to something meaningless.
    const Hand hand = Hand::make(chainPoints(4), chainTets(4));
    const PreparedCfdBoundary boundary = hand.uniform(wall());
    std::vector<double> flux = zeros(hand.topology.faces());
    // One boundary face carries mass in and nothing carries it out.
    const FaceIndex first = hand.topology.faceOf(BoundaryFaceIndex::fromValue(0));
    flux[first.value()] = kDiscriminatingConstant;

    const CfdPressureCorrectionInput input{.predictedFaceFlux = flux,
                                           .momentumDiagonal =
                                               unitDiagonal(hand, kDiscriminatingConstant),
                                           .density = kDensity};
    const CfdCouplingFailure failure = refusal(hand, boundary, input);
    CHECK(failure.problem == CfdCouplingProblem::PrescribedFluxUnbalanced);
    CHECK_THAT(failure.error.message, Catch::Matchers::ContainsSubstring("pure Neumann"));
}

TEST_CASE("CfdCoupling_TheThroughputIsHalfTheBoundaryMagnitudeSum", "[cfd][pvc]") {
    // ADR-058 fixed the throughput as `0.5 * sum |phi|` over the boundary, and
    // this operator divides its relative measures by the same scale.
    const Hand hand = Hand::make(chainPoints(5), chainTets(5));
    const PreparedCfdBoundary boundary = hand.mixed(inlet(0.3), outlet(0.0));
    std::vector<double> velocity = zeros(3 * hand.topology.cells());
    velocity[0] = 0.02;
    const std::vector<double> flux = producedFlux(hand, boundary, velocity);
    const CfdPressureCorrectionSystem system =
        assemble(hand, boundary, flux, unitDiagonal(hand, kDiscriminatingConstant));

    double magnitude = 0.0;
    for (std::size_t b = 0; b < hand.topology.boundaryFaces(); ++b) {
        magnitude += std::abs(flux[hand.topology.faceOf(BoundaryFaceIndex::fromValue(b)).value()]);
    }
    CHECK_THAT(system.summary.boundaryThroughput,
               Catch::Matchers::WithinRel(0.5 * magnitude, 1.0e-15));
}

TEST_CASE("CfdCoupling_TheDiagonalSpreadIsZeroWhenTheThreeComponentsAgree", "[cfd][pvc]") {
    const Hand hand = Hand::make(chainPoints(5), chainTets(5));
    const PreparedCfdBoundary boundary = hand.uniform(wall());
    const CfdPressureCorrectionSystem same = assemble(
        hand, boundary, zeros(hand.topology.faces()), unitDiagonal(hand, kDiscriminatingConstant));
    CHECK(same.summary.largestDiagonalSpread == 0.0);

    std::vector<double> uneven = unitDiagonal(hand, kDiscriminatingConstant);
    uneven[2 * hand.topology.cells()] = kDiscriminatingConstant * 2.0;
    const CfdPressureCorrectionSystem spread =
        assemble(hand, boundary, zeros(hand.topology.faces()), uneven);
    CHECK(spread.summary.largestDiagonalSpread > 0.4);
}

// ===========================================================================
// THE IDENTITY — the milestone's central claim
// ===========================================================================

TEST_CASE("CfdCoupling_TheCorrectedDivergenceIsThePressureSystemsOwnResidual", "[cfd][pvc]") {
    // ADR-075 Decision 5. `div(phi_corrected)_P == (A p')_P - b_P` EXACTLY, as
    // algebra and not as an approximation. Measured here with this file's own
    // dense product and its own face walk, so neither side is the operator's.
    const Hand hand = Hand::make(chainPoints(6), chainTets(6));
    const PreparedCfdBoundary boundary = hand.mixed(inlet(0.25), outlet(0.0));
    const std::vector<double> velocity = zeros(3 * hand.topology.cells());
    const std::vector<double> flux = producedFlux(hand, boundary, velocity);
    const std::vector<double> diagonal = unitDiagonal(hand, kDiscriminatingConstant);
    const CfdPressureCorrectionSystem system = assemble(hand, boundary, flux, diagonal);

    std::expected<CfdLinearSolveResult, Error> solved =
        denseSolve()(system.matrix, system.source, CfdLinearSolverSettings{});
    REQUIRE(solved.has_value());

    const CfdCouplingCorrectionInput input{.pressureCorrection = solved->solution,
                                           .pressure = zeros(hand.topology.cells()),
                                           .predictedVelocity = velocity,
                                           .predictedFaceFlux = flux,
                                           .momentumDiagonal = diagonal,
                                           .density = kDensity,
                                           .iteration = CfdIterationSettings{}};
    std::expected<CfdCouplingCorrection, CfdCouplingFailure> corrected =
        cfd::applyCfdCouplingCorrection(hand.topology, hand.geometry, boundary, input);
    INFO((corrected.has_value() ? std::string{} : corrected.error().error.message));
    REQUIRE(corrected.has_value());

    const std::vector<double> measured = divergenceOf(hand.topology, corrected->faceFlux);
    const std::vector<double> predicted =
        residualOf(system.matrix, system.source, solved->solution);
    REQUIRE(measured.size() == predicted.size());

    const double scale = std::max(system.summary.boundaryThroughput, 1.0e-30);
    double worst = 0.0;
    for (std::size_t c = 0; c < measured.size(); ++c) {
        worst = std::max(worst, std::abs(measured[c] - predicted[c]) / scale);
    }
    INFO("worst gap / throughput = " << worst);
    CHECK(worst <= 1.0e-12);
}

TEST_CASE("CfdCoupling_TheIdentityHoldsForADeliberatelyWrongPressureCorrection", "[cfd][pvc]") {
    // THE STRONGER HALF. If the identity only held for the right `p'` it would
    // be a coincidence of convergence; holding for a wrong one shows the flux
    // correction and the matrix carry THE SAME coefficients. ADR-075 Q6
    // measured this gap at 3.810534e-16.
    const Hand hand = Hand::make(chainPoints(6), chainTets(6));
    const PreparedCfdBoundary boundary = hand.mixed(inlet(0.25), outlet(0.0));
    const std::vector<double> velocity = zeros(3 * hand.topology.cells());
    const std::vector<double> flux = producedFlux(hand, boundary, velocity);
    const std::vector<double> diagonal = unitDiagonal(hand, kDiscriminatingConstant);
    const CfdPressureCorrectionSystem system = assemble(hand, boundary, flux, diagonal);

    // A `p'` that solves nothing at all.
    std::vector<double> wrong(hand.topology.cells(), 0.0);
    for (std::size_t c = 0; c < wrong.size(); ++c) {
        wrong[c] = kDiscriminatingConstant * (static_cast<double>(c % 7) - 3.0);
    }

    const CfdCouplingCorrectionInput input{.pressureCorrection = wrong,
                                           .pressure = zeros(hand.topology.cells()),
                                           .predictedVelocity = velocity,
                                           .predictedFaceFlux = flux,
                                           .momentumDiagonal = diagonal,
                                           .density = kDensity,
                                           .iteration = CfdIterationSettings{}};
    std::expected<CfdCouplingCorrection, CfdCouplingFailure> corrected =
        cfd::applyCfdCouplingCorrection(hand.topology, hand.geometry, boundary, input);
    REQUIRE(corrected.has_value());

    const std::vector<double> measured = divergenceOf(hand.topology, corrected->faceFlux);
    const std::vector<double> predicted = residualOf(system.matrix, system.source, wrong);
    double scale = 0.0;
    for (const double p : predicted) {
        scale = std::max(scale, std::abs(p));
    }
    scale = std::max(scale, 1.0e-30);
    double worst = 0.0;
    for (std::size_t c = 0; c < measured.size(); ++c) {
        worst = std::max(worst, std::abs(measured[c] - predicted[c]) / scale);
    }
    INFO("worst gap / scale = " << worst);
    CHECK(worst <= 1.0e-12);
}

// ===========================================================================
// The corrections
// ===========================================================================

TEST_CASE("CfdCoupling_AUniformCorrectionLeavesTheVelocityUnchangedInAPureNeumannDomain",
          "[cfd][pvc]") {
    // `sum_f S_f == 0` over a closed cell, and every facet is zero-gradient, so
    // the face sum telescopes to zero and the velocity does not move.
    //
    // THE CONDITION MATTERS AND IS STATED: with a FIXED-PRESSURE facet present,
    // `p'_b = 0` rather than `p'_P`, and a uniform `p'` is then a real gradient
    // against that boundary. The next test asserts exactly that.
    const Hand hand = Hand::make(chainPoints(6), chainTets(6));
    const PreparedCfdBoundary boundary = hand.uniform(wall());
    const std::vector<double> flux = zeros(hand.topology.faces());
    const std::vector<double> diagonal = unitDiagonal(hand, kDiscriminatingConstant);
    const std::vector<double> velocity = zeros(3 * hand.topology.cells());
    const std::vector<double> uniform(hand.topology.cells(), kDiscriminatingConstant);

    const CfdCouplingCorrectionInput input{.pressureCorrection = uniform,
                                           .pressure = zeros(hand.topology.cells()),
                                           .predictedVelocity = velocity,
                                           .predictedFaceFlux = flux,
                                           .momentumDiagonal = diagonal,
                                           .density = kDensity,
                                           .iteration = CfdIterationSettings{}};
    std::expected<CfdCouplingCorrection, CfdCouplingFailure> corrected =
        cfd::applyCfdCouplingCorrection(hand.topology, hand.geometry, boundary, input);
    REQUIRE(corrected.has_value());

    double worst = 0.0;
    for (std::size_t k = 0; k < corrected->velocity.size(); ++k) {
        worst = std::max(worst, std::abs(corrected->velocity[k] - velocity[k]));
    }
    INFO("worst velocity change = " << worst);
    CHECK(worst <= 1.0e-12);
}

TEST_CASE("CfdCoupling_AUniformCorrectionDoesMoveTheVelocityAgainstAFixedPressureBoundary",
          "[cfd][pvc]") {
    const Hand hand = Hand::make(chainPoints(6), chainTets(6));
    const PreparedCfdBoundary boundary = hand.mixed(inlet(0.1), outlet(0.0));
    const std::vector<double> velocity = zeros(3 * hand.topology.cells());
    const std::vector<double> flux = producedFlux(hand, boundary, velocity);
    const std::vector<double> diagonal = unitDiagonal(hand, kDiscriminatingConstant);
    const std::vector<double> uniform(hand.topology.cells(), kDiscriminatingConstant);

    const CfdCouplingCorrectionInput input{.pressureCorrection = uniform,
                                           .pressure = zeros(hand.topology.cells()),
                                           .predictedVelocity = velocity,
                                           .predictedFaceFlux = flux,
                                           .momentumDiagonal = diagonal,
                                           .density = kDensity,
                                           .iteration = CfdIterationSettings{}};
    std::expected<CfdCouplingCorrection, CfdCouplingFailure> corrected =
        cfd::applyCfdCouplingCorrection(hand.topology, hand.geometry, boundary, input);
    REQUIRE(corrected.has_value());

    double worst = 0.0;
    for (std::size_t k = 0; k < corrected->velocity.size(); ++k) {
        worst = std::max(worst, std::abs(corrected->velocity[k] - velocity[k]));
    }
    INFO("worst velocity change = " << worst);
    CHECK(worst > 0.0);
}

TEST_CASE("CfdCoupling_TheFluxIsUnchangedAtAPrescribedFluxFacetAndCorrectedAtAFixedPressureOne",
          "[cfd][pvc]") {
    const Hand hand = Hand::make(chainPoints(6), chainTets(6));
    const PreparedCfdBoundary boundary = hand.mixed(inlet(0.2), outlet(0.0));
    const std::vector<double> velocity = zeros(3 * hand.topology.cells());
    const std::vector<double> flux = producedFlux(hand, boundary, velocity);
    const std::vector<double> diagonal = unitDiagonal(hand, kDiscriminatingConstant);
    std::vector<double> correction(hand.topology.cells(), kDiscriminatingConstant);

    const CfdCouplingCorrectionInput input{.pressureCorrection = correction,
                                           .pressure = zeros(hand.topology.cells()),
                                           .predictedVelocity = velocity,
                                           .predictedFaceFlux = flux,
                                           .momentumDiagonal = diagonal,
                                           .density = kDensity,
                                           .iteration = CfdIterationSettings{}};
    std::expected<CfdCouplingCorrection, CfdCouplingFailure> corrected =
        cfd::applyCfdCouplingCorrection(hand.topology, hand.geometry, boundary, input);
    REQUIRE(corrected.has_value());

    std::size_t unchanged = 0;
    std::size_t moved = 0;
    for (std::size_t b = 0; b < hand.topology.boundaryFaces(); ++b) {
        const BoundaryFaceIndex boundaryFace = BoundaryFaceIndex::fromValue(b);
        const FaceIndex face = hand.topology.faceOf(boundaryFace);
        const std::optional<CfdBoundaryTreatment> treatment = boundary.treatment(boundaryFace);
        REQUIRE(treatment.has_value());
        INFO("boundary face " << b);
        if (treatment->pressure == CfdBoundaryPolicy::FixedValue) {
            CHECK(corrected->faceFlux[face.value()] != flux[face.value()]);
            ++moved;
        } else {
            CHECK(corrected->faceFlux[face.value()] == flux[face.value()]);
            ++unchanged;
        }
    }
    CHECK(moved > 0);
    CHECK(unchanged > 0);
}

TEST_CASE("CfdCoupling_ThePressureIsUpdatedByTheRelaxationFactorExactly", "[cfd][pvc]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.uniform(wall());
    const std::vector<double> flux = zeros(hand.topology.faces());
    const std::vector<double> diagonal = unitDiagonal(hand, kDiscriminatingConstant);
    const std::vector<double> velocity = zeros(3 * hand.topology.cells());
    std::vector<double> start(hand.topology.cells(), 0.0);
    std::vector<double> correction(hand.topology.cells(), 0.0);
    for (std::size_t c = 0; c < hand.topology.cells(); ++c) {
        start[c] = 1000.0 + static_cast<double>(c);
        correction[c] = kDiscriminatingConstant * (1.0 + static_cast<double>(c));
    }

    CfdIterationSettings settings;
    settings.pressureRelaxation = 0.3;
    const CfdCouplingCorrectionInput input{.pressureCorrection = correction,
                                           .pressure = start,
                                           .predictedVelocity = velocity,
                                           .predictedFaceFlux = flux,
                                           .momentumDiagonal = diagonal,
                                           .density = kDensity,
                                           .iteration = settings};
    std::expected<CfdCouplingCorrection, CfdCouplingFailure> corrected =
        cfd::applyCfdCouplingCorrection(hand.topology, hand.geometry, boundary, input);
    REQUIRE(corrected.has_value());
    for (std::size_t c = 0; c < hand.topology.cells(); ++c) {
        INFO("cell " << c);
        CHECK(corrected->pressure[c] == start[c] + 0.3 * correction[c]);
    }
}

TEST_CASE("CfdCoupling_TheQualifiedGradientOperatorWouldImposeThePrescribedPressureInstead",
          "[cfd][pvc]") {
    // ADR-075 Decision 12, DEMONSTRATED rather than asserted. The reason the
    // velocity correction does not call `reconstructCfdPressureGradient` is that
    // it applies the PRESCRIBED `p_b` at a fixed-pressure facet, while the
    // pressure CORRECTION is zero there. This test shows the two disagree, so
    // the design note rests on a measurement.
    const Hand hand = Hand::make(chainPoints(6), chainTets(6));
    const PreparedCfdBoundary boundary = hand.mixed(inlet(0.1), outlet(kDiscriminatingConstant));
    const std::vector<double> uniform(hand.topology.cells(), 0.0);

    // The gradient of an all-zero field. If the operator ignored the prescribed
    // boundary value this would be exactly zero everywhere.
    const cfd::CfdGradientInput input{.method = cfd::CfdGradientMethod::GreenGauss,
                                      .cellValue = uniform};
    Result<cfd::CfdPressureGradientField> field =
        cfd::reconstructCfdPressureGradient(hand.topology, hand.geometry, boundary, input);
    INFO((field.has_value() ? std::string{} : field.error().message));
    REQUIRE(field.has_value());

    double largest = 0.0;
    for (std::size_t c = 0; c < hand.topology.cells(); ++c) {
        const std::optional<cfd::CfdPressureGradientField::Vector> g =
            field->at(CellIndex::fromValue(c));
        REQUIRE(g.has_value());
        largest = std::max({largest, std::abs(g->x.si()), std::abs(g->y.si()),
                            std::abs(g->z.si())});
    }
    // NON-ZERO: the prescribed outlet pressure entered a gradient of a field
    // that is identically zero. For `p'` that is the wrong boundary value, and
    // this number is how wrong.
    INFO("largest |grad| of an all-zero field = " << largest);
    CHECK(largest > 0.0);
}

// ===========================================================================
// The predictor
// ===========================================================================

TEST_CASE("CfdCoupling_AtUnitRelaxationTheDiagonalIsTheAssembledOne", "[cfd][pvc]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.uniform(wall());
    const std::vector<double> velocity = zeros(3 * hand.topology.cells());
    const std::vector<double> flux = producedFlux(hand, boundary, velocity);
    const std::vector<CfdMomentumSystem> systems =
        momentumSystems(hand, boundary, flux, velocity);

    CfdIterationSettings settings;
    settings.velocityRelaxation = 1.0;
    const CfdCouplingPredictorInput input{.momentum = systems,
                                          .velocity = velocity,
                                          .iteration = settings,
                                          .linearSolver = CfdLinearSolverSettings{}};
    std::expected<CfdCouplingPredictor, CfdCouplingFailure> predicted =
        cfd::cfdCouplingPredictor(hand.topology, hand.geometry, boundary, input, denseSolve());
    INFO((predicted.has_value() ? std::string{} : predicted.error().error.message));
    REQUIRE(predicted.has_value());

    const std::size_t cells = hand.topology.cells();
    for (std::size_t c = 0; c < 3; ++c) {
        for (std::size_t cell = 0; cell < cells; ++cell) {
            INFO("component " << c << " cell " << cell);
            CHECK(predicted->diagonal[c * cells + cell] ==
                  systems[c].matrix.coeffSi(cell, cell));
        }
    }
}

TEST_CASE("CfdCoupling_TheRelaxedDiagonalIsTheAssembledOneDividedByAlpha", "[cfd][pvc]") {
    // ADR-075 Q4: using the UNRELAXED diagonal for `d_P` inflates every
    // correction coefficient by exactly `1 / alpha_u`. The predictor returns the
    // relaxed one so the consistent value is the one already to hand.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.uniform(wall());
    const std::vector<double> velocity = zeros(3 * hand.topology.cells());
    const std::vector<double> flux = producedFlux(hand, boundary, velocity);
    const std::vector<CfdMomentumSystem> systems =
        momentumSystems(hand, boundary, flux, velocity);

    CfdIterationSettings settings;
    settings.velocityRelaxation = 0.7;
    const CfdCouplingPredictorInput input{.momentum = systems,
                                          .velocity = velocity,
                                          .iteration = settings,
                                          .linearSolver = CfdLinearSolverSettings{}};
    std::expected<CfdCouplingPredictor, CfdCouplingFailure> predicted =
        cfd::cfdCouplingPredictor(hand.topology, hand.geometry, boundary, input, denseSolve());
    REQUIRE(predicted.has_value());

    const std::size_t cells = hand.topology.cells();
    for (std::size_t c = 0; c < 3; ++c) {
        for (std::size_t cell = 0; cell < cells; ++cell) {
            INFO("component " << c << " cell " << cell);
            CHECK(predicted->diagonal[c * cells + cell] ==
                  systems[c].matrix.coeffSi(cell, cell) / 0.7);
        }
    }
}

TEST_CASE("CfdCoupling_ThePredictorRefusesTheWrongNumberOfMomentumSystems", "[cfd][pvc]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.uniform(wall());
    const std::vector<double> velocity = zeros(3 * hand.topology.cells());
    const std::vector<double> flux = producedFlux(hand, boundary, velocity);
    std::vector<CfdMomentumSystem> systems = momentumSystems(hand, boundary, flux, velocity);
    systems.pop_back();

    const CfdCouplingPredictorInput input{.momentum = systems,
                                          .velocity = velocity,
                                          .iteration = CfdIterationSettings{},
                                          .linearSolver = CfdLinearSolverSettings{}};
    std::expected<CfdCouplingPredictor, CfdCouplingFailure> predicted =
        cfd::cfdCouplingPredictor(hand.topology, hand.geometry, boundary, input, denseSolve());
    REQUIRE_FALSE(predicted.has_value());
    CHECK(predicted.error().problem == CfdCouplingProblem::MomentumSystemsWrong);
}

TEST_CASE("CfdCoupling_ThePredictorRefusesSystemsOutOfComponentOrder", "[cfd][pvc]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.uniform(wall());
    const std::vector<double> velocity = zeros(3 * hand.topology.cells());
    const std::vector<double> flux = producedFlux(hand, boundary, velocity);
    std::vector<CfdMomentumSystem> systems = momentumSystems(hand, boundary, flux, velocity);
    std::swap(systems[0], systems[2]);

    const CfdCouplingPredictorInput input{.momentum = systems,
                                          .velocity = velocity,
                                          .iteration = CfdIterationSettings{},
                                          .linearSolver = CfdLinearSolverSettings{}};
    std::expected<CfdCouplingPredictor, CfdCouplingFailure> predicted =
        cfd::cfdCouplingPredictor(hand.topology, hand.geometry, boundary, input, denseSolve());
    REQUIRE_FALSE(predicted.has_value());
    CHECK(predicted.error().problem == CfdCouplingProblem::MomentumSystemsWrong);
}

TEST_CASE("CfdCoupling_AnAbsentSolveIsRefusedRatherThanAssumed", "[cfd][pvc]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.uniform(wall());
    const std::vector<double> velocity = zeros(3 * hand.topology.cells());
    const std::vector<double> flux = producedFlux(hand, boundary, velocity);
    const std::vector<CfdMomentumSystem> systems =
        momentumSystems(hand, boundary, flux, velocity);

    const CfdCouplingPredictorInput input{.momentum = systems,
                                          .velocity = velocity,
                                          .iteration = CfdIterationSettings{},
                                          .linearSolver = CfdLinearSolverSettings{}};
    std::expected<CfdCouplingPredictor, CfdCouplingFailure> predicted = cfd::cfdCouplingPredictor(
        hand.topology, hand.geometry, boundary, input, CfdLinearSolve{});
    REQUIRE_FALSE(predicted.has_value());
    CHECK(predicted.error().problem == CfdCouplingProblem::LinearSolveRefused);
    CHECK_THAT(predicted.error().error.message,
               Catch::Matchers::ContainsSubstring("P19-LINSOLVE-001"));
}

TEST_CASE("CfdCoupling_APortRefusalKeepsThePortsOwnDiagnostic", "[cfd][pvc]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.uniform(wall());
    const std::vector<double> velocity = zeros(3 * hand.topology.cells());
    const std::vector<double> flux = producedFlux(hand, boundary, velocity);
    const std::vector<CfdMomentumSystem> systems =
        momentumSystems(hand, boundary, flux, velocity);

    const CfdLinearSolve refuses = [](const CfdSparseMatrix&, std::span<const double>,
                                      const CfdLinearSolverSettings&)
        -> std::expected<CfdLinearSolveResult, Error> {
        return std::unexpected(Error{ErrorCode::Internal, "the port said NOPE-12345"});
    };
    const CfdCouplingPredictorInput input{.momentum = systems,
                                          .velocity = velocity,
                                          .iteration = CfdIterationSettings{},
                                          .linearSolver = CfdLinearSolverSettings{}};
    std::expected<CfdCouplingPredictor, CfdCouplingFailure> predicted =
        cfd::cfdCouplingPredictor(hand.topology, hand.geometry, boundary, input, refuses);
    REQUIRE_FALSE(predicted.has_value());
    CHECK(predicted.error().problem == CfdCouplingProblem::LinearSolveRefused);
    CHECK_THAT(predicted.error().error.message,
               Catch::Matchers::ContainsSubstring("NOPE-12345"));
}

TEST_CASE("CfdCoupling_APortReturningTheWrongLengthIsUnusable", "[cfd][pvc]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.uniform(wall());
    const std::vector<double> velocity = zeros(3 * hand.topology.cells());
    const std::vector<double> flux = producedFlux(hand, boundary, velocity);
    const std::vector<CfdMomentumSystem> systems =
        momentumSystems(hand, boundary, flux, velocity);

    const CfdLinearSolve shortSolve = [](const CfdSparseMatrix&, std::span<const double>,
                                         const CfdLinearSolverSettings&)
        -> std::expected<CfdLinearSolveResult, Error> {
        return CfdLinearSolveResult{std::vector<double>{1.0}, 0.0};
    };
    const CfdCouplingPredictorInput input{.momentum = systems,
                                          .velocity = velocity,
                                          .iteration = CfdIterationSettings{},
                                          .linearSolver = CfdLinearSolverSettings{}};
    std::expected<CfdCouplingPredictor, CfdCouplingFailure> predicted =
        cfd::cfdCouplingPredictor(hand.topology, hand.geometry, boundary, input, shortSolve);
    REQUIRE_FALSE(predicted.has_value());
    CHECK(predicted.error().problem == CfdCouplingProblem::LinearSolutionUnusable);
}

TEST_CASE("CfdCoupling_APortReturningANonFiniteValueIsUnusable", "[cfd][pvc]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.uniform(wall());
    const std::vector<double> velocity = zeros(3 * hand.topology.cells());
    const std::vector<double> flux = producedFlux(hand, boundary, velocity);
    const std::vector<CfdMomentumSystem> systems =
        momentumSystems(hand, boundary, flux, velocity);
    const std::size_t cells = hand.topology.cells();

    const CfdLinearSolve nanSolve = [cells](const CfdSparseMatrix&, std::span<const double>,
                                            const CfdLinearSolverSettings&)
        -> std::expected<CfdLinearSolveResult, Error> {
        std::vector<double> x(cells, 0.0);
        x[0] = std::numeric_limits<double>::quiet_NaN();
        return CfdLinearSolveResult{std::move(x), 0.0};
    };
    const CfdCouplingPredictorInput input{.momentum = systems,
                                          .velocity = velocity,
                                          .iteration = CfdIterationSettings{},
                                          .linearSolver = CfdLinearSolverSettings{}};
    std::expected<CfdCouplingPredictor, CfdCouplingFailure> predicted =
        cfd::cfdCouplingPredictor(hand.topology, hand.geometry, boundary, input, nanSolve);
    REQUIRE_FALSE(predicted.has_value());
    CHECK(predicted.error().problem == CfdCouplingProblem::LinearSolutionUnusable);
}

TEST_CASE("CfdCoupling_APortReportingAResidualAboveTheToleranceIsRefused", "[cfd][pvc]") {
    // A LIBRARY SAYING "Success" IS NEVER SUFFICIENT EVIDENCE, which is
    // ADR-052's rule. Here the port returns the RIGHT answer and an honest but
    // too-large residual, and it is still refused.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.uniform(wall());
    const std::vector<double> velocity = zeros(3 * hand.topology.cells());
    const std::vector<double> flux = producedFlux(hand, boundary, velocity);
    const std::vector<CfdMomentumSystem> systems =
        momentumSystems(hand, boundary, flux, velocity);

    const CfdCouplingPredictorInput input{.momentum = systems,
                                          .velocity = velocity,
                                          .iteration = CfdIterationSettings{},
                                          .linearSolver = CfdLinearSolverSettings{}};
    // The dense solve is exact and its own residual is at or near zero, so a
    // SCALED report cannot be made to exceed the tolerance -- the first form of
    // this test multiplied by 1e12 and the port still passed. The report is
    // stated instead, and the ANSWER is untouched.
    std::expected<CfdCouplingPredictor, CfdCouplingFailure> predicted = cfd::cfdCouplingPredictor(
        hand.topology, hand.geometry, boundary, input, reportingResidual(1.0e-3));
    REQUIRE_FALSE(predicted.has_value());
    CHECK(predicted.error().problem == CfdCouplingProblem::LinearSolveRefused);
    CHECK_THAT(predicted.error().error.message,
               Catch::Matchers::ContainsSubstring("normalised residual"));
}

// ===========================================================================
// Refusals
// ===========================================================================

TEST_CASE("CfdCoupling_AWrongLengthFluxIsRefused", "[cfd][pvc]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.uniform(wall());
    std::vector<double> flux = zeros(hand.topology.faces());
    flux.pop_back();
    const CfdPressureCorrectionInput input{.predictedFaceFlux = flux,
                                           .momentumDiagonal =
                                               unitDiagonal(hand, kDiscriminatingConstant),
                                           .density = kDensity};
    CHECK(refusal(hand, boundary, input).problem == CfdCouplingProblem::ValueLengthWrong);
}

TEST_CASE("CfdCoupling_AWrongLengthDiagonalIsRefused", "[cfd][pvc]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.uniform(wall());
    std::vector<double> diagonal = unitDiagonal(hand, kDiscriminatingConstant);
    diagonal.pop_back();
    const CfdPressureCorrectionInput input{.predictedFaceFlux = zeros(hand.topology.faces()),
                                           .momentumDiagonal = diagonal,
                                           .density = kDensity};
    CHECK(refusal(hand, boundary, input).problem == CfdCouplingProblem::ValueLengthWrong);
}

TEST_CASE("CfdCoupling_ANonFiniteFluxIsRefused", "[cfd][pvc]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.uniform(wall());
    std::vector<double> flux = zeros(hand.topology.faces());
    flux[0] = std::numeric_limits<double>::quiet_NaN();
    const CfdPressureCorrectionInput input{.predictedFaceFlux = flux,
                                           .momentumDiagonal =
                                               unitDiagonal(hand, kDiscriminatingConstant),
                                           .density = kDensity};
    CHECK(refusal(hand, boundary, input).problem == CfdCouplingProblem::ValueNotFinite);
}

TEST_CASE("CfdCoupling_ANonPositiveDiagonalIsRefused", "[cfd][pvc]") {
    // `d_P = V_P / a_P` is meaningless at a non-positive diagonal, and a
    // non-positive diagonal is a defective assembly rather than a hard problem.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.uniform(wall());
    std::vector<double> diagonal = unitDiagonal(hand, kDiscriminatingConstant);
    diagonal[1] = 0.0;
    const CfdPressureCorrectionInput input{.predictedFaceFlux = zeros(hand.topology.faces()),
                                           .momentumDiagonal = diagonal,
                                           .density = kDensity};
    CHECK(refusal(hand, boundary, input).problem ==
          CfdCouplingProblem::MomentumDiagonalNotPositive);
}

TEST_CASE("CfdCoupling_ANonPositiveDensityIsRefused", "[cfd][pvc]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.uniform(wall());
    const CfdPressureCorrectionInput input{.predictedFaceFlux = zeros(hand.topology.faces()),
                                           .momentumDiagonal =
                                               unitDiagonal(hand, kDiscriminatingConstant),
                                           .density = Density::fromSi(0.0)};
    CHECK(refusal(hand, boundary, input).problem == CfdCouplingProblem::ValueNotFinite);
}

TEST_CASE("CfdCoupling_TheRefusalOrderIsPinned", "[cfd][pvc]") {
    // An input with TWO faults reports the earlier phase. A refusal order that
    // is not pinned is a refusal order that drifts.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.uniform(wall());

    // A length fault AND a non-finite value: the length wins.
    std::vector<double> flux = zeros(hand.topology.faces());
    flux[0] = std::numeric_limits<double>::quiet_NaN();
    flux.pop_back();
    const CfdPressureCorrectionInput lengthFirst{
        .predictedFaceFlux = flux,
        .momentumDiagonal = unitDiagonal(hand, kDiscriminatingConstant),
        .density = kDensity};
    CHECK(refusal(hand, boundary, lengthFirst).problem == CfdCouplingProblem::ValueLengthWrong);

    // A non-finite diagonal AND a non-positive one: finiteness wins, because it
    // is checked in phase 3 and positivity in phase 4.
    std::vector<double> diagonal = unitDiagonal(hand, kDiscriminatingConstant);
    diagonal[0] = std::numeric_limits<double>::infinity();
    diagonal[1] = -1.0;
    const CfdPressureCorrectionInput finiteFirst{
        .predictedFaceFlux = zeros(hand.topology.faces()),
        .momentumDiagonal = diagonal,
        .density = kDensity};
    CHECK(refusal(hand, boundary, finiteFirst).problem == CfdCouplingProblem::ValueNotFinite);
}

TEST_CASE("CfdCoupling_ThePredictorAndTheAssemblyAgreeWithTheirOwnPredicate", "[cfd][pvc]") {
    // RECORDED FOR WHAT IT IS WORTH: one decision path, so their agreement
    // cannot fail. It is here because a caller may legitimately ask whether an
    // input is usable without paying for the answer.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.uniform(wall());
    std::vector<double> flux = zeros(hand.topology.faces());
    flux.pop_back();
    const CfdPressureCorrectionInput bad{.predictedFaceFlux = flux,
                                         .momentumDiagonal =
                                             unitDiagonal(hand, kDiscriminatingConstant),
                                         .density = kDensity};
    const std::optional<CfdCouplingFailure> predicted =
        cfd::cfdPressureCorrectionFailure(hand.topology, hand.geometry, boundary, bad);
    REQUIRE(predicted.has_value());
    CHECK(predicted->problem == refusal(hand, boundary, bad).problem);

    const CfdPressureCorrectionInput good{.predictedFaceFlux = zeros(hand.topology.faces()),
                                          .momentumDiagonal =
                                              unitDiagonal(hand, kDiscriminatingConstant),
                                          .density = kDensity};
    CHECK_FALSE(
        cfd::cfdPressureCorrectionFailure(hand.topology, hand.geometry, boundary, good).has_value());
}

TEST_CASE("CfdCoupling_EveryProblemHasItsOwnName", "[cfd][pvc]") {
    std::vector<std::string_view> names;
    for (const CfdCouplingProblem problem : cfd::kCfdCouplingProblems) {
        names.push_back(cfd::toString(problem));
    }
    CHECK(names.size() == cfd::kCfdCouplingProblems.size());
    std::ranges::sort(names);
    CHECK(std::ranges::adjacent_find(names) == names.end());
    for (const std::string_view name : names) {
        CHECK_FALSE(name.empty());
    }
}

TEST_CASE("CfdCoupling_TheProblemArrayHoldsEveryEnumeratorOnce", "[cfd][pvc]") {
    // The array and the enum cannot drift apart silently: a new enumerator left
    // out of the array shows up as a name that the array does not produce.
    std::vector<std::string_view> fromArray;
    for (const CfdCouplingProblem problem : cfd::kCfdCouplingProblems) {
        fromArray.push_back(cfd::toString(problem));
    }
    // `EvaluationIsInconsistent` is also the fallback for an unknown value, so
    // the count of DISTINCT names must equal the array's length exactly.
    std::ranges::sort(fromArray);
    CHECK(static_cast<std::size_t>(std::ranges::distance(
              fromArray.begin(), std::ranges::unique(fromArray).begin())) ==
          cfd::kCfdCouplingProblems.size());
}

// ===========================================================================
// The criteria
// ===========================================================================

TEST_CASE("CfdCoupling_ConvergenceNeedsEveryMomentumComponentAndContinuity", "[cfd][pvc]") {
    CfdConvergenceSettings settings;
    const CfdResiduals first{1.0, 1.0, 1.0, 1.0};

    CfdResiduals all{1.0e-9, 1.0e-9, 1.0e-9, 1.0e-9};
    CHECK(cfd::cfdCouplingVerdict(all, first, settings) == CfdCouplingVerdict::Converged);

    // One component above its tolerance is enough to continue.
    for (std::size_t k = 0; k < 4; ++k) {
        CfdResiduals one = all;
        (k == 0 ? one.momentumX : k == 1 ? one.momentumY : k == 2 ? one.momentumZ : one.continuity) =
            1.0e-3;
        INFO("slot " << k);
        CHECK(cfd::cfdCouplingVerdict(one, first, settings) == CfdCouplingVerdict::Continue);
    }
}

TEST_CASE("CfdCoupling_DivergenceIsCheckedBeforeConvergence", "[cfd][pvc]") {
    // A run whose residual exploded and then happened to land under a tolerance
    // has still diverged, and reporting it as converged would publish a field
    // nobody should trust.
    CfdConvergenceSettings settings;
    settings.divergenceFactor = 10.0;
    const CfdResiduals first{1.0e-12, 1.0e-12, 1.0e-12, 1.0e-12};
    const CfdResiduals now{1.0e-9, 1.0e-9, 1.0e-9, 1.0e-9};
    // Every residual is under its tolerance AND a thousand times its first
    // value. Divergence wins.
    CHECK(cfd::cfdCouplingVerdict(now, first, settings) == CfdCouplingVerdict::Diverged);
}

TEST_CASE("CfdCoupling_ANonFiniteResidualIsDivergenceAndNotContinue", "[cfd][pvc]") {
    // A NaN residual is not below a tolerance, and treating it as "continue"
    // would spend the whole budget on a dead run.
    CfdConvergenceSettings settings;
    const CfdResiduals first{1.0, 1.0, 1.0, 1.0};
    for (std::size_t k = 0; k < 4; ++k) {
        CfdResiduals now{1.0e-12, 1.0e-12, 1.0e-12, 1.0e-12};
        (k == 0 ? now.momentumX : k == 1 ? now.momentumY : k == 2 ? now.momentumZ : now.continuity) =
            std::numeric_limits<double>::quiet_NaN();
        INFO("slot " << k);
        CHECK(cfd::cfdCouplingVerdict(now, first, settings) == CfdCouplingVerdict::Diverged);
    }
}

TEST_CASE("CfdCoupling_TheFirstIterationCannotDiverge", "[cfd][pvc]") {
    // Passing the current record as `first` makes every ratio exactly 1, and
    // `CfdConvergenceSettings` requires the factor to exceed 1.
    CfdConvergenceSettings settings;
    const CfdResiduals now{0.5, 0.25, 0.125, 0.0625};
    CHECK(cfd::cfdCouplingVerdict(now, now, settings) != CfdCouplingVerdict::Diverged);
}

TEST_CASE("CfdCoupling_TheBudgetIsExhaustedAtTheLimitAndNotAfterIt", "[cfd][pvc]") {
    CfdSteadySettings settings;
    settings.maxIterations = 3;
    CHECK_FALSE(cfd::cfdCouplingBudgetExhausted(0, settings));
    CHECK_FALSE(cfd::cfdCouplingBudgetExhausted(2, settings));
    CHECK(cfd::cfdCouplingBudgetExhausted(3, settings));
    CHECK(cfd::cfdCouplingBudgetExhausted(4, settings));
}

TEST_CASE("CfdCoupling_EveryVerdictHasItsOwnName", "[cfd][pvc]") {
    const std::array<CfdCouplingVerdict, 3> all{CfdCouplingVerdict::Converged,
                                                CfdCouplingVerdict::Diverged,
                                                CfdCouplingVerdict::Continue};
    std::vector<std::string_view> names;
    for (const CfdCouplingVerdict verdict : all) {
        names.push_back(cfd::toString(verdict));
    }
    std::ranges::sort(names);
    CHECK(std::ranges::adjacent_find(names) == names.end());
}

// ===========================================================================
// One outer iteration, determinism, and the benchmarks
// ===========================================================================

namespace {

[[nodiscard]] CfdCouplingIteration advance(const Hand& hand, const PreparedCfdBoundary& boundary,
                                           std::span<const double> velocity,
                                           std::span<const double> pressure,
                                           const CfdLinearSolve& solve,
                                           const CfdIterationSettings& settings,
                                           bool couplePressure = false) {
    const std::vector<double> flux = producedFlux(hand, boundary, velocity);
    // WITHOUT THE PRESSURE GRADIENT THE MOMENTUM EQUATION IS MISSING ONE OF ITS
    // TWO DRIVING FORCES, and a review pointed out that every fixture here left
    // it out -- so the pressure was a pure OUTPUT and nothing could converge in
    // the usual sense. The benchmark passes `couplePressure` and is a real
    // SIMPLE run; the single-stage tests leave it off deliberately, because
    // they are about one stage and not about a converging loop.
    const std::vector<cfd::PressureGradient3> gradient =
        couplePressure ? pressureGradientOf(hand, boundary, pressure)
                       : std::vector<cfd::PressureGradient3>{};
    const std::vector<CfdMomentumSystem> systems =
        momentumSystems(hand, boundary, flux, velocity, gradient);
    const CfdCouplingIterationInput input{.momentum = systems,
                                          .velocity = velocity,
                                          .pressure = pressure,
                                          .density = kDensity,
                                          .iteration = settings,
                                          .linearSolver = CfdLinearSolverSettings{}};
    std::expected<CfdCouplingIteration, CfdCouplingFailure> iteration =
        cfd::advanceCfdCoupling(hand.topology, hand.geometry, boundary, input, solve);
    INFO((iteration.has_value() ? std::string{} : iteration.error().error.message));
    REQUIRE(iteration.has_value());
    return std::move(*iteration);
}

} // namespace

TEST_CASE("CfdCoupling_OneIterationClosesContinuityOnTheCorrectedFlux", "[cfd][pvc]") {
    // THE CENTRAL CLAIM, measured by the QUALIFIED continuity evaluator over the
    // corrected flux rather than by this operator's own arithmetic.
    const Hand hand = Hand::make(chainPoints(6), chainTets(6));
    const PreparedCfdBoundary boundary = hand.mixed(inlet(0.05), outlet(0.0));
    const std::vector<double> velocity = zeros(3 * hand.topology.cells());
    const std::vector<double> pressure = zeros(hand.topology.cells());
    const CfdCouplingIteration iteration =
        advance(hand, boundary, velocity, pressure, denseSolve(), CfdIterationSettings{});

    INFO("maxCellContinuity " << iteration.balance.maxCellContinuity << " systemResidual "
                              << iteration.summary.systemResidual);
    // THE IDENTITY, read through the two reported numbers.
    CHECK(std::abs(iteration.residuals.continuity - iteration.summary.systemResidual) <=
          cfd::kCfdCouplingClosureFloor);
    CHECK(iteration.balance.maxCellContinuity <=
          iteration.summary.systemResidual + cfd::kCfdCouplingClosureFloor);
}

TEST_CASE("CfdCoupling_WhatTheSolverREPORTSChangesNoResultAndNoMeasure", "[cfd][pvc]") {
    // AN EARLIER FORM OF THIS TEST ASSERTED THE OPPOSITE, and the assertion was
    // the defect. It checked that a closure BOUND moved with the solver's
    // reported residual -- and `ADR-075`'s own amendment records why that
    // derivation was wrong: the solver reports an L2 norm over a matrix scale
    // while the continuity measure is an infinity norm over the throughput, and
    // the ratio between them drifts with mesh size.
    //
    // Nothing this operator measures may depend on what a port chooses to
    // REPORT about a solution it has already returned. Here the same exact
    // solve reports a residual a million times larger.
    const Hand hand = Hand::make(chainPoints(5), chainTets(5));
    const PreparedCfdBoundary boundary = hand.mixed(inlet(0.05), outlet(0.0));
    const std::vector<double> velocity = zeros(3 * hand.topology.cells());
    const std::vector<double> pressure = zeros(hand.topology.cells());

    const CfdCouplingIteration tight =
        advance(hand, boundary, velocity, pressure, denseSolve(), CfdIterationSettings{});
    const CfdCouplingIteration loose =
        advance(hand, boundary, velocity, pressure, denseSolve(1.0e6), CfdIterationSettings{});

    // The report differs...
    CHECK(loose.summary.pressureSolveResidual > tight.summary.pressureSolveResidual);
    // ...and nothing else does.
    CHECK(tight.summary.systemResidual == loose.summary.systemResidual);
    CHECK(tight.summary.continuityScale == loose.summary.continuityScale);
    CHECK(tight.residuals == loose.residuals);
    CHECK(tight.balance == loose.balance);
    REQUIRE(tight.corrected.faceFlux.size() == loose.corrected.faceFlux.size());
    for (std::size_t f = 0; f < tight.corrected.faceFlux.size(); ++f) {
        INFO("face " << f);
        CHECK(tight.corrected.faceFlux[f] == loose.corrected.faceFlux[f]);
    }
}

TEST_CASE("CfdCoupling_RepeatedAdvanceIsBitIdentical", "[cfd][pvc]") {
    // ADR-075 Decision 7 and Q5: a shuffled face order was bit-identical in 0
    // of 200 trials, so every loop runs in index order. Determinism is this
    // operator's; a non-deterministic supplied solve would make the run
    // non-deterministic and nothing here could detect it.
    const Hand hand = Hand::make(chainPoints(6), chainTets(6));
    const PreparedCfdBoundary boundary = hand.mixed(inlet(0.05), outlet(0.0));
    const std::vector<double> velocity = zeros(3 * hand.topology.cells());
    const std::vector<double> pressure = zeros(hand.topology.cells());

    const CfdCouplingIteration a =
        advance(hand, boundary, velocity, pressure, denseSolve(), CfdIterationSettings{});
    const CfdCouplingIteration b =
        advance(hand, boundary, velocity, pressure, denseSolve(), CfdIterationSettings{});

    CHECK(a.corrected == b.corrected);
    CHECK(a.pressureCorrection == b.pressureCorrection);
    CHECK(a.residuals == b.residuals);
    CHECK(a.balance == b.balance);
    CHECK(a.summary == b.summary);
}

TEST_CASE("CfdCoupling_AClosedDomainAtRestIsAnExactFixedPoint", "[cfd][pvc]") {
    // AN ANALYTIC BENCHMARK. Every boundary a wall, the fluid at rest and the
    // pressure uniform: the exact steady solution is u = 0 with p constant, so
    // the pressure correction must be exactly zero and the velocity must not
    // move. Nothing here is read from the implementation.
    const Hand hand = Hand::make(chainPoints(8), chainTets(8));
    const PreparedCfdBoundary boundary = hand.uniform(wall());
    const std::vector<double> velocity = zeros(3 * hand.topology.cells());
    const std::vector<double> pressure(hand.topology.cells(), kDiscriminatingConstant);

    const CfdCouplingIteration iteration =
        advance(hand, boundary, velocity, pressure, denseSolve(), CfdIterationSettings{});

    for (std::size_t c = 0; c < hand.topology.cells(); ++c) {
        INFO("cell " << c);
        CHECK(iteration.pressureCorrection[c] == 0.0);
        CHECK(iteration.corrected.pressure[c] == kDiscriminatingConstant);
    }
    for (std::size_t k = 0; k < iteration.corrected.velocity.size(); ++k) {
        INFO("component-blocked index " << k);
        CHECK(iteration.corrected.velocity[k] == 0.0);
    }
    for (std::size_t f = 0; f < hand.topology.faces(); ++f) {
        INFO("face " << f);
        CHECK(iteration.corrected.faceFlux[f] == 0.0);
    }
    CHECK(iteration.balance.maxCellContinuity == 0.0);
}

TEST_CASE("CfdCoupling_AMovingWallDomainStillConservesMassExactly", "[cfd][pvc]") {
    // A moving wall prescribes a VELOCITY and still carries no through-flow, so
    // the domain is closed and the flux is bit-zero at every facet whatever the
    // wall does. ADR-067's table gives all three wall kinds `phi = FixedValue 0`.
    const Hand hand = Hand::make(chainPoints(6), chainTets(6));

    // A MOVING WALL'S VELOCITY MUST BE TANGENTIAL TO ITS FACET, which
    // `bindCfdBoundary` enforces: "its wall velocity is not tangential to its
    // target ... and the wall does not move". An arbitrary triple is refused
    // before this operator sees anything, so the tangent is CONSTRUCTED from
    // the facet's own outward normal rather than guessed.
    const std::optional<Direction3D> outward =
        hand.geometry.boundaryOutwardNormal(BoundaryFaceIndex::fromValue(0));
    REQUIRE(outward.has_value());
    const std::array<double, 3> n{outward->x(), outward->y(), outward->z()};
    // Cross the normal with whichever axis it is least aligned to, so the
    // result cannot be degenerate.
    std::size_t least = 0;
    for (std::size_t i = 1; i < 3; ++i) {
        if (std::abs(n[i]) < std::abs(n[least])) {
            least = i;
        }
    }
    const std::array<double, 3> axis{least == 0 ? 1.0 : 0.0, least == 1 ? 1.0 : 0.0,
                                     least == 2 ? 1.0 : 0.0};
    std::array<double, 3> tangent{n[1] * axis[2] - n[2] * axis[1], n[2] * axis[0] - n[0] * axis[2],
                                  n[0] * axis[1] - n[1] * axis[0]};
    const double length =
        std::sqrt(tangent[0] * tangent[0] + tangent[1] * tangent[1] + tangent[2] * tangent[2]);
    REQUIRE(length > 0.0);
    const double speed = 0.1;
    for (double& t : tangent) {
        t = t / length * speed;
    }
    const CfdBoundaryCondition moving{
        CfdBoundaryConditionId::fromValue(4),
        MovingWall{someFace(4),
                   cfd::Velocity3{Velocity::fromSi(tangent[0]), Velocity::fromSi(tangent[1]),
                                  Velocity::fromSi(tangent[2])}}};
    const PreparedCfdBoundary boundary =
        hand.oneFacetAt(BoundaryFaceIndex::fromValue(0), moving, wall());
    const std::vector<double> velocity = zeros(3 * hand.topology.cells());
    const std::vector<double> pressure = zeros(hand.topology.cells());

    const CfdCouplingIteration iteration =
        advance(hand, boundary, velocity, pressure, denseSolve(), CfdIterationSettings{});
    for (std::size_t b = 0; b < hand.topology.boundaryFaces(); ++b) {
        const FaceIndex face = hand.topology.faceOf(BoundaryFaceIndex::fromValue(b));
        INFO("boundary face " << b);
        CHECK(iteration.corrected.faceFlux[face.value()] == 0.0);
    }
}

TEST_CASE("CfdCoupling_TheSimpleBenchmarkConservesMassAndClosesContinuity", "[cfd][pvc]") {
    // AN ANALYTIC BENCHMARK, and the only test here that runs a LOOP. The loop
    // is the TEST's, because "Convergence loop" is P19-STEADY-001's item; what
    // this operator provides is one iteration and the criteria.
    //
    // The independent claim: whatever the fields converge to, a duct with one
    // inlet and one outlet must conserve mass globally, and the corrected flux
    // must satisfy discrete continuity to the solve's accuracy at EVERY
    // iteration -- not only at the end.
    const Hand hand = Hand::make(chainPoints(10), chainTets(10));
    const PreparedCfdBoundary boundary = hand.mixed(inlet(0.02), outlet(0.0));

    std::vector<double> velocity = zeros(3 * hand.topology.cells());
    std::vector<double> pressure = zeros(hand.topology.cells());
    CfdIterationSettings settings;
    settings.velocityRelaxation = 0.7;
    settings.pressureRelaxation = 0.3;

    std::optional<CfdResiduals> first;
    std::size_t iterations = 0;
    double worstClosure = 0.0;
    for (std::size_t k = 0; k < 25; ++k) {
        const CfdCouplingIteration iteration =
            advance(hand, boundary, velocity, pressure, denseSolve(), settings);
        ++iterations;
        if (!first.has_value()) {
            first = iteration.residuals;
        }

        // EVERY iteration closes continuity, not just the last.
        INFO("iteration " << k << " closure " << iteration.balance.maxCellContinuity
                          << " systemResidual " << iteration.summary.systemResidual);
        worstClosure = std::max(worstClosure, iteration.balance.maxCellContinuity);

        // And the global mass balance is near zero: inflow matches outflow.
        CHECK(iteration.balance.globalImbalance <= 1.0e-9);

        velocity = iteration.corrected.velocity;
        pressure = iteration.corrected.pressure;

        const CfdCouplingVerdict verdict =
            cfd::cfdCouplingVerdict(iteration.residuals, *first, CfdConvergenceSettings{});
        CHECK(verdict != CfdCouplingVerdict::Diverged);
        if (verdict == CfdCouplingVerdict::Converged) {
            break;
        }
    }
    INFO("iterations " << iterations << " worst closure " << worstClosure);
    CHECK(iterations >= 1);
    CHECK(worstClosure <= 1.0e-6);
}

TEST_CASE("CfdCoupling_ASingleCellDomainIsHandled", "[cfd][pvc]") {
    // The smallest mesh there is: one cell, every face on the boundary, no
    // internal face at all. The matrix is 1x1 and the pin is the whole system.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    REQUIRE(hand.topology.cells() == 1);
    REQUIRE(hand.topology.internalFaces() == 0);
    const PreparedCfdBoundary boundary = hand.uniform(wall());
    const CfdPressureCorrectionSystem system = assemble(
        hand, boundary, zeros(hand.topology.faces()), unitDiagonal(hand, kDiscriminatingConstant));
    CHECK(system.matrix.rows() == 1);
    CHECK(system.summary.internalFacesCoupled == 0);
    REQUIRE(system.summary.pinnedCell.has_value());
    CHECK(system.summary.pinnedCell->value() == 0);
    CHECK(system.source[0] == 0.0);
}

TEST_CASE("CfdCoupling_InvalidSettingsAreRefusedBeforeAnythingIsAssembled", "[cfd][pvc]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.uniform(wall());
    const std::vector<double> velocity = zeros(3 * hand.topology.cells());
    const std::vector<double> flux = producedFlux(hand, boundary, velocity);
    const std::vector<CfdMomentumSystem> systems =
        momentumSystems(hand, boundary, flux, velocity);

    CfdIterationSettings settings;
    settings.velocityRelaxation = 0.0;  // outside (0, 1]
    const CfdCouplingPredictorInput input{.momentum = systems,
                                          .velocity = velocity,
                                          .iteration = settings,
                                          .linearSolver = CfdLinearSolverSettings{}};
    std::expected<CfdCouplingPredictor, CfdCouplingFailure> predicted =
        cfd::cfdCouplingPredictor(hand.topology, hand.geometry, boundary, input, denseSolve());
    REQUIRE_FALSE(predicted.has_value());
    CHECK(predicted.error().problem == CfdCouplingProblem::SettingsInvalid);
}

TEST_CASE("CfdCoupling_TheMeasuredConstantsSitAboveTheirMeasurements", "[cfd][pvc]") {
    // The bounds are MEASURED, and tightening one below its measurement must
    // break the BUILD rather than a test. These static_asserts carry the
    // measurement in their own failure messages.
    static_assert(cfd::kCfdCouplingSymmetryTolerance >= 1.0e-13,
                  "ADR-075 Q3 measured the assembled symmetry error at exactly 0.0, and Q1 "
                  "measured a per-cell assembly's worst relative disagreement at 1.241051e-14");
    static_assert(cfd::kCfdCouplingCompatibilityTolerance >= 1.0e-14,
                  "ADR-075 Q2 measured a balanced flux's compatibility residual at 6.217249e-15");
    static_assert(cfd::kCfdCouplingClosureFloor >= 1.0e-14,
                  "ADR-075 Q6 measured the worst closure after correction at 2.020606e-14 "
                  "relative to the boundary throughput");
    static_assert(cfd::kCfdCouplingNormalTolerance >= 1.0e-13,
                  "sum_i n_i^2 - 1 for a unit normal is a three-term sum of squares of "
                  "well-conditioned doubles");
    // AND BOUNDED FROM ABOVE AS WELL AS BELOW. A one-sided assert admits any
    // amount of LOOSENING, which is the direction that hides defects: the first
    // form of this block let kCfdCouplingCompatibilityTolerance be 1e+3.
    static_assert(cfd::kCfdCouplingSymmetryTolerance <= 1.0e-12,
                  "a correct assembly is symmetric to the last bit (ADR-075 Q3 measured exactly "
                  "0.0), so this gate must stay tight enough to catch a defective scatter");
    static_assert(cfd::kCfdCouplingCompatibilityTolerance <= 1.0e-12,
                  "ADR-075 Q2 measured an UNBALANCED flux at 5.327555e+00, so any bound far "
                  "below that discriminates -- but a loose one stops being a check");
    static_assert(cfd::kCfdCouplingClosureFloor <= 1.0e-12,
                  "the closure allowance covers the identity's arithmetic gap, measured at "
                  "1.686762e-14 of throughput, and nothing larger");
    static_assert(cfd::kCfdCouplingNormalTolerance <= 1.0e-10,
                  "sum_i n_i^2 - 1 is a three-term sum of squares of well-conditioned doubles");
    CHECK(true);
}

// ===========================================================================
// WHAT THE TWO ADVERSARIAL REVIEWS FORCED INTO THIS SUITE
//
// The test-adequacy review's structural finding: the identity tests compare two
// PRODUCTION OUTPUTS, so they are satisfied by any `a_f` -- wrong by 2x, wrong
// by 1e7, taken from the wrong component, or interpolated with `1 - w`. The
// corrected flux is INVARIANT under a uniform rescaling of `a_f`, because the
// correction is a projection: `A` scales, `p'` scales inversely, and `a_f p'`
// does not move. So the coefficient's VALUE has to be pinned directly, against
// a diagonal that varies per cell AND per component -- with a constant one
// `interpolate(d, d, w) == d` for every weight and `sum_i n_i^2 d^i == d`
// exactly, and neither the weight nor the projection is exercised at all.
// ===========================================================================

namespace {

/// A diagonal that varies per cell AND per component, so that the
/// interpolation weight and the normal projection both matter.
///
/// NO TWO ENTRIES SHARE A FACTOR OF TWO, so a mutation that swaps a component
/// or drops a term cannot hide behind exact binary arithmetic.
[[nodiscard]] std::vector<double> varyingDiagonal(const Hand& hand) {
    const std::size_t cells = hand.topology.cells();
    std::vector<double> out(3 * cells, 0.0);
    for (std::size_t c = 0; c < cells; ++c) {
        for (std::size_t i = 0; i < 3; ++i) {
            out[i * cells + c] = kDiscriminatingConstant *
                                 (1.0 + 0.37 * static_cast<double>(c) + 0.13 * static_cast<double>(i));
        }
    }
    return out;
}

/// Two tetrahedra that share no node: a DISCONNECTED mesh.
[[nodiscard]] std::vector<Point3D> twoRegionPoints() {
    return {Point3D{0_mm, 0_mm, 0_mm},   Point3D{10_mm, 0_mm, 0_mm},
            Point3D{0_mm, 10_mm, 0_mm},  Point3D{0_mm, 0_mm, 10_mm},
            Point3D{50_mm, 0_mm, 0_mm},  Point3D{60_mm, 0_mm, 0_mm},
            Point3D{50_mm, 10_mm, 0_mm}, Point3D{50_mm, 0_mm, 10_mm}};
}
const std::vector<std::array<std::size_t, 4>> kTwoRegions{{0, 1, 2, 3}, {4, 5, 6, 7}};

[[nodiscard]] CfdBoundaryCondition slip() {
    return CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(5), SlipWall{someFace(5)}};
}

[[nodiscard]] CfdBoundaryCondition pressureInlet(double pressure) {
    return CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(6),
                                PressureInlet{someFace(6), Pressure::fromSi(pressure)}};
}

} // namespace

TEST_CASE("CfdCoupling_TheFaceCoefficientHasTheValueItsFormulaGives", "[cfd][pvc]") {
    // THE TEST THE IDENTITY CANNOT BE. The oracle computes `d = V/a` from the
    // geometry itself and groups the product differently, so a mutation to the
    // coefficient's VALUE -- a doubled volume, a wrong component, a reversed
    // interpolation -- shows up here and nowhere else.
    const Hand hand = Hand::make(gradedChainPoints(6), chainTets(6));
    const PreparedCfdBoundary boundary = hand.mixed(inlet(0.1), outlet(0.0));
    const std::vector<double> velocity = zeros(3 * hand.topology.cells());
    const std::vector<double> flux = producedFlux(hand, boundary, velocity);
    const std::vector<double> diagonal = varyingDiagonal(hand);
    const CfdPressureCorrectionSystem system = assemble(hand, boundary, flux, diagonal);

    const std::size_t cells = hand.topology.cells();
    REQUIRE(hand.topology.internalFaces() > 0);
    std::size_t checked = 0;
    for (std::size_t f = 0; f < hand.topology.internalFaces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        const std::size_t owner = hand.topology.owner(face).value();
        const std::size_t neighbour = hand.topology.neighbour(face)->value();
        const std::optional<double> weight = cfd::cfdFaceInterpolationWeight(hand.geometry, face);
        const std::optional<Direction3D> normal = hand.geometry.faceUnitNormal(face);
        REQUIRE(weight.has_value());
        REQUIRE(normal.has_value());
        const std::array<double, 3> n{normal->x(), normal->y(), normal->z()};

        const double volumeOwner = hand.geometry.cellVolume(CellIndex::fromValue(owner)).si();
        const double volumeNeighbour =
            hand.geometry.cellVolume(CellIndex::fromValue(neighbour)).si();

        // Grouped differently from production, which multiplies rho in first.
        double projected = 0.0;
        for (std::size_t i = 0; i < 3; ++i) {
            const double dOwner = volumeOwner / diagonal[i * cells + owner];
            const double dNeighbour = volumeNeighbour / diagonal[i * cells + neighbour];
            projected += n[i] * n[i] * (dNeighbour + *weight * (dOwner - dNeighbour));
        }
        const double expected = projected * (kDensity.si() * hand.geometry.faceArea(face).si() /
                                             hand.geometry.orthogonalDistance(face).si());

        INFO("internal face " << f << " owner " << owner << " neighbour " << neighbour
                              << " weight " << *weight);
        CHECK_THAT(system.matrix.coeffSi(owner, neighbour),
                   Catch::Matchers::WithinRel(-expected, 1.0e-13));
        CHECK_THAT(system.matrix.coeffSi(neighbour, owner),
                   Catch::Matchers::WithinRel(-expected, 1.0e-13));
        ++checked;
    }
    CHECK(checked == hand.topology.internalFaces());

    // AND THE PREMISES THAT MAKE THE ABOVE BITE, ASSERTED WITH A MARGIN.
    //
    // THE FIRST FORM OF THIS CHECK WAS VACUOUS: it asked whether any weight was
    // "not exactly 0.5", which floating-point noise satisfies on a uniform
    // mesh, and a probe that reversed the interpolation then SURVIVED because
    // at w = 0.5 the two forms agree exactly. A premise needs a margin, not an
    // inequality.
    CHECK(system.summary.largestDiagonalSpread > 0.0);
    double worstOffset = 0.0;
    for (std::size_t f = 0; f < hand.topology.internalFaces(); ++f) {
        const std::optional<double> weight =
            cfd::cfdFaceInterpolationWeight(hand.geometry, FaceIndex::fromValue(f));
        REQUIRE(weight.has_value());
        worstOffset = std::max(worstOffset, std::abs(*weight - 0.5));
    }
    INFO("largest |weight - 0.5| over the internal faces: " << worstOffset);
    CHECK(worstOffset > 0.05);
}

TEST_CASE("CfdCoupling_TheBoundaryCoefficientUsesTheOwnersOwnCoefficient", "[cfd][pvc]") {
    // The same value test at a fixed-pressure facet, where the weight is 1 and
    // the owner's `d` is used alone.
    const Hand hand = Hand::make(chainPoints(5), chainTets(5));
    const PreparedCfdBoundary boundary = hand.mixed(inlet(0.1), outlet(0.0));
    const std::vector<double> velocity = zeros(3 * hand.topology.cells());
    const std::vector<double> flux = producedFlux(hand, boundary, velocity);
    const std::vector<double> diagonal = varyingDiagonal(hand);
    const CfdPressureCorrectionSystem system = assemble(hand, boundary, flux, diagonal);

    const std::size_t cells = hand.topology.cells();
    std::vector<double> expectedDiagonal(cells, 0.0);
    for (std::size_t f = 0; f < hand.topology.internalFaces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        const std::size_t owner = hand.topology.owner(face).value();
        const std::size_t neighbour = hand.topology.neighbour(face)->value();
        const double coefficient = -system.matrix.coeffSi(owner, neighbour);
        expectedDiagonal[owner] += coefficient;
        expectedDiagonal[neighbour] += coefficient;
    }
    std::size_t fixed = 0;
    for (std::size_t b = 0; b < hand.topology.boundaryFaces(); ++b) {
        const BoundaryFaceIndex boundaryFace = BoundaryFaceIndex::fromValue(b);
        const std::optional<CfdBoundaryTreatment> treatment = boundary.treatment(boundaryFace);
        REQUIRE(treatment.has_value());
        if (treatment->pressure != CfdBoundaryPolicy::FixedValue) {
            continue;
        }
        const FaceIndex face = hand.topology.faceOf(boundaryFace);
        const std::size_t owner = hand.topology.owner(face).value();
        const std::optional<Direction3D> normal = hand.geometry.faceUnitNormal(face);
        REQUIRE(normal.has_value());
        const std::array<double, 3> n{normal->x(), normal->y(), normal->z()};
        const double volume = hand.geometry.cellVolume(CellIndex::fromValue(owner)).si();
        double projected = 0.0;
        for (std::size_t i = 0; i < 3; ++i) {
            projected += n[i] * n[i] * (volume / diagonal[i * cells + owner]);
        }
        expectedDiagonal[owner] += projected * (kDensity.si() * hand.geometry.faceArea(face).si() /
                                                hand.geometry.orthogonalDistance(face).si());
        ++fixed;
    }
    CHECK(fixed > 0);
    REQUIRE_FALSE(system.summary.pinnedCell.has_value());
    for (std::size_t c = 0; c < cells; ++c) {
        INFO("cell " << c);
        CHECK_THAT(system.matrix.coeffSi(c, c),
                   Catch::Matchers::WithinRel(expectedDiagonal[c], 1.0e-12));
    }
}

TEST_CASE("CfdCoupling_TheIdentityHoldsAwayFromThePinnedCellAndIsSilentAtIt", "[cfd][pvc]") {
    // THE PINNED CASE, WHICH NEITHER IDENTITY TEST REACHED. Both used
    // mixed(inlet, outlet), and a PressureOutlet fixes the pressure, so nothing
    // was ever pinned in them. The header claimed the identity "exactly" with
    // no carve-out; its own correctness review worked the algebra and found it
    // false at the pinned cell. Here it is, measured.
    const Hand hand = Hand::make(chainPoints(7), chainTets(7));
    const PreparedCfdBoundary boundary = hand.uniform(wall());
    const std::vector<double> diagonal = varyingDiagonal(hand);

    // A closed domain with REAL interior flow: zero at every boundary face, so
    // the prescribed flux balances exactly, and non-zero internally.
    std::vector<double> flux = zeros(hand.topology.faces());
    for (std::size_t f = 0; f < hand.topology.internalFaces(); ++f) {
        flux[f] = 1.0e-4 * (1.0 + static_cast<double>(f % 5)) * ((f % 2 == 0) ? 1.0 : -1.0);
    }
    const CfdPressureCorrectionSystem system = assemble(hand, boundary, flux, diagonal);
    REQUIRE(system.summary.pinnedCell.has_value());
    const std::size_t pinned = system.summary.pinnedCell->value();

    std::expected<CfdLinearSolveResult, Error> solved =
        denseSolve()(system.matrix, system.source, CfdLinearSolverSettings{});
    REQUIRE(solved.has_value());
    CHECK(solved->solution[pinned] == 0.0);

    const CfdCouplingCorrectionInput input{.pressureCorrection = solved->solution,
                                           .pressure = zeros(hand.topology.cells()),
                                           .predictedVelocity = zeros(3 * hand.topology.cells()),
                                           .predictedFaceFlux = flux,
                                           .momentumDiagonal = diagonal,
                                           .density = kDensity,
                                           .iteration = CfdIterationSettings{}};
    std::expected<CfdCouplingCorrection, CfdCouplingFailure> corrected =
        cfd::applyCfdCouplingCorrection(hand.topology, hand.geometry, boundary, input);
    INFO((corrected.has_value() ? std::string{} : corrected.error().error.message));
    REQUIRE(corrected.has_value());

    const std::vector<double> measured = divergenceOf(hand.topology, corrected->faceFlux);
    const std::vector<double> predicted =
        residualOf(system.matrix, system.source, solved->solution);

    double scale = 0.0;
    for (const double phi : flux) {
        scale = std::max(scale, std::abs(phi));
    }
    REQUIRE(scale > 0.0);

    // AWAY FROM THE PIN: the identity holds.
    for (std::size_t c = 0; c < measured.size(); ++c) {
        if (c == pinned) {
            continue;
        }
        INFO("cell " << c);
        CHECK(std::abs(measured[c] - predicted[c]) / scale <= 1.0e-12);
    }

    // AT THE PIN: IT HOLDS TOO, AND A REVIEW PREDICTED THAT IT WOULD NOT.
    //
    // The argument against was sound as far as it went: the pinned row of `A`
    // and its right-hand side are cleared, so `(A p') - b` is zero there by
    // construction, while the measured flux sum "ought to be" the unpinned
    // system's compatibility residual. This measures both sides instead.
    //
    // Both are zero, and the mechanism is the compatibility condition. Pinning
    // happens only where no boundary fixes the pressure, so every boundary
    // prescribes its flux and the gate requires those to balance; global
    // conservation then forces the pinned cell's sum to the negative of all the
    // others, each of which is zero to solve accuracy. The non-zero quantity in
    // sight is the PINNED system's source sum -- that cell's own original
    // imbalance, which the pin removed from the right-hand side -- and it is
    // NOT what the measured divergence equals.
    double sourceSum = 0.0;
    for (const double b : system.source) {
        sourceSum += b;
    }
    INFO("pinned cell " << pinned << " measured " << measured[pinned] << " predicted "
                        << predicted[pinned] << " pinned-system source sum " << sourceSum);
    CHECK(predicted[pinned] == 0.0);
    CHECK(std::abs(measured[pinned]) / scale <= 1.0e-12);
    // The source sum is non-zero, so the agreement above is not two zeros that
    // coincide because nothing was flowing.
    CHECK(std::abs(sourceSum) / scale > 1.0e-3);
}

TEST_CASE("CfdCoupling_ASlipWallMakesTheThreeComponentDiagonalsDiffer", "[cfd][pvc]") {
    // DECISION 11 WAS EFFECTIVELY UNTESTED, and its own review said why: a slip
    // wall's `Symmetry` velocity is the ONLY treatment that makes the three
    // momentum matrices differ per component. Everywhere else the three
    // diagonals are bit-identical, so `sum_i n_i^2 d^i` is always evaluated as
    // `d * sum_i n_i^2` and the projection is never exercised.
    const Hand hand = Hand::make(chainPoints(6), chainTets(6));
    const PreparedCfdBoundary boundary = hand.oneFacetAt(BoundaryFaceIndex::fromValue(0), slip(),
                                                         wall());
    const std::vector<double> velocity = zeros(3 * hand.topology.cells());
    const std::vector<double> flux = producedFlux(hand, boundary, velocity);
    const std::vector<CfdMomentumSystem> systems =
        momentumSystems(hand, boundary, flux, velocity);

    // THE PREMISE, ASSERTED: the three assembled matrices are NOT identical.
    std::size_t differing = 0;
    for (std::size_t cell = 0; cell < hand.topology.cells(); ++cell) {
        const double x = systems[0].matrix.coeffSi(cell, cell);
        const double y = systems[1].matrix.coeffSi(cell, cell);
        const double z = systems[2].matrix.coeffSi(cell, cell);
        if (x != y || y != z) {
            ++differing;
        }
    }
    INFO("cells whose three momentum diagonals differ: " << differing);
    CHECK(differing > 0);

    // And the coupling reports the spread rather than hiding it.
    const CfdCouplingPredictorInput predictorInput{.momentum = systems,
                                                   .velocity = velocity,
                                                   .iteration = CfdIterationSettings{},
                                                   .linearSolver = CfdLinearSolverSettings{}};
    std::expected<CfdCouplingPredictor, CfdCouplingFailure> predicted =
        cfd::cfdCouplingPredictor(hand.topology, hand.geometry, boundary, predictorInput,
                                  denseSolve());
    INFO((predicted.has_value() ? std::string{} : predicted.error().error.message));
    REQUIRE(predicted.has_value());
    const CfdPressureCorrectionSystem system =
        assemble(hand, boundary, flux, predicted->diagonal);
    CHECK(system.summary.largestDiagonalSpread > 0.0);
    CHECK(system.matrix.largestSymmetryError() == 0.0);
}

TEST_CASE("CfdCoupling_APressureInletFixesThePressureLevelLikeAnOutlet", "[cfd][pvc]") {
    // `PressureInlet` was never exercised: the import was there and unused.
    const Hand hand = Hand::make(chainPoints(6), chainTets(6));
    const PreparedCfdBoundary boundary = hand.mixed(pressureInlet(50.0), outlet(0.0));
    const std::vector<double> velocity = zeros(3 * hand.topology.cells());
    const std::vector<double> flux = producedFlux(hand, boundary, velocity);
    const CfdPressureCorrectionSystem system =
        assemble(hand, boundary, flux, varyingDiagonal(hand));
    CHECK(system.summary.boundaryFacesFixedPressure == hand.topology.boundaryFaces());
    CHECK(system.summary.boundaryFacesZeroGradient == 0);
    CHECK_FALSE(system.summary.constantModeWasInTheNullSpace);
    CHECK_FALSE(system.summary.pinnedCell.has_value());
    CHECK(system.matrix.largestSymmetryError() == 0.0);
}

TEST_CASE("CfdCoupling_ADisconnectedRegionWithNoPressureIsRefused", "[cfd][pvc]") {
    // ONE PIN FIXES ONE REGION. The first draft read a GLOBAL "did any boundary
    // fix the pressure", so on a disconnected mesh it would conclude "nothing
    // to pin" as soon as either region had a pressure boundary -- leaving the
    // other region's constant mode free and the matrix singular, which is
    // precisely the precondition `solveSymmetricSparse` needs.
    const Hand hand = Hand::make(twoRegionPoints(), kTwoRegions);
    REQUIRE(hand.topology.cells() == 2);
    REQUIRE(hand.topology.internalFaces() == 0);

    // Every facet a wall: both regions are pure Neumann, and the reference cell
    // can only be in one of them.
    const PreparedCfdBoundary boundary = hand.uniform(wall());
    const CfdPressureCorrectionInput input{.predictedFaceFlux = zeros(hand.topology.faces()),
                                           .momentumDiagonal = varyingDiagonal(hand),
                                           .density = kDensity};
    const CfdCouplingFailure failure = refusal(hand, boundary, input);
    CHECK(failure.problem == CfdCouplingProblem::PressureLevelNotFixed);
    CHECK_THAT(failure.error.message, Catch::Matchers::ContainsSubstring("connected region"));
}

TEST_CASE("CfdCoupling_ANonZeroBalancedFluxClearsTheCompatibilityGate", "[cfd][pvc]") {
    // THE BALANCED SIDE OF THE GATE WAS NEVER MEASURED. The only balanced case
    // used an all-zero flux, so the throughput was zero, `relativeDeparture`
    // took its no-scale branch and returned the literal 0.0 -- the assertion
    // tested a hard-coded constant. ADR-075 Q2's balanced figure
    // (6.217249e-15) came from a NON-ZERO flux, and this is that case.
    const Hand hand = Hand::make(chainPoints(5), chainTets(5));
    const PreparedCfdBoundary boundary = hand.uniform(wall());
    std::vector<double> flux = zeros(hand.topology.faces());
    REQUIRE(hand.topology.boundaryFaces() >= 2);
    const FaceIndex a = hand.topology.faceOf(BoundaryFaceIndex::fromValue(0));
    const FaceIndex b = hand.topology.faceOf(BoundaryFaceIndex::fromValue(1));
    flux[a.value()] = -kDiscriminatingConstant;
    flux[b.value()] = +kDiscriminatingConstant;

    const CfdPressureCorrectionSystem system =
        assemble(hand, boundary, flux, varyingDiagonal(hand));
    // The throughput is genuinely non-zero, so the residual is a real ratio.
    CHECK(system.summary.boundaryThroughput > 0.0);
    CHECK_THAT(system.summary.boundaryThroughput,
               Catch::Matchers::WithinRel(kDiscriminatingConstant, 1.0e-15));
    INFO("compatibility residual " << system.summary.compatibilityResidual);
    CHECK(system.summary.compatibilityResidual <= cfd::kCfdCouplingCompatibilityTolerance);
    CHECK(system.summary.compatibilityResidual >= 0.0);

    // AND A FLUX OFF BY ONE PART IN 1e9 IS REFUSED, so the gate is not 13
    // decades wide: the only unbalanced case before this sat at exactly 2.0.
    flux[b.value()] = kDiscriminatingConstant * (1.0 + 1.0e-9);
    const CfdPressureCorrectionInput tilted{.predictedFaceFlux = flux,
                                            .momentumDiagonal = varyingDiagonal(hand),
                                            .density = kDensity};
    CHECK(refusal(hand, boundary, tilted).problem ==
          CfdCouplingProblem::PrescribedFluxUnbalanced);

    // A CASE THAT SITS JUST ABOVE THE BOUND, so the throughput's factor of one
    // half is load-bearing rather than decorative. A mutation probe dropped
    // that factor and SURVIVED, because every case was either at 2.0 or at
    // exactly 0.0 and a factor of two changed no verdict.
    //
    // With two faces at -C and +C(1+d), the net is C*d and the throughput is
    // about C, so the relative residual is about d. At d = 1.5e-13 the gate
    // refuses; halve the residual by dropping the 0.5 and it would not.
    flux[b.value()] = kDiscriminatingConstant * (1.0 + 1.5e-13);
    const CfdPressureCorrectionInput marginal{.predictedFaceFlux = flux,
                                              .momentumDiagonal = varyingDiagonal(hand),
                                              .density = kDensity};
    CHECK(refusal(hand, boundary, marginal).problem ==
          CfdCouplingProblem::PrescribedFluxUnbalanced);

    // AND JUST BELOW IT, so the bound is a threshold and not a floor on
    // everything.
    flux[b.value()] = kDiscriminatingConstant * (1.0 + 2.0e-14);
    const CfdPressureCorrectionInput inside{.predictedFaceFlux = flux,
                                            .momentumDiagonal = varyingDiagonal(hand),
                                            .density = kDensity};
    CHECK_FALSE(cfd::cfdPressureCorrectionProblem(hand.topology, hand.geometry, boundary, inside)
                    .has_value());
}

TEST_CASE("CfdCoupling_ThePredictorsVelocitySolvesTheRelaxedSystem", "[cfd][pvc]") {
    // `CfdCouplingPredictor::velocity` was read by NO assertion, so a predictor
    // that discarded the solve entirely would have survived the whole suite.
    const Hand hand = Hand::make(chainPoints(5), chainTets(5));
    const PreparedCfdBoundary boundary = hand.mixed(inlet(0.2), outlet(0.0));
    std::vector<double> velocity = zeros(3 * hand.topology.cells());
    for (std::size_t k = 0; k < velocity.size(); ++k) {
        velocity[k] = 2.0e-7 * (1.0 + static_cast<double>(k % 4));
    }
    const std::vector<double> flux = producedFlux(hand, boundary, velocity);
    const std::vector<CfdMomentumSystem> systems =
        momentumSystems(hand, boundary, flux, velocity);

    CfdIterationSettings settings;
    settings.velocityRelaxation = 0.7;
    const CfdCouplingPredictorInput input{.momentum = systems,
                                          .velocity = velocity,
                                          .iteration = settings,
                                          .linearSolver = CfdLinearSolverSettings{}};
    std::expected<CfdCouplingPredictor, CfdCouplingFailure> predicted =
        cfd::cfdCouplingPredictor(hand.topology, hand.geometry, boundary, input, denseSolve());
    INFO((predicted.has_value() ? std::string{} : predicted.error().error.message));
    REQUIRE(predicted.has_value());

    // Rebuild the relaxed system HERE and solve it with the same independent
    // dense solve, then compare. This pins the relaxed matrix AND the deferred
    // source term, neither of which any assertion reached before.
    const std::size_t cells = hand.topology.cells();
    for (std::size_t c = 0; c < 3; ++c) {
        const CfdSparseMatrix& original = systems[c].matrix;
        std::vector<cfd::CfdSparseIndex> rowStart(original.rowStart().begin(),
                                                  original.rowStart().end());
        std::vector<cfd::CfdSparseIndex> inner(original.inner().begin(), original.inner().end());
        std::vector<double> values(original.values().begin(), original.values().end());
        std::vector<double> source = systems[c].source;
        for (std::size_t row = 0; row < cells; ++row) {
            for (cfd::CfdSparseIndex slot = rowStart[row]; slot < rowStart[row + 1]; ++slot) {
                if (inner[static_cast<std::size_t>(slot)] != static_cast<cfd::CfdSparseIndex>(row)) {
                    continue;
                }
                const double a = values[static_cast<std::size_t>(slot)];
                values[static_cast<std::size_t>(slot)] = a / 0.7;
                source[row] += ((1.0 - 0.7) / 0.7) * a * velocity[c * cells + row];
                break;
            }
        }
        std::expected<CfdSparseMatrix, cfd::CfdSparseFailure> relaxed = CfdSparseMatrix::fromCsr(
            cells, cells, std::move(rowStart), std::move(inner), std::move(values));
        REQUIRE(relaxed.has_value());
        std::expected<CfdLinearSolveResult, Error> mine =
            denseSolve()(*relaxed, source, CfdLinearSolverSettings{});
        REQUIRE(mine.has_value());
        for (std::size_t cell = 0; cell < cells; ++cell) {
            INFO("component " << c << " cell " << cell);
            CHECK_THAT(predicted->velocity[c * cells + cell],
                       Catch::Matchers::WithinRel(mine->solution[cell], 1.0e-11));
        }
    }
}

TEST_CASE("CfdCoupling_TheVelocityCorrectionIsTheFaceSumOverTheDiagonal", "[cfd][pvc]") {
    // The velocity correction's VALUE was pinned by nothing: one test saw zero
    // and one needed only `> 0.0`. Here the face sum is formed independently.
    const Hand hand = Hand::make(chainPoints(6), chainTets(6));
    const PreparedCfdBoundary boundary = hand.mixed(inlet(0.1), outlet(0.0));
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> diagonal = varyingDiagonal(hand);
    const std::vector<double> velocity = zeros(3 * cells);
    const std::vector<double> flux = producedFlux(hand, boundary, velocity);

    std::vector<double> correction(cells, 0.0);
    for (std::size_t c = 0; c < cells; ++c) {
        correction[c] = kDiscriminatingConstant * (1.0 + 0.41 * static_cast<double>(c));
    }

    const CfdCouplingCorrectionInput input{.pressureCorrection = correction,
                                           .pressure = zeros(cells),
                                           .predictedVelocity = velocity,
                                           .predictedFaceFlux = flux,
                                           .momentumDiagonal = diagonal,
                                           .density = kDensity,
                                           .iteration = CfdIterationSettings{}};
    std::expected<CfdCouplingCorrection, CfdCouplingFailure> corrected =
        cfd::applyCfdCouplingCorrection(hand.topology, hand.geometry, boundary, input);
    REQUIRE(corrected.has_value());

    // `u'^i_P = -(1/a^i_P) sum_f p'_f S_f,i`, formed here from the geometry.
    std::vector<std::array<double, 3>> faceSum(cells, std::array<double, 3>{0.0, 0.0, 0.0});
    for (std::size_t f = 0; f < hand.topology.internalFaces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        const std::size_t owner = hand.topology.owner(face).value();
        const std::size_t neighbour = hand.topology.neighbour(face)->value();
        const std::optional<double> weight = cfd::cfdFaceInterpolationWeight(hand.geometry, face);
        REQUIRE(weight.has_value());
        const double faceValue =
            correction[neighbour] + *weight * (correction[owner] - correction[neighbour]);
        const AreaVector3D area = hand.geometry.faceAreaVector(face);
        const std::array<double, 3> s{area.x.si(), area.y.si(), area.z.si()};
        for (std::size_t i = 0; i < 3; ++i) {
            faceSum[owner][i] += faceValue * s[i];
            faceSum[neighbour][i] -= faceValue * s[i];
        }
    }
    for (std::size_t b = 0; b < hand.topology.boundaryFaces(); ++b) {
        const BoundaryFaceIndex boundaryFace = BoundaryFaceIndex::fromValue(b);
        const FaceIndex face = hand.topology.faceOf(boundaryFace);
        const std::size_t owner = hand.topology.owner(face).value();
        const std::optional<CfdBoundaryTreatment> treatment = boundary.treatment(boundaryFace);
        REQUIRE(treatment.has_value());
        // `p'_b = 0` where the pressure is fixed, `p'_P` where it is not.
        const double faceValue = treatment->pressure == CfdBoundaryPolicy::FixedValue
                                     ? 0.0
                                     : correction[owner];
        const AreaVector3D area = hand.geometry.faceAreaVector(face);
        const std::array<double, 3> s{area.x.si(), area.y.si(), area.z.si()};
        for (std::size_t i = 0; i < 3; ++i) {
            faceSum[owner][i] += faceValue * s[i];
        }
    }
    for (std::size_t c = 0; c < cells; ++c) {
        for (std::size_t i = 0; i < 3; ++i) {
            const double expected = -faceSum[c][i] / diagonal[i * cells + c];
            INFO("cell " << c << " component " << i);
            CHECK_THAT(corrected->velocity[i * cells + c],
                       Catch::Matchers::WithinRel(expected, 1.0e-12));
        }
    }
}

TEST_CASE("CfdCoupling_TheMomentumResidualIsTheNormalisedResidualOfTheSuppliedSystem",
          "[cfd][pvc]") {
    // THE ORACLE WAS ALREADY IN THIS FILE AND UNUSED. `infinityNormOf`, `norm2`
    // and `residualOf` are a transcription of ADR-052's normalisation, and
    // nothing compared them with production's -- so a momentum residual
    // measured against the wrong component, or with the sign of `b` flipped,
    // would have survived.
    const Hand hand = Hand::make(chainPoints(6), chainTets(6));
    const PreparedCfdBoundary boundary = hand.mixed(inlet(0.15), outlet(0.0));
    const std::size_t cells = hand.topology.cells();
    std::vector<double> velocity = zeros(3 * cells);
    for (std::size_t k = 0; k < velocity.size(); ++k) {
        velocity[k] = 3.0e-7 * (1.0 + static_cast<double>(k % 5));
    }
    const std::vector<double> pressure = zeros(cells);
    const CfdCouplingIteration iteration =
        advance(hand, boundary, velocity, pressure, denseSolve(), CfdIterationSettings{});

    const std::vector<double> flux = producedFlux(hand, boundary, velocity);
    const std::vector<CfdMomentumSystem> systems =
        momentumSystems(hand, boundary, flux, velocity);
    const std::array<double, 3> reported{iteration.residuals.momentumX,
                                         iteration.residuals.momentumY,
                                         iteration.residuals.momentumZ};
    for (std::size_t c = 0; c < 3; ++c) {
        const std::vector<double> component(velocity.begin() + static_cast<std::ptrdiff_t>(c * cells),
                                            velocity.begin() +
                                                static_cast<std::ptrdiff_t>((c + 1) * cells));
        const std::vector<double> r = residualOf(systems[c].matrix, systems[c].source, component);
        const double denominator =
            infinityNormOf(systems[c].matrix) * norm2(component) + norm2(systems[c].source);
        REQUIRE(denominator > 0.0);
        const double expected = norm2(r) / denominator;
        INFO("component " << c << " reported " << reported[c] << " expected " << expected);
        CHECK_THAT(reported[c], Catch::Matchers::WithinRel(expected, 1.0e-12));
        // And it is a real number rather than an accidental zero.
        CHECK(expected > 0.0);
    }
}

TEST_CASE("CfdCoupling_TheClosureBoundIsTheSystemsOwnResidualAndNotTheSolvers", "[cfd][pvc]") {
    // THE BOUND'S DERIVATION CHANGED AFTER THE REVIEW, and so did this test.
    // The first form multiplied the SOLVER's residual by a slack, and the
    // review showed the two normalisations do not convert: the solver reports
    // an L2 norm over a matrix scale, `maxCellContinuity` an infinity norm over
    // the throughput. The bound is now the system's OWN residual in the
    // continuity measure, so a solver that merely REPORTS a different number
    // must not move it at all.
    const Hand hand = Hand::make(chainPoints(6), chainTets(6));
    const PreparedCfdBoundary boundary = hand.mixed(inlet(0.05), outlet(0.0));
    const std::vector<double> velocity = zeros(3 * hand.topology.cells());
    const std::vector<double> pressure = zeros(hand.topology.cells());

    const CfdCouplingIteration honest =
        advance(hand, boundary, velocity, pressure, denseSolve(), CfdIterationSettings{});
    const CfdCouplingIteration lying =
        advance(hand, boundary, velocity, pressure, reportingResidual(1.0e-10),
                CfdIterationSettings{});

    CHECK(lying.summary.pressureSolveResidual != honest.summary.pressureSolveResidual);
    // The system residual does NOT follow the report: it is measured here from
    // the matrix, the source and the solution.
    CHECK(lying.summary.systemResidual == honest.summary.systemResidual);
    // AND IT IS THE QUANTITY THE IDENTITY NAMES, so the measured continuity
    // matches it to within the identity's own arithmetic allowance. This is the
    // claim an earlier `continuityClosed` flag only appeared to make.
    INFO("systemResidual " << honest.summary.systemResidual << " continuity "
                           << honest.residuals.continuity);
    CHECK(std::abs(honest.residuals.continuity - honest.summary.systemResidual) <=
          cfd::kCfdCouplingClosureFloor);
}

TEST_CASE("CfdCoupling_ContinuityIsReportedNotClosedWhenTheSolveIsWrong", "[cfd][pvc]") {
    // `continuityClosed == false` WAS NEVER OBSERVED, so `= true` would have
    // survived. A port that returns a deliberately wrong `p'` -- while
    // reporting an acceptable residual -- makes the corrected flux
    // non-conservative, and the flag must say so.
    const Hand hand = Hand::make(chainPoints(6), chainTets(6));
    const PreparedCfdBoundary boundary = hand.mixed(inlet(0.05), outlet(0.0));
    const std::size_t cells = hand.topology.cells();
    const CfdLinearSolve wrong = [cells](const CfdSparseMatrix& matrix,
                                         std::span<const double> rhs,
                                         const CfdLinearSolverSettings& settings)
        -> std::expected<CfdLinearSolveResult, Error> {
        if (matrix.rows() != cells) {
            // The momentum systems are solved honestly; only the pressure
            // system is sabotaged. Both are `cells` square, so distinguish by
            // symmetry: the pressure matrix is symmetric, momentum is not.
            return denseSolve()(matrix, rhs, settings);
        }
        if (matrix.largestSymmetryError() != 0.0) {
            return denseSolve()(matrix, rhs, settings);
        }
        std::vector<double> x(cells, 0.0);
        for (std::size_t c = 0; c < cells; ++c) {
            x[c] = kDiscriminatingConstant * (1.0 + static_cast<double>(c));
        }
        return CfdLinearSolveResult{std::move(x), 0.0};
    };

    const CfdCouplingIteration iteration =
        advance(hand, boundary, zeros(3 * cells), zeros(cells), wrong, CfdIterationSettings{});
    INFO("continuity " << iteration.residuals.continuity << " systemResidual "
                       << iteration.summary.systemResidual);
    // The system residual is large, so the bound is large too -- and the
    // identity means the measured continuity tracks it. What must NOT happen is
    // a claim of closure against a bound derived from a lie.
    CHECK(iteration.summary.systemResidual > 1.0e-6);
    CHECK(iteration.residuals.continuity > 1.0e-6);
}

TEST_CASE("CfdCoupling_AdvanceReportsItsOwnGateBeforeReachingAnyOperator", "[cfd][pvc]") {
    // THE ORDER AND THE BLAME. A geometry built for another mesh used to be
    // reported as `LinearSolveRefused` or `SettingsInvalid`, because the
    // settings and the predictor ran first and phase 1 was unreachable until
    // after three solves.
    const Hand hand = Hand::make(chainPoints(5), chainTets(5));
    const Hand other = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.mixed(inlet(0.1), outlet(0.0));
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> velocity = zeros(3 * cells);
    const std::vector<double> flux = producedFlux(hand, boundary, velocity);
    const std::vector<CfdMomentumSystem> systems =
        momentumSystems(hand, boundary, flux, velocity);

    CfdIterationSettings bad;
    bad.velocityRelaxation = 0.0;  // also invalid
    const CfdCouplingIterationInput input{.momentum = systems,
                                          .velocity = velocity,
                                          .pressure = zeros(cells),
                                          .density = kDensity,
                                          .iteration = bad,
                                          .linearSolver = CfdLinearSolverSettings{}};
    // A mismatched geometry AND invalid settings AND no solve: geometry wins.
    std::expected<CfdCouplingIteration, CfdCouplingFailure> iteration = cfd::advanceCfdCoupling(
        hand.topology, other.geometry, boundary, input, CfdLinearSolve{});
    REQUIRE_FALSE(iteration.has_value());
    CHECK(iteration.error().problem == CfdCouplingProblem::GeometryDescribesAnotherTopology);
}

TEST_CASE("CfdCoupling_ABadDensityIsTheCallersFaultAndNotThisModules", "[cfd][pvc]") {
    // It used to reach `cfdMassFlux` and come back as
    // `EvaluationIsInconsistent` with `ErrorCode::Internal` -- this module
    // blaming itself for the caller's input, which is the category error
    // P19-MOM-001's review found in its own first draft.
    const Hand hand = Hand::make(chainPoints(5), chainTets(5));
    const PreparedCfdBoundary boundary = hand.mixed(inlet(0.1), outlet(0.0));
    const std::size_t cells = hand.topology.cells();
    const std::vector<double> velocity = zeros(3 * cells);
    const std::vector<double> flux = producedFlux(hand, boundary, velocity);
    const std::vector<CfdMomentumSystem> systems =
        momentumSystems(hand, boundary, flux, velocity);

    for (const double rho : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN()}) {
        const CfdCouplingIterationInput input{.momentum = systems,
                                              .velocity = velocity,
                                              .pressure = zeros(cells),
                                              .density = Density::fromSi(rho),
                                              .iteration = CfdIterationSettings{},
                                              .linearSolver = CfdLinearSolverSettings{}};
        std::expected<CfdCouplingIteration, CfdCouplingFailure> iteration =
            cfd::advanceCfdCoupling(hand.topology, hand.geometry, boundary, input, denseSolve());
        INFO("density " << rho);
        REQUIRE_FALSE(iteration.has_value());
        CHECK(iteration.error().problem == CfdCouplingProblem::ValueNotFinite);
        CHECK(iteration.error().error.code == ErrorCode::InvalidArgument);
    }
}

TEST_CASE("CfdCoupling_AZeroFirstResidualIsNotABaselineToDivergeFrom", "[cfd][pvc]") {
    // `now > factor * 0` is true for ANY positive residual, so a run that
    // started exactly satisfied -- the closed-domain-at-rest benchmark on its
    // first iteration -- was reported DIVERGED on its second.
    CfdConvergenceSettings settings;
    const CfdResiduals first{0.0, 0.0, 0.0, 0.0};
    const CfdResiduals now{1.0e-12, 1.0e-12, 1.0e-12, 1.0e-12};
    CHECK(cfd::cfdCouplingVerdict(now, first, settings) != CfdCouplingVerdict::Diverged);
    // It is still CONVERGED, because those residuals are under the tolerances.
    CHECK(cfd::cfdCouplingVerdict(now, first, settings) == CfdCouplingVerdict::Converged);
    // And a genuinely large residual against a zero baseline is not called
    // divergence either -- there is nothing to compare it with. It simply has
    // not converged.
    const CfdResiduals big{1.0, 1.0, 1.0, 1.0};
    CHECK(cfd::cfdCouplingVerdict(big, first, settings) == CfdCouplingVerdict::Continue);
}

TEST_CASE("CfdCoupling_UnusableConvergenceSettingsDecideNothing", "[cfd][pvc]") {
    // `cfdCouplingVerdict` is noexcept and returns a verdict, so it cannot
    // refuse. Its precondition is validated settings; when that does not hold
    // it reports `Continue` by DECISION rather than by accident. A
    // `divergenceFactor` of 0.5 -- which `validate` rejects -- used to make the
    // FIRST iteration diverge.
    const CfdResiduals now{1.0e-12, 1.0e-12, 1.0e-12, 1.0e-12};

    CfdConvergenceSettings halfFactor;
    halfFactor.divergenceFactor = 0.5;
    REQUIRE_FALSE(cfd::validate(halfFactor).has_value());
    CHECK(cfd::cfdCouplingVerdict(now, now, halfFactor) == CfdCouplingVerdict::Continue);

    CfdConvergenceSettings nanFactor;
    nanFactor.divergenceFactor = std::numeric_limits<double>::quiet_NaN();
    CHECK(cfd::cfdCouplingVerdict(now, now, nanFactor) == CfdCouplingVerdict::Continue);

    CfdConvergenceSettings nanTolerance;
    nanTolerance.momentumResidualTolerance = std::numeric_limits<double>::quiet_NaN();
    CHECK(cfd::cfdCouplingVerdict(now, now, nanTolerance) == CfdCouplingVerdict::Continue);

    // And valid settings still decide.
    CHECK(cfd::cfdCouplingVerdict(now, now, CfdConvergenceSettings{}) ==
          CfdCouplingVerdict::Converged);
}

TEST_CASE("CfdCoupling_TheConvergenceComparisonsAreNotStrict", "[cfd][pvc]") {
    // No residual in the suite ever sat exactly AT a tolerance, so `<=` to `<`
    // survived. Here they do.
    CfdConvergenceSettings settings;
    const CfdResiduals first{1.0, 1.0, 1.0, 1.0};
    const CfdResiduals at{settings.momentumResidualTolerance, settings.momentumResidualTolerance,
                          settings.momentumResidualTolerance,
                          settings.continuityResidualTolerance};
    CHECK(cfd::cfdCouplingVerdict(at, first, settings) == CfdCouplingVerdict::Converged);

    // And exactly AT the divergence factor is not divergence.
    CfdConvergenceSettings tight;
    tight.divergenceFactor = 2.0;
    tight.momentumResidualTolerance = 1.0e-30;
    tight.continuityResidualTolerance = 1.0e-30;
    const CfdResiduals base{1.0, 1.0, 1.0, 1.0};
    const CfdResiduals doubled{2.0, 2.0, 2.0, 2.0};
    CHECK(cfd::cfdCouplingVerdict(doubled, base, tight) == CfdCouplingVerdict::Continue);
    const CfdResiduals past{2.0, 2.0, 2.0, 2.0000000001};
    CHECK(cfd::cfdCouplingVerdict(past, base, tight) == CfdCouplingVerdict::Diverged);
}

TEST_CASE("CfdCoupling_AClosedDomainWithInteriorFlowIsScaledByTheInteriorFlux", "[cfd][pvc]") {
    // AN ABSOLUTE kg/s NUMBER USED TO GO INTO A DIMENSIONLESS FIELD HERE, and
    // the review's scenario was concrete: at millimetre scale with water a
    // wholly non-conservative field reports ~1e-9 kg/s and `cfdCouplingVerdict`
    // calls it converged, so in a closed domain the continuity criterion was
    // effectively disabled. The interior flux is the honest scale.
    const Hand hand = Hand::make(chainPoints(7), chainTets(7));
    const PreparedCfdBoundary boundary = hand.uniform(wall());
    const std::size_t cells = hand.topology.cells();
    std::vector<double> velocity = zeros(3 * cells);
    for (std::size_t k = 0; k < velocity.size(); ++k) {
        velocity[k] = 4.0e-7 * (1.0 + static_cast<double>(k % 3));
    }
    const CfdCouplingIteration iteration =
        advance(hand, boundary, velocity, zeros(cells), denseSolve(), CfdIterationSettings{});

    CHECK(iteration.summary.closedDomain);
    CHECK_FALSE(iteration.summary.massBalanceDefined);
    // THE SCALE IS THE PREDICTED INTERIOR FLUX, NOT THE CORRECTED ONE, and the
    // first form of this test asserted the corrected one -- which the
    // correction drives toward zero, so the ratio became noise over noise and
    // reported 0.375. The predicted flux is the magnitude actually being
    // balanced, and it is what the boundary throughput is formed from too.
    CHECK(iteration.summary.continuityScale > 0.0);
    double correctedInterior = 0.0;
    for (std::size_t f = 0; f < hand.topology.internalFaces(); ++f) {
        correctedInterior += std::abs(iteration.corrected.faceFlux[f]);
    }
    INFO("scale " << iteration.summary.continuityScale << " vs corrected interior "
                  << 0.5 * correctedInterior);
    CHECK(iteration.summary.continuityScale > 0.5 * correctedInterior);
    // ADR-058's two measures stay UNDEFINED: they are not filled from this.
    CHECK(iteration.balance == cfd::CfdMassBalance{});
    // And the residual is dimensionless and small.
    CHECK(iteration.residuals.continuity < 1.0e-9);
}

TEST_CASE("CfdCoupling_AnOpenDomainAtRestIsNotAnInternalFailure", "[cfd][pvc]") {
    // `massBalance()` is absent "for a closed domain AND for any mesh whose
    // boundary phi is all zero". A duct with two PRESSURE boundaries started
    // from rest is the second case: the flux is bit-zero everywhere, the
    // throughput is zero, and the second draft refused a perfectly well-posed
    // first iteration as an internal inconsistency.
    const Hand hand = Hand::make(chainPoints(6), chainTets(6));
    const PreparedCfdBoundary boundary = hand.mixed(pressureInlet(0.0), outlet(0.0));
    const std::size_t cells = hand.topology.cells();
    const CfdCouplingIteration iteration =
        advance(hand, boundary, zeros(3 * cells), zeros(cells), denseSolve(),
                CfdIterationSettings{});

    CHECK_FALSE(iteration.summary.closedDomain);
    CHECK_FALSE(iteration.summary.massBalanceDefined);
    CHECK(iteration.residuals.continuity == 0.0);
    for (std::size_t f = 0; f < hand.topology.faces(); ++f) {
        INFO("face " << f);
        CHECK(iteration.corrected.faceFlux[f] == 0.0);
    }
}

TEST_CASE("CfdCoupling_TheReferenceCellIsTheBoundarysAndNotCellZero", "[cfd][pvc]") {
    // Every fixture put the datum at cell 0's centroid, so "pin the named
    // reference cell" was never distinguished from "pin cell 0".
    const Hand hand = Hand::make(chainPoints(8), chainTets(8));
    const std::size_t cells = hand.topology.cells();
    REQUIRE(cells > 2);

    // Put the datum inside a cell that is NOT cell 0.
    const std::size_t target = cells - 1;
    const std::optional<Point3D> centroid =
        hand.geometry.cellCentroid(CellIndex::fromValue(target));
    REQUIRE(centroid.has_value());
    std::vector<ElementId> ascending = hand.facets();
    std::ranges::sort(ascending, [](ElementId a, ElementId b) { return a.value() < b.value(); });
    const CfdResolvedTarget bound{wall(), ascending};
    const std::array<CfdResolvedTarget, 1> targets{bound};
    Result<PreparedCfdBoundary> prepared = cfd::bindCfdBoundary(
        hand.topology, hand.geometry, hand.mesh, targets, 0,
        cfd::CfdReferencePressure{.value = 0_Pa, .location = *centroid}, std::nullopt,
        std::nullopt);
    INFO((prepared.has_value() ? std::string{} : prepared.error().message));
    REQUIRE(prepared.has_value());
    const std::optional<CellIndex> reference = prepared->referenceCell();
    REQUIRE(reference.has_value());
    INFO("reference cell " << reference->value() << " of " << cells);

    const CfdPressureCorrectionSystem system =
        assemble(hand, *prepared, zeros(hand.topology.faces()), varyingDiagonal(hand));
    REQUIRE(system.summary.pinnedCell.has_value());
    CHECK(system.summary.pinnedCell->value() == reference->value());
    // The row and column cleared are the REFERENCE cell's, whichever it is.
    for (std::size_t k = 0; k < cells; ++k) {
        if (k == reference->value()) {
            continue;
        }
        INFO("index " << k);
        CHECK(system.matrix.coeffSi(reference->value(), k) == 0.0);
        CHECK(system.matrix.coeffSi(k, reference->value()) == 0.0);
    }
}

TEST_CASE("CfdCoupling_ASingleCellPinFallsBackToAUnitDiagonalAndSaysSo", "[cfd][pvc]") {
    // The rule "the diagonal keeps its own value" has exactly one exception,
    // and the comment used to state it unconditionally.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const PreparedCfdBoundary boundary = hand.uniform(wall());
    const CfdPressureCorrectionSystem system =
        assemble(hand, boundary, zeros(hand.topology.faces()), varyingDiagonal(hand));
    REQUIRE(system.summary.pinnedCell.has_value());
    CHECK(system.summary.pinnedDiagonalIsUnitValued);
    CHECK(system.matrix.coeffSi(0, 0) == 1.0);
}

TEST_CASE("CfdCoupling_TheRangeGuardAcceptsEveryConstructibleTopology", "[cfd][pvc]") {
    // `TopologyNamesAnUnknownCell` IS UNREACHABLE THROUGH `buildCfdTopology`,
    // which validates every owner and neighbour, and it is recorded as
    // unreachable rather than claimed as tested -- the same way every sibling
    // operator records its own copy of this guard.
    //
    // THE GUARD STILL HAS TO EXIST. `CellIndex::kInvalid` is `SIZE_MAX` and
    // this operator WRITES through `owner` and `neighbour` -- into the symbolic
    // pattern, the right-hand side and the per-cell face sums. `CfdConvection.hpp`
    // records that P19-DIV-001's review and then P19-CONV-001's review each
    // found that index reaching writes; the first draft of this operator
    // dropped the guard, which was the third regression, and its own
    // correctness review found it.
    CHECK(cfd::toString(CfdCouplingProblem::TopologyNamesAnUnknownCell) ==
          "TopologyNamesAnUnknownCell");

    // AND A WELL-FORMED TOPOLOGY PASSES THE PHASE: every owner and neighbour of
    // every face of a real mesh is in range.
    const Hand hand = Hand::make(chainPoints(8), chainTets(8));
    const std::size_t cells = hand.topology.cells();
    for (std::size_t f = 0; f < hand.topology.faces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        INFO("face " << f);
        CHECK(hand.topology.owner(face).isValid());
        CHECK(hand.topology.owner(face).value() < cells);
        if (const std::optional<CellIndex> neighbour = hand.topology.neighbour(face)) {
            CHECK(neighbour->isValid());
            CHECK(neighbour->value() < cells);
        }
    }
}

TEST_CASE("CfdCoupling_TheAllocationFreePredicateAgreesWithTheFailureOne", "[cfd][pvc]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.uniform(wall());
    std::vector<double> flux = zeros(hand.topology.faces());
    flux.pop_back();
    const CfdPressureCorrectionInput bad{.predictedFaceFlux = flux,
                                         .momentumDiagonal = varyingDiagonal(hand),
                                         .density = kDensity};
    const std::optional<CfdCouplingProblem> problem =
        cfd::cfdPressureCorrectionProblem(hand.topology, hand.geometry, boundary, bad);
    REQUIRE(problem.has_value());
    CHECK(*problem == CfdCouplingProblem::ValueLengthWrong);

    const CfdPressureCorrectionInput good{.predictedFaceFlux = zeros(hand.topology.faces()),
                                          .momentumDiagonal = varyingDiagonal(hand),
                                          .density = kDensity};
    CHECK_FALSE(
        cfd::cfdPressureCorrectionProblem(hand.topology, hand.geometry, boundary, good).has_value());
    static_assert(noexcept(cfd::cfdPressureCorrectionProblem(hand.topology, hand.geometry, boundary,
                                                             good)),
                  "the allocation-free predicate must be noexcept, which is the whole reason it "
                  "exists");
}

TEST_CASE("CfdCoupling_TheBenchmarkConvergesAndNotMerelyRuns", "[cfd][pvc]") {
    // THE LOOP NEVER ASSERTED CONVERGENCE, so a predictor that returned zero,
    // or a relaxation that solved the wrong equation, would have survived: mass
    // is conserved and continuity closes for ANY predictor, because the
    // correction is a projection.
    // THE SPEED IS CHOSEN FROM THE CELL PECLET NUMBER, NOT PICKED, and the
    // first form of this test picked one. `Pe = rho u dx / mu` with water and a
    // 10 mm cell is 200 at 20 mm/s: deeply convection-dominated, and upwind
    // convection then drives the momentum diagonal NEGATIVE, which this
    // operator refuses (see `L9` and
    // `ANonPositiveAssembledDiagonalIsRefusedAndIsReachable`). That refusal is
    // the honest answer, and it means a converging benchmark has to sit in the
    // regime where SIMPLE has a coefficient at all.
    //
    //     Pe = 998.2 * 2e-5 * 0.01 / 1.002e-3 = 0.199
    const double inletSpeed = 2.0e-5;
    const double cellSize = 0.01;
    const double peclet = kDensity.si() * inletSpeed * cellSize / kViscosity.si();
    INFO("cell Peclet number " << peclet);
    REQUIRE(peclet < 1.0);

    const Hand hand = Hand::make(chainPoints(10), chainTets(10));
    const PreparedCfdBoundary boundary = hand.mixed(inlet(inletSpeed), outlet(0.0));
    const std::size_t cells = hand.topology.cells();

    std::vector<double> velocity = zeros(3 * cells);
    std::vector<double> pressure = zeros(cells);
    CfdIterationSettings settings;
    settings.velocityRelaxation = 0.7;
    settings.pressureRelaxation = 0.3;

    std::optional<CfdResiduals> first;
    std::optional<std::size_t> convergedAt;
    double firstMomentum = 0.0;
    double lastMomentum = 0.0;
    for (std::size_t k = 0; k < 200; ++k) {
        const CfdCouplingIteration iteration =
            advance(hand, boundary, velocity, pressure, denseSolve(), settings, true);
        if (!first.has_value()) {
            first = iteration.residuals;
            firstMomentum = std::max({iteration.residuals.momentumX, iteration.residuals.momentumY,
                                      iteration.residuals.momentumZ});
        }
        lastMomentum = std::max({iteration.residuals.momentumX, iteration.residuals.momentumY,
                                 iteration.residuals.momentumZ});
        velocity = iteration.corrected.velocity;
        pressure = iteration.corrected.pressure;

        const CfdCouplingVerdict verdict =
            cfd::cfdCouplingVerdict(iteration.residuals, *first, CfdConvergenceSettings{});
        REQUIRE(verdict != CfdCouplingVerdict::Diverged);
        if (verdict == CfdCouplingVerdict::Converged) {
            convergedAt = k + 1;
            break;
        }
    }
    INFO("first momentum residual " << firstMomentum << ", last " << lastMomentum
                                    << ", converged at "
                                    << (convergedAt.has_value() ? *convergedAt : 0));
    // THE REAL CLAIM: the outer iteration drives the momentum residual DOWN.
    CHECK(firstMomentum > 0.0);
    CHECK(lastMomentum < firstMomentum);
    REQUIRE(convergedAt.has_value());
    CHECK(*convergedAt >= 1);
}

TEST_CASE("CfdCoupling_ANonPositiveAssembledDiagonalIsRefusedAndIsReachable", "[cfd][pvc]") {
    // CONFIRMING WHAT A REVIEW ONLY SUSPECTED. It argued that upwind convection
    // can put a negative `phi` on the diagonal at a zero-gradient-velocity
    // boundary, where diffusion adds nothing, so a correct assembly can hand
    // this operator a non-positive diagonal -- and said it had not built the
    // mesh. These velocities build it: a chain mesh at millimetres per second
    // with water is convection-dominated, and the assembled x-momentum diagonal
    // goes negative.
    //
    // SO THE REFUSAL IS REACHABLE FROM A CORRECT INPUT, which is why it is
    // described as a hard problem rather than a defective assembly, and why it
    // is recorded as a known limitation rather than presented as a guard
    // against caller error.
    const Hand hand = Hand::make(chainPoints(6), chainTets(6));
    const PreparedCfdBoundary boundary = hand.mixed(inlet(0.2), outlet(0.0));
    const std::size_t cells = hand.topology.cells();
    std::vector<double> velocity = zeros(3 * cells);
    for (std::size_t k = 0; k < velocity.size(); ++k) {
        velocity[k] = 5.0e-3 * (1.0 + static_cast<double>(k % 4));
    }
    const std::vector<double> flux = producedFlux(hand, boundary, velocity);
    const std::vector<CfdMomentumSystem> systems =
        momentumSystems(hand, boundary, flux, velocity);

    // THE PREMISE, MEASURED: a correct assembly really did produce one.
    std::size_t nonPositive = 0;
    double worst = 0.0;
    for (std::size_t c = 0; c < 3; ++c) {
        for (std::size_t cell = 0; cell < cells; ++cell) {
            const double a = systems[c].matrix.coeffSi(cell, cell);
            if (!(a > 0.0)) {
                ++nonPositive;
                worst = std::min(worst, a);
            }
        }
    }
    INFO("non-positive assembled diagonals: " << nonPositive << ", worst " << worst);
    REQUIRE(nonPositive > 0);

    const CfdCouplingIterationInput input{.momentum = systems,
                                          .velocity = velocity,
                                          .pressure = zeros(cells),
                                          .density = kDensity,
                                          .iteration = CfdIterationSettings{},
                                          .linearSolver = CfdLinearSolverSettings{}};
    std::expected<CfdCouplingIteration, CfdCouplingFailure> iteration =
        cfd::advanceCfdCoupling(hand.topology, hand.geometry, boundary, input, denseSolve());
    REQUIRE_FALSE(iteration.has_value());
    CHECK(iteration.error().problem == CfdCouplingProblem::MomentumDiagonalNotPositive);
    CHECK_THAT(iteration.error().error.message,
               Catch::Matchers::ContainsSubstring("strictly positive"));
}

TEST_CASE("CfdCoupling_TheRealSolverDrivesTheBenchmarkAndAgreesWithTheDenseReference",
          "[cfd][pvc][linsolve]") {
    // P19-LINSOLVE-001 ADDED THIS CASE, and it is the only place where the
    // production solver meets the operator it was built for. Every other case
    // in this file supplies `denseSolve`, a Gaussian elimination written here
    // so that the operator could be qualified before a solver existed. That
    // reference is now worth more than it was: it is INDEPENDENT of
    // `BetterCAD::linalg`, of Eigen, and of both factorisations, so running the
    // same SIMPLE loop twice -- once through `cfdLinearSolve()` and once through
    // the dense reference -- checks the real solver against an implementation
    // that shares no code with it.
    //
    // The fixture is the benchmark's, for the reason the benchmark gives: the
    // inlet speed is derived from the cell Peclet number rather than picked,
    // because upwind convection drives the momentum diagonal negative above
    // Pe ~ 1 and this operator refuses that.
    const double inletSpeed = 2.0e-5;
    const double peclet = kDensity.si() * inletSpeed * 0.01 / kViscosity.si();
    REQUIRE(peclet < 1.0);

    const Hand hand = Hand::make(chainPoints(10), chainTets(10));
    const PreparedCfdBoundary boundary = hand.mixed(inlet(inletSpeed), outlet(0.0));
    const std::size_t cells = hand.topology.cells();
    CfdIterationSettings settings;
    settings.velocityRelaxation = 0.7;
    settings.pressureRelaxation = 0.3;

    const auto run = [&](const CfdLinearSolve& solve, std::size_t& iterations) {
        std::vector<double> velocity = zeros(3 * cells);
        std::vector<double> pressure = zeros(cells);
        std::optional<CfdResiduals> first;
        iterations = 0;
        for (std::size_t k = 0; k < 200; ++k) {
            const CfdCouplingIteration iteration =
                advance(hand, boundary, velocity, pressure, solve, settings, true);
            if (!first.has_value()) {
                first = iteration.residuals;
            }
            velocity = iteration.corrected.velocity;
            pressure = iteration.corrected.pressure;
            const CfdCouplingVerdict verdict =
                cfd::cfdCouplingVerdict(iteration.residuals, *first, CfdConvergenceSettings{});
            REQUIRE(verdict != CfdCouplingVerdict::Diverged);
            iterations = k + 1;
            if (verdict == CfdCouplingVerdict::Converged) {
                break;
            }
        }
        return std::pair{velocity, pressure};
    };

    std::size_t realIterations = 0;
    std::size_t denseIterations = 0;
    const auto [realVelocity, realPressure] = run(cfd::cfdLinearSolve(), realIterations);
    const auto [denseVelocity, densePressure] = run(denseSolve(), denseIterations);

    SECTION("the real solver drives the loop to convergence") {
        // A solver that returned zero, or refused, could not get here: the
        // operator refuses a non-finite result and the verdict would never
        // reach Converged.
        CHECK(realIterations > 1);
        CHECK(realIterations < 200);
    }

    SECTION("and it agrees with the independent dense reference") {
        // TWO DIFFERENT FACTORISATIONS OF THE SAME SYSTEMS, so the agreement
        // is numerical and NOT bitwise, and nothing here claims otherwise.
        double velocityScale = 0.0;
        double worstVelocity = 0.0;
        for (std::size_t i = 0; i < realVelocity.size(); ++i) {
            velocityScale = std::max(velocityScale, std::abs(denseVelocity[i]));
            worstVelocity = std::max(worstVelocity, std::abs(realVelocity[i] - denseVelocity[i]));
        }
        double pressureScale = 0.0;
        double worstPressure = 0.0;
        for (std::size_t i = 0; i < realPressure.size(); ++i) {
            pressureScale = std::max(pressureScale, std::abs(densePressure[i]));
            worstPressure = std::max(worstPressure, std::abs(realPressure[i] - densePressure[i]));
        }
        INFO("iterations: real " << realIterations << ", dense " << denseIterations);
        REQUIRE(velocityScale > 0.0);
        // THE PRESSURE PREMISE IS REQUIRED AND NOT SKIPPED. An `if` here meant
        // that an identically zero pressure field would silently remove the
        // pressure comparison, and nothing asserted otherwise.
        REQUIRE(pressureScale > 0.0);
        CHECK(worstVelocity / velocityScale <= 1.0e-6);
        CHECK(worstPressure / pressureScale <= 1.0e-6);
        // AND THE REFERENCE MUST HAVE CONVERGED TOO, or the comparison above is
        // against an unconverged state and the only symptom would be a
        // confusing bound failure.
        CHECK(denseIterations > 1);
        CHECK(denseIterations < 200);
        WARN(std::format("real vs dense reference: velocity {:.3e} relative, pressure {:.3e} relative, "
                         "{} vs {} iterations",
                         worstVelocity / velocityScale,
                         pressureScale > 0.0 ? worstPressure / pressureScale : 0.0, realIterations,
                         denseIterations));
    }

    SECTION("ONE iteration from the same state agrees far more tightly than the loop") {
        // THE LOOP COMPARISON'S BOUND IS THE CONVERGENCE TOLERANCE ITSELF.
        // `CfdConvergenceSettings` stops at 1e-6, so both runs halt inside the
        // same 1e-6 neighbourhood of the fixed point and landing within 1e-6 of
        // each other is close to automatic -- it discriminates gross solver
        // error, not the accuracy the gates promise. Isolating a SINGLE
        // iteration from one shared state removes the loop from the comparison,
        // and the two solvers must then agree to their own accuracy.
        const std::vector<double> velocity = zeros(3 * cells);
        const std::vector<double> pressure = zeros(cells);
        const CfdCouplingIteration byReal =
            advance(hand, boundary, velocity, pressure, cfd::cfdLinearSolve(), settings, true);
        const CfdCouplingIteration byDense =
            advance(hand, boundary, velocity, pressure, denseSolve(), settings, true);

        double scale = 0.0;
        double worst = 0.0;
        for (std::size_t i = 0; i < byReal.corrected.velocity.size(); ++i) {
            scale = std::max(scale, std::abs(byDense.corrected.velocity[i]));
            worst = std::max(worst, std::abs(byReal.corrected.velocity[i] - byDense.corrected.velocity[i]));
        }
        REQUIRE(scale > 0.0);
        INFO("one-iteration relative departure " << worst / scale);
        CHECK(worst / scale <= 1.0e-10);

        double pressureScale = 0.0;
        double worstPressure = 0.0;
        for (std::size_t i = 0; i < byReal.corrected.pressure.size(); ++i) {
            pressureScale = std::max(pressureScale, std::abs(byDense.corrected.pressure[i]));
            worstPressure =
                std::max(worstPressure, std::abs(byReal.corrected.pressure[i] - byDense.corrected.pressure[i]));
        }
        REQUIRE(pressureScale > 0.0);
        CHECK(worstPressure / pressureScale <= 1.0e-10);
        WARN(std::format("one iteration, real vs dense: velocity {:.3e} relative, pressure {:.3e} relative",
                         worst / scale, worstPressure / pressureScale));
    }

    SECTION("the real solver drives the residual DOWN, not merely to a verdict") {
        // The benchmark case asserts this of the dense reference; under the real
        // solver the only convergence claim was an iteration count.
        const std::vector<double> velocity = zeros(3 * cells);
        const std::vector<double> pressure = zeros(cells);
        const CfdCouplingIteration first =
            advance(hand, boundary, velocity, pressure, cfd::cfdLinearSolve(), settings, true);
        const double firstMomentum =
            std::max({first.residuals.momentumX, first.residuals.momentumY, first.residuals.momentumZ});
        REQUIRE(firstMomentum > 0.0);
        const CfdCouplingIteration last = advance(hand, boundary, realVelocity, realPressure,
                                                  cfd::cfdLinearSolve(), settings, true);
        const double lastMomentum =
            std::max({last.residuals.momentumX, last.residuals.momentumY, last.residuals.momentumZ});
        CHECK(lastMomentum < firstMomentum);
    }

    SECTION("the closure identity holds under the real solver too") {
        // ADR-075 Decision 5's identity is a property of the OPERATOR, so it
        // must not depend on which solver supplied the correction.
        const CfdCouplingIteration iteration =
            advance(hand, boundary, realVelocity, realPressure, cfd::cfdLinearSolve(), settings, true);
        CHECK(std::abs(iteration.residuals.continuity - iteration.summary.systemResidual) <=
              cfd::kCfdCouplingClosureFloor);
        CHECK(iteration.balance.maxCellContinuity <=
              iteration.summary.systemResidual + cfd::kCfdCouplingClosureFloor);
    }
}
