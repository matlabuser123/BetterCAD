// P18-DATA-001: the thermal identity model, the result model and currentness.
//
// WHAT THESE TESTS ARE FOR. This milestone's claims are almost all about what
// CANNOT happen -- a heat load cannot be a node, a temperature cannot enter the
// definition, a result from one mesh generation cannot be read against the
// next, a failed solve cannot make a stale result current, a Celsius value
// cannot exist. A claim of that shape is worth nothing written in a comment,
// so each one here is either a compile-time assertion, a compile-fail case in
// tests/compile_fail/ThermalIdMisuse.cpp, or a test that constructs the
// dangerous thing and watches it be refused.
//
// THE VALUES IN THE RESULTS BELOW ARE NOT A SOLVE. Nothing solves yet. They
// are data chosen to exercise the container's invariants -- cardinality,
// ordering, finiteness, absolute temperature, stamp binding, mode
// completeness -- and the tests say nothing about physics. The first physical
// numbers arrive with P18-SOLVE-001.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/materials/ThermalProperties.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/core/units/UnitCatalog.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/meshing/MeshSizing.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/thermal/ThermalAnalysis.hpp>
#include <bettercad/thermal/ThermalAnalysisObject.hpp>
#include <bettercad/thermal/ThermalData.hpp>
#include <bettercad/thermal/ThermalResult.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
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
using thermal::AnalysisState;
using thermal::ConvectionBoundary;
using thermal::HeatBalance;
using thermal::HeatFlux3;
using thermal::PrescribedTemperature;
using thermal::ResultCurrency;
using thermal::StaleReason;
using thermal::SurfaceHeatFluxLoad;
using thermal::TemperatureGradient3;
using thermal::ThermalAnalysis;
using thermal::ThermalAnalysisDefinition;
using thermal::ThermalAnalysisMode;
using thermal::ThermalBoundaryCondition;
using thermal::ThermalLoad;
using thermal::ThermalResult;
using thermal::ThermalResultSlot;
using thermal::ThermalResultSource;
using thermal::ThermalSolverSettings;
using thermal::TransientOutputPolicy;
using thermal::TransientSettings;
using thermal::UniformInitialTemperature;
using thermal::VectorComponent;
using thermal::VolumetricHeatSourceLoad;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

/// A document with a block, a meshing control, a fully characterised
/// conductor and one steady thermal analysis: everything a thermal result
/// needs provenance for.
struct ConductingStudy {
    Document document{"Part"};
    features::Regenerator regenerator;
    meshing::Mesher mesher;
    ObjectId feature{};
    MeshControlId control{};
    ThermalAnalysisId analysis{};
    MaterialId material{};
    std::array<EntityId, 4> lines{};

    explicit ConductingStudy(ThermalAnalysisMode mode = ThermalAnalysisMode::Steady) {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        lines = addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
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
        definition.mechanical.density = materials::MaterialProperty<Density>::known(7850_kg_per_m3);
        definition.thermal.thermalConductivity =
            materials::MaterialProperty<ThermalConductivity>::known(45_W_per_m_K);
        definition.thermal.specificHeatCapacity =
            materials::MaterialProperty<SpecificHeatCapacity>::known(460_J_per_kg_K);
        const Result<MaterialId> id = features::createMaterial(document, "Steel", definition);
        REQUIRE(id.has_value());
        material = *id;
        REQUIRE(features::assignMaterial(document, material).has_value());

        auto study = ThermalAnalysis::create("Study", mode == ThermalAnalysisMode::Steady
                                                          ? steadyDefinition()
                                                          : transientDefinition());
        INFO((study.has_value() ? std::string{} : study.error().message));
        REQUIRE(study.has_value());
        analysis = ThermalAnalysisId::fromValue(require(document.addObject(std::move(*study))).value());

        requireReport(regenerator, document);
    }

    [[nodiscard]] ThermalAnalysisDefinition steadyDefinition() const {
        return ThermalAnalysisDefinition{.mesh = control};
    }

    [[nodiscard]] ThermalAnalysisDefinition transientDefinition() const {
        return ThermalAnalysisDefinition{
            .mesh = control,
            .mode = ThermalAnalysisMode::Transient,
            .initialCondition = UniformInitialTemperature{.value = 293.15_K},
            .transient = TransientSettings{.timeStep = 0.5_s, .endTime = 10_s},
        };
    }

    [[nodiscard]] FaceName startCap() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::StartCap}};
    }
    [[nodiscard]] FaceName endCap() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::EndCap}};
    }
    [[nodiscard]] FaceName side() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::Side, .entity = lines.at(0)}};
    }

    [[nodiscard]] const ThermalAnalysis& study() const {
        const ThermalAnalysis* object = thermal::findThermalAnalysis(document, analysis);
        REQUIRE(object != nullptr);
        return *object;
    }

    [[nodiscard]] const ThermalAnalysisDefinition& definition() const { return study().definition(); }
    [[nodiscard]] std::uint64_t revision() const { return study().revision(); }

    /// Replaces the definition through the document, so the change is
    /// tracked and the revision moves exactly as a command would move it.
    [[nodiscard]] Result<bool> setDefinition(ThermalAnalysisDefinition definition) {
        return document.modifyObject<ThermalAnalysis>(
            ObjectId::fromValue(analysis.value()),
            [&definition](ThermalAnalysis& object) -> Result<bool> {
                return object.setDefinition(definition);
            });
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

    [[nodiscard]] ThermalResultSource currentSource() const {
        const Result<ThermalResultSource> source =
            thermal::currentResultSource(document, regenerator, mesher, analysis);
        INFO((source.has_value() ? std::string{} : source.error().message));
        REQUIRE(source.has_value());
        return *source;
    }

    [[nodiscard]] AnalysisState state(const ThermalResult* result, const Error* failure) const {
        return thermal::analysisState(document, regenerator, mesher, analysis, result, failure);
    }

    /// A result of the right shape for the current mesh, uniform at @p
    /// temperature. The numbers exercise the container and are not a
    /// solution; see the file header.
    [[nodiscard]] ThermalResult resultFor(const ThermalResultSource& source,
                                          Temperature temperature = 293.15_K) const {
        const meshing::VolumeMesh& held = mesh();
        std::vector<Temperature> temperatures(held.nodeCount(), temperature);
        std::vector<TemperatureGradient3> gradients(held.tetrahedronCount(), TemperatureGradient3{});
        std::vector<HeatFlux3> fluxes(held.tetrahedronCount(), HeatFlux3{});
        HeatBalance balance{};
        std::optional<Time> time;
        if (source.mode == ThermalAnalysisMode::Transient) {
            balance.storedEnergyRate = 0_W;
            time = 1_s;
        }
        Result<ThermalResult> result = ThermalResult::create(
            source, held, std::move(temperatures), std::move(gradients), std::move(fluxes), balance, time);
        INFO((result.has_value() ? std::string{} : result.error().message));
        REQUIRE(result.has_value());
        return std::move(*result);
    }
};

[[nodiscard]] ThermalLoad fluxOn(const FaceName& face, ThermalLoadId id, double wattsPerSquareMetre) {
    return ThermalLoad{id, SurfaceHeatFluxLoad{.face = face,
                                              .flux = HeatFlux::fromSi(wattsPerSquareMetre)}};
}

[[nodiscard]] ThermalBoundaryCondition heldAt(const FaceName& face, ThermalBoundaryConditionId id,
                                              Temperature value) {
    return ThermalBoundaryCondition{id, PrescribedTemperature{.face = face, .value = value}};
}

} // namespace

// ---------------------------------------------------------------------------
// Identity domains
// ---------------------------------------------------------------------------

TEST_CASE("ThermalData_StrongIdsAreDistinct", "[thermal][data][id]") {
    // Not interchangeable, not integers, and not P17's. The compile-fail cases
    // in tests/compile_fail/ThermalIdMisuse.cpp prove the conversions do not
    // exist; this proves the types are distinct and behave as identities.
    static_assert(!std::is_same_v<ThermalAnalysisId, ThermalLoadId>);
    static_assert(!std::is_same_v<ThermalLoadId, ThermalBoundaryConditionId>);
    static_assert(!std::is_same_v<ThermalAnalysisId, ThermalBoundaryConditionId>);
    static_assert(!std::is_same_v<ThermalAnalysisId, AnalysisId>);
    static_assert(!std::is_same_v<ThermalLoadId, LoadId>);
    static_assert(!std::is_same_v<ThermalBoundaryConditionId, RestraintId>);
    static_assert(!std::is_convertible_v<ThermalAnalysisId, std::uint64_t>);
    static_assert(!std::is_convertible_v<std::uint64_t, ThermalLoadId>);
    static_assert(!std::is_convertible_v<ThermalLoadId, LoadId>);
    static_assert(!std::is_convertible_v<LoadId, ThermalLoadId>);
    static_assert(!std::is_convertible_v<AnalysisId, ThermalAnalysisId>);
    static_assert(!std::is_convertible_v<ThermalAnalysisId, AnalysisId>);

    CHECK_FALSE(ThermalAnalysisId{}.isValid());
    CHECK(ThermalAnalysisId::fromValue(7).isValid());
    CHECK(ThermalAnalysisId::fromValue(7) == ThermalAnalysisId::fromValue(7));
    CHECK(ThermalAnalysisId::fromValue(7) < ThermalAnalysisId::fromValue(8));

    // The diagnostic names keep the kinds apart in a log as well.
    CHECK(std::format("{}", ThermalAnalysisId::fromValue(3)) == "thermal analysis:3");
    CHECK(std::format("{}", ThermalLoadId::fromValue(3)) == "thermal load:3");
    CHECK(std::format("{}", ThermalBoundaryConditionId::fromValue(3)) ==
          "thermal boundary condition:3");
}

TEST_CASE("ThermalData_OnlyTheAnalysisIsADocumentObjectIdentity", "[thermal][data][id]") {
    // THE DOMAIN CLAIM, IN THE TYPE SYSTEM. An analysis is a document object,
    // so its ID widens to ObjectId. A load and a boundary condition are
    // members of an analysis's definition, so theirs do not.
    static_assert(std::is_convertible_v<ThermalAnalysisId, ObjectId>);
    static_assert(!std::is_convertible_v<ThermalLoadId, ObjectId>);
    static_assert(!std::is_convertible_v<ThermalBoundaryConditionId, ObjectId>);

    ConductingStudy part;
    const ObjectId widened = part.analysis;
    CHECK(part.document.findObject(widened) != nullptr);
}

TEST_CASE("ThermalData_NoMeshOrSolverHandleCanActAsAThermalIdentity", "[thermal][data][id]") {
    // The negative half of the identity model as a COMPILE-TIME fact. A
    // NodeId, an ElementId and a row index are mesh-local; none can be used
    // where a persisted thermal identity is wanted, and none of the thermal
    // identities can be used as a mesh handle.
    static_assert(!std::is_convertible_v<meshing::NodeId, ThermalAnalysisId>);
    static_assert(!std::is_convertible_v<meshing::NodeId, ThermalLoadId>);
    static_assert(!std::is_convertible_v<meshing::ElementId, ThermalLoadId>);
    static_assert(!std::is_convertible_v<meshing::NodeId, ThermalBoundaryConditionId>);
    static_assert(!std::is_constructible_v<ThermalLoadId, meshing::NodeId>);
    static_assert(!std::is_constructible_v<ThermalBoundaryConditionId, meshing::ElementId>);
    static_assert(!std::is_convertible_v<ThermalAnalysisId, meshing::NodeId>);
    static_assert(!std::is_convertible_v<ThermalLoadId, meshing::NodeId>);
    static_assert(!std::is_convertible_v<ThermalBoundaryConditionId, meshing::ElementId>);
    static_assert(!std::is_convertible_v<std::size_t, ThermalLoadId>);

    // And the canonical records have nowhere for a mesh handle to hide: a
    // face load is a FaceName and a value, exactly.
    struct PermittedFlux {
        FaceName face;
        HeatFlux flux;
    };
    struct PermittedHeld {
        FaceName face;
        Temperature value;
    };
    struct PermittedConvection {
        FaceName face;
        HeatTransferCoefficient coefficient;
        Temperature ambient;
    };
    static_assert(sizeof(SurfaceHeatFluxLoad) == sizeof(PermittedFlux));
    static_assert(sizeof(PrescribedTemperature) == sizeof(PermittedHeld));
    static_assert(sizeof(ConvectionBoundary) == sizeof(PermittedConvection));
    static_assert(sizeof(VolumetricHeatSourceLoad) == sizeof(VolumetricHeatSource));
    static_assert(!std::is_constructible_v<SurfaceHeatFluxLoad, meshing::NodeId, HeatFlux>);
    static_assert(!std::is_constructible_v<PrescribedTemperature, meshing::ElementId, Temperature>);
    SUCCEED("the conversions that would break the identity model do not exist");
}

// ---------------------------------------------------------------------------
// Units and conventions
// ---------------------------------------------------------------------------

TEST_CASE("ThermalData_UnitsAreExplicitSi", "[thermal][data][units]") {
    // SI internally, through the units framework. No field is a bare double
    // except the two solver tolerances, which are ratios.
    static_assert(std::is_same_v<decltype(SurfaceHeatFluxLoad::flux), HeatFlux>);
    static_assert(std::is_same_v<decltype(VolumetricHeatSourceLoad::generation), VolumetricHeatSource>);
    static_assert(std::is_same_v<decltype(PrescribedTemperature::value), Temperature>);
    static_assert(std::is_same_v<decltype(ConvectionBoundary::coefficient), HeatTransferCoefficient>);
    static_assert(std::is_same_v<decltype(ConvectionBoundary::ambient), Temperature>);
    static_assert(std::is_same_v<decltype(UniformInitialTemperature::value), Temperature>);
    static_assert(std::is_same_v<decltype(TransientSettings::timeStep), Time>);
    static_assert(std::is_same_v<decltype(TransientSettings::endTime), Time>);
    static_assert(std::is_same_v<decltype(TemperatureGradient3::x), TemperatureGradient>);
    static_assert(std::is_same_v<decltype(HeatFlux3::x), HeatFlux>);
    static_assert(std::is_same_v<decltype(HeatBalance::appliedSurfaceHeat), Power>);
    static_assert(std::is_same_v<decltype(HeatBalance::storedEnergyRate), std::optional<Power>>);
    static_assert(std::is_same_v<decltype(ThermalSolverSettings::relativeResidualTolerance), double>);

    // THE DIMENSIONS CLOSE. Fourier's law, convection and the balance are
    // dimensionally consistent IN THE TYPE SYSTEM, which is what makes a
    // swapped unit a compile error in P18-ELEM-001 rather than a wrong number.
    static_assert(std::is_same_v<decltype(std::declval<ThermalConductivity>() *
                                          std::declval<TemperatureGradient>()),
                                 HeatFlux>,
                  "q = -k grad(T)");
    static_assert(std::is_same_v<decltype(std::declval<HeatTransferCoefficient>() *
                                          std::declval<Temperature>()),
                                 HeatFlux>,
                  "q = h (T_ambient - T)");
    static_assert(std::is_same_v<decltype(std::declval<HeatFlux>() * std::declval<Area>()), Power>,
                  "a flux over a face is a power");
    static_assert(std::is_same_v<decltype(std::declval<VolumetricHeatSource>() *
                                          std::declval<Volume>()),
                                 Power>,
                  "generation over a volume is a power");
    static_assert(std::is_same_v<decltype(std::declval<Power>() * std::declval<Time>()), Energy>);
    static_assert(std::is_same_v<decltype(std::declval<Density>() * std::declval<SpecificHeatCapacity>() *
                                          std::declval<Volume>() * std::declval<Temperature>()),
                                 Energy>,
                  "rho cp V T is the stored energy");
    // And the two per-area coefficients are NOT the per-length one.
    static_assert(!std::is_same_v<HeatTransferCoefficient, ThermalConductivity>);
    static_assert(!std::is_same_v<HeatFlux, Power>);
    static_assert(!std::is_same_v<TemperatureGradient, Temperature>);

    // The literals and units reach the catalog.
    CHECK((5_W_per_m2).si() == 5.0);
    CHECK((2.5_K_per_m).si() == 2.5);
    CHECK((10_W_per_m2_K).si() == 10.0);
    CHECK((1000_W_per_m3).si() == 1000.0);
    CHECK((300_K).si() == 300.0);
    CHECK(findUnit("W/m^2").has_value());
    CHECK(findUnit("K/m").has_value());
    CHECK(findUnit("W/(m^2 K)").has_value());
    CHECK(findUnit("W/m^3").has_value());
    CHECK(findUnit("W/m^2")->dimension == dimensions::heatFlux);
    CHECK(findUnit("W/(m^2 K)")->dimension == dimensions::heatTransferCoefficient);
}

TEST_CASE("ThermalData_TemperaturesAreKelvinAndNoCelsiusUnitExists", "[thermal][data][units]") {
    // THE CELSIUS PROBE, as a fact about the whole catalog. An offset scale
    // needs an affine conversion the units framework does not have, and a
    // "degC" that scaled like a kelvin would read 20 degC as 20 K. There is
    // exactly one temperature unit, and it is absolute.
    std::vector<std::string_view> temperatureUnits;
    for (const UnitDescriptor& unit : unitCatalog()) {
        if (unit.dimension == dimensions::temperature) {
            temperatureUnits.push_back(unit.symbol);
        }
    }
    REQUIRE(temperatureUnits.size() == 1);
    CHECK(temperatureUnits.front() == "K");
    CHECK_FALSE(findUnit("degC").has_value());
    CHECK_FALSE(findUnit("C").has_value());
    CHECK_FALSE(findUnit("F").has_value());

    // And the absolute-temperature rule, in one place.
    CHECK(thermal::isAbsoluteTemperature(0_K));
    CHECK(thermal::isAbsoluteTemperature(293.15_K));
    CHECK_FALSE(thermal::isAbsoluteTemperature(Temperature::fromSi(-0.001)));
    CHECK_FALSE(thermal::isAbsoluteTemperature(Temperature::fromSi(kNaN)));
    CHECK_FALSE(thermal::isAbsoluteTemperature(Temperature::fromSi(kInf)));
    CHECK_FALSE(thermal::isAbsoluteTemperature(Temperature::fromSi(-kInf)));
}

TEST_CASE("ThermalData_VectorComponentOrderIsFrozen", "[thermal][data][convention]") {
    // Frozen so that P18-POST-001's recovery, a report and a renderer all
    // index the same thing. A module that chose its own order would swap two
    // components and produce a flux wrong in direction only.
    static_assert(static_cast<int>(VectorComponent::X) == 0);
    static_assert(static_cast<int>(VectorComponent::Y) == 1);
    static_assert(static_cast<int>(VectorComponent::Z) == 2);
    REQUIRE(thermal::kVectorComponents.size() == 3);
    CHECK(thermal::kVectorComponents[0] == VectorComponent::X);
    CHECK(thermal::kVectorComponents[1] == VectorComponent::Y);
    CHECK(thermal::kVectorComponents[2] == VectorComponent::Z);

    // The accessor agrees with the named field, for every component. If the
    // switch were ever reordered this fails rather than returning a neighbour.
    const TemperatureGradient3 gradient{.x = 1_K_per_m, .y = 2_K_per_m, .z = 3_K_per_m};
    CHECK(gradient.component(VectorComponent::X) == gradient.x);
    CHECK(gradient.component(VectorComponent::Y) == gradient.y);
    CHECK(gradient.component(VectorComponent::Z) == gradient.z);
    const HeatFlux3 flux{.x = 1_W_per_m2, .y = 2_W_per_m2, .z = 3_W_per_m2};
    CHECK(flux.component(VectorComponent::X) == flux.x);
    CHECK(flux.component(VectorComponent::Y) == flux.y);
    CHECK(flux.component(VectorComponent::Z) == flux.z);
    for (const VectorComponent which : thermal::kVectorComponents) {
        CHECK(thermal::toString(which).size() == 1);
    }
}

TEST_CASE("ThermalData_HeatFluxMagnitudeIsDerivedNotStored", "[thermal][data][convention]") {
    // Three components and nothing else: the magnitude is computed from them
    // every time, so it can never disagree with what it summarises.
    static_assert(sizeof(HeatFlux3) == 3 * sizeof(HeatFlux));
    static_assert(sizeof(TemperatureGradient3) == 3 * sizeof(TemperatureGradient));
    const HeatFlux3 flux{.x = 3_W_per_m2, .y = 4_W_per_m2, .z = 0_W_per_m2};
    CHECK_THAT(flux.magnitude().si(), WithinRel(5.0, 1e-15));
    CHECK(HeatFlux3{}.magnitude().si() == 0.0);
    CHECK_THAT((HeatFlux3{.x = -1_W_per_m2, .y = -1_W_per_m2, .z = -1_W_per_m2}).magnitude().si(),
               WithinRel(std::sqrt(3.0), 1e-15));
}

TEST_CASE("ThermalData_HeatBalanceSignConventionAndNormalization", "[thermal][data][convention]") {
    // HEAT INTO THE BODY IS POSITIVE, for every term, and the imbalance is
    // derived from the terms.
    SECTION("a balanced steady field") {
        const HeatBalance balance{.appliedSurfaceHeat = 100_W,
                                  .generatedHeat = 20_W,
                                  .convectionHeat = -70_W,
                                  .prescribedTemperatureReaction = -50_W};
        CHECK_THAT(balance.netImbalance().si(), Catch::Matchers::WithinAbs(0.0, 1e-12));
        CHECK_THAT(balance.scale().si(), WithinRel(240.0, 1e-15));
        REQUIRE(balance.normalizedImbalance().has_value());
        CHECK(*balance.normalizedImbalance() < 1e-15);
        CHECK_FALSE(balance.storedEnergyRate.has_value());
    }
    SECTION("an unbalanced field reports how far off, relative to its scale") {
        const HeatBalance balance{.appliedSurfaceHeat = 100_W, .prescribedTemperatureReaction = -90_W};
        CHECK_THAT(balance.netImbalance().si(), WithinRel(10.0, 1e-15));
        REQUIRE(balance.normalizedImbalance().has_value());
        CHECK_THAT(*balance.normalizedImbalance(), WithinRel(10.0 / 190.0, 1e-15));
    }
    SECTION("a transient field subtracts what is stored") {
        const HeatBalance balance{.appliedSurfaceHeat = 100_W,
                                  .prescribedTemperatureReaction = -60_W,
                                  .storedEnergyRate = 40_W};
        CHECK_THAT(balance.netImbalance().si(), Catch::Matchers::WithinAbs(0.0, 1e-12));
        CHECK_THAT(balance.scale().si(), WithinRel(200.0, 1e-15));
    }
    SECTION("no heat at all is not a perfect balance; the metric does not apply") {
        const HeatBalance nothing{};
        CHECK(nothing.netImbalance().si() == 0.0);
        CHECK_FALSE(nothing.normalizedImbalance().has_value());
    }
    SECTION("finiteness covers the optional term") {
        HeatBalance balance{.appliedSurfaceHeat = 1_W};
        CHECK(thermal::isFinite(balance));
        balance.storedEnergyRate = Power::fromSi(kNaN);
        CHECK_FALSE(thermal::isFinite(balance));
        balance.storedEnergyRate.reset();
        balance.convectionHeat = Power::fromSi(-kInf);
        CHECK_FALSE(thermal::isFinite(balance));
    }
}

TEST_CASE("ThermalData_TheModeIsAnEnumNotABool", "[thermal][data][convention]") {
    static_assert(std::is_enum_v<ThermalAnalysisMode>);
    static_assert(!std::is_same_v<ThermalAnalysisMode, bool>);
    static_assert(!std::is_convertible_v<bool, ThermalAnalysisMode>);
    static_assert(!std::is_convertible_v<ThermalAnalysisMode, bool>);
    CHECK(thermal::toString(ThermalAnalysisMode::Steady) == "steady");
    CHECK(thermal::toString(ThermalAnalysisMode::Transient) == "transient");
    CHECK(thermal::toString(TransientOutputPolicy::FinalOnly) == "final_only");
    CHECK(thermal::toString(TransientOutputPolicy::EveryStep) == "every_step");
    CHECK(thermal::toString(thermal::ThermalLoadKind::SurfaceHeatFlux) == "surface_heat_flux");
    CHECK(thermal::toString(thermal::ThermalLoadKind::VolumetricHeatSource) == "volumetric_heat_source");
    CHECK(thermal::toString(thermal::ThermalBoundaryConditionKind::PrescribedTemperature) ==
          "prescribed_temperature");
    CHECK(thermal::toString(thermal::ThermalBoundaryConditionKind::Convection) == "convection");
}

// ---------------------------------------------------------------------------
// Validation of canonical records
// ---------------------------------------------------------------------------

TEST_CASE("ThermalData_SolverSettingsAreValidatedFiniteAndPositive", "[thermal][data][settings]") {
    CHECK(thermal::validate(ThermalSolverSettings{}).has_value());
    CHECK(thermal::validate(ThermalSolverSettings{.relativeResidualTolerance = 1e-6, .pivotFloor = 1e-9})
              .has_value());
    for (const double bad : {0.0, -1e-9, kNaN, kInf}) {
        CAPTURE(bad);
        CHECK_FALSE(thermal::validate(ThermalSolverSettings{.relativeResidualTolerance = bad}).has_value());
        CHECK_FALSE(thermal::validate(ThermalSolverSettings{.pivotFloor = bad}).has_value());
    }
    const Result<void> nan = thermal::validate(ThermalSolverSettings{.relativeResidualTolerance = kNaN});
    REQUIRE_FALSE(nan.has_value());
    CHECK_THAT(nan.error().message, ContainsSubstring("finite and positive"));
}

TEST_CASE("ThermalData_TransientSettingsRequireAPositiveStepWithinTheEndTime",
          "[thermal][data][settings]") {
    CHECK(thermal::validate(TransientSettings{.timeStep = 0.5_s, .endTime = 10_s}).has_value());
    CHECK(thermal::validate(TransientSettings{.timeStep = 10_s, .endTime = 10_s}).has_value());
    CHECK(thermal::validate(TransientSettings{.timeStep = 1_s,
                                              .endTime = 3_s,
                                              .output = TransientOutputPolicy::EveryStep})
              .has_value());

    SECTION("dt must be positive and finite") {
        for (const double bad : {0.0, -0.5, kNaN, kInf}) {
            CAPTURE(bad);
            CHECK_FALSE(thermal::validate(TransientSettings{.timeStep = Time::fromSi(bad), .endTime = 10_s})
                            .has_value());
        }
        const Result<void> zero = thermal::validate(TransientSettings{.timeStep = 0_s, .endTime = 10_s});
        REQUIRE_FALSE(zero.has_value());
        CHECK_THAT(zero.error().message, ContainsSubstring("time step"));
    }
    SECTION("the end time must be positive and finite") {
        for (const double bad : {0.0, -1.0, kNaN, kInf}) {
            CAPTURE(bad);
            CHECK_FALSE(thermal::validate(TransientSettings{.timeStep = 0.5_s, .endTime = Time::fromSi(bad)})
                            .has_value());
        }
    }
    SECTION("the step cannot be longer than the span") {
        const Result<void> wrong = thermal::validate(TransientSettings{.timeStep = 11_s, .endTime = 10_s});
        REQUIRE_FALSE(wrong.has_value());
        CHECK_THAT(wrong.error().message, ContainsSubstring("shorter than the time step"));
    }
    SECTION("time is in seconds, through the units framework") {
        static_assert(std::is_same_v<decltype(TransientSettings::timeStep), Time>);
        CHECK((0.5_s).si() == 0.5);
        CHECK((2.0 * units::minute).si() == 120.0);
    }
}

TEST_CASE("ThermalData_InitialTemperatureMustBeAFiniteAbsoluteTemperature", "[thermal][data][settings]") {
    CHECK(thermal::validate(UniformInitialTemperature{.value = 293.15_K}).has_value());
    CHECK(thermal::validate(UniformInitialTemperature{.value = 0_K}).has_value());
    for (const double bad : {-0.001, -273.15, kNaN, kInf, -kInf}) {
        CAPTURE(bad);
        const Result<void> wrong = thermal::validate(UniformInitialTemperature{.value = Temperature::fromSi(bad)});
        REQUIRE_FALSE(wrong.has_value());
        CHECK_THAT(wrong.error().message, ContainsSubstring("absolute temperature"));
    }
}

TEST_CASE("ThermalData_LoadsAreValidatedOnTheirOwnTerms", "[thermal][data][load]") {
    ConductingStudy part;
    const ThermalLoadId id = ThermalLoadId::fromValue(1);

    SECTION("a well-formed flux load, positive or negative") {
        CHECK(thermal::validate(fluxOn(part.endCap(), id, 500.0)).has_value());
        CHECK(thermal::validate(fluxOn(part.endCap(), id, -500.0)).has_value());
        const ThermalLoad load = fluxOn(part.endCap(), id, 500.0);
        CHECK(load.kind() == thermal::ThermalLoadKind::SurfaceHeatFlux);
        REQUIRE(load.target().has_value());
        CHECK(*load.target() == part.endCap());
        CHECK(load.volumetricHeatSource() == nullptr);
    }
    SECTION("a well-formed volumetric source has no face") {
        const ThermalLoad load{id, VolumetricHeatSourceLoad{.generation = 1000_W_per_m3}};
        CHECK(thermal::validate(load).has_value());
        CHECK(load.kind() == thermal::ThermalLoadKind::VolumetricHeatSource);
        CHECK_FALSE(load.target().has_value());
        CHECK(load.surfaceHeatFlux() == nullptr);
    }
    SECTION("the invalid identity is refused") {
        const Result<void> wrong = thermal::validate(fluxOn(part.endCap(), ThermalLoadId{}, 500.0));
        REQUIRE_FALSE(wrong.has_value());
        CHECK_THAT(wrong.error().message, ContainsSubstring("valid ThermalLoadId"));
    }
    SECTION("a non-finite value is refused") {
        REQUIRE_FALSE(thermal::validate(fluxOn(part.endCap(), id, kNaN)).has_value());
        REQUIRE_FALSE(thermal::validate(fluxOn(part.endCap(), id, kInf)).has_value());
        const ThermalLoad source{id, VolumetricHeatSourceLoad{.generation = VolumetricHeatSource::fromSi(kNaN)}};
        const Result<void> wrong = thermal::validate(source);
        REQUIRE_FALSE(wrong.has_value());
        CHECK_THAT(wrong.error().message, ContainsSubstring("not finite"));
    }
    SECTION("a malformed face selector is refused by core's own check") {
        // A Side role with no entity is not a face anyone can resolve.
        const FaceName bad{part.feature, FaceSelector{.role = FaceRole::Side}};
        CHECK_FALSE(thermal::validate(fluxOn(bad, id, 500.0)).has_value());
    }
}

TEST_CASE("ThermalData_BoundaryConditionsAreValidatedOnTheirOwnTerms", "[thermal][data][bc]") {
    ConductingStudy part;
    const ThermalBoundaryConditionId id = ThermalBoundaryConditionId::fromValue(1);

    SECTION("a well-formed prescribed temperature") {
        const ThermalBoundaryCondition held = heldAt(part.startCap(), id, 350_K);
        CHECK(thermal::validate(held).has_value());
        CHECK(held.kind() == thermal::ThermalBoundaryConditionKind::PrescribedTemperature);
        CHECK(held.face() == part.startCap());
        CHECK(held.convection() == nullptr);
        CHECK(thermal::validate(heldAt(part.startCap(), id, 0_K)).has_value());
    }
    SECTION("a well-formed convection surface") {
        const ThermalBoundaryCondition convection{
            id, ConvectionBoundary{.face = part.side(), .coefficient = 25_W_per_m2_K, .ambient = 293.15_K}};
        CHECK(thermal::validate(convection).has_value());
        CHECK(convection.kind() == thermal::ThermalBoundaryConditionKind::Convection);
        CHECK(convection.face() == part.side());
        CHECK(convection.prescribedTemperature() == nullptr);
    }
    SECTION("the invalid identity is refused") {
        CHECK_FALSE(thermal::validate(heldAt(part.startCap(), ThermalBoundaryConditionId{}, 350_K))
                        .has_value());
    }
    SECTION("a prescribed temperature must be a finite absolute temperature") {
        for (const double bad : {-1.0, kNaN, kInf}) {
            CAPTURE(bad);
            const Result<void> wrong = thermal::validate(heldAt(part.startCap(), id, Temperature::fromSi(bad)));
            REQUIRE_FALSE(wrong.has_value());
            CHECK_THAT(wrong.error().message, ContainsSubstring("absolute temperature"));
        }
    }
    SECTION("a convection coefficient must be finite and positive") {
        for (const double bad : {0.0, -5.0, kNaN, kInf}) {
            CAPTURE(bad);
            const ThermalBoundaryCondition wrong{
                id, ConvectionBoundary{.face = part.side(),
                                       .coefficient = HeatTransferCoefficient::fromSi(bad),
                                       .ambient = 293.15_K}};
            const Result<void> checked = thermal::validate(wrong);
            REQUIRE_FALSE(checked.has_value());
            CHECK_THAT(checked.error().message, ContainsSubstring("convection coefficient"));
        }
    }
    SECTION("a convection ambient must be a finite absolute temperature") {
        for (const double bad : {-1.0, kNaN, kInf}) {
            CAPTURE(bad);
            const ThermalBoundaryCondition wrong{
                id, ConvectionBoundary{.face = part.side(), .coefficient = 25_W_per_m2_K,
                                       .ambient = Temperature::fromSi(bad)}};
            const Result<void> checked = thermal::validate(wrong);
            REQUIRE_FALSE(checked.has_value());
            CHECK_THAT(checked.error().message, ContainsSubstring("ambient"));
        }
    }
    SECTION("a malformed face selector is refused") {
        const FaceName bad{part.feature, FaceSelector{.role = FaceRole::Side}};
        CHECK_FALSE(thermal::validate(heldAt(bad, id, 350_K)).has_value());
    }
}

// ---------------------------------------------------------------------------
// The analysis object
// ---------------------------------------------------------------------------

TEST_CASE("ThermalData_TheDefinitionHoldsOnlyIntent", "[thermal][data][analysis]") {
    // THE INSTRUMENT P17 ARRIVED AT: state the types that MAY appear, in
    // order, and let an added field break a size equality. The mirror
    // declares the same seven members in the same order, so padding is the
    // same and any eighth field changes the size. A field added to the
    // definition by a later milestone extends this mirror, and extending it is
    // where that field's authority as CANONICAL INTENT is stated.
    struct PermittedDefinition {
        MeshControlId mesh;
        ThermalAnalysisMode mode;
        std::vector<ThermalLoad> loads;
        std::vector<ThermalBoundaryCondition> boundaryConditions;
        std::optional<UniformInitialTemperature> initialCondition;
        ThermalSolverSettings solver;
        std::optional<TransientSettings> transient;
    };
    static_assert(sizeof(ThermalAnalysisDefinition) == sizeof(PermittedDefinition),
                  "the thermal analysis definition holds a control, a mode, the loads, the "
                  "boundary conditions, an initial condition, the solver settings and the "
                  "transient settings -- nothing else. A temperature array, a result, a mesh "
                  "handle, a facet set, a DOF map or a matrix added to it breaks this");

    // And derived state cannot be put in it, which is the property the size
    // equality is a proxy for.
    static_assert(!std::is_constructible_v<ThermalAnalysisDefinition, ThermalResult>);
    static_assert(!std::is_constructible_v<ThermalAnalysisDefinition, meshing::MeshStamp>);
    static_assert(!std::is_constructible_v<ThermalAnalysisDefinition, std::vector<Temperature>>);
    static_assert(!std::is_constructible_v<ThermalAnalysisDefinition, HeatBalance>);
    static_assert(!std::is_constructible_v<UniformInitialTemperature, std::vector<Temperature>>,
                  "the initial condition is one value, never a per-node field");

    ConductingStudy part;
    const ThermalAnalysis& study = part.study();
    CHECK(study.typeName() == "thermal-analysis");
    CHECK(study.analysisId() == part.analysis);
    CHECK(study.definition().mesh == part.control);
    CHECK(study.definition().mode == ThermalAnalysisMode::Steady);

    // It depends on the control, so the document's graph knows that editing
    // the control -- or the body beneath it -- reaches this analysis.
    const std::vector<ObjectId> dependencies = study.dependencies();
    REQUIRE(dependencies.size() == 1);
    CHECK(dependencies.front() == ObjectId::fromValue(part.control.value()));

    // An object of another kind carrying the number is not an analysis.
    CHECK(thermal::findThermalAnalysis(part.document, ThermalAnalysisId::fromValue(part.control.value())) ==
          nullptr);
    CHECK(thermal::findThermalAnalysis(part.document, ThermalAnalysisId::fromValue(999999)) == nullptr);
}

TEST_CASE("ThermalData_TheDefinitionIsValidatedPerMode", "[thermal][data][analysis]") {
    ConductingStudy part;

    SECTION("the invalid control handle is refused") {
        CHECK_FALSE(thermal::validate(ThermalAnalysisDefinition{}).has_value());
    }
    SECTION("a steady analysis must not carry an initial condition") {
        ThermalAnalysisDefinition definition = part.steadyDefinition();
        definition.initialCondition = UniformInitialTemperature{.value = 293.15_K};
        const Result<void> wrong = thermal::validate(definition);
        REQUIRE_FALSE(wrong.has_value());
        CHECK_THAT(wrong.error().message, ContainsSubstring("no initial condition"));
    }
    SECTION("a steady analysis must not carry transient settings") {
        ThermalAnalysisDefinition definition = part.steadyDefinition();
        definition.transient = TransientSettings{.timeStep = 1_s, .endTime = 2_s};
        const Result<void> wrong = thermal::validate(definition);
        REQUIRE_FALSE(wrong.has_value());
        CHECK_THAT(wrong.error().message, ContainsSubstring("no time step"));
    }
    SECTION("a transient analysis needs an initial condition") {
        ThermalAnalysisDefinition definition = part.transientDefinition();
        definition.initialCondition.reset();
        const Result<void> wrong = thermal::validate(definition);
        REQUIRE_FALSE(wrong.has_value());
        CHECK_THAT(wrong.error().message, ContainsSubstring("initial temperature"));
    }
    SECTION("a transient analysis needs transient settings, and valid ones") {
        ThermalAnalysisDefinition definition = part.transientDefinition();
        definition.transient.reset();
        REQUIRE_FALSE(thermal::validate(definition).has_value());
        definition.transient = TransientSettings{.timeStep = 0_s, .endTime = 10_s};
        REQUIRE_FALSE(thermal::validate(definition).has_value());
        definition.initialCondition = UniformInitialTemperature{.value = Temperature::fromSi(-5.0)};
        definition.transient = TransientSettings{.timeStep = 1_s, .endTime = 10_s};
        REQUIRE_FALSE(thermal::validate(definition).has_value());
    }
    SECTION("the complete transient definition is accepted and can enter a document") {
        REQUIRE(thermal::validate(part.transientDefinition()).has_value());
        REQUIRE(part.setDefinition(part.transientDefinition()).has_value());
        CHECK(part.definition().mode == ThermalAnalysisMode::Transient);
    }
    SECTION("invalid solver settings are refused through the definition") {
        ThermalAnalysisDefinition definition = part.steadyDefinition();
        definition.solver.pivotFloor = -1.0;
        CHECK_FALSE(thermal::validate(definition).has_value());
    }
    SECTION("a duplicate load identity is refused") {
        ThermalAnalysisDefinition definition = part.steadyDefinition();
        definition.loads.push_back(fluxOn(part.endCap(), ThermalLoadId::fromValue(1), 100.0));
        definition.loads.push_back(fluxOn(part.startCap(), ThermalLoadId::fromValue(1), 200.0));
        const Result<void> wrong = thermal::validate(definition);
        REQUIRE_FALSE(wrong.has_value());
        CHECK_THAT(wrong.error().message, ContainsSubstring("appears twice among the loads"));
    }
    SECTION("a duplicate boundary condition identity is refused") {
        ThermalAnalysisDefinition definition = part.steadyDefinition();
        definition.boundaryConditions.push_back(heldAt(part.startCap(), ThermalBoundaryConditionId::fromValue(4), 300_K));
        definition.boundaryConditions.push_back(heldAt(part.endCap(), ThermalBoundaryConditionId::fromValue(4), 400_K));
        const Result<void> wrong = thermal::validate(definition);
        REQUIRE_FALSE(wrong.has_value());
        CHECK_THAT(wrong.error().message, ContainsSubstring("appears twice among the boundary conditions"));
    }
    SECTION("a malformed load is refused through the definition, and create refuses it too") {
        ThermalAnalysisDefinition definition = part.steadyDefinition();
        definition.loads.push_back(fluxOn(part.endCap(), ThermalLoadId::fromValue(1), kNaN));
        REQUIRE_FALSE(thermal::validate(definition).has_value());
        REQUIRE_FALSE(ThermalAnalysis::create("Bad", definition).has_value());
        REQUIRE_FALSE(part.setDefinition(definition).has_value());
    }
    SECTION("loads and boundary conditions in the same document order are equal intent") {
        ThermalAnalysisDefinition a = part.steadyDefinition();
        a.loads.push_back(fluxOn(part.endCap(), ThermalLoadId::fromValue(1), 100.0));
        a.loads.push_back(fluxOn(part.startCap(), ThermalLoadId::fromValue(2), 200.0));
        ThermalAnalysisDefinition b = a;
        CHECK(a == b);
        std::swap(b.loads[0], b.loads[1]);
        // THE ORDER IS CANONICAL STATE. Two definitions with the same records
        // in a different order are different intent, as they are for P17.
        CHECK_FALSE(a == b);
    }
}

TEST_CASE("ThermalData_ANoOpEditDoesNotMoveTheRevision", "[thermal][data][analysis]") {
    // The precision that keeps a result current across a re-set of the same
    // value. The contract MeshControl::setDefinition established.
    ConductingStudy part;
    const std::uint64_t before = part.revision();
    const Result<bool> noop = part.setDefinition(part.steadyDefinition());
    REQUIRE(noop.has_value());
    CHECK_FALSE(*noop);
    CHECK(part.revision() == before);

    ThermalAnalysisDefinition changed = part.steadyDefinition();
    changed.solver.relativeResidualTolerance = 1e-8;
    const Result<bool> effective = part.setDefinition(changed);
    REQUIRE(effective.has_value());
    CHECK(*effective);
    CHECK(part.revision() == before + 1);
}

TEST_CASE("ThermalData_AddUndoRedoRestoresTheAnalysisExactly", "[thermal][data][analysis][history]") {
    // THE COMMAND-HISTORY AUDIT, as a test. core's AddObjectCommand keeps the
    // removed object on undo and re-inserts clone() on redo; the history
    // stores COMMANDS, never a snapshot of the document, so there is nowhere
    // a result array could be captured. What comes back is the same identity
    // and the same intent.
    ConductingStudy part;
    CommandHistory history;
    ThermalAnalysisDefinition definition = part.steadyDefinition();
    definition.loads.push_back(fluxOn(part.endCap(), ThermalLoadId::fromValue(1), 250.0));
    definition.boundaryConditions.push_back(heldAt(part.startCap(), ThermalBoundaryConditionId::fromValue(1), 300_K));
    auto study = ThermalAnalysis::create("Second", definition);
    REQUIRE(study.has_value());
    auto command = std::make_unique<AddObjectCommand>(std::move(*study));
    const AddObjectCommand* raw = command.get();
    const std::size_t before = part.document.objectCount();
    REQUIRE(history.execute(part.document, std::move(command)).has_value());

    const ThermalAnalysisId added = ThermalAnalysisId::fromValue(raw->objectId().value());
    REQUIRE(added.isValid());
    REQUIRE(thermal::findThermalAnalysis(part.document, added) != nullptr);
    CHECK(part.document.objectCount() == before + 1U);

    REQUIRE(history.undo(part.document).has_value());
    CHECK(thermal::findThermalAnalysis(part.document, added) == nullptr);
    CHECK(part.document.objectCount() == before);

    REQUIRE(history.redo(part.document).has_value());
    const ThermalAnalysis* restored = thermal::findThermalAnalysis(part.document, added);
    REQUIRE(restored != nullptr);
    CHECK(restored->analysisId() == added);
    CHECK(restored->definition() == definition);
    CHECK(restored->name() == "Second");

    // And the roster of analyses is deterministic: ascending by identity.
    const std::vector<ThermalAnalysisId> ids = thermal::thermalAnalysisIds(part.document);
    REQUIRE(ids.size() == 2);
    CHECK(ids == std::vector{part.analysis, added});
    CHECK(std::ranges::is_sorted(ids, [](ThermalAnalysisId a, ThermalAnalysisId b) {
        return a.value() < b.value();
    }));
}

TEST_CASE("ThermalData_SavingADocumentWithAThermalAnalysisIsRefusedUntilPersistenceExists",
          "[thermal][data][analysis][persistence]") {
    // THE PERSISTENCE AUDIT, as a test. P18-PERSIST-001 has not happened, so
    // the document format has no representation of a thermal analysis. The
    // honest behaviour is a REFUSAL THAT NAMES THE TYPE -- not a file with the
    // analysis silently dropped, which a user would read back as "my analysis
    // was lost", and not a file carrying a result array, which this milestone
    // forbids outright. This test pins the current behaviour so that the
    // persistence milestone replaces a known refusal rather than a surprise.
    ConductingStudy part;
    const Result<std::string> json = io::documentToJson(part.document);
    REQUIRE_FALSE(json.has_value());
    CHECK_THAT(json.error().message, ContainsSubstring("thermal-analysis"));
    CHECK_THAT(json.error().message, ContainsSubstring("cannot be saved"));
}

// ---------------------------------------------------------------------------
// The result container
// ---------------------------------------------------------------------------

TEST_CASE("ThermalData_AResultMustMatchItsMeshExactly", "[thermal][data][result]") {
    ConductingStudy part;
    part.requireMesh();
    const ThermalResultSource source = part.currentSource();
    const meshing::VolumeMesh& mesh = part.mesh();
    const std::size_t nodes = mesh.nodeCount();
    const std::size_t tetrahedra = mesh.tetrahedronCount();
    REQUIRE(nodes > 0);
    REQUIRE(tetrahedra > 0);

    const auto attempt = [&](std::vector<Temperature> temperatures, std::vector<TemperatureGradient3> gradients,
                             std::vector<HeatFlux3> fluxes, HeatBalance balance, std::optional<Time> time,
                             ThermalResultSource with = {}) {
        if (!with.mesh.isValid()) {
            with = source;
        }
        return ThermalResult::create(with, mesh, std::move(temperatures), std::move(gradients),
                                     std::move(fluxes), balance, time);
    };
    const std::vector<Temperature> okTemperatures(nodes, 300_K);
    const std::vector<TemperatureGradient3> okGradients(tetrahedra);
    const std::vector<HeatFlux3> okFluxes(tetrahedra);

    SECTION("the right shape is accepted") {
        REQUIRE(attempt(okTemperatures, okGradients, okFluxes, {}, std::nullopt).has_value());
    }
    SECTION("one temperature per node") {
        const Result<ThermalResult> wrong =
            attempt(std::vector<Temperature>(nodes - 1, 300_K), okGradients, okFluxes, {}, std::nullopt);
        REQUIRE_FALSE(wrong.has_value());
        CHECK_THAT(wrong.error().message, ContainsSubstring("temperatures for"));
    }
    SECTION("one gradient and one flux per tetrahedron") {
        REQUIRE_FALSE(attempt(okTemperatures, std::vector<TemperatureGradient3>(tetrahedra + 1), okFluxes, {},
                              std::nullopt)
                          .has_value());
        const Result<ThermalResult> wrong =
            attempt(okTemperatures, okGradients, std::vector<HeatFlux3>(tetrahedra - 1), {}, std::nullopt);
        REQUIRE_FALSE(wrong.has_value());
        CHECK_THAT(wrong.error().message, ContainsSubstring("tetrahedra"));
    }
    SECTION("the source must name this mesh") {
        ThermalResultSource foreign = source;
        foreign.mesh.generation += 1;
        const Result<ThermalResult> wrong = attempt(okTemperatures, okGradients, okFluxes, {}, std::nullopt, foreign);
        REQUIRE_FALSE(wrong.has_value());
        CHECK_THAT(wrong.error().message, ContainsSubstring("does not name this mesh"));
    }
    SECTION("no temperature may be non-finite or below absolute zero") {
        for (const double bad : {kNaN, kInf, -1.0}) {
            CAPTURE(bad);
            std::vector<Temperature> temperatures = okTemperatures;
            temperatures.back() = Temperature::fromSi(bad);
            const Result<ThermalResult> wrong = attempt(temperatures, okGradients, okFluxes, {}, std::nullopt);
            REQUIRE_FALSE(wrong.has_value());
            CHECK_THAT(wrong.error().message, ContainsSubstring("absolute temperature"));
        }
    }
    SECTION("no gradient or flux component may be non-finite") {
        std::vector<TemperatureGradient3> gradients = okGradients;
        gradients.front().y = TemperatureGradient::fromSi(kNaN);
        REQUIRE_FALSE(attempt(okTemperatures, gradients, okFluxes, {}, std::nullopt).has_value());
        std::vector<HeatFlux3> fluxes = okFluxes;
        fluxes.front().z = HeatFlux::fromSi(kInf);
        const Result<ThermalResult> wrong = attempt(okTemperatures, okGradients, fluxes, {}, std::nullopt);
        REQUIRE_FALSE(wrong.has_value());
        CHECK_THAT(wrong.error().message, ContainsSubstring("heat flux"));
    }
    SECTION("no balance term may be non-finite") {
        const Result<ThermalResult> wrong =
            attempt(okTemperatures, okGradients, okFluxes, HeatBalance{.generatedHeat = Power::fromSi(kNaN)},
                    std::nullopt);
        REQUIRE_FALSE(wrong.has_value());
        CHECK_THAT(wrong.error().message, ContainsSubstring("balance"));
    }
    SECTION("a steady result has no time and no stored-energy rate") {
        const Result<ThermalResult> timed = attempt(okTemperatures, okGradients, okFluxes, {}, 1_s);
        REQUIRE_FALSE(timed.has_value());
        CHECK_THAT(timed.error().message, ContainsSubstring("steady result has no time"));
        const Result<ThermalResult> stored =
            attempt(okTemperatures, okGradients, okFluxes, HeatBalance{.storedEnergyRate = 0_W}, std::nullopt);
        REQUIRE_FALSE(stored.has_value());
        CHECK_THAT(stored.error().message, ContainsSubstring("not zero"));
    }
    SECTION("a transient result needs a time and a stored-energy rate") {
        ThermalResultSource transient = source;
        transient.mode = ThermalAnalysisMode::Transient;
        REQUIRE_FALSE(attempt(okTemperatures, okGradients, okFluxes, HeatBalance{.storedEnergyRate = 0_W},
                              std::nullopt, transient)
                          .has_value());
        REQUIRE_FALSE(attempt(okTemperatures, okGradients, okFluxes, HeatBalance{.storedEnergyRate = 0_W},
                              Time::fromSi(-1.0), transient)
                          .has_value());
        REQUIRE_FALSE(attempt(okTemperatures, okGradients, okFluxes, HeatBalance{.storedEnergyRate = 0_W},
                              Time::fromSi(kNaN), transient)
                          .has_value());
        const Result<ThermalResult> noRate = attempt(okTemperatures, okGradients, okFluxes, {}, 1_s, transient);
        REQUIRE_FALSE(noRate.has_value());
        CHECK_THAT(noRate.error().message, ContainsSubstring("stored-energy rate"));
        const Result<ThermalResult> ok =
            attempt(okTemperatures, okGradients, okFluxes, HeatBalance{.storedEnergyRate = 2_W}, 0_s, transient);
        REQUIRE(ok.has_value());
        REQUIRE(ok->time().has_value());
        CHECK(ok->time()->si() == 0.0);
        CHECK(ok->source().mode == ThermalAnalysisMode::Transient);
    }
}

TEST_CASE("ThermalData_SameIdsNewMeshGenerationIsStale", "[thermal][data][currentness]") {
    // THE CENTRAL CLAIM OF THE MILESTONE. Remeshing the same body with the
    // same settings gives a mesh with the same node count, the same element
    // count AND THE SAME NodeIds -- and it is a different mesh. A result read
    // across that gap would map every temperature onto whatever material now
    // sits at that number. Counts are not identity; IDs are not identity; the
    // stamp is.
    ConductingStudy part;
    part.requireMesh();
    const ThermalResultSource before = part.currentSource();
    const ThermalResult result = part.resultFor(before);
    CHECK(result.describes(part.mesh()));
    CHECK(thermal::resultCurrency(&result, before) == ResultCurrency::Current);

    std::vector<meshing::NodeId> idsBefore;
    for (const meshing::Node& node : part.mesh().mesh().nodes()) {
        idsBefore.push_back(node.id);
    }
    const std::size_t tetrahedraBefore = part.mesh().tetrahedronCount();
    const meshing::MeshStamp stampBefore = part.mesh().mesh().stamp();

    part.requireMesh(); // the same body, the same settings, a new generation
    const meshing::VolumeMesh& remeshed = part.mesh();
    std::vector<meshing::NodeId> idsAfter;
    for (const meshing::Node& node : remeshed.mesh().nodes()) {
        idsAfter.push_back(node.id);
    }
    REQUIRE(idsAfter == idsBefore);
    REQUIRE(remeshed.tetrahedronCount() == tetrahedraBefore);
    REQUIRE_FALSE(remeshed.mesh().stamp() == stampBefore);

    CHECK_FALSE(result.describes(remeshed));
    const ThermalResultSource after = part.currentSource();
    CHECK(thermal::resultCurrency(&result, after) == ResultCurrency::Stale);
    CHECK(thermal::staleReasons(result.source(), after) == std::vector{StaleReason::Mesh});
    CHECK(part.state(&result, nullptr) == AnalysisState::SolvedStale);
    const Result<Temperature> refused = result.temperatureOf(remeshed, idsAfter.front());
    REQUIRE_FALSE(refused.has_value());
    CHECK_THAT(refused.error().message, ContainsSubstring("not computed on that mesh"));

    WARN(std::format("remesh of the same body: {} nodes with identical NodeIds and {} tetrahedra "
                     "either way; stamps differ; result stale for reason mesh",
                     idsBefore.size(), tetrahedraBefore));
}

TEST_CASE("ThermalData_SameContentNewSourceGenerationIsStale", "[thermal][data][currentness]") {
    // CURRENTNESS COMPARES SOURCE IDENTITY, NEVER ARRAYS. Two results whose
    // every value is identical, computed on two generations of the mesh: the
    // old one is stale and the new one is current, and nothing about their
    // contents could have told them apart.
    ConductingStudy part;
    part.requireMesh();
    const ThermalResult old = part.resultFor(part.currentSource(), 310_K);
    part.requireMesh();
    const ThermalResultSource now = part.currentSource();
    const ThermalResult fresh = part.resultFor(now, 310_K);

    REQUIRE(std::ranges::equal(old.temperatures(), fresh.temperatures()));
    REQUIRE(std::ranges::equal(old.gradients(), fresh.gradients()));
    REQUIRE(std::ranges::equal(old.heatFluxes(), fresh.heatFluxes()));
    REQUIRE(old.heatBalance() == fresh.heatBalance());
    CHECK(thermal::resultCurrency(&old, now) == ResultCurrency::Stale);
    CHECK(thermal::resultCurrency(&fresh, now) == ResultCurrency::Current);
    CHECK_FALSE(old.source() == fresh.source());
}

TEST_CASE("ThermalData_CurrentnessComparesSourceIdentityNeverArraysOrSizes", "[thermal][data][currentness]") {
    // The converse: two results with DIFFERENT values on the same generation
    // are both current. A currentness check that read values, or that read
    // sizes, would answer differently; this one reads neither.
    ConductingStudy part;
    part.requireMesh();
    const ThermalResultSource source = part.currentSource();
    const ThermalResult cold = part.resultFor(source, 250_K);
    const ThermalResult hot = part.resultFor(source, 450_K);
    REQUIRE_FALSE(std::ranges::equal(cold.temperatures(), hot.temperatures()));
    CHECK(thermal::resultCurrency(&cold, source) == ResultCurrency::Current);
    CHECK(thermal::resultCurrency(&hot, source) == ResultCurrency::Current);
    CHECK(cold.source() == hot.source());
}

TEST_CASE("ThermalData_SourceFingerprintIsSensitiveToEveryField", "[thermal][data][currentness]") {
    // THE TRUTH TABLE, one row per dependency, each mutated ALONE. A
    // comparison that omitted any one of these would pass every other row,
    // which is why each is its own section rather than one combined check.
    ConductingStudy part;
    part.requireMesh();
    const ThermalResultSource source = part.currentSource();

    SECTION("nothing changed") {
        CHECK(thermal::staleReasons(source, source).empty());
    }
    SECTION("the body") {
        ThermalResultSource other = source;
        other.body = ObjectId::fromValue(other.body.value() + 1);
        CHECK(thermal::staleReasons(source, other) == std::vector{StaleReason::Body});
    }
    SECTION("the mesh control") {
        ThermalResultSource other = source;
        other.control = MeshControlId::fromValue(other.control.value() + 1);
        CHECK(thermal::staleReasons(source, other) == std::vector{StaleReason::Control});
    }
    SECTION("the geometry revision") {
        ThermalResultSource other = source;
        other.geometry.value += 1;
        CHECK(thermal::staleReasons(source, other) == std::vector{StaleReason::Geometry});
    }
    SECTION("the mesh generation alone") {
        ThermalResultSource other = source;
        other.mesh.generation += 1;
        CHECK(thermal::staleReasons(source, other) == std::vector{StaleReason::Mesh});
    }
    SECTION("the mesh identity alone") {
        ThermalResultSource other = source;
        other.mesh.mesh = meshing::MeshId::fromValue(other.mesh.mesh.value() + 1);
        CHECK(thermal::staleReasons(source, other) == std::vector{StaleReason::Mesh});
    }
    SECTION("the material") {
        ThermalResultSource other = source;
        other.material = MaterialId::fromValue(other.material.value() + 1);
        CHECK(thermal::staleReasons(source, other) == std::vector{StaleReason::Material});
    }
    SECTION("the material revision alone") {
        ThermalResultSource other = source;
        other.materialRevision += 1;
        CHECK(thermal::staleReasons(source, other) == std::vector{StaleReason::Material});
    }
    SECTION("the analysis") {
        ThermalResultSource other = source;
        other.analysis = ThermalAnalysisId::fromValue(other.analysis.value() + 1);
        CHECK(thermal::staleReasons(source, other) == std::vector{StaleReason::Analysis});
    }
    SECTION("the analysis revision alone -- loads, BCs, initial condition, solver, transient") {
        ThermalResultSource other = source;
        other.analysisRevision += 1;
        CHECK(thermal::staleReasons(source, other) == std::vector{StaleReason::Analysis});
    }
    SECTION("the mode alone") {
        ThermalResultSource other = source;
        other.mode = ThermalAnalysisMode::Transient;
        CHECK(thermal::staleReasons(source, other) == std::vector{StaleReason::Mode});
    }
    SECTION("several at once are all reported, in enum order") {
        ThermalResultSource other = source;
        other.mode = ThermalAnalysisMode::Transient;
        other.materialRevision += 1;
        other.geometry.value += 1;
        CHECK(thermal::staleReasons(source, other) ==
              std::vector{StaleReason::Geometry, StaleReason::Material, StaleReason::Mode});
    }
    SECTION("every mutation above also fails equality, so currency says stale") {
        ThermalResultSource other = source;
        other.analysisRevision += 1;
        const ThermalResult result = part.resultFor(source);
        CHECK(thermal::resultCurrency(&result, other) == ResultCurrency::Stale);
        CHECK(thermal::resultCurrency(nullptr, other) == ResultCurrency::NoResult);
        CHECK(thermal::describesTheModel(ResultCurrency::Current));
        CHECK_FALSE(thermal::describesTheModel(ResultCurrency::Stale));
        CHECK_FALSE(thermal::describesTheModel(ResultCurrency::NoResult));
    }
}

TEST_CASE("ThermalData_EveryDefinitionEditMakesTheResultStale", "[thermal][data][currentness]") {
    // Not a mutated struct this time: a real edit to the real document, one
    // field at a time, and the mesh must stay current throughout -- a change
    // to loads, boundary conditions, settings or the initial condition is not
    // a reason to remesh.
    const auto expectStale = [](ConductingStudy& part, const ThermalResult& result,
                                const std::vector<StaleReason>& reasons) {
        CHECK(part.mesher.currency(part.document, part.control) == meshing::MeshCurrency::Current);
        const ThermalResultSource after = part.currentSource();
        CHECK(thermal::resultCurrency(&result, after) == ResultCurrency::Stale);
        CHECK(thermal::staleReasons(result.source(), after) == reasons);
        CHECK(part.state(&result, nullptr) == AnalysisState::SolvedStale);
    };

    SECTION("steady analysis") {
        ConductingStudy part;
        part.requireMesh();
        const ThermalResult result = part.resultFor(part.currentSource());
        REQUIRE(thermal::resultCurrency(&result, part.currentSource()) == ResultCurrency::Current);
        ThermalAnalysisDefinition definition = part.steadyDefinition();

        SECTION("a heat flux load is added") {
            definition.loads.push_back(fluxOn(part.endCap(), ThermalLoadId::fromValue(1), 500.0));
            REQUIRE(part.setDefinition(definition).has_value());
            expectStale(part, result, {StaleReason::Analysis});
        }
        SECTION("a volumetric source is added") {
            definition.loads.emplace_back(ThermalLoadId::fromValue(1),
                                          VolumetricHeatSourceLoad{.generation = 1000_W_per_m3});
            REQUIRE(part.setDefinition(definition).has_value());
            expectStale(part, result, {StaleReason::Analysis});
        }
        SECTION("a prescribed temperature is added") {
            definition.boundaryConditions.push_back(
                heldAt(part.startCap(), ThermalBoundaryConditionId::fromValue(1), 300_K));
            REQUIRE(part.setDefinition(definition).has_value());
            expectStale(part, result, {StaleReason::Analysis});
        }
        SECTION("a convection surface is added") {
            definition.boundaryConditions.emplace_back(
                ThermalBoundaryConditionId::fromValue(1),
                ConvectionBoundary{.face = part.side(), .coefficient = 25_W_per_m2_K, .ambient = 293.15_K});
            REQUIRE(part.setDefinition(definition).has_value());
            expectStale(part, result, {StaleReason::Analysis});
        }
        SECTION("a load's value is edited") {
            definition.loads.push_back(fluxOn(part.endCap(), ThermalLoadId::fromValue(1), 500.0));
            REQUIRE(part.setDefinition(definition).has_value());
            const ThermalResult loaded = part.resultFor(part.currentSource());
            definition.loads.back() = fluxOn(part.endCap(), ThermalLoadId::fromValue(1), 600.0);
            REQUIRE(part.setDefinition(definition).has_value());
            expectStale(part, loaded, {StaleReason::Analysis});
        }
        SECTION("the solver tolerance") {
            definition.solver.relativeResidualTolerance = 1e-10;
            REQUIRE(part.setDefinition(definition).has_value());
            expectStale(part, result, {StaleReason::Analysis});
        }
        SECTION("the mode becomes transient") {
            REQUIRE(part.setDefinition(part.transientDefinition()).has_value());
            expectStale(part, result, {StaleReason::Analysis, StaleReason::Mode});
        }
    }
    SECTION("transient analysis") {
        ConductingStudy part{ThermalAnalysisMode::Transient};
        part.requireMesh();
        const ThermalResult result = part.resultFor(part.currentSource());
        REQUIRE(result.time().has_value());
        REQUIRE(thermal::resultCurrency(&result, part.currentSource()) == ResultCurrency::Current);
        ThermalAnalysisDefinition definition = part.transientDefinition();

        SECTION("the time step") {
            definition.transient->timeStep = 0.25_s;
            REQUIRE(part.setDefinition(definition).has_value());
            expectStale(part, result, {StaleReason::Analysis});
        }
        SECTION("the end time") {
            definition.transient->endTime = 20_s;
            REQUIRE(part.setDefinition(definition).has_value());
            expectStale(part, result, {StaleReason::Analysis});
        }
        SECTION("the output policy") {
            definition.transient->output = TransientOutputPolicy::EveryStep;
            REQUIRE(part.setDefinition(definition).has_value());
            expectStale(part, result, {StaleReason::Analysis});
        }
        SECTION("the initial temperature") {
            definition.initialCondition->value = 300_K;
            REQUIRE(part.setDefinition(definition).has_value());
            expectStale(part, result, {StaleReason::Analysis});
        }
        SECTION("the mode becomes steady") {
            REQUIRE(part.setDefinition(part.steadyDefinition()).has_value());
            expectStale(part, result, {StaleReason::Analysis, StaleReason::Mode});
        }
    }
}

TEST_CASE("ThermalData_AMaterialEditStalesTheResultAndNotTheMesh", "[thermal][data][currentness]") {
    // A mesh is a function of geometry and meshing intent; k is in neither.
    // An architecture that invalidated the mesh on a conductivity change
    // would remesh a hundred thousand elements to answer a question about a
    // number.
    ConductingStudy part;
    part.requireMesh();
    const ThermalResultSource before = part.currentSource();
    const ThermalResult result = part.resultFor(before);
    CHECK(thermal::resultCurrency(&result, before) == ResultCurrency::Current);

    materials::ThermalProperties thermalProperties =
        features::findMaterial(part.document, part.material)->definition().thermal;
    thermalProperties.thermalConductivity = materials::MaterialProperty<ThermalConductivity>::known(50_W_per_m_K);
    REQUIRE(features::setMaterialThermal(part.document, part.material, thermalProperties).has_value());

    CHECK(part.mesher.currency(part.document, part.control) == meshing::MeshCurrency::Current);
    const ThermalResultSource after = part.currentSource();
    CHECK(after.materialRevision != before.materialRevision);
    CHECK(after.mesh == before.mesh);
    CHECK(after.analysisRevision == before.analysisRevision);
    CHECK(thermal::resultCurrency(&result, after) == ResultCurrency::Stale);
    CHECK(thermal::staleReasons(result.source(), after) == std::vector{StaleReason::Material});
    CHECK(part.state(&result, nullptr) == AnalysisState::SolvedStale);

    WARN("a thermal conductivity edit, 45 -> 50 W/(m K): mesh current, result stale, reason material");
}

TEST_CASE("ThermalData_AGeometryEditMakesInputsUnavailable", "[thermal][data][currentness]") {
    ConductingStudy part;
    part.requireMesh();
    const ThermalResult result = part.resultFor(part.currentSource());

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

    // The input boundary refuses before a source can even be computed, which
    // is the strongest form of stale: there is no current state to compare
    // with, and the surviving result is not offered as anything.
    const Result<ThermalResultSource> current =
        thermal::currentResultSource(part.document, part.regenerator, part.mesher, part.analysis);
    REQUIRE_FALSE(current.has_value());
    CHECK(part.state(&result, nullptr) == AnalysisState::InputsUnavailable);
}

TEST_CASE("ThermalData_TheSourceIsPopulatedFromTheDocumentAndNotDefaulted", "[thermal][data][currentness]") {
    // The truth table mutates a STRUCT. This proves `currentResultSource`
    // actually READS each field from the document: a forgotten field would be
    // zero, equal before and after, and every truth-table row would still pass.
    ConductingStudy part;
    part.requireMesh();
    const ThermalResultSource source = part.currentSource();
    CHECK(source.body == part.feature);
    CHECK(source.control == part.control);
    CHECK(source.geometry.isValid());
    CHECK(source.mesh == part.mesh().mesh().stamp());
    CHECK(source.material == part.material);
    CHECK(source.materialRevision != 0);
    CHECK(source.materialRevision == features::findMaterial(part.document, part.material)->revision());
    CHECK(source.analysis == part.analysis);
    CHECK(source.analysisRevision != 0);
    CHECK(source.analysisRevision == part.revision());
    CHECK(source.mode == ThermalAnalysisMode::Steady);

    REQUIRE(part.setDefinition(part.transientDefinition()).has_value());
    const ThermalResultSource after = part.currentSource();
    CHECK(after.mode == ThermalAnalysisMode::Transient);
    CHECK(after.analysisRevision == source.analysisRevision + 1);
    CHECK(after.analysisRevision == part.revision());
}

TEST_CASE("ThermalData_AViewerOnlyChangeDoesNotStaleAResult", "[thermal][data][currentness]") {
    // Presentation state is not a dependency. A quality THRESHOLD edit changes
    // how an element is classified and can never change the element, so P16
    // keeps the mesh current -- and a result computed on it stays current too.
    ConductingStudy part;
    part.requireMesh();
    const ThermalResult result = part.resultFor(part.currentSource());
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
    CHECK(thermal::resultCurrency(&result, part.currentSource()) == ResultCurrency::Current);
    CHECK(part.state(&result, nullptr) == AnalysisState::SolvedCurrent);
}

// ---------------------------------------------------------------------------
// The result lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("ThermalData_FailedAttemptIsNotCurrent", "[thermal][data][lifecycle]") {
    ConductingStudy part;
    part.requireMesh();
    ThermalResultSlot slot;
    CHECK(slot.result() == nullptr);
    CHECK(slot.failure() == nullptr);
    CHECK(part.state(slot.result(), slot.failure()) == AnalysisState::Ready);

    slot.recordFailure(makeError(ErrorCode::Internal, "the solver gave up").error());
    CHECK(slot.result() == nullptr);
    REQUIRE(slot.failure() != nullptr);
    CHECK_THAT(slot.failure()->message, ContainsSubstring("gave up"));
    CHECK(part.state(slot.result(), slot.failure()) == AnalysisState::SolveFailed);
    CHECK(thermal::resultCurrency(slot.result(), part.currentSource()) == ResultCurrency::NoResult);

    // A later success clears the failure and is the first result held.
    slot.recordSuccess(part.resultFor(part.currentSource()));
    CHECK(slot.failure() == nullptr);
    REQUIRE(slot.result() != nullptr);
    CHECK(part.state(slot.result(), slot.failure()) == AnalysisState::SolvedCurrent);

    slot.reset();
    CHECK(slot.result() == nullptr);
    CHECK(slot.failure() == nullptr);
    CHECK(part.state(slot.result(), slot.failure()) == AnalysisState::Ready);
}

TEST_CASE("ThermalData_FailedAttemptDoesNotResurrectStaleResult", "[thermal][data][lifecycle]") {
    // The sequence: solve, remesh (result goes stale), attempt to re-solve and
    // FAIL. The stale result must survive untouched -- same object, same
    // source, still stale -- the state must say the attempt failed, and
    // nothing in the slot may present that result as current.
    ConductingStudy part;
    part.requireMesh();
    ThermalResultSlot slot;
    slot.recordSuccess(part.resultFor(part.currentSource()));
    const ThermalResultSource solvedOn = slot.result()->source();
    CHECK(part.state(slot.result(), slot.failure()) == AnalysisState::SolvedCurrent);

    part.requireMesh();
    CHECK(part.state(slot.result(), slot.failure()) == AnalysisState::SolvedStale);

    slot.recordFailure(makeError(ErrorCode::Internal, "singular system").error());
    REQUIRE(slot.result() != nullptr);
    CHECK(slot.result()->source() == solvedOn);
    CHECK_FALSE(slot.result()->source() == part.currentSource());
    CHECK(thermal::resultCurrency(slot.result(), part.currentSource()) == ResultCurrency::Stale);
    CHECK(part.state(slot.result(), slot.failure()) == AnalysisState::SolveFailed);

    // A second failure changes nothing about the result either.
    slot.recordFailure(makeError(ErrorCode::Internal, "still singular").error());
    CHECK(slot.result()->source() == solvedOn);
    CHECK(part.state(slot.result(), slot.failure()) == AnalysisState::SolveFailed);

    // Only a new SUCCESS replaces it and clears the failure.
    slot.recordSuccess(part.resultFor(part.currentSource()));
    CHECK(slot.failure() == nullptr);
    CHECK(slot.result()->source() == part.currentSource());
    CHECK(part.state(slot.result(), slot.failure()) == AnalysisState::SolvedCurrent);
}

TEST_CASE("ThermalData_TheStateMachineReachesEveryStateItDeclares", "[thermal][data][state]") {
    ConductingStudy part;

    SECTION("no analysis") {
        CHECK(thermal::analysisState(part.document, part.regenerator, part.mesher,
                                     ThermalAnalysisId::fromValue(999999), nullptr, nullptr) ==
              AnalysisState::NoAnalysis);
    }
    SECTION("defined, inputs unavailable: nothing meshed yet") {
        CHECK(part.state(nullptr, nullptr) == AnalysisState::InputsUnavailable);
    }
    SECTION("ready: inputs resolve and nothing is solved") {
        part.requireMesh();
        CHECK(part.state(nullptr, nullptr) == AnalysisState::Ready);
    }
    SECTION("solved and current") {
        part.requireMesh();
        const ThermalResult result = part.resultFor(part.currentSource());
        CHECK(part.state(&result, nullptr) == AnalysisState::SolvedCurrent);
    }
    SECTION("solved and stale") {
        part.requireMesh();
        const ThermalResult result = part.resultFor(part.currentSource());
        part.requireMesh();
        CHECK(part.state(&result, nullptr) == AnalysisState::SolvedStale);
    }
    SECTION("solve failed with no result") {
        part.requireMesh();
        const Error failure = makeError(ErrorCode::Internal, "the solver gave up").error();
        CHECK(part.state(nullptr, &failure) == AnalysisState::SolveFailed);
    }
    SECTION("solve failed takes precedence over a surviving stale result") {
        part.requireMesh();
        const ThermalResult result = part.resultFor(part.currentSource());
        part.requireMesh();
        const Error failure = makeError(ErrorCode::Internal, "the solver gave up").error();
        CHECK(part.state(&result, &failure) == AnalysisState::SolveFailed);
    }
    SECTION("and over a surviving current one") {
        part.requireMesh();
        const ThermalResult result = part.resultFor(part.currentSource());
        const Error failure = makeError(ErrorCode::Internal, "the solver gave up").error();
        CHECK(part.state(&result, &failure) == AnalysisState::SolveFailed);
    }
    SECTION("every state has a name") {
        for (const AnalysisState state : {AnalysisState::NoAnalysis, AnalysisState::InputsUnavailable,
                                          AnalysisState::Ready, AnalysisState::SolvedCurrent,
                                          AnalysisState::SolvedStale, AnalysisState::SolveFailed}) {
            CHECK(thermal::toString(state) != "unknown");
        }
    }
}

// ---------------------------------------------------------------------------
// Immutability, access and storage
// ---------------------------------------------------------------------------

TEST_CASE("ThermalData_ResultsAreImmutableAndReadOnly", "[thermal][data][result]") {
    // No setter, no mutable view, no mutable source. A renderer or a
    // post-processor holding a result cannot write a temperature, a flux or a
    // provenance field back into it; the compile-fail group tries each one.
    static_assert(std::is_same_v<decltype(std::declval<const ThermalResult&>().source()),
                                 const ThermalResultSource&>);
    static_assert(std::is_const_v<std::remove_reference_t<
                      decltype(std::declval<const ThermalResult&>().temperatures()[0])>>);
    static_assert(std::is_const_v<std::remove_reference_t<
                      decltype(std::declval<const ThermalResult&>().gradients()[0])>>);
    static_assert(std::is_const_v<std::remove_reference_t<
                      decltype(std::declval<const ThermalResult&>().heatFluxes()[0])>>);
    static_assert(std::is_same_v<decltype(std::declval<const ThermalResult&>().heatBalance()),
                                 const HeatBalance&>);
    // A result IS assignable, deliberately: the slot that holds one per
    // analysis must be able to replace it when a new solve succeeds, which is
    // replacement by the owner and not mutation through a reference.
    static_assert(std::is_move_assignable_v<ThermalResult>);
    // And it is not default-constructible: the only way to one is the
    // validating factory.
    static_assert(!std::is_default_constructible_v<ThermalResult>);

    ConductingStudy part;
    part.requireMesh();
    const ThermalResult result = part.resultFor(part.currentSource());
    CHECK(result.source().mesh == part.mesh().mesh().stamp());
}

TEST_CASE("ThermalData_ATemperatureIsFoundByNodeWithoutAssumingDenseIds", "[thermal][data][result]") {
    // Node IDs start at 1 and a builder may choose them, so indexing by
    // id.value() is a latent defect; the mesh's own ascending enumeration is
    // the qualified way and the accessor uses it.
    ConductingStudy part;
    part.requireMesh();
    const meshing::VolumeMesh& mesh = part.mesh();
    const ThermalResultSource source = part.currentSource();

    // Distinct temperatures per position, so a wrong lookup returns a
    // different value rather than the same one by luck.
    std::vector<Temperature> temperatures;
    for (std::size_t i = 0; i < mesh.nodeCount(); ++i) {
        temperatures.push_back(Temperature::fromSi(300.0 + static_cast<double>(i)));
    }
    const Result<ThermalResult> built = ThermalResult::create(
        source, mesh, temperatures, std::vector<TemperatureGradient3>(mesh.tetrahedronCount()),
        std::vector<HeatFlux3>(mesh.tetrahedronCount()), {}, std::nullopt);
    REQUIRE(built.has_value());
    const ThermalResult& result = *built;

    const std::span<const meshing::Node> nodes = mesh.mesh().nodes();
    CHECK(nodes.front().id.value() >= 1);
    CHECK(std::ranges::is_sorted(nodes, {}, [](const meshing::Node& n) { return n.id; }));
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const Result<Temperature> found = result.temperatureOf(mesh, nodes[i].id);
        REQUIRE(found.has_value());
        CHECK(found->si() == 300.0 + static_cast<double>(i));
    }
    const Result<Temperature> absent = result.temperatureOf(mesh, meshing::NodeId::fromValue(999999));
    REQUIRE_FALSE(absent.has_value());
    CHECK_THAT(absent.error().message, ContainsSubstring("not in this mesh"));
    CHECK_FALSE(result.temperatureOf(mesh, meshing::NodeId{}).has_value());
}

TEST_CASE("ThermalData_AResultDoesNotCopyTheMeshAndItsSizeIsIndependentOfTheMesh",
          "[thermal][data][result]") {
    // O(nodes + elements), not O(mesh), and the object itself is a fixed size
    // whatever the mesh. A VolumeMesh embedded by value would make sizeof
    // grow past any plausible bound; a mesh copied per solve is the obvious
    // way to make results unusable at scale.
    static_assert(sizeof(ThermalResult) < sizeof(meshing::VolumeMesh),
                  "a result records a stamp, never a copy of the mesh");
    static_assert(sizeof(ThermalResult) <= 3 * sizeof(std::vector<int>) + sizeof(ThermalResultSource) +
                                               sizeof(HeatBalance) + sizeof(std::optional<Time>) + 16,
                  "three vectors, a source, a balance and a time, and nothing else by value");

    ConductingStudy part;
    part.requireMesh();
    const ThermalResult coarse = part.resultFor(part.currentSource());
    const std::size_t coarseNodes = part.mesh().nodeCount();

    // A SECOND, FINER BODY in the same document, and a second analysis on it:
    // more nodes, more elements, longer arrays, the same object size. A
    // cylinder rather than a finer block, for the reason P16-SIZE-001
    // measured: OCCT triangulates a planar face with two triangles whatever
    // the deflection, so the block's 9-node mesh cannot be refined by a global
    // target at all, while a curved wall subdivides and the target governs.
    auto profile = std::make_unique<sketch::Sketch>("Round", Frame3D::xy());
    REQUIRE(profile->addCircle(Point2D{0_mm, 0_mm}, 6_mm).has_value());
    const ObjectId round = require(part.document.addObject(std::move(profile)));
    auto cylinder = features::ExtrudeFeature::create(
        "Cylinder", {.profile = SketchId::fromValue(round.value()), .depth = 20_mm});
    REQUIRE(cylinder.has_value());
    const ObjectId cylinderBody = require(part.document.addObject(std::move(*cylinder)));
    requireReport(part.regenerator, part.document);
    meshing::MeshControlDefinition finer{.body = cylinderBody};
    finer.mesh.sizing.globalTargetSize = 3_mm;
    auto control = meshing::MeshControl::create("Fine", finer);
    REQUIRE(control.has_value());
    const MeshControlId fineControl =
        MeshControlId::fromValue(require(part.document.addObject(std::move(*control))).value());
    auto study = ThermalAnalysis::create("FineStudy", ThermalAnalysisDefinition{.mesh = fineControl});
    REQUIRE(study.has_value());
    const ThermalAnalysisId fineAnalysis =
        ThermalAnalysisId::fromValue(require(part.document.addObject(std::move(*study))).value());
    const Result<const meshing::VolumeMesh*> fineMesh = part.mesher.generate(part.document, part.regenerator, fineControl);
    REQUIRE(fineMesh.has_value());
    const Result<ThermalResultSource> fineSource =
        thermal::currentResultSource(part.document, part.regenerator, part.mesher, fineAnalysis);
    REQUIRE(fineSource.has_value());
    const std::size_t fineNodes = (*fineMesh)->nodeCount();
    const std::size_t fineTets = (*fineMesh)->tetrahedronCount();
    REQUIRE(fineNodes > coarseNodes);
    const Result<ThermalResult> fine = ThermalResult::create(
        *fineSource, **fineMesh, std::vector<Temperature>(fineNodes, 300_K),
        std::vector<TemperatureGradient3>(fineTets), std::vector<HeatFlux3>(fineTets), {}, std::nullopt);
    REQUIRE(fine.has_value());

    CHECK(fine->temperatures().size() == fineNodes);
    CHECK(fine->gradients().size() == fineTets);
    CHECK(fine->heatFluxes().size() == fineTets);
    CHECK(coarse.temperatures().size() == coarseNodes);
    CHECK(sizeof(*fine) == sizeof(coarse));
    const std::size_t arrays = fine->temperatures().size() * sizeof(Temperature) +
                               fine->gradients().size() * sizeof(TemperatureGradient3) +
                               fine->heatFluxes().size() * sizeof(HeatFlux3);
    CHECK(sizeof(ThermalResult) < arrays);

    // The two analyses are independent: each result is current against its
    // own source and stale against the other's, by identity alone.
    CHECK(thermal::resultCurrency(&coarse, part.currentSource()) == ResultCurrency::Current);
    CHECK(thermal::resultCurrency(&*fine, *fineSource) == ResultCurrency::Current);
    CHECK(thermal::resultCurrency(&coarse, *fineSource) == ResultCurrency::Stale);
    const std::vector<StaleReason> reasons = thermal::staleReasons(coarse.source(), *fineSource);
    CHECK(std::ranges::find(reasons, StaleReason::Control) != reasons.end());
    CHECK(std::ranges::find(reasons, StaleReason::Mesh) != reasons.end());
    CHECK(std::ranges::find(reasons, StaleReason::Analysis) != reasons.end());

    WARN(std::format("coarse {} nodes, fine {} nodes and {} tetrahedra; sizeof(ThermalResult) = {} "
                     "bytes either way",
                     coarseNodes, fineNodes, fineTets, sizeof(ThermalResult)));
}

// ---------------------------------------------------------------------------
// Determinism
// ---------------------------------------------------------------------------

TEST_CASE("ThermalData_DeterministicCanonicalOrdering", "[thermal][data][determinism]") {
    // No wall clock, no pointer identity, no unordered iteration. Repeated
    // evaluation of an unchanged document gives the same source, the same
    // state and the same stale-reason vector, and the roster of analyses is
    // in one order whatever order they were made in.
    ConductingStudy part;
    part.requireMesh();
    const ThermalResultSource source = part.currentSource();
    const ThermalResult result = part.resultFor(source);

    for (int i = 0; i < 16; ++i) {
        CHECK(part.currentSource() == source);
        CHECK(part.state(&result, nullptr) == AnalysisState::SolvedCurrent);
    }

    ThermalResultSource moved = source;
    moved.mode = ThermalAnalysisMode::Transient;
    moved.geometry.value += 1;
    moved.materialRevision += 1;
    const std::vector<StaleReason> first = thermal::staleReasons(source, moved);
    for (int i = 0; i < 16; ++i) {
        CHECK(thermal::staleReasons(source, moved) == first);
    }
    CHECK(first == std::vector{StaleReason::Geometry, StaleReason::Material, StaleReason::Mode});

    for (const char* name : {"B", "A", "C"}) {
        auto study = ThermalAnalysis::create(name, ThermalAnalysisDefinition{.mesh = part.control});
        REQUIRE(study.has_value());
        REQUIRE(part.document.addObject(std::move(*study)).has_value());
    }
    const std::vector<ThermalAnalysisId> ids = thermal::thermalAnalysisIds(part.document);
    REQUIRE(ids.size() == 4);
    CHECK(ids.front() == part.analysis);
    CHECK(std::ranges::is_sorted(ids, [](ThermalAnalysisId a, ThermalAnalysisId b) {
        return a.value() < b.value();
    }));
    for (int i = 0; i < 16; ++i) {
        CHECK(thermal::thermalAnalysisIds(part.document) == ids);
    }

    // The source carries nothing a clock or an address could reach: every
    // field is an identity, a revision or an enumerator.
    static_assert(std::is_trivially_copyable_v<ThermalResultSource>);
    static_assert(sizeof(ThermalResultSource) ==
                  sizeof(ObjectId) + sizeof(MeshControlId) + sizeof(meshing::GeometryRevision) +
                      sizeof(meshing::MeshStamp) + sizeof(MaterialId) + sizeof(std::uint64_t) +
                      sizeof(ThermalAnalysisId) + sizeof(std::uint64_t) + 8,
                  "nine fields: two object identities, a geometry revision, a mesh stamp, a "
                  "material identity and revision, an analysis identity and revision, and a mode "
                  "padded to the alignment -- a timestamp or a pointer added here breaks this");

    WARN(std::format("{} analysis states, {} stale reasons and a roster of {} analyses, all "
                     "deterministic over 16 runs",
                     6, first.size(), ids.size()));
}
