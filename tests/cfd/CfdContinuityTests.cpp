/// \file
/// P19-CONT-001 — the continuity evaluation. ADR-074.
///
/// WHAT THIS SUITE CAN AND CANNOT PROVE, stated first so a reader is not misled
/// by the number of assertions.
///
/// Continuity is a MEASUREMENT, not an assembly, so most of what there is to
/// check is that the right quantity is reported under the right name. Three
/// kinds of assertion here are independent of the implementation's internals,
/// and they carry the milestone's falsifiable content:
///
/// ```text
/// (a) THE DECOMPOSITION. Every per-cell residual and the headline
///     `netBoundaryFlux` must be the QUALIFIED divergence's own values,
///     bit-for-bit.  This is checkable because `CfdContinuityReport` exposes the
///     divergence it was built from, and because a test can call
///     `divergeCfdMassFlux` on the same input itself
/// (b) THE TWO MEASURES ARE THE ONES ADR-058 FIXED, and in particular
///     `massBalance().globalImbalance` is the PHYSICAL boundary sum and NOT
///     `divergence().globalImbalance()`, which is a floating-point artifact of a
///     different magnitude.  The two are separated by sixteen orders of
///     magnitude on a field whose mass balance genuinely fails, so a swap cannot
///     pass
/// (c) THE SIGN CONVENTION. A positive boundary `phi` is outflow and a negative
///     one is inflow, whatever the facet is CALLED, and a `PressureInlet`
///     carrying outflow is reported rather than refused
/// ```
///
/// THE FIXTURES ARE A SIXTH COPY, AND THAT IS STATED RATHER THAN HIDDEN.
/// `buildMesh`, `struct Hand`, `orthogonalPoints` and their neighbours also
/// exist in `CfdDiffusionTests.cpp`, `CfdGradientTests.cpp`,
/// `CfdDivergenceTests.cpp`, `CfdConvectionTests.cpp` and `CfdMomentumTests.cpp`,
/// whose own comment records the policy: extracting them would edit five
/// qualified suites to serve a sixth, which is the refactor-with-a-feature the
/// working order forbids. What must not be duplicated is an ORACLE.
///
/// TWO FIXTURES ARE NEW HERE, because no existing suite needed them: a
/// multi-patch topology (`Hand::split`) and a mixed-condition boundary
/// (`Hand::mixed`). Items 7 and 9 -- the inlet/outlet attribution and multiple
/// inlets/outlets -- cannot be reached with the single-patch uniform fixtures
/// the other five suites share.
///
/// WHAT THIS FILE DELIBERATELY DOES NOT ASSERT.
///
/// There is no claim that `outflow - inflow == netBoundaryFlux`, and none that
/// the per-patch subtotals sum to it. ADR-074's Q2 and Q3 measured both before
/// any of this existed: bit-exact on 1.02% and 1.93% of balanced fields. The
/// suite asserts the MEASURED BOUND and asserts that the departure is reported.
///
/// There is no acceptance verdict anywhere in here. Whether a mass balance is
/// good enough is `P19-MASSBAL-001`'s and `P19-VALID-001`'s, with thresholds that
/// are policy; this suite checks that the numbers are the right numbers.
///
/// There is no `internalFacesCancelExactly` flag to test, for ADR-071's reason:
/// the scatter adds `+flux` and `-flux`, so such a flag would ask whether
/// `x + (-x) == 0.0` and could not read false. Internal-face cancellation is
/// tested through its aggregate consequence and through one direct reading of an
/// internal face's two contributions.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "reference/MeshTestSupport.hpp"

#include <MeshReferenceModels.hpp>

#include <bettercad/cfd/CfdBoundary.hpp>
#include <bettercad/cfd/CfdContinuity.hpp>
#include <bettercad/cfd/CfdConvection.hpp>
#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdDivergence.hpp>
#include <bettercad/cfd/CfdGeometry.hpp>
#include <bettercad/cfd/CfdAnalysis.hpp>
#include <bettercad/cfd/CfdMesh.hpp>
#include <bettercad/cfd/CfdTopology.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/materials/ThermalProperties.hpp>
#include <bettercad/core/math/Direction.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
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

using cfd::BoundaryFaceIndex;
using cfd::CellIndex;
using cfd::CfdBoundaryCondition;
using cfd::CfdBoundaryConditionKind;
using cfd::CfdBoundaryPolicy;
using cfd::CfdContinuityFailure;
using cfd::CfdContinuityInput;
using cfd::CfdContinuityProblem;
using cfd::CfdContinuityReport;
using cfd::CfdGeometry;
using cfd::CfdMassBalance;
using cfd::CfdMassFluxDivergenceField;
using cfd::CfdMassFluxDivergenceInput;
using cfd::CfdMassFluxInput;
using cfd::CfdPatchMassFlow;
using cfd::CfdResolvedTarget;
using cfd::CfdTopology;
using cfd::FaceIndex;
using cfd::MassFlowRate;
using cfd::MovingWall;
using cfd::NoSlipWall;
using cfd::PatchIndex;
using cfd::PressureInlet;
using cfd::PressureOutlet;
using cfd::PreparedCfdBoundary;
using cfd::SlipWall;
using cfd::Velocity3;
using cfd::VelocityInlet;
using meshing::ElementId;
using meshing::NodeId;

namespace {

constexpr auto kRegion = meshing::RegionId::fromValue(1);

/// A CONSTANT NO POWER OF TWO DIVIDES, so a mutation that drops or doubles a
/// factor cannot be hidden by exact binary arithmetic.
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

[[nodiscard]] std::vector<std::array<std::size_t, 4>> chainTets(std::size_t cells) {
    std::vector<std::array<std::size_t, 4>> out;
    for (std::size_t k = 0; k < cells; ++k) {
        out.push_back({k, k + 1, k + 2, k + 3});
    }
    return out;
}

/// Every triangle on one patch, as five other cfd suites build it.
[[nodiscard]] std::vector<meshing::MappedFace> onePatch(const meshing::Mesh& mesh) {
    meshing::MappedFace face;
    face.index = 0;
    for (const meshing::Triangle& triangle : mesh.triangles()) {
        face.facets.push_back(triangle.id);
    }
    return {face};
}

/// The triangles dealt round-robin into @p count patches.
///
/// NEW IN THIS SUITE, because item 9 ("Multiple inlets / outlets") cannot be
/// reached with one patch.
///
/// THE ROUND-ROBIN DEALS *TRIANGLES* TO PATCHES AND DOES NOT MAKE THE PATCHES
/// INTERLEAVED IN `BoundaryFaceIndex` ORDER. An earlier version of this comment
/// claimed it did. A review read `buildCfdTopology` instead of the comment: the
/// boundary range is sorted by `(patch, ownerSlot, key)`, and the patch CSR
/// depends on it -- "the boundary range is ordered by patch, so each patch's
/// faces are contiguous and the starts are a prefix count". Whatever order the
/// triangles are dealt in, each patch ends up CONTIGUOUS.
///
/// The old comment was wrong twice over, because contiguous blocks would not
/// agree bit-for-bit with the total either: `((a+b)+(c+d))` is not
/// `(((a+b)+c)+d)`. What this helper is for is simply producing more than one
/// patch.
[[nodiscard]] std::vector<meshing::MappedFace> splitPatches(const meshing::Mesh& mesh,
                                                            std::size_t count) {
    REQUIRE(count > 0);
    std::vector<meshing::MappedFace> out(count);
    for (std::size_t p = 0; p < count; ++p) {
        out[p].index = p;
    }
    std::size_t k = 0;
    for (const meshing::Triangle& triangle : mesh.triangles()) {
        out[k % count].facets.push_back(triangle.id);
        ++k;
    }
    // A PATCH WITH NO FACES IS LEGITIMATE AND IS NOT FILTERED OUT, because the
    // header promises a record for every patch the topology holds and the
    // all-zero record is the thing worth testing.
    return out;
}

[[nodiscard]] FaceName someFace(std::uint64_t feature = 1) {
    return FaceName{ObjectId::fromValue(feature), FaceSelector{.role = FaceRole::EndCap}};
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

    /// The same mesh with its facets dealt into @p patches patches.
    [[nodiscard]] static Hand split(const std::vector<Point3D>& points,
                                    const std::vector<std::array<std::size_t, 4>>& tets,
                                    std::size_t patches) {
        meshing::Mesh mesh = buildMesh(points, tets);
        const std::vector<meshing::MappedFace> mapped = splitPatches(mesh, patches);
        Result<CfdTopology> topology = cfd::buildCfdTopology(mesh, mapped);
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

    /// Two conditions, the boundary facets dealt between them by the same
    /// round-robin `splitPatches` uses, so condition `k % 2` lands on patch
    /// `k % patches` consistently when both counts are 2.
    ///
    /// NEW IN THIS SUITE, for item 7. Every existing cfd fixture binds ONE
    /// condition to every facet, which cannot produce a mesh with an inlet and
    /// an outlet on it -- and an inlet/outlet attribution that is only ever
    /// tested on a uniform boundary is not tested at all.
    [[nodiscard]] PreparedCfdBoundary mixed(const CfdBoundaryCondition& first,
                                            const CfdBoundaryCondition& second) const {
        std::vector<ElementId> ascending = facets();
        std::ranges::sort(ascending, [](ElementId a, ElementId b) { return a.value() < b.value(); });
        REQUIRE(std::ranges::adjacent_find(ascending) == ascending.end());
        REQUIRE(ascending.size() >= 2);
        std::vector<ElementId> a;
        std::vector<ElementId> b;
        for (std::size_t k = 0; k < ascending.size(); ++k) {
            (k % 2 == 0 ? a : b).push_back(ascending[k]);
        }
        REQUIRE(!a.empty());
        REQUIRE(!b.empty());
        const std::array<CfdResolvedTarget, 2> targets{CfdResolvedTarget{first, a},
                                                       CfdResolvedTarget{second, b}};
        const bool anyFixesPressure =
            cfd::treatmentOf(first.kind()).pressure == CfdBoundaryPolicy::FixedValue ||
            cfd::treatmentOf(second.kind()).pressure == CfdBoundaryPolicy::FixedValue;
        return bindTargets(targets, anyFixesPressure);
    }

    [[nodiscard]] PreparedCfdBoundary walls() const {
        return uniform(
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{someFace()}});
    }

    /// A moving wall on ONE facet, no-slip on the rest.
    ///
    /// TWO FIXTURE PREMISES FAILED BEFORE THIS ONE WORKED, and both are recorded
    /// because the test it serves is real and only the fixture was wrong:
    ///
    /// ```text
    /// Velocity3{}          refused: "a wall at rest is a no-slip wall".  A
    ///                      moving wall that does not move is not a distinct
    ///                      condition, so P19-BC-001 refuses it by name
    /// every facet          refused: "its target is not planar ... tangency has
    ///                      no meaning on a curved target".  A moving wall
    ///                      prescribes a TANGENTIAL velocity, which needs a
    ///                      normal, and a tetrahedron's facets are not coplanar
    /// ```
    ///
    /// ONE FACET IS TRIVIALLY PLANAR, so this reaches the `MovingWall` arm of
    /// `isWallKind` -- which is the whole point, since a wrong arm there would
    /// show only on the kind it got wrong. The domain stays CLOSED, because
    /// no-slip and moving wall are both walls.
    ///
    /// The wall velocity never reaches `phi`: ADR-067's table gives `phi` as
    /// `FixedValue 0` at a moving wall, because such a wall moves no mass
    /// through itself.
    [[nodiscard]] PreparedCfdBoundary movingWallOnOneFacet() const {
        std::vector<ElementId> ascending = facets();
        std::ranges::sort(ascending, [](ElementId a, ElementId b) { return a.value() < b.value(); });
        REQUIRE(ascending.size() >= 2);
        const std::vector<ElementId> one{ascending.front()};
        const std::vector<ElementId> rest(ascending.begin() + 1, ascending.end());
        const std::array<CfdResolvedTarget, 2> targets{
            CfdResolvedTarget{
                CfdBoundaryCondition{
                    CfdBoundaryConditionId::fromValue(1),
                    MovingWall{someFace(1), Velocity3{Velocity::fromSi(0.1), Velocity::fromSi(0.0),
                                                      Velocity::fromSi(0.0)}}},
                one},
            CfdResolvedTarget{CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(2),
                                                   NoSlipWall{someFace(2)}},
                              rest}};
        return bindTargets(targets, false);
    }

    [[nodiscard]] PreparedCfdBoundary slips() const {
        return uniform(
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), SlipWall{someFace()}});
    }

    [[nodiscard]] PreparedCfdBoundary outlets() const {
        return uniform(CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1),
                                            PressureOutlet{someFace(), 0_Pa}});
    }

    [[nodiscard]] PreparedCfdBoundary inlets(Velocity speed) const {
        return uniform(CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1),
                                            VelocityInlet{someFace(), speed}});
    }

    /// A pressure inlet on half the facets and a pressure outlet on the other
    /// half: `phi` is `Computed` on both, so a test may supply any value and
    /// in particular may make the facets named "inlet" carry outflow.
    [[nodiscard]] PreparedCfdBoundary inletAndOutlet() const {
        return mixed(CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1),
                                          PressureInlet{someFace(1), 10_Pa}},
                     CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(2),
                                          PressureOutlet{someFace(2), 0_Pa}});
    }
};

// ---------------------------------------------------------------------------
// Flux builders
// ---------------------------------------------------------------------------

/// `phi` zero everywhere: conservative, and the only field for which every
/// claim is exact.
[[nodiscard]] std::vector<double> zeroFlux(const CfdTopology& topology) {
    return std::vector<double>(topology.faces(), 0.0);
}

/// `phi` produced by the QUALIFIED producer from a uniform velocity.
///
/// THE ONLY FIELD IN THIS SUITE THAT IS CONSERVATIVE BY CONSTRUCTION, and it is
/// built by `cfdMassFlux` rather than by hand so that the convention under test
/// is the convention in production -- item 1's whole content.
[[nodiscard]] std::vector<double> producedFlux(const Hand& hand, const PreparedCfdBoundary& boundary,
                                               Velocity3 velocity, Density density) {
    const std::size_t cells = hand.topology.cells();
    std::vector<double> u(3 * cells, 0.0);
    for (std::size_t c = 0; c < cells; ++c) {
        u[0 * cells + c] = velocity.x.si();
        u[1 * cells + c] = velocity.y.si();
        u[2 * cells + c] = velocity.z.si();
    }
    const CfdMassFluxInput input{u, density};
    std::expected<std::vector<double>, cfd::CfdConvectionFailure> phi =
        cfd::cfdMassFlux(hand.topology, hand.geometry, boundary, input);
    INFO((phi.has_value() ? std::string{} : phi.error().error.message));
    REQUIRE(phi.has_value());
    return std::move(*phi);
}

/// Every BOUNDARY face set to @p value, internal faces left at zero.
[[nodiscard]] std::vector<double> boundaryFlux(const CfdTopology& topology, double value) {
    std::vector<double> phi(topology.faces(), 0.0);
    for (std::size_t b = 0; b < topology.boundaryFaces(); ++b) {
        const FaceIndex face = topology.faceOf(BoundaryFaceIndex::fromValue(b));
        phi[face.value()] = value;
    }
    return phi;
}

// ---------------------------------------------------------------------------
// Report helpers
// ---------------------------------------------------------------------------

[[nodiscard]] CfdContinuityReport evaluate(const Hand& hand, const PreparedCfdBoundary& boundary,
                                           std::span<const double> phi) {
    const CfdContinuityInput input{phi};
    std::expected<CfdContinuityReport, CfdContinuityFailure> report =
        cfd::evaluateCfdContinuity(hand.topology, hand.geometry, boundary, input);
    INFO((report.has_value() ? std::string{} : report.error().error.message));
    REQUIRE(report.has_value());
    return std::move(*report);
}

[[nodiscard]] CfdContinuityFailure refusal(const Hand& hand, const PreparedCfdBoundary& boundary,
                                           std::span<const double> phi) {
    const CfdContinuityInput input{phi};
    std::expected<CfdContinuityReport, CfdContinuityFailure> report =
        cfd::evaluateCfdContinuity(hand.topology, hand.geometry, boundary, input);
    REQUIRE_FALSE(report.has_value());
    return report.error();
}

/// The divergence of the same input, taken INDEPENDENTLY of the report.
[[nodiscard]] CfdMassFluxDivergenceField independentDivergence(const Hand& hand,
                                                               std::span<const double> phi) {
    const CfdMassFluxDivergenceInput input{phi};
    Result<CfdMassFluxDivergenceField> field =
        cfd::divergeCfdMassFlux(hand.topology, hand.geometry, input);
    INFO((field.has_value() ? std::string{} : field.error().message));
    REQUIRE(field.has_value());
    return std::move(*field);
}

/// `sum |phi|` over the BOUNDARY faces: the scale every relative claim here is
/// stated against.
[[nodiscard]] double boundaryMagnitude(const CfdTopology& topology, std::span<const double> phi) {
    double sum = 0.0;
    for (std::size_t b = 0; b < topology.boundaryFaces(); ++b) {
        const FaceIndex face = topology.faceOf(BoundaryFaceIndex::fromValue(b));
        sum += std::abs(phi[face.value()]);
    }
    return sum;
}

[[nodiscard]] std::vector<double> withLeadingNan(std::span<const double> values) {
    std::vector<double> out(values.begin(), values.end());
    for (double& entry : out) {
        entry = std::numeric_limits<double>::quiet_NaN();
        break;
    }
    return out;
}

/// A copy one entry SHORT, built from an explicit range rather than copied and
/// popped.
///
/// `pop_back()` ON A VECTOR THE OPTIMIZER CANNOT PROVE NON-EMPTY IS A
/// RELEASE-ONLY BUILD FAILURE. `-Werror=array-bounds` at `-O3`, once this helper
/// is inlined into its caller, bounds the destructor's subscript at -1 and ends
/// the build; Debug accepts it. `P19-MOM-001`'s first qualification attempt
/// failed on exactly that line after sixty-six minutes, and the note that had
/// argued such sites safe -- "no access follows them" -- was wrong, because
/// `pop_back()` IS the access. Built one short instead.
[[nodiscard]] std::vector<double> shortened(std::span<const double> values) {
    const std::size_t kept = values.empty() ? std::size_t{0} : values.size() - 1;
    std::vector<double> out(values.begin(),
                            values.begin() + static_cast<std::ptrdiff_t>(kept));
    return out;
}

constexpr Density kDensity = 998_kg_per_m3;
constexpr DynamicViscosity kViscosity = DynamicViscosity::fromSi(1.002e-3);

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

/// One meshed reference body, prepared exactly as `CfdConvectionTests.cpp` and
/// `CfdMomentumTests.cpp` prepare it, because these are the same bodies and a
/// different construction would make the comparison with those suites' figures
/// meaningless.
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

    /// Every facet a no-slip wall: a CLOSED body, where `phi` is exactly zero at
    /// every boundary face.
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

    /// A pressure OUTLET everywhere instead: an OPEN body, so the boundary
    /// throughput is non-zero and the relative measures exist.
    ///
    /// NEEDED BECAUSE THE WALL BODY CANNOT REACH THEM. On a closed body the
    /// throughput is zero and `massBalance()` is deliberately absent, so a
    /// reference test that only ever used `wallEverywhere()` would never
    /// evaluate the two measures on a real mesh at all.
    [[nodiscard]] PreparedCfdBoundary outletEverywhere() const {
        const CfdResolvedTarget target{
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1),
                                 PressureOutlet{someFace(), 0_Pa}},
            facets()};
        const std::array<CfdResolvedTarget, 1> targets{target};
        Result<PreparedCfdBoundary> bound =
            cfd::bindCfdBoundary(*topology, *geometry, input->mesh().mesh(), targets, 0,
                                 std::nullopt, std::nullopt, std::nullopt);
        INFO((bound.has_value() ? std::string{} : bound.error().message));
        REQUIRE(bound.has_value());
        return std::move(*bound);
    }
};

struct Meshable {
    bettercad::reference::MeshReferenceModelKind kind{};
    std::string_view name{};
};

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
// Item 1 — the mass-flux convention is BOUND, not redefined
// ---------------------------------------------------------------------------

TEST_CASE("CfdContinuity_TheInputIsOneValuePerFaceInFaceIndexOrder", "[cfd][cont]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();
    const std::vector<double> phi = zeroFlux(hand.topology);
    REQUIRE(phi.size() == hand.topology.faces());
    REQUIRE(hand.topology.faces() ==
            hand.topology.internalFaces() + hand.topology.boundaryFaces());

    CHECK(evaluate(hand, boundary, phi).cells() == hand.topology.cells());

    // One short and one long are both the LENGTH fault, and neither is read as
    // whatever happens to sit in the array.
    CHECK(refusal(hand, boundary, shortened(phi)).problem == CfdContinuityProblem::ValueLengthWrong);
    std::vector<double> tooLong = phi;
    tooLong.push_back(0.0);
    CHECK(refusal(hand, boundary, tooLong).problem == CfdContinuityProblem::ValueLengthWrong);
}

TEST_CASE("CfdContinuity_ThePhiIsTheOneTheQualifiedProducerMakes", "[cfd][cont]") {
    // ITEM 1 IS SATISFIED BY BINDING, NOT BY DEFINING. The field under test is
    // built by `cfdMassFlux` -- ADR-072's producer, already qualified -- and fed
    // to the evaluator unchanged. If this evaluator had its own notion of the
    // convention, the two would disagree here.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.outlets();
    const std::vector<double> phi =
        producedFlux(hand, boundary, Velocity3{Velocity::fromSi(1), Velocity::fromSi(2), Velocity::fromSi(3)}, Density::fromSi(998.2));

    const CfdContinuityReport report = evaluate(hand, boundary, phi);
    const CfdMassFluxDivergenceField independent = independentDivergence(hand, phi);

    // (a) THE DECOMPOSITION, bit-for-bit against an independently taken
    // divergence of the same input.
    REQUIRE(report.divergence().cells() == independent.cells());
    for (std::size_t c = 0; c < report.cells(); ++c) {
        const CellIndex cell = CellIndex::fromValue(c);
        const std::optional<MassFlowRate> mine = report.cellResidualAt(cell);
        const std::optional<MassFlowRate> theirs = independent.at(cell);
        REQUIRE(mine.has_value());
        REQUIRE(theirs.has_value());
        CHECK(mine->si() == theirs->si());
    }
    CHECK(report.netBoundaryFlux().si() == independent.netBoundaryFlux().si());
    CHECK(report.largestCellResidual().si() == independent.largestMagnitude().si());
}

TEST_CASE("CfdContinuity_APositiveBoundaryPhiIsOutflowAndANegativeOneIsInflow", "[cfd][cont]") {
    // (c) THE SIGN CONVENTION, pinned directly and in both directions, because
    // every later attribution claim rests on it.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const PreparedCfdBoundary boundary = hand.outlets();

    const std::vector<double> out = boundaryFlux(hand.topology, kDiscriminatingConstant);
    const CfdContinuityReport outward = evaluate(hand, boundary, out);
    CHECK(outward.outflow().si() > 0.0);
    CHECK(outward.inflow().si() == 0.0);
    CHECK(outward.summary().outflowFaces == hand.topology.boundaryFaces());
    CHECK(outward.summary().inflowFaces == 0);
    CHECK(outward.netBoundaryFlux().si() > 0.0);

    const std::vector<double> in = boundaryFlux(hand.topology, -kDiscriminatingConstant);
    const CfdContinuityReport inward = evaluate(hand, boundary, in);
    CHECK(inward.inflow().si() > 0.0);
    CHECK(inward.outflow().si() == 0.0);
    CHECK(inward.summary().inflowFaces == hand.topology.boundaryFaces());
    CHECK(inward.summary().outflowFaces == 0);
    CHECK(inward.netBoundaryFlux().si() < 0.0);

    // INFLOW AND OUTFLOW ARE MAGNITUDES AND ARE NEVER NEGATIVE, which is what
    // makes `outflow - inflow` meaningful at all.
    CHECK(inward.inflow().si() == outward.outflow().si());
}

// ---------------------------------------------------------------------------
// Item 2 — the cell continuity residual
// ---------------------------------------------------------------------------

TEST_CASE("CfdContinuity_TheCellResidualIsTheDivergencesOwnValueAndNotASecondSum", "[cfd][cont]") {
    const Hand hand = Hand::make(chainPoints(4), chainTets(4));
    const PreparedCfdBoundary boundary = hand.outlets();
    const std::vector<double> phi =
        producedFlux(hand, boundary, Velocity3{Velocity::fromSi(0.5), -Velocity::fromSi(0.25), Velocity::fromSi(2)}, Density::fromSi(1.225));
    const CfdContinuityReport report = evaluate(hand, boundary, phi);

    std::size_t checked = 0;
    for (std::size_t c = 0; c < report.cells(); ++c) {
        const CellIndex cell = CellIndex::fromValue(c);
        const std::optional<MassFlowRate> residual = report.cellResidualAt(cell);
        const std::optional<MassFlowRate> fromField = report.divergence().at(cell);
        REQUIRE(residual.has_value());
        REQUIRE(fromField.has_value());
        CHECK(residual->si() == fromField->si());
        ++checked;
    }
    REQUIRE(checked == hand.topology.cells());
    REQUIRE(checked > 0);

    // Total over its inputs: a cell the report does not hold is an empty
    // optional and never undefined behaviour.
    CHECK_FALSE(report.cellResidualAt(CellIndex::fromValue(report.cells())).has_value());
}

TEST_CASE("CfdContinuity_AConservativeFieldClearsTheCellTolerance", "[cfd][cont]") {
    // THE FLOATING-POINT FLOOR, MEASURED BEFORE THIS CODE EXISTED. A uniform
    // velocity gives `phi_f = rho (U . S_f)`, whose sum over a closed cell
    // vanishes in real arithmetic, so what is left is rounding. ADR-074's Q1
    // measured the worst at 7.4598e-15 over 20,000 random tetrahedra per scale
    // and the tolerance is one decade above it.
    //
    // THIS TEST USED `walls()` AND ASSERTED NO TOLERANCE AT ALL -- both reviews
    // found it independently. On a wall boundary ADR-067's table makes every
    // boundary `phi` bit-zero, so each cell's residual is the FULL internal
    // flux, O(1) rather than O(1e-15): the field LEAST able to clear the bound
    // the test is named for. Worse, the throughput is then zero and
    // `massBalance()` is deliberately absent, so the bound could not be read
    // even if the field had been conservative. The one assertion it did make
    // duplicated `AnInternalFacesTwoContributionsSumToExactlyZero`.
    //
    // `outlets()` GIVES `phi` THE `Computed` POLICY at the boundary, so the
    // producer uses the interior velocity, the cell sums telescope to the mesh
    // closure, and the throughput is positive -- which is what makes the
    // measured floor readable here.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.outlets();
    const std::vector<double> phi =
        producedFlux(hand, boundary, Velocity3{Velocity::fromSi(3), -Velocity::fromSi(1), Velocity::fromSi(0.5)}, Density::fromSi(998.2));

    const CfdContinuityReport report = evaluate(hand, boundary, phi);

    // THE PREMISE: the field really is conservative, so the residual is the
    // mesh closure and nothing else, and the throughput really is positive, so
    // the measure exists.
    REQUIRE(report.throughput().si() > 0.0);
    REQUIRE(report.massBalance().has_value());

    // AND NOW THE BOUND THE TEST IS NAMED FOR.
    INFO("maxCellContinuity = " << report.massBalance()->maxCellContinuity
                                << ", worstClosure = " << report.summary().worstClosure);
    CHECK(report.massBalance()->maxCellContinuity >= 0.0);
    CHECK(report.massBalance()->maxCellContinuity < cfd::kCfdContinuityCellTolerance);

    // Per cell as well as at the maximum, each scaled by the magnitude that
    // cell accumulated -- which is the quantity Q1 actually measured.
    std::size_t checked = 0;
    for (std::size_t c = 0; c < report.cells(); ++c) {
        const CellIndex cell = CellIndex::fromValue(c);
        const std::optional<MassFlowRate> residual = report.cellResidualAt(cell);
        REQUIRE(residual.has_value());
        double magnitude = 0.0;
        for (const FaceIndex face : hand.topology.cellFaces(cell)) {
            magnitude += std::abs(phi[face.value()]);
        }
        REQUIRE(magnitude > 0.0);
        const double relative = std::abs(residual->si()) / magnitude;
        INFO("cell " << c << " relative residual " << relative);
        CHECK(relative < cfd::kCfdContinuityCellTolerance);
        ++checked;
    }
    REQUIRE(checked == hand.topology.cells());
    REQUIRE(checked >= 2);
}

TEST_CASE("CfdContinuity_TheWorstCellIsTheFirstInIndexOrderWhenSeveralTie", "[cfd][cont]") {
    // A TIE IS REACHABLE, which is why the header promises an order rather than
    // leaving "the worst cell" to the traversal. On the two-cell wall fixture
    // the two residuals are exact negations, so their MAGNITUDES tie exactly.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();
    const std::vector<double> phi =
        producedFlux(hand, boundary, Velocity3{Velocity::fromSi(3), -Velocity::fromSi(1), Velocity::fromSi(0.5)}, Density::fromSi(998.2));
    const CfdContinuityReport report = evaluate(hand, boundary, phi);

    const std::optional<MassFlowRate> a = report.cellResidualAt(CellIndex::fromValue(0));
    const std::optional<MassFlowRate> b = report.cellResidualAt(CellIndex::fromValue(1));
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(std::abs(a->si()) == std::abs(b->si()));
    REQUIRE(std::abs(a->si()) > 0.0); // otherwise the tie is vacuous

    REQUIRE(report.worstCell().has_value());
    CHECK(report.worstCell()->value() == 0);
}

TEST_CASE("CfdContinuity_TheLargestCellResidualEqualsTheDivergencesAccessorExactly",
          "[cfd][cont]") {
    // A MAXIMUM OVER THE SAME FINITE VALUES IS ORDER-INDEPENDENT in a way a sum
    // is not, so this one IS an exact identity and is asserted as one. The
    // divergence reports the magnitude without saying where; this evaluator
    // walks for the location and must not disturb the value on the way.
    const Hand hand = Hand::make(chainPoints(5), chainTets(5));
    const PreparedCfdBoundary boundary = hand.outlets();
    const std::vector<double> phi = boundaryFlux(hand.topology, kDiscriminatingConstant);
    const CfdContinuityReport report = evaluate(hand, boundary, phi);

    CHECK(report.largestCellResidual().si() == report.divergence().largestMagnitude().si());
    CHECK(report.largestCellResidual().si() ==
          independentDivergence(hand, phi).largestMagnitude().si());
}

// ---------------------------------------------------------------------------
// Item 3 — the global mass imbalance, and the name collision
// ---------------------------------------------------------------------------

TEST_CASE("CfdContinuity_TheGlobalImbalanceIsTheBoundarySumAndNotTheAccumulationArtifact",
          "[cfd][cont]") {
    // ============================================================
    // THE MOST IMPORTANT TEST IN THIS FILE.
    //
    // `CfdDivergenceField::globalImbalance()` and
    // `CfdMassBalance::globalImbalance` have THE SAME NAME and mean DIFFERENT
    // QUANTITIES:
    //
    //   the divergence's   globalNetFlux() - netBoundaryFlux()
    //                      a floating-point artifact of the accumulation order,
    //                      which ADR-071 measured bit-zero on 40.34% and at
    //                      worst 1.125e-16 relative
    //   the mass balance's |sum over boundary phi| / throughput
    //                      the PHYSICAL imbalance
    //
    // Wiring the first into the second would pass EVERY mass balance, always,
    // because the artifact stays near 1e-16 however badly mass conservation
    // fails -- and nothing in the type system objects, since both are a
    // `double` behind a member of that name.
    //
    // The field below is maximally unbalanced: every boundary face carries the
    // same positive flux, so all mass leaves and none enters. The physical
    // measure must be of order 1; the artifact must stay near the eps.
    // ============================================================
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.outlets();
    const std::vector<double> phi = boundaryFlux(hand.topology, kDiscriminatingConstant);
    const CfdContinuityReport report = evaluate(hand, boundary, phi);

    REQUIRE(report.massBalance().has_value());
    const CfdMassBalance balance = *report.massBalance();

    // ALL FLOW ONE WAY: |net| == sum|phi| == 2 * throughput, so the relative
    // measure is exactly its maximum of 2.
    const double magnitude = boundaryMagnitude(hand.topology, phi);
    REQUIRE(magnitude > 0.0);
    CHECK_THAT(report.throughput().si(),
               Catch::Matchers::WithinRel(0.5 * magnitude, 1.0e-15));
    CHECK_THAT(balance.globalImbalance, Catch::Matchers::WithinRel(2.0, 1.0e-14));

    // THE ARTIFACT IS MANY ORDERS OF MAGNITUDE SMALLER -- about 2e14 on this
    // fixture, asserted below at a deliberately slack 1e10 so the test states a
    // separation it can defend. (An earlier comment claimed "sixteen orders"
    // while the evidence said ten and the assertion required 1e10; a review
    // found the three disagreeing.) This is the
    // separation a swap would have to survive, and it cannot.
    CHECK(report.summary().accumulationResidual < cfd::kCfdContinuityAggregationTolerance);
    CHECK(balance.globalImbalance > 1.0);
    CHECK(balance.globalImbalance / std::max(report.summary().accumulationResidual, 1.0e-300) >
          1.0e10);

    // And the artifact is NOT what the mass balance reports, stated as the
    // inequality a mutation would have to break.
    CHECK(balance.globalImbalance != report.summary().accumulationResidual);
}

TEST_CASE("CfdContinuity_TheRelativeGlobalImbalanceLiesBetweenZeroAndTwo", "[cfd][cont]") {
    // AN INVARIANT OF THE DENOMINATOR, measured at 1.9590 worst over 20,000
    // random unbalanced fields and provable: |sum phi| <= sum |phi| = 2 *
    // throughput. It is asserted across several regimes rather than one.
    const Hand hand = Hand::make(chainPoints(4), chainTets(4));
    const PreparedCfdBoundary boundary = hand.outlets();

    for (const double value : {kDiscriminatingConstant, -kDiscriminatingConstant, 1.0e-30, 1.0e30}) {
        std::vector<double> phi = boundaryFlux(hand.topology, value);
        const CfdContinuityReport report = evaluate(hand, boundary, phi);
        REQUIRE(report.massBalance().has_value());
        const double measure = report.massBalance()->globalImbalance;
        INFO("phi = " << value << ", measure = " << measure);
        CHECK(measure >= 0.0);
        CHECK(measure <= 2.0 + 1.0e-12);
        CHECK(report.massBalance()->maxCellContinuity >= 0.0);
    }
}

TEST_CASE("CfdContinuity_ABalancedBoundaryGivesANearZeroImbalance", "[cfd][cont]") {
    // THE DISCRIMINATION, from the other end: a field whose boundary fluxes
    // cancel must report a tiny relative imbalance, so the measure separates
    // balanced from unbalanced rather than merely producing a number. Half the
    // boundary faces carry `+v` and half `-v`.
    const Hand hand = Hand::make(chainPoints(4), chainTets(4));
    const PreparedCfdBoundary boundary = hand.outlets();
    const std::size_t boundaryFaces = hand.topology.boundaryFaces();
    REQUIRE(boundaryFaces >= 2);
    REQUIRE(boundaryFaces % 2 == 0);

    std::vector<double> phi(hand.topology.faces(), 0.0);
    for (std::size_t b = 0; b < boundaryFaces; ++b) {
        const FaceIndex face = hand.topology.faceOf(BoundaryFaceIndex::fromValue(b));
        phi[face.value()] = (b % 2 == 0 ? 1.0 : -1.0) * kDiscriminatingConstant;
    }

    const CfdContinuityReport report = evaluate(hand, boundary, phi);
    REQUIRE(report.massBalance().has_value());
    INFO("globalImbalance = " << report.massBalance()->globalImbalance);
    CHECK(report.massBalance()->globalImbalance < cfd::kCfdContinuityAggregationTolerance);
    CHECK(report.inflow().si() == report.outflow().si());
}

// ---------------------------------------------------------------------------
// Item 4 — boundary mass flow
// ---------------------------------------------------------------------------

TEST_CASE("CfdContinuity_TheFaceCountsSplitTheBoundaryByTheSignOfPhi", "[cfd][cont]") {
    const Hand hand = Hand::make(chainPoints(4), chainTets(4));
    const PreparedCfdBoundary boundary = hand.outlets();
    const std::size_t boundaryFaces = hand.topology.boundaryFaces();

    std::vector<double> phi(hand.topology.faces(), 0.0);
    std::size_t expectedOut = 0;
    std::size_t expectedIn = 0;
    std::size_t expectedZero = 0;
    for (std::size_t b = 0; b < boundaryFaces; ++b) {
        const FaceIndex face = hand.topology.faceOf(BoundaryFaceIndex::fromValue(b));
        if (b % 3 == 0) {
            phi[face.value()] = kDiscriminatingConstant;
            ++expectedOut;
        } else if (b % 3 == 1) {
            phi[face.value()] = -kDiscriminatingConstant;
            ++expectedIn;
        } else {
            phi[face.value()] = 0.0;
            ++expectedZero;
        }
    }
    REQUIRE(expectedOut > 0);
    REQUIRE(expectedIn > 0);
    REQUIRE(expectedZero > 0);

    const CfdContinuityReport report = evaluate(hand, boundary, phi);
    CHECK(report.summary().outflowFaces == expectedOut);
    CHECK(report.summary().inflowFaces == expectedIn);
    CHECK(report.summary().zeroFluxFaces == expectedZero);
    CHECK(report.summary().outflowFaces + report.summary().inflowFaces +
              report.summary().zeroFluxFaces ==
          boundaryFaces);
    CHECK(report.summary().boundaryFaces == boundaryFaces);

    CHECK(report.inflow().si() >= 0.0);
    CHECK(report.outflow().si() >= 0.0);
}

TEST_CASE("CfdContinuity_ANegativeZeroIsNeitherInflowNorOutflow", "[cfd][cont]") {
    // `-0.0` COMPARES EQUAL TO `0.0` AND SO LANDS IN NEITHER BRANCH, which is
    // the correct answer and is pinned because the obvious alternative --
    // testing `std::signbit` -- would make the FACE COUNTS depend on a signed
    // zero, and a field assembled by subtraction produces `-0.0` routinely.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const PreparedCfdBoundary boundary = hand.outlets();
    std::vector<double> phi(hand.topology.faces(), 0.0);
    for (std::size_t b = 0; b < hand.topology.boundaryFaces(); ++b) {
        const FaceIndex face = hand.topology.faceOf(BoundaryFaceIndex::fromValue(b));
        phi[face.value()] = -0.0;
    }
    REQUIRE(std::signbit(phi[hand.topology.faceOf(BoundaryFaceIndex::fromValue(0)).value()]));

    const CfdContinuityReport report = evaluate(hand, boundary, phi);
    CHECK(report.summary().zeroFluxFaces == hand.topology.boundaryFaces());
    CHECK(report.summary().inflowFaces == 0);
    CHECK(report.summary().outflowFaces == 0);
    CHECK(report.inflow().si() == 0.0);
    CHECK(report.outflow().si() == 0.0);
}

// ---------------------------------------------------------------------------
// Item 5 — internal-face cancellation
// ---------------------------------------------------------------------------

TEST_CASE("CfdContinuity_AnInternalFacesTwoContributionsSumToExactlyZero", "[cfd][cont]") {
    // DIRECTLY, AS ADR-071 SAYS A CLAIM LIKE THIS BELONGS: read the two
    // contributions off a two-cell field rather than carrying a flag that
    // cannot read false. With a wall boundary every boundary `phi` is bit-zero,
    // so each cell's residual IS its single internal face's contribution.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();
    REQUIRE(hand.topology.internalFaces() == 1);

    std::vector<double> phi(hand.topology.faces(), 0.0);
    phi[0] = kDiscriminatingConstant; // face 0 is internal: ADR-064 numbers them first
    REQUIRE(hand.topology.isInternal(FaceIndex::fromValue(0)));

    const CfdContinuityReport report = evaluate(hand, boundary, phi);
    const std::optional<MassFlowRate> owner = report.cellResidualAt(CellIndex::fromValue(0));
    const std::optional<MassFlowRate> neighbour = report.cellResidualAt(CellIndex::fromValue(1));
    REQUIRE(owner.has_value());
    REQUIRE(neighbour.has_value());

    CHECK(owner->si() == kDiscriminatingConstant);
    CHECK(neighbour->si() == -kDiscriminatingConstant);
    CHECK(owner->si() + neighbour->si() == 0.0);

    // AND THE BOUNDARY SEES NOTHING OF IT, which is the point: an internal face
    // moves mass between cells and none across the boundary.
    CHECK(report.netBoundaryFlux().si() == 0.0);
    CHECK(report.inflow().si() == 0.0);
    CHECK(report.outflow().si() == 0.0);
}

TEST_CASE("CfdContinuity_TheAccumulationResidualIsTheMeasuredCancellation", "[cfd][cont]") {
    // THE AGGREGATE CONSEQUENCE of internal-face cancellation, and a
    // falsifiable one: a scatter writing `+flux` to both sides instead of
    // `+flux` and `-flux` would make this of order 1 rather than of order the
    // eps. It is reported rather than asserted zero because each cell
    // accumulates its own faces before the two sides of an internal face meet,
    // and `(a + x) + (b - x)` is not `a + b`.
    const Hand hand = Hand::make(chainPoints(5), chainTets(5));
    const PreparedCfdBoundary boundary = hand.outlets();
    const std::vector<double> phi =
        producedFlux(hand, boundary, Velocity3{Velocity::fromSi(1), Velocity::fromSi(1), Velocity::fromSi(1)}, Density::fromSi(998.2));
    REQUIRE(hand.topology.internalFaces() > 0);

    const CfdContinuityReport report = evaluate(hand, boundary, phi);
    INFO("accumulationResidual = " << report.summary().accumulationResidual);
    CHECK(report.summary().accumulationResidual < cfd::kCfdContinuityAggregationTolerance);

    // AND IT IS THE DIFFERENCE OF THE TWO SUMS, not something else of a similar
    // size: re-derived here from the divergence's own two accessors.
    const double magnitude = boundaryMagnitude(hand.topology, phi);
    REQUIRE(magnitude > 0.0);
    const double expected =
        std::abs(report.divergence().globalNetFlux().si() - report.netBoundaryFlux().si()) /
        magnitude;
    CHECK(report.summary().accumulationResidual == expected);
}

// ---------------------------------------------------------------------------
// Item 6 — the closed-domain check
// ---------------------------------------------------------------------------

TEST_CASE("CfdContinuity_AllThreeWallKindsMakeAClosedDomain", "[cfd][cont]") {
    // ALL THREE, because `isWallKind` is a total switch and a wrong arm would
    // show only on the kind it got wrong.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const std::vector<double> phi = zeroFlux(hand.topology);

    // NO-SLIP AND SLIP BIND UNIFORMLY; a MOVING wall needs a planar target, so
    // it is reached on one facet with no-slip elsewhere. All three kinds are
    // walls, so every one of these domains is closed.
    std::size_t checked = 0;
    for (const PreparedCfdBoundary& boundary :
         {hand.walls(), hand.slips(), hand.movingWallOnOneFacet()}) {
        const CfdContinuityReport report = evaluate(hand, boundary, phi);
        INFO("wall arrangement " << checked);
        CHECK(report.summary().closedDomain);
        CHECK(report.summary().wallFaces == hand.topology.boundaryFaces());
        CHECK(report.summary().openFaces == 0);
        ++checked;
    }
    REQUIRE(checked == 3);

    // AND THE MOVING-WALL ARM IS REACHED RATHER THAN ASSUMED: the mixed
    // arrangement really does carry a `MovingWall` facet, asserted off the
    // prepared boundary so a binder that silently substituted no-slip would
    // make this fail instead of passing quietly.
    const PreparedCfdBoundary mixedWalls = hand.movingWallOnOneFacet();
    std::size_t movingFacets = 0;
    for (std::size_t b = 0; b < hand.topology.boundaryFaces(); ++b) {
        const std::optional<cfd::CfdBoundaryFace> face =
            mixedWalls.face(BoundaryFaceIndex::fromValue(b));
        REQUIRE(face.has_value());
        if (face->kind == CfdBoundaryConditionKind::MovingWall) {
            ++movingFacets;
        }
    }
    CHECK(movingFacets == 1);
}

TEST_CASE("CfdContinuity_AClosedDomainHasExactlyZeroBoundaryFluxAndNoMassBalance", "[cfd][cont]") {
    // EXACTLY, NOT TO A TOLERANCE. ADR-067's table prescribes `phi = 0` at all
    // three wall kinds and ADR-072 writes `0.0` there, so the boundary sum is
    // bit-zero -- measured at 0 non-zero nets in 5,000 trials. The `phi` here
    // is the PRODUCER's, so the zero is the table's and not the test's.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();
    const std::vector<double> phi =
        producedFlux(hand, boundary, Velocity3{Velocity::fromSi(7), -Velocity::fromSi(3), Velocity::fromSi(11)}, Density::fromSi(998.2));

    for (std::size_t b = 0; b < hand.topology.boundaryFaces(); ++b) {
        const FaceIndex face = hand.topology.faceOf(BoundaryFaceIndex::fromValue(b));
        INFO("boundary face " << b);
        CHECK(phi[face.value()] == 0.0);
    }

    const CfdContinuityReport report = evaluate(hand, boundary, phi);
    CHECK(report.summary().closedDomain);
    CHECK(report.netBoundaryFlux().si() == 0.0);
    CHECK(report.throughput().si() == 0.0);

    // AND THE RELATIVE MEASURES ARE ABSENT, not zero. Reporting 0.0 would claim
    // perfect balance where the measure is undefined, and ADR-074 Decision 9
    // leaves the floor that would make it total to P19-MASSBAL-001.
    CHECK_FALSE(report.massBalance().has_value());
}

TEST_CASE("CfdContinuity_AVelocityInletIsNotAWallSoTheDomainIsNotClosed", "[cfd][cont]") {
    // THE TRAP `isWallKind` EXISTS TO AVOID. ADR-067's table gives `phi` the
    // `FixedValue` policy at a velocity inlet AND at all three walls -- only the
    // VALUE differs. So a `closedDomain` derived from
    // `treatmentOf(kind).massFlux == FixedValue` would call this mesh closed,
    // with an inlet in it.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const PreparedCfdBoundary boundary = hand.inlets(Velocity::fromSi(2));
    REQUIRE(cfd::treatmentOf(CfdBoundaryConditionKind::VelocityInlet).massFlux ==
            CfdBoundaryPolicy::FixedValue);
    REQUIRE(cfd::treatmentOf(CfdBoundaryConditionKind::NoSlipWall).massFlux ==
            CfdBoundaryPolicy::FixedValue);

    const std::vector<double> phi =
        producedFlux(hand, boundary, Velocity3{Velocity::fromSi(1), Velocity::fromSi(0), Velocity::fromSi(0)}, Density::fromSi(998.2));
    const CfdContinuityReport report = evaluate(hand, boundary, phi);

    CHECK_FALSE(report.summary().closedDomain);
    CHECK(report.summary().wallFaces == 0);
    CHECK(report.summary().openFaces == hand.topology.boundaryFaces());
}

TEST_CASE("CfdContinuity_WallFacesCountTheConditionAndZeroFluxFacesCountTheValue", "[cfd][cont]") {
    // THE DISTINCTION THAT MAKES `closedDomain` WORTH HAVING. `wallFaces` is
    // what the boundary CONDITION says; `zeroFluxFaces` is what the supplied
    // `phi` actually is. A caller-supplied `phi` that is non-zero on a wall face
    // makes them disagree, and the disagreement is VISIBLE rather than silently
    // reconciled -- ADR-071 Decision 5's rule that a policy says where a value
    // comes from, not whether a sum may be taken over it.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();
    const std::vector<double> phi = boundaryFlux(hand.topology, kDiscriminatingConstant);

    const CfdContinuityReport report = evaluate(hand, boundary, phi);
    CHECK(report.summary().closedDomain);                                  // by the table
    CHECK(report.summary().wallFaces == hand.topology.boundaryFaces());    // by the table
    CHECK(report.summary().zeroFluxFaces == 0);                            // by the value
    CHECK(report.summary().outflowFaces == hand.topology.boundaryFaces()); // by the value
    CHECK(report.netBoundaryFlux().si() > 0.0);
    // The mass balance EXISTS here, because the throughput is positive even
    // though the domain is closed by the table. Nothing is refused.
    CHECK(report.massBalance().has_value());
}

// ---------------------------------------------------------------------------
// Item 7 — the inlet/outlet check is the global imbalance
// ---------------------------------------------------------------------------

TEST_CASE("CfdContinuity_APressureInletCarryingOutflowIsReportedAndNotRefused", "[cfd][cont]") {
    // ADR-074 DECISION 4, AND ADR-058'S REASON FOR IT: at a pressure boundary
    // the flow rate is an OUTCOME of the solve, so a facet named an inlet may
    // carry outflow, and P19-CONV-001 already reports `backflowFaces`.
    // Attributing direction from the condition's NAME would be wrong exactly in
    // the regime this diagnostic exists for.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.inletAndOutlet();

    // EVERY boundary face carries OUTFLOW, including the ones named inlet.
    const std::vector<double> phi = boundaryFlux(hand.topology, kDiscriminatingConstant);
    const CfdContinuityReport report = evaluate(hand, boundary, phi);

    CHECK(report.summary().outflowFaces == hand.topology.boundaryFaces());
    CHECK(report.summary().inflowFaces == 0);
    CHECK(report.inflow().si() == 0.0);

    // The patch named `PressureInlet` is present, carries outflow, and is not
    // an error. THE JUXTAPOSITION IS THE DIAGNOSTIC.
    std::size_t inletPatchesCarryingOutflow = 0;
    for (const CfdPatchMassFlow& patch : report.patches()) {
        if (patch.kind == CfdBoundaryConditionKind::PressureInlet && patch.outflowFaces > 0) {
            ++inletPatchesCarryingOutflow;
            CHECK(patch.inflow.si() == 0.0);
            CHECK(patch.outflow.si() > 0.0);
        }
    }
    INFO("inlet patches carrying outflow = " << inletPatchesCarryingOutflow);
    CHECK(inletPatchesCarryingOutflow == 0); // single-patch fixture: see the split test below
}

TEST_CASE("CfdContinuity_TheCheckIsTheGlobalImbalanceAndNotOutletMinusInlet", "[cfd][cont]") {
    // ADR-058's section heading is "Mass balance means a discrete measure, not a
    // sum of inlets and outlets". This pins that the reported measure is the
    // BOUNDARY SUM, and that `outflow - inflow` is reported as agreeing to a
    // MEASURED bound rather than asserted equal.
    const Hand hand = Hand::make(chainPoints(4), chainTets(4));
    const PreparedCfdBoundary boundary = hand.outlets();
    const std::size_t boundaryFaces = hand.topology.boundaryFaces();

    std::vector<double> phi(hand.topology.faces(), 0.0);
    for (std::size_t b = 0; b < boundaryFaces; ++b) {
        const FaceIndex face = hand.topology.faceOf(BoundaryFaceIndex::fromValue(b));
        // Magnitudes spanning several orders, so the two accumulation orders
        // genuinely differ -- the regime Q2 measured.
        const double scale = std::pow(10.0, static_cast<double>(b % 7) - 3.0);
        phi[face.value()] = (b % 2 == 0 ? 1.0 : -1.0) * kDiscriminatingConstant * scale;
    }

    const CfdContinuityReport report = evaluate(hand, boundary, phi);
    const double magnitude = boundaryMagnitude(hand.topology, phi);
    REQUIRE(magnitude > 0.0);

    // The reported departure IS the departure, re-derived from the accessors.
    const double expected =
        std::abs((report.outflow().si() - report.inflow().si()) - report.netBoundaryFlux().si()) /
        magnitude;
    CHECK(report.summary().directionalSplitResidual == expected);
    INFO("directionalSplitResidual = " << report.summary().directionalSplitResidual);
    CHECK(report.summary().directionalSplitResidual < cfd::kCfdContinuityAggregationTolerance);

    // And the headline number is the DIVERGENCE's, not this file's own sum.
    CHECK(report.netBoundaryFlux().si() == report.divergence().netBoundaryFlux().si());
}

// ---------------------------------------------------------------------------
// Item 9 — multiple inlets and outlets
// ---------------------------------------------------------------------------

TEST_CASE("CfdContinuity_EveryPatchGetsARecordInPatchIndexOrder", "[cfd][cont]") {
    const Hand hand = Hand::split(orthogonalPoints(), kTwoCells, 3);
    const PreparedCfdBoundary boundary = hand.outlets();
    REQUIRE(hand.topology.patches() == 3);

    const std::vector<double> phi = boundaryFlux(hand.topology, kDiscriminatingConstant);
    const CfdContinuityReport report = evaluate(hand, boundary, phi);

    REQUIRE(report.patches().size() == hand.topology.patches());
    CHECK(report.summary().patches == hand.topology.patches());
    std::size_t totalFaces = 0;
    for (std::size_t p = 0; p < report.patches().size(); ++p) {
        INFO("patch " << p);
        CHECK(report.patches()[p].patch.value() == p); // PatchIndex ORDER
        totalFaces += report.patches()[p].faces;
        CHECK(report.patches()[p].inflowFaces + report.patches()[p].outflowFaces +
                  report.patches()[p].zeroFluxFaces ==
              report.patches()[p].faces);
    }
    CHECK(totalFaces == hand.topology.boundaryFaces());
}

TEST_CASE("CfdContinuity_PerPatchSumsReconcileWithTheTotalWithinTheAggregationTolerance",
          "[cfd][cont]") {
    // NOT AN IDENTITY: ADR-074's Q3 measured the per-patch reconstruction
    // bit-exact on 1.93% of balanced fields, worst 9.6334e-16. The patches are
    // interleaved by `splitPatches`, so the two accumulation orders genuinely
    // differ rather than coinciding by construction.
    const Hand hand = Hand::split(chainPoints(4), chainTets(4), 4);
    const PreparedCfdBoundary boundary = hand.outlets();
    REQUIRE(hand.topology.patches() == 4);
    const std::size_t boundaryFaces = hand.topology.boundaryFaces();

    std::vector<double> phi(hand.topology.faces(), 0.0);
    for (std::size_t b = 0; b < boundaryFaces; ++b) {
        const FaceIndex face = hand.topology.faceOf(BoundaryFaceIndex::fromValue(b));
        const double scale = std::pow(10.0, static_cast<double>(b % 5) - 2.0);
        phi[face.value()] = (b % 2 == 0 ? 1.0 : -1.0) * kDiscriminatingConstant * scale;
    }

    const CfdContinuityReport report = evaluate(hand, boundary, phi);
    const double magnitude = boundaryMagnitude(hand.topology, phi);
    REQUIRE(magnitude > 0.0);

    double patchSum = 0.0;
    double patchInflow = 0.0;
    double patchOutflow = 0.0;
    for (const CfdPatchMassFlow& patch : report.patches()) {
        patchSum += patch.net.si();
        patchInflow += patch.inflow.si();
        patchOutflow += patch.outflow.si();
    }
    const double expected = std::abs(patchSum - report.netBoundaryFlux().si()) / magnitude;
    CHECK(report.summary().patchSumResidual == expected);
    INFO("patchSumResidual = " << report.summary().patchSumResidual);
    CHECK(report.summary().patchSumResidual < cfd::kCfdContinuityAggregationTolerance);

    // The per-patch inflow and outflow reconstruct the totals to the same bound.
    CHECK_THAT(patchInflow,
               Catch::Matchers::WithinAbs(report.inflow().si(),
                                          cfd::kCfdContinuityAggregationTolerance * magnitude));
    CHECK_THAT(patchOutflow,
               Catch::Matchers::WithinAbs(report.outflow().si(),
                                          cfd::kCfdContinuityAggregationTolerance * magnitude));
}

TEST_CASE("CfdContinuity_TwoConditionsOnSeparatePatchesAreAttributedSeparately", "[cfd][cont]") {
    // ONE INLET PATCH AND ONE OUTLET PATCH, which is item 9's content and which
    // no existing cfd fixture could produce.
    //
    // TWO COMMENTS HERE SAID THINGS THIS TEST DOES NOT DO, and a review found
    // both. It claimed "TWO INLETS AND TWO OUTLETS" of a fixture with one of
    // each. And it claimed "the inlet patch carries inflow, the outlet patch
    // outflow" -- which is neither asserted nor true: `phi` is assigned by
    // `BoundaryFaceIndex` parity while patch membership is contiguous (and
    // `facets()` sorts by `ElementId`, so the two orders are unrelated), and
    // the assertions below are the four GLOBAL ones. Which signs landed on
    // which patch is pinned by `EachPatchRecordDescribesItsOwnFaces` instead,
    // which re-derives each patch's contents from the topology.
    //
    // What this test does constrain: both kinds are seen, each patch carries
    // exactly one of them, and both directions are present on the mesh.
    const Hand hand = Hand::split(orthogonalPoints(), kTwoCells, 2);
    const PreparedCfdBoundary boundary = hand.inletAndOutlet();
    REQUIRE(hand.topology.patches() == 2);

    // Mixed signs across the boundary, by face parity -- NOT by patch.
    std::vector<double> phi(hand.topology.faces(), 0.0);
    for (std::size_t b = 0; b < hand.topology.boundaryFaces(); ++b) {
        const FaceIndex face = hand.topology.faceOf(BoundaryFaceIndex::fromValue(b));
        phi[face.value()] = (b % 2 == 0 ? -1.0 : 1.0) * kDiscriminatingConstant;
    }

    const CfdContinuityReport report = evaluate(hand, boundary, phi);
    REQUIRE(report.patches().size() == 2);

    std::size_t kindsSeen = 0;
    for (const CfdPatchMassFlow& patch : report.patches()) {
        INFO("patch " << patch.patch.value());
        if (patch.kind.has_value()) {
            ++kindsSeen;
            CHECK((*patch.kind == CfdBoundaryConditionKind::PressureInlet ||
                   *patch.kind == CfdBoundaryConditionKind::PressureOutlet));
        }
    }
    CHECK(kindsSeen == 2);

    // Both directions are present and attributed to different patches.
    CHECK(report.summary().inflowFaces > 0);
    CHECK(report.summary().outflowFaces > 0);
    CHECK(report.inflow().si() > 0.0);
    CHECK(report.outflow().si() > 0.0);
}

TEST_CASE("CfdContinuity_AMixedPatchReportsNoSingleKind", "[cfd][cont]") {
    // ONE PATCH, TWO CONDITIONS. `mixed()` deals conditions by face parity and
    // `onePatch` puts every facet on patch 0, so patch 0 sees both kinds and
    // must report an empty optional -- which means MIXED, not absent.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.inletAndOutlet();
    REQUIRE(hand.topology.patches() == 1);

    const std::vector<double> phi = boundaryFlux(hand.topology, kDiscriminatingConstant);
    const CfdContinuityReport report = evaluate(hand, boundary, phi);

    REQUIRE(report.patches().size() == 1);
    CHECK_FALSE(report.patches()[0].kind.has_value());
    CHECK(report.patches()[0].faces == hand.topology.boundaryFaces());
}

// ---------------------------------------------------------------------------
// Item 10 — deterministic aggregation
// ---------------------------------------------------------------------------

TEST_CASE("CfdContinuity_TwoEvaluationsOfTheSameInputAreBitIdentical", "[cfd][cont]") {
    // DETERMINISM, which is a claim about repeatability and is DISTINCT from the
    // associativity claims above: those say two different orders disagree by a
    // bounded amount, this says one order gives one answer every time.
    const Hand hand = Hand::split(chainPoints(4), chainTets(4), 3);
    const PreparedCfdBoundary boundary = hand.inletAndOutlet();
    std::vector<double> phi(hand.topology.faces(), 0.0);
    for (std::size_t b = 0; b < hand.topology.boundaryFaces(); ++b) {
        const FaceIndex face = hand.topology.faceOf(BoundaryFaceIndex::fromValue(b));
        const double scale = std::pow(10.0, static_cast<double>(b % 9) - 4.0);
        phi[face.value()] = (b % 2 == 0 ? 1.0 : -1.0) * kDiscriminatingConstant * scale;
    }

    const CfdContinuityReport first = evaluate(hand, boundary, phi);
    const CfdContinuityReport second = evaluate(hand, boundary, phi);

    CHECK(first.summary() == second.summary());
    CHECK(first.inflow().si() == second.inflow().si());
    CHECK(first.outflow().si() == second.outflow().si());
    CHECK(first.netBoundaryFlux().si() == second.netBoundaryFlux().si());
    CHECK(first.throughput().si() == second.throughput().si());
    REQUIRE(first.massBalance().has_value());
    REQUIRE(second.massBalance().has_value());
    CHECK(first.massBalance()->maxCellContinuity == second.massBalance()->maxCellContinuity);
    CHECK(first.massBalance()->globalImbalance == second.massBalance()->globalImbalance);
    REQUIRE(first.patches().size() == second.patches().size());
    for (std::size_t p = 0; p < first.patches().size(); ++p) {
        INFO("patch " << p);
        CHECK(first.patches()[p] == second.patches()[p]);
    }
    for (std::size_t c = 0; c < first.cells(); ++c) {
        const CellIndex cell = CellIndex::fromValue(c);
        CHECK(first.cellResidualAt(cell)->si() == second.cellResidualAt(cell)->si());
    }
}

// ---------------------------------------------------------------------------
// Item 8 — the tolerances, and that they are not interchangeable
// ---------------------------------------------------------------------------

TEST_CASE("CfdContinuity_TheTwoTolerancesAreDistinctBecauseTheirMeasurementsAre", "[cfd][cont]") {
    // TWO AND NOT ONE, DELIBERATELY. The cell bound is one decade above
    // 7.4598e-15 and the aggregation bound one decade above 9.6334e-16. A single
    // shared `1e-13` would be a hundred times looser than the aggregation
    // measurement supports, and a formula error two orders of magnitude large in
    // the directional split or the per-patch sum would pass it.
    static_assert(cfd::kCfdContinuityCellTolerance > cfd::kCfdContinuityAggregationTolerance,
                  "the cell bound is the looser of the two, because its measurement is larger");
    static_assert(cfd::kCfdContinuityCellTolerance == 1.0e-13);
    static_assert(cfd::kCfdContinuityAggregationTolerance == 1.0e-14);

    // THE FOUR `CHECK`s THAT USED TO SIT HERE COMPARED TWO COMPILE-TIME
    // LITERALS -- `kCfdContinuityCellTolerance > 7.4598e-15` and siblings --
    // which the `static_assert`s above already pin exactly. A review called
    // them what they were: assertions about nothing. The relationship between a
    // constant and its measurement is a claim about the MEASUREMENT and belongs
    // in ADR-074; the claim about BEHAVIOUR is that a tolerance tightened past
    // its measurement must break the suite, which is a mutation probe (M21,
    // M22) and not a test.
    static_assert(cfd::kCfdContinuityCellTolerance > 7.4598e-15,
                  "Q1's worst measurement: below this the constant would be asking the suite to "
                  "beat the floor it was measured against");
    static_assert(cfd::kCfdContinuityAggregationTolerance > 9.6334e-16,
                  "Q2/Q3's worst measurement, for the same reason");
}

TEST_CASE("CfdContinuity_TheRelativeMeasuresAreDimensionlessAndTheAbsoluteOnesAreTyped",
          "[cfd][cont]") {
    // ARCHITECTURE.md'S RULE AGAINST A UNITLESS ENGINEERING API, kept by putting
    // the dimensioned answers where the dimension is known. Asserted at COMPILE
    // TIME, so it cannot drift.
    static_assert(std::is_same_v<decltype(std::declval<CfdContinuityReport>().inflow()),
                                 MassFlowRate>);
    static_assert(std::is_same_v<decltype(std::declval<CfdContinuityReport>().outflow()),
                                 MassFlowRate>);
    static_assert(
        std::is_same_v<decltype(std::declval<CfdContinuityReport>().netBoundaryFlux()),
                       MassFlowRate>);
    static_assert(std::is_same_v<decltype(std::declval<CfdContinuityReport>().throughput()),
                                 MassFlowRate>);
    static_assert(std::is_same_v<decltype(std::declval<CfdPatchMassFlow>().inflow), MassFlowRate>);
    static_assert(std::is_same_v<decltype(std::declval<CfdPatchMassFlow>().outflow), MassFlowRate>);
    static_assert(std::is_same_v<decltype(std::declval<CfdPatchMassFlow>().net), MassFlowRate>);

    // AND THE RELATIVE ONES ARE PLAIN DOUBLES, because a ratio of two mass flow
    // rates is dimensionless and typing it would be a lie about its dimension.
    static_assert(std::is_same_v<decltype(std::declval<CfdMassBalance>().globalImbalance), double>);
    static_assert(
        std::is_same_v<decltype(std::declval<CfdMassBalance>().maxCellContinuity), double>);
    static_assert(std::is_same_v<
                  decltype(std::declval<cfd::CfdContinuitySummary>().accumulationResidual), double>);

    // NO RUNTIME ASSERTION HERE, AND THAT IS THE POINT. This case used to end
    // `CHECK(true);` so Catch2 would count it as having asserted something; a
    // review listed it among the tautologies. The `static_assert`s above ARE
    // the test, and a build that reaches this line has already passed them.
    SUCCEED("the dimensional claims are compile-time; see the static_asserts above");
}

// ---------------------------------------------------------------------------
// The refusal gate
// ---------------------------------------------------------------------------

TEST_CASE("CfdContinuity_RefusesAGeometryBuiltForAnotherTopology", "[cfd][cont]") {
    const Hand one = Hand::make(orthogonalPoints(), kTwoCells);
    const Hand other = Hand::make(chainPoints(4), chainTets(4));
    const PreparedCfdBoundary boundary = one.walls();
    const std::vector<double> phi = zeroFlux(one.topology);

    const CfdContinuityInput input{phi};
    std::expected<CfdContinuityReport, CfdContinuityFailure> report =
        cfd::evaluateCfdContinuity(one.topology, other.geometry, boundary, input);
    REQUIRE_FALSE(report.has_value());
    CHECK(report.error().problem == CfdContinuityProblem::GeometryDescribesAnotherTopology);
    CHECK(report.error().error.code == ErrorCode::InvalidArgument);
    CHECK_THAT(report.error().error.message,
               Catch::Matchers::ContainsSubstring("different mesh generation"));
}

TEST_CASE("CfdContinuity_RefusesABoundaryWithTheWrongFaceCount", "[cfd][cont]") {
    const Hand one = Hand::make(orthogonalPoints(), kTwoCells);
    const Hand other = Hand::make(chainPoints(4), chainTets(4));
    REQUIRE(one.topology.boundaryFaces() != other.topology.boundaryFaces());
    const PreparedCfdBoundary foreign = other.walls();
    const std::vector<double> phi = zeroFlux(one.topology);

    const CfdContinuityInput input{phi};
    std::expected<CfdContinuityReport, CfdContinuityFailure> report =
        cfd::evaluateCfdContinuity(one.topology, one.geometry, foreign, input);
    REQUIRE_FALSE(report.has_value());
    CHECK(report.error().problem == CfdContinuityProblem::BoundaryDescribesAnotherTopology);
    CHECK_THAT(report.error().error.message, Catch::Matchers::ContainsSubstring("boundary faces"));
}

TEST_CASE("CfdContinuity_RefusesANonFiniteSuppliedFlux", "[cfd][cont]") {
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();
    const std::vector<double> phi = withLeadingNan(zeroFlux(hand.topology));

    const CfdContinuityFailure failure = refusal(hand, boundary, phi);
    CHECK(failure.problem == CfdContinuityProblem::ValueNotFinite);
    CHECK(failure.error.code == ErrorCode::InvalidArgument);
    CHECK_THAT(failure.error.message, Catch::Matchers::ContainsSubstring("not finite at face 0"));

    // An infinity is the same fault.
    std::vector<double> infinite = zeroFlux(hand.topology);
    infinite[0] = std::numeric_limits<double>::infinity();
    CHECK(refusal(hand, boundary, infinite).problem == CfdContinuityProblem::ValueNotFinite);
}

TEST_CASE("CfdContinuity_TheDeclaredPhaseOrderIsTheCheckOrder", "[cfd][cont]") {
    // THE ORDER BETWEEN PHASES, asserted by giving an input two faults in two
    // different phases and requiring the EARLIER phase to be reported. Within a
    // phase no order is claimed.
    const Hand one = Hand::make(orthogonalPoints(), kTwoCells);
    const Hand other = Hand::make(chainPoints(4), chainTets(4));
    const PreparedCfdBoundary foreign = other.walls();

    // geometry (phase 1) beats boundary (phase 2): both wrong at once.
    {
        const std::vector<double> phi = zeroFlux(one.topology);
        const CfdContinuityInput input{phi};
        const std::optional<CfdContinuityProblem> problem =
            cfd::cfdContinuityProblem(one.topology, other.geometry, foreign, input);
        REQUIRE(problem.has_value());
        CHECK(*problem == CfdContinuityProblem::GeometryDescribesAnotherTopology);
    }
    // boundary (phase 2) beats length (phase 5).
    {
        const std::vector<double> phi = shortened(zeroFlux(one.topology));
        const CfdContinuityInput input{phi};
        const std::optional<CfdContinuityProblem> problem =
            cfd::cfdContinuityProblem(one.topology, one.geometry, foreign, input);
        REQUIRE(problem.has_value());
        CHECK(*problem == CfdContinuityProblem::BoundaryDescribesAnotherTopology);
    }
    // length (phase 5) beats finiteness (phase 6): short AND with a NaN.
    {
        const std::vector<double> phi = withLeadingNan(shortened(zeroFlux(one.topology)));
        const CfdContinuityInput input{phi};
        const PreparedCfdBoundary boundary = one.walls();
        const std::optional<CfdContinuityProblem> problem =
            cfd::cfdContinuityProblem(one.topology, one.geometry, boundary, input);
        REQUIRE(problem.has_value());
        CHECK(*problem == CfdContinuityProblem::ValueLengthWrong);
    }
}

TEST_CASE("CfdContinuity_ThePredictorsAgreeWithTheEvaluationOnEveryReachablePhase", "[cfd][cont]") {
    const Hand one = Hand::make(orthogonalPoints(), kTwoCells);
    const Hand other = Hand::make(chainPoints(4), chainTets(4));
    const PreparedCfdBoundary boundary = one.walls();
    const PreparedCfdBoundary foreign = other.walls();
    const std::vector<double> good = zeroFlux(one.topology);
    const std::vector<double> shortFlux = shortened(good);
    const std::vector<double> nanFlux = withLeadingNan(good);

    struct Case {
        std::string_view label;
        const CfdGeometry* geometry;
        const PreparedCfdBoundary* boundary;
        std::span<const double> phi;
        std::optional<CfdContinuityProblem> expected;
    };
    const std::array<Case, 4> cases{
        Case{"good", &one.geometry, &boundary, good, std::nullopt},
        Case{"foreign geometry", &other.geometry, &boundary, good,
             CfdContinuityProblem::GeometryDescribesAnotherTopology},
        Case{"foreign boundary", &one.geometry, &foreign, good,
             CfdContinuityProblem::BoundaryDescribesAnotherTopology},
        Case{"short flux", &one.geometry, &boundary, shortFlux,
             CfdContinuityProblem::ValueLengthWrong},
    };

    for (const Case& c : cases) {
        INFO(c.label);
        const CfdContinuityInput input{c.phi};
        const std::optional<CfdContinuityProblem> problem =
            cfd::cfdContinuityProblem(one.topology, *c.geometry, *c.boundary, input);
        CHECK(problem == c.expected);

        const std::optional<CfdContinuityFailure> failure =
            cfd::cfdContinuityFailure(one.topology, *c.geometry, *c.boundary, input);
        CHECK(failure.has_value() == c.expected.has_value());
        if (failure.has_value()) {
            CHECK(failure->problem == *c.expected);
            CHECK_FALSE(failure->error.message.empty());
        }

        std::expected<CfdContinuityReport, CfdContinuityFailure> report =
            cfd::evaluateCfdContinuity(one.topology, *c.geometry, *c.boundary, input);
        CHECK(report.has_value() == !c.expected.has_value());
        if (!report.has_value()) {
            CHECK(report.error().problem == *c.expected);
        }
    }

    // The NaN case separately, because it must be reported identically by all
    // three doors too.
    const CfdContinuityInput nanInput{nanFlux};
    CHECK(cfd::cfdContinuityProblem(one.topology, one.geometry, boundary, nanInput) ==
          CfdContinuityProblem::ValueNotFinite);
}

TEST_CASE("CfdContinuity_EveryProblemHasADistinctName", "[cfd][cont]") {
    std::vector<std::string_view> names;
    for (const CfdContinuityProblem problem : cfd::kCfdContinuityProblems) {
        const std::string_view name = cfd::toString(problem);
        CHECK_FALSE(name.empty());
        CHECK(name != "unknown continuity problem");
        names.push_back(name);
    }
    REQUIRE(names.size() == 8);
    std::ranges::sort(names);
    CHECK(std::ranges::adjacent_find(names) == names.end());
}


TEST_CASE("CfdContinuity_RefusesANonFiniteResultThroughTheDivergencesGate", "[cfd][cont]") {
    // THE PHASE THAT JUDGES THE ANSWER, reached by the route it actually has.
    //
    // THIS TEST WAS WRITTEN TO PROVE SOMETHING ELSE AND FAILED, WHICH IS WHY IT
    // IS WORTH READING. The first version built a boundary field whose per-cell
    // residuals stay finite while the same-sign `inflow`/`outflow` accumulation
    // overflows, on the theory that only this evaluator's own finiteness loop
    // could catch it -- the shape of P19-MOM-001's surviving probe M19. The
    // premise was false: `divergeCfdMassFlux` gates its own accumulated
    // magnitude `sum_f |flux_f|` as well as its two totals, and refused the
    // input with
    //
    //     "the mass flow rate divergence totals are not finite: boundary inf,
    //      global inf, accumulated magnitude inf"
    //
    // while every per-cell residual was indeed finite. Since `inflow`,
    // `outflow`, `throughput` and every patch subtotal are all bounded by that
    // magnitude, no input can overflow them without the sub-operator refusing
    // first. The header's justification for the loop was corrected to say so,
    // and the loop is recorded as an unreachable defence rather than kept with
    // a false story attached.
    //
    // What IS reachable, and is asserted here, is the refusal itself: a huge but
    // finite `phi` produces a non-finite divergence, and this evaluator reports
    // it under ITS OWN TAG rather than letting a foreign enumerator escape --
    // which matters because `divergeCfdMassFlux` returns an UNTAGGED `Error`.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.outlets();
    const double huge = 1.7e308;
    REQUIRE(std::isfinite(huge));
    REQUIRE_FALSE(std::isfinite(huge + huge));

    const std::vector<double> phi = boundaryFlux(hand.topology, huge);
    // (i) EVERY SUPPLIED VALUE IS FINITE, so phase 6 does not fire and the
    //     refusal cannot be a disguised input check.
    for (const double value : phi) {
        REQUIRE(std::isfinite(value));
    }
    const CfdContinuityInput input{phi};
    CHECK_FALSE(
        cfd::cfdContinuityProblem(hand.topology, hand.geometry, boundary, input).has_value());

    // (ii) THE DIVERGENCE REFUSES IT, which is the route, asserted rather than
    //      assumed so that a later change making the divergence tolerant would
    //      fail here instead of silently changing which gate fires.
    const CfdMassFluxDivergenceInput divergenceInput{phi};
    Result<CfdMassFluxDivergenceField> independent =
        cfd::divergeCfdMassFlux(hand.topology, hand.geometry, divergenceInput);
    REQUIRE_FALSE(independent.has_value());

    // (iii) and the evaluator reports it under its own tag, naming the cause.
    const CfdContinuityFailure failure = refusal(hand, boundary, phi);
    CHECK(failure.problem == CfdContinuityProblem::ResultIsNotFinite);
    CHECK(failure.error.code == ErrorCode::InvalidArgument);
    CHECK_THAT(failure.error.message, Catch::Matchers::ContainsSubstring("not finite"));
    CHECK_THAT(failure.error.message, Catch::Matchers::ContainsSubstring("divergence"));
}

TEST_CASE("CfdContinuity_EveryQuantityIsBoundedByTheMagnitudeTheDivergenceGates", "[cfd][cont]") {
    // THE INEQUALITIES THAT MAKE THE FINITENESS LOOP UNREACHABLE, asserted so
    // the header's claim is checked rather than argued. If a later change made
    // one of these false, the loop would become reachable and this test would
    // say so.
    const Hand hand = Hand::split(chainPoints(4), chainTets(4), 3);
    const PreparedCfdBoundary boundary = hand.inletAndOutlet();
    std::vector<double> phi(hand.topology.faces(), 0.0);
    for (std::size_t b = 0; b < hand.topology.boundaryFaces(); ++b) {
        const FaceIndex face = hand.topology.faceOf(BoundaryFaceIndex::fromValue(b));
        const double scale = std::pow(10.0, static_cast<double>(b % 11) - 5.0);
        phi[face.value()] = (b % 3 == 0 ? -1.0 : 1.0) * kDiscriminatingConstant * scale;
    }
    const CfdContinuityReport report = evaluate(hand, boundary, phi);

    // The scale the divergence gates: `sum_f |flux_f|` over ALL faces.
    double allFaces = 0.0;
    for (const double value : phi) {
        allFaces += std::abs(value);
    }
    const double onBoundary = boundaryMagnitude(hand.topology, phi);
    REQUIRE(onBoundary > 0.0);
    CHECK(onBoundary <= allFaces);

    CHECK(report.inflow().si() <= onBoundary);
    CHECK(report.outflow().si() <= onBoundary);
    CHECK(std::abs(report.netBoundaryFlux().si()) <= onBoundary);
    CHECK_THAT(report.throughput().si(), Catch::Matchers::WithinRel(0.5 * onBoundary, 1.0e-14));

    double patchMagnitude = 0.0;
    for (const CfdPatchMassFlow& patch : report.patches()) {
        CHECK(patch.inflow.si() <= onBoundary);
        CHECK(patch.outflow.si() <= onBoundary);
        CHECK(std::abs(patch.net.si()) <= onBoundary);
        patchMagnitude += patch.inflow.si() + patch.outflow.si();
    }
    CHECK_THAT(patchMagnitude, Catch::Matchers::WithinRel(onBoundary, 1.0e-13));

    // And the largest cell residual is a maximum over values the divergence
    // itself produced and gated.
    CHECK(report.largestCellResidual().si() == report.divergence().largestMagnitude().si());
}

TEST_CASE("CfdContinuity_TheUnreachableProblemsAreRecordedAsUnreached", "[cfd][cont]") {
    // RECORDED RATHER THAN CLAIMED COVERED, which is what `CfdDivergence.hpp`
    // and `CfdGradient.hpp` both do with their own unreachable refusals.
    //
    // ```text
    // TopologyNamesAnUnknownCell    `buildCfdTopology` validates every owner and
    //                               neighbour, so no CONSTRUCTIBLE topology
    //                               names a cell it does not hold.  The guard
    //                               exists because `divergeCfdMassFlux` indexes
    //                               by owner and neighbour and its own gate has
    //                               NO enumerator for this -- and because
    //                               P19-DIV-001's review found that unguarded
    //                               index reaching WRITES, and P19-CONV-001's
    //                               review found the correction REGRESSED one
    //                               milestone later
    // TopologyNamesAnUnknownPatch   the same, for `patchOf`, which THIS
    //                               evaluator subscripts an array by
    // EvaluationIsInconsistent      unreachable while `divergeCfdMassFlux`
    //                               keeps its present gate: phases 1, 5 and 6
    //                               cover the three problems its mass-flux path
    //                               can report, so the predictor must agree
    // ```
    //
    // The FINITENESS loop inside `ResultIsNotFinite` is unreachable too, for the
    // reason the preceding test measures; the TAG is reachable through the
    // divergence's door and is tested there.
    CHECK(cfd::toString(CfdContinuityProblem::TopologyNamesAnUnknownCell) ==
          "topology names an unknown cell");
    CHECK(cfd::toString(CfdContinuityProblem::TopologyNamesAnUnknownPatch) ==
          "topology names an unknown patch");
    CHECK(cfd::toString(CfdContinuityProblem::EvaluationIsInconsistent) ==
          "evaluation is inconsistent");

    // AND A WELL-FORMED TOPOLOGY PASSES BOTH RANGE PHASES.
    //
    // WHAT THIS BLOCK IS AND IS NOT, because a review pointed out that an
    // earlier comment oversold it: it re-walks `owner`, `neighbour` and
    // `patchOf` itself and never calls `evaluateCfdContinuity`, so it is a
    // statement about `buildCfdTopology`'s output and NOT about this
    // milestone's gate. It is kept because it establishes the PREMISE that the
    // two guards are unreachable from a constructible topology -- which is what
    // L9 claims -- and it is labelled rather than counted as coverage of the
    // guards themselves. Its loops also dominate this suite's assertion count,
    // which is worth knowing when reading that number.
    const Hand hand = Hand::split(orthogonalPoints(), kTwoCells, 3);
    std::size_t facesWalked = 0;
    for (std::size_t f = 0; f < hand.topology.faces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        const CellIndex owner = hand.topology.owner(face);
        REQUIRE(owner.isValid());
        REQUIRE(owner.value() < hand.topology.cells());
        const std::optional<CellIndex> neighbour = hand.topology.neighbour(face);
        if (neighbour.has_value()) {
            REQUIRE(neighbour->isValid());
            REQUIRE(neighbour->value() < hand.topology.cells());
        }
        ++facesWalked;
    }
    REQUIRE(facesWalked == hand.topology.faces());
    std::size_t patchesWalked = 0;
    for (std::size_t b = 0; b < hand.topology.boundaryFaces(); ++b) {
        const PatchIndex patch = hand.topology.patchOf(BoundaryFaceIndex::fromValue(b));
        REQUIRE(patch.isValid());
        REQUIRE(patch.value() < hand.topology.patches());
        ++patchesWalked;
    }
    REQUIRE(patchesWalked == hand.topology.boundaryFaces());
}

// ---------------------------------------------------------------------------
// Independent validation: a hand-computed small system
// ---------------------------------------------------------------------------

TEST_CASE("CfdContinuity_AHandComputedOneCellSystemMatchesInClosedForm", "[cfd][cont]") {
    // INDEPENDENT VALIDATION, AND EXACT. Every expected figure below is hand
    // arithmetic over values chosen to be EXACTLY REPRESENTABLE in binary, so
    // the oracle carries no rounding of its own and the comparison is `==`
    // rather than a tolerance. Nothing here is derived from the implementation:
    // the fluxes are chosen, the sums are done by hand, and the two measures are
    // formed from ADR-058's definitions directly.
    //
    //     phi over the four boundary faces   8, -2, 4, -2      all exact
    //     outflow   = 8 + 4                  = 12
    //     inflow    = 2 + 2                  = 4
    //     net       = 8 - 2 + 4 - 2          = 8
    //     sum |phi| = 8 + 2 + 4 + 2          = 16
    //     throughput= 0.5 * 16               = 8
    //     globalImbalance   = |8| / 8        = 1.0   EXACTLY
    //     cell residual     = 8              (one cell owns all four faces)
    //     maxCellContinuity = |8| / 8        = 1.0   EXACTLY
    //
    // THE VALUES ARE ORDER-INDEPENDENT, which matters because the face order is
    // the topology's and not this test's: every partial sum of {8, -2, 4, -2}
    // and of their magnitudes is a small integer, so any traversal order gives
    // the same double. The oracle therefore does not smuggle in an assumption
    // about ADR-064's numbering.
    //
    // 1.0 RATHER THAN A PRETTIER RATIO IS DELIBERATE. An earlier draft used
    // {8, -2, 4, -1}, whose imbalance is 9/7.5 = 1.2 -- and 1.2 is NOT
    // representable, so the expected value would have had to be written as
    // `9.0 / 7.5` and the test would have been comparing the code's division
    // against the same division. Choosing a ratio that is a power of two keeps
    // the oracle independent.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const PreparedCfdBoundary boundary = hand.outlets();
    REQUIRE(hand.topology.cells() == 1);
    REQUIRE(hand.topology.boundaryFaces() == 4);
    REQUIRE(hand.topology.internalFaces() == 0);
    REQUIRE(hand.topology.patches() == 1);

    const std::array<double, 4> chosen{8.0, -2.0, 4.0, -2.0};
    std::vector<double> phi(hand.topology.faces(), 0.0);
    for (std::size_t b = 0; b < 4; ++b) {
        const FaceIndex face = hand.topology.faceOf(BoundaryFaceIndex::fromValue(b));
        phi[face.value()] = chosen[b];
    }

    const CfdContinuityReport report = evaluate(hand, boundary, phi);

    CHECK(report.outflow().si() == 12.0);
    CHECK(report.inflow().si() == 4.0);
    CHECK(report.netBoundaryFlux().si() == 8.0);
    CHECK(report.throughput().si() == 8.0);

    CHECK(report.summary().outflowFaces == 2);
    CHECK(report.summary().inflowFaces == 2);
    CHECK(report.summary().zeroFluxFaces == 0);

    const std::optional<MassFlowRate> residual = report.cellResidualAt(CellIndex::fromValue(0));
    REQUIRE(residual.has_value());
    CHECK(residual->si() == 8.0);
    CHECK(report.largestCellResidual().si() == 8.0);
    REQUIRE(report.worstCell().has_value());
    CHECK(report.worstCell()->value() == 0);

    REQUIRE(report.massBalance().has_value());
    CHECK(report.massBalance()->globalImbalance == 1.0);
    CHECK(report.massBalance()->maxCellContinuity == 1.0);

    // AND EVERY REPORTED DEPARTURE IS EXACTLY ZERO, because with these values
    // the two reconstructions agree bit-for-bit: (12 - 4) == 8 exactly, the one
    // patch's net IS the total, and with a single cell the global sum IS the
    // boundary sum.
    CHECK(report.summary().directionalSplitResidual == 0.0);
    CHECK(report.summary().patchSumResidual == 0.0);
    CHECK(report.summary().accumulationResidual == 0.0);

    // The one patch carries the whole thing, in closed form too.
    REQUIRE(report.patches().size() == 1);
    CHECK(report.patches()[0].faces == 4);
    CHECK(report.patches()[0].inflow.si() == 4.0);
    CHECK(report.patches()[0].outflow.si() == 12.0);
    CHECK(report.patches()[0].net.si() == 8.0);
    CHECK(report.patches()[0].kind == CfdBoundaryConditionKind::PressureOutlet);
}

TEST_CASE("CfdContinuity_AHandBalancedTwoCellSystemConservesExactly", "[cfd][cont]") {
    // THE SAME ORACLE WITH AN INTERNAL FACE, so internal-face cancellation is
    // part of the closed-form check rather than only of the aggregate measure.
    //
    // Each cell is given boundary fluxes that exactly cancel its own share of
    // the internal face, using powers of two so every partial sum is exact:
    //
    //     internal face       +16  (out of the owner, into the neighbour)
    //     owner's boundary    three faces summing to -16
    //     neighbour's         three faces summing to +16
    //
    //     owner residual      +16 - 16 = 0   EXACTLY
    //     neighbour residual  -16 + 16 = 0   EXACTLY
    //     net boundary        -16 + 16 = 0   EXACTLY
    //     sum |phi| boundary  32,  throughput 16
    //     globalImbalance     |0| / 16 = 0   EXACTLY
    //     maxCellContinuity   |0| / 16 = 0   EXACTLY
    //
    // WHICH BOUNDARY FACE BELONGS TO WHICH CELL IS ASKED OF THE TOPOLOGY, not
    // assumed, because the fixture's face numbering is ADR-064's and not this
    // test's to predict.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.outlets();
    REQUIRE(hand.topology.cells() == 2);
    REQUIRE(hand.topology.internalFaces() == 1);
    REQUIRE(hand.topology.boundaryFaces() == 6);

    const FaceIndex internal = FaceIndex::fromValue(0);
    REQUIRE(hand.topology.isInternal(internal));
    const CellIndex owner = hand.topology.owner(internal);
    const std::optional<CellIndex> neighbour = hand.topology.neighbour(internal);
    REQUIRE(neighbour.has_value());
    REQUIRE(owner.value() != neighbour->value());

    std::vector<double> phi(hand.topology.faces(), 0.0);
    phi[internal.value()] = 16.0;

    // Each cell's three boundary faces get -8, -4, -4 or +8, +4, +4 so each
    // triple sums to exactly -16 or +16.
    const std::array<double, 3> magnitudes{8.0, 4.0, 4.0};
    std::size_t ownerSeen = 0;
    std::size_t neighbourSeen = 0;
    for (std::size_t b = 0; b < hand.topology.boundaryFaces(); ++b) {
        const FaceIndex face = hand.topology.faceOf(BoundaryFaceIndex::fromValue(b));
        const CellIndex cell = hand.topology.owner(face);
        if (cell.value() == owner.value()) {
            REQUIRE(ownerSeen < 3);
            phi[face.value()] = -magnitudes[ownerSeen];
            ++ownerSeen;
        } else {
            REQUIRE(cell.value() == neighbour->value());
            REQUIRE(neighbourSeen < 3);
            phi[face.value()] = magnitudes[neighbourSeen];
            ++neighbourSeen;
        }
    }
    REQUIRE(ownerSeen == 3);
    REQUIRE(neighbourSeen == 3);

    const CfdContinuityReport report = evaluate(hand, boundary, phi);

    // EXACT CONSERVATION, cell by cell.
    const std::optional<MassFlowRate> ownerResidual = report.cellResidualAt(owner);
    const std::optional<MassFlowRate> neighbourResidual = report.cellResidualAt(*neighbour);
    REQUIRE(ownerResidual.has_value());
    REQUIRE(neighbourResidual.has_value());
    CHECK(ownerResidual->si() == 0.0);
    CHECK(neighbourResidual->si() == 0.0);
    CHECK(report.largestCellResidual().si() == 0.0);

    // AND AT THE BOUNDARY.
    CHECK(report.inflow().si() == 16.0);
    CHECK(report.outflow().si() == 16.0);
    CHECK(report.netBoundaryFlux().si() == 0.0);
    CHECK(report.throughput().si() == 16.0);

    REQUIRE(report.massBalance().has_value());
    CHECK(report.massBalance()->globalImbalance == 0.0);
    CHECK(report.massBalance()->maxCellContinuity == 0.0);

    CHECK(report.summary().directionalSplitResidual == 0.0);
    CHECK(report.summary().patchSumResidual == 0.0);
    CHECK(report.summary().accumulationResidual == 0.0);

    // THE INTERNAL FACE CARRIED 16 EACH WAY AND THE BOUNDARY NEVER SAW IT,
    // which is the closed-form statement of internal-face cancellation: the
    // boundary net is zero while a flux of 16 crossed the mesh internally.
    CHECK(phi[internal.value()] == 16.0);
    CHECK(report.summary().internalFaces == 1);
}

TEST_CASE("CfdContinuity_AUniformFieldOnAnAnalyticTetIsDivergenceFreeByTheDivergenceTheorem",
          "[cfd][cont]") {
    // THE ANALYTIC CHECK, from the divergence theorem rather than from hand
    // arithmetic: for a UNIFORM velocity `U`,
    //
    //     sum_f phi_f = rho * U . (sum_f S_f) = 0
    //
    // because a closed cell's outward area vectors sum to zero. So the residual
    // of a uniform field is the mesh closure and nothing else, and the field is
    // divergence-free for a reason external to this code.
    //
    // THE CLOSURE IS ASSERTED TO BE SMALL FIRST, so the test cannot pass by
    // measuring a mesh whose faces do not close. And the reference tetrahedron
    // is the orthogonal one, whose four outward area vectors are
    //
    //     (-a^2/2, 0, 0)   (0, -a^2/2, 0)   (0, 0, -a^2/2)   (a^2/2)(1, 1, 1)
    //
    // which sum to exactly zero in real arithmetic -- a fact about the shape,
    // computed by hand here and not read from the geometry module.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const PreparedCfdBoundary boundary = hand.outlets();
    const double a = 0.01; // 10 mm, from `oneCellPoints`
    const double half = 0.5 * a * a;
    const std::array<std::array<double, 3>, 4> handAreas{
        {{-half, 0.0, 0.0}, {0.0, -half, 0.0}, {0.0, 0.0, -half}, {half, half, half}}};
    for (std::size_t k = 0; k < 3; ++k) {
        double sum = 0.0;
        for (const auto& s : handAreas) {
            sum += s[k];
        }
        INFO("hand-computed closure component " << k);
        CHECK(sum == 0.0);
    }

    // The producer's `phi` for a uniform velocity, through a boundary where
    // `phi` is `Computed` so the interior value is used.
    const double rho = 998.2;
    const std::array<double, 3> u{1.5, -0.75, 2.25};
    const std::vector<double> phi =
        producedFlux(hand, boundary, Velocity3{Velocity::fromSi(u[0]), Velocity::fromSi(u[1]),
                                               Velocity::fromSi(u[2])},
                     Density::fromSi(rho));

    // THE HAND-COMPUTED EXPECTATION, as a SET, because the face order is
    // ADR-064's. Each produced `phi` must equal `rho * (U . S_f)` for one of the
    // four hand-computed area vectors, to the tolerance a product of three
    // doubles carries.
    std::vector<double> expected;
    for (const auto& s : handAreas) {
        expected.push_back(rho * (u[0] * s[0] + u[1] * s[1] + u[2] * s[2]));
    }
    std::vector<double> produced;
    for (std::size_t b = 0; b < hand.topology.boundaryFaces(); ++b) {
        produced.push_back(phi[hand.topology.faceOf(BoundaryFaceIndex::fromValue(b)).value()]);
    }
    REQUIRE(produced.size() == expected.size());
    std::ranges::sort(expected);
    std::ranges::sort(produced);
    double scale = 0.0;
    for (const double value : expected) {
        scale += std::abs(value);
    }
    REQUIRE(scale > 0.0);
    for (std::size_t k = 0; k < expected.size(); ++k) {
        INFO("face " << k << " expected " << expected[k] << " produced " << produced[k]);
        CHECK_THAT(produced[k], Catch::Matchers::WithinAbs(expected[k], 1.0e-13 * scale));
    }

    // AND THE RESIDUAL IS THE CLOSURE, measured against the floor ADR-074's Q1
    // established rather than against zero.
    const CfdContinuityReport report = evaluate(hand, boundary, phi);
    REQUIRE(report.massBalance().has_value());
    INFO("maxCellContinuity = " << report.massBalance()->maxCellContinuity);
    CHECK(report.massBalance()->maxCellContinuity < cfd::kCfdContinuityCellTolerance);
    CHECK(report.massBalance()->globalImbalance < cfd::kCfdContinuityCellTolerance);
}

// ---------------------------------------------------------------------------
// Closing the measured mutation survivors
//
// Every test in this section exists because an adversarial review APPLIED a
// single-line mutation to `src/cfd/CfdContinuity.cpp`, rebuilt, re-ran the
// suite, and found it green. The mutation is quoted in each case, so a reader
// can see what the test is for rather than inferring it.
// ---------------------------------------------------------------------------

TEST_CASE("CfdContinuity_AMeshWithWallsAndAnOutletIsNotAClosedDomain", "[cfd][cont]") {
    // KILLS:  report.summary_.closedDomain = report.summary_.openFaces == 0;
    //      -> report.summary_.closedDomain = report.summary_.wallFaces > 0;
    //
    // EVERY OTHER FIXTURE IN THIS SUITE IS UNIFORM IN THE ONE DIMENSION
    // `closedDomain` IS ABOUT. `walls()`, `slips()`, `movingWallOnOneFacet()`
    // and `wallEverywhere()` are all wall; `outlets()`, `inlets()`,
    // `inletAndOutlet()` and `outletEverywhere()` are all open. The two mixed
    // fixtures mix two WALL kinds and two OPEN kinds respectively, so neither
    // crosses the boundary. The result was that the suite never evaluated the
    // ordinary CFD arrangement -- walls plus an opening -- and a `closedDomain`
    // derived from `wallFaces > 0` would have reported EVERY real analysis as
    // closed, inviting a reader to expect a bit-zero boundary flux on a mesh
    // with an outlet in it.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary =
        hand.mixed(CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1),
                                        NoSlipWall{someFace(1)}},
                   CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(2),
                                        PressureOutlet{someFace(2), 0_Pa}});

    // THE PREMISE: the boundary really does carry both, asserted off the
    // prepared boundary so a binder that substituted one kind would fail here
    // rather than make the test pass quietly.
    std::size_t walls = 0;
    std::size_t opens = 0;
    for (std::size_t b = 0; b < hand.topology.boundaryFaces(); ++b) {
        const std::optional<cfd::CfdBoundaryFace> face =
            boundary.face(BoundaryFaceIndex::fromValue(b));
        REQUIRE(face.has_value());
        if (face->kind == CfdBoundaryConditionKind::NoSlipWall) {
            ++walls;
        } else if (face->kind == CfdBoundaryConditionKind::PressureOutlet) {
            ++opens;
        }
    }
    REQUIRE(walls > 0);
    REQUIRE(opens > 0);
    REQUIRE(walls + opens == hand.topology.boundaryFaces());

    const std::vector<double> phi = boundaryFlux(hand.topology, kDiscriminatingConstant);
    const CfdContinuityReport report = evaluate(hand, boundary, phi);

    CHECK_FALSE(report.summary().closedDomain);
    CHECK(report.summary().wallFaces == walls);
    CHECK(report.summary().openFaces == opens);
    CHECK(report.summary().wallFaces > 0);  // so `wallFaces > 0` cannot stand in
    CHECK(report.summary().openFaces > 0);  // for `openFaces == 0`
    CHECK(report.summary().wallFaces + report.summary().openFaces ==
          hand.topology.boundaryFaces());
}

TEST_CASE("CfdContinuity_EachPatchRecordDescribesItsOwnFaces", "[cfd][cont]") {
    // KILLS:  report.patches_[patch.value()]
    //      -> report.patches_[(patch.value() + 1) % patchCount]
    //
    // A ROTATION OF THE WHOLE TABLE PASSED EVERYTHING. The record's `patch`
    // LABEL is written in one loop and its CONTENTS in another, and the only
    // assertion tying a record to an index checked the label -- which the
    // rotation does not touch. Every other patch assertion was an aggregate:
    // the totals reconstruct, the per-record counts sum to `faces`, the kinds
    // seen are the right set. All of those are rotation-invariant, and
    // round-robin `splitPatches` even equalises the face counts so cardinality
    // does not discriminate either.
    //
    // THIS TEST RE-DERIVES EACH PATCH'S CONTENTS FROM THE TOPOLOGY AND THE
    // BOUNDARY, independently of the report, and requires the record at index
    // `p` to describe patch `p`.
    const Hand hand = Hand::split(orthogonalPoints(), kTwoCells, 2);
    const PreparedCfdBoundary boundary = hand.inletAndOutlet();
    REQUIRE(hand.topology.patches() == 2);

    // Asymmetric magnitudes AND asymmetric signs, so no two patches can carry
    // the same numbers: a rotation must then be visible in the values.
    std::vector<double> phi(hand.topology.faces(), 0.0);
    for (std::size_t b = 0; b < hand.topology.boundaryFaces(); ++b) {
        const FaceIndex face = hand.topology.faceOf(BoundaryFaceIndex::fromValue(b));
        const PatchIndex patch = hand.topology.patchOf(BoundaryFaceIndex::fromValue(b));
        const double sign = patch.value() == 0 ? -1.0 : 1.0;
        const double scale = 1.0 + static_cast<double>(patch.value()) * 7.0 +
                             static_cast<double>(b) * 0.25;
        phi[face.value()] = sign * kDiscriminatingConstant * scale;
    }

    const CfdContinuityReport report = evaluate(hand, boundary, phi);
    REQUIRE(report.patches().size() == hand.topology.patches());

    // The independent expectation, built from the topology and the boundary.
    struct Expected {
        std::size_t faces = 0;
        std::size_t inflowFaces = 0;
        std::size_t outflowFaces = 0;
        double inflow = 0.0;
        double outflow = 0.0;
        double net = 0.0;
        std::optional<CfdBoundaryConditionKind> kind{};
        bool seen = false;
        bool mixed = false;
    };
    std::vector<Expected> expected(hand.topology.patches());
    for (std::size_t b = 0; b < hand.topology.boundaryFaces(); ++b) {
        const BoundaryFaceIndex bf = BoundaryFaceIndex::fromValue(b);
        const PatchIndex patch = hand.topology.patchOf(bf);
        Expected& e = expected[patch.value()];
        const double value = phi[hand.topology.faceOf(bf).value()];
        e.faces += 1;
        if (value > 0.0) {
            e.outflowFaces += 1;
            e.outflow += value;
        } else if (value < 0.0) {
            e.inflowFaces += 1;
            e.inflow += -value;
        }
        e.net += value;
        const std::optional<cfd::CfdBoundaryFace> face = boundary.face(bf);
        REQUIRE(face.has_value());
        if (!e.seen) {
            e.seen = true;
            e.kind = face->kind;
        } else if (e.kind.has_value() && *e.kind != face->kind) {
            e.mixed = true;
            e.kind.reset();
        }
    }

    // THE PREMISE: the two patches must be distinguishable, or a rotation would
    // be undetectable however the assertions are written.
    REQUIRE(expected.size() == 2);
    REQUIRE(expected[0].net != expected[1].net);
    REQUIRE(expected[0].kind != expected[1].kind);

    for (std::size_t p = 0; p < report.patches().size(); ++p) {
        INFO("patch " << p);
        const CfdPatchMassFlow& record = report.patches()[p];
        CHECK(record.patch.value() == p);
        CHECK(record.faces == expected[p].faces);
        CHECK(record.inflowFaces == expected[p].inflowFaces);
        CHECK(record.outflowFaces == expected[p].outflowFaces);
        CHECK(record.inflow.si() == expected[p].inflow);
        CHECK(record.outflow.si() == expected[p].outflow);
        CHECK(record.net.si() == expected[p].net);
        CHECK(record.kind == expected[p].kind);
    }
}

TEST_CASE("CfdContinuity_APatchNamedAnInletCarryingOutflowIsReported", "[cfd][cont]") {
    // REPLACES a test whose loop body never executed. The old version used a
    // ONE-patch fixture with two conditions, so patch 0 was mixed, its `kind`
    // was empty, the `kind == PressureInlet` predicate was never true, and the
    // surviving assertion said that NO inlet patch carried outflow -- the
    // opposite of the claim in its own name. ADR-074 Decision 4's patch-level
    // diagnostic had no test anywhere in the suite.
    //
    // TWO PATCHES, so each carries ONE kind, and `phi` POSITIVE EVERYWHERE, so
    // the facets named `PressureInlet` carry outflow. That is legitimate:
    // ADR-058 makes the rate at a pressure boundary an outcome of the solve,
    // and P19-CONV-001 already reports `backflowFaces`. The juxtaposition is
    // the diagnostic, and nothing is refused for it.
    const Hand hand = Hand::split(orthogonalPoints(), kTwoCells, 2);
    const PreparedCfdBoundary boundary = hand.inletAndOutlet();
    REQUIRE(hand.topology.patches() == 2);

    const std::vector<double> phi = boundaryFlux(hand.topology, kDiscriminatingConstant);
    const CfdContinuityReport report = evaluate(hand, boundary, phi);

    // Nothing is refused, and the whole boundary is outflow.
    CHECK(report.summary().outflowFaces == hand.topology.boundaryFaces());
    CHECK(report.summary().inflowFaces == 0);
    CHECK(report.inflow().si() == 0.0);
    CHECK(report.outflow().si() > 0.0);

    std::size_t inletPatchesCarryingOutflow = 0;
    std::size_t outletPatches = 0;
    for (const CfdPatchMassFlow& patch : report.patches()) {
        INFO("patch " << patch.patch.value());
        REQUIRE(patch.kind.has_value()); // two patches, one condition each
        if (*patch.kind == CfdBoundaryConditionKind::PressureInlet) {
            ++inletPatchesCarryingOutflow;
            CHECK(patch.outflowFaces == patch.faces);
            CHECK(patch.inflowFaces == 0);
            CHECK(patch.outflow.si() > 0.0);
            CHECK(patch.inflow.si() == 0.0);
            CHECK(patch.net.si() > 0.0);
        } else {
            CHECK(*patch.kind == CfdBoundaryConditionKind::PressureOutlet);
            ++outletPatches;
        }
    }
    // THE COUNT IS ASSERTED NON-ZERO, which is what the old version could not
    // do: a patch named an inlet exists, carries outflow, and is reported.
    CHECK(inletPatchesCarryingOutflow == 1);
    CHECK(outletPatches == 1);
}

TEST_CASE("CfdContinuity_RefusesANonFiniteFluxAtAnyFaceAndNotOnlyTheFirst", "[cfd][cont]") {
    // KILLS:  for (std::size_t f = 0; f < input.faceFlux.size(); ++f)
    //      -> for (std::size_t f = 0; f < (input.faceFlux.empty() ? 0u : 1u); ++f)
    //
    // EVERY NON-FINITE FIXTURE IN THE SUITE POISONED INDEX 0. `withLeadingNan`
    // sets the first entry and breaks, and the one infinity test wrote
    // `infinite[0]`. A loop narrowed to the first element alone therefore
    // passed everything, and a NaN at any other face would have flowed into the
    // divergence -- the hole `P19-DIFF-001`'s review found, which this phase's
    // own comment cites as its reason for existing.
    //
    // THE INDEX IS PARAMETERISED over the first face, the first BOUNDARY face
    // and the last face, so an internal face, a boundary face and the end of
    // the array are all covered.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.outlets();
    const std::size_t faces = hand.topology.faces();
    REQUIRE(hand.topology.internalFaces() >= 1);
    REQUIRE(faces >= 3);

    const std::array<std::size_t, 3> indices{0, hand.topology.internalFaces(), faces - 1};
    std::size_t checked = 0;
    for (const std::size_t index : indices) {
        REQUIRE(index < faces);
        for (const double poison : {std::numeric_limits<double>::quiet_NaN(),
                                    std::numeric_limits<double>::infinity(),
                                    -std::numeric_limits<double>::infinity()}) {
            std::vector<double> phi = zeroFlux(hand.topology);
            phi[index] = poison;
            INFO("poisoned face " << index << " of " << faces);
            const CfdContinuityFailure failure = refusal(hand, boundary, phi);
            CHECK(failure.problem == CfdContinuityProblem::ValueNotFinite);
            CHECK_THAT(failure.error.message,
                       Catch::Matchers::ContainsSubstring("at face " + std::to_string(index)));
            ++checked;
        }
    }
    REQUIRE(checked == indices.size() * 3);
}

TEST_CASE("CfdContinuity_TheWorstCellIsNamedAndNotAlwaysCellZero", "[cfd][cont]") {
    // KILLS TWO:
    //   report.worstCell_ = cell;  ->  report.worstCell_ = CellIndex::fromValue(0);
    //   if (!worstCell_.has_value() || magnitude > ...)  ->  if (magnitude > ...)
    //
    // `worstCell()` was asserted only `== 0`, on a one-cell mesh and on a
    // two-cell tie. So "always cell 0" passed, and so did an empty optional on
    // any exactly-conservative mesh -- which contradicts the header's promise
    // that it is empty only for an EMPTY report.
    //
    // A CHAIN WITH A DELIBERATE WORST CELL THAT IS NOT CELL 0.
    const Hand hand = Hand::make(chainPoints(5), chainTets(5));
    const PreparedCfdBoundary boundary = hand.outlets();
    const std::size_t cells = hand.topology.cells();
    REQUIRE(cells >= 3);

    // Give exactly one cell a large boundary flux and the others a small one, by
    // asking the topology which cell owns each boundary face.
    const std::size_t target = cells - 1;
    std::vector<double> phi(hand.topology.faces(), 0.0);
    for (std::size_t b = 0; b < hand.topology.boundaryFaces(); ++b) {
        const FaceIndex face = hand.topology.faceOf(BoundaryFaceIndex::fromValue(b));
        const CellIndex owner = hand.topology.owner(face);
        phi[face.value()] = owner.value() == target ? kDiscriminatingConstant : 1.0e-6;
    }

    const CfdContinuityReport report = evaluate(hand, boundary, phi);

    // THE PREMISE: the intended cell really is the worst, computed from the
    // report's own per-cell residuals rather than assumed from the fixture.
    std::size_t argmax = 0;
    double best = -1.0;
    for (std::size_t c = 0; c < cells; ++c) {
        const std::optional<MassFlowRate> residual =
            report.cellResidualAt(CellIndex::fromValue(c));
        REQUIRE(residual.has_value());
        if (std::abs(residual->si()) > best) {
            best = std::abs(residual->si());
            argmax = c;
        }
    }
    INFO("argmax = " << argmax << " of " << cells);
    REQUIRE(argmax != 0); // otherwise "always cell 0" would still pass

    REQUIRE(report.worstCell().has_value());
    CHECK(report.worstCell()->value() == argmax);
    CHECK(report.largestCellResidual().si() == best);
}

TEST_CASE("CfdContinuity_TheWorstCellIsPresentEvenOnAnExactlyConservativeMesh", "[cfd][cont]") {
    // THE OTHER HALF of the survivor above: dropping the "no cell seen yet" arm
    // leaves `worstCell()` EMPTY whenever every residual is exactly zero,
    // because no magnitude is ever strictly greater than the initial 0.0. The
    // header promises an empty optional only for an EMPTY report.
    //
    // A ZERO FIELD ON A WALL BOUNDARY: every residual is exactly 0.0.
    const Hand hand = Hand::make(orthogonalPoints(), kTwoCells);
    const PreparedCfdBoundary boundary = hand.walls();
    const std::vector<double> phi = zeroFlux(hand.topology);
    const CfdContinuityReport report = evaluate(hand, boundary, phi);

    REQUIRE(report.cells() > 0);
    for (std::size_t c = 0; c < report.cells(); ++c) {
        const std::optional<MassFlowRate> residual =
            report.cellResidualAt(CellIndex::fromValue(c));
        REQUIRE(residual.has_value());
        REQUIRE(residual->si() == 0.0); // the premise: EXACTLY conservative
    }
    CHECK(report.largestCellResidual().si() == 0.0);

    // PRESENT, not absent, because the report is not empty.
    REQUIRE(report.worstCell().has_value());
    CHECK(report.worstCell()->value() == 0); // the first, by the tie-break rule
}

TEST_CASE("CfdContinuity_TheSummaryCarriesTheDivergencesClosureVerbatim", "[cfd][cont]") {
    // KILLS:  summary_.worstClosure = divergenceSummary.worstClosure;
    //      -> summary_.worstClosure = divergenceSummary.worstClosure + 1.0;
    //
    // The member appeared in the suite exactly once, inside an `INFO(...)`, so
    // it was pinned to be STABLE across runs by the determinism test and never
    // pinned to be RIGHT.
    const Hand hand = Hand::split(chainPoints(4), chainTets(4), 3);
    const PreparedCfdBoundary boundary = hand.outlets();
    const std::vector<double> phi = boundaryFlux(hand.topology, kDiscriminatingConstant);
    const CfdContinuityReport report = evaluate(hand, boundary, phi);

    CHECK(report.summary().worstClosure == report.divergence().summary().worstClosure);
    CHECK(report.summary().worstClosure == independentDivergence(hand, phi).summary().worstClosure);
    CHECK(report.summary().worstClosure >= 0.0);
}

TEST_CASE("CfdContinuity_TheRelativeMeasuresSampleTheInteriorOfTheirRange", "[cfd][cont]") {
    // THE `[0, 2]` TEST ONLY EVER SAMPLED THE ENDPOINT. All four of its values
    // were uniform over the boundary, so `|net| == sum|phi| == 2 * throughput`
    // and the measure was exactly 2.0 on every iteration; the `>= 0.0` arm
    // cannot fail. The interior of the claimed range was never reached.
    //
    // MIXED SIGNS AND MIXED MAGNITUDES, with the expected value computed
    // independently from ADR-058's definition.
    const Hand hand = Hand::make(chainPoints(4), chainTets(4));
    const PreparedCfdBoundary boundary = hand.outlets();
    const std::size_t boundaryFaces = hand.topology.boundaryFaces();
    REQUIRE(boundaryFaces >= 4);

    std::size_t interiorSamples = 0;
    for (const std::size_t positives : {1u, 2u, 3u}) {
        REQUIRE(positives < boundaryFaces);
        std::vector<double> phi(hand.topology.faces(), 0.0);
        for (std::size_t b = 0; b < boundaryFaces; ++b) {
            const FaceIndex face = hand.topology.faceOf(BoundaryFaceIndex::fromValue(b));
            phi[face.value()] = (b < positives ? 1.0 : -1.0) * kDiscriminatingConstant;
        }
        const CfdContinuityReport report = evaluate(hand, boundary, phi);
        REQUIRE(report.massBalance().has_value());
        const double measure = report.massBalance()->globalImbalance;

        // Independent expectation: |net| / (0.5 * sum|phi|) with equal
        // magnitudes is |2p - n| / (0.5 * n) where n is the face count.
        const double n = static_cast<double>(boundaryFaces);
        const double p = static_cast<double>(positives);
        const double want = std::abs(2.0 * p - n) / (0.5 * n);
        INFO(positives << " positive of " << boundaryFaces << ": measure " << measure
                       << ", expected " << want);
        CHECK_THAT(measure, Catch::Matchers::WithinRel(want, 1.0e-13));
        CHECK(measure >= 0.0);
        CHECK(measure <= 2.0 + 1.0e-12);
        if (measure > 1.0e-9 && measure < 2.0 - 1.0e-9) {
            ++interiorSamples;
        }
    }
    // AT LEAST ONE SAMPLE STRICTLY INSIDE `(0, 2)`, which is what the old test
    // could not claim.
    INFO("interior samples = " << interiorSamples);
    REQUIRE(interiorSamples > 0);
}

TEST_CASE("CfdContinuity_APatchWithNoBoundaryFacesReadsAllZero", "[cfd][cont]") {
    // THE HEADER PROMISES A RECORD FOR EVERY PATCH THE TOPOLOGY HOLDS,
    // "including patches with no boundary faces -- which read all-zero rather
    // than being absent, so a caller indexing by patch cannot miss". A review
    // pointed out that no fixture in the suite produces an empty patch, so the
    // promise was asserted only by comment.
    //
    // IT IS TESTED RATHER THAN WITHDRAWN, because `CfdTopologyProblem` has no
    // refusal for an empty patch -- `UnpatchedBoundaryFace` is the opposite
    // fault, a boundary face with no patch -- so an empty `MappedFace` is
    // admitted. If a later change made it refusable this test would fail
    // loudly, which is the right outcome either way.
    const meshing::Mesh mesh = buildMesh(orthogonalPoints(), kTwoCells);

    // Three patches declared; every triangle given to patch 0, so patches 1 and
    // 2 hold nothing.
    std::vector<meshing::MappedFace> mapped(3);
    for (std::size_t p = 0; p < mapped.size(); ++p) {
        mapped[p].index = p;
    }
    for (const meshing::Triangle& triangle : mesh.triangles()) {
        mapped[0].facets.push_back(triangle.id);
    }
    REQUIRE_FALSE(mapped[0].facets.empty());
    REQUIRE(mapped[1].facets.empty());
    REQUIRE(mapped[2].facets.empty());

    Result<CfdTopology> topology = cfd::buildCfdTopology(mesh, mapped);
    INFO((topology.has_value() ? std::string{} : topology.error().message));
    REQUIRE(topology.has_value());
    REQUIRE(topology->patches() == 3);
    Result<CfdGeometry> geometry = cfd::buildCfdGeometry(mesh, *topology);
    REQUIRE(geometry.has_value());
    const Hand hand{mesh, std::move(*topology), std::move(*geometry)};

    const PreparedCfdBoundary boundary = hand.outlets();
    const std::vector<double> phi = boundaryFlux(hand.topology, kDiscriminatingConstant);
    const CfdContinuityReport report = evaluate(hand, boundary, phi);

    // EVERY PATCH HAS A RECORD, IN ORDER, AND THE EMPTY ONES ARE ALL-ZERO
    // RATHER THAN ABSENT.
    REQUIRE(report.patches().size() == 3);
    CHECK(report.summary().patches == 3);
    std::size_t empties = 0;
    for (std::size_t p = 0; p < report.patches().size(); ++p) {
        INFO("patch " << p);
        const CfdPatchMassFlow& record = report.patches()[p];
        CHECK(record.patch.value() == p);
        if (record.faces == 0) {
            ++empties;
            CHECK(record.inflowFaces == 0);
            CHECK(record.outflowFaces == 0);
            CHECK(record.zeroFluxFaces == 0);
            CHECK(record.inflow.si() == 0.0);
            CHECK(record.outflow.si() == 0.0);
            CHECK(record.net.si() == 0.0);
            // AND AN EMPTY OPTIONAL, which here means EMPTY and not MIXED --
            // the distinction the header now draws by `faces == 0`.
            CHECK_FALSE(record.kind.has_value());
        }
    }
    CHECK(empties == 2);

    // The non-empty patch carries the whole boundary, so nothing was lost by
    // the empty ones being present.
    CHECK(report.patches()[0].faces == hand.topology.boundaryFaces());
    CHECK(report.patches()[0].outflow.si() == report.outflow().si());
}
// ---------------------------------------------------------------------------
// The reference bodies
// ---------------------------------------------------------------------------


TEST_CASE("CfdContinuity_OnTheReferenceBodiesTheIdentitiesHold", "[cfd][cont][refmod]") {
    // THE REFERENCE MESHES RATHER THAN HAND FIXTURES, because every claim above
    // was made on a tetrahedron or a short chain, and these bodies run from 6 to
    // 808 cells with a closure nobody chose. (An earlier version of this
    // sentence said "thousands of cells, hundreds of boundary faces"; the
    // measured maxima are 808 and 288, and the counts are tabulated below.) Each
    // body is evaluated TWICE -- closed and open -- because the two reach different
    // halves of the report: a closed body has no throughput and deliberately no
    // relative measures, so a reference test using only `wallEverywhere()` would
    // never evaluate ADR-058's two measures on a real mesh at all.
    const std::vector<Meshable> models = meshableModels();
    REQUIRE_FALSE(models.empty());

    std::size_t closedBodies = 0;
    std::size_t openBodies = 0;
    std::size_t largestCells = 0;
    std::size_t largestBoundary = 0;
    for (const Meshable& model : models) {
        INFO("reference model " << model.name);
        const Reference reference{model.kind};
        const std::size_t cells = reference.topology->cells();
        // THE PREMISE IS ASSERTED, NOT DESCRIBED -- and asserting it showed the
        // description was WRONG. A review pointed out that the only guard here
        // was `cells > 0`, so a body meshing to a single cell would satisfy
        // every assertion while the strength this test claims evaporated. The
        // first guard written was `cells > 100`, and `MeshBlock` failed it with
        // SIX. The counts, measured rather than assumed:
        //
        //     MeshBlock               6 cells    12 boundary   6 internal
        //     MeshThinPlate          12          12           18
        //     MeshTransformedBase    12          12           18
        //     MeshTransformedPlaced  12          12           18
        //     MeshPlateWithHole     126         160          172
        //     MeshLocalRefinement   267          12          ...
        //     MeshCylinder          508         140          946
        //     MeshTube              808         288         1472
        //
        // So the comment above this test USED TO CLAIM "thousands of cells,
        // hundreds of boundary faces". The largest body has 808 and 288. The
        // claim is corrected there, and the guards below are the measured
        // minima -- which still bound the premise that matters, that every body
        // is MULTI-cell with interior faces, so a cell residual is not the
        // boundary net and `maxCellContinuity` is not `globalImbalance`.
        REQUIRE(cells >= 6);
        REQUIRE(reference.topology->boundaryFaces() >= 12);
        REQUIRE(reference.topology->internalFaces() >= 6);
        largestCells = std::max(largestCells, cells);
        largestBoundary = std::max(largestBoundary, reference.topology->boundaryFaces());

        std::vector<double> velocity(3 * cells, 0.0);
        for (std::size_t c = 0; c < cells; ++c) {
            velocity[0 * cells + c] = 0.05;
            velocity[1 * cells + c] = -0.03;
            velocity[2 * cells + c] = 0.02;
        }

        // ---- the CLOSED body -------------------------------------------------
        {
            const PreparedCfdBoundary boundary = reference.wallEverywhere();
            const CfdMassFluxInput produced{velocity, kDensity};
            std::expected<std::vector<double>, cfd::CfdConvectionFailure> flux =
                cfd::cfdMassFlux(*reference.topology, *reference.geometry, boundary, produced);
            INFO(model.name << " closed: "
                            << (flux.has_value() ? std::string{} : flux.error().error.message));
            REQUIRE(flux.has_value());

            const CfdContinuityInput input{*flux};
            std::expected<CfdContinuityReport, CfdContinuityFailure> report =
                cfd::evaluateCfdContinuity(*reference.topology, *reference.geometry, boundary,
                                           input);
            INFO(model.name << ": "
                            << (report.has_value() ? std::string{} : report.error().error.message));
            REQUIRE(report.has_value());
            ++closedBodies;

            // (1) THE DECOMPOSITION: every residual is the QUALIFIED
            // divergence's own value, taken independently here.
            const CfdMassFluxDivergenceInput divergenceInput{*flux};
            Result<CfdMassFluxDivergenceField> independent =
                cfd::divergeCfdMassFlux(*reference.topology, *reference.geometry, divergenceInput);
            REQUIRE(independent.has_value());
            std::size_t comparedCells = 0;
            for (std::size_t c = 0; c < cells; ++c) {
                const CellIndex cell = CellIndex::fromValue(c);
                const std::optional<MassFlowRate> mine = report->cellResidualAt(cell);
                const std::optional<MassFlowRate> theirs = independent->at(cell);
                REQUIRE(mine.has_value());
                REQUIRE(theirs.has_value());
                REQUIRE(mine->si() == theirs->si());
                ++comparedCells;
            }
            REQUIRE(comparedCells == cells);
            CHECK(report->netBoundaryFlux().si() == independent->netBoundaryFlux().si());
            CHECK(report->largestCellResidual().si() == independent->largestMagnitude().si());

            // (2) A CLOSED DOMAIN: every facet a wall by the table, and the
            // boundary flux BIT-ZERO rather than merely small.
            CHECK(report->summary().closedDomain);
            CHECK(report->summary().wallFaces == reference.topology->boundaryFaces());
            CHECK(report->summary().openFaces == 0);
            CHECK(report->netBoundaryFlux().si() == 0.0);
            CHECK(report->throughput().si() == 0.0);
            CHECK(report->inflow().si() == 0.0);
            CHECK(report->outflow().si() == 0.0);

            // AND THE RELATIVE MEASURES ARE ABSENT, not zero.
            CHECK_FALSE(report->massBalance().has_value());

            // (3) THE TWO BOUNDARY-ONLY DEPARTURES ARE EXACTLY ZERO, AND THAT
            // IS A STATEMENT ABOUT THE FIXTURE AND NOT ABOUT THE AGGREGATION.
            // Both are built from boundary sums over a boundary that is
            // bit-zero, so there is nothing to accumulate and
            // `relativeDeparture` returns 0.0 by its scale<=0 arm.
            //
            // AN EARLIER VERSION CALLED THIS "stronger than the measured bound".
            // It was not; it was vacuous, and a review said so. The label is
            // corrected rather than the assertion removed, because zero IS the
            // right answer here -- it just is not evidence about the arithmetic.
            CHECK(report->summary().directionalSplitResidual == 0.0);
            CHECK(report->summary().patchSumResidual == 0.0);

            // THE ACCUMULATION RESIDUAL IS NOT VACUOUS ANY MORE, AND THIS
            // ASSERTION IS THE PROOF THAT IT USED TO BE. It is now carried from
            // the divergence, whose denominator is the ALL-FACES magnitude --
            // non-zero here, because the interior faces carry the uniform
            // field. Before the fix this read `== 0.0` and passed on every
            // body; afterwards the same bodies report 1e-17 to 3e-17, so the
            // number measures something it previously could not.
            CHECK(report->summary().accumulationResidual >= 0.0);
            CHECK(report->summary().accumulationResidual <
                  cfd::kCfdContinuityAggregationTolerance);
            CHECK(report->summary().accumulationResidual ==
                  report->divergence().summary().relativeImbalance);
            CHECK(report->summary().worstClosure ==
                  report->divergence().summary().worstClosure);
            CHECK(report->summary().cells == cells);
            CHECK(report->summary().boundaryFaces == reference.topology->boundaryFaces());
            CHECK(report->summary().internalFaces == reference.topology->internalFaces());
        }

        // ---- the OPEN body ---------------------------------------------------
        {
            const PreparedCfdBoundary boundary = reference.outletEverywhere();
            const CfdMassFluxInput produced{velocity, kDensity};
            std::expected<std::vector<double>, cfd::CfdConvectionFailure> flux =
                cfd::cfdMassFlux(*reference.topology, *reference.geometry, boundary, produced);
            INFO(model.name << " open: "
                            << (flux.has_value() ? std::string{} : flux.error().error.message));
            REQUIRE(flux.has_value());

            const CfdContinuityInput input{*flux};
            std::expected<CfdContinuityReport, CfdContinuityFailure> report =
                cfd::evaluateCfdContinuity(*reference.topology, *reference.geometry, boundary,
                                           input);
            INFO(model.name << ": "
                            << (report.has_value() ? std::string{} : report.error().error.message));
            REQUIRE(report.has_value());
            ++openBodies;

            CHECK_FALSE(report->summary().closedDomain);
            CHECK(report->summary().openFaces == reference.topology->boundaryFaces());
            CHECK(report->summary().wallFaces == 0);

            // THE THROUGHPUT IS POSITIVE on a real mesh with a uniform velocity
            // through an open boundary, so ADR-058's two measures EXIST and are
            // evaluated here rather than only on hand fixtures.
            REQUIRE(report->throughput().si() > 0.0);
            REQUIRE(report->massBalance().has_value());
            const CfdMassBalance balance = *report->massBalance();
            CHECK(balance.maxCellContinuity >= 0.0);
            CHECK(balance.globalImbalance >= 0.0);
            CHECK(balance.globalImbalance <= 2.0 + 1.0e-12);
            CHECK(cfd::isWellFormed(balance));

            // A UNIFORM VELOCITY THROUGH A CLOSED SURFACE IS DIVERGENCE-FREE in
            // real arithmetic, because `sum_f S_f` vanishes -- so the whole-body
            // imbalance is the mesh closure and nothing else. This is the one
            // place the relative measures meet a mesh nobody designed to flatter
            // them.
            INFO("maxCellContinuity = " << balance.maxCellContinuity
                                        << ", globalImbalance = " << balance.globalImbalance
                                        << ", worstClosure = " << report->summary().worstClosure);
            CHECK(balance.globalImbalance < cfd::kCfdContinuityCellTolerance);

            // ================================================================
            // THE PER-CELL MEASURE, BOUNDED ON A MULTI-CELL MESH FOR THE FIRST
            // TIME. A review proved this was needed by MEASUREMENT: replacing
            //
            //     balance.maxCellContinuity = largestCellResidual / throughput
            // with
            //     balance.maxCellContinuity = |netBoundaryFlux| / throughput
            //
            // -- reporting the GLOBAL imbalance under the name `CfdData.hpp`
            // reserves for the per-cell one, which is precisely the drift that
            // file's comment exists to prevent -- passed all 40 cases and 6943
            // assertions. Every other site that touched `maxCellContinuity` was
            // either a ONE-CELL fixture, where the cell residual IS the boundary
            // net, or a `>= 0.0` that cannot fail.
            //
            // This body has thousands of cells and a divergence-free field, so
            // the two quantities are numerically far apart and the bound is
            // justified from outside the code.
            // ================================================================
            CHECK(balance.maxCellContinuity < cfd::kCfdContinuityCellTolerance);

            // AND THE HEADLINE MEASURE'S PROVENANCE, which was also only
            // implied. A review showed that building `globalImbalance` from this
            // file's own `outflow - inflow` instead of the qualified
            // `netBoundaryFlux` passed everything -- ADR-058's "not a sum of
            // inlets and outlets" and ADR-074 Decision 1 both violated, and
            // invisible because `directionalSplitResidual` being small is
            // exactly what hides it. Multiplying back by the throughput
            // recovers the numerator and names which of the two it was.
            // RECOMPUTED AS A DIVISION, NOT RECOVERED BY MULTIPLYING BACK. The
            // first form of these two assertions multiplied the measure by the
            // throughput and compared against the numerator -- and FAILED on
            // five bodies while printing identical values, because `(a/b)*b` is
            // not `a` in floating point. Dividing the same operands the same way
            // is bit-exact, and still names WHICH numerator was used, which is
            // the whole point.
            CHECK(balance.globalImbalance ==
                  std::abs(report->divergence().netBoundaryFlux().si()) /
                      report->throughput().si());
            CHECK(balance.maxCellContinuity ==
                  report->largestCellResidual().si() / report->throughput().si());

            // (3') THE AGGREGATION BOUNDS, on a real boundary with hundreds of
            // faces summed in two different orders. These are the claims Q2 and
            // Q3 measured, now on real meshes. BOUNDED BELOW AS WELL AS ABOVE:
            // a review found that dropping `std::abs` from `relativeDeparture`
            // was caught by exactly one assertion in the whole suite, because
            // `< tolerance` is satisfied by any negative number.
            CHECK(report->summary().directionalSplitResidual >= 0.0);
            CHECK(report->summary().patchSumResidual >= 0.0);
            CHECK(report->summary().accumulationResidual >= 0.0);
            CHECK(report->summary().directionalSplitResidual <
                  cfd::kCfdContinuityAggregationTolerance);
            CHECK(report->summary().patchSumResidual < cfd::kCfdContinuityAggregationTolerance);
            CHECK(report->summary().accumulationResidual <
                  cfd::kCfdContinuityAggregationTolerance);
            CHECK(report->summary().accumulationResidual ==
                  report->divergence().summary().relativeImbalance);

            // The headline number is still the divergence's.
            CHECK(report->netBoundaryFlux().si() == report->divergence().netBoundaryFlux().si());

            // THE WORST CELL IS NAMED, AND ON A MESH WITH THOUSANDS OF CELLS.
            // Two review probes survived here: hardcoding the location to cell
            // 0, and dropping the "no cell seen yet" arm so the optional stays
            // empty on an exactly-conservative mesh. The location was only ever
            // asserted `== 0` on one- and two-cell fixtures.
            REQUIRE(report->worstCell().has_value());
            CHECK(report->worstCell()->value() < cells);
            const std::optional<MassFlowRate> worst = report->cellResidualAt(*report->worstCell());
            REQUIRE(worst.has_value());
            CHECK(std::abs(worst->si()) == report->largestCellResidual().si());
        }
    }
    // AND THE "LARGE MESH" CLAIM IS BACKED SOMEWHERE, rather than asserted of
    // every body. The suite must reach at least one body of several hundred
    // cells, or the per-cell/global distinction this test exists to exercise
    // would rest entirely on 6- and 12-cell meshes.
    INFO("largest body: " << largestCells << " cells, " << largestBoundary << " boundary faces");
    REQUIRE(largestCells >= 500);
    REQUIRE(largestBoundary >= 140);
    INFO("closed bodies = " << closedBodies << ", open bodies = " << openBodies);
    REQUIRE(closedBodies == models.size());
    REQUIRE(openBodies == models.size());
    REQUIRE(closedBodies >= 8);
}
