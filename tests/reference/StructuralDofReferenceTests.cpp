// P17-DOF-001 against the meshing reference models.
//
// WHY THIS FILE IS SEPARATE FROM tests/structural/StructuralDofTests.cpp. Those
// tests build their meshes with `MeshBuilder`, because that is the only way to
// produce SPARSE node handles and sparse handles are the case the numbering is
// designed for. These use meshes that came out of Netgen through the ordinary
// pipeline -- thousands of nodes, real connectivity, real boundary
// attribution -- so the claims checked here are the ones a hand-built mesh
// cannot support:
//
//   the numbering scales, and 3N is exact on a mesh nobody chose the size of
//   a restraint reaches nodes THE WAY A RESTRAINT WILL: by naming a CAD face
//     and resolving it through P16's map, never by holding a NodeId
//   the partition is consistent on a mesh with a void in it, and on one with
//     local refinement, where the node distribution is deliberately uneven
//   the whole chain is deterministic over a remesh of the same model
//
// MODELS: RM-MESH-01 (block, six nameable faces), RM-MESH-03 (plate with a
// through-hole), RM-MESH-04 (tube, two distinct walls) and RM-MESH-07 (local
// refinement). The brief names these four.
//
// NOTHING HERE SOLVES ANYTHING, and no displacement has a physical meaning.

#include "reference/MeshTestSupport.hpp"

#include <MeshReferenceModels.hpp>

#include <bettercad/core/document/References.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/structural/StructuralDof.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <utility>
#include <cstddef>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace bettercad;
using namespace bettercad::test;
using namespace bettercad::test::meshref;
using Catch::Matchers::ContainsSubstring;
using meshing::NodeId;
using structural::ConstraintSet;
using structural::DofComponent;
using structural::DofIndex;
using structural::FreeEquationIndex;
using structural::FreeEquationMap;
using structural::MeshDofMap;
using structural::NodalDof;
using structural::kDofComponents;
using structural::kDofsPerNode;

[[nodiscard]] MeshDofMap mapOf(const meshing::VolumeMesh& volume) {
    Result<MeshDofMap> map = structural::buildMeshDofMap(volume.mesh());
    INFO((map.has_value() ? std::string{} : map.error().message));
    REQUIRE(map.has_value());
    return std::move(*map);
}

/// The nodes a CAD face's boundary facets use: the path a restraint will take.
///
/// `boundaryFacetsOf` then `boundaryNodesOf`, both P16's, so the node set is
/// derived from the geometry rather than selected independently -- which is
/// what ADR-032 requires and what makes the set survive a remesh as a
/// REFERENCE even though the handles do not.
[[nodiscard]] std::vector<NodeId> nodesOfFace(const meshing::GeometryMeshMap& map,
                                              const meshing::Mesh& mesh,
                                              const FaceName& face) {
    const Result<meshing::BoundaryFacetSet> facets = meshing::boundaryFacetsOf(map, face);
    INFO((facets.has_value() ? std::string{} : facets.error().message));
    REQUIRE(facets.has_value());
    REQUIRE(facets->fullyResolved());
    REQUIRE_FALSE(facets->facets.empty());
    Result<std::vector<NodeId>> nodes = meshing::boundaryNodesOf(map, mesh, facets->facets);
    INFO((nodes.has_value() ? std::string{} : nodes.error().message));
    REQUIRE(nodes.has_value());
    REQUIRE_FALSE(nodes->empty());
    return std::move(*nodes);
}

/// Every claim a numbering must satisfy on any real mesh, in one place so each
/// model is checked the same way rather than by copied code.
void checkNumbering(const MeshDofMap& map, const meshing::VolumeMesh& volume) {
    REQUIRE(map.describes(volume));
    REQUIRE(map.nodeCount() == volume.nodeCount());
    CHECK(map.dofCount() == kDofsPerNode * static_cast<DofIndex::ValueType>(volume.nodeCount()));

    // Dense, ascending, and the mesh's own order.
    REQUIRE(map.nodes().size() == volume.mesh().nodes().size());
    CHECK(std::ranges::is_sorted(map.nodes()));
    CHECK(std::ranges::adjacent_find(map.nodes()) == map.nodes().end());
    for (std::size_t i = 0; i < map.nodes().size(); ++i) {
        if (map.nodes()[i] != volume.mesh().nodes()[i].id) {
            FAIL("node ordinal " << i << " disagrees with the mesh enumeration");
        }
    }

    // The round trip, over EVERY degree of freedom of the mesh. Affordable
    // because it is linear, and it is the only check that can see a numbering
    // which is wrong for one node out of several thousand.
    for (DofIndex::ValueType value = 1; value <= map.dofCount(); ++value) {
        const DofIndex index = DofIndex::fromValue(value);
        const Result<NodalDof> dof = map.dofAt(index);
        if (!dof) {
            FAIL("degree of freedom " << value << " does not resolve: " << dof.error().message);
        }
        const Result<DofIndex> back = map.indexOf(*dof);
        if (!back || *back != index) {
            FAIL("degree of freedom " << value << " does not round trip");
        }
    }

    // And each node's three are consecutive, which is the frozen convention.
    for (std::size_t ordinal = 0; ordinal < map.nodeCount(); ++ordinal) {
        const Result<std::array<DofIndex, kDofsPerNode>> three = map.indicesOf(map.nodeAt(ordinal));
        REQUIRE(three.has_value());
        if ((*three)[0].value() != kDofsPerNode * ordinal + 1 ||
            (*three)[2].value() != kDofsPerNode * ordinal + 3) {
            FAIL("node ordinal " << ordinal << " is not numbered consecutively");
        }
    }
}

/// The partition, checked against the constraint set rather than against
/// itself.
void checkPartition(const MeshDofMap& map, const ConstraintSet& set,
                    const FreeEquationMap& equations) {
    REQUIRE(equations.dofCount() == map.dofCount());
    CHECK(equations.constrainedCount() == set.size());
    CHECK(equations.freeCount() == map.dofCount() - set.size());

    std::size_t freeSeen = 0;
    for (DofIndex::ValueType value = 1; value <= equations.dofCount(); ++value) {
        const DofIndex index = DofIndex::fromValue(value);
        const bool isFree = equations.isFree(index);
        if (isFree == equations.isConstrained(index)) {
            FAIL("degree of freedom " << value << " is neither or both");
        }
        if (isFree == set.contains(index)) {
            FAIL("degree of freedom " << value << " disagrees with the constraint set");
        }
        if (isFree) {
            const std::optional<FreeEquationIndex> row = equations.equationOf(index);
            REQUIRE(row.has_value());
            if (row->value() != freeSeen) {
                FAIL("degree of freedom " << value << " is row " << row->value() << ", expected "
                                          << freeSeen);
            }
            if (equations.dofOf(*row) != index) {
                FAIL("row " << row->value() << " does not invert");
            }
            ++freeSeen;
        }
    }
    CHECK(freeSeen == equations.freeCount());
}

} // namespace

TEST_CASE("StructuralDof_NumbersTheReferenceMeshesAndScalesToThem",
          "[structural][dof][reference]") {
    SECTION("RM-MESH-01, a block whose six faces are all nameable") {
        auto built = reference::buildMeshBlockReferenceModel();
        REQUIRE(built.has_value());
        MeshedReference model(std::move(built->document));
        const meshing::VolumeMesh& volume = model.require();
        const MeshDofMap map = mapOf(volume);
        INFO("nodes " << map.nodeCount() << " dofs " << map.dofCount());
        // MEASURED: 8 nodes, 24 degrees of freedom. A 120 x 70 x 35 mm box is
        // tetrahedralised on its corners alone, which is a legitimate mesh and
        // was NOT what the first draft of this test assumed -- it asserted a
        // node count above a hundred and failed. P16's own RM-MESH-01 sizing
        // table records the same 8 nodes and 6 tetrahedra at 30, 20 and 10 mm,
        // so this is qualified behaviour and not a defect to work around.
        //
        // Asserting a mesh DENSITY would be asserting Netgen's sizing
        // behaviour, which P16 owns and measures. What this test is entitled
        // to require is that the numbering holds on whatever mesh arrives --
        // and the four models cover 24, 243, 783 and 183 degrees of freedom,
        // every one of them round-tripped.
        CHECK(map.nodeCount() >= 4);
        checkNumbering(map, volume);
    }

    SECTION("RM-MESH-03, a plate with a through-hole the mesh must leave empty") {
        auto built = reference::buildMeshPlateWithHoleReferenceModel();
        REQUIRE(built.has_value());
        MeshedReference model(std::move(built->document));
        const meshing::VolumeMesh& volume = model.require();
        const MeshDofMap map = mapOf(volume);
        INFO("nodes " << map.nodeCount() << " dofs " << map.dofCount());
        checkNumbering(map, volume);
    }

    SECTION("RM-MESH-04, a tube whose two walls are different CAD faces") {
        auto built = reference::buildMeshTubeReferenceModel();
        REQUIRE(built.has_value());
        MeshedReference model(std::move(built->document));
        const meshing::VolumeMesh& volume = model.require();
        const MeshDofMap map = mapOf(volume);
        INFO("nodes " << map.nodeCount() << " dofs " << map.dofCount());
        checkNumbering(map, volume);
    }

    SECTION("RM-MESH-07, local refinement, so the nodes are deliberately uneven") {
        auto built = reference::buildMeshLocalRefinementReferenceModel();
        REQUIRE(built.has_value());
        MeshedReference model(std::move(built->document));
        const meshing::VolumeMesh& volume = model.require();
        const MeshDofMap map = mapOf(volume);
        INFO("nodes " << map.nodeCount() << " dofs " << map.dofCount());
        checkNumbering(map, volume);
    }

    SECTION("the numbering is a pure function of the mesh at every sizing level") {
        // Three sizing levels of one model, through the ordinary entry point.
        //
        // WHAT IS NOT ASSERTED IS THAT THE MESH GETS FINER. P16 owns sizing,
        // and its own committed RM-MESH-01 table records 8 nodes and 6
        // tetrahedra at 30, 20 AND 10 mm alike, because a global target is an
        // upper bound and does not subdivide a planar box interior. The first
        // draft of this section asserted a refinement and failed, which is the
        // second time in this test file that an assertion of mine was about
        // P16's behaviour rather than about this milestone's.
        //
        // What IS asserted is that the numbering tracks whatever mesh arrives:
        // 3N exactly, dense, ascending, and a round trip over every degree of
        // freedom, at every level.
        auto built = reference::buildMeshPlateWithHoleReferenceModel();
        REQUIRE(built.has_value());
        MeshedReference model(std::move(built->document));
        for (const double targetMm : {12.0, 8.0, 5.0}) {
            meshing::VolumeMeshControls controls;
            controls.sizing.globalTargetSize = Length::fromSi(targetMm * 1e-3);
            const meshing::VolumeMesh volume = model.requireWith(controls);
            const MeshDofMap map = mapOf(volume);
            INFO("target " << targetMm << " mm, nodes " << map.nodeCount() << " dofs "
                           << map.dofCount());
            checkNumbering(map, volume);

            // And the whole chain on it: a quarter of the nodes fully fixed,
            // the partition, and compact equation numbering.
            const std::vector<meshing::NodeId> some(
                map.nodes().begin(),
                map.nodes().begin() + static_cast<std::ptrdiff_t>(map.nodeCount() / 4));
            Result<ConstraintSet> set =
                structural::buildConstraintSet(map, structural::fullyFixedDofs(some));
            INFO((set.has_value() ? std::string{} : set.error().message));
            REQUIRE(set.has_value());
            const Result<FreeEquationMap> equations =
                structural::buildFreeEquationMap(map, *set);
            REQUIRE(equations.has_value());
            checkPartition(map, *set, *equations);
        }
    }
}

TEST_CASE("StructuralDof_ConstrainsTheNodesOfANamedCadFaceAndNumbersTheRest",
          "[structural][dof][reference]") {
    // THE WAY A RESTRAINT WILL REACH THE SOLVER. The face is named in CAD
    // terms, P16 resolves it to boundary facets and those to nodes, and only
    // then does a degree-of-freedom index exist. Nothing holds a NodeId across
    // anything.
    auto built = reference::buildMeshBlockReferenceModel();
    REQUIRE(built.has_value());
    // THE FACE NAME IS TAKEN FROM THE MODEL, BEFORE ITS DOCUMENT IS MOVED.
    // That is the suite's own pattern: the model struct knows which sketch
    // entity swept which face, and a face named any other way would be a face
    // index by another spelling.
    const FaceName bottom = built->bottom();
    MeshedReference model(std::move(built->document));
    const meshing::VolumeMesh& volume = model.require();
    const MeshDofMap map = mapOf(volume);

    // Resolved against the mesh this model actually produced, so the face name
    // has to BIND -- an unresolved reference fails in nodesOfFace rather than
    // becoming an empty node set, which is the distinction P16 insists on.
    const std::vector<NodeId> fixedNodes = nodesOfFace(model.map(), volume.mesh(), bottom);
    INFO("face nodes " << fixedNodes.size() << " of " << map.nodeCount());
    CHECK(fixedNodes.size() > 3);
    CHECK(fixedNodes.size() < map.nodeCount());

    const std::vector<NodalDof> prescribed = structural::fullyFixedDofs(fixedNodes);
    REQUIRE(prescribed.size() == fixedNodes.size() * kDofsPerNode);

    Result<ConstraintSet> set = structural::buildConstraintSet(map, prescribed);
    INFO((set.has_value() ? std::string{} : set.error().message));
    REQUIRE(set.has_value());
    CHECK(set->size() == prescribed.size());

    Result<FreeEquationMap> equations = structural::buildFreeEquationMap(map, *set);
    INFO((equations.has_value() ? std::string{} : equations.error().message));
    REQUIRE(equations.has_value());

    // The arithmetic that matters to an assembler: a fully fixed face removes
    // exactly three equations per node of that face, and no more.
    CHECK(equations->freeCount() == map.dofCount() - fixedNodes.size() * kDofsPerNode);
    checkPartition(map, *set, *equations);

    // Every node of the face has all three of its degrees of freedom
    // prescribed, and a node NOT on the face has none of them. The second half
    // is what a control that over-applied would fail.
    for (const NodeId node : fixedNodes) {
        const Result<std::array<DofIndex, kDofsPerNode>> three = map.indicesOf(node);
        REQUIRE(three.has_value());
        for (const DofIndex index : *three) {
            CHECK(set->contains(index));
            CHECK(equations->isConstrained(index));
        }
    }
    const std::set<NodeId::ValueType> onFace = [&fixedNodes] {
        std::set<NodeId::ValueType> handles;
        for (const NodeId node : fixedNodes) {
            handles.insert(node.value());
        }
        return handles;
    }();
    std::size_t interiorChecked = 0;
    for (const NodeId node : map.nodes()) {
        if (onFace.contains(node.value())) {
            continue;
        }
        const Result<std::array<DofIndex, kDofsPerNode>> three = map.indicesOf(node);
        REQUIRE(three.has_value());
        for (const DofIndex index : *three) {
            if (set->contains(index)) {
                FAIL("node " << node.value() << " is not on the face but is constrained");
            }
        }
        ++interiorChecked;
    }
    CHECK(interiorChecked == map.nodeCount() - fixedNodes.size());
}

TEST_CASE("StructuralDof_ConstrainsTwoDistinctCadFacesOfATubeWithoutOverlap",
          "[structural][dof][reference]") {
    // RM-MESH-04 exists because its inner and outer walls are swept by
    // DIFFERENT circles and so carry different names. If the two resolved to
    // the same facets, fixing both would produce a duplicate degree of freedom
    // -- which this milestone REFUSES -- so a successful build here is also
    // evidence the two faces are genuinely distinct.
    auto built = reference::buildMeshTubeReferenceModel();
    REQUIRE(built.has_value());
    const FaceName outerWall = built->outerWall();
    const FaceName innerWall = built->innerWall();
    REQUIRE_FALSE(outerWall == innerWall);
    MeshedReference model(std::move(built->document));
    const meshing::VolumeMesh& volume = model.require();
    const MeshDofMap map = mapOf(volume);

    const std::vector<NodeId> outer = nodesOfFace(model.map(), volume.mesh(), outerWall);
    const std::vector<NodeId> inner = nodesOfFace(model.map(), volume.mesh(), innerWall);
    INFO("outer " << outer.size() << " inner " << inner.size());

    // Disjoint, which is what "the void survives" means in node terms: no node
    // lies on both walls of a tube.
    std::vector<NodeId> shared;
    std::ranges::set_intersection(outer, inner, std::back_inserter(shared));
    CHECK(shared.empty());

    std::vector<NodalDof> both = structural::fullyFixedDofs(outer);
    const std::vector<NodalDof> innerDofs = structural::fullyFixedDofs(inner);
    both.insert(both.end(), innerDofs.begin(), innerDofs.end());

    Result<ConstraintSet> set = structural::buildConstraintSet(map, both);
    INFO((set.has_value() ? std::string{} : set.error().message));
    REQUIRE(set.has_value());
    CHECK(set->size() == (outer.size() + inner.size()) * kDofsPerNode);

    Result<FreeEquationMap> equations = structural::buildFreeEquationMap(map, *set);
    REQUIRE(equations.has_value());
    checkPartition(map, *set, *equations);

    // And the adversarial half: listing one of the walls TWICE is a duplicate
    // and is refused, rather than being silently deduplicated into the set
    // above.
    std::vector<NodalDof> repeated = structural::fullyFixedDofs(outer);
    const std::vector<NodalDof> outerAgain = structural::fullyFixedDofs(outer);
    repeated.insert(repeated.end(), outerAgain.begin(), outerAgain.end());
    const Result<ConstraintSet> refused = structural::buildConstraintSet(map, repeated);
    REQUIRE_FALSE(refused.has_value());
    CHECK_THAT(refused.error().message, ContainsSubstring("more than once"));
}

TEST_CASE("StructuralDof_IsDeterministicAcrossARemeshOfTheSameReferenceModel",
          "[structural][dof][reference]") {
    // Determinism in the sense that matters: the same model, meshed again,
    // gives the same NUMBERING STRUCTURE -- and the old numbering is refused
    // against the new mesh, because no solver identity survives a remesh.
    auto built = reference::buildMeshLocalRefinementReferenceModel();
    REQUIRE(built.has_value());
    MeshedReference model(std::move(built->document));
    const meshing::VolumeMesh& first = model.require();
    const MeshDofMap firstMap = mapOf(first);
    const std::size_t firstNodes = firstMap.nodeCount();
    const std::vector<NodeId> firstHandles(firstMap.nodes().begin(), firstMap.nodes().end());
    const meshing::MeshStamp firstStamp = first.mesh().stamp();

    Result<ConstraintSet> built0 = structural::buildConstraintSet(
        firstMap, structural::fullyFixedDofs(std::vector<NodeId>{firstMap.nodeAt(0)}));
    REQUIRE(built0.has_value());
    const ConstraintSet stale = std::move(*built0);

    const meshing::VolumeMesh& second = model.require();
    REQUIRE(second.mesh().stamp() != firstStamp);
    const MeshDofMap secondMap = mapOf(second);

    // The mesher is deterministic, so the same model gives the same node count
    // and the same handles -- which is exactly why the stamp, and not the
    // handles, is what detects a remesh.
    CHECK(secondMap.nodeCount() == firstNodes);
    CHECK(std::ranges::equal(secondMap.nodes(), firstHandles));
    CHECK(secondMap.dofCount() == firstMap.dofCount());
    CHECK(secondMap.stamp() != firstMap.stamp());
    CHECK(secondMap != firstMap);
    CHECK_FALSE(firstMap.describes(second));
    CHECK(secondMap.describes(second));

    // And the stale constraint set is refused against the new numbering, even
    // though every handle in it is still a node of the new mesh.
    REQUIRE(secondMap.nodeOrdinal(firstMap.nodeAt(0)).has_value());
    const Result<FreeEquationMap> refused = structural::buildFreeEquationMap(secondMap, stale);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == ErrorCode::FailedPrecondition);
}
