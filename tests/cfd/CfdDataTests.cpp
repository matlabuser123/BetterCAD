// P19-DATA-001: the CFD identity model, the canonical/derived line, the semantic
// fingerprint, the result model and currentness.
//
// WHAT THESE TESTS ARE FOR. Most of this milestone's claims are about what
// CANNOT happen -- a boundary condition cannot be a node, a pressure field cannot
// enter the definition, the fluid cannot be named twice, a result from one mesh
// generation cannot be read against the next, a failed attempt cannot make a
// stale result current. Each is a compile-time assertion, a case in
// tests/compile_fail/CfdIdMisuse.cpp, or a test that builds the dangerous thing
// and watches it be refused.
//
// NOTHING HERE IS A SOLVE. The metadata in the results below is data chosen to
// exercise the container's invariants; the first physical numbers arrive with
// the steady solver.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"

#include <bettercad/cfd/CfdAnalysis.hpp>
#include <bettercad/cfd/CfdAnalysisObject.hpp>
#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdResult.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/meshing/MeshSizing.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <functional>
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
using cfd::Acceleration3;
using cfd::AnalysisState;
using cfd::CfdAnalysis;
using cfd::CfdAnalysisDefinition;
using cfd::CfdAnalysisMode;
using cfd::CfdBoundaryCondition;
using cfd::CfdBoundaryConditionKind;
using cfd::CfdConvergenceSettings;
using cfd::CfdDefinitionFingerprint;
using cfd::CfdFailureStage;
using cfd::CfdFluidProperties;
using cfd::CfdGravity;
using cfd::CfdInitialCondition;
using cfd::CfdIterationSettings;
using cfd::CfdLinearSolverSettings;
using cfd::CfdMassBalance;
using cfd::CfdOutputSettings;
using cfd::CfdReferencePressure;
using cfd::CfdResidualHistory;
using cfd::CfdResiduals;
using cfd::CfdResult;
using cfd::CfdResultMetadata;
using cfd::CfdResultSlot;
using cfd::CfdResultSource;
using cfd::CfdSolveFailure;
using cfd::CfdSteadySettings;
using cfd::CfdTimeSampling;
using cfd::CfdTransientSettings;
using cfd::MovingWall;
using cfd::NoSlipWall;
using cfd::PressureDatumProblem;
using cfd::PressureInlet;
using cfd::PressureOutlet;
using cfd::ResultCurrency;
using cfd::SlipWall;
using cfd::StaleReason;
using cfd::Velocity3;
using cfd::VelocityInlet;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

[[nodiscard]] Velocity3 velocity(double x, double y, double z) {
    return Velocity3{Velocity::fromSi(x), Velocity::fromSi(y), Velocity::fromSi(z)};
}

[[nodiscard]] Acceleration3 acceleration(double x, double y, double z) {
    return Acceleration3{Acceleration::fromSi(x), Acceleration::fromSi(y), Acceleration::fromSi(z)};
}

/// A block of water with a meshing control and one steady CFD analysis:
/// everything a CFD result needs provenance for. The block IS the fluid region.
struct FlowStudy {
    Document document{"Part"};
    features::Regenerator regenerator;
    meshing::Mesher mesher;
    ObjectId feature{};
    MeshControlId control{};
    CfdAnalysisId analysis{};
    MaterialId material{};
    std::array<EntityId, 4> lines{};

    explicit FlowStudy(bool withFluid = true) {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        lines = addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
        const ObjectId profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Fluid", {.profile = SketchId::fromValue(profile.value()), .depth = 20_mm});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));

        auto intent = meshing::MeshControl::create("Mesh", meshing::MeshControlDefinition{.body = feature});
        REQUIRE(intent.has_value());
        control = MeshControlId::fromValue(require(document.addObject(std::move(*intent))).value());

        if (withFluid) {
            features::MaterialDefinition water;
            water.designation = "Water";
            water.mechanical.density = materials::MaterialProperty<Density>::known(998_kg_per_m3);
            const Result<MaterialId> id = features::createMaterial(document, "Water", water);
            REQUIRE(id.has_value());
            material = *id;
            REQUIRE(features::assignMaterial(document, material).has_value());
        }

        auto study = CfdAnalysis::create("Flow", steadyDefinition());
        INFO((study.has_value() ? std::string{} : study.error().message));
        REQUIRE(study.has_value());
        analysis = CfdAnalysisId::fromValue(require(document.addObject(std::move(*study))).value());

        requireReport(regenerator, document);
    }

    [[nodiscard]] FaceName startCap() const { return FaceName{feature, FaceSelector{.role = FaceRole::StartCap}}; }
    [[nodiscard]] FaceName endCap() const { return FaceName{feature, FaceSelector{.role = FaceRole::EndCap}}; }
    [[nodiscard]] FaceName side(std::size_t which = 0) const {
        return FaceName{feature, FaceSelector{.role = FaceRole::Side, .entity = lines.at(which)}};
    }

    /// A channel: water enters through the start cap, leaves through the end cap
    /// at 0 Pa, and the four sides are walls. The outlet fixes the pressure.
    [[nodiscard]] CfdAnalysisDefinition steadyDefinition() const {
        CfdAnalysisDefinition definition{.mesh = control};
        definition.boundaryConditions = {
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), VelocityInlet{startCap(), 0.1_m_per_s}},
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(2), PressureOutlet{endCap(), 0_Pa}},
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(3), NoSlipWall{side(0)}},
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(4), NoSlipWall{side(1)}},
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(5), NoSlipWall{side(2)}},
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(6), NoSlipWall{side(3)}},
        };
        return definition;
    }

    [[nodiscard]] CfdAnalysisDefinition transientDefinition() const {
        CfdAnalysisDefinition definition = steadyDefinition();
        definition.mode = CfdAnalysisMode::Transient;
        definition.steady.reset();
        definition.transient = CfdTransientSettings{.timeStep = 0.01_s, .endTime = 1_s};
        return definition;
    }

    [[nodiscard]] const CfdAnalysis& study() const {
        const CfdAnalysis* object = cfd::findCfdAnalysis(document, analysis);
        REQUIRE(object != nullptr);
        return *object;
    }

    [[nodiscard]] const CfdAnalysisDefinition& definition() const { return study().definition(); }
    [[nodiscard]] std::uint64_t revision() const { return study().revision(); }

    /// Replaces the definition through the document, so the revision moves
    /// exactly as a command would move it.
    [[nodiscard]] Result<bool> setDefinition(CfdAnalysisDefinition definition) {
        return document.modifyObject<CfdAnalysis>(
            ObjectId::fromValue(analysis.value()),
            [&definition](CfdAnalysis& object) -> Result<bool> { return object.setDefinition(definition); });
    }

    void requireMesh() {
        const Result<const meshing::VolumeMesh*> mesh = mesher.generate(document, regenerator, control);
        INFO((mesh.has_value() ? std::string{} : mesh.error().message));
        REQUIRE(mesh.has_value());
    }

    [[nodiscard]] const meshing::VolumeMesh& mesh() const {
        const meshing::VolumeMesh* held = mesher.mesh(control);
        REQUIRE(held != nullptr);
        return *held;
    }

    [[nodiscard]] CfdResultSource currentSource() const {
        const Result<CfdResultSource> source = cfd::currentResultSource(document, regenerator, mesher, analysis);
        INFO((source.has_value() ? std::string{} : source.error().message));
        REQUIRE(source.has_value());
        return *source;
    }

    [[nodiscard]] AnalysisState state(const CfdResult* result, const CfdSolveFailure* failure) const {
        return cfd::analysisState(document, regenerator, mesher, analysis, result, failure);
    }

    /// Well-formed metadata for @p mode. Not a solve; see the file header.
    [[nodiscard]] static CfdResultMetadata metadataFor(CfdAnalysisMode mode, std::uint32_t iterations = 12) {
        CfdResultMetadata metadata{.outerIterations = iterations,
                                   .residuals = CfdResiduals{1e-7, 2e-7, 3e-7, 4e-7},
                                   .massBalance = CfdMassBalance{1e-9, 1e-12}};
        if (mode == CfdAnalysisMode::Transient) {
            metadata.time = 1_s;
            metadata.step = 100U;
        }
        return metadata;
    }

    [[nodiscard]] CfdResult resultFor(const CfdResultSource& source, std::uint32_t iterations = 12) const {
        Result<CfdResult> result = CfdResult::create(source, mesh(), metadataFor(source.mode, iterations));
        INFO((result.has_value() ? std::string{} : result.error().message));
        REQUIRE(result.has_value());
        return std::move(*result);
    }

    void setDensity(Density density) {
        materials::MechanicalProperties mechanical = features::findMaterial(document, material)->definition().mechanical;
        mechanical.density = materials::MaterialProperty<Density>::known(density);
        REQUIRE(features::setMaterialMechanical(document, material, mechanical).has_value());
    }

    void changeTheGeometry() {
        REQUIRE(document
                    .modifyObject<features::ExtrudeFeature>(feature,
                                                            [](features::ExtrudeFeature& extrude) -> Result<bool> {
                                                                auto definition = extrude.definition();
                                                                definition.depth = 35_mm;
                                                                return extrude.setDefinition(definition).has_value();
                                                            })
                    .has_value());
        requireReport(regenerator, document);
    }
};

[[nodiscard]] CfdBoundaryCondition condition(std::uint64_t id, auto payload) {
    return CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(id), std::move(payload)};
}

[[nodiscard]] Error refusalOf(const Result<void>& result) {
    REQUIRE_FALSE(result.has_value());
    return result.error();
}

/// FNV-1a 64, written again here rather than called: the test's oracle for the
/// digest must not be the code under test.
[[nodiscard]] std::uint64_t independentFnv1a(std::string_view text) {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const char c : text) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

} // namespace

// ---------------------------------------------------------------------------
// Identity domains
// ---------------------------------------------------------------------------

TEST_CASE("CfdData_StrongIdsAreDistinct", "[cfd][data][id]") {
    // Not interchangeable, not integers, and not P17's or P18's. The compile-fail
    // group proves the conversions do not exist; this proves the types are
    // distinct and behave as identities.
    static_assert(!std::is_same_v<CfdAnalysisId, CfdBoundaryConditionId>);
    static_assert(!std::is_same_v<CfdAnalysisId, ThermalAnalysisId>);
    static_assert(!std::is_same_v<CfdAnalysisId, AnalysisId>);
    static_assert(!std::is_same_v<CfdBoundaryConditionId, ThermalBoundaryConditionId>);
    static_assert(!std::is_same_v<CfdBoundaryConditionId, RestraintId>);
    static_assert(!std::is_convertible_v<CfdAnalysisId, std::uint64_t>);
    static_assert(!std::is_convertible_v<std::uint64_t, CfdBoundaryConditionId>);
    static_assert(!std::is_convertible_v<CfdAnalysisId, ThermalAnalysisId>);
    static_assert(!std::is_convertible_v<ThermalAnalysisId, CfdAnalysisId>);
    static_assert(!std::is_convertible_v<AnalysisId, CfdAnalysisId>);
    static_assert(!std::is_convertible_v<ThermalBoundaryConditionId, CfdBoundaryConditionId>);
    static_assert(!std::is_convertible_v<CfdBoundaryConditionId, CfdAnalysisId>);

    CHECK_FALSE(CfdAnalysisId{}.isValid());
    CHECK(CfdAnalysisId::fromValue(7).isValid());
    CHECK(CfdAnalysisId::fromValue(7) == CfdAnalysisId::fromValue(7));
    CHECK(CfdAnalysisId::fromValue(7) < CfdAnalysisId::fromValue(8));

    // The diagnostic names keep the kinds apart in a log as well.
    CHECK(std::format("{}", CfdAnalysisId::fromValue(3)) == "cfd analysis:3");
    CHECK(std::format("{}", CfdBoundaryConditionId::fromValue(3)) == "cfd boundary condition:3");
}

TEST_CASE("CfdData_OnlyTheAnalysisIsADocumentObjectIdentity", "[cfd][data][id]") {
    // THE DOMAIN CLAIM, IN THE TYPE SYSTEM. An analysis is a document object; a
    // boundary condition is a member of a definition. And there is no third
    // identity: the initial condition is one record per analysis (ADR-061).
    static_assert(std::is_convertible_v<CfdAnalysisId, ObjectId>);
    static_assert(!std::is_convertible_v<CfdBoundaryConditionId, ObjectId>);
    static_assert(std::is_same_v<decltype(CfdAnalysisDefinition::initialCondition), CfdInitialCondition>,
                  "one initial condition, held by value: nothing for an identity to distinguish");

    FlowStudy part;
    const ObjectId widened = part.analysis;
    CHECK(part.document.findObject(widened) != nullptr);
}

TEST_CASE("CfdData_NoMeshHandleCanActAsACfdIdentity", "[cfd][data][id]") {
    // A NodeId, an ElementId and an index are mesh-local; none can be used where
    // a persisted CFD identity is wanted, nor the reverse.
    static_assert(!std::is_convertible_v<meshing::NodeId, CfdAnalysisId>);
    static_assert(!std::is_convertible_v<meshing::NodeId, CfdBoundaryConditionId>);
    static_assert(!std::is_convertible_v<meshing::ElementId, CfdBoundaryConditionId>);
    static_assert(!std::is_constructible_v<CfdBoundaryConditionId, meshing::ElementId>);
    static_assert(!std::is_convertible_v<CfdBoundaryConditionId, meshing::NodeId>);
    static_assert(!std::is_convertible_v<std::size_t, CfdBoundaryConditionId>);

    // And the canonical records have nowhere for a mesh handle to hide: each is a
    // FaceName and typed values, exactly. A facet list, a FaceIndex or a patch
    // number added to any of them breaks its line.
    struct PermittedInlet {
        FaceName face;
        Velocity normalSpeed;
    };
    struct PermittedPressure {
        FaceName face;
        Pressure staticPressure;
    };
    struct PermittedWall {
        FaceName face;
    };
    struct PermittedMovingWall {
        FaceName face;
        Velocity3 velocity;
    };
    static_assert(sizeof(VelocityInlet) == sizeof(PermittedInlet));
    static_assert(sizeof(PressureInlet) == sizeof(PermittedPressure));
    static_assert(sizeof(PressureOutlet) == sizeof(PermittedPressure));
    static_assert(sizeof(NoSlipWall) == sizeof(PermittedWall));
    static_assert(sizeof(SlipWall) == sizeof(PermittedWall));
    static_assert(sizeof(MovingWall) == sizeof(PermittedMovingWall));
    static_assert(!std::is_constructible_v<VelocityInlet, meshing::NodeId, Velocity>);
    static_assert(!std::is_constructible_v<NoSlipWall, meshing::ElementId>);
    SUCCEED("the conversions that would break the identity model do not exist");
}

// ---------------------------------------------------------------------------
// Units and conventions
// ---------------------------------------------------------------------------

TEST_CASE("CfdData_UnitsAreExplicitSi", "[cfd][data][units]") {
    // SI through the units framework. The only bare doubles are ratios.
    static_assert(std::is_same_v<decltype(VelocityInlet::normalSpeed), Velocity>);
    static_assert(std::is_same_v<decltype(PressureInlet::staticPressure), Pressure>);
    static_assert(std::is_same_v<decltype(PressureOutlet::staticPressure), Pressure>);
    static_assert(std::is_same_v<decltype(MovingWall::velocity), Velocity3>);
    static_assert(std::is_same_v<decltype(CfdInitialCondition::velocity), Velocity3>);
    static_assert(std::is_same_v<decltype(CfdInitialCondition::pressure), Pressure>);
    static_assert(std::is_same_v<decltype(CfdReferencePressure::value), Pressure>);
    static_assert(std::is_same_v<decltype(CfdReferencePressure::location), Point3D>);
    static_assert(std::is_same_v<decltype(CfdGravity::acceleration), Acceleration3>);
    static_assert(std::is_same_v<decltype(CfdGravity::referenceHeight), Length>);
    static_assert(std::is_same_v<decltype(CfdTransientSettings::timeStep), Time>);
    static_assert(std::is_same_v<decltype(CfdTransientSettings::endTime), Time>);
    static_assert(std::is_same_v<decltype(CfdFluidProperties::density), Density>);
    static_assert(std::is_same_v<decltype(CfdFluidProperties::viscosity), DynamicViscosity>);
    static_assert(std::is_same_v<decltype(std::declval<const CfdFluidProperties&>().kinematicViscosity()),
                                 KinematicViscosity>);
    static_assert(std::is_same_v<decltype(CfdResultMetadata::time), std::optional<Time>>);

    // The literals land in SI.
    const VelocityInlet fast{.face = {}, .normalSpeed = 36_km_per_h};
    const PressureOutlet outlet{.face = {}, .staticPressure = 2_kPa};
    const CfdReferencePressure atmosphere{.value = 1_bar, .location = {}};
    CHECK_THAT(fast.normalSpeed.si(), WithinRel(10.0, 1e-15));
    CHECK_THAT(outlet.staticPressure.si(), WithinRel(2000.0, 1e-15));
    CHECK_THAT(atmosphere.value.si(), WithinRel(100000.0, 1e-15));
}

TEST_CASE("CfdData_VectorsAreDistinctTypesWithAFrozenComponentOrder", "[cfd][data][convention]") {
    // One template, distinct instances: m/s is not m/s^2.
    static_assert(!std::is_same_v<Velocity3, Acceleration3>);
    static_assert(!std::is_convertible_v<Velocity3, Acceleration3>);
    static_assert(!std::is_convertible_v<Acceleration3, Velocity3>);

    static_assert(static_cast<int>(cfd::VectorComponent::X) == 0);
    static_assert(static_cast<int>(cfd::VectorComponent::Y) == 1);
    static_assert(static_cast<int>(cfd::VectorComponent::Z) == 2);
    CHECK(cfd::kVectorComponents ==
          std::array{cfd::VectorComponent::X, cfd::VectorComponent::Y, cfd::VectorComponent::Z});
    CHECK(cfd::toString(cfd::VectorComponent::X) == "x");
    CHECK(cfd::toString(cfd::VectorComponent::Y) == "y");
    CHECK(cfd::toString(cfd::VectorComponent::Z) == "z");

    const Velocity3 u = velocity(3.0, 4.0, 12.0);
    CHECK(u.component(cfd::VectorComponent::X).si() == 3.0);
    CHECK(u.component(cfd::VectorComponent::Y).si() == 4.0);
    CHECK(u.component(cfd::VectorComponent::Z).si() == 12.0);
    // THE MAGNITUDE IS DERIVED: three fields and nothing else.
    static_assert(sizeof(Velocity3) == 3 * sizeof(Velocity));
    CHECK(u.magnitude().si() == 13.0);
    CHECK(cfd::isFinite(u));
    CHECK_FALSE(cfd::isFinite(velocity(0.0, kNaN, 0.0)));
}

TEST_CASE("CfdData_KinematicViscosityIsDerivedNotStored", "[cfd][data][fluid]") {
    // ADR-058: nu = mu / rho, derived only. Two fields; a third (a stored nu, a
    // material id, a default) breaks this.
    struct PermittedFluid {
        Density density;
        DynamicViscosity viscosity;
    };
    static_assert(sizeof(CfdFluidProperties) == sizeof(PermittedFluid));

    const CfdFluidProperties water{.density = 998_kg_per_m3, .viscosity = 1.002_mPa_s};
    REQUIRE(cfd::validate(water).has_value());
    CHECK_THAT(water.kinematicViscosity().si(), WithinRel(1.002e-3 / 998.0, 1e-15));

    CHECK_THAT(refusalOf(cfd::validate(CfdFluidProperties{.density = 0_kg_per_m3, .viscosity = 1_mPa_s})).message,
               ContainsSubstring("density"));
    CHECK_THAT(refusalOf(cfd::validate(CfdFluidProperties{.density = 998_kg_per_m3, .viscosity = 0_Pa_s})).message,
               ContainsSubstring("viscosity"));
    CHECK_FALSE(cfd::validate(CfdFluidProperties{.density = Density::fromSi(kNaN), .viscosity = 1_mPa_s}).has_value());
    CHECK_FALSE(cfd::validate(CfdFluidProperties{.density = 998_kg_per_m3, .viscosity = DynamicViscosity::fromSi(kInf)})
                    .has_value());
    CHECK_FALSE(cfd::validate(CfdFluidProperties{.density = Density::fromSi(-1.0), .viscosity = 1_mPa_s}).has_value());
}

TEST_CASE("CfdData_TheModeIsAnEnumNotABool", "[cfd][data][convention]") {
    static_assert(std::is_enum_v<CfdAnalysisMode>);
    static_assert(!std::is_convertible_v<bool, CfdAnalysisMode>);
    CHECK(cfd::toString(CfdAnalysisMode::Steady) == "steady");
    CHECK(cfd::toString(CfdAnalysisMode::Transient) == "transient");
    CHECK(cfd::toString(static_cast<CfdAnalysisMode>(2)) == "unknown");
}

// ---------------------------------------------------------------------------
// Canonical records, each on its own terms
// ---------------------------------------------------------------------------

TEST_CASE("CfdData_BoundaryConditionsAreValidatedOnTheirOwnTerms", "[cfd][data][bc]") {
    FlowStudy part;

    SECTION("every kind, well formed, is accepted") {
        for (const CfdBoundaryCondition& accepted :
             {condition(1, VelocityInlet{part.startCap(), 0.5_m_per_s}),
              condition(2, PressureInlet{part.startCap(), -150_Pa}), condition(3, PressureOutlet{part.endCap(), 0_Pa}),
              condition(4, NoSlipWall{part.side()}), condition(5, MovingWall{part.endCap(), velocity(1.0, 0.0, 0.0)}),
              condition(6, SlipWall{part.side(1)})}) {
            INFO(cfd::toString(accepted.kind()));
            CHECK(cfd::validate(accepted).has_value());
        }
    }
    SECTION("the invalid identity is refused") {
        CHECK_THAT(refusalOf(cfd::validate(condition(0, NoSlipWall{part.side()}))).message,
                   ContainsSubstring("valid CfdBoundaryConditionId"));
    }
    SECTION("a malformed face selector is refused by core's own check") {
        // A side face must name the profile entity that sweeps it.
        const FaceName unnamedSide{part.feature, FaceSelector{.role = FaceRole::Side}};
        CHECK_FALSE(cfd::validate(condition(1, NoSlipWall{unnamedSide})).has_value());
    }
    SECTION("a face role this build does not know, or the invalid feature, is refused") {
        // Core checks neither; without these two refusals two different
        // out-of-range roles would both write "unknown" into the canonical text.
        FaceName unknownRole = part.endCap();
        unknownRole.face.role = static_cast<FaceRole>(7);
        CHECK_THAT(refusalOf(cfd::validate(condition(1, NoSlipWall{unknownRole}))).message,
                   ContainsSubstring("face role"));
        FaceName noFeature = part.endCap();
        noFeature.feature = ObjectId{};
        CHECK_THAT(refusalOf(cfd::validate(condition(1, NoSlipWall{noFeature}))).message,
                   ContainsSubstring("invalid feature"));
    }
    SECTION("an inflow speed must be finite and positive: zero is a wall, negative an outflow") {
        CHECK_THAT(refusalOf(cfd::validate(condition(1, VelocityInlet{part.startCap(), 0_m_per_s}))).message,
                   ContainsSubstring("no-slip wall"));
        CHECK_FALSE(cfd::validate(condition(1, VelocityInlet{part.startCap(), Velocity::fromSi(-0.1)})).has_value());
        CHECK_FALSE(cfd::validate(condition(1, VelocityInlet{part.startCap(), Velocity::fromSi(kNaN)})).has_value());
        CHECK_FALSE(cfd::validate(condition(1, VelocityInlet{part.startCap(), Velocity::fromSi(kInf)})).has_value());
    }
    SECTION("a static pressure may have either sign but must be finite") {
        CHECK(cfd::validate(condition(1, PressureOutlet{part.endCap(), Pressure::fromSi(-2.5e5)})).has_value());
        CHECK_FALSE(cfd::validate(condition(1, PressureOutlet{part.endCap(), Pressure::fromSi(kNaN)})).has_value());
        CHECK_FALSE(cfd::validate(condition(1, PressureInlet{part.startCap(), Pressure::fromSi(-kInf)})).has_value());
    }
    SECTION("a moving wall must move, finitely") {
        CHECK_THAT(refusalOf(cfd::validate(condition(1, MovingWall{part.endCap(), velocity(0.0, 0.0, 0.0)}))).message,
                   ContainsSubstring("does not move"));
        CHECK_FALSE(cfd::validate(condition(1, MovingWall{part.endCap(), velocity(1.0, kNaN, 0.0)})).has_value());
        // -0.0 in every component is still a wall at rest.
        CHECK_FALSE(cfd::validate(condition(1, MovingWall{part.endCap(), velocity(-0.0, -0.0, -0.0)})).has_value());
    }
}

TEST_CASE("CfdData_TheKindIsTheAlternativeHeld", "[cfd][data][bc]") {
    FlowStudy part;
    const std::array conditions{
        condition(1, VelocityInlet{part.startCap(), 1_m_per_s}), condition(2, PressureInlet{part.startCap(), 1_Pa}),
        condition(3, PressureOutlet{part.endCap(), 1_Pa}),       condition(4, NoSlipWall{part.side(0)}),
        condition(5, MovingWall{part.side(1), velocity(0.0, 0.0, 1.0)}), condition(6, SlipWall{part.side(2)})};
    // kind() is computed from the variant's index, so the enum and the
    // alternatives must be declared in the same order; this is the check.
    for (std::size_t i = 0; i < conditions.size(); ++i) {
        CHECK(conditions[i].kind() == cfd::kCfdBoundaryConditionKinds[i]);
    }
    CHECK(conditions[0].velocityInlet() != nullptr);
    CHECK(conditions[1].pressureInlet() != nullptr);
    CHECK(conditions[2].pressureOutlet() != nullptr);
    CHECK(conditions[3].noSlipWall() != nullptr);
    CHECK(conditions[4].movingWall() != nullptr);
    CHECK(conditions[5].slipWall() != nullptr);
    CHECK(conditions[0].pressureInlet() == nullptr);
    CHECK(conditions[3].movingWall() == nullptr);

    CHECK(conditions[0].face() == part.startCap());
    CHECK(conditions[2].face() == part.endCap());
    CHECK(conditions[5].face() == part.side(2));

    // Exactly the two pressure conditions fix the pressure level.
    const std::array expected{false, true, true, false, false, false};
    for (std::size_t i = 0; i < conditions.size(); ++i) {
        CHECK(conditions[i].fixesPressure() == expected[i]);
    }

    // Every kind has a distinct name, and the value after the last is unnamed.
    std::vector<std::string_view> names;
    for (const CfdBoundaryConditionKind kind : cfd::kCfdBoundaryConditionKinds) {
        CHECK(cfd::toString(kind) != "unknown");
        names.push_back(cfd::toString(kind));
    }
    std::ranges::sort(names);
    CHECK(std::ranges::adjacent_find(names) == names.end());
    CHECK(cfd::toString(static_cast<CfdBoundaryConditionKind>(cfd::kCfdBoundaryConditionKinds.size())) == "unknown");
}

TEST_CASE("CfdData_InitialConditionReferencePressureAndGravityAreValidated", "[cfd][data][settings]") {
    CHECK(cfd::validate(CfdInitialCondition{}).has_value());
    CHECK(cfd::validate(CfdInitialCondition{.velocity = velocity(0.1, 0.0, 0.0), .pressure = -50_Pa}).has_value());
    CHECK_THAT(refusalOf(cfd::validate(CfdInitialCondition{.velocity = velocity(kNaN, 0.0, 0.0), .pressure = {}}))
                   .message,
               ContainsSubstring("initial velocity"));
    CHECK_THAT(refusalOf(cfd::validate(CfdInitialCondition{.velocity = {}, .pressure = Pressure::fromSi(kInf)}))
                   .message,
               ContainsSubstring("initial pressure"));

    const Point3D origin{};
    CHECK(cfd::validate(CfdReferencePressure{.value = 101325_Pa, .location = origin}).has_value());
    CHECK_FALSE(cfd::validate(CfdReferencePressure{.value = Pressure::fromSi(kNaN), .location = origin}).has_value());
    CHECK_THAT(refusalOf(cfd::validate(CfdReferencePressure{
                             .value = 0_Pa, .location = Point3D{Length::fromSi(kInf), Length{}, Length{}}}))
                   .message,
               ContainsSubstring("location"));

    CHECK(cfd::validate(CfdGravity{acceleration(0.0, 0.0, -9.80665)}).has_value());
    CHECK(cfd::validate(CfdGravity{acceleration(1.0, 2.0, 3.0)}).has_value());
    CHECK_THAT(refusalOf(cfd::validate(CfdGravity{acceleration(0.0, 0.0, 0.0)})).message,
               ContainsSubstring("absence of a gravity record"));
    CHECK_FALSE(cfd::validate(CfdGravity{acceleration(0.0, kNaN, -9.81)}).has_value());
    CHECK(cfd::validate(CfdGravity{.acceleration = acceleration(0.0, 0.0, -9.81), .referenceHeight = -3_m})
              .has_value());
    CHECK_THAT(refusalOf(cfd::validate(CfdGravity{.acceleration = acceleration(0.0, 0.0, -9.81),
                                                  .referenceHeight = Length::fromSi(kNaN)}))
                   .message,
               ContainsSubstring("reference height"));
}

TEST_CASE("CfdData_SolverSettingsAreValidated", "[cfd][data][settings]") {
    SECTION("relaxation factors lie in (0, 1]") {
        CHECK(cfd::validate(CfdIterationSettings{}).has_value());
        CHECK(cfd::validate(CfdIterationSettings{.velocityRelaxation = 1.0, .pressureRelaxation = 1.0}).has_value());
        for (const double bad : {0.0, -0.1, 1.0000001, kNaN, kInf}) {
            INFO(bad);
            CHECK_FALSE(cfd::validate(CfdIterationSettings{.velocityRelaxation = bad}).has_value());
            CHECK_FALSE(cfd::validate(CfdIterationSettings{.pressureRelaxation = bad}).has_value());
        }
    }
    SECTION("tolerances are finite and positive; divergence needs growth beyond 1") {
        CHECK(cfd::validate(CfdConvergenceSettings{}).has_value());
        for (const double bad : {0.0, -1e-6, kNaN, kInf}) {
            INFO(bad);
            CHECK_FALSE(cfd::validate(CfdConvergenceSettings{.momentumResidualTolerance = bad}).has_value());
            CHECK_FALSE(cfd::validate(CfdConvergenceSettings{.continuityResidualTolerance = bad}).has_value());
            CHECK_FALSE(cfd::validate(CfdLinearSolverSettings{.relativeResidualTolerance = bad}).has_value());
        }
        CHECK_FALSE(cfd::validate(CfdConvergenceSettings{.divergenceFactor = 1.0}).has_value());
        CHECK_FALSE(cfd::validate(CfdConvergenceSettings{.divergenceFactor = kNaN}).has_value());
        CHECK(cfd::validate(CfdConvergenceSettings{.divergenceFactor = 1.5}).has_value());
        CHECK(cfd::validate(CfdLinearSolverSettings{}).has_value());
    }
    SECTION("a steady budget of at least one iteration") {
        CHECK(cfd::validate(CfdSteadySettings{}).has_value());
        CHECK(cfd::validate(CfdSteadySettings{.maxIterations = 1}).has_value());
        CHECK_FALSE(cfd::validate(CfdSteadySettings{.maxIterations = 0}).has_value());
    }
    SECTION("a transient step that is positive, within the end time, with a per-step budget") {
        CHECK(cfd::validate(CfdTransientSettings{.timeStep = 0.1_s, .endTime = 1_s}).has_value());
        CHECK(cfd::validate(CfdTransientSettings{.timeStep = 1_s, .endTime = 1_s}).has_value());
        CHECK_FALSE(cfd::validate(CfdTransientSettings{.timeStep = 0_s, .endTime = 1_s}).has_value());
        CHECK_FALSE(cfd::validate(CfdTransientSettings{.timeStep = Time::fromSi(kNaN), .endTime = 1_s}).has_value());
        CHECK_FALSE(cfd::validate(CfdTransientSettings{.timeStep = 0.1_s, .endTime = Time::fromSi(kInf)}).has_value());
        CHECK_THAT(refusalOf(cfd::validate(CfdTransientSettings{.timeStep = 2_s, .endTime = 1_s})).message,
                   ContainsSubstring("not one step"));
        CHECK_FALSE(
            cfd::validate(CfdTransientSettings{.timeStep = 0.1_s, .endTime = 1_s, .maxIterationsPerStep = 0}).has_value());
        CHECK_FALSE(cfd::validate(CfdTransientSettings{.timeStep = 0.1_s,
                                                       .endTime = 1_s,
                                                       .sampling = static_cast<CfdTimeSampling>(7)})
                        .has_value());
    }
    SECTION("output policies this build recognises") {
        CHECK(cfd::validate(CfdOutputSettings{}).has_value());
        CHECK(cfd::validate(CfdOutputSettings{.residualHistory = CfdResidualHistory::EveryIteration}).has_value());
        CHECK_FALSE(cfd::validate(CfdOutputSettings{.residualHistory = static_cast<CfdResidualHistory>(9)}).has_value());
    }
}

// ---------------------------------------------------------------------------
// The definition
// ---------------------------------------------------------------------------

TEST_CASE("CfdData_TheDefinitionHoldsOnlyIntent", "[cfd][data][analysis]") {
    // THE INSTRUMENT P17 ARRIVED AT: the types that MAY appear, in order, and a
    // size equality an added field breaks. Extending this mirror is where a later
    // milestone states a new field's authority as CANONICAL INTENT.
    struct PermittedDefinition {
        MeshControlId mesh;
        CfdAnalysisMode mode;
        std::vector<CfdBoundaryCondition> boundaryConditions;
        CfdInitialCondition initialCondition;
        std::optional<CfdReferencePressure> referencePressure;
        std::optional<CfdGravity> gravity;
        CfdIterationSettings iteration;
        CfdConvergenceSettings convergence;
        CfdLinearSolverSettings linearSolver;
        std::optional<CfdSteadySettings> steady;
        std::optional<CfdTransientSettings> transient;
        CfdOutputSettings output;
    };
    static_assert(sizeof(CfdAnalysisDefinition) == sizeof(PermittedDefinition),
                  "the CFD definition holds a control, a mode, the boundary conditions, the initial condition, the "
                  "pressure datum, gravity and the settings -- nothing else. A field array, a result, a mesh handle, "
                  "a face set, a matrix or a fluid added to it breaks this");

    // A SIZE CANNOT SEE A BYTE HIDDEN IN PADDING: there are seven after `mode`.
    // So the member COUNT is pinned too, by a structured binding that names
    // exactly twelve: a thirteenth member of any size makes this line ill-formed.
    // (The compile-fail group then proves the members a leak would most likely
    // be called do not exist.)
    [[maybe_unused]] const auto& [mesh, mode, conditions, initial, reference, gravity, iteration, convergence,
                                  linear, steady, transient, output] = CfdAnalysisDefinition{};
    [[maybe_unused]] const auto& [initialVelocity, initialPressure] = CfdInitialCondition{};

    FlowStudy part;
    const CfdAnalysis& study = part.study();
    CHECK(study.typeName() == "cfd-analysis");
    CHECK(study.analysisId() == part.analysis);
    CHECK(study.definition().mesh == part.control);
    CHECK(study.definition().mode == CfdAnalysisMode::Steady);

    // It depends on the control, so editing the control -- or the body beneath
    // it -- reaches this analysis.
    const std::vector<ObjectId> dependencies = study.dependencies();
    REQUIRE(dependencies.size() == 1);
    CHECK(dependencies.front() == ObjectId::fromValue(part.control.value()));

    // An object of another kind carrying the number is not a CFD analysis.
    CHECK(cfd::findCfdAnalysis(part.document, CfdAnalysisId::fromValue(part.control.value())) == nullptr);
    CHECK(cfd::findCfdAnalysis(part.document, CfdAnalysisId::fromValue(999999)) == nullptr);
}

TEST_CASE("CfdData_TheDefinitionIsValidatedPerMode", "[cfd][data][analysis]") {
    FlowStudy part;

    SECTION("the default-constructed definition names no mesh") {
        CHECK_THAT(refusalOf(cfd::validate(CfdAnalysisDefinition{})).message, ContainsSubstring("invalid handle"));
    }
    SECTION("an unknown mode is refused, by the mode check and not by a later one") {
        // Both shapes, because each would be refused by mode-completeness anyway
        // in one of them: the message is what proves the mode check fired.
        for (CfdAnalysisDefinition definition : {part.steadyDefinition(), part.transientDefinition()}) {
            definition.mode = static_cast<CfdAnalysisMode>(5);
            CHECK_THAT(refusalOf(cfd::validate(definition)).message, ContainsSubstring("not one this build recognises"));
        }
    }
    SECTION("a steady analysis needs steady settings and must not carry transient ones") {
        CfdAnalysisDefinition definition = part.steadyDefinition();
        REQUIRE(cfd::validate(definition).has_value());
        definition.transient = CfdTransientSettings{.timeStep = 1_s, .endTime = 2_s};
        CHECK_THAT(refusalOf(cfd::validate(definition)).message, ContainsSubstring("no time step"));
        definition.transient.reset();
        definition.steady.reset();
        CHECK_THAT(refusalOf(cfd::validate(definition)).message, ContainsSubstring("steady settings"));
        definition.steady = CfdSteadySettings{.maxIterations = 0};
        CHECK_FALSE(cfd::validate(definition).has_value());
    }
    SECTION("a transient analysis needs transient settings and must not carry steady ones") {
        CfdAnalysisDefinition definition = part.transientDefinition();
        REQUIRE(cfd::validate(definition).has_value());
        definition.steady = CfdSteadySettings{};
        CHECK_THAT(refusalOf(cfd::validate(definition)).message, ContainsSubstring("no steady settings"));
        definition.steady.reset();
        definition.transient.reset();
        CHECK_THAT(refusalOf(cfd::validate(definition)).message, ContainsSubstring("time step and an end time"));
        definition.transient = CfdTransientSettings{.timeStep = 0_s, .endTime = 1_s};
        CHECK_FALSE(cfd::validate(definition).has_value());
    }
    SECTION("the complete transient definition is accepted and can enter the document") {
        REQUIRE(part.setDefinition(part.transientDefinition()).has_value());
        CHECK(part.definition().mode == CfdAnalysisMode::Transient);
    }
    SECTION("every nested record is validated through the definition") {
        const std::vector<std::function<void(CfdAnalysisDefinition&)>> breakages{
            [](CfdAnalysisDefinition& d) { d.initialCondition.pressure = Pressure::fromSi(kNaN); },
            [](CfdAnalysisDefinition& d) { d.iteration.velocityRelaxation = 0.0; },
            [](CfdAnalysisDefinition& d) { d.convergence.continuityResidualTolerance = -1.0; },
            [](CfdAnalysisDefinition& d) { d.linearSolver.relativeResidualTolerance = kInf; },
            [](CfdAnalysisDefinition& d) { d.output.residualHistory = static_cast<CfdResidualHistory>(3); },
            [](CfdAnalysisDefinition& d) { d.gravity = CfdGravity{}; },
            [&part](CfdAnalysisDefinition& d) {
                d.boundaryConditions.push_back(condition(9, VelocityInlet{part.startCap(), Velocity::fromSi(kNaN)}));
            },
        };
        for (std::size_t i = 0; i < breakages.size(); ++i) {
            INFO(i);
            CfdAnalysisDefinition definition = part.steadyDefinition();
            breakages[i](definition);
            CHECK_FALSE(cfd::validate(definition).has_value());
            CHECK_FALSE(CfdAnalysis::create("Bad", definition).has_value());
            CHECK_FALSE(part.setDefinition(definition).has_value());
        }
        // A reference pressure is validated too, on a definition that may carry one.
        CfdAnalysisDefinition closed{.mesh = part.control};
        closed.referencePressure = CfdReferencePressure{.value = Pressure::fromSi(kNaN), .location = {}};
        CHECK_FALSE(cfd::validate(closed).has_value());
    }
    SECTION("a duplicate boundary condition identity is refused, two conditions on one face are not (yet)") {
        CfdAnalysisDefinition definition = part.steadyDefinition();
        definition.boundaryConditions.push_back(condition(3, SlipWall{part.side(3)}));
        CHECK_THAT(refusalOf(cfd::validate(definition)).message,
                   ContainsSubstring("appears twice among the boundary conditions"));

        // A facet two conditions claim is P19-BC-001's refusal, made against the
        // geometry; the definition cannot know whether two FaceNames overlap.
        CfdAnalysisDefinition sameFace = part.steadyDefinition();
        sameFace.boundaryConditions.push_back(condition(7, SlipWall{part.side(0)}));
        CHECK(cfd::validate(sameFace).has_value());
    }
    SECTION("boundary conditions in a different order are different intent") {
        CfdAnalysisDefinition a = part.steadyDefinition();
        CfdAnalysisDefinition b = a;
        CHECK(a == b);
        std::swap(b.boundaryConditions[0], b.boundaryConditions[1]);
        CHECK_FALSE(a == b);
        CHECK_FALSE(cfd::fingerprint(a) == cfd::fingerprint(b));
    }
}

TEST_CASE("CfdData_ThePressureDatumIsRequiredIfAndOnlyIfNoBoundaryFixesIt", "[cfd][data][datum]") {
    // ADR-058's rule as a truth table over the two facts it depends on.
    FlowStudy part;
    const CfdReferencePressure reference{.value = 0_Pa, .location = Point3D{20_mm, 15_mm, 10_mm}};

    CfdAnalysisDefinition walls{.mesh = part.control};
    walls.boundaryConditions = {condition(1, NoSlipWall{part.side(0)}),
                                condition(2, MovingWall{part.endCap(), velocity(1.0, 0.0, 0.0)})};

    SECTION("nothing fixes the pressure and there is no reference: missing, but storable") {
        CHECK(cfd::pressureDatumProblem(walls) == PressureDatumProblem::Missing);
        // NOT refused by the definition: it is the state of every definition
        // before its outlet is added. Refused before a solve, by the input gate.
        CHECK(cfd::validate(walls).has_value());
    }
    SECTION("nothing fixes the pressure and a reference is given: well posed (a closed cavity)") {
        CfdAnalysisDefinition cavity = walls;
        cavity.referencePressure = reference;
        CHECK_FALSE(cfd::pressureDatumProblem(cavity).has_value());
        CHECK(cfd::validate(cavity).has_value());
    }
    SECTION("a pressure outlet fixes it and there is no reference: well posed (a channel)") {
        CHECK_FALSE(cfd::pressureDatumProblem(part.steadyDefinition()).has_value());
    }
    SECTION("a pressure inlet alone fixes it too") {
        CfdAnalysisDefinition driven = walls;
        driven.boundaryConditions.push_back(condition(3, PressureInlet{part.startCap(), 10_Pa}));
        CHECK_FALSE(cfd::pressureDatumProblem(driven).has_value());
    }
    SECTION("a boundary fixes it AND a reference is given: overdetermined, and refused") {
        CfdAnalysisDefinition both = part.steadyDefinition();
        both.referencePressure = reference;
        CHECK(cfd::pressureDatumProblem(both) == PressureDatumProblem::Overdetermined);
        CHECK_THAT(refusalOf(cfd::validate(both)).message, ContainsSubstring("overdetermined"));
        CHECK_FALSE(part.setDefinition(both).has_value());
    }
    SECTION("the names") {
        CHECK(cfd::toString(PressureDatumProblem::Missing) == "pressure_datum_missing");
        CHECK(cfd::toString(PressureDatumProblem::Overdetermined) == "pressure_datum_overdetermined");
        CHECK(cfd::toString(static_cast<PressureDatumProblem>(2)) == "unknown");
    }
}

TEST_CASE("CfdData_ANoOpEditDoesNotMoveTheRevision", "[cfd][data][analysis]") {
    FlowStudy part;
    const std::uint64_t before = part.revision();
    const Result<bool> noop = part.setDefinition(part.steadyDefinition());
    REQUIRE(noop.has_value());
    CHECK_FALSE(*noop);
    CHECK(part.revision() == before);

    CfdAnalysisDefinition changed = part.steadyDefinition();
    changed.iteration.pressureRelaxation = 0.2;
    const Result<bool> effective = part.setDefinition(changed);
    REQUIRE(effective.has_value());
    CHECK(*effective);
    CHECK(part.revision() == before + 1);
}

TEST_CASE("CfdData_AddUndoRedoRestoresTheAnalysisExactly", "[cfd][data][analysis][history]") {
    // The history stores COMMANDS, never a snapshot of the document, so there is
    // nowhere a field could be captured. What comes back is the same identity and
    // the same intent.
    FlowStudy part;
    CommandHistory history;
    CfdAnalysisDefinition definition = part.transientDefinition();
    definition.gravity = CfdGravity{acceleration(0.0, 0.0, -9.81)};
    auto study = CfdAnalysis::create("Second", definition);
    REQUIRE(study.has_value());
    auto command = std::make_unique<AddObjectCommand>(std::move(*study));
    const AddObjectCommand* raw = command.get();
    const std::size_t before = part.document.objectCount();
    REQUIRE(history.execute(part.document, std::move(command)).has_value());

    const CfdAnalysisId added = CfdAnalysisId::fromValue(raw->objectId().value());
    REQUIRE(added.isValid());
    REQUIRE(cfd::findCfdAnalysis(part.document, added) != nullptr);
    CHECK(part.document.objectCount() == before + 1U);

    REQUIRE(history.undo(part.document).has_value());
    CHECK(cfd::findCfdAnalysis(part.document, added) == nullptr);
    CHECK(part.document.objectCount() == before);

    REQUIRE(history.redo(part.document).has_value());
    const CfdAnalysis* restored = cfd::findCfdAnalysis(part.document, added);
    REQUIRE(restored != nullptr);
    CHECK(restored->analysisId() == added);
    CHECK(restored->definition() == definition);
    CHECK(cfd::fingerprint(restored->definition()) == cfd::fingerprint(definition));
    CHECK(restored->name() == "Second");
    CHECK(cfd::cfdAnalysisIds(part.document) == std::vector{part.analysis, added});
}

TEST_CASE("CfdData_SavingADocumentWithACfdAnalysisIsRefusedByName", "[cfd][data][analysis][persistence]") {
    // THE PERSISTENCE AUDIT, as a test. P19-PERSIST-001 owns the file format.
    // Until it exists the honest behaviour is a REFUSAL THAT NAMES THE TYPE --
    // not a file that silently drops the analysis -- so that the persistence
    // milestone replaces a known refusal rather than a surprise.
    FlowStudy part;
    const Result<std::string> json = io::documentToJson(part.document);
    REQUIRE_FALSE(json.has_value());
    CHECK_THAT(json.error().message, ContainsSubstring("'cfd-analysis' cannot be saved"));
}

// ---------------------------------------------------------------------------
// The semantic fingerprint
// ---------------------------------------------------------------------------

TEST_CASE("CfdData_TheFingerprintIsAGoldenFunctionOfTheText", "[cfd][data][fingerprint]") {
    // AN INDEPENDENT ORACLE, NOT A RECORDING. The expected text below is written
    // from the documented format, not printed by the code and pasted back, and
    // the digest is recomputed by a second FNV-1a in this file. The same text and
    // the same number on every preset is the cross-preset determinism claim.
    CfdAnalysisDefinition definition{.mesh = MeshControlId::fromValue(7)};
    const ObjectId feature = ObjectId::fromValue(3);
    definition.boundaryConditions = {
        condition(1, VelocityInlet{FaceName{feature, FaceSelector{.role = FaceRole::StartCap}}, 0.25_m_per_s}),
        condition(2, PressureOutlet{FaceName{feature, FaceSelector{.role = FaceRole::EndCap}}, 0_Pa}),
        condition(3, NoSlipWall{FaceName{feature, FaceSelector{.role = FaceRole::Side,
                                                               .entity = EntityId::fromValue(5)}}}),
    };
    definition.gravity = CfdGravity{acceleration(0.0, 0.0, -9.81)};
    REQUIRE(cfd::validate(definition).has_value());

    const std::string expected = "cfd-definition-v1\n"
                                 "mesh=7\n"
                                 "mode=steady\n"
                                 "boundary_conditions=3\n"
                                 "bc[0].id=1\n"
                                 "bc[0].kind=velocity_inlet\n"
                                 "bc[0].face=feature=3;role=start_cap;entity=-;along=-;along_sketch=-;edge=-;copies=[]\n"
                                 "bc[0].normal_speed=0.25\n"
                                 "bc[1].id=2\n"
                                 "bc[1].kind=pressure_outlet\n"
                                 "bc[1].face=feature=3;role=end_cap;entity=-;along=-;along_sketch=-;edge=-;copies=[]\n"
                                 "bc[1].static_pressure=0\n"
                                 "bc[2].id=3\n"
                                 "bc[2].kind=no_slip_wall\n"
                                 "bc[2].face=feature=3;role=side;entity=5;along=-;along_sketch=-;edge=-;copies=[]\n"
                                 "initial.velocity=0,0,0\n"
                                 "initial.pressure=0\n"
                                 "reference_pressure=none\n"
                                 "gravity.acceleration=0,0,-9.81\n"
                                 "gravity.reference_height=0\n"
                                 "iteration.velocity_relaxation=0.7\n"
                                 "iteration.pressure_relaxation=0.3\n"
                                 "convergence.momentum_residual_tolerance=1e-06\n"
                                 "convergence.continuity_residual_tolerance=1e-06\n"
                                 "convergence.divergence_factor=1000\n"
                                 "linear_solver.relative_residual_tolerance=1e-09\n"
                                 "steady.max_iterations=1000\n"
                                 "transient=none\n"
                                 "output.residual_history=final_only\n";
    CHECK(cfd::canonicalText(definition) == expected);
    const CfdDefinitionFingerprint digest = cfd::fingerprint(definition);
    CHECK(digest.value == independentFnv1a(expected));
    CHECK(cfd::toString(digest) == std::format("cfd-definition-v1:{:016x}", independentFnv1a(expected)));
    WARN("golden fingerprint " << cfd::toString(digest));
}

TEST_CASE("CfdData_TheSecondGoldenCoversEveryKindAndTheTransientLines", "[cfd][data][fingerprint]") {
    // The first golden is a channel. This one is a transient cavity with every
    // record the first leaves out -- a pressure inlet, a moving wall, a slip wall,
    // a face with a path edge and a copy, the reference pressure, a non-zero
    // reference height and every non-default setting -- so no line of the format
    // can change unnoticed. Written from the format, digested independently.
    CfdAnalysisDefinition definition{.mesh = MeshControlId::fromValue(12)};
    definition.mode = CfdAnalysisMode::Transient;
    const ObjectId feature = ObjectId::fromValue(4);
    definition.boundaryConditions = {
        condition(5, PressureInlet{FaceName{feature, FaceSelector{.role = FaceRole::StartCap}}, -12.5_Pa}),
        condition(6, MovingWall{FaceName{feature, FaceSelector{.role = FaceRole::EndCap}}, velocity(0.0, 1.5, 0.0)}),
        condition(7, SlipWall{FaceName{feature,
                                       FaceSelector{.role = FaceRole::Side,
                                                    .entity = EntityId::fromValue(2),
                                                    .along = EntityId::fromValue(9),
                                                    .alongSketch = SketchId::fromValue(8),
                                                    .copies = {FaceCopy{ObjectId::fromValue(20), 3},
                                                               FaceCopy{ObjectId::fromValue(21), 1}}}}}),
    };
    definition.initialCondition = CfdInitialCondition{.velocity = velocity(0.125, 0.0, -0.5), .pressure = 7_Pa};
    definition.gravity = CfdGravity{.acceleration = acceleration(0.0, -9.80665, 0.0), .referenceHeight = 0.25_m};
    definition.iteration = CfdIterationSettings{.velocityRelaxation = 0.5, .pressureRelaxation = 0.125};
    definition.convergence = CfdConvergenceSettings{.momentumResidualTolerance = 2.5e-7,
                                                    .continuityResidualTolerance = 1e-8,
                                                    .divergenceFactor = 50.0};
    definition.linearSolver = CfdLinearSolverSettings{.relativeResidualTolerance = 1e-12};
    definition.steady.reset();
    definition.transient = CfdTransientSettings{
        .timeStep = 0.001_s, .endTime = 0.5_s, .maxIterationsPerStep = 20, .sampling = CfdTimeSampling::EveryStep};
    definition.output = CfdOutputSettings{.residualHistory = CfdResidualHistory::EveryIteration};
    // The pressure inlet fixes p, so there is no reference pressure here; the
    // reference lines are pinned by the third definition below.
    REQUIRE(cfd::validate(definition).has_value());

    const std::string expected =
        "cfd-definition-v1\n"
        "mesh=12\n"
        "mode=transient\n"
        "boundary_conditions=3\n"
        "bc[0].id=5\n"
        "bc[0].kind=pressure_inlet\n"
        "bc[0].face=feature=4;role=start_cap;entity=-;along=-;along_sketch=-;edge=-;copies=[]\n"
        "bc[0].static_pressure=-12.5\n"
        "bc[1].id=6\n"
        "bc[1].kind=moving_wall\n"
        "bc[1].face=feature=4;role=end_cap;entity=-;along=-;along_sketch=-;edge=-;copies=[]\n"
        "bc[1].velocity=0,1.5,0\n"
        "bc[2].id=7\n"
        "bc[2].kind=slip\n"
        "bc[2].face=feature=4;role=side;entity=2;along=9;along_sketch=8;edge=-;copies=[20:3,21:1]\n"
        "initial.velocity=0.125,0,-0.5\n"
        "initial.pressure=7\n"
        "reference_pressure=none\n"
        "gravity.acceleration=0,-9.80665,0\n"
        "gravity.reference_height=0.25\n"
        "iteration.velocity_relaxation=0.5\n"
        "iteration.pressure_relaxation=0.125\n"
        "convergence.momentum_residual_tolerance=2.5e-07\n"
        "convergence.continuity_residual_tolerance=1e-08\n"
        "convergence.divergence_factor=50\n"
        "linear_solver.relative_residual_tolerance=1e-12\n"
        "steady=none\n"
        "transient.time_step=0.001\n"
        "transient.end_time=0.5\n"
        "transient.max_iterations_per_step=20\n"
        "transient.sampling=every_step\n"
        "output.residual_history=every_iteration\n";
    CHECK(cfd::canonicalText(definition) == expected);
    CHECK(cfd::fingerprint(definition).value == independentFnv1a(expected));

    // The reference-pressure and chamfer-edge lines, on a closed cavity.
    CfdAnalysisDefinition cavity{.mesh = MeshControlId::fromValue(2)};
    cavity.boundaryConditions = {condition(
        1, NoSlipWall{FaceName{feature, FaceSelector{.role = FaceRole::Chamfer, .edge = ChamferEdgeId::fromValue(3)}}})};
    cavity.referencePressure = CfdReferencePressure{.value = 101325_Pa, .location = Point3D{1_m, -2_m, 0.5_m}};
    REQUIRE(cfd::validate(cavity).has_value());
    const std::string cavityText = cfd::canonicalText(cavity);
    CHECK_THAT(cavityText,
               ContainsSubstring("bc[0].face=feature=4;role=chamfer;entity=-;along=-;along_sketch=-;edge=3;copies=[]\n"));
    CHECK_THAT(cavityText, ContainsSubstring("reference_pressure.value=101325\n"
                                             "reference_pressure.location=1,-2,0.5\n"
                                             "gravity=none\n"));
    CHECK(cfd::fingerprint(cavity).value == independentFnv1a(cavityText));
}

TEST_CASE("CfdData_TheFingerprintIgnoresIdentityAndEverythingButMeaning", "[cfd][data][fingerprint]") {
    FlowStudy part;
    const CfdAnalysisDefinition definition = part.steadyDefinition();
    const CfdDefinitionFingerprint first = cfd::fingerprint(definition);

    // The same value, built again, copied, and held by a second object with
    // another name, id and revision: one fingerprint.
    CHECK(cfd::fingerprint(part.steadyDefinition()) == first);
    const CfdAnalysisDefinition copy = definition; // NOLINT(performance-unnecessary-copy-initialization)
    CHECK(cfd::fingerprint(copy) == first);
    auto other = CfdAnalysis::create("AnotherName", definition);
    REQUIRE(other.has_value());
    const CfdAnalysisId second = CfdAnalysisId::fromValue(require(part.document.addObject(std::move(*other))).value());
    REQUIRE(second != part.analysis);
    CHECK(cfd::fingerprint(cfd::findCfdAnalysis(part.document, second)->definition()) == first);
    CHECK(cfd::fingerprint(part.definition()) == first);

    // -0.0 and +0.0 compare equal, so they must digest alike.
    CfdAnalysisDefinition negativeZero = definition;
    negativeZero.initialCondition.pressure = Pressure::fromSi(-0.0);
    negativeZero.initialCondition.velocity = velocity(-0.0, 0.0, -0.0);
    REQUIRE(negativeZero == definition);
    CHECK(cfd::canonicalText(negativeZero) == cfd::canonicalText(definition));
    CHECK(cfd::fingerprint(negativeZero) == first);

    // A value that differs only in its last bit is a different value.
    CfdAnalysisDefinition nextUp = definition;
    nextUp.iteration.velocityRelaxation = std::nextafter(0.7, 1.0);
    REQUIRE_FALSE(nextUp == definition);
    CHECK_FALSE(cfd::fingerprint(nextUp) == first);
}

TEST_CASE("CfdData_TheFingerprintCoversEveryField", "[cfd][data][fingerprint]") {
    // THE TRUTH TABLE OF THE DIGEST. Every field mutated ALONE must change both
    // operator== and the fingerprint; a field the text forgot would pass every
    // other row. The size mirrors pin the records the text walks, including
    // core's FaceName and FaceSelector: a field added there must be added here.
    struct PermittedSelector {
        FaceRole role;
        std::optional<EntityId> entity;
        std::optional<EntityId> along;
        std::optional<SketchId> alongSketch;
        std::optional<ChamferEdgeId> edge;
        std::vector<FaceCopy> copies;
    };
    static_assert(sizeof(FaceSelector) == sizeof(PermittedSelector));
    struct PermittedFaceName {
        ObjectId feature;
        FaceSelector face;
    };
    static_assert(sizeof(FaceName) == sizeof(PermittedFaceName));
    static_assert(sizeof(CfdInitialCondition) == sizeof(Velocity3) + sizeof(Pressure));
    static_assert(sizeof(CfdIterationSettings) == 2 * sizeof(double));
    static_assert(sizeof(CfdConvergenceSettings) == 3 * sizeof(double));
    static_assert(sizeof(CfdLinearSolverSettings) == sizeof(double));
    static_assert(sizeof(CfdSteadySettings) == sizeof(std::uint32_t));
    struct PermittedTransient {
        Time timeStep;
        Time endTime;
        std::uint32_t maxIterationsPerStep;
        CfdTimeSampling sampling;
    };
    static_assert(sizeof(CfdTransientSettings) == sizeof(PermittedTransient));
    static_assert(sizeof(CfdOutputSettings) == sizeof(CfdResidualHistory));
    struct PermittedCopy {
        ObjectId feature;
        std::uint32_t instance;
    };
    static_assert(sizeof(FaceCopy) == sizeof(PermittedCopy));
    static_assert(sizeof(CfdReferencePressure) == sizeof(Pressure) + sizeof(Point3D));
    static_assert(sizeof(CfdGravity) == sizeof(Acceleration3) + sizeof(Length));
    // The boundary condition is a CLASS: its id and its payload variant, nothing
    // else. A cache of resolved facets added to it would be compared by == and
    // not written to the text -- and would be derived state inside intent.
    struct PermittedCondition {
        CfdBoundaryConditionId id;
        std::variant<VelocityInlet, PressureInlet, PressureOutlet, NoSlipWall, MovingWall, SlipWall> payload;
    };
    static_assert(sizeof(CfdBoundaryCondition) == sizeof(PermittedCondition));
    // And the COUNTS, which a size cannot see when a member hides in padding.
    [[maybe_unused]] const auto& [selRole, selEntity, selAlong, selSketch, selEdge, selCopies] = FaceSelector{};
    [[maybe_unused]] const auto& [nameFeature, nameFace] = FaceName{};
    [[maybe_unused]] const auto& [copyFeature, copyInstance] = FaceCopy{};
    [[maybe_unused]] const auto& [stepTime, endTime, perStep, sampling] = CfdTransientSettings{};
    [[maybe_unused]] const auto& [refValue, refLocation] = CfdReferencePressure{};
    [[maybe_unused]] const auto& [gravityVector, gravityHeight] = CfdGravity{};
    [[maybe_unused]] const auto& [velocityRelaxation, pressureRelaxation] = CfdIterationSettings{};
    [[maybe_unused]] const auto& [momentumTolerance, continuityTolerance, divergence] = CfdConvergenceSettings{};
    [[maybe_unused]] const auto& [inletFace, inletSpeed] = VelocityInlet{};
    [[maybe_unused]] const auto& [pInletFace, pInletPressure] = PressureInlet{};
    [[maybe_unused]] const auto& [outletFace, outletPressure] = PressureOutlet{};
    [[maybe_unused]] const auto& [wallFace] = NoSlipWall{};
    [[maybe_unused]] const auto& [movingFace, movingVelocity] = MovingWall{};
    [[maybe_unused]] const auto& [slipFace] = SlipWall{};

    FlowStudy part;
    CfdAnalysisDefinition base = part.steadyDefinition();
    base.boundaryConditions.push_back(condition(7, PressureInlet{part.startCap(), 5_Pa}));
    base.boundaryConditions.push_back(condition(8, MovingWall{part.endCap(), velocity(0.5, 0.0, 0.0)}));
    FaceName copied = part.side(1);
    copied.face.copies.push_back(FaceCopy{ObjectId::fromValue(11), 2});
    base.boundaryConditions.push_back(condition(9, SlipWall{copied}));
    base.gravity = CfdGravity{.acceleration = acceleration(0.0, -9.81, 0.0), .referenceHeight = 0.1_m};
    CfdAnalysisDefinition transientBase = part.transientDefinition();

    using Edit = std::function<void(CfdAnalysisDefinition&)>;
    const std::vector<std::pair<std::string, Edit>> steadyEdits{
        {"mesh", [](auto& d) { d.mesh = MeshControlId::fromValue(d.mesh.value() + 100); }},
        {"bc id", [](auto& d) { d.boundaryConditions[0] = condition(42, *d.boundaryConditions[0].velocityInlet()); }},
        {"bc kind", [](auto& d) { d.boundaryConditions[2] = condition(3, SlipWall{d.boundaryConditions[2].face()}); }},
        {"bc face feature", [](auto& d) {
             VelocityInlet inlet = *d.boundaryConditions[0].velocityInlet();
             inlet.face.feature = ObjectId::fromValue(inlet.face.feature.value() + 1);
             d.boundaryConditions[0] = condition(1, inlet);
         }},
        {"bc face role", [](auto& d) {
             VelocityInlet inlet = *d.boundaryConditions[0].velocityInlet();
             inlet.face.face.role = FaceRole::EndCap;
             d.boundaryConditions[0] = condition(1, inlet);
         }},
        {"bc face entity", [](auto& d) {
             NoSlipWall wall = *d.boundaryConditions[2].noSlipWall();
             wall.face.face.entity = EntityId::fromValue(wall.face.face.entity->value() + 1);
             d.boundaryConditions[2] = condition(3, wall);
         }},
        {"bc face along", [](auto& d) {
             NoSlipWall wall = *d.boundaryConditions[2].noSlipWall();
             wall.face.face.along = EntityId::fromValue(1);
             d.boundaryConditions[2] = condition(3, wall);
         }},
        {"bc face along sketch", [](auto& d) {
             NoSlipWall wall = *d.boundaryConditions[2].noSlipWall();
             wall.face.face.alongSketch = SketchId::fromValue(1);
             d.boundaryConditions[2] = condition(3, wall);
         }},
        {"bc face edge", [](auto& d) {
             NoSlipWall wall = *d.boundaryConditions[2].noSlipWall();
             wall.face.face.edge = ChamferEdgeId::fromValue(1);
             d.boundaryConditions[2] = condition(3, wall);
         }},
        {"bc face copies", [](auto& d) {
             NoSlipWall wall = *d.boundaryConditions[2].noSlipWall();
             wall.face.face.copies.push_back(FaceCopy{ObjectId::fromValue(11), 2});
             d.boundaryConditions[2] = condition(3, wall);
         }},
        {"inlet speed", [](auto& d) {
             VelocityInlet inlet = *d.boundaryConditions[0].velocityInlet();
             inlet.normalSpeed = 0.2_m_per_s;
             d.boundaryConditions[0] = condition(1, inlet);
         }},
        {"outlet pressure", [](auto& d) {
             PressureOutlet outlet = *d.boundaryConditions[1].pressureOutlet();
             outlet.staticPressure = 1_Pa;
             d.boundaryConditions[1] = condition(2, outlet);
         }},
        {"pressure inlet pressure", [](auto& d) {
             PressureInlet inlet = *d.boundaryConditions[6].pressureInlet();
             inlet.staticPressure = 6_Pa;
             d.boundaryConditions[6] = condition(7, inlet);
         }},
        {"wall velocity", [](auto& d) {
             MovingWall wall = *d.boundaryConditions[7].movingWall();
             wall.velocity.z = 0.1_m_per_s;
             d.boundaryConditions[7] = condition(8, wall);
         }},
        {"a condition removed", [](auto& d) { d.boundaryConditions.pop_back(); }},
        {"initial velocity", [](auto& d) { d.initialCondition.velocity.y = 0.01_m_per_s; }},
        {"initial pressure", [](auto& d) { d.initialCondition.pressure = 3_Pa; }},
        {"gravity removed", [](auto& d) { d.gravity.reset(); }},
        {"gravity changed", [](auto& d) { d.gravity->acceleration.x = 0.5_m_per_s2; }},
        {"gravity reference height", [](auto& d) { d.gravity->referenceHeight = 0.2_m; }},
        {"bc face copy instance", [](auto& d) {
             SlipWall wall = *d.boundaryConditions[8].slipWall();
             wall.face.face.copies.front().instance = 3;
             d.boundaryConditions[8] = condition(9, wall);
         }},
        {"bc face copy feature", [](auto& d) {
             SlipWall wall = *d.boundaryConditions[8].slipWall();
             wall.face.face.copies.front().feature = ObjectId::fromValue(12);
             d.boundaryConditions[8] = condition(9, wall);
         }},
        {"velocity relaxation", [](auto& d) { d.iteration.velocityRelaxation = 0.6; }},
        {"pressure relaxation", [](auto& d) { d.iteration.pressureRelaxation = 0.25; }},
        {"momentum tolerance", [](auto& d) { d.convergence.momentumResidualTolerance = 1e-7; }},
        {"continuity tolerance", [](auto& d) { d.convergence.continuityResidualTolerance = 1e-5; }},
        {"divergence factor", [](auto& d) { d.convergence.divergenceFactor = 100.0; }},
        {"linear tolerance", [](auto& d) { d.linearSolver.relativeResidualTolerance = 1e-10; }},
        {"steady budget", [](auto& d) { d.steady->maxIterations = 999; }},
        {"residual history", [](auto& d) { d.output.residualHistory = CfdResidualHistory::EveryIteration; }},
    };
    const std::vector<std::pair<std::string, Edit>> transientEdits{
        {"mode", [](auto& d) {
             d.mode = CfdAnalysisMode::Steady;
             d.transient.reset();
             d.steady = CfdSteadySettings{};
         }},
        {"time step", [](auto& d) { d.transient->timeStep = 0.02_s; }},
        {"end time", [](auto& d) { d.transient->endTime = 2_s; }},
        {"per-step budget", [](auto& d) { d.transient->maxIterationsPerStep = 51; }},
        {"sampling", [](auto& d) { d.transient->sampling = CfdTimeSampling::EveryStep; }},
    };
    // The reference pressure needs a definition nothing else fixes.
    CfdAnalysisDefinition cavity{.mesh = part.control};
    cavity.boundaryConditions = {condition(1, NoSlipWall{part.side(0)})};
    cavity.referencePressure = CfdReferencePressure{.value = 0_Pa, .location = Point3D{1_mm, 2_mm, 3_mm}};
    const std::vector<std::pair<std::string, Edit>> cavityEdits{
        {"reference removed", [](auto& d) { d.referencePressure.reset(); }},
        {"reference value", [](auto& d) { d.referencePressure->value = 1_Pa; }},
        {"reference location x", [](auto& d) { d.referencePressure->location.x = 5_mm; }},
        {"reference location y", [](auto& d) { d.referencePressure->location.y = 5_mm; }},
        {"reference location z", [](auto& d) { d.referencePressure->location.z = 4_mm; }},
    };

    std::size_t rows = 0;
    for (const auto& [from, edits] : {std::pair{base, steadyEdits}, std::pair{transientBase, transientEdits},
                                      std::pair{cavity, cavityEdits}}) {
        REQUIRE(cfd::validate(from).has_value());
        for (const auto& [name, edit] : edits) {
            INFO(name);
            CfdAnalysisDefinition changed = from;
            edit(changed);
            CHECK_FALSE(changed == from);
            CHECK_FALSE(cfd::canonicalText(changed) == cfd::canonicalText(from));
            CHECK_FALSE(cfd::fingerprint(changed) == cfd::fingerprint(from));
            ++rows;
        }
    }
    CHECK(rows == 40);
}

// ---------------------------------------------------------------------------
// The result container
// ---------------------------------------------------------------------------

TEST_CASE("CfdData_AResultMustMatchItsMeshAndItsMode", "[cfd][data][result]") {
    FlowStudy part;
    part.requireMesh();
    const CfdResultSource source = part.currentSource();
    const meshing::VolumeMesh& mesh = part.mesh();

    SECTION("well formed: accepted, and bound to its mesh") {
        const CfdResult result = part.resultFor(source);
        CHECK(result.describes(mesh));
        CHECK(result.source() == source);
        CHECK(result.metadata() == FlowStudy::metadataFor(CfdAnalysisMode::Steady));
    }
    SECTION("a source naming another mesh is refused") {
        CfdResultSource other = source;
        other.mesh = meshing::MeshStamp{};
        CHECK_THAT(CfdResult::create(other, mesh, FlowStudy::metadataFor(CfdAnalysisMode::Steady)).error().message,
                   ContainsSubstring("does not name this mesh"));
    }
    SECTION("zero outer iterations is not an accepted result") {
        CHECK_FALSE(CfdResult::create(source, mesh, FlowStudy::metadataFor(CfdAnalysisMode::Steady, 0)).has_value());
    }
    SECTION("a residual or a mass-balance measure that is not a norm is refused") {
        for (const double bad : {kNaN, kInf, -1e-9}) {
            INFO(bad);
            CfdResultMetadata residual = FlowStudy::metadataFor(CfdAnalysisMode::Steady);
            residual.residuals.momentumY = bad;
            CHECK_FALSE(CfdResult::create(source, mesh, residual).has_value());
            CfdResultMetadata balance = FlowStudy::metadataFor(CfdAnalysisMode::Steady);
            balance.massBalance.globalImbalance = bad;
            CHECK_FALSE(CfdResult::create(source, mesh, balance).has_value());
        }
    }
    SECTION("a steady result has no time and no step") {
        CfdResultMetadata timed = FlowStudy::metadataFor(CfdAnalysisMode::Steady);
        timed.time = 0_s;
        CHECK_THAT(CfdResult::create(source, mesh, timed).error().message, ContainsSubstring("no time and no step"));
        CfdResultMetadata stepped = FlowStudy::metadataFor(CfdAnalysisMode::Steady);
        stepped.step = 1U;
        CHECK_FALSE(CfdResult::create(source, mesh, stepped).has_value());
    }
    SECTION("a transient sample needs a time after 0 and a step from 1") {
        CfdResultSource transient = source;
        transient.mode = CfdAnalysisMode::Transient;
        CHECK(CfdResult::create(transient, mesh, FlowStudy::metadataFor(CfdAnalysisMode::Transient)).has_value());
        for (const std::optional<Time> badTime : {std::optional<Time>{}, std::optional<Time>{0_s},
                                                  std::optional<Time>{Time::fromSi(-1.0)},
                                                  std::optional<Time>{Time::fromSi(kNaN)}}) {
            CfdResultMetadata metadata = FlowStudy::metadataFor(CfdAnalysisMode::Transient);
            metadata.time = badTime;
            CHECK_FALSE(CfdResult::create(transient, mesh, metadata).has_value());
        }
        CfdResultMetadata stepZero = FlowStudy::metadataFor(CfdAnalysisMode::Transient);
        stepZero.step = 0U;
        CHECK_FALSE(CfdResult::create(transient, mesh, stepZero).has_value());
        CfdResultMetadata noStep = FlowStudy::metadataFor(CfdAnalysisMode::Transient);
        noStep.step.reset();
        CHECK_FALSE(CfdResult::create(transient, mesh, noStep).has_value());
    }
}

TEST_CASE("CfdData_AResultHoldsProvenanceAndMetadataOnly", "[cfd][data][result]") {
    // ADR-061: the fields of the solution (p, U per cell; phi per face) arrive
    // with P19-DOF-001, which extends this mirror. Until then the container is a
    // source and a metadata record and nothing else -- in particular not a mesh,
    // so its size does not depend on the mesh.
    struct PermittedResult {
        CfdResultSource source;
        CfdResultMetadata metadata;
    };
    static_assert(sizeof(CfdResult) == sizeof(PermittedResult));
    struct PermittedSource {
        ObjectId body;
        MeshControlId control;
        meshing::GeometryRevision geometry;
        meshing::MeshStamp mesh;
        MaterialId fluid;
        std::uint64_t fluidRevision;
        CfdAnalysisId analysis;
        std::uint64_t analysisRevision;
        CfdDefinitionFingerprint definition;
        CfdAnalysisMode mode;
    };
    static_assert(sizeof(CfdResultSource) == sizeof(PermittedSource));
    struct PermittedMetadata {
        std::uint32_t outerIterations;
        CfdResiduals residuals;
        CfdMassBalance massBalance;
        std::optional<Time> time;
        std::optional<std::uint32_t> step;
    };
    static_assert(sizeof(CfdResultMetadata) == sizeof(PermittedMetadata));
    static_assert(sizeof(CfdResiduals) == 4 * sizeof(double));
    static_assert(sizeof(CfdMassBalance) == 2 * sizeof(double));
    // The member counts, which padding would hide from a size (seven bytes after
    // the source's `mode`, four after the metadata's iteration count).
    [[maybe_unused]] const auto& [body, control, geometry, mesh, fluid, fluidRevision, analysis, analysisRevision,
                                  definition, mode] = CfdResultSource{};
    [[maybe_unused]] const auto& [iterations, residuals, balance, time, step] = CfdResultMetadata{};
    [[maybe_unused]] const auto& [stage, error, failedOn, failedAfter] = CfdSolveFailure{};

    // No default constructor, no public constructor: only the factory.
    static_assert(!std::is_default_constructible_v<CfdResult>);
    static_assert(!std::is_constructible_v<CfdResult, CfdResultSource, CfdResultMetadata>);
    // And a failure is not a result.
    static_assert(!std::is_constructible_v<CfdResult, CfdSolveFailure>);
    static_assert(!std::is_convertible_v<CfdSolveFailure, CfdResult>);
    SUCCEED("a result is its provenance and its metadata");
}

TEST_CASE("CfdData_ResultsAreImmutableAndReadOnly", "[cfd][data][result]") {
    static_assert(
        std::is_same_v<decltype(std::declval<const CfdResult&>().source()), const CfdResultSource&>);
    static_assert(
        std::is_same_v<decltype(std::declval<const CfdResult&>().metadata()), const CfdResultMetadata&>);
    static_assert(std::is_same_v<decltype(std::declval<CfdResult&>().source()), const CfdResultSource&>,
                  "not even a non-const result offers a mutable source");
    static_assert(std::is_same_v<decltype(std::declval<const CfdResultSlot&>().result()), const CfdResult*>);
    static_assert(std::is_same_v<decltype(std::declval<CfdResultSlot&>().result()), const CfdResult*>);
    SUCCEED("the compile-fail group tries each write");
}

// ---------------------------------------------------------------------------
// Currentness
// ---------------------------------------------------------------------------

TEST_CASE("CfdData_SameIdsNewMeshGenerationIsStale", "[cfd][data][currentness]") {
    // THE CENTRAL CLAIM. Remeshing the same body with the same settings gives the
    // same counts AND THE SAME NodeIds -- and a different mesh. A cell index read
    // across that gap would put every velocity on whatever tetrahedron now has
    // that number. The stamp is identity; counts and ids are not.
    FlowStudy part;
    part.requireMesh();
    const CfdResultSource before = part.currentSource();
    const CfdResult result = part.resultFor(before);
    CHECK(cfd::resultCurrency(&result, before) == ResultCurrency::Current);

    std::vector<meshing::NodeId> idsBefore;
    for (const meshing::Node& node : part.mesh().mesh().nodes()) {
        idsBefore.push_back(node.id);
    }
    const std::size_t tetrahedraBefore = part.mesh().tetrahedronCount();
    part.requireMesh();
    std::vector<meshing::NodeId> idsAfter;
    for (const meshing::Node& node : part.mesh().mesh().nodes()) {
        idsAfter.push_back(node.id);
    }
    REQUIRE(idsAfter == idsBefore);
    REQUIRE(part.mesh().tetrahedronCount() == tetrahedraBefore);

    CHECK_FALSE(result.describes(part.mesh()));
    const CfdResultSource after = part.currentSource();
    CHECK(cfd::resultCurrency(&result, after) == ResultCurrency::Stale);
    CHECK(cfd::staleReasons(result.source(), after) == std::vector{StaleReason::Mesh});
    CHECK(part.state(&result, nullptr) == AnalysisState::SolvedStale);
    WARN(std::format("remesh: {} nodes with identical NodeIds, {} tetrahedra either way; result stale, reason mesh",
                     idsBefore.size(), tetrahedraBefore));
}

TEST_CASE("CfdData_CurrentnessComparesSourceIdentityNeverMetadata", "[cfd][data][currentness]") {
    FlowStudy part;
    part.requireMesh();
    const CfdResultSource source = part.currentSource();

    // Different metadata, one source: both current.
    const CfdResult quick = part.resultFor(source, 5);
    const CfdResult slow = part.resultFor(source, 500);
    REQUIRE_FALSE(quick.metadata() == slow.metadata());
    CHECK(cfd::resultCurrency(&quick, source) == ResultCurrency::Current);
    CHECK(cfd::resultCurrency(&slow, source) == ResultCurrency::Current);

    // Identical metadata on two generations: one stale, one current.
    part.requireMesh();
    const CfdResultSource now = part.currentSource();
    const CfdResult fresh = part.resultFor(now, 5);
    REQUIRE(fresh.metadata() == quick.metadata());
    CHECK(cfd::resultCurrency(&quick, now) == ResultCurrency::Stale);
    CHECK(cfd::resultCurrency(&fresh, now) == ResultCurrency::Current);
    CHECK(cfd::resultCurrency(nullptr, now) == ResultCurrency::NoResult);
}

TEST_CASE("CfdData_StaleReasonsAreSensitiveToEveryField", "[cfd][data][currentness]") {
    // One row per field, each mutated ALONE.
    FlowStudy part;
    part.requireMesh();
    const CfdResultSource source = part.currentSource();
    CHECK(cfd::staleReasons(source, source).empty());

    using Edit = std::function<void(CfdResultSource&)>;
    const std::vector<std::pair<Edit, std::vector<StaleReason>>> rows{
        {[](auto& s) { s.body = ObjectId::fromValue(s.body.value() + 1000); }, {StaleReason::Body}},
        {[](auto& s) { s.control = MeshControlId::fromValue(s.control.value() + 1000); }, {StaleReason::Control}},
        {[](auto& s) { s.geometry = meshing::GeometryRevision{}; }, {StaleReason::Geometry}},
        {[](auto& s) { s.mesh = meshing::MeshStamp{}; }, {StaleReason::Mesh}},
        {[](auto& s) { s.fluid = MaterialId::fromValue(s.fluid.value() + 1000); }, {StaleReason::Fluid}},
        {[](auto& s) { ++s.fluidRevision; }, {StaleReason::Fluid}},
        {[](auto& s) { s.analysis = CfdAnalysisId::fromValue(s.analysis.value() + 1000); }, {StaleReason::Analysis}},
        {[](auto& s) { ++s.analysisRevision; }, {StaleReason::Analysis}},
        {[](auto& s) { ++s.definition.value; }, {StaleReason::Definition}},
        {[](auto& s) { s.mode = CfdAnalysisMode::Transient; }, {StaleReason::Mode}},
    };
    for (std::size_t i = 0; i < rows.size(); ++i) {
        INFO(i);
        CfdResultSource changed = source;
        rows[i].first(changed);
        REQUIRE_FALSE(changed == source);
        CHECK(cfd::staleReasons(source, changed) == rows[i].second);
    }

    // Several at once: every one reported, in the enum's order.
    CfdResultSource many = source;
    many.mode = CfdAnalysisMode::Transient;
    many.mesh = meshing::MeshStamp{};
    ++many.fluidRevision;
    CHECK(cfd::staleReasons(source, many) ==
          std::vector{StaleReason::Mesh, StaleReason::Fluid, StaleReason::Mode});
}

TEST_CASE("CfdData_EveryDefinitionEditMakesTheResultStale", "[cfd][data][currentness]") {
    FlowStudy part;
    part.requireMesh();

    const std::vector<std::function<void(CfdAnalysisDefinition&)>> edits{
        [&part](auto& d) { d.boundaryConditions[0] = condition(1, VelocityInlet{part.startCap(), 0.2_m_per_s}); },
        [&part](auto& d) { d.boundaryConditions.push_back(condition(7, SlipWall{part.side(0)})); },
        [](auto& d) { d.initialCondition.pressure = 1_Pa; },
        [](auto& d) { d.gravity = CfdGravity{acceleration(0.0, 0.0, -9.81)}; },
        [](auto& d) { d.iteration.velocityRelaxation = 0.5; },
        [](auto& d) { d.convergence.momentumResidualTolerance = 1e-8; },
        [](auto& d) { d.linearSolver.relativeResidualTolerance = 1e-11; },
        [](auto& d) { d.steady->maxIterations = 10; },
        [](auto& d) { d.output.residualHistory = CfdResidualHistory::EveryIteration; },
    };
    for (std::size_t i = 0; i < edits.size(); ++i) {
        INFO(i);
        REQUIRE(part.setDefinition(part.steadyDefinition()).has_value());
        const CfdResult result = part.resultFor(part.currentSource());
        CfdAnalysisDefinition changed = part.steadyDefinition();
        edits[i](changed);
        const Result<bool> edited = part.setDefinition(changed);
        REQUIRE(edited.has_value());
        REQUIRE(*edited);
        CHECK(cfd::resultCurrency(&result, part.currentSource()) == ResultCurrency::Stale);
        CHECK(cfd::staleReasons(result.source(), part.currentSource()) ==
              std::vector{StaleReason::Analysis, StaleReason::Definition});
    }

    // A mode switch says so as well.
    REQUIRE(part.setDefinition(part.steadyDefinition()).has_value());
    const CfdResult steady = part.resultFor(part.currentSource());
    REQUIRE(part.setDefinition(part.transientDefinition()).has_value());
    CHECK(cfd::staleReasons(steady.source(), part.currentSource()) ==
          std::vector{StaleReason::Analysis, StaleReason::Definition, StaleReason::Mode});
}

TEST_CASE("CfdData_AnEditTypedBackKeepsTheResultStale", "[cfd][data][currentness]") {
    // THE REGIME, PINNED (ADR-061). Edit a value and type the old one back: the
    // definition -- and so its fingerprint -- is what the result was computed
    // from, but the analysis revision has moved twice. The result stays STALE,
    // and the reasons say exactly that only the revision differs. This is P18's
    // revision-keyed regime for results; adopting content keys instead is the
    // open owner decision D1 of P18-QUAL-001, and this test fails first if it is
    // adopted without being decided.
    FlowStudy part;
    part.requireMesh();
    const CfdResult result = part.resultFor(part.currentSource());

    CfdAnalysisDefinition changed = part.steadyDefinition();
    changed.iteration.pressureRelaxation = 0.2;
    REQUIRE(*part.setDefinition(changed));
    REQUIRE(*part.setDefinition(part.steadyDefinition()));

    const CfdResultSource now = part.currentSource();
    CHECK(now.definition == result.source().definition);
    CHECK(now.analysisRevision == result.source().analysisRevision + 2);
    CHECK(cfd::resultCurrency(&result, now) == ResultCurrency::Stale);
    CHECK(cfd::staleReasons(result.source(), now) == std::vector{StaleReason::Analysis});
}

TEST_CASE("CfdData_ARenameStalesTheResultByRevisionOnly", "[cfd][data][currentness]") {
    // RECORDED, NOT CHOSEN HERE: renaming a document object moves its revision
    // (core's Document::rename), so a renamed analysis's result is stale for the
    // single reason `analysis` although its meaning -- the fingerprint -- is
    // unchanged. P18's analyses behave identically; it is the revision-keyed
    // regime of ADR-061 section 5 seen from another edit.
    FlowStudy part;
    part.requireMesh();
    const CfdResult result = part.resultFor(part.currentSource());
    REQUIRE(part.document.rename(ObjectId::fromValue(part.analysis.value()), "RenamedFlow").has_value());
    const CfdResultSource now = part.currentSource();
    CHECK(now.definition == result.source().definition);
    CHECK(cfd::staleReasons(result.source(), now) == std::vector{StaleReason::Analysis});
}

TEST_CASE("CfdData_AFluidEditStalesTheResultAndNotTheMesh", "[cfd][data][currentness]") {
    // A mesh is a function of geometry and meshing intent; the density is in
    // neither.
    FlowStudy part;
    part.requireMesh();
    const CfdResultSource before = part.currentSource();
    const CfdResult result = part.resultFor(before);

    part.setDensity(1000_kg_per_m3);
    CHECK(part.mesher.currency(part.document, part.control) == meshing::MeshCurrency::Current);
    const CfdResultSource after = part.currentSource();
    CHECK(after.fluidRevision != before.fluidRevision);
    CHECK(after.mesh == before.mesh);
    CHECK(after.analysisRevision == before.analysisRevision);
    CHECK(after.definition == before.definition);
    CHECK(cfd::staleReasons(result.source(), after) == std::vector{StaleReason::Fluid});
    CHECK(part.state(&result, nullptr) == AnalysisState::SolvedStale);
}

TEST_CASE("CfdData_AGeometryEditMakesInputsUnavailable", "[cfd][data][currentness]") {
    FlowStudy part;
    part.requireMesh();
    const CfdResult result = part.resultFor(part.currentSource());
    part.changeTheGeometry();
    // The input boundary refuses before a source can be computed: there is no
    // current state to compare with, and the surviving result is offered as
    // nothing.
    CHECK_FALSE(cfd::currentResultSource(part.document, part.regenerator, part.mesher, part.analysis).has_value());
    CHECK(part.state(&result, nullptr) == AnalysisState::InputsUnavailable);
}

TEST_CASE("CfdData_TheSourceIsPopulatedFromTheDocumentAndNotDefaulted", "[cfd][data][currentness]") {
    // The truth table mutates a struct; this proves `currentResultSource` READS
    // each field from the document.
    FlowStudy part;
    part.requireMesh();
    const CfdResultSource source = part.currentSource();
    CHECK(source.body == part.feature);
    CHECK(source.control == part.control);
    CHECK(source.geometry.isValid());
    CHECK(source.mesh == part.mesh().mesh().stamp());
    CHECK(source.fluid == part.material);
    CHECK(source.fluidRevision != 0);
    CHECK(source.fluidRevision == features::findMaterial(part.document, part.material)->revision());
    CHECK(source.analysis == part.analysis);
    CHECK(source.analysisRevision != 0);
    CHECK(source.analysisRevision == part.revision());
    CHECK(source.definition == cfd::fingerprint(part.definition()));
    CHECK(source.mode == CfdAnalysisMode::Steady);

    REQUIRE(part.setDefinition(part.transientDefinition()).has_value());
    const CfdResultSource after = part.currentSource();
    CHECK(after.mode == CfdAnalysisMode::Transient);
    CHECK(after.analysisRevision == source.analysisRevision + 1);
    CHECK(after.definition == cfd::fingerprint(part.transientDefinition()));

    // There is no CFD analysis 999999 and the source says so by name.
    const Result<CfdResultSource> missing =
        cfd::currentResultSource(part.document, part.regenerator, part.mesher, CfdAnalysisId::fromValue(999999));
    REQUIRE_FALSE(missing.has_value());
    CHECK_THAT(missing.error().message, ContainsSubstring("no CFD analysis"));
}

TEST_CASE("CfdData_AViewerOnlyChangeDoesNotStaleAResult", "[cfd][data][currentness]") {
    // A quality THRESHOLD edit reclassifies elements and cannot change one, so
    // P16 keeps the mesh current -- and a result on it stays current too.
    FlowStudy part;
    part.requireMesh();
    const CfdResult result = part.resultFor(part.currentSource());
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
    CHECK(cfd::resultCurrency(&result, part.currentSource()) == ResultCurrency::Current);
    CHECK(part.state(&result, nullptr) == AnalysisState::SolvedCurrent);
}

// ---------------------------------------------------------------------------
// The lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("CfdData_FailedAttemptIsNotCurrent", "[cfd][data][lifecycle]") {
    FlowStudy part;
    part.requireMesh();
    CfdResultSlot slot;
    CHECK(slot.result() == nullptr);
    CHECK(slot.failure() == nullptr);
    CHECK(part.state(slot.result(), slot.failure()) == AnalysisState::Ready);

    // THE FAILURE IS STRUCTURED: where it stopped, why, against what, how far.
    slot.recordFailure(CfdSolveFailure{.stage = CfdFailureStage::Diverged,
                                       .error = makeError(ErrorCode::Internal, "the continuity residual grew").error(),
                                       .source = part.currentSource(),
                                       .outerIterations = 37U});
    CHECK(slot.result() == nullptr);
    REQUIRE(slot.failure() != nullptr);
    CHECK(slot.failure()->stage == CfdFailureStage::Diverged);
    CHECK_THAT(slot.failure()->error.message, ContainsSubstring("grew"));
    CHECK(slot.failure()->source == part.currentSource());
    CHECK(slot.failure()->outerIterations == 37U);
    CHECK(part.state(slot.result(), slot.failure()) == AnalysisState::SolveFailed);
    CHECK(cfd::resultCurrency(slot.result(), part.currentSource()) == ResultCurrency::NoResult);

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

TEST_CASE("CfdData_FailedAttemptDoesNotResurrectStaleResult", "[cfd][data][lifecycle]") {
    // Solve, remesh (stale), re-solve and FAIL: the stale result survives
    // untouched, the state says the attempt failed, nothing presents the old
    // result as current. Only a new success replaces it.
    FlowStudy part;
    part.requireMesh();
    CfdResultSlot slot;
    slot.recordSuccess(part.resultFor(part.currentSource()));
    const CfdResultSource solvedOn = slot.result()->source();
    const CfdResultMetadata reported = slot.result()->metadata();

    part.requireMesh();
    CHECK(part.state(slot.result(), slot.failure()) == AnalysisState::SolvedStale);

    // An input refusal: nothing resolved far enough to have a source.
    slot.recordFailure(CfdSolveFailure{.stage = CfdFailureStage::IterationLimitReached,
                                       .error = makeError(ErrorCode::Internal, "1000 iterations").error()});
    REQUIRE(slot.result() != nullptr);
    CHECK(slot.result()->source() == solvedOn);
    CHECK(slot.result()->metadata() == reported);
    CHECK(cfd::resultCurrency(slot.result(), part.currentSource()) == ResultCurrency::Stale);
    CHECK(part.state(slot.result(), slot.failure()) == AnalysisState::SolveFailed);

    // A second failure replaces the first and touches nothing else.
    slot.recordFailure(CfdSolveFailure{.stage = CfdFailureStage::NonFiniteField,
                                       .error = makeError(ErrorCode::Internal, "NaN in U").error(),
                                       .source = part.currentSource(),
                                       .outerIterations = 3U});
    CHECK(slot.failure()->stage == CfdFailureStage::NonFiniteField);
    CHECK(slot.result()->source() == solvedOn);
    CHECK(part.state(slot.result(), slot.failure()) == AnalysisState::SolveFailed);

    slot.recordSuccess(part.resultFor(part.currentSource()));
    CHECK(slot.failure() == nullptr);
    CHECK(slot.result()->source() == part.currentSource());
    CHECK(part.state(slot.result(), slot.failure()) == AnalysisState::SolvedCurrent);
}

TEST_CASE("CfdData_TheStateMachineReachesEveryStateItDeclares", "[cfd][data][state]") {
    const CfdSolveFailure failure{.stage = CfdFailureStage::LinearSolveFailed,
                                  .error = makeError(ErrorCode::Internal, "the solver gave up").error()};

    SECTION("no analysis") {
        FlowStudy part;
        CHECK(cfd::analysisState(part.document, part.regenerator, part.mesher, CfdAnalysisId::fromValue(999999),
                                 nullptr, nullptr) == AnalysisState::NoAnalysis);
        // The id of an object of another kind is not a CFD analysis either.
        CHECK(cfd::analysisState(part.document, part.regenerator, part.mesher,
                                 CfdAnalysisId::fromValue(part.control.value()), nullptr,
                                 nullptr) == AnalysisState::NoAnalysis);
    }
    SECTION("inputs unavailable: nothing meshed") {
        FlowStudy part;
        CHECK(part.state(nullptr, nullptr) == AnalysisState::InputsUnavailable);
    }
    SECTION("inputs unavailable: meshed, but no fluid") {
        FlowStudy part{false};
        part.requireMesh();
        CHECK(cfd::cfdInputProblem(part.document, part.regenerator, part.mesher, part.control) ==
              cfd::InputProblem::NoMaterialAssigned);
        CHECK(part.state(nullptr, nullptr) == AnalysisState::InputsUnavailable);
    }
    SECTION("inputs unavailable: meshed and fluid, but nothing fixes the pressure") {
        // A definition may be STORED without a datum; it is not READY without one.
        FlowStudy part;
        part.requireMesh();
        CfdAnalysisDefinition walls{.mesh = part.control};
        walls.boundaryConditions = {condition(1, NoSlipWall{part.side(0)})};
        REQUIRE(part.setDefinition(walls).has_value());
        const Result<CfdResultSource> source =
            cfd::currentResultSource(part.document, part.regenerator, part.mesher, part.analysis);
        REQUIRE_FALSE(source.has_value());
        CHECK_THAT(source.error().message, ContainsSubstring("pressure_datum_missing"));
        CHECK(part.state(nullptr, nullptr) == AnalysisState::InputsUnavailable);

        walls.referencePressure = CfdReferencePressure{.value = 0_Pa, .location = Point3D{10_mm, 10_mm, 10_mm}};
        REQUIRE(part.setDefinition(walls).has_value());
        CHECK(part.state(nullptr, nullptr) == AnalysisState::Ready);
    }
    SECTION("ready") {
        FlowStudy part;
        part.requireMesh();
        CHECK(part.state(nullptr, nullptr) == AnalysisState::Ready);
    }
    SECTION("solved and current") {
        FlowStudy part;
        part.requireMesh();
        const CfdResult result = part.resultFor(part.currentSource());
        CHECK(part.state(&result, nullptr) == AnalysisState::SolvedCurrent);
    }
    SECTION("solved and stale") {
        FlowStudy part;
        part.requireMesh();
        const CfdResult result = part.resultFor(part.currentSource());
        part.requireMesh();
        CHECK(part.state(&result, nullptr) == AnalysisState::SolvedStale);
    }
    SECTION("solve failed: with no result, over a stale one, over a current one") {
        FlowStudy part;
        part.requireMesh();
        CHECK(part.state(nullptr, &failure) == AnalysisState::SolveFailed);
        const CfdResult result = part.resultFor(part.currentSource());
        CHECK(part.state(&result, &failure) == AnalysisState::SolveFailed);
        part.requireMesh();
        CHECK(part.state(&result, &failure) == AnalysisState::SolveFailed);
    }
    SECTION("every state has a name") {
        for (const AnalysisState state : {AnalysisState::NoAnalysis, AnalysisState::InputsUnavailable,
                                          AnalysisState::Ready, AnalysisState::SolvedCurrent,
                                          AnalysisState::SolvedStale, AnalysisState::SolveFailed}) {
            CHECK(cfd::toString(state) != "unknown");
        }
        CHECK(cfd::toString(static_cast<AnalysisState>(6)) == "unknown");
    }
}

TEST_CASE("CfdData_EveryEnumeratorHasADistinctName", "[cfd][data][names]") {
    // Each roster is tied to its enum: the value after the last is unnamed, so an
    // enumerator added without a name -- or without joining its roster -- fails.
    const auto distinct = [](std::vector<std::string_view> names) {
        std::ranges::sort(names);
        return std::ranges::adjacent_find(names) == names.end() &&
               std::ranges::find(names, std::string_view{"unknown"}) == names.end();
    };

    std::vector<std::string_view> stages;
    for (const CfdFailureStage stage : cfd::kCfdFailureStages) {
        stages.push_back(cfd::toString(stage));
    }
    CHECK(distinct(stages));
    CHECK(cfd::toString(static_cast<CfdFailureStage>(cfd::kCfdFailureStages.size())) == "unknown");
    CHECK(cfd::toString(CfdFailureStage::IterationLimitReached) == "iteration_limit_reached");

    std::vector<std::string_view> reasons;
    for (std::uint8_t i = 0; i <= static_cast<std::uint8_t>(StaleReason::Mode); ++i) {
        reasons.push_back(cfd::toString(static_cast<StaleReason>(i)));
    }
    CHECK(reasons.size() == 8);
    CHECK(distinct(reasons));
    CHECK(cfd::toString(static_cast<StaleReason>(8)) == "unknown");

    CHECK(distinct({cfd::toString(ResultCurrency::NoResult), cfd::toString(ResultCurrency::Current),
                    cfd::toString(ResultCurrency::Stale)}));
    CHECK(cfd::toString(static_cast<ResultCurrency>(3)) == "unknown");
    CHECK(distinct({cfd::toString(CfdTimeSampling::FinalOnly), cfd::toString(CfdTimeSampling::EveryStep)}));
    CHECK(cfd::toString(static_cast<CfdTimeSampling>(2)) == "unknown");
    CHECK(distinct({cfd::toString(CfdResidualHistory::FinalOnly), cfd::toString(CfdResidualHistory::EveryIteration)}));
    CHECK(cfd::toString(static_cast<CfdResidualHistory>(2)) == "unknown");
    CHECK(cfd::toString(static_cast<cfd::VectorComponent>(3)) == "unknown");

    // describesTheModel is true for exactly one currency.
    CHECK(cfd::describesTheModel(ResultCurrency::Current));
    CHECK_FALSE(cfd::describesTheModel(ResultCurrency::Stale));
    CHECK_FALSE(cfd::describesTheModel(ResultCurrency::NoResult));
}

// ---------------------------------------------------------------------------
// Determinism
// ---------------------------------------------------------------------------

TEST_CASE("CfdData_DeterministicCanonicalOrdering", "[cfd][data][determinism]") {
    // The roster of analyses is ascending by identity whatever order they were
    // INSERTED in, and a removed one leaves a gap, not a renumbering. (The
    // document holds its objects in a map keyed by id, so the roster's own sort is
    // a second guarantee; this checks the answer, including after an earlier id
    // is re-inserted LAST, as redo does.)
    FlowStudy part;
    std::vector<CfdAnalysisId> added{part.analysis};
    for (int i = 0; i < 3; ++i) {
        auto study = CfdAnalysis::create(std::format("Flow{}", i), part.steadyDefinition());
        REQUIRE(study.has_value());
        added.push_back(CfdAnalysisId::fromValue(require(part.document.addObject(std::move(*study))).value()));
    }
    const std::vector<CfdAnalysisId> roster = cfd::cfdAnalysisIds(part.document);
    CHECK(roster == added);
    CHECK(std::ranges::is_sorted(roster, [](CfdAnalysisId a, CfdAnalysisId b) { return a.value() < b.value(); }));

    Result<std::unique_ptr<DocumentObject>> removed = part.document.removeObject(added[1]);
    REQUIRE(removed.has_value());
    CHECK(cfd::cfdAnalysisIds(part.document) == std::vector{added[0], added[2], added[3]});
    REQUIRE(part.document.insertObject(std::move(*removed)).has_value());
    CHECK(cfd::cfdAnalysisIds(part.document) == added);

    // The same document state gives the same source, field for field, twice.
    part.requireMesh();
    CHECK(part.currentSource() == part.currentSource());
}
