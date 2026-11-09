// P19-STEADY-001 -- the steady incompressible solve, end to end from a Document.
//
// WHAT THIS SUITE IS ABOUT, AND WHAT IT IS DELIBERATELY NOT ABOUT. Every
// numerical step inside the loop belongs to an operator qualified under an
// earlier milestone, and each has its own suite: the predictor and the four
// corrections are `CfdCouplingTests.cpp`, the residuals and the mass balance are
// `CfdContinuityTests.cpp`, the two kernels are `CfdLinearSolveTests.cpp`. Those
// are not re-tested here, because a second set of assertions over the same
// arithmetic is not independent evidence of anything.
//
// What IS here is what only this milestone can get wrong:
//
// ```text
// the loop            does it stop for the right reason, and report it
// the vocabulary      do sixteen coupling problems land on the right nine stages
// the publication     is it atomic, and does a failure keep what came before
// the provenance      do the settings that drove the run match the fingerprint
//                     the result publishes
// the fields          are they the layout's lengths, and are they the ones the
//                     last iteration produced
// ```
//
// THE PECLET CONFINEMENT IS ASSERTED, NOT ASSUMED. `P19-PVCOUPLE-001` is
// qualified only at LOW CELL PECLET NUMBER, so the inlet speed here is chosen to
// satisfy that and the test says so with a `REQUIRE` rather than a comment.
// Water at 0.1 m/s through 5 mm cells is `Pe ~ 500`, which would be outside
// every qualified bound in the tree.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "reference/MeshTestSupport.hpp"

#include <bettercad/cfd/CfdAnalysis.hpp>
#include <bettercad/cfd/CfdAnalysisObject.hpp>
#include <bettercad/cfd/CfdBoundary.hpp>
#include <bettercad/cfd/CfdCoupling.hpp>
#include <bettercad/cfd/CfdConvection.hpp>
#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdDivergence.hpp>
#include <bettercad/cfd/CfdFields.hpp>
#include <bettercad/cfd/CfdGeometry.hpp>
#include <bettercad/cfd/CfdLinearSolve.hpp>
#include <bettercad/cfd/CfdMesh.hpp>
#include <bettercad/cfd/CfdResult.hpp>
#include <bettercad/cfd/CfdSparse.hpp>
#include <bettercad/cfd/CfdSteady.hpp>
#include <bettercad/cfd/CfdTopology.hpp>
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
#include <optional>
#include <span>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using bettercad::cfd::Acceleration3;
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
using bettercad::cfd::CfdLinearSolverSettings;
using bettercad::cfd::CfdResult;
using bettercad::cfd::CfdResultFields;
using bettercad::cfd::CfdResultSlot;
using bettercad::cfd::CfdSolveFailure;
using bettercad::cfd::CfdSparseMatrix;
using bettercad::cfd::CfdSteadyReport;
using bettercad::cfd::CfdVariable;
using bettercad::cfd::NoSlipWall;
using bettercad::cfd::PressureOutlet;
using bettercad::cfd::VelocityInlet;
using Catch::Matchers::ContainsSubstring;

namespace {

constexpr Density kDensity = 998_kg_per_m3;
constexpr DynamicViscosity kViscosity = DynamicViscosity::fromSi(1.002e-3);

/// The inlet speed, chosen so the CELL Peclet number stays below 1.
///
/// `Pe = rho u dx / mu`. The test asserts it against the domain's LARGEST
/// dimension, 40 mm, which is conservative: every real cell is smaller, so the
/// real cell Peclet number is smaller than the number asserted.
constexpr Velocity kInletSpeed = Velocity::fromSi(1.0e-5);
constexpr double kDomainLengthM = 0.040;

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

/// A 40 x 30 x 20 mm channel, every CAD face named, the control called "Mesh"
/// so `MeshedReference` adopts it. The same construction
/// `CfdBoundaryMappedTests.cpp` uses, because a different one would make the
/// two suites' figures incomparable.
Document channelDocument() {
    Document document{"CfdSteadyChannel"};
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

/// The four side-face names, read OFF THE MAP rather than reconstructed.
struct ChannelFaces {
    ObjectId feature{};
    std::array<EntityId, 4> lines{};

    [[nodiscard]] FaceName startCap() const { return FaceName{feature, FaceSelector{.role = FaceRole::StartCap}}; }
    [[nodiscard]] FaceName endCap() const { return FaceName{feature, FaceSelector{.role = FaceRole::EndCap}}; }
    [[nodiscard]] FaceName side(std::size_t which) const {
        return FaceName{feature, FaceSelector{.role = FaceRole::Side, .entity = lines.at(which)}};
    }
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
        store(channelDefinition());
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
    /// is also the pressure datum -- and four no-slip walls. Six conditions over
    /// six named faces: full coverage, and the pressure level is fixed.
    ///
    /// THE TOLERANCES ARE LOOSENED FROM ADR-058'S DEFAULTS, DELIBERATELY AND
    /// THROUGH THE DEFINITION. 1e-6 on a Debug build takes long enough that the
    /// suite would be the slowest thing in the tree; 1e-4 still requires many
    /// outer iterations and a genuinely coupled solution. Loosening them through
    /// the definition is also the only way to loosen them, which is the point of
    /// ADR-077 Decision 7.
    [[nodiscard]] CfdAnalysisDefinition channelDefinition() const {
        CfdAnalysisDefinition definition{.mesh = model.control()};
        definition.mode = CfdAnalysisMode::Steady;
        definition.boundaryConditions = {
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1), VelocityInlet{faces.startCap(), kInletSpeed}},
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(2), PressureOutlet{faces.endCap(), 0_Pa}},
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(3), NoSlipWall{faces.side(0)}},
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(4), NoSlipWall{faces.side(1)}},
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(5), NoSlipWall{faces.side(2)}},
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(6), NoSlipWall{faces.side(3)}},
        };
        definition.convergence.momentumResidualTolerance = 1.0e-4;
        definition.convergence.continuityResidualTolerance = 1.0e-4;
        definition.steady = cfd::CfdSteadySettings{.maxIterations = 400};
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

    [[nodiscard]] std::expected<CfdSteadyReport, CfdSolveFailure> solve(CfdResultSlot& slot,
                                                                       const CfdLinearSolve& port) {
        return cfd::solveCfdSteady(model.document(), model.regenerator(), model.mesher(), analysis, port, slot);
    }

    [[nodiscard]] std::expected<CfdSteadyReport, CfdSolveFailure> solve(CfdResultSlot& slot) {
        return solve(slot, cfd::cfdLinearSolve());
    }

    /// The prepared boundary, for a test that recomputes the predicted flux.
    [[nodiscard]] Result<cfd::PreparedCfdBoundary> boundary() {
        const Result<cfd::CfdModel> input =
            cfd::requireCfdModel(model.document(), model.regenerator(), model.mesher(), model.control());
        REQUIRE(input.has_value());
        const Result<cfd::CfdMeshContract> contract = cfd::requireCfdMesh(*input);
        REQUIRE(contract.has_value());
        const Result<cfd::CfdTopology> built = cfd::requireCfdTopology(*input, *contract);
        REQUIRE(built.has_value());
        const Result<cfd::CfdGeometry> metrics = cfd::requireCfdGeometry(*input, *built);
        REQUIRE(metrics.has_value());
        const cfd::CfdAnalysis* object = cfd::findCfdAnalysis(model.document(), analysis);
        REQUIRE(object != nullptr);
        return cfd::prepareCfdBoundary(*input, *built, *metrics, object->definition());
    }

    /// The finite-volume topology, for a test that diverges the published flux.
    [[nodiscard]] cfd::CfdTopology topology() {
        const Result<cfd::CfdModel> input =
            cfd::requireCfdModel(model.document(), model.regenerator(), model.mesher(), model.control());
        REQUIRE(input.has_value());
        const Result<cfd::CfdMeshContract> contract = cfd::requireCfdMesh(*input);
        REQUIRE(contract.has_value());
        Result<cfd::CfdTopology> built = cfd::requireCfdTopology(*input, *contract);
        INFO((built.has_value() ? std::string{} : built.error().message));
        REQUIRE(built.has_value());
        return std::move(*built);
    }

    /// The finite-volume geometry, for a test that needs cell positions.
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

/// The cell Peclet number, bounded conservatively by the domain's longest side.
[[nodiscard]] double conservativePeclet() {
    return kDensity.si() * kInletSpeed.si() * kDomainLengthM / kViscosity.si();
}

// ---------------------------------------------------------------------------
// An INDEPENDENT dense solve, written here from the textbook algorithm
// ---------------------------------------------------------------------------

/// Gaussian elimination with partial pivoting, and a residual computed from the
/// ORIGINAL dense copy.
///
/// INDEPENDENT OF PRODUCTION BY CONSTRUCTION. It shares no code with
/// `linalg::solveSymmetricSparse` or `linalg::solveSparse`: a different
/// algorithm (dense LU with row pivoting rather than a sparse LDLT or QR), a
/// different storage order, and its own residual. `P19-PVCOUPLE-001`'s review
/// found an "independent" oracle that was a character-for-character
/// transcription of production, so the distinction is stated and kept.
[[nodiscard]] std::expected<CfdLinearSolveResult, Error> denseSolve(const CfdSparseMatrix& matrix,
                                                                   std::span<const double> rightHandSide,
                                                                   const CfdLinearSolverSettings& settings) {
    const std::size_t n = matrix.rows();
    if (n == 0 || matrix.columns() != n || rightHandSide.size() != n) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "dense reference: the system is not square"});
    }
    std::vector<double> a(n * n, 0.0);
    for (std::size_t r = 0; r < n; ++r) {
        for (std::size_t c = 0; c < n; ++c) {
            a[r * n + c] = matrix.coeffSi(r, c);
        }
    }
    const std::vector<double> original = a;
    std::vector<double> x(rightHandSide.begin(), rightHandSide.end());

    for (std::size_t col = 0; col < n; ++col) {
        std::size_t pivot = col;
        for (std::size_t r = col + 1; r < n; ++r) {
            if (std::abs(a[r * n + col]) > std::abs(a[pivot * n + col])) {
                pivot = r;
            }
        }
        if (!(std::abs(a[pivot * n + col]) > 0.0)) {
            return std::unexpected(Error{ErrorCode::FailedPrecondition, "dense reference: the system is singular"});
        }
        if (pivot != col) {
            for (std::size_t c = 0; c < n; ++c) {
                std::swap(a[pivot * n + c], a[col * n + c]);
            }
            std::swap(x[pivot], x[col]);
        }
        for (std::size_t r = col + 1; r < n; ++r) {
            const double factor = a[r * n + col] / a[col * n + col];
            if (factor == 0.0) {
                continue;
            }
            for (std::size_t c = col; c < n; ++c) {
                a[r * n + c] -= factor * a[col * n + c];
            }
            x[r] -= factor * x[col];
        }
    }
    for (std::size_t back = n; back-- > 0;) {
        double sum = x[back];
        for (std::size_t c = back + 1; c < n; ++c) {
            sum -= a[back * n + c] * x[c];
        }
        x[back] = sum / a[back * n + back];
    }
    for (const double value : x) {
        if (!std::isfinite(value)) {
            return std::unexpected(Error{ErrorCode::FailedPrecondition, "dense reference: the solution is not finite"});
        }
    }

    // The residual, from the ORIGINAL copy and this function's own norms.
    double worst = 0.0;
    double scale = 0.0;
    for (std::size_t r = 0; r < n; ++r) {
        double row = 0.0;
        for (std::size_t c = 0; c < n; ++c) {
            row += original[r * n + c] * x[c];
        }
        worst = std::max(worst, std::abs(row - rightHandSide[r]));
        scale = std::max(scale, std::abs(rightHandSide[r]));
    }
    // max|Ax - b| / max|b|, which is NOT the measure production reports --
    // ADR-052's kernel uses ||r||_2 / (||A||_inf ||x||_2 + ||b||_2). Both are
    // then compared against the same `relativeResidualTolerance` by
    // `advanceCfdCoupling`, so the two runs' linear solves are gated on
    // different quantities. One more reason the two loops need not stop on the
    // same iteration, which is why the caller now asserts that they did. An
    // adversarial review asked for this to be stated.
    const double normalized = scale > 0.0 ? worst / scale : worst;
    if (normalized > settings.relativeResidualTolerance) {
        return std::unexpected(Error{ErrorCode::FailedPrecondition, "dense reference: the residual is too large"});
    }
    return CfdLinearSolveResult{.solution = std::move(x), .normalizedResidual = normalized};
}

/// The production port with its solutions multiplied by @p gain: a run that
/// over-corrects every iteration and therefore grows.
[[nodiscard]] CfdLinearSolve amplifying(double gain) {
    return [gain](const CfdSparseMatrix& matrix, std::span<const double> rhs,
                  const CfdLinearSolverSettings& settings) -> std::expected<CfdLinearSolveResult, Error> {
        std::expected<CfdLinearSolveResult, Error> solved = cfd::cfdLinearSolve()(matrix, rhs, settings);
        if (!solved.has_value()) {
            return solved;
        }
        for (double& value : solved->solution) {
            value *= gain;
        }
        return solved;
    };
}

/// A port that answers with a NaN solution. The residual it claims is 0, so
/// nothing but a finiteness check can catch it.
[[nodiscard]] CfdLinearSolve nonFinitePort() {
    return [](const CfdSparseMatrix& matrix, std::span<const double> rhs,
              const CfdLinearSolverSettings&) -> std::expected<CfdLinearSolveResult, Error> {
        (void)rhs;
        return CfdLinearSolveResult{
            .solution = std::vector<double>(matrix.rows(), std::numeric_limits<double>::quiet_NaN()),
            .normalizedResidual = 0.0};
    };
}

[[nodiscard]] double largestAbsoluteDifference(std::span<const double> a, std::span<const double> b) {
    REQUIRE(a.size() == b.size());
    double worst = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        worst = std::max(worst, std::abs(a[i] - b[i]));
    }
    return worst;
}

[[nodiscard]] double largestMagnitude(std::span<const double> values) {
    double worst = 0.0;
    for (const double value : values) {
        worst = std::max(worst, std::abs(value));
    }
    return worst;
}

} // namespace

// ---------------------------------------------------------------------------
// The run that works
// ---------------------------------------------------------------------------

TEST_CASE("CfdSteady_SolvesTheChannelAndPublishesAResultWithItsFields", "[cfd][steady]") {
    // THE CONFINEMENT FIRST. Everything below is only meaningful inside
    // P19-PVCOUPLE-001's qualified bound.
    REQUIRE(conservativePeclet() < 1.0);

    Channel channel;
    CfdResultSlot slot;
    const std::expected<CfdSteadyReport, CfdSolveFailure> report = channel.solve(slot);
    INFO((report.has_value() ? std::string{} : report.error().error.message));
    REQUIRE(report.has_value());

    SECTION("it converged, and took more than one iteration to do it") {
        // MORE THAN ONE MATTERS: a loop that published after a single pass
        // would satisfy every other assertion here while never having coupled
        // anything.
        CHECK(report->outerIterations > 1);
        CHECK(report->outerIterations < 400);
    }
    SECTION("a result is published, and it is the analysis's current one") {
        REQUIRE(slot.result() != nullptr);
        CHECK(slot.failure() == nullptr);
        CHECK(cfd::analysisState(channel.model.document(), channel.model.regenerator(), channel.model.mesher(),
                                 channel.analysis, slot.result(), slot.failure()) ==
              cfd::AnalysisState::SolvedCurrent);
    }
    SECTION("the published fields have the LAYOUT's three lengths") {
        const CfdFieldMap map = channel.layout();
        REQUIRE(slot.result() != nullptr);
        const CfdResultFields& fields = slot.result()->fields();
        CHECK(fields.pressure().size() == map.size(CfdVariable::Pressure));
        CHECK(fields.velocity().size() == map.size(CfdVariable::Velocity));
        CHECK(fields.massFlux().size() == map.size(CfdVariable::MassFlux));
        CHECK(fields.velocity().size() == 3 * map.cells());
        CHECK(fields.massFlux().size() == map.faces());
    }
    SECTION("the published fields are finite and the flow is not identically zero") {
        REQUIRE(slot.result() != nullptr);
        const CfdResultFields& fields = slot.result()->fields();
        for (const std::span<const double> array : {fields.pressure(), fields.velocity(), fields.massFlux()}) {
            for (const double value : array) {
                REQUIRE(std::isfinite(value));
            }
        }
        // A SOLVER THAT PUBLISHED ZEROS WOULD PASS EVERY LENGTH AND FINITENESS
        // CHECK ABOVE. Water is entering, so the flux cannot be zero everywhere.
        CHECK(largestMagnitude(fields.massFlux()) > 0.0);
        CHECK(largestMagnitude(fields.velocity()) > 0.0);
    }
    SECTION("the report's own numbers are the ones it claims") {
        // CLOSES THREE SURVIVING PROBES. The campaign showed that
        // `firstResiduals` could publish the LAST iteration's record (M1), the
        // iteration count could be off by one (M2) and
        // `largestPressureCorrection` could report `continuityScale` instead
        // (M4), with nothing in the suite noticing any of them.
        const CfdAnalysisDefinition definition = channel.channelDefinition();
        REQUIRE(definition.steady.has_value());

        // M2: a run that converged inside its budget cannot have taken more
        // iterations than the budget allows.
        CHECK(report->outerIterations <= definition.steady->maxIterations);

        // M1: the run started un-converged and ended converged, so the first
        // iteration's residuals CANNOT equal the last's. Publishing the last
        // record twice is what M1 did.
        CHECK_FALSE(report->firstResiduals == report->residuals);
        CHECK(report->firstResiduals.continuity > report->residuals.continuity);

        // M4: `largestPressureCorrection` is a pressure in Pa and
        // `continuityScale` is a throughput in kg/s; at convergence the
        // correction is small and the scale is not. Asserting they differ pins
        // the swap, and asserting the correction is small pins which is which.
        CHECK(report->largestPressureCorrection != report->continuityScale);
        CHECK(report->largestPressureCorrection >= 0.0);
        CHECK(report->largestPressureCorrection < report->continuityScale);
    }
    SECTION("the metadata agrees with the report, and carries no time or step") {
        REQUIRE(slot.result() != nullptr);
        const cfd::CfdResultMetadata& metadata = slot.result()->metadata();
        CHECK(metadata.outerIterations == report->outerIterations);
        CHECK(metadata.residuals == report->residuals);
        CHECK_FALSE(metadata.time.has_value());
        CHECK_FALSE(metadata.step.has_value());
        // M3 REPLACED THE PUBLISHED BALANCE WITH THE DEFAULT AND SURVIVED,
        // because the only test that looked at `massBalance` was the
        // closed-domain one, where the default is CORRECT. In an open domain
        // the evaluator's measures are defined, so the default is a
        // measurement nobody made.
        CHECK(report->massBalanceDefined);
        CHECK_FALSE(metadata.massBalance == cfd::CfdMassBalance{});
        CHECK(cfd::isWellFormed(metadata.massBalance));
    }
    SECTION("the residuals met the tolerances the DEFINITION asked for") {
        const CfdAnalysisDefinition definition = channel.channelDefinition();
        CHECK(report->residuals.momentumX <= definition.convergence.momentumResidualTolerance);
        CHECK(report->residuals.momentumY <= definition.convergence.momentumResidualTolerance);
        CHECK(report->residuals.momentumZ <= definition.convergence.momentumResidualTolerance);
        CHECK(report->residuals.continuity <= definition.convergence.continuityResidualTolerance);
    }
    SECTION("the channel is an OPEN domain, so the mass balance is defined") {
        // The closed-domain case is `CfdContinuityTests.cpp`'s; what matters
        // here is that this fixture is the open one, or the balance published
        // would be the default and the assertion above would be vacuous.
        CHECK_FALSE(report->closedDomain);
        CHECK(report->massBalanceDefined);
        CHECK(report->continuityScale > 0.0);
    }
}

// ---------------------------------------------------------------------------
// The independent reference
// ---------------------------------------------------------------------------

TEST_CASE("CfdSteady_TheProductionPortAndADenseReferenceAgree", "[cfd][steady][validation]") {
    // THE STRONGEST CHECK IN THIS MILESTONE. Both runs drive the same
    // orchestration; only the linear solve differs, and the dense one is a
    // different algorithm written here from the textbook. If the orchestration
    // were wrong, both would be wrong in the same way and this would pass -- so
    // it is evidence about the SOLVE being interchangeable, which is exactly
    // what ADR-075's supplied-port design claims.
    REQUIRE(conservativePeclet() < 1.0);

    Channel channel;
    CfdResultSlot production;
    CfdResultSlot reference;
    const std::expected<CfdSteadyReport, CfdSolveFailure> fast = channel.solve(production);
    INFO((fast.has_value() ? std::string{} : fast.error().error.message));
    REQUIRE(fast.has_value());
    const std::expected<CfdSteadyReport, CfdSolveFailure> slow = channel.solve(reference, denseSolve);
    INFO((slow.has_value() ? std::string{} : slow.error().error.message));
    REQUIRE(slow.has_value());

    REQUIRE(production.result() != nullptr);
    REQUIRE(reference.result() != nullptr);
    const CfdResultFields& a = production.result()->fields();
    const CfdResultFields& b = reference.result()->fields();

    const double pressureScale = std::max(largestMagnitude(a.pressure()), 1.0e-300);
    const double velocityScale = std::max(largestMagnitude(a.velocity()), 1.0e-300);
    const double fluxScale = std::max(largestMagnitude(a.massFlux()), 1.0e-300);

    INFO("iterations: production " << fast->outerIterations << ", dense " << slow->outerIterations);
    WARN("steady channel, production vs dense: pressure "
         << largestAbsoluteDifference(a.pressure(), b.pressure()) / pressureScale << ", velocity "
         << largestAbsoluteDifference(a.velocity(), b.velocity()) / velocityScale << ", flux "
         << largestAbsoluteDifference(a.massFlux(), b.massFlux()) / fluxScale);

    // THE TWO RUNS MUST STOP ON THE SAME ITERATION, AND THAT IS NOW ASSERTED
    // RATHER THAN LOGGED. An adversarial review pointed out that the agreement
    // below is only achievable while they do: one iteration apart, the published
    // fields differ by one SIMPLE update -- order 1e-4 at this fixture's
    // tolerance, not 1e-15 -- and the comparison would fail by four decades,
    // reading to a maintainer as a numerical regression rather than as a
    // stopping disagreement. Asserting it keeps the two failures distinguishable.
    CHECK(fast->outerIterations == slow->outerIterations);

    // TWO DECADES ABOVE THE MEASUREMENT, which is this project's practice
    // (`kCfdCouplingCompatibilityTolerance`: "One decade above the balanced
    // measurement, rounded up to the next power of ten"). The first version used
    // 1e-8 -- seven decades above -- which the same review noted would let a
    // regression degrade agreement a millionfold and still pass.
    // Measured: pressure 1.485e-15, velocity 4.626e-16, flux 1.415e-16.
    CHECK(largestAbsoluteDifference(a.pressure(), b.pressure()) / pressureScale < 1.0e-13);
    CHECK(largestAbsoluteDifference(a.velocity(), b.velocity()) / velocityScale < 1.0e-13);
    CHECK(largestAbsoluteDifference(a.massFlux(), b.massFlux()) / fluxScale < 1.0e-13);
}

// ---------------------------------------------------------------------------
// Determinism
// ---------------------------------------------------------------------------

TEST_CASE("CfdSteady_RepeatingTheRunPublishesIdenticalFields", "[cfd][steady][determinism]") {
    // NO TOLERANCE, because there is nothing to tolerance: the same operations
    // in the same order on the same data. The same form P17-SOLVE-001 and
    // P19-LINSOLVE-001 used.
    Channel channel;
    CfdResultSlot first;
    const std::expected<CfdSteadyReport, CfdSolveFailure> once = channel.solve(first);
    REQUIRE(once.has_value());
    REQUIRE(first.result() != nullptr);
    const CfdResultFields published = first.result()->fields();

    std::size_t differences = 0;
    for (int repeat = 0; repeat < 3; ++repeat) {
        CfdResultSlot again;
        const std::expected<CfdSteadyReport, CfdSolveFailure> more = channel.solve(again);
        REQUIRE(more.has_value());
        REQUIRE(again.result() != nullptr);
        if (!(again.result()->fields() == published) || !(*more == *once)) {
            ++differences;
        }
    }
    CHECK(differences == 0);
}

// ---------------------------------------------------------------------------
// The loop's three ways of stopping badly
// ---------------------------------------------------------------------------

TEST_CASE("CfdSteady_ABudgetTooSmallIsAFailureAndNeverAResult", "[cfd][steady]") {
    // CfdSteadySettings states it and CfdCoupling.hpp repeats it: "Reaching the
    // budget without converging is a FAILURE and never a result."
    Channel channel;
    CfdAnalysisDefinition definition = channel.channelDefinition();
    definition.steady = cfd::CfdSteadySettings{.maxIterations = 1};
    channel.store(definition);

    CfdResultSlot slot;
    const std::expected<CfdSteadyReport, CfdSolveFailure> report = channel.solve(slot);
    REQUIRE_FALSE(report.has_value());
    CHECK(report.error().stage == CfdFailureStage::IterationLimitReached);
    CHECK_THAT(report.error().error.message, ContainsSubstring("did not converge"));
    CHECK(report.error().outerIterations == 1U);
    // NOTHING IS PUBLISHED, and the failure is.
    CHECK(slot.result() == nullptr);
    CHECK(slot.failure() != nullptr);
    CHECK(slot.failure()->stage == CfdFailureStage::IterationLimitReached);
    // The source is known by then, because the inputs resolved.
    CHECK(slot.failure()->source.has_value());
}

TEST_CASE("CfdSteady_ADivergingRunIsRefusedByItsRealName", "[cfd][steady]") {
    // THE GAIN IS MEASURED, NOT CHOSEN. A first draft used 8.0 and got
    // `IllPosed`, because an over-corrected velocity drives the upwind
    // convection coefficient negative and `assembleCfdMomentum`'s diagonal
    // guard fires before any residual can grow by a factor of 1000. The band
    // that actually diverges is narrow, and `experiments/stage-probe.out` holds
    // the whole sweep:
    //
    //     1.05, 1.2   iteration_limit_reached after 400
    //     1.5         DIVERGED after 13
    //     2.0, 4.0    ill_posed (momentum diagonal at cell 7 goes negative)
    //     1e300       linear_solve_failed (the port's own residual gate)
    Channel channel;
    CfdResultSlot slot;
    const std::expected<CfdSteadyReport, CfdSolveFailure> report = channel.solve(slot, amplifying(1.5));
    REQUIRE_FALSE(report.has_value());
    // Diverged, NOT IterationLimitReached: the verdict is checked before the
    // budget, so a run that blows up stops for the reason it blew up, and after
    // 13 iterations rather than spending all 400.
    CHECK(report.error().stage == CfdFailureStage::Diverged);
    // THE DIAGNOSTIC NAMES THE CRITERION AND BOTH NUMBERS, which is what an
    // adversarial review asked for and what ARCHITECTURE.md requires of a
    // diagnostic. The measured message is:
    //
    //   "CFD steady: outer iteration 13 diverged, a residual past 1000 times
    //    its first-iteration value: continuity 2127.85 from 0.10585"
    //
    // AND IT IS CONTINUITY THAT GROWS, NOT MOMENTUM -- which independently
    // confirms the same review's finding that the x- and y-momentum baselines
    // are exactly zero from a zero initial field, so the ratio test is inert for
    // them. ADR-077 L9 records that.
    CHECK_THAT(report.error().error.message, ContainsSubstring("diverged"));
    CHECK_THAT(report.error().error.message, ContainsSubstring("first-iteration value:"));
    CHECK_THAT(report.error().error.message, ContainsSubstring(" from "));
    REQUIRE(report.error().outerIterations.has_value());
    CHECK(*report.error().outerIterations < 400U);
    CHECK(slot.result() == nullptr);
    CHECK(slot.failure() != nullptr);
}

TEST_CASE("CfdSteady_ANaNFromTheSolveIsRejectedAsAnUnusableSolve", "[cfd][steady]") {
    // ITEM 14, "NaN / Inf rejection", AND THE STAGE IT SURFACES UNDER WAS
    // MEASURED RATHER THAN PREDICTED.
    //
    // A first draft of this case asserted `NonFiniteField` and FAILED: the
    // coupling documents `LinearSolutionUnusable` as "the supplied solve
    // returned a solution of the wrong length, OR ONE HOLDING A NON-FINITE
    // VALUE", and `stageOf` maps that to `LinearSolveFailed`. That is the right
    // answer -- a port returning NaN is a broken SOLVE, not a field that went
    // non-finite -- and the test was wrong, not the code.
    //
    // What item 14 requires is that nothing non-finite can be published, and
    // that is what is asserted here: the run is refused and the slot stays
    // empty. `CfdSteady_ThreeOfTheNineStagesAreUnreachableFromHere` records
    // which stage cannot be produced at all, and why.
    Channel channel;
    CfdResultSlot slot;
    const std::expected<CfdSteadyReport, CfdSolveFailure> report = channel.solve(slot, nonFinitePort());
    REQUIRE_FALSE(report.has_value());
    CHECK(report.error().stage == CfdFailureStage::LinearSolveFailed);
    CHECK(slot.result() == nullptr);
    CHECK(slot.failure() != nullptr);
}

TEST_CASE("CfdSteady_ThreeOfTheNineStagesAreUnreachableFromHere", "[cfd][steady]") {
    // RECORDED RATHER THAN PAPERED OVER, and measured over fifteen driving
    // ports (`experiments/stage-probe.out`). `CfdFailureStage` has nine values
    // and belongs to `CfdResult.hpp`, not to this milestone; three of them
    // cannot be produced by `solveCfdSteady`:
    //
    // ```text
    // MassBalanceFailed      "the mass balance failed VALIDATION" -- that is
    //                        P19-VALID-001's, and CfdResult.hpp says
    //                        convergence is recorded here and not re-judged
    // ContinuityNotClosed    would need the `continuityClosed` flag that
    //                        ADR-075 Decision 5 DELETED after a mutation probe
    //                        showed it was true by construction for every input
    // NonFiniteField         every route to it is intercepted by a guard in a
    //                        lower qualified layer, measured: a non-finite
    //                        solution is LinearSolutionUnusable; a
    //                        finite-but-extreme one drives the momentum
    //                        diagonal negative (IllPosed) or trips the solve's
    //                        own residual gate (LinearSolveFailed)
    // ```
    //
    // MANUFACTURING A CHECK TO LIGHT ANY OF THEM would be the inverse of the
    // rule this milestone already applied once, when a non-finite field loop was
    // written and then removed as unreachable (ADR-077 Decision 4). So the
    // absence is asserted instead, and ADR-077 L8 carries it.
    //
    // The MAPPING onto NonFiniteField is still total and still tested, directly,
    // in `CfdSteady_TheProblemTableIsPinnedWholeAndTotal`: `stageOf` must answer
    // for every `CfdCouplingProblem` the layer below can raise, whether or not
    // this milestone can drive the layer below into raising it.
    using cfd::CfdCouplingProblem;
    CHECK(cfd::stageOf(CfdCouplingProblem::ValueNotFinite) == CfdFailureStage::NonFiniteField);
    CHECK(cfd::stageOf(CfdCouplingProblem::ResultIsNotFinite) == CfdFailureStage::NonFiniteField);
    CHECK(cfd::stageOf(CfdCouplingProblem::LinearSolutionUnusable) == CfdFailureStage::LinearSolveFailed);

    // And nothing non-finite reaches a result whatever happens, because the
    // field container refuses it as a type invariant.
    Channel channel;
    const CfdFieldMap map = channel.layout();
    std::vector<double> pressure(map.size(CfdVariable::Pressure), 0.0);
    pressure.front() = std::numeric_limits<double>::quiet_NaN();
    CHECK_FALSE(CfdResultFields::create(map, pressure,
                                        std::vector<double>(map.size(CfdVariable::Velocity), 0.0),
                                        std::vector<double>(map.size(CfdVariable::MassFlux), 0.0))
                    .has_value());
}

// ---------------------------------------------------------------------------
// Inputs
// ---------------------------------------------------------------------------

TEST_CASE("CfdSteady_RefusesEveryInputItCannotSolve", "[cfd][steady]") {
    Channel channel;

    SECTION("an analysis the document does not hold") {
        CfdResultSlot slot;
        const std::expected<CfdSteadyReport, CfdSolveFailure> report =
            cfd::solveCfdSteady(channel.model.document(), channel.model.regenerator(), channel.model.mesher(),
                                CfdAnalysisId::fromValue(987654), cfd::cfdLinearSolve(), slot);
        REQUIRE_FALSE(report.has_value());
        CHECK(report.error().stage == CfdFailureStage::InputRefused);
        CHECK_THAT(report.error().error.message, ContainsSubstring("there is no CFD analysis"));
        // NO SOURCE, because the inputs never resolved far enough to have one.
        CHECK_FALSE(report.error().source.has_value());
        CHECK_FALSE(report.error().outerIterations.has_value());
    }
    SECTION("a transient analysis is not a steady one with a looser budget") {
        CfdAnalysisDefinition definition = channel.channelDefinition();
        definition.mode = CfdAnalysisMode::Transient;
        definition.steady.reset();
        // A REAL transient record: `validate` refuses a zero time step, so a
        // default-constructed one would be rejected at STORE time and the
        // section would never reach the solve it is about.
        definition.transient = cfd::CfdTransientSettings{.timeStep = 1_s, .endTime = 10_s};
        channel.store(definition);
        CfdResultSlot slot;
        const std::expected<CfdSteadyReport, CfdSolveFailure> report = channel.solve(slot);
        REQUIRE_FALSE(report.has_value());
        CHECK(report.error().stage == CfdFailureStage::InputRefused);
        CHECK_THAT(report.error().error.message, ContainsSubstring("steady solve needs a steady analysis"));
    }
    SECTION("a definition with no pressure datum has no inputs a solve could use") {
        CfdAnalysisDefinition definition = channel.channelDefinition();
        // Replace the outlet with a wall: nothing then fixes the pressure.
        definition.boundaryConditions[1] =
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(2), NoSlipWall{channel.faces.endCap()}};
        channel.store(definition);
        CfdResultSlot slot;
        const std::expected<CfdSteadyReport, CfdSolveFailure> report = channel.solve(slot);
        REQUIRE_FALSE(report.has_value());
        CHECK(report.error().stage == CfdFailureStage::InputRefused);
    }
}

// ---------------------------------------------------------------------------
// The publication, and the provenance
// ---------------------------------------------------------------------------

TEST_CASE("CfdSteady_AFailedAttemptKeepsWhatTheModelWas", "[cfd][steady]") {
    // CfdResultSlot's documented contract, now exercised by its first
    // production consumer: "the result, if any, is KEPT UNTOUCHED -- not
    // discarded, not re-stamped -- so a failed attempt can neither erase what
    // the model was nor make an old result current."
    Channel channel;
    CfdResultSlot slot;
    REQUIRE(channel.solve(slot).has_value());
    REQUIRE(slot.result() != nullptr);
    const CfdResultFields published = slot.result()->fields();
    const cfd::CfdResultSource source = slot.result()->source();

    // Now make the next attempt fail, through the definition.
    CfdAnalysisDefinition definition = channel.channelDefinition();
    definition.steady = cfd::CfdSteadySettings{.maxIterations = 1};
    channel.store(definition);
    REQUIRE_FALSE(channel.solve(slot).has_value());

    REQUIRE(slot.result() != nullptr);
    CHECK(slot.result()->fields() == published);
    CHECK(slot.result()->source() == source);
    REQUIRE(slot.failure() != nullptr);
    // AND THE ANALYSIS IS NOT SOLVED-CURRENT WHILE ITS LATEST ATTEMPT FAILED,
    // even though the surviving result's own source still compares Current.
    CHECK(cfd::analysisState(channel.model.document(), channel.model.regenerator(), channel.model.mesher(),
                             channel.analysis, slot.result(), slot.failure()) == cfd::AnalysisState::SolveFailed);
}

TEST_CASE("CfdSteady_TheSettingsThatDroveTheRunAreTheONESTheFingerprintNames", "[cfd][steady]") {
    // ADR-077 Decision 7. The settings are not parameters, so they cannot
    // disagree with the fingerprint the result publishes. The observable form
    // of that: editing ONLY a setting moves the definition fingerprint, and the
    // result published before the edit is then Stale rather than Current.
    Channel channel;
    CfdResultSlot slot;
    REQUIRE(channel.solve(slot).has_value());
    REQUIRE(slot.result() != nullptr);
    const cfd::CfdDefinitionFingerprint before = slot.result()->source().definition;

    CfdAnalysisDefinition definition = channel.channelDefinition();
    definition.iteration.pressureRelaxation = 0.25;
    channel.store(definition);

    const Result<cfd::CfdResultSource> current = cfd::currentResultSource(
        channel.model.document(), channel.model.regenerator(), channel.model.mesher(), channel.analysis);
    REQUIRE(current.has_value());
    CHECK_FALSE(current->definition == before);
    CHECK(cfd::resultCurrency(slot.result(), *current) == cfd::ResultCurrency::Stale);
}

// ---------------------------------------------------------------------------
// The vocabulary, pinned whole
// ---------------------------------------------------------------------------

TEST_CASE("CfdSteady_TheProblemTableIsPinnedWholeAndTotal", "[cfd][steady]") {
    // ADR-075 Decision 9 assigned this mapping here, and a table is only worth
    // having if every row is asserted. Sixteen onto nine, deliberately not
    // injective.
    using cfd::CfdCouplingProblem;
    const std::array<std::pair<CfdCouplingProblem, CfdFailureStage>, 16> expected{{
        {CfdCouplingProblem::GeometryDescribesAnotherTopology, CfdFailureStage::IllPosed},
        {CfdCouplingProblem::BoundaryDescribesAnotherTopology, CfdFailureStage::IllPosed},
        {CfdCouplingProblem::TopologyNamesAnUnknownCell, CfdFailureStage::IllPosed},
        {CfdCouplingProblem::ValueLengthWrong, CfdFailureStage::IllPosed},
        {CfdCouplingProblem::ValueNotFinite, CfdFailureStage::NonFiniteField},
        {CfdCouplingProblem::SettingsInvalid, CfdFailureStage::InputRefused},
        {CfdCouplingProblem::MomentumSystemsWrong, CfdFailureStage::IllPosed},
        {CfdCouplingProblem::MomentumDiagonalNotPositive, CfdFailureStage::IllPosed},
        {CfdCouplingProblem::FaceWeightUnavailable, CfdFailureStage::IllPosed},
        {CfdCouplingProblem::PressureLevelNotFixed, CfdFailureStage::InputRefused},
        {CfdCouplingProblem::PrescribedFluxUnbalanced, CfdFailureStage::InputRefused},
        {CfdCouplingProblem::PressureMatrixNotSymmetric, CfdFailureStage::IllPosed},
        {CfdCouplingProblem::LinearSolveRefused, CfdFailureStage::LinearSolveFailed},
        {CfdCouplingProblem::LinearSolutionUnusable, CfdFailureStage::LinearSolveFailed},
        {CfdCouplingProblem::ResultIsNotFinite, CfdFailureStage::NonFiniteField},
        {CfdCouplingProblem::EvaluationIsInconsistent, CfdFailureStage::IllPosed},
    }};
    static_assert(expected.size() == cfd::kCfdCouplingProblems.size(),
                  "every CfdCouplingProblem must have a row here");

    for (const auto& [problem, stage] : expected) {
        INFO(cfd::toString(problem));
        CHECK(cfd::stageOf(problem) == stage);
    }

    // EVERY ENUMERATOR IS COVERED, counted rather than assumed: a seventeenth
    // upstream would fail the static_assert above, and a row for a problem that
    // does not exist would not compile.
    for (const CfdCouplingProblem problem : cfd::kCfdCouplingProblems) {
        const auto found = std::ranges::find_if(expected, [problem](const auto& row) { return row.first == problem; });
        INFO(cfd::toString(problem));
        CHECK(found != expected.end());
    }

    // TWO OF THE NINE STAGES ARE NOT PRODUCED BY THIS MAPPING, and that is
    // asserted rather than left to a reader. ADR-077 L8: MassBalanceFailed is
    // validation's (P19-VALID-001) and ContinuityNotClosed would need the flag
    // ADR-075 Decision 5 deleted as unfalsifiable.
    for (const CfdCouplingProblem problem : cfd::kCfdCouplingProblems) {
        CHECK(cfd::stageOf(problem) != CfdFailureStage::MassBalanceFailed);
        CHECK(cfd::stageOf(problem) != CfdFailureStage::ContinuityNotClosed);
        CHECK(cfd::stageOf(problem) != CfdFailureStage::MeshRefused);
    }
}

// ---------------------------------------------------------------------------
// The field container
// ---------------------------------------------------------------------------

TEST_CASE("CfdSteady_AFieldSetMustFitItsLayoutAndBeFinite", "[cfd][steady]") {
    Channel channel;
    const CfdFieldMap map = channel.layout();
    const std::size_t cells = map.size(CfdVariable::Pressure);
    const std::size_t components = map.size(CfdVariable::Velocity);
    const std::size_t faces = map.size(CfdVariable::MassFlux);
    REQUIRE(cells > 0);
    REQUIRE(components == 3 * cells);
    REQUIRE(faces > 0);

    const auto zeros = [](std::size_t n) { return std::vector<double>(n, 0.0); };

    SECTION("the right three lengths are accepted") {
        CHECK(CfdResultFields::create(map, zeros(cells), zeros(components), zeros(faces)).has_value());
    }
    SECTION("each wrong length is refused, and the message names which array") {
        CHECK_THAT(CfdResultFields::create(map, zeros(cells + 1), zeros(components), zeros(faces)).error().message,
                   ContainsSubstring("pressure array"));
        CHECK_THAT(CfdResultFields::create(map, zeros(cells), zeros(components - 1), zeros(faces)).error().message,
                   ContainsSubstring("velocity array"));
        CHECK_THAT(CfdResultFields::create(map, zeros(cells), zeros(components), zeros(faces + 3)).error().message,
                   ContainsSubstring("mass flux array"));
    }
    SECTION("a non-finite value is refused wherever it sits, and the index is named") {
        // EVERY ARRAY, NOT JUST THE PRESSURE. The probe campaign showed that the
        // mass-flux array could be dropped from the scan entirely -- M16
        // replaced it with the pressure array, so the flux was never
        // examined -- and no test noticed, because only the pressure was ever
        // given a NaN.
        for (const double bad : {std::numeric_limits<double>::quiet_NaN(),
                                 std::numeric_limits<double>::infinity(),
                                 -std::numeric_limits<double>::infinity()}) {
            INFO(bad);
            std::vector<double> pressure = zeros(cells);
            pressure.back() = bad;
            const Result<CfdResultFields> inPressure =
                CfdResultFields::create(map, pressure, zeros(components), zeros(faces));
            REQUIRE_FALSE(inPressure.has_value());
            CHECK_THAT(inPressure.error().message, ContainsSubstring("pressure["));
            CHECK_THAT(inPressure.error().message, ContainsSubstring(std::to_string(cells - 1)));

            std::vector<double> velocity = zeros(components);
            velocity.back() = bad;
            const Result<CfdResultFields> inVelocity =
                CfdResultFields::create(map, zeros(cells), velocity, zeros(faces));
            REQUIRE_FALSE(inVelocity.has_value());
            CHECK_THAT(inVelocity.error().message, ContainsSubstring("velocity["));

            std::vector<double> flux = zeros(faces);
            flux.back() = bad;
            const Result<CfdResultFields> inFlux =
                CfdResultFields::create(map, zeros(cells), zeros(components), flux);
            REQUIRE_FALSE(inFlux.has_value());
            CHECK_THAT(inFlux.error().message, ContainsSubstring("mass flux["));
            CHECK_THAT(inFlux.error().message, ContainsSubstring(std::to_string(faces - 1)));
        }
    }
    SECTION("the counts are read back off the arrays") {
        const Result<CfdResultFields> fields =
            CfdResultFields::create(map, zeros(cells), zeros(components), zeros(faces));
        REQUIRE(fields.has_value());
        CHECK(fields->cells() == cells);
        CHECK(fields->faces() == faces);
    }
}

// ---------------------------------------------------------------------------
// Gravity: the regression test for an adversarial review's one DEFECT
// ---------------------------------------------------------------------------

TEST_CASE("CfdSteady_GravityReachesTheMomentumEquation", "[cfd][steady]") {
    // THIS TEST FAILED BEFORE `momentum.gravity = definition.gravity` EXISTED,
    // and that is the point of it. An adversarial review found that
    // `solveCfdSteady` never passed the definition's gravity to
    // `assembleCfdMomentum`, while `prepareCfdBoundary` DID apply the
    // hydrostatic field at the prescribed-pressure facet. One treatment, two
    // halves (ADR-073 Decision 6), and only one of them was applied.
    //
    // WHAT DISCRIMINATES WAS MEASURED, AND IT IS NOT WHAT THIS COMMENT FIRST
    // SAID. Gravity is put ACROSS the flow, along -Y, while the channel runs
    // along +Z, and the fix was reverted to find out which assertion actually
    // catches it:
    //
    // ```text
    //                         with the fix      without it
    // dp/dy over the cells    -5.78e-05 Pa/m    -7.98e-06 Pa/m   <- CATCHES IT
    // ratio to -rho*g          1.158             0.160
    // sign of dp/dy            negative          negative        <- does not
    // max|Uy| / max|Uz|        0.081             0.075           <- does not
    // ```
    //
    // The first draft predicted that the SIGN would flip and that a spurious
    // transverse flow would appear. Both predictions were wrong: the outlet's
    // prescribed hydrostatic profile alone still produces a pressure that falls
    // with y, and it drives almost no transverse flow. What it cannot produce is
    // the full gradient -- it reaches only 16% of rho*g, because only the
    // boundary facet imposes it and nothing carries it into the interior.
    //
    // So the magnitude of dp/dy is the discriminator. The transverse ratio is
    // kept below as a confinement check rather than as evidence of the fix: it
    // says the flow stays axial, which is worth knowing and is not what catches
    // the defect.
    // THE MAGNITUDE IS SCALED, AND EARTH GRAVITY WAS TRIED FIRST. At
    // g = 9.81 m/s^2 the body force is rho*g = 9790 Pa/m against an inlet speed
    // of 1e-5 m/s, so the induced flow leaves the LOW CELL PECLET regime
    // `P19-PVCOUPLE-001` is qualified in and `assembleCfdMomentum` refuses the
    // run outright -- "the x momentum diagonal at cell 8 is -33.6 kg/s". That
    // refusal is the operator behaving correctly, so the test is wrong to ask
    // for Earth.
    //
    // What this case is about is whether the TERM is applied, not how strong it
    // is, so g is chosen to induce a flow of the same order as the inlet:
    // u ~ rho g L^2 / mu = 998 * 5e-8 * 1e-4 / 1.002e-3 ~ 5e-6 m/s. The Peclet
    // confinement is then asserted from the MEASURED velocity rather than from
    // the inlet alone, because gravity is now also driving the flow.
    constexpr double kGravitySi = 5.0e-8;

    Channel channel;
    CfdAnalysisDefinition definition = channel.channelDefinition();
    definition.gravity = cfd::CfdGravity{
        Acceleration3{Acceleration::fromSi(0.0), Acceleration::fromSi(-kGravitySi), Acceleration::fromSi(0.0)},
        0_mm};
    channel.store(definition);

    CfdResultSlot slot;
    const std::expected<CfdSteadyReport, CfdSolveFailure> report = channel.solve(slot);
    INFO((report.has_value() ? std::string{} : report.error().error.message));
    REQUIRE(report.has_value());
    REQUIRE(slot.result() != nullptr);

    const CfdResultFields& fields = slot.result()->fields();
    const std::size_t cells = fields.pressure().size();
    REQUIRE(cells > 0);
    REQUIRE(fields.velocity().size() == 3 * cells);

    // Component-blocked: component c occupies [c * cells, (c + 1) * cells).
    const std::span<const double> u = fields.velocity();
    double axial = 0.0;
    double transverse = 0.0;
    for (std::size_t cell = 0; cell < cells; ++cell) {
        axial = std::max(axial, std::abs(u[2 * cells + cell]));
        transverse = std::max(transverse, std::abs(u[1 * cells + cell]));
    }
    REQUIRE(axial > 0.0);
    // THE CONFINEMENT, FROM THE MEASURED FIELD. Conservative again: the domain's
    // longest side, so every real cell gives a smaller number than this.
    double fastest = 0.0;
    for (const double value : u) {
        fastest = std::max(fastest, std::abs(value));
    }
    const double measuredPeclet = kDensity.si() * fastest * kDomainLengthM / kViscosity.si();
    WARN("gravity across the flow: max |Uz| " << axial << ", max |Uy| " << transverse
         << ", ratio " << transverse / axial << ", measured Pe <= " << measuredPeclet);
    REQUIRE(measuredPeclet < 1.0);

    // A CONFINEMENT CHECK, NOT THE DISCRIMINATOR (see the table above): this
    // reads 0.081 with the fix and 0.075 without, so it cannot distinguish them.
    // It is kept because an axial channel whose transverse flow was a
    // comparable fraction of its axial flow would not be the problem this
    // fixture claims to pose.
    CHECK(transverse / axial < 0.25);

    // AND THE HYDROSTATIC SIGNATURE IS PRESENT IN THE PUBLISHED PRESSURE,
    // ESTIMATED BY A LEAST-SQUARES SLOPE RATHER THAN BY TWO CELLS.
    //
    // A first version compared the pressure at the extreme-y cells and asserted
    // the difference within a factor of two of rho*g*dy. It measured 4.55e-6 Pa
    // against 1.12e-6 -- right sign, 4.06x the magnitude -- because those two
    // cells sit at different z and so the difference also carries the axial
    // drop. The bound was a prediction and it was wrong.
    //
    // dp/dy over ALL cells is the right estimator: the axial variation is
    // uncorrelated with y across this mesh, so it enters the fit as scatter
    // rather than as bias. Gravity points along -Y, so pressure rises as y
    // falls and the slope must be about -rho*g.
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
    WARN("dp/dy measured " << slope << " Pa/m, -rho*g " << expected << " Pa/m, ratio "
         << slope / expected);
    // THE MAGNITUDE IS THE DISCRIMINATING HALF, measured: 1.158 with the fix
    // and 0.160 without. The sign check below passes either way and is kept only
    // because a positive slope would mean something else had gone wrong
    // entirely. The factor-of-two window holds 1.158 with room on both sides and
    // excludes 0.160 by a wide margin, so it is bounded by the defect's
    // signature rather than fitted to the passing number.
    CHECK(slope < 0.0);
    CHECK(slope / expected > 0.5);
    CHECK(slope / expected < 2.0);
}

TEST_CASE("CfdSteady_AClosedDomainPublishesAnUndefinedMassBalance", "[cfd][steady]") {
    // THE ONE PATH ON WHICH THE PUBLISHED METADATA IS NOT A MEASUREMENT, and it
    // was untested until an adversarial review pointed that out. The open
    // channel is what every other case here drives; this is the closed one.
    //
    // ADR-058's two mass-balance measures are relative to the BOUNDARY
    // THROUGHPUT, which is zero when every facet is a wall, so they are
    // UNDEFINED rather than small -- `CfdContinuity.hpp` says so and the
    // coupling leaves them defaulted for that reason. `CfdResultMetadata` has no
    // field that can say "undefined", so what reaches the slot is `(0, 0)`,
    // which READS AS A PERFECT BALANCE. ADR-077 L11 records that; this test
    // makes it observable so it cannot change unnoticed.
    //
    // Nothing here invents a number: what is asserted is that the published
    // value is exactly the evaluator's default and that the report's own flag
    // says it is undefined.
    Channel channel;

    // Every facet a no-slip wall, so nothing enters or leaves. Nothing then
    // fixes the pressure either, so the datum has to be a reference pressure --
    // which is the other half of why this path is worth driving.
    const cfd::CfdGeometry geometry = channel.geometry();
    const std::optional<Point3D> inside = geometry.cellCentroid(CellIndex::fromValue(0));
    REQUIRE(inside.has_value());

    CfdAnalysisDefinition definition = channel.channelDefinition();
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
    const std::expected<CfdSteadyReport, CfdSolveFailure> report = channel.solve(slot);
    INFO((report.has_value() ? std::string{} : report.error().error.message));
    REQUIRE(report.has_value());
    REQUIRE(slot.result() != nullptr);

    SECTION("the report says the domain is closed and the measures are undefined") {
        CHECK(report->closedDomain);
        CHECK_FALSE(report->massBalanceDefined);
    }
    SECTION("the published metadata carries the evaluator's default, which reads as a perfect balance") {
        // EXACTLY the default, not approximately: nothing is computed into it.
        CHECK(slot.result()->metadata().massBalance == cfd::CfdMassBalance{});
        // And it is accepted by the result's own gate, which is why this cannot
        // be caught by `CfdResult::create` and has to be a documented
        // limitation instead.
        CHECK(cfd::isWellFormed(slot.result()->metadata().massBalance));
    }
    SECTION("a closed domain at rest stays at rest, and that is what is published") {
        const CfdResultFields& fields = slot.result()->fields();
        // No inflow, no body force, walls everywhere: the only solution is zero
        // flow. If this ever reported motion, the closed domain would be
        // manufacturing it.
        CHECK(largestMagnitude(fields.velocity()) == 0.0);
        CHECK(largestMagnitude(fields.massFlux()) == 0.0);
    }
}

TEST_CASE("CfdSteady_ThePublishedFluxIsTheConservativeOne", "[cfd][steady][validation]") {
    // THE MOST IMPORTANT GAP THE PROBE CAMPAIGN FOUND. `M12` replaced the
    // published mass flux with the PRE-correction flux -- the predicted `phi*`
    // the momentum solve produces before the pressure correction -- and
    // SURVIVED the whole suite. Publishing that would publish a
    // NON-CONSERVATIVE field, which is the one thing SIMPLE's flux correction
    // exists to prevent, under a result that claims to be a solution.
    //
    // THE CHECK IS AN INDEPENDENT QUALIFIED OPERATOR, not an arithmetic repeat:
    // `divergeCfdMassFlux` is `P19-DIV-001`'s, it knows nothing about this
    // milestone, and it answers the question directly -- what is the per-cell
    // net flux of this field, and what is its global imbalance.
    //
    // This is also the independent validation of the SOLUTION that the evidence
    // otherwise lacked: the dense-reference comparison validates the SOLVE as
    // interchangeable, and a wrong loop would be wrong identically in both runs.
    // Conservation is a property of the published field alone.
    REQUIRE(conservativePeclet() < 1.0);

    Channel channel;
    CfdResultSlot slot;
    const std::expected<CfdSteadyReport, CfdSolveFailure> report = channel.solve(slot);
    INFO((report.has_value() ? std::string{} : report.error().error.message));
    REQUIRE(report.has_value());
    REQUIRE(slot.result() != nullptr);

    const cfd::CfdTopology topology = channel.topology();
    const cfd::CfdGeometry geometry = channel.geometry();
    const CfdResultFields& fields = slot.result()->fields();

    const Result<cfd::CfdMassFluxDivergenceField> published = cfd::divergeCfdMassFlux(
        topology, geometry, cfd::CfdMassFluxDivergenceInput{.faceFlux = fields.massFlux()});
    INFO((published.has_value() ? std::string{} : published.error().message));
    REQUIRE(published.has_value());

    // The scale to judge against is the boundary throughput the report carries,
    // which is the same denominator ADR-058 uses.
    REQUIRE(report->continuityScale > 0.0);
    const double worst = std::abs(published->largestMagnitude().si()) / report->continuityScale;
    const double imbalance = std::abs(published->globalImbalance().si()) / report->continuityScale;

    // AND THE SAME MEASURE ON THE FIELD M12 WOULD HAVE PUBLISHED, so the bound
    // below is bounded by the defect's signature rather than fitted to the
    // passing number. The predicted flux is `cfdMassFlux` of the published
    // velocity, which is exactly what the loop computes at the top of an
    // iteration.
    const Result<cfd::PreparedCfdBoundary> boundary = channel.boundary();
    REQUIRE(boundary.has_value());
    const std::expected<std::vector<double>, cfd::CfdConvectionFailure> predicted = cfd::cfdMassFlux(
        topology, geometry, *boundary,
        cfd::CfdMassFluxInput{.currentVelocity = fields.velocity(), .density = kDensity});
    REQUIRE(predicted.has_value());
    const Result<cfd::CfdMassFluxDivergenceField> uncorrected = cfd::divergeCfdMassFlux(
        topology, geometry, cfd::CfdMassFluxDivergenceInput{.faceFlux = *predicted});
    REQUIRE(uncorrected.has_value());
    const double worstUncorrected =
        std::abs(uncorrected->largestMagnitude().si()) / report->continuityScale;

    WARN("published flux: worst per-cell |sum phi| / throughput " << worst << ", global imbalance "
         << imbalance << "; the SAME measure on the pre-correction flux " << worstUncorrected);

    // The published field is conservative to about the continuity residual the
    // run stopped on, which is what ADR-075 Decision 5's identity says it can
    // be and no better. The pre-correction field is not.
    CHECK(worst <= 10.0 * report->residuals.continuity);
    CHECK(imbalance < 1.0e-6);
    CHECK(worst < worstUncorrected);
}

TEST_CASE("CfdSteady_EveryFailureStageHasItsOwnName", "[cfd][steady]") {
    // `M19` SWAPPED TWO `toString` NAMES AND SURVIVED: the existing test checks
    // that the nine strings are distinct, which a swap preserves. Pinning each
    // name is what catches it, and a name is what a user reads in a log.
    using cfd::CfdFailureStage;
    CHECK(cfd::toString(CfdFailureStage::InputRefused) == "input_refused");
    CHECK(cfd::toString(CfdFailureStage::MeshRefused) == "mesh_refused");
    CHECK(cfd::toString(CfdFailureStage::IllPosed) == "ill_posed");
    CHECK(cfd::toString(CfdFailureStage::LinearSolveFailed) == "linear_solve_failed");
    CHECK(cfd::toString(CfdFailureStage::Diverged) == "diverged");
    CHECK(cfd::toString(CfdFailureStage::IterationLimitReached) == "iteration_limit_reached");
    CHECK(cfd::toString(CfdFailureStage::NonFiniteField) == "non_finite_field");
    CHECK(cfd::toString(CfdFailureStage::ContinuityNotClosed) == "continuity_not_closed");
    CHECK(cfd::toString(CfdFailureStage::MassBalanceFailed) == "mass_balance_failed");

    // AND THE ROSTER IS IN DECLARATION ORDER, which `M22` reordered and the
    // existing size-and-distinctness checks could not see.
    REQUIRE(cfd::kCfdFailureStages.size() == 9);
    CHECK(cfd::kCfdFailureStages[0] == CfdFailureStage::InputRefused);
    CHECK(cfd::kCfdFailureStages[1] == CfdFailureStage::MeshRefused);
    CHECK(cfd::kCfdFailureStages[2] == CfdFailureStage::IllPosed);
    CHECK(cfd::kCfdFailureStages[3] == CfdFailureStage::LinearSolveFailed);
    CHECK(cfd::kCfdFailureStages[4] == CfdFailureStage::Diverged);
    CHECK(cfd::kCfdFailureStages[5] == CfdFailureStage::IterationLimitReached);
    CHECK(cfd::kCfdFailureStages[6] == CfdFailureStage::NonFiniteField);
    CHECK(cfd::kCfdFailureStages[7] == CfdFailureStage::ContinuityNotClosed);
    CHECK(cfd::kCfdFailureStages[8] == CfdFailureStage::MassBalanceFailed);
}

TEST_CASE("CfdSteady_AFieldSetIsHeldToBOTHCountsOfItsLayout", "[cfd][steady]") {
    // `M18` DROPPED THE FACE-COUNT HALF of `CfdResult::create`'s
    // fields-fit-this-layout guard -- `A || B` where no test fired `B` -- and
    // survived. Firing it alone needs a field set whose CELL count is right and
    // whose FACE count is wrong, which `CfdResultFields::create` will not build
    // against the same layout. So it is built against a DIFFERENT layout: a
    // coarser mesh with the same cell count would be ideal and is not available,
    // so the test asserts the reachable half of the invariant directly.
    Channel channel;
    const CfdFieldMap map = channel.layout();
    const std::size_t cells = map.size(CfdVariable::Pressure);
    const std::size_t faces = map.size(CfdVariable::MassFlux);
    REQUIRE(cells > 0);
    REQUIRE(faces > 0);
    REQUIRE(faces != cells);

    // A field set carries its own two counts, read back off its arrays, and
    // those are what `CfdResult::create` compares against the layout. Pinning
    // both says the comparison has two halves whatever fires them.
    const Result<CfdResultFields> good =
        CfdResultFields::create(map, std::vector<double>(cells, 0.0),
                                std::vector<double>(3 * cells, 0.0), std::vector<double>(faces, 0.0));
    REQUIRE(good.has_value());
    CHECK(good->cells() == map.cells());
    CHECK(good->faces() == map.faces());
    // The two counts are independent: a guard that checked only one would let a
    // field set from another mesh through whenever that mesh had the same cell
    // count. Recorded as ADR-077 L15, because this suite cannot build such a
    // pair from one fixture.
    CHECK(good->cells() != good->faces());
}

TEST_CASE("CfdSteady_AFailureForAnAnalysisThatIsGoneReportsNoAnalysis", "[cfd][steady]") {
    // `M20` REORDERED `analysisState`'s FIRST TWO CHECKS AND SURVIVED. The
    // documented precedence is that `NoAnalysis` outranks everything, including
    // `SolveFailed` -- `CfdResult.hpp` says `SolveFailed` "outranks every state
    // but `NoAnalysis`" -- and nothing exercised the one combination that can
    // tell the two orders apart: a held failure for an analysis the document no
    // longer has.
    //
    // THE CODE IS P19-DATA-001's AND THE GAP WAS ITS SUITE'S, but this milestone
    // is `CfdResultSlot`'s first production consumer and the first thing that
    // can produce a real failure to hold, so the test belongs here.
    Channel channel;
    CfdAnalysisDefinition definition = channel.channelDefinition();
    definition.steady = cfd::CfdSteadySettings{.maxIterations = 1};
    channel.store(definition);

    CfdResultSlot slot;
    REQUIRE_FALSE(channel.solve(slot).has_value());
    REQUIRE(slot.failure() != nullptr);

    // A failure is held, and the analysis asked about does not exist.
    CHECK(cfd::analysisState(channel.model.document(), channel.model.regenerator(), channel.model.mesher(),
                             CfdAnalysisId::fromValue(987654), slot.result(), slot.failure()) ==
          cfd::AnalysisState::NoAnalysis);
    // While for the analysis that DOES exist, the same held failure is
    // SolveFailed -- so the two orders really are distinguishable and this
    // pins which one is in force.
    CHECK(cfd::analysisState(channel.model.document(), channel.model.regenerator(), channel.model.mesher(),
                             channel.analysis, slot.result(), slot.failure()) ==
          cfd::AnalysisState::SolveFailed);
}

TEST_CASE("CfdSteady_TheReportedIterationCountIsCountedIndependently", "[cfd][steady]") {
    // `M2` MADE `report.outerIterations` OFF BY ONE AND SURVIVED TWICE. The
    // first attempt to close it asserted `outerIterations <= maxIterations`,
    // which is useless when the budget is 400 and the run converges in a few
    // dozen: `completed + 1` is still under the budget. That prediction was
    // wrong and the probe said so.
    //
    // THE SOLVE PORT IS AN INDEPENDENT COUNTER. Every outer iteration performs a
    // fixed number of linear solves -- three momentum systems in the predictor
    // and one pressure correction -- so counting the port's calls counts the
    // iterations without consulting the orchestrator's own bookkeeping. The
    // ratio is measured below rather than assumed, and then pinned.
    REQUIRE(conservativePeclet() < 1.0);

    Channel channel;
    std::size_t calls = 0;
    const CfdLinearSolve counting = [&calls](const CfdSparseMatrix& matrix, std::span<const double> rhs,
                                             const CfdLinearSolverSettings& settings)
        -> std::expected<CfdLinearSolveResult, Error> {
        ++calls;
        return cfd::cfdLinearSolve()(matrix, rhs, settings);
    };

    CfdResultSlot slot;
    const std::expected<CfdSteadyReport, CfdSolveFailure> report = channel.solve(slot, counting);
    INFO((report.has_value() ? std::string{} : report.error().error.message));
    REQUIRE(report.has_value());
    REQUIRE(report->outerIterations > 1);

    WARN("solve-port calls " << calls << " over " << report->outerIterations
         << " reported iterations, ratio " << (static_cast<double>(calls) /
                                               static_cast<double>(report->outerIterations)));

    // FOUR PER ITERATION: three momentum components plus one pressure
    // correction, which is `advanceCfdCoupling`'s documented sequence --
    // "1 predictor relax the three momentum systems, solve" and "4 solve the
    // supplied port". An off-by-one in the reported count breaks this exactly.
    CHECK(calls == 4 * static_cast<std::size_t>(report->outerIterations));

    // AND THE PUBLISHED METADATA CARRIES THE SAME COUNT, so the number a reader
    // sees on the result is the one the port counted.
    REQUIRE(slot.result() != nullptr);
    CHECK(slot.result()->metadata().outerIterations == report->outerIterations);
}
