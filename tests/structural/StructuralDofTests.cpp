// P17-DOF-001: the degree-of-freedom numbering and the constraint model.
//
// WHAT THESE TESTS ARE FOR. The milestone's claims are about an index space,
// and an index space is exactly the kind of thing that can be wrong in a way
// every spot check passes: a numbering that is off by one for the last node, a
// free numbering with a gap in it, a map that survives a remesh it should not.
// So the properties below are checked EXHAUSTIVELY where that is affordable --
// over every degree of freedom of several meshes, in both directions -- rather
// than on a chosen index.
//
// THE MESHES ARE BUILT BY HAND, DELIBERATELY. `MeshBuilder` is the only way to
// produce a mesh with SPARSE node handles, and sparse handles are the case the
// whole design exists for: a generator that skipped numbers would defeat
// `3 * nodeId.value() + component` and nothing in the current backend produces
// one. The reference-model integration, where the meshes come from Netgen
// through the ordinary pipeline, is in tests/reference/.
//
// NOTHING HERE SOLVES ANYTHING. There is no stiffness matrix and no
// displacement with a physical meaning; the numbers are indices.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/structural/StructuralDof.hpp>
#include <bettercad/structural/StructuralResult.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <compare>
#include <cstdint>
#include <limits>
#include <memory>
#include <numeric>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using meshing::NodeId;
using structural::ConstraintProblem;
using structural::ConstraintSet;
using structural::DofComponent;
using structural::DofIndex;
using structural::DofMapProblem;
using structural::FreeEquationIndex;
using structural::FreeEquationMap;
using structural::MeshDofMap;
using structural::NodalDof;
using structural::kDofComponents;
using structural::kDofsPerNode;

namespace {

[[nodiscard]] Point3D at(double xMm, double yMm, double zMm) {
    return Point3D{Length::fromSi(xMm * 1e-3), Length::fromSi(yMm * 1e-3),
                   Length::fromSi(zMm * 1e-3)};
}

/// A mesh carrying exactly the node handles asked for, at distinct positions.
///
/// Positions are derived from the handle so no two nodes coincide, which keeps
/// the mesh a valid container; nothing in this file depends on where a node is.
[[nodiscard]] meshing::Mesh meshWithNodes(const std::vector<NodeId::ValueType>& handles) {
    meshing::MeshBuilder builder;
    for (const NodeId::ValueType handle : handles) {
        const Result<NodeId> added =
            builder.addNode(NodeId::fromValue(handle), at(static_cast<double>(handle), 0.0, 0.0));
        INFO((added.has_value() ? std::string{} : added.error().message));
        REQUIRE(added.has_value());
    }
    return builder.build();
}

/// `count` nodes with the dense handles a generator allocates: 1 .. count.
[[nodiscard]] meshing::Mesh denseMesh(NodeId::ValueType count) {
    std::vector<NodeId::ValueType> handles(count);
    std::iota(handles.begin(), handles.end(), NodeId::ValueType{1});
    return meshWithNodes(handles);
}

[[nodiscard]] MeshDofMap mapOf(const meshing::Mesh& mesh) {
    Result<MeshDofMap> map = structural::buildMeshDofMap(mesh);
    INFO((map.has_value() ? std::string{} : map.error().message));
    REQUIRE(map.has_value());
    return std::move(*map);
}

[[nodiscard]] ConstraintSet constraintsOf(const MeshDofMap& map,
                                          const std::vector<NodalDof>& prescribed) {
    Result<ConstraintSet> set = structural::buildConstraintSet(map, prescribed);
    INFO((set.has_value() ? std::string{} : set.error().message));
    REQUIRE(set.has_value());
    return std::move(*set);
}

[[nodiscard]] FreeEquationMap equationsOf(const MeshDofMap& map, const ConstraintSet& set) {
    Result<FreeEquationMap> equations = structural::buildFreeEquationMap(map, set);
    INFO((equations.has_value() ? std::string{} : equations.error().message));
    REQUIRE(equations.has_value());
    return std::move(*equations);
}

[[nodiscard]] DofIndex dofOf(const MeshDofMap& map, NodeId node, DofComponent component) {
    Result<DofIndex> index = map.indexOf(NodalDof{.node = node, .component = component});
    INFO((index.has_value() ? std::string{} : index.error().message));
    REQUIRE(index.has_value());
    return *index;
}

/// A block, meshed through the ordinary pipeline: the fixture for the tests
/// that have to agree with a real mesh and a real `StructuralResult`.
struct MeshedBlock {
    Document document{"Part"};
    features::Regenerator regenerator;
    meshing::Mesher mesher;
    ObjectId feature{};
    MeshControlId control{};

    MeshedBlock() {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
        const ObjectId profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = 20_mm});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));

        auto intent =
            meshing::MeshControl::create("Mesh", meshing::MeshControlDefinition{.body = feature});
        REQUIRE(intent.has_value());
        control = MeshControlId::fromValue(require(document.addObject(std::move(*intent))).value());
        requireReport(regenerator, document);

        const Result<const meshing::VolumeMesh*> mesh =
            mesher.generate(document, regenerator, control);
        INFO((mesh.has_value() ? std::string{} : mesh.error().message));
        REQUIRE(mesh.has_value());
    }

    [[nodiscard]] const meshing::VolumeMesh& volume() const {
        const meshing::VolumeMesh* held = mesher.mesh(control);
        REQUIRE(held != nullptr);
        return *held;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// The numbering
// ---------------------------------------------------------------------------

TEST_CASE("MeshDofMap_GivesEveryNodeExactlyThreeTranslationalDofs", "[structural][dof]") {
    // Three per node and nothing else: the kinematics of a solid continuum
    // element, asserted on the count rather than described.
    STATIC_REQUIRE(kDofsPerNode == 3);
    STATIC_REQUIRE(kDofComponents.size() == kDofsPerNode);

    for (const NodeId::ValueType count : {NodeId::ValueType{1}, NodeId::ValueType{2},
                                          NodeId::ValueType{7}, NodeId::ValueType{64}}) {
        const meshing::Mesh mesh = denseMesh(count);
        const MeshDofMap map = mapOf(mesh);
        INFO("node count " << count);
        CHECK(map.nodeCount() == count);
        CHECK(map.dofCount() == 3ULL * count);

        // Every node resolves to three DISTINCT indices, and the union of them
        // over all nodes is exactly 1 .. 3N with no gap and no repeat. That is
        // the whole claim of a numbering, and it is checked as a set rather
        // than by trusting the formula.
        std::set<DofIndex::ValueType> seen;
        for (const meshing::Node& node : mesh.nodes()) {
            const Result<std::array<DofIndex, kDofsPerNode>> three = map.indicesOf(node.id);
            REQUIRE(three.has_value());
            for (const DofIndex index : *three) {
                CHECK(index.isValid());
                CHECK(map.contains(index));
                CHECK(seen.insert(index.value()).second);
            }
        }
        REQUIRE(seen.size() == map.dofCount());
        CHECK(*seen.begin() == 1);
        CHECK(*seen.rbegin() == map.dofCount());
    }
}

TEST_CASE("MeshDofMap_NumbersANodesThreeDofsConsecutivelyRatherThanInComponentBlocks",
          "[structural][dof]") {
    // THE CONVENTION, FROZEN. This test exists to fail if anyone changes it,
    // because P17-ELEM-001's twelve-component element vector and
    // P17-ASSEMBLY-001's bandwidth both depend on it.
    const meshing::Mesh mesh = denseMesh(5);
    const MeshDofMap map = mapOf(mesh);

    for (std::size_t ordinal = 0; ordinal < map.nodeCount(); ++ordinal) {
        const NodeId node = map.nodeAt(ordinal);
        REQUIRE(node.isValid());
        INFO("ordinal " << ordinal);
        CHECK(dofOf(map, node, DofComponent::Ux).value() == 3 * ordinal + 1);
        CHECK(dofOf(map, node, DofComponent::Uy).value() == 3 * ordinal + 2);
        CHECK(dofOf(map, node, DofComponent::Uz).value() == 3 * ordinal + 3);
    }

    // And the discriminating half: under COMPONENT BLOCKING the second node's
    // Ux would be index 2, and all five Ux would be 1..5. Both are asserted
    // false, so a switch to the other convention cannot pass this test by
    // satisfying only the formula above.
    const NodeId second = map.nodeAt(1);
    CHECK(dofOf(map, second, DofComponent::Ux).value() != 2);
    CHECK(dofOf(map, second, DofComponent::Ux).value() == 4);
    CHECK(dofOf(map, map.nodeAt(4), DofComponent::Ux).value() == 13);
}

TEST_CASE("MeshDofMap_IndexAndDofAreExactInversesForEveryDegreeOfFreedom", "[structural][dof]") {
    // Both directions, over every DOF of a dense mesh and of a deliberately
    // sparse one. A numbering whose round trip is not the identity is numbering
    // two things the same, and a spot check would not see it.
    const std::vector<std::vector<NodeId::ValueType>> fixtures{
        {1, 2, 3, 4, 5, 6, 7, 8},
        {3, 1000, 9000000},
        {1, 4, 10, 11, 4294967295U},
    };
    for (const std::vector<NodeId::ValueType>& handles : fixtures) {
        const meshing::Mesh mesh = meshWithNodes(handles);
        const MeshDofMap map = mapOf(mesh);
        INFO("nodes " << handles.size());

        for (DofIndex::ValueType value = 1; value <= map.dofCount(); ++value) {
            const DofIndex index = DofIndex::fromValue(value);
            const Result<NodalDof> dof = map.dofAt(index);
            REQUIRE(dof.has_value());
            const Result<DofIndex> back = map.indexOf(*dof);
            REQUIRE(back.has_value());
            CHECK(*back == index);
        }

        for (const meshing::Node& node : mesh.nodes()) {
            for (const DofComponent component : kDofComponents) {
                const NodalDof dof{.node = node.id, .component = component};
                const Result<DofIndex> index = map.indexOf(dof);
                REQUIRE(index.has_value());
                const Result<NodalDof> back = map.dofAt(*index);
                REQUIRE(back.has_value());
                CHECK(*back == dof);
            }
        }
    }
}

TEST_CASE("MeshDofMap_NumbersSparseNodeHandlesDenselyRatherThanByHandleValue",
          "[structural][dof]") {
    // THE DEFECT THE DESIGN EXISTS TO PREVENT, with the brief's own numbers.
    const meshing::Mesh mesh = meshWithNodes({3, 1000, 9000000});
    const MeshDofMap map = mapOf(mesh);

    REQUIRE(map.nodeCount() == 3);
    CHECK(map.dofCount() == 9);
    // `3 * nodeId.value() + component` would give 27000003 here, which is the
    // number asserted against rather than merely avoided.
    CHECK(map.dofCount() != 27000003);

    CHECK(map.nodeOrdinal(NodeId::fromValue(3)) == std::optional<std::size_t>{0});
    CHECK(map.nodeOrdinal(NodeId::fromValue(1000)) == std::optional<std::size_t>{1});
    CHECK(map.nodeOrdinal(NodeId::fromValue(9000000)) == std::optional<std::size_t>{2});

    CHECK(dofOf(map, NodeId::fromValue(3), DofComponent::Ux).value() == 1);
    CHECK(dofOf(map, NodeId::fromValue(1000), DofComponent::Ux).value() == 4);
    CHECK(dofOf(map, NodeId::fromValue(9000000), DofComponent::Uz).value() == 9);

    // And the handles that are NOT nodes of this mesh get nothing, including
    // the ones a dense numbering would have produced.
    for (const NodeId::ValueType absent : {1U, 2U, 4U, 999U, 1001U, 8999999U, 9000001U}) {
        INFO("absent handle " << absent);
        CHECK_FALSE(map.nodeOrdinal(NodeId::fromValue(absent)).has_value());
        CHECK_FALSE(map.indexOf(NodalDof{.node = NodeId::fromValue(absent),
                                         .component = DofComponent::Ux})
                        .has_value());
    }
}

TEST_CASE("MeshDofMap_OrdinalsFollowTheMeshOwnNodeEnumeration", "[structural][dof]") {
    // The ordinal is NOT invented here: it is the mesh's own index. Asserted
    // element by element against `mesh.nodes()`, including for a mesh whose
    // handles are sparse, so the two can never be read as different orders.
    const meshing::Mesh mesh = meshWithNodes({2, 5, 9, 40, 41, 100});
    const MeshDofMap map = mapOf(mesh);

    REQUIRE(map.nodes().size() == mesh.nodes().size());
    for (std::size_t i = 0; i < mesh.nodes().size(); ++i) {
        INFO("ordinal " << i);
        CHECK(map.nodes()[i] == mesh.nodes()[i].id);
        CHECK(map.nodeAt(i) == mesh.nodes()[i].id);
        CHECK(map.nodeOrdinal(mesh.nodes()[i].id) == std::optional<std::size_t>{i});
    }
    // Ascending, which is what makes the binary search correct.
    CHECK(std::ranges::is_sorted(map.nodes()));
    CHECK(std::ranges::adjacent_find(map.nodes()) == map.nodes().end());
    // Out of range gives an invalid handle, not the first node.
    CHECK_FALSE(map.nodeAt(map.nodeCount()).isValid());
}

TEST_CASE("MeshDofMap_UsesTheSameNodeOrdinalAsAStructuralResult", "[structural][dof]") {
    // THE CROSS-CONVENTION TEST. P17-DATA-001 keyed a result's displacement
    // array "parallel to mesh.nodes()", and this milestone keys its DOFs the
    // same way. If the two ever diverge a solver would write node i's answer
    // into node j's slot, and the numbers would look plausible.
    //
    // Checked against the RESULT's own lookup rather than against the array
    // index, so two independent APIs are compared.
    MeshedBlock part;
    const meshing::VolumeMesh& volume = part.volume();
    const MeshDofMap map = mapOf(volume.mesh());

    REQUIRE(map.nodeCount() == volume.nodeCount());
    CHECK(map.describes(volume));

    std::vector<Translation3D> displacements;
    displacements.reserve(volume.nodeCount());
    for (std::size_t i = 0; i < volume.nodeCount(); ++i) {
        // The ordinal, encoded: entry i carries i, so a mismatch is visible as
        // a wrong number rather than as a wrong node.
        displacements.push_back(Translation3D{Length::fromSi(static_cast<double>(i)),
                                              Length::fromSi(0.0), Length::fromSi(0.0)});
    }

    structural::StructuralResultSource source;
    source.body = part.feature;
    source.control = part.control;
    source.mesh = volume.mesh().stamp();
    Result<structural::StructuralResult> result = structural::StructuralResult::create(
        source, volume, displacements, {},
        std::vector<structural::Strain6>(volume.tetrahedronCount()),
        std::vector<structural::Stress6>(volume.tetrahedronCount()));
    INFO((result.has_value() ? std::string{} : result.error().message));
    REQUIRE(result.has_value());
    REQUIRE(result->displacements().size() == map.nodeCount());

    for (std::size_t ordinal = 0; ordinal < map.nodeCount(); ++ordinal) {
        const NodeId node = map.nodeAt(ordinal);
        REQUIRE(node.isValid());
        const Result<Translation3D> found = result->displacementOf(volume, node);
        REQUIRE(found.has_value());
        INFO("ordinal " << ordinal << " node " << node.value());
        CHECK(found->x.si() == static_cast<double>(ordinal));

        // And the DOF indices of that node address the same ordinal.
        const Result<NodalDof> first = map.dofAt(DofIndex::fromValue(3 * ordinal + 1));
        REQUIRE(first.has_value());
        CHECK(first->node == node);
    }
}

TEST_CASE("MeshDofMap_RefusesAMeshWithNothingToNumber", "[structural][dof]") {
    SECTION("a mesh with no nodes") {
        const meshing::Mesh empty = meshing::MeshBuilder{}.build();
        REQUIRE(empty.stamp().isValid());
        REQUIRE(empty.nodeCount() == 0);
        CHECK(structural::dofMapProblem(empty) ==
              std::optional<DofMapProblem>{DofMapProblem::MeshHasNoNodes});
        const Result<MeshDofMap> map = structural::buildMeshDofMap(empty);
        REQUIRE_FALSE(map.has_value());
        CHECK(map.error().code == ErrorCode::FailedPrecondition);
        CHECK_THAT(map.error().message, ContainsSubstring("no nodes"));
    }

    SECTION("a mesh with no identity cannot be bound to") {
        const meshing::Mesh none;
        REQUIRE_FALSE(none.stamp().isValid());
        CHECK(structural::dofMapProblem(none) ==
              std::optional<DofMapProblem>{DofMapProblem::MeshHasNoIdentity});
        const Result<MeshDofMap> map = structural::buildMeshDofMap(none);
        REQUIRE_FALSE(map.has_value());
        CHECK(map.error().code == ErrorCode::FailedPrecondition);
        CHECK_THAT(map.error().message, ContainsSubstring("identity"));
    }

    SECTION("every DofMapProblem is reached") {
        CHECK(structural::toString(DofMapProblem::MeshHasNoNodes) == "mesh_has_no_nodes");
        CHECK(structural::toString(DofMapProblem::MeshHasNoIdentity) == "mesh_has_no_identity");
    }
}

TEST_CASE("MeshDofMap_RefusesADegreeOfFreedomItDoesNotHave", "[structural][dof]") {
    const meshing::Mesh mesh = denseMesh(4);
    const MeshDofMap map = mapOf(mesh);

    SECTION("a node the mesh does not have") {
        const Result<DofIndex> index =
            map.indexOf(NodalDof{.node = NodeId::fromValue(99), .component = DofComponent::Ux});
        REQUIRE_FALSE(index.has_value());
        CHECK(index.error().code == ErrorCode::NotFound);
        CHECK_THAT(index.error().message, ContainsSubstring("node:99"));
        CHECK_FALSE(map.indicesOf(NodeId::fromValue(99)).has_value());
    }

    SECTION("an invalid handle is not the first node") {
        REQUIRE_FALSE(NodeId{}.isValid());
        CHECK_FALSE(map.nodeOrdinal(NodeId{}).has_value());
        CHECK_FALSE(map.indexOf(NodalDof{.node = NodeId{}, .component = DofComponent::Ux})
                        .has_value());
    }

    SECTION("a component that is not one of the three") {
        // Reachable: DofComponent is a uint8_t enum, so a value cast from an
        // out-of-range integer is representable and names nothing.
        const auto bogus = static_cast<DofComponent>(7);
        CHECK_FALSE(structural::isRecognised(bogus));
        CHECK(structural::offsetOf(bogus) == kDofsPerNode);
        const Result<DofIndex> index =
            map.indexOf(NodalDof{.node = map.nodeAt(0), .component = bogus});
        REQUIRE_FALSE(index.has_value());
        CHECK(index.error().code == ErrorCode::InvalidArgument);
        CHECK_THAT(index.error().message, ContainsSubstring("displacement component"));
    }

    SECTION("an index outside one to three N") {
        CHECK_FALSE(map.contains(DofIndex{}));
        CHECK_FALSE(map.dofAt(DofIndex{}).has_value());
        CHECK(map.contains(DofIndex::fromValue(1)));
        CHECK(map.contains(DofIndex::fromValue(map.dofCount())));
        CHECK_FALSE(map.contains(DofIndex::fromValue(map.dofCount() + 1)));
        const Result<NodalDof> past = map.dofAt(DofIndex::fromValue(map.dofCount() + 1));
        REQUIRE_FALSE(past.has_value());
        CHECK(past.error().code == ErrorCode::NotFound);
    }

    SECTION("the three recognised components are recognised") {
        for (const DofComponent component : kDofComponents) {
            CHECK(structural::isRecognised(component));
            CHECK(structural::componentAt(structural::offsetOf(component)) == component);
        }
    }
}

TEST_CASE("MeshDofMap_IsBoundToTheMeshItWasBuiltFrom", "[structural][dof]") {
    const meshing::Mesh first = denseMesh(4);
    const meshing::Mesh second = denseMesh(4);
    const MeshDofMap map = mapOf(first);

    // Same node count, same numeric handles, different mesh.
    REQUIRE(first.nodeCount() == second.nodeCount());
    REQUIRE(first.nodes()[0].id == second.nodes()[0].id);
    REQUIRE(first.stamp() != second.stamp());

    CHECK(map.describes(first));
    CHECK_FALSE(map.describes(second));
    CHECK(map.stamp() == first.stamp());

    // A copy of a mesh keeps its stamp, so it is the SAME mesh by identity and
    // the numbering still describes it. That is `Mesh`'s documented behaviour
    // -- "copying a mesh ... is not a remesh" -- and the test says so, because
    // a reader could reasonably expect the opposite.
    const meshing::Mesh copied = first;
    CHECK(map.describes(copied));
}

TEST_CASE("MeshDofMap_NumberingIsIdenticalOverRepeatedBuildsOfTheSameMesh", "[structural][dof]") {
    const meshing::Mesh mesh = meshWithNodes({1, 4, 10, 11, 500});
    const MeshDofMap first = mapOf(mesh);

    for (int repeat = 0; repeat < 16; ++repeat) {
        const MeshDofMap again = mapOf(mesh);
        INFO("repeat " << repeat);
        CHECK(again == first);
        CHECK(again.dofCount() == first.dofCount());
        CHECK(std::ranges::equal(again.nodes(), first.nodes()));
        for (DofIndex::ValueType value = 1; value <= first.dofCount(); ++value) {
            const Result<NodalDof> a = first.dofAt(DofIndex::fromValue(value));
            const Result<NodalDof> b = again.dofAt(DofIndex::fromValue(value));
            REQUIRE(a.has_value());
            REQUIRE(b.has_value());
            CHECK(*a == *b);
        }
    }
}

// ---------------------------------------------------------------------------
// Constraints
// ---------------------------------------------------------------------------

TEST_CASE("ConstraintSet_SortsPrescribedDofsIntoACanonicalAscendingSet", "[structural][dof]") {
    const meshing::Mesh mesh = denseMesh(4);
    const MeshDofMap map = mapOf(mesh);

    const std::vector<NodalDof> prescribed{
        NodalDof{.node = map.nodeAt(2), .component = DofComponent::Uz},
        NodalDof{.node = map.nodeAt(0), .component = DofComponent::Uy},
        NodalDof{.node = map.nodeAt(3), .component = DofComponent::Ux},
    };
    const ConstraintSet set = constraintsOf(map, prescribed);

    REQUIRE(set.size() == 3);
    CHECK(std::ranges::is_sorted(set.constrained()));
    CHECK(set.stamp() == mesh.stamp());
    CHECK_FALSE(set.isEmpty());

    // The exact indices, so a silently reordered or renumbered set is visible.
    CHECK(set.constrained()[0] == DofIndex::fromValue(2));  // node 0, Uy
    CHECK(set.constrained()[1] == DofIndex::fromValue(9));  // node 2, Uz
    CHECK(set.constrained()[2] == DofIndex::fromValue(10)); // node 3, Ux

    for (const NodalDof& dof : prescribed) {
        CHECK(set.contains(dofOf(map, dof.node, dof.component)));
    }
    // And nothing else is in it.
    for (DofIndex::ValueType value = 1; value <= map.dofCount(); ++value) {
        const DofIndex index = DofIndex::fromValue(value);
        const bool expected = value == 2 || value == 9 || value == 10;
        INFO("dof " << value);
        CHECK(set.contains(index) == expected);
    }
}

TEST_CASE("ConstraintSet_IsIndependentOfTheOrderConstraintsAreListedIn", "[structural][dof]") {
    // Order independence is the property an assembler relies on without
    // knowing it: two restraint features applied in either order must give the
    // same equations. Checked over every permutation of a small set rather than
    // on one shuffle.
    const meshing::Mesh mesh = denseMesh(3);
    const MeshDofMap map = mapOf(mesh);

    std::vector<NodalDof> prescribed{
        NodalDof{.node = map.nodeAt(0), .component = DofComponent::Ux},
        NodalDof{.node = map.nodeAt(1), .component = DofComponent::Uz},
        NodalDof{.node = map.nodeAt(2), .component = DofComponent::Uy},
        NodalDof{.node = map.nodeAt(2), .component = DofComponent::Ux},
    };
    std::ranges::sort(prescribed);
    const ConstraintSet canonical = constraintsOf(map, prescribed);
    const FreeEquationMap canonicalEquations = equationsOf(map, canonical);

    int permutations = 0;
    do {
        ++permutations;
        const ConstraintSet set = constraintsOf(map, prescribed);
        CHECK(set == canonical);
        CHECK(std::ranges::equal(set.constrained(), canonical.constrained()));
        // And the consequence, which is the thing that actually matters.
        const FreeEquationMap equations = equationsOf(map, set);
        CHECK(equations == canonicalEquations);
        CHECK(std::ranges::equal(equations.freeDofs(), canonicalEquations.freeDofs()));
    } while (std::ranges::next_permutation(prescribed).found);
    CHECK(permutations == 24);
}

TEST_CASE("ConstraintSet_RefusesAnUnusableConstraint", "[structural][dof]") {
    const meshing::Mesh mesh = denseMesh(4);
    const MeshDofMap map = mapOf(mesh);

    SECTION("the same degree of freedom twice") {
        const std::vector<NodalDof> prescribed{
            NodalDof{.node = map.nodeAt(1), .component = DofComponent::Uy},
            NodalDof{.node = map.nodeAt(0), .component = DofComponent::Ux},
            NodalDof{.node = map.nodeAt(1), .component = DofComponent::Uy},
        };
        CHECK(structural::constraintProblem(map, prescribed) ==
              std::optional<ConstraintProblem>{ConstraintProblem::DuplicateDof});
        const Result<ConstraintSet> set = structural::buildConstraintSet(map, prescribed);
        REQUIRE_FALSE(set.has_value());
        CHECK(set.error().code == ErrorCode::InvalidArgument);
        CHECK_THAT(set.error().message, ContainsSubstring("more than once"));
        // The diagnostic names the offending degree of freedom, not just the
        // fact of a duplicate.
        CHECK_THAT(set.error().message, ContainsSubstring("node:2"));
        CHECK_THAT(set.error().message, ContainsSubstring("uy"));
    }

    SECTION("the same NODE twice is not a duplicate if the components differ") {
        const std::vector<NodalDof> prescribed{
            NodalDof{.node = map.nodeAt(1), .component = DofComponent::Ux},
            NodalDof{.node = map.nodeAt(1), .component = DofComponent::Uy},
        };
        CHECK_FALSE(structural::constraintProblem(map, prescribed).has_value());
        CHECK(constraintsOf(map, prescribed).size() == 2);
    }

    SECTION("a node the mesh does not have") {
        const std::vector<NodalDof> prescribed{
            NodalDof{.node = map.nodeAt(0), .component = DofComponent::Ux},
            NodalDof{.node = NodeId::fromValue(77), .component = DofComponent::Ux},
        };
        CHECK(structural::constraintProblem(map, prescribed) ==
              std::optional<ConstraintProblem>{ConstraintProblem::NodeNotInMesh});
        const Result<ConstraintSet> set = structural::buildConstraintSet(map, prescribed);
        REQUIRE_FALSE(set.has_value());
        CHECK(set.error().code == ErrorCode::NotFound);
        CHECK_THAT(set.error().message, ContainsSubstring("node:77"));
    }

    SECTION("a node from a different mesh, even with a handle this one also has") {
        const meshing::Mesh other = denseMesh(4);
        const MeshDofMap otherMap = mapOf(other);
        // The handle IS a node of both meshes, so the refusal below is about
        // the numbering it is checked against and not about the handle.
        const std::vector<NodalDof> prescribed{
            NodalDof{.node = otherMap.nodeAt(0), .component = DofComponent::Ux}};
        CHECK_FALSE(structural::constraintProblem(map, prescribed).has_value());
        // Which is why the stamp check lives on buildFreeEquationMap: a handle
        // alone cannot tell the two meshes apart, and the SET can.
        const ConstraintSet set = constraintsOf(otherMap, prescribed);
        CHECK_FALSE(structural::buildFreeEquationMap(map, set).has_value());
    }

    SECTION("an invalid node handle") {
        const std::vector<NodalDof> prescribed{
            NodalDof{.node = NodeId{}, .component = DofComponent::Uz}};
        CHECK(structural::constraintProblem(map, prescribed) ==
              std::optional<ConstraintProblem>{ConstraintProblem::NodeNotInMesh});
    }

    SECTION("a component that is not one of the three") {
        const std::vector<NodalDof> prescribed{
            NodalDof{.node = map.nodeAt(0), .component = static_cast<DofComponent>(9)}};
        CHECK(structural::constraintProblem(map, prescribed) ==
              std::optional<ConstraintProblem>{ConstraintProblem::ComponentNotRecognised});
        const Result<ConstraintSet> set = structural::buildConstraintSet(map, prescribed);
        REQUIRE_FALSE(set.has_value());
        CHECK(set.error().code == ErrorCode::InvalidArgument);
    }

    SECTION("every ConstraintProblem is reached, and each has a name") {
        CHECK(structural::toString(ConstraintProblem::NodeNotInMesh) == "node_not_in_mesh");
        CHECK(structural::toString(ConstraintProblem::ComponentNotRecognised) ==
              "component_not_recognised");
        CHECK(structural::toString(ConstraintProblem::DuplicateDof) == "duplicate_dof");
    }
}

TEST_CASE("ConstraintSet_AcceptsAModelWithNoConstraintsAtAll", "[structural][dof]") {
    // An unrestrained model is a real model with a singular stiffness matrix.
    // Refusing to NUMBER it would report a physics problem as a numbering
    // failure, and P17-SOLVE-001 is where the rigid-body modes are visible.
    const meshing::Mesh mesh = denseMesh(3);
    const MeshDofMap map = mapOf(mesh);

    CHECK_FALSE(structural::constraintProblem(map, {}).has_value());
    const ConstraintSet set = constraintsOf(map, {});
    CHECK(set.isEmpty());
    CHECK(set.size() == 0);
    CHECK(set.stamp() == mesh.stamp());
    CHECK_FALSE(set.contains(DofIndex::fromValue(1)));

    const FreeEquationMap equations = equationsOf(map, set);
    CHECK(equations.freeCount() == 9);
    CHECK(equations.constrainedCount() == 0);
}

TEST_CASE("ConstraintSet_FullyFixedDofsNamesAllThreeComponentsOfEveryNode", "[structural][dof]") {
    const meshing::Mesh mesh = meshWithNodes({5, 6, 90});
    const MeshDofMap map = mapOf(mesh);

    const std::vector<NodeId> nodes{NodeId::fromValue(5), NodeId::fromValue(90)};
    const std::vector<NodalDof> dofs = structural::fullyFixedDofs(nodes);
    REQUIRE(dofs.size() == 6);
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        for (std::size_t c = 0; c < kDofsPerNode; ++c) {
            INFO("node " << i << " component " << c);
            CHECK(dofs[i * kDofsPerNode + c].node == nodes[i]);
            CHECK(dofs[i * kDofsPerNode + c].component == kDofComponents[c]);
        }
    }

    const ConstraintSet set = constraintsOf(map, dofs);
    CHECK(set.size() == 6);
    // Node 6, which was not named, keeps all three of its DOFs.
    const Result<std::array<DofIndex, kDofsPerNode>> free = map.indicesOf(NodeId::fromValue(6));
    REQUIRE(free.has_value());
    for (const DofIndex index : *free) {
        CHECK_FALSE(set.contains(index));
    }

    // It does NO validation of its own, which is why buildConstraintSet is
    // still the gate: an unknown node passes through the helper and is refused
    // by the set.
    const std::vector<NodalDof> unknown =
        structural::fullyFixedDofs(std::vector<NodeId>{NodeId::fromValue(4242)});
    CHECK(unknown.size() == 3);
    CHECK(structural::constraintProblem(map, unknown) ==
          std::optional<ConstraintProblem>{ConstraintProblem::NodeNotInMesh});

    CHECK(structural::fullyFixedDofs({}).empty());
}

// ---------------------------------------------------------------------------
// The free equation numbering
// ---------------------------------------------------------------------------

TEST_CASE("FreeEquationMap_NumbersTheUnknownsCompactlyFromZero", "[structural][dof]") {
    const meshing::Mesh mesh = denseMesh(4);
    const MeshDofMap map = mapOf(mesh);
    // Fix node 0 entirely and node 2's y: four of twelve.
    std::vector<NodalDof> prescribed =
        structural::fullyFixedDofs(std::vector<NodeId>{map.nodeAt(0)});
    prescribed.push_back(NodalDof{.node = map.nodeAt(2), .component = DofComponent::Uy});
    const ConstraintSet set = constraintsOf(map, prescribed);
    const FreeEquationMap equations = equationsOf(map, set);

    REQUIRE(equations.dofCount() == 12);
    CHECK(equations.freeCount() == 8);
    CHECK(equations.constrainedCount() == 4);
    CHECK(equations.stamp() == mesh.stamp());

    // COMPACT AND GAP-FREE: the equation numbers are exactly 0 .. Nfree-1,
    // each used once, checked as a set.
    std::set<FreeEquationIndex::ValueType> rows;
    for (DofIndex::ValueType value = 1; value <= equations.dofCount(); ++value) {
        const std::optional<FreeEquationIndex> row = equations.equationOf(DofIndex::fromValue(value));
        if (row.has_value()) {
            CHECK(rows.insert(row->value()).second);
        }
    }
    REQUIRE(rows.size() == equations.freeCount());
    CHECK(*rows.begin() == 0);
    CHECK(*rows.rbegin() == equations.freeCount() - 1);

    // The exact assignment, so a renumbering is visible: DOFs 1,2,3 and 8 are
    // constrained, so DOF 4 is row 0 and DOF 12 is row 7.
    CHECK(equations.equationOf(DofIndex::fromValue(4)) ==
          std::optional<FreeEquationIndex>{FreeEquationIndex::fromValue(0)});
    CHECK(equations.equationOf(DofIndex::fromValue(7)) ==
          std::optional<FreeEquationIndex>{FreeEquationIndex::fromValue(3)});
    CHECK(equations.equationOf(DofIndex::fromValue(9)) ==
          std::optional<FreeEquationIndex>{FreeEquationIndex::fromValue(4)});
    CHECK(equations.equationOf(DofIndex::fromValue(12)) ==
          std::optional<FreeEquationIndex>{FreeEquationIndex::fromValue(7)});
}

TEST_CASE("FreeEquationMap_PartitionsEveryDofIntoFreeOrConstrained", "[structural][dof]") {
    // `isFree xor isConstrained` holds by construction here -- only one of the
    // two is stored -- so the test asserts it against the CONSTRAINT SET's own
    // list instead, which compares two independently built objects and is not
    // a tautology.
    const meshing::Mesh mesh = meshWithNodes({1, 7, 8, 200, 201});
    const MeshDofMap map = mapOf(mesh);
    std::vector<NodalDof> prescribed =
        structural::fullyFixedDofs(std::vector<NodeId>{NodeId::fromValue(7)});
    prescribed.push_back(NodalDof{.node = NodeId::fromValue(201), .component = DofComponent::Uz});
    const ConstraintSet set = constraintsOf(map, prescribed);
    const FreeEquationMap equations = equationsOf(map, set);

    REQUIRE(equations.dofCount() == 15);
    std::size_t freeSeen = 0;
    std::size_t constrainedSeen = 0;
    for (DofIndex::ValueType value = 1; value <= equations.dofCount(); ++value) {
        const DofIndex index = DofIndex::fromValue(value);
        INFO("dof " << value);
        REQUIRE(equations.contains(index));
        // Exactly one of the two, for every degree of freedom.
        CHECK(equations.isFree(index) != equations.isConstrained(index));
        // And it agrees with what the constraint set was asked for.
        CHECK(equations.isConstrained(index) == set.contains(index));
        if (equations.isFree(index)) {
            ++freeSeen;
        } else {
            ++constrainedSeen;
        }
    }
    CHECK(freeSeen == equations.freeCount());
    CHECK(constrainedSeen == equations.constrainedCount());
    CHECK(freeSeen + constrainedSeen == equations.dofCount());
    CHECK(constrainedSeen == set.size());

    SECTION("a degree of freedom of another mesh is absent, not constrained") {
        const DofIndex past = DofIndex::fromValue(equations.dofCount() + 1);
        CHECK_FALSE(equations.contains(past));
        CHECK_FALSE(equations.isFree(past));
        CHECK_FALSE(equations.isConstrained(past));
        CHECK_FALSE(equations.contains(DofIndex{}));
        CHECK_FALSE(equations.isFree(DofIndex{}));
        CHECK_FALSE(equations.isConstrained(DofIndex{}));
    }
}

TEST_CASE("FreeEquationMap_EquationAndDofAreExactInverses", "[structural][dof]") {
    const meshing::Mesh mesh = meshWithNodes({2, 3, 11, 4000});
    const MeshDofMap map = mapOf(mesh);
    const ConstraintSet set = constraintsOf(
        map, {NodalDof{.node = NodeId::fromValue(3), .component = DofComponent::Uy},
              NodalDof{.node = NodeId::fromValue(4000), .component = DofComponent::Ux}});
    const FreeEquationMap equations = equationsOf(map, set);

    REQUIRE(equations.freeCount() == 10);
    for (FreeEquationIndex::ValueType row = 0; row < equations.freeCount(); ++row) {
        const FreeEquationIndex equation = FreeEquationIndex::fromValue(row);
        const DofIndex dof = equations.dofOf(equation);
        INFO("row " << row);
        REQUIRE(dof.isValid());
        CHECK(equations.equationOf(dof) == std::optional<FreeEquationIndex>{equation});
        CHECK(equations.freeDofs()[static_cast<std::size_t>(row)] == dof);
        // And the degree of freedom it names is a real one of the mesh.
        CHECK(map.dofAt(dof).has_value());
    }
    CHECK(std::ranges::is_sorted(equations.freeDofs()));
    // One past the last row has no degree of freedom, rather than wrapping to
    // the first.
    CHECK_FALSE(equations.dofOf(FreeEquationIndex::fromValue(equations.freeCount())).isValid());
}

TEST_CASE("FreeEquationMap_GivesAConstrainedDofNoEquationRatherThanRowZero",
          "[structural][dof]") {
    // The sentinel defect, asserted away: `std::optional` means a constrained
    // degree of freedom has NO row, and the first free one genuinely owns row
    // 0.
    const meshing::Mesh mesh = denseMesh(2);
    const MeshDofMap map = mapOf(mesh);
    const ConstraintSet set =
        constraintsOf(map, structural::fullyFixedDofs(std::vector<NodeId>{map.nodeAt(0)}));
    const FreeEquationMap equations = equationsOf(map, set);

    for (DofIndex::ValueType value = 1; value <= 3; ++value) {
        INFO("constrained dof " << value);
        CHECK_FALSE(equations.equationOf(DofIndex::fromValue(value)).has_value());
    }
    CHECK(equations.equationOf(DofIndex::fromValue(4)) ==
          std::optional<FreeEquationIndex>{FreeEquationIndex::fromValue(0)});
    CHECK(equations.dofOf(FreeEquationIndex::fromValue(0)) == DofIndex::fromValue(4));
}

TEST_CASE("FreeEquationMap_RefusesAConstraintSetBuiltAgainstADifferentMesh", "[structural][dof]") {
    // Identical node counts and identical numeric handles, different meshes.
    // Every mesh BetterCAD builds takes a fresh MeshId from a process-local
    // counter, so the stamps differ even though nothing else does.
    const meshing::Mesh first = denseMesh(3);
    const meshing::Mesh second = denseMesh(3);
    const MeshDofMap firstMap = mapOf(first);
    const MeshDofMap secondMap = mapOf(second);
    REQUIRE(firstMap.nodeCount() == secondMap.nodeCount());
    REQUIRE(std::ranges::equal(firstMap.nodes(), secondMap.nodes()));
    REQUIRE(firstMap.stamp() != secondMap.stamp());

    const ConstraintSet foreign = constraintsOf(
        secondMap, {NodalDof{.node = secondMap.nodeAt(0), .component = DofComponent::Ux}});

    const Result<FreeEquationMap> refused = structural::buildFreeEquationMap(firstMap, foreign);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(refused.error().message, ContainsSubstring("different mesh"));

    // And the matching pair is accepted, so the refusal is about the mismatch.
    CHECK(structural::buildFreeEquationMap(secondMap, foreign).has_value());
}

TEST_CASE("FreeEquationMap_HandlesTheTwoExtremesOfConstraint", "[structural][dof]") {
    const meshing::Mesh mesh = denseMesh(3);
    const MeshDofMap map = mapOf(mesh);

    SECTION("nothing constrained: every degree of freedom is an unknown") {
        const ConstraintSet none = constraintsOf(map, {});
        const FreeEquationMap equations = equationsOf(map, none);
        REQUIRE(equations.freeCount() == 9);
        CHECK(equations.constrainedCount() == 0);
        for (DofIndex::ValueType value = 1; value <= 9; ++value) {
            CHECK(equations.equationOf(DofIndex::fromValue(value)) ==
                  std::optional<FreeEquationIndex>{FreeEquationIndex::fromValue(value - 1)});
        }
    }

    SECTION("everything constrained: no equations, and no row zero") {
        std::vector<NodeId> all(map.nodes().begin(), map.nodes().end());
        const ConstraintSet every = constraintsOf(map, structural::fullyFixedDofs(all));
        REQUIRE(every.size() == 9);
        const FreeEquationMap equations = equationsOf(map, every);
        CHECK(equations.freeCount() == 0);
        CHECK(equations.constrainedCount() == 9);
        CHECK(equations.freeDofs().empty());
        CHECK_FALSE(equations.dofOf(FreeEquationIndex::fromValue(0)).isValid());
        for (DofIndex::ValueType value = 1; value <= 9; ++value) {
            INFO("dof " << value);
            CHECK_FALSE(equations.isFree(DofIndex::fromValue(value)));
            CHECK(equations.isConstrained(DofIndex::fromValue(value)));
        }
    }
}

TEST_CASE("FreeEquationMap_NumberingIsIdenticalOverRepeatedBuilds", "[structural][dof]") {
    const meshing::Mesh mesh = meshWithNodes({1, 2, 9, 10, 11, 77});
    const MeshDofMap map = mapOf(mesh);
    const ConstraintSet set = constraintsOf(
        map, {NodalDof{.node = NodeId::fromValue(9), .component = DofComponent::Uz},
              NodalDof{.node = NodeId::fromValue(1), .component = DofComponent::Ux},
              NodalDof{.node = NodeId::fromValue(77), .component = DofComponent::Uy}});
    const FreeEquationMap first = equationsOf(map, set);

    for (int repeat = 0; repeat < 16; ++repeat) {
        const FreeEquationMap again = equationsOf(map, set);
        INFO("repeat " << repeat);
        CHECK(again == first);
        CHECK(again.freeCount() == first.freeCount());
        CHECK(std::ranges::equal(again.freeDofs(), first.freeDofs()));
    }
}

// ---------------------------------------------------------------------------
// Remesh invalidation
// ---------------------------------------------------------------------------

TEST_CASE("MeshDofMap_ARemeshInvalidatesTheNumberingAndTheConstraintsBuiltOnIt",
          "[structural][dof]") {
    // THE CENTRAL CLAIM: no solver identity survives a remesh. Asserted
    // through the ordinary pipeline, with the mesh regenerated by the mesher
    // rather than rebuilt by hand.
    MeshedBlock part;
    const meshing::MeshStamp before = part.volume().mesh().stamp();
    const MeshDofMap map = mapOf(part.volume().mesh());
    const ConstraintSet set =
        constraintsOf(map, structural::fullyFixedDofs(std::vector<NodeId>{map.nodeAt(0)}));
    const FreeEquationMap equations = equationsOf(map, set);
    REQUIRE(equations.freeCount() == map.dofCount() - 3);

    // A remesh of the same unchanged model.
    const Result<const meshing::VolumeMesh*> again =
        part.mesher.generate(part.document, part.regenerator, part.control);
    INFO((again.has_value() ? std::string{} : again.error().message));
    REQUIRE(again.has_value());
    const meshing::VolumeMesh& fresh = **again;
    REQUIRE(fresh.mesh().stamp() != before);

    // The numbering no longer describes the mesh the document holds, even
    // though the node count and the handles are likely identical.
    INFO("nodes before " << map.nodeCount() << " after " << fresh.nodeCount());
    CHECK_FALSE(map.describes(fresh));

    const MeshDofMap refreshed = mapOf(fresh.mesh());
    CHECK(refreshed.describes(fresh));
    CHECK(refreshed.stamp() != map.stamp());
    CHECK(refreshed != map);

    // And the stale constraint set cannot be numbered against the new mesh:
    // the refusal is by STAMP, so it holds whether or not the handles agree.
    const Result<FreeEquationMap> refused = structural::buildFreeEquationMap(refreshed, set);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == ErrorCode::FailedPrecondition);

    // Rebuilding the constraints against the fresh numbering works, which is
    // the intended recovery: restraints are named in CAD terms and resolved
    // again, never carried as indices.
    const ConstraintSet rebuilt =
        constraintsOf(refreshed, structural::fullyFixedDofs(std::vector<NodeId>{refreshed.nodeAt(0)}));
    CHECK(structural::buildFreeEquationMap(refreshed, rebuilt).has_value());
}

// ---------------------------------------------------------------------------
// The type model
// ---------------------------------------------------------------------------

TEST_CASE("FreeEquationMap_RefusesAConstraintSetThatOutgrowsTheNumbering", "[structural][dof]") {
    // ONE MeshStamp, TWO NODE COUNTS, and a stamp comparison alone cannot tell
    // them apart. `MeshBuilder` sets its stamp in its CONSTRUCTOR and `build()`
    // is documented as "a snapshot, which is what makes a mesh a value rather
    // than a handle to a living object" -- so two snapshots of one builder
    // share a stamp and differ in size.
    //
    // Found by reading `MeshBuilder` during the adversarial review, not by a
    // failing test, and the first draft of `buildFreeEquationMap` accepted the
    // pair: it compared stamps and nothing else.
    meshing::MeshBuilder builder;
    for (NodeId::ValueType i = 1; i <= 5; ++i) {
        REQUIRE(builder.addNode(at(static_cast<double>(i), 0.0, 0.0)).has_value());
    }
    const meshing::Mesh small = builder.build();
    for (NodeId::ValueType i = 6; i <= 10; ++i) {
        REQUIRE(builder.addNode(at(static_cast<double>(i), 0.0, 0.0)).has_value());
    }
    const meshing::Mesh large = builder.build();

    REQUIRE(small.stamp() == large.stamp());
    REQUIRE(small.nodeCount() == 5);
    REQUIRE(large.nodeCount() == 10);

    const MeshDofMap smallMap = mapOf(small);
    const MeshDofMap largeMap = mapOf(large);
    REQUIRE(smallMap.dofCount() == 15);
    REQUIRE(largeMap.dofCount() == 30);
    REQUIRE(smallMap.stamp() == largeMap.stamp());

    SECTION("a constraint beyond the numbering is refused, not ignored") {
        const ConstraintSet set = constraintsOf(
            largeMap, {NodalDof{.node = large.nodes()[9].id, .component = DofComponent::Uz}});
        REQUIRE(set.constrained()[0] == DofIndex::fromValue(30));

        // Without the range check this was ACCEPTED: the loop over 1 ..= 15
        // never meets index 30, so all fifteen degrees of freedom came back
        // free and `constrainedCount()` was 0 -- an unrestrained system
        // assembled from a restrained model.
        const Result<FreeEquationMap> refused =
            structural::buildFreeEquationMap(smallMap, set);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().code == ErrorCode::FailedPrecondition);
        CHECK_THAT(refused.error().message, ContainsSubstring("does not have"));
    }

    SECTION("more constraints than there are degrees of freedom is refused") {
        // The sharper form: this is the case whose `freeCount` reservation
        // would have been a subtraction below zero on an unsigned type.
        const std::vector<NodeId> all(largeMap.nodes().begin(), largeMap.nodes().end());
        const ConstraintSet every = constraintsOf(largeMap, structural::fullyFixedDofs(all));
        REQUIRE(every.size() == 30);
        REQUIRE(every.size() > smallMap.dofCount());
        const Result<FreeEquationMap> refused =
            structural::buildFreeEquationMap(smallMap, every);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().code == ErrorCode::FailedPrecondition);
    }

    SECTION("describes() tells the two snapshots apart as well") {
        // The same hole at the mesh boundary, closed the same way: the node
        // count distinguishes every pair that can actually arise, because a
        // builder only grows and its handles are strictly increasing, so two
        // snapshots of differing content always differ in count.
        CHECK(smallMap.describes(small));
        CHECK_FALSE(smallMap.describes(large));
        CHECK(largeMap.describes(large));
        CHECK_FALSE(largeMap.describes(small));
    }

    SECTION("the matching pairs are still accepted") {
        const ConstraintSet set = constraintsOf(
            smallMap, {NodalDof{.node = small.nodes()[4].id, .component = DofComponent::Uz}});
        const FreeEquationMap equations = equationsOf(smallMap, set);
        CHECK(equations.freeCount() == 14);
        CHECK(equations.constrainedCount() == 1);
    }
}

TEST_CASE("StructuralDof_ComponentLookupIsCyclicOverAFlatElementPosition", "[structural][dof]") {
    // `componentAt` is CYCLIC, and that is what interleaved numbering means:
    // flat position 3 of an element vector is the SECOND node's Ux. So the
    // modulo is not a defensive guard but the lookup P17-ELEM-001 needs for a
    // twelve-component Tet4 vector [ux1 uy1 uz1 ux2 ... uz4].
    //
    // It was unreachable until this test existed -- every caller passed an
    // offset already reduced -- and a mutation that removed the modulo
    // SURVIVED the suite. That is what a surviving mutation is for.
    STATIC_REQUIRE(structural::componentAt(0) == DofComponent::Ux);
    STATIC_REQUIRE(structural::componentAt(1) == DofComponent::Uy);
    STATIC_REQUIRE(structural::componentAt(2) == DofComponent::Uz);
    STATIC_REQUIRE(structural::componentAt(3) == DofComponent::Ux);
    STATIC_REQUIRE(structural::componentAt(4) == DofComponent::Uy);
    STATIC_REQUIRE(structural::componentAt(11) == DofComponent::Uz);

    // The whole twelve, as a Tet4 element vector is written, so the ordering a
    // later milestone depends on is fixed here rather than there.
    constexpr std::size_t kTet4Dofs = 12;
    for (std::size_t flat = 0; flat < kTet4Dofs; ++flat) {
        INFO("flat position " << flat);
        CHECK(structural::componentAt(flat) == kDofComponents[flat % kDofsPerNode]);
        // And the node it belongs to is flat / 3, which is the same arithmetic
        // `dofAt` inverts.
        CHECK(flat / kDofsPerNode == flat / 3);
    }
}

TEST_CASE("StructuralDof_SolverLocalIndicesAreDistinctTypes", "[structural][dof]") {
    // A DofIndex and a FreeEquationIndex count different things, and the
    // compiler enforces it. The compile-fail cases in
    // tests/compile_fail/StructuralDofMisuse.cpp prove the conversions are
    // REJECTED; these assert the same model from inside a build that compiles.
    STATIC_REQUIRE_FALSE(std::is_convertible_v<FreeEquationIndex, DofIndex>);
    STATIC_REQUIRE_FALSE(std::is_convertible_v<DofIndex, FreeEquationIndex>);
    STATIC_REQUIRE_FALSE(std::is_convertible_v<FreeEquationIndex, NodeId>);
    STATIC_REQUIRE_FALSE(std::is_convertible_v<FreeEquationIndex, std::uint64_t>);
    STATIC_REQUIRE_FALSE(std::is_convertible_v<std::uint64_t, FreeEquationIndex>);

    // NO INVALID VALUE, so there is no default construction to mistake for row
    // zero. Absence is std::optional, which the compiler makes the caller
    // handle.
    STATIC_REQUIRE_FALSE(std::is_default_constructible_v<FreeEquationIndex>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<FreeEquationIndex>);
    STATIC_REQUIRE(sizeof(FreeEquationIndex) == sizeof(FreeEquationIndex::ValueType));

    // A DofIndex does have one, and it is invalid: P17-DATA-001's convention,
    // inherited rather than redefined.
    STATIC_REQUIRE(std::is_default_constructible_v<DofIndex>);
    CHECK_FALSE(DofIndex{}.isValid());

    // The three maps cannot be conjured: possession is the evidence that a
    // numbering is a real mesh's numbering (ADR-036's idiom).
    STATIC_REQUIRE_FALSE(std::is_default_constructible_v<MeshDofMap>);
    STATIC_REQUIRE_FALSE(std::is_default_constructible_v<ConstraintSet>);
    STATIC_REQUIRE_FALSE(std::is_default_constructible_v<FreeEquationMap>);

    // They are values, not borrowed views, so copying one is safe -- unlike
    // StructuralModel, which borrows and is move-only.
    STATIC_REQUIRE(std::is_copy_constructible_v<MeshDofMap>);
    STATIC_REQUIRE(std::is_copy_constructible_v<ConstraintSet>);
    STATIC_REQUIRE(std::is_copy_constructible_v<FreeEquationMap>);

    // Ordered, because the numbering and every set built from it are kept
    // ascending and searched rather than hashed.
    STATIC_REQUIRE(std::three_way_comparable<FreeEquationIndex>);
    STATIC_REQUIRE(std::three_way_comparable<DofIndex>);
    CHECK(FreeEquationIndex::fromValue(0) < FreeEquationIndex::fromValue(1));
    CHECK(FreeEquationIndex::fromValue(4).value() == 4);
}

TEST_CASE("StructuralDof_ScalesWithoutOverflowingTheIndexSpace", "[structural][dof]") {
    // The scale sanity the brief asks for, done as arithmetic on the bound
    // rather than by building a mesh with millions of nodes: a DofIndex is
    // 64-bit and a NodeId is 32-bit, so 3N cannot overflow for ANY
    // representable mesh. That is a compile-time proof, not a runtime branch
    // nothing could take -- the header carries the static_assert.
    STATIC_REQUIRE(sizeof(DofIndex::ValueType) == 8);
    STATIC_REQUIRE(sizeof(NodeId::ValueType) == 4);

    constexpr auto largest = static_cast<DofIndex::ValueType>(
        std::numeric_limits<NodeId::ValueType>::max());
    STATIC_REQUIRE(3 * largest + 1 < std::numeric_limits<DofIndex::ValueType>::max());
    // A 32-bit index would NOT have been enough, which is why DofIndex is
    // 64-bit: this is the assertion that records the reason.
    STATIC_REQUIRE(3 * largest > std::numeric_limits<std::uint32_t>::max());

    // And one mesh big enough to cost something, numbered and partitioned, so
    // the O(n log n) construction is exercised rather than argued.
    const meshing::Mesh mesh = denseMesh(20000);
    const MeshDofMap map = mapOf(mesh);
    REQUIRE(map.dofCount() == 60000);
    std::vector<NodeId> firstThousand(map.nodes().begin(), map.nodes().begin() + 1000);
    const ConstraintSet set = constraintsOf(map, structural::fullyFixedDofs(firstThousand));
    REQUIRE(set.size() == 3000);
    const FreeEquationMap equations = equationsOf(map, set);
    CHECK(equations.freeCount() == 57000);
    CHECK(equations.constrainedCount() == 3000);
    // Spot the boundary between the constrained prefix and the free remainder.
    CHECK(equations.isConstrained(DofIndex::fromValue(3000)));
    CHECK(equations.isFree(DofIndex::fromValue(3001)));
    CHECK(equations.equationOf(DofIndex::fromValue(3001)) ==
          std::optional<FreeEquationIndex>{FreeEquationIndex::fromValue(0)});
    CHECK(equations.equationOf(DofIndex::fromValue(60000)) ==
          std::optional<FreeEquationIndex>{FreeEquationIndex::fromValue(56999)});
}
