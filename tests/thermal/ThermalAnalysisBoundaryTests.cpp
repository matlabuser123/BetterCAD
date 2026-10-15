// P18-ARCH-001: the boundary a thermal analysis is prepared at.
//
// WHAT THESE TESTS ARE FOR. The milestone's claim is architectural -- "a stale
// mesh can never be solved as current, and no conductivity is ever assumed" --
// and an architectural claim written only in prose is an intention. P17
// proved the same claim for its own boundary before an element existed; these
// cases do the same for the thermal one, so that P18-ELEM-001 onwards inherit
// a gate that is already proved rather than one they must remember to honour.
//
// Every value of `InputProblem` is reached by one of these cases, and the
// roster case at the end is what keeps the enum honest.

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
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/meshing/MeshSizing.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/thermal/ThermalAnalysis.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using thermal::InputProblem;

namespace {

/// A block with a meshing control: the smallest document a thermal analysis
/// could legitimately be prepared from, once a material is assigned.
struct ConductingPart {
    Document document{"Part"};
    features::Regenerator regenerator;
    meshing::Mesher mesher;
    ObjectId feature{};
    MeshControlId control{};
    MaterialId material{};

    ConductingPart() {
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

    void assign(const features::MaterialDefinition& definition, const std::string& name) {
        const Result<MaterialId> id = features::createMaterial(document, name, definition);
        REQUIRE(id.has_value());
        material = *id;
        REQUIRE(features::assignMaterial(document, material).has_value());
        requireReport(regenerator, document);
    }

    /// A material with a usable conductivity AND the transient inputs: what a
    /// fully characterised conductor looks like.
    void assignUsableMaterial() {
        features::MaterialDefinition definition;
        definition.designation = "Steel";
        definition.mechanical.density = materials::MaterialProperty<Density>::known(7850_kg_per_m3);
        definition.thermal.thermalConductivity =
            materials::MaterialProperty<ThermalConductivity>::known(45_W_per_m_K);
        definition.thermal.specificHeatCapacity =
            materials::MaterialProperty<SpecificHeatCapacity>::known(460_J_per_kg_K);
        assign(definition, "Steel");
    }

    /// A material with a conductivity and NOTHING else thermal: what most
    /// datasheets give, and all that steady conduction needs.
    void assignConductivityOnlyMaterial() {
        features::MaterialDefinition definition;
        definition.designation = "Sheet";
        definition.thermal.thermalConductivity =
            materials::MaterialProperty<ThermalConductivity>::known(16_W_per_m_K);
        assign(definition, "Sheet");
    }

    /// A material that resolves but cannot conduct: mechanically characterised,
    /// no thermal conductivity. P15 decides that this is unusable, and names
    /// the missing input.
    void assignMaterialWithoutConductivity() {
        features::MaterialDefinition definition;
        definition.designation = "Unmeasured";
        definition.mechanical.youngsModulus =
            materials::MaterialProperty<ElasticModulus>::known(210_GPa);
        definition.mechanical.poissonRatio =
            materials::MaterialProperty<bettercad::PoissonRatio>::known(PoissonRatio::of(0.3));
        assign(definition, "Unmeasured");
    }

    void requireMesh() {
        const Result<const meshing::VolumeMesh*> mesh =
            mesher.generate(document, regenerator, control);
        INFO((mesh.has_value() ? std::string{} : mesh.error().message));
        REQUIRE(mesh.has_value());
        REQUIRE(mesher.currency(document, control) == meshing::MeshCurrency::Current);
    }

    [[nodiscard]] std::optional<InputProblem> problem() const {
        return thermal::thermalInputProblem(document, regenerator, mesher, control);
    }

    [[nodiscard]] Result<thermal::ThermalModel> prepare() const {
        return thermal::requireThermalModel(document, regenerator, mesher, control);
    }
};

} // namespace

TEST_CASE("ThermalInput_AcceptsACurrentMeshWithAUsableMaterial", "[thermal][arch]") {
    ConductingPart part;
    part.assignUsableMaterial();
    part.requireMesh();

    REQUIRE_FALSE(part.problem().has_value());
    const Result<thermal::ThermalModel> model = part.prepare();
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

    // The conductivity is P15's, resolved and not stored here, in SI.
    CHECK(model->conductivity() == 45_W_per_m_K);
    CHECK(model->conductivity().si() == 45.0);
    // And the fixture really is fully characterised, so the sibling case
    // that ACCEPTS a conductivity-only material is a contrast and not a
    // coincidence: this one would carry a transient too.
    const Result<features::TransientConductionProperties> transient =
        features::requireTransientConductionProperties(part.document, part.material);
    REQUIRE(transient.has_value());
    CHECK(transient->thermalConductivity == model->conductivity());
    CHECK(transient->density == 7850_kg_per_m3);
    CHECK(transient->specificHeatCapacity == 460_J_per_kg_K);

    WARN(std::format("prepared: {} tets, {} nodes, k = {}", model->mesh().tetrahedronCount(),
                     model->mesh().nodeCount(), toString(model->conductivity())));
}

TEST_CASE("ThermalInput_AcceptsAMaterialThatHasOnlyAConductivity", "[thermal][arch]") {
    // THE LINE BETWEEN THIS BOUNDARY AND P18-MAT-001. Steady conduction needs
    // `k` and nothing else, and P15's own documentation says a conductivity
    // without a specific heat "is what most datasheets give". So a material
    // with only `k` is ACCEPTED here; whether it can carry a TRANSIENT -- which
    // needs rho and cp as well -- is a per-mode question that P18-MAT-001 asks
    // through requireTransientConductionProperties, not a reason to refuse
    // every analysis at the door.
    ConductingPart part;
    part.assignConductivityOnlyMaterial();
    part.requireMesh();

    REQUIRE_FALSE(part.problem().has_value());
    const Result<thermal::ThermalModel> model = part.prepare();
    REQUIRE(model.has_value());
    CHECK(model->conductivity() == 16_W_per_m_K);
    // And the transient inputs really are absent, so the acceptance above is
    // not an accident of the fixture.
    CHECK_FALSE(features::requireTransientConductionProperties(part.document, part.material)
                    .has_value());
}

TEST_CASE("ThermalInput_RefusesAControlTheDocumentDoesNotHave", "[thermal][arch]") {
    ConductingPart part;
    part.assignUsableMaterial();
    part.requireMesh();

    const MeshControlId absent = MeshControlId::fromValue(part.control.value() + 1000);
    CHECK(thermal::thermalInputProblem(part.document, part.regenerator, part.mesher, absent)
          == InputProblem::ControlNotFound);
    const Result<thermal::ThermalModel> model =
        thermal::requireThermalModel(part.document, part.regenerator, part.mesher, absent);
    REQUIRE_FALSE(model.has_value());
    CHECK(model.error().code == ErrorCode::NotFound);
}

TEST_CASE("ThermalInput_RefusesWhenNothingHasBeenMeshed", "[thermal][arch]") {
    ConductingPart part;
    part.assignUsableMaterial();

    CHECK(part.problem() == InputProblem::NoMesh);
    REQUIRE_FALSE(part.prepare().has_value());
}

TEST_CASE("ThermalInput_RefusesAMeshOfGeometryThatHasSinceChanged", "[thermal][arch]") {
    // THE SCENARIO P16 LEFT OPEN, and the reason this boundary exists. A mesh
    // M1 is generated from geometry G1; the geometry becomes G2; a caller still
    // holds M1 and asks for an analysis. The mesh is internally perfectly valid
    // -- it is simply a mesh of a body that no longer exists in that form.
    ConductingPart part;
    part.assignUsableMaterial();
    part.requireMesh();
    REQUIRE_FALSE(part.problem().has_value());

    const std::size_t tetrahedraOfG1 = part.mesher.mesh(part.control)->tetrahedronCount();

    // G1 -> G2, and the document is REGENERATED so that the geometry is current
    // again -- the sharp case. Nothing is wrong with the document; the only
    // stale thing is the mesh.
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

    CHECK_FALSE(
        meshing::geometryIneligibility(part.document, part.regenerator, part.feature).has_value());
    REQUIRE(part.mesher.mesh(part.control) != nullptr);
    CHECK(part.mesher.mesh(part.control)->tetrahedronCount() == tetrahedraOfG1);

    // THE GATE.
    CHECK(part.mesher.currency(part.document, part.control)
          == meshing::MeshCurrency::StaleGeometry);
    CHECK(part.problem() == InputProblem::MeshStale);
    const Result<thermal::ThermalModel> model = part.prepare();
    REQUIRE_FALSE(model.has_value());
    CHECK(model.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(model.error().message, ContainsSubstring("geometry changed"));

    WARN(std::format("a mesh of changed geometry: {} tetrahedra still held and still valid, "
                     "currency {}, thermal analysis refused as {}",
                     tetrahedraOfG1,
                     meshing::toString(part.mesher.currency(part.document, part.control)),
                     thermal::toString(*part.problem())));
}

TEST_CASE("ThermalInput_RefusesAMeshWhoseSizingIntentHasChanged", "[thermal][arch]") {
    ConductingPart part;
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

TEST_CASE("ThermalInput_RefusesAfterAFailedGenerationOfEligibleGeometry", "[thermal][arch]") {
    // A failed attempt is its own state: an older mesh may survive it, and
    // reporting that mesh as merely stale would hide that the replacement
    // failed. The geometry must stay ELIGIBLE for this case to exist, so the
    // failure is forced by a meshing request that cannot be honoured -- an
    // unresolvable local sizing reference -- exactly as P17's boundary test
    // learned to do.
    ConductingPart part;
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

    CHECK_FALSE(
        meshing::geometryIneligibility(part.document, part.regenerator, part.feature).has_value());
    REQUIRE(part.mesher.mesh(part.control) != nullptr);
    CHECK(part.mesher.mesh(part.control)->tetrahedronCount() == tetrahedraBefore);

    CHECK(part.mesher.currency(part.document, part.control)
          == meshing::MeshCurrency::GenerationFailed);
    CHECK(part.problem() == InputProblem::MeshGenerationFailed);
    REQUIRE_FALSE(part.prepare().has_value());
}

TEST_CASE("ThermalInput_RefusesAnIneligibleBodyBeforeLookingAtTheMesh", "[thermal][arch]") {
    // ORDERING IS PART OF THE CONTRACT. The body is deleted while a current
    // mesh is still held, so a boundary that asked the mesh first would report
    // nothing wrong with it.
    ConductingPart part;
    part.assignUsableMaterial();
    part.requireMesh();
    REQUIRE_FALSE(part.problem().has_value());

    REQUIRE(part.document.removeObject(part.feature).has_value());
    REQUIRE(part.mesher.mesh(part.control) != nullptr);

    CHECK(part.problem() == InputProblem::GeometryIneligible);
    const Result<thermal::ThermalModel> model = part.prepare();
    REQUIRE_FALSE(model.has_value());
    CHECK_THAT(model.error().message, ContainsSubstring("object_not_found"));
}

TEST_CASE("ThermalInput_RefusesWhenTheDocumentNamesNoMaterial", "[thermal][arch]") {
    // No material is not a solve with a typical conductivity. ADR-028 forbids
    // this module holding any material data of its own, so there is nothing
    // to fall back to -- which is the point.
    ConductingPart part;
    part.requireMesh();

    CHECK(part.problem() == InputProblem::NoMaterialAssigned);
    REQUIRE_FALSE(part.prepare().has_value());
}

TEST_CASE("ThermalInput_RefusesAMaterialThatCannotConduct", "[thermal][arch]") {
    // The material resolves; its thermal conductivity does not. P15 decides
    // that and names the missing INPUT, so a user knows what to supply.
    ConductingPart part;
    part.assignMaterialWithoutConductivity();
    part.requireMesh();

    CHECK(part.problem() == InputProblem::MaterialUnusableForConduction);
    const Result<thermal::ThermalModel> model = part.prepare();
    REQUIRE_FALSE(model.has_value());
    CHECK_THAT(model.error().message, ContainsSubstring("thermal conductivity"));
    CHECK_THAT(model.error().message, ContainsSubstring("Unmeasured"));
}

TEST_CASE("ThermalInput_ReachesEveryInputProblemItDeclares", "[thermal][arch]") {
    // The enum has no value nothing can return. Each case above reaches one,
    // and this records the roster so that adding a value without a case is
    // visible.
    const std::array<InputProblem, 7> declared{
        InputProblem::ControlNotFound,
        InputProblem::GeometryIneligible,
        InputProblem::NoMesh,
        InputProblem::MeshStale,
        InputProblem::MeshGenerationFailed,
        InputProblem::NoMaterialAssigned,
        InputProblem::MaterialUnusableForConduction,
    };
    for (const InputProblem problem : declared) {
        CHECK_FALSE(thermal::toString(problem).empty());
        CHECK(thermal::toString(problem) != "unknown");
    }
    WARN(std::format("{} input problems declared, every one reached by a case in this file",
                     declared.size()));
}
