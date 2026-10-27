// P19-ARCH-001 -- the boundary a CFD analysis is prepared at (ADR-060).
//
// Every refusal here is a refusal that already exists upstream: P16 knows when a
// mesh is stale and P15 knows when a material cannot serve. These tests prove the
// CFD module ASKS, in P16's order, and cannot be handed the answer. They are not
// physics tests: there is no cell, no face, no field and no solve in this
// milestone.
//
// THE THERMAL BOUNDARY IS THE CONTROL. The CFD boundary is a third copy of the
// device P17 and P18 qualified, so the strongest evidence that it delegates and
// does not reimplement is that, for the same document in the same state, it names
// the same reason as `requireThermalModel`. One case below does exactly that.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"

#include <bettercad/cfd/CfdAnalysis.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/materials/Completeness.hpp>
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
#include <bettercad/thermal/ThermalAnalysis.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <cstddef>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using cfd::InputProblem;

namespace {

/// A block with a meshing control: the smallest document a CFD analysis could
/// legitimately be prepared from, once a material is assigned. The block IS the
/// fluid region (ADR-058): which body is a fluid is a property of the material it
/// carries, not of its geometry.
struct FluidPart {
    Document document{"Part"};
    features::Regenerator regenerator;
    meshing::Mesher mesher;
    ObjectId feature{};
    MeshControlId control{};
    MaterialId material{};

    FluidPart() {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
        const ObjectId profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Fluid", {.profile = SketchId::fromValue(profile.value()), .depth = 20_mm});
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

    /// A material with a density and a dynamic viscosity: a fluid, as P15 can
    /// characterise one since P19-FLUID-001 (water near 20 degC).
    void assignFluid() {
        features::MaterialDefinition definition;
        definition.designation = "Water";
        definition.mechanical.density = materials::MaterialProperty<Density>::known(998_kg_per_m3);
        definition.thermal.dynamicViscosity = materials::MaterialProperty<DynamicViscosity>::known(1.002_mPa_s);
        assign(definition, "Water");
    }

    /// A material with a density and NO viscosity: what every fluid was before
    /// P19-FLUID-001, and now a material a flow refuses.
    void assignDensityWithoutViscosity() {
        features::MaterialDefinition definition;
        definition.designation = "Unflowing";
        definition.mechanical.density = materials::MaterialProperty<Density>::known(998_kg_per_m3);
        assign(definition, "Unflowing");
    }

    /// A material that resolves but has no density: mechanically uncharacterised.
    void assignMaterialWithoutDensity() {
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
        return cfd::cfdInputProblem(document, regenerator, mesher, control);
    }

    [[nodiscard]] Result<cfd::CfdModel> prepare() const {
        return cfd::requireCfdModel(document, regenerator, mesher, control);
    }

    void changeTheGeometry() {
        REQUIRE(document
                    .modifyObject<features::ExtrudeFeature>(
                        feature,
                        [](features::ExtrudeFeature& extrude) -> Result<bool> {
                            auto definition = extrude.definition();
                            definition.depth = 35_mm;
                            return extrude.setDefinition(definition).has_value();
                        })
                    .has_value());
        requireReport(regenerator, document);
    }
};

} // namespace

TEST_CASE("CfdInput_AcceptsACurrentMeshWithAUsableDensity", "[cfd][arch]") {
    FluidPart part;
    part.assignFluid();
    part.requireMesh();

    REQUIRE_FALSE(part.problem().has_value());
    const Result<cfd::CfdModel> model = part.prepare();
    INFO((model.has_value() ? std::string{} : model.error().message));
    REQUIRE(model.has_value());

    // Possession carries everything a later milestone needs, all of it taken from
    // ONE validated lookup so that a mesh and a mapping from different generations
    // cannot be combined.
    CHECK(model->body() == part.feature);
    CHECK(model->control() == part.control);
    CHECK(model->mesh().tetrahedronCount() > 0);
    CHECK(model->mesh().nodeCount() > 0);
    CHECK(model->mesh().boundaryTriangleCount() > 0);
    CHECK(model->map().report().complete());
    CHECK(model->map().meshStamp() == model->mesh().mesh().stamp());
    CHECK(model->quality().structurallyValid);

    // The density is P15's, resolved and not stored here, in SI; and the model names
    // the material it came from and the geometry it was validated against, so two
    // fluids of equal density, or two revisions of one body, are different sources.
    CHECK(model->density() == 998_kg_per_m3);
    CHECK(model->density().si() == 998.0);
    CHECK(model->materialId() == part.material);
    // And the viscosity (P19-FLUID-001), from the same resolution.
    CHECK(model->fluid().viscosity() == 1.002_mPa_s);
    CHECK(model->fluid().id() == part.material);
    CHECK(model->fluid().density() == model->density());
    CHECK(model->geometryRevision() == meshing::geometryRevision(part.document, part.feature));

    WARN(std::format("prepared: {} tets (= finite-volume cells), {} boundary triangles (= boundary "
                     "faces), {} nodes, rho = {}",
                     model->mesh().tetrahedronCount(), model->mesh().boundaryTriangleCount(),
                     model->mesh().nodeCount(), toString(model->density())));
}

TEST_CASE("CfdInput_RefusesADensityWithNoViscosity", "[cfd][arch]") {
    // P19-ARCH-001 LEFT A TRIPWIRE HERE, and P19-FLUID-001 tripped it on purpose.
    // The case used to accept a density with no viscosity "because no material can
    // carry one yet" and asserted there were seven consumers, so that the day a CFD
    // consumer appeared it would fail and the boundary would have to be taught to
    // ask. It was: P15 now has a dynamic viscosity and an IncompressibleFlow
    // consumer requiring { density } and { dynamic viscosity }, and this boundary
    // asks P15 for exactly that row (ADR-062).
    REQUIRE(materials::consumerKinds().size() == 8);
    REQUIRE(materials::consumerKinds().back() == materials::ConsumerKind::IncompressibleFlow);
    const materials::PropertyRequirement flow = materials::requiredProperties(materials::ConsumerKind::IncompressibleFlow);
    CHECK(flow.mechanical == std::vector{materials::MechanicalPropertyKind::Density});
    CHECK(flow.thermal == std::vector{materials::ThermalPropertyKind::DynamicViscosity});

    FluidPart part;
    part.assignDensityWithoutViscosity();
    part.requireMesh();
    CHECK(part.problem() == InputProblem::FluidViscosityUnusable);
    const Result<cfd::CfdModel> model = part.prepare();
    REQUIRE_FALSE(model.has_value());
    CHECK(model.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(model.error().message, ContainsSubstring("Unflowing"));
    CHECK_THAT(model.error().message, ContainsSubstring("no dynamic viscosity"));
    CHECK_THAT(model.error().message, ContainsSubstring("incompressible flow"));
    // The density is usable, so the diagnostic does not blame it.
    CHECK_THAT(model.error().message, !ContainsSubstring("no density"));
}

TEST_CASE("CfdInput_RefusesAControlTheDocumentDoesNotHave", "[cfd][arch]") {
    FluidPart part;
    part.assignFluid();
    part.requireMesh();

    const MeshControlId absent = MeshControlId::fromValue(part.control.value() + 1000);
    CHECK(cfd::cfdInputProblem(part.document, part.regenerator, part.mesher, absent) ==
          InputProblem::ControlNotFound);
    const Result<cfd::CfdModel> model =
        cfd::requireCfdModel(part.document, part.regenerator, part.mesher, absent);
    REQUIRE_FALSE(model.has_value());
    CHECK(model.error().code == ErrorCode::NotFound);
}

TEST_CASE("CfdInput_RefusesWhenNothingHasBeenMeshed", "[cfd][arch]") {
    FluidPart part;
    part.assignFluid();

    CHECK(part.problem() == InputProblem::NoMesh);
    REQUIRE_FALSE(part.prepare().has_value());
}

TEST_CASE("CfdInput_RefusesAMeshOfGeometryThatHasSinceChanged", "[cfd][arch]") {
    // THE SCENARIO P16 LEFT OPEN, and the reason this boundary exists. A mesh M1 is
    // generated from geometry G1; the geometry becomes G2; a caller still holds M1
    // and asks for an analysis. The mesh is internally perfectly valid -- it is
    // simply a mesh of a body that no longer exists in that form -- and every cell,
    // face area and flux a finite-volume solver derived from it would be a number
    // about the wrong body.
    FluidPart part;
    part.assignFluid();
    part.requireMesh();
    REQUIRE_FALSE(part.problem().has_value());

    const std::size_t tetrahedraOfG1 = part.mesher.mesh(part.control)->tetrahedronCount();

    // G1 -> G2, and the document is REGENERATED so that the geometry is current
    // again -- the sharp case. Nothing is wrong with the document; the only stale
    // thing is the mesh.
    part.changeTheGeometry();

    CHECK_FALSE(
        meshing::geometryIneligibility(part.document, part.regenerator, part.feature).has_value());
    REQUIRE(part.mesher.mesh(part.control) != nullptr);
    CHECK(part.mesher.mesh(part.control)->tetrahedronCount() == tetrahedraOfG1);

    // THE GATE.
    CHECK(part.mesher.currency(part.document, part.control) ==
          meshing::MeshCurrency::StaleGeometry);
    CHECK(part.problem() == InputProblem::MeshStale);
    const Result<cfd::CfdModel> model = part.prepare();
    REQUIRE_FALSE(model.has_value());
    CHECK(model.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(model.error().message, ContainsSubstring("geometry changed"));

    WARN(std::format("a mesh of changed geometry: {} tetrahedra still held and still valid, "
                     "currency {}, CFD analysis refused as {}",
                     tetrahedraOfG1,
                     meshing::toString(part.mesher.currency(part.document, part.control)),
                     cfd::toString(*part.problem())));
}

TEST_CASE("CfdInput_RefusesAMeshWhoseSizingIntentHasChanged", "[cfd][arch]") {
    FluidPart part;
    part.assignFluid();
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

TEST_CASE("CfdInput_RefusesAfterAFailedGenerationOfEligibleGeometry", "[cfd][arch]") {
    // A failed attempt is its own state: an older mesh may survive it, and reporting
    // that mesh as merely stale would hide that the replacement failed. The geometry
    // must stay ELIGIBLE for this case to exist, so the failure is forced by a
    // meshing request that cannot be honoured -- an unresolvable local sizing
    // reference -- exactly as the structural and thermal boundaries learned to do.
    FluidPart part;
    part.assignFluid();
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

    CHECK(part.mesher.currency(part.document, part.control) ==
          meshing::MeshCurrency::GenerationFailed);
    CHECK(part.problem() == InputProblem::MeshGenerationFailed);
    REQUIRE_FALSE(part.prepare().has_value());
}

TEST_CASE("CfdInput_RefusesAnIneligibleBodyBeforeLookingAtTheMesh", "[cfd][arch]") {
    // ORDERING IS PART OF THE CONTRACT. The body is deleted while a current mesh is
    // still held, so a boundary that asked the mesh first would report nothing wrong
    // with it.
    FluidPart part;
    part.assignFluid();
    part.requireMesh();
    REQUIRE_FALSE(part.problem().has_value());

    REQUIRE(part.document.removeObject(part.feature).has_value());
    REQUIRE(part.mesher.mesh(part.control) != nullptr);

    CHECK(part.problem() == InputProblem::GeometryIneligible);
    const Result<cfd::CfdModel> model = part.prepare();
    REQUIRE_FALSE(model.has_value());
    CHECK_THAT(model.error().message, ContainsSubstring("object_not_found"));
}

TEST_CASE("CfdInput_ReportsTheFirstFailureInTheDocumentedOrder", "[cfd][arch]") {
    // ORDER IS PART OF THE CONTRACT, and a single failing input cannot pin it: moving
    // the material check first would pass every case that has only one thing wrong.
    // Here two things are wrong at once, and the EARLIER check must be the one named.
    {
        // geometry (a deleted body) AND material (none): geometry first.
        FluidPart part;
        part.requireMesh();
        REQUIRE(part.document.removeObject(part.feature).has_value());
        CHECK(part.problem() == InputProblem::GeometryIneligible);
    }
    {
        // mesh (none yet) AND material (none): the mesh first.
        FluidPart part;
        CHECK(part.problem() == InputProblem::NoMesh);
    }
    {
        // mesh (stale) AND material (a density-less one): the mesh first.
        FluidPart part;
        part.assignFluid();
        part.requireMesh();
        part.assignMaterialWithoutDensity();
        part.changeTheGeometry();
        CHECK(part.problem() == InputProblem::MeshStale);
    }
    {
        // control (absent) AND everything else wrong: the control first.
        FluidPart part;
        const MeshControlId absent = MeshControlId::fromValue(part.control.value() + 1000);
        CHECK(cfd::cfdInputProblem(part.document, part.regenerator, part.mesher, absent) ==
              InputProblem::ControlNotFound);
    }
}

TEST_CASE("CfdInput_RefusesWhenTheDocumentNamesNoMaterial", "[cfd][arch]") {
    // No material is not a solve with air. ADR-028 forbids this module holding any
    // material data of its own, so there is nothing to fall back to -- which is the
    // point.
    FluidPart part;
    part.requireMesh();

    CHECK(part.problem() == InputProblem::NoMaterialAssigned);
    REQUIRE_FALSE(part.prepare().has_value());
}

TEST_CASE("CfdInput_RefusesAMaterialThatHasNoDensity", "[cfd][arch]") {
    // The material resolves; its density does not. P15 decides that and names the
    // missing INPUT, so a user knows what to supply.
    FluidPart part;
    part.assignMaterialWithoutDensity();
    part.requireMesh();

    CHECK(part.problem() == InputProblem::FluidDensityUnusable);
    const Result<cfd::CfdModel> model = part.prepare();
    REQUIRE_FALSE(model.has_value());
    CHECK_THAT(model.error().message, ContainsSubstring("density"));
    CHECK_THAT(model.error().message, ContainsSubstring("Unmeasured"));
    // This material has no viscosity either, and P15 names EVERY gap at once; the
    // category is the first in P15's order, the density.
    CHECK_THAT(model.error().message, ContainsSubstring("no density and no dynamic viscosity"));
}

TEST_CASE("CfdInput_NamesTheSameReasonAsTheThermalBoundaryForTheSameDocument", "[cfd][arch][delegation]") {
    // THE CONTROL EXPERIMENT. The CFD boundary delegates the geometry and mesh checks
    // to P16, as the thermal one does. If it reimplemented them, some state would make
    // the two disagree; walking the same document through every mesh and geometry
    // state both boundaries can see, they must name the same reason, step for step.
    FluidPart part;
    // Steady conduction needs only a conductivity; give the one fluid material a conductivity as well as a
    // density so the thermal boundary can be asked about the very same document.
    {
        features::MaterialDefinition definition;
        definition.designation = "Water";
        definition.mechanical.density = materials::MaterialProperty<Density>::known(998_kg_per_m3);
        definition.thermal.thermalConductivity =
            materials::MaterialProperty<ThermalConductivity>::known(0.6_W_per_m_K);
        definition.thermal.dynamicViscosity = materials::MaterialProperty<DynamicViscosity>::known(1.002_mPa_s);
        part.assign(definition, "Water");
    }

    const auto both = [&part]() {
        const std::optional<InputProblem> c = part.problem();
        const auto t = thermal::thermalInputProblem(part.document, part.regenerator, part.mesher, part.control);
        INFO("cfd " << (c ? std::string{cfd::toString(*c)} : std::string{"none"}) << ", thermal "
                    << (t ? std::string{thermal::toString(*t)} : std::string{"none"}));
        REQUIRE(c.has_value() == t.has_value());
        if (c.has_value()) {
            CHECK(cfd::toString(*c) == thermal::toString(*t));
        }
    };

    both();                    // nothing meshed
    part.requireMesh();
    both();                    // current
    part.changeTheGeometry();
    both();                    // stale geometry, regenerated
    part.requireMesh();
    both();                    // current again
    REQUIRE(part.document.removeObject(part.feature).has_value());
    both();                    // body gone

    // And the structural boundary, the other qualified copy of the device: wherever the
    // CFD boundary refuses for a geometry or mesh reason, structural names the same
    // one. (Structural also needs E and nu, which this fluid does not carry, so it may
    // refuse for a MATERIAL reason where CFD accepts; that is the only difference
    // allowed, and it is not compared.)
    const auto structuralAgrees = [&part]() {
        const std::optional<InputProblem> c = part.problem();
        const auto s = structural::structuralInputProblem(part.document, part.regenerator, part.mesher, part.control);
        if (c.has_value() && *c != InputProblem::NoMaterialAssigned && *c != InputProblem::FluidDensityUnusable &&
            *c != InputProblem::FluidViscosityUnusable) {
            INFO("cfd " << cfd::toString(*c));
            REQUIRE(s.has_value());
            CHECK(cfd::toString(*c) == structural::toString(*s));
        }
    };
    FluidPart other;
    other.assignFluid();
    structuralAgrees();        // (the first part, body gone)
    other.requireMesh();
    {
        const std::optional<InputProblem> c = other.problem();
        CHECK_FALSE(c.has_value());
    }
    other.changeTheGeometry();
    {
        const std::optional<InputProblem> c = other.problem();
        const auto s = structural::structuralInputProblem(other.document, other.regenerator, other.mesher, other.control);
        REQUIRE(c.has_value());
        REQUIRE(s.has_value());
        CHECK(cfd::toString(*c) == structural::toString(*s));
    }
    FluidPart unmeshed;
    unmeshed.assignFluid();
    {
        const auto s = structural::structuralInputProblem(unmeshed.document, unmeshed.regenerator, unmeshed.mesher, unmeshed.control);
        REQUIRE(unmeshed.problem().has_value());
        REQUIRE(s.has_value());
        CHECK(cfd::toString(*unmeshed.problem()) == structural::toString(*s));
    }
}

TEST_CASE("CfdInput_PossessionOfACfdModelIsTheOnlyProof", "[cfd][arch][compile]") {
    // ADR-060: `CfdModel` has no public constructor and one friend, so holding one
    // proves `requireCfdModel` accepted the input. Made a build-time fact: it cannot
    // be default-made, copied (a copy would be a second proof of the same moment), or
    // assembled from its parts by a caller.
    using cfd::CfdModel;
    STATIC_REQUIRE_FALSE(std::is_default_constructible_v<CfdModel>);
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<CfdModel>);
    STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<CfdModel>);
    STATIC_REQUIRE(std::is_nothrow_move_constructible_v<CfdModel>);
    STATIC_REQUIRE_FALSE(std::is_constructible_v<CfdModel, ObjectId, MeshControlId, const meshing::VolumeMesh&,
                                                 const meshing::GeometryMeshMap&, const meshing::MeshQualityReport&,
                                                 cfd::CfdFluid, meshing::GeometryRevision>);
    // Nor can its fluid be made except by resolving one (P19-FLUID-001).
    STATIC_REQUIRE_FALSE(std::is_default_constructible_v<cfd::CfdFluid>);
    STATIC_REQUIRE_FALSE(std::is_constructible_v<cfd::CfdFluid, MaterialId, std::uint64_t, cfd::CfdFluidProperties>);
    SUCCEED("the proof of an accepted input cannot be forged, copied or made from nothing");
}

TEST_CASE("CfdInput_ReachesEveryInputProblemItDeclares", "[cfd][arch]") {
    // The enum has no value nothing can return. Each case above reaches one, and this
    // records the roster so that adding a value without a case is visible.
    const std::array<InputProblem, 8> declared{
        InputProblem::ControlNotFound,
        InputProblem::GeometryIneligible,
        InputProblem::NoMesh,
        InputProblem::MeshStale,
        InputProblem::MeshGenerationFailed,
        InputProblem::NoMaterialAssigned,
        InputProblem::FluidDensityUnusable,
        InputProblem::FluidViscosityUnusable,
    };
    for (const InputProblem problem : declared) {
        CHECK_FALSE(cfd::toString(problem).empty());
        CHECK(cfd::toString(problem) != "unknown");
    }
    // THE ROSTER IS TIED TO THE ENUM. The enumerators are consecutive from 0, so the
    // value one past the last must have no name; an eighth enumerator that is given a
    // name makes this fail until it is added above and given a case of its own.
    for (std::size_t i = 0; i < declared.size(); ++i) {
        CHECK(declared[i] == static_cast<InputProblem>(i));
    }
    CHECK(cfd::toString(static_cast<InputProblem>(declared.size())) == "unknown");
    WARN(std::format("{} input problems declared, every one reached by a case in this file",
                     declared.size()));
}
