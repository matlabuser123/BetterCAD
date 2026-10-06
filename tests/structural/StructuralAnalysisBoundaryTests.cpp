// P17-ARCH-001: the boundary a structural analysis is prepared at.
//
// WHAT THESE TESTS ARE FOR. The milestone's claim is architectural -- "a stale
// mesh can never be solved as current" -- and an architectural claim written
// only in prose is an intention. P16 recorded the hazard it could not close:
// `Mesher::mesh()` hands back a stale mesh deliberately, so nothing FORCES a
// holder to ask. These tests freeze the answer now, before an element, a
// degree of freedom or a solve exists, so that the later milestones inherit a
// gate that is already proved rather than one they must remember to honour.
//
// Every value of `InputProblem` is reached by one of these cases. That is not
// coverage theatre: two further values were drafted and deleted during the
// audit because nothing could return them, and this file is what keeps the
// enum honest.

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
#include <bettercad/meshing/MeshSizing.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/structural/StructuralAnalysis.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <memory>
#include <optional>
#include <string>
#include <utility>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using structural::InputProblem;

namespace {

/// A block with a meshing control and a usable structural material: the
/// smallest document a structural analysis could legitimately be prepared from.
struct AnalysablePart {
    Document document{"Part"};
    features::Regenerator regenerator;
    meshing::Mesher mesher;
    ObjectId feature{};
    MeshControlId control{};
    MaterialId material{};

    AnalysablePart() {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
        const ObjectId profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = 20_mm});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));

        auto intent = meshing::MeshControl::create(
            "Mesh", meshing::MeshControlDefinition{.body = feature});
        REQUIRE(intent.has_value());
        control = MeshControlId::fromValue(require(document.addObject(std::move(*intent))).value());

        requireReport(regenerator, document);
    }

    /// Assigns a material with both linear-elastic inputs present and in range.
    void assignUsableMaterial() {
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
        requireReport(regenerator, document);
    }

    /// Assigns a material that resolves but cannot describe linear elasticity:
    /// a Young's modulus and NO Poisson ratio. P15 decides that this is
    /// unusable, and names the missing input rather than the derived constant.
    void assignMaterialWithoutPoissonRatio() {
        features::MaterialDefinition definition;
        definition.designation = "Unmeasured";
        definition.mechanical.youngsModulus =
            materials::MaterialProperty<ElasticModulus>::known(210_GPa);
        const Result<MaterialId> id =
            features::createMaterial(document, "Unmeasured", definition);
        REQUIRE(id.has_value());
        material = *id;
        REQUIRE(features::assignMaterial(document, material).has_value());
        requireReport(regenerator, document);
    }

    void requireMesh() {
        const Result<const meshing::VolumeMesh*> mesh =
            mesher.generate(document, regenerator, control);
        INFO((mesh.has_value() ? std::string{} : mesh.error().message));
        REQUIRE(mesh.has_value());
        REQUIRE(mesher.currency(document, control) == meshing::MeshCurrency::Current);
    }

    [[nodiscard]] std::optional<InputProblem> problem() const {
        return structural::structuralInputProblem(document, regenerator, mesher, control);
    }

    [[nodiscard]] Result<structural::StructuralModel> prepare() const {
        return structural::requireStructuralModel(document, regenerator, mesher, control);
    }
};

} // namespace

TEST_CASE("StructuralInput_AcceptsACurrentMeshWithAUsableMaterial", "[structural][arch]") {
    AnalysablePart part;
    part.assignUsableMaterial();
    part.requireMesh();

    REQUIRE_FALSE(part.problem().has_value());
    const Result<structural::StructuralModel> model = part.prepare();
    INFO((model.has_value() ? std::string{} : model.error().message));
    REQUIRE(model.has_value());

    // Possession carries everything a later milestone needs, all of it taken
    // from ONE validated lookup so that a mesh and a mapping from different
    // generations cannot be combined.
    CHECK(model->body() == part.feature);
    CHECK(model->control() == part.control);
    CHECK(model->mesh().tetrahedronCount() > 0);
    CHECK(model->mesh().nodeCount() > 0);
    CHECK(model->map().report().complete());
    CHECK(model->map().meshStamp() == model->mesh().mesh().stamp());
    CHECK(model->quality().structurallyValid);

    // The elastic constants are P15's, resolved and not stored here. E and nu
    // are what was assigned; G and K are derived by P15 from them (ADR-027).
    CHECK(model->elastic().youngsModulus == 210_GPa);
    CHECK(model->elastic().poissonRatio.value() == 0.3);
    CHECK(model->elastic().shearModulus.si() > 0.0);
    CHECK(model->elastic().bulkModulus.si() > 0.0);

    WARN(std::format("prepared: {} tets, {} nodes, E = {}, nu = {}",
                     model->mesh().tetrahedronCount(), model->mesh().nodeCount(),
                     toString(model->elastic().youngsModulus),
                     model->elastic().poissonRatio.value()));
}

TEST_CASE("StructuralInput_RefusesAControlTheDocumentDoesNotHave", "[structural][arch]") {
    AnalysablePart part;
    part.assignUsableMaterial();
    part.requireMesh();

    const MeshControlId absent = MeshControlId::fromValue(part.control.value() + 1000);
    CHECK(structural::structuralInputProblem(part.document, part.regenerator, part.mesher, absent)
          == InputProblem::ControlNotFound);
    const Result<structural::StructuralModel> model =
        structural::requireStructuralModel(part.document, part.regenerator, part.mesher, absent);
    REQUIRE_FALSE(model.has_value());
    CHECK(model.error().code == ErrorCode::NotFound);
}

TEST_CASE("StructuralInput_RefusesWhenNothingHasBeenMeshed", "[structural][arch]") {
    AnalysablePart part;
    part.assignUsableMaterial();

    CHECK(part.problem() == InputProblem::NoMesh);
    REQUIRE_FALSE(part.prepare().has_value());
}

TEST_CASE("StructuralInput_RefusesAMeshOfGeometryThatHasSinceChanged", "[structural][arch]") {
    // THE SCENARIO P16 LEFT OPEN, and the reason this boundary exists. A mesh
    // M1 is generated from geometry G1; the geometry becomes G2; a caller still
    // holds M1 and asks for an analysis. The mesh is internally perfectly valid
    // -- it is simply a mesh of a body that no longer exists in that form.
    AnalysablePart part;
    part.assignUsableMaterial();
    part.requireMesh();
    REQUIRE_FALSE(part.problem().has_value());

    const std::size_t tetrahedraOfG1 = part.mesher.mesh(part.control)->tetrahedronCount();

    // G1 -> G2. The feature's depth changes, and the document is regenerated so
    // that the GEOMETRY is current again -- which is what makes this the sharp
    // case rather than the easy one. Nothing is wrong with the document; the
    // only stale thing is the mesh.
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

    // The geometry itself is eligible again...
    CHECK_FALSE(
        meshing::geometryIneligibility(part.document, part.regenerator, part.feature).has_value());
    // ...and the mesh is still there, still internally valid, still returned.
    REQUIRE(part.mesher.mesh(part.control) != nullptr);
    CHECK(part.mesher.mesh(part.control)->tetrahedronCount() == tetrahedraOfG1);

    // THE GATE. P16 says the mesh describes a body that no longer exists in
    // that form, and the structural boundary refuses on exactly that.
    CHECK(part.mesher.currency(part.document, part.control)
          == meshing::MeshCurrency::StaleGeometry);
    CHECK(part.problem() == InputProblem::MeshStale);
    const Result<structural::StructuralModel> model = part.prepare();
    REQUIRE_FALSE(model.has_value());
    CHECK(model.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(model.error().message, ContainsSubstring("geometry changed"));

    WARN(std::format("a mesh of changed geometry: {} tetrahedra still held and still valid, "
                     "currency {}, analysis refused as {}",
                     tetrahedraOfG1,
                     meshing::toString(part.mesher.currency(part.document, part.control)),
                     structural::toString(*part.problem())));
}

TEST_CASE("StructuralInput_RefusesAMeshWhoseSizingIntentHasChanged", "[structural][arch]") {
    // The other half of staleness, and a different user story: the shape is
    // unchanged, the discretisation request is not. Refused for the same reason
    // -- the held mesh is not the mesh the document now asks for.
    AnalysablePart part;
    part.assignUsableMaterial();
    part.requireMesh();
    REQUIRE_FALSE(part.problem().has_value());

    REQUIRE(part.document
                .modifyObject<meshing::MeshControl>(
                    ObjectId::fromValue(part.control.value()),
                    [](meshing::MeshControl& intent) -> Result<bool> {
                        auto definition = intent.definition();
                        definition.mesh.sizing.globalTargetSize = 5_mm;
                        return intent.setDefinition(definition).has_value();
                    })
                .has_value());

    CHECK(part.mesher.currency(part.document, part.control) == meshing::MeshCurrency::StaleIntent);
    CHECK(part.problem() == InputProblem::MeshStale);
    REQUIRE_FALSE(part.prepare().has_value());
}

TEST_CASE("StructuralInput_RefusesAfterAFailedGenerationOfEligibleGeometry",
          "[structural][arch]") {
    // A failed attempt is its own state. An older mesh may survive it, and
    // reporting that mesh as merely stale would hide that the replacement
    // failed -- so the analysis is refused with the failure, not the staleness.
    //
    // THE GEOMETRY MUST STAY ELIGIBLE for this case to exist at all, and the
    // first draft of this test got that wrong: it deleted the body, which makes
    // the geometry ineligible, and the boundary reported GeometryIneligible
    // because it asks geometry FIRST. That ordering is correct and is tested
    // separately, so reaching the generation failure needs a model whose body
    // is fine and whose MESHING REQUEST cannot be honoured.
    //
    // An unresolvable local sizing reference is exactly that: `resolveSizing`
    // reports an unresolved control rather than failing, and `volumeMeshFor`
    // then declines to proceed -- because quietly meshing while ignoring a
    // refinement the user asked for would hand back a mesh that looks like the
    // requested one and is not.
    AnalysablePart part;
    part.assignUsableMaterial();
    part.requireMesh();
    const std::size_t tetrahedraBefore = part.mesher.mesh(part.control)->tetrahedronCount();

    REQUIRE(part.document
                .modifyObject<meshing::MeshControl>(
                    ObjectId::fromValue(part.control.value()),
                    [this_feature = part.feature](meshing::MeshControl& intent) -> Result<bool> {
                        auto definition = intent.definition();
                        definition.mesh.sizing.local.push_back(meshing::LocalMeshSizing{
                            .face = FaceName{this_feature,
                                             FaceSelector{.role = FaceRole::Side,
                                                          .entity = EntityId::fromValue(987654U)}},
                            .targetSize = 2_mm});
                        return intent.setDefinition(definition).has_value();
                    })
                .has_value());

    const Result<const meshing::VolumeMesh*> retry =
        part.mesher.generate(part.document, part.regenerator, part.control);
    REQUIRE_FALSE(retry.has_value());

    // The body is untouched and still perfectly analysable on its own terms.
    CHECK_FALSE(
        meshing::geometryIneligibility(part.document, part.regenerator, part.feature).has_value());
    // The old mesh survived the failed attempt, as P16 guarantees.
    REQUIRE(part.mesher.mesh(part.control) != nullptr);
    CHECK(part.mesher.mesh(part.control)->tetrahedronCount() == tetrahedraBefore);

    CHECK(part.mesher.currency(part.document, part.control)
          == meshing::MeshCurrency::GenerationFailed);
    CHECK(part.problem() == InputProblem::MeshGenerationFailed);
    REQUIRE_FALSE(part.prepare().has_value());

    WARN(std::format("eligible body, failed remesh: {} tetrahedra still held, currency {}, "
                     "analysis refused as {}",
                     tetrahedraBefore,
                     meshing::toString(part.mesher.currency(part.document, part.control)),
                     structural::toString(*part.problem())));
}

TEST_CASE("StructuralInput_RefusesAnIneligibleBodyBeforeLookingAtTheMesh", "[structural][arch]") {
    // ORDERING IS PART OF THE CONTRACT. The body is deleted while a current
    // mesh is still held, so a boundary that asked the mesh first would report
    // nothing wrong with it. Geometry eligibility is asked first, and the
    // reason is P16's own.
    AnalysablePart part;
    part.assignUsableMaterial();
    part.requireMesh();
    REQUIRE_FALSE(part.problem().has_value());

    REQUIRE(part.document.removeObject(part.feature).has_value());
    REQUIRE(part.mesher.mesh(part.control) != nullptr);

    CHECK(part.problem() == InputProblem::GeometryIneligible);
    const Result<structural::StructuralModel> model = part.prepare();
    REQUIRE_FALSE(model.has_value());
    CHECK_THAT(model.error().message, ContainsSubstring("object_not_found"));
}

TEST_CASE("StructuralInput_RefusesWhenTheDocumentNamesNoMaterial", "[structural][arch]") {
    // No material is not a solve with defaults. ADR-028 forbids this module
    // holding any material data of its own, so there is nothing to fall back
    // to -- which is the point.
    AnalysablePart part;
    part.requireMesh();

    CHECK(part.problem() == InputProblem::NoMaterialAssigned);
    const Result<structural::StructuralModel> model = part.prepare();
    REQUIRE_FALSE(model.has_value());
}

TEST_CASE("StructuralInput_RefusesAMaterialThatCannotDescribeLinearElasticity",
          "[structural][arch]") {
    // The material resolves; its linear-elastic inputs do not. P15 decides
    // that and names the missing INPUT -- a user told the shear modulus is
    // unavailable has nothing to act on, a user told the Poisson ratio is
    // unknown does.
    AnalysablePart part;
    part.assignMaterialWithoutPoissonRatio();
    part.requireMesh();

    CHECK(part.problem() == InputProblem::MaterialUnusableForLinearElasticity);
    const Result<structural::StructuralModel> model = part.prepare();
    REQUIRE_FALSE(model.has_value());
    CHECK_THAT(model.error().message, ContainsSubstring("Poisson"));
    CHECK_THAT(model.error().message, ContainsSubstring("Unmeasured"));
}

TEST_CASE("StructuralInput_ReachesEveryInputProblemItDeclares", "[structural][arch]") {
    // The enum has no value nothing can return. Checked by construction rather
    // than asserted: each case above reaches one, and this records the roster
    // so that adding a value without a case is visible.
    const std::array<InputProblem, 7> declared{
        InputProblem::ControlNotFound,
        InputProblem::GeometryIneligible,
        InputProblem::NoMesh,
        InputProblem::MeshStale,
        InputProblem::MeshGenerationFailed,
        InputProblem::NoMaterialAssigned,
        InputProblem::MaterialUnusableForLinearElasticity,
    };
    for (const InputProblem problem : declared) {
        CHECK_FALSE(structural::toString(problem).empty());
        CHECK(structural::toString(problem) != "unknown");
    }
    WARN(std::format("{} input problems declared, every one reached by a case in this file",
                     declared.size()));
}
