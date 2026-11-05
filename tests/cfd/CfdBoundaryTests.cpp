// P19-BC-001: the CFD boundary conditions of one analysis (ADR-067), driven
// through the hand-built seam.
//
// WHY THERE ARE TWO FILES. This one drives `bindCfdBoundary`, which takes a
// plain topology, a plain geometry and plain resolved targets, so a facet the
// topology does not hold, a boundary face no condition claims and two
// conditions on one facet can be PRESENTED rather than hoped for. The
// companion, CfdBoundaryMappedTests.cpp, drives the same code through P16's own
// resolution on real meshes, and is the authority for the claims that are about
// naming and attribution. Neither file can replace the other: a document that
// produces an uncovered facet is not something a model can be asked for, and a
// hand-built target cannot prove a FaceName resolves.
//
// THE ARITHMETIC IS CHECKED AGAINST FORMULAS WRITTEN OUT AGAIN HERE -- the
// signed volume of a tetrahedron, the height along gravity, the hydrostatic
// drop -- never against the accessor that produced them.
//
// WHAT IS NOT CLAIMED. There is no matrix, no source vector and no solve in
// this milestone, so nothing here assembles or converges anything. That a mass
// flux is `rho (U . Sf)` is ADR-058's and P19-CONT-001's to honour; this file
// checks only that the POLICY says so.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"

#include <bettercad/cfd/CfdBoundary.hpp>
#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdGeometry.hpp>
#include <bettercad/cfd/CfdTopology.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/meshing/Mesh.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using cfd::Acceleration3;
using cfd::BoundaryFaceIndex;
using cfd::CellIndex;
using cfd::CfdBoundaryCardinality;
using cfd::CfdBoundaryCondition;
using cfd::CfdBoundaryConditionKind;
using cfd::CfdBoundaryPolicy;
using cfd::CfdBoundaryProblem;
using cfd::CfdBoundaryTreatment;
using cfd::CfdGeometry;
using cfd::FaceIndex;
using cfd::CfdGravity;
using cfd::CfdReferencePressure;
using cfd::CfdResolvedTarget;
using cfd::CfdTopology;
using cfd::MovingWall;
using cfd::NoSlipWall;
using cfd::PressureInlet;
using cfd::PressureOutlet;
using cfd::SlipWall;
using cfd::VelocityInlet;
using cfd::Velocity3;
using meshing::ElementId;
using meshing::NodeId;

namespace {

const meshing::RegionId kRegion = meshing::RegionId::fromValue(1);

/// ADR-063's face table, written out again HERE rather than included from the
/// header, so a fixture that agreed with a corrupted table would not agree with
/// this one.
constexpr std::array<std::array<std::size_t, 3>, 4> kFaces{
    {{0, 2, 1}, {0, 1, 3}, {1, 2, 3}, {0, 3, 2}}};

std::map<std::array<std::size_t, 3>, std::pair<std::size_t, std::array<std::size_t, 3>>>
faceUses(const std::vector<std::array<std::size_t, 4>>& tets) {
    std::map<std::array<std::size_t, 3>, std::pair<std::size_t, std::array<std::size_t, 3>>> uses;
    for (const auto& tet : tets) {
        for (const auto& face : kFaces) {
            const std::array<std::size_t, 3> wound{tet[face[0]], tet[face[1]], tet[face[2]]};
            std::array<std::size_t, 3> key = wound;
            std::ranges::sort(key);
            auto& [count, winding] = uses[key];
            if (count++ == 0) {
                winding = wound;
            }
        }
    }
    return uses;
}

meshing::Mesh buildMesh(const std::vector<Point3D>& points,
                        const std::vector<std::array<std::size_t, 4>>& tets) {
    meshing::MeshBuilder builder;
    std::vector<NodeId> nodes;
    for (const Point3D& p : points) {
        nodes.push_back(require(builder.addNode(p)));
    }
    for (const auto& tet : tets) {
        REQUIRE(builder.addTetrahedron(
            {nodes[tet[0]], nodes[tet[1]], nodes[tet[2]], nodes[tet[3]]}, kRegion));
    }
    for (const auto& [key, use] : faceUses(tets)) {
        const auto& [count, w] = use;
        if (count == 1) {
            REQUIRE(builder.addTriangle({nodes[w[0]], nodes[w[1]], nodes[w[2]]}, kRegion));
        }
    }
    return builder.build();
}

/// ONE cell: four boundary faces and no internal face.
std::vector<Point3D> oneCellPoints() {
    return {Point3D{0_mm, 0_mm, 0_mm}, Point3D{10_mm, 0_mm, 0_mm}, Point3D{0_mm, 10_mm, 0_mm},
            Point3D{0_mm, 0_mm, 10_mm}};
}
const std::vector<std::array<std::size_t, 4>> kOneCell{{0, 1, 2, 3}};

/// Two cells sharing one face: A = (0,1,2,3), B = (1,2,3,4), both positive.
std::vector<Point3D> twoCellPoints() {
    return {Point3D{0_mm, 0_mm, 0_mm},  Point3D{10_mm, 0_mm, 0_mm},  Point3D{0_mm, 10_mm, 0_mm},
            Point3D{0_mm, 0_mm, 10_mm}, Point3D{10_mm, 10_mm, 10_mm}};
}
const std::vector<std::array<std::size_t, 4>> kTwoCells{{0, 1, 2, 3}, {1, 2, 3, 4}};

/// One patch holding every boundary triangle of @p mesh.
std::vector<meshing::MappedFace> onePatch(const meshing::Mesh& mesh) {
    meshing::MappedFace face;
    face.index = 0;
    for (const meshing::Triangle& triangle : mesh.triangles()) {
        face.facets.push_back(triangle.id);
    }
    return {face};
}

/// A topology and a geometry of one hand-built mesh, both through the plain
/// builders, so the seam can be shown configurations a document cannot produce.
struct Hand {
    meshing::Mesh mesh;
    CfdTopology topology;
    CfdGeometry geometry;

    static Hand make(const std::vector<Point3D>& points,
                     const std::vector<std::array<std::size_t, 4>>& tets) {
        meshing::Mesh mesh = buildMesh(points, tets);
        const std::vector<meshing::MappedFace> patches = onePatch(mesh);
        Result<CfdTopology> topology = cfd::buildCfdTopology(mesh, patches);
        INFO((topology.has_value() ? std::string{} : topology.error().message));
        REQUIRE(topology.has_value());
        Result<CfdGeometry> geometry = cfd::buildCfdGeometry(mesh, *topology);
        INFO((geometry.has_value() ? std::string{} : geometry.error().message));
        REQUIRE(geometry.has_value());
        return Hand{std::move(mesh), std::move(*topology), std::move(*geometry)};
    }

    /// Every boundary facet of the mesh, in BoundaryFaceIndex order.
    [[nodiscard]] std::vector<ElementId> facets() const {
        std::vector<ElementId> out;
        for (std::size_t b = 0; b < topology.boundaryFaces(); ++b) {
            out.push_back(topology.triangleOf(BoundaryFaceIndex::fromValue(b)));
        }
        return out;
    }

    /// The boundary facet whose three nodes all satisfy @p onPlane. Found from
    /// the MESH, not from the geometry's normals, so a fixture does not depend
    /// on the accessor a test then asserts about.
    template <typename Predicate>
    [[nodiscard]] ElementId facetWhere(Predicate onPlane) const {
        for (const meshing::Triangle& triangle : mesh.triangles()) {
            bool all = true;
            for (const NodeId node : triangle.nodes) {
                const meshing::Node* found = mesh.findNode(node);
                REQUIRE(found != nullptr);
                if (!onPlane(found->position)) {
                    all = false;
                    break;
                }
            }
            if (all) {
                return triangle.id;
            }
        }
        FAIL("no boundary facet lies in the requested plane");
        return ElementId{};
    }
};

CfdBoundaryCondition inlet(std::uint64_t id, const FaceName& face, Velocity speed) {
    return CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(id),
                                VelocityInlet{face, speed}};
}
CfdBoundaryCondition outlet(std::uint64_t id, const FaceName& face, Pressure value) {
    return CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(id),
                                PressureOutlet{face, value}};
}
CfdBoundaryCondition wall(std::uint64_t id, const FaceName& face) {
    return CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(id), NoSlipWall{face}};
}
CfdBoundaryCondition slip(std::uint64_t id, const FaceName& face) {
    return CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(id), SlipWall{face}};
}
CfdBoundaryCondition moving(std::uint64_t id, const FaceName& face, const Velocity3& velocity) {
    return CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(id), MovingWall{face, velocity}};
}

/// A face name that is well formed on its own terms. The seam never resolves
/// one -- it is handed facets directly -- so the only thing that matters here
/// is that `validate(condition)` accepts it.
FaceName someFace(std::uint64_t feature = 1) {
    return FaceName{ObjectId::fromValue(feature), FaceSelector{.role = FaceRole::EndCap}};
}

/// Six times the signed volume of a b c d, in cubic metres. The determinant
/// form, written out again so `locatePoint` is not checked against itself.
double sixSignedVolume(const Point3D& a, const Point3D& b, const Point3D& c, const Point3D& d) {
    const double ux = (b.x - a.x).si();
    const double uy = (b.y - a.y).si();
    const double uz = (b.z - a.z).si();
    const double vx = (c.x - a.x).si();
    const double vy = (c.y - a.y).si();
    const double vz = (c.z - a.z).si();
    const double wx = (d.x - a.x).si();
    const double wy = (d.y - a.y).si();
    const double wz = (d.z - a.z).si();
    return ux * (vy * wz - vz * wy) - uy * (vx * wz - vz * wx) + uz * (vx * wy - vy * wx);
}

/// Whether @p p lies in the tetrahedron a b c d, by the barycentric sign test
/// written out here. The ORACLE: it shares no code with `locatePoint`.
bool containsPoint(const Point3D& a, const Point3D& b, const Point3D& c, const Point3D& d,
                   const Point3D& p) {
    const double total = sixSignedVolume(a, b, c, d);
    if (total == 0.0) {
        return false;
    }
    const double sign = total > 0.0 ? 1.0 : -1.0;
    const double tolerance = 1e-9 * std::abs(total);
    return sign * sixSignedVolume(p, b, c, d) >= -tolerance &&
           sign * sixSignedVolume(a, p, c, d) >= -tolerance &&
           sign * sixSignedVolume(a, b, p, d) >= -tolerance &&
           sign * sixSignedVolume(a, b, c, p) >= -tolerance;
}

} // namespace

// ---------------------------------------------------------------------------
// The frozen table
// ---------------------------------------------------------------------------

TEST_CASE("CfdBoundary_TheTreatmentTableIsFrozenEntryByEntry", "[cfd][bc][policy]") {
    // ADR-067 Decision 2, written out again here rather than read from the
    // header, so a corrupted table would not agree with this one. Each row is
    // stated as three independent policies, not compared to a struct the
    // production code also builds.
    using K = CfdBoundaryConditionKind;
    using P = CfdBoundaryPolicy;

    const CfdBoundaryTreatment velocityInlet = cfd::treatmentOf(K::VelocityInlet);
    CHECK(velocityInlet.pressure == P::ZeroGradient);
    CHECK(velocityInlet.velocity == P::FixedValue);
    CHECK(velocityInlet.massFlux == P::FixedValue);

    for (const K kind : {K::PressureInlet, K::PressureOutlet}) {
        const CfdBoundaryTreatment t = cfd::treatmentOf(kind);
        INFO(cfd::toString(kind));
        CHECK(t.pressure == P::FixedValue);
        CHECK(t.velocity == P::ZeroGradient);
        // ADR-058: "the flow rate is an OUTCOME of the solve, not an input".
        CHECK(t.massFlux == P::Computed);
    }

    for (const K kind : {K::NoSlipWall, K::MovingWall}) {
        const CfdBoundaryTreatment t = cfd::treatmentOf(kind);
        INFO(cfd::toString(kind));
        CHECK(t.pressure == P::ZeroGradient);
        CHECK(t.velocity == P::FixedValue);
        CHECK(t.massFlux == P::FixedValue);
    }

    const CfdBoundaryTreatment slipWall = cfd::treatmentOf(K::Slip);
    CHECK(slipWall.pressure == P::ZeroGradient);
    // NEITHER of the first two: the normal component is zero and the tangential
    // ones are zero-gradient.
    CHECK(slipWall.velocity == P::Symmetry);
    CHECK(slipWall.massFlux == P::FixedValue);
}

TEST_CASE("CfdBoundary_SymmetryAndComputedAreReachableAndDistinct", "[cfd][bc][policy]") {
    // The two values the checklist's "zero-gradient" and "fixed-value" wording
    // does not cover. If either were collapsed onto another value this test
    // fails, which is the notice that ADR-067 Decision 2 needs rewriting.
    std::set<CfdBoundaryPolicy> velocities;
    std::set<CfdBoundaryPolicy> fluxes;
    for (const CfdBoundaryConditionKind kind : cfd::kCfdBoundaryConditionKinds) {
        velocities.insert(cfd::treatmentOf(kind).velocity);
        fluxes.insert(cfd::treatmentOf(kind).massFlux);
    }
    CHECK(velocities.contains(CfdBoundaryPolicy::Symmetry));
    CHECK(fluxes.contains(CfdBoundaryPolicy::Computed));
    CHECK(velocities.size() == 3); // FixedValue, ZeroGradient, Symmetry
    CHECK(fluxes.size() == 2);     // FixedValue, Computed

    // All four values are distinct strings, so a diagnostic cannot conflate
    // them.
    std::set<std::string> names;
    for (const CfdBoundaryPolicy policy : cfd::kCfdBoundaryPolicies) {
        names.insert(std::string{cfd::toString(policy)});
    }
    CHECK(names.size() == cfd::kCfdBoundaryPolicies.size());
    CHECK_FALSE(names.contains("unknown"));
}

TEST_CASE("CfdBoundary_ToStringNamesEveryEnumeratorAndFallsBackForNone", "[cfd][bc]") {
    for (const CfdBoundaryProblem problem : cfd::kCfdBoundaryProblems) {
        CHECK(cfd::toString(problem) != "unknown");
    }
    for (const cfd::CfdBoundaryStaleReason reason : cfd::kCfdBoundaryStaleReasons) {
        CHECK(cfd::toString(reason) != "unknown");
    }
    for (const cfd::TargetProblem problem : cfd::kTargetProblems) {
        CHECK(cfd::toString(problem) != "unknown");
    }
    // Distinct, so two problems cannot read alike in a log.
    std::set<std::string> problems;
    for (const CfdBoundaryProblem problem : cfd::kCfdBoundaryProblems) {
        problems.insert(std::string{cfd::toString(problem)});
    }
    CHECK(problems.size() == cfd::kCfdBoundaryProblems.size());

    // A value this build does not define falls back rather than reading off the
    // end of the switch.
    CHECK(cfd::toString(static_cast<CfdBoundaryProblem>(200)) == "unknown");
    CHECK(cfd::toString(static_cast<CfdBoundaryPolicy>(200)) == "unknown");
    CHECK(cfd::toString(static_cast<cfd::CfdBoundaryStaleReason>(200)) == "unknown");
    CHECK(cfd::toString(static_cast<cfd::TargetProblem>(200)) == "unknown");
}

// ---------------------------------------------------------------------------
// The pressure datum's point locator
// ---------------------------------------------------------------------------

TEST_CASE("CfdBoundary_ThePointLocatorAgreesWithTheBarycentricSignTest", "[cfd][bc][datum]") {
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    REQUIRE(hand.topology.cells() == 1);

    const std::array<Point3D, 4> v{oneCellPoints()[0], oneCellPoints()[1], oneCellPoints()[2],
                                   oneCellPoints()[3]};
    const double total = sixSignedVolume(v[0], v[1], v[2], v[3]);
    REQUIRE(total != 0.0);

    SECTION("a point inside is located, and the independent sign test agrees") {
        const Point3D inside{1_mm, 1_mm, 1_mm};
        const std::optional<CellIndex> cell = cfd::locatePoint(hand.topology, hand.mesh, inside);
        REQUIRE(cell.has_value());
        CHECK(cell->value() == 0);

        // The oracle: all four sub-volumes share the whole's sign.
        const double sign = total > 0.0 ? 1.0 : -1.0;
        CHECK(sign * sixSignedVolume(inside, v[1], v[2], v[3]) > 0.0);
        CHECK(sign * sixSignedVolume(v[0], inside, v[2], v[3]) > 0.0);
        CHECK(sign * sixSignedVolume(v[0], v[1], inside, v[3]) > 0.0);
        CHECK(sign * sixSignedVolume(v[0], v[1], v[2], inside) > 0.0);
    }

    SECTION("the centroid is inside, as a closed form") {
        const Point3D centroid{2.5_mm, 2.5_mm, 2.5_mm};
        CHECK(cfd::locatePoint(hand.topology, hand.mesh, centroid).has_value());
    }

    SECTION("a point outside is located nowhere, and nothing nearby is chosen") {
        for (const Point3D outside : {Point3D{100_mm, 100_mm, 100_mm}, Point3D{-1_mm, 1_mm, 1_mm},
                                      Point3D{9_mm, 9_mm, 9_mm}}) {
            CHECK_FALSE(cfd::locatePoint(hand.topology, hand.mesh, outside).has_value());
        }
    }
}

TEST_CASE("CfdBoundary_APointOnASharedFaceResolvesToTheLowerCell", "[cfd][bc][datum]") {
    // DETERMINISM, NOT A TIE-BREAK BY LUCK. The shared face of the two cells is
    // nodes 1, 2, 3; its centroid lies exactly on it, so both cells contain the
    // point and the rule -- the lowest CellIndex -- decides.
    const Hand hand = Hand::make(twoCellPoints(), kTwoCells);
    REQUIRE(hand.topology.cells() == 2);
    REQUIRE(hand.topology.internalFaces() == 1);

    const std::vector<Point3D> p = twoCellPoints();
    const Point3D onFace{Length::fromSi((p[1].x.si() + p[2].x.si() + p[3].x.si()) / 3.0),
                         Length::fromSi((p[1].y.si() + p[2].y.si() + p[3].y.si()) / 3.0),
                         Length::fromSi((p[1].z.si() + p[2].z.si() + p[3].z.si()) / 3.0)};

    // BOTH CELLS REALLY DO CONTAIN IT, proved by the independent oracle, so the
    // assertion below is about the RULE and not about only one cell qualifying.
    // An earlier draft of this test checked `std::abs(...) >= 0.0` here, which
    // is vacuously true and proved nothing; mutation analysis found it.
    REQUIRE(containsPoint(p[0], p[1], p[2], p[3], onFace));
    REQUIRE(containsPoint(p[1], p[2], p[3], p[4], onFace));

    // So the answer is a CHOICE between two valid cells, and the rule is that
    // the lower index wins.
    const std::optional<CellIndex> cell = cfd::locatePoint(hand.topology, hand.mesh, onFace);
    REQUIRE(cell.has_value());
    CHECK(cell->value() == 0);
    // Stated the other way round as well, so the rule is covered by more than
    // one assertion: the answer is the MINIMUM over the containing cells.
    std::vector<std::size_t> containing;
    for (std::size_t c = 0; c < hand.topology.cells(); ++c) {
        const std::array<std::size_t, 4>& tet = kTwoCells[c];
        if (containsPoint(p[tet[0]], p[tet[1]], p[tet[2]], p[tet[3]], onFace)) {
            containing.push_back(c);
        }
    }
    REQUIRE(containing.size() == 2);
    CHECK(cell->value() == *std::ranges::min_element(containing));

    // And the same answer every time, which is what "the same cell in every
    // build" means.
    for (int repeat = 0; repeat < 8; ++repeat) {
        CHECK(cfd::locatePoint(hand.topology, hand.mesh, onFace) == cell);
    }
}

// ---------------------------------------------------------------------------
// Gravity and the hydrostatic boundary pressure
// ---------------------------------------------------------------------------

TEST_CASE("CfdBoundary_TheHeightAlongGravityIncreasesUpwardWhateverGravityIs", "[cfd][bc][datum]") {
    const Acceleration3 down{.x = 0_m_per_s2, .y = 0_m_per_s2, .z = -9.81_m_per_s2};

    // h = -(x . g_hat), so a point 5 m above the origin has height +5 m.
    const std::optional<Length> up = cfd::heightAlongGravity(Point3D{0_m, 0_m, 5_m}, down);
    REQUIRE(up.has_value());
    CHECK_THAT(up->si(), WithinAbs(5.0, 1e-12));

    const std::optional<Length> below = cfd::heightAlongGravity(Point3D{0_m, 0_m, -2_m}, down);
    REQUIRE(below.has_value());
    CHECK_THAT(below->si(), WithinAbs(-2.0, 1e-12));

    // "Down" is whatever the model says it is: along -X, the height IS x.
    const Acceleration3 alongX{.x = -9.81_m_per_s2, .y = 0_m_per_s2, .z = 0_m_per_s2};
    const std::optional<Length> sideways = cfd::heightAlongGravity(Point3D{3_m, 0_m, 0_m}, alongX);
    REQUIRE(sideways.has_value());
    CHECK_THAT(sideways->si(), WithinAbs(3.0, 1e-12));

    // A zero vector is not a direction. No gravity is the ABSENCE of the record
    // (CfdData.hpp), so this is the malformed case and not "no gravity".
    CHECK_FALSE(cfd::heightAlongGravity(Point3D{1_m, 1_m, 1_m}, Acceleration3{}).has_value());
}

TEST_CASE("CfdBoundary_AHydrostaticBoundaryPressureFollowsTheClosedForm", "[cfd][bc][datum]") {
    const Density water = 1000_kg_per_m3;
    const CfdGravity gravity{
        .acceleration = {.x = 0_m_per_s2, .y = 0_m_per_s2, .z = -9.81_m_per_s2},
        .referenceHeight = 0_m};

    SECTION("without gravity the prescribed pressure is uniform and rho is not consulted") {
        for (const Point3D where : {Point3D{0_m, 0_m, 0_m}, Point3D{0_m, 0_m, -100_m}}) {
            const std::optional<Pressure> p =
                cfd::hydrostaticPressure(5000_Pa, where, std::nullopt, std::nullopt);
            REQUIRE(p.has_value());
            CHECK_THAT(p->si(), WithinAbs(5000.0, 1e-9));
        }
    }

    SECTION("one metre below the datum is rho g higher") {
        // p = p_b - rho |g| (h - h_ref), h = -1 m: +1000 * 9.81 = 9810 Pa.
        const std::optional<Pressure> p =
            cfd::hydrostaticPressure(0_Pa, Point3D{0_m, 0_m, -1_m}, gravity, water);
        REQUIRE(p.has_value());
        CHECK_THAT(p->si(), WithinRel(9810.0, 1e-12));
    }

    SECTION("above the datum it is lower, by the same formula") {
        const std::optional<Pressure> p =
            cfd::hydrostaticPressure(0_Pa, Point3D{0_m, 0_m, 2_m}, gravity, water);
        REQUIRE(p.has_value());
        CHECK_THAT(p->si(), WithinRel(-2.0 * 1000.0 * 9.81, 1e-12));
    }

    SECTION("the reference height moves the datum and nothing else") {
        const CfdGravity raised{.acceleration = gravity.acceleration, .referenceHeight = -1_m};
        // At h = -1 m with h_ref = -1 m the drop is zero.
        const std::optional<Pressure> p =
            cfd::hydrostaticPressure(123_Pa, Point3D{0_m, 0_m, -1_m}, raised, water);
        REQUIRE(p.has_value());
        CHECK_THAT(p->si(), WithinAbs(123.0, 1e-9));
    }

    SECTION("gravity with no density is refused rather than silently uniform") {
        CHECK_FALSE(
            cfd::hydrostaticPressure(0_Pa, Point3D{0_m, 0_m, -1_m}, gravity, std::nullopt)
                .has_value());
    }
}

// ---------------------------------------------------------------------------
// The seam's refusals
// ---------------------------------------------------------------------------

TEST_CASE("CfdBoundary_ABoundaryFaceWithNoConditionIsRefusedByName", "[cfd][bc][coverage]") {
    // ADR-058: every boundary facet carries exactly one condition. A flux would
    // otherwise leave through a face nothing prescribes.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const std::vector<ElementId> facets = hand.facets();
    REQUIRE(facets.size() == 4);

    // Three of the four covered, by a condition that fixes the pressure so the
    // datum is not what refuses.
    const std::vector<CfdResolvedTarget> targets{
        CfdResolvedTarget{.condition = outlet(1, someFace(), 0_Pa),
                          .facets = {facets[0], facets[1], facets[2]}}};
    const Result<cfd::PreparedCfdBoundary> bound =
        cfd::bindCfdBoundary(hand.topology, hand.geometry, hand.mesh, targets, 0, std::nullopt, std::nullopt,
                             std::nullopt);
    REQUIRE_FALSE(bound.has_value());
    CHECK_THAT(bound.error().message, ContainsSubstring("carries no condition"));
    CHECK_THAT(bound.error().message, ContainsSubstring("nothing prescribes"));
}

TEST_CASE("CfdBoundary_AFacetTwoConditionsClaimIsRefusedAndNeitherIsChosen",
          "[cfd][bc][coverage]") {
    // NOT the first, NOT the last, NOT by kind. No two CFD conditions
    // superpose, so two on one facet is a contradiction.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const std::vector<ElementId> facets = hand.facets();

    const std::vector<CfdResolvedTarget> targets{
        CfdResolvedTarget{.condition = outlet(1, someFace(), 0_Pa), .facets = facets},
        CfdResolvedTarget{.condition = wall(2, someFace(2)), .facets = {facets[1]}}};
    const Result<cfd::PreparedCfdBoundary> bound =
        cfd::bindCfdBoundary(hand.topology, hand.geometry, hand.mesh, targets, 0, std::nullopt, std::nullopt,
                             std::nullopt);
    REQUIRE_FALSE(bound.has_value());
    CHECK_THAT(bound.error().message, ContainsSubstring("claimed by both"));
    // BOTH identities are named, so the diagnostic does not hide one of them.
    CHECK_THAT(bound.error().message, ContainsSubstring("1"));
    CHECK_THAT(bound.error().message, ContainsSubstring("2"));
    CHECK_THAT(bound.error().message, ContainsSubstring("superpose"));
}

TEST_CASE("CfdBoundary_ATargetWithNoFacetIsNotAnEmptyCondition", "[cfd][bc][coverage]") {
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const std::vector<CfdResolvedTarget> targets{
        CfdResolvedTarget{.condition = outlet(1, someFace(), 0_Pa), .facets = hand.facets()},
        CfdResolvedTarget{.condition = wall(2, someFace(2)), .facets = {}}};
    const Result<cfd::PreparedCfdBoundary> bound =
        cfd::bindCfdBoundary(hand.topology, hand.geometry, hand.mesh, targets, 0, std::nullopt, std::nullopt,
                             std::nullopt);
    REQUIRE_FALSE(bound.has_value());
    CHECK_THAT(bound.error().message, ContainsSubstring("attributed no boundary facet"));
    CHECK_THAT(bound.error().message, ContainsSubstring("nothing to act on"));
}

TEST_CASE("CfdBoundary_AFacetOutsideTheTopologyIsRefusedRatherThanSkipped", "[cfd][bc][coverage]") {
    // Unreachable once the staged gate's bindings pass, and refused anyway:
    // skipping it would drop the facet from the coverage count and make the
    // coverage claim vacuous.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    std::vector<ElementId> facets = hand.facets();
    facets.push_back(ElementId::fromValue(900000));

    const std::vector<CfdResolvedTarget> targets{
        CfdResolvedTarget{.condition = outlet(1, someFace(), 0_Pa), .facets = std::move(facets)}};
    const Result<cfd::PreparedCfdBoundary> bound =
        cfd::bindCfdBoundary(hand.topology, hand.geometry, hand.mesh, targets, 0, std::nullopt, std::nullopt,
                             std::nullopt);
    REQUIRE_FALSE(bound.has_value());
    CHECK_THAT(bound.error().message, ContainsSubstring("not a boundary face of this topology"));
}

TEST_CASE("CfdBoundary_AnUnnamedPatchRefusesTheAnalysisBeforeAnyConditionIsRead",
          "[cfd][bc][coverage]") {
    // CfdMesh.hpp:222 assigned this refusal here: no GeometryReference can
    // reach an unnamed patch, so coverage is impossible. Asked EARLY -- the
    // conditions below are deliberately invalid, and the unnamed patch is still
    // what is reported.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const std::vector<CfdResolvedTarget> targets{
        CfdResolvedTarget{.condition = inlet(0, someFace(), 1_m_per_s), .facets = hand.facets()}};
    const Result<cfd::PreparedCfdBoundary> bound =
        cfd::bindCfdBoundary(hand.topology, hand.geometry, hand.mesh, targets, 1, std::nullopt, std::nullopt,
                             std::nullopt);
    REQUIRE_FALSE(bound.has_value());
    CHECK_THAT(bound.error().message, ContainsSubstring("carry no face name"));
    CHECK_THAT(bound.error().message, ContainsSubstring("exactly one condition"));
}

TEST_CASE("CfdBoundary_AnInvalidConditionIsRefusedWithP19Data001sOwnMessage", "[cfd][bc]") {
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);

    SECTION("an invalid identity") {
        const std::vector<CfdResolvedTarget> targets{
            CfdResolvedTarget{.condition = outlet(0, someFace(), 0_Pa), .facets = hand.facets()}};
        const Result<cfd::PreparedCfdBoundary> bound = cfd::bindCfdBoundary(
            hand.topology, hand.geometry, hand.mesh, targets, 0, std::nullopt, std::nullopt, std::nullopt);
        REQUIRE_FALSE(bound.has_value());
    }

    SECTION("a non-positive inflow speed, which is P19-DATA-001's rule and not a second opinion") {
        const std::vector<CfdResolvedTarget> targets{
            CfdResolvedTarget{.condition = inlet(1, someFace(), 0_m_per_s),
                              .facets = hand.facets()}};
        const Result<cfd::PreparedCfdBoundary> bound = cfd::bindCfdBoundary(
            hand.topology, hand.geometry, hand.mesh, targets, 0, std::nullopt, std::nullopt, std::nullopt);
        REQUIRE_FALSE(bound.has_value());
        // The condition's identity is prefixed onto the delegated message.
        CHECK_THAT(bound.error().message, ContainsSubstring("1"));
    }

    SECTION("a duplicate identity is refused rather than merged") {
        const std::vector<ElementId> facets = hand.facets();
        const std::vector<CfdResolvedTarget> targets{
            CfdResolvedTarget{.condition = outlet(7, someFace(), 0_Pa),
                              .facets = {facets[0], facets[1]}},
            CfdResolvedTarget{.condition = wall(7, someFace(2)), .facets = {facets[2], facets[3]}}};
        const Result<cfd::PreparedCfdBoundary> bound = cfd::bindCfdBoundary(
            hand.topology, hand.geometry, hand.mesh, targets, 0, std::nullopt, std::nullopt, std::nullopt);
        REQUIRE_FALSE(bound.has_value());
        CHECK_THAT(bound.error().message, ContainsSubstring("appears twice"));
        CHECK_THAT(bound.error().message, ContainsSubstring("silently ignored"));
    }
}

TEST_CASE("CfdBoundary_AMovingWallVelocityMustBeTangentialOnEveryFacet", "[cfd][bc][orientation]") {
    // ADR-058: the wall does not move and there is no moving mesh, so a normal
    // component would be flow through a wall.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const std::vector<ElementId> facets = hand.facets();

    // The facet in the z = 0 plane, found from the MESH so the fixture does not
    // depend on the normal accessor the assertion is about.
    const ElementId flat =
        hand.facetWhere([](const Point3D& p) { return std::abs(p.z.si()) < 1e-15; });
    std::vector<ElementId> rest;
    for (const ElementId facet : facets) {
        if (facet != flat) {
            rest.push_back(facet);
        }
    }
    REQUIRE(rest.size() == 3);

    SECTION("a velocity in the face is accepted") {
        const std::vector<CfdResolvedTarget> targets{
            CfdResolvedTarget{.condition = moving(1, someFace(),
                                                  Velocity3{.x = 1_m_per_s,
                                                            .y = 0_m_per_s,
                                                            .z = 0_m_per_s}),
                              .facets = {flat}},
            CfdResolvedTarget{.condition = outlet(2, someFace(2), 0_Pa), .facets = rest}};
        const Result<cfd::PreparedCfdBoundary> bound = cfd::bindCfdBoundary(
            hand.topology, hand.geometry, hand.mesh, targets, 0, std::nullopt, std::nullopt, std::nullopt);
        INFO((bound.has_value() ? std::string{} : bound.error().message));
        REQUIRE(bound.has_value());
        // And the prescribed velocity is the wall's own, unchanged.
        const std::optional<BoundaryFaceIndex> b = hand.topology.boundaryFaceOfTriangle(flat);
        REQUIRE(b.has_value());
        const std::optional<cfd::CfdBoundaryFace> record = bound->face(*b);
        REQUIRE(record.has_value());
        REQUIRE(record->velocity.has_value());
        CHECK_THAT(record->velocity->x.si(), WithinAbs(1.0, 1e-15));
        CHECK_THAT(record->velocity->magnitude().si(), WithinRel(1.0, 1e-12));
    }

    SECTION("a velocity along the normal is refused, and the margin is wide") {
        const std::vector<CfdResolvedTarget> targets{
            CfdResolvedTarget{.condition = moving(1, someFace(),
                                                  Velocity3{.x = 0_m_per_s,
                                                            .y = 0_m_per_s,
                                                            .z = 1_m_per_s}),
                              .facets = {flat}},
            CfdResolvedTarget{.condition = outlet(2, someFace(2), 0_Pa), .facets = rest}};
        const Result<cfd::PreparedCfdBoundary> bound = cfd::bindCfdBoundary(
            hand.topology, hand.geometry, hand.mesh, targets, 0, std::nullopt, std::nullopt, std::nullopt);
        REQUIRE_FALSE(bound.has_value());
        CHECK_THAT(bound.error().message, ContainsSubstring("not tangential"));
        CHECK_THAT(bound.error().message, ContainsSubstring("the wall does not move"));
    }
}

TEST_CASE("CfdBoundary_AMissingPressureDatumIsRefusedAndAReferencePointFixesIt",
          "[cfd][bc][datum]") {
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const std::vector<CfdResolvedTarget> walls{
        CfdResolvedTarget{.condition = wall(1, someFace()), .facets = hand.facets()}};

    SECTION("nothing fixes the pressure: refused") {
        const Result<cfd::PreparedCfdBoundary> bound = cfd::bindCfdBoundary(
            hand.topology, hand.geometry, hand.mesh, walls, 0, std::nullopt, std::nullopt, std::nullopt);
        REQUIRE_FALSE(bound.has_value());
        CHECK_THAT(bound.error().message, ContainsSubstring("only up to a constant"));
    }

    SECTION("a boundary that fixes the pressure needs no reference") {
        const std::vector<CfdResolvedTarget> withOutlet{
            CfdResolvedTarget{.condition = outlet(1, someFace(), 0_Pa), .facets = hand.facets()}};
        const Result<cfd::PreparedCfdBoundary> bound = cfd::bindCfdBoundary(
            hand.topology, hand.geometry, hand.mesh, withOutlet, 0, std::nullopt, std::nullopt, std::nullopt);
        INFO((bound.has_value() ? std::string{} : bound.error().message));
        REQUIRE(bound.has_value());
        CHECK_FALSE(bound->referenceCell().has_value());
    }

    SECTION("a reference point outside the fluid is refused by name") {
        const CfdReferencePressure outside{.value = 0_Pa,
                                           .location = Point3D{500_mm, 500_mm, 500_mm}};
        CHECK(cfd::bindCfdBoundaryFailure(hand.topology, hand.geometry, hand.mesh, walls, 0,
                                          outside, std::nullopt, std::nullopt)
                  ->problem == CfdBoundaryProblem::ReferencePointOutsideFluid);
        const Result<cfd::PreparedCfdBoundary> bound = cfd::bindCfdBoundary(
            hand.topology, hand.geometry, hand.mesh, walls, 0, outside, std::nullopt, std::nullopt);
        REQUIRE_FALSE(bound.has_value());
        CHECK_THAT(bound.error().message, ContainsSubstring("lies in no cell of the fluid"));
    }

    SECTION("a reference point inside the fluid fixes the level and is located") {
        const CfdReferencePressure inside{.value = 0_Pa, .location = Point3D{1_mm, 1_mm, 1_mm}};
        const Result<cfd::PreparedCfdBoundary> bound = cfd::bindCfdBoundary(
            hand.topology, hand.geometry, hand.mesh, walls, 0, inside, std::nullopt, std::nullopt);
        INFO((bound.has_value() ? std::string{} : bound.error().message));
        REQUIRE(bound.has_value());
        REQUIRE(bound->referenceCell().has_value());
        // The INDEPENDENT oracle, applied to the gate's own answer.
        const std::array<Point3D, 4> v{oneCellPoints()[0], oneCellPoints()[1], oneCellPoints()[2],
                                       oneCellPoints()[3]};
        CHECK(containsPoint(v[0], v[1], v[2], v[3], inside.location));
        CHECK(bound->referenceCell()->value() == 0);
    }

    SECTION("a reference pressure beside a pressure boundary is overdetermined") {
        // `validate(definition)` refuses this, and this gate does not assume it
        // was called. A review found it accepted, with the point located while
        // a boundary also fixed the level.
        const std::vector<CfdResolvedTarget> withOutlet{
            CfdResolvedTarget{.condition = outlet(1, someFace(), 0_Pa), .facets = hand.facets()}};
        const CfdReferencePressure inside{.value = 0_Pa, .location = Point3D{1_mm, 1_mm, 1_mm}};
        CHECK(cfd::bindCfdBoundaryFailure(hand.topology, hand.geometry, hand.mesh, withOutlet, 0,
                                          inside, std::nullopt, std::nullopt)
                  ->problem == CfdBoundaryProblem::PressureDatumOverdetermined);
        const Result<cfd::PreparedCfdBoundary> bound =
            cfd::bindCfdBoundary(hand.topology, hand.geometry, hand.mesh, withOutlet, 0, inside,
                                 std::nullopt, std::nullopt);
        REQUIRE_FALSE(bound.has_value());
        CHECK_THAT(bound.error().message, ContainsSubstring("overdetermined"));
    }

    SECTION("a malformed datum record is refused with P19-DATA-001's own message") {
        const CfdReferencePressure nan{.value = Pressure::fromSi(
                                           std::numeric_limits<double>::quiet_NaN()),
                                       .location = Point3D{1_mm, 1_mm, 1_mm}};
        CHECK(cfd::bindCfdBoundaryFailure(hand.topology, hand.geometry, hand.mesh, walls, 0, nan,
                                          std::nullopt, std::nullopt)
                  ->problem == CfdBoundaryProblem::DatumInvalid);

        // A ZERO GRAVITY VECTOR is malformed, not "no gravity": no gravity is
        // the ABSENCE of the record. A review found this reaching the
        // arithmetic and being reported as a non-finite product.
        const std::vector<CfdResolvedTarget> withOutlet{
            CfdResolvedTarget{.condition = outlet(1, someFace(), 0_Pa), .facets = hand.facets()}};
        CHECK(cfd::bindCfdBoundaryFailure(hand.topology, hand.geometry, hand.mesh, withOutlet, 0,
                                          std::nullopt, CfdGravity{}, 998_kg_per_m3)
                  ->problem == CfdBoundaryProblem::DatumInvalid);
    }

    SECTION("gravity with a pressure boundary and no density is refused by name") {
        const std::vector<CfdResolvedTarget> withOutlet{
            CfdResolvedTarget{.condition = outlet(1, someFace(), 0_Pa), .facets = hand.facets()}};
        const CfdGravity down{
            .acceleration = {.x = 0_m_per_s2, .y = 0_m_per_s2, .z = -9.81_m_per_s2},
            .referenceHeight = 0_m};
        CHECK(cfd::bindCfdBoundaryFailure(hand.topology, hand.geometry, hand.mesh, withOutlet, 0,
                                          std::nullopt, down, std::nullopt)
                  ->problem == CfdBoundaryProblem::HydrostaticDensityMissing);
        const Result<cfd::PreparedCfdBoundary> bound =
            cfd::bindCfdBoundary(hand.topology, hand.geometry, hand.mesh, withOutlet, 0,
                                 std::nullopt, down, std::nullopt);
        REQUIRE_FALSE(bound.has_value());
        CHECK_THAT(bound.error().message, ContainsSubstring("needs the fluid's density"));
    }
}

TEST_CASE("CfdBoundary_AGeometryOfAnotherTopologyIsRefused", "[cfd][bc][binding]") {
    const Hand one = Hand::make(oneCellPoints(), kOneCell);
    const Hand two = Hand::make(twoCellPoints(), kTwoCells);

    const std::vector<CfdResolvedTarget> targets{
        CfdResolvedTarget{.condition = outlet(1, someFace(), 0_Pa), .facets = one.facets()}};
    const Result<cfd::PreparedCfdBoundary> bound =
        cfd::bindCfdBoundary(one.topology, two.geometry, one.mesh, targets, 0, std::nullopt, std::nullopt,
                             std::nullopt);
    REQUIRE_FALSE(bound.has_value());
    CHECK_THAT(bound.error().message, ContainsSubstring("not computed from this topology"));
}

// ---------------------------------------------------------------------------
// What a successful binding says
// ---------------------------------------------------------------------------

TEST_CASE("CfdBoundary_TheCardinalityOfEveryConditionAgreesBothWays", "[cfd][bc][cardinality]") {
    const Hand hand = Hand::make(twoCellPoints(), kTwoCells);
    const std::vector<ElementId> facets = hand.facets();
    REQUIRE(facets.size() == 6);

    std::vector<ElementId> first{facets[0], facets[1]};
    std::vector<ElementId> rest{facets[2], facets[3], facets[4], facets[5]};
    const std::vector<CfdResolvedTarget> targets{
        CfdResolvedTarget{.condition = outlet(1, someFace(), 0_Pa), .facets = first},
        CfdResolvedTarget{.condition = slip(2, someFace(2)), .facets = rest}};

    const Result<cfd::PreparedCfdBoundary> bound =
        cfd::bindCfdBoundary(hand.topology, hand.geometry, hand.mesh, targets, 0, std::nullopt, std::nullopt,
                             std::nullopt);
    INFO((bound.has_value() ? std::string{} : bound.error().message));
    REQUIRE(bound.has_value());

    REQUIRE(bound->cardinality().size() == 2);
    // Records are in the order the conditions were given, which is the
    // definition's canonical order.
    CHECK(bound->cardinality()[0].condition == CfdBoundaryConditionId::fromValue(1));
    CHECK(bound->cardinality()[1].condition == CfdBoundaryConditionId::fromValue(2));

    std::size_t assigned = 0;
    for (const CfdBoundaryCardinality& record : bound->cardinality()) {
        INFO(record.condition.value());
        CHECK(record.agrees());
        CHECK(record.expected == record.assigned);
        assigned += record.assigned;
    }
    // The sum is the whole boundary, counted once each.
    CHECK(assigned == hand.topology.boundaryFaces());
    CHECK(bound->boundaryFaces() == hand.topology.boundaryFaces());

    // Counting forward from the targets gives the same total, which is what
    // makes the agreement above non-vacuous.
    CHECK(first.size() + rest.size() == hand.topology.boundaryFaces());
}

TEST_CASE("CfdBoundary_AnInletVelocityPointsIntoTheDomainOnEveryFacet", "[cfd][bc][orientation]") {
    // POSITIVE IS INTO THE DOMAIN. The prescribed velocity is constructed from
    // the outward normal, so the inflow sense is true by construction -- and
    // this asserts the construction, facet by facet, against the normal read
    // independently.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const Velocity speed = 0.25_m_per_s;
    const std::vector<CfdResolvedTarget> withDatum{
        CfdResolvedTarget{.condition = inlet(1, someFace(), speed),
                          .facets = {hand.facets()[0], hand.facets()[1], hand.facets()[2]}},
        CfdResolvedTarget{.condition = outlet(2, someFace(2), 0_Pa),
                          .facets = {hand.facets()[3]}}};
    const Result<cfd::PreparedCfdBoundary> bound =
        cfd::bindCfdBoundary(hand.topology, hand.geometry, hand.mesh, withDatum, 0, std::nullopt,
                             std::nullopt, std::nullopt);
    INFO((bound.has_value() ? std::string{} : bound.error().message));
    REQUIRE(bound.has_value());

    std::size_t inlets = 0;
    for (std::size_t b = 0; b < bound->boundaryFaces(); ++b) {
        const BoundaryFaceIndex boundaryFace = BoundaryFaceIndex::fromValue(b);
        const std::optional<cfd::CfdBoundaryFace> record = bound->face(boundaryFace);
        REQUIRE(record.has_value());
        if (record->kind != CfdBoundaryConditionKind::VelocityInlet) {
            continue;
        }
        ++inlets;
        REQUIRE(record->velocity.has_value());
        // THE INDEPENDENT STATEMENT, and the one that matters. Dotting the
        // prescribed velocity with the normal it was CONSTRUCTED from is a
        // tautology -- a review pointed that out, and it is why this compares
        // against the direction of the OWNER CELL'S INTERIOR instead, which is
        // two centroids and no normal. If `boundaryOutwardNormal` were
        // reversed, the velocity would blow fluid out of the domain and only
        // this assertion would notice.
        const FaceIndex face = hand.topology.faceOf(boundaryFace);
        const CellIndex owner = hand.topology.owner(face);
        const std::optional<Point3D> cellCentre = hand.geometry.cellCentroid(owner);
        const std::optional<Point3D> faceCentre = hand.geometry.faceCentroid(face);
        REQUIRE(cellCentre.has_value());
        REQUIRE(faceCentre.has_value());
        const double towardsInterior =
            record->velocity->x.si() * (cellCentre->x - faceCentre->x).si() +
            record->velocity->y.si() * (cellCentre->y - faceCentre->y).si() +
            record->velocity->z.si() * (cellCentre->z - faceCentre->z).si();
        CHECK(towardsInterior > 0.0);

        // And the construction itself, stated for completeness: against the
        // outward normal, by the full speed.
        const std::optional<Direction3D> normal =
            hand.geometry.boundaryOutwardNormal(boundaryFace);
        REQUIRE(normal.has_value());
        const double along = record->velocity->x.si() * normal->x() +
                             record->velocity->y.si() * normal->y() +
                             record->velocity->z.si() * normal->z();
        CHECK(along < 0.0);
        CHECK_THAT(along, WithinRel(-speed.si(), 1e-12));
        CHECK_THAT(record->velocity->magnitude().si(), WithinRel(speed.si(), 1e-12));
    }
    CHECK(inlets == 3);
}

TEST_CASE("CfdBoundary_OnlyTheFixedValueFieldsCarryAValue", "[cfd][bc][policy]") {
    const Hand hand = Hand::make(twoCellPoints(), kTwoCells);
    const std::vector<ElementId> facets = hand.facets();
    const std::vector<CfdResolvedTarget> targets{
        CfdResolvedTarget{.condition = outlet(1, someFace(), 500_Pa),
                          .facets = {facets[0], facets[1]}},
        CfdResolvedTarget{.condition = slip(2, someFace(2)), .facets = {facets[2], facets[3]}},
        CfdResolvedTarget{.condition = wall(3, someFace(3)), .facets = {facets[4], facets[5]}}};
    const Result<cfd::PreparedCfdBoundary> bound =
        cfd::bindCfdBoundary(hand.topology, hand.geometry, hand.mesh, targets, 0, std::nullopt, std::nullopt,
                             std::nullopt);
    INFO((bound.has_value() ? std::string{} : bound.error().message));
    REQUIRE(bound.has_value());

    for (std::size_t b = 0; b < bound->boundaryFaces(); ++b) {
        const BoundaryFaceIndex boundaryFace = BoundaryFaceIndex::fromValue(b);
        const std::optional<cfd::CfdBoundaryFace> record = bound->face(boundaryFace);
        REQUIRE(record.has_value());
        const std::optional<CfdBoundaryTreatment> treatment = bound->treatment(boundaryFace);
        REQUIRE(treatment.has_value());
        // The invariant the header states: a value is present EXACTLY when its
        // policy is FixedValue.
        CHECK(record->pressure.has_value() ==
              (treatment->pressure == CfdBoundaryPolicy::FixedValue));
        CHECK(record->velocity.has_value() ==
              (treatment->velocity == CfdBoundaryPolicy::FixedValue));
        if (record->kind == CfdBoundaryConditionKind::PressureOutlet) {
            REQUIRE(record->pressure.has_value());
            // No gravity: uniform, and exactly the prescribed value.
            CHECK_THAT(record->pressure->si(), WithinAbs(500.0, 1e-9));
        }
        if (record->kind == CfdBoundaryConditionKind::NoSlipWall) {
            REQUIRE(record->velocity.has_value());
            CHECK_THAT(record->velocity->magnitude().si(), WithinAbs(0.0, 1e-18));
        }
        if (record->kind == CfdBoundaryConditionKind::Slip) {
            // Symmetry carries NO prescribed velocity: the tangential part is
            // an outcome.
            CHECK_FALSE(record->velocity.has_value());
        }
    }
    CHECK(bound->treatment(BoundaryFaceIndex{}) == std::nullopt);
    CHECK(bound->face(BoundaryFaceIndex::fromValue(bound->boundaryFaces())) == std::nullopt);
}

TEST_CASE("CfdBoundary_ATargetWhoseFacetsRepeatIsRefusedForThatReason",
          "[cfd][bc][coverage]") {
    // NOT "claimed by both 3 and 3", which is what an earlier draft said: the
    // cause is one target's own list, not two conditions.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    std::vector<ElementId> facets = hand.facets();
    REQUIRE(facets.size() == 4);
    const std::vector<ElementId> repeated{facets[0], facets[0], facets[1], facets[2], facets[3]};

    const std::vector<CfdResolvedTarget> targets{
        CfdResolvedTarget{.condition = outlet(1, someFace(), 0_Pa), .facets = repeated}};
    CHECK(cfd::bindCfdBoundaryFailure(hand.topology, hand.geometry, hand.mesh, targets, 0,
                                      std::nullopt, std::nullopt, std::nullopt)
              ->problem == CfdBoundaryProblem::TargetFacetsNotUnique);
    const Result<cfd::PreparedCfdBoundary> bound = cfd::bindCfdBoundary(
        hand.topology, hand.geometry, hand.mesh, targets, 0, std::nullopt, std::nullopt,
        std::nullopt);
    REQUIRE_FALSE(bound.has_value());
    CHECK_THAT(bound.error().message, ContainsSubstring("strictly ascending"));

    // Out of order, without a repeat, is refused by the same rule.
    std::vector<ElementId> reversed = facets;
    std::ranges::reverse(reversed);
    const std::vector<CfdResolvedTarget> outOfOrder{
        CfdResolvedTarget{.condition = outlet(1, someFace(), 0_Pa), .facets = reversed}};
    CHECK(cfd::bindCfdBoundaryFailure(hand.topology, hand.geometry, hand.mesh, outOfOrder, 0,
                                      std::nullopt, std::nullopt, std::nullopt)
              ->problem == CfdBoundaryProblem::TargetFacetsNotUnique);
}

TEST_CASE("CfdBoundary_AMovingWallOnACurvedTargetIsRefusedRatherThanApproximated",
          "[cfd][bc][orientation]") {
    // ADR-067 Decision 5, as the review amended it. Two facets of a tetrahedron
    // have different normals, so a target covering both is not planar -- the
    // smallest curved target there is. On such a target no single normal
    // describes the surface, and a per-facet tangency test at a tolerance tight
    // enough to catch a real normal component would refuse the one velocity
    // that IS tangential to a cylinder.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const std::vector<ElementId> facets = hand.facets();
    const ElementId flat =
        hand.facetWhere([](const Point3D& p) { return std::abs(p.z.si()) < 1e-15; });

    std::vector<ElementId> two;
    for (const ElementId facet : facets) {
        if (facet == flat || two.size() == 1) {
            two.push_back(facet);
        }
        if (two.size() == 2) {
            break;
        }
    }
    std::ranges::sort(two);
    REQUIRE(two.size() == 2);
    std::vector<ElementId> rest;
    for (const ElementId facet : facets) {
        if (std::ranges::find(two, facet) == two.end()) {
            rest.push_back(facet);
        }
    }

    // THE MARGIN, MEASURED, so the tolerance is constrained from above: the two
    // facets' normals really do differ by far more than the threshold.
    const std::optional<BoundaryFaceIndex> a = hand.topology.boundaryFaceOfTriangle(two[0]);
    const std::optional<BoundaryFaceIndex> b = hand.topology.boundaryFaceOfTriangle(two[1]);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    const std::optional<Direction3D> na = hand.geometry.boundaryOutwardNormal(*a);
    const std::optional<Direction3D> nb = hand.geometry.boundaryOutwardNormal(*b);
    REQUIRE(na.has_value());
    REQUIRE(nb.has_value());
    const double deviation = 1.0 - na->dot(*nb);
    CHECK(deviation > 1e-3);

    const std::vector<CfdResolvedTarget> targets{
        CfdResolvedTarget{
            .condition = moving(1, someFace(),
                                Velocity3{.x = 1_m_per_s, .y = 0_m_per_s, .z = 0_m_per_s}),
            .facets = two},
        CfdResolvedTarget{.condition = outlet(2, someFace(2), 0_Pa), .facets = rest}};
    CHECK(cfd::bindCfdBoundaryFailure(hand.topology, hand.geometry, hand.mesh, targets, 0,
                                      std::nullopt, std::nullopt, std::nullopt)
              ->problem == CfdBoundaryProblem::WallTargetNotPlanar);
    const Result<cfd::PreparedCfdBoundary> bound = cfd::bindCfdBoundary(
        hand.topology, hand.geometry, hand.mesh, targets, 0, std::nullopt, std::nullopt,
        std::nullopt);
    REQUIRE_FALSE(bound.has_value());
    CHECK_THAT(bound.error().message, ContainsSubstring("not planar"));
    CHECK_THAT(bound.error().message, ContainsSubstring("rather than approximated"));

    // AND THE PLANAR MARGIN, so the tolerance is constrained from below too: a
    // single-facet target deviates from itself by nothing at all.
    const std::vector<CfdResolvedTarget> planar{
        CfdResolvedTarget{
            .condition = moving(1, someFace(),
                                Velocity3{.x = 1_m_per_s, .y = 0_m_per_s, .z = 0_m_per_s}),
            .facets = {flat}},
        CfdResolvedTarget{.condition = outlet(2, someFace(2), 0_Pa),
                          .facets = [&] {
                              std::vector<ElementId> others;
                              for (const ElementId facet : facets) {
                                  if (facet != flat) {
                                      others.push_back(facet);
                                  }
                              }
                              return others;
                          }()}};
    const Result<cfd::PreparedCfdBoundary> ok = cfd::bindCfdBoundary(
        hand.topology, hand.geometry, hand.mesh, planar, 0, std::nullopt, std::nullopt,
        std::nullopt);
    INFO((ok.has_value() ? std::string{} : ok.error().message));
    REQUIRE(ok.has_value());
}

TEST_CASE("CfdBoundary_TheTangencyMarginIsMeasuredOnBothSides", "[cfd][bc][orientation]") {
    // ADR-067 Decision 5 claims the fixture demonstrates both margins. A review
    // found no margin measured anywhere, which left `kTangentialCosine` free to
    // be any value in a very wide range. These two numbers pin it.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const ElementId flat =
        hand.facetWhere([](const Point3D& p) { return std::abs(p.z.si()) < 1e-15; });
    const std::optional<BoundaryFaceIndex> b = hand.topology.boundaryFaceOfTriangle(flat);
    REQUIRE(b.has_value());
    const std::optional<Direction3D> normal = hand.geometry.boundaryOutwardNormal(*b);
    REQUIRE(normal.has_value());

    const auto ratioOf = [&](const Velocity3& w) {
        const double speed = w.magnitude().si();
        const double along =
            w.x.si() * normal->x() + w.y.si() * normal->y() + w.z.si() * normal->z();
        return std::abs(along) / speed;
    };

    // ACCEPTED: in the plane of the facet, to round-off.
    const double accepted = ratioOf(Velocity3{.x = 1_m_per_s, .y = 0_m_per_s, .z = 0_m_per_s});
    CHECK(accepted < 1e-15);

    // REFUSED: along the normal.
    const double refused = ratioOf(Velocity3{.x = 0_m_per_s, .y = 0_m_per_s, .z = 1_m_per_s});
    CHECK_THAT(refused, WithinAbs(1.0, 1e-12));

    // AND AN OBLIQUE ONE, which is what no earlier fixture had: 1 degree off
    // tangential is refused, so the tolerance cannot be loosened to 1e-2 and
    // still pass.
    const double oblique = ratioOf(Velocity3{.x = Velocity::fromSi(std::cos(0.0174533)),
                                             .y = 0_m_per_s,
                                             .z = Velocity::fromSi(std::sin(0.0174533))});
    CHECK(oblique > 1e-2);

    std::vector<ElementId> rest;
    for (const ElementId facet : hand.facets()) {
        if (facet != flat) {
            rest.push_back(facet);
        }
    }
    const std::vector<CfdResolvedTarget> targets{
        CfdResolvedTarget{.condition = moving(1, someFace(),
                                              Velocity3{.x = Velocity::fromSi(std::cos(0.0174533)),
                                                        .y = 0_m_per_s,
                                                        .z = Velocity::fromSi(std::sin(0.0174533))}),
                          .facets = {flat}},
        CfdResolvedTarget{.condition = outlet(2, someFace(2), 0_Pa), .facets = rest}};
    CHECK(cfd::bindCfdBoundaryFailure(hand.topology, hand.geometry, hand.mesh, targets, 0,
                                      std::nullopt, std::nullopt, std::nullopt)
              ->problem == CfdBoundaryProblem::WallVelocityNotTangential);
}

TEST_CASE("CfdBoundary_APressureInletIsBoundLikeAnOutletAndFixesTheLevel", "[cfd][bc][policy]") {
    // The one payload branch no earlier test took, so a copy-paste between the
    // two pressure payloads would have been invisible.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const std::vector<CfdResolvedTarget> targets{
        CfdResolvedTarget{.condition = CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1),
                                                            PressureInlet{someFace(), 750_Pa}},
                          .facets = hand.facets()}};
    const Result<cfd::PreparedCfdBoundary> bound = cfd::bindCfdBoundary(
        hand.topology, hand.geometry, hand.mesh, targets, 0, std::nullopt, std::nullopt,
        std::nullopt);
    INFO((bound.has_value() ? std::string{} : bound.error().message));
    REQUIRE(bound.has_value());
    for (std::size_t b = 0; b < bound->boundaryFaces(); ++b) {
        const std::optional<cfd::CfdBoundaryFace> record =
            bound->face(BoundaryFaceIndex::fromValue(b));
        REQUIRE(record.has_value());
        CHECK(record->kind == CfdBoundaryConditionKind::PressureInlet);
        REQUIRE(record->pressure.has_value());
        // ITS OWN staticPressure, not the outlet's -- which is the point.
        CHECK_THAT(record->pressure->si(), WithinAbs(750.0, 1e-9));
        CHECK_FALSE(record->velocity.has_value());
    }
    // And it fixes the pressure, so no reference is needed.
    CHECK_FALSE(bound->referenceCell().has_value());
}

TEST_CASE("CfdBoundary_AnUnrecognisedKindCannotBecomeAnOpenBoundary", "[cfd][bc][policy]") {
    // The fallback's stated safety property, which nothing exercised: the walls'
    // treatment is the most constraining, so a kind this build does not define
    // cannot let flow through by accident.
    const CfdBoundaryTreatment fallback =
        cfd::treatmentOf(static_cast<CfdBoundaryConditionKind>(200));
    CHECK(fallback.pressure == CfdBoundaryPolicy::ZeroGradient);
    CHECK(fallback.velocity == CfdBoundaryPolicy::FixedValue);
    CHECK(fallback.massFlux == CfdBoundaryPolicy::FixedValue);
    CHECK(fallback == cfd::treatmentOf(CfdBoundaryConditionKind::NoSlipWall));
}

TEST_CASE("CfdBoundary_TheLocatorsToleranceIsConstrainedFromBothSides", "[cfd][bc][datum]") {
    // A review showed the tolerance was free over seven orders of magnitude:
    // the outside points were far outside and the on-face point was exactly on
    // the face, so nothing pinned it. This puts a point just outside.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const std::array<Point3D, 4> v{oneCellPoints()[0], oneCellPoints()[1], oneCellPoints()[2],
                                   oneCellPoints()[3]};
    const double total = std::abs(sixSignedVolume(v[0], v[1], v[2], v[3]));

    // A point a measured fraction of the cell's volume OUTSIDE the z = 0 face,
    // which is the face of nodes 0, 1, 2 -- so the sub-volume that detects the
    // breach is the one replacing node 3, not node 0. (An earlier draft of this
    // test measured the wrong one and reported a fraction that was not the
    // breach at all.)
    const Point3D justOutside{2_mm, 2_mm, Length::fromSi(-1e-7)};
    const double breach = std::abs(sixSignedVolume(v[0], v[1], v[2], justOutside));
    const double fraction = breach / total;
    INFO("breach fraction " << fraction);
    // Far above the 1e-9 threshold, and far below a careless 1e-2 one, so this
    // point discriminates between them.
    CHECK(fraction > 1e-9);
    CHECK(fraction < 1e-2);
    CHECK_FALSE(cfd::locatePoint(hand.topology, hand.mesh, justOutside).has_value());

    // And a point just INSIDE by the same margin is located, so the threshold
    // is not merely "refuse everything near the surface".
    const Point3D justInside{2_mm, 2_mm, Length::fromSi(1e-7)};
    CHECK(cfd::locatePoint(hand.topology, hand.mesh, justInside).has_value());
}

TEST_CASE("CfdBoundary_TheHydrostaticTermUsesTheGravityItWasGivenNotNineEightOne",
          "[cfd][bc][datum]") {
    // Every other fixture uses |g| = 9.81, so hardcoding it in the production
    // code would have passed the whole suite -- a review's mutation candidate.
    const Density water = 1000_kg_per_m3;
    const CfdGravity lunar{
        .acceleration = {.x = 0_m_per_s2, .y = 0_m_per_s2, .z = -1.62_m_per_s2},
        .referenceHeight = 0_m};
    const std::optional<Pressure> p =
        cfd::hydrostaticPressure(0_Pa, Point3D{0_m, 0_m, -1_m}, lunar, water);
    REQUIRE(p.has_value());
    CHECK_THAT(p->si(), WithinRel(1000.0 * 1.62, 1e-12));
    // And it is NOT the Earth answer, by a wide margin.
    CHECK(std::abs(p->si() - 1000.0 * 9.81) > 1000.0);
}

TEST_CASE("CfdBoundary_EverySeamReachableProblemIsAssertedByTag", "[cfd][bc]") {
    // A review found the seam discarding the problem TAG, so eight refusals
    // could be swapped for one another with every message assertion still
    // passing. This asserts the tag for each one the seam can reach.
    const Hand hand = Hand::make(oneCellPoints(), kOneCell);
    const Hand other = Hand::make(twoCellPoints(), kTwoCells);
    const std::vector<ElementId> facets = hand.facets();
    const auto tag = [&](const Hand& h, std::span<const CfdResolvedTarget> targets,
                         std::size_t unnamed) {
        const std::optional<cfd::CfdBoundaryFailure> failure = cfd::bindCfdBoundaryFailure(
            h.topology, h.geometry, h.mesh, targets, unnamed, std::nullopt, std::nullopt,
            std::nullopt);
        REQUIRE(failure.has_value());
        return failure->problem;
    };

    const std::vector<CfdResolvedTarget> whole{
        CfdResolvedTarget{.condition = outlet(1, someFace(), 0_Pa), .facets = facets}};

    CHECK(tag(hand, whole, 3) == CfdBoundaryProblem::UnnamedPatch);
    CHECK(tag(hand,
              std::vector<CfdResolvedTarget>{
                  CfdResolvedTarget{.condition = outlet(0, someFace(), 0_Pa), .facets = facets}},
              0) == CfdBoundaryProblem::ConditionInvalid);
    CHECK(tag(hand,
              std::vector<CfdResolvedTarget>{
                  CfdResolvedTarget{.condition = outlet(7, someFace(), 0_Pa),
                                    .facets = {facets[0], facets[1]}},
                  CfdResolvedTarget{.condition = wall(7, someFace(2)),
                                    .facets = {facets[2], facets[3]}}},
              0) == CfdBoundaryProblem::DuplicateConditionId);
    CHECK(tag(hand,
              std::vector<CfdResolvedTarget>{
                  CfdResolvedTarget{.condition = outlet(1, someFace(), 0_Pa), .facets = facets},
                  CfdResolvedTarget{.condition = wall(2, someFace(2)), .facets = {}}},
              0) == CfdBoundaryProblem::TargetWithoutFacets);
    {
        std::vector<ElementId> stranger = facets;
        stranger.push_back(ElementId::fromValue(900000));
        CHECK(tag(hand,
                  std::vector<CfdResolvedTarget>{CfdResolvedTarget{
                      .condition = outlet(1, someFace(), 0_Pa), .facets = stranger}},
                  0) == CfdBoundaryProblem::FacetNotInTopology);
    }
    CHECK(tag(hand,
              std::vector<CfdResolvedTarget>{
                  CfdResolvedTarget{.condition = outlet(1, someFace(), 0_Pa), .facets = facets},
                  CfdResolvedTarget{.condition = wall(2, someFace(2)), .facets = {facets[1]}}},
              0) == CfdBoundaryProblem::FacetClaimedTwice);
    CHECK(tag(hand,
              std::vector<CfdResolvedTarget>{
                  CfdResolvedTarget{.condition = outlet(1, someFace(), 0_Pa),
                                    .facets = {facets[0], facets[1], facets[2]}}},
              0) == CfdBoundaryProblem::FacetWithoutCondition);
    CHECK(tag(hand,
              std::vector<CfdResolvedTarget>{
                  CfdResolvedTarget{.condition = wall(1, someFace()), .facets = facets}},
              0) == CfdBoundaryProblem::PressureDatumMissing);

    // And the geometry binding, which needs two different meshes.
    const std::optional<cfd::CfdBoundaryFailure> mismatched = cfd::bindCfdBoundaryFailure(
        hand.topology, other.geometry, hand.mesh, whole, 0, std::nullopt, std::nullopt,
        std::nullopt);
    REQUIRE(mismatched.has_value());
    CHECK(mismatched->problem == CfdBoundaryProblem::GeometryOfAnotherTopology);
    CHECK(mismatched->error.code == ErrorCode::FailedPrecondition);

    // A successful binding reports no failure at all, so the helper above is
    // not simply always answering.
    CHECK_FALSE(cfd::bindCfdBoundaryFailure(hand.topology, hand.geometry, hand.mesh, whole, 0,
                                            std::nullopt, std::nullopt, std::nullopt)
                    .has_value());
}

TEST_CASE("CfdBoundary_TwoBindingsOfOneInputAreEqual", "[cfd][bc][determinism]") {
    const Hand hand = Hand::make(twoCellPoints(), kTwoCells);
    const std::vector<ElementId> facets = hand.facets();
    const std::vector<CfdResolvedTarget> targets{
        CfdResolvedTarget{.condition = outlet(1, someFace(), 0_Pa),
                          .facets = {facets[0], facets[1], facets[2]}},
        CfdResolvedTarget{.condition = wall(2, someFace(2)),
                          .facets = {facets[3], facets[4], facets[5]}}};

    const Result<cfd::PreparedCfdBoundary> first = cfd::bindCfdBoundary(
        hand.topology, hand.geometry, hand.mesh, targets, 0, std::nullopt, std::nullopt, std::nullopt);
    REQUIRE(first.has_value());
    for (int repeat = 0; repeat < 5; ++repeat) {
        const Result<cfd::PreparedCfdBoundary> again = cfd::bindCfdBoundary(
            hand.topology, hand.geometry, hand.mesh, targets, 0, std::nullopt, std::nullopt, std::nullopt);
        REQUIRE(again.has_value());
        CHECK(*again == *first);
    }

    // Equality DISCRIMINATES, so the comparison above means something.
    const std::vector<CfdResolvedTarget> different{
        CfdResolvedTarget{.condition = outlet(1, someFace(), 1_Pa),
                          .facets = {facets[0], facets[1], facets[2]}},
        CfdResolvedTarget{.condition = wall(2, someFace(2)),
                          .facets = {facets[3], facets[4], facets[5]}}};
    const Result<cfd::PreparedCfdBoundary> other = cfd::bindCfdBoundary(
        hand.topology, hand.geometry, hand.mesh, different, 0, std::nullopt, std::nullopt, std::nullopt);
    REQUIRE(other.has_value());
    CHECK_FALSE(*other == *first);
}
