// P16-DATA-001: the engineering mesh data model.
//
// Every expected geometric value here is computed BY HAND in the test or in the
// comment beside it, never by calling the function under test. The reference
// tetrahedron's volume is 1/6 because the determinant of the identity matrix is
// 1, not because signedVolume() said so.
#include <bettercad/core/math/Vector.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/meshing/MeshValidation.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

using namespace bettercad;
using namespace bettercad::meshing;

namespace {

/// Millimetres, because every coordinate in these fixtures is a round number of
/// them and a bare Length::fromSi(0.001) reads as noise.
[[nodiscard]] Point3D mm(double x, double y, double z) {
    return Point3D{Length::fromSi(x * 1e-3), Length::fromSi(y * 1e-3), Length::fromSi(z * 1e-3)};
}

constexpr RegionId kRegion1 = RegionId::fromValue(1);
constexpr RegionId kRegion2 = RegionId::fromValue(2);

/// The reference tetrahedron of the milestone brief, in metres so the expected
/// volume is exactly 1/6 m^3.
///
///   p1 = (0,0,0)  p2 = (1,0,0)  p3 = (0,1,0)  p4 = (0,0,1)
///
///   e1 = p2-p1 = (1,0,0)   e2 = p3-p1 = (0,1,0)   e3 = p4-p1 = (0,0,1)
///   e2 x e3 = (1*1-0*0, 0*0-0*1, 0*0-1*0) = (1,0,0)
///   e1 . (1,0,0) = 1
///   V = 1/6
///
/// Positive, so the brief's ordering is already the valid orientation under the
/// convention ADR-032 fixed. Nothing was reordered to make that true.
[[nodiscard]] Point3D unitTet(int corner) {
    switch (corner) {
    case 0:
        return Point3D{Length::fromSi(0.0), Length::fromSi(0.0), Length::fromSi(0.0)};
    case 1:
        return Point3D{Length::fromSi(1.0), Length::fromSi(0.0), Length::fromSi(0.0)};
    case 2:
        return Point3D{Length::fromSi(0.0), Length::fromSi(1.0), Length::fromSi(0.0)};
    default:
        return Point3D{Length::fromSi(0.0), Length::fromSi(0.0), Length::fromSi(1.0)};
    }
}

[[nodiscard]] bool hasKind(const MeshValidationReport& report, MeshIssueKind kind) {
    return std::ranges::any_of(report.issues, [kind](const MeshIssue& i) { return i.kind == kind; });
}

[[nodiscard]] std::size_t countKind(const MeshValidationReport& report, MeshIssueKind kind) {
    return static_cast<std::size_t>(
        std::ranges::count_if(report.issues, [kind](const MeshIssue& i) { return i.kind == kind; }));
}

/// A builder holding one valid positive tetrahedron on nodes 1..4.
[[nodiscard]] MeshBuilder referenceTetBuilder() {
    MeshBuilder builder;
    for (int corner = 0; corner < 4; ++corner) {
        REQUIRE(builder.addNode(unitTet(corner)).has_value());
    }
    REQUIRE(builder
                .addTetrahedron({NodeId::fromValue(1), NodeId::fromValue(2), NodeId::fromValue(3),
                                 NodeId::fromValue(4)},
                                kRegion1)
                .has_value());
    return builder;
}

} // namespace

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------

TEST_CASE("MeshIds_DefaultHandleIsInvalidAndZeroIsNeverAllocated", "[meshing][data][ids]") {
    CHECK_FALSE(NodeId{}.isValid());
    CHECK_FALSE(ElementId{}.isValid());
    CHECK_FALSE(RegionId{}.isValid());
    CHECK_FALSE(MeshId{}.isValid());
    CHECK(NodeId{}.value() == 0);

    // Allocation starts at 1, matching every other identifier in BetterCAD, so a
    // default-constructed handle never points at the first node.
    MeshBuilder builder;
    const auto first = builder.addNode(mm(0, 0, 0));
    REQUIRE(first.has_value());
    CHECK(first->value() == 1);
    CHECK(first->isValid());
}

TEST_CASE("MeshIds_AreOrderedAndComparableWithinTheirOwnKind", "[meshing][data][ids]") {
    CHECK(NodeId::fromValue(1) < NodeId::fromValue(2));
    CHECK(NodeId::fromValue(7) == NodeId::fromValue(7));
    CHECK(NodeId::fromValue(7) != NodeId::fromValue(8));
    CHECK(ElementId::fromValue(3) < ElementId::fromValue(4));
    CHECK(RegionId::fromValue(1) != RegionId::fromValue(2));
}

TEST_CASE("MeshStamp_IsUniquePerMeshAndDistinguishesGenerations", "[meshing][data][ids]") {
    const MeshBuilder first;
    const MeshBuilder second;
    CHECK(first.stamp().mesh != second.stamp().mesh);
    CHECK(first.stamp().isValid());

    const MeshBuilder regenerated(1);
    CHECK(regenerated.stamp().generation == 1);
    CHECK(first.stamp().generation == 0);
}

TEST_CASE("Mesh_RefusesAHandleFromAnotherMeshRatherThanReinterpretingIt", "[meshing][data][ids]") {
    const Mesh a = referenceTetBuilder().build();
    const Mesh b = referenceTetBuilder().build();

    // Both meshes have a node:1, and they are DIFFERENT nodes. ADR-031 requires
    // the mismatch to be refused rather than silently resolved.
    CHECK(a.owns(a.stamp()));
    CHECK_FALSE(a.owns(b.stamp()));
    CHECK_FALSE(b.owns(a.stamp()));
    CHECK_FALSE(a.owns(MeshStamp{}));

    // A generation change alone makes a stamp stale, even for the same MeshId.
    MeshStamp nextGeneration = a.stamp();
    nextGeneration.generation += 1;
    CHECK_FALSE(a.owns(nextGeneration));
}

TEST_CASE("Mesh_CopyKeepsTheSameTopologyAndHandlesAndIsNotARemesh", "[meshing][data][ids]") {
    const Mesh original = referenceTetBuilder().build();
    const Mesh copy = original; // NOLINT(performance-unnecessary-copy-initialization)

    CHECK(copy.stamp() == original.stamp());
    CHECK(copy == original);
    CHECK(copy.owns(original.stamp()));
    CHECK(copy.nodeCount() == original.nodeCount());
}

// ---------------------------------------------------------------------------
// Nodes and coordinates
// ---------------------------------------------------------------------------

TEST_CASE("MeshBuilder_RejectsANonFiniteCoordinateOnEveryAxis", "[meshing][data][nodes]") {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();

    for (const double bad : {nan, inf, -inf}) {
        for (int axis = 0; axis < 3; ++axis) {
            MeshBuilder builder;
            Point3D position = mm(1, 2, 3);
            (axis == 0 ? position.x : axis == 1 ? position.y : position.z) = Length::fromSi(bad);

            const auto added = builder.addNode(position);
            REQUIRE_FALSE(added.has_value());
            CHECK(added.error().code == ErrorCode::InvalidArgument);
            // Atomic: the rejected node did not enter the mesh.
            CHECK(builder.nodeCount() == 0);
        }
    }
}

TEST_CASE("MeshBuilder_RejectsARepeatedNodeHandleAndLeavesTheMeshUnchanged", "[meshing][data][nodes]") {
    MeshBuilder builder;
    REQUIRE(builder.addNode(NodeId::fromValue(5), mm(0, 0, 0)).has_value());
    CHECK(builder.nodeCount() == 1);

    const auto repeated = builder.addNode(NodeId::fromValue(5), mm(1, 1, 1));
    REQUIRE_FALSE(repeated.has_value());
    CHECK(repeated.error().code == ErrorCode::AlreadyExists);
    CHECK(builder.nodeCount() == 1);

    // The original node is untouched: no last-write-wins.
    const Mesh mesh = builder.build();
    const Node* node = mesh.findNode(NodeId::fromValue(5));
    REQUIRE(node != nullptr);
    CHECK(node->position == mm(0, 0, 0));
}

TEST_CASE("MeshBuilder_RejectsAnOutOfOrderNodeHandle", "[meshing][data][nodes]") {
    MeshBuilder builder;
    REQUIRE(builder.addNode(NodeId::fromValue(10), mm(0, 0, 0)).has_value());

    const auto earlier = builder.addNode(NodeId::fromValue(4), mm(1, 0, 0));
    REQUIRE_FALSE(earlier.has_value());
    CHECK(earlier.error().code == ErrorCode::AlreadyExists);
    CHECK(builder.nodeCount() == 1);
}

TEST_CASE("MeshBuilder_RejectsTheInvalidNodeHandle", "[meshing][data][nodes]") {
    MeshBuilder builder;
    const auto added = builder.addNode(NodeId{}, mm(0, 0, 0));
    REQUIRE_FALSE(added.has_value());
    CHECK(added.error().code == ErrorCode::InvalidArgument);
    CHECK(builder.nodeCount() == 0);
}

TEST_CASE("Mesh_ResolvesSparseHandlesByIdentityAndNeverByPosition", "[meshing][data][nodes]") {
    // The gap case the architecture review called out: indexing nodes[id] would
    // resolve node:4 to the node stored at index 4 -- a different point, or out
    // of range.
    MeshBuilder builder;
    REQUIRE(builder.addNode(NodeId::fromValue(1), mm(1, 0, 0)).has_value());
    REQUIRE(builder.addNode(NodeId::fromValue(4), mm(4, 0, 0)).has_value());
    REQUIRE(builder.addNode(NodeId::fromValue(10), mm(10, 0, 0)).has_value());
    const Mesh mesh = builder.build();

    REQUIRE(mesh.findNode(NodeId::fromValue(1)) != nullptr);
    CHECK(mesh.findNode(NodeId::fromValue(1))->position == mm(1, 0, 0));
    REQUIRE(mesh.findNode(NodeId::fromValue(4)) != nullptr);
    CHECK(mesh.findNode(NodeId::fromValue(4))->position == mm(4, 0, 0));
    REQUIRE(mesh.findNode(NodeId::fromValue(10)) != nullptr);
    CHECK(mesh.findNode(NodeId::fromValue(10))->position == mm(10, 0, 0));

    // Handles in the gaps name nothing at all.
    CHECK(mesh.findNode(NodeId::fromValue(2)) == nullptr);
    CHECK(mesh.findNode(NodeId::fromValue(5)) == nullptr);
    CHECK(mesh.findNode(NodeId::fromValue(11)) == nullptr);
    CHECK(mesh.findNode(NodeId{}) == nullptr);
}

TEST_CASE("Mesh_TwoNodesAtTheSamePositionStayTwoNodes", "[meshing][data][nodes]") {
    // Identity and position are different concepts. Merging them here would make
    // the data model decide topology -- two coincident nodes may be a contact
    // interface or a disconnected region, and that is not this layer's call.
    MeshBuilder builder;
    const auto first = builder.addNode(mm(1, 2, 3));
    const auto second = builder.addNode(mm(1, 2, 3));
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(*first != *second);

    const Mesh mesh = builder.build();
    CHECK(mesh.nodeCount() == 2);
    CHECK(mesh.findNode(*first)->position == mesh.findNode(*second)->position);
}

// ---------------------------------------------------------------------------
// Signed volume: the orientation convention
// ---------------------------------------------------------------------------

TEST_CASE("SignedVolume_OfTheReferenceTetrahedronIsPositiveOneSixth", "[meshing][data][geometry]") {
    // Hand-computed above: V = 1/6 exactly.
    const Volume v = signedVolume(unitTet(0), unitTet(1), unitTet(2), unitTet(3));
    CHECK_THAT(v.si(), Catch::Matchers::WithinAbs(1.0 / 6.0, 1e-15));
    CHECK(v.si() > 0.0);
}

TEST_CASE("SignedVolume_ChangesSignWhenTwoNodesAreSwapped", "[meshing][data][geometry]") {
    const Volume forward = signedVolume(unitTet(0), unitTet(1), unitTet(2), unitTet(3));
    // Swap p2 and p3. Hand-computed: e1=(0,1,0), e2=(1,0,0), e3=(0,0,1),
    // e2 x e3 = (0,-1,0), e1 . (0,-1,0) = -1, so V = -1/6.
    const Volume swapped = signedVolume(unitTet(0), unitTet(2), unitTet(1), unitTet(3));

    CHECK_THAT(swapped.si(), Catch::Matchers::WithinAbs(-1.0 / 6.0, 1e-15));
    CHECK_THAT(swapped.si(), Catch::Matchers::WithinAbs(-forward.si(), 1e-15));
    CHECK(swapped.si() < 0.0);
}

TEST_CASE("SignedVolume_SignFollowsThePermutationParityNotTheApparentReversal",
          "[meshing][data][geometry]") {
    // Worth its own test because the intuitive move is wrong, and a mesher
    // author "repairing" inverted elements by reversing their connectivity would
    // achieve exactly nothing.
    //
    // An ODD permutation flips the sign; an EVEN one preserves it.
    const Volume reference = signedVolume(unitTet(0), unitTet(1), unitTet(2), unitTet(3));
    REQUIRE(reference.si() > 0.0);

    // Reversing all four: (1,2,3,4) -> (4,3,2,1) is (1 4)(2 3), two swaps, EVEN.
    const Volume reversed = signedVolume(unitTet(3), unitTet(2), unitTet(1), unitTet(0));
    CHECK_THAT(reversed.si(), Catch::Matchers::WithinAbs(reference.si(), 1e-15));
    CHECK(reversed.si() > 0.0);

    // One swap is ODD, so it does flip.
    const Volume oneSwap = signedVolume(unitTet(1), unitTet(0), unitTet(2), unitTet(3));
    CHECK_THAT(oneSwap.si(), Catch::Matchers::WithinAbs(-reference.si(), 1e-15));

    // A 3-cycle is EVEN, so it does not.
    const Volume cycled = signedVolume(unitTet(0), unitTet(2), unitTet(3), unitTet(1));
    CHECK_THAT(cycled.si(), Catch::Matchers::WithinAbs(reference.si(), 1e-15));
}

TEST_CASE("SignedVolume_OfCoplanarNodesIsExactlyZero", "[meshing][data][geometry]") {
    const Volume v = signedVolume(mm(0, 0, 0), mm(1, 0, 0), mm(0, 1, 0), mm(1, 1, 0));
    CHECK(v.si() == 0.0);
}

TEST_CASE("SignedVolume_OfANonAxisAlignedTetrahedronMatchesTheHandComputation",
          "[meshing][data][geometry]") {
    // Every coordinate varies, so an implementation that accidentally assumed
    // axis alignment cannot pass.
    //
    //   p1 = (1,1,1)  p2 = (2,3,1)  p3 = (1,2,5)  p4 = (4,1,2)   [metres]
    //   e1 = (1,2,0)  e2 = (0,1,4)  e3 = (3,0,1)
    //   e2 x e3 = (1*1 - 4*0, 4*3 - 0*1, 0*0 - 1*3) = (1, 12, -3)
    //   e1 . (1,12,-3) = 1 + 24 + 0 = 25
    //   V = 25/6
    const auto m = [](double x, double y, double z) {
        return Point3D{Length::fromSi(x), Length::fromSi(y), Length::fromSi(z)};
    };
    const Volume v = signedVolume(m(1, 1, 1), m(2, 3, 1), m(1, 2, 5), m(4, 1, 2));
    CHECK_THAT(v.si(), Catch::Matchers::WithinAbs(25.0 / 6.0, 1e-14));
    CHECK(v.si() > 0.0);
}

TEST_CASE("SignedVolume_IsUnchangedByTranslation", "[meshing][data][geometry]") {
    const Translation3D d{Length::fromSi(12.5), Length::fromSi(-3.25), Length::fromSi(7.75)};
    const Volume before = signedVolume(unitTet(0), unitTet(1), unitTet(2), unitTet(3));
    const Volume after =
        signedVolume(unitTet(0) + d, unitTet(1) + d, unitTet(2) + d, unitTet(3) + d);

    CHECK_THAT(after.si(), Catch::Matchers::WithinAbs(before.si(), 1e-15));
}

TEST_CASE("SignedVolume_IsUnchangedByAProperRotation", "[meshing][data][geometry]") {
    // A rotation of 37 degrees about Z has determinant +1, so a signed volume
    // must be preserved exactly in exact arithmetic and to rounding here.
    const double angle = 37.0 * std::numbers::pi / 180.0;
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    const auto rotate = [c, s](const Point3D& p) {
        return Point3D{Length::fromSi(c * p.x.si() - s * p.y.si()),
                       Length::fromSi(s * p.x.si() + c * p.y.si()), p.z};
    };

    const Volume before = signedVolume(unitTet(0), unitTet(1), unitTet(2), unitTet(3));
    const Volume after = signedVolume(rotate(unitTet(0)), rotate(unitTet(1)), rotate(unitTet(2)),
                                      rotate(unitTet(3)));

    CHECK_THAT(after.si(), Catch::Matchers::WithinAbs(before.si(), 1e-15));
    CHECK(after.si() > 0.0);
}

TEST_CASE("SignedVolume_ChangesSignUnderAReflection", "[meshing][data][geometry]") {
    // A reflection has determinant -1. If orientation detection were fake -- an
    // abs(), or a magnitude compared against zero -- this would still come back
    // positive.
    const auto reflect = [](const Point3D& p) { return Point3D{Length::fromSi(-p.x.si()), p.y, p.z}; };

    const Volume before = signedVolume(unitTet(0), unitTet(1), unitTet(2), unitTet(3));
    const Volume after = signedVolume(reflect(unitTet(0)), reflect(unitTet(1)), reflect(unitTet(2)),
                                      reflect(unitTet(3)));

    CHECK(before.si() > 0.0);
    CHECK(after.si() < 0.0);
    CHECK_THAT(after.si(), Catch::Matchers::WithinAbs(-before.si(), 1e-15));
}

TEST_CASE("TriangleArea_MatchesTheHandComputationAndIsZeroWhenCollinear",
          "[meshing][data][geometry]") {
    // A 3-4-5 right triangle in the XY plane: area = 1/2 * 3 * 4 = 6.
    const auto m = [](double x, double y, double z) {
        return Point3D{Length::fromSi(x), Length::fromSi(y), Length::fromSi(z)};
    };
    CHECK_THAT(triangleArea(m(0, 0, 0), m(3, 0, 0), m(0, 4, 0)).si(),
               Catch::Matchers::WithinAbs(6.0, 1e-15));

    // Collinear points enclose nothing.
    CHECK(triangleArea(m(0, 0, 0), m(1, 1, 1), m(2, 2, 2)).si() == 0.0);
    // Two coincident points likewise.
    CHECK(triangleArea(m(1, 1, 1), m(1, 1, 1), m(2, 5, 3)).si() == 0.0);
}

TEST_CASE("TriangleArea_IsUnchangedByTranslation", "[meshing][data][geometry]") {
    const Translation3D d{Length::fromSi(-8.0), Length::fromSi(2.0), Length::fromSi(0.5)};
    const auto m = [](double x, double y, double z) {
        return Point3D{Length::fromSi(x), Length::fromSi(y), Length::fromSi(z)};
    };
    const Area before = triangleArea(m(0, 0, 0), m(3, 0, 0), m(0, 4, 0));
    const Area after = triangleArea(m(0, 0, 0) + d, m(3, 0, 0) + d, m(0, 4, 0) + d);
    CHECK_THAT(after.si(), Catch::Matchers::WithinAbs(before.si(), 1e-15));
}

// ---------------------------------------------------------------------------
// Connectivity
// ---------------------------------------------------------------------------

TEST_CASE("MeshBuilder_RejectsAnElementNamingANodeThatDoesNotExist", "[meshing][data][connectivity]") {
    MeshBuilder builder = referenceTetBuilder();
    const std::size_t before = builder.elementCount();

    const auto added = builder.addTetrahedron({NodeId::fromValue(1), NodeId::fromValue(2),
                                               NodeId::fromValue(3), NodeId::fromValue(999)},
                                              kRegion1);
    REQUIRE_FALSE(added.has_value());
    CHECK(added.error().code == ErrorCode::NotFound);
    CHECK(builder.elementCount() == before);

    const auto triangle =
        builder.addTriangle({NodeId::fromValue(1), NodeId::fromValue(2), NodeId::fromValue(999)}, kRegion1);
    REQUIRE_FALSE(triangle.has_value());
    CHECK(triangle.error().code == ErrorCode::NotFound);
    CHECK(builder.elementCount() == before);
}

TEST_CASE("MeshBuilder_RejectsAnElementNamingTheSameNodeTwice", "[meshing][data][connectivity]") {
    MeshBuilder builder = referenceTetBuilder();
    const std::size_t before = builder.elementCount();

    // The connectivity defect is reported directly, not indirectly as a zero
    // volume: [A, B, B, C] is wrong whatever the points happen to be.
    const auto tet = builder.addTetrahedron({NodeId::fromValue(1), NodeId::fromValue(2),
                                             NodeId::fromValue(2), NodeId::fromValue(3)},
                                            kRegion1);
    REQUIRE_FALSE(tet.has_value());
    CHECK(tet.error().code == ErrorCode::InvalidArgument);

    const auto triangle =
        builder.addTriangle({NodeId::fromValue(1), NodeId::fromValue(1), NodeId::fromValue(2)}, kRegion1);
    REQUIRE_FALSE(triangle.has_value());
    CHECK(triangle.error().code == ErrorCode::InvalidArgument);

    CHECK(builder.elementCount() == before);
}

TEST_CASE("MeshBuilder_RejectsAnElementWithNoRegion", "[meshing][data][connectivity]") {
    MeshBuilder builder = referenceTetBuilder();
    const std::size_t before = builder.elementCount();

    const auto added = builder.addTetrahedron({NodeId::fromValue(1), NodeId::fromValue(2),
                                               NodeId::fromValue(3), NodeId::fromValue(4)},
                                              RegionId{});
    REQUIRE_FALSE(added.has_value());
    CHECK(added.error().code == ErrorCode::InvalidArgument);
    CHECK(builder.elementCount() == before);
}

TEST_CASE("MeshBuilder_RejectsAnElementWithAnUnderfilledConnectivityList",
          "[meshing][data][connectivity]") {
    // An under-filled braced list is VALID aggregate initialisation of
    // std::array<NodeId, 4> -- it zero-fills the fourth handle -- so the
    // compiler cannot catch this one and the check has to be here. The zero
    // handle is the invalid handle, and it is refused.
    MeshBuilder builder = referenceTetBuilder();
    const std::size_t before = builder.elementCount();

    const auto added = builder.addTetrahedron({NodeId::fromValue(1), NodeId::fromValue(2),
                                               NodeId::fromValue(3)},
                                              kRegion1);
    REQUIRE_FALSE(added.has_value());
    CHECK(added.error().code == ErrorCode::InvalidArgument);
    CHECK(builder.elementCount() == before);

    const auto triangle = builder.addTriangle({NodeId::fromValue(1), NodeId::fromValue(2)}, kRegion1);
    REQUIRE_FALSE(triangle.has_value());
    CHECK(triangle.error().code == ErrorCode::InvalidArgument);
    CHECK(builder.elementCount() == before);
}

TEST_CASE("MeshBuilder_NeverReordersElementConnectivity", "[meshing][data][connectivity]") {
    // Sorting the handles would look tidy and would flip orientation. The stored
    // order must be the order given.
    MeshBuilder builder;
    for (int corner = 0; corner < 4; ++corner) {
        REQUIRE(builder.addNode(unitTet(corner)).has_value());
    }
    const std::array<NodeId, 4> given{NodeId::fromValue(3), NodeId::fromValue(1), NodeId::fromValue(4),
                                      NodeId::fromValue(2)};
    REQUIRE(builder.addTetrahedron(given, kRegion1).has_value());

    const Mesh mesh = builder.build();
    REQUIRE(mesh.tetrahedra().size() == 1);
    CHECK(mesh.tetrahedra()[0].nodes == given);
}

TEST_CASE("Mesh_ElementTypeIsReportedPerElementAndSharesOneIdentitySpace",
          "[meshing][data][elements]") {
    MeshBuilder builder = referenceTetBuilder(); // element 1 is the tetrahedron
    const auto triangle =
        builder.addTriangle({NodeId::fromValue(1), NodeId::fromValue(2), NodeId::fromValue(3)}, kRegion1);
    REQUIRE(triangle.has_value());
    CHECK(triangle->value() == 2); // one counter across both kinds

    const Mesh mesh = builder.build();
    CHECK(mesh.elementType(ElementId::fromValue(1)) == ElementType::Tetrahedron4);
    CHECK(mesh.elementType(ElementId::fromValue(2)) == ElementType::Triangle3);
    CHECK_FALSE(mesh.elementType(ElementId::fromValue(3)).has_value());
    CHECK_FALSE(mesh.elementType(ElementId{}).has_value());
    CHECK(mesh.elementCount() == 2);
}

TEST_CASE("ElementType_ReportsItsNodeCountAndName", "[meshing][data][elements]") {
    CHECK(nodeCount(ElementType::Triangle3) == 3);
    CHECK(nodeCount(ElementType::Tetrahedron4) == 4);
    CHECK(toString(ElementType::Triangle3) == "triangle3");
    CHECK(toString(ElementType::Tetrahedron4) == "tetrahedron4");
}

// ---------------------------------------------------------------------------
// Whole-mesh validation
// ---------------------------------------------------------------------------

TEST_CASE("Validate_AcceptsTheReferenceTetrahedron", "[meshing][data][validation]") {
    const Mesh mesh = referenceTetBuilder().build();
    const MeshValidationReport report = validate(mesh);
    CHECK(report.dataValid());
    CHECK(report.issues.empty());
}

TEST_CASE("Validate_RejectsAnInvertedTetrahedronAndNamesIt", "[meshing][data][validation]") {
    MeshBuilder builder;
    for (int corner = 0; corner < 4; ++corner) {
        REQUIRE(builder.addNode(unitTet(corner)).has_value());
    }
    // p2 and p3 swapped: hand-computed V = -1/6.
    REQUIRE(builder
                .addTetrahedron({NodeId::fromValue(1), NodeId::fromValue(3), NodeId::fromValue(2),
                                 NodeId::fromValue(4)},
                                kRegion1)
                .has_value());

    const MeshValidationReport report = validate(builder.build());
    CHECK_FALSE(report.dataValid());
    REQUIRE(hasKind(report, MeshIssueKind::InvertedTetrahedron));
    // Reported as inverted, NOT as degenerate: the distinction is the whole
    // point of keeping the sign.
    CHECK_FALSE(hasKind(report, MeshIssueKind::DegenerateTetrahedron));
    const auto issue = std::ranges::find_if(
        report.issues, [](const MeshIssue& i) { return i.kind == MeshIssueKind::InvertedTetrahedron; });
    REQUIRE(issue != report.issues.end());
    CHECK(issue->element == ElementId::fromValue(1));
}

TEST_CASE("Validate_RejectsACoplanarTetrahedronDespiteFourDistinctHandles",
          "[meshing][data][validation]") {
    // Connectivity is impeccable -- four distinct, existing nodes. Geometric
    // degeneracy is a different defect and must be caught on its own.
    MeshBuilder builder;
    REQUIRE(builder.addNode(mm(0, 0, 0)).has_value());
    REQUIRE(builder.addNode(mm(1, 0, 0)).has_value());
    REQUIRE(builder.addNode(mm(0, 1, 0)).has_value());
    REQUIRE(builder.addNode(mm(1, 1, 0)).has_value());
    REQUIRE(builder
                .addTetrahedron({NodeId::fromValue(1), NodeId::fromValue(2), NodeId::fromValue(3),
                                 NodeId::fromValue(4)},
                                kRegion1)
                .has_value());

    const MeshValidationReport report = validate(builder.build());
    CHECK_FALSE(report.dataValid());
    CHECK(hasKind(report, MeshIssueKind::DegenerateTetrahedron));
    CHECK_FALSE(hasKind(report, MeshIssueKind::InvertedTetrahedron));
}

TEST_CASE("Validate_RejectsADegenerateTriangle", "[meshing][data][validation]") {
    MeshBuilder builder;
    REQUIRE(builder.addNode(mm(0, 0, 0)).has_value());
    REQUIRE(builder.addNode(mm(1, 1, 1)).has_value());
    REQUIRE(builder.addNode(mm(2, 2, 2)).has_value());
    REQUIRE(builder
                .addTriangle({NodeId::fromValue(1), NodeId::fromValue(2), NodeId::fromValue(3)}, kRegion1)
                .has_value());

    const MeshValidationReport report = validate(builder.build());
    CHECK_FALSE(report.dataValid());
    CHECK(hasKind(report, MeshIssueKind::DegenerateTriangle));
}

TEST_CASE("Validate_AcceptsAThinTetrahedronBecauseQualityIsNotDataValidity",
          "[meshing][data][validation]") {
    // A sliver. It will solve badly and P16-QUALITY-001 should say so; it is
    // still a correct description of a region of space, so the DATA model must
    // not reject it. A tolerance here would silently become a quality threshold.
    MeshBuilder builder;
    REQUIRE(builder.addNode(mm(0, 0, 0)).has_value());
    REQUIRE(builder.addNode(mm(1, 0, 0)).has_value());
    REQUIRE(builder.addNode(mm(0, 1, 0)).has_value());
    REQUIRE(builder.addNode(Point3D{Length::zero(), Length::zero(), Length::fromSi(1e-12)}).has_value());
    REQUIRE(builder
                .addTetrahedron({NodeId::fromValue(1), NodeId::fromValue(2), NodeId::fromValue(3),
                                 NodeId::fromValue(4)},
                                kRegion1)
                .has_value());

    const MeshValidationReport report = validate(builder.build());
    CHECK(report.dataValid());
}

TEST_CASE("Validate_RejectsANodeSharedBetweenTwoRegions", "[meshing][data][validation]") {
    // ADR-032: "A mesh has one region per solid, shares no node between
    // regions." Two tetrahedra that both use node 1 but claim different regions
    // describe two solids welded together, which P16 does not represent.
    MeshBuilder builder;
    for (int corner = 0; corner < 4; ++corner) {
        REQUIRE(builder.addNode(unitTet(corner)).has_value());
    }
    REQUIRE(builder.addNode(mm(-1000, 0, 0)).has_value());  // node 5
    REQUIRE(builder.addNode(mm(-1000, 1000, 0)).has_value()); // node 6
    REQUIRE(builder.addNode(mm(-1000, 0, 1000)).has_value()); // node 7

    REQUIRE(builder
                .addTetrahedron({NodeId::fromValue(1), NodeId::fromValue(2), NodeId::fromValue(3),
                                 NodeId::fromValue(4)},
                                kRegion1)
                .has_value());
    // Region 2 reuses node 1.
    REQUIRE(builder
                .addTetrahedron({NodeId::fromValue(1), NodeId::fromValue(5), NodeId::fromValue(6),
                                 NodeId::fromValue(7)},
                                kRegion2)
                .has_value());

    const MeshValidationReport report = validate(builder.build());
    CHECK_FALSE(report.dataValid());
    REQUIRE(hasKind(report, MeshIssueKind::NodeSharedBetweenRegions));
    const auto issue = std::ranges::find_if(report.issues, [](const MeshIssue& i) {
        return i.kind == MeshIssueKind::NodeSharedBetweenRegions;
    });
    REQUIRE(issue != report.issues.end());
    CHECK(issue->node == NodeId::fromValue(1));
    CHECK(issue->region == kRegion2);
}

TEST_CASE("Validate_AcceptsTwoRegionsThatShareNoNode", "[meshing][data][validation]") {
    MeshBuilder builder;
    for (int corner = 0; corner < 4; ++corner) {
        REQUIRE(builder.addNode(unitTet(corner)).has_value());
    }
    const Translation3D far{Length::fromSi(100.0), Length::zero(), Length::zero()};
    for (int corner = 0; corner < 4; ++corner) {
        REQUIRE(builder.addNode(unitTet(corner) + far).has_value());
    }
    REQUIRE(builder
                .addTetrahedron({NodeId::fromValue(1), NodeId::fromValue(2), NodeId::fromValue(3),
                                 NodeId::fromValue(4)},
                                kRegion1)
                .has_value());
    REQUIRE(builder
                .addTetrahedron({NodeId::fromValue(5), NodeId::fromValue(6), NodeId::fromValue(7),
                                 NodeId::fromValue(8)},
                                kRegion2)
                .has_value());

    const Mesh mesh = builder.build();
    CHECK(validate(mesh).dataValid());
    REQUIRE(mesh.regions().size() == 2);
    CHECK(mesh.regions()[0] == kRegion1);
    CHECK(mesh.regions()[1] == kRegion2);
}

TEST_CASE("Validate_ReportsAnEmptyMeshAsHavingNoElements", "[meshing][data][validation]") {
    const Mesh empty;
    CHECK(empty.isEmpty());
    const MeshValidationReport report = validate(empty);
    CHECK_FALSE(report.dataValid());
    CHECK(hasKind(report, MeshIssueKind::EmptyMesh));

    // A mesh holding nodes but no elements is the same complaint: a container
    // may be empty, a mesh offered to a consumer may not.
    MeshBuilder builder;
    REQUIRE(builder.addNode(mm(0, 0, 0)).has_value());
    CHECK(hasKind(validate(builder.build()), MeshIssueKind::EmptyMesh));
}

TEST_CASE("Validate_CollectsEveryIssueRatherThanStoppingAtTheFirst", "[meshing][data][validation]") {
    MeshBuilder builder;
    for (int corner = 0; corner < 4; ++corner) {
        REQUIRE(builder.addNode(unitTet(corner)).has_value());
    }
    REQUIRE(builder.addNode(mm(0, 0, 0)).has_value()); // 5
    REQUIRE(builder.addNode(mm(1, 0, 0)).has_value()); // 6
    REQUIRE(builder.addNode(mm(0, 1, 0)).has_value()); // 7
    REQUIRE(builder.addNode(mm(1, 1, 0)).has_value()); // 8
    // 9..12: a second unit tetrahedron, translated clear of the first.
    const Translation3D clear{Length::fromSi(50.0), Length::zero(), Length::zero()};
    for (int corner = 0; corner < 4; ++corner) {
        REQUIRE(builder.addNode(unitTet(corner) + clear).has_value());
    }

    // Two inverted tetrahedra and one coplanar one. A report that stops at the
    // first turns one debugging session into three.
    //
    // BOTH inversions swap exactly TWO nodes, which is an ODD permutation. That
    // matters and is easy to get wrong: reversing all four handles --
    // (1,2,3,4) -> (4,3,2,1) -- is the permutation (1 4)(2 3), which is EVEN, so
    // it preserves the sign and does NOT invert the element. An earlier version
    // of this fixture used exactly that and expected an inversion the
    // mathematics does not produce.
    REQUIRE(builder
                .addTetrahedron({NodeId::fromValue(1), NodeId::fromValue(3), NodeId::fromValue(2),
                                 NodeId::fromValue(4)},
                                kRegion1)
                .has_value());
    REQUIRE(builder
                .addTetrahedron({NodeId::fromValue(10), NodeId::fromValue(9), NodeId::fromValue(11),
                                 NodeId::fromValue(12)},
                                kRegion1)
                .has_value());
    REQUIRE(builder
                .addTetrahedron({NodeId::fromValue(5), NodeId::fromValue(6), NodeId::fromValue(7),
                                 NodeId::fromValue(8)},
                                kRegion1)
                .has_value());

    const MeshValidationReport report = validate(builder.build());
    CHECK_FALSE(report.dataValid());
    CHECK(countKind(report, MeshIssueKind::InvertedTetrahedron) == 2);
    CHECK(countKind(report, MeshIssueKind::DegenerateTetrahedron) == 1);
}

TEST_CASE("Validate_OrdersIssuesByKindThenByHandle", "[meshing][data][validation]") {
    MeshBuilder builder;
    REQUIRE(builder.addNode(mm(0, 0, 0)).has_value());
    REQUIRE(builder.addNode(mm(1, 0, 0)).has_value());
    REQUIRE(builder.addNode(mm(0, 1, 0)).has_value());
    REQUIRE(builder.addNode(mm(1, 1, 0)).has_value());
    REQUIRE(builder.addNode(mm(2, 2, 2)).has_value());
    // A degenerate triangle (nodes 1, 2, 5 are collinear along x=y=z... they are
    // not; nodes 1,2,4 lie in z=0 and are not collinear either) -- so build the
    // degeneracy explicitly from three points on one line, and a coplanar
    // tetrahedron. DegenerateTriangle sorts BEFORE DegenerateTetrahedron, so the
    // report must present them in that order whichever was added first.
    REQUIRE(builder.addNode(mm(4, 4, 4)).has_value()); // 6, collinear with 1 and 5
    REQUIRE(builder
                .addTetrahedron({NodeId::fromValue(1), NodeId::fromValue(2), NodeId::fromValue(3),
                                 NodeId::fromValue(4)},
                                kRegion1)
                .has_value()); // coplanar, added FIRST
    REQUIRE(builder
                .addTriangle({NodeId::fromValue(1), NodeId::fromValue(5), NodeId::fromValue(6)}, kRegion1)
                .has_value()); // (0,0,0) (2,2,2) (4,4,4): collinear, added SECOND

    const MeshValidationReport report = validate(builder.build());
    REQUIRE(report.issues.size() == 2);
    // Kind order, not insertion order: the triangle was added second and is
    // reported first, because DegenerateTriangle precedes DegenerateTetrahedron.
    CHECK(report.issues[0].kind == MeshIssueKind::DegenerateTriangle);
    CHECK(report.issues[1].kind == MeshIssueKind::DegenerateTetrahedron);
    for (std::size_t i = 1; i < report.issues.size(); ++i) {
        CHECK(static_cast<std::uint8_t>(report.issues[i - 1].kind) <=
              static_cast<std::uint8_t>(report.issues[i].kind));
    }
}

TEST_CASE("Validate_RejectsANonFiniteDeterminantFromExtremeButFiniteCoordinates",
          "[meshing][data][validation]") {
    // Each coordinate is finite, but the triple product overflows to infinity.
    // A non-finite determinant is never evidence of a valid element, and must
    // not pass a "> 0" comparison that an infinity would answer "yes" to.
    const double huge = 1e300;
    const auto m = [](double x, double y, double z) {
        return Point3D{Length::fromSi(x), Length::fromSi(y), Length::fromSi(z)};
    };
    MeshBuilder builder;
    REQUIRE(builder.addNode(m(0, 0, 0)).has_value());
    REQUIRE(builder.addNode(m(huge, 0, 0)).has_value());
    REQUIRE(builder.addNode(m(0, huge, 0)).has_value());
    REQUIRE(builder.addNode(m(0, 0, huge)).has_value());
    REQUIRE(builder
                .addTetrahedron({NodeId::fromValue(1), NodeId::fromValue(2), NodeId::fromValue(3),
                                 NodeId::fromValue(4)},
                                kRegion1)
                .has_value());

    const Volume v = signedVolume(m(0, 0, 0), m(huge, 0, 0), m(0, huge, 0), m(0, 0, huge));
    REQUIRE_FALSE(isFinite(v));

    const MeshValidationReport report = validate(builder.build());
    CHECK_FALSE(report.dataValid());
    CHECK(hasKind(report, MeshIssueKind::DegenerateTetrahedron));
}

// ---------------------------------------------------------------------------
// Bounds
// ---------------------------------------------------------------------------

TEST_CASE("Bounds_OfAnEmptyMeshAreAbsentRatherThanAZeroBox", "[meshing][data][bounds]") {
    const Mesh empty;
    CHECK_FALSE(empty.bounds().has_value());

    // ... because a zero box at the origin is the RIGHT answer for a mesh with
    // one node at the origin, and the two must stay distinguishable.
    MeshBuilder builder;
    REQUIRE(builder.addNode(Point3D{}).has_value());
    const auto single = builder.build().bounds();
    REQUIRE(single.has_value());
    CHECK(single->min == Point3D{});
    CHECK(single->max == Point3D{});
}

TEST_CASE("Bounds_SpanEveryNodeIncludingNegativeCoordinates", "[meshing][data][bounds]") {
    MeshBuilder builder;
    REQUIRE(builder.addNode(mm(-5, 2, 9)).has_value());
    REQUIRE(builder.addNode(mm(3, -7, 1)).has_value());
    REQUIRE(builder.addNode(mm(0, 4, -2)).has_value());

    const auto bounds = builder.build().bounds();
    REQUIRE(bounds.has_value());
    CHECK(bounds->min == mm(-5, -7, -2));
    CHECK(bounds->max == mm(3, 4, 9));
    CHECK(bounds->min.x <= bounds->max.x);
    CHECK(bounds->min.y <= bounds->max.y);
    CHECK(bounds->min.z <= bounds->max.z);
}

TEST_CASE("Bounds_TranslateWithTheMesh", "[meshing][data][bounds]") {
    const Translation3D d{Length::fromSi(0.25), Length::fromSi(-0.5), Length::fromSi(2.0)};
    MeshBuilder original;
    MeshBuilder moved;
    for (int corner = 0; corner < 4; ++corner) {
        REQUIRE(original.addNode(unitTet(corner)).has_value());
        REQUIRE(moved.addNode(unitTet(corner) + d).has_value());
    }

    const auto before = original.build().bounds();
    const auto after = moved.build().bounds();
    REQUIRE(before.has_value());
    REQUIRE(after.has_value());
    CHECK(after->min == before->min + d);
    CHECK(after->max == before->max + d);
}

// ---------------------------------------------------------------------------
// Determinism
// ---------------------------------------------------------------------------

TEST_CASE("Mesh_EnumeratesNodesAndElementsInHandleOrderEveryTime", "[meshing][data][determinism]") {
    const auto buildOnce = [] {
        MeshBuilder builder;
        // Sparse, and deliberately not contiguous, so ordering cannot come from
        // an accident of density.
        REQUIRE(builder.addNode(NodeId::fromValue(2), mm(0, 0, 0)).has_value());
        REQUIRE(builder.addNode(NodeId::fromValue(5), mm(1, 0, 0)).has_value());
        REQUIRE(builder.addNode(NodeId::fromValue(9), mm(0, 1, 0)).has_value());
        REQUIRE(builder.addNode(NodeId::fromValue(40), mm(0, 0, 1)).has_value());
        REQUIRE(builder
                    .addTetrahedron({NodeId::fromValue(2), NodeId::fromValue(5), NodeId::fromValue(9),
                                     NodeId::fromValue(40)},
                                    kRegion1)
                    .has_value());
        REQUIRE(builder
                    .addTriangle({NodeId::fromValue(2), NodeId::fromValue(5), NodeId::fromValue(9)},
                                 kRegion1)
                    .has_value());
        return builder.build();
    };

    const Mesh first = buildOnce();
    for (int repeat = 0; repeat < 8; ++repeat) {
        const Mesh again = buildOnce();

        std::vector<NodeId::ValueType> firstNodes;
        std::vector<NodeId::ValueType> againNodes;
        for (const Node& n : first.nodes()) {
            firstNodes.push_back(n.id.value());
        }
        for (const Node& n : again.nodes()) {
            againNodes.push_back(n.id.value());
        }
        CHECK(firstNodes == againNodes);
        CHECK(firstNodes == std::vector<NodeId::ValueType>{2, 5, 9, 40});
        CHECK(std::ranges::is_sorted(againNodes));

        REQUIRE(again.tetrahedra().size() == 1);
        REQUIRE(again.triangles().size() == 1);
        CHECK(again.tetrahedra()[0].id == first.tetrahedra()[0].id);
        CHECK(again.triangles()[0].id == first.triangles()[0].id);
        CHECK(again.bounds() == first.bounds());
        CHECK(validate(again) == validate(first));
    }
}

TEST_CASE("Validate_ProducesAnIdenticalReportOnRepeatedRuns", "[meshing][data][determinism]") {
    MeshBuilder builder;
    for (int corner = 0; corner < 4; ++corner) {
        REQUIRE(builder.addNode(unitTet(corner)).has_value());
    }
    REQUIRE(builder.addNode(mm(0, 0, 0)).has_value());
    REQUIRE(builder.addNode(mm(1, 0, 0)).has_value());
    REQUIRE(builder.addNode(mm(0, 1, 0)).has_value());
    REQUIRE(builder.addNode(mm(1, 1, 0)).has_value());
    REQUIRE(builder
                .addTetrahedron({NodeId::fromValue(1), NodeId::fromValue(3), NodeId::fromValue(2),
                                 NodeId::fromValue(4)},
                                kRegion1)
                .has_value());
    REQUIRE(builder
                .addTetrahedron({NodeId::fromValue(5), NodeId::fromValue(6), NodeId::fromValue(7),
                                 NodeId::fromValue(8)},
                                kRegion1)
                .has_value());
    const Mesh mesh = builder.build();

    const MeshValidationReport first = validate(mesh);
    for (int repeat = 0; repeat < 8; ++repeat) {
        // Equality covers kind, handles and MESSAGE, so a report assembled from
        // an unordered container would be caught here.
        CHECK(validate(mesh) == first);
    }
}

TEST_CASE("MeshIssueKind_EveryKindHasAName", "[meshing][data][validation]") {
    const MeshIssueKind kinds[] = {
        MeshIssueKind::NonFiniteCoordinate,  MeshIssueKind::MissingNodeReference,
        MeshIssueKind::RepeatedNodeReference, MeshIssueKind::DegenerateTriangle,
        MeshIssueKind::DegenerateTetrahedron, MeshIssueKind::InvertedTetrahedron,
        MeshIssueKind::NodeSharedBetweenRegions, MeshIssueKind::MissingRegion,
        MeshIssueKind::EmptyMesh,
    };
    for (const MeshIssueKind kind : kinds) {
        CHECK_FALSE(toString(kind).empty());
        CHECK(toString(kind) != "unknown");
    }
}
