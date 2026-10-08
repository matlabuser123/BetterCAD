// P17-BC-001: canonical restraints and the constrained degree-of-freedom set.
//
// WHAT THESE TESTS ARE FOR. Restraints have no force to conserve, so there is
// no analytical integral to check and the arithmetic is integer: a face with
// `N` unique mapped nodes restrained in `k` components gives exactly `k N`
// constrained degrees of freedom. The real claims are about AUTHORITY and
// about COMPLETENESS, and they need different evidence:
//
//   AUTHORITY      a canonical restraint holds no facet, no node and no index;
//                  an unresolved target is refused and never rebound to a
//                  nearby face; a prepared set from one mesh cannot be read
//                  against another. Checked by building the dangerous thing
//                  and watching it be refused.
//
//   COMPLETENESS   EVERY node of every mapped facet is constrained in exactly
//                  the requested components, and nothing else is. Checked
//                  against a node set the TEST builds from P16's facet
//                  mapping, facet by facet, with its own deduplication -- not
//                  from `boundaryNodesOf`, which is the production path.
//
// THE COMPLETENESS CHECK IS BOTH-WAYS ON PURPOSE (brief section 134). A test
// that only asked "is every target node constrained?" would pass a production
// path that constrained the whole mesh, and a test that only asked "is every
// constrained node on the target?" would pass one that constrained a single
// corner. Both directions, every time.
//
// COUNTS ARE NEVER COMPARED ACROSS MESHES (brief section 133). A finer mesh
// has more nodes on the same face and SHOULD have more constrained degrees of
// freedom; the invariant that survives a remesh is the semantic one above, so
// that is what the remesh tests assert.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/meshing/MeshSizing.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/structural/StructuralAnalysisObject.hpp>
#include <bettercad/structural/StructuralConstraints.hpp>
#include <bettercad/structural/StructuralResult.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using structural::ConstraintSet;
using structural::DofComponent;
using structural::DofIndex;
using structural::FreeEquationMap;
using structural::MeshDofMap;
using structural::NodalDof;
using structural::PreparedRestraints;
using structural::RestraintComponents;
using structural::RestraintProblem;
using structural::RestraintResolution;
using structural::StructuralAnalysis;
using structural::StructuralAnalysisDefinition;
using structural::StructuralModel;
using structural::StructuralResult;
using structural::StructuralResultSource;
using structural::StructuralRestraint;
using structural::Strain6;
using structural::Stress6;

namespace {

/// A document with a block, a meshing control and an analysis: everything a
/// restraint needs to be prepared against.
///
/// 40 x 30 mm in plan, extruded 20 mm along +Z. Three different edge lengths,
/// so a swapped axis cannot pass, and the start cap at z = 0 and end cap at
/// z = 20 mm are opposite faces that share no node -- which is what the
/// off-target test needs.
///
/// THE MATERIAL IS ASSIGNED, AND NOT BECAUSE A RESTRAINT NEEDS ONE. The audit
/// found that `requireStructuralModel` refuses a body with no material -- the
/// input boundary ADR-036 put there asks for it on behalf of every structural
/// milestone, so a restraint inherits the requirement without having one of
/// its own. `StructuralBC_IsIndependentOfTheMaterial` is where that
/// distinction is made evidence rather than a comment (brief section 120).
struct RestrainedPart {
    Document document{"Part"};
    features::Regenerator regenerator;
    meshing::Mesher mesher;
    ObjectId feature{};
    MeshControlId control{};
    AnalysisId analysis{};
    MaterialId material{};
    std::array<EntityId, 4> lines{};

    explicit RestrainedPart(bool withMaterial = true) {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        lines = addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
        const ObjectId profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = 20_mm});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));

        // A LOCAL REFINEMENT, FOR THE VOLUME AND NOT FOR THE FACE. What it
        // buys and what it cannot buy are both worth stating, because the
        // first draft of this fixture assumed the second.
        //
        // IT CANNOT MAKE A PLANAR FACE CARRY MORE NODES. A box face is exactly
        // representable, so nothing in the sizing controls subdivides its two
        // boundary triangles: this block's every planar face carries exactly
        // four mapped nodes -- its four CAD corners -- at any global target,
        // which is also why P16's own evidence records RM-MESH-01 meshing to
        // the same eight nodes at 30, 20 and 10 mm. The "all the mapped nodes,
        // not just the corners" claim therefore cannot be made here at all,
        // and is made on a CURVED reference face instead
        // (`StructuralBCReferenceTests.cpp`). A test that tried it here would
        // have passed a corners-only implementation.
        //
        // WHAT IT DOES BUY is interior nodes: 20 mm globally with 6 mm on side
        // 0 -- RM-MESH-07's own ratio -- takes the block from 9 nodes to 19, so
        // most of the mesh is OFF any target face and the off-target half of
        // `checkExactlyConstrained` has something to find.
        auto intent = meshing::MeshControl::create(
            "Mesh",
            meshing::MeshControlDefinition{
                .body = feature,
                .mesh = {.sizing = {.globalTargetSize = 20_mm,
                                    .local = {meshing::LocalMeshSizing{
                                        .face = FaceName{feature,
                                                         FaceSelector{.role = FaceRole::Side,
                                                                      .entity = lines.at(0)}},
                                        .targetSize = 6_mm}}}}});
        REQUIRE(intent.has_value());
        control = MeshControlId::fromValue(require(document.addObject(std::move(*intent))).value());

        if (withMaterial) {
            features::MaterialDefinition definition;
            definition.designation = "Steel";
            definition.mechanical.youngsModulus =
                materials::MaterialProperty<ElasticModulus>::known(210_GPa);
            definition.mechanical.poissonRatio =
                materials::MaterialProperty<bettercad::PoissonRatio>::known(PoissonRatio::of(0.3));
            const Result<MaterialId> id = features::createMaterial(document, "Steel", definition);
            REQUIRE(id.has_value());
            material = *id;
            REQUIRE(features::assignMaterial(document, material).has_value());
        }

        auto study =
            StructuralAnalysis::create("Study", StructuralAnalysisDefinition{.mesh = control});
        REQUIRE(study.has_value());
        analysis = AnalysisId::fromValue(require(document.addObject(std::move(*study))).value());

        requireReport(regenerator, document);
        mesh();
    }

    [[nodiscard]] FaceName startCap() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::StartCap}};
    }
    [[nodiscard]] FaceName endCap() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::EndCap}};
    }
    [[nodiscard]] FaceName side(std::size_t which) const {
        return FaceName{feature, FaceSelector{.role = FaceRole::Side, .entity = lines.at(which)}};
    }

    /// Generates, or regenerates, the mesh. Each call is a new generation.
    const meshing::VolumeMesh& mesh() {
        const Result<const meshing::VolumeMesh*> generated =
            mesher.generate(document, regenerator, control);
        INFO((generated.has_value() ? std::string{} : generated.error().message));
        REQUIRE(generated.has_value());
        return **generated;
    }

    [[nodiscard]] const meshing::VolumeMesh& volume() const {
        const meshing::VolumeMesh* held = mesher.mesh(control);
        REQUIRE(held != nullptr);
        return *held;
    }

    [[nodiscard]] StructuralModel model() const {
        Result<StructuralModel> prepared =
            structural::requireStructuralModel(document, regenerator, mesher, control);
        INFO((prepared.has_value() ? std::string{} : prepared.error().message));
        REQUIRE(prepared.has_value());
        return std::move(*prepared);
    }

    [[nodiscard]] MeshDofMap numbering() const {
        Result<MeshDofMap> map = structural::buildMeshDofMap(volume().mesh());
        INFO((map.has_value() ? std::string{} : map.error().message));
        REQUIRE(map.has_value());
        return std::move(*map);
    }

    [[nodiscard]] StructuralResultSource currentSource() const {
        const Result<StructuralResultSource> source =
            structural::currentResultSource(document, regenerator, mesher, analysis);
        INFO((source.has_value() ? std::string{} : source.error().message));
        REQUIRE(source.has_value());
        return *source;
    }

    /// A result of the right shape for the current mesh. The numbers exercise
    /// the container and are not a solution.
    [[nodiscard]] StructuralResult resultFor(const StructuralResultSource& source) const {
        const meshing::VolumeMesh& held = volume();
        std::vector<Translation3D> displacements(held.nodeCount(), Translation3D{});
        std::vector<Strain6> strains(held.tetrahedronCount(), Strain6{});
        std::vector<Stress6> stresses(held.tetrahedronCount(), Stress6{});
        Result<StructuralResult> result = StructuralResult::create(
            source, held, std::move(displacements), {}, std::move(strains), std::move(stresses));
        INFO((result.has_value() ? std::string{} : result.error().message));
        REQUIRE(result.has_value());
        return std::move(*result);
    }

    /// Replaces the analysis's restraints, which is the edit a user makes.
    ///
    /// RETURNS WHETHER ANYTHING CHANGED, and the first draft of this helper
    /// did not -- it returned `setDefinition(...).has_value()`, which is true
    /// whenever the call SUCCEEDED, including when the definition compared
    /// equal and nothing moved. `Document::modifyObject` bumps the revision
    /// only when the mutation reports a change, so that draft bumped it on
    /// every call and the currentness tests below would have passed even if
    /// the restraints were not in the definition at all. The M14 mutation
    /// found it.
    bool setRestraints(std::vector<StructuralRestraint> restraints) {
        const Result<bool> changed = document.modifyObject<StructuralAnalysis>(
            ObjectId::fromValue(analysis.value()),
            [&](StructuralAnalysis& study) -> Result<bool> {
                StructuralAnalysisDefinition definition = study.definition();
                definition.restraints = std::move(restraints);
                return study.setDefinition(std::move(definition));
            });
        INFO((changed.has_value() ? std::string{} : changed.error().message));
        REQUIRE(changed.has_value());
        return *changed;
    }
};

/// THE EXPECTED NODE SET, BUILT IN THE TEST FROM P16'S FACET MAPPING.
///
/// Deliberately not `meshing::boundaryNodesOf`, which is what production
/// calls: this walks the facets P16 attributes to the face, takes all three
/// corners of each, and deduplicates with a `std::set` of its own. So "every
/// node of every mapped facet, once" is checked against an independently
/// assembled answer rather than against the same function twice.
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

[[nodiscard]] PreparedRestraints prepare(const RestrainedPart& part,
                                         const std::vector<StructuralRestraint>& restraints) {
    const StructuralModel model = part.model();
    Result<PreparedRestraints> prepared =
        structural::prepareStructuralRestraints(model, part.numbering(), restraints);
    INFO((prepared.has_value() ? std::string{} : prepared.error().message));
    REQUIRE(prepared.has_value());
    return std::move(*prepared);
}

[[nodiscard]] Error refusal(const RestrainedPart& part,
                            const std::vector<StructuralRestraint>& restraints) {
    const StructuralModel model = part.model();
    Result<PreparedRestraints> prepared =
        structural::prepareStructuralRestraints(model, part.numbering(), restraints);
    REQUIRE_FALSE(prepared.has_value());
    return prepared.error();
}

/// Which of the three components of @p node are constrained, read through
/// P17-DOF's numbering -- never by computing an index here.
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

/// THE BOTH-WAYS SEMANTIC CHECK of brief section 134: every node of @p face
/// carries exactly @p wanted, and every node NOT on @p face carries nothing.
void checkExactlyConstrained(const StructuralModel& model, const MeshDofMap& numbering,
                             const PreparedRestraints& prepared, const FaceName& face,
                             const RestraintComponents& wanted) {
    const std::set<meshing::NodeId::ValueType> target = expectedNodes(model, face);
    REQUIRE_FALSE(target.empty());

    std::size_t onTarget = 0;
    std::size_t offTarget = 0;
    for (const meshing::Node& node : model.mesh().mesh().nodes()) {
        const RestraintComponents held = constrainedAt(numbering, prepared.constraints(), node.id);
        const bool isTarget = target.contains(node.id.value());
        INFO("node " << node.id.value() << (isTarget ? " on target" : " off target") << ": held "
                     << structural::toString(held));
        if (isTarget) {
            CHECK(held == wanted);
            ++onTarget;
        } else {
            CHECK(held.isEmpty());
            ++offTarget;
        }
    }
    CHECK(onTarget == target.size());
    CHECK(offTarget == model.mesh().mesh().nodes().size() - target.size());
    // AND THE EXACT INTEGER COUNT, which is the whole arithmetic of this
    // milestone: k components on N unique nodes is k N constrained DOFs.
    CHECK(prepared.constraints().size() == target.size() * wanted.count());
}

} // namespace

// ---------------------------------------------------------------------------
// The schema
// ---------------------------------------------------------------------------

TEST_CASE("StructuralBC_CanonicalRestraintsCarryNoMeshHandle", "[structural][bc]") {
    // THE CENTRAL RULE, in compile-time form: a restraint is a RestraintId, a
    // FaceName and a component mask, and there is nowhere in it for a boundary
    // facet, a NodeId or a DofIndex to hide. The mirror declares the same
    // three members in the same order, so an added field changes the size.
    struct PermittedRestraint {
        RestraintId id;
        FaceName face;
        RestraintComponents components;
    };
    static_assert(sizeof(StructuralRestraint) == sizeof(PermittedRestraint),
                  "a canonical restraint holds an identity, a CAD target and a component mask -- "
                  "nothing else. A node list, a facet list or a DOF index added to it breaks "
                  "this");

    // And it cannot be built from any of them, which is the property the size
    // equality is a proxy for.
    static_assert(!std::is_constructible_v<StructuralRestraint, meshing::NodeId>);
    static_assert(!std::is_constructible_v<StructuralRestraint, meshing::ElementId>);
    static_assert(!std::is_constructible_v<StructuralRestraint, DofIndex>);
    static_assert(!std::is_constructible_v<StructuralRestraint, meshing::MeshStamp>);
    static_assert(!std::is_constructible_v<StructuralRestraint, ConstraintSet>);

    // The mask is a whole byte and nothing more: it cannot be smuggling a
    // displacement magnitude (brief sections 59, 60).
    static_assert(sizeof(RestraintComponents) == sizeof(std::uint8_t));
    static_assert(!std::is_constructible_v<RestraintComponents, double>);
    static_assert(!std::is_constructible_v<RestraintComponents, Length>);

    WARN("a canonical restraint is " << sizeof(StructuralRestraint)
                                     << " bytes: an id, a FaceName and a one-byte mask");
}

TEST_CASE("StructuralBC_SchemaUsesRestraintIdAndACadTarget", "[structural][bc]") {
    const RestraintId id = RestraintId::fromValue(7);
    const FaceName face{ObjectId::fromValue(3), FaceSelector{.role = FaceRole::EndCap}};
    const StructuralRestraint restraint = StructuralRestraint::fixedSupport(id, face);

    CHECK(restraint.id() == id);
    CHECK(restraint.face() == face);
    CHECK(restraint.components().isFixed());
    CHECK(structural::validate(restraint).has_value());

    // The identity is the intent and nothing derived: two restraints with the
    // same id on different faces are different records, and the id is what
    // P17-DATA-001 reserved for exactly this.
    const StructuralRestraint elsewhere = StructuralRestraint::fixedSupport(
        id, FaceName{ObjectId::fromValue(3), FaceSelector{.role = FaceRole::StartCap}});
    CHECK(restraint != elsewhere);
}

TEST_CASE("StructuralBC_FixedEqualsTheThreeComponentsCombined", "[structural][bc]") {
    // The mask algebra, asserted at compile time beside the type and again
    // here through the public API so a reader sees it.
    CHECK(RestraintComponents::fixed().count() == 3);
    CHECK(RestraintComponents::fixed().isFixed());
    CHECK(RestraintComponents::along(DofComponent::Ux)
              .unionWith(RestraintComponents::along(DofComponent::Uy))
              .unionWith(RestraintComponents::along(DofComponent::Uz)) ==
          RestraintComponents::fixed());

    SECTION("adding a component twice is one component") {
        RestraintComponents mask = RestraintComponents::along(DofComponent::Uy);
        mask.add(DofComponent::Uy);
        CHECK(mask.count() == 1);
        CHECK(mask == RestraintComponents::along(DofComponent::Uy));
    }

    SECTION("an empty mask is representable and refused") {
        const RestraintComponents mask;
        CHECK(mask.isEmpty());
        CHECK(structural::toString(mask) == "none");
        const StructuralRestraint restraint{
            RestraintId::fromValue(1),
            FaceName{ObjectId::fromValue(1), FaceSelector{.role = FaceRole::EndCap}}, mask};
        const Result<void> checked = structural::validate(restraint);
        REQUIRE_FALSE(checked.has_value());
        CHECK_THAT(checked.error().message, ContainsSubstring("nothing for it to constrain"));
    }

    SECTION("the mask prints in component terms") {
        CHECK(structural::toString(RestraintComponents::along(DofComponent::Ux)) == "ux");
        CHECK(structural::toString(RestraintComponents::along(DofComponent::Ux)
                                       .unionWith(RestraintComponents::along(DofComponent::Uz))) ==
              "ux+uz");
        CHECK(structural::toString(RestraintComponents::fixed()) == "fixed");
    }
}

// ---------------------------------------------------------------------------
// Resolving a CAD target to the current node set
// ---------------------------------------------------------------------------

TEST_CASE("StructuralBC_FaceFacetsResolveToTheirUniqueNodes", "[structural][bc]") {
    RestrainedPart part;
    const StructuralModel model = part.model();
    const MeshDofMap numbering = part.numbering();
    const FaceName face = part.endCap();

    const std::set<meshing::NodeId::ValueType> expected = expectedNodes(model, face);
    const std::size_t facets = facetCountOf(model, face);

    const PreparedRestraints prepared = prepare(
        part, {StructuralRestraint::fixedSupport(RestraintId::fromValue(1), face)});

    INFO("the end cap has " << facets << " facets and " << expected.size() << " unique nodes");

    SECTION("every node of every mapped facet is in the resolved set, and only those") {
        std::set<meshing::NodeId::ValueType> resolved;
        for (const meshing::NodeId node : prepared.nodes()) {
            resolved.insert(node.value());
        }
        CHECK(resolved == expected);
    }

    SECTION("the facet nodes were deduplicated, so the set is smaller than 3 x facets") {
        // THE INSTRUMENT'S OWN PREMISE FIRST: the face must have more than one
        // facet, or 3 x facets could not exceed the node count and this would
        // prove nothing.
        REQUIRE(facets > 1);
        CHECK(prepared.nodes().size() < 3 * facets);
        CHECK(prepared.nodes().size() == expected.size());
    }

    SECTION("the resolved nodes are ascending and unique") {
        CHECK(std::ranges::is_sorted(prepared.nodes()));
        CHECK(std::ranges::adjacent_find(prepared.nodes()) == prepared.nodes().end());
    }

    SECTION("and the per-restraint record traces back to that resolution") {
        REQUIRE(prepared.resolutions().size() == 1);
        const RestraintResolution& resolution = prepared.resolutions().front();
        CHECK(resolution.restraint == RestraintId::fromValue(1));
        CHECK(resolution.facets == facets);
        CHECK(resolution.nodes == expected.size());
        CHECK(resolution.components == RestraintComponents::fixed());
        CHECK(resolution.degreesOfFreedom == 3 * expected.size());
    }
}

TEST_CASE("StructuralBC_ConstrainsEveryMappedNodeAndNothingElse", "[structural][bc]") {
    // Brief sections 130, 134 and 135 in one place: the exact integer count,
    // the both-ways membership check, and the off-target check on an
    // asymmetric block whose opposite face shares no node with the target.
    RestrainedPart part;
    const StructuralModel model = part.model();
    const MeshDofMap numbering = part.numbering();
    const FaceName face = part.endCap();
    const std::size_t faceNodes = expectedNodes(model, face).size();

    SECTION("a fixed support gives 3 N") {
        const PreparedRestraints prepared =
            prepare(part, {StructuralRestraint::fixedSupport(RestraintId::fromValue(1), face)});
        CHECK(prepared.constraints().size() == 3 * faceNodes);
        checkExactlyConstrained(model, numbering, prepared, face,
                                RestraintComponents::fixed());
    }

    SECTION("ux alone gives N") {
        const PreparedRestraints prepared = prepare(
            part, {StructuralRestraint{RestraintId::fromValue(1), face,
                                       RestraintComponents::along(DofComponent::Ux)}});
        CHECK(prepared.constraints().size() == faceNodes);
        checkExactlyConstrained(model, numbering, prepared, face,
                                RestraintComponents::along(DofComponent::Ux));
    }

    SECTION("uy alone gives N") {
        const PreparedRestraints prepared = prepare(
            part, {StructuralRestraint{RestraintId::fromValue(1), face,
                                       RestraintComponents::along(DofComponent::Uy)}});
        CHECK(prepared.constraints().size() == faceNodes);
        checkExactlyConstrained(model, numbering, prepared, face,
                                RestraintComponents::along(DofComponent::Uy));
    }

    SECTION("uz alone gives N") {
        const PreparedRestraints prepared = prepare(
            part, {StructuralRestraint{RestraintId::fromValue(1), face,
                                       RestraintComponents::along(DofComponent::Uz)}});
        CHECK(prepared.constraints().size() == faceNodes);
        checkExactlyConstrained(model, numbering, prepared, face,
                                RestraintComponents::along(DofComponent::Uz));
    }

    SECTION("ux+uy gives 2 N") {
        const RestraintComponents mask = RestraintComponents::along(DofComponent::Ux)
                                             .unionWith(RestraintComponents::along(DofComponent::Uy));
        const PreparedRestraints prepared =
            prepare(part, {StructuralRestraint{RestraintId::fromValue(1), face, mask}});
        CHECK(prepared.constraints().size() == 2 * faceNodes);
        checkExactlyConstrained(model, numbering, prepared, face, mask);
    }

    SECTION("ux+uz gives 2 N") {
        const RestraintComponents mask = RestraintComponents::along(DofComponent::Ux)
                                             .unionWith(RestraintComponents::along(DofComponent::Uz));
        const PreparedRestraints prepared =
            prepare(part, {StructuralRestraint{RestraintId::fromValue(1), face, mask}});
        CHECK(prepared.constraints().size() == 2 * faceNodes);
        checkExactlyConstrained(model, numbering, prepared, face, mask);
    }

    SECTION("uy+uz gives 2 N") {
        const RestraintComponents mask = RestraintComponents::along(DofComponent::Uy)
                                             .unionWith(RestraintComponents::along(DofComponent::Uz));
        const PreparedRestraints prepared =
            prepare(part, {StructuralRestraint{RestraintId::fromValue(1), face, mask}});
        CHECK(prepared.constraints().size() == 2 * faceNodes);
        checkExactlyConstrained(model, numbering, prepared, face, mask);
    }
}

TEST_CASE("StructuralBC_ConstrainsMoreThanTheMeshIsForcedTo", "[structural][bc]") {
    // The off-target half of brief section 135, on its own, with its premise
    // asserted: most of this mesh is NOT on the restrained face, so a
    // production path that constrained the whole model would be visible here.
    //
    // THE CORNERS CLAIM OF BRIEF SECTION 131 IS NOT MADE HERE, and the reason
    // is recorded on the fixture: every planar face of a box carries exactly
    // its four CAD corners whatever the sizing asks for, so a corners-only
    // implementation could not be distinguished from a correct one on this
    // geometry. It is made on RM-MESH-04's curved inner wall instead, where a
    // face carries many more nodes than it has bounding vertices.
    RestrainedPart part;
    const StructuralModel model = part.model();
    const FaceName face = part.side(0);
    const std::set<meshing::NodeId::ValueType> expected = expectedNodes(model, face);
    const std::size_t total = model.mesh().mesh().nodes().size();
    REQUIRE(expected.size() * 2 < total);

    const PreparedRestraints prepared = prepare(
        part, {StructuralRestraint::fixedSupport(RestraintId::fromValue(1), face)});
    CHECK(prepared.nodes().size() == expected.size());
    CHECK(prepared.constraints().size() == 3 * expected.size());
    CHECK(prepared.constraints().size() < 3 * total);
    WARN("the 40 x 20 mm side face carries " << expected.size() << " of the mesh's " << total
                                             << " nodes");
}

TEST_CASE("StructuralBC_NumbersThroughP17DofAndNeverByArithmetic", "[structural][bc]") {
    // Every constrained index must be one P17-DOF would produce for a node of
    // the target and a requested component. Checked by DECODING each index
    // back to its NodalDof through `dofAt`, which is the inverse P17-DOF owns.
    RestrainedPart part;
    const StructuralModel model = part.model();
    const MeshDofMap numbering = part.numbering();
    const FaceName face = part.startCap();
    const std::set<meshing::NodeId::ValueType> expected = expectedNodes(model, face);

    const RestraintComponents mask = RestraintComponents::along(DofComponent::Uy)
                                         .unionWith(RestraintComponents::along(DofComponent::Uz));
    const PreparedRestraints prepared =
        prepare(part, {StructuralRestraint{RestraintId::fromValue(1), face, mask}});

    for (const DofIndex index : prepared.constraints().constrained()) {
        const Result<NodalDof> dof = numbering.dofAt(index);
        INFO("index " << index.value());
        REQUIRE(dof.has_value());
        CHECK(expected.contains(dof->node.value()));
        CHECK(mask.holds(dof->component));
    }
    CHECK(prepared.constraints().size() == 2 * expected.size());
}

TEST_CASE("StructuralBC_IntegratesWithTheFreeEquationNumbering", "[structural][bc]") {
    // Brief sections 63 and 64: P17-BC builds a ConstraintSet and P17-DOF
    // numbers the free equations. The identity that matters is the partition.
    RestrainedPart part;
    const MeshDofMap numbering = part.numbering();
    const StructuralModel model = part.model();
    const FaceName face = part.endCap();
    const std::size_t faceNodes = expectedNodes(model, face).size();

    const PreparedRestraints prepared = prepare(
        part, {StructuralRestraint::fixedSupport(RestraintId::fromValue(1), face)});

    Result<FreeEquationMap> free =
        structural::buildFreeEquationMap(numbering, prepared.constraints());
    INFO((free.has_value() ? std::string{} : free.error().message));
    REQUIRE(free.has_value());

    const DofIndex::ValueType total = numbering.dofCount();
    WARN("N_total " << total << ", N_constrained " << free->constrainedCount() << ", N_free "
                    << free->freeCount());

    CHECK(free->dofCount() == total);
    CHECK(free->constrainedCount() == 3 * faceNodes);
    CHECK(free->freeCount() + free->constrainedCount() == total);

    SECTION("and a fully fixed body leaves no free equation") {
        // Brief section 65: representable, and not a crash. Every node, every
        // component, through P17-DOF's own helper.
        const std::vector<NodalDof> everything = structural::fullyFixedDofs(numbering.nodes());
        Result<ConstraintSet> all = structural::buildConstraintSet(numbering, everything);
        REQUIRE(all.has_value());
        Result<FreeEquationMap> none = structural::buildFreeEquationMap(numbering, *all);
        REQUIRE(none.has_value());
        CHECK(none->freeCount() == 0);
        CHECK(none->constrainedCount() == total);
    }
}

// ---------------------------------------------------------------------------
// Overlap, duplication and the union
// ---------------------------------------------------------------------------

TEST_CASE("StructuralBC_CombinesComplementaryRestraintsOnOneFace", "[structural][bc]") {
    // Brief sections 28 and 88: three single-component restraints on one face
    // are a fixed support, and the derived set is identical.
    RestrainedPart part;
    const StructuralModel model = part.model();
    const MeshDofMap numbering = part.numbering();
    const FaceName face = part.endCap();
    const std::size_t faceNodes = expectedNodes(model, face).size();

    const PreparedRestraints combined = prepare(
        part, {StructuralRestraint{RestraintId::fromValue(1), face,
                                   RestraintComponents::along(DofComponent::Ux)},
               StructuralRestraint{RestraintId::fromValue(2), face,
                                   RestraintComponents::along(DofComponent::Uy)},
               StructuralRestraint{RestraintId::fromValue(3), face,
                                   RestraintComponents::along(DofComponent::Uz)}});
    const PreparedRestraints fixed = prepare(
        part, {StructuralRestraint::fixedSupport(RestraintId::fromValue(1), face)});

    CHECK(combined.constraints().size() == 3 * faceNodes);
    CHECK(combined.constraints() == fixed.constraints());
    checkExactlyConstrained(model, numbering, combined, face, RestraintComponents::fixed());

    SECTION("but the per-restraint traceability still shows three records") {
        CHECK(combined.resolutions().size() == 3);
        std::size_t summed = 0;
        for (const RestraintResolution& resolution : combined.resolutions()) {
            CHECK(resolution.components.count() == 1);
            summed += resolution.degreesOfFreedom;
        }
        CHECK(summed == combined.constraints().size());
    }
}

TEST_CASE("StructuralBC_UnionsOverlappingRestraintsWithoutRefusingThem", "[structural][bc]") {
    // Brief sections 27, 29, 68, 69 and 89. P17-DOF's `buildConstraintSet`
    // REFUSES a repeated DofIndex, deliberately. So a redundant restraint is
    // normalised here, because every prescribed value in this scope is zero
    // and the repetition is demonstrably harmless -- and a lawful overlap
    // between two CAD faces must not fail because their shared edge nodes
    // repeat.
    RestrainedPart part;
    const StructuralModel model = part.model();
    const MeshDofMap numbering = part.numbering();
    const FaceName face = part.endCap();
    const std::size_t faceNodes = expectedNodes(model, face).size();

    SECTION("a fixed support plus a redundant ux on the same face is still 3 N") {
        const PreparedRestraints prepared = prepare(
            part, {StructuralRestraint::fixedSupport(RestraintId::fromValue(1), face),
                   StructuralRestraint{RestraintId::fromValue(2), face,
                                       RestraintComponents::along(DofComponent::Ux)}});
        CHECK(prepared.constraints().size() == 3 * faceNodes);
        checkExactlyConstrained(model, numbering, prepared, face,
                                RestraintComponents::fixed());
        // The sum over the records EXCEEDS the set, which is the point of
        // recording both.
        std::size_t summed = 0;
        for (const RestraintResolution& resolution : prepared.resolutions()) {
            summed += resolution.degreesOfFreedom;
        }
        CHECK(summed == 4 * faceNodes);
        CHECK(summed > prepared.constraints().size());
    }

    SECTION("two different ids on the identical target and component is one set of N") {
        // Brief section 26: distinct identities, same physical consequence.
        // Lawful, and NOT a duplicate-identity error.
        const PreparedRestraints prepared = prepare(
            part, {StructuralRestraint{RestraintId::fromValue(1), face,
                                       RestraintComponents::along(DofComponent::Ux)},
                   StructuralRestraint{RestraintId::fromValue(2), face,
                                       RestraintComponents::along(DofComponent::Ux)}});
        CHECK(prepared.constraints().size() == faceNodes);
        CHECK(prepared.nodes().size() == faceNodes);
    }

    SECTION("two adjacent faces sharing an edge contribute the shared nodes once") {
        // Brief sections 86 and 136: a node on the shared edge belongs to BOTH
        // targets, and that is not off-target. Its premise is asserted first:
        // the two faces must really share nodes.
        const FaceName other = part.side(0);
        const std::set<meshing::NodeId::ValueType> a = expectedNodes(model, face);
        const std::set<meshing::NodeId::ValueType> b = expectedNodes(model, other);
        std::vector<meshing::NodeId::ValueType> shared;
        std::ranges::set_intersection(a, b, std::back_inserter(shared));
        REQUIRE_FALSE(shared.empty());

        const PreparedRestraints prepared = prepare(
            part, {StructuralRestraint{RestraintId::fromValue(1), face,
                                       RestraintComponents::along(DofComponent::Uz)},
                   StructuralRestraint{RestraintId::fromValue(2), other,
                                       RestraintComponents::along(DofComponent::Uz)}});

        std::set<meshing::NodeId::ValueType> expected = a;
        expected.insert(b.begin(), b.end());
        CHECK(prepared.nodes().size() == expected.size());
        CHECK(prepared.constraints().size() == expected.size());
        CHECK(prepared.nodes().size() < a.size() + b.size());
        WARN("the end cap and side 0 share " << shared.size() << " nodes, counted once");
    }

    SECTION("opposing faces restrained in the same component union cleanly") {
        // Brief section 96. Opposite caps of the block share no node, so the
        // union is the exact sum -- which is what makes it the complement of
        // the shared-edge case above.
        const FaceName other = part.startCap();
        const std::set<meshing::NodeId::ValueType> a = expectedNodes(model, face);
        const std::set<meshing::NodeId::ValueType> b = expectedNodes(model, other);
        std::vector<meshing::NodeId::ValueType> shared;
        std::ranges::set_intersection(a, b, std::back_inserter(shared));
        REQUIRE(shared.empty());

        const PreparedRestraints prepared = prepare(
            part, {StructuralRestraint{RestraintId::fromValue(1), face,
                                       RestraintComponents::along(DofComponent::Ux)},
                   StructuralRestraint{RestraintId::fromValue(2), other,
                                       RestraintComponents::along(DofComponent::Ux)}});
        CHECK(prepared.nodes().size() == a.size() + b.size());
        CHECK(prepared.constraints().size() == a.size() + b.size());
    }
}

TEST_CASE("StructuralBC_RefusesTwoRecordsWithOneRestraintId", "[structural][bc]") {
    // Brief section 25. Two records for one identity is MALFORMED, not an
    // overlap: one of them would be ignored and nothing could tell which.
    RestrainedPart part;
    const FaceName face = part.endCap();
    const Error error = refusal(
        part, {StructuralRestraint::fixedSupport(RestraintId::fromValue(4), face),
               StructuralRestraint{RestraintId::fromValue(4), part.startCap(),
                                   RestraintComponents::along(DofComponent::Ux)}});
    CHECK(error.code == ErrorCode::InvalidArgument);
    CHECK_THAT(error.message, ContainsSubstring("restraint:4"));
    CHECK_THAT(error.message, ContainsSubstring("more than once"));

    const StructuralModel model = part.model();
    CHECK(structural::structuralRestraintProblem(
              model, part.numbering(),
              std::vector{StructuralRestraint::fixedSupport(RestraintId::fromValue(4), face),
                          StructuralRestraint::fixedSupport(RestraintId::fromValue(4), face)}) ==
          RestraintProblem::DuplicateRestraintId);
}

TEST_CASE("StructuralBC_RefusesARestraintWithNoComponent", "[structural][bc]") {
    // Brief section 6. A restraint that holds nothing at zero is a record
    // whose intent cannot be acted on, not a weaker restraint, and it is
    // refused BEFORE any mesh work.
    RestrainedPart part;
    const StructuralModel model = part.model();
    const std::vector<StructuralRestraint> restraints{
        StructuralRestraint{RestraintId::fromValue(2), part.endCap(), RestraintComponents{}}};

    const Error error = refusal(part, restraints);
    CHECK(error.code == ErrorCode::InvalidArgument);
    CHECK_THAT(error.message, ContainsSubstring("restraint:2"));
    CHECK_THAT(error.message, ContainsSubstring("nothing for it to constrain"));
    CHECK(structural::structuralRestraintProblem(model, part.numbering(), restraints) ==
          RestraintProblem::NoComponents);
}

TEST_CASE("StructuralBC_PreparesAnEmptyRestraintSetSuccessfully", "[structural][bc]") {
    // Brief sections 66 and 106. A model with no restraints is a real model
    // whose stiffness matrix is singular; refusing it here would report a
    // physics problem as a resolution failure, and P17-SOLVE-001 is where the
    // rigid-body modes are visible.
    RestrainedPart part;
    const PreparedRestraints prepared = prepare(part, {});
    CHECK(prepared.constraints().isEmpty());
    CHECK(prepared.constraints().size() == 0);
    CHECK(prepared.nodes().empty());
    CHECK(prepared.resolutions().empty());
    CHECK(prepared.describes(part.volume().mesh()));

    // And the free numbering it gives is the whole model, which is exactly the
    // separation of concerns: no refusal here, every DOF free.
    Result<FreeEquationMap> free =
        structural::buildFreeEquationMap(part.numbering(), prepared.constraints());
    REQUIRE(free.has_value());
    CHECK(free->freeCount() == part.numbering().dofCount());
    CHECK(free->constrainedCount() == 0);
}

TEST_CASE("StructuralBC_DoesNotDecideWhetherTheModelIsSufficientlyConstrained",
          "[structural][bc]") {
    // Brief section 67. One ux restraint on one face leaves five rigid-body
    // modes, and preparation SUCCEEDS: detecting that is later work, and
    // deciding it here would be overreach.
    RestrainedPart part;
    const PreparedRestraints prepared = prepare(
        part, {StructuralRestraint{RestraintId::fromValue(1), part.endCap(),
                                   RestraintComponents::along(DofComponent::Ux)}});
    CHECK_FALSE(prepared.constraints().isEmpty());
    CHECK(prepared.constraints().size() < part.numbering().dofCount());
}

// ---------------------------------------------------------------------------
// Failure paths
// ---------------------------------------------------------------------------

TEST_CASE("StructuralBC_RefusesAnUnresolvedTargetWithNoNearestFaceFallback",
          "[structural][bc]") {
    // Brief sections 14, 74, 75, 76 and 100. The refusal IS the pass. A
    // FaceName naming a feature that is not in this body resolves to nothing,
    // and nothing nearby is substituted -- the block has six nameable faces
    // and a fallback would have found one of them.
    RestrainedPart part;
    const StructuralModel model = part.model();
    const FaceName absent{ObjectId::fromValue(part.feature.value() + 500),
                          FaceSelector{.role = FaceRole::EndCap}};
    const std::vector<StructuralRestraint> restraints{
        StructuralRestraint::fixedSupport(RestraintId::fromValue(9), absent)};

    const Error error = refusal(part, restraints);
    CHECK(error.code == ErrorCode::NotFound);
    CHECK_THAT(error.message, ContainsSubstring("restraint:9"));
    CHECK_THAT(error.message, ContainsSubstring("names no face of the body"));
    CHECK_THAT(error.message, ContainsSubstring("Nothing nearby is rebound"));
    CHECK(structural::structuralRestraintProblem(model, part.numbering(), restraints) ==
          RestraintProblem::TargetUnresolved);
}

TEST_CASE("StructuralBC_RefusesAMalformedTargetBeforeTouchingTheMesh", "[structural][bc]") {
    // Brief section 102. A Side face is named by a profile entity; omitting it
    // makes the selector malformed on its own terms, and core's own
    // validate(FaceSelector) says so.
    RestrainedPart part;
    const StructuralModel model = part.model();
    const FaceName malformed{part.feature, FaceSelector{.role = FaceRole::Side}};
    const std::vector<StructuralRestraint> restraints{
        StructuralRestraint::fixedSupport(RestraintId::fromValue(1), malformed)};

    const Error error = refusal(part, restraints);
    CHECK(error.code == ErrorCode::InvalidArgument);
    CHECK(structural::structuralRestraintProblem(model, part.numbering(), restraints) ==
          RestraintProblem::TargetInvalid);

    SECTION("and the canonical record check catches it without a mesh at all") {
        const Result<void> checked = structural::validate(restraints.front());
        REQUIRE_FALSE(checked.has_value());
        CHECK(checked.error().code == ErrorCode::InvalidArgument);
    }
}

TEST_CASE("StructuralBC_RefusesANumberingBuiltForADifferentMesh", "[structural][bc]") {
    // Brief sections 44, 58, 80 and the mutation "reuse M1 prepared
    // constraints on M2". The numbering carries a MeshStamp and a node count,
    // and a stamp alone is not sufficient -- MeshDofMap::describes records
    // why.
    RestrainedPart part;
    const MeshDofMap stale = part.numbering();
    const meshing::MeshStamp firstStamp = part.volume().mesh().stamp();

    // A new generation of the same unchanged model: a different mesh identity.
    part.mesh();
    const StructuralModel model = part.model();
    REQUIRE(model.mesh().mesh().stamp() != firstStamp);
    REQUIRE_FALSE(stale.describes(model.mesh().mesh()));

    const std::vector<StructuralRestraint> restraints{
        StructuralRestraint::fixedSupport(RestraintId::fromValue(1), part.endCap())};
    Result<PreparedRestraints> prepared =
        structural::prepareStructuralRestraints(model, stale, restraints);
    REQUIRE_FALSE(prepared.has_value());
    CHECK(prepared.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(prepared.error().message, ContainsSubstring("different mesh"));
    CHECK(structural::structuralRestraintProblem(model, stale, restraints) ==
          RestraintProblem::NumberingIsForADifferentMesh);

    SECTION("even with no restraints at all, so nothing half-bound is published") {
        CHECK(structural::structuralRestraintProblem(model, stale, {}) ==
              RestraintProblem::NumberingIsForADifferentMesh);
        CHECK_FALSE(structural::prepareStructuralRestraints(model, stale, {}).has_value());
    }

    SECTION("and a set prepared against the matching numbering is stamped with that mesh") {
        const PreparedRestraints prepared_ = prepare(part, restraints);
        CHECK(prepared_.describes(model.mesh().mesh()));
        CHECK(prepared_.mesh() == model.mesh().mesh().stamp());
        CHECK(prepared_.mesh() != firstStamp);
    }
}

TEST_CASE("StructuralBC_CannotBePreparedBeforeAMeshExists", "[structural][bc]") {
    // Brief section 105. There is no mesh, so there is no StructuralModel, so
    // there is nothing to prepare against -- and no zero-constraint set is
    // fabricated. ADR-036's gate, inherited rather than re-asked.
    Document document{"Part"};
    features::Regenerator regenerator;
    meshing::Mesher mesher;

    auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
    addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
    const ObjectId profile = require(document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = 20_mm});
    REQUIRE(extrude.has_value());
    const ObjectId feature = require(document.addObject(std::move(*extrude)));
    auto intent =
        meshing::MeshControl::create("Mesh", meshing::MeshControlDefinition{.body = feature});
    REQUIRE(intent.has_value());
    const MeshControlId control =
        MeshControlId::fromValue(require(document.addObject(std::move(*intent))).value());
    requireReport(regenerator, document);

    const Result<StructuralModel> model =
        structural::requireStructuralModel(document, regenerator, mesher, control);
    REQUIRE_FALSE(model.has_value());
    WARN("no mesh: " << model.error().message);
}

// ---------------------------------------------------------------------------
// Remesh and determinism
// ---------------------------------------------------------------------------

TEST_CASE("StructuralBC_ReResolvesTheNodeSetAfterARemesh", "[structural][bc]") {
    // Brief sections 42, 43, 80 and 132. The canonical restraint never
    // changes; the mesh does. The invariant across the remesh is the SEMANTIC
    // one -- every current node on the restrained face carries the requested
    // components -- and never a raw count comparison.
    RestrainedPart part;
    const FaceName face = part.endCap();
    const std::vector<StructuralRestraint> restraints{
        StructuralRestraint::fixedSupport(RestraintId::fromValue(1), face)};

    const StructuralModel first = part.model();
    const MeshDofMap firstNumbering = part.numbering();
    const PreparedRestraints before =
        *structural::prepareStructuralRestraints(first, firstNumbering, restraints);
    const meshing::MeshStamp firstStamp = first.mesh().mesh().stamp();

    part.mesh();
    const StructuralModel second = part.model();
    const MeshDofMap secondNumbering = part.numbering();
    Result<PreparedRestraints> after =
        structural::prepareStructuralRestraints(second, secondNumbering, restraints);
    INFO((after.has_value() ? std::string{} : after.error().message));
    REQUIRE(after.has_value());

    SECTION("the mesh identity moved, so the old prepared set is not readable against it") {
        CHECK(second.mesh().mesh().stamp() != firstStamp);
        // THE CLAIM THAT MATTERS, both ways round: the set prepared for the
        // first mesh does NOT describe the second, and the one prepared for
        // the second does. A consumer cannot mistake one for the other.
        CHECK_FALSE(before.describes(second.mesh().mesh()));
        CHECK(after->describes(second.mesh().mesh()));
        CHECK(after->mesh() == second.mesh().mesh().stamp());
        CHECK(before.mesh() == firstStamp);
    }

    SECTION("the canonical record is unchanged, byte for byte") {
        const std::vector<StructuralRestraint> again{
            StructuralRestraint::fixedSupport(RestraintId::fromValue(1), face)};
        CHECK(again == restraints);
    }

    SECTION("and every node of the re-resolved face carries all three components") {
        checkExactlyConstrained(second, secondNumbering, *after, face,
                                RestraintComponents::fixed());
    }
}

TEST_CASE("StructuralBC_IsIndependentOfTheOrderRestraintsAreGivenIn", "[structural][bc]") {
    // Brief sections 34, 91, 92, 110 and 111. Order is the user's and is
    // preserved in the RECORDS; it cannot reach the derived set, because the
    // node union is sorted and the ConstraintSet sorts its indices.
    RestrainedPart part;
    const FaceName a = part.endCap();
    const FaceName b = part.side(1);
    const StructuralRestraint first{RestraintId::fromValue(1), a,
                                    RestraintComponents::along(DofComponent::Ux)};
    const StructuralRestraint second{RestraintId::fromValue(2), b,
                                     RestraintComponents::fixed()};

    const PreparedRestraints forwards = prepare(part, {first, second});
    const PreparedRestraints backwards = prepare(part, {second, first});

    CHECK(forwards.constraints() == backwards.constraints());
    CHECK(std::ranges::equal(forwards.nodes(), backwards.nodes()));

    SECTION("but the per-restraint records keep the order they were given in") {
        REQUIRE(forwards.resolutions().size() == 2);
        REQUIRE(backwards.resolutions().size() == 2);
        CHECK(forwards.resolutions()[0].restraint == RestraintId::fromValue(1));
        CHECK(backwards.resolutions()[0].restraint == RestraintId::fromValue(2));
    }

    SECTION("and repeating the preparation gives exactly the same set") {
        // Brief section 109: the fingerprints are the ordered index lists, and
        // they are compared element for element with no tolerance.
        const PreparedRestraints again = prepare(part, {first, second});
        CHECK(std::ranges::equal(again.constraints().constrained(),
                                 forwards.constraints().constrained()));
        CHECK(std::ranges::equal(again.nodes(), forwards.nodes()));
        CHECK(again.constraints() == forwards.constraints());
    }
}

// ---------------------------------------------------------------------------
// Currentness
// ---------------------------------------------------------------------------

TEST_CASE("StructuralBC_ARestraintEditStalesTheResultAndNotTheMesh", "[structural][bc]") {
    // Brief sections 52, 53, 54 and 56, and the mutations "mark P16 mesh stale
    // on restraint edit" and "omit restraint revision from result currentness".
    //
    // AND NO NEW MECHANISM. The restraints live in the analysis definition, so
    // an edit moves `analysisRevision`, which P17-DATA-001 already compares.
    RestrainedPart part{/*withMaterial=*/true};
    const FaceName face = part.endCap();
    REQUIRE(part.setRestraints({StructuralRestraint{
        RestraintId::fromValue(1), face, RestraintComponents::along(DofComponent::Ux)}}));

    const StructuralResultSource before = part.currentSource();
    const StructuralResult result = part.resultFor(before);
    REQUIRE(structural::resultCurrency(&result, part.currentSource()) ==
            structural::ResultCurrency::Current);

    // THE CONTROL, AND IT IS NOT OPTIONAL. Every section below asserts that an
    // edit moved the revision; this asserts that merely CALLING the setter does
    // not. Without it the sections prove only that modifyObject bumps a
    // revision, which it would do whatever the definition held.
    SECTION("re-setting the identical restraints changes nothing") {
        CHECK_FALSE(part.setRestraints({StructuralRestraint{
            RestraintId::fromValue(1), face, RestraintComponents::along(DofComponent::Ux)}}));
        const StructuralResultSource after = part.currentSource();
        CHECK(after == before);
        CHECK(structural::resultCurrency(&result, after) == structural::ResultCurrency::Current);
    }

    auto stales = [&](const char* what) {
        const StructuralResultSource after = part.currentSource();
        INFO(what);
        CHECK(structural::resultCurrency(&result, after) == structural::ResultCurrency::Stale);
        CHECK(structural::staleReasons(result.source(), after) ==
              std::vector{structural::StaleReason::Analysis});
        // The mesh did NOT move, and neither did the geometry.
        CHECK(part.mesher.currency(part.document, part.control) ==
              meshing::MeshCurrency::Current);
        CHECK(after.mesh == before.mesh);
        CHECK(after.geometry == before.geometry);
    };

    SECTION("editing the component mask") {
        CHECK(part.setRestraints({StructuralRestraint{
            RestraintId::fromValue(1), face,
            RestraintComponents::along(DofComponent::Ux)
                .unionWith(RestraintComponents::along(DofComponent::Uy))}}));
        stales("ux -> ux+uy");
    }

    SECTION("editing the target face") {
        CHECK(part.setRestraints(
            {StructuralRestraint{RestraintId::fromValue(1), part.startCap(),
                                 RestraintComponents::along(DofComponent::Ux)}}));
        stales("end cap -> start cap");
    }

    SECTION("adding a restraint") {
        CHECK(part.setRestraints(
            {StructuralRestraint{RestraintId::fromValue(1), face,
                                 RestraintComponents::along(DofComponent::Ux)},
             StructuralRestraint::fixedSupport(RestraintId::fromValue(2), part.startCap())}));
        stales("one restraint -> two");
    }

    SECTION("removing a restraint") {
        CHECK(part.setRestraints({}));
        stales("one restraint -> none");
    }
}

TEST_CASE("StructuralBC_ARestraintLivesInTheDefinitionAndNotInAParallelCounter",
          "[structural][bc]") {
    // Brief section 56: no second stale counter was added. The evidence is
    // that the restraints are a field of the definition, whose revision is the
    // object's -- so an identical edit applied twice gives identical sources.
    RestrainedPart part{/*withMaterial=*/true};
    const StructuralRestraint restraint{RestraintId::fromValue(1), part.endCap(),
                                        RestraintComponents::fixed()};

    REQUIRE(part.setRestraints({restraint}));
    const StructuralAnalysis* study =
        part.document.findObjectAs<StructuralAnalysis>(ObjectId::fromValue(part.analysis.value()));
    REQUIRE(study != nullptr);
    REQUIRE(study->definition().restraints.size() == 1);
    CHECK(study->definition().restraints.front() == restraint);

    // A definition holding the same restraints compares equal, which is what
    // makes the revision the only thing that has to move.
    StructuralAnalysisDefinition copy = study->definition();
    CHECK(copy == study->definition());
    copy.restraints.front() = StructuralRestraint{RestraintId::fromValue(1), part.endCap(),
                                                  RestraintComponents::along(DofComponent::Uz)};
    CHECK(copy != study->definition());
}

TEST_CASE("StructuralBC_IsIndependentOfTheMaterial", "[structural][bc]") {
    // Brief section 120, stated precisely rather than loosely.
    //
    // WHAT IS NOT TRUE: that a restraint can be prepared on a body with no
    // material. `requireStructuralModel` refuses one -- the input boundary
    // asks for a material on behalf of the whole of P17, and the audit found
    // that before this test was written. Claiming otherwise would be a false
    // capability.
    //
    // WHAT IS TRUE, AND IS WHAT THE MILESTONE OWNS: a restraint is kinematic.
    // `prepareStructuralRestraints` takes no material parameter at all, and
    // changing the one the body has cannot move a single constrained index.
    static_assert(std::is_invocable_r_v<Result<PreparedRestraints>,
                                        decltype(structural::prepareStructuralRestraints),
                                        const StructuralModel&, const MeshDofMap&,
                                        std::span<const StructuralRestraint>>,
                  "restraint preparation takes a model, a numbering and the restraints -- there "
                  "is no material argument to pass");
    static_assert(!std::is_invocable_v<decltype(structural::prepareStructuralRestraints),
                                       const StructuralModel&, const MeshDofMap&,
                                       std::span<const StructuralRestraint>,
                                       structural::StructuralMaterial>);

    RestrainedPart part;
    const std::vector<StructuralRestraint> restraints{
        StructuralRestraint::fixedSupport(RestraintId::fromValue(1), part.endCap())};
    const PreparedRestraints steel = prepare(part, restraints);
    CHECK_FALSE(steel.constraints().isEmpty());

    // 210 GPa -> 70 GPa, a factor of three, and the constrained set does not
    // move by one index.
    materials::MechanicalProperties mechanical =
        features::findMaterial(part.document, part.material)->definition().mechanical;
    mechanical.youngsModulus = materials::MaterialProperty<ElasticModulus>::known(70_GPa);
    REQUIRE(features::setMaterialMechanical(part.document, part.material, mechanical).has_value());

    const PreparedRestraints aluminium = prepare(part, restraints);
    CHECK(aluminium.constraints() == steel.constraints());
    CHECK(std::ranges::equal(aluminium.nodes(), steel.nodes()));
}

TEST_CASE("StructuralBC_ReportsEveryProblemThroughTheSameOrderedChecks", "[structural][bc]") {
    // `structuralRestraintProblem` and `prepareStructuralRestraints` run one
    // shared pass, so a caller that asks which problem there is cannot be told
    // something different from the caller that asks for the set. Asserted by
    // agreeing on every case the diagnostics matrix records.
    RestrainedPart part;
    const StructuralModel model = part.model();
    const MeshDofMap numbering = part.numbering();

    struct Case {
        const char* what;
        std::vector<StructuralRestraint> restraints;
        RestraintProblem expected;
    };
    const std::vector<Case> cases{
        {"duplicate id",
         {StructuralRestraint::fixedSupport(RestraintId::fromValue(1), part.endCap()),
          StructuralRestraint::fixedSupport(RestraintId::fromValue(1), part.startCap())},
         RestraintProblem::DuplicateRestraintId},
        {"empty mask",
         {StructuralRestraint{RestraintId::fromValue(1), part.endCap(), RestraintComponents{}}},
         RestraintProblem::NoComponents},
        {"malformed selector",
         {StructuralRestraint::fixedSupport(RestraintId::fromValue(1),
                                            FaceName{part.feature,
                                                     FaceSelector{.role = FaceRole::Side}})},
         RestraintProblem::TargetInvalid},
        {"unresolved target",
         {StructuralRestraint::fixedSupport(
             RestraintId::fromValue(1),
             FaceName{ObjectId::fromValue(part.feature.value() + 500),
                      FaceSelector{.role = FaceRole::EndCap}})},
         RestraintProblem::TargetUnresolved},
    };

    for (const Case& one : cases) {
        INFO(one.what);
        const std::optional<RestraintProblem> problem =
            structural::structuralRestraintProblem(model, numbering, one.restraints);
        REQUIRE(problem.has_value());
        CHECK(*problem == one.expected);
        // And nothing is published.
        CHECK_FALSE(
            structural::prepareStructuralRestraints(model, numbering, one.restraints).has_value());
        // Every value has a name, and none of them is "unknown".
        CHECK(structural::toString(*problem) != "unknown");
    }

    SECTION("and the two values no mesh can produce are named too") {
        // `TargetWithoutFacets` and `TargetWithoutNodes` are UNTESTED rather
        // than untested-and-unmentioned: P16 reports an unattributed face as
        // Unresolved, so a resolved face with no facet cannot be constructed
        // through the production path. They are kept because a mapping that
        // lost a face must be reported and not passed over.
        CHECK(structural::toString(RestraintProblem::TargetWithoutFacets) ==
              "target_without_facets");
        CHECK(structural::toString(RestraintProblem::TargetWithoutNodes) ==
              "target_without_nodes");
        CHECK(structural::toString(RestraintProblem::NumberingIsForADifferentMesh) ==
              "numbering_is_for_a_different_mesh");
    }
}
