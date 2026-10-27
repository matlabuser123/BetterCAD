// P19-FLUID-001: resolving the fluid a CFD analysis consumes (ADR-062).
//
// THE FLUID IS P15's. These tests prove that CFD asks P15 -- the document's
// material, P15's requirement row for incompressible flow, P15's diagnostic --
// and holds nothing of its own: no default fluid, no second validation, no stored
// kinematic viscosity. And that a fluid edit reaches a CFD result through the
// source record, and reaches nothing else.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"

#include <bettercad/cfd/CfdAnalysis.hpp>
#include <bettercad/cfd/CfdAnalysisObject.hpp>
#include <bettercad/cfd/CfdFluid.hpp>
#include <bettercad/cfd/CfdResult.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/materials/Completeness.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/materials/ThermalProperties.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;
using cfd::CfdFluid;
using cfd::FluidProblem;
using cfd::StaleReason;
using features::MaterialDefinition;
using materials::MaterialProperty;

namespace {

MaterialDefinition water() {
    MaterialDefinition definition;
    definition.designation = "Water";
    definition.mechanical.density = MaterialProperty<Density>::known(998_kg_per_m3);
    definition.thermal.dynamicViscosity = MaterialProperty<DynamicViscosity>::known(1.002_mPa_s);
    return definition;
}

/// A block whose single material is a fluid, a meshing control, and a channel
/// analysis: an inlet, an outlet that fixes the pressure, walls.
struct Channel {
    Document document{"Part"};
    features::Regenerator regenerator;
    meshing::Mesher mesher;
    ObjectId feature{};
    MeshControlId control{};
    MaterialId fluid{};
    CfdAnalysisId analysis{};

    explicit Channel(std::optional<MaterialDefinition> material = water()) {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        const auto lines = addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
        const ObjectId profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Fluid", {.profile = SketchId::fromValue(profile.value()), .depth = 20_mm});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));
        auto intent = meshing::MeshControl::create("Mesh", meshing::MeshControlDefinition{.body = feature});
        REQUIRE(intent.has_value());
        control = MeshControlId::fromValue(require(document.addObject(std::move(*intent))).value());

        if (material.has_value()) {
            const Result<MaterialId> id = features::createMaterial(document, "Water", *material);
            REQUIRE(id.has_value());
            fluid = *id;
            REQUIRE(features::assignMaterial(document, fluid).has_value());
        }

        cfd::CfdAnalysisDefinition definition{.mesh = control};
        const FaceName start{feature, FaceSelector{.role = FaceRole::StartCap}};
        const FaceName end{feature, FaceSelector{.role = FaceRole::EndCap}};
        definition.boundaryConditions = {
            cfd::CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), cfd::VelocityInlet{start, 0.1_m_per_s}},
            cfd::CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(2), cfd::PressureOutlet{end, 0_Pa}},
        };
        for (std::size_t i = 0; i < lines.size(); ++i) {
            definition.boundaryConditions.push_back(cfd::CfdBoundaryCondition{
                CfdBoundaryConditionId::fromValue(3 + i),
                cfd::NoSlipWall{FaceName{feature, FaceSelector{.role = FaceRole::Side, .entity = lines[i]}}}});
        }
        auto study = cfd::CfdAnalysis::create("Flow", definition);
        REQUIRE(study.has_value());
        analysis = CfdAnalysisId::fromValue(require(document.addObject(std::move(*study))).value());
        requireReport(regenerator, document);
    }

    void requireMesh() {
        const Result<const meshing::VolumeMesh*> mesh = mesher.generate(document, regenerator, control);
        INFO((mesh.has_value() ? std::string{} : mesh.error().message));
        REQUIRE(mesh.has_value());
    }

    [[nodiscard]] cfd::CfdResultSource source() const {
        const Result<cfd::CfdResultSource> current = cfd::currentResultSource(document, regenerator, mesher, analysis);
        INFO((current.has_value() ? std::string{} : current.error().message));
        REQUIRE(current.has_value());
        return *current;
    }

    [[nodiscard]] cfd::CfdResult result() const {
        Result<cfd::CfdResult> made = cfd::CfdResult::create(
            source(), *mesher.mesh(control),
            cfd::CfdResultMetadata{.outerIterations = 10, .residuals = {}, .massBalance = {}});
        REQUIRE(made.has_value());
        return std::move(*made);
    }

    [[nodiscard]] std::uint64_t fluidRevision() const { return features::findMaterial(document, fluid)->revision(); }

    void setViscosity(DynamicViscosity mu) {
        materials::ThermalProperties thermal = features::findMaterial(document, fluid)->definition().thermal;
        thermal.dynamicViscosity = MaterialProperty<DynamicViscosity>::known(mu);
        REQUIRE(features::setMaterialThermal(document, fluid, thermal).has_value());
    }
};

[[nodiscard]] Result<CfdFluid> resolveIn(const Document& document) { return cfd::resolveCfdFluid(document); }

} // namespace

// ---------------------------------------------------------------------------
// The resolution
// ---------------------------------------------------------------------------

TEST_CASE("CfdFluid_ResolvesDensityAndViscosityThroughP15", "[cfd][fluid]") {
    Channel channel;
    const Result<CfdFluid> fluid = resolveIn(channel.document);
    INFO((fluid.has_value() ? std::string{} : fluid.error().message));
    REQUIRE(fluid.has_value());

    // P15's values, in SI, BY VALUE; and the identity and revision they came from.
    CHECK(fluid->id() == channel.fluid);
    CHECK(fluid->revision() == channel.fluidRevision());
    CHECK(fluid->revision() != 0);
    CHECK(fluid->density() == 998_kg_per_m3);
    CHECK(fluid->viscosity() == 1.002_mPa_s);
    CHECK(fluid->properties() == cfd::CfdFluidProperties{.density = 998_kg_per_m3, .viscosity = 1.002_mPa_s});

    // The same values P15's own requirement function hands over: CFD reads, it
    // does not re-derive.
    const auto p15 = features::requireIncompressibleFlowProperties(channel.document, channel.fluid);
    REQUIRE(p15.has_value());
    CHECK(fluid->density() == p15->density);
    CHECK(fluid->viscosity() == p15->dynamicViscosity);

    // nu = mu / rho, derived on demand, exactly the quotient.
    CHECK(fluid->kinematicViscosity() == (1.002_mPa_s) / (998_kg_per_m3));
    CHECK_THAT(fluid->kinematicViscosity().si(), WithinRel(1.002e-3 / 998.0, 1e-15));
    CHECK_FALSE(cfd::cfdFluidProblem(channel.document).has_value());

    WARN(std::format("water: rho = {} kg/m^3, mu = {} Pa s, nu = {} m^2/s (derived)", fluid->density().si(),
                     fluid->viscosity().si(), fluid->kinematicViscosity().si()));
}

TEST_CASE("CfdFluid_HoldsNothingButTheIdentityTheRevisionAndTheTwoValues", "[cfd][fluid]") {
    // No name, no provenance copy, no stored nu, no default: an id, a revision and
    // rho and mu. A field added here would be a second material record.
    struct Permitted {
        MaterialId id;
        std::uint64_t revision;
        cfd::CfdFluidProperties properties;
    };
    static_assert(sizeof(CfdFluid) == sizeof(Permitted));
    static_assert(sizeof(cfd::CfdFluidProperties) == sizeof(Density) + sizeof(DynamicViscosity));
    // Only the resolver makes one.
    static_assert(!std::is_default_constructible_v<CfdFluid>);
    static_assert(!std::is_constructible_v<CfdFluid, MaterialId, std::uint64_t, cfd::CfdFluidProperties>);
    static_assert(std::is_copy_constructible_v<CfdFluid>, "a resolved value, freely copied, never edited");
    // SI, explicit, in the types.
    static_assert(std::is_same_v<decltype(std::declval<const CfdFluid&>().density()), Density>);
    static_assert(std::is_same_v<decltype(std::declval<const CfdFluid&>().viscosity()), DynamicViscosity>);
    static_assert(std::is_same_v<decltype(std::declval<const CfdFluid&>().kinematicViscosity()), KinematicViscosity>);
    SUCCEED("a CfdFluid is a reference back to P15 and two values");
}

TEST_CASE("CfdFluid_RefusesAMissingFluidAndThereIsNoDefaultAir", "[cfd][fluid]") {
    // No material is not a solve with air or water. There is no fluid data in the
    // module to fall back to, and nothing is written to the document.
    Channel channel{std::nullopt};
    const std::size_t objects = channel.document.objectCount();
    const Result<CfdFluid> fluid = resolveIn(channel.document);
    REQUIRE_FALSE(fluid.has_value());
    CHECK_THAT(fluid.error().message, ContainsSubstring("no material assigned"));
    CHECK(cfd::cfdFluidProblem(channel.document) == FluidProblem::NoMaterialAssigned);
    CHECK(channel.document.objectCount() == objects);
    CHECK(features::materialCount(channel.document) == 0);

    // And the boundary says the same.
    channel.requireMesh();
    CHECK(cfd::cfdInputProblem(channel.document, channel.regenerator, channel.mesher, channel.control) ==
          cfd::InputProblem::NoMaterialAssigned);
}

TEST_CASE("CfdFluid_AFluidDeletedAfterASolveIsMissingNotRemembered", "[cfd][fluid]") {
    // The dangling case: a fluid is assigned, a result exists, and the material is
    // then deleted. CFD does not fall back to the values it last saw -- it holds
    // none -- so the fluid is missing, the inputs are unavailable, and the old
    // result is offered as nothing.
    Channel channel;
    channel.requireMesh();
    const cfd::CfdResult result = channel.result();
    REQUIRE(features::removeMaterial(channel.document, channel.fluid).has_value());

    const Result<CfdFluid> fluid = resolveIn(channel.document);
    REQUIRE_FALSE(fluid.has_value());
    CHECK(cfd::cfdFluidProblem(channel.document) == FluidProblem::NoMaterialAssigned);
    CHECK(cfd::cfdInputProblem(channel.document, channel.regenerator, channel.mesher, channel.control) ==
          cfd::InputProblem::NoMaterialAssigned);
    CHECK_FALSE(cfd::currentResultSource(channel.document, channel.regenerator, channel.mesher, channel.analysis)
                    .has_value());
    CHECK(cfd::analysisState(channel.document, channel.regenerator, channel.mesher, channel.analysis, &result,
                             nullptr) == cfd::AnalysisState::InputsUnavailable);
}

TEST_CASE("CfdFluid_RefusesAMaterialMissingEitherPropertyNamingEveryGap", "[cfd][fluid]") {
    MaterialDefinition densityOnly;
    densityOnly.mechanical.density = MaterialProperty<Density>::known(998_kg_per_m3);
    MaterialDefinition viscosityOnly;
    viscosityOnly.thermal.dynamicViscosity = MaterialProperty<DynamicViscosity>::known(1_mPa_s);

    SECTION("a density and no viscosity") {
        Channel channel{densityOnly};
        const Result<CfdFluid> fluid = resolveIn(channel.document);
        REQUIRE_FALSE(fluid.has_value());
        CHECK(cfd::cfdFluidProblem(channel.document) == FluidProblem::ViscosityUnavailable);
        CHECK_THAT(fluid.error().message, ContainsSubstring("has no dynamic viscosity, which incompressible flow needs"));
        channel.requireMesh();
        CHECK(cfd::cfdInputProblem(channel.document, channel.regenerator, channel.mesher, channel.control) ==
              cfd::InputProblem::FluidViscosityUnusable);
    }
    SECTION("a viscosity and no density") {
        Channel channel{viscosityOnly};
        CHECK(cfd::cfdFluidProblem(channel.document) == FluidProblem::DensityUnavailable);
        CHECK_THAT(resolveIn(channel.document).error().message, ContainsSubstring("has no density, which"));
        channel.requireMesh();
        CHECK(cfd::cfdInputProblem(channel.document, channel.regenerator, channel.mesher, channel.control) ==
              cfd::InputProblem::FluidDensityUnusable);
    }
    SECTION("neither: one diagnostic with both, categorised by the first") {
        Channel channel{MaterialDefinition{}};
        CHECK(cfd::cfdFluidProblem(channel.document) == FluidProblem::DensityUnavailable);
        CHECK_THAT(resolveIn(channel.document).error().message, ContainsSubstring("no density and no dynamic viscosity"));
    }
    SECTION("the problem and the resolution always agree") {
        for (const std::optional<MaterialDefinition>& material :
             {std::optional<MaterialDefinition>{water()}, std::optional<MaterialDefinition>{densityOnly},
              std::optional<MaterialDefinition>{viscosityOnly}, std::optional<MaterialDefinition>{MaterialDefinition{}},
              std::optional<MaterialDefinition>{}}) {
            Channel channel{material};
            CHECK(cfd::cfdFluidProblem(channel.document).has_value() == !resolveIn(channel.document).has_value());
        }
    }
}

TEST_CASE("CfdFluid_AnUnusableValueCannotReachTheResolution", "[cfd][fluid]") {
    // NaN, infinity, zero and negative viscosities are refused by P15 at ENTRY, so
    // no document can hold one for CFD to meet -- and CFD therefore has no check of
    // its own to duplicate. The resolution of the document is unchanged by each
    // refused edit.
    Channel channel;
    const Result<CfdFluid> before = resolveIn(channel.document);
    REQUIRE(before.has_value());
    for (const double bad : {0.0, -1.0e-3, std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity()}) {
        INFO("mu = " << bad);
        materials::ThermalProperties thermal = features::findMaterial(channel.document, channel.fluid)->definition().thermal;
        thermal.dynamicViscosity = MaterialProperty<DynamicViscosity>::known(DynamicViscosity::fromSi(bad));
        CHECK_FALSE(features::setMaterialThermal(channel.document, channel.fluid, thermal).has_value());
        const Result<CfdFluid> after = resolveIn(channel.document);
        REQUIRE(after.has_value());
        CHECK(*after == *before);
    }
}

TEST_CASE("CfdFluid_IsP15sCompletenessReportForIncompressibleFlow", "[cfd][fluid]") {
    MaterialDefinition densityOnly;
    densityOnly.mechanical.density = MaterialProperty<Density>::known(998_kg_per_m3);
    Channel channel{densityOnly};
    const Result<materials::CompletenessReport> report = cfd::cfdFluidCompleteness(channel.document);
    REQUIRE(report.has_value());
    // P15's own report, unreinterpreted.
    const Result<materials::CompletenessReport> p15 =
        features::effectiveMaterialCompleteness(channel.document, materials::ConsumerKind::IncompressibleFlow);
    REQUIRE(p15.has_value());
    CHECK(*report == *p15);
    CHECK(report->state == materials::CompletenessState::Incomplete);
    CHECK(report->missingMechanical.empty());
    CHECK(report->missingThermal == std::vector{materials::ThermalPropertyKind::DynamicViscosity});
    CHECK(cfd::kFluidConsumer == materials::ConsumerKind::IncompressibleFlow);
}

// ---------------------------------------------------------------------------
// What a fluid edit reaches, and what it does not
// ---------------------------------------------------------------------------

TEST_CASE("CfdFluid_AViscosityEditStalesTheResultAndNothingElse", "[cfd][fluid][currentness]") {
    Channel channel;
    channel.requireMesh();
    const cfd::CfdResult result = channel.result();
    const cfd::CfdResultSource before = result.source();
    const meshing::GeometryRevision geometry = meshing::geometryRevision(channel.document, channel.feature);

    channel.setViscosity(1.5_mPa_s);

    // The geometry did not move and the mesh is still current: a viscosity is in
    // neither, and an architecture that remeshed on a fluid edit would remesh to
    // answer a question about a number.
    CHECK(meshing::geometryRevision(channel.document, channel.feature) == geometry);
    CHECK(channel.mesher.currency(channel.document, channel.control) == meshing::MeshCurrency::Current);

    const cfd::CfdResultSource after = channel.source();
    CHECK(after.fluid == before.fluid);
    CHECK(after.fluidRevision != before.fluidRevision);
    CHECK(after.geometry == before.geometry);
    CHECK(after.mesh == before.mesh);
    CHECK(after.analysisRevision == before.analysisRevision);
    CHECK(after.definition == before.definition);
    CHECK(cfd::staleReasons(before, after) == std::vector{StaleReason::Fluid});
    CHECK(cfd::analysisState(channel.document, channel.regenerator, channel.mesher, channel.analysis, &result,
                             nullptr) == cfd::AnalysisState::SolvedStale);

    // And the fluid resolved now carries the new value and the new revision.
    const Result<CfdFluid> fluid = resolveIn(channel.document);
    REQUIRE(fluid.has_value());
    CHECK(fluid->viscosity() == 1.5_mPa_s);
    CHECK(fluid->revision() == after.fluidRevision);
}

TEST_CASE("CfdFluid_ReEnteringTheSameValueStalesNothing", "[cfd][fluid][currentness]") {
    // Not "every save stales": an edit that changes nothing is not an edit, so the
    // material's revision does not move and the result stays current.
    Channel channel;
    channel.requireMesh();
    const cfd::CfdResult result = channel.result();
    const std::uint64_t revision = channel.fluidRevision();

    const materials::ThermalProperties same = features::findMaterial(channel.document, channel.fluid)->definition().thermal;
    const Result<bool> edited = features::setMaterialThermal(channel.document, channel.fluid, same);
    REQUIRE(edited.has_value());
    CHECK_FALSE(*edited);
    CHECK(channel.fluidRevision() == revision);
    CHECK(cfd::resultCurrency(&result, channel.source()) == cfd::ResultCurrency::Current);
}

TEST_CASE("CfdFluid_AnEditOfAPropertyAFlowDoesNotReadStillMovesTheRevision", "[cfd][fluid][currentness]") {
    // RECORDED, AND CONSERVATIVE, as P18-MAT-001 recorded it for conduction: the
    // source tracks the MATERIAL's revision, so an edit to a property a flow does
    // not read (here a conductivity) also stales the result, with reason `fluid`.
    // Stale-when-unsure is the safe side; a per-property revision would be a
    // second revision scheme P15 does not have.
    Channel channel;
    channel.requireMesh();
    const cfd::CfdResult result = channel.result();
    materials::ThermalProperties thermal = features::findMaterial(channel.document, channel.fluid)->definition().thermal;
    thermal.thermalConductivity = MaterialProperty<ThermalConductivity>::known(0.6_W_per_m_K);
    REQUIRE(features::setMaterialThermal(channel.document, channel.fluid, thermal).has_value());
    CHECK(cfd::staleReasons(result.source(), channel.source()) == std::vector{StaleReason::Fluid});
    // The values a flow reads did not change.
    CHECK(resolveIn(channel.document)->properties() == cfd::CfdFluidProperties{998_kg_per_m3, 1.002_mPa_s});
}

TEST_CASE("CfdFluid_AssigningAnotherFluidStalesTheResultForTheFluid", "[cfd][fluid][currentness]") {
    Channel channel;
    channel.requireMesh();
    const cfd::CfdResult result = channel.result();

    MaterialDefinition oil;
    oil.designation = "Oil";
    oil.mechanical.density = MaterialProperty<Density>::known(870_kg_per_m3);
    oil.thermal.dynamicViscosity = MaterialProperty<DynamicViscosity>::known(DynamicViscosity::fromSi(0.03));
    const Result<MaterialId> id = features::createMaterial(channel.document, "Oil", oil);
    REQUIRE(id.has_value());
    REQUIRE(features::assignMaterial(channel.document, *id).has_value());

    const cfd::CfdResultSource now = channel.source();
    CHECK(now.fluid == *id);
    CHECK(cfd::staleReasons(result.source(), now) == std::vector{StaleReason::Fluid});
    CHECK(channel.mesher.currency(channel.document, channel.control) == meshing::MeshCurrency::Current);
    CHECK(resolveIn(channel.document)->viscosity() == DynamicViscosity::fromSi(0.03));
}

// ---------------------------------------------------------------------------
// Determinism and names
// ---------------------------------------------------------------------------

TEST_CASE("CfdFluid_ResolutionIsDeterministicAndReadOnly", "[cfd][fluid][determinism]") {
    Channel channel;
    const std::uint64_t revision = channel.fluidRevision();
    const std::size_t objects = channel.document.objectCount();
    const Result<CfdFluid> first = resolveIn(channel.document);
    const Result<CfdFluid> second = resolveIn(channel.document);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(*first == *second);
    CHECK(channel.fluidRevision() == revision);
    CHECK(channel.document.objectCount() == objects);

    // The same material at two revisions is two different fluids, even with equal
    // values, because the revision is part of the identity.
    channel.setViscosity(2_mPa_s);
    channel.setViscosity(1.002_mPa_s);
    const Result<CfdFluid> retyped = resolveIn(channel.document);
    REQUIRE(retyped.has_value());
    CHECK(retyped->properties() == first->properties());
    CHECK_FALSE(*retyped == *first);
}

TEST_CASE("CfdFluid_EveryFluidProblemHasADistinctName", "[cfd][fluid][names]") {
    CHECK(cfd::toString(FluidProblem::NoMaterialAssigned) == "no_material_assigned");
    CHECK(cfd::toString(FluidProblem::DensityUnavailable) == "density_unavailable");
    CHECK(cfd::toString(FluidProblem::ViscosityUnavailable) == "viscosity_unavailable");
    CHECK(cfd::toString(static_cast<FluidProblem>(3)) == "unknown");
    CHECK(cfd::toString(cfd::InputProblem::FluidViscosityUnusable) == "fluid_viscosity_unusable");
}
