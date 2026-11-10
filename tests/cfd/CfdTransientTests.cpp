// P19-TRANSIENT-001 -- the transient incompressible march, end to end from a
// Document, plus the time grid as a value type.
//
// WHAT THIS SUITE IS ABOUT, AND WHAT IT IS DELIBERATELY NOT ABOUT. Every
// numerical step inside a step's inner loop belongs to an operator qualified
// under an earlier milestone, the transient momentum term included: the term is
// `CfdMomentumTests.cpp`'s, the predictor and the corrections are
// `CfdCouplingTests.cpp`'s, the residuals and the mass balance are
// `CfdContinuityTests.cpp`'s, the kernels are `CfdLinearSolveTests.cpp`'s.
// Re-asserting their arithmetic here would not be independent evidence of
// anything.
//
// What IS here is what only this milestone can get wrong:
//
// ```text
// the grid          does it land on the end time exactly, and are the times
//                   formed so they do not drift with the step count
// the march         does every step get its own dt, its own baseline and its
//                   own budget
// U^n               is the transient source carrying the PREVIOUS field and
//                   not the current one -- the defect ADR-073 Decision 4
//                   warns about, which converges to the correct STEADY answer
//                   while reporting a history
// the sampling      does the policy decide what is kept, and is t = 0 kept out
// the publication   is it atomic over the WHOLE march, and does a failed march
//                   keep what came before
// ```
//
// THE INITIAL CONDITION IS PLUG FLOW, AND THAT IS A CORRECTNESS REQUIREMENT OF
// THE SUITE RATHER THAN A CONVENIENCE. `CfdInitialCondition` is uniform, and a
// uniform velocity equal to the inlet speed is DIVERGENCE-COMPATIBLE with the
// boundary: a uniform field has zero flux through every closed cell, and the
// start cap's prescribed inflow is exactly what that field carries. A ZERO
// initial velocity is not compatible -- the inlet pushes mass into a field that
// carries none -- so t = 0+ would be an impulsive start, where incompressibility
// forces the velocity to jump by O(1) and the pressure to scale as 1/dt. The
// first-step change would then NOT be O(dt), and the refinement assertion below
// would be measuring the wrong thing. Starting from plug flow makes the march
// the classical start-up problem (a flat profile relaxing to a developed one
// over the viscous time), which is smooth in time and is what backward Euler's
// order can be read off.
//
// THE TIMESCALES, COMPUTED RATHER THAN GUESSED, because every `dt` below is
// chosen against them:
//
// ```text
// viscous    rho h^2 / mu  = 998 * (0.01)^2 / 1.002e-3  ~  100 s
// convective L / u         = 0.040 / 1.0e-5             ~  4000 s
// ```
//
// so `dt = 0.1 s` is strongly transient (the term dominates the matrix) and
// `dt = 1e7 s` is effectively steady (it vanishes from it). Each march below
// asserts its own regime against `viscousTimescaleSeconds()` with a `REQUIRE`
// rather than stating it in a comment. An earlier draft of this paragraph
// credited a helper called `transientRegime()`; no such function exists, and an
// adversarial review found that the name was fiction while the claim was true.
//
// THE PECLET CONFINEMENT IS INHERITED AND RE-ASSERTED. `P19-PVCOUPLE-001` is
// qualified only at low cell Peclet number, so the inlet speed is the steady
// suite's and the bound is checked here too.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "reference/MeshTestSupport.hpp"

#include <bettercad/cfd/CfdAnalysis.hpp>
#include <bettercad/cfd/CfdAnalysisObject.hpp>
#include <bettercad/cfd/CfdBoundary.hpp>
#include <bettercad/cfd/CfdConvection.hpp>
#include <bettercad/cfd/CfdCoupling.hpp>
#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdFields.hpp>
#include <bettercad/cfd/CfdGeometry.hpp>
#include <bettercad/cfd/CfdGradient.hpp>
#include <bettercad/cfd/CfdLinearSolve.hpp>
#include <bettercad/cfd/CfdMesh.hpp>
#include <bettercad/cfd/CfdMomentum.hpp>
#include <bettercad/cfd/CfdResult.hpp>
#include <bettercad/cfd/CfdSteady.hpp>
#include <bettercad/cfd/CfdTopology.hpp>
#include <bettercad/cfd/CfdTransient.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/materials/ThermalProperties.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using bettercad::cfd::CellIndex;
using bettercad::cfd::CfdAnalysis;
using bettercad::cfd::CfdAnalysisDefinition;
using bettercad::CfdAnalysisId;
using bettercad::cfd::CfdAnalysisMode;
using bettercad::cfd::CfdBoundaryCondition;
using bettercad::CfdBoundaryConditionId;
using bettercad::cfd::CfdFailureStage;
using bettercad::cfd::CfdFieldMap;
using bettercad::cfd::CfdLinearSolve;
using bettercad::cfd::CfdLinearSolveResult;
using bettercad::cfd::CfdResult;
using bettercad::cfd::CfdResultFields;
using bettercad::cfd::CfdResultSlot;
using bettercad::cfd::CfdSolveFailure;
using bettercad::cfd::CfdSteadyReport;
using bettercad::cfd::CfdTimeGrid;
using bettercad::cfd::CfdTimeSampling;
using bettercad::cfd::CfdTransientReport;
using bettercad::cfd::CfdTransientSettings;
using bettercad::cfd::CfdVariable;
using bettercad::cfd::NoSlipWall;
using bettercad::cfd::PressureOutlet;
using bettercad::cfd::VelocityInlet;
using bettercad::cfd::Velocity3;
using Catch::Matchers::ContainsSubstring;

namespace {

constexpr Density kDensity = 998_kg_per_m3;
constexpr DynamicViscosity kViscosity = DynamicViscosity::fromSi(1.002e-3);

/// The steady suite's inlet speed, unchanged so the two suites' figures are
/// comparable and so the cell Peclet bound is the same one.
constexpr Velocity kInletSpeed = Velocity::fromSi(1.0e-5);
constexpr double kDomainLengthM = 0.040;
/// The half-height of the 20 mm channel, which sets the viscous timescale.
constexpr double kHalfHeightM = 0.010;

/// `rho h^2 / mu`, the time a velocity perturbation needs to diffuse across the
/// half-channel. Every `dt` in this file is chosen relative to it.
[[nodiscard]] double viscousTimescaleSeconds() {
    return kDensity.si() * kHalfHeightM * kHalfHeightM / kViscosity.si();
}

/// The cell Peclet number, bounded conservatively by the domain's longest side.
[[nodiscard]] double conservativePeclet() {
    return kDensity.si() * kInletSpeed.si() * kDomainLengthM / kViscosity.si();
}

void assignWaterTo(Document& document, MaterialId& id) {
    features::MaterialDefinition water;
    water.designation = "Water";
    water.mechanical.density = materials::MaterialProperty<Density>::known(kDensity);
    water.thermal.dynamicViscosity = materials::MaterialProperty<DynamicViscosity>::known(kViscosity);
    const Result<MaterialId> created = features::createMaterial(document, "CfdWater", water);
    REQUIRE(created.has_value());
    REQUIRE(features::assignMaterial(document, *created).has_value());
    id = *created;
}

/// The steady suite's 40 x 30 x 20 mm channel, built the same way so the two
/// suites measure the same body.
Document channelDocument() {
    Document document{"CfdTransientChannel"};
    auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
    (void)addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
    const ObjectId profile = require(document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Fluid", {.profile = SketchId::fromValue(profile.value()), .depth = 20_mm});
    REQUIRE(extrude.has_value());
    const ObjectId solid = require(document.addObject(std::move(*extrude)));
    meshing::MeshControlDefinition control;
    control.body = solid;
    auto intent = meshing::MeshControl::create("Mesh", control);
    REQUIRE(intent.has_value());
    (void)require(document.addObject(std::move(*intent)));
    return document;
}

struct ChannelFaces {
    ObjectId feature{};
    std::array<EntityId, 4> lines{};

    [[nodiscard]] FaceName startCap() const { return FaceName{feature, FaceSelector{.role = FaceRole::StartCap}}; }
    [[nodiscard]] FaceName endCap() const { return FaceName{feature, FaceSelector{.role = FaceRole::EndCap}}; }
    [[nodiscard]] FaceName side(std::size_t which) const {
        return FaceName{feature, FaceSelector{.role = FaceRole::Side, .entity = lines.at(which)}};
    }
};

/// How the channel is driven: the transient settings, or none for the steady
/// comparison runs.
struct Drive {
    CfdAnalysisMode mode = CfdAnalysisMode::Transient;
    Time timeStep = Time::fromSi(0.1);
    Time endTime = Time::fromSi(0.2);
    /// 200 AND NOT 60, AND THE MARGIN IS THE POINT. The first step of a march
    /// from plug flow measured SIXTY outer iterations on debug-ext, which is
    /// exactly what a budget of 60 permits: the suite would have been one
    /// iteration from a false failure on any preset that rounds differently.
    /// Step 1 is expensive for a physical reason -- p^0 is zero everywhere
    /// while the developed field needs a pressure gradient, and the pressure is
    /// a constraint rather than an evolving field, so the whole pressure
    /// solution has to be built inside step 1. Step 2 inherits it as a guess
    /// and took EIGHT.
    std::uint32_t maxIterationsPerStep = 200;
    CfdTimeSampling sampling = CfdTimeSampling::FinalOnly;
    std::uint32_t maxSteadyIterations = 400;
    /// Plug flow unless a test wants the incompatible zero state.
    bool plugFlowInitialCondition = true;
    double divergenceFactor = 100.0;
};

/// One meshed channel with a stored CFD analysis, driven exactly as production
/// drives it: the solve reads the DOCUMENT, so the definition has to be in it.
struct Channel {
    meshref::MeshedReference model;
    MaterialId material{};
    CfdAnalysisId analysis{};
    ChannelFaces faces{};

    Channel() : model(channelDocument()) {
        assignWaterTo(model.document(), material);
        (void)model.regenerate();
        (void)model.require();
        faces = readFaces();
        store(definitionFor(Drive{}));
    }

    [[nodiscard]] ChannelFaces readFaces() const {
        ChannelFaces found;
        found.feature = model.body();
        std::size_t next = 0;
        for (const meshing::MappedFace& face : model.map().faces()) {
            for (const FaceName& name : face.names) {
                if (name.face.role == FaceRole::Side && name.face.entity.has_value() && next < 4) {
                    const EntityId entity = *name.face.entity;
                    if (std::ranges::find(std::span{found.lines}.first(next), entity) ==
                        std::span{found.lines}.first(next).end()) {
                        found.lines[next++] = entity;
                    }
                }
            }
        }
        REQUIRE(next == 4);
        return found;
    }

    /// Water in through the start cap, out through the end cap at 0 Pa -- which
    /// is also the pressure datum -- and four no-slip walls.
    ///
    /// THE TOLERANCES ARE LOOSENED FROM ADR-058'S DEFAULTS, DELIBERATELY AND
    /// THROUGH THE DEFINITION, exactly as the steady suite loosens them and for
    /// the same reason: 1e-6 per step on a Debug build would make a multi-step
    /// march the slowest thing in the tree, and 1e-4 still demands a genuinely
    /// coupled solution at every step. Loosening them through the definition is
    /// also the only way to loosen them, which is the point of ADR-077
    /// Decision 7.
    [[nodiscard]] CfdAnalysisDefinition definitionFor(const Drive& drive) const {
        CfdAnalysisDefinition definition{.mesh = model.control()};
        definition.mode = drive.mode;
        definition.boundaryConditions = {
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), VelocityInlet{faces.startCap(), kInletSpeed}},
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(2), PressureOutlet{faces.endCap(), 0_Pa}},
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(3), NoSlipWall{faces.side(0)}},
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(4), NoSlipWall{faces.side(1)}},
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(5), NoSlipWall{faces.side(2)}},
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(6), NoSlipWall{faces.side(3)}},
        };
        if (drive.plugFlowInitialCondition) {
            // THE DIVERGENCE-COMPATIBLE START. The extrusion is along +z, so
            // the start cap's inward normal is +z and a uniform +z field of the
            // inlet speed carries exactly the prescribed inflow.
            definition.initialCondition.velocity = Velocity3{.x = Velocity{}, .y = Velocity{}, .z = kInletSpeed};
        }
        definition.convergence.momentumResidualTolerance = 1.0e-4;
        definition.convergence.continuityResidualTolerance = 1.0e-4;
        definition.convergence.divergenceFactor = drive.divergenceFactor;
        if (drive.mode == CfdAnalysisMode::Transient) {
            definition.steady.reset();
            definition.transient = CfdTransientSettings{.timeStep = drive.timeStep,
                                                        .endTime = drive.endTime,
                                                        .maxIterationsPerStep = drive.maxIterationsPerStep,
                                                        .sampling = drive.sampling};
        } else {
            definition.transient.reset();
            definition.steady = cfd::CfdSteadySettings{.maxIterations = drive.maxSteadyIterations};
        }
        return definition;
    }

    /// Replaces the stored definition, so the revision and the fingerprint move
    /// exactly as a command would move them.
    void store(const CfdAnalysisDefinition& definition) {
        if (analysis.isValid()) {
            const Result<bool> changed = model.document().modifyObject<CfdAnalysis>(
                ObjectId::fromValue(analysis.value()),
                [&definition](CfdAnalysis& object) -> Result<bool> { return object.setDefinition(definition); });
            INFO((changed.has_value() ? std::string{} : changed.error().message));
            REQUIRE(changed.has_value());
            return;
        }
        auto study = CfdAnalysis::create("Flow", definition);
        INFO((study.has_value() ? std::string{} : study.error().message));
        REQUIRE(study.has_value());
        analysis = CfdAnalysisId::fromValue(require(model.document().addObject(std::move(*study))).value());
        REQUIRE(analysis.isValid());
    }

    void drive(const Drive& d) { store(definitionFor(d)); }

    [[nodiscard]] std::expected<CfdTransientReport, CfdSolveFailure> march(CfdResultSlot& slot,
                                                                          const CfdLinearSolve& port) {
        return cfd::solveCfdTransient(model.document(), model.regenerator(), model.mesher(), analysis, port, slot);
    }

    [[nodiscard]] std::expected<CfdTransientReport, CfdSolveFailure> march(CfdResultSlot& slot) {
        return march(slot, cfd::cfdLinearSolve());
    }

    [[nodiscard]] std::expected<CfdSteadyReport, CfdSolveFailure> solveSteady(CfdResultSlot& slot) {
        return cfd::solveCfdSteady(model.document(), model.regenerator(), model.mesher(), analysis,
                                   cfd::cfdLinearSolve(), slot);
    }

    /// The finite-volume geometry, for the test that needs cell positions.
    [[nodiscard]] cfd::CfdGeometry geometry() {
        const Result<cfd::CfdModel> input =
            cfd::requireCfdModel(model.document(), model.regenerator(), model.mesher(), model.control());
        REQUIRE(input.has_value());
        const Result<cfd::CfdMeshContract> contract = cfd::requireCfdMesh(*input);
        REQUIRE(contract.has_value());
        const Result<cfd::CfdTopology> topology = cfd::requireCfdTopology(*input, *contract);
        REQUIRE(topology.has_value());
        Result<cfd::CfdGeometry> built = cfd::requireCfdGeometry(*input, *topology);
        INFO((built.has_value() ? std::string{} : built.error().message));
        REQUIRE(built.has_value());
        return std::move(*built);
    }

    [[nodiscard]] CfdFieldMap layout() {
        const Result<cfd::CfdModel> input =
            cfd::requireCfdModel(model.document(), model.regenerator(), model.mesher(), model.control());
        INFO((input.has_value() ? std::string{} : input.error().message));
        REQUIRE(input.has_value());
        const Result<cfd::CfdMeshContract> contract = cfd::requireCfdMesh(*input);
        REQUIRE(contract.has_value());
        const Result<cfd::CfdTopology> topology = cfd::requireCfdTopology(*input, *contract);
        REQUIRE(topology.has_value());
        Result<CfdFieldMap> map = cfd::requireCfdFieldMap(*input, *topology);
        INFO((map.has_value() ? std::string{} : map.error().message));
        REQUIRE(map.has_value());
        return std::move(*map);
    }
};

/// A solve port that delegates to production for the first @p allowed calls
/// and then refuses, so a march can be made to fail at a CHOSEN step.
///
/// WRITTEN BECAUSE EVERY FAILURE TEST IN THIS FILE FAILED AT STEP 1, which a
/// mutation analysis pointed out leaves `ADR-078`'s headline atomicity promise
/// -- a march that fails part way publishes NOTHING -- tested only where no
/// sample had been produced yet.
[[nodiscard]] CfdLinearSolve refusingAfter(std::size_t allowed, const std::shared_ptr<std::size_t>& calls) {
    const CfdLinearSolve production = cfd::cfdLinearSolve();
    return [production, allowed, calls](const cfd::CfdSparseMatrix& matrix, std::span<const double> rhs,
                                        const cfd::CfdLinearSolverSettings& settings)
               -> std::expected<CfdLinearSolveResult, Error> {
        ++(*calls);
        if (*calls > allowed) {
            return std::unexpected(Error{ErrorCode::Internal, "the test port refused this solve"});
        }
        return production(matrix, rhs, settings);
    };
}

/// A port that returns production's solution multiplied by @p factor after
/// @p allowed calls, while reporting the residual production measured.
///
/// THIS IS A LYING PORT, AND `ADR-075` L4 IS WHY IT CAN EXIST: the coupling
/// trusts the residual a port reports and "no check here can detect that". So a
/// divergence can be FORCED rather than hoped for, which is what turns the
/// Diverged stage from one this fixture MIGHT reach into one it does.
[[nodiscard]] CfdLinearSolve inflatingAfter(std::size_t allowed, double factor,
                                            const std::shared_ptr<std::size_t>& calls) {
    const CfdLinearSolve production = cfd::cfdLinearSolve();
    return [production, allowed, factor, calls](const cfd::CfdSparseMatrix& matrix, std::span<const double> rhs,
                                                const cfd::CfdLinearSolverSettings& settings)
               -> std::expected<CfdLinearSolveResult, Error> {
        ++(*calls);
        std::expected<CfdLinearSolveResult, Error> solved = production(matrix, rhs, settings);
        if (!solved.has_value() || *calls <= allowed) {
            return solved;
        }
        for (double& value : solved->solution) {
            value *= factor;
        }
        return solved;
    };
}

/// The largest absolute difference between two equal-length arrays.
[[nodiscard]] double maxAbsDiff(std::span<const double> a, std::span<const double> b) {
    REQUIRE(a.size() == b.size());
    double worst = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        worst = std::max(worst, std::abs(a[i] - b[i]));
    }
    return worst;
}

/// The largest absolute value in an array.
[[nodiscard]] double maxAbs(std::span<const double> a) {
    double worst = 0.0;
    for (const double value : a) {
        worst = std::max(worst, std::abs(value));
    }
    return worst;
}

} // namespace

// ---------------------------------------------------------------------------
// The time grid, as a value type: no mesh, no solve, no document
// ---------------------------------------------------------------------------

TEST_CASE("CfdTimeGrid_RefusesAStepThatIsNotFiniteAndPositive", "[cfd][transient]") {
    for (const double bad : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity()}) {
        const Result<CfdTimeGrid> grid = CfdTimeGrid::create(Time::fromSi(bad), Time::fromSi(1.0));
        INFO("step " << bad);
        REQUIRE_FALSE(grid.has_value());
        CHECK(grid.error().code == ErrorCode::InvalidArgument);
        CHECK_THAT(grid.error().message, ContainsSubstring("time step must be finite and positive"));
    }
}

TEST_CASE("CfdTimeGrid_RefusesAnEndTimeThatIsNotFiniteAndPositive", "[cfd][transient]") {
    // A ZERO END TIME IS REFUSED HERE AND ACCEPTED BY P18's GRID, and the
    // difference is not an oversight: a grid of no steps has nothing a CFD run
    // could publish, because `CfdResult::create` requires a transient sample to
    // carry a time above zero and a step of at least one.
    for (const double bad : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity()}) {
        const Result<CfdTimeGrid> grid = CfdTimeGrid::create(Time::fromSi(0.1), Time::fromSi(bad));
        INFO("end time " << bad);
        REQUIRE_FALSE(grid.has_value());
        CHECK(grid.error().code == ErrorCode::InvalidArgument);
        // THE SPECIFIC GATE AND NOT JUST THE WORDS "end time". A mutation
        // analysis found that dropping `!isFinite(endTime)` still produced a
        // refusal, from the step-count guard, whose message happens to contain
        // "an end time of nan s" -- so the looser assertion passed on a
        // different gate entirely.
        CHECK_THAT(grid.error().message, ContainsSubstring("end time must be finite and positive"));
    }
}

TEST_CASE("CfdTimeGrid_RefusesMoreStepsThanTheGuardAllowsAndNeverEnlargesTheStep", "[cfd][transient]") {
    // REACHABLE THROUGH A PERFECTLY VALID DEFINITION. Both times are finite and
    // positive and the end time is longer than one step, so
    // `validate(CfdTransientSettings)` accepts this pair; the step count is
    // what cannot be represented.
    const Result<CfdTimeGrid> grid = CfdTimeGrid::create(Time::fromSi(1.0e-300), Time::fromSi(1.0e300));
    REQUIRE_FALSE(grid.has_value());
    CHECK(grid.error().code == ErrorCode::InvalidArgument);
    CHECK_THAT(grid.error().message, ContainsSubstring("more than the limit"));
    // THE PROMISE THAT MATTERS is in the message: the step is not enlarged to
    // fit, because that would be solving a different problem.
    CHECK_THAT(grid.error().message, ContainsSubstring("not enlarged"));

    SECTION("exactly the limit is accepted") {
        const Result<CfdTimeGrid> edge =
            CfdTimeGrid::create(Time::fromSi(1.0), Time::fromSi(static_cast<double>(cfd::kMaxCfdTimeSteps)));
        REQUIRE(edge.has_value());
        CHECK(edge->stepCount() == cfd::kMaxCfdTimeSteps);
        // O(1) IN MEMORY: a million-step grid costs nothing until a step is
        // taken, which is why this assertion is affordable at all.
        CHECK(edge->timeAt(cfd::kMaxCfdTimeSteps).si() == static_cast<double>(cfd::kMaxCfdTimeSteps));
    }
}

TEST_CASE("CfdTimeGrid_DividesAWholeNumberOfStepsWithNoRemainder", "[cfd][transient]") {
    const Result<CfdTimeGrid> grid = CfdTimeGrid::create(Time::fromSi(0.25), Time::fromSi(1.0));
    REQUIRE(grid.has_value());
    CHECK(grid->stepCount() == 4);
    CHECK_FALSE(grid->hasPartialFinalStep());
    CHECK(grid->nominalStep().si() == 0.25);
    for (std::size_t k = 0; k < 4; ++k) {
        CHECK(grid->stepAt(k).si() == 0.25);
    }
    CHECK(grid->timeAt(0).si() == 0.0);
    CHECK(grid->timeAt(4).si() == 1.0);
    CHECK(grid->endTime().si() == 1.0);
    CHECK(grid->integratedSpan().si() == 1.0);
    REQUIRE(grid->times().size() == 5);
    // THE ELEMENTS AND NOT JUST THE SIZE. `times()` could have been written as
    // `out[i] = timeAt(i + 1)` and only its length was ever asserted.
    CHECK(grid->times()[0].si() == 0.0);
    CHECK(grid->times()[2].si() == 0.5);
    CHECK(grid->times()[4].si() == 1.0);
    // AND THE DOCUMENTED OUT-OF-RANGE DIRECTIONS, which nothing asserted:
    // a step past the last is zero, and a time past the last is the end time.
    CHECK(grid->stepAt(4).si() == 0.0);
    CHECK(grid->stepAt(99).si() == 0.0);
    CHECK(grid->timeAt(99).si() == 1.0);
}

TEST_CASE("CfdTimeGrid_ShortensTheFinalStepSoTheMarchLandsOnTheEndTimeExactly", "[cfd][transient]") {
    // 1.0 / 0.3 is 3.33...: three full steps and a remainder of 0.1.
    const Result<CfdTimeGrid> grid = CfdTimeGrid::create(Time::fromSi(0.3), Time::fromSi(1.0));
    REQUIRE(grid.has_value());
    CHECK(grid->stepCount() == 4);
    CHECK(grid->hasPartialFinalStep());
    CHECK(grid->stepAt(0).si() == 0.3);
    CHECK(grid->stepAt(1).si() == 0.3);
    CHECK(grid->stepAt(2).si() == 0.3);
    // The remainder, to rounding. It is NOT 0.3.
    CHECK(grid->stepAt(3).si() < 0.3);
    CHECK(std::abs(grid->stepAt(3).si() - 0.1) < 1.0e-15);
    // THE WHOLE POINT: the last time is the end time itself and not 4 * 0.3.
    CHECK(grid->timeAt(4).si() == 1.0);
    CHECK(grid->timeAt(4).si() != 4.0 * 0.3);
    // The steps add up to the end time to rounding, which is what a consumer
    // accumulating anything over the run must read.
    CHECK(std::abs(grid->integratedSpan().si() - 1.0) < 1.0e-15);
}

TEST_CASE("CfdTimeGrid_AbsorbsTheRoundingOfAQuotientThatIsAWholeNumber", "[cfd][transient]") {
    // `0.3 / 0.1` is 2.9999999999999996 in double precision. Without
    // kCfdTimeGridTolerance this would be two full steps and a third of
    // 5.5e-17 s -- a step so small the transient coefficient rho V / dt would
    // overflow the matrix.
    CHECK(0.3 / 0.1 != 3.0);
    const Result<CfdTimeGrid> grid = CfdTimeGrid::create(Time::fromSi(0.1), Time::fromSi(0.3));
    REQUIRE(grid.has_value());
    CHECK(grid->stepCount() == 3);
    CHECK_FALSE(grid->hasPartialFinalStep());
    CHECK(grid->stepAt(2).si() == 0.1);
    CHECK(grid->timeAt(3).si() == 0.3);

    SECTION("and this is the one grid where integratedSpan is NOT the end time") {
        // THE ONLY CASE THAT DISTINGUISHES THEM, which is why it is asserted
        // here and nowhere else: `endTime / dt` was a whole number only to
        // within the tolerance, so the steps add up to 3 * 0.1 =
        // 0.30000000000000004 while the end time is 0.3 exactly. A mutation
        // analysis found that `integratedSpan()` could have been written as
        // `return end_;` and every other assertion in the file would still
        // pass. A consumer accumulating anything over the run reads THIS.
        CHECK(grid->integratedSpan().si() == 3.0 * 0.1);
        CHECK(grid->integratedSpan().si() != grid->endTime().si());
        CHECK(std::abs(grid->integratedSpan().si() - grid->endTime().si()) <=
              cfd::kCfdTimeGridTolerance * grid->endTime().si());
    }
}

TEST_CASE("CfdTimeGrid_TheWholeNumberToleranceIsRELATIVEToTheStepCount", "[cfd][transient]") {
    // A MUTATION ANALYSIS FOUND THIS BAND UNTESTED. Nothing in the suite
    // exercised a quotient whose distance from a whole number sits BETWEEN the
    // absolute tolerance and the relative one, which is exactly where the two
    // forms differ -- and it is the band that matters, because it only opens up
    // at large step counts.
    //
    //   endTime / dt   = 100000.00005
    //   |ratio - round| = 5e-5          >  kCfdTimeGridTolerance      (1e-9)
    //                                   <= kCfdTimeGridTolerance * N  (1e-4)
    //
    // So the relative form accepts this as a whole number of steps and the
    // absolute form would make a 100001st step of 5e-5 * dt -- a step 20,000
    // times shorter than the nominal one, whose rho V / dt would be 20,000
    // times larger. That is the defect the relative scaling exists to prevent.
    const double dt = 1.0e-3;
    const double endTime = 100000.00005 * dt;
    const double ratio = endTime / dt;
    const double nearest = std::round(ratio);
    REQUIRE(std::abs(ratio - nearest) > cfd::kCfdTimeGridTolerance);
    REQUIRE(std::abs(ratio - nearest) <= cfd::kCfdTimeGridTolerance * nearest);

    const Result<CfdTimeGrid> grid = CfdTimeGrid::create(Time::fromSi(dt), Time::fromSi(endTime));
    REQUIRE(grid.has_value());
    CHECK(grid->stepCount() == 100000);
    CHECK_FALSE(grid->hasPartialFinalStep());
    CHECK(grid->stepAt(99999).si() == dt);
}

TEST_CASE("CfdTimeGrid_FormsTimesByMultiplicationSoTheyDoNotDriftWithTheStepCount", "[cfd][transient]") {
    // A RUNNING SUM WOULD DRIFT, and the drift grows with the step count: this
    // is the determinism the checklist's "Deterministic timestepping" asks of
    // the grid, stated as an identity rather than as a repeat-run comparison.
    const Result<CfdTimeGrid> grid = CfdTimeGrid::create(Time::fromSi(0.1), Time::fromSi(1000.0));
    REQUIRE(grid.has_value());
    REQUIRE(grid->stepCount() == 10000);

    double running = 0.0;
    std::size_t drifted = 0;
    std::size_t notAMultiple = 0;
    for (std::size_t k = 1; k < grid->stepCount(); ++k) {
        running += 0.1;
        if (grid->timeAt(k).si() != static_cast<double>(k) * 0.1) {
            ++notAMultiple;
        }
        if (grid->timeAt(k).si() != running) {
            ++drifted;
        }
    }
    // COUNTED AND THEN ASSERTED ONCE. A REQUIRE inside the loop would spend
    // twenty thousand assertions and ten seconds of every preset on one
    // property, and the counters are the measurement either way.
    CHECK(notAMultiple == 0);
    // THE MEASUREMENT, NOT THE CLAIM: a running sum really does disagree, so
    // the choice of a multiplication is load-bearing rather than stylistic.
    INFO("times where a running sum disagrees: " << drifted);
    CHECK(drifted > 0);

    SECTION("the times are strictly increasing and the last is exact") {
        std::size_t notIncreasing = 0;
        for (std::size_t k = 1; k <= grid->stepCount(); ++k) {
            if (!(grid->timeAt(k).si() > grid->timeAt(k - 1).si())) {
                ++notIncreasing;
            }
        }
        CHECK(notIncreasing == 0);
        CHECK(grid->timeAt(grid->stepCount()).si() == 1000.0);
    }
}

TEST_CASE("CfdTimeGrid_TreatsAStepLongerThanTheEndTimeAsOneShortenedStep", "[cfd][transient]") {
    // TOTAL OVER FINITE POSITIVE INPUTS, DELIBERATELY. "You must be able to
    // take at least one full step" is an analysis policy that
    // `validate(CfdTransientSettings)` already holds, so the grid does not
    // repeat it -- a second spelling of one rule is two rules that can drift.
    // The orchestrator never sees this case, and the test below proves the
    // definition refuses it.
    const Result<CfdTimeGrid> grid = CfdTimeGrid::create(Time::fromSi(10.0), Time::fromSi(1.0));
    REQUIRE(grid.has_value());
    CHECK(grid->stepCount() == 1);
    CHECK(grid->hasPartialFinalStep());
    CHECK(grid->stepAt(0).si() == 1.0);
    CHECK(grid->timeAt(1).si() == 1.0);
    CHECK(grid->integratedSpan().si() == 1.0);
}

TEST_CASE("CfdTimeGrid_AnalysisSettingsRefuseWhatTheGridWouldAccept", "[cfd][transient]") {
    // The other half of the split above, tested where it lives.
    const Result<void> ordered =
        cfd::validate(CfdTransientSettings{.timeStep = Time::fromSi(10.0), .endTime = Time::fromSi(1.0)});
    REQUIRE_FALSE(ordered.has_value());
    CHECK_THAT(ordered.error().message, ContainsSubstring("shorter than the time step"));

    const Result<void> step =
        cfd::validate(CfdTransientSettings{.timeStep = Time::fromSi(0.0), .endTime = Time::fromSi(1.0)});
    REQUIRE_FALSE(step.has_value());
    CHECK_THAT(step.error().message, ContainsSubstring("time step"));

    const Result<void> good =
        cfd::validate(CfdTransientSettings{.timeStep = Time::fromSi(0.1), .endTime = Time::fromSi(1.0)});
    CHECK(good.has_value());
}

// ---------------------------------------------------------------------------
// The march
// ---------------------------------------------------------------------------

TEST_CASE("CfdTransient_MarchesTheChannelAndPublishesTheFinalSample", "[cfd][transient]") {
    REQUIRE(conservativePeclet() < 1.0);

    Channel channel;
    // dt = 0.1 s against a viscous time of ~100 s: strongly transient.
    const Drive drive{.timeStep = Time::fromSi(0.1), .endTime = Time::fromSi(0.2)};
    REQUIRE(drive.timeStep.si() < 0.01 * viscousTimescaleSeconds());
    channel.drive(drive);

    CfdResultSlot slot;
    const std::expected<CfdTransientReport, CfdSolveFailure> report = channel.march(slot);
    INFO((report.has_value() ? std::string{} : report.error().error.message));
    REQUIRE(report.has_value());

    SECTION("it marched every step of the grid, each with at least one iteration") {
        REQUIRE(report->grid.stepCount() == 2);
        REQUIRE(report->steps.size() == 2);
        CHECK(report->steps[0].index == 1);
        CHECK(report->steps[1].index == 2);
        for (const cfd::CfdTransientStepRecord& step : report->steps) {
            CHECK(step.outerIterations >= 1);
            CHECK(step.outerIterations <= drive.maxIterationsPerStep);
        }
        // MORE THAN ONE MATTERS, as the steady suite says of its own loop: a
        // step that published after a single pass would satisfy every other
        // assertion here while never having coupled anything. Measured 60 on
        // step 1 and 8 on step 2 (debug-ext), for the reason `Drive` records.
        CHECK(report->steps[0].outerIterations > 1);
        CHECK(report->steps[0].outerIterations > report->steps[1].outerIterations);
        CHECK(report->totalOuterIterations ==
              static_cast<std::uint64_t>(report->steps[0].outerIterations) + report->steps[1].outerIterations);
    }
    SECTION("every step carries its own time and its own size") {
        CHECK(report->steps[0].time.si() == 0.1);
        CHECK(report->steps[1].time.si() == 0.2);
        CHECK(report->steps[0].stepSize.si() == 0.1);
        CHECK(report->steps[1].stepSize.si() == 0.1);
        CHECK_FALSE(report->grid.hasPartialFinalStep());
    }
    SECTION("FinalOnly keeps one sample, at the end time") {
        REQUIRE(report->samples.size() == 1);
        REQUIRE(report->sampleTimes.size() == 1);
        CHECK(report->sampleTimes[0].si() == 0.2);
        CHECK(report->sampling == CfdTimeSampling::FinalOnly);
    }
    SECTION("the sample is a transient result: it carries a time and a step") {
        REQUIRE(report->samples.size() == 1);
        const cfd::CfdResultMetadata& metadata = report->samples[0].metadata();
        REQUIRE(metadata.time.has_value());
        CHECK(metadata.time->si() == 0.2);
        REQUIRE(metadata.step.has_value());
        CHECK(*metadata.step == 2);
        // t = 0 IS NOT A SAMPLE, and the container is what refuses it: a
        // transient result needs a time above zero and a step of at least one.
        CHECK(metadata.step.value() >= 1);
    }
    SECTION("the slot holds the final sample and nothing else") {
        REQUIRE(slot.result() != nullptr);
        CHECK(slot.failure() == nullptr);
        REQUIRE_FALSE(report->samples.empty());
        const CfdResult& last = report->samples.back();
        // ADR-078 Decision 2: the duplication is checkable, which is why it is
        // preferred to a reference that can go stale.
        CHECK(slot.result()->source() == last.source());
        CHECK(slot.result()->metadata() == last.metadata());
        CHECK(slot.result()->fields() == last.fields());
        CHECK(cfd::analysisState(channel.model.document(), channel.model.regenerator(), channel.model.mesher(),
                                 channel.analysis, slot.result(), slot.failure()) ==
              cfd::AnalysisState::SolvedCurrent);
    }
    SECTION("the published fields have the layout's three lengths and are finite") {
        const CfdFieldMap map = channel.layout();
        REQUIRE(slot.result() != nullptr);
        const CfdResultFields& fields = slot.result()->fields();
        CHECK(fields.pressure().size() == map.size(CfdVariable::Pressure));
        CHECK(fields.velocity().size() == map.size(CfdVariable::Velocity));
        CHECK(fields.massFlux().size() == map.size(CfdVariable::MassFlux));
        for (const std::span<const double> array : {fields.pressure(), fields.velocity(), fields.massFlux()}) {
            for (const double value : array) {
                REQUIRE(std::isfinite(value));
            }
        }
        // A MARCH THAT PUBLISHED ZEROS would pass every length and finiteness
        // check above. Water is moving.
        CHECK(maxAbs(fields.massFlux()) > 0.0);
    }
    SECTION("the t = 0 state is recorded as a plain array and it is plug flow") {
        const CfdFieldMap map = channel.layout();
        REQUIRE(report->initialVelocity.size() == map.size(CfdVariable::Velocity));
        REQUIRE(report->initialPressure.size() == map.size(CfdVariable::Pressure));
        const std::size_t cells = map.cells();
        // Component-blocked: z is the third block, and it is the inlet speed.
        for (std::size_t c = 0; c < cells; ++c) {
            REQUIRE(report->initialVelocity[2 * cells + c] == kInletSpeed.si());
            REQUIRE(report->initialVelocity[c] == 0.0);
        }
        // AND THE PRESSURE, WHICH ONLY HAD ITS SIZE ASSERTED until a mutation
        // analysis pointed that out. Publishing the FINAL pressure field here
        // instead of the t = 0 one would have passed every assertion above. The
        // initial condition sets no pressure and the march produces one, so the
        // two arrays are trivial to tell apart -- and now something does.
        CHECK(maxAbs(report->initialPressure) == 0.0);
        REQUIRE(slot.result() != nullptr);
        CHECK(maxAbs(slot.result()->fields().pressure()) > 0.0);
    }
}

TEST_CASE("CfdTransient_EveryStepAppliesTheTransientTerm", "[cfd][transient]") {
    Channel channel;
    channel.drive(Drive{.timeStep = Time::fromSi(0.1), .endTime = Time::fromSi(0.2)});
    CfdResultSlot slot;
    const std::expected<CfdTransientReport, CfdSolveFailure> report = channel.march(slot);
    INFO((report.has_value() ? std::string{} : report.error().error.message));
    REQUIRE(report.has_value());

    // RECORDED RATHER THAN GUARDED (ADR-077 Decision 4's rule): the
    // orchestrator always supplies a step, so a guard could not fire for any
    // input and this assertion is the only honest way to hold the claim.
    for (const cfd::CfdTransientStepRecord& step : report->steps) {
        INFO("step " << step.index);
        REQUIRE(step.transientTermApplied);
        // rho V / dt is positive and finite for every cell of a real mesh.
        REQUIRE(step.smallestTransientCoefficient.si() > 0.0);
        REQUIRE(std::isfinite(step.largestTransientCoefficient.si()));
        REQUIRE(step.largestTransientCoefficient.si() >= step.smallestTransientCoefficient.si());
    }

    SECTION("the coefficient scales as 1/dt, which is what makes it the transient term") {
        // HALVING dt DOUBLES rho V / dt. A term that had lost its dt -- the
        // defect ADR-073's unit assertions exist to prevent -- would not move.
        Channel other;
        other.drive(Drive{.timeStep = Time::fromSi(0.05), .endTime = Time::fromSi(0.05)});
        CfdResultSlot otherSlot;
        const std::expected<CfdTransientReport, CfdSolveFailure> halved = other.march(otherSlot);
        INFO((halved.has_value() ? std::string{} : halved.error().error.message));
        REQUIRE(halved.has_value());
        const double coarse = report->steps.front().largestTransientCoefficient.si();
        const double fine = halved->steps.front().largestTransientCoefficient.si();
        INFO("rho V / dt at dt = 0.1 s: " << coarse << ", at dt = 0.05 s: " << fine);
        // The two channels are meshed from the same document construction, so
        // the largest cell volume is the same; only dt differs.
        CHECK(std::abs(fine / coarse - 2.0) < 1.0e-9);
    }
}

TEST_CASE("CfdTransient_TheTransientCoefficientDoesNotDependOnTheComponent", "[cfd][transient]") {
    // The step record carries the X component's coefficients and the header
    // claims that is exact rather than a sample of three, because rho, V and dt
    // have no component. That is an assertion, not a sentence.
    Channel channel;
    channel.drive(Drive{.timeStep = Time::fromSi(0.1), .endTime = Time::fromSi(0.1)});

    const Result<cfd::CfdModel> model = cfd::requireCfdModel(channel.model.document(), channel.model.regenerator(),
                                                             channel.model.mesher(), channel.model.control());
    REQUIRE(model.has_value());
    const Result<cfd::CfdMeshContract> contract = cfd::requireCfdMesh(*model);
    REQUIRE(contract.has_value());
    const Result<cfd::CfdTopology> topology = cfd::requireCfdTopology(*model, *contract);
    REQUIRE(topology.has_value());
    const Result<cfd::CfdGeometry> geometry = cfd::requireCfdGeometry(*model, *topology);
    REQUIRE(geometry.has_value());
    const cfd::CfdAnalysis* object = cfd::findCfdAnalysis(channel.model.document(), channel.analysis);
    REQUIRE(object != nullptr);
    const Result<cfd::PreparedCfdBoundary> boundary =
        cfd::prepareCfdBoundary(*model, *topology, *geometry, object->definition());
    REQUIRE(boundary.has_value());

    const std::size_t cells = topology->cells();
    const std::vector<double> velocity(3 * cells, kInletSpeed.si());
    const std::vector<double> flux(topology->faces(), 0.0);

    std::optional<double> smallest;
    std::optional<double> largest;
    for (const cfd::VectorComponent component : cfd::kVectorComponents) {
        cfd::CfdMomentumInput input;
        input.density = kDensity;
        input.viscosity = kViscosity;
        input.component = component;
        input.faceFlux = flux;
        input.currentVelocity = velocity;
        input.previousVelocity = velocity;
        input.timeStep = Time::fromSi(0.1);
        const std::expected<cfd::CfdMomentumSystem, cfd::CfdMomentumFailure> system =
            cfd::assembleCfdMomentum(*topology, *geometry, *boundary, input);
        INFO((system.has_value() ? std::string{} : system.error().error.message));
        REQUIRE(system.has_value());
        REQUIRE(system->summary.transientTermApplied);
        if (!smallest.has_value()) {
            smallest = system->summary.smallestTransientCoefficient.si();
            largest = system->summary.largestTransientCoefficient.si();
            continue;
        }
        // BIT-EXACT, not close: the same rho, the same volumes, the same dt.
        REQUIRE(system->summary.smallestTransientCoefficient.si() == *smallest);
        REQUIRE(system->summary.largestTransientCoefficient.si() == *largest);
    }
    REQUIRE(smallest.has_value());
    CHECK(*smallest > 0.0);
}

TEST_CASE("CfdTransient_SamplesEveryStepWhenThePolicyAsksAndKeepsTZeroOut", "[cfd][transient]") {
    Channel channel;
    channel.drive(Drive{.timeStep = Time::fromSi(0.1),
                        .endTime = Time::fromSi(0.3),
                        .sampling = CfdTimeSampling::EveryStep});
    CfdResultSlot slot;
    const std::expected<CfdTransientReport, CfdSolveFailure> report = channel.march(slot);
    INFO((report.has_value() ? std::string{} : report.error().error.message));
    REQUIRE(report.has_value());

    REQUIRE(report->grid.stepCount() == 3);
    // EveryStep IS t_1 ... t_N: THREE samples, not four. P18's history keeps
    // t_0 as well; P19's cannot, because `CfdResult::create` refuses a sample
    // at t = 0 -- no system was solved to produce it (ADR-078 Decision 3).
    REQUIRE(report->samples.size() == 3);
    REQUIRE(report->sampleTimes.size() == 3);
    CHECK(report->sampleTimes[0].si() == 0.1);
    CHECK(report->sampleTimes[1].si() == 0.2);
    CHECK(report->sampleTimes[2].si() == 0.3);
    // THE POLICY ITSELF, which a report hardcoding FinalOnly survived: the only
    // assertion on this field was in the FinalOnly test.
    CHECK(report->sampling == CfdTimeSampling::EveryStep);

    SECTION("each step's record and its sample agree on what was measured") {
        // THIS IS THE ASSERTION THAT WOULD HAVE CAUGHT A MISSING FIELD, and it
        // exists because one was missing: `CfdTransientStepRecord::balance` was
        // never assigned, so every record carried two zeros while the sample
        // carried the evaluator's real measures. The suite passed, because the
        // only assertion on it was `isfinite` and zero is finite.
        //
        // A STRUCTURAL CROSS-CHECK RATHER THAN A BOUND. There is no tolerance
        // here to be satisfied by a default: the record and the sample are
        // filled from the same iteration, so they must be EQUAL, and any field
        // the march forgets to copy makes them differ.
        REQUIRE(report->samples.size() == report->steps.size());
        for (std::size_t i = 0; i < report->samples.size(); ++i) {
            INFO("sample " << i);
            const cfd::CfdResultMetadata& metadata = report->samples[i].metadata();
            REQUIRE(metadata.massBalance == report->steps[i].balance);
            REQUIRE(metadata.residuals == report->steps[i].residuals);
            REQUIRE(metadata.outerIterations == report->steps[i].outerIterations);
            REQUIRE(metadata.time.has_value());
            REQUIRE(metadata.time->si() == report->steps[i].time.si());
            REQUIRE(metadata.step.has_value());
            REQUIRE(*metadata.step == report->steps[i].index);
        }
    }
    SECTION("every sample is a validated result with its own time and step") {
        for (std::size_t i = 0; i < report->samples.size(); ++i) {
            INFO("sample " << i);
            const cfd::CfdResultMetadata& metadata = report->samples[i].metadata();
            REQUIRE(metadata.time.has_value());
            REQUIRE(metadata.step.has_value());
            CHECK(metadata.time->si() == report->sampleTimes[i].si());
            CHECK(*metadata.step == i + 1);
            CHECK(metadata.outerIterations == report->steps[i].outerIterations);
            CHECK(metadata.residuals == report->steps[i].residuals);
            // POSSESSION IS THE EVIDENCE: holding these fields proves their
            // three lengths were checked and every value is finite.
            CHECK(report->samples[i].fields().pressure().size() > 0);
        }
    }
    SECTION("the samples differ from each other, so the history is a history") {
        // A MARCH THAT PUBLISHED THE SAME FIELD THREE TIMES would satisfy every
        // length, time and index assertion above.
        const double firstToLast =
            maxAbsDiff(report->samples.front().fields().velocity(), report->samples.back().fields().velocity());
        INFO("largest |U(t_3) - U(t_1)| = " << firstToLast);
        CHECK(firstToLast > 0.0);
    }
    SECTION("the times ascend and the slot still holds only the last") {
        for (std::size_t i = 1; i < report->sampleTimes.size(); ++i) {
            REQUIRE(report->sampleTimes[i].si() > report->sampleTimes[i - 1].si());
        }
        REQUIRE(slot.result() != nullptr);
        CHECK(slot.result()->metadata() == report->samples.back().metadata());
    }
}

TEST_CASE("CfdTransient_LandsOnTheEndTimeWithAShortenedFinalStep", "[cfd][transient]") {
    // "Actual final partial step", end to end rather than on the grid alone:
    // 0.25 s at a 0.1 s step is two full steps and a 0.05 s remainder.
    Channel channel;
    channel.drive(Drive{.timeStep = Time::fromSi(0.1),
                        .endTime = Time::fromSi(0.25),
                        .sampling = CfdTimeSampling::EveryStep});
    CfdResultSlot slot;
    const std::expected<CfdTransientReport, CfdSolveFailure> report = channel.march(slot);
    INFO((report.has_value() ? std::string{} : report.error().error.message));
    REQUIRE(report.has_value());

    REQUIRE(report->grid.stepCount() == 3);
    CHECK(report->grid.hasPartialFinalStep());
    REQUIRE(report->steps.size() == 3);
    CHECK(report->steps[0].stepSize.si() == 0.1);
    CHECK(report->steps[1].stepSize.si() == 0.1);
    // THE SHORTENED ONE, and the step the solver actually assembled with.
    CHECK(std::abs(report->steps[2].stepSize.si() - 0.05) < 1.0e-15);
    CHECK(report->steps[2].time.si() == 0.25);
    REQUIRE(report->sampleTimes.size() == 3);
    CHECK(report->sampleTimes.back().si() == 0.25);

    SECTION("the shortened step used ITS OWN coefficient, not the nominal one") {
        // rho V / dt_last is TWICE rho V / dt for a step half as long. A march
        // that reused the nominal step's matrix on the remainder would report
        // the same coefficient here, and would be integrating 0.1 s of physics
        // over 0.05 s of time.
        const double full = report->steps[1].largestTransientCoefficient.si();
        const double partial = report->steps[2].largestTransientCoefficient.si();
        INFO("rho V / dt on the full step: " << full << ", on the remainder: " << partial);
        CHECK(std::abs(partial / full - 2.0) < 1.0e-9);
    }
}

TEST_CASE("CfdTransient_EachStepGetsItsOwnDivergenceBaseline", "[cfd][transient]") {
    // ADR-078 Decision 5, which is one `optional` reset inside the step loop
    // rather than outside it and so is invisible in a diff. Both residual
    // records are on the report exactly so this can be asserted.
    Channel channel;
    channel.drive(Drive{.timeStep = Time::fromSi(0.1),
                        .endTime = Time::fromSi(0.3),
                        .sampling = CfdTimeSampling::EveryStep});
    CfdResultSlot slot;
    const std::expected<CfdTransientReport, CfdSolveFailure> report = channel.march(slot);
    INFO((report.has_value() ? std::string{} : report.error().error.message));
    REQUIRE(report.has_value());
    REQUIRE(report->steps.size() == 3);

    // A RUN-WIDE BASELINE WOULD REPEAT STEP 1'S NUMBERS in every record. Each
    // step is posed from a new U^n, so its first-iteration residuals are its
    // own.
    const cfd::CfdResiduals& first = report->steps[0].firstResiduals;
    std::size_t moved = 0;
    for (std::size_t i = 1; i < report->steps.size(); ++i) {
        if (!(report->steps[i].firstResiduals == first)) {
            ++moved;
        }
    }
    INFO("steps whose baseline differs from step 1's: " << moved);
    CHECK(moved == report->steps.size() - 1);

    SECTION("every step converged against its own baseline") {
        for (const cfd::CfdTransientStepRecord& step : report->steps) {
            INFO("step " << step.index);
            // The verdict that accepted the step is the qualified pure
            // function, re-asked here against the records the report carries.
            REQUIRE(cfd::cfdCouplingVerdict(step.residuals, step.firstResiduals,
                                            cfd::findCfdAnalysis(channel.model.document(), channel.analysis)
                                                ->definition()
                                                .convergence) == cfd::CfdCouplingVerdict::Converged);
        }
    }
    SECTION("the prescribed boundary throughput does not move with time") {
        // A RUNTIME COMPANION TO THE COMPILE-FAIL GATES, AND NOT A SUBSTITUTE
        // FOR THEM. If a step read a different inlet speed -- a time-varying
        // boundary frozen differently per step -- the throughput would move by
        // an O(1) fraction, which is the failure mode this excludes. What it
        // does NOT prove is that no record COULD carry a time function; that is
        // a statement about the types, and `CfdTransientMisuse.cpp` holds it.
        //
        // THE BOUND IS THE CONVERGENCE TOLERANCE AND NOT ZERO, AND AN EARLIER
        // VERSION OF THIS ASSERTION HAD IT WRONG. It demanded equality to
        // 1e-9 relative and failed at 4.1e-7, which was the ASSERTION's defect
        // and not the march's: `continuityScale` is the boundary throughput,
        // and only the INLET half of it is prescribed. The outlet flux is
        // computed, and converges to the inlet's value only to the 1e-4 the
        // definition accepts, so it is free to differ between steps by about
        // that much. 1e-4 is therefore the derived bound; the measured
        // variation is 4.1e-7 relative, 240 times inside it.
        const double firstScale = report->steps.front().continuityScale;
        REQUIRE(firstScale > 0.0);
        for (const cfd::CfdTransientStepRecord& step : report->steps) {
            INFO("step " << step.index << " scale " << step.continuityScale);
            REQUIRE(std::abs(step.continuityScale - firstScale) <= 1.0e-4 * firstScale);
        }
    }
    SECTION("the per-step mass balance comes from the qualified evaluator") {
        // THESE ASSERTIONS HAVE TEETH BECAUSE A MUTATION ANALYSIS SHOWED THE
        // EARLIER ONES HAD NONE. `isfinite` and `isWellFormed` are both
        // satisfied by a default-constructed record -- `isWellFormed` is
        // "finite and not negative" on both members -- so every one of these
        // five fields survived being replaced by a hardcoded literal. What
        // follows is what a literal cannot satisfy.
        //
        // THE THROUGHPUT HAS AN ANALYTIC VALUE, which is the only genuinely
        // independent reference available for any of them:
        //
        //   rho * u * A = 998 * 1.0e-5 * (0.040 * 0.030) = 1.1976e-5 kg/s
        //
        // and `continuityScale` is the boundary throughput, which for a channel
        // with one inlet and one outlet is exactly that. The bound is the
        // convergence tolerance with margin: only the INLET half is prescribed,
        // and the outlet flux converges to it to 1e-4.
        const double analyticThroughput = kDensity.si() * kInletSpeed.si() * 0.040 * 0.030;
        WARN("analytic inlet throughput rho u A = " << analyticThroughput << " kg/s; measured scale "
             << report->steps.front().continuityScale << " kg/s");
        for (const cfd::CfdTransientStepRecord& step : report->steps) {
            INFO("step " << step.index);
            // The channel is OPEN -- an inlet and an outlet -- so ADR-058's two
            // relative measures are defined rather than undefined.
            REQUIRE(step.massBalanceDefined);
            REQUIRE_FALSE(step.closedDomain);
            REQUIRE(std::abs(step.continuityScale - analyticThroughput) <= 1.0e-2 * analyticThroughput);
            // A MEASUREMENT AND NOT A DEFAULT. The corrected field is
            // conservative to the pressure solve's own residual, which is tiny
            // and NOT zero, so a strictly positive measure is what distinguishes
            // the evaluator's answer from `CfdMassBalance{}`.
            REQUIRE(step.balance.maxCellContinuity > 0.0);
            REQUIRE(step.balance.globalImbalance >= 0.0);
            REQUIRE(std::isfinite(step.balance.globalImbalance));
            REQUIRE(cfd::isWellFormed(step.balance));
            // A CONVERGED STEP IS NOT AN EXACT ONE, so the last iteration still
            // applied a non-zero pressure correction.
            REQUIRE(step.largestPressureCorrection > 0.0);
        }
    }
    SECTION("a step's baseline really is its FIRST iteration and not its last") {
        // TWO MUTATIONS SURVIVED THE EARLIER VERSION OF THIS TEST: assigning
        // `firstResiduals` on every inner iteration instead of once, and
        // recording `iteration->residuals` as the baseline. Both leave the
        // "baseline moved between steps" check above satisfied, because the
        // per-step LAST residuals also differ between steps -- and the second
        // disables divergence detection entirely, since `now > factor * now` is
        // never true.
        //
        // WHAT DISTINGUISHES A GENUINE FIRST-ITERATION RECORD: a step that
        // needed more than one iteration did NOT start converged, so re-asking
        // the qualified verdict with its baseline as both arguments must answer
        // Continue. A record that was secretly the final one would answer
        // Converged, because that is how the step was accepted.
        const cfd::CfdConvergenceSettings convergence =
            cfd::findCfdAnalysis(channel.model.document(), channel.analysis)->definition().convergence;
        std::size_t multiStep = 0;
        for (const cfd::CfdTransientStepRecord& step : report->steps) {
            INFO("step " << step.index << " took " << step.outerIterations << " iterations");
            if (step.outerIterations <= 1) {
                continue;
            }
            ++multiStep;
            REQUIRE_FALSE(step.firstResiduals == step.residuals);
            REQUIRE(cfd::cfdCouplingVerdict(step.firstResiduals, step.firstResiduals, convergence) ==
                    cfd::CfdCouplingVerdict::Continue);
        }
        // AND AT LEAST ONE STEP MUST HAVE NEEDED MORE THAN ONE ITERATION, or
        // the loop above is vacuous and this section proves nothing.
        REQUIRE(multiStep > 0);
    }
}

// ---------------------------------------------------------------------------
// INDEPENDENT VALIDATION
// ---------------------------------------------------------------------------

TEST_CASE("CfdTransient_TheFirstStepsChangeScalesWithTheTimeStep", "[cfd][transient][validation]") {
    // "Time-step refinement", AND THE FIRST OF THE TWO TESTS THAT CAN SEE THE
    // ONE DEFECT THIS MILESTONE CAN INTRODUCE.
    //
    // Backward Euler from a divergence-compatible state gives
    // `U^1 - U^0 = O(dt)`, so halving dt halves the change. If the transient
    // source carried the CURRENT velocity instead of U^n (ADR-073 Decision 4),
    // the two halves of the term would cancel, the step would converge to the
    // STEADY solution at every dt, and this ratio would be 1 rather than 2.
    Channel channel;
    const auto changeAfterOneStep = [&channel](double dt) {
        channel.drive(Drive{.timeStep = Time::fromSi(dt), .endTime = Time::fromSi(dt)});
        CfdResultSlot slot;
        const std::expected<CfdTransientReport, CfdSolveFailure> report = channel.march(slot);
        INFO((report.has_value() ? std::string{} : report.error().error.message));
        REQUIRE(report.has_value());
        REQUIRE(report->grid.stepCount() == 1);
        REQUIRE(report->samples.size() == 1);
        // THE STEP MUST HAVE ACTUALLY COUPLED, and this assertion exists
        // because `TheAcceptanceCriterionWeakensAsTheStepShrinks` measured a
        // regime where it would not: below about dt = 1e-3 s on this fixture
        // the first iteration already satisfies the criterion, and a refinement
        // ratio taken across that boundary would be comparing a converged step
        // with a single-iteration one. Measured 60 iterations at each of the
        // three steps used here.
        REQUIRE(report->steps.front().outerIterations > 1);
        return maxAbsDiff(report->samples.front().fields().velocity(), report->initialVelocity);
    };

    const double coarse = changeAfterOneStep(0.2);
    const double fine = changeAfterOneStep(0.1);
    const double finer = changeAfterOneStep(0.05);
    INFO("largest |U^1 - U^0| at dt = 0.2, 0.1, 0.05 s: " << coarse << ", " << fine << ", " << finer);

    REQUIRE(coarse > 0.0);
    REQUIRE(fine > 0.0);
    REQUIRE(finer > 0.0);

    // FIRST ORDER: the ratio approaches 2 as dt shrinks. The window is wide
    // because the step is a nonlinear SIMPLE solve and not a linear one, and
    // because the convergence tolerance is 1e-4 on both runs; what it excludes
    // is the defect's signature, which is a ratio of 1.
    const double ratioCoarse = coarse / fine;
    const double ratioFine = fine / finer;
    INFO("ratios: " << ratioCoarse << " and " << ratioFine);
    CHECK(ratioCoarse > 1.6);
    CHECK(ratioCoarse < 2.5);
    CHECK(ratioFine > 1.6);
    CHECK(ratioFine < 2.5);
}

TEST_CASE("CfdTransient_TheAcceptanceCriterionWeakensAsTheStepShrinks", "[cfd][transient][validation]") {
    // AN ADVERSARIAL REVIEW DERIVED THIS AND THIS TEST MEASURES IT. The claim
    // is about the NORMALISATION of the per-step convergence criterion, and it
    // is a property of the qualified coupling rather than a defect introduced
    // here -- but it bounds what "the step converged" is worth, and nothing in
    // the tree recorded it.
    //
    // THE ALGEBRA. `advanceCfdCoupling` measures the momentum residual against
    // the field the iteration started from, with the system as supplied:
    //
    //     residual = A U - b,   normalised by  ||A||_inf ||U|| + ||b||
    //
    // On a step's FIRST iteration, U is U^n. ADR-073 puts rho V / dt on the
    // diagonal and rho V / dt * U^n in the source, so
    //
    //     A U^n - b = (A_steady + rho V/dt) U^n - (b_steady + rho V/dt * U^n)
    //               = A_steady U^n - b_steady
    //
    // and the two transient halves cancel EXACTLY. The numerator is therefore
    // independent of dt while ||A||_inf and ||b|| both grow as 1/dt, so the
    // reported first-iteration residual is Theta(dt) -- it falls toward zero as
    // the step shrinks, for any U^n, whether or not anything was solved.
    //
    // THE CONSEQUENCE, AND IT IS NOT A WRONG ANSWER. Below a dt set by the
    // mesh, the fluid and the tolerance, a step is accepted on its first
    // iteration and the march becomes a lagged-pressure, non-iterated scheme:
    // still consistent, still first-order, but no longer the converged
    // backward-Euler step the header describes. What stops meaning what a
    // reader expects is the EVIDENCE: `outerIterations` and the residual record
    // then say "converged" about a step that coupled nothing.
    //
    // RECORDED AS ADR-078 L11 and measured here rather than argued.
    Channel channel;
    const auto iterationsAt = [&channel](double dt) {
        channel.drive(Drive{.timeStep = Time::fromSi(dt),
                            .endTime = Time::fromSi(3.0 * dt),
                            .sampling = CfdTimeSampling::EveryStep});
        CfdResultSlot slot;
        const std::expected<CfdTransientReport, CfdSolveFailure> report = channel.march(slot);
        INFO((report.has_value() ? std::string{} : report.error().error.message));
        REQUIRE(report.has_value());
        REQUIRE(report->steps.size() == 3);
        return std::array<std::uint32_t, 3>{report->steps[0].outerIterations, report->steps[1].outerIterations,
                                            report->steps[2].outerIterations};
    };

    const std::array<std::uint32_t, 3> coarse = iterationsAt(1.0e-1);
    const std::array<std::uint32_t, 3> fine = iterationsAt(1.0e-3);
    const std::array<std::uint32_t, 3> finest = iterationsAt(1.0e-5);
    WARN("outer iterations per step at dt = 1e-1: " << coarse[0] << " " << coarse[1] << " " << coarse[2]
         << "; at 1e-3: " << fine[0] << " " << fine[1] << " " << fine[2] << "; at 1e-5: " << finest[0] << " "
         << finest[1] << " " << finest[2]);

    // WHAT WAS MEASURED, AND IT IS NOT WHAT THIS TEST FIRST PREDICTED:
    //
    // ```text
    //   dt      step 1   step 2   step 3
    //   1e-1      60        8        8
    //   1e-3       1       65       62
    //   1e-5       1       65       62
    // ```
    //
    // The first draft predicted the LATER steps would collapse to one
    // iteration, and asserted `fine[1] <= coarse[1]`. That assertion FAILED at
    // 65 <= 8, and the failure is the interesting part: it is STEP 1 that
    // collapses, from sixty iterations to one, and the later steps then become
    // more expensive rather than less.
    //
    // THE MECHANISM IS THE ONE THE REVIEW DERIVED, arriving one step earlier
    // than predicted. At dt = 1e-3 the first iteration's normalised momentum
    // residual is already under tolerance before anything has been coupled, so
    // step 1 is accepted having done almost nothing -- and because step 1 is
    // where the pressure field is normally built from p = 0, that work does not
    // disappear; it moves into steps 2 and 3, which is why they rise from 8 to
    // 65. The collapse is also SATURATED: 1e-3 and 1e-5 give identical counts,
    // so this is a regime and not a trend.
    //
    // THE ANSWER IS NOT WRONG. A step accepted on its first iteration is a
    // lagged-pressure step: still consistent, still first order, and still
    // published with a conservative flux, because the correction runs whether
    // or not the loop iterates again. What stops being worth what a reader
    // expects is the EVIDENCE -- `outerIterations = 1` and a residual under
    // tolerance say "this step converged" about a step that coupled nothing.
    //
    // ADR-078 L11 records it. Every validation in this file runs at
    // dt >= 0.05 s, where step 1 still takes tens of iterations, and
    // `TheFirstStepsChangeScalesWithTheTimeStep` asserts that directly so the
    // refinement measurement is provably about converged steps.
    REQUIRE(coarse[0] > 1);
    REQUIRE(coarse[1] > 1);

    // THE COLLAPSE, asserted with one iteration of slack across build
    // configurations rather than pinned at exactly 1: the claim is that the
    // criterion is satisfied essentially immediately, and the measured value is
    // 1 on debug-ext at both small steps.
    CHECK(fine[0] <= 2);
    CHECK(finest[0] <= 2);
    CHECK(fine[0] * 10 < coarse[0]);

    // AND THE WORK MOVED RATHER THAN VANISHING, which is what says the march as
    // a whole is still coupling and this is not a solver that stopped solving.
    CHECK(fine[1] > 1);
    CHECK(finest[1] > 1);
    CHECK(fine[1] + fine[2] > coarse[1] + coarse[2]);
}

TEST_CASE("CfdTransient_AShortMarchWithASmallStepIsNotTheSteadySolution", "[cfd][transient][validation]") {
    // THE SECOND TEST THAT CAN SEE THE DEFECT, and the direction that matters
    // most: with the current velocity in the transient source, ONE step would
    // already be the steady answer.
    Channel channel;

    channel.drive(Drive{.timeStep = Time::fromSi(0.1), .endTime = Time::fromSi(0.2)});
    CfdResultSlot transientSlot;
    const std::expected<CfdTransientReport, CfdSolveFailure> march = channel.march(transientSlot);
    INFO((march.has_value() ? std::string{} : march.error().error.message));
    REQUIRE(march.has_value());

    channel.drive(Drive{.mode = CfdAnalysisMode::Steady});
    CfdResultSlot steadySlot;
    const std::expected<CfdSteadyReport, CfdSolveFailure> steady = channel.solveSteady(steadySlot);
    INFO((steady.has_value() ? std::string{} : steady.error().error.message));
    REQUIRE(steady.has_value());
    REQUIRE(steadySlot.result() != nullptr);

    const std::span<const double> steadyU = steadySlot.result()->fields().velocity();
    const std::span<const double> marchedU = march->samples.back().fields().velocity();
    const double fromSteady = maxAbsDiff(marchedU, steadyU);
    const double fromStart = maxAbsDiff(marchedU, march->initialVelocity);
    const double startToSteady = maxAbsDiff(march->initialVelocity, steadyU);
    INFO("|U(0.2 s) - U_steady| = " << fromSteady << ", |U(0.2 s) - U^0| = " << fromStart
                                    << ", |U^0 - U_steady| = " << startToSteady);

    // 0.2 s is 0.002 viscous times, so the field has barely moved: it is still
    // far closer to plug flow than to the developed profile.
    REQUIRE(startToSteady > 0.0);
    CHECK(fromStart < 0.1 * startToSteady);
    CHECK(fromSteady > 0.5 * startToSteady);
}

TEST_CASE("CfdTransient_ASingleLargeStepIsTheSteadySolution", "[cfd][transient][validation]") {
    // THE STEADY LIMIT, and the cross-check between the two orchestration
    // bodies that ADR-078 Decision 4 promised in place of a shared helper. At
    // dt = 1e7 s -- 1e5 viscous times -- rho V / dt vanishes from the matrix
    // relative to convection and diffusion, so one step must land on the steady
    // solution. If the two step bodies ever diverge in a way that matters, this
    // is what sees it.
    Channel channel;

    channel.drive(Drive{.mode = CfdAnalysisMode::Steady});
    CfdResultSlot steadySlot;
    const std::expected<CfdSteadyReport, CfdSolveFailure> steady = channel.solveSteady(steadySlot);
    INFO((steady.has_value() ? std::string{} : steady.error().error.message));
    REQUIRE(steady.has_value());
    REQUIRE(steadySlot.result() != nullptr);

    const Time huge = Time::fromSi(1.0e7);
    REQUIRE(huge.si() > 1000.0 * viscousTimescaleSeconds());
    channel.drive(Drive{.timeStep = huge, .endTime = huge, .maxIterationsPerStep = 400});
    CfdResultSlot marchSlot;
    const std::expected<CfdTransientReport, CfdSolveFailure> march = channel.march(marchSlot);
    INFO((march.has_value() ? std::string{} : march.error().error.message));
    REQUIRE(march.has_value());
    REQUIRE(march->grid.stepCount() == 1);

    const std::span<const double> steadyU = steadySlot.result()->fields().velocity();
    const std::span<const double> steadyP = steadySlot.result()->fields().pressure();
    const std::span<const double> marchU = march->samples.back().fields().velocity();
    const std::span<const double> marchP = march->samples.back().fields().pressure();

    const double velocityGap = maxAbsDiff(marchU, steadyU) / std::max(maxAbs(steadyU), 1.0e-300);
    const double pressureGap = maxAbsDiff(marchP, steadyP) / std::max(maxAbs(steadyP), 1.0e-300);
    INFO("relative |U_1step - U_steady| = " << velocityGap << ", pressure " << pressureGap);

    // THE BOUND IS DERIVED FROM WHAT BOTH RUNS WERE ACCEPTED AT, not fitted to
    // the number measured. Each run stops when its momentum and continuity
    // residuals fall below 1e-4 relative and is then iterated no further, so
    // two solutions of the same fixed point can differ by about twice that
    // before either has broken a promise; 1e-3 is that with a decade of margin.
    //
    // MEASURED FAR BETTER THAN THE BOUND: 1.5e-7 on the velocity and 3.9e-8 on
    // the pressure (debug-ext), and the two runs took the SAME 59 outer
    // iterations. The bound is kept at the derived value rather than tightened
    // onto the measurement, because the measurement is what this test reports
    // and the bound is what it promises. What the bound has to exclude is the
    // defect's signature, which is an O(1) relative difference between two
    // orchestration bodies that have drifted apart.
    CHECK(velocityGap < 1.0e-3);
    CHECK(pressureGap < 1.0e-3);

    SECTION("and the transient term really was negligible, rather than absent") {
        // The term WAS applied -- so this is the steady limit of a transient
        // solve, not a steady solve in disguise.
        REQUIRE(march->steps.front().transientTermApplied);
        const double transient = march->steps.front().largestTransientCoefficient.si();
        WARN("largest rho V / dt at dt = 1e7 s: " << transient << " kg/s");
        CHECK(transient > 0.0);
        // THE BOUND IS DERIVED FROM THE DOMAIN rather than from whatever mesh
        // the mesher happens to produce, which is what an adversarial review
        // asked for: rho V / dt for ANY cell is at most rho * V_domain / dt,
        // and the domain is 40 x 30 x 20 mm, so
        //
        //     998 * 2.4e-5 / 1e7 = 2.4e-9 kg/s
        //
        // and even a hypothetical single-cell mesh stays four hundred times
        // below this bound. What makes 1e-6 the right ORDER to assert is the
        // coefficient it has to be negligible against: diffusion's
        // mu A / d ~ 5e-6 kg/s on this mesh. The transient term is five orders
        // below that here, which is what "it vanished from the matrix" means.
        CHECK(transient < 1.0e-6);
    }
}

TEST_CASE("CfdTransient_TheMarchApproachesTheSteadySolutionAsTimeGrows", "[cfd][transient][validation]") {
    // THE OTHER FORM OF THE STEADY LIMIT: not a large step, but many steps of a
    // physical size. The distance to the steady solution must FALL
    // monotonically, and the per-step change must shrink -- which is the
    // approach to the fixed point.
    //
    // THE FIXED-POINT TEST ADR-078 DECISION 8 DESCRIBED IS NOT REACHABLE, and
    // this replaces it. Starting a march AT the steady solution would need a
    // non-uniform initial condition, and `CfdInitialCondition` is uniform by
    // ADR-068's design: there is no way to put a developed profile into a
    // document. Nothing is lost for defect detection, because that test could
    // not see the U^n defect anyway (the two arrays are equal at a fixed
    // point); what is lost is a check on the term's exact cancellation, which
    // CfdMomentumTests.cpp holds one layer down.
    Channel channel;

    channel.drive(Drive{.mode = CfdAnalysisMode::Steady});
    CfdResultSlot steadySlot;
    const std::expected<CfdSteadyReport, CfdSolveFailure> steady = channel.solveSteady(steadySlot);
    INFO((steady.has_value() ? std::string{} : steady.error().error.message));
    REQUIRE(steady.has_value());
    REQUIRE(steadySlot.result() != nullptr);
    const std::span<const double> steadyU = steadySlot.result()->fields().velocity();

    // Four steps of half a viscous time each: two viscous times in all, which
    // is far enough for the profile to develop substantially.
    const Time step = Time::fromSi(0.5 * viscousTimescaleSeconds());
    channel.drive(Drive{.timeStep = step,
                        .endTime = Time::fromSi(4.0 * step.si()),
                        .maxIterationsPerStep = 200,
                        .sampling = CfdTimeSampling::EveryStep});
    CfdResultSlot marchSlot;
    const std::expected<CfdTransientReport, CfdSolveFailure> march = channel.march(marchSlot);
    INFO((march.has_value() ? std::string{} : march.error().error.message));
    REQUIRE(march.has_value());
    REQUIRE(march->samples.size() == 4);

    std::vector<double> distance;
    for (const CfdResult& sample : march->samples) {
        distance.push_back(maxAbsDiff(sample.fields().velocity(), steadyU));
    }
    const double startDistance = maxAbsDiff(march->initialVelocity, steadyU);
    INFO("|U^0 - U_steady| = " << startDistance << "; then " << distance[0] << ", " << distance[1] << ", "
                               << distance[2] << ", " << distance[3]);

    // MONOTONE APPROACH. A march that integrated the wrong sign of the history
    // term, or reused a step's matrix across a step boundary, would not give a
    // monotone sequence here.
    REQUIRE(distance[0] < startDistance);
    for (std::size_t i = 1; i < distance.size(); ++i) {
        INFO("sample " << i);
        REQUIRE(distance[i] < distance[i - 1]);
    }
    // AND IT GOT MEASURABLY CLOSER, so this is not four indistinguishable
    // states passing a monotonicity check on noise.
    CHECK(distance.back() < 0.5 * startDistance);

    SECTION("the per-step change shrinks as the fixed point is approached") {
        std::vector<double> change;
        change.push_back(maxAbsDiff(march->samples[0].fields().velocity(), march->initialVelocity));
        for (std::size_t i = 1; i < march->samples.size(); ++i) {
            change.push_back(
                maxAbsDiff(march->samples[i].fields().velocity(), march->samples[i - 1].fields().velocity()));
        }
        INFO("per-step |dU|: " << change[0] << ", " << change[1] << ", " << change[2] << ", " << change[3]);
        for (std::size_t i = 1; i < change.size(); ++i) {
            INFO("step " << i + 1);
            REQUIRE(change[i] < change[i - 1]);
        }
    }
}

// ---------------------------------------------------------------------------
// Failure paths
// ---------------------------------------------------------------------------

TEST_CASE("CfdTransient_RefusesASteadyAnalysisAndTheSteadySolveRefusesATransientOne", "[cfd][transient]") {
    Channel channel;

    SECTION("a transient march will not solve a steady analysis") {
        channel.drive(Drive{.mode = CfdAnalysisMode::Steady});
        CfdResultSlot slot;
        const std::expected<CfdTransientReport, CfdSolveFailure> report = channel.march(slot);
        REQUIRE_FALSE(report.has_value());
        CHECK(report.error().stage == CfdFailureStage::InputRefused);
        CHECK_THAT(report.error().error.message, ContainsSubstring("needs a transient analysis"));
        CHECK(slot.result() == nullptr);
        REQUIRE(slot.failure() != nullptr);
        CHECK(*slot.failure() == report.error());
    }
    SECTION("and the steady solve will not solve a transient one") {
        channel.drive(Drive{});
        CfdResultSlot slot;
        const std::expected<CfdSteadyReport, CfdSolveFailure> report = channel.solveSteady(slot);
        REQUIRE_FALSE(report.has_value());
        CHECK(report.error().stage == CfdFailureStage::InputRefused);
        CHECK_THAT(report.error().error.message, ContainsSubstring("needs a steady analysis"));
    }
}

TEST_CASE("CfdTransient_AStepThatExhaustsItsBudgetFailsTheWholeMarch", "[cfd][transient]") {
    // NO RETRY, NO SMALLER STEP, NO PARTIAL HISTORY. One outer iteration per
    // step cannot converge a coupled problem, so step 1 exhausts its budget and
    // the RUN fails -- publishing nothing, not a one-step history.
    Channel channel;
    channel.drive(Drive{.timeStep = Time::fromSi(0.1),
                        .endTime = Time::fromSi(0.3),
                        .maxIterationsPerStep = 1,
                        .sampling = CfdTimeSampling::EveryStep});
    CfdResultSlot slot;
    const std::expected<CfdTransientReport, CfdSolveFailure> report = channel.march(slot);
    REQUIRE_FALSE(report.has_value());
    CHECK(report.error().stage == CfdFailureStage::IterationLimitReached);
    CHECK_THAT(report.error().error.message, ContainsSubstring("did not converge"));
    // THE DIAGNOSTIC NAMES THE STEP AND THE TIME, because "a diagnostic says
    // what failed, where, why and which object caused it".
    CHECK_THAT(report.error().error.message, ContainsSubstring("step 1 of 3"));
    CHECK_THAT(report.error().error.message, ContainsSubstring("not retried"));
    CHECK(slot.result() == nullptr);
    REQUIRE(slot.failure() != nullptr);
    // THE VALUE AND NOT JUST ITS PRESENCE. A mutation analysis found that
    // saturating with `max` instead of `min` -- which would report 4294967295
    // for every failure -- survived, because only `has_value()` was asserted.
    // One iteration was permitted and one was taken.
    REQUIRE(slot.failure()->outerIterations.has_value());
    CHECK(*slot.failure()->outerIterations == 1);
    // AND THE FAILURE CARRIES ITS PROVENANCE, which only the negative case
    // (an analysis that is not in the document) asserted.
    CHECK(slot.failure()->source.has_value());
}

TEST_CASE("CfdTransient_AFailedMarchKeepsWhatTheModelWas", "[cfd][transient]") {
    // Publication is atomic over the WHOLE march, and a failure neither erases
    // the previous result nor makes it current.
    Channel channel;
    channel.drive(Drive{.timeStep = Time::fromSi(0.1), .endTime = Time::fromSi(0.1)});
    CfdResultSlot slot;
    const std::expected<CfdTransientReport, CfdSolveFailure> good = channel.march(slot);
    INFO((good.has_value() ? std::string{} : good.error().error.message));
    REQUIRE(good.has_value());
    REQUIRE(slot.result() != nullptr);
    const cfd::CfdResultMetadata kept = slot.result()->metadata();
    const std::vector<double> keptVelocity{slot.result()->fields().velocity().begin(),
                                           slot.result()->fields().velocity().end()};

    channel.drive(Drive{.timeStep = Time::fromSi(0.1), .endTime = Time::fromSi(0.2), .maxIterationsPerStep = 1});
    const std::expected<CfdTransientReport, CfdSolveFailure> bad = channel.march(slot);
    REQUIRE_FALSE(bad.has_value());

    REQUIRE(slot.result() != nullptr);
    CHECK(slot.result()->metadata() == kept);
    CHECK(maxAbsDiff(slot.result()->fields().velocity(), keptVelocity) == 0.0);
    REQUIRE(slot.failure() != nullptr);
    // THE ANALYSIS IS NOT SOLVED-CURRENT while its latest attempt failed, even
    // though the surviving result's own inputs still match.
    CHECK(cfd::analysisState(channel.model.document(), channel.model.regenerator(), channel.model.mesher(),
                             channel.analysis, slot.result(), slot.failure()) == cfd::AnalysisState::SolveFailed);
}

TEST_CASE("CfdTransient_AMarchThatFailsAtALaterStepPublishesNothing", "[cfd][transient][validation]") {
    // `ADR-078`'s HEADLINE ATOMICITY PROMISE, TESTED WHERE IT CAN ACTUALLY
    // BREAK. A mutation analysis found that moving `slot.recordSuccess` inside
    // the sampling block -- publishing each sample as it is produced -- survived
    // the whole suite, because EVERY failure test failed at step 1, where no
    // sample had been produced yet. A march that failed at step 3 of 5 would
    // then leave the slot holding step 2 while reporting a failure, which is
    // precisely what "a partial history is not a worse history, it is a claim
    // about a time span that was never integrated" forbids.
    Channel channel;
    const Time step = Time::fromSi(0.1);

    // PHASE 1: how many solve calls does step 1 alone take? A one-step march at
    // the same dt from the same state does exactly step 1's work, so its call
    // count is step 1's -- and nothing in the transient settings but the step
    // itself reaches the assembly, so step 1 of a three-step march is
    // bit-identical to it.
    channel.drive(Drive{.timeStep = step, .endTime = step});
    const auto firstStepCalls = std::make_shared<std::size_t>(0);
    CfdResultSlot slot;
    const std::expected<CfdTransientReport, CfdSolveFailure> one =
        channel.march(slot, refusingAfter(std::numeric_limits<std::size_t>::max(), firstStepCalls));
    INFO((one.has_value() ? std::string{} : one.error().error.message));
    REQUIRE(one.has_value());
    REQUIRE(*firstStepCalls > 0);
    REQUIRE(slot.result() != nullptr);
    const cfd::CfdResultMetadata kept = slot.result()->metadata();
    const std::vector<double> keptVelocity{slot.result()->fields().velocity().begin(),
                                           slot.result()->fields().velocity().end()};
    WARN("solve-port calls for step 1 alone: " << *firstStepCalls << " over "
         << one->steps.front().outerIterations << " outer iterations");

    // PHASE 2: three steps, sampling EVERY step, with the port refusing on the
    // call immediately after step 1's last. Step 1 therefore converges and is
    // sampled, and step 2's very first solve is refused.
    channel.drive(Drive{.timeStep = step,
                        .endTime = Time::fromSi(3.0 * step.si()),
                        .sampling = CfdTimeSampling::EveryStep});
    const auto calls = std::make_shared<std::size_t>(0);
    const std::expected<CfdTransientReport, CfdSolveFailure> bad =
        channel.march(slot, refusingAfter(*firstStepCalls, calls));
    REQUIRE_FALSE(bad.has_value());
    // The refusal came from the port, through the coupling's vocabulary and
    // `stageOf`, on the call after step 1's last.
    CHECK(bad.error().stage == CfdFailureStage::LinearSolveFailed);
    CHECK(*calls == *firstStepCalls + 1);

    // AND NOTHING FROM STEP 1 REACHED THE SLOT. What is held is still the
    // one-step march's result, byte for byte.
    REQUIRE(slot.result() != nullptr);
    CHECK(slot.result()->metadata() == kept);
    CHECK(maxAbsDiff(slot.result()->fields().velocity(), keptVelocity) == 0.0);
    REQUIRE(slot.result()->metadata().step.has_value());
    CHECK(*slot.result()->metadata().step == 1);
    CHECK(slot.result()->metadata().time->si() == 0.1);
    REQUIRE(slot.failure() != nullptr);
    CHECK(cfd::analysisState(channel.model.document(), channel.model.regenerator(), channel.model.mesher(),
                             channel.analysis, slot.result(), slot.failure()) == cfd::AnalysisState::SolveFailed);

    SECTION("and the step-number diagnostic is exercised past step 1") {
        // THE "step N of M" FORMATTING FOR N > 1, which nothing else reaches:
        // every other failure in this file happens at step 1, so `step + 1`
        // could have been written `1` and no assertion would have moved.
        const auto later = std::make_shared<std::size_t>(0);
        CfdResultSlot other;
        const std::expected<CfdTransientReport, CfdSolveFailure> diverged =
            channel.march(other, inflatingAfter(*firstStepCalls + 4, 1.0e3, later));
        REQUIRE_FALSE(diverged.has_value());
        INFO(diverged.error().error.message);
        CHECK(diverged.error().stage == CfdFailureStage::Diverged);
        CHECK_THAT(diverged.error().error.message, ContainsSubstring("step 2 of 3"));
    }
}

TEST_CASE("CfdTransient_GravityReachesEveryStepsMomentumEquation", "[cfd][transient][validation]") {
    // `CfdTransient.cpp` CLAIMS THIS AND NOTHING HELD THE CLAIM. The body-force
    // line in the step assembly carries a comment saying an adversarial review
    // found it missing from `solveCfdSteady` and that the defect "is not
    // repeated here" -- but a mutation analysis found that dropping
    // `momentum.gravity = definition.gravity` survived this entire suite,
    // because no transient fixture set a gravity record at all.
    //
    // THE DISCRIMINATOR IS THE STEADY SUITE'S, MEASURED THERE RATHER THAN
    // PREDICTED HERE: dp/dy over the cells reaches 1.158 of -rho*g with the
    // body force and only 0.160 without, because the outlet's prescribed
    // hydrostatic profile alone cannot carry the gradient into the interior.
    // Neither the sign of dp/dy nor the transverse flow distinguishes them.
    //
    // ONE LARGE STEP, AND THAT IS NOT A WEAKER TEST THAN A PHYSICAL dt. At a
    // small dt the transverse momentum balance is dominated by rho dU/dt, so
    // dp/dy is NOT forced to -rho*g and the discriminator would measure
    // nothing. At dt = 1e7 s the transient term has vanished from the matrix
    // and the balance is the steady one. What is being tested is whether the
    // gravity record reaches the per-iteration assembly at all, and that line
    // is the same line for every step of every march.
    constexpr double kGravitySi = 5.0e-8;

    Channel channel;
    CfdAnalysisDefinition definition = channel.definitionFor(
        Drive{.timeStep = Time::fromSi(1.0e7), .endTime = Time::fromSi(1.0e7), .maxIterationsPerStep = 400});
    definition.gravity = cfd::CfdGravity{cfd::Acceleration3{.x = Acceleration::fromSi(0.0),
                                                            .y = Acceleration::fromSi(-kGravitySi),
                                                            .z = Acceleration::fromSi(0.0)},
                                         0_mm};
    channel.store(definition);

    CfdResultSlot slot;
    const std::expected<CfdTransientReport, CfdSolveFailure> report = channel.march(slot);
    INFO((report.has_value() ? std::string{} : report.error().error.message));
    REQUIRE(report.has_value());
    REQUIRE(slot.result() != nullptr);
    REQUIRE(report->steps.front().transientTermApplied);

    const CfdResultFields& fields = slot.result()->fields();
    const std::size_t cells = fields.pressure().size();
    REQUIRE(cells > 0);

    // THE PECLET CONFINEMENT FROM THE MEASURED FIELD, because gravity is now
    // also driving the flow.
    double fastest = 0.0;
    for (const double value : fields.velocity()) {
        fastest = std::max(fastest, std::abs(value));
    }
    const double measuredPeclet = kDensity.si() * fastest * kDomainLengthM / kViscosity.si();
    REQUIRE(measuredPeclet < 1.0);

    // dp/dy BY A LEAST-SQUARES SLOPE OVER ALL CELLS, not by two cells: the
    // axial variation is uncorrelated with y across this mesh, so it enters as
    // scatter rather than bias.
    const cfd::CfdGeometry geometry = channel.geometry();
    double sumY = 0.0;
    double sumP = 0.0;
    for (std::size_t cell = 0; cell < cells; ++cell) {
        const std::optional<Point3D> centroid = geometry.cellCentroid(CellIndex::fromValue(cell));
        REQUIRE(centroid.has_value());
        sumY += centroid->y.si();
        sumP += fields.pressure()[cell];
    }
    const double meanY = sumY / static_cast<double>(cells);
    const double meanP = sumP / static_cast<double>(cells);
    double covariance = 0.0;
    double variance = 0.0;
    for (std::size_t cell = 0; cell < cells; ++cell) {
        const std::optional<Point3D> centroid = geometry.cellCentroid(CellIndex::fromValue(cell));
        REQUIRE(centroid.has_value());
        const double dy = centroid->y.si() - meanY;
        covariance += dy * (fields.pressure()[cell] - meanP);
        variance += dy * dy;
    }
    REQUIRE(variance > 0.0);
    const double slope = covariance / variance;
    const double expected = -kDensity.si() * kGravitySi;
    WARN("transient dp/dy " << slope << " Pa/m, -rho*g " << expected << " Pa/m, ratio " << slope / expected);
    // THE SAME WINDOW THE STEADY SUITE USES, and it is bounded by the defect's
    // signature rather than fitted: it holds 1.158 with room on both sides and
    // excludes 0.160 by a wide margin.
    CHECK(slope < 0.0);
    CHECK(slope / expected > 0.5);
    CHECK(slope / expected < 2.0);
}

TEST_CASE("CfdTransient_RefusesAStepCountTheGridCannotRepresent", "[cfd][transient]") {
    // The grid's resource guard, reached THROUGH the solver so that the
    // refusal's stage and its publication are covered too.
    Channel channel;
    channel.drive(Drive{.timeStep = Time::fromSi(1.0e-9), .endTime = Time::fromSi(1.0e6)});
    CfdResultSlot slot;
    const std::expected<CfdTransientReport, CfdSolveFailure> report = channel.march(slot);
    REQUIRE_FALSE(report.has_value());
    CHECK(report.error().stage == CfdFailureStage::InputRefused);
    CHECK_THAT(report.error().error.message, ContainsSubstring("more than the limit"));
    CHECK(slot.result() == nullptr);
    CHECK(slot.failure() != nullptr);
}

TEST_CASE("CfdTransient_RefusesAHistoryLargerThanTheStoredValueGuard", "[cfd][transient]") {
    // REFUSED BEFORE MARCHING, and never reduced to fewer samples: quietly
    // downgrading EveryStep to FinalOnly would publish a history the definition
    // did not ask for under a fingerprint that says it did.
    Channel channel;
    const CfdFieldMap map = channel.layout();
    const std::size_t perSample =
        map.size(CfdVariable::Pressure) + map.size(CfdVariable::Velocity) + map.size(CfdVariable::MassFlux);
    REQUIRE(perSample > 0);
    const std::size_t tooMany = cfd::kMaxCfdStoredValues / perSample + 1;
    INFO("values per sample " << perSample << ", samples needed to trip the guard " << tooMany);
    REQUIRE(tooMany <= cfd::kMaxCfdTimeSteps);

    const double dt = 1.0 / static_cast<double>(tooMany);
    channel.drive(Drive{.timeStep = Time::fromSi(dt),
                        .endTime = Time::fromSi(1.0),
                        .sampling = CfdTimeSampling::EveryStep});
    CfdResultSlot slot;
    const std::expected<CfdTransientReport, CfdSolveFailure> report = channel.march(slot);
    REQUIRE_FALSE(report.has_value());
    CHECK(report.error().stage == CfdFailureStage::InputRefused);
    CHECK_THAT(report.error().error.message, ContainsSubstring("more than the limit"));
    CHECK_THAT(report.error().error.message, ContainsSubstring("not reduced to fit"));
    CHECK(slot.result() == nullptr);

    SECTION("exactly the guard's limit is accepted") {
        // THE BOUNDARY, which a `>=` in place of the `>` would refuse. A
        // mutation analysis found nothing probed it: `tooMany` is one past the
        // limit, so both comparisons refuse it.
        const std::size_t exactly = cfd::kMaxCfdStoredValues / perSample;
        REQUIRE(exactly >= 1);
        channel.drive(Drive{.timeStep = Time::fromSi(1.0 / static_cast<double>(exactly)),
                            .endTime = Time::fromSi(1.0),
                            .maxIterationsPerStep = 1,
                            .sampling = CfdTimeSampling::EveryStep});
        CfdResultSlot other;
        const std::expected<CfdTransientReport, CfdSolveFailure> edge = channel.march(other);
        REQUIRE_FALSE(edge.has_value());
        // It is refused for running out of iterations, NOT for the history
        // size: the guard let this one through.
        CHECK(edge.error().stage == CfdFailureStage::IterationLimitReached);
    }
    SECTION("the same step count with FinalOnly is accepted by the guard") {
        // The guard is on the STORED HISTORY and not on the step count, so the
        // same march keeping one sample passes it and is then refused for a
        // DIFFERENT reason -- a budget of one iteration cannot converge step 1,
        // which has to build the whole pressure field from p = 0. What matters
        // here is that the refusal is not the stored-value guard's.
        //
        // AN EARLIER COMMENT SAID "or not at all", which the REQUIRE_FALSE
        // below forbids; an adversarial review found the contradiction. The
        // assertion is the claim, so the comment was wrong and not the
        // assertion.
        channel.drive(Drive{.timeStep = Time::fromSi(dt),
                            .endTime = Time::fromSi(1.0),
                            .maxIterationsPerStep = 1,
                            .sampling = CfdTimeSampling::FinalOnly});
        CfdResultSlot other;
        const std::expected<CfdTransientReport, CfdSolveFailure> second = channel.march(other);
        REQUIRE_FALSE(second.has_value());
        CHECK(second.error().stage != CfdFailureStage::InputRefused);
    }
}

TEST_CASE("CfdTransient_ANaNFromTheSolveIsRejectedRatherThanPublished", "[cfd][transient]") {
    // "Reject NaN / Inf", discharged by the MAPPING rather than by a loop here:
    // the coupling refuses a non-finite solution one layer down and `stageOf`
    // turns its vocabulary into this one's.
    Channel channel;
    channel.drive(Drive{.timeStep = Time::fromSi(0.1), .endTime = Time::fromSi(0.1)});
    CfdResultSlot slot;
    const CfdLinearSolve poisoned = [](const cfd::CfdSparseMatrix& matrix, std::span<const double>,
                                       const cfd::CfdLinearSolverSettings&)
        -> std::expected<CfdLinearSolveResult, Error> {
        return CfdLinearSolveResult{
            .solution = std::vector<double>(matrix.rows(), std::numeric_limits<double>::quiet_NaN()),
            .normalizedResidual = 0.0};
    };
    const std::expected<CfdTransientReport, CfdSolveFailure> report = channel.march(slot, poisoned);
    REQUIRE_FALSE(report.has_value());
    INFO(report.error().error.message);
    CHECK((report.error().stage == CfdFailureStage::NonFiniteField ||
           report.error().stage == CfdFailureStage::LinearSolveFailed));
    CHECK(slot.result() == nullptr);
    CHECK(slot.failure() != nullptr);
}

TEST_CASE("CfdTransient_ADivergingStepIsRefusedByItsRealName", "[cfd][transient]") {
    // THE DIVERGENCE IS FORCED, AND AN EARLIER VERSION OF THIS TEST ONLY HOPED
    // FOR IT. That version set `divergenceFactor` barely above 1 and carried a
    // `WARN`-and-`return` escape for "no residual grew anywhere" -- which a
    // mutation analysis showed meant the `Diverged` refusal could be DELETED
    // outright and this test would still pass, taking its own escape hatch
    // while `IterationLimitReached` stood in for the stage. A test that cannot
    // fail for its own reason is not a test.
    //
    // `inflatingAfter` is the replacement: it hands back production's solution
    // multiplied by a thousand, with the residual production measured.
    // `ADR-075` L4 is what makes that constructible -- the coupling trusts the
    // residual a port reports and "no check here can detect that" -- so the
    // first iteration establishes an honest baseline and the second is a
    // thousand times worse than it. The default divergence factor of 100 then
    // fires, and it fires deterministically.
    Channel channel;
    const auto calls = std::make_shared<std::size_t>(0);
    channel.drive(Drive{.timeStep = Time::fromSi(0.1), .endTime = Time::fromSi(0.3)});
    CfdResultSlot slot;
    const std::expected<CfdTransientReport, CfdSolveFailure> report =
        channel.march(slot, inflatingAfter(4, 1.0e3, calls));
    REQUIRE_FALSE(report.has_value());
    INFO(report.error().error.message);
    CHECK(report.error().stage == CfdFailureStage::Diverged);
    CHECK_THAT(report.error().error.message, ContainsSubstring("diverged"));
    CHECK_THAT(report.error().error.message, ContainsSubstring("step 1 of 3"));
    // THE MESSAGE NAMES WHICH RESIDUAL GREW AND BOTH NUMBERS, which is the one
    // thing `residualsThatGrew` exists for: swapping its two arguments empties
    // the list and "from" appears only inside the per-criterion format.
    CHECK_THAT(report.error().error.message, ContainsSubstring("from"));
    CHECK_THAT(report.error().error.message, ContainsSubstring("times its first-iteration value"));
    CHECK(slot.result() == nullptr);
    CHECK(slot.failure() != nullptr);
}

// ---------------------------------------------------------------------------
// Determinism and provenance
// ---------------------------------------------------------------------------

TEST_CASE("CfdTransient_RepeatingTheMarchPublishesIdenticalHistories", "[cfd][transient][determinism]") {
    Channel channel;
    channel.drive(Drive{.timeStep = Time::fromSi(0.1),
                        .endTime = Time::fromSi(0.2),
                        .sampling = CfdTimeSampling::EveryStep});

    CfdResultSlot first;
    const std::expected<CfdTransientReport, CfdSolveFailure> one = channel.march(first);
    INFO((one.has_value() ? std::string{} : one.error().error.message));
    REQUIRE(one.has_value());
    CfdResultSlot second;
    const std::expected<CfdTransientReport, CfdSolveFailure> two = channel.march(second);
    REQUIRE(two.has_value());

    // BIT-EXACT, which is what a determinism claim is about. `CfdTransientReport`
    // has no operator== because `CfdResult` has none, so the comparison is
    // written out -- which also says exactly what is being compared.
    CHECK(one->grid == two->grid);
    CHECK(one->steps == two->steps);
    CHECK(one->sampleTimes == two->sampleTimes);
    CHECK(one->initialPressure == two->initialPressure);
    CHECK(one->initialVelocity == two->initialVelocity);
    CHECK(one->totalOuterIterations == two->totalOuterIterations);
    REQUIRE(one->samples.size() == two->samples.size());
    for (std::size_t i = 0; i < one->samples.size(); ++i) {
        INFO("sample " << i);
        REQUIRE(one->samples[i].fields() == two->samples[i].fields());
        REQUIRE(one->samples[i].metadata() == two->samples[i].metadata());
        REQUIRE(one->samples[i].source() == two->samples[i].source());
    }
}

TEST_CASE("CfdTransient_TheSettingsThatDroveTheMarchAreTheOnesTheFingerprintNames",
          "[cfd][transient]") {
    // ADR-077 Decision 7, inherited: the settings come from the definition, so
    // the fingerprint the sample publishes cannot name settings the run did not
    // use. Changing them moves the fingerprint, which is the only way to change
    // them.
    Channel channel;
    channel.drive(Drive{.timeStep = Time::fromSi(0.1), .endTime = Time::fromSi(0.1)});
    CfdResultSlot slot;
    const std::expected<CfdTransientReport, CfdSolveFailure> first = channel.march(slot);
    INFO((first.has_value() ? std::string{} : first.error().error.message));
    REQUIRE(first.has_value());
    const cfd::CfdResultSource before = first->samples.back().source();

    // A DIFFERENT TIME STEP IS A DIFFERENT ANALYSIS, because canonicalText
    // writes transient.time_step into the fingerprint.
    channel.drive(Drive{.timeStep = Time::fromSi(0.05), .endTime = Time::fromSi(0.05)});
    CfdResultSlot other;
    const std::expected<CfdTransientReport, CfdSolveFailure> second = channel.march(other);
    REQUIRE(second.has_value());
    const cfd::CfdResultSource after = second->samples.back().source();

    CHECK(before.definition != after.definition);
    // And the earlier result is now STALE against the current source, which is
    // the whole purpose of carrying the fingerprint.
    const Result<cfd::CfdResultSource> current = cfd::currentResultSource(
        channel.model.document(), channel.model.regenerator(), channel.model.mesher(), channel.analysis);
    REQUIRE(current.has_value());
    CHECK(cfd::resultCurrency(&first->samples.back(), *current) == cfd::ResultCurrency::Stale);
    CHECK(cfd::resultCurrency(&second->samples.back(), *current) == cfd::ResultCurrency::Current);
}

TEST_CASE("CfdTransient_AClosedDomainMarchesWithAnUndefinedMassBalance", "[cfd][transient]") {
    // THE ONE PATH ON WHICH THE PUBLISHED METADATA IS NOT A MEASUREMENT, and it
    // is here because a mutation analysis found that `closedDomain` and
    // `massBalanceDefined` both survived being replaced by a hardcoded literal:
    // every other march in this file drives the OPEN channel, where the
    // expected values are `false` and `true`. A fixture whose expected values
    // are the OTHER ONES is the only thing that can falsify a constant.
    //
    // ADR-058's two mass-balance measures are relative to the BOUNDARY
    // THROUGHPUT, which is zero when every facet is a wall, so they are
    // UNDEFINED rather than small. `CfdResultMetadata` has no field that can
    // say "undefined", so what reaches the slot is `(0, 0)` -- which READS AS A
    // PERFECT BALANCE at every sampled step. That is the transient form of
    // ADR-077 L11 and this test makes it observable so it cannot change
    // unnoticed.
    Channel channel;
    const cfd::CfdGeometry geometry = channel.geometry();
    const std::optional<Point3D> inside = geometry.cellCentroid(CellIndex::fromValue(0));
    REQUIRE(inside.has_value());

    // Every facet a wall, nothing fixing the pressure but a reference, and the
    // fluid AT REST at t = 0 -- not the plug flow the open channel starts from,
    // which in a closed box would be an impulsive stop against the walls.
    CfdAnalysisDefinition definition = channel.definitionFor(Drive{.timeStep = Time::fromSi(0.1),
                                                                   .endTime = Time::fromSi(0.2),
                                                                   .sampling = CfdTimeSampling::EveryStep,
                                                                   .plugFlowInitialCondition = false});
    definition.boundaryConditions = {
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), NoSlipWall{channel.faces.startCap()}},
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(2), NoSlipWall{channel.faces.endCap()}},
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(3), NoSlipWall{channel.faces.side(0)}},
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(4), NoSlipWall{channel.faces.side(1)}},
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(5), NoSlipWall{channel.faces.side(2)}},
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(6), NoSlipWall{channel.faces.side(3)}},
    };
    definition.referencePressure = cfd::CfdReferencePressure{.value = 0_Pa, .location = *inside};
    channel.store(definition);

    CfdResultSlot slot;
    const std::expected<CfdTransientReport, CfdSolveFailure> report = channel.march(slot);
    INFO((report.has_value() ? std::string{} : report.error().error.message));
    REQUIRE(report.has_value());
    REQUIRE(slot.result() != nullptr);
    REQUIRE(report->steps.size() == 2);

    SECTION("every step reports the domain closed and its measures undefined") {
        for (const cfd::CfdTransientStepRecord& step : report->steps) {
            INFO("step " << step.index);
            REQUIRE(step.closedDomain);
            REQUIRE_FALSE(step.massBalanceDefined);
            // EXACTLY the default, not approximately: nothing is computed into
            // it. This is the assertion the open-channel test cannot make and
            // the open-channel test's `> 0.0` is the one this cannot.
            REQUIRE(step.balance == cfd::CfdMassBalance{});
        }
    }
    SECTION("and the published samples carry that default at every time level") {
        for (const CfdResult& sample : report->samples) {
            CHECK(sample.metadata().massBalance == cfd::CfdMassBalance{});
            // Accepted by the result's own gate, which is why this cannot be
            // caught by `CfdResult::create` and has to be documented instead.
            CHECK(cfd::isWellFormed(sample.metadata().massBalance));
        }
    }
    SECTION("a closed domain at rest stays at rest for the whole march") {
        // No inflow, no body force, walls everywhere: the only solution is zero
        // flow at every time level. A march that reported motion here would be
        // manufacturing it.
        for (const CfdResult& sample : report->samples) {
            CHECK(maxAbs(sample.fields().velocity()) == 0.0);
            CHECK(maxAbs(sample.fields().massFlux()) == 0.0);
        }
    }
}

TEST_CASE("CfdTransient_RefusesEveryInputItCannotMarch", "[cfd][transient]") {
    Channel channel;
    SECTION("an analysis that is not in the document") {
        CfdResultSlot slot;
        const std::expected<CfdTransientReport, CfdSolveFailure> report = cfd::solveCfdTransient(
            channel.model.document(), channel.model.regenerator(), channel.model.mesher(),
            CfdAnalysisId::fromValue(9999), cfd::cfdLinearSolve(), slot);
        REQUIRE_FALSE(report.has_value());
        CHECK(report.error().stage == CfdFailureStage::InputRefused);
        CHECK(report.error().source.has_value() == false);
        CHECK_THAT(report.error().error.message, ContainsSubstring("no CFD analysis"));
    }
    SECTION("a definition whose pressure level nothing fixes") {
        CfdAnalysisDefinition definition = channel.definitionFor(Drive{});
        // Replace the pressure outlet with a wall: nothing fixes the level, and
        // there is no reference pressure.
        definition.boundaryConditions[1] =
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(2), NoSlipWall{channel.faces.endCap()}};
        channel.store(definition);
        CfdResultSlot slot;
        const std::expected<CfdTransientReport, CfdSolveFailure> report = channel.march(slot);
        REQUIRE_FALSE(report.has_value());
        CHECK(report.error().stage == CfdFailureStage::InputRefused);
        CHECK(slot.result() == nullptr);
        CHECK(slot.failure() != nullptr);
    }
}
