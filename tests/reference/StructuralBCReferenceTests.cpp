// P17-BC-001 against the meshing reference models.
//
// WHY THESE ARE SEPARATE from tests/structural/StructuralBCTests.cpp. That
// file uses one block whose every planar face carries exactly four mapped
// nodes -- its four CAD corners, and no sizing control changes that, because a
// plane is exactly representable. Four claims cannot be made there:
//
//   a CURVED face carries many more nodes than it has bounding vertices, so
//     "all the mapped nodes, not just the corners" has something to measure
//   a RIGID TRANSFORM rotates the face and NOT the component convention: a
//     global ux restraint is still global ux
//   LOCAL REFINEMENT changes the constrained node count, and the semantic
//     invariant is not a count at all
//   the drilled-hole wall has no canonical reference, and is REFUSED rather
//     than approximated
//
// The last of those is the honest one: it is a P16 limitation, the refusal is
// what passes, and no geometric fallback is implemented.
//
// NO COUNT IS COMPARED ACROSS TWO MESHES (brief section 133). A finer mesh has
// more nodes on the same face and should have more constrained degrees of
// freedom. What survives a remesh is the semantic invariant of brief section
// 132 -- every current node mapped to the restrained region carries the
// requested components -- so that is what is asserted.

#include "reference/MeshTestSupport.hpp"

#include <MeshReferenceModels.hpp>

#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/meshing/MeshSizing.hpp>
#include <bettercad/structural/StructuralConstraints.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace bettercad;
using namespace bettercad::test;
using namespace bettercad::test::meshref;
using structural::ConstraintSet;
using structural::DofComponent;
using structural::DofIndex;
using structural::FreeEquationMap;
using structural::MeshDofMap;
using structural::NodalDof;
using structural::PreparedRestraints;
using structural::RestraintComponents;
using structural::RestraintProblem;
using structural::StructuralModel;
using structural::StructuralRestraint;

/// Gives @p document a steel, which `requireStructuralModel` asks for on
/// behalf of the whole of P17. A restraint has no material of its own; see
/// `StructuralBC_IsIndependentOfTheMaterial`.
void assignSteel(Document& document) {
    features::MaterialDefinition definition;
    definition.designation = "Steel";
    definition.mechanical.youngsModulus =
        materials::MaterialProperty<ElasticModulus>::known(ElasticModulus::fromSi(210.0e9));
    definition.mechanical.poissonRatio =
        materials::MaterialProperty<bettercad::PoissonRatio>::known(PoissonRatio::of(0.3));
    const Result<MaterialId> id = features::createMaterial(document, "Steel", definition);
    REQUIRE(id.has_value());
    REQUIRE(features::assignMaterial(document, *id).has_value());
}

[[nodiscard]] StructuralModel modelOf(MeshedReference& model) {
    Result<StructuralModel> prepared = structural::requireStructuralModel(
        model.document(), model.regenerator(), model.mesher(), model.control());
    INFO((prepared.has_value() ? std::string{} : prepared.error().message));
    REQUIRE(prepared.has_value());
    return std::move(*prepared);
}

[[nodiscard]] MeshDofMap numberingOf(const StructuralModel& model) {
    Result<MeshDofMap> numbering = structural::buildMeshDofMap(model.mesh().mesh());
    INFO((numbering.has_value() ? std::string{} : numbering.error().message));
    REQUIRE(numbering.has_value());
    return std::move(*numbering);
}

/// THE EXPECTED NODE SET, assembled in the test from P16's facet mapping.
///
/// Walks the facets P16 attributes to the face, takes all three corners of
/// each, and deduplicates with its own `std::set`. Deliberately NOT
/// `meshing::boundaryNodesOf`, which is the production path.
[[nodiscard]] std::set<meshing::NodeId::ValueType> expectedNodes(const StructuralModel& model,
                                                                 const FaceName& face) {
    Result<meshing::BoundaryFacetSet> facets = meshing::boundaryFacetsOf(model.map(), face);
    INFO((facets.has_value() ? std::string{} : facets.error().message));
    REQUIRE(facets.has_value());
    REQUIRE(facets->fullyResolved());
    REQUIRE_FALSE(facets->facets.empty());

    std::set<meshing::NodeId::ValueType> nodes;
    for (const meshing::ElementId facet : facets->facets) {
        const meshing::Triangle* triangle = model.mesh().mesh().findTriangle(facet);
        REQUIRE(triangle != nullptr);
        for (const meshing::NodeId node : triangle->nodes) {
            nodes.insert(node.value());
        }
    }
    return nodes;
}

[[nodiscard]] std::size_t facetCountOf(const StructuralModel& model, const FaceName& face) {
    Result<meshing::BoundaryFacetSet> facets = meshing::boundaryFacetsOf(model.map(), face);
    REQUIRE(facets.has_value());
    return facets->facets.size();
}

[[nodiscard]] RestraintComponents constrainedAt(const MeshDofMap& numbering,
                                                const ConstraintSet& set, meshing::NodeId node) {
    RestraintComponents held;
    for (const DofComponent component : structural::kDofComponents) {
        const Result<DofIndex> index =
            numbering.indexOf(NodalDof{.node = node, .component = component});
        REQUIRE(index.has_value());
        if (set.contains(*index)) {
            held.add(component);
        }
    }
    return held;
}

/// Brief section 134's both-ways semantic check, on a reference model.
void checkExactlyConstrained(const StructuralModel& model, const MeshDofMap& numbering,
                             const PreparedRestraints& prepared, const FaceName& face,
                             const RestraintComponents& wanted) {
    const std::set<meshing::NodeId::ValueType> target = expectedNodes(model, face);
    REQUIRE_FALSE(target.empty());
    for (const meshing::Node& node : model.mesh().mesh().nodes()) {
        const RestraintComponents held = constrainedAt(numbering, prepared.constraints(), node.id);
        const bool isTarget = target.contains(node.id.value());
        INFO("node " << node.id.value() << (isTarget ? " on target" : " off target") << ": held "
                     << structural::toString(held));
        CHECK(held == (isTarget ? wanted : RestraintComponents{}));
    }
    CHECK(prepared.constraints().size() == target.size() * wanted.count());
}

/// One row of the mapping table brief section 145 asks for.
struct Row {
    std::string model;
    std::string target;
    std::size_t facets = 0;
    std::size_t nodes = 0;
    std::string components;
    std::size_t expected = 0;
    std::size_t actual = 0;
};

[[nodiscard]] Row measure(MeshedReference& reference, const std::string& name,
                          const std::string& targetName, const FaceName& face,
                          const RestraintComponents& components) {
    const StructuralModel model = modelOf(reference);
    const MeshDofMap numbering = numberingOf(model);
    const std::vector<StructuralRestraint> restraints{
        StructuralRestraint{RestraintId::fromValue(1), face, components}};
    Result<PreparedRestraints> prepared =
        structural::prepareStructuralRestraints(model, numbering, restraints);
    INFO((prepared.has_value() ? std::string{} : prepared.error().message));
    REQUIRE(prepared.has_value());

    const std::set<meshing::NodeId::ValueType> nodes = expectedNodes(model, face);
    Row row;
    row.model = name;
    row.target = targetName;
    row.facets = facetCountOf(model, face);
    row.nodes = nodes.size();
    row.components = structural::toString(components);
    row.expected = nodes.size() * components.count();
    row.actual = prepared->constraints().size();
    checkExactlyConstrained(model, numbering, *prepared, face, components);
    return row;
}

void report(const Row& row) {
    WARN(row.model << " | " << row.target << " | resolved | " << row.facets << " facets | "
                   << row.nodes << " nodes | " << row.components << " | " << row.expected
                   << " expected | " << row.actual << " actual | "
                   << (row.expected == row.actual ? "PASS" : "FAIL"));
}

} // namespace

TEST_CASE("StructuralBC_ResolvesEveryComponentCombinationOnAReferenceBlockFace",
          "[structural][bc][reference]") {
    // RM-MESH-01, brief section 95. A 120 x 70 x 35 mm block whose six faces
    // are all nameable; its end cap is the stable planar target. The exact
    // integer arithmetic of the milestone, on the same face, for all five
    // combinations the brief names.
    auto built = reference::buildMeshBlockReferenceModel();
    REQUIRE(built.has_value());
    const FaceName top = built->top();
    MeshedReference reference(std::move(built->document));
    assignSteel(reference.document());
    reference.require();

    const RestraintComponents ux = RestraintComponents::along(DofComponent::Ux);
    const RestraintComponents uy = RestraintComponents::along(DofComponent::Uy);
    const RestraintComponents uz = RestraintComponents::along(DofComponent::Uz);

    const Row x = measure(reference, "RM-MESH-01", "end cap", top, ux);
    const Row y = measure(reference, "RM-MESH-01", "end cap", top, uy);
    const Row z = measure(reference, "RM-MESH-01", "end cap", top, uz);
    const Row xy = measure(reference, "RM-MESH-01", "end cap", top, ux.unionWith(uy));
    const Row fixed = measure(reference, "RM-MESH-01", "end cap", top,
                              RestraintComponents::fixed());
    for (const Row& row : {x, y, z, xy, fixed}) {
        report(row);
        CHECK(row.actual == row.expected);
    }

    SECTION("one component gives N, two give 2 N and three give 3 N on the same face") {
        REQUIRE(x.nodes > 0);
        CHECK(x.actual == x.nodes);
        CHECK(y.actual == x.nodes);
        CHECK(z.actual == x.nodes);
        CHECK(xy.actual == 2 * x.nodes);
        CHECK(fixed.actual == 3 * x.nodes);
    }
}

TEST_CASE("StructuralBC_UnionsTwoOpposingReferenceFacesInOneComponent",
          "[structural][bc][reference]") {
    // RM-MESH-01, brief section 96. Not a good physical model, and not meant
    // to be: it is a mapping test. The two caps share no node, so the union is
    // the exact sum -- and that is asserted rather than assumed.
    auto built = reference::buildMeshBlockReferenceModel();
    REQUIRE(built.has_value());
    const FaceName bottom = built->bottom();
    const FaceName top = built->top();
    MeshedReference reference(std::move(built->document));
    assignSteel(reference.document());
    reference.require();

    const StructuralModel model = modelOf(reference);
    const MeshDofMap numbering = numberingOf(model);
    const std::set<meshing::NodeId::ValueType> low = expectedNodes(model, bottom);
    const std::set<meshing::NodeId::ValueType> high = expectedNodes(model, top);
    std::vector<meshing::NodeId::ValueType> shared;
    std::ranges::set_intersection(low, high, std::back_inserter(shared));
    REQUIRE(shared.empty());

    const RestraintComponents ux = RestraintComponents::along(DofComponent::Ux);
    const std::vector<StructuralRestraint> restraints{
        StructuralRestraint{RestraintId::fromValue(1), bottom, ux},
        StructuralRestraint{RestraintId::fromValue(2), top, ux}};
    Result<PreparedRestraints> prepared =
        structural::prepareStructuralRestraints(model, numbering, restraints);
    INFO((prepared.has_value() ? std::string{} : prepared.error().message));
    REQUIRE(prepared.has_value());

    CHECK(prepared->nodes().size() == low.size() + high.size());
    CHECK(prepared->constraints().size() == low.size() + high.size());
    CHECK(prepared->resolutions().size() == 2);

    // Every node of either cap holds ux and nothing else; every other node
    // holds nothing.
    std::set<meshing::NodeId::ValueType> both = low;
    both.insert(high.begin(), high.end());
    for (const meshing::Node& node : model.mesh().mesh().nodes()) {
        const RestraintComponents held = constrainedAt(numbering, prepared->constraints(), node.id);
        INFO("node " << node.id.value());
        CHECK(held == (both.contains(node.id.value()) ? ux : RestraintComponents{}));
    }
    WARN("RM-MESH-01 | both caps | ux | " << low.size() << " + " << high.size() << " nodes | "
                                          << prepared->constraints().size() << " DOFs");
}

TEST_CASE("StructuralBC_ConstrainsEveryNodeOfACurvedReferenceWall",
          "[structural][bc][reference]") {
    // RM-MESH-04, brief sections 97 and 131. A tube whose inner and outer
    // walls are swept by DIFFERENT circles, so they carry different names.
    //
    // THIS IS WHERE THE CORNERS CLAIM IS MADE. The inner wall is bounded by
    // two circular edges and carries many more mapped nodes than any vertex
    // count, so a production path that constrained only the CAD boundary
    // vertices would be visible. The premise is asserted before the property.
    //
    // AND NO NORMAL OR TANGENT REINTERPRETATION. The wall's normal turns
    // through 360 degrees around the axis; a ux restraint holds global X at
    // zero on every one of its nodes, which is the convention this milestone
    // froze.
    auto built = reference::buildMeshTubeReferenceModel();
    REQUIRE(built.has_value());
    const FaceName inner = built->innerWall();
    const FaceName outer = built->outerWall();
    MeshedReference reference(std::move(built->document));
    assignSteel(reference.document());
    reference.require();

    const StructuralModel model = modelOf(reference);
    const MeshDofMap numbering = numberingOf(model);
    const std::set<meshing::NodeId::ValueType> innerNodes = expectedNodes(model, inner);
    const std::set<meshing::NodeId::ValueType> outerNodes = expectedNodes(model, outer);

    SECTION("the curved wall carries far more nodes than its bounding vertices") {
        INFO("the inner wall has " << facetCountOf(model, inner) << " facets and "
                                   << innerNodes.size() << " unique nodes");
        REQUIRE(innerNodes.size() > 8);
        CHECK(facetCountOf(model, inner) > innerNodes.size() / 2);
    }

    SECTION("a ux restraint holds global X on every one of them") {
        const RestraintComponents ux = RestraintComponents::along(DofComponent::Ux);
        const std::vector<StructuralRestraint> restraints{
            StructuralRestraint{RestraintId::fromValue(1), inner, ux}};
        Result<PreparedRestraints> prepared =
            structural::prepareStructuralRestraints(model, numbering, restraints);
        INFO((prepared.has_value() ? std::string{} : prepared.error().message));
        REQUIRE(prepared.has_value());
        CHECK(prepared->constraints().size() == innerNodes.size());
        checkExactlyConstrained(model, numbering, *prepared, inner, ux);
        WARN("RM-MESH-04 | inner wall | " << facetCountOf(model, inner) << " facets | "
                                          << innerNodes.size() << " nodes | ux | "
                                          << prepared->constraints().size() << " DOFs");
    }

    SECTION("and the inner and outer walls are not confused for one another") {
        // The two walls are different faces, so restraining one must not reach
        // the other. On a tube they share the nodes of the two annular caps'
        // boundaries -- which is lawful -- so the claim is that each wall has
        // nodes the other does not.
        std::vector<meshing::NodeId::ValueType> onlyInner;
        std::ranges::set_difference(innerNodes, outerNodes, std::back_inserter(onlyInner));
        REQUIRE_FALSE(onlyInner.empty());

        const RestraintComponents uz = RestraintComponents::along(DofComponent::Uz);
        Result<PreparedRestraints> prepared = structural::prepareStructuralRestraints(
            model, numbering,
            std::vector{StructuralRestraint{RestraintId::fromValue(1), inner, uz}});
        REQUIRE(prepared.has_value());
        for (const meshing::NodeId::ValueType node : onlyInner) {
            const Result<DofIndex> index = numbering.indexOf(
                NodalDof{.node = meshing::NodeId::fromValue(node), .component = DofComponent::Uz});
            REQUIRE(index.has_value());
            CHECK(prepared->constraints().contains(*index));
        }
        std::vector<meshing::NodeId::ValueType> onlyOuter;
        std::ranges::set_difference(outerNodes, innerNodes, std::back_inserter(onlyOuter));
        REQUIRE_FALSE(onlyOuter.empty());
        for (const meshing::NodeId::ValueType node : onlyOuter) {
            const Result<DofIndex> index = numbering.indexOf(
                NodalDof{.node = meshing::NodeId::fromValue(node), .component = DofComponent::Uz});
            REQUIRE(index.has_value());
            CHECK_FALSE(prepared->constraints().contains(*index));
        }
    }
}

TEST_CASE("StructuralBC_KeepsGlobalComponentsUnderARigidTransform",
          "[structural][bc][reference]") {
    // RM-MESH-06, brief sections 49, 50, 82 and 98. The same canonical target
    // on the base model and on the rigidly transformed one.
    //
    // THE CONVENTION BEING FROZEN: `Ux` is global X, whatever the face's normal
    // is doing. The transform rotates the datum face's normal; the restrained
    // component does not rotate with it, and the constrained DOF COUNT is
    // therefore the same on both models even though the geometry is not.
    auto base = reference::buildMeshTransformedBaseReferenceModel();
    REQUIRE(base.has_value());
    auto placed = reference::buildMeshTransformedPlacedReferenceModel();
    REQUIRE(placed.has_value());
    const FaceName baseFace = base->datumFace();
    const FaceName placedFace = placed->datumFace();

    MeshedReference baseReference(std::move(base->document));
    assignSteel(baseReference.document());
    baseReference.require();
    MeshedReference placedReference(std::move(placed->document));
    assignSteel(placedReference.document());
    placedReference.require();

    const RestraintComponents ux = RestraintComponents::along(DofComponent::Ux);
    const Row here = measure(baseReference, "RM-MESH-06 base", "datum face", baseFace, ux);
    const Row there = measure(placedReference, "RM-MESH-06 placed", "datum face", placedFace, ux);
    report(here);
    report(there);

    SECTION("the canonical target resolves on both models") {
        CHECK(here.nodes > 0);
        CHECK(there.nodes > 0);
        CHECK(here.actual == here.expected);
        CHECK(there.actual == there.expected);
    }

    SECTION("and ux is still ux, so the same target gives the same count") {
        // The two models are the same block, one of them moved, so the datum
        // face carries the same number of mapped nodes -- and a ux restraint
        // constrains all of them on both. A face-normal interpretation would
        // have given a different component set on the rotated body.
        CHECK(there.nodes == here.nodes);
        CHECK(there.actual == here.actual);
        CHECK(here.components == "ux");
        CHECK(there.components == "ux");
    }

    SECTION("the restrained nodes really did move, so the geometry was transformed") {
        // The premise of the claim above: if the placed model were in the same
        // place, nothing would have been tested. Compared through the mesh's
        // own coordinates.
        const StructuralModel first = modelOf(baseReference);
        const StructuralModel second = modelOf(placedReference);
        double largest = 0.0;
        for (const meshing::NodeId::ValueType handle : expectedNodes(first, baseFace)) {
            const meshing::Node* a =
                first.mesh().mesh().findNode(meshing::NodeId::fromValue(handle));
            const meshing::Node* b =
                second.mesh().mesh().findNode(meshing::NodeId::fromValue(handle));
            if (a == nullptr || b == nullptr) {
                continue;
            }
            largest = std::max(largest, std::abs(a->position.x.si() - b->position.x.si()) +
                                            std::abs(a->position.y.si() - b->position.y.si()) +
                                            std::abs(a->position.z.si() - b->position.z.si()));
        }
        INFO("largest coordinate difference " << largest << " m");
        CHECK(largest > 1e-4);
    }
}

TEST_CASE("StructuralBC_ReResolvesALocallyRefinedReferenceFace", "[structural][bc][reference]") {
    // RM-MESH-07, brief sections 83, 99, 132 and 133. Local refinement on one
    // face, with a second face proving the rest of the body was left alone.
    //
    // THE CLAIM IS NOT THAT THE COUNT IS THE SAME. It is that the canonical
    // restraint is unchanged, that the mapped node count MAY change, and that
    // the constrained DOF count follows it exactly -- three times the node
    // count for a fixed support, at every level. Comparing raw counts across
    // meshes would be the wrong invariant, and section 133 says so.
    auto built = reference::buildMeshLocalRefinementReferenceModel();
    REQUIRE(built.has_value());
    const FaceName refined = built->refined();
    const FaceName coarse = built->coarse();
    MeshedReference reference(std::move(built->document));
    assignSteel(reference.document());

    const std::vector<StructuralRestraint> restraints{
        StructuralRestraint::fixedSupport(RestraintId::fromValue(1), refined)};

    struct Level {
        double target = 0.0;
        std::size_t facets = 0;
        std::size_t nodes = 0;
        std::size_t constrained = 0;
        std::size_t coarseNodes = 0;
        std::size_t meshNodes = 0;
        meshing::MeshStamp mesh{};
    };
    std::vector<Level> levels;

    for (const double targetMm : {12.0, 6.0, 3.0}) {
        meshing::MeshControlDefinition definition = reference.definition()->definition();
        REQUIRE(definition.mesh.sizing.local.size() == 1);
        definition.mesh.sizing.local.front().targetSize = Length::fromSi(targetMm * 1e-3);
        REQUIRE(reference.document()
                    .modifyObject<meshing::MeshControl>(
                        reference.controlObject(),
                        [&definition](meshing::MeshControl& control) {
                            return control.setDefinition(definition);
                        })
                    .has_value());
        reference.require();

        const StructuralModel model = modelOf(reference);
        const MeshDofMap numbering = numberingOf(model);
        Result<PreparedRestraints> prepared =
            structural::prepareStructuralRestraints(model, numbering, restraints);
        INFO((prepared.has_value() ? std::string{} : prepared.error().message));
        REQUIRE(prepared.has_value());

        // THE SEMANTIC INVARIANT, AT EVERY LEVEL. Not a count comparison: every
        // current node mapped to the restrained face carries all three
        // components, and nothing else carries any.
        checkExactlyConstrained(model, numbering, *prepared, refined,
                                RestraintComponents::fixed());

        levels.push_back(Level{.target = targetMm,
                               .facets = facetCountOf(model, refined),
                               .nodes = expectedNodes(model, refined).size(),
                               .constrained = prepared->constraints().size(),
                               .coarseNodes = expectedNodes(model, coarse).size(),
                               .meshNodes = model.mesh().mesh().nodes().size(),
                               .mesh = model.mesh().mesh().stamp()});
    }
    REQUIRE(levels.size() == 3);

    for (const Level& level : levels) {
        WARN("RM-MESH-07 | refined face at " << level.target << " mm | " << level.facets
                                             << " facets | " << level.nodes << " nodes | fixed | "
                                             << level.constrained << " DOFs | mesh "
                                             << level.meshNodes << " nodes");
    }

    SECTION("the sizing really did change the mesh") {
        // THE PREMISE, FIRST. A MeshStamp differs on every regeneration
        // whatever the sizing says, so comparing stamps would prove nothing;
        // the body's own node count is what shows the control was honoured.
        for (std::size_t i = 1; i < levels.size(); ++i) {
            INFO("target " << levels[i - 1].target << " mm -> " << levels[i].target << " mm");
            CHECK(levels[i].mesh != levels[i - 1].mesh);
            CHECK(levels[i].meshNodes > levels[i - 1].meshNodes);
        }
    }

    SECTION("every level gave 3 N for its own N") {
        for (const Level& level : levels) {
            INFO("target " << level.target << " mm");
            CHECK(level.constrained == 3 * level.nodes);
        }
    }

    SECTION("the canonical restraint never changed") {
        const std::vector<StructuralRestraint> again{
            StructuralRestraint::fixedSupport(RestraintId::fromValue(1), refined)};
        CHECK(again == restraints);
    }

    SECTION("and the PLANAR face's own node set did not move, which is a P16 property") {
        // RECORDED RATHER THAN ASSERTED AWAY. The body gained nodes at every
        // level and the restrained face kept exactly its four, because a plane
        // is exactly representable and nothing in the sizing retriangulates
        // it. So this model cannot show a constrained count FOLLOWING a
        // refinement, and the claim brief section 99 asks for is made on
        // RM-MESH-02's curved wall instead, where the face itself refines.
        for (std::size_t i = 1; i < levels.size(); ++i) {
            CHECK(levels[i].nodes == levels[i - 1].nodes);
            CHECK(levels[i].facets == levels[i - 1].facets);
            CHECK(levels[i].coarseNodes == levels[i - 1].coarseNodes);
        }
    }
}

TEST_CASE("StructuralBC_FollowsTheNodeCountOfARefinedCurvedFace",
          "[structural][bc][reference]") {
    // RM-MESH-02, brief sections 83, 99 and 133. The claim RM-MESH-07 cannot
    // make: the mapped node count of a CURVED face really does change with the
    // discretisation, and the constrained DOF count follows it exactly.
    //
    // THE LEVELS ARE ASSERTED TO DIFFER BEFORE ANYTHING ELSE IS COMPARED,
    // which is the lesson of P17-LOAD-001's convergence test -- its first two
    // drafts both passed while measuring the same mesh three times.
    //
    // AND NO COUNT IS COMPARED ACROSS LEVELS AS AN INVARIANT. More nodes is
    // the CORRECT outcome of a finer mesh; what has to hold at every level is
    // the semantic check of brief section 132, which is run each time.
    auto built = reference::buildMeshCylinderReferenceModel();
    REQUIRE(built.has_value());
    const FaceName wall = built->wall();
    MeshedReference reference(std::move(built->document));
    assignSteel(reference.document());

    const RestraintComponents uz = RestraintComponents::along(DofComponent::Uz);
    const std::vector<StructuralRestraint> restraints{
        StructuralRestraint{RestraintId::fromValue(1), wall, uz}};

    struct Level {
        double deflection = 0.0;
        std::size_t facets = 0;
        std::size_t nodes = 0;
        std::size_t constrained = 0;
    };
    std::vector<Level> levels;

    for (const double deflectionMm : {0.40, 0.10, 0.025}) {
        meshing::MeshControlDefinition definition = reference.definition()->definition();
        definition.mesh.surface.linearDeflection = Length::fromSi(deflectionMm * 1e-3);
        REQUIRE(reference.document()
                    .modifyObject<meshing::MeshControl>(
                        reference.controlObject(),
                        [&definition](meshing::MeshControl& control) {
                            return control.setDefinition(definition);
                        })
                    .has_value());
        reference.require();

        const StructuralModel model = modelOf(reference);
        const MeshDofMap numbering = numberingOf(model);
        Result<PreparedRestraints> prepared =
            structural::prepareStructuralRestraints(model, numbering, restraints);
        INFO((prepared.has_value() ? std::string{} : prepared.error().message));
        REQUIRE(prepared.has_value());
        checkExactlyConstrained(model, numbering, *prepared, wall, uz);
        levels.push_back(Level{.deflection = deflectionMm,
                               .facets = facetCountOf(model, wall),
                               .nodes = expectedNodes(model, wall).size(),
                               .constrained = prepared->constraints().size()});
    }
    REQUIRE(levels.size() == 3);

    for (const Level& level : levels) {
        WARN("RM-MESH-02 | lateral wall at " << level.deflection << " mm deflection | "
                                             << level.facets << " facets | " << level.nodes
                                             << " nodes | uz | " << level.constrained << " DOFs");
    }

    SECTION("the levels really are different discretisations of the same face") {
        for (std::size_t i = 1; i < levels.size(); ++i) {
            INFO("level " << i - 1 << " had " << levels[i - 1].facets << " facets, level " << i
                          << " has " << levels[i].facets);
            CHECK(levels[i].facets > levels[i - 1].facets);
            CHECK(levels[i].nodes > levels[i - 1].nodes);
        }
    }

    SECTION("and the constrained count is N at every one of them, not a fixed number") {
        for (const Level& level : levels) {
            INFO("deflection " << level.deflection << " mm");
            CHECK(level.constrained == level.nodes);
        }
        CHECK(levels.back().constrained > levels.front().constrained);
    }
}

TEST_CASE("StructuralBC_RefusesTheDrilledHoleWallWithNoGeometricFallback",
          "[structural][bc][reference]") {
    // THE KNOWN P16 LIMITATION, brief sections 14, 47 and 149, recorded as a
    // refusal rather than worked around. `cutHole` names a hole's flat faces
    // and NOT its cylindrical wall, so there is no canonical reference for
    // that wall.
    //
    // RM-MESH-03 puts its hole in the profile sketch, which makes the bore a
    // named Side face -- so the unsupported case is a selector aimed at a face
    // the naming chain never attributed, which is what a drilled hole's wall
    // amounts to. Both halves are here: the unnamed wall is refused, and the
    // NAMED bore of the same geometric kind succeeds, which is what makes this
    // a P16 limitation rather than a P17 defect.
    auto built = reference::buildMeshPlateWithHoleReferenceModel();
    REQUIRE(built.has_value());
    const FaceName unnamed{built->solid, FaceSelector{.role = FaceRole::Side,
                                                      .entity = EntityId::fromValue(99999)}};
    const FaceName supported = built->holeWall();
    MeshedReference reference(std::move(built->document));
    assignSteel(reference.document());
    reference.require();

    const StructuralModel model = modelOf(reference);
    const MeshDofMap numbering = numberingOf(model);

    SECTION("a wall with no canonical name is refused, and nothing nearby is rebound") {
        const std::vector<StructuralRestraint> restraints{
            StructuralRestraint::fixedSupport(RestraintId::fromValue(1), unnamed)};
        const std::optional<RestraintProblem> problem =
            structural::structuralRestraintProblem(model, numbering, restraints);
        INFO("problem " << (problem.has_value() ? structural::toString(*problem) : "none"));
        REQUIRE(problem.has_value());
        CHECK(*problem == RestraintProblem::TargetUnresolved);
        Result<PreparedRestraints> prepared =
            structural::prepareStructuralRestraints(model, numbering, restraints);
        REQUIRE_FALSE(prepared.has_value());
        CHECK(prepared.error().code == ErrorCode::NotFound);
        WARN("unsupported target: stable canonical FaceName NO, geometric fallback NO, "
             "preparation explicit failure, PASS");
    }

    SECTION("the named bore of the same geometric kind is restrained normally") {
        const std::vector<StructuralRestraint> restraints{
            StructuralRestraint::fixedSupport(RestraintId::fromValue(1), supported)};
        CHECK_FALSE(
            structural::structuralRestraintProblem(model, numbering, restraints).has_value());
        Result<PreparedRestraints> prepared =
            structural::prepareStructuralRestraints(model, numbering, restraints);
        REQUIRE(prepared.has_value());
        REQUIRE(prepared->resolutions().size() == 1);
        CHECK(prepared->resolutions()[0].facets > 0);
        CHECK(prepared->resolutions()[0].nodes > 0);
        checkExactlyConstrained(model, numbering, *prepared, supported,
                                RestraintComponents::fixed());
    }
}

TEST_CASE("StructuralBC_PartitionsAReferenceModelsEquationsWithP17Dof",
          "[structural][bc][reference]") {
    // Brief sections 94 and 129, on the largest reference model this milestone
    // touches. No solver: the claim is the partition, recorded as the counts
    // the brief asks for.
    auto built = reference::buildMeshTubeReferenceModel();
    REQUIRE(built.has_value());
    const FaceName bottom = built->bottomAnnulus();
    MeshedReference reference(std::move(built->document));
    assignSteel(reference.document());
    reference.require();

    const StructuralModel model = modelOf(reference);
    const MeshDofMap numbering = numberingOf(model);
    Result<PreparedRestraints> prepared = structural::prepareStructuralRestraints(
        model, numbering,
        std::vector{StructuralRestraint::fixedSupport(RestraintId::fromValue(1), bottom)});
    INFO((prepared.has_value() ? std::string{} : prepared.error().message));
    REQUIRE(prepared.has_value());

    Result<FreeEquationMap> free =
        structural::buildFreeEquationMap(numbering, prepared->constraints());
    INFO((free.has_value() ? std::string{} : free.error().message));
    REQUIRE(free.has_value());

    const DofIndex::ValueType total = numbering.dofCount();
    WARN("RM-MESH-04 | bottom annulus fixed | N_total " << total << " | N_constrained "
                                                        << free->constrainedCount()
                                                        << " | N_free " << free->freeCount());
    CHECK(free->dofCount() == total);
    CHECK(free->freeCount() + free->constrainedCount() == total);
    CHECK(free->constrainedCount() == prepared->constraints().size());
    CHECK(free->freeCount() > 0);
}
