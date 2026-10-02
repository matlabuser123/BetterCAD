// P16-VOL-001: the Tet4 validation this milestone ADDS.
//
// Deliberately NOT a re-test of P16-DATA-001. That milestone already qualified
// the valid reference tetrahedron, the inverted one, the coplanar one, repeated
// and invalid node handles, non-finite coordinates, region sharing, the empty
// mesh, report ordering and report determinism, and re-asserting them here
// would make the suite longer without making it stronger. What is new is
// duplicate-tetrahedron detection, and the cases below are the ones that
// distinguish a real implementation of it from one that happens to pass.
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/meshing/MeshValidation.hpp>
#include <bettercad/meshing/SurfaceMesh.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include <vector>

using namespace bettercad;
using namespace bettercad::meshing;

namespace {

constexpr RegionId kRegion = RegionId::fromValue(1);

[[nodiscard]] Point3D mm(double x, double y, double z) {
    return Point3D{Length::fromSi(x * 1e-3), Length::fromSi(y * 1e-3), Length::fromSi(z * 1e-3)};
}

[[nodiscard]] NodeId n(NodeId::ValueType value) {
    return NodeId::fromValue(value);
}

/// Five nodes: a unit tetrahedron plus a fifth point, so a test can build a
/// second, genuinely different tetrahedron as a control.
[[nodiscard]] MeshBuilder fiveNodes() {
    MeshBuilder builder;
    REQUIRE(builder.addNode(mm(0, 0, 0)).has_value());
    REQUIRE(builder.addNode(mm(1, 0, 0)).has_value());
    REQUIRE(builder.addNode(mm(0, 1, 0)).has_value());
    REQUIRE(builder.addNode(mm(0, 0, 1)).has_value());
    REQUIRE(builder.addNode(mm(1, 1, 1)).has_value());
    return builder;
}

[[nodiscard]] std::vector<MeshIssueKind> kindsOf(const MeshValidationReport& report) {
    std::vector<MeshIssueKind> kinds;
    kinds.reserve(report.issues.size());
    for (const MeshIssue& issue : report.issues) {
        kinds.push_back(issue.kind);
    }
    return kinds;
}

} // namespace

TEST_CASE("Validate_RejectsTwoTetrahedraOnTheSameFourNodes", "[meshing][vol][validation]") {
    MeshBuilder builder = fiveNodes();
    REQUIRE(builder.addTetrahedron({n(1), n(2), n(3), n(4)}, kRegion).has_value());
    // The SAME four nodes in the SAME order. MeshBuilder accepts it -- a
    // topological duplicate is a whole-mesh question it deliberately does not
    // answer -- so validation has to.
    REQUIRE(builder.addTetrahedron({n(1), n(2), n(3), n(4)}, kRegion).has_value());

    const MeshValidationReport report = validate(builder.build());
    REQUIRE_FALSE(report.dataValid());
    REQUIRE(report.issues.size() == 1);
    CHECK(report.issues.front().kind == MeshIssueKind::DuplicateTetrahedron);
    // The SECOND element is named, the one conflicting with an element already
    // accepted, matching NodeSharedBetweenRegions' convention.
    CHECK(report.issues.front().element == ElementId::fromValue(2));
}

TEST_CASE("Validate_RejectsADuplicateWhoseNodesAreListedInADifferentOrder",
          "[meshing][vol][validation]") {
    MeshBuilder builder = fiveNodes();
    REQUIRE(builder.addTetrahedron({n(1), n(2), n(3), n(4)}, kRegion).has_value());
    // (1,2,3,4) -> (2,1,4,3) is an EVEN permutation, so this tetrahedron has the
    // same positive volume and the same four nodes. A duplicate check that
    // compared the handles in stored order would call these two different
    // elements and let one through.
    REQUIRE(builder.addTetrahedron({n(2), n(1), n(4), n(3)}, kRegion).has_value());

    const MeshValidationReport report = validate(builder.build());
    REQUIRE_FALSE(report.dataValid());
    CHECK(kindsOf(report) == std::vector{MeshIssueKind::DuplicateTetrahedron});
}

TEST_CASE("Validate_RejectsADuplicateThatIsAlsoInverted", "[meshing][vol][validation]") {
    MeshBuilder builder = fiveNodes();
    REQUIRE(builder.addTetrahedron({n(1), n(2), n(3), n(4)}, kRegion).has_value());
    // (1,2,3,4) -> (2,1,3,4) is a single swap: an ODD permutation, so this
    // occupies the same four nodes with NEGATIVE volume. It is both a duplicate
    // and inverted, and BOTH must be reported: a check that stopped at the
    // first would hide whichever it did not look for, and this is the more
    // dangerous pairing because the inverted twin is invisible to an
    // order-sensitive comparison.
    REQUIRE(builder.addTetrahedron({n(2), n(1), n(3), n(4)}, kRegion).has_value());

    const MeshValidationReport report = validate(builder.build());
    REQUIRE_FALSE(report.dataValid());
    // Enumeration order: InvertedTetrahedron precedes DuplicateTetrahedron.
    CHECK(kindsOf(report) ==
          std::vector{MeshIssueKind::InvertedTetrahedron, MeshIssueKind::DuplicateTetrahedron});
}

TEST_CASE("Validate_AcceptsTwoTetrahedraSharingAFace", "[meshing][vol][validation]") {
    // THE CONTROL THAT MAKES THE DUPLICATE TESTS MEAN SOMETHING. Two tetrahedra
    // sharing three of four nodes are what every real volume mesh is made of.
    // A duplicate check keyed on "shares nodes" rather than on the node SET
    // would reject this, and a mesher built on it would reject every mesh.
    MeshBuilder builder = fiveNodes();
    REQUIRE(builder.addTetrahedron({n(1), n(2), n(3), n(4)}, kRegion).has_value());
    REQUIRE(builder.addTetrahedron({n(2), n(3), n(4), n(5)}, kRegion).has_value());

    const MeshValidationReport report = validate(builder.build());
    INFO("issues: " << report.issues.size());
    CHECK(report.dataValid());
}

TEST_CASE("Validate_ReportsEveryDuplicateAgainstTheFirstElementAccepted",
          "[meshing][vol][validation]") {
    MeshBuilder builder = fiveNodes();
    REQUIRE(builder.addTetrahedron({n(1), n(2), n(3), n(4)}, kRegion).has_value());
    REQUIRE(builder.addTetrahedron({n(1), n(2), n(3), n(4)}, kRegion).has_value());
    REQUIRE(builder.addTetrahedron({n(2), n(1), n(4), n(3)}, kRegion).has_value());

    const MeshValidationReport report = validate(builder.build());
    // Three copies give TWO complaints, not one and not three: the first is the
    // element the others duplicate.
    REQUIRE(report.issues.size() == 2);
    CHECK(report.issues[0].element == ElementId::fromValue(2));
    CHECK(report.issues[1].element == ElementId::fromValue(3));
}

TEST_CASE("MeshBuilder_RejectsATetrahedronNamingANodeTheMeshDoesNotHave",
          "[meshing][vol][validation]") {
    MeshBuilder builder = fiveNodes();
    // Handle 99 is a valid handle that names no node of this mesh -- distinct
    // from the INVALID handle P16-DATA-001 already covers. It must not be
    // resolvable, which is the whole reason lookup is by identity rather than
    // by indexing into the node array.
    const Result<ElementId> added = builder.addTetrahedron({n(1), n(2), n(3), n(99)}, kRegion);
    REQUIRE_FALSE(added.has_value());
    CHECK(added.error().code == ErrorCode::NotFound);
    // Atomic: the rejected element left nothing behind.
    CHECK(builder.elementCount() == 0);
}

TEST_CASE("TetrahedralBoundary_OfOneTetrahedronIsItsFourFaces", "[meshing][vol][boundary]") {
    // The boundary extractor, tested DIRECTLY on connectivity whose answer is
    // known by counting rather than by meshing. The end-to-end conformity
    // checks exercise it on real meshes, but only ever positively; these two
    // cases pin what it means.
    MeshBuilder builder = fiveNodes();
    REQUIRE(builder.addTetrahedron({n(1), n(2), n(3), n(4)}, kRegion).has_value());
    const Mesh mesh = builder.build();

    const std::vector<std::array<NodeId, 3>> boundary = meshing::tetrahedralBoundary(mesh);
    CHECK(boundary.size() == 4);

    // Each face is wound to face OUT of its tetrahedron, so the four outward
    // normals enclose a positive volume: 1/6 sum of dot(a, cross(b, c)) over
    // the faces equals the tetrahedron's own volume. A boundary extractor that
    // got a winding wrong would still return four faces, and this is what
    // distinguishes it.
    MeshBuilder surfaceBuilder;
    for (const Node& node : mesh.nodes()) {
        REQUIRE(surfaceBuilder.addNode(node.id, node.position).has_value());
    }
    for (const std::array<NodeId, 3>& face : boundary) {
        REQUIRE(surfaceBuilder.addTriangle(face, kRegion).has_value());
    }
    const Mesh surface = surfaceBuilder.build();
    const Volume enclosed = meshing::enclosedVolume(surface);
    const Volume direct = meshing::signedVolume(mm(0, 0, 0), mm(1, 0, 0), mm(0, 1, 0), mm(0, 0, 1));
    CHECK(enclosed.si() > 0.0);
    CHECK(std::abs(enclosed.si() - direct.si()) < std::abs(direct.si()) * 1e-9);
}

TEST_CASE("TetrahedralBoundary_DropsTheFaceTwoTetrahedraShare", "[meshing][vol][boundary]") {
    // Two tetrahedra sharing the face (2,3,4): 8 faces in total, 2 of which are
    // the same one seen from both sides, so 6 remain. A boundary extractor that
    // keyed on winding rather than on the node SET would see two different
    // faces there and report 8 -- a mesh whose "boundary" included an interior
    // wall, which the conformity check would then reject for the wrong reason.
    MeshBuilder builder = fiveNodes();
    REQUIRE(builder.addTetrahedron({n(1), n(2), n(3), n(4)}, kRegion).has_value());
    REQUIRE(builder.addTetrahedron({n(2), n(3), n(4), n(5)}, kRegion).has_value());

    const std::vector<std::array<NodeId, 3>> boundary =
        meshing::tetrahedralBoundary(builder.build());
    CHECK(boundary.size() == 6);
}

TEST_CASE("TetrahedralVolume_SumsTheSignedVolumesWithoutTakingAbsoluteValues",
          "[meshing][vol][boundary]") {
    // Two tetrahedra of equal shape, one inverted. The sum must be ZERO, not
    // twice one of them: an abs() anywhere in the chain would make an inverted
    // element contribute as though it were correct, which is exactly the defect
    // the sign convention exists to expose.
    MeshBuilder builder = fiveNodes();
    REQUIRE(builder.addTetrahedron({n(1), n(2), n(3), n(4)}, kRegion).has_value());
    REQUIRE(builder.addTetrahedron({n(2), n(1), n(3), n(4)}, kRegion).has_value());

    const Volume total = meshing::tetrahedralVolume(builder.build());
    CHECK(std::abs(total.si()) < 1e-30);
}

TEST_CASE("MeshIssueKind_EveryKindHasAName", "[meshing][vol][validation]") {
    // Guards the enum against a kind being added with no name, which would
    // surface as an empty string in a diagnostic rather than as a failure.
    constexpr std::array kinds{
        MeshIssueKind::NonFiniteCoordinate,  MeshIssueKind::MissingNodeReference,
        MeshIssueKind::RepeatedNodeReference, MeshIssueKind::DegenerateTriangle,
        MeshIssueKind::DegenerateTetrahedron, MeshIssueKind::InvertedTetrahedron,
        MeshIssueKind::DuplicateTetrahedron,  MeshIssueKind::NodeSharedBetweenRegions,
        MeshIssueKind::MissingRegion,         MeshIssueKind::EmptyMesh,
    };
    std::vector<std::string_view> names;
    for (const MeshIssueKind kind : kinds) {
        const std::string_view name = toString(kind);
        INFO("kind " << static_cast<int>(kind));
        CHECK_FALSE(name.empty());
        CHECK(name != "unknown");
        names.push_back(name);
    }
    // And every name is distinct, so two kinds cannot be confused in a log.
    std::ranges::sort(names);
    CHECK(std::ranges::adjacent_find(names) == names.end());
}
