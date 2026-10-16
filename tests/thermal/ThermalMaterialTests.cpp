// P18-MAT-001: resolving the material a thermal analysis consumes.
//
// WHAT THESE TESTS ARE ACTUALLY FOR. The implementation is thin because the
// audit found P15 already owns every property, requirement and validation this
// milestone's checklist names. That makes the tests the substance rather than
// the garnish: they are what demonstrates the reuse is real and correct,
// instead of a claim that P15 "probably" handles it.
//
// So the boundary cases below -- k at zero, cp negative, NaN everywhere -- are
// run against the THERMAL resolver and against P15's entry points, and they
// pass because P15 refuses them. If a future change made P18 stop delegating,
// or made P15 stop validating, these fail.
//
// The mode-requirement cases are the heart of the milestone: a STEADY analysis
// needs k and nothing else, a TRANSIENT one needs k, rho and cp, and
// over-requiring is as much a defect as under-requiring. The invalidation cases
// are its hard requirement: a material edit leaves the P16 mesh CURRENT and
// makes the thermal result STALE.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"

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
#include <bettercad/thermal/ThermalAnalysis.hpp>
#include <bettercad/thermal/ThermalAnalysisObject.hpp>
#include <bettercad/thermal/ThermalData.hpp>
#include <bettercad/thermal/ThermalMaterial.hpp>
#include <bettercad/thermal/ThermalResult.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;
using materials::ConsumerKind;
using materials::MechanicalPropertyKind;
using materials::ThermalPropertyKind;
using thermal::AnalysisState;
using thermal::MaterialProblem;
using thermal::ResultCurrency;
using thermal::StaleReason;
using thermal::ThermalAnalysis;
using thermal::ThermalAnalysisDefinition;
using thermal::ThermalAnalysisMode;
using thermal::ThermalCapability;
using thermal::ThermalCapacity;
using thermal::ThermalMaterial;
using thermal::ThermalResult;
using thermal::ThermalResultSource;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

/// Distinctive, non-round values, so a swapped field or a stray unit factor
/// shows as a different number rather than a coincidence.
constexpr double kK = 42.75;      // W/(m K)
constexpr double kRho = 7853.25;  // kg/m^3
constexpr double kCp = 486.5;     // J/(kg K)

/// Thermal properties with the given k and cp, either or both absent.
[[nodiscard]] materials::ThermalProperties thermalWith(std::optional<ThermalConductivity> k,
                                                       std::optional<SpecificHeatCapacity> cp) {
    materials::ThermalProperties properties;
    if (k) {
        properties.thermalConductivity = materials::MaterialProperty<ThermalConductivity>::known(*k);
    }
    if (cp) {
        properties.specificHeatCapacity =
            materials::MaterialProperty<SpecificHeatCapacity>::known(*cp);
    }
    return properties;
}

/// The capacity pair of a resolved material, REQUIRED to be present first, so a
/// resolver that forgot it fails the test with an assertion rather than
/// aborting the process on an empty optional.
[[nodiscard]] ThermalCapacity capacityOf(const ThermalMaterial& material) {
    REQUIRE(material.capacity().has_value());
    return *material.capacity();
}

/// Mechanical properties with a density, or none.
[[nodiscard]] materials::MechanicalProperties mechanicalWith(std::optional<Density> rho) {
    materials::MechanicalProperties properties;
    if (rho) {
        properties.density = materials::MaterialProperty<Density>::known(*rho);
    }
    return properties;
}

/// A block with a mesh control and a thermal analysis, whose material the test
/// sets.
struct ThermalMaterialPart {
    Document document{"Part"};
    features::Regenerator regenerator;
    meshing::Mesher mesher;
    ObjectId feature{};
    MeshControlId control{};
    ThermalAnalysisId analysis{};
    MaterialId material{};

    explicit ThermalMaterialPart(ThermalAnalysisMode mode = ThermalAnalysisMode::Steady) {
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

        auto study = ThermalAnalysis::create("Study", definitionFor(mode));
        INFO((study.has_value() ? std::string{} : study.error().message));
        REQUIRE(study.has_value());
        analysis = ThermalAnalysisId::fromValue(require(document.addObject(std::move(*study))).value());
        requireReport(regenerator, document);
    }

    [[nodiscard]] ThermalAnalysisDefinition definitionFor(ThermalAnalysisMode mode) const {
        if (mode == ThermalAnalysisMode::Steady) {
            return ThermalAnalysisDefinition{.mesh = control};
        }
        return ThermalAnalysisDefinition{
            .mesh = control,
            .mode = ThermalAnalysisMode::Transient,
            .initialCondition = thermal::UniformInitialTemperature{.value = 293.15_K},
            .transient = thermal::TransientSettings{.timeStep = 0.5_s, .endTime = 10_s},
        };
    }

    /// Assigns a material with exactly the properties given.
    MaterialId assign(std::string name, const materials::MechanicalProperties& mechanical,
                      const materials::ThermalProperties& thermalProperties) {
        features::MaterialDefinition definition;
        definition.designation = name;
        definition.mechanical = mechanical;
        definition.thermal = thermalProperties;
        const Result<MaterialId> id = features::createMaterial(document, name, definition);
        INFO((id.has_value() ? std::string{} : id.error().message));
        REQUIRE(id.has_value());
        material = *id;
        REQUIRE(features::assignMaterial(document, material).has_value());
        requireReport(regenerator, document);
        return material;
    }

    /// The fully characterised conductor: k, rho and cp.
    MaterialId assignFull() {
        return assign("Steel", mechanicalWith(Density::fromSi(kRho)),
                      thermalWith(ThermalConductivity::fromSi(kK), SpecificHeatCapacity::fromSi(kCp)));
    }

    /// k only: what most datasheets give, and all steady conduction needs.
    MaterialId assignConductivityOnly() {
        return assign("Sheet", mechanicalWith(std::nullopt),
                      thermalWith(ThermalConductivity::fromSi(kK), std::nullopt));
    }

    [[nodiscard]] const features::Material& materialObject() const {
        const features::Material* object = features::findMaterial(document, material);
        REQUIRE(object != nullptr);
        return *object;
    }

    [[nodiscard]] Result<ThermalMaterial> resolve(ThermalAnalysisMode mode) const {
        return thermal::resolveThermalMaterial(document, feature, mode);
    }

    [[nodiscard]] std::optional<MaterialProblem> problem(ThermalAnalysisMode mode) const {
        return thermal::thermalMaterialProblem(document, feature, mode);
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

    [[nodiscard]] Result<ThermalResultSource> source() const {
        return thermal::currentResultSource(document, regenerator, mesher, analysis);
    }

    [[nodiscard]] ThermalResultSource currentSource() const {
        const Result<ThermalResultSource> current = source();
        INFO((current.has_value() ? std::string{} : current.error().message));
        REQUIRE(current.has_value());
        return *current;
    }

    [[nodiscard]] AnalysisState state(const ThermalResult* result) const {
        return thermal::analysisState(document, regenerator, mesher, analysis, result, nullptr);
    }

    /// A synthetic result of the right shape for the current mesh (P18-DATA-001's
    /// container; nothing is solved). Its only role here is to carry a source.
    [[nodiscard]] ThermalResult resultFor(const ThermalResultSource& current) const {
        const meshing::VolumeMesh& held = mesh();
        std::vector<Temperature> temperatures(held.nodeCount(), 293.15_K);
        std::vector<thermal::TemperatureGradient3> gradients(held.tetrahedronCount());
        std::vector<thermal::HeatFlux3> fluxes(held.tetrahedronCount());
        thermal::HeatBalance balance{};
        std::optional<Time> time;
        if (current.mode == ThermalAnalysisMode::Transient) {
            balance.storedEnergyRate = 0_W;
            time = 1_s;
        }
        Result<ThermalResult> result = ThermalResult::create(
            current, held, std::move(temperatures), std::move(gradients), std::move(fluxes), balance, time);
        INFO((result.has_value() ? std::string{} : result.error().message));
        REQUIRE(result.has_value());
        return std::move(*result);
    }

    [[nodiscard]] meshing::MeshControlDefinition meshingIntent() const {
        const auto* object = dynamic_cast<const meshing::MeshControl*>(
            document.findObject(ObjectId::fromValue(control.value())));
        REQUIRE(object != nullptr);
        return object->definition();
    }
};

} // namespace

// ---------------------------------------------------------------------------
// The consumer mapping, and what P15 says each one needs
// ---------------------------------------------------------------------------

TEST_CASE("ThermalMaterial_TheModeChoosesP15sConsumerAndNothingElseDecidesIt",
          "[thermal][material]") {
    // The requirement table is P15's and is READ, never restated. If P18 ever
    // grew a second copy, this is the test that would disagree with it.
    CHECK(thermal::consumerFor(ThermalAnalysisMode::Steady) == ConsumerKind::ThermalSteady);
    CHECK(thermal::consumerFor(ThermalAnalysisMode::Transient) == ConsumerKind::ThermalTransient);

    const materials::PropertyRequirement steady =
        materials::requiredProperties(thermal::consumerFor(ThermalAnalysisMode::Steady));
    CHECK(steady.mechanical.empty());
    CHECK(steady.thermal == std::vector{ThermalPropertyKind::ThermalConductivity});

    const materials::PropertyRequirement transient =
        materials::requiredProperties(thermal::consumerFor(ThermalAnalysisMode::Transient));
    CHECK(transient.mechanical == std::vector{MechanicalPropertyKind::Density});
    CHECK(transient.thermal == std::vector{ThermalPropertyKind::ThermalConductivity,
                                           ThermalPropertyKind::SpecificHeatCapacity});

    // Neither thermal consumer requires a structural property, and neither
    // structural consumer requires a thermal one: the two phases share a
    // material and not a requirement.
    for (const ConsumerKind structural : {ConsumerKind::FeaLinearStatic,
                                          ConsumerKind::FeaLinearStaticWithGravity}) {
        CHECK(materials::requiredProperties(structural).thermal.empty());
    }
    for (const MechanicalPropertyKind kind : transient.mechanical) {
        CHECK(kind != MechanicalPropertyKind::YoungsModulus);
        CHECK(kind != MechanicalPropertyKind::PoissonRatio);
    }
}

// ---------------------------------------------------------------------------
// Resolution, exact values, units
// ---------------------------------------------------------------------------

TEST_CASE("ThermalMaterial_ResolveSteadyConductivity", "[thermal][material]") {
    // k only: rho and cp absent. Steady resolution passes and carries exactly k.
    ThermalMaterialPart part;
    part.assignConductivityOnly();

    CHECK_FALSE(part.problem(ThermalAnalysisMode::Steady).has_value());
    const Result<ThermalMaterial> resolved = part.resolve(ThermalAnalysisMode::Steady);
    INFO((resolved.has_value() ? std::string{} : resolved.error().message));
    REQUIRE(resolved.has_value());
    CHECK(resolved->id() == part.material);
    CHECK(resolved->mode() == ThermalAnalysisMode::Steady);
    CHECK(resolved->conductivity().si() == kK);
    // NOT "unknown": the view holds no capacity because the mode consumes none.
    CHECK_FALSE(resolved->capacity().has_value());
}

TEST_CASE("ThermalMaterial_ResolveTransientProperties", "[thermal][material]") {
    // k, rho and cp, each a distinctive number, each landing in its own field.
    ThermalMaterialPart part;
    part.assignFull();

    CHECK_FALSE(part.problem(ThermalAnalysisMode::Transient).has_value());
    const Result<ThermalMaterial> resolved = part.resolve(ThermalAnalysisMode::Transient);
    INFO((resolved.has_value() ? std::string{} : resolved.error().message));
    REQUIRE(resolved.has_value());
    CHECK(resolved->mode() == ThermalAnalysisMode::Transient);
    CHECK(resolved->conductivity().si() == kK);
    REQUIRE(resolved->capacity().has_value());
    CHECK(capacityOf(*resolved).density.si() == kRho);
    CHECK(capacityOf(*resolved).specificHeatCapacity.si() == kCp);
    // THE FIELD-SWAP CHECK, made possible by distinct values: rho is not in cp
    // and cp is not in rho.
    CHECK(capacityOf(*resolved).density.si() != kCp);
    CHECK(capacityOf(*resolved).specificHeatCapacity.si() != kRho);

    // The same material resolved for steady leaves the capacity out -- the
    // view is what the analysis consumes, not what the material has.
    const Result<ThermalMaterial> steady = part.resolve(ThermalAnalysisMode::Steady);
    REQUIRE(steady.has_value());
    CHECK(steady->conductivity().si() == kK);
    CHECK_FALSE(steady->capacity().has_value());
    CHECK_FALSE(*steady == *resolved);
}

TEST_CASE("ThermalMaterial_VolumetricHeatCapacityIsDerivedNotStored", "[thermal][material][units]") {
    // THE INDEPENDENT DIMENSIONAL ARGUMENT, asserted in the type system rather
    // than taken from production code: [kg/m^3] x [J/(kg K)] = [J/(m^3 K)], so
    // a capacity term needs BOTH rho and cp, and the product has the dimension
    // of energy per volume per kelvin.
    static_assert(std::is_same_v<decltype(std::declval<Density>() * std::declval<SpecificHeatCapacity>()),
                                 thermal::VolumetricHeatCapacity>);
    static_assert(std::is_same_v<decltype(std::declval<Energy>() /
                                          (std::declval<Volume>() * std::declval<Temperature>())),
                                 thermal::VolumetricHeatCapacity>,
                  "rho cp is energy per volume per kelvin");
    static_assert(!std::is_same_v<thermal::VolumetricHeatCapacity, SpecificHeatCapacity>);
    static_assert(!std::is_same_v<thermal::VolumetricHeatCapacity, Density>);

    // Derived every time from the two it comes from; the pair is the whole
    // struct, so there is nowhere a stored product could hide.
    static_assert(sizeof(ThermalCapacity) == sizeof(Density) + sizeof(SpecificHeatCapacity),
                  "rho and cp, and no stored rho cp beside them");
    const ThermalCapacity capacity{.density = Density::fromSi(kRho),
                                   .specificHeatCapacity = SpecificHeatCapacity::fromSi(kCp)};
    CHECK_THAT(capacity.volumetric().si(), WithinRel(kRho * kCp, 1e-15));
    CHECK_THAT(capacity.volumetric().si(), WithinRel(3820606.125, 1e-12));
}

TEST_CASE("ThermalMaterial_UnitsAreCanonicalAndEquivalentInputsAgree", "[thermal][material][units]") {
    // SI internally, through the units framework. The three unit traps -- a
    // W/(mm K), a g/cm^3, a kJ/(kg K) -- are converted AT THE LITERAL and
    // nowhere in P18: 7.8 g/cm^3 IS 7800 kg/m^3 before it reaches a material,
    // and the resolver returns the canonical number unchanged.
    static_assert(std::is_same_v<decltype(std::declval<ThermalMaterial>().conductivity()),
                                 ThermalConductivity>);
    static_assert(std::is_same_v<decltype(ThermalCapacity::density), Density>);
    static_assert(std::is_same_v<decltype(ThermalCapacity::specificHeatCapacity), SpecificHeatCapacity>);
    static_assert(!std::is_convertible_v<double, ThermalConductivity>);
    static_assert(!std::is_convertible_v<ThermalConductivity, double>);

    ThermalMaterialPart part;
    part.assign("Plain", mechanicalWith(7800_kg_per_m3),
                thermalWith(45_W_per_m_K, 500_J_per_kg_K));
    const Result<ThermalMaterial> resolved = part.resolve(ThermalAnalysisMode::Transient);
    REQUIRE(resolved.has_value());
    CHECK(resolved->conductivity().si() == 45.0);
    CHECK(capacityOf(*resolved).density.si() == 7800.0);
    CHECK(capacityOf(*resolved).specificHeatCapacity.si() == 500.0);
    // Not 0.5 (a kJ misread), not 7.8 (a g/cm^3 misread), not 0.045 (a mm).
    CHECK(capacityOf(*resolved).specificHeatCapacity.si() != 0.5);
    CHECK(capacityOf(*resolved).density.si() != 7.8);
    CHECK(resolved->conductivity().si() != 0.045);

    // Equivalent inputs in other units agree exactly with the canonical ones.
    CHECK((0.5_kJ_per_kg_K).si() == (500_J_per_kg_K).si());
    CHECK_THAT((7.8_g_per_cm3).si(), WithinRel((7800_kg_per_m3).si(), 1e-15));
    CHECK((45_W_per_m_K).in(units::W_per_m_K) == 45.0);
}

TEST_CASE("ThermalMaterial_AcceptsAnyFinitePositiveValue", "[thermal][material]") {
    // No invented magnitude bounds. A very poor conductor and a very good one
    // are both materials; so are a very light and a very dense one.
    const auto accepts = [](double k, double rho, double cp) {
        ThermalMaterialPart part;
        part.assign("Odd", mechanicalWith(Density::fromSi(rho)),
                    thermalWith(ThermalConductivity::fromSi(k), SpecificHeatCapacity::fromSi(cp)));
        const Result<ThermalMaterial> resolved = part.resolve(ThermalAnalysisMode::Transient);
        INFO((resolved.has_value() ? std::string{} : resolved.error().message));
        REQUIRE(resolved.has_value());
        CHECK(resolved->conductivity().si() == k);
        CHECK(capacityOf(*resolved).density.si() == rho);
        CHECK(capacityOf(*resolved).specificHeatCapacity.si() == cp);
    };
    accepts(1.0e-9, 1.0e-6, 1.0e-6);
    accepts(1.0e9, 1.0e9, 1.0e9);
    accepts(std::nextafter(0.0, 1.0), std::nextafter(0.0, 1.0), std::nextafter(0.0, 1.0));
    accepts(std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
            std::numeric_limits<double>::max());
}

// ---------------------------------------------------------------------------
// What each mode requires, and what it must NOT require
// ---------------------------------------------------------------------------

TEST_CASE("ThermalMaterial_MissingConductivityFailsBothModes", "[thermal][material][requirements]") {
    ThermalMaterialPart part;
    part.assign("NoK", mechanicalWith(Density::fromSi(kRho)),
                thermalWith(std::nullopt, SpecificHeatCapacity::fromSi(kCp)));
    for (const ThermalAnalysisMode mode : {ThermalAnalysisMode::Steady, ThermalAnalysisMode::Transient}) {
        CAPTURE(thermal::toString(mode));
        CHECK(part.problem(mode) == MaterialProblem::RequiredPropertyUnavailable);
        const Result<ThermalMaterial> resolved = part.resolve(mode);
        REQUIRE_FALSE(resolved.has_value());
        // P15's own diagnostic, carried: it names the material and the
        // missing input. Never a zero conductivity, never a fallback.
        CHECK_THAT(resolved.error().message, ContainsSubstring("thermal conductivity"));
        CHECK_THAT(resolved.error().message, ContainsSubstring("NoK"));
        CHECK(resolved.error().code == ErrorCode::FailedPrecondition);
    }
}

TEST_CASE("ThermalMaterial_DensityIsRequiredOnlyByTransient", "[thermal][material][requirements]") {
    // k and cp present, rho absent.
    ThermalMaterialPart part;
    part.assign("NoRho", mechanicalWith(std::nullopt),
                thermalWith(ThermalConductivity::fromSi(kK), SpecificHeatCapacity::fromSi(kCp)));

    SECTION("steady resolves, and the diagnostic machinery says nothing about density") {
        CHECK_FALSE(part.problem(ThermalAnalysisMode::Steady).has_value());
        REQUIRE(part.resolve(ThermalAnalysisMode::Steady).has_value());
        const Result<materials::CompletenessReport> report =
            thermal::thermalMaterialCompleteness(part.document, ThermalAnalysisMode::Steady);
        REQUIRE(report.has_value());
        CHECK(report->ready());
        CHECK(report->missingMechanical.empty());
        CHECK(report->missingThermal.empty());
    }
    SECTION("transient refuses, naming the density and not the specific heat") {
        CHECK(part.problem(ThermalAnalysisMode::Transient) == MaterialProblem::RequiredPropertyUnavailable);
        const Result<ThermalMaterial> resolved = part.resolve(ThermalAnalysisMode::Transient);
        REQUIRE_FALSE(resolved.has_value());
        CHECK_THAT(resolved.error().message, ContainsSubstring("density"));
        CHECK_THAT(resolved.error().message, !ContainsSubstring("specific heat"));
        const Result<materials::CompletenessReport> report =
            thermal::thermalMaterialCompleteness(part.document, ThermalAnalysisMode::Transient);
        REQUIRE(report.has_value());
        CHECK_FALSE(report->ready());
        CHECK(report->missingMechanical == std::vector{MechanicalPropertyKind::Density});
        CHECK(report->missingThermal.empty());
    }
}

TEST_CASE("ThermalMaterial_SpecificHeatIsRequiredOnlyByTransient", "[thermal][material][requirements]") {
    // k and rho present, cp absent.
    ThermalMaterialPart part;
    part.assign("NoCp", mechanicalWith(Density::fromSi(kRho)),
                thermalWith(ThermalConductivity::fromSi(kK), std::nullopt));

    SECTION("steady resolves") {
        CHECK_FALSE(part.problem(ThermalAnalysisMode::Steady).has_value());
        const Result<ThermalMaterial> resolved = part.resolve(ThermalAnalysisMode::Steady);
        REQUIRE(resolved.has_value());
        CHECK(resolved->conductivity().si() == kK);
    }
    SECTION("transient refuses, naming the specific heat and not the density") {
        CHECK(part.problem(ThermalAnalysisMode::Transient) == MaterialProblem::RequiredPropertyUnavailable);
        const Result<ThermalMaterial> resolved = part.resolve(ThermalAnalysisMode::Transient);
        REQUIRE_FALSE(resolved.has_value());
        CHECK_THAT(resolved.error().message, ContainsSubstring("specific heat"));
        CHECK_THAT(resolved.error().message, !ContainsSubstring("density"));
        const Result<materials::CompletenessReport> report =
            thermal::thermalMaterialCompleteness(part.document, ThermalAnalysisMode::Transient);
        REQUIRE(report.has_value());
        CHECK(report->missingMechanical.empty());
        CHECK(report->missingThermal == std::vector{ThermalPropertyKind::SpecificHeatCapacity});
    }
}

TEST_CASE("ThermalMaterial_SteadyNeedsNeitherDensityNorSpecificHeat", "[thermal][material][requirements]") {
    // THE MANDATORY OVER-REQUIREMENT CHECK: k only, both capacity inputs
    // absent, and steady conduction must resolve. "All thermal properties
    // always required" is the defect this test exists to catch.
    ThermalMaterialPart part;
    part.assignConductivityOnly();
    CHECK_FALSE(part.problem(ThermalAnalysisMode::Steady).has_value());
    const Result<ThermalMaterial> resolved = part.resolve(ThermalAnalysisMode::Steady);
    INFO((resolved.has_value() ? std::string{} : resolved.error().message));
    REQUIRE(resolved.has_value());
    CHECK(resolved->conductivity().si() == kK);
    CHECK_FALSE(resolved->capacity().has_value());

    // And the material really does lack both, so this is not a coincidence.
    CHECK_FALSE(features::requireDensity(part.document, part.material).has_value());
    CHECK_FALSE(materials::hasSpecificHeatCapacity(part.materialObject().definition().thermal));
    CHECK(materials::hasSteadyConductionProperties(part.materialObject().definition().thermal));
}

TEST_CASE("ThermalMaterial_TransientNamesEveryMissingPropertyDeterministically",
          "[thermal][material][requirements]") {
    // k only, under transient: BOTH gaps named at once, in a fixed order, the
    // same on every evaluation.
    ThermalMaterialPart part;
    part.assignConductivityOnly();
    CHECK(part.problem(ThermalAnalysisMode::Transient) == MaterialProblem::RequiredPropertyUnavailable);

    const Result<ThermalMaterial> first = part.resolve(ThermalAnalysisMode::Transient);
    REQUIRE_FALSE(first.has_value());
    CHECK_THAT(first.error().message, ContainsSubstring("density"));
    CHECK_THAT(first.error().message, ContainsSubstring("specific heat"));
    CHECK_THAT(first.error().message, !ContainsSubstring("thermal conductivity"));
    CHECK_THAT(first.error().message, ContainsSubstring("transient conduction"));
    // P15's message lists the density before the specific heat: mechanical
    // before thermal, its documented order.
    CHECK(first.error().message.find("density") < first.error().message.find("specific heat"));

    const Result<materials::CompletenessReport> report =
        thermal::thermalMaterialCompleteness(part.document, ThermalAnalysisMode::Transient);
    REQUIRE(report.has_value());
    CHECK(report->state == materials::CompletenessState::Incomplete);
    CHECK(report->missingMechanical == std::vector{MechanicalPropertyKind::Density});
    CHECK(report->missingThermal == std::vector{ThermalPropertyKind::SpecificHeatCapacity});
    CHECK(report->presentThermal == std::vector{ThermalPropertyKind::ThermalConductivity});
    REQUIRE(report->issues.size() == 2);
    CHECK(report->issues[0].mechanical == MechanicalPropertyKind::Density);
    CHECK(report->issues[1].thermal == ThermalPropertyKind::SpecificHeatCapacity);

    for (int i = 0; i < 16; ++i) {
        const Result<ThermalMaterial> again = part.resolve(ThermalAnalysisMode::Transient);
        REQUIRE_FALSE(again.has_value());
        CHECK(again.error().message == first.error().message);
        CHECK(again.error().code == first.error().code);
        CHECK(*thermal::thermalMaterialCompleteness(part.document, ThermalAnalysisMode::Transient) == *report);
    }
}

TEST_CASE("ThermalMaterial_CapabilityMatrix", "[thermal][material][requirements]") {
    // The compact policy test: five material variants, two derived answers each.
    struct Row {
        const char* label;
        std::optional<double> k;
        std::optional<double> rho;
        std::optional<double> cp;
        bool steady;
        bool transient;
    };
    const Row rows[]{
        {"k only", kK, std::nullopt, std::nullopt, true, false},
        {"k + rho", kK, kRho, std::nullopt, true, false},
        {"k + cp", kK, std::nullopt, kCp, true, false},
        {"k + rho + cp", kK, kRho, kCp, true, true},
        {"rho + cp, no k", std::nullopt, kRho, kCp, false, false},
        {"nothing", std::nullopt, std::nullopt, std::nullopt, false, false},
    };
    for (const Row& row : rows) {
        CAPTURE(row.label);
        ThermalMaterialPart part;
        std::optional<ThermalConductivity> k;
        std::optional<SpecificHeatCapacity> cp;
        std::optional<Density> rho;
        if (row.k) {
            k = ThermalConductivity::fromSi(*row.k);
        }
        if (row.cp) {
            cp = SpecificHeatCapacity::fromSi(*row.cp);
        }
        if (row.rho) {
            rho = Density::fromSi(*row.rho);
        }
        part.assign("Variant", mechanicalWith(rho), thermalWith(k, cp));

        const Result<ThermalCapability> capability = thermal::thermalCapability(part.document, part.material);
        REQUIRE(capability.has_value());
        CHECK(capability->steady == row.steady);
        CHECK(capability->transient == row.transient);
        // The capability and the resolver agree, which is what makes the
        // former safe to show a user before the latter is called.
        CHECK(part.resolve(ThermalAnalysisMode::Steady).has_value() == row.steady);
        CHECK(part.resolve(ThermalAnalysisMode::Transient).has_value() == row.transient);
    }
    // And a material that is not there has no capability to report.
    ThermalMaterialPart part;
    CHECK_FALSE(thermal::thermalCapability(part.document, MaterialId::fromValue(999999)).has_value());
}

TEST_CASE("ThermalMaterial_ATransientAnalysisOnAConductivityOnlyMaterialIsNotReady",
          "[thermal][material][requirements][state]") {
    // THE INTEGRATION WITH P18-DATA-001's STATE MODEL. The input boundary
    // accepts a k-only material (P18-ARCH-001 decided that), so what makes a
    // transient analysis on it NOT READY is this milestone's resolution,
    // called from currentResultSource for the analysis's mode.
    ThermalMaterialPart steady{ThermalAnalysisMode::Steady};
    steady.assignConductivityOnly();
    steady.requireMesh();
    CHECK(steady.state(nullptr) == AnalysisState::Ready);

    ThermalMaterialPart transient{ThermalAnalysisMode::Transient};
    transient.assignConductivityOnly();
    transient.requireMesh();
    CHECK_FALSE(transient.problem(ThermalAnalysisMode::Steady).has_value());
    CHECK(transient.problem(ThermalAnalysisMode::Transient) == MaterialProblem::RequiredPropertyUnavailable);
    CHECK(thermal::requireThermalModel(transient.document, transient.regenerator, transient.mesher,
                                       transient.control)
              .has_value());
    const Result<ThermalResultSource> none = transient.source();
    REQUIRE_FALSE(none.has_value());
    CHECK_THAT(none.error().message, ContainsSubstring("transient conduction"));
    CHECK(transient.state(nullptr) == AnalysisState::InputsUnavailable);

    // Supplying rho and cp -- a P15 edit, through P15 -- makes it Ready, with
    // no remesh.
    const meshing::MeshStamp stamp = transient.mesh().mesh().stamp();
    materials::MechanicalProperties mechanical = transient.materialObject().definition().mechanical;
    mechanical.density = materials::MaterialProperty<Density>::known(Density::fromSi(kRho));
    REQUIRE(features::setMaterialMechanical(transient.document, transient.material, mechanical).has_value());
    materials::ThermalProperties thermalProperties = transient.materialObject().definition().thermal;
    thermalProperties.specificHeatCapacity =
        materials::MaterialProperty<SpecificHeatCapacity>::known(SpecificHeatCapacity::fromSi(kCp));
    REQUIRE(features::setMaterialThermal(transient.document, transient.material, thermalProperties).has_value());
    CHECK(transient.state(nullptr) == AnalysisState::Ready);
    CHECK(transient.mesh().mesh().stamp() == stamp);
    CHECK(transient.mesher.currency(transient.document, transient.control) == meshing::MeshCurrency::Current);
}

// ---------------------------------------------------------------------------
// Validity: P15 refuses at entry, and the resolver asks P15 anyway
// ---------------------------------------------------------------------------

TEST_CASE("ThermalMaterial_P15RefusesAnUnusableValueBeforeItCanReachTheDocument",
          "[thermal][material][validity]") {
    // The checklist asks P18 to reject k = 0, k < 0, NaN and infinity, and the
    // same for rho and cp where required. P15 makes those values
    // UNREPRESENTABLE: creation refuses them, and so does every edit, through
    // the same validator. That is a stronger guarantee than a check at the
    // resolver, and the honest test is of the guarantee that exists.
    ThermalMaterialPart part;

    const auto creationRefuses = [&part](const materials::MechanicalProperties& mechanical,
                                         const materials::ThermalProperties& thermalProperties,
                                         std::string_view why) {
        features::MaterialDefinition definition;
        definition.designation = "Under test";
        definition.mechanical = mechanical;
        definition.thermal = thermalProperties;
        const Result<MaterialId> id = features::createMaterial(part.document, "UnderTest", definition);
        INFO(why);
        REQUIRE_FALSE(id.has_value());
        CHECK_THAT(id.error().message, ContainsSubstring("not valid"));
    };
    const materials::MechanicalProperties goodRho = mechanicalWith(Density::fromSi(kRho));
    const auto thermalK = [](double k) {
        return thermalWith(ThermalConductivity::fromSi(k), SpecificHeatCapacity::fromSi(kCp));
    };
    const auto thermalCp = [](double cp) {
        return thermalWith(ThermalConductivity::fromSi(kK), SpecificHeatCapacity::fromSi(cp));
    };
    const materials::ThermalProperties goodThermal = thermalK(kK);

    SECTION("thermal conductivity") {
        creationRefuses(goodRho, thermalK(0.0), "k = 0");
        creationRefuses(goodRho, thermalK(-45.0), "k negative");
        // NaN IS THE ONE A RANGE CHECK ALONE MISSES: k <= 0 is FALSE for NaN.
        creationRefuses(goodRho, thermalK(kNaN), "k NaN");
        creationRefuses(goodRho, thermalK(kInf), "k +infinity");
        creationRefuses(goodRho, thermalK(-kInf), "k -infinity");
    }
    SECTION("specific heat capacity") {
        creationRefuses(goodRho, thermalCp(0.0), "cp = 0");
        creationRefuses(goodRho, thermalCp(-500.0), "cp negative");
        creationRefuses(goodRho, thermalCp(kNaN), "cp NaN");
        creationRefuses(goodRho, thermalCp(kInf), "cp +infinity");
        creationRefuses(goodRho, thermalCp(-kInf), "cp -infinity");
    }
    SECTION("density") {
        creationRefuses(mechanicalWith(Density::fromSi(0.0)), goodThermal, "rho = 0");
        creationRefuses(mechanicalWith(Density::fromSi(-7800.0)), goodThermal, "rho negative");
        creationRefuses(mechanicalWith(Density::fromSi(kNaN)), goodThermal, "rho NaN");
        creationRefuses(mechanicalWith(Density::fromSi(kInf)), goodThermal, "rho +infinity");
        creationRefuses(mechanicalWith(Density::fromSi(-kInf)), goodThermal, "rho -infinity");
    }
    SECTION("an EDIT is refused too, so a valid material cannot be spoiled") {
        part.assignFull();
        materials::ThermalProperties broken = part.materialObject().definition().thermal;
        broken.thermalConductivity = materials::MaterialProperty<ThermalConductivity>::known(
            ThermalConductivity::fromSi(0.0));
        REQUIRE_FALSE(features::setMaterialThermal(part.document, part.material, broken).has_value());
        // AND THE MATERIAL IS UNCHANGED: a refused edit leaves the old value,
        // so the resolver still succeeds, on the value really there.
        CHECK(part.materialObject().definition().thermal.thermalConductivity.value()->si() == kK);
        CHECK(require(part.resolve(ThermalAnalysisMode::Transient)).conductivity().si() == kK);
    }
}

TEST_CASE("ThermalMaterial_TheResolverStillAsksP15RatherThanAssumingValidity",
          "[thermal][material][validity]") {
    // Given the above, the resolver's own range checks would sit behind a
    // boundary that cannot currently be crossed -- so there are none here, and
    // the delegation is what covers that half. The reachable half of P15's
    // contract -- a required property that is ABSENT -- is refused by calling
    // P15's own require* functions, the same ones that refuse an out-of-range
    // value if an import path or a file ever produced one.
    ThermalMaterialPart part;
    part.assign("Unmeasured", mechanicalWith(Density::fromSi(kRho)), thermalWith(std::nullopt, std::nullopt));

    CHECK(part.problem(ThermalAnalysisMode::Steady) == MaterialProblem::RequiredPropertyUnavailable);
    const Result<ThermalMaterial> steady = part.resolve(ThermalAnalysisMode::Steady);
    REQUIRE_FALSE(steady.has_value());
    CHECK(steady.error().message == features::requireThermalConductivity(part.document, part.material).error().message);

    const Result<ThermalMaterial> transient = part.resolve(ThermalAnalysisMode::Transient);
    REQUIRE_FALSE(transient.has_value());
    CHECK(transient.error().message ==
          features::requireTransientConductionProperties(part.document, part.material).error().message);

    WARN("an unusable VALUE cannot reach a document, because P15 refuses it at entry; an "
         "ABSENT required property can, and the resolver refuses that through P15's own words");
}

TEST_CASE("ThermalMaterial_MissingAndZeroAreDifferentThings", "[thermal][material][validity]") {
    // Absent is not zero. An Unknown conductivity reads as empty, never as
    // 0 W/(m K); a zero conductivity cannot be stored at all. The two are
    // distinguishable at P15 and the resolver never conflates them.
    materials::ThermalProperties absent;
    CHECK_FALSE(absent.thermalConductivity.value().has_value());
    CHECK(absent.thermalConductivity.isUnknown());
    CHECK_FALSE(absent.thermalConductivity.value() == std::optional<ThermalConductivity>{0_W_per_m_K});

    materials::ThermalProperties zero;
    zero.thermalConductivity = materials::MaterialProperty<ThermalConductivity>::known(0_W_per_m_K);
    const Result<void> refused = materials::validate(zero);
    REQUIRE_FALSE(refused.has_value());
    CHECK_THAT(refused.error().message, ContainsSubstring("greater than zero"));
    CHECK(materials::validate(absent).has_value());
    CHECK_FALSE(materials::hasSteadyConductionProperties(absent));
    CHECK_FALSE(materials::hasSteadyConductionProperties(zero));
}

// ---------------------------------------------------------------------------
// No material, no body
// ---------------------------------------------------------------------------

TEST_CASE("ThermalMaterial_RefusesWhenNothingIsAssigned", "[thermal][material]") {
    ThermalMaterialPart part;
    for (const ThermalAnalysisMode mode : {ThermalAnalysisMode::Steady, ThermalAnalysisMode::Transient}) {
        CAPTURE(thermal::toString(mode));
        CHECK(part.problem(mode) == MaterialProblem::NoMaterialAssigned);
        const Result<ThermalMaterial> resolved = part.resolve(mode);
        REQUIRE_FALSE(resolved.has_value());
        CHECK_THAT(resolved.error().message, ContainsSubstring("no material assigned"));
    }
}

TEST_CASE("ThermalMaterial_RefusesWhenTheAssignedMaterialIsGone", "[thermal][material]") {
    // Deleted, not unassigned: a DIFFERENT diagnostic (ADR-026), carried
    // rather than flattened. No cached value survives the deletion.
    ThermalMaterialPart part;
    part.assignFull();
    REQUIRE(part.resolve(ThermalAnalysisMode::Transient).has_value());
    REQUIRE(features::removeMaterial(part.document, part.material).has_value());

    CHECK(part.problem(ThermalAnalysisMode::Steady) == MaterialProblem::NoMaterialAssigned);
    const Result<ThermalMaterial> resolved = part.resolve(ThermalAnalysisMode::Transient);
    REQUIRE_FALSE(resolved.has_value());
    CHECK_THAT(resolved.error().message, !ContainsSubstring("no material assigned"));
    CHECK(features::materialAssignment(part.document).state ==
          features::MaterialAssignmentState::Unresolved);
}

TEST_CASE("ThermalMaterial_RefusesWhenTheBodyIsGone", "[thermal][material]") {
    ThermalMaterialPart part;
    part.assignFull();
    const ObjectId absent = ObjectId::fromValue(999999);
    CHECK(thermal::thermalMaterialProblem(part.document, absent, ThermalAnalysisMode::Steady) ==
          MaterialProblem::BodyNotFound);
    const Result<ThermalMaterial> resolved =
        thermal::resolveThermalMaterial(part.document, absent, ThermalAnalysisMode::Steady);
    REQUIRE_FALSE(resolved.has_value());
    CHECK(resolved.error().code == ErrorCode::NotFound);
}

TEST_CASE("ThermalMaterial_ReachesEveryMaterialProblemItDeclares", "[thermal][material]") {
    ThermalMaterialPart part;
    CHECK(thermal::thermalMaterialProblem(part.document, ObjectId::fromValue(999999),
                                          ThermalAnalysisMode::Steady) == MaterialProblem::BodyNotFound);
    CHECK(part.problem(ThermalAnalysisMode::Steady) == MaterialProblem::NoMaterialAssigned);
    part.assign("NoK", mechanicalWith(std::nullopt), thermalWith(std::nullopt, std::nullopt));
    CHECK(part.problem(ThermalAnalysisMode::Steady) == MaterialProblem::RequiredPropertyUnavailable);
    for (const MaterialProblem problem : {MaterialProblem::BodyNotFound, MaterialProblem::NoMaterialAssigned,
                                          MaterialProblem::RequiredPropertyUnavailable}) {
        CHECK(thermal::toString(problem) != "unknown");
    }
}

// ---------------------------------------------------------------------------
// Provenance
// ---------------------------------------------------------------------------

TEST_CASE("ThermalMaterial_PreservesIdentityAndRevisionWithoutCopyingProvenance",
          "[thermal][material][provenance]") {
    ThermalMaterialPart part;
    part.assignFull();
    const ThermalMaterial resolved = require(part.resolve(ThermalAnalysisMode::Transient));
    CHECK(resolved.id() == part.material);
    CHECK(resolved.revision() == part.materialObject().revision());
    CHECK(resolved.revision() != 0);

    // THE VIEW IS A REFERENCE BACK, NOT A RECORD. It is small and fixed: an
    // id, a revision, a mode, a conductivity and an optional capacity pair --
    // no name, no designation, no provenance text, no library key.
    struct Permitted {
        MaterialId id;
        std::uint64_t revision;
        ThermalAnalysisMode mode;
        ThermalConductivity conductivity;
        std::optional<ThermalCapacity> capacity;
    };
    static_assert(sizeof(ThermalMaterial) == sizeof(Permitted),
                  "a thermal material view is an id, a revision, a mode, k and an optional rho/cp "
                  "pair -- nothing else. A name, a provenance record or a database entry added to "
                  "it breaks this");
    static_assert(!std::is_constructible_v<ThermalMaterial, features::MaterialDefinition>);
    static_assert(!std::is_default_constructible_v<ThermalMaterial>,
                  "the only way to one is the validating resolver");
}

TEST_CASE("ThermalMaterial_TheSameIdWithAChangedConductivityIsADifferentInput",
          "[thermal][material][provenance]") {
    // THE ADVERSARIAL CASE: MaterialId and designation unchanged, only k moved.
    // Currentness that compared the id alone would call the old result
    // current, and the user would read a temperature field computed from a
    // conductivity their model no longer has.
    ThermalMaterialPart part;
    part.assignFull();
    const ThermalMaterial before = require(part.resolve(ThermalAnalysisMode::Steady));

    materials::ThermalProperties changed = part.materialObject().definition().thermal;
    changed.thermalConductivity = materials::MaterialProperty<ThermalConductivity>::known(50_W_per_m_K);
    REQUIRE(features::setMaterialThermal(part.document, part.material, changed).has_value());

    const ThermalMaterial after = require(part.resolve(ThermalAnalysisMode::Steady));
    CHECK(after.id() == before.id());
    CHECK(after.revision() == before.revision() + 1);
    CHECK(after.conductivity().si() == 50.0);
    CHECK_FALSE(after == before);
    CHECK(part.materialObject().definition().designation == "Steel");
}

TEST_CASE("ThermalMaterial_ProvenanceDistinguishesIdAndRevision", "[thermal][material][provenance]") {
    // A revision 1, A revision 2, B revision 1: three distinct provenances.
    ThermalMaterialPart part;
    const MaterialId a = part.assignFull();
    const ThermalMaterial a1 = require(part.resolve(ThermalAnalysisMode::Transient));

    materials::ThermalProperties edited = part.materialObject().definition().thermal;
    edited.specificHeatCapacity = materials::MaterialProperty<SpecificHeatCapacity>::known(490_J_per_kg_K);
    REQUIRE(features::setMaterialThermal(part.document, a, edited).has_value());
    const ThermalMaterial a2 = require(part.resolve(ThermalAnalysisMode::Transient));

    // B carries the numbers A had at revision 1, exactly.
    const MaterialId b = part.assign("Other", mechanicalWith(Density::fromSi(kRho)),
                                     thermalWith(ThermalConductivity::fromSi(kK), SpecificHeatCapacity::fromSi(kCp)));
    const ThermalMaterial b1 = require(part.resolve(ThermalAnalysisMode::Transient));

    CHECK(a1.id() == a2.id());
    CHECK(a1.revision() != a2.revision());
    CHECK_FALSE(a1 == a2);
    CHECK(b1.id() == b);
    CHECK(b1.id() != a1.id());
    // SAME VALUES, DIFFERENT CANONICAL IDENTITY: not equal, by identity.
    CHECK(b1.conductivity() == a1.conductivity());
    CHECK(b1.capacity() == a1.capacity());
    CHECK_FALSE(b1 == a1);
}

// ---------------------------------------------------------------------------
// Invalidation: the result goes stale, the mesh does not
// ---------------------------------------------------------------------------

TEST_CASE("ThermalMaterial_AConductivityEditLeavesTheMeshCurrentAndStalesTheResult",
          "[thermal][material][invalidate]") {
    // THE HARD REQUIREMENT. A mesh is a function of geometry and meshing
    // intent, and k is in neither.
    ThermalMaterialPart part;
    part.assignFull();
    part.requireMesh();

    const meshing::MeshStamp stampBefore = part.mesh().mesh().stamp();
    const meshing::GeometryRevision geometryBefore = meshing::geometryRevision(part.document, part.feature);
    const meshing::MeshControlDefinition intentBefore = part.meshingIntent();
    std::vector<meshing::NodeId> idsBefore;
    for (const meshing::Node& node : part.mesh().mesh().nodes()) {
        idsBefore.push_back(node.id);
    }
    const ThermalResultSource before = part.currentSource();
    const ThermalResult result = part.resultFor(before);
    CHECK(part.state(&result) == AnalysisState::SolvedCurrent);

    materials::ThermalProperties changed = part.materialObject().definition().thermal;
    changed.thermalConductivity = materials::MaterialProperty<ThermalConductivity>::known(50_W_per_m_K);
    REQUIRE(features::setMaterialThermal(part.document, part.material, changed).has_value());

    // THE MESH IS UNTOUCHED, asked through P16's own currentness API rather
    // than by comparing a pointer; and no hidden remesh happened.
    CHECK(part.mesher.currency(part.document, part.control) == meshing::MeshCurrency::Current);
    CHECK(meshing::describesTheModel(part.mesher.currency(part.document, part.control)));
    CHECK(part.mesh().mesh().stamp() == stampBefore);
    std::vector<meshing::NodeId> idsAfter;
    for (const meshing::Node& node : part.mesh().mesh().nodes()) {
        idsAfter.push_back(node.id);
    }
    CHECK(idsAfter == idsBefore);
    // The geometry did not move either, nor the meshing intent.
    CHECK(meshing::geometryRevision(part.document, part.feature) == geometryBefore);
    CHECK(part.meshingIntent() == intentBefore);

    // AND THE RESULT SOURCE DID MOVE, on the material alone.
    const ThermalResultSource after = part.currentSource();
    CHECK(thermal::staleReasons(before, after) == std::vector{StaleReason::Material});
    CHECK(after.mesh == before.mesh);
    CHECK(after.geometry == before.geometry);
    CHECK(after.material == before.material);
    CHECK(after.materialRevision == before.materialRevision + 1);
    CHECK(thermal::resultCurrency(&result, after) == ResultCurrency::Stale);
    CHECK(part.state(&result) == AnalysisState::SolvedStale);
    CHECK(require(part.resolve(ThermalAnalysisMode::Steady)).conductivity().si() == 50.0);

    WARN(std::format("k {} -> 50 W/(m K): mesh {}, stamp and NodeIds unchanged, geometry revision "
                     "unchanged, meshing intent unchanged; result source stale on material alone",
                     kK, meshing::toString(part.mesher.currency(part.document, part.control))));
}

TEST_CASE("ThermalMaterial_DensityAndSpecificHeatEditsStaleBothModes",
          "[thermal][material][invalidate]") {
    // THE POLICY, MEASURED: materialRevision is the material's one document
    // revision, which moves on any effective edit. So a rho or cp edit stales
    // a STEADY result too, conservatively -- the same choice P17-MAT-001 made
    // and recorded for a density edit on a no-gravity analysis -- and stales a
    // transient one necessarily.
    const auto densityEdit = [](ThermalAnalysisMode mode) {
        ThermalMaterialPart part{mode};
        part.assignFull();
        part.requireMesh();
        const ThermalResultSource before = part.currentSource();
        const ThermalResult result = part.resultFor(before);

        materials::MechanicalProperties mechanical = part.materialObject().definition().mechanical;
        mechanical.density = materials::MaterialProperty<Density>::known(7900_kg_per_m3);
        REQUIRE(features::setMaterialMechanical(part.document, part.material, mechanical).has_value());

        CHECK(part.mesher.currency(part.document, part.control) == meshing::MeshCurrency::Current);
        CHECK(thermal::staleReasons(before, part.currentSource()) == std::vector{StaleReason::Material});
        CHECK(part.state(&result) == AnalysisState::SolvedStale);
        if (mode == ThermalAnalysisMode::Transient) {
            CHECK(capacityOf(require(part.resolve(mode))).density.si() == 7900.0);
        }
    };
    const auto specificHeatEdit = [](ThermalAnalysisMode mode) {
        ThermalMaterialPart part{mode};
        part.assignFull();
        part.requireMesh();
        const ThermalResultSource before = part.currentSource();
        const ThermalResult result = part.resultFor(before);

        materials::ThermalProperties thermalProperties = part.materialObject().definition().thermal;
        thermalProperties.specificHeatCapacity =
            materials::MaterialProperty<SpecificHeatCapacity>::known(490_J_per_kg_K);
        REQUIRE(features::setMaterialThermal(part.document, part.material, thermalProperties).has_value());

        CHECK(part.mesher.currency(part.document, part.control) == meshing::MeshCurrency::Current);
        CHECK(thermal::staleReasons(before, part.currentSource()) == std::vector{StaleReason::Material});
        CHECK(part.state(&result) == AnalysisState::SolvedStale);
        if (mode == ThermalAnalysisMode::Transient) {
            CHECK(capacityOf(require(part.resolve(mode))).specificHeatCapacity.si() == 490.0);
        }
    };
    SECTION("a density edit under a steady analysis") { densityEdit(ThermalAnalysisMode::Steady); }
    SECTION("a density edit under a transient analysis") { densityEdit(ThermalAnalysisMode::Transient); }
    SECTION("a specific heat edit under a steady analysis") {
        specificHeatEdit(ThermalAnalysisMode::Steady);
    }
    SECTION("a specific heat edit under a transient analysis") {
        specificHeatEdit(ThermalAnalysisMode::Transient);
    }
}

TEST_CASE("ThermalMaterial_AStructuralEditStalesTheThermalResultUnderWholeMaterialRevision",
          "[thermal][material][invalidate]") {
    // E and nu are not thermal inputs, and an edit to them still stales a
    // thermal result: ONE revision per material, the same policy P17 applies
    // to a thermal edit on a structural result. Consistent between the two
    // phases, and the cost -- one unnecessary re-solve -- is recorded.
    ThermalMaterialPart part;
    part.assignFull();
    part.requireMesh();
    const ThermalResultSource before = part.currentSource();
    const ThermalResult result = part.resultFor(before);

    materials::MechanicalProperties mechanical = part.materialObject().definition().mechanical;
    mechanical.youngsModulus = materials::MaterialProperty<ElasticModulus>::known(210_GPa);
    REQUIRE(features::setMaterialMechanical(part.document, part.material, mechanical).has_value());

    CHECK(part.mesher.currency(part.document, part.control) == meshing::MeshCurrency::Current);
    CHECK(thermal::staleReasons(before, part.currentSource()) == std::vector{StaleReason::Material});
    CHECK(part.state(&result) == AnalysisState::SolvedStale);
    // The thermal inputs did not change.
    CHECK(require(part.resolve(ThermalAnalysisMode::Transient)).conductivity().si() == kK);

    // And a NO-OP material edit moves nothing: the precision available
    // without a per-property revision.
    const ThermalResultSource mid = part.currentSource();
    REQUIRE(features::setMaterialMechanical(part.document, part.material, mechanical).has_value());
    CHECK(part.currentSource() == mid);
}

TEST_CASE("ThermalMaterial_ReassignmentStalesTheResultAndResolvesTheNewMaterial",
          "[thermal][material][invalidate]") {
    ThermalMaterialPart part;
    part.assignFull();
    part.requireMesh();
    const ThermalResultSource before = part.currentSource();
    const ThermalResult result = part.resultFor(before);
    const meshing::MeshStamp stamp = part.mesh().mesh().stamp();

    SECTION("to a different material") {
        features::MaterialDefinition second;
        second.designation = "Aluminium";
        second.mechanical = mechanicalWith(2700_kg_per_m3);
        second.thermal = thermalWith(167_W_per_m_K, 896_J_per_kg_K);
        const Result<MaterialId> other = features::createMaterial(part.document, "Aluminium", second);
        REQUIRE(other.has_value());
        REQUIRE(features::assignMaterial(part.document, *other).has_value());

        CHECK(part.mesher.currency(part.document, part.control) == meshing::MeshCurrency::Current);
        CHECK(part.mesh().mesh().stamp() == stamp);
        const ThermalResultSource after = part.currentSource();
        CHECK(thermal::staleReasons(before, after) == std::vector{StaleReason::Material});
        CHECK(after.material == *other);
        CHECK(part.state(&result) == AnalysisState::SolvedStale);
        const ThermalMaterial resolved = require(part.resolve(ThermalAnalysisMode::Transient));
        CHECK(resolved.id() == *other);
        CHECK(resolved.conductivity().si() == 167.0);
        CHECK(capacityOf(resolved).density.si() == 2700.0);
    }
    SECTION("to a numerically identical material") {
        // Identity changed, values did not: stale all the same, because
        // currentness follows source authority and never deep-compares k,
        // rho and cp.
        const Result<MaterialId> twin = features::cloneMaterial(part.document, part.material, "SteelTwin");
        REQUIRE(twin.has_value());
        REQUIRE(features::assignMaterial(part.document, *twin).has_value());

        CHECK(part.mesher.currency(part.document, part.control) == meshing::MeshCurrency::Current);
        const ThermalResultSource after = part.currentSource();
        CHECK(thermal::staleReasons(before, after) == std::vector{StaleReason::Material});
        CHECK(part.state(&result) == AnalysisState::SolvedStale);
        const ThermalMaterial resolved = require(part.resolve(ThermalAnalysisMode::Transient));
        CHECK(resolved.conductivity().si() == kK);
        CHECK(capacityOf(resolved).density.si() == kRho);
        CHECK(capacityOf(resolved).specificHeatCapacity.si() == kCp);
        CHECK(resolved.id() != part.material);
    }
}

TEST_CASE("ThermalMaterial_RemovalStalesTheResultAndNothingCachedSurvives",
          "[thermal][material][invalidate]") {
    ThermalMaterialPart part;
    part.assignFull();
    part.requireMesh();
    const ThermalResultSource before = part.currentSource();
    const ThermalResult result = part.resultFor(before);
    const meshing::MeshStamp stamp = part.mesh().mesh().stamp();

    SECTION("the assignment is removed") {
        REQUIRE(features::removeMaterialAssignment(part.document).has_value());
        CHECK(part.mesher.currency(part.document, part.control) == meshing::MeshCurrency::Current);
        CHECK(part.mesh().mesh().stamp() == stamp);
        // No current source can be computed, so the old result is not current
        // -- and not offered as "stale" either: there is nothing to compare it
        // with. The input boundary reports the reason.
        REQUIRE_FALSE(part.source().has_value());
        CHECK(part.state(&result) == AnalysisState::InputsUnavailable);
        CHECK(part.problem(ThermalAnalysisMode::Steady) == MaterialProblem::NoMaterialAssigned);
        CHECK(thermal::thermalInputProblem(part.document, part.regenerator, part.mesher, part.control) ==
              thermal::InputProblem::NoMaterialAssigned);
    }
    SECTION("the material itself is deleted") {
        REQUIRE(features::removeMaterial(part.document, part.material).has_value());
        CHECK(part.mesher.currency(part.document, part.control) == meshing::MeshCurrency::Current);
        REQUIRE_FALSE(part.source().has_value());
        CHECK(part.state(&result) == AnalysisState::InputsUnavailable);
        CHECK(part.problem(ThermalAnalysisMode::Transient) == MaterialProblem::NoMaterialAssigned);
    }
}

TEST_CASE("ThermalMaterial_AFailedMaterialEditChangesNothing", "[thermal][material][invalidate]") {
    // ATOMICITY, INHERITED FROM P15: a refused edit commits nothing, so the
    // revision does not move, the mesh is untouched and the result stays
    // CURRENT. No false invalidation.
    ThermalMaterialPart part{ThermalAnalysisMode::Transient};
    part.assignFull();
    part.requireMesh();
    const ThermalResultSource before = part.currentSource();
    const ThermalResult result = part.resultFor(before);
    const std::uint64_t revision = part.materialObject().revision();
    const std::uint64_t documentRevision = part.document.revision();

    materials::ThermalProperties broken = part.materialObject().definition().thermal;
    broken.specificHeatCapacity = materials::MaterialProperty<SpecificHeatCapacity>::known(
        SpecificHeatCapacity::fromSi(-1.0));
    REQUIRE_FALSE(features::setMaterialThermal(part.document, part.material, broken).has_value());
    materials::MechanicalProperties brokenToo = part.materialObject().definition().mechanical;
    brokenToo.density = materials::MaterialProperty<Density>::known(Density::fromSi(kNaN));
    REQUIRE_FALSE(features::setMaterialMechanical(part.document, part.material, brokenToo).has_value());

    CHECK(part.materialObject().revision() == revision);
    CHECK(part.document.revision() == documentRevision);
    CHECK(part.mesher.currency(part.document, part.control) == meshing::MeshCurrency::Current);
    CHECK(part.currentSource() == before);
    CHECK(part.state(&result) == AnalysisState::SolvedCurrent);
    const ThermalMaterial resolved = require(part.resolve(ThermalAnalysisMode::Transient));
    CHECK(capacityOf(resolved).specificHeatCapacity.si() == kCp);
    CHECK(capacityOf(resolved).density.si() == kRho);
}

TEST_CASE("ThermalMaterial_ARemeshLeavesTheMaterialSourceAloneAndStalesOnTheMeshAxis",
          "[thermal][material][invalidate]") {
    // Two independent source axes: a remesh without a material edit moves the
    // mesh stamp and nothing about the material; the result is stale for
    // reason mesh and for no other.
    ThermalMaterialPart part;
    part.assignFull();
    part.requireMesh();
    const ThermalResultSource before = part.currentSource();
    const ThermalResult result = part.resultFor(before);
    const ThermalMaterial materialBefore = require(part.resolve(ThermalAnalysisMode::Steady));

    part.requireMesh();
    const ThermalResultSource after = part.currentSource();
    CHECK(thermal::staleReasons(before, after) == std::vector{StaleReason::Mesh});
    CHECK(after.material == before.material);
    CHECK(after.materialRevision == before.materialRevision);
    CHECK(require(part.resolve(ThermalAnalysisMode::Steady)) == materialBefore);
    CHECK(part.state(&result) == AnalysisState::SolvedStale);
}

// ---------------------------------------------------------------------------
// Determinism and read-only resolution
// ---------------------------------------------------------------------------

TEST_CASE("ThermalMaterial_ResolutionIsDeterministicAndReadOnly", "[thermal][material][determinism]") {
    ThermalMaterialPart part;
    part.assignFull();
    const std::uint64_t documentRevision = part.document.revision();
    const std::uint64_t materialRevision = part.materialObject().revision();

    const ThermalMaterial first = require(part.resolve(ThermalAnalysisMode::Transient));
    for (int i = 0; i < 16; ++i) {
        CHECK(require(part.resolve(ThermalAnalysisMode::Transient)) == first);
        CHECK(require(part.resolve(ThermalAnalysisMode::Steady)).conductivity() == first.conductivity());
        CHECK(*thermal::thermalCapability(part.document, part.material) ==
              ThermalCapability{.steady = true, .transient = true});
    }
    CHECK(part.document.revision() == documentRevision);
    CHECK(part.materialObject().revision() == materialRevision);

    // Failed resolutions fill nothing: after eight refusals the absent cp is
    // still absent -- not 0, not a typical value, not anything.
    materials::ThermalProperties withoutCp = part.materialObject().definition().thermal;
    withoutCp.specificHeatCapacity = materials::MaterialProperty<SpecificHeatCapacity>::unknown();
    REQUIRE(features::setMaterialThermal(part.document, part.material, withoutCp).has_value());
    const std::uint64_t edited = part.document.revision();
    for (int i = 0; i < 8; ++i) {
        REQUIRE_FALSE(part.resolve(ThermalAnalysisMode::Transient).has_value());
    }
    CHECK(part.document.revision() == edited);
    CHECK(part.materialObject().definition().thermal.specificHeatCapacity.isUnknown());
    CHECK(part.materialObject().definition().thermal.thermalConductivity.value()->si() == kK);
    CHECK(part.materialObject().definition().mechanical.density.value()->si() == kRho);
}
