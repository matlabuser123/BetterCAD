// P18-CLI-001: the production entry that runs one thermal analysis (`thermal::runThermalAnalysis`).
//
// WHAT THESE TESTS ARE FOR. The entry exists so that a headless command line is an ADAPTER over the qualified chain instead of a
// second copy of it. So the claim worth testing is not "it returns something" but three equivalences and one refusal discipline:
//
//   1. IT IS THE QUALIFIED CHAIN. The ORACLE is `vztest::derive`, the chain the viewer milestone wrote beside its tests out of the
//      same qualified functions; every array, the post result, the balance and the validation report must be identical to it, for a
//      steady analysis and for a transient one. The two were written separately, so agreement is evidence that the order, the
//      arguments and the definition they read are right, not an echo.
//   2. IT ACCEPTS EXACTLY WHAT P18-VALID ACCEPTS, and no more: `accepted()` is the validator's verdict AND a completed chain.
//   3. IT NEVER SOLVES WHAT MUST NOT BE SOLVED. A pure-Neumann steady model, an unresolved face, a conflicting pair of temperatures
//      and a material without what the mode needs each stop the chain at the link that owns the refusal, and the report names the
//      rule. Nothing is regularised, rebound or defaulted.
//   4. IT IS READ-ONLY. The document, the analysis revision and the mesh stamp are the same after a run.
//
// Every expectation about a refusal is a code from the validator (`ValidationCode`), never a substring of English.

#include "renderer/ThermalVizTestSupport.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/thermal/ThermalAnalysisRun.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using thermal::ThermalAnalysisMode;
using thermal::ThermalAnalysisRun;
using thermal::ThermalBoundaryCondition;
using thermal::ThermalRunStage;
using thermal::ValidationCode;
using vztest::VizStudy;

namespace {

[[nodiscard]] bool hasCode(const thermal::ThermalValidationReport& report, ValidationCode code) {
    return std::ranges::any_of(report.findings, [code](const thermal::ValidationFinding& f) { return f.code == code; });
}

/// Bit equality, not value equality: -0.0 and +0.0, or two NaNs, must not hide a difference.
[[nodiscard]] bool sameBits(std::span<const Temperature> a, std::span<const Temperature> b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::bit_cast<std::uint64_t>(a[i].si()) != std::bit_cast<std::uint64_t>(b[i].si())) {
            return false;
        }
    }
    return true;
}

/// Every product of the run against every product of the oracle chain.
void requireSameAsTheQualifiedChain(const ThermalAnalysisRun& run, const vztest::Derived& oracle, const VizStudy& study) {
    REQUIRE_FALSE(run.stoppedAt.has_value());
    REQUIRE(run.dofMap.has_value());
    CHECK(std::ranges::equal(run.dofMap->nodes(), oracle.map.nodes()));
    REQUIRE(run.loads.has_value());
    REQUIRE(run.boundary.has_value());
    REQUIRE(run.system.has_value());
    REQUIRE(oracle.system.has_value());

    if (oracle.steady.has_value()) {
        REQUIRE(run.steady.has_value());
        CHECK_FALSE(run.transient.has_value());
        CHECK(sameBits(run.steady->temperatures(), oracle.steady->temperatures()));
        CHECK(run.steady->residual() == oracle.steady->residual());
        CHECK(std::bit_cast<std::uint64_t>(run.steady->pivotRatio()) ==
              std::bit_cast<std::uint64_t>(oracle.steady->pivotRatio()));
    } else {
        REQUIRE(oracle.transient.has_value());
        REQUIRE(run.transient.has_value());
        CHECK_FALSE(run.steady.has_value());
        REQUIRE(run.transient->sampleCount() == oracle.transient->sampleCount());
        for (std::size_t i = 0; i < run.transient->sampleCount(); ++i) {
            CHECK(std::bit_cast<std::uint64_t>(run.transient->timeOf(i).si()) ==
                  std::bit_cast<std::uint64_t>(oracle.transient->timeOf(i).si()));
            CHECK(sameBits(run.transient->temperaturesAt(i), oracle.transient->temperaturesAt(i)));
        }
    }
    REQUIRE(run.post.has_value());
    REQUIRE(oracle.post.has_value());
    CHECK(*run.post == *oracle.post);
    REQUIRE(run.balance.has_value());
    REQUIRE(oracle.balance.has_value());
    CHECK(*run.balance == *oracle.balance);
    CHECK(run.validation == vztest::validationOf(study, oracle));
}

void editExtrudeDepth(VizStudy& study, double metres) {
    REQUIRE(study.document
                .modifyObject<features::ExtrudeFeature>(
                    study.feature,
                    [metres](features::ExtrudeFeature& extrude) -> Result<bool> {
                        auto definition = extrude.definition();
                        definition.depth = Length::fromSi(metres);
                        return extrude.setDefinition(definition).has_value();
                    })
                .has_value());
    bettercad::test::requireReport(study.regenerator, study.document);
}

} // namespace

// ---------------------------------------------------------------------------
// It is the qualified chain
// ---------------------------------------------------------------------------

TEST_CASE("ThermalRun_SteadyIsTheQualifiedChainBitForBit", "[thermal][run][equivalence]") {
    VizStudy study;
    vztest::multiFree(study);
    const std::unique_ptr<vztest::Derived> oracle = vztest::derive(study);
    REQUIRE(oracle->balance.has_value());

    const ThermalAnalysisRun run = thermal::runThermalAnalysis(study.document, study.regenerator, study.mesher, study.analysis);
    CHECK(run.mode == ThermalAnalysisMode::Steady);
    CHECK(run.analysis == study.analysis);
    requireSameAsTheQualifiedChain(run, *oracle, study);
    CHECK(run.accepted());
    CHECK(run.validation.status() == thermal::ValidationStatus::Pass);
}

TEST_CASE("ThermalRun_TransientIsTheQualifiedChainBitForBit", "[thermal][run][equivalence][transient]") {
    VizStudy study;
    vztest::define(study, vztest::transientDefinition(
                              study, {vztest::generationLoad(1, 2.0e5)},
                              {vztest::temperatureBc(1, study.startCap(), 300.0),
                               vztest::convectionBc(2, study.endCap(), 25.0, vztest::kAmbient)},
                              293.15, 0.5, 5.0));
    const std::unique_ptr<vztest::Derived> oracle = vztest::derive(study);
    REQUIRE(oracle->balance.has_value());

    const ThermalAnalysisRun run = thermal::runThermalAnalysis(study.document, study.regenerator, study.mesher, study.analysis);
    CHECK(run.mode == ThermalAnalysisMode::Transient);
    requireSameAsTheQualifiedChain(run, *oracle, study);
    CHECK(run.accepted());
    CHECK(run.transient->sampleCount() == 11U);   // t0 and ten steps
}

TEST_CASE("ThermalRun_TwoRunsOfOneStateAreIdentical", "[thermal][run][determinism]") {
    VizStudy study;
    vztest::multiFree(study);
    const ThermalAnalysisRun a = thermal::runThermalAnalysis(study.document, study.regenerator, study.mesher, study.analysis);
    const ThermalAnalysisRun b = thermal::runThermalAnalysis(study.document, study.regenerator, study.mesher, study.analysis);
    REQUIRE(a.accepted());
    REQUIRE(b.accepted());
    CHECK(sameBits(a.steady->temperatures(), b.steady->temperatures()));
    CHECK(*a.post == *b.post);
    CHECK(*a.balance == *b.balance);
    CHECK(a.validation == b.validation);
}

TEST_CASE("ThermalRun_ModeIsTheDefinitionsAndOnlyItsSolutionIsBuilt", "[thermal][run][mode]") {
    VizStudy study;
    vztest::multiFree(study);
    const ThermalAnalysisRun steady = thermal::runThermalAnalysis(study.document, study.regenerator, study.mesher, study.analysis);
    CHECK(steady.steady.has_value());
    CHECK_FALSE(steady.transient.has_value());

    vztest::define(study, vztest::transientDefinition(study, {vztest::generationLoad(1, 1.0e5)},
                                                      {vztest::temperatureBc(1, study.startCap(), 300.0)}, 293.15, 1.0, 3.0));
    const ThermalAnalysisRun transient =
        thermal::runThermalAnalysis(study.document, study.regenerator, study.mesher, study.analysis);
    CHECK(transient.transient.has_value());
    CHECK_FALSE(transient.steady.has_value());
}

// ---------------------------------------------------------------------------
// Acceptance is the validator's
// ---------------------------------------------------------------------------

TEST_CASE("ThermalRun_AcceptedIsACompletedChainAndTheValidatorsVerdict", "[thermal][run][acceptance]") {
    VizStudy study;
    vztest::multiFree(study);
    const ThermalAnalysisRun run = thermal::runThermalAnalysis(study.document, study.regenerator, study.mesher, study.analysis);
    REQUIRE(run.accepted());
    CHECK(run.validation.accepted());
    CHECK_FALSE(run.stoppedAt.has_value());
    CHECK_FALSE(run.failure.has_value());
    // The validator re-judged the result: every rule evaluated and none skipped.
    for (const thermal::ThermalCheck& check : run.validation.checks) {
        CHECK(check.state != thermal::CheckState::Skipped);
    }
}

TEST_CASE("ThermalRun_ARefusedChainIsNeverAccepted", "[thermal][run][acceptance]") {
    VizStudy study;
    // Generation and nothing to hold the temperature: pure Neumann.
    vztest::define(study, vztest::steadyDefinition(study, {vztest::generationLoad(1, 1.0e5)}, {}));
    const ThermalAnalysisRun run = thermal::runThermalAnalysis(study.document, study.regenerator, study.mesher, study.analysis);
    CHECK_FALSE(run.accepted());
    REQUIRE(run.stoppedAt.has_value());
    REQUIRE(run.failure.has_value());
    CHECK(run.validation.status() == thermal::ValidationStatus::Fail);
    CHECK_FALSE(run.validation.accepted());
}

// ---------------------------------------------------------------------------
// Well posedness
// ---------------------------------------------------------------------------

TEST_CASE("ThermalRun_PureNeumannSteadyIsRefusedWithTheDatumDiagnosticAndNeverRegularised",
          "[thermal][run][well-posed][singular]") {
    VizStudy study;
    vztest::define(study, vztest::steadyDefinition(study, {vztest::generationLoad(1, 1.0e5)}, {}));
    const ThermalAnalysisRun run = thermal::runThermalAnalysis(study.document, study.regenerator, study.mesher, study.analysis);
    REQUIRE(run.stoppedAt.has_value());
    CHECK(*run.stoppedAt == ThermalRunStage::Solve);
    CHECK_FALSE(run.steady.has_value());
    CHECK_FALSE(run.post.has_value());
    CHECK_FALSE(run.balance.has_value());
    CHECK(run.system.has_value());   // assembled, then refused by the solve's own anchoring check
    CHECK(hasCode(run.validation, ValidationCode::NoTemperatureDatum));
    CHECK(run.validation.statusOf(thermal::ValidationRule::WellPosed) == thermal::CheckStatus::Fail);
}

TEST_CASE("ThermalRun_RobinOnlySteadyIsAccepted", "[thermal][run][well-posed]") {
    VizStudy study;
    // A convection condition anchors the model to an absolute temperature: it needs no prescribed temperature.
    vztest::define(study, vztest::steadyDefinition(study, {vztest::generationLoad(1, 1.0e5)},
                                                   {vztest::convectionBc(1, study.endCap(), 25.0, vztest::kAmbient)}));
    const ThermalAnalysisRun run = thermal::runThermalAnalysis(study.document, study.regenerator, study.mesher, study.analysis);
    CHECK(run.accepted());
    REQUIRE(run.steady.has_value());
    CHECK(run.steady->constrainedCount() == 0U);
}

TEST_CASE("ThermalRun_FullyConstrainedSteadyIsAccepted", "[thermal][run][well-posed]") {
    VizStudy study;
    // A THIN PLATE: with the global target far larger than the thickness the mesh has no interior node, so holding every face holds
    // every unknown. (The 20 mm block has one interior node, which would leave one free equation and test something else.)
    editExtrudeDepth(study, 0.001);
    study.remesh();
    // ONE VALUE ON EVERY FACE: faces share edges and corners, and two different temperatures on a shared node are a conflict the
    // core refuses (ThermalRun_ConflictingPrescribedTemperatures...), so a field that differs per face would not be a model.
    std::vector<ThermalBoundaryCondition> held{vztest::temperatureBc(1, study.startCap(), 320.0),
                                               vztest::temperatureBc(2, study.endCap(), 320.0)};
    for (std::size_t side = 0; side < 4; ++side) {
        held.push_back(vztest::temperatureBc(3 + side, study.side(side), 320.0));
    }
    vztest::define(study, vztest::steadyDefinition(study, {}, held));
    const ThermalAnalysisRun run = thermal::runThermalAnalysis(study.document, study.regenerator, study.mesher, study.analysis);
    CHECK(run.accepted());
    REQUIRE(run.steady.has_value());
    CHECK(run.steady->freeEquations() == 0U);
}

TEST_CASE("ThermalRun_PureNeumannTransientIsAcceptedWhenTheCapacityIsPositive", "[thermal][run][well-posed][transient]") {
    VizStudy study;
    // The steady datum rule must not be applied to a transient: heat with nowhere to go raises the temperature, and that is well
    // posed because the capacity holds it.
    vztest::define(study, vztest::transientDefinition(study, {vztest::generationLoad(1, 1.0e5)}, {}, 300.0, 1.0, 4.0));
    const ThermalAnalysisRun run = thermal::runThermalAnalysis(study.document, study.regenerator, study.mesher, study.analysis);
    CHECK(run.accepted());
    CHECK_FALSE(hasCode(run.validation, ValidationCode::NoTemperatureDatum));
    CHECK(hasCode(run.validation, ValidationCode::TransientWithoutDatum));
}

// ---------------------------------------------------------------------------
// Time comes from the core
// ---------------------------------------------------------------------------

TEST_CASE("ThermalRun_TransientTimesAreTheCoresAndTheLastStepIsPartial", "[thermal][run][transient][time]") {
    VizStudy study;
    vztest::define(study, vztest::transientDefinition(
                              study, {vztest::generationLoad(1, 1.0e5)}, {vztest::temperatureBc(1, study.startCap(), 300.0)}, 300.0,
                              0.3, 1.0));
    const ThermalAnalysisRun run = thermal::runThermalAnalysis(study.document, study.regenerator, study.mesher, study.analysis);
    REQUIRE(run.accepted());
    REQUIRE(run.transient.has_value());

    const auto grid = thermal::TimeGrid::create(0.3, 1.0);
    REQUIRE(grid.has_value());
    const std::vector<double> expected = grid->times();
    REQUIRE(run.transient->sampleCount() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CHECK(std::bit_cast<std::uint64_t>(run.transient->timeOf(i).si()) == std::bit_cast<std::uint64_t>(expected[i]));
    }
    // 0.3 does not divide 1.0: the run lands ON the end time, not on 1.2.
    CHECK(grid->hasPartialFinalStep());
    CHECK(run.transient->timeOf(run.transient->sampleCount() - 1U).si() == 1.0);
    CHECK(run.transient->sampleCount() == 5U);
}

// ---------------------------------------------------------------------------
// The material
// ---------------------------------------------------------------------------

TEST_CASE("ThermalRun_NoMaterialIsRefusedAndNoneIsDefaulted", "[thermal][run][material]") {
    VizStudy study;
    vztest::multiFree(study);
    REQUIRE(features::removeMaterialAssignment(study.document).has_value());
    const ThermalAnalysisRun run = thermal::runThermalAnalysis(study.document, study.regenerator, study.mesher, study.analysis);
    CHECK_FALSE(run.accepted());
    REQUIRE(run.stoppedAt.has_value());
    CHECK(*run.stoppedAt == ThermalRunStage::Model);
    CHECK_FALSE(run.dofMap.has_value());
    CHECK_FALSE(run.steady.has_value());
    CHECK(hasCode(run.validation, ValidationCode::MaterialMissing));
}

TEST_CASE("ThermalRun_SteadyNeedsOnlyConductivityAndTransientNeedsMore", "[thermal][run][material][mode]") {
    VizStudy study;
    materials::ThermalProperties onlyK;
    onlyK.thermalConductivity = materials::MaterialProperty<ThermalConductivity>::known(45_W_per_m_K);
    REQUIRE(features::setMaterialThermal(study.document, study.material, onlyK).has_value());

    vztest::multiFree(study);
    const ThermalAnalysisRun steady = thermal::runThermalAnalysis(study.document, study.regenerator, study.mesher, study.analysis);
    CHECK(steady.accepted());

    vztest::define(study, vztest::transientDefinition(study, {vztest::generationLoad(1, 1.0e5)},
                                                      {vztest::temperatureBc(1, study.startCap(), 300.0)}, 293.15, 1.0, 3.0));
    const ThermalAnalysisRun transient =
        thermal::runThermalAnalysis(study.document, study.regenerator, study.mesher, study.analysis);
    CHECK_FALSE(transient.accepted());
    CHECK_FALSE(transient.transient.has_value());
    CHECK(hasCode(transient.validation, ValidationCode::MaterialIncompleteForMode));
}

// ---------------------------------------------------------------------------
// Intent that cannot be acted on
// ---------------------------------------------------------------------------

TEST_CASE("ThermalRun_AnUnresolvedFaceIsRefusedAtTheLoadsAndNeverRebound", "[thermal][run][reference]") {
    VizStudy study;
    // Well formed, and naming a side no profile entity generates.
    const FaceName nowhere{study.feature, FaceSelector{.role = FaceRole::Side, .entity = EntityId::fromValue(9991U)}};
    vztest::define(study, vztest::steadyDefinition(study, {vztest::surfaceLoad(1, nowhere, 500.0)},
                                                   {vztest::temperatureBc(1, study.startCap(), 300.0)}));
    const ThermalAnalysisRun run = thermal::runThermalAnalysis(study.document, study.regenerator, study.mesher, study.analysis);
    CHECK_FALSE(run.accepted());
    REQUIRE(run.stoppedAt.has_value());
    CHECK(*run.stoppedAt == ThermalRunStage::Loads);
    CHECK_FALSE(run.loads.has_value());
    CHECK_FALSE(run.steady.has_value());
    CHECK(hasCode(run.validation, ValidationCode::LoadsInvalid));
    // The canonical reference is exactly what the user wrote.
    CHECK(study.definition().loads.front().target() == nowhere);
}

TEST_CASE("ThermalRun_ConflictingPrescribedTemperaturesAreRefusedNotResolvedByOrder", "[thermal][run][boundary]") {
    VizStudy study;
    vztest::define(study, vztest::steadyDefinition(study, {},
                                                   {vztest::temperatureBc(1, study.startCap(), 300.0),
                                                    vztest::temperatureBc(2, study.startCap(), 350.0)}));
    const ThermalAnalysisRun run = thermal::runThermalAnalysis(study.document, study.regenerator, study.mesher, study.analysis);
    CHECK_FALSE(run.accepted());
    REQUIRE(run.stoppedAt.has_value());
    CHECK(*run.stoppedAt == ThermalRunStage::Boundary);
    CHECK_FALSE(run.steady.has_value());
    CHECK(hasCode(run.validation, ValidationCode::BoundaryConditionsInvalid));
}

TEST_CASE("ThermalRun_AnUnknownAnalysisIsRefused", "[thermal][run][analysis]") {
    VizStudy study;
    vztest::multiFree(study);
    const ThermalAnalysisRun run =
        thermal::runThermalAnalysis(study.document, study.regenerator, study.mesher, ThermalAnalysisId::fromValue(9999U));
    CHECK_FALSE(run.accepted());
    REQUIRE(run.stoppedAt.has_value());
    CHECK(*run.stoppedAt == ThermalRunStage::Model);
    REQUIRE(run.failure.has_value());
    CHECK(run.failure->code == ErrorCode::NotFound);
    CHECK(hasCode(run.validation, ValidationCode::AnalysisNotFound));
}

// ---------------------------------------------------------------------------
// Currentness
// ---------------------------------------------------------------------------

TEST_CASE("ThermalRun_ARunOfAnEditedGeometryIsStaleAndTheEditedGeometryIsNotRunOnTheOldMesh",
          "[thermal][run][currentness][stale]") {
    VizStudy study;
    vztest::multiFree(study);
    const ThermalAnalysisRun before = thermal::runThermalAnalysis(study.document, study.regenerator, study.mesher, study.analysis);
    REQUIRE(before.accepted());
    REQUIRE(thermal::isCurrent(before.validation, study.document, study.regenerator, study.mesher));

    // The geometry moved and was regenerated; the mesh held was built for the old body. NOTHING IS REMESHED.
    editExtrudeDepth(study, 0.035);
    CHECK_FALSE(thermal::isCurrent(before.validation, study.document, study.regenerator, study.mesher));

    const ThermalAnalysisRun after = thermal::runThermalAnalysis(study.document, study.regenerator, study.mesher, study.analysis);
    CHECK_FALSE(after.accepted());
    REQUIRE(after.stoppedAt.has_value());
    CHECK(*after.stoppedAt == ThermalRunStage::Model);
    CHECK(hasCode(after.validation, ValidationCode::MeshStale));
    // Nothing was published from the stale mesh.
    CHECK_FALSE(after.post.has_value());
    CHECK_FALSE(after.balance.has_value());
    // The earlier run is unchanged: it describes what the model WAS, and says so through its report.
    CHECK(before.accepted());
}

TEST_CASE("ThermalRun_ARemeshWithTheSameCountsAndIdsStillMakesTheOldRunStale", "[thermal][run][currentness][stale][remesh]") {
    VizStudy study;
    vztest::multiFree(study);
    const ThermalAnalysisRun before = thermal::runThermalAnalysis(study.document, study.regenerator, study.mesher, study.analysis);
    REQUIRE(before.accepted());
    const std::size_t nodes = study.mesh().nodes().size();
    const std::size_t tets = study.mesh().tetrahedra().size();

    study.remesh();
    // The same body, the same intent: same counts. A different mesh all the same.
    CHECK(study.mesh().nodes().size() == nodes);
    CHECK(study.mesh().tetrahedra().size() == tets);
    CHECK_FALSE(thermal::isCurrent(before.validation, study.document, study.regenerator, study.mesher));

    // A fresh run is of the new mesh and is accepted; the old one stays what it was.
    const ThermalAnalysisRun fresh = thermal::runThermalAnalysis(study.document, study.regenerator, study.mesher, study.analysis);
    CHECK(fresh.accepted());
    CHECK(thermal::isCurrent(fresh.validation, study.document, study.regenerator, study.mesher));
    CHECK_FALSE(before.validation == fresh.validation);
}

TEST_CASE("ThermalRun_AnEditAfterARunMakesItStaleAndAFailedRerunPublishesNothing", "[thermal][run][currentness][stale]") {
    VizStudy study;
    vztest::multiFree(study);
    const ThermalAnalysisRun first = thermal::runThermalAnalysis(study.document, study.regenerator, study.mesher, study.analysis);
    REQUIRE(first.accepted());

    // A canonical edit that makes the next run fail: a second temperature on a held face.
    thermal::ThermalAnalysisDefinition edited = study.definition();
    edited.boundaryConditions.push_back(vztest::temperatureBc(9, study.startCap(), 123.0));
    vztest::define(study, edited);
    CHECK_FALSE(thermal::isCurrent(first.validation, study.document, study.regenerator, study.mesher));

    const ThermalAnalysisRun second = thermal::runThermalAnalysis(study.document, study.regenerator, study.mesher, study.analysis);
    CHECK_FALSE(second.accepted());
    CHECK_FALSE(second.steady.has_value());
    CHECK_FALSE(second.post.has_value());
    CHECK_FALSE(second.balance.has_value());
    // A caller that kept the first run still holds the first run, which is stale and which the second does not revive.
    CHECK_FALSE(thermal::isCurrent(first.validation, study.document, study.regenerator, study.mesher));
}

TEST_CASE("ThermalRun_ARunEditsNothing", "[thermal][run][read-only]") {
    VizStudy study;
    vztest::multiFree(study);
    const thermal::ThermalAnalysis* object = thermal::findThermalAnalysis(study.document, study.analysis);
    REQUIRE(object != nullptr);
    const std::uint64_t revision = object->revision();
    const thermal::ThermalAnalysisDefinition definition = object->definition();
    const meshing::MeshStamp stamp = study.mesh().stamp();

    const ThermalAnalysisRun run = thermal::runThermalAnalysis(study.document, study.regenerator, study.mesher, study.analysis);
    REQUIRE(run.accepted());

    const thermal::ThermalAnalysis* after = thermal::findThermalAnalysis(study.document, study.analysis);
    REQUIRE(after != nullptr);
    CHECK(after->revision() == revision);
    CHECK(after->definition() == definition);
    CHECK(study.mesh().stamp() == stamp);
}
