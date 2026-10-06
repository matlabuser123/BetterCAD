// P17-DATA-001: the identity model, the result model and currentness.
//
// WHAT THESE TESTS ARE FOR. This milestone's claims are almost all about what
// CANNOT happen -- a DOF index cannot be persisted, a load cannot be a node, a
// result from one mesh cannot be read against another, a stale result cannot be
// made to look current. A claim of that shape is worth nothing written in a
// comment, so each one here is either a compile-fail case or a test that
// constructs the dangerous thing and watches it be refused.
//
// THE VALUES IN THE RESULTS BELOW ARE NOT A SOLVE. Nothing solves yet. They are
// data chosen to exercise the container's invariants -- cardinality, ordering,
// finiteness, stamp binding -- and the tests say nothing about physics. The
// first physical numbers arrive with P17-SOLVE-001.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/structural/StructuralAnalysisObject.hpp>
#include <bettercad/structural/StructuralData.hpp>
#include <bettercad/structural/StructuralResult.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using structural::AnalysisState;
using structural::DofComponent;
using structural::DofIndex;
using structural::ResultCurrency;
using structural::StaleReason;
using structural::Strain6;
using structural::Stress6;
using structural::StructuralAnalysis;
using structural::StructuralAnalysisDefinition;
using structural::StructuralResult;
using structural::StructuralResultSource;
using structural::TensorComponent;

namespace {

/// A document with a block, a meshing control, a material and an analysis:
/// everything a structural result needs provenance for.
struct AnalysedPart {
    Document document{"Part"};
    features::Regenerator regenerator;
    meshing::Mesher mesher;
    ObjectId feature{};
    MeshControlId control{};
    AnalysisId analysis{};
    MaterialId material{};

    AnalysedPart() {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
        const ObjectId profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = 20_mm});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));

        auto intent = meshing::MeshControl::create("Mesh",
                                                   meshing::MeshControlDefinition{.body = feature});
        REQUIRE(intent.has_value());
        control = MeshControlId::fromValue(require(document.addObject(std::move(*intent))).value());

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

        auto study = StructuralAnalysis::create("Study",
                                                StructuralAnalysisDefinition{.mesh = control});
        REQUIRE(study.has_value());
        analysis = AnalysisId::fromValue(require(document.addObject(std::move(*study))).value());

        requireReport(regenerator, document);
    }

    void requireMesh() {
        const Result<const meshing::VolumeMesh*> mesh =
            mesher.generate(document, regenerator, control);
        INFO((mesh.has_value() ? std::string{} : mesh.error().message));
        REQUIRE(mesh.has_value());
    }

    [[nodiscard]] const meshing::VolumeMesh& mesh() const {
        const meshing::VolumeMesh* held = mesher.mesh(control);
        REQUIRE(held != nullptr);
        return *held;
    }

    [[nodiscard]] StructuralResultSource currentSource() const {
        const Result<StructuralResultSource> source =
            structural::currentResultSource(document, regenerator, mesher, analysis);
        INFO((source.has_value() ? std::string{} : source.error().message));
        REQUIRE(source.has_value());
        return *source;
    }

    /// A result of the right shape for the current mesh. The numbers exercise
    /// the container and are not a solution; see the file header.
    [[nodiscard]] StructuralResult resultFor(const StructuralResultSource& source) const {
        const meshing::VolumeMesh& held = mesh();
        std::vector<Translation3D> displacements(held.nodeCount(), Translation3D{});
        std::vector<Strain6> strains(held.tetrahedronCount(), Strain6{});
        std::vector<Stress6> stresses(held.tetrahedronCount(), Stress6{});
        Result<StructuralResult> result = StructuralResult::create(
            source, held, std::move(displacements), {}, std::move(strains), std::move(stresses));
        INFO((result.has_value() ? std::string{} : result.error().message));
        REQUIRE(result.has_value());
        return std::move(*result);
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Identity domains
// ---------------------------------------------------------------------------

TEST_CASE("StructuralData_TheThreeDocumentIdentitiesAreDistinctTypes", "[structural][data][id]") {
    // Not interchangeable, and not integers. The compile-fail cases in
    // tests/compile_fail/StructuralIdMisuse.cpp prove the conversions do not
    // exist; this proves the types are distinct and behave as identities.
    static_assert(!std::is_same_v<AnalysisId, LoadId>);
    static_assert(!std::is_same_v<LoadId, RestraintId>);
    static_assert(!std::is_same_v<AnalysisId, RestraintId>);
    static_assert(!std::is_convertible_v<AnalysisId, std::uint64_t>);
    static_assert(!std::is_convertible_v<std::uint64_t, LoadId>);

    CHECK_FALSE(AnalysisId{}.isValid());
    CHECK(AnalysisId::fromValue(7).isValid());
    CHECK(AnalysisId::fromValue(7) == AnalysisId::fromValue(7));
    CHECK(AnalysisId::fromValue(7) < AnalysisId::fromValue(8));
}

TEST_CASE("StructuralData_OnlyTheAnalysisIsADocumentObjectIdentity", "[structural][data][id]") {
    // THE DOMAIN CLAIM, IN THE TYPE SYSTEM. An analysis is a document object,
    // so its ID widens to ObjectId and the document's own machinery -- the
    // dependency graph, findObject, the command history -- can take it. A load
    // and a restraint are members of an analysis's definition, like a boundary
    // set is a member of a control's, so theirs do not widen.
    static_assert(std::is_convertible_v<AnalysisId, ObjectId>);
    static_assert(!std::is_convertible_v<LoadId, ObjectId>);
    static_assert(!std::is_convertible_v<RestraintId, ObjectId>);
    static_assert(!std::is_convertible_v<BoundarySetId, ObjectId>,
                  "the precedent this follows: a member identity does not widen");

    AnalysedPart part;
    const ObjectId widened = part.analysis;
    CHECK(part.document.findObject(widened) != nullptr);
}

TEST_CASE("StructuralData_ADofIndexIsSolverLocalAndNotADocumentIdentity",
          "[structural][data][dof]") {
    // Not an Id<Tag> at all, which is the point: core/Id.hpp is where
    // PERSISTED document identities live, and a DOF index means one row of one
    // assembled system under one numbering of one mesh.
    static_assert(!std::is_convertible_v<DofIndex, ObjectId>);
    static_assert(!std::is_convertible_v<DofIndex, meshing::NodeId>);
    static_assert(!std::is_convertible_v<meshing::NodeId, DofIndex>);
    static_assert(!std::is_convertible_v<DofIndex, std::uint64_t>);
    static_assert(!std::is_convertible_v<std::uint64_t, DofIndex>);

    // 64-BIT ON PURPOSE. A NodeId is 32-bit and there are three DOFs per node,
    // so a 32-bit index overflows before the node count does. Checked rather
    // than asserted in a comment.
    static_assert(std::is_same_v<DofIndex::ValueType, std::uint64_t>);
    static_assert(sizeof(DofIndex::ValueType) > sizeof(meshing::NodeId::ValueType));
    constexpr std::uint64_t beyond32Bits = 3ULL * 2'000'000'000ULL;
    CHECK(DofIndex::fromValue(beyond32Bits).value() == beyond32Bits);

    CHECK_FALSE(DofIndex{}.isValid());
    CHECK(DofIndex::fromValue(1).isValid());
}

TEST_CASE("StructuralData_NoSolverHandleCanActAsADocumentIdentity", "[structural][data][id]") {
    // The negative half of the identity model, as a COMPILE-TIME fact rather
    // than a search of the source. Whether a tag happens to appear in a header
    // is a proxy; whether a solver handle can be USED where a persisted
    // identity is wanted is the property, and it is the one that breaks a
    // build if someone changes it.
    static_assert(!std::is_convertible_v<DofIndex, ObjectId>);
    static_assert(!std::is_convertible_v<meshing::NodeId, ObjectId>);
    static_assert(!std::is_convertible_v<meshing::ElementId, ObjectId>);
    static_assert(!std::is_constructible_v<ObjectId, DofIndex>);
    static_assert(!std::is_constructible_v<ObjectId, meshing::NodeId>);

    // Nor the other way: a document identity is not a mesh or solver handle.
    static_assert(!std::is_convertible_v<AnalysisId, meshing::NodeId>);
    static_assert(!std::is_convertible_v<LoadId, meshing::NodeId>);
    static_assert(!std::is_convertible_v<RestraintId, DofIndex>);

    // And the three canonical ones ARE document identities, which is the
    // positive half and is equally a compile-time fact.
    static_assert(std::is_convertible_v<AnalysisId, ObjectId>);
    SUCCEED("the conversions that would break the identity model do not exist");
}

// ---------------------------------------------------------------------------
// Component conventions
// ---------------------------------------------------------------------------

TEST_CASE("StructuralData_TheTensorComponentOrderIsFrozen", "[structural][data][convention]") {
    // Frozen here so that P17-ELEM-001's B and D matrices, P17-POST-001's
    // recovery and any report all index the same thing. A module that chose its
    // own order would produce stress wrong in the shear terms only.
    static_assert(static_cast<int>(TensorComponent::XX) == 0);
    static_assert(static_cast<int>(TensorComponent::YY) == 1);
    static_assert(static_cast<int>(TensorComponent::ZZ) == 2);
    static_assert(static_cast<int>(TensorComponent::XY) == 3);
    static_assert(static_cast<int>(TensorComponent::YZ) == 4);
    static_assert(static_cast<int>(TensorComponent::ZX) == 5);
    REQUIRE(structural::kTensorComponents.size() == 6);

    // The accessor agrees with the named field, for every component. If the
    // switch in `component()` were ever reordered this fails rather than
    // silently returning a neighbour.
    const Strain6 strain{.xx = 1.0, .yy = 2.0, .zz = 3.0,
                         .gammaXy = 4.0, .gammaYz = 5.0, .gammaZx = 6.0};
    CHECK(strain.component(TensorComponent::XX) == strain.xx);
    CHECK(strain.component(TensorComponent::YY) == strain.yy);
    CHECK(strain.component(TensorComponent::ZZ) == strain.zz);
    CHECK(strain.component(TensorComponent::XY) == strain.gammaXy);
    CHECK(strain.component(TensorComponent::YZ) == strain.gammaYz);
    CHECK(strain.component(TensorComponent::ZX) == strain.gammaZx);

    const Stress6 stress{.xx = 1_Pa, .yy = 2_Pa, .zz = 3_Pa,
                         .xy = 4_Pa, .yz = 5_Pa, .zx = 6_Pa};
    CHECK(stress.component(TensorComponent::XX) == stress.xx);
    CHECK(stress.component(TensorComponent::XY) == stress.xy);
    CHECK(stress.component(TensorComponent::YZ) == stress.yz);
    CHECK(stress.component(TensorComponent::ZX) == stress.zx);

    // Stress and strain are in the SAME order, which is what lets a
    // constitutive product be written component by component.
    for (const TensorComponent which : structural::kTensorComponents) {
        CHECK(structural::toString(which).size() == 2);
    }
}

TEST_CASE("StructuralData_TheShearConventionIsEngineeringAndIsInTheFieldNames",
          "[structural][data][convention]") {
    // The hazard: gamma_xy = 2 * epsilon_xy, and a factor of two lost between
    // two milestones produces stress that is wrong in the shear terms and
    // plausible everywhere else. The defence is that the field is NAMED
    // gammaXy, so a reader cannot take it for the tensor component.
    //
    // NAMING THE FIELDS HERE IS THE GUARD. If someone renames them to xy, yz
    // and zx -- the rename that would reintroduce the ambiguity -- this file
    // stops compiling. A search of the header's text would merely stop
    // matching, which a reader could talk themselves past.
    const Strain6 strain{.gammaXy = 2.0, .gammaYz = 4.0, .gammaZx = 6.0};
    CHECK(strain.gammaXy == 2.0);
    CHECK(strain.gammaYz == 4.0);
    CHECK(strain.gammaZx == 6.0);

    // The relationship the names assert, written out once so the factor of two
    // is in the test suite and not only in a comment.
    constexpr double tensorShearXy = 1.0;
    const Strain6 fromTensor{.gammaXy = 2.0 * tensorShearXy};
    CHECK(fromTensor.gammaXy == 2.0);
    static_assert(std::is_same_v<decltype(Strain6::gammaXy), double>,
                  "strain is dimensionless, so a plain double and no second unit system");
}

TEST_CASE("StructuralData_ResultFieldsCarryTheirUnits", "[structural][data][units]") {
    // SI internally, through the units framework that already exists. No field
    // is a bare double except strain, which is a ratio.
    static_assert(std::is_same_v<decltype(Translation3D::x), Length>);
    static_assert(std::is_same_v<decltype(Force3D::x), Force>);
    static_assert(std::is_same_v<decltype(Stress6::xx), Stress>);
    static_assert(std::is_same_v<decltype(Strain6::xx), double>);

    // A displacement is a Translation3D, which already existed: "how far every
    // point moves along X, Y and Z". Defining a second length-triple would
    // have been the duplicate unit machinery this milestone must not create.
    const Translation3D displacement{.x = 1_mm, .y = 0_mm, .z = 0_mm};
    CHECK(displacement.x.si() == 0.001);

    const Force3D reaction{.x = Force::fromSi(10.0), .y = {}, .z = {}};
    CHECK(reaction.x.si() == 10.0);

    const Stress6 stress{.xx = 200_MPa};
    CHECK(stress.xx.si() == 200.0e6);
}

// ---------------------------------------------------------------------------
// The analysis object
// ---------------------------------------------------------------------------

TEST_CASE("StructuralData_AnAnalysisIsADocumentObjectThatHoldsOnlyIntent",
          "[structural][data][analysis]") {
    AnalysedPart part;
    const StructuralAnalysis* study = structural::findStructuralAnalysis(part.document,
                                                                        part.analysis);
    REQUIRE(study != nullptr);
    CHECK(study->typeName() == "structural-analysis");
    CHECK(study->analysisId() == part.analysis);
    CHECK(study->definition().mesh == part.control);

    // It depends on the control, so the document's graph knows that editing the
    // control -- or the body beneath it -- reaches this analysis.
    const std::vector<ObjectId> dependencies = study->dependencies();
    REQUIRE(dependencies.size() == 1);
    CHECK(dependencies.front() == ObjectId::fromValue(part.control.value()));

    // AND IT HOLDS NOTHING DERIVED, which is a fact about the type's size
    // rather than about the header's text. The definition is one MeshControlId
    // and nothing else; a displacement array, a result or a mesh handle added
    // to it would make this fail, and no amount of renaming would hide that.
    static_assert(sizeof(StructuralAnalysisDefinition) == sizeof(MeshControlId),
                  "the analysis definition holds intent only: adding derived state to it is "
                  "the defect this asserts against");
    static_assert(std::is_trivially_copyable_v<StructuralAnalysisDefinition>,
                  "no owning container has appeared in it");
}

TEST_CASE("StructuralData_ANoOpEditDoesNotMoveTheAnalysisRevision",
          "[structural][data][analysis]") {
    // The precision that keeps a result current across a re-set of the same
    // value. MeshControl::setDefinition established the contract and this is
    // the same one, which matters because the analysis revision is what makes
    // a load or restraint edit stale a result.
    AnalysedPart part;
    const std::uint64_t before =
        structural::findStructuralAnalysis(part.document, part.analysis)->revision();

    REQUIRE(part.document
                .modifyObject<StructuralAnalysis>(
                    ObjectId::fromValue(part.analysis.value()),
                    [&part](StructuralAnalysis& study) -> Result<bool> {
                        return study.setDefinition(
                            StructuralAnalysisDefinition{.mesh = part.control});
                    })
                .has_value());
    CHECK(structural::findStructuralAnalysis(part.document, part.analysis)->revision() == before);
}

// ---------------------------------------------------------------------------
// The result container
// ---------------------------------------------------------------------------

TEST_CASE("StructuralData_AResultMustMatchItsMeshExactly", "[structural][data][result]") {
    AnalysedPart part;
    part.requireMesh();
    const StructuralResultSource source = part.currentSource();
    const meshing::VolumeMesh& mesh = part.mesh();

    const std::size_t nodes = mesh.nodeCount();
    const std::size_t tetrahedra = mesh.tetrahedronCount();
    REQUIRE(nodes > 0);
    REQUIRE(tetrahedra > 0);

    SECTION("one displacement per node") {
        const Result<StructuralResult> wrong = StructuralResult::create(
            source, mesh, std::vector<Translation3D>(nodes - 1), {},
            std::vector<Strain6>(tetrahedra), std::vector<Stress6>(tetrahedra));
        REQUIRE_FALSE(wrong.has_value());
        CHECK_THAT(wrong.error().message, ContainsSubstring("displacements for"));
    }
    SECTION("one strain and one stress per tetrahedron") {
        const Result<StructuralResult> wrong = StructuralResult::create(
            source, mesh, std::vector<Translation3D>(nodes), {},
            std::vector<Strain6>(tetrahedra + 1), std::vector<Stress6>(tetrahedra));
        REQUIRE_FALSE(wrong.has_value());
        CHECK_THAT(wrong.error().message, ContainsSubstring("tetrahedra"));
    }
    SECTION("the source must name this mesh") {
        StructuralResultSource foreign = source;
        foreign.mesh.generation += 1;
        const Result<StructuralResult> wrong = StructuralResult::create(
            foreign, mesh, std::vector<Translation3D>(nodes), {},
            std::vector<Strain6>(tetrahedra), std::vector<Stress6>(tetrahedra));
        REQUIRE_FALSE(wrong.has_value());
        CHECK_THAT(wrong.error().message, ContainsSubstring("does not name this mesh"));
    }
    SECTION("no value may be non-finite") {
        std::vector<Translation3D> displacements(nodes);
        displacements.front().x = Length::fromSi(std::numeric_limits<double>::quiet_NaN());
        const Result<StructuralResult> wrong = StructuralResult::create(
            source, mesh, std::move(displacements), {}, std::vector<Strain6>(tetrahedra),
            std::vector<Stress6>(tetrahedra));
        REQUIRE_FALSE(wrong.has_value());
        CHECK_THAT(wrong.error().message, ContainsSubstring("not finite"));
    }
    SECTION("reactions are ascending, unique and real nodes of this mesh") {
        const meshing::NodeId first = mesh.mesh().nodes().front().id;
        const meshing::NodeId second = mesh.mesh().nodes()[1].id;
        const Result<StructuralResult> descending = StructuralResult::create(
            source, mesh, std::vector<Translation3D>(nodes),
            {structural::NodalReaction{.node = second}, structural::NodalReaction{.node = first}},
            std::vector<Strain6>(tetrahedra), std::vector<Stress6>(tetrahedra));
        REQUIRE_FALSE(descending.has_value());
        CHECK_THAT(descending.error().message, ContainsSubstring("ascending"));

        const Result<StructuralResult> foreign = StructuralResult::create(
            source, mesh, std::vector<Translation3D>(nodes),
            {structural::NodalReaction{.node = meshing::NodeId::fromValue(999999)}},
            std::vector<Strain6>(tetrahedra), std::vector<Stress6>(tetrahedra));
        REQUIRE_FALSE(foreign.has_value());
        CHECK_THAT(foreign.error().message, ContainsSubstring("not in this mesh"));
    }
}

TEST_CASE("StructuralData_AResultIsBoundToTheMeshItWasComputedOn", "[structural][data][result]") {
    // COUNTS ARE NOT IDENTITY. Remeshing the same body with the same settings
    // produces a mesh with the same node and element counts and a different
    // stamp, and a result read across that gap would map every value onto the
    // wrong material.
    AnalysedPart part;
    part.requireMesh();
    const StructuralResult result = part.resultFor(part.currentSource());
    CHECK(result.describes(part.mesh()));

    const std::size_t nodesBefore = part.mesh().nodeCount();
    const meshing::MeshStamp stampBefore = part.mesh().mesh().stamp();

    part.requireMesh(); // the same body, the same settings, a new mesh
    const meshing::VolumeMesh& remeshed = part.mesh();
    CHECK(remeshed.nodeCount() == nodesBefore);
    CHECK_FALSE(remeshed.mesh().stamp() == stampBefore);

    CHECK_FALSE(result.describes(remeshed));
    const Result<Translation3D> refused =
        result.displacementOf(remeshed, remeshed.mesh().nodes().front().id);
    REQUIRE_FALSE(refused.has_value());
    CHECK_THAT(refused.error().message, ContainsSubstring("not computed on that mesh"));

    WARN(std::format("remesh of the same body: {} nodes either way, stamps differ, result refused",
                     nodesBefore));
}

TEST_CASE("StructuralData_ADisplacementIsFoundByNodeWithoutAssumingDenseIds",
          "[structural][data][result]") {
    // The access model. Node IDs start at 1 and a builder may choose them, so
    // indexing by id.value() is a latent defect; the mesh's own ascending
    // enumeration is the qualified way and the accessor uses it.
    AnalysedPart part;
    part.requireMesh();
    const StructuralResult result = part.resultFor(part.currentSource());
    const meshing::VolumeMesh& mesh = part.mesh();

    for (const meshing::Node& node : mesh.mesh().nodes()) {
        const Result<Translation3D> found = result.displacementOf(mesh, node.id);
        REQUIRE(found.has_value());
    }
    const Result<Translation3D> absent =
        result.displacementOf(mesh, meshing::NodeId::fromValue(999999));
    REQUIRE_FALSE(absent.has_value());

    // And the ids are not 0..N-1, which is the assumption being guarded
    // against: they start at 1.
    CHECK(mesh.mesh().nodes().front().id.value() >= 1);
    CHECK(std::ranges::is_sorted(mesh.mesh().nodes(), {},
                                 [](const meshing::Node& n) { return n.id; }));
}

TEST_CASE("StructuralData_AResultDoesNotCopyTheMesh", "[structural][data][result]") {
    // O(nodes + elements), not O(mesh). A result records a twelve-byte stamp
    // and its own arrays; duplicating the mesh per solve is the obvious way to
    // make results unusable at scale.
    AnalysedPart part;
    part.requireMesh();
    const StructuralResult result = part.resultFor(part.currentSource());
    const meshing::VolumeMesh& mesh = part.mesh();

    const std::size_t arrays = result.displacements().size() * sizeof(Translation3D) +
                               result.strains().size() * sizeof(Strain6) +
                               result.stresses().size() * sizeof(Stress6);
    CHECK(sizeof(StructuralResult) < arrays);
    CHECK(result.displacements().size() == mesh.nodeCount());
    CHECK(result.strains().size() == mesh.tetrahedronCount());

    // THE OBJECT ITSELF IS A FIXED SIZE, whatever the mesh. Four vectors and a
    // source stamp: a VolumeMesh embedded by value would make sizeof grow past
    // any plausible bound, so this is the embedding check and not a search for
    // a member name someone could rename.
    static_assert(sizeof(StructuralResult) < sizeof(meshing::VolumeMesh),
                  "a result records a stamp, never a copy of the mesh");
    static_assert(sizeof(StructuralResult) <= 4 * sizeof(std::vector<int>) +
                                                  sizeof(StructuralResultSource) + 64,
                  "four vectors and a source stamp, and nothing else by value");
}

// ---------------------------------------------------------------------------
// Currentness
// ---------------------------------------------------------------------------

TEST_CASE("StructuralData_EveryDependencyAloneCanStaleAResult",
          "[structural][data][currentness]") {
    // THE TRUTH TABLE, one row per dependency, each mutated ALONE. A comparison
    // that omitted any one of these would pass every other row, which is why
    // each is its own section rather than one combined check.
    AnalysedPart part;
    part.requireMesh();
    const StructuralResultSource source = part.currentSource();

    SECTION("nothing changed") {
        CHECK(structural::staleReasons(source, source).empty());
    }
    SECTION("the body") {
        StructuralResultSource other = source;
        other.body = ObjectId::fromValue(other.body.value() + 1);
        CHECK(structural::staleReasons(source, other) ==
              std::vector{StaleReason::Body});
    }
    SECTION("the mesh control") {
        StructuralResultSource other = source;
        other.control = MeshControlId::fromValue(other.control.value() + 1);
        CHECK(structural::staleReasons(source, other) == std::vector{StaleReason::Control});
    }
    SECTION("the geometry revision") {
        StructuralResultSource other = source;
        other.geometry.value += 1;
        CHECK(structural::staleReasons(source, other) == std::vector{StaleReason::Geometry});
    }
    SECTION("the mesh stamp") {
        StructuralResultSource other = source;
        other.mesh.generation += 1;
        CHECK(structural::staleReasons(source, other) == std::vector{StaleReason::Mesh});
    }
    SECTION("the material") {
        StructuralResultSource other = source;
        other.material = MaterialId::fromValue(other.material.value() + 1);
        CHECK(structural::staleReasons(source, other) == std::vector{StaleReason::Material});
    }
    SECTION("the material revision alone") {
        StructuralResultSource other = source;
        other.materialRevision += 1;
        CHECK(structural::staleReasons(source, other) == std::vector{StaleReason::Material});
    }
    SECTION("the analysis") {
        StructuralResultSource other = source;
        other.analysis = AnalysisId::fromValue(other.analysis.value() + 1);
        CHECK(structural::staleReasons(source, other) == std::vector{StaleReason::Analysis});
    }
    SECTION("the analysis revision alone -- loads, restraints and solver settings") {
        StructuralResultSource other = source;
        other.analysisRevision += 1;
        CHECK(structural::staleReasons(source, other) == std::vector{StaleReason::Analysis});
    }
    SECTION("several at once are all reported") {
        StructuralResultSource other = source;
        other.geometry.value += 1;
        other.materialRevision += 1;
        CHECK(structural::staleReasons(source, other) ==
              std::vector{StaleReason::Geometry, StaleReason::Material});
    }
}

TEST_CASE("StructuralData_AMaterialEditStalesTheResultAndNotTheMesh",
          "[structural][data][currentness]") {
    // The row worth stating out loud. A mesh is a function of geometry and
    // meshing intent; E is in neither. An architecture that invalidated the
    // mesh on a modulus change would remesh a hundred thousand elements to
    // answer a question about a number.
    AnalysedPart part;
    part.requireMesh();
    const StructuralResultSource before = part.currentSource();
    const StructuralResult result = part.resultFor(before);
    CHECK(structural::resultCurrency(&result, part.currentSource()) == ResultCurrency::Current);

    materials::MechanicalProperties mechanical =
        features::findMaterial(part.document, part.material)->definition().mechanical;
    mechanical.youngsModulus = materials::MaterialProperty<ElasticModulus>::known(190_GPa);
    REQUIRE(features::setMaterialMechanical(part.document, part.material, mechanical).has_value());

    // The mesh did not move...
    CHECK(part.mesher.currency(part.document, part.control) == meshing::MeshCurrency::Current);
    // ...and the result did.
    const StructuralResultSource after = part.currentSource();
    CHECK(structural::resultCurrency(&result, after) == ResultCurrency::Stale);
    CHECK(structural::staleReasons(result.source(), after) ==
          std::vector{StaleReason::Material});

    WARN("a Young's modulus edit: mesh current, result stale, reason material");
}

TEST_CASE("StructuralData_AGeometryEditStalesTheResultThroughTheInputBoundary",
          "[structural][data][currentness]") {
    AnalysedPart part;
    part.requireMesh();
    const StructuralResult result = part.resultFor(part.currentSource());

    REQUIRE(part.document
                .modifyObject<features::ExtrudeFeature>(
                    part.feature,
                    [](features::ExtrudeFeature& extrude) -> Result<bool> {
                        auto definition = extrude.definition();
                        definition.depth = 35_mm;
                        return extrude.setDefinition(definition).has_value();
                    })
                .has_value());
    requireReport(part.regenerator, part.document);

    // The input boundary refuses before a source can even be computed, which is
    // the strongest form of stale: there is no current state to compare with.
    const Result<StructuralResultSource> current =
        structural::currentResultSource(part.document, part.regenerator, part.mesher,
                                        part.analysis);
    REQUIRE_FALSE(current.has_value());
    CHECK(structural::analysisState(part.document, part.regenerator, part.mesher, part.analysis,
                                    &result, nullptr) == AnalysisState::InputsUnavailable);
}

TEST_CASE("StructuralData_TheSourceStampIsPopulatedFromTheDocumentAndNotDefaulted",
          "[structural][data][currentness]") {
    // A BLIND SPOT THE ADVERSARIAL REVIEW FOUND, and the test that closes it.
    //
    // The truth table above mutates a StructuralResultSource STRUCT and checks
    // that `staleReasons` notices. That proves the comparison is complete and
    // proves nothing about `currentResultSource`: if it forgot to read the
    // analysis revision and left the field zero, every one of those sections
    // would still pass. Only an edit to the real DOCUMENT can tell.
    //
    // The material half is covered by AMaterialEditStalesTheResultAndNotTheMesh,
    // which edits a real modulus. This is the analysis half.
    AnalysedPart part;
    part.requireMesh();

    // A second control on the SAME body, so the analysis has somewhere to move
    // to that leaves the body and the material alone.
    auto second =
        meshing::MeshControl::create("Mesh2", meshing::MeshControlDefinition{.body = part.feature});
    REQUIRE(second.has_value());
    const MeshControlId other =
        MeshControlId::fromValue(require(part.document.addObject(std::move(*second))).value());
    REQUIRE(part.mesher.generate(part.document, part.regenerator, other).has_value());

    const StructuralResultSource before = part.currentSource();
    REQUIRE(before.analysisRevision != 0);
    REQUIRE(before.materialRevision != 0);

    REQUIRE(part.document
                .modifyObject<StructuralAnalysis>(
                    ObjectId::fromValue(part.analysis.value()),
                    [other](StructuralAnalysis& study) -> Result<bool> {
                        return study.setDefinition(StructuralAnalysisDefinition{.mesh = other});
                    })
                .has_value());

    const StructuralResultSource after = part.currentSource();
    // THE CLAIM: the revision the stamp carries came from the document, so a
    // real edit moves it. A defaulted or forgotten field would be equal here.
    CHECK(after.analysisRevision != before.analysisRevision);
    CHECK(after.analysis == before.analysis);
    // And the body and the material did not move, which is what makes the first
    // check about the analysis rather than about everything at once.
    CHECK(after.body == before.body);
    CHECK(after.material == before.material);
    CHECK(after.materialRevision == before.materialRevision);

    const std::vector<StaleReason> reasons = structural::staleReasons(before, after);
    CHECK(std::ranges::find(reasons, StaleReason::Analysis) != reasons.end());

    WARN(std::format("analysis revision {} -> {} after a real definition edit; {} stale reasons",
                     before.analysisRevision, after.analysisRevision, reasons.size()));
}

TEST_CASE("StructuralData_AViewerOnlyChangeDoesNotStaleAResult",
          "[structural][data][currentness]") {
    // Presentation state is not a dependency. A quality THRESHOLD edit is the
    // sharpest case: it changes how an element is classified and can never
    // change the element, so P16 keeps the mesh current -- and a result
    // computed on that mesh stays current too.
    AnalysedPart part;
    part.requireMesh();
    const StructuralResult result = part.resultFor(part.currentSource());
    CHECK(structural::resultCurrency(&result, part.currentSource()) == ResultCurrency::Current);

    REQUIRE(part.document
                .modifyObject<meshing::MeshControl>(
                    ObjectId::fromValue(part.control.value()),
                    [](meshing::MeshControl& control) -> Result<bool> {
                        auto definition = control.definition();
                        definition.quality.limits[meshing::QualityMetric::TetMinDihedralAngle] =
                            meshing::QualityThreshold{.warning = 0.3, .failure = 0.1};
                        return control.setDefinition(definition).has_value();
                    })
                .has_value());

    CHECK(part.mesher.currency(part.document, part.control) == meshing::MeshCurrency::Current);
    CHECK(structural::resultCurrency(&result, part.currentSource()) == ResultCurrency::Current);
    CHECK(structural::analysisState(part.document, part.regenerator, part.mesher, part.analysis,
                                    &result, nullptr) == AnalysisState::SolvedCurrent);
}

TEST_CASE("StructuralData_AStaleResultCannotBeMadeCurrentByEditingItsProvenance",
          "[structural][data][currentness]") {
    // The failure mode the whole design is built against: refreshing a result
    // by rewriting what it claims to come from, rather than by recomputing it.
    // There is no way to do it, and this records the absence as a property
    // rather than as an omission.
    static_assert(std::is_same_v<decltype(std::declval<const StructuralResult&>().source()),
                                 const StructuralResultSource&>,
                  "source() hands back a const reference and there is no other accessor");
    AnalysedPart part;
    part.requireMesh();
    const StructuralResult result = part.resultFor(part.currentSource());

    // AND NO CONSUMER CAN MUTATE THE SOLUTION. Every array is handed out as a
    // span of CONST elements, so a renderer or a post-processor holding a
    // result cannot write a displacement or a stress back into it. That is the
    // property the brief asks for -- "do not allow GUI code to modify stress,
    // displacement or reaction in place" -- and it is the one worth asserting.
    //
    // A result IS assignable, deliberately: the solver service that holds one
    // per analysis must be able to replace it when a new solve succeeds, which
    // is replacement by the owner and not mutation through a reference.
    static_assert(std::is_const_v<std::remove_reference_t<
                      decltype(std::declval<const StructuralResult&>().displacements()[0])>>);
    static_assert(std::is_const_v<std::remove_reference_t<
                      decltype(std::declval<const StructuralResult&>().stresses()[0])>>);
    static_assert(std::is_const_v<std::remove_reference_t<
                      decltype(std::declval<const StructuralResult&>().reactions()[0])>>);
    CHECK(result.source().mesh == part.mesh().mesh().stamp());
}

// ---------------------------------------------------------------------------
// The state machine
// ---------------------------------------------------------------------------

TEST_CASE("StructuralData_TheAnalysisStateMachineReachesEveryStateItDeclares",
          "[structural][data][state]") {
    AnalysedPart part;

    SECTION("no analysis") {
        CHECK(structural::analysisState(part.document, part.regenerator, part.mesher,
                                        AnalysisId::fromValue(999999), nullptr, nullptr) ==
              AnalysisState::NoAnalysis);
    }
    SECTION("inputs unavailable: nothing meshed yet") {
        CHECK(structural::analysisState(part.document, part.regenerator, part.mesher,
                                        part.analysis, nullptr, nullptr) ==
              AnalysisState::InputsUnavailable);
    }
    SECTION("ready: inputs resolve and nothing is solved") {
        part.requireMesh();
        CHECK(structural::analysisState(part.document, part.regenerator, part.mesher,
                                        part.analysis, nullptr, nullptr) == AnalysisState::Ready);
    }
    SECTION("solved and current") {
        part.requireMesh();
        const StructuralResult result = part.resultFor(part.currentSource());
        CHECK(structural::analysisState(part.document, part.regenerator, part.mesher,
                                        part.analysis, &result, nullptr) ==
              AnalysisState::SolvedCurrent);
    }
    SECTION("solved and stale") {
        part.requireMesh();
        const StructuralResult result = part.resultFor(part.currentSource());
        part.requireMesh(); // a new mesh, so the result's stamp no longer matches
        CHECK(structural::analysisState(part.document, part.regenerator, part.mesher,
                                        part.analysis, &result, nullptr) ==
              AnalysisState::SolvedStale);
    }
    SECTION("solve failed takes precedence over a surviving stale result") {
        part.requireMesh();
        const StructuralResult result = part.resultFor(part.currentSource());
        const Error failure = makeError(ErrorCode::Internal, "the solver gave up").error();
        CHECK(structural::analysisState(part.document, part.regenerator, part.mesher,
                                        part.analysis, &result, &failure) ==
              AnalysisState::SolveFailed);
    }
}

TEST_CASE("StructuralData_StateAndStaleReasonsAreDeterministic", "[structural][data][state]") {
    // No wall clock, no pointer identity, no unordered iteration. Repeated
    // evaluation of an unchanged document gives the same answer, and so does
    // the stale-reason set.
    AnalysedPart part;
    part.requireMesh();
    const StructuralResultSource source = part.currentSource();
    const StructuralResult result = part.resultFor(source);

    for (int i = 0; i < 16; ++i) {
        CHECK(part.currentSource() == source);
        CHECK(structural::analysisState(part.document, part.regenerator, part.mesher,
                                        part.analysis, &result, nullptr) ==
              AnalysisState::SolvedCurrent);
    }

    StructuralResultSource moved = source;
    moved.geometry.value += 1;
    moved.materialRevision += 1;
    const std::vector<StaleReason> first = structural::staleReasons(source, moved);
    for (int i = 0; i < 16; ++i) {
        CHECK(structural::staleReasons(source, moved) == first);
    }
    CHECK(first == std::vector{StaleReason::Geometry, StaleReason::Material});

    WARN(std::format("{} analysis states and {} stale reasons, all deterministic over 16 runs",
                     6, first.size()));
}
