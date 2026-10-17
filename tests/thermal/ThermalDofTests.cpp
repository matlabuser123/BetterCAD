// P18-DOF-001: the temperature degree-of-freedom numbering and the
// prescribed-temperature constraint model.
//
// WHAT THESE TESTS ARE FOR. The milestone's claims are about an index space,
// and an index space is exactly the kind of thing that can be wrong in a way
// every spot check passes: a numbering off by one for the last node, a free
// numbering with a gap, a map that survives a remesh it should not, a
// prescribed temperature that quietly became zero. So the properties below
// are checked EXHAUSTIVELY where that is affordable -- over every unknown of
// several meshes, in both directions -- and the expected tables are written
// BY HAND from the documented mesh order, never by calling the production
// map to compute what the production map should answer.
//
// THE MESHES ARE BUILT BY HAND, DELIBERATELY. `MeshBuilder` is the only way to
// produce a mesh with SPARSE node handles (7, 104, 9001, 500000), and sparse
// handles are the case the whole design exists for: `temperature[node.value()]`
// would work on every mesh the current backend produces and fail on the first
// one that is not dense. The pipeline cases -- remesh, material edit, result
// ordering -- use the real mesher.
//
// NOTHING HERE SOLVES ANYTHING. There is no conductivity matrix, no heat
// load and no temperature with a physical meaning; the numbers are indices,
// and the kelvin values are chosen to be told apart.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/materials/ThermalProperties.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/thermal/ThermalAnalysisObject.hpp>
#include <bettercad/thermal/ThermalData.hpp>
#include <bettercad/thermal/ThermalDof.hpp>
#include <bettercad/thermal/ThermalResult.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using meshing::NodeId;
using thermal::ConstraintProblem;
using thermal::DofMapProblem;
using thermal::ThermalAnalysis;
using thermal::ThermalAnalysisDefinition;
using thermal::ThermalConstraintMap;
using thermal::ThermalDofIndex;
using thermal::ThermalDofMap;
using thermal::ThermalFreeEquationIndex;
using thermal::ThermalFreeEquationMap;
using thermal::ThermalResult;
using thermal::ThermalResultSource;
using thermal::ThermalTemperatureConstraint;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

[[nodiscard]] Point3D at(double xMm, double yMm, double zMm) {
    return Point3D{Length::fromSi(xMm * 1e-3), Length::fromSi(yMm * 1e-3), Length::fromSi(zMm * 1e-3)};
}

/// A mesh carrying exactly the node handles asked for, at distinct positions,
/// from a fresh builder (so a fresh MeshId).
[[nodiscard]] meshing::Mesh meshWithNodes(const std::vector<NodeId::ValueType>& handles) {
    meshing::MeshBuilder builder;
    for (const NodeId::ValueType handle : handles) {
        const Result<NodeId> added =
            builder.addNode(NodeId::fromValue(handle), at(static_cast<double>(handle % 1000), 0.0, 0.0));
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

/// THE SPARSE FIXTURE the brief names: four nodes whose handles are nothing
/// like their ordinals.
const std::vector<NodeId::ValueType> kSparse{7, 104, 9001, 500000};

[[nodiscard]] ThermalDofMap mapOf(const meshing::Mesh& mesh) {
    Result<ThermalDofMap> map = thermal::buildThermalDofMap(mesh);
    INFO((map.has_value() ? std::string{} : map.error().message));
    REQUIRE(map.has_value());
    return std::move(*map);
}

[[nodiscard]] ThermalDofIndex dof(ThermalDofIndex::ValueType value) {
    return ThermalDofIndex::fromValue(value);
}

[[nodiscard]] ThermalTemperatureConstraint hold(ThermalDofIndex::ValueType d, double kelvin) {
    return ThermalTemperatureConstraint{.dof = dof(d), .temperature = Temperature::fromSi(kelvin)};
}

[[nodiscard]] ThermalConstraintMap constraintsOf(const ThermalDofMap& map,
                                                 const std::vector<ThermalTemperatureConstraint>& prescribed) {
    Result<ThermalConstraintMap> built = thermal::buildThermalConstraintMap(map, prescribed);
    INFO((built.has_value() ? std::string{} : built.error().message));
    REQUIRE(built.has_value());
    return std::move(*built);
}

[[nodiscard]] ThermalFreeEquationMap equationsOf(const ThermalDofMap& map, const ThermalConstraintMap& constraints) {
    Result<ThermalFreeEquationMap> built = thermal::buildThermalFreeEquationMap(map, constraints);
    INFO((built.has_value() ? std::string{} : built.error().message));
    REQUIRE(built.has_value());
    return std::move(*built);
}

[[nodiscard]] ThermalDofIndex dofOf(const ThermalDofMap& map, NodeId node) {
    Result<ThermalDofIndex> index = map.dofOf(node);
    INFO((index.has_value() ? std::string{} : index.error().message));
    REQUIRE(index.has_value());
    return *index;
}

/// The node of @p index, REQUIRED to resolve first, so a numbering that
/// forgot the node fails the test with an assertion rather than reading an
/// error result.
[[nodiscard]] NodeId nodeAt(const ThermalDofMap& map, ThermalDofIndex index) {
    Result<NodeId> node = map.nodeOf(index);
    INFO((node.has_value() ? std::string{} : node.error().message));
    REQUIRE(node.has_value());
    return *node;
}

[[nodiscard]] std::array<ThermalDofIndex, 4> localDofs(const ThermalDofMap& map,
                                                       const meshing::Tetrahedron& element) {
    Result<std::array<ThermalDofIndex, 4>> dofs = map.elementDofs(element);
    INFO((dofs.has_value() ? std::string{} : dofs.error().message));
    REQUIRE(dofs.has_value());
    return *dofs;
}

[[nodiscard]] Temperature temperatureAt(const ThermalResult& result, const meshing::VolumeMesh& mesh,
                                        NodeId node) {
    Result<Temperature> temperature = result.temperatureOf(mesh, node);
    INFO((temperature.has_value() ? std::string{} : temperature.error().message));
    REQUIRE(temperature.has_value());
    return *temperature;
}

/// A block through the ordinary pipeline, with a conducting material and a
/// steady thermal analysis: the fixture for the cases that must agree with a
/// real mesh, a real result and real edits.
struct ThermalDofStudy {
    Document document{"Part"};
    features::Regenerator regenerator;
    meshing::Mesher mesher;
    ObjectId feature{};
    MeshControlId control{};
    ThermalAnalysisId analysis{};
    MaterialId material{};

    ThermalDofStudy() {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
        const ObjectId profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = 20_mm});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));

        auto intent = meshing::MeshControl::create("Mesh", meshing::MeshControlDefinition{.body = feature});
        REQUIRE(intent.has_value());
        control = MeshControlId::fromValue(require(document.addObject(std::move(*intent))).value());

        features::MaterialDefinition definition;
        definition.designation = "Steel";
        definition.mechanical.density = materials::MaterialProperty<Density>::known(7850_kg_per_m3);
        definition.thermal.thermalConductivity =
            materials::MaterialProperty<ThermalConductivity>::known(45_W_per_m_K);
        definition.thermal.specificHeatCapacity =
            materials::MaterialProperty<SpecificHeatCapacity>::known(460_J_per_kg_K);
        const Result<MaterialId> id = features::createMaterial(document, "Steel", definition);
        REQUIRE(id.has_value());
        material = *id;
        REQUIRE(features::assignMaterial(document, material).has_value());

        auto study = ThermalAnalysis::create("Study", ThermalAnalysisDefinition{.mesh = control});
        REQUIRE(study.has_value());
        analysis = ThermalAnalysisId::fromValue(require(document.addObject(std::move(*study))).value());
        requireReport(regenerator, document);
        requireMesh();
    }

    void requireMesh() {
        const Result<const meshing::VolumeMesh*> mesh = mesher.generate(document, regenerator, control);
        INFO((mesh.has_value() ? std::string{} : mesh.error().message));
        REQUIRE(mesh.has_value());
    }

    [[nodiscard]] const meshing::VolumeMesh& volume() const {
        const meshing::VolumeMesh* held = mesher.mesh(control);
        REQUIRE(held != nullptr);
        return *held;
    }

    [[nodiscard]] ThermalResultSource currentSource() const {
        const Result<ThermalResultSource> source =
            thermal::currentResultSource(document, regenerator, mesher, analysis);
        INFO((source.has_value() ? std::string{} : source.error().message));
        REQUIRE(source.has_value());
        return *source;
    }

    [[nodiscard]] Result<bool> setDefinition(ThermalAnalysisDefinition definition) {
        return document.modifyObject<ThermalAnalysis>(
            ObjectId::fromValue(analysis.value()),
            [&definition](ThermalAnalysis& object) -> Result<bool> { return object.setDefinition(definition); });
    }

    [[nodiscard]] bool meshCurrent() const {
        return mesher.currency(document, control) == meshing::MeshCurrency::Current;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// The numbering
// ---------------------------------------------------------------------------

TEST_CASE("ThermalDof_OneScalarDofPerNode", "[thermal][dof]") {
    // Exactly one unknown per node, numbered 0 .. N-1, and no component
    // anywhere in the type: there is no Ux, no rotation, no flux unknown.
    static_assert(thermal::kThermalDofsPerNode == 1);
    const meshing::Mesh mesh = denseMesh(5);
    const ThermalDofMap map = mapOf(mesh);
    CHECK(map.nodeCount() == 5);
    CHECK(map.dofCount() == 5);

    std::set<ThermalDofIndex::ValueType> seen;
    for (const meshing::Node& node : mesh.nodes()) {
        const ThermalDofIndex index = dofOf(map, node.id);
        CHECK(index.value() < 5);
        CHECK(seen.insert(index.value()).second);
    }
    CHECK(seen == std::set<ThermalDofIndex::ValueType>{0, 1, 2, 3, 4});
}

TEST_CASE("ThermalDof_NodeToDofAndDofToNodeAreExactInverses", "[thermal][dof]") {
    // Over EVERY unknown of three meshes, in both directions. A numbering
    // whose round trip is not the identity is numbering two things the same.
    for (const meshing::Mesh& mesh : {denseMesh(1), denseMesh(7), meshWithNodes(kSparse)}) {
        const ThermalDofMap map = mapOf(mesh);
        REQUIRE(map.dofCount() == mesh.nodeCount());
        for (const meshing::Node& node : mesh.nodes()) {
            const ThermalDofIndex index = dofOf(map, node.id);
            const Result<NodeId> back = map.nodeOf(index);
            REQUIRE(back.has_value());
            CHECK(*back == node.id);
        }
        for (ThermalDofIndex::ValueType value = 0; value < map.dofCount(); ++value) {
            const Result<NodeId> node = map.nodeOf(dof(value));
            REQUIRE(node.has_value());
            CHECK(dofOf(map, *node) == dof(value));
        }
    }
}

TEST_CASE("ThermalDof_SparseNodeIds", "[thermal][dof][sparse]") {
    // THE ORACLE, BY HAND, from P16's documented order (ascending NodeId):
    //
    //     mesh ordinal   NodeId    expected ThermalDofIndex
    //     0              7         0
    //     1              104       1
    //     2              9001      2
    //     3              500000    3
    const meshing::Mesh mesh = meshWithNodes(kSparse);
    const ThermalDofMap map = mapOf(mesh);
    REQUIRE(map.dofCount() == 4);
    CHECK(dofOf(map, NodeId::fromValue(7)) == dof(0));
    CHECK(dofOf(map, NodeId::fromValue(104)) == dof(1));
    CHECK(dofOf(map, NodeId::fromValue(9001)) == dof(2));
    CHECK(dofOf(map, NodeId::fromValue(500000)) == dof(3));
    CHECK(nodeAt(map, dof(0)) == NodeId::fromValue(7));
    CHECK(nodeAt(map, dof(3)) == NodeId::fromValue(500000));
    // And the handles between them resolve to nothing: no clamp, no insert.
    for (const NodeId::ValueType absent : {1U, 8U, 103U, 105U, 9000U, 499999U, 500001U}) {
        CAPTURE(absent);
        CHECK_FALSE(map.dofOf(NodeId::fromValue(absent)).has_value());
    }
}

TEST_CASE("ThermalDof_NodeIdIsNotRawEquationNumber", "[thermal][dof][sparse]") {
    // For the sparse mesh no unknown's index equals its node's handle, and a
    // handle that happens to be a valid index value names no node.
    const meshing::Mesh mesh = meshWithNodes(kSparse);
    const ThermalDofMap map = mapOf(mesh);
    for (const meshing::Node& node : mesh.nodes()) {
        CHECK(dofOf(map, node.id).value() != node.id.value());
    }
    // `temperature[node.value()]` would read index 7 of a 4-entry array.
    CHECK(dofOf(map, NodeId::fromValue(7)).value() < map.dofCount());
    CHECK(NodeId::fromValue(7).value() >= map.dofCount());
    // NodeId 2 is a valid-looking index and is not a node of this mesh.
    CHECK_FALSE(map.dofOf(NodeId::fromValue(2)).has_value());
    CHECK(map.contains(dof(2)));
}

TEST_CASE("ThermalDof_LargeSparseNodeIdDoesNotControlStorageSize", "[thermal][dof][sparse]") {
    // Two nodes, one with a handle near the top of the 32-bit range. Storage
    // and cardinality are those of two nodes, not of four billion.
    const NodeId::ValueType huge = std::numeric_limits<NodeId::ValueType>::max() - 1;
    const meshing::Mesh mesh = meshWithNodes({1, huge});
    const ThermalDofMap map = mapOf(mesh);
    CHECK(map.nodeCount() == 2);
    CHECK(map.dofCount() == 2);
    CHECK(map.nodes().size() == 2);
    CHECK(dofOf(map, NodeId::fromValue(huge)) == dof(1));
    CHECK(nodeAt(map, dof(1)) == NodeId::fromValue(huge));
    CHECK_FALSE(map.contains(dof(2)));
    WARN(std::format("2 nodes, maximum NodeId {}: the map holds {} handles ({} bytes), not {}",
                     huge, map.nodes().size(), map.nodes().size() * sizeof(NodeId), huge));
}

TEST_CASE("ThermalDof_OrdinalsFollowTheMeshOwnNodeEnumeration", "[thermal][dof]") {
    // The map's node list IS the mesh's enumeration, element for element, and
    // not a second ordering of it. MeshBuilder refuses a handle that is not
    // greater than the last, so P16's order is ascending NodeId by
    // construction and the map copies it rather than sorting.
    const meshing::Mesh mesh = meshWithNodes({3, 11, 12, 200, 201});
    const ThermalDofMap map = mapOf(mesh);
    REQUIRE(map.nodes().size() == mesh.nodes().size());
    for (std::size_t i = 0; i < mesh.nodes().size(); ++i) {
        CHECK(map.nodes()[i] == mesh.nodes()[i].id);
        CHECK(dofOf(map, mesh.nodes()[i].id).value() == i);
    }
    // And an out-of-order insertion is refused by P16, not reordered by P18.
    meshing::MeshBuilder builder;
    REQUIRE(builder.addNode(NodeId::fromValue(10), at(1.0, 0.0, 0.0)).has_value());
    const Result<NodeId> backwards = builder.addNode(NodeId::fromValue(5), at(2.0, 0.0, 0.0));
    REQUIRE_FALSE(backwards.has_value());
    CHECK(backwards.error().code == ErrorCode::AlreadyExists);
}

// ---------------------------------------------------------------------------
// Element-local unknowns
// ---------------------------------------------------------------------------

TEST_CASE("ThermalDof_TetLocalDofsPreserveNodeOrder", "[thermal][dof][element]") {
    // A tetrahedron whose local node order is nothing like numeric order:
    // [9001, 7, 500000, 104]. Expected local unknowns, by hand: [2, 0, 3, 1].
    meshing::MeshBuilder builder;
    for (const NodeId::ValueType handle : kSparse) {
        REQUIRE(builder.addNode(NodeId::fromValue(handle), at(static_cast<double>(handle % 1000), 0.0, 0.0))
                    .has_value());
    }
    const std::array<NodeId, 4> local{NodeId::fromValue(9001), NodeId::fromValue(7), NodeId::fromValue(500000),
                                      NodeId::fromValue(104)};
    REQUIRE(builder.addTetrahedron(local, meshing::RegionId::fromValue(1)).has_value());
    const meshing::Mesh mesh = builder.build();
    const ThermalDofMap map = mapOf(mesh);

    REQUIRE(mesh.tetrahedra().size() == 1);
    const meshing::Tetrahedron& element = mesh.tetrahedra().front();
    REQUIRE(element.nodes == local);
    const Result<std::array<ThermalDofIndex, 4>> dofs = map.elementDofs(element);
    REQUIRE(dofs.has_value());
    CHECK((*dofs)[0] == dof(2));
    CHECK((*dofs)[1] == dof(0));
    CHECK((*dofs)[2] == dof(3));
    CHECK((*dofs)[3] == dof(1));
    // Not sorted: the sorted form would be [0, 1, 2, 3].
    CHECK_FALSE(std::ranges::is_sorted(*dofs));
    for (int i = 0; i < 16; ++i) {
        CHECK(localDofs(map, element) == *dofs);
    }
}

TEST_CASE("ThermalDof_SharedNodeUsesSameDof", "[thermal][dof][element]") {
    // Two tetrahedra sharing three nodes: the shared nodes map to the same
    // unknown in both, and five nodes give five unknowns, not eight.
    meshing::MeshBuilder builder;
    for (const NodeId::ValueType handle : {2U, 5U, 9U, 14U, 30U}) {
        REQUIRE(builder.addNode(NodeId::fromValue(handle), at(static_cast<double>(handle), 0.0, 0.0)).has_value());
    }
    const auto n = [](NodeId::ValueType v) { return NodeId::fromValue(v); };
    REQUIRE(builder.addTetrahedron({n(2), n(5), n(9), n(14)}, meshing::RegionId::fromValue(1)).has_value());
    REQUIRE(builder.addTetrahedron({n(30), n(9), n(5), n(14)}, meshing::RegionId::fromValue(1)).has_value());
    const meshing::Mesh mesh = builder.build();
    const ThermalDofMap map = mapOf(mesh);
    CHECK(map.dofCount() == 5);

    const auto first = localDofs(map, mesh.tetrahedra()[0]);
    const auto second = localDofs(map, mesh.tetrahedra()[1]);
    // Node 5 is local 1 in the first and local 2 in the second; node 9 is
    // local 2 and local 1; node 14 is local 3 in both.
    CHECK(first[1] == second[2]);
    CHECK(first[2] == second[1]);
    CHECK(first[3] == second[3]);
    CHECK(first[1] == dof(1));
    CHECK(first[2] == dof(2));
    CHECK(first[3] == dof(3));
    CHECK(second[0] == dof(4));
}

TEST_CASE("ThermalDof_AnElementWithAForeignNodeIsRefusedAndCreatesNothing", "[thermal][dof][element]") {
    const meshing::Mesh mesh = meshWithNodes(kSparse);
    const ThermalDofMap map = mapOf(mesh);
    // A tetrahedron record naming a node this mesh does not have. Built by
    // hand, because the builder would refuse it -- which is the point: the
    // map must refuse it too rather than trusting the record.
    const meshing::Tetrahedron foreign{.id = meshing::ElementId::fromValue(1),
                                       .nodes = {NodeId::fromValue(7), NodeId::fromValue(104),
                                                 NodeId::fromValue(8), NodeId::fromValue(9001)},
                                       .region = meshing::RegionId::fromValue(1)};
    const Result<std::array<ThermalDofIndex, 4>> refused = map.elementDofs(foreign);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == ErrorCode::NotFound);
    CHECK_THAT(refused.error().message, ContainsSubstring("node:8"));
    CHECK(map.dofCount() == 4);
}

// ---------------------------------------------------------------------------
// Refusals
// ---------------------------------------------------------------------------

TEST_CASE("ThermalDof_RefusesAMeshWithNothingToNumber", "[thermal][dof]") {
    SECTION("no nodes") {
        const meshing::Mesh empty = meshing::MeshBuilder{}.build();
        CHECK(thermal::thermalDofMapProblem(empty) == DofMapProblem::MeshHasNoNodes);
        const Result<ThermalDofMap> refused = thermal::buildThermalDofMap(empty);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().code == ErrorCode::FailedPrecondition);
    }
    SECTION("no identity") {
        const meshing::Mesh anonymous;
        CHECK(thermal::thermalDofMapProblem(anonymous) == DofMapProblem::MeshHasNoIdentity);
        REQUIRE_FALSE(thermal::buildThermalDofMap(anonymous).has_value());
    }
    SECTION("one node is numberable, whatever its validity as a volume mesh") {
        const ThermalDofMap one = mapOf(denseMesh(1));
        CHECK(one.dofCount() == 1);
        CHECK(dofOf(one, NodeId::fromValue(1)) == dof(0));
    }
    for (const DofMapProblem problem : {DofMapProblem::MeshHasNoNodes, DofMapProblem::MeshHasNoIdentity}) {
        CHECK(thermal::toString(problem) != "unknown");
    }
}

TEST_CASE("ThermalDof_RefusesALookupItDoesNotHave", "[thermal][dof]") {
    const ThermalDofMap map = mapOf(denseMesh(4));
    SECTION("a node that is not in the mesh") {
        const Result<ThermalDofIndex> refused = map.dofOf(NodeId::fromValue(99));
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().code == ErrorCode::NotFound);
    }
    SECTION("the invalid handle") {
        REQUIRE_FALSE(map.dofOf(NodeId{}).has_value());
    }
    SECTION("an unknown past the end") {
        REQUIRE_FALSE(map.nodeOf(dof(4)).has_value());
        REQUIRE_FALSE(map.nodeOf(dof(std::numeric_limits<ThermalDofIndex::ValueType>::max())).has_value());
        CHECK_FALSE(map.contains(dof(4)));
        CHECK(map.contains(dof(3)));
    }
}

// ---------------------------------------------------------------------------
// Source binding
// ---------------------------------------------------------------------------

TEST_CASE("ThermalDof_BindsExactMeshSource", "[thermal][dof][source]") {
    const meshing::Mesh mesh = meshWithNodes(kSparse);
    const ThermalDofMap map = mapOf(mesh);
    CHECK(map.describes(mesh));
    CHECK(map.stamp() == mesh.stamp());
    // A COPY of the mesh is the same mesh: same stamp, same handles.
    const meshing::Mesh copy = mesh;
    CHECK(map.describes(copy));
    // Two snapshots of ONE builder share a stamp and differ in count; the
    // count closes the gap the stamp alone would leave.
    meshing::MeshBuilder builder;
    REQUIRE(builder.addNode(NodeId::fromValue(1), at(0.0, 0.0, 0.0)).has_value());
    const meshing::Mesh small = builder.build();
    REQUIRE(builder.addNode(NodeId::fromValue(2), at(1.0, 0.0, 0.0)).has_value());
    const meshing::Mesh large = builder.build();
    REQUIRE(small.stamp() == large.stamp());
    const ThermalDofMap smallMap = mapOf(small);
    CHECK(smallMap.describes(small));
    CHECK_FALSE(smallMap.describes(large));
}

TEST_CASE("ThermalDof_SameCountsAndSameIdsNewMeshIsStale", "[thermal][dof][source]") {
    // THE HARD GATE. Two meshes, same node count, same handles, same
    // positions, built separately: different sources. Neither map describes
    // the other's mesh, and a constraint map from one is refused against the
    // other's numbering.
    const meshing::Mesh first = meshWithNodes(kSparse);
    const meshing::Mesh second = meshWithNodes(kSparse);
    REQUIRE(first.nodeCount() == second.nodeCount());
    for (std::size_t i = 0; i < first.nodeCount(); ++i) {
        REQUIRE(first.nodes()[i].id == second.nodes()[i].id);
    }
    REQUIRE_FALSE(first.stamp() == second.stamp());

    const ThermalDofMap d1 = mapOf(first);
    const ThermalDofMap d2 = mapOf(second);
    CHECK(d1.describes(first));
    CHECK_FALSE(d1.describes(second));
    CHECK_FALSE(d2.describes(first));
    CHECK_FALSE(d1 == d2);
    // The numbers agree -- which is exactly why identity, not numbers, decides.
    CHECK(d1.nodes().size() == d2.nodes().size());
    CHECK(std::ranges::equal(d1.nodes(), d2.nodes()));

    const ThermalConstraintMap c1 = constraintsOf(d1, {hold(1, 300.0)});
    const Result<ThermalFreeEquationMap> refused = thermal::buildThermalFreeEquationMap(d2, c1);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == ErrorCode::FailedPrecondition);
    CHECK_FALSE(equationsOf(d1, c1).agreesWith(d2));
}

TEST_CASE("ThermalDof_RemeshInvalidatesOldMap", "[thermal][dof][source]") {
    // THE CENTRAL CLAIM, through the ordinary pipeline: no solver identity
    // survives a remesh, even one that reproduces every count and handle.
    ThermalDofStudy part;
    const meshing::MeshStamp before = part.volume().mesh().stamp();
    const ThermalDofMap d1 = mapOf(part.volume().mesh());
    const ThermalConstraintMap c1 = constraintsOf(d1, {hold(0, 300.0)});
    const ThermalFreeEquationMap f1 = equationsOf(d1, c1);
    REQUIRE(f1.freeCount() == d1.dofCount() - 1);
    std::vector<NodeId> idsBefore(d1.nodes().begin(), d1.nodes().end());

    part.requireMesh();
    const meshing::VolumeMesh& fresh = part.volume();
    REQUIRE_FALSE(fresh.mesh().stamp() == before);
    std::vector<NodeId> idsAfter;
    for (const meshing::Node& node : fresh.mesh().nodes()) {
        idsAfter.push_back(node.id);
    }
    REQUIRE(idsAfter == idsBefore);

    CHECK_FALSE(d1.describes(fresh));
    const ThermalDofMap d2 = mapOf(fresh.mesh());
    CHECK(d2.describes(fresh));
    CHECK_FALSE(d2 == d1);
    CHECK(std::ranges::equal(d1.nodes(), d2.nodes()));
    REQUIRE_FALSE(thermal::buildThermalFreeEquationMap(d2, c1).has_value());
    CHECK_FALSE(f1.agreesWith(d2));
    // The recovery: constraints resolved again against the fresh numbering.
    const ThermalConstraintMap c2 = constraintsOf(d2, {hold(0, 300.0)});
    CHECK(thermal::buildThermalFreeEquationMap(d2, c2).has_value());

    WARN(std::format("remesh: {} nodes with identical NodeIds either way; old map refused by stamp",
                     idsBefore.size()));
}

// ---------------------------------------------------------------------------
// Invalidation granularity
// ---------------------------------------------------------------------------

TEST_CASE("ThermalDof_MaterialEditKeepsDofMapCurrent", "[thermal][dof][source]") {
    // THE DISTINCTION P18-MAT-001 FROZE, carried one layer up: a material edit
    // leaves the mesh current, so the numbering built from it stays current
    // and equal to a rebuild -- while the thermal RESULT goes stale.
    ThermalDofStudy part;
    const ThermalDofMap map = mapOf(part.volume().mesh());
    const ThermalResultSource before = part.currentSource();

    materials::ThermalProperties changed = features::findMaterial(part.document, part.material)->definition().thermal;
    changed.thermalConductivity = materials::MaterialProperty<ThermalConductivity>::known(50_W_per_m_K);
    REQUIRE(features::setMaterialThermal(part.document, part.material, changed).has_value());

    CHECK(part.meshCurrent());
    CHECK(map.describes(part.volume()));
    CHECK(mapOf(part.volume().mesh()) == map);
    const ThermalResultSource after = part.currentSource();
    CHECK(thermal::staleReasons(before, after) == std::vector{thermal::StaleReason::Material});
    CHECK(after.mesh == before.mesh);
}

TEST_CASE("ThermalDof_LoadSolverAndBoundaryConditionEditsKeepDofMapCurrent", "[thermal][dof][source]") {
    // Analysis intent -- loads, solver settings, boundary conditions -- is not
    // a dependency of the numbering. The mesh does not move, so the map does
    // not either; the result source moves on the analysis alone.
    ThermalDofStudy part;
    const ThermalDofMap map = mapOf(part.volume().mesh());
    const ThermalResultSource before = part.currentSource();
    const FaceName endCap{part.feature, FaceSelector{.role = FaceRole::EndCap}};

    const auto check = [&] {
        CHECK(part.meshCurrent());
        CHECK(map.describes(part.volume()));
        CHECK(mapOf(part.volume().mesh()) == map);
        CHECK(thermal::staleReasons(before, part.currentSource()) ==
              std::vector{thermal::StaleReason::Analysis});
    };
    SECTION("a heat flux load is added") {
        ThermalAnalysisDefinition definition{.mesh = part.control};
        definition.loads.emplace_back(ThermalLoadId::fromValue(1),
                                      thermal::SurfaceHeatFluxLoad{.face = endCap, .flux = 500_W_per_m2});
        REQUIRE(part.setDefinition(definition).has_value());
        check();
    }
    SECTION("the solver tolerance is edited") {
        ThermalAnalysisDefinition definition{.mesh = part.control};
        definition.solver.relativeResidualTolerance = 1e-10;
        REQUIRE(part.setDefinition(definition).has_value());
        check();
    }
    SECTION("a prescribed-temperature boundary condition is added") {
        ThermalAnalysisDefinition definition{.mesh = part.control};
        definition.boundaryConditions.emplace_back(
            ThermalBoundaryConditionId::fromValue(1),
            thermal::PrescribedTemperature{.face = endCap, .value = 350_K});
        REQUIRE(part.setDefinition(definition).has_value());
        check();
    }
}

TEST_CASE("ThermalDof_AChangedConstraintSetStalesTheFreeMapButNotTheDofMap", "[thermal][dof][source]") {
    // The hierarchy: mesh -> DofMap; DofMap + constraints -> ConstraintMap;
    // DofMap + ConstraintMap -> FreeEquationMap. A change to WHICH unknowns
    // are prescribed leaves the DofMap alone and makes the old free map
    // disagree with the new constraint map. P18-BC-001 will feed real
    // boundary conditions in; the seam is exercised here with synthetic ones.
    const ThermalDofMap map = mapOf(denseMesh(6));
    const ThermalConstraintMap a = constraintsOf(map, {hold(1, 300.0), hold(4, 350.0)});
    const ThermalFreeEquationMap fa = equationsOf(map, a);
    CHECK(fa.agreesWith(map));
    CHECK(fa.agreesWith(a));

    const ThermalConstraintMap b = constraintsOf(map, {hold(1, 300.0), hold(2, 350.0)});
    CHECK_FALSE(fa.agreesWith(b));
    CHECK(equationsOf(map, b).agreesWith(b));
    CHECK_FALSE(equationsOf(map, b) == fa);

    // The same SET at different VALUES has the same free topology, and the
    // map says so -- which is exactly why a reduced system may never be
    // called current on the strength of a matching free map alone: Qf - Kfc Tc
    // moved. Recorded in the header; measured here.
    const ThermalConstraintMap aHotter = constraintsOf(map, {hold(1, 320.0), hold(4, 380.0)});
    CHECK(fa.agreesWith(aHotter));
    CHECK_FALSE(a == aHotter);
}

// ---------------------------------------------------------------------------
// The constraint map
// ---------------------------------------------------------------------------

TEST_CASE("ThermalDof_ConstraintMapStoresNonzeroTemperature", "[thermal][dof][constraint]") {
    // THE P18-SPECIFIC FACT: the value is part of the constraint, in kelvin,
    // and it is 300 and 350, not 0. The mutation that forces every prescribed
    // temperature to zero -- P17's `uc = 0` copied across -- fails here.
    const ThermalDofMap map = mapOf(denseMesh(5));
    const ThermalConstraintMap constraints = constraintsOf(map, {hold(1, 300.0), hold(4, 350.0)});
    REQUIRE(constraints.size() == 2);
    CHECK(constraints.isConstrained(dof(1)));
    CHECK(constraints.isConstrained(dof(4)));
    REQUIRE(constraints.prescribedTemperature(dof(1)).has_value());
    REQUIRE(constraints.prescribedTemperature(dof(4)).has_value());
    CHECK(constraints.prescribedTemperature(dof(1))->si() == 300.0);
    CHECK(constraints.prescribedTemperature(dof(4))->si() == 350.0);
    CHECK(constraints.prescribedTemperature(dof(1))->si() != 0.0);
    // A free unknown has NO prescribed temperature, not a temperature of 0 K.
    CHECK_FALSE(constraints.isConstrained(dof(0)));
    CHECK_FALSE(constraints.prescribedTemperature(dof(0)).has_value());
    CHECK_FALSE(constraints.prescribedTemperature(dof(5)).has_value());
    static_assert(std::is_same_v<decltype(ThermalTemperatureConstraint::temperature), Temperature>);
    static_assert(!std::is_convertible_v<double, Temperature>);
    static_assert(!std::is_default_constructible_v<ThermalTemperatureConstraint>,
                  "a constraint cannot exist without its unknown");
}

TEST_CASE("ThermalDof_ConstraintMapDeterministic", "[thermal][dof][constraint]") {
    // {8, 2, 5} in every insertion order gives the same map, ordered 2, 5, 8,
    // and the same again over repeated construction.
    const ThermalDofMap map = mapOf(denseMesh(10));
    std::vector<ThermalTemperatureConstraint> listed{hold(8, 310.0), hold(2, 320.0), hold(5, 330.0)};
    std::ranges::sort(listed, {}, [](const ThermalTemperatureConstraint& c) { return c.dof; });
    const ThermalConstraintMap canonical = constraintsOf(map, listed);
    REQUIRE(canonical.size() == 3);
    CHECK(canonical.constrained()[0].dof == dof(2));
    CHECK(canonical.constrained()[1].dof == dof(5));
    CHECK(canonical.constrained()[2].dof == dof(8));
    CHECK(canonical.constrained()[0].temperature.si() == 320.0);
    CHECK(canonical.constrained()[2].temperature.si() == 310.0);

    std::vector<ThermalTemperatureConstraint> permuted = listed;
    int permutations = 0;
    do {
        CHECK(constraintsOf(map, permuted) == canonical);
        ++permutations;
    } while (std::ranges::next_permutation(permuted, {}, [](const ThermalTemperatureConstraint& c) {
                 return c.dof;
             }).found);
    CHECK(permutations == 6);
    for (int i = 0; i < 16; ++i) {
        CHECK(constraintsOf(map, listed) == canonical);
    }
}

TEST_CASE("ThermalDof_DuplicateIdenticalConstraintDeduplicates", "[thermal][dof][constraint]") {
    // DOF 4 = 300 K, stated twice: one mathematical constraint, counted once.
    const ThermalDofMap map = mapOf(denseMesh(6));
    const ThermalConstraintMap constraints = constraintsOf(map, {hold(4, 300.0), hold(4, 300.0), hold(1, 310.0)});
    CHECK(constraints.size() == 2);
    CHECK(constraints.prescribedTemperature(dof(4))->si() == 300.0);
    const ThermalFreeEquationMap equations = equationsOf(map, constraints);
    CHECK(equations.constrainedCount() == 2);
    CHECK(equations.freeCount() == 4);
    // And it equals the map built from the un-duplicated list.
    CHECK(constraints == constraintsOf(map, {hold(1, 310.0), hold(4, 300.0)}));
}

TEST_CASE("ThermalDof_ConflictingConstraintFails", "[thermal][dof][constraint]") {
    // DOF 4 = 300 K and DOF 4 = 350 K: refused, both values named, in either
    // listing order. Not the first, not the last, not the mean.
    const ThermalDofMap map = mapOf(denseMesh(6));
    for (const auto& listed : {std::vector{hold(4, 300.0), hold(4, 350.0)},
                               std::vector{hold(4, 350.0), hold(4, 300.0)},
                               std::vector{hold(1, 290.0), hold(4, 350.0), hold(2, 295.0), hold(4, 300.0)}}) {
        CHECK(thermal::thermalConstraintProblem(map, listed) == ConstraintProblem::ConflictingTemperature);
        const Result<ThermalConstraintMap> refused = thermal::buildThermalConstraintMap(map, listed);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().code == ErrorCode::InvalidArgument);
        CHECK_THAT(refused.error().message, ContainsSubstring("unknown 4"));
        CHECK_THAT(refused.error().message, ContainsSubstring("300"));
        CHECK_THAT(refused.error().message, ContainsSubstring("350"));
    }
    // Exact equality decides "the same": 300 K and 300.000001 K conflict.
    CHECK(thermal::thermalConstraintProblem(map, std::vector{hold(4, 300.0), hold(4, 300.000001)}) ==
          ConstraintProblem::ConflictingTemperature);
}

TEST_CASE("ThermalDof_ConstraintMapRefusesUnusableInput", "[thermal][dof][constraint]") {
    const ThermalDofMap map = mapOf(denseMesh(4));
    SECTION("an unknown the numbering does not have") {
        CHECK(thermal::thermalConstraintProblem(map, std::vector{hold(4, 300.0)}) == ConstraintProblem::DofNotInMap);
        const Result<ThermalConstraintMap> refused = thermal::buildThermalConstraintMap(map, std::vector{hold(4, 300.0)});
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().code == ErrorCode::NotFound);
    }
    SECTION("a temperature that is not absolute") {
        for (const double bad : {kNaN, kInf, -kInf, -1.0}) {
            CAPTURE(bad);
            CHECK(thermal::thermalConstraintProblem(map, std::vector{hold(1, bad)}) ==
                  ConstraintProblem::TemperatureNotAbsolute);
            const Result<ThermalConstraintMap> refused =
                thermal::buildThermalConstraintMap(map, std::vector{hold(1, bad)});
            REQUIRE_FALSE(refused.has_value());
            CHECK_THAT(refused.error().message, ContainsSubstring("absolute temperature"));
        }
        // 0 K is the limit of the scale and is accepted, as everywhere in P18.
        CHECK(thermal::buildThermalConstraintMap(map, std::vector{hold(1, 0.0)}).has_value());
    }
    for (const ConstraintProblem problem : {ConstraintProblem::DofNotInMap, ConstraintProblem::TemperatureNotAbsolute,
                                            ConstraintProblem::ConflictingTemperature}) {
        CHECK(thermal::toString(problem) != "unknown");
    }
}

TEST_CASE("ThermalDof_FullyConstrainedAndNoConstraintsAreSupported", "[thermal][dof][constraint]") {
    const ThermalDofMap map = mapOf(denseMesh(4));
    SECTION("nothing prescribed: Nfree = Ndof") {
        const ThermalConstraintMap none = constraintsOf(map, {});
        CHECK(none.isEmpty());
        const ThermalFreeEquationMap equations = equationsOf(map, none);
        CHECK(equations.freeCount() == 4);
        CHECK(equations.constrainedCount() == 0);
        for (ThermalDofIndex::ValueType d = 0; d < 4; ++d) {
            CHECK(equations.equationOf(dof(d)) == ThermalFreeEquationIndex::fromValue(d));
        }
    }
    SECTION("everything prescribed: Nfree = 0") {
        const ThermalConstraintMap all = constraintsOf(map, {hold(0, 300.0), hold(1, 310.0), hold(2, 320.0), hold(3, 330.0)});
        CHECK(all.size() == 4);
        const ThermalFreeEquationMap equations = equationsOf(map, all);
        CHECK(equations.freeCount() == 0);
        CHECK(equations.constrainedCount() == 4);
        CHECK(equations.freeDofs().empty());
        for (ThermalDofIndex::ValueType d = 0; d < 4; ++d) {
            CHECK_FALSE(equations.equationOf(dof(d)).has_value());
            CHECK(equations.isConstrained(dof(d)));
        }
        CHECK_FALSE(equations.dofOf(ThermalFreeEquationIndex::fromValue(0)).has_value());
        // And the full vector is the prescribed vector, with no reduced part.
        const Result<std::vector<Temperature>> full = thermal::reconstructTemperatures(equations, all, {});
        REQUIRE(full.has_value());
        REQUIRE(full->size() == 4);
        CHECK((*full)[2].si() == 320.0);
    }
}

// ---------------------------------------------------------------------------
// The free-equation map
// ---------------------------------------------------------------------------

TEST_CASE("ThermalDof_FreeEquationMap", "[thermal][dof][free]") {
    // THE ORACLE, BY HAND: unknowns 0..4, prescribed 1 and 4.
    //
    //     DOF 0 -> row 0     DOF 2 -> row 1     DOF 3 -> row 2     Nfree 3
    const ThermalDofMap map = mapOf(denseMesh(5));
    const ThermalConstraintMap constraints = constraintsOf(map, {hold(1, 300.0), hold(4, 350.0)});
    const ThermalFreeEquationMap equations = equationsOf(map, constraints);
    CHECK(equations.dofCount() == 5);
    CHECK(equations.freeCount() == 3);
    CHECK(equations.constrainedCount() == 2);
    CHECK(equations.equationOf(dof(0)) == ThermalFreeEquationIndex::fromValue(0));
    CHECK(equations.equationOf(dof(2)) == ThermalFreeEquationIndex::fromValue(1));
    CHECK(equations.equationOf(dof(3)) == ThermalFreeEquationIndex::fromValue(2));
    CHECK_FALSE(equations.equationOf(dof(1)).has_value());
    CHECK_FALSE(equations.equationOf(dof(4)).has_value());
    // Reverse.
    CHECK(equations.dofOf(ThermalFreeEquationIndex::fromValue(0)) == dof(0));
    CHECK(equations.dofOf(ThermalFreeEquationIndex::fromValue(1)) == dof(2));
    CHECK(equations.dofOf(ThermalFreeEquationIndex::fromValue(2)) == dof(3));
    CHECK_FALSE(equations.dofOf(ThermalFreeEquationIndex::fromValue(3)).has_value());
    // freeDofs is the row -> unknown table, ascending.
    REQUIRE(equations.freeDofs().size() == 3);
    CHECK(equations.freeDofs()[0] == dof(0));
    CHECK(equations.freeDofs()[1] == dof(2));
    CHECK(equations.freeDofs()[2] == dof(3));
}

TEST_CASE("ThermalDof_EveryDofIsFreeXorConstrainedAndRowsAreContiguous", "[thermal][dof][free]") {
    // Ndof = 10, unique constraints {1, 4, 7}: Nconstrained 3, Nfree 7, every
    // unknown in exactly one class, rows exactly 0..6 as a set and by order.
    // Cross-checked against the constraint map's own list, which is the
    // non-tautological form: two independently built objects compared.
    const ThermalDofMap map = mapOf(denseMesh(10));
    const ThermalConstraintMap constraints = constraintsOf(map, {hold(7, 300.0), hold(1, 310.0), hold(4, 320.0)});
    const ThermalFreeEquationMap equations = equationsOf(map, constraints);
    CHECK(equations.constrainedCount() == 3);
    CHECK(equations.freeCount() == 7);
    CHECK(equations.freeCount() + equations.constrainedCount() == equations.dofCount());

    std::set<ThermalFreeEquationIndex::ValueType> rows;
    ThermalFreeEquationIndex::ValueType expectedRow = 0;
    for (ThermalDofIndex::ValueType d = 0; d < 10; ++d) {
        const bool free = equations.isFree(dof(d));
        const bool constrained = equations.isConstrained(dof(d));
        CHECK(free != constrained);
        CHECK(constrained == constraints.isConstrained(dof(d)));
        if (free) {
            const std::optional<ThermalFreeEquationIndex> row = equations.equationOf(dof(d));
            REQUIRE(row.has_value());
            CHECK(row->value() == expectedRow);
            ++expectedRow;
            CHECK(rows.insert(row->value()).second);
            CHECK(equations.dofOf(*row) == dof(d));
        }
    }
    CHECK(rows == std::set<ThermalFreeEquationIndex::ValueType>{0, 1, 2, 3, 4, 5, 6});
    // An unknown of another size is neither free nor constrained: absent.
    CHECK_FALSE(equations.isFree(dof(10)));
    CHECK_FALSE(equations.isConstrained(dof(10)));
}

TEST_CASE("ThermalDof_FreeEquationOrderingDeterministic", "[thermal][dof][free]") {
    const ThermalDofMap map = mapOf(meshWithNodes({3, 40, 41, 900, 901, 9000}));
    std::vector<ThermalTemperatureConstraint> listed{hold(5, 300.0), hold(0, 310.0), hold(3, 320.0)};
    const ThermalFreeEquationMap first = equationsOf(map, constraintsOf(map, listed));
    REQUIRE(first.freeCount() == 3);
    CHECK(first.freeDofs()[0] == dof(1));
    CHECK(first.freeDofs()[1] == dof(2));
    CHECK(first.freeDofs()[2] == dof(4));
    std::ranges::sort(listed, {}, [](const ThermalTemperatureConstraint& c) { return c.dof; });
    std::vector<ThermalTemperatureConstraint> permuted = listed;
    do {
        CHECK(equationsOf(map, constraintsOf(map, permuted)) == first);
    } while (std::ranges::next_permutation(permuted, {}, [](const ThermalTemperatureConstraint& c) {
                 return c.dof;
             }).found);
    for (int i = 0; i < 16; ++i) {
        CHECK(equationsOf(map, constraintsOf(map, listed)) == first);
    }
    // A fresh mesh with the same handles is a fresh source: its free map is
    // equal in every number and unequal as a value, because the stamp differs.
    const ThermalDofMap fresh = mapOf(meshWithNodes({3, 40, 41, 900, 901, 9000}));
    const ThermalFreeEquationMap other = equationsOf(fresh, constraintsOf(fresh, listed));
    CHECK(std::ranges::equal(other.freeDofs(), first.freeDofs()));
    CHECK_FALSE(other == first);
}

TEST_CASE("ThermalDof_FreeMapRejectsDifferentDofSource", "[thermal][dof][free][source]") {
    SECTION("a constraint map from another mesh") {
        const ThermalDofMap a = mapOf(denseMesh(5));
        const ThermalDofMap b = mapOf(denseMesh(5));
        const ThermalConstraintMap fromA = constraintsOf(a, {hold(1, 300.0)});
        REQUIRE_FALSE(thermal::buildThermalFreeEquationMap(b, fromA).has_value());
        CHECK_FALSE(equationsOf(a, fromA).agreesWith(b));
    }
    SECTION("a constraint map from a larger snapshot of the same builder") {
        meshing::MeshBuilder builder;
        for (NodeId::ValueType i = 1; i <= 5; ++i) {
            REQUIRE(builder.addNode(at(static_cast<double>(i), 0.0, 0.0)).has_value());
        }
        const meshing::Mesh small = builder.build();
        for (NodeId::ValueType i = 6; i <= 10; ++i) {
            REQUIRE(builder.addNode(at(static_cast<double>(i), 0.0, 0.0)).has_value());
        }
        const meshing::Mesh large = builder.build();
        const ThermalDofMap smallMap = mapOf(small);
        const ThermalDofMap largeMap = mapOf(large);
        REQUIRE(smallMap.stamp() == largeMap.stamp());
        const ThermalConstraintMap fromLarge = constraintsOf(largeMap, {hold(9, 300.0)});
        // Same stamp, different count: refused, and NOT accepted as "every
        // unknown free" -- the defect P17-DOF-001 found in itself.
        const Result<ThermalFreeEquationMap> refused = thermal::buildThermalFreeEquationMap(smallMap, fromLarge);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().code == ErrorCode::FailedPrecondition);
    }
}

TEST_CASE("ThermalDof_FullReconstructionOracle", "[thermal][dof][free]") {
    // Ndof = 5; DOF 1 = 300 K, DOF 4 = 350 K; reduced solution 310, 320, 330.
    // Expected, by hand: [310, 300, 320, 330, 350].
    const ThermalDofMap map = mapOf(denseMesh(5));
    const ThermalConstraintMap constraints = constraintsOf(map, {hold(1, 300.0), hold(4, 350.0)});
    const ThermalFreeEquationMap equations = equationsOf(map, constraints);
    const std::vector<Temperature> reduced{310_K, 320_K, 330_K};
    const Result<std::vector<Temperature>> full = thermal::reconstructTemperatures(equations, constraints, reduced);
    REQUIRE(full.has_value());
    REQUIRE(full->size() == 5);
    CHECK((*full)[0].si() == 310.0);
    CHECK((*full)[1].si() == 300.0);
    CHECK((*full)[2].si() == 320.0);
    CHECK((*full)[3].si() == 330.0);
    CHECK((*full)[4].si() == 350.0);

    // The wrong number of reduced values is refused, and so is a constraint
    // map the free map was not built from.
    REQUIRE_FALSE(thermal::reconstructTemperatures(equations, constraints, std::vector<Temperature>{310_K, 320_K}).has_value());
    const ThermalConstraintMap other = constraintsOf(map, {hold(2, 300.0)});
    const Result<std::vector<Temperature>> refused = thermal::reconstructTemperatures(equations, other, reduced);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == ErrorCode::FailedPrecondition);
    // And the full vector is what ThermalResult::create takes: parallel to
    // the mesh's nodes, so it can be handed straight to the result container.
    CHECK(full->size() == map.nodeCount());
}

// ---------------------------------------------------------------------------
// Integration with the result container
// ---------------------------------------------------------------------------

TEST_CASE("ThermalDof_ResultArrayIsIndexedByThermalDof", "[thermal][dof][result]") {
    // `temperatures()[dof]` IS `temperatureOf(mesh, node)` for the node whose
    // unknown that is -- P18-DATA-001's "parallel to mesh.nodes()" and this
    // milestone's "the unknown is the node ordinal" are one convention.
    ThermalDofStudy part;
    const meshing::VolumeMesh& mesh = part.volume();
    const ThermalDofMap map = mapOf(mesh.mesh());
    std::vector<Temperature> temperatures;
    for (std::size_t i = 0; i < mesh.nodeCount(); ++i) {
        temperatures.push_back(Temperature::fromSi(300.0 + 7.0 * static_cast<double>(i)));
    }
    const Result<ThermalResult> built = ThermalResult::create(
        part.currentSource(), mesh, temperatures, std::vector<thermal::TemperatureGradient3>(mesh.tetrahedronCount()),
        std::vector<thermal::HeatFlux3>(mesh.tetrahedronCount()), {}, std::nullopt);
    REQUIRE(built.has_value());
    const ThermalResult& result = *built;
    REQUIRE(result.temperatures().size() == map.dofCount());
    for (const meshing::Node& node : mesh.mesh().nodes()) {
        const ThermalDofIndex index = dofOf(map, node.id);
        REQUIRE(index.value() < result.temperatures().size());
        CHECK(result.temperatures()[static_cast<std::size_t>(index.value())] == temperatureAt(result, mesh, node.id));
    }
    // Element-local gather: four temperatures in the element's own node order.
    for (const meshing::Tetrahedron& element : mesh.mesh().tetrahedra()) {
        const auto local = localDofs(map, element);
        for (std::size_t i = 0; i < 4; ++i) {
            REQUIRE(local[i].value() < result.temperatures().size());
            CHECK(result.temperatures()[static_cast<std::size_t>(local[i].value())] ==
                  temperatureAt(result, mesh, element.nodes[i]));
        }
    }

    // After a remesh, the old array and the old map are both refused against
    // the new mesh even though every length agrees.
    part.requireMesh();
    const meshing::VolumeMesh& fresh = part.volume();
    REQUIRE(fresh.nodeCount() == mesh.nodeCount());
    CHECK_FALSE(map.describes(fresh));
    CHECK_FALSE(result.describes(fresh));
    REQUIRE_FALSE(result.temperatureOf(fresh, fresh.mesh().nodes().front().id).has_value());
}

// ---------------------------------------------------------------------------
// The type model, determinism and scale
// ---------------------------------------------------------------------------

TEST_CASE("ThermalDof_SolverLocalTypesAreDistinct", "[thermal][dof][types]") {
    static_assert(!std::is_convertible_v<NodeId, ThermalDofIndex>);
    static_assert(!std::is_convertible_v<ThermalDofIndex, NodeId>);
    static_assert(!std::is_convertible_v<meshing::ElementId, ThermalDofIndex>);
    static_assert(!std::is_convertible_v<ThermalDofIndex, ThermalFreeEquationIndex>);
    static_assert(!std::is_convertible_v<ThermalFreeEquationIndex, ThermalDofIndex>);
    static_assert(!std::is_convertible_v<std::uint64_t, ThermalDofIndex>);
    static_assert(!std::is_convertible_v<ThermalDofIndex, std::uint64_t>);
    static_assert(!std::is_convertible_v<ThermalDofIndex, ObjectId>);
    static_assert(!std::is_constructible_v<ThermalDofIndex, NodeId>);
    static_assert(!std::is_default_constructible_v<ThermalDofIndex>);
    static_assert(!std::is_default_constructible_v<ThermalFreeEquationIndex>);
    static_assert(!std::is_default_constructible_v<ThermalDofMap>);
    static_assert(!std::is_default_constructible_v<ThermalConstraintMap>);
    static_assert(!std::is_default_constructible_v<ThermalFreeEquationMap>);
    static_assert(std::is_same_v<ThermalDofIndex::ValueType, std::uint64_t>);
    static_assert(sizeof(ThermalDofIndex::ValueType) > sizeof(NodeId::ValueType));

    // DERIVED, NEVER CANONICAL: nothing here can enter the analysis definition.
    static_assert(!std::is_constructible_v<ThermalAnalysisDefinition, ThermalDofMap>);
    static_assert(!std::is_constructible_v<ThermalAnalysisDefinition, ThermalConstraintMap>);
    static_assert(!std::is_constructible_v<ThermalAnalysisDefinition, ThermalFreeEquationMap>);
    static_assert(!std::is_constructible_v<ThermalAnalysisDefinition, ThermalDofIndex>);
    static_assert(!std::is_constructible_v<thermal::PrescribedTemperature, ThermalDofIndex, Temperature>,
                  "a canonical boundary condition names a CAD face, never an unknown");
    static_assert(!std::is_constructible_v<thermal::PrescribedTemperature, NodeId, Temperature>,
                  "and never a node");
    SUCCEED("the conversions that would confuse the index spaces do not exist");
}

TEST_CASE("ThermalDof_MapConstructionIsDeterministic", "[thermal][dof][determinism]") {
    const meshing::Mesh mesh = meshWithNodes(kSparse);
    const ThermalDofMap first = mapOf(mesh);
    for (int i = 0; i < 16; ++i) {
        const ThermalDofMap again = mapOf(mesh);
        CHECK(again == first);
        CHECK(std::ranges::equal(again.nodes(), first.nodes()));
        for (const meshing::Node& node : mesh.nodes()) {
            CHECK(dofOf(again, node.id) == dofOf(first, node.id));
        }
    }
}

TEST_CASE("ThermalDof_ScalesWithoutOverflowingTheIndexSpace", "[thermal][dof]") {
    // The bound is a compile-time proof (see the header); a 20 000-node mesh
    // is numbered and round-tripped as a runtime check that nothing narrows.
    const meshing::Mesh mesh = denseMesh(20000);
    const ThermalDofMap map = mapOf(mesh);
    CHECK(map.dofCount() == 20000);
    CHECK(dofOf(map, NodeId::fromValue(20000)) == dof(19999));
    CHECK(nodeAt(map, dof(19999)) == NodeId::fromValue(20000));
    CHECK(dofOf(map, NodeId::fromValue(1)) == dof(0));
}

TEST_CASE("ThermalDof_MultipleDocumentsHaveIndependentMaps", "[thermal][dof][source]") {
    // No global, no static: two documents' meshes with the same NodeIds give
    // two maps that describe only their own mesh.
    ThermalDofStudy a;
    ThermalDofStudy b;
    const ThermalDofMap ma = mapOf(a.volume().mesh());
    const ThermalDofMap mb = mapOf(b.volume().mesh());
    CHECK(std::ranges::equal(ma.nodes(), mb.nodes()));
    CHECK(ma.describes(a.volume()));
    CHECK(mb.describes(b.volume()));
    CHECK_FALSE(ma.describes(b.volume()));
    CHECK_FALSE(mb.describes(a.volume()));
    CHECK_FALSE(ma == mb);
}
